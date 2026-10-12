/*
 * settings.c — INI 設定の読み込み・保存（表駆動）
 *
 * 方針（仕様書 8章）
 *   - EXE と同じフォルダの MdView.ini。UTF-8（BOM なし）で保存し、BOM 付きでも読み込める。
 *   - 不明なキーは無視し、不正値・範囲外の値は既定値のままにする。
 *   - 保存は一時ファイルに書いてから置き換える。
 */
#include "settings.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define MAX_INI_BYTES (1024 * 1024)

/* ------------------------------------------------------------------------
 * キー定義表
 * ------------------------------------------------------------------------ */

typedef struct {
    const char *name;
    int value;
} NamedValue;

typedef enum {
    KEY_INT,        /* int、範囲 [min, max] */
    KEY_BOOL,       /* bool、0/1・true/false など */
    KEY_BOOL_NAMED, /* bool。names の value が 0/1 */
    KEY_FONT        /* wchar_t[SETTINGS_FONT_NAME_MAX]、INI 上は UTF-8。空・長すぎる値は無視 */
} KeyType;

typedef struct {
    const char *section;
    const char *key;
    KeyType type;
    size_t offset;
    const NamedValue *names;
    size_t nameCount;
    int min, max;
} KeyDef;

static const NamedValue THEME_NAMES[] = {
    { "Light", 0 },
    { "Dark",  1 },
};

#define FIELD(f) offsetof(Settings, f)
#define NAMES(t) (t), ARRAY_COUNT(t)
#define NO_NAMES NULL, 0
#define NO_RANGE 0, 0

/* 保存時はこの順で出力する（セクションは連続させること）。 */
static const KeyDef KEY_TABLE[] = {
    { "Window",  "X",             KEY_INT,        FIELD(x),             NO_NAMES, INT_MIN, INT_MAX },
    { "Window",  "Y",             KEY_INT,        FIELD(y),             NO_NAMES, INT_MIN, INT_MAX },
    { "Window",  "Width",         KEY_INT,        FIELD(width),         NO_NAMES, 240, 16000 },
    { "Window",  "Height",        KEY_INT,        FIELD(height),        NO_NAMES, 180, 16000 },
    { "Window",  "Maximized",     KEY_BOOL,       FIELD(maximized),     NO_NAMES, NO_RANGE },
    { "Window",  "ShowStatusBar", KEY_BOOL,       FIELD(showStatusBar), NO_NAMES, NO_RANGE },
    { "Display", "Theme",         KEY_BOOL_NAMED, FIELD(dark),          NAMES(THEME_NAMES), NO_RANGE },
    { "Display", "Zoom",          KEY_INT,        FIELD(zoom),          NO_NAMES, 50, 300 },
    { "Display", "FontSize",      KEY_INT,        FIELD(fontSize),      NO_NAMES, 6, 40 },
    { "Display", "FontBody",      KEY_FONT,       FIELD(fontBody),      NO_NAMES, NO_RANGE },
    { "Display", "FontCode",      KEY_FONT,       FIELD(fontCode),      NO_NAMES, NO_RANGE },
    { "File",    "AutoReload",    KEY_BOOL,       FIELD(autoReload),    NO_NAMES, NO_RANGE },
};

/* ------------------------------------------------------------------------
 * 既定値・パス
 * ------------------------------------------------------------------------ */

void settings_set_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->x = CW_USEDEFAULT;
    s->y = CW_USEDEFAULT;
    s->width = 0;   /* 0：既定の 900×1000（96 DPI 基準）を使う */
    s->height = 0;
    s->maximized = false;
    s->showStatusBar = true;

    s->dark = false;
    s->zoom = 100;
    s->fontSize = 11;
    wcscpy(s->fontBody, L"Yu Gothic UI");
    wcscpy(s->fontCode, L"Consolas");

    s->autoReload = true;
}

