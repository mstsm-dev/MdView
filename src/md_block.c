/*
 * md_block.c — ブロック要素の変換
 *
 * 行配列（Line）を先頭から読み、ブロックごとに RTF の段落・表を出力する。
 * 引用とリストの中身は、接頭辞（"> " やリスト記号の幅）を外した行配列を作って
 * md_blocks() を再帰呼び出しする。インデント・引用の深さは Ctx で受け渡す。
 *
 * 対応：ATX / Setext 見出し、段落、引用（入れ子）、箇条書き・番号付き・タスクリスト
 *       （入れ子、tight / loose）、フェンス／インデントのコードブロック、GFM の表、
 *       水平線、参照リンク定義、HTML コメント。
 *
 * コードブロック・表・水平線は RichEdit の表（\trowd 〜 \row）で描く。
 * 幅は MdOptions.width_twips（表示領域の幅）から計算する。
 */
#include "md_internal.h"

int md_parse_refdef(R *r, Line l, int store)
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
    if (store && !md_find_ref(r, l.s + ls, le - ls)) {
        if (r->nrefs == r->caprefs) {
            r->caprefs = r->caprefs ? r->caprefs * 2 : 16;
            r->refs = (Ref *)realloc(r->refs, sizeof(Ref) * (size_t)r->caprefs);
            if (!r->refs) abort();
        }
        r->refs[r->nrefs].label = md_norm_label(l.s + ls, le - ls);
        r->refs[r->nrefs].url   = xstrndup(l.s + ds, (size_t)(de - ds));
        r->nrefs++;
    }
    return 1;
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
    char *pl = md_inline_plain(r, s, n);
    int empty = 1;
    for (char *p = pl; *p; p++) if (!is_ws((unsigned char)*p)) { empty = 0; break; }
    free(pl);
    if (empty && !r->has_bullet) return;
    para_begin(r, cx, 0, para_sa(r, cx), r->fs, C_TEXT, 0);
    md_inline_rtf(r, s, n);
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
    char *pl = md_inline_plain(r, s, n);
    add_heading(r, pl);
    free(pl);
    para_begin(r, cx, Z(sb[lvl]), Z(lvl <= 2 ? 200 : 120), fs, lvl == 6 ? C_H6 : C_HEAD, 1);
    if (lvl <= 2) bprintf(&r->out, "\\brdrb\\brdrs\\brdrw10\\brsp%d\\brdrcf%d ", Z(80), C_BORDER);
    bputs(&r->out, "\\b ");
    md_inline_rtf(r, s, n);
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
            char *pl = md_inline_plain(r, cells[k].s, cells[k].n);
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
            if (k < cn) md_inline_rtf(r, cells[k].s, cells[k].n);
            bputs(b, "\\cell");
        }
        bputs(b, "\\row\n");
    }
    bputs(b, "\\pard\n");
    emit_spacer(r, Z(16));
    return end;
}


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
        md_blocks(r, it->v, it->n, c2);
        flush_bullet(r, c2);
        lfree(it);
    }
    free(items);
    if (!loose && cx.list_level == 0) emit_spacer(r, Z(12));
    return i;
}

void md_blocks(R *r, Line *L, int n, Ctx cx)
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
            md_blocks(r, q.v, q.n, c2);
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

        if (md_parse_refdef(r, l, 0)) { i++; continue; }

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

void md_collect_refs(R *r, Lines *L)
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
        md_parse_refdef(r, l, 1);
    }
}
