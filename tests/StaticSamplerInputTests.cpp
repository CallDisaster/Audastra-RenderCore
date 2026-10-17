// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "RenderSkinFixture.h"
#include "../src/Render/Scene/ModelSkinInput.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace War3Map::Render;

namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Fixture {
    std::array<War3RenderModelVertexV2, 3> vertices{{
        {-1, -1, 0, 0, 0, 1, 0, 1}, {1, -1, 0, 0, 0, 1, 1, 1}, {0, 1, 0, 0, 0, 1, .5f, 0}}};
    std::array<std::uint16_t, 12> indices{0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2};
    std::array<War3RenderModelLayerV2, 4> layers{{
        {WAR3_RENDER_MODEL_NO_TEXTURE, 0, 1, 1}, {WAR3_RENDER_MODEL_NO_TEXTURE, 1, 2, .75f},
        {WAR3_RENDER_MODEL_NO_TEXTURE, 2, 4, .5f}, {WAR3_RENDER_MODEL_NO_TEXTURE, 3, 0, .25f}}};
    std::array<War3RenderModelMeshV2, 4> meshes{{{0, 3, 0, 0, 1}, {3, 3, 0, 1, 1}, {6, 3, 0, 2, 1}, {9, 3, 0, 3, 1}}};
    War3RenderModelV2 model{0, 4};
    War3RenderModelInstanceV2 instance{0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, {1, 1, 1}};
    std::array<std::uint32_t, 4> flags{0, 1, 2, 3};
    War3RenderStaticSamplerModelSceneV2 Scene() const {
        return {{sizeof(War3RenderStaticSamplerModelSceneV2), 3, 12, 4, 4, 1, 1, 0,
            vertices.data(), indices.data(), layers.data(), meshes.data(), &model, &instance, nullptr},
            WAR3_RENDER_STATIC_LAYER_SAMPLERS_V1, 4, flags.data()};
    }
};

void OwnershipAndLegacy() {
    Fixture fixture; auto input = fixture.Scene();
    ModelScene output; std::uint64_t bytes = 0;
    Check(CopyStaticModelInput(input.base, 0, 0, output, bytes), "valid explicit static input rejected");
    // 独立加总：3*32 + 12*2 + 4*16 + 4*20 + 8 + 64 = 336旧载荷。
    const auto tail = sizeof(War3RenderStaticSamplerModelSceneV2) - sizeof(War3RenderModelSceneV2);
    const auto residentSamplerBytes = static_cast<std::uint64_t>(output.staticSamplerFlags.capacity()) * 4;
    Check(bytes == 336 + residentSamplerBytes + tail && output.explicitStaticSamplers
        && output.staticSamplerFlags == std::vector<std::uint32_t>({0, 1, 2, 3})
        && output.StaticSamplerBytes() == residentSamplerBytes && !output.initialSkin, "mode, absolute flags or bytes lost");
    Check(bytes + output.staticSamplerBackendBudget == 320ull * 1024 * 1024
        && output.staticSamplerBackendBudget >= 16, "backend budget did not reserve the remaining original aggregate");
    fixture.flags.fill(3); fixture.vertices[0].x = 99; fixture.indices[0] = 2;
    fixture.layers[0].flags = 4;
    Check(output.staticSamplerFlags[0] == 0 && output.vertices[0].x == -1 && output.indices[0] == 0
        && output.layers[0].flags == 1 && output.layers[2].flags == 4, "input buffers were borrowed or lighting flags conflated");
    Fixture legacyFixture; auto legacy = legacyFixture.Scene().base; legacy.structSize = sizeof(legacy);
    Check(CopyStaticModelInput(legacy, 0, 0, output, bytes) && bytes == 336
        && !output.explicitStaticSamplers && output.staticSamplerFlags.empty() && output.StaticSamplerBytes() == 0
        && output.staticSamplerBackendBudget == 0,
        "legacy input must reset explicit mode without allocating or changing old payload");
    War3RenderStaticSamplerModelSceneV2 empty{};
    empty.base.structSize = sizeof(empty); empty.samplerFormat = WAR3_RENDER_STATIC_LAYER_SAMPLERS_V1;
    Check(CopyStaticModelInput(empty.base, 0, 0, output, bytes) && output.explicitStaticSamplers
        && output.staticSamplerFlags.empty() && output.StaticSamplerBytes() == 0 && bytes == tail
        && bytes + output.staticSamplerBackendBudget == 320ull * 1024 * 1024, "empty explicit scene lost its mode");
}

