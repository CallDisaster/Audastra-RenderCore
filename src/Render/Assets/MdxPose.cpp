// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3MapCore/Render/MdxPose.h"
#include "MdxRigidStaticPreviewInternal.h"
#include "MdxNormalPreview.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace War3Map::Render {
namespace {

using Matrix = std::array<float, 16>;

struct PoseError final : std::runtime_error {
    std::string code;
    PoseError(std::string errorCode, std::string message)
        : std::runtime_error(message), code(std::move(errorCode)) {}
};

[[noreturn]] void Fail(const char* code, const std::string& message) {
    throw PoseError(code, message);
}

template <std::size_t N>
void CheckFinite(const std::array<float, N>& values) {
    for (const auto value : values)
        if (!std::isfinite(value)) Fail("MDX_POSE_NUMBER_INVALID", "节点轨迹或矩阵产生非有限数值。");
}

std::array<float, 4> Normalize(std::array<float, 4> value) {
    CheckFinite(value);
    double squared = 0;
    for (const auto component : value) squared += static_cast<double>(component) * component;
    if (!std::isfinite(squared) || squared < 1e-20)
        Fail("MDX_POSE_ROTATION_INVALID", "节点四元数长度为零或超出可求值范围。");
    const auto factor = 1.0 / std::sqrt(squared);
    for (auto& component : value) component = static_cast<float>(component * factor);
    CheckFinite(value);
    return value;
}

std::array<float, 4> Slerp(std::array<float, 4> first, std::array<float, 4> second, double factor) {
    first = Normalize(first);
    second = Normalize(second);
    double cosine = 0;
    for (std::size_t i = 0; i < 4; ++i) cosine += static_cast<double>(first[i]) * second[i];
    if (cosine < 0) {
        for (auto& component : second) component = -component;
        cosine = -cosine;
    }
    cosine = std::clamp(cosine, 0.0, 1.0);
    double a = 1.0 - factor, b = factor;
    if (cosine < 0.9995) {
        const auto angle = std::acos(cosine);
        const auto denominator = std::sin(angle);
        if (!(denominator > 0)) Fail("MDX_POSE_ROTATION_INVALID", "四元数球面插值分母无效。");
        a = std::sin((1.0 - factor) * angle) / denominator;
        b = std::sin(factor * angle) / denominator;
    }
    std::array<float, 4> result{};
    for (std::size_t i = 0; i < 4; ++i)
        result[i] = static_cast<float>(a * first[i] + b * second[i]);
    return Normalize(result);
}

std::array<float, 4> Squad(const std::array<float, 4>& first,
    const std::array<float, 4>& outControl, const std::array<float, 4>& inControl,
    const std::array<float, 4>& second, double factor) {
    // 通用球面四边形插值：三次 SLERP；此处自行实现，不复制外部代码。
    // 参照 Microsoft XMQuaternionSquad 的四个单位四元数约定：
    // https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmquaternionsquad
    // 以及 gl-matrix 的公开 sqlerp 数学组合：
    // https://github.com/toji/gl-matrix/blob/master/src/quat.js
    // MDX 的 Hermite/Bezier 切线如何对应原版曲线尚未验证，故仅用于预览。
    const auto endpoints = Slerp(first, second, factor);
    const auto controls = Slerp(outControl, inControl, factor);
    return Slerp(endpoints, controls, 2.0 * factor * (1.0 - factor));
}

void PreviewWarning(std::vector<Diagnostic>& warnings, const char* code, std::string message) {
    warnings.push_back({DiagnosticSeverity::Warning, "Mdx.Pose", code, std::move(message), std::nullopt});
}

struct TrackTime {
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::int64_t frame = 0;
};

template <std::size_t N>
std::array<float, N> SampleTrack(const MdxModel& model, const std::optional<MdxNodeTrack<N>>& optional,
    std::array<float, N> fallback, const MdxSequence& sequence, std::uint32_t sequenceFrame,
    std::uint64_t globalElapsedMs, std::uint32_t objectId, const char* trackName,
    std::vector<Diagnostic>& warnings) {
    if (!optional) return fallback;
    const auto& track = *optional;
    if (track.interpolation != MdxInterpolation::None && track.interpolation != MdxInterpolation::Linear
        && track.interpolation != MdxInterpolation::Hermite && track.interpolation != MdxInterpolation::Bezier)
        Fail("MDX_POSE_INTERPOLATION_UNSUPPORTED", "未知节点轨迹插值类型。");
    if (track.keys.size() > MdxLimits{}.maxTrackKeys)
        Fail("MDX_POSE_LIMIT", "节点轨迹按键超过读取器硬上限。");
    TrackTime time{sequence.start, sequence.end, sequenceFrame};
    if (track.globalSequence != UINT32_MAX) {
        if (track.globalSequence >= model.globalSequences.size())
            Fail("MDX_POSE_GLOBAL_SEQUENCE_INVALID", "节点轨迹引用不存在的全局序列。");
        const auto period = model.globalSequences[track.globalSequence];
        // 参考实现不改原键：将时钟加回首源帧，与编译侧归零键表独立对照。
        time = {0, period, 0};
        if (period != 0) time.frame = static_cast<std::int64_t>(globalElapsedMs % period);
        if (!track.keys.empty()) time.frame += track.keys.front().frame;
    }
    std::vector<const MdxNodeKey<N>*> keys;
    keys.reserve(track.keys.size());
    const bool global = track.globalSequence != UINT32_MAX;
    std::int64_t previous = std::numeric_limits<std::int64_t>::min();
    for (const auto& key : track.keys) {
        if (key.frame < previous && (global || (key.frame <= time.end && previous >= time.start)))
            Fail("MDX_POSE_KEYS_INVALID", "本次节点采样触及逆序帧区间；未实现原版逆序缓存语义，同帧按源序保留。");
        previous = key.frame;
        if (global && static_cast<std::int64_t>(key.frame) - track.keys.front().frame > INT32_MAX)
            Fail("MDX_POSE_GLOBAL_SPAN_UNSUPPORTED", "全局轨迹归零后的帧跨度超出已确认的有符号范围。");
        if (global || (key.frame >= time.start && key.frame <= time.end)) keys.push_back(&key);
    }
    if (keys.empty()) return fallback;
    if (keys.size() == 1) {
        CheckFinite(keys.front()->value);
        return keys.front()->value;
    }
    // 独立参考沿用源键，跨过全部相等帧后取最后一条，不去重或重排。
    const auto found = std::upper_bound(keys.begin(), keys.end(), time.frame,
        [](std::int64_t frame, const auto* key) { return frame < key->frame; });
    if (found != keys.begin() && (*(found - 1))->frame == time.frame) {
        CheckFinite((*(found - 1))->value);
        return (*(found - 1))->value;
    }
    const bool outside = found == keys.begin() || found == keys.end();
    const auto& left = outside ? *keys.back() : **(found - 1);
    const auto& right = outside ? *keys.front() : **found;
    // 独立参考保留源帧：首键前的偏移基准仍是首键，实际端点则是末键/首键。
    const auto delta = time.frame - (found == keys.begin() ? keys.front()->frame : left.frame);
    auto interval = static_cast<std::int64_t>(right.frame) - left.frame;
    if (interval < 0) interval += static_cast<std::int64_t>(sequence.end) - sequence.start;
    const auto nativeTime = time.frame - (global ? track.keys.front().frame : 0);
    if (outside && (nativeTime > INT32_MAX || delta < INT32_MIN || delta > INT32_MAX
        || interval < INT32_MIN || interval > INT32_MAX))
        Fail("MDX_POSE_BOUNDARY_UNSUPPORTED", "轨迹接缝超出已确认的有符号时间范围，尚未模拟原版整数回绕。");
    if (track.interpolation == MdxInterpolation::None || interval == 0) {
        CheckFinite(left.value);
        return left.value;
    }
    const auto factor = static_cast<double>(delta) / static_cast<double>(interval);
    if constexpr (N == 4) {
        if (track.interpolation == MdxInterpolation::Linear)
            return Slerp(left.value, right.value, factor);
        PreviewWarning(warnings, "MDX_POSE_GENERIC_ROTATION_PREVIEW", std::string(trackName) + " objectId="
            + std::to_string(objectId) + " 在 " + std::to_string(left.frame) + ".."
            + std::to_string(right.frame) + " 帧之间使用通用四元数 squad 预览，非原版等价证明。");
        const auto validControl = [](const std::array<float, 4>& control) {
            double squared = 0;
            for (const auto value : control) {
                if (!std::isfinite(value)) return false;
                squared += static_cast<double>(value) * value;
            }
            return std::isfinite(squared) && squared >= 1e-20;
        };
        if (!validControl(left.outTangent) || !validControl(right.inTangent)) {
            PreviewWarning(warnings, "MDX_POSE_ROTATION_CONTROL_FALLBACK", std::string(trackName) + " objectId="
                + std::to_string(objectId) + " 的曲线控制四元数无效；仅该段退为端点球面插值预览。");
            return Slerp(left.value, right.value, factor);
        }
        return Squad(left.value, left.outTangent, right.inTangent, right.value, factor);
    } else {
        std::array<float, N> result{};
        const auto u = 1.0 - factor;
        for (std::size_t i = 0; i < N; ++i) {
            const auto a = static_cast<double>(left.value[i]);
            const auto b = static_cast<double>(right.value[i]);
            double value = 0;
            switch (track.interpolation) {
            case MdxInterpolation::Linear: value = u * a + factor * b; break;
            case MdxInterpolation::Hermite:
                value = (2 * factor * factor * factor - 3 * factor * factor + 1) * a
                    + (factor * factor * factor - 2 * factor * factor + factor) * left.outTangent[i]
                    + (-2 * factor * factor * factor + 3 * factor * factor) * b
                    + (factor * factor * factor - factor * factor) * right.inTangent[i];
                break;
            case MdxInterpolation::Bezier:
                value = u * u * u * a + 3 * u * u * factor * left.outTangent[i]
                    + 3 * u * factor * factor * right.inTangent[i] + factor * factor * factor * b;
                break;
            default: Fail("MDX_POSE_INTERPOLATION_UNSUPPORTED", "未知节点轨迹插值类型。");
            }
            result[i] = static_cast<float>(value);
        }
        CheckFinite(result);
        return result;
    }
}

Matrix Identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Matrix LocalMatrix(const MdxPoseNode& node) {
    const auto& q = node.rotation;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    auto matrix = Identity();
    matrix[0] = static_cast<float>((1 - 2 * (y * y + z * z)) * node.scaling[0]);
    matrix[1] = static_cast<float>((2 * (x * y + z * w)) * node.scaling[0]);
    matrix[2] = static_cast<float>((2 * (x * z - y * w)) * node.scaling[0]);
    matrix[4] = static_cast<float>((2 * (x * y - z * w)) * node.scaling[1]);
    matrix[5] = static_cast<float>((1 - 2 * (x * x + z * z)) * node.scaling[1]);
    matrix[6] = static_cast<float>((2 * (y * z + x * w)) * node.scaling[1]);
    matrix[8] = static_cast<float>((2 * (x * z + y * w)) * node.scaling[2]);
    matrix[9] = static_cast<float>((2 * (y * z - x * w)) * node.scaling[2]);
    matrix[10] = static_cast<float>((1 - 2 * (x * x + y * y)) * node.scaling[2]);
    for (std::size_t row = 0; row < 3; ++row) {
        double moved = node.translation[row] + node.pivot[row];
        for (std::size_t column = 0; column < 3; ++column)
            moved -= static_cast<double>(matrix[column * 4 + row]) * node.pivot[column];
        matrix[12 + row] = static_cast<float>(moved);
    }
    CheckFinite(matrix);
    return matrix;
}

Matrix Multiply(const Matrix& first, const Matrix& second) {
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            double value = 0;
            for (std::size_t inner = 0; inner < 4; ++inner)
                value += static_cast<double>(first[inner * 4 + row]) * second[column * 4 + inner];
            result[column * 4 + row] = static_cast<float>(value);
        }
    }
    CheckFinite(result);
    return result;
}

