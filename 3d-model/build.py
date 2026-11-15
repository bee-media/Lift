"""Сборка всех STL. Запуск: python3 build.py [число_этажных_секций]

out/print/*.stl  — детали в ориентации для печати (стоят на столе правильной стороной)
out/assembly_N.stl — вся шахта в сборе (для просмотра), N — число этажных секций
"""
import os
import sys
import params as P
import shaft as S
import car as C
import top as T
from lib import save_stl, union

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out')
PR = os.path.join(OUT, 'print')
os.makedirs(PR, exist_ok=True)


def on_bed(p):
    """Опустить деталь на z=0."""
    return p.translate((0, 0, -p.bounding_box()[2]))


br, z_ax = S.bracket()           # пассивный (без мотора)
bm, zm_ax = T.bracket_motor()     # под мотор N20
print_parts = {
    '1_dno_shakhty': S.dno(),
    '2_section_etazh': S.floor_section(),
    # потолок печатаем крышей вниз — крыша ложится на стол, без моста 75 мм
    '3_potolok_s_otverstiyami': on_bed(S.cap().rotate((180, 0, 0))),
    '4a_kronshtein_motor_N20': bm,
    '5a_shkiv_na_val_motora': on_bed(T.pulley_motor().rotate((0, -90, 0))),
    '4b_kronshtein_bez_motora': br,
    '5b_shkiv_na_os_M3': on_bed(S.pulley().rotate((0, -90, 0))),
    '6_cabin_kabina': C.cabin(),
    '7_counterweight_protivoves': C.counterweight(),
}
for name, p in print_parts.items():
    t = save_stl(p, os.path.join(PR, name + '.stl'))
    bb = t.bounds
    print(f'{name:32s} {bb[1][0]-bb[0][0]:6.1f} x {bb[1][1]-bb[0][1]:6.1f} x {bb[1][2]-bb[0][2]:6.1f} мм  '
          f'объём {t.volume/1000:6.1f} см3  watertight={t.is_watertight}')

# --- общий вид в сборке ---
D = P.DNO_T
parts = [S.dno()]
sec = S.floor_section()
parts += [sec.translate((0, 0, D + k * P.H)) for k in range(N)]
cap_z = D + N * P.H
parts.append(S.cap().translate((0, 0, cap_z)))
roof_top = cap_z + S.roof_top()
parts.append(bm.translate((0, 0, roof_top)))
parts.append(T.pulley_motor().translate((0, S.pulley_y(), roof_top + zm_ax)))
k_show = 1 if N > 1 else 0                      # кабина стоит напротив двери 2-го этажа
parts.append(C.cabin().translate((0, 0, D + k_show * P.H + P.CAB_STOP)))
cw_top = cap_z + P.J + P.CAP_RING_H - P.CW_Z - 0.5
parts.append(C.counterweight().translate((0, 0, cw_top - k_show * P.H)))
save_stl(union(parts), os.path.join(OUT, f'assembly_{N}floors.stl'))
print('готово:', OUT)
