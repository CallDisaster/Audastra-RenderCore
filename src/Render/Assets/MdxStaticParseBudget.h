// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

// 包含于MdxModel.cpp匿名命名空间。复用Reader边界，不构造第二套模型。
// 第一遍只有固定大小状态；所有模型/变长临时存储在第二遍分配之前收费。
struct StaticTopologyEntry {
    const MdxNode* node = nullptr;
    std::uint8_t state = 0;
};

struct StaticParsePlan {
    std::size_t sequences = 0, textures = 0, materials = 0, geosets = 0;
    std::size_t bones = 0, helpers = 0, pivots = 0, animations = 0;
    std::size_t ownedBytes = sizeof(MdxModel), geosetScratch = 0, peakBytes = 0;
    std::size_t maximumBytes = 0;

    void Charge(std::size_t count, std::size_t stride) {
        if (ownedBytes > maximumBytes || count > (maximumBytes - ownedBytes) / stride)
            Fail("MDX_STATIC_PARSE_BUDGET", "静态MDX有效存储超过调用方解析预算。");
        ownedBytes += count * stride;
    }
    void Count(std::size_t& target, std::size_t count, std::size_t maximum, std::size_t stride) {
        if (target > maximum || count > maximum - target) Fail("MDX_LIMIT", "静态MDX累计数量超过上限。");
        target += count;
        Charge(count, stride);
    }
    void String(std::string_view text) { Charge(text.size() + 1, 1); }
    void Finish() {
        // animations主表从第二遍开始即reserve；GEOA局部表/GEOS图元表/节点
        // 拓扑表不同时存在。source在ParseMdxWithPolicy返回、临时表析构后封存。
        const auto animationBytes = animations * sizeof(GeosetAnimation);
        const auto transient = animationBytes + std::max({geosetScratch, animationBytes,
            (bones + helpers) * sizeof(StaticTopologyEntry)});
        const auto source = sizeof(MdxNoControllerStaticSource)
            + (bones + helpers) * sizeof(MdxNoControllerNodeOrigin)
            + pivots * sizeof(std::array<std::uint32_t, 3>);
        const auto temporaryPeak = std::max(transient, source);
        if (ownedBytes > maximumBytes || temporaryPeak > maximumBytes - ownedBytes)
            Fail("MDX_STATIC_PARSE_BUDGET", "静态MDX临时表或来源封存超过调用方解析预算。");
        peakBytes = ownedBytes + temporaryPeak;
    }
};

void ValidateMdxLimits(const MdxLimits& limits) {
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
        Fail("MDX_LIMITS", "调用方不能放宽MDX读取上限。");
}

void StaticOnly(bool condition) {
    if (!condition) Fail("MDX_STATIC_PARSE_UNQUALIFIED", "静态预算读取不接纳控制器、缺失法线或未解释数据。");
}

void ScanStaticMaterials(Reader reader, StaticParsePlan& plan, const MdxLimits& limits) {
    while (reader.Remaining()) {
        const auto start = reader.Position();
        const auto size = reader.U32("材质大小");
        if (size < 20 || size - 4 > reader.Remaining()) Fail("MDX_SIZE_INVALID", "材质大小无效。");
        auto material = reader.Until(start + size, "材质");
        plan.Count(plan.materials, 1, limits.maxMaterials, sizeof(MdxMaterial));
        material.Skip(8, "材质静态字段"); material.ExpectTag("LAYS", "材质层");
        const auto count = material.Count(28, limits.maxLayersPerMaterial, "材质层");
        plan.Charge(count, sizeof(MdxLayer));
        for (std::size_t index = 0; index < count; ++index) {
            StaticOnly(material.U32("材质层大小") == 28);
            material.Skip(12, "层静态字段");
            StaticOnly(material.U32("贴图动画") == UINT32_MAX && material.U32("UV组") == 0);
            material.Skip(4, "层透明度");
        }
        StaticOnly(material.Remaining() == 0);
        reader.Skip(size - 4, "材质内容");
    }
}

