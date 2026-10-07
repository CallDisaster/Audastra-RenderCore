// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3RenderCore/CompiledModel.h"
#include "War3RenderCore/ExternalFrame.h"
#include "War3RenderCore/MaterialTextures.h"
#include "War3RenderCore/NodeKeySamplingPolicy.h"
#include "War3RenderCore/MaterialAlphaKeySamplingPolicy.h"

#include <memory>
#include <span>

namespace War3RenderCore {
namespace Detail { struct BatchData; struct ExternalBatchData; struct GeosetPreviewData; struct MaterialBatchData; struct TextureBatchData; struct MaterialAlphaPreviewBatchData; }

enum class GeosetFailurePolicy : std::uint32_t { Strict = 0, PreviewHideInvalidGeosets = 1 };
struct GeosetIssue {
    std::uint32_t geosetIndex = NoIndex; // 原 MDX geoset 编号；不是节点 objectId。
    PoseDiagnostic diagnostic;         // 原始局部拒绝诊断，不转为整模型成功。
};

// 一个所有者线程使用一个求值器；不同求值器可共享不可变 CompiledModel。
// Create 负责容量预热；Evaluate 不分配、不抛异常、不推进宿主时钟。
// 输出视图仅借用内部工作区，下一次 Evaluate/移动/销毁之前有效，不能交异步 GPU 直接借用。
class PoseBatchEvaluator {
public:
    PoseBatchEvaluator();
    ~PoseBatchEvaluator();
    PoseBatchEvaluator(PoseBatchEvaluator&&) noexcept;
    PoseBatchEvaluator& operator=(PoseBatchEvaluator&&) noexcept;
    PoseBatchEvaluator(const PoseBatchEvaluator&) = delete;
    PoseBatchEvaluator& operator=(const PoseBatchEvaluator&) = delete;

    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> Create(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits = {});
    // 仅显式预览容错已界定 GEOA 数据；原三参数入口和外部帧保持严格。
    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> Create(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
        GeosetFailurePolicy policy);
    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> Create(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
        GeosetFailurePolicy policy, MaterialChannelOptions materials);
    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> Create(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
        GeosetFailurePolicy policy, MaterialChannelOptions materials, TextureChannelOptions textures,
        std::span<const CompiledMaterialTextures> textureAssets);

    // 只在此显式入口选择源序节点预览；全部旧Create和外部帧入口保持Strict。
    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> Create(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
        GeosetFailurePolicy policy, MaterialChannelOptions materials, TextureChannelOptions textures,
        std::span<const CompiledMaterialTextures> textureAssets, NodeKeySamplingPolicy nodeKeys);

    // 材质键策略独立显式选择；全部旧Create和外部帧入口仍为Strict。
    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> Create(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits,
        GeosetFailurePolicy policy, MaterialChannelOptions materials, TextureChannelOptions textures,
        std::span<const CompiledMaterialTextures> textureAssets, NodeKeySamplingPolicy nodeKeys,
        MaterialAlphaKeySamplingPolicy materialAlphaKeys);

    // 专用模式：预热新回执且计入预算，不改变旧 Create 的存储估计或预算边界。
    // 两个模式不能互调 Evaluate；误用仍使前次输出失效。
    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> CreateExternalFrames(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits = {});

    // 同一批可混合 Computed / ExternalGroups。外部片段按资产的 geoset 原顺序展开，
    // 数量必须等于 GroupCount；透明度仍由该请求的动作时间求值，外部来源不产出骨骼矩阵。
    // 不允许输入借用本求值器上一次输出；否则内部复用可能覆盖尚未读取的数据。
    [[nodiscard]] BatchOutcome Evaluate(std::span<const PoseRequest> requests,
        std::span<const GroupMatrix> externalGroups = {}, std::span<const BoneOverride> overrides = {}) noexcept;

