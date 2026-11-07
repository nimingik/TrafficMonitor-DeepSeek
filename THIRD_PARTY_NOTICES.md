# 第三方声明与版权归属

## 本项目

- **TrafficMonitor-DeepSeek**（DeepSeekMonitor 插件）
- Copyright (C) 2026 nimingik
- 本项目原创代码（`DeepSeekMonitor.cpp`、`test_period.cpp`、`build.ps1` 等）同样采用 **"Anti 996" License Version 1.0 (Draft)**，全文见 [LICENSE](LICENSE)。

采用该许可证的原因：本插件直接实现上游 TrafficMonitor 定义的插件接口，并随仓库再分发其官方接口头文件，属于上游作品的衍生/再分发副本。上游许可证第 1 条要求在每份衍生副本上**显著、未经修改地展示该许可证与版权声明**，因此本仓库的 `LICENSE` 是上游许可证文件的逐字副本（未作任何改动）。

## TrafficMonitor（上游）

- 项目：<https://github.com/zhongyang219/TrafficMonitor>
- 作者：Zhong Yang（zhongyang219），`zhongyang219@hotmail.com`
- 许可证：**"Anti 996" License Version 1.0 (Draft)**，全文见 [LICENSE](LICENSE)
- 版权声明：`Copyright (c) <2019> <copyright zhongyang219>`

本仓库中的 `PluginInterface.h` 取自 TrafficMonitor **V1.86** 的官方插件接口头文件（API 版本 7），文件头部保留了原作者的版权声明：

> `TrafficMonitor 插件接口` / `Copyright (C) by Zhong Yang 2021`

该头文件未作功能性修改，仅用于编译期类型声明；本插件不包含、也不链接 TrafficMonitor 的可执行代码。

## 构建期工具（不随二进制分发）

- **ziglang 0.9.1**（Zig c++ 编译器，MinGW-w64 后端）：MIT License，<https://github.com/ziglang/zig-pypi>。仅在从源码构建时使用，产物 DLL 不含其代码。
- Windows SDK 系统库：由 zig 自带的 MinGW-w64 导入库提供。

## 商标与免责

- **DeepSeek** 及其 API 是深度求索（DeepSeek）的产品与商标。本项目是**非官方**的第三方工具，与其无隶属、授权或合作关系。
- **TrafficMonitor** 是 Zhong Yang 的项目与商标，本插件同样为非官方扩展。
- 时段与价格规则的最终解释以 DeepSeek 官方公告为准；本项目按 [DeepSeek 官方错峰定价规则](https://api-docs.deepseek.com/quick_start/pricing) 实现，规则若变更请以官方为准并提交 Issue/PR。
- 本项目按许可证原文 "AS IS" 提供，不提供任何担保。
