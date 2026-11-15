"""Верхняя часть: кронштейн с карманом под мотор N20 и шкив, насаживаемый на вал мотора."""
import numpy as np
import manifold3d as m
import params as P
import shaft as S
from lib import box, cyl, cyl_x


def bracket_motor():
    """Плита на потолок + пьедестал-люлька под N20. Вал мотора смотрит в -X и несёт шкив.
    Печать: плитой вниз, карман открыт вверх (поддержки не нужны). Возвращает (деталь, z оси вала)."""
    R, yp = P.PULLEY_D / 2, S.pulley_y()
    y0, y1 = yp - R - 3, yp + R + 3
    plate_t = 3.0
    z_ax = plate_t + R + 3
    fit = P.N20_FIT
    pw, ph = P.N20_W + 2 * fit, P.N20_H + 2 * fit
    pl = P.N20_L + fit
    yc = yp + P.N20_SHAFT_OFF                 # ось корпуса мотора
    x_face = 8.0                              # торец редуктора упирается сюда
    x_back = x_face + pl
    wall = 2.0
    z_top = z_ax + 3.0                        # стенки люльки доходят чуть выше оси
    plate = box(-15, x_back + wall + 1, y0, y1, 0, plate_t)
    ped = box(x_face - wall, x_back + wall, yc - pw / 2 - wall, yc + pw / 2 + wall, 0, z_top)
    pocket = box(x_face, x_back, yc - pw / 2, yc + pw / 2, z_ax - ph / 2, z_top + 1)
    # торцевые борта (у вала и у контактов) - низкие: вал и контакты мотора лежат над ними, без отверстий
    z_rim = z_ax - P.N20_RIM
    rim_cut = (box(x_face - wall - 1, x_face, yc - pw / 2 - wall - 1, yc + pw / 2 + wall + 1, z_rim, z_top + 1)
               + box(x_back, x_back + wall + 1, yc - pw / 2 - wall - 1, yc + pw / 2 + wall + 1, z_rim, z_top + 1))
    br = plate + ped - pocket - rim_cut
    for y in (P.CAB_Y, P.CW_Y):
        br = br - cyl(P.STRING_HOLE / 2, -1, plate_t + 1, 0, y, 32)
        for sx in (-12, 12):
            br = br - cyl(0.95, -1, plate_t + 1, sx, y, 24)
    return br, z_ax


def pulley_motor():
    """Шкив с V-канавкой и D-образным отверстием под вал мотора. Ось вдоль X (сборочная ориентация)."""
    R, w = P.PULLEY_D / 2, P.PULLEY_W / 2
    prof = [(0, -w), (R, -w), (R, -1.5), (R - P.PULLEY_GROOVE, 0), (R, 1.5), (R, w), (0, w)]
    wheel = m.Manifold.revolve(m.CrossSection([np.array(prof, dtype=float)]), 96).rotate((0, 90, 0))
    r = P.N20_SHAFT_D / 2 + 0.1
    bore = cyl_x(r, -w - 1, w + 1, 0, 0, 40) ^ box(-w - 2, w + 2, -r - 1, P.N20_FLAT + 0.1, -r - 1, r + 1)
    return wheel - bore
