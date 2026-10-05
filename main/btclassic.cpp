#include "config.h"

// esp_bt.h и esp_gap_bt_api.h уже подключены в config.h - см. пояснение там.

// ==================== CLASSIC BLUETOOTH (BR/EDR) ====================
// Классический поиск (inquiry) - это старый добрый Bluetooth, тот самый,
// что в настройках телефона называется "виден всем устройствам".
//
// Зачем он нужен, если уже есть BLE-сканер:
//  * телефон с включённым Bluetooth по BLE молчит, а в режиме "видим всем"
//    честно отвечает и отдаёт СВОЁ ИМЯ (то самое "ZOV");
//  * в ответе приходит Class of Device - официальная метка класса:
//    телефон, компьютер, наушники, колонка, микрофон, часы, принтер...
//    Здесь это не догадка по вендору, а заявление самого устройства;
//  * старые гарнитуры/колонки/авто по BLE не рекламируются вообще.
//
// Рамки: только публичный ответ на запрос "кто здесь". Никаких подключений,
// спаривания, подмены адреса и прочего - это чужое устройство, и оно
// само решило быть видимым.

static bool classicCbReady = false;

// Адреса, ответившие на текущий поиск, и очередь запросов имени.
#define CLASSIC_INQ_ADDRS 8
static uint8_t inqAddrs[CLASSIC_INQ_ADDRS][6];
static uint8_t inqAddrCount = 0;

static uint8_t nameReqList[CLASSIC_NAME_MAX][6];
static uint8_t nameReqCount = 0;
static uint8_t nameReqPos = 0;
static bool nameReqBusy = false;
static uint32_t nameReqAt = 0;
static bool classicNamePhase = false;

static void inqTrackAddr(const uint8_t *bda) {
  for (uint8_t i = 0; i < inqAddrCount; i++) {
    if (memcmp(inqAddrs[i], bda, 6) == 0) return;
  }
  if (inqAddrCount < CLASSIC_INQ_ADDRS) memcpy(inqAddrs[inqAddrCount++], bda, 6);
}
// ---------- Class of Device -> тип и категория ----------
// CoD - 24 бита: major service (13 бит), major device (5 бит, 8..12),
// minor device (6 бит, 2..7). Нам нужны два последних поля.
static DevType codType(uint32_t cod) {
  if (!cod) return DT_UNKNOWN;
  uint8_t major = (uint8_t)((cod & ESP_BT_COD_MAJOR_DEV_BIT_MASK) >> ESP_BT_COD_MAJOR_DEV_BIT_OFFSET);
  uint8_t minor = (uint8_t)((cod & ESP_BT_COD_MINOR_DEV_BIT_MASK) >> ESP_BT_COD_MINOR_DEV_BIT_OFFSET);
  switch (major) {
    case ESP_BT_COD_MAJOR_DEV_COMPUTER: return DT_COMPUTER;
    case ESP_BT_COD_MAJOR_DEV_PHONE: return DT_PHONE;
    case ESP_BT_COD_MAJOR_DEV_LAN_NAP: return DT_NETWORK;
    case ESP_BT_COD_MAJOR_DEV_AV: return DT_AUDIO;
    case ESP_BT_COD_MAJOR_DEV_PERIPHERAL: return DT_HID;
    case ESP_BT_COD_MAJOR_DEV_IMAGING:
      return (minor & 0x14) ? DT_CAMERA : DT_PRINTER;  // display/camera vs печать
    case ESP_BT_COD_MAJOR_DEV_WEARABLE: return DT_WATCH;
    case ESP_BT_COD_MAJOR_DEV_HEALTH: return DT_WATCH;
    default: return DT_UNKNOWN;
  }
}

