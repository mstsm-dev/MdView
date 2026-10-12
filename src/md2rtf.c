/*
 * md2rtf.c - Markdown (CommonMark + GFM subset) to RichEdit RTF converter
 *
 * Supported:
 *   blocks : ATX / setext headings, paragraphs, block quotes (nested),
 *            bullet / ordered / task lists (nested, tight / loose),
 *            fenced / indented code blocks, GFM tables, thematic breaks,
 *            link reference definitions, HTML comments
 *   inline : emphasis, strong, strikethrough, code spans, links, images,
 *            reference links, autolinks, GFM extended autolinks,
 *            hard line breaks, backslash escapes, entities, simple inline HTML
 */
#include "md2rtf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* growable buffer                                                     */
/* ------------------------------------------------------------------ */
typedef struct { char *p; size_t n, cap; } Buf;

static void bgrow(Buf *b, size_t add)
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
static void bput(Buf *b, const char *s, size_t n)
{
    bgrow(b, n);
    if (n) memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void bputs(Buf *b, const char *s) { bput(b, s, strlen(s)); }
static void bputc(Buf *b, char c) { bput(b, &c, 1); }
static void bprintf(Buf *b, const char *fmt, ...)
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
static char *bdetach(Buf *b)
{
    bgrow(b, 0);
    char *p = b->p;
    b->p = NULL;
    b->n = b->cap = 0;
    return p;
}
static void bfree(Buf *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }

static char *xstrndup(const char *s, size_t n)
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
static int is_ws(int c)    { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_alnum(int c) { return is_alpha(c) || is_digit(c); }
static int is_punct(int c)
{
    return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126);
}
static int to_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int u8dec(const unsigned char *s, int n, unsigned *cp)
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

static void u8enc(Buf *b, unsigned cp)
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

static void rtf_u16(Buf *b, unsigned u)
{
    int v = (u >= 0x8000) ? (int)u - 0x10000 : (int)u;
    bprintf(b, "\\u%d?", v);
}

/* writes one code point as RTF text */
static void rtf_cp(Buf *b, unsigned cp)
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

static void rtf_text(Buf *b, const char *s, int n)
{
    int i = 0;
    while (i < n) {
        unsigned cp;
        i += u8dec((const unsigned char *)s + i, n - i, &cp);
        rtf_cp(b, cp);
    }
}

/* ------------------------------------------------------------------ */
/* theme                                                               */
/* ------------------------------------------------------------------ */
enum {
    C_TEXT = 1, C_HEAD, C_LINK, C_CODEBG, C_INLBG, C_QUOTE, C_BORDER,
    C_CODETXT, C_THBG, C_QBAR, C_H6, C_COUNT
};

static const unsigned THEME[2][C_COUNT] = {
    /* light (GitHub-like) */
    { 0, 0x1F2328, 0x1F2328, 0x0969DA, 0xF3F5F7, 0xE8EBEF, 0x59636E, 0xD1D9E0,
      0x1F2328, 0xF3F5F7, 0xD1D9E0, 0x59636E },
    /* dark */
    { 0, 0xD1D7E0, 0xF0F6FC, 0x4493F8, 0x161B22, 0x2A313C, 0x9198A1, 0x3D444D,
      0xE6EDF3, 0x161B22, 0x3D444D, 0x9198A1 },
};

unsigned md_theme_background(int dark) { return dark ? 0x0D1117 : 0xFFFFFF; }
unsigned md_theme_text(int dark)       { return THEME[dark ? 1 : 0][C_TEXT]; }

/* ------------------------------------------------------------------ */
/* lines                                                               */
/* ------------------------------------------------------------------ */
typedef struct { const char *s; int n; } Line;
typedef struct { Line *v; int n, cap; } Lines;

static void lpush(Lines *L, const char *s, int n)
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
static void lfree(Lines *L) { free(L->v); L->v = NULL; L->n = L->cap = 0; }

static int  lead_sp(Line l) { int i = 0; while (i < l.n && l.s[i] == ' ') i++; return i; }
static int  is_blank(Line l) { return lead_sp(l) == l.n; }
static Line lstrip(Line l, int k) { if (k > l.n) k = l.n; if (k < 0) k = 0; l.s += k; l.n -= k; return l; }
static Line ltrim(Line l)
{
    while (l.n > 0 && (l.s[0] == ' ' || l.s[0] == '\t')) { l.s++; l.n--; }
    while (l.n > 0 && (l.s[l.n - 1] == ' ' || l.s[l.n - 1] == '\t')) l.n--;
    return l;
}
static int line_has(Line l, const char *w)
{
    int k = (int)strlen(w);
    for (int i = 0; i + k <= l.n; i++)
        if (memcmp(l.s + i, w, (size_t)k) == 0) return 1;
    return 0;
}
static int lstarts(Line l, const char *pfx)
{
    int k = (int)strlen(pfx);
    return l.n >= k && memcmp(l.s, pfx, (size_t)k) == 0;
}

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

#define Z(x) ((x) * r->o->scale_pct / 100)

static void add_link(R *r, const char *url, const char *text)
{
    if (r->nlinks == r->caplinks) {
        r->caplinks = r->caplinks ? r->caplinks * 2 : 32;
        r->links = (MdLink *)realloc(r->links, sizeof(MdLink) * (size_t)r->caplinks);
        if (!r->links) abort();
    }
    r->links[r->nlinks].url  = xstrndup(url, strlen(url));
    r->links[r->nlinks].text = xstrndup(text, strlen(text));
    r->nlinks++;
}

/* --- link reference definitions ---------------------------------- */
static char *norm_label(const char *s, int n)
{
    Buf b = {0};
    int sp = 0;
    for (int i = 0; i < n; i++) {
        int c = (unsigned char)s[i];
        if (is_ws(c)) { sp = 1; continue; }
        if (sp && b.n) bputc(&b, ' ');
        sp = 0;
        bputc(&b, (char)to_lower(c));
    }
    return bdetach(&b);
}

static Ref *find_ref(R *r, const char *s, int n)
{
    char *k = norm_label(s, n);
    Ref *res = NULL;
    for (int i = 0; i < r->nrefs; i++)
        if (strcmp(r->refs[i].label, k) == 0) { res = &r->refs[i]; break; }
    free(k);
    return res;
}

/* [label]: dest "title"  (single line). store != 0 -> remember it */
static int parse_refdef(R *r, Line l, int store)
{
    int i = lead_sp(l);
    if (i > 3 || i >= l.n || l.s[i] != '[') return 0;
    int j = i + 1;
    while (j < l.n && l.s[j] != ']') { if (l.s[j] == '\\') j++; j++; }
    if (j >= l.n || j == i + 1 || l.s[i + 1] == '^') return 0;
    if (j + 1 >= l.n || l.s[j + 1] != ':') return 0;
    int ls = i + 1, le = j;
    int k = j + 2;
    while (k < l.n && l.s[k] == ' ') k++;
    if (k >= l.n) return 0;
    int ds, de;
    if (l.s[k] == '<') {
        ds = k + 1; de = ds;
        while (de < l.n && l.s[de] != '>') de++;
        if (de >= l.n) return 0;
        k = de + 1;
    } else {
        ds = k; de = k;
        while (de < l.n && l.s[de] != ' ') de++;
        k = de;
    }
    while (k < l.n && l.s[k] == ' ') k++;
    if (k < l.n) {
        char q = l.s[k];
        char qc = (q == '(') ? ')' : q;
        if (q != '"' && q != '\'' && q != '(') return 0;
        int t = k + 1;
        while (t < l.n && l.s[t] != qc) { if (l.s[t] == '\\') t++; t++; }
        if (t >= l.n) return 0;
        k = t + 1;
        while (k < l.n && l.s[k] == ' ') k++;
        if (k < l.n) return 0;
    }
    if (store && !find_ref(r, l.s + ls, le - ls)) {
        if (r->nrefs == r->caprefs) {
            r->caprefs = r->caprefs ? r->caprefs * 2 : 16;
            r->refs = (Ref *)realloc(r->refs, sizeof(Ref) * (size_t)r->caprefs);
            if (!r->refs) abort();
        }
        r->refs[r->nrefs].label = norm_label(l.s + ls, le - ls);
        r->refs[r->nrefs].url   = xstrndup(l.s + ds, (size_t)(de - ds));
        r->nrefs++;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* inline rendering                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    R   *r;
    Buf *out;
    int  plain;     /* 1 = write plain UTF-8 text instead of RTF */
    int  in_link;   /* inside link text: no nested links         */
    int  html_link; /* an <a href> field is open                 */
} IC;

static void inl(IC *c, const char *s, int n);

static void ic_cp(IC *c, unsigned cp)
{
    if (c->plain) { if (cp == '\n') cp = ' '; u8enc(c->out, cp); }
    else rtf_cp(c->out, cp);
}
static void ic_text(IC *c, const char *s, int n)
{
    if (n <= 0) return;
    if (c->plain) bput(c->out, s, (size_t)n);
    else rtf_text(c->out, s, n);
}
static void ic_raw(IC *c, const char *rtf) { if (!c->plain) bputs(c->out, rtf); }
static void ic_break(IC *c) { if (c->plain) bputc(c->out, ' '); else bputs(c->out, "\\line "); }

static void link_begin(IC *c, const char *url, int ul)
{
    if (c->plain) return;
    Buf *b = c->out;
    bputs(b, "{\\field{\\*\\fldinst{HYPERLINK \"");
    int i = 0;
    while (i < ul) {
        unsigned cp;
        i += u8dec((const unsigned char *)url + i, ul - i, &cp);
        if (cp == '"') bputs(b, "%22");
        else if (cp == ' ') bputs(b, "%20");
        else if (cp < 0x20) { }
        else rtf_cp(b, cp);
    }
    bprintf(b, "\"}}{\\fldrslt{\\ul\\cf%d ", C_LINK);
}
static void link_end(IC *c) { if (!c->plain) bputs(c->out, "}}}"); }

/* backslash-unescape + entity-free copy of a link destination */
static char *unescape_url(const char *s, int n)
{
    Buf b = {0};
    for (int i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n && is_punct((unsigned char)s[i + 1])) i++;
        bputc(&b, s[i]);
    }
    return bdetach(&b);
}

static void flank(const char *s, int n, int i, int j, char d, int *can_open, int *can_close)
{
    int p  = i > 0 ? (unsigned char)s[i - 1] : 0;
    int nx = j < n ? (unsigned char)s[j] : 0;
    /* non-ASCII (e.g. Japanese) counts as a letter: works better for CJK text */
    int pws = is_ws(p),  ppu = p < 128 && is_punct(p);
    int nws = is_ws(nx), npu = nx < 128 && is_punct(nx);
    int left  = !nws && (!npu || pws || ppu);
    int right = !pws && (!ppu || nws || npu);
    if (d == '_') {
        *can_open  = left && (!right || ppu);
        *can_close = right && (!left || npu);
    } else {
        *can_open  = left;
        *can_close = right;
    }
}

static int find_tick_close(const char *s, int n, int from, int k)
{
    int i = from;
    while (i < n) {
        if (s[i] == '`') {
            int j = i;
            while (j < n && s[j] == '`') j++;
            if (j - i == k) return i;
            i = j;
        } else i++;
    }
    return -1;
}

/* skips a code span starting at s[i]=='`' ; returns index after it */
static int skip_code(const char *s, int n, int i)
{
    int j = i;
    while (j < n && s[j] == '`') j++;
    int e = find_tick_close(s, n, j, j - i);
    return e >= 0 ? e + (j - i) : j;
}

static int find_emph_close(const char *s, int n, int from, char d, int *mlen)
{
    int stack = 0, i = from;
    while (i < n) {
        char ch = s[i];
        if (ch == '\\' && i + 1 < n) { i += 2; continue; }
        if (ch == '`') { i = skip_code(s, n, i); continue; }
        if (ch == d) {
            int j = i, co, cc;
            while (j < n && s[j] == d) j++;
            flank(s, n, i, j, d, &co, &cc);
            if (cc && stack == 0) { *mlen = j - i; return i; }
            if (cc && stack > 0) stack--;
            else if (co) stack++;
            i = j;
            continue;
        }
        i++;
    }
    return -1;
}

static int find_bracket_close(const char *s, int n, int from)
{
    int depth = 1, i = from;
    while (i < n) {
        char ch = s[i];
        if (ch == '\\') { i += 2; continue; }
        if (ch == '`') { i = skip_code(s, n, i); continue; }
        if (ch == '[') depth++;
        else if (ch == ']' && --depth == 0) return i;
        i++;
    }
    return -1;
}

/* parses "(dest "title")" at s[i]=='(' ; returns index after ')' or -1 */
static int parse_inline_dest(const char *s, int n, int i, int *ds, int *dl)
{
    i++;
    while (i < n && is_ws((unsigned char)s[i]) && s[i]) i++;
    if (i < n && s[i] == '<') {
        int e = i + 1;
        while (e < n && s[e] != '>' && s[e] != '\n') { if (s[e] == '\\') e++; e++; }
        if (e >= n || s[e] != '>') return -1;
        *ds = i + 1; *dl = e - (i + 1);
        i = e + 1;
    } else {
        int e = i, depth = 0;
        while (e < n) {
            char ch = s[e];
            if (ch == '\\' && e + 1 < n) { e += 2; continue; }
            if (ch == ' ' || ch == '\n' || ch == '\t') break;
            if (ch == '(') depth++;
            if (ch == ')') { if (depth == 0) break; depth--; }
            e++;
        }
        *ds = i; *dl = e - i;
        i = e;
    }
    while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t')) i++;
    if (i < n && (s[i] == '"' || s[i] == '\'' || s[i] == '(')) {
        char qc = s[i] == '(' ? ')' : s[i];
        int e = i + 1;
        while (e < n && s[e] != qc) { if (s[e] == '\\') e++; e++; }
        if (e >= n) return -1;
        i = e + 1;
        while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t')) i++;
    }
    if (i >= n || s[i] != ')') return -1;
    return i + 1;
}

static void image_text(IC *c, const char *alt, int n)
{
    static const char LBL[] = "[\xE7\x94\xBB\xE5\x83\x8F"; /* "[画像" */
    ic_text(c, LBL, (int)sizeof LBL - 1);
    if (n > 0) {
        ic_text(c, ": ", 2);
        IC sub = *c;
        sub.in_link = 1;
        inl(&sub, alt, n);
    }
    ic_text(c, "]", 1);
}

/* emits a complete link; text rendered from markdown (or as image label) */
static void emit_link(IC *c, const char *url, int ul, const char *txt, int tl, int image, int literal_text)
{
    char *u = unescape_url(url, ul);
    if (!c->plain) {
        Buf pt = {0};
        IC pc = { c->r, &pt, 1, 1, 0 };
        if (image) image_text(&pc, txt, tl);
        else if (literal_text) ic_text(&pc, txt, tl);
        else inl(&pc, txt, tl);
        bgrow(&pt, 0);
        add_link(c->r, u, pt.p);
        bfree(&pt);
    }
    link_begin(c, u, (int)strlen(u));
    IC sub = *c;
    sub.in_link = 1;
    if (image) image_text(&sub, txt, tl);
    else if (literal_text) ic_text(&sub, txt, tl);
    else inl(&sub, txt, tl);
    link_end(c);
    free(u);
}

static int try_link(IC *c, const char *s, int n, int i, int image, int *next)
{
    int close = find_bracket_close(s, n, i + 1);
    if (close < 0) return 0;
    const char *txt = s + i + 1;
    int tl = close - (i + 1);
    const char *url = NULL;
    int ul = 0, after = -1;

    if (close + 1 < n && s[close + 1] == '(') {
        int ds, dl;
        int e = parse_inline_dest(s, n, close + 1, &ds, &dl);
        if (e >= 0) { url = s + ds; ul = dl; after = e; }
    }
    if (!url) {
        const char *lab = txt;
        int ll = tl, e = close + 1;
        if (close + 1 < n && s[close + 1] == '[') {
            int c2 = close + 2;
            while (c2 < n && s[c2] != ']' && s[c2] != '[') c2++;
            if (c2 < n && s[c2] == ']') {
                if (c2 > close + 2) { lab = s + close + 2; ll = c2 - (close + 2); }
                e = c2 + 1;
            }
        }
        Ref *rf = find_ref(c->r, lab, ll);
        if (!rf) return 0;
        url = rf->url;
        ul = (int)strlen(url);
        after = e;
    }
    if (c->in_link) {              /* links cannot nest: render text only */
        if (image) image_text(c, txt, tl);
        else inl(c, txt, tl);
    } else {
        emit_link(c, url, ul, txt, tl, image, 0);
    }
    *next = after;
    return 1;
}

static int match_ci(const char *s, int n, const char *w)
{
    int k = (int)strlen(w);
    if (n < k) return 0;
    for (int i = 0; i < k; i++)
        if (to_lower((unsigned char)s[i]) != w[i]) return 0;
    return 1;
}

/* attribute value from inside an html tag, e.g. src / alt / href */
static int html_attr(const char *s, int n, const char *name, int *vs, int *vl)
{
    int k = (int)strlen(name);
    for (int i = 1; i + k < n; i++) {
        if (!is_ws((unsigned char)s[i - 1]) || !match_ci(s + i, n - i, name)) continue;
        int j = i + k;
        while (j < n && s[j] == ' ') j++;
        if (j >= n || s[j] != '=') continue;
        j++;
        while (j < n && s[j] == ' ') j++;
        if (j < n && (s[j] == '"' || s[j] == '\'')) {
            char q = s[j];
            int e = j + 1;
            while (e < n && s[e] != q) e++;
            *vs = j + 1; *vl = e - (j + 1);
        } else {
            int e = j;
            while (e < n && !is_ws((unsigned char)s[e]) && s[e] != '>') e++;
            *vs = j; *vl = e - j;
        }
        return 1;
    }
    return 0;
}

/* inline HTML / autolinks at s[i]=='<'. returns index after it, or i */
static int try_html(IC *c, const char *s, int n, int i)
{
    if (n - i >= 4 && memcmp(s + i, "<!--", 4) == 0) {
        for (int k = i + 4; k + 2 < n; k++)
            if (s[k] == '-' && s[k + 1] == '-' && s[k + 2] == '>') return k + 3;
        return i;
    }
    int j = i + 1;
    /* <scheme:...> autolink */
    if (j < n && is_alpha((unsigned char)s[j])) {
        int k = j;
        while (k < n && (is_alnum((unsigned char)s[k]) || s[k] == '+' || s[k] == '.' || s[k] == '-')) k++;
        if (k < n && s[k] == ':' && k - j >= 2 && k - j <= 32) {
            int m = k + 1;
            while (m < n && s[m] != '>' && s[m] != '<' && !is_ws((unsigned char)s[m])) m++;
            if (m < n && s[m] == '>') {
                if (c->in_link) ic_text(c, s + j, m - j);
                else emit_link(c, s + j, m - j, s + j, m - j, 0, 1);
                return m + 1;
            }
        }
    }
    /* <user@example.com> */
    {
        int m = j, at = -1;
        while (m < n && s[m] != '>' && s[m] != '<' && !is_ws((unsigned char)s[m])) { if (s[m] == '@') at = m; m++; }
        if (m < n && s[m] == '>' && at > j && memchr(s + at, '.', (size_t)(m - at))) {
            if (c->in_link) ic_text(c, s + j, m - j);
            else {
                Buf u = {0};
                bputs(&u, "mailto:");
                bput(&u, s + j, (size_t)(m - j));
                emit_link(c, u.p, (int)u.n, s + j, m - j, 0, 1);
                bfree(&u);
            }
            return m + 1;
        }
    }
    /* html tag */
    int closing = 0;
    if (j < n && s[j] == '/') { closing = 1; j++; }
    if (j >= n || !is_alpha((unsigned char)s[j])) return i;
    int ne = j;
    while (ne < n && (is_alnum((unsigned char)s[ne]) || s[ne] == '-')) ne++;
    int e = ne;
    while (e < n && s[e] != '>' && s[e] != '<') e++;
    if (e >= n || s[e] != '>') return i;
    if (ne < n && !(is_ws((unsigned char)s[ne]) || s[ne] == '>' || s[ne] == '/')) return i;

    char name[16];
    int nl = ne - j;
    if (nl >= (int)sizeof name) nl = (int)sizeof name - 1;
    for (int k = 0; k < nl; k++) name[k] = (char)to_lower((unsigned char)s[j + k]);
    name[nl] = 0;
    const char *tag = s + i;
    int tagn = e + 1 - i;

    if (!strcmp(name, "br")) ic_break(c);
    else if (!strcmp(name, "b") || !strcmp(name, "strong")) ic_raw(c, closing ? "\\b0 " : "\\b ");
    else if (!strcmp(name, "i") || !strcmp(name, "em")) ic_raw(c, closing ? "\\i0 " : "\\i ");
    else if (!strcmp(name, "u") || !strcmp(name, "ins")) ic_raw(c, closing ? "\\ulnone " : "\\ul ");
    else if (!strcmp(name, "s") || !strcmp(name, "del") || !strcmp(name, "strike"))
        ic_raw(c, closing ? "\\strike0 " : "\\strike ");
    else if (!strcmp(name, "sup")) ic_raw(c, closing ? "\\nosupersub " : "\\super ");
    else if (!strcmp(name, "sub")) ic_raw(c, closing ? "\\nosupersub " : "\\sub ");
    else if (!strcmp(name, "code") || !strcmp(name, "kbd") || !strcmp(name, "tt"))
        ic_raw(c, closing ? "\\f0 " : "\\f1 ");
    else if (!strcmp(name, "img") && !closing) {
        int ss = 0, sl = 0, as = 0, al = 0;
        html_attr(tag, tagn, "src", &ss, &sl);
        html_attr(tag, tagn, "alt", &as, &al);
        if (sl > 0 && !c->in_link) emit_link(c, tag + ss, sl, tag + as, al, 1, 0);
        else image_text(c, tag + as, al);
    } else if (!strcmp(name, "a")) {
        if (!closing && !c->in_link && !c->html_link) {
            int hs = 0, hl = 0;
            if (html_attr(tag, tagn, "href", &hs, &hl) && hl > 0) {
                char *u = unescape_url(tag + hs, hl);
                if (!c->plain) add_link(c->r, u, "");
                link_begin(c, u, (int)strlen(u));
                free(u);
                c->html_link = 1;
                c->in_link = 1;
            }
        } else if (closing && c->html_link) {
            link_end(c);
            c->html_link = 0;
            c->in_link = 0;
        }
    }
    /* any other tag: dropped */
    return e + 1;
}

static const struct { const char *name; unsigned cp; } ENTITIES[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
    {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122},
    {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xAB},
    {"raquo", 0xBB}, {"times", 0xD7}, {"divide", 0xF7}, {"deg", 0xB0},
    {"middot", 0xB7}, {"bull", 0x2022}, {"larr", 0x2190}, {"rarr", 0x2192},
    {"uarr", 0x2191}, {"darr", 0x2193}, {"check", 0x2713}, {"yen", 0xA5},
    {"emsp", 0x2003}, {"ensp", 0x2002}, {"thinsp", 0x2009}, {"zwj", 0x200D},
};

static int try_entity(const char *s, int n, int i, unsigned *cp)
{
    int j = i + 1;
    if (j < n && s[j] == '#') {
        j++;
        unsigned v = 0;
        int hex = 0, d = 0;
        if (j < n && (s[j] == 'x' || s[j] == 'X')) { hex = 1; j++; }
        while (j < n && d < 8) {
            int ch = (unsigned char)s[j], dv;
            if (is_digit(ch)) dv = ch - '0';
            else if (hex && ch >= 'a' && ch <= 'f') dv = ch - 'a' + 10;
            else if (hex && ch >= 'A' && ch <= 'F') dv = ch - 'A' + 10;
            else break;
            v = v * (hex ? 16 : 10) + (unsigned)dv;
            j++; d++;
        }
        if (d == 0 || j >= n || s[j] != ';') return 0;
        if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
        *cp = v;
        return j + 1 - i;
    }
    int k = j;
    while (k < n && is_alnum((unsigned char)s[k]) && k - j < 10) k++;
    if (k >= n || s[k] != ';' || k == j) return 0;
    for (size_t e = 0; e < sizeof ENTITIES / sizeof ENTITIES[0]; e++) {
        if ((int)strlen(ENTITIES[e].name) == k - j && memcmp(ENTITIES[e].name, s + j, (size_t)(k - j)) == 0) {
            *cp = ENTITIES[e].cp;
            return k + 1 - i;
        }
    }
    return 0;
}

/* GFM extended autolink: http://, https://, www. */
static int try_autolink_ext(IC *c, const char *s, int n, int i)
{
    if (c->in_link) return i;
    if (i > 0) {
        int p = (unsigned char)s[i - 1];
        if (!(is_ws(p) || p == '*' || p == '_' || p == '~' || p == '(' || p >= 0x80)) return i;
    }
    int pl;
    if (match_ci(s + i, n - i, "https://")) pl = 8;
    else if (match_ci(s + i, n - i, "http://")) pl = 7;
    else if (match_ci(s + i, n - i, "www.")) pl = 4;
    else return i;
    int e = i + pl;
    while (e < n && !is_ws((unsigned char)s[e]) && s[e] != '<') e++;
    /* trim trailing punctuation and unbalanced ')' */
    for (;;) {
        if (e <= i + pl) break;
        char t = s[e - 1];
        if (strchr("?!.,:*_~'\";", t)) { e--; continue; }
        if (t == ')') {
            int o = 0, cl = 0;
            for (int k = i; k < e; k++) { if (s[k] == '(') o++; else if (s[k] == ')') cl++; }
            if (cl > o) { e--; continue; }
        }
        break;
    }
    if (e <= i + pl) return i;
    if (pl == 4 && !memchr(s + i + 4, '.', (size_t)(e - i - 4))) return i;
    if (pl == 4) {
        Buf u = {0};
        bputs(&u, "http://");
        bput(&u, s + i, (size_t)(e - i));
        emit_link(c, u.p, (int)u.n, s + i, e - i, 0, 1);
        bfree(&u);
    } else {
        emit_link(c, s + i, e - i, s + i, e - i, 0, 1);
    }
    return e;
}

static void inl(IC *c, const char *s, int n)
{
    R *r = c->r;
    if (++r->depth > 200) { ic_text(c, s, n); r->depth--; return; }
    int opened_html_link = c->html_link;
    int i = 0;
    while (i < n) {
        unsigned char ch = (unsigned char)s[i];
        int ni;
        switch (ch) {
        case '\\':
            if (i + 1 < n && s[i + 1] == '\n') { ic_break(c); i += 2; continue; }
            if (i + 1 < n && is_punct((unsigned char)s[i + 1])) { ic_cp(c, (unsigned char)s[i + 1]); i += 2; continue; }
            break;
        case ' ': {
            int j = i;
            while (j < n && s[j] == ' ') j++;
            if (j < n && s[j] == '\n') {
                if (j - i >= 2) ic_break(c);
                else ic_cp(c, ' ');
                i = j + 1;
                while (i < n && s[i] == ' ') i++;
                continue;
            }
            if (j == n) { i = j; continue; }
            ic_text(c, s + i, j - i);
            i = j;
            continue;
        }
        case '\n': {
            /* soft break: no space between two CJK characters */
            int prev_wide = i > 0 && ((unsigned char)s[i - 1] & 0x80);
            int j = i + 1;
            while (j < n && s[j] == ' ') j++;
            int next_wide = j < n && ((unsigned char)s[j] & 0x80);
            if (!(prev_wide && next_wide)) ic_cp(c, ' ');
            i = j;
            continue;
        }
        case '`': {
            int j = i;
            while (j < n && s[j] == '`') j++;
            int k = j - i;
            int e = find_tick_close(s, n, j, k);
            if (e < 0) { ic_text(c, s + i, k); i = j; continue; }
            int cs = j, ce = e;
            int all_sp = 1;
            for (int t = cs; t < ce; t++) if (s[t] != ' ' && s[t] != '\n') { all_sp = 0; break; }
            if (!all_sp && ce - cs >= 2 && (s[cs] == ' ' || s[cs] == '\n') && (s[ce - 1] == ' ' || s[ce - 1] == '\n')) { cs++; ce--; }
            if (!c->plain)
                bprintf(c->out, "{\\f1\\fs%d\\highlight%d\\cf%d ", r->fs * 9 / 10, C_INLBG, C_CODETXT);
            for (int t = cs; t < ce; t++) {
                if (s[t] == '\n') ic_cp(c, ' ');
                else {
                    unsigned cp;
                    t += u8dec((const unsigned char *)s + t, ce - t, &cp) - 1;
                    ic_cp(c, cp);
                }
            }
            ic_raw(c, "}");
            i = e + k;
            continue;
        }
        case '*':
        case '_': {
            int j = i, co, cc;
            while (j < n && s[j] == (char)ch) j++;
            int L = j - i;
            flank(s, n, i, j, (char)ch, &co, &cc);
            if (co) {
                int m;
                int e = find_emph_close(s, n, j, (char)ch, &m);
                if (e >= 0) {
                    int k = (L >= 3 && m >= 3) ? 3 : (L >= 2 && m >= 2) ? 2 : 1;
                    ic_text(c, s + i, L - k);
                    ic_raw(c, k == 3 ? "{\\b\\i " : k == 2 ? "{\\b " : "{\\i ");
                    inl(c, s + j, e - j);
                    ic_raw(c, "}");
                    ic_text(c, s + e + k, m - k);
                    i = e + m;
                    continue;
                }
            }
            ic_text(c, s + i, L);
            i = j;
            continue;
        }
        case '~': {
            int j = i, co, cc;
            while (j < n && s[j] == '~') j++;
            int L = j - i;
            if (L <= 2) {
                flank(s, n, i, j, '~', &co, &cc);
                int m;
                int e = co ? find_emph_close(s, n, j, '~', &m) : -1;
                if (e >= 0 && m == L) {
                    ic_raw(c, "{\\strike ");
                    inl(c, s + j, e - j);
                    ic_raw(c, "}");
                    i = e + m;
                    continue;
                }
            }
            ic_text(c, s + i, L);
            i = j;
            continue;
        }
        case '!':
            if (i + 1 < n && s[i + 1] == '[' && try_link(c, s, n, i + 1, 1, &ni)) { i = ni; continue; }
            break;
        case '[':
            if (try_link(c, s, n, i, 0, &ni)) { i = ni; continue; }
            break;
        case '<':
            ni = try_html(c, s, n, i);
            if (ni > i) { i = ni; continue; }
            break;
        case '&': {
            unsigned cp;
            int k = try_entity(s, n, i, &cp);
            if (k > 0) { ic_cp(c, cp); i += k; continue; }
            break;
        }
        case 'h': case 'H': case 'w': case 'W':
            ni = try_autolink_ext(c, s, n, i);
            if (ni > i) { i = ni; continue; }
            break;
        default:
            break;
        }
        unsigned cp;
        int k = u8dec((const unsigned char *)s + i, n - i, &cp);
        ic_cp(c, cp);
        i += k;
    }
    if (c->html_link && !opened_html_link) {  /* unclosed <a> */
        link_end(c);
        c->html_link = 0;
        c->in_link = 0;
    }
    r->depth--;
}

/* plain text of inline markdown (caller frees) */
static char *inline_plain(R *r, const char *s, int n)
{
    Buf b = {0};
    IC c = { r, &b, 1, 0, 0 };
    inl(&c, s, n);
    return bdetach(&b);
}

static void inline_rtf(R *r, const char *s, int n)
{
    IC c = { r, &r->out, 0, 0, 0 };
    inl(&c, s, n);
}

/* ------------------------------------------------------------------ */
/* block helpers                                                       */
/* ------------------------------------------------------------------ */
static int is_hr(Line l)
{
    int i = lead_sp(l);
    if (i > 3) return 0;
    char ch = 0;
    int cnt = 0;
    for (; i < l.n; i++) {
        char x = l.s[i];
        if (x == ' ' || x == '\t') continue;
        if (x != '*' && x != '-' && x != '_') return 0;
        if (!ch) ch = x;
        else if (x != ch) return 0;
        cnt++;
    }
    return cnt >= 3;
}

static int is_atx(Line l, int *lvl, Line *content)
{
    int i = lead_sp(l);
    if (i > 3) return 0;
    int h = 0;
    while (i < l.n && l.s[i] == '#' && h < 7) { h++; i++; }
    if (h < 1 || h > 6) return 0;
    if (i < l.n && l.s[i] != ' ' && l.s[i] != '\t') return 0;
    while (i < l.n && (l.s[i] == ' ' || l.s[i] == '\t')) i++;
    int e = l.n;
    while (e > i && (l.s[e - 1] == ' ' || l.s[e - 1] == '\t')) e--;
    int k = e;
    while (k > i && l.s[k - 1] == '#') k--;
    if (k < e && (k == i || l.s[k - 1] == ' ' || l.s[k - 1] == '\t')) {
        e = k;
        while (e > i && (l.s[e - 1] == ' ' || l.s[e - 1] == '\t')) e--;
    }
    content->s = l.s + i;
    content->n = e - i;
    *lvl = h;
    return 1;
}

static int is_setext(Line l)
{
    int i = lead_sp(l);
    if (i > 3 || i >= l.n) return 0;
    char ch = l.s[i];
    if (ch != '=' && ch != '-') return 0;
    while (i < l.n && l.s[i] == ch) i++;
    while (i < l.n && (l.s[i] == ' ' || l.s[i] == '\t')) i++;
    return i == l.n ? (ch == '=' ? 1 : 2) : 0;
}

static int is_fence(Line l, char *fc, int *fl, int *ind)
{
    int i = lead_sp(l);
    if (i > 3 || i >= l.n) return 0;
    char ch = l.s[i];
    if (ch != '`' && ch != '~') return 0;
    int j = i;
    while (j < l.n && l.s[j] == ch) j++;
    if (j - i < 3) return 0;
    if (ch == '`')
        for (int k = j; k < l.n; k++) if (l.s[k] == '`') return 0;
    *fc = ch; *fl = j - i; *ind = i;
    return 1;
}

static int is_fence_close(Line l, char fc, int fl)
{
    int i = lead_sp(l);
    if (i > 3) return 0;
    int j = i;
    while (j < l.n && l.s[j] == fc) j++;
    if (j - i < fl) return 0;
    while (j < l.n && (l.s[j] == ' ' || l.s[j] == '\t')) j++;
    return j == l.n;
}

static int is_quote(Line l)
{
    int i = lead_sp(l);
    return i <= 3 && i < l.n && l.s[i] == '>';
}
static Line strip_quote(Line l)
{
    int i = lead_sp(l) + 1;
    if (i < l.n && l.s[i] == ' ') i++;
    return lstrip(l, i);
}

typedef struct { int ordered, start, content, empty; char ch; } Mark;

static int is_marker(Line l, Mark *m)
{
    int i = lead_sp(l), j;
    if (i > 3 || i >= l.n) return 0;
    char ch = l.s[i];
    if (ch == '-' || ch == '+' || ch == '*') {
        m->ordered = 0; m->start = 1; m->ch = ch; j = i + 1;
    } else if (is_digit((unsigned char)ch)) {
        int v = 0, d = 0;
        j = i;
        while (j < l.n && is_digit((unsigned char)l.s[j]) && d < 9) { v = v * 10 + (l.s[j] - '0'); j++; d++; }
        if (j >= l.n || (l.s[j] != '.' && l.s[j] != ')')) return 0;
        m->ordered = 1; m->start = v; m->ch = l.s[j]; j++;
    } else return 0;
    if (j < l.n && l.s[j] != ' ' && l.s[j] != '\t') return 0;
    int k = j;
    while (k < l.n && l.s[k] == ' ') k++;
    if (k == l.n) { m->empty = 1; m->content = j + 1; }
    else { m->empty = 0; m->content = (k - j > 4) ? j + 1 : k; }
    return 1;
}

/* line that can interrupt a paragraph */
static int interrupts(Line l)
{
    int lv; Line hc; char fc; int fl, fi; Mark m;
    if (is_blank(l)) return 1;
    if (lead_sp(l) >= 4) return 0;
    if (is_atx(l, &lv, &hc) || is_fence(l, &fc, &fl, &fi) || is_hr(l) || is_quote(l)) return 1;
    if (is_marker(l, &m) && !m.empty && (!m.ordered || m.start == 1)) return 1;
    {
        Line t = lstrip(l, lead_sp(l));
        if (lstarts(t, "<!--")) return 1;
    }
    return 0;
}

/* --- tables ---------------------------------------------------------- */
#define MAXCOLS 64

static int split_cells(Line l, Line *cells, int max)
{
    l = ltrim(l);
    if (l.n > 0 && l.s[0] == '|') { l.s++; l.n--; }
    if (l.n > 0 && l.s[l.n - 1] == '|' && !(l.n > 1 && l.s[l.n - 2] == '\\')) l.n--;
    int cnt = 0, st = 0;
    for (int i = 0; i <= l.n; i++) {
        if (i == l.n || (l.s[i] == '|' && !(i > 0 && l.s[i - 1] == '\\'))) {
            if (cnt < max) {
                Line c = { l.s + st, i - st };
                cells[cnt++] = ltrim(c);
            }
            st = i + 1;
        }
    }
    return cnt;
}

static int has_pipe(Line l)
{
    for (int i = 0; i < l.n; i++)
        if (l.s[i] == '|' && !(i > 0 && l.s[i - 1] == '\\')) return 1;
    return 0;
}

static int delim_row(Line l, int *al, int max)
{
    if (!has_pipe(l) && !lstarts(ltrim(l), "-") && !lstarts(ltrim(l), ":")) return 0;
    Line cells[MAXCOLS];
    int n = split_cells(l, cells, max);
    for (int i = 0; i < n; i++) {
        Line c = cells[i];
        if (c.n == 0) return 0;
        int left = c.s[0] == ':', right = c.s[c.n - 1] == ':';
        int dash = 0;
        for (int k = left; k < c.n - right; k++) {
            if (c.s[k] != '-') return 0;
            dash++;
        }
        if (!dash) return 0;
        al[i] = (left && right) ? 1 : right ? 2 : 0;
    }
    return n;
}

static int table_start(Line h, Line d, int *ncols, int *al)
{
    if (!has_pipe(h) || lead_sp(h) > 3) return 0;
    Line cells[MAXCOLS];
    int hn = split_cells(h, cells, MAXCOLS);
    int dn = delim_row(d, al, MAXCOLS);
    if (dn == 0 || dn != hn) return 0;
    if (!has_pipe(d) && dn == 1) return 0;
    *ncols = hn;
    return 1;
}

static int disp_width(const char *s)
{
    int w = 0, i = 0, n = (int)strlen(s);
    while (i < n) {
        unsigned cp;
        i += u8dec((const unsigned char *)s + i, n - i, &cp);
        w += cp < 0x1100 ? 1 : 2;
    }
    return w;
}

/* ------------------------------------------------------------------ */
/* block emitters                                                      */
/* ------------------------------------------------------------------ */
static void para_begin(R *r, Ctx cx, int sb, int sa, int fs, int color, int keepn)
{
    Buf *b = &r->out;
    bprintf(b, "\\pard\\plain\\sl276\\slmult1\\sb%d\\sa%d\\li%d", sb, sa, cx.indent);
    if (keepn) bputs(b, "\\keepn");
    if (cx.quote)
        bprintf(b, "\\brdrl\\brdrs\\brdrw30\\brsp%d\\brdrcf%d", Z(140), C_QBAR);
    if (r->has_bullet)
        bprintf(b, "\\fi-%d\\tx%d", r->bullet_hang, cx.indent);
    bprintf(b, "\\f0\\fs%d\\cf%d ", fs, cx.quote && color == C_TEXT ? C_QUOTE : color);
    if (r->has_bullet) {
        bprintf(b, "{\\cf%d ", C_QUOTE);
        rtf_text(b, r->bullet, (int)strlen(r->bullet));
        bputs(b, "}\\tab ");
        r->has_bullet = 0;
    }
}

static int para_sa(R *r, Ctx cx) { return cx.tight ? Z(40) : Z(160); }

static void flush_bullet(R *r, Ctx cx)
{
    if (!r->has_bullet) return;
    para_begin(r, cx, 0, Z(40), r->fs, C_TEXT, 0);
    bputs(&r->out, "\\par\n");
}

static void emit_spacer(R *r, int halfpt)
{
    bprintf(&r->out, "\\pard\\plain\\sb0\\sa0\\fs%d\\par\n", halfpt < 2 ? 2 : halfpt);
}

static void emit_para(R *r, Ctx cx, const char *s, int n)
{
    char *pl = inline_plain(r, s, n);
    int empty = 1;
    for (char *p = pl; *p; p++) if (!is_ws((unsigned char)*p)) { empty = 0; break; }
    free(pl);
    if (empty && !r->has_bullet) return;
    para_begin(r, cx, 0, para_sa(r, cx), r->fs, C_TEXT, 0);
    inline_rtf(r, s, n);
    bputs(&r->out, "\\par\n");
}

static void add_heading(R *r, const char *plain)
{
    Buf b = {0};
    for (const unsigned char *p = (const unsigned char *)plain; *p; p++) {
        int ch = *p;
        if (ch >= 0x80) bputc(&b, (char)ch);
        else if (is_alnum(ch)) bputc(&b, (char)to_lower(ch));
        else if (ch == ' ') bputc(&b, '-');
        else if (ch == '-' || ch == '_') bputc(&b, (char)ch);
    }
    char *base = bdetach(&b);
    char *slug = NULL;
    for (int k = 0; !slug; k++) {
        Buf t = {0};
        bputs(&t, base);
        if (k) bprintf(&t, "-%d", k);
        int dup = 0;
        for (int i = 0; i < r->nheads; i++) if (!strcmp(r->heads[i].slug, t.p)) { dup = 1; break; }
        if (dup) bfree(&t);
        else slug = bdetach(&t);
    }
    free(base);
    if (r->nheads == r->capheads) {
        r->capheads = r->capheads ? r->capheads * 2 : 32;
        r->heads = (MdHeading *)realloc(r->heads, sizeof(MdHeading) * (size_t)r->capheads);
        if (!r->heads) abort();
    }
    r->heads[r->nheads].slug = slug;
    r->heads[r->nheads].text = xstrndup(plain, strlen(plain));
    r->nheads++;
}

static void emit_heading(R *r, Ctx cx, int lvl, const char *s, int n)
{
    static const int pct[7] = { 0, 200, 155, 128, 110, 100, 92 };
    static const int sb[7]  = { 0, 360, 340, 280, 240, 200, 200 };
    int fs = r->fs * pct[lvl] / 100;
    char *pl = inline_plain(r, s, n);
    add_heading(r, pl);
    free(pl);
    para_begin(r, cx, Z(sb[lvl]), Z(lvl <= 2 ? 200 : 120), fs, lvl == 6 ? C_H6 : C_HEAD, 1);
    if (lvl <= 2) bprintf(&r->out, "\\brdrb\\brdrs\\brdrw10\\brsp%d\\brdrcf%d ", Z(80), C_BORDER);
    bputs(&r->out, "\\b ");
    inline_rtf(r, s, n);
    bputs(&r->out, "\\par\n");
}

static int right_edge(R *r, Ctx cx)
{
    int right = r->o->width_twips;
    if (right < cx.indent + 2400) right = cx.indent + 2400;
    return right;
}

static void cell_borders(Buf *b, int color)
{
    bprintf(b, "\\clbrdrt\\brdrs\\brdrw10\\brdrcf%d\\clbrdrl\\brdrs\\brdrw10\\brdrcf%d"
               "\\clbrdrb\\brdrs\\brdrw10\\brdrcf%d\\clbrdrr\\brdrs\\brdrw10\\brdrcf%d",
            color, color, color, color);
}

static void emit_code(R *r, Ctx cx, Line *L, int n)
{
    Buf *b = &r->out;
    flush_bullet(r, cx);
    int fs = r->fs * 9 / 10;
    bprintf(b, "\\trowd\\trgaph%d\\trleft%d", Z(200), cx.indent);
    cell_borders(b, C_CODEBG);
    bprintf(b, "\\clcbpat%d\\cellx%d\n", C_CODEBG, right_edge(r, cx));
    bprintf(b, "\\pard\\intbl\\plain\\sl0\\sb%d\\sa%d\\f1\\fs%d\\cf%d\\highlight%d ",
            Z(140), Z(140), fs, C_CODETXT, C_CODEBG);
    if (n == 0) bputc(b, ' ');
    for (int i = 0; i < n; i++) {
        if (i) bputs(b, "\\line ");
        rtf_text(b, L[i].s, L[i].n);
    }
    bputs(b, "\\cell\\row\\pard\n");
    emit_spacer(r, Z(cx.tight ? 8 : 16));
}

static void emit_hr(R *r, Ctx cx)
{
    Buf *b = &r->out;
    flush_bullet(r, cx);
    bprintf(b, "\\trowd\\trgaph0\\trleft%d\\clbrdrb\\brdrs\\brdrw20\\brdrcf%d\\cellx%d\n",
            cx.indent, C_BORDER, right_edge(r, cx));
    bputs(b, "\\pard\\intbl\\plain\\sb0\\sa0\\fs6 \\cell\\row\\pard\n");
    emit_spacer(r, Z(20));
}

static int parse_table(R *r, Line *L, int n, int i, Ctx cx, int ncols, const int *al)
{
    Buf *b = &r->out;
    int end = i + 2;
    while (end < n && !is_blank(L[end]) && !(interrupts(L[end]) && !has_pipe(L[end]))) end++;

    /* measure columns */
    int maxw[MAXCOLS] = {0};
    for (int row = i; row < end; row++) {
        if (row == i + 1) continue;
        Line cells[MAXCOLS];
        int cn = split_cells(L[row], cells, MAXCOLS);
        for (int k = 0; k < cn && k < ncols; k++) {
            char *pl = inline_plain(r, cells[k].s, cells[k].n);
            int w = disp_width(pl);
            free(pl);
            if (w > maxw[k]) maxw[k] = w;
        }
    }
    int gaph = Z(110);
    int chw  = r->fs * 11 / 2;  /* approx. width of a half-width char in twips */
    int avail = right_edge(r, cx) - cx.indent;
    int w[MAXCOLS], sum = 0;
    for (int k = 0; k < ncols; k++) {
        w[k] = maxw[k] * chw + gaph * 2 + Z(120);
        if (w[k] < Z(700)) w[k] = Z(700);
        sum += w[k];
    }
    if (sum > avail) {
        int minw = Z(700), fixed = 0, flex = 0;
        for (int k = 0; k < ncols; k++) flex += w[k] - minw;
        fixed = minw * ncols;
        int room = avail - fixed;
        for (int k = 0; k < ncols; k++)
            w[k] = minw + (room > 0 && flex > 0 ? (int)((long long)(w[k] - minw) * room / flex) : 0);
    }
    int x[MAXCOLS], acc = cx.indent;
    for (int k = 0; k < ncols; k++) { acc += w[k]; x[k] = acc; }

    flush_bullet(r, cx);
    for (int row = i; row < end; row++) {
        if (row == i + 1) continue;
        int header = row == i;
        Line cells[MAXCOLS];
        int cn = split_cells(L[row], cells, MAXCOLS);
        bprintf(b, "\\trowd\\trgaph%d\\trleft%d", gaph, cx.indent);
        for (int k = 0; k < ncols; k++) {
            cell_borders(b, C_BORDER);
            if (header) bprintf(b, "\\clcbpat%d", C_THBG);
            bprintf(b, "\\cellx%d", x[k]);
        }
        bputc(b, '\n');
        for (int k = 0; k < ncols; k++) {
            bprintf(b, "\\pard\\intbl\\plain\\sl0\\sb%d\\sa%d%s\\f0\\fs%d\\cf%d ",
                    Z(60), Z(60), al[k] == 1 ? "\\qc" : al[k] == 2 ? "\\qr" : "\\ql",
                    r->fs, cx.quote ? C_QUOTE : C_TEXT);
            if (header) {
                bprintf(b, "\\b\\highlight%d ", C_THBG);
            }
            if (k < cn) inline_rtf(r, cells[k].s, cells[k].n);
            bputs(b, "\\cell");
        }
        bputs(b, "\\row\n");
    }
    bputs(b, "\\pard\n");
    emit_spacer(r, Z(16));
    return end;
}

static void blocks(R *r, Line *L, int n, Ctx cx);

static int parse_list(R *r, Line *L, int n, int i, Ctx cx)
{
    Mark m0;
    is_marker(L[i], &m0);
    Lines *items = NULL;
    int nitems = 0, capitems = 0, loose = 0;

    while (i < n) {
        Mark m;
        if (!is_marker(L[i], &m) || m.ordered != m0.ordered || m.ch != m0.ch || is_hr(L[i])) break;
        int W = m.content;
        Lines it = {0};
        Line first = lstrip(L[i], W);
        lpush(&it, first.s, first.n);
        char fc = 0; int fl = 0, fi, in_fence = 0;
        if (is_fence(first, &fc, &fl, &fi)) in_fence = 1;
        int j = i + 1, inner_blank = 0;
        while (j < n) {
            Line x = L[j];
            if (is_blank(x)) {
                int k = j;
                while (k < n && is_blank(L[k])) k++;
                if (k < n && lead_sp(L[k]) >= W && !(m.empty && it.n == 1 && it.v[0].n == 0)) {
                    for (; j < k; j++) lpush(&it, "", 0);
                    if (!in_fence) inner_blank = 1;
                    continue;
                }
                break;
            }
            int xi = lead_sp(x);
            if (xi >= W) {
                Line sx = lstrip(x, W);
                lpush(&it, sx.s, sx.n);
                if (in_fence) { if (is_fence_close(sx, fc, fl)) in_fence = 0; }
                else if (is_fence(sx, &fc, &fl, &fi)) in_fence = 1;
                j++;
                continue;
            }
            if (in_fence) break;
            Line last = it.v[it.n - 1];
            Mark tmp;
            if (!is_blank(last) && !interrupts(x) && !is_marker(x, &tmp)) {
                Line sx = lstrip(x, xi);
                lpush(&it, sx.s, sx.n);
                j++;
                continue;
            }
            break;
        }
        if (inner_blank) loose = 1;
        if (nitems == capitems) {
            capitems = capitems ? capitems * 2 : 8;
            items = (Lines *)realloc(items, sizeof(Lines) * (size_t)capitems);
            if (!items) abort();
        }
        items[nitems++] = it;

        int k = j;
        while (k < n && is_blank(L[k])) k++;
        Mark m2;
        if (k < n && is_marker(L[k], &m2) && m2.ordered == m0.ordered && m2.ch == m0.ch && !is_hr(L[k])) {
            if (k > j) loose = 1;
            i = k;
            continue;
        }
        i = j;
        break;
    }

    /* render */
    int digits = 1;
    for (int v = m0.start + nitems - 1; v >= 10; v /= 10) digits++;
    int hang = m0.ordered ? Z(260 + 110 * digits) : Z(340);
    Ctx c2 = cx;
    c2.indent = cx.indent + hang;
    c2.tight = !loose;
    c2.list_level = cx.list_level + 1;
    static const char *GLYPH[3] = { "\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA" }; /* • ◦ ▪ */

    for (int k = 0; k < nitems; k++) {
        Lines *it = &items[k];
        if (m0.ordered) snprintf(r->bullet, sizeof r->bullet, "%d%c", m0.start + k, m0.ch);
        else snprintf(r->bullet, sizeof r->bullet, "%s", GLYPH[cx.list_level % 3]);
        if (it->n > 0) {
            Line f = it->v[0];
            if (f.n >= 3 && f.s[0] == '[' && f.s[2] == ']' && (f.n == 3 || f.s[3] == ' ') &&
                (f.s[1] == ' ' || f.s[1] == 'x' || f.s[1] == 'X')) {
                snprintf(r->bullet, sizeof r->bullet, "%s",
                         f.s[1] == ' ' ? "\xE2\x98\x90" : "\xE2\x98\x91"); /* ☐ ☑ */
                it->v[0] = lstrip(f, f.n > 3 ? 4 : 3);
            }
        }
        r->has_bullet = 1;
        r->bullet_hang = hang;
        blocks(r, it->v, it->n, c2);
        flush_bullet(r, c2);
        lfree(it);
    }
    free(items);
    if (!loose && cx.list_level == 0) emit_spacer(r, Z(12));
    return i;
}

static void blocks(R *r, Line *L, int n, Ctx cx)
{
    if (++r->depth > 60) { r->depth--; return; }
    int i = 0;
    while (i < n) {
        Line l = L[i];
        if (is_blank(l)) { i++; continue; }
        int ind = lead_sp(l);

        /* indented code */
        if (ind >= 4) {
            int j = i, last = i;
            while (j < n && (is_blank(L[j]) || lead_sp(L[j]) >= 4)) { if (!is_blank(L[j])) last = j; j++; }
            Lines cl = {0};
            for (int k = i; k <= last; k++) { Line s = lstrip(L[k], 4); lpush(&cl, s.s, s.n); }
            emit_code(r, cx, cl.v, cl.n);
            lfree(&cl);
            i = last + 1;
            continue;
        }

        /* fenced code */
        char fc; int fl, fi;
        if (is_fence(l, &fc, &fl, &fi)) {
            Lines cl = {0};
            int j = i + 1;
            while (j < n && !is_fence_close(L[j], fc, fl)) {
                int sp = lead_sp(L[j]);
                Line s = lstrip(L[j], sp < fi ? sp : fi);
                lpush(&cl, s.s, s.n);
                j++;
            }
            emit_code(r, cx, cl.v, cl.n);
            lfree(&cl);
            i = j < n ? j + 1 : n;
            continue;
        }

        int lvl; Line hc;
        if (is_atx(l, &lvl, &hc)) { emit_heading(r, cx, lvl, hc.s, hc.n); i++; continue; }
        if (is_hr(l)) { emit_hr(r, cx); i++; continue; }

        /* block quote */
        if (is_quote(l)) {
            Lines q = {0};
            int j = i;
            while (j < n) {
                if (is_quote(L[j])) { Line s = strip_quote(L[j]); lpush(&q, s.s, s.n); }
                else if (!is_blank(L[j]) && q.n > 0 && !is_blank(q.v[q.n - 1]) && !interrupts(L[j]))
                    lpush(&q, L[j].s, L[j].n);
                else break;
                j++;
            }
            Ctx c2 = cx;
            c2.quote++;
            c2.indent += Z(300);
            c2.tight = 0;
            blocks(r, q.v, q.n, c2);
            lfree(&q);
            i = j;
            continue;
        }

        Mark mk;
        if (is_marker(l, &mk)) { i = parse_list(r, L, n, i, cx); continue; }

        int ncols, al[MAXCOLS];
        if (i + 1 < n && table_start(L[i], L[i + 1], &ncols, al)) {
            i = parse_table(r, L, n, i, cx, ncols, al);
            continue;
        }

        if (lstarts(lstrip(l, ind), "<!--")) {
            int j = i;
            while (j < n && !line_has(L[j], "-->")) j++;
            i = j < n ? j + 1 : n;
            continue;
        }

        if (parse_refdef(r, l, 0)) { i++; continue; }

        /* paragraph (maybe setext heading) */
        int j = i + 1, setext = 0;
        while (j < n) {
            Line x = L[j];
            if (is_blank(x)) break;
            if ((setext = is_setext(x)) != 0) break;
            if (interrupts(x)) break;
            int nc2, al2[MAXCOLS];
            if (j + 1 < n && table_start(x, L[j + 1], &nc2, al2)) break;
            j++;
        }
        Buf p = {0};
        for (int k = i; k < j; k++) {
            if (k > i) bputc(&p, '\n');
            Line s = lstrip(L[k], lead_sp(L[k]));
            bput(&p, s.s, (size_t)s.n);
        }
        if (setext) {
            while (p.n > 0 && (p.p[p.n - 1] == ' ' || p.p[p.n - 1] == '\t')) p.n--;
            emit_heading(r, cx, setext, p.p, (int)p.n);
            i = j + 1;
        } else {
            emit_para(r, cx, p.p ? p.p : "", (int)p.n);
            i = j;
        }
        bfree(&p);
    }
    r->depth--;
}

/* ------------------------------------------------------------------ */
/* document                                                            */
/* ------------------------------------------------------------------ */
static void rtf_header(R *r)
{
    Buf *b = &r->out;
    const unsigned *col = r->col;
    bputs(b, "{\\rtf1\\ansi\\deff0\\uc1\\deflang1041{\\fonttbl{\\f0\\fnil\\fcharset128 ");
    rtf_text(b, r->o->font_body, (int)strlen(r->o->font_body));
    bputs(b, ";}{\\f1\\fmodern\\fcharset0 ");
    rtf_text(b, r->o->font_code, (int)strlen(r->o->font_code));
    bputs(b, ";}}\n{\\colortbl;");
    for (int k = 1; k < C_COUNT; k++)
        bprintf(b, "\\red%u\\green%u\\blue%u;", (col[k] >> 16) & 255, (col[k] >> 8) & 255, col[k] & 255);
    bputs(b, "}\n\\viewkind4\n");
}

/* normalizes newlines and expands tabs (tab stop 4) */
static char *preprocess(const char *src, size_t len, Lines *lines)
{
    Buf b = {0};
    size_t i = 0;
    if (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) i = 3;
    int col = 0;
    for (; i < len; i++) {
        char ch = src[i];
        if (ch == '\r') { if (i + 1 < len && src[i + 1] == '\n') continue; ch = '\n'; }
        if (ch == '\n') { bputc(&b, '\n'); col = 0; continue; }
        if (ch == '\t') { int k = 4 - (col % 4); while (k--) { bputc(&b, ' '); col++; } continue; }
        if (ch == 0) ch = ' ';
        bputc(&b, ch);
        if (((unsigned char)ch & 0xC0) != 0x80) col++;
    }
    char *text = bdetach(&b);
    size_t n = strlen(text);
    size_t st = 0;
    for (size_t k = 0; k <= n; k++) {
        if (k == n || text[k] == '\n') {
            if (k == n && st == n) break;
            lpush(lines, text + st, (int)(k - st));
            st = k + 1;
        }
    }
    return text;
}

static void collect_refs(R *r, Lines *L)
{
    char fc = 0; int fl = 0, fi, in_fence = 0;
    for (int i = 0; i < L->n; i++) {
        Line l = L->v[i];
        while (is_quote(l)) l = strip_quote(l);
        Line t = lstrip(l, lead_sp(l) > 3 ? 0 : lead_sp(l));
        if (in_fence) { if (is_fence_close(t, fc, fl)) in_fence = 0; continue; }
        if (is_fence(t, &fc, &fl, &fi)) { in_fence = 1; continue; }
        Mark m;
        if (is_marker(l, &m)) l = lstrip(l, m.content);
        parse_refdef(r, l, 1);
    }
}

static void r_init(R *r, const MdOptions *opt)
{
    memset(r, 0, sizeof *r);
    r->o = opt;
    r->col = THEME[opt->dark ? 1 : 0];
    r->fs = opt->body_halfpt * opt->scale_pct / 100;
    if (r->fs < 8) r->fs = 8;
}

static void r_finish(R *r, MdDoc *out)
{
    bputs(&r->out, "}");
    out->rtf_len = r->out.n;
    out->rtf = bdetach(&r->out);
    out->links = r->links;   out->nlinks = r->nlinks;
    out->heads = r->heads;   out->nheads = r->nheads;
    for (int i = 0; i < r->nrefs; i++) { free(r->refs[i].label); free(r->refs[i].url); }
    free(r->refs);
}

int md_to_rtf(const char *src, size_t len, const MdOptions *opt, MdDoc *out)
{
    R r;
    memset(out, 0, sizeof *out);
    r_init(&r, opt);
    Lines lines = {0};
    char *text = preprocess(src, len, &lines);
    collect_refs(&r, &lines);
    rtf_header(&r);
    Ctx cx = { 0, 0, 0, 0 };
    blocks(&r, lines.v, lines.n, cx);
    bprintf(&r.out, "\\pard\\plain\\fs%d\\par\n", r.fs / 2);
    r_finish(&r, out);
    lfree(&lines);
    free(text);
    return 1;
}

int text_to_rtf(const char *src, size_t len, const MdOptions *opt, MdDoc *out)
{
    R r;
    memset(out, 0, sizeof *out);
    r_init(&r, opt);
    Lines lines = {0};
    char *text = preprocess(src, len, &lines);
    rtf_header(&r);
    bprintf(&r.out, "\\pard\\plain\\sl0\\f1\\fs%d\\cf%d ", r.fs * 9 / 10, C_TEXT);
    for (int i = 0; i < lines.n; i++) {
        rtf_text(&r.out, lines.v[i].s, lines.v[i].n);
        bputs(&r.out, "\\par\n");
    }
    r_finish(&r, out);
    lfree(&lines);
    free(text);
    return 1;
}

void md_doc_free(MdDoc *d)
{
    if (!d) return;
    free(d->rtf);
    for (int i = 0; i < d->nlinks; i++) { free(d->links[i].url); free(d->links[i].text); }
    for (int i = 0; i < d->nheads; i++) { free(d->heads[i].slug); free(d->heads[i].text); }
    free(d->links);
    free(d->heads);
    memset(d, 0, sizeof *d);
}
