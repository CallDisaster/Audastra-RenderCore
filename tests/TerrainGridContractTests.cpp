// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "War3MapCore/RenderPluginTerrainGrid.h"
#include "War3RenderCore/Legacy/RenderPluginTerrainGrid.h"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
// 独立冻结467的全部43个slot及其真实签名，不用当前API别名充当旧consumer。
struct FrozenApi43 {
    std::uint32_t structSize, apiVersion;
    War3RendererHandle (WAR3_RENDER_CALL* createRenderer)(const War3RenderCreateInfoV2*);
    void (WAR3_RENDER_CALL* destroyRenderer)(War3RendererHandle);
    std::int32_t (WAR3_RENDER_CALL* attachNativeWindow)(War3RendererHandle, void*);
    std::int32_t (WAR3_RENDER_CALL* resize)(War3RendererHandle, std::uint32_t, std::uint32_t);
    std::int32_t (WAR3_RENDER_CALL* submitMesh)(War3RendererHandle, const War3RenderMeshV2*);
    std::int32_t (WAR3_RENDER_CALL* setCamera)(War3RendererHandle, const War3RenderCameraV2*);
    void (WAR3_RENDER_CALL* requestFrame)(War3RendererHandle);
    std::int32_t (WAR3_RENDER_CALL* getStatus)(War3RendererHandle, War3RenderStatusV2*);
    std::int32_t (WAR3_RENDER_CALL* captureRgba)(War3RendererHandle, void*, std::uint64_t, War3RenderCaptureInfoV2*);
    std::int32_t (WAR3_RENDER_CALL* getViewInfo)(War3RendererHandle, War3RenderViewInfoV2*);
    std::int32_t (WAR3_RENDER_CALL* submitTerrainScene)(War3RendererHandle, const War3RenderTerrainSceneV2*);
    std::int32_t (WAR3_RENDER_CALL* submitTerrainMipScene)(War3RendererHandle, const War3RenderTerrainMipSceneV2*);
    std::int32_t (WAR3_RENDER_CALL* captureFrameRgba)(War3RendererHandle, void*, std::uint64_t, War3RenderCapturedFrameV2*);
    std::int32_t (WAR3_RENDER_CALL* submitModelScene)(War3RendererHandle, const War3RenderModelSceneV2*);
    std::int32_t (WAR3_RENDER_CALL* setEnvironment)(War3RendererHandle, const War3RenderEnvironmentV2*);
    std::int32_t (WAR3_RENDER_CALL* updateModelInstances)(War3RendererHandle, const War3RenderModelInstanceV2*, std::uint32_t);
    std::int32_t (WAR3_RENDER_CALL* setOverlayLines)(War3RendererHandle, const War3RenderOverlayLineV2*, std::uint32_t);
    std::int32_t (WAR3_RENDER_CALL* submitSkinnedModelScene)(War3RendererHandle, const War3RenderSkinnedModelSceneV2*);
    std::int32_t (WAR3_RENDER_CALL* updateSkinMatrices)(War3RendererHandle, const War3RenderSkinUpdateV2*);
    std::int32_t (WAR3_RENDER_CALL* getSkinStatus)(War3RendererHandle, War3RenderSkinStatusV2*);
    std::int32_t (WAR3_RENDER_CALL* captureSkinnedFrameRgba)(War3RendererHandle, void*, std::uint64_t, War3RenderSkinnedCapturedFrameV2*);
    std::int32_t (WAR3_RENDER_CALL* submitParticleModelScene)(War3RendererHandle, const War3RenderParticleSceneV2*);
    std::int32_t (WAR3_RENDER_CALL* updateParticleModelFrame)(War3RendererHandle, const War3RenderParticleUpdateV2*);
    std::int32_t (WAR3_RENDER_CALL* captureParticleFrameRgba)(War3RendererHandle, void*, std::uint64_t, War3RenderParticleCapturedFrameV2*);
    std::int32_t (WAR3_RENDER_CALL* getEditorPresentationTarget)(War3RendererHandle, War3RenderEditorPresentationTargetV2*);
    std::int32_t (WAR3_RENDER_CALL* submitEditorVisibility)(War3RendererHandle, const War3RenderEditorVisibilityUpdateV2*);
    std::int32_t (WAR3_RENDER_CALL* captureEditorPresentationFrameRgba)(War3RendererHandle, std::uint64_t,
        std::uint64_t, void*, std::uint64_t, War3RenderEditorCapturedFrameV2*);
    std::int32_t (WAR3_RENDER_CALL* submitEditorModelScene)(War3RendererHandle, const War3RenderEditorModelSceneV2*);
    std::int32_t (WAR3_RENDER_CALL* getEditorCompletedFrame)(War3RendererHandle, War3RenderEditorCompletedFrameV2*);
    std::int32_t (WAR3_RENDER_CALL* submitModelSceneLayer)(War3RendererHandle, const War3RenderModelSceneLayerV2*);
    std::int32_t (WAR3_RENDER_CALL* updateModelSceneLayer)(War3RendererHandle, const War3RenderModelLayerUpdateV2*);
    std::int32_t (WAR3_RENDER_CALL* removeModelSceneLayer)(War3RendererHandle, std::uint64_t, std::uint64_t, std::uint64_t);
    std::int32_t (WAR3_RENDER_CALL* getModelLayersTarget)(War3RendererHandle, War3RenderModelLayersTargetV2*);
    std::int32_t (WAR3_RENDER_CALL* captureLayeredFrameRgba)(War3RendererHandle, std::uint64_t,
        void*, std::uint64_t, War3RenderLayeredCapturedFrameV2*);
    std::int32_t (WAR3_RENDER_CALL* getFrameMetrics)(War3RendererHandle, std::uint64_t,
        War3RenderFrameMetricsV2*, std::uint32_t, War3RenderFrameMetricsPageV2*);
    std::int32_t (WAR3_RENDER_CALL* submitEditorInstanceRoster)(War3RendererHandle, const War3RenderEditorInstanceRosterV2*, std::uint64_t*);
    std::int32_t (WAR3_RENDER_CALL* getEditorInstanceRosterResult)(War3RendererHandle, std::uint64_t, War3RenderInstanceRosterResultV2*);
    std::int32_t (WAR3_RENDER_CALL* getEditorRosterCapabilities)(War3RendererHandle, std::uint32_t*);
    std::int32_t (WAR3_RENDER_CALL* cancelEditorInstanceRoster)(War3RendererHandle, std::uint64_t, War3RenderInstanceRosterResultV2*);
    std::int32_t (WAR3_RENDER_CALL* getModelLayerCapabilities)(War3RendererHandle, std::uint32_t*);
    std::int32_t (WAR3_RENDER_CALL* getStaticModelSamplerCapabilities)(War3RendererHandle, War3RenderStaticSamplerCapabilitiesV2*);
    std::int32_t (WAR3_RENDER_CALL* getTerrainBlightCapabilities)(War3RendererHandle, War3RenderTerrainBlightCapabilitiesV2*);
    std::int32_t (WAR3_RENDER_CALL* submitTerrainBlightFrame)(War3RendererHandle, const War3RenderTerrainBlightFrameV2*);
};
#define FROZEN_SLOT(member, index) \
    static_assert(offsetof(FrozenApi43, member) == 8 + index * sizeof(void(*)())); \
    static_assert(offsetof(War3RenderPluginApiV2, member) == offsetof(FrozenApi43, member)); \
    static_assert(std::is_same_v<decltype(FrozenApi43::member), decltype(War3RenderPluginApiV2::member)>)
