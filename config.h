#pragma once

// ===========================================================================
//  UBPD - Unknown Bluetooth Pocket Device  v0.1.0
//
//  Карманный инструмент на классическом ESP32: BLE-сканер, RSSI-радар,
//  подписка на свои устройства, смена своего BLE-имени, диагностика.
//
//  Рамки проекта:
//   * работаем только с открытыми (broadcast) данными и с добровольными
//     подключениями к СВОИМ устройствам;
//   * нет подмены MAC-адреса, нет деаутентификации, нет эксплойтов;
//   * используй устройство только там, где сканирование эфира разрешено.
// ===========================================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#define UBPD_VERSION "0.3.0"

// ---------- OLED 128x64, 4-пиновый модуль (VCC GND SCL SDA) ----------
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_W 128
#define SCREEN_H 64
#define FONT_BODY u8g2_font_5x7_t_cyrillic
#define FONT_HEAD u8g2_font_6x12_t_cyrillic

// ---------- Кнопки: кнопка -> GND, INPUT_PULLUP ----------
// Плата ещё без кнопок: пока управление идёт через Serial (см. README).
#define BTN_UP 32
#define BTN_DOWN 33
#define BTN_OK 25
#define BTN_BACK 26
#define BTN_DEBOUNCE_MS 40
#define BTN_REPEAT_MS 220
#define BTN_LONG_MS 700
#define SERIAL_CONTROL 1

#define LED_PIN 2

// ---------- Батарея: Li-Ion + делитель напряжения на ADC1 ----------
// BATT_ENABLED=1 включить, когда подпаяешь делитель к GPIO.
#define BATT_ENABLED 0
#define BATT_ADC_PIN 34
#define BATT_DIVIDER 2.0f
#define BATT_V_MIN 3.30f
#define BATT_V_MAX 4.20f

// ---------- Внутренний датчик температуры (классический ESP32) ----------
// Если линковка ругается на temperatureRead - поставь 0.
#define TEMP_ENABLED 0

// ---------- Пищалка ----------
// BUZZER_ENABLED 1 = пищалка подключена (пин ниже), 0 = выключить совсем.
// BUZZER_ACTIVE 1 = АКТИВНЫЙ буззер (внутри свой генератор).
//             0 = ПАССИВНЫЙ (динамик-пьезо): управляется частотой через tone().
//
// ДВА ВАРИАНТА ПОДКЛЮЧЕНИЯ - выбери по факту:
//
// 1) BUZZER_INVERT 0 - буззер на 3.3 В:  "+" -> GPIO27, "-" -> GND.
//    Звук = пин HIGH, тишина = пин LOW.
//    Если на 3.3 В буззер молчит даже при проверке - он 5-вольтовый (частый случай),
//    тогда вариант 2.
//
// 2) BUZZER_INVERT 1 - 5-вольтовый буззер: "+" -> 5V (VIN), "-" -> GPIO27.
//    Звук = пин LOW (тянет минус на землю), тишина = пин в воздухе (high-Z, ток не течёт).
//    Так буззер получает свои 5 В, а не жалкие 3.3 В.
//
// Правильный (для здоровья GPIO) вариант для 5 В - транзистор: база через 1 кОм на
// GPIO27, эмиттер на GND, коллектор на "-" буззера, "+" на 5V. Тогда ставим INVERT 0.
//
// Малый 3.3 В буззер (до ~20 мА) можно вешать на пин напрямую.
#define BUZZER_ENABLED 0   // пока ЗАБЫЛИ про буззер: 1 - включить обратно
#define BUZZER_ACTIVE 1
#define BUZZER_INVERT 0
#define BUZZER_PIN 27
#define BUZZER_FREQ 2200
#define BUZZER_ON_MS 80
#define BUZZER_GAP_MS 120

// Отладка пищалки в Serial (1 - печатать каждое событие)
#define BUZZER_DEBUG 0

