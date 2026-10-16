// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "MdxNodeRead.h"
#include <algorithm>
#include <vector>

namespace War3Map::Render::MdxReadInternal {

// 经典二进制读取器按文档登记次序覆盖 Node 的 objectId，再消费 parent/MATS/PIVT 索引。
// 此处只接纳其有界子集：其它节点已经逐项等于读取序号，仅 PREM 使用无效 ID 哨兵，
// 全部节点种类已识别且 PIVT 完整。有限重复/错位 ID、未知块和缺轴心仍走原拒绝路径。
// 原文件字段、父引用和矩阵引用都不改写；不据此宣称粒子外观与原版一致。
inline void ResolvePremSentinelIds(MdxModel& model, std::vector<Diagnostic>& diagnostics) {
    std::vector<MdxNode*> nodes;
    nodes.reserve(MdxNodeCount(model));
    for (auto& node : model.bones) nodes.push_back(&node);
    for (auto& node : model.helpers) nodes.push_back(&node);
    for (auto& node : model.otherNodes) nodes.push_back(&node);
    if (model.skippedChunks != 0 || model.pivots.size() < nodes.size()) return;
    std::sort(nodes.begin(), nodes.end(), [](auto* a, auto* b) {
        return a->sourceByteOffset < b->sourceByteOffset;
    });
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto& node = *nodes[index];
        if (node.sourceByteOffset == 0 || (index != 0 && node.sourceByteOffset == nodes[index - 1]->sourceByteOffset)) return;
        if (node.objectId == UINT32_MAX && node.kind == MdxNodeKind::ParticleEmitter) continue;
        if (node.objectId != index) return;
    }
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = *nodes[index];
        if (node.objectId != UINT32_MAX) continue;
        node.objectId = static_cast<std::uint32_t>(index);
        diagnostics.push_back({DiagnosticSeverity::Warning, "Mdx", "MDX_NODE_ID_BY_SOURCE_ORDER",
            "PREM 原 objectId=4294967295，按经典文档读取次序使用运行时节点=" + std::to_string(index)
                + "；原 ID、父引用和轴心均保留，粒子外观未作原版签收。",
            OriginSpan{{}, node.sourceByteOffset + 84, 4, {}}});
    }
    for (auto& emitter : model.premEmitters)
        emitter.nodeId = model.otherNodes[emitter.otherNodeIndex].objectId;
}

} // namespace War3Map::Render::MdxReadInternal
