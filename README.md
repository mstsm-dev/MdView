# MdView

Windows 向けの軽量 Markdown ビューア。C11 / Win32 API / RichEdit のみで構成し、インストール不要の単一 EXE（約175KB）で動作します。

- 版数：1.3.1
- 仕様：`MdView_仕様書_v1.2.md`
- 変更履歴：`CHANGELOG.md`
- ライセンス：The Unlicense（パブリックドメイン。`LICENSE`）

## 生成AIによる作成について

本ソフトウェアのソースコード、リソース、仕様書、ドキュメントは、**生成AIを用いて作成**しました。

| 工程 | 使用した AI |
|---|---|
| 初版のソースコード（1.0.0）、アイコン、ドラッグ＆ドロップの修正 | Anthropic Claude |
| ImageViewerLite の構成に合わせたリファクタリング（1.1.0）、ステータスバー・INI 化・描画の修正（1.2.0）、エディタの指定（1.3.0）、本 README・CHANGELOG・仕様書 | Anthropic Claude |

要件の決定と成果物の採否は利用者（作成者）が行っていますが、コードは人手による全面的なレビューを経ていません。次の「諸注意」を確認したうえで利用してください。

## ビルド（MSYS2 UCRT64）

MSYS2 の **UCRT64** ターミナルで実行します（`which gcc` が `/ucrt64/bin/gcc` であること）。

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-binutils make
make          # dist/MdView.exe を生成
make dist     # INI も dist/ にコピー
make check    # Markdown 変換の自己テスト
make DEBUG=1  # デバッグビルド（-O0 -g、シンボルを残す）
make clean
```

Linux からのクロスビルドは `make CROSS=x86_64-w64-mingw32-` で行えます。

## 主な操作

| キー・マウス | 操作 |
|---|---|
| Ctrl+O / ドロップ / 第1引数 | ファイルを開く |
| F5 / Ctrl+R | 再読み込み |
| Ctrl+E | エディタで開く（INI の `[Editor]` で指定。下記） |
| Alt+← / BackSpace / マウスの戻るボタン | 前の文書に戻る |
| Ctrl+F / F3 / Shift+F3 | 検索 / 次を検索 / 前を検索 |
| Ctrl+ホイール / Ctrl++ / Ctrl+- / Ctrl+0 | 拡大 / 縮小 / 標準サイズ |
| Ctrl+D | ダークテーマ |
| Ctrl+U | ソース表示 |
| Ctrl+W | 終了 |
| リンクをクリック | 外部リンクはブラウザ、`#見出し` はジャンプ、`.md` はビューア内で開く |

ファイルが更新されると1秒程度で自動的に再読み込みします（スクロール位置は維持）。ステータスバーにはファイルサイズと行数を表示します。

## ソース構成

```text
MdView/
├── Makefile
├── README.md / CHANGELOG.md / MdView_仕様書_v1.2.md
├── LICENSE                    The Unlicense
├── MdView.ini                 既定の設定ファイル（EXE と同じフォルダに置く）
├── include/
│   ├── common.h               Windows ヘッダ設定・共通ユーティリティ
│   ├── version.h              版数（C とリソースで共用）
│   ├── resource.h             リソース ID
│   ├── types.h                ScrollKeep
│   ├── app.h                  アプリ全体の状態とモジュール間 API
│   ├── md2rtf.h               Markdown 変換の公開 API（Windows 非依存）
│   ├── md_internal.h          Markdown 変換の内部定義
│   └── (各モジュールのヘッダ)
├── src/
│   ├── main.c                 起動・終了、メッセージループ
│   ├── window.c               ウィンドウプロシージャ、RichEdit のサブクラス、ステータスバー、ドロップ
│   ├── commands.c             メニュー／ショートカットのコマンド処理、メニューの状態表示
│   ├── app.c                  開く・再読み込み・戻る・表示・倍率・リンク
│   ├── viewer.c               RichEdit のラッパー（流し込み・スクロール・検索）
│   ├── find.c                 検索ダイアログ
│   ├── links.c                リンクの取り出し・分類・解決
│   ├── history.c              戻るの履歴
│   ├── droptarget.c           OLE ドロップターゲット
│   ├── editor.c               エディタで開く（INI の [Editor]、%1 の置き換え）
│   ├── textfile.c             ファイル読み込み・文字コード判別・拡張子の一覧
│   ├── settings.c             INI 読み書き（表駆動）
│   ├── util.c                 文字列変換・リソース文字列
│   ├── md2rtf.c               Markdown 変換：公開 API・配色・前処理
│   ├── md_block.c             Markdown 変換：ブロック要素
│   └── md_inline.c            Markdown 変換：インライン要素
├── res/
│   ├── resource.rc            メニュー・ショートカット・文言・版数情報
│   ├── manifest.xml           DPI・コモンコントロール v6
│   ├── welcome.md             ファイル未指定時に表示する文書
│   └── MdView.ico
├── samples/sample.md          表示確認用（対応記法をひととおり含む）
└── tests/md2rtf_check.c       Markdown 変換の自己テスト
```

### よくある変更の手順

