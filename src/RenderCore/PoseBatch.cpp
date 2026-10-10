// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3RenderCore/PoseBatch.h"
#include "PoseTracks.h"
#include "GeosetChannels.h"
#include "MaterialChannels.h"
#include "MaterialTextures.h"
#include "SourceOrderNodeTrack.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>

namespace War3RenderCore {
namespace Detail {
struct BatchData {
    std::vector<CompiledModel> assets;
    std::vector<NodePose> nodes;
    std::vector<GroupMatrix> groups;
    std::vector<float> alphas;
    std::vector<Vec3> colors;
    std::vector<PoseDiagnostic> diagnostics;
    std::vector<PoseReceipt> receipts;
    std::vector<InstanceIdentity> identities;
    BatchLimits limits;
    std::uint64_t storageBytes = 0;
    std::uint64_t epoch = 0;
    std::uint64_t lastVersion = 0;
    std::uint32_t active = 0;
};
struct ExternalBatchData {
    std::vector<ExternalFrameReceipt> receipts;
};
struct GeosetPreviewData {
    std::vector<GeosetIssue> issues;
    std::vector<Range32> ranges;
    std::uint64_t storageBytes = 0;
};
struct MaterialBatchData {
    std::vector<float> alphas;
    std::vector<Range32> ranges;
    std::vector<MaterialLayerIssue> issues;
    std::uint64_t storageBytes = 0;
};
struct TextureBatchData {
    std::vector<CompiledMaterialTextures> assets;
    std::vector<std::uint32_t> textureIds;
    std::vector<UvTransform> uvTransforms; // 与 textureIds 同长同序
    std::vector<Range32> ranges;
    std::vector<MaterialLayerIssue> issues;
    std::uint64_t storageBytes = 0;
};
// 独立所有者，旧MaterialBatchData/默认Create的sizeof与容量合同保持不变。
struct MaterialAlphaPreviewBatchData {
    std::vector<MaterialAlphaPreviewWarning> warnings;
    std::vector<Range32> ranges;
    std::uint64_t storageBytes = 0;
};
} // namespace Detail
namespace {
using namespace Detail;
bool LessIdentity(const auto& a, const auto& b) noexcept {
    return std::tie(a.value, a.generation) < std::tie(b.value, b.generation);
}
bool Overlaps(std::span<const GroupMatrix> input, std::span<const GroupMatrix> storage) noexcept {
    if (input.empty() || storage.empty()) return false;
    const std::less<const void*> less;
    return less(input.data(), storage.data() + storage.size()) && less(storage.data(), input.data() + input.size());
}
bool SupportedSource(ExternalFrameSource source) noexcept {
    return source == ExternalFrameSource::NativeEvaluated || source == ExternalFrameSource::OfflineComputed;
}
template <typename A, typename B>
bool ByteOverlaps(std::span<A> input, std::span<B> storage) noexcept {
    const auto a = std::as_bytes(input), b = std::as_bytes(storage);
    if (a.empty() || b.empty()) return false;
    const std::less<const void*> less;
    return less(a.data(), b.data() + b.size()) && less(b.data(), a.data() + a.size());
}
template <typename T>
bool AliasesOutput(std::span<const T> input, const BatchData& data) noexcept {
    return ByteOverlaps(input, std::span(data.groups)) || ByteOverlaps(input, std::span(data.alphas))
        || ByteOverlaps(input, std::span(data.colors));
}
bool InRange(Range32 range, std::size_t size) noexcept {
    return range.offset <= size && range.count <= size - range.offset;
}
void CopyExternalFrame(const CompiledModel& asset, const ExternalFrameRequest& request,
    const ExternalFrameContext& context, std::span<const GroupMatrix> inputGroups,
    std::span<const float> inputAlphas, std::span<const Vec3> inputColors,
    std::span<GroupMatrix> groups, std::span<float> alphas, std::span<Vec3> colors, Evaluation& e) noexcept {
    if (request.asset != asset.Identity()) { e.Fail(Code::AssetStale); return; }
    if (request.instance.value == 0) { e.Fail(Code::InstanceIdentityInvalid); return; }
    if (request.reserved != 0) { e.Fail(Code::RequestInvalid); return; }
    if (request.contractVersion != ExternalFrameContractVersion) { e.Fail(Code::ExternalFrameVersionInvalid); return; }
    if (!SupportedSource(request.source) || request.source != context.source) { e.Fail(Code::ExternalFrameSourceInvalid); return; }
    if (request.sourceEpoch == 0 || request.frameVersion == 0 || request.sourceEpoch != context.sourceEpoch
        || request.frameVersion != context.frameVersion) { e.Fail(Code::ExternalFrameStale); return; }
    const auto& view = request.groups;
    const bool local = view.space == CoordinateSpace::ModelLocal && view.worldTransformApplied == 0;
    const bool world = view.space == CoordinateSpace::World && view.worldTransformApplied == 1;
    if (view.layout != MatrixLayout::ColumnMajor3x4 || view.strideBytes != sizeof(GroupMatrix) || (!local && !world)) {
        e.Fail(Code::ExternalFrameLayoutInvalid); return;
    }
    if (view.matrices.count != asset.GroupCount()) { e.Fail(Code::ExternalCountInvalid); return; }
    if (!InRange(view.matrices, inputGroups.size())) { e.Fail(Code::ExternalRangeInvalid); return; }
    if (request.alphas.count != asset.GeosetCount() || request.colors.count != asset.GeosetCount()) {
        e.Fail(Code::ExternalChannelCountInvalid); return;
    }
    if (!InRange(request.alphas, inputAlphas.size()) || !InRange(request.colors, inputColors.size())) {
        e.Fail(Code::ExternalChannelRangeInvalid); return;
    }
    const auto groupValues = inputGroups.subspan(view.matrices.offset, view.matrices.count);
    const auto alphaValues = inputAlphas.subspan(request.alphas.offset, request.alphas.count);
    const auto colorValues = inputColors.subspan(request.colors.offset, request.colors.count);
    for (const auto& value : groupValues) if (!CheckFinite(value.values, e)) return;
    for (const auto value : alphaValues) if (!std::isfinite(value)) { e.Fail(Code::GeoaNumberInvalid); return; }
    for (const auto& value : colorValues) for (const auto component : value)
        if (!std::isfinite(component) || component < 0 || component > 1) { e.Fail(Code::GeoaColorInvalid); return; }
    std::copy(groupValues.begin(), groupValues.end(), groups.begin());
    std::copy(alphaValues.begin(), alphaValues.end(), alphas.begin());
    std::copy(colorValues.begin(), colorValues.end(), colors.begin());
}
Code ExternalCheck(const ModelData& model, const PoseRequest& request, std::span<const GroupMatrix> external) noexcept {
    const auto& view = request.external;
    if (view.layout != MatrixLayout::ColumnMajor3x4 || view.strideBytes != sizeof(GroupMatrix)
        || view.space != CoordinateSpace::ModelLocal || view.worldTransformApplied != 0) return Code::ExternalLayoutInvalid;
    if (view.matrices.count != model.groups.size()) return Code::ExternalCountInvalid;
    if (view.matrices.offset > external.size() || view.matrices.count > external.size() - view.matrices.offset)
        return Code::ExternalRangeInvalid;
    return Code::Ok;
}
void EvaluateOne(const CompiledModel& asset, const PoseRequest& request, std::span<const GroupMatrix> external,
    std::span<NodePose> nodes, std::span<GroupMatrix> groups, std::span<float> alphas, std::span<Vec3> colors,
    PoseReceipt& receipt, Evaluation& e, GeosetPreviewData* preview,
    std::span<GeosetIssue> issues, std::uint32_t& issueCount) noexcept {
    const auto& model = *ModelAccess::Get(asset);
    if (request.asset != model.identity) { e.Fail(Code::AssetStale); return; }
    if (request.instance.value == 0) { e.Fail(Code::InstanceIdentityInvalid); return; }
    if (request.overrides.count != 0) { e.Fail(Code::OverrideUnsupported); return; }
    if (request.hasCamera > 1 || (request.source != PoseSource::Computed && request.source != PoseSource::ExternalGroups)) {
        e.Fail(Code::RequestInvalid); return;
    }
    if (request.sequenceIndex >= model.sequences.size()) { e.Fail(Code::PoseSequenceInvalid); return; }
    if (request.hasCamera != 0 && !CheckCamera(request.camera, e)) return;
    const auto& sequence = model.sequences[request.sequenceIndex];
    if (sequence.error != Code::Ok) { e.Fail(sequence.error); return; }
    receipt.sequenceFrame = SequenceFrame(sequence, request);
    if (request.source == PoseSource::Computed) {
        if (!EvaluateComputed(model, request, receipt.sequenceFrame, nodes, groups, e)) return;
    } else {
        const auto error = ExternalCheck(model, request, external);
        if (error != Code::Ok) { e.Fail(error); return; }
        const auto values = external.subspan(request.external.matrices.offset, request.external.matrices.count);
        // 外部输入每次都可能变化，数值检查不能像资产静态检查那样移到加载时。
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!CheckFinite(values[i].values, e)) return;
            groups[i] = values[i];
        }
    }
    e.objectId = NoIndex; e.track = TrackKind::None;
    if (preview) (void)EvaluateGeosets(model, request.sequenceIndex, receipt.sequenceFrame, request.globalElapsedMs,
        alphas, colors, e, issues, issueCount);
    else (void)EvaluateGeosets(model, request.sequenceIndex, receipt.sequenceFrame, request.globalElapsedMs, alphas, colors, e);
}
} // namespace