FROZEN_SLOT(createRenderer, 0); FROZEN_SLOT(destroyRenderer, 1); FROZEN_SLOT(attachNativeWindow, 2);
FROZEN_SLOT(resize, 3); FROZEN_SLOT(submitMesh, 4); FROZEN_SLOT(setCamera, 5); FROZEN_SLOT(requestFrame, 6);
FROZEN_SLOT(getStatus, 7); FROZEN_SLOT(captureRgba, 8); FROZEN_SLOT(getViewInfo, 9);
FROZEN_SLOT(submitTerrainScene, 10); FROZEN_SLOT(submitTerrainMipScene, 11); FROZEN_SLOT(captureFrameRgba, 12);
FROZEN_SLOT(submitModelScene, 13); FROZEN_SLOT(setEnvironment, 14); FROZEN_SLOT(updateModelInstances, 15);
FROZEN_SLOT(setOverlayLines, 16); FROZEN_SLOT(submitSkinnedModelScene, 17); FROZEN_SLOT(updateSkinMatrices, 18);
FROZEN_SLOT(getSkinStatus, 19); FROZEN_SLOT(captureSkinnedFrameRgba, 20); FROZEN_SLOT(submitParticleModelScene, 21);
FROZEN_SLOT(updateParticleModelFrame, 22); FROZEN_SLOT(captureParticleFrameRgba, 23);
FROZEN_SLOT(getEditorPresentationTarget, 24); FROZEN_SLOT(submitEditorVisibility, 25);
FROZEN_SLOT(captureEditorPresentationFrameRgba, 26); FROZEN_SLOT(submitEditorModelScene, 27);
FROZEN_SLOT(getEditorCompletedFrame, 28); FROZEN_SLOT(submitModelSceneLayer, 29); FROZEN_SLOT(updateModelSceneLayer, 30);
FROZEN_SLOT(removeModelSceneLayer, 31); FROZEN_SLOT(getModelLayersTarget, 32); FROZEN_SLOT(captureLayeredFrameRgba, 33);
FROZEN_SLOT(getFrameMetrics, 34); FROZEN_SLOT(submitEditorInstanceRoster, 35); FROZEN_SLOT(getEditorInstanceRosterResult, 36);
FROZEN_SLOT(getEditorRosterCapabilities, 37); FROZEN_SLOT(cancelEditorInstanceRoster, 38);
FROZEN_SLOT(getModelLayerCapabilities, 39); FROZEN_SLOT(getStaticModelSamplerCapabilities, 40);
FROZEN_SLOT(getTerrainBlightCapabilities, 41); FROZEN_SLOT(submitTerrainBlightFrame, 42);
#undef FROZEN_SLOT
using CapsQuery = std::int32_t (WAR3_RENDER_CALL*)(War3RendererHandle, War3RenderTerrainGridCapabilitiesV2*);
using Submit = std::int32_t (WAR3_RENDER_CALL*)(War3RendererHandle, const War3RenderTerrainGridFrameV2*);
using FactsQuery = std::int32_t (WAR3_RENDER_CALL*)(War3RendererHandle, const War3RenderViewInfoV2*, War3RenderTerrainGridCaptureFactsV2*);
static_assert(std::is_same_v<decltype(War3RenderPluginApiV2::getTerrainGridCapabilities), CapsQuery>);
static_assert(std::is_same_v<decltype(War3RenderPluginApiV2::submitTerrainGridFrame), Submit>);
static_assert(std::is_same_v<decltype(War3RenderPluginApiV2::getTerrainGridCaptureFacts), FactsQuery>);
static_assert(offsetof(War3RenderPluginApiV2, getTerrainGridCapabilities) == sizeof(FrozenApi43));
static_assert(offsetof(War3RenderPluginApiV2, submitTerrainGridFrame) == 8 + 44 * sizeof(void(*)()));
static_assert(offsetof(War3RenderPluginApiV2, getTerrainGridCaptureFacts) == 8 + 45 * sizeof(void(*)()));
static_assert(sizeof(War3RenderPluginApiV2) == 8 + 46 * sizeof(void(*)()));

