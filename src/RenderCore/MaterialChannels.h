// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "ModelData.h"
namespace War3RenderCore::Detail {
CompiledMaterialChannel CompileMaterialAlpha(const War3Map::Render::MdxLayer& layer,
    bool materialUninterpreted, MaterialLayerLayout layout, const std::vector<std::uint32_t>& periods);
bool EvaluateMaterialAlphas(const ModelData& model, const Sequence& sequence, std::uint32_t frame,
    std::uint64_t global, std::span<float> output, MaterialLayerIssue& issue, Evaluation& e) noexcept;
}