// Категория для режима, похожего на CLEAR SCAN: кого человек узнаёт с ходу.
static uint8_t codKind(uint32_t cod, uint8_t *conf) {
  *conf = 0;
  if (!cod) return KIND_NONE;
  uint8_t major = (uint8_t)((cod & ESP_BT_COD_MAJOR_DEV_BIT_MASK) >> ESP_BT_COD_MAJOR_DEV_BIT_OFFSET);
  uint8_t minor = (uint8_t)((cod & ESP_BT_COD_MINOR_DEV_BIT_MASK) >> ESP_BT_COD_MINOR_DEV_BIT_OFFSET);
  if (major == ESP_BT_COD_MAJOR_DEV_PHONE) {
    *conf = 90;
    return KIND_PHONE;
  }
  if (major == ESP_BT_COD_MAJOR_DEV_AV) {
    switch (minor) {
      case 0x01:  // носимый гарнитурный блок
      case 0x02:  // hands-free
      case 0x06:  // наушники
      case 0x07:  // переносное аудио
        *conf = 90;
        return KIND_HEADPHONE;
      case 0x05:  // громкоговоритель
      case 0x0A:  // Hi-Fi аудио
        *conf = 85;
        return KIND_SPEAKER;
      case 0x04:  // микрофон
        *conf = 85;
        return KIND_MIC;
      default:
        break;
    }
  }
  return KIND_NONE;
}

void classicLabel(const ClassicDevice *d, char *out, size_t maxChars) {
  if (d->name[0]) {
    snprintf(out, maxChars + 1, "%.*s", (int)maxChars, d->name);
    return;
  }
  snprintf(out, maxChars + 1, "%02X:%02X", d->addr[4], d->addr[5]);
}

// ---------- Кэш найденных по классике ----------
static void classicUpsert(const uint8_t *bda, const char *name, uint32_t cod,
                          int8_t rssi, bool haveRssi) {
  uint32_t now = millis();
  int16_t slot = -1;
  bool fresh = false;

  portENTER_CRITICAL(&clsMux);
  for (int i = 0; i < MAX_CLASSIC; i++) {
    if (classics[i].used && memcmp(classics[i].addr, bda, 6) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    for (int i = 0; i < MAX_CLASSIC; i++) {
      if (!classics[i].used) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) {  // мест нет: забираем самый давний
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < MAX_CLASSIC; i++) {
      if (classics[i].lastSeen <= oldest) {
        oldest = classics[i].lastSeen;
        slot = i;
      }
    }
  } else {
    fresh = !classics[slot].used;
  }

  if (fresh) {
    memset(&classics[slot], 0, sizeof(ClassicDevice));
    memcpy(classics[slot].addr, bda, 6);
    classics[slot].used = true;
    classics[slot].firstSeen = now;
    classics[slot].rssi = -128;  // пока неизвестно
  }

#if CLASSIC_DEBUG
  bool hadName = classics[slot].name[0] != 0;
#endif
  if (name && name[0]) {
    size_t k = 0;
    for (size_t i = 0; name[i] && k + 1 < sizeof(classics[slot].name); i++) {
      uint8_t c = (uint8_t)name[i];
      if (c >= 0x20) classics[slot].name[k++] = (char)c;
    }
    classics[slot].name[k] = 0;
  }
  if (cod) {
    classics[slot].cod = cod;
    classics[slot].major = (uint8_t)((cod & ESP_BT_COD_MAJOR_DEV_BIT_MASK) >> ESP_BT_COD_MAJOR_DEV_BIT_OFFSET);
    classics[slot].minor = (uint8_t)((cod & ESP_BT_COD_MINOR_DEV_BIT_MASK) >> ESP_BT_COD_MINOR_DEV_BIT_OFFSET);
    classics[slot].type = codType(cod);
    uint8_t kc = 0;
    classics[slot].kind = codKind(cod, &kc);
    classics[slot].kindConf = kc;
  }
  if (haveRssi) classics[slot].rssi = rssi;
  classics[slot].lastSeen = now;
  portEXIT_CRITICAL(&clsMux);

#if CLASSIC_DEBUG
  if (fresh || (classics[slot].name[0] && !hadName)) {
    Serial.printf("[cls] %02X:%02X:%02X:%02X:%02X:%02X rssi=%d cod=0x%06X \"%s\"%s\n",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5],
                  (int)classics[slot].rssi, (unsigned)cod,
                  classics[slot].name[0] ? classics[slot].name : "",
                  fresh ? " [новое]" : " [имя пришло позже]");
  }
#endif
}