using Vec3 = std::array<float, 3>;

Vec3 Subtract(const Vec3& first, const Vec3& second) {
    return {first[0] - second[0], first[1] - second[1], first[2] - second[2]};
}

Vec3 Cross(const Vec3& first, const Vec3& second) {
    return {first[1] * second[2] - first[2] * second[1],
        first[2] * second[0] - first[0] * second[2],
        first[0] * second[1] - first[1] * second[0]};
}

Vec3 UnitVector(const Vec3& value, const char* code) {
    CheckFinite(value);
    const auto length = std::hypot(static_cast<double>(value[0]), value[1], value[2]);
    if (!std::isfinite(length) || length < 1e-8)
        Fail(code, "相机方向或向上方向退化，无法求通用预览姿态。");
    Vec3 result{};
    for (std::size_t i = 0; i < 3; ++i) result[i] = static_cast<float>(value[i] / length);
    CheckFinite(result);
    return result;
}

void CheckCamera(const MdxPoseCamera& camera) {
    CheckFinite(camera.eye);
    CheckFinite(camera.up);
    for (const auto coordinate : camera.eye)
        if (std::abs(coordinate) > 400000.0f)
            Fail("MDX_POSE_CAMERA_INVALID", "模型局部相机视点超出有界预览范围。");
    for (const auto coordinate : camera.up)
        if (std::abs(coordinate) > 10000.0f)
            Fail("MDX_POSE_CAMERA_INVALID", "模型局部相机向上向量超出有界预览范围。");
    (void)UnitVector(camera.up, "MDX_POSE_CAMERA_INVALID");
}

