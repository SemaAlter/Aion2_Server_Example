#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>

namespace sema::data {

// Interactable world objects: gather nodes, chests, portals, quest objects.
struct env_object_template {
    template_id id{};
    std::string name;
    std::string usage_type;   // EEnvObjectUsage member, e.g. GatherSource, Quest, MapEvent
    std::string usage_value;  // usage-specific key, e.g. the GatherSource name
    std::uint32_t interact_range{};
    std::uint32_t interact_time_ms{};
};

struct env_object_spawn {
    template_id id{};
    template_id env_object_id{};
    map_id map{};
    vector3 position;
    float heading{};
};

struct map_info {
    template_id id{};
    std::string name;
};

} // namespace sema::data