    // 当前帧身份由独立宿主上下文给出，输入不能自称“预期版本”。
    // alpha 只要求有限；RGB 必须有限且为 0..1 的绝对值，不乘静态颜色。
    // 输入不得借用本求值器之前的 Groups/Alphas/Colors（含跨类型别名）。
    [[nodiscard]] BatchOutcome EvaluateExternalFrames(std::span<const ExternalFrameRequest> requests,
        const ExternalFrameContext& context, std::span<const GroupMatrix> groups,
        std::span<const float> alphas, std::span<const Vec3> colors) noexcept;

    // 受预算控制的有效存储估计（含诊断/回执/暂存），不代表进程 RSS 或资产占用。
    [[nodiscard]] std::uint64_t StorageBytes() const noexcept;
    [[nodiscard]] std::span<const PoseReceipt> Receipts() const noexcept;
    // 外部模式仅发布此类型回执，旧 Receipts() 为空；Nodes() 始终为空。
    [[nodiscard]] std::span<const ExternalFrameReceipt> ExternalFrameReceipts() const noexcept;
    [[nodiscard]] std::span<const NodePose> Nodes(std::uint32_t instanceIndex) const noexcept;
    [[nodiscard]] std::span<const GroupMatrix> Groups(std::uint32_t instanceIndex) const noexcept;
    [[nodiscard]] std::span<const float> Alphas(std::uint32_t instanceIndex) const noexcept;
    // RGB 与 Alphas 共用 geoset 序及回执片段；失败/下次求值后的寿命规则相同。
    [[nodiscard]] std::span<const Vec3> Colors(std::uint32_t instanceIndex) const noexcept;
    [[nodiscard]] bool MaterialChannelsEnabled() const noexcept;
    // 原material/layer唯一序；身份/借用期沿同一PoseReceipt，失败为空。
    [[nodiscard]] std::span<const float> LayerAlphas(std::uint32_t instanceIndex) const noexcept;
    // 每实例最多首个严格材质错误；物理来源不伪装成node objectId。
    [[nodiscard]] std::span<const MaterialLayerIssue> MaterialIssues(std::uint32_t instanceIndex) const noexcept;
    // 实际成功警告，借用期与同一PoseReceipt一致；默认/严格模式、整实例失败为空。
    [[nodiscard]] std::span<const MaterialAlphaPreviewWarning> MaterialAlphaPreviewWarnings(std::uint32_t instanceIndex) const noexcept;
    [[nodiscard]] bool TextureChannelsEnabled() const noexcept;
    // 绝对TEXS ID，不经float；唯一material/layer序，沿同一PoseReceipt，失败为空。
    [[nodiscard]] std::span<const std::uint32_t> LayerTextureIds(std::uint32_t instanceIndex) const noexcept;
    [[nodiscard]] std::span<const MaterialLayerIssue> TextureIssues(std::uint32_t instanceIndex) const noexcept;
    [[nodiscard]] std::span<const PoseDiagnostic> Diagnostics(std::uint32_t instanceIndex) const noexcept;
    // 与对应姿势相同的借用期；严格模式/整实例失败为空，非空表示局部预览。
    [[nodiscard]] std::span<const GeosetIssue> GeosetIssues(std::uint32_t instanceIndex) const noexcept;
private:
    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> CreateImpl(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits, bool externalFrames);
    std::unique_ptr<Detail::BatchData> data_;
    // 单独分配，保留 BatchData 大小，避免改变旧 Create 的预算公式。
    std::unique_ptr<Detail::ExternalBatchData> externalData_;
    // 独立预分配，仅显式容错模式使用；不改变严格 Create 或外部帧预算。
    std::unique_ptr<Detail::GeosetPreviewData> preview_;
    std::unique_ptr<Detail::MaterialBatchData> materials_;
    std::unique_ptr<Detail::TextureBatchData> textures_;
    std::unique_ptr<Detail::MaterialAlphaPreviewBatchData> materialAlphaPreview_;
    NodeKeySamplingPolicy nodeKeys_ = NodeKeySamplingPolicy::Strict;
    MaterialAlphaKeySamplingPolicy materialAlphaKeys_ = MaterialAlphaKeySamplingPolicy::Strict;
};

} // namespace War3RenderCore
