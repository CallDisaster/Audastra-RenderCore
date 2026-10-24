// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3MapCore/Render/MdxModel.h"
#include "MdxReadInternal.h"

namespace War3Map::Render::MdxReadInternal {

inline void ValidateMdxLimits(const MdxLimits& limits) {
    constexpr MdxLimits hard{};
    if (limits.maxInputBytes > hard.maxInputBytes || limits.maxGeosets > hard.maxGeosets
        || limits.maxVerticesPerGeoset > hard.maxVerticesPerGeoset || limits.maxTotalVertices > hard.maxTotalVertices
        || limits.maxTotalIndices > hard.maxTotalIndices || limits.maxTextures > hard.maxTextures
        || limits.maxMaterials > hard.maxMaterials || limits.maxLayersPerMaterial > hard.maxLayersPerMaterial
        || limits.maxSequences > hard.maxSequences || limits.maxTrackKeys > hard.maxTrackKeys
        || limits.maxGlobalSequences > hard.maxGlobalSequences || limits.maxBones > hard.maxBones
        || limits.maxHelpers > hard.maxHelpers || limits.maxPivots > hard.maxPivots
        || limits.maxMatrixGroupsPerGeoset > hard.maxMatrixGroupsPerGeoset
        || limits.maxMatrixIndicesPerGeoset > hard.maxMatrixIndicesPerGeoset
        || limits.maxTotalBoneTrackKeys > hard.maxTotalBoneTrackKeys || limits.maxNodes > hard.maxNodes)
        Fail("MDX_LIMITS", "调用方不能放宽MDX读取上限。");
}

// 共用有界源视图：不创建types/counts临时vector，不丢strip/未知组，不扩成三角形。
// 普通模型读取器决定哪些组用于显示；接收面读取器保留所有组及其物理位置。
struct PrimitiveView {
    std::uint32_t groupCount = 0, indexCount = 0;
    Reader types, counts, indices;
};

inline PrimitiveView ReadPrimitiveView(Reader& reader, const MdxLimits& limits, std::uint32_t vertexCount) {
    reader.ExpectTag("PTYP", "图元类型");
    const auto groupCount = reader.Count(4, limits.maxTotalIndices, "图元类型");
    const auto types = reader.Until(reader.Position() + static_cast<std::size_t>(groupCount) * 4, "图元类型");
    reader.Skip(static_cast<std::size_t>(groupCount) * 4, "图元类型");
    reader.ExpectTag("PCNT", "图元计数");
    if (reader.Count(4, limits.maxTotalIndices, "图元计数") != groupCount)
        Fail("MDX_VALUE_INVALID", "图元类型与计数组数不一致。");
    const auto counts = reader.Until(reader.Position() + static_cast<std::size_t>(groupCount) * 4, "图元计数");
    auto countValues = counts;
    std::uint64_t declared = 0;
    while (countValues.Remaining()) declared += countValues.U32("图元计数");
    reader.Skip(static_cast<std::size_t>(groupCount) * 4, "图元计数");
    reader.ExpectTag("PVTX", "图元索引");
    const auto indexCount = reader.Count(2, limits.maxTotalIndices, "图元索引");
    if (declared != indexCount) Fail("MDX_VALUE_INVALID", "图元计数之和与索引数量不一致。");
    const auto indices = reader.Until(reader.Position() + static_cast<std::size_t>(indexCount) * 2, "图元索引");
    auto indexValues = indices;
    while (indexValues.Remaining())
        if (indexValues.U16("图元索引") >= vertexCount) Fail("MDX_VALUE_INVALID", "图元索引超出顶点范围。");
    reader.Skip(static_cast<std::size_t>(indexCount) * 2, "图元索引");
    return {groupCount, indexCount, types, counts, indices};
}

} // namespace War3Map::Render::MdxReadInternal
