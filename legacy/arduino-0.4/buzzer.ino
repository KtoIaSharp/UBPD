#include "config.h"

// ==================== ПИЩАЛКА ====================
// Тип и схема переключаются НА УСТРОЙСТВЕ (SETTINGS, сохраняется в NVS),
// перекомпиляция не нужна:
//
//   ТИП:   АКТИВНЫЙ - внутри свой генератор: даём постоянку (HIGH/LOW);
//          ПАССИВНЫЙ - пьезо/динамик: нужна частота, используем tone().
//   СХЕМА: 3.3V (HI) - "+" буззера на GPIO27, "-" на GND (звук = HIGH);
//          5V (LOW)  - "+" на 5V/VBUS, "-" на GPIO27, и в тишине пин уходит
//                      в воздух (high-Z), чтобы ток вообще не тёк.
//
// Сигналы в работе: 1 - своё устройство появилось, 2 - исчезло,
// 3 - найдено с очень сильным сигналом. Проверка железа: HOLD OK в меню,
// кнопка "тест" в WEB UI или 'p' в Serial.

#if BUZZER_ENABLED
static uint8_t beepsLeft = 0;
static bool beepActive = false;
static uint32_t beepAt = 0;
#endif

static void buzzerHardwareOn() {
#if BUZZER_ENABLED
  if (app.buzzerPassive) {
    tone(BUZZER_PIN, BUZZER_FREQ);
  } else if (app.buzzerInvert) {
    pinMode(BUZZER_PIN, OUTPUT);     // 5-вольтовая схема: тянем "-" на землю
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH);  // 3.3 В схема: "+" на пине
  }
#endif
}

static void buzzerHardwareOff() {
#if BUZZER_ENABLED
  if (app.buzzerPassive) {
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
  } else if (app.buzzerInvert) {
    pinMode(BUZZER_PIN, INPUT);      // high-Z: ток через буззер не идёт = тишина
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
#endif
}

void buzzerInit() {
#if BUZZER_ENABLED
#if BUZZER_ACTIVE
  Serial.printf("[i] Пищалка: по умолчанию активный буззер на GPIO%u\n", BUZZER_PIN);
#else
  Serial.printf("[i] Пищалка: по умолчанию пассивный на GPIO%u, %u Гц\n", BUZZER_PIN, BUZZER_FREQ);
#endif
#endif
}

// Держим пин в тишине - вызывается после загрузки настроек и при их смене.
void buzzerApplyIdle() {
#if BUZZER_ENABLED
  buzzerHardwareOff();
#endif
}

void buzzerPlay(uint8_t count) {
#if BUZZER_ENABLED
  if (!app.buzzerOn || count == 0) return;
  beepsLeft = count;
  beepAt = millis();
#if BUZZER_DEBUG
  Serial.printf("[buzzer] Play(%u) t=%lu\n", (unsigned)count, (unsigned long)millis());
#endif
#else
  (void)count;
#endif
}

// Полная тишина: гасим пин и сбрасываем состояние.
void buzzerSilence() {
#if BUZZER_ENABLED
  buzzerHardwareOff();
  beepsLeft = 0;
  beepActive = false;
#endif
}

// Быстрая проверка железа: 3 сигнала независимо от настроек и режима.
void buzzerTest() {
#if BUZZER_ENABLED
  buzzerHardwareOff();   // страховка: если пин почему-то висел высоким
  beepActive = false;
  beepsLeft = 3;
  beepAt = millis();
  Serial.printf("[i] ТЕСТ ПИЩАЛКИ: 3 сигнала (%s, схема %s)\n",
                app.buzzerPassive ? "пассивный/частота" : "активный/постоянка",
                app.buzzerInvert ? "5V-LOW" : "3.3V-HIGH");
#else
  Serial.println("[i] ТЕСТ ПИЩАЛКИ: нечего проверять - BUZZER_ENABLED 0 в config.h");
#endif
}

void buzzerTick() {
#if BUZZER_ENABLED
  uint32_t now = millis();

  // Страховки: пищалка выключена в настройках или счётчик повреждён -
  // пин обязан быть в тишине.
  if (!app.buzzerOn && !beepActive) {
    if (beepsLeft) { beepsLeft = 0; buzzerHardwareOff(); }
  }
  if (beepsLeft > 10) beepsLeft = 3;

  if ((int32_t)(now - beepAt) < 0) return;
  if (beepActive) {
    buzzerHardwareOff();
    beepActive = false;
    beepAt = now + BUZZER_GAP_MS;
    return;
  }
  if (!beepsLeft) return;
  beepsLeft--;
  buzzerHardwareOn();
  beepActive = true;
  beepAt = now + BUZZER_ON_MS;
#if BUZZER_DEBUG
  Serial.printf("[buzzer] ON beepsLeft=%u t=%lu\n", (unsigned)beepsLeft, (unsigned long)now);
#endif
#endif
}
