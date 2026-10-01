// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3MapCore/Render/MdxModel.h"
#include "MdxNodeRead.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>

namespace War3Map::Render {
namespace {

constexpr const char* kSubsystem = "Mdx";
constexpr std::uint32_t kClassicVersion = 800;
constexpr std::uint32_t kNoGlobalSequence = 0xFFFFFFFFu;
constexpr std::uint32_t kTriangles = 4;

using namespace MdxReadInternal;

struct GeosetAnimation {
    std::uint32_t geosetId = 0;
    MdxGeosetAnimation value;
};

// 跳过一条动画轨迹：tag、count、插值类型、全局序列、按键。valueBytes 为单个值的字节数。
void SkipTrack(Reader& reader, std::size_t valueBytes, const MdxLimits& limits, const char* what) {
    const auto count = reader.U32(what);
    const auto interpolation = reader.U32(what);
    reader.U32(what);
    if (count > limits.maxTrackKeys) Fail("MDX_LIMIT", std::string(what) + " 按键数量超过上限。");
    const auto keyBytes = 4 + valueBytes * (interpolation > 1 ? 3 : 1);
    reader.Skip(static_cast<std::size_t>(count) * keyBytes, what);
}

std::size_t TrackValueBytes(const std::string& tag) {
    // 层：KMTF 贴图 ID、KMTA 透明度；geoset 动画：KGAO 透明度、KGAC 颜色。
    if (tag == "KMTF" || tag == "KMTA" || tag == "KGAO") return 4;
    if (tag == "KGAC") return 12;
    return 0;
}

void ParseSequences(Reader reader, MdxModel& model, const MdxLimits& limits) {
    constexpr std::size_t kSequenceBytes = 132;
    if (reader.Remaining() % kSequenceBytes != 0) Fail("MDX_SIZE_INVALID", "SEQS 块大小不是 132 的整数倍。");
    const auto count = reader.Remaining() / kSequenceBytes;
    if (count > limits.maxSequences - model.sequences.size()) Fail("MDX_LIMIT", "动作数量超过上限。");
    for (std::size_t index = 0; index < count; ++index) {
        MdxSequence sequence;
        sequence.name = reader.FixedString(80, "动作名称");
        sequence.start = reader.U32("动作起点");
        sequence.end = reader.U32("动作终点");
        reader.F32("动作移动速度");
        sequence.flags = reader.U32("动作标志");
        reader.Skip(kSequenceBytes - 96, "动作其余字段");
        model.sequences.push_back(std::move(sequence));
    }
}

void ParseGlobalSequences(Reader reader, MdxModel& model, const MdxLimits& limits) {
    if (reader.Remaining() % 4 != 0) Fail("MDX_SIZE_INVALID", "GLBS 块大小不是 4 的整数倍。");
    const auto count = reader.Remaining() / 4;
    if (count > limits.maxGlobalSequences - model.globalSequences.size()) Fail("MDX_LIMIT", "全局序列数量超过上限。");
    for (std::size_t i = 0; i < count; ++i) model.globalSequences.push_back(reader.U32("全局序列周期"));
}

void ParseTextures(Reader reader, MdxModel& model, const MdxLimits& limits) {
    constexpr std::size_t kTextureBytes = 268;
    if (reader.Remaining() % kTextureBytes != 0) Fail("MDX_SIZE_INVALID", "TEXS 块大小不是 268 的整数倍。");
    const auto count = reader.Remaining() / kTextureBytes;
    if (count > limits.maxTextures) Fail("MDX_LIMIT", "贴图数量超过上限。");
    for (std::size_t index = 0; index < count; ++index) {
        MdxTexture texture;
        texture.replaceableId = reader.U32("可替换贴图 ID");
        texture.path = reader.FixedString(260, "贴图路径");
        texture.flags = reader.U32("贴图标志");
        model.textures.push_back(std::move(texture));
    }
}

void ParseMaterials(Reader reader, MdxModel& model, const MdxLimits& limits) {
    while (reader.Remaining() != 0) {
        const auto materialStart = reader.Position();
        const auto materialBytes = reader.U32("材质大小");
        if (materialBytes < 20 || materialBytes > reader.Remaining() + 4) Fail("MDX_SIZE_INVALID", "材质大小无效。");
        if (model.materials.size() >= limits.maxMaterials) Fail("MDX_LIMIT", "材质数量超过上限。");
        const auto materialEnd = materialStart + materialBytes;
        MdxMaterial material;
        material.priorityPlane = reader.I32("材质优先级");
        material.flags = reader.U32("材质标志");
        reader.ExpectTag("LAYS", "材质层");
        const auto layerCount = reader.U32("材质层数量");
        if (layerCount > limits.maxLayersPerMaterial) Fail("MDX_LIMIT", "材质层数量超过上限。");
        for (std::uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            const auto layerStart = reader.Position();
            const auto layerBytes = reader.U32("材质层大小");
            if (layerBytes < 28 || layerStart + layerBytes > materialEnd) Fail("MDX_SIZE_INVALID", "材质层大小无效。");
            MdxLayer layer;
            const auto filter = reader.U32("过滤模式");
            if (filter > static_cast<std::uint32_t>(MdxFilterMode::Modulate2x)) Fail("MDX_VALUE_INVALID", "未知的层过滤模式。");
            layer.filterMode = static_cast<MdxFilterMode>(filter);
            layer.shadingFlags = reader.U32("着色标志");
            layer.textureId = reader.U32("层贴图 ID");
            layer.textureAnimationId = reader.U32("贴图动画 ID");
            layer.coordId = reader.U32("UV 组");
            layer.alpha = reader.F32("层透明度");
            // 层内其余为动画轨迹；只记录是否有贴图 ID 轨迹。
            while (reader.Position() + 16 <= layerStart + layerBytes) {
                const auto tag = reader.PeekTag();
                const auto valueBytes = TrackValueBytes(tag);
                if (valueBytes == 0) break;
                reader.Tag("层轨迹");
                layer.animatedTexture |= tag == "KMTF";
                layer.animatedAlpha |= tag == "KMTA";
                layer.hasUninterpretedData |= tag != "KMTF" && tag != "KMTA";
                SkipTrack(reader, valueBytes, limits, "层轨迹");
            }
            if (reader.Position() > layerStart + layerBytes) Fail("MDX_SIZE_INVALID", "材质层轨迹越过层边界。");
            layer.hasUninterpretedData |= reader.Position() != layerStart + layerBytes;
            reader.Skip(layerStart + layerBytes - reader.Position(), "材质层其余字段");
            material.layers.push_back(layer);
        }
        if (reader.Position() > materialEnd) Fail("MDX_SIZE_INVALID", "材质层越过材质边界。");
        material.hasUninterpretedData = reader.Position() != materialEnd;
        reader.Skip(materialEnd - reader.Position(), "材质其余字段");
        model.materials.push_back(std::move(material));
    }
}

void ParseGeosets(Reader reader, MdxModel& model, const MdxLimits& limits, std::size_t& totalVertices, std::size_t& totalIndices) {
    while (reader.Remaining() != 0) {
        const auto start = reader.Position();
        const auto bytes = reader.U32("geoset 大小");
        if (bytes < 4 || bytes > reader.Remaining() + 4) Fail("MDX_SIZE_INVALID", "geoset 大小无效。");
        if (model.geosets.size() >= limits.maxGeosets) Fail("MDX_LIMIT", "geoset 数量超过上限。");
        const auto end = start + bytes;
        MdxGeoset geoset;
        reader.ExpectTag("VRTX", "顶点");
        const auto vertexCount = reader.Count(12, limits.maxVerticesPerGeoset, "顶点");
        totalVertices += vertexCount;
        if (totalVertices > limits.maxTotalVertices) Fail("MDX_LIMIT", "模型顶点总数超过上限。");
        geoset.positions.resize(vertexCount);
        for (auto& position : geoset.positions)
            for (auto& axis : position) axis = reader.F32("顶点坐标");
        reader.ExpectTag("NRMS", "法线");
        const auto normalCount = reader.Count(12, limits.maxVerticesPerGeoset, "法线");
        if (normalCount != vertexCount) Fail("MDX_VALUE_INVALID", "法线数量与顶点数量不一致。");
        geoset.normals.resize(normalCount);
        for (auto& normal : geoset.normals)
            for (auto& axis : normal) axis = reader.F32("法线");
        reader.ExpectTag("PTYP", "图元类型");
        const auto groupCount = reader.Count(4, limits.maxTotalIndices, "图元类型");
        std::vector<std::uint32_t> types(groupCount);
        for (auto& type : types) type = reader.U32("图元类型");
        reader.ExpectTag("PCNT", "图元计数");
        if (reader.Count(4, limits.maxTotalIndices, "图元计数") != groupCount) Fail("MDX_VALUE_INVALID", "图元类型与计数组数不一致。");
        std::vector<std::uint32_t> counts(groupCount);
        for (auto& count : counts) count = reader.U32("图元计数");
        reader.ExpectTag("PVTX", "图元索引");
        const auto indexCount = reader.Count(2, limits.maxTotalIndices, "图元索引");
        std::uint64_t declared = 0;
        for (const auto count : counts) declared += count;
        if (declared != indexCount) Fail("MDX_VALUE_INVALID", "图元计数之和与索引数量不一致。");
        for (std::size_t group = 0; group < groupCount; ++group) {
            const bool triangles = types[group] == kTriangles && counts[group] % 3 == 0;
            if (!triangles) ++geoset.skippedPrimitiveGroups;
            for (std::uint32_t i = 0; i < counts[group]; ++i) {
                const auto index = reader.U16("图元索引");
                if (index >= vertexCount) Fail("MDX_VALUE_INVALID", "图元索引超出顶点范围。");
                if (triangles) geoset.indices.push_back(index);
            }
        }
        totalIndices += geoset.indices.size();
        if (totalIndices > limits.maxTotalIndices) Fail("MDX_LIMIT", "模型索引总数超过上限。");
        reader.ExpectTag("GNDX", "顶点组");
        const auto vertexGroupCount = reader.Count(1, limits.maxVerticesPerGeoset, "顶点组");
        if (vertexGroupCount != vertexCount) Fail("MDX_VALUE_INVALID", "GNDX 槽号数量与顶点数量不一致。");
        geoset.vertexGroups.reserve(vertexGroupCount);
        for (std::uint32_t i = 0; i < vertexGroupCount; ++i)
            geoset.vertexGroups.push_back(reader.U8("顶点组槽号"));
        reader.ExpectTag("MTGC", "矩阵组");
        const auto matrixGroupCount = reader.Count(4, limits.maxMatrixGroupsPerGeoset, "矩阵组");
        if (vertexCount != 0 && matrixGroupCount == 0) Fail("MDX_VALUE_INVALID", "有顶点的 geoset 缺少矩阵组。");
        std::uint64_t matrixCount = 0;
        geoset.matrixGroupSizes.reserve(matrixGroupCount);
        for (std::uint32_t i = 0; i < matrixGroupCount; ++i) {
            const auto count = reader.U32("矩阵组大小");
            if (count == 0) Fail("MDX_VALUE_INVALID", "矩阵组不可为空。");
            matrixCount += count;
            if (matrixCount > limits.maxMatrixIndicesPerGeoset) Fail("MDX_LIMIT", "矩阵索引数量超过上限。");
            geoset.matrixGroupSizes.push_back(count);
        }
        for (const auto slot : geoset.vertexGroups)
            if (slot >= matrixGroupCount) Fail("MDX_REFERENCE_INVALID", "GNDX 槽号超出 MTGC 组数。");
        reader.ExpectTag("MATS", "矩阵索引");
        const auto matrixIndexCount = reader.Count(4, limits.maxMatrixIndicesPerGeoset, "矩阵索引");
        if (matrixCount != matrixIndexCount) Fail("MDX_VALUE_INVALID", "MTGC 大小之和与 MATS 索引数不一致。");
        geoset.matrixIndices.reserve(matrixIndexCount);
        for (std::uint32_t i = 0; i < matrixIndexCount; ++i)
            geoset.matrixIndices.push_back(reader.U32("矩阵节点 ID"));
        geoset.materialId = reader.U32("geoset 材质");
        geoset.selectionGroup = reader.U32("选择组");
        geoset.selectionFlags = reader.U32("选择标志");
        reader.Skip(28, "geoset 包围盒");
        reader.Skip(static_cast<std::size_t>(reader.Count(28, limits.maxSequences, "动作包围盒")) * 28, "动作包围盒");
        reader.ExpectTag("UVAS", "UV 组");
        const auto uvSets = reader.Count(0, 16, "UV 组");
        for (std::uint32_t set = 0; set < uvSets; ++set) {
            reader.ExpectTag("UVBS", "UV");
            const auto uvCount = reader.Count(8, limits.maxVerticesPerGeoset, "UV");
            if (uvCount != vertexCount) Fail("MDX_VALUE_INVALID", "UV 数量与顶点数量不一致。");
            if (set == 0) {
                geoset.uvs.resize(uvCount);
                for (auto& uv : geoset.uvs) { uv[0] = reader.F32("UV"); uv[1] = reader.F32("UV"); }
            } else {
                reader.Skip(static_cast<std::size_t>(uvCount) * 8, "UV");
            }
        }
        if (geoset.uvs.empty()) geoset.uvs.assign(vertexCount, {0.0f, 0.0f});
        if (reader.Position() > end) Fail("MDX_SIZE_INVALID", "geoset 内容越过其边界。");
        geoset.uninterpretedTailBytes = end - reader.Position();
        reader.Skip(end - reader.Position(), "geoset 其余字段");
        model.geosets.push_back(std::move(geoset));
    }
}

#include "MdxGeosetRead.inl"

std::string Lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// “站立”动作：第一个可用的 stand；没有时取第一个可用动作，坏序列不参与默认姿势。
std::optional<MdxSequence> StandSequence(const MdxModel& model) {
    for (const auto& sequence : model.sequences)
        if (sequence.Usable() && Lower(sequence.name).rfind("stand", 0) == 0) return sequence;
    for (const auto& sequence : model.sequences)
        if (sequence.Usable()) return sequence;
    return std::nullopt;
}

float StandAlpha(const MdxGeosetAnimation& animation, const std::optional<MdxSequence>& stand) {
    if (!animation.hasAlphaTrack || animation.alphaKeys.empty()) return animation.staticAlpha;
    // 全局序列与动作无关：取第一个按键。
    if (animation.globalSequence != kNoGlobalSequence || !stand) return animation.alphaKeys.front().value;
    // 只看落在该动作区间内的按键；没有按键时取静态值。起点之前（含）最后一个按键优先，否则取区间内第一个。
    const MdxGeosetAlphaKey* atStart = nullptr;
    const MdxGeosetAlphaKey* first = nullptr;
    for (const auto& key : animation.alphaKeys) {
        if (key.frame < stand->start || key.frame > stand->end) continue;
        if (first == nullptr) first = &key;
        if (key.frame == stand->start) atStart = &key;
    }
    if (atStart != nullptr) return atStart->value;
    if (first != nullptr) return first->value;
    return animation.staticAlpha;
}

// 读取一条轨迹的按键：每值 width 个浮点；插值 > 1（Hermite/Bezier）时每键另有入/出切线，读取后丢弃。
template <class Key, class Assign>
std::vector<Key> ReadTrackKeys(Reader& reader, std::size_t width, const MdxLimits& limits, const char* what, Assign assign) {
    const auto count = reader.U32(what);
    const auto interpolation = reader.U32(what);
    reader.U32(what); // 全局序列
    if (count > limits.maxTrackKeys) Fail("MDX_LIMIT", std::string(what) + " 按键数量超过上限。");
    if (interpolation > 3) Fail("MDX_VALUE_INVALID", std::string(what) + " 插值类型未知。");
    const auto tangents = interpolation > 1;
    reader.Need(static_cast<std::size_t>(count) * (4 + 4 * width * (tangents ? 3 : 1)), what);
    std::vector<Key> keys;
    keys.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        Key key;
        key.frame = reader.I32(what);
        std::array<float, 4> value{};
        for (std::size_t component = 0; component < width; ++component) value[component] = reader.F32(what);
        if (tangents) reader.Skip(8 * width, what);
        assign(key, value);
        keys.push_back(key);
    }
    return keys;
}

std::array<float, 3> Rgb(const std::array<float, 4>& bgr) { return {bgr[2], bgr[1], bgr[0]}; }

struct RotationKey {
    std::int32_t frame = 0;
    std::array<float, 4> quaternion{};
};

void ParseLights(Reader reader, MdxModel& model, const MdxLimits& limits, std::size_t& totalKeys) {
    const auto color = [](MdxColorKey& key, const std::array<float, 4>& value) { key.rgb = Rgb(value); };
    const auto scalar = [](MdxScalarKey& key, const std::array<float, 4>& value) { key.value = value[0]; };
    while (reader.Remaining() != 0) {
        const auto lightStart = reader.Position();
        const auto lightBytes = reader.U32("光源大小");
        if (lightBytes < 4 + 96 + 44 || lightBytes > reader.Remaining() + 4) Fail("MDX_SIZE_INVALID", "光源大小无效。");
        if (model.lights.size() >= limits.maxMaterials) Fail("MDX_LIMIT", "光源数量超过上限。");
        const auto lightEnd = lightStart + lightBytes;
        MdxLight light;
        CheckNodeBudget(model, limits);
        auto nodeReader = reader.Until(lightEnd, "光源对象");
        auto node = ParseNode(nodeReader, limits, totalKeys, 44, "光源节点");
        reader.Skip(nodeReader.Position() - reader.Position(), "光源节点");
        node.kind = MdxNodeKind::Light;
        light.name = node.name;
        if (node.rotation && !node.rotation->keys.empty()) light.rotation = node.rotation->keys.front().value;
        model.otherNodes.push_back(std::move(node));
        light.type = reader.U32("光源类型");
        if (light.type > 2) Fail("MDX_VALUE_INVALID", "未知的光源类型。");
        reader.Skip(8, "衰减范围");
        light.color = Rgb({reader.F32("光源颜色"), reader.F32("光源颜色"), reader.F32("光源颜色"), 0});
        light.intensity = reader.F32("光源强度");
        light.ambientColor = Rgb({reader.F32("环境光颜色"), reader.F32("环境光颜色"), reader.F32("环境光颜色"), 0});
        light.ambientIntensity = reader.F32("环境光强度");
        while (reader.Position() + 16 <= lightEnd) {
            const auto tag = reader.Tag("光源轨迹");
            if (tag == "KLAC") light.colorKeys = ReadTrackKeys<MdxColorKey>(reader, 3, limits, "光源颜色轨迹", color);
            else if (tag == "KLBC") light.ambientColorKeys = ReadTrackKeys<MdxColorKey>(reader, 3, limits, "环境光颜色轨迹", color);
            else if (tag == "KLAI") light.intensityKeys = ReadTrackKeys<MdxScalarKey>(reader, 1, limits, "光源强度轨迹", scalar);
            else if (tag == "KLBI") light.ambientIntensityKeys = ReadTrackKeys<MdxScalarKey>(reader, 1, limits, "环境光强度轨迹", scalar);
            else if (tag == "KLAS" || tag == "KLAE" || tag == "KLAV") SkipTrack(reader, 4, limits, "光源轨迹");
            else Fail("MDX_TAG_MISMATCH", "光源含未知轨迹 " + tag + "。");
        }
        if (reader.Position() != lightEnd) Fail("MDX_SIZE_INVALID", "光源轨迹未对齐光源边界。");
        model.lights.push_back(std::move(light));
    }
}

} // namespace

