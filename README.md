# TrafficMonitor-DeepSeek

[![Release](https://img.shields.io/github/v/release/nimingik/TrafficMonitor-DeepSeek)](https://github.com/nimingik/TrafficMonitor-DeepSeek/releases/latest)
[![License: Anti 996](https://img.shields.io/badge/license-Anti%20996-blue.svg)](LICENSE)
![Platform: Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D6.svg)

在 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 任务栏里显示 **DeepSeek 账户余额** 和 **API 错峰时段（高峰 / 空闲）**，并倒计时到下一次价格切换。

```
任务栏示例：
  DeepSeek余额  ¥110.00
  DeepSeek时段  空闲·剩 15:59:59
```

> **非官方项目**：本项目与 DeepSeek（深度求索）及 TrafficMonitor 作者均无隶属、授权或合作关系。时段与价格规则的最终解释以 [DeepSeek 官方定价文档](https://api-docs.deepseek.com/quick_start/pricing) 为准。

---

## 功能

| 显示项 | 说明 | 示例 |
|---|---|---|
| `DeepSeek余额` | 账户余额（CNY），定时刷新 | `¥110.00` |
| `DeepSeek时段` | 当前高峰 / 空闲，及到下次切换的倒计时 | `空闲·剩15:59:59` |

- **余额来源**：`GET https://api.deepseek.com/user/balance`，默认每 5 分钟刷新（可设 1–1440 分钟）。
- **左键点击**显示项：立即刷新余额；**右键点击**：打开设置对话框。
- **Key 安全**：设置框里 API Key 默认以 `●` 掩码显示（防截屏泄露），可勾选临时显示明文，支持「测试连接」。
- **跨时区准确**：本机时间统一换算为北京时间（UTC+8）后判定，含夏令时偏差处理。
- **状态可查**：余额明细、上次刷新时间、错误原因、下次切换时刻都写在鼠标悬停 tooltip 里。

## 错峰时段规则

依据 DeepSeek 官方现行规则：

> 空闲时段价格为高峰时段价格的一半。北京时间周一至周五（不含中国法定节假日）9:00–12:00、14:00–18:00 为高峰时段；其余时段，包括周末及中国法定节假日全天均为空闲时段。

官方[定价页](https://api-docs.deepseek.com/quick_start/pricing)以 UTC 表述同一规则：`Peak hours are 01:00 - 04:00 and 06:00 - 10:00 UTC, Monday through Friday, excluding Chinese public holidays`，换算到 UTC+8 正好是上面的两段，且明确 `Off-peak rates are half of the peak rates`。

因此本插件的判定为：

| 时段 | 判定 | 价格 |
|---|---|---|
| 工作日 9:00–12:00、14:00–18:00 | 高峰 | 标准价 |
| 工作日 0:00–9:00、12:00–14:00、18:00–24:00 | 空闲 | 约半价 |
| 周六、周日全天 | 空闲 | 约半价 |
| 中国法定节假日全天 | 空闲 | 约半价 |
| 周末调休补班日 | 按工作日算高峰 | 标准价 |

倒计时会在四个切换点（工作日 9:00 / 12:00 / 14:00 / 18:00）自动翻转；若下次切换不在当天（如周末、长假），tooltip 会显示具体日期，例如 `下次切换: 2026-10-08 09:00`。

## 安装

### 方式一：直接下载（推荐）

1. 到 [Releases](https://github.com/nimingik/TrafficMonitor-DeepSeek/releases/latest) 下载 `DeepSeekMonitor-v1.0.0.zip` 并解压。
2. **完全退出 TrafficMonitor**（托盘图标右键 → 退出；若 TM 以管理员权限运行，安装脚本也要以管理员身份运行）。DLL 在 TM 运行期间被占用，不能热替换。
3. 双击 `install_update.bat`，把 TrafficMonitor 安装文件夹拖进去（或按提示输入路径）。脚本会校验目录、检查 TM 是否仍在运行，然后把 DLL 复制到 `<TM目录>\plugins\`。

   也可以手动复制 `DeepSeekMonitor.dll` 到该目录（`plugins` 不存在就新建）。
4. 重新打开 TrafficMonitor → 任务栏右键 → **显示项目** → 勾选 `DeepSeek余额` 和 `DeepSeek时段`。

### 方式二：从源码构建

见下方[开发](#开发)。

## 配置 API Key

1. 在任务栏上**右键点击** `DeepSeek余额` 或 `DeepSeek时段` → **打开设置**。
2. 粘贴 DeepSeek API Key（`sk-` 开头，在 <https://platform.deepseek.com/api_keys> 创建）。
3. 点 **测试连接**，显示「连接成功」即 Key 有效、余额已读出。
4. 可调整刷新间隔（1–1440 分钟），点 **确定** 保存。

配置保存在 `plugins\deepseek_monitor.ini`（也可直接编辑）：

```ini
[config]
api_key=sk-xxxxxxxx
refresh_minutes=5
holidays=2027-01-01,2027-01-02
workdays=2027-01-03
```

> ⚠️ **ini 明文存储 Key**，请勿分享该文件、截图或提交到任何仓库（本仓库 `.gitignore` 已排除 `*.ini`）。

## 法定节假日与调休

内置 **2026 年**安排，依据 [国务院办公厅关于 2026 年部分节假日安排的通知（国办发明电〔2025〕7 号）](https://www.gov.cn/yaowen/liebiao/202511/content_7047099.htm)：

| 假期 | 放假日期 | 调休补班日 |
|---|---|---|
| 元旦 | 1/1–1/3 | 1/4（周日） |
| 春节 | 2/15–2/23（9 天） | 2/14（周六）、2/28（周六） |
| 清明 | 4/4–4/6 | — |
| 劳动节 | 5/1–5/5 | 5/9（周六） |
| 端午 | 6/19–6/21 | — |
| 中秋 | 9/25–9/27 | 9/20（周日） |
| 国庆 | 10/1–10/7 | 10/10（周六） |

**后续年份无需重新编译**：国务院一般在上一年 11 月公布次年安排，把日期追加到 ini 即可，格式 `YYYY-MM-DD`，逗号分隔：

```ini
holidays=2027-01-01,2027-01-02   # 放假日（含周末），整段放假逐日列出
workdays=2027-01-03              # 调休补班日（周末但按工作日算高峰）
```

`holidays` / `workdays` 优先级高于「周一至周五」的默认判定，二者同时存在时以 `workdays` 为准。

## 常见问题

| 现象 / tooltip 错误 | 原因与处理 |
|---|---|
| `未配置` | 还没填 API Key，右键 → 打开设置 |
| `获取中` / `获取失败` | 首次请求进行中 / 失败，鼠标悬停看具体原因 |
| `HTTP 401` | Key 无效或已删除，重新生成 |
| `HTTP 402` | 账户余额不足，充值 |
| `HTTP 429` | 请求太频繁，调大刷新间隔、检查是否多端共用同一 Key |
| `HTTP 403` | 地区限制或代理/防火墙拦截 |
| 安装脚本报「DLL 被占用」 | TrafficMonitor 仍在运行，先彻底退出 |
| 插件没出现在显示项目列表 | 确认 DLL 在 `plugins\` 下、TM 版本 ≥ 1.86（插件接口 API v7）、架构为 x64 |

## 开发

**依赖**：zig 0.9.1（MinGW-w64 后端），无需 Visual Studio。

```powershell
# 安装编译器
pip install ziglang==0.9.1

# 编译 DLL
powershell -ExecutionPolicy Bypass -File build.ps1

# 编译 DLL + 运行 43 项时段规则单元测试
powershell -ExecutionPolicy Bypass -File build.ps1 -Test

# 若 zig 不在 PATH，可显式指定
powershell -ExecutionPolicy Bypass -File build.ps1 -ZigPath "C:\path\to\zig.exe" -Test
```

等价的手工命令：

```powershell
zig c++ -target x86_64-windows-gnu -shared -O2 -g0 DeepSeekMonitor.cpp -o DeepSeekMonitor.dll
zig c++ -target x86_64-windows-gnu -O2 -g0 test_period.cpp -o test_period.exe
.\test_period.exe
```

`test_period.cpp` 通过 `#include` 主源码直接测试纯函数 `CalcPeriodAt()`，覆盖工作日、午休、周末、2026 全部节假日与补班日、9:00/12:00/14:00/18:00 边界、跨周末与长假倒计时、ini 扩展，共 43 项断言。构建与测试均可用 `build.ps1 -Test` 在本地一键完成，不依赖 CI 服务。

### 目录结构

```
├── DeepSeekMonitor.cpp       插件全部源码（单文件，Win32 API，无第三方运行库）
├── PluginInterface.h         TrafficMonitor V1.86 官方插件接口头文件（含上游版权声明）
├── build.ps1                 一键构建 / 测试脚本
├── install_update.bat        Release 包内的通用安装脚本（拖拽/输入 TM 安装目录）
├── test_period.cpp            43 项时段规则单元测试
├── LICENSE                   "Anti 996" License 1.0 (Draft) 原文
├── THIRD_PARTY_NOTICES.md    版权归属、第三方与商标声明
└── CHANGELOG.md
```

### 提交改动

欢迎 Issue / PR。修改时段规则时请**同时补充 `test_period.cpp` 的用例**并保证 `-Test` 全绿；节假日表更新建议同时更新本 README 表格与 CHANGELOG。

## 致谢

- [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)（Zhong Yang）—— 优秀的任务栏监控软件与插件接口
- [ziglang](https://github.com/ziglang/zig-pypi) —— 让 Windows 上的 C++ 构建无需安装 Visual Studio
- DeepSeek —— 官方错峰定价规则与余额 API

## 许可证

本项目采用 **"Anti 996" License Version 1.0 (Draft)**，与上游 TrafficMonitor 一致，全文见 [LICENSE](LICENSE)。

该许可证要求：每份再分发或衍生副本都必须**显著且未经修改地**展示许可证全文与版权声明，并遵守适用司法辖区的劳动法规。本仓库的 `LICENSE` 即上游许可证文件的逐字副本。版权归属详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

按许可证原文，本软件以 **"AS IS"** 提供，不附带任何明示或默示担保。
