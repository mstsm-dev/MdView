/*
 * settings.h — INI 設定の保持・読み込み・保存
 *
 * INI のキーと Settings のフィールドの対応は settings.c の KEY_TABLE に
 * 一覧で定義している。項目を追加するときはフィールドと表の1行を足す。
 */
#ifndef MDV_SETTINGS_H
#define MDV_SETTINGS_H

#include "common.h"

#define SETTINGS_FILE_NAME L"MdView.ini"
#define SETTINGS_FONT_NAME_MAX 64      /* フォント名の最大文字数（終端を含む） */
#define SETTINGS_EDITOR_PATH_MAX 1024  /* エディタのパスの最大文字数（終端を含む） */
#define SETTINGS_EDITOR_ARGS_MAX 1024  /* エディタの引数の最大文字数（終端を含む。PATH_MAX 以下にすること） */
#define SETTINGS_EDITOR_DEFAULT_ARGS L"\"%1\""

typedef struct {
    /* [Window] */
    int x, y;                                 /* 通常時の位置（CW_USEDEFAULT なら OS 任せ） */
    int width, height;                        /* 通常時のサイズ（0 なら既定値を DPI に合わせて使う） */
    bool maximized;
    bool showStatusBar;

    /* [Display] */
    bool dark;                                /* Theme=Dark */
    int zoom;                                 /* 倍率 %（50〜300） */
    int fontSize;                             /* 本文 pt（6〜40） */
    wchar_t fontBody[SETTINGS_FONT_NAME_MAX]; /* 本文フォント */
    wchar_t fontCode[SETTINGS_FONT_NAME_MAX]; /* 等幅フォント */

    /* [File] */
    bool autoReload;                          /* 変更時に自動再読み込み */

    /* [Editor]（「エディタで開く」。editor.c） */
    wchar_t editorPath[SETTINGS_EDITOR_PATH_MAX]; /* エディタのフルパス。空なら関連付けの「編集」→メモ帳 */
    wchar_t editorArgs[SETTINGS_EDITOR_ARGS_MAX]; /* 引数。%1 を文書のフルパスに置き換える。既定 "%1" */
} Settings;

void settings_set_defaults(Settings *s);

/* EXE と同じフォルダの INI パスを返す（呼び出し側で free）。 */
wchar_t *settings_default_path(void);

/* 既定値を設定したうえで INI を読み込む。ファイルがなければ既定値のまま。 */
void settings_load(Settings *s, const wchar_t *path);

/* INI を UTF-8（BOM なし）で一時ファイル経由で保存する。 */
bool settings_save(const Settings *s, const wchar_t *path);

#endif /* MDV_SETTINGS_H */
