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
namespace { void Check(bool ok) { if (!ok) throw std::runtime_error("蒙皮输入校验失败"); } }
int main() {
    try {
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
        static_assert(sizeof(War3RenderPluginApiV2) == 8 + 37 * sizeof(void(*)()));
        static_assert(std::is_standard_layout_v<War3RenderFrameMetricsV2> && sizeof(War3RenderFrameMetricsV2) == 408);
        static_assert(sizeof(War3RenderFrameMetricsV2::completedSteadyNs) == 8
            && offsetof(War3RenderFrameMetricsV2, completedSteadyNs) == 16
            && offsetof(War3RenderFrameMetricsV2, readbackCpuNs) == 48
            && offsetof(War3RenderFrameMetricsV2, completed) == 80);
        static_assert(std::is_standard_layout_v<War3RenderFrameMetricsPageV2>
            && sizeof(War3RenderFrameMetricsPageV2) == 304
            && offsetof(War3RenderFrameMetricsPageV2, oldestFrame) == 16
            && offsetof(War3RenderFrameMetricsPageV2, adapterName) == 48);
        std::cout << "skin_input copied=1 range_layout_count_rejections=1 recovery=1 old_tail_unchanged=1\n";
        std::cout << "frame_metrics_abi pointer_bytes=" << sizeof(void*) << " api_bytes=" << sizeof(War3RenderPluginApiV2)
            << " tail_offset=" << offsetof(War3RenderPluginApiV2, getFrameMetrics)
            << " record_bytes=" << sizeof(War3RenderFrameMetricsV2) << " page_bytes=" << sizeof(War3RenderFrameMetricsPageV2) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
