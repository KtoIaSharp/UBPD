#include "config.h"

// ==================== ДАННЫЕ ЭКРАНОВ ====================
const char *const MENU_ITEMS[] = {
  "RADAR", "SCANNERS", "DEVICE TYPE", "WATCH", "TIMED SCAN", "SLEEP WATCH",
  "BLE FUN", "IDENTITY", "PC REMOTE", "REMOTE", "WEB UI", "MANAGER", "DIAGNOSTICS"
};
const uint8_t MENU_COUNT = 13;

const char *const TYPE_NAMES[] = {
  "Unknown", "Audio", "HID", "Beacon", "Phone", "Computer",
  "Watch", "Tracker", "Printer", "Camera", "Network"
};
#define TYPE_NAME_COUNT 11

static const char *const IDENT_PRESETS[] = {
  "UBPD", "BPD-01", "UNKNOWN", "POCKET", "TERMINAL", "SCANNER", "BLE DEVICE", "CUSTOM"
};
#define IDENT_PRESET_COUNT 8
#define IDENT_CUSTOM_IDX 7

static const char *const FUN_NAMES[] = {
  "КРУТОЙ МАЯЧОК", "ЛОХОТРОН 3000", "ГДЕ НАУШНИКИ", "UNKNOWN",
  "НЕ ТРОГАЙ", "BPD", "СТРАННЫЙ ГАДЖЕТ", "ЗДЕСЬ НИКОГО"
};
#define FUN_COUNT 8

static const char *const FUN_MODES[] = {"SINGLE", "CYCLE", "RANDOM"};

static const char EDITOR_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.";
static const uint8_t EDITOR_CHARS_COUNT = sizeof(EDITOR_CHARS) - 1;

static const char *const TIMED_FIELDS[] = {"ИНТЕРВАЛ", "ДЛИТЕЛЬН.", "ЗАПУСКОВ", "СТАРТ"};

// доступ к данным экранов из других вкладок (web.ino)
const char *typeNameAt(uint8_t t) { return TYPE_NAMES[t < TYPE_NAME_COUNT ? t : 0]; }
const char *identNameAt(uint8_t i) { return i < IDENT_PRESET_COUNT ? IDENT_PRESETS[i] : ""; }
uint8_t identNameCount() { return IDENT_PRESET_COUNT; }
const char *funNameAt(uint8_t i) { return i < FUN_COUNT ? FUN_NAMES[i] : ""; }
uint8_t funNameCount() { return FUN_COUNT; }
const char *funModeName(uint8_t m) { return FUN_MODES[m < 3 ? m : 0]; }

static uint32_t lastFrame = 0;
static uint32_t lastListRefresh = 0;

static void drawPcOverlay();

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void uiInit() {
  memset(&app, 0, sizeof(app));
  app.screen = SCR_MENU;
  app.devIndex = -1;
  app.deviceFrom = SCR_MENU;
  app.radarCursor = 0;
  app.radarSource = RS_BLE;
  app.radarSweep = true;
  app.identIdx = 0;
  snprintf(app.customNameStr, sizeof(app.customNameStr), "MY_UBPD");
  app.funMode = 0;
  app.funIndex = 0;
  app.timedInterval = 60;
  app.timedDuration = 10;
  app.timedRuns = 20;
  app.connState = 0;
  app.redraw = true;
  app.buzzerOn = true;
  app.buzzerInvert = (BUZZER_INVERT != 0);
  app.buzzerPassive = (BUZZER_ACTIVE == 0);
  app.strongRssi = -60;
  app.sleepInterval = 30;
  app.apOpen = true;
  app.settingsCursor = 0;
  app.logCount = 0;
  app.logHead = 0;
  app.sleeping = false;
  // фон по умолчанию разрешён всем (правится в MANAGER)
  for (uint8_t i = 0; i < BG_COUNT; i++) app.bg[i] = BG_BG;
  app.mgrCursor = 0;
  app.bgCursor = 0;
  app.sortMode = SORT_RSSI;
  app.diagPage = 0;
  app.scannersCursor = 0;
  app.combCursor = 0;
  app.combTop = 0;
  app.combSel = 0;
  app.combPhase = true;
  app.combSwitchAt = 0;
  app.classicDevFrom = SCR_MENU;
  app.pcMsg[0] = 0;
  app.pcMsgUntil = 0;
  app.pcCount = 0;
  app.remoteMode = 0;
  app.remoteConnectMode = 0;
  app.trackCursor = 0;
  app.trackTop = 0;
}

const char *identityAdvName() {
  if (app.identIdx < IDENT_CUSTOM_IDX) return IDENT_PRESETS[app.identIdx];
  return app.customNameStr;
}

// ==================== ФОН / ПРАВА ФУНКЦИЙ ====================
bool bgAllows(uint8_t func) {
  return app.bg[func] == BG_BG || app.bg[func] == BG_BG_SLEEP;
}

bool sleepAllows(uint8_t func) {
  return app.bg[func] == BG_SLEEP || app.bg[func] == BG_BG_SLEEP;
}

const char *bgStateName(uint8_t st) {
  switch (st) {
    case BG_BG: return "ФОН";
    case BG_SLEEP: return "СОН";
    case BG_BG_SLEEP: return "ФОН+СОН";
    default: return "ВЫКЛ";
  }
}

// Нужно ли сейчас сканировать: либо сама вкладка, либо фоновый мониторинг WATCH.
static bool screenWantsScan(Screen s) {
  if (s == SCR_BLESCAN || s == SCR_WATCH || s == SCR_TYPE) return true;
  if (s == SCR_RADAR && app.radarSource == RS_BLE) return true;
  if (s == SCR_WEB) return true;
  return false;
}

// ==================== РАДИО ПО ЭКРАНУ ====================
void syncRadios() {
  bleScanStop();
  wifiScanStop();

  if (app.screen != SCR_WEB && webRunning && !bgAllows(BG_WEBAP)) webStop();
  if (app.screen != SCR_WEB && !(app.screen == SCR_RADAR && app.radarSource == RS_WIFI)) wifiRadioOff();

  // --- сканирование: своя вкладка + фоновый мониторинг WATCH ---
  bool wantScan = screenWantsScan(app.screen);
  if (!wantScan && app.screen != SCR_CLASSIC && app.screen != SCR_SCANNER &&
      app.screen != SCR_PCPOPUP &&
      bgAllows(BG_WATCHMON) && favCount() > 0 && !app.sleeping) wantScan = true;

  if (app.screen == SCR_CLASSIC) {
    classicScanStart();
  } else if (app.screen == SCR_REMOTE) {
    // BLE и классика делят один радиомодуль: пока идёт классический поиск,
    // BLE-скан молчит, и наоборот. В списке устройств REMOTE ищем свои наушники,
    // а в плеере/пульте поиск глушим - радио нужно A2DP/AVRCP.
    if (app.remoteMode == 0) classicScanStart();
    else classicScanStop();
  } else if (app.screen == SCR_SCANNER) {
    // Общий список: тот же один радиомодуль, поэтому BLE и классика идут по
    // очереди (combPhase переключается в screenTick). Найденное копится в обоих
    // кэшах и показывается вместе.
    if (app.combPhase) {
      classicScanStop();
      bleScanStart();
    } else {
      bleScanStop();
      classicScanStart();
    }
  } else {
    classicScanStop();
    if (app.screen == SCR_RADAR && app.radarSource == RS_WIFI) {
      wifiScanStart();
    } else if (wantScan) {
      bleScanStart();
    }
  }

  if (app.screen == SCR_WEB) webInit();

  // --- реклама: своя вкладка, иначе - только с разрешения MANAGER ---
  const char *wantAdv = nullptr;
  if (app.screen == SCR_PCREMOTE || app.screen == SCR_PCPOPUP) wantAdv = nullptr;  // PC REMOTE: радио отдано SPP
  else if (app.screen == SCR_IDENTITY) wantAdv = identityAdvName();
  else if (app.screen == SCR_BLEFUN && app.funRunning) wantAdv = funNameAt(app.funIndex);
  else if (app.funRunning && bgAllows(BG_BLEFUN)) wantAdv = funNameAt(app.funIndex);
  else if (bgAllows(BG_IDENT)) wantAdv = identityAdvName();

#if SCAN_DEBUG
  Serial.printf("[sync] scr=%u bg0..4=%u,%u,%u,%u,%u scan=%d adv=\"%s\" run=%d\n",
                (unsigned)app.screen, app.bg[0], app.bg[1], app.bg[2], app.bg[3], app.bg[4],
                (int)wantScan, wantAdv ? wantAdv : "-", (int)advRunning);
#endif

  if (!wantAdv) {
    advStop();
  } else if (strcmp(wantAdv, advName) != 0 || !advRunning) {
    advStart(wantAdv);
  }
}

void enterScreen(Screen s) {
  if (app.screen == SCR_DEVICE && s != SCR_DEVICE && app.connState == 2) bleDisconnect();

  app.screen = s;
  app.redraw = true;

  switch (s) {
    case SCR_SCANNERS:
      app.scannersCursor = 0;
      break;
    case SCR_SCANNER:
      app.combCursor = 0;
      app.combTop = 0;
      app.combSel = 0;
      app.combPhase = true;
      app.combSwitchAt = millis();
      buildSortedDeviceList();
      buildClassicList();
      buildCombinedList();
      clampComb();
      break;
    case SCR_BLESCAN:
    case SCR_TYPE:
      app.listTop = 0;
      app.listCursor = 0;
      buildSortedDeviceList();
      if (app.selValid) {
        int16_t i = scanOrderIndexOf(app.selAddr);
        if (i >= 0) app.listCursor = (uint8_t)i;
      }
      clampList();
      break;
    case SCR_DEVICE:
      app.devAction = 0;
      if (app.connState != 2) {
        app.connState = 0;
        app.connLineCount = 0;
      }
      break;
    case SCR_WATCH:
      app.favCursor = 0;
      break;
    case SCR_CLASSIC:
      app.classicTop = 0;
      app.classicCursor = 0;
      app.classicSel = -1;
      buildClassicList();
      clampClassicList();
      break;
    case SCR_REMOTE:
      app.classicTop = 0;
      app.classicCursor = 0;
      app.classicSel = -1;
      app.remoteMode = 0;
      app.trackCursor = 0;
      app.trackTop = 0;
      buildClassicList();
      clampClassicList();
      playerScanTracks();
      break;
    case SCR_IDENTITY:
      app.identEditing = false;
      break;
    case SCR_TIMED:
      app.timedCursor = 0;
      break;
    default:
      break;
  }
  syncRadios();
}

