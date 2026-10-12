/*
 * find.h — 検索（標準の「検索」ダイアログ FindTextW と、次・前を検索）
 */
#ifndef MDV_FIND_H
#define MDV_FIND_H

#include "common.h"

#include <commdlg.h>

#define FIND_TEXT_MAX 256

typedef struct {
    HWND dialog;               /* 表示中のモードレスダイアログ。なければ NULL */
    UINT message;              /* FINDMSGSTRING の登録メッセージ */
    FINDREPLACEW fr;
    wchar_t text[FIND_TEXT_MAX];
} FindState;

void find_init(FindState *f);

/* 検索ダイアログを開く（表示中なら前面へ）。短い選択文字列があれば検索語にする。 */
void find_show(FindState *f, HWND owner, HWND edit);

/* 次（down = true）／前を検索する。末尾・先頭で折り返す。検索語が空ならダイアログを開く。 */
void find_next(FindState *f, HWND owner, HWND edit, bool down);

/* ウィンドウプロシージャから呼ぶ。検索ダイアログからの通知なら処理して true。 */
bool find_handle_message(FindState *f, HWND owner, HWND edit, UINT message, LPARAM lParam);

/* メッセージループから呼ぶ。ダイアログ向けのキー入力なら処理して true。 */
bool find_translate(FindState *f, MSG *msg);

#endif /* MDV_FIND_H */
