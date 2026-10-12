/*
 * MdView - lightweight Markdown viewer for Windows
 *
 * Win32 API + RichEdit (Msftedit.dll). No runtime dependencies.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <richedit.h>
#include <ole2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "md2rtf.h"

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif
#ifndef EM_SETTYPOGRAPHYOPTIONS
#define EM_SETTYPOGRAPHYOPTIONS (WM_USER + 202)
#endif
#ifndef TO_ADVANCEDTYPOGRAPHY
#define TO_ADVANCEDTYPOGRAPHY 1
#endif

#define APP_NAME    L"MdView"
#define APP_VERSION L"1.0.0"
#define REG_KEY     L"Software\\MdView"

enum {
    IDM_OPEN = 100, IDM_RELOAD, IDM_EDIT, IDM_EXIT,
    IDM_COPY, IDM_SELALL, IDM_FIND, IDM_FINDNEXT, IDM_FINDPREV,
    IDM_ZOOMIN, IDM_ZOOMOUT, IDM_ZOOMRESET, IDM_DARK, IDM_AUTORELOAD, IDM_RAW,
    IDM_BACK, IDM_ABOUT,
};
enum { TID_RESIZE = 1, TID_WATCH = 2 };
#define WM_APP_OPENFILE (WM_APP + 1)   /* lParam = malloc'ed WCHAR* path */
enum { KEEP_TOP, KEEP_CP, KEEP_Y };

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */
static HINSTANCE g_inst;
static HWND      g_hwnd, g_edit, g_find;
static HMENU     g_menu;
static HACCEL    g_accel;
static UINT      g_findMsg;
static FINDREPLACEW g_fr;
static WCHAR     g_findText[256];

static WCHAR     g_path[MAX_PATH * 2];
static char     *g_src;
static size_t    g_srclen;
static FILETIME  g_ftime;
static DWORD     g_fsize;
static MdDoc     g_doc;
static int       g_lastWidth = -1;

/* settings */
static int   g_scale = 100;
static int   g_dark = 0;
static int   g_auto = 1;
static int   g_raw = 0;
static int   g_fontSize = 11;
static WCHAR g_fontBody[64] = L"Yu Gothic UI";
static WCHAR g_fontCode[64] = L"Consolas";

/* back history */
typedef struct { WCHAR *path; LONG cp; } Hist;
#define HIST_MAX 32
static Hist g_hist[HIST_MAX];
static int  g_nhist;

static const int ZOOMS[] = { 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300 };

static const char WELCOME[] =
    "# MdView\n"
    "\n"
    "\xE8\xBB\xBD\xE9\x87\x8F\xE3\x81\xAA Markdown \xE3\x83\x93\xE3\x83\xA5\xE3\x83\xBC\xE3\x82\xA2\xE3\x81\xA7\xE3\x81\x99\xE3\x80\x82"
    "\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB\xE3\x82\x92\xE3\x83\x89\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xB0\xEF\xBC\x86\xE3\x83\x89\xE3\x83\xAD\xE3\x83\x83\xE3\x83\x97\xE3\x81\x99\xE3\x82\x8B\xE3\x81\x8B\xE3\x80\x81"
    "**Ctrl+O** \xE3\x81\xA7\xE9\x96\x8B\xE3\x81\x84\xE3\x81\xA6\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84\xE3\x80\x82\n"
    "\n"
    "| \xE3\x82\xAD\xE3\x83\xBC | \xE6\xA9\x9F\xE8\x83\xBD |\n"
    "|---|---|\n"
    "| Ctrl+O | \xE9\x96\x8B\xE3\x81\x8F |\n"
    "| F5 | \xE5\x86\x8D\xE8\xAA\xAD\xE3\x81\xBF\xE8\xBE\xBC\xE3\x81\xBF |\n"
    "| Ctrl+E | \xE3\x82\xA8\xE3\x83\x87\xE3\x82\xA3\xE3\x82\xBF\xE3\x81\xA7\xE9\x96\x8B\xE3\x81\x8F |\n"
    "| Ctrl+F / F3 / Shift+F3 | \xE6\xA4\x9C\xE7\xB4\xA2 / \xE6\xAC\xA1\xE3\x82\x92\xE6\xA4\x9C\xE7\xB4\xA2 / \xE5\x89\x8D\xE3\x82\x92\xE6\xA4\x9C\xE7\xB4\xA2 |\n"
    "| Ctrl+\xE3\x83\x9B\xE3\x82\xA4\xE3\x83\xBC\xE3\x83\xAB / Ctrl++ / Ctrl+- / Ctrl+0 | \xE6\x8B\xA1\xE5\xA4\xA7\xE3\x83\xBB\xE7\xB8\xAE\xE5\xB0\x8F |\n"
    "| Ctrl+D | \xE3\x83\x80\xE3\x83\xBC\xE3\x82\xAF\xE3\x83\x86\xE3\x83\xBC\xE3\x83\x9E |\n"
    "| Ctrl+U | \xE3\x82\xBD\xE3\x83\xBC\xE3\x82\xB9\xE8\xA1\xA8\xE7\xA4\xBA |\n"
    "| Alt+\xE2\x86\x90 / BackSpace | \xE6\x88\xBB\xE3\x82\x8B |\n";

/* ------------------------------------------------------------------ */
/* utilities                                                           */
/* ------------------------------------------------------------------ */
static char *wide_to_utf8(const WCHAR *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc(n > 0 ? (size_t)n : 1);
    if (!s) return NULL;
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    else s[0] = 0;
    return s;
}

static WCHAR *utf8_to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = (WCHAR *)malloc(sizeof(WCHAR) * (n > 0 ? (size_t)n : 1));
    if (!w) return NULL;
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    else w[0] = 0;
    return w;
}

static int valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t k;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2) k = 1;
        else if ((c & 0xF0) == 0xE0) k = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) k = 3;
        else return 0;
        if (i + k >= n) return 0;
        for (size_t j = 1; j <= k; j++) if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k + 1;
    }
    return 1;
}

static char *wide_n_to_utf8(const WCHAR *w, int wn, size_t *outlen)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wn, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, wn, s, n, NULL, NULL);
    s[n] = 0;
    *outlen = (size_t)n;
    return s;
}