PoseBatchEvaluator::PoseBatchEvaluator() = default;
PoseBatchEvaluator::~PoseBatchEvaluator() = default;
PoseBatchEvaluator::PoseBatchEvaluator(PoseBatchEvaluator&&) noexcept = default;
PoseBatchEvaluator& PoseBatchEvaluator::operator=(PoseBatchEvaluator&&) noexcept = default;

War3Map::Result<PoseBatchEvaluator> PoseBatchEvaluator::Create(
    std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits) {
    return CreateImpl(assets, evaluationEpoch, limits, false);
}
War3Map::Result<PoseBatchEvaluator> PoseBatchEvaluator::CreateExternalFrames(
    std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits) {
    return CreateImpl(assets, evaluationEpoch, limits, true);
}
War3Map::Result<PoseBatchEvaluator> PoseBatchEvaluator::CreateImpl(
    std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits, bool externalFrames) {
    const auto failure = [](Code code) {
        return War3Map::Result<PoseBatchEvaluator>::Failure("RenderCore.Prepare", CodeName(code), CodeMessage(code));
    };
    if (evaluationEpoch == 0 || assets.empty() || assets.size() > 4096) return failure(Code::RequestInvalid);
    if (limits.maxInstances == 0 || limits.maxInstances > 65536 || limits.maxBytes == 0
        || limits.maxBytes > 64ull * 1024 * 1024) return failure(Code::CapacityExceeded);
    if (limits.reserved != 0 || (limits.outputMode != PoseOutputMode::FullNodes
        && limits.outputMode != PoseOutputMode::GroupsAndAlphas)) return failure(Code::RequestInvalid);
    std::uint32_t nodeCapacity = limits.maxNodes;
    if (limits.outputMode == PoseOutputMode::GroupsAndAlphas) {
        nodeCapacity = 0;
        for (const auto& asset : assets) nodeCapacity = std::max(nodeCapacity, asset.NodeCount());
        nodeCapacity = std::min(nodeCapacity, limits.maxNodes);
    }
    if (externalFrames) nodeCapacity = 0;
    const std::uint64_t diagnostics = (externalFrames ? 0 : 4ull * limits.maxNodes) + limits.maxInstances;
    const std::uint64_t bytes = sizeof(BatchData) + assets.size() * sizeof(CompiledModel)
        + static_cast<std::uint64_t>(nodeCapacity) * sizeof(NodePose)
        + static_cast<std::uint64_t>(limits.maxGroups) * sizeof(GroupMatrix)
        + static_cast<std::uint64_t>(limits.maxGeosets) * (sizeof(float) + sizeof(Vec3))
        + diagnostics * sizeof(PoseDiagnostic)
        + static_cast<std::uint64_t>(limits.maxInstances) * (sizeof(PoseReceipt) + sizeof(InstanceIdentity))
        + (externalFrames ? sizeof(ExternalBatchData)
            + static_cast<std::uint64_t>(limits.maxInstances) * sizeof(ExternalFrameReceipt) : 0);
    if (diagnostics > UINT32_MAX || bytes > limits.maxBytes) return failure(Code::CapacityExceeded);
    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (!assets[i].Valid()) return failure(Code::AssetIndexInvalid);
        for (std::size_t j = 0; j < i; ++j)
            if (assets[i].Identity() == assets[j].Identity() && !assets[i].SharesStorageWith(assets[j]))
                return failure(Code::AssetIdentityConflict);
    }
    try {
        PoseBatchEvaluator result;
        auto data = std::make_unique<BatchData>();
        data->assets.assign(assets.begin(), assets.end());
        data->nodes.resize(nodeCapacity); data->groups.resize(limits.maxGroups); data->alphas.resize(limits.maxGeosets); data->colors.resize(limits.maxGeosets);
        data->diagnostics.resize(static_cast<std::size_t>(diagnostics));
        data->receipts.resize(limits.maxInstances); data->identities.resize(limits.maxInstances);
        data->epoch = evaluationEpoch; data->limits = limits; data->storageBytes = bytes;
        result.data_ = std::move(data);
        if (externalFrames) {
            result.externalData_ = std::make_unique<ExternalBatchData>();
            result.externalData_->receipts.resize(limits.maxInstances);
        }
        return War3Map::Result<PoseBatchEvaluator>::Success(std::move(result));
    } catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}

