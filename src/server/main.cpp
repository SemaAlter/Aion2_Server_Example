#include "gameplay_session.hpp"
#include "lobby_config.hpp"
#include "lobby_session.hpp"
#include "replay_scenario.hpp"
#include "skill_catalog.hpp"
#include "tutorial_script.hpp"
#include "tutorial_world.hpp"
#include "sema/frame_codec.hpp"
#include "sema/gameplay.hpp"
#include "sema/handshake.hpp"
#include "sema/lobby.hpp"
#include "sema/data/game_data.hpp"
#include "sema/data/database.hpp"

#include <Windows.h>
#include <bcrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* help_text = R"(Sema Local - experimental AION2 gameplay server

Usage:
  "Sema Local.exe" [options]

Options:
  --host <address>       Address to bind (default: 127.0.0.1)
  --port <number>        TCP port to bind (default: 13328)
  --scenario <path>      Replay framed server messages when tutorial starts
  --cutscene-phase <path> Replay the captured cutscene world-state handoff
  --skills <path>        Load gameplay skill definitions from CSV
  --data-dir <path>      Load world-data catalogs and walk meshes
  --database-config <path> Load world-data catalogs from MySQL
  --handshake            Answer the captured RSA client hello (experimental)
  --session-data <hex>   Fixed handshake plaintext (requires --handshake)
  --gateway              Run the Europe lobby on port 13700
  --gateway-port <port>  Lobby port (default: 13700)
  --config <path>        Lobby server INI configuration
  --server-name <name>   Override the configured lobby name
  --advertise-host <ip>  IPv4 address sent to clients (default: 127.0.0.1)
  --help                 Show this help
)";

struct options {
    std::string host = "127.0.0.1";
    unsigned short port = 13328;
    std::optional<std::filesystem::path> scenario;
    std::optional<std::filesystem::path> cutscene_phase;
    std::optional<std::filesystem::path> skills;
    std::optional<std::filesystem::path> data_directory;
    std::optional<std::filesystem::path> database_config;
    bool handshake = false;
    std::optional<std::vector<std::uint8_t>> session_data;
    bool gateway = false;
    unsigned short gateway_port = 13700;
    sema::lobby_server lobby_server;
    std::optional<std::filesystem::path> lobby_config;
    std::optional<std::string> server_name_override;
    std::string advertise_host = "127.0.0.1";
    bool show_help = false;
};

std::atomic_bool stopping = false;
SOCKET listener_socket = INVALID_SOCKET;
SOCKET gateway_socket = INVALID_SOCKET;

BOOL WINAPI console_handler(DWORD) {
    stopping = true;
    if (listener_socket != INVALID_SOCKET) closesocket(listener_socket);
    if (gateway_socket != INVALID_SOCKET) closesocket(gateway_socket);
    return TRUE;
}

std::vector<std::uint8_t> decode_hex(const std::string& text) {
    if (text.empty() || text.size() % 2 != 0) throw std::invalid_argument("hex value must contain complete bytes");
    std::vector<std::uint8_t> result;
    result.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(text.data() + i, text.data() + i + 2, value, 16);
        if (error != std::errc{} || end != text.data() + i + 2) throw std::invalid_argument("value is not hexadecimal");
        result.push_back(static_cast<std::uint8_t>(value));
    }
    return result;
}

