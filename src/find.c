/*
 * find.c — 検索（標準の「検索」ダイアログと、次・前を検索）
 *
 * ダイアログはモードレス。キー入力は main.c のメッセージループから find_translate で渡し、
 * 「次を検索」の通知（FINDMSGSTRING）は window.c から find_handle_message で受け取る。
 */
#include "find.h"
#include "viewer.h"

#include <stdlib.h>
#include <string.h>

#define FIND_SELECTION_MAX 250 /* これより短い選択文字列を検索語の初期値にする */

void find_init(FindState *f)
{
    memset(f, 0, sizeof(*f));
    f->message = RegisterWindowMessageW(FINDMSGSTRINGW);
}

void find_show(FindState *f, HWND owner, HWND edit)
{
    if (f->dialog) {
        SetFocus(f->dialog);
        return;
    }
    CHARRANGE sel = viewer_selection(edit);
    if (sel.cpMax > sel.cpMin && sel.cpMax - sel.cpMin < FIND_SELECTION_MAX) {
        wchar_t *text = viewer_text_range(edit, sel);
        if (text) {
            lstrcpynW(f->text, text, FIND_TEXT_MAX);
            free(text);
        }
    }
    memset(&f->fr, 0, sizeof(f->fr));
    f->fr.lStructSize = sizeof(f->fr);
    f->fr.hwndOwner = owner;
    f->fr.lpstrFindWhat = f->text;
    f->fr.wFindWhatLen = FIND_TEXT_MAX;
    f->fr.Flags = FR_DOWN;
    f->dialog = FindTextW(&f->fr);
}

void find_next(FindState *f, HWND owner, HWND edit, bool down)
{
    if (!f->text[0]) {
        find_show(f, owner, edit);
        return;
    }
    CHARRANGE sel = viewer_selection(edit);
    DWORD flags = (down ? FR_DOWN : 0) | (f->fr.Flags & (FR_MATCHCASE | FR_WHOLEWORD));

    /* 下方向は選択の終わりから末尾へ、上方向は選択の始まりから先頭へ */
    CHARRANGE range = down ? (CHARRANGE){ sel.cpMax, -1 } : (CHARRANGE){ sel.cpMin, 0 };
    CHARRANGE found;
    LONG cp = viewer_find(edit, f->text, range, flags, &found);
    if (cp < 0) { /* 折り返して全体から探す */
        range = down ? (CHARRANGE){ 0, -1 } : (CHARRANGE){ viewer_text_length(edit), 0 };
        cp = viewer_find(edit, f->text, range, flags, &found);
    }
    if (cp < 0) {
        res_message_boxf(f->dialog ? f->dialog : owner, IDS_FIND_NOT_FOUND, MB_ICONINFORMATION, f->text);
        return;
    }
    viewer_select(edit, found);
}

bool find_handle_message(FindState *f, HWND owner, HWND edit, UINT message, LPARAM lParam)
{
    if (!f->message || message != f->message) return false;
    const FINDREPLACEW *fr = (const FINDREPLACEW *)lParam;
    if (fr->Flags & FR_DIALOGTERM) f->dialog = NULL;
    else if (fr->Flags & FR_FINDNEXT) find_next(f, owner, edit, (fr->Flags & FR_DOWN) != 0);
    return true;
}

bool find_translate(FindState *f, MSG *msg)
{
    return f->dialog && IsDialogMessageW(f->dialog, msg);
}
