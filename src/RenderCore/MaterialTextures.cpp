// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "MaterialTextures.h"
#include "NodeTrackCompile.h"
#include "PoseTracks.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <string>
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
// 共用节点轨迹编译器需要抛出式预算；超限换成本 sidecar 的容量失败。
struct TextureLimitExceeded {};
struct TrackBudget {
    TextureBudget& budget;
    void Charge(std::uint64_t count, std::uint64_t stride) {
        if (!stride || !budget.Add(count, stride)) throw TextureLimitExceeded{};
    }
};
template <std::size_t N>
Track<N> CompileUvTrack(const std::optional<War3Map::Render::MdxNodeTrack<N>>& source,
    const War3Map::Render::MdxModel& model, TrackBudget& budget) {
    if (source) budget.Charge(source->keys.size(), sizeof(Key<N>));
    return CompileTrack(source, model.globalSequences, model.sequences, budget);
}
bool HasKeys(const auto& track) noexcept { return track.present && !track.keys.empty(); }
// 原版 6F77C730：从单位阵起，KTAR、KTAS、KTAT 依次左乘（行向量约定，辅助函数 6F1AB2D0/6F1AAC70 均为左乘），
// 所以坐标先平移、再绕 (0.5,0.5) 缩放、最后绕 (0.5,0.5) 旋转。只有带键的轨迹参与，与原版读取器一致。
UvTransform ComposeUv(const std::array<float, 3>* translation, const Quaternion* rotation,
    const std::array<float, 3>* scaling) noexcept {
    // 行 0..2 为线性部分，行 3 为平移；与原版 4x3 存储一致。
    std::array<std::array<double, 3>, 4> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}};
    const auto translate = [&](double x, double y, double z) {
        for (std::size_t c = 0; c < 3; ++c) m[3][c] += x * m[0][c] + y * m[1][c] + z * m[2][c];
    };
    const auto linear = [&](const std::array<std::array<double, 3>, 3>& l) {
        std::array<std::array<double, 3>, 3> next{};
        for (std::size_t r = 0; r < 3; ++r)
            for (std::size_t c = 0; c < 3; ++c) next[r][c] = l[r][0] * m[0][c] + l[r][1] * m[1][c] + l[r][2] * m[2][c];
        for (std::size_t r = 0; r < 3; ++r) m[r] = next[r];
    };
    if (rotation) {
        const double x = (*rotation)[0], y = (*rotation)[1], z = (*rotation)[2], w = (*rotation)[3];
        translate(0.5, 0.5, 0);
        linear({{{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)},
            {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)},
            {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)}}});
        translate(-0.5, -0.5, 0);
    }
    if (scaling) {
        translate(0.5, 0.5, 0);
        linear({{{(*scaling)[0], 0, 0}, {0, (*scaling)[1], 0}, {0, 0, (*scaling)[2]}}});
        translate(-0.5, -0.5, 0);
    }
    if (translation) translate((*translation)[0], (*translation)[1], (*translation)[2]);
    // 输入为 (u, v, 0, 1)：u' 取第 0 列，v' 取第 1 列。
    return {static_cast<float>(m[0][0]), static_cast<float>(m[1][0]), static_cast<float>(m[3][0]),
        static_cast<float>(m[0][1]), static_cast<float>(m[1][1]), static_cast<float>(m[3][1])};
}
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
    // 没有KMTF的层贴图ID固定，与UV动画(TXAN)、坐标组和其它未解释字段无关：保持静态贴图，
    // 不能因为同一场景里别的模型有KMTF就把这些层（以及整个模型）拒绝隐藏。
    if (!layer.animatedTexture) {
        if (layer.textureId >= textureCount)
            return fail(Code::MaterialTextureReferenceInvalid, layer.staticTextureIdByteOffset, layer.textureId);
        if (!layer.textureKeys.empty()) return fail(Code::MaterialTextureUninterpreted, layout.sourceByteOffset, 0);
        return Code::Ok;
    }
    if (material.hasUninterpretedData || layer.hasUninterpretedData)
        return fail(Code::MaterialTextureUninterpreted, layout.sourceByteOffset, 0);
    if (layer.duplicateTextureByteOffset || !layer.duplicateTextureTracks.empty())
        return fail(Code::MaterialTextureUninterpreted, layer.duplicateTextureByteOffset
            ? layer.duplicateTextureByteOffset : layer.duplicateTextureTracks.front().sourceByteOffset, 0);
    // TXAN 由独立坐标渠道求值，不影响贴图ID；非零坐标组（第二套UV）仍未实现。
    if (layer.coordId != 0)
        return fail(Code::MaterialTextureUninterpreted, layer.staticTextureIdByteOffset + 8, layer.coordId);
    if (layer.textureId >= textureCount)
        return fail(Code::MaterialTextureReferenceInvalid, layer.staticTextureIdByteOffset, layer.textureId);
    if (!layer.animatedTexture) {
        if (!layer.textureKeys.empty()) return fail(Code::MaterialTextureUninterpreted, layout.sourceByteOffset, 0);
        return Code::Ok;
    }
    // 贴图ID是整数轨迹：Linear/Hermite/Bezier标记不改变取值，统一按阶梯取键（与mdx-m3-viewer整数轨迹一致）；
    // 切线原样保留。只有未知插值编号仍拒绝。
    if (layer.textureInterpolation > 3)
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
bool CompiledMaterialTextures::HasAnimatedUv() const noexcept { return data_ && data_->uvAnimated; }
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
    if (model.textureAnimations.size() > hard.maxTextures) return failure(Code::MaterialTextureLimit);
    if (!budget.Add(model.textureAnimations.size(), sizeof(UvAnimationChannel))) return failure(Code::MaterialTextureLimit);
    std::vector<War3Map::Diagnostic> uvNotes;
    std::size_t uvNotesOmitted = 0;
    const auto uvNote = [&](std::string message) {
        if (uvNotes.size() < 8) uvNotes.push_back({War3Map::DiagnosticSeverity::Warning, "RenderCore.MaterialTextures",
            "MATERIAL_UV_ANIMATION_STATIC", std::move(message), std::nullopt});
        else ++uvNotesOmitted;
    };
    try {
        auto data = std::make_shared<MaterialTextureData>(); data->asset = asset;
        data->layouts.reserve(static_cast<std::size_t>(count)); data->channels.reserve(static_cast<std::size_t>(count));
        // TXAN 轨迹与节点共用编译器；出错的条目不发布坐标动画（引用层静态坐标并报告），不影响贴图ID或几何。
        std::vector<std::uint8_t> uvUsable(model.textureAnimations.size(), 0);
        data->uvAnimations.reserve(model.textureAnimations.size());
        TrackBudget trackBudget{budget};
        for (std::size_t t = 0; t < model.textureAnimations.size(); ++t) {
            const auto& source = model.textureAnimations[t];
            UvAnimationChannel uv; uv.sourceByteOffset = source.sourceByteOffset;
            uv.translation = CompileUvTrack(source.translation, model, trackBudget);
            uv.rotation = CompileUvTrack(source.rotation, model, trackBudget);
            uv.scaling = CompileUvTrack(source.scaling, model, trackBudget);
            const auto error = uv.translation.error != Code::Ok ? uv.translation.error
                : uv.rotation.error != Code::Ok ? uv.rotation.error : uv.scaling.error;
            if (error != Code::Ok) uvNote("TXAN " + std::to_string(t) + " offset=" + std::to_string(source.sourceByteOffset)
                + "：" + CodeName(error) + "，引用它的材质层保持静态坐标。");
            else uvUsable[t] = HasKeys(uv.translation) || HasKeys(uv.rotation) || HasKeys(uv.scaling) ? 1 : 0;
            data->uvAnimations.push_back(std::move(uv));
        }
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
                if (layer.textureAnimationId != NoIndex) {
                    const auto where = "material=" + std::to_string(m) + " layer=" + std::to_string(l);
                    if (layer.textureAnimationId < uvUsable.size()) {
                        if (uvUsable[layer.textureAnimationId]) {
                            channel.uvAnimation = layer.textureAnimationId; data->uvAnimated = true;
                        }
                    } else if (!model.textureAnimationIssue.empty()) {
                        uvNote(where + "：TXAN 块未采用（" + model.textureAnimationIssue + "），保持静态坐标。");
                    } else {
                        uvNote(where + "：引用不存在的 TXAN " + std::to_string(layer.textureAnimationId) + "，保持静态坐标。");
                    }
                }
                data->layouts.push_back(layout); data->channels.push_back(std::move(channel));
            }
        }
        data->storageBytes = budget.used;
        auto result = Out::Success(MaterialTextureAccess::Make(std::move(data)));
        if (result.value->HasAnimatedTexture()) result.diagnostics.push_back({War3Map::DiagnosticSeverity::Warning,
            "RenderCore.MaterialTextures", "MATERIAL_TEXTURE_PREVIEW_APPROXIMATE",
            "KMTF按有界整数阶梯GenericPreview（含标记为Linear/Hermite/Bezier的整数轨迹）；原版选键/边界等价尚未证明。", std::nullopt});
        if (result.value->HasAnimatedUv()) result.diagnostics.push_back({War3Map::DiagnosticSeverity::Info,
            "RenderCore.MaterialTextures", "MATERIAL_UV_ANIMATION_PREVIEW",
            "TXAN按原版取键区间与合成顺序求值；插值沿用节点预览实现，未声明逐位等价；贴图矩阵按(u,v,0,1)二维仿射应用。", std::nullopt});
        result.diagnostics.insert(result.diagnostics.end(), uvNotes.begin(), uvNotes.end());
        if (uvNotesOmitted) result.diagnostics.push_back({War3Map::DiagnosticSeverity::Warning, "RenderCore.MaterialTextures",
            "MATERIAL_UV_ANIMATION_STATIC", "另有 " + std::to_string(uvNotesOmitted) + " 处 TXAN 引用保持静态坐标，未逐条列出。", std::nullopt});
        return result;
    } catch (const TextureLimitExceeded&) { return failure(Code::MaterialTextureLimit); }
    catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}
