/*
 * settings.h — 設定の保持・読み込み・保存（レジストリ HKCU\Software\MdView）
 *
 * レジストリの値名と Settings のフィールドの対応は settings.c の VALUE_TABLE に
 * 一覧で定義している。項目を追加するときはフィールドと表の1行を足す。
 */
#ifndef MDV_SETTINGS_H
#define MDV_SETTINGS_H

#include "common.h"

#define SETTINGS_REG_KEY L"Software\\MdView"
#define SETTINGS_FONT_NAME_MAX 64 /* フォント名の最大文字数（終端を含む） */

typedef struct {
    /* 表示 */
    int zoom;                                 /* Zoom：倍率 %（50〜300）         */
    bool dark;                                /* Dark：ダークテーマ              */
    bool autoReload;                          /* AutoReload：変更時に自動再読み込み */
    int fontSize;                             /* FontSize：本文 pt（6〜40）       */
    wchar_t fontBody[SETTINGS_FONT_NAME_MAX]; /* FontBody：本文フォント          */
    wchar_t fontCode[SETTINGS_FONT_NAME_MAX]; /* FontCode：等幅フォント          */

    /* ウィンドウ */
    WINDOWPLACEMENT placement;                /* Placement：位置・サイズ・最大化 */
    bool hasPlacement;                        /* placement が有効か              */
} Settings;

void settings_set_defaults(Settings *s);

/* 既定値を設定したうえでレジストリから読み込む。キーがなければ既定値のまま。 */
void settings_load(Settings *s);

/* レジストリへ保存する。hasPlacement が false なら Placement は書かない。 */
bool settings_save(const Settings *s);

#endif /* MDV_SETTINGS_H */
