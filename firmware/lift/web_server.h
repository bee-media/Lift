// Wi-Fi, веб-сервер, API и обновление прошивки по воздуху.
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "webpage.h"
#include "lift_ctrl.h"

static WebServer server(80);
static DNSServer dnsServer;
static bool apMode = false;
static String g_apPass;            // пароль точки LIFT-Setup (8..63 символа), пусто = открыта
static uint32_t g_apRestartAt = 0; // когда перезапустить точку с новым паролем (после ответа клиенту)

static void startAp() {
  if (g_apPass.length() >= 8) WiFi.softAP("LIFT-Setup", g_apPass.c_str());
  else WiFi.softAP("LIFT-Setup");
}

// ---------- Мини-разбор плоского JSON (достаточно для нашей панели) ----------
static bool jFind(const String& b, const char* key, int& vs) {
  String k = String("\"") + key + "\"";
  int i = b.indexOf(k);
  if (i < 0) return false;
  i = b.indexOf(':', i + k.length());
  if (i < 0) return false;
  i++;
  while (i < (int)b.length() && b[i] == ' ') i++;
  vs = i;
  return true;
}
static bool jNum(const String& b, const char* key, float& out) {
  int i; if (!jFind(b, key, i)) return false;
  out = b.substring(i).toFloat();
  return true;
}
static bool jBool(const String& b, const char* key, bool& out) {
  int i; if (!jFind(b, key, i)) return false;
  out = b.startsWith("true", i);
  return true;
}
static bool jStr(const String& b, const char* key, char* out, size_t cap) {
  int i; if (!jFind(b, key, i)) return false;
  if (b[i] != '"') return false;
  i++;
  size_t n = 0;
  while (i < (int)b.length() && b[i] != '"' && n + 1 < cap) {
    if (b[i] == '\\' && i + 1 < (int)b.length()) i++;
    out[n++] = b[i++];
  }
  out[n] = 0;
  return true;
}

static String f1(float x) { return String(x, 1); }

static String stateJson() {
  const Settings& c = lift.cfg;
  int n = c.floors;
  String s;
  s.reserve(1600);
  s += "{\"sim\":false,\"floors\":"; s += n;
  s += ",\"pos\":"; s += f1(lift.pos);
  s += ",\"vel\":"; s += f1(lift.vel);
  int fl = (int)roundf(lift.pos / c.pitch);
  if (fl < 0) fl = 0; if (fl > n - 1) fl = n - 1;
  s += ",\"floor\":"; s += fl;
  s += ",\"state\":\""; s += STATE_NAMES[lift.state]; s += "\"";
  s += ",\"dir\":"; s += (int)lift.dir;
  s += ",\"target\":"; s += (int)lift.target;

  // Очередь по порядку нажатий
  int idx[MAX_FLOORS], m = 0;
  for (int f = 0; f < n; f++) if (lift.callT[f]) idx[m++] = f;
  for (int i = 1; i < m; i++) { int v = idx[i], j = i - 1; while (j >= 0 && lift.callT[idx[j]] > lift.callT[v]) { idx[j + 1] = idx[j]; j--; } idx[j + 1] = v; }
  s += ",\"calls\":[";
  for (int i = 0; i < m; i++) { if (i) s += ","; s += idx[i]; }
  s += "],\"hall\":[";
  uint32_t h = lift.hall;
  for (int f = 0; f < n; f++) { if (f) s += ","; s += ((h >> f) & 1) ? 1 : 0; }
  s += "],\"led\":[";
  for (int f = 0; f < n; f++) {
    if (f) s += ",";
    int ls = lift.ledState(f);
    s += lift.ledTestOn(f) ? "\"test\"" : (ls == 1 ? "\"call\"" : (ls == 2 ? "\"here\"" : "\"off\""));
  }
  s += "],\"btn\":[";
  uint32_t bt = lift.buttonsNow();
  for (int f = 0; f < n; f++) { if (f) s += ","; s += ((bt >> f) & 1) ? 1 : 0; }
  s += "],\"ioTest\":"; s += lift.ioTestOn() ? "true" : "false";
  s += ",\"clock\":"; s += clockMinutes();
  s += ",\"nightNow\":"; s += (c.night || nightScheduled()) ? "true" : "false";
  s += ",\"fault\":\""; if (lift.state == ST_FAULT) s += lift.fault; s += "\"";
  s += ",\"parking\":"; s += lift.parking ? "true" : "false";
  s += ",\"counts\":"; s += (long)lift.counts();
  s += ",\"noEnc\":"; s += g_useEnc ? "false" : "true";
  s += ",\"pwm\":"; s += (int)fabsf(lift.pwm);
  s += ",\"cal\":{\"done\":"; s += lift.calDone ? "true" : "false";
  s += ",\"progress\":"; s += String(lift.calProgress, 2);
  s += ",\"centers\":[";
  if (lift.calDone) for (int f = 0; f < n; f++) { if (f) s += ","; s += f1(lift.calCenter(f)); }
  s += "]},\"mode\":\""; s += lift.modeName(); s += "\"";
  s += ",\"uptime\":"; s += (long)(millis() / 1000);
  s += ",\"ip\":\""; if (WiFi.status() == WL_CONNECTED) s += WiFi.localIP().toString(); s += "\"";
  s += ",\"ssidNow\":\""; if (WiFi.status() == WL_CONNECTED) s += WiFi.SSID(); s += "\"";
  s += ",\"parkFloor\":"; s += c.parkEnabled ? (int)c.parkFloor - 1 : -1;
  s += "}";
  return s;
}

