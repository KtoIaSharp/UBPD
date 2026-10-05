#include "globals.h"

// Определения глобальных объектов UBPD - ровно один раз на всю программу.
// Объявления (extern) лежат в globals.h, типы - в config.h.

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

uint8_t brightness = 200;

BleDevice devices[MAX_DEVICES];
portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t advCount = 0;
volatile uint16_t slotRecycles = 0;

int16_t scanOrder[MAX_DEVICES];
uint8_t scanOrderCount = 0;

// Общий список вкладки SCANNER (классика + BLE). Кодировка - см. globals.h.
int16_t combOrder[MAX_DEVICES + MAX_CLASSIC];
uint8_t combCount = 0;

FavDevice favorites[MAX_FAVS];

WifiNet wifiNets[MAX_WIFI];
uint8_t wifiCount = 0;
bool wifiScanning = false;
uint32_t wifiLastScan = 0;
uint32_t wifiScansDone = 0;
bool wifiRadioOn = false;

// --- v0.4: классический Bluetooth ---
ClassicDevice classics[MAX_CLASSIC];
portMUX_TYPE clsMux = portMUX_INITIALIZER_UNLOCKED;
int16_t classicOrder[MAX_CLASSIC];
uint8_t classicCount = 0;
volatile bool classicScanning = false;
volatile bool classicWant = false;
uint32_t classicLastStart = 0;

bool bleScanning = false;
bool advRunning = false;
char advName[34] = {0};

BLEClient *activeClient = nullptr;

uint16_t errorCount = 0;
char lastError[40] = {0};

AppState app;

RssiSample rssiLog[MAX_RSSI_LOG];

bool webRunning = false;

char toastMsg[44] = {0};
uint32_t toastUntil = 0;
