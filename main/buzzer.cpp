#include "config.h"

#include <driver/ledc.h>

// ==================== ВИБРОМОТОР (haptic вместо пищалки) ====================
// На GPIO27 - вибро-модуль с PWM-входом. Управляем ШИМ (LEDC): частота ~20 кГц
// (выше слышимого - мотор не пищит), скважность = сила вибрации.
//
// Раньше тут был буззер (LEDC-частота / GPIO). Теперь это чистый ШИМ.
// Настройки на устройстве (SETTINGS, NVS): ВИБРО вкл/выкл, СИЛА (слабо/сильно),
// СХЕМА (норма/инверсия).
//
// События: 1 - своё устройство появилось, 2 - исчезло, 3 - очень сильный сигнал.
// Проверка: HOLD OK в меню, кнопка «тест» в WEB UI или 'p' в Serial.

#define H_MODE LEDC_LOW_SPEED_MODE
#define H_TIMER LEDC_TIMER_0
#define H_CH LEDC_CHANNEL_0
#define H_RES_BITS 10
#define H_MAX ((1 << H_RES_BITS) - 1)
#define H_DUTY_STRONG 800
#define H_DUTY_WEAK 300

#if BUZZER_ENABLED
static uint8_t beepsLeft = 0;
static bool beepActive = false;
static uint32_t beepAt = 0;
static bool ledcReady = false;

static void ledcEnsure() {
  if (ledcReady) return;
  ledc_timer_config_t t = {};
  t.speed_mode = H_MODE;
  t.duty_resolution = (ledc_timer_bit_t)H_RES_BITS;
  t.timer_num = H_TIMER;
  t.freq_hz = BUZZER_FREQ;
  t.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&t) != ESP_OK) {
    setError("haptic timer failed");
    return;
  }
  ledc_channel_config_t c = {};
  c.gpio_num = BUZZER_PIN;
  c.speed_mode = H_MODE;
  c.channel = H_CH;
  c.timer_sel = H_TIMER;
  c.duty = 0;
  c.hpoint = 0;
  c.intr_type = LEDC_INTR_DISABLE;
  if (ledc_channel_config(&c) != ESP_OK) {
    setError("haptic channel failed");
    return;
  }
  ledcReady = true;
}

static void hapticSetDuty(uint32_t d) {
  if (!ledcReady) return;
  ledc_set_duty(H_MODE, H_CH, d);
  ledc_update_duty(H_MODE, H_CH);
}

static void hapticOn() {
  ledcEnsure();
  uint32_t d = app.buzzerPassive ? H_DUTY_WEAK : H_DUTY_STRONG;
  hapticSetDuty(app.buzzerInvert ? 0 : d);          // инверсия: активный низкий
}

static void hapticOff() {
  if (!ledcReady) return;
  hapticSetDuty(app.buzzerInvert ? H_MAX : 0);
}
#endif

void buzzerInit() {
#if BUZZER_ENABLED
  Serial.printf("[i] ВИБРО: модуль на GPIO%u, ШИМ %u Гц\n", BUZZER_PIN, BUZZER_FREQ);
#endif
}

// Держим мотор в покое - после загрузки настроек и при их смене.
void buzzerApplyIdle() {
#if BUZZER_ENABLED
  hapticOff();
#endif
}

void buzzerPlay(uint8_t count) {
#if BUZZER_ENABLED
  if (!app.buzzerOn || count == 0) return;
  beepsLeft = count;
  beepAt = millis();
#else
  (void)count;
#endif
}

// Полная тишина: гасим мотор и сбрасываем состояние.
void buzzerSilence() {
#if BUZZER_ENABLED
  hapticOff();
  beepsLeft = 0;
  beepActive = false;
#endif
}

// Быстрая проверка: 3 импульса независимо от настроек.
void buzzerTest() {
#if BUZZER_ENABLED
  hapticOff();
  beepActive = false;
  beepsLeft = 3;
  beepAt = millis();
  Serial.printf("[i] ТЕСТ ВИБРО: 3 импульса (%s, %s)\n",
                app.buzzerPassive ? "слабо" : "сильно",
                app.buzzerInvert ? "инверсия" : "норма");
#else
  Serial.println("[i] ТЕСТ ВИБРО: нечего проверять - BUZZER_ENABLED 0 в config.h");
#endif
}

void buzzerTick() {
#if BUZZER_ENABLED
  uint32_t now = millis();

  if (!app.buzzerOn && !beepActive) {
    if (beepsLeft) { beepsLeft = 0; hapticOff(); }
  }
  if (beepsLeft > 10) beepsLeft = 3;

  if ((int32_t)(now - beepAt) < 0) return;
  if (beepActive) {
    hapticOff();
    beepActive = false;
    beepAt = now + BUZZER_GAP_MS;
    return;
  }
  if (!beepsLeft) return;
  beepsLeft--;
  hapticOn();
  beepActive = true;
  beepAt = now + BUZZER_ON_MS;
#endif
}
