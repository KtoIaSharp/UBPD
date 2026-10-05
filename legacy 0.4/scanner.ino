#include "config.h"
#include "companies.h"

// ==================== ИНФЕРЕНЦИЯ ТИПА УСТРОЙСТВА ====================
// ==================== КАТЕГОРИЯ ДЛЯ CLEAR SCAN ====================
static bool svcEquals(BLEAdvertisedDevice &dev, uint16_t id);
// Задача режима: оставить в списке только то, что человек узнаёт с ходу -
// телефоны, наушники/гарнитуры, колонки и микрофоны. Всё остальное (маячки,
// трекеры, часы, датчики) не показываем.
//
// Порядок признаков: appearance и профили (это заявление самого устройства) ->
// имя в рекламе -> производитель (уже как догадка, помечается "?").

// Производитель -> категория. Не зависит от общего типа устройства: у телефонов
// и наушников он в рекламе часто не выставлен, а вендор говорит сам за себя.
static uint8_t kindFromVendor(uint16_t cid, uint8_t *conf) {
  switch (cid) {
    // телефоны (и то, что похоже на телефон без имени)
    case 76: case 117: case 224: case 398: case 637: case 911: case 1946:
    case 1839: case 2103: case 939: case 8: case 1: case 196: case 301:
    case 749: case 709: case 1855:
      *conf = 40;
      return KIND_PHONE;

    // наушники / гарнитуры / слуховые аппараты
    case 1172: case 1993: case 204: case 1560: case 1338:
      *conf = 45;
      return KIND_HEADPHONE;
    case 103: case 137: case 263: case 642: case 661: case 186: case 480: case 1607:
      *conf = 30;
      return KIND_HEADPHONE;

    // колонки
    case 87: case 158: case 1447: case 2042: case 1626: case 259:
    case 335: case 600: case 843: case 2016: case 217:
      *conf = 45;
      return KIND_SPEAKER;

    // микрофоны
    case 1197:
      *conf = 45;
      return KIND_MIC;

    default:
      break;
  }
  return KIND_NONE;
}

static uint8_t inferKind(BLEAdvertisedDevice &dev, DevType type, uint16_t cid,
                         const char *low, uint8_t *conf) {
  (void)type;
  uint8_t kind = KIND_NONE;
  uint8_t c = 0;

  // 1) appearance (заявление самого устройства)
  if (dev.haveAppearance()) {
    uint16_t a = dev.getAppearance();
    if (a >= 0x0040 && a <= 0x007F) { kind = KIND_PHONE; c = 75; }
    else if (a >= 0x0880 && a <= 0x08BF) { kind = KIND_HEADPHONE; c = 80; }   // wearable audio
    else if (a >= 0x08C0 && a <= 0x08FF) { kind = KIND_HEADPHONE; c = 70; }   // слуховой аппарат
    else if (a >= 0x0800 && a <= 0x083F) { kind = KIND_SPEAKER; c = 70; }     // audio sink
    else if (a >= 0x0840 && a <= 0x087F) { kind = KIND_MIC; c = 60; }         // audio source
  }

  // 2) classic-аудио профили
  if (kind == KIND_NONE) {
    if (svcEquals(dev, 0x1108) || svcEquals(dev, 0x111E) || svcEquals(dev, 0x1112)) { kind = KIND_HEADPHONE; c = 70; }
    else if (svcEquals(dev, 0x110B)) { kind = KIND_SPEAKER; c = 65; }
    else if (svcEquals(dev, 0x110A) || svcEquals(dev, 0x110C)) { kind = KIND_MIC; c = 55; }
  }

  // 3) имя (самый понятный признак для человека)
  if (strstr(low, "airpod") || strstr(low, "buds") || strstr(low, "earbud") ||
      strstr(low, "headphone") || strstr(low, "headset") || strstr(low, "wh-10") ||
      strstr(low, "wf-10") || strstr(low, "beats") || strstr(low, "freebuds") ||
      strstr(low, "momentum") || strstr(low, "liberty") || strstr(low, "ellipse") ||
      strstr(low, "soundcore") || strstr(low, "haylou") || strstr(low, "xm4") ||
      strstr(low, "xm5") || strstr(low, "quantum")) { kind = KIND_HEADPHONE; c = 85; }

  if (strstr(low, "speaker") || strstr(low, "jbl") || strstr(low, "flip") ||
      strstr(low, "boombox") || strstr(low, "partybox") || strstr(low, "soundlink") ||
      strstr(low, "sonos") || strstr(low, "homepod") || strstr(low, "wonderboom") ||
      strstr(low, "megaboom") || strstr(low, "emberton") || strstr(low, "stanmore") ||
      strstr(low, "willen") || strstr(low, "srs-") || strstr(low, "xb-") ||
      strstr(low, "motion+") || strstr(low, "tronsmart")) { kind = KIND_SPEAKER; c = 85; }

  if (strstr(low, "microphone") || strstr(low, " mic") || strstr(low, "(mic") ||
      strstr(low, "mic-") || strstr(low, "mikrofon") || strstr(low, "rode") ||
      strstr(low, "wireless go") || strstr(low, "boya") || strstr(low, "lavalier") ||
      strstr(low, "shure")) { kind = KIND_MIC; c = 85; }

  if (strstr(low, "iphone") || strstr(low, "galaxy") || strstr(low, "pixel") ||
      strstr(low, "redmi") || strstr(low, "oneplus") || strstr(low, "realme") ||
      strstr(low, "honor") || strstr(low, "meizu") || strstr(low, "zenfone") ||
      strstr(low, "moto ") || strstr(low, "sm-") || strstr(low, "find x") ||
      strstr(low, "phone")) { kind = KIND_PHONE; c = 85; }

  // 4) производитель - только как догадка
  if (kind == KIND_NONE) {
    uint8_t vc = 0;
    uint8_t vk = kindFromVendor(cid, &vc);
    if (vk != KIND_NONE) { kind = vk; c = vc; }
  }

  *conf = c;
  return kind;
}