#define AT(type, member, offset) static_assert(offsetof(type, member) == offset)
#define GROUND(member, index) AT(War3RenderTerrainGridGroundVertexV2, member, index * sizeof(float))
GROUND(x, 0); GROUND(y, 1); GROUND(groundZ, 2); GROUND(textureR, 3); GROUND(textureG, 4);
GROUND(textureB, 5); GROUND(fallbackR, 6); GROUND(fallbackG, 7); GROUND(fallbackB, 8);
#undef GROUND
#define CAPS(member, index) \
    AT(War3RenderTerrainGridCapabilitiesV2, member, index * 4); \
    static_assert(std::is_same_v<decltype(War3RenderTerrainGridCapabilitiesV2::member), std::uint32_t>)
CAPS(structSize, 0); CAPS(gridFormat, 1); CAPS(maxGroundTextures, 2);
CAPS(maxBlightBatches, 3); CAPS(maxWaterTextures, 4); CAPS(reserved, 5);
#undef CAPS
AT(War3RenderTerrainGridWaterVertexV2, waterZ, 0); AT(War3RenderTerrainGridWaterVertexV2, red, 4);
AT(War3RenderTerrainGridWaterVertexV2, green, 8); AT(War3RenderTerrainGridWaterVertexV2, blue, 12);
AT(War3RenderTerrainGridBaseQuadV2, sourceTile, 0);
AT(War3RenderTerrainGridWaterQuadV2, sourceTile, 0); AT(War3RenderTerrainGridWaterQuadV2, alphaLevel, 4);
#define FRAME(member, index) AT(War3RenderTerrainGridFrameV2, member, index * 4)
FRAME(structSize, 0); FRAME(gridFormat, 1); FRAME(width, 2); FRAME(height, 3); FRAME(gridVertexCount, 4);
FRAME(baseQuadCount, 5); FRAME(baseIndexCount, 6); FRAME(baseBatchCount, 7); FRAME(blightIndexCount, 8);
FRAME(blightBatchCount, 9); FRAME(waterQuadCount, 10); FRAME(waterIndexCount, 11);
#undef FRAME
AT(War3RenderTerrainGridFrameV2, groundScene, 48);
#define PTR(member, index, type) \
    AT(War3RenderTerrainGridFrameV2, member, 48 + sizeof(War3RenderTerrainMipSceneV2) + index * sizeof(void*)); \
    static_assert(std::is_same_v<decltype(War3RenderTerrainGridFrameV2::member), const type*>)