War3Map::Result<PoseBatchEvaluator> PoseBatchEvaluator::Create(
    std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
    GeosetFailurePolicy policy) {
    const auto failure = [](Code code) {
        return War3Map::Result<PoseBatchEvaluator>::Failure("RenderCore.Prepare", CodeName(code), CodeMessage(code));
    };
    if (policy != GeosetFailurePolicy::Strict && policy != GeosetFailurePolicy::PreviewHideInvalidGeosets)
        return failure(Code::RequestInvalid);
    auto result = Create(assets, evaluationEpoch, limits);
    if (!result.Succeeded() || policy == GeosetFailurePolicy::Strict) return result;
    const std::uint64_t bytes = sizeof(GeosetPreviewData)
        + static_cast<std::uint64_t>(limits.maxGeosets) * sizeof(GeosetIssue)
        + static_cast<std::uint64_t>(limits.maxInstances) * sizeof(Range32);
    if (bytes > limits.maxBytes - result.value->StorageBytes()) return failure(Code::CapacityExceeded);
    try {
        auto preview = std::make_unique<GeosetPreviewData>();
        preview->issues.resize(limits.maxGeosets); preview->ranges.resize(limits.maxInstances);
        preview->storageBytes = bytes;
        result.value->preview_ = std::move(preview);
        return result;
    } catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}

