// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3RenderCore/CompiledModel.h"
#include "War3RenderCore/ExternalFrame.h"

#include <memory>
#include <span>

namespace War3RenderCore {
namespace Detail { struct BatchData; struct ExternalBatchData; }

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
    [[nodiscard]] std::span<const PoseDiagnostic> Diagnostics(std::uint32_t instanceIndex) const noexcept;
private:
    [[nodiscard]] static War3Map::Result<PoseBatchEvaluator> CreateImpl(
        std::span<const CompiledModel> assets, std::uint64_t evaluationEpoch, BatchLimits limits, bool externalFrames);
    std::unique_ptr<Detail::BatchData> data_;
    // 单独分配，保留 BatchData 大小，避免改变旧 Create 的预算公式。
    std::unique_ptr<Detail::ExternalBatchData> externalData_;
};

} // namespace War3RenderCore
