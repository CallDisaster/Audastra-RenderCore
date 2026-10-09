<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# 0.1.12：显式普通节点原序预览

本单元基于 0.1.11（`f8e2ee7e05efedbc73faaa6ac2294926eb54d731`）。它处理普通动作
KGTR/KGRT/KGSC 中的逆序帧在严格采样时使整个实例失效的边界。调用方必须显式选择
`NodeKeySamplingPolicy::SourceOrderPreview`；全部既有 `Create`、两参
`SampleMdxNodePose`、外部帧入口继续使用 `Strict`。

消费入口：

```cpp
auto batch = War3RenderCore::PoseBatchEvaluator::Create(assets, epoch, limits,
    geosetPolicy, materials, textures, textureAssets,
    War3RenderCore::NodeKeySamplingPolicy::SourceOrderPreview);
auto reference = War3Map::Render::SampleMdxNodePose(model, request,
    War3RenderCore::NodeKeySamplingPolicy::SourceOrderPreview);
```

宿主只在交互世界预览中选择该策略。CLI、保存、自动转换或发布验证不得默认选择它。
核心本身没有 UI、文件写入、游戏代码或相机/时钟管理职责；没有自动推断“当前为世界页”。

## 原序选择合同

只有非全局节点轨迹在所选动作中存在逆序边、因此严格编译没有可用连续索引时，
才进入 `SourceOrderNodeTrack.h` 的唯一共用 helper。核心批量和旧参考使用同一段选择代码，
旧浮点插值/归一化/父链计算仍分别执行。原始键、重复帧、不同值及切线完整保留；
编译器未排序、删除或改写源键。

1. 限定原始帧在所选动作闭区间内的键，保留原序。
2. 精确同帧时取最后一个源序同帧键，包括末尾重复起点。
3. 否则取第一个原序 `frame > time` 的键，以及它的原序前邻。
4. 无右邻或首键前时才沿旧规则连接末键和首键；接缝的时间差与动作长度仍按旧合同。
5. 动作内无键返回该 TRS 默认值并保留逆序警告；单键返回该键。合法零长度动作不会
   因本策略被新增拒绝。严格模式对逆序边覆盖的零长度/空区间仍保留原来的拒绝。

例如 `Birth` 源帧 `[4168,5419,6253,4168]` 不得先判断 `time >= last.frame`，
否则末键回到起点会使整个中段恒定取起点值。本合同在中段沿前两个区间插值，起点精确
匹配仍取最后源序起点键。对于 `Stand` 的不同值重复帧，本策略可能在精确匹配后跳变；
这是明确的近似预览规则，尚未确认原版缓存等价。

每个进入该策略的轨迹发布 `MDX_POSE_SOURCE_ORDER_PREVIEW` 警告，包含原始 objectId、
TRS 渠道和第一条相交逆序边的左右源帧。结果始终为 `PreviewApproximate`。
宿主必须保留所属实例/资产身份后展示警告，不能用此成功回执证明原版游戏表现。

## 拒绝与预算

未知策略、未知插值、全局逆序、无效全局索引、有符号接缝边界、非有限采样值、
无效端点四元数、坏父链/轴心/几何及原来的输入预算仍拒绝。只对所采样的键检查数值，
未改变旧核心对其他动作未访问坏值的隔离规则；曲线控制四元数原有显式警告回退继续保留。
零周期全局时钟以及有偏移帧的全局单键继续旧路径，不进入原序策略。

预热将诊断容量由 `4*maxNodes+maxInstances` 扩至 `7*maxNodes+maxInstances`，新增有效
存储为 `3*maxNodes*sizeof(PoseDiagnostic)`。`StorageBytes` 仍是有效存储估计，不能
当成分配器实际峰值/RSS。容量不足在预热拒绝；`Evaluate` 不分配、不重写资产。
严格模式的容量、预算公式和公共定宽结构保持旧值。

正常轨迹仍二分选段；原序分支线性检查原轨迹。按键仍受每轨迹 65536、每资产 262144
的既有硬上限约束。新增内部每次 `Evaluate` 最多 1048576 次原序按键访问；扫描前按整条
原轨迹数扣除预算，所有实例共享，下一次调用重置，默认Strict不消耗。超限实例回执为
`CapacityExceeded`，错误诊断为 `RENDER_CORE_SOURCE_ORDER_KEY_LIMIT`，含 objectId/TRS、
本轨迹所需次数和剩余次数。该码的 `leftFrame/rightFrame` 承载所需/剩余访问数，并由专用
格式化函数明确标为访问次数；宿主不得把它们标为源帧。已经完成的其他实例仍各自有有效
回执，超限实例没有部分输出。
宿主仍需要维持总实例/节点和交互任务预算；性能门禁须使用相同候选二进制实测，不能
由源审或编译成功推断。

