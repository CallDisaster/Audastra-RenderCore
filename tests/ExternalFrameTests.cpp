// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3RenderCore/PoseBatch.h"
#include "War3MapCore/Render/MdxModel.h"
#include "MdxTestModel.h"
#include "RenderCoreAllocProbe.h"

#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace War3RenderCore;
using namespace War3Map::Render;
std::uint32_t checkedCalls = 0;
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template <typename T>
bool SameBits(std::span<const T> a, std::span<const T> b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size_bytes()) == 0);
}
CompiledModel Asset(bool noSequences = true) {
    auto parsed = ParseMdx(MdxTest::BuildTestMdx());
    Require(parsed.Succeeded(), "原创模型解析失败");
    if (noSequences) parsed.value->sequences.clear();
    for (auto& g : parsed.value->geosets) {
        g.matrixGroupSizes = {1, 1}; g.matrixIndices = {0, 0}; g.vertexGroups = {0, 1, 0, 1};
    }
    auto compiled = CompileMdxModel(*parsed.value, {11, 7}, {"synthetic.mdx", "original-test", "external-frame-v1"});
    Require(compiled.Succeeded(), "原创模型编译失败");
    return std::move(*compiled.value);
}
BatchLimits Limits() { return {2, 0, 8, 4, 65536}; }
PoseBatchEvaluator Prepare(const CompiledModel& asset) {
    const std::array assets{asset};
    auto result = PoseBatchEvaluator::CreateExternalFrames(assets, 101, Limits());
    Require(result.Succeeded(), "外部帧预热失败");
    return std::move(*result.value);
}
struct Fixture {
    CompiledModel asset = Asset();
    PoseBatchEvaluator evaluator = Prepare(asset);
    ExternalFrameContext context{31, 900, ExternalFrameSource::NativeEvaluated};
    std::array<ExternalFrameRequest, 2> requests{};
    std::vector<GroupMatrix> groups;
    std::vector<float> alphas;
    std::vector<Vec3> colors;
    Fixture() {
        groups.resize(2 * asset.GroupCount()); alphas.resize(2 * asset.GeosetCount()); colors.resize(alphas.size());
        for (std::uint32_t i = 0; i < 2; ++i) {
            auto& r = requests[i]; r.instance = {i + 1, 4}; r.asset = asset.Identity();
            r.source = context.source; r.sourceEpoch = context.sourceEpoch; r.frameVersion = context.frameVersion;
            r.groups.matrices = {i * asset.GroupCount(), asset.GroupCount()};
            r.groups.space = CoordinateSpace::ModelLocal; r.groups.worldTransformApplied = 0;
            r.alphas = {i * asset.GeosetCount(), asset.GeosetCount()}; r.colors = r.alphas;
            for (std::uint32_t j = 0; j < asset.GroupCount(); ++j) {
                auto& g = groups[r.groups.matrices.offset + j];
                g.values = {2, 0, 0, 0, 3, 0, 0, 0, 4, static_cast<float>(i * 100 + j), 6, 7};
            }
            for (std::uint32_t j = 0; j < asset.GeosetCount(); ++j) {
                alphas[r.alphas.offset + j] = i == 0 ? -0.25f : 1.25f;
                colors[r.colors.offset + j] = i == 0 ? Vec3{0, 0.25f, 1} : Vec3{1, 0.75f, 0};
            }
        }
        groups[0].values[1] = std::bit_cast<float>(std::uint32_t{0x80000000});
        alphas[0] = std::bit_cast<float>(std::uint32_t{0x80000000});
        colors[0][0] = std::bit_cast<float>(std::uint32_t{0x80000000});
        requests[1].groups.space = CoordinateSpace::World; requests[1].groups.worldTransformApplied = 1;
    }
    BatchOutcome Run(std::span<const GroupMatrix> g, std::span<const float> a, std::span<const Vec3> c) {
        RenderCoreAllocProbe::Begin();
        const auto result = evaluator.EvaluateExternalFrames(requests, context, g, a, c);
        const auto calls = RenderCoreAllocProbe::End();
        Require(calls == 0, "外部帧热路径发生堆分配"); ++checkedCalls;
        return result;
    }
    BatchOutcome Run() { return Run(groups, alphas, colors); }
    void Valid() {
        const auto result = Run();
        Require(result.error == Code::Ok && result.failedInstances == 0 && result.instanceCount == 2, "有效外部帧被拒绝");
        const auto receipts = evaluator.ExternalFrameReceipts();
        Require(receipts.size() == 2 && evaluator.Receipts().empty(), "外部帧回执类型错误");
        for (std::uint32_t i = 0; i < 2; ++i) {
            const auto& r = receipts[i]; const auto& input = requests[i];
            Require(r.instance == input.instance && r.asset == input.asset && r.evaluationEpoch == 101 && r.poseVersion != 0
                && r.source == input.source && r.sourceEpoch == input.sourceEpoch && r.frameVersion == input.frameVersion
                && r.contractVersion == ExternalFrameContractVersion && r.space == input.groups.space
                && r.worldTransformApplied == input.groups.worldTransformApplied, "外部帧来源/空间/版本回执错误");
            Require(evaluator.Nodes(i).empty() && evaluator.Diagnostics(i).empty(), "外部帧不应计算节点或动作诊断");
            Require(SameBits(evaluator.Groups(i), std::span<const GroupMatrix>(groups).subspan(input.groups.matrices.offset, input.groups.matrices.count))
                && SameBits(evaluator.Alphas(i), std::span<const float>(alphas).subspan(input.alphas.offset, input.alphas.count))
                && SameBits(evaluator.Colors(i), std::span<const Vec3>(colors).subspan(input.colors.offset, input.colors.count)), "外部帧没有逐位复制");
            Require(r.groups.count == asset.GroupCount() && r.alphas.count == asset.GeosetCount() && r.colors == r.alphas,
                "外部帧输出范围错误");
        }
    }
    void InstanceRejected(Code expected) {
        const auto result = Run();
        const auto receipts = evaluator.ExternalFrameReceipts();
        Require(result.error == Code::Ok && result.failedInstances == 1 && result.instanceCount == 2
            && receipts.size() == 2 && receipts[0].error == Code::Ok && receipts[1].error == expected
            && receipts[1].poseVersion == 0 && receipts[1].groups.count == 0 && receipts[1].alphas.count == 0
            && receipts[1].colors.count == 0 && evaluator.Groups(1).empty() && evaluator.Alphas(1).empty()
            && evaluator.Colors(1).empty() && evaluator.Nodes(1).empty(), "实例拒绝或失败输出边界错误");
        Require(evaluator.Diagnostics(1).size() == 1 && evaluator.Diagnostics(1)[0].code == expected, "失败诊断不明确");
    }
    void BatchRejected(BatchOutcome result, Code expected) {
        Require(result.error == expected && result.instanceCount == 0 && evaluator.ExternalFrameReceipts().empty()
            && evaluator.Receipts().empty() && evaluator.Groups(0).empty() && evaluator.Alphas(0).empty()
            && evaluator.Colors(0).empty() && evaluator.Diagnostics(0).empty(), "批次拒绝泄漏旧输出");
    }
};
void Positive() {
    Fixture f; Require(f.asset.SequenceCount() == 0, "无动作资产夹具错误"); f.Valid();
    const auto version = f.evaluator.ExternalFrameReceipts()[1].poseVersion;
    f.Valid(); Require(f.evaluator.ExternalFrameReceipts()[0].poseVersion > version, "外部输出版本未递增");
    f.context.source = ExternalFrameSource::OfflineComputed;
    for (auto& r : f.requests) r.source = f.context.source;
    f.Valid();
}
void InstanceRejections() {
    Fixture f; f.Valid(); const auto good = f.requests[1];
    const auto reject = [&](Code code) { f.InstanceRejected(code); f.requests[1] = good; f.Valid(); };
    auto& r = f.requests[1];
    r.asset.generation += 1; reject(Code::AssetStale);
    r.assetIndex = 99; reject(Code::AssetIndexInvalid);
    r.instance.value = 0; reject(Code::InstanceIdentityInvalid);
    r.reserved = 1; reject(Code::RequestInvalid);
    r.contractVersion = 0; reject(Code::ExternalFrameVersionInvalid);
    r.contractVersion = 2; reject(Code::ExternalFrameVersionInvalid);
    r.source = ExternalFrameSource::Unknown; reject(Code::ExternalFrameSourceInvalid);
    r.source = static_cast<ExternalFrameSource>(9); reject(Code::ExternalFrameSourceInvalid);
    r.source = ExternalFrameSource::OfflineComputed; reject(Code::ExternalFrameSourceInvalid);
    r.sourceEpoch = 0; reject(Code::ExternalFrameStale);
    r.sourceEpoch -= 1; reject(Code::ExternalFrameStale);
    r.frameVersion = 0; reject(Code::ExternalFrameStale);
    r.frameVersion -= 1; reject(Code::ExternalFrameStale);
    auto unspecifiedSpace = ExternalFrameRequest{}.groups;
    unspecifiedSpace.matrices = good.groups.matrices;
    r.groups = unspecifiedSpace; reject(Code::ExternalFrameLayoutInvalid);
    r.groups = unspecifiedSpace; r.groups.space = CoordinateSpace::ModelLocal; reject(Code::ExternalFrameLayoutInvalid);
    r.groups = unspecifiedSpace; r.groups.worldTransformApplied = 0; reject(Code::ExternalFrameLayoutInvalid);
    r.groups.space = CoordinateSpace::NodeLocal; reject(Code::ExternalFrameLayoutInvalid);
    r.groups.space = static_cast<CoordinateSpace>(99); reject(Code::ExternalFrameLayoutInvalid);
    r.groups.worldTransformApplied = 0; reject(Code::ExternalFrameLayoutInvalid);
    r.groups.space = CoordinateSpace::ModelLocal; reject(Code::ExternalFrameLayoutInvalid);
    r.groups.worldTransformApplied = 2; reject(Code::ExternalFrameLayoutInvalid);
    r.groups.strideBytes = 64; reject(Code::ExternalFrameLayoutInvalid);
    r.groups.layout = static_cast<MatrixLayout>(99); reject(Code::ExternalFrameLayoutInvalid);
    r.groups.matrices.count = 0; reject(Code::ExternalCountInvalid);
    r.groups.matrices.count -= 1; reject(Code::ExternalCountInvalid);
    r.groups.matrices.count += 1; reject(Code::ExternalCountInvalid);
    r.groups.matrices.offset = UINT32_MAX; reject(Code::ExternalRangeInvalid);
    r.groups.matrices.offset += 1; reject(Code::ExternalRangeInvalid);
    r.alphas.count = 0; reject(Code::ExternalChannelCountInvalid);
    r.alphas.count -= 1; reject(Code::ExternalChannelCountInvalid);
    r.alphas.count += 1; reject(Code::ExternalChannelCountInvalid);
    r.colors.count = 0; reject(Code::ExternalChannelCountInvalid);
    r.colors.count -= 1; reject(Code::ExternalChannelCountInvalid);
    r.colors.count += 1; reject(Code::ExternalChannelCountInvalid);
    r.alphas.offset = UINT32_MAX; reject(Code::ExternalChannelRangeInvalid);
    r.colors.offset = UINT32_MAX; reject(Code::ExternalChannelRangeInvalid);
    r.alphas.offset += 1; reject(Code::ExternalChannelRangeInvalid);
    r.colors.offset += 1; reject(Code::ExternalChannelRangeInvalid);
    const auto groupAt = good.groups.matrices.offset, channelAt = good.alphas.offset;
    const auto groupValue = f.groups[groupAt].values[0], alphaValue = f.alphas[channelAt], colorValue = f.colors[channelAt][0];
    for (const auto bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        f.groups[groupAt].values[0] = bad; f.InstanceRejected(Code::PoseNumberInvalid); f.groups[groupAt].values[0] = groupValue; f.Valid();
        f.alphas[channelAt] = bad; f.InstanceRejected(Code::GeoaNumberInvalid); f.alphas[channelAt] = alphaValue; f.Valid();
        f.colors[channelAt][0] = bad; f.InstanceRejected(Code::GeoaColorInvalid); f.colors[channelAt][0] = colorValue; f.Valid();
    }
    for (const auto bad : {-0.01f, 1.01f}) {
        f.colors[channelAt][0] = bad; f.InstanceRejected(Code::GeoaColorInvalid); f.colors[channelAt][0] = colorValue; f.Valid();
    }
}
void BatchRejections() {
    Fixture f; f.Valid(); const auto context = f.context;
    const auto reject = [&] { f.BatchRejected(f.Run(), Code::ExternalFrameContextInvalid); f.context = context; f.Valid(); };
    f.context.sourceEpoch = 0; reject(); f.context.frameVersion = 0; reject();
    f.context.contractVersion = 0; reject(); f.context.contractVersion = 2; reject();
    f.context.source = ExternalFrameSource::Unknown; reject(); f.context.source = static_cast<ExternalFrameSource>(99); reject();
    const auto second = f.requests[1]; f.requests[1].instance = f.requests[0].instance;
    f.BatchRejected(f.Run(), Code::DuplicateInstance); f.requests[1] = second; f.Valid();
    const auto groupAlias = f.evaluator.Groups(0); f.BatchRejected(f.Run(groupAlias, f.alphas, f.colors), Code::RequestInvalid); f.Valid();
    const auto alphaAlias = f.evaluator.Alphas(0); f.BatchRejected(f.Run(f.groups, alphaAlias, f.colors), Code::RequestInvalid); f.Valid();
    const auto colorAlias = f.evaluator.Colors(0); f.BatchRejected(f.Run(f.groups, f.alphas, colorAlias), Code::RequestInvalid); f.Valid();
    // 只做地址区间检查，不解引用跨类型视图；必须在任何结果写入前拒绝。
    const auto groupsAsFloats = std::span<const float>(reinterpret_cast<const float*>(f.evaluator.Groups(0).data()), 1);
    f.BatchRejected(f.Run(f.groups, groupsAsFloats, f.colors), Code::RequestInvalid); f.Valid();
    const auto colorsAsGroups = std::span<const GroupMatrix>(reinterpret_cast<const GroupMatrix*>(f.evaluator.Colors(0).data()), 1);
    f.BatchRejected(f.Run(colorsAsGroups, f.alphas, f.colors), Code::RequestInvalid); f.Valid();
    const std::array<ExternalFrameRequest, 3> tooMany{f.requests[0], f.requests[1], f.requests[0]};
    RenderCoreAllocProbe::Begin();
    const auto over = f.evaluator.EvaluateExternalFrames(tooMany, f.context, f.groups, f.alphas, f.colors);
    const auto calls = RenderCoreAllocProbe::End(); Require(calls == 0, "容量拒绝发生分配");
    f.BatchRejected(over, Code::CapacityExceeded); f.Valid();
    const std::array assets{f.asset}; auto smallLimits = Limits(); smallLimits.maxGroups = f.asset.GroupCount();
    auto small = PoseBatchEvaluator::CreateExternalFrames(assets, 102, smallLimits); Require(small.Succeeded(), "小容量预热失败");
    Require(small.value->EvaluateExternalFrames(f.requests, f.context, f.groups, f.alphas, f.colors).error == Code::CapacityExceeded,
        "组容量不足未拒绝");
    smallLimits = Limits(); smallLimits.maxGeosets = f.asset.GeosetCount();
    small = PoseBatchEvaluator::CreateExternalFrames(assets, 103, smallLimits); Require(small.Succeeded(), "通道小容量预热失败");
    Require(small.value->EvaluateExternalFrames(f.requests, f.context, f.groups, f.alphas, f.colors).error == Code::CapacityExceeded,
        "通道容量不足未拒绝");
    for (const auto missing : {0, 1, 2}) {
        const auto result = missing == 0 ? f.Run({}, f.alphas, f.colors)
            : missing == 1 ? f.Run(f.groups, {}, f.colors) : f.Run(f.groups, f.alphas, {});
        Require(result.error == Code::Ok && result.failedInstances == 2 && f.evaluator.Groups(0).empty()
            && f.evaluator.Groups(1).empty() && f.evaluator.Alphas(0).empty() && f.evaluator.Colors(0).empty(),
            "缺失完整输入池时补了默认值或泄漏结果");
        f.Valid();
    }
}
void ModesAndBudget() {
    Fixture f; f.Valid();
    const std::array<PoseRequest, 1> oldRequests{};
    RenderCoreAllocProbe::Begin(); const auto wrong = f.evaluator.Evaluate(oldRequests); const auto calls = RenderCoreAllocProbe::End();
    Require(calls == 0, "模式拒绝发生分配"); f.BatchRejected(wrong, Code::RequestInvalid); f.Valid();
    const auto oldAsset = Asset(false); const std::array oldAssets{oldAsset}; auto limits = Limits(); limits.maxNodes = 4;
    auto old = PoseBatchEvaluator::Create(oldAssets, 201, limits); Require(old.Succeeded(), "旧模式预热失败");
    PoseRequest request; request.asset = oldAsset.Identity(); request.instance = {1, 1}; request.sequenceIndex = 1;
    const std::array requests{request}; Require(old.value->Evaluate(requests).failedInstances == 0, "旧模式求值失败");
    RenderCoreAllocProbe::Begin(); const auto misuse = old.value->EvaluateExternalFrames(f.requests, f.context, f.groups, f.alphas, f.colors);
    const auto misuseCalls = RenderCoreAllocProbe::End();
    Require(misuseCalls == 0 && misuse.error == Code::RequestInvalid && old.value->Receipts().empty()
        && old.value->ExternalFrameReceipts().empty() && old.value->Groups(0).empty(), "旧模式误用未失效输出");
    Require(old.value->Evaluate(requests).failedInstances == 0, "旧模式误用后无法恢复");
    const auto legacyBytes = old.value->StorageBytes(); limits.maxBytes = legacyBytes;
    Require(PoseBatchEvaluator::Create(oldAssets, 201, limits).Succeeded(), "旧模式精确预算边界改变");
    limits.maxBytes -= 1; Require(!PoseBatchEvaluator::Create(oldAssets, 201, limits).Succeeded(), "旧模式预算少一字节未拒绝");
    const std::array assets{f.asset}; limits = Limits(); limits.maxBytes = f.evaluator.StorageBytes();
    Require(PoseBatchEvaluator::CreateExternalFrames(assets, 301, limits).Succeeded(), "外部模式精确预算失败");
    limits.maxBytes -= 1; Require(!PoseBatchEvaluator::CreateExternalFrames(assets, 301, limits).Succeeded(), "外部模式预算少一字节未拒绝");
    auto moved = std::move(f.evaluator);
    Require(moved.ExternalFrameReceipts().size() == 2 && f.evaluator.ExternalFrameReceipts().empty(), "移动丢失外部模式身份");
    Require(moved.EvaluateExternalFrames(f.requests, f.context, f.groups, f.alphas, f.colors).failedInstances == 0, "移动后外部帧无法接收");
}
} // namespace

int main() {
    try {
        Positive(); InstanceRejections(); BatchRejections(); ModesAndBudget();
        std::cout << "external_frame_calls=" << checkedCalls << " bit_copy=1 no_action_clock=1 aliases_rejected=1 zero_allocations=1\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
