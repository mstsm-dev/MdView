/*
 * settings.c — 設定のレジストリ読み込み・保存（表駆動）
 *
 * 方針（仕様書 8章）
 *   - 保存先は HKCU\Software\MdView。キーがなければ既定値を使う。
 *   - 型が違う値・範囲外の値は、その項目だけ既定値のままにする。
 *   - 終了時（WM_CLOSE）に保存する。
 */
#include "settings.h"

#include <string.h>
#include <wchar.h>

/* ------------------------------------------------------------------------
 * 値の定義表
 * ------------------------------------------------------------------------ */

typedef enum {
    VALUE_INT,       /* int、REG_DWORD、範囲 [min, max] */
    VALUE_BOOL,      /* bool、REG_DWORD、0 以外を true */
    VALUE_STRING,    /* wchar_t[SETTINGS_FONT_NAME_MAX]、REG_SZ、空文字列は無視 */
    VALUE_PLACEMENT  /* WINDOWPLACEMENT、REG_BINARY、いずれかのモニタ上にあること */
} ValueType;

typedef struct {
    const wchar_t *name;
    ValueType type;
    size_t offset;
    int min, max;
} ValueDef;

#define FIELD(f) offsetof(Settings, f)
#define NO_RANGE 0, 0

/* 保存時はこの順で書く。 */
static const ValueDef VALUE_TABLE[] = {
    { L"Zoom",       VALUE_INT,       FIELD(zoom),       50, 300 },
    { L"Dark",       VALUE_BOOL,      FIELD(dark),       NO_RANGE },
    { L"AutoReload", VALUE_BOOL,      FIELD(autoReload), NO_RANGE },
    { L"FontSize",   VALUE_INT,       FIELD(fontSize),   6, 40 },
    { L"FontBody",   VALUE_STRING,    FIELD(fontBody),   NO_RANGE },
    { L"FontCode",   VALUE_STRING,    FIELD(fontCode),   NO_RANGE },
    { L"Placement",  VALUE_PLACEMENT, FIELD(placement),  NO_RANGE },
};

/* ------------------------------------------------------------------------
 * 既定値
 * ------------------------------------------------------------------------ */

void settings_set_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->zoom = 100;
    s->dark = false;
    s->autoReload = true;
    s->fontSize = 11;
    wcscpy(s->fontBody, L"Yu Gothic UI");
    wcscpy(s->fontCode, L"Consolas");
    s->hasPlacement = false;
}

/* ------------------------------------------------------------------------
 * 読み込み
 * ------------------------------------------------------------------------ */

static bool read_dword(HKEY key, const wchar_t *name, DWORD *out)
{
    DWORD value, type, size = sizeof(value);
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
        return false;
    *out = value;
    return true;
}

static void read_value(HKEY key, Settings *s, const ValueDef *def)
{
    void *field = (char *)s + def->offset;

    switch (def->type) {
    case VALUE_INT: {
        DWORD v;
        if (read_dword(key, def->name, &v) && (int)v >= def->min && (int)v <= def->max) *(int *)field = (int)v;
        break;
    }
    case VALUE_BOOL: {
        DWORD v;
        if (read_dword(key, def->name, &v)) *(bool *)field = v != 0;
        break;
    }
    case VALUE_STRING: {
        /* 終端を含めて収まらない値は ERROR_MORE_DATA になり、既定値のままにする */
        wchar_t text[SETTINGS_FONT_NAME_MAX];
        DWORD type, size = sizeof(text) - sizeof(wchar_t);
        if (RegQueryValueExW(key, def->name, NULL, &type, (BYTE *)text, &size) == ERROR_SUCCESS
            && type == REG_SZ && size > sizeof(wchar_t)) {
            text[size / sizeof(wchar_t)] = L'\0';
            lstrcpynW((wchar_t *)field, text, SETTINGS_FONT_NAME_MAX);
        }
        break;
    }
    case VALUE_PLACEMENT: {
        WINDOWPLACEMENT wp;
        DWORD type, size = sizeof(wp);
        if (RegQueryValueExW(key, def->name, NULL, &type, (BYTE *)&wp, &size) == ERROR_SUCCESS
            && type == REG_BINARY && size == sizeof(wp) && wp.length == sizeof(wp)
            && MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONULL)) {
            *(WINDOWPLACEMENT *)field = wp;
            s->hasPlacement = true;
        }
        break;
    }
    }
}

void settings_load(Settings *s)
{
    settings_set_defaults(s);
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    for (size_t i = 0; i < ARRAY_COUNT(VALUE_TABLE); ++i) read_value(key, s, &VALUE_TABLE[i]);
    RegCloseKey(key);
}

/* ------------------------------------------------------------------------
 * 保存
 * ------------------------------------------------------------------------ */

static bool write_value(HKEY key, const Settings *s, const ValueDef *def)
{
    const void *field = (const char *)s + def->offset;
    DWORD dword;

    switch (def->type) {
    case VALUE_INT:
        dword = (DWORD)*(const int *)field;
        return RegSetValueExW(key, def->name, 0, REG_DWORD, (const BYTE *)&dword, sizeof(dword)) == ERROR_SUCCESS;
    case VALUE_BOOL:
        dword = *(const bool *)field ? 1 : 0;
        return RegSetValueExW(key, def->name, 0, REG_DWORD, (const BYTE *)&dword, sizeof(dword)) == ERROR_SUCCESS;
    case VALUE_STRING: {
        const wchar_t *text = (const wchar_t *)field;
        DWORD bytes = (DWORD)((wcslen(text) + 1) * sizeof(wchar_t));
        return RegSetValueExW(key, def->name, 0, REG_SZ, (const BYTE *)text, bytes) == ERROR_SUCCESS;
    }
    case VALUE_PLACEMENT:
        if (!s->hasPlacement) return true;
        return RegSetValueExW(key, def->name, 0, REG_BINARY, (const BYTE *)field, sizeof(WINDOWPLACEMENT)) == ERROR_SUCCESS;
    }
    return false;
}

bool settings_save(const Settings *s)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS)
        return false;
    bool ok = true;
    for (size_t i = 0; i < ARRAY_COUNT(VALUE_TABLE); ++i) ok = write_value(key, s, &VALUE_TABLE[i]) && ok;
    RegCloseKey(key);
    return ok;
}
