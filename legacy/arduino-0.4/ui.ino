#include "config.h"

// ==================== ДАННЫЕ ЭКРАНОВ ====================
const char *const MENU_ITEMS[] = {
  "RADAR", "SCANNER", "CLASSIC BT", "CLEAR SCAN", "DEVICE TYPE", "WATCH",
  "TIMED SCAN", "SLEEP WATCH", "BLE FUN", "IDENTITY", "REMOTE", "WEB UI",
  "MANAGER", "DIAGNOSTICS"
};
const uint8_t MENU_COUNT = 14;

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
  app.sortMode = SORT_RSSI;
  app.diagPage = 0;
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
  if (s == SCR_SCANNER || s == SCR_WATCH || s == SCR_TYPE || s == SCR_CLEAR) return true;
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
  if (!wantScan && app.screen != SCR_CLASSIC && bgAllows(BG_WATCHMON) && favCount() > 0 && !app.sleeping) wantScan = true;

  if (app.screen == SCR_CLASSIC) {
    // BLE и классика делят один радиомодуль: пока идёт классический поиск,
    // BLE-скан молчит, и наоборот.
    classicScanStart();
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
  if (app.screen == SCR_IDENTITY) wantAdv = identityAdvName();
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
    case SCR_SCANNER:
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
  drawCurrentScreen();
}

void screenTick() {
  uint32_t now = millis();

  if (app.screen != SCR_CLASSIC &&
      (app.screen == SCR_RADAR || app.screen == SCR_SCANNER || app.screen == SCR_WATCH ||
       app.screen == SCR_TYPE || app.screen == SCR_CLEAR || app.screen == SCR_WEB ||
       (bgAllows(BG_WATCHMON) && favCount() > 0))) {
    if (now - lastListRefresh >= 1000) {
      lastListRefresh = now;
      bleScanRecycle();
      buildSortedDeviceList();
      buildClearList();
      forgetStaleDevices();
    }
  }
  if (app.screen == SCR_SCANNER || app.screen == SCR_TYPE) {
    int16_t i = app.selValid ? scanOrderIndexOf(app.selAddr) : -1;
    if (i >= 0) app.listCursor = (uint8_t)i;
    clampList();
  } else if (app.screen == SCR_CLEAR) {
    int16_t i = app.selValid ? clearOrderIndexOf(app.selAddr) : -1;
    if (i >= 0) app.listCursor = (uint8_t)i;
    clampClearList();
  } else if (app.screen == SCR_CLASSIC) {
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
    case SCR_SCANNER: handleScanner(ev); break;
    case SCR_DEVICE: handleDevice(ev); break;
    case SCR_WATCH: handleWatch(ev); break;
    case SCR_IDENTITY: handleIdentity(ev); break;
    case SCR_BLEFUN: handleBleFun(ev); break;
    case SCR_TIMED: handleTimed(ev); break;
    case SCR_TYPE: handleType(ev); break;
    case SCR_CLEAR: handleClear(ev); break;
    case SCR_CLASSIC: handleClassic(ev); break;
    case SCR_CLASSICDEV: handleClassicDev(ev); break;
    case SCR_SLEEP: handleSleep(ev); break;
    case SCR_SETTINGS: handleSettings(ev); break;
    case SCR_MANAGER: handleManager(ev); break;
    case SCR_WEB: handleWeb(ev); break;
    case SCR_REMOTE:
      if (ev == EV_BACK || ev == EV_BACK_LONG) enterScreen(SCR_MENU);
      break;
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
    toast("ТЕСТ ПИЩАЛКИ", 1200);
  } else if (ev == EV_OK) {
    openMenuItem();
  }
}

