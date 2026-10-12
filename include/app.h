/*
 * app.h — アプリケーション全体の状態と、モジュール間をつなぐ操作
 *
 * 担当ファイル
 *   main.c     … 起動・終了処理、メッセージループ
 *   window.c   … ウィンドウプロシージャ、RichEdit のサブクラス、ステータスバー、レイアウト、
 *                タイマー、ドロップ、右クリックメニュー、ウィンドウ位置・テーマ
 *   commands.c … メニュー／アクセラレータのコマンド処理、メニューの状態表示
 *   app.c      … 文書を開く・再読み込み・戻る・表示（RTF 生成と流し込み）・倍率・リンク
 */
#ifndef MDV_APP_H
#define MDV_APP_H

#include "common.h"
#include "find.h"
#include "history.h"
#include "md2rtf.h"
#include "settings.h"
#include "textfile.h"
#include "types.h"

typedef struct {
    /* ---- Win32 資源 ---- */
    HINSTANCE instance;
    HWND hwnd;
    HWND edit;           /* 本文の RichEdit（viewer.c） */
    HWND statusBar;      /* ファイルサイズ・行数 */
    HMENU mainMenu;
    HMENU popupMenuRoot; /* LoadMenu の戻り値（破棄用） */
    HMENU popupMenu;     /* 実際に表示するサブメニュー */
    HACCEL accelerators;

    /* ---- 設定 ---- */
    Settings cfg;        /* 実行中の値。終了時に INI へ保存 */
    wchar_t *iniPath;

    /* ---- 文書 ---- */
    wchar_t *path;       /* 表示中のファイル（フルパス）。NULL なら初期表示の文書 */
    char *source;        /* ファイル内容（UTF-8） */
    size_t sourceLength;
    FileStamp stamp;     /* 自動再読み込みの変更検知用。stamp.size はステータスバーにも使う */
    size_t lineCount;    /* ステータスバー表示用 */
    MdDoc doc;           /* 最後に生成した RTF とリンク・見出しの一覧 */
    bool rawView;        /* ソース表示中（保存しない） */
    int lastWidth;       /* 最後に生成したときの表示幅（twips）。幅が変わったら作り直す */

    History history;
    FindState find;
} App;

/* ---- app.c ---- */
void app_render(App *app, ScrollKeep keep);              /* RTF を作り直して表示する */
bool app_open(App *app, const wchar_t *path, bool pushHistory);
void app_reload(App *app, ScrollKeep keep);               /* 失敗時は何もしない */
void app_go_back(App *app);
void app_check_file_changed(App *app);                    /* 自動再読み込み（タイマーから） */
void app_zoom_step(App *app, int direction);              /* +1: 拡大、-1: 縮小、0: 100% */
void app_follow_link(App *app, const wchar_t *url);
void app_update_title(App *app);
void app_update_status(App *app);                         /* ファイルサイズ・行数 */
COLORREF app_background(const App *app);                  /* テーマの背景色 */

/* ---- window.c ---- */
bool window_register_class(HINSTANCE instance);
HWND window_create(App *app);
void window_restore_placement(App *app, int showCommand);
void window_save_placement(App *app);                     /* 現在の位置・サイズを cfg へ */
void window_apply_theme(App *app);                        /* タイトルバーの明暗 */
void window_show_context_menu(App *app, int x, int y);    /* スクリーン座標。(-1,-1) はキーボードから */

/* ---- commands.c ---- */
void commands_execute(App *app, int commandId);
void commands_update_menu(App *app, HMENU menu);

#endif /* MDV_APP_H */
