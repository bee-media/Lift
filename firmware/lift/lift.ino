// Учебный лифт на ESP32-S3.
// Плата: ESP32S3 Dev Module, ядро esp32 3.x. Библиотека: Adafruit NeoPixel (1.12.4 или новее).
// Подробности в docs/README.md, схемы подключения в docs/wiring.md.
#include "config.h"
#include "dispatcher.h"
#include "hardware.h"
#include "lift_ctrl.h"
#include "web_server.h"

static void controlTask(void*) {
  uint32_t last = micros();
  for (;;) {
    uint32_t now = micros();
    float dt = (now - last) * 1e-6f;
    last = now;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.0005f) dt = 0.0005f;
    lift.tick(dt);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  HW::begin();
  lift.begin();
  // Контроль движения в отдельной задаче, чтобы веб и Wi-Fi не мешали.
  xTaskCreatePinnedToCore(controlTask, "lift", 8192, nullptr, 3, nullptr, 1);
  netBegin();
  webBegin();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  if (apMode) dnsServer.processNextRequest();
  if (g_apRestartAt && (int32_t)(millis() - g_apRestartAt) > 0) {  // смена пароля точки доступа
    g_apRestartAt = 0;
    startAp();
  }
  delay(2);
}
