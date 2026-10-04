#include "config.h"
#include "web_page.h"

// ==================== LOCAL WEB UI (v0.2) ====================
// ESP32 поднимает свою точку доступа и отдаёт страницу с настройками, списком
// устройств, WATCH, IDENTITY, BLE FUN, диагностикой и CSV-логом RSSI.
// OLED остаётся основным интерфейсом: страница нужна, чтобы не тыкать кнопками
// при настройке.
//
// Честно: Wi-Fi и BLE делят один радиомодуль, поэтому с включённой точкой доступа
// скан идёт медленнее и RSSI грубее, чем без неё.

static char apSsid[20] = {0};

// ==================== УТИЛИТЫ ====================
static bool parseMac(const String &s, uint8_t *out) {
  int v[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
  return true;
}

static void macToStr(const uint8_t *a, char *out, size_t outLen) {
  snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

static int16_t deviceSlotByMac(const uint8_t *a) {
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].used && memcmp(devices[i].addr, a, 6) == 0) return (int16_t)i;
  }
  return -1;
}

static bool favFoundNow(const uint8_t *a) {
  uint32_t now = millis();
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].used) continue;
    if (memcmp(devices[i].addr, a, 6) != 0) continue;
    if (now - devices[i].lastSeen > FAV_GONE_MS) continue;
    return true;
  }
  return false;
}

// ==================== ОБРАБОТЧИКИ ====================
static void webRoot() {
  webServer.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

static void webApiState() {
  String j = "{";
  j += "\"ver\":\"" UBPD_VERSION "\",";
  j += "\"heap\":" + String(ESP.getFreeHeap() / 1024) + ",";
  j += "\"uptime\":" + String(millis() / 1000) + ",";
  j += "\"ble\":" + String(bleScanning ? 1 : 0) + ",";
  j += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  j += "\"adv\":\"" + String(advName) + "\",";
  j += "\"buzzer\":" + String(app.buzzerOn ? 1 : 0) + ",";
  j += "\"buzzerHw\":\"" + String(BUZZER_ENABLED ? "железо есть" : "заглушка: нет буззера") + "\",";
  j += "\"strong\":" + String(app.strongRssi) + ",";
  j += "\"sleep\":" + String(app.sleepInterval) + ",";
  j += "\"bright\":" + String(brightness) + ",";
  j += "\"ap\":\"" + String(apSsid) + "\",";
  j += "\"apOpen\":" + String(app.apOpen ? 1 : 0) + ",";
  j += "\"apPass\":\"" AP_PASS "\",";
  j += "\"identIdx\":" + String(app.identIdx) + ",";
  j += "\"custom\":\"" + String(app.customNameStr) + "\",";
  j += "\"funMode\":" + String(app.funMode) + ",";
  j += "\"funRun\":" + String(app.funRunning ? 1 : 0) + ",";
  j += "\"funIdx\":" + String(app.funIndex) + ",";

  j += "\"presets\":[";
  for (uint8_t i = 0; i < identNameCount(); i++) {
    j += String(i ? "," : "") + "\"" + String(identNameAt(i)) + "\"";
  }
  j += "],";
  j += "\"funs\":[";
  for (uint8_t i = 0; i < funNameCount(); i++) {
    j += String(i ? "," : "") + "\"" + String(funNameAt(i)) + "\"";
  }
  j += "],";
  j += "\"funModes\":[";
  for (uint8_t i = 0; i < 3; i++) j += String(i ? "," : "") + "\"" + String(funModeName(i)) + "\"";
  j += "],";

  j += "\"devices\":[";
  bool first = true;
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].used) continue;
    char mac[20];
    macToStr(devices[i].addr, mac, sizeof(mac));
    if (!first) j += ",";
    first = false;
    j += "{\"name\":\"" + String(devices[i].name) + "\",\"mac\":\"" + String(mac) + "\",";
    j += "\"rssi\":" + String(devices[i].rssi) + ",";
    j += "\"type\":\"" + String(typeNameAt(devices[i].type)) + "\",";
    j += "\"conf\":" + String(devices[i].confidence) + ",";
    j += "\"watch\":" + String(isFavorite(devices[i].addr) ? 1 : 0) + "}";
  }
  j += "],";

  j += "\"watch\":[";
  first = true;
  for (int f = 0; f < MAX_FAVS; f++) {
    if (!favorites[f].used) continue;
    char mac[20];
    macToStr(favorites[f].addr, mac, sizeof(mac));
    if (!first) j += ",";
    first = false;
    int16_t r = favRssi((int16_t)f);
    j += "{\"name\":\"" + String(favorites[f].name) + "\",\"mac\":\"" + String(mac) + "\",";
    j += "\"found\":" + String(r != 127 ? 1 : 0) + ",\"rssi\":" + String(r == 127 ? 0 : r) + "}";
  }
  j += "]}";

  webServer.send(200, "application/json; charset=utf-8", j);
}

static void webApiScan() {
  int on = webServer.hasArg("on") ? webServer.arg("on").toInt() : 1;
  if (on) bleScanStart();
  else bleScanStop();
  webServer.send(200, "text/plain", "ok");
}

static void webApiWatch() {
  uint8_t mac[6];
  if (webServer.hasArg("mac") && parseMac(webServer.arg("mac"), mac)) {
    int16_t slot = deviceSlotByMac(mac);
    if (slot >= 0) toggleFavorite(slot);
    else webServer.send(200, "text/plain", "not in air");
    if (slot >= 0) webServer.send(200, "text/plain", "ok");
  } else {
    webServer.send(400, "text/plain", "bad mac");
  }
}