/* reads a text file and returns UTF-8 (BOM / UTF-16 / ANSI aware) */
static int read_file(const WCHAR *path, char **out, size_t *outlen, FILETIME *ft, DWORD *sz)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li) || li.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return 0; }
    DWORD n = (DWORD)li.QuadPart, got = 0;
    unsigned char *raw = (unsigned char *)malloc(n + 4);
    if (!raw) { CloseHandle(h); return 0; }
    while (got < n) {
        DWORD rd = 0;
        if (!ReadFile(h, raw + got, n - got, &rd, NULL) || rd == 0) break;
        got += rd;
    }
    if (ft) GetFileTime(h, NULL, NULL, ft);
    if (sz) *sz = n;
    CloseHandle(h);
    n = got;
    memset(raw + n, 0, 4);

    char *res = NULL;
    size_t rl = 0;
    if (n >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        res = wide_n_to_utf8((const WCHAR *)(raw + 2), (int)((n - 2) / 2), &rl);
    } else if (n >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        for (DWORD i = 2; i + 1 < n; i += 2) { unsigned char t = raw[i]; raw[i] = raw[i + 1]; raw[i + 1] = t; }
        res = wide_n_to_utf8((const WCHAR *)(raw + 2), (int)((n - 2) / 2), &rl);
    } else if (n >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        rl = n - 3;
        res = (char *)malloc(rl + 1);
        if (res) { memcpy(res, raw + 3, rl); res[rl] = 0; }
    } else if (valid_utf8(raw, n)) {
        res = (char *)raw;
        rl = n;
        raw = NULL;
    } else {
        int wn = MultiByteToWideChar(CP_ACP, 0, (const char *)raw, (int)n, NULL, 0);
        WCHAR *w = (WCHAR *)malloc(sizeof(WCHAR) * ((size_t)wn + 1));
        if (w) {
            MultiByteToWideChar(CP_ACP, 0, (const char *)raw, (int)n, w, wn);
            res = wide_n_to_utf8(w, wn, &rl);
            free(w);
        }
    }
    free(raw);
    if (!res) return 0;
    *out = res;
    *outlen = rl;
    return 1;
}

static int dpi_x(void)
{
    HDC dc = GetDC(NULL);
    int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return d > 0 ? d : 96;
}

static void set_dark_titlebar(HWND hwnd, int dark)
{
    typedef HRESULT (WINAPI *PFN)(HWND, DWORD, LPCVOID, DWORD);
    static PFN fn;
    static int tried;
    if (!tried) {
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        if (m) fn = (PFN)(void *)GetProcAddress(m, "DwmSetWindowAttribute");
        tried = 1;
    }
    if (!fn) return;
    BOOL v = dark ? TRUE : FALSE;
    if (FAILED(fn(hwnd, 20, &v, sizeof v))) fn(hwnd, 19, &v, sizeof v);
}

/* ------------------------------------------------------------------ */
/* settings                                                            */
/* ------------------------------------------------------------------ */
static DWORD reg_dword(HKEY k, const WCHAR *name, DWORD def)
{
    DWORD v, t, sz = sizeof v;
    if (RegQueryValueExW(k, name, NULL, &t, (BYTE *)&v, &sz) == ERROR_SUCCESS && t == REG_DWORD) return v;
    return def;
}
static void reg_str(HKEY k, const WCHAR *name, WCHAR *buf, DWORD cch)
{
    WCHAR tmp[64];
    DWORD t, sz = sizeof tmp - sizeof(WCHAR);
    if (RegQueryValueExW(k, name, NULL, &t, (BYTE *)tmp, &sz) == ERROR_SUCCESS && t == REG_SZ && sz > sizeof(WCHAR)) {
        tmp[sz / sizeof(WCHAR)] = 0;
        lstrcpynW(buf, tmp, (int)cch);
    }
}

static void load_settings(WINDOWPLACEMENT *wp, int *has_wp)
{
    HKEY k;
    *has_wp = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    g_scale = (int)reg_dword(k, L"Zoom", 100);
    if (g_scale < 50 || g_scale > 300) g_scale = 100;
    g_dark = reg_dword(k, L"Dark", 0) ? 1 : 0;
    g_auto = reg_dword(k, L"AutoReload", 1) ? 1 : 0;
    g_fontSize = (int)reg_dword(k, L"FontSize", 11);
    if (g_fontSize < 6 || g_fontSize > 40) g_fontSize = 11;
    reg_str(k, L"FontBody", g_fontBody, 64);
    reg_str(k, L"FontCode", g_fontCode, 64);
    DWORD t, sz = sizeof *wp;
    if (RegQueryValueExW(k, L"Placement", NULL, &t, (BYTE *)wp, &sz) == ERROR_SUCCESS &&
        t == REG_BINARY && sz == sizeof *wp && wp->length == sizeof *wp) {
        if (MonitorFromRect(&wp->rcNormalPosition, MONITOR_DEFAULTTONULL)) *has_wp = 1;
    }
    RegCloseKey(k);
}

static void save_settings(void)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    DWORD v;
    v = (DWORD)g_scale;    RegSetValueExW(k, L"Zoom", 0, REG_DWORD, (BYTE *)&v, sizeof v);
    v = (DWORD)g_dark;     RegSetValueExW(k, L"Dark", 0, REG_DWORD, (BYTE *)&v, sizeof v);
    v = (DWORD)g_auto;     RegSetValueExW(k, L"AutoReload", 0, REG_DWORD, (BYTE *)&v, sizeof v);
    v = (DWORD)g_fontSize; RegSetValueExW(k, L"FontSize", 0, REG_DWORD, (BYTE *)&v, sizeof v);
    RegSetValueExW(k, L"FontBody", 0, REG_SZ, (BYTE *)g_fontBody, (DWORD)((wcslen(g_fontBody) + 1) * sizeof(WCHAR)));
    RegSetValueExW(k, L"FontCode", 0, REG_SZ, (BYTE *)g_fontCode, (DWORD)((wcslen(g_fontCode) + 1) * sizeof(WCHAR)));
    WINDOWPLACEMENT wp;
    wp.length = sizeof wp;
    if (GetWindowPlacement(g_hwnd, &wp))
        RegSetValueExW(k, L"Placement", 0, REG_BINARY, (BYTE *)&wp, sizeof wp);
    RegCloseKey(k);
}

