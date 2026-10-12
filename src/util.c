/*
 * util.c — 文字列変換・画面 DPI・リソースの小さな補助関数
 */
#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

wchar_t *str_dup(const wchar_t *s)
{
    if (!s) return NULL;
    size_t bytes = (wcslen(s) + 1) * sizeof(wchar_t);
    wchar_t *copy = malloc(bytes);
    if (copy) memcpy(copy, s, bytes);
    return copy;
}

char *utf8_from_wide(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = malloc(n > 0 ? (size_t)n : 1);
    if (!s) return NULL;
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    else s[0] = '\0';
    return s;
}

char *utf8_from_wide_n(const wchar_t *w, int length, size_t *outLength)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, length, NULL, 0, NULL, NULL);
    char *s = malloc((size_t)n + 1);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, length, s, n, NULL, NULL);
    s[n] = '\0';
    *outLength = (size_t)n;
    return s;
}

wchar_t *wide_from_utf8(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = malloc(sizeof(wchar_t) * (n > 0 ? (size_t)n : 1));
    if (!w) return NULL;
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    else w[0] = L'\0';
    return w;
}

wchar_t *path_join(const wchar_t *dir, const wchar_t *name)
{
    size_t dirLen = wcslen(dir);
    size_t nameLen = wcslen(name);
    bool needSlash = dirLen > 0 && dir[dirLen - 1] != L'\\';
    size_t total = dirLen + (needSlash ? 1 : 0) + nameLen + 1;

    wchar_t *joined = malloc(total * sizeof(wchar_t));
    if (!joined) return NULL;

    memcpy(joined, dir, dirLen * sizeof(wchar_t));
    if (needSlash) joined[dirLen++] = L'\\';
    memcpy(joined + dirLen, name, (nameLen + 1) * sizeof(wchar_t));
    return joined;
}

wchar_t *module_path(void)
{
    /* 長いパスに対応するため、切り詰められなくなるまでバッファを広げる。 */
    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2) {
        wchar_t *buffer = malloc(capacity * sizeof(wchar_t));
        if (!buffer) return NULL;
        DWORD length = GetModuleFileNameW(NULL, buffer, capacity);
        if (length > 0 && length < capacity) return buffer;
        free(buffer);
        if (length == 0) return NULL;
    }
    return NULL;
}

int screen_dpi(void)
{
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return dpi > 0 ? dpi : 96;
}

const wchar_t *res_string(UINT id, wchar_t *buffer, int capacity)
{
    if (capacity <= 0) return buffer;
    if (LoadStringW(GetModuleHandleW(NULL), id, buffer, capacity) <= 0) buffer[0] = L'\0';
    return buffer;
}

int res_message_box(HWND owner, UINT textId, UINT flags)
{
    wchar_t text[512];
    return MessageBoxW(owner, res_string(textId, text, ARRAY_COUNT(text)), APP_NAME, flags);
}

int res_message_boxf(HWND owner, UINT formatId, UINT flags, ...)
{
    wchar_t format[512];
    res_string(formatId, format, ARRAY_COUNT(format));

    va_list args;
    va_start(args, flags);
    int length = _vscwprintf(format, args);
    va_end(args);
    if (length < 0) return MessageBoxW(owner, format, APP_NAME, flags);

    wchar_t *text = malloc(((size_t)length + 1) * sizeof(wchar_t));
    if (!text) return 0;
    va_start(args, flags);
    vswprintf(text, (size_t)length + 1, format, args);
    va_end(args);

    int result = MessageBoxW(owner, text, APP_NAME, flags);
    free(text);
    return result;
}

const char *res_data(UINT id, size_t *size)
{
    HMODULE module = GetModuleHandleW(NULL);
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL handle = info ? LoadResource(module, info) : NULL;
    const char *data = handle ? (const char *)LockResource(handle) : NULL;
    *size = data ? SizeofResource(module, info) : 0;
    return data;
}
