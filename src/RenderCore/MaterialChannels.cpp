// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "MaterialChannels.h"
#include "GeosetChannels.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace War3RenderCore::Detail {
namespace {
Code MaterialCode(Code code) noexcept {
    switch (code) {
    case Code::GeoaNumberInvalid: return Code::MaterialAlphaNumberInvalid;
    case Code::GeoaInterpolationUnsupported: return Code::MaterialAlphaInterpolationUnsupported;
    case Code::GeoaGlobalSequenceUnsupported: return Code::MaterialAlphaGlobalSequenceUnsupported;
    case Code::GeoaKeysInvalid: return Code::MaterialAlphaKeysInvalid;
    case Code::GeoaBoundaryUnsupported: return Code::MaterialAlphaBoundaryUnsupported;
    case Code::GeoaGlobalSpanUnsupported: return Code::MaterialAlphaGlobalSpanUnsupported;
    case Code::GeoaLimit: return Code::MaterialAlphaLimit;
    default: return code;
    }
}
}
std::uint64_t MaterialAlphaDuplicateStorageBytes(const War3Map::Render::MdxLayer& layer,
    bool materialUninterpreted) noexcept {
    if (materialUninterpreted || layer.hasUninterpretedData || !layer.animatedAlpha
        || !std::isfinite(layer.alpha) || layer.alphaInterpolation > 3 || layer.alphaGlobalSequence != NoIndex
        || layer.alphaKeys.size() > War3Map::Render::MdxLimits{}.maxTrackKeys) return 0;
    std::uint64_t duplicateCount = 0;
    const War3Map::Render::MdxMaterialAlphaKey* previous = nullptr;
    for (const auto& key : layer.alphaKeys) {
        if (!std::isfinite(key.value) || (layer.alphaInterpolation >= 2
            && (!std::isfinite(key.inTangent) || !std::isfinite(key.outTangent)))) return 0;
        if (previous) {
            if (key.frame < previous->frame) return 0;
            if (key.frame == previous->frame) {
                if (std::bit_cast<std::uint32_t>(key.value) != std::bit_cast<std::uint32_t>(previous->value)
                    || (layer.alphaInterpolation >= 2
                        && (std::bit_cast<std::uint32_t>(key.inTangent) != std::bit_cast<std::uint32_t>(previous->inTangent)
                            || std::bit_cast<std::uint32_t>(key.outTangent) != std::bit_cast<std::uint32_t>(previous->outTangent)))) return 0;
                ++duplicateCount;
            }
        }
        previous = &key;
    }
    if (!duplicateCount) return 0;
    return sizeof(MaterialAlphaDuplicatePreview) + layer.alphaKeys.size() * sizeof(GeoKey<1>)
        + duplicateCount * sizeof(MaterialAlphaPreviewWarning);
}
CompiledMaterialChannel CompileMaterialAlpha(const War3Map::Render::MdxLayer& layer,
    bool materialUninterpreted, MaterialLayerLayout layout, const std::vector<std::uint32_t>& periods,
    const std::vector<War3Map::Render::MdxSequence>& sequences) {
    CompiledMaterialChannel out;
    out.issue.materialIndex = layout.materialIndex; out.issue.layerIndex = layout.layerIndex;
    out.issue.sourceByteOffset = layer.staticAlphaByteOffset;
    out.issue.sourceValueBits = std::bit_cast<std::uint32_t>(layer.alpha);
    if (materialUninterpreted || layer.hasUninterpretedData) {
        out.alpha.error = out.issue.error = Code::MaterialAlphaUninterpreted;
        out.issue.sourceByteOffset = layout.sourceByteOffset; return out;
    }
    out.alpha = CompileMaterialScalarAlpha(layer, periods, false, &sequences);
    out.alpha.error = MaterialCode(out.alpha.error); out.issue.error = out.alpha.error;
    const auto compatibleBytes = out.alpha.error == Code::MaterialAlphaKeysInvalid
        ? MaterialAlphaDuplicateStorageBytes(layer, false) : 0;
    if (compatibleBytes) {
        auto compatible = CompileMaterialScalarAlpha(layer, periods, true, &sequences);
        if (compatible.error == Code::Ok) {
            auto prepared = std::make_unique<MaterialAlphaDuplicatePreview>();
            prepared->alpha = std::move(compatible);
            const auto duplicateCount = (compatibleBytes
                - sizeof(MaterialAlphaDuplicatePreview) - layer.alphaKeys.size() * sizeof(GeoKey<1>)) / sizeof(MaterialAlphaPreviewWarning);
            prepared->warnings.reserve(static_cast<std::size_t>(duplicateCount));
            for (std::size_t i = 1; i < layer.alphaKeys.size(); ++i) {
                const auto& left = layer.alphaKeys[i - 1]; const auto& right = layer.alphaKeys[i];
                if (left.frame != right.frame) continue;
                MaterialAlphaPreviewWarning warning;
                warning.sourceFrame = right.frame;
                warning.materialIndex = layout.materialIndex; warning.layerIndex = layout.layerIndex;
                warning.leftSourceByteOffset = left.sourceByteOffset; warning.rightSourceByteOffset = right.sourceByteOffset;
                warning.sourceValueBits = std::bit_cast<std::uint32_t>(right.value);
                warning.interpolation = layer.alphaInterpolation;
                if (layer.alphaInterpolation >= 2) {
                    warning.inTangentBits = std::bit_cast<std::uint32_t>(right.inTangent);
                    warning.outTangentBits = std::bit_cast<std::uint32_t>(right.outTangent);
                }
                prepared->warnings.push_back(warning);
            }
            out.identicalDuplicates = std::move(prepared);
        }
    }
    if (out.alpha.error == Code::Ok || out.identicalDuplicates) {
        out.sources.reserve(layer.alphaKeys.size());
        for (const auto& key : layer.alphaKeys)
            out.sources.push_back({key.sourceByteOffset + 4, key.frame, std::bit_cast<std::uint32_t>(key.value)});
    }
    if (!layer.animatedAlpha || !std::isfinite(layer.alpha)) return out;
    out.issue.sourceByteOffset = layer.alphaSourceByteOffset;
    if (out.alpha.error == Code::MaterialAlphaInterpolationUnsupported) {
        out.issue.sourceByteOffset += 8; out.issue.sourceValueBits = layer.alphaInterpolation; return out;
    }
    if (out.alpha.error == Code::MaterialAlphaGlobalSequenceUnsupported) {
        out.issue.sourceByteOffset += 12; out.issue.sourceValueBits = layer.alphaGlobalSequence; return out;
    }
    std::uint32_t previous = 0; bool first = true;
    for (const auto& key : layer.alphaKeys) {
        const auto badValue = !std::isfinite(key.value);
        const auto badIn = layer.alphaInterpolation >= 2 && !std::isfinite(key.inTangent);
        const auto badOut = layer.alphaInterpolation >= 2 && !std::isfinite(key.outTangent);
        // 非全局轨迹按原版逐动作区间取键，源键序本身不是编译错误；只为全局轨迹定位逆序键。
        const auto descending = layer.alphaGlobalSequence != NoIndex && !first && key.frame <= previous;
        const auto badSpan = layer.alphaGlobalSequence != NoIndex &&
            static_cast<std::uint64_t>(key.frame) - layer.alphaKeys.front().frame > INT32_MAX;
        if (badValue || badIn || badOut || descending || badSpan) {
            out.issue.sourceFrame = key.frame;
            if (badValue || badIn || badOut) {
                out.issue.sourceByteOffset = key.sourceByteOffset + (badValue ? 4 : badIn ? 8 : 12);
                out.issue.sourceValueBits = std::bit_cast<std::uint32_t>(badValue ? key.value : badIn ? key.inTangent : key.outTangent);
            } else { out.issue.sourceByteOffset = key.sourceByteOffset; out.issue.sourceValueBits = key.frame; }
            return out;
        }
        first = false; previous = key.frame;
    }
    return out;
}
bool EvaluateMaterialAlphas(const ModelData& model, std::uint32_t sequenceIndex, std::uint32_t frame,
    std::uint64_t global, std::span<float> output, MaterialLayerIssue& issue, Evaluation& e,
    MaterialAlphaKeySamplingPolicy keyPolicy, std::span<MaterialAlphaPreviewWarning> warnings,
    std::uint32_t& warningCount) noexcept {
    warningCount = 0;
    if (output.size() != model.materialAlphas.size()) { e.Fail(Code::OutputRangeInvalid); return false; }
    if (sequenceIndex >= model.sequences.size()) { e.Fail(Code::PoseSequenceInvalid); return false; }
    const auto& sequence = model.sequences[sequenceIndex];
    for (std::size_t i = 0; i < model.materialAlphas.size(); ++i) {
        const auto& source = model.materialAlphas[i];
        const bool compatible = keyPolicy == MaterialAlphaKeySamplingPolicy::IdenticalDuplicatesPreview
            && source.identicalDuplicates;
        const auto& channel = compatible ? source.identicalDuplicates->alpha : source.alpha;
        Evaluation local{};
        local.nodeKeys = e.nodeKeys; // 逐动作区间含逆序边时与 GEOA/节点同一策略：严格拒绝、原序预览按原版查找。
        output[i] = SampleScalarAlpha(channel, sequence, sequenceIndex, frame, global, local, compatible);
        if (local.Good()) {
            if (compatible) {
                const auto& originals = source.identicalDuplicates->warnings;
                if (originals.size() > e.diagnostics.size() - e.used || originals.size() > warnings.size() - warningCount) {
                    e.Fail(Code::CapacityExceeded); return false;
                }
                for (const auto& original : originals) {
                    auto warning = original; warning.diagnosticIndex = e.used;
                    e.diagnostics[e.used++] = {Code::MaterialAlphaIdenticalDuplicatesPreview, Severity::Warning,
                        NoIndex, TrackKind::MaterialAlpha, warning.sourceFrame, warning.sourceFrame};
                    warnings[warningCount++] = warning;
                }
            }
            continue;
        }
        issue = source.issue; issue.error = MaterialCode(local.error);
        // 运行期边界/数值错误保留原左键的物理身份；不将采样结果伪装成源值。
        if (channel.error == Code::Ok && !source.sources.empty() && !channel.global && !channel.ranges.empty()) {
            // 逐动作区间：定位到当前区间首个原键（键序未排序，不能二分）。
            const auto range = sequenceIndex < channel.ranges.size() ? channel.ranges[sequenceIndex] : Range32{};
            const auto index = range.count ? static_cast<std::size_t>(range.offset) : std::size_t{0};
            if (index < source.sources.size()) {
                const auto& original = source.sources[index]; issue.sourceByteOffset = original.valueOffset;
                issue.sourceFrame = original.frame; issue.sourceValueBits = original.valueBits;
            }
        } else if (channel.error == Code::Ok && !source.sources.empty()) {
            const auto time = channel.global ? (channel.period ? global % channel.period : 0u) : frame;
            const auto begin = channel.global ? channel.keys.begin()
                : std::lower_bound(channel.keys.begin(), channel.keys.end(), sequence.start,
                    [](const auto& key, auto t) { return key.frame < t; });
            const auto stop = channel.global ? channel.keys.end()
                : std::upper_bound(begin, channel.keys.end(), sequence.end,
                    [](auto t, const auto& key) { return t < key.frame; });
            const auto end = std::upper_bound(begin, stop, time,
                [](auto t, const auto& key) { return t < key.frame; });
            const auto selected = end == begin ? begin : end - 1;
            const auto index = static_cast<std::size_t>(selected - channel.keys.begin());
            const auto& original = source.sources[index]; issue.sourceByteOffset = original.valueOffset;
            issue.sourceFrame = original.frame; issue.sourceValueBits = original.valueBits;
        }
        e.objectId = NoIndex; e.track = TrackKind::MaterialAlpha; e.Fail(issue.error); return false;
    }
    return true;
}
}
