// copyright 2020 BotanicFields, Inc.
// BF-018A
// M5Atom Matrix clock with NTP time sync and TM1637 segment display
//
#include <M5Atom.h>
#include <Ticker.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_system.h>
#include "TM1637.h"
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_flash_encrypt.h>
#include <esp_spi_flash.h>
#include "esp32/rom/spi_flash.h"
#include <stdarg.h>

#define CLK 32  // TM1637 CLK ピン
#define DIO 26  // TM1637 DIO ピン
#define PIR_PIN 25  // HC-SR501 人感センサー
TM1637 tm1637(CLK, DIO);

// TubeTab を経由せずにセグメントを直接書き込む
// seg: A=bit0, B=bit1, C=bit2, D=bit3, E=bit4, F=bit5, G=bit6
void tm1637WriteRaw(uint8_t pos, uint8_t seg)
{
  tm1637.start();
  tm1637.writeByte(ADDR_FIXED);
  tm1637.stop();
  tm1637.start();
  tm1637.writeByte(pos | 0xC0);
  tm1637.writeByte((int8_t)seg);
  tm1637.stop();
  tm1637.start();
  tm1637.writeByte(tm1637.Cmd_DispCtrl);
  tm1637.stop();
}

// WiFi・PIR送信先設定は config.h（git 管理外）に分離
// config.h.example をコピーして作成する
#include "config.h"

// NTP・時刻設定
const int   gmt_offset = (3600 * 9) + 60 * 7;  // JST+9 + 7分進め
const int   daylight   = 0;                      // サマータイムなし
const char* ntp_server  = "ntp.jst.mfeed.ad.jp";
const char* ntp_server2 = "ntp.nict.jp";
const char* ntp_server3 = "pool.ntp.org";
struct tm      td;  // 現在時刻（年月日時分秒）
struct timeval tv;  // 現在時刻（マイクロ秒精度、Ticker位相合わせ用）

// VPS送信設定（pir_endpoint / api_key は config.h で定義）
const int   sensor_no    = 2;

// Let's Encrypt ルート証明書（ISRG Root X1 + X2 を両方埋め込み）
// Root X1: RSA,   有効期限 2035-06-04（E5/E6/E7/E8 系中間CAに対応）
// Root X2: ECDSA, 有効期限 2040-09-17（YE2/YE3 系中間CAに対応）
// 中間CAが更新されても対応できるようルートCAを信頼点とする
const char* root_ca = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----

)EOF";

// 状態管理
const int     loop_delay_ms  = 100;   // loop() の周期 [ms]
volatile bool tco_tick        = false; // Tickerからloop()への100ms通知フラグ
volatile bool ntp_just_synced = false; // SNTPコールバックからの同期完了通知
bool          wifi_ok         = false; // WiFi接続状態
bool          ntp_synced      = false; // NTP初回同期完了フラグ
bool          ntp_hour_synced = false; // 最後の時刻同期が成功したか（インジケーター用）
bool          ntp_requested   = false; // configTime()済みフラグ（重複呼び出し防止）
unsigned long wifi_begin_ms  = 0;     // WiFi.begin()を呼んだ時刻（タイムアウト管理用）

// PIR状態管理
bool pir_state     = false;  // 現在のPIR状態（表示用）
bool pir_prev      = false;  // 前回のPIR状態（立ち上がりエッジ検出用）
int  pir_count     = 0;      // 10分間の検出回数
int  pir_last_slot = -1;     // 最後に送信した10分スロット（hour*6 + min/10）

// 診断ログ（NVS保存・循環バッファ）
Preferences prefs;

#define DIAG_OK       0
#define DIAG_NETWORK  1  // DNS/接続失敗
#define DIAG_HTTP     2  // 4xx/5xx
#define DIAG_WATCHDOG 3  // TLSハング
#define DIAG_BOOT     4  // 起動（リセット理由付き）

#define LOG_SIZE 50

struct LogEvent {
  uint32_t ts;    // unix timestamp
  uint8_t  type;  // DIAG_*
};

LogEvent event_log[LOG_SIZE];
int      log_head          = 0;  // 次の書き込み位置
int      log_count         = 0;  // 記録済みイベント数（最大LOG_SIZE）
uint32_t diag_fail_cnt     = 0;  // VPS送信累計失敗回数
uint32_t diag_watchdog_cnt = 0;  // ウォッチドッグ発火累計回数
uint32_t diag_wifi_rst_cnt = 0;  // WiFiリセット累計回数
time_t   diag_last_ok      = 0;  // 最終成功時刻（循環で消えても残す用）
uint8_t  diag_boot_reason  = 0;  // 直前の起動理由（esp_reset_reason_t）

void diagRecord(uint8_t type);  // forward declaration
void setupServerHandlers();     // forward declaration
void startServer();             // forward declaration
void stopServer();              // forward declaration

// Core 0 flash writer: struct と globals（setup() より前に宣言必須）
struct FlashOpReq {
  const esp_partition_t* part;
  bool   is_erase;
  size_t offset;
  size_t len;
  const void* data;
};
static QueueHandle_t     flash_op_queue  = NULL;
static SemaphoreHandle_t flash_op_done   = NULL;
static esp_err_t         flash_op_result = ESP_OK;
static void flashOpTask(void* param);  // forward declaration
static esp_err_t flashErase(const esp_partition_t* part, size_t off, size_t len);
static esp_err_t flashWrite(const esp_partition_t* part, size_t off, const void* data, size_t len);

static const char* diagTypeName(uint8_t type)
{
  switch (type) {
    case DIAG_OK:       return "OK";
    case DIAG_NETWORK:  return "NETWORK";
    case DIAG_HTTP:     return "HTTP";
    case DIAG_WATCHDOG: return "WATCHDOG";
    case DIAG_BOOT:     return "BOOT";
    default:            return "?";
  }
}

static const char* resetReasonName(esp_reset_reason_t r)
{
  switch (r) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_SW:       return "SW(restart)";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default:               return "OTHER";
  }
}

void diagLoad()
{
  if (!prefs.begin("diag", false)) {
    Serial.println("diagLoad: prefs.begin FAILED");
    return;
  }
  log_head          = (int)prefs.getUInt("lhead",    0);
  log_count         = (int)prefs.getUInt("lcount",   0);
  diag_fail_cnt     = prefs.getUInt("fail",      0);
  diag_watchdog_cnt = prefs.getUInt("watchdog",  0);
  diag_wifi_rst_cnt = prefs.getUInt("wifirst",   0);
  diag_last_ok      = (time_t)prefs.getULong("lastok", 0);
  diag_boot_reason  = prefs.getUChar("bootrst", 0);
  if (prefs.isKey("ldata"))
    prefs.getBytes("ldata", event_log, sizeof(event_log));
  prefs.end();
  // 今回の起動理由を記録（NTP前なのでts=0）
  diag_boot_reason = (uint8_t)esp_reset_reason();
  diagRecord(DIAG_BOOT);
}

void diagSave()
{
  if (!prefs.begin("diag", false)) {
    Serial.println("diagSave: prefs.begin FAILED");
    return;
  }
  bool ok = true;
  ok &= (prefs.putUInt("lhead",    (uint32_t)log_head)          != 0);
  ok &= (prefs.putUInt("lcount",   (uint32_t)log_count)         != 0);
  ok &= (prefs.putUInt("fail",     diag_fail_cnt)               != 0);
  ok &= (prefs.putUInt("watchdog", diag_watchdog_cnt)           != 0);
  ok &= (prefs.putUInt("wifirst",  diag_wifi_rst_cnt)           != 0);
  ok &= (prefs.putULong("lastok",  (unsigned long)diag_last_ok) != 0);
  ok &= (prefs.putUChar("bootrst", diag_boot_reason)           != 0);
  ok &= (prefs.putBytes("ldata",   event_log, sizeof(event_log)) == sizeof(event_log));
  if (!ok) Serial.println("diagSave: putXxx partial FAILED");
  prefs.end();
}

void diagRecord(uint8_t type)
{
  event_log[log_head].ts   = (uint32_t)time(NULL);
  event_log[log_head].type = type;
  log_head = (log_head + 1) % LOG_SIZE;
  if (log_count < LOG_SIZE) log_count++;
  diagSave();
}

