<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# RenderCore CPU：第 1 步

用途：FORGE 已解析的 MDX → 不可变编译资产 → 独立实例批量姿势 → 模型局部矩阵组与透明度。
本步不接 GPU、插件、Qt、世界页或游戏；所有结果只有 `PreviewApproximate`。
未从 WarVK 复制代码或着色器。2026-09-30 按作者决定迁移至独立共享仓库，
授权为 GPL-3.0-only，作者保留另行授权的权利；见仓库根 COPYRIGHT.md。

## 构建

从仓库根目录执行：

```sh
cmake -S . -B build/render-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/render-core --parallel 2
ctest --test-dir build/render-core --output-on-failure
```

静态库目标 `war3-render-core`（别名 `War3RenderCore::CPU`）只依赖标准库及现有数据头。
测试单独编译未改动的 `MdxModel.cpp` / `MdxPose.cpp`，不链接整个 Core/RenderSupport。
只构建库可设置 `-DWAR3_RENDER_CORE_BUILD_TESTS=OFF`。
编辑器顶层仍限 Windows x64；独立入口可在 Linux 构建，也可用 Windows `-A Win32` 检查 x86。

## 用法与寿命

```cpp
#include "War3RenderCore/PoseBatch.h"
// parsed 是宿主已解析的 MdxModel；来源摘要须由宿主计算，不能仅用路径。
auto compiled = War3RenderCore::CompileMdxModel(parsed, {11, 1},
    {virtualPath, sourceLayer, contentDigest});
if (!compiled.Succeeded()) { /* 将 diagnostics 交给宿主 */ return; }
const std::array assets{*compiled.value};
auto prepared = War3RenderCore::PoseBatchEvaluator::Create(assets, 101);
if (!prepared.Succeeded()) { /* 预算/预热失败 */ return; }
auto& evaluator = *prepared.value;
std::array<War3RenderCore::PoseRequest, 2> requests{};
for (std::uint32_t i = 0; i < 2; ++i) {
    requests[i].asset = {11, 1}; requests[i].instance = {i + 1, 1};
    requests[i].sequenceIndex = sequenceIndices[i];
    requests[i].sequenceElapsedMs = explicitTimesMs[i];
}
const auto result = evaluator.Evaluate(requests);
if (result.error != War3RenderCore::Code::Ok) { /* 整批拒绝，没有有效回执 */ return; }
for (std::uint32_t i = 0; i < result.instanceCount; ++i) {
    const auto& receipt = evaluator.Receipts()[i];
    if (receipt.error != War3RenderCore::Code::Ok) { /* 不提交该实例 */ continue; }
    // 将 Groups(i)、Alphas(i) 和 receipt 复制到宿主拥有的帧包。
}
```

`CompiledModel` 拥有数据，不借用原始 `MdxModel`。其句柄可以共享，但只读。
宿主为来源绑定不冲突的 `AssetIdentity`；来源/内容变化须换代。同编号代次的不同编译对象不能混入同一资产表。
实例编号不能为零；同批不能出现相同编号与代次。核心不推断游戏地址寿命或战争迷雾。
每个求值器单一所有者线程；宿主为新求值器分配不同的非零 `evaluationEpoch`。
成功时 `poseVersion` 单调递增，失败为零；同输入的数值结果逐位稳定，不代表版本号不变。
输出 span 只活到下一次 Evaluate、移动或销毁，失败调用也会使旧视图失效。
不要把它们借给异步渲染器；宿主必须复制到自己管理的帧存储。

## 输入与结果约定

`GroupMatrix` 为列主序 3×4、12 个 IEEE float32，48 字节步长；
`x' = m[0]*x + m[3]*y + m[6]*z + m[9]`，y/z 类推。
它已按组平均，处于模型局部空间，未乘实例世界变换。
每个 geoset 按原顺序贡献其 `GeosetLayout.groups` 范围。
外部组矩阵来源使用同一布局；必须提供完整数量、正确布局/空间/步长且全部有限。
同一批可混用自己求值与外部组矩阵；外部来源无节点矩阵输出，透明度仍按请求时间求值。
不能把本求值器的旧组矩阵 span 直接当下一次的外部输入，别名将拒绝。

骨骼覆盖结构写明节点、分量、空间、替换/叠加和作用阶段；W1 全部拒绝。
请求中有覆盖数量时该实例拒绝，传入非空覆盖池时整批拒绝（包括未引用的数据）。
模型局部相机由宿主分别为每个实例逆变换；核心不接收或修改世界相机。

## 范围与性能边界