void uiLoop() {
  uint8_t ev;
  while (btnEventPop(&ev)) handleEvent(ev);

  screenTick();

  uint32_t now = millis();
  uint16_t frame = (app.screen == SCR_RADAR) ? FRAME_RADAR_MS : FRAME_UI_MS;
  if (now - lastFrame < frame) return;
  lastFrame = now;
  app.redraw = false;
  if (app.pcMsg[0] && (int32_t)(app.pcMsgUntil - millis()) > 0) {
    drawPcOverlay();   // текст с ПК: на весь экран, базовый экран не рисуем (нет мигания)
  } else {
    drawCurrentScreen();
  }
}

void screenTick() {
  uint32_t now = millis();

  // Входящее Bluetooth-подключение (SPP): всплывающее окно с выбором действия.
  if (app.screen != SCR_PCPOPUP) {
    uint8_t peer[6];
    if (pcRemoteTakeIncoming(peer)) {
      memcpy(app.pcPeer, peer, 6);
      app.pcBack = app.screen;
      app.pcChoice = 0;
      enterScreen(SCR_PCPOPUP);
      buzzerPlay(1);   // вибро-уведомление
    }
  }

  if (app.screen != SCR_CLASSIC && app.screen != SCR_SCANNER &&
      (app.screen == SCR_RADAR || app.screen == SCR_BLESCAN || app.screen == SCR_WATCH ||
       app.screen == SCR_TYPE || app.screen == SCR_WEB ||
       (bgAllows(BG_WATCHMON) && favCount() > 0))) {
    if (now - lastListRefresh >= 1000) {
      lastListRefresh = now;
      bleScanRecycle();
      buildSortedDeviceList();
      forgetStaleDevices();
    }
  }
  if (app.screen == SCR_BLESCAN || app.screen == SCR_TYPE) {
    int16_t i = app.selValid ? scanOrderIndexOf(app.selAddr) : -1;
    if (i >= 0) app.listCursor = (uint8_t)i;
    clampList();
  } else if (app.screen == SCR_CLASSIC || app.screen == SCR_REMOTE) {
    classicTick();  // поиск сам заканчивается - держим его запущенным
    if (now - lastListRefresh >= 1000) {
      lastListRefresh = now;
      buildClassicList();
      forgetStaleClassic();
    }
    if (app.classicSel >= 0 && app.classicSel < MAX_CLASSIC && classics[app.classicSel].used) {
      int16_t i = classicOrderIndexOf(classics[app.classicSel].addr);
      if (i >= 0) app.classicCursor = (uint8_t)i;
    }
    clampClassicList();
  } else if (app.screen == SCR_SCANNER) {
    classicTick();
    if (now - app.combSwitchAt >= COMB_SWITCH_MS) {  // BLE <-> классика по очереди
      app.combSwitchAt = now;
      app.combPhase = !app.combPhase;
      syncRadios();
    }
    if (now - lastListRefresh >= 1000) {
      lastListRefresh = now;
      bleScanRecycle();
      buildSortedDeviceList();
      buildClassicList();
      buildCombinedList();
      forgetStaleDevices();
      forgetStaleClassic();
    }
    clampComb();
  }

  if (app.screen == SCR_RADAR) {
    radarTick();
    if (app.radarSource == RS_WIFI) {
      wifiScanPoll();
      if (!wifiScanning && now - wifiLastScan > 5000) wifiScanStart();
    }
  }

  if (app.screen == SCR_SLEEP) sleepTick();
  if (app.screen == SCR_BLEFUN) bleFunTick();

  // фоновые задачи: работают только с разрешения MANAGER
  if (app.timedRunning && (app.screen == SCR_TIMED || bgAllows(BG_TIMED))) timedTick();
  if (webRunning) webTick();
  if (app.funRunning && app.screen != SCR_BLEFUN && bgAllows(BG_BLEFUN)) bleFunTick();
}

void clampList() {
  uint8_t n = scanOrderCount;
  uint8_t rows = listRows();
  if (!n) {
    app.listCursor = 0;
    app.listTop = 0;
    return;
  }
  if (app.listCursor >= n) app.listCursor = n - 1;
  if (app.listTop + rows > n) app.listTop = (n > rows) ? (n - rows) : 0;
  if (app.listCursor < app.listTop) app.listTop = app.listCursor;
  if (app.listCursor >= app.listTop + rows) app.listTop = app.listCursor - rows + 1;
}

// ==================== СОБЫТИЯ ====================
void handleEvent(uint8_t ev) {
  switch (app.screen) {
    case SCR_MENU: handleMenu(ev); break;
    case SCR_RADAR: handleRadar(ev); break;
    case SCR_SCANNERS: handleScanners(ev); break;
    case SCR_SCANNER: handleScanner(ev); break;
    case SCR_BLESCAN: handleBleScan(ev); break;
    case SCR_DEVICE: handleDevice(ev); break;
    case SCR_WATCH: handleWatch(ev); break;
    case SCR_IDENTITY: handleIdentity(ev); break;
    case SCR_BLEFUN: handleBleFun(ev); break;
    case SCR_TIMED: handleTimed(ev); break;
    case SCR_TYPE: handleType(ev); break;
    case SCR_CLASSIC: handleClassic(ev); break;
    case SCR_CLASSICDEV: handleClassicDev(ev); break;
    case SCR_PCREMOTE: handlePcRemote(ev); break;
    case SCR_PCPOPUP: handlePcPopup(ev); break;
    case SCR_SLEEP: handleSleep(ev); break;
    case SCR_SETTINGS: handleSettings(ev); break;
    case SCR_MANAGER: handleManager(ev); break;
    case SCR_BGSET: handleBgSet(ev); break;
    case SCR_WEB: handleWeb(ev); break;
    case SCR_REMOTE: handleRemote(ev); break;
    case SCR_DIAG:
      if (ev == EV_BACK || ev == EV_BACK_LONG) enterScreen(SCR_MENU);
      else if (ev == EV_OK || ev == EV_OK_LONG) app.diagPage = (uint8_t)((app.diagPage + 1) % 2);
      break;
    default:
      break;
  }
}

void handleMenu(uint8_t ev) {
  if (ev == EV_UP) {
    if (app.menuCursor) app.menuCursor--;
  } else if (ev == EV_DOWN) {
    if (app.menuCursor + 1 < MENU_COUNT) app.menuCursor++;
  } else if (ev == EV_OK_LONG) {
    // быстрая проверка пищалки из любого места меню
    buzzerTest();
    toast("ТЕСТ ВИБРО", 1200);
  } else if (ev == EV_OK) {
    openMenuItem();
  }
}

void openMenuItem() {
  Screen s = SCR_MENU;
  switch (app.menuCursor) {
    case 0: s = SCR_RADAR; break;
    case 1: s = SCR_SCANNERS; break;
    case 2: s = SCR_TYPE; break;
    case 3: s = SCR_WATCH; break;
    case 4: s = SCR_TIMED; break;
    case 5: s = SCR_SLEEP; break;
    case 6: s = SCR_BLEFUN; break;
    case 7: s = SCR_IDENTITY; break;
    case 8: s = SCR_PCREMOTE; break;
    case 9: s = SCR_REMOTE; break;
    case 10: s = SCR_WEB; break;
    case 11: s = SCR_MANAGER; break;
    case 12: s = SCR_DIAG; break;
    default: break;
  }
  enterScreen(s);
}

void openDeviceScreen(int16_t devIdx, Screen from) {
  if (devIdx < 0 || devIdx >= MAX_DEVICES || !devices[devIdx].used) return;
  app.devIndex = devIdx;
  app.deviceFrom = (uint8_t)from;
  app.devAction = 0;
  app.connState = 0;
  app.connLineCount = 0;
  memcpy(app.selAddr, devices[devIdx].addr, 6);
  app.selValid = true;
  enterScreen(SCR_DEVICE);
}

// ==================== SCANNERS (подменю) ====================
// Три режима обнаружения в одной вкладке меню: общий список (классика + BLE),
// только классика и только BLE. Отдельный CLEAR SCAN убран: фильтр «телефон/
// наушники/колонка/микрофон» по сути то же, что и так находит классический поиск.
static const char *const SCANNER_ITEMS[] = {"SCANNER", "CLASSIC SCANNER", "BLE SCANNER"};
#define SCANNER_ITEMS_COUNT 3

