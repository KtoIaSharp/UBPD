#include "config.h"
#include "web_page.h"

#include <esp_http_server.h>
#include <stdarg.h>

// ==================== LOCAL WEB UI НА ЧИСТОМ ESP-IDF ====================
// Раньше тут был Arduino-класс WebServer (webServer.on / send / hasArg).
// Теперь это esp_http_server: обычные обработчики httpd_req_t, явные URI,
// никакого ручного handleClient() в главном цикле.
//
// Точка доступа поднимается через apStart() из wifi.cpp (esp_wifi), а не
// через Arduino-класс WiFi.
//
// Честно: Wi-Fi и Bluetooth делят один радиомодуль, поэтому с включённой точкой
// доступа скан идёт медленнее и RSSI грубее, чем без неё.

static char apSsid[20] = {0};
static httpd_handle_t srv = nullptr;

// ==================== УТИЛИТЫ ====================
static bool parseMac(const char *s, uint8_t *out) {
  int v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
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

// Аргумент из query-строки: /api/fun?idx=2
static bool reqArg(httpd_req_t *req, const char *key, char *out, size_t outLen) {
  size_t qlen = httpd_req_get_url_query_len(req) + 1;
  if (qlen <= 1) return false;
  char *q = (char *)malloc(qlen);
  if (!q) return false;
  bool ok = false;
  if (httpd_req_get_url_query_str(req, q, qlen) == ESP_OK) {
    char val[80];
    if (httpd_query_key_value(q, key, val, sizeof(val)) == ESP_OK) {
      snprintf(out, outLen, "%.*s", outLen > 0 ? (int)(outLen - 1) : 0, val);
      ok = true;
    }
  }
  free(q);
  return ok;
}

static int reqArgInt(httpd_req_t *req, const char *key, int def) {
  char v[32];
  if (!reqArg(req, key, v, sizeof(v))) return def;
  return atoi(v);
}

static esp_err_t sendText(httpd_req_t *req, const char *s) {
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  return httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
}

// Небольшая наращивалка JSON/CSV: пишем в buf, следим за границей.
static void jaddf(char *buf, size_t cap, size_t *used, const char *fmt, ...) {
  if (*used >= cap) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + *used, cap - *used, fmt, ap);
  va_end(ap);
  if (n > 0) *used += (size_t)n;
  if (*used >= cap) *used = cap - 1;
}

static void jsonEsc(char *dst, size_t dstLen, const char *src) {
  size_t k = 0;
  for (size_t i = 0; src[i] && k + 1 < dstLen; i++) {
    char c = src[i];
    if (c == '"' || c == '\\') {
      if (k + 2 >= dstLen) break;
      dst[k++] = '\\';
      dst[k++] = c;
    } else if ((uint8_t)c >= 0x20) {
      dst[k++] = c;
    }
  }
  dst[k] = 0;
}

