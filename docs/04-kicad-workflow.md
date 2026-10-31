# 04 — AIを活用したKiCad回路図・PCB製作フロー

## A. 回路図を作る

1. Claude CodeでMCPの `get_project_info` と `set_output_dir` を実行し、プロジェクト保存先を確定。
2. `create_schematic` でプロジェクトを作成し、MCPが対応する `compile_schematic` 等でシンボル・ネットを生成。
3. XIAOの14ピンとCTの極性・定格を**公式資料から**照合。
4. `kicad-cli sch erc` でERC、ネットリストを出力して配線接続を検証。
5. KiCadの回路図エディタで目視し、重なったラベル、未接続、DNPを確認。

### 失敗1：標準シンボルが見つからない

最初、XIAOをJ2/J3という2つの7ピンソケットで表しました。しかし**右列J3の物理ピン順が誤っていた**ことが判明しました。その後、Seeedの公式ピン配置と照合して修正し、最終的には**U1＝14ピン一体型シンボル・フットプリント**へ統合しました。

重要なピン：`U1.2 = D1/A1/GPIO3 (ADC_A1)`、`U1.12 = 3V3`、`U1.13 = GND`、`U1.14 = 5V (未接続)`。フットプリントは2.54mmピッチ、2列間15.24mm、XIAOのUSB側を上にした向きです。**基板製作前に実物でソケット高さ・はんだ穴径・外形を再確認**します。

### 失敗2：MCPコンパイルでフットプリントが消える

ログでは、`current-monitor.design.json`にフットプリント欄を設定してもMCPが `.kicad_sch` に反映できず、再コンパイルすると割当が消えました。そこで次の回避策を採りました。

- `.kicad_sch` のFootprint属性をバックアップ後に直接設定するスクリプト（`tools/assign_footprints.py`）を作成
- プロジェクト内ライブラリ `current-monitor.pretty`、`current-monitor.kicad_sym`、`fp-lib-table`、`sym-lib-table` を保存
- `.design.json`から再コンパイルした場合は、割当スクリプトをもう一度実行
- **手作業でPCB Editorを更新した後は自動再生成スクリプトを不用意に実行しない**

## B. PCB化する

KiCad標準手順は「**回路図からPCBを更新（F8）**」です。今回はMCPでPCBの新規作成を完結できなかったため、KiCad 10.0.8の `pcbnew` Python APIを使い、ネット情報から部品を配置しました。以後はKiCad GUIのF8で同期できます。

### 今回の配置・配線条件（実施ログ）

| 項目 | 値・考え方 |
|---|---|
| 外形 | 仮50×40mm、M3非めっき取付穴3か所 |
| 積層 | 2層、FR4、総厚1.6mm、35µm銅（設計値） |
| U1 | USB端子を基板上端へ／U.FLアンテナ配線スペースを考慮 |
| J1・R1 | CT端子台と負担抵抗を極力近接 |
| R4・C2・D1 | ADC入力の近傍、短い信号経路 |
| トラック | GPIOと信号0.25mm、CTとバイアス0.40mm、3V3は概ね0.30～0.40mm |
| ビア | 外径0.60mm、穴径0.30mm（報告値） |
| 設計クリアランス | 0.20mm |
| GNDプレーン | 表裏GNDベタ。連続性と帰路に注意 |

![上面3D](../examples/current-monitor/images/pcb-top.png)

**注**：この図はXIAO・ソケットが未表示のKiCadレンダリングです。後続工程で公式CADの形状とKiCad標準モデルを合成した**近似**の実装モデルが作られたと報告されていますが、その生成ファイルと画像は本ドキュメントには含まれません。

## C. チェックの順番

1. シンボルのピン番号と実物のパッド番号を一致させる
2. ERCで未接続・電源・ネットラベルを検査
3. ネットリストとXIAO公式ピン表を照合
4. PCB配置でコートヤード、端子台の電線差し込み方向、USB/アンテナ、取付穴の干渉をチェック
5. 配線し、GND帰路が不自然に分断されていないか目視
6. `kicad-cli pcb drc --schematic-parity --severity-all` でDRCと回路図の一致を確認
7. 3D表示で高さ、ケーブル・コネクタ干渉を確認
8. **通電前に**部品型番、保護回路、製造データを別途レビュー

```bash
kicad-cli sch erc current-monitor.kicad_sch
kicad-cli pcb drc --schematic-parity --severity-all current-monitor.kicad_pcb
```

実施ログではERC/DRCが0件、未接続0件でしたが、作業完了の証明は**ログであり、こちらで実ファイルから再実行したわけではありません**。また、DRC 0件は保護回路の安全性・ADC精度・組立性を保証しません。

## D. 3DとBOM

最終工程ではXIAO公式STEPを直接取得できず、公式KiCad基板データを元に**近似STEP**を作成したと記録されています。公式モデルと同じとは言えません。BOMの型番候補は [CSV](../examples/current-monitor/hardware/bom-candidates.csv) に移したので、データシートと現物で確認してから注文してください。
