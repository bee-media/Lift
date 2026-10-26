// Железо: энкодер, мотор (DRV8833), расширители MCP23017, светодиоды WS2812.
// Нужно ядро esp32 версии 3.x (ledcAttach) и библиотека Adafruit NeoPixel 1.12.4 или новее.
#pragma once
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "config.h"

// ---------- Энкодер (квадратурный, x4, в прерывании) ----------
static volatile int32_t g_enc = 0;
static volatile uint8_t g_encPrev = 0;

// В прерывании нельзя трогать флеш: когда Wi-Fi или NVS пишут во флеш, кэш отключён.
// Поэтому таблица лежит в RAM (DRAM_ATTR), а входы читаются прямо из регистра GPIO.
static const DRAM_ATTR int8_t ENC_TABLE[16] = { 0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0 };

static void IRAM_ATTR encIsr() {
  uint32_t in = REG_READ(GPIO_IN_REG);
  uint8_t s = (uint8_t)((((in >> PIN_ENC_A) & 1) << 1) | ((in >> PIN_ENC_B) & 1));
  int32_t v = g_enc;
  g_enc = v + ENC_TABLE[(g_encPrev << 2) | s];
  g_encPrev = s;
}

// ---------- MCP23017 ----------
static bool mcpOk[4] = { false, false, false, false };
static const uint8_t MCP_ADDR[4] = { MCP_BTN_LO, MCP_BTN_HI, MCP_HALL_LO, MCP_HALL_HI };

static bool mcpWrite(uint8_t addr, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool mcpRead16(uint8_t addr, uint16_t& out) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)0x12);  // GPIOA, затем автоматически GPIOB
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)2) != 2) return false;
  uint8_t a = Wire.read();
  uint8_t b = Wire.read();
  out = a | ((uint16_t)b << 8);
  return true;
}

static uint32_t g_rawBtn = 0xFFFFFFFF;   // 1 = отпущено
static uint32_t g_rawHall = 0xFFFFFFFF;  // 1 = магнита нет (A3144 тянет вниз при магните)
static uint16_t g_i2cFails = 0;

// ---------- Светодиоды ----------
static Adafruit_NeoPixel g_strip(MAX_FLOORS, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);

namespace HW {

// Опрос и настройка расширителей. Можно вызывать повторно (например, после починки проводов).
static void probeExpanders() {
  for (int i = 0; i < 4; i++) {
    bool ok = mcpWrite(MCP_ADDR[i], 0x00, 0xFF);  // IODIRA: все входы
    ok = ok && mcpWrite(MCP_ADDR[i], 0x01, 0xFF);  // IODIRB
    ok = ok && mcpWrite(MCP_ADDR[i], 0x0C, 0xFF);  // GPPUA: подтяжки
    ok = ok && mcpWrite(MCP_ADDR[i], 0x0D, 0xFF);  // GPPUB
    mcpOk[i] = ok;
  }
}

static void begin() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  probeExpanders();
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  g_encPrev = (gpio_get_level((gpio_num_t)PIN_ENC_A) << 1) | gpio_get_level((gpio_num_t)PIN_ENC_B);
  attachInterrupt(PIN_ENC_A, encIsr, CHANGE);
  attachInterrupt(PIN_ENC_B, encIsr, CHANGE);

  ledcAttach(PIN_MOT_IN1, 20000, 10);
  ledcAttach(PIN_MOT_IN2, 20000, 10);
  ledcWrite(PIN_MOT_IN1, 0);
  ledcWrite(PIN_MOT_IN2, 0);

#if HAVE_ESTOP
  pinMode(PIN_ESTOP, INPUT_PULLUP);
#endif
  g_strip.begin();
  g_strip.clear();
  g_strip.show();
}

static bool allExpandersPresent() {
  return mcpOk[0] && mcpOk[1] && mcpOk[2] && mcpOk[3];
}

// Читает кнопки и датчики. Возвращает false при сбое шины.
static bool readInputs() {
  bool ok = true;
  uint16_t v;
  for (int i = 0; i < 4; i++) {
    if (!mcpOk[i]) continue;
    if (mcpRead16(MCP_ADDR[i], v)) {
      if (i == 0) g_rawBtn = (g_rawBtn & 0xFFFF0000) | v;
      if (i == 1) g_rawBtn = (g_rawBtn & 0x0000FFFF) | ((uint32_t)v << 16);
      if (i == 2) g_rawHall = (g_rawHall & 0xFFFF0000) | v;
      if (i == 3) g_rawHall = (g_rawHall & 0x0000FFFF) | ((uint32_t)v << 16);
    } else {
      ok = false;
    }
  }
  if (ok) g_i2cFails = 0; else if (g_i2cFails < 65000) g_i2cFails++;
  return ok;
}

static uint32_t buttonsPressed() { return ~g_rawBtn; }
static uint32_t hallsPresent(bool invert) { return invert ? g_rawHall : ~g_rawHall; }

static int32_t encoder() { return g_enc; }

// Мощность -100..100 %. 0 с торможением (оба входа высокие) удерживает кабину.
static void motorDrive(float pct) {
  if (pct > 100) pct = 100;
  if (pct < -100) pct = -100;
  uint32_t d = (uint32_t)(fabsf(pct) * 10.23f);
  if (pct > 0) { ledcWrite(PIN_MOT_IN1, d); ledcWrite(PIN_MOT_IN2, 0); }
  else if (pct < 0) { ledcWrite(PIN_MOT_IN1, 0); ledcWrite(PIN_MOT_IN2, d); }
  else { ledcWrite(PIN_MOT_IN1, 1023); ledcWrite(PIN_MOT_IN2, 1023); }
}
static void motorBrake() { motorDrive(0); }
static void motorCoast() { ledcWrite(PIN_MOT_IN1, 0); ledcWrite(PIN_MOT_IN2, 0); }

static bool estop() {
#if HAVE_ESTOP
  return digitalRead(PIN_ESTOP) == LOW;
#else
  return false;
#endif
}

static void setPixel(int i, uint8_t r, uint8_t g, uint8_t b) { g_strip.setPixelColor(i, r, g, b); }
static void showPixels(uint8_t brightnessPct) {
  g_strip.setBrightness((uint16_t)brightnessPct * 255 / 100);
  g_strip.show();
}

}  // namespace HW
