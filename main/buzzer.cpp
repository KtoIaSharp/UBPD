#include "config.h"

#include <driver/ledc.h>

// ==================== ПИЩАЛКА ====================
// Тип и схема переключаются НА УСТРОЙСТВЕ (SETTINGS, сохраняется в NVS),
// перекомпиляция не нужна:
//
//   ТИП:   АКТИВНЫЙ - внутри свой генератор: даём постоянку (HIGH/LOW);
//          ПАССИВНЫЙ - пьезо/динамик: нужна частота, её даёт LEDC-таймер.
//   СХЕМА: 3.3V (HI) - "+" буззера на GPIO27, "-" на GND (звук = HIGH);
//          5V (LOW)  - "+" на 5V/VBUS, "-" на GPIO27, и в тишине пин уходит
//                      в воздух (high-Z), чтобы ток вообще не тёк.
//
// Раньше частота шла через Arduino tone(); теперь это LEDC из esp-idf
// (driver/ledc.h): явный таймер, явный канал, никакого глобального состояния
// библиотеки. Уровни и high-Z - тоже напрямую через драйвер GPIO.
//
// Сигналы в работе: 1 - своё устройство появилось, 2 - исчезло,
// 3 - найдено с очень сильным сигналом. Проверка железа: HOLD OK в меню,
// кнопка "тест" в WEB UI или 'p' в Serial.

#define BUZZER_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_DUTY_HALF 512  // 50% при 10-битном разрешении

#if BUZZER_ENABLED
static uint8_t beepsLeft = 0;
static bool beepActive = false;
static uint32_t beepAt = 0;
static bool ledcReady = false;

static void ledcEnsure() {
  if (ledcReady) return;
  ledc_timer_config_t t = {};
  t.speed_mode = BUZZER_LEDC_MODE;
  t.duty_resolution = LEDC_TIMER_10_BIT;
  t.timer_num = BUZZER_LEDC_TIMER;
  t.freq_hz = BUZZER_FREQ;
  t.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&t) != ESP_OK) {
    setError("ledc timer failed");
    return;
  }
  ledc_channel_config_t c = {};
  c.gpio_num = BUZZER_PIN;
  c.speed_mode = BUZZER_LEDC_MODE;
  c.channel = BUZZER_LEDC_CHANNEL;
  c.timer_sel = BUZZER_LEDC_TIMER;
  c.duty = 0;
  c.hpoint = 0;
  c.intr_type = LEDC_INTR_DISABLE;
  if (ledc_channel_config(&c) != ESP_OK) {
    setError("ledc channel failed");
    return;
  }
  ledcReady = true;
}

static void pinOutput(uint8_t level) {
  gpio_set_direction((gpio_num_t)BUZZER_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)BUZZER_PIN, level);
}

static void pinFloating() {
  gpio_set_direction((gpio_num_t)BUZZER_PIN, GPIO_MODE_INPUT);
}
#endif

#if BUZZER_ENABLED
static void buzzerHardwareOn() {
  if (app.buzzerPassive) {
    ledcEnsure();
    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, BUZZER_FREQ);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY_HALF);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
  } else if (app.buzzerInvert) {
    pinOutput(0);  // 5-вольтовая схема: тянем "-" на землю
  } else {
    pinOutput(1);  // 3.3 В схема: "+" на пине
  }
}

static void buzzerHardwareOff() {
  if (app.buzzerPassive) {
    ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    if (app.buzzerInvert) pinFloating();
  } else if (app.buzzerInvert) {
    pinFloating();  // high-Z: ток через буззер не идёт = тишина
  } else {
    pinOutput(0);
  }
}
#endif

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
