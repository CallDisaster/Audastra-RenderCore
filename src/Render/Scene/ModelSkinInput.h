// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "ModelSceneData.h"
#include "War3MapCore/RenderPluginInstanceRoster.h"
#include <cstdint>
#include <span>

namespace War3Map::Render {
// 仅在输入调用期间借用。先统一预检旧/新静态描述符、计数、flags与累计预算，
// 再由CopyStaticModelInput验证/深拷贝全部几何。preflight包含provider/backend两份
// 声明长度flags准入，不能单独作为发布依据，也不代表全进程RSS上限。
struct StaticSamplerSceneView {
    bool explicitSamplers = false;
    std::span<const std::uint32_t> samplerFlags;
    std::uint64_t payloadBytes = 0;
};
[[nodiscard]] bool ValidateStaticSamplerScene(const War3RenderModelSceneV2& input,
    std::uint64_t textureDescriptorBytes, std::uint64_t texturePixelBytes, StaticSamplerSceneView& output);
// texture字节数由调用方既有纹理链验证提供，计入同一320MiB门；这里只拥有几何/flags。
// 返回payloadBytes为provider实际载荷；backend须按staticSamplerBackendBudget复核其flags实际capacity。
// 成功后调用方须把同次已验证纹理交给candidate再发布。失败不改output/payloadBytes。
[[nodiscard]] bool CopyStaticModelInput(const War3RenderModelSceneV2& input,
    std::uint64_t textureDescriptorBytes, std::uint64_t texturePixelBytes,
    ModelScene& output, std::uint64_t& payloadBytes);
// 纯CPU验证：没有Windows/D3D依赖；异常由插件边界捕获。目标均为尚未发布的临时副本。
[[nodiscard]] bool ValidateModelGeometry(const ModelScene& scene, std::uint32_t textureCount);
[[nodiscard]] bool CopySkinnedModelInput(const War3RenderSkinnedModelSceneV2& input,
    ModelScene& scene, std::uint64_t& payloadBytes);
[[nodiscard]] bool CopySkinUpdate(const War3RenderSkinUpdateV2& input, const ModelScene& scene, SkinPose& pose);
// 增量复制：pose 是同一场景版本、同尺寸/模式、由上面整份复制（或本函数）得到的较旧副本，
// 只校验并复制 changed 中列出的实例（严格递增、可为空），其余实例保持 pose 原值。
// 只用于逐实例固定布局；列出实例的模型与各片段位置必须与 pose 相同。
// 调用方保证未列出实例的输入内容与 pose 逐字节相同，否则结果是旧值而不是输入值（仍是校验过的数据）。
// 失败时 pose 不变。
[[nodiscard]] bool CopySkinUpdateInstances(const War3RenderSkinUpdateV2& input, const ModelScene& scene,
    std::span<const std::uint32_t> changed, SkinPose& pose);
// 显式新roster初始化，静态资产已经验证且保持驻留。只复制校验/显隐必需layout元数据，
// 不复制几何、纹理或索引；失败不改变output，旧CopySkinUpdate的固定baseline规则保持。
[[nodiscard]] bool CopySkinRoster(const War3RenderSkinUpdateV2& input,
    const ModelScene& residentStatic, ModelScene& output);
// 只复制追加suffix和完整layout元数据，原静态前缀通过shared_ptr保持拥有。
// 不处理纹理字节，调用方须按同一累计预算校验/拥有两类纹理后才发布。
[[nodiscard]] bool CopySkinAssetAppend(const War3RenderAssetAppendV2& assets,
    const War3RenderSkinUpdateV2& input, std::shared_ptr<const ModelScene> residentStatic,
    ModelScene& output, std::uint64_t& payloadBytes);
}