| やりたいこと | 変更箇所 |
|---|---|
| 版数を上げる | `include/version.h` のみ（`res/manifest.xml` の version も合わせると親切） |
| ショートカットを変更・追加 | `res/resource.rc` の `ACCELERATORS`（とメニューの表記） |
| メニューを追加 | `resource.h` に ID、`resource.rc` に項目、`commands.c` の `commands_execute` に処理 |
| 文言を変更 | `res/resource.rc` の `STRINGTABLE` |
| 初期表示の文書を変更 | `res/welcome.md` |
| INI の項目を追加 | `Settings` にフィールド、`src/settings.c` の `KEY_TABLE` に1行 |
| ステータスバーの表示内容を変更 | `src/app.c` の `app_update_status`（書式は `STRINGTABLE` の `IDS_STATUS_*`、欄の幅は `src/window.c` の `STATUS_SIZE_WIDTH`） |
| エディタの起動方法を変更 | `src/editor.c`（引数の組み立ては `editor_build_args`、パスの解決は `editor_resolve_path`） |
| ビューア内で開く拡張子を追加 | `src/textfile.c` の `MARKDOWN_EXTENSIONS` に1行（フィルタ名の文言も合わせる） |
| 配色を変更 | `src/md2rtf.c` の `THEME` 表（背景色は `md_theme_background`） |
| 見出し・段落の大きさや間隔を変更 | `src/md_block.c` の `emit_heading` / `para_begin` |
| 記法を追加 | ブロックは `src/md_block.c` の `md_blocks`、インラインは `src/md_inline.c` の `inl`。変更後は `make check` |

## 諸注意

### 品質・検証状況

- **Windows 実機での動作は未検証です。** 検証は Linux 上の MinGW-w64 によるクロスビルド（`-Wpedantic` を含め警告ゼロ）と、Wine 9.0 上での確認に限られます。
- 1.0.0 からのリファクタリングでは、Markdown 変換の出力が 1,628 件の入力でバイト単位で一致すること、Wine 上で同じ操作をした画面・タイトル・保存される設定が一致することを確認しています（仕様書 16.2 節）。
- **1.2.0 の「本文の下端に以前の表示の断片が残る」不具合の修正は未確認です。** Wine では修正前の版でも再現しなかったため、原因の分析（仕様書 7.1 節）に基づいて修正しています。実機で、表を含む文書をウィンドウの高さを変えながらスクロールして確認してください。
- **リンクのクリックは未確認です。** Wine の RichEdit がリンク（HYPERLINK フィールド）を表示しないためです。実機で `samples/sample.md` のリンクを確認してください。
- 表・コードブロック・見出しの下線・引用の罫線の見た目は RichEdit の版に依存します。日本語フォントでの表示、UTF-16・Shift_JIS の判別、MSYS2 UCRT64 でのビルドも実機で確認してください。
- 生成 AI の出力には、もっともらしく見えて誤っている箇所が含まれることがあります。重要な用途に使う前に、コードを読んで確認してください。

### 利用上の注意

- 本ソフトウェアは現状のまま提供され、動作や結果について一切保証しません。利用によって生じた損害について作成者は責任を負いません。
- ライセンスは The Unlicense です（末尾の「ライセンス」を参照）。
- EXE にはコード署名をしていないため、初回起動時に SmartScreen の警告が出ることがあります。
- 設定は EXE と同じフォルダの `MdView.ini` に保存します。`C:\Program Files` など書き込めない場所に置くと設定を保存できません。フォントなどは MdView を終了してから INI を直接編集してください（項目は仕様書 8.1 節）。

  ```ini
  [Display]
  FontBody=Meiryo UI
  FontSize=12
  ```

- 「エディタで開く」（Ctrl+E）のエディタは `[Editor]` で指定します。`Path` にフルパス、`Args` に引数を書き、`%1` が文書のフルパスに置き換わります（空白を含むパスに備えて `"%1"` と書いてください）。`Path` が空ならメモ帳などの関連付けで開きます（仕様書 4.5 節）。

  ```ini
  [Editor]
  Path=%LOCALAPPDATA%\Programs\Microsoft VS Code\Code.exe
  Args=--reuse-window "%1"
  ```

- INI は MdView の終了時に書き直すため、手で書いた注釈（`;` の行）は消えます。
- 1.1.0 まではレジストリ `HKCU\Software\MdView` に保存していました。1.2.0 はこれを読み込みません。不要なら `reg delete HKCU\Software\MdView /f` で削除できます。

- 管理者として実行している場合、Windows の仕様（UIPI）により通常権限のエクスプローラーからはドロップできません。通常権限で起動してください。
- 本ソフトウェアはファイルを読み取るだけで、変更・削除は行いません。リンクのクリック以外にネットワーク通信は行いません。

### 機能上の制限

- 編集、HTML / PDF 出力、印刷はありません。
- 画像は埋め込み表示せず、`[画像: 説明]` のリンクとして表示します（クリックで既定のアプリで開く）。
- コードの色分け、脚注、数式、HTML のブロック要素（`<details>` など）の解釈には対応していません（タグを除いた中身を表示）。
- 64MiB を超えるファイルは開けません。

## ライセンス

本ソフトウェア（ソースコード、リソース、ドキュメント、ビルド済み EXE）は [The Unlicense](https://unlicense.org) のもとで公開します。全文は同梱の `LICENSE` を参照してください。

- 著作権を放棄し、パブリックドメインとして提供します。
- 商用・非商用を問わず、複製・改変・公開・販売・再配布を、目的や方法にかかわらず自由に行えます。許諾の手続きや著作権表示は不要です。
- 無保証です。本ソフトウェアの利用によって生じたいかなる損害についても、作者は責任を負いません。
