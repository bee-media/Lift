// Диспетчер лифта. Чистая логика без железа.
// Те же правила реализованы в firmware/lift/dispatcher.h, любое изменение делается в обоих местах.
//
// plan(s) -> {target, dir}
//   s.mode      'down' | 'fifo' | 'both'
//   s.calls     [{f, t, prio}]  f = индекс этажа с 0, t = порядок поступления
//   s.pos       положение кабины в этажах (дробное)
//   s.dir       текущее направление движения (-1, 0, +1)
//   s.moving    едет ли кабина
//   s.brake     путь торможения в этажах (после него остановиться уже можно)
//   s.target    текущая цель (-1 если нет), сохраняется, если лучше выбрать нельзя
//   s.priorityOn
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.Dispatcher = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  const EPS = 1e-6;

  function plan(s) {
    const calls = s.calls || [];
    const keep = { target: s.moving && s.target >= 0 ? s.target : -1, dir: s.dir || 0 };
    if (!calls.length) return { target: -1, dir: 0 };

    // Впереди по ходу движения и дальше пути торможения.
    // Текущая цель всегда остаётся возможной: до неё уже тормозим.
    const reachable = (f) => {
      if (!s.moving || !s.dir) return true;
      if (f === s.target) return true;
      return s.dir * (f - s.pos) >= s.brake - EPS;
    };
    const cand = calls.filter((c) => reachable(c.f));
    if (!cand.length) return keep;

    const byAge = (a, b) => a.t - b.t;
    const near = (a, b) => Math.abs(a.f - s.pos) - Math.abs(b.f - s.pos) || a.t - b.t;
    const sign = (f) => (f > s.pos + EPS ? 1 : f < s.pos - EPS ? -1 : 0);
    const go = (f, dir) => ({ target: f, dir: dir !== undefined ? dir : sign(f) });

    // Приоритет: ближайший приоритетный вызов, остальные ждут.
    if (s.priorityOn) {
      const pr = cand.filter((c) => c.prio).sort(near);
      if (pr.length) return go(pr[0].f);
    }

    // Вызов на этаже, где кабина стоит, обслуживается сразу.
    if (!s.moving) {
      const here = cand.find((c) => Math.abs(c.f - s.pos) < 0.05);
      if (here) return { target: here.f, dir: s.dir || 0 };
    }

    if (s.mode === 'fifo') {
      return go(cand.slice().sort(byAge)[0].f);
    }

    let dir = s.moving ? s.dir : 0;
    if (!dir) dir = sign(cand.slice().sort(byAge)[0].f) || -1;

    if (s.mode === 'both') {
      const ahead = cand.filter((c) => dir * (c.f - s.pos) > EPS).sort(near);
      if (ahead.length) return go(ahead[0].f, dir);
      const back = cand.filter((c) => -dir * (c.f - s.pos) > EPS).sort(near);
      if (back.length) return go(back[0].f, -dir);
      return keep;
    }

    // 'down': вверх без остановок до самого верхнего вызова, вниз останавливаемся везде.
    const above = cand.filter((c) => c.f > s.pos + EPS);
    const below = cand.filter((c) => c.f < s.pos - EPS);
    const top = (arr) => arr.reduce((m, c) => (c.f > m ? c.f : m), -1);
    if (dir > 0) {
      if (above.length) return go(top(above), 1);
      if (below.length) return go(below.sort((a, b) => b.f - a.f)[0].f, -1);
    } else {
      if (below.length) return go(below.sort((a, b) => b.f - a.f)[0].f, -1);
      if (above.length) return go(top(above), 1);
    }
    return keep;
  }

  // Попадает ли время m (минут от полуночи) в окно [from, to); окно может переходить через полночь.
  // Для ночной подсветки. То же в firmware/lift/config.h (inWindow).
  function inWindow(m, from, to) {
    if (from === to) return false;
    if (from < to) return m >= from && m < to;
    return m >= from || m < to;
  }

  return { plan, inWindow };
});
