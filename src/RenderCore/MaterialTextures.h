// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "ModelData.h"
#include "War3RenderCore/MaterialTextures.h"
namespace War3RenderCore::Detail {
struct MaterialTextureKey {
    std::uint32_t frame = 0, textureId = 0;
    std::uint32_t sourceFrame = 0, reserved = 0;
    std::uint64_t sourceByteOffset = 0;
};
struct MaterialTextureChannel {
    std::uint32_t fallback = 0, period = 0;
    std::uint32_t interpolation = 0, globalSequence = NoIndex;
    bool global = false;
    MaterialLayerIssue issue;
    std::vector<MaterialTextureKey> keys;
    std::vector<std::uint32_t> reachable;
    std::vector<Range32> sequenceKeys;
    std::uint32_t uvAnimation = NoIndex; // uvAnimations 下标；NoIndex 为静态坐标
};
// TXAN 一条：节点同款轨迹（原版同一组编译器与逐动作区间）。
struct UvAnimationChannel {
    std::uint64_t sourceByteOffset = 0;
    Track<3> translation;
    Track<4> rotation;
    Track<3> scaling;
};
struct MaterialTextureData {
    CompiledModel asset;
    std::vector<MaterialLayerLayout> layouts;
    std::vector<MaterialTextureChannel> channels;
    std::vector<UvAnimationChannel> uvAnimations;
    std::uint64_t storageBytes = 0;
    bool animated = false, uvAnimated = false;
};
struct MaterialTextureAccess {
    static const MaterialTextureData* Get(const CompiledMaterialTextures& textures) noexcept { return textures.data_.get(); }
    static CompiledMaterialTextures Make(std::shared_ptr<const MaterialTextureData> data) {
        CompiledMaterialTextures result; result.data_ = std::move(data); return result;
    }
};
// uvOutput 与 output 同长同序；无 TXAN 的层写单位变换。
bool EvaluateMaterialTextures(const MaterialTextureData& source, std::uint32_t sequenceIndex,
    std::uint32_t frame, std::uint64_t global, std::span<std::uint32_t> output, std::span<UvTransform> uvOutput,
    MaterialLayerIssue& issue, Evaluation& evaluation) noexcept;
}
