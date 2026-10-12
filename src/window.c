/*
 * window.c — メインウィンドウ（メッセージ処理・RichEdit のサブクラス・レイアウト・タイマー・
 *            ドロップ・右クリックメニュー・ウィンドウ位置・テーマ）
 *
 * キーボード操作は resource.rc のアクセラレータ経由で WM_COMMAND として届き、
 * commands.c で処理する。ここではウィンドウ状態と、RichEdit が先に受け取ってしまう
 * 入力（Ctrl+ホイール・右クリック・BackSpace）だけを扱う。
 */
#include "app.h"
#include "droptarget.h"
#include "links.h"
#include "viewer.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <stdlib.h>
#include <windowsx.h>

static const wchar_t WINDOW_CLASS[] = L"MdViewWindow";

enum {
    TID_RESIZE = 1, /* サイズ変更が落ち着いてから表・コードブロックの幅を作り直す */
    TID_WATCH  = 2  /* ファイルの変更検知（自動再読み込み） */
};
#define RESIZE_DELAY_MS 150
#define WATCH_INTERVAL_MS 1000

#define DEFAULT_WIDTH 900   /* 初回起動時のウィンドウサイズ（96 DPI 基準の px） */
#define DEFAULT_HEIGHT 1000

/* ------------------------------------------------------------------------
 * テーマ・レイアウト
 * ------------------------------------------------------------------------ */

/*
 * タイトルバーの明暗。DWMWA_USE_IMMERSIVE_DARK_MODE は Windows 10 20H1 以降が 20、
 * 1809〜1909 が 19。それより前の Windows では両方失敗し、何も変わらない。
 */
#define DWMWA_DARK_MODE        20
#define DWMWA_DARK_MODE_LEGACY 19

void window_apply_theme(App *a)
{
    BOOL dark = a->cfg.dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(a->hwnd, DWMWA_DARK_MODE, &dark, sizeof(dark))))
        DwmSetWindowAttribute(a->hwnd, DWMWA_DARK_MODE_LEGACY, &dark, sizeof(dark));
}

static void on_size(App *a)
{
    RECT client;
    GetClientRect(a->hwnd, &client);
    viewer_layout(a->edit, client.right, client.bottom, screen_dpi());
    SetTimer(a->hwnd, TID_RESIZE, RESIZE_DELAY_MS, NULL);
}

static void on_timer(App *a, UINT_PTR id)
{
    if (id == TID_RESIZE) {
        KillTimer(a->hwnd, TID_RESIZE);
        if (viewer_width_twips(a->edit, screen_dpi()) != a->lastWidth) app_render(a, SCROLL_KEEP_TEXT);
    } else if (id == TID_WATCH) {
        app_check_file_changed(a);
    }
}

/* ------------------------------------------------------------------------
 * ウィンドウ位置の保存・復元
 * ------------------------------------------------------------------------ */

void window_restore_placement(App *a, int showCommand)
{
    if (a->cfg.hasPlacement) {
        WINDOWPLACEMENT wp = a->cfg.placement;
        if (wp.showCmd != SW_SHOWMAXIMIZED) wp.showCmd = SW_SHOWNORMAL;
        if (showCommand == SW_SHOWMINIMIZED || showCommand == SW_MINIMIZE || showCommand == SW_SHOWMINNOACTIVE)
            wp.showCmd = (UINT)showCommand;
        SetWindowPlacement(a->hwnd, &wp);
    } else {
        ShowWindow(a->hwnd, showCommand);
    }
    UpdateWindow(a->hwnd);
}

void window_save_placement(App *a)
{
    WINDOWPLACEMENT wp = { .length = sizeof(wp) };
    if (GetWindowPlacement(a->hwnd, &wp)) {
        a->cfg.placement = wp;
        a->cfg.hasPlacement = true;
    }
}

/* ------------------------------------------------------------------------
 * 入力：右クリック・ドロップ・リンク
 * ------------------------------------------------------------------------ */

void window_show_context_menu(App *a, int x, int y)
{
    if (x == -1 && y == -1) { /* キーボード（Shift+F10 等）から */
        RECT r;
        GetWindowRect(a->edit, &r);
        x = r.left + 20;
        y = r.top + 20;
    }
    /* 灰色・チェックの表示は WM_INITMENUPOPUP → commands_update_menu で行う */
    TrackPopupMenu(a->popupMenu, TPM_RIGHTBUTTON, x, y, 0, a->hwnd, NULL);
}

/* WM_DROPFILES / EN_DROPFILES（OLE ドロップターゲットを登録できなかったときの経路） */
static void on_drop_files(App *a, HDROP drop)
{
    wchar_t path[MAX_PATH * 2];
    if (DragQueryFileW(drop, 0, path, ARRAY_COUNT(path))) {
        SetForegroundWindow(a->hwnd);
        app_open(a, path, true);
    }
    DragFinish(drop);
}

