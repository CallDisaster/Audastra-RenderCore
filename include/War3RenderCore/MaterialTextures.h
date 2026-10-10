// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3RenderCore/CompiledModel.h"
#include <array>
#include <memory>
#include <span>
namespace War3RenderCore {
namespace Detail { struct MaterialTextureData; struct MaterialTextureAccess; }
struct TextureChannelOptions {
    std::uint32_t enabled = 0, maxLayerValues = 8192;
};
static_assert(sizeof(TextureChannelOptions) == 8);
// 材质层贴图坐标二维仿射，与 War3RenderMaterialStateV2::uvTransform 同义：
// u'=m0*u+m1*v+m2，v'=m3*u+m4*v+m5。
using UvTransform = std::array<float, 6>;
inline constexpr UvTransform IdentityUvTransform{1, 0, 0, 0, 1, 0};
// 单独拥有整数轨迹、可达TEXS引用与TXAN坐标轨迹；旧CompiledModel及默认容量公式不改变。
// KMTF仅支持None(0)阶梯GenericPreview，未取得原版选键证明；TXAN按原版6F77C730合成顺序。
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
    // 至少一层引用了可用且带轨迹的 TXAN；与 KMTF 无关，宿主据此开启逐帧坐标状态。
    [[nodiscard]] bool HasAnimatedUv() const noexcept;
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