void handleScanners(uint8_t ev) {
  if (ev == EV_UP) {
    if (app.scannersCursor) app.scannersCursor--;
  } else if (ev == EV_DOWN) {
    if (app.scannersCursor + 1 < SCANNER_ITEMS_COUNT) app.scannersCursor++;
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (app.scannersCursor == 0) enterScreen(SCR_SCANNER);
    else if (app.scannersCursor == 1) enterScreen(SCR_CLASSIC);
    else enterScreen(SCR_BLESCAN);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawScannersScreen() {
  u8g2.clearBuffer();
  drawHeader("SCANNERS", "");
  uint8_t rows = listRows();
  for (uint8_t r = 0; r < rows && r < SCANNER_ITEMS_COUNT; r++) {
    drawRow(r, SCANNER_ITEMS[r], "", r == app.scannersCursor);
  }
  drawFooter("OK-открыть  BACK-меню");
  u8g2.sendBuffer();
}

// Короткая метка типа устройства для одной строки списка.
static const char *shortTypeTag(DevType t) {
  switch (t) {
    case DT_PHONE: return "PH";
    case DT_AUDIO: return "AU";
    case DT_HID: return "HID";
    case DT_COMPUTER: return "PC";
    case DT_WATCH: return "WCH";
    case DT_TRACKER: return "TRK";
    case DT_BEACON: return "BCN";
    case DT_PRINTER: return "PRN";
    case DT_CAMERA: return "CAM";
    case DT_NETWORK: return "NET";
    default: return "--";
  }
}

void clampComb() {
  uint8_t n = combCount;
  uint8_t rows = listRows();
  if (!n) {
    app.combCursor = 0;
    app.combTop = 0;
    return;
  }
  if (app.combCursor >= n) app.combCursor = n - 1;
  if (app.combTop + rows > n) app.combTop = (n > rows) ? (n - rows) : 0;
  if (app.combCursor < app.combTop) app.combTop = app.combCursor;
  if (app.combCursor >= app.combTop + rows) app.combTop = app.combCursor - rows + 1;
}

// ==================== SCANNER (классика + BLE) ====================
void handleScanner(uint8_t ev) {
  if (ev == EV_UP || ev == EV_DOWN) {
    if (!combCount) return;
    int16_t idx = (int16_t)app.combCursor;
    if (ev == EV_UP) {
      if (idx > 0) idx--;
    } else {
      if (idx + 1 < (int16_t)combCount) idx++;
    }
    app.combCursor = (uint8_t)idx;
    app.combSel = combOrder[idx];
    clampComb();
  } else if (ev == EV_OK_LONG) {
    if (!combCount) return;
    int16_t v = combOrder[app.combCursor];
    if (v >= 0) toggleFavorite(v);
    else toggleFavoriteClassic((int16_t)(-1 - v));
  } else if (ev == EV_OK) {
    if (!combCount) return;
    int16_t v = combOrder[app.combCursor];
    if (v >= 0) {
      openDeviceScreen(v, SCR_SCANNER);
    } else {
      app.classicSel = (int16_t)(-1 - v);
      app.classicDevFrom = SCR_SCANNER;
      enterScreen(SCR_CLASSICDEV);
    }
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_SCANNERS);
  }
}

static void drawScannerScreen() {
  u8g2.clearBuffer();
  char right[10];
  snprintf(right, sizeof(right), "%u", (unsigned)combCount);
  drawHeader("SCANNER", right);

  uint8_t rows = listRows();
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = app.combTop + r;
    if (i >= combCount) break;
    int16_t v = combOrder[i];
    char tag[8];
    char nm[20];
    char rr[8];
    const uint8_t *addr;
    if (v >= 0) {
      const BleDevice *d = &devices[v];
      if (d->kind != KIND_NONE) kindTag(d->kind, d->kindConf, tag, sizeof(tag));
      else snprintf(tag, sizeof(tag), "%s", shortTypeTag(d->type));
      deviceLabel(d, nm, 12);
      addr = d->addr;
      snprintf(rr, sizeof(rr), "%d", d->rssi);
    } else {
      const ClassicDevice *d = &classics[-1 - v];
      if (d->kind != KIND_NONE) kindTag(d->kind, d->kindConf, tag, sizeof(tag));
      else snprintf(tag, sizeof(tag), "%s", shortTypeTag(d->type));
      classicLabel(d, nm, 12);
      addr = d->addr;
      if (d->rssi == -128) snprintf(rr, sizeof(rr), "--");
      else snprintf(rr, sizeof(rr), "%d", d->rssi);
    }
    char left[26];
    snprintf(left, sizeof(left), "%s%s %s", isFavorite(addr) ? "*" : " ", tag, nm);
    drawRow(r, left, rr, i == app.combCursor);
  }
  if (!combCount) {
    u8g2.setFont(FONT_BODY);
    txtCenter(rowBaseline(1), "поиск BLE + classic...");
  }
  drawFooter("OK-инфо HOLD-следить");
  u8g2.sendBuffer();
}

// ==================== BLE SCANNER (только BLE) ====================
void handleBleScan(uint8_t ev) {
  if (ev == EV_UP || ev == EV_DOWN) {
    if (!scanOrderCount) return;
    int16_t idx = app.selValid ? scanOrderIndexOf(app.selAddr) : (int16_t)app.listCursor;
    if (idx < 0) idx = (int16_t)app.listCursor;
    if (ev == EV_UP) {
      if (idx > 0) idx--;
    } else {
      if (idx + 1 < (int16_t)scanOrderCount) idx++;
    }
    app.listCursor = (uint8_t)idx;
    memcpy(app.selAddr, devices[scanOrder[idx]].addr, 6);
    app.selValid = true;
    clampList();
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (!scanOrderCount) return;
    openDeviceScreen(scanOrder[app.listCursor], SCR_BLESCAN);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_SCANNERS);
  }
}

static void drawBleScanScreen() {
  u8g2.clearBuffer();
  char right[8];
  snprintf(right, sizeof(right), "%u", (unsigned)scanOrderCount);
  drawHeader("BLE SCANNER", right);

  uint8_t rows = listRows();
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = app.listTop + r;
    if (i >= scanOrderCount) break;
    int16_t di = scanOrder[i];
    char nm[20];
    char label[22];
    deviceLabel(&devices[di], nm, 14);
    snprintf(label, sizeof(label), "%s%s", isFavorite(devices[di].addr) ? "*" : " ", nm);
    char rr[8];
    snprintf(rr, sizeof(rr), "%d", devices[di].rssi);
    bool sel = app.selValid && memcmp(devices[di].addr, app.selAddr, 6) == 0;
    drawRow(r, label, rr, sel);
  }
  if (!scanOrderCount) {
    u8g2.setFont(FONT_BODY);
    txtCenter(rowBaseline(1), "поиск BLE...");
  }
  drawFooter("OK-инфо BACK-назад");
  u8g2.sendBuffer();
}

// ==================== ШТАТНЫЙ ЭКРАН УСТРОЙСТВА ====================
static void startConnect(int16_t devIdx) {
  uint8_t addr[6];
  if (!deviceAddrSnapshot(devIdx, addr)) return;
  app.connState = 1;
  app.connLineCount = 0;
  connLine("СОЕДИНЯЮ...");
  drawCurrentScreen();
  ledPulse(400);
  bool ok = bleConnect(addr);
  app.connState = ok ? 2 : 3;
  app.devAction = 0;
  toast(ok ? "CONNECTED" : "FAILED", 1500);
}

void handleDevice(uint8_t ev) {
  if (app.connState == 2 || app.connState == 3) {
    if (ev == EV_OK || ev == EV_BACK || ev == EV_OK_LONG || ev == EV_BACK_LONG) {
      bool failed = (app.connState == 3);
      bleDisconnect();
      app.connState = 0;
      app.connLineCount = 0;
      toast(failed ? "NOT CONNECTED" : "DISCONNECTED", 1200);
      enterScreen((Screen)app.deviceFrom);
    }
    return;
  }

  if (ev == EV_UP) {
    if (app.devAction) app.devAction--;
  } else if (ev == EV_DOWN) {
    if (app.devAction + 1 < 3) app.devAction++;
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (app.devAction == 0) startConnect(app.devIndex);
    else if (app.devAction == 1) toggleFavorite(app.devIndex);
    else enterScreen((Screen)app.deviceFrom);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen((Screen)app.deviceFrom);
  }
}

static void drawDeviceScreen() {
  if (app.connState == 2 || app.connState == 3) {
    char body[6 * 28];
    body[0] = 0;
    size_t used = 0;
    for (uint8_t i = 0; i < app.connLineCount; i++) {
      int n = snprintf(body + used, sizeof(body) - used, "%s%s",
                       used ? "\n" : "", app.connLines[i]);
      if (n < 0 || (size_t)n >= sizeof(body) - used) break;
      used += (size_t)n;
    }
    drawTextScreen(app.connState == 2 ? "CONNECTED" : "CONNECT ERROR", "", body, "OK-отключить");
    return;
  }

  u8g2.clearBuffer();
  if (app.devIndex < 0 || app.devIndex >= MAX_DEVICES || !devices[app.devIndex].used) {
    drawHeader("DEVICE", "");
    u8g2.setFont(FONT_BODY);
    txt(2, rowBaseline(0), "устройство потеряно");
    drawFooter("BACK");
    u8g2.sendBuffer();
    return;
  }

  BleDevice *d = &devices[app.devIndex];
  char title[22];
  char rr[8];
  deviceLabel(d, title, 18);
  snprintf(rr, sizeof(rr), "%d", d->rssi);
  drawHeader(title, rr);

  u8g2.setFont(FONT_BODY);
  char l0[28], l1[28];
  const char *tn = TYPE_NAMES[d->type < TYPE_NAME_COUNT ? d->type : 0];
  snprintf(l0, sizeof(l0), "ТИП: %s %u%%", tn, (unsigned)d->confidence);
  if (d->type == DT_BEACON && d->svc[0]) snprintf(l0, sizeof(l0), "%.25s", d->svc);
  macLabel(d->addr, l1, sizeof(l1));
  char mfg[10];
  mfg[0] = 0;
  if (!d->name[0]) {
    const char *cs = companyShort(d->companyId);
    if (cs && *cs) snprintf(mfg, sizeof(mfg), "%s", cs);
    else if (d->companyId) snprintf(mfg, sizeof(mfg), "0x%04X", d->companyId);
  }
  drawRow(0, l0, mfg, false);
  drawRow(1, l1, "", false);

  uint8_t rows = listRows();
  uint8_t actionRows = (rows > 2) ? (uint8_t)(rows - 2) : 1;
  const char *wlabel = isFavorite(d->addr) ? "UNWATCH" : "WATCH";
  const char *actions[3] = {"CONNECT", wlabel, "BACK"};

  uint8_t top = 0;
  if (app.devAction >= actionRows) top = app.devAction - actionRows + 1;
  for (uint8_t r = 0; r < actionRows; r++) {
    uint8_t i = top + r;
    if (i >= 3) break;
    drawRow((uint8_t)(2 + r), actions[i], "", i == app.devAction);
  }
  drawFooter("OK-выбор BACK-назад");
  u8g2.sendBuffer();
}

// ==================== WATCH ====================
static int16_t favSlotAt(uint8_t cursor) {
  uint8_t n = 0;
  for (int i = 0; i < MAX_FAVS; i++) {
    if (!favorites[i].used) continue;
    if (n == cursor) return (int16_t)i;
    n++;
  }
  return -1;
}

int16_t favRssi(int16_t slot) {
  uint32_t now = millis();
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].used) continue;
    if (memcmp(devices[i].addr, favorites[slot].addr, 6) != 0) continue;
    if (now - devices[i].lastSeen > FAV_GONE_MS) continue;
    return devices[i].rssi;
  }
  // своё устройство могло быть найдено только по классике (BLE его не видит)
  for (int i = 0; i < MAX_CLASSIC; i++) {
    if (!classics[i].used) continue;
    if (memcmp(classics[i].addr, favorites[slot].addr, 6) != 0) continue;
    if (now - classics[i].lastSeen > FAV_GONE_MS) continue;
    if (classics[i].rssi == -128) continue;
    return classics[i].rssi;
  }
  return 127;
}

static void pushTimedLog(uint8_t found, uint8_t favs);

