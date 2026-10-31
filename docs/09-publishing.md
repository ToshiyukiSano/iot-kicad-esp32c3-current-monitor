# 09 — 手元のKiCadプロジェクトをGitHubに公開する

このドキュメントはGitHub用に構成済みです。実際にClaudeがUbuntu上で作ったKiCad/Arduinoソースはチャットへ添付されていないため、本パッケージには含まれません。必要なら安全なファイルだけを取り込みます。

## 推奨リポジトリ名

`iot-kicad-esp32c3-current-monitor`（タイトルは「生成AI × KiCadで始めるIoTハードウェア開発」）。別のセンサー案件を増やす場合は `examples/` に追加します。

## ローカルから取り込む

このリポジトリのルートで：

```bash
bash scripts/import_local_project.sh "$HOME/Projects/current-monitor"
```

コピー対象は `*.kicad_sch`、`*.kicad_pcb`、`*.kicad_pro`、`.kicad_sym`、`*.pretty`、`fp-lib-table`、`sym-lib-table`、実在する `.ino`、該当BOM/検査結果などの**明示的な対象だけ**。ユーザー別設定、認証情報、Claude内部ログ、バックアップ丸ごと、ツールチェーンはコピーしません。コピーした内容を開き、機密情報・安全上の問題がないか確認してください。

**重要**： `.kicad_sch` と `.kicad_pcb` だけでなく、ローカルシンボル・フットプリント・ライブラリ登録ファイルも合わせて版管理し、プロジェクトを別PCで開けるか確認してください。3Dモデルの参照が絶対パスなら相対参照に修正します。

## 新規リポジトリをGitHubに作る（Ubuntuで実行）

`gh` がない場合はGitHub CLIを導入し、ログインしてください。**公開前の確認が済むまではprivateがおすすめです。**

```bash
sudo apt install gh
gh auth login
cd /path/to/iot-kicad-guide
git init
git add .
git commit -m "docs: add AI-assisted IoT and KiCad development guide"
git branch -M main
gh repo create iot-kicad-esp32c3-current-monitor --private --source=. --remote=origin --push
```

公開して問題ないことをレビューしたら、GitHubのSettingsでvisibilityをPublicに変更できます。既存のリポジトリに載せる場合は、新規リポジトリを作らず適切なサブディレクトリに配置します。

## 公開前チェック

- [ ] 回路は教育用・未検証と書いてある
- [ ] 端子台、負担抵抗、逆給電、電気安全に未解決課題がないか確認した
- [ ] シンボル/フットプリントが別PCで開ける
- [ ] Arduinoのコンパイル・実機テスト結果がある場合だけ「検証済み」と書いた
- [ ] 製造用Gerberを未検証のまま公開していない
- [ ] APIキー、Wi-Fiパスワード、ローカルログ、バックアップ、ユーザー名などを含まない
- [ ] 外部CADモデル・画像・ライブラリのライセンスを確認した
- [ ] ライセンスを付ける場合、権利関係と責任範囲を判断した

※ GitHubの `README.md` や回路図を載せただけで、製造や設備設置の安全性が裏付けられるわけではありません。

### ワンコマンド登録（確認付き）

上記を個別に実行する代わりに、`gh auth login` が済んでいれば、このパッケージに同梱したスクリプトも使えます。**既定はprivate**で、2回の確認入力が必要です。

```bash
bash scripts/publish_github.sh
```

公開を選ぶ場合のみ、内容を十分に確認してから `bash scripts/publish_github.sh iot-kicad-esp32c3-current-monitor --public` とします。既存リポジトリへの上書きはしません。
