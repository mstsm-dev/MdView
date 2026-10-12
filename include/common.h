/*
 * common.h — 全モジュール共通の Windows ヘッダ設定と小さなユーティリティ
 *
 * Markdown 変換（md2rtf.h 系）はこのヘッダを読み込まない（Windows 非依存のため）。
 */
#ifndef MDV_COMMON_H
#define MDV_COMMON_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "version.h"
#include "resource.h"

#define APP_NAME L"" MDV_APP_NAME_STRING
#define APP_VERSION L"" MDV_VERSION_STRING

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* ドロップされたファイルを開く（lParam = malloc した wchar_t* パス。受け手が free） */
#define WM_APP_OPENFILE (WM_APP + 1)

/* ---- 文字列（util.c） ---- */

/* wcsdup 相当。s が NULL なら NULL を返す。 */
wchar_t *str_dup(const wchar_t *s);

/* UTF-16 → UTF-8（呼び出し側で free）。確保できなければ NULL。 */
char *utf8_from_wide(const wchar_t *w);

/* UTF-16（長さ指定）→ UTF-8。*outLength にバイト数（終端を除く）。 */
char *utf8_from_wide_n(const wchar_t *w, int length, size_t *outLength);

/* UTF-8 → UTF-16（呼び出し側で free）。確保できなければ NULL。 */
wchar_t *wide_from_utf8(const char *s);

/* ---- 画面（util.c） ---- */

/* 画面の論理 DPI（システム DPI 対応のため全モニタ共通）。 */
int screen_dpi(void);

/* ---- リソース（util.c） ---- */

/* STRINGTABLE の文字列を buffer に読み込む。見つからなければ空文字列。 */
const wchar_t *res_string(UINT id, wchar_t *buffer, int capacity);

/* STRINGTABLE の文字列でメッセージボックスを表示する。 */
int res_message_box(HWND owner, UINT textId, UINT flags);

/* STRINGTABLE の書式文字列（%ls など）に引数を埋めてメッセージボックスを表示する。 */
int res_message_boxf(HWND owner, UINT formatId, UINT flags, ...);

/* RCDATA の内容（終端なし）。見つからなければ NULL。 */
const char *res_data(UINT id, size_t *size);

#endif /* MDV_COMMON_H */