// ---------- Запрос имени ----------
// Имя в эфире по классике приходит не всегда, поэтому после поиска
// добираем его отдельным запросом у найденных устройств.
static void classicSetName(const uint8_t *bda, const char *name) {
  portENTER_CRITICAL(&clsMux);
  for (int i = 0; i < MAX_CLASSIC; i++) {
    if (!classics[i].used || memcmp(classics[i].addr, bda, 6) != 0) continue;
    size_t k = 0;
    for (size_t j = 0; name[j] && k + 1 < sizeof(classics[i].name); j++) {
      uint8_t c = (uint8_t)name[j];
      if (c >= 0x20) classics[i].name[k++] = (char)c;
    }
    classics[i].name[k] = 0;
    break;
  }
  portEXIT_CRITICAL(&clsMux);
#if CLASSIC_DEBUG
  Serial.printf("[cls] имя от %02X:%02X:%02X:%02X:%02X:%02X = \"%s\"\n",
                bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], name);
#endif
}

static void classicNamesPrepare() {
  nameReqCount = 0;
  nameReqPos = 0;
  nameReqBusy = false;
  classicNamePhase = false;
#if CLASSIC_NAME_REQ
  for (uint8_t i = 0; i < inqAddrCount && nameReqCount < CLASSIC_NAME_MAX; i++) {
    bool need = false;
    portENTER_CRITICAL(&clsMux);
    for (int j = 0; j < MAX_CLASSIC; j++) {
      if (classics[j].used && memcmp(classics[j].addr, inqAddrs[i], 6) == 0) {
        need = (classics[j].name[0] == 0);
        break;
      }
    }
    portEXIT_CRITICAL(&clsMux);
    if (need) memcpy(nameReqList[nameReqCount++], inqAddrs[i], 6);
  }
  if (nameReqCount) {
    classicNamePhase = true;
    Serial.printf("[i] CLASSIC: спрашиваю имя у %u устройств\n", (unsigned)nameReqCount);
  }
#endif
}

static void classicNamesTick() {
  if (!classicNamePhase) return;
  if (!classicWant) {
    classicNamePhase = false;
    return;
  }
  if (nameReqBusy) {
    if (millis() - nameReqAt > CLASSIC_NAME_TIMEOUT) {  // ответа нет - не ждём вечно
      nameReqBusy = false;
      nameReqPos++;
    } else {
      return;
    }
  }
  if (nameReqPos >= nameReqCount) {
    classicNamePhase = false;
    return;
  }
  nameReqBusy = true;
  nameReqAt = millis();
  if (esp_bt_gap_read_remote_name(nameReqList[nameReqPos]) != ESP_OK) {
    nameReqBusy = false;
    nameReqPos++;
  }
}

// ---------- Ответ стека ----------
static void classicGapCb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
      uint8_t bda[6];
      memcpy(bda, param->disc_res.bda, 6);
      char nm[CLASSIC_NAME_LEN];
      nm[0] = 0;
      uint32_t cod = 0;
      int8_t rssi = 0;
      bool haveRssi = false;
      for (int i = 0; i < param->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
        if (!p->val) continue;
        if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->len > 0) {
          snprintf(nm, sizeof(nm), "%.*s", p->len, (const char *)p->val);
        } else if (p->type == ESP_BT_GAP_DEV_PROP_COD) {
          cod = *(uint32_t *)p->val;
        } else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI) {
          rssi = *(int8_t *)p->val;
          haveRssi = true;
        }
      }
      classicUpsert(bda, nm, cod, rssi, haveRssi);
      inqTrackAddr(bda);
      break;
    }
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT: {
      if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS &&
          param->read_rmt_name.rmt_name[0]) {
        classicSetName(param->read_rmt_name.bda, (const char *)param->read_rmt_name.rmt_name);
      }
      nameReqBusy = false;
      if (classicNamePhase) nameReqPos++;
      break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        classicScanning = false;
        if (classicWant && !classicNamePhase) classicNamesPrepare();
      }
      break;
    default:
      break;
  }
}

// ---------- Запуск/остановка поиска ----------
// Поиск сам заканчивается через CLASSIC_INQ_UNITS * 1.28 c, поэтому его
// перезапускает classicTick, пока вкладка открыта.
void classicScanStart() {
  bool wasWanted = classicWant;
  classicWant = true;
  if (classicScanning) return;
  if (millis() - classicLastStart < CLASSIC_RESTART_MS) return;
  classicLastStart = millis();

  if (!classicCbReady) {
    if (esp_bt_gap_register_callback(classicGapCb) != ESP_OK) {
      setError("bt_gap cb failed");
      return;
    }
    classicCbReady = true;
  }
  esp_err_t e = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, CLASSIC_INQ_UNITS, 0);
  if (e == ESP_OK) {
    classicScanning = true;
    inqAddrCount = 0;
    // поиск перезапускается сам каждые ~10 c - пишем в Serial только при входе
    if (!wasWanted) Serial.println("[i] CLASSIC: поиск (inquiry) запущен");
  } else {
    setError("bt inquiry failed");
  }
}

