<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# War3RenderCore 0.1.1

本机唯一上游：经典 MDX 有界解析、冻结姿态参考、预热 CPU 批量求值、插件输入可移植校验与原创测试。
只依赖 C++20 编译器和标准库，不包含 Qt、设备、GPU 后端、MPQ 读取或游戏安装逻辑。
浮点选项为 MSVC `/fp:precise`、其他编译器 `-ffp-contract=off`。

```sh
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release --parallel 1
ctest --test-dir build/x64 -C Release -j 1 --output-on-failure
cmake -S . -B build/x86 -A Win32
cmake --build build/x86 --config Release --parallel 1
ctest --test-dir build/x86 -C Release -j 1 --output-on-failure
```

32 位 MinGW 的默认 x87 会保留扩展精度，可能连已解析 float 与相同 float 常量的比较也不同。
本轮标准库检查显式采用 SSE2，与 MSVC Win32 的计算精度配置对齐；不启用 fast-math，也不改变原断言。

```sh
cmake -S . -B build/mingw -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=-msse2 -mfpmath=sse"
cmake --build build/mingw --parallel 1
ctest --test-dir build/mingw -j 1 --output-on-failure
```

该配置只用于 x86/SSE2 编译器，不应用到其他架构；跨编译器数值对照仍须实际重算夹具。

FORGE 通过 WAR3_RENDER_CORE_SOURCE_DIR 引用这个仓库，并检查锁定提交及已跟踪文件 SHA。
算法只保留一份。历史 War3Map 命名空间和部分头路径保留以减少 ABI/调用变动。
插件合同是原输入测试的标准库闭包，游戏渲染接口尚未接通。

许可见 COPYRIGHT.md；来源见 docs/PROVENANCE.md 和 docs/MIGRATION.tsv。
暂不接受外部贡献。不提交暴雪模型、贴图或反编译材料，不设置远程，不自动公开。

Computed/ExternalGroups 是近似预览；新外部帧输入验证并复制宿主提供的数值。
绘制只读资产视图本轮仅有规格；原版运行时、游戏集成与共享绘制尚未验证。

0.1.1 将 FORGE `eb35f87` 的轨迹接缝、动作局部逆序区间和模型层契约同步到唯一上游，
保留 0.1.0 已求值外部帧入口。迁移清单与行为边界见 `docs/UPDATES_0_1_1.md`。

修改流程固定为：先在本仓库修改并验证、建立本地提交，再在 FORGE 运行
`python tools/dev/update-render-core-lock.py`，由该脚本重新生成提交/版本/全文件 SHA 锁。
FORGE 配置及实际消费者构建都拒绝未提交的共享修改；不要在 FORGE 恢复第二份实现。
共享仓库保持无远程、无推送，发布时机仍由作者决定。
