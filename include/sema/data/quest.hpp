#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sema::data {

struct quest_objective {
    std::uint16_t index{};
    std::string objective_type;  // EQuestObjectiveType member, e.g. KillNpc, AskNpc, ClearMapEvent
    template_id target_id{};     // resolved NPC or item ID when the goal names one
    std::uint32_t required_count{1};
    std::string payload;         // full client goal as JSON
};

struct quest_reward {
    template_id item_id{};       // 0 for the experience/currency row
    std::uint32_t item_count{};
    std::uint64_t experience{};
    std::uint64_t currency{};
    std::string character_class; // empty unless the reward is class-specific
};

struct quest_template {
    template_id id{};
    std::string name;
    std::uint16_t minimum_level{};
    faction allegiance{faction::neutral};
    bool repeatable{};
    std::vector<quest_objective> objectives;
    std::vector<quest_reward> rewards;
};

} // namespace sema::data
