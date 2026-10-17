// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "RenderSkinFixture.h"
#include "War3RenderCore/Legacy/RenderPluginModelLayers.h"
#include "War3RenderCore/Legacy/RenderPluginFrameMetrics.h"
#include "../src/Render/Scene/ModelSkinInput.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace War3Map::Render;
namespace {
void Check(bool ok) { if (!ok) throw std::runtime_error("蒙皮输入校验失败"); }
void LayerFlags() {
    static_assert(WAR3_RENDER_MODEL_UNSHADED == 1 && WAR3_RENDER_MODEL_UNFOGGED == 2
        && WAR3_RENDER_MODEL_NO_DEPTH_WRITE == 4 && WAR3_RENDER_MODEL_LAYER_FLAGS == 7);
    static_assert(sizeof(War3RenderModelLayerV2) == 16 && offsetof(War3RenderModelLayerV2, alpha) == 12);
    RenderSkinFixture fixture; auto update = fixture.Update(); const auto description = fixture.Scene(update);
    for (std::uint32_t flags = 0; flags <= WAR3_RENDER_MODEL_LAYER_FLAGS; ++flags) {
        fixture.layer.flags = flags; ModelScene scene; std::uint64_t bytes = 0;
        Check(CopySkinnedModelInput(description, scene, bytes) && scene.layers[0].flags == flags);
        Check(ValidateModelGeometry(scene, 0));
    }
    for (const auto flags : {8u, 0x80000000u, UINT32_MAX}) {
        fixture.layer.flags = flags; ModelScene scene; std::uint64_t bytes = 0;
        Check(!CopySkinnedModelInput(description, scene, bytes));
        scene.layers.push_back(fixture.layer); Check(!ValidateModelGeometry(scene, 0));
    }
}
}
int main() {
    try {
        LayerFlags();
        RenderSkinFixture fixture;
        auto update = fixture.Update(); auto description = fixture.Scene(update);
        ModelScene scene; std::uint64_t bytes = 0;
        Check(CopySkinnedModelInput(description,scene,bytes));
        Check(scene.initialSkin && scene.initialSkin->groups.size() == 2 && bytes != 0);
        fixture.groups[0].values[9] = -100;
        Check(scene.initialSkin->groups[0].values[9] == -500); // 无借用，静态/动态都拷贝。
        SkinPose pose; update.contentVersion = 2;
        Check(CopySkinUpdate(update,scene,pose) && pose.groups[0].values[9] == -100);
        const auto reject = [&](War3RenderSkinUpdateV2 bad) { SkinPose temporary; Check(!CopySkinUpdate(bad,scene,temporary)); };
        auto bad = update; bad.matrixStrideBytes = 64; reject(bad);
        bad = update; bad.worldTransformApplied = 1; reject(bad);
        bad = update; bad.matrixFormat = 99; reject(bad);
        bad = update; bad.sceneVersion = 99; reject(bad);
        bad = update; bad.groupCount = 1; reject(bad);
        bad = update; bad.groups = nullptr; reject(bad);
        bad = update; bad.structSize = 4; reject(bad);
        fixture.instances[1].firstGroup = 0; reject(update); fixture.instances[1].firstGroup = 1;
        fixture.alphas[1] = std::numeric_limits<float>::max(); Check(CopySkinUpdate(update,scene,pose));
        fixture.alphas[1] = std::numeric_limits<float>::quiet_NaN(); reject(update); fixture.alphas[1] = 1;
        fixture.groups[0].values[0] = std::numeric_limits<float>::infinity(); reject(update); fixture.groups[0].values[0] = 1;
        const auto rejectScene = [&] { ModelScene temporary; Check(!CopySkinnedModelInput(description,temporary,bytes)); };
        fixture.vertices[0].group = 1; rejectScene(); fixture.vertices[0].group = 0;
        fixture.mesh.firstGroup = 1; rejectScene(); fixture.mesh.firstGroup = 0;
        fixture.mesh.mesh.indexCount = 3; rejectScene(); fixture.mesh.mesh.indexCount = 6;
        fixture.model.model.firstMesh = 1; rejectScene(); fixture.model.model.firstMesh = 0;
        Check(CopySkinUpdate(update,scene,pose));
        ModelScene recovered; Check(CopySkinnedModelInput(description,recovered,bytes));
        // 新模式显式允许同模型共享完整片段，并在不改变资产/池容量时切换。
        auto shared = update; shared.matrixFormat = WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4_SHARED_RANGES;
        fixture.instances[1].firstGroup = fixture.instances[1].firstAlpha = 0;
        auto sharedDescription = fixture.Scene(shared);
        ModelScene sharedScene; Check(CopySkinnedModelInput(sharedDescription, sharedScene, bytes));
        fixture.instances[1].firstGroup = fixture.instances[1].firstAlpha = 1;
        ++shared.contentVersion; Check(CopySkinUpdate(shared, sharedScene, pose));
        auto wrongMode = shared; wrongMode.matrixFormat = WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4;
        Check(!CopySkinUpdate(wrongMode, sharedScene, pose));
        fixture.instances[1].firstGroup = 2;
        Check(!CopySkinUpdate(shared, sharedScene, pose));
        fixture.instances[1].firstGroup = 1;
        // 不允许把另一模型的片段声明成自己的；数据恰好同长也不能串姿势。
        auto aliasedModel = sharedScene;
        aliasedModel.models.push_back(aliasedModel.models.front());
        aliasedModel.modelGroupCounts.push_back(1); aliasedModel.initialSkin.reset();
        fixture.instances[1].instance.model = 1; fixture.instances[1].firstGroup = 0;
        Check(!CopySkinUpdate(shared, aliasedModel, pose));
        fixture.instances[1].instance.model = 0; fixture.instances[1].firstGroup = 1;
        War3RenderSkinUpdateV2 empty{}; empty.structSize = sizeof(empty); empty.sceneVersion = 2;
        empty.contentVersion = 1; empty.matrixFormat = WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4; empty.matrixStrideBytes = 48;
        War3RenderSkinnedModelSceneV2 clear{}; clear.structSize = sizeof(clear); clear.initialPose = &empty;
        ModelScene cleared; Check(CopySkinnedModelInput(clear,cleared,bytes) && cleared.vertices.empty());
        // 可选新尾部与旧描述分开：每实例实际draw层alpha拥有自己的完整片段。
        War3RenderMaterialSkinUpdateV2 material{}; material.base.base = fixture.Update();
        material.base.base.structSize = sizeof(material); material.layerAlphaFormat = WAR3_RENDER_DYNAMIC_LAYER_ALPHA_V1;
        float layerAlphas[]{1, .5f}; War3RenderMaterialLayerSliceV2 layerSlices[]{{0, 1}, {1, 1}};
        material.layerAlphaCount = 2; material.instanceSliceCount = 2;
        material.layerAlphas = layerAlphas; material.instanceSlices = layerSlices;
        auto materialDescription = fixture.Scene(material.base.base); ModelScene materialScene;
        Check(CopySkinnedModelInput(materialDescription, materialScene, bytes));
        Check(materialScene.dynamicMaterialAlphas && !materialScene.dynamicColors && materialScene.modelLayerCounts == std::vector<std::uint32_t>{1}
            && materialScene.initialSkin->layerAlphas == std::vector<float>({1, .5f}));
        layerAlphas[1] = .25f;
        Check(materialScene.initialSkin->layerAlphas[1] == .5f && CopySkinUpdate(material.base.base, materialScene, pose) && pose.layerAlphas[1] == .25f);
        const auto rejectMaterial = [&] { SkinPose temporary; Check(!CopySkinUpdate(material.base.base, materialScene, temporary)); };
        material.layerAlphas = nullptr; rejectMaterial(); material.layerAlphas = layerAlphas;
        material.instanceSliceCount = 1; rejectMaterial(); material.instanceSliceCount = 2;
        layerSlices[1].count = 2; rejectMaterial(); layerSlices[1].count = 1;
        layerSlices[1].first = 0; rejectMaterial(); layerSlices[1].first = 1;
        layerAlphas[1] = std::numeric_limits<float>::quiet_NaN(); rejectMaterial(); layerAlphas[1] = .25f;
        material.reserved = 1; rejectMaterial(); material.reserved = 0;
        material.base.colorCount = 2; rejectMaterial(); material.base.colorCount = 0;
        material.layerAlphaFormat = 99; rejectMaterial(); material.layerAlphaFormat = WAR3_RENDER_DYNAMIC_LAYER_ALPHA_V1;
        material.base.base.sceneVersion = 2; ModelScene roster;
        Check(CopySkinRoster(material.base.base, materialScene, roster) && roster.dynamicMaterialAlphas && roster.modelLayerCounts == materialScene.modelLayerCounts);
        auto legacyRoster = fixture.Update(2); legacyRoster.sceneVersion = 2; Check(!CopySkinRoster(legacyRoster, materialScene, roster));
        material.base.base.instanceCount = material.base.base.groupCount = material.base.base.alphaCount = 0;
        material.layerAlphaCount = material.instanceSliceCount = 0;
        Check(CopySkinRoster(material.base.base, materialScene, roster) && roster.initialSkin->instances.empty()
            && roster.dynamicMaterialAlphas && roster.initialSkin->layerAlphas.empty());
        // 共享片段只能由同模型完整别名；额外材质通道不允许部分重叠或跨模型。
        material.base.base = shared; material.base.base.structSize = sizeof(material);
        material.layerAlphaCount = material.instanceSliceCount = 2; layerSlices[1].first = 0;
        fixture.instances[1].firstGroup = fixture.instances[1].firstAlpha = 0;
        ModelScene materialShared; auto materialSharedDescription = fixture.Scene(material.base.base);
        Check(CopySkinnedModelInput(materialSharedDescription, materialShared, bytes));
        materialShared.initialSkin.reset(); materialShared.models.push_back(materialShared.models[0]);
        materialShared.modelGroupCounts.push_back(1); materialShared.modelLayerCounts.push_back(1);
        fixture.instances[1].instance.model = 1; fixture.instances[1].firstGroup = fixture.instances[1].firstAlpha = 1;
        Check(!CopySkinUpdate(material.base.base, materialShared, pose));
        fixture.instances[1].instance.model = 0; fixture.instances[1].firstGroup = fixture.instances[1].firstAlpha = 1;
        RenderSkinFixture stateFixture; War3RenderMaterialStateSkinUpdateV2 states{};
        states.base.base.base = stateFixture.Update(); states.base.base.base.structSize = sizeof(states);
        states.base.layerAlphaFormat = WAR3_RENDER_DYNAMIC_LAYER_ALPHA_V1; states.base.layerAlphaCount = 2;
        states.base.instanceSliceCount = 2; states.base.layerAlphas = layerAlphas; states.base.instanceSlices = layerSlices;
        War3RenderMaterialStateV2 stateValues[]{{0,0,{1,0,0,0,1,0}}, {1,3,{0,.5f,.125f,.25f,0,.625f}}};
        War3RenderMaterialLayerSliceV2 stateSlices[]{{0,1},{1,1}}; layerSlices[0] = {0,1}; layerSlices[1] = {1,1};
        states.materialStateFormat = WAR3_RENDER_DYNAMIC_MATERIAL_STATE_V1; states.materialStateCount = states.materialInstanceSliceCount = 2;
        states.materialStates = stateValues; states.materialInstanceSlices = stateSlices;
        auto stateDescription = stateFixture.Scene(states.base.base.base); stateDescription.textureCount = 2;
        ModelScene stateScene; Check(CopySkinnedModelInput(stateDescription, stateScene, bytes));
        Check(stateScene.dynamicMaterialStates && stateScene.initialSkin->materialStates[1].textureIndex == 1);
        stateValues[1].textureIndex = 0; ++states.base.base.base.contentVersion;
        Check(stateScene.initialSkin->materialStates[1].textureIndex == 1 && CopySkinUpdate(states.base.base.base, stateScene, pose)
            && pose.materialStates[1].textureIndex == 0);
        const auto rejectState = [&] { SkinPose temporary; Check(!CopySkinUpdate(states.base.base.base, stateScene, temporary)); };
        stateValues[1].textureIndex = 2; rejectState(); stateValues[1].textureIndex = WAR3_RENDER_MODEL_NO_TEXTURE;
        Check(CopySkinUpdate(states.base.base.base, stateScene, pose)); stateValues[1].textureIndex = 1;
        stateValues[1].samplerFlags = 4; rejectState(); stateValues[1].samplerFlags = 3;
        stateValues[1].uvTransform[1] = std::numeric_limits<float>::quiet_NaN(); rejectState(); stateValues[1].uvTransform[1] = .5f;
        stateSlices[1].first = 0; rejectState(); stateSlices[1].first = 1;
        states.materialInstanceSliceCount = 1; rejectState(); states.materialInstanceSliceCount = 2;
        states.materialStates = nullptr; rejectState(); states.materialStates = stateValues;
        states.reserved = 1; rejectState(); states.reserved = 0;
        auto shortState = states.base; shortState.base.base.structSize = sizeof(shortState);
        Check(!CopySkinUpdate(shortState.base.base, stateScene, pose)); // 没有尾部不能继续旧静态贴图。
        states.base.base.base.sceneVersion = 2; ModelScene stateRoster;
        Check(CopySkinRoster(states.base.base.base, stateScene, stateRoster) && stateRoster.dynamicMaterialStates);
        states.base.base.base.matrixFormat = WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4_SHARED_RANGES;
        stateFixture.instances[1].firstGroup = stateFixture.instances[1].firstAlpha = 0;
        states.base.instanceSlices = layerSlices; layerSlices[1].first = 0; stateSlices[1].first = 0;
        ModelScene stateShared; Check(CopySkinnedModelInput(stateDescription, stateShared, bytes));
        stateShared.initialSkin.reset(); stateShared.models.push_back(stateShared.models[0]); stateShared.modelGroupCounts.push_back(1);
        stateShared.modelLayerCounts.push_back(1); stateFixture.instances[1].instance.model = 1;
        Check(!CopySkinUpdate(states.base.base.base, stateShared, pose)); // 不允许跨模型别名。
        static_assert(offsetof(War3RenderPluginApiV2,submitSkinnedModelScene)
            == offsetof(War3RenderPluginApiV2,setOverlayLines) + sizeof(War3RenderPluginApiV2::setOverlayLines));
        // 0.1.0 的 28 个函数指针前缀尺寸在 x64/Win32 均保持不变。
        static_assert(offsetof(War3RenderPluginApiV2,getEditorCompletedFrame) == 8 + 28 * sizeof(void(*)()));
        static_assert(offsetof(War3RenderPluginApiV2,submitModelSceneLayer)
            == offsetof(War3RenderPluginApiV2,getEditorCompletedFrame) + sizeof(War3RenderPluginApiV2::getEditorCompletedFrame));
        static_assert(std::is_standard_layout_v<War3RenderLayeredCapturedFrameV2>);
        static_assert(offsetof(War3RenderLayeredCapturedFrameV2,world) == 16);
        static_assert(offsetof(War3RenderSkinSharedRangeCapabilitiesV2,base) == 0);
        // A1 只追加函数尾部，0.1.1 的全部入口保持尺寸与偏移；无指针的计时载荷两档位一致。
        static_assert(offsetof(War3RenderPluginApiV2, getFrameMetrics)
            == offsetof(War3RenderPluginApiV2, captureLayeredFrameRgba) + sizeof(War3RenderPluginApiV2::captureLayeredFrameRgba));
        static_assert(offsetof(War3RenderPluginApiV2, getFrameMetrics) == 8 + 34 * sizeof(void(*)()));
        static_assert(offsetof(War3RenderPluginApiV2, submitEditorInstanceRoster) == 8 + 35 * sizeof(void(*)()));
        static_assert(offsetof(War3RenderPluginApiV2, getModelLayerCapabilities) == 8 + 39 * sizeof(void(*)()));
        static_assert(offsetof(War3RenderPluginApiV2, getStaticModelSamplerCapabilities) == 8 + 40 * sizeof(void(*)()));
        static_assert(sizeof(War3RenderPluginApiV2) == 8 + 41 * sizeof(void(*)()));
        static_assert(offsetof(War3RenderModelSceneLayerV2, role) == (sizeof(void*) == 8 ? 56 : 48));
        static_assert(sizeof(War3RenderModelSceneLayerV2) == (sizeof(void*) == 8 ? 64 : 56));
        static_assert(std::is_standard_layout_v<War3RenderFrameMetricsV2> && sizeof(War3RenderFrameMetricsV2) == 408);
        static_assert(sizeof(War3RenderFrameMetricsV2::completedSteadyNs) == 8
            && offsetof(War3RenderFrameMetricsV2, completedSteadyNs) == 16
            && offsetof(War3RenderFrameMetricsV2, readbackCpuNs) == 48
            && offsetof(War3RenderFrameMetricsV2, completed) == 80);
        static_assert(std::is_standard_layout_v<War3RenderFrameMetricsPageV2>
            && sizeof(War3RenderFrameMetricsPageV2) == 304
            && offsetof(War3RenderFrameMetricsPageV2, oldestFrame) == 16
            && offsetof(War3RenderFrameMetricsPageV2, adapterName) == 48);
        std::cout << "skin_input copied=1 range_layout_count_rejections=1 recovery=1 old_tail_unchanged=1 material_alpha_owned=1 material_roster_empty=1\n";
        std::cout << "frame_metrics_abi pointer_bytes=" << sizeof(void*) << " api_bytes=" << sizeof(War3RenderPluginApiV2)
            << " tail_offset=" << offsetof(War3RenderPluginApiV2, getFrameMetrics)
            << " record_bytes=" << sizeof(War3RenderFrameMetricsV2) << " page_bytes=" << sizeof(War3RenderFrameMetricsPageV2) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