War3Map::Result<PoseBatchEvaluator> PoseBatchEvaluator::Create(
    std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
    GeosetFailurePolicy policy, MaterialChannelOptions materials) {
    const auto failure = [](Code code) {
        return War3Map::Result<PoseBatchEvaluator>::Failure("RenderCore.Prepare", CodeName(code), CodeMessage(code));
    };
    if (materials.enabled > 1 || (materials.enabled && (!materials.maxLayerValues || materials.maxLayerValues > 16777216u)))
        return failure(Code::RequestInvalid);
    auto result = Create(assets, evaluationEpoch, limits, policy);
    if (!result.Succeeded() || !materials.enabled) return result;
    const auto bytes = sizeof(MaterialBatchData) + static_cast<std::uint64_t>(materials.maxLayerValues) * sizeof(float)
        + static_cast<std::uint64_t>(limits.maxInstances) * (sizeof(Range32) + sizeof(MaterialLayerIssue));
    if (bytes > limits.maxBytes - result.value->StorageBytes()) return failure(Code::CapacityExceeded);
    try {
        auto data = std::make_unique<MaterialBatchData>();
        data->alphas.resize(materials.maxLayerValues); data->ranges.resize(limits.maxInstances);
        data->issues.resize(limits.maxInstances); data->storageBytes = bytes;
        result.value->materials_ = std::move(data); return result;
    } catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}

War3Map::Result<PoseBatchEvaluator> PoseBatchEvaluator::Create(
    std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
    GeosetFailurePolicy policy, MaterialChannelOptions materials, TextureChannelOptions textures,
    std::span<const CompiledMaterialTextures> textureAssets) {
    const auto failure = [](Code code) {
        return War3Map::Result<PoseBatchEvaluator>::Failure("RenderCore.Prepare", CodeName(code), CodeMessage(code));
    };
    if (textures.enabled > 1 || (textures.enabled && (!textures.maxLayerValues || textures.maxLayerValues > 16777216u))
        || (!textures.enabled && !textureAssets.empty())) return failure(Code::RequestInvalid);
    auto result = Create(assets, evaluationEpoch, limits, policy, materials);
    if (!result.Succeeded() || !textures.enabled) return result;
    if (textureAssets.size() != assets.size()) return failure(Code::AssetIndexInvalid);
    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (!textureAssets[i].Matches(assets[i])) return failure(Code::AssetStale);
        if (textureAssets[i].MaterialLayerCount() != assets[i].MaterialLayerCount()) return failure(Code::OutputRangeInvalid);
    }
    const auto bytes = sizeof(TextureBatchData) + textureAssets.size() * sizeof(CompiledMaterialTextures)
        + static_cast<std::uint64_t>(textures.maxLayerValues) * (sizeof(std::uint32_t) + sizeof(UvTransform))
        + static_cast<std::uint64_t>(limits.maxInstances) * (sizeof(Range32) + sizeof(MaterialLayerIssue));
    if (bytes > limits.maxBytes - result.value->StorageBytes()) return failure(Code::CapacityExceeded);
    try {
        auto data = std::make_unique<TextureBatchData>(); data->assets.assign(textureAssets.begin(), textureAssets.end());
        data->textureIds.resize(textures.maxLayerValues); data->uvTransforms.resize(textures.maxLayerValues);
        data->ranges.resize(limits.maxInstances);
        data->issues.resize(limits.maxInstances); data->storageBytes = bytes;
        result.value->textures_ = std::move(data); return result;
    } catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}

War3Map::Result<PoseBatchEvaluator> PoseBatchEvaluator::Create(
    std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
    GeosetFailurePolicy policy, MaterialChannelOptions materials, TextureChannelOptions textures,
    std::span<const CompiledMaterialTextures> textureAssets, NodeKeySamplingPolicy nodeKeys) {
    const auto failure = [](Code code) {
        return War3Map::Result<PoseBatchEvaluator>::Failure("RenderCore.Prepare", CodeName(code), CodeMessage(code));
    };
    if (nodeKeys != NodeKeySamplingPolicy::Strict && nodeKeys != NodeKeySamplingPolicy::SourceOrderPreview)
        return failure(Code::RequestInvalid);
    auto result = Create(assets, evaluationEpoch, limits, policy, materials, textures, textureAssets);
    if (!result.Succeeded() || nodeKeys == NodeKeySamplingPolicy::Strict) return result;
    const auto count = 7ull * limits.maxNodes + limits.maxInstances;
    const auto extraBytes = 3ull * limits.maxNodes * sizeof(PoseDiagnostic);
    if (count > UINT32_MAX || result.value->StorageBytes() > limits.maxBytes
        || extraBytes > limits.maxBytes - result.value->StorageBytes())
        return failure(Code::CapacityExceeded);
    try {
        // 准备阶段预热；Evaluate不分配。旧严格BatchData大小/预算保持原样。
        result.value->data_->diagnostics.resize(static_cast<std::size_t>(count));
        result.value->data_->storageBytes += extraBytes;
        result.value->nodeKeys_ = nodeKeys;
        return result;
    } catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}

