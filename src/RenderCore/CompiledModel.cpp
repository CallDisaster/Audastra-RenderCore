// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "PoseMath.h"
#include "SourceOrderNodeTrack.h"
#include "GeosetChannels.h"
#include "MaterialChannels.h"
#include "../Render/Assets/MdxNormalPreview.h"
#include "../Render/Assets/MdxRigidStaticPreviewInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace War3RenderCore {
namespace {
using namespace War3Map::Render;
using namespace Detail;

struct CompileFailure { Code code; };
void Require(bool condition, Code code) { if (!condition) throw CompileFailure{code}; }

// 估计的是核心拥有的有效存储与拓扑临时表；不是分配器开销或系统 RSS 承诺。
struct Budget {
    std::uint64_t remaining;
    void Charge(std::uint64_t count, std::uint64_t stride) {
        Require(stride != 0 && count <= remaining / stride, Code::CapacityExceeded);
        remaining -= count * stride;
    }
};
template <std::size_t N>
bool ControlValid(const std::array<float, N>& values) {
    if (!Finite(values)) return false;
    double squared = 0;
    for (const auto x : values) squared += static_cast<double>(x) * x;
    return std::isfinite(squared) && squared >= 1e-20;
}

template <std::size_t N>
void CompileSequenceKeys(Track<N>& track, const std::vector<MdxSequence>& sequences, Budget& budget) {
    // 原版按动作文件顺序切连续区间；只有与按时间二分不同（逆序键、重叠或倒序排列的动作）时才保存。
    const bool sorted = std::is_sorted(track.keys.begin(), track.keys.end(),
        [](const auto& left, const auto& right) { return left.frame < right.frame; });
    bool needed = !sorted;
    if (sorted) ForEachGameSequenceRange(track.keys, sequences, [&](std::size_t index, std::size_t first, std::size_t count) {
        const auto& sequence = sequences[index];
        const auto begin = std::lower_bound(track.keys.begin(), track.keys.end(), static_cast<std::int64_t>(sequence.start),
            [](const auto& key, auto value) { return key.frame < value; });
        const auto end = std::upper_bound(begin, track.keys.end(), static_cast<std::int64_t>(sequence.end),
            [](auto value, const auto& key) { return value < key.frame; });
        if (end != begin && (static_cast<std::size_t>(begin - track.keys.begin()) != first
            || static_cast<std::size_t>(end - begin) != count)) needed = true;
        if (end == begin && count != 0) needed = true;
        return !needed;
    });
    if (!needed) return;
    budget.Charge(sequences.size(), sizeof(Range32) + sizeof(std::uint8_t));
    track.sequenceKeys.reserve(sequences.size());
    track.sequenceDescending.reserve(sequences.size());
    ForEachGameSequenceRange(track.keys, sequences, [&](std::size_t, std::size_t first, std::size_t count) {
        std::int64_t left = 0, right = 0;
        track.sequenceKeys.push_back({static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count)});
        track.sequenceDescending.push_back(FirstDescendingEdge(track.keys, first, count, left, right) ? 1 : 0);
        return true;
    });
}

