// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "ModelData.h"
#include "War3RenderCore/PoseBatch.h"
namespace War3RenderCore::Detail {
// 材质复用同一标量计算次序；来源/错误编号由MaterialChannels独立绑定。
Alpha CompileMaterialScalarAlpha(const War3Map::Render::MdxLayer& source,
    const std::vector<std::uint32_t>& periods, bool identicalDuplicatesPreview = false);
float SampleScalarAlpha(const Alpha& channel, const Sequence& sequence, std::uint32_t frame,
    std::uint64_t global, Evaluation& e, bool identicalDuplicatesPreview = false) noexcept;
Alpha CompileGeosetAlpha(const std::optional<War3Map::Render::MdxGeosetAnimation>& source,
    const std::vector<std::uint32_t>& periods);
ColorChannel CompileGeosetRgb(const std::optional<War3Map::Render::MdxGeosetAnimation>& source,
    const std::vector<std::uint32_t>& periods);
bool EvaluateGeosets(const ModelData& model,const Sequence& sequence,std::uint32_t frame,std::uint64_t global,
    std::span<float> alphas,std::span<Vec3> colors,Evaluation& e) noexcept;
bool EvaluateGeosets(const ModelData& model,const Sequence& sequence,std::uint32_t frame,std::uint64_t global,
    std::span<float> alphas,std::span<Vec3> colors,Evaluation& e,
    std::span<GeosetIssue> issues,std::uint32_t& issueCount) noexcept;
}