void handleWatch(uint8_t ev) {
  uint8_t n = (uint8_t)favCount();
  if (ev == EV_UP) {
    if (app.favCursor) app.favCursor--;
  } else if (ev == EV_DOWN) {
    if (app.favCursor + 1 < n) app.favCursor++;
  } else if (ev == EV_OK) {
    int16_t slot = favSlotAt(app.favCursor);
    if (slot < 0) return;
    int16_t di = -1;
    for (int i = 0; i < MAX_DEVICES; i++) {
      if (!devices[i].used) continue;
      if (memcmp(devices[i].addr, favorites[slot].addr, 6) == 0) {
        di = i;
        break;
      }
    }
    if (di < 0) {
      int16_t ci = classicLookupByAddr(favorites[slot].addr);
      if (ci >= 0) {
        app.classicSel = ci;
        app.classicDevFrom = SCR_WATCH;
        enterScreen(SCR_CLASSICDEV);
      } else {
        toast("NOT FOUND", 1200);
      }
    } else {
      openDeviceScreen(di, SCR_WATCH);
    }
  } else if (ev == EV_OK_LONG) {
    int16_t slot = favSlotAt(app.favCursor);
    if (slot < 0) return;
    favorites[slot].used = false;
    storageSaveFavorites();
    toast("УДАЛЕНО", 1200);
    if (app.favCursor) app.favCursor--;
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawWatchScreen() {
  u8g2.clearBuffer();
  char right[8];
  snprintf(right, sizeof(right), "%d", favCount());
  drawHeader("* WATCH", right);

  uint8_t n = (uint8_t)favCount();
  uint8_t rows = listRows();
  uint8_t top = 0;
  if (n && app.favCursor >= rows) top = app.favCursor - rows + 1;

  char left[24];
  int drawn = 0;
  for (int i = 0; i < MAX_FAVS; i++) {
    if (!favorites[i].used) continue;
    if (drawn < top) {
      drawn++;
      continue;
    }
    if (drawn - top >= rows) break;
    int16_t r = favRssi((int16_t)i);
    bool found = (r != 127);
    snprintf(left, sizeof(left), "%.14s", favorites[i].name);
    char rt[10];
    if (found) snprintf(rt, sizeof(rt), "%d", r);
    else snprintf(rt, sizeof(rt), "GONE");
    drawRow((uint8_t)(drawn - top), left, rt, drawn == app.favCursor);
    drawn++;
  }
  if (!n) {
    u8g2.setFont(FONT_BODY);
    txtCenter(rowBaseline(1), "список пуст");
    txtCenter(rowBaseline(2), "WATCH в SCANNERS");
  }
  drawFooter("OK-инфо HOLD-удалить");
  u8g2.sendBuffer();
}

// ==================== IDENTITY ====================
static void editCharShift(int8_t dir) {
  char *c = &app.customNameStr[app.identPos];
  const char *p = strchr(EDITOR_CHARS, *c);
  int idx = p ? (int)(p - EDITOR_CHARS) : 0;
  int n = (int)EDITOR_CHARS_COUNT;
  idx = (idx + dir + n) % n;
  *c = EDITOR_CHARS[idx];
}

void handleIdentity(uint8_t ev) {
  if (app.identEditing) {
    if (ev == EV_UP) editCharShift(1);
    else if (ev == EV_DOWN) editCharShift(-1);
    else if (ev == EV_OK) app.identPos = (uint8_t)((app.identPos + 1) % 8);
    else if (ev == EV_BACK || ev == EV_BACK_LONG || ev == EV_OK_LONG) {
      app.identEditing = false;
      storageSaveSettings();
      advStart(identityAdvName());
      toast("ИМЯ СОХРАНЕНО", 1500);
    }
    return;
  }

  if (ev == EV_UP) {
    if (app.identIdx) app.identIdx--;
  } else if (ev == EV_DOWN) {
    if (app.identIdx + 1 < IDENT_PRESET_COUNT) app.identIdx++;
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (app.identIdx == IDENT_CUSTOM_IDX) {
      app.identEditing = true;
      app.identPos = 0;
    } else {
      storageSaveSettings();
      advStart(identityAdvName());
      toast("ИМЯ В ЭФИРЕ", 1500);
    }
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    storageSaveSettings();
    enterScreen(SCR_MENU);
  }
}

static void drawIdentityScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(FONT_BODY);

  if (app.identEditing) {
    drawHeader("CUSTOM NAME", "");
    char ch[2] = {0, 0};
    for (uint8_t i = 0; i < 8; i++) {
      int16_t x = (int16_t)(2 + i * 7);
      int16_t y = rowBaseline(0);
      ch[0] = app.customNameStr[i] ? app.customNameStr[i] : ' ';
      if (i == app.identPos) {
        u8g2.setDrawColor(1);
        u8g2.drawBox(x - 1, y - 7, 7, 9);
        u8g2.setDrawColor(0);
        u8g2.drawStr(x, y, ch);
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(x, y, ch);
      }
    }
    txt(2, rowBaseline(1), "UP/DOWN - символ");
    txt(2, rowBaseline(2), "OK - вправо");
    txt(2, rowBaseline(3), "BACK - сохранить");
    drawFooter("из 8 символов");
    u8g2.sendBuffer();
    return;
  }

  char right[16];
  snprintf(right, sizeof(right), "%.10s", advName[0] ? advName : identityAdvName());
  drawHeader("IDENTITY", right);
  uint8_t rows = listRows();
  uint8_t top = 0;
  if (app.identIdx >= rows) top = app.identIdx - rows + 1;
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = top + r;
    if (i >= IDENT_PRESET_COUNT) break;
    char left[24];
    if (i == IDENT_CUSTOM_IDX) snprintf(left, sizeof(left), "CUSTOM: %s", app.customNameStr);
    else snprintf(left, sizeof(left), "%.22s", IDENT_PRESETS[i]);
    drawRow(r, left, "", i == app.identIdx);
  }
  drawFooter("OK-выбрать BACK-меню");
  u8g2.sendBuffer();
}

// ==================== BLE FUN ====================
void bleFunTick() {
  if (!app.funRunning || app.funMode == 0) return;
  if ((int32_t)(app.funNextSwitch - millis()) > 0) return;
  if (app.funMode == 1) {
    app.funIndex = (uint8_t)((app.funIndex + 1) % FUN_COUNT);
  } else {
    app.funIndex = (uint8_t)(esp_random() % FUN_COUNT);
  }
  advStart(FUN_NAMES[app.funIndex]);
  app.funNextSwitch = millis() + 10000;
}

void handleBleFun(uint8_t ev) {
  uint8_t total = FUN_COUNT + 2;
  if (ev == EV_UP) {
    if (app.funCursor) app.funCursor--;
  } else if (ev == EV_DOWN) {
    if (app.funCursor + 1 < total) app.funCursor++;
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    if (app.funRunning) toast("ADV в фоне", 1200);
    enterScreen(SCR_MENU);
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (app.funCursor < FUN_COUNT) {
      app.funIndex = app.funCursor;
      app.funMode = 0;
      app.funRunning = true;
      syncRadios();
      toast("ИМЯ В ЭФИРЕ", 1200);
    } else if (app.funCursor == FUN_COUNT) {
      app.funMode = (uint8_t)((app.funMode + 1) % 3);
      app.funNextSwitch = millis() + 10000;
      storageSaveSettings();
    } else {
      app.funRunning = !app.funRunning;
      syncRadios();
    }
  }
}

static void drawBleFunScreen() {
  u8g2.clearBuffer();
  char right[6];
  snprintf(right, sizeof(right), "%s", app.funRunning ? "ON" : "OFF");
  drawHeader("BLE FUN", right);

  uint8_t total = FUN_COUNT + 2;
  uint8_t rows = listRows();
  uint8_t top = 0;
  if (app.funCursor >= rows) top = app.funCursor - rows + 1;

  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = top + r;
    if (i >= total) break;
    char left[28];
    if (i < FUN_COUNT) {
      bool cur = (i == app.funIndex) && app.funRunning && app.funMode == 0;
      snprintf(left, sizeof(left), "%s%.20s", cur ? "*" : " ", FUN_NAMES[i]);
    } else if (i == FUN_COUNT) {
      snprintf(left, sizeof(left), "РЕЖИМ: %s", FUN_MODES[app.funMode]);
    } else {
      snprintf(left, sizeof(left), "%s", app.funRunning ? "ОСТАНОВИТЬ" : "ЗАПУСТИТЬ");
    }
    drawRow(r, left, "", i == app.funCursor);
  }
  drawFooter(advName[0] ? advName : "OK-запуск BACK-меню");
  u8g2.sendBuffer();
}

// ==================== TIMED SCAN ====================
static void pushTimedLog(uint8_t found, uint8_t favs) {
  if (app.timedLogCount < 3) app.timedLogCount++;
  for (int i = 2; i > 0; i--) {
    memcpy(app.timedLog[i], app.timedLog[i - 1], sizeof(app.timedLog[0]));
  }
  snprintf(app.timedLog[0], sizeof(app.timedLog[0]), "n=%u fav=%u", (unsigned)found, (unsigned)favs);
}

static void timedStart() {
  app.timedRunning = true;
  app.timedDone = 1;
  app.timedPhase = 0;
  app.timedFound = 0;
  app.timedLogCount = 0;
  memset(app.timedLog, 0, sizeof(app.timedLog));
  bleScanStart();
  app.timedDeadline = millis() + (uint32_t)app.timedDuration * 1000UL;
  toast("СКАН ЗАПУЩЕН", 1500);
}

void timedTick() {
  if (!app.timedRunning) return;
  uint32_t now = millis();
  if ((int32_t)(app.timedDeadline - now) > 0) return;

  if (app.timedPhase == 0) {
    bleScanStop();
    buildSortedDeviceList();
    uint8_t favs = 0;
    for (int f = 0; f < MAX_FAVS; f++) {
      if (!favorites[f].used) continue;
      for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].used) continue;
        if (memcmp(devices[i].addr, favorites[f].addr, 6) != 0) continue;
        if (now - devices[i].lastSeen > FAV_GONE_MS) continue;
        favs++;
        break;
      }
    }
    app.timedFound = favs;
    pushTimedLog(scanOrderCount, favs);
    app.timedPhase = 1;
    app.timedDeadline = now + (uint32_t)app.timedInterval * 1000UL;
    return;
  }

  if (app.timedDone >= app.timedRuns) {
    app.timedRunning = false;
    toast("СКАН ЗАВЕРШЁН", 2000);
    Serial.printf("[i] TIMED SCAN готов: %u прогонов\n", (unsigned)app.timedDone);
    return;
  }
  bleScanStart();
  app.timedPhase = 0;
  app.timedDone++;
  app.timedDeadline = now + (uint32_t)app.timedDuration * 1000UL;
}

void handleTimed(uint8_t ev) {
  if (app.timedRunning) {
    if (ev == EV_BACK || ev == EV_BACK_LONG || ev == EV_OK_LONG) {
      app.timedRunning = false;
      bleScanStop();
      toast("ОСТАНОВЛЕНО", 1200);
    }
    return;
  }

  if (ev == EV_UP || ev == EV_DOWN) {
    int8_t d = (ev == EV_UP) ? 1 : -1;
    if (app.timedCursor == 0) {
      int16_t v = (int16_t)app.timedInterval + d * 10;
      app.timedInterval = (uint16_t)constrain(v, 10, 600);
    } else if (app.timedCursor == 1) {
      int16_t v = (int16_t)app.timedDuration + d * 5;
      app.timedDuration = (uint16_t)constrain(v, 5, 60);
    } else if (app.timedCursor == 2) {
      int16_t v = (int16_t)app.timedRuns + d;
      app.timedRuns = (uint16_t)constrain(v, 1, 100);
    }
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (app.timedCursor == 3) {
      storageSaveSettings();
      timedStart();
    } else {
      app.timedCursor++;
    }
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    storageSaveSettings();
    enterScreen(SCR_MENU);
  }
}

