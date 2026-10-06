#include "config.h"

#include <esp_spp_api.h>
#include <esp_bt_device.h>
#include <cJSON.h>

// ==================== PC REMOTE — Bluetooth SPP + JSON ====================
// Управление прибором ТОЛЬКО с ПК по классическому Bluetooth (SPP), без Wi-Fi
// и без точки доступа. Обмен — JSON-строками (одна на строку, '\n' в конце).
// Кадры GIF приходят БИНАРНО: ПК кодирует GIF в кадры 1-бит 128x64 (1024 Б),
// ESP только копирует кадр в буфер дисплея и шлёт на экран — без вычислений.
//
// Команды:
//   {"cmd":"oled","text":"...","secs":5}   текст на экран на N секунд
//   {"cmd":"clear"}                         стереть
//   {"cmd":"state"}                         запрос статуса (ответ JSON)
//   {"cmd":"anim","n":<кадров>}             дальше идут n*1024 байт кадров
// Ответы: {"ok":1,...}

#define FRAME_BYTES ((SCREEN_W * SCREEN_H) / 8)  // 128*64/8 = 1024

static uint32_t sConnHandle = 0xFFFFFFFF;
static bool sConnected = false;

static char sLine[256];
static size_t sLineLen = 0;

static uint8_t sFrame[FRAME_BYTES];
static uint32_t sFrameFill = 0;
static uint32_t sAnimLeft = 0;   // сколько байт кадров ещё ждём
static bool sAnimMode = false;

bool pcRemoteConnected() { return sConnected; }

void pcRemoteSend(const char *s) {
  if (!sConnected) return;
  esp_spp_write(sConnHandle, (int)strlen(s), (uint8_t *)s);
}

static void pcHandleJson(const char *s) {
  cJSON *root = cJSON_Parse(s);
  if (!root) return;
  cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
  if (cJSON_IsString(cmd)) {
    if (strcmp(cmd->valuestring, "oled") == 0) {
      cJSON *t = cJSON_GetObjectItem(root, "text");
      if (cJSON_IsString(t)) {
        snprintf(app.pcMsg, sizeof(app.pcMsg), "%s", t->valuestring);
        int secs = 5;
        cJSON *sc = cJSON_GetObjectItem(root, "secs");
        if (cJSON_IsNumber(sc)) secs = sc->valueint;
        if (secs < 1) secs = 1;
        if (secs > 60) secs = 60;
        app.pcMsgUntil = millis() + (uint32_t)secs * 1000UL;
        app.pcCount++;
        pcRemoteSend("{\"ok\":1}\n");
      }
    } else if (strcmp(cmd->valuestring, "clear") == 0) {
      app.pcMsg[0] = 0;
      app.pcMsgUntil = 0;
      pcRemoteSend("{\"ok\":1}\n");
    } else if (strcmp(cmd->valuestring, "state") == 0) {
      char out[160];
      snprintf(out, sizeof(out),
               "{\"ok\":1,\"ver\":\"%s\",\"up\":\"%s\",\"heap\":%u,\"scan\":%u,\"cls\":%u,\"msg\":%u}\n",
               UBPD_VERSION, uptimeLabel(), (unsigned)(ESP.getFreeHeap() / 1024),
               (unsigned)scanOrderCount, (unsigned)classicCount, (unsigned)app.pcCount);
      pcRemoteSend(out);
    } else if (strcmp(cmd->valuestring, "anim") == 0) {
      cJSON *n = cJSON_GetObjectItem(root, "n");
      int frames = cJSON_IsNumber(n) ? n->valueint : 0;
      if (frames < 1) frames = 1;
      if (frames > 600) frames = 600;      // ~1024*600 = 600 КБ - разумный предел
      sAnimLeft = (uint32_t)frames * FRAME_BYTES;
      sAnimMode = true;
      sFrameFill = 0;
      pcRemoteSend("{\"ok\":1}\n");
    }
  }
  cJSON_Delete(root);
}

static void pcHandleBytes(const uint8_t *d, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    if (sAnimMode) {
      sFrame[sFrameFill++] = d[i];
      if (sFrameFill >= FRAME_BYTES) {
        sFrameFill = 0;
        uint8_t *buf = u8g2.getBufferPtr();
        if (buf) memcpy(buf, sFrame, FRAME_BYTES);
        u8g2.sendBuffer();
        if (sAnimLeft > FRAME_BYTES) sAnimLeft -= FRAME_BYTES;
        else { sAnimLeft = 0; sAnimMode = false; }
      }
    } else {
      char c = (char)d[i];
      if (c == '\n' || c == '\r') {
        if (sLineLen) { sLine[sLineLen] = 0; pcHandleJson(sLine); sLineLen = 0; }
      } else if (sLineLen + 1 < sizeof(sLine)) {
        sLine[sLineLen++] = c;
      }
    }
  }
}

static void pcSppCb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  switch (event) {
    case ESP_SPP_INIT_EVT:
      esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "UBPD");
      break;
    case ESP_SPP_SRV_OPEN_EVT:
      sConnHandle = param->srv_open.handle;
      sConnected = true;
      sLineLen = 0;
      sAnimMode = false;
      sFrameFill = 0;
      Serial.printf("[i] PC REMOTE: ПК подключён (%08X)\n", (unsigned)sConnHandle);
      break;
    case ESP_SPP_DATA_IND_EVT:
      pcHandleBytes(param->data_ind.data, param->data_ind.len);
      break;
    case ESP_SPP_CLOSE_EVT:
      sConnected = false;
      sAnimMode = false;
      sFrameFill = 0;
      sConnHandle = 0xFFFFFFFF;
      Serial.println("[i] PC REMOTE: ПК отключён");
      break;
    default:
      break;
  }
}

void pcRemoteInit() {
  esp_bt_dev_set_device_name("UBPD");
  // Чтобы ПК нашёл прибор в поиске, классика должна быть «видима всем»
  // (сам SPP-сервер делает его только соединяемым, но НЕ обнаруживаемым).
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  if (esp_spp_register_callback(pcSppCb) != ESP_OK) {
    setError("spp cb failed");
    return;
  }
  if (esp_spp_init(ESP_SPP_MODE_CB) != ESP_OK) {
    setError("spp init failed");
  }
}