// ---------- WEB UI (локальная точка доступа) ----------
#define AP_SSID_BASE "UBPD"
#define AP_PASS "ubpd1234"
#define WEB_PORT 80
#define MAX_RSSI_LOG 96

// ---------- Сон / энергосбережение ----------
#define SLEEP_SCAN_MS 4000
#define SLEEP_STATUS_MS 1500

// ---------- Лимиты ----------
#define MAX_DEVICES 24
#define MAX_FAVS 16
#define MAX_WIFI 16
#define DEV_NAME_LEN 28
#define DEV_SVC_LEN 44
#define CONN_LINES 6
#define CONN_LINE_LEN 27
#define RADAR_PTS 10
#define SERIAL_BAUD 115200

// ---------- Отладка радара в Serial (1 - печатать точки и координаты) ----------
#define RADAR_DEBUG 0

// ---------- Отладка скана: печатать MAC + имя каждого нового устройства ----------
#define SCAN_DEBUG 1

// ---------- Тайминги ----------
#define STALE_MS 30000
#define FORGET_MS 300000
#define FAV_GONE_MS 10000
#define FRAME_RADAR_MS 60
#define FRAME_UI_MS 120

// ==================== ТИПЫ ====================

enum DevType : uint8_t {
  DT_UNKNOWN = 0, DT_AUDIO, DT_HID, DT_BEACON, DT_PHONE, DT_COMPUTER,
  DT_WATCH, DT_TRACKER, DT_PRINTER, DT_CAMERA, DT_NETWORK
};

// Категория для режима CLEAR SCAN
enum DevKind : uint8_t {
  KIND_NONE = 0, KIND_PHONE, KIND_HEADPHONE, KIND_SPEAKER, KIND_MIC
};

// Фоновые функции: по умолчанию НИЧЕГО не живёт в фоне само по себе -
// что именно работает после выхода из вкладки решает MANAGER.
enum BgFunc : uint8_t {
  BG_BLEFUN = 0, BG_IDENT, BG_WATCHMON, BG_TIMED, BG_WEBAP, BG_COUNT
};

// 4 состояния фона для каждой функции
enum BgState : uint8_t {
  BG_OFF = 0,        // только в своей вкладке
  BG_BG,             // работает в фоне
  BG_SLEEP,          // работает только во время SLEEP WATCH
  BG_BG_SLEEP        // работает и в фоне, и во сне
};

enum SortMode : uint8_t { SORT_RSSI = 0, SORT_NAME, SORT_TYPE };

enum Screen : uint8_t {
  SCR_MENU = 0, SCR_RADAR, SCR_SCANNER, SCR_DEVICE, SCR_REMOTE, SCR_WATCH,
  SCR_IDENTITY, SCR_BLEFUN, SCR_TIMED, SCR_TYPE, SCR_CLEAR, SCR_SLEEP,
  SCR_SETTINGS, SCR_MANAGER, SCR_WEB, SCR_DIAG
};

enum BtnId : uint8_t { B_UP = 0, B_DOWN, B_OK, B_BACK, B_COUNT };
enum BtnEv : uint8_t { EV_UP = 0, EV_DOWN, EV_OK, EV_BACK, EV_OK_LONG, EV_BACK_LONG };
enum RadarSource : uint8_t { RS_BLE = 0, RS_WIFI };

struct BleDevice {
  bool used;
  uint8_t addr[6];
  char name[DEV_NAME_LEN];
  char svc[DEV_SVC_LEN];
  int8_t rssi;
  int8_t txPower;
  uint16_t appearance;
  uint16_t companyId;
  uint32_t firstSeen;
  uint32_t lastSeen;
  uint16_t advCount;
  DevType type;
  uint8_t confidence;
  uint8_t kind;         // CLEAR SCAN: KIND_*
  uint8_t kindConf;     // уверенность категории, %
};

struct FavDevice {
  bool used;
  uint8_t addr[6];
  char name[DEV_NAME_LEN];
};