static void drawTimedScreen() {
  u8g2.clearBuffer();
  char right[10];
  if (app.timedRunning) snprintf(right, sizeof(right), "%u/%u", (unsigned)app.timedDone, (unsigned)app.timedRuns);
  else snprintf(right, sizeof(right), "%u", (unsigned)app.timedRuns);
  drawHeader("TIMED SCAN", right);
  u8g2.setFont(FONT_BODY);

  if (app.timedRunning) {
    uint32_t leftMs = (int32_t)(app.timedDeadline - millis()) > 0 ? (app.timedDeadline - millis()) : 0;
    char l0[26];
    snprintf(l0, sizeof(l0), "%s  %u с", app.timedPhase == 0 ? "СКАНИРУЮ" : "ПАУЗА",
             (unsigned)(leftMs / 1000));
    txt(2, rowBaseline(0), l0);
    char l1[26];
    snprintf(l1, sizeof(l1), "устройств: %u", (unsigned)scanOrderCount);
    txt(2, rowBaseline(1), l1);
    char l2[26];
    snprintf(l2, sizeof(l2), "своих найдено: %u", (unsigned)app.timedFound);
    txt(2, rowBaseline(2), l2);
    for (uint8_t i = 0; i < app.timedLogCount && i < 1; i++) {
      txt(2, rowBaseline(3), app.timedLog[i]);
    }
    drawFooter("BACK-остановить");
    u8g2.sendBuffer();
    return;
  }

  char l[26];
  snprintf(l, sizeof(l), "%s: %u с", TIMED_FIELDS[0], (unsigned)app.timedInterval);
  drawRow(0, l, "", app.timedCursor == 0);
  snprintf(l, sizeof(l), "%s: %u с", TIMED_FIELDS[1], (unsigned)app.timedDuration);
  drawRow(1, l, "", app.timedCursor == 1);
  snprintf(l, sizeof(l), "%s: %u", TIMED_FIELDS[2], (unsigned)app.timedRuns);
  drawRow(2, l, "", app.timedCursor == 2);
  drawRow(3, TIMED_FIELDS[3], "", app.timedCursor == 3);
  drawFooter("UP/DOWN-знач. OK-старт");
  u8g2.sendBuffer();
}

// ==================== DEVICE TYPE ====================
void handleType(uint8_t ev) {
  if (ev == EV_UP || ev == EV_DOWN) {
    if (!scanOrderCount) return;
    int16_t idx = (int16_t)app.listCursor;
    if (ev == EV_UP) {
      if (idx > 0) idx--;
    } else {
      if (idx + 1 < (int16_t)scanOrderCount) idx++;
    }
    app.listCursor = (uint8_t)idx;
    clampList();
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (!scanOrderCount) return;
    openDeviceScreen(scanOrder[app.listCursor], SCR_TYPE);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawTypeScreen() {
  u8g2.clearBuffer();
  char right[8];
  snprintf(right, sizeof(right), "%u", (unsigned)scanOrderCount);
  drawHeader("DEVICE TYPE", right);

  uint8_t rows = listRows();
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = app.listTop + r;
    if (i >= scanOrderCount) break;
    int16_t di = scanOrder[i];
    char left[24];
    char nm[16];
    deviceLabel(&devices[di], nm, 12);
    const char *tn = TYPE_NAMES[devices[di].type < TYPE_NAME_COUNT ? devices[di].type : 0];
    snprintf(left, sizeof(left), "%.12s", nm);
    char rt[14];
    snprintf(rt, sizeof(rt), "%u%% %s", (unsigned)devices[di].confidence, tn);
    drawRow(r, left, rt, i == app.listCursor);
  }
  if (!scanOrderCount) {
    u8g2.setFont(FONT_BODY);
    txtCenter(rowBaseline(1), "нет данных");
  }
  drawFooter("вероятностно, не точно");
  u8g2.sendBuffer();
}

// ==================== SLEEP WATCH ====================
void handleSleep(uint8_t ev) {
  if (app.sleeping) {
    if (ev == EV_BACK || ev == EV_BACK_LONG || ev == EV_OK_LONG) {
      sleepStop();
      toast("СОН ВЫКЛ", 1200);
    }
    return;
  }
  if (ev == EV_UP || ev == EV_DOWN) {
    int16_t v = (int16_t)app.sleepInterval + (ev == EV_UP ? 15 : -15);
    app.sleepInterval = (uint16_t)constrain(v, 15, 300);
    storageSaveSettings();
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    sleepStart();
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawSleepScreen() {
  u8g2.clearBuffer();
  char right[8];
  snprintf(right, sizeof(right), "%uс", (unsigned)app.sleepInterval);
  drawHeader("SLEEP WATCH", right);
  u8g2.setFont(FONT_BODY);

  if (app.sleeping) {
    txt(2, rowBaseline(0), "РЕЖИМ СНА АКТИВЕН");
    char l[26];
    snprintf(l, sizeof(l), "циклов: %u", (unsigned)app.sleepEvents);
    txt(2, rowBaseline(1), l);
    snprintf(l, sizeof(l), "найдено/потеряно: %u/%u", (unsigned)app.sleepFound, (unsigned)app.sleepLost);
    txt(2, rowBaseline(2), l);
    txt(2, rowBaseline(3), "любая кнопка - выход");
    drawFooter("BACK-стоп");
    u8g2.sendBuffer();
    return;
  }

  txt(2, rowBaseline(0), "OLED гаснет, скан раз в");
  txt(2, rowBaseline(1), "интервал, на событие - LED");
  txt(2, rowBaseline(2), "и пищалка (если есть).");
  char l[26];
  snprintf(l, sizeof(l), "ИНТЕРВАЛ: %u с", (unsigned)app.sleepInterval);
  txt(2, rowBaseline(3), l);
  drawFooter("UP/DOWN-интервал OK-старт");
  u8g2.sendBuffer();
}

// ==================== SETTINGS ====================
static const char *const SETTINGS_ROWS[] = {
  "ВИБРО", "СИЛА ВИБРО", "ИНВЕРСИЯ", "СИЛЬНЫЙ RSSI", "СОН: ИНТЕРВАЛ",
  "ЯРКОСТЬ", "ТОЧКА ДОСТУПА", "СБРОС НАСТРОЕК"
};
#define SETTINGS_ROWS_COUNT 8

void handleSettings(uint8_t ev) {
  if (ev == EV_UP) {
    if (app.settingsCursor) app.settingsCursor--;
  } else if (ev == EV_DOWN) {
    if (app.settingsCursor + 1 < SETTINGS_ROWS_COUNT) app.settingsCursor++;
  } else if (ev == EV_OK_LONG) {
    if (app.settingsCursor == 0) {
      buzzerTest();
      toast("ТЕСТ ВИБРО", 1500);
    } else if (app.settingsCursor == 7) {
      storageReset();
      storageLoad();
      app.buzzerInvert = (BUZZER_INVERT != 0);
      app.buzzerPassive = (BUZZER_ACTIVE == 0);
      buzzerApplyIdle();
      u8g2.setContrast(brightness);
      toast("НАСТРОЙКИ СБРОШЕНЫ", 2000);
    }
  } else if (ev == EV_OK) {
    switch (app.settingsCursor) {
      case 0:
        app.buzzerOn = !app.buzzerOn;
        if (!app.buzzerOn) buzzerSilence();
        break;
      case 1:
        app.buzzerPassive = !app.buzzerPassive;
        buzzerApplyIdle();
        toast(app.buzzerPassive ? "СЛАБО" : "СИЛЬНО", 1500);
        buzzerTest();
        break;
      case 2:
        app.buzzerInvert = !app.buzzerInvert;
        buzzerApplyIdle();
        toast(app.buzzerInvert ? "ИНВЕРСИЯ" : "НОРМА", 1500);
        buzzerTest();
        break;
      case 3: {
        int16_t v = (int16_t)app.strongRssi - 5;
        if (v < -100) v = -20;
        app.strongRssi = (int8_t)v;
        break;
      }
      case 4: {
        int16_t v = (int16_t)app.sleepInterval + 15;
        if (v > 300) v = 15;
        app.sleepInterval = (uint16_t)v;
        break;
      }
      case 5:
        brightness = (brightness >= 255) ? 60 : (uint8_t)(brightness + 65);
        u8g2.setContrast(brightness);
        break;
      case 6: app.apOpen = !app.apOpen; break;
      default: break;
    }
    storageSaveSettings();
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    storageSaveSettings();
    enterScreen(SCR_MANAGER);
  }
}

static void drawSettingsScreen() {
  u8g2.clearBuffer();
  drawHeader("SETTINGS", "v" UBPD_VERSION);
  uint8_t rows = listRows();
  uint8_t top = 0;
  if (app.settingsCursor >= rows) top = app.settingsCursor - rows + 1;
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = top + r;
    if (i >= SETTINGS_ROWS_COUNT) break;
    char right[14];
    right[0] = 0;
    switch (i) {
      case 0:
#if BUZZER_ENABLED
        snprintf(right, sizeof(right), app.buzzerOn ? "ВКЛ" : "ВЫКЛ");
#else
        snprintf(right, sizeof(right), "НЕТ ЖЕЛЕЗА");
#endif
        break;
      case 1:
        snprintf(right, sizeof(right), app.buzzerPassive ? "СЛАБО" : "СИЛЬНО");
        break;
      case 2:
        snprintf(right, sizeof(right), app.buzzerInvert ? "ИНВЕРС" : "НОРМА");
        break;
      case 3: snprintf(right, sizeof(right), "%d dBm", app.strongRssi); break;
      case 4: snprintf(right, sizeof(right), "%u с", (unsigned)app.sleepInterval); break;
      case 5: snprintf(right, sizeof(right), "%u", (unsigned)brightness); break;
      case 6: snprintf(right, sizeof(right), app.apOpen ? "ОТКРЫТА" : "ПАРОЛЬ"); break;
      case 7: snprintf(right, sizeof(right), "HOLD"); break;
      default: break;
    }
    drawRow(r, SETTINGS_ROWS[i], right, i == app.settingsCursor);
  }
  drawFooter("OK-сменить HOLD-сброс");
  u8g2.sendBuffer();
}

// ==================== CLEAR SCAN УБРАН (v0.4.5) ====================
// Раньше здесь был отдельный режим CLEAR SCAN. Он не нужен: то же самое
// (телефоны/наушники/колонки/микрофоны) находит классический поиск, а метка
// типа (PH/HP/SP/MIC) теперь показывается прямо в общем списке SCANNER.


// ==================== CLASSIC BT (BR/EDR) ====================
// Вкладка ищет устройства по старому Bluetooth: телефоны в режиме "виден всем",
// гарнитуры, колонки, часы, авто. Телефон по BLE молчит, а тут отвечает и
// отдаёт своё имя. Класс устройства (CoD) приходит из эфира, поэтому
// "телефон / наушники / колонка / микрофон" - факт, а не догадка по вендору.

static const char *classicTypeTag(DevType t) {
  switch (t) {
    case DT_PHONE: return "PH";
    case DT_COMPUTER: return "PC";
    case DT_AUDIO: return "AU";
    case DT_HID: return "HID";
    case DT_NETWORK: return "NET";
    case DT_PRINTER: return "PRN";
    case DT_CAMERA: return "CAM";
    case DT_WATCH: return "WCH";
    default: return "--";
  }
}

void clampClassicList() {
  uint8_t n = classicCount;
  uint8_t rows = listRows();
  if (!n) {
    app.classicCursor = 0;
    app.classicTop = 0;
    return;
  }
  if (app.classicCursor >= n) app.classicCursor = n - 1;
  if (app.classicTop + rows > n) app.classicTop = (n > rows) ? (n - rows) : 0;
  if (app.classicCursor < app.classicTop) app.classicTop = app.classicCursor;
  if (app.classicCursor >= app.classicTop + rows) app.classicTop = app.classicCursor - rows + 1;
}

void handleClassic(uint8_t ev) {
  if (ev == EV_UP || ev == EV_DOWN) {
    if (!classicCount) return;
    int16_t idx = (int16_t)app.classicCursor;
    if (ev == EV_UP) {
      if (idx > 0) idx--;
    } else {
      if (idx + 1 < (int16_t)classicCount) idx++;
    }
    app.classicCursor = (uint8_t)idx;
    app.classicSel = classicOrder[idx];
    clampClassicList();
  } else if (ev == EV_OK_LONG) {
    if (!classicCount) return;
    toggleFavoriteClassic(classicOrder[app.classicCursor]);
  } else if (ev == EV_OK) {
    if (!classicCount) return;
    app.classicSel = classicOrder[app.classicCursor];
    app.classicDevFrom = SCR_CLASSIC;
    enterScreen(SCR_CLASSICDEV);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_SCANNERS);
  }
}

void handleClassicDev(uint8_t ev) {
  if (ev == EV_OK || ev == EV_OK_LONG) {
    if (app.classicSel >= 0) toggleFavoriteClassic(app.classicSel);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen((Screen)app.classicDevFrom);
  }
}

static void drawClassicScreen() {
  u8g2.clearBuffer();
  char right[10];
  snprintf(right, sizeof(right), "%u%s", (unsigned)classicCount, classicScanning ? "*" : "");
  drawHeader("CLASSIC SCANNER", right);

  uint8_t rows = listRows();
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = app.classicTop + r;
    if (i >= classicCount) break;
    const ClassicDevice *d = &classics[classicOrder[i]];
    char tag[8];
    if (d->kind != KIND_NONE) kindTag(d->kind, d->kindConf, tag, sizeof(tag));
    else snprintf(tag, sizeof(tag), "%s", classicTypeTag(d->type));
    char nm[20];
    classicLabel(d, nm, 12);
    char left[24];
    snprintf(left, sizeof(left), "%s%s %s", isFavorite(d->addr) ? "*" : " ", tag, nm);
    char rr[8];
    if (d->rssi == -128) snprintf(rr, sizeof(rr), "--");
    else snprintf(rr, sizeof(rr), "%d", d->rssi);
    drawRow(r, left, rr, i == app.classicCursor);
  }
  if (!classicCount) {
    u8g2.setFont(FONT_BODY);
    txtCenter(rowBaseline(1), "поиск classic...");
    txtCenter(rowBaseline(2), "устройство должно быть");
    txtCenter(rowBaseline(3), "\"видно всем\"");
  }
  drawFooter("OK-инфо HOLD-следить");
  u8g2.sendBuffer();
}

