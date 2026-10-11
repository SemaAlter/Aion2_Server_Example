#include "csv_reader.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>

namespace sema::data::detail {
namespace {

std::vector<std::string> parse_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char value = line[i];
        if (value == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (value == ',' && !quoted) {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field.push_back(value);
        }
    }
    if (quoted) throw std::runtime_error("unterminated quoted CSV field");
    fields.push_back(std::move(field));
    return fields;
}

template <typename Value>
Value integer(const std::string& text, const char* field) {
    Value result{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::runtime_error(std::string("invalid ") + field + ": " + text);
    }
    return result;
}

} // namespace

std::vector<csv_row> read_csv(
    const std::filesystem::path& path,
    const std::vector<std::string>& expected_header) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not open game-data file: " + path.string());
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("empty game-data file: " + path.string());
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (parse_line(line) != expected_header) {
        throw std::runtime_error("unsupported header in game-data file: " + path.string());
    }

    std::vector<csv_row> rows;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.starts_with('#')) continue;
        auto fields = parse_line(line);
        if (fields.size() != expected_header.size()) {
            throw std::runtime_error(path.string() + ":" + std::to_string(line_number)
                + " has " + std::to_string(fields.size()) + " fields; expected "
                + std::to_string(expected_header.size()));
        }
        rows.push_back({line_number, std::move(fields)});
    }
    return rows;
}

std::uint32_t u32(const std::string& value, const char* field) { return integer<std::uint32_t>(value, field); }
std::uint64_t u64(const std::string& value, const char* field) { return integer<std::uint64_t>(value, field); }

float number(const std::string& value, const char* field) {
    float result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::runtime_error(std::string("invalid ") + field + ": " + value);
    }
    return result;
}

} // namespace sema::data::detail
