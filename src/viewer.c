/*
 * viewer.c — 本文表示用 RichEdit のラッパー
 *
 * RichEdit 4.1 以降（Msftedit.dll）の機能を使う：RTF の HYPERLINK フィールド（EN_LINK）、
 * 表（\trowd）、セルの背景色、EM_FINDTEXTEXW、EM_GETSCROLLPOS / EM_SETSCROLLPOS。
 */
#include "viewer.h"

#include <stdlib.h>
#include <string.h>

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif
#ifndef EM_SETTYPOGRAPHYOPTIONS
#define EM_SETTYPOGRAPHYOPTIONS (WM_USER + 202)
#endif
#ifndef TO_ADVANCEDTYPOGRAPHY
#define TO_ADVANCEDTYPOGRAPHY 1
#endif

#define VIEWER_PADDING 20       /* 本文の左右余白（96 DPI 基準の px）。上は 1/2、下は 1/4 */
#define VIEWER_WIDTH_MARGIN 120 /* 表の幅に残す余裕（twips） */

/* ------------------------------------------------------------------------
 * 作成・レイアウト
 * ------------------------------------------------------------------------ */

bool viewer_load_library(void)
{
    return LoadLibraryW(L"Msftedit.dll") != NULL;
}

HWND viewer_create(HWND parent, HINSTANCE instance, UINT id)
{
    /* ES_DISABLENOSCROLL：スクロールバーを常に出し、出入りで表示幅が変わらないようにする */
    HWND edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                ES_AUTOVSCROLL | ES_NOHIDESEL | ES_DISABLENOSCROLL,
                                0, 0, 0, 0, parent, (HMENU)(UINT_PTR)id, instance, NULL);
    if (!edit) return NULL;
    SendMessageW(edit, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);  /* 既定の 32K 文字制限を外す */
    SendMessageW(edit, EM_SETEVENTMASK, 0, ENM_LINK | ENM_DROPFILES);
    SendMessageW(edit, EM_AUTOURLDETECT, FALSE, 0);     /* リンクは RTF のフィールドで指定する */
    SendMessageW(edit, EM_SETUNDOLIMIT, 0, 0);
    SendMessageW(edit, EM_SETTYPOGRAPHYOPTIONS, TO_ADVANCEDTYPOGRAPHY, TO_ADVANCEDTYPOGRAPHY);
    return edit;
}

void viewer_layout(HWND edit, int width, int height, int dpi)
{
    MoveWindow(edit, 0, 0, width, height, TRUE);
    int pad = MulDiv(VIEWER_PADDING, dpi, 96);
    RECT r;
    GetClientRect(edit, &r);
    r.left += pad;
    r.top += pad / 2;
    r.right -= pad;
    r.bottom -= pad / 4;
    if (r.right <= r.left) r.right = r.left + 1;
    if (r.bottom <= r.top) r.bottom = r.top + 1;
    SendMessageW(edit, EM_SETRECT, 0, (LPARAM)&r);
}

static RECT format_rect(HWND edit)
{
    RECT r;
    SendMessageW(edit, EM_GETRECT, 0, (LPARAM)&r);
    return r;
}

int viewer_width_twips(HWND edit, int dpi)
{
    RECT r = format_rect(edit);
    int px = r.right - r.left;
    if (px < 100) px = 100;
    return MulDiv(px, 1440, dpi) - VIEWER_WIDTH_MARGIN;
}

/* ------------------------------------------------------------------------
 * スクロール
 * ------------------------------------------------------------------------ */

LONG viewer_first_visible_cp(HWND edit)
{
    RECT r = format_rect(edit);
    POINTL pt = { r.left + 1, r.top + 1 };
    return (LONG)SendMessageW(edit, EM_CHARFROMPOS, 0, (LPARAM)&pt);
}