template <std::size_t N>
Track<N> CompileTrack(const std::optional<MdxNodeTrack<N>>& source,
    const std::vector<std::uint32_t>& periods, const std::vector<MdxSequence>& sequences, Budget& budget) {
    Track<N> track;
    if (!source) return track;
    track.present = true;
    track.interpolation = source->interpolation;
    if (static_cast<std::uint32_t>(track.interpolation) > 3) {
        track.error = Code::PoseInterpolationUnsupported;
        return track;
    }
    track.global = source->globalSequence != NoIndex;
    if (track.global) {
        if (source->globalSequence >= periods.size()) {
            track.error = Code::PoseGlobalSequenceInvalid;
            return track;
        }
        track.period = periods[source->globalSequence];
    }
    track.keys.reserve(source->keys.size());
    if (track.global && !source->keys.empty()) track.origin = source->keys.front().frame;
    std::int64_t previous = std::numeric_limits<std::int64_t>::min();
    for (const auto& key : source->keys) {
        if (key.frame < previous) {
            if (track.global) {
                track.error = Code::PoseKeysInvalid;
                track.keys.clear();
                return track;
            }
        }
        previous = key.frame;
        const auto frame = static_cast<std::int64_t>(key.frame) - track.origin;
        if (track.global && frame > INT32_MAX) {
            track.error = Code::PoseGlobalSpanUnsupported;
            track.keys.clear();
            return track;
        }
        track.keys.push_back({static_cast<std::int32_t>(frame), key.value, key.inTangent, key.outTangent,
            Finite(key.value), ControlValid(key.inTangent), ControlValid(key.outTangent)});
    }
    if (!track.global) CompileSequenceKeys(track, sequences, budget);
    return track;
}


