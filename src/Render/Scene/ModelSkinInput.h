// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "ModelSceneData.h"
#include <cstdint>

namespace War3Map::Render {
// 纯CPU验证：没有Windows/D3D依赖；异常由插件边界捕获。目标均为尚未发布的临时副本。
[[nodiscard]] bool ValidateModelGeometry(const ModelScene& scene, std::uint32_t textureCount);
[[nodiscard]] bool CopySkinnedModelInput(const War3RenderSkinnedModelSceneV2& input,
    ModelScene& scene, std::uint64_t& payloadBytes);
[[nodiscard]] bool CopySkinUpdate(const War3RenderSkinUpdateV2& input, const ModelScene& scene, SkinPose& pose);
// 显式新roster初始化，静态资产已经验证且保持驻留。只复制校验/显隐必需layout元数据，
// 不复制几何、纹理或索引；失败不改变output，旧CopySkinUpdate的固定baseline规则保持。
[[nodiscard]] bool CopySkinRoster(const War3RenderSkinUpdateV2& input,
    const ModelScene& residentStatic, ModelScene& output);
}
