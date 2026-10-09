// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "War3RenderCore/GameKeyRanges.h"
#include "War3RenderCore/PoseBatch.h"
#include "War3MapCore/Render/MdxPose.h"
#include "SourceOrderPreviewFixture.h"
#include "RenderCoreAllocProbe.h"
#include "../src/RenderCore/ModelData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace War3RenderCore;
using namespace War3Map::Render;
using Warning = std::pair<std::string, std::string>;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> bool Bits(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
MdxModel Parse(const std::vector<std::byte>& bytes) {
    auto parsed = ParseMdx(bytes); Require(parsed.Succeeded(), "fixture MDX parse failed");
    return std::move(*parsed.value);
}
CompiledModel Compile(const MdxModel& model) {
    auto result = CompileMdxModel(model, {11, 1}, {"Tests/SourceOrder.mdx", "synthetic-or-local", "fixture-v1"});
    Require(result.Succeeded(), "compile failed"); return std::move(*result.value);
}
BatchLimits Limits(std::uint32_t count = 128) { return {2, count, 128, 32, 65536}; }
auto Prepared(const CompiledModel& asset, NodeKeySamplingPolicy mode, BatchLimits limits = Limits()) {
    const std::array assets{asset};
    return PoseBatchEvaluator::Create(assets, 101, limits, GeosetFailurePolicy::Strict, {}, {}, {}, mode);
}
PoseBatchEvaluator Prepare(const CompiledModel& asset, NodeKeySamplingPolicy mode, BatchLimits limits = Limits()) {
    auto result = Prepared(asset, mode, limits); Require(result.Succeeded(), "prepare failed");
    return std::move(*result.value);
}
PoseRequest Request(std::uint32_t sequence, std::uint64_t elapsed) {
    PoseRequest r; r.instance = {1, 1}; r.asset = {11, 1};
    r.sequenceIndex = sequence; r.sequenceElapsedMs = elapsed; r.globalElapsedMs = UINT64_MAX;
    return r;
}
MdxPoseRequest ReferenceRequest(const PoseRequest& r, std::uint32_t node) {
    return {r.sequenceIndex, r.sequenceElapsedMs, r.globalElapsedMs, node, std::nullopt};
}
const NodePose& Node(std::span<const NodePose> nodes, std::uint32_t id) {
    const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const auto& n) { return n.objectId == id; });
    Require(found != nodes.end(), "node absent"); return *found;
}
struct Sample { std::vector<NodePose> nodes; std::vector<PoseDiagnostic> diagnostics; };
Sample Compare(const MdxModel& model, PoseRequest r, NodeKeySamplingPolicy mode,
    Code expected = Code::Ok, BatchLimits limits = Limits()) {
    const auto asset = Compile(model); auto evaluator = Prepare(asset, mode, limits);
    const std::array requests{r};
    RenderCoreAllocProbe::Begin(); const auto outcome = evaluator.Evaluate(requests);
    const auto allocations = RenderCoreAllocProbe::End();
    Require(allocations == 0, "Evaluate allocated");
    if (outcome.error != Code::Ok || evaluator.Receipts().size() != 1)
        throw std::runtime_error(std::string("batch receipt missing: ") + CodeName(outcome.error)
            + " nodes=" + std::to_string(asset.NodeCount()) + "/" + std::to_string(limits.maxNodes)
            + " groups=" + std::to_string(asset.GroupCount()) + "/" + std::to_string(limits.maxGroups)
            + " geosets=" + std::to_string(asset.GeosetCount()) + "/" + std::to_string(limits.maxGeosets));
    const auto& receipt = evaluator.Receipts()[0];
    Require(receipt.error == expected && receipt.quality == PoseQuality::PreviewApproximate, "receipt code/quality");
    if (expected != Code::Ok) {
        Require(receipt.poseVersion == 0 && evaluator.Nodes(0).empty() && evaluator.Groups(0).empty()
            && evaluator.Alphas(0).empty(), "failed instance exposed partial result");
        bool referenceRejected = false;
        for (std::uint32_t i = 0; i < asset.NodeCount(); ++i) {
            const auto result = SampleMdxNodePose(model, ReferenceRequest(r, asset.Node(i).objectId), mode);
            if (!result.Succeeded()) {
                Require(result.diagnostics.front().code == CodeName(expected), "reference error mismatch");
                referenceRejected = true; break;
            }
        }
        Require(referenceRejected, "compiled-only rejection"); return {};
    }
    Require(receipt.poseVersion != 0, "success version zero");
    std::set<Warning> referenceWarnings, batchWarnings;
    for (std::uint32_t i = 0; i < asset.NodeCount(); ++i) {
        const auto result = SampleMdxNodePose(model, ReferenceRequest(r, asset.Node(i).objectId), mode);
        Require(result.Succeeded(), "reference preview failed");
        const auto& a = evaluator.Nodes(0)[i]; const auto& b = result.value->chain.back();
        Require(a.objectId == b.objectId && a.parentId == b.parentId && Bits(a.pivot, b.pivot)
            && Bits(a.translation, b.translation) && Bits(a.rotation, b.rotation) && Bits(a.scaling, b.scaling)
            && Bits(a.localMatrix, b.localMatrix) && Bits(a.worldMatrix, b.worldMatrix), "compiled/reference bits differ");
        Require(receipt.sequenceFrame == result.value->sequenceFrame, "sequence frame differs");
        for (const auto& warning : result.diagnostics)
            referenceWarnings.emplace(warning.code, warning.message);
    }
    for (const auto& warning : evaluator.Diagnostics(0))
        batchWarnings.emplace(CodeName(warning.code), DescribeDiagnostic(warning));
    Require(referenceWarnings == batchWarnings, "warning provenance differs");
    return {{evaluator.Nodes(0).begin(), evaluator.Nodes(0).end()},
        {evaluator.Diagnostics(0).begin(), evaluator.Diagnostics(0).end()}};
}
bool HasWarning(const Sample& sample, std::uint32_t id, TrackKind track,
    std::int64_t left, std::int64_t right) {
    return std::any_of(sample.diagnostics.begin(), sample.diagnostics.end(), [&](const auto& d) {
        return d.code == Code::NodeSourceOrderPreview && d.objectId == id && d.track == track
            && d.leftFrame == left && d.rightFrame == right && d.severity == Severity::Warning;
    });
}
void SourceOrder() {
    const auto bytes = SourceOrderTest::Bytes(), before = bytes; auto model = Parse(bytes);
    const auto original19 = model.bones[1].scaling->keys;
    const auto asset = Compile(model);
    // 直接核对只读编译库存：保留重复帧、原序和不同值，不能靠排序/删键通过差分。
    const auto* data = Detail::ModelAccess::Get(asset);
    const auto compiled19 = std::find_if(data->nodes.begin(), data->nodes.end(),
        [](const auto& n) { return n.layout.objectId == 19; });
    Require(compiled19 != data->nodes.end() && compiled19->scaling.keys.size() == original19.size(), "compiled raw key count");
    for (std::size_t i = 0; i < original19.size(); ++i)
        Require(compiled19->scaling.keys[i].frame == original19[i].frame
            && Bits(compiled19->scaling.keys[i].value, original19[i].value), "compiled sorted/dropped source keys");
    for (const auto elapsed : {0ull, 625ull, 1251ull, 1668ull, 2085ull}) {
        Compare(model, Request(0, elapsed), NodeKeySamplingPolicy::Strict, Code::PoseKeysInvalid);
        const auto sample = Compare(model, Request(0, elapsed), NodeKeySamplingPolicy::SourceOrderPreview);
        const auto z = Node(sample.nodes, 45).translation[2];
        // 1.27a 顺序查找：动作终点 6253 时扫描越过末尾重复起点键 4168，跨度为 0 取该键（-300）。
        const std::array<float, 5> expected{-300, static_cast<float>(-300.0 + 625.0 / 1251.0 * 269.0),
            -31, -5.5f, -300};
        const auto index = elapsed == 0 ? 0u : elapsed == 625 ? 1u : elapsed == 1251 ? 2u : elapsed == 1668 ? 3u : 4u;
        Require(std::abs(z - expected[index]) < 0.00004f, "Birth follows 1.27a ordered lookup");
        Require(HasWarning(sample, 45, TrackKind::Translation, 6253, 4168), "Birth source warning lost");
        Require(Node(sample.nodes, 45).rotation == Quaternion{0, 0, 0, 1}
            && Node(sample.nodes, 45).scaling == Vec3{1, 1, 1}, "global0 single key changed");
    }
    const std::array<std::uint64_t, 9> times{0, 417, 418, 419, 834, 835, 869, 2100, 4169};
    // 1.27a 顺序查找：15003 停在第一个同帧键（2）；15419 已越过逆序键 15003，按 15003→15454 插值。
    const std::array<float, 9> expected{1, static_cast<float>(1.0 + 417.0 / 418.0), 2,
        static_cast<float>(2.0 + 2.0 / 416.0), static_cast<float>(3.0 + 2.0 * 416.0 / 451.0),
        static_cast<float>(3.0 + 2.0 * 417.0 / 451.0), 5,
        static_cast<float>(5.0 - 4.0 * 1231.0 / 3300.0), 1};
    for (std::size_t i = 0; i < times.size(); ++i) {
        Compare(model, Request(1, times[i]), NodeKeySamplingPolicy::Strict, Code::PoseKeysInvalid);
        const auto sample = Compare(model, Request(1, times[i]), NodeKeySamplingPolicy::SourceOrderPreview);
        for (const auto id : {19u, 22u}) {
            Require(std::abs(Node(sample.nodes, id).scaling[0] - expected[i]) < 0.000001f, "Stand independent numeric expectation");
            Require(HasWarning(sample, id, TrackKind::Scale, 15419, 15003), "Stand warning owner/frame lost");
        }
    }
    Compare(model, Request(2, 100), NodeKeySamplingPolicy::Strict);
    const auto safe = Compare(model, Request(2, 100), NodeKeySamplingPolicy::SourceOrderPreview);
    Require(safe.diagnostics.empty(), "unrelated action warned");
    Require(bytes == before && model.bones[1].scaling->keys.size() == original19.size(), "source mutated");
    for (std::size_t i = 0; i < original19.size(); ++i)
        Require(model.bones[1].scaling->keys[i].frame == original19[i].frame
            && Bits(model.bones[1].scaling->keys[i].value, original19[i].value), "raw source reordered");
}
// 原版逐动作区间：动作按文件顺序共享只进游标，重叠或倒序排列的动作拿不到被前者收走的键。
void GameRanges() {
    auto m = Parse(SourceOrderTest::Bytes()); m.bones[1].scaling.reset(); m.bones[2].scaling.reset();
    m.helpers[0].translation = MdxNodeTrack<3>{MdxInterpolation::Linear, UINT32_MAX,
        {{100, {10, 0, 0}, {}, {}}, {200, {20, 0, 0}, {}, {}}, {300, {30, 0, 0}, {}, {}}, {400, {40, 0, 0}, {}, {}}}};
    m.sequences = {{"A", 100, 300, 1}, {"B", 200, 400, 1}, {"C", 600, 700, 1}};
    for (const auto mode : {NodeKeySamplingPolicy::Strict, NodeKeySamplingPolicy::SourceOrderPreview}) {
        const auto a = Compare(m, Request(0, 150), mode);
        Require(Node(a.nodes, 45).translation[0] == 25 && a.diagnostics.empty(), "first action interpolates its keys");
        // B 与 A 重叠：200、300 已归 A，B 只剩 400，任意时刻取该键。
        const auto b = Compare(m, Request(1, 50), mode);
        Require(Node(b.nodes, 45).translation[0] == 40, "overlapping action lost keys taken by the previous one");
    }
    // 已确认不降序的区间：二分快路径与原版线性扫描逐时刻一致（含同帧、首键前与末键后）。
    struct FrameKey { std::int32_t frame; };
    const std::vector<FrameKey> sorted{{0}, {5}, {100}, {100}, {100}, {250}, {250}, {300}, {999}};
    for (std::int64_t time = -3; time <= 1003; ++time)
        for (const std::size_t offset : {std::size_t{0}, std::size_t{2}}) {
            const auto count = sorted.size() - offset;
            const auto linear = War3RenderCore::SelectGameKeyWindow(sorted, offset, count, 0, 1000, time);
            const auto fast = War3RenderCore::SelectGameKeyWindow(sorted, offset, count, 0, 1000, time, true);
            Require(linear.kind == fast.kind && linear.left == fast.left && linear.right == fast.right
                && linear.offset == fast.offset && linear.span == fast.span && linear.seam == fast.seam,
                "ascending fast path differs from ordered scan");
        }
    m.sequences = {{"Late", 300, 400, 1}, {"Early", 100, 200, 1}};
    for (const auto mode : {NodeKeySamplingPolicy::Strict, NodeKeySamplingPolicy::SourceOrderPreview}) {
        const auto late = Compare(m, Request(0, 50), mode);
        Require(Node(late.nodes, 45).translation[0] == 35, "late action ranges after skipped keys");
        // 游标已越过 100、200：排在后面的早期动作区间为空，取默认值。
        const auto early = Compare(m, Request(1, 50), mode);
        Require(Node(early.nodes, 45).translation == Vec3{}, "reversed action order must leave an empty range");
    }
}
MdxModel SingleTrack() {
    auto m = Parse(SourceOrderTest::Bytes()); m.bones[1].scaling.reset(); m.bones[2].scaling.reset();
    m.helpers[0].translation = MdxNodeTrack<3>{MdxInterpolation::Linear, UINT32_MAX,
        {{100, {10, 0, 0}, {}, {}}, {500, {50, 0, 0}, {}, {}}, {200, {20, 0, 0}, {}, {}}, {700, {70, 0, 0}, {}, {}}}};
    m.sequences[0] = {"Seam", 0, 1000, 1}; return m;
}
void Boundaries() {
    auto m = SingleTrack();
    const auto after = Compare(m, Request(0, 800), NodeKeySamplingPolicy::SourceOrderPreview);
    Require(Node(after.nodes, 45).translation[0] == 55, "no-right-neighbor seam changed");
    const auto before = Compare(m, Request(0, 50), NodeKeySamplingPolicy::SourceOrderPreview);
    Require(Node(before.nodes, 45).translation[0] == 77.5f, "before-first seam changed");
    Compare(m, Request(0, 800), NodeKeySamplingPolicy::Strict, Code::PoseKeysInvalid);
    // 零长动作 200：游标跳过 100 后遇到 500 超过终点即停，区间为空，两种模式都取默认值。
    m.sequences[0].start = m.sequences[0].end = 200;
    const auto zeroStrict = Compare(m, Request(0, UINT64_MAX), NodeKeySamplingPolicy::Strict);
    const auto zero = Compare(m, Request(0, UINT64_MAX), NodeKeySamplingPolicy::SourceOrderPreview);
    Require(Node(zeroStrict.nodes, 45).translation == Vec3{} && Node(zero.nodes, 45).translation == Vec3{}
        && zero.diagnostics.empty(), "zero-length empty game range");
    m.sequences[0] = {"Empty", 800, 1000, 1};
    const auto empty = Compare(m, Request(0, 50), NodeKeySamplingPolicy::Strict);
    Require(Node(empty.nodes, 45).translation == Vec3{}, "empty sequence default changed");
    Compare(m, Request(0, 50), NodeKeySamplingPolicy::SourceOrderPreview);
    // 逆序边跨过动作、但原版区间为空（首键已超过终点）：两种模式都返回默认TRS，不再警告。
    m.helpers[0].translation->keys = {{500, {50, 0, 0}, {}, {}}, {100, {10, 0, 0}, {}, {}}};
    m.sequences[0] = {"CoveredGap", 200, 300, 1};
    const auto gapStrict = Compare(m, Request(0, 1), NodeKeySamplingPolicy::Strict);
    const auto gap = Compare(m, Request(0, 1), NodeKeySamplingPolicy::SourceOrderPreview);
    Require(Node(gapStrict.nodes, 45).translation == Vec3{} && Node(gap.nodes, 45).translation == Vec3{}
        && gap.diagnostics.empty(), "empty game range must use defaults");
    for (const auto number : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        m = SingleTrack(); m.helpers[0].translation->keys[1].value[0] = number;
        Compare(m, Request(0, 300), NodeKeySamplingPolicy::SourceOrderPreview, Code::PoseNumberInvalid);
    }
    m = SingleTrack(); m.helpers[0].rotation = MdxNodeTrack<4>{MdxInterpolation::Linear, UINT32_MAX,
        {{100, {0, 0, 0, 1}, {}, {}}, {500, {}, {}, {}}, {200, {0, 0, 0, 1}, {}, {}}}};
    Compare(m, Request(0, 300), NodeKeySamplingPolicy::SourceOrderPreview, Code::PoseRotationInvalid);
    m = SingleTrack(); m.helpers[0].translation->interpolation = static_cast<MdxInterpolation>(99);
    Compare(m, Request(0, 300), NodeKeySamplingPolicy::SourceOrderPreview, Code::PoseInterpolationUnsupported);
    m = SingleTrack(); m.helpers[0].translation->globalSequence = 99;
    Compare(m, Request(0, 300), NodeKeySamplingPolicy::SourceOrderPreview, Code::PoseGlobalSequenceInvalid);
    m = SingleTrack(); m.globalSequences = {1000}; m.helpers[0].translation->globalSequence = 0;
    Compare(m, Request(0, 300), NodeKeySamplingPolicy::SourceOrderPreview, Code::PoseKeysInvalid);
    m = SingleTrack(); m.sequences[0].flags = 7;
    Compare(m, Request(0, 300), NodeKeySamplingPolicy::SourceOrderPreview, Code::PoseSequenceUnsupported);
    m = SingleTrack(); Compare(m, Request(99, 300), NodeKeySamplingPolicy::SourceOrderPreview, Code::PoseSequenceInvalid);
    m = SingleTrack(); m.sequences[0].end = UINT32_MAX;
    Compare(m, Request(0, 2147483648ull), NodeKeySamplingPolicy::SourceOrderPreview, Code::PoseBoundaryUnsupported);
    // 单调输入严格旧入口和新显式入口在零动作与全局0常量上逐位一致。
    m = SingleTrack(); m.helpers[0].translation->keys = {{200, {7, 8, 9}, {}, {}}};
    m.sequences[0].start = m.sequences[0].end = 200;
    const auto strict = Compare(m, Request(0, 10), NodeKeySamplingPolicy::Strict);
    const auto preview = Compare(m, Request(0, 10), NodeKeySamplingPolicy::SourceOrderPreview);
    Require(Bits(Node(strict.nodes, 45).translation, Node(preview.nodes, 45).translation)
        && preview.diagnostics.empty(), "monotonic zero-length old behavior changed");
}
void Budgets() {
    auto m = Parse(SourceOrderTest::Bytes()); const auto asset = Compile(m);
    const std::array assets{asset}; auto limits = Limits(4);
    auto old = PoseBatchEvaluator::Create(assets, 101, limits);
    auto strict = Prepared(asset, NodeKeySamplingPolicy::Strict, limits);
    auto preview = Prepared(asset, NodeKeySamplingPolicy::SourceOrderPreview, limits);
    Require(old.Succeeded() && strict.Succeeded() && preview.Succeeded(), "budget setup");
    Require(old.value->StorageBytes() == strict.value->StorageBytes(), "old strict storage changed");
    const std::array strictRequests{Request(1, 419)};
    Require(old.value->Evaluate(strictRequests).failedInstances == 1
        && old.value->Receipts()[0].error == Code::PoseKeysInvalid, "old Create no longer strict");
    const auto oldReference = SampleMdxNodePose(m, ReferenceRequest(strictRequests[0], 19));
    Require(!oldReference.Succeeded() && oldReference.diagnostics.front().code == CodeName(Code::PoseKeysInvalid),
        "old reference no longer strict");
    const auto oldBytes = old.value->StorageBytes(), previewBytes = preview.value->StorageBytes();
    Require(previewBytes == oldBytes + 3ull * limits.maxNodes * sizeof(PoseDiagnostic), "preview prewarm accounting");
    limits.maxBytes = oldBytes;
    Require(PoseBatchEvaluator::Create(assets, 101, limits).Succeeded()
        && !Prepared(asset, NodeKeySamplingPolicy::SourceOrderPreview, limits).Succeeded(), "preview bypassed strict budget");
    limits.maxBytes = previewBytes;
    Require(Prepared(asset, NodeKeySamplingPolicy::SourceOrderPreview, limits).Succeeded(), "exact preview budget rejected");
    limits.maxBytes = previewBytes - 1;
    Require(!Prepared(asset, NodeKeySamplingPolicy::SourceOrderPreview, limits).Succeeded(), "one-byte short accepted");
    Require(!Prepared(asset, static_cast<NodeKeySamplingPolicy>(99)).Succeeded(), "unknown policy accepted");
    Require(!SampleMdxNodePose(m, ReferenceRequest(strictRequests[0], 19),
        static_cast<NodeKeySamplingPolicy>(99)).Succeeded(), "unknown reference policy accepted");
    auto moved = std::move(*preview.value); PoseBatchEvaluator assigned; assigned = std::move(moved);
    const std::array requests{Request(1, 419)};
    RenderCoreAllocProbe::Begin(); Code error = Code::Ok;
    for (int i = 0; i < 64; ++i) {
        const auto outcome = assigned.Evaluate(requests);
        if (outcome.error != Code::Ok || assigned.Receipts()[0].error != Code::Ok) error = Code::RequestInvalid;
    }
    const auto allocations = RenderCoreAllocProbe::End();
    Require(error == Code::Ok && allocations == 0, "move lost policy or hot path allocated");
    Require(assigned.Diagnostics(0).size() == 2, "prewarmed warning count incorrect");
    // 世界消费的compact输出仍采完整骨架；诊断分片按实例隔离，批量容量不是持久实例数。
    limits = Limits(8); limits.outputMode = PoseOutputMode::GroupsAndAlphas;
    auto compact = Prepared(asset, NodeKeySamplingPolicy::SourceOrderPreview, limits);
    Require(compact.Succeeded(), "compact preview prewarm");
    auto second = requests[0]; second.instance.value = 2; const std::array two{requests[0], second};
    RenderCoreAllocProbe::Begin(); const auto compactOutcome = compact.value->Evaluate(two);
    const auto compactAllocations = RenderCoreAllocProbe::End();
    Require(compactOutcome.failedInstances == 0 && compactAllocations == 0
        && compact.value->Nodes(0).empty() && compact.value->Nodes(1).empty()
        && compact.value->Diagnostics(0).size() == 2 && compact.value->Diagnostics(1).size() == 2,
        "compact preview or per-instance diagnostics lost");
    limits = Limits(3); auto tiny = Prepared(asset, NodeKeySamplingPolicy::SourceOrderPreview, limits);
    Require(tiny.Succeeded(), "small capacity prewarm"); const auto outcome = tiny.value->Evaluate(requests);
    Require(outcome.error == Code::CapacityExceeded && tiny.value->Receipts().empty(), "node capacity relaxed");
    // 3 TRS原序警告 + 四元数近似/控制回退 + 相机警告同节点，完整容量不得截断。
    m = SingleTrack(); auto& node = m.helpers[0]; node.flags = 8;
    node.scaling = *node.translation;
    node.rotation = MdxNodeTrack<4>{MdxInterpolation::Hermite, UINT32_MAX,
        {{100, {0, 0, 0, 1}, {}, {}}, {500, {0, 0, 1, 0}, {}, {}}, {200, {0, 0, 0, 1}, {}, {}}}};
    const auto sampled = Compare(m, Request(0, 300), NodeKeySamplingPolicy::SourceOrderPreview);
    Require(sampled.diagnostics.size() == 6, "worst node diagnostics truncated");

    // 相同资产的16次原序扫描恰好1048576，下一实例须拒绝，不得每实例重置预算。
    m = SingleTrack(); auto& keys = m.helpers[0].translation->keys; keys.clear();
    for (std::int32_t i = 0; i < 65535; ++i) keys.push_back({i, {1, 0, 0}, {}, {}});
    keys.push_back({100, {1, 0, 0}, {}, {}});
    m.sequences[0].end = 70000; // 原版区间收下全部 65536 键（含末尾逆序键）
    const auto large = Compile(m); limits = Limits(68); limits.maxInstances = 17;
    auto bounded = Prepared(large, NodeKeySamplingPolicy::SourceOrderPreview, limits);
    Require(bounded.Succeeded(), "key work budget prewarm");
    std::array<PoseRequest, 17> repeats{};
    for (std::size_t i = 0; i < repeats.size(); ++i) { repeats[i] = Request(0, 300); repeats[i].instance.value = i + 1; }
    const auto exact = std::span(repeats).first(16);
    RenderCoreAllocProbe::Begin(); const auto atLimit = bounded.value->Evaluate(exact);
    const auto atLimitAllocations = RenderCoreAllocProbe::End();
    Require(atLimit.error == Code::Ok && atLimit.failedInstances == 0 && atLimitAllocations == 0,
        "exact key work cap rejected/allocated");
    const auto exceeded = bounded.value->Evaluate(repeats);
    Require(exceeded.error == Code::Ok && exceeded.failedInstances == 1
        && bounded.value->Receipts()[16].error == Code::CapacityExceeded
        && bounded.value->Receipts()[16].poseVersion == 0 && bounded.value->Nodes(16).empty(),
        "repeated asset bypassed shared key work cap");
    const auto refusal = bounded.value->Diagnostics(16);
    Require(refusal.size() == 1 && refusal[0].code == Code::NodeSourceOrderKeyLimit
        && refusal[0].objectId == 45 && refusal[0].track == TrackKind::Translation
        && refusal[0].leftFrame == 65536 && refusal[0].rightFrame == 0
        && DescribeDiagnostic(refusal[0]).find("1048576") != std::string::npos,
        "key work diagnostic not explicit");
    const auto reset = bounded.value->Evaluate(exact);
    Require(reset.error == Code::Ok && reset.failedInstances == 0, "key work cap leaked into next batch");
    auto strictLarge = Prepared(large, NodeKeySamplingPolicy::Strict, limits);
    Require(strictLarge.Succeeded(), "strict repeated asset prepare");
    const auto strictOutcome = strictLarge.value->Evaluate(repeats);
    Require(strictOutcome.error == Code::Ok && strictOutcome.failedInstances == 17,
        "strict work cap changed behavior");
    for (const auto& r : strictLarge.value->Receipts()) Require(r.error == Code::PoseKeysInvalid, "strict budget affected code");
}
void RealModel() {
    // 只能由SHA绑定wrapper调用；单独此组只提供CPU数值测试，不构成来源签收。
#if defined(_WIN32) && defined(_MSC_VER)
    std::size_t required = 0;
    Require(getenv_s(&required, nullptr, 0, "FORGE_SOURCE_ORDER_MODEL") == 0 && required > 1,
        "real model environment missing");
    std::string path(required, '\0'); std::size_t copied = 0;
    Require(getenv_s(&copied, path.data(), path.size(), "FORGE_SOURCE_ORDER_MODEL") == 0 && copied == required,
        "real model environment changed during read");
    path.resize(required - 1);
#else
    const auto* path = std::getenv("FORGE_SOURCE_ORDER_MODEL"); Require(path && *path, "real model environment missing");
#endif
    std::ifstream stream(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    Require(stream.good(), "real model open failed"); const auto length = stream.tellg();
    Require(length > 0 && length <= 32 * 1024 * 1024, "real model size invalid");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length)); stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(stream.good(), "real model read failed"); char extra = 0;
    Require(!stream.read(&extra, 1) && stream.eof(), "real model length changed");
    const auto before = bytes; auto m = Parse(bytes);
    Require(MdxNodeCount(m) == 47 && m.sequences.size() == 4 && m.geosets.size() == 4, "real fixture identity shape changed");
    const auto realAsset = Compile(m);
    Require(realAsset.NodeCount() == 47 && realAsset.GroupCount() == 149 && realAsset.GeosetCount() == 4,
        "real fixture compiled capacity changed");
    // 原始GEOS的MTGC数31+9+94+15=149；按真实结构精确预热，保持原65536 B预算。
    const BatchLimits realLimits{1, realAsset.NodeCount(), realAsset.GroupCount(), realAsset.GeosetCount(), 65536};
    Require(m.bones.size() > 22 && m.bones[19].objectId == 19 && m.bones[22].objectId == 22
        && m.bones[19].scaling.has_value() && m.bones[22].scaling.has_value(), "real node index changed");
    const auto& keys = m.bones[19].scaling->keys;
    Require(keys.size() == 14 && keys[1].frame == 15003 && keys[2].frame == 15419 && keys[3].frame == 15003
        && keys[1].value != keys[3].value, "real reverse/duplicate evidence changed");
    const std::array<std::pair<std::uint32_t, std::uint64_t>, 14> cases{{{0, 0}, {0, 2085},
        {1, 0}, {1, 100}, {1, 417}, {1, 418}, {1, 834}, {1, 2100}, {1, 4168}, {1, 4169},
        {2, 0}, {2, 2083}, {3, 0}, {3, 4409}}};
    for (const auto& [sequence, elapsed] : cases) {
        const auto r = Request(sequence, elapsed);
        Compare(m, r, NodeKeySamplingPolicy::Strict, sequence < 2 ? Code::PoseKeysInvalid : Code::Ok, realLimits);
        const auto sample = Compare(m, r, NodeKeySamplingPolicy::SourceOrderPreview, Code::Ok, realLimits);
        if (sequence == 0) Require(HasWarning(sample, 45, TrackKind::Translation, 6253, 4168), "real Birth owner");
        if (sequence == 1) for (const auto id : {19u, 22u})
            Require(HasWarning(sample, id, TrackKind::Scale, 15419, 15003), "real Stand owner");
    }
    for (const auto elapsed : {625ull, 1251ull, 1668ull}) {
        const auto sample = Compare(m, Request(0, elapsed), NodeKeySamplingPolicy::SourceOrderPreview, Code::Ok, realLimits);
        Require(Node(sample.nodes, 45).translation[2] > -300, "real Birth midsection buried");
    }
    Require(bytes == before && keys[2].frame == 15419 && keys[3].frame == 15003, "real bytes/keys changed");
    std::cout << "REAL_MODEL_CPU: 14 approved-source cases + 3 Birth midsections; PreviewApproximate only\n";
}
}
int main(int argc, char** argv) {
    try {
        Require(argc == 2, "group required"); const std::string group = argv[1];
        if (group == "source-order") SourceOrder();
        else if (group == "boundaries") Boundaries();
        else if (group == "budgets") Budgets();
        else if (group == "game-ranges") GameRanges();
        else if (group == "real-model") RealModel();
        else throw std::runtime_error("unknown group");
        std::cout << "PASS " << group << '\n'; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
