// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "ModelData.h"
namespace War3RenderCore::Detail {
Alpha CompileGeosetAlpha(const std::optional<War3Map::Render::MdxGeosetAnimation>& source,
    const std::vector<std::uint32_t>& periods);
ColorChannel CompileGeosetRgb(const std::optional<War3Map::Render::MdxGeosetAnimation>& source,
    const std::vector<std::uint32_t>& periods);
bool EvaluateGeosets(const ModelData& model,const Sequence& sequence,std::uint32_t frame,std::uint64_t global,
    std::span<float> alphas,std::span<Vec3> colors,Evaluation& e) noexcept;
}
