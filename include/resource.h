/*
 * resource.h — リソースIDの一覧
 *
 * C ソースとリソーススクリプト（windres）の両方から読み込まれる。
 * 番号帯：
 *   100番台 アイコン / 200番台 メニュー・アクセラレータ / 300番台 データ（RCDATA）
 *   900番台 子ウィンドウ
 *   1000番台 コマンド（メニュー・アクセラレータ共通）
 *   2000番台以降 文字列（STRINGTABLE）
 */
#ifndef MDV_RESOURCE_H
#define MDV_RESOURCE_H

/* ---- アイコン ---- */
#define IDI_APP                 101

/* ---- メニュー・アクセラレータ ---- */
#define IDR_MAINMENU            201
#define IDR_POPUPMENU           202
#define IDR_ACCELERATORS        203

/* ---- データ ---- */
#define IDR_WELCOME             301 /* ファイル未指定時に表示する Markdown（UTF-8） */

/* ---- 子ウィンドウ ---- */
#define IDC_VIEWER              901 /* 本文の RichEdit */
#define IDC_STATUSBAR           902 /* ステータスバー */

/* ---- コマンド：ファイル ---- */
#define IDM_OPEN                1001
#define IDM_RELOAD              1002
#define IDM_EDIT                1003
#define IDM_BACK                1004
#define IDM_EXIT                1005

/* ---- コマンド：編集 ---- */
#define IDM_COPY                1010
#define IDM_SELALL              1011
#define IDM_FIND                1012
#define IDM_FINDNEXT            1013
#define IDM_FINDPREV            1014

/* ---- コマンド：表示 ---- */
#define IDM_ZOOMIN              1020
#define IDM_ZOOMOUT             1021
#define IDM_ZOOMRESET           1022
#define IDM_DARK                1023
#define IDM_RAW                 1024
#define IDM_AUTORELOAD          1025

/* ---- コマンド：ヘルプ ---- */
#define IDM_ABOUT               1030

/* ---- 文字列：メッセージ ---- */
#define IDS_ABOUT_FORMAT        2001 /* 書式: %ls %ls %ls（アプリ名・版数・INI のパス） */
#define IDS_RICHEDIT_ERROR      2002
#define IDS_INIT_ERROR          2003
#define IDS_OPEN_FAILED         2004 /* 書式: %ls（フルパス） */
#define IDS_FILE_NOT_FOUND      2005 /* 書式: %ls（フルパス） */
#define IDS_CONFIRM_LINK        2006 /* 書式: %ls（URL） */
#define IDS_CONFIRM_FILE        2007 /* 書式: %ls（フルパス） */
#define IDS_FIND_NOT_FOUND      2008 /* 書式: %ls（検索文字列） */

/* ---- 文字列：画面表示 ---- */
#define IDS_TITLE_SOURCE        2101
#define IDS_TITLE_ZOOM          2102 /* 書式: %d（倍率 %） */
#define IDS_IMAGE_LABEL         2103
#define IDS_STATUS_BYTES        2104 /* 書式: %llu（バイト数） */
#define IDS_STATUS_KIB          2105 /* 書式: %.2f %llu（KiB・バイト数） */
#define IDS_STATUS_MIB          2106 /* 書式: %.2f %llu（MiB・バイト数） */
#define IDS_STATUS_LINES        2107 /* 書式: %llu（行数） */

/* ---- 文字列：ファイル選択ダイアログ ---- */
#define IDS_FILTER_MARKDOWN     2201
#define IDS_FILTER_ALL          2202

#endif /* MDV_RESOURCE_H */