void viewer_scroll_to_cp(HWND edit, LONG cp)
{
    RECT r = format_rect(edit);
    POINTL p = { 0, 0 };
    POINT scroll = { 0, 0 };
    SendMessageW(edit, EM_POSFROMCHAR, (WPARAM)&p, cp);
    SendMessageW(edit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    scroll.y += p.y - r.top;
    if (scroll.y < 0) scroll.y = 0;
    SendMessageW(edit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
}

ViewerScroll viewer_save_scroll(HWND edit, ScrollKeep mode)
{
    ViewerScroll s = { mode, 0, { 0, 0 } };
    if (mode == SCROLL_KEEP_TEXT) s.cp = viewer_first_visible_cp(edit);
    else if (mode == SCROLL_KEEP_PIXELS) SendMessageW(edit, EM_GETSCROLLPOS, 0, (LPARAM)&s.pos);
    return s;
}

/* ------------------------------------------------------------------------
 * 表示
 * ------------------------------------------------------------------------ */

typedef struct {
    const char *p;
    size_t left;
} StreamSource;

static DWORD CALLBACK stream_callback(DWORD_PTR cookie, LPBYTE buffer, LONG capacity, LONG *written)
{
    StreamSource *s = (StreamSource *)cookie;
    size_t n = s->left < (size_t)capacity ? s->left : (size_t)capacity;
    memcpy(buffer, s->p, n);
    s->p += n;
    s->left -= n;
    *written = (LONG)n;
    return 0;
}

void viewer_show_rtf(HWND edit, const char *rtf, size_t length, COLORREF background, const ViewerScroll *scroll)
{
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(edit, EM_SETBKGNDCOLOR, 0, background);

    StreamSource source = { rtf, length };
    EDITSTREAM stream = { (DWORD_PTR)&source, 0, stream_callback };
    SendMessageW(edit, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
    SendMessageW(edit, EM_SETSEL, 0, 0);

    if (scroll->mode == SCROLL_KEEP_TEXT) {
        viewer_scroll_to_cp(edit, scroll->cp);
    } else {
        POINT pos = scroll->mode == SCROLL_KEEP_PIXELS ? scroll->pos : (POINT){ 0, 0 };
        SendMessageW(edit, EM_SETSCROLLPOS, 0, (LPARAM)&pos);
    }
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, NULL, TRUE);
}

/* ------------------------------------------------------------------------
 * 選択・文字列・検索
 * ------------------------------------------------------------------------ */

CHARRANGE viewer_selection(HWND edit)
{
    CHARRANGE r;
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&r);
    return r;
}

bool viewer_has_selection(HWND edit)
{
    CHARRANGE r = viewer_selection(edit);
    return r.cpMin != r.cpMax;
}

void viewer_select(HWND edit, CHARRANGE range)
{
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&range);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

wchar_t *viewer_text_range(HWND edit, CHARRANGE range)
{
    LONG length = range.cpMax - range.cpMin;
    if (length < 0) return NULL;
    wchar_t *text = calloc((size_t)length + 2, sizeof(wchar_t));
    if (!text) return NULL;
    TEXTRANGEW tr;
    tr.chrg = range;
    tr.lpstrText = text;
    SendMessageW(edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    return text;
}

LONG viewer_text_length(HWND edit)
{
    return GetWindowTextLengthW(edit);
}

LONG viewer_find(HWND edit, const wchar_t *text, CHARRANGE range, DWORD flags, CHARRANGE *found)
{
    FINDTEXTEXW ft;
    ft.chrg = range;
    ft.lpstrText = text;
    LONG cp = (LONG)SendMessageW(edit, EM_FINDTEXTEXW, flags, (LPARAM)&ft);
    if (cp >= 0 && found) *found = ft.chrgText;
    return cp;
}

bool viewer_is_line_start(HWND edit, LONG cp)
{
    LONG line = (LONG)SendMessageW(edit, EM_EXLINEFROMCHAR, 0, cp);
    return (LONG)SendMessageW(edit, EM_LINEINDEX, (WPARAM)line, 0) == cp;
}
