#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sema::data::detail {

struct csv_row {
    std::size_t line{};
    std::vector<std::string> fields;
};

[[nodiscard]] std::vector<csv_row> read_csv(
    const std::filesystem::path& path,
    const std::vector<std::string>& expected_header);

[[nodiscard]] std::uint32_t u32(const std::string& value, const char* field);
[[nodiscard]] std::uint64_t u64(const std::string& value, const char* field);
[[nodiscard]] float number(const std::string& value, const char* field);

} // namespace sema::data::detail
