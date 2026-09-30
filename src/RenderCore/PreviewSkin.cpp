// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "PoseMath.h"

namespace War3RenderCore {
Code SkinPreviewGeoset(const CompiledModel& model, std::uint32_t geosetIndex,
    std::span<const GroupMatrix> groups, std::span<Vec3> positions, std::span<Vec3> normals) noexcept {
    const auto* data = Detail::ModelAccess::Get(model);
    if (!data || geosetIndex >= data->geosets.size()) return Code::SkinGeosetInvalid;
    const auto& layout = data->geosets[geosetIndex];
    if (groups.size() != data->groups.size()) return Code::ExternalCountInvalid;
    if (positions.size() != layout.vertices.count || normals.size() != layout.vertices.count)
        return Code::OutputRangeInvalid;
    Detail::Evaluation e{};
    for (std::uint32_t i = 0; i < layout.groups.count; ++i)
        if (!Detail::CheckFinite(groups[layout.groups.offset + i].values, e)) return e.error;
    for (std::uint32_t i = 0; i < layout.vertices.count; ++i) {
        const auto vertex = layout.vertices.offset + i;
        const auto matrix = Detail::Unpack(groups[layout.groups.offset + data->vertexGroups[vertex]]);
        const auto position = Detail::TransformPosition(matrix, data->positions[vertex], e);
        const auto normal = Detail::TransformNormal(matrix, data->normals[vertex], e);
        if (!e.Good()) return e.error;
        positions[i] = position; normals[i] = normal;
    }
    return Code::Ok;
}
} // namespace War3RenderCore