static void drawClassicDevScreen() {
  if (app.classicSel < 0 || app.classicSel >= MAX_CLASSIC || !classics[app.classicSel].used) {
    drawTextScreen("CLASSIC BT", "", "устройство потеряно", "BACK-назад");
    return;
  }
  const ClassicDevice *d = &classics[app.classicSel];
  char title[22];
  classicLabel(d, title, 18);
  char rr[8];
  if (d->rssi == -128) snprintf(rr, sizeof(rr), "--");
  else snprintf(rr, sizeof(rr), "%d", d->rssi);

  char mac[20];
  macLabel(d->addr, mac, sizeof(mac));
  char kind[10];
  kind[0] = 0;
  if (d->kind != KIND_NONE) kindTag(d->kind, d->kindConf, kind, sizeof(kind));

  char body[5 * 28];
  snprintf(body, sizeof(body),
           "ТИП: %s%s%s\n"
           "MAC: %s\n"
           "КЛАСС: %04X  КОД: %u\n"
           "%s",
           typeNameAt(d->type), kind[0] ? " " : "", kind,
           mac,
           (unsigned)(d->cod & 0x1FFF), (unsigned)d->cod,
           isFavorite(d->addr) ? "* СЛЕЖУ (OK-снять)" : "OK-поставить на слежение");
  drawTextScreen(title, rr, body, "OK-следить BACK-назад");
}

// ==================== MANAGER ====================
// Папка со всем служебным. Внутри неё отдельная категория «НАСТРОЙКИ ФОНА»
// (права функций в фоне), а рядом - точка доступа, сортировка, вход в настройки
// и сброс. Сами функции фона по умолчанию не работают: пока НАСТРОЙКИ ФОНА
// не разрешат иначе, вкладка живёт только пока ты в ней.

static const char *const MANAGER_ROWS[] = {
  "НАСТРОЙКИ ФОНА", "ТОЧКА ДОСТУПА", "СОРТИРОВКА", "НАСТРОЙКИ", "СБРОС НАСТРОЕК"
};
#define MANAGER_ROWS_COUNT 5
#define MGR_BG_ROW 0
#define MGR_AP_ROW 1
#define MGR_SORT_ROW 2
#define MGR_SETTINGS_ROW 3
#define MGR_RESET_ROW 4

static const char *sortName(uint8_t m) {
  switch (m) {
    case SORT_NAME: return "ИМЯ";
    case SORT_TYPE: return "ТИП";
    default: return "RSSI";
  }
}

void handleManager(uint8_t ev) {
  if (ev == EV_UP) {
    if (app.mgrCursor) app.mgrCursor--;
  } else if (ev == EV_DOWN) {
    if (app.mgrCursor + 1 < MANAGER_ROWS_COUNT) app.mgrCursor++;
  } else if (ev == EV_OK_LONG) {
    if (app.mgrCursor == MGR_RESET_ROW) {
      storageReset();
      storageLoad();
      buzzerApplyIdle();
      u8g2.setContrast(brightness);
      toast("НАСТРОЙКИ СБРОШЕНЫ", 2000);
    }
  } else if (ev == EV_OK) {
    if (app.mgrCursor == MGR_BG_ROW) {
      enterScreen(SCR_BGSET);                     // категория «НАСТРОЙКИ ФОНА»
    } else if (app.mgrCursor == MGR_AP_ROW) {     // точка доступа
      if (webRunning) webStop();
      else webInit();
      storageSaveSettings();
    } else if (app.mgrCursor == MGR_SORT_ROW) {
      app.sortMode = (uint8_t)((app.sortMode + 1) % 3);
      storageSaveSettings();
      buildSortedDeviceList();
    } else if (app.mgrCursor == MGR_SETTINGS_ROW) {
      enterScreen(SCR_SETTINGS);
    }
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawManagerScreen() {
  u8g2.clearBuffer();
  drawHeader("MANAGER", webRunning ? "AP:ON" : "");
  uint8_t rows = listRows();
  uint8_t top = 0;
  if (app.mgrCursor >= rows) top = app.mgrCursor - rows + 1;

  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = top + r;
    if (i >= MANAGER_ROWS_COUNT) break;
    char right[14];
    right[0] = 0;
    if (i == MGR_BG_ROW) {
      uint8_t on = 0;
      for (uint8_t k = 0; k < BG_COUNT; k++) if (app.bg[k] != BG_OFF) on++;
      snprintf(right, sizeof(right), "%u/%u", (unsigned)on, (unsigned)BG_COUNT);
    } else if (i == MGR_AP_ROW) {
      snprintf(right, sizeof(right), "%s", webRunning ? "ВКЛ" : "ВЫКЛ");
    } else if (i == MGR_SORT_ROW) {
      snprintf(right, sizeof(right), "%s", sortName(app.sortMode));
    } else if (i == MGR_RESET_ROW) {
      snprintf(right, sizeof(right), "HOLD");
    }
    drawRow(r, MANAGER_ROWS[i], right, i == app.mgrCursor);
  }
  drawFooter("OK-открыть  HOLD-сброс");
  u8g2.sendBuffer();
}

// ==================== НАСТРОЙКИ ФОНА (категория внутри MANAGER) ====================
// Права функций в фоне: у каждой своё из 4 состояний (ВЫКЛ/ФОН/СОН/ФОН+СОН).
// Функция работает в фоне только если здесь разрешено.
static const char *const BG_ROWS[] = {
  "ФОН: BLE FUN", "ФОН: IDENTITY", "ФОН: WATCH", "ФОН: TIMED SCAN", "ФОН: WEB UI"
};

void handleBgSet(uint8_t ev) {
  if (ev == EV_UP) {
    if (app.bgCursor) app.bgCursor--;
  } else if (ev == EV_DOWN) {
    if (app.bgCursor + 1 < BG_COUNT) app.bgCursor++;
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (app.bgCursor < BG_COUNT) {
      uint8_t st = (uint8_t)((app.bg[app.bgCursor] + 1) % 4);
      app.bg[app.bgCursor] = st;
      storageSaveSettings();
      syncRadios();   // права поменялись - сразу применяем
      toast(bgStateName(st), 1200);
    }
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MANAGER);
  }
}

