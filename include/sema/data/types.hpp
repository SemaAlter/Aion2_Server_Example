#pragma once

#include <cstdint>

namespace sema::data {

using template_id = std::uint32_t;
using entity_id = std::uint64_t;
using map_id = std::uint32_t;

struct vector3 {
    float x{};
    float y{};
    float z{};
};

enum class faction : std::uint8_t {
    neutral,
    elyos,
    asmodian,
};

} // namespace sema::data
