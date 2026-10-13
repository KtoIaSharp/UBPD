#include "config.h"

// ==================== РАЗМЕТКА ЭКРАНА ====================
// Метрики берём из шрифта, чтобы разметка не ломалась при смене шрифта.
static uint8_t g_asc = 7, g_desc = 2;
static int16_t g_hdrH = 12, g_rowH = 11, g_rowY0 = 20, g_lineY = 53, g_footY = 61;
static uint8_t g_rows = 4;

void layoutCompute() {
  u8g2.setFont(FONT_BODY);
  g_asc = u8g2.getAscent();
  g_desc = u8g2.getDescent();
  if (g_asc < 5 || g_asc > 12) g_asc = 7;
  if (g_desc > 4) g_desc = 2;
  g_hdrH = g_asc + g_desc + 3;
  g_footY = SCREEN_H - 3;
  g_lineY = g_footY - g_asc - 2;
  g_rowH = g_asc + g_desc + 2;
  g_rowY0 = g_hdrH + 1 + g_asc;
  int16_t avail = g_lineY - (g_rowY0 - g_asc) - 1;
  g_rows = avail / g_rowH;
  if (g_rows < 2) g_rows = 2;
  if (g_rows > 6) g_rows = 6;
}

uint8_t listRows() { return g_rows; }
int16_t rowTop(uint8_t row) { return g_rowY0 + row * g_rowH - g_asc - 1; }
int16_t rowBaseline(uint8_t row) { return g_rowY0 + row * g_rowH; }

void displayInit() {
  Wire.begin(OLED_SDA, OLED_SCL, 400000);
  if (!u8g2.begin()) {
    Serial.println("[!] OLED не отвечает: проверь SDA=21, SCL=22, питание 3V3 и адрес 0x3C");
  }
  u8g2.setBusClock(400000);
  u8g2.setContrast(brightness);
  u8g2.setFont(FONT_BODY);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  layoutCompute();
  Serial.printf("[i] Display: %ux%u rows=%u\n", SCREEN_W, SCREEN_H, g_rows);
}

void splashScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(FONT_HEAD);
  txtCenter(24, "UBPD");
  u8g2.setFont(FONT_BODY);
  txtCenter(40, "UNKNOWN BLUETOOTH");
  txtCenter(50, "POCKET DEVICE");
  txtCenter(62, "v" UBPD_VERSION);
  u8g2.sendBuffer();
  ledPulse(200);
  delay(1500);
}

// ==================== ПРИМИТИВЫ ====================
void txt(int16_t x, int16_t y, const char *s) { u8g2.drawUTF8(x, y, s); }

int16_t txtW(const char *s) { return (int16_t)u8g2.getUTF8Width(s); }

void txtCenter(int16_t y, const char *s) {
  u8g2.drawUTF8((SCREEN_W - txtW(s)) / 2, y, s);
}

void toast(const char *msg, uint16_t ms) {
  snprintf(toastMsg, sizeof(toastMsg), "%s", msg);
  toastUntil = millis() + ms;
}

void drawHeader(const char *title, const char *right) {
  u8g2.setFont(FONT_BODY);
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, SCREEN_W, g_hdrH);
  u8g2.setDrawColor(0);
  txt(2, g_asc + 1, title);
  if (right && *right) txt(SCREEN_W - 2 - txtW(right), g_asc + 1, right);
  u8g2.setDrawColor(1);
}

void drawFooter(const char *hint) {
  u8g2.setFont(FONT_BODY);
  const char *t = hint;
  if (toastMsg[0] && (int32_t)(toastUntil - millis()) > 0) t = toastMsg;
  u8g2.drawHLine(0, g_lineY, SCREEN_W);
  txt(2, g_footY, t);
}

void drawRow(uint8_t row, const char *left, const char *right, bool sel) {
  int16_t y = g_rowY0 + row * g_rowH;
  if (sel) {
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, y - g_asc - 1, SCREEN_W, g_rowH - 1);
    u8g2.setDrawColor(0);
  }
  txt(2, y, left);
  if (right && *right) txt(SCREEN_W - 2 - txtW(right), y, right);
  u8g2.setDrawColor(1);
}

void drawTextScreen(const char *title, const char *right, const char *body, const char *hint) {
  u8g2.clearBuffer();
  drawHeader(title, right);
  u8g2.setFont(FONT_BODY);
  int16_t y = g_rowY0;
  const char *p = body;
  while (*p && y <= g_lineY - 2) {
    const char *nl = strchr(p, '\n');
    size_t n = nl ? (size_t)(nl - p) : strlen(p);
    char buf[64];
    if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
    memcpy(buf, p, n);
    buf[n] = 0;
    txt(2, y, buf);
    y += g_rowH;
    if (!nl) break;
    p = nl + 1;
  }
  drawFooter(hint);
  u8g2.sendBuffer();
}