void diagPrint()
{
  Serial.println("=== Diag log ===");
  if (diag_last_ok == 0) {
    Serial.println("Last OK  : (none)");
  } else {
    struct tm* t = localtime(&diag_last_ok);
    Serial.printf("Last OK  : %04d-%02d-%02d %02d:%02d:%02d\n",
      t->tm_year+1900, t->tm_mon+1, t->tm_mday,
      t->tm_hour, t->tm_min, t->tm_sec);
  }
  int start = (log_count < LOG_SIZE) ? 0 : log_head;
  for (int i = 0; i < log_count; i++) {
    int idx = (start + i) % LOG_SIZE;
    time_t ts = (time_t)event_log[idx].ts;
    uint8_t type = event_log[idx].type;
    if (ts == 0) {
      // NTP未同期での記録（起動直後など）
      const char* reason = (type == DIAG_BOOT)
        ? resetReasonName((esp_reset_reason_t)diag_boot_reason)
        : "?";
      Serial.printf("[---- boot ----] %s (%s)\n", diagTypeName(type), reason);
    } else {
      struct tm* t = localtime(&ts);
      Serial.printf("[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
        t->tm_year+1900, t->tm_mon+1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec,
        diagTypeName(type));
    }
  }
  Serial.printf("Total: fail=%u watchdog=%u wifi_rst=%u\n",
    diag_fail_cnt, diag_watchdog_cnt, diag_wifi_rst_cnt);
  Serial.println("================");
}

// diagPrint() のString返し版（Web表示用）
String diagGetString()
{
  String s;
  s.reserve(2048);
  s += "=== Diag log ===\n";
  if (diag_last_ok == 0) {
    s += "Last OK  : (none)\n";
  } else {
    struct tm* t = localtime(&diag_last_ok);
    char buf[64];
    snprintf(buf, sizeof(buf), "Last OK  : %04d-%02d-%02d %02d:%02d:%02d\n",
      t->tm_year+1900, t->tm_mon+1, t->tm_mday,
      t->tm_hour, t->tm_min, t->tm_sec);
    s += buf;
  }
  int start = (log_count < LOG_SIZE) ? 0 : log_head;
  for (int i = 0; i < log_count; i++) {
    int idx = (start + i) % LOG_SIZE;
    time_t ts = (time_t)event_log[idx].ts;
    uint8_t type = event_log[idx].type;
    char buf[80];
    if (ts == 0) {
      const char* reason = (type == DIAG_BOOT)
        ? resetReasonName((esp_reset_reason_t)diag_boot_reason)
        : "?";
      snprintf(buf, sizeof(buf), "[---- boot ----] %s (%s)\n", diagTypeName(type), reason);
    } else {
      struct tm* t = localtime(&ts);
      snprintf(buf, sizeof(buf), "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
        t->tm_year+1900, t->tm_mon+1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec,
        diagTypeName(type));
    }
    s += buf;
  }
  char buf[80];
  snprintf(buf, sizeof(buf), "Total: fail=%u watchdog=%u wifi_rst=%u\n",
    diag_fail_cnt, diag_watchdog_cnt, diag_wifi_rst_cnt);
  s += buf;
  s += "================\n";
  return s;
}

// ----- Webログリングバッファ（ブラウザ用シリアルモニタ代替） -----
#define WEBLOG_LINES    300
#define WEBLOG_LINE_LEN  96
static char         weblog_buf[WEBLOG_LINES][WEBLOG_LINE_LEN];
static int          weblog_head  = 0;
static int          weblog_count = 0;
static portMUX_TYPE weblog_mux   = portMUX_INITIALIZER_UNLOCKED;

void webLogf(const char* fmt, ...)
{
  char msg[WEBLOG_LINE_LEN];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  Serial.print(msg);

  // タイムスタンプ付きでバッファに格納（末尾の改行は除去）
  char line[WEBLOG_LINE_LEN];
  time_t now = time(NULL);
  if (now > 1000000000UL) {
    struct tm* t = localtime(&now);
    snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s",
      t->tm_hour, t->tm_min, t->tm_sec, msg);
  } else {
    snprintf(line, sizeof(line), "[--:--:--] %s", msg);
  }
  int len = strlen(line);
  while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

  portENTER_CRITICAL(&weblog_mux);
  strncpy(weblog_buf[weblog_head], line, WEBLOG_LINE_LEN - 1);
  weblog_buf[weblog_head][WEBLOG_LINE_LEN - 1] = '\0';
  weblog_head = (weblog_head + 1) % WEBLOG_LINES;
  if (weblog_count < WEBLOG_LINES) weblog_count++;
  portEXIT_CRITICAL(&weblog_mux);
}

// VPS送信エラー管理
volatile bool          vps_error               = false;  // 直近の送信が失敗したか
volatile bool          wifi_reconnect_requested = false;  // sendWorkerからの再接続要求
volatile bool          do_wifi_reset           = false;  // WiFiドライバリセット要求
volatile int           vps_fail_count          = 0;      // VPS送信連続失敗回数（2回でリセット）
volatile unsigned long worker_busy_since       = 0;      // sendWorker処理開始時刻（0=待機中）

// PIR送信キュー（sendWorkerとloop()間のデータ渡し）
QueueHandle_t send_queue = NULL;

// WiFi静的IP設定（doWifiReset()でも使用）
const IPAddress static_ip(192, 168, 3, 47);
const IPAddress static_gateway(192, 168, 3, 1);
const IPAddress static_subnet(255, 255, 255, 0);
const IPAddress static_dns(192, 168, 3, 1);

// NTP同期失敗タイムアウト管理
// WiFi接続中でもNTP同期失敗が続くとPIR送信が永久に止まるため、5分でdoWifiReset()
unsigned long ntp_fail_ms = 0;  // ntp_syncedがfalseになった時刻（0=正常）

// WiFi切断管理
unsigned long wifi_lost_ms      = 0;  // 切断を検知した時刻
unsigned long wifi_reconnect_ms = 0;  // 最後に再接続を試みた時刻

// M5 Matrix 表示レイアウト:
//   col:  0  1  2  3  4
//   row0: s  s  s  s  s   s=秒リング（時計回り、外周16ドット）
//   row1: s  W  .  N  s   W=WiFi状態（内部左列）、N=NTP状態（内部右列）
//   row2: s  W  .  N  s   青=正常、赤=エラー
//   row3: s  .  .  .  s
//   row4: s  s  s  s  s
// displaybuff用バッファ: [xsize, ysize, R,G,B, R,G,B, ... ] = 2 + 25*3 = 77バイト
uint8_t dispbuf[2 + NUM_LEDS * 3];

void UpdateMatrix()
{
  // 接続済み=暗青, 未接続/未同期=暗赤
  CRGB wifi_color = wifi_ok         ? CRGB(0x00, 0x00, 0x60) : CRGB(0x60, 0x00, 0x00);
  CRGB ntp_color  = ntp_hour_synced ? CRGB(0x00, 0x00, 0x60) : CRGB(0x60, 0x00, 0x00);

  CRGB buf[25];
  for (int i = 0; i < 25; i++) buf[i] = CRGB(0, 0, 0);

  // 内部中央: WiFi(row1-2, col1)、NTP(row1-2, col3)
  buf[6]  = buf[11] = wifi_color;
  buf[8]  = buf[13] = ntp_color;

  // PIR人感センサー: pixel 12（row2中央）に検出状態を表示
  buf[12] = pir_state ? CRGB(0x00, 0x00, 0x60) : CRGB(0, 0, 0);

  // VPS送信エラー: pixel 17（row3中央）にエラー時暗赤で表示
  buf[17] = vps_error ? CRGB(0x60, 0x00, 0x00) : CRGB(0, 0, 0);

  // 外周秒リング（時計回り16ドット、上中央=12時位置スタート）
  static const int ring[16] = {2,3,4, 9,14,19,24, 23,22,21,20, 15,10,5, 0,1};
  int lit = ntp_synced ? (td.tm_sec * 16 / 60) : 0;
  for (int i = 0; i < 16; i++) {
    buf[ring[i]] = (i < lit) ? CRGB(0x40, 0x80, 0x00) : CRGB(0x00, 0x00, 0x20);
  }

  // displaybuff形式に変換して一括描画（mutex内で全ピクセル書き込み→1回だけshow）
  dispbuf[0] = 5;  // xsize
  dispbuf[1] = 5;  // ysize
  // displaybuff内部がraw[1](G)←[+0]、raw[0](R)←[+1]と入れ替えるため逆順で渡す
  for (int i = 0; i < 25; i++) {
    dispbuf[2 + i * 3 + 0] = buf[i].g;
    dispbuf[2 + i * 3 + 1] = buf[i].r;
    dispbuf[2 + i * 3 + 2] = buf[i].b;
  }
  M5.dis.displaybuff(dispbuf);
}

