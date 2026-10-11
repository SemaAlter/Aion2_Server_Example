#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windivert.h>

#include <array>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr const char* europe_lobby = "193.202.112.93";
constexpr unsigned short lobby_port = 13700;

const char* help_text = R"(Sema Redirector - configured AION2 region routing

Usage:
  Sema.Redirector.exe --config <regions.ini> [--target <Sema-server-IPv4>]
  Sema.Redirector.exe --target <Sema-server-IPv4>

Options:
  --config <path>       Load [region.*] routes from an INI file
  --target <IPv4>       Override every configured local target address
  --list-regions        Validate the configuration and print enabled routes
  --official <IPv4>     Legacy single-route official lobby
  --port <number>       Legacy single-route lobby port (default: 13700)
  --help                Show this help

This program must run as Administrator when routing is active. It changes only
packets for configured lobby endpoints and restores normal routing when it exits.
)";

struct region_route {
    std::string id;
    std::string client_key;
    std::string display_name;
    std::string official_host;
    unsigned short official_port = lobby_port;
    std::string target_host;
    unsigned short target_port{};
    bool enabled = true;
    UINT32 official_address{};
    UINT32 target_address{};
};

struct options {
    std::optional<std::filesystem::path> config_path;
    std::optional<std::string> target_override;
    std::optional<std::string> legacy_official;
    std::optional<unsigned short> legacy_port;
    bool list_regions = false;
    bool show_help = false;
};

class winsock_session {
public:
    winsock_session() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
    }
    ~winsock_session() { WSACleanup(); }
    winsock_session(const winsock_session&) = delete;
    winsock_session& operator=(const winsock_session&) = delete;
};

std::atomic_bool stopping = false;
HANDLE divert_handle = INVALID_HANDLE_VALUE;

BOOL WINAPI console_handler(DWORD) {
    stopping = true;
    if (divert_handle != INVALID_HANDLE_VALUE) WinDivertShutdown(divert_handle, WINDIVERT_SHUTDOWN_BOTH);
    return TRUE;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

unsigned short parse_port(const std::string& value) {
    unsigned port = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port == 0 || port > 65535) {
        throw std::invalid_argument("invalid TCP port: " + value);
    }
    return static_cast<unsigned short>(port);
}

bool parse_bool(const std::string& value, const std::string& label) {
    if (value == "true" || value == "1" || value == "yes") return true;
    if (value == "false" || value == "0" || value == "no") return false;
    throw std::invalid_argument(label + " must be true or false");
}

UINT32 parse_ipv4(const std::string& value, const char* label) {
    IN_ADDR address{};
    if (InetPtonA(AF_INET, value.c_str(), &address) != 1) {
        throw std::invalid_argument(std::string("invalid ") + label + " IPv4 address: " + value);
    }
    return address.S_un.S_addr;
}

options parse_options(int argc, char** argv) {
    options result;
    const auto value_after = [&](int& index, const std::string& option) -> std::string {
        if (++index >= argc) throw std::invalid_argument(option + " requires a value");
        return argv[index];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--config") result.config_path = value_after(i, argument);
        else if (argument == "--target") result.target_override = value_after(i, argument);
        else if (argument == "--official") result.legacy_official = value_after(i, argument);
        else if (argument == "--port") result.legacy_port = parse_port(value_after(i, argument));
        else if (argument == "--list-regions") result.list_regions = true;
        else if (argument == "--help" || argument == "-h") result.show_help = true;
        else throw std::invalid_argument("unknown option: " + argument);
    }
    if (result.target_override) parse_ipv4(*result.target_override, "target");
    if (result.legacy_official) parse_ipv4(*result.legacy_official, "official");
    return result;
}

