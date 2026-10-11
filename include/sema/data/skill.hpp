#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sema::data {

enum class skill_target : std::uint8_t { self, friendly, hostile, ground };
enum class skill_hit_shape : std::uint8_t { single, radius, frontal_cone };

struct skill_effect {
    std::uint16_t index{};
    std::string effect_type;       // ESkillEffectType member, e.g. Damage
    std::string value_expression;  // comma-separated EffectValueList
    template_id target_filter_id{};
};

struct skill_template {
    template_id id{};
    std::string name;
    std::uint16_t max_level{1};
    std::uint32_t cooldown_ms{};
    std::uint32_t cast_time_ms{};
    float range{};
    skill_target target{skill_target::hostile};
    skill_hit_shape hit_shape{skill_hit_shape::single};
    std::uint16_t max_targets{1};
    float angle_degrees{};
    std::vector<skill_effect> effects;
};

} // namespace sema::data