War3Map::Result<PoseBatchEvaluator> PoseBatchEvaluator::Create(
    std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
    GeosetFailurePolicy policy, MaterialChannelOptions materials, TextureChannelOptions textures,
    std::span<const CompiledMaterialTextures> textureAssets, NodeKeySamplingPolicy nodeKeys,
    MaterialAlphaKeySamplingPolicy materialAlphaKeys) {
    const auto failure = [](Code code) {
        return War3Map::Result<PoseBatchEvaluator>::Failure("RenderCore.Prepare", CodeName(code), CodeMessage(code));
    };
    if (materialAlphaKeys != MaterialAlphaKeySamplingPolicy::Strict
        && materialAlphaKeys != MaterialAlphaKeySamplingPolicy::IdenticalDuplicatesPreview)
        return failure(Code::RequestInvalid);
    if (materialAlphaKeys == MaterialAlphaKeySamplingPolicy::IdenticalDuplicatesPreview && !materials.enabled)
        return failure(Code::RequestInvalid);
    auto result = Create(assets, evaluationEpoch, limits, policy, materials, textures, textureAssets, nodeKeys);
    if (!result.Succeeded() || materialAlphaKeys == MaterialAlphaKeySamplingPolicy::Strict) return result;
    std::uint32_t warningsPerInstance = 0;
    for (const auto& asset : assets)
        warningsPerInstance = std::max(warningsPerInstance, ModelAccess::Get(asset)->materialDuplicateWarnings);
    const auto extraCount = static_cast<std::uint64_t>(warningsPerInstance) * limits.maxInstances;
    const auto count = result.value->data_->diagnostics.size() + extraCount;
    const auto extraBytes = extraCount * sizeof(PoseDiagnostic);
    const auto warningBytes = sizeof(MaterialAlphaPreviewBatchData) + extraCount * sizeof(MaterialAlphaPreviewWarning)
        + static_cast<std::uint64_t>(limits.maxInstances) * sizeof(Range32);
    if (count > UINT32_MAX || result.value->StorageBytes() > limits.maxBytes
        || extraBytes > limits.maxBytes - result.value->StorageBytes()
        || warningBytes > limits.maxBytes - result.value->StorageBytes() - extraBytes) return failure(Code::CapacityExceeded);
    try {
        // 新工作区独立构造，避免resize的几何增长隐含扩大最后的拥有容量。
        std::vector<PoseDiagnostic> diagnostics(static_cast<std::size_t>(count));
        auto warnings = std::make_unique<MaterialAlphaPreviewBatchData>();
        warnings->warnings.resize(static_cast<std::size_t>(extraCount)); warnings->ranges.resize(limits.maxInstances);
        warnings->storageBytes = warningBytes;
        result.value->data_->diagnostics = std::move(diagnostics);
        result.value->data_->storageBytes += extraBytes;
        result.value->materialAlphaPreview_ = std::move(warnings);
        result.value->materialAlphaKeys_ = materialAlphaKeys;
        return result;
    } catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}

