#pragma once

#include "types.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

namespace sema::data {

struct walk_triangle {
    std::array<vector3, 3> vertices;
};

class walk_mesh {
public:
    void add(walk_triangle triangle);
    [[nodiscard]] bool contains(float x, float y) const noexcept;
    [[nodiscard]] std::optional<vector3> project(float x, float y) const noexcept;
    [[nodiscard]] std::size_t triangle_count() const noexcept { return triangles_.size(); }

private:
    std::vector<walk_triangle> triangles_;
};

[[nodiscard]] walk_mesh load_walk_mesh_obj(const std::filesystem::path& path);

} // namespace sema::data