std::shared_ptr<const ModelData> Compile(const MdxModel& model,
    AssetIdentity identity, AssetSource source, std::uint64_t maxBytes, bool rigidStaticPreview) {
    const MdxLimits hard;
    Require(identity.value != 0 && !source.virtualPath.empty() && !source.sourceLayer.empty()
        && !source.contentDigest.empty() && source.virtualPath.size() <= 4096
        && source.sourceLayer.size() <= 1024 && source.contentDigest.size() <= 256, Code::AssetIdentityInvalid);
    Require(maxBytes != 0 && maxBytes <= 128ull * 1024 * 1024, Code::CapacityExceeded);
    Require(model.version == 800 && model.bones.size() <= hard.maxBones && model.helpers.size() <= hard.maxHelpers
        && MdxNodeCount(model) <= hard.maxNodes && model.pivots.size() <= hard.maxPivots && model.sequences.size() <= hard.maxSequences
        && model.globalSequences.size() <= hard.maxGlobalSequences, Code::PoseModelUnsupported);
    Require(rigidStaticPreview || model.geosets.empty() || model.skinningTopologyComplete, Code::SkinTopologyIncomplete);
    Require(model.geosets.size() <= hard.maxGeosets, Code::SkinModelUnsupported);
    const auto nodeCount = MdxNodeCount(model);
    Require(rigidStaticPreview || nodeCount != 0, Code::PoseNodeUnavailable);
    Budget budget{maxBytes};
    budget.Charge(1, sizeof(ModelData));
    if (rigidStaticPreview) {
        budget.Charge(model.rigidStaticPreviewSource->Bytes().size(), 1);
        budget.Charge(model.geosets.size(), sizeof(MdxRigidStaticGeosetOrigin));
    }
    budget.Charge(source.virtualPath.size() + source.sourceLayer.size() + source.contentDigest.size() + 3, 1);
    budget.Charge(nodeCount, sizeof(CompiledNode) + 7 * sizeof(std::uint32_t) + sizeof(const MdxNode*));
    budget.Charge(model.sequences.size(), sizeof(Sequence));
    budget.Charge(model.geosets.size(), sizeof(GeosetLayout) + sizeof(Alpha) + sizeof(ColorChannel) + sizeof(GeosetColor));
    std::uint64_t totalKeys = 0;
    const auto chargeNode = [&](const MdxNode& node) {
        const auto chargeTrack = [&](const auto& t) {
            if (!t) return;
            const auto count = t->keys.size();
            Require(count <= hard.maxTrackKeys && count <= hard.maxTotalBoneTrackKeys - totalKeys, Code::PoseLimit);
            totalKeys += count;
            // Key<4> 是各通道的保守上界，包含预编译的数值/控制点标记。
            budget.Charge(count, sizeof(Key<4>));
        };
        chargeTrack(node.translation); chargeTrack(node.rotation); chargeTrack(node.scaling);
    };
    ForEachMdxNode(model, chargeNode);
    std::uint64_t totalVertices = 0, totalGroups = 0, totalMembers = 0;
    for (const auto& g : model.geosets) {
        Require(!g.positions.empty() && g.positions.size() <= hard.maxVerticesPerGeoset
            && g.normals.size() == g.positions.size() && g.vertexGroups.size() == g.positions.size()
            && (rigidStaticPreview || !g.matrixGroupSizes.empty()) && g.matrixGroupSizes.size() <= hard.maxMatrixGroupsPerGeoset
            && g.matrixIndices.size() <= hard.maxMatrixIndicesPerGeoset && g.uninterpretedTailBytes == 0,
            Code::SkinGeometryUnsupported);
        const auto groupCount = rigidStaticPreview ? std::size_t{1} : g.matrixGroupSizes.size();
        totalVertices += g.positions.size(); totalGroups += groupCount; totalMembers += g.matrixIndices.size();
        Require(totalVertices <= hard.maxTotalVertices && totalMembers <= UINT32_MAX && totalGroups <= UINT32_MAX,
            Code::CapacityExceeded);
        budget.Charge(g.positions.size(), 2 * sizeof(Vec3) + sizeof(std::uint32_t));
        budget.Charge(groupCount, sizeof(Range32));
        budget.Charge(g.matrixIndices.size(), sizeof(std::uint32_t));
        if (g.animation) {
            // 超限轨迹会在编译的 Alpha.error 中明确拒绝，不复制无界数据。
            budget.Charge(std::min(g.animation->alphaKeys.size(), hard.maxTrackKeys), sizeof(GeoKey<1>));
            budget.Charge(std::min(g.animation->colorKeys.size(), hard.maxTrackKeys), sizeof(GeoKey<3>));
        }
    }
    auto out = std::make_shared<ModelData>();
    out->identity = identity; out->source = std::move(source);
    if (rigidStaticPreview) out->rigidStaticPreviewSource = model.rigidStaticPreviewSource;
    out->sequences.reserve(model.sequences.size());
    for (const auto& s : model.sequences) out->sequences.push_back({s.start, s.end, s.flags,
        s.end < s.start || s.flags > 1 ? Code::PoseSequenceUnsupported : Code::Ok});

    std::vector<const MdxNode*> nodes;
    nodes.reserve(nodeCount);
    ForEachMdxNode(model, [&](const MdxNode& n) { nodes.push_back(&n); });
    std::vector<std::uint32_t> sorted(nodeCount), parent(nodeCount, NoIndex), toTopo(nodeCount, NoIndex), path;
    std::vector<std::uint8_t> state(nodeCount, 0);
    path.reserve(nodeCount);
    std::iota(sorted.begin(), sorted.end(), 0u);
    std::sort(sorted.begin(), sorted.end(), [&](auto a, auto b) { return nodes[a]->objectId < nodes[b]->objectId; });
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const auto id = nodes[sorted[i]]->objectId;
        Require(id != NoIndex && (i == 0 || nodes[sorted[i - 1]]->objectId != id), Code::PoseNodeInvalid);
    }
    const auto findNode = [&](std::uint32_t id) -> std::uint32_t {
        const auto found = std::lower_bound(sorted.begin(), sorted.end(), id,
            [&](auto index, auto key) { return nodes[index]->objectId < key; });
        return found != sorted.end() && nodes[*found]->objectId == id ? *found : NoIndex;
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = *nodes[i];
        Require(n.objectId < model.pivots.size(), Code::PosePivotUnavailable);
        Require(Finite(model.pivots[n.objectId]), Code::PoseNumberInvalid);
        Require((n.flags & ~(MdxNodeNonTransformFlags(n.kind) | 0xFFu)) == 0, Code::PoseNodeFlagsUnsupported);
        Require(n.parentId >= -1, Code::PoseParentInvalid);
        if (n.parentId != -1) {
            parent[i] = findNode(static_cast<std::uint32_t>(n.parentId));
            Require(parent[i] != NoIndex, Code::PoseParentUnavailable);
        }
    }
    out->nodes.reserve(nodeCount);
    for (std::uint32_t start = 0; start < nodes.size(); ++start) {
        auto current = start;
        while (current != NoIndex && state[current] != 2) {
            Require(state[current] != 1, Code::PoseParentInvalid);
            state[current] = 1; path.push_back(current); current = parent[current];
        }
        while (!path.empty()) {
            const auto index = path.back(); path.pop_back();
            const auto& n = *nodes[index];
            CompiledNode compiled;
            compiled.layout = {n.objectId, n.parentId, parent[index] == NoIndex ? NoIndex : toTopo[parent[index]], n.flags};
            compiled.pivot = model.pivots[n.objectId];
            compiled.translation = CompileTrack(n.translation, model.globalSequences, model.sequences, budget);
            compiled.rotation = CompileTrack(n.rotation, model.globalSequences, model.sequences, budget);
            compiled.scaling = CompileTrack(n.scaling, model.globalSequences, model.sequences, budget);
            toTopo[index] = static_cast<std::uint32_t>(out->nodes.size());
            out->nodes.push_back(std::move(compiled)); state[index] = 2;
        }
    }
    out->geosets.reserve(model.geosets.size()); out->alphas.reserve(model.geosets.size());
    out->colors.reserve(model.geosets.size()); out->colorTracks.reserve(model.geosets.size());
    out->groups.reserve(static_cast<std::size_t>(totalGroups));
    out->groupMembers.reserve(static_cast<std::size_t>(totalMembers));
    out->vertexGroups.reserve(static_cast<std::size_t>(totalVertices));
    out->positions.reserve(static_cast<std::size_t>(totalVertices)); out->normals.reserve(static_cast<std::size_t>(totalVertices));
    for (const auto& g : model.geosets) {
        const GeosetLayout layout{{static_cast<std::uint32_t>(out->groups.size()),
            rigidStaticPreview ? 1u : static_cast<std::uint32_t>(g.matrixGroupSizes.size())},
            {static_cast<std::uint32_t>(out->positions.size()), static_cast<std::uint32_t>(g.positions.size())}};
        std::size_t offset = 0;
        if (rigidStaticPreview) out->groups.push_back({0, 0}); // 明确来源的局部 identity；不虚构骨骼成员。
        for (const auto count : g.matrixGroupSizes) {
            Require(count != 0 && count <= g.matrixIndices.size() - offset, Code::SkinGeometryUnsupported);
            out->groups.push_back({static_cast<std::uint32_t>(out->groupMembers.size()), count});
            for (std::uint32_t member = 0; member < count; ++member) {
                const auto index = findNode(g.matrixIndices[offset + member]);
                Require(index != NoIndex, Code::PoseNodeUnavailable);
                out->groupMembers.push_back(toTopo[index]);
            }
            offset += count;
        }
        Require(offset == g.matrixIndices.size(), Code::SkinGeometryUnsupported);
        for (const auto group : g.vertexGroups) {
            Require(rigidStaticPreview ? group == 0 : group < g.matrixGroupSizes.size(), Code::SkinGeometryUnsupported);
            out->vertexGroups.push_back(group);
        }
        // 原始顶点/法线是静态输入，加载时查完；变换后溢出或退化仍须逐帧报告。
        for (std::size_t i = 0; i < g.positions.size(); ++i) {
            Require(Finite(g.positions[i]) && Finite(g.normals[i]), Code::PoseNumberInvalid);
        }
        out->positions.insert(out->positions.end(), g.positions.begin(), g.positions.end());
        out->normals.insert(out->normals.end(), g.normals.begin(), g.normals.end());
        out->geosets.push_back(layout); out->alphas.push_back(CompileGeosetAlpha(g.animation,model.globalSequences,model.sequences));
        out->colorTracks.push_back(CompileGeosetRgb(g.animation,model.globalSequences,model.sequences));
        budget.Charge(out->alphas.back().ranges.size()+out->colorTracks.back().ranges.size(),sizeof(Range32)+sizeof(std::uint8_t));
        out->colors.push_back(g.animation ? CompileGeosetColor(g.animation->flags,
            g.animation->staticColor, g.animation->hasUninterpretedData) : GeosetColor{});
    }
    Require(model.materials.size() <= hard.maxMaterials, Code::MaterialAlphaLimit);
    std::size_t materialLayerCount = 0;
    for (const auto& material : model.materials) {
        Require(material.layers.size() <= hard.maxLayersPerMaterial, Code::MaterialAlphaLimit);
        materialLayerCount += material.layers.size();
    }
    budget.Charge(materialLayerCount, sizeof(MaterialLayerLayout) + sizeof(CompiledMaterialChannel));
    out->materialLayouts.reserve(materialLayerCount); out->materialAlphas.reserve(materialLayerCount);
    for (std::uint32_t m = 0; m < model.materials.size(); ++m) {
        const auto& material = model.materials[m];
        Require(material.layers.size() <= hard.maxLayersPerMaterial, Code::MaterialAlphaLimit);
        for (std::uint32_t l = 0; l < material.layers.size(); ++l) {
            const auto& layer = material.layers[l];
            // 原始键上限已在scalar编译中诊断；预算在reserve前计入实际所有者。
            budget.Charge(layer.alphaKeys.size(), sizeof(GeoKey<1>) + sizeof(MaterialKeySource));
            if (const auto bytes = MaterialAlphaDuplicateStorageBytes(layer, material.hasUninterpretedData))
                budget.Charge(1, bytes);
            const MaterialLayerLayout layout{m, l, layer.animatedAlpha ? layer.alphaSourceByteOffset : layer.staticAlphaByteOffset};
            out->materialLayouts.push_back(layout);
            out->materialAlphas.push_back(CompileMaterialAlpha(layer, material.hasUninterpretedData, layout, model.globalSequences));
            if (const auto& prepared = out->materialAlphas.back().identicalDuplicates) {
                Require(prepared->warnings.size() <= UINT32_MAX - out->materialDuplicateWarnings, Code::MaterialAlphaLimit);
                out->materialDuplicateWarnings += static_cast<std::uint32_t>(prepared->warnings.size());
            }
            out->animatedMaterialAlpha |= layer.animatedAlpha;
        }
    }
    return out;
}
} // namespace

