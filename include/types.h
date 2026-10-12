/*
 * types.h — 複数モジュールで共有する列挙型
 */
#ifndef MDV_TYPES_H
#define MDV_TYPES_H

/* 再表示（RTF の流し込み）後のスクロール位置の決め方 */
typedef enum {
    SCROLL_TOP,         /* 先頭へ（別の文書を開いたとき・ソース表示の切替） */
    SCROLL_KEEP_TEXT,   /* 表示先頭の文字を維持（倍率・テーマ・幅の変更：本文は同じで位置だけ変わる） */
    SCROLL_KEEP_PIXELS  /* スクロール量を維持（再読み込み：本文が変わるので文字位置は当てにならない） */
} ScrollKeep;

#endif /* MDV_TYPES_H */