PTR(groundGrid, 0, War3RenderTerrainGridGroundVertexV2); PTR(waterGrid, 1, War3RenderTerrainGridWaterVertexV2);
PTR(baseQuads, 2, War3RenderTerrainGridBaseQuadV2); PTR(baseIndices, 3, std::uint32_t);
PTR(baseBatches, 4, War3RenderTerrainBatchV2); PTR(blightIndices, 5, std::uint32_t);
PTR(blightBatches, 6, War3RenderTerrainBlightBatchV2); PTR(blightTexture, 7, War3RenderTextureMipChainV2);
PTR(waterQuads, 8, War3RenderTerrainGridWaterQuadV2); PTR(waterIndices, 9, std::uint32_t);
PTR(waterTexture, 10, War3RenderTextureMipChainV2);
#undef PTR
AT(War3RenderTerrainGridCaptureFactsV2, structSize, 0); AT(War3RenderTerrainGridCaptureFactsV2, gridFormat, 4);
AT(War3RenderTerrainGridCaptureFactsV2, view, 8);
static_assert(std::is_same_v<decltype(War3RenderTerrainGridCaptureFactsV2::view), War3RenderViewInfoV2>);
#define FACT(member, index) \
    AT(War3RenderTerrainGridCaptureFactsV2, member, 8 + sizeof(War3RenderViewInfoV2) + index * 4); \
    static_assert(std::is_same_v<decltype(War3RenderTerrainGridCaptureFactsV2::member), std::uint32_t>)
FACT(gridVertexCount, 0); FACT(baseQuadCount, 1); FACT(transitionVertexCount, 2); FACT(blightIndexCount, 3);
FACT(blightBatchCount, 4); FACT(waterQuadCount, 5); FACT(groundTextureCount, 6); FACT(passBits, 7);
#undef FACT
#undef AT
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(WAR3_RENDER_TERRAIN_GRID_FETCH_V1 == 1);
static_assert(WAR3_RENDER_TERRAIN_GRID_HAS_BLIGHT == 1 && WAR3_RENDER_TERRAIN_GRID_HAS_WATER == 2);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_DIMENSION == 2049 && WAR3_RENDER_TERRAIN_GRID_MAX_SOURCE_TILES == 262144);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_VERTEX_RECORDS == 1048576 && WAR3_RENDER_TERRAIN_GRID_MAX_INDICES == 6291456);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_BATCHES == 4096 && WAR3_RENDER_TERRAIN_GRID_MAX_GROUND_TEXTURES == 16);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_BLIGHT_BATCHES == 32 && WAR3_RENDER_TERRAIN_GRID_MAX_WATER_TEXTURES == 1);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_TEXTURE_DIMENSION == 2048 && WAR3_RENDER_TERRAIN_GRID_MAX_MIPS == 12);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_MIP_BYTES == 16 * 1024 * 1024);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_CHAIN_BYTES == 24 * 1024 * 1024);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_RGBA_BYTES == 64 * 1024 * 1024);
static_assert(WAR3_RENDER_TERRAIN_GRID_MAX_WORKING_BYTES == 128 * 1024 * 1024);

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::uint32_t queryCalls = 0;
std::int32_t queryResult = 0;
War3RenderTerrainGridCapabilitiesV2 advertised{sizeof(advertised), 1, 16, 32, 1, 0};

// 以下仅是纯ABI协商/输出字节夹具，不实现provider几何校验、容量/深拷贝、GPU或来源证明。
std::int32_t WAR3_RENDER_CALL CapsFixture(War3RendererHandle, War3RenderTerrainGridCapabilitiesV2* output) {
    ++queryCalls;
    if (queryResult) return queryResult;
    if (!output || output->structSize < sizeof(*output)) return -1;
    std::memcpy(output, &advertised, sizeof(advertised));
    return 0;
}
std::int32_t WAR3_RENDER_CALL SubmitFixture(War3RendererHandle, const War3RenderTerrainGridFrameV2*) { return 17; }
bool Consumer(const War3RenderPluginApiV2& api) {
    if (api.apiVersion != WAR3_RENDER_PLUGIN_API_V2
        || api.structSize < offsetof(War3RenderPluginApiV2, getTerrainGridCaptureFacts) + sizeof(api.getTerrainGridCaptureFacts)
        || !api.getTerrainGridCapabilities || !api.submitTerrainGridFrame || !api.getTerrainGridCaptureFacts) return false;
    War3RenderTerrainGridCapabilitiesV2 caps{}; caps.structSize = sizeof(caps);
    return api.getTerrainGridCapabilities(nullptr, &caps) == 0 && caps.structSize == sizeof(caps)
        && caps.gridFormat == 1 && caps.maxGroundTextures == 16 && caps.maxBlightBatches == 32
        && caps.maxWaterTextures == 1 && caps.reserved == 0;
}

