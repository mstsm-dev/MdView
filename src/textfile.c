/*
 * textfile.c — テキストファイルの読み込み（文字コード判別）と Markdown 拡張子の一覧
 */
#include "textfile.h"

#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ビューア内で開く拡張子。ファイル選択ダイアログのパターンとリンクの判定に共通で使う。 */
static const wchar_t *const MARKDOWN_EXTENSIONS[] = {
    L".md", L".markdown", L".mdown", L".mkd", L".txt",
};

/* ------------------------------------------------------------------------
 * 文字コード
 * ------------------------------------------------------------------------ */

static bool valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t k;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2) k = 1;
        else if ((c & 0xF0) == 0xE0) k = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) k = 3;
        else return false;
        if (i + k >= n) return false;
        for (size_t j = 1; j <= k; j++) if ((s[i + j] & 0xC0) != 0x80) return false;
        i += k + 1;
    }
    return true;
}

/* raw（n バイト、末尾に 4 バイトの 0 を確保済み）を UTF-8 にする。raw の所有権を受け取る。 */
static char *to_utf8(unsigned char *raw, DWORD n, size_t *outLength)
{
    char *result = NULL;
    size_t length = 0;

    if (n >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {            /* UTF-16 LE */
        result = utf8_from_wide_n((const wchar_t *)(raw + 2), (int)((n - 2) / 2), &length);
    } else if (n >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {     /* UTF-16 BE */
        for (DWORD i = 2; i + 1 < n; i += 2) {
            unsigned char t = raw[i];
            raw[i] = raw[i + 1];
            raw[i + 1] = t;
        }
        result = utf8_from_wide_n((const wchar_t *)(raw + 2), (int)((n - 2) / 2), &length);
    } else if (n >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { /* UTF-8 BOM */
        length = n - 3;
        result = malloc(length + 1);
        if (result) {
            memcpy(result, raw + 3, length);
            result[length] = '\0';
        }
    } else if (valid_utf8(raw, n)) {                             /* UTF-8 */
        result = (char *)raw;
        length = n;
        raw = NULL;
    } else {                                                     /* ANSI（日本語環境なら Shift_JIS） */
        int wn = MultiByteToWideChar(CP_ACP, 0, (const char *)raw, (int)n, NULL, 0);
        wchar_t *w = malloc(sizeof(wchar_t) * ((size_t)wn + 1));
        if (w) {
            MultiByteToWideChar(CP_ACP, 0, (const char *)raw, (int)n, w, wn);
            result = utf8_from_wide_n(w, wn, &length);
            free(w);
        }
    }
    free(raw);
    *outLength = length;
    return result;
}

/* ------------------------------------------------------------------------
 * 公開関数
 * ------------------------------------------------------------------------ */

bool textfile_read(const wchar_t *path, char **utf8, size_t *length, FileStamp *stamp)
{
    /* エディタが開いたままでも読めるよう、書き込み・削除の共有を許す */
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > TEXTFILE_MAX_BYTES) {
        CloseHandle(file);
        return false;
    }
    DWORD n = (DWORD)size.QuadPart, got = 0;
    unsigned char *raw = malloc((size_t)n + 4);
    if (!raw) {
        CloseHandle(file);
        return false;
    }
    while (got < n) {
        DWORD read = 0;
        if (!ReadFile(file, raw + got, n - got, &read, NULL) || read == 0) break;
        got += read;
    }
    if (stamp) {
        GetFileTime(file, NULL, NULL, &stamp->time);
        stamp->size = n;
    }
    CloseHandle(file);
    memset(raw + got, 0, 4);

    char *text = to_utf8(raw, got, length);
    if (!text) return false;
    *utf8 = text;
    return true;
}

bool textfile_changed(const wchar_t *path, const FileStamp *stamp)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) return false;
    return CompareFileTime(&attributes.ftLastWriteTime, &stamp->time) != 0
           || attributes.nFileSizeLow != stamp->size;
}

bool textfile_is_markdown(const wchar_t *path)
{
    const wchar_t *extension = PathFindExtensionW(path);
    for (size_t i = 0; i < ARRAY_COUNT(MARKDOWN_EXTENSIONS); ++i) {
        if (_wcsicmp(extension, MARKDOWN_EXTENSIONS[i]) == 0) return true;
    }
    return false;
}

wchar_t *textfile_filter_pattern(void)
{
    size_t length = 1;
    for (size_t i = 0; i < ARRAY_COUNT(MARKDOWN_EXTENSIONS); ++i) length += wcslen(MARKDOWN_EXTENSIONS[i]) + 2;

    wchar_t *pattern = malloc(length * sizeof(wchar_t));
    if (!pattern) return NULL;
    pattern[0] = L'\0';
    for (size_t i = 0; i < ARRAY_COUNT(MARKDOWN_EXTENSIONS); ++i) {
        if (i) wcscat(pattern, L";");
        wcscat(pattern, L"*");
        wcscat(pattern, MARKDOWN_EXTENSIONS[i]);
    }
    return pattern;
}
