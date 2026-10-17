// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3MapCore/RenderPlugin.h"

// R0/R1 基础 ABI。V1 保持不变；所有输入在调用期间复制，渲染器不拥有文档。
// 除销毁/显式截图外均不等待 GPU。设备、交换链和 immediate context 只在渲染线程使用。
// destroy 不得与其他 API 并发或从 API/sent-message 回调重入；HWND 必须存活到销毁返回。
extern "C" {
constexpr std::uint32_t WAR3_RENDER_PLUGIN_API_V2 = 2;
struct War3RenderCreateInfoV2 {
    std::uint32_t structSize;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t forceWarp; // 仅测试或显式软件模式；硬件失败不会静默冒称硬件。
};
struct War3RenderVertexV2 { float x, y, z, red, green, blue; };
struct War3RenderMeshV2 {
    std::uint32_t structSize;
    std::uint32_t vertexCount;
    std::uint32_t indexCount;
    std::uint64_t documentRevision;
    const War3RenderVertexV2* vertices;
    const std::uint32_t* indices;
};
struct War3RenderCameraV2 {
    std::uint32_t structSize;
    float targetX, targetY, targetZ;
    float distance, yawRadians, pitchRadians;
};
struct War3RenderStatusV2 {
    std::uint32_t structSize;
    std::int32_t result; // 0=正常/待首帧；负值=HRESULT；1=无效输入
    std::uint32_t deviceReady;
    std::uint32_t software;
    std::uint32_t width, height;
    std::uint64_t frameCount;
    std::uint64_t documentRevision;
    char message[256]; // UTF-8，始终零结尾
};
struct War3RenderCaptureInfoV2 {
    std::uint32_t structSize;
    std::uint32_t width, height;
    std::uint64_t frameCount;
    std::uint64_t documentRevision;
    std::uint64_t ticket;
};
// 最近完成 Draw/Present 的 CPU 回执；不等待 GPU，也不做像素读回。
// row-major、row-vector，D3D clip Z=[0,1]，即本帧实际上传的 float MVP。
struct War3RenderViewInfoV2 {
    std::uint32_t structSize;
    std::uint32_t width, height;
    std::uint64_t frameCount, documentRevision;
    std::uint64_t surfaceEpoch, sceneEpoch, viewEpoch;
    float viewProjection[16];
};
// 与 RGBA 同一次完成 Draw/Present/读回的不可变回执，不是随后查询的当前 view。
// 三个内嵌结构的 structSize 由插件填写；只有顶层 structSize 是调用方输入。
struct War3RenderCapturedFrameV2 {
    std::uint32_t structSize;
    War3RenderCaptureInfoV2 capture;
    War3RenderViewInfoV2 view;
    War3RenderCameraV2 camera;
};
// 地表基础层预览的可选提交。原 V2 顶点/网格保持原样；此处允许按 UV 拆顶点，
// XYZ 必须由宿主沿用其高度网格。只读 RGBA8 mip0；不自动生成 mip 或原版混合层。
// 贴图批次的顶点 RGB 与贴图相乘（宿主的光照明暗；白色即原色）。
struct War3RenderTerrainVertexV2 { float x, y, z, red, green, blue, u, v; };
struct War3RenderTextureRgbaV2 {
    std::uint32_t structSize;
    std::uint32_t width, height;
    std::uint64_t rgbaByteCount;
    const void* rgba; // 紧密行优先 RGBA8，顶行先存；调用期间深拷贝。
};
constexpr std::uint32_t WAR3_RENDER_HEIGHT_COLOR_TEXTURE = UINT32_MAX;
// 贴图批次可按位或此标志：该批次采样贴图 alpha 并混合到之前批次之上（地表过渡层）。
// 未带标志的批次忽略 alpha、按不透明绘制。高度色哨兵不能带标志。
constexpr std::uint32_t WAR3_RENDER_TERRAIN_ALPHA_BLEND = 0x80000000u;
// 半透明批次（水面）：按贴图 alpha 混合，但在不透明模型之后绘制、只做深度测试不写深度，
// 使水下的模型（悬崖底座、水中装饰物）透过水面可见。可单独使用，也可与上一标志同时出现。
constexpr std::uint32_t WAR3_RENDER_TERRAIN_TRANSLUCENT = 0x40000000u;
struct War3RenderTerrainBatchV2 {
    std::uint32_t firstIndex, indexCount;
    std::uint32_t textureIndex; // 上述哨兵表示高度色；否则索引 textures（可带 alpha 混合标志）。
};
struct War3RenderTerrainSceneV2 {
    std::uint32_t structSize;
    std::uint32_t vertexCount, indexCount, textureCount, batchCount;
    std::uint64_t documentRevision;
    const War3RenderTerrainVertexV2* vertices;
    const std::uint32_t* indices;
    const War3RenderTextureRgbaV2* textures;
    const War3RenderTerrainBatchV2* batches;
};
// 原始连续 mip 链的可选提交。mips[0] 是 mip0；逐级尺寸必须为 max(1,前级/2)，
// 允许尾部截短，不能跳级、重复 1x1 或自动补造。全部描述符与像素在调用期间深拷贝。
struct War3RenderMipRgbaV2 {
    std::uint32_t structSize;
    std::uint32_t width, height;
    std::uint64_t rgbaByteCount;
    const void* rgba;
};
struct War3RenderTextureMipChainV2 {
    std::uint32_t structSize;
    std::uint32_t mipCount;
    std::uint32_t maxSampleMip; // <mipCount；更低分辨率的保留级不会参与采样。
    std::uint32_t atlasColumns, atlasRows; // 必须非零；普通纹理显式 1x1。
    const War3RenderMipRgbaV2* mips;
};
struct War3RenderTerrainMipSceneV2 {
    std::uint32_t structSize;
    std::uint32_t vertexCount, indexCount, textureCount, batchCount;
    std::uint64_t documentRevision;
    const War3RenderTerrainVertexV2* vertices;
    const std::uint32_t* indices;
    const War3RenderTextureMipChainV2* textures;
    const War3RenderTerrainBatchV2* batches;
};
// mip 入口沿用网格预算；1..12 级，每级<=16MiB、每链<=24MiB、全部链<=64MiB、
// 整包<=128MiB。每个可采级的宽/高必须整除 atlas 格数（每格>=1像素）。
// 采样在各级独立 clamp 至所属 cell 半像素内部后插值，不重建原图已包含的混合像素。
// 硬预算：地表入口 1048576 顶点、6291456 索引（普通网格仍为 262144/1572864）、16 纹理、4096 batch；单纹理尺寸<=2048、
// RGBA<=16MiB、累计纹理<=64MiB、整个提交载荷<=128MiB。batch 必须按索引顺序连续
// 覆盖全部三角形，无重叠/空洞；UV 有限且在[0,1]，半texel内缩由 CPU atlas 规划完成。
// 模型场景（装饰物、可破坏物、单位的静态姿态）。与地表场景并存、独立提交；
// 空场景（instanceCount=0）清除模型。不改变 documentRevision 与拾取回执身份（模型不参与拾取）。
struct War3RenderModelVertexV2 { float x, y, z, nx, ny, nz, u, v; };
constexpr std::uint32_t WAR3_RENDER_MODEL_NO_TEXTURE = UINT32_MAX;
// filterMode 同 MDX：0 不透明、1 透明测试(alpha≥0.75)、2 混合、3 叠加、4 叠加 alpha、5 调制、6 双倍调制。
// flags：1 无光照、2 不受雾影响、4 不写深度；可组合，未知位拒绝。模型均按双面绘制。
constexpr std::uint32_t WAR3_RENDER_MODEL_UNSHADED = 1;
constexpr std::uint32_t WAR3_RENDER_MODEL_UNFOGGED = 2;
constexpr std::uint32_t WAR3_RENDER_MODEL_NO_DEPTH_WRITE = 4;
constexpr std::uint32_t WAR3_RENDER_MODEL_LAYER_FLAGS = WAR3_RENDER_MODEL_UNSHADED
    | WAR3_RENDER_MODEL_UNFOGGED | WAR3_RENDER_MODEL_NO_DEPTH_WRITE;
struct War3RenderModelLayerV2 { std::uint32_t textureIndex, filterMode, flags; float alpha; };
// indices 为 16 位；baseVertex 为该网格首顶点在 vertices 中的位置。
struct War3RenderModelMeshV2 { std::uint32_t firstIndex, indexCount, baseVertex, firstLayer, layerCount; };
struct War3RenderModelV2 { std::uint32_t firstMesh, meshCount; };
// 行主序 3×4 模型→世界变换；color 为顶点颜色乘数（0..1）。实例须按 model 升序排列。
struct War3RenderModelInstanceV2 { std::uint32_t model; float transform[12]; float color[3]; };
struct War3RenderModelSceneV2 {
    std::uint32_t structSize;
    std::uint32_t vertexCount, indexCount, layerCount, meshCount, modelCount, instanceCount, textureCount;
    const War3RenderModelVertexV2* vertices;
    const std::uint16_t* indices;
    const War3RenderModelLayerV2* layers;
    const War3RenderModelMeshV2* meshes;
    const War3RenderModelV2* models;
    const War3RenderModelInstanceV2* instances;
    // 每张贴图 atlas 1×1；可只给 mip0（maxSampleMip=0），插件生成其余级用于远处采样。
    const War3RenderTextureMipChainV2* textures;
};
// 模型硬预算：2097152 顶点、6291456 索引、131072 层、65536 网格、16384 模型、262144 实例、
// 4096 贴图（单张<=2048、<=16MiB，累计<=192MiB）；整个提交载荷<=320MiB。
// 场景环境：模型光照、距离雾与地表静态阴影。未设置时为中性灰光、无雾、无阴影。
// 地表光照由宿主烘焙在顶点颜色里，不受此结构影响。
constexpr std::uint32_t WAR3_RENDER_FOG_NONE = 0, WAR3_RENDER_FOG_LINEAR = 1, WAR3_RENDER_FOG_EXP = 2,
    WAR3_RENDER_FOG_EXP2 = 3;
struct War3RenderEnvironmentV2 {
    std::uint32_t structSize;
    float towardLight[3];   // 模型光向（指向光源），长度须 > 0
    float lightColor[3];    // 模型直射光：颜色 × 强度，0..16
    float ambientColor[3];  // 模型环境光：颜色 × 強度，0..16
    std::uint32_t fogMode;  // WAR3_RENDER_FOG_*；按视深作用于地表与模型
    float fogStart, fogEnd; // 线性雾：start < end
    float fogDensity;       // 指数雾：每世界单位，>= 0
    float fogColor[3];      // 0..1
    // 静态阴影：8 位图（0 无阴影、255 全阴影），行优先、首行在南；像素 (i, j) 覆盖世界
    // [originX + i·cell, originX + (i+1)·cell) × [originY + j·cell, …)。shadowWidth=0 表示无阴影。
    std::uint32_t shadowWidth, shadowHeight; // 各 <= 8192
    float shadowOriginX, shadowOriginY, shadowCellSize; // cell > 0
    float shadowDarkness;   // 0..1：全阴影处地表颜色乘 (1 − darkness)
    const std::uint8_t* shadow;
};
// 编辑叠加线（选择框、区域、镜头标记等）：世界坐标线段，绘制在全部场景之后且不做深度测试。
// 至多 262144 条；颜色 0..1；lineCount=0 清除。
struct War3RenderOverlayLineV2 { float from[3]; float to[3]; float color[3]; };
// 可选矩阵组蒙皮扩展。旧结构不变，以下结构只供新增入口使用；均为通用预览近似。
// 模型局部、列向量、四列各三个 float；values[column*3+row]，紧密步长 48 字节。
// 组内平均已经完成，尚未乘实例世界变换；实例 transform 仍是旧接口的行主序 3x4。
constexpr std::uint32_t WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4 = 1;
// 显式新布局：同一模型的实例可共享完整姿势片段；更新可切换片段，不改变静态几何。
// 宿主必须先探测 sharedRangeMatrixFormat，旧模式的连续/固定偏移合同保持不变。
constexpr std::uint32_t WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4_SHARED_RANGES = 2;
struct War3RenderGroupMatrixV2 { float values[12]; };
struct War3RenderSkinnedModelVertexV2 { War3RenderModelVertexV2 vertex; std::uint32_t group; };
// group 是该网格组片段内的局部序号；firstGroup 相对模型的组矩阵起点。
struct War3RenderSkinnedModelMeshV2 { War3RenderModelMeshV2 mesh; std::uint32_t firstGroup, groupCount; };
struct War3RenderSkinnedModelV2 { War3RenderModelV2 model; std::uint32_t groupCount; };
// firstGroup/firstAlpha 分别指向本次完整更新的矩阵和透明度池。每实例有 model.groupCount
// 条矩阵和 model.meshCount 个透明度（按模型内网格顺序）。实例按 model 升序；片段紧密连续。
struct War3RenderSkinnedModelInstanceV2 {
    War3RenderModelInstanceV2 instance;
    std::uint32_t firstGroup, firstAlpha;
};
struct War3RenderSkinUpdateV2 {
    std::uint32_t structSize, instanceCount, groupCount, alphaCount;
    std::uint64_t sceneVersion, contentVersion;
    std::uint32_t matrixFormat, matrixStrideBytes, worldTransformApplied, reserved;
    const War3RenderSkinnedModelInstanceV2* instances;
    const War3RenderGroupMatrixV2* groups;
    const float* alphas; // 有限值；着色器按 saturate 取可见度，<=0 的网格不输出像素。
};
struct War3RenderSkinnedModelSceneV2 {
    std::uint32_t structSize, vertexCount, indexCount, layerCount, meshCount, modelCount, textureCount;
    const War3RenderSkinnedModelVertexV2* vertices;
    const std::uint16_t* indices;
    const War3RenderModelLayerV2* layers;
    const War3RenderSkinnedModelMeshV2* meshes;
    const War3RenderSkinnedModelV2* models;
    const War3RenderTextureMipChainV2* textures;
    const War3RenderSkinUpdateV2* initialPose;
};
// 模型槽互斥：新提交替换旧静态/蒙皮模型，地形和叠加不变。清空须全部计数为0，initialPose仍必需。
// sceneVersion 在同一 renderer 内严格递增且非0；contentVersion 在该 scene 内严格递增且非0。
// 更新不能更换实例数、模型序号或组/透明度片段。输入期间深拷贝；返回0仅代表CPU接收。
// 静态预算沿用旧模型；组矩阵/透明度各最多1048576条；更新<=64MiB；完整提交<=320MiB。
// 矩阵/变换元素须有限且绝对值<=1e7；透明度只要求有限，颜色仍限0..1。
// 网格必须依序连续覆盖索引，模型依序连续覆盖网格；累计 layer 绘制次数<=131072。
// updateSkinMatrices 不更新旧静态槽；切回旧 submitModelScene 会清除蒙皮模式。
// GPU 使用两个动态缓冲槽：填充非活动槽后再发布；此预算不是进程RSS/GPU总驻留上限。
struct War3RenderSkinStatusV2 {
    std::uint32_t structSize;
    std::int32_t result; // 0=当前内容已Draw，2=尚待Draw，负值=上传/设备失败
    std::uint32_t active, reserved;
    std::uint64_t frameCount, sceneVersion, contentVersion;
    std::uint64_t staticUploadCount, matrixUploadCount; // 成功GPU安装次数，不是CPU接收/字节/每个资源计数
};
struct War3RenderSkinnedCapturedFrameV2 {
    std::uint32_t structSize;
    War3RenderCapturedFrameV2 frame;
    War3RenderSkinStatusV2 skin; // 与像素同次Draw/读回；绝不通过随后查询当前状态拼接
};

// 粒子/条带可选尾部：全世界空间动态三角形，过滤编号见枚举；不改变旧模型/蒙皮结构。
enum War3RenderParticleFilterV2 : std::uint32_t {
    WAR3_PARTICLE_BLEND, WAR3_PARTICLE_ADDITIVE, WAR3_PARTICLE_MODULATE,
    WAR3_PARTICLE_MODULATE2X, WAR3_PARTICLE_ALPHAKEY, WAR3_PARTICLE_ADDALPHA, WAR3_PARTICLE_OPAQUE
};
struct War3RenderParticleVertexV2 { float position[3], uv[2], color[4]; };
struct War3RenderParticleDrawV2 { std::uint32_t firstVertex, vertexCount, texture, filter; };
struct War3RenderParticleUpdateV2 {
    std::uint32_t structSize, vertexCount, drawCount, reserved;
    std::uint64_t sceneVersion, contentVersion;
    const War3RenderParticleVertexV2* vertices;
    const War3RenderParticleDrawV2* draws;
    // 同次更新的模型矩阵，版本必须相同；在同一锁中发布，不能分两次拼装新模型/旧粒子。
    const War3RenderSkinUpdateV2* modelPose;
};
struct War3RenderParticleSceneV2 {
    std::uint32_t structSize, textureCount;
    const War3RenderTextureMipChainV2* textures;
    const War3RenderSkinnedModelSceneV2* models;
    const War3RenderParticleUpdateV2* initialFrame;
};
struct War3RenderParticleStatusV2 {
    std::uint32_t structSize, vertexCount, drawCount, reserved;
    // renderer 生命周期累计：成功安装非空纹理库 +1；成功安装初始/更新帧 +1（包括空帧）。
    // Draw/capture、拒绝和失败不增加；动态帧不重新传纹理。
    std::uint64_t sceneVersion, contentVersion, textureUploadCount, dynamicUploadCount;
};
struct War3RenderParticleCapturedFrameV2 {
    std::uint32_t structSize;
    War3RenderSkinnedCapturedFrameV2 models;
    War3RenderParticleStatusV2 particles; // 与像素/模型同次Draw，不是随后查询的当前状态
};
// 单场景粒子纹理<=256/64MiB；动态顶点<=1048576，绘制批次<=65536，紧密覆盖三角形。
// 新场景/更新与模型一起深拷贝后发布。旧模型/姿势写入口会清除粒子模式；
// 地形、相机、视口、环境、叠加及只读查询保持该模式。纯粒子用有效的空模型/初始姿势前缀。
// GPU部分失败不出具新版本截图；没有把CPU接收或状态查询当作GPU完成栅栏。
static_assert(sizeof(War3RenderParticleVertexV2)==36 && sizeof(War3RenderParticleDrawV2)==16);

struct War3RenderEditorPresentationTargetV2;
struct War3RenderEditorVisibilityUpdateV2;
struct War3RenderEditorCapturedFrameV2;
struct War3RenderEditorModelSceneV2;
struct War3RenderEditorCompletedFrameV2;
struct War3RenderModelSceneLayerV2;
struct War3RenderModelLayerUpdateV2;
struct War3RenderModelLayersTargetV2;
struct War3RenderLayeredCapturedFrameV2;
struct War3RenderFrameMetricsV2;
struct War3RenderFrameMetricsPageV2;
struct War3RenderEditorInstanceRosterV2;
struct War3RenderInstanceRosterResultV2;
struct War3RenderTerrainBlightCapabilitiesV2;
struct War3RenderTerrainBlightFrameV2;
struct War3RenderTerrainGridCapabilitiesV2;
struct War3RenderTerrainGridFrameV2;
struct War3RenderTerrainGridCaptureFactsV2;
struct War3RenderPluginApiV2 {
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    War3RendererHandle (WAR3_RENDER_CALL* createRenderer)(const War3RenderCreateInfoV2*);
    void (WAR3_RENDER_CALL* destroyRenderer)(War3RendererHandle);
    std::int32_t (WAR3_RENDER_CALL* attachNativeWindow)(War3RendererHandle, void*);
    std::int32_t (WAR3_RENDER_CALL* resize)(War3RendererHandle, std::uint32_t, std::uint32_t);
    std::int32_t (WAR3_RENDER_CALL* submitMesh)(War3RendererHandle, const War3RenderMeshV2*);
    std::int32_t (WAR3_RENDER_CALL* setCamera)(War3RendererHandle, const War3RenderCameraV2*);
    void (WAR3_RENDER_CALL* requestFrame)(War3RendererHandle);
    std::int32_t (WAR3_RENDER_CALL* getStatus)(War3RendererHandle, War3RenderStatusV2*);
    // 仅无窗口验收/显式导出调用，最多等待 10 秒。输出紧密 RGBA8，bufferSize >= width*height*4。
    // 像素与 receipt 在同一锁下返回；不能用随后 getStatus 的新帧替代截图身份。并发截图拒绝。
    std::int32_t (WAR3_RENDER_CALL* captureRgba)(War3RendererHandle, void*, std::uint64_t, War3RenderCaptureInfoV2* receipt);
    // 可选尾部扩展；宿主先核 structSize。0=当前状态的完成帧，2=首帧/最新状态仍待渲染。
    // 仅成功写 output；变更窗口/尺寸/网格/相机后不能拿旧回执拾取新视图。
    std::int32_t (WAR3_RENDER_CALL* getViewInfo)(War3RendererHandle, War3RenderViewInfoV2*);
    // 可选尾部扩展：先核 offsetof+sizeof 指针及非空，再调用。返回0仅表示CPU接收；
    // 完成帧仍以 getViewInfo 为准。无效输入保留旧场景；submitMesh 切回高度色并清材质。
    std::int32_t (WAR3_RENDER_CALL* submitTerrainScene)(War3RendererHandle, const War3RenderTerrainSceneV2*);
    // 可选尾部；先核 structSize 与函数非空。旧 submitTerrainScene 仍只提交/采样 mip0。
    std::int32_t (WAR3_RENDER_CALL* submitTerrainMipScene)(War3RendererHandle, const War3RenderTerrainMipSceneV2*);
    // 可选尾部。先核 offsetof+sizeof 指针及非空；缺能力不能用旧 capture 后拼 getViewInfo 代替。
    // 复用 captureRgba 的预算/等待规则。0 才写 RGBA 与完整同帧回执；2=超时或已被新状态替代。
    // 返回时核当前 surface/scene/view epochs；pending/错误时不写 buffer 或 receipt。
    // receipt->structSize 至少为当前大小；更大结构只写已知前缀，未知尾字节不动。
    std::int32_t (WAR3_RENDER_CALL* captureFrameRgba)(War3RendererHandle, void*, std::uint64_t,
        War3RenderCapturedFrameV2* receipt);
    // 可选尾部。先核 offsetof+sizeof 指针及非空。返回 0 仅表示 CPU 接收；无效输入保留旧模型。
    std::int32_t (WAR3_RENDER_CALL* submitModelScene)(War3RendererHandle, const War3RenderModelSceneV2*);
    // 可选尾部。先核 offsetof+sizeof 指针及非空。深拷贝阴影图；无效输入保留旧环境。
    std::int32_t (WAR3_RENDER_CALL* setEnvironment)(War3RendererHandle, const War3RenderEnvironmentV2*);
    // 可选尾部。只替换最近一次 submitModelScene 的实例变换与颜色：数量及每个实例的 model 必须与之相同，
    // 不重传几何与贴图（拖动等局部更新用）。再次 submitModelScene 后此覆盖失效。无效输入保留旧实例。
    std::int32_t (WAR3_RENDER_CALL* updateModelInstances)(War3RendererHandle, const War3RenderModelInstanceV2*, std::uint32_t);
    // 可选尾部。深拷贝叠加线；无效输入保留旧叠加。
    std::int32_t (WAR3_RENDER_CALL* setOverlayLines)(War3RendererHandle, const War3RenderOverlayLineV2*, std::uint32_t);
    // 可选尾部：每个入口均先检查 offsetof+sizeof(函数指针) 及非空，旧插件不具备这些能力。
    std::int32_t (WAR3_RENDER_CALL* submitSkinnedModelScene)(War3RendererHandle, const War3RenderSkinnedModelSceneV2*);
    std::int32_t (WAR3_RENDER_CALL* updateSkinMatrices)(War3RendererHandle, const War3RenderSkinUpdateV2*);
    // 函数返回0表示查询成功；仍须检查 status.result。计数/版本属于实际安装并Draw的内容。
    std::int32_t (WAR3_RENDER_CALL* getSkinStatus)(War3RendererHandle, War3RenderSkinStatusV2*);
    // 和旧 captureFrame 相同的10秒上限；被新场景/姿势/视图替代返回2且不写任何输出。
    std::int32_t (WAR3_RENDER_CALL* captureSkinnedFrameRgba)(War3RendererHandle, void*, std::uint64_t,
        War3RenderSkinnedCapturedFrameV2*);
    // 每个入口均须先检查structSize及非空。旧插件缺该尾部时只禁用粒子，不能禁用其他模型。
    std::int32_t (WAR3_RENDER_CALL* submitParticleModelScene)(War3RendererHandle, const War3RenderParticleSceneV2*);
    std::int32_t (WAR3_RENDER_CALL* updateParticleModelFrame)(War3RendererHandle, const War3RenderParticleUpdateV2*);
    std::int32_t (WAR3_RENDER_CALL* captureParticleFrameRgba)(War3RendererHandle, void*, std::uint64_t,
        War3RenderParticleCapturedFrameV2*);
    // 编辑器显隐可选尾部：每个入口独立核offsetof+sizeof指针及非空；旧插件明确不支持。
    std::int32_t (WAR3_RENDER_CALL* getEditorPresentationTarget)(War3RendererHandle,
        War3RenderEditorPresentationTargetV2*);
    std::int32_t (WAR3_RENDER_CALL* submitEditorVisibility)(War3RendererHandle,
        const War3RenderEditorVisibilityUpdateV2*);
    // 只返回指定epoch/显隐修订的完成帧；被新模型/姿势/掩码替代返回2且不写输出。
    std::int32_t (WAR3_RENDER_CALL* captureEditorPresentationFrameRgba)(War3RendererHandle,
        std::uint64_t expectedModelEpoch, std::uint64_t expectedPresentationRevision,
        void*, std::uint64_t, War3RenderEditorCapturedFrameV2*);
    // 新模型与显隐同次发布，禁止先提交新模型再用第二次请求补掩码导致短暂显现。
    std::int32_t (WAR3_RENDER_CALL* submitEditorModelScene)(War3RendererHandle, const War3RenderEditorModelSceneV2*);
    // 可选只读尾部：先核 offsetof+sizeof 指针及非空。0=当前请求的同次完成 Draw/Present，
    // 2=未完成/已被新请求替代；不等待、不读回像素，失败不写 output。接受更大结构，保留未知尾部。
    std::int32_t (WAR3_RENDER_CALL* getEditorCompletedFrame)(War3RendererHandle, War3RenderEditorCompletedFrameV2*);
    // 通用附加模型层尾部：独立修订，始终绑定主模型 epoch；旧插件尺寸不足时明确不支持。
    std::int32_t (WAR3_RENDER_CALL* submitModelSceneLayer)(War3RendererHandle, const struct War3RenderModelSceneLayerV2*);
    std::int32_t (WAR3_RENDER_CALL* updateModelSceneLayer)(War3RendererHandle, const struct War3RenderModelLayerUpdateV2*);
    std::int32_t (WAR3_RENDER_CALL* removeModelSceneLayer)(War3RendererHandle, std::uint64_t layerId,
        std::uint64_t parentModelEpoch, std::uint64_t expectedRevision);
    std::int32_t (WAR3_RENDER_CALL* getModelLayersTarget)(War3RendererHandle, struct War3RenderModelLayersTargetV2*);
    std::int32_t (WAR3_RENDER_CALL* captureLayeredFrameRgba)(War3RendererHandle, std::uint64_t expectedLayersRevision,
        void*, std::uint64_t, struct War3RenderLayeredCapturedFrameV2*);
    // 可选只读测量尾部：先核 offsetof+sizeof 指针及非空。仅枚举实际完成的 Draw/Present，
    // 不等待、不读回、不调度；记录身份与时长同次发布。分页合同见 RenderPluginFrameMetrics.h。
    std::int32_t (WAR3_RENDER_CALL* getFrameMetrics)(War3RendererHandle, std::uint64_t afterFrame,
        struct War3RenderFrameMetricsV2* records, std::uint32_t capacity, struct War3RenderFrameMetricsPageV2*);
    // 可选驻留资产roster事务：只追加尾部，独立核尺寸与非空；0仅为CPU接收票据。
    std::int32_t (WAR3_RENDER_CALL* submitEditorInstanceRoster)(War3RendererHandle,
        const War3RenderEditorInstanceRosterV2*, std::uint64_t* ticket);
    std::int32_t (WAR3_RENDER_CALL* getEditorInstanceRosterResult)(War3RendererHandle,
        std::uint64_t ticket, War3RenderInstanceRosterResultV2*);
    // 同一roster事务的可选能力。旧尺寸不足时不能发送未知扩展结构。
    std::int32_t (WAR3_RENDER_CALL* getEditorRosterCapabilities)(War3RendererHandle, std::uint32_t* flags);
    // 按确切票据取消仍未提交的roster；已完成只返回原终态，不回滚完成帧。
    // 旧插件没有此尾部时，宿主必须保留在途owner直到可证明的终态。
    std::int32_t (WAR3_RENDER_CALL* cancelEditorInstanceRoster)(War3RendererHandle,
        std::uint64_t ticket, War3RenderInstanceRosterResultV2*);
    // 可选只读尾部；先核offsetof+sizeof指针及非空。缺能力不能发送新模型层角色。
    std::int32_t (WAR3_RENDER_CALL* getModelLayerCapabilities)(War3RendererHandle, std::uint32_t* flags);
    // 静态draw层sampler可选能力；先核表容量及非空，再检查明确的格式/flags。
    // 合同见RenderPluginStaticSamplers.h。不能按API大小或skin能力推断支持。
    std::int32_t (WAR3_RENDER_CALL* getStaticModelSamplerCapabilities)(War3RendererHandle,
        struct War3RenderStaticSamplerCapabilitiesV2*);
    // 独立荒芜pass的可选尾部。两个入口分别核offsetof+sizeof指针及非空，
    // 再查询实际renderer能力；合同见RenderPluginTerrainBlight.h，不改变旧16纹理入口。
    std::int32_t (WAR3_RENDER_CALL* getTerrainBlightCapabilities)(War3RendererHandle,
        War3RenderTerrainBlightCapabilitiesV2*);
    // 返回0仅表示同步深拷贝/CPU接收；完整同帧提交，完成仍以getViewInfo/capture为准。
    std::int32_t (WAR3_RENDER_CALL* submitTerrainBlightFrame)(War3RendererHandle,
        const War3RenderTerrainBlightFrameV2*);
    // shared-grid可选尾部：先核三slot容量/非空和明确Caps，不改变旧43函数前缀。
    // 具体有界借用、空池、预算及捕获事实合同见RenderPluginTerrainGrid.h。
    std::int32_t (WAR3_RENDER_CALL* getTerrainGridCapabilities)(War3RendererHandle,
        War3RenderTerrainGridCapabilitiesV2*);
    std::int32_t (WAR3_RENDER_CALL* submitTerrainGridFrame)(War3RendererHandle,
        const War3RenderTerrainGridFrameV2*);
    std::int32_t (WAR3_RENDER_CALL* getTerrainGridCaptureFacts)(War3RendererHandle,
        const War3RenderViewInfoV2* expected, War3RenderTerrainGridCaptureFactsV2*);
};
using War3GetRenderPluginApiV2 = const War3RenderPluginApiV2* (WAR3_RENDER_CALL*)(std::uint32_t);
WAR3_RENDER_EXPORT const War3RenderPluginApiV2* WAR3_RENDER_CALL
War3MapEditor_GetRenderPluginApiV2(std::uint32_t requestedApiVersion);
}