const char *kindTag(uint8_t kind, uint8_t conf, char *buf, size_t bufLen) {
  const char *tag = "";
  switch (kind) {
    case KIND_PHONE: tag = "PH"; break;
    case KIND_HEADPHONE: tag = "HP"; break;
    case KIND_SPEAKER: tag = "SP"; break;
    case KIND_MIC: tag = "MIC"; break;
    default: tag = ""; break;
  }
  snprintf(buf, bufLen, "%s%s", tag, (conf < 60) ? "?" : "");
  return buf;
}

static bool svcEquals(BLEAdvertisedDevice &dev, uint16_t id) {
  BLEUUID want(id);
  for (int i = 0; i < dev.getServiceUUIDCount(); i++) {
    if (dev.getServiceUUID(i).equals(want)) return true;
  }
  return false;
}

static void lowerCopy(const char *src, char *dst, size_t dstLen) {
  size_t i = 0;
  for (; src[i] && i + 1 < dstLen; i++) dst[i] = (char)tolower((unsigned char)src[i]);
  dst[i] = 0;
}

static DevType inferDeviceType(BLEAdvertisedDevice &dev, uint8_t *conf) {
  DevType best = DT_UNKNOWN;
  uint8_t bestConf = 15;

  auto bump = [&](DevType t, uint8_t c) {
    if (c > bestConf) {
      best = t;
      bestConf = c;
    }
  };

  // iBeacon: Apple company id + тип 0x02 0x15
  if (dev.haveManufacturerData()) {
    auto mfg = dev.getManufacturerData();
    if (mfg.size() >= 4 && (uint8_t)mfg[0] == 0x4C && (uint8_t)mfg[1] == 0x00 &&
        (uint8_t)mfg[2] == 0x02 && (uint8_t)mfg[3] == 0x15) {
      *conf = 95;
      return DT_BEACON;
    }
  }

  // Сервисы по SIG-таблицам
  if (svcEquals(dev, 0x1812) || svcEquals(dev, 0x1815)) bump(DT_HID, 88);
  if (svcEquals(dev, 0x180D) || svcEquals(dev, 0x1809) || svcEquals(dev, 0x181D)) bump(DT_WATCH, 68);
  if (svcEquals(dev, 0x1108) || svcEquals(dev, 0x110A) || svcEquals(dev, 0x110B) || svcEquals(dev, 0x111E)) bump(DT_AUDIO, 82);
  if (svcEquals(dev, 0x180F)) bump(DT_UNKNOWN, 25);
  if (svcEquals(dev, 0x181A) || svcEquals(dev, 0x181C)) bump(DT_UNKNOWN, 25);

  // Appearance (profile-категории Bluetooth SIG)
  if (dev.haveAppearance()) {
    uint16_t a = dev.getAppearance();
    if (a >= 0x0040 && a <= 0x007F) bump(DT_PHONE, 60);
    else if (a >= 0x0080 && a <= 0x00BF) bump(DT_COMPUTER, 60);
    else if (a >= 0x00C0 && a <= 0x00DF) bump(DT_WATCH, 65);
    else if (a >= 0x03C0 && a <= 0x03CF) bump(DT_HID, 75);
    else if (a >= 0x0800 && a <= 0x08FF) bump(DT_AUDIO, 70);
    else if (a >= 0x0900 && a <= 0x093F) bump(DT_WATCH, 55);
  }

  // Имя в рекламе
  char nm[64] = {0};
  if (dev.haveName()) snprintf(nm, sizeof(nm), "%s", dev.getName().c_str());
  char low[64];
  lowerCopy(nm, low, sizeof(low));

  if (strstr(low, "keyboard") || strstr(low, "kbd") || strstr(low, "mouse") ||
      strstr(low, "keychron") || strstr(low, "logitech") || strstr(low, "gamepad") ||
      strstr(low, "controller") || strstr(low, "hid")) bump(DT_HID, 80);

  if (strstr(low, "airpods") || strstr(low, "buds") || strstr(low, "earbuds") ||
      strstr(low, "headphone") || strstr(low, "headset") || strstr(low, "jbl") ||
      strstr(low, "flip") || strstr(low, "soundcore") || strstr(low, "bose") ||
      strstr(low, "jabra") || strstr(low, "speaker") || strstr(low, "audio") ||
      strstr(low, "music") || strstr(low, "marshall") || strstr(low, "beats") ||
      strstr(low, "wh-") || strstr(low, "wf-") || strstr(low, "freebuds")) bump(DT_AUDIO, 78);

  if (strstr(low, "watch") || strstr(low, "band") || strstr(low, "amazfit") ||
      strstr(low, "garmin") || strstr(low, "fitbit") || strstr(low, "mi band")) bump(DT_WATCH, 72);

  if (strstr(low, "airtag") || strstr(low, "smarttag") || strstr(low, "tile") ||
      strstr(low, "tracker") || strstr(low, "tag ")) bump(DT_TRACKER, 70);

  if (strstr(low, "iphone") || strstr(low, "galaxy") || strstr(low, "pixel") ||
      strstr(low, "redmi") || strstr(low, "phone") || strstr(low, "oneplus") ||
      strstr(low, "realme") || strstr(low, "honor")) bump(DT_PHONE, 62);

  if (strstr(low, "macbook") || strstr(low, "laptop") || strstr(low, "thinkpad") ||
      strstr(low, "imac") || strstr(low, "desktop") || strstr(low, "ubuntu")) bump(DT_COMPUTER, 62);

  if (strstr(low, "printer") || strstr(low, "hp ")) bump(DT_PRINTER, 60);
  if (strstr(low, "cam") || strstr(low, "ipcam")) bump(DT_CAMERA, 55);
  if (strstr(low, "router") || strstr(low, "tp-link") || strstr(low, "miwifi") ||
      strstr(low, "repeater")) bump(DT_NETWORK, 50);

  // Производитель по Company ID
  if (dev.haveManufacturerData()) {
    auto mfg = dev.getManufacturerData();
    if (mfg.size() >= 2) {
      uint16_t cid = (uint8_t)mfg[0] | ((uint16_t)(uint8_t)mfg[1] << 8);
      switch (cid) {
        case 0x004C: bump(DT_PHONE, 35); break;      // Apple
        case 0x0006: bump(DT_COMPUTER, 40); break;   // Microsoft
        case 0x00E0: bump(DT_PHONE, 35); break;      // Google
        case 0x0075: bump(DT_PHONE, 35); break;      // Samsung
        case 0x0087: bump(DT_WATCH, 50); break;      // Garmin
        case 0x0157: bump(DT_AUDIO, 45); break;      // Anker / Soundcore
        case 0x0171: bump(DT_COMPUTER, 35); break;   // Amazon
        default: break;
      }
    }
  }

  *conf = bestConf;
  return best;
}

