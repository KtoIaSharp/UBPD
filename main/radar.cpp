#include "config.h"

// ==================== ТОЧКИ РАДАРА ====================
// Честно: это карта силы сигнала (RSSI), а не направление.
// Правило шкалы: 2 dBm = 1 пиксель, -45 dBm в центре, -91 dBm на краю
// (в комнате реальные значения -55...-95, иначе все точки липли бы к краю).
// Угол точки - стабильный хэш адреса (не направление).

#define RADAR_R 23
#define RADAR_CX 46
#define RADAR_CY 27
#define RADAR_CENTER_DBM 45

struct RadarPoint {
  int8_t rssi;
  uint32_t hash;
  int16_t index;
  char label[16];
};

static RadarPoint pts[RADAR_PTS];
static uint8_t ptCount = 0;
static float sweepAngle = 0.0f;

static uint32_t hashBytes(const char *s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; s[i]; i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

static uint32_t hashMac(const uint8_t *a) {
  uint32_t h = 2166136261u;
  for (uint8_t i = 0; i < 6; i++) {
    h ^= a[i];
    h *= 16777619u;
  }
  return h;
}

static int16_t rssiToRadius(int8_t rssi) {
  int16_t steps = ((int16_t)(-rssi) - RADAR_CENTER_DBM) / 2;
  if (steps < 0) steps = 0;
  if (steps > RADAR_R) steps = RADAR_R;
  return steps;
}

void buildRadarPoints() {
  ptCount = 0;
  if (app.radarSource == RS_BLE) {
    for (uint8_t i = 0; i < scanOrderCount && ptCount < RADAR_PTS; i++) {
      int16_t di = scanOrder[i];
      pts[ptCount].rssi = devices[di].rssi;
      pts[ptCount].hash = hashMac(devices[di].addr);
      pts[ptCount].index = di;
      deviceLabel(&devices[di], pts[ptCount].label, 15);
      ptCount++;
    }
  } else {
    int16_t order[MAX_WIFI];
    for (uint8_t i = 0; i < wifiCount; i++) order[i] = (int16_t)i;
    for (uint8_t i = 1; i < wifiCount; i++) {
      int16_t k = order[i];
      int j = (int)i - 1;
      while (j >= 0 && wifiNets[order[j]].rssi < wifiNets[k].rssi) {
        order[j + 1] = order[j];
        j--;
      }
      order[j + 1] = k;
    }
    for (uint8_t i = 0; i < wifiCount && ptCount < RADAR_PTS; i++) {
      int16_t wi = order[i];
      pts[ptCount].rssi = wifiNets[wi].rssi;
      pts[ptCount].hash = hashBytes(wifiNets[wi].ssid);
      pts[ptCount].index = wi;
      snprintf(pts[ptCount].label, sizeof(pts[ptCount].label), "%.15s", wifiNets[wi].ssid);
      ptCount++;
    }
  }
  if (ptCount == 0) app.radarCursor = 0;
  else if (app.radarCursor >= ptCount) app.radarCursor = ptCount - 1;

#if RADAR_DEBUG
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg > 1000) {
    lastDbg = millis();
    Serial.printf("[radar] src=%s pts=%u scanOrder=%u wifi=%u\n",
                  app.radarSource == RS_BLE ? "BLE" : "WI-FI",
                  (unsigned)ptCount, (unsigned)scanOrderCount, (unsigned)wifiCount);
    for (uint8_t i = 0; i < ptCount; i++) {
      Serial.printf("   #%u %s rssi=%d r=%d x=%d y=%d\n", (unsigned)i, pts[i].label,
                    pts[i].rssi, rssiToRadius(pts[i].rssi),
                    RADAR_CX + (int16_t)(cosf((float)(pts[i].hash % 628u) / 100.0f) * rssiToRadius(pts[i].rssi)),
                    RADAR_CY + (int16_t)(sinf((float)(pts[i].hash % 628u) / 100.0f) * rssiToRadius(pts[i].rssi)));
    }
  }
#endif
}

void radarTick() {
  if (!app.radarSweep) return;
  static uint32_t lastTick = 0;
  uint32_t now = millis();
  if (now - lastTick < 40) return;  // привязка к времени, а не к частоте цикла
  lastTick = now;
  sweepAngle += 0.08f;
  if (sweepAngle > 6.2831853f) sweepAngle -= 6.2831853f;
}

void drawRadarOverlay() {
  u8g2.clearBuffer();
  drawHeader("RADAR OPT", "");
  const char *src = app.radarSource == RS_BLE ? "BLE" : "WI-FI";
  char l0[40], l1[40];
  snprintf(l0, sizeof(l0), "ИСТОЧНИК: %s", src);
  snprintf(l1, sizeof(l1), "РАЗВЁРТКА: %s", app.radarSweep ? "ВКЛ" : "ВЫКЛ");
  drawRow(0, l0, "", app.radarOpt == 0);
  drawRow(1, l1, "", app.radarOpt == 1);
  drawRow(2, "ШКАЛА: -45 центр", "", app.radarOpt == 2);
  drawRow(3, "ЗАКРЫТЬ", "", app.radarOpt == 3);
  drawFooter("OK-выбор BACK-закрыть");
  u8g2.sendBuffer();
}