static String configJson() {
  Settings c = lift.getConfig();
  String s;
  s.reserve(700);
  s += "{\"floors\":"; s += c.floors;
  s += ",\"mode\":\""; s += MODE_NAMES[c.mode > 2 ? 0 : c.mode]; s += "\"";
  s += ",\"vmax\":"; s += f1(c.vmax);
  s += ",\"accel\":"; s += f1(c.accel);
  s += ",\"creep\":"; s += f1(c.creep);
  s += ",\"dwell\":"; s += f1(c.dwell);
  s += ",\"parkEnabled\":"; s += c.parkEnabled ? "true" : "false";
  s += ",\"parkFloor\":"; s += c.parkFloor;
  s += ",\"parkIdle\":"; s += f1(c.parkIdle);
  s += ",\"longPress\":"; s += f1(c.longPress);
  s += ",\"priorityOn\":"; s += c.priorityOn ? "true" : "false";
  s += ",\"prioMask\":\""; s += c.prioMask; s += "\"";
  s += ",\"brightness\":"; s += c.brightness;
  s += ",\"night\":"; s += c.night ? "true" : "false";
  s += ",\"nightAuto\":"; s += g_nightAuto ? "true" : "false";
  s += ",\"nightFrom\":"; s += (int)g_nightFrom;
  s += ",\"nightTo\":"; s += (int)g_nightTo;
  s += ",\"nightBright\":"; s += (int)g_nightBright;
  s += ",\"demo\":"; s += c.demo ? "true" : "false";
  s += ",\"slowmo\":"; s += c.slowmo ? "true" : "false";
  s += ",\"faultHall\":"; s += (int)c.faultHall;
  s += ",\"pitch\":"; s += f1(c.pitch);
  s += ",\"hallZone\":"; s += f1(c.hallZone);
  s += ",\"cpm\":"; s += String(c.cpm, 3);
  s += ",\"useEnc\":"; s += g_useEnc ? "true" : "false";
  s += ",\"openGain\":"; s += String(g_openGain, 2);
  s += ",\"minDuty\":"; s += String(g_minDuty, 0);
  s += ",\"decel\":"; s += f1(g_decel);
  s += ",\"approach\":"; s += f1(g_approach);
  s += ",\"runDuty\":"; s += String(g_runDuty, 0);
  s += ",\"invMotor\":"; s += c.invMotor ? "true" : "false";
  s += ",\"invEnc\":"; s += c.invEnc ? "true" : "false";
  s += ",\"invHall\":"; s += c.invHall ? "true" : "false";
  s += ",\"kp\":"; s += String(c.kp, 2);
  s += ",\"ki\":"; s += String(c.ki, 2);
  s += ",\"hostname\":\""; s += c.hostname; s += "\"";
  s += ",\"ssid\":\""; s += c.ssid; s += "\"";
  s += ",\"pass\":\"\"";
  s += ",\"apSecured\":"; s += g_apPass.length() >= 8 ? "true" : "false";
  s += "}";
  return s;
}

