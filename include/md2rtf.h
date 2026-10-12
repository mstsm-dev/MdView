/*
 * md2rtf.h — Markdown（CommonMark + GFM のサブセット）→ RichEdit 用 RTF 変換
 *
 * Windows に依存しない C11。入出力の文字列はすべて UTF-8。
 * 実装は md2rtf.c（公開 API）・md_block.c・md_inline.c、内部定義は md_internal.h。
 * 対応する記法は仕様書 2章、RTF への対応づけは 6章を参照。
 */
#ifndef MDV_MD2RTF_H
#define MDV_MD2RTF_H

#include <stddef.h>

typedef struct {
    int         scale_pct;    /* 表示倍率 %（100 = 標準）。文字サイズ・余白に掛ける        */
    int         width_twips;  /* 表・コードブロックに使える幅（twips）                      */
    int         dark;         /* 0 = ライトテーマ、1 = ダークテーマ                         */
    const char *font_body;    /* 本文フォント名                                            */
    const char *font_code;    /* 等幅フォント名                                            */
    int         body_halfpt;  /* 本文の文字サイズ（半ポイント。22 = 11pt）                  */
    const char *image_label;  /* 画像の表示ラベル（"[ラベル: alt]"）。NULL なら "画像"        */
} MdOptions;

typedef struct { char *url;  char *text; } MdLink;     /* リンク先 / 表示文字列（平文） */
typedef struct { char *slug; char *text; } MdHeading;  /* アンカー名 / 見出しの平文     */

typedef struct {
    char      *rtf;
    size_t     rtf_len;
    MdLink    *links;  int nlinks;   /* 文書内の出現順 */
    MdHeading *heads;  int nheads;   /* 文書内の出現順 */
} MdDoc;

/* Markdown を RTF に変換する。成功で 1。結果は md_doc_free で解放する。 */
int  md_to_rtf(const char *src, size_t len, const MdOptions *opt, MdDoc *out);

/* 平文（ソース表示）を等幅フォントの RTF に変換する。 */
int  text_to_rtf(const char *src, size_t len, const MdOptions *opt, MdDoc *out);

void md_doc_free(MdDoc *d);

/* テーマの背景色・文字色（0xRRGGBB）。ホスト側で背景色を合わせるために使う。 */
unsigned md_theme_background(int dark);
unsigned md_theme_text(int dark);

#endif /* MDV_MD2RTF_H */
