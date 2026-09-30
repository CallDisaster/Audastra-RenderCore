// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include <cstdint>

#if defined(_WIN32) && defined(WAR3_RENDER_PLUGIN_BUILD)
#define WAR3_RENDER_EXPORT __declspec(dllexport)
#define WAR3_RENDER_CALL __cdecl
#elif defined(_WIN32)
#define WAR3_RENDER_EXPORT
#define WAR3_RENDER_CALL __cdecl
#else
#define WAR3_RENDER_EXPORT
#define WAR3_RENDER_CALL
#endif

extern "C" {

constexpr std::uint32_t WAR3_RENDER_PLUGIN_API_VERSION = 1;

struct War3SceneSnapshotV1 {
    std::uint32_t structSize;
    std::uint32_t schemaVersion;
    std::uint64_t documentRevision;
    // 不可变字节由 Core 持有，仅在 submitSceneSnapshot 调用期间有效。
    const void* payload;
    std::uint64_t payloadSize;
};

using War3RendererHandle = void*;

struct War3RenderPluginApiV1 {
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    War3RendererHandle (WAR3_RENDER_CALL* createRenderer)();
    void (WAR3_RENDER_CALL* destroyRenderer)(War3RendererHandle renderer);
    std::int32_t (WAR3_RENDER_CALL* submitSceneSnapshot)(
        War3RendererHandle renderer,
        const War3SceneSnapshotV1* snapshot);
    std::int32_t (WAR3_RENDER_CALL* attachNativeWindow)(
        War3RendererHandle renderer,
        void* nativeWindow);
    void (WAR3_RENDER_CALL* onDocumentRevisionChanged)(
        War3RendererHandle renderer,
        std::uint64_t revision);
};

// 返回的函数表具有进程级生命周期；任何异常都不得跨越此 C ABI。
WAR3_RENDER_EXPORT const War3RenderPluginApiV1* WAR3_RENDER_CALL
War3MapEditor_GetRenderPluginApi(std::uint32_t requestedApiVersion);

}
