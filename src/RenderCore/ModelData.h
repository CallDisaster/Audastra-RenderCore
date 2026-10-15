// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3RenderCore/CompiledModel.h"
#include "War3RenderCore/NodeKeySamplingPolicy.h"
#include "War3MapCore/Render/MdxModel.h"

#include <vector>

namespace War3RenderCore::Detail {

template <std::size_t N>
struct Key {
    std::int32_t frame = 0;
    std::array<float, N> value{};
    std::array<float, N> inTangent{};
    std::array<float, N> outTangent{};
    bool finiteValue = true;
    bool validInControl = true;
    bool validOutControl = true;
};
template <std::size_t N>
struct Track {
    bool present = false;
    bool global = false;
    War3Map::Render::MdxInterpolation interpolation = War3Map::Render::MdxInterpolation::None;
    Code error = Code::Ok;
    std::uint32_t period = 0;
    std::int32_t origin = 0; // 全局键已归零；诊断仍指向源文件帧号。
    std::vector<Key<N>> keys;
    // 原版逐动作区间（ForEachGameSequenceRange）；与按时间二分结果相同且无逆序时为空，沿用二分。
    std::vector<Range32> sequenceKeys;
    std::vector<std::uint8_t> sequenceDescending; // 与 sequenceKeys 同长；区间内含逆序边

};
struct CompiledNode {
    NodeLayout layout;
    Vec3 pivot{};
    Track<3> translation;
    Track<4> rotation;
    Track<3> scaling;
};
struct Sequence {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t flags = 0;
    Code error = Code::Ok;
};
template<std::size_t N> struct GeoKey {
    std::uint32_t frame = 0;
    std::array<float,N> value{}, inTangent{}, outTangent{};
};
template<std::size_t N> struct GeoChannel {
    std::array<float,N> fallback{};
    std::uint32_t mode = 0, period = 0;
    bool global = false;
    Code error = Code::Ok;
    std::vector<GeoKey<N>> keys;
    // GEOA 非全局轨迹：原版逐动作区间（与动作同长）及区间内是否含逆序边。全局轨迹与材质通道为空，沿用旧路径。
    std::vector<Range32> ranges;
    std::vector<std::uint8_t> descending;
};
using Alpha = GeoChannel<1>;
using ColorChannel = GeoChannel<3>;
struct MaterialKeySource { std::uint64_t valueOffset; std::uint32_t frame, valueBits; };
struct MaterialAlphaDuplicatePreview {
    Alpha alpha; // 全部原序键，包括每个同帧键；不借用原MdxModel。
    std::vector<MaterialAlphaPreviewWarning> warnings; // 每对相邻同帧键各一条，prepare计入预算。
};
struct CompiledMaterialChannel {
    Alpha alpha;
    MaterialLayerIssue issue;
    std::vector<MaterialKeySource> sources;
    std::unique_ptr<MaterialAlphaDuplicatePreview> identicalDuplicates;
};
struct ModelData {
    AssetIdentity identity;
    AssetSource source;
    std::shared_ptr<const War3Map::Render::MdxRigidStaticPreviewSource> rigidStaticPreviewSource;
    std::shared_ptr<const War3Map::Render::MdxNoControllerStaticSource> noControllerStaticSource;
    std::vector<CompiledNode> nodes;
    std::vector<Sequence> sequences;
    std::vector<GeosetLayout> geosets;
    std::vector<Range32> groups;
    std::vector<std::uint32_t> groupMembers; // 已转成拓扑序号
    std::vector<std::uint32_t> vertexGroups; // geoset 内的组号；不是全资产组号
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Alpha> alphas;
    std::vector<GeosetColor> colors;
    std::vector<ColorChannel> colorTracks;
    std::vector<MaterialLayerLayout> materialLayouts;
    std::vector<CompiledMaterialChannel> materialAlphas;
    bool animatedMaterialAlpha = false;
    std::uint32_t materialDuplicateWarnings = 0;
};
struct ModelAccess {
    static const ModelData* Get(const CompiledModel& model) noexcept { return model.data_.get(); }
    static CompiledModel Make(std::shared_ptr<const ModelData> data) {
        CompiledModel model;
        model.data_ = std::move(data);
        return model;
    }
};

// 运行期只写预分配的定宽诊断。错误不抛异常，避免异常对象的隐式堆分配。
struct Evaluation {
    std::span<PoseDiagnostic> diagnostics;
    std::uint32_t used = 0;
    std::uint32_t objectId = NoIndex;
    TrackKind track = TrackKind::None;
    Code error = Code::Ok;
    bool Good() const noexcept { return error == Code::Ok; }
    NodeKeySamplingPolicy nodeKeys = NodeKeySamplingPolicy::Strict;
    // 只在显式原序批次使用；共享所有实例，指向Evaluate栈上的有界计数。
    std::uint64_t* sourceOrderKeyBudget = nullptr;
    Code failureDiagnostic = Code::Ok;
    std::int64_t failureLeft = 0, failureRight = 0;
    void Fail(Code code) noexcept { if (Good()) error = code; }
    void Warn(Code code, std::int64_t left = 0, std::int64_t right = 0) noexcept {
        if (used >= diagnostics.size()) { Fail(Code::CapacityExceeded); return; }
        diagnostics[used++] = {code, Severity::Warning, objectId, track, left, right};
    }
};

template <std::size_t N>
bool Finite(const std::array<float, N>& value) noexcept;

} // namespace War3RenderCore::Detail