static void applyConfigBody(const String& b) {
  Settings s = lift.getConfig();
  float v; bool bv; char tmp[80];
  if (jNum(b, "floors", v)) s.floors = constrain((int)v, 2, MAX_FLOORS);
  if (jStr(b, "mode", tmp, sizeof(tmp))) s.mode = !strcmp(tmp, "fifo") ? 1 : (!strcmp(tmp, "both") ? 2 : 0);
  if (jNum(b, "vmax", v)) s.vmax = constrain(v, 20.0f, 300.0f);
  if (jNum(b, "accel", v)) s.accel = constrain(v, 5.0f, 1000.0f);
  if (jNum(b, "creep", v)) s.creep = constrain(v, 1.0f, 60.0f);
  if (jNum(b, "dwell", v)) s.dwell = constrain(v, 0.5f, 30.0f);
  if (jBool(b, "parkEnabled", bv)) s.parkEnabled = bv;
  if (jNum(b, "parkFloor", v)) s.parkFloor = constrain((int)v, 1, MAX_FLOORS);
  if (jNum(b, "parkIdle", v)) s.parkIdle = constrain(v, 5.0f, 600.0f);
  if (jNum(b, "longPress", v)) s.longPress = constrain(v, 1.0f, 10.0f);
  if (jBool(b, "priorityOn", bv)) s.priorityOn = bv;
  if (jStr(b, "prioMask", tmp, sizeof(tmp))) { strncpy(s.prioMask, tmp, MAX_FLOORS); s.prioMask[MAX_FLOORS] = 0; }
  if (jNum(b, "brightness", v)) s.brightness = constrain((int)v, 5, 100);
  if (jBool(b, "night", bv)) s.night = bv;
  {
    bool a0 = g_nightAuto; uint16_t f0 = g_nightFrom, t0 = g_nightTo; uint8_t b0 = g_nightBright;
    if (jBool(b, "nightAuto", bv)) g_nightAuto = bv;
    if (jNum(b, "nightFrom", v)) g_nightFrom = constrain((int)v, 0, 1439);
    if (jNum(b, "nightTo", v)) g_nightTo = constrain((int)v, 0, 1439);
    if (jNum(b, "nightBright", v)) g_nightBright = constrain((int)v, 1, 100);
    if (a0 != g_nightAuto || f0 != g_nightFrom || t0 != g_nightTo || b0 != g_nightBright) nightSave();
  }
  if (jBool(b, "demo", bv)) s.demo = bv;
  if (jBool(b, "slowmo", bv)) s.slowmo = bv;
  if (jNum(b, "faultHall", v)) s.faultHall = constrain((int)v, -1, MAX_FLOORS - 1);
  if (jNum(b, "pitch", v)) s.pitch = constrain(v, 50.0f, 150.0f);
  if (jNum(b, "hallZone", v)) s.hallZone = constrain(v, 4.0f, 40.0f);
  if (jNum(b, "cpm", v)) s.cpm = constrain(v, 0.5f, 100.0f);
  // Режим без энкодера: при переключении лифт заново ищет первый этаж.
  bool encOld = g_useEnc;
  float gainOld = g_openGain, minOld = g_minDuty;
  if (jBool(b, "useEnc", bv)) g_useEnc = bv;
  if (jNum(b, "openGain", v)) g_openGain = constrain(v, 0.05f, 5.0f);
  if (jNum(b, "minDuty", v)) g_minDuty = constrain(v, 5.0f, 90.0f);
  float decOld = g_decel, apOld = g_approach, runOld = g_runDuty;
  if (jNum(b, "decel", v)) g_decel = constrain(v, 5.0f, 1000.0f);
  if (jNum(b, "approach", v)) g_approach = constrain(v, 1.0f, 50.0f);
  if (jNum(b, "runDuty", v)) g_runDuty = constrain(v, 5.0f, 90.0f);
  if (g_useEnc != encOld || g_openGain != gainOld || g_minDuty != minOld || g_decel != decOld || g_approach != apOld || g_runDuty != runOld) motorOptSave();
  if (g_useEnc != encOld) {
    liftLog(g_useEnc ? "Энкодер включён" : "Энкодер выключен: положение по времени и датчикам");
    lift.post(CMD_HOME);
  }
  if (jBool(b, "invMotor", bv)) s.invMotor = bv;
  if (jBool(b, "invEnc", bv)) s.invEnc = bv;
  if (jBool(b, "invHall", bv)) s.invHall = bv;
  if (jNum(b, "kp", v)) s.kp = constrain(v, 0.0f, 20.0f);
  if (jNum(b, "ki", v)) s.ki = constrain(v, 0.0f, 20.0f);
  if (jStr(b, "hostname", tmp, sizeof(tmp)) && tmp[0]) { strncpy(s.hostname, tmp, sizeof(s.hostname) - 1); }
  if (jStr(b, "ssid", tmp, sizeof(tmp))) { strncpy(s.ssid, tmp, sizeof(s.ssid) - 1); }
  if (jStr(b, "pass", tmp, sizeof(tmp))) { strncpy(s.pass, tmp, sizeof(s.pass) - 1); }
  lift.setConfig(s);
  // Пароль точки доступа: только 8..63 символа, иначе игнорируем. Точка перезапустится после ответа.
  if (jStr(b, "apPass", tmp, sizeof(tmp))) {
    size_t n = strlen(tmp);
    if (n >= 8 && n <= 63) {
      g_apPass = String(tmp);
      apPassSave(g_apPass);
      g_apRestartAt = millis() + 800;
      liftLog("Пароль точки LIFT-Setup изменён");
    }
  }
}

