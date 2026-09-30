// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/Render/MdxModel.h"
#include <cstdint>

namespace War3RenderCore {
// 结构分类，不是渲染能力或编译有效性证明。调用方必须另外检查 CompileMdxModel 的结果。
enum class ModelContentKind : std::uint32_t { Mesh = 0, ParticleOnlyUnimplemented = 1, OtherNodeOnly = 2 };
[[nodiscard]] inline ModelContentKind ClassifyMdxContent(const War3Map::Render::MdxModel& model) noexcept {
    if (!model.geosets.empty()) return ModelContentKind::Mesh;
    for (const auto& node : model.otherNodes) {
        if (node.kind == War3Map::Render::MdxNodeKind::ParticleEmitter
            || node.kind == War3Map::Render::MdxNodeKind::ParticleEmitter2)
            return ModelContentKind::ParticleOnlyUnimplemented;
    }
    // 无 GEOS 并不自动等于纯粒子；空模型、只有光源或只有挂点仍单独保留。
    return ModelContentKind::OtherNodeOnly;
}
} // namespace War3RenderCore
