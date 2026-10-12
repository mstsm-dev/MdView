/*
 * md2rtf_check.c — Markdown 変換の自己テスト（make check）
 *
 * 引数の各 Markdown ファイルを、倍率・テーマ・ソース表示の組み合わせで RTF に変換し、
 * 次を検査する。Windows に依存しないため、ホストの gcc でビルドして実行できる。
 *   - RTF が "{\rtf1" で始まり、波かっこの対応が取れている（途中で 0 未満にならない）
 *   - リンク・見出しの一覧の文字列が NULL でない
 */
#include "md2rtf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (s && fread(s, 1, (size_t)n, f) != (size_t)n) {
        free(s);
        s = NULL;
    }
    fclose(f);
    if (s) {
        s[n] = '\0';
        *length = (size_t)n;
    }
    return s;
}

static int check_rtf(const MdDoc *d, char *why, size_t whyLength)
{
    if (d->rtf_len < 6 || strncmp(d->rtf, "{\\rtf1", 6) != 0) {
        snprintf(why, whyLength, "header");
        return 0;
    }
    long depth = 0;
    for (size_t i = 0; i < d->rtf_len; i++) {
        if (d->rtf[i] == '\\') { i++; continue; }
        if (d->rtf[i] == '{') depth++;
        if (d->rtf[i] == '}' && --depth < 0) {
            snprintf(why, whyLength, "unbalanced '}' at %zu", i);
            return 0;
        }
    }
    if (depth != 0) {
        snprintf(why, whyLength, "unclosed '{' (depth %ld)", depth);
        return 0;
    }
    for (int i = 0; i < d->nlinks; i++) {
        if (!d->links[i].url || !d->links[i].text) { snprintf(why, whyLength, "link %d", i); return 0; }
    }
    for (int i = 0; i < d->nheads; i++) {
        if (!d->heads[i].slug || !d->heads[i].text) { snprintf(why, whyLength, "heading %d", i); return 0; }
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const struct { int scale, dark, raw; } CASES[] = {
        { 100, 0, 0 }, { 150, 1, 0 }, { 50, 0, 0 }, { 300, 1, 0 }, { 100, 0, 1 },
    };
    int failures = 0, runs = 0;

    for (int a = 1; a < argc; a++) {
        size_t length = 0;
        char *src = read_all(argv[a], &length);
        if (!src) {
            printf("NG   %s: cannot read\n", argv[a]);
            failures++;
            continue;
        }
        for (size_t c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
            MdOptions o = { CASES[c].scale, 9000, CASES[c].dark, "Yu Gothic UI", "Consolas", 22, NULL };
            MdDoc d;
            if (CASES[c].raw) text_to_rtf(src, length, &o, &d);
            else md_to_rtf(src, length, &o, &d);
            char why[96] = "";
            runs++;
            if (!check_rtf(&d, why, sizeof why)) {
                printf("NG   %s (zoom %d, dark %d, raw %d): %s\n", argv[a], CASES[c].scale, CASES[c].dark, CASES[c].raw, why);
                failures++;
            } else if (c == 0) {
                printf("OK   %s (%zu bytes RTF, %d links, %d headings)\n", argv[a], d.rtf_len, d.nlinks, d.nheads);
            }
            md_doc_free(&d);
        }
        free(src);
    }
    printf("%d / %d passed\n", runs - failures, runs);
    return failures ? 1 : 0;
}