/* ------------------------------------------------------------------ */
/* rendering                                                           */
/* ------------------------------------------------------------------ */
typedef struct { const char *p; size_t left; } StreamSrc;

static DWORD CALLBACK stream_cb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb)
{
    StreamSrc *s = (StreamSrc *)cookie;
    size_t n = s->left < (size_t)cb ? s->left : (size_t)cb;
    memcpy(buf, s->p, n);
    s->p += n;
    s->left -= n;
    *pcb = (LONG)n;
    return 0;
}

static void get_fmt_rect(RECT *rc) { SendMessageW(g_edit, EM_GETRECT, 0, (LPARAM)rc); }

static LONG first_visible_cp(void)
{
    RECT rc;
    get_fmt_rect(&rc);
    POINTL pt = { rc.left + 1, rc.top + 1 };
    return (LONG)SendMessageW(g_edit, EM_CHARFROMPOS, 0, (LPARAM)&pt);
}

static void scroll_to_cp(LONG cp)
{
    RECT rc;
    POINTL p = {0, 0};
    POINT sp = {0, 0};
    get_fmt_rect(&rc);
    SendMessageW(g_edit, EM_POSFROMCHAR, (WPARAM)&p, cp);
    SendMessageW(g_edit, EM_GETSCROLLPOS, 0, (LPARAM)&sp);
    sp.y += p.y - rc.top;
    if (sp.y < 0) sp.y = 0;
    SendMessageW(g_edit, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
}

static int content_width_twips(void)
{
    RECT rc;
    get_fmt_rect(&rc);
    int px = rc.right - rc.left;
    if (px < 100) px = 100;
    return MulDiv(px, 1440, dpi_x()) - 120;
}

static void update_title(void)
{
    WCHAR t[MAX_PATH * 2 + 64];
    WCHAR zoom[32] = L"";
    if (g_scale != 100) swprintf(zoom, 32, L" (%d%%)", g_scale);
    if (g_path[0])
        swprintf(t, sizeof t / sizeof t[0], L"%ls%ls%ls - %ls", PathFindFileNameW(g_path),
                 g_raw ? L" [\x30BD\x30FC\x30B9]" : L"", zoom, APP_NAME);
    else
        swprintf(t, sizeof t / sizeof t[0], L"%ls%ls", APP_NAME, zoom);
    SetWindowTextW(g_hwnd, t);
}

static void render(int keep)
{
    LONG cp = 0;
    POINT sp = {0, 0};
    if (keep == KEEP_CP) cp = first_visible_cp();
    else if (keep == KEEP_Y) SendMessageW(g_edit, EM_GETSCROLLPOS, 0, (LPARAM)&sp);

    char *fb = wide_to_utf8(g_fontBody);
    char *fc = wide_to_utf8(g_fontCode);
    MdOptions o;
    o.scale_pct   = g_scale;
    o.width_twips = content_width_twips();
    o.dark        = g_dark;
    o.font_body   = fb ? fb : "Yu Gothic UI";
    o.font_code   = fc ? fc : "Consolas";
    o.body_halfpt = g_fontSize * 2;

    md_doc_free(&g_doc);
    const char *src = g_src ? g_src : WELCOME;
    size_t len = g_src ? g_srclen : sizeof WELCOME - 1;
    if (g_raw && g_src) text_to_rtf(src, len, &o, &g_doc);
    else md_to_rtf(src, len, &o, &g_doc);
    free(fb);
    free(fc);

    unsigned bg = md_theme_background(g_dark);
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_edit, EM_SETBKGNDCOLOR, 0, RGB((bg >> 16) & 255, (bg >> 8) & 255, bg & 255));
    StreamSrc ss = { g_doc.rtf, g_doc.rtf_len };
    EDITSTREAM es = { (DWORD_PTR)&ss, 0, stream_cb };
    SendMessageW(g_edit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    SendMessageW(g_edit, EM_SETSEL, 0, 0);
    if (keep == KEEP_CP) scroll_to_cp(cp);
    else {
        if (keep == KEEP_TOP) sp.x = sp.y = 0;
        SendMessageW(g_edit, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
    }
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, NULL, TRUE);
    g_lastWidth = o.width_twips;
    update_title();
}

static void apply_theme(void)
{
    set_dark_titlebar(g_hwnd, g_dark);
    CheckMenuItem(g_menu, IDM_DARK, g_dark ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_menu, IDM_AUTORELOAD, g_auto ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_menu, IDM_RAW, g_raw ? MF_CHECKED : MF_UNCHECKED);
}

/* ------------------------------------------------------------------ */
/* files / navigation                                                  */
/* ------------------------------------------------------------------ */
static void hist_push(const WCHAR *path, LONG cp)
{
    if (g_nhist == HIST_MAX) {
        free(g_hist[0].path);
        memmove(g_hist, g_hist + 1, sizeof(Hist) * (HIST_MAX - 1));
        g_nhist--;
    }
    g_hist[g_nhist].path = _wcsdup(path);
    g_hist[g_nhist].cp = cp;
    g_nhist++;
}

static int open_file(const WCHAR *path, int push)
{
    WCHAR full[MAX_PATH * 2];
    if (!GetFullPathNameW(path, MAX_PATH * 2, full, NULL)) lstrcpynW(full, path, MAX_PATH * 2);
    char *buf;
    size_t len;
    FILETIME ft;
    DWORD sz;
    if (!read_file(full, &buf, &len, &ft, &sz)) {
        WCHAR msg[MAX_PATH * 2 + 64];
        swprintf(msg, sizeof msg / sizeof msg[0], L"\x30D5\x30A1\x30A4\x30EB\x3092\x958B\x3051\x307E\x305B\x3093\x3002\n%ls", full);
        MessageBoxW(g_hwnd, msg, APP_NAME, MB_ICONWARNING);
        return 0;
    }
    if (push && g_path[0]) hist_push(g_path, first_visible_cp());
    free(g_src);
    g_src = buf;
    g_srclen = len;
    lstrcpynW(g_path, full, MAX_PATH * 2);
    g_ftime = ft;
    g_fsize = sz;
    render(KEEP_TOP);
    return 1;
}

static void reload_file(int keep)
{
    if (!g_path[0]) { render(KEEP_CP); return; }
    char *buf;
    size_t len;
    FILETIME ft;
    DWORD sz;
    if (!read_file(g_path, &buf, &len, &ft, &sz)) return;
    free(g_src);
    g_src = buf;
    g_srclen = len;
    g_ftime = ft;
    g_fsize = sz;
    render(keep);
}

static void go_back(void)
{
    if (g_nhist == 0) { MessageBeep(MB_OK); return; }
    Hist h = g_hist[--g_nhist];
    if (open_file(h.path, 0)) scroll_to_cp(h.cp);
    free(h.path);
}

static void check_file_changed(void)
{
    if (!g_auto || !g_path[0]) return;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(g_path, GetFileExInfoStandard, &fa)) return;
    if (CompareFileTime(&fa.ftLastWriteTime, &g_ftime) != 0 || fa.nFileSizeLow != g_fsize)
        reload_file(KEEP_Y);
}

