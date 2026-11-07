//==============================================================================
// DeepSeekMonitor —— TrafficMonitor 任务栏插件
// 功能：显示 DeepSeek API 账户余额、高峰/空闲（错峰优惠）时段及切换倒计时
// 接口：TrafficMonitor 插件接口 v7（适配 TrafficMonitor 1.86，见 PluginInterface.h）
// 构建：见 build.ps1（Zig c++ / MinGW-w64 后端，无需 Visual Studio）
//
// Copyright (C) 2026 nimingik
// SPDX-License-Identifier: Anti-996
// 许可证全文见仓库根目录 LICENSE；版权与第三方声明见 THIRD_PARTY_NOTICES.md
// 项目主页：https://github.com/nimingik/TrafficMonitor-DeepSeek
//==============================================================================
#include "PluginInterface.h"

#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cctype>

//------------------------------------------------------------------------------
// 常量
//------------------------------------------------------------------------------
// DeepSeek 官方错峰优惠规则（北京时间）：
//   高峰时段：周一至周五（不含中国法定节假日）9:00-12:00、14:00-18:00
//   空闲时段：其余全部时段（工作日早晚与午休、周末全天、法定节假日全天），
//             空闲时段价格约为高峰时段价格的一半
static const int BEIJING_UTC_OFFSET_MIN = 8 * 60;               // UTC+8
static const wchar_t* DEEPSEEK_HOST     = L"api.deepseek.com";
static const wchar_t* DEEPSEEK_PATH     = L"/user/balance";
static const wchar_t* INI_FILE_NAME     = L"deepseek_monitor.ini";

// 对话框控件 ID
#define IDC_EDIT_KEY      1001
#define IDC_EDIT_INTERVAL 1002
#define IDC_BTN_TEST      1003
#define IDC_CHK_SHOW      1004

