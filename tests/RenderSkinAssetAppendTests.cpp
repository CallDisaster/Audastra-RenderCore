// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "RenderSkinFixture.h"
#include "../src/Render/Scene/ModelSkinInput.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace War3Map::Render;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() try {
    RenderSkinFixture fixture;
    auto first = fixture.Update(); first.instanceCount = first.groupCount = first.alphaCount = 1;
    auto resident = std::make_shared<ModelScene>(); std::uint64_t bytes = 0;
    Check(CopySkinnedModelInput(fixture.Scene(first), *resident, bytes), "resident fixture");
    const auto* oldVertices = resident->vertices.data(); const auto oldBaseline = resident->initialSkin;
    auto mesh = fixture.mesh; mesh.mesh.baseVertex = 4; mesh.mesh.firstIndex = 6; mesh.mesh.firstLayer = 1;
    auto model = fixture.model; model.model.firstMesh = 1;
    fixture.instances[1].instance.model = 1;
    auto pose = fixture.Update(); pose.sceneVersion = 2;
    std::array<War3RenderMeshColorV2, 2> colors{{{{1,0,0}},{{0,0,1}}}};
    War3RenderDynamicColoredSkinUpdateV2 colored{pose, WAR3_RENDER_DYNAMIC_MESH_RGB_V1, 2, colors.data()};
    colored.base.structSize = sizeof(colored);
    const War3RenderMeshColorV2 staticColor{{0,1,1}};
    War3RenderAssetAppendV2 append{}; append.structSize = sizeof(append);
    append.prefix = {4,6,1,1,1,0,0};
    append.vertexCount = 4; append.indexCount = 6; append.layerCount = append.meshCount = append.modelCount = append.colorCount = 1;
    append.vertices = fixture.vertices.data(); append.indices = fixture.indices.data(); append.layers = &fixture.layer;
    append.meshes = &mesh; append.models = &model; append.colors = &staticColor;
    ModelScene candidate;
    Check(CopySkinAssetAppend(append, colored.base, resident, candidate, bytes), "append B with explicit RGB promotion");
    Check(candidate.staticBase == resident && candidate.vertices.size() == 4 && candidate.indices.size() == 6
        && candidate.VertexCount() == 8 && candidate.IndexCount() == 12 && candidate.models.size() == 2
        && candidate.dynamicColors && candidate.meshColors[1].rgb[1] == 1
        && resident->vertices.data() == oldVertices && resident->initialSkin == oldBaseline,
        "suffix owns only new geometry, keeps original owner and immutable baseline");
    fixture.vertices[0].vertex.x = 777;
    Check(candidate.vertices[0].x == -200 && resident->vertices[0].x == -200, "borrowed suffix survived submit");
    SkinPose updated; ++colored.base.contentVersion;
    Check(CopySkinUpdate(colored.base, candidate, updated), "strict ordinary update works on new baseline");
    Check(!CopySkinUpdate(first, candidate, updated), "old scene cannot write appended baseline");
    const auto baseline = candidate.initialSkin;
    const auto rejected = [&] {
        std::uint64_t unchanged = 991;
        Check(!CopySkinAssetAppend(append, colored.base, resident, candidate, unchanged)
            && candidate.initialSkin == baseline && unchanged == 991, "rejection retains output and payload");
    };
    ++append.prefix.vertexCount; rejected(); --append.prefix.vertexCount;
    fixture.layer.flags = 8; rejected();
    for (const auto flags : {WAR3_RENDER_MODEL_UNFOGGED, WAR3_RENDER_MODEL_LAYER_FLAGS}) {
        fixture.layer.flags = flags; ModelScene flagsCandidate; std::uint64_t flagsBytes = 0;
        Check(CopySkinAssetAppend(append, colored.base, resident, flagsCandidate, flagsBytes),
            "asset append accepts Unfogged and combined flags");
        // layers 使用全局序号：先保留 resident 前缀，再追加新层。
        Check(flagsCandidate.layers.size() == append.prefix.layerCount + append.layerCount
            && flagsCandidate.layers[0].flags == resident->layers[0].flags
            && flagsCandidate.layers[append.prefix.layerCount].flags == flags,
            "asset append retains resident prefix and appended Unfogged flags");
    }
    fixture.layer.flags = WAR3_RENDER_MODEL_UNSHADED;
    mesh.mesh.baseVertex = 0; rejected(); mesh.mesh.baseVertex = 4;
    fixture.groups[1].values[1] = std::numeric_limits<float>::quiet_NaN(); rejected(); fixture.groups[1].values[1] = 0;
    colors[1].rgb[0] = 1.1f; rejected(); colors[1].rgb[0] = 0;
    candidate.SealStaticSuffix();
    auto appended = std::make_shared<ModelScene>(std::move(candidate));
    std::weak_ptr<ModelScene> previousOwner = appended;
    const auto previousPage = appended->staticSuffix;
    append.prefix = {8,12,2,2,2,0,0}; mesh.mesh.baseVertex = 8; mesh.mesh.firstIndex = 12; mesh.mesh.firstLayer = 2;
    model.model.firstMesh = 2; colored.base.sceneVersion = 3; colored.base.contentVersion = 1;
    Check(CopySkinAssetAppend(append, colored.base, appended, candidate, bytes), "append C keeps global model ordinals");
    candidate.SealStaticSuffix(); appended.reset();
    Check(previousOwner.expired() && candidate.staticBase == resident && candidate.staticPrefixes.size() == 1
        && candidate.staticPrefixes[0] == previousPage && candidate.models.size() == 3,
        "static pages retain bytes without retaining previous dynamic/layout owner");
    auto empty = colored; empty.base.sceneVersion = 4; empty.base.contentVersion = 1;
    empty.base.instanceCount = empty.base.groupCount = empty.base.alphaCount = empty.colorCount = 0;
    empty.base.instances = nullptr; empty.base.groups = nullptr; empty.base.alphas = nullptr; empty.colors = nullptr;
    ModelScene zero;
    Check(CopySkinRoster(empty.base, candidate, zero) && zero.dynamicColors && zero.initialSkin->instances.empty(),
        "zero instances preserve explicit dynamic RGB format");
    empty.base.sceneVersion = 5;
    Check(CopySkinRoster(empty.base, zero, candidate), "empty RGB baseline can rebind again");
    static_assert(offsetof(War3RenderEditorAssetRosterV2, base) == 0);
    std::cout << "asset suffix owner, strict update, RGB promotion, bounds and output rollback passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
