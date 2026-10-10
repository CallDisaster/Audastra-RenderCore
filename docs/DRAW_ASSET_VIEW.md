<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# 只读绘制资产视图规格：W1

本文件定义后续 WarVK 共享绘制所需数据和今天的缺口。本轮没有新增视图 API、公开私有 `ModelData` 或接通 GPU 绘制。宿主继续负责资源解析优先级、实际内容摘要、设备安装和实例生命周期。

## 当前公共面的实际能力

| 数据 | 今天的公共入口 | 当前限制 |
| --- | --- | --- |
| 内容与来源身份 | `CompiledModel::Identity/Source` | 来源路径、层和内容摘要由宿主绑定；不证明游戏本次实际加载的 MDX/纹理来源 |
| 原 geoset 顺序与范围 | `CompiledModel::Geosets` | 暴露每 geoset 的组和顶点范围；不提供绘制材质层或完整拓扑 |
| 节点拓扑 | `NodeCount/Node` | 拓扑索引、objectId、parentId、flags；不是游戏原生节点或 palette 的直接索引 |
| 每顶点组槽号 | `CompiledModel::VertexGroups` | 须结合 geoset 顶点片段；不是跨资产通用组编号 |
| 动态组矩阵、alpha、RGB | `PoseBatchEvaluator::Groups/Alphas/Colors` | 输出借用求值器工作区，下一次 Evaluate/移动/销毁前有效；宿主需复制为自有帧包 |
| 静态 GEOA 颜色 | `CompiledModel::GeosetColors` | 不能替代 KGAC 当前帧求值结果 |
| bind-pose 位置、法线、UV、索引 | 公共 `MdxModel` 的 `MdxGeoset` | `CompiledModel` 未公开这些数组；UV 当前只有第一组，索引只保留受支持三角形 |
| MTGC/MATS 组成员 | 公共 `MdxGeoset::matrixGroupSizes/matrixIndices` | `CompiledModel` 未公开成员数组；成员为 MDX objectId，必须建立原生节点映射 |
| 材质层、纹理条目 | 公共 `MdxModel::materials/textures` | 静态描述可读；轨迹、全部UV层及宿主实际纹理内容尚未闭合 |

WarVK 不得通过 `Detail::ModelAccess`、私有 `ModelData`、编译器布局或指针偏移读取绘制资产。公共 `MdxModel` 是进程内解析 DTO，不是可直接 memcpy 的插件/IPC 协议，也不免除未来稳定只读视图的设计工作。

## 后续视图必须提供的数据

每个视图绑定一个不可变资产代际和明确的解析/编译合同版本。原始 geoset 顺序完整保留；不把过滤或合并后的 mesh 顺序假称原 geoset。计数、范围及索引域统一使用定宽整数，边界和资源寿命先验证再发布。

| 范围 | 必需字段与约束 |
| --- | --- |
| 资产身份 | 实际 MDX 摘要、来源层、规范虚拟路径、解析/编译合同版本；纹理依赖另有实际摘要和完整覆盖状态 |
| geoset | 原顺序 ID、顶点/索引/组片段、materialId、所有数据的完整性与受支持状态；空片段和不支持内容明确区分 |
| bind-pose 几何 | 模型局部 float32 位置与法线、三角形索引、索引类型、baseVertex 与索引域；不得发布已蒙皮或已乘世界变换的顶点冒充 bind pose |
| UV | 每个已支持层的维度、层 ID、范围及材质层 coordId 对应关系；缺层或未支持层拒绝受影响绘制，不回退首层 |
| 蒙皮拓扑 | 每顶点组槽号、每 geoset 组范围、组大小及有序 objectId 成员、跨节点/原生 palette 映射所需稳定身份；无组刚体单列 |
| 材质层 | 原层顺序、priorityPlane、filterMode、shadingFlags、textureId/replaceableId、coordId、textureAnimationId、静态alpha及动画标记；未解释尾部及未知字段具名 |
| 纹理来源 | 原路径/replaceable 语义、实际被宿主解析的来源/摘要、格式/尺寸/mip完整性、寻址与采样支持；视图不访问MPQ或持有宿主文件句柄 |

没有实现前，不把上述清单称作已支持。未来提供的数组须有共享不可变所有权或明确 lease；复制/序列化须显式编码版本、大小端、范围与预算。当前 C++ span、字符串、shared_ptr 和私有容器不属于跨进程 ABI。

## 语义登记与当前未支持项

| 项目 | 预览规则或当前支持 | 与原版对照的边界 |
| --- | --- | --- |
| 位置蒙皮 | 组内矩阵平均后使用列主序3×4变换；实例世界变换由宿主另行处理 | 原生组顺序、objectId、palette范围、刚体分支与同帧空间须逐项证明，数量相同不足以认定一致 |
| 法线蒙皮 | `SkinPreviewGeoset` 和 FORGE `SkinModel.hlsl` 对组矩阵3×3结果归一化；不是逆转置 | **预览规则，与原版不同**：已保存的 native kernel 使用同组3×3、不加平移，该 kernel 不做归一化。本轮保持现有行为；不推断原版后续照明永不归一化 |
| GEOA alpha/RGB | 原 geoset 顺序、静态值及当前已支持KGAO/KGAC通用预览；BGR读取后转RGB | 只有同输入核心参考对拍，不构成原版逐帧颜色签收 |
| 材质动画 | DTO记录KMTA/KMTF等存在标记；层alpha/textureId仍是静态入口 | 不能把动画标记当完整轨迹求值或材质可绘制；动态材质需后续版本与同帧合同 |
| UV与纹理动画 | DTO目前提供首组UV与静态层引用；TXAN 由材质贴图 sidecar 逐帧求值为层 UV 仿射（0.1.19） | 多UV层及coordId完整映射尚未提供；缺失不使用默认UV补齐 |
| 过滤、深度、剔除、采样 | DTO保存filterMode/shadingFlags与纹理flags，宿主后端维护现有规则 | 各后端画面等价尚需独立验证，不从字段相同推断执行顺序、深度/混合/alpha-test相同 |
| 节点与附属消费者 | 经典公共节点参与姿态父链 | 附件、事件、粒子/条带及碰撞等消费者不因骨架可求值就自动支持共享绘制；不得据此跳过原版计算 |

外部已求值帧必须显式绑定来源、`ModelLocal/World` 空间及版本，并携带完整组矩阵和实际alpha/RGB。World矩阵不能标成ModelLocal；适配器只有在世界变换关系与可逆性已证明时才能转换。绘制资产视图与动态帧分开，资产切换/过期、数量不符、未支持材质或缺数据时拒绝相应共享输入，宿主保留原版处理路径。