// ==================== ОБРАБОТЧИКИ ====================
static esp_err_t webRoot(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t webApiState(httpd_req_t *req) {
  const size_t cap = 8192;
  char *j = (char *)malloc(cap);
  if (!j) return httpd_resp_send_500(req);
  size_t u = 0;
  char esc[80];

  jaddf(j, cap, &u, "{\"ver\":\"" UBPD_VERSION "\",");
  jaddf(j, cap, &u, "\"heap\":%u,", (unsigned)(ESP.getFreeHeap() / 1024));
  jaddf(j, cap, &u, "\"uptime\":%u,", (unsigned)(millis() / 1000));
  jaddf(j, cap, &u, "\"ble\":%d,", bleScanning ? 1 : 0);
  jaddf(j, cap, &u, "\"cls\":%d,", classicScanning ? 1 : 0);
  jaddf(j, cap, &u, "\"clsCount\":%u,", (unsigned)classicCount);
  jaddf(j, cap, &u, "\"clients\":%d,", apClientCount());
  jaddf(j, cap, &u, "\"adv\":\"%s\",", advName);
  jaddf(j, cap, &u, "\"buzzer\":%d,", app.buzzerOn ? 1 : 0);
  jaddf(j, cap, &u, "\"buzzerHw\":\"%s\",", BUZZER_ENABLED ? "железо есть" : "заглушка: нет буззера");
  jaddf(j, cap, &u, "\"strong\":%d,", app.strongRssi);
  jaddf(j, cap, &u, "\"sleep\":%u,", (unsigned)app.sleepInterval);
  jaddf(j, cap, &u, "\"bright\":%u,", (unsigned)brightness);
  jaddf(j, cap, &u, "\"ap\":\"%s\",", apSsid);
  jaddf(j, cap, &u, "\"apOpen\":%d,", app.apOpen ? 1 : 0);
  jaddf(j, cap, &u, "\"apPass\":\"" AP_PASS "\",");
  jaddf(j, cap, &u, "\"identIdx\":%u,", (unsigned)app.identIdx);
  jsonEsc(esc, sizeof(esc), app.customNameStr);
  jaddf(j, cap, &u, "\"custom\":\"%s\",", esc);
  jaddf(j, cap, &u, "\"funMode\":%u,", (unsigned)app.funMode);
  jaddf(j, cap, &u, "\"funRun\":%d,", app.funRunning ? 1 : 0);
  jaddf(j, cap, &u, "\"funIdx\":%u,", (unsigned)app.funIndex);

  jaddf(j, cap, &u, "\"presets\":[");
  for (uint8_t i = 0; i < identNameCount(); i++) {
    jsonEsc(esc, sizeof(esc), identNameAt(i));
    jaddf(j, cap, &u, "%s\"%s\"", i ? "," : "", esc);
  }
  jaddf(j, cap, &u, "],");

  jaddf(j, cap, &u, "\"funs\":[");
  for (uint8_t i = 0; i < funNameCount(); i++) {
    jsonEsc(esc, sizeof(esc), funNameAt(i));
    jaddf(j, cap, &u, "%s\"%s\"", i ? "," : "", esc);
  }
  jaddf(j, cap, &u, "],");

  jaddf(j, cap, &u, "\"funModes\":[");
  for (uint8_t i = 0; i < 3; i++) jaddf(j, cap, &u, "%s\"%s\"", i ? "," : "", funModeName(i));
  jaddf(j, cap, &u, "],");

  jaddf(j, cap, &u, "\"devices\":[");
  bool first = true;
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].used) continue;
    char mac[20];
    macToStr(devices[i].addr, mac, sizeof(mac));
    jsonEsc(esc, sizeof(esc), devices[i].name);
    jaddf(j, cap, &u,
          "%s{\"name\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,\"type\":\"%s\",\"conf\":%u,\"watch\":%d}",
          first ? "" : ",", esc, mac, devices[i].rssi, typeNameAt(devices[i].type),
          (unsigned)devices[i].confidence, isFavorite(devices[i].addr) ? 1 : 0);
    first = false;
  }
  jaddf(j, cap, &u, "],");

  jaddf(j, cap, &u, "\"watch\":[");
  first = true;
  for (int f = 0; f < MAX_FAVS; f++) {
    if (!favorites[f].used) continue;
    char mac[20];
    macToStr(favorites[f].addr, mac, sizeof(mac));
    jsonEsc(esc, sizeof(esc), favorites[f].name);
    int16_t r = favRssi((int16_t)f);
    jaddf(j, cap, &u, "%s{\"name\":\"%s\",\"mac\":\"%s\",\"found\":%d,\"rssi\":%d}",
          first ? "" : ",", esc, mac, (r != 127) ? 1 : 0, (r == 127) ? 0 : r);
    first = false;
  }
  jaddf(j, cap, &u, "],");

  jaddf(j, cap, &u, "\"classic\":[");
  first = true;
  for (int i = 0; i < MAX_CLASSIC; i++) {
    if (!classics[i].used) continue;
    char mac[20];
    char nm[CLASSIC_NAME_LEN];
    macToStr(classics[i].addr, mac, sizeof(mac));
    classicLabel(&classics[i], nm, CLASSIC_NAME_LEN - 1);
    jsonEsc(esc, sizeof(esc), nm);
    jaddf(j, cap, &u,
          "%s{\"name\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,\"type\":\"%s\",\"cod\":\"0x%X\",\"watch\":%d}",
          first ? "" : ",", esc, mac, classics[i].rssi, typeNameAt(classics[i].type),
          (unsigned)classics[i].cod, isFavorite(classics[i].addr) ? 1 : 0);
    first = false;
  }
  jaddf(j, cap, &u, "]}");

  httpd_resp_set_type(req, "application/json; charset=utf-8");
  esp_err_t err = httpd_resp_send(req, j, HTTPD_RESP_USE_STRLEN);
  free(j);
  return err;
}

static esp_err_t webApiScan(httpd_req_t *req) {
  if (reqArgInt(req, "on", 1)) bleScanStart();
  else bleScanStop();
  return sendText(req, "ok");
}

static esp_err_t webApiClassic(httpd_req_t *req) {
  if (reqArgInt(req, "on", 1)) classicScanStart();
  else classicScanStop();
  return sendText(req, "ok");
}

