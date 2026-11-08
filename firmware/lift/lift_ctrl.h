// Контроллер лифта: базирование, калибровка, движение, вызовы, светодиоды.
// Работает в отдельной задаче (см. lift.ino), веб только отправляет команды и читает состояние.
#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "dispatcher.h"
#include "hardware.h"

enum State : uint8_t { ST_HOMING, ST_IDLE, ST_MOVING, ST_DWELL, ST_CALIB, ST_FAULT };
static const char* const STATE_NAMES[] = { "homing", "idle", "moving", "dwell", "calibrating", "fault" };
static const char* const MODE_NAMES[] = { "down", "fifo", "both" };

enum CmdType : uint8_t { CMD_CALL, CMD_HOME, CMD_STOP, CMD_CLEAR, CMD_CALIBRATE, CMD_PARK, CMD_RESET };
struct CmdMsg { CmdType c; int arg; };

// ---------- Журнал ----------
struct LogEntry { uint32_t t; char text[200]; };
static LogEntry g_log[40];
static uint8_t g_logHead = 0, g_logCount = 0;
static SemaphoreHandle_t g_logMux = nullptr;

static void liftLog(const char* fmt, ...) {
  if (!g_logMux) g_logMux = xSemaphoreCreateMutex();
  char buf[200];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  Serial.println(buf);
  xSemaphoreTake(g_logMux, portMAX_DELAY);
  g_log[g_logHead].t = millis() / 1000;
  strncpy(g_log[g_logHead].text, buf, sizeof(g_log[0].text) - 1);
  g_log[g_logHead].text[sizeof(g_log[0].text) - 1] = 0;
  g_logHead = (g_logHead + 1) % 40;
  if (g_logCount < 40) g_logCount++;
  xSemaphoreGive(g_logMux);
}

class Lift {
 public:
  // ---- Данные для веба (пишет только задача контроля) ----
  volatile uint8_t state = ST_HOMING;
  volatile float pos = 0;          // мм от 1-го этажа
  volatile float vel = 0;          // мм/с
  volatile int8_t dir = 0;
  volatile int8_t target = -1;
  volatile int8_t curFloor = 0;
  volatile uint32_t hall = 0;      // биты датчиков
  volatile int32_t callT[MAX_FLOORS];
  volatile bool parking = false;
  volatile float pwm = 0;
  volatile float calProgress = 0;
  volatile bool calDone = false;
  char fault[200] = "";

  // ---- Настройки: веб меняет webCfg под мьютексом, контроллер забирает ----
  Settings webCfg;
  Settings cfg;
  CalData cal;
  SemaphoreHandle_t cfgMux;
  volatile bool cfgDirty = false;
  QueueHandle_t cmdQ;

  void begin() {
    cfgMux = xSemaphoreCreateMutex();
    cmdQ = xQueueCreate(16, sizeof(CmdMsg));
    for (int i = 0; i < MAX_FLOORS; i++) callT[i] = 0;
    settingsLoad(webCfg);
    cfg = webCfg;
    motorOptLoad();
    calLoad(cal, cfg);
    calDone = cal.done;
    encOff = (float)encS();
    lastEnc = encS();
    liftLog("Старт, версия %s", FW_VERSION);
    if (!g_useEnc) liftLog("РЕЖИМ БЕЗ ЭНКОДЕРА: положение по времени, остановка по датчикам Холла");
    if (!HW::allExpandersPresent()) liftLog("Внимание: расширители MCP23017 отвечают не все");
    HW::readInputs();
    hall = HW::hallsPresent(cfg.invHall) & floorMask();
    hallCand = hall;
    startHoming();
  }

  void post(CmdType c, int arg = 0) {
    CmdMsg m = { c, arg };
    xQueueSend(cmdQ, &m, 0);
  }

  // Вызывается из веба.
  void setConfig(const Settings& s) {
    xSemaphoreTake(cfgMux, portMAX_DELAY);
    webCfg = s;
    cfgDirty = true;
    xSemaphoreGive(cfgMux);
    settingsSave(s);
  }
  Settings getConfig() {
    xSemaphoreTake(cfgMux, portMAX_DELAY);
    Settings s = webCfg;
    xSemaphoreGive(cfgMux);
    return s;
  }

