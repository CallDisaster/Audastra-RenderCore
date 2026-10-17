// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "War3RenderCore/Legacy/RenderPluginParticleMaterial.h"
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

namespace {
// 冻结旧布局，不以新头的别名代替旧消费者的二进制前缀。
struct LegacyParticleDraw {
    std::uint32_t firstVertex, vertexCount, texture, filter;
};
struct LegacyParticleUpdate {
    std::uint32_t structSize, vertexCount, drawCount, reserved;
    std::uint64_t sceneVersion, contentVersion;
    const War3RenderParticleVertexV2* vertices;
    const LegacyParticleDraw* draws;
    const War3RenderSkinUpdateV2* modelPose;
};
static_assert(sizeof(LegacyParticleDraw) == sizeof(War3RenderParticleDrawV2));
static_assert(sizeof(LegacyParticleUpdate) == sizeof(War3RenderParticleUpdateV2));
static_assert(offsetof(LegacyParticleUpdate, vertices) == offsetof(War3RenderParticleUpdateV2, vertices));
static_assert(offsetof(LegacyParticleUpdate, draws) == offsetof(War3RenderParticleUpdateV2, draws));
static_assert(offsetof(LegacyParticleUpdate, modelPose) == offsetof(War3RenderParticleUpdateV2, modelPose));
static_assert(offsetof(War3RenderPluginApiV2, getModelLayerCapabilities) == 8 + 39 * sizeof(void(*)()));
static_assert(offsetof(War3RenderPluginApiV2, getStaticModelSamplerCapabilities) == 8 + 40 * sizeof(void(*)()));
static_assert(offsetof(War3RenderPluginApiV2, getStaticModelSamplerCapabilities)
    + sizeof(War3RenderPluginApiV2::getStaticModelSamplerCapabilities) == 8 + 41 * sizeof(void(*)()));
static_assert(WAR3_RENDER_PARTICLE_MATERIAL_FLAGS_V1 == 1 && WAR3_PARTICLE_UNFOGGED == 1);

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::int32_t WAR3_RENDER_CALL LegacyStatusOnly(War3RendererHandle, War3RenderSkinStatusV2* output) {
    if (!output || output->structSize < sizeof(*output)) return -1;
    War3RenderSkinStatusV2 value{}; value.structSize = sizeof(value); value.result = 2;
    *output = value;
    return 0;
}

std::int32_t WAR3_RENDER_CALL LegacyMaterialState(War3RendererHandle, War3RenderSkinStatusV2* output) {
    if (!output || output->structSize < sizeof(War3RenderSkinMaterialStateCapabilitiesV2)) return -1;
    War3RenderSkinMaterialStateCapabilitiesV2 value{};
    auto& status = *reinterpret_cast<War3RenderSkinStatusV2*>(&value);
    status.structSize = sizeof(status); status.result = 2;
    value.materialStateFormat = WAR3_RENDER_DYNAMIC_MATERIAL_STATE_V1;
    std::memcpy(output, &value, sizeof(value));
    return 0;
}
}

int main() {
    try {
        // 两种已知旧provider只写自己的前缀；这是ABI合同夹具，真实provider的
        // 生产输入验证、深拷贝、事务与像素消费由集成测试单独验收。
        for (const auto provider : {&LegacyStatusOnly, &LegacyMaterialState}) {
            War3RenderParticleMaterialCapabilitiesV2 query{};
            auto& status = *reinterpret_cast<War3RenderSkinStatusV2*>(&query);
            status.structSize = sizeof(query);
            War3RenderPluginApiV2 api{};
            api.structSize = sizeof(api); api.apiVersion = WAR3_RENDER_PLUGIN_API_V2;
            api.getSkinStatus = provider;
            Check(api.getSkinStatus(nullptr, &status) == 0, "legacy capability query failed");
            Check(query.particleMaterialFlagsFormat == 0 && query.particleMaterialFlagsMask == 0,
                "legacy provider must not advertise the new particle material tail");
            Check(status.result == 2, "capability query must not fabricate a completed frame");
        }
        // 原始字节读取验证旧draw stride和update前缀不会被新增平行flags池挤移。
        War3RenderParticleDrawV2 draws[]{{0, 3, 0, WAR3_PARTICLE_BLEND}, {3, 3, 0, WAR3_PARTICLE_BLEND}};
        const std::uint32_t flags[]{0, WAR3_PARTICLE_UNFOGGED};
        War3RenderParticleMaterialUpdateV2 extended{};
        extended.base.structSize = sizeof(extended); extended.base.drawCount = 2;
        extended.base.vertexCount = 6; extended.base.sceneVersion = 7; extended.base.contentVersion = 11;
        extended.base.draws = draws;
        extended.materialFlagsFormat = WAR3_RENDER_PARTICLE_MATERIAL_FLAGS_V1;
        extended.materialFlagsCount = 2; extended.materialFlags = flags;
        LegacyParticleUpdate legacy{}; std::memcpy(&legacy, &extended, sizeof(legacy));
        Check(legacy.drawCount == 2 && legacy.vertexCount == 6 && legacy.sceneVersion == 7
            && legacy.contentVersion == 11, "frozen update prefix offsets changed");
        LegacyParticleDraw second{};
        std::memcpy(&second, reinterpret_cast<const unsigned char*>(legacy.draws) + sizeof(LegacyParticleDraw), sizeof(second));
        Check(second.firstVertex == 3 && second.vertexCount == 3 && second.filter == WAR3_PARTICLE_BLEND,
            "frozen draw stride/field offsets changed");
        Check(extended.materialFlags[0] == 0 && extended.materialFlags[1] == WAR3_PARTICLE_UNFOGGED,
            "parallel material flags lost distinct draw order");
        std::cout << "particle-material ABI prefixes and legacy capability fixtures passed; no GPU proof\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
