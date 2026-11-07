//==============================================================================
// test_period.cpp —— 高峰/空闲时段规则单元测试（验证用）
// 规则：高峰 = 工作日 9:00-12:00 / 14:00-18:00；其余（含周末、法定节假日）为空闲
// 覆盖：日常工作日、午休、周末、2026 全部法定节假日、调休补班日、切换点倒计时、ini 扩展
//
// Copyright (C) 2026 nimingik
// SPDX-License-Identifier: Anti-996（许可证全文见 LICENSE）
//==============================================================================
#include "DeepSeekMonitor.cpp"
#include <cstdio>

static int g_pass = 0, g_fail = 0;

// 构造指定北京时间并计算（FILETIME 往返以填充 wDayOfWeek）
static SPeriod T(int y, int m, int d, int hh, int mm, int ss = 0)
{
    SYSTEMTIME st = { 0 };
    st.wYear = (WORD)y; st.wMonth = (WORD)m; st.wDay = (WORD)d;
    st.wHour = (WORD)hh; st.wMinute = (WORD)mm; st.wSecond = (WORD)ss;
    FILETIME f;
    if (!SystemTimeToFileTime(&st, &f)) { g_fail++; }
    FileTimeToSystemTime(&f, &st);   // 填充 wDayOfWeek
    return CalcPeriodAt(st);
}

static void Check(bool cond, const char* name)
{
    if (cond) { g_pass++; printf("PASS: %s\n", name); }
    else      { g_fail++; printf("FAIL: %s\n", name); }
}

