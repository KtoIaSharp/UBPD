#include "config.h"

#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ==================== REMOTE: A2DP-плеер (источник) ====================
// UBPD отдаёт звук на СВОИ наушники/колонку по классическому Bluetooth (A2DP
// source). Радио одно: в режиме REMOTE BLE/Wi-Fi спят (см. syncRadios).
//
// Звук стримится прямо с SD: отдельная задача читает файл в кольцевой буфер, а
// A2DP-колбэк забирает оттуда кадры и приводит к 16-бит стерео 44100 (энкодер
// IDF работает только в нём). Так размер трека не ограничен кучей ESP32.
// Если файла нет/не открылся - играем тест-тон 440 Гц (проверка канала A2DP).

static bool sInit = false;
static bool sConnected = false;
static bool sPlaying = false;
static bool sWantPlay = false;    // просили играть, но A2DP-линк ещё не встал
static uint8_t sPeer[6] = {0};

// --- формат исходного PCM ---
static bool sHaveSrc = false;     // открыт трек (стрим) - иначе тон
static uint8_t sChannels = 2;     // 1 или 2
static uint8_t sBytesPerSamp = 2; // 1 (8-бит), 2 (16-бит), 4 (32-бит)
static uint16_t sBlockAlign = 4;  // байт на кадр исходника
static uint32_t sRate = 44100;    // Гц исходника

// --- стриминг WAV с SD через кольцевой буфер ---
#define AUD_RING_BYTES (24 * 1024)
static uint8_t sRing[AUD_RING_BYTES];
static volatile size_t sRingHead = 0;   // читает колбэк
static volatile size_t sRingTail = 0;   // пишет задача
static FILE *sFile = nullptr;
static long sDataStart = 0;
static uint32_t sDataFrames = 1;
static volatile uint32_t sPlayedFrames = 0;
static volatile bool sStreamRun = false;
static TaskHandle_t sStreamTask = nullptr;

// --- приведение частоты/каналов ---
static uint8_t sCurFrame[8];             // текущий кадр исходника
static bool sCurValid = false;
static double sFrac = 0.0;

// --- тон ---
static float sPh = 0.0f;
static const float sPhInc = 2.0f * 3.14159265f * 440.0f / 44100.0f;

static void playerStopStream();   // вперёд: нужен в play/stop/connect

bool playerConnected() { return sConnected; }
bool playerPlaying() { return sPlaying; }

// Колбэк стека A2DP: подключение/аудио.
static void a2dCb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
  switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
      if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        sConnected = true;
        Serial.println("[i] REMOTE: A2DP подключён");
        // Ссылка есть - только теперь можно запускать поток (START до этого
        // подключения отвергается стеком, поэтому запуск был "в пустоту").
        if (sWantPlay) esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
      } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        sConnected = false;
        sPlaying = false;
        sWantPlay = false;
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

// ==================== КОЛЬЦЕВОЙ БУФЕР ====================
static size_t ringUsed() {
  size_t h = sRingHead, t = sRingTail;
  return (t >= h) ? (t - h) : (AUD_RING_BYTES - h + t);
}
static size_t ringFree() { return AUD_RING_BYTES - 1 - ringUsed(); }

// Достаём один кадр исходника (blockAlign байт). false - данных нет.
static bool ringPopFrame() {
  if (ringUsed() < sBlockAlign) return false;
  size_t h = sRingHead;
  for (uint16_t i = 0; i < sBlockAlign; i++) {
    sCurFrame[i] = sRing[h];
    h = (h + 1) % AUD_RING_BYTES;
  }
  __sync_synchronize();
  sRingHead = h;
  sCurValid = true;
  if (sDataFrames) {
    uint32_t pf = sPlayedFrames + 1;
    if (pf >= sDataFrames) pf -= sDataFrames;
    sPlayedFrames = pf;
  }
  return true;
}

// Задача-чтец: тянет файл в кольцо, по концу файла - зацикливает.
static void streamTask(void *arg) {
  (void)arg;
  while (sStreamRun) {
    size_t freeb = ringFree();
    if (freeb < 1024) { vTaskDelay(1); continue; }
    size_t chunk = freeb > 4096 ? 4096 : freeb;
    size_t t = sRingTail;
    size_t first = AUD_RING_BYTES - t;
    if (first > chunk) first = chunk;
    if (first == 0) continue;
    size_t got = fread(sRing + t, 1, first, sFile);
    if (got == 0) {
      fseek(sFile, sDataStart, SEEK_SET);   // конец - повторяем трек
      continue;
    }
    __sync_synchronize();
    sRingTail = (t + got) % AUD_RING_BYTES;
  }
  sStreamTask = nullptr;
  vTaskDelete(nullptr);
}