bool SameCaptured(const War3RenderViewInfoV2& a, const War3RenderViewInfoV2& b) {
    return a.structSize == b.structSize && a.width == b.width && a.height == b.height
        && a.frameCount == b.frameCount && a.documentRevision == b.documentRevision
        && a.surfaceEpoch == b.surfaceEpoch && a.sceneEpoch == b.sceneEpoch && a.viewEpoch == b.viewEpoch
        && std::memcmp(a.viewProjection, b.viewProjection, sizeof(a.viewProjection)) == 0;
}
War3RenderTerrainGridCaptureFactsV2 captured{};
War3RenderViewInfoV2 current{};
bool grid = true, pending = false, uploadError = false;
int fixtureHandle;
std::int32_t WAR3_RENDER_CALL FactsFixture(War3RendererHandle handle, const War3RenderViewInfoV2* expected,
    War3RenderTerrainGridCaptureFactsV2* output) {
    if (handle != &fixtureHandle || !expected || expected->structSize != sizeof(*expected)
        || !output || output->structSize < sizeof(*output) || captured.gridFormat != 1 || (captured.passBits & ~3u)) return -1;
    auto state = current; state.frameCount = captured.view.frameCount;
    if (!grid || pending || uploadError || !SameCaptured(*expected, captured.view) || !SameCaptured(state, captured.view)) return 2;
    std::memcpy(output, &captured, sizeof(captured));
    return 0;
}

void CheckCapabilities() {
    War3RenderPluginApiV2 api{}; api.apiVersion = WAR3_RENDER_PLUGIN_API_V2;
    api.getTerrainGridCapabilities = CapsFixture; api.submitTerrainGridFrame = SubmitFixture;
    api.getTerrainGridCaptureFacts = FactsFixture;
    for (std::size_t bytes : {sizeof(FrozenApi43), offsetof(War3RenderPluginApiV2, submitTerrainGridFrame),
                             offsetof(War3RenderPluginApiV2, getTerrainGridCaptureFacts), sizeof(api) - 1}) {
        api.structSize = static_cast<std::uint32_t>(bytes);
        Check(!Consumer(api) && queryCalls == 0, "partial table read/called a new slot");
    }
    api.structSize = sizeof(api); auto saved = api;
    api.getTerrainGridCapabilities = nullptr; Check(!Consumer(api) && queryCalls == 0, "missing caps accepted");
    api = saved; api.submitTerrainGridFrame = nullptr; Check(!Consumer(api) && queryCalls == 0, "missing submit accepted");
    api = saved; api.getTerrainGridCaptureFacts = nullptr; Check(!Consumer(api) && queryCalls == 0, "missing facts accepted");
    api = saved; api.apiVersion = 0; Check(!Consumer(api) && queryCalls == 0, "unknown API accepted"); api = saved;
    Check(Consumer(api) && queryCalls == 1 && api.submitTerrainGridFrame(nullptr, nullptr) == 17,
        "null-renderer compile capability or exact function signatures failed");
    const auto valid = advertised;
    for (auto member : {&War3RenderTerrainGridCapabilitiesV2::structSize, &War3RenderTerrainGridCapabilitiesV2::gridFormat,
                       &War3RenderTerrainGridCapabilitiesV2::maxGroundTextures, &War3RenderTerrainGridCapabilitiesV2::maxBlightBatches,
                       &War3RenderTerrainGridCapabilitiesV2::maxWaterTextures, &War3RenderTerrainGridCapabilitiesV2::reserved}) {
        advertised = valid; ++(advertised.*member); Check(!Consumer(api), "unknown capability field accepted");
        advertised = valid; if (advertised.*member) --(advertised.*member);
        else advertised.*member = 1;
        Check(!Consumer(api), "partial capability field accepted");
    }
    advertised = valid; queryResult = -7; Check(!Consumer(api), "query failure enabled mode"); queryResult = 0;
    struct Larger { War3RenderTerrainGridCapabilitiesV2 base; std::array<unsigned char, 13> tail; };
    Larger larger{}; larger.base.structSize = sizeof(larger); larger.tail.fill(0xa5);
    Check(CapsFixture(nullptr, &larger.base) == 0 && std::memcmp(&larger.base, &valid, sizeof(valid)) == 0,
        "caps known prefix was not written exactly");
    for (auto byte : larger.tail) Check(byte == 0xa5, "caps unknown future tail overwritten");
    auto shortOutput = valid; --shortOutput.structSize; const auto before = shortOutput;
    Check(CapsFixture(nullptr, &shortOutput) != 0 && std::memcmp(&before, &shortOutput, sizeof(before)) == 0,
        "short caps output written");
    shortOutput = valid; queryResult = -7;
    Check(CapsFixture(nullptr, &shortOutput) == -7 && std::memcmp(&valid, &shortOutput, sizeof(valid)) == 0,
        "failed caps output written"); queryResult = 0;
}

