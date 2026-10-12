# MdView サンプル

これは **MdView** の表示確認用サンプルです。*斜体*、***太字斜体***、~~取り消し線~~、`インラインコード` が使えます。
改行は同じ段落として
つながります。行末にスペース2つで  
強制改行します。

## 見出しレベル 2

### 見出しレベル 3

#### 見出しレベル 4

##### 見出しレベル 5

###### 見出しレベル 6

## リンク

- 外部リンク: [Microsoft Learn](https://learn.microsoft.com/ja-jp/ "Learn")
- 自動リンク: https://github.com/ と <https://example.com>
- 参照リンク: [CommonMark 仕様][cm]
- ページ内リンク: [表のセクションへ](#表)
- 画像: ![ロゴ](logo.png)

[cm]: https://spec.commonmark.org/

## リスト

1. 最初の項目
2. 2番目の項目
   - ネストした箇条書き
   - もうひとつ
     1. さらにネスト
3. 3番目の項目

- [x] 完了したタスク
- [ ] 未完了のタスク

## 引用

> 引用文です。
> 複数行にまたがります。
>
> > ネストした引用

## コード

```powershell
# PowerShell の例
Get-ADUser -Filter * -Properties mail |
    Select-Object SamAccountName, mail
```

    インデントによるコードブロック
    2行目

## 表

| 項目 | 説明 | 数値 |
|:-----|:----:|-----:|
| Entra Connect | ハイブリッド ID 同期 | 1 |
| Intune | デバイス管理 | 20 |
| Defender | `セキュリティ` | 300 |

---

エスケープ: \*アスタリスク\* &amp; &copy; &#x2713;
