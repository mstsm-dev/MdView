/*
 * editor.h — 「エディタで開く」（仕様書 4.5 節）
 *
 * INI の [Editor] Path にエディタのフルパス、Args に引数を書く。
 * Args の %1 は表示中の文書のフルパスに置き換える（空白を含むパスに備えて "%1" と書く）。
 * Path が空なら、関連付けの「編集」、それもなければメモ帳で開く（1.2.0 までの動作）。
 */
#ifndef MDV_EDITOR_H
#define MDV_EDITOR_H

#include "common.h"

/*
 * 引数の文字列を作る（呼び出し側で free）。args の "%1" をすべて file に置き換える。
 * args に %1 がなければ、末尾に空白と "file"（引用符付き）を足す。args が空なら "file" だけ。
 */
wchar_t *editor_build_args(const wchar_t *args, const wchar_t *file);

/*
 * 実行するエディタのパスを作る（呼び出し側で free）。前後の空白と引用符を除き、
 * 環境変数（%LOCALAPPDATA% など）を展開する。相対パスは MdView.exe のフォルダを基準にする。
 * path が空なら NULL。
 */
wchar_t *editor_resolve_path(const wchar_t *path);

/*
 * file をエディタで開く。作業フォルダは file のフォルダ。
 * エディタが見つからない・起動できないときはメッセージを表示して false。
 */
bool editor_open(HWND owner, const wchar_t *editorPath, const wchar_t *editorArgs, const wchar_t *file);

#endif /* MDV_EDITOR_H */
