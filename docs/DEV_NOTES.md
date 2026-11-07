# 开发笔记

给想改源码的人看的几处容易踩坑的地方。

## ABI 约束

- 目标必须是 **x86_64-windows-gnu**（`-target x86_64-windows-gnu`），32 位 TM 无法加载。
- `PluginInterface.h` 中 `ITMPlugin::GetAPIVersion()` 默认返回 **7**，对应 TrafficMonitor V1.86+。升级上游头文件前请比对接口变更表（头文件末尾有版本历史）。
- 唯一导出符号：`extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()`。不要改动其签名或调用约定。
- 插件源码只用 Win32 API（WinHTTP 通过 `GetProcAddress` 动态加载），**没有第三方运行库依赖**，不需要额外 DLL 一起分发。

## 时区换算

所有时段判定都在北京时间（UTC+8）上进行，即使本机在别的时区：

1. `GetLocalTime` → `SystemTimeToFileTime` 得到本地 FILETIME；
2. `GetTimeZoneInformation` 取 `Bias`（东八区为 -480），夏令时生效时再加 `DaylightBias`；
3. `北京时间 = 本地 + (480 + Bias) 分钟`；
4. `FileTimeToSystemTime` 反序列化，`wDayOfWeek` 由系统填充（不要自己用蔡勒公式算，FILETIME 往返最可靠）。

`CalcPeriodAt(const SYSTEMTIME&)` 是纯函数，接收任意北京时间，便于注入测试；`CalcPeriod()` 只负责取当前时间后转调它。

## 找下次切换点

从「今天 0 点」起逐日扫描未来 12 天，对每个工作日检查 9:00 / 12:00 / 14:00 / 18:00 四个候选点，取第一个满足「时刻 > 现在 且 状态 != 当前状态」的点。12 天足以覆盖中国最长假期（9 天）；若未来出现更长连休，需要调大扫描窗口。

切换点不在当天时，`SPeriod::switch_ymd` 记录日期，tooltip 会显示 `2026-10-08 09:00` 而不是只写 `9:00`。

## 节假日表

- 内置表在 `CN_HOLIDAY_RANGES_2026` / `CN_MAKEUP_WORKDAYS_2026`，`ExpandDateRange` 按真实日历逐日展开（跨月也正确）。
- 判定优先级：`workdays`（补班） > `holidays`（放假） > 「周一至周五」。
- **改次年安排优先用 ini 扩展**（`holidays=` / `workdays=`），不要改源码；只有需要把新表内置进 DLL 时才动常量并重新编译。
- 日期用 `ymd = 年*10000 + 月*100 + 日` 整数表示，便于比较与查表。

## 调试

- 鼠标悬停任务栏显示项可看完整 tooltip：余额、上次刷新、HTTP 状态、当前时段、剩余时间、下次切换。
- DLL 在 TM 运行期间被占用，**每次重编译前必须完全退出 TM**（管理员权限运行的 TM 需要同样权限才能结束）。
- 改时段规则请同步加 `test_period.cpp` 用例，并跑 `powershell -File build.ps1 -Test`。
- 设置对话框是纯 Win32 代码动态创建控件（无 .rc 资源），改动时注意控件 ID 与 `AddDlgItem` 的坐标布局。
