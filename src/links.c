/*
 * links.c — リンクの取得・分類・解決（仕様書 4.2 節）
 */
#include "links.h"
#include "viewer.h"

#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define LINK_TEXT_MAX 8192 /* これより長いリンク文字列は無視する */

/* ------------------------------------------------------------------------
 * クリックされたリンク先の取り出し
 * ------------------------------------------------------------------------ */

/* "...HYPERLINK \"url\"..." から url を取り出す */
static wchar_t *url_from_field(const wchar_t *text)
{
    const wchar_t *h = wcsstr(text, L"HYPERLINK");
    if (!h) return NULL;
    const wchar_t *q1 = wcschr(h, L'"');
    const wchar_t *q2 = q1 ? wcschr(q1 + 1, L'"') : NULL;
    if (!q2) return NULL;
    size_t n = (size_t)(q2 - (q1 + 1));
    wchar_t *url = malloc((n + 1) * sizeof(wchar_t));
    if (!url) return NULL;
    memcpy(url, q1 + 1, n * sizeof(wchar_t));
    url[n] = L'\0';
    return url;
}

static wchar_t *url_from_link_table(const wchar_t *text, const MdDoc *doc)
{
    char *t8 = utf8_from_wide(text);
    if (!t8) return NULL;
    wchar_t *url = NULL;
    for (int i = 0; i < doc->nlinks; i++) {
        if (strcmp(doc->links[i].text, t8) == 0) {
            url = wide_from_utf8(doc->links[i].url);
            break;
        }
    }
    free(t8);
    return url;
}

static bool looks_like_url(const wchar_t *text)
{
    return wcsstr(text, L"://") || _wcsnicmp(text, L"www.", 4) == 0 || _wcsnicmp(text, L"mailto:", 7) == 0;
}

wchar_t *link_target_from_click(HWND edit, const ENLINK *link, const MdDoc *doc)
{
    if (link->msg != WM_LBUTTONUP) return NULL;
    if (viewer_has_selection(edit)) return NULL; /* 文字列を選択していた */
    LONG length = link->chrg.cpMax - link->chrg.cpMin;
    if (length <= 0 || length > LINK_TEXT_MAX) return NULL;

    wchar_t *text = viewer_text_range(edit, link->chrg);
    if (!text) return NULL;

    /* 1) 範囲に隠しのフィールド命令が含まれる場合 2) リンク表を表示文字列で照合 3) 文字列自体が URL */
    wchar_t *url = url_from_field(text);
    if (!url) url = url_from_link_table(text, doc);
    if (!url && looks_like_url(text)) url = str_dup(text);
    free(text);
    return url;
}

/* ------------------------------------------------------------------------
 * 分類
 * ------------------------------------------------------------------------ */

LinkKind link_classify(const wchar_t *url)
{
    if (url[0] == L'#') return LINK_ANCHOR;

    /* スキーム：英数字と + - . が2文字以上続いて ':'（"C:" のドライブ名は除く） */
    const wchar_t *c = url;
    while ((*c >= L'a' && *c <= L'z') || (*c >= L'A' && *c <= L'Z') || (*c >= L'0' && *c <= L'9') ||
           *c == L'+' || *c == L'-' || *c == L'.') c++;
    if (*c == L':' && c - url >= 2) {
        if (!_wcsnicmp(url, L"http:", 5) || !_wcsnicmp(url, L"https:", 6) ||
            !_wcsnicmp(url, L"mailto:", 7) || !_wcsnicmp(url, L"ftp:", 4)) return LINK_WEB;
        return LINK_OTHER_SCHEME;
    }
    if (!_wcsnicmp(url, L"www.", 4)) return LINK_WWW;
    return LINK_LOCAL;
}

