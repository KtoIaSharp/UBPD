#include "config.h"

#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <dirent.h>

// ==================== REMOTE: A2DP-плеер (источник) ====================
// UBPD отдаёт звук на СВОИ наушники/колонку по классическому Bluetooth (A2DP
// source). Радио одно: в режиме REMOTE BLE/Wi-Fi спят (см. syncRadios).
//
// Источник звука: WAV (16-бит PCM, моно/стерео) из файла (SD/флеш) ЛИБО, если
// файла нет, тестовый тон 440 Гц - чтобы проверить сам канал A2DP.
//
// По умолчанию кадр PCM = 44100 Гц, 16 бит. Старые наушники ждут стерео.

static bool sInit = false;
static bool sConnected = false;
static bool sPlaying = false;
static uint8_t sPeer[6] = {0};

// --- WAV ---
static uint8_t *sWav = nullptr;   // буфер с PCM (после парса WAV)
static size_t sWavLen = 0;
static size_t sWavPos = 0;
static bool sHaveWav = false;

// --- тон ---
static float sPh = 0.0f;
static const float sPhInc = 2.0f * 3.14159265f * 440.0f / 44100.0f;

bool playerConnected() { return sConnected; }
bool playerPlaying() { return sPlaying; }

// Колбэк стека A2DP: подключение/аудио.
static void a2dCb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
  switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
      if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        sConnected = true;
        Serial.println("[i] REMOTE: A2DP подключён");
      } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        sConnected = false;
        sPlaying = false;
        Serial.println("[i] REMOTE: A2DP отключён");
      }
      break;
    case ESP_A2D_AUDIO_STATE_EVT:
      sPlaying = (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED);
      break;
    default:
      break;
  }
}

// Колбэк данных: стек просит len байт PCM. Отдаём WAV (по кругу) или тон.
static int32_t a2dDataCb(uint8_t *buf, int32_t len) {
  if (sHaveWav && sWav && sWavLen) {
    for (int32_t i = 0; i < len; i++) {
      buf[i] = sWav[sWavPos];
      if (++sWavPos >= sWavLen) sWavPos = 0;  // зацикливаем
    }
  } else {
    int16_t *p = (int16_t *)buf;
    int frames = len / 4;   // 16 бит стерео = 4 байта на кадр
    for (int i = 0; i < frames; i++) {
      int16_t v = (int16_t)(sinf(sPh) * 9000.0f);
      p[2 * i] = v;
      p[2 * i + 1] = v;
      sPh += sPhInc;
      if (sPh > 6.2831853f) sPh -= 6.2831853f;
    }
  }
  return len;
}

void playerInit() {
  if (sInit) return;
  esp_a2d_register_callback(&a2dCb);
  esp_a2d_source_register_data_callback(&a2dDataCb);
  if (esp_a2d_source_init() != ESP_OK) {
    setError("a2d source init failed");
    return;
  }
  sInit = true;
  Serial.println("[i] REMOTE: A2DP-источник готов");
}

// Подключиться к устройству по MAC (обычно наушники из CLASSIC-скана).
bool playerConnect(const uint8_t *bda) {
  playerInit();
  avrcInit();
  memcpy(sPeer, bda, 6);
  esp_err_t e = esp_a2d_source_connect((uint8_t *)bda);
  if (e != ESP_OK) setError("a2d connect failed");
  return e == ESP_OK;
}

void playerPlay()  { esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START); }
void playerStop()  { esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP); }

void playerDisconnect() {
  esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
  esp_a2d_source_disconnect(sPeer);
}

// Загрузка WAV: берём PCM-данные из чанка "data" (без декодера - WAV и так PCM).
bool playerLoadWav(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 64) { fclose(f); return false; }
  uint8_t *file = (uint8_t *)malloc((size_t)sz);
  if (!file) { fclose(f); return false; }
  fread(file, 1, (size_t)sz, f);
  fclose(f);

  // Простой парс: ищем "data" и берём всё после него.
  size_t off = 12;  // RIFF....WAVE
  size_t dataOff = 0, dataLen = 0;
  while (off + 8 <= (size_t)sz) {
    uint32_t clen = file[off + 4] | (file[off + 5] << 8) | (file[off + 6] << 16) | ((uint32_t)file[off + 7] << 24);
    if (memcmp(&file[off], "data", 4) == 0) { dataOff = off + 8; dataLen = clen; break; }
    off += 8 + clen + (clen & 1);
  }
  if (!dataOff || dataLen < 64 || dataOff + dataLen > (size_t)sz) { free(file); return false; }

  if (sWav) free(sWav);
  sWav = (uint8_t *)malloc(dataLen);
  if (!sWav) { free(file); return false; }
  memcpy(sWav, file + dataOff, dataLen);
  sWavLen = dataLen;
  sWavPos = 0;
  sHaveWav = true;
  free(file);
  Serial.printf("[i] REMOTE: WAV загружен, %u байт PCM\n", (unsigned)dataLen);
  return true;
}