void CheckFacts() {
    captured.structSize = sizeof(captured); captured.gridFormat = 1;
    auto& view = captured.view; view.structSize = sizeof(view); view.width = 640; view.height = 480;
    view.frameCount = 101; view.documentRevision = 202; view.surfaceEpoch = 303; view.sceneEpoch = 404; view.viewEpoch = 505;
    for (std::size_t i = 0; i < 16; ++i) view.viewProjection[i] = static_cast<float>(i) + .25f;
    captured.gridVertexCount = 9; captured.baseQuadCount = 4; captured.transitionVertexCount = 4;
    captured.blightIndexCount = 6; captured.blightBatchCount = 1; captured.waterQuadCount = 1;
    captured.groundTextureCount = 16; captured.passBits = 3; current = view;
    struct Larger { War3RenderTerrainGridCaptureFactsV2 base; std::array<unsigned char, 13> tail; };
    Larger larger{}; larger.base.structSize = sizeof(larger); larger.tail.fill(0xa5);
    Check(FactsFixture(&fixtureHandle, &view, &larger.base) == 0
        && std::memcmp(&larger.base, &captured, sizeof(captured)) == 0, "facts known prefix differs");
    for (auto byte : larger.tail) Check(byte == 0xa5, "facts unknown future tail overwritten");
    auto rejects = [&](const War3RenderViewInfoV2* expected, std::int32_t code = 2) {
        War3RenderTerrainGridCaptureFactsV2 output{}; output.structSize = sizeof(output); output.passBits = 0xabab;
        const auto before = output;
        Check(FactsFixture(&fixtureHandle, expected, &output) == code
            && std::memcmp(&before, &output, sizeof(output)) == 0, "stale/invalid facts output written");
    };
    auto other = view; --other.structSize; rejects(&other, -1); rejects(nullptr, -1);
    other = view; ++other.width; rejects(&other); other = view; ++other.height; rejects(&other);
    for (auto member : {&War3RenderViewInfoV2::frameCount, &War3RenderViewInfoV2::documentRevision,
                       &War3RenderViewInfoV2::surfaceEpoch, &War3RenderViewInfoV2::sceneEpoch, &War3RenderViewInfoV2::viewEpoch}) {
        other = view; ++(other.*member); rejects(&other);
    }
    for (std::size_t i = 0; i < 16; ++i) { other = view; other.viewProjection[i] += 1; rejects(&other); }
    pending = true; rejects(&view); pending = false; uploadError = true; rejects(&view); uploadError = false;
    grid = false; rejects(&view); grid = true;
    current.sceneEpoch++; rejects(&view); current = view; current.documentRevision++; rejects(&view); current = view;
    current.surfaceEpoch++; rejects(&view); current = view; current.viewEpoch++; rejects(&view); current = view;
    current.width++; rejects(&view); current = view; current.height++; rejects(&view); current = view;
    current.viewProjection[15] += 1; rejects(&view); current = view;
    current.frameCount++; // 同状态后续普通frame不使既有同次capture失效。
    War3RenderTerrainGridCaptureFactsV2 output{}; output.structSize = sizeof(output);
    Check(FactsFixture(&fixtureHandle, &view, &output) == 0, "ordinary later frame invalidated same-state capture");
    current = view; captured.passBits = 4; rejects(&view, -1); captured.passBits = 3;
    captured.gridFormat = 2; rejects(&view, -1); captured.gridFormat = 1;
    output.structSize = sizeof(output) - 1; const auto shortBefore = output;
    Check(FactsFixture(&fixtureHandle, &view, &output) == -1
        && std::memcmp(&shortBefore, &output, sizeof(output)) == 0, "short facts output written");
}

