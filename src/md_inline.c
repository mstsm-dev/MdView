/*
 * md_inline.c — インライン要素の変換と参照リンク定義の検索
 *
 * 対応：強調・太字（* _）、取り消し線（~ ~~）、コードスパン、リンク・画像
 *       （インライン／参照）、自動リンク（<...>・GFM の URL）、強制改行、
 *       バックスラッシュエスケープ、HTML 実体参照、一部のインライン HTML。
 *
 * 出力先は IC.plain で切り替える。0 なら RTF、1 なら平文 UTF-8
 * （見出しのアンカー、表の列幅計算、リンク表の表示文字列に使う）。
 * 平文と RTF で見える文字が一致するようにすること（見出しジャンプが本文検索のため）。
 */
#include "md_internal.h"

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
char *md_norm_label(const char *s, int n)
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

Ref *md_find_ref(R *r, const char *s, int n)
{
    char *k = md_norm_label(s, n);
    Ref *res = NULL;
    for (int i = 0; i < r->nrefs; i++)
        if (strcmp(r->refs[i].label, k) == 0) { res = &r->refs[i]; break; }
    free(k);
    return res;
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

/* 画像は "[ラベル: 代替テキスト]" と表示する（ラベルは MdOptions.image_label） */
static void image_text(IC *c, const char *alt, int n)
{
    static const char DEFAULT_LABEL[] = "\xE7\x94\xBB\xE5\x83\x8F"; /* "画像" */
    const char *label = c->r->o->image_label ? c->r->o->image_label : DEFAULT_LABEL;
    ic_text(c, "[", 1);
    ic_text(c, label, (int)strlen(label));
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
        Ref *rf = md_find_ref(c->r, lab, ll);
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
char *md_inline_plain(R *r, const char *s, int n)
{
    Buf b = {0};
    IC c = { r, &b, 1, 0, 0 };
    inl(&c, s, n);
    return bdetach(&b);
}

void md_inline_rtf(R *r, const char *s, int n)
{
    IC c = { r, &r->out, 0, 0, 0 };
    inl(&c, s, n);
}
