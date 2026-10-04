#include "config.h"

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);
  Serial.println();
  Serial.printf("=== UBPD v%s ===\n", UBPD_VERSION);

  hardwareInit();
  buzzerInit();
  displayInit();
  uiInit();
  storageLoad();
  buzzerApplyIdle();
  BLEDevice::init("UBPD");
  splashScreen();

#if SERIAL_CONTROL
  Serial.println("[i] Serial: u=UP d=DOWN o=OK b=BACK (или стрелки Enter/Esc)");
#endif
}

void loop() {
  pollButtons();
  pollSerialControl();
  pollLed();
  buzzerTick();
  uiLoop();
}