bool EmptyFrame(const War3RenderTerrainGridFrameV2& frame) {
    const auto& scene = frame.groundScene;
    const std::array<std::uint32_t, 10> counts{frame.width, frame.height, frame.gridVertexCount, frame.baseQuadCount,
        frame.baseIndexCount, frame.baseBatchCount, frame.blightIndexCount, frame.blightBatchCount, frame.waterQuadCount, frame.waterIndexCount};
    const std::array<const void*, 15> pointers{frame.groundGrid, frame.waterGrid, frame.baseQuads, frame.baseIndices,
        frame.baseBatches, frame.blightIndices, frame.blightBatches, frame.blightTexture, frame.waterQuads,
        frame.waterIndices, frame.waterTexture, scene.vertices, scene.indices, scene.textures, scene.batches};
    if (frame.structSize != sizeof(frame) || frame.gridFormat != 1 || scene.structSize != sizeof(scene)
        || scene.vertexCount || scene.indexCount || scene.textureCount || scene.batchCount) return false;
    for (auto count : counts) if (count) return false;
    for (auto pointer : pointers) if (pointer) return false;
    return true;
}
void CheckEmptyAndBytes() {
    War3RenderTerrainGridFrameV2 empty{}; empty.structSize = sizeof(empty); empty.gridFormat = 1;
    empty.groundScene.structSize = sizeof(empty.groundScene); empty.groundScene.documentRevision = 99;
    Check(EmptyFrame(empty), "explicit revision empty frame not representable");
    for (auto member : {&War3RenderTerrainGridFrameV2::width, &War3RenderTerrainGridFrameV2::height,
                       &War3RenderTerrainGridFrameV2::gridVertexCount, &War3RenderTerrainGridFrameV2::baseQuadCount,
                       &War3RenderTerrainGridFrameV2::baseIndexCount, &War3RenderTerrainGridFrameV2::baseBatchCount,
                       &War3RenderTerrainGridFrameV2::blightIndexCount, &War3RenderTerrainGridFrameV2::blightBatchCount,
                       &War3RenderTerrainGridFrameV2::waterQuadCount, &War3RenderTerrainGridFrameV2::waterIndexCount}) {
        auto bad = empty; bad.*member = 1; Check(!EmptyFrame(bad), "nonzero empty count accepted");
    }
    const War3RenderTerrainGridGroundVertexV2 ground{1, 2, 3, .1f, .2f, .3f, .4f, .5f, .6f};
    std::array<float, 9> words{}; std::memcpy(words.data(), &ground, sizeof(ground));
    Check(words == std::array<float, 9>{1, 2, 3, .1f, .2f, .3f, .4f, .5f, .6f}, "ground XYZ/color bytes reordered");
    std::uint32_t index = 0; War3RenderTerrainGridWaterVertexV2 water{};
    War3RenderTerrainGridBaseQuadV2 base{}; War3RenderTerrainGridWaterQuadV2 waterQuad{};
    War3RenderTerrainBatchV2 batch{}; War3RenderTerrainBlightBatchV2 blight{}; War3RenderTextureMipChainV2 texture{};
    auto bad = empty; bad.groundGrid = &ground; Check(!EmptyFrame(bad), "empty ground pointer accepted");
    bad = empty; bad.waterGrid = &water; Check(!EmptyFrame(bad), "empty water grid accepted");
    bad = empty; bad.baseQuads = &base; Check(!EmptyFrame(bad), "empty base quads accepted");
    bad = empty; bad.baseIndices = &index; Check(!EmptyFrame(bad), "empty base indices accepted");
    bad = empty; bad.baseBatches = &batch; Check(!EmptyFrame(bad), "empty base batches accepted");
    bad = empty; bad.blightIndices = &index; Check(!EmptyFrame(bad), "empty blight indices accepted");
    bad = empty; bad.blightBatches = &blight; Check(!EmptyFrame(bad), "empty blight batches accepted");
    bad = empty; bad.blightTexture = &texture; Check(!EmptyFrame(bad), "empty blight texture accepted");
    bad = empty; bad.waterQuads = &waterQuad; Check(!EmptyFrame(bad), "empty water quads accepted");
    bad = empty; bad.waterIndices = &index; Check(!EmptyFrame(bad), "empty water indices accepted");
    bad = empty; bad.waterTexture = &texture; Check(!EmptyFrame(bad), "empty water texture accepted");
    War3RenderTerrainVertexV2 transition{};
    bad = empty; bad.groundScene.vertices = &transition; Check(!EmptyFrame(bad), "empty transition vertices accepted");
    bad = empty; bad.groundScene.indices = &index; Check(!EmptyFrame(bad), "empty transition indices accepted");
    bad = empty; bad.groundScene.textures = &texture; Check(!EmptyFrame(bad), "empty ground table accepted");
    bad = empty; bad.groundScene.batches = &batch; Check(!EmptyFrame(bad), "empty transition batches accepted");
    bad = empty; --bad.structSize; Check(!EmptyFrame(bad), "inexact input size accepted");
    bad = empty; ++bad.gridFormat; Check(!EmptyFrame(bad), "unknown input format accepted");
    bad = empty; --bad.groundScene.structSize; Check(!EmptyFrame(bad), "inexact nested input size accepted");
}

