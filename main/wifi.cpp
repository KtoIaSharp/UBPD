#include "config.h"

#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>

// ==================== WI-FI НА ЧИСТОМ ESP-IDF ====================
// Раньше здесь был Arduino-класс WiFi (WiFi.mode / WiFi.scanNetworks / WiFi.softAP).
// Теперь это esp_wifi + esp_netif напрямую: один владелец радио, явные события,
// никаких скрытых переключений режима.
//
// Классический ESP32 делит один радиомодуль между Wi-Fi и Bluetooth, поэтому
// режим Wi-Fi поднимается только когда он реально нужен (радар Wi-Fi, точка
// доступа) и гасится, когда вкладка закрыта - это делает syncRadios().

static bool wifiInited = false;
static esp_netif_t *netifSta = nullptr;
static esp_netif_t *netifAp = nullptr;
static volatile bool scanDone = false;
static bool apUp = false;

static void wifiEventHandler(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)data;
  if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) scanDone = true;
}

// Инициализация ровно один раз: esp_wifi_init повторно вызывать нельзя.
static void wifiEnsure() {
  if (wifiInited) return;
  esp_netif_init();
  esp_event_loop_create_default();
  netifSta = esp_netif_create_default_wifi_sta();
  netifAp = esp_netif_create_default_wifi_ap();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    setError("wifi init failed");
    return;
  }
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, nullptr, nullptr);
  wifiInited = true;
}

bool wifiIsReady() { return wifiInited; }

// ==================== СКАН ЭФИРА ====================
void wifiScanStart() {
  wifiEnsure();
  if (!wifiInited || wifiScanning) return;

  // Точка доступа, если она поднята, должна выжить - поэтому APSTA.
  wifi_mode_t mode = apUp ? WIFI_MODE_APSTA : WIFI_MODE_STA;
  if (esp_wifi_set_mode(mode) != ESP_OK) {
    setError("wifi mode failed");
    return;
  }
  // Радио должно быть запущено: esp_wifi_stop() в wifiRadioOff() его гасит.
  if (esp_wifi_start() != ESP_OK && !wifiRadioOn) {
    setError("wifi start failed");
    return;
  }

  wifi_scan_config_t sc = {};
  sc.show_hidden = false;
  scanDone = false;
  if (esp_wifi_scan_start(&sc, false) != ESP_OK) {  // false = асинхронно
    setError("wifi scan failed");
    return;
  }
  wifiScanning = true;
  wifiLastScan = millis();
  wifiRadioOn = true;
}

void wifiScanPoll() {
  if (!wifiScanning) return;
  if (!scanDone) return;
  scanDone = false;

  uint16_t found = 0;
  esp_wifi_scan_get_ap_num(&found);
  uint16_t cnt = (found > MAX_WIFI) ? MAX_WIFI : found;
  wifi_ap_record_t recs[MAX_WIFI];
  if (cnt) esp_wifi_scan_get_ap_records(&cnt, recs);

  for (uint16_t i = 0; i < cnt; i++) {
    snprintf(wifiNets[i].ssid, sizeof(wifiNets[i].ssid), "%s", (const char *)recs[i].ssid);
    if (!wifiNets[i].ssid[0]) snprintf(wifiNets[i].ssid, sizeof(wifiNets[i].ssid), "<hidden>");
    wifiNets[i].rssi = recs[i].rssi;
    wifiNets[i].ch = recs[i].primary;
    wifiNets[i].open = (recs[i].authmode == WIFI_AUTH_OPEN);
  }
  wifiCount = (uint8_t)cnt;
  wifiScanning = false;
  wifiScansDone++;
  Serial.printf("[i] Wi-Fi сетей: %u\n", (uint8_t)cnt);
}

void wifiScanStop() {
  if (!wifiScanning) return;
  esp_wifi_scan_stop();
  wifiScanning = false;
  scanDone = false;
}

// Заглушить радио полностью: иначе Wi-Fi держит эфир и мешает Bluetooth.
void wifiRadioOff() {
  if (!wifiRadioOn && !apUp) return;
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_NULL);
  wifiRadioOn = false;
  apUp = false;
  webRunning = false;
}

// Мягкий старт радио (после esp_wifi_stop его надо поднять снова).
void wifiStart() {
  wifi_mode_t cur = WIFI_MODE_NULL;
  esp_wifi_get_mode(&cur);
  if (cur == WIFI_MODE_NULL) {
    esp_wifi_set_mode(apUp ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  }
  esp_wifi_start();
  wifiRadioOn = true;
}

// ==================== ТОЧКА ДОСТУПА ====================
bool apStart(const char *ssid, const char *pass, bool open) {
  wifiEnsure();
  if (!wifiInited) return false;

  wifi_config_t cfg = {};
  snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), "%s", ssid);
  cfg.ap.ssid_len = (uint8_t)strlen(ssid);
  cfg.ap.channel = 1;
  cfg.ap.max_connection = 4;
  cfg.ap.authmode = open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  if (!open) snprintf((char *)cfg.ap.password, sizeof(cfg.ap.password), "%s", pass);

  // Режим APSTA: точка доступа не мешает сканеру и эфиру BLE.
  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (err != ESP_OK) return false;
  err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
  if (err != ESP_OK) return false;
  err = esp_wifi_start();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) return false;

  apUp = true;
  wifiRadioOn = true;
  return true;
}

void apStop() {
  if (!apUp) return;
  apUp = false;
  esp_wifi_set_mode(wifiScanning ? WIFI_MODE_STA : WIFI_MODE_NULL);
  Serial.println("[i] Точка доступа выключена");
}

bool apIsUp() { return apUp; }

const char *apIpString() {
  static char buf[16];
  buf[0] = 0;
  if (!netifAp) return buf;
  esp_netif_ip_info_t ip = {};
  if (esp_netif_get_ip_info(netifAp, &ip) != ESP_OK) return buf;
  snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
  return buf;
}

int apClientCount() {
  if (!netifAp) return 0;
  wifi_sta_list_t list = {};
  if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return 0;
  return list.num;
}
