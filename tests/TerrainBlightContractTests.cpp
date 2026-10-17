// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "War3MapCore/RenderPluginTerrainBlight.h"
#include "War3RenderCore/Legacy/RenderPluginTerrainBlight.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
// 独立冻结1440658的terrain布局，不以新类型别名充当旧consumer。
struct FrozenVertex { float x, y, z, red, green, blue, u, v; };
struct FrozenBatch { std::uint32_t firstIndex, indexCount, textureIndex; };
struct FrozenMipScene {
    std::uint32_t structSize, vertexCount, indexCount, textureCount, batchCount;
    std::uint64_t documentRevision;
    const FrozenVertex* vertices;
    const std::uint32_t* indices;
    const War3RenderTextureMipChainV2* textures;
    const FrozenBatch* batches;
};
struct FrozenApi41 {
    std::uint32_t structSize, apiVersion;
    void (*functions[41])();
};
static_assert(sizeof(FrozenVertex) == 32 && sizeof(FrozenVertex) == sizeof(War3RenderTerrainVertexV2));
static_assert(sizeof(FrozenBatch) == 12 && sizeof(FrozenBatch) == sizeof(War3RenderTerrainBatchV2));
static_assert(sizeof(FrozenMipScene) == sizeof(War3RenderTerrainMipSceneV2));
#define CHECK_SCENE_OFFSET(member) \
    static_assert(offsetof(FrozenMipScene, member) == offsetof(War3RenderTerrainMipSceneV2, member))
