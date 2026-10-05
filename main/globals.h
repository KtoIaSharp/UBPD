#pragma once

// Глобальные объекты UBPD.
//
// Раньше они были прямо в config.h: скетч Arduino склеивался в ОДИН файл,
// поэтому повторных определений не возникало. Теперь main/ собирается как
// несколько .cpp, и общие переменные должны лежать в одном месте (globals.cpp),
// а остальные файлы видеть только extern-объявления.

#include "config.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

extern uint8_t brightness;

extern BleDevice devices[MAX_DEVICES];
extern portMUX_TYPE devMux;
extern volatile uint32_t advCount;
extern volatile uint16_t slotRecycles;

extern int16_t scanOrder[MAX_DEVICES];
extern uint8_t scanOrderCount;

// Общий список вкладки SCANNER (классика + BLE в одном списке). Кодировка:
// значение >= 0 - индекс в devices (BLE), значение < 0 - индекс в classics,
// равный (-1 - значение). Классика всегда идёт выше BLE.
extern int16_t combOrder[MAX_DEVICES + MAX_CLASSIC];
extern uint8_t combCount;

extern FavDevice favorites[MAX_FAVS];

extern WifiNet wifiNets[MAX_WIFI];
extern uint8_t wifiCount;
extern bool wifiScanning;
extern uint32_t wifiLastScan;
extern uint32_t wifiScansDone;
extern bool wifiRadioOn;

// --- v0.4: классический Bluetooth ---
extern ClassicDevice classics[MAX_CLASSIC];
extern portMUX_TYPE clsMux;
extern int16_t classicOrder[MAX_CLASSIC];
extern uint8_t classicCount;
extern volatile bool classicScanning;
extern volatile bool classicWant;
extern uint32_t classicLastStart;

extern bool bleScanning;
extern bool advRunning;
extern char advName[34];

extern BLEClient *activeClient;

extern uint16_t errorCount;
extern char lastError[40];

extern AppState app;

extern RssiSample rssiLog[MAX_RSSI_LOG];

extern bool webRunning;

extern char toastMsg[44];
extern uint32_t toastUntil;