void MatrixInit()
{
  M5.dis.setBrightness(20);
  M5.dis.clear();
  UpdateMatrix();
}


// ----- TCO (Time Code Output) 関連 -----
// Ticker割り込みからloop()に処理を移したため、以下のTCO生成コードは現在無効。
// 将来の復元用に残存。

// PWM出力設定（40kHz、デューティ50%）
const uint8_t  ledc_pin        = 22;
const uint8_t  ledc_channel    = 0;
const double   ledc_frequency  = 4e4;  // 40kHz
const uint8_t  ledc_resolution = 10;   // 分解能 2^10 = 1024
const uint32_t ledc_duty_on    = 512;  // 50% duty
const uint32_t ledc_duty_off   = 0;    // 0% duty

// Ticker設定
const int ticker_period = 100;  // 100ms周期
const int marker        = 0xff; // JJYマーカーパルスを示す値
int       last_usec;            // Ticker統計用（前回呼び出し時刻 [μs]）
Ticker    tk;

// 整数をBCD3桁に変換（例: 123 → 0x123）
int Int3bcd(int a)
{
  return (a % 10) + (a / 10 % 10 * 16) + (a / 100 % 10 * 256);
}

// 8ビット値のパリティを返す（1の個数が奇数なら1、偶数なら0）
int Parity8(int a)
{
  int pa = a;
  for (int i = 1; i < 8; ++i) {
    pa += a >> i;
  }
  return pa % 2;
}

void TcOn()
{
  ledcWrite(ledc_channel, ledc_duty_on);
}

void TcOff()
{
  ledcWrite(ledc_channel, ledc_duty_off);
}

