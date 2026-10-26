// Симулятор лифта в браузере. Нужен, чтобы пользоваться панелью без железа.
// Отдаёт тот же формат состояния, что и прошивка (см. firmware/lift/lift.ino, /api/state).
(function (root) {
  const D = root.Dispatcher;

  const DEFAULTS = {
    floors: 5, mode: 'down',
    vmax: 120, accel: 300, creep: 25, dwell: 3,
    parkEnabled: false, parkFloor: 1, parkIdle: 60, longPress: 5,
    priorityOn: false, prioMask: '',
    brightness: 40, night: false, demo: false, slowmo: false, faultHall: -1,
    pitch: 100, hallZone: 12, cpm: 23.0,
    invMotor: false, invEnc: false, invHall: false, kp: 1.2, ki: 0.8,
    hostname: 'lift', ssid: '', pass: '',
  };

  class SimLift {
    constructor() {
      this.sim = true;
      this.cfg = Object.assign({}, DEFAULTS);
      this.calls = [];
      this.seq = 0;
      this.pos = 100 * (0.4 + Math.random() * 1.6);
      this.vel = 0;
      this.state = 'homing';
      this.dir = 0;
      this.target = -1;
      this.cur = 0;
      this.timer = 0;
      this.idleFor = 0;
      this.demoIn = 4;
      this.fault = '';
      this.parking = false;
      this.cal = { done: false, centers: [], progress: 0, phase: '' };
      this.logs = [];
      this.t0 = performance.now();
      this.last = this.t0;
      this.say('Включился. Ищу первый этаж.');
      this._loop = setInterval(() => this.step(), 30);
    }

    say(text) {
      this.logs.unshift({ t: Math.round((performance.now() - this.t0) / 1000), text });
      if (this.logs.length > 80) this.logs.pop();
    }

    scale() { return (this.cfg.slowmo ? 0.3 : 1) * (this.cfg.night ? 0.6 : 1); }
    hallAt(k) {
      if (k === this.cfg.faultHall) return 0;
      return Math.abs(this.pos - k * this.cfg.pitch) <= this.cfg.hallZone / 2 ? 1 : 0;
    }

    isPrio(f) { return this.cfg.prioMask[f] === '1'; }

    call(f) {
      if (f < 0 || f >= this.cfg.floors || this.state === 'fault') return false;
      if (this.calls.some((c) => c.f === f)) return true;
      this.calls.push({ f, t: this.seq++, prio: this.isPrio(f) });
      this.say(`Вызов с ${f + 1} этажа`);
      return true;
    }
    cancel(f) { this.calls = this.calls.filter((c) => c.f !== f); }

    setConfig(p) {
      const old = this.cfg.floors;
      Object.assign(this.cfg, p);
      this.cfg.floors = Math.max(2, Math.min(25, this.cfg.floors | 0));
      this.cfg.parkFloor = Math.max(1, Math.min(this.cfg.floors, this.cfg.parkFloor | 0));
      this.calls = this.calls.filter((c) => c.f < this.cfg.floors);
      if (this.cfg.floors !== old && this.target >= this.cfg.floors) this.target = -1;
    }

    command(name, arg) {
      switch (name) {
        case 'home': this.fault = ''; this.state = 'homing'; this.calls = []; this.target = -1; this.say('Ищу первый этаж.'); break;
        case 'stop': this.vel = 0; this.state = 'fault'; this.fault = 'Нажат аварийный стоп'; this.say('Аварийный стоп'); break;
        case 'clear': if (this.state === 'fault') { this.fault = ''; this.state = 'homing'; this.say('Ошибка сброшена, ищу первый этаж.'); } break;
        case 'calibrate':
          if (this.state === 'idle') { this.state = 'calibrating'; this.cal = { done: false, centers: [], progress: 0, phase: 'up' }; this.say('Калибровка: еду вверх.'); }
          break;
        case 'park': this.cfg.parkFloor = (arg | 0) + 1; this.cfg.parkEnabled = true; this.say(`Парковка: ${arg + 1} этаж`); break;
        case 'reset': Object.assign(this.cfg, DEFAULTS); this.say('Настройки сброшены'); break;
      }
    }

    step() {
      const now = performance.now();
      const dt = Math.min(0.1, (now - this.last) / 1000);
      this.last = now;
      this.update(dt);
    }

    update(dt) {
      const c = this.cfg, pitch = c.pitch, k = this.scale();
      const vmax = c.vmax * k, a = c.accel * k, creep = Math.min(c.creep, vmax);
      const topMM = (c.floors - 1) * pitch;

      // Проверка датчика: если Холл сломан, лифт замечает это, проезжая этаж.
      if ((this.state === 'moving' || this.state === 'calibrating') && c.faultHall >= 0 && c.faultHall < c.floors &&
          Math.abs(this.pos - c.faultHall * pitch) < 1.5) {
        this.vel = 0; this.state = 'fault';
        this.fault = `Не вижу датчик на ${c.faultHall + 1} этаже`;
        this.say(this.fault);
        return;
      }

      if (this.state === 'homing') {
        this.vel = -creep;
        this.pos += this.vel * dt;
        if (this.pos <= 0) { this.pos = 0; this.vel = 0; this.cur = 0; this.dir = 0; this.state = 'idle'; this.say('Нашёл первый этаж. Готов.'); }
        if (this.pos > topMM + 10) this.pos = topMM + 10;
      } else if (this.state === 'calibrating') {
        const up = this.cal.phase === 'up';
        this.vel = (up ? 1 : -1) * Math.max(creep, 30 * k);
        this.pos += this.vel * dt;
        this.cal.progress = Math.max(0, Math.min(1, this.pos / topMM));
        const idx = Math.round(this.pos / pitch);
        if (up && Math.abs(this.pos - idx * pitch) < 1 && this.cal.centers[idx] === undefined) {
          this.cal.centers[idx] = +(idx * pitch + (Math.random() - 0.5) * 0.8).toFixed(1);
        }
        if (up && this.pos >= topMM) { this.pos = topMM; this.cal.phase = 'down'; }
        if (!up && this.pos <= 0) {
          this.pos = 0; this.vel = 0; this.cur = 0; this.state = 'idle';
          this.cal.done = true; this.say('Калибровка закончена.');
        }
      } else if (this.state === 'moving') {
        this.drive(dt, vmax, a, creep);
      } else if (this.state === 'dwell') {
        this.timer -= dt;
        if (this.timer <= 0) this.state = 'idle';
      } else if (this.state === 'idle') {
        this.idleFor += dt;
        this.think();
        this.demoTick(dt);
      }
    }

    think() {
      const c = this.cfg, pitch = c.pitch;
      const r = D.plan({ mode: c.mode, calls: this.calls, pos: this.pos / pitch, dir: this.dir, moving: false, brake: 0, target: -1, priorityOn: c.priorityOn });
      if (r.target >= 0) {
        this.idleFor = 0; this.parking = false;
        if (Math.abs(r.target * pitch - this.pos) < 0.5) { this.arrive(r.target); return; }
        this.target = r.target; this.dir = r.dir; this.state = 'moving';
        this.say(`Еду ${this.dir > 0 ? 'вверх' : 'вниз'} на ${this.target + 1} этаж`);
        return;
      }
      if (c.parkEnabled && this.idleFor > c.parkIdle && this.cur !== c.parkFloor - 1) {
        this.parking = true; this.target = c.parkFloor - 1;
        this.dir = this.target * pitch > this.pos ? 1 : -1; this.state = 'moving'; this.idleFor = 0;
        this.say(`Долго стоял, еду на парковку: ${this.target + 1} этаж`);
      }
    }

    demoTick(dt) {
      if (!this.cfg.demo) return;
      this.demoIn -= dt;
      if (this.demoIn <= 0) {
        this.demoIn = 2 + Math.random() * 5;
        this.call(Math.floor(Math.random() * this.cfg.floors));
      }
    }

    drive(dt, vmax, a, creep) {
      const pitch = this.cfg.pitch;
      const dirNow = this.vel > 0.1 ? 1 : this.vel < -0.1 ? -1 : this.dir;
      const brake = (this.vel * this.vel) / (2 * a) / pitch + 0.02;
      if (!this.parking) {
        const r = D.plan({ mode: this.cfg.mode, calls: this.calls, pos: this.pos / pitch, dir: dirNow, moving: true, brake, target: this.target, priorityOn: this.cfg.priorityOn });
        if (r.target >= 0 && r.target !== this.target) { this.target = r.target; this.say(`Теперь еду на ${this.target + 1} этаж`); }
        if (r.dir) this.dir = r.dir;
      } else if (this.calls.length) {
        this.parking = false;
      }
      const dist = this.target * pitch - this.pos;
      const sgn = Math.sign(dist) || 1;
      const ad = Math.abs(dist);
      const want = Math.max(Math.min(vmax, Math.sqrt(2 * a * ad)), Math.min(creep, ad * 3));
      const dv = sgn * want - this.vel;
      this.vel += Math.max(-a * dt, Math.min(a * dt, dv));
      this.pos += this.vel * dt;
      if (Math.sign(this.target * pitch - this.pos) !== sgn || Math.abs(this.target * pitch - this.pos) < 0.3) {
        this.pos = this.target * pitch;
        this.vel = 0;
        this.arrive(this.target);
      }
    }

    arrive(f) {
      this.cur = f; this.vel = 0; this.target = -1; this.parking = false;
      this.calls = this.calls.filter((c) => c.f !== f);
      this.state = 'dwell'; this.timer = this.cfg.dwell; this.idleFor = 0;
      // Если дальше вызовов нет, лифт запоминает последнее направление.
      this.say(`Приехал на ${f + 1} этаж`);
    }

    snapshot() {
      const c = this.cfg;
      const hall = [];
      for (let i = 0; i < c.floors; i++) hall.push(this.hallAt(i));
      const here = this.state === 'idle' || this.state === 'dwell' ? this.cur : -1;
      const led = [];
      for (let i = 0; i < c.floors; i++) {
        led.push(this.calls.some((x) => x.f === i) ? 'call' : i === here ? 'here' : 'off');
      }
      const floorF = this.pos / c.pitch;
      return {
        sim: true, floors: c.floors, pos: +this.pos.toFixed(1), vel: +this.vel.toFixed(1),
        floor: Math.max(0, Math.min(c.floors - 1, Math.round(floorF))),
        state: this.state, dir: this.dir, target: this.target,
        calls: this.calls.slice().sort((x, y) => x.t - y.t).map((x) => x.f),
        hall, led, fault: this.fault, parking: this.parking,
        counts: Math.round(this.pos * c.cpm), pwm: Math.round(Math.min(100, Math.abs(this.vel) / Math.max(1, c.vmax) * 80)),
        cal: this.cal, mode: c.mode, uptime: Math.round((performance.now() - this.t0) / 1000),
        parkFloor: c.parkEnabled ? c.parkFloor - 1 : -1,
      };
    }
  }

  root.SimLift = SimLift;
  root.LIFT_DEFAULTS = DEFAULTS;
})(typeof self !== 'undefined' ? self : this);