CHECK_SCENE_OFFSET(structSize);
CHECK_SCENE_OFFSET(vertexCount);
CHECK_SCENE_OFFSET(indexCount);
CHECK_SCENE_OFFSET(textureCount);
CHECK_SCENE_OFFSET(batchCount);
CHECK_SCENE_OFFSET(documentRevision);
CHECK_SCENE_OFFSET(vertices);
CHECK_SCENE_OFFSET(indices);
CHECK_SCENE_OFFSET(textures);
CHECK_SCENE_OFFSET(batches);
#undef CHECK_SCENE_OFFSET
static_assert(offsetof(War3RenderPluginApiV2, getStaticModelSamplerCapabilities) == 8 + 40 * sizeof(void(*)()));
static_assert(offsetof(War3RenderPluginApiV2, getTerrainBlightCapabilities) == sizeof(FrozenApi41));
static_assert(offsetof(War3RenderPluginApiV2, submitTerrainBlightFrame) == 8 + 42 * sizeof(void(*)()));
static_assert(sizeof(War3RenderPluginApiV2) == 8 + 43 * sizeof(void(*)()));
static_assert(WAR3_RENDER_TERRAIN_BLIGHT_SHARED_BASE_V1 == 1);
static_assert(WAR3_RENDER_TERRAIN_BLIGHT_MAX_BATCHES == 32);
static_assert(sizeof(War3RenderTerrainBlightBatchV2) == 24);
static_assert(sizeof(War3RenderTerrainBlightCapabilitiesV2) == 16);

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void CheckFrozenTerrainBytes() {
    const std::array<War3RenderTerrainVertexV2, 4> vertices{{
        {1, 2, 3, .4f, .5f, .6f, .1f, .2f}, {4, 5, 6, .7f, .8f, .9f, .3f, .4f},
        {7, 8, 9, .1f, .2f, .3f, .5f, .6f}, {10, 11, 12, .4f, .5f, .6f, .7f, .8f}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 1, 3, 2};
    const std::array<War3RenderTerrainBatchV2, 1> batches{{{0, 6, 15}}};
    // 描述符字节夹具；不作为完整可提交的纹理/生产场景验收。
    std::array<War3RenderTextureMipChainV2, 16> textures{};
    War3RenderTextureMipChainV2 extraTexture{};
    const War3RenderTerrainBlightBatchV2 extraBatch{0, 6, .125f, .25f, .5f, .75f};
    War3RenderTerrainBlightFrameV2 input{};
    input.structSize = sizeof(input); input.baseQuadCount = 1;
    auto& base = input.baseScene;
    base.structSize = sizeof(base); base.vertexCount = 4; base.indexCount = 6;
    base.textureCount = 16; base.batchCount = 1; base.documentRevision = 0x123456789abcdef0ULL;
    base.vertices = vertices.data(); base.indices = indices.data();
    base.textures = textures.data(); base.batches = batches.data();
    input.blightIndexCount = 6; input.blightBatchCount = 1;
    input.blightIndices = indices.data(); input.blightBatches = &extraBatch; input.blightTexture = &extraTexture;
    FrozenMipScene frozen{};
    std::memcpy(&frozen, &base, sizeof(frozen));
    Check(frozen.structSize == sizeof(FrozenMipScene) && frozen.vertexCount == 4 && frozen.indexCount == 6
        && frozen.textureCount == 16 && frozen.batchCount == 1 && frozen.documentRevision == 0x123456789abcdef0ULL,
        "new frame changed the frozen terrain descriptor counts, size or revision");
    FrozenVertex vertex{};
    std::memcpy(&vertex, reinterpret_cast<const unsigned char*>(frozen.vertices) + 3 * sizeof(FrozenVertex), sizeof(vertex));
    Check(vertex.x == 10 && vertex.y == 11 && vertex.z == 12 && vertex.red == .4f
        && vertex.green == .5f && vertex.blue == .6f && vertex.u == .7f && vertex.v == .8f,
        "shared base changed the frozen vertex stride or fields");
    FrozenBatch batch{}; std::memcpy(&batch, frozen.batches, sizeof(batch));
    Check(batch.firstIndex == 0 && batch.indexCount == 6 && batch.textureIndex == 15,
        "independent pass changed the frozen batch texture slot or stride");
    Check(frozen.textures == textures.data() && input.blightTexture == &extraTexture
        && input.blightIndices == base.indices && base.vertexCount == 4,
        "independent pass was folded into the old resource table or vertex stream");
    Check(input.blightBatches->uvLeft == .125f && input.blightBatches->uvRight == .25f
        && input.blightBatches->uvTop == .5f && input.blightBatches->uvBottom == .75f,
        "CellUv left/right/top/bottom field order changed");
}

std::uint32_t queryCalls = 0;
std::int32_t queryResult = 0;
War3RenderTerrainBlightCapabilitiesV2 advertised{
    sizeof(War3RenderTerrainBlightCapabilitiesV2), WAR3_RENDER_TERRAIN_BLIGHT_SHARED_BASE_V1,
    WAR3_RENDER_TERRAIN_BLIGHT_MAX_BATCHES, 0};

// 仅ABI协商夹具，不实现生产几何验证、预算、深拷贝、GPU上传或失败回滚。
std::int32_t WAR3_RENDER_CALL CapabilityFixture(War3RendererHandle, War3RenderTerrainBlightCapabilitiesV2* output) {
    ++queryCalls;
    if (queryResult) return queryResult;
    if (!output || output->structSize < sizeof(*output)) return -1;
    std::memcpy(output, &advertised, sizeof(advertised));
    return 0;
}
std::int32_t WAR3_RENDER_CALL SubmitSignatureFixture(War3RendererHandle, const War3RenderTerrainBlightFrameV2*) {
    return 17;
}
bool ConsumerFixture(const War3RenderPluginApiV2& api) {
    if (api.apiVersion != WAR3_RENDER_PLUGIN_API_V2
        || api.structSize < offsetof(War3RenderPluginApiV2, getTerrainBlightCapabilities)
            + sizeof(api.getTerrainBlightCapabilities)
        || api.structSize < offsetof(War3RenderPluginApiV2, submitTerrainBlightFrame)
            + sizeof(api.submitTerrainBlightFrame)
        || !api.getTerrainBlightCapabilities || !api.submitTerrainBlightFrame) return false;
    War3RenderTerrainBlightCapabilitiesV2 output{}; output.structSize = sizeof(output);
    // 初始载入可在renderer创建前查询；没有伪造handle或GPU初始化。
    return api.getTerrainBlightCapabilities(nullptr, &output) == 0
        && output.structSize == sizeof(output)
        && output.blightFormat == WAR3_RENDER_TERRAIN_BLIGHT_SHARED_BASE_V1
        && output.maxBlightBatches == WAR3_RENDER_TERRAIN_BLIGHT_MAX_BATCHES && !output.reserved;
}
void CheckCapabilityBoundary() {
    War3RenderPluginApiV2 api{}; api.apiVersion = WAR3_RENDER_PLUGIN_API_V2;
    api.getTerrainBlightCapabilities = CapabilityFixture; api.submitTerrainBlightFrame = SubmitSignatureFixture;
    api.structSize = sizeof(FrozenApi41);
    Check(!ConsumerFixture(api) && queryCalls == 0, "old table read or called a new tail");
    api.structSize = static_cast<std::uint32_t>(offsetof(War3RenderPluginApiV2, submitTerrainBlightFrame));
    Check(!ConsumerFixture(api) && queryCalls == 0, "query-only partial table enabled a missing submit slot");
    api.structSize = sizeof(api); api.submitTerrainBlightFrame = nullptr;
    Check(!ConsumerFixture(api) && queryCalls == 0, "null submit pointer enabled the new mode");
    api.submitTerrainBlightFrame = SubmitSignatureFixture; api.getTerrainBlightCapabilities = nullptr;
    Check(!ConsumerFixture(api) && queryCalls == 0, "non-null submit bypassed explicit capability query");
    api.getTerrainBlightCapabilities = CapabilityFixture;
    Check(ConsumerFixture(api) && queryCalls == 1, "explicit null-renderer capability negotiation failed");
    queryResult = -7;
    Check(!ConsumerFixture(api), "failed capability query enabled the new mode");
    queryResult = 0;
    const auto supported = advertised;
    advertised.structSize = sizeof(advertised) - 1;
    Check(!ConsumerFixture(api), "partial known capability prefix accepted");
    advertised = supported; advertised.structSize = sizeof(advertised) + 1;
    Check(!ConsumerFixture(api), "unknown capability response size accepted");
    advertised = supported; advertised.blightFormat = 2;
    Check(!ConsumerFixture(api), "unknown format inferred from a non-null function");
    advertised = supported; advertised.maxBlightBatches = 31;
    Check(!ConsumerFixture(api), "partial cell capacity accepted as format 1");
    advertised = supported; advertised.maxBlightBatches = 33;
    Check(!ConsumerFixture(api), "larger cell capacity silently changed format 1");
    advertised = supported; advertised.reserved = 1;
    Check(!ConsumerFixture(api), "unknown capability flags accepted");
    advertised = supported;
    Check(ConsumerFixture(api) && api.submitTerrainBlightFrame(nullptr, nullptr) == 17,
        "negotiated function pointer signatures no longer dispatch correctly");

    struct LargerOutput { War3RenderTerrainBlightCapabilitiesV2 base; std::array<unsigned char, 13> tail; };
    static_assert(offsetof(LargerOutput, tail) == sizeof(War3RenderTerrainBlightCapabilitiesV2));
    LargerOutput larger{}; larger.base.structSize = sizeof(larger); larger.tail.fill(0xa5);
    Check(api.getTerrainBlightCapabilities(nullptr, &larger.base) == 0
        && std::memcmp(&larger.base, &supported, sizeof(supported)) == 0,
        "capability fixture failed to write the exact known prefix");
    for (auto byte : larger.tail) Check(byte == 0xa5, "capability fixture overwrote caller's unknown tail");
    War3RenderTerrainBlightCapabilitiesV2 shortOutput{sizeof(shortOutput) - 1, 0xabab, 0xcdcd, 0xefef};
    const auto original = shortOutput;
    Check(api.getTerrainBlightCapabilities(nullptr, &shortOutput) != 0
        && std::memcmp(&shortOutput, &original, sizeof(original)) == 0,
        "short capability output was accepted or modified");
    shortOutput.structSize = sizeof(shortOutput);
    const auto failedOriginal = shortOutput; queryResult = -7;
    Check(api.getTerrainBlightCapabilities(nullptr, &shortOutput) == -7
        && std::memcmp(&shortOutput, &failedOriginal, sizeof(failedOriginal)) == 0,
        "failed capability query modified output");
    queryResult = 0;
}
}

int main() {
    try {
        CheckFrozenTerrainBytes();
        CheckCapabilityBoundary();
        std::cout << "terrain-blight ABI: frozen 41-function prefix, 43-function table, terrain bytes and capability fixtures passed; no provider/GPU proof\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
