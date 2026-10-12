/*
 * app.c — 文書を開く・再読み込み・戻る・表示・倍率・リンク
 *
 * 表示の流れ
 *   app_render() で現在の設定（倍率・テーマ・フォント・表示幅）から RTF を作り直し、
 *   viewer_show_rtf() で RichEdit に流し込む。スクロール位置は ScrollKeep で決める。
 *   倍率・テーマ・幅の変更は本文が同じなので「表示先頭の文字」を、再読み込みは
 *   本文が変わるので「スクロール量」を維持する。
 */
#include "app.h"
#include "links.h"
#include "viewer.h"

#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* Ctrl+ホイール・Ctrl++/- で切り替える倍率（%） */
static const int ZOOM_STEPS[] = { 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300 };

/* ------------------------------------------------------------------------
 * 表示
 * ------------------------------------------------------------------------ */

static COLORREF theme_background(bool dark)
{
    unsigned bg = md_theme_background(dark ? 1 : 0);
    return RGB((bg >> 16) & 255, (bg >> 8) & 255, bg & 255);
}

void app_update_title(App *a)
{
    wchar_t zoom[32] = L"";
    wchar_t source[32] = L"";
    if (a->cfg.zoom != 100) {
        wchar_t format[32];
        swprintf(zoom, ARRAY_COUNT(zoom), res_string(IDS_TITLE_ZOOM, format, ARRAY_COUNT(format)), a->cfg.zoom);
    }
    if (a->rawView) res_string(IDS_TITLE_SOURCE, source, ARRAY_COUNT(source));

    wchar_t title[MAX_PATH * 2 + 64];
    if (a->path)
        swprintf(title, ARRAY_COUNT(title), L"%ls%ls%ls - %ls", PathFindFileNameW(a->path), source, zoom, APP_NAME);
    else
        swprintf(title, ARRAY_COUNT(title), L"%ls%ls", APP_NAME, zoom);
    SetWindowTextW(a->hwnd, title);
}

void app_render(App *a, ScrollKeep keep)
{
    ViewerScroll scroll = viewer_save_scroll(a->edit, keep);

    wchar_t imageLabel[32];
    char *fontBody = utf8_from_wide(a->cfg.fontBody);
    char *fontCode = utf8_from_wide(a->cfg.fontCode);
    char *label = utf8_from_wide(res_string(IDS_IMAGE_LABEL, imageLabel, ARRAY_COUNT(imageLabel)));

    MdOptions options;
    options.scale_pct   = a->cfg.zoom;
    options.width_twips = viewer_width_twips(a->edit, screen_dpi());
    options.dark        = a->cfg.dark ? 1 : 0;
    options.font_body   = fontBody ? fontBody : "Yu Gothic UI";
    options.font_code   = fontCode ? fontCode : "Consolas";
    options.body_halfpt = a->cfg.fontSize * 2;
    options.image_label = (label && label[0]) ? label : NULL;

    /* ファイル未指定ならリソースの初期表示用文書（IDR_WELCOME）を表示する */
    const char *src = a->source;
    size_t length = a->sourceLength;
    if (!src) src = res_data(IDR_WELCOME, &length);
    if (!src) { src = ""; length = 0; }

    md_doc_free(&a->doc);
    if (a->rawView && a->source) text_to_rtf(src, length, &options, &a->doc);
    else md_to_rtf(src, length, &options, &a->doc);
    free(fontBody);
    free(fontCode);
    free(label);

    viewer_show_rtf(a->edit, a->doc.rtf, a->doc.rtf_len, theme_background(a->cfg.dark), &scroll);
    a->lastWidth = options.width_twips;
    app_update_title(a);
}

/* ------------------------------------------------------------------------
 * 開く・再読み込み・戻る
 * ------------------------------------------------------------------------ */

static wchar_t *full_path(const wchar_t *path)
{
    DWORD n = GetFullPathNameW(path, 0, NULL, NULL);
    wchar_t *full = n ? malloc((size_t)n * sizeof(wchar_t)) : NULL;
    if (full && GetFullPathNameW(path, n, full, NULL) && full[0]) return full;
    free(full);
    return str_dup(path);
}

bool app_open(App *a, const wchar_t *path, bool pushHistory)
{
    wchar_t *full = full_path(path);
    if (!full) return false;

    char *text;
    size_t length;
    FileStamp stamp;
    if (!textfile_read(full, &text, &length, &stamp)) {
        res_message_boxf(a->hwnd, IDS_OPEN_FAILED, MB_ICONWARNING, full);
        free(full);
        return false;
    }
    if (pushHistory && a->path) history_push(&a->history, a->path, viewer_first_visible_cp(a->edit));

    free(a->source);
    free(a->path);
    a->source = text;
    a->sourceLength = length;
    a->path = full;
    a->stamp = stamp;
    app_render(a, SCROLL_TOP);
    return true;
}