options parse_options(int argc, char** argv) {
    options result;
    const auto require_value = [&](int& index, const std::string& option) -> std::string {
        if (++index >= argc) throw std::invalid_argument(option + " requires a value");
        return argv[index];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--host") {
            result.host = require_value(i, argument);
        } else if (argument == "--port") {
            const auto value = require_value(i, argument);
            unsigned port = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
            if (error != std::errc{} || end != value.data() + value.size() || port == 0 || port > 65535) {
                throw std::invalid_argument("invalid TCP port: " + value);
            }
            result.port = static_cast<unsigned short>(port);
        } else if (argument == "--scenario") {
            result.scenario = std::filesystem::absolute(require_value(i, argument));
        } else if (argument == "--cutscene-phase") {
            result.cutscene_phase = std::filesystem::absolute(require_value(i, argument));
        } else if (argument == "--skills") {
            result.skills = std::filesystem::absolute(require_value(i, argument));
        } else if (argument == "--data-dir") {
            result.data_directory = std::filesystem::absolute(require_value(i, argument));
        } else if (argument == "--database-config") {
            result.database_config = std::filesystem::absolute(require_value(i, argument));
        } else if (argument == "--handshake") {
            result.handshake = true;
        } else if (argument == "--session-data") {
            result.session_data = decode_hex(require_value(i, argument));
        } else if (argument == "--gateway") {
            result.gateway = true;
        } else if (argument == "--gateway-port") {
            const auto value = require_value(i, argument);
            unsigned port = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
            if (error != std::errc{} || end != value.data() + value.size() || port == 0 || port > 65535) {
                throw std::invalid_argument("invalid gateway port: " + value);
            }
            result.gateway_port = static_cast<unsigned short>(port);
        } else if (argument == "--config") {
            result.lobby_config = std::filesystem::absolute(require_value(i, argument));
        } else if (argument == "--server-name") {
            result.server_name_override = require_value(i, argument);
            if (result.server_name_override->empty() || result.server_name_override->size() > 255) {
                throw std::invalid_argument("server name must contain 1 to 255 bytes");
            }
        } else if (argument == "--advertise-host") {
            result.advertise_host = require_value(i, argument);
        } else if (argument == "--help" || argument == "-h") {
            result.show_help = true;
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    if (result.session_data && !result.handshake) throw std::invalid_argument("--session-data requires --handshake");
    if (result.lobby_config) result.lobby_server = load_lobby_server_config(*result.lobby_config);
    if (result.server_name_override) result.lobby_server.name = *result.server_name_override;
    IN_ADDR address{};
    if (InetPtonA(AF_INET, result.advertise_host.c_str(), &address) != 1) {
        throw std::invalid_argument("invalid advertised IPv4 address: " + result.advertise_host);
    }
    return result;
}

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

std::string socket_error(const char* operation) {
    return std::string(operation) + " failed with Winsock error " + std::to_string(WSAGetLastError());
}

void send_all(SOCKET socket, std::span<const std::uint8_t> bytes) {
    while (!bytes.empty()) {
        const auto chunk = static_cast<int>(std::min<std::size_t>(bytes.size(), INT_MAX));
        const auto sent = send(socket, reinterpret_cast<const char*>(bytes.data()), chunk, 0);
        if (sent == SOCKET_ERROR) throw std::runtime_error(socket_error("send"));
        bytes = bytes.subspan(static_cast<std::size_t>(sent));
    }
}

std::string hex_preview(std::span<const std::uint8_t> bytes) {
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    const auto visible = std::min<std::size_t>(bytes.size(), 32);
    for (std::size_t i = 0; i < visible; ++i) output << std::setw(2) << static_cast<unsigned>(bytes[i]);
    if (bytes.size() > visible) output << "...";
    return output.str();
}

std::vector<std::uint8_t> random_session_data() {
    std::vector<std::uint8_t> result(32);
    if (BCryptGenRandom(nullptr, result.data(), static_cast<ULONG>(result.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
    return result;
}

SOCKET create_listener(const std::string& host, const unsigned short port) {
    const auto result = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (result == INVALID_SOCKET) throw std::runtime_error(socket_error("socket"));
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &endpoint.sin_addr) != 1) {
        closesocket(result);
        throw std::invalid_argument("invalid IPv4 host");
    }
    if (bind(result, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR) {
        const auto error = socket_error("bind");  // read before closesocket resets it
        closesocket(result);
        throw std::runtime_error(error);
    }
    if (listen(result, SOMAXCONN) == SOCKET_ERROR) {
        const auto error = socket_error("listen");  // read before closesocket resets it
        closesocket(result);
        throw std::runtime_error(error);
    }
    return result;
}

void handle_gateway_client(SOCKET client, const options& config) {
    sema::frame_decoder decoder;
    std::array<std::uint8_t, 4096> buffer{};
    std::size_t sequence = 0;
    lobby_session session({
        config.lobby_server,
        config.advertise_host,
        config.port,
        config.session_data.value_or(random_session_data()),
    });
    std::cout << "[gateway] client connected.\n";
    try {
        for (;;) {
            const auto received = recv(client, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
            if (received == 0) break;
            if (received == SOCKET_ERROR) throw std::runtime_error(socket_error("gateway recv"));
            decoder.append(std::span(buffer).first(static_cast<std::size_t>(received)));
            sema::frame frame;
            while (decoder.try_read(frame)) {
                ++sequence;
                std::cout << "[gateway] C->S #" << sequence << " body=" << frame.body.size()
                          << " hex=" << hex_preview(frame.body) << '\n';
                const auto response = session.respond(frame.body);
                if (!response.empty()) {
                    const auto encoded = sema::encode_frame(response);
                    send_all(client, encoded);
                    std::cout << "[gateway] S->C body=" << response.size()
                              << " hex=" << hex_preview(response) << '\n';
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "[gateway] session error: " << error.what() << '\n';
    }
    closesocket(client);
    std::cout << "[gateway] client disconnected.\n";
}

void run_gateway(const options& config) {
    try {
        gateway_socket = create_listener(config.host, config.gateway_port);
        std::cout << "Sema Local Europe gateway listening on " << config.host << ':' << config.gateway_port << ".\n";
        while (!stopping) {
            const auto client = accept(gateway_socket, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                if (stopping) break;
                throw std::runtime_error(socket_error("gateway accept"));
            }
            BOOL no_delay = TRUE;
            setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
            std::thread(handle_gateway_client, client, config).detach();
        }
    } catch (const std::exception& error) {
        if (!stopping) std::cerr << "Gateway error: " << error.what() << '\n';
        stopping = true;
        if (listener_socket != INVALID_SOCKET) closesocket(listener_socket);
    }
    if (gateway_socket != INVALID_SOCKET) closesocket(gateway_socket);
    gateway_socket = INVALID_SOCKET;
}

void replay(
    SOCKET socket,
    const replay_scenario& scenario,
    std::atomic_bool& cancelled,
    std::mutex& write_mutex,
    const std::string& endpoint) {
    std::size_t sequence = 0;
    try {
        for (const auto& event : scenario.events) {
            const auto end = std::chrono::steady_clock::now() + event.delay;
            while (!cancelled && std::chrono::steady_clock::now() < end) {
                std::this_thread::sleep_for(std::min(std::chrono::milliseconds(20),
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now())));
            }
            if (cancelled) return;
            auto body = event.body;
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            (void)sema::refresh_gameplay_server_time(body, static_cast<std::uint64_t>(now));
            const auto encoded = sema::encode_frame(body);
            std::lock_guard lock(write_mutex);
            send_all(socket, encoded);
            std::cout << '[' << endpoint << "] S->C #" << ++sequence << " body=" << body.size()
                      << " hex=" << hex_preview(body) << '\n';
        }
    } catch (const std::exception& error) {
        if (!cancelled) std::cerr << '[' << endpoint << "] replay error: " << error.what() << '\n';
    }
}

void send_gameplay_ticks(
    SOCKET socket,
    std::atomic_bool& cancelled,
    std::mutex& write_mutex,
    const std::string& endpoint) {
    auto next_tick = std::chrono::steady_clock::now();
    try {
        while (!cancelled) {
            next_tick += std::chrono::milliseconds(50);
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const auto response = sema::encode_frame(
                sema::encode_gameplay_tick(static_cast<std::uint64_t>(now)));
            {
                std::lock_guard lock(write_mutex);
                send_all(socket, response);
            }
            std::this_thread::sleep_until(next_tick);
        }
    } catch (const std::exception& error) {
        if (!cancelled) std::cerr << '[' << endpoint << "] tick error: " << error.what() << '\n';
    }
}

void send_gameplay_sequence(
    SOCKET socket,
    std::vector<sema::gameplay_timed_message> events,
    std::atomic_bool& cancelled,
    std::mutex& write_mutex,
    const std::string& endpoint,
    const std::string& label) {
    try {
        for (const auto& event : events) {
            const auto end = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(event.delay_milliseconds);
            while (!cancelled && std::chrono::steady_clock::now() < end) {
                std::this_thread::sleep_for(std::min(std::chrono::milliseconds(20),
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now())));
            }
            if (cancelled) return;
            auto body = event.body;
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            (void)sema::refresh_gameplay_server_time(body, static_cast<std::uint64_t>(now));
            const auto response = sema::encode_frame(body);
            std::lock_guard lock(write_mutex);
            send_all(socket, response);
        }
        std::cout << '[' << endpoint << "] " << label << " acknowledged.\n";
    } catch (const std::exception& error) {
        if (!cancelled) std::cerr << '[' << endpoint << "] " << label << " error: " << error.what() << '\n';
    }
}

void run_tutorial_monster_ai(
    SOCKET socket,
    tutorial_world& world,
    const std::uint32_t player_entity_id,
    std::atomic_bool& cancelled,
    std::mutex& write_mutex,
    const std::string& endpoint) {
    const auto wait_until = [&](const std::chrono::steady_clock::time_point end) {
        while (!cancelled && std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(std::min(std::chrono::milliseconds(20),
                std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now())));
        }
    };

    try {
        wait_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(1000));
        // Messages from several monsters interleave, so they are queued by due time instead of
        // being sent as one blocking sequence.
        std::multimap<std::chrono::steady_clock::time_point, std::vector<std::uint8_t>> pending;
        const auto step = std::chrono::milliseconds(tutorial_world::ai_step_milliseconds);
        auto next_tick = std::chrono::steady_clock::now();
        while (!cancelled) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_tick) {
                for (auto& event : world.tick(player_entity_id, tutorial_world::ai_step_milliseconds)) {
                    pending.emplace(now + std::chrono::milliseconds(event.delay_milliseconds), std::move(event.body));
                }
                next_tick += step;
            }
            while (!pending.empty() && pending.begin()->first <= std::chrono::steady_clock::now()) {
                const auto& body = pending.begin()->second;
                const auto encoded = sema::encode_frame(body);
                {
                    std::lock_guard lock(write_mutex);
                    send_all(socket, encoded);
                }
                std::cout << '[' << endpoint << "] S->C monster-ai body=" << body.size()
                          << " hex=" << hex_preview(body) << '\n';
                pending.erase(pending.begin());
            }
            wait_until(pending.empty() ? next_tick : std::min(next_tick, pending.begin()->first));
        }
    } catch (const std::exception& error) {
        if (!cancelled) std::cerr << '[' << endpoint << "] monster AI error: " << error.what() << '\n';
    }
}

// Character creation and world entry replayed from your own recording (scenarios/character-create.json,
// world-enter.json, world-ready.json); template_name is the recorded character's name.
struct creation_data {
    std::optional<replay_phase> created;
    std::optional<replay_phase> world_enter;
    std::optional<replay_phase> world_ready;
    static constexpr const char* template_name = "Elyos0001";
};

void handle_client(
    SOCKET client,
    const options& config,
    const std::optional<replay_scenario>& scenario,
    const std::optional<replay_phase>& cutscene_phase,
    const std::optional<tutorial_script>& script_template,
    const std::optional<replay_phase>& login_character,
    const creation_data& creation,
    const skill_catalog& skills,
    const std::optional<sema::data::game_data_store>& game_data) {
    sockaddr_in peer{};
    int peer_size = sizeof(peer);
    getpeername(client, reinterpret_cast<sockaddr*>(&peer), &peer_size);
    std::array<char, INET_ADDRSTRLEN> address{};
    inet_ntop(AF_INET, &peer.sin_addr, address.data(), static_cast<DWORD>(address.size()));
    const std::string endpoint = std::string(address.data()) + ':' + std::to_string(ntohs(peer.sin_port));
    std::cout << '[' << endpoint << "] connected.\n";

    sema::frame_decoder decoder;
    std::array<std::uint8_t, 16 * 1024> receive_buffer{};
    std::size_t sequence = 0;
    std::atomic_bool cancelled = false;
    std::mutex write_mutex;
    std::optional<std::thread> replay_thread;
    std::optional<std::thread> tick_thread;
    std::optional<std::thread> monster_ai_thread;
    std::vector<std::thread> action_threads;
    std::optional<std::vector<std::uint8_t>> handshake_session_data;
    std::optional<gameplay_session> protocol_session;
    tutorial_world world = scenario && game_data ? tutorial_world(*game_data, scenario->map_id) : tutorial_world{};
    std::optional<tutorial_script> script = script_template;
    const auto server_now = [] {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    };
    const auto start_monster_ai = [&] {
        if (!monster_ai_thread && scenario) {
            monster_ai_thread.emplace(run_tutorial_monster_ai, client, std::ref(world),
                scenario->player_entity_id, std::ref(cancelled), std::ref(write_mutex), endpoint);
        }
    };
    if (config.handshake) {
        handshake_session_data = config.session_data.value_or(random_session_data());
        protocol_session.emplace(config.lobby_server.id, *handshake_session_data, skills);
        if (login_character) protocol_session->set_login_characters({login_character->events.front().body});
        if (creation.created) protocol_session->set_character_template(creation.created->events.front().body, creation_data::template_name);
    }
    const auto start_replay = [&] {
        if (scenario && !replay_thread) {
            replay_thread.emplace(replay, client, std::cref(*scenario),
                std::ref(cancelled), std::ref(write_mutex), endpoint);
        }
    };
    if (!config.handshake) start_replay();
    bool close_requested = false;

    try {
        for (;;) {
            const auto received = recv(client, reinterpret_cast<char*>(receive_buffer.data()),
                static_cast<int>(receive_buffer.size()), 0);
            if (received == 0) break;
            if (received == SOCKET_ERROR) throw std::runtime_error(socket_error("recv"));
            decoder.append(std::span(receive_buffer).first(static_cast<std::size_t>(received)));

            sema::frame current;
            while (decoder.try_read(current)) {
                ++sequence;
                std::cout << '[' << endpoint << "] C->S #" << sequence << " declared=" << current.declared_length
                          << " body=" << current.body.size() << " hex=" << hex_preview(current.body) << '\n';
                if (close_requested) continue;
                if (protocol_session) {
                    const auto was_authenticated = protocol_session->authenticated();
                    const auto response_body = protocol_session->respond(current.body);
                    if (!response_body.empty()) {
                        const auto response = sema::encode_frame(response_body);
                        std::lock_guard lock(write_mutex);
                        send_all(client, response);
                        std::cout << '[' << endpoint << "] S->C body=" << response_body.size()
                                  << " hex=" << hex_preview(response_body) << '\n';
                    }
                    if (protocol_session->close_requested()) {
                        close_requested = true;
                        cancelled = true;
                        shutdown(client, SD_SEND);
                        std::cout << '[' << endpoint << "] return to character selection requested.\n";
                        break;
                    }
                    if (const auto position = protocol_session->player_position()) {
                        world.update_player_position(*position);
                    }
                    if (sequence == 1) {
                        std::cout << '[' << endpoint << "] handshake sessionData="
                                  << hex_preview(*handshake_session_data) << '\n';
                    }
                    if (protocol_session->consume_tutorial_request()) {
                        std::cout << '[' << endpoint << "] tutorial bootstrap requested.\n";
                        start_replay();
                    }
                    if (protocol_session->consume_sprint_interrupted() && scenario && world.stop_sprint()) {
                        // Sent before any attack or interaction answer, like the official server.
                        std::lock_guard lock(write_mutex);
                        for (const auto& body : sema::encode_gameplay_sprint_end(scenario->player_entity_id)) {
                            send_all(client, sema::encode_frame(body));
                        }
                    }
                    for (const auto& interaction : protocol_session->consume_tutorial_interactions()) {
                        if (!script || !scenario) continue;
                        auto progression = script->on_interaction(
                            interaction, scenario->player_entity_id, world, server_now());
                        if (progression.empty()) continue;
                        start_monster_ai();
                        action_threads.emplace_back(send_gameplay_sequence, client, std::move(progression),
                            std::ref(cancelled), std::ref(write_mutex), endpoint,
                            "tutorial interaction " + std::to_string(interaction.object_entity_id));
                    }
                    if (script && scenario) {
                        auto exit = protocol_session->consume_tutorial_leave_request()
                            ? script->leave(scenario->player_entity_id, server_now())
                            : script->leave_if_overdue(scenario->player_entity_id, server_now());
                        if (!exit.empty()) {
                            action_threads.emplace_back(send_gameplay_sequence, client, std::move(exit),
                                std::ref(cancelled), std::ref(write_mutex), endpoint, std::string("tutorial exit"));
                        }
                    }
                    for (const auto* world_phase : {protocol_session->consume_world_enter() ? &creation.world_enter : nullptr,
                             protocol_session->consume_world_ready() ? &creation.world_ready : nullptr}) {
                        if (!world_phase || !*world_phase) continue;
                        std::vector<sema::gameplay_timed_message> entry;
                        for (auto& event : prepare_replay_phase(**world_phase, (*world_phase)->source_player_entity_id, server_now())) {
                            entry.push_back({static_cast<std::uint32_t>(event.delay.count()), sema::rename_character(
                                std::move(event.body), creation_data::template_name, protocol_session->character_name())});
                        }
                        std::cout << '[' << endpoint << "] world entry as " << protocol_session->character_name() << ".\n";
                        action_threads.emplace_back(send_gameplay_sequence, client, std::move(entry),
                            std::ref(cancelled), std::ref(write_mutex), endpoint, std::string("world entry"));
                    }
                    if (protocol_session->consume_tutorial_light_trigger() && scenario) {
                        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        std::vector<sema::gameplay_timed_message> transition;
                        const auto cutscene_action = world.claim_player_state(2);
                        if (cutscene_phase) {
                            auto handoff = prepare_replay_phase(*cutscene_phase,
                                scenario->player_entity_id, static_cast<std::uint64_t>(now));
                            if (script) script->adapt_handoff(handoff, cutscene_action);
                            transition.reserve(handoff.size() + 16);
                            for (auto& event : handoff) {
                                transition.push_back({static_cast<std::uint32_t>(event.delay.count()),
                                    std::move(event.body)});
                            }
                        }
                        auto cutscene = sema::gameplay_tutorial_light_prelude(
                            scenario->player_entity_id, static_cast<std::uint64_t>(now), cutscene_action);
                        if (cutscene_phase && !cutscene.empty()) {
                            // The official server completes the world-state handoff, then lets the
                            // client settle in the cinematic instance before starting Seq_1278.
                            cutscene.front().delay_milliseconds = 9'000;
                        }
                        transition.insert(transition.end(), std::make_move_iterator(cutscene.begin()),
                            std::make_move_iterator(cutscene.end()));
                        // Scheduled event 301100300 advances every 60 s through state 4
                        // (official-tutorial-combat-door); later tutorial areas wait on it.
                        for (std::uint32_t state = 2; state <= 4; ++state) {
                            transition.push_back({60'000, sema::encode_gameplay_tutorial_event_state(
                                state, static_cast<std::uint64_t>(now) + (state - 1) * 60'000ULL)});
                        }
                        action_threads.emplace_back(send_gameplay_sequence, client, std::move(transition),
                            std::ref(cancelled), std::ref(write_mutex), endpoint,
                            std::string("tutorial light transition"));
                    }
                    if (protocol_session->consume_tutorial_cutscene_finished() && scenario) {
                        auto encounter = world.spawn_group(1, scenario->player_entity_id, true, false);
                        encounter.push_back({0, sema::encode_gameplay_tutorial_monster_prompt()});
                        action_threads.emplace_back(send_gameplay_sequence, client, std::move(encounter),
                            std::ref(cancelled), std::ref(write_mutex), endpoint,
                            std::string("tutorial post-cutscene encounter"));
                        start_monster_ai();
                    }
                    if (const auto dash = protocol_session->consume_tutorial_dash()) {
                        const auto* skill = skills.find(dash->cast.skill_id);
                        if (!skill || !scenario) throw std::runtime_error("dash data is unavailable");
                        auto dash_sequence = sema::gameplay_tutorial_dash_sequence(
                            dash->cast, *skill, scenario->player_entity_id, dash->action_sequence,
                            world.claim_player_state(2), server_now(),
                            protocol_session->player_position().value_or(sema::gameplay_movement{}));
                        world.start_sprint();
                        // Skill.NeedCostSp of the server dash skill 4002.
                        for (auto& message : world.spend_resources(scenario->player_entity_id, 0, 10'000)) {
                            dash_sequence.insert(dash_sequence.begin() + 1, std::move(message));
                        }
                        start_monster_ai();
                        action_threads.emplace_back(send_gameplay_sequence, client, std::move(dash_sequence),
                            std::ref(cancelled),
                            std::ref(write_mutex), endpoint, std::string("tutorial dash"));
                    }
                    if (protocol_session->consume_tutorial_takeoff() && scenario && script) {
                        if (const auto position = protocol_session->player_position()) {
                            action_threads.emplace_back(send_gameplay_sequence, client,
                                std::vector<sema::gameplay_timed_message>{
                                    {100, sema::encode_gameplay_takeoff_height(scenario->player_entity_id, position->z)}},
                                std::ref(cancelled), std::ref(write_mutex), endpoint, std::string("tutorial takeoff"));
                        }
                    }
                    if (protocol_session->consume_tutorial_landing() && scenario && script) {
                        const auto state = world.claim_player_state(1);
                        const auto position = protocol_session->player_position().value_or(sema::gameplay_movement{});
                        action_threads.emplace_back(send_gameplay_sequence, client,
                            std::vector<sema::gameplay_timed_message>{
                                {100, sema::encode_gameplay_jump_landed(scenario->player_entity_id, state, server_now(), position)},
                                {200, sema::encode_gameplay_action_finished(scenario->player_entity_id, state, 0x01)}},
                            std::ref(cancelled), std::ref(write_mutex), endpoint, std::string("tutorial landing"));
                    }
                    if (const auto potion = protocol_session->consume_tutorial_potion(); potion && script && scenario) {
                        if (const auto item = script->use_potion()) {
                            const auto state = world.claim_player_state(1);
                            const auto position = protocol_session->player_position().value_or(sema::gameplay_movement{});
                            auto potion_sequence = sema::gameplay_potion_use_sequence(scenario->player_entity_id,
                                gameplay_session::potion_item_skill, potion->action_sequence, state, *item,
                                server_now(), position);
                            world.start_heal_over_time(gameplay_session::potion_item_skill, state);
                            start_monster_ai();
                            action_threads.emplace_back(send_gameplay_sequence, client, std::move(potion_sequence),
                                std::ref(cancelled), std::ref(write_mutex), endpoint, std::string("tutorial potion"));
                        }
                    }
                    if (const auto attack = protocol_session->consume_tutorial_attack()) {
                        const auto* skill = skills.find(attack->cast.skill_id);
                        if (!skill || !scenario) throw std::runtime_error("attack data is unavailable");
                        const auto label = std::string("tutorial skill ") + std::to_string(attack->cast.skill_id);
                        const auto position = protocol_session->player_position();
                        const auto* skill_data = game_data ? game_data->skills.find(attack->cast.skill_id) : nullptr;
                        const auto range_world_units = skill_data ? skill_data->range * 100.0F : 300.0F;
                        // An early cast still plays its sequence so the client's action
                        // sequence stays aligned, but it hits nothing.
                        const auto ready = world.try_use_skill(attack->cast.skill_id,
                            skill_data ? skill_data->cooldown_ms : 0, std::chrono::steady_clock::now());
                        if (!ready) std::cout << '[' << endpoint << "] " << label << " rejected: cooldown.\n";
                        const auto targets = position && ready
                            ? world.apply_attack(
                                skill->kind,
                                skill_data ? skill_data->hit_shape : sema::data::skill_hit_shape::single,
                                skill_data ? skill_data->max_targets : 1,
                                attack->cast.target_entity_id,
                                *position, range_world_units,
                                std::bit_cast<float>(attack->cast.facing),
                                skill_data ? skill_data->angle_degrees : 0.0F)
                            : std::vector<sema::gameplay_attack_target>{};
                        auto attack_sequence = sema::gameplay_tutorial_attack_sequence(
                            attack->cast, *skill, scenario->player_entity_id,
                            attack->action_sequence, targets);
                        if (ready) {
                            // Skill.NeedCostMp: the secondary attacks (9868, 9869) cost 200 MP.
                            const auto mp_cost = skill->kind == sema::gameplay_skill_kind::secondary_attack ? 200U : 0U;
                            auto costs = world.spend_resources(scenario->player_entity_id, mp_cost, 0);
                            attack_sequence.insert(attack_sequence.begin() + 1, std::make_move_iterator(costs.begin()),
                                std::make_move_iterator(costs.end()));
                            start_monster_ai();
                        }
                        if (script) {
                            // Kill counters and quest steps follow the cast completion (06 38),
                            // before the delayed death records and combo release.
                            auto progression = script->on_kills(targets, attack->cast.skill_id,
                                scenario->player_entity_id, world, server_now());
                            const auto completion = std::ranges::find_if(attack_sequence, [](const auto& message) {
                                return message.body.size() > 1 && message.body[0] == 0x06 && message.body[1] == 0x38;
                            });
                            attack_sequence.insert(completion == attack_sequence.end() ? completion : completion + 1,
                                std::make_move_iterator(progression.begin()), std::make_move_iterator(progression.end()));
                        }
                        action_threads.emplace_back(send_gameplay_sequence, client, std::move(attack_sequence),
                            std::ref(cancelled),
                            std::ref(write_mutex), endpoint, label);
                        if (script && position && ready) {
                            auto tubes = script->on_attack(*position, std::bit_cast<float>(attack->cast.facing),
                                range_world_units, skill_data ? skill_data->angle_degrees : 0.0F,
                                skill_data && skill_data->hit_shape == sema::data::skill_hit_shape::frontal_cone,
                                scenario->player_entity_id, world, server_now());
                            if (!tubes.empty()) {
                                action_threads.emplace_back(send_gameplay_sequence, client, std::move(tubes),
                                    std::ref(cancelled), std::ref(write_mutex), endpoint, std::string("tutorial test tube"));
                            }
                        }
                    }
                    if (!was_authenticated && protocol_session->authenticated() && !tick_thread) {
                        tick_thread.emplace(send_gameplay_ticks, client, std::ref(cancelled),
                            std::ref(write_mutex), endpoint);
                    }
                }
            }
            // The retail flow sends no response to 25 36. Stop world traffic,
            // then let the client close its side of the gameplay socket.
            if (close_requested) continue;
        }
    } catch (const std::exception& error) {
        std::cerr << '[' << endpoint << "] session error: " << error.what() << '\n';
    }

    cancelled = true;
    if (replay_thread && replay_thread->joinable()) replay_thread->join();
    if (tick_thread && tick_thread->joinable()) tick_thread->join();
    if (monster_ai_thread && monster_ai_thread->joinable()) monster_ai_thread->join();
    for (auto& thread : action_threads) if (thread.joinable()) thread.join();
    shutdown(client, SD_BOTH);
    closesocket(client);
    std::cout << '[' << endpoint << "] disconnected.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const auto config = parse_options(argc, argv);
        if (config.show_help) {
            std::cout << help_text;
            return 0;
        }
        const std::optional<replay_scenario> scenario = config.scenario
            ? std::optional(load_replay_scenario(*config.scenario))
            : std::nullopt;
        const std::optional<replay_phase> cutscene_phase = config.cutscene_phase
            ? std::optional(load_replay_phase(*config.cutscene_phase))
            : std::nullopt;
        const skill_catalog skills = config.skills ? load_skill_catalog(*config.skills) : skill_catalog{};
        std::optional<tutorial_script> script;
        std::optional<replay_phase> login_character;
        creation_data creation;
        if (scenario && config.scenario) {
            const auto beside = [&](const char* name) -> std::optional<replay_phase> {
                const auto path = config.scenario->parent_path() / name;
                if (!std::filesystem::is_regular_file(path)) return std::nullopt;
                return load_replay_phase(path);
            };
            try {
                script.emplace(*scenario, beside("tutorial-room-spawns.json"), beside("tutorial-exit.json"));
                login_character = beside("tutorial-login-character.json");
                creation = {beside("character-create.json"), beside("world-enter.json"), beside("world-ready.json")};
            } catch (const std::exception& error) {
                std::cerr << "Tutorial script disabled: " << error.what() << '\n';
            }
        }
        std::optional<sema::data::game_data_store> game_data;
        if (config.database_config) {
            const auto database = sema::data::load_database_config(*config.database_config);
            game_data = sema::data::load_game_data_mysql(database, config.data_directory.value_or(std::filesystem::path{}));
            if (config.data_directory) sema::data::apply_game_data_overrides(*config.data_directory, *game_data);
        } else if (config.data_directory) {
            game_data = sema::data::load_game_data(*config.data_directory);
        }
        winsock_session winsock;
        SetConsoleCtrlHandler(console_handler, TRUE);

        listener_socket = create_listener(config.host, config.port);
        std::optional<std::thread> gateway_thread;
        if (config.gateway) gateway_thread.emplace(run_gateway, std::cref(config));

        std::cout << "Sema Local listening on " << config.host << ':' << config.port << ".\n";
        if (scenario) std::cout << "Replay scenario: " << scenario->name << " (" << scenario->events.size() << " events).\n";
        if (cutscene_phase) {
            std::cout << "Cutscene phase: " << cutscene_phase->name << " ("
                      << cutscene_phase->events.size() << " events).\n";
        }
        if (config.skills) std::cout << "Skill catalog: " << skills.size() << " definitions.\n";
        if (game_data) {
            std::cout << "World data: " << game_data->skills.size() << " skills, "
                      << game_data->items.size() << " items, " << game_data->npcs.size() << " NPCs, "
                      << game_data->monsters.size() << " monsters, " << game_data->spawns.size()
                      << " spawns, " << game_data->quests.size() << " quests, " << game_data->events.size()
                      << " events, " << game_data->walk_meshes.size() << " walk meshes, "
                      << game_data->indexed_client_tables << " indexed client tables.\n";
            std::cout << "World extensions: " << game_data->maps.size() << " maps, "
                      << game_data->loot_tables.size() << " loot tables, " << game_data->loot_owner_count()
                      << " loot owner links, " << game_data->env_objects.size() << " environment objects, "
                      << game_data->env_object_spawns.size() << " environment spawns.\n";
        }
        if (scenario && game_data) {
            const tutorial_world world(*game_data, scenario->map_id);
            std::cout << "Tutorial world: " << world.monster_count() << " monsters on map "
                      << scenario->map_id << ".\n";
            if (script) {
                std::cout << "Tutorial script: quest instance " << script->quest_instance() << ", "
                          << script->objects().size() << " bootstrap objects.\n";
            }
        }
        if (config.handshake) std::cout << "Experimental RSA handshake enabled (PKCS#1 v1.5).\n";
        if (config.gateway) {
            const auto faction = config.lobby_server.faction == sema::lobby_faction::elyos ? "Elyos" : "Asmodians";
            std::cout << "Europe lobby entry: " << config.lobby_server.name << " (ID " << config.lobby_server.id
                      << ", " << faction << ", load " << static_cast<unsigned>(config.lobby_server.load_percent) << "%).\n"
                      << "Gameplay endpoint advertised to clients: " << config.advertise_host << ':' << config.port << ".\n";
        }

        while (!stopping) {
            const auto client = accept(listener_socket, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                if (stopping) break;
                throw std::runtime_error(socket_error("accept"));
            }
            BOOL no_delay = TRUE;
            setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
            std::thread(handle_client, client, std::cref(config), std::cref(scenario), std::cref(cutscene_phase),
                std::cref(script), std::cref(login_character), std::cref(creation), std::cref(skills), std::cref(game_data)).detach();
        }
        if (listener_socket != INVALID_SOCKET) closesocket(listener_socket);
        listener_socket = INVALID_SOCKET;
        stopping = true;
        if (gateway_socket != INVALID_SOCKET) closesocket(gateway_socket);
        if (gateway_thread && gateway_thread->joinable()) gateway_thread->join();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
