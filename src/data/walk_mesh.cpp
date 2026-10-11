#include "sema/data/walk_mesh.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sema::data {
namespace {

constexpr float epsilon = 0.0001F;

float signed_area(const vector3& a, const vector3& b, const float x, const float y) noexcept {
    return (x - b.x) * (a.y - b.y) - (a.x - b.x) * (y - b.y);
}

bool covers(const walk_triangle& triangle, const float x, const float y) noexcept {
    const auto d1 = signed_area(triangle.vertices[0], triangle.vertices[1], x, y);
    const auto d2 = signed_area(triangle.vertices[1], triangle.vertices[2], x, y);
    const auto d3 = signed_area(triangle.vertices[2], triangle.vertices[0], x, y);
    const bool negative = d1 < -epsilon || d2 < -epsilon || d3 < -epsilon;
    const bool positive = d1 > epsilon || d2 > epsilon || d3 > epsilon;
    return !(negative && positive);
}

std::size_t vertex_index(const std::string& token, const std::size_t vertex_count) {
    const auto separator = token.find('/');
    const auto index_text = token.substr(0, separator);
    int index{};
    const auto [end, error] = std::from_chars(index_text.data(), index_text.data() + index_text.size(), index);
    if (error != std::errc{} || end != index_text.data() + index_text.size() || index == 0) {
        throw std::runtime_error("invalid OBJ face index: " + token);
    }
    const auto resolved = index > 0 ? index - 1 : static_cast<int>(vertex_count) + index;
    if (resolved < 0 || static_cast<std::size_t>(resolved) >= vertex_count) {
        throw std::runtime_error("OBJ face index is outside the vertex list");
    }
    return static_cast<std::size_t>(resolved);
}

} // namespace

void walk_mesh::add(walk_triangle triangle) {
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    if (std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) < epsilon) {
        throw std::runtime_error("walk-mesh triangle has no horizontal area");
    }
    triangles_.push_back(std::move(triangle));
}

bool walk_mesh::contains(const float x, const float y) const noexcept {
    for (const auto& triangle : triangles_) if (covers(triangle, x, y)) return true;
    return false;
}

std::optional<vector3> walk_mesh::project(const float x, const float y) const noexcept {
    for (const auto& triangle : triangles_) {
        if (!covers(triangle, x, y)) continue;
        const auto& a = triangle.vertices[0];
        const auto& b = triangle.vertices[1];
        const auto& c = triangle.vertices[2];
        const auto denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(denominator) < epsilon) continue;
        const auto first = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / denominator;
        const auto second = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / denominator;
        const auto third = 1.0F - first - second;
        return vector3{x, y, first * a.z + second * b.z + third * c.z};
    }
    return std::nullopt;
}

walk_mesh load_walk_mesh_obj(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not open walk mesh: " + path.string());
    std::vector<vector3> vertices;
    walk_mesh result;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream values(line);
        std::string kind;
        values >> kind;
        if (kind == "v") {
            vector3 vertex;
            if (!(values >> vertex.x >> vertex.y >> vertex.z)) throw std::runtime_error("invalid OBJ vertex");
            vertices.push_back(vertex);
        } else if (kind == "f") {
            std::vector<std::size_t> face;
            std::string token;
            while (values >> token) face.push_back(vertex_index(token, vertices.size()));
            if (face.size() < 3) throw std::runtime_error("OBJ face requires at least three vertices");
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                result.add({{vertices[face[0]], vertices[face[i]], vertices[face[i + 1]]}});
            }
        }
    }
    return result;
}

} // namespace sema::data
