# KMTA 与 PREM 的显式预览合同

0.1.13 的默认材质求值保持 Strict。第九工厂参数
`MaterialAlphaKeySamplingPolicy::IdenticalDuplicatesPreview` 仅在材质通道启用时可用。
资格检查覆盖完整轨迹：模式0至3、全部使用的值/切线有限、非全局、帧序非递减；
同帧值及适用切线必须原位相同。真正下降、同帧冲突、全局重复和未知尾部仍拒绝。
编译保留全部原序键；没有排序、删键、改源文件或全透明度回退。

成功姿势的 `MaterialAlphaPreviewWarnings(instance)` 与同次 Diagnostics 关联。
每条48字节来源记录包含材质/层、左右原键字节、原帧、值/切线位与插值模式。
诊断码 `MDX_MATERIAL_ALPHA_IDENTICAL_DUPLICATES_PREVIEW` 表示预览近似，不能授予保存资格。
默认 `PoseDiagnostic` 仍32字节；独立警告工作区只在显式模式预热并纳入 StorageBytes。
Evaluate 不分配。失败姿势清空 typed 警告并保持唯一首 Error。

PREM 的公共读取结果 `MdxModel::premEmitters` 保留源记录序、物理偏移、公共节点身份、
260字节路径原位、六个静态标量及七种轨迹。轨迹保留原键序、原值位和实际源位置。
无法安全界定的外层/Node 仍按既有全模型门禁拒绝；可界定记录内的未知/坏参数只局部
标记 unsupported，保留父模型网格和诊断。后置 GLBS 在读取所有块后核对。
子资源来源解析、递归门禁、发射物理、出生锚点和子网格绘制属于消费者，解析成功
不能当成 PREM 完成或原版物理等价。

CPU 回归按 policy/sampling/negative/budgets/real-model 与 fields/local/bounds/real 分组。
真实样本只从环境变量读取，缺失跳过；执行者还须在测试前后绑定样本 SHA。
局部 Sleep 边界拒绝与未支持的 PREM 轨迹不得由此放宽。
