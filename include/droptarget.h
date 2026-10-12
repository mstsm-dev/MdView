/*
 * droptarget.h — ファイルのドラッグ＆ドロップを受け付ける OLE ドロップターゲット
 *
 * RichEdit は独自の OLE ドロップターゲットを登録し、読み取り専用のときはファイルを
 * 拒否する（そのため WM_DROPFILES も届かない）。これを差し替えて CF_HDROP を受け付け、
 * 先頭のファイルのパスを WM_APP_OPENFILE で通知する。OleInitialize 済みであること。
 */
#ifndef MDV_DROPTARGET_H
#define MDV_DROPTARGET_H

#include "common.h"

/* window の既存のドロップターゲットを外して登録する。ドロップ時は notify へ通知。 */
bool droptarget_register(HWND window, HWND notify);

void droptarget_revoke(HWND window);

#endif /* MDV_DROPTARGET_H */