static void drawBgSetScreen() {
  u8g2.clearBuffer();
  drawHeader("НАСТРОЙКИ ФОНА", "");
  uint8_t rows = listRows();
  uint8_t top = 0;
  if (app.bgCursor >= rows) top = app.bgCursor - rows + 1;
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = top + r;
    if (i >= BG_COUNT) break;
    drawRow(r, BG_ROWS[i], bgStateName(app.bg[i]), i == app.bgCursor);
  }
  drawFooter("OK-сменить  BACK-назад");
  u8g2.sendBuffer();
}

// ==================== REMOTE (A2DP-плеер) ====================
// REMOTE — три режима:
//   0) устройства: выбрать свои наушники/колонку и подключиться (A2DP+AVRCP);
//   1) плеер: список *.wav из /UBPD/sounds, OK - играть, HOLD - рандом;
//   2) AVRCP-пульт: UP/DOWN - громкость, OK - play/pause, HOLD - next.
void handleRemote(uint8_t ev) {
  if (app.remoteMode == 0) {
    if (ev == EV_UP || ev == EV_DOWN) {
      if (!classicCount) return;
      int16_t idx = (int16_t)app.classicCursor;
      if (ev == EV_UP) { if (idx > 0) idx--; }
      else { if (idx + 1 < (int16_t)classicCount) idx++; }
      app.classicCursor = (uint8_t)idx;
      clampClassicList();
    } else if (ev == EV_OK_LONG) {
      app.remoteConnectMode = app.remoteConnectMode ? 0 : 1;   // FULL <-> CTRL
      toast(app.remoteConnectMode ? "РЕЖИМ: CTRL" : "РЕЖИМ: FULL", 1200);
    } else if (ev == EV_OK) {
      if (!classicCount) return;
      const ClassicDevice *d = &classics[classicOrder[app.classicCursor]];
      playerConnect(d->addr);            // A2DP+AVRCP (и ACL для AVRCP)
      if (app.remoteConnectMode == 0) {
        playerScanTracks();
        if (playerTrackCount()) playerPlayTrack(0);
        else { playerUseTone(); playerPlay(); toast("НЕТ ФАЙЛОВ НА SD - ТОН", 2500); }
        app.trackCursor = 0;
        app.remoteMode = 1;              // FULL: плеер, играем
        toast("FULL: играю", 1500);
      } else {
        app.remoteMode = 2;              // CTRL: только управление, без воспроизведения
        toast("CTRL: управление", 1500);
      }
      syncRadios();                      // в плеере/пульте поиск больше не нужен
    } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
      enterScreen(SCR_MENU);
    }
  } else if (app.remoteMode == 1) {
    uint8_t n = playerTrackCount();
    if (ev == EV_UP || ev == EV_DOWN) {
      if (!n) return;
      int16_t idx = (int16_t)playerCurrentTrack();
      if (ev == EV_UP) idx = (idx > 0) ? idx - 1 : (int16_t)n - 1;
      else idx = (idx + 1 < (int16_t)n) ? idx + 1 : 0;
      playerPlayTrack((uint8_t)idx);         // переключение сразу играет
      app.trackCursor = (uint8_t)idx;
    } else if (ev == EV_OK) {
      if (playerPlaying()) { playerPause(); toast("ПАУЗА", 800); }
      else if (n) { playerPlay(); toast("ИГРАЮ", 800); }
      else { playerUseTone(); playerPlay(); toast("ТЕСТ-ТОН", 900); }
    } else if (ev == EV_OK_LONG) {
      if (n) { playerPlayRandom(); app.trackCursor = playerCurrentTrack(); toast("RANDOM", 900); }
    } else if (ev == EV_BACK_LONG) {
      playerStop();                          // HOLD BACK - выйти из REMOTE совсем
      app.remoteMode = 0;
      enterScreen(SCR_MENU);
    } else if (ev == EV_BACK) {
      playerStop();                          // назад к списку устройств
      app.remoteMode = 0;
      syncRadios();
    }
  } else {
    if (ev == EV_UP) { avrcVolUp(); toast("VOL+", 700); }
    else if (ev == EV_DOWN) { avrcVolDown(); toast("VOL-", 700); }
    else if (ev == EV_OK) { avrcPlayPause(); toast("PLAY/PAUSE", 900); }
    else if (ev == EV_OK_LONG) { avrcNext(); toast("NEXT", 900); }
    else if (ev == EV_BACK_LONG) { avrcPrev(); toast("PREV", 900); }
    else if (ev == EV_BACK) { app.remoteMode = 0; syncRadios(); }
  }
}

// Обрезка UTF-8 строки справа по ширине в пикселях (для центрирования имени).
static void clipUtf8(char *s, int16_t maxW) {
  while (s[0] && txtW(s) > maxW) {
    size_t len = strlen(s);
    size_t i = len;
    while (i > 0 && ((uint8_t)s[i - 1] & 0xC0) == 0x80) i--;
    if (i > 0) i--;
    s[i] = 0;
  }
}

// Экран плеера (FULL): имя трека, полоса прогресса, номер. Не список, а плеер.
static void drawRemotePlayerScreen() {
  u8g2.clearBuffer();
  uint8_t n = playerTrackCount();
  uint8_t cur = playerCurrentTrack();
  drawHeader("ПЛЕЕР", playerPlaying() ? "PLAY" : "STOP");

  char nm[44];
  if (n) snprintf(nm, sizeof(nm), "%s", playerTrackName(cur));
  else snprintf(nm, sizeof(nm), sdMounted() ? "нет *.wav в /UBPD" : "SD НЕ ПОДКЛЮЧЕНА");
  u8g2.setFont(FONT_HEAD);
  clipUtf8(nm, SCREEN_W - 6);
  txtCenter(26, nm);

  uint8_t prog = playerProgress();
  u8g2.drawFrame(8, 33, SCREEN_W - 16, 7);
  if (prog) {
    int16_t w = (int16_t)((SCREEN_W - 18) * prog / 100);
    if (w > 0) u8g2.drawBox(9, 34, (uint8_t)w, 5);
  }

  u8g2.setFont(FONT_BODY);
  char idx[40];
  if (n) snprintf(idx, sizeof(idx), "ТРЕК %u/%u", (unsigned)(cur + 1), (unsigned)n);
  else snprintf(idx, sizeof(idx), sdMounted() ? "положи WAV в sounds" : "проверь карту/контакты");
  txtCenter(48, idx);

  drawFooter("OK-плей U/D HOLD-выход");
  u8g2.sendBuffer();
}

static void drawRemoteScreen() {
  u8g2.clearBuffer();

  if (app.remoteMode == 0) {
    drawHeader("REMOTE:BT", app.remoteConnectMode ? "CTRL" : "FULL");
    uint8_t rows = listRows();
    for (uint8_t r = 0; r < rows; r++) {
      uint8_t i = app.classicTop + r;
      if (i >= classicCount) break;
      const ClassicDevice *d = &classics[classicOrder[i]];
      char nm[20];
      classicLabel(d, nm, 12);
      char tag[8];
      if (d->kind != KIND_NONE) kindTag(d->kind, d->kindConf, tag, sizeof(tag));
      else snprintf(tag, sizeof(tag), "%s", "BT");
      char left[24];
      snprintf(left, sizeof(left), "%s %s", tag, nm);
      char rr[8];
      if (d->rssi == -128) snprintf(rr, sizeof(rr), "--");
      else snprintf(rr, sizeof(rr), "%d", d->rssi);
      drawRow(r, left, rr, i == app.classicCursor);
    }
    if (!classicCount) {
      u8g2.setFont(FONT_BODY);
      txtCenter(rowBaseline(1), "поиск наушников...");
      txtCenter(rowBaseline(2), "включи их, режим");
      txtCenter(rowBaseline(3), "\"виден всем\"");
    }
    drawFooter("OK-подкл HOLD-режим");
    u8g2.sendBuffer();
    return;
  }

  if (app.remoteMode == 1) {
    drawRemotePlayerScreen();
    return;
  }

  // AVRCP-пульт
  drawHeader("REMOTE:CTRL", playerConnected() ? "LINK" : "");
  u8g2.setFont(FONT_BODY);
  txt(2, rowBaseline(0), "AVRCP: своё устройство");
  txt(2, rowBaseline(1), "UP/DOWN - громкость");
  txt(2, rowBaseline(2), "OK - play/pause");
  txt(2, rowBaseline(3), "HOLD - next  HOLD BACK - prev");
  drawFooter("BACK-назад");
  u8g2.sendBuffer();
}

static void drawWebScreen() {
  u8g2.clearBuffer();
  drawHeader("WEB UI", webRunning ? "ON" : "OFF");
  u8g2.setFont(FONT_BODY);
  char l[26];
  if (webRunning) {
    txt(2, rowBaseline(0), "ТОЧКА ДОСТУПА:");
    snprintf(l, sizeof(l), "%.20s", webApSsid());
    txt(2, rowBaseline(1), l);
    snprintf(l, sizeof(l), "http://%s", apIpString());
    txt(2, rowBaseline(2), l);
    snprintf(l, sizeof(l), "клиентов: %d", apClientCount());
    txt(2, rowBaseline(3), l);
    drawFooter("OK-выключить AP");
  } else {
    txt(2, rowBaseline(0), "AP выключена.");
    txt(2, rowBaseline(1), "OK - включить, затем");
    txt(2, rowBaseline(2), "открой в телефоне адрес");
    txt(2, rowBaseline(3), "192.168.4.1");
    drawFooter("OK-включить BACK-меню");
  }
  u8g2.sendBuffer();
}

