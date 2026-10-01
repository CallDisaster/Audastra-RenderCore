// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3MapCore/Render/MdxModel.h"
#include <algorithm>
#include <cstring>

namespace War3Map::Render::MdxRigidStaticPreviewInternal {

inline bool AllowedChunk(std::string_view tag) noexcept {
    return tag == "VERS" || tag == "MODL" || tag == "SEQS" || tag == "MTLS"
        || tag == "TEXS" || tag == "GEOS";
}

// 来源由专用解析入口密封；当前公开模型仍可能被调用方修改，不能只信布尔资格。
inline bool Qualified(const MdxModel& model) noexcept {
    const MdxLimits hard;
    if (!model.rigidStaticPreviewSource || model.version != 800 || MdxNodeCount(model) != 0
        || !model.pivots.empty() || !model.lights.empty() || !model.collisionShapes.empty()
        || !model.globalSequences.empty() || model.skippedChunks != 0 || model.geosets.empty()
        || model.geosets.size() > hard.maxGeosets || model.sequences.size() > hard.maxSequences) return false;
    const auto bytes = model.rigidStaticPreviewSource->Bytes();
    const auto origins = model.rigidStaticPreviewSource->Geosets();
    if (bytes.size() > hard.maxInputBytes || origins.size() != model.geosets.size()) return false;
    const auto u32Is = [&](std::size_t offset, std::uint32_t expected) {
        if (offset > bytes.size() || bytes.size() - offset < 4) return false;
        std::uint32_t actual = 0; std::memcpy(&actual, bytes.data() + offset, 4);
        return actual == expected;
    };
    const auto matches = [&](std::size_t offset, const auto& values) {
        const auto count = values.size() * sizeof(values[0]);
        return offset <= bytes.size() && count <= bytes.size() - offset
            && (count == 0 || std::memcmp(bytes.data() + offset, values.data(), count) == 0);
    };
    std::size_t total = 0;
    for (std::size_t i = 0; i < origins.size(); ++i) {
        const auto& source = origins[i]; const auto& g = model.geosets[i];
        if (source.geosetBegin >= source.geosetEnd || source.geosetEnd > bytes.size()
            || !u32Is(source.geosetBegin, static_cast<std::uint32_t>(source.geosetEnd - source.geosetBegin))
            || g.positions.empty() || g.positions.size() > hard.maxVerticesPerGeoset
            || g.positions.size() != source.vertexCount || g.normals.size() != source.vertexCount
            || g.vertexGroups.size() != source.vertexCount || g.indices.size() != source.indexCount
            || g.uvs.size() != source.vertexCount || !g.matrixGroupSizes.empty() || !g.matrixIndices.empty()
            || g.animation || g.uninterpretedTailBytes != 0 || g.skippedPrimitiveGroups != 0
            || g.materialId >= model.materials.size()
            || !u32Is(source.vertexGroupsCountOffset, source.vertexCount)
            || !u32Is(source.matrixGroupsCountOffset, 0) || !u32Is(source.matrixIndicesCountOffset, 0)
            || !matches(source.positionsOffset, g.positions) || !matches(source.normalsOffset, g.normals)
            || !matches(source.indicesOffset, g.indices)
            || !matches(source.vertexGroupsCountOffset + 4, g.vertexGroups)
            || !std::all_of(g.vertexGroups.begin(), g.vertexGroups.end(), [](auto slot) { return slot == 0; })) return false;
        if (source.uvCount != 0) {
            if (source.uvCount != source.vertexCount || !matches(source.uvsOffset, g.uvs)) return false;
        } else if (!std::all_of(g.uvs.begin(), g.uvs.end(), [](const auto& uv) { return uv[0] == 0 && uv[1] == 0; })) return false;
        total += g.positions.size();
        if (total > hard.maxTotalVertices) return false;
    }
    return true;
}

inline Diagnostic Warning(const MdxRigidStaticGeosetOrigin& origin, std::size_t geoset) {
    return {DiagnosticSeverity::Warning, "Mdx.Preview", "MDX_RIGID_STATIC_PREVIEW",
        "预览近似：无控制器静态 geoset=" + std::to_string(geoset)
        + " 使用模型局部 identity；源 GNDX offset=" + std::to_string(origin.vertexGroupsCountOffset)
        + " count=" + std::to_string(origin.vertexCount)
        + "，MTGC offset=" + std::to_string(origin.matrixGroupsCountOffset) + " count=0"
        + "，MATS offset=" + std::to_string(origin.matrixIndicesCountOffset) + " count=0；原字节保留，非原版等价签收。",
        OriginSpan{{}, origin.matrixGroupsCountOffset, 4, {}}};
}

} // namespace War3Map::Render::MdxRigidStaticPreviewInternal