加载时编译拓扑、节点索引、轨迹、组索引、顶点组号与透明度。
按键顺序/插值等条件性问题编译成状态，在访问该轨迹时仍返回参考诊断。
每帧只二分选段并求值；不重复建节点表/查父链，不用 map，不自动扩容。
不支持的节点不能跳过：完整骨架求值的拒绝范围比查询一条祖先链更大。
样条和相机沿用 FORGE 通用预览，缺相机仍按旧规则给静态预览警告；边界缺键仍拒绝。
正常矩阵组输出不扫描每个顶点；`SkinPreviewGeoset` 是可选 CPU 对照助手，
零缩放导致的变换后法线退化在该助手中拒绝，不能用组矩阵可生成推断所有顶点可蒙皮。
法线为同组 3×3 变换后归一化，非逆转置、非游戏等价；负/大于 1 的有限透明度保留参考语义。
预热容量是整批总容量。预算覆盖受控有效存储，不包含宿主原始模型、分配器开销或系统 RSS。
定宽描述符不是可直接 memcpy 的 IPC 协议；C++ 句柄、字符串、span 只供同进程使用。
浮点默认禁止 fast-math；同编译器/选项对照逐位比较，跨平台数值等价仍需本机验证。

## 第 2 步：只发布组矩阵与透明度

`BatchLimits::outputMode = PoseOutputMode::GroupsAndAlphas` 仍计算完整骨架，但只给单个实例
保留可复用 NodePose 暂存。成功回执的 nodes 为空，Groups/Alphas/Diagnostics 与 FullNodes
模式相同；默认仍是 FullNodes，旧调用行为不变。maxNodes 仍是整批计算节点上限，并为
最坏诊断预留 `4*maxNodes+maxInstances`，不是“能在世界中存在的实例总数”。

`BatchLimits` 从 24 B 增至 32 B（枚举加 reserved=0），有尺寸断言；PoseRequest=120 B、
PoseReceipt=96 B、GroupMatrix=48 B 均不变。StorageBytes 是求值器有效存储估计，包含
诊断/回执/单实例暂存，不包括资产、分配器、进程 RSS 或 GPU。

### 第 3 步处理 2000 个实例的约定

例如每实例 50 节点、10 geoset、60 组，创建一个紧凑求值器：

```cpp
War3RenderCore::BatchLimits limits;
limits.maxInstances = 64;
limits.maxNodes = 64 * 50;
limits.maxGroups = 64 * 60;
limits.maxGeosets = 64 * 10;
limits.maxBytes = 2ull * 1024 * 1024;
limits.outputMode = War3RenderCore::PoseOutputMode::GroupsAndAlphas;
// assets 是宿主持有的编译资产表；epoch 由宿主分配非零且不复用。
auto prepared = War3RenderCore::PoseBatchEvaluator::Create(assets, epoch, limits);
```

按容量拆成 31 批 64 个加最后 16 个，依次 Evaluate；每批成功后立即把组矩阵、透明度、
实例身份、evaluationEpoch/poseVersion 和必要诊断复制到宿主预分配的本帧包，再算下一批。
不得把内部 span 交给异步渲染，也不能把批内 Range32 原样当作全帧池偏移；拷贝时重新定位。
本轮 compact 测试实际做了这一条拷贝/复用链，旧批帧包在后续求值后仍保留。

异构模型按该批实际节点/组/geoset 总量装箱，任一上限先到就分批；单模型也超上限则明确
不支持/超预算，不悄悄截断它。资产表超过 4096 时按资产分组建多个求值器，分别分配 epoch。
批次失败不能发布空成功；实例失败也不得把上一帧 poseVersion 假报为本帧结果。

上述合成样本 Linux x64 有效存储：FullNodes 1,207,544 B，紧凑 615,344 B；复用 2000 实例
测得 C++ new/new[] 0 次。这不是整个插件/系统零分配，也不是实际游戏性能基准。
2000 实例一份宿主组池约 5,760,000 B，透明度 80,000 B，插件实例数据 144,000 B；再加
192,000 B 的 CPU 回执，合计 6,176,000 B，未含诊断/资产/帧包管理。两份在途帧包要另算预算。

RenderCore 透明度按 geoset 顺序；插件按实际绘制 mesh 顺序，第 3 步服务须建立 geoset→mesh
映射，不能把过滤后不同数量的数组直接拷过去。同理组/实例需要按插件模型排序和偏移规则打包。
GPU 内容版本是整份更新的 contentVersion，不是某一个实例的 poseVersion；宿主保留二者映射，
截图拿实际 contentVersion 查回整份帧包。本步尚未实现这个服务或任何世界页时钟。