// 現在の秒に対応するJJY時刻コード値を返す
// 戻り値: marker=マーカーパルス、0=ゼロパルス、非0=ワンパルス
int TcoValue()
{
  int bcd_hour   = Int3bcd(td.tm_hour);
  int bcd_minute = Int3bcd(td.tm_min);
  int year       = td.tm_year + 1900;
  int bcd_year   = Int3bcd(year);

  int parity_bcd_hour   = Parity8(bcd_hour);
  int parity_bcd_minute = Parity8(bcd_minute);

  // 年通算日の計算
  static const int days_of_month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int days = td.tm_mday;
  for (int i = 0; i < td.tm_mon; ++i)  // td.tm_mon は0始まり
    days += days_of_month[i];
  if ((td.tm_mon >= 2) && (((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0)))
    ++days;  // うるう年補正
  int bcd_days = Int3bcd(days);

  int day_of_week = td.tm_wday;

  int tco;
  switch (td.tm_sec) {
  case  0: tco = marker;              break;
  case  1: tco = bcd_minute & 0x40;   break;
  case  2: tco = bcd_minute & 0x20;   break;
  case  3: tco = bcd_minute & 0x10;   break;
  case  4: tco = 0;                   break;
  case  5: tco = bcd_minute & 0x08;   break;
  case  6: tco = bcd_minute & 0x04;   break;
  case  7: tco = bcd_minute & 0x02;   break;
  case  8: tco = bcd_minute & 0x01;   break;
  case  9: tco = marker;              break;

  case 10: tco = 0;                   break;
  case 11: tco = 0;                   break;
  case 12: tco = bcd_hour & 0x20;     break;
  case 13: tco = bcd_hour & 0x10;     break;
  case 14: tco = 0;                   break;
  case 15: tco = bcd_hour & 0x08;     break;
  case 16: tco = bcd_hour & 0x04;     break;
  case 17: tco = bcd_hour & 0x02;     break;
  case 18: tco = bcd_hour & 0x01;     break;
  case 19: tco = marker;              break;

  case 20: tco = 0;                   break;
  case 21: tco = 0;                   break;
  case 22: tco = bcd_days & 0x200;    break;
  case 23: tco = bcd_days & 0x100;    break;
  case 24: tco = 0;                   break;
  case 25: tco = bcd_days & 0x080;    break;
  case 26: tco = bcd_days & 0x040;    break;
  case 27: tco = bcd_days & 0x020;    break;
  case 28: tco = bcd_days & 0x010;    break;
  case 29: tco = marker;              break;

  case 30: tco = bcd_days & 0x008;    break;
  case 31: tco = bcd_days & 0x004;    break;
  case 32: tco = bcd_days & 0x002;    break;
  case 33: tco = bcd_days & 0x001;    break;
  case 34: tco = 0;                   break;
  case 35: tco = 0;                   break;
  case 36: tco = parity_bcd_hour;     break;
  case 37: tco = parity_bcd_minute;   break;
  case 38: tco = 0;                   break;
  case 39: tco = marker;              break;

  case 40: tco = 0;                   break;
  case 41: tco = bcd_year & 0x80;     break;
  case 42: tco = bcd_year & 0x40;     break;
  case 43: tco = bcd_year & 0x20;     break;
  case 44: tco = bcd_year & 0x10;     break;
  case 45: tco = bcd_year & 0x08;     break;
  case 46: tco = bcd_year & 0x04;     break;
  case 47: tco = bcd_year & 0x02;     break;
  case 48: tco = bcd_year & 0x01;     break;
  case 49: tco = marker;              break;

  case 50: tco = day_of_week & 0x04;  break;
  case 51: tco = day_of_week & 0x02;  break;
  case 52: tco = day_of_week & 0x01;  break;
  case 53: tco = 0;                   break;
  case 54: tco = 0;                   break;
  case 55: tco = 0;                   break;
  case 56: tco = 0;                   break;
  case 57: tco = 0;                   break;
  case 58: tco = 0;                   break;
  case 59: tco = marker;              break;
  default: tco = 0;                   break;
  }
  return tco;
}

// 各タイミングのTCOパルス処理（現在は無効）
void Tco000ms()
{
  TcOn();
  if (td.tm_sec == 0) {
    Serial.println();
    Serial.println(&td, "%A %B %d %Y %H:%M:%S");
  }
}

void Tco200ms()
{
  if (TcoValue() == marker) {
    TcOff();
    Serial.print(" ");
    Serial.print(td.tm_sec);
    Serial.print(" ");
  }
}

void Tco500ms()
{
  if (TcoValue() != 0) {
    TcOff();
    if (TcoValue() != marker)
      Serial.print("1");
  }
}

void Tco800ms()
{
  TcOff();
  if (TcoValue() == 0)
    Serial.print("0");
}

// Tickerコールバック: loop()への通知フラグを立てるだけ
// TM1637ビットバン通信など時間のかかる処理を割り込みコンテキストで行わないための対策
void TcoGen()
{
  tco_tick = true;
}


// SNTPの時刻同期完了コールバック（同期のたびに呼ばれる）
void sntpSyncCallback(struct timeval* tv)
{
  ntp_just_synced = true;
}

// PIR送信結果
enum SendResult { SEND_OK, SEND_HTTP_ERROR, SEND_NETWORK_ERROR };

// PIR検出回数をVPSへHTTPS POSTする
SendResult sendPirData(int count)
{
  if (pir_endpoint[0] == '\0') return SEND_OK;  // 送信先未設定なら何もしない
  if (WiFi.status() != WL_CONNECTED) return SEND_NETWORK_ERROR;

  // 実時刻（7分進めオフセットなし）を10分単位に切り捨て
  time_t now_utc = time(NULL);
  time_t now_jst_aligned = (now_utc + 9 * 3600) / 600 * 600;  // JST変換後10分切り捨て
  struct tm* ta = gmtime(&now_jst_aligned);
  char measured[20];
  snprintf(measured, sizeof(measured), "%04d-%02d-%02d %02d:%02d:%02d",
           ta->tm_year + 1900, ta->tm_mon + 1, ta->tm_mday,
           ta->tm_hour, ta->tm_min, ta->tm_sec);

  char json[128];
  snprintf(json, sizeof(json),
           "{\"count\":%d,\"sensor_no\":%d,\"measured\":\"%s\"}",
           count, sensor_no, measured);

  WiFiClientSecure client;
  client.setCACert(root_ca);
  client.setTimeout(15);           // HTTP応答タイムアウト 15秒
  client.setHandshakeTimeout(15);  // TLSハンドシェイクタイムアウト 15秒

  HTTPClient https;
  https.setTimeout(15000);  // HTTP応答タイムアウト 15秒
  if (https.begin(client, pir_endpoint)) {
    https.addHeader("Content-Type", "application/json");
    if (api_key[0] != '\0')
      https.addHeader("X-API-Key", api_key);
    int code = https.POST((uint8_t*)json, strlen(json));
    Serial.printf("PIR POST: %d (count=%d)\n", code, count);
    https.end();
    if (code >= 200 && code < 300) return SEND_OK;
    if (code < 0)                   return SEND_NETWORK_ERROR;  // 接続失敗・タイムアウト
    return SEND_HTTP_ERROR;                                      // 4xx/5xx
  }
  return SEND_NETWORK_ERROR;
}

// PIR送信ワーカータスク（常駐。キューからデータを受け取って送信する）
// 旧 fire-and-forget + vTaskDelete(NULL) パターンを廃止。
// sendPirData() が正常リターンすることでC++デストラクタが確実に実行され、
// WiFiClientSecure / HTTPClient のmbedTLSヒープリークを防ぐ。
void sendWorker(void* param)
{
  while (true) {
    int count;
    if (xQueueReceive(send_queue, &count, portMAX_DELAY) != pdTRUE) continue;

    // WiFi切断中は送信せず捨てる（切断→失敗→do_wifi_reset 連鎖を防ぐ）
    if (WiFi.status() != WL_CONNECTED) {
      Serial.printf("WiFi down, skip PIR count=%d\n", count);
      vps_error = true;
      continue;
    }

    worker_busy_since = millis();

    // ネットワーク障害のみリトライ（4xx/5xxはリトライ不要）
    SendResult result = sendPirData(count);
    if (result == SEND_NETWORK_ERROR) {
      delay(5000);
      result = sendPirData(count);
    }

    worker_busy_since = 0;

    if (result == SEND_OK) {
      vps_error      = false;
      vps_fail_count = 0;
      diag_last_ok   = time(NULL);  // RAMのみ更新。失敗/リセット時のdiagsave()に便乗して保存される
    } else {
      Serial.printf("PIR SEND FAILED: count=%d result=%d (discarded)\n", count, result);
      diag_fail_cnt++;  // RAMのみ（NVS書き込みなし）
      vps_error = true;
      vps_fail_count++;
      // 2回連続失敗（約20分）でWiFiドライバリセット要求
      if (vps_fail_count >= 2) {
        webLogf("VPS failed %d times, requesting WiFi reset...\n", vps_fail_count);
        do_wifi_reset = true;
      }
      // ネットワーク障害かつ接続済み表示 → WiFiスタックが壊れている可能性
      if (result == SEND_NETWORK_ERROR && WiFi.status() == WL_CONNECTED) {
        webLogf("WiFi stack may be stuck, requesting reconnect...\n");
        wifi_reconnect_requested = true;
      }
    }
  }
}

// WiFiドライバのみリセット（ESP.restart()と違い時計・LED表示は継続）
void doWifiReset()
{
  stopServer();
  webLogf("WiFi driver reset...\n");
  diag_wifi_rst_cnt++;
  diagSave();
  vps_fail_count           = 0;
  worker_busy_since        = 0;
  wifi_reconnect_requested = false;
  do_wifi_reset            = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.config(static_ip, static_gateway, static_subnet, static_dns);
  WiFi.begin(wifi_ssid, wifi_pass);
  wifi_lost_ms      = millis();
  wifi_reconnect_ms = millis();
}

void setup()
{
  // CH552T VCC=5V による GPIO_0 過電圧対策（ESP32 WiFi/フラッシュ安定化）
  // 参照: https://macsbug.wordpress.com/2021/10/10/bugs-and-measures-for-m5stack-atom/
  pinMode(0, OUTPUT);
  digitalWrite(0, LOW);

  M5.begin(true, false, true);
  sntp_set_time_sync_notification_cb(sntpSyncCallback);
  diagLoad();

  // TM1637セグメントディスプレイ初期化
  tm1637.init();
  tm1637.set(BRIGHT_DARKEST);  // BRIGHT_DARKEST=0, BRIGHT_TYPICAL=2, BRIGHTEST=7

  // PIR人感センサー入力
  pinMode(PIR_PIN, INPUT);

  // M5 Matrixインジケータ初期化（起動時は全て未接続状態で表示）
  MatrixInit();

  // WiFi接続（静的IP、常時ON、最大15秒待ち）
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.config(static_ip, static_gateway, static_subnet, static_dns);
  wifi_begin_ms = millis();
  WiFi.begin(wifi_ssid, wifi_pass);
  for (int cnt = 0; cnt < 30 && WiFi.status() != WL_CONNECTED; cnt++)
    delay(500);
  setupServerHandlers();
  if (WiFi.status() == WL_CONNECTED) {
    webLogf("WiFi connected\n");
    wifi_ok = true;
    esp_wifi_set_ps(WIFI_PS_NONE);  // Power Save無効: ビーコン割り込みバーストによるLEDノイズ防止
    startServer();
  } else {
    webLogf("WiFi timeout, continuing without WiFi\n");
  }

  // NTP設定（一度だけ、以降はSNTPが自動再同期）
  configTime(gmt_offset, daylight, ntp_server, ntp_server2, ntp_server3);

  // TCO用PWM出力初期化（40kHz）
  ledcSetup(ledc_channel, ledc_frequency, ledc_resolution);
  ledcAttachPin(ledc_pin, ledc_channel);

  // Tickerの位相をタイマー境界の中間（+50ms）に合わせる
  gettimeofday(&tv, NULL);
  delayMicroseconds(150000 - tv.tv_usec % 100000);

  // Ticker統計用の基準時刻を記録
  gettimeofday(&tv, NULL);
  last_usec = tv.tv_usec;

  // PIR送信キューとワーカータスクを起動（常駐、Core 0）
  send_queue = xQueueCreate(2, sizeof(int));
  xTaskCreatePinnedToCore(sendWorker, "send", 20480, NULL, 1, NULL, 0);

  // Core 0 flash writer（HTTP upload 中の IPC デッドロック回避）
  flash_op_queue = xQueueCreate(1, sizeof(FlashOpReq));
  flash_op_done  = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(flashOpTask, "flash", 4096, NULL, 5, NULL, 0);

  // 100ms Ticker開始
  tk.attach_ms(ticker_period, TcoGen);
}


// ----- Web サーバー（OTA / 診断ログ / ブラウザログ） -----
WebServer server(80);
bool      server_running = false;

// ----- Core 0 flash writer（HTTP upload 中の IPC デッドロック回避）-----
// HTTP upload は Core 1。flash write は両 core のキャッシュ無効化 IPC が必要。
// Core 0 WiFi タスクが割り込み禁止中だと Core 1→Core 0 の IPC が届かず silently fail。
// Core 0 から実行すれば Core 1（待機中）への IPC は届く。
// struct/globals は上部（forward declaration 直後）で定義済み。
static void flashOpTask(void* param)
{
  FlashOpReq req;
  while (true) {
    if (xQueueReceive(flash_op_queue, &req, portMAX_DELAY) != pdTRUE) continue;
    if (req.is_erase) {
      flash_op_result = esp_partition_erase_range(req.part, req.offset, req.len);
    } else {
      flash_op_result = esp_partition_write(req.part, req.offset, req.data, req.len);
    }
    xSemaphoreGive(flash_op_done);
  }
}

static esp_err_t flashErase(const esp_partition_t* part, size_t off, size_t len)
{
  FlashOpReq req = {part, true, off, len, NULL};
  xQueueSend(flash_op_queue, &req, portMAX_DELAY);
  xSemaphoreTake(flash_op_done, portMAX_DELAY);
  return flash_op_result;
}

static esp_err_t flashWrite(const esp_partition_t* part, size_t off, const void* data, size_t len)
{
  FlashOpReq req = {part, false, off, len, data};
  xQueueSend(flash_op_queue, &req, portMAX_DELAY);
  xSemaphoreTake(flash_op_done, portMAX_DELAY);
  return flash_op_result;
}

// ----- ブラウザ OTA アップロードハンドラ（パーティション直接書き込み）-----
// esp_ota_begin() を呼ぶと WIP マーカーガードが有効になり、sector 0 への
// すべての書き込みが block される（esp_ota_end() でも解除不可）。
// 回避策: OTA API を完全に bypass し esp_partition_write() で直接書く。
// sector 0 は最後に書くことで WIP マーカーを回避する。
static const esp_partition_t* ota_upload_part    = NULL;
static esp_err_t              ota_upload_err     = ESP_OK;
static bool                   ota_first_logged   = false;
static size_t                 ota_write_offset   = 0;     // 書き込み位置（sector 1 以降）
static uint8_t                ota_sec0_buf[4096];          // sector 0 バッファ（最後に書く）
static size_t                 ota_sec0_size      = 0;
static volatile bool          ota_flash_write_pending = false; // loop() で sector0 書き込みを延期
static bool                   ota_staging_done       = false; // sector0 staging 書き込み済み
static bool                   ota_staging_attempted  = false; // staging を1度だけ試みたか

// Note: performDeferredOTAWrite() 削除済み。
// sector0 は NVS 経由 + startup コンテキストで書き込む（startServer() 内の NVS pending check を参照）。
// HTTP handler / WiFi 実行中はすべての sector0 書き込みが silently fail するため。

// ----- OTA診断: otadataリセット + sector0 書き込み再試行 -----
// 過去の esp_ota_begin() が残した WIP エントリが sector0 write をブロックしている仮説を検証
void handleOtaReset()
{
  // 1) otadata の内容を読んで状態確認
  const esp_partition_t* otadata = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
  if (!otadata) {
    webLogf("ota-reset: otadata partition not found\n");
    server.send(500, "text/plain", "otadata not found");
    return;
  }

  // otadata 先頭 32 バイト（2つの esp_ota_select_entry_t の一部）を読む
  uint8_t ota_buf[32] = {0};
  spi_flash_read(otadata->address, ota_buf, sizeof(ota_buf));
  webLogf("otadata[0..7]:   %02x%02x%02x%02x %02x%02x%02x%02x\n",
          ota_buf[0],ota_buf[1],ota_buf[2],ota_buf[3],
          ota_buf[4],ota_buf[5],ota_buf[6],ota_buf[7]);
  webLogf("otadata[8..15]:  %02x%02x%02x%02x %02x%02x%02x%02x\n",
          ota_buf[8],ota_buf[9],ota_buf[10],ota_buf[11],
          ota_buf[12],ota_buf[13],ota_buf[14],ota_buf[15]);
  webLogf("otadata[16..23]: %02x%02x%02x%02x %02x%02x%02x%02x\n",
          ota_buf[16],ota_buf[17],ota_buf[18],ota_buf[19],
          ota_buf[20],ota_buf[21],ota_buf[22],ota_buf[23]);

  // 2) app1 sector0 の現在値
  const esp_partition_t* app1 = esp_ota_get_next_update_partition(NULL);
  if (app1) {
    uint8_t s0[4] = {0};
    spi_flash_read(app1->address, s0, 4);
    webLogf("app1 sector0 before: %02x%02x%02x%02x\n",
            s0[0],s0[1],s0[2],s0[3]);
  }

  // 3) otadata を消去（WIP エントリを削除）
  esp_err_t ota_err = esp_partition_erase_range(otadata, 0, otadata->size);
  webLogf("otadata erase: 0x%x\n", ota_err);

  // 4) otadata 消去後の内容確認
  spi_flash_read(otadata->address, ota_buf, 8);
  webLogf("otadata after erase[0..7]: %02x%02x%02x%02x %02x%02x%02x%02x\n",
          ota_buf[0],ota_buf[1],ota_buf[2],ota_buf[3],
          ota_buf[4],ota_buf[5],ota_buf[6],ota_buf[7]);

  // 5) otadata 消去後に app1 sector0 を erase + write 再試行
  if (app1) {
    uint8_t s0_before[4] = {0};
    spi_flash_read(app1->address, s0_before, 4);
    webLogf("app1 sector0 before erase: %02x%02x%02x%02x\n",
            s0_before[0],s0_before[1],s0_before[2],s0_before[3]);

    esp_err_t erase_err = esp_partition_erase_range(app1, 0, SPI_FLASH_SEC_SIZE);
    webLogf("app1 sector0 erase: 0x%x\n", erase_err);

    uint8_t s0_after[4] = {0};
    spi_flash_read(app1->address, s0_after, 4);
    webLogf("app1 sector0 after erase: %02x%02x%02x%02x\n",
            s0_after[0],s0_after[1],s0_after[2],s0_after[3]);

    bool erase_ok = (s0_after[0] == 0xFF && s0_after[1] == 0xFF &&
                     s0_after[2] == 0xFF && s0_after[3] == 0xFF);
    webLogf("app1 sector0 erase result: %s\n",
            erase_ok ? "SUCCESS (ffffffff)" : "FAILED (unchanged)");
  }

  server.send(200, "text/html; charset=utf-8",
    "<html><head><meta http-equiv='refresh' content='5;url=/log'>"
    "</head><body><p>OTA reset 実行。5秒後に /log で結果確認</p></body></html>");
}

void handleUpdatePage()
{
  server.send(200, "text/html; charset=utf-8",
    "<html><head><title>OTA Update</title></head><body>"
    "<h2>OTA Firmware Update</h2>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'><br><br>"
    "<input type='submit' value='Upload &amp; Flash'>"
    "</form>"
    "<p>ビルド: <code>pio run -e m5atom-ota</code></p>"
    "<p>ファイル: <code>.pio/build/m5atom-ota/firmware.bin</code></p>"
    "<p>アップロード中はページが固まります（約40秒）。完了後リブートします。</p>"
    "</body></html>");
}

void handleUpdateDone()
{
  if (ota_upload_err == ESP_OK && ota_flash_write_pending) {
    // staging 書き込み成功 → 再起動して startup コンテキストで sector0 を転記
    server.send(200, "text/html; charset=utf-8",
      "<html><head></head><body>"
      "<p>Upload OK. リブート後に sector0 を書き込みます。"
      "約15秒後に <a href='/log'>/log</a> で結果確認できます。</p>"
      "</body></html>");
    delay(1000);   // HTTP レスポンスの TCP フラッシュを待つ
    ESP.restart();
  } else {
    char msg[160];
    snprintf(msg, sizeof(msg),
      "<html><body><p>OTA 失敗 (esp_err=0x%x)。"
      "<a href='/update'>戻る</a></p></body></html>",
      ota_upload_err);
    server.send(500, "text/html; charset=utf-8", msg);
  }
}

void handleUpdateUpload()
{
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    webLogf("OTA upload start: %s\n", upload.filename.c_str());
    ota_upload_err        = ESP_OK;
    ota_first_logged      = false;
    ota_write_offset      = SPI_FLASH_SEC_SIZE;  // sector 1 から書く（sector 0 は最後）
    ota_sec0_size         = 0;
    ota_staging_done      = false;
    ota_staging_attempted = false;
    memset(ota_sec0_buf, 0xFF, sizeof(ota_sec0_buf));

    ota_upload_part = esp_ota_get_next_update_partition(NULL);
    if (!ota_upload_part) {
      ota_upload_err = ESP_ERR_NOT_FOUND;
      webLogf("OTA: no update partition\n");
      return;
    }
    webLogf("OTA part: %s @ 0x%x size=0x%x\n",
            ota_upload_part->label, ota_upload_part->address, ota_upload_part->size);
    webLogf("flash enc mode: %d (0=off 1=dev 2=release)\n",
            (int)esp_get_flash_encryption_mode());
    webLogf("free heap: %u\n", esp_get_free_heap_size());
    // esp_ota_begin() は呼ばない（WIP ガードを起動させないため）

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (ota_upload_err != ESP_OK) return;

    const uint8_t *src = upload.buf;
    size_t remaining   = upload.currentSize;

    if (!ota_first_logged && remaining >= 4) {
      ota_first_logged = true;
      webLogf("OTA recv[0..3]: %02x%02x%02x%02x\n",
              src[0], src[1], src[2], src[3]);
    }

    // sector 0 をバッファに蓄積（最初の 4096 バイト）
    if (ota_sec0_size < sizeof(ota_sec0_buf)) {
      size_t take = sizeof(ota_sec0_buf) - ota_sec0_size;
      if (take > remaining) take = remaining;
      memcpy(ota_sec0_buf + ota_sec0_size, src, take);
      ota_sec0_size += take;
      src       += take;
      remaining -= take;
    }

    // sector0 バッファが揃った瞬間に1度だけ: warmup write → staging 書き込みを試行
    // ota_staging_attempted で2回目以降をスキップ（毎 chunk で繰り返すと crash/timeout）
    if (!ota_staging_attempted && ota_sec0_size == sizeof(ota_sec0_buf)) {
      ota_staging_attempted = true;
      const uint32_t STAGING_MAGIC    = 0xDEADBEEF;
      const uint32_t STAGING_DATA_OFF = 0x100000;
      const uint32_t STAGING_HDR_OFF  = 0x101000;

      // sec0 バッファ先頭確認
      webLogf("OTA: sec0buf[0..3]=%02x%02x%02x%02x\n",
              ota_sec0_buf[0], ota_sec0_buf[1], ota_sec0_buf[2], ota_sec0_buf[3]);

      // warmup: sector1 (0x1000) に Core 0 経由で書いて IPC 到達を確認
      // sector1+ ループで後ほど上書きされるので害なし
      uint32_t warmup[4] = {0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00};
      esp_err_t ewu = flashErase(ota_upload_part, SPI_FLASH_SEC_SIZE, SPI_FLASH_SEC_SIZE);
      esp_err_t wwu = flashWrite(ota_upload_part, SPI_FLASH_SEC_SIZE, warmup, 16);
      uint8_t vwu[4] = {0};
      spi_flash_read(ota_upload_part->address + SPI_FLASH_SEC_SIZE, vwu, 4);
      bool warmup_ok = (vwu[0] == 0x44 && vwu[1] == 0x33 && vwu[2] == 0x22 && vwu[3] == 0x11);
      webLogf("OTA: warmup(Core0)@0x1000 e=0x%x w=0x%x rb=%02x%02x%02x%02x -> %s\n",
              ewu, wwu, vwu[0], vwu[1], vwu[2], vwu[3], warmup_ok ? "OK" : "FAIL");

      // staging 書き込み（Core 0 経由）
      webLogf("OTA: staging phys_hdr=0x%x\n", ota_upload_part->address + STAGING_HDR_OFF);
      esp_err_t ea = flashErase(ota_upload_part, STAGING_DATA_OFF, SPI_FLASH_SEC_SIZE);
      esp_err_t wa = flashWrite(ota_upload_part, STAGING_DATA_OFF,
                                ota_sec0_buf, sizeof(ota_sec0_buf));
      esp_err_t eb = flashErase(ota_upload_part, STAGING_HDR_OFF, SPI_FLASH_SEC_SIZE);
      uint8_t hdr[8];
      uint32_t sz32 = (uint32_t)sizeof(ota_sec0_buf);
      memcpy(hdr,     &STAGING_MAGIC, 4);
      memcpy(hdr + 4, &sz32,          4);
      esp_err_t wb = flashWrite(ota_upload_part, STAGING_HDR_OFF, hdr, 8);

      uint8_t vhdr[8] = {0};
      spi_flash_read(ota_upload_part->address + STAGING_HDR_OFF, vhdr, 8);
      uint32_t vmag = vhdr[0] | ((uint32_t)vhdr[1]<<8) |
                      ((uint32_t)vhdr[2]<<16) | ((uint32_t)vhdr[3]<<24);
      ota_staging_done = (ea==ESP_OK && wa==ESP_OK && eb==ESP_OK && wb==ESP_OK &&
                          vmag == STAGING_MAGIC);
      webLogf("OTA: staging ea=0x%x wa=0x%x eb=0x%x wb=0x%x vmag=0x%08x -> %s\n",
              ea, wa, eb, wb, vmag, ota_staging_done ? "OK" : "FAIL");
      // 失敗しても abort しない: sector1+ を継続し UPLOAD_FILE_END で再試行する
    }

    // sector 1 以降: Core 0 経由で消去→書き込み
    while (remaining > 0) {
      if ((ota_write_offset % SPI_FLASH_SEC_SIZE) == 0) {
        esp_err_t err = flashErase(ota_upload_part, ota_write_offset, SPI_FLASH_SEC_SIZE);
        if (err != ESP_OK) {
          ota_upload_err = err;
          webLogf("OTA erase failed @ 0x%x: 0x%x\n", ota_write_offset, err);
          return;
        }
      }
      size_t space      = SPI_FLASH_SEC_SIZE - (ota_write_offset % SPI_FLASH_SEC_SIZE);
      size_t write_size = (remaining < space) ? remaining : space;
      esp_err_t err = flashWrite(ota_upload_part, ota_write_offset, src, write_size);
      if (err != ESP_OK) {
        ota_upload_err = err;
        webLogf("OTA write failed @ 0x%x: 0x%x\n", ota_write_offset, err);
        return;
      }
      ota_write_offset += write_size;
      src              += write_size;
      remaining        -= write_size;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (ota_upload_err != ESP_OK) return;

    // sector 0 を最後に書く（OTA ハンドルなし → WIP ガードなし）
    uint32_t phys0 = ota_upload_part->address;  // sector 0 物理アドレス

    // フラッシュチップ情報
    uint32_t chip_id = 0;
    esp_flash_read_id(NULL, &chip_id);
    webLogf("flash chip id: 0x%06x\n", chip_id);

    // SR1/SR2 読み取り（ROM関数経由）
    uint32_t sr_lo = 0, sr_hi = 0;
    esp_rom_spiflash_read_status(&g_rom_flashchip, &sr_lo);
    esp_rom_spiflash_read_statushigh(&g_rom_flashchip, &sr_hi);
    webLogf("SR1=0x%02x SR2=0x%02x (CMP=%d QE=%d)\n",
            sr_lo & 0xFF, sr_hi & 0xFF,
            (int)((sr_hi >> 6) & 1), (int)((sr_hi >> 1) & 1));

    // CMP(bit6) と BP ビットをクリアして全保護を解除
    // QE(bit1) 等の他ビットは保持する
    bool needs_sr_clear = (sr_hi & 0x40) || (sr_lo & 0x7C);
    if (needs_sr_clear) {
      uint32_t new_sr1 = sr_lo & ~0x7C;          // BP[2:0], TB, SEC をクリア
      uint32_t new_sr2 = sr_hi & ~0x40;          // CMP をクリア
      uint32_t new_status = (new_sr1 & 0xFF) | ((new_sr2 & 0xFF) << 8);
      webLogf("clearing SR1 BP + SR2 CMP, new_status=0x%04x\n", new_status);
      int r = esp_rom_spiflash_write_status(&g_rom_flashchip, new_status);
      webLogf("write_status: %d\n", r);
      // 確認
      esp_rom_spiflash_read_status(&g_rom_flashchip, &sr_lo);
      esp_rom_spiflash_read_statushigh(&g_rom_flashchip, &sr_hi);
      webLogf("SR1=0x%02x SR2=0x%02x after clear\n", sr_lo & 0xFF, sr_hi & 0xFF);
    }

    // sector 0 物理値確認（startup erase の効果確認: ffffffff なら startup erase OK）
    uint8_t pre[4] = {0};
    spi_flash_read(phys0, pre, 4);
    webLogf("sector0 before write[0..3]: %02x%02x%02x%02x\n",
            pre[0], pre[1], pre[2], pre[3]);

    // sector 1 物理値確認（UPLOAD_FILE_WRITE の書き込みが効いたか確認）
    // ffffffff = sector1+ も silently fail、それ以外 = 正常書き込み
    uint8_t sec1[4] = {0};
    spi_flash_read(phys0 + SPI_FLASH_SEC_SIZE, sec1, 4);
    webLogf("sector1 after upload[0..3]: %02x%02x%02x%02x\n",
            sec1[0], sec1[1], sec1[2], sec1[3]);

    // sector 0 バッファ内容確認
    webLogf("ota_sec0_buf[0..3]: %02x%02x%02x%02x total=%u\n",
            ota_sec0_buf[0], ota_sec0_buf[1], ota_sec0_buf[2], ota_sec0_buf[3],
            upload.totalSize);

    // staging が UPLOAD_FILE_WRITE で未完了なら END コンテキスト（sector1+ 書き込み後）で再試行
    if (!ota_staging_done) {
      const uint32_t STAGING_MAGIC    = 0xDEADBEEF;
      const uint32_t STAGING_DATA_OFF = 0x100000;
      const uint32_t STAGING_HDR_OFF  = 0x101000;
      webLogf("OTA: retrying staging in END context (Core 0)\n");
      esp_err_t ea2 = flashErase(ota_upload_part, STAGING_DATA_OFF, SPI_FLASH_SEC_SIZE);
      esp_err_t wa2 = flashWrite(ota_upload_part, STAGING_DATA_OFF,
                                 ota_sec0_buf, sizeof(ota_sec0_buf));
      esp_err_t eb2 = flashErase(ota_upload_part, STAGING_HDR_OFF, SPI_FLASH_SEC_SIZE);
      uint8_t hdr2[8];
      uint32_t sz32b = (uint32_t)sizeof(ota_sec0_buf);
      memcpy(hdr2,     &STAGING_MAGIC, 4);
      memcpy(hdr2 + 4, &sz32b,         4);
      esp_err_t wb2 = flashWrite(ota_upload_part, STAGING_HDR_OFF, hdr2, 8);
      uint8_t vhdr2[8] = {0};
      spi_flash_read(ota_upload_part->address + STAGING_HDR_OFF, vhdr2, 8);
      uint32_t vmag2 = vhdr2[0] | ((uint32_t)vhdr2[1]<<8) |
                       ((uint32_t)vhdr2[2]<<16) | ((uint32_t)vhdr2[3]<<24);
      ota_staging_done = (ea2==ESP_OK && wa2==ESP_OK && eb2==ESP_OK && wb2==ESP_OK &&
                          vmag2 == STAGING_MAGIC);
      webLogf("OTA: staging retry ea=0x%x wa=0x%x eb=0x%x wb=0x%x vmag=0x%08x -> %s\n",
              ea2, wa2, eb2, wb2, vmag2, ota_staging_done ? "OK" : "FAIL");
    }

    if (!ota_staging_done) {
      webLogf("OTA: staging FAILED in both WRITE and END contexts\n");
      ota_upload_err = ESP_FAIL;
    } else {
      ota_flash_write_pending = true;
      webLogf("OTA: staging OK. Will write sector0 on next startup.\n");
    }

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    ota_upload_err = ESP_FAIL;
    webLogf("OTA upload aborted\n");
  }
}

void handleLog()
{
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent(
    "<html><head><title>Log</title>"
    "<meta http-equiv='refresh' content='60'>"
    "</head><body><pre>");
  int count = weblog_count;
  int start = (count < WEBLOG_LINES) ? 0 : weblog_head;
  for (int i = 0; i < count; i++) {
    int idx = (start + i) % WEBLOG_LINES;
    server.sendContent(weblog_buf[idx]);
    server.sendContent("\n");
  }
  server.sendContent("</pre></body></html>");
}

void handleDiag()
{
  String body = "<html><head><title>Diag</title></head><body><pre>";
  body += diagGetString();
  body += "</pre><a href='/diag/save'><button>Save to flash</button></a></body></html>";
  server.send(200, "text/html; charset=utf-8", body);
}

void handleDiagSave()
{
  diagSave();
  server.sendHeader("Location", "/diag");
  server.send(302);
}

void setupServerHandlers()
{
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8",
      "<html><head><title>m5atom-clock</title></head><body>"
      "<h2>m5atom-clock</h2>"
      "<ul>"
      "<li><a href='/log'>Log</a> &mdash; 動作ログ（直近300行）</li>"
      "<li><a href='/diag'>Diag</a> &mdash; 診断ログ（NVS保存）</li>"
      "<li><a href='/update'>Update</a> &mdash; ブラウザOTAアップロード</li>"
      "</ul>"
      "</body></html>");
  });
  server.on("/log",       HTTP_GET,  handleLog);
  server.on("/diag",      HTTP_GET,  handleDiag);
  server.on("/diag/save", HTTP_GET,  handleDiagSave);
  server.on("/update",    HTTP_GET,  handleUpdatePage);
  server.on("/update",    HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/ota-reset", HTTP_GET,  handleOtaReset);
}