BatchOutcome PoseBatchEvaluator::Evaluate(std::span<const PoseRequest> requests,
    std::span<const GroupMatrix> externalGroups, std::span<const BoneOverride> overrides) noexcept {
    if (!data_) return {Code::RequestInvalid, 0, 0};
    auto& data = *data_;
    data.active = 0;
    if (externalData_) return {Code::RequestInvalid, 0, 0};
    // 即使没有实例引用该池，也不能静默忽略宿主传入的覆盖。
    if (!overrides.empty()) return {Code::OverrideUnsupported, 0, 0};
    if (requests.size() > data.limits.maxInstances) return {Code::CapacityExceeded, 0, 0};
    if (Overlaps(externalGroups, data.groups) || (materials_ && ByteOverlaps(externalGroups, std::span(materials_->alphas)))
        || (textures_ && (ByteOverlaps(externalGroups, std::span(textures_->textureIds))
            || ByteOverlaps(externalGroups, std::span(textures_->uvTransforms)))))
        return {Code::RequestInvalid, 0, 0};
    std::uint64_t nodesNeeded = 0, groupsNeeded = 0, alphasNeeded = 0, layersNeeded = 0;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& r = requests[i];
        data.identities[i] = r.instance;
        if (r.assetIndex < data.assets.size()) {
            const auto& a = data.assets[r.assetIndex];
            if (r.source == PoseSource::Computed) nodesNeeded += a.NodeCount();
            groupsNeeded += a.GroupCount(); alphasNeeded += a.GeosetCount();
            if (materials_ || textures_) layersNeeded += a.MaterialLayerCount();
        }
    }
    if (nodesNeeded > data.limits.maxNodes || groupsNeeded > data.limits.maxGroups || alphasNeeded > data.limits.maxGeosets
        || (materials_ && layersNeeded > materials_->alphas.size())
        || (textures_ && layersNeeded > textures_->textureIds.size()))
        return {Code::CapacityExceeded, 0, 0};
    const auto identities = std::span(data.identities).first(requests.size());
    std::sort(identities.begin(), identities.end(), [](const auto& a, const auto& b) { return LessIdentity(a, b); });
    for (std::size_t i = 1; i < identities.size(); ++i)
        if (identities[i] == identities[i - 1]) return {Code::DuplicateInstance, 0, 0};

    std::uint64_t sourceOrderKeyBudget = MaxSourceOrderKeyVisits;
    std::uint32_t nodeOffset = 0, groupOffset = 0, alphaOffset = 0, layerOffset = 0, diagnosticOffset = 0, warningOffset = 0, failed = 0;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& r = requests[i];
        const auto* asset = r.assetIndex < data.assets.size() ? &data.assets[r.assetIndex] : nullptr;
        const auto nodeCount = asset && r.source == PoseSource::Computed ? asset->NodeCount() : 0;
        const auto groupCount = asset ? asset->GroupCount() : 0;
        const auto alphaCount = asset ? asset->GeosetCount() : 0;
        const auto layerCount = asset && (materials_ || textures_) ? asset->MaterialLayerCount() : 0;
        if (materials_) { materials_->ranges[i] = {}; materials_->issues[i] = {}; }
        if (textures_) { textures_->ranges[i] = {}; textures_->issues[i] = {}; }
        if (materialAlphaPreview_) materialAlphaPreview_->ranges[i] = {};
        // 源序预览最多另加每TRS通道一条；旧严格入口容量公式不变。
        const auto materialWarnings = asset && materialAlphaKeys_ == MaterialAlphaKeySamplingPolicy::IdenticalDuplicatesPreview
            ? ModelAccess::Get(*asset)->materialDuplicateWarnings : 0;
        const auto diagnosticCapacity = (nodeKeys_ == NodeKeySamplingPolicy::SourceOrderPreview ? 7u : 4u) * nodeCount
            + 1 + materialWarnings;
        std::uint32_t materialWarningCount = 0;
        const auto materialWarningStorage = materialAlphaPreview_
            ? std::span(materialAlphaPreview_->warnings).subspan(warningOffset, materialWarnings)
            : std::span<MaterialAlphaPreviewWarning>{};
        PoseReceipt receipt;
        receipt.instance = r.instance; receipt.asset = r.asset; receipt.source = r.source;
        receipt.evaluationEpoch = data.epoch;
        const bool compact = data.limits.outputMode == PoseOutputMode::GroupsAndAlphas;
        receipt.nodes = compact ? Range32{} : Range32{nodeOffset, nodeCount};
        receipt.groups = {groupOffset, groupCount};
        receipt.alphas = {alphaOffset, alphaCount};
        std::uint32_t issueCount = 0;
        const auto issues = preview_ ? std::span(preview_->issues).subspan(alphaOffset, alphaCount) : std::span<GeosetIssue>{};
        Evaluation e{std::span(data.diagnostics).subspan(diagnosticOffset, diagnosticCapacity)};
        e.nodeKeys = nodeKeys_;
        if (nodeKeys_ == NodeKeySamplingPolicy::SourceOrderPreview) e.sourceOrderKeyBudget = &sourceOrderKeyBudget;
        if (!asset) e.Fail(Code::AssetIndexInvalid);
        else EvaluateOne(*asset, r, externalGroups, std::span(data.nodes).subspan(compact ? 0 : nodeOffset, nodeCount),
            std::span(data.groups).subspan(groupOffset, groupCount), std::span(data.alphas).subspan(alphaOffset, alphaCount), std::span(data.colors).subspan(alphaOffset,alphaCount), receipt, e, preview_.get(), issues, issueCount);
        if (e.Good() && materials_) {
            const auto& model = *ModelAccess::Get(*asset);
            (void)EvaluateMaterialAlphas(model, r.sequenceIndex, receipt.sequenceFrame, r.globalElapsedMs,
                std::span(materials_->alphas).subspan(layerOffset, layerCount), materials_->issues[i], e,
                materialAlphaKeys_, materialWarningStorage, materialWarningCount);
        }
        if (e.Good() && textures_) {
            const auto& source = *MaterialTextureAccess::Get(textures_->assets[r.assetIndex]);
            (void)EvaluateMaterialTextures(source, r.sequenceIndex, receipt.sequenceFrame, r.globalElapsedMs,
                std::span(textures_->textureIds).subspan(layerOffset, layerCount),
                std::span(textures_->uvTransforms).subspan(layerOffset, layerCount), textures_->issues[i], e);
        }
        if (e.Good() && data.lastVersion == UINT64_MAX) e.Fail(Code::VersionExhausted);
        if (!e.Good()) {
            ++failed;
            receipt.error = e.error;
            receipt.nodes = {}; receipt.groups = {}; receipt.alphas = {}; receipt.sequenceFrame = 0;
            // 和参考 Result 的失败行为一致：丢弃此前警告，只发布第一条错误，姿势版本仍为 0。
            e.diagnostics[0] = {e.failureDiagnostic == Code::Ok ? e.error : e.failureDiagnostic,
                Severity::Error, e.objectId, e.track, e.failureLeft, e.failureRight}; e.used = 1;
            issueCount = 0;
        } else receipt.poseVersion = ++data.lastVersion;
        receipt.diagnostics = {diagnosticOffset, e.used};
        data.receipts[i] = receipt;
        if (materials_ && e.Good()) materials_->ranges[i] = {layerOffset, layerCount};
        if (textures_ && e.Good()) textures_->ranges[i] = {layerOffset, layerCount};
        if (materialAlphaPreview_ && e.Good()) materialAlphaPreview_->ranges[i] = {warningOffset, materialWarningCount};
        if (preview_) preview_->ranges[i] = {alphaOffset, issueCount};
        nodeOffset += nodeCount; groupOffset += groupCount; alphaOffset += alphaCount;
        layerOffset += layerCount;
        diagnosticOffset += diagnosticCapacity;
        warningOffset += materialWarnings;
    }
    data.active = static_cast<std::uint32_t>(requests.size());
    return {Code::Ok, data.active, failed};
}

