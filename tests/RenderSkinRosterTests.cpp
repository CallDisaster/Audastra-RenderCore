// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "RenderSkinFixture.h"
#include "War3MapCore/RenderPluginInstanceRoster.h"
#include "../src/Render/Scene/ModelSkinInput.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
using namespace War3Map::Render;
namespace {
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
}
int main() {
    try {
        RenderSkinFixture fixture;
        auto one = fixture.Update(); one.instanceCount = one.groupCount = one.alphaCount = 1;
        auto description = fixture.Scene(one);
        ModelScene resident; std::uint64_t bytes = 0;
        Check(CopySkinnedModelInput(description, resident, bytes), "initial one-instance scene");
        const auto initial = resident.initialSkin;
        const auto* vertices = resident.vertices.data(); const auto* indices = resident.indices.data();
        std::vector<War3RenderSkinnedModelInstanceV2> instances(20, fixture.instances[0]);
        std::vector<War3RenderGroupMatrixV2> groups(20, fixture.groups[0]);
        std::vector<float> alphas(20, 1);
        for (std::uint32_t i = 0; i < instances.size(); ++i) {
            instances[i].firstGroup = instances[i].firstAlpha = i;
            groups[i].values[9] = static_cast<float>(i * 10);
        }
        auto twenty = one; twenty.sceneVersion = 2;
        twenty.instanceCount = twenty.groupCount = twenty.alphaCount = 20;
        twenty.instances = instances.data(); twenty.groups = groups.data(); twenty.alphas = alphas.data();
        SkinPose ordinary;
        Check(!CopySkinUpdate(twenty, resident, ordinary), "ordinary update must reject changed roster");
        ModelScene rebound;
        Check(CopySkinRoster(twenty, resident, rebound), "one to twenty roster");
        Check(rebound.initialSkin->instances.size() == 20 && rebound.instances.size() == 20
            && rebound.models.size() == resident.models.size() && rebound.meshGroups == resident.meshGroups
            && !rebound.explicitStaticSamplers && rebound.staticSamplerFlags.empty() && rebound.staticSamplerBackendBudget == 0,
            "new baseline and fixed layout");
        Check(rebound.vertices.empty() && rebound.indices.empty() && rebound.textures.empty()
            && resident.vertices.data() == vertices && resident.indices.data() == indices
            && resident.initialSkin == initial, "roster does not duplicate or mutate static assets");
        groups[0].values[9] = 999;
        Check(rebound.initialSkin->groups[0].values[9] == 0, "input lifetime is owned");
        ++twenty.contentVersion;
        Check(CopySkinUpdate(twenty, rebound, ordinary) && ordinary.groups[0].values[9] == 999,
            "ordinary update accepts exact new baseline");
        Check(!CopySkinUpdate(one, rebound, ordinary), "old scene cannot update new roster");
        const auto baseline = rebound.initialSkin;
        const auto reject = [&](War3RenderSkinUpdateV2 bad) {
            Check(!CopySkinRoster(bad, resident, rebound) && rebound.initialSkin == baseline,
                "rejected roster preserves output owner");
        };
        auto bad = twenty; bad.reserved = 1; reject(bad);
        bad = twenty; bad.groupCount = 19; reject(bad);
        bad = twenty; bad.matrixFormat = WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4_SHARED_RANGES; reject(bad);
        groups[0].values[0] = std::numeric_limits<float>::quiet_NaN(); reject(twenty); groups[0].values[0] = 1;
        instances[1].firstGroup = 0; reject(twenty); instances[1].firstGroup = 1;
        War3RenderSkinUpdateV2 empty{}; empty.structSize = sizeof(empty);
        empty.sceneVersion = 3; empty.contentVersion = 1;
        empty.matrixFormat = WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4; empty.matrixStrideBytes = 48;
        Check(CopySkinRoster(empty, resident, rebound) && rebound.initialSkin->instances.empty()
            && rebound.initialSkin->groups.empty() && rebound.instances.empty()
            && rebound.models.size() == 1, "twenty to zero retains resident layout");
        static_assert(std::is_standard_layout_v<War3RenderEditorInstanceRosterV2>);
        static_assert(std::is_standard_layout_v<War3RenderInstanceRosterResultV2>);
        static_assert(offsetof(War3RenderPluginApiV2, getFrameMetrics) == 8 + 34 * sizeof(void(*)()));
        static_assert(offsetof(War3RenderPluginApiV2, submitEditorInstanceRoster) == 8 + 35 * sizeof(void(*)()));
        static_assert(offsetof(War3RenderPluginApiV2, getModelLayerCapabilities) == 8 + 39 * sizeof(void(*)()));
        static_assert(offsetof(War3RenderPluginApiV2, getStaticModelSamplerCapabilities) == 8 + 40 * sizeof(void(*)()));
        static_assert(sizeof(War3RenderPluginApiV2) == 8 + 41 * sizeof(void(*)()));
        std::cout << "resident-roster one->twenty->zero, strict baseline and rollback passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