bool CompiledModel::Valid() const noexcept { return data_ != nullptr; }
AssetIdentity CompiledModel::Identity() const noexcept { return data_ ? data_->identity : AssetIdentity{}; }
const AssetSource& CompiledModel::Source() const noexcept { static const AssetSource empty; return data_ ? data_->source : empty; }
std::uint32_t CompiledModel::NodeCount() const noexcept { return data_ ? static_cast<std::uint32_t>(data_->nodes.size()) : 0; }
std::uint32_t CompiledModel::GroupCount() const noexcept { return data_ ? static_cast<std::uint32_t>(data_->groups.size()) : 0; }
std::uint32_t CompiledModel::GeosetCount() const noexcept { return data_ ? static_cast<std::uint32_t>(data_->geosets.size()) : 0; }
std::uint32_t CompiledModel::SequenceCount() const noexcept { return data_ ? static_cast<std::uint32_t>(data_->sequences.size()) : 0; }
std::uint32_t CompiledModel::MaterialLayerCount() const noexcept { return data_ ? static_cast<std::uint32_t>(data_->materialLayouts.size()) : 0; }
bool CompiledModel::HasAnimatedMaterialAlpha() const noexcept { return data_ && data_->animatedMaterialAlpha; }
std::span<const MaterialLayerLayout> CompiledModel::MaterialLayerLayouts() const noexcept {
    return data_ ? std::span<const MaterialLayerLayout>(data_->materialLayouts) : std::span<const MaterialLayerLayout>{};
}
NodeLayout CompiledModel::Node(std::uint32_t index) const noexcept {
    return data_ && index < data_->nodes.size() ? data_->nodes[index].layout : NodeLayout{};
}
std::span<const GeosetLayout> CompiledModel::Geosets() const noexcept {
    return data_ ? std::span<const GeosetLayout>(data_->geosets) : std::span<const GeosetLayout>{};
}
std::span<const std::uint32_t> CompiledModel::VertexGroups() const noexcept {
    return data_ ? std::span<const std::uint32_t>(data_->vertexGroups) : std::span<const std::uint32_t>{};
}
std::span<const GeosetColor> CompiledModel::GeosetColors() const noexcept {
    return data_ ? std::span<const GeosetColor>(data_->colors) : std::span<const GeosetColor>{};
}
bool CompiledModel::SharesStorageWith(const CompiledModel& other) const noexcept { return data_ == other.data_; }
bool CompiledModel::IsRigidStaticPreview() const noexcept { return data_ && data_->rigidStaticPreviewSource != nullptr; }