  const char* modeName() const { return MODE_NAMES[cfg.mode > 2 ? 0 : cfg.mode]; }
  int32_t counts() const { return encS() - (int32_t)encOff; }
  bool isLedCall(int f) const { return callT[f] != 0; }
  int ledState(int f) const {  // 0 нет, 1 вызов, 2 кабина здесь
    if (callT[f]) return 1;
    if ((state == ST_IDLE || state == ST_DWELL) && curFloor == f) return 2;
    return 0;
  }
  float calCenter(int f) const { return cal.center[f]; }

  // =========================================================================
  void tick(float dt) {
    applyCommands();
    syncConfig();
    readInputs();
    updatePosition(dt);
    handleButtons(dt);

    switch (state) {
      case ST_HOMING: stepHoming(dt); break;
      case ST_CALIB: stepCalib(dt); break;
      case ST_MOVING: stepMoving(dt); break;
      case ST_DWELL:
        timer -= dt;
        if (timer <= 0) state = ST_IDLE;
        break;
      case ST_IDLE: stepIdle(dt); break;
      case ST_FAULT: break;
    }
    if (state == ST_IDLE || state == ST_DWELL || state == ST_FAULT) { stopMotor(); }
    updateLeds();
  }

 private:
  // ---- Положение ----
  float encOff = 0;
  int32_t lastEnc = 0;
  float velF = 0;
  // Без энкодера «счётчик» ведём сами: интегрируем командную скорость (virtCounts), датчики Холла поправляют.
  float virtCounts = 0, cmdV = 0, vEstF = 0;
  int32_t encS() const {
    if (!g_useEnc) return (int32_t)virtCounts;
    return cfg.invEnc ? -g_enc : g_enc;
  }

  // ---- Датчики, кнопки ----
  uint32_t hallCand = 0, hallStableSet = 0;
  int32_t hallCandEnc = 0;
  bool hallCandSeen = false;
  uint32_t btnStable = 0, btnCand = 0;
  uint8_t btnCnt = 0;
  float holdT[MAX_FLOORS] = {0};
  bool holdFired[MAX_FLOORS] = {false};
  bool wasCalled[MAX_FLOORS] = {false};

  // ---- Движение ----
  float vcmd = 0, iterm = 0, stuckT = 0, wrongT = 0, timer = 0, idleFor = 0, demoIn = 4;
  float startMM = 0;
  bool seenRise[MAX_FLOORS] = {false};
  int32_t seqNo = 1;
  float travelStart = 0;
  uint8_t homePhase = 0;
  int32_t homeRiseEnc = 0;
  int32_t calRiseEnc[MAX_FLOORS], calFallEnc[MAX_FLOORS];
  uint32_t calRiseT[MAX_FLOORS], calFallT[MAX_FLOORS], calStartT = 0;
  bool calRiseSeen[MAX_FLOORS], calFallSeen[MAX_FLOORS];
  uint32_t ledLast[MAX_FLOORS];
  uint32_t ledShown = 0;
  float blink = 0;

  float speedScale() const { return (cfg.slowmo ? 0.3f : 1.0f) * (cfg.night ? 0.6f : 1.0f); }
  uint32_t floorMask() const { return cfg.floors >= 32 ? 0xFFFFFFFF : ((1UL << cfg.floors) - 1); }

  void applyCommands() {
    CmdMsg m;
    while (xQueueReceive(cmdQ, &m, 0) == pdTRUE) {
      switch (m.c) {
        case CMD_CALL: addCall(m.arg); break;
        case CMD_HOME: clearFault(); clearCalls(); startHoming(); break;
        case CMD_STOP: setFault("Нажат аварийный стоп"); break;
        case CMD_CLEAR: if (state == ST_FAULT) { clearFault(); startHoming(); } break;
        case CMD_CALIBRATE: if (state == ST_IDLE) startCalib(); break;
        case CMD_PARK: {
          Settings s = getConfig();
          s.parkFloor = m.arg + 1; s.parkEnabled = true;
          setConfig(s);
          liftLog("Дом лифта: %d этаж", m.arg + 1);
          break;
        }
        case CMD_RESET: {
          factoryReset();
          Settings d = settingsDefaults();
          setConfig(d);
          calLoad(cal, d);
          calDone = false;
          liftLog("Настройки сброшены");
          break;
        }
      }
    }
  }