static void webApiWatchDel() {
  uint8_t mac[6];
  if (webServer.hasArg("mac") && parseMac(webServer.arg("mac"), mac)) {
    int16_t f = findFavIndex(mac);
    if (f >= 0) {
      favorites[f].used = false;
      storageSaveFavorites();
    }
    webServer.send(200, "text/plain", "ok");
  } else {
    webServer.send(400, "text/plain", "bad mac");
  }
}

static void webApiIdentity() {
  if (webServer.hasArg("idx")) {
    int idx = webServer.arg("idx").toInt();
    if (idx >= 0 && idx < (int)identNameCount()) app.identIdx = (uint8_t)idx;
  } else if (webServer.hasArg("name")) {
    String n = webServer.arg("name");
    snprintf(app.customNameStr, sizeof(app.customNameStr), "%s", n.c_str());
    app.identIdx = (uint8_t)(identNameCount() - 1);
  }
  storageSaveSettings();
  webServer.send(200, "text/plain", "ok");
}

static void webApiFun() {
  if (webServer.hasArg("mode")) {
    int m = webServer.arg("mode").toInt();
    if (m >= 0 && m < 3) app.funMode = (uint8_t)m;
  }
  if (webServer.hasArg("idx")) {
    int i = webServer.arg("idx").toInt();
    if (i >= 0 && i < (int)funNameCount()) {
      app.funIndex = (uint8_t)i;
      app.funRunning = true;
    }
  }
  if (webServer.hasArg("run")) {
    app.funRunning = webServer.arg("run").toInt() != 0;
  }
  storageSaveSettings();
  webServer.send(200, "text/plain", "ok");
}

static void webApiSettings() {
  if (webServer.hasArg("buzzer")) app.buzzerOn = webServer.arg("buzzer").toInt() != 0;
  if (webServer.hasArg("strong")) app.strongRssi = (int8_t)constrain(webServer.arg("strong").toInt(), -100, -20);
  if (webServer.hasArg("sleep")) app.sleepInterval = (uint16_t)constrain(webServer.arg("sleep").toInt(), 10, 600);
  if (webServer.hasArg("bright")) brightness = (uint8_t)constrain(webServer.arg("bright").toInt(), 10, 255);
  if (webServer.hasArg("apOpen")) app.apOpen = webServer.arg("apOpen").toInt() != 0;
  u8g2.setContrast(brightness);
  storageSaveSettings();
  webServer.send(200, "text/plain", "ok");
}

static void webApiLogCsv() {
  String csv = "uptime_s,mac,rssi\n";
  uint8_t count = app.logCount < MAX_RSSI_LOG ? app.logCount : MAX_RSSI_LOG;
  for (uint8_t i = 0; i < count; i++) {
    uint8_t idx = (uint8_t)((app.logHead + MAX_RSSI_LOG - count + i) % MAX_RSSI_LOG);
    char mac[20];
    macToStr(rssiLog[idx].addr, mac, sizeof(mac));
    csv += String(rssiLog[idx].t / 1000) + "," + String(mac) + "," + String(rssiLog[idx].rssi) + "\n";
  }
  webServer.sendHeader("Content-Disposition", "attachment; filename=ubpd_rssi.csv");
  webServer.send(200, "text/csv; charset=utf-8", csv);
}

static void webApiNvsReset() {
  prefs.clear();
  webServer.send(200, "text/plain", "cleared");
  Serial.println("[i] NVS очищен через WEB UI");
  delay(200);
  ESP.restart();
}

static void webApiBeep() {
  buzzerTest();
  webServer.send(200, "text/plain", "beep");
}

static void webApiReboot() {
  webServer.send(200, "text/plain", "reboot");
  delay(200);
  ESP.restart();
}

static void webNotFound() {
  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/plain", "");
}

// ==================== ЖИЗНЕННЫЙ ЦИКЛ ====================
void webInit() {
  if (webRunning) return;

  uint8_t m[6] = {0};
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  snprintf(apSsid, sizeof(apSsid), "%s-%02X%02X", AP_SSID_BASE, m[4], m[5]);

  WiFi.mode(WIFI_AP);
  delay(50);
  bool ok = app.apOpen ? WiFi.softAP(apSsid) : WiFi.softAP(apSsid, AP_PASS);
  wifiRadioOn = true;
  if (!ok) {
    setError("softAP failed");
    toast("AP FAIL", 1500);
    return;
  }

  webServer.on("/", webRoot);
  webServer.on("/api/state", webApiState);
  webServer.on("/api/scan", webApiScan);
  webServer.on("/api/watch", webApiWatch);
  webServer.on("/api/watch/del", webApiWatchDel);
  webServer.on("/api/identity", webApiIdentity);
  webServer.on("/api/fun", webApiFun);
  webServer.on("/api/settings", webApiSettings);
  webServer.on("/api/log.csv", webApiLogCsv);
  webServer.on("/api/beep", webApiBeep);
  webServer.on("/api/nvs/reset", webApiNvsReset);
  webServer.on("/api/reboot", webApiReboot);
  webServer.onNotFound(webNotFound);
  webServer.begin();
  webRunning = true;

  Serial.printf("[i] WEB UI: SSID \"%s\", http://%s/ (%s)\n", apSsid,
                WiFi.softAPIP().toString().c_str(), app.apOpen ? "открытая" : "по паролю");
}

void webStop() {
  if (!webRunning) return;
  webServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiRadioOn = false;
  webRunning = false;
  Serial.println("[i] WEB UI остановлен");
}

void webTick() {
  if (webRunning) webServer.handleClient();
}

bool webIsRunning() { return webRunning; }

const char *webApSsid() { return apSsid; }
