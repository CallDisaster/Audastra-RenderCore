// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "RenderSkinFixture.h"
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
        War3RenderSkinUpdateV2 empty{}; empty.structSize = sizeof(empty); empty.sceneVersion = 2;
        empty.contentVersion = 1; empty.matrixFormat = WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4; empty.matrixStrideBytes = 48;
        War3RenderSkinnedModelSceneV2 clear{}; clear.structSize = sizeof(clear); clear.initialPose = &empty;
        ModelScene cleared; Check(CopySkinnedModelInput(clear,cleared,bytes) && cleared.vertices.empty());
        static_assert(offsetof(War3RenderPluginApiV2,submitSkinnedModelScene)
            == offsetof(War3RenderPluginApiV2,setOverlayLines) + sizeof(War3RenderPluginApiV2::setOverlayLines));
        std::cout << "skin_input copied=1 range_layout_count_rejections=1 recovery=1 old_tail_unchanged=1\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
