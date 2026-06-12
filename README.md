# m5atom-clock

M5Atom Matrix + TM1637 4桁セグメントLED による NTP 同期時計。

- **TM1637**: 時刻（HH:MM、コロン1秒点滅）
- **5×5 LEDマトリクス**: 外周16ドットの秒リング + WiFi / NTP 状態インジケーター
- **人感センサー（HC-SR501）**: 在室検出を表示し、10分ごとの検出回数を自宅モニタリングシステム [housemonitor](https://github.com/gallipon/housemonitor) へ HTTPS POST（任意機能）

[BotanicFields, Inc. の JJY シミュレーター BF-018A](https://github.com/botanicfields/BF-018A)（MIT License）をベースに、時計として大幅に改変・機能追加したもの。

## ハードウェア

| 部品 | 用途 | 接続 |
|------|------|------|
| M5Atom Matrix | メインMCU（ESP32）・5×5 LED表示 | — |
| TM1637 4桁セグメント（Grove） | HH:MM 表示 | CLK: GPIO32, DIO: GPIO26 |
| HC-SR501（任意） | 人感センサー | GPIO25 |

## 主な機能・設計

### 省電力 WiFi 運用

NTP 同期後は WiFi を切断し、毎時 `:55`〜`:02` のみ接続して再同期する（1時間のうち約53分は WiFi オフ）。接続が間に合わなかった回はスキップし、内蔵 RTC で時刻表示を継続して次の正時に再試行する。

### フリッカーフリー描画

M5Atom ライブラリの `drawpix()` は呼ぶたびに `FastLED.show()` が走るため、`CRGB buf[25]` に全ピクセルの最終状態を構築してから一括描画する方式にしている（`clear()` による全消灯の中間状態を見せない）。

### 安定稼働のための仕組み

- NTP 同期完了は `sntp_set_time_sync_notification_cb()` のコールバックで検出し、マトリクス上のインジケーターに反映
- PIR 送信は常駐ワーカータスク + キュー方式（fire-and-forget タスクの mbedTLS ヒープリークを排除）
- 起動時にリセット理由（watchdog / fail / WiFi リセット）を `Preferences` に累積記録
- ブラウザから確認できる Web ログ（シリアルモニタ代替のリングバッファ）

### 開発中: ブラウザ OTA アップデート

`/update` エンドポイントからファームウェアを OTA 更新する機能を実装中だが、**まだ正しく動作していない**。

これまでの調査で、HTTP アップロードのストリーム処理中に同一コアからの flash 書き込みが silently fail する事象（IPC デッドロック起因）を特定し、Core 0 専用の flash writer タスクに書き込みを委譲する方式を実装済み。実機での検証が完了していないため、当面の書き込みは USB（UF2/シリアル）を使用する。

## ビルド・書き込み

PlatformIO を使用。

```bash
cp src/config.h.example src/config.h
# config.h に WiFi SSID / パスワードを設定（PIR 送信先は空欄で無効化）

pio run -t upload
```

> M5Atom ライブラリ等に手パッチが必要な場合がある。詳細は [PATCHES.md](PATCHES.md) を参照。

## 設定メモ

- タイムゾーンは JST+9 に**意図的に7分進めたオフセット**を加えている（`src/main.cpp` の `gmt_offset`）。正確な時刻にする場合は `+ 60 * 7` を削除する
- 毎分0秒に任意の4桁メッセージを1秒間表示できる（config.h の `minute_banner`、TM1637 セグメントパターンで指定。デフォルト無効）
- NTP サーバーは ntp.jst.mfeed.ad.jp / ntp.nict.jp / pool.ntp.org の順でフォールバック

表示レイアウト・色補正（WS2812B の GRB 順対応）・WiFi 管理の詳細は [docs/spec.md](docs/spec.md) を参照。

## 開発について

機能追加・安定化・OTA の原因調査は Claude Code を併用して実施。

## ライセンス

MIT（オリジナル: Copyright 2020 BotanicFields, Inc. / 改変: Copyright 2026 gallipon）
