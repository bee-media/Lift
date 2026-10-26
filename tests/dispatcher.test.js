const assert = require('node:assert');
const { test } = require('node:test');
const { plan } = require('../web/src/dispatcher.js');

const base = { mode: 'down', pos: 0, dir: 0, moving: false, brake: 0.3, target: -1, priorityOn: false };
const calls = (...fs) => fs.map((f, i) => ({ f, t: i, prio: false }));

test('нет вызовов: стоим', () => {
  assert.deepStrictEqual(plan({ ...base, calls: [] }), { target: -1, dir: 0 });
});

test('простой: едем на самый старый вызов, если он выше, то на самый верхний', () => {
  const r = plan({ ...base, pos: 2, calls: calls(5, 3, 7) });
  assert.deepStrictEqual(r, { target: 7, dir: 1 });
});

test('вверх: вызовы ниже ждут, цель остаётся верхней', () => {
  const r = plan({ ...base, pos: 2.5, dir: 1, moving: true, target: 7, calls: calls(7, 1, 4) });
  assert.strictEqual(r.target, 7);
  assert.strictEqual(r.dir, 1);
});

test('вверх: новый вызов выше продлевает цель', () => {
  const r = plan({ ...base, pos: 2.5, dir: 1, moving: true, target: 5, calls: calls(5, 9) });
  assert.strictEqual(r.target, 9);
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
  assert.strictEqual(r.target, 6); // «вверх до верхнего», так что 6 и без приоритета
  const r2 = plan({ ...base, pos: 0, priorityOn: false, calls: [{ f: 2, t: 0, prio: false }, { f: 3, t: 1, prio: true }] });
  assert.strictEqual(r2.target, 3);
});

test('движущаяся кабина, все вызовы позади: сохраняем текущую цель', () => {
  const r = plan({ ...base, pos: 4.9, dir: -1, moving: true, target: 4, brake: 0.3, calls: calls(5) });
  assert.strictEqual(r.target, 4);
});