void RejectedPreservation() {
    Fixture fixture; const auto valid = fixture.Scene();
    ModelScene retained; std::uint64_t retainedBytes = 0;
    Check(CopyStaticModelInput(valid.base, 0, 0, retained, retainedBytes), "retained setup");
    const auto* vertices = retained.vertices.data();
    const auto* flags = retained.staticSamplerFlags.data();
    const auto backendBudget = retained.staticSamplerBackendBudget;
    const auto reject = [&](const War3RenderModelSceneV2& bad, std::uint64_t descriptors = 0, std::uint64_t pixels = 0) {
        std::uint64_t unchanged = 991;
        Check(!CopyStaticModelInput(bad, descriptors, pixels, retained, unchanged) && unchanged == 991
            && retained.vertices.data() == vertices && retained.staticSamplerFlags.data() == flags
            && retained.explicitStaticSamplers && retained.staticSamplerFlags[0] == 0
            && retained.staticSamplerBackendBudget == backendBudget,
            "rejection changed retained scene or payload output");
    };
    const auto rejectHeader = [&](const War3RenderStaticSamplerModelSceneV2& bad) {
        StaticSamplerSceneView view{true, std::span<const std::uint32_t>(fixture.flags).first(1), 337};
        const auto* before = view.samplerFlags.data();
        Check(!ValidateStaticSamplerScene(bad.base, 0, 0, view) && view.explicitSamplers
            && view.payloadBytes == 337 && view.samplerFlags.data() == before && view.samplerFlags.size() == 1,
            "rejected preflight changed its view output");
        reject(bad.base);
    };
    for (auto size : {0u, static_cast<std::uint32_t>(sizeof(War3RenderModelSceneV2) - 1),
            static_cast<std::uint32_t>(sizeof(War3RenderModelSceneV2) + 1),
            static_cast<std::uint32_t>(sizeof(valid) - 1), static_cast<std::uint32_t>(sizeof(valid) + 1)}) {
        auto bad = valid; bad.base.structSize = size; rejectHeader(bad);
    }
    auto bad = valid; bad.samplerFormat = 0; rejectHeader(bad);
    bad = valid; bad.samplerFormat = 2; rejectHeader(bad);
    for (auto count : {0u, 3u, 5u, UINT32_MAX}) { bad = valid; bad.samplerFlagCount = count; rejectHeader(bad); }
    bad = valid; bad.samplerFlags = nullptr; rejectHeader(bad);
    bad = valid; bad.base.layerCount = bad.samplerFlagCount = 0; rejectHeader(bad); // 零count非空指针也不接受。
    for (auto unknown : {4u, 7u, 0x80000000u, UINT32_MAX}) {
        fixture.flags[0] = unknown; rejectHeader(valid); fixture.flags[0] = 0;
    }
    bad = valid; bad.base.vertexCount = 2097153; rejectHeader(bad);
    bad = valid; bad.base.indexCount = 6291457; rejectHeader(bad);
    bad = valid; bad.base.layerCount = 131073; rejectHeader(bad);
    bad = valid; bad.base.meshCount = 65537; rejectHeader(bad);
    bad = valid; bad.base.modelCount = 16385; rejectHeader(bad);
    bad = valid; bad.base.instanceCount = UINT32_MAX; rejectHeader(bad); // 乘法须先拓宽，累计预算须在复制前拒绝。
    bad = valid; bad.base.textureCount = 4097; rejectHeader(bad);
    bad = valid; bad.base.vertices = nullptr; rejectHeader(bad);
    bad = valid; bad.base.indices = nullptr; rejectHeader(bad);
    bad = valid; bad.base.layers = nullptr; rejectHeader(bad);
    bad = valid; bad.base.meshes = nullptr; rejectHeader(bad);
    bad = valid; bad.base.models = nullptr; rejectHeader(bad);
    bad = valid; bad.base.instances = nullptr; rejectHeader(bad);
    bad = valid; bad.base.textureCount = 1; rejectHeader(bad);
    reject(valid.base, UINT64_MAX, 0); reject(valid.base, 0, UINT64_MAX);
    reject(valid.base, 320ull * 1024 * 1024, 1);
    bad = valid; bad.samplerFlags = reinterpret_cast<const std::uint32_t*>(std::uintptr_t{1});
    reject(bad.base, UINT64_MAX, 0); // 超预算必须在读取flags或复制任何数组之前拒绝。
    fixture.indices[0] = 3; reject(valid.base); fixture.indices[0] = 0;
    fixture.meshes[0].firstIndex = UINT32_MAX; reject(valid.base); fixture.meshes[0].firstIndex = 0;
    fixture.meshes[0].firstLayer = UINT32_MAX; reject(valid.base); fixture.meshes[0].firstLayer = 0;
    fixture.meshes[0].baseVertex = 3; reject(valid.base); fixture.meshes[0].baseVertex = 0;
    fixture.model.firstMesh = UINT32_MAX; reject(valid.base); fixture.model.firstMesh = 0;
    fixture.instance.model = 1; reject(valid.base); fixture.instance.model = 0;
    fixture.layers[0].flags = 8; reject(valid.base); fixture.layers[0].flags = 1;
    fixture.layers[0].textureIndex = 0; reject(valid.base); fixture.layers[0].textureIndex = WAR3_RENDER_MODEL_NO_TEXTURE;
    fixture.layers[0].alpha = std::numeric_limits<float>::quiet_NaN(); reject(valid.base); fixture.layers[0].alpha = 1;
    fixture.vertices[0].u = std::numeric_limits<float>::infinity(); reject(valid.base); fixture.vertices[0].u = 0;
}