void ScanStaticGeosets(Reader outer, StaticParsePlan& plan, const MdxLimits& limits,
    std::size_t& totalVertices, std::size_t& totalIndices) {
    while (outer.Remaining()) {
        const auto start = outer.Position();
        const auto size = outer.U32("geoset大小");
        if (size < 4 || size - 4 > outer.Remaining()) Fail("MDX_SIZE_INVALID", "geoset大小无效。");
        auto reader = outer.Until(start + size, "GEOS");
        plan.Count(plan.geosets, 1, limits.maxGeosets, sizeof(MdxGeoset));
        reader.ExpectTag("VRTX", "顶点");
        const auto vertices = reader.Count(12, limits.maxVerticesPerGeoset, "顶点");
        plan.Count(totalVertices, vertices, limits.maxTotalVertices, sizeof(std::array<float, 3>));
        reader.Skip(vertices * 12, "顶点"); reader.ExpectTag("NRMS", "法线");
        const auto normals = reader.Count(12, limits.maxVerticesPerGeoset, "法线");
        StaticOnly(normals == vertices);
        plan.Charge(normals, sizeof(std::array<float, 3>)); reader.Skip(normals * 12, "法线");
        reader.ExpectTag("PTYP", "图元类型");
        const auto groups = reader.Count(4, limits.maxTotalIndices, "图元类型");
        for (std::size_t group = 0; group < groups; ++group) StaticOnly(reader.U32("图元类型") == kTriangles);
        reader.ExpectTag("PCNT", "图元计数");
        StaticOnly(reader.Count(4, limits.maxTotalIndices, "图元计数") == groups);
        std::uint64_t declared = 0;
        for (std::size_t group = 0; group < groups; ++group) {
            const auto count = reader.U32("图元计数"); StaticOnly(count % 3 == 0); declared += count;
        }
        plan.geosetScratch = std::max(plan.geosetScratch, groups * 2 * sizeof(std::uint32_t));
        reader.ExpectTag("PVTX", "图元索引");
        const auto indices = reader.Count(2, limits.maxTotalIndices, "图元索引");
        StaticOnly(declared == indices);
        plan.Count(totalIndices, indices, limits.maxTotalIndices, sizeof(std::uint16_t));
        reader.Skip(indices * 2, "图元索引"); reader.ExpectTag("GNDX", "顶点组");
        const auto vertexGroups = reader.Count(1, limits.maxVerticesPerGeoset, "顶点组");
        StaticOnly(vertexGroups == vertices); plan.Charge(vertexGroups, sizeof(std::uint8_t));
        reader.Skip(vertexGroups, "顶点组"); reader.ExpectTag("MTGC", "矩阵组");
        const auto matrixGroups = reader.Count(4, limits.maxMatrixGroupsPerGeoset, "矩阵组");
        plan.Charge(matrixGroups, sizeof(std::uint32_t)); reader.Skip(matrixGroups * 4, "矩阵组");
        reader.ExpectTag("MATS", "矩阵索引");
        const auto matrices = reader.Count(4, limits.maxMatrixIndicesPerGeoset, "矩阵索引");
        plan.Charge(matrices, sizeof(std::uint32_t)); reader.Skip(matrices * 4, "矩阵索引");
        reader.Skip(40, "几何静态字段和包围盒");
        const auto bounds = reader.Count(28, limits.maxSequences, "动作包围盒");
        reader.Skip(bounds * 28, "动作包围盒"); reader.ExpectTag("UVAS", "UV组");
        const auto uvSets = reader.Count(0, 16, "UV组");
        for (std::size_t set = 0; set < uvSets; ++set) {
            reader.ExpectTag("UVBS", "UV");
            const auto count = reader.Count(8, limits.maxVerticesPerGeoset, "UV");
            StaticOnly(count == vertices); reader.Skip(count * 8, "UV");
        }
        // 无UV时共享parser会补零；预算同样覆盖真实拥有的第一组数组。
        plan.Charge(vertices, sizeof(std::array<float, 2>));
        StaticOnly(reader.Remaining() == 0); outer.Skip(size - 4, "GEOS内容");
    }
}

