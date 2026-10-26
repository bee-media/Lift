// Пины, настройки и их хранение во флеше.
#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define FW_VERSION "1.0.0"
#define MAX_FLOORS 25

// ---------- Пины ESP32-S3 (GPIO26..37 не используем: флеш/PSRAM; GPIO0,3,45,46: strapping) ----------
#define PIN_SDA      8    // I2C к четырём MCP23017
#define PIN_SCL      9
#define PIN_ENC_A    4    // энкодер N20, канал A (питание энкодера 3,3 В)
#define PIN_ENC_B    5    // канал B
#define PIN_MOT_IN1  6    // DRV8833 IN1
#define PIN_MOT_IN2  7    // DRV8833 IN2
#define PIN_LED_DATA 15   // цепочка WS2812 через 74AHCT125
#define PIN_ESTOP    11   // необязательная кнопка аварийного стопа (замыкает на GND)
#define HAVE_ESTOP   0    // поставьте 1, если кнопка установлена

// ---------- Адреса MCP23017 ----------
// 0x20: кнопки этажей 1..16   (порт A = этажи 1..8, порт B = этажи 9..16)
// 0x21: кнопки этажей 17..25  (порт A = этажи 17..24, порт B бит 0 = этаж 25)
// 0x22: датчики Холла 1..16
// 0x23: датчики Холла 17..25
#define MCP_BTN_LO 0x20
#define MCP_BTN_HI 0x21
#define MCP_HALL_LO 0x22
#define MCP_HALL_HI 0x23

// ---------- Настройки (всё меняется из веб-панели) ----------
struct Settings {
  uint32_t magic;
  uint8_t floors;
  uint8_t mode;            // 0 = попутный вниз, 1 = по очереди, 2 = попутный в обе стороны
  float vmax;              // мм/с
  float accel;             // мм/с^2
  float creep;             // мм/с, скорость подхода
  float dwell;             // с
  bool parkEnabled;
  uint8_t parkFloor;       // 1..floors
  float parkIdle;          // с
  float longPress;         // с
  bool priorityOn;
  char prioMask[MAX_FLOORS + 1];
  uint8_t brightness;      // %
  bool night;
  bool demo;
  bool slowmo;
  int8_t faultHall;        // -1 = нет; иначе номер этажа (с 0), датчик «сломан» для урока
  float pitch;             // мм между этажами
  float hallZone;          // мм, ширина зоны срабатывания по умолчанию
  float cpm;               // импульсов энкодера на 1 мм
  bool invMotor;
  bool invEnc;
  bool invHall;
  float kp;                // регулятор скорости: % мощности на мм/с ошибки
  float ki;
  char hostname[24];
  char ssid[33];
  char pass[65];
};

static const uint32_t SETTINGS_MAGIC = 0x4C494654 + sizeof(Settings);

static Settings settingsDefaults() {
  Settings s;
  memset(&s, 0, sizeof(s));
  s.magic = SETTINGS_MAGIC;
  s.floors = 5; s.mode = 0;
  s.vmax = 120; s.accel = 300; s.creep = 25; s.dwell = 3;
  s.parkEnabled = false; s.parkFloor = 1; s.parkIdle = 60; s.longPress = 5;
  s.priorityOn = false;
  s.brightness = 40; s.night = false; s.demo = false; s.slowmo = false; s.faultHall = -1;
  s.pitch = 100; s.hallZone = 12;
  s.cpm = 23.0f;           // 7 имп/оборот * 4 фронта * редуктор 100:1 / окружность шкива 121 мм
  s.invMotor = false; s.invEnc = false; s.invHall = false;
  s.kp = 1.2f; s.ki = 0.8f;
  strcpy(s.hostname, "lift");
  return s;
}

// Калибровка: где на самом деле стоят центры датчиков.
struct CalData {
  uint32_t magic;
  bool done;
  float cpm;
  float center[MAX_FLOORS];   // мм от 1-го этажа
  float half[MAX_FLOORS];     // половина ширины зоны, мм
};
static const uint32_t CAL_MAGIC = 0x43414C00 + sizeof(CalData);

static Preferences prefs;

static void settingsLoad(Settings& s) {
  s = settingsDefaults();
  prefs.begin("lift", true);
  Settings tmp;
  if (prefs.getBytesLength("cfg") == sizeof(Settings)) {
    prefs.getBytes("cfg", &tmp, sizeof(Settings));
    if (tmp.magic == SETTINGS_MAGIC) s = tmp;
  }
  prefs.end();
}

static void settingsSave(const Settings& s) {
  prefs.begin("lift", false);
  prefs.putBytes("cfg", &s, sizeof(Settings));
  prefs.end();
}

static void calLoad(CalData& c, const Settings& s) {
  memset(&c, 0, sizeof(c));
  c.magic = CAL_MAGIC; c.done = false; c.cpm = s.cpm;
  for (int i = 0; i < MAX_FLOORS; i++) { c.center[i] = i * s.pitch; c.half[i] = s.hallZone / 2; }
  prefs.begin("lift", true);
  CalData tmp;
  if (prefs.getBytesLength("cal") == sizeof(CalData)) {
    prefs.getBytes("cal", &tmp, sizeof(CalData));
    if (tmp.magic == CAL_MAGIC) c = tmp;
  }
  prefs.end();
}

static void calSave(const CalData& c) {
  prefs.begin("lift", false);
  prefs.putBytes("cal", &c, sizeof(CalData));
  prefs.end();
}

// Пароль точки доступа LIFT-Setup лежит в отдельной области памяти ("liftsec"), поэтому
// «заводской сброс» его не стирает и не открывает сеть соседям. Пустой пароль = точка открыта.
static String apPassLoad() {
  Preferences p;
  p.begin("liftsec", true);
  String s = p.getString("ap", "");
  p.end();
  return s;
}

static void apPassSave(const String& s) {
  Preferences p;
  p.begin("liftsec", false);
  p.putString("ap", s);
  p.end();
}

static void factoryReset() {
  prefs.begin("lift", false);
  prefs.clear();
  prefs.end();
}