Vec3 PivotWorld(const Matrix& matrix, const Vec3& pivot) {
    Vec3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        const double value = static_cast<double>(matrix[12 + row])
            + static_cast<double>(matrix[row]) * pivot[0]
            + static_cast<double>(matrix[4 + row]) * pivot[1]
            + static_cast<double>(matrix[8 + row]) * pivot[2];
        result[row] = static_cast<float>(value);
    }
    CheckFinite(result);
    return result;
}

Matrix AxisRotation(std::size_t axis, double cosine, double sine) {
    auto matrix = Identity();
    const auto a = (axis + 1) % 3, b = (axis + 2) % 3;
    matrix[a * 4 + a] = static_cast<float>(cosine);
    matrix[b * 4 + a] = static_cast<float>(-sine);
    matrix[a * 4 + b] = static_cast<float>(sine);
    matrix[b * 4 + b] = static_cast<float>(cosine);
    return matrix;
}

Matrix FacingBasis(const Vec3& towardEye, const Vec3& cameraUp, std::uint32_t flags,
    std::vector<Diagnostic>& warnings, std::uint32_t objectId) {
    const auto lock = flags & 0x70u;
    if (lock != 0 && lock != 0x10u && lock != 0x20u && lock != 0x40u)
        Fail("MDX_POSE_CAMERA_FLAGS_UNSUPPORTED", "相机节点同时锁定多个轴，通用预览没有确定的自由度。");
    if (lock != 0) {
        const std::size_t axis = lock == 0x10u ? 0 : lock == 0x20u ? 1 : 2;
        // X/Z 锁轴以 +Y 为面朝方向；Y 锁轴以 +Z 为面朝方向。
        const std::size_t a = axis == 1 ? 2 : 1;
        const std::size_t b = 3 - axis - a;
        const double towardA = towardEye[a], towardB = towardEye[b];
        const auto length = std::hypot(towardA, towardB);
        if (length < 1e-8) {
            PreviewWarning(warnings, "MDX_POSE_CAMERA_AXIS_SINGULAR", "objectId="
                + std::to_string(objectId) + " 视线平行锁定轴，保留零角度通用预览。");
            return Identity();
        }
        // Rodrigues 对单位主轴的二维平面旋转；符号由右手坐标与列向量约定决定。
        const auto reference = Vec3{a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f};
        const auto rotated = Cross(Vec3{axis == 0 ? 1.0f : 0.0f,
            axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f}, reference);
        const double cosine = towardA / length;
        const double sine = towardEye[b] / (length * rotated[b]);
        return AxisRotation(axis, cosine, sine);
    }
    // 通用球面看向矩阵的逆：局部 +Y 朝视点，+Z 与相机 up 尽可能一致。
    // 参照 Microsoft XMMatrixLookAtLH 的 eye/target/up 构造，但坐标采用 MDX 的 Z-up。
    // https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmmatrixlookatlh
    const auto facing = UnitVector(towardEye, "MDX_POSE_CAMERA_INVALID");
    auto right = Cross(facing, cameraUp);
    if (std::hypot(static_cast<double>(right[0]), right[1], right[2]) < 1e-8)
        right = Cross(facing, std::abs(facing[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0});
    right = UnitVector(right, "MDX_POSE_CAMERA_INVALID");
    const auto up = UnitVector(Cross(right, facing), "MDX_POSE_CAMERA_INVALID");
    auto matrix = Identity();
    for (std::size_t row = 0; row < 3; ++row) {
        matrix[row] = right[row];
        matrix[4 + row] = facing[row];
        matrix[8 + row] = up[row];
    }
    return matrix;
}

Matrix CameraFacingWorld(const MdxPoseNode& node, const Matrix& staticWorld,
    const MdxPoseCamera& camera, std::uint32_t flags, std::vector<Diagnostic>& warnings) {
    const auto anchor = PivotWorld(staticWorld, node.pivot);
    const auto towardEye = Subtract(camera.eye, anchor);
    const auto basis = FacingBasis(towardEye, UnitVector(camera.up, "MDX_POSE_CAMERA_INVALID"),
        flags, warnings, node.objectId);
    auto spin = node;
    spin.pivot = {};
    spin.translation = {};
    const auto oriented = Multiply(basis, LocalMatrix(spin));
    auto world = oriented;
    for (std::size_t row = 0; row < 3; ++row) {
        double moved = anchor[row];
        for (std::size_t column = 0; column < 3; ++column)
            moved -= static_cast<double>(oriented[column * 4 + row]) * node.pivot[column];
        world[12 + row] = static_cast<float>(moved);
    }
    CheckFinite(world);
    return world;
}

#include "MdxInheritanceReference.inl"

MdxPoseSample Sample(const MdxModel& model, const MdxPoseRequest& request,
    std::vector<Diagnostic>& warnings) {
    const MdxLimits hard;
    if (model.version != 800 || model.bones.size() > hard.maxBones || model.helpers.size() > hard.maxHelpers
        || MdxNodeCount(model) > hard.maxNodes || model.pivots.size() > hard.maxPivots || model.sequences.size() > hard.maxSequences
        || model.globalSequences.size() > hard.maxGlobalSequences)
        Fail("MDX_POSE_MODEL_UNSUPPORTED", "姿态求值只接受有界的经典 MDX 800 模型。");
    if (request.sequenceIndex >= model.sequences.size())
        Fail("MDX_POSE_SEQUENCE_INVALID", "请求的动作序号不存在。");
    if (request.camera) CheckCamera(*request.camera);
    const auto& sequence = model.sequences[request.sequenceIndex];
    if (sequence.end < sequence.start || sequence.flags > 1)
        Fail("MDX_POSE_SEQUENCE_UNSUPPORTED", "动作区间或循环标志尚不支持。");
    const auto duration = static_cast<std::uint64_t>(sequence.end) - sequence.start;
    const auto elapsed = sequence.flags == 0 && duration != 0
        ? request.sequenceElapsedMs % duration : std::min(request.sequenceElapsedMs, duration);
    const auto frame = static_cast<std::uint32_t>(sequence.start + elapsed);

    std::map<std::uint32_t, const MdxNode*> nodes;
    std::size_t totalTrackKeys = 0;
    const auto addNode = [&](const MdxNode& node) {
        if (node.objectId == UINT32_MAX || !nodes.emplace(node.objectId, &node).second)
            Fail("MDX_POSE_NODE_INVALID", "公共节点 ID 重复或为无效哨兵。");
        const auto addTrack = [&](const auto& track) {
            if (!track) return;
            const auto count = track->keys.size();
            if (count > hard.maxTrackKeys || count > hard.maxTotalBoneTrackKeys - totalTrackKeys)
                Fail("MDX_POSE_LIMIT", "节点轨迹总按键超过读取器硬上限。");
            totalTrackKeys += count;
        };
        addTrack(node.translation);
        addTrack(node.rotation);
        addTrack(node.scaling);
    };
    ForEachMdxNode(model, addNode);
    const auto requested = nodes.find(request.objectId);
    if (requested == nodes.end()) Fail("MDX_POSE_NODE_UNAVAILABLE", "请求的 公共节点不存在。");
    std::vector<const MdxNode*> reverseChain;
    std::map<std::uint32_t, bool> seen;
    const MdxNode* current = requested->second;
    while (current) {
        if (!seen.emplace(current->objectId, true).second)
            Fail("MDX_POSE_PARENT_INVALID", "节点父链形成环。");
        if (current->objectId >= model.pivots.size())
            Fail("MDX_POSE_PIVOT_UNAVAILABLE", "节点缺少按 objectId 对齐的 PIVT 轴心。");
        if ((current->flags & ~(MdxNodeNonTransformFlags(current->kind) | 0xFFu)) != 0)
            Fail("MDX_POSE_NODE_FLAGS_UNSUPPORTED", "节点含未实现的继承或变换标志。");
        reverseChain.push_back(current);
        if (current->parentId == -1) break;
        if (current->parentId < -1)
            Fail("MDX_POSE_PARENT_INVALID", "节点父 ID 无效。");
        const auto parent = nodes.find(static_cast<std::uint32_t>(current->parentId));
        if (parent == nodes.end()) Fail("MDX_POSE_PARENT_UNAVAILABLE", "节点父 ID 指向未解析的节点类型。");
        current = parent->second;
    }
    MdxPoseSample result;
    result.sequenceFrame = frame;
    result.chain.reserve(reverseChain.size());
    for (auto at = reverseChain.rbegin(); at != reverseChain.rend(); ++at) {
        const auto& source = **at;
        MdxPoseNode node;
        node.objectId = source.objectId;
        node.parentId = source.parentId;
        node.pivot = model.pivots[source.objectId];
        node.translation = SampleTrack(model, source.translation, node.translation, sequence, frame,
            request.globalElapsedMs, source.objectId, "KGTR", warnings);
        node.rotation = Normalize(SampleTrack(model, source.rotation, node.rotation, sequence, frame,
            request.globalElapsedMs, source.objectId, "KGRT", warnings));
        node.scaling = SampleTrack(model, source.scaling, node.scaling, sequence, frame,
            request.globalElapsedMs, source.objectId, "KGSC", warnings);
        node.localMatrix = LocalMatrix(node);
        node.worldMatrix = result.chain.empty() ? node.localMatrix : Multiply(result.chain.back().worldMatrix, node.localMatrix);
        if ((source.flags & 7u) != 0)
            node.worldMatrix = InheritedWorld(node, result.chain.empty() ? nullptr : &result.chain.back(), source.flags);
        if ((source.flags & 0xF8u) != 0) {
            if (request.camera) {
                node.worldMatrix = CameraFacingWorld(node, node.worldMatrix, *request.camera, source.flags, warnings);
                PreviewWarning(warnings, "MDX_POSE_GENERIC_CAMERA_PREVIEW", "objectId="
                    + std::to_string(source.objectId) + " 使用有界相机朝向通用算法预览，非原版等价证明。");
            } else {
                PreviewWarning(warnings, "MDX_POSE_CAMERA_STATIC_PREVIEW", "objectId="
                    + std::to_string(source.objectId) + " 无相机输入，暂以静态节点姿态预览。");
            }
        }
        result.chain.push_back(std::move(node));
    }
    return result;
}

void CheckAffine(const Matrix& matrix) {
    CheckFinite(matrix);
    if (matrix[3] != 0 || matrix[7] != 0 || matrix[11] != 0 || matrix[15] != 1)
        Fail("MDX_SKIN_MATRIX_UNSUPPORTED", "节点姿态矩阵不是可支持的仿射矩阵。");
}

std::array<float, 3> TransformPosition(const Matrix& transform, const std::array<float, 3>& point) {
    CheckFinite(point);
    std::array<float, 3> result{};
    for (std::size_t row = 0; row < 3; ++row) {
        const double value = static_cast<double>(transform[12 + row])
            + static_cast<double>(transform[row]) * point[0]
            + static_cast<double>(transform[4 + row]) * point[1]
            + static_cast<double>(transform[8 + row]) * point[2];
        result[row] = static_cast<float>(value);
    }
    CheckFinite(result);
    return result;
}

std::array<float, 3> TransformNormal(const Matrix& transform, const std::array<float, 3>& normal) {
    CheckFinite(normal);
    // 原始零法线没有方向，保留零而非伪造受骨骼影响的方向；非零变换退化仍拒绝。
    if (normal[0] == 0 && normal[1] == 0 && normal[2] == 0) return {};
    std::array<double, 3> result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            result[row] += static_cast<double>(transform[column * 4 + row]) * normal[column];
    const auto magnitude = std::hypot(result[0], result[1], result[2]);
    if (!std::isfinite(magnitude) || magnitude <= 1e-20)
        Fail("MDX_SKIN_NORMAL_INVALID", "输入法线为零或姿态法线无法归一化。");
    std::array<float, 3> normalized{};
    for (std::size_t row = 0; row < 3; ++row)
        normalized[row] = static_cast<float>(result[row] / magnitude);
    CheckFinite(normalized);
    return normalized;
}

MdxRigidSkinnedGeoset SkinRigidGeoset(const MdxModel& model, const MdxRigidSkinRequest& request,
    std::vector<Diagnostic>& warnings) {
    const MdxLimits hard;
    if (!model.skinningTopologyComplete)
        Fail("MDX_SKIN_TOPOLOGY_INCOMPLETE", "模型骨骼/矩阵拓扑未闭合：" + model.skinningTopologyIssue);
    if (model.version != 800 || model.geosets.size() > hard.maxGeosets)
        Fail("MDX_SKIN_MODEL_UNSUPPORTED", "只接受有界的经典 MDX 800 模型。");
    if (request.geosetIndex >= model.geosets.size())
        Fail("MDX_SKIN_GEOSET_INVALID", "请求的 geoset 序号不存在。");
    const auto& geoset = model.geosets[request.geosetIndex];
    if (geoset.positions.empty() || geoset.positions.size() > hard.maxVerticesPerGeoset
        || geoset.normals.size() != geoset.positions.size()
        || geoset.vertexGroups.size() != geoset.positions.size()
        || geoset.matrixGroupSizes.empty() || geoset.matrixGroupSizes.size() > hard.maxMatrixGroupsPerGeoset
        || geoset.matrixIndices.size() > hard.maxMatrixIndicesPerGeoset
        || geoset.uninterpretedTailBytes != 0)
        Fail("MDX_SKIN_GEOMETRY_UNSUPPORTED", "geoset 缺少有界、完整的顶点/法线/矩阵组数据。");

    std::vector<Matrix> groupTransforms;
    groupTransforms.reserve(geoset.matrixGroupSizes.size());
    std::map<std::uint32_t, Matrix> nodeTransforms;
    std::set<std::pair<std::string, std::string>> reportedWarnings;
    MdxRigidSkinnedGeoset result;
    std::size_t offset = 0;
    for (const auto size : geoset.matrixGroupSizes) {
        if (size == 0 || size > geoset.matrixIndices.size() - offset)
            Fail("MDX_SKIN_GEOMETRY_UNSUPPORTED", "MTGC 与 MATS 长度不一致。");
        std::array<double, 16> sum{};
        for (std::size_t member = 0; member < size; ++member) {
            const auto objectId = geoset.matrixIndices[offset + member];
            auto found = nodeTransforms.find(objectId);
            if (found == nodeTransforms.end()) {
                const auto pose = SampleMdxNodePose(model, {request.sequenceIndex, request.sequenceElapsedMs,
                    request.globalElapsedMs, objectId, request.camera});
                if (!pose.Succeeded()) {
                    const auto& diagnostic = pose.diagnostics.front();
                    Fail(diagnostic.code.c_str(), diagnostic.message);
                }
                for (const auto& diagnostic : pose.diagnostics) {
                    if (reportedWarnings.emplace(diagnostic.code, diagnostic.message).second)
                        warnings.push_back(diagnostic);
                }
                result.sequenceFrame = pose.value->sequenceFrame;
                CheckAffine(pose.value->chain.back().worldMatrix);
                found = nodeTransforms.emplace(objectId, pose.value->chain.back().worldMatrix).first;
            }
            // 原版先平均整条组矩阵，再按 GNDX 槽号对每个顶点做一次变换。
            for (std::size_t column = 0; column < 4; ++column)
                for (std::size_t row = 0; row < 3; ++row)
                    sum[column * 4 + row] += found->second[column * 4 + row];
        }
        auto averaged = Identity();
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t row = 0; row < 3; ++row)
                averaged[column * 4 + row] = static_cast<float>(sum[column * 4 + row] / size);
        CheckAffine(averaged);
        groupTransforms.push_back(averaged);
        offset += size;
    }
    if (offset != geoset.matrixIndices.size())
        Fail("MDX_SKIN_GEOMETRY_UNSUPPORTED", "MTGC 与 MATS 长度不一致。");

    const auto zeroNormals = MdxZeroNormalCount(geoset.normals);
    if (zeroNormals != 0) warnings.push_back(MdxZeroNormalWarning(request.geosetIndex, zeroNormals));
    result.positions.reserve(geoset.positions.size());
    result.normals.reserve(geoset.normals.size());
    for (std::size_t vertex = 0; vertex < geoset.positions.size(); ++vertex) {
        const auto group = geoset.vertexGroups[vertex];
        if (group >= groupTransforms.size())
            Fail("MDX_SKIN_GEOMETRY_UNSUPPORTED", "GNDX 槽号超出 MTGC 组数。");
        const auto& transform = groupTransforms[group];
        result.positions.push_back(TransformPosition(transform, geoset.positions[vertex]));
        result.normals.push_back(TransformNormal(transform, geoset.normals[vertex]));
    }
    return result;
}

