#include "skill_catalog.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string trim(std::string value) {
    const auto is_space = [](const unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;) {
        const auto comma = line.find(',', start);
        result.push_back(trim(line.substr(start, comma - start)));
        if (comma == std::string::npos) return result;
        start = comma + 1;
    }
}

std::uint32_t parse_u32(const std::string& text, const char* field) {
    std::uint32_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::runtime_error(std::string("invalid skill ") + field + ": " + text);
    }
    return result;
}

bool parse_bool(std::string text, const char* field) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (text == "true" || text == "1") return true;
    if (text == "false" || text == "0") return false;
    throw std::runtime_error(std::string("invalid skill ") + field + ": " + text);
}

sema::gameplay_skill_kind parse_kind(const std::string& text) {
    if (text == "dash") return sema::gameplay_skill_kind::dash;
    if (text == "primary") return sema::gameplay_skill_kind::primary_attack;
    if (text == "secondary") return sema::gameplay_skill_kind::secondary_attack;
    throw std::runtime_error("invalid skill kind: " + text);
}

} // namespace

const sema::gameplay_skill_definition* skill_catalog::find(const std::uint32_t skill_id) const noexcept {
    const auto found = skills_.find(skill_id);
    return found == skills_.end() ? nullptr : &found->second;
}

void skill_catalog::add(sema::gameplay_skill_definition skill) {
    if (skill.skill_id == 0 || skill.name.empty() || skill.server_skill_id == 0) {
        throw std::runtime_error("skill catalog entry is incomplete");
    }
    if (!skills_.emplace(skill.skill_id, std::move(skill)).second) {
        throw std::runtime_error("duplicate skill ID in catalog");
    }
}

void skill_catalog::validate() const {
    if (skills_.empty()) throw std::runtime_error("skill catalog is empty");
    for (const auto& [id, skill] : skills_) {
        if (skill.next_request_skill_id != 0 && !find(skill.next_request_skill_id)) {
            throw std::runtime_error("skill " + std::to_string(id) + " references an unknown next request skill");
        }
        if (skill.announced_next_skill_id != 0 && !find(skill.announced_next_skill_id)) {
            throw std::runtime_error("skill " + std::to_string(id) + " announces an unknown next skill");
        }
    }
}

skill_catalog load_skill_catalog(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not open skill catalog: " + path.string());
    const std::string expected_header = "skill_id,name,kind,server_skill_id,next_request_skill_id,announced_next_skill_id,sends_combo_stage,begins_combat_state,terminal_release_delay_ms";
    std::string line;
    if (!std::getline(input, line) || trim(line) != expected_header) {
        throw std::runtime_error("skill catalog header does not match the supported schema");
    }

    skill_catalog result;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.starts_with('#')) continue;
        const auto fields = split_csv(line);
        if (fields.size() != 9) {
            throw std::runtime_error("skill catalog line " + std::to_string(line_number) + " requires 9 fields");
        }
        result.add({
            parse_u32(fields[0], "skill_id"), fields[1], parse_kind(fields[2]),
            parse_u32(fields[3], "server_skill_id"), parse_u32(fields[4], "next_request_skill_id"),
            parse_u32(fields[5], "announced_next_skill_id"), parse_bool(fields[6], "sends_combo_stage"),
            parse_bool(fields[7], "begins_combat_state"), parse_u32(fields[8], "terminal_release_delay_ms")});
    }
    result.validate();
    return result;
}