void classicScanStop() {
  classicWant = false;
  classicNamePhase = false;
  nameReqBusy = false;
  if (!classicScanning) return;
  esp_bt_gap_cancel_discovery();
  classicScanning = false;
  Serial.println("[i] CLASSIC: поиск остановлен");
}

void classicTick() {
  if (classicNamePhase) {  // сначала добираем имена, потом новый поиск
    classicNamesTick();
    return;
  }
  if (classicWant && !classicScanning) classicScanStart();
}

// ---------- Списки ----------
void forgetStaleClassic() {
  uint32_t now = millis();
  portENTER_CRITICAL(&clsMux);
  for (int i = 0; i < MAX_CLASSIC; i++) {
    if (classics[i].used && now - classics[i].lastSeen > FORGET_MS) classics[i].used = false;
  }
  portEXIT_CRITICAL(&clsMux);
}

void buildClassicList() {
  uint32_t now = millis();
  int16_t idx[MAX_CLASSIC];
  int8_t rssi[MAX_CLASSIC];
  uint8_t n = 0;

  portENTER_CRITICAL(&clsMux);
  for (int i = 0; i < MAX_CLASSIC; i++) {
    if (!classics[i].used) continue;
    if (now - classics[i].lastSeen > STALE_MS) continue;
    idx[n] = (int16_t)i;
    rssi[n] = classics[i].rssi;
    n++;
  }
  portEXIT_CRITICAL(&clsMux);

  for (uint8_t i = 1; i < n; i++) {  // сильные выше, без силы сигнала - в конце
    int16_t ki = idx[i];
    int8_t kr = rssi[i];
    int j = (int)i - 1;
    while (j >= 0 && rssi[j] < kr) {
      idx[j + 1] = idx[j];
      rssi[j + 1] = rssi[j];
      j--;
    }
    idx[j + 1] = ki;
    rssi[j + 1] = kr;
  }
  for (uint8_t i = 0; i < n; i++) classicOrder[i] = idx[i];
  classicCount = n;

  static uint8_t lastLogged = 255;
  if (n != lastLogged) {
    lastLogged = n;
    Serial.printf("[i] CLASSIC устройств: %u\n", (unsigned)n);
  }
}

int16_t classicOrderIndexOf(const uint8_t *addr) {
  for (uint8_t i = 0; i < classicCount; i++) {
    if (memcmp(classics[classicOrder[i]].addr, addr, 6) == 0) return (int16_t)i;
  }
  return -1;
}

int16_t classicLookupByAddr(const uint8_t *addr) {
  for (int i = 0; i < MAX_CLASSIC; i++) {
    if (classics[i].used && memcmp(classics[i].addr, addr, 6) == 0) return (int16_t)i;
  }
  return -1;
}

// ---------- Слежение за своим устройством ----------
void toggleFavoriteClassic(int16_t clsIdx) {
  if (clsIdx < 0 || clsIdx >= MAX_CLASSIC || !classics[clsIdx].used) return;
  int16_t f = findFavIndex(classics[clsIdx].addr);
  if (f >= 0) {
    favorites[f].used = false;
    storageSaveFavorites();
    toast("UNWATCHED", 1200);
    return;
  }
  for (int i = 0; i < MAX_FAVS; i++) {
    if (favorites[i].used) continue;
    favorites[i].used = true;
    memcpy(favorites[i].addr, classics[clsIdx].addr, 6);
    char nm[CLASSIC_NAME_LEN];
    classicLabel(&classics[clsIdx], nm, DEV_NAME_LEN - 1);
    snprintf(favorites[i].name, sizeof(favorites[i].name), "%s", nm);
    storageSaveFavorites();
    toast("* WATCHING", 1200);
    return;
  }
  toast("WATCH LIST FULL", 1500);
}