MdxRigidSkinnedGeoset StaticPreviewGeoset(const MdxModel& model, const MdxRigidSkinRequest& request,
    std::vector<Diagnostic>& warnings) {
    if (!MdxRigidStaticPreviewInternal::Qualified(model))
        Fail("MDX_RIGID_STATIC_PREVIEW_UNQUALIFIED", "预览近似的当前几何、矩阵、节点或原字节来源资格不一致。");
    if (request.geosetIndex >= model.geosets.size())
        Fail("MDX_SKIN_GEOSET_INVALID", "请求的 geoset 序号不存在。");
    if (request.sequenceIndex >= model.sequences.size())
        Fail("MDX_POSE_SEQUENCE_INVALID", "请求的动作序号不存在。");
    if (request.camera) CheckCamera(*request.camera);
    const auto& sequence = model.sequences[request.sequenceIndex];
    if (sequence.end < sequence.start || sequence.flags > 1)
        Fail("MDX_POSE_SEQUENCE_UNSUPPORTED", "动作区间或循环标志尚不支持。");
    const auto duration = static_cast<std::uint64_t>(sequence.end) - sequence.start;
    const auto elapsed = sequence.flags == 0 && duration != 0
        ? request.sequenceElapsedMs % duration : std::min(request.sequenceElapsedMs, duration);
    MdxRigidSkinnedGeoset result;
    result.sequenceFrame = static_cast<std::uint32_t>(sequence.start + elapsed);
    const auto& g = model.geosets[request.geosetIndex];
    warnings.push_back(MdxRigidStaticPreviewInternal::Warning(
        model.rigidStaticPreviewSource->Geosets()[request.geosetIndex], request.geosetIndex));
    const auto zeroNormals = MdxZeroNormalCount(g.normals);
    if (zeroNormals != 0) warnings.push_back(MdxZeroNormalWarning(request.geosetIndex, zeroNormals));
    result.positions.reserve(g.positions.size()); result.normals.reserve(g.normals.size());
    const auto localIdentity = Identity();
    for (std::size_t i = 0; i < g.positions.size(); ++i) {
        result.positions.push_back(TransformPosition(localIdentity, g.positions[i]));
        result.normals.push_back(TransformNormal(localIdentity, g.normals[i]));
    }
    return result;
}

} // namespace

