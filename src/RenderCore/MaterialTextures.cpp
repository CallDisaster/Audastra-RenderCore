// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "MaterialTextures.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace War3RenderCore {
namespace {
using namespace Detail;
struct TextureBudget {
    std::uint64_t used = 0, limit = 0;
    bool Add(std::uint64_t count, std::uint64_t bytes) noexcept {
        if (bytes && count > (limit - used) / bytes) return false;
        used += count * bytes; return true;
    }
};
MaterialLayerIssue TextureIssue(MaterialLayerLayout layout, Code code,
    std::uint64_t offset, std::uint32_t value, std::uint32_t frame = 0) noexcept {
    return {layout.materialIndex, layout.layerIndex, offset, code, frame, value, 0};
}
Code CheckTextureLayer(const War3Map::Render::MdxMaterial& material,
    const War3Map::Render::MdxLayer& layer, MaterialLayerLayout layout,
    std::size_t textureCount, std::span<const std::uint32_t> periods, MaterialTextureChannel& channel) noexcept {
    const auto fail = [&](Code code, std::uint64_t offset, std::uint32_t value, std::uint32_t frame = 0) {
        channel.issue = TextureIssue(layout, code, offset, value, frame); return code;
    };
    if (material.hasUninterpretedData || layer.hasUninterpretedData)
        return fail(Code::MaterialTextureUninterpreted, layout.sourceByteOffset, 0);
    if (layer.duplicateTextureByteOffset || !layer.duplicateTextureTracks.empty())
        return fail(Code::MaterialTextureUninterpreted, layer.duplicateTextureByteOffset
            ? layer.duplicateTextureByteOffset : layer.duplicateTextureTracks.front().sourceByteOffset, 0);
    // 首单元不隐含实现TXAN或其它UV组。
    if (layer.textureAnimationId != NoIndex || layer.coordId != 0)
        return fail(Code::MaterialTextureUninterpreted, layer.staticTextureIdByteOffset
            + (layer.textureAnimationId != NoIndex ? 4 : 8),
            layer.textureAnimationId != NoIndex ? layer.textureAnimationId : layer.coordId);
    if (layer.textureId >= textureCount)
        return fail(Code::MaterialTextureReferenceInvalid, layer.staticTextureIdByteOffset, layer.textureId);
    if (!layer.animatedTexture) {
        if (!layer.textureKeys.empty()) return fail(Code::MaterialTextureUninterpreted, layout.sourceByteOffset, 0);
        return Code::Ok;
    }
    if (layer.textureInterpolation != 0)
        return fail(Code::MaterialTextureInterpolationUnsupported, layer.textureSourceByteOffset + 8, layer.textureInterpolation);
    if (layer.textureGlobalSequence != NoIndex && layer.textureGlobalSequence >= periods.size())
        return fail(Code::MaterialTextureGlobalSequenceUnsupported, layer.textureSourceByteOffset + 12, layer.textureGlobalSequence);
    std::uint32_t previous = 0; bool first = true;
    for (const auto& key : layer.textureKeys) {
        if (!first && key.frame <= previous)
            return fail(Code::MaterialTextureKeysInvalid, key.sourceByteOffset, key.frame, key.frame);
        if (key.textureId >= textureCount)
            return fail(Code::MaterialTextureReferenceInvalid, key.sourceByteOffset + 4, key.textureId, key.frame);
        if (layer.textureGlobalSequence != NoIndex
            && static_cast<std::uint64_t>(key.frame) - layer.textureKeys.front().frame > INT32_MAX)
            return fail(Code::MaterialTextureGlobalSpanUnsupported, key.sourceByteOffset, key.frame, key.frame);
        first = false; previous = key.frame;
    }
    return Code::Ok;
}
}
bool CompiledMaterialTextures::Valid() const noexcept { return data_ != nullptr; }
AssetIdentity CompiledMaterialTextures::Identity() const noexcept { return data_ ? data_->asset.Identity() : AssetIdentity{}; }
const AssetSource& CompiledMaterialTextures::Source() const noexcept {
    static const AssetSource empty; return data_ ? data_->asset.Source() : empty;
}
bool CompiledMaterialTextures::Matches(const CompiledModel& asset) const noexcept {
    return data_ && asset.Valid() && data_->asset.SharesStorageWith(asset)
        && data_->asset.Identity() == asset.Identity() && data_->asset.Source() == asset.Source();
}
std::uint32_t CompiledMaterialTextures::MaterialLayerCount() const noexcept {
    return data_ ? static_cast<std::uint32_t>(data_->layouts.size()) : 0;
}
std::span<const MaterialLayerLayout> CompiledMaterialTextures::MaterialLayerLayouts() const noexcept {
    return data_ ? std::span<const MaterialLayerLayout>(data_->layouts) : std::span<const MaterialLayerLayout>{};
}
std::span<const std::uint32_t> CompiledMaterialTextures::ReachableTextureIds(std::uint32_t layer) const noexcept {
    return data_ && layer < data_->channels.size() ? std::span<const std::uint32_t>(data_->channels[layer].reachable)
        : std::span<const std::uint32_t>{};
}
bool CompiledMaterialTextures::HasAnimatedTexture() const noexcept { return data_ && data_->animated; }
std::uint64_t CompiledMaterialTextures::StorageBytes() const noexcept { return data_ ? data_->storageBytes : 0; }
War3Map::Result<CompiledMaterialTextures> CompileMdxMaterialTextures(
    const War3Map::Render::MdxModel& model, const CompiledModel& asset, std::uint64_t maxCompiledBytes) {
    using Out = War3Map::Result<CompiledMaterialTextures>;
    const auto failure = [](Code code) { return Out::Failure("RenderCore.MaterialTextures", CodeName(code), CodeMessage(code)); };
    const auto* compiled = ModelAccess::Get(asset);
    if (!compiled) return failure(Code::AssetIndexInvalid);
    if (!maxCompiledBytes || maxCompiledBytes > 128ull * 1024 * 1024) return failure(Code::MaterialTextureLimit);
    const War3Map::Render::MdxLimits hard;
    if (model.materials.size() > hard.maxMaterials || model.textures.size() > hard.maxTextures
        || model.globalSequences.size() > hard.maxGlobalSequences || model.sequences.size() != compiled->sequences.size())
        return failure(Code::MaterialTextureLimit);
    std::uint64_t count = 0;
    for (const auto& material : model.materials) {
        if (material.layers.size() > hard.maxLayersPerMaterial) return failure(Code::MaterialTextureLimit);
        count += material.layers.size();
    }
    if (count != asset.MaterialLayerCount()) return failure(Code::OutputRangeInvalid);
    TextureBudget budget{0, maxCompiledBytes};
    if (!budget.Add(1, sizeof(MaterialTextureData))
        || !budget.Add(count, sizeof(MaterialLayerLayout) + sizeof(MaterialTextureChannel))) return failure(Code::MaterialTextureLimit);
    try {
        auto data = std::make_shared<MaterialTextureData>(); data->asset = asset;
        data->layouts.reserve(static_cast<std::size_t>(count)); data->channels.reserve(static_cast<std::size_t>(count));
        for (std::size_t m = 0; m < model.materials.size(); ++m) {
            const auto& material = model.materials[m];
            for (std::size_t l = 0; l < material.layers.size(); ++l) {
                const auto& layer = material.layers[l];
                const MaterialLayerLayout layout{static_cast<std::uint32_t>(m), static_cast<std::uint32_t>(l),
                    layer.animatedTexture ? layer.textureSourceByteOffset : layer.staticTextureIdByteOffset};
                if (layer.textureKeys.size() > hard.maxTrackKeys) return failure(Code::MaterialTextureLimit);
                MaterialTextureChannel channel; channel.fallback = layer.textureId;
                channel.interpolation = layer.textureInterpolation; channel.globalSequence = layer.textureGlobalSequence;
                channel.issue = TextureIssue(layout, Code::Ok, layout.sourceByteOffset, layer.textureId);
                data->animated |= layer.animatedTexture;
                if (CheckTextureLayer(material, layer, layout, model.textures.size(), model.globalSequences, channel) == Code::Ok) {
                    channel.global = layer.animatedTexture && layer.textureGlobalSequence != NoIndex;
                    if (channel.global) channel.period = model.globalSequences[layer.textureGlobalSequence];
                    const auto keyCount = layer.animatedTexture ? layer.textureKeys.size() : 0;
                    if (!budget.Add(keyCount, sizeof(MaterialTextureKey))
                        || !budget.Add(keyCount + 1, sizeof(std::uint32_t))
                        || (!channel.global && !budget.Add(compiled->sequences.size(), sizeof(Range32))))
                        return failure(Code::MaterialTextureLimit);
                    channel.keys.reserve(keyCount); channel.reachable.reserve(keyCount + 1);
                    channel.reachable.push_back(layer.textureId);
                    for (const auto& key : layer.textureKeys) {
                        const auto frame = channel.global ? key.frame - layer.textureKeys.front().frame : key.frame;
                        channel.keys.push_back({frame, key.textureId, key.frame, 0, key.sourceByteOffset});
                        channel.reachable.push_back(key.textureId);
                    }
                    std::sort(channel.reachable.begin(), channel.reachable.end());
                    channel.reachable.erase(std::unique(channel.reachable.begin(), channel.reachable.end()), channel.reachable.end());
                    if (!channel.global) {
                        channel.sequenceKeys.reserve(compiled->sequences.size());
                        for (const auto& sequence : compiled->sequences) {
                            const auto begin = std::lower_bound(channel.keys.begin(), channel.keys.end(), sequence.start,
                                [](const auto& key, auto time) { return key.frame < time; });
                            const auto end = std::upper_bound(begin, channel.keys.end(), sequence.end,
                                [](auto time, const auto& key) { return time < key.frame; });
                            channel.sequenceKeys.push_back({static_cast<std::uint32_t>(begin - channel.keys.begin()),
                                static_cast<std::uint32_t>(end - begin)});
                        }
                    }
                }
                data->layouts.push_back(layout); data->channels.push_back(std::move(channel));
            }
        }
        data->storageBytes = budget.used;
        auto result = Out::Success(MaterialTextureAccess::Make(std::move(data)));
        if (result.value->HasAnimatedTexture()) result.diagnostics.push_back({War3Map::DiagnosticSeverity::Warning,
            "RenderCore.MaterialTextures", "MATERIAL_TEXTURE_PREVIEW_APPROXIMATE",
            "KMTF None(0)使用有界整数阶梯GenericPreview；原版选键/边界等价尚未证明。", std::nullopt});
        return result;
    } catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}