static bool iBeaconUuid(BLEAdvertisedDevice &dev, char *out, size_t outLen) {
  if (!dev.haveManufacturerData()) return false;
  auto mfg = dev.getManufacturerData();
  if (mfg.size() < 25 || (uint8_t)mfg[0] != 0x4C || (uint8_t)mfg[1] != 0x00 ||
      (uint8_t)mfg[2] != 0x02 || (uint8_t)mfg[3] != 0x15) return false;
  snprintf(out, outLen, "%02X%02X%02X%02X-%02X%02X-%02X%02X",
           (uint8_t)mfg[4], (uint8_t)mfg[5], (uint8_t)mfg[6], (uint8_t)mfg[7],
           (uint8_t)mfg[8], (uint8_t)mfg[9], (uint8_t)mfg[10], (uint8_t)mfg[11]);
  return true;
}

static void upperCopy(char *s) {
  for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void uuidShort(BLEUUID u, char *out, size_t outLen) {
  char full[40];
  snprintf(full, sizeof(full), "%s", u.toString().c_str());
  if (strncmp(full, "0000", 4) == 0) snprintf(out, outLen, "%.4s", full + 4);
  else snprintf(out, outLen, "%.8s", full);
  upperCopy(out);
}

static void buildSvcString(BLEAdvertisedDevice &dev, char *out, size_t outLen) {
  out[0] = 0;
  size_t used = 0;
  int cnt = dev.getServiceUUIDCount();
  for (int i = 0; i < cnt && i < 4; i++) {
    char code[10];
    uuidShort(dev.getServiceUUID(i), code, sizeof(code));
    int n = snprintf(out + used, outLen - used, "%s%s", used ? " " : "", code);
    if (n < 0 || (size_t)n >= outLen - used) break;
    used += (size_t)n;
  }
}

// ==================== КЭШ УСТРОЙСТВ ====================

// Полная таблица производителей лежит в companies.h (сгенерирована из Bluetooth SIG).
// Нужна потому, что многие устройства не кладут имя в рекламу вообще.
const char *companyShort(uint16_t cid) {
  if (cid == 0) return "";
  return companyNameLookup(cid);
}

static void advUpsert(BLEAdvertisedDevice &dev, DevType type, uint8_t confidence) {
  uint8_t addr[6];
  memcpy(addr, dev.getAddress().getNative(), 6);

  char raw[64] = {0};
  if (dev.haveName()) snprintf(raw, sizeof(raw), "%s", dev.getName().c_str());

  char nm[DEV_NAME_LEN] = {0};
  size_t k = 0;
  for (size_t i = 0; raw[i] && k + 1 < sizeof(nm); i++) {
    uint8_t c = (uint8_t)raw[i];
    if (c >= 0x20) nm[k++] = (char)c;
  }
  nm[k] = 0;
  char svc[DEV_SVC_LEN] = {0};
  buildSvcString(dev, svc, sizeof(svc));
  char ib[24] = {0};
  bool isIbeacon = iBeaconUuid(dev, ib, sizeof(ib));

  uint16_t cid = 0;
  if (dev.haveManufacturerData()) {
    auto mfg = dev.getManufacturerData();
    if (mfg.size() >= 2) cid = (uint8_t)mfg[0] | ((uint16_t)(uint8_t)mfg[1] << 8);
  }

  // категория для CLEAR SCAN (телефон/наушники/колонка/микрофон)
  char lowName[64] = {0};
  lowerCopy(raw, lowName, sizeof(lowName));
  uint8_t kindConf = 0;
  uint8_t kind = inferKind(dev, type, cid, lowName, &kindConf);

  uint32_t now = millis();
  int16_t slot = -1;
  bool fresh = false;

  portENTER_CRITICAL(&devMux);
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].used && memcmp(devices[i].addr, addr, 6) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    for (int i = 0; i < MAX_DEVICES; i++) {
      if (!devices[i].used) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) {  // мест нет: забираем слот самого давнего
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < MAX_DEVICES; i++) {
      if (devices[i].lastSeen <= oldest) {
        oldest = devices[i].lastSeen;
        slot = i;
      }
    }
    slotRecycles++;
  } else {
    fresh = !devices[slot].used;
  }

  if (fresh) {
    memset(&devices[slot], 0, sizeof(BleDevice));
    memcpy(devices[slot].addr, addr, 6);
    devices[slot].used = true;
    devices[slot].firstSeen = now;
    devices[slot].advCount = 0;
  }
  bool hadName = devices[slot].name[0] != 0;
  if (nm[0]) snprintf(devices[slot].name, sizeof(devices[slot].name), "%s", nm);
  if (svc[0]) snprintf(devices[slot].svc, sizeof(devices[slot].svc), "%s", svc);
  if (isIbeacon) snprintf(devices[slot].svc, sizeof(devices[slot].svc), "iBeacon %s", ib);
  devices[slot].rssi = (int8_t)dev.getRSSI();
  devices[slot].txPower = dev.haveTXPower() ? (int8_t)dev.getTXPower() : 0;
  devices[slot].appearance = dev.haveAppearance() ? dev.getAppearance() : 0;
  devices[slot].companyId = cid;
  devices[slot].kind = kind;
  devices[slot].kindConf = kindConf;
  devices[slot].lastSeen = now;
  devices[slot].advCount++;
  if (type != DT_UNKNOWN) {
    devices[slot].type = type;
    devices[slot].confidence = confidence;
  } else if (isIbeacon) {
    devices[slot].type = DT_BEACON;
    devices[slot].confidence = 95;
  }
  portEXIT_CRITICAL(&devMux);

