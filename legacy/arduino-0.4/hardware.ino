#include "config.h"

// ==================== КНОПКИ ====================
struct BtnState {
  uint8_t pin;
  bool raw;
  bool stable;
  bool longSent;
  uint32_t lastChange;
  uint32_t lastEvent;
};

static BtnState btns[B_COUNT] = {
  {BTN_UP, false, false, false, 0, 0},
  {BTN_DOWN, false, false, false, 0, 0},
  {BTN_OK, false, false, false, 0, 0},
  {BTN_BACK, false, false, false, 0, 0}
};

#define EVQ_SIZE 12
static uint8_t evq[EVQ_SIZE];
static uint8_t evqHead = 0, evqTail = 0;

void pushBtnEvent(uint8_t ev) {
  uint8_t nxt = (evqTail + 1) % EVQ_SIZE;
  if (nxt == evqHead) return;
  evq[evqTail] = ev;
  evqTail = nxt;
}

bool btnEventPop(uint8_t *ev) {
  if (evqHead == evqTail) return false;
  *ev = evq[evqHead];
  evqHead = (evqHead + 1) % EVQ_SIZE;
  return true;
}

void pollButtons() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < B_COUNT; i++) {
    bool raw = (digitalRead(btns[i].pin) == LOW);
    if (raw != btns[i].raw) {
      btns[i].raw = raw;
      btns[i].lastChange = now;
    }
    if (now - btns[i].lastChange < BTN_DEBOUNCE_MS) continue;

    if (raw && !btns[i].stable) {
      btns[i].stable = true;
      btns[i].lastEvent = now;
      btns[i].longSent = false;
      pushBtnEvent(i);
      ledPulse(20);
    } else if (!raw && btns[i].stable) {
      btns[i].stable = false;
    } else if (raw && btns[i].stable) {
      if (!btns[i].longSent && (now - btns[i].lastEvent) >= BTN_LONG_MS) {
        btns[i].longSent = true;
        if (i == B_OK) pushBtnEvent(EV_OK_LONG);
        else if (i == B_BACK) pushBtnEvent(EV_BACK_LONG);
      }
      if ((i == B_UP || i == B_DOWN) && (now - btns[i].lastEvent) >= BTN_REPEAT_MS) {
        btns[i].lastEvent = now;
        pushBtnEvent(i);
      }
    }
  }
}

void pollSerialControl() {
#if SERIAL_CONTROL
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'u': case 'U': case 'w': pushBtnEvent(EV_UP); break;
      case 'd': case 'D': case 's': pushBtnEvent(EV_DOWN); break;
      case 'o': case 'O': case 'e': case '\r': pushBtnEvent(EV_OK); break;
      case 'b': case 'B': case 'q': case 27: pushBtnEvent(EV_BACK); break;
      case 'l': case 'L': pushBtnEvent(EV_OK_LONG); break;
      case 'm': case 'M': pushBtnEvent(EV_BACK_LONG); break;
      case 'h': case 'H': printHelp(); break;
      case 'p': case 'P': buzzerTest(); break;
      default: break;
    }
  }
#endif
}

void printHelp() {
  Serial.println();
  Serial.println("--- UBPD serial ---");
  Serial.println("u / w  - UP");
  Serial.println("d / s  - DOWN");
  Serial.println("o / Enter - OK");
  Serial.println("b / Esc   - BACK");
  Serial.println("l      - OK (долгое нажатие)");
  Serial.println("m      - BACK (долгое нажатие)");
  Serial.println("h      - эта справка");
  Serial.println("p      - тест пищалки");
  Serial.println("-------------------");
}

// ==================== СВЕТОДИОД ====================
static uint32_t ledUntil = 0;

void ledPulse(uint16_t ms) {
  digitalWrite(LED_PIN, HIGH);
  ledUntil = millis() + ms;
}

void pollLed() {
  if (ledUntil && (int32_t)(ledUntil - millis()) <= 0) {
    digitalWrite(LED_PIN, LOW);
    ledUntil = 0;
  }
}

// ==================== БАТАРЕЯ ====================
float batteryVoltage() {
#if BATT_ENABLED
  uint32_t mv = analogReadMilliVolts(BATT_ADC_PIN);
  return ((float)mv * BATT_DIVIDER) / 1000.0f;
#else
  return 0.0f;
#endif
}

int batteryPercent() {
#if BATT_ENABLED
  float v = batteryVoltage();
  int p = (int)((v - BATT_V_MIN) / (BATT_V_MAX - BATT_V_MIN) * 100.0f);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
#else
  return -1;
#endif
}

static char battLabel[6] = "";

const char *batteryLabel() {
  int p = batteryPercent();
  if (p < 0) snprintf(battLabel, sizeof(battLabel), "--");
  else snprintf(battLabel, sizeof(battLabel), "%d%%", p);
  return battLabel;
}

// ==================== ТЕМПЕРАТУРА ====================
float chipTemperature() {
#if TEMP_ENABLED
  extern float temperatureRead();
  float t = temperatureRead();
  if (isnan(t) || t < -40.0f || t > 125.0f) return NAN;
  return t;
#else
  return NAN;
#endif
}

// ==================== ПРОЧЕЕ ====================
void hardwareInit() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
#if BATT_ENABLED
  analogReadResolution(12);
  analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);
#else
  pinMode(BATT_ADC_PIN, INPUT);
#endif
  memset(devices, 0, sizeof(devices));
  memset(favorites, 0, sizeof(favorites));
  memset(wifiNets, 0, sizeof(wifiNets));
}

void setError(const char *msg) {
  errorCount++;
  snprintf(lastError, sizeof(lastError), "%s", msg);
  Serial.printf("[!] %s\n", msg);
}

const char *uptimeLabel() {
  static char buf[16];
  uint32_t s = millis() / 1000;
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
  return buf;
}