Result<MdxPoseSample> SampleMdxNodePose(const MdxModel& model, const MdxPoseRequest& request) {
    try {
        std::vector<Diagnostic> warnings;
        auto result = Result<MdxPoseSample>::Success(Sample(model, request, warnings));
        result.diagnostics = std::move(warnings);
        return result;
    } catch (const PoseError& error) {
        return Result<MdxPoseSample>::Failure("Mdx.Pose", error.code, error.what());
    } catch (const std::bad_alloc&) {
        return Result<MdxPoseSample>::Failure("Mdx.Pose", "MDX_POSE_ALLOCATION", "姿态求值结果分配失败。");
    } catch (const std::length_error&) {
        return Result<MdxPoseSample>::Failure("Mdx.Pose", "MDX_POSE_ALLOCATION", "姿态求值超过容器容量。");
    }
}

Result<MdxRigidSkinnedGeoset> SampleMdxRigidSkinnedGeoset(
    const MdxModel& model, const MdxRigidSkinRequest& request) {
    if (model.rigidStaticPreviewSource) return Result<MdxRigidSkinnedGeoset>::Failure("Mdx.Skin",
        "MDX_RIGID_STATIC_PREVIEW_EXPLICIT_REQUIRED", "无控制器来源必须明确申请预览近似采样，不能作为普通骨骼模型。");
    try {
        std::vector<Diagnostic> warnings;
        auto result = Result<MdxRigidSkinnedGeoset>::Success(SkinRigidGeoset(model, request, warnings));
        result.diagnostics = std::move(warnings);
        return result;
    } catch (const PoseError& error) {
        return Result<MdxRigidSkinnedGeoset>::Failure("Mdx.Skin", error.code, error.what());
    } catch (const std::bad_alloc&) {
        return Result<MdxRigidSkinnedGeoset>::Failure("Mdx.Skin", "MDX_SKIN_ALLOCATION", "顶点姿态结果分配失败。");
    } catch (const std::length_error&) {
        return Result<MdxRigidSkinnedGeoset>::Failure("Mdx.Skin", "MDX_SKIN_ALLOCATION", "顶点姿态超过容器容量。");
    }
}

Result<MdxRigidSkinnedGeoset> SampleMdxRigidStaticPreviewGeoset(
    const MdxModel& model, const MdxRigidSkinRequest& request) {
    try {
        std::vector<Diagnostic> warnings;
        auto result = Result<MdxRigidSkinnedGeoset>::Success(StaticPreviewGeoset(model, request, warnings));
        result.diagnostics = std::move(warnings);
        return result;
    } catch (const PoseError& error) {
        return Result<MdxRigidSkinnedGeoset>::Failure("Mdx.Skin", error.code, error.what());
    } catch (const std::bad_alloc&) {
        return Result<MdxRigidSkinnedGeoset>::Failure("Mdx.Skin", "MDX_SKIN_ALLOCATION", "静态预览采样分配失败。");
    } catch (const std::length_error&) {
        return Result<MdxRigidSkinnedGeoset>::Failure("Mdx.Skin", "MDX_SKIN_ALLOCATION", "静态预览采样超出容器容量。");
    }
}

} // namespace War3Map::Render