// Читаем один сэмпл канала chIdx из кадра (8/16/32-бит, LE).
static int16_t wavSample(const uint8_t *frame, int chIdx) {
  const uint8_t *p = frame + (size_t)chIdx * sBytesPerSamp;
  if (sBytesPerSamp == 1) return (int16_t)(((int)p[0] - 128) << 8);        // 8-бит без знака
  if (sBytesPerSamp == 2) return (int16_t)(p[0] | (p[1] << 8));            // 16-бит знаковый
  int32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);  // 32-бит -> старшие 16
  return (int16_t)(v >> 16);
}

// Колбэк данных: стек просит len байт PCM (16-бит стерео 44100). Отдаём поток
// с SD (с приведением формата) или тест-тон.
static int32_t a2dDataCb(uint8_t *buf, int32_t len) {
  int16_t *out = (int16_t *)buf;
  int32_t frames = len / 4;   // 16 бит стерео = 4 байта на кадр

  if (sHaveSrc) {
    const double step = (double)sRate / 44100.0;
    for (int32_t f = 0; f < frames; f++) {
      if (!sCurValid && !ringPopFrame()) {   // старт/недобор - тишина
        out[2 * f] = 0;
        out[2 * f + 1] = 0;
        continue;
      }
      int16_t l = wavSample(sCurFrame, 0);
      int16_t r = sChannels > 1 ? wavSample(sCurFrame, 1) : l;
      out[2 * f] = l;
      out[2 * f + 1] = r;
      sFrac += step;
      while (sFrac >= 1.0) {
        if (!ringPopFrame()) break;           // буфер пуст - держим текущий кадр
        sFrac -= 1.0;
      }
    }
  } else {
    for (int32_t f = 0; f < frames; f++) {
      int16_t v = (int16_t)(sinf(sPh) * 9000.0f);
      out[2 * f] = v;
      out[2 * f + 1] = v;
      sPh += sPhInc;
      if (sPh > 6.2831853f) sPh -= 6.2831853f;
    }
  }
  for (int32_t i = frames * 4; i < len; i++) buf[i] = 0;  // хвост, если len не кратен 4
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

// Запуск/пауза/стоп. START принимается только при поднятом A2DP-линке, поэтому
// если соединения ещё нет - запоминаем намерение и стартуем в колбэке CONNECTED.
void playerPlay() {
  sWantPlay = true;
  if (sConnected) esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
}
void playerPause() {
  sWantPlay = false;
  esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
}
void playerStop() {
  sWantPlay = false;
  esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
  playerStopStream();
}

// Прогресс текущего трека 0..100 (для полосы в плеере). Для тест-тона длины нет -
// рисуем бегущую полосу, пока играет.
uint8_t playerProgress() {
  if (sHaveSrc && sDataFrames) {
    uint32_t p = (uint32_t)((uint64_t)sPlayedFrames * 100u / sDataFrames);
    return p > 100 ? 100 : (uint8_t)p;
  }
  if (sPlaying) return (uint8_t)((millis() / 40u) % 100u);
  return 0;
}

void playerDisconnect() {
  esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
  playerStopStream();
  esp_a2d_source_disconnect(sPeer);
}

// ==================== ОТКРЫТИЕ/ЗАКРЫТИЕ ТРЕКА ====================
// Останавливаем задачу-чтец и закрываем файл (ждём выхода задачи, не рвём FILE).
static void playerStopStream() {
  sStreamRun = false;
  for (int i = 0; i < 300 && sStreamTask; i++) vTaskDelay(1);
  if (sFile) { fclose(sFile); sFile = nullptr; }
  sHaveSrc = false;
  sRingHead = 0;
  sRingTail = 0;
  sCurValid = false;
  sFrac = 0.0;
  sPlayedFrames = 0;
  sDataFrames = 1;
}

// Разбираем WAV (ищем "fmt " и "data" по чанкам) и запускаем стрим.
static bool playerStartStream(const char *path) {
  playerStopStream();

  FILE *f = fopen(path, "rb");
  if (!f) { Serial.printf("[!] REMOTE: не открыть %s\n", path); return false; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 44) { fclose(f); Serial.println("[!] REMOTE: файл слишком мал"); return false; }

  uint8_t riff[12];
  if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    fclose(f);
    Serial.println("[!] REMOTE: не WAV (нет RIFF/WAVE)");
    return false;
  }

  uint16_t fmtTag = 0, ch = 0, bits = 0;
  uint32_t rate = 0;
  bool haveFmt = false;
  long dataStart = 0;
  uint32_t dataLen = 0;
  long pos = 12;
  while (pos + 8 <= sz) {
    uint8_t ch8[8];
    fseek(f, pos, SEEK_SET);
    if (fread(ch8, 1, 8, f) != 8) break;
    uint32_t clen = ch8[4] | (ch8[5] << 8) | (ch8[6] << 16) | ((uint32_t)ch8[7] << 24);
    if (memcmp(ch8, "fmt ", 4) == 0) {
      uint8_t fb[16];
      if (fread(fb, 1, 16, f) != 16) break;
      fmtTag = (uint16_t)(fb[0] | (fb[1] << 8));
      ch     = (uint16_t)(fb[2] | (fb[3] << 8));
      rate   = (uint32_t)fb[4] | ((uint32_t)fb[5] << 8) | ((uint32_t)fb[6] << 16) | ((uint32_t)fb[7] << 24);
      bits   = (uint16_t)(fb[14] | (fb[15] << 8));
      haveFmt = true;
    } else if (memcmp(ch8, "data", 4) == 0) {
      dataStart = pos + 8;
      dataLen = clen;
      break;
    }
    pos += 8 + (long)clen + (long)(clen & 1);
  }

  bool ok = haveFmt && dataStart && dataLen >= 64 && dataStart + (long)dataLen <= sz &&
            (fmtTag == 1 || fmtTag == 0xFFFE) &&
            (bits == 8 || bits == 16 || bits == 32) &&
            ch >= 1 && ch <= 2 && rate >= 4000 && rate <= 192000;
  if (!ok) {
    fclose(f);
    Serial.printf("[!] REMOTE: WAV не поддержан (tag=%u ch=%u rate=%u bits=%u)\n",
                  (unsigned)fmtTag, (unsigned)ch, (unsigned)rate, (unsigned)bits);
    return false;
  }

  sChannels = (uint8_t)ch;
  sBytesPerSamp = (uint8_t)(bits / 8);
  sBlockAlign = (uint16_t)(ch * (bits / 8));
  sRate = rate;
  sDataStart = dataStart;
  sDataFrames = dataLen / sBlockAlign;
  if (!sDataFrames) sDataFrames = 1;
  sPlayedFrames = 0;
  sRingHead = 0;
  sRingTail = 0;
  sCurValid = false;
  sFrac = 0.0;
  sFile = f;
  fseek(f, dataStart, SEEK_SET);
  sHaveSrc = true;
  sStreamRun = true;
  if (xTaskCreatePinnedToCore(streamTask, "wavstr", 6144, nullptr, 5, &sStreamTask, 1) != pdPASS) {
    Serial.println("[!] REMOTE: не создал задачу стрима");
    playerStopStream();
    return false;
  }
  Serial.printf("[i] REMOTE: стрим %s  %uHz %uch %ubit, %u кадр.\n",
                path, (unsigned)sRate, (unsigned)sChannels, (unsigned)bits, (unsigned)sDataFrames);
  return true;
}

