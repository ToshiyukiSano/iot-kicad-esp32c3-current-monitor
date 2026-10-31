# 05 — 電流計測ファームウェアの考え方

> **重要：会話中にはClaudeが生成した約435行の `.ino` 本文が添付されていません。従って、このリポジトリは完成済み実機検証コードを含みません。** 手元のUbuntuから [移管手順](09-publishing.md) に沿って正本を追加してください。Claudeが `arduino-cli` インストール中の通信エラーに遭遇したため、最終コンパイルの成功も未確認です。

## 要件

| 項目 | 目標 |
|---|---|
| ボード | Seeed XIAO ESP32C3 / Arduino-ESP32 3.x候補 |
| ADC | A1 = GPIO3 = ADC1_CH3 |
| サンプリング | 2,000サンプル/秒（実機計測で検証） |
| 集計周期 | 200ms（50Hzで10周期、60Hzで12周期） |
| 出力 | 時刻・Irms・ADC平均・ノイズ指標・欠損カウントをCSV |
| 校正 | 既知電流と比較した校正係数 |
| 無線 | まずは無効。Wi-Fi送信は非同期で後から導入 |

## データ処理

1. ADCを一定周期でサンプリング。Arduino `analogReadMilliVolts()` とIDF連続ADC/DMAではサンプル形式・校正処理が異なるので、混在させない。
2. 計測窓ごとの平均値を計算して1.65V前後のDCオフセットを差し引く。
3. 電圧RMSを計算する。`Vrms = sqrt(mean((v_i - mean(v))^2))`。
4. 理想変流比と負担抵抗から `Irms ≈ Vrms × 3000 / 10`。RCフィルタとADCのゲイン誤差は校正で評価。
5. ノイズが大きい低電流域では0AでのRMSと変動幅を記録し、無理にゼロと決めつけない。
6. 一定期間の平均・標準偏差・最大・負荷増減を特徴量として取り出す。

疑似コード（**実機用完成コードではありません**）：

```text
repeat every 200 ms:
    samples = acquire_adc_samples(target_fs=2000)
    assert timing_valid and no_overflow
    mean_v = average(samples)
    rms_v = sqrt(average((samples - mean_v)^2))
    i_rms = rms_v * (3000 / 10) * calibration_gain
    csv(timestamp, i_rms, mean_v, timing_stats, overflow_count)
```

### DMA/連続ADCで注意すること

- APIがArduino-ESP32/ESP-IDFの版と一致するか
- ADC1（Wi-Fiとの併用）を選び、ADC2に切り替えない
- データ取得周期、DMAバッファあふれ、欠損数、電圧換算の校正を**実測**する
- Wi-Fi・Serial出力がサンプリング周期を乱さないようタスク間の処理を分離する
- サンプリング2kHzは、ADC変換クロックやオーバーサンプリングの設定と混同しない

## 想定するCSV例（架空データ）

```csv
timestamp_ms,current_rms_A,adc_mean_V,sample_count,overruns
200,0.32,1.648,400,0
400,5.03,1.649,400,0
600,6.01,1.650,400,0
```

**これはフォーマット例であって、実測データではありません。**

## コンパイル例

手元のコードと開発環境を確認したうえで、Arduino IDEからXIAO ESP32C3に書き込むか、対応するFQBNを照合した `arduino-cli` を使用してください。特定FQBNはこの文書では確定していません。

```bash
arduino-cli board listall | grep -i xiao
arduino-cli core list
# 使用中のボードとコアを確認してから compile を実行
```

**次の完了条件**：コンパイル成功ログ、Core版、実機で測ったサンプル間隔、0Aノイズ、5A→6A判別の再現性を記録する。