wchar_t *settings_default_path(void)
{
    wchar_t *exe = module_path();
    if (!exe) return NULL;

    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) *slash = L'\0';
    wchar_t *ini = path_join(slash ? exe : L".", SETTINGS_FILE_NAME);
    free(exe);
    return ini;
}

/* ------------------------------------------------------------------------
 * 値の解析
 * ------------------------------------------------------------------------ */

static bool equals_ci(const char *a, const char *b)
{
    return _stricmp(a, b) == 0;
}

static bool parse_long(const char *text, long *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (end == text || *end != '\0') return false;
    *out = value;
    return true;
}

static bool parse_bool(const char *text, bool *out)
{
    static const char *const TRUE_WORDS[] = { "1", "true", "yes", "on" };
    static const char *const FALSE_WORDS[] = { "0", "false", "no", "off" };
    for (size_t i = 0; i < ARRAY_COUNT(TRUE_WORDS); ++i) {
        if (equals_ci(text, TRUE_WORDS[i])) { *out = true; return true; }
    }
    for (size_t i = 0; i < ARRAY_COUNT(FALSE_WORDS); ++i) {
        if (equals_ci(text, FALSE_WORDS[i])) { *out = false; return true; }
    }
    return false;
}

static bool parse_named(const KeyDef *def, const char *text, int *out)
{
    for (size_t i = 0; i < def->nameCount; ++i) {
        if (equals_ci(text, def->names[i].name)) { *out = def->names[i].value; return true; }
    }
    return false;
}

/* 1つの値を解析してフィールドに格納する。不正なら何もしない（既定値を維持）。 */
static void apply_value(Settings *s, const KeyDef *def, const char *text)
{
    void *field = (char *)s + def->offset;

    switch (def->type) {
    case KEY_INT: {
        long v;
        if (parse_long(text, &v) && v >= def->min && v <= def->max) *(int *)field = (int)v;
        break;
    }
    case KEY_BOOL: {
        bool v;
        if (parse_bool(text, &v)) *(bool *)field = v;
        break;
    }
    case KEY_BOOL_NAMED: {
        int v;
        if (parse_named(def, text, &v)) *(bool *)field = (v != 0);
        break;
    }
    case KEY_FONT: {
        if (!text[0]) break;
        wchar_t *w = wide_from_utf8(text);
        if (w && w[0] && wcslen(w) < SETTINGS_FONT_NAME_MAX) wcscpy((wchar_t *)field, w);
        free(w);
        break;
    }
    }
}

static void apply_entry(Settings *s, const char *section, const char *key, const char *value)
{
    for (size_t i = 0; i < ARRAY_COUNT(KEY_TABLE); ++i) {
        const KeyDef *def = &KEY_TABLE[i];
        if (equals_ci(section, def->section) && equals_ci(key, def->key)) {
            apply_value(s, def, value);
            return;
        }
    }
    /* 不明なキーは無視する */
}

/* ------------------------------------------------------------------------
 * 読み込み
 * ------------------------------------------------------------------------ */

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t') ++text;
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    return text;
}

static char *read_small_file(const wchar_t *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;

    char *buffer = NULL;
    LARGE_INTEGER size;
    if (GetFileSizeEx(file, &size) && size.QuadPart <= MAX_INI_BYTES) {
        buffer = calloc((size_t)size.QuadPart + 1, 1);
        DWORD read = 0;
        if (buffer && ReadFile(file, buffer, (DWORD)size.QuadPart, &read, NULL)) {
            buffer[read] = '\0';
        } else {
            free(buffer);
            buffer = NULL;
        }
    }
    CloseHandle(file);
    return buffer;
}