#if SCAN_DEBUG
  if (fresh || (nm[0] && !hadName)) {
    Serial.printf("[adv] %02X:%02X:%02X:%02X:%02X:%02X rssi=%d имя=\"%s\"%s%s\n",
                  addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], (int)dev.getRSSI(),
                  devices[slot].name[0] ? devices[slot].name : raw,
                  fresh ? " [новое]" : " [имя пришло позже]",
                  dev.haveName() ? "" : " (в этом пакете имени нет)");
    uint8_t *pl = dev.getPayload();
    size_t plLen = dev.getPayloadLength();
    Serial.printf("[raw] %u байт:", (unsigned)plLen);
    for (size_t i = 0; i < plLen && i < 62; i++) {
      Serial.printf(" %02X", pl[i]);
    }
    bool hasNameField = false;
    for (size_t i = 0; i + 1 < plLen;) {
      uint8_t len = pl[i];
      if (len == 0) break;
      uint8_t type = pl[i + 1];
      if (type == 0x08 || type == 0x09) hasNameField = true;
      i += (size_t)len + 1;
    }
    Serial.printf("  -> поле имени (0x08/0x09): %s\n", hasNameField ? "ЕСТЬ" : "нет");
  }
#endif

  advCount++;
}

class AdvCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    uint8_t conf = 0;
    DevType t = inferDeviceType(dev, &conf);
    advUpsert(dev, t, conf);
  }
};

