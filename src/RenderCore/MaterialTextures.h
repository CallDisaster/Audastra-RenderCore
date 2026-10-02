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
};
struct MaterialTextureData {
    CompiledModel asset;
    std::vector<MaterialLayerLayout> layouts;
    std::vector<MaterialTextureChannel> channels;
    std::uint64_t storageBytes = 0;
    bool animated = false;
};
struct MaterialTextureAccess {
    static const MaterialTextureData* Get(const CompiledMaterialTextures& textures) noexcept { return textures.data_.get(); }
    static CompiledMaterialTextures Make(std::shared_ptr<const MaterialTextureData> data) {
        CompiledMaterialTextures result; result.data_ = std::move(data); return result;
    }
};
bool EvaluateMaterialTextures(const MaterialTextureData& source, std::uint32_t sequenceIndex,
    std::uint32_t frame, std::uint64_t global, std::span<std::uint32_t> output,
    MaterialLayerIssue& issue, Evaluation& evaluation) noexcept;
}
