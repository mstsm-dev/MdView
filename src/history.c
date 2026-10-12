/*
 * history.c — 「戻る」の履歴
 */
#include "history.h"

#include <stdlib.h>
#include <string.h>

void history_push(History *h, const wchar_t *path, LONG cp)
{
    if (h->count == HISTORY_MAX) {
        free(h->items[0].path);
        memmove(h->items, h->items + 1, sizeof(HistoryEntry) * (HISTORY_MAX - 1));
        h->count--;
    }
    h->items[h->count].path = str_dup(path);
    h->items[h->count].cp = cp;
    h->count++;
}

bool history_pop(History *h, HistoryEntry *out)
{
    if (h->count == 0) return false;
    *out = h->items[--h->count];
    return true;
}

bool history_empty(const History *h)
{
    return h->count == 0;
}

void history_clear(History *h)
{
    for (int i = 0; i < h->count; ++i) free(h->items[i].path);
    h->count = 0;
}