static esp_err_t webApiWatch(httpd_req_t *req) {
  char m[40];
  uint8_t mac[6];
  if (!reqArg(req, "mac", m, sizeof(m)) || !parseMac(m, mac)) return sendText(req, "bad mac");

  int16_t slot = deviceSlotByMac(mac);
  if (slot >= 0) {
    toggleFavorite(slot);
  } else {
    // устройство из классического поиска: BLE его не видит
    int16_t ci = classicLookupByAddr(mac);
    if (ci < 0) return sendText(req, "not in air");
    toggleFavoriteClassic(ci);
  }
  return sendText(req, "ok");
}

static esp_err_t webApiWatchDel(httpd_req_t *req) {
  char m[40];
  uint8_t mac[6];
  if (!reqArg(req, "mac", m, sizeof(m)) || !parseMac(m, mac)) return sendText(req, "bad mac");
  int16_t f = findFavIndex(mac);
  if (f >= 0) {
    favorites[f].used = false;
    storageSaveFavorites();
  }
  return sendText(req, "ok");
}

static esp_err_t webApiIdentity(httpd_req_t *req) {
  char v[40];
  if (reqArg(req, "idx", v, sizeof(v))) {
    int idx = atoi(v);
    if (idx >= 0 && idx < (int)identNameCount()) app.identIdx = (uint8_t)idx;
  } else if (reqArg(req, "name", v, sizeof(v))) {
    snprintf(app.customNameStr, sizeof(app.customNameStr), "%.*s",
             (int)(sizeof(app.customNameStr) - 1), v);
    app.identIdx = (uint8_t)(identNameCount() - 1);
  }
  storageSaveSettings();
  return sendText(req, "ok");
}

static esp_err_t webApiFun(httpd_req_t *req) {
  char v[40];
  if (reqArg(req, "mode", v, sizeof(v))) {
    int m = atoi(v);
    if (m >= 0 && m < 3) app.funMode = (uint8_t)m;
  }
  if (reqArg(req, "idx", v, sizeof(v))) {
    int i = atoi(v);
    if (i >= 0 && i < (int)funNameCount()) {
      app.funIndex = (uint8_t)i;
      app.funRunning = true;
    }
  }
  if (reqArg(req, "run", v, sizeof(v))) app.funRunning = atoi(v) != 0;
  storageSaveSettings();
  return sendText(req, "ok");
}

static esp_err_t webApiSettings(httpd_req_t *req) {
  char v[40];
  if (reqArg(req, "buzzer", v, sizeof(v))) app.buzzerOn = atoi(v) != 0;
  if (reqArg(req, "strong", v, sizeof(v))) app.strongRssi = (int8_t)constrain(atoi(v), -100, -20);
  if (reqArg(req, "sleep", v, sizeof(v))) app.sleepInterval = (uint16_t)constrain(atoi(v), 10, 600);
  if (reqArg(req, "bright", v, sizeof(v))) brightness = (uint8_t)constrain(atoi(v), 10, 255);
  if (reqArg(req, "apOpen", v, sizeof(v))) app.apOpen = atoi(v) != 0;
  u8g2.setContrast(brightness);
  storageSaveSettings();
  return sendText(req, "ok");
}

static esp_err_t webApiLogCsv(httpd_req_t *req) {
  const size_t cap = 6 * 1024;
  char *csv = (char *)malloc(cap);
  if (!csv) return httpd_resp_send_500(req);
  size_t u = 0;
  jaddf(csv, cap, &u, "uptime_s,mac,rssi\n");
  uint8_t count = app.logCount < MAX_RSSI_LOG ? app.logCount : MAX_RSSI_LOG;
  for (uint8_t i = 0; i < count; i++) {
    uint8_t idx = (uint8_t)((app.logHead + MAX_RSSI_LOG - count + i) % MAX_RSSI_LOG);
    char mac[20];
    macToStr(rssiLog[idx].addr, mac, sizeof(mac));
    jaddf(csv, cap, &u, "%u,%s,%d\n", (unsigned)(rssiLog[idx].t / 1000), mac, rssiLog[idx].rssi);
  }
  httpd_resp_set_type(req, "text/csv; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=ubpd_rssi.csv");
  esp_err_t err = httpd_resp_send(req, csv, HTTPD_RESP_USE_STRLEN);
  free(csv);
  return err;
}

static esp_err_t webApiNvsReset(httpd_req_t *req) {
  storageReset();
  sendText(req, "cleared");
  Serial.println("[i] NVS очищен через WEB UI");
  vTaskDelay(pdMS_TO_TICKS(200));
  ESP.restart();
  return ESP_OK;
}

static esp_err_t webApiBeep(httpd_req_t *req) {
  buzzerTest();
  return sendText(req, "beep");
}

