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
CompiledMaterialChannel CompileMaterialAlpha(const War3Map::Render::MdxLayer& layer,
    bool materialUninterpreted, MaterialLayerLayout layout, const std::vector<std::uint32_t>& periods) {
    CompiledMaterialChannel out;
    out.issue.materialIndex = layout.materialIndex; out.issue.layerIndex = layout.layerIndex;
    out.issue.sourceByteOffset = layer.staticAlphaByteOffset;
    out.issue.sourceValueBits = std::bit_cast<std::uint32_t>(layer.alpha);
    if (materialUninterpreted || layer.hasUninterpretedData) {
        out.alpha.error = out.issue.error = Code::MaterialAlphaUninterpreted;
        out.issue.sourceByteOffset = layout.sourceByteOffset; return out;
    }
    out.alpha = CompileMaterialScalarAlpha(layer, periods);
    out.alpha.error = MaterialCode(out.alpha.error); out.issue.error = out.alpha.error;
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
        const auto descending = !first && key.frame <= previous;
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
    out.sources.reserve(out.alpha.keys.size());
    if (out.alpha.error == Code::Ok) for (const auto& key : layer.alphaKeys)
        out.sources.push_back({key.sourceByteOffset + 4, key.frame, std::bit_cast<std::uint32_t>(key.value)});
    return out;
}
bool EvaluateMaterialAlphas(const ModelData& model, const Sequence& sequence, std::uint32_t frame,
    std::uint64_t global, std::span<float> output, MaterialLayerIssue& issue, Evaluation& e) noexcept {
    if (output.size() != model.materialAlphas.size()) { e.Fail(Code::OutputRangeInvalid); return false; }
    for (std::size_t i = 0; i < model.materialAlphas.size(); ++i) {
        const auto& source = model.materialAlphas[i];
        Evaluation local{};
        output[i] = SampleScalarAlpha(source.alpha, sequence, frame, global, local);
        if (local.Good()) continue;
        issue = source.issue; issue.error = MaterialCode(local.error);
        // 运行期边界/数值错误保留原左键的物理身份；不将采样结果伪装成源值。
        if (source.alpha.error == Code::Ok && !source.sources.empty()) {
            const auto time = source.alpha.global ? (source.alpha.period ? global % source.alpha.period : 0u) : frame;
            const auto begin = source.alpha.global ? source.alpha.keys.begin()
                : std::lower_bound(source.alpha.keys.begin(), source.alpha.keys.end(), sequence.start,
                    [](const auto& key, auto t) { return key.frame < t; });
            const auto stop = source.alpha.global ? source.alpha.keys.end()
                : std::upper_bound(begin, source.alpha.keys.end(), sequence.end,
                    [](auto t, const auto& key) { return t < key.frame; });
            const auto end = std::upper_bound(begin, stop, time,
                [](auto t, const auto& key) { return t < key.frame; });
            const auto selected = end == begin ? begin : end - 1;
            const auto index = static_cast<std::size_t>(selected - source.alpha.keys.begin());
            const auto& original = source.sources[index]; issue.sourceByteOffset = original.valueOffset;
            issue.sourceFrame = original.frame; issue.sourceValueBits = original.valueBits;
        }
        e.objectId = NoIndex; e.track = TrackKind::MaterialAlpha; e.Fail(issue.error); return false;
    }
    return true;
}
}
