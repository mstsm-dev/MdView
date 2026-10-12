/*
 * version.h — 版数の唯一の定義場所
 *
 * C ソースとリソーススクリプト（windres）の両方から読み込まれるため、
 * #define 以外の記述（型宣言など）を置かないこと。
 * 版数を上げるときは数値と文字列の両方を更新する。
 */
#ifndef MDV_VERSION_H
#define MDV_VERSION_H

#define MDV_VERSION_MAJOR 1
#define MDV_VERSION_MINOR 3
#define MDV_VERSION_PATCH 0
#define MDV_VERSION_STRING "1.3.0"

#define MDV_APP_NAME_STRING "MdView"
#define MDV_EXE_NAME_STRING "MdView.exe"

#endif /* MDV_VERSION_H */
