// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "ModelData.h"
#include "War3RenderCore/PoseBatch.h"
namespace War3RenderCore::Detail {
// 材质复用同一标量计算次序；来源/错误编号由MaterialChannels独立绑定。
// 给出 sequences 时 KMTA 非全局轨迹与 GEOA 同样按原版逐动作区间编译（IDA：6F785260 → 6F780270 → 6F78A090）。
Alpha CompileMaterialScalarAlpha(const War3Map::Render::MdxLayer& source,
    const std::vector<std::uint32_t>& periods, bool identicalDuplicatesPreview = false,
    const std::vector<War3Map::Render::MdxSequence>* sequences = nullptr);
float SampleScalarAlpha(const Alpha& channel, const Sequence& sequence, std::uint32_t sequenceIndex, std::uint32_t frame,
    std::uint64_t global, Evaluation& e, bool identicalDuplicatesPreview = false) noexcept;
// GEOA 非全局轨迹按原版逐动作区间编译（见 GameKeyRanges.h）；含逆序边的区间只在原序预览策略下采样。
Alpha CompileGeosetAlpha(const std::optional<War3Map::Render::MdxGeosetAnimation>& source,
    const std::vector<std::uint32_t>& periods, const std::vector<War3Map::Render::MdxSequence>& sequences);
ColorChannel CompileGeosetRgb(const std::optional<War3Map::Render::MdxGeosetAnimation>& source,
    const std::vector<std::uint32_t>& periods, const std::vector<War3Map::Render::MdxSequence>& sequences);
bool EvaluateGeosets(const ModelData& model,std::uint32_t sequenceIndex,std::uint32_t frame,std::uint64_t global,
    std::span<float> alphas,std::span<Vec3> colors,Evaluation& e) noexcept;
bool EvaluateGeosets(const ModelData& model,std::uint32_t sequenceIndex,std::uint32_t frame,std::uint64_t global,
    std::span<float> alphas,std::span<Vec3> colors,Evaluation& e,
    std::span<GeosetIssue> issues,std::uint32_t& issueCount) noexcept;
}