static AdvCallbacks advCallbacks;

void bleScanStart() {
  if (bleScanning) return;
  if (classicScanning) return;  // радио одно: классический поиск и BLE-скан по очереди
  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&advCallbacks, true);  // дубликаты = живой RSSI
  scan->setActiveScan(true);
  scan->setInterval(90);
  scan->setWindow(60);
  scan->start(0, nullptr, false);                            // 0 = непрерывно
  bleScanning = true;
}

void bleScanStop() {
  if (!bleScanning) return;
  BLEDevice::getScan()->stop();
  bleScanning = false;
}

void bleScanRecycle() {
  if (!bleScanning) return;
  BLEDevice::getScan()->clearResults();  // не даём кэшу стека расти
}

// ==================== СПИСКИ И ИЗБРАННОЕ ====================
void forgetStaleDevices() {
  uint32_t now = millis();
  portENTER_CRITICAL(&devMux);
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].used && now - devices[i].lastSeen > FORGET_MS) {
      devices[i].used = false;
    }
  }
  portEXIT_CRITICAL(&devMux);
}

static bool favFoundPrev[MAX_FAVS];
static bool favStrongPrev[MAX_FAVS];
static uint32_t favLogAt[MAX_FAVS];

static void logRssiSample(const uint8_t *addr, int8_t rssi) {
  memcpy(rssiLog[app.logHead].addr, addr, 6);
  rssiLog[app.logHead].rssi = rssi;
  rssiLog[app.logHead].t = millis();
  app.logHead = (uint8_t)((app.logHead + 1) % MAX_RSSI_LOG);
  if (app.logCount < MAX_RSSI_LOG) app.logCount++;
}

