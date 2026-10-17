// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "War3MapCore/RenderPluginStaticSamplers.h"
#include "War3RenderCore/Legacy/RenderPluginStaticSamplers.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
// 冻结5d8之前的完整静态scene/layer布局，避免用新类型别名充当旧consumer。
struct FrozenLayer {
    std::uint32_t textureIndex, filterMode, flags;
    float alpha;
};
struct FrozenStaticScene {
    std::uint32_t structSize;
    std::uint32_t vertexCount, indexCount, layerCount, meshCount, modelCount, instanceCount, textureCount;
    const War3RenderModelVertexV2* vertices;
    const std::uint16_t* indices;
    const FrozenLayer* layers;
    const War3RenderModelMeshV2* meshes;
    const War3RenderModelV2* models;
    const War3RenderModelInstanceV2* instances;
    const War3RenderTextureMipChainV2* textures;
};
struct FrozenApiPrefix {
    std::uint32_t structSize, apiVersion;
    void (*functions[40])();
};
static_assert(sizeof(FrozenLayer) == 16 && sizeof(FrozenLayer) == sizeof(War3RenderModelLayerV2));
static_assert(offsetof(FrozenLayer, textureIndex) == offsetof(War3RenderModelLayerV2, textureIndex));
static_assert(offsetof(FrozenLayer, filterMode) == offsetof(War3RenderModelLayerV2, filterMode));
static_assert(offsetof(FrozenLayer, flags) == offsetof(War3RenderModelLayerV2, flags));
static_assert(offsetof(FrozenLayer, alpha) == offsetof(War3RenderModelLayerV2, alpha));
static_assert(sizeof(FrozenStaticScene) == sizeof(War3RenderModelSceneV2));
static_assert(sizeof(FrozenStaticScene) == (sizeof(void*) == 8 ? 88 : 60));
#define CHECK_SCENE_OFFSET(member) \
    static_assert(offsetof(FrozenStaticScene, member) == offsetof(War3RenderModelSceneV2, member))
CHECK_SCENE_OFFSET(structSize);
CHECK_SCENE_OFFSET(vertexCount);
CHECK_SCENE_OFFSET(indexCount);
CHECK_SCENE_OFFSET(layerCount);
CHECK_SCENE_OFFSET(meshCount);
CHECK_SCENE_OFFSET(modelCount);
CHECK_SCENE_OFFSET(instanceCount);
CHECK_SCENE_OFFSET(textureCount);
CHECK_SCENE_OFFSET(vertices);
CHECK_SCENE_OFFSET(indices);
CHECK_SCENE_OFFSET(layers);
CHECK_SCENE_OFFSET(meshes);
CHECK_SCENE_OFFSET(models);
CHECK_SCENE_OFFSET(instances);
CHECK_SCENE_OFFSET(textures);
#undef CHECK_SCENE_OFFSET

// 全部旧函数的绝对位置由冻结序号给出，不从相邻新字段反推。
#define CHECK_API_OFFSET(member, index) \
    static_assert(offsetof(War3RenderPluginApiV2, member) == 8 + index * sizeof(void(*)())); \
    static_assert(sizeof(War3RenderPluginApiV2::member) == sizeof(void(*)()))
CHECK_API_OFFSET(createRenderer, 0);
CHECK_API_OFFSET(destroyRenderer, 1);
CHECK_API_OFFSET(attachNativeWindow, 2);
CHECK_API_OFFSET(resize, 3);
CHECK_API_OFFSET(submitMesh, 4);
CHECK_API_OFFSET(setCamera, 5);
CHECK_API_OFFSET(requestFrame, 6);
CHECK_API_OFFSET(getStatus, 7);
CHECK_API_OFFSET(captureRgba, 8);
CHECK_API_OFFSET(getViewInfo, 9);
CHECK_API_OFFSET(submitTerrainScene, 10);
CHECK_API_OFFSET(submitTerrainMipScene, 11);
CHECK_API_OFFSET(captureFrameRgba, 12);
CHECK_API_OFFSET(submitModelScene, 13);
CHECK_API_OFFSET(setEnvironment, 14);
CHECK_API_OFFSET(updateModelInstances, 15);
CHECK_API_OFFSET(setOverlayLines, 16);
CHECK_API_OFFSET(submitSkinnedModelScene, 17);
CHECK_API_OFFSET(updateSkinMatrices, 18);
CHECK_API_OFFSET(getSkinStatus, 19);
CHECK_API_OFFSET(captureSkinnedFrameRgba, 20);
CHECK_API_OFFSET(submitParticleModelScene, 21);
CHECK_API_OFFSET(updateParticleModelFrame, 22);
CHECK_API_OFFSET(captureParticleFrameRgba, 23);
CHECK_API_OFFSET(getEditorPresentationTarget, 24);
CHECK_API_OFFSET(submitEditorVisibility, 25);
CHECK_API_OFFSET(captureEditorPresentationFrameRgba, 26);
CHECK_API_OFFSET(submitEditorModelScene, 27);
CHECK_API_OFFSET(getEditorCompletedFrame, 28);
CHECK_API_OFFSET(submitModelSceneLayer, 29);
CHECK_API_OFFSET(updateModelSceneLayer, 30);
CHECK_API_OFFSET(removeModelSceneLayer, 31);
CHECK_API_OFFSET(getModelLayersTarget, 32);
CHECK_API_OFFSET(captureLayeredFrameRgba, 33);
CHECK_API_OFFSET(getFrameMetrics, 34);
CHECK_API_OFFSET(submitEditorInstanceRoster, 35);
CHECK_API_OFFSET(getEditorInstanceRosterResult, 36);
CHECK_API_OFFSET(getEditorRosterCapabilities, 37);
CHECK_API_OFFSET(cancelEditorInstanceRoster, 38);
CHECK_API_OFFSET(getModelLayerCapabilities, 39);
CHECK_API_OFFSET(getStaticModelSamplerCapabilities, 40);
#undef CHECK_API_OFFSET
static_assert(offsetof(War3RenderPluginApiV2, getStaticModelSamplerCapabilities) == sizeof(FrozenApiPrefix));
static_assert(offsetof(War3RenderPluginApiV2, getStaticModelSamplerCapabilities)
    + sizeof(War3RenderPluginApiV2::getStaticModelSamplerCapabilities) == sizeof(FrozenApiPrefix) + sizeof(void(*)()));
