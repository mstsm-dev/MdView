/*
 * viewer.h — 本文表示用 RichEdit（Msftedit.dll / RICHEDIT50W）の薄いラッパー
 *
 * RichEdit へのメッセージ送信はこのモジュールに集約する。App には依存しない。
 */
#ifndef MDV_VIEWER_H
#define MDV_VIEWER_H

#include "common.h"
#include "types.h"

#include <commdlg.h>  /* viewer_find の FR_DOWN / FR_MATCHCASE / FR_WHOLEWORD */
#include <richedit.h>

/* 再表示前に保存したスクロール位置 */
typedef struct {
    ScrollKeep mode;
    LONG cp;     /* SCROLL_KEEP_TEXT：表示先頭の文字位置 */
    POINT pos;   /* SCROLL_KEEP_PIXELS：スクロール量     */
} ViewerScroll;

/* Msftedit.dll を読み込む。失敗なら false。 */
bool viewer_load_library(void);

/* 読み取り専用の RichEdit を作る（リンク通知・ドロップ通知・高度な文字組みを有効化）。 */
HWND viewer_create(HWND parent, HINSTANCE instance, UINT id);

/* 親のクライアント全体に広げ、本文の余白（DPI に比例）を設定する。 */
void viewer_layout(HWND edit, int width, int height, int dpi);

/* 表・コードブロックに使える幅（twips）。 */
int viewer_width_twips(HWND edit, int dpi);

/* 再表示の前に現在の位置を記録する。 */
ViewerScroll viewer_save_scroll(HWND edit, ScrollKeep mode);

/* RTF を流し込み、背景色を設定して、scroll に従って位置を戻す（再描画は1回）。 */
void viewer_show_rtf(HWND edit, const char *rtf, size_t length, COLORREF background, const ViewerScroll *scroll);

/* 文字位置 cp が表示領域の先頭に来るようにスクロールする。 */
void viewer_scroll_to_cp(HWND edit, LONG cp);

/* 表示領域の先頭にある文字位置。 */
LONG viewer_first_visible_cp(HWND edit);

/* 選択範囲 */
CHARRANGE viewer_selection(HWND edit);
bool viewer_has_selection(HWND edit);
void viewer_select(HWND edit, CHARRANGE range); /* 選択してキャレット位置までスクロール */

/* 範囲の文字列（呼び出し側で free）。失敗なら NULL。 */
wchar_t *viewer_text_range(HWND edit, CHARRANGE range);

/* 文字数 */
LONG viewer_text_length(HWND edit);

/* EM_FINDTEXTEXW。見つかった開始位置（なければ -1）、*found に範囲。flags は FR_DOWN 等。 */
LONG viewer_find(HWND edit, const wchar_t *text, CHARRANGE range, DWORD flags, CHARRANGE *found);

/* cp が行頭か（折り返しを含む表示行の先頭）。 */
bool viewer_is_line_start(HWND edit, LONG cp);

#endif /* MDV_VIEWER_H */
