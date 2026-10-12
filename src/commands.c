/*
 * commands.c — メニュー・アクセラレータ・右クリックメニューのコマンド処理
 */
#include "app.h"
#include "editor.h"
#include "viewer.h"

#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define OPEN_PATH_CAPACITY (MAX_PATH * 2)

/* ------------------------------------------------------------------------
 * 個別コマンド
 * ------------------------------------------------------------------------ */

/* "ラベル\0パターン\0" を追記し、追記後の位置を返す。 */
static wchar_t *append_filter(wchar_t *cursor, const wchar_t *label, const wchar_t *pattern)
{
    size_t labelLen = wcslen(label) + 1;
    size_t patternLen = wcslen(pattern) + 1;
    memcpy(cursor, label, labelLen * sizeof(wchar_t));
    cursor += labelLen;
    memcpy(cursor, pattern, patternLen * sizeof(wchar_t));
    return cursor + patternLen;
}

static wchar_t *build_open_filter(void)
{
    wchar_t markdownLabel[128];
    wchar_t allLabel[128];
    res_string(IDS_FILTER_MARKDOWN, markdownLabel, ARRAY_COUNT(markdownLabel));
    res_string(IDS_FILTER_ALL, allLabel, ARRAY_COUNT(allLabel));

    wchar_t *pattern = textfile_filter_pattern();
    if (!pattern) return NULL;

    size_t length = wcslen(markdownLabel) + wcslen(pattern) + wcslen(allLabel) + wcslen(L"*.*") + 5;
    wchar_t *filter = malloc(length * sizeof(wchar_t));
    if (filter) {
        wchar_t *cursor = append_filter(filter, markdownLabel, pattern);
        cursor = append_filter(cursor, allLabel, L"*.*");
        *cursor = L'\0'; /* フィルタ全体の終端は \0\0 */
    }
    free(pattern);
    return filter;
}

static void open_file(App *a)
{
    wchar_t path[OPEN_PATH_CAPACITY] = L"";
    wchar_t *filter = build_open_filter();
    if (!filter) return;

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = a->hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = OPEN_PATH_CAPACITY;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) app_open(a, path, true);
    free(filter);
}

/* INI の [Editor] で指定したエディタで開く（未指定なら関連付けの「編集」→メモ帳）。editor.c */
static void open_in_editor(App *a)
{
    if (!a->path) return;
    editor_open(a->hwnd, a->cfg.editorPath, a->cfg.editorArgs, a->path);
}

static void show_about(App *a)
{
    res_message_boxf(a->hwnd, IDS_ABOUT_FORMAT, MB_ICONINFORMATION, APP_NAME, APP_VERSION,
                     a->iniPath ? a->iniPath : SETTINGS_FILE_NAME);
}

static void toggle_dark(App *a)
{
    a->cfg.dark = !a->cfg.dark;
    window_apply_theme(a);
    app_render(a, SCROLL_KEEP_TEXT);
}

static void toggle_raw(App *a)
{
    a->rawView = !a->rawView;
    app_render(a, SCROLL_TOP);
}

/* ------------------------------------------------------------------------
 * 公開関数
 * ------------------------------------------------------------------------ */

void commands_execute(App *a, int id)
{
    switch (id) {
    /* ファイル */
    case IDM_OPEN:       open_file(a); break;
    case IDM_RELOAD:     app_reload(a, SCROLL_KEEP_PIXELS); break;
    case IDM_EDIT:       open_in_editor(a); break;
    case IDM_BACK:       app_go_back(a); break;
    case IDM_EXIT:       PostMessageW(a->hwnd, WM_CLOSE, 0, 0); break;

    /* 編集 */
    case IDM_COPY:       SendMessageW(a->edit, WM_COPY, 0, 0); break;
    case IDM_SELALL:     SendMessageW(a->edit, EM_SETSEL, 0, -1); break;
    case IDM_FIND:       find_show(&a->find, a->hwnd, a->edit); break;
    case IDM_FINDNEXT:   find_next(&a->find, a->hwnd, a->edit, true); break;
    case IDM_FINDPREV:   find_next(&a->find, a->hwnd, a->edit, false); break;

    /* 表示 */
    case IDM_ZOOMIN:     app_zoom_step(a, +1); break;
    case IDM_ZOOMOUT:    app_zoom_step(a, -1); break;
    case IDM_ZOOMRESET:  app_zoom_step(a, 0); break;
    case IDM_DARK:       toggle_dark(a); break;
    case IDM_RAW:        toggle_raw(a); break;
    case IDM_AUTORELOAD: a->cfg.autoReload = !a->cfg.autoReload; break;

    /* ヘルプ */
    case IDM_ABOUT:      show_about(a); break;
    }
}

static void check(HMENU menu, int command, bool checked)
{
    CheckMenuItem(menu, (UINT)command, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

static void enable(HMENU menu, int command, bool enabled)
{
    EnableMenuItem(menu, (UINT)command, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

/*
 * メニューを開く直前に、現在の状態を示す。
 * 灰色表示は右クリックメニューだけに行う。メインメニューの項目を灰色にすると、
 * 同じコマンドのショートカット（Alt+← など）まで TranslateAccelerator に無視されるため。
 */
void commands_update_menu(App *a, HMENU menu)
{
    check(menu, IDM_DARK, a->cfg.dark);
    check(menu, IDM_RAW, a->rawView);
    check(menu, IDM_AUTORELOAD, a->cfg.autoReload);

    if (menu == a->popupMenu) {
        enable(menu, IDM_COPY, viewer_has_selection(a->edit));
        enable(menu, IDM_BACK, !history_empty(&a->history));
    }
}
