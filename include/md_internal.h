/*
 * md_internal.h — Markdown→RTF 変換の内部共有定義（md2rtf 内部専用）
 *
 * md2rtf.c / md_block.c / md_inline.c だけが読み込む。アプリ側は md2rtf.h を使うこと。
 * Windows に依存しない C11 で書き、ホスト上のテスト（make check）でもビルドできるようにする。
 *
 * 担当ファイル
 *   md2rtf.c   … 公開 API、配色、RTF ヘッダ、前処理（改行・タブ）、状態の初期化と後始末
 *   md_block.c … ブロック要素（見出し・段落・リスト・引用・コード・表・水平線）
 *   md_inline.c… インライン要素（強調・コード・リンク・画像・HTML・実体参照）と参照リンク定義
 *
 * 文字列・行の小さな補助関数は、各ファイルで使えるよう static inline でここに置く。
 */
#ifndef MDV_MD_INTERNAL_H
#define MDV_MD_INTERNAL_H

#include "md2rtf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* growable buffer                                                     */
/* ------------------------------------------------------------------ */
typedef struct { char *p; size_t n, cap; } Buf;

static inline void bgrow(Buf *b, size_t add)
{
    if (b->n + add + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 1024;
        while (nc < b->n + add + 1) nc *= 2;
        char *np = (char *)realloc(b->p, nc);
        if (!np) abort();
        b->p = np;
        b->cap = nc;
    }
    b->p[b->n] = 0;
}
static inline void bput(Buf *b, const char *s, size_t n)
{
    bgrow(b, n);
    if (n) memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static inline void bputs(Buf *b, const char *s) { bput(b, s, strlen(s)); }
static inline void bputc(Buf *b, char c) { bput(b, &c, 1); }
static inline void bprintf(Buf *b, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if ((size_t)k < sizeof tmp) { bput(b, tmp, (size_t)k); return; }
    bgrow(b, (size_t)k);
    va_start(ap, fmt);
    vsnprintf(b->p + b->n, (size_t)k + 1, fmt, ap);
    va_end(ap);
    b->n += (size_t)k;
}
static inline char *bdetach(Buf *b)
{
    bgrow(b, 0);
    char *p = b->p;
    b->p = NULL;
    b->n = b->cap = 0;
    return p;
}
static inline void bfree(Buf *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }

static inline char *xstrndup(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (!p) abort();
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ------------------------------------------------------------------ */
/* character helpers                                                   */
/* ------------------------------------------------------------------ */
static inline int is_ws(int c)    { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0; }
static inline int is_digit(int c) { return c >= '0' && c <= '9'; }
static inline int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline int is_alnum(int c) { return is_alpha(c) || is_digit(c); }
static inline int is_punct(int c)
{
    return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126);
}
static inline int to_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static inline int u8dec(const unsigned char *s, int n, unsigned *cp)
{
    unsigned c = s[0], v;
    int k;
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0)      { k = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { k = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { k = 4; v = c & 0x07; }
    else { *cp = 0xFFFD; return 1; }
    if (n < k) { *cp = 0xFFFD; return 1; }
    for (int i = 1; i < k; i++) {
        if ((s[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (s[i] & 0x3F);
    }
    *cp = v;
    return k;
}

static inline void u8enc(Buf *b, unsigned cp)
{
    char t[4];
    if (cp < 0x80) { bputc(b, (char)cp); }
    else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 0x3F)); bput(b, t, 2); }
    else if (cp < 0x10000) {
        t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        t[2] = (char)(0x80 | (cp & 0x3F)); bput(b, t, 3);
    } else {
        t[0] = (char)(0xF0 | (cp >> 18)); t[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[3] = (char)(0x80 | (cp & 0x3F)); bput(b, t, 4);
    }
}

static inline void rtf_u16(Buf *b, unsigned u)
{
    int v = (u >= 0x8000) ? (int)u - 0x10000 : (int)u;
    bprintf(b, "\\u%d?", v);
}

/* writes one code point as RTF text */
static inline void rtf_cp(Buf *b, unsigned cp)
{
    if (cp == '\\' || cp == '{' || cp == '}') { bputc(b, '\\'); bputc(b, (char)cp); }
    else if (cp == '\t') bputs(b, "\\tab ");
    else if (cp == '\n') bputs(b, "\\line ");
    else if (cp < 0x20) { /* drop control chars */ }
    else if (cp < 0x80) bputc(b, (char)cp);
    else if (cp < 0x10000) rtf_u16(b, cp);
    else {
        cp -= 0x10000;
        rtf_u16(b, 0xD800 + (cp >> 10));
        rtf_u16(b, 0xDC00 + (cp & 0x3FF));
    }
}

static inline void rtf_text(Buf *b, const char *s, int n)
{
    int i = 0;
    while (i < n) {
        unsigned cp;
        i += u8dec((const unsigned char *)s + i, n - i, &cp);
        rtf_cp(b, cp);
    }
}
/* ------------------------------------------------------------------ */
/* lines                                                               */
/* ------------------------------------------------------------------ */
typedef struct { const char *s; int n; } Line;
typedef struct { Line *v; int n, cap; } Lines;

static inline void lpush(Lines *L, const char *s, int n)
{
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 16;
        Line *nv = (Line *)realloc(L->v, sizeof(Line) * (size_t)L->cap);
        if (!nv) abort();
        L->v = nv;
    }
    L->v[L->n].s = s;
    L->v[L->n].n = n;
    L->n++;
}
static inline void lfree(Lines *L) { free(L->v); L->v = NULL; L->n = L->cap = 0; }

static inline int  lead_sp(Line l) { int i = 0; while (i < l.n && l.s[i] == ' ') i++; return i; }
static inline int  is_blank(Line l) { return lead_sp(l) == l.n; }
static inline Line lstrip(Line l, int k) { if (k > l.n) k = l.n; if (k < 0) k = 0; l.s += k; l.n -= k; return l; }
static inline Line ltrim(Line l)
{
    while (l.n > 0 && (l.s[0] == ' ' || l.s[0] == '\t')) { l.s++; l.n--; }
    while (l.n > 0 && (l.s[l.n - 1] == ' ' || l.s[l.n - 1] == '\t')) l.n--;
    return l;
}
static inline int line_has(Line l, const char *w)
{
    int k = (int)strlen(w);
    for (int i = 0; i + k <= l.n; i++)
        if (memcmp(l.s + i, w, (size_t)k) == 0) return 1;
    return 0;
}
static inline int lstarts(Line l, const char *pfx)
{
    int k = (int)strlen(pfx);
    return l.n >= k && memcmp(l.s, pfx, (size_t)k) == 0;
}

/* ------------------------------------------------------------------ */
/* theme colors (index into the RTF color table, see md2rtf.c THEME)   */
/* ------------------------------------------------------------------ */
enum {
    C_TEXT = 1, C_HEAD, C_LINK, C_CODEBG, C_INLBG, C_QUOTE, C_BORDER,
    C_CODETXT, C_THBG, C_QBAR, C_H6, C_COUNT
};

/* ------------------------------------------------------------------ */
/* renderer state                                                      */
/* ------------------------------------------------------------------ */
typedef struct { char *label; char *url; } Ref;

typedef struct {
    Buf              out;
    const MdOptions *o;
    const unsigned  *col;
    Ref       *refs;  int nrefs,  caprefs;
    MdLink    *links; int nlinks, caplinks;
    MdHeading *heads; int nheads, capheads;
    char  bullet[32];
    int   bullet_hang;
    int   has_bullet;
    int   fs;      /* body size, half-points, scaled */
    int   depth;   /* recursion guard               */
} R;

typedef struct {
    int indent;      /* left indent in twips   */
    int quote;       /* block quote depth      */
    int tight;       /* inside a tight list    */
    int list_level;  /* nesting level of lists */
} Ctx;

/* twips / half-points scaled by the zoom (needs a local `R *r`) */
#define Z(x) ((x) * r->o->scale_pct / 100)

/* ---- md_inline.c ---- */
char *md_norm_label(const char *s, int n);             /* 参照ラベルの正規化（呼び出し側で free） */
Ref  *md_find_ref(R *r, const char *s, int n);         /* 参照リンク定義の検索。なければ NULL     */
char *md_inline_plain(R *r, const char *s, int n);     /* インラインの平文（呼び出し側で free）   */
void  md_inline_rtf(R *r, const char *s, int n);       /* インラインを r->out へ RTF で出力       */

/* ---- md_block.c ---- */
int   md_parse_refdef(R *r, Line l, int store);        /* 参照リンク定義の行か。store なら登録   */
void  md_collect_refs(R *r, Lines *L);                 /* 文書全体から参照リンク定義を集める     */
void  md_blocks(R *r, Line *L, int n, Ctx cx);         /* ブロック列を r->out へ出力             */

#endif /* MDV_MD_INTERNAL_H */