void startServer()
{
  if (server_running) return;

  // staging 領域チェック: sector0 データが staging に書かれていれば startup コンテキストで転記
  // （HTTP handler / WiFi 実行中は sector0 書き込みが silently fail → app1 末尾2セクターを staging に使用）
  {
    const esp_partition_t* tgt = esp_ota_get_next_update_partition(NULL);
    if (tgt) {
      const uint32_t STAGING_MAGIC    = 0xDEADBEEF;
      const uint32_t STAGING_DATA_OFF = 0x100000;
      const uint32_t STAGING_HDR_OFF  = 0x101000;

      uint8_t vhdr[8] = {0};
      spi_flash_read(tgt->address + STAGING_HDR_OFF, vhdr, 8);
      uint32_t magic   = vhdr[0] | ((uint32_t)vhdr[1]<<8) |
                         ((uint32_t)vhdr[2]<<16) | ((uint32_t)vhdr[3]<<24);
      uint32_t sec0_sz = vhdr[4] | ((uint32_t)vhdr[5]<<8) |
                         ((uint32_t)vhdr[6]<<16) | ((uint32_t)vhdr[7]<<24);
      webLogf("OTA startup: staging magic=0x%08x sec0_sz=%u\n", magic, sec0_sz);

      if (magic == STAGING_MAGIC && sec0_sz > 0 && sec0_sz <= SPI_FLASH_SEC_SIZE) {
        webLogf("OTA startup: staging found! Reading sector0 data...\n");

        static uint8_t staging_sec0[SPI_FLASH_SEC_SIZE];
        spi_flash_read(tgt->address + STAGING_DATA_OFF, staging_sec0, SPI_FLASH_SEC_SIZE);
        webLogf("OTA startup: staging[0..3]=%02x%02x%02x%02x\n",
                staging_sec0[0], staging_sec0[1], staging_sec0[2], staging_sec0[3]);

        // staging 領域を先に消去（失敗しても無限ループ防止）
        esp_partition_erase_range(tgt, STAGING_DATA_OFF, SPI_FLASH_SEC_SIZE);
        esp_partition_erase_range(tgt, STAGING_HDR_OFF,  SPI_FLASH_SEC_SIZE);

        // 現在の sector0 物理値
        uint8_t cur[4] = {0};
        spi_flash_read(tgt->address, cur, 4);
        webLogf("OTA startup: sector0 current=%02x%02x%02x%02x\n",
                cur[0], cur[1], cur[2], cur[3]);

        // 0→1 遷移があれば erase が必要
        bool need_erase = false;
        for (int i = 0; i < 4; i++) {
          if (cur[i] & ~staging_sec0[i]) { need_erase = true; break; }
        }
        if (need_erase) {
          webLogf("OTA startup: erasing sector0...\n");
          esp_err_t ee = esp_partition_erase_range(tgt, 0, SPI_FLASH_SEC_SIZE);
          webLogf("OTA startup: erase 0x%x\n", ee);
        }

        esp_err_t we = esp_partition_write(tgt, 0, staging_sec0, sec0_sz);
        webLogf("OTA startup: write 0x%x\n", we);

        uint8_t after[4] = {0};
        spi_flash_read(tgt->address, after, 4);
        webLogf("OTA startup: sector0 after=%02x%02x%02x%02x (want %02x%02x%02x%02x)\n",
                after[0], after[1], after[2], after[3],
                staging_sec0[0], staging_sec0[1], staging_sec0[2], staging_sec0[3]);

        bool verify_ok = (after[0] == staging_sec0[0] && after[1] == staging_sec0[1] &&
                          after[2] == staging_sec0[2] && after[3] == staging_sec0[3]);
        if (verify_ok) {
          esp_err_t boot_err = esp_ota_set_boot_partition(tgt);
          webLogf("OTA startup: set_boot 0x%x\n", boot_err);
          if (boot_err == ESP_OK) {
            webLogf("OTA startup: SUCCESS! Rebooting to new firmware...\n");
            diagSave();
            delay(200);
            ESP.restart();
          } else {
            webLogf("OTA startup: set_boot FAILED 0x%x\n", boot_err);
          }
        } else {
          webLogf("OTA startup: write FAILED (mismatch)\n");
        }
      }
    }
  }

  // OTAパーティションにゴミデータが残っていれば消去（起動毎に1回チェック）
  // 注意: 有効なファームウェアヘッダ(0xE9)があれば消去しない
  static bool ota_checked = false;
  if (!ota_checked) {
    ota_checked = true;
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    if (next) {
      uint8_t buf[4] = {0};
      spi_flash_read(next->address, buf, 4);
      webLogf("OTA: next part sector0 physical: %02x%02x%02x%02x\n",
              buf[0], buf[1], buf[2], buf[3]);
      if (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF && buf[3] == 0xFF) {
        // クリーン（all 0xFF）: 何もしない
      } else if (buf[0] == 0xE9) {
        // 有効なファームウェアヘッダ: 消去しない
        webLogf("OTA: next part has firmware (0xE9), skip erase\n");
      } else {
        // ゴミデータ: 消去
        webLogf("OTA: partition dirty (garbage), erasing...\n");
        esp_err_t err = esp_partition_erase_range(next, 0, next->size);
        webLogf("OTA: erase %s\n", err == ESP_OK ? "done" : "FAILED");
      }
    }
  }

  // パーティション情報
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next    = esp_ota_get_next_update_partition(NULL);
  webLogf("OTA running: %s @ 0x%x\n",
          running ? running->label : "none",
          running ? running->address : 0);
  webLogf("OTA target:  %s @ 0x%x\n",
          next ? next->label : "NONE",
          next ? next->address : 0);

  server.begin();
  server_running = true;
  webLogf("WebServer: http://%s/\n", WiFi.localIP().toString().c_str());
}

