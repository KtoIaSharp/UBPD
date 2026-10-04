#include "config.h"

// NVS (Preferences) — энергонезависимое хранилище:
// избранное, имя, режимы, настройки таймера.
// SD-карта в v0.1 не используется: она появится позже как доп. хранилище.

void storageSaveFavorites() {
  prefs.putBytes("favs", favorites, sizeof(favorites));
}

void storageSaveSettings() {
  prefs.putUChar("identIdx", app.identIdx);
  prefs.putString("custom", app.customNameStr);
  prefs.putUChar("radarSrc", app.radarSource);
  prefs.putUChar("radarSwp", app.radarSweep ? 1 : 0);
  prefs.putUChar("funMode", app.funMode);
  prefs.putUChar("bright", brightness);
  prefs.putUShort("tInt", app.timedInterval);
  prefs.putUShort("tDur", app.timedDuration);
  prefs.putUShort("tRun", app.timedRuns);
  prefs.putUChar("buzzer", app.buzzerOn ? 1 : 0);
  prefs.putUChar("buzzerInv", app.buzzerInvert ? 1 : 0);
  prefs.putUChar("buzzerPas", app.buzzerPassive ? 1 : 0);
  prefs.putChar("strong", app.strongRssi);
  prefs.putUShort("sleepInt", app.sleepInterval);
  prefs.putUChar("apOpen", app.apOpen ? 1 : 0);
  prefs.putBytes("bg", app.bg, sizeof(app.bg));
  prefs.putUChar("sort", app.sortMode);
}

void storageLoad() {
  prefs.begin("ubpd", false);

  app.identIdx = prefs.getUChar("identIdx", app.identIdx);
  String custom = prefs.getString("custom", app.customNameStr);
  snprintf(app.customNameStr, sizeof(app.customNameStr), "%s", custom.c_str());

  app.radarSource = prefs.getUChar("radarSrc", app.radarSource);
  app.radarSweep = prefs.getUChar("radarSwp", app.radarSweep ? 1 : 0) != 0;
  app.funMode = prefs.getUChar("funMode", app.funMode);
  brightness = prefs.getUChar("bright", brightness);
  app.timedInterval = prefs.getUShort("tInt", app.timedInterval);
  app.timedDuration = prefs.getUShort("tDur", app.timedDuration);
  app.timedRuns = prefs.getUShort("tRun", app.timedRuns);
  app.buzzerOn = prefs.getUChar("buzzer", app.buzzerOn ? 1 : 0) != 0;
  app.buzzerInvert = prefs.getUChar("buzzerInv", app.buzzerInvert ? 1 : 0) != 0;
  app.buzzerPassive = prefs.getUChar("buzzerPas", app.buzzerPassive ? 1 : 0) != 0;
  app.strongRssi = prefs.getChar("strong", app.strongRssi);
  app.sleepInterval = prefs.getUShort("sleepInt", app.sleepInterval);
  app.apOpen = prefs.getUChar("apOpen", app.apOpen ? 1 : 0) != 0;

  if (prefs.getBytesLength("bg") == sizeof(app.bg)) {
    prefs.getBytes("bg", app.bg, sizeof(app.bg));
    for (uint8_t i = 0; i < BG_COUNT; i++) {
      if (app.bg[i] > BG_BG_SLEEP) app.bg[i] = BG_BG;
    }
  }
  app.sortMode = prefs.getUChar("sort", app.sortMode);
  if (app.sortMode > SORT_TYPE) app.sortMode = SORT_RSSI;

  if (app.identIdx > 7) app.identIdx = 0;
  if (app.radarSource > RS_WIFI) app.radarSource = RS_BLE;
  if (app.funMode > 2) app.funMode = 0;
  if (app.timedInterval < 10 || app.timedInterval > 600) app.timedInterval = 60;
  if (app.timedDuration < 5 || app.timedDuration > 60) app.timedDuration = 10;
  if (app.timedRuns < 1 || app.timedRuns > 100) app.timedRuns = 20;
  if (app.strongRssi > -20 || app.strongRssi < -100) app.strongRssi = -60;
  if (app.sleepInterval < 15 || app.sleepInterval > 300) app.sleepInterval = 30;

  if (prefs.getBytesLength("favs") == sizeof(favorites)) {
    prefs.getBytes("favs", favorites, sizeof(favorites));
    for (int i = 0; i < MAX_FAVS; i++) {
      if (favorites[i].used) favorites[i].name[DEV_NAME_LEN - 1] = 0;
    }
  } else {
    memset(favorites, 0, sizeof(favorites));
  }

  Serial.printf("[i] NVS: имя idx=%u, радар=%s, избранных=%d\n",
                (unsigned)app.identIdx, app.radarSource == RS_BLE ? "BLE" : "WI-FI", favCount());
}

int favCount() {
  int n = 0;
  for (int i = 0; i < MAX_FAVS; i++) {
    if (favorites[i].used) n++;
  }
  return n;
}
