// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "PoseTracks.h"
#include "SourceOrderNodeTrack.h"

namespace War3RenderCore::Detail {
namespace {
using Interpolation = War3Map::Render::MdxInterpolation;

template <std::size_t N>
std::array<float, N> SampleTrack(const Track<N>& track, std::array<float, N> fallback,
    const Sequence& sequence, std::uint32_t sequenceIndex, std::uint32_t frame,
    std::uint64_t globalTime, Evaluation& e) noexcept {
    if (!e.Good() || !track.present) return fallback;
    if (track.error != Code::Ok) { e.Fail(track.error); return fallback; }
    const auto keyValue = [&](const Key<N>& key) {
        if (!key.finiteValue) e.Fail(Code::PoseNumberInvalid);
        return key.value;
    };
    const std::int64_t beginTime = track.global ? 0 : sequence.start;
    const std::int64_t endTime = track.global ? track.period : sequence.end;
    // 零周期的全局时钟固定为 0，仍使用全局区间，不能回退到动作时间。
    const std::int64_t time = track.global
        ? (track.period == 0 ? 0 : static_cast<std::int64_t>(globalTime % track.period)) : frame;
    const Key<N>* leftKey = nullptr;
    const Key<N>* rightKey = nullptr;
    std::int64_t offset = 0, span = 0;
    bool seam = false;
    const bool ranged = !track.global && !track.sequenceKeys.empty();
    const auto range = ranged ? track.sequenceKeys[sequenceIndex] : Range32{};
    if (ranged && track.sequenceDescending[sequenceIndex] != 0) {
        // 区间内含逆序边：严格模式拒绝；显式原序预览按原版顺序查找（扫描次数受批次预算限制）。
        if (e.nodeKeys != NodeKeySamplingPolicy::SourceOrderPreview) { e.Fail(Code::PoseKeysInvalid); return fallback; }
        const auto remaining = e.sourceOrderKeyBudget ? *e.sourceOrderKeyBudget : 0;
        if (!e.sourceOrderKeyBudget || range.count > remaining) {
            e.failureDiagnostic = Code::NodeSourceOrderKeyLimit;
            e.failureLeft = static_cast<std::int64_t>(range.count);
            e.failureRight = static_cast<std::int64_t>(remaining);
            e.Fail(Code::CapacityExceeded); return fallback;
        }
        *e.sourceOrderKeyBudget -= range.count;
        std::int64_t reverseLeft = 0, reverseRight = 0;
        (void)FirstDescendingEdge(track.keys, range.offset, range.count, reverseLeft, reverseRight);
        e.Warn(Code::NodeSourceOrderPreview, reverseLeft, reverseRight);
        const auto window = SelectGameKeyWindow(track.keys, range.offset, range.count, beginTime, endTime, time);
        if (!e.Good() || window.kind == SourceOrderWindowKind::Empty) return fallback;
        if (window.kind == SourceOrderWindowKind::Value) return keyValue(track.keys[window.left]);
        leftKey = &track.keys[window.left]; rightKey = &track.keys[window.right];
        offset = window.offset; span = window.span; seam = window.seam;
    } else {
        // 单调区间二分与原版顺序查找结果相同；全局键已经减去首帧，周期外右键仍可参与插值。
        auto begin = track.keys.begin(), end = track.keys.end();
        if (ranged) {
            begin += range.offset; end = begin + range.count;
        } else if (!track.global) {
            begin = std::lower_bound(begin, end, beginTime, [](const auto& key, auto value) { return key.frame < value; });
            end = std::upper_bound(begin, end, endTime, [](auto value, const auto& key) { return value < key.frame; });
        }
        if (begin == end) return fallback;
        if (end - begin == 1) return keyValue(*begin);
        const auto found = std::upper_bound(begin, end, time,
            [](auto value, const auto& key) { return value < key.frame; });
        if (found != begin && (found - 1)->frame == time) return keyValue(*(found - 1));
        seam = found == begin || found == end;
        leftKey = &*(seam ? end - 1 : found - 1);
        rightKey = &*(seam ? begin : found);
        // 旧原版接缝合同：首键前的时间差仍取首键；负跨度只加当前动作长度。
        offset = time - (found == begin ? begin->frame : leftKey->frame);
        span = static_cast<std::int64_t>(rightKey->frame) - leftKey->frame;
        if (span < 0) span += static_cast<std::int64_t>(sequence.end) - sequence.start;
    }
    const auto& left = *leftKey;
    const auto& right = *rightKey;
    if (seam && (time > INT32_MAX || offset < INT32_MIN || offset > INT32_MAX
        || span < INT32_MIN || span > INT32_MAX)) {
        e.Fail(Code::PoseBoundaryUnsupported); return fallback;
    }
    if (track.interpolation == Interpolation::None || span == 0) return keyValue(left);
    // 端点/时间规则来自原版；浮点插值仍沿用本预览实现，不宣称逐位原版等价。
    const auto factor = static_cast<double>(offset) / static_cast<double>(span);
    if constexpr (N == 4) {
        if (track.interpolation == Interpolation::Linear) return Slerp(left.value, right.value, factor, e);
        e.Warn(Code::GenericRotationPreview, static_cast<std::int64_t>(left.frame) + track.origin,
            static_cast<std::int64_t>(right.frame) + track.origin);
        if (!left.validOutControl || !right.validInControl) {
            e.Warn(Code::RotationControlFallback, static_cast<std::int64_t>(left.frame) + track.origin,
                static_cast<std::int64_t>(right.frame) + track.origin);
            return Slerp(left.value, right.value, factor, e);
        }
        return Squad(left.value, left.outTangent, right.inTangent, right.value, factor, e);
    } else {
        std::array<float, N> result{};
        const auto u = 1.0 - factor;
        for (std::size_t i = 0; i < N; ++i) {
            const auto a = static_cast<double>(left.value[i]), b = static_cast<double>(right.value[i]);
            double value = 0;
            switch (track.interpolation) {
            case Interpolation::Linear: value = u * a + factor * b; break;
            case Interpolation::Hermite:
                value = (2 * factor * factor * factor - 3 * factor * factor + 1) * a
                    + (factor * factor * factor - 2 * factor * factor + factor) * left.outTangent[i]
                    + (-2 * factor * factor * factor + 3 * factor * factor) * b
                    + (factor * factor * factor - factor * factor) * right.inTangent[i];
                break;
            case Interpolation::Bezier:
                value = u * u * u * a + 3 * u * u * factor * left.outTangent[i]
                    + 3 * u * factor * factor * right.inTangent[i] + factor * factor * factor * b;
                break;
            default: e.Fail(Code::PoseInterpolationUnsupported); return fallback;
            }
            result[i] = static_cast<float>(value);
        }
        (void)CheckFinite(result, e);
        return result;
    }
}
} // namespace

std::array<float, 3> SampleVec3Track(const Track<3>& track, std::array<float, 3> fallback,
    const Sequence& sequence, std::uint32_t sequenceIndex, std::uint32_t frame, std::uint64_t globalTime,
    Evaluation& e) noexcept {
    return SampleTrack(track, fallback, sequence, sequenceIndex, frame, globalTime, e);
}
Quaternion SampleQuaternionTrack(const Track<4>& track, Quaternion fallback,
    const Sequence& sequence, std::uint32_t sequenceIndex, std::uint32_t frame, std::uint64_t globalTime,
    Evaluation& e) noexcept {
    return SampleTrack(track, fallback, sequence, sequenceIndex, frame, globalTime, e);
}

std::uint32_t SequenceFrame(const Sequence& sequence, const PoseRequest& request) noexcept {
    const auto duration = static_cast<std::uint64_t>(sequence.end) - sequence.start;
    const auto elapsed = sequence.flags == 0 && duration != 0
        ? request.sequenceElapsedMs % duration : std::min(request.sequenceElapsedMs, duration);
    return static_cast<std::uint32_t>(sequence.start + elapsed);
}

bool EvaluateComputed(const ModelData& model, const PoseRequest& request, std::uint32_t frame,
    std::span<NodePose> nodes, std::span<GroupMatrix> groups, Evaluation& e) noexcept {
    const auto& sequence = model.sequences[request.sequenceIndex];
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& source = model.nodes[i];
        NodePose node;
        node.objectId = source.layout.objectId; node.parentId = source.layout.parentId; node.pivot = source.pivot;
        if (model.noControllerStaticSource) {
            // 原版未保留整个模型的动画控制器：直接使用模型局部单位矩阵，不做PIVT加减。
            // 原始pivot仍作为来源保留；禁止逐节点无轨迹时自行采用此分支。
            node.localMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            node.worldMatrix = node.localMatrix;
            nodes[i] = node;
            continue;
        }
        e.objectId = node.objectId;
        e.track = TrackKind::Translation;
        node.translation = SampleTrack(source.translation, node.translation, sequence, request.sequenceIndex, frame, request.globalElapsedMs, e);
        if (!e.Good()) return false;
        e.track = TrackKind::Rotation;
        node.rotation = Normalize(SampleTrack(source.rotation, node.rotation, sequence, request.sequenceIndex, frame, request.globalElapsedMs, e), e);
        if (!e.Good()) return false;
        e.track = TrackKind::Scale;
        node.scaling = SampleTrack(source.scaling, node.scaling, sequence, request.sequenceIndex, frame, request.globalElapsedMs, e);
        if (!e.Good()) return false;
        node.localMatrix = LocalMatrix(node, e);
        node.worldMatrix = source.layout.parentIndex == NoIndex ? node.localMatrix
            : Multiply(nodes[source.layout.parentIndex].worldMatrix, node.localMatrix, e);
        if ((source.layout.flags & 7u) != 0 && e.Good())
            node.worldMatrix = InheritedWorld(node, source.layout.parentIndex == NoIndex ? nullptr
                : &nodes[source.layout.parentIndex], source.layout.flags, e);
        e.track = TrackKind::None;
        if ((source.layout.flags & 0xF8u) != 0 && e.Good()) {
            if (request.hasCamera != 0) {
                node.worldMatrix = CameraFacingWorld(node, node.worldMatrix, request.camera, source.layout.flags, e);
                if (e.Good()) e.Warn(Code::GenericCameraPreview);
            } else e.Warn(Code::CameraStaticPreview);
        }
        if (!e.Good()) return false;
        nodes[i] = node;
    }
    for (std::size_t i = 0; i < model.groups.size(); ++i) {
        const auto members = model.groups[i];
        if (model.rigidStaticPreviewSource && members.count == 0) {
            groups[i].values = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
            continue;
        }
        std::array<double, 12> sum{};
        for (std::uint32_t j = 0; j < members.count; ++j) {
            const auto& matrix = nodes[model.groupMembers[members.offset + j]].worldMatrix;
            if (matrix[3] != 0 || matrix[7] != 0 || matrix[11] != 0 || matrix[15] != 1) {
                e.Fail(Code::SkinMatrixUnsupported); return false;
            }
            for (std::size_t column = 0; column < 4; ++column)
                for (std::size_t row = 0; row < 3; ++row) sum[column * 3 + row] += matrix[column * 4 + row];
        }
        GroupMatrix matrix;
        for (std::size_t j = 0; j < 12; ++j) matrix.values[j] = static_cast<float>(sum[j] / members.count);
        if (!CheckFinite(matrix.values, e)) return false;
        groups[i] = matrix;
    }
    return true;
}

} // namespace War3RenderCore::Detail
