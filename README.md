# MdView

Win32 API と RichEdit (Msftedit.dll) だけで動く、軽量な Markdown ビューアです。
ランタイム不要の単一 exe（約 150KB）で、Windows 7 以降で動作します。

## ビルド (MSYS2)

1. MSYS2 の **UCRT64**（または MINGW64）シェルを開く
2. ツールチェーンを入れる

   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
   ```

3. このフォルダで `make` を実行すると `mdview.exe` ができます

Linux からのクロスビルド: `make CROSS=x86_64-w64-mingw32-`

## 使い方

- `mdview.exe file.md`、ドラッグ＆ドロップ、または Ctrl+O で開きます
- `.md` を右クリック →「プログラムから開く」で関連付けもできます
- 管理者として実行している場合、Windows の仕様（UIPI）により通常権限のエクスプローラーからはドロップできません。通常権限で起動してください
- ファイルが更新されると自動で再読み込みします（スクロール位置は維持）

| キー | 機能 |
|---|---|
| Ctrl+O | 開く |
| F5 / Ctrl+R | 再読み込み |
| Ctrl+E | エディタで開く |
| Ctrl+F / F3 / Shift+F3 | 検索 / 次を検索 / 前を検索 |
| Ctrl+ホイール / Ctrl++ / Ctrl+- / Ctrl+0 | 拡大 / 縮小 / 標準 |
| Ctrl+D | ダークテーマ |
| Ctrl+U | ソース表示 |
| Alt+← / BackSpace / マウスの戻るボタン | 前のファイルに戻る |
| Ctrl+W | 終了 |

リンクの動作:

- `http(s)://`、`mailto:` は既定のブラウザ／メーラーで開きます
- `#見出し` は文書内の見出しへジャンプします
- 相対パスの `.md` はビューア内で開きます（戻る操作に対応）
- それ以外のローカルファイルや未知のスキームは、確認ダイアログを出してから開きます

## 対応している Markdown

- 見出し（ATX `#` / Setext `===` `---`）、段落、強制改行
- 強調・太字・取り消し線、インラインコード、バックスラッシュエスケープ、HTML 実体参照
- リンク（インライン／参照／自動リンク／GFM の URL 自動リンク）、画像（`[画像: alt]` のリンクとして表示）
- 箇条書き・番号付きリスト（ネスト、tight/loose）、タスクリスト
- 引用（ネスト）、フェンス／インデントのコードブロック、GFM テーブル（左／中央／右寄せ）、水平線
- 一部のインライン HTML（`<br>` `<b>` `<i>` `<code>` `<kbd>` `<sup>` `<sub>` `<a href>` `<img>`）、HTML コメント

日本語の段落内の改行は、文字の間にスペースを入れずにつなげます。

未対応: 画像の埋め込み表示、シンタックスハイライト、脚注、数式、ブロックレベル HTML の解釈（タグは取り除いて本文だけ表示）、印刷。

## 設定

設定は `HKCU\Software\MdView` に保存されます（ウィンドウ位置、倍率、テーマ、自動再読み込み）。
フォントは次の値を直接編集して変えられます。

| 値 | 型 | 既定値 |
|---|---|---|
| FontBody | REG_SZ | Yu Gothic UI |
| FontCode | REG_SZ | Consolas |
| FontSize | REG_DWORD | 11 |

PowerShell の例:

```powershell
Set-ItemProperty -Path HKCU:\Software\MdView -Name FontBody -Value 'Meiryo UI'
```

## ファイル構成

| ファイル | 内容 |
|---|---|
| src/main.c | ウィンドウ、メニュー、ファイル読み込み、リンク・検索などの UI |
| src/md2rtf.c / .h | Markdown → RTF 変換（Windows に依存しない C） |
| src/app.rc / app.manifest / mdview.ico | アイコン、バージョン情報、マニフェスト（Common Controls v6、DPI 対応） |
| sample.md | 表示確認用のサンプル |

文字コードは UTF-8（BOM あり／なし）、UTF-16 (BOM 付き)、Shift_JIS などの ANSI を自動判別します。