bool OptionalPoolShape(const War3RenderTerrainGridFrameV2& frame) {
    const bool blight = frame.blightIndexCount || frame.blightBatchCount;
    if (blight ? (!frame.blightIndexCount || frame.blightIndexCount % 6 || !frame.blightBatchCount
            || frame.blightBatchCount > 32 || !frame.blightIndices || !frame.blightBatches || !frame.blightTexture)
        : (frame.blightIndices || frame.blightBatches || frame.blightTexture)) return false;
    const bool water = frame.waterQuadCount || frame.waterIndexCount;
    if (water ? (!frame.waterQuadCount || std::uint64_t(frame.waterQuadCount) * 6 != frame.waterIndexCount
            || !frame.waterGrid || !frame.waterQuads || !frame.waterIndices || !frame.waterTexture)
        : (frame.waterGrid || frame.waterQuads || frame.waterIndices || frame.waterTexture)) return false;
    return true;
}
void CheckOptionalPoolShape() {
    War3RenderTerrainGridFrameV2 frame{};
    std::uint32_t index = 0; War3RenderTerrainBlightBatchV2 batch{}; War3RenderTextureMipChainV2 texture{};
    War3RenderTerrainGridWaterVertexV2 gridVertex{}; War3RenderTerrainGridWaterQuadV2 quad{};
    Check(OptionalPoolShape(frame), "empty optional pool representation rejected");
    frame.blightIndices = &index; Check(!OptionalPoolShape(frame), "ghost empty blight pointer accepted");
    frame.blightIndexCount = 6; frame.blightBatchCount = 1; frame.blightBatches = &batch; frame.blightTexture = &texture;
    Check(OptionalPoolShape(frame), "complete blight pool not representable");
    auto bad = frame; bad.blightBatches = nullptr; Check(!OptionalPoolShape(bad), "partial nonempty blight accepted");
    frame.waterGrid = &gridVertex; Check(!OptionalPoolShape(frame), "ghost empty water grid accepted");
    frame.waterQuadCount = 1; frame.waterIndexCount = 6; frame.waterQuads = &quad;
    frame.waterIndices = &index; frame.waterTexture = &texture;
    Check(OptionalPoolShape(frame), "complete water pool not representable");
    bad = frame; bad.waterIndexCount = 0; Check(!OptionalPoolShape(bad), "partial water counts accepted");
    bad = frame; bad.waterGrid = nullptr; Check(!OptionalPoolShape(bad), "partial nonempty water accepted");
}

void CheckMaterialGroupedBaseOrder() {
    // 纯合同序列夹具：baseQuads仍按源tile升序，indices按材质取q=2,0,1；water不得照此重排。
    const std::array<War3RenderTerrainGridBaseQuadV2, 3> quads{{{4, 0, 0, 0, 0}, {8, 0, 0, 0, 0}, {15, 0, 0, 0, 0}}};
    const std::array<std::uint32_t, 18> grouped{8, 9, 10, 9, 11, 10, 0, 1, 2, 1, 3, 2, 4, 5, 6, 5, 7, 6};
    const auto covers = [](const auto& indices, bool water) {
        std::array<bool, 3> seen{};
        for (std::uint32_t group = 0; group < 3; ++group) {
            const auto first = indices[group * 6], quad = first / 4;
            if (first % 4 || quad >= 3 || seen[quad] || (water && quad != group)) return false;
            const std::array<std::uint32_t, 6> canonical{first, first + 1, first + 2, first + 1, first + 3, first + 2};
            for (std::size_t corner = 0; corner < 6; ++corner)
                if (indices[group * 6 + corner] != canonical[corner]) return false;
            seen[quad] = true;
        }
        return seen[0] && seen[1] && seen[2];
    };
    Check(quads[0].sourceTile < quads[1].sourceTile && quads[1].sourceTile < quads[2].sourceTile
        && covers(grouped, false) && !covers(grouped, true), "base material grouping was constrained to water q order");
    auto duplicate = grouped;
    for (std::size_t i = 0; i < 6; ++i) duplicate[12 + i] = grouped[i];
    Check(!covers(duplicate, false), "duplicate grouped base quad accepted by contract fixture");
}
}

int main() {
    try {
        CheckCapabilities(); CheckFacts(); CheckEmptyAndBytes(); CheckOptionalPoolShape(); CheckMaterialGroupedBaseOrder();
        std::cout << "terrain-grid ABI: frozen 43-slot prefix, 46-slot table, 24/200/160 x64 layout and pure contract fixtures passed; no provider/GPU proof\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