//------------------------------------------------------------------------------
// WinHTTP 动态加载（不静态依赖 winhttp.dll，插件更精简、加载更快）
//------------------------------------------------------------------------------
typedef void* HINTERNET;
typedef HINTERNET(WINAPI* PFN_WinHttpOpen)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef BOOL(WINAPI* PFN_WinHttpSetTimeouts)(HINTERNET, int, int, int, int);
typedef HINTERNET(WINAPI* PFN_WinHttpConnect)(HINTERNET, LPCWSTR, WORD, DWORD);
typedef HINTERNET(WINAPI* PFN_WinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
typedef BOOL(WINAPI* PFN_WinHttpAddRequestHeaders)(HINTERNET, LPCWSTR, DWORD, DWORD);
typedef BOOL(WINAPI* PFN_WinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI* PFN_WinHttpReceiveResponse)(HINTERNET, LPVOID);
typedef BOOL(WINAPI* PFN_WinHttpQueryDataAvailable)(HINTERNET, DWORD*);
typedef BOOL(WINAPI* PFN_WinHttpReadData)(HINTERNET, LPVOID, DWORD, DWORD*);
typedef BOOL(WINAPI* PFN_WinHttpQueryHeaders)(HINTERNET, DWORD, LPCWSTR, LPVOID, DWORD*, LPVOID);
typedef BOOL(WINAPI* PFN_WinHttpCloseHandle)(HINTERNET);

static PFN_WinHttpOpen              pWinHttpOpen = nullptr;
static PFN_WinHttpSetTimeouts       pWinHttpSetTimeouts = nullptr;
static PFN_WinHttpConnect           pWinHttpConnect = nullptr;
static PFN_WinHttpOpenRequest       pWinHttpOpenRequest = nullptr;
static PFN_WinHttpAddRequestHeaders pWinHttpAddRequestHeaders = nullptr;
static PFN_WinHttpSendRequest       pWinHttpSendRequest = nullptr;
static PFN_WinHttpReceiveResponse   pWinHttpReceiveResponse = nullptr;
static PFN_WinHttpQueryDataAvailable pWinHttpQueryDataAvailable = nullptr;
static PFN_WinHttpReadData          pWinHttpReadData = nullptr;
static PFN_WinHttpQueryHeaders      pWinHttpQueryHeaders = nullptr;
static PFN_WinHttpCloseHandle       pWinHttpCloseHandle = nullptr;

#define WINHTTP_QUERY_STATUS_CODE     19
#define WINHTTP_QUERY_FLAG_NUMBER     0x20000000

#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 0
#define WINHTTP_FLAG_SECURE                0x00800000
#define WINHTTP_ADDREQ_FLAG_ADD            0x20000000
#define INTERNET_DEFAULT_HTTPS_PORT        443

static bool InitWinHttp()
{
    if (pWinHttpOpen)
        return true;
    HMODULE h = LoadLibraryW(L"winhttp.dll");
    if (!h)
        return false;
    pWinHttpOpen              = (PFN_WinHttpOpen)GetProcAddress(h, "WinHttpOpen");
    pWinHttpSetTimeouts       = (PFN_WinHttpSetTimeouts)GetProcAddress(h, "WinHttpSetTimeouts");
    pWinHttpConnect           = (PFN_WinHttpConnect)GetProcAddress(h, "WinHttpConnect");
    pWinHttpOpenRequest       = (PFN_WinHttpOpenRequest)GetProcAddress(h, "WinHttpOpenRequest");
    pWinHttpAddRequestHeaders = (PFN_WinHttpAddRequestHeaders)GetProcAddress(h, "WinHttpAddRequestHeaders");
    pWinHttpSendRequest       = (PFN_WinHttpSendRequest)GetProcAddress(h, "WinHttpSendRequest");
    pWinHttpReceiveResponse   = (PFN_WinHttpReceiveResponse)GetProcAddress(h, "WinHttpReceiveResponse");
    pWinHttpQueryDataAvailable = (PFN_WinHttpQueryDataAvailable)GetProcAddress(h, "WinHttpQueryDataAvailable");
    pWinHttpReadData          = (PFN_WinHttpReadData)GetProcAddress(h, "WinHttpReadData");
    pWinHttpQueryHeaders      = (PFN_WinHttpQueryHeaders)GetProcAddress(h, "WinHttpQueryHeaders");
    pWinHttpCloseHandle       = (PFN_WinHttpCloseHandle)GetProcAddress(h, "WinHttpCloseHandle");
    if (!pWinHttpOpen || !pWinHttpConnect || !pWinHttpOpenRequest || !pWinHttpSendRequest ||
        !pWinHttpReceiveResponse || !pWinHttpReadData || !pWinHttpCloseHandle || !pWinHttpQueryDataAvailable ||
        !pWinHttpAddRequestHeaders || !pWinHttpQueryHeaders)
        return false;
    return true;
}

//------------------------------------------------------------------------------
// 全局状态
//------------------------------------------------------------------------------
static HMODULE          g_hInst = nullptr;
static std::wstring     g_config_dir;   // TrafficMonitor 通过 EI_CONFIG_DIR 传入
static std::wstring     g_ini_path;     // 配置文件完整路径
static CRITICAL_SECTION g_cs;

struct SConfig
{
    std::wstring api_key;
    int          refresh_minutes = 5;
};
static SConfig g_cfg;

struct SData
{
    std::wstring balance_text = L"--";   // 形如 "¥110.00"
    std::wstring currency_symbol = L"¥";
    std::wstring granted_text;           // 赠金
    std::wstring topped_text;            // 充值
    bool         is_available = false;
    bool         has_fetched  = false;
    bool         has_balance  = false;   // 是否成功获取过余额
    bool         fetching     = false;
    std::wstring last_error;
    DWORD        last_fetch_tick = 0;
};
static SData g_data;

//------------------------------------------------------------------------------
// 小工具
//------------------------------------------------------------------------------
static std::wstring A2W(const std::string& s)
{
    std::wstring w;
    w.reserve(s.size());
    for (char c : s)
        w.push_back((wchar_t)(unsigned char)c);
    return w;
}

static std::wstring Num2(int n)
{
    return std::to_wstring(n);
}

static std::wstring Pad2(int n)
{
    return (n < 10 ? L"0" : L"") + std::to_wstring(n);
}

// 倒计时秒数 -> "H:MM:SS"
static std::wstring FormatRemain(int sec)
{
    if (sec < 0) sec = 0;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    return std::to_wstring(h) + L":" + Pad2(m) + L":" + Pad2(s);
}

// "X小时XX分XX秒"
static std::wstring FormatRemainCN(int sec)
{
    if (sec < 0) sec = 0;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    std::wstring r;
    if (h > 0) r += std::to_wstring(h) + L"小时";
    r += std::to_wstring(m) + L"分";
    r += std::to_wstring(s) + L"秒";
    return r;
}

//------------------------------------------------------------------------------
// 配置读写（ini 存放在 TrafficMonitor 插件配置目录）
//------------------------------------------------------------------------------
static std::wstring GetDllDir()
{
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    wchar_t* p = wcsrchr(path, L'\\');
    if (p) *p = 0;
    return std::wstring(path);
}

static std::wstring GetIniPath()
{
    if (!g_ini_path.empty())
        return g_ini_path;
    std::wstring dir = g_config_dir.empty() ? GetDllDir() : g_config_dir;
    if (!dir.empty() && dir.back() != L'\\')
        dir += L'\\';
    g_ini_path = dir + INI_FILE_NAME;
    return g_ini_path;
}

// 法定节假日数据（2026 内置表 + ini 扩展；函数定义见"时段计算"一节）
static std::vector<int> g_holidays;  // 放假日期（ymd = 年*10000+月*100+日）
static std::vector<int> g_workdays;  // 调休补班日（ymd）
static void InitHolidays();
static void AppendIniDates(std::vector<int>& v, const std::wstring& csv);

static void LoadConfig()
{
    InitHolidays();
    std::wstring ini = GetIniPath();
    wchar_t buf[1024] = { 0 };
    GetPrivateProfileStringW(L"config", L"api_key", L"", buf, 1024, ini.c_str());
    g_cfg.api_key = buf;
    g_cfg.refresh_minutes = (int)GetPrivateProfileIntW(L"config", L"refresh_minutes", 5, ini.c_str());
    if (g_cfg.refresh_minutes < 1)   g_cfg.refresh_minutes = 1;
    if (g_cfg.refresh_minutes > 1440) g_cfg.refresh_minutes = 1440;

    // ini 自定义节假日 / 补班日（追加到内置表，便于将来年份更新，格式 YYYY-MM-DD,YYYY-MM-DD）
    wchar_t hbuf[4096] = { 0 };
    GetPrivateProfileStringW(L"config", L"holidays", L"", hbuf, 4096, ini.c_str());
    AppendIniDates(g_holidays, hbuf);
    wchar_t wbuf[4096] = { 0 };
    GetPrivateProfileStringW(L"config", L"workdays", L"", wbuf, 4096, ini.c_str());
    AppendIniDates(g_workdays, wbuf);
}

static void SaveConfig()
{
    std::wstring ini = GetIniPath();
    WritePrivateProfileStringW(L"config", L"api_key", g_cfg.api_key.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"config", L"refresh_minutes", std::to_wstring(g_cfg.refresh_minutes).c_str(), ini.c_str());
}

//------------------------------------------------------------------------------
// DeepSeek 余额 API：GET https://api.deepseek.com/user/balance
//   请求头：Accept: application/json
//           Authorization: Bearer <API_KEY>
//   响应：{"is_available":true,"balance_infos":[{"currency":"CNY",
//          "total_balance":"110.00","granted_balance":"10.00","topped_up_balance":"100.00"}]}
//------------------------------------------------------------------------------
static bool HttpGetBalance(const std::wstring& api_key, std::string& response, DWORD& status_code)
{
    status_code = 0;
    if (api_key.empty())
        return false;
    if (!InitWinHttp())
        return false;

    HINTERNET hSession = pWinHttpOpen(L"DeepSeekMonitor/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      nullptr, nullptr, 0);
    if (!hSession)
        return false;
    pWinHttpSetTimeouts(hSession, 5000, 5000, 8000, 8000);

    bool ok = false;
    HINTERNET hConnect = pWinHttpConnect(hSession, DEEPSEEK_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect)
    {
        HINTERNET hRequest = pWinHttpOpenRequest(hConnect, L"GET", DEEPSEEK_PATH, nullptr,
                                                 nullptr, nullptr, WINHTTP_FLAG_SECURE);
        if (hRequest)
        {
            std::wstring headers = L"Accept: application/json\r\nAuthorization: Bearer " + api_key + L"\r\n";
            pWinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
            if (pWinHttpSendRequest(hRequest, nullptr, 0, nullptr, 0, 0, 0) &&
                pWinHttpReceiveResponse(hRequest, nullptr))
            {
                DWORD sc = 0, sc_size = sizeof(sc);
                if (pWinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                         nullptr, &sc, &sc_size, nullptr))
                    status_code = sc;
                ok = true;
                DWORD size = 0;
                while (pWinHttpQueryDataAvailable(hRequest, &size) && size > 0)
                {
                    std::string chunk(size, '\0');
                    DWORD read = 0;
                    if (!pWinHttpReadData(hRequest, &chunk[0], size, &read) || read == 0)
                        break;
                    chunk.resize(read);
                    response += chunk;
                }
            }
            pWinHttpCloseHandle(hRequest);
        }
        pWinHttpCloseHandle(hConnect);
    }
    pWinHttpCloseHandle(hSession);
    return ok;
}

// 从 json 中 key_pos 处（key 引号位置）之后提取字符串值
static bool JsonStringAfter(const std::string& json, size_t key_pos, std::string& value)
{
    size_t p = json.find(':', key_pos);
    if (p == std::string::npos)
        return false;
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n'))
        p++;
    if (p >= json.size() || json[p] != '"')
        return false;
    p++;
    std::string out;
    while (p < json.size() && json[p] != '"')
    {
        if (json[p] == '\\' && p + 1 < json.size())
            p++;
        out += json[p];
        p++;
    }
    value = out;
    return true;
}

// 查找 "key" 并取其字符串值
static bool JsonGetString(const std::string& json, const char* key, std::string& value)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos)
        return false;
    return JsonStringAfter(json, p, value);
}

