#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "machine/nv2a_vsh_batch_capture.hpp"

namespace xlle::vsh_triangle_oracle {

struct Result {
    bool declined = false;
    std::vector<uint32_t> indices;
};

namespace detail {

struct VertexView {
    std::array<float, 4> position{};
    std::array<float, 4> diffuse{};
};

inline float word_float(uint32_t word) {
    float value = 0.0f;
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

inline VertexView vertex_from_execution(const vsh_batch_capture::Execution& execution) {
    VertexView vertex;
    for (unsigned lane = 0; lane < 4; ++lane) {
        vertex.position[lane] = word_float(execution.expected[28u + lane]);
        vertex.diffuse[lane] = word_float(execution.expected[4u + lane]);
    }
    return vertex;
}

inline bool finite_vertex(const VertexView& vertex) {
    for (float value : vertex.position)
        if (!std::isfinite(value)) return false;
    if (vertex.position[3] == 0.0f) return false;
    for (float value : vertex.diffuse)
        if (!std::isfinite(value)) return false;
    return true;
}

inline bool fixed_xy(float value, int64_t& fixed) {
    if (!std::isfinite(value)) return false;
    const double rounded = std::llround(static_cast<double>(value) * 16.0);
    if (std::abs(rounded) > 1000000.0) return false;
    fixed = static_cast<int64_t>(rounded);
    return true;
}

}  // namespace detail

inline Result expand_resident_primary(const vsh_batch_capture::Batch& batch) {
    vsh_batch_capture::detail::validate_mapping(batch);
    if (batch.primitive_mode != 5u && batch.primitive_mode != 6u &&
        batch.primitive_mode != 7u) {
        throw std::runtime_error("Resident-primary oracle primitive mode is unsupported");
    }

    std::vector<detail::VertexView> vertices;
    vertices.reserve(batch.references.size());
    for (uint32_t execution_id : batch.references) {
        if (execution_id >= batch.executions.size())
            throw std::runtime_error("Resident-primary oracle reference is out of range");
        vertices.push_back(detail::vertex_from_execution(batch.executions[execution_id]));
    }

    Result result;
    result.indices.reserve(vertices.size());
    const auto append_triangle = [&](std::size_t ia, std::size_t ib,
                                     std::size_t ic) -> bool {
        const detail::VertexView& a = vertices[ia];
        const detail::VertexView& b = vertices[ib];
        const detail::VertexView& c = vertices[ic];
        if (!detail::finite_vertex(a) || !detail::finite_vertex(b) ||
            !detail::finite_vertex(c)) {
            return false;
        }
        const bool all_behind_w = a.position[3] < 0.0f && b.position[3] < 0.0f &&
                                  c.position[3] < 0.0f;
        if (all_behind_w) return true;
        const bool crosses_w_plane = a.position[3] < 0.0f || b.position[3] < 0.0f ||
                                     c.position[3] < 0.0f;
        if (crosses_w_plane) return false;

        uint32_t oa = batch.references[ia];
        uint32_t ob = batch.references[ib];
        uint32_t oc = batch.references[ic];
        if (batch.large_target) {
            const double ax = static_cast<double>(a.position[0]);
            const double ay = static_cast<double>(a.position[1]);
            const double bx = static_cast<double>(b.position[0]);
            const double by = static_cast<double>(b.position[1]);
            const double cx = static_cast<double>(c.position[0]);
            const double cy = static_cast<double>(c.position[1]);
            const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (area == 0.0) return true;
            if (!batch.cull_enabled) {
                if (area < 0.0) std::swap(ob, oc);
            } else if (batch.cull_front) {
                if (area > 0.0) return true;
                std::swap(ob, oc);
            } else if (area < 0.0) {
                return true;
            }
        } else {
            int64_t ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
            if (!detail::fixed_xy(a.position[0], ax) ||
                !detail::fixed_xy(a.position[1], ay) ||
                !detail::fixed_xy(b.position[0], bx) ||
                !detail::fixed_xy(b.position[1], by) ||
                !detail::fixed_xy(c.position[0], cx) ||
                !detail::fixed_xy(c.position[1], cy)) {
                return false;
            }
            const int64_t area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (area == 0) return true;
            if (!batch.cull_enabled) {
                if (area < 0) std::swap(ob, oc);
            } else if (batch.cull_front) {
                if (area > 0) return true;
                std::swap(ob, oc);
            } else if (area < 0) {
                return true;
            }
        }
        result.indices.push_back(oa);
        result.indices.push_back(ob);
        result.indices.push_back(oc);
        return true;
    };

    bool ok = true;
    if (batch.primitive_mode == 5u) {
        for (std::size_t i = 0; ok && i + 2 < vertices.size(); i += 3)
            ok = append_triangle(i, i + 1, i + 2);
    } else if (batch.primitive_mode == 6u) {
        for (std::size_t i = 2; ok && i < vertices.size(); ++i) {
            if ((i & 1u) == 0)
                ok = append_triangle(i - 2, i - 1, i);
            else
                ok = append_triangle(i - 1, i - 2, i);
        }
    } else {
        for (std::size_t i = 2; ok && i < vertices.size(); ++i)
            ok = append_triangle(0, i - 1, i);
    }
    if (!ok) {
        result.declined = true;
        result.indices.clear();
    }
    return result;
}

}  // namespace xlle::vsh_triangle_oracle