BatchOutcome PoseBatchEvaluator::EvaluateExternalFrames(std::span<const ExternalFrameRequest> requests,
    const ExternalFrameContext& context, std::span<const GroupMatrix> groups,
    std::span<const float> alphas, std::span<const Vec3> colors) noexcept {
    if (!data_) return {Code::RequestInvalid, 0, 0};
    auto& data = *data_;
    data.active = 0;
    if (!externalData_ || materials_ || textures_) return {Code::RequestInvalid, 0, 0};
    if (context.contractVersion != ExternalFrameContractVersion || !SupportedSource(context.source)
        || context.sourceEpoch == 0 || context.frameVersion == 0) return {Code::ExternalFrameContextInvalid, 0, 0};
    if (requests.size() > data.limits.maxInstances) return {Code::CapacityExceeded, 0, 0};
    if (AliasesOutput(groups, data) || AliasesOutput(alphas, data) || AliasesOutput(colors, data))
        return {Code::RequestInvalid, 0, 0};
    std::uint64_t groupsNeeded = 0, geosetsNeeded = 0;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& r = requests[i];
        data.identities[i] = r.instance;
        if (r.assetIndex < data.assets.size()) {
            groupsNeeded += data.assets[r.assetIndex].GroupCount();
            geosetsNeeded += data.assets[r.assetIndex].GeosetCount();
        }
    }
    if (groupsNeeded > data.limits.maxGroups || geosetsNeeded > data.limits.maxGeosets)
        return {Code::CapacityExceeded, 0, 0};
    const auto identities = std::span(data.identities).first(requests.size());
    std::sort(identities.begin(), identities.end(), [](const auto& a, const auto& b) { return LessIdentity(a, b); });
    for (std::size_t i = 1; i < identities.size(); ++i)
        if (identities[i] == identities[i - 1]) return {Code::DuplicateInstance, 0, 0};
    std::uint32_t groupOffset = 0, channelOffset = 0, failed = 0;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& r = requests[i];
        const auto* asset = r.assetIndex < data.assets.size() ? &data.assets[r.assetIndex] : nullptr;
        const auto groupCount = asset ? asset->GroupCount() : 0;
        const auto channelCount = asset ? asset->GeosetCount() : 0;
        ExternalFrameReceipt receipt;
        receipt.instance = r.instance; receipt.asset = r.asset; receipt.evaluationEpoch = data.epoch;
        receipt.sourceEpoch = r.sourceEpoch; receipt.frameVersion = r.frameVersion;
        receipt.contractVersion = r.contractVersion; receipt.source = r.source;
        receipt.space = r.groups.space; receipt.worldTransformApplied = r.groups.worldTransformApplied;
        receipt.groups = {groupOffset, groupCount}; receipt.alphas = {channelOffset, channelCount};
        receipt.colors = receipt.alphas;
        Evaluation e{std::span(data.diagnostics).subspan(i, 1)};
        if (!asset) e.Fail(Code::AssetIndexInvalid);
        else CopyExternalFrame(*asset, r, context, groups, alphas, colors,
            std::span(data.groups).subspan(groupOffset, groupCount),
            std::span(data.alphas).subspan(channelOffset, channelCount),
            std::span(data.colors).subspan(channelOffset, channelCount), e);
        if (e.Good() && data.lastVersion == UINT64_MAX) e.Fail(Code::VersionExhausted);
        if (!e.Good()) {
            ++failed; receipt.error = e.error; receipt.groups = {}; receipt.alphas = {}; receipt.colors = {};
            e.diagnostics[0] = {e.error, Severity::Error, NoIndex, TrackKind::None, 0, 0}; e.used = 1;
        } else receipt.poseVersion = ++data.lastVersion;
        receipt.diagnostics = {static_cast<std::uint32_t>(i), e.used};
        externalData_->receipts[i] = receipt;
        // 只在内部镜像范围，沿用既有输出访问器；此旧回执不会对外发布。
        PoseReceipt view;
        view.error = receipt.error; view.groups = receipt.groups; view.alphas = receipt.alphas; view.diagnostics = receipt.diagnostics;
        data.receipts[i] = view;
        groupOffset += groupCount; channelOffset += channelCount;
    }
    data.active = static_cast<std::uint32_t>(requests.size());
    return {Code::Ok, data.active, failed};
}

