#include "config.h"

#include <nvs.h>
#include <nvs_flash.h>

// NVS напрямую через esp-idf (nvs_flash/nvs), без обёртки Preferences из Arduino.
// Пространство имён оставлено прежним ("ubpd"), поэтому настройки, сохранённые
// старой Arduino-сборкой, читаются этой без миграции.
//
// SD-карта по-прежнему не используется - она появится позже как доп. хранилище.

static const char *NVS_NS = "ubpd";
static bool nvsReady = false;

static void storageEnsure() {
  if (nvsReady) return;
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  nvsReady = (err == ESP_OK);
  if (!nvsReady) setError("nvs init failed");
}

// ---------- запись ----------
static void nvsSetU8(const char *key, uint8_t v) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u8(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

static void nvsSetI8(const char *key, int8_t v) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_i8(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

static void nvsSetU16(const char *key, uint16_t v) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u16(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

static void nvsSetStr(const char *key, const char *v) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

static void nvsSetBlob(const char *key, const void *data, size_t len) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_blob(h, key, data, len);
  nvs_commit(h);
  nvs_close(h);
}

// ---------- чтение ----------
static uint8_t nvsGetU8(const char *key, uint8_t def) {
  nvs_handle_t h;
  uint8_t v = def;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return def;
  nvs_get_u8(h, key, &v);
  nvs_close(h);
  return v;
}

static int8_t nvsGetI8(const char *key, int8_t def) {
  nvs_handle_t h;
  int8_t v = def;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return def;
  nvs_get_i8(h, key, &v);
  nvs_close(h);
  return v;
}

static uint16_t nvsGetU16(const char *key, uint16_t def) {
  nvs_handle_t h;
  uint16_t v = def;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return def;
  nvs_get_u16(h, key, &v);
  nvs_close(h);
  return v;
}

static void nvsGetStr(const char *key, char *out, size_t outLen, const char *def) {
  snprintf(out, outLen, "%s", def ? def : "");
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
  size_t len = outLen;
  if (nvs_get_str(h, key, out, &len) != ESP_OK) snprintf(out, outLen, "%s", def ? def : "");
  nvs_close(h);
}

// Читает blob в buf; true только если размер совпал с ожидаемым.
static bool nvsGetBlob(const char *key, void *buf, size_t len) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
  size_t got = len;
  esp_err_t err = nvs_get_blob(h, key, buf, &got);
  nvs_close(h);
  return (err == ESP_OK && got == len);
}

// ---------- публичное ----------
void storageSaveFavorites() {
  storageEnsure();
  nvsSetBlob("favs", favorites, sizeof(favorites));
}

void storageSaveSettings() {
  storageEnsure();
  nvsSetU8("identIdx", app.identIdx);
  nvsSetStr("custom", app.customNameStr);
  nvsSetU8("radarSrc", app.radarSource);
  nvsSetU8("radarSwp", app.radarSweep ? 1 : 0);
  nvsSetU8("funMode", app.funMode);
  nvsSetU8("bright", brightness);
  nvsSetU16("tInt", app.timedInterval);
  nvsSetU16("tDur", app.timedDuration);
  nvsSetU16("tRun", app.timedRuns);
  nvsSetU8("buzzer", app.buzzerOn ? 1 : 0);
  nvsSetU8("buzzerInv", app.buzzerInvert ? 1 : 0);
  nvsSetU8("buzzerPas", app.buzzerPassive ? 1 : 0);
  nvsSetI8("strong", app.strongRssi);
  nvsSetU16("sleepInt", app.sleepInterval);
  nvsSetU8("apOpen", app.apOpen ? 1 : 0);
  nvsSetBlob("bg", app.bg, sizeof(app.bg));
  nvsSetU8("sort", app.sortMode);
}

// Сброс настроек: чистим только своё пространство имён (как Preferences.clear()).
void storageReset() {
  storageEnsure();
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_all(h);
  nvs_commit(h);
  nvs_close(h);
}

void storageLoad() {
  storageEnsure();

  app.identIdx = nvsGetU8("identIdx", app.identIdx);
  nvsGetStr("custom", app.customNameStr, sizeof(app.customNameStr), app.customNameStr);
  app.radarSource = nvsGetU8("radarSrc", app.radarSource);
  app.radarSweep = nvsGetU8("radarSwp", app.radarSweep ? 1 : 0) != 0;
  app.funMode = nvsGetU8("funMode", app.funMode);
  brightness = nvsGetU8("bright", brightness);
  app.timedInterval = nvsGetU16("tInt", app.timedInterval);
  app.timedDuration = nvsGetU16("tDur", app.timedDuration);
  app.timedRuns = nvsGetU16("tRun", app.timedRuns);
  app.buzzerOn = nvsGetU8("buzzer", app.buzzerOn ? 1 : 0) != 0;
  app.buzzerInvert = nvsGetU8("buzzerInv", app.buzzerInvert ? 1 : 0) != 0;
  app.buzzerPassive = nvsGetU8("buzzerPas", app.buzzerPassive ? 1 : 0) != 0;
  app.strongRssi = nvsGetI8("strong", app.strongRssi);
  app.sleepInterval = nvsGetU16("sleepInt", app.sleepInterval);
  app.apOpen = nvsGetU8("apOpen", app.apOpen ? 1 : 0) != 0;

  if (nvsGetBlob("bg", app.bg, sizeof(app.bg))) {
    for (uint8_t i = 0; i < BG_COUNT; i++) {
      if (app.bg[i] > BG_BG_SLEEP) app.bg[i] = BG_BG;
    }
  }
  app.sortMode = nvsGetU8("sort", app.sortMode);
  if (app.sortMode > SORT_TYPE) app.sortMode = SORT_RSSI;

  if (app.identIdx > 7) app.identIdx = 0;
  if (app.radarSource > RS_WIFI) app.radarSource = RS_BLE;
  if (app.funMode > 2) app.funMode = 0;
  if (app.timedInterval < 10 || app.timedInterval > 600) app.timedInterval = 60;
  if (app.timedDuration < 5 || app.timedDuration > 60) app.timedDuration = 10;
  if (app.timedRuns < 1 || app.timedRuns > 100) app.timedRuns = 20;
  if (app.strongRssi > -20 || app.strongRssi < -100) app.strongRssi = -60;
  if (app.sleepInterval < 15 || app.sleepInterval > 300) app.sleepInterval = 30;

  if (nvsGetBlob("favs", favorites, sizeof(favorites))) {
    for (int i = 0; i < MAX_FAVS; i++) {
      if (favorites[i].used) favorites[i].name[DEV_NAME_LEN - 1] = 0;
    }
  } else {
    memset(favorites, 0, sizeof(favorites));
  }

  Serial.printf("[i] NVS: имя idx=%u, радар=%s, избранных=%d\n",
                (unsigned)app.identIdx, app.radarSource == RS_BLE ? "BLE" : "WI-FI", favCount());
}

int favCount() {
  int n = 0;
  for (int i = 0; i < MAX_FAVS; i++) {
    if (favorites[i].used) n++;
  }
  return n;
}