std::vector<region_route> load_region_routes(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open region configuration: " + path.string());

    std::string default_target;
    unsigned short default_target_port = lobby_port;
    std::vector<region_route> routes;
    region_route* current = nullptr;
    std::string section;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            current = nullptr;
            constexpr std::string_view prefix = "region.";
            if (section.starts_with(prefix)) {
                const auto id = trim(section.substr(prefix.size()));
                if (id.empty()) throw std::runtime_error("empty region ID at line " + std::to_string(line_number));
                routes.push_back({});
                current = &routes.back();
                current->id = id;
                current->client_key = id;
                current->display_name = id;
            } else if (section != "redirector") {
                throw std::runtime_error("unknown section [" + section + "] at line " + std::to_string(line_number));
            }
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("expected key=value at line " + std::to_string(line_number));
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (section == "redirector") {
            if (key == "default_target") default_target = value;
            else if (key == "default_target_port") default_target_port = parse_port(value);
            else throw std::runtime_error("unknown redirector key '" + key + "' at line " + std::to_string(line_number));
        } else if (current != nullptr) {
            if (key == "client_key") current->client_key = value;
            else if (key == "display_name") current->display_name = value;
            else if (key == "official_host") current->official_host = value;
            else if (key == "official_port") current->official_port = parse_port(value);
            else if (key == "target_host") current->target_host = value;
            else if (key == "target_port") current->target_port = parse_port(value);
            else if (key == "enabled") current->enabled = parse_bool(value, "region " + current->id + " enabled");
            else throw std::runtime_error("unknown region key '" + key + "' at line " + std::to_string(line_number));
        } else {
            throw std::runtime_error("key outside a section at line " + std::to_string(line_number));
        }
    }

    std::vector<region_route> enabled;
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> official_endpoints;
    for (auto& route : routes) {
        if (!route.enabled) continue;
        if (!ids.insert(route.id).second) throw std::runtime_error("duplicate enabled region ID: " + route.id);
        if (route.official_host.empty()) throw std::runtime_error("region " + route.id + " has no official_host");
        if (route.target_host.empty()) route.target_host = default_target;
        if (route.target_host.empty()) throw std::runtime_error("region " + route.id + " has no target_host or default_target");
        const auto endpoint = route.official_host + ':' + std::to_string(route.official_port);
        if (!official_endpoints.insert(endpoint).second) throw std::runtime_error("duplicate official endpoint: " + endpoint);
        route.official_address = parse_ipv4(route.official_host, "official");
        route.target_address = parse_ipv4(route.target_host, "target");
        if (route.target_port == 0) route.target_port = default_target_port;
        enabled.push_back(std::move(route));
    }
    if (enabled.empty()) throw std::runtime_error("region configuration has no enabled routes");
    return enabled;
}

std::vector<region_route> resolve_routes(const options& config) {
    std::vector<region_route> routes;
    if (config.config_path) {
        if (config.legacy_official || config.legacy_port) {
            throw std::invalid_argument("--official and --port cannot be combined with --config");
        }
        routes = load_region_routes(*config.config_path);
    } else {
        if (!config.target_override) throw std::invalid_argument("--config or --target is required");
        region_route route;
        route.id = "EU";
        route.client_key = "EU";
        route.display_name = "Europe";
        route.official_host = config.legacy_official.value_or(europe_lobby);
        route.official_port = config.legacy_port.value_or(lobby_port);
        route.target_host = *config.target_override;
        route.target_port = route.official_port;
        route.official_address = parse_ipv4(route.official_host, "official");
        route.target_address = parse_ipv4(route.target_host, "target");
        routes.push_back(std::move(route));
    }

    if (config.target_override && config.config_path) {
        for (auto& route : routes) {
            route.target_host = *config.target_override;
            route.target_address = parse_ipv4(route.target_host, "target");
        }
    }
    return routes;
}

void print_routes(const std::vector<region_route>& routes) {
    std::cout << "Configured region routes:\n";
    for (const auto& route : routes) {
        std::cout << "  " << route.id << " [client key " << route.client_key << "] " << route.display_name << "\n"
                  << "    " << route.official_host << ':' << route.official_port << " -> "
                  << route.target_host << ':' << route.target_port << '\n';
    }
}

std::string make_filter(const std::vector<region_route>& routes) {
    std::string filter = "(";
    bool first = true;
    std::unordered_set<std::string> responses;
    const auto append = [&](const std::string& clause) {
        if (!first) filter += " or ";
        filter += '(' + clause + ')';
        first = false;
    };
    for (const auto& route : routes) {
        append("outbound and ip.DstAddr == " + route.official_host + " and tcp.DstPort == "
            + std::to_string(route.official_port));
        // Replies from a server on this PC, which reflection addressed to the official host.
        append("outbound and ip.DstAddr == " + route.official_host + " and tcp.SrcPort == "
            + std::to_string(route.target_port));
        const auto response = route.target_host + ':' + std::to_string(route.target_port);
        if (responses.insert(response).second) {
            append("ip.SrcAddr == " + route.target_host + " and tcp.SrcPort == "
                + std::to_string(route.target_port));
        }
    }
    filter += ')';
    return filter;
}

// True when the route's target is this PC: loopback, or the address the client connects from.
bool targets_this_pc(const region_route& route, const UINT32 client_address) {
    return (ntohl(route.target_address) >> 24) == 127 || route.target_address == client_address;
}

