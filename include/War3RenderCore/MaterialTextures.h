// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3RenderCore/CompiledModel.h"
#include <memory>
#include <span>
namespace War3RenderCore {
namespace Detail { struct MaterialTextureData; struct MaterialTextureAccess; }
struct TextureChannelOptions {
    std::uint32_t enabled = 0, maxLayerValues = 8192;
};
static_assert(sizeof(TextureChannelOptions) == 8);
// 单独拥有整数轨迹和可达TEXS引用；旧CompiledModel及默认容量公式不改变。
// 仅支持None(0)阶梯GenericPreview；未取得原版KMTF选键证明。
class CompiledMaterialTextures {
public:
    CompiledMaterialTextures() = default;
    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] AssetIdentity Identity() const noexcept;
    [[nodiscard]] const AssetSource& Source() const noexcept;
    [[nodiscard]] bool Matches(const CompiledModel& asset) const noexcept;
    [[nodiscard]] std::uint32_t MaterialLayerCount() const noexcept;
    [[nodiscard]] std::span<const MaterialLayerLayout> MaterialLayerLayouts() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> ReachableTextureIds(std::uint32_t layer) const noexcept;
    [[nodiscard]] bool HasAnimatedTexture() const noexcept;
    [[nodiscard]] std::uint64_t StorageBytes() const noexcept;
private:
    std::shared_ptr<const Detail::MaterialTextureData> data_;
    friend struct Detail::MaterialTextureAccess;
};
// 原层错误编译成带物理来源的拒绝状态；求值不发布部分贴图状态。
// sidecar必须来自同一冻结MDX/asset来源，Create再次检查同一compiled owner。
[[nodiscard]] War3Map::Result<CompiledMaterialTextures> CompileMdxMaterialTextures(
    const War3Map::Render::MdxModel& model, const CompiledModel& asset,
    std::uint64_t maxCompiledBytes = 128ull * 1024 * 1024);
}
