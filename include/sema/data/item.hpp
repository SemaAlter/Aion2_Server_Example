#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>

namespace sema::data {

enum class item_category : std::uint8_t { unknown, weapon, armor, consumable, quest, material };

struct item_template {
    template_id id{};
    std::string name;
    item_category category{item_category::unknown};
    std::uint32_t max_stack{1};
    std::uint16_t required_level{};
};

} // namespace sema::data