void AccountingAndModes() {
    Fixture fixture; const auto input = fixture.Scene();
    StaticSamplerSceneView view;
    auto manyInstances = input; manyInstances.base.instanceCount = 262145;
    Check(ValidateStaticSamplerScene(manyInstances.base, 0, 0, view),
        "explicit static input must not inherit the skin instance cap"); // 只预检，不从短fixture数组复制。
    auto legacyManyInstances = manyInstances.base; legacyManyInstances.structSize = sizeof(legacyManyInstances);
    Check(ValidateStaticSamplerScene(legacyManyInstances, 0, 0, view),
        "legacy static input must not inherit the skin instance cap");
    ModelScene spareCapacity;
    spareCapacity.staticSamplerFlags.reserve(8);
    spareCapacity.staticSamplerFlags.assign({0, 1, 2, 3});
    Check(spareCapacity.StaticSamplerBytes() == static_cast<std::uint64_t>(spareCapacity.staticSamplerFlags.capacity()) * 4
        && spareCapacity.StaticSamplerBytes() >= 32, "resident flags must account allocated capacity");
    Check(ValidateStaticSamplerScene(input.base, 128, 256, view)
        && view.payloadBytes == 336 + 32 + sizeof(input) - sizeof(input.base) + 384,
        "texture, both sampler copies and descriptor bytes must share the original budget");
    constexpr std::uint64_t maximum = 320ull * 1024 * 1024;
    const auto geometryBytes = view.payloadBytes - 384;
    // 只检查调用方提供的已验证字节数的累计算术，不声称这些虚拟bytes是合法纹理链。
    Check(ValidateStaticSamplerScene(input.base, maximum - geometryBytes - 1, 1, view)
        && view.payloadBytes == maximum, "exact aggregate payload boundary rejected");
    const auto preserved = view.payloadBytes;
    Check(!ValidateStaticSamplerScene(input.base, maximum - geometryBytes, 1, view)
        && view.payloadBytes == preserved, "one byte over aggregate budget accepted or changed output");
    // 实际capacity可大于声明count；精确最低容量边界只能在分配后发布或保留旧对象。
    ModelScene boundary; std::uint64_t boundaryBytes = 0;
    Check(CopyStaticModelInput(input.base, 0, 0, boundary, boundaryBytes), "aggregate boundary setup");
    const auto* oldBoundaryFlags = boundary.staticSamplerFlags.data();
    const auto oldBoundaryBudget = boundary.staticSamplerBackendBudget;
    boundaryBytes = 997;
    if (CopyStaticModelInput(input.base, maximum - geometryBytes - 1, 1, boundary, boundaryBytes)) {
        Check(boundary.StaticSamplerBytes() == 16 && boundaryBytes == maximum - 16
            && boundary.staticSamplerBackendBudget == 16, "exact aggregate publication lost actual capacity accounting");
    } else {
        Check(boundaryBytes == 997 && boundary.staticSamplerFlags.data() == oldBoundaryFlags
            && boundary.staticSamplerBackendBudget == oldBoundaryBudget, "post-allocation rejection published a partial candidate");
    }
    std::vector<std::uint32_t> maximumFlags(131072, 3);
    auto maximumInput = input;
    maximumInput.base.layerCount = maximumInput.samplerFlagCount = 131072;
    maximumInput.samplerFlags = maximumFlags.data();
    Check(ValidateStaticSamplerScene(maximumInput.base, 0, 0, view) && view.samplerFlags.size_bytes() == 524288,
        "documented maximum sampler array changed"); // preflight不读取这份虚拟layer数组，也不发布它。
    ModelScene staticScene; std::uint64_t bytes = 0;
    Check(CopyStaticModelInput(input.base, 0, 0, staticScene, bytes), "mode setup");
    auto invalid = staticScene; invalid.staticSamplerFlags.pop_back();
    Check(!ValidateModelGeometry(invalid, 0), "partial resident sampler pool accepted");
    invalid = staticScene; invalid.explicitStaticSamplers = false;
    Check(!ValidateModelGeometry(invalid, 0), "legacy mode accepted hidden explicit sampler pool");
    invalid = ModelScene{}; invalid.staticSamplerBackendBudget = 1;
    Check(!ValidateModelGeometry(invalid, 0), "legacy mode accepted hidden static sampler budget");
    invalid = staticScene; invalid.staticSamplerFlags[0] = 4;
    Check(!ValidateModelGeometry(invalid, 0), "unknown resident sampler flags accepted");
    RenderSkinFixture skinFixture; auto pose = skinFixture.Update(); auto skinInput = skinFixture.Scene(pose);
    ModelScene skinned;
    Check(CopySkinnedModelInput(skinInput, skinned, bytes) && !skinned.explicitStaticSamplers
        && skinned.staticSamplerFlags.empty() && skinned.staticSamplerBackendBudget == 0,
        "legacy skin unexpectedly entered static sampler mode");
    SkinPose updated;
    Check(CopySkinUpdate(pose, skinned, updated), "legacy skin update changed");
    auto mixed = skinned; mixed.explicitStaticSamplers = true;
    mixed.staticSamplerFlags.assign(mixed.layers.size(), 3);
    Check(!CopySkinUpdate(pose, mixed, updated) && !ValidateModelGeometry(mixed, 0), "skin/static mode mix accepted");
    const auto* oldStaticFlags = staticScene.staticSamplerFlags.data();
    const auto oldStaticBudget = staticScene.staticSamplerBackendBudget;
    const auto untouched = bytes;
    Check(!CopySkinnedModelInput(skinInput, staticScene, bytes) && bytes == untouched
        && staticScene.staticSamplerFlags.data() == oldStaticFlags && staticScene.explicitStaticSamplers,
        "skin initialization stripped an explicit static sampler target");
    Check(staticScene.staticSamplerBackendBudget == oldStaticBudget, "skin initialization changed static backend budget");
    ModelScene roster; pose.sceneVersion = 2;
    Check(CopySkinRoster(pose, skinned, roster) && !roster.explicitStaticSamplers && roster.staticSamplerFlags.empty()
        && roster.staticSamplerBackendBudget == 0,
        "legacy roster inherited explicit static sampler mode");
    Check(!CopySkinRoster(pose, mixed, roster), "roster stripped invalid static sampler metadata");
    War3RenderAssetAppendV2 append{}; append.structSize = sizeof(append);
    const auto* rosterOwner = roster.initialSkin.get();
    Check(!CopySkinAssetAppend(append, pose, std::make_shared<ModelScene>(mixed), roster, bytes)
        && roster.initialSkin.get() == rosterOwner && bytes == untouched,
        "append stripped invalid static sampler metadata or changed output");
}
}

int main() {
    try {
        OwnershipAndLegacy();
        RejectedPreservation();
        AccountingAndModes();
        std::cout << "static-sampler CPU input: original geometry contract, owned flags, strict tails, aggregate bytes, preserved rejection and skin separation passed; no GPU proof\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