int main()
{
    InitHolidays();

    // ---- 日常工作日（2026-10-23 周五 / 10-26 周一）----
    Check(T(2026, 10, 23, 22, 22).peak == false, "周五 22:22 空闲（用户当前场景）");
    Check(T(2026, 10, 23, 10, 0).peak == true,  "周五 10:00 高峰");
    Check(T(2026, 10, 23, 12, 30).peak == false, "周五 12:30 空闲（午休）");
    Check(T(2026, 10, 23, 14, 0).peak == true,  "周五 14:00 高峰");
    Check(T(2026, 10, 23, 8, 59).peak == false, "周五 08:59 空闲（早高峰前）");
    Check(T(2026, 10, 23, 9, 0).peak == true,   "周五 09:00 高峰（含边界）");
    Check(T(2026, 10, 23, 12, 0).peak == false, "周五 12:00 空闲（不含边界）");
    Check(T(2026, 10, 23, 18, 0).peak == false, "周五 18:00 空闲（不含边界）");
    Check(T(2026, 10, 23, 17, 59).peak == true, "周五 17:59 高峰");

    // ---- 周末 ----
    Check(T(2026, 10, 24, 10, 0).peak == false, "周六 10:00 空闲");
    Check(T(2026, 10, 25, 15, 0).peak == false, "周日 15:00 空闲");

    // ---- 2026 法定节假日（全部）----
    Check(T(2026, 1, 1, 10, 0).peak == false,  "元旦 1/1 空闲");
    Check(T(2026, 1, 3, 15, 0).peak == false,  "元旦 1/3 空闲");
    Check(T(2026, 2, 17, 10, 0).peak == false, "春节 2/17 空闲");
    Check(T(2026, 2, 23, 16, 0).peak == false, "春节 2/23（最后一天）空闲");
    Check(T(2026, 4, 4, 10, 0).peak == false,  "清明 4/4 空闲");
    Check(T(2026, 4, 6, 10, 0).peak == false,  "清明 4/6 空闲");
    Check(T(2026, 5, 1, 10, 0).peak == false,  "劳动节 5/1 空闲");
    Check(T(2026, 5, 5, 10, 0).peak == false,  "劳动节 5/5 空闲");
    Check(T(2026, 6, 19, 10, 0).peak == false, "端午 6/19 空闲");
    Check(T(2026, 6, 21, 10, 0).peak == false, "端午 6/21 空闲");
    Check(T(2026, 9, 25, 10, 0).peak == false, "中秋 9/25 空闲");
    Check(T(2026, 9, 27, 10, 0).peak == false, "中秋 9/27 空闲");
    Check(T(2026, 10, 1, 10, 0).peak == false, "国庆 10/1 空闲");
    Check(T(2026, 10, 7, 15, 0).peak == false, "国庆 10/7 空闲");
    Check(T(2026, 1, 5, 10, 0).peak == true,   "元旦后 1/5（周一）恢复高峰");

    // ---- 调休补班日（周末上班 → 高峰生效）----
    Check(T(2026, 1, 4, 10, 0).peak == true,   "1/4 周日补班 → 高峰");
    Check(T(2026, 2, 14, 10, 0).peak == true,  "2/14 周六补班 → 高峰");
    Check(T(2026, 2, 28, 10, 0).peak == true,  "2/28 周六补班 → 高峰");
    Check(T(2026, 5, 9, 10, 0).peak == true,   "5/9 周六补班 → 高峰");
    Check(T(2026, 9, 20, 10, 0).peak == true,  "9/20 周日补班 → 高峰");
    Check(T(2026, 10, 10, 10, 0).peak == true, "10/10 周六补班 → 高峰");
    Check(T(2026, 10, 11, 10, 0).peak == false, "10/11 周日正常休息 → 空闲");

    // ---- 切换点倒计时 ----
    {
        SPeriod p = T(2026, 10, 23, 8, 59, 0);
        Check(p.remain_sec >= 59 && p.remain_sec <= 60 && p.switch_hour == 9 && p.switch_minute == 0,
              "周五 08:59 → 60 秒后 9:00 切换");
    }
    {
        SPeriod p = T(2026, 10, 23, 11, 59, 0);
        Check(p.remain_sec >= 59 && p.remain_sec <= 60 && p.switch_hour == 12 && p.switch_minute == 0,
              "周五 11:59 → 60 秒后 12:00 切换");
    }
    {
        SPeriod p = T(2026, 10, 23, 17, 59, 0);
        Check(p.remain_sec >= 59 && p.remain_sec <= 60 && p.switch_hour == 18 && p.switch_minute == 0,
              "周五 17:59 → 60 秒后 18:00 切换");
    }
    {
        SPeriod p = T(2026, 10, 23, 18, 0, 0);   // 周五下班 → 下周一 9:00
        Check(!p.peak && p.switch_ymd == 20261026 && p.switch_hour == 9 && p.switch_minute == 0,
              "周五 18:00 → 下周一 9:00 切换（含日期）");
        Check(p.remain_sec == 226800, "周五 18:00 → 周一 9:00 剩余 226800 秒（63 小时）");
    }
    {
        SPeriod p = T(2026, 10, 7, 15, 0, 0);    // 国庆最后一天 → 10/8 9:00
        Check(!p.peak && p.switch_ymd == 20261008 && p.switch_hour == 9,
              "国庆 10/7 15:00 → 10/8 9:00 切换");
    }
    {
        SPeriod p = T(2026, 2, 23, 16, 0, 0);    // 春节最后一天 → 2/24 9:00
        Check(!p.peak && p.switch_ymd == 20260224 && p.switch_hour == 9,
              "春节 2/23 16:00 → 2/24 9:00 切换");
    }

    // ---- ini 扩展（模拟 2027 年安排发布后用户在 ini 追加）----
    AppendIniDates(g_holidays, L"2027-01-01");           // 周五，设为放假
    AppendIniDates(g_workdays, L"2027-01-02");           // 周六，设为补班
    Check(T(2027, 1, 1, 10, 0).peak == false, "ini 扩展：2027-01-01 放假 → 空闲");
    Check(T(2027, 1, 2, 10, 0).peak == true,  "ini 扩展：2027-01-02 补班 → 高峰");
    Check(T(2027, 1, 4, 10, 0).peak == true,  "ini 扩展：2027-01-04（周一）正常高峰");

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