void app_reload(App *a, ScrollKeep keep)
{
    if (!a->path) {
        app_render(a, SCROLL_KEEP_TEXT);
        return;
    }
    char *text;
    size_t length;
    FileStamp stamp;
    if (!textfile_read(a->path, &text, &length, &stamp)) return; /* 保存途中などで読めなければ次回に任せる */
    free(a->source);
    a->source = text;
    a->sourceLength = length;
    a->stamp = stamp;
    app_render(a, keep);
}

void app_go_back(App *a)
{
    HistoryEntry entry;
    if (!history_pop(&a->history, &entry)) {
        MessageBeep(MB_OK);
        return;
    }
    if (app_open(a, entry.path, false)) viewer_scroll_to_cp(a->edit, entry.cp);
    free(entry.path);
}

void app_check_file_changed(App *a)
{
    if (!a->cfg.autoReload || !a->path) return;
    if (textfile_changed(a->path, &a->stamp)) app_reload(a, SCROLL_KEEP_PIXELS);
}

/* ------------------------------------------------------------------------
 * 倍率
 * ------------------------------------------------------------------------ */

void app_zoom_step(App *a, int direction)
{
    int n = (int)ARRAY_COUNT(ZOOM_STEPS);
    int zoom = a->cfg.zoom;
    if (direction == 0) {
        zoom = 100;
    } else if (direction > 0) {
        for (int i = 0; i < n; i++) if (ZOOM_STEPS[i] > a->cfg.zoom) { zoom = ZOOM_STEPS[i]; break; }
    } else {
        for (int i = n - 1; i >= 0; i--) if (ZOOM_STEPS[i] < a->cfg.zoom) { zoom = ZOOM_STEPS[i]; break; }
    }
    if (zoom == a->cfg.zoom) return;
    a->cfg.zoom = zoom;
    app_render(a, SCROLL_KEEP_TEXT);
}

/* ------------------------------------------------------------------------
 * リンク（仕様書 4.2 節）
 * ------------------------------------------------------------------------ */

static void jump_anchor(App *a, const char *fragment)
{
    LONG cp;
    if (!link_find_anchor(a->edit, &a->doc, fragment, &cp)) {
        MessageBeep(MB_OK);
        return;
    }
    if (cp >= 0) viewer_scroll_to_cp(a->edit, cp);
}

static void shell_open(App *a, const wchar_t *target)
{
    ShellExecuteW(a->hwnd, L"open", target, NULL, NULL, SW_SHOWNORMAL);
}

static void follow_local(App *a, const wchar_t *url)
{
    LocalLink link;
    if (!link_resolve_local(url, a->path, &link)) return;

    if (link.emptyPath && link.fragment) {
        jump_anchor(a, link.fragment);
    } else if (!PathFileExistsW(link.path)) {
        res_message_boxf(a->hwnd, IDS_FILE_NOT_FOUND, MB_ICONWARNING, link.path);
    } else if (textfile_is_markdown(link.path)) {
        if (app_open(a, link.path, true) && link.fragment && link.fragment[0]) jump_anchor(a, link.fragment);
    } else if (res_message_boxf(a->hwnd, IDS_CONFIRM_FILE, MB_YESNO | MB_ICONQUESTION, link.path) == IDYES) {
        /* 実行ファイルなどを不用意に起動しないよう、Markdown 以外は確認してから開く */
        shell_open(a, link.path);
    }
    link_local_free(&link);
}

void app_follow_link(App *a, const wchar_t *url)
{
    if (!url[0]) return;

    switch (link_classify(url)) {
    case LINK_ANCHOR: {
        char *fragment = utf8_from_wide(url + 1);
        if (fragment) {
            jump_anchor(a, fragment);
            free(fragment);
        }
        break;
    }
    case LINK_WEB:
        shell_open(a, url);
        break;
    case LINK_OTHER_SCHEME:
        if (res_message_boxf(a->hwnd, IDS_CONFIRM_LINK, MB_YESNO | MB_ICONQUESTION, url) == IDYES)
            shell_open(a, url);
        break;
    case LINK_WWW: {
        wchar_t web[2100];
        swprintf(web, ARRAY_COUNT(web), L"http://%.2000ls", url);
        shell_open(a, web);
        break;
    }
    case LINK_LOCAL:
        follow_local(a, url);
        break;
    }
}
