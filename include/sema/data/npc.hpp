#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sema::data {

enum class npc_kind : std::uint8_t { ambient, merchant, quest, teleporter, monster };

struct npc_template {
    template_id id{};
    std::string name;
    npc_kind kind{npc_kind::ambient};
    std::uint16_t level{1};
    faction allegiance{faction::neutral};
    std::string interaction;
    std::vector<template_id> skills;  // NpcData ActiveSkillList, highest priority first
};

} // namespace sema::data
