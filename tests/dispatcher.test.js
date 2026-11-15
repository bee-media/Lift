const assert = require('node:assert');
const { test } = require('node:test');
const { plan, inWindow } = require('../web/src/dispatcher.js');

const base = { mode: 'down', pos: 0, dir: 0, moving: false, brake: 0.3, target: -1, priorityOn: false };
const calls = (...fs) => fs.map((f, i) => ({ f, t: i, prio: false }));

test('нет вызовов: стоим', () => {
  assert.deepStrictEqual(plan({ ...base, calls: [] }), { target: -1, dir: 0 });
});

test('простой: едем на самый старый вызов (порядок вызовов), остальные потом', () => {
  const r = plan({ ...base, pos: 2, calls: calls(5, 3, 7) });
  assert.deepStrictEqual(r, { target: 5, dir: 1 });
});

test('первый вызвал 10-й, второй 5-й: сначала на 10-й', () => {
  const r = plan({ ...base, pos: 0, calls: calls(9, 4) });
  assert.deepStrictEqual(r, { target: 9, dir: 1 });
});

test('первый вызвал 5-й, второй 10-й: сначала на 5-й, не пролетая его', () => {
  const r = plan({ ...base, pos: 0, calls: calls(4, 9) });
  assert.deepStrictEqual(r, { target: 4, dir: 1 });
});

test('вверх: вызовы ниже ждут, цель остаётся прежней', () => {
  const r = plan({ ...base, pos: 2.5, dir: 1, moving: true, target: 7, calls: calls(7, 1, 4) });
  assert.strictEqual(r.target, 7);
  assert.strictEqual(r.dir, 1);
});

test('вверх: новый вызов выше цели ждёт своей очереди, цель не меняется', () => {
  const r = plan({ ...base, pos: 2.5, dir: 1, moving: true, target: 5, calls: calls(5, 9) });
  assert.strictEqual(r.target, 5);
});

test('вниз: останавливаемся на ближайшем вызове по пути', () => {
  const r = plan({ ...base, pos: 8.2, dir: -1, moving: true, target: 1, calls: calls(1, 6, 3) });
  assert.deepStrictEqual(r, { target: 6, dir: -1 });
});

test('вниз: вызов, мимо которого уже не успеть затормозить, пропускается', () => {
  const r = plan({ ...base, pos: 6.1, dir: -1, moving: true, target: 3, brake: 0.3, calls: calls(3, 6) });
  assert.strictEqual(r.target, 3);
});

test('текущая цель остаётся целью, даже когда уже идёт торможение до неё', () => {
  const r = plan({ ...base, pos: 3.2, dir: -1, moving: true, target: 3, brake: 0.25, calls: calls(3, 1) });
  assert.strictEqual(r.target, 3);
});

test('вниз закончили, остался вызов выше: разворот вверх', () => {
  const r = plan({ ...base, pos: 1, dir: -1, moving: false, calls: calls(4) });
  assert.deepStrictEqual(r, { target: 4, dir: 1 });
});

test('вызов на текущем этаже обслуживается сразу', () => {
  const r = plan({ ...base, pos: 3, calls: calls(3, 6) });
  assert.strictEqual(r.target, 3);
});

test('режим по очереди: самый старый, даже если дальше', () => {
  const r = plan({ ...base, mode: 'fifo', pos: 5, calls: calls(0, 6) });
  assert.strictEqual(r.target, 0);
});

test('режим в обе стороны: ближайший по ходу движения', () => {
  const r = plan({ ...base, mode: 'both', pos: 3.2, dir: 1, moving: true, target: 8, calls: calls(8, 5, 1) });
  assert.deepStrictEqual(r, { target: 5, dir: 1 });
});

test('приоритет: приоритетный этаж обслуживается раньше', () => {
  const cs = calls(2, 6);
  cs[1].prio = true;
  const r = plan({ ...base, pos: 0, priorityOn: true, calls: cs });
  assert.strictEqual(r.target, 6);
});

test('приоритет выключен: игнорируется', () => {
  const cs = calls(2, 6);
  cs[1].prio = true;
  const r = plan({ ...base, pos: 0, priorityOn: false, calls: cs });
  assert.strictEqual(r.target, 2); // приоритета нет: идём по порядку вызовов, сначала 2
  const r2 = plan({ ...base, pos: 0, priorityOn: false, calls: [{ f: 2, t: 0, prio: false }, { f: 3, t: 1, prio: true }] });
  assert.strictEqual(r2.target, 2);
});

test('движущаяся кабина, все вызовы позади: сохраняем текущую цель', () => {
  const r = plan({ ...base, pos: 4.9, dir: -1, moving: true, target: 4, brake: 0.3, calls: calls(5) });
  assert.strictEqual(r.target, 4);
});

test('ночное окно: обычное и через полночь', () => {
  const h = (x) => x * 60;
  assert.strictEqual(inWindow(h(12), h(9), h(17)), true);
  assert.strictEqual(inWindow(h(17), h(9), h(17)), false);   // конец не входит
  assert.strictEqual(inWindow(h(8), h(9), h(17)), false);
  assert.strictEqual(inWindow(h(23), h(21), h(7)), true);    // 21:00-07:00
  assert.strictEqual(inWindow(h(3), h(21), h(7)), true);
  assert.strictEqual(inWindow(h(7), h(21), h(7)), false);
  assert.strictEqual(inWindow(h(12), h(21), h(7)), false);
  assert.strictEqual(inWindow(h(5), h(6), h(6)), false);     // пустое окно
});

test('вниз: расчёт чуть проскочил цель (без энкодера), цель не теряется', () => {
  const r = plan({ ...base, moving: true, dir: -1, pos: 2.97, target: 3, brake: 0.02, calls: calls(3, 2, 1) });
  assert.deepStrictEqual(r, { target: 3, dir: -1 });
});

test('вверх: расчёт чуть проскочил цель, цель не теряется', () => {
  const r = plan({ ...base, moving: true, dir: 1, pos: 4.03, target: 4, brake: 0.02, calls: calls(4, 1) });
  assert.strictEqual(r.target, 4);
});

test('попутные вызовы: 5, 4, 3, 2 с 1-го этажа = вверх на 5, потом вниз с остановками на 4, 3, 2', () => {
  let pos = 0, dir = 0;
  const pending = [4, 3, 2, 1];
  const visited = [];
  let r = plan({ ...base, pos, calls: calls(...pending) });
  // первый вызов 5-го этажа поступил раньше остальных
  r = plan({ ...base, pos, calls: [{ f: 4, t: 0, prio: false }, { f: 3, t: 1, prio: false }, { f: 2, t: 2, prio: false }, { f: 1, t: 3, prio: false }] });
  assert.deepStrictEqual(r, { target: 4, dir: 1 });
  pos = 4; dir = 1;
  const left = [{ f: 3, t: 1 }, { f: 2, t: 2 }, { f: 1, t: 3 }].map((c) => ({ ...c, prio: false }));
  while (left.length) {
    r = plan({ ...base, pos, dir, calls: left });
    visited.push(r.target);
    pos = r.target; dir = r.dir;
    left.splice(left.findIndex((c) => c.f === r.target), 1);
  }
  assert.deepStrictEqual(visited, [3, 2, 1]);
});