static void checkFavorites(uint32_t now) {
  bool strong = false;
  for (int f = 0; f < MAX_FAVS; f++) {
    bool found = false;
    int8_t rssi = 0;
    if (favorites[f].used) {
      for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].used) continue;
        if (memcmp(devices[i].addr, favorites[f].addr, 6) != 0) continue;
        if (now - devices[i].lastSeen > FAV_GONE_MS) continue;
        found = true;
        rssi = devices[i].rssi;
        break;
      }
      if (found && !favFoundPrev[f]) {
        ledPulse(250);
        toast("* DEVICE FOUND", 1500);
        buzzerPlay(1);
        Serial.printf("[*] Найдено: %s (%d dBm)\n", favorites[f].name, rssi);
      } else if (!found && favFoundPrev[f]) {
        buzzerPlay(2);
        Serial.printf("[i] Потеряно: %s\n", favorites[f].name);
      }
      if (found) {
        if (now - favLogAt[f] > 5000) {
          favLogAt[f] = now;
          logRssiSample(favorites[f].addr, rssi);
        }
      }
      bool isStrong = found && (rssi >= app.strongRssi);
      if (isStrong && !favStrongPrev[f]) strong = true;
      favStrongPrev[f] = isStrong;
    } else {
      favStrongPrev[f] = false;
    }
    favFoundPrev[f] = found;
  }
  if (strong) buzzerPlay(3);
}

// Сравнение для сортировки списков: <0 - a идёт раньше b.
// Режим задаётся в MANAGER (RSSI / имя / тип).
static int cmpDevices(int16_t ai, int8_t arssi, int16_t bi, int8_t brssi) {
  const char *an = devices[ai].name;
  const char *bn = devices[bi].name;
  if (app.sortMode == SORT_NAME) {
    bool ae = (an[0] == 0), be = (bn[0] == 0);
    if (ae != be) return ae ? 1 : -1;        // без имени - в конец списка
    if (!ae) {
      int c = strcasecmp(an, bn);
      if (c != 0) return c;
    }
    return (int)brssi - (int)arssi;
  }
  if (app.sortMode == SORT_TYPE) {
    if (devices[ai].type != devices[bi].type) {
      return (int)devices[ai].type - (int)devices[bi].type;
    }
    return (int)brssi - (int)arssi;
  }
  return (int)brssi - (int)arssi;             // по RSSI: сильные выше
}

void buildSortedDeviceList() {
  uint32_t now = millis();
  int16_t idx[MAX_DEVICES];
  int8_t rssi[MAX_DEVICES];
  uint8_t n = 0;

  portENTER_CRITICAL(&devMux);
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].used) continue;
    if (now - devices[i].lastSeen > STALE_MS) continue;
    idx[n] = (int16_t)i;
    rssi[n] = devices[i].rssi;
    n++;
  }
  portEXIT_CRITICAL(&devMux);

  for (uint8_t i = 1; i < n; i++) {  // сортировка вставками по режиму из MANAGER
    int16_t ki = idx[i];
    int8_t kr = rssi[i];
    int j = (int)i - 1;
    while (j >= 0 && cmpDevices(idx[j], rssi[j], ki, kr) > 0) {
      idx[j + 1] = idx[j];
      rssi[j + 1] = rssi[j];
      j--;
    }
    idx[j + 1] = ki;
    rssi[j + 1] = kr;
  }
  for (uint8_t i = 0; i < n; i++) scanOrder[i] = idx[i];
  scanOrderCount = n;

  static uint8_t lastLogged = 255;
  if (n != lastLogged) {
    lastLogged = n;
    Serial.printf("[i] BLE устройств в эфире: %u\n", (unsigned)n);
  }

  checkFavorites(now);
}

// Список для CLEAR SCAN: берём уже отсортированный scanOrder и оставляем только
// распознанные категории (телефон/наушники/колонка/микрофон).
void buildClearList() {
  clearCount = 0;
  for (uint8_t i = 0; i < scanOrderCount && clearCount < MAX_DEVICES; i++) {
    int16_t di = scanOrder[i];
    if (devices[di].kind == KIND_NONE) continue;
    clearOrder[clearCount++] = di;
  }

#if SCAN_DEBUG
  static uint8_t lastClear = 255;
  if (clearCount != lastClear) {
    lastClear = clearCount;
    Serial.printf("[clear] распознано: %u\n", (unsigned)clearCount);
    for (uint8_t i = 0; i < clearCount; i++) {
      char tag[6];
      char nm[20];
      kindTag(devices[clearOrder[i]].kind, devices[clearOrder[i]].kindConf, tag, sizeof(tag));
      deviceLabel(&devices[clearOrder[i]], nm, 16);
      Serial.printf("   %s %s rssi=%d conf=%u%%\n", tag, nm,
                    devices[clearOrder[i]].rssi, (unsigned)devices[clearOrder[i]].kindConf);
    }
  }
#endif
}

