# ケーススタディ — XIAO ESP32C3 電流モニター

## 目的

工作機械の加工負荷を交流電流の変化として追跡する。機械型式は未定。まず1チャンネルCTで信号処理とノイズの再現性を学ぶ。

## 仕様（2026-11-01時点、実機未検証）

- XIAO ESP32C3／A1(GPIO3)
- SR-3704-150N／3000:1／50/60Hz／最大80A（負担抵抗10Ω以下）
- R1=10Ω、R2/R3=10kΩ、R4=4.7kΩ、C1=10µF、C2=100nF、C3=100nF、BAT54S
- 約1.65Vバイアス、1次RC低域フィルタ約339Hz、D2=未実装
- 50×40mm／2層基板／35µm銅／M3取付穴3つ（Claudeの作業報告）
- 計測ファームウェア：2kHz・200ms RMSを目標。**コンパイル・実機動作未確認**。

## 画像

![PCB上面](images/pcb-top.png)

![PCB斜め](images/pcb-iso.png)

画像はPCBエディタ出力であり、XIAOとピンソケットは表示されていません。

## ソースファイルを追加するには

Ubuntuにある正本 `~/Projects/current-monitor` から、[import_local_project.sh](../../scripts/import_local_project.sh) を用いて安全なファイルを追加してください。現時点で `hardware/` と `firmware/` は受け入れ先の雛形です。

## 詳細

[回路設計](../../docs/03-circuit-design.md)・[KiCadワークフロー](../../docs/04-kicad-workflow.md)・[試験計画](../../docs/06-testing.md)・[安全](../../docs/07-safety-open-items.md) を参照。
