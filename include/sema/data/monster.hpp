#pragma once

#include "types.hpp"

#include <cstdint>

namespace sema::data {

enum class monster_rank : std::uint8_t { normal, elite, boss };

struct monster_template {
    template_id id{};
    template_id npc_id{};
    monster_rank rank{monster_rank::normal};
    std::uint64_t max_health{1};
    std::uint32_t attack{};
    std::uint32_t defense{};
    float aggro_radius{};
    float move_speed{};
    float attack_range{};
    std::uint32_t server_skill_id{};
    std::uint32_t attack_interval_ms{1500};
};

} // namespace sema::data