int16_t clearOrderIndexOf(const uint8_t *addr) {
  for (uint8_t i = 0; i < clearCount; i++) {
    if (memcmp(devices[clearOrder[i]].addr, addr, 6) == 0) return (int16_t)i;
  }
  return -1;
}

int16_t scanOrderIndexOf(const uint8_t *addr) {
  for (uint8_t i = 0; i < scanOrderCount; i++) {
    if (memcmp(devices[scanOrder[i]].addr, addr, 6) == 0) return (int16_t)i;
  }
  return -1;
}

bool deviceAddrSnapshot(int16_t idx, uint8_t *out) {
  if (idx < 0 || idx >= MAX_DEVICES || !devices[idx].used) return false;
  portENTER_CRITICAL(&devMux);
  memcpy(out, devices[idx].addr, 6);
  portEXIT_CRITICAL(&devMux);
  return true;
}

void deviceLabel(const BleDevice *d, char *out, size_t maxChars) {
  if (d->name[0]) {
    snprintf(out, maxChars + 1, "%.*s", (int)maxChars, d->name);
    return;
  }
  // имени в эфире нет - показываем, чем устройство представилось: производитель или тип
  const char *tag = companyShort(d->companyId);
  if (!tag || !*tag) tag = (d->type != DT_UNKNOWN) ? typeNameAt(d->type) : "";
  if (tag && *tag) snprintf(out, maxChars + 1, "%s %02X:%02X", tag, d->addr[4], d->addr[5]);
  else snprintf(out, maxChars + 1, "%02X:%02X", d->addr[4], d->addr[5]);
}

