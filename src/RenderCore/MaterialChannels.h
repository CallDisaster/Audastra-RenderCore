// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "ModelData.h"
#include "War3RenderCore/MaterialAlphaKeySamplingPolicy.h"
namespace War3RenderCore::Detail {
CompiledMaterialChannel CompileMaterialAlpha(const War3Map::Render::MdxLayer& layer,
    bool materialUninterpreted, MaterialLayerLayout layout, const std::vector<std::uint32_t>& periods);
// 在分配前复核完整原轨迹；不合资格返回0，不改变严格编译的首错/来源。
std::uint64_t MaterialAlphaDuplicateStorageBytes(const War3Map::Render::MdxLayer& layer,
    bool materialUninterpreted) noexcept;
bool EvaluateMaterialAlphas(const ModelData& model, const Sequence& sequence, std::uint32_t frame,
    std::uint64_t global, std::span<float> output, MaterialLayerIssue& issue, Evaluation& e,
    MaterialAlphaKeySamplingPolicy keyPolicy, std::span<MaterialAlphaPreviewWarning> warnings,
    std::uint32_t& warningCount) noexcept;
}