/* droptarget.c からの通知 */
static void on_open_file(App *a, wchar_t *path)
{
    if (!path) return;
    SetForegroundWindow(a->hwnd);
    app_open(a, path, true);
    free(path);
}

static LRESULT on_notify(App *a, const NMHDR *header)
{
    if (header->hwndFrom != a->edit) return 0;
    if (header->code == EN_LINK) {
        wchar_t *url = link_target_from_click(a->edit, (const ENLINK *)header, &a->doc);
        if (url) {
            app_follow_link(a, url);
            free(url);
        }
        return 0;
    }
    if (header->code == EN_DROPFILES) {
        on_drop_files(a, (HDROP)((const ENDROPFILES *)header)->hDrop);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * RichEdit のサブクラス
 * ------------------------------------------------------------------------ */

static LRESULT CALLBACK viewer_subclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                        UINT_PTR id, DWORD_PTR ref)
{
    App *a = (App *)ref;
    switch (message) {
    case WM_MOUSEWHEEL: /* Ctrl+ホイール：RichEdit 自身のズームではなく、RTF を作り直して拡大縮小する */
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
            app_zoom_step(a, GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1 : -1);
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        window_show_context_menu(a, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_BACK) {
            app_go_back(a);
            return 0;
        }
        break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

/* ------------------------------------------------------------------------
 * ウィンドウプロシージャ
 * ------------------------------------------------------------------------ */

static bool on_create(App *a, HWND hwnd)
{
    a->hwnd = hwnd;
    a->edit = viewer_create(hwnd, a->instance, IDC_VIEWER);
    if (!a->edit) return false;
    SetWindowSubclass(a->edit, viewer_subclass, 1, (DWORD_PTR)a);
    return true;
}

/* 本文と枠（メニューバーなど）の両方でドロップを受け付ける */
static void register_drop_targets(App *a)
{
    bool editOk = droptarget_register(a->edit, a->hwnd);
    bool windowOk = droptarget_register(a->hwnd, a->hwnd);
    if (!editOk || !windowOk) { /* 失敗時は従来の WM_DROPFILES で受ける */
        DragAcceptFiles(a->hwnd, TRUE);
        if (!editOk) DragAcceptFiles(a->edit, TRUE);
    }
}

static void on_close(App *a)
{
    window_save_placement(a);
    settings_save(&a->cfg);
    DestroyWindow(a->hwnd);
}

static void on_destroy(App *a)
{
    KillTimer(a->hwnd, TID_WATCH);
    droptarget_revoke(a->edit);
    droptarget_revoke(a->hwnd);
    SetMenu(a->hwnd, NULL); /* メニューは main.c で破棄する */
    PostQuitMessage(0);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    App *a = (App *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        a = ((CREATESTRUCTW *)lParam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)a);
    }
    if (!a) return DefWindowProcW(hwnd, message, wParam, lParam);
    if (find_handle_message(&a->find, hwnd, a->edit, message, lParam)) return 0;

    switch (message) {
    case WM_CREATE:
        return on_create(a, hwnd) ? 0 : -1;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && a->edit) on_size(a);
        return 0;

    case WM_TIMER:
        on_timer(a, wParam);
        return 0;

    case WM_SETFOCUS:
        SetFocus(a->edit);
        return 0;

    case WM_NOTIFY:
        return on_notify(a, (const NMHDR *)lParam);

    case WM_DROPFILES:
        on_drop_files(a, (HDROP)wParam);
        return 0;

    case WM_APP_OPENFILE:
        on_open_file(a, (wchar_t *)lParam);
        return 0;

    case WM_COMMAND:
        commands_execute(a, LOWORD(wParam));
        return 0;

    case WM_INITMENUPOPUP:
        commands_update_menu(a, (HMENU)wParam);
        return 0;

    case WM_APPCOMMAND: /* マウスの「戻る」ボタン */
        if (GET_APPCOMMAND_LPARAM(lParam) == APPCOMMAND_BROWSER_BACKWARD) {
            app_go_back(a);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        on_close(a);
        return 0;

    case WM_DESTROY:
        on_destroy(a);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool window_register_class(HINSTANCE instance)
{
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIcon = LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                          GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    return RegisterClassExW(&wc) != 0;
}

HWND window_create(App *a)
{
    int dpi = screen_dpi();
    HWND hwnd = CreateWindowExW(0, WINDOW_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                MulDiv(DEFAULT_WIDTH, dpi, 96), MulDiv(DEFAULT_HEIGHT, dpi, 96),
                                NULL, a->mainMenu, a->instance, a);
    if (!hwnd) return NULL;
    register_drop_targets(a);
    SetTimer(hwnd, TID_WATCH, WATCH_INTERVAL_MS, NULL);
    return hwnd;
}