void macLabel(const uint8_t *addr, char *out, size_t outLen) {
  snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

int16_t findFavIndex(const uint8_t *addr) {
  for (int i = 0; i < MAX_FAVS; i++) {
    if (favorites[i].used && memcmp(favorites[i].addr, addr, 6) == 0) return (int16_t)i;
  }
  return -1;
}

bool isFavorite(const uint8_t *addr) { return findFavIndex(addr) >= 0; }

void toggleFavorite(int16_t devIdx) {
  if (devIdx < 0 || devIdx >= MAX_DEVICES || !devices[devIdx].used) return;
  int16_t f = findFavIndex(devices[devIdx].addr);
  if (f >= 0) {
    favorites[f].used = false;
    storageSaveFavorites();
    toast("UNWATCHED", 1200);
    Serial.printf("[i] Снят со слежения: %s\n", favorites[f].name);
    return;
  }
  for (int i = 0; i < MAX_FAVS; i++) {
    if (favorites[i].used) continue;
    favorites[i].used = true;
    memcpy(favorites[i].addr, devices[devIdx].addr, 6);
    deviceLabel(&devices[devIdx], favorites[i].name, DEV_NAME_LEN - 1);
    favFoundPrev[i] = true;
    storageSaveFavorites();
    toast("* WATCHING", 1200);
    Serial.printf("[i] Слежение включено: %s\n", favorites[i].name);
    return;
  }
  toast("WATCH LIST FULL", 1500);
}

// ==================== ПОДКЛЮЧЕНИЕ ====================
void connLine(const char *s) {
  if (app.connLineCount >= CONN_LINES) return;
  snprintf(app.connLines[app.connLineCount], CONN_LINE_LEN, "%s", s);
  app.connLineCount++;
}

bool bleConnect(const uint8_t *addr) {
  bleScanStop();
  buzzerSilence();  // connect() блокирует цикл до ~20 с - не даём пищалке висеть HIGH
  app.connLineCount = 0;
  BLEAddress address((uint8_t *)addr);
  if (!activeClient) {
    activeClient = BLEDevice::createClient();
    if (!activeClient) {
      connLine("NO CLIENT MEM");
      setError("createClient failed");
      return false;
    }
  }
  if (!activeClient->connect(address)) {
    connLine("CONNECT FAILED");
    return false;
  }  connLine("CONNECTED OK");

  std::map<std::string, BLERemoteService *> *svcs = activeClient->getServices();
  if (svcs) {
    int shown = 0;
    for (auto &kv : *svcs) {
      if (shown >= 2 || app.connLineCount >= CONN_LINES - 2) break;
      char code[10];
      uuidShort(kv.second->getUUID(), code, sizeof(code));
      int chCount = 0;
      std::map<std::string, BLERemoteCharacteristic *> *chars = kv.second->getCharacteristics();
      if (chars) chCount = (int)chars->size();
      char line[CONN_LINE_LEN];
      snprintf(line, sizeof(line), "%s ch:%d", code, chCount);
      connLine(line);
      shown++;
    }
    char tail[CONN_LINE_LEN];
    snprintf(tail, sizeof(tail), "SERVICES: %d", (int)svcs->size());
    connLine(tail);
  } else {
    connLine("NO SERVICES");
  }
  return true;
}

void bleDisconnect() {
  // клиент создаётся один раз и переиспользуется: в ядре 2.x нет deleteClient
  if (activeClient) activeClient->disconnect();
  app.connState = 0;
  app.connLineCount = 0;
  Serial.println("[i] Отключено");
}

// ==================== РЕКЛАМА (СВОЁ ИМЯ) ====================
static void advTruncateUtf8(const char *src, char *dst, size_t maxBytes) {
  size_t out = 0;
  const uint8_t *p = (const uint8_t *)src;
  while (*p) {
    uint8_t len = 1;
    if ((*p & 0xE0) == 0xC0) len = 2;
    else if ((*p & 0xF0) == 0xE0) len = 3;
    else if ((*p & 0xF8) == 0xF0) len = 4;
    bool complete = true;
    for (uint8_t i = 1; i < len; i++) {
      if (p[i] == 0) {
        complete = false;
        break;
      }
    }
    if (!complete) break;
    if (out + len > maxBytes) break;
    for (uint8_t i = 0; i < len; i++) dst[out++] = (char)p[i];
    p += len;
  }
  dst[out] = 0;
}

void advStart(const char *nameUtf8) {
  advStop();
  // 24 байта: adv-поле имени ограничено 29 байтами вместе с заголовком
  advTruncateUtf8(nameUtf8, advName, 24);
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advData.setName(std::string(advName));
  pAdv->setMinInterval(0x80);
  pAdv->setMaxInterval(0xA0);
  pAdv->setAdvertisementData(advData);
  pAdv->start();
  advRunning = true;
  Serial.printf("[i] Реклама как \"%s\" (%u байт)\n", advName, (unsigned)strlen(advName));
}

void advStop() {
  if (!advRunning) return;
  BLEDevice::getAdvertising()->stop();
  advRunning = false;
  advName[0] = 0;
  Serial.println("[i] Реклама остановлена");
}

// ==================== WI-FI ====================
void wifiScanStart() {
  if (wifiScanning) return;
  if (!wifiRadioOn) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(60);
    wifiRadioOn = true;
  }
  WiFi.scanNetworks(true, false);  // async, скрытые сети не нужны
  wifiScanning = true;
  wifiLastScan = millis();
}

void wifiScanStop() {
  if (!wifiScanning) return;
  wifiScanning = false;
  WiFi.scanDelete();
}

void wifiRadioOff() {
  if (!wifiRadioOn) return;
  WiFi.mode(WIFI_OFF);
  wifiRadioOn = false;
}

void wifiScanPoll() {
  if (!wifiScanning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  if (n < 0) {
    wifiScanning = false;
    setError("wifi scan failed");
    return;
  }
  uint8_t cnt = (uint8_t)(n > MAX_WIFI ? MAX_WIFI : n);
  for (uint8_t i = 0; i < cnt; i++) {
    snprintf(wifiNets[i].ssid, sizeof(wifiNets[i].ssid), "%s", WiFi.SSID(i).c_str());
    if (!wifiNets[i].ssid[0]) snprintf(wifiNets[i].ssid, sizeof(wifiNets[i].ssid), "<hidden>");
    wifiNets[i].rssi = (int8_t)WiFi.RSSI(i);
    wifiNets[i].ch = (uint8_t)WiFi.channel(i);
    wifiNets[i].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
  wifiCount = cnt;
  WiFi.scanDelete();
  wifiScanning = false;
  wifiScansDone++;
  Serial.printf("[i] Wi-Fi сетей: %u\n", wifiCount);
}
