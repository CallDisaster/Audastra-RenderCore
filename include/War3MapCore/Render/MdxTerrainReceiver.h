// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3MapCore/Render/MdxModel.h"

namespace War3Map::Render {

// 原 PTYP/PCNT/PVTX 的一组，保留空组、未知类型及原索引切片；不转换成显示三角形。
struct MdxTerrainReceiverGroup {
    std::uint32_t sourceType = 0, firstIndex = 0, indexCount = 0;
    std::uint64_t typeByteOffset = 0, countByteOffset = 0, indicesByteOffset = 0;
    // 1.27a cliff loader 的映射；PTYP6 不擅自映为下游支持的 fan。
    [[nodiscard]] std::uint32_t NativeTopology() const noexcept {
        return sourceType == 4 ? 3u : sourceType == 5 ? 4u : 0u;
    }
};

struct MdxTerrainReceiverGeoset {
    std::uint64_t sourceByteOffset = 0, sourceByteSize = 0, positionsByteOffset = 0;
    std::vector<std::array<float, 3>> positions; // VRTX 原顺序；未蒙皮、未旋转或起伏变形。
    std::vector<MdxTerrainReceiverGroup> groups; // 全部原组，原顺序。
    std::vector<std::uint16_t> indices; // 全部原 PVTX；firstIndex/indexCount 指向此表。
};

// 不可变的来源几何视图，不是预览 mesh，也不是完整模型/动画/原版保存资格。
// 原输入封存于此；宿主用现有 SHA 实现对 Bytes() 计算身份，再绑定资源 winner、修订和成员 SHA。
// 不接受调用者提供的“已验证”布尔值或伪造摘要。未解释 GEOS 尾部/其它块按外层边界保留，
// 本 API 不声明它们语义有效；接收面 owner 仍须检查静态普通 cliff/ramp 的适用范围。
class MdxTerrainReceiverSource {
public:
    [[nodiscard]] std::span<const std::byte> Bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::span<const MdxTerrainReceiverGeoset> Geosets() const noexcept { return geosets_; }
    [[nodiscard]] std::size_t ChargedResidentBytes() const noexcept { return chargedResidentBytes_; }
private:
    MdxTerrainReceiverSource(std::vector<std::byte> bytes, std::vector<MdxTerrainReceiverGeoset> geosets,
        std::size_t chargedResidentBytes)
        : bytes_(std::move(bytes)), geosets_(std::move(geosets)), chargedResidentBytes_(chargedResidentBytes) {}
    std::vector<std::byte> bytes_;
    std::vector<MdxTerrainReceiverGeoset> geosets_;
    std::size_t chargedResidentBytes_ = 0;
    friend Result<MdxTerrainReceiverSource> ReadMdxTerrainReceiverSource(
        std::span<const std::byte>, std::size_t, const MdxLimits&);
};

// 两遍读取：第一遍不分配变长表，先预收原文件、解码位置、全部组和原索引的有效存储。
// maximumResidentBytes 为1..128MiB且只能收紧；MdxLimits 既有硬上限保持，累计索引包含所有类型。
// 计量 sizeof 有效存储，不包含分配器元数据、借用输入、固定栈和有界失败诊断。
// 只读经典800的全部GEOS，不因 Stand、GEOA/材质 alpha、选择标志或控制器删除任何几何。
[[nodiscard]] Result<MdxTerrainReceiverSource> ReadMdxTerrainReceiverSource(
    std::span<const std::byte> bytes, std::size_t maximumResidentBytes, const MdxLimits& limits = {});

// receiver专用完整静态语法门：先以既有两遍计量器检查所有可分配表，再用共用模型解析器
// 验证完整引用和节点拓扑。接纳空法线、原strip/未知/空图元组；不按材质/GEOA可见性删几何。
// 其它域仍限于已解释的经典静态块，拒绝TRS、材质/GEOA轨迹、未知尾部和外部动画。
// 返回实际预收的解析峰值，模型在返回前释放；不授予caster、预览或原版保存资格，
// 也不替代ReadMdxTerrainReceiverSource的原序几何来源。1..128MiB只可收紧；计量口径同静态解析API。
[[nodiscard]] Result<std::size_t> ValidateMdxTerrainReceiverStaticBudgeted(
    std::span<const std::byte> bytes, std::size_t maximumResidentBytes, const MdxLimits& limits = {});

} // namespace War3Map::Render