Result<MdxModel> ParseMdx(std::span<const std::byte> bytes, const MdxLimits& limits) {
    try {
        constexpr MdxLimits hard{};
        if (limits.maxInputBytes > hard.maxInputBytes || limits.maxGeosets > hard.maxGeosets
            || limits.maxVerticesPerGeoset > hard.maxVerticesPerGeoset || limits.maxTotalVertices > hard.maxTotalVertices
            || limits.maxTotalIndices > hard.maxTotalIndices || limits.maxTextures > hard.maxTextures
            || limits.maxMaterials > hard.maxMaterials || limits.maxLayersPerMaterial > hard.maxLayersPerMaterial
            || limits.maxSequences > hard.maxSequences || limits.maxTrackKeys > hard.maxTrackKeys
            || limits.maxGlobalSequences > hard.maxGlobalSequences || limits.maxBones > hard.maxBones
            || limits.maxHelpers > hard.maxHelpers || limits.maxPivots > hard.maxPivots
            || limits.maxMatrixGroupsPerGeoset > hard.maxMatrixGroupsPerGeoset
            || limits.maxMatrixIndicesPerGeoset > hard.maxMatrixIndicesPerGeoset
            || limits.maxTotalBoneTrackKeys > hard.maxTotalBoneTrackKeys || limits.maxNodes > hard.maxNodes)
            Fail("MDX_LIMITS", "调用方不能放宽 MDX 读取上限。");
        if (bytes.size() > limits.maxInputBytes) Fail("MDX_LIMIT", "MDX 文件超过大小上限。");
        Reader file(bytes, 0, bytes.size());
        if (file.Tag("文件头") != "MDLX") Fail("MDX_MAGIC", "不是 MDX 文件（缺少 MDLX 文件头）。");
        MdxModel model;
        std::vector<GeosetAnimation> animations;
        std::size_t totalVertices = 0, totalIndices = 0, totalBoneKeys = 0;
        bool seenPivots = false;
        while (file.Remaining() != 0) {
            const auto tag = file.Tag("块标记");
            const auto size = file.U32("块大小");
            file.Need(size, "块内容");
            const Reader chunk(bytes, file.Position(), file.Position() + size);
            if (tag == "VERS") {
                auto version = chunk;
                model.version = version.U32("版本");
                if (model.version != kClassicVersion)
                    Fail("MDX_VERSION_UNSUPPORTED", "只支持经典 MDX 版本 800（实际 " + std::to_string(model.version) + "）。");
            } else if (tag == "MODL") {
                auto header = chunk;
                model.name = header.FixedString(80, "模型名称");
                header.Skip(260, "动画文件名");
                model.boundsRadius = header.F32("包围半径");
                for (auto& axis : model.minimum) axis = header.F32("包围盒");
                for (auto& axis : model.maximum) axis = header.F32("包围盒");
            } else if (tag == "SEQS") {
                ParseSequences(chunk, model, limits);
            } else if (tag == "GLBS") {
                ParseGlobalSequences(chunk, model, limits);
            } else if (tag == "TEXS") {
                ParseTextures(chunk, model, limits);
            } else if (tag == "MTLS") {
                ParseMaterials(chunk, model, limits);
            } else if (tag == "GEOS") {
                if (model.version != kClassicVersion) Fail("MDX_VERSION_UNSUPPORTED", "GEOS 之前缺少版本 800 的 VERS 块。");
                ParseGeosets(chunk, model, limits, totalVertices, totalIndices);
            } else if (tag == "GEOA") {
                auto parsed = ParseGeosetAnimations(chunk, limits);
                if (parsed.size() > limits.maxGeosets - animations.size())
                    Fail("MDX_LIMIT", "geoset 动画总数超过上限。");
                animations.insert(animations.end(), std::make_move_iterator(parsed.begin()),
                    std::make_move_iterator(parsed.end()));
            } else if (tag == "LITE") {
                ParseLights(chunk, model, limits, totalBoneKeys);
            } else if (tag == "BONE") {
                if (model.version != kClassicVersion) Fail("MDX_VERSION_UNSUPPORTED", "BONE 之前缺少版本 800 的 VERS 块。");
                ParseBones(chunk, model, limits, totalBoneKeys);
            } else if (tag == "HELP") {
                if (model.version != kClassicVersion) Fail("MDX_VERSION_UNSUPPORTED", "HELP 之前缺少版本 800 的 VERS 块。");
                ParseHelpers(chunk, model, limits, totalBoneKeys);
            } else if (tag == "ATCH" || tag == "PREM" || tag == "PRE2" || tag == "RIBB"
                || tag == "EVTS" || tag == "CLID") {
                if (model.version != kClassicVersion) Fail("MDX_VERSION_UNSUPPORTED", "公共节点之前缺少版本 800。");
                if (tag == "ATCH") ParseSizedNodes(chunk, model, limits, totalBoneKeys, MdxNodeKind::Attachment, 264, "ATCH");
                else if (tag == "PREM") ParseSizedNodes(chunk, model, limits, totalBoneKeys, MdxNodeKind::ParticleEmitter, 284, "PREM");
                else if (tag == "PRE2") ParseSizedNodes(chunk, model, limits, totalBoneKeys, MdxNodeKind::ParticleEmitter2, 171, "PRE2");
                else if (tag == "RIBB") ParseSizedNodes(chunk, model, limits, totalBoneKeys, MdxNodeKind::Ribbon, 52, "RIBB");
                else if (tag == "EVTS") ParseEvents(chunk, model, limits, totalBoneKeys);
                else ParseCollisions(chunk, model, limits, totalBoneKeys);
            } else if (tag == "PIVT") {
                if (seenPivots) Fail("MDX_VALUE_INVALID", "MDX 含重复 PIVT 块。");
                ParsePivots(chunk, model, limits);
                seenPivots = true;
            } else {
                ++model.skippedChunks;
            }
            file.Skip(size, "块内容");
        }
        if (model.version != kClassicVersion) Fail("MDX_VERSION_UNSUPPORTED", "缺少 VERS 块。");
        for (const auto& geoset : model.geosets)
            if (geoset.materialId >= model.materials.size()) Fail("MDX_REFERENCE_INVALID", "geoset 引用了不存在的材质。");
        for (const auto& material : model.materials)
            for (const auto& layer : material.layers)
                if (layer.textureId >= model.textures.size()) Fail("MDX_REFERENCE_INVALID", "材质层引用了不存在的贴图。");
        const auto checkGlobalSequence = [&](const auto& track) {
            return !track || track->globalSequence == kNoGlobalSequence
                || track->globalSequence < model.globalSequences.size();
        };
        const auto checkTracks = [&](const MdxNode& node) {
            if (!checkGlobalSequence(node.translation) || !checkGlobalSequence(node.rotation)
                || !checkGlobalSequence(node.scaling))
                Fail("MDX_REFERENCE_INVALID", "节点轨迹引用了不存在的全局序列。");
        };
        ForEachMdxNode(model, checkTracks);
        model.skinningTopologyComplete = true;
        const auto incomplete = [&](std::string reason) {
            if (model.skinningTopologyComplete) model.skinningTopologyIssue = std::move(reason);
            model.skinningTopologyComplete = false;
        };
        std::map<std::uint32_t, const MdxNode*> nodes;
        const auto registerNode = [&](const MdxNode& node) {
            if (node.objectId == UINT32_MAX || !nodes.emplace(node.objectId, &node).second)
                Fail("MDX_REFERENCE_INVALID", "公共节点对象 ID 重复或为无效哨兵。");
            if (node.parentId < -1) Fail("MDX_REFERENCE_INVALID", "节点父 ID 不是有效对象或根哨兵。");
            if (node.objectId >= model.pivots.size()) incomplete("节点 objectId 没有对应 PIVT 轴心。");
        };
        ForEachMdxNode(model, registerNode);
        // 已识别的节点父链不能成环；引用其它尚未解析的节点种类时保留静态几何，拒绝宣布可蒙皮。
        std::map<std::uint32_t, std::uint8_t> visit;
        for (const auto& [id, node] : nodes) {
            std::vector<std::uint32_t> path;
            auto* current = node;
            while (current && visit[current->objectId] != 2) {
                if (visit[current->objectId] == 1) Fail("MDX_REFERENCE_INVALID", "节点父链形成环。");
                visit[current->objectId] = 1;
                path.push_back(current->objectId);
                if (current->parentId == -1) break;
                const auto parent = nodes.find(static_cast<std::uint32_t>(current->parentId));
                if (parent == nodes.end()) {
                    incomplete("节点父 ID 指向未解析的节点种类，不能推测姿态。");
                    break;
                }
                current = parent->second;
            }
            for (const auto traversed : path) visit[traversed] = 2;
        }
        for (const auto& geoset : model.geosets) {
            if (geoset.uninterpretedTailBytes != 0)
                incomplete("GEOS 含未解释的尾部数据，不能推测蒙皮语义。");
            for (const auto objectId : geoset.matrixIndices)
                if (!nodes.contains(objectId)) incomplete("MATS 引用了未解析的节点对象 ID。");
        }
        const auto stand = StandSequence(model);
        for (const auto& animation : animations) {
            if (animation.geosetId >= model.geosets.size()) Fail("MDX_REFERENCE_INVALID", "geoset 动画引用了不存在的 geoset。");
            auto& geoset = model.geosets[animation.geosetId];
            const bool duplicate = geoset.animation.has_value();
            geoset.visibleInStand = StandAlpha(animation.value, stand) > 0.0f;
            geoset.animation = animation.value;
            if (duplicate) geoset.animation->hasUninterpretedData = true;
        }
        auto result = Result<MdxModel>::Success(std::move(model));
        for (std::size_t index = 0; index < result.value->sequences.size(); ++index) {
            const auto& sequence = result.value->sequences[index];
            if (!sequence.Usable()) result.diagnostics.push_back({DiagnosticSeverity::Warning, kSubsystem,
                "MDX_SEQUENCE_UNAVAILABLE", "sequence=" + std::to_string(index) + " name=" + sequence.name
                    + " start=" + std::to_string(sequence.start) + " end=" + std::to_string(sequence.end)
                    + " flags=" + std::to_string(sequence.flags) + "：动作不可用，其余动作保留。", std::nullopt});
        }
        return result;
    } catch (const MdxError& error) {
        return Result<MdxModel>::Failure(kSubsystem, error.code, error.what());
    } catch (const std::bad_alloc&) {
        return Result<MdxModel>::Failure(kSubsystem, "MDX_ALLOCATION", "MDX 读取分配失败。");
    } catch (const std::length_error&) {
        return Result<MdxModel>::Failure(kSubsystem, "MDX_ALLOCATION", "MDX 读取超出容器容量。");
    }
}

#include "MdxGeosetSample.inl"

std::string MdxModelFile(std::string_view modelField, std::uint32_t variation, std::uint32_t variationCount) {
    std::string base(modelField);
    const auto lower = Lower(base);
    if (lower.size() >= 4 && (lower.ends_with(".mdl") || lower.ends_with(".mdx"))) base.resize(base.size() - 4);
    if (variationCount > 1) base += std::to_string(variation);
    return base + ".mdx";
}

} // namespace War3Map::Render
