/*
 * history.h — 「戻る」の履歴（文書のパスと表示位置のスタック）
 */
#ifndef MDV_HISTORY_H
#define MDV_HISTORY_H

#include "common.h"

#define HISTORY_MAX 32 /* 超えたら最も古いものから捨てる */

typedef struct {
    wchar_t *path;
    LONG cp;          /* 表示先頭の文字位置 */
} HistoryEntry;

typedef struct {
    HistoryEntry items[HISTORY_MAX];
    int count;
} History;

void history_push(History *h, const wchar_t *path, LONG cp);

/* 最新の項目を取り出す。空なら false。取り出した path は呼び出し側で free。 */
bool history_pop(History *h, HistoryEntry *out);

bool history_empty(const History *h);
void history_clear(History *h);

#endif /* MDV_HISTORY_H */