void stopServer()
{
  if (!server_running) return;
  server.stop();
  server_running = false;
}


void loop()
{
  M5.update();

  // ボタン操作: 短押し=診断ログ出力、長押し3秒=完全リセット
  if (M5.Btn.wasReleased()) {
    diagPrint();
  }
  if (M5.Btn.pressedFor(3000)) {
    diagSave();
    ESP.restart();
  }

  // PIR読み取り（100ms loop周期でサンプリング、立ち上がりエッジでカウント）
  pir_state = digitalRead(PIR_PIN);
  if (!pir_prev && pir_state) {
    pir_count++;
    Serial.printf("PIR detected: %d\n", pir_count);
  }
  pir_prev = pir_state;


  // 100msティック処理（TcoGenフラグが立ったときのみ実行）
  if (tco_tick) {
    tco_tick = false;

    if (!ntp_synced) {
      // NTP未同期: セグメントに "----" を表示しながら同期完了を待つ
      tm1637.display(0, 16);
      tm1637.display(1, 16);
      tm1637.display(2, 16);
      tm1637.display(3, 16);
      tm1637.point(POINT_OFF);

      // NTP同期完了チェック（1秒タイムアウト）
      if (getLocalTime(&td, 1000)) {
        ntp_synced = true;
        ntp_fail_ms = 0;
      } else {
        // WiFi接続中にNTP同期が5分以上できない場合はWiFiリセット
        // （WiFi phantom状態でNTPパケットが通らないと永久に止まるため）
        if (wifi_ok) {
          if (ntp_fail_ms == 0) ntp_fail_ms = millis();
          if (millis() - ntp_fail_ms > 5UL * 60 * 1000) {
            webLogf("NTP stuck 5min, WiFi reset...\n");
            ntp_fail_ms = 0;
            doWifiReset();
          }
        }
      }

    } else {
      // NTP同期済み: RTCから時刻を読み出してセグメントとMatrixを更新
      // タイムアウト0: 即時リターン（デフォルト5000msはloop()をブロックするため使用しない）
      static int ntp_fail_count = 0;
      if (!getLocalTime(&td, 0)) {
        if (++ntp_fail_count >= 5) {
          ntp_synced     = false;
          ntp_fail_count = 0;
          ntp_fail_ms    = 0;  // タイマーはntp_synced=falseのブロックで計測開始
        }
      } else {
        ntp_fail_count = 0;

        // PIRデータ送信（実時刻の0/10/20/30/40/50分ちょうどに送信）
        time_t now_jst = time(NULL) + 9 * 3600;
        int real_min = (now_jst / 60) % 60;
        int real_sec = now_jst % 60;
        if (real_min % 10 == 0 && real_sec == 0) {
          int cur_slot = (int)(now_jst / 600);
          if (cur_slot != pir_last_slot) {
            int snap = pir_count;
            if (xQueueSend(send_queue, &snap, 0) != pdTRUE)
              Serial.printf("sendQueue full, PIR data discarded\n");
            pir_count = 0;
            pir_last_slot = cur_slot;
          }
        }

        const bool banner_active =
            (minute_banner[0] | minute_banner[1] | minute_banner[2] | minute_banner[3]) != 0;
        if (td.tm_sec < 1 && banner_active) {
          // 毎分0秒に minute_banner（config.h）を1秒間表示。全0なら通常表示を継続
          for (int i = 0; i < 4; ++i) tm1637WriteRaw(i, minute_banner[i]);
          tm1637.point(POINT_OFF);
        } else {
          // 通常表示: HH:MM（コロンは1秒ごとに点滅）
          tm1637.point(td.tm_sec & 1 ? POINT_ON : POINT_OFF);
          tm1637.display(0, td.tm_hour / 10);
          tm1637.display(1, td.tm_hour % 10);
          tm1637.display(2, td.tm_min / 10);
          tm1637.display(3, td.tm_min % 10);
        }
      }
    }

    // SNTP同期完了の反映
    if (ntp_just_synced) {
      ntp_just_synced = false;
      ntp_hour_synced = true;
    }

    UpdateMatrix();
  }

  // sendWorkerのハング監視: 3分以上完了しない場合はESP.restart()でリセット
  // タスクを外から強制終了するとTLSリソースがリークするため、完全リセットで確実に回収する
  if (worker_busy_since > 0 && millis() - worker_busy_since > 3UL * 60 * 1000) {
    webLogf("sendWorker hung 3min, restarting...\n");
    vps_error = true;
    diag_watchdog_cnt++;
    diagRecord(DIAG_WATCHDOG);
    ESP.restart();
  }

  // VPS失敗2回によるWiFiリセット要求
  if (do_wifi_reset) {
    doWifiReset();
  }

  // WiFi接続状態を更新・切断時は30秒ごとに再接続試行、5分でリセット
  bool new_wifi_ok = (WiFi.status() == WL_CONNECTED);

  // pirSendTaskからの強制再接続要求（タスク内でのWiFi操作は競合するためここで処理）
  // ソフト再接続では幽霊接続（WL_CONNECTEDだがDNS/通信不可）が直らないためdoWifiReset()を使う
  if (wifi_reconnect_requested) {
    wifi_reconnect_requested = false;
    webLogf("Forcing WiFi reset as requested...\n");
    doWifiReset();
    new_wifi_ok = false;
  } else {
    wifi_reconnect_requested = false;
  }

  if (!new_wifi_ok) {
    unsigned long now = millis();
    if (wifi_lost_ms == 0) {
      wifi_lost_ms = now;
      stopServer();
    }
    if (now - wifi_lost_ms > 5UL * 60 * 1000) {
      webLogf("WiFi lost 5min, restarting...\n");
      diagSave();
      ESP.restart();
    } else if (wifi_reconnect_ms == 0 || now - wifi_reconnect_ms > 30000) {
      webLogf("WiFi reconnect...\n");
      WiFi.disconnect();
      delay(100);
      WiFi.begin(wifi_ssid, wifi_pass);
      wifi_reconnect_ms = now;
    }
  } else {
    if (!wifi_ok) {
      // 切断→接続復帰の瞬間: Power Saveを無効化（再接続後にデフォルト設定に戻るため）
      esp_wifi_set_ps(WIFI_PS_NONE);
      startServer();
    }
    wifi_lost_ms      = 0;
    wifi_reconnect_ms = 0;
  }
  wifi_ok = new_wifi_ok;

  // WebServer 処理（10ms刻み）
  for (int i = 0; i < loop_delay_ms / 10; i++) {
    if (server_running) server.handleClient();
    delay(10);
  }

  // OTA: handleUpdateDone() が ESP.restart() を呼ぶため、ここには到達しない
}

/*
The MIT License
SPDX short identifier: MIT

Copyright 2020 BotanicFields, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
// TM1637 動作確認用テストコード
#include "M5Atom.h"
#include "TM1637.h"
#define CLK 32
#define DIO 26
TM1637 tm1637(CLK, DIO);
void setup()
{
  tm1637.init();
  tm1637.set(BRIGHT_TYPICAL);  // BRIGHT_DARKEST=0, BRIGHT_TYPICAL=2, BRIGHTEST=7
}
void loop()
{
  int8_t NumTab[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};  // 0~9,A,b,C,d,E,F
  int8_t ListDisp[4];
  unsigned char i = 0;
  unsigned char count = 0;
  delay(150);
  while (1) {
    i = count;
    count++;
    if (count == sizeof(NumTab)) count = 0;
    for (unsigned char BitSelect = 0; BitSelect < 4; BitSelect++) {
      ListDisp[BitSelect] = NumTab[i];
      i++;
      if (i == sizeof(NumTab)) i = 0;
    }
    tm1637.display(0, ListDisp[0]);
    tm1637.display(1, ListDisp[1]);
    tm1637.display(2, ListDisp[2]);
    tm1637.display(3, ListDisp[3]);
    delay(300);
  }
}
*/