static String logJson() {
  String s = "[";
  xSemaphoreTake(g_logMux, portMAX_DELAY);
  for (int i = 0; i < g_logCount; i++) {
    int k = (g_logHead - 1 - i + 40) % 40;  // свежие первыми
    if (i) s += ",";
    s += "{\"t\":"; s += (long)g_log[k].t; s += ",\"text\":\"";
    for (const char* p = g_log[k].text; *p; p++) { if (*p == '"' || *p == '\\') s += '\\'; s += *p; }
    s += "\"}";
  }
  xSemaphoreGive(g_logMux);
  s += "]";
  return s;
}

static void netBegin() {
  Settings s = lift.getConfig();
  // Режим выставляем один раз, сразу AP_STA: быстрая смена режимов после старта радио роняет
  // драйвер (Cache error в esp_phy_enable). Точка LIFT-Setup остаётся всегда как запасной вход.
  WiFi.persistent(false);
  WiFi.setHostname(s.hostname);
  WiFi.mode(WIFI_AP_STA);
  delay(300);
  g_apPass = apPassLoad();
  startAp();
  apMode = true;
  dnsServer.start(53, "*", WiFi.softAPIP());
  liftLog("Точка доступа LIFT-Setup (%s), адрес 192.168.4.1", g_apPass.length() >= 8 ? "с паролем" : "ОТКРЫТАЯ, задайте пароль в настройках");
  if (s.ssid[0]) {
    WiFi.begin(s.ssid, s.pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) delay(250);
    if (WiFi.status() == WL_CONNECTED) liftLog("Домашний Wi-Fi подключён: %s", WiFi.localIP().toString().c_str());
    else liftLog("Не удалось подключиться к домашнему Wi-Fi, лифт доступен через LIFT-Setup");
  }
  // Время из интернета, когда лифт подключён к домашней сети (иначе часы задаёт браузер панели).
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  if (MDNS.begin(s.hostname)) MDNS.addService("http", "tcp", 80);
  ArduinoOTA.setHostname(s.hostname);
  ArduinoOTA.onStart([]() { lift.post(CMD_STOP); });
  ArduinoOTA.begin();
}