std::uint64_t PoseBatchEvaluator::StorageBytes() const noexcept {
    return data_ ? data_->storageBytes + (preview_ ? preview_->storageBytes : 0) + (materials_ ? materials_->storageBytes : 0)
        + (textures_ ? textures_->storageBytes : 0) + (materialAlphaPreview_ ? materialAlphaPreview_->storageBytes : 0) : 0;
}

std::span<const PoseReceipt> PoseBatchEvaluator::Receipts() const noexcept {
    return data_ && !externalData_ ? std::span<const PoseReceipt>(data_->receipts).first(data_->active) : std::span<const PoseReceipt>{};
}
std::span<const ExternalFrameReceipt> PoseBatchEvaluator::ExternalFrameReceipts() const noexcept {
    return data_ && externalData_ ? std::span<const ExternalFrameReceipt>(externalData_->receipts).first(data_->active)
        : std::span<const ExternalFrameReceipt>{};
}
std::span<const NodePose> PoseBatchEvaluator::Nodes(std::uint32_t index) const noexcept {
    if (!data_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = data_->receipts[index].nodes;
    return std::span<const NodePose>(data_->nodes).subspan(range.offset, range.count);
}
std::span<const GroupMatrix> PoseBatchEvaluator::Groups(std::uint32_t index) const noexcept {
    if (!data_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = data_->receipts[index].groups;
    return std::span<const GroupMatrix>(data_->groups).subspan(range.offset, range.count);
}
std::span<const float> PoseBatchEvaluator::Alphas(std::uint32_t index) const noexcept {
    if (!data_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = data_->receipts[index].alphas;
    return std::span<const float>(data_->alphas).subspan(range.offset, range.count);
}
std::span<const Vec3> PoseBatchEvaluator::Colors(std::uint32_t index) const noexcept {
    if (!data_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = data_->receipts[index].alphas;
    return std::span<const Vec3>(data_->colors).subspan(range.offset,range.count);
}
bool PoseBatchEvaluator::MaterialChannelsEnabled() const noexcept { return materials_ != nullptr; }
std::span<const float> PoseBatchEvaluator::LayerAlphas(std::uint32_t index) const noexcept {
    if (!data_ || !materials_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = materials_->ranges[index];
    return std::span<const float>(materials_->alphas).subspan(range.offset, range.count);
}
std::span<const MaterialLayerIssue> PoseBatchEvaluator::MaterialIssues(std::uint32_t index) const noexcept {
    if (!data_ || !materials_ || index >= data_->active || materials_->issues[index].error == Code::Ok) return {};
    return std::span<const MaterialLayerIssue>(materials_->issues).subspan(index, 1);
}
std::span<const MaterialAlphaPreviewWarning> PoseBatchEvaluator::MaterialAlphaPreviewWarnings(std::uint32_t index) const noexcept {
    if (!data_ || !materialAlphaPreview_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = materialAlphaPreview_->ranges[index];
    return std::span<const MaterialAlphaPreviewWarning>(materialAlphaPreview_->warnings).subspan(range.offset, range.count);
}
bool PoseBatchEvaluator::TextureChannelsEnabled() const noexcept { return textures_ != nullptr; }
std::span<const std::uint32_t> PoseBatchEvaluator::LayerTextureIds(std::uint32_t index) const noexcept {
    if (!data_ || !textures_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = textures_->ranges[index];
    return std::span<const std::uint32_t>(textures_->textureIds).subspan(range.offset, range.count);
}
std::span<const UvTransform> PoseBatchEvaluator::LayerUvTransforms(std::uint32_t index) const noexcept {
    if (!data_ || !textures_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = textures_->ranges[index];
    return std::span<const UvTransform>(textures_->uvTransforms).subspan(range.offset, range.count);
}
std::span<const MaterialLayerIssue> PoseBatchEvaluator::TextureIssues(std::uint32_t index) const noexcept {
    if (!data_ || !textures_ || index >= data_->active || textures_->issues[index].error == Code::Ok) return {};
    return std::span<const MaterialLayerIssue>(textures_->issues).subspan(index, 1);
}
std::span<const PoseDiagnostic> PoseBatchEvaluator::Diagnostics(std::uint32_t index) const noexcept {
    if (!data_ || index >= data_->active) return {};
    const auto range = data_->receipts[index].diagnostics;
    return std::span<const PoseDiagnostic>(data_->diagnostics).subspan(range.offset, range.count);
}
std::span<const GeosetIssue> PoseBatchEvaluator::GeosetIssues(std::uint32_t index) const noexcept {
    if (!data_ || !preview_ || index >= data_->active || data_->receipts[index].error != Code::Ok) return {};
    const auto range = preview_->ranges[index];
    return std::span<const GeosetIssue>(preview_->issues).subspan(range.offset, range.count);
}
} // namespace War3RenderCore