// PC REMOTE: текст на экран UBPD. Тело POST = текст, ?secs= сколько секунд.
static esp_err_t webApiOled(httpd_req_t *req) {
  char tmp[PC_MSG_LEN];
  if (reqArg(req, "clear", tmp, sizeof(tmp))) {
    app.pcMsg[0] = 0;
    app.pcMsgUntil = 0;
    return sendText(req, "cleared");
  }
  int secs = reqArgInt(req, "secs", 5);
  if (secs < 1) secs = 1;
  if (secs > 60) secs = 60;

  char buf[PC_MSG_LEN];
  size_t total = 0;
  if (req->content_len > 0) {
    int r = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (r > 0) total = (size_t)r;
  }
  buf[total] = 0;
  if (total == 0) reqArg(req, "text", buf, sizeof(buf));  // запасной путь: ?text=

  if (buf[0]) {
    snprintf(app.pcMsg, sizeof(app.pcMsg), "%s", buf);
    app.pcMsgUntil = millis() + (uint32_t)secs * 1000UL;
    app.pcCount++;
  }
  return sendText(req, "ok");
}

static esp_err_t webApiReboot(httpd_req_t *req) {
  sendText(req, "reboot");
  vTaskDelay(pdMS_TO_TICKS(200));
  ESP.restart();
  return ESP_OK;
}

static esp_err_t webNotFound(httpd_req_t *req, httpd_err_code_t err) {
  (void)err;
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/");
  return httpd_resp_send(req, nullptr, 0);
}

// ==================== МАРШРУТЫ ====================
// В IDF 4.4 нет HTTP_ANY, поэтому каждый обработчик регистрируется и на GET,
// и на POST: страница читает состояние через GET, а команды шлёт POST-ом.
static const httpd_uri_t ROUTES[] = {
    {"/", HTTP_GET, webRoot, nullptr},
    {"/api/state", HTTP_GET, webApiState, nullptr},
    {"/api/scan", HTTP_GET, webApiScan, nullptr},
    {"/api/classic", HTTP_GET, webApiClassic, nullptr},
    {"/api/watch", HTTP_GET, webApiWatch, nullptr},
    {"/api/watch/del", HTTP_GET, webApiWatchDel, nullptr},
    {"/api/identity", HTTP_GET, webApiIdentity, nullptr},
    {"/api/fun", HTTP_GET, webApiFun, nullptr},
    {"/api/settings", HTTP_GET, webApiSettings, nullptr},
    {"/api/log.csv", HTTP_GET, webApiLogCsv, nullptr},
    {"/api/beep", HTTP_GET, webApiBeep, nullptr},
    {"/api/oled", HTTP_POST, webApiOled, nullptr},
    {"/api/nvs/reset", HTTP_GET, webApiNvsReset, nullptr},
    {"/api/reboot", HTTP_GET, webApiReboot, nullptr},
};
#define ROUTE_COUNT (sizeof(ROUTES) / sizeof(ROUTES[0]))

// ==================== ЖИЗНЕННЫЙ ЦИКЛ ====================
void webInit() {
  if (webRunning && srv) return;

  uint8_t m[6] = {0};
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  snprintf(apSsid, sizeof(apSsid), "%s-%02X%02X", AP_SSID_BASE, m[4], m[5]);

  if (!apStart(apSsid, AP_PASS, app.apOpen)) {
    setError("softAP failed");
    toast("AP FAIL", 1500);
    return;
  }

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = ROUTE_COUNT * 2 + 2;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  if (httpd_start(&srv, &cfg) != ESP_OK) {
    setError("httpd start failed");
    apStop();
    srv = nullptr;
    return;
  }
  for (size_t i = 0; i < ROUTE_COUNT; i++) {
    httpd_register_uri_handler(srv, &ROUTES[i]);
    httpd_uri_t post = ROUTES[i];
    post.method = HTTP_POST;
    httpd_register_uri_handler(srv, &post);
  }
  httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, webNotFound);

  webRunning = true;
  Serial.printf("[i] WEB UI: SSID \"%s\", http://%s/ (%s)\n", apSsid, apIpString(),
                app.apOpen ? "открытая" : "по паролю");
}

void webStop() {
  if (!webRunning && !srv) return;
  if (srv) {
    httpd_stop(srv);
    srv = nullptr;
  }
  apStop();
  webRunning = false;
  Serial.println("[i] WEB UI остановлен");
}

// esp_http_server работает в своей задаче: ручного опроса в цикле не нужно.
// Функция оставлена, чтобы не менять вызовы в ui.cpp.
void webTick() {}

bool webIsRunning() { return webRunning; }

const char *webApSsid() { return apSsid; }
