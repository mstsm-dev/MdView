/*
 * editor.c — 「エディタで開く」（仕様書 4.5 節）
 */
#include "editor.h"

#include <shellapi.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PLACEHOLDER L"%1"
#define PLACEHOLDER_LENGTH 2

/* ------------------------------------------------------------------------
 * 引数・パスの組み立て
 * ------------------------------------------------------------------------ */

static size_t count_placeholders(const wchar_t *s)
{
    size_t n = 0;
    for (const wchar_t *p = wcsstr(s, PLACEHOLDER); p; p = wcsstr(p + PLACEHOLDER_LENGTH, PLACEHOLDER)) n++;
    return n;
}

wchar_t *editor_build_args(const wchar_t *args, const wchar_t *file)
{
    if (!args) args = L"";
    size_t fileLength = wcslen(file);
    size_t count = count_placeholders(args);

    if (count == 0) { /* %1 がない：末尾に "file" を足す */
        size_t argsLength = wcslen(args);
        size_t total = argsLength + 1 + fileLength + 2 + 1;
        wchar_t *out = malloc(total * sizeof(wchar_t));
        if (!out) return NULL;
        wchar_t *cursor = out;
        memcpy(cursor, args, argsLength * sizeof(wchar_t));
        cursor += argsLength;
        if (argsLength > 0) *cursor++ = L' ';
        *cursor++ = L'"';
        memcpy(cursor, file, fileLength * sizeof(wchar_t));
        cursor += fileLength;
        *cursor++ = L'"';
        *cursor = L'\0';
        return out;
    }

    size_t total = wcslen(args) + count * fileLength - count * PLACEHOLDER_LENGTH + 1;
    wchar_t *out = malloc(total * sizeof(wchar_t));
    if (!out) return NULL;
    wchar_t *cursor = out;
    const wchar_t *p = args;
    for (const wchar_t *hit = wcsstr(p, PLACEHOLDER); hit; hit = wcsstr(p, PLACEHOLDER)) {
        size_t before = (size_t)(hit - p);
        memcpy(cursor, p, before * sizeof(wchar_t));
        cursor += before;
        memcpy(cursor, file, fileLength * sizeof(wchar_t));
        cursor += fileLength;
        p = hit + PLACEHOLDER_LENGTH;
    }
    wcscpy(cursor, p);
    return out;
}

wchar_t *editor_resolve_path(const wchar_t *path)
{
    if (!path) return NULL;

    /* 前後の空白と、全体を囲む引用符を除く */
    while (*path == L' ' || *path == L'\t') path++;
    size_t n = wcslen(path);
    while (n > 0 && (path[n - 1] == L' ' || path[n - 1] == L'\t')) n--;
    if (n >= 2 && path[0] == L'"' && path[n - 1] == L'"') {
        path++;
        n -= 2;
    }
    if (n == 0) return NULL;

    wchar_t *trimmed = malloc((n + 1) * sizeof(wchar_t));
    if (!trimmed) return NULL;
    memcpy(trimmed, path, n * sizeof(wchar_t));
    trimmed[n] = L'\0';

    /* 環境変数を展開する */
    DWORD needed = ExpandEnvironmentStringsW(trimmed, NULL, 0);
    wchar_t *expanded = needed ? malloc((size_t)needed * sizeof(wchar_t)) : NULL;
    if (!expanded || !ExpandEnvironmentStringsW(trimmed, expanded, needed)) {
        free(expanded);
        expanded = trimmed;
        trimmed = NULL;
    }
    free(trimmed);

    /* 相対パスは MdView.exe のフォルダを基準にする（ポータブル運用で同じフォルダに置けるように） */
    if (PathIsRelativeW(expanded)) {
        wchar_t *exe = module_path();
        if (exe) {
            wchar_t *slash = wcsrchr(exe, L'\\');
            if (slash) *slash = L'\0';
            wchar_t *joined = path_join(exe, expanded);
            free(exe);
            if (joined) {
                free(expanded);
                expanded = joined;
            }
        }
    }
    return expanded;
}

/* ------------------------------------------------------------------------
 * 起動
 * ------------------------------------------------------------------------ */

/* Path が空のとき（1.2.0 までの動作）：関連付けの「編集」、なければメモ帳 */
static bool open_with_default(HWND owner, const wchar_t *file)
{
    INT_PTR result = (INT_PTR)ShellExecuteW(owner, L"edit", file, NULL, NULL, SW_SHOWNORMAL);
    if (result > 32) return true;

    wchar_t *args = editor_build_args(L"\"%1\"", file);
    if (!args) return false;
    result = (INT_PTR)ShellExecuteW(owner, L"open", L"notepad.exe", args, NULL, SW_SHOWNORMAL);
    free(args);
    return result > 32;
}

bool editor_open(HWND owner, const wchar_t *editorPath, const wchar_t *editorArgs, const wchar_t *file)
{
    wchar_t *exe = editor_resolve_path(editorPath);
    if (!exe) return open_with_default(owner, file);

    bool ok = false;
    if (!PathFileExistsW(exe)) {
        res_message_boxf(owner, IDS_EDITOR_NOT_FOUND, MB_ICONWARNING, exe);
    } else {
        wchar_t *args = editor_build_args(editorArgs, file);
        wchar_t *dir = str_dup(file);
        if (dir) PathRemoveFileSpecW(dir);
        if (args) {
            INT_PTR result = (INT_PTR)ShellExecuteW(owner, L"open", exe, args, dir, SW_SHOWNORMAL);
            ok = result > 32;
            if (!ok) res_message_boxf(owner, IDS_EDITOR_FAILED, MB_ICONWARNING, exe, args);
        }
        free(dir);
        free(args);
    }
    free(exe);
    return ok;
}
