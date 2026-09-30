// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3RenderCore/Result.h"
#include "War3RenderCore/PoseTypes.h"
#include "War3RenderCore/GeosetColor.h"

#include <memory>
#include <span>

namespace War3Map::Render { struct MdxModel; }
namespace War3RenderCore {
namespace Detail { struct ModelData; struct ModelAccess; }

// 仅为进程内的不可变资产句柄，shared_ptr 不属于序列化/插件 ABI。
// 拥有轨迹、拓扑、顶点组号及 CPU 核对所用的位置/法线，不借用原始 MdxModel。
class CompiledModel {
public:
    CompiledModel() = default;
    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] AssetIdentity Identity() const noexcept;
    [[nodiscard]] const AssetSource& Source() const noexcept;
    [[nodiscard]] std::uint32_t NodeCount() const noexcept;
    [[nodiscard]] std::uint32_t GroupCount() const noexcept;
    [[nodiscard]] std::uint32_t GeosetCount() const noexcept;
    [[nodiscard]] std::uint32_t SequenceCount() const noexcept;
    [[nodiscard]] NodeLayout Node(std::uint32_t index) const noexcept;
    [[nodiscard]] std::span<const GeosetLayout> Geosets() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> VertexGroups() const noexcept;
    // GEOA 静态属性按原 geoset 顺序；动态 KGAC 必须读取求值器 Colors，不能用它冒充当前帧。
    [[nodiscard]] std::span<const GeosetColor> GeosetColors() const noexcept;
    [[nodiscard]] bool SharesStorageWith(const CompiledModel& other) const noexcept;
private:
    std::shared_ptr<const Detail::ModelData> data_;
    friend struct Detail::ModelAccess;
};

// 静态结构错误在加载时拒绝。序列/轨迹/透明度的条件性不支持状态预先编译成诊断，
// 求值时只读取该状态，不重建节点表或重复遍历按键校验；没有静默修正输入。
[[nodiscard]] War3Map::Result<CompiledModel> CompileMdxModel(
    const War3Map::Render::MdxModel& model, AssetIdentity identity, AssetSource source,
    std::uint64_t maxCompiledBytes = 128ull * 1024 * 1024);

// 可选的 CPU 预览/对照助手，不由批量姿势求值自动调用。输入为该资产全部组矩阵。
// 输出跨度必须精确匹配该 geoset 顶点数；失败时调用者须丢弃整个输出跨度。
// 法线遵循现有预览：同一组矩阵 3x3 变换后归一化，不是逆转置语义。
[[nodiscard]] Code SkinPreviewGeoset(const CompiledModel& model, std::uint32_t geosetIndex,
    std::span<const GroupMatrix> groups, std::span<Vec3> positions, std::span<Vec3> normals) noexcept;

} // namespace War3RenderCore