StaticParsePlan ScanStaticParse(std::span<const std::byte> bytes, const MdxLimits& limits,
    std::size_t maximumBytes) {
    ValidateMdxLimits(limits);
    if (!maximumBytes || maximumBytes > 128u * 1024u * 1024u)
        Fail("MDX_STATIC_PARSE_BUDGET", "静态解析预算必须为1至128MiB，不能放宽硬上限。");
    if (bytes.size() > limits.maxInputBytes) Fail("MDX_LIMIT", "MDX文件超过大小上限。");
    StaticParsePlan plan; plan.maximumBytes = maximumBytes; plan.Charge(0, 1);
    Reader file(bytes, 0, bytes.size());
    if (file.TagView("文件头") != "MDLX") Fail("MDX_MAGIC", "缺少MDLX文件头。");
    constexpr std::array<std::string_view, 10> tags{"VERS", "MODL", "SEQS", "TEXS", "MTLS", "GEOS", "GEOA", "BONE", "HELP", "PIVT"};
    std::size_t totalVertices = 0, totalIndices = 0;
    while (file.Remaining()) {
        const auto tag = file.TagView("块标记"); const auto size = file.U32("块大小");
        file.Need(size, "块内容");
        const auto found = std::find(tags.begin(), tags.end(), tag);
        StaticOnly(found != tags.end());
        auto chunk = file.Until(file.Position() + size, "块内容");
        if (tag == "VERS") { StaticOnly(size == 4 && chunk.U32("版本") == 800); }
        else if (tag == "MODL") {
            StaticOnly(size == 372); plan.String(chunk.FixedStringView(80, "模型名称"));
            StaticOnly(chunk.FixedStringView(260, "外部动画").empty());
        } else if (tag == "SEQS") {
            StaticOnly(size % 132 == 0); plan.Count(plan.sequences, size / 132, limits.maxSequences, sizeof(MdxSequence));
            while (chunk.Remaining()) {
                plan.String(chunk.FixedStringView(80, "动作名称"));
                const auto start = chunk.U32("动作起点"), end = chunk.U32("动作终点");
                chunk.Skip(4, "动作速度"); StaticOnly(end >= start && chunk.U32("动作标志") <= 1);
                chunk.Skip(36, "动作其余字段");
            }
        } else if (tag == "TEXS") {
            StaticOnly(size % 268 == 0); plan.Count(plan.textures, size / 268, limits.maxTextures, sizeof(MdxTexture));
            while (chunk.Remaining()) {
                chunk.Skip(4, "替换贴图"); plan.String(chunk.FixedStringView(260, "贴图路径")); chunk.Skip(4, "贴图标志");
            }
        } else if (tag == "MTLS") ScanStaticMaterials(chunk, plan, limits);
        else if (tag == "GEOS") ScanStaticGeosets(chunk, plan, limits, totalVertices, totalIndices);
        else if (tag == "GEOA") {
            while (chunk.Remaining()) {
                StaticOnly(chunk.U32("GEOA大小") == 28); chunk.Skip(4, "静态alpha");
                StaticOnly((chunk.U32("GEOA标志") & ~2u) == 0); chunk.Skip(12, "静态颜色");
                chunk.U32("GEOA对象");
                if (++plan.animations > limits.maxGeosets) Fail("MDX_LIMIT", "GEOA累计数量超过上限。");
            }
        } else if (tag == "BONE" || tag == "HELP") {
            while (chunk.Remaining()) {
                StaticOnly(chunk.U32("节点大小") == 96);
                if (tag == "BONE") plan.Count(plan.bones, 1, limits.maxBones, sizeof(MdxBone));
                else plan.Count(plan.helpers, 1, limits.maxHelpers, sizeof(MdxNode));
                if (plan.bones + plan.helpers > limits.maxNodes) Fail("MDX_LIMIT", "公共节点累计数量超过上限。");
                plan.String(chunk.FixedStringView(80, "节点名称")); chunk.Skip(8, "节点身份");
                StaticOnly((chunk.U32("节点标志") & ~0x80000100u) == 0);
                if (tag == "BONE") chunk.Skip(8, "骨骼引用");
            }
        } else if (tag == "PIVT") {
            StaticOnly(size % 12 == 0); plan.Count(plan.pivots, size / 12, limits.maxPivots, sizeof(std::array<float, 3>));
        }
        file.Skip(size, "块内容");
    }
    plan.Finish(); return plan;
}

// 专用静态路径没有PREM重编号。精确reserve连续表并排序，不使用不可计量的map节点。
void ValidateStaticTopology(MdxModel& model) {
    std::vector<StaticTopologyEntry> nodes; nodes.reserve(MdxNodeCount(model));
    ForEachMdxNode(model, [&](const MdxNode& node) { nodes.push_back({&node, 0}); });
    std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.node->objectId < b.node->objectId; });
    const auto find = [&](std::uint32_t id) -> StaticTopologyEntry* {
        const auto found = std::lower_bound(nodes.begin(), nodes.end(), id,
            [](const auto& entry, auto value) { return entry.node->objectId < value; });
        return found != nodes.end() && found->node->objectId == id ? &*found : nullptr;
    };
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto& node = *nodes[index].node;
        if (node.objectId == UINT32_MAX || node.objectId >= model.pivots.size() || node.parentId < -1
            || (index && nodes[index - 1].node->objectId == node.objectId)
            || (node.parentId >= 0 && !find(static_cast<std::uint32_t>(node.parentId))))
            Fail("MDX_REFERENCE_INVALID", "静态节点身份、父引用或PIVT索引无效。");
    }
    for (auto& entry : nodes) {
        auto* current = &entry;
        while (current && current->state != 2) {
            if (current->state == 1) Fail("MDX_REFERENCE_INVALID", "节点父链形成环。");
            current->state = 1;
            current = current->node->parentId < 0 ? nullptr : find(static_cast<std::uint32_t>(current->node->parentId));
        }
        current = &entry;
        while (current && current->state == 1) {
            current->state = 2;
            current = current->node->parentId < 0 ? nullptr : find(static_cast<std::uint32_t>(current->node->parentId));
        }
    }
    for (const auto& geoset : model.geosets)
        for (const auto id : geoset.matrixIndices)
            if (!find(id)) Fail("MDX_REFERENCE_INVALID", "MATS引用了不存在的静态节点。");
    model.skinningTopologyComplete = true;
}