  void syncConfig() {
    if (!cfgDirty) return;
    xSemaphoreTake(cfgMux, portMAX_DELAY);
    cfg = webCfg;
    cfgDirty = false;
    xSemaphoreGive(cfgMux);
    if (cfg.floors < 2) cfg.floors = 2;
    if (cfg.floors > MAX_FLOORS) cfg.floors = MAX_FLOORS;
    if (cfg.parkFloor < 1) cfg.parkFloor = 1;
    if (cfg.parkFloor > cfg.floors) cfg.parkFloor = cfg.floors;
    for (int i = cfg.floors; i < MAX_FLOORS; i++) callT[i] = 0;
    if (target >= cfg.floors) target = -1;
  }

  void setFault(const char* msg) {
    strncpy(fault, msg, sizeof(fault) - 1);
    fault[sizeof(fault) - 1] = 0;
    state = ST_FAULT;
    stopMotor();
    vcmd = 0;
    liftLog("ОШИБКА: %s", fault);
  }
  void clearFault() { fault[0] = 0; }

  void clearCalls() { for (int i = 0; i < MAX_FLOORS; i++) callT[i] = 0; target = -1; }

  void addCall(int f) {
    if (f < 0 || f >= cfg.floors || state == ST_FAULT) return;
    if (callT[f]) return;
    callT[f] = seqNo++;
    liftLog("Вызов с %d этажа", f + 1);
  }

  // ---- Входы ----
  void readInputs() {
    bool ok = HW::readInputs();
    if (!ok && g_i2cFails > 40 && state != ST_FAULT) { setFault("Нет связи с расширителями (I2C)"); return; }
#if HAVE_ESTOP
    if (HW::estop() && state != ST_FAULT) { setFault("Нажат аварийный стоп"); return; }
#endif
    uint32_t h = HW::hallsPresent(cfg.invHall) & floorMask();
    if (cfg.faultHall >= 0 && cfg.faultHall < cfg.floors) h &= ~(1UL << cfg.faultHall);
    // Состояние принимаем, когда два чтения подряд одинаковы; позицию берём из первого отличного чтения.
    if (h != hall) {
      if (!hallCandSeen || h != hallCand) { hallCand = h; hallCandEnc = encS(); hallCandSeen = true; }
      else {
        uint32_t old = hall;
        hall = h;
        hallCandSeen = false;
        onHallChange(old, h, hallCandEnc);
      }
    } else {
      hallCandSeen = false;
    }
  }

  void updatePosition(float dt) {
    if (!g_useEnc) {
      vEstF += min(1.0f, dt * 10.0f) * (cmdV - vEstF);   // мотор разгоняется с запаздыванием
      virtCounts += vEstF * cfg.cpm * dt;
      velF = vEstF;
      vel = velF;
      lastEnc = encS();
      pos = ((float)lastEnc - encOff) / cfg.cpm;
      return;
    }
    int32_t e = encS();
    float v = ((float)(e - lastEnc) / cfg.cpm) / dt;
    lastEnc = e;
    velF += 0.2f * (v - velF);
    vel = velF;
    pos = ((float)e - encOff) / cfg.cpm;
  }

  void handleButtons(float dt) {
    uint32_t b = HW::buttonsPressed() & floorMask();
    if (b != btnCand) { btnCand = b; btnCnt = 0; }
    else if (btnCnt < 255) btnCnt++;
    if (btnCnt >= 3) btnStable = btnCand;
    for (int f = 0; f < cfg.floors; f++) {
      bool down = (btnStable >> f) & 1;
      if (down) {
        if (holdT[f] == 0) { wasCalled[f] = callT[f] != 0; addCall(f); holdFired[f] = false; }
        holdT[f] += dt;
        if (!holdFired[f] && holdT[f] >= cfg.longPress) {
          holdFired[f] = true;
          if (!wasCalled[f]) callT[f] = 0;
          post(CMD_PARK, f);
        }
      } else {
        holdT[f] = 0;
      }
    }
  }