std::string win32_error(const char* operation) {
    return std::string(operation) + " failed with Windows error " + std::to_string(GetLastError());
}

} // namespace

int main(int argc, char** argv) {
    try {
        const winsock_session winsock;
        const auto config = parse_options(argc, argv);
        if (config.show_help) {
            std::cout << help_text;
            return 0;
        }

        const auto routes = resolve_routes(config);
        print_routes(routes);
        if (config.list_regions) return 0;

        const auto filter = make_filter(routes);
        SetConsoleCtrlHandler(console_handler, TRUE);
        divert_handle = WinDivertOpen(filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);
        if (divert_handle == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (error == ERROR_ACCESS_DENIED) {
                throw std::runtime_error("Administrator access is required. Right-click the redirector and choose Run as administrator.");
            }
            throw std::runtime_error(win32_error("WinDivertOpen"));
        }

        std::cout << "Sema Redirector active. Only configured lobby endpoints are redirected.\n"
                  << "Press Ctrl+C to stop.\n";

        std::unordered_map<unsigned short, std::size_t> client_routes;
        std::array<unsigned char, 0xFFFF> packet{};
        while (!stopping) {
            UINT packet_length = 0;
            WINDIVERT_ADDRESS address{};
            if (!WinDivertRecv(divert_handle, packet.data(), static_cast<UINT>(packet.size()), &packet_length, &address)) {
                if (stopping || GetLastError() == ERROR_NO_DATA) break;
                throw std::runtime_error(win32_error("WinDivertRecv"));
            }

            PWINDIVERT_IPHDR ip = nullptr;
            PWINDIVERT_TCPHDR tcp = nullptr;
            if (!WinDivertHelperParsePacket(packet.data(), packet_length, &ip, nullptr, nullptr, nullptr, nullptr, &tcp,
                    nullptr, nullptr, nullptr, nullptr, nullptr) || ip == nullptr || tcp == nullptr) {
                continue;
            }

            bool translated = false;
            if (address.Outbound) {
                for (std::size_t index = 0; index < routes.size(); ++index) {
                    const auto& route = routes[index];
                    if (ip->DstAddr != route.official_address || ntohs(tcp->DstPort) != route.official_port) continue;
                    client_routes[ntohs(tcp->SrcPort)] = index;
                    if (targets_this_pc(route, ip->SrcAddr)) {
                        // Windows drops an outbound packet rewritten to one of its own addresses.
                        // Reflect it instead: the local server receives official:client -> own:target.
                        ip->DstAddr = ip->SrcAddr;
                        ip->SrcAddr = route.official_address;
                        address.Outbound = FALSE;
                    } else {
                        ip->DstAddr = route.target_address;
                    }
                    tcp->DstPort = htons(route.target_port);
                    translated = true;
                    break;
                }
            }

            if (!translated && address.Outbound) {
                // Local server reply to a reflected connection: hand it back to the client as if
                // it came from the official lobby.
                const auto found = client_routes.find(ntohs(tcp->DstPort));
                if (found != client_routes.end()) {
                    const auto& route = routes[found->second];
                    if (ip->DstAddr == route.official_address && ntohs(tcp->SrcPort) == route.target_port) {
                        ip->DstAddr = ip->SrcAddr;
                        ip->SrcAddr = route.official_address;
                        tcp->SrcPort = htons(route.official_port);
                        address.Outbound = FALSE;
                        translated = true;
                    }
                }
            }

            if (!translated) {
                const auto found = client_routes.find(ntohs(tcp->DstPort));
                if (found != client_routes.end()) {
                    const auto& route = routes[found->second];
                    if (ip->SrcAddr == route.target_address && ntohs(tcp->SrcPort) == route.target_port) {
                        ip->SrcAddr = route.official_address;
                        tcp->SrcPort = htons(route.official_port);
                        if (address.Outbound) address.Outbound = FALSE;
                        translated = true;
                    }
                }
            }
            if (!translated) continue;

            WinDivertHelperCalcChecksums(packet.data(), packet_length, &address, 0);
            if (!WinDivertSend(divert_handle, packet.data(), packet_length, nullptr, &address)) {
                throw std::runtime_error(win32_error("WinDivertSend"));
            }
        }

        WinDivertClose(divert_handle);
        divert_handle = INVALID_HANDLE_VALUE;
        std::cout << "Sema Redirector stopped; normal lobby routing restored.\n";
        return 0;
    } catch (const std::exception& error) {
        if (divert_handle != INVALID_HANDLE_VALUE) WinDivertClose(divert_handle);
        divert_handle = INVALID_HANDLE_VALUE;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