static void do_open_dialog(void)
{
    WCHAR file[MAX_PATH * 2] = L"";
    OPENFILENAMEW of;
    memset(&of, 0, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = L"Markdown (*.md;*.markdown;*.mdown;*.mkd;*.txt)\0*.md;*.markdown;*.mdown;*.mkd;*.txt\0"
                     L"\x3059\x3079\x3066\x306E\x30D5\x30A1\x30A4\x30EB (*.*)\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH * 2;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&of)) open_file(file, 1);
}

static void open_in_editor(void)
{
    if (!g_path[0]) return;
    INT_PTR r = (INT_PTR)ShellExecuteW(g_hwnd, L"edit", g_path, NULL, NULL, SW_SHOWNORMAL);
    if (r <= 32) {
        WCHAR args[MAX_PATH * 2 + 4];
        swprintf(args, sizeof args / sizeof args[0], L"\"%ls\"", g_path);
        ShellExecuteW(g_hwnd, L"open", L"notepad.exe", args, NULL, SW_SHOWNORMAL);
    }
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* percent-decodes a UTF-8 string in place */
static void pct_decode(char *s)
{
    char *d = s;
    while (*s) {
        if (s[0] == '%' && hexval((unsigned char)s[1]) >= 0 && hexval((unsigned char)s[2]) >= 0) {
            *d++ = (char)(hexval((unsigned char)s[1]) * 16 + hexval((unsigned char)s[2]));
            s += 3;
        } else *d++ = *s++;
    }
    *d = 0;
}

static void jump_anchor(const char *frag_utf8)
{
    char *f = _strdup(frag_utf8);
    if (!f) return;
    pct_decode(f);
    for (char *p = f; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    int k = -1;
    for (int i = 0; i < g_doc.nheads; i++) if (!strcmp(g_doc.heads[i].slug, f)) { k = i; break; }
    free(f);
    if (k < 0) { MessageBeep(MB_OK); return; }
    const char *text = g_doc.heads[k].text;
    if (!text[0]) return;
    int ord = 0;
    for (int i = 0; i < k; i++) if (!strcmp(g_doc.heads[i].text, text)) ord++;
    WCHAR *w = utf8_to_wide(text);
    if (!w) return;
    LONG start = 0;
    for (;;) {
        FINDTEXTEXW ft;
        ft.chrg.cpMin = start;
        ft.chrg.cpMax = -1;
        ft.lpstrText = w;
        LONG r = (LONG)SendMessageW(g_edit, EM_FINDTEXTEXW, FR_DOWN | FR_MATCHCASE, (LPARAM)&ft);
        if (r < 0) break;
        LONG line = (LONG)SendMessageW(g_edit, EM_EXLINEFROMCHAR, 0, r);
        LONG ls = (LONG)SendMessageW(g_edit, EM_LINEINDEX, (WPARAM)line, 0);
        if (ls == r) {
            if (ord == 0) { scroll_to_cp(r); break; }
            ord--;
        }
        start = r + 1;
    }
    free(w);
}

static int is_md_ext(const WCHAR *path)
{
    const WCHAR *e = PathFindExtensionW(path);
    return !_wcsicmp(e, L".md") || !_wcsicmp(e, L".markdown") || !_wcsicmp(e, L".mdown") ||
           !_wcsicmp(e, L".mkd") || !_wcsicmp(e, L".txt");
}

static void navigate(const WCHAR *url)
{
    if (!url[0]) return;
    if (url[0] == L'#') {
        char *f = wide_to_utf8(url + 1);
        if (f) { jump_anchor(f); free(f); }
        return;
    }
    /* URL with scheme? (two or more letters followed by ':') */
    const WCHAR *c = url;
    while ((*c >= L'a' && *c <= L'z') || (*c >= L'A' && *c <= L'Z') || (*c >= L'0' && *c <= L'9') ||
           *c == L'+' || *c == L'-' || *c == L'.') c++;
    if (*c == L':' && c - url >= 2) {
        if (!_wcsnicmp(url, L"http:", 5) || !_wcsnicmp(url, L"https:", 6) ||
            !_wcsnicmp(url, L"mailto:", 7) || !_wcsnicmp(url, L"ftp:", 4)) {
            ShellExecuteW(g_hwnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        } else {
            WCHAR msg[2200];
            swprintf(msg, sizeof msg / sizeof msg[0], L"\x6B21\x306E\x30EA\x30F3\x30AF\x3092\x958B\x304D\x307E\x3059\x304B\xFF1F\n\n%.2000ls", url);
            if (MessageBoxW(g_hwnd, msg, APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES)
                ShellExecuteW(g_hwnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        }
        return;
    }
    if (!_wcsnicmp(url, L"www.", 4)) {
        WCHAR u[2100];
        swprintf(u, sizeof u / sizeof u[0], L"http://%.2000ls", url);
        ShellExecuteW(g_hwnd, L"open", u, NULL, NULL, SW_SHOWNORMAL);
        return;
    }

    /* local file (relative to the current document) */
    char *u8 = wide_to_utf8(url);
    if (!u8) return;
    char *frag = strchr(u8, '#');
    if (frag) *frag++ = 0;
    char *q = strchr(u8, '?');
    if (q) *q = 0;
    pct_decode(u8);
    WCHAR *rel = utf8_to_wide(u8);
    if (!rel) { free(u8); return; }
    for (WCHAR *p = rel; *p; p++) if (*p == L'/') *p = L'\\';

    WCHAR full[MAX_PATH * 2];
    const WCHAR *r = rel;
    if (PathIsRelativeW(rel) || (r[0] == L'\\' && r[1] != L'\\')) {
        WCHAR dir[MAX_PATH * 2];
        if (g_path[0]) { lstrcpynW(dir, g_path, MAX_PATH * 2); PathRemoveFileSpecW(dir); }
        else GetCurrentDirectoryW(MAX_PATH * 2, dir);
        while (*r == L'\\') r++;
        if (!PathCombineW(full, dir, r)) lstrcpynW(full, r, MAX_PATH * 2);
    } else {
        lstrcpynW(full, rel, MAX_PATH * 2);
    }

    if (!rel[0] && frag) {                     /* "#frag" handled above; "?#frag" etc. */
        jump_anchor(frag);
    } else if (!PathFileExistsW(full)) {
        WCHAR msg[MAX_PATH * 2 + 64];
        swprintf(msg, sizeof msg / sizeof msg[0], L"\x30D5\x30A1\x30A4\x30EB\x304C\x898B\x3064\x304B\x308A\x307E\x305B\x3093\x3002\n%ls", full);
        MessageBoxW(g_hwnd, msg, APP_NAME, MB_ICONWARNING);
    } else if (is_md_ext(full)) {
        if (open_file(full, 1) && frag && frag[0]) jump_anchor(frag);
    } else {
        WCHAR msg[MAX_PATH * 2 + 64];
        swprintf(msg, sizeof msg / sizeof msg[0], L"\x6B21\x306E\x30D5\x30A1\x30A4\x30EB\x3092\x958B\x304D\x307E\x3059\x304B\xFF1F\n\n%ls", full);
        if (MessageBoxW(g_hwnd, msg, APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES)
            ShellExecuteW(g_hwnd, L"open", full, NULL, NULL, SW_SHOWNORMAL);
    }
    free(rel);
    free(u8);
}

static void on_link(ENLINK *el)
{
    if (el->msg != WM_LBUTTONUP) return;
    CHARRANGE sel;
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMin != sel.cpMax) return;          /* user was selecting text */
    LONG len = el->chrg.cpMax - el->chrg.cpMin;
    if (len <= 0 || len > 8192) return;
    WCHAR *t = (WCHAR *)calloc((size_t)len + 2, sizeof(WCHAR));
    if (!t) return;
    TEXTRANGEW tr;
    tr.chrg = el->chrg;
    tr.lpstrText = t;
    SendMessageW(g_edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);

    WCHAR *url = NULL;
    /* 1) the range may contain the hidden field instruction */
    WCHAR *h = wcsstr(t, L"HYPERLINK");
    if (h) {
        WCHAR *q1 = wcschr(h, L'"');
        WCHAR *q2 = q1 ? wcschr(q1 + 1, L'"') : NULL;
        if (q2) { *q2 = 0; url = _wcsdup(q1 + 1); }
    }
    /* 2) look up the visible text in the link table */
    if (!url) {
        char *t8 = wide_to_utf8(t);
        if (t8) {
            for (int i = 0; i < g_doc.nlinks; i++)
                if (!strcmp(g_doc.links[i].text, t8)) { url = utf8_to_wide(g_doc.links[i].url); break; }
            free(t8);
        }
    }
    /* 3) the text itself is a URL */
    if (!url && (wcsstr(t, L"://") || !_wcsnicmp(t, L"www.", 4) || !_wcsnicmp(t, L"mailto:", 7)))
        url = _wcsdup(t);
    free(t);
    if (url) {
        navigate(url);
        free(url);
    }
}

/* ------------------------------------------------------------------ */
/* find                                                                */
/* ------------------------------------------------------------------ */
static void show_find(void)
{
    if (g_find) { SetFocus(g_find); return; }
    CHARRANGE sel;
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMax > sel.cpMin && sel.cpMax - sel.cpMin < 250) {
        TEXTRANGEW tr;
        tr.chrg = sel;
        tr.lpstrText = g_findText;
        SendMessageW(g_edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    }
    memset(&g_fr, 0, sizeof g_fr);
    g_fr.lStructSize = sizeof g_fr;
    g_fr.hwndOwner = g_hwnd;
    g_fr.lpstrFindWhat = g_findText;
    g_fr.wFindWhatLen = sizeof g_findText / sizeof g_findText[0];
    g_fr.Flags = FR_DOWN;
    g_find = FindTextW(&g_fr);
}

static void do_find(int down)
{
    if (!g_findText[0]) { show_find(); return; }
    CHARRANGE sel;
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    FINDTEXTEXW ft;
    ft.lpstrText = g_findText;
    WPARAM fl = (down ? FR_DOWN : 0) | (g_fr.Flags & (FR_MATCHCASE | FR_WHOLEWORD));
    if (down) { ft.chrg.cpMin = sel.cpMax; ft.chrg.cpMax = -1; }
    else      { ft.chrg.cpMin = sel.cpMin; ft.chrg.cpMax = 0; }
    LONG r = (LONG)SendMessageW(g_edit, EM_FINDTEXTEXW, fl, (LPARAM)&ft);
    if (r < 0) {                                 /* wrap around */
        if (down) { ft.chrg.cpMin = 0; ft.chrg.cpMax = -1; }
        else      { ft.chrg.cpMin = GetWindowTextLengthW(g_edit); ft.chrg.cpMax = 0; }
        r = (LONG)SendMessageW(g_edit, EM_FINDTEXTEXW, fl, (LPARAM)&ft);
    }
    if (r < 0) {
        WCHAR msg[320];
        swprintf(msg, sizeof msg / sizeof msg[0], L"\x300C%ls\x300D\x304C\x898B\x3064\x304B\x308A\x307E\x305B\x3093\x3002", g_findText);
        MessageBoxW(g_find ? g_find : g_hwnd, msg, APP_NAME, MB_ICONINFORMATION);
        return;
    }
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */
static void zoom_step(int dir)
{
    int n = (int)(sizeof ZOOMS / sizeof ZOOMS[0]);
    int z = g_scale;
    if (dir == 0) z = 100;
    else if (dir > 0) { for (int i = 0; i < n; i++) if (ZOOMS[i] > g_scale) { z = ZOOMS[i]; break; } }
    else { for (int i = n - 1; i >= 0; i--) if (ZOOMS[i] < g_scale) { z = ZOOMS[i]; break; } }
    if (z == g_scale) return;
    g_scale = z;
    render(KEEP_CP);
}

static void show_about(void)
{
    MessageBoxW(g_hwnd,
                APP_NAME L" " APP_VERSION L"\n\n"
                L"\x8EFD\x91CF Markdown \x30D3\x30E5\x30FC\x30A2 (Win32 API + RichEdit)\n\n"
                L"\x30D5\x30A9\x30F3\x30C8\x8A2D\x5B9A: HKCU\\" REG_KEY L"\n"
                L"  FontBody / FontCode (REG_SZ), FontSize (REG_DWORD)",
                APP_NAME, MB_ICONINFORMATION);
}

static void show_context_menu(int x, int y)
{
    HMENU m = CreatePopupMenu();
    CHARRANGE sel;
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    AppendMenuW(m, MF_STRING | (sel.cpMin == sel.cpMax ? MF_GRAYED : 0), IDM_COPY, L"\x30B3\x30D4\x30FC(&C)\tCtrl+C");
    AppendMenuW(m, MF_STRING, IDM_SELALL, L"\x3059\x3079\x3066\x9078\x629E(&A)\tCtrl+A");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_nhist ? 0 : MF_GRAYED), IDM_BACK, L"\x623B\x308B(&B)\tAlt+\x2190");
    AppendMenuW(m, MF_STRING, IDM_RELOAD, L"\x518D\x8AAD\x307F\x8FBC\x307F(&R)\tF5");
    AppendMenuW(m, MF_STRING | (g_raw ? MF_CHECKED : 0), IDM_RAW, L"\x30BD\x30FC\x30B9\x8868\x793A(&U)\tCtrl+U");
    if (x == -1 && y == -1) {
        RECT rc;
        GetWindowRect(g_edit, &rc);
        x = rc.left + 20;
        y = rc.top + 20;
    }
    TrackPopupMenu(m, TPM_RIGHTBUTTON, x, y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

static void on_command(int id)
{
    switch (id) {
    case IDM_OPEN:       do_open_dialog(); break;
    case IDM_RELOAD:     reload_file(KEEP_Y); break;
    case IDM_EDIT:       open_in_editor(); break;
    case IDM_EXIT:       PostMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
    case IDM_COPY:       SendMessageW(g_edit, WM_COPY, 0, 0); break;
    case IDM_SELALL:     SendMessageW(g_edit, EM_SETSEL, 0, -1); break;
    case IDM_FIND:       show_find(); break;
    case IDM_FINDNEXT:   do_find(1); break;
    case IDM_FINDPREV:   do_find(0); break;
    case IDM_ZOOMIN:     zoom_step(+1); break;
    case IDM_ZOOMOUT:    zoom_step(-1); break;
    case IDM_ZOOMRESET:  zoom_step(0); break;
    case IDM_DARK:       g_dark = !g_dark; apply_theme(); render(KEEP_CP); break;
    case IDM_AUTORELOAD: g_auto = !g_auto; apply_theme(); break;
    case IDM_RAW:        g_raw = !g_raw; apply_theme(); render(KEEP_TOP); break;
    case IDM_BACK:       go_back(); break;
    case IDM_ABOUT:      show_about(); break;
    }
}

static void open_drop(HDROP hd)
{
    WCHAR file[MAX_PATH * 2];
    if (DragQueryFileW(hd, 0, file, MAX_PATH * 2)) {
        SetForegroundWindow(g_hwnd);
        open_file(file, 1);
    }
    DragFinish(hd);
}

/* ------------------------------------------------------------------ */
/* OLE drop target                                                     */
/*                                                                     */
/* RichEdit registers its own OLE drop target, which refuses files     */
/* while the control is read-only (and then WM_DROPFILES never comes). */
/* We replace it with our own target that accepts CF_HDROP.            */
/* ------------------------------------------------------------------ */
typedef struct {
    IDropTarget iface;
    LONG        ref;
    BOOL        accept;
} DropTarget;

static FORMATETC g_fmtHdrop = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

static DWORD drop_effect(DWORD allowed)
{
    if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
    if (allowed & DROPEFFECT_LINK) return DROPEFFECT_LINK;
    return DROPEFFECT_NONE;   /* never MOVE: the source would delete the file */
}

static HRESULT STDMETHODCALLTYPE dt_QueryInterface(IDropTarget *This, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget)) {
        *ppv = This;
        IDropTarget_AddRef(This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dt_AddRef(IDropTarget *This)
{
    return (ULONG)InterlockedIncrement(&((DropTarget *)This)->ref);
}
static ULONG STDMETHODCALLTYPE dt_Release(IDropTarget *This)
{
    return (ULONG)InterlockedDecrement(&((DropTarget *)This)->ref);  /* static object */
}
static HRESULT STDMETHODCALLTYPE dt_DragEnter(IDropTarget *This, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
    (void)keys; (void)pt;
    DropTarget *dt = (DropTarget *)This;
    dt->accept = data && IDataObject_QueryGetData(data, &g_fmtHdrop) == S_OK;
    *effect = dt->accept ? drop_effect(*effect) : DROPEFFECT_NONE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_DragOver(IDropTarget *This, DWORD keys, POINTL pt, DWORD *effect)
{
    (void)keys; (void)pt;
    *effect = ((DropTarget *)This)->accept ? drop_effect(*effect) : DROPEFFECT_NONE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_DragLeave(IDropTarget *This)
{
    ((DropTarget *)This)->accept = FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_Drop(IDropTarget *This, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
    (void)keys; (void)pt;
    DropTarget *dt = (DropTarget *)This;
    STGMEDIUM stg;
    *effect = DROPEFFECT_NONE;
    if (dt->accept && data && IDataObject_GetData(data, &g_fmtHdrop, &stg) == S_OK) {
        HDROP hd = (HDROP)GlobalLock(stg.hGlobal);
        if (hd) {
            UINT n = DragQueryFileW(hd, 0, NULL, 0);
            if (n > 0) {
                WCHAR *path = (WCHAR *)calloc((size_t)n + 1, sizeof(WCHAR));
                if (path && DragQueryFileW(hd, 0, path, n + 1)) {
                    /* open after returning, so Explorer is not blocked by a dialog */
                    if (!PostMessageW(g_hwnd, WM_APP_OPENFILE, 0, (LPARAM)path)) free(path);
                    *effect = drop_effect(DROPEFFECT_COPY | DROPEFFECT_LINK);
                } else free(path);
            }
            GlobalUnlock(stg.hGlobal);
        }
        ReleaseStgMedium(&stg);
    }
    dt->accept = FALSE;
    return S_OK;
}

static IDropTargetVtbl g_dtVtbl = {
    dt_QueryInterface, dt_AddRef, dt_Release,
    dt_DragEnter, dt_DragOver, dt_DragLeave, dt_Drop,
};
static DropTarget g_dropTarget = { { &g_dtVtbl }, 1, FALSE };

static void register_drop_targets(void)
{
    /* replace RichEdit's own target; the main window covers the menu bar etc. */
    RevokeDragDrop(g_edit);
    HRESULT h1 = RegisterDragDrop(g_edit, &g_dropTarget.iface);
    HRESULT h2 = RegisterDragDrop(g_hwnd, &g_dropTarget.iface);
    if (FAILED(h1) || FAILED(h2)) {
        /* fallback: classic WM_DROPFILES */
        DragAcceptFiles(g_hwnd, TRUE);
        if (FAILED(h1)) DragAcceptFiles(g_edit, TRUE);
    }
}

static void revoke_drop_targets(void)
{
    RevokeDragDrop(g_edit);
    RevokeDragDrop(g_hwnd);
}

/* ------------------------------------------------------------------ */
/* windows                                                             */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK edit_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    switch (msg) {
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
            zoom_step(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? +1 : -1);
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        show_context_menu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_BACK) { go_back(); return 0; }
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void layout(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    MoveWindow(g_edit, 0, 0, rc.right, rc.bottom, TRUE);
    int pad = MulDiv(20, dpi_x(), 96);
    RECT fr;
    GetClientRect(g_edit, &fr);
    fr.left += pad;
    fr.top += pad / 2;
    fr.right -= pad;
    fr.bottom -= pad / 4;
    if (fr.right <= fr.left) fr.right = fr.left + 1;
    if (fr.bottom <= fr.top) fr.bottom = fr.top + 1;
    SendMessageW(g_edit, EM_SETRECT, 0, (LPARAM)&fr);
}

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu();
    HMENU f = CreatePopupMenu(), e = CreatePopupMenu(), v = CreatePopupMenu(), h = CreatePopupMenu();

    AppendMenuW(f, MF_STRING, IDM_OPEN,   L"\x958B\x304F(&O)...\tCtrl+O");
    AppendMenuW(f, MF_STRING, IDM_RELOAD, L"\x518D\x8AAD\x307F\x8FBC\x307F(&R)\tF5");
    AppendMenuW(f, MF_STRING, IDM_EDIT,   L"\x30A8\x30C7\x30A3\x30BF\x3067\x958B\x304F(&E)\tCtrl+E");
    AppendMenuW(f, MF_STRING, IDM_BACK,   L"\x623B\x308B(&B)\tAlt+\x2190");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_EXIT,   L"\x7D42\x4E86(&X)\tAlt+F4");

    AppendMenuW(e, MF_STRING, IDM_COPY,     L"\x30B3\x30D4\x30FC(&C)\tCtrl+C");
    AppendMenuW(e, MF_STRING, IDM_SELALL,   L"\x3059\x3079\x3066\x9078\x629E(&A)\tCtrl+A");
    AppendMenuW(e, MF_SEPARATOR, 0, NULL);
    AppendMenuW(e, MF_STRING, IDM_FIND,     L"\x691C\x7D22(&F)...\tCtrl+F");
    AppendMenuW(e, MF_STRING, IDM_FINDNEXT, L"\x6B21\x3092\x691C\x7D22(&N)\tF3");
    AppendMenuW(e, MF_STRING, IDM_FINDPREV, L"\x524D\x3092\x691C\x7D22(&P)\tShift+F3");

    AppendMenuW(v, MF_STRING, IDM_ZOOMIN,     L"\x62E1\x5927(&I)\tCtrl++");
    AppendMenuW(v, MF_STRING, IDM_ZOOMOUT,    L"\x7E2E\x5C0F(&O)\tCtrl+-");
    AppendMenuW(v, MF_STRING, IDM_ZOOMRESET,  L"\x6A19\x6E96\x30B5\x30A4\x30BA(&Z)\tCtrl+0");
    AppendMenuW(v, MF_SEPARATOR, 0, NULL);
    AppendMenuW(v, MF_STRING, IDM_DARK,       L"\x30C0\x30FC\x30AF\x30C6\x30FC\x30DE(&D)\tCtrl+D");
    AppendMenuW(v, MF_STRING, IDM_RAW,        L"\x30BD\x30FC\x30B9\x8868\x793A(&U)\tCtrl+U");
    AppendMenuW(v, MF_STRING, IDM_AUTORELOAD, L"\x5909\x66F4\x6642\x306B\x81EA\x52D5\x518D\x8AAD\x307F\x8FBC\x307F(&A)");

    AppendMenuW(h, MF_STRING, IDM_ABOUT, L"\x30D0\x30FC\x30B8\x30E7\x30F3\x60C5\x5831(&A)");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)f, L"\x30D5\x30A1\x30A4\x30EB(&F)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)e, L"\x7DE8\x96C6(&E)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)v, L"\x8868\x793A(&V)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)h, L"\x30D8\x30EB\x30D7(&H)");
    return bar;
}

static HACCEL build_accel(void)
{
    ACCEL a[] = {
        { FVIRTKEY | FCONTROL,          'O',           IDM_OPEN },
        { FVIRTKEY,                     VK_F5,         IDM_RELOAD },
        { FVIRTKEY | FCONTROL,          'R',           IDM_RELOAD },
        { FVIRTKEY | FCONTROL,          'E',           IDM_EDIT },
        { FVIRTKEY | FCONTROL,          'F',           IDM_FIND },
        { FVIRTKEY,                     VK_F3,         IDM_FINDNEXT },
        { FVIRTKEY | FSHIFT,            VK_F3,         IDM_FINDPREV },
        { FVIRTKEY | FCONTROL,          VK_OEM_PLUS,   IDM_ZOOMIN },
        { FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_PLUS,   IDM_ZOOMIN },
        { FVIRTKEY | FCONTROL,          VK_ADD,        IDM_ZOOMIN },
        { FVIRTKEY | FCONTROL,          VK_OEM_MINUS,  IDM_ZOOMOUT },
        { FVIRTKEY | FCONTROL,          VK_SUBTRACT,   IDM_ZOOMOUT },
        { FVIRTKEY | FCONTROL,          '0',           IDM_ZOOMRESET },
        { FVIRTKEY | FCONTROL,          VK_NUMPAD0,    IDM_ZOOMRESET },
        { FVIRTKEY | FCONTROL,          'D',           IDM_DARK },
        { FVIRTKEY | FCONTROL,          'U',           IDM_RAW },
        { FVIRTKEY | FALT,              VK_LEFT,       IDM_BACK },
        { FVIRTKEY | FCONTROL,          'W',           IDM_EXIT },
    };
    return CreateAcceleratorTableW(a, (int)(sizeof a / sizeof a[0]));
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g_findMsg && g_findMsg) {
        FINDREPLACEW *fr = (FINDREPLACEW *)lp;
        if (fr->Flags & FR_DIALOGTERM) g_find = NULL;
        else if (fr->Flags & FR_FINDNEXT) do_find((fr->Flags & FR_DOWN) != 0);
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        g_edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                 ES_AUTOVSCROLL | ES_NOHIDESEL | ES_DISABLENOSCROLL,
                                 0, 0, 0, 0, hwnd, (HMENU)1, g_inst, NULL);
        if (!g_edit) return -1;
        SendMessageW(g_edit, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
        SendMessageW(g_edit, EM_SETEVENTMASK, 0, ENM_LINK | ENM_DROPFILES);
        SendMessageW(g_edit, EM_AUTOURLDETECT, FALSE, 0);
        SendMessageW(g_edit, EM_SETUNDOLIMIT, 0, 0);
        SendMessageW(g_edit, EM_SETTYPOGRAPHYOPTIONS, TO_ADVANCEDTYPOGRAPHY, TO_ADVANCEDTYPOGRAPHY);
        SetWindowSubclass(g_edit, edit_subclass, 1, 0);
        return 0;

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            layout(hwnd);
            SetTimer(hwnd, TID_RESIZE, 150, NULL);
        }
        return 0;

    case WM_TIMER:
        if (wp == TID_RESIZE) {
            KillTimer(hwnd, TID_RESIZE);
            if (content_width_twips() != g_lastWidth) render(KEEP_CP);
        } else if (wp == TID_WATCH) {
            check_file_changed();
        }
        return 0;

    case WM_SETFOCUS:
        SetFocus(g_edit);
        return 0;

    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->hwndFrom == g_edit) {
            if (nh->code == EN_LINK) { on_link((ENLINK *)lp); return 0; }
            if (nh->code == EN_DROPFILES) { open_drop((HDROP)((ENDROPFILES *)lp)->hDrop); return 1; }
        }
        break;
    }

    case WM_DROPFILES:
        open_drop((HDROP)wp);
        return 0;

    case WM_APP_OPENFILE: {
        WCHAR *path = (WCHAR *)lp;
        if (path) {
            SetForegroundWindow(hwnd);
            open_file(path, 1);
            free(path);
        }
        return 0;
    }

    case WM_COMMAND:
        on_command(LOWORD(wp));
        return 0;

    case WM_APPCOMMAND:
        if (GET_APPCOMMAND_LPARAM(lp) == APPCOMMAND_BROWSER_BACKWARD) { go_back(); return TRUE; }
        break;

    case WM_CLOSE:
        save_settings();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        revoke_drop_targets();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmdLine, int nShow)
{
    (void)hPrev; (void)cmdLine;
    g_inst = hInst;

    HRESULT ole = OleInitialize(NULL);   /* required for RegisterDragDrop */

    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    if (!LoadLibraryW(L"Msftedit.dll")) {
        MessageBoxW(NULL, L"Msftedit.dll \x3092\x8AAD\x307F\x8FBC\x3081\x307E\x305B\x3093\x3002", APP_NAME, MB_ICONERROR);
        return 1;
    }
    g_findMsg = RegisterWindowMessageW(FINDMSGSTRINGW);

    WINDOWPLACEMENT wp;
    int has_wp;
    load_settings(&wp, &has_wp);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MdViewWindow";
    RegisterClassExW(&wc);

    g_menu = build_menu();
    int d = dpi_x();
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, APP_NAME, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(900, d, 96), MulDiv(1000, d, 96),
                             NULL, g_menu, hInst, NULL);
    if (!g_hwnd) return 1;
    register_drop_targets();
    g_accel = build_accel();
    apply_theme();

    if (has_wp) {
        if (wp.showCmd != SW_SHOWMAXIMIZED) wp.showCmd = SW_SHOWNORMAL;
        if (nShow == SW_SHOWMINIMIZED || nShow == SW_MINIMIZE || nShow == SW_SHOWMINNOACTIVE) wp.showCmd = (UINT)nShow;
        SetWindowPlacement(g_hwnd, &wp);
    } else {
        ShowWindow(g_hwnd, nShow);
    }
    UpdateWindow(g_hwnd);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) open_file(argv[1], 0);
    else render(KEEP_TOP);
    if (argv) LocalFree(argv);

    SetTimer(g_hwnd, TID_WATCH, 1000, NULL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (g_find && IsDialogMessageW(g_find, &m)) continue;
        if (TranslateAcceleratorW(g_hwnd, g_accel, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    md_doc_free(&g_doc);
    free(g_src);
    if (SUCCEEDED(ole)) OleUninitialize();
    return (int)m.wParam;
}