  // ---- События датчиков ----
  void onHallChange(uint32_t oldH, uint32_t newH, int32_t edgeEnc) {
    uint32_t changed = oldH ^ newH;
    for (int f = 0; f < cfg.floors; f++) {
      if (!((changed >> f) & 1)) continue;
      bool rise = (newH >> f) & 1;
      if (state == ST_CALIB) {
        if (rise) { calRiseEnc[f] = edgeEnc; calRiseT[f] = millis(); calRiseSeen[f] = true; }
        else { calFallEnc[f] = edgeEnc; calFallT[f] = millis(); calFallSeen[f] = true; }
      } else if (state == ST_HOMING) {
        if (rise && f == 0) { homeRiseEnc = edgeEnc; homeRiseSeen = true; }
      } else if (state == ST_MOVING) {
        if (rise) seenRise[f] = true;
        float s = vcmd > 0 ? 1.0f : -1.0f;
        float expected = rise ? cal.center[f] - s * cal.half[f] : cal.center[f] + s * cal.half[f];
        float edgePos = ((float)edgeEnc - encOff) / cfg.cpm;
        float err = expected - edgePos;
        if (fabsf(err) > 0.45f * cfg.pitch) {
          setFault("Потерял счёт этажей");
          return;
        }
        encOff = (float)edgeEnc - expected * cfg.cpm;
        if (fabsf(err) > 4.0f) liftLog("Поправка по датчику %d этажа: %.1f мм", f + 1, err);
      }
    }
  }
  bool homeRiseSeen = false;

  // ---- Мотор ----
  void stopMotor() { iterm = 0; stuckT = 0; wrongT = 0; pwm = 0; cmdV = 0; HW::motorBrake(); }

  void driveVel(float vTarget, float dt) {
    cmdV = vTarget;
    if (!g_useEnc) {
      // Без обратной связи: мощность прямо пропорциональна нужной скорости (коэффициент подбирает калибровка).
      float u = g_openGain * vTarget;
      if (fabsf(vTarget) > 1.0f) {
        const float MIN_DUTY = 10.0f;
        if (vTarget > 0 && u < MIN_DUTY) u = MIN_DUTY;
        if (vTarget < 0 && u > -MIN_DUTY) u = -MIN_DUTY;
      }
      pwm = u;
      HW::motorDrive(cfg.invMotor ? -u : u);
      return;
    }
    float err = vTarget - velF;
    iterm += cfg.ki * err * dt;
    if (iterm > 60) iterm = 60;
    if (iterm < -60) iterm = -60;
    float u = cfg.kp * err + iterm + 0.35f * vTarget;
    if (fabsf(vTarget) > 1.0f) {
      const float MIN_DUTY = 10.0f;
      if (vTarget > 0 && u < MIN_DUTY) u = MIN_DUTY;
      if (vTarget < 0 && u > -MIN_DUTY) u = -MIN_DUTY;
    }
    pwm = u;
    HW::motorDrive(cfg.invMotor ? -u : u);
    if (fabsf(vTarget) > 10.0f && fabsf(velF) < 2.0f) stuckT += dt; else stuckT = 0;
    if (stuckT > 1.5f) { setFault("Кабина не едет, возможно застряла"); return; }
    // Кабина едет против команды: мотор или энкодер подключены наоборот, регулятор бы разогнал её до упора.
    if (fabsf(vTarget) > 10.0f && fabsf(velF) > 10.0f && vTarget * velF < 0) wrongT += dt; else wrongT = 0;
    if (wrongT > 0.4f) setFault("Едет не туда: проверьте «Развернуть мотор» и «Развернуть энкодер»");
  }

  // ---- Базирование ----
  // Расширители, без которых лифт с таким числом этажей работать не может.
  bool expandersReady() const {
    bool ok = mcpOk[0] && mcpOk[2];                     // кнопки и датчики этажей 1..16
    if (cfg.floors > 16) ok = ok && mcpOk[1] && mcpOk[3];  // этажи 17..25
    return ok;
  }

  void startHoming() {
    HW::probeExpanders();
    if (!expandersReady()) {
      setFault("Нет расширителей MCP23017: проверьте провода I2C и адреса");
      return;
    }
    state = ST_HOMING;
    homeRiseSeen = false;
    homePhase = (hall & 1) ? 0 : 1;
    travelStart = pos;
    vcmd = 0; target = -1; dir = 0;
    liftLog("Ищу первый этаж");
  }

