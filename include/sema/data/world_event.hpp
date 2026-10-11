#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sema::data {

struct world_event_action {
    std::uint16_t index{};
    std::string action_type;  // directive type, e.g. StartSpawner, PlayNpcTalk, Delay
    std::uint32_t delay_ms{};
    std::string payload;      // stage, step, phase, and the client directive as JSON
};

struct world_event_template {
    template_id id{};
    std::string name;
    map_id map{};
    std::string trigger_type;
    std::string trigger_value;
    bool enabled{true};
    std::vector<world_event_action> actions;
};

} // namespace sema::data
