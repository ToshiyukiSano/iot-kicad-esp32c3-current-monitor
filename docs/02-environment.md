# 02 — UbuntuでKiCadとClaude Codeを連携する

実施環境の記録（2026-11-01）：**Ubuntu 24.04、KiCad CLI 10.0.8、Claude Code 2.1.316、Claude Pro**。別OS/別バージョンでは挙動が異なる可能性があります。

## 1. KiCadを準備

今回のUbuntu環境ではKiCad公式PPAから10系を導入しました。実行時点の配布状況は必ず公式情報を確認してください。

```bash
sudo add-apt-repository --yes ppa:kicad/kicad-10.0-releases
sudo apt update
sudo apt install --install-recommends kicad
kicad-cli version
kicad
```

標準ライブラリは例として `/usr/share/kicad/symbols`、`/usr/share/kicad/footprints`、ユーザー設定は `~/.config/kicad/10.0` です。環境によって異なります。

## 2. Claude Codeを準備

公式ドキュメント：[Claude Code](https://code.claude.com/docs/en/overview)

```bash
curl -fsSL https://claude.ai/install.sh | bash
claude --version
mkdir -p ~/Projects/current-monitor
cd ~/Projects/current-monitor
claude
```

初回ログインや利用料金・プラン要件は公式案内に従ってください。インストールコマンドは実行前に入手元を確認してください。

## 3. KiCad MCPを連携

作業ログでは [unmateria/MCP-Kicad](https://github.com/unmateria/MCP-Kicad) を使用。配布バイナリ名、対応KiCadバージョン、公開機能はリリースごとに変わりうるため、必ずREADMEを参照してください。

導入後の実行ファイルが `~/bin/mcp-kicad` にある前提で、ユーザー全体へ登録します。

```bash
claude mcp add --scope user kicad -- "$HOME/bin/mcp-kicad"
claude mcp get kicad
claude mcp list
```

**実際につまずいた点**：登録スコープが特定フォルダのままだと、`~/Projects/current-monitor` で起動したClaudeからサーバーを使えませんでした。`--scope user` で登録し、Claudeセッションを再起動して解消しました。

Claudeのプロンプト例：

```text
KiCad MCPのget_project_infoを実際に呼び出し、KiCad CLIの場所、ライブラリ、working_dir、output_dirを日本語で報告してください。
次にset_output_dirで出力先を /home/ubuntu/Projects/current-monitor に変更し、get_project_infoで確認してください。
```

**実行ログの確認値**：`/usr/bin/kicad-cli`、`/home/ubuntu/Projects/current-monitor`、MCP標準ライブラリ `/home/ubuntu/bin/libs`。これは今回のPC固有の値です。

## 4. 役割分担

| 作業 | ツール | 人が確かめること |
|---|---|---|
| 要件・電気設計レビュー | ChatGPT/Claude | 一次資料と設備適合性 |
| 回路図生成・ERC | Claude Code + KiCad MCP | ピン配列、部品定格、保護 |
| 配置・配線・ゾーン | KiCad GUI / pcbnew Python API | 製造条件、基板外形、GND帰路 |
| Firmware | Arduino IDE / arduino-cli | コンパイル、実機タイミング |
| 計測の再現性 | 電流基準器・低電圧負荷・ログ解析 | 校正と誤差 |

MCPの実装によってPCB生成やフットプリント設定の範囲が異なります。今回のMCPでは回路図作成支援は動作した一方、PCB生成をpcbnew APIで補いました。