  void stepHoming(float dt) {
    float creep = cfg.creep * speedScale();
    if (creep < 8) creep = 8;
    if (fabsf(pos - travelStart) > (cfg.floors * cfg.pitch * 1.3f + 60)) { setFault("Не нашёл первый этаж"); return; }
    if (homePhase == 0) {            // стоим в зоне 1-го этажа: сначала выезжаем вверх из неё
      driveVel(creep, dt);
      if (!(hall & 1)) { homePhase = 1; travelStart = pos; }
    } else if (homePhase == 1) {     // едем вниз до входа в зону
      driveVel(-creep, dt);
      if (homeRiseSeen) homePhase = 2;
    } else {                         // от края зоны до центра = половина зоны, это положение покоя на упорах
      driveVel(-creep, dt);
      float stopEnc = (float)homeRiseEnc - cal.half[0] * cfg.cpm;
      if ((float)encS() <= stopEnc) {
        stopMotor();
        encOff = (float)encS();
        pos = 0; velF = 0; vcmd = 0;
        curFloor = 0; dir = 0;
        state = ST_IDLE; idleFor = 0;
        liftLog("Нашёл первый этаж. Готов.");
      }
    }
  }

  // ---- Калибровка ----
  void startCalib() {
    for (int i = 0; i < MAX_FLOORS; i++) { calRiseSeen[i] = false; calFallSeen[i] = false; }
    travelStart = pos;
    calProgress = 0; calDone = false;
    calStartT = millis();
    state = ST_CALIB;
    liftLog(g_useEnc ? "Калибровка: еду вверх" : "Калибровка без энкодера: еду вверх, измеряю скорость по датчикам");
  }

  // Калибровка без энкодера: скорость и расстояния находим по времени между датчиками (при постоянной скорости),
  // а коэффициент мощности подстраиваем так, чтобы реальная скорость совпала с заданной.
  void finishCalibOpenLoop(float vCmd) {
    int last = cfg.floors - 1;
    float tr[MAX_FLOORS], tf[MAX_FLOORS];
    for (int f = 1; f <= last; f++) {
      tr[f] = (float)(calRiseT[f] - calStartT) / 1000.0f;
      tf[f] = (float)(calFallT[f] - calStartT) / 1000.0f;
    }
    float vReal = 0, t1 = 0;
    if (last >= 2) {
      t1 = 0.5f * (tr[1] + tf[1]);
      float tl = 0.5f * (tr[last] + tf[last]);
      if (tl > t1 + 0.2f) vReal = (last - 1) * cfg.pitch / (tl - t1);
    }
    if (vReal < 5.0f) { setFault("Калибровка без энкодера: нужно минимум 3 этажа и исправные датчики"); return; }
    cal.center[0] = 0;
    cal.half[0] = cfg.hallZone / 2;
    for (int f = 1; f <= last; f++) {
      float tc = 0.5f * (tr[f] + tf[f]);
      cal.center[f] = cfg.pitch + vReal * (tc - t1);
      cal.half[f] = 0.5f * vReal * (tf[f] - tr[f]);
    }
    cal.done = true;
    calSave(cal);
    float g = g_openGain * vCmd / vReal;
    if (g < 0.05f) g = 0.05f;
    if (g > 5.0f) g = 5.0f;
    g_openGain = g;
    motorOptSave();
    calDone = true;
    liftLog("Калибровка без энкодера: скорость %.0f мм/с при команде %.0f, коэффициент %.2f", vReal, vCmd, g);
    startMove(0);
  }