void handleWeb(uint8_t ev) {
  if (ev == EV_OK || ev == EV_OK_LONG) {
    if (webRunning) {
      webStop();
      toast("AP ВЫКЛ", 1200);
    } else {
      webInit();
      toast("AP ВКЛ", 1200);
    }
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawDiagScreen() {
  u8g2.clearBuffer();

  if (app.diagPage == 1) {
    // страница ABOUT
    drawHeader("ABOUT", "2/2");
    u8g2.setFont(FONT_BODY);
    char l[26];
    snprintf(l, sizeof(l), "%s rev%u  %uMHz", ESP.getChipModel(),
             (unsigned)ESP.getChipRevision(), (unsigned)ESP.getCpuFreqMHz());
    txt(2, rowBaseline(0), l);
    uint8_t m[6] = {0};
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    snprintf(l, sizeof(l), "MAC %02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    txt(2, rowBaseline(1), l);
    snprintf(l, sizeof(l), "FLASH %u MB  RAM %u KB",
             (unsigned)(ESP.getFlashChipSize() / 1048576), (unsigned)(ESP.getHeapSize() / 1024));
    txt(2, rowBaseline(2), l);
    snprintf(l, sizeof(l), "FW %s  сборка %s", UBPD_VERSION, __DATE__);
    txt(2, rowBaseline(3), l);
    snprintf(l, sizeof(l), "uptime %s", uptimeLabel());
    txt(2, rowBaseline(4), l);
    drawFooter("OK-страница BACK-меню");
    u8g2.sendBuffer();
    return;
  }

  drawHeader("DIAGNOSTICS", "1/2");
  u8g2.setFont(FONT_BODY);

  uint16_t used = 0;
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].used) used++;
  }

  char l[26];
  float t = chipTemperature();
  if (isnan(t)) snprintf(l, sizeof(l), "BAT %s  TEMP n/a", batteryLabel());
  else snprintf(l, sizeof(l), "BAT %s  TEMP %.0fC", batteryLabel(), t);
  txt(2, rowBaseline(0), l);

  snprintf(l, sizeof(l), "RAM %uK/%uK", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024));
  txt(2, rowBaseline(1), l);

  snprintf(l, sizeof(l), "FLASH %uK  v%s", (unsigned)(ESP.getFlashChipSize() / 1024), UBPD_VERSION);
  txt(2, rowBaseline(2), l);

  snprintf(l, sizeof(l), "BLE n:%u/%u CLS %u", (unsigned)scanOrderCount, (unsigned)used, (unsigned)classicCount);
  txt(2, rowBaseline(3), l);

  if (listRows() > 4) {
    snprintf(l, sizeof(l), "ADV %s  WIFI %s", advRunning ? "ON" : "OFF",
             app.radarSource == RS_WIFI ? "MAP" : (wifiRadioOn ? "ON" : "OFF"));
    txt(2, rowBaseline(4), l);
  }

  char foot[26];
  snprintf(foot, sizeof(foot), "UP %s  E:%u  W:%u", uptimeLabel(), (unsigned)errorCount, (unsigned)app.timedDone);
  drawFooter(foot);
  u8g2.sendBuffer();
}
// ==================== PC REMOTE (текст с ПК на экран) ====================
// ПК открывает страницу WEB UI (http://192.168.4.1) и отправляет текст — он
// показывается поверх любого экрана несколько секунд:
//   POST /api/oled?secs=5   (тело запроса = текст)
//   GET  /api/oled?clear=1  (стереть)
void handlePcRemote(uint8_t ev) {
  if (ev == EV_OK || ev == EV_OK_LONG) {
    app.pcMsg[0] = 0;
    app.pcMsgUntil = 0;
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawPcRemoteScreen() {
  u8g2.clearBuffer();
  char right[12];
  snprintf(right, sizeof(right), "n%u", (unsigned)app.pcCount);
  drawHeader("PC REMOTE", right);
  u8g2.setFont(FONT_BODY);
  char l[26];
  txt(2, rowBaseline(0), "Bluetooth SPP");
  snprintf(l, sizeof(l), "ПК: %s", pcRemoteConnected() ? "ПОДКЛЮЧЁН" : "ожидание...");
  txt(2, rowBaseline(1), l);
  txt(2, rowBaseline(2), "имя: UBPD");
  txt(2, rowBaseline(3), app.pcMsg[0] ? "текст с ПК показан" : "JSON: oled/clear/state");
  drawFooter("OK-стереть BACK-меню");
  u8g2.sendBuffer();
}

// ==================== ВСПЛЫВАЮЩЕЕ ОКНО ВХОДЯЩЕГО ПОДКЛЮЧЕНИЯ ====================
// Кто-то (телефон/ПК) подключился к нашему SPP-серверу "UBPD" - спрашиваем,
// что с ним делать. Соединение уже установлено; FULL/CTRL переиспользуют его
// для A2DP-плеера или AVRCP-пульта, "ПК" оставляет как канал PC REMOTE.
static const char *const PC_POPUP_ROWS[] = {
  "FULL: играть",       // A2DP+AVRCP, запустить плеер
  "CTRL: пульт",        // только AVRCP-управление
  "ПК: SPP-терминал",   // оставить как канал PC REMOTE
  "ОТКАЗ (отключить)"   // разорвать соединение
};
#define PC_POPUP_COUNT 4

void handlePcPopup(uint8_t ev) {
  if (ev == EV_UP) {
    if (app.pcChoice) app.pcChoice--;
  } else if (ev == EV_DOWN) {
    if (app.pcChoice + 1 < PC_POPUP_COUNT) app.pcChoice++;
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    uint8_t peer[6];
    memcpy(peer, app.pcPeer, 6);
    if (app.pcChoice == 0) {
      enterScreen(SCR_REMOTE);            // сбросит remoteMode, задаём ниже
      playerConnect(peer);
      playerScanTracks();
      if (playerTrackCount()) playerPlayTrack(0);
      else { playerUseTone(); playerPlay(); toast("НЕТ ФАЙЛОВ НА SD - ТОН", 2500); }
      app.trackCursor = 0;
      app.remoteMode = 1;                 // FULL: плеер, играем
      syncRadios();                       // поиск больше не нужен - радио под A2DP
      toast("FULL: играю", 1500);
    } else if (app.pcChoice == 1) {
      avrcInit();
      enterScreen(SCR_REMOTE);
      playerConnect(peer);
      app.remoteMode = 2;                 // CTRL: AVRCP-пульт
      syncRadios();
      toast("CTRL: управление", 1500);
    } else if (app.pcChoice == 2) {
      enterScreen(SCR_PCREMOTE);          // ПК: оставляем SPP как есть
    } else {
      pcRemoteDisconnect();               // ОТКАЗ
      enterScreen(app.pcBack);
    }
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    pcRemoteDisconnect();
    enterScreen(app.pcBack);
  }
}

static void drawPcPopupScreen() {
  u8g2.clearBuffer();
  char nm[16];
  int16_t idx = classicLookupByAddr(app.pcPeer);
  if (idx >= 0) classicLabel(&classics[idx], nm, 12);
  else snprintf(nm, sizeof(nm), "%02X:%02X:%02X", app.pcPeer[3], app.pcPeer[4], app.pcPeer[5]);
  char title[32];
  snprintf(title, sizeof(title), "ВХОД: %s", nm);
  drawHeader(title, "BT");

  uint8_t rows = listRows();
  for (uint8_t r = 0; r < rows && r < PC_POPUP_COUNT; r++) {
    drawRow(r, PC_POPUP_ROWS[r], "", r == app.pcChoice);
  }
  drawFooter("OK-выбор  BACK-отказ");
  u8g2.sendBuffer();
}

// Текст с ПК: на весь экран крупным шрифтом (3-6 символов влезает легко).
// Рисуем ТОЛЬКО его (базовый экран не рисуем) - поэтому не мигает.
static void drawPcOverlay() {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(FONT_BIG);

  int asc = u8g2.getAscent();
  int desc = u8g2.getDescent();
  int lineH = asc + desc + 2;
  uint8_t maxLines = (uint8_t)(SCREEN_H / lineH);
  if (maxLines < 1) maxLines = 1;
  if (maxLines > 3) maxLines = 3;

  char lines[4][48];
  uint8_t nLines = 0;
  size_t i = 0;
  while (app.pcMsg[i] && nLines < maxLines) {
    size_t k = 0;
    lines[nLines][0] = 0;
    while (app.pcMsg[i]) {
      size_t beforeChar = i, kBefore = k;
      uint8_t c = (uint8_t)app.pcMsg[i];
      size_t adv = 1;
      if ((c & 0xE0) == 0xC0) adv = 2;
      else if ((c & 0xF0) == 0xE0) adv = 3;
      else if ((c & 0xF8) == 0xF0) adv = 4;
      for (size_t j = 0; j < adv && app.pcMsg[i] && k + 1 < sizeof(lines[0]); j++) lines[nLines][k++] = app.pcMsg[i++];
      lines[nLines][k] = 0;
      if (u8g2.getUTF8Width(lines[nLines]) > SCREEN_W - 4 && kBefore > 0) {
        k = kBefore;
        lines[nLines][k] = 0;
        i = beforeChar;
        break;
      }
    }
    nLines++;
  }
  if (nLines == 0) nLines = 1;

  int totalH = nLines * lineH;
  int y0 = (SCREEN_H - totalH) / 2 + asc;
  for (uint8_t r = 0; r < nLines; r++) {
    int w = u8g2.getUTF8Width(lines[r]);
    u8g2.drawUTF8((SCREEN_W - w) / 2, y0 + r * lineH, lines[r]);
  }
  u8g2.sendBuffer();
}

// ==================== ДИСПЕТЧЕР ОТРИСОВКИ ====================
static void drawMenuScreen() {
  u8g2.clearBuffer();
  drawHeader("UBPD", batteryLabel());
  uint8_t rows = listRows();
  if (app.menuCursor < app.menuTop) app.menuTop = app.menuCursor;
  if (app.menuCursor >= app.menuTop + rows) app.menuTop = app.menuCursor - rows + 1;
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = app.menuTop + r;
    if (i >= MENU_COUNT) break;
    drawRow(r, MENU_ITEMS[i], "", i == app.menuCursor);
  }
  drawFooter(app.funRunning ? "* BLE FUN в фоне" : "HOLD OK-вибро");
  u8g2.sendBuffer();
}

void drawCurrentScreen() {
  switch (app.screen) {
    case SCR_MENU: drawMenuScreen(); break;
    case SCR_RADAR: drawRadarScreen(); break;
    case SCR_SCANNERS: drawScannersScreen(); break;
    case SCR_SCANNER: drawScannerScreen(); break;
    case SCR_BLESCAN: drawBleScanScreen(); break;
    case SCR_DEVICE: drawDeviceScreen(); break;
    case SCR_REMOTE: drawRemoteScreen(); break;
    case SCR_WATCH: drawWatchScreen(); break;
    case SCR_IDENTITY: drawIdentityScreen(); break;
    case SCR_BLEFUN: drawBleFunScreen(); break;
    case SCR_TIMED: drawTimedScreen(); break;
    case SCR_TYPE: drawTypeScreen(); break;
    case SCR_CLASSIC: drawClassicScreen(); break;
    case SCR_CLASSICDEV: drawClassicDevScreen(); break;
    case SCR_PCREMOTE: drawPcRemoteScreen(); break;
    case SCR_PCPOPUP: drawPcPopupScreen(); break;
    case SCR_SLEEP: drawSleepScreen(); break;
    case SCR_SETTINGS: drawSettingsScreen(); break;
    case SCR_MANAGER: drawManagerScreen(); break;
    case SCR_BGSET: drawBgSetScreen(); break;
    case SCR_WEB: drawWebScreen(); break;
    case SCR_DIAG: drawDiagScreen(); break;
    default: break;
  }
}
