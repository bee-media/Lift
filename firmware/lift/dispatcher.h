// Диспетчер: выбирает, куда ехать. Точный порт web/src/dispatcher.js.
// Правила менять сразу в обоих файлах, тесты лежат в tests/dispatcher.test.js.
#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"

struct DCall { int f; int32_t t; bool prio; };

struct DPlanIn {
  uint8_t mode;      // 0 вниз, 1 по очереди, 2 в обе стороны
  const DCall* calls;
  int n;
  float pos;         // этажи, дробное
  int dir;           // -1, 0, +1
  bool moving;
  float brake;       // путь торможения в этажах
  int target;        // текущая цель или -1
  bool priorityOn;
};

struct DPlanOut { int target; int dir; };

static const float D_EPS = 1e-6f;

static inline int dSign(float f, float pos) {
  return f > pos + D_EPS ? 1 : (f < pos - D_EPS ? -1 : 0);
}

static DPlanOut dispatchPlan(const DPlanIn& s) {
  DPlanOut keep = { (s.moving && s.target >= 0) ? s.target : -1, s.dir };
  if (s.n == 0) return { -1, 0 };

  DCall cand[MAX_FLOORS];
  int m = 0;
  for (int i = 0; i < s.n; i++) {
    int f = s.calls[i].f;
    bool ok;
    if (!s.moving || !s.dir) ok = true;
    else if (f == s.target) ok = true;  // до текущей цели уже тормозим
    else ok = (float)s.dir * ((float)f - s.pos) >= s.brake - D_EPS;
    if (ok) cand[m++] = s.calls[i];
  }
  if (m == 0) return keep;

  // Без энкодера расчётное положение может чуть проскочить центр цели, пока датчик ещё не подтвердил этаж.
  // Тогда цель всё равно считаем впереди, иначе лифт «забудет» её и поедет к другому вызову мимо.
  float pos = s.pos;
  if (s.moving && s.dir && s.target >= 0) {
    float over = (float)s.dir * ((float)s.target - s.pos);
    bool has = false;
    for (int i = 0; i < m; i++) if (cand[i].f == s.target) has = true;
    if (over < 0 && over > -0.5f && has) pos = (float)s.target - (float)s.dir * 1e-3f;
  }

  auto oldest = [&](const DCall* a, int cnt) {
    int b = 0;
    for (int i = 1; i < cnt; i++) if (a[i].t < a[b].t) b = i;
    return a[b];
  };
  auto nearest = [&](const DCall* a, int cnt) {
    int b = 0;
    for (int i = 1; i < cnt; i++) {
      float di = fabsf((float)a[i].f - pos), db = fabsf((float)a[b].f - pos);
      if (di < db || (di == db && a[i].t < a[b].t)) b = i;
    }
    return a[b];
  };
  auto go = [&](int f, int dir, bool useDir) -> DPlanOut {
    return { f, useDir ? dir : dSign((float)f, pos) };
  };

  if (s.priorityOn) {
    DCall pr[MAX_FLOORS]; int np = 0;
    for (int i = 0; i < m; i++) if (cand[i].prio) pr[np++] = cand[i];
    if (np) return go(nearest(pr, np).f, 0, false);
  }

  if (!s.moving) {
    for (int i = 0; i < m; i++)
      if (fabsf((float)cand[i].f - pos) < 0.05f) return { cand[i].f, s.dir };
  }

  if (s.mode == 1) return go(oldest(cand, m).f, 0, false);

  int dir = s.moving ? s.dir : 0;
  if (!dir) {
    dir = dSign((float)oldest(cand, m).f, pos);
    if (!dir) dir = -1;
  }

  DCall above[MAX_FLOORS], below[MAX_FLOORS], ahead[MAX_FLOORS], back[MAX_FLOORS];
  int na = 0, nb = 0, nh = 0, nk = 0;
  for (int i = 0; i < m; i++) {
    float d = (float)cand[i].f - pos;
    if (d > D_EPS) above[na++] = cand[i];
    if (d < -D_EPS) below[nb++] = cand[i];
    if ((float)dir * d > D_EPS) ahead[nh++] = cand[i];
    if ((float)(-dir) * d > D_EPS) back[nk++] = cand[i];
  }

  if (s.mode == 2) {
    if (nh) return go(nearest(ahead, nh).f, dir, true);
    if (nk) return go(nearest(back, nk).f, -dir, true);
    return keep;
  }

  // Режим 0: порядок вызовов важнее всего. Вверх едем без остановок к САМОМУ РАННЕМУ вызову сверху,
  // вниз останавливаемся у каждого вызова по пути. Новые вызовы выше цели ждут своей очереди.
  auto topOf = [&](const DCall* a, int cnt) { int mx = a[0].f; for (int i = 1; i < cnt; i++) if (a[i].f > mx) mx = a[i].f; return mx; };
  if (dir > 0) {
    if (na) return go(oldest(above, na).f, 1, true);
    if (nb) return go(topOf(below, nb), -1, true);
  } else {
    if (nb) return go(topOf(below, nb), -1, true);
    if (na) return go(oldest(above, na).f, 1, true);
  }
  return keep;
}
