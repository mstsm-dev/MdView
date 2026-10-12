/*
 * main.c — MdView のエントリーポイント
 *
 * 起動順：OLE・コモンコントロール初期化 → RichEdit 読み込み → 設定（INI）読み込み
 *         → リソース読み込み → ウィンドウ作成 → タイトルバーのテーマ → ウィンドウ表示
 *         → コマンドライン第1引数のファイル（なければ初期表示の文書）→ メッセージループ
 * 終了時：WM_CLOSE（window.c）で位置と設定を INI へ保存、ここで残りを解放。
 */
#include "app.h"

#include <commctrl.h>
#include <ole2.h>
#include <shellapi.h>
#include <stdlib.h>

#include "viewer.h"

static App g_app;

static bool load_ui_resources(App *a)
{
    a->mainMenu = LoadMenuW(a->instance, MAKEINTRESOURCEW(IDR_MAINMENU));
    a->popupMenuRoot = LoadMenuW(a->instance, MAKEINTRESOURCEW(IDR_POPUPMENU));
    a->popupMenu = a->popupMenuRoot ? GetSubMenu(a->popupMenuRoot, 0) : NULL;
    a->accelerators = LoadAcceleratorsW(a->instance, MAKEINTRESOURCEW(IDR_ACCELERATORS));
    return a->mainMenu && a->popupMenu && a->accelerators;
}

/* ウィンドウ破棄後に残る資源を解放する。途中で失敗した場合も呼べる。 */
static void release_state(App *a)
{
    md_doc_free(&a->doc);
    history_clear(&a->history);
    if (a->mainMenu) DestroyMenu(a->mainMenu);
    if (a->popupMenuRoot) DestroyMenu(a->popupMenuRoot);
    /* アクセラレータテーブルはリソースから読んだものなので解放不要 */
    free(a->source);
    free(a->path);
    free(a->iniPath);
}

/* コマンドライン第1引数（開くファイルのパス）を返す。呼び出し側で free。 */
static wchar_t *command_line_path(void)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t *path = (argv && argc > 1) ? str_dup(argv[1]) : NULL;
    if (argv) LocalFree(argv);
    return path;
}

static int run_message_loop(App *a)
{
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (find_translate(&a->find, &msg)) continue;
        if (TranslateAcceleratorW(a->hwnd, a->accelerators, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int showCommand)
{
    (void)previous;
    (void)commandLine; /* Unicode の引数は CommandLineToArgvW で取得する */

    App *a = &g_app;
    a->instance = instance;

    HRESULT ole = OleInitialize(NULL); /* RegisterDragDrop に必要 */
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&controls);

    int exitCode = 1;
    if (!viewer_load_library()) {
        res_message_box(NULL, IDS_RICHEDIT_ERROR, MB_ICONERROR);
        goto done;
    }
    find_init(&a->find);
    a->iniPath = settings_default_path();
    settings_load(&a->cfg, a->iniPath);

    if (!load_ui_resources(a) || !window_register_class(instance) || !window_create(a)) {
        res_message_box(NULL, IDS_INIT_ERROR, MB_OK | MB_ICONERROR);
        goto done;
    }
    window_apply_theme(a);
    window_restore_placement(a, showCommand);

    wchar_t *initialPath = command_line_path();
    if (!initialPath || !app_open(a, initialPath, false)) {
        if (!a->path) app_render(a, SCROLL_TOP); /* 開けなければ初期表示の文書 */
    }
    free(initialPath);

    exitCode = run_message_loop(a);

done:
    release_state(a);
    if (SUCCEEDED(ole)) OleUninitialize();
    return exitCode;
}
