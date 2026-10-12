/*
 * links.h — リンクの取得・分類・解決（仕様書 4.2 節）
 *
 * 実際に開く処理（メッセージボックスや文書の切替）は app.c の app_follow_link が担当し、
 * ここは判定と文字列処理だけを行う。
 */
#ifndef MDV_LINKS_H
#define MDV_LINKS_H

#include "common.h"
#include "md2rtf.h"

#include <richedit.h>

typedef enum {
    LINK_ANCHOR,        /* "#見出し" */
    LINK_WEB,           /* http: https: mailto: ftp:（そのまま開く） */
    LINK_OTHER_SCHEME,  /* その他のスキーム（確認してから開く） */
    LINK_WWW,           /* "www." で始まる（http:// を補う） */
    LINK_LOCAL          /* ローカルファイル（文書からの相対パスなど） */
} LinkKind;

/* ローカルリンクを解決した結果 */
typedef struct {
    wchar_t *path;      /* フルパス（呼び出し側で link_local_free） */
    char *fragment;     /* "#" 以降（UTF-8、% デコード前）。なければ NULL */
    bool emptyPath;     /* パス部分が空だった（"?x#frag" など。fragment だけを使う） */
} LocalLink;

/*
 * EN_LINK 通知から、クリックされたリンク先を取り出す（呼び出し側で free）。
 * 左ボタンを離したとき以外・文字列を選択中・取り出せないときは NULL。
 * 取り出し順：フィールド命令の HYPERLINK "..." → doc のリンク表（表示文字列で照合）→ 表示文字列が URL。
 */
wchar_t *link_target_from_click(HWND edit, const ENLINK *link, const MdDoc *doc);

LinkKind link_classify(const wchar_t *url);

/* ローカルリンクをフルパスに解決する。documentPath が NULL ならカレントディレクトリ基準。 */
bool link_resolve_local(const wchar_t *url, const wchar_t *documentPath, LocalLink *out);
void link_local_free(LocalLink *link);

/*
 * fragment（UTF-8、% エンコード可）に対応する見出しの文字位置を探す。
 * 見出しの一覧にない名前なら false。見つかったが本文上で位置を特定できなければ *cp = -1。
 */
bool link_find_anchor(HWND edit, const MdDoc *doc, const char *fragment, LONG *cp);

#endif /* MDV_LINKS_H */