void drawRadarScreen() {
  buildRadarPoints();

  if (app.radarOverlay) {
    drawRadarOverlay();
    return;
  }

  u8g2.clearBuffer();
  u8g2.setFont(FONT_BODY);
  u8g2.setDrawColor(1);

  u8g2.drawCircle(RADAR_CX, RADAR_CY, RADAR_R);
  u8g2.drawCircle(RADAR_CX, RADAR_CY, (RADAR_R * 2) / 3);
  u8g2.drawCircle(RADAR_CX, RADAR_CY, RADAR_R / 3);
  u8g2.drawDisc(RADAR_CX, RADAR_CY, 1);

  if (app.radarSweep) {
    int16_t ex = RADAR_CX + (int16_t)(cosf(sweepAngle) * RADAR_R);
    int16_t ey = RADAR_CY + (int16_t)(sinf(sweepAngle) * RADAR_R);
    u8g2.drawLine(RADAR_CX, RADAR_CY, ex, ey);
  }

  for (uint8_t i = 0; i < ptCount; i++) {
    float ang = (float)(pts[i].hash % 628u) / 100.0f;
    int16_t r = rssiToRadius(pts[i].rssi);
    int16_t x = RADAR_CX + (int16_t)(cosf(ang) * r);
    int16_t y = RADAR_CY + (int16_t)(sinf(ang) * r);
    u8g2.setDrawColor(0);
    u8g2.drawBox(x - 2, y - 2, 5, 5);  // чёрный ореол: точка видна поверх колец
    u8g2.setDrawColor(1);
    if (i == app.radarCursor) u8g2.drawFrame(x - 2, y - 2, 5, 5);
    u8g2.drawBox(x - 1, y - 1, 3, 3);
  }

  // легенда справа: не направление, только сила сигнала
  u8g2.drawUTF8(74, 10, "RADAR");
  u8g2.drawUTF8(74, 20, app.radarSource == RS_BLE ? "BLE" : "WI-FI");
  u8g2.drawUTF8(74, 30, "RSSI");
  u8g2.drawUTF8(74, 40, "MAP");
  char cnt[12];
  snprintf(cnt, sizeof(cnt), "n:%u", (unsigned)ptCount);
  u8g2.drawUTF8(74, 50, cnt);

  // строка выбранной цели
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 54, SCREEN_W, 10);
  u8g2.setDrawColor(1);
  if (ptCount) {
    char rr[8];
    snprintf(rr, sizeof(rr), "%d", pts[app.radarCursor].rssi);
    u8g2.drawUTF8(2, 62, pts[app.radarCursor].label);
    u8g2.drawUTF8(SCREEN_W - 2 - txtW(rr), 62, rr);
  } else {
    u8g2.drawUTF8(2, 62, app.radarSource == RS_BLE ? "поиск BLE..." : "поиск Wi-Fi...");
  }
  u8g2.sendBuffer();
}

void handleRadar(uint8_t ev) {
  if (app.radarOverlay) {
    if (ev == EV_UP) {
      if (app.radarOpt) app.radarOpt--;
    } else if (ev == EV_DOWN) {
      if (app.radarOpt < 3) app.radarOpt++;
    } else if (ev == EV_OK) {
      if (app.radarOpt == 0) {
        app.radarSource = (app.radarSource == RS_BLE) ? RS_WIFI : RS_BLE;
        app.radarCursor = 0;
        syncRadios();
      } else if (app.radarOpt == 1) {
        app.radarSweep = !app.radarSweep;
      } else if (app.radarOpt == 3) {
        app.radarOverlay = false;
      }
    } else if (ev == EV_BACK || ev == EV_BACK_LONG || ev == EV_OK_LONG) {
      app.radarOverlay = false;
    }
    return;
  }

  if (ev == EV_UP) {
    if (app.radarCursor) app.radarCursor--;
  } else if (ev == EV_DOWN) {
    if (ptCount && app.radarCursor + 1 < ptCount) app.radarCursor++;
  } else if (ev == EV_OK_LONG) {
    app.radarOverlay = true;
    app.radarOpt = 0;
  } else if (ev == EV_OK) {
    if (!ptCount) return;
    if (app.radarSource == RS_BLE) {
      openDeviceScreen(pts[app.radarCursor].index, SCR_RADAR);
    } else {
      char t[26];
      int16_t wi = pts[app.radarCursor].index;
      snprintf(t, sizeof(t), "%.14s ch%u %d", wifiNets[wi].ssid, (unsigned)wifiNets[wi].ch, wifiNets[wi].rssi);
      toast(t, 2500);
    }
  } else if (ev == EV_BACK || ev == EV_BACK_LONG) {
    enterScreen(SCR_MENU);
  }
}