  void stepCalib(float dt) {
    float v = cfg.creep * speedScale();
    if (v < 20) v = 20;
    driveVel(v, dt);
    float top = (cfg.floors - 1) * cfg.pitch;
    calProgress = constrain(pos / top, 0.0f, 1.0f);
    if (pos - travelStart > (cfg.floors * cfg.pitch * 1.3f + 60)) { setFault("Калибровка: не нашёл верхний датчик"); return; }
    int last = cfg.floors - 1;
    if (!calFallSeen[last]) return;
    stopMotor();
    for (int f = 1; f < cfg.floors; f++) {
      if (!calRiseSeen[f] || !calFallSeen[f] || calFallEnc[f] <= calRiseEnc[f]) {
        char b[64]; snprintf(b, sizeof(b), "Калибровка: нет сигнала датчика %d этажа", f + 1);
        setFault(b);
        return;
      }
    }
    if (!g_useEnc) { finishCalibOpenLoop(v); return; }
    float topCenterEnc = 0.5f * (calRiseEnc[last] + calFallEnc[last]);
    float newCpm = (topCenterEnc - encOff) / (last * cfg.pitch);
    if (newCpm < 0.5f || newCpm > 100) { setFault("Калибровка: странное число импульсов"); return; }
    cal.cpm = newCpm;
    cal.center[0] = 0;
    cal.half[0] = cfg.hallZone / 2;
    for (int f = 1; f < cfg.floors; f++) {
      cal.center[f] = (0.5f * (calRiseEnc[f] + calFallEnc[f]) - encOff) / newCpm;
      cal.half[f] = 0.5f * (calFallEnc[f] - calRiseEnc[f]) / newCpm;
    }
    cal.done = true;
    calSave(cal);
    // Обновляем число импульсов на мм, положение пересчитываем так, чтобы кабина осталась на месте.
    float posNow = pos;
    Settings s = getConfig();
    s.cpm = newCpm;
    setConfig(s);
    cfg.cpm = newCpm;
    cfgDirty = false;
    encOff = (float)encS() - posNow * newCpm;
    lastEnc = encS();
    calDone = true;
    liftLog("Калибровка закончена. Импульсов на мм: %.2f", newCpm);
    startMove(0);
  }

  // ---- Движение к этажу ----
  void startMove(int f) {
    target = f;
    dir = (cal.center[f] > pos) ? 1 : -1;
    vcmd = 0;
    startMM = pos;
    for (int i = 0; i < MAX_FLOORS; i++) seenRise[i] = (hall >> i) & 1;
    state = ST_MOVING;
  }

  int buildCalls(DCall* out) {
    int n = 0;
    for (int f = 0; f < cfg.floors; f++)
      if (callT[f]) { out[n].f = f; out[n].t = callT[f]; out[n].prio = cfg.prioMask[f] == '1'; n++; }
    return n;
  }

  void stepIdle(float dt) {
    idleFor += dt;
    DCall calls[MAX_FLOORS];
    int n = buildCalls(calls);
    DPlanIn in = { cfg.mode, calls, n, pos / cfg.pitch, dir, false, 0, -1, cfg.priorityOn };
    DPlanOut r = dispatchPlan(in);
    if (r.target >= 0) {
      idleFor = 0; parking = false;
      if (r.target == curFloor && fabsf(pos - cal.center[r.target]) < 2.0f) { arrive(r.target); return; }
      liftLog("Еду %s на %d этаж", r.dir > 0 ? "вверх" : "вниз", r.target + 1);
      startMove(r.target);
      dir = r.dir ? r.dir : dir;
      return;
    }
    if (cfg.parkEnabled && idleFor > cfg.parkIdle && curFloor != cfg.parkFloor - 1) {
      parking = true; idleFor = 0;
      liftLog("Долго стоял, еду домой: %d этаж", cfg.parkFloor);
      startMove(cfg.parkFloor - 1);
      return;
    }
    if (cfg.demo) {
      demoIn -= dt;
      if (demoIn <= 0) { demoIn = 2 + (float)(esp_random() % 5000) / 1000.0f; addCall(esp_random() % cfg.floors); }
    }
  }