void playerUseTone() {
  if (sWav) { free(sWav); sWav = nullptr; }
  sWavLen = 0;
  sWavPos = 0;
  sHaveWav = false;
}

// ==================== ПЛЕЙЛИСТ (WAV из /UBPD/sounds) ====================
#define PLAYER_MAX_TRACKS 32
#define PLAYER_DIR "/sdcard/UBPD/sounds"
static char sTracks[PLAYER_MAX_TRACKS][48];
static uint8_t sTrackCount = 0;
static uint8_t sTrackCur = 0;

uint8_t playerTrackCount() { return sTrackCount; }
const char *playerTrackName(uint8_t i) { return i < sTrackCount ? sTracks[i] : ""; }
uint8_t playerCurrentTrack() { return sTrackCur; }

// Собираем список *.wav из /UBPD/sounds (если SD смонтирована).
void playerScanTracks() {
  sTrackCount = 0;
  DIR *d = opendir(PLAYER_DIR);
  if (!d) {
    Serial.println("[i] REMOTE: /UBPD/sounds недоступен (нет SD?)");
    return;
  }
  struct dirent *e;
  while ((e = readdir(d)) != nullptr && sTrackCount < PLAYER_MAX_TRACKS) {
    size_t n = strlen(e->d_name);
    if (n < 5) continue;
    if (strcasecmp(e->d_name + n - 4, ".wav") != 0) continue;
    snprintf(sTracks[sTrackCount], sizeof(sTracks[0]), "%s", e->d_name);
    sTrackCount++;
  }
  closedir(d);
  Serial.printf("[i] REMOTE: треков найдено: %u\n", (unsigned)sTrackCount);
}

bool playerPlayTrack(uint8_t idx) {
  if (idx >= sTrackCount) return false;
  char path[96];
  snprintf(path, sizeof(path), "%s/%s", PLAYER_DIR, sTracks[idx]);
  if (!playerLoadWav(path)) return false;
  sTrackCur = idx;
  playerPlay();
  return true;
}

void playerPlayRandom() {
  if (sTrackCount) playerPlayTrack((uint8_t)(esp_random() % sTrackCount));
}

void playerNextTrack() {
  if (sTrackCount) playerPlayTrack((uint8_t)((sTrackCur + 1) % sTrackCount));
}

// ==================== AVRCP (управление по классике) ====================
// AVRCP-контроллер (CT): посылаем play/pause/next/prev/громкость подключённому
// устройству. Инициализируется один раз; AVCTP поднимается на том же ACL, что
// и A2DP.
static bool sAvrcInit = false;

static void avrcCb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param) {
  if (event == ESP_AVRC_CT_CONNECTION_STATE_EVT) {
    Serial.printf("[i] REMOTE: AVRCP %s\n", param->conn_stat.connected ? "подключён" : "отключён");
  }
}

void avrcInit() {
  if (sAvrcInit) return;
  esp_avrc_ct_register_callback(avrcCb);
  if (esp_avrc_ct_init() != ESP_OK) {
    setError("avrc init failed");
    return;
  }
  sAvrcInit = true;
}

static void avrcPt(uint8_t key) {
  esp_avrc_ct_send_passthrough_cmd(0, key, ESP_AVRC_PT_CMD_STATE_PRESSED);
  esp_avrc_ct_send_passthrough_cmd(0, key, ESP_AVRC_PT_CMD_STATE_RELEASED);
}

void avrcPlayPause() {
  static bool playing = false;
  playing = !playing;
  avrcPt(playing ? ESP_AVRC_PT_CMD_PLAY : ESP_AVRC_PT_CMD_PAUSE);
}
void avrcNext()    { avrcPt(ESP_AVRC_PT_CMD_FORWARD); }
void avrcPrev()    { avrcPt(ESP_AVRC_PT_CMD_BACKWARD); }
void avrcVolUp()   { avrcPt(ESP_AVRC_PT_CMD_VOL_UP); }
void avrcVolDown() { avrcPt(ESP_AVRC_PT_CMD_VOL_DOWN); }