static_assert(WAR3_RENDER_STATIC_LAYER_SAMPLERS_V1 == 1);
static_assert(WAR3_RENDER_STATIC_SAMPLER_U_WRAP == 1 && WAR3_RENDER_STATIC_SAMPLER_V_WRAP == 2);
static_assert(WAR3_RENDER_STATIC_SAMPLER_FLAGS_MASK == 3 && WAR3_RENDER_STATIC_SAMPLER_LEGACY_FLAGS == 3);

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void CheckFrozenSceneBytes() {
    const std::array<War3RenderModelLayerV2, 4> layers{{
        {7, 0, 0, 1.0f}, {7, 1, WAR3_RENDER_MODEL_UNSHADED, .75f},
        {7, 2, WAR3_RENDER_MODEL_UNFOGGED, .5f}, {7, 3, WAR3_RENDER_MODEL_NO_DEPTH_WRITE, .25f}}};
    const std::array<std::uint32_t, 4> flags{0, 1, 2, 3};
    War3RenderStaticSamplerModelSceneV2 input{};
    input.base.structSize = sizeof(input);
    input.base.vertexCount = 13; input.base.indexCount = 21;
    input.base.layerCount = static_cast<std::uint32_t>(layers.size());
    input.base.meshCount = 4; input.base.modelCount = 1; input.base.instanceCount = 2; input.base.textureCount = 8;
    input.base.layers = layers.data();
    input.samplerFormat = WAR3_RENDER_STATIC_LAYER_SAMPLERS_V1;
    input.samplerFlagCount = static_cast<std::uint32_t>(flags.size()); input.samplerFlags = flags.data();
    FrozenStaticScene frozen{};
    std::memcpy(&frozen, &input, sizeof(frozen));
    Check(frozen.structSize == sizeof(input) && frozen.vertexCount == 13 && frozen.indexCount == 21
        && frozen.layerCount == 4 && frozen.meshCount == 4 && frozen.modelCount == 1
        && frozen.instanceCount == 2 && frozen.textureCount == 8, "frozen scene count prefix moved");
    const std::array<FrozenLayer, 4> expected{{{7, 0, 0, 1.0f}, {7, 1, 1, .75f}, {7, 2, 2, .5f}, {7, 3, 4, .25f}}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        FrozenLayer actual{};
        std::memcpy(&actual, reinterpret_cast<const unsigned char*>(frozen.layers) + i * sizeof(FrozenLayer), sizeof(actual));
        Check(actual.textureIndex == expected[i].textureIndex && actual.filterMode == expected[i].filterMode
            && actual.flags == expected[i].flags && actual.alpha == expected[i].alpha,
            "sampler tail changed frozen material flags, filter, alpha or stride");
        Check(input.samplerFlags[i] == i, "parallel sampler pool lost absolute layer identity");
    }
    // 旧descriptor的大小、字节和默认语义不因新尾部出现而改变。
    War3RenderModelSceneV2 legacy{};
    legacy.structSize = sizeof(legacy); legacy.layerCount = 4; legacy.layers = layers.data();
    FrozenStaticScene old{}; std::memcpy(&old, &legacy, sizeof(old));
    Check(old.structSize == sizeof(FrozenStaticScene) && old.layerCount == 4,
        "legacy static descriptor no longer has its original complete prefix");
}

std::uint32_t queryCalls = 0;
std::uint32_t advertisedSize = sizeof(War3RenderStaticSamplerCapabilitiesV2);
std::uint32_t advertisedFormat = WAR3_RENDER_STATIC_LAYER_SAMPLERS_V1;
std::uint32_t advertisedMask = WAR3_RENDER_STATIC_SAMPLER_FLAGS_MASK;
std::uint32_t advertisedReserved = 0;