namespace Detail {
bool EvaluateMaterialTextures(const MaterialTextureData& source, std::uint32_t sequenceIndex,
    std::uint32_t frame, std::uint64_t global, std::span<std::uint32_t> output,
    MaterialLayerIssue& issue, Evaluation& evaluation) noexcept {
    if (output.size() != source.channels.size()) { evaluation.Fail(Code::OutputRangeInvalid); return false; }
    for (std::size_t i = 0; i < source.channels.size(); ++i) {
        const auto& channel = source.channels[i];
        if (channel.issue.error != Code::Ok) {
            issue = channel.issue; evaluation.objectId = NoIndex; evaluation.track = TrackKind::MaterialTexture;
            evaluation.Fail(issue.error); return false;
        }
        const auto range = channel.global ? Range32{0, static_cast<std::uint32_t>(channel.keys.size())}
            : channel.sequenceKeys[sequenceIndex];
        const auto keys = std::span(channel.keys).subspan(range.offset, range.count);
        const auto time = channel.global ? (channel.period ? global % channel.period : 0u) : frame;
        const auto end = std::upper_bound(keys.begin(), keys.end(), time,
            [](auto t, const auto& key) { return t < key.frame; });
        // 明确的generic边界：无动作键/首键前用真实静态ID，尾后保持最后键。
        output[i] = end == keys.begin() ? channel.fallback : (end - 1)->textureId;
    }
    return true;
}
}
}