  void stepMoving(float dt) {
    float k = speedScale();
    float vmax = cfg.vmax * k, a = cfg.accel * k, creep = min(cfg.creep, vmax);
    float pitch = cfg.pitch;

    // Проверка: не пропущен ли датчик, мимо которого проехали.
    for (int f = 0; f < cfg.floors; f++) {
      // Без энкодера положение приблизительное, поэтому допуск шире.
      float c = cal.center[f], mg = cal.half[f] + (g_useEnc ? 6.0f : 0.3f * pitch);
      bool passedUp = startMM < c - mg && pos > c + mg;
      bool passedDown = startMM > c + mg && pos < c - mg;
      if ((passedUp || passedDown) && !seenRise[f]) {
        char b[64]; snprintf(b, sizeof(b), "Не вижу датчик на %d этаже", f + 1);
        setFault(b);
        return;
      }
    }

    int dirNow = vcmd > 0.1f ? 1 : (vcmd < -0.1f ? -1 : dir);
    float brake = (vcmd * vcmd) / (2 * a) / pitch + 0.02f;
    DCall calls[MAX_FLOORS];
    int n = buildCalls(calls);
    if (!parking) {
      DPlanIn in = { cfg.mode, calls, n, pos / pitch, dirNow, true, brake, target, cfg.priorityOn };
      DPlanOut r = dispatchPlan(in);
      if (r.target >= 0 && r.target != target) { target = r.target; liftLog("Теперь еду на %d этаж", target + 1); }
      if (r.dir) dir = r.dir;
    } else if (n > 0) {
      parking = false;
    }

    float tmm = cal.center[target];
    float dist = tmm - pos;
    float sgn = dist >= 0 ? 1.0f : -1.0f;
    float ad = fabsf(dist);
    float want = max(min(vmax, sqrtf(2 * a * max(ad - 3.0f, 0.0f))), min(creep, ad * 3));
    // Без энкодера останавливаемся по датчику: пока датчик нужного этажа не сработал, ползём дальше
    // на скорости подхода, даже если по расчёту уже приехали. Сработал датчик: положение исправится само.
    bool waitHall = !g_useEnc && !seenRise[target];
    if (waitHall) {
      float mdir = (tmm >= startMM) ? 1.0f : -1.0f;
      if (ad < 15.0f || sgn != mdir) { sgn = mdir; want = creep; }
      if ((pos - tmm) * mdir > 0.6f * pitch) {
        char b[64]; snprintf(b, sizeof(b), "Не вижу датчик на %d этаже", target + 1);
        setFault(b);
        return;
      }
    }
    float dv = sgn * want - vcmd;
    float lim = a * dt;
    vcmd += constrain(dv, -lim, lim);
    driveVel(vcmd, dt);
    if (state != ST_MOVING) return;
    float after = tmm - pos;
    bool waitNow = !g_useEnc && !seenRise[target];
    if (!waitNow && ((after >= 0 ? 1.0f : -1.0f) != sgn || fabsf(after) < 0.4f)) arrive(target);
  }

  void arrive(int f) {
    stopMotor();
    vcmd = 0; velF = 0;
    curFloor = f; target = -1; parking = false;
    callT[f] = 0;
    state = ST_DWELL; timer = cfg.dwell; idleFor = 0;
    liftLog("Приехал на %d этаж", f + 1);
    if (!((hall >> f) & 1)) {
      char b[64]; snprintf(b, sizeof(b), "Не вижу датчик на %d этаже", f + 1);
      setFault(b);
    }
  }

  // ---- Светодиоды этажей ----
  void updateLeds() {
    blink += 0.005f;
    uint8_t bright = cfg.night ? min((int)cfg.brightness, 15) : cfg.brightness;
    uint32_t colors[MAX_FLOORS];
    bool faultOn = state == ST_FAULT && ((int)(millis() / 400) & 1);
    for (int f = 0; f < MAX_FLOORS; f++) {
      uint32_t c = 0;
      if (f < cfg.floors) {
        int ls = ledState(f);
        if (state == ST_FAULT) c = faultOn ? 0xFF0000 : 0x000000;
        else if (state == ST_HOMING) c = (f == 0) ? 0x0000FF : 0;
        else if (state == ST_CALIB) c = 0x003C50;
        else if (ls == 1) c = 0xFF9600;
        else if (ls == 2) c = 0x00C850;
        else if (cfg.parkEnabled && f == cfg.parkFloor - 1) c = 0x000028;
      }
      colors[f] = c;
    }
    static uint32_t lastShow = 0;
    bool changed = bright != ledShown;
    for (int f = 0; f < MAX_FLOORS; f++) if (colors[f] != ledLast[f]) changed = true;
    if (!changed || millis() - lastShow < 50) return;
    lastShow = millis();
    ledShown = bright;
    for (int f = 0; f < MAX_FLOORS; f++) {
      ledLast[f] = colors[f];
      HW::setPixel(f, colors[f] >> 16, (colors[f] >> 8) & 0xFF, colors[f] & 0xFF);
    }
    HW::showPixels(bright);
  }
};

Lift lift;  // единственный экземпляр