void playerUseTone() {
  playerStopStream();
}

// Совместимость: прежнее имя "загрузить WAV" = открыть поток.
bool playerLoadWav(const char *path) {
  return playerStartStream(path);
}

// ==================== ПЛЕЙЛИСТ (WAV из /UBPD/sounds) ====================
#define PLAYER_MAX_TRACKS 32
#define PLAYER_DIR "/sdcard/UBPD/sounds"
static char sTracks[PLAYER_MAX_TRACKS][128];
static uint8_t sTrackCount = 0;
static uint8_t sTrackCur = 0;

uint8_t playerTrackCount() { return sTrackCount; }
const char *playerTrackName(uint8_t i) { return i < sTrackCount ? sTracks[i] : ""; }
uint8_t playerCurrentTrack() { return sTrackCur; }

// Диагностика формата WAV (для отладки "почему молчит/гудит").
static void wavLogInfo(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { Serial.printf("[!] REMOTE: нет доступа %s\n", path); return; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t h[44];
  size_t got = fread(h, 1, sizeof(h), f);
  fclose(f);
  if (got < 44 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
    Serial.printf("[!] REMOTE: %s не WAV (%ld б)\n", path, sz);
    return;
  }
  uint16_t tag = (uint16_t)(h[20] | (h[21] << 8));
  uint16_t ch = (uint16_t)(h[22] | (h[23] << 8));
  uint32_t rate = (uint32_t)h[24] | ((uint32_t)h[25] << 8) | ((uint32_t)h[26] << 16) | ((uint32_t)h[27] << 24);
  uint16_t bits = (uint16_t)(h[34] | (h[35] << 8));
  Serial.printf("[i] REMOTE: %s  %ld б  %uHz %uch %ubit tag=%u  heap=%u\n",
                path + strlen(PLAYER_DIR) + 1, sz, (unsigned)rate, (unsigned)ch,
                (unsigned)bits, (unsigned)tag, (unsigned)(ESP.getFreeHeap()));
}

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
    snprintf(sTracks[sTrackCount], sizeof(sTracks[0]), "%.120s", e->d_name);
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", PLAYER_DIR, sTracks[sTrackCount]);
    wavLogInfo(path);
    sTrackCount++;
  }
  closedir(d);
  Serial.printf("[i] REMOTE: треков найдено: %u\n", (unsigned)sTrackCount);
}

bool playerPlayTrack(uint8_t idx) {
  if (idx >= sTrackCount) return false;
  char path[300];
  snprintf(path, sizeof(path), "%s/%s", PLAYER_DIR, sTracks[idx]);
  if (!playerStartStream(path)) return false;
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