// 解析余额响应；优先取 CNY，其次 USD，最后取第一个 total_balance
static bool ParseBalanceJson(const std::string& json, bool& is_available,
                             std::string& total, std::string& granted, std::string& topped,
                             std::wstring& symbol)
{
    is_available = false;
    size_t pa = json.find("\"is_available\"");
    if (pa != std::string::npos)
    {
        size_t pc = json.find(':', pa);
        if (pc != std::string::npos)
        {
            pc++;
            while (pc < json.size() && (json[pc] == ' ' || json[pc] == '\t' || json[pc] == '\r' || json[pc] == '\n'))
                pc++;
            is_available = (json.compare(pc, 4, "true") == 0);
        }
    }

    const char* curs[] = { "CNY", "USD" };
    for (const char* cur : curs)
    {
        std::string pat = std::string("\"") + cur + "\"";
        size_t q = json.find(pat);
        if (q == std::string::npos)
            continue;
        size_t t = json.find("\"total_balance\"", q);
        if (t == std::string::npos)
            continue;
        if (JsonStringAfter(json, t, total))
        {
            symbol = (cur[0] == 'C') ? L"¥" : L"$";
            size_t gg = json.find("\"granted_balance\"", q);
            if (gg != std::string::npos) JsonStringAfter(json, gg, granted);
            size_t tt = json.find("\"topped_up_balance\"", q);
            if (tt != std::string::npos) JsonStringAfter(json, tt, topped);
            return true;
        }
    }
    // 兜底：取第一个 total_balance
    size_t t = json.find("\"total_balance\"");
    if (t != std::string::npos && JsonStringAfter(json, t, total))
    {
        symbol = L"¥";
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
// 高峰 / 空闲时段计算（北京时间）
//------------------------------------------------------------------------------
struct SPeriod
{
    bool peak;          // true=高峰期（标准价）；false=空闲时段（优惠价）
    int  remain_sec;    // 距离下次切换的秒数
    int  hour, minute, second;  // 当前北京时间
    int  switch_hour, switch_minute; // 下次切换时刻
    int  switch_ymd;    // 下次切换日期（ymd，0=今天）
};

// 2026 年中国法定节假日放假区间（来源：国办发明电〔2025〕7 号）
// 每项为 {起始月*100+日, 结束月*100+日}，年份固定 2026
static const int CN_HOLIDAY_RANGES_2026[][2] = {
    { 101, 103 },   // 元旦   1/1-1/3
    { 215, 223 },   // 春节   2/15-2/23（9 天）
    { 404, 406 },   // 清明   4/4-4/6
    { 501, 505 },   // 劳动节 5/1-5/5
    { 619, 621 },   // 端午   6/19-6/21
    { 925, 927 },   // 中秋   9/25-9/27
    { 1001, 1007 }, // 国庆   10/1-10/7
};
// 2026 年调休补班日（周末上班，月*100+日）
static const int CN_MAKEUP_WORKDAYS_2026[] = {
    104,    // 1/4（周日）
    214,    // 2/14（周六）
    228,    // 2/28（周六）
    509,    // 5/9（周六）
    920,    // 9/20（周日）
    1010,   // 10/10（周六）
};

// 节假日数据：内置 2026 表之外可在 ini 中追加：
//   holidays=2027-01-01,2027-01-02   （放假日期）
//   workdays=2027-01-03              （调休补班日）
// （g_holidays / g_workdays 定义见 LoadConfig 之前）

static bool ContainsInt(const std::vector<int>& v, int x)
{
    for (int i : v) if (i == x) return true;
    return false;
}

// 把 [a, b] 日期区间逐日展开成 ymd 列表（按真实日历迭代，支持跨月）
static void ExpandDateRange(int y1, int m1, int d1, int y2, int m2, int d2,
                            std::vector<int>& out)
{
    SYSTEMTIME a = { 0 }, b = { 0 };
    a.wYear = (WORD)y1; a.wMonth = (WORD)m1; a.wDay = (WORD)d1;
    b.wYear = (WORD)y2; b.wMonth = (WORD)m2; b.wDay = (WORD)d2;
    FILETIME fa, fb;
    if (!SystemTimeToFileTime(&a, &fa) || !SystemTimeToFileTime(&b, &fb))
        return;
    ULARGE_INTEGER ua, ub;
    ua.LowPart = fa.dwLowDateTime; ua.HighPart = fa.dwHighDateTime;
    ub.LowPart = fb.dwLowDateTime; ub.HighPart = fb.dwHighDateTime;
    for (ULARGE_INTEGER u = ua; u.QuadPart <= ub.QuadPart; u.QuadPart += 864000000000LL)
    {
        FILETIME f; f.dwLowDateTime = u.LowPart; f.dwHighDateTime = u.HighPart;
        SYSTEMTIME s;
        if (FileTimeToSystemTime(&f, &s))
            out.push_back(s.wYear * 10000 + s.wMonth * 100 + s.wDay);
    }
}

static void InitHolidays()
{
    g_holidays.clear();
    g_workdays.clear();
    for (auto& r : CN_HOLIDAY_RANGES_2026)
        ExpandDateRange(2026, r[0] / 100, r[0] % 100, 2026, r[1] / 100, r[1] % 100,
                        g_holidays);
    for (int md : CN_MAKEUP_WORKDAYS_2026)
        g_workdays.push_back(2026 * 10000 + md);
}

// 解析 ini 中的日期列表（YYYY-MM-DD,YYYY-MM-DD,...）并追加
static void AppendIniDates(std::vector<int>& v, const std::wstring& csv)
{
    size_t i = 0;
    while (i < csv.size())
    {
        size_t j = csv.find(L',', i);
        std::wstring tok = csv.substr(i, j == std::wstring::npos ? j : j - i);
        i = (j == std::wstring::npos) ? csv.size() : j + 1;
        // 去空白
        while (!tok.empty() && iswspace((wint_t)tok.front())) tok.erase(tok.begin());
        while (!tok.empty() && iswspace((wint_t)tok.back()))  tok.pop_back();
        if (tok.size() >= 8)
        {
            int y = _wtoi(tok.substr(0, 4).c_str());
            int m = _wtoi(tok.substr(5, 2).c_str());
            int d = _wtoi(tok.substr(8, 2).c_str());
            if (y >= 2000 && m >= 1 && m <= 12 && d >= 1 && d <= 31)
                v.push_back(y * 10000 + m * 100 + d);
        }
    }
}

static bool IsWorkingDay(int ymd, int dow)  // dow: 0=周日 … 6=周六
{
    if (ContainsInt(g_workdays, ymd)) return true;   // 调休补班日
    if (ContainsInt(g_holidays, ymd)) return false;  // 法定节假日
    return dow != 0 && dow != 6;
}

// sod: 当天秒数。高峰 = 工作日 9:00-12:00、14:00-18:00
static bool IsPeakTime(int ymd, int dow, int sod)
{
    if (!IsWorkingDay(ymd, dow)) return false;
    int mins = sod / 60;
    return (mins >= 540 && mins < 720) || (mins >= 840 && mins < 1080);
}

static SPeriod CalcPeriodAt(const SYSTEMTIME& bj)
{
    int ymd = bj.wYear * 10000 + bj.wMonth * 100 + bj.wDay;
    int sod = bj.wHour * 3600 + bj.wMinute * 60 + bj.wSecond;

    SPeriod r = { false, 86400, bj.wHour, bj.wMinute, bj.wSecond, 0, 0, 0 };
    r.peak = IsPeakTime(ymd, bj.wDayOfWeek, sod);

    // 当天 0 点（用 1601 起 FILETIME  epoch 换算，避免依赖时区）
    SYSTEMTIME mid = bj;
    mid.wHour = 0; mid.wMinute = 0; mid.wSecond = 0; mid.wMilliseconds = 0;
    FILETIME fm;
    SystemTimeToFileTime(&mid, &fm);
    ULARGE_INTEGER midnight;
    midnight.LowPart = fm.dwLowDateTime;
    midnight.HighPart = fm.dwHighDateTime;
    ULARGE_INTEGER nowFt;
    {
        FILETIME fn;
        SystemTimeToFileTime(&bj, &fn);
        nowFt.LowPart = fn.dwLowDateTime;
        nowFt.HighPart = fn.dwHighDateTime;
    }

    // 找下一次状态切换：逐日扫描未来 12 天，候选点为工作日的 9:00 / 12:00 / 14:00 / 18:00
    const int cands[4] = { 9 * 3600, 12 * 3600, 14 * 3600, 18 * 3600 };
    for (int day = 0; day < 12; day++)
    {
        ULARGE_INTEGER dm;
        dm.QuadPart = midnight.QuadPart + (LONGLONG)day * 864000000000LL;
        FILETIME df; df.dwLowDateTime = dm.LowPart; df.dwHighDateTime = dm.HighPart;
        SYSTEMTIME ds;
        if (!FileTimeToSystemTime(&df, &ds)) continue;
        int dymd = ds.wYear * 10000 + ds.wMonth * 100 + ds.wDay;
        if (!IsWorkingDay(dymd, ds.wDayOfWeek)) continue;
        for (int c : cands)
        {
            ULARGE_INTEGER t;
            t.QuadPart = dm.QuadPart + (LONGLONG)c * 10000000LL;
            if (t.QuadPart <= nowFt.QuadPart) continue;   // 已过的切换点
            if (IsPeakTime(dymd, ds.wDayOfWeek, c) == r.peak) continue;
            r.remain_sec = (int)((t.QuadPart - nowFt.QuadPart) / 10000000LL);
            r.switch_hour = c / 3600;
            r.switch_minute = (c % 3600) / 60;
            r.switch_ymd = (dymd == ymd) ? 0 : dymd;      // 0 表示今天
            return r;
        }
    }
    return r;  // 兜底：12 天内必有切换点（最长假期 9 天 < 12 天）
}

static SPeriod CalcPeriod()
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    // 本地时间 -> 北京时间
    TIME_ZONE_INFORMATION tzi = { 0 };
    DWORD tzr = GetTimeZoneInformation(&tzi);
    LONG bias = tzi.Bias;
    if (tzr == TIME_ZONE_ID_DAYLIGHT)
        bias += tzi.DaylightBias;

    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // UTC = local + bias（Bias 对东八区为 -480 分钟）
    // 北京时间 = UTC + 480 分钟 => 本地时间 + (480 + bias) 分钟
    u.QuadPart += (LONGLONG)(BEIJING_UTC_OFFSET_MIN + bias) * 60 * 10000000LL;
    FILETIME ft2;
    ft2.dwLowDateTime = u.LowPart;
    ft2.dwHighDateTime = u.HighPart;
    SYSTEMTIME bj;
    FileTimeToSystemTime(&ft2, &bj);
    return CalcPeriodAt(bj);
}

//------------------------------------------------------------------------------
// 后台刷新线程
//------------------------------------------------------------------------------
struct SFetchParam
{
    std::wstring key;
};

static DWORD WINAPI FetchThreadProc(LPVOID param)
{
    SFetchParam* p = (SFetchParam*)param;
    std::wstring key = p->key;
    delete p;

    std::string resp;
    DWORD status = 0;
    bool net_ok = HttpGetBalance(key, resp, status);

    bool is_avail = false;
    std::string total, granted, topped;
    std::wstring symbol = L"¥";
    bool parse_ok = ParseBalanceJson(resp, is_avail, total, granted, topped, symbol);

    EnterCriticalSection(&g_cs);
    if (net_ok && status >= 200 && status < 300 && parse_ok)
    {
        g_data.balance_text = symbol + A2W(total);
        g_data.currency_symbol = symbol;
        g_data.granted_text = symbol + A2W(granted);
        g_data.topped_text = symbol + A2W(topped);
        g_data.is_available = is_avail;
        g_data.has_balance = true;
        g_data.last_error.clear();
    }
    else if (net_ok && status >= 400)
    {
        g_data.last_error = L"HTTP " + std::to_wstring(status) +
            (status == 401 ? L"：API Key 无效" :
             status == 402 ? L"：账户余额不足" :
             status == 429 ? L"：请求过于频繁" : L"：请求失败");
    }
    else if (net_ok && !parse_ok)
    {
        g_data.last_error = L"返回数据解析失败";
    }
    else
    {
        g_data.last_error = L"网络请求失败（请检查网络或 API Key）";
    }
    g_data.has_fetched = true;
    g_data.fetching = false;
    g_data.last_fetch_tick = GetTickCount();
    LeaveCriticalSection(&g_cs);
    return 0;
}

static void StartFetch(const std::wstring& key)
{
    if (key.empty())
        return;
    EnterCriticalSection(&g_cs);
    if (g_data.fetching)
    {
        LeaveCriticalSection(&g_cs);
        return;
    }
    g_data.fetching = true;
    LeaveCriticalSection(&g_cs);

    SFetchParam* p = new SFetchParam();
    p->key = key;
    HANDLE h = CreateThread(nullptr, 0, FetchThreadProc, p, 0, nullptr);
    if (h)
        CloseHandle(h);
    else
    {
        delete p;
        EnterCriticalSection(&g_cs);
        g_data.fetching = false;
        LeaveCriticalSection(&g_cs);
    }
}

//------------------------------------------------------------------------------
// 显示项：DeepSeek 余额
//------------------------------------------------------------------------------
static INT_PTR OpenOptionsDialog(void* hParent);   // 稍后定义

class CBalanceItem : public IPluginItem
{
public:
    virtual const wchar_t* GetItemName() const { return L"DeepSeek余额"; }
    virtual const wchar_t* GetItemId() const { return L"DeepSeekBalance"; }
    virtual const wchar_t* GetItemLableText() const { return L"DeepSeek余额:"; }
    virtual const wchar_t* GetItemValueSampleText() const { return L"¥1234.56"; }

    virtual const wchar_t* GetItemValueText() const
    {
        CBalanceItem* self = const_cast<CBalanceItem*>(this);
        EnterCriticalSection(&g_cs);
        if (g_cfg.api_key.empty())
            self->m_value = L"未配置";
        else if (!g_data.has_balance)
            self->m_value = g_data.fetching ? L"获取中"
                            : (g_data.last_error.empty() ? L"--" : L"获取失败");
        else
            self->m_value = g_data.balance_text;   // 刷新失败时保留上次已知余额
        LeaveCriticalSection(&g_cs);
        return self->m_value.c_str();
    }

    virtual int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
    {
        if (type == MT_LCLICKED)
        {
            EnterCriticalSection(&g_cs);
            std::wstring key = g_cfg.api_key;
            LeaveCriticalSection(&g_cs);
            StartFetch(key);
            return 1;
        }
        if (type == MT_RCLICKED)
        {
            OpenOptionsDialog(hWnd);
            return 1;
        }
        return 0;
    }

private:
    std::wstring m_value;
};

//------------------------------------------------------------------------------
// 显示项：高峰 / 空闲时段
//------------------------------------------------------------------------------
class CPeriodItem : public IPluginItem
{
public:
    virtual const wchar_t* GetItemName() const { return L"DeepSeek时段"; }
    virtual const wchar_t* GetItemId() const { return L"DeepSeekPeriod"; }
    virtual const wchar_t* GetItemLableText() const { return L"DeepSeek时段:"; }
    virtual const wchar_t* GetItemValueSampleText() const { return L"空闲·剩12:34:56"; }

    virtual const wchar_t* GetItemValueText() const
    {
        CPeriodItem* self = const_cast<CPeriodItem*>(this);
        SPeriod p = CalcPeriod();
        self->m_value = p.peak ? (L"高峰·剩" + FormatRemain(p.remain_sec))
                               : (L"空闲·剩" + FormatRemain(p.remain_sec));
        return self->m_value.c_str();
    }

    virtual int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
    {
        if (type == MT_LCLICKED)
        {
            EnterCriticalSection(&g_cs);
            std::wstring key = g_cfg.api_key;
            LeaveCriticalSection(&g_cs);
            StartFetch(key);
            return 1;
        }
        if (type == MT_RCLICKED)
        {
            OpenOptionsDialog(hWnd);
            return 1;
        }
        return 0;
    }

private:
    std::wstring m_value;
};

//------------------------------------------------------------------------------
// 插件主体
//------------------------------------------------------------------------------
class CDeepSeekPlugin : public ITMPlugin
{
public:
    CDeepSeekPlugin() {}

    virtual IPluginItem* GetItem(int index) override
    {
        switch (index)
        {
        case 0: return &m_balance;
        case 1: return &m_period;
        }
        return nullptr;
    }

    virtual void DataRequired() override
    {
        // 时段文本在 GetItemValueText 中即时计算；此处只负责按间隔刷新余额
        EnterCriticalSection(&g_cs);
        std::wstring key = g_cfg.api_key;
        bool need = !key.empty() && !g_data.fetching &&
            (g_data.last_fetch_tick == 0 ||
             (GetTickCount() - g_data.last_fetch_tick) >= (DWORD)g_cfg.refresh_minutes * 60000u);
        LeaveCriticalSection(&g_cs);
        if (need)
            StartFetch(key);
    }

    virtual const wchar_t* GetInfo(PluginInfoIndex index) override
    {
        switch (index)
        {
        case TMI_NAME:        return L"DeepSeek 监控";
        case TMI_DESCRIPTION: return L"显示 DeepSeek API 账户余额与高峰/空闲（错峰优惠）时段";
        case TMI_AUTHOR:      return L"WorkBuddy AI";
        case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
        case TMI_VERSION:     return L"1.0";
        case TMI_URL:         return L"https://api-docs.deepseek.com";
        case TMI_MAX:         return L"";
        }
        return L"";
    }

    virtual OptionReturn ShowOptionsDialog(void* hParent) override
    {
        INT_PTR r = OpenOptionsDialog(hParent);
        return (r != 0) ? OR_OPTION_CHANGED : OR_OPTION_UNCHANGED;
    }

    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override
    {
        if (index == EI_CONFIG_DIR && data && data[0])
        {
            EnterCriticalSection(&g_cs);
            g_config_dir = data;
            g_ini_path.clear();
            LoadConfig();
            LeaveCriticalSection(&g_cs);
        }
    }

    virtual void OnInitialize(ITrafficMonitor* pApp) override
    {
        m_app = pApp;
        EnterCriticalSection(&g_cs);
        LoadConfig();
        LeaveCriticalSection(&g_cs);
    }

    virtual const wchar_t* GetTooltipInfo() override
    {
        EnterCriticalSection(&g_cs);
        m_tooltip = BuildTooltip();
        LeaveCriticalSection(&g_cs);
        return m_tooltip.c_str();
    }

private:
    std::wstring BuildTooltip()
    {
        SPeriod p = CalcPeriod();
        std::wstring t = L"DeepSeek 账户监控\n";
        if (g_cfg.api_key.empty())
        {
            t += L"尚未配置 API Key（右键点击显示项打开设置）\n";
            t += L"空闲时段：工作日 9:00-12:00 / 14:00-18:00 之外的全部时间\n";
            t += L"（含周末与法定节假日，API 价格约为高峰时段一半）";
            return t;
        }
        if (!g_data.has_balance)
        {
            t += g_data.fetching ? L"余额: 获取中…\n" : L"余额: 获取失败\n";
            if (!g_data.last_error.empty())
                t += L"原因: " + g_data.last_error + L"\n";
        }
        else
        {
            t += L"余额: " + g_data.balance_text + (g_data.is_available ? L"（账户可用）" : L"（余额不足）") + L"\n";
            if (!g_data.granted_text.empty())
                t += L"赠金: " + g_data.granted_text + L" / 充值: " + g_data.topped_text + L"\n";
            if (!g_data.last_error.empty())
                t += L"上次刷新失败: " + g_data.last_error + L"\n";
        }
        t += p.peak ? L"当前时段: 高峰期（工作日 9:00-12:00 / 14:00-18:00，标准价）\n"
                    : L"当前时段: 空闲时段（优惠价，约为高峰一半）\n";
        t += L"本时段剩: " + FormatRemainCN(p.remain_sec) + L"\n";
        if (p.switch_ymd != 0)
        {
            // 跨天/长假后的切换点：显示日期，避免只写 9:00 造成误解
            wchar_t dbuf[32];
            swprintf(dbuf, 32, L"%04d-%02d-%02d ",
                      p.switch_ymd / 10000, (p.switch_ymd / 100) % 100, p.switch_ymd % 100);
            t += L"下次切换: " + std::wstring(dbuf) + Pad2(p.switch_hour) + L":" +
                 Pad2(p.switch_minute) + L":00（北京时间）\n";
        }
        else
        {
            t += L"下次切换: " + Pad2(p.switch_hour) + L":" + Pad2(p.switch_minute) +
                 L":00（北京时间，今天）\n";
        }
        t += L"刷新间隔: " + std::to_wstring(g_cfg.refresh_minutes) + L" 分钟（左键立即刷新 / 右键设置）";
        if (!g_data.last_error.empty())
            t += L"\n上次刷新失败: " + g_data.last_error;
        return t;
    }

    CBalanceItem     m_balance;
    CPeriodItem      m_period;
    ITrafficMonitor* m_app = nullptr;
    std::wstring     m_tooltip;
};

static CDeepSeekPlugin g_plugin;

//------------------------------------------------------------------------------
// 选项设置对话框（内存 DLGTEMPLATE，无需资源文件）
//------------------------------------------------------------------------------
static void PutWord(std::vector<BYTE>& b, WORD w)
{
    b.push_back((BYTE)(w & 0xFF));
    b.push_back((BYTE)((w >> 8) & 0xFF));
}

static void PutDWord(std::vector<BYTE>& b, DWORD d)
{
    PutWord(b, (WORD)(d & 0xFFFF));
    PutWord(b, (WORD)((d >> 16) & 0xFFFF));
}

static void PutShort(std::vector<BYTE>& b, short s)
{
    PutWord(b, (WORD)s);
}

static void PutWStr(std::vector<BYTE>& b, const wchar_t* s)
{
    for (const wchar_t* p = s; *p; p++)
        PutWord(b, (WORD)*p);
    PutWord(b, 0);
}

static void Align4(std::vector<BYTE>& b)
{
    while (b.size() % 4)
        b.push_back(0);
}

static void AddDlgItem(std::vector<BYTE>& b, DWORD style, short x, short y, short cx, short cy,
                       WORD id, WORD cls, const wchar_t* title)
{
    // 注意：DLGITEMTEMPLATE 没有 menu 字段（只有 DLGTEMPLATE 对话框头才有）
    // 布局：style, exStyle, x, y, cx, cy, id, class(0xFFFF+原子), title, extraCount
    Align4(b);
    PutDWord(b, style);                              // style
    PutDWord(b, 0);                                  // dwExtendedStyle
    PutShort(b, x); PutShort(b, y); PutShort(b, cx); PutShort(b, cy);
    PutWord(b, id);                                  // id
    PutWord(b, 0xFFFF); PutWord(b, cls);             // 预定义窗口类
    PutWStr(b, title);                               // 文本
    PutWord(b, 0);                                   // 附加数据长度
}

static DLGTEMPLATE* BuildOptionsTemplate(std::vector<BYTE>& b)
{
    b.clear();
    DWORD style = WS_POPUP | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_VISIBLE |
                  DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    PutDWord(b, style);
    PutDWord(b, 0);                                  // dwExtendedStyle
    PutWord(b, 8);                                   // cdit
    PutShort(b, 0); PutShort(b, 0); PutShort(b, 240); PutShort(b, 124);
    PutWord(b, 0);                                   // menu（无）
    PutWord(b, 0);                                   // class（默认对话框类）
    PutWStr(b, L"DeepSeek 监控设置");
    PutWord(b, 9);                                   // 字号
    PutWord(b, FW_NORMAL);                           // 字重
    b.push_back(0);                                  // 斜体
    b.push_back(DEFAULT_CHARSET);                    // 字符集
    PutWStr(b, L"MS Shell Dlg");

    AddDlgItem(b, WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 10, 120, 10,
               0xFFFF, 0x0082, L"DeepSeek API Key:");
    // Key 输入框使用 ES_PASSWORD 掩码显示（●），防止明文泄露/截屏暴露
    AddDlgItem(b, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
               10, 22, 160, 12, IDC_EDIT_KEY, 0x0081, L"");
    AddDlgItem(b, BS_AUTOCHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
               176, 23, 56, 10, IDC_CHK_SHOW, 0x0080, L"显示");
    AddDlgItem(b, WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 42, 120, 10,
               0xFFFF, 0x0082, L"刷新间隔（分钟）:");
    AddDlgItem(b, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER,
               10, 54, 50, 12, IDC_EDIT_INTERVAL, 0x0081, L"5");
    AddDlgItem(b, BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
               72, 52, 72, 14, IDC_BTN_TEST, 0x0080, L"测试连接");
    AddDlgItem(b, BS_DEFPUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
               98, 94, 62, 14, IDOK, 0x0080, L"确定");
    AddDlgItem(b, BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
               168, 94, 62, 14, IDCANCEL, 0x0080, L"取消");
    return (DLGTEMPLATE*)b.data();
}

static void TrimInPlace(std::wstring& s)
{
    while (!s.empty() && iswspace((wint_t)s.back()))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && iswspace((wint_t)s[i]))
        i++;
    if (i > 0)
        s = s.substr(i);
}

