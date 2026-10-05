#include "config.h"

// ==================== SLEEP WATCH ====================
// Экономичный режим: OLED гаснет, сканы редкие (раз в app.sleepInterval секунд),
// на событие - LED и пищалка. Так устройство можно носить с собой.
//
// Честно про питание: light sleep на классическом ESP32 с поднятым BLE-стеком
// может быть отклонён контроллером - тогда вместо сна просто пауза (см. лог).

static bool sleepPrev[MAX_FAVS];
static bool sleepStrongPrev[MAX_FAVS];
static uint8_t sleepPrevCount = 0;

static void sleepArmButtonWake() {
  gpio_wakeup_enable((gpio_num_t)BTN_UP, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_DOWN, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_OK, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_BACK, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
}

static bool anyButtonDown() {
  return digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW ||
         digitalRead(BTN_OK) == LOW || digitalRead(BTN_BACK) == LOW;
}

static void sleepDoze(uint16_t seconds) {
  buzzerSilence();  // ВАЖНО: пока спим, цикл заблокирован и пищать некому -
                    // активный буззер держал бы HIGH всё время сна
  sleepArmButtonWake();
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  Serial.flush();  // иначе последние строки лога теряются при выключении UART
  uint32_t t0 = millis();
  esp_err_t err = esp_light_sleep_start();
  uint32_t spent = millis() - t0;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  if (err != ESP_OK || spent < 200) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Serial.printf("[i] light sleep недоступен (код %d) - пауза через delay\n", (int)err);
    }
    delay((uint32_t)seconds * 1000UL);
  }
}

void sleepStart() {
  if (favCount() == 0) {
    toast("СНАЧАЛА WATCH", 2000);
    return;
  }
  buzzerSilence();
  app.sleeping = true;
  app.sleepPhase = 0;
  app.sleepEvents = 0;
  app.sleepFound = 0;
  app.sleepLost = 0;
  for (int i = 0; i < MAX_FAVS; i++) sleepPrev[i] = false;
  // реклама может продолжаться во сне, если MANAGER разрешил
  if (!sleepAllows(BG_BLEFUN) && !sleepAllows(BG_IDENT)) advStop();
  if (webRunning) {
    webStop();
    Serial.println("[i] SLEEP: точка доступа выключена (во сне Wi-Fi не живёт)");
  }
  bleScanStart();
  app.sleepDeadline = millis() + SLEEP_SCAN_MS;
  Serial.printf("[i] SLEEP WATCH: интервал %u с, избранных %d\n",
                (unsigned)app.sleepInterval, favCount());
}

void sleepStop() {
  app.sleeping = false;
  app.sleepPhase = 0;
  bleScanStop();
  u8g2.setPowerSave(0);
}

static void sleepCheckFavorites(uint32_t now) {
  uint8_t found = 0, lost = 0;
  bool strongNew = false;
  // события во сне отдаём только если MANAGER разрешил WATCH в режиме СОН
  bool report = sleepAllows(BG_WATCHMON);

  for (int f = 0; f < MAX_FAVS; f++) {
    if (!favorites[f].used) {
      sleepPrev[f] = false;
      sleepStrongPrev[f] = false;
      continue;
    }
    bool online = false;
    bool isStrong = false;
    for (int i = 0; i < MAX_DEVICES; i++) {
      if (!devices[i].used) continue;
      if (memcmp(devices[i].addr, favorites[f].addr, 6) != 0) continue;
      if (now - devices[i].lastSeen > FAV_GONE_MS) continue;
      online = true;
      if (devices[i].rssi >= app.strongRssi) isStrong = true;
      break;
    }
    if (online && !sleepPrev[f]) {
      found++;
      if (report) {
        ledPulse(300);
        toast("* НАЙДЕНО", 1500);
        buzzerPlay(1);
      }
      Serial.printf("[*] SLEEP: найдено %s%s\n", favorites[f].name, report ? "" : " (без сигнала)");
    } else if (!online && sleepPrev[f]) {
      lost++;
      if (report) buzzerPlay(2);
      Serial.printf("[i] SLEEP: потеряно %s%s\n", favorites[f].name, report ? "" : " (без сигнала)");
    }
    // «сильный сигнал» - только на переходе, иначе пищало бы каждый цикл
    if (isStrong && !sleepStrongPrev[f]) strongNew = true;
    sleepStrongPrev[f] = isStrong;
    sleepPrev[f] = online;
  }

  if (strongNew && report) {
    buzzerPlay(3);
    ledPulse(400);
  }
  app.sleepFound = found;
  app.sleepLost = lost;
  sleepPrevCount = (uint8_t)(found + lost);
}

void sleepTick() {
  if (!app.sleeping) return;
  uint32_t now = millis();
  if ((int32_t)(app.sleepDeadline - now) > 0) return;

  if (app.sleepPhase == 0) {
    bleScanStop();
    buildSortedDeviceList();
    sleepCheckFavorites(now);
    app.sleepEvents++;
    Serial.printf("[i] SLEEP цикл %u: скан %u уст., найдено %u, потеряно %u\n",
                  (unsigned)app.sleepEvents, (unsigned)scanOrderCount,
                  (unsigned)app.sleepFound, (unsigned)app.sleepLost);
    app.sleepPhase = 1;
    app.sleepDeadline = now + SLEEP_STATUS_MS;
    u8g2.setPowerSave(0);
    return;
  }

  // фаза 1: показали статус и уходим в сон
  u8g2.setPowerSave(1);
  app.sleepPhase = 2;
  Serial.printf("[i] SLEEP: OLED выкл, пауза %u с (выход - любая кнопка или символ в Serial)\n",
                (unsigned)app.sleepInterval);
  sleepDoze(app.sleepInterval);
  u8g2.setPowerSave(0);
  if (anyButtonDown() || Serial.available()) {
    sleepStop();
    toast("ВЫХОД ИЗ СНА", 1500);
    Serial.println("[i] SLEEP WATCH выключен");
    return;
  }
  bleScanStart();
  app.sleepPhase = 0;
  app.sleepDeadline = millis() + SLEEP_SCAN_MS;
}
