<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# 无动画控制器的静态模型

经典版1.27a会先检查整个模型是否需要动画控制器。无需控制器时，游戏直接使用模型矩阵；
该路径不计算节点轴心的平移、旋转或缩放。普通动画路径仍会读取轴心，即使其中某个节点恰好没有轨迹。

共享核心为这条路径提供一个保守子集。`ParseMdx` 完整读取后，同时检查：

- 源文件只有完整识别的静态块，没有外部动画文件、未知块或控制器块。
- 节点仅为BONE/HELP，有完整合法拓扑和矩阵组，没有任何TRS轨迹、继承或朝向标志。
- 材质没有透明度、贴图或坐标动画；GEOA没有轨迹或未解释数据。被后续GEOA覆盖的动态条目也会阻止旁路。
- 数量、字节边界、几何和引用继续遵守原有检查。

符合条件时，解析器创建只读的 `MdxNoControllerStaticSource`，封存节点身份、原标志与PIVT浮点位。
它不复制整个MDX文件。编译和参考采样会重新检查当前模型的全部静态条件，并核对这些封存字段；
解析后新增动画或动态对象，不能沿用旧资格。

该路径允许保留非有限PIVT，以及变换解释中未使用的节点高位 `0x80000000`。
这两个值均不改写：不会把NaN填成零、删掉骨骼或补造矩阵组。
仅对符合完整资格的模型，姿态矩阵直接采用模型局部单位矩阵，由宿主继续施加实例世界变换。
其它未知节点标志仍被拒绝；不满足资格时，非有限PIVT仍报 `MDX_NUMBER_INVALID`，并说明阻止静态路径的原因。

存在上述兼容数据时，读取、编译和参考采样均给出 `MDX_NO_CONTROLLER_STATIC` 警告，标明PIVT来源和原值数量。
`CompiledModel::UsesNoControllerStatic()` 可查询编译后的路径。
`MdxModel::pivots`、参考节点及批量 `NodePose::pivot` 保留原始来源，可能非有限，不能作为有效附着点。
实际姿态应读取有限的 `localMatrix/worldMatrix`；当前资格中不包含粒子、附着、光源或事件。

这与 `ParseMdxRigidStaticPreview` 的缺少MTGC预览是两种独立能力。
本路径保留并使用已存在的节点与矩阵组，不获得MDX写出能力，也不改变外部帧的有限值、身份和来源检查。

原创回归登记为 `render-no-controller-static`，包含原始NaN载荷与负零、静态父子链、
参考/批量矩阵与几何、热路径无分配、源文件动态内容拒绝、解析后资格失效及原有动画继续生效。
这些检查验证CPU读取和求值合同；实际游戏画面与宿主GPU显示仍需要另行核对。

0.1.20新增静态caster预算入口：

```cpp
auto parsed = ParseMdxNoControllerStaticBudgeted(bytes, maximumResidentBytes, limits);
// 成功：parsed.value->model 和 parsed.value->chargedPeakBytes
```

它先用同一个有界Reader无模型分配扫描全部静态记录，按调用方预算计入模型、
容器条目、NUL前原文字节、法线/UV/矩阵数组、静态GEOA、临时图元表、连续节点拓扑表
与最终不可伪造来源的nodes/pivotBits。第二遍复用既有parser，但使用已计量的精确reserve；
不会先把大模型展开，再检查编译预算。动态/未知块即使在文件末尾，也在展开前拒绝。

`chargedPeakBytes`表示有效存储的保守阶段峰值：模型保留数组，加上GEOA主表与当前
GEOS/GEOA局部表/节点拓扑表中的最大者；再与模型加来源封存的阶段比较。重复合法块
累计计数，静态GEOA仍按文件顺序最后值生效；重复PIVT继续由原解析规则拒绝。
输入原字节为借用，不在该预算内；固定调用栈、有界错误诊断、分配器管理数据（包括
shared_ptr控制块）和系统RSS不属于该有效存储口径。字符串按原文长度加终止符计量，
不把标准库的小字符串布局或分配粒度冒充跨编译器合同。最大预算为128MiB，只能收紧。

此入口只接受无控制器caster子集，并将缺失法线等本来无法编译的模型提前拒绝。
成功不复制普通读取的提示性诊断；后续Compile仍检查封存资格并给出兼容警告。
预算耗尽为 `MDX_STATIC_PARSE_BUDGET`，动态/未知语义为 `MDX_STATIC_PARSE_UNQUALIFIED`，
结构损坏和引用错误保持原有独立诊断；失败不返回部分模型。普通 `ParseMdx` 默认行为不变。

预算回归加入既有 `render-no-controller-static`：精确/少一字节、原数量硬上限、
父链环、重复静态GEOA、大模型小预算及末尾动态块分配前拒绝。新增探针只检查失败前
没有大型模型数组分配；它不把有效存储计量等同于分配器或RSS测量。