`PoseRequest=120 B`、`PoseDiagnostic=32 B`、`BatchLimits=32 B` 及插件 C ABI 不变。
新增枚举值附加在原诊断码末尾，旧编号未改变。C++ 求值器新增私有策略状态，消费者必须
按新的头和静态库重编译；这里没有承诺旧 C++ 对象布局二进制兼容。

## 验收入口与来源

新 CPU 目标 `war3-render-source-order-tests` 提供三个独立 CTest：
`render-source-order-source-order`、`render-source-order-boundaries`、
`render-source-order-budgets`。原创四节点字节夹具覆盖 19/22 的不同值重复帧、45 的重复
Birth 末键、严格拒绝、独立数值、原序/源字节不变、编译/参考逐位差分、警告所属节点、
接缝/空轨迹/零动作、全局 0、数值/插值/边界拒绝、精确预热预算和无分配移动后求值，
以及同资产16/17实例恰好达到/超过每批访问硬帽、下一批重置和Strict拒绝码不变。
旧完整 CTest 仍需执行以验证严格路径。

若配置时存在 Python3，另登记 `render-source-order-real`，只从环境变量
`FORGE_SOURCE_ORDER_MODEL` 和 `FORGE_SOURCE_ORDER_OWNED_MAP` 读取本地自有副本。
未设置则返回 77/CTest skipped。wrapper 在 CPU 测试前后流式核验固定模型和地图 SHA：

- 模型：`2cb39727b36e830151ee0e23099fce2fb7a1feb71ec8f5025ddb104607fca696`，293672 B。
- 自有地图副本：`172e3b1b170b6d4d4665efa2a99cfcab3ca41f598e990b17664448a6a3c8880a`，341259558 B。

绑定依据是该地图副本的只读提取回执；wrapper 不执行地图、不提取 MPQ、不写安装目录。
它复测 14 个既有采样时刻及三个 Birth 中段，只验证本核心 CPU 结果；不证明 GPU 完成帧、
原版进入游戏、原版节点缓存或所有动作/地图兼容。测试源码不含本机路径，真实模型与地图
不提交、不分发。单独调用测试程序 `real-model` 组仅有 CPU 断言，来源签收必须由 wrapper
的前后 SHA 门禁完成。

本单元的实现和原创夹具由项目编写；未复制 WarVK 或第三方代码。只读参考资料是
[MDLX 动画字段读取器](https://github.com/flowtsohg/mdx-m3-viewer/blob/master/src/parsers/mdlx/animations.ts)
和[独立查看器的节点动画代码](https://github.com/flowtsohg/mdx-m3-viewer/blob/master/src/viewer/handlers/mdx/sd.ts)。
这两者仅帮助检查源序/字段及边界，不构成 Blizzard 运行时等价证据；本实现没有复制其
末帧先判断行为，也没有把第三方查看器当成原版裁判。

## 0.1.15：按原版反汇编改写

以上“原序选择合同”第 1–3 条是 0.1.12 的推测规则，0.1.15 起由 1.27a Game.dll 的实际做法替代：

- **区间切分**（`sub_6F78A090`，各轨迹编译器 KGTR `6F780970` 等在复制源键后调用）：源键按文件顺序复制、不排序。
  动作按 SEQS 文件顺序共享一个只进游标：先跳过帧小于起点的键（遇到帧大于终点的键即停），再连续收下
  所有帧不大于终点的键（不再检查起点）。逆序键因此留在所在动作的区间；与前一动作重叠或排在更早动作之后的
  动作，可能拿不到被前者收走的键，区间为空时取 TRS 默认值。
- **区间内查找**（`sub_6F789830`）：从区间首键向后线性扫描，停在下一键帧大于时间处，同帧取连续的最后一个。
  原版从上一帧结果继续扫描；同一动作内时间单调增加时与每次从首键扫描结果相同，时间回退时原版也从首键重扫。
- **取值**（位置/缩放 `sub_6F77AAD0`，旋转 `sub_6F77AE30`）：区间 0 键返回默认值，1 键直接取值；时间早于首键时
  按动作终点重查得到末键，时间差仍以首键计（插值比例为负，原版写法照搬）；末键接回首键，跨度为负时加动作长度；
  跨度为 0 时不论插值类型都取当前键。

单调且区间与按时间二分一致的轨迹不保存区间，沿用二分（结果相同）。严格模式仍拒绝区间内含逆序边的动作；
显式原序预览按上述规则取值并逐轨迹报告 `MDX_POSE_SOURCE_ORDER_PREVIEW`。插值的浮点运算仍是本实现，
不宣称与原版逐位相同。新增 CTest `render-source-order-game-ranges` 覆盖重叠与倒序排列的动作。