static void openMenuItem() {
  Screen s = SCR_MENU;
  switch (app.menuCursor) {
    case 0: s = SCR_RADAR; break;
    case 1: s = SCR_SCANNER; break;
    case 2: s = SCR_CLASSIC; break;
    case 3: s = SCR_CLEAR; break;
    case 4: s = SCR_TYPE; break;
    case 5: s = SCR_WATCH; break;
    case 6: s = SCR_TIMED; break;
    case 7: s = SCR_SLEEP; break;
    case 8: s = SCR_BLEFUN; break;
    case 9: s = SCR_IDENTITY; break;
    case 10: s = SCR_REMOTE; break;
    case 11: s = SCR_WEB; break;
    case 12: s = SCR_MANAGER; break;
    case 13: s = SCR_DIAG; break;
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

// ==================== SCANNER ====================
void handleScanner(uint8_t ev) {
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
    openDeviceScreen(scanOrder[app.listCursor], SCR_SCANNER);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawScannerScreen() {
  u8g2.clearBuffer();
  char right[8];
  snprintf(right, sizeof(right), "%u", (unsigned)scanOrderCount);
  drawHeader("SCANNER", right);

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
  drawFooter("OK-инфо BACK-меню");
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
    txtCenter(rowBaseline(2), "WATCH в SCANNER");
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
  "ПИЩАЛКА", "ТИП ПИЩАЛКИ", "СХЕМА ПИЩ.", "СИЛЬНЫЙ RSSI", "СОН: ИНТЕРВАЛ",
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
      toast("ТЕСТ ПИЩАЛКИ", 1500);
    } else if (app.settingsCursor == 7) {
      prefs.clear();
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
        toast(app.buzzerPassive ? "ПАССИВНЫЙ" : "АКТИВНЫЙ", 1500);
        buzzerTest();
        break;
      case 2:
        app.buzzerInvert = !app.buzzerInvert;
        buzzerApplyIdle();
        toast(app.buzzerInvert ? "5V СХЕМА" : "3.3V СХЕМА", 1500);
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
        snprintf(right, sizeof(right), app.buzzerPassive ? "ПАССИВ" : "АКТИВ");
        break;
      case 2:
        snprintf(right, sizeof(right), app.buzzerInvert ? "5V (LOW)" : "3.3V (HI)");
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

// ==================== CLEAR SCAN ====================
// Отдельный режим: показывает только телефоны, наушники/гарнитуры, колонки и
// микрофоны. Остальное (маячки, трекеры, часы, датчики) отфильтровано.
// Метки: PH - телефон, HP - наушники, SP - колонка, MIC - микрофон,
// "?" рядом с меткой = догадка по производителю, а не заявление устройства.

void clampClearList() {
  uint8_t n = clearCount;
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

void handleClear(uint8_t ev) {
  if (ev == EV_UP || ev == EV_DOWN) {
    if (!clearCount) return;
    int16_t idx = app.selValid ? clearOrderIndexOf(app.selAddr) : (int16_t)app.listCursor;
    if (idx < 0) idx = (int16_t)app.listCursor;
    if (ev == EV_UP) {
      if (idx > 0) idx--;
    } else {
      if (idx + 1 < (int16_t)clearCount) idx++;
    }
    app.listCursor = (uint8_t)idx;
    memcpy(app.selAddr, devices[clearOrder[idx]].addr, 6);
    app.selValid = true;
    clampClearList();
  } else if (ev == EV_OK || ev == EV_OK_LONG) {
    if (!clearCount) return;
    openDeviceScreen(clearOrder[app.listCursor], SCR_CLEAR);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

static void drawClearScreen() {
  u8g2.clearBuffer();
  char right[10];
  snprintf(right, sizeof(right), "%u", (unsigned)clearCount);
  drawHeader("CLEAR SCAN", right);

  uint8_t rows = listRows();
  for (uint8_t r = 0; r < rows; r++) {
    uint8_t i = app.listTop + r;
    if (i >= clearCount) break;
    int16_t di = clearOrder[i];
    char tag[6];
    kindTag(devices[di].kind, devices[di].kindConf, tag, sizeof(tag));
    char nm[20];
    deviceLabel(&devices[di], nm, 11);
    char left[22];
    snprintf(left, sizeof(left), "%s %s", tag, nm);
    char rr[8];
    snprintf(rr, sizeof(rr), "%d", devices[di].rssi);
    bool sel = app.selValid && memcmp(devices[di].addr, app.selAddr, 6) == 0;
    drawRow(r, left, rr, sel);
  }
  if (!clearCount) {
    u8g2.setFont(FONT_BODY);
    txtCenter(rowBaseline(1), "пока ничего");
    txtCenter(rowBaseline(2), "телефоны/наушники/");
    txtCenter(rowBaseline(3), "колонки/микрофоны");
  }
  drawFooter("PH/HP/SP/MIC  ?=догадка");
  u8g2.sendBuffer();
}

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
    enterScreen(SCR_CLASSICDEV);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}

void handleClassicDev(uint8_t ev) {
  if (ev == EV_OK || ev == EV_OK_LONG) {
    if (app.classicSel >= 0) toggleFavoriteClassic(app.classicSel);
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_CLASSIC);
  }
}

static void drawClassicScreen() {
  u8g2.clearBuffer();
  char right[10];
  snprintf(right, sizeof(right), "%u%s", (unsigned)classicCount, classicScanning ? "*" : "");
  drawHeader("CLASSIC BT", right);

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
// Здесь живёт всё служебное: права функций в фоне (4 состояния), точка доступа,
// сортировка списков и вход в настройки. Сами функции фона не имеют -
// они работают только в своей вкладке, пока MANAGER не разрешит иначе.

static const char *const MANAGER_ROWS[] = {
  "ФОН: BLE FUN", "ФОН: IDENTITY", "ФОН: WATCH", "ФОН: TIMED SCAN",
  "ФОН: WEB UI", "ТОЧКА ДОСТУПА", "СОРТИРОВКА", "НАСТРОЙКИ", "СБРОС НАСТРОЕК"
};
#define MANAGER_ROWS_COUNT 9
#define MGR_SORT_ROW 6
#define MGR_SETTINGS_ROW 7
#define MGR_RESET_ROW 8

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
      prefs.clear();
      storageLoad();
      buzzerApplyIdle();
      u8g2.setContrast(brightness);
      toast("НАСТРОЙКИ СБРОШЕНЫ", 2000);
    }
  } else if (ev == EV_OK) {
    if (app.mgrCursor < BG_COUNT) {
      uint8_t st = (uint8_t)((app.bg[app.mgrCursor] + 1) % 4);
      app.bg[app.mgrCursor] = st;
      storageSaveSettings();
      syncRadios();   // права поменялись - сразу применяем
      toast(bgStateName(st), 1200);
    } else if (app.mgrCursor == 5) {          // точка доступа
      if (webRunning) webStop();
      else webInit();
      storageSaveSettings();
    } else if (app.mgrCursor == MGR_SORT_ROW) {
      app.sortMode = (uint8_t)((app.sortMode + 1) % 3);
      storageSaveSettings();
      buildSortedDeviceList();
      buildClearList();
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
    if (i < BG_COUNT) snprintf(right, sizeof(right), "%s", bgStateName(app.bg[i]));
    else if (i == 5) snprintf(right, sizeof(right), "%s", webRunning ? "ВКЛ" : "ВЫКЛ");
    else if (i == MGR_SORT_ROW) snprintf(right, sizeof(right), "%s", sortName(app.sortMode));
    else if (i == MGR_RESET_ROW) snprintf(right, sizeof(right), "HOLD");
    drawRow(r, MANAGER_ROWS[i], right, i == app.mgrCursor);
  }
  drawFooter("OK-сменить  HOLD-сброс");
  u8g2.sendBuffer();
}

// ==================== ЗАГЛУШКИ ====================
static void drawRemoteScreen() {
  drawTextScreen("REMOTE (AVRCP)", "",
                 "AVRCP - только classic\n"
                 "Bluetooth, а на ESP32 он\n"
                 "не живёт вместе с BLE+\n"
                 "Wi-Fi. План: режим v0.3",
                 "только для СВОИХ");
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
    snprintf(l, sizeof(l), "http://%s", WiFi.softAPIP().toString().c_str());
    txt(2, rowBaseline(2), l);
    snprintf(l, sizeof(l), "клиентов: %u", (unsigned)WiFi.softAPgetStationNum());
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
  drawFooter(app.funRunning ? "* BLE FUN в фоне" : "HOLD OK-пищалка");
  u8g2.sendBuffer();
}

void drawCurrentScreen() {
  switch (app.screen) {
    case SCR_MENU: drawMenuScreen(); break;
    case SCR_RADAR: drawRadarScreen(); break;
    case SCR_SCANNER: drawScannerScreen(); break;
    case SCR_DEVICE: drawDeviceScreen(); break;
    case SCR_REMOTE: drawRemoteScreen(); break;
    case SCR_WATCH: drawWatchScreen(); break;
    case SCR_IDENTITY: drawIdentityScreen(); break;
    case SCR_BLEFUN: drawBleFunScreen(); break;
    case SCR_TIMED: drawTimedScreen(); break;
    case SCR_TYPE: drawTypeScreen(); break;
    case SCR_CLEAR: drawClearScreen(); break;
    case SCR_CLASSIC: drawClassicScreen(); break;
    case SCR_CLASSICDEV: drawClassicDevScreen(); break;
    case SCR_SLEEP: drawSleepScreen(); break;
    case SCR_SETTINGS: drawSettingsScreen(); break;
    case SCR_MANAGER: drawManagerScreen(); break;
    case SCR_WEB: drawWebScreen(); break;
    case SCR_DIAG: drawDiagScreen(); break;
    default: break;
  }
}
