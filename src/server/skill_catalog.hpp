#pragma once

#include "sema/gameplay.hpp"

#include <filesystem>
#include <unordered_map>

class skill_catalog {
public:
    [[nodiscard]] const sema::gameplay_skill_definition* find(std::uint32_t skill_id) const noexcept;
    void add(sema::gameplay_skill_definition skill);
    void validate() const;
    [[nodiscard]] std::size_t size() const noexcept { return skills_.size(); }

private:
    std::unordered_map<std::uint32_t, sema::gameplay_skill_definition> skills_;
};

[[nodiscard]] skill_catalog load_skill_catalog(const std::filesystem::path& path);
