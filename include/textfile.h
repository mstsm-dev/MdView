/*
 * textfile.h — テキストファイルの読み込み（文字コード判別）と Markdown 拡張子の一覧
 */
#ifndef MDV_TEXTFILE_H
#define MDV_TEXTFILE_H

#include "common.h"

#define TEXTFILE_MAX_BYTES (64 * 1024 * 1024) /* これより大きいファイルは開かない */

/* 変更検知に使う、ファイルの更新日時とサイズ */
typedef struct {
    FILETIME time;
    DWORD size;
} FileStamp;

/*
 * ファイルを読み込み UTF-8 で返す（呼び出し側で free）。仕様書 4.1 節：
 * UTF-8（BOM の有無）、UTF-16 LE/BE（BOM 付き）、それ以外は ANSI（CP_ACP）として変換する。
 */
bool textfile_read(const wchar_t *path, char **utf8, size_t *length, FileStamp *stamp);

/* ファイルの更新日時またはサイズが stamp と異なるか。属性を取得できなければ false。 */
bool textfile_changed(const wchar_t *path, const FileStamp *stamp);

/* ビューア内で開く拡張子（.md など）か。 */
bool textfile_is_markdown(const wchar_t *path);

/* ファイル選択ダイアログ用のパターン "*.md;*.markdown;..."（呼び出し側で free）。 */
wchar_t *textfile_filter_pattern(void);

#endif /* MDV_TEXTFILE_H */