static INT_PTR CALLBACK OptionsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_EDIT_KEY, g_cfg.api_key.c_str());
        SetDlgItemInt(hDlg, IDC_EDIT_INTERVAL, g_cfg.refresh_minutes, FALSE);
        // 密码掩码字符：●（ES_PASSWORD 默认是 *，圆点更好看）
        SendDlgItemMessageW(hDlg, IDC_EDIT_KEY, EM_SETPASSWORDCHAR, (WPARAM)L'●', 0);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_CHK_SHOW:
            if (HIWORD(wp) == BN_CLICKED)
            {
                // 勾选"显示"则明文展示，取消勾选则恢复 ● 掩码
                BOOL show = (SendDlgItemMessageW(hDlg, IDC_CHK_SHOW, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SendDlgItemMessageW(hDlg, IDC_EDIT_KEY, EM_SETPASSWORDCHAR,
                                    show ? 0 : (WPARAM)L'●', 0);
                InvalidateRect(GetDlgItem(hDlg, IDC_EDIT_KEY), nullptr, TRUE);
            }
            return TRUE;

        case IDC_BTN_TEST:
        {
            wchar_t key[1024] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_KEY, key, 1024);
            std::wstring k = key;
            TrimInPlace(k);
            if (k.empty())
            {
                MessageBoxW(hDlg, L"请先填写 API Key。", L"DeepSeek 测试", MB_ICONWARNING);
                return TRUE;
            }
            HCURSOR old = SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_WAIT));
            std::string resp;
            DWORD status = 0;
            bool net_ok = HttpGetBalance(k, resp, status);
            SetCursor(old);
            bool is_avail = false;
            std::string total, granted, topped;
            std::wstring symbol = L"¥";
            bool parse_ok = ParseBalanceJson(resp, is_avail, total, granted, topped, symbol);
            if (net_ok && status >= 200 && status < 300 && parse_ok)
            {
                std::wstring text = L"连接成功！\n余额: " + symbol + A2W(total) +
                    L"\n账户" + (is_avail ? L"可用" : L"余额不足") +
                    L"\n赠金: " + symbol + A2W(granted) + L" / 充值: " + symbol + A2W(topped);
                MessageBoxW(hDlg, text.c_str(), L"DeepSeek 测试", MB_ICONINFORMATION);
            }
            else if (net_ok && status >= 400)
            {
                std::wstring text = L"连接失败：HTTP " + std::to_wstring(status) + L"\n" +
                    (status == 401 ? L"API Key 无效，请检查是否复制完整。" :
                     status == 402 ? L"账户余额不足。" :
                     status == 429 ? L"请求过于频繁，请稍后再试。" : L"请检查 API Key 或稍后重试。");
                MessageBoxW(hDlg, text.c_str(), L"DeepSeek 测试", MB_ICONERROR);
            }
            else
            {
                MessageBoxW(hDlg,
                    net_ok ? L"服务器已响应，但返回数据无法解析，请确认 API Key 是否正确。"
                           : L"连接失败，请检查网络连接或 API Key。",
                    L"DeepSeek 测试", MB_ICONERROR);
            }
            return TRUE;
        }

        case IDOK:
        {
            wchar_t key[1024] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_KEY, key, 1024);
            std::wstring k = key;
            TrimInPlace(k);
            BOOL translated = FALSE;
            UINT v = GetDlgItemInt(hDlg, IDC_EDIT_INTERVAL, &translated, FALSE);
            EnterCriticalSection(&g_cs);
            g_cfg.api_key = k;
            if (translated && v >= 1 && v <= 1440)
                g_cfg.refresh_minutes = (int)v;
            SaveConfig();
            g_data.last_fetch_tick = 0;   // 立即触发一次刷新
            LeaveCriticalSection(&g_cs);
            EndDialog(hDlg, 1);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR OpenOptionsDialog(void* hParent)
{
    std::vector<BYTE> buf;
    DLGTEMPLATE* tpl = BuildOptionsTemplate(buf);
    return DialogBoxIndirectParamW(g_hInst, tpl, (HWND)hParent, OptionsDlgProc, 0);
}

//------------------------------------------------------------------------------
// DLL 入口与导出
//------------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hModule;
        // 固定模块引用：防止 TrafficMonitor 退出时 FreeLibrary 卸载
        // 仍在执行刷线程的插件代码，避免退出崩溃
        HMODULE hPinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCWSTR)&DllMain, &hPinned);
        InitializeCriticalSection(&g_cs);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &g_plugin;
}