// 仅ABI provider夹具；不实现生产输入校验/深拷贝/预算，也不声称设备能力。
std::int32_t WAR3_RENDER_CALL CapabilityFixture(War3RendererHandle, War3RenderStaticSamplerCapabilitiesV2* output) {
    ++queryCalls;
    if (!output || output->structSize < sizeof(*output)) return -1;
    const War3RenderStaticSamplerCapabilitiesV2 value{advertisedSize, advertisedFormat, advertisedMask, advertisedReserved};
    std::memcpy(output, &value, sizeof(value));
    return 0;
}

// 冻结consumer夹具：短表不能读取/调用尾部；有地址也不等于格式已协商。
bool ConsumerFixture(const War3RenderPluginApiV2& api) {
    if (api.structSize < offsetof(War3RenderPluginApiV2, getStaticModelSamplerCapabilities)
            + sizeof(api.getStaticModelSamplerCapabilities) || !api.getStaticModelSamplerCapabilities) return false;
    War3RenderStaticSamplerCapabilitiesV2 query{}; query.structSize = sizeof(query);
    return api.getStaticModelSamplerCapabilities(nullptr, &query) == 0
        && query.structSize == sizeof(query)
        && query.samplerFormat == WAR3_RENDER_STATIC_LAYER_SAMPLERS_V1
        && query.supportedSamplerFlags == WAR3_RENDER_STATIC_SAMPLER_FLAGS_MASK && !query.reserved;
}

void CheckCapabilityContract() {
    War3RenderPluginApiV2 api{};
    api.structSize = sizeof(FrozenApiPrefix); api.apiVersion = WAR3_RENDER_PLUGIN_API_V2;
    api.getStaticModelSamplerCapabilities = CapabilityFixture;
    Check(!ConsumerFixture(api) && queryCalls == 0, "old API prefix exposed or called a new function");
    api.structSize = sizeof(api); api.getStaticModelSamplerCapabilities = nullptr;
    Check(!ConsumerFixture(api) && queryCalls == 0, "null capability function was accepted");
    api.getStaticModelSamplerCapabilities = CapabilityFixture;
    Check(ConsumerFixture(api) && queryCalls == 1, "explicit static sampler capability not negotiated");
    advertisedSize = sizeof(War3RenderStaticSamplerCapabilitiesV2) - 1;
    Check(!ConsumerFixture(api), "partial capability response accepted");
    advertisedSize = sizeof(War3RenderStaticSamplerCapabilitiesV2) + 1;
    Check(!ConsumerFixture(api), "unknown capability response size accepted");
    advertisedSize = sizeof(War3RenderStaticSamplerCapabilitiesV2);
    advertisedFormat = 2;
    Check(!ConsumerFixture(api), "unknown capability format guessed from API table size");
    advertisedFormat = WAR3_RENDER_STATIC_LAYER_SAMPLERS_V1; advertisedMask = 7;
    Check(!ConsumerFixture(api), "unknown sampler capability bits accepted");
    advertisedMask = 1;
    Check(!ConsumerFixture(api), "partial sampler capability mistaken for all four modes");
    advertisedMask = WAR3_RENDER_STATIC_SAMPLER_FLAGS_MASK; advertisedReserved = 1;
    Check(!ConsumerFixture(api), "nonzero reserved capability accepted");
    advertisedReserved = 0;

    struct LargerOutput { War3RenderStaticSamplerCapabilitiesV2 base; std::array<unsigned char, 13> tail; };
    static_assert(offsetof(LargerOutput, tail) == sizeof(War3RenderStaticSamplerCapabilitiesV2));
    LargerOutput larger{}; larger.base.structSize = sizeof(larger); larger.tail.fill(0xa5);
    Check(api.getStaticModelSamplerCapabilities(nullptr, &larger.base) == 0
        && larger.base.structSize == sizeof(War3RenderStaticSamplerCapabilitiesV2)
        && larger.base.samplerFormat == 1 && larger.base.supportedSamplerFlags == 3 && !larger.base.reserved,
        "known capability output prefix is not explicit");
    for (auto byte : larger.tail) Check(byte == 0xa5, "capability query overwrote caller's unknown tail");
    War3RenderStaticSamplerCapabilitiesV2 shortOutput{sizeof(shortOutput) - 1, 0xabab, 0xcdcd, 0xefef};
    const auto original = shortOutput;
    Check(api.getStaticModelSamplerCapabilities(nullptr, &shortOutput) != 0
        && std::memcmp(&shortOutput, &original, sizeof(original)) == 0,
        "short capability output was accepted or modified");
}
}

int main() {
    try {
        CheckFrozenSceneBytes();
        CheckCapabilityContract();
        std::cout << "static-sampler ABI: frozen 40-function prefix, scene/layer bytes, four layer identities, capability fixtures passed; no provider/GPU proof\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
