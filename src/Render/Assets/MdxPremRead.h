// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "MdxNodeRead.h"
#include <bit>
#include <cmath>

// PREM 字段/标志的格式参考（不是移植第三方实现）：
// https://github.com/flowtsohg/mdx-m3-viewer/blob/master/src/parsers/mdlx/particleemitter.ts
// https://github.com/flowtsohg/mdx-m3-viewer/blob/master/src/parsers/mdlx/animationmap.ts
namespace War3Map::Render::MdxReadInternal {
inline void RejectPrem(MdxPremEmitter& emitter, MdxPremSupport support, const char* code,
    std::uint64_t offset, std::uint32_t bits = 0, std::uint64_t length = 4) {
    // 只保留首个真实局部原因；所有原字段/已可界定轨迹仍继续读取。
    if (emitter.support != MdxPremSupport::Supported) return;
    emitter.support = support;
    emitter.issueCode = code;
    emitter.issueByteOffset = offset;
    emitter.issueByteLength = length;
    emitter.issueValueBits = bits;
}

inline void RetainPremTail(Reader& reader, MdxPremEmitter& emitter, std::size_t offset) {
    emitter.uninterpretedTailByteOffset = offset;
    emitter.uninterpretedTailBytes = reader.Position() + reader.Remaining() - offset;
    reader.Skip(reader.Remaining(), "PREM 未解释尾部");
}

inline std::optional<MdxPremParameter> PremParameter(std::string_view tag) {
    if (tag == "KPEE") return MdxPremParameter::EmissionRate;
    if (tag == "KPEG") return MdxPremParameter::Gravity;
    if (tag == "KPLN") return MdxPremParameter::Longitude;
    if (tag == "KPLT") return MdxPremParameter::Latitude;
    if (tag == "KPEL") return MdxPremParameter::LifeSpan;
    if (tag == "KPES") return MdxPremParameter::Speed;
    if (tag == "KPEV") return MdxPremParameter::Visibility;
    return std::nullopt;
}

inline float PremFloat(Reader& reader, std::uint32_t& rawBits) {
    rawBits = reader.U32("PREM 原始浮点位");
    return std::bit_cast<float>(rawBits);
}

inline bool ReadPremScalarTrack(Reader& reader, MdxPremEmitter& emitter, MdxPremScalarTrack& track,
    const MdxLimits& limits, std::size_t& totalKeys, std::size_t tagOffset) {
    track.sourceByteOffset = tagOffset;
    if (reader.Remaining() < 12) {
        RejectPrem(emitter, MdxPremSupport::InvalidTrack, "MDX_PREM_TRACK_INVALID", tagOffset);
        RetainPremTail(reader, emitter, tagOffset);
        return false;
    }
    track.sourceKeyCount = reader.U32("PREM 按键数");
    track.interpolation = reader.U32("PREM 插值");
    track.globalSequence = reader.U32("PREM 全局序列");
    if (track.interpolation > 3) {
        RejectPrem(emitter, MdxPremSupport::InvalidTrack, "MDX_PREM_TRACK_INVALID", tagOffset + 8, track.interpolation);
        RetainPremTail(reader, emitter, tagOffset);
        return false;
    }
    // 与公共 Node 共用现有单轨迹及总键预算；未知/超限负载不分配，不反过来隐藏父网格。
    if (track.sourceKeyCount > limits.maxTrackKeys || totalKeys > limits.maxTotalBoneTrackKeys
        || track.sourceKeyCount > limits.maxTotalBoneTrackKeys - totalKeys) {
        RejectPrem(emitter, MdxPremSupport::TrackKeyLimit, "MDX_PREM_TRACK_KEY_LIMIT", tagOffset + 4, track.sourceKeyCount);
        RetainPremTail(reader, emitter, tagOffset);
        return false;
    }
    const std::size_t stride = track.interpolation >= 2 ? 16u : 8u;
    if (track.sourceKeyCount > reader.Remaining() / stride) {
        RejectPrem(emitter, MdxPremSupport::InvalidTrack, "MDX_PREM_TRACK_INVALID", tagOffset + 4, track.sourceKeyCount);
        RetainPremTail(reader, emitter, tagOffset);
        return false;
    }
    track.sourceByteSize = 16u + static_cast<std::uint64_t>(track.sourceKeyCount) * stride;
    totalKeys += track.sourceKeyCount;
    track.keys.reserve(track.sourceKeyCount);
    if (!track.sourceKeyCount)
        RejectPrem(emitter, MdxPremSupport::InvalidTrack, "MDX_PREM_TRACK_INVALID", tagOffset + 4);
    for (std::uint32_t index = 0; index < track.sourceKeyCount; ++index) {
        MdxPremScalarKey key;
        key.sourceByteOffset = reader.Position();
        key.frame = reader.I32("PREM 原始 frame");
        key.value = PremFloat(reader, key.rawValueBits[0]);
        if (track.interpolation >= 2) {
            key.inTangent = PremFloat(reader, key.rawValueBits[1]);
            key.outTangent = PremFloat(reader, key.rawValueBits[2]);
        }
        if (key.frame < 0 || (!track.keys.empty() && key.frame <= track.keys.back().frame))
            RejectPrem(emitter, MdxPremSupport::InvalidTrack, "MDX_PREM_TRACK_KEYS_INVALID", key.sourceByteOffset,
                std::bit_cast<std::uint32_t>(key.frame));
        const std::array values{key.value, key.inTangent, key.outTangent};
        for (std::size_t component = 0; component < values.size(); ++component)
            if (!std::isfinite(values[component]))
                RejectPrem(emitter, MdxPremSupport::InvalidTrack, "MDX_PREM_TRACK_NUMBER_INVALID",
                    key.sourceByteOffset + 4 + component * 4, key.rawValueBits[component]);
        track.keys.push_back(key);
    }
    return true;
}

inline void ParsePremEmitters(Reader reader, MdxModel& model, const MdxLimits& limits, std::size_t& totalKeys) {
    while (reader.Remaining()) {
        CheckNodeBudget(model, limits);
        const auto start = reader.Position();
        const auto size = reader.U32("PREM 外层长度");
        // 大小与公共 Node 仍严格 fail-closed；只隔离已知外层内的新增负载语义。
        if (size < 384u || size - 4u > reader.Remaining())
            Fail("MDX_SIZE_INVALID", "PREM 对象大小无效。");
        auto body = reader.Until(start + size, "PREM 外层记录");
        auto node = ParseNode(body, limits, totalKeys, 284, "PREM Node");
        node.kind = MdxNodeKind::ParticleEmitter;
        MdxPremEmitter emitter;
        emitter.sourceIndex = static_cast<std::uint32_t>(model.premEmitters.size());
        emitter.otherNodeIndex = static_cast<std::uint32_t>(model.otherNodes.size());
        emitter.nodeId = node.objectId;
        emitter.flags = node.flags;
        emitter.recordByteOffset = start;
        emitter.recordByteSize = size;
        emitter.payloadByteOffset = body.Position();
        if ((emitter.flags & 0x18000u) != 0x8000u || !(emitter.flags & 0x1000u)
            || (emitter.flags & ~0x190FFu))
            RejectPrem(emitter, MdxPremSupport::UnsupportedFlags, "MDX_PREM_FLAGS_UNSUPPORTED", start + 96, emitter.flags);
        emitter.emissionRate = PremFloat(body, emitter.staticValueBits[0]);
        emitter.gravity = PremFloat(body, emitter.staticValueBits[1]);
        emitter.longitude = PremFloat(body, emitter.staticValueBits[2]);
        emitter.latitude = PremFloat(body, emitter.staticValueBits[3]);
        emitter.pathByteOffset = body.Position();
        std::size_t pathSize = emitter.rawPath.size();
        for (std::size_t index = 0; index < emitter.rawPath.size(); ++index) {
            emitter.rawPath[index] = static_cast<std::byte>(body.U8("PREM 原始 path[260]"));
            if (!emitter.pathTerminated && emitter.rawPath[index] == std::byte{0}) {
                emitter.pathTerminated = true;
                pathSize = index;
            }
        }
        emitter.path.assign(reinterpret_cast<const char*>(emitter.rawPath.data()), pathSize);
        if (!emitter.pathTerminated || emitter.path.empty())
            RejectPrem(emitter, MdxPremSupport::InvalidPath, "MDX_PREM_PATH_INVALID", emitter.pathByteOffset, 0,
                emitter.pathTerminated ? 1u : 260u);
        emitter.lifeSpan = PremFloat(body, emitter.staticValueBits[4]);
        emitter.speed = PremFloat(body, emitter.staticValueBits[5]);
        const std::array values{emitter.emissionRate, emitter.gravity, emitter.longitude, emitter.latitude,
            emitter.lifeSpan, emitter.speed};
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto fieldOffset = emitter.payloadByteOffset + (index < 4 ? index * 4 : 276 + (index - 4) * 4);
            if (!std::isfinite(values[index]))
                RejectPrem(emitter, MdxPremSupport::InvalidParameters, "MDX_PREM_NUMBER_INVALID", fieldOffset,
                    emitter.staticValueBits[index]);
        }
        while (body.Remaining()) {
            const auto trackOffset = body.Position();
            if (body.Remaining() < 4) {
                RejectPrem(emitter, MdxPremSupport::UnknownTrack, "MDX_PREM_TAIL_UNSUPPORTED", trackOffset, 0, body.Remaining());
                RetainPremTail(body, emitter, trackOffset);
                break;
            }
            const auto tag = body.Tag("PREM 参数轨迹");
            const auto parameter = PremParameter(tag);
            if (!parameter) {
                RejectPrem(emitter, MdxPremSupport::UnknownTrack, "MDX_PREM_TRACK_UNSUPPORTED", trackOffset);
                RetainPremTail(body, emitter, trackOffset);
                break;
            }
            auto& slot = emitter.tracks[static_cast<std::size_t>(*parameter)];
            if (slot) {
                RejectPrem(emitter, MdxPremSupport::DuplicateTrack, "MDX_PREM_TRACK_DUPLICATE", trackOffset);
                RetainPremTail(body, emitter, trackOffset);
                break;
            }
            slot.emplace();
            if (!ReadPremScalarTrack(body, emitter, *slot, limits, totalKeys, trackOffset)) break;
        }
        model.otherNodes.push_back(std::move(node));
        model.premEmitters.push_back(std::move(emitter));
        reader.Skip(size - 4, "PREM 外层内容");
    }
}

inline void FinalizePremEmitters(MdxModel& model) {
    // GLBS 可以出现在 PREM 后面；原 global 索引一直保留，只使此发射器局部不可用。
    for (auto& emitter : model.premEmitters)
        for (const auto& track : emitter.tracks)
            if (track && track->globalSequence != UINT32_MAX && track->globalSequence >= model.globalSequences.size())
                RejectPrem(emitter, MdxPremSupport::InvalidGlobalSequence, "MDX_PREM_GLOBAL_INVALID",
                    track->sourceByteOffset + 12, track->globalSequence);
}

inline void AppendPremDiagnostics(Result<MdxModel>& result) {
    for (const auto& emitter : result.value->premEmitters)
        if (emitter.support != MdxPremSupport::Supported)
            result.diagnostics.push_back({DiagnosticSeverity::Warning, "Mdx", emitter.issueCode,
                "PREM sourceIndex=" + std::to_string(emitter.sourceIndex) + " nodeId=" + std::to_string(emitter.nodeId)
                    + "：安全记录内的发射负载不支持，保留原节点和父模型网格。",
                OriginSpan{{}, emitter.issueByteOffset, emitter.issueByteLength, {}}});
}
} // namespace War3Map::Render::MdxReadInternal
