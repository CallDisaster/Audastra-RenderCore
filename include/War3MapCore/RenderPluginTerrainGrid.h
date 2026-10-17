// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginTerrainBlight.h"
#include <cstddef>
#include <type_traits>

extern "C" {
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_FETCH_V1 = 1;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_HAS_BLIGHT = 1;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_HAS_WATER = 2;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_DIMENSION = 2049;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_SOURCE_TILES = 262144;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_VERTEX_RECORDS = 1048576;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_INDICES = 6291456;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_BATCHES = 4096;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_GROUND_TEXTURES = 16;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_BLIGHT_BATCHES = 32;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_WATER_TEXTURES = 1;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_TEXTURE_DIMENSION = 2048;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_MIPS = 12;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_MIP_BYTES = 16 * 1024 * 1024;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_CHAIN_BYTES = 24 * 1024 * 1024;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_RGBA_BYTES = 64 * 1024 * 1024;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_GRID_MAX_WORKING_BYTES = 128 * 1024 * 1024;

// caller清零并填写容量；成功只写24B前缀并回structSize=24，更大输出尾部不动。
// 失败/容量不足不写输出。允许null renderer只查编译能力，不创建或访问GPU；
// 非null仍须有效handle。格式1精确报告下列16/32/1和reserved=0，未知值不能推断支持。
// host先核三个尾slot均在表容量内且非空，再核全部能力；提交前能力失效拒绝整帧。
struct War3RenderTerrainGridCapabilitiesV2 {
    std::uint32_t structSize, gridFormat;
    std::uint32_t maxGroundTextures, maxBlightBatches, maxWaterTextures, reserved;
};

// 完整row-major源grid；真实shaped XY/Z和最终CPU颜色，不在GPU重新估算HeightColor/光照。
// 坐标有限有界，XY逐位遵守原128世界单位步长，六个RGB均在[0,1]。
// textureRGB含原shade/仅必要时的近似荒芜tint，fallbackRGB为原HeightColor*shade。
struct War3RenderTerrainGridGroundVertexV2 {
    float x, y, groundZ;
    float textureR, textureG, textureB;
    float fallbackR, fallbackG, fallbackB;
};

// 与GroundGrid同一完整grid索引，不重复XY。waterZ=原waterBaseHeight+样式offset；
// RGB取原TerrainWaterColor(waterZ-shapedGroundZ)，保逐角原函数及浮点求值顺序。
struct War3RenderTerrainGridWaterVertexV2 { float waterZ, red, green, blue; };

// sourceTile是完整源tile索引，严格递增唯一，覆盖全部visible base（包括fallback）。
// fallback以其base batch的height-color sentinel识别，四项UV必须为0。
// 其余UV来自原CellUv半texel内缩矩形，不新增公式或改变mip/cell clamp策略。
struct War3RenderTerrainGridBaseQuadV2 {
    std::uint32_t sourceTile;
    float uvLeft, uvRight, uvTop, uvBottom;
};

// sourceTile严格递增唯一，允许来自hidden-base格。alphaLevel为原CPU四次
// alpha+=cornerAlpha/4.0f后clamp并lround(alpha*15)的0..15结果，不改sum/4或逐vertex透明度。
struct War3RenderTerrainGridWaterQuadV2 { std::uint32_t sourceTile, alphaLevel; };

// 只经submitTerrainGridFrame提交，不扩展467旧descriptor或blight-format1语义。
// 本输入、groundScene及其内嵌输入structSize均等于本机精确sizeof；指针只借用到调用返回。
// provider同步有界校验并深拷贝后才排队；失败不发布半份CPU场景，0仅为CPU接收。
struct War3RenderTerrainGridFrameV2 {
    std::uint32_t structSize, gridFormat, width, height;
    std::uint32_t gridVertexCount, baseQuadCount, baseIndexCount, baseBatchCount;
    std::uint32_t blightIndexCount, blightBatchCount, waterQuadCount, waterIndexCount;
    // 仅transition残余VB/indices/batches和共享ground纹理表<=16；唯一revision取此处。
    // transition为空时三项count0/ptrnull仍允许ground表供base使用，只适用于本新入口；
    // 禁止把这个嵌套scene交给旧submit入口，旧入口空场景语义不变。
    War3RenderTerrainMipSceneV2 groundScene;
    const War3RenderTerrainGridGroundVertexV2* groundGrid;
    const War3RenderTerrainGridWaterVertexV2* waterGrid;
    const War3RenderTerrainGridBaseQuadV2* baseQuads;
    const std::uint32_t* baseIndices;
    const War3RenderTerrainBatchV2* baseBatches;
    const std::uint32_t* blightIndices;
    const War3RenderTerrainBlightBatchV2* blightBatches;
    const War3RenderTextureMipChainV2* blightTexture;
    const War3RenderTerrainGridWaterQuadV2* waterQuads;
    const std::uint32_t* waterIndices;
    const War3RenderTextureMipChainV2* waterTexture;
};

// 非空grid：2<=width,height<=2049，checked gridV=width*height，
// tileCount=(width-1)*(height-1)<=262144；GroundGrid恰gridV条。
// sourceTile的SW=(tile/(width-1))*width+tile%(width-1)，corner偏移0/1/width/width+1。
// base virtualID=q*4+corner，每quad六索引严格{f,f+1,f+2,f+1,f+3,f+2}；
// baseIndices允许按材质稳定归组重排quad组，但每q在全列表恰一次、完整覆盖所有baseQuads，
// 不要求q依次递增。baseQuads自身仍按sourceTile升序；只有waterIndices要求按q连续顺序。
// base batches连续完整覆盖indices，仅不透明实际ground贴图或height sentinel；逐quad核UV资格。
// transition保旧32B顶点/实际ID/标准拓扑/批序，仅原alpha transition，禁止translucent；
// host核真实tile/XYZ/RGB来源，provider不把有界几何检查冒充地图来源认证。
// blightIndices只引用不透明贴图base quad（非fallback），每quad全pass最多一次；
// <=32个唯一canonical8x4 cell稳定批，组内q升序；不引用transition或water。
// water自身virtualID，按q逐quad六索引完整覆盖；取GroundGrid真实XY与WaterGrid.Z/RGB，
// alpha cell UV来自原4x4/128平方水atlas，单mip/max0；不宣称原水多mip/动画接通。
// 不按alphaLevel重排透明批；ground/blight完整连续mip及不采尾级沿原上限/UNORM/LOD保留。
//
// 各可空base/transition池：count0严格ptrnull，非零严格非null，完整覆盖相关indices。
// 空blight：两count0且Indices/Batches/Texture三ptrnull；非空时完整且一份纹理。
// 空water：两count0且WaterGrid/Quads/Indices/Texture四ptrnull；非空waterI=6Q且四ptr齐，
// WaterGrid恰gridV条。全空：width/height/gridV及所有count0、全部ptrnull，groundScene合法空；
// documentRevision仍显式。旧submitMesh/terrain/mip/blight成功必须清grid/water及其捕获事实。
//
// 唯一几何记录账=gridV+(water非空?gridV:0)+transitionV<=1048576；base/water各4Q虚拟域
// 也<=1048576，不作为额外存储顶点。四域indices合计<=6291456，base/transition/blight批次
// 加非空water一批合计<=4096；不能每pass另领帽。ground表16+独立blight1+water1不是旧表扩18。
// 各表所有RGBA合计<=64MiB，别名指针仍计独立owned副本；每级16MiB/链24MiB/尺寸2048/12级不变。
// builder/provider各原128MiB工作量口径包含全部记录/indices/quads/batches/mip描述符/RGBA、
// owned映射/实际capacity/校验bitset与排序去重scratch/上传metadata，分配前预收、最终同公式复核。
// 禁止先造旧整份baseVB再转换并隐去并存，不抬帽/降采样/删地表或hidden水来腾槽。
// 128MiB不是RSS或旧accepted/new pending/GPU并存峰值证明；来源/修订租约仍由host持有。
//
// GPU局部备齐全部新owner后一次swap，失败保旧完整GPU场景；沿CPU一次分配的sceneEpoch，
// GPU完成不得再次递增。顺序base->transition->blight->不透明模型/粒子/层->水只读depth->透明。
// base/blight/water fetch显式无IA layout/VB、独立index/VS SRV，BaseVertexLocation=0；
// transition和普通draw显式恢复旧layout/VB/shader/纹理/常量/blend/depth/raster并清fetch SRV。
// pick仍用完整CPU terrain而非transition VB，原DDA/8192/hidden-ground/水不可选语义不改。

// 与最近一次成功RGBA读取的同次实际grid mesh一起冻结；不是submitted/requested统计。
// passBits只表示该mesh已接受的blight/water资源存在，不证明像素/深度贡献或可见性；其余位拒绝。
// caller仅填写容量，成功只写已知160B前缀并回size160，未知尾部不写；失败不写任何输出。
struct War3RenderTerrainGridCaptureFactsV2 {
    std::uint32_t structSize, gridFormat;
    War3RenderViewInfoV2 view;
    std::uint32_t gridVertexCount, baseQuadCount, transitionVertexCount, blightIndexCount;
    std::uint32_t blightBatchCount, waterQuadCount, groundTextureCount, passBits;
};

// getTerrainGridCaptureFacts(handle,const War3RenderViewInfoV2* expected,Facts*)不触发绘制/读取。
// expected非null且structSize精确；同锁逐项核revision/物理尺寸/frame/3epochs/MVP与最近捕获副本一致。
// 当前state的revision/尺寸/3epochs仍须匹配、无pending或上传错误，不借旧completed；
// 同状态后续普通frame允许推进，不要求当前frame号等于捕获frame。0才写Facts；
// stale/非grid/捕获已换返回2，invalid/错误沿旧返回码，失败不写；旧入口清对应事实。

static_assert(sizeof(War3RenderTerrainGridCapabilitiesV2) == 24);
static_assert(sizeof(War3RenderTerrainGridGroundVertexV2) == 36);
static_assert(sizeof(War3RenderTerrainGridWaterVertexV2) == 16);
static_assert(sizeof(War3RenderTerrainGridBaseQuadV2) == 20);
static_assert(sizeof(War3RenderTerrainGridWaterQuadV2) == 8);
static_assert(offsetof(War3RenderTerrainGridBaseQuadV2, uvLeft) == 4);
static_assert(offsetof(War3RenderTerrainGridBaseQuadV2, uvRight) == 8);
static_assert(offsetof(War3RenderTerrainGridBaseQuadV2, uvTop) == 12);
static_assert(offsetof(War3RenderTerrainGridBaseQuadV2, uvBottom) == 16);
#define WAR3_GRID_POD(type) \
    static_assert(std::is_standard_layout_v<type> && std::is_trivially_copyable_v<type>)
WAR3_GRID_POD(War3RenderTerrainGridCapabilitiesV2);
WAR3_GRID_POD(War3RenderTerrainGridGroundVertexV2);
WAR3_GRID_POD(War3RenderTerrainGridWaterVertexV2);
WAR3_GRID_POD(War3RenderTerrainGridBaseQuadV2);
WAR3_GRID_POD(War3RenderTerrainGridWaterQuadV2);
WAR3_GRID_POD(War3RenderTerrainGridFrameV2);
WAR3_GRID_POD(War3RenderTerrainGridCaptureFactsV2);
#undef WAR3_GRID_POD
// 精确200/160为Windows x64合同；其它指针宽度的输入仍须用本机精确sizeof。
static_assert(sizeof(void*) != 8 || sizeof(War3RenderTerrainMipSceneV2) == 64);
static_assert(sizeof(void*) != 8 || sizeof(War3RenderViewInfoV2) == 120);
static_assert(sizeof(void*) != 8 || sizeof(War3RenderTerrainGridFrameV2) == 200);
static_assert(sizeof(void*) != 8 || offsetof(War3RenderTerrainGridFrameV2, groundScene) == 48);
static_assert(sizeof(void*) != 8 || offsetof(War3RenderTerrainGridFrameV2, groundGrid) == 112);
static_assert(sizeof(void*) != 8 || sizeof(War3RenderTerrainGridCaptureFactsV2) == 160);
static_assert(sizeof(void*) != 8 || offsetof(War3RenderTerrainGridCaptureFactsV2, view) == 8);
static_assert(sizeof(void*) != 8 || offsetof(War3RenderTerrainGridCaptureFactsV2, gridVertexCount) == 128);
}
