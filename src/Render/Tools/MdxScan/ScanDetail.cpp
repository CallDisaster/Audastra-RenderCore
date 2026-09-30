// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "ScanDetail.h"
#include "../../Assets/MdxNormalPreview.h"
#include "War3RenderCore/ModelContent.h"
#include <array>
#include <charconv>
#include <map>

namespace War3Map::Render::Scan {
namespace {
constexpr std::string_view Cut = "detail_truncated=1";
const char* Kind(MdxNodeKind kind) {
    switch (kind) {
    case MdxNodeKind::Helper: return "HELP";
    case MdxNodeKind::Bone: return "BONE";
    case MdxNodeKind::Attachment: return "ATCH";
    case MdxNodeKind::ParticleEmitter: return "PREM";
    case MdxNodeKind::ParticleEmitter2: return "PRE2";
    case MdxNodeKind::Ribbon: return "RIBB";
    case MdxNodeKind::Event: return "EVTS";
    case MdxNodeKind::Light: return "LITE";
    case MdxNodeKind::Collision: return "CLID";
    }
    return "UNKNOWN";
}
std::string Hex(std::uint32_t value) {
    std::array<char, 8> digits{};
    const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), value, 16);
    return "0x" + std::string(digits.data(), result.ptr);
}
}
void AppendDetail(Record& record, std::string_view entry) {
    if (entry.empty() || record.detail.ends_with(Cut)) return;
    const auto separator = record.detail.empty() ? 0u : 2u;
    // 留出末尾截断标记的空间，不剪断 UTF-8 字符或半条字段。
    if (entry.size() > MaxDetailBytes - Cut.size() - 2
        || record.detail.size() + separator + entry.size() > MaxDetailBytes - Cut.size() - 2) {
        if (!record.detail.empty()) record.detail += "; ";
        record.detail += Cut;
        return;
    }
    if (!record.detail.empty()) record.detail += "; ";
    record.detail += entry;
}
void DescribeModel(const MdxModel& model, Record& record, const Checkpoint& checkpoint) {
    // 枚举只用于解释，不改变任何编译/求值准入。
    const auto content = War3RenderCore::ClassifyMdxContent(model);
    AppendDetail(record, content == War3RenderCore::ModelContentKind::Mesh ? "content=mesh"
        : content == War3RenderCore::ModelContentKind::ParticleOnlyUnimplemented
            ? "content=particle_only_unimplemented; particle_rendered=0" : "content=other_node_only");
    std::map<std::uint32_t, const MdxNode*> nodes;
    std::map<std::string, std::uint32_t> kinds;
    ForEachMdxNode(model, [&](const MdxNode& node) { nodes.emplace(node.objectId, &node); ++kinds[Kind(node.kind)]; });
    for (const auto& [kind, count] : kinds) AppendDetail(record, kind + "=" + std::to_string(count));
    for (std::size_t s = 0; s < model.sequences.size(); ++s) {
        checkpoint();
        const auto& sequence = model.sequences[s];
        if (!sequence.Usable()) AppendDetail(record, "bad_sequence=" + std::to_string(s)
            + " name=" + sequence.name + " start=" + std::to_string(sequence.start)
            + " end=" + std::to_string(sequence.end) + " flags=" + Hex(sequence.flags));
    }
    for (std::size_t g = 0; g < model.geosets.size(); ++g) {
        checkpoint();
        const auto count = MdxZeroNormalCount(model.geosets[g].normals);
        if (count != 0) AppendDetail(record, "zero_normals geoset=" + std::to_string(g) + " vertices=" + std::to_string(count));
    }
    if (!model.skinningTopologyComplete) AppendDetail(record, "topology=" + model.skinningTopologyIssue);
    ForEachMdxNode(model, [&](const MdxNode& node) {
        checkpoint();
        const auto unknown = node.flags & ~(MdxNodeNonTransformFlags(node.kind) | 0xFFu);
        const auto parent = node.parentId >= 0 ? nodes.find(static_cast<std::uint32_t>(node.parentId)) : nodes.end();
        const bool missing = node.parentId >= 0 && parent == nodes.end();
        const bool otherParent = parent != nodes.end() && parent->second->kind != MdxNodeKind::Bone
            && parent->second->kind != MdxNodeKind::Helper;
        if ((node.flags & 7u) != 0 || unknown != 0 || missing || otherParent) {
            const auto parentKind = node.parentId == -1 ? "ROOT" : parent == nodes.end() ? "MISSING" : Kind(parent->second->kind);
            AppendDetail(record, "node=" + std::to_string(node.objectId) + " kind=" + Kind(node.kind) + " name=" + node.name
                + " flags=" + Hex(node.flags) + " unknown=" + Hex(unknown)
                + " parent=" + std::to_string(node.parentId) + " parent_kind=" + parentKind);
        }
    });
}
} // namespace War3Map::Render::Scan
