/*
 * md2rtf.h - Markdown (CommonMark + GFM subset) to RichEdit RTF converter
 *
 * Pure C, no Windows dependency. Input / output strings are UTF-8.
 */
#ifndef MD2RTF_H
#define MD2RTF_H

#include <stddef.h>

typedef struct {
    int         scale_pct;    /* zoom in percent (100 = normal)             */
    int         width_twips;  /* usable width for tables / code blocks      */
    int         dark;         /* 0 = light theme, 1 = dark theme            */
    const char *font_body;    /* body font name (UTF-8)                     */
    const char *font_code;    /* monospace font name (UTF-8)                */
    int         body_halfpt;  /* body font size in half-points (22 = 11pt)  */
} MdOptions;

typedef struct { char *url;  char *text; } MdLink;     /* link target / shown text */
typedef struct { char *slug; char *text; } MdHeading;  /* anchor slug / heading    */

typedef struct {
    char      *rtf;
    size_t     rtf_len;
    MdLink    *links;  int nlinks;
    MdHeading *heads;  int nheads;
} MdDoc;

/* Converts Markdown to RTF. Returns 1 on success. Free with md_doc_free. */
int  md_to_rtf(const char *src, size_t len, const MdOptions *opt, MdDoc *out);

/* Converts plain text (source view) to RTF with the monospace font. */
int  text_to_rtf(const char *src, size_t len, const MdOptions *opt, MdDoc *out);

void md_doc_free(MdDoc *d);

/* Theme colors (0xRRGGBB) so the host can match the background. */
unsigned md_theme_background(int dark);
unsigned md_theme_text(int dark);

#endif