static void webBegin() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html; charset=utf-8", (const char*)WEBPAGE_GZ, WEBPAGE_GZ_LEN);
  });
  server.on("/api/state", HTTP_GET, []() { server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", stateJson()); });
  server.on("/api/config", HTTP_GET, []() { server.send(200, "application/json", configJson()); });
  server.on("/api/config", HTTP_POST, []() { applyConfigBody(server.arg("plain")); server.send(200, "application/json", "{\"ok\":true}"); });
  // Панель присылает время браузера (секунды UTC и часовой пояс в минутах): лифту нужны часы для ночного режима.
  server.on("/api/time", HTTP_POST, []() {
    long t = server.arg("t").toInt();
    int tz = server.arg("tz").toInt();
    if (t > 1700000000L) {
      struct timeval tv = { (time_t)t, 0 };
      time_t cur = time(nullptr);
      if (cur < 1700000000L || labs((long)cur - t) > 2) settimeofday(&tv, nullptr);
    }
    if (tz >= -720 && tz <= 840 && tz != g_tzMin) { g_tzMin = tz; nightSave(); }
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/log", HTTP_GET, []() { server.send(200, "application/json", logJson()); });
  server.on("/api/call", HTTP_POST, []() { lift.post(CMD_CALL, server.arg("f").toInt()); server.send(200, "application/json", "{\"ok\":true}"); });
  server.on("/api/cmd", HTTP_POST, []() {
    String n = server.arg("name");
    int a = server.arg("arg").toInt();
    if (n == "home") lift.post(CMD_HOME);
    else if (n == "stop") lift.post(CMD_STOP);
    else if (n == "clear") lift.post(CMD_CLEAR);
    else if (n == "calibrate") lift.post(CMD_CALIBRATE);
    else if (n == "park") lift.post(CMD_PARK, a);
    else if (n == "reset") lift.post(CMD_RESET);
    else if (n == "motortest") lift.post(CMD_MOTORTEST, a);
    else if (n == "ledtest") lift.post(CMD_LEDTEST, a);
    else if (n == "iotest") lift.post(CMD_IOTEST, a);
    else if (n == "wifi") {
      // Подключиться к сохранённой домашней сети без перезагрузки; точка LIFT-Setup остаётся.
      Settings c = lift.getConfig();
      WiFi.disconnect();
      if (c.ssid[0]) WiFi.begin(c.ssid, c.pass);
    }
    else if (n == "reboot") { server.send(200, "application/json", "{\"ok\":true}"); delay(300); ESP.restart(); return; }
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/update", HTTP_POST, []() {
    bool ok = !Update.hasError();
    server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
    if (ok) { delay(500); ESP.restart(); }
  }, []() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
      lift.post(CMD_STOP);
      liftLog("Обновление прошивки: %s", up.filename.c_str());
      Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (up.status == UPLOAD_FILE_WRITE) {
      Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
      Update.end(true);
    }
  });
  server.onNotFound([]() {
    // Только для тех, кто подключён к точке LIFT-Setup (окно «Войти в сеть» на телефоне).
    if (apMode && server.client().localIP() == WiFi.softAPIP()) { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); }
    else server.send(404, "text/plain", "not found");
  });
  server.begin();
}
