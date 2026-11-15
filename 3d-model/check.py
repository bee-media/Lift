"""Виртуальная проверка сборки: пересечения деталей, зазоры, остановки кабины у дверей."""
import sys
import params as P
import shaft as S
import car as C
from lib import union

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3   # число этажных секций (= этажей)
D = P.DNO_T

parts = {'dno': S.dno()}
sec = S.floor_section()
for k in range(N):
    parts[f'sec{k + 1}'] = sec.translate((0, 0, D + k * P.H))
cap_z = D + N * P.H
parts['cap'] = S.cap().translate((0, 0, cap_z))
shaft_all = union(parts.values())
roof_under = cap_z + P.J + P.CAP_RING_H
print(f'{N} этажей, высота шахты до потолка: {roof_under:.1f} мм')

bad = 0
names = list(parts)
for a, b in zip(names, names[1:]):
    v = (parts[a] ^ parts[b]).volume()
    print(f'  стык {a}-{b}: пересечение {v:.4f} мм3')
    bad += v > 1e-6

cabin, cw = C.cabin(), C.counterweight()
# остановки кабины: пол кабины вровень с порогом каждого этажа
assert abs(P.CAB_STOP + P.CAB_FLOOR - P.SILL_Z) < 1e-9
stops = [D + k * P.H + P.CAB_STOP for k in range(N)]
print('остановки кабины (низ кабины, мм):', [round(z, 1) for z in stops])

z_hi = roof_under - P.CAB_Z - 0.5
zs = sorted(set([stops[0] + i * 2.0 for i in range(int((z_hi - stops[0]) / 2.0) + 1)] + stops + [z_hi]))
hits_cab = 0
for z in zs:
    v = (cabin.translate((0, 0, z)) ^ shaft_all).volume()
    if v > 1e-6:
        hits_cab += 1
        print('  КОЛЛИЗИЯ кабина z=%.1f vol=%.3f' % (z, v))
zcw_hi = roof_under - P.CW_Z - 0.5
zs_cw = [D + i * 2.0 for i in range(int((zcw_hi - D) / 2.0) + 1)] + [zcw_hi]
hits_cw = 0
for z in zs_cw:
    v = (cw.translate((0, 0, z)) ^ shaft_all).volume()
    if v > 1e-6:
        hits_cw += 1
        print('  КОЛЛИЗИЯ противовес z=%.1f vol=%.3f' % (z, v))
vcc = max((cabin.translate((0, 0, a)) ^ cw.translate((0, 0, b))).volume()
          for a in (zs[0], zs[-1]) for b in (zs_cw[0], zs_cw[-1]))
print(f'кабина: {len(zs)} положений, коллизий {hits_cab}; противовес: {len(zs_cw)} положений, коллизий {hits_cw}; '
      f'кабина↔противовес {vcc:.3f}')
for z in stops[:2] + [z_hi]:
    print(f'  z={z:6.1f}: зазор кабина {cabin.translate((0, 0, z)).min_gap(shaft_all, 3.0):.3f}  '
          f'противовес(на той же высоте) {cw.translate((0, 0, z)).min_gap(shaft_all, 3.0):.3f}')
print('ИТОГ:', 'OK' if (bad == 0 and hits_cab == 0 and hits_cw == 0 and vcc < 1e-6) else 'ЕСТЬ ПРОБЛЕМЫ')
