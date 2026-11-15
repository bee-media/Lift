(function () {
  const $ = (s) => document.querySelector(s);
  const FH = 64; // высота этажа на экране, px

  // ---------- Связь с лифтом или симулятором ----------
  const Http = {
    sim: false,
    async state() { return (await fetch('/api/state', { cache: 'no-store' })).json(); },
    async config() { return (await fetch('/api/config', { cache: 'no-store' })).json(); },
    async setConfig(p) { await fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(p) }); },
    async call(f) { await fetch('/api/call?f=' + f, { method: 'POST' }); },
    async cmd(n, a) { await fetch('/api/cmd?name=' + n + '&arg=' + (a || 0), { method: 'POST' }); },
    async log() { return (await fetch('/api/log', { cache: 'no-store' })).json(); },
    // Лифту нужны часы для ночного режима: отдаём ему время этого браузера и часовой пояс.
    async syncTime() {
      await fetch('/api/time?t=' + Math.floor(Date.now() / 1000) + '&tz=' + (-new Date().getTimezoneOffset()), { method: 'POST' });
    },
  };
  let Sim = null;
  const SimApi = {
    sim: true,
    async state() { return Sim.snapshot(); },
    async config() { return Object.assign({}, Sim.cfg); },
    async setConfig(p) { Sim.setConfig(p); },
    async call(f) { Sim.call(f); },
    async cmd(n, a) { Sim.command(n, a); },
    async log() { return Sim.logs; },
    async syncTime() {},
    press(f, down) { Sim.press(f, down); },
  };
  let api = Http;
  let cfg = {};
  let st = null;
  let drawnFloors = 0;

  async function connect() {
    const forceSim = location.protocol === 'file:' || /[?&]sim\b/.test(location.search);
    if (!forceSim) {
      try {
        const ctl = new AbortController();
        const to = setTimeout(() => ctl.abort(), 2000);
        const r = await fetch('/api/state', { signal: ctl.signal, cache: 'no-store' });
        clearTimeout(to);
        if (r.ok) { api = Http; return; }
      } catch (e) { /* лифта нет, идём в симулятор */ }
    }
    Sim = new window.SimLift();
    api = SimApi;
  }

  // ---------- Тексты ----------
  const MODES = {
    down: ['Попутный вниз', 'Сначала еду к тому, кто нажал первым, вверх без остановок по пути, а вниз останавливаюсь у каждого, кто нажал по пути.'],
    fifo: ['По очереди', 'Еду к тем, кто нажал раньше, в порядке нажатий.'],
    both: ['Попутный в обе стороны', 'Забираю всех, кто по пути, и вверх, и вниз.'],
  };
  const N = (f) => (f + 1);

  function sentence(s) {
    switch (s.state) {
      case 'homing': return ['Ищу первый этаж…', 'Еду вниз потихоньку, пока датчик Холла не почувствует магнит кабины.'];
      case 'calibrating': return ['Учусь: запоминаю, где каждый этаж', 'Еду вверх и записываю, где сработал каждый датчик.'];
      case 'fault': return ['Что-то не так', s.fault || ''];
      case 'dwell': return [`Приехал на ${N(s.floor)} этаж`, 'Жду, пока выйдут и зайдут.'];
      case 'moving': {
        const up = s.vel >= 0 && s.dir >= 0;
        const why = s.mode === 'down'
          ? (up ? 'Вверх я без остановок: сначала доеду до того, кто нажал первым. Остальные подождут своей очереди, вниз заберу всех по пути.' : 'Вниз я останавливаюсь у каждого, кто нажал кнопку по пути.')
          : MODES[s.mode][1];
        return [`Еду ${up ? 'вверх' : 'вниз'} на ${N(s.target)} этаж`, why];
      }
      default: return [`Стою на ${N(s.floor)} этаже`, s.calls.length ? 'Думаю, куда ехать.' : 'Жду, пока кто-нибудь нажмёт кнопку.'];
    }
  }

  // ---------- Шахта ----------
  let shaftEls = [];
  function buildShaft(n) {
    const sh = $('#shaft');
    sh.innerHTML = '';
    sh.style.height = n * FH + 'px';
    shaftEls = [];
    for (let i = n - 1; i >= 0; i--) {
      const row = document.createElement('div');
      row.className = 'floor';
      row.style.top = (n - 1 - i) * FH + 'px';
      row.innerHTML = `<span class="num">${N(i)}</span><span class="hall"></span><span class="park"></span>`;
      const b = document.createElement('button');
      b.className = 'callbtn';
      b.textContent = N(i);
      b.setAttribute('aria-label', `Вызвать лифт на ${N(i)} этаж`);
      attachPress(b, i);
      row.appendChild(b);
      sh.appendChild(row);
      shaftEls[i] = { hall: row.querySelector('.hall'), btn: b, park: row.querySelector('.park') };
    }
    const cab = document.createElement('div');
    cab.className = 'cab'; cab.id = 'cab';
    cab.innerHTML = '<span id="cabnum">1</span><span class="magnet"></span>';
    sh.appendChild(cab);
    drawnFloors = n;
  }

  function attachPress(btn, f) {
    let timer = 0, fired = false;
    const clear = () => { clearTimeout(timer); timer = 0; };
    btn.addEventListener('pointerdown', () => {
      if (api.press) api.press(f, true);
      fired = false; clear();
      timer = setTimeout(async () => {
        fired = true; await api.cmd('park', f); toast(`Дом лифта: ${N(f)} этаж`); refreshCfg();
      }, (cfg.longPress || 5) * 1000);
    });
    ['pointerup', 'pointerleave', 'pointercancel'].forEach((e) => btn.addEventListener(e, () => { clear(); if (api.press) api.press(f, false); }));
    btn.addEventListener('click', () => { if (!fired) api.call(f); });
    btn.addEventListener('contextmenu', (e) => e.preventDefault());
  }

  function renderState(s) {
    if (s.floors !== drawnFloors) buildShaft(s.floors);
    const pitch = cfg.pitch || 100;
    // Ниже пола кабина опуститься не может: при поиске 1-го этажа расчётная высота может уйти в минус
    // (датчик не видит магнит), а на деле кабина уже стоит на упорах.
    const fl = Math.max(0, Math.min(s.floors - 1, s.pos / pitch));
    const cab = $('#cab');
    cab.style.top = ((s.floors - 1 - fl) * FH + 8) + 'px';
    $('#cabnum').textContent = N(s.floor);
    for (let i = 0; i < s.floors; i++) {
      const e = shaftEls[i];
      e.hall.classList.toggle('on', !!s.hall[i]);
      e.btn.classList.toggle('call', s.led[i] === 'call');
      e.btn.classList.toggle('here', s.led[i] === 'here');
      e.btn.classList.toggle('test', s.led[i] === 'test');
      e.park.textContent = s.parkFloor === i ? '🏠' : '';
    }
    // Шахта следует за кабиной
    const wrap = $('#shaftwrap');
    if (wrap.scrollHeight > wrap.clientHeight) {
      const want = (s.floors - 1 - fl) * FH - wrap.clientHeight / 2 + 40;
      if (s.state !== 'idle' || Math.abs(wrap.scrollTop - want) > wrap.clientHeight) wrap.scrollTop += (want - wrap.scrollTop) * 0.2;
    }
    const [t, w] = sentence(s);
    $('#status').textContent = t;
    $('#why').textContent = w;
    $('#k-floor').textContent = N(s.floor) + ' этаж';
    $('#k-target').textContent = s.target >= 0 ? N(s.target) + ' этаж' : '–';
    $('#k-speed').textContent = Math.abs(s.vel).toFixed(0) + ' мм/с';
    $('#k-pos').textContent = (s.pos / 10).toFixed(1) + ' см';
    $('#k-counts').textContent = s.counts + (s.noEnc ? ' (расчёт, энкодера нет)' : ' импульсов');
    $('#noenc').classList.toggle('hidden', !s.noEnc);
    $('#k-pwm').textContent = s.pwm + ' %';
    $('#k-mode').textContent = (MODES[s.mode] || ['?'])[0];
    $('#queue').innerHTML = s.calls.length ? s.calls.map((f) => `<span class="chip on">${N(f)}</span>`).join('') : '<span class="why">никто не ждёт</span>';
    const fault = s.state === 'fault';
    $('#faultcard').classList.toggle('hidden', !fault);
    $('#faulttext').textContent = s.fault || '';
    $('#btn-demo').textContent = cfg.demo ? 'Выключить показ' : 'Показать самому';
    $('#btn-slow').textContent = cfg.slowmo ? 'Обычная скорость' : 'Замедлить';
    const conn = $('#conn');
    conn.textContent = s.sim ? 'симулятор (лифт не подключён)' : 'лифт на связи';
    conn.className = 'badge ' + (s.sim ? '' : 'ok');
    renderCalib(s);
    renderIo(s);
    const ci = $('#clockinfo');
    if (ci) {
      ci.textContent = s.clock >= 0
        ? `Время на лифте: ${hhmm(s.clock)}.${s.nightNow ? ' Сейчас ночной режим: подсветка приглушена.' : ''}`
        : 'Часы лифта ещё не заданы: откройте панель из браузера (время придёт само) или подключите домашний Wi-Fi. Пока расписание не работает.';
    }
    const ns = $('#netstatus');
    if (ns) {
      ns.textContent = s.ip
        ? `Подключён к «${s.ssidNow}». Адрес лифта в этой сети: http://${s.ip}`
        : 'Домашняя сеть не подключена. Лифт всегда доступен через сеть LIFT-Setup (192.168.4.1).';
    }
  }

  // ---------- Калибровка ----------
  function renderCalib(s) {
    if ($('#tab-calib').classList.contains('hidden')) return;
    const prog = s.state === 'calibrating' ? (s.cal.progress || 0) : (s.cal && s.cal.done ? 1 : 0);
    $('#c-prog').style.width = prog * 100 + '%';
    let h = '<tr><th>Этаж</th><th>Датчик</th><th>Центр, мм</th></tr>';
    for (let i = s.floors - 1; i >= 0; i--) {
      const c = s.cal && s.cal.centers ? s.cal.centers[i] : undefined;
      h += `<tr><td>${N(i)}</td><td>${s.hall[i] ? '🔴 магнит рядом' : '⚪ нет'}</td><td>${c !== undefined ? c : '–'}</td></tr>`;
    }
    $('#c-table').innerHTML = h;
  }

  // ---------- Проверка кнопок и светодиодов ----------
  const hhmm = (m) => String(Math.floor(m / 60)).padStart(2, '0') + ':' + String(m % 60).padStart(2, '0');
  let ioOn = false, ioTimer = 0, ioDrawn = 0, ioTiles = [];
  function ioSet(on) {
    ioOn = on;
    clearInterval(ioTimer); ioTimer = 0;
    api.cmd('iotest', on ? 1 : 0);
    // Лифт сам выключит проверку через 90 с, поэтому пока вкладка открыта, подтверждаем каждые 30 с.
    if (on) ioTimer = setInterval(() => api.cmd('iotest', 1), 30000);
    $('#io-toggle').textContent = on ? 'Выключить проверку' : 'Включить проверку';
    $('#io-toggle').classList.toggle('primary', !on);
  }
  function renderIo(s) {
    if ($('#tab-calib').classList.contains('hidden')) { if (ioOn) ioSet(false); return; }
    if (ioDrawn !== s.floors) {
      const g = $('#io-grid');
      g.innerHTML = '';
      ioTiles = [];
      for (let i = 0; i < s.floors; i++) {
        const t = document.createElement('button');
        t.type = 'button'; t.className = 'iotile';
        t.innerHTML = `<b>${N(i)}</b><span class="d btn" title="кнопка"></span><span class="d hl" title="датчик Холла"></span>`;
        t.setAttribute('aria-label', `Мигнуть светодиодом ${N(i)} этажа`);
        t.onclick = () => { api.cmd('ledtest', i); toast(`Мигает светодиод ${N(i)} этажа`); };
        g.appendChild(t);
        ioTiles[i] = t;
      }
      ioDrawn = s.floors;
    }
    for (let i = 0; i < s.floors; i++) {
      const t = ioTiles[i];
      t.classList.toggle('pressed', !!(s.btn && s.btn[i]));
      t.classList.toggle('lit', s.led[i] === 'test');
      t.querySelector('.hl').classList.toggle('on', !!s.hall[i]);
    }
  }

  // ---------- Настройки ----------
  const SCHEMA = [
    ['Лифт', [
      { k: 'floors', l: 'Сколько этажей', t: 'num', min: 2, max: 25, step: 1 },
      { k: 'mode', l: 'Как лифт выбирает, куда ехать', t: 'sel', opts: Object.entries(MODES).map(([v, a]) => [v, a[0]]) },
      { k: 'dwell', l: 'Сколько стоять на этаже', t: 'range', min: 0.5, max: 20, step: 0.5, u: 'с' },
      { k: 'night', l: 'Ночной режим', h: 'Тише и медленнее, подсветка до «яркости ночью»', t: 'chk' },
    ]],
    ['Скорость', [
      { k: 'vmax', l: 'Максимальная скорость', t: 'range', min: 20, max: 300, step: 5, u: 'мм/с' },
      { k: 'accel', l: 'Разгон', h: 'Чем меньше, тем плавнее трогается. 5 = очень плавно, 1000 = рывком', t: 'range', min: 5, max: 1000, step: 5, u: 'мм/с²' },
      { k: 'decel', l: 'Торможение', h: 'Чем меньше, тем раньше лифт начинает тормозить и тем мягче останавливается', t: 'range', min: 5, max: 1000, step: 5, u: 'мм/с²' },
      { k: 'creep', l: 'Скорость подхода к этажу', h: 'Последние миллиметры едем медленно. Без энкодера ниже мощности на ходу не опустится', t: 'range', min: 1, max: 60, step: 1, u: 'мм/с' },
      { k: 'approach', l: 'Путь подхода', h: 'С какого расстояния до этажа ехать на скорости подхода. Больше = дольше ползёт, мягче останавливается', t: 'range', min: 1, max: 50, step: 1, u: 'мм' },
    ]],
    ['Дом лифта (парковка)', [
      { k: 'parkEnabled', l: 'Возвращаться домой', t: 'chk' },
      { k: 'parkFloor', l: 'Домашний этаж', t: 'num', min: 1, max: 25, step: 1 },
      { k: 'parkIdle', l: 'Через сколько секунд простоя ехать домой', t: 'num', min: 5, max: 600, step: 5, u: 'с' },
      { k: 'longPress', l: 'Сколько держать кнопку, чтобы задать дом', t: 'range', min: 1, max: 10, step: 0.5, u: 'с' },
    ]],
    ['Приоритет', [
      { k: 'priorityOn', l: 'Включить приоритетные этажи', h: 'Их вызовы обслуживаются раньше всех', t: 'chk' },
      { k: 'prioMask', l: 'Какие этажи приоритетные', t: 'prio' },
    ]],
    ['Подсветка', [
      { k: 'brightness', l: 'Яркость светодиодов', t: 'range', min: 5, max: 100, step: 5, u: '%' },
      { k: 'nightAuto', l: 'Приглушать подсветку ночью по расписанию', h: 'Только свет, скорость не меняется', t: 'chk' },
      { k: 'nightFrom', l: 'Ночь начинается', t: 'time' },
      { k: 'nightTo', l: 'Ночь заканчивается', t: 'time' },
      { k: 'nightBright', l: 'Яркость ночью', t: 'range', min: 1, max: 50, step: 1, u: '%' },
    ]],
    ['Режимы для показа', [
      { k: 'demo', l: 'Лифт сам вызывает себя', t: 'chk' },
      { k: 'slowmo', l: 'Замедленный показ', t: 'chk' },
      { k: 'faultHall', l: 'Сломать датчик на этаже (0 = не ломать)', h: 'Для урока: что делает лифт, если датчик не отвечает', t: 'num', min: 0, max: 25, step: 1, map: (v) => v + 1, unmap: (v) => v - 1 },
    ]],
    ['Железо (для взрослых)', [
      { k: 'pitch', l: 'Расстояние между этажами', t: 'num', min: 50, max: 150, step: 0.5, u: 'мм' },
      { k: 'hallZone', l: 'Ширина зоны датчика', t: 'num', min: 4, max: 40, step: 1, u: 'мм' },
      { k: 'cpm', l: 'Импульсов энкодера на 1 мм', h: 'Измеряется калибровкой', t: 'num', min: 0.5, max: 50, step: 0.01 },
      { k: 'useEnc', l: 'Энкодер подключён', h: 'Выключите, пока мотор без энкодера: положение считается по времени, остановка по датчикам этажей', t: 'chk', simHide: true },
      { k: 'openGain', l: 'Мощность на 1 мм/с (без энкодера)', h: 'Подбирается автоматически при калибровке', t: 'num', min: 0.05, max: 5, step: 0.01, simHide: true },
      { k: 'minDuty', l: 'Мощность старта мотора (без энкодера)', h: 'Ниже этого мотор не трогается. Если не крутит, увеличьте', t: 'range', min: 10, max: 90, step: 1, u: '%', simHide: true },
      { k: 'runDuty', l: 'Мощность на ходу (без энкодера)', h: 'Минимум, на котором мотор уже едет. Меньше старта = медленнее подход. Если кабина встаёт на ходу, увеличьте', t: 'range', min: 5, max: 90, step: 1, u: '%', simHide: true },
      { k: 'invMotor', l: 'Развернуть мотор', t: 'chk' },
      { k: 'invEnc', l: 'Развернуть энкодер', t: 'chk' },
      { k: 'invHall', l: 'Инвертировать датчики Холла', t: 'chk' },
      { k: 'kp', l: 'Регулятор скорости: P', t: 'num', min: 0, max: 20, step: 0.05 },
      { k: 'ki', l: 'Регулятор скорости: I', t: 'num', min: 0, max: 20, step: 0.05 },
    ]],
    ['Сеть', [
      { k: 'hostname', l: 'Имя в сети (имя.local)', t: 'txt', real: true },
      { k: 'ssid', l: 'Wi-Fi: название сети', t: 'txt', real: true },
      { k: 'pass', l: 'Wi-Fi: пароль', t: 'pwd', real: true, h: 'Пароль не показывается. Оставьте пустым, чтобы не менять' },
    ]],
  ];

  let saveTimer = 0, pending = {};
  function queueSave(k, v) {
    pending[k] = v;
    clearTimeout(saveTimer);
    saveTimer = setTimeout(async () => {
      const p = pending; pending = {};
      await api.setConfig(p);
      toast('Сохранено');
      if ('floors' in p || 'prioMask' in p) { await refreshCfg(); buildSettings(); }
      else await refreshCfg();
    }, 400);
  }
  async function refreshCfg() { cfg = await api.config(); }

  function buildSettings() {
    const root = $('#settingsForm');
    root.innerHTML = '';
    for (const [title, fields] of SCHEMA) {
      const card = document.createElement('div');
      card.className = 'card';
      card.innerHTML = `<h2>${title}</h2>`;
      for (const f of fields) {
        if ((f.real || f.simHide) && api.sim) continue;
        const raw = cfg[f.k];
        const val = f.map ? f.map(raw) : raw;
        const wrap = document.createElement('div');
        wrap.className = 'field';
        const lab = document.createElement('label');
        lab.innerHTML = f.l + (f.h ? `<small>${f.h}</small>` : '');
        wrap.appendChild(lab);
        let ctl;
        if (f.t === 'chk') {
          ctl = document.createElement('input'); ctl.type = 'checkbox'; ctl.checked = !!val;
          ctl.onchange = () => queueSave(f.k, ctl.checked);
        } else if (f.t === 'sel') {
          ctl = document.createElement('select');
          f.opts.forEach(([v, t]) => { const o = document.createElement('option'); o.value = v; o.textContent = t; ctl.appendChild(o); });
          ctl.value = val; ctl.onchange = () => queueSave(f.k, ctl.value);
        } else if (f.t === 'time') {
          ctl = document.createElement('input'); ctl.type = 'time'; ctl.value = hhmm(+val || 0);
          ctl.onchange = () => {
            const m = /^(\d\d):(\d\d)$/.exec(ctl.value);
            if (m) queueSave(f.k, (+m[1]) * 60 + (+m[2]));
          };
        } else if (f.t === 'range') {
          ctl = document.createElement('div');
          ctl.innerHTML = `<input type="range" min="${f.min}" max="${f.max}" step="${f.step}" value="${val}"><div class="val"></div>`;
          const r = ctl.querySelector('input'), o = ctl.querySelector('.val');
          const show = () => (o.textContent = r.value + ' ' + (f.u || ''));
          show();
          r.oninput = show; r.onchange = () => queueSave(f.k, +r.value);
        } else if (f.t === 'num') {
          ctl = document.createElement('input'); ctl.type = 'number'; ctl.min = f.min; ctl.max = f.max; ctl.step = f.step; ctl.value = val;
          ctl.onchange = () => { const v = +ctl.value; queueSave(f.k, f.unmap ? f.unmap(v) : v); };
        } else if (f.t === 'txt' || f.t === 'pwd') {
          ctl = document.createElement('input'); ctl.type = f.t === 'pwd' ? 'password' : 'text'; ctl.value = val || '';
          ctl.dataset.k = f.k;
          // Эти поля сохраняются кнопкой ниже, а не сами по себе.
          if (!f.real) ctl.onchange = () => queueSave(f.k, ctl.value);
        } else if (f.t === 'prio') {
          ctl = document.createElement('div'); ctl.className = 'chips'; ctl.style.gridColumn = '1 / -1';
          const mask = (raw || '').padEnd(cfg.floors, '0').split('');
          for (let i = 0; i < cfg.floors; i++) {
            const c = document.createElement('button');
            c.type = 'button'; c.className = 'chip' + (mask[i] === '1' ? ' on' : ''); c.textContent = N(i);
            c.onclick = () => { mask[i] = mask[i] === '1' ? '0' : '1'; c.classList.toggle('on'); queueSave(f.k, mask.join('')); };
            ctl.appendChild(c);
          }
          wrap.style.gridTemplateColumns = '1fr';
        }
        wrap.appendChild(ctl);
        card.appendChild(wrap);
      }
      if (title === 'Подсветка') {
        const ci = document.createElement('p');
        ci.className = 'why'; ci.id = 'clockinfo';
        card.appendChild(ci);
      }
      if (title === 'Сеть' && !api.sim) {
        const info = document.createElement('p');
        info.className = 'why'; info.id = 'netstatus';
        card.appendChild(info);
        const row = document.createElement('div');
        row.className = 'row';
        const b = document.createElement('button');
        b.className = 'btn primary'; b.textContent = 'Сохранить и подключить';
        b.onclick = async () => {
          const get = (k) => card.querySelector(`[data-k="${k}"]`).value.trim();
          const p = { hostname: get('hostname') || 'lift', ssid: get('ssid') };
          const pw = card.querySelector('[data-k="pass"]').value;
          if (pw) p.pass = pw;
          if (!p.ssid) return toast('Введите название сети');
          await api.setConfig(p);
          await api.cmd('wifi');
          toast('Подключаюсь к «' + p.ssid + '»…');
        };
        row.appendChild(b);
        card.appendChild(row);

        // Пароль собственной точки доступа LIFT-Setup
        const h = document.createElement('h2');
        h.style.marginTop = '18px'; h.textContent = 'Пароль точки LIFT-Setup';
        card.appendChild(h);
        const apInfo = document.createElement('p');
        apInfo.className = 'why';
        apInfo.textContent = cfg.apSecured
          ? 'Сейчас точка защищена паролем. Новый пароль заменит старый.'
          : 'Сейчас точка ОТКРЫТА: к ней может подключиться любой сосед. Задайте пароль.';
        if (!cfg.apSecured) apInfo.style.color = 'var(--bad)';
        card.appendChild(apInfo);
        const apWrap = document.createElement('div');
        apWrap.className = 'field';
        apWrap.innerHTML = '<label>Новый пароль<small>От 8 до 63 символов</small></label>';
        const apIn = document.createElement('input');
        apIn.type = 'password'; apIn.autocomplete = 'new-password';
        apWrap.appendChild(apIn);
        card.appendChild(apWrap);
        const apRow = document.createElement('div');
        apRow.className = 'row';
        const apBtn = document.createElement('button');
        apBtn.className = 'btn primary'; apBtn.textContent = 'Сохранить пароль';
        apBtn.onclick = async () => {
          const pw = apIn.value;
          if (pw.length < 8 || pw.length > 63) return toast('Пароль должен быть от 8 до 63 символов');
          if (!confirm('Лифт перезапустит свою сеть. Телефон отключится: подключитесь к LIFT-Setup заново с новым паролем. Продолжить?')) return;
          await api.setConfig({ apPass: pw });
          apIn.value = '';
          toast('Пароль сохранён. Подключитесь к LIFT-Setup заново');
        };
        apRow.appendChild(apBtn);
        card.appendChild(apRow);
      }
      if (card.children.length > 1) root.appendChild(card);
    }
  }

  // ---------- Сервис ----------
  async function renderLog() {
    if ($('#tab-service').classList.contains('hidden')) return;
    const rows = await api.log();
    $('#logbox').innerHTML = rows.map((r) => `<div>[${r.t} с] ${r.text}</div>`).join('');
  }
  function setupOta() {
    if (api.sim) {
      $('#otahint').textContent = 'Обновление работает, когда панель открыта с самого лифта. Сейчас показан симулятор.';
      $('#otaform').querySelectorAll('input,button').forEach((e) => (e.disabled = true));
      return;
    }
    $('#otaform').onsubmit = (e) => {
      e.preventDefault();
      const file = $('#otafile').files[0];
      if (!file) return toast('Выберите файл .bin');
      const x = new XMLHttpRequest();
      x.open('POST', '/update');
      x.upload.onprogress = (ev) => { if (ev.lengthComputable) $('#ota-prog').style.width = (ev.loaded / ev.total * 100) + '%'; };
      x.onload = () => toast(x.status === 200 ? 'Готово, лифт перезагружается' : 'Ошибка обновления');
      const fd = new FormData(); fd.append('firmware', file, file.name);
      x.send(fd);
    };
  }

  // ---------- Мелочи ----------
  let toastTimer = 0;
  function toast(t) {
    const e = $('#toast'); e.textContent = t; e.classList.add('show');
    clearTimeout(toastTimer); toastTimer = setTimeout(() => e.classList.remove('show'), 1600);
  }

  function setupTabs() {
    document.querySelectorAll('#tabs button').forEach((b) => {
      b.onclick = () => {
        document.querySelectorAll('#tabs button').forEach((x) => x.setAttribute('aria-selected', String(x === b)));
        ['play', 'settings', 'calib', 'service'].forEach((n) => $('#tab-' + n).classList.toggle('hidden', n !== b.dataset.tab));
        if (b.dataset.tab === 'settings') refreshCfg().then(buildSettings);
        if (b.dataset.tab === 'service') renderLog();
      };
    });
    $('#btn-clear').onclick = () => api.cmd('clear');
    $('#btn-demo').onclick = async () => { await api.setConfig({ demo: !cfg.demo }); await refreshCfg(); };
    $('#btn-slow').onclick = async () => { await api.setConfig({ slowmo: !cfg.slowmo }); await refreshCfg(); };
    $('#c-home').onclick = () => api.cmd('home');
    $('#c-cal').onclick = () => api.cmd('calibrate');
    $('#c-stop').onclick = () => api.cmd('stop');
    $('#io-toggle').onclick = () => ioSet(!ioOn);
    $('#io-chase').onclick = () => { api.cmd('ledtest', 100); toast('Светодиоды загораются по очереди'); };
    $('#mt-fwd').onclick = async () => {
      if (api.sim) return toast('В симуляторе мотора нет');
      await api.cmd('motortest', 1); toast('Мотор крутится вперёд 1 секунду');
    };
    $('#mt-back').onclick = async () => {
      if (api.sim) return toast('В симуляторе мотора нет');
      await api.cmd('motortest', -1); toast('Мотор крутится назад 1 секунду');
    };
    $('#btn-reboot').onclick = async () => {
      if (api.sim) return toast('В симуляторе перезагружать нечего');
      if (confirm('Перезагрузить лифт? Он снова будет искать первый этаж.')) { await api.cmd('reboot'); toast('Перезагружаюсь…'); }
    };
    $('#btn-reset').onclick = async () => {
      if (confirm('Вернуть все настройки как на заводе?')) { await api.cmd('reset'); await refreshCfg(); buildSettings(); toast('Сброшено'); }
    };
  }

  async function tick() {
    try {
      st = await api.state();
      renderState(st);
      document.body.dataset.online = '1';
    } catch (e) {
      const conn = $('#conn'); conn.textContent = 'нет связи с лифтом'; conn.className = 'badge bad';
    }
    setTimeout(tick, api.sim ? 80 : 200);
  }

  (async function init() {
    setupTabs();
    await connect();
    await refreshCfg();
    setupOta();
    api.syncTime().catch(() => {});
    setInterval(() => api.syncTime().catch(() => {}), 5 * 60 * 1000);
    buildShaft(cfg.floors || 5);
    tick();
    setInterval(renderLog, 1500);
    window.__lift = { get api() { return api; }, get sim() { return Sim; } };
  })();
})();