static void parse_ini(Settings *s, char *text)
{
    static const char UTF8_BOM[] = "\xEF\xBB\xBF";
    if (strncmp(text, UTF8_BOM, 3) == 0) text += 3;

    char section[64] = "";
    char *line = text;
    while (line && *line) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            *next++ = '\0';
            next += strspn(next, "\r\n");
        }

        char *p = trim(line);
        if (*p == '[') {
            char *close = strchr(p + 1, ']');
            if (close) {
                *close = '\0';
                snprintf(section, sizeof(section), "%s", trim(p + 1));
            }
        } else if (*p != '\0' && *p != ';' && *p != '#') {
            char *equal = strchr(p, '=');
            if (equal) {
                *equal = '\0';
                apply_entry(s, section, trim(p), trim(equal + 1));
            }
        }
        line = next;
    }
}

void settings_load(Settings *s, const wchar_t *path)
{
    settings_set_defaults(s);
    if (!path) return;

    char *text = read_small_file(path);
    if (!text) return;
    parse_ini(s, text);
    free(text);
}

/* ------------------------------------------------------------------------
 * 保存
 * ------------------------------------------------------------------------ */

static const char *name_of(const KeyDef *def, int value)
{
    for (size_t i = 0; i < def->nameCount; ++i) {
        if (def->names[i].value == value) return def->names[i].name;
    }
    return def->nameCount ? def->names[0].name : "";
}

static int format_value(const Settings *s, const KeyDef *def, char *out, size_t capacity)
{
    const void *field = (const char *)s + def->offset;

    switch (def->type) {
    case KEY_INT:
        return snprintf(out, capacity, "%d", *(const int *)field);
    case KEY_BOOL:
        return snprintf(out, capacity, "%d", *(const bool *)field ? 1 : 0);
    case KEY_BOOL_NAMED:
        return snprintf(out, capacity, "%s", name_of(def, *(const bool *)field ? 1 : 0));
    case KEY_FONT: {
        char *u8 = utf8_from_wide((const wchar_t *)field);
        int n = u8 ? snprintf(out, capacity, "%s", u8) : -1;
        free(u8);
        return n;
    }
    }
    return -1;
}

/* INI 本文を組み立てる。失敗時は -1。 */
static int build_ini_text(const Settings *s, char *out, size_t capacity)
{
    size_t used = 0;
    int n = snprintf(out, capacity, "[General]\r\nConfigVersion=1\r\n");
    if (n < 0 || (size_t)n >= capacity) return -1;
    used = (size_t)n;

    const char *currentSection = "";
    for (size_t i = 0; i < ARRAY_COUNT(KEY_TABLE); ++i) {
        const KeyDef *def = &KEY_TABLE[i];
        char value[256];
        n = format_value(s, def, value, sizeof(value));
        if (n < 0 || (size_t)n >= sizeof(value)) return -1;

        if (strcmp(currentSection, def->section) != 0) {
            currentSection = def->section;
            n = snprintf(out + used, capacity - used, "\r\n[%s]\r\n", currentSection);
            if (n < 0 || (size_t)n >= capacity - used) return -1;
            used += (size_t)n;
        }
        n = snprintf(out + used, capacity - used, "%s=%s\r\n", def->key, value);
        if (n < 0 || (size_t)n >= capacity - used) return -1;
        used += (size_t)n;
    }
    return (int)used;
}

bool settings_save(const Settings *s, const wchar_t *path)
{
    if (!path) return false;

    char text[4096];
    int length = build_ini_text(s, text, sizeof(text));
    if (length < 0) return false;

    size_t pathLen = wcslen(path);
    wchar_t *tempPath = malloc((pathLen + 5) * sizeof(wchar_t));
    if (!tempPath) return false;
    memcpy(tempPath, path, pathLen * sizeof(wchar_t));
    memcpy(tempPath + pathLen, L".tmp", 5 * sizeof(wchar_t));

    bool ok = false;
    HANDLE file = CreateFileW(tempPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ok = WriteFile(file, text, (DWORD)length, &written, NULL)
             && written == (DWORD)length
             && FlushFileBuffers(file);
        CloseHandle(file);
        if (ok) ok = MoveFileExW(tempPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok) DeleteFileW(tempPath);
    }
    free(tempPath);
    return ok;
}
