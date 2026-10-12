/*
 * md2rtf.c — Markdown（CommonMark + GFM のサブセット）→ RichEdit 用 RTF 変換の公開 API
 *
 * 処理の流れ（仕様書 6章）
 *   preprocess()      改行の正規化（CRLF/CR→LF）、タブ展開（4桁）、行配列の作成
 *   md_collect_refs() 文書全体から参照リンク定義 [label]: url を集める
 *   rtf_header()      フォント表・色表
 *   md_blocks()       ブロック要素 → インライン要素の順に RTF を出力（md_block.c / md_inline.c）
 *
 * 出力の RTF は ASCII のみ（非 ASCII は \uN? でエスケープ）。
 * 配色は THEME 表で一元管理し、表の列は md_internal.h の C_* と対応する。
 */
#include "md_internal.h"

/* ------------------------------------------------------------------ */
/* theme（列は C_TEXT〜C_H6 の順。0 列目は RTF 色表の既定色で未使用）    */
/* ------------------------------------------------------------------ */
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
    md_collect_refs(&r, &lines);
    rtf_header(&r);
    Ctx cx = { 0, 0, 0, 0 };
    md_blocks(&r, lines.v, lines.n, cx);
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