namespace Detail {
bool EvaluateMaterialTextures(const MaterialTextureData& source, std::uint32_t sequenceIndex,
    std::uint32_t frame, std::uint64_t global, std::span<std::uint32_t> output, std::span<UvTransform> uvOutput,
    MaterialLayerIssue& issue, Evaluation& evaluation) noexcept {
    if (output.size() != source.channels.size() || uvOutput.size() != source.channels.size()) {
        evaluation.Fail(Code::OutputRangeInvalid); return false;
    }
    const auto* model = ModelAccess::Get(source.asset);
    if (!model || sequenceIndex >= model->sequences.size()) { evaluation.Fail(Code::OutputRangeInvalid); return false; }
    const auto& sequence = model->sequences[sequenceIndex];
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
        uvOutput[i] = IdentityUvTransform;
        if (channel.uvAnimation == NoIndex) continue;
        const auto& uv = source.uvAnimations[channel.uvAnimation];
        // 采样警告（通用旋转/原序预览）已在编译诊断中整体披露，这里只保留失败；预算与取键策略沿用实例。
        std::array<PoseDiagnostic, 8> scratch{};
        Evaluation local{std::span(scratch)};
        local.nodeKeys = evaluation.nodeKeys; local.sourceOrderKeyBudget = evaluation.sourceOrderKeyBudget;
        const auto translation = SampleVec3Track(uv.translation, {0, 0, 0}, sequence, sequenceIndex, frame, global, local);
        const auto rotation = SampleQuaternionTrack(uv.rotation, {0, 0, 0, 1}, sequence, sequenceIndex, frame, global, local);
        const auto scaling = SampleVec3Track(uv.scaling, {1, 1, 1}, sequence, sequenceIndex, frame, global, local);
        if (local.Good()) {
            uvOutput[i] = ComposeUv(HasKeys(uv.translation) ? &translation : nullptr,
                HasKeys(uv.rotation) ? &rotation : nullptr, HasKeys(uv.scaling) ? &scaling : nullptr);
            for (const auto value : uvOutput[i]) if (!std::isfinite(value)) local.Fail(Code::PoseNumberInvalid);
        }
        if (!local.Good()) {
            const auto& layout = source.layouts[i];
            issue = {layout.materialIndex, layout.layerIndex, uv.sourceByteOffset, local.error, frame, channel.uvAnimation, 0};
            evaluation.objectId = NoIndex; evaluation.track = TrackKind::MaterialTexture;
            evaluation.failureDiagnostic = local.failureDiagnostic;
            evaluation.failureLeft = local.failureLeft; evaluation.failureRight = local.failureRight;
            evaluation.Fail(local.error); return false;
        }
    }
    return true;
}
}
}
