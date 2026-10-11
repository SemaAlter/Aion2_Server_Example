#pragma once

#include "types.hpp"

#include <cstdint>

namespace sema::data {

enum class spawn_kind : std::uint8_t { npc, monster };

struct spawn_point {
    template_id id{};
    template_id template_ref{};
    spawn_kind kind{spawn_kind::npc};
    map_id map{};
    vector3 position;
    float heading{};
    std::uint32_t respawn_seconds{};
};

} // namespace sema::data
