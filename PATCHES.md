# ライブラリパッチメモ

`pio run --target clean` やライブラリ更新後に `.pio/libdeps/` が再生成された場合、以下のパッチを再適用すること。

---

## M5Atom @ 0.0.2 — Wire.begin の曖昧さ解消

ESP32 Arduino 2.0.x で `Wire.begin(int, int, int)` が曖昧なオーバーロードエラーになるため、
第3引数を `uint32_t` にキャストする。

### `.pio/libdeps/m5atom/M5Atom/src/M5Atom.cpp` (line 24)

```cpp
// 変更前
Wire.begin(25,21,10000);

// 変更後
Wire.begin(25,21,(uint32_t)10000);
```

### `.pio/libdeps/m5atom/M5Atom/src/utility/MPU6886.cpp` (line 35)

```cpp
// 変更前
Wire1.begin(25,21,100000);

// 変更後
Wire1.begin(25,21,(uint32_t)100000);
```

---

## Grove 4-Digit Display — TubeTab[16] をダッシュに修正

`TubeTab[16]` はデフォルト `0x3D`（G）だが、NTP未同期中に `----` を表示するため `16` を
インデックスとして渡している。正しい `-`（ダッシュ）のセグメントパターン `0x40`（セグメントgのみ）に変更。

### `.pio/libdeps/m5atom/Grove 4-Digit Display/TM1637.cpp` (line 38)

```cpp
// 変更前
0x3D,    // 0-9,a-fに加えてg-zまでを追加 2021/7/4

// 変更後
0x40,    // 0-9,a-fに加えてg-zまでを追加 2021/7/4 / [16]=dash(-) に変更
```

---

## WiFiManager — 削除済み

WiFiManager は ESP32 Arduino 2.0.x と非互換（`CONFIG_ESP32_PHY_MAX_TX_POWER` 未定義、`WiFi.onEvent()` シグネチャ変更）。
コード上でも未使用だったため `#include` と `lib_deps` エントリを削除した。
将来使う場合は ESP32 Arduino 2.x 対応の新しいバージョンが必要。