/* ------------------------------------------------------------------------
 * ローカルリンク
 * ------------------------------------------------------------------------ */

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* UTF-8 文字列の %XX をその場でデコードする */
static void percent_decode(char *s)
{
    char *d = s;
    while (*s) {
        if (s[0] == '%' && hex_value((unsigned char)s[1]) >= 0 && hex_value((unsigned char)s[2]) >= 0) {
            *d++ = (char)(hex_value((unsigned char)s[1]) * 16 + hex_value((unsigned char)s[2]));
            s += 3;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';
}

bool link_resolve_local(const wchar_t *url, const wchar_t *documentPath, LocalLink *out)
{
    memset(out, 0, sizeof(*out));

    /* "path?query#fragment" → path を % デコードし、'/' を '\' にする */
    char *u8 = utf8_from_wide(url);
    if (!u8) return false;
    char *fragment = strchr(u8, '#');
    if (fragment) *fragment++ = '\0';
    char *query = strchr(u8, '?');
    if (query) *query = '\0';
    percent_decode(u8);
    wchar_t *relative = wide_from_utf8(u8);
    if (!relative) {
        free(u8);
        return false;
    }
    for (wchar_t *p = relative; *p; p++) if (*p == L'/') *p = L'\\';

    /* 相対パス（"\x" のようなルート相対も含む）は文書のフォルダを基準にする */
    wchar_t full[MAX_PATH * 2];
    const wchar_t *r = relative;
    if (PathIsRelativeW(relative) || (r[0] == L'\\' && r[1] != L'\\')) {
        wchar_t dir[MAX_PATH * 2];
        if (documentPath) {
            lstrcpynW(dir, documentPath, MAX_PATH * 2);
            PathRemoveFileSpecW(dir);
        } else {
            GetCurrentDirectoryW(MAX_PATH * 2, dir);
        }
        while (*r == L'\\') r++;
        if (!PathCombineW(full, dir, r)) lstrcpynW(full, r, MAX_PATH * 2);
    } else {
        lstrcpynW(full, relative, MAX_PATH * 2);
    }

    out->emptyPath = relative[0] == L'\0';
    out->path = str_dup(full);
    if (fragment) {
        size_t n = strlen(fragment);
        out->fragment = malloc(n + 1);
        if (out->fragment) memcpy(out->fragment, fragment, n + 1);
    }
    free(relative);
    free(u8);
    return out->path != NULL;
}

void link_local_free(LocalLink *link)
{
    free(link->path);
    free(link->fragment);
    memset(link, 0, sizeof(*link));
}

/* ------------------------------------------------------------------------
 * 見出しへのジャンプ
 * ------------------------------------------------------------------------ */

/*
 * 見出しの一覧（md2rtf が生成した slug）から番号 k を探し、本文中で同じ文字列が
 * 行頭に現れる箇所を数えて位置を決める（同じ文字列の見出しが複数ある場合は出現順）。
 */
bool link_find_anchor(HWND edit, const MdDoc *doc, const char *fragment, LONG *cp)
{
    *cp = -1;
    size_t n = strlen(fragment);
    char *name = malloc(n + 1);
    if (!name) return false;
    memcpy(name, fragment, n + 1);
    percent_decode(name);
    for (char *p = name; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);

    int k = -1;
    for (int i = 0; i < doc->nheads; i++) {
        if (strcmp(doc->heads[i].slug, name) == 0) { k = i; break; }
    }
    free(name);
    if (k < 0) return false;

    const char *text = doc->heads[k].text;
    if (!text[0]) return true;
    int ordinal = 0;
    for (int i = 0; i < k; i++) if (strcmp(doc->heads[i].text, text) == 0) ordinal++;

    wchar_t *w = wide_from_utf8(text);
    if (!w) return true;
    LONG start = 0;
    for (;;) {
        LONG found = viewer_find(edit, w, (CHARRANGE){ start, -1 }, FR_DOWN | FR_MATCHCASE, NULL);
        if (found < 0) break;
        if (viewer_is_line_start(edit, found)) {
            if (ordinal == 0) { *cp = found; break; }
            ordinal--;
        }
        start = found + 1;
    }
    free(w);
    return true;
}