struct WifiNet {
  char ssid[33];
  int8_t rssi;
  uint8_t ch;
  bool open;
};

struct AppState {
  Screen screen;
  bool redraw;
  uint8_t menuCursor;
  uint8_t menuTop;
  uint8_t listCursor;
  uint8_t listTop;
  uint8_t selAddr[6];
  bool selValid;
  int16_t devIndex;
  uint8_t deviceFrom;
  uint8_t devAction;
  uint8_t connState;            // 0 нет, 1 подключение, 2 подключено, 3 ошибка
  uint8_t connLineCount;
  char connLines[CONN_LINES][CONN_LINE_LEN];
  uint8_t radarCursor;
  uint8_t radarSource;
  bool radarSweep;
  bool radarOverlay;
  uint8_t radarOpt;
  uint8_t favCursor;
  uint8_t identIdx;
  bool identEditing;
  uint8_t identPos;
  char customNameStr[9];
  uint8_t funCursor;
  uint8_t funMode;              // 0 SINGLE, 1 CYCLE, 2 RANDOM
  bool funRunning;
  uint8_t funIndex;
  uint32_t funNextSwitch;
  uint8_t timedCursor;
  uint16_t timedInterval;
  uint16_t timedDuration;
  uint16_t timedRuns;
  bool timedRunning;
  uint8_t timedPhase;           // 0 скан, 1 пауза
  uint16_t timedDone;
  uint32_t timedDeadline;
  uint16_t timedFound;
  char timedLog[3][14];
  uint8_t timedLogCount;
  // --- v0.2 ---
  uint8_t settingsCursor;
  bool buzzerOn;
  bool buzzerInvert;     // 5-вольтовая схема (звук на LOW)
  bool buzzerPassive;    // пассивный буззер (нужна частота, tone())
  int8_t strongRssi;
  uint16_t sleepInterval;
  uint8_t sleepCursor;
  bool sleeping;
  uint8_t sleepPhase;         // 0 скан, 1 показ статуса, 2 сон
  uint32_t sleepDeadline;
  uint16_t sleepEvents;
  uint8_t sleepFound;
  uint8_t sleepLost;
  uint8_t logCount;
  uint8_t logHead;
  bool apOpen;
  // --- v0.3: менеджер фона ---
  uint8_t bg[BG_COUNT];
  uint8_t mgrCursor;
  uint8_t sortMode;
  uint8_t diagPage;
};

struct RssiSample {
  uint8_t addr[6];
  int8_t rssi;
  uint32_t t;
};

// ==================== ГЛОБАЛЬНЫЕ ====================
// Одна единица трансляции: все вкладки .ino склеиваются в один файл,
// поэтому глобалы живут здесь и видны всем.

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

uint8_t brightness = 200;

BleDevice devices[MAX_DEVICES];
portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t advCount = 0;
volatile uint16_t slotRecycles = 0;

int16_t scanOrder[MAX_DEVICES];
uint8_t scanOrderCount = 0;

// Отфильтрованный список для CLEAR SCAN (только телефон/наушники/колонка/микрофон)
int16_t clearOrder[MAX_DEVICES];
uint8_t clearCount = 0;

FavDevice favorites[MAX_FAVS];

WifiNet wifiNets[MAX_WIFI];
uint8_t wifiCount = 0;
bool wifiScanning = false;
uint32_t wifiLastScan = 0;
uint32_t wifiScansDone = 0;
bool wifiRadioOn = false;

bool bleScanning = false;
bool advRunning = false;
char advName[34] = {0};

BLEClient *activeClient = nullptr;

uint16_t errorCount = 0;
char lastError[40] = {0};

Preferences prefs;
AppState app;

RssiSample rssiLog[MAX_RSSI_LOG];

WebServer webServer(WEB_PORT);
bool webRunning = false;

char toastMsg[44] = {0};
uint32_t toastUntil = 0;