War3Map::Result<CompiledModel> CompileMdxModel(const MdxModel& model, AssetIdentity identity,
    AssetSource source, std::uint64_t maxCompiledBytes) {
    if (model.rigidStaticPreviewSource) return War3Map::Result<CompiledModel>::Failure("RenderCore.Compile",
        "MDX_RIGID_STATIC_PREVIEW_EXPLICIT_REQUIRED", "无控制器静态来源必须明确申请预览近似编译，不能作为普通骨骼模型。");
    const auto failure = [](Code code) {
        return War3Map::Result<CompiledModel>::Failure("RenderCore.Compile", CodeName(code), CodeMessage(code));
    };
    try {
        auto result = War3Map::Result<CompiledModel>::Success(ModelAccess::Make(Compile(model, identity, std::move(source), maxCompiledBytes, false)));
        for (std::size_t g = 0; g < model.geosets.size(); ++g) {
            const auto count = MdxZeroNormalCount(model.geosets[g].normals);
            if (count != 0) result.diagnostics.push_back(MdxZeroNormalWarning(g, count));
        }
        return result;
    } catch (const CompileFailure& e) { return failure(e.code); }
    catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}

War3Map::Result<CompiledModel> CompileMdxRigidStaticPreview(const MdxModel& model, AssetIdentity identity,
    AssetSource source, std::uint64_t maxCompiledBytes) {
    if (!MdxRigidStaticPreviewInternal::Qualified(model))
        return War3Map::Result<CompiledModel>::Failure("RenderCore.Compile", "MDX_RIGID_STATIC_PREVIEW_UNQUALIFIED",
            "预览近似需要密封的无控制器静态来源；当前几何、槽号、矩阵或节点资格与来源不一致。");
    const auto failure = [](Code code) {
        return War3Map::Result<CompiledModel>::Failure("RenderCore.Compile", CodeName(code), CodeMessage(code));
    };
    try {
        auto result = War3Map::Result<CompiledModel>::Success(ModelAccess::Make(
            Compile(model, identity, std::move(source), maxCompiledBytes, true)));
        const auto origins = model.rigidStaticPreviewSource->Geosets();
        for (std::size_t g = 0; g < model.geosets.size(); ++g) {
            auto warning = MdxRigidStaticPreviewInternal::Warning(origins[g], g);
            warning.origin->source = result.value->Source().virtualPath;
            result.diagnostics.push_back(std::move(warning));
            const auto count = MdxZeroNormalCount(model.geosets[g].normals);
            if (count != 0) result.diagnostics.push_back(MdxZeroNormalWarning(g, count));
        }
        return result;
    } catch (const CompileFailure& e) { return failure(e.code); }
    catch (const std::bad_alloc&) { return failure(Code::AllocationFailed); }
    catch (const std::length_error&) { return failure(Code::AllocationFailed); }
}
} // namespace War3RenderCore
