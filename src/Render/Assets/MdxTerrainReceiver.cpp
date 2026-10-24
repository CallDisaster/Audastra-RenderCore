// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3MapCore/Render/MdxTerrainReceiver.h"
#include "MdxPrimitiveRead.h"

#include <new>
#include <stdexcept>

namespace War3Map::Render {
namespace {
using namespace MdxReadInternal;
constexpr const char* kReceiverSubsystem = "Mdx.TerrainReceiver";

// 仅遍历共有GEOS几何前缀。后部蒙皮/UV/材质/动画不是此源视图的资格，
// 按原包含式边界保留；不创建第二套完整模型parser，不执行任何控制器。
template <typename Visit>
void VisitGeosets(std::span<const std::byte> bytes, const MdxLimits& limits, Visit&& visit) {
    Reader file(bytes, 0, bytes.size());
    if (file.TagView("文件头") != "MDLX") Fail("MDX_MAGIC", "缺少MDLX文件头。");
    bool seenVersion = false;
    while (file.Remaining()) {
        const auto tag = file.TagView("块标记");
        const auto size = file.U32("块大小");
        file.Need(size, "块内容");
        auto chunk = file.Until(file.Position() + size, "块内容");
        if (tag == "VERS") {
            if (seenVersion || size != 4 || chunk.U32("版本") != 800)
                Fail("MDX_VERSION_UNSUPPORTED", "接收面来源要求唯一完整版本800的VERS块。");
            seenVersion = true;
        } else if (tag == "GEOS") {
            if (!seenVersion) Fail("MDX_VERSION_UNSUPPORTED", "GEOS之前缺少版本800的VERS块。");
            while (chunk.Remaining()) {
                const auto start = chunk.Position();
                const auto geosetSize = chunk.U32("geoset大小");
                if (geosetSize < 4 || geosetSize - 4 > chunk.Remaining())
                    Fail("MDX_SIZE_INVALID", "geoset大小无效。");
                auto reader = chunk.Until(start + geosetSize, "GEOS对象");
                reader.ExpectTag("VRTX", "顶点");
                const auto vertices = reader.Count(12, limits.maxVerticesPerGeoset, "顶点");
                const auto positions = reader.Until(reader.Position() + static_cast<std::size_t>(vertices) * 12, "顶点");
                for (std::size_t i = 0; i < static_cast<std::size_t>(vertices) * 3; ++i) reader.F32("顶点坐标");
                reader.ExpectTag("NRMS", "法线");
                const auto normals = reader.Count(12, limits.maxVerticesPerGeoset, "法线");
                if (normals != 0 && normals != vertices) Fail("MDX_VALUE_INVALID", "法线数量与顶点数量不一致。");
                // 即使不输出法线也不掩盖损坏的共有几何前缀。
                for (std::size_t i = 0; i < static_cast<std::size_t>(normals) * 3; ++i) reader.F32("法线");
                auto primitive = ReadPrimitiveView(reader, limits, vertices);
                visit(start, geosetSize, vertices, positions, std::move(primitive));
                chunk.Skip(geosetSize - 4, "GEOS对象内容");
            }
        }
        file.Skip(size, "块内容");
    }
    if (!seenVersion) Fail("MDX_VERSION_UNSUPPORTED", "缺少版本800的VERS块。");
}

struct ReceiverReadPlan {
    std::size_t geosets = 0, vertices = 0, groups = 0, indices = 0;
    std::size_t residentBytes = sizeof(MdxTerrainReceiverSource), maximumBytes = 0;
    void Charge(std::size_t count, std::size_t stride) {
        if (residentBytes > maximumBytes || count > (maximumBytes - residentBytes) / stride)
            Fail("MDX_RECEIVER_BUDGET", "接收面来源的原文或解码存储超过调用方预算。");
        residentBytes += count * stride;
    }
    void Count(std::size_t& total, std::size_t count, std::size_t maximum, std::size_t stride) {
        if (total > maximum || count > maximum - total) Fail("MDX_LIMIT", "接收面原图元累计数量超过上限。");
        total += count; Charge(count, stride);
    }
};
} // namespace

Result<MdxTerrainReceiverSource> ReadMdxTerrainReceiverSource(
    std::span<const std::byte> bytes, std::size_t maximumResidentBytes, const MdxLimits& limits) {
    using Out = Result<MdxTerrainReceiverSource>;
    try {
        ValidateMdxLimits(limits);
        if (!maximumResidentBytes || maximumResidentBytes > 128u * 1024u * 1024u)
            Fail("MDX_RECEIVER_BUDGET", "接收面解析预算必须为1至128MiB。");
        if (bytes.size() > limits.maxInputBytes) Fail("MDX_LIMIT", "MDX文件超过大小上限。");
        ReceiverReadPlan plan; plan.maximumBytes = maximumResidentBytes;
        plan.Charge(bytes.size(), sizeof(std::byte));
        VisitGeosets(bytes, limits, [&](auto, auto, auto vertices, auto, auto primitive) {
            plan.Count(plan.geosets, 1, limits.maxGeosets, sizeof(MdxTerrainReceiverGeoset));
            plan.Count(plan.vertices, vertices, limits.maxTotalVertices, sizeof(std::array<float, 3>));
            plan.Count(plan.groups, primitive.groupCount, limits.maxTotalIndices, sizeof(MdxTerrainReceiverGroup));
            plan.Count(plan.indices, primitive.indexCount, limits.maxTotalIndices, sizeof(std::uint16_t));
        });
        if (!plan.geosets) Fail("MDX_RECEIVER_EMPTY", "接收面来源没有GEOS对象。");
        // 第一遍成功后才分配。仅从自己的原文快照解码第二遍，来源和数组不可分离。
        std::vector<std::byte> stored(bytes.begin(), bytes.end());
        std::vector<MdxTerrainReceiverGeoset> geosets; geosets.reserve(plan.geosets);
        VisitGeosets(stored, limits, [&](auto start, auto size, auto vertices, auto positions, auto primitive) {
            MdxTerrainReceiverGeoset geoset;
            geoset.sourceByteOffset = start; geoset.sourceByteSize = size;
            geoset.positionsByteOffset = positions.Position();
            geoset.positions.resize(vertices);
            for (auto& position : geoset.positions) for (auto& axis : position) axis = positions.F32("顶点坐标");
            geoset.groups.reserve(primitive.groupCount); geoset.indices.reserve(primitive.indexCount);
            std::uint32_t firstIndex = 0;
            for (std::size_t group = 0; group < primitive.groupCount; ++group) {
                MdxTerrainReceiverGroup output;
                output.typeByteOffset = primitive.types.Position(); output.countByteOffset = primitive.counts.Position();
                output.sourceType = primitive.types.U32("图元类型"); output.indexCount = primitive.counts.U32("图元计数");
                output.firstIndex = firstIndex;
                output.indicesByteOffset = primitive.indices.Position() + static_cast<std::size_t>(firstIndex) * 2;
                firstIndex += output.indexCount; geoset.groups.push_back(output);
            }
            while (primitive.indices.Remaining()) geoset.indices.push_back(primitive.indices.U16("图元索引"));
            geosets.push_back(std::move(geoset));
        });
        return Out::Success(MdxTerrainReceiverSource(std::move(stored), std::move(geosets), plan.residentBytes));
    } catch (const MdxError& error) {
        return Out::Failure(kReceiverSubsystem, error.code, error.what());
    } catch (const std::bad_alloc&) {
        return Out::Failure(kReceiverSubsystem, "MDX_ALLOCATION", "接收面来源分配失败。");
    } catch (const std::length_error&) {
        return Out::Failure(kReceiverSubsystem, "MDX_ALLOCATION", "接收面来源超出容器容量。");
    }
}

} // namespace War3Map::Render
