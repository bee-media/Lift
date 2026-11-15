"""Детали шахты: этажная секция (ромбовидные окна), база с дверным проёмом, потолок, кронштейны, шкивы."""
import params as P
from lib import box, cyl, cyl_x, cyl_y, union, prism_xz, prism_yz, prism_xy, keep_above

A = P.OUT / 2                    # 40.0  внешняя половина
B = P.IN_H                       # 37.6  внутренняя половина
S_OUT = B + P.HALF               # 38.7  внешняя грань шипа
K_IN = S_OUT + P.CL              # 38.9  внутренняя грань гнезда


def ring(a_out, a_in, z0, z1):
    return box(-a_out, a_out, -a_out, a_out, z0, z1) - box(-a_in, a_in, -a_in, a_in, z0 - 1, z1 + 1)


# ---------------- направляющие ----------------
def _slant_cut(prism, axis, z_bottom, z_top):
    """Косые торцы направляющей (угол 45° от стенки, печать без поддержек).

    axis: 'x' (боковая стенка x=+B) или 'y' (задняя стенка y=+B).
    Снизу: z >= z_bottom + d, сверху: z <= z_top + d - зазор, где d — вылет от стенки.
    """
    nx, ny = (1, 0) if axis == 'x' else (0, 1)
    if z_bottom is not None:
        prism = keep_above(prism, (nx, ny, 1), (z_bottom + B))
    if z_top is not None:
        prism = keep_above(prism, (-nx, -ny, -1), -(z_top + B - P.RAIL_JOINT_GAP))
    return prism


def side_ribs(z0, z1, bottom_cut, top_cut):
    """Рёбра кабины на левой и правой стенке. z0..z1 — габарит заготовки до косых срезов."""
    yc = P.CAB_Y
    r = box(B - P.CR_H, B + 0.4, yc - P.CR_T / 2, yc + P.CR_T / 2, z0, z1 + P.CR_H)
    r = _slant_cut(r, 'x', z0 if bottom_cut else None, z1 if top_cut else None)
    return r + r.mirror((1, 0, 0))


def t_rails(z0, z1, bottom_cut, top_cut):
    """Два Т-рельса противовеса на задней стенке (x = ±WR_X)."""
    y_head1 = B - P.WR_STEM_H            # 34.6  — начало ножки
    y_head0 = y_head1 - P.WR_HEAD_H      # 32.6  — торец шляпки
    stem = box(P.WR_X - P.WR_STEM_W / 2, P.WR_X + P.WR_STEM_W / 2, y_head1 - 0.2, B + 0.4, z0, z1 + 5)
    head = box(P.WR_X - P.WR_HEAD_W / 2, P.WR_X + P.WR_HEAD_W / 2, y_head0, y_head1, z0, z1 + 5)
    r = stem + head
    r = _slant_cut(r, 'y', z0 if bottom_cut else None, z1 if top_cut else None)
    return r + r.mirror((1, 0, 0))


def rails(z0, z1, bottom_cut=True, top_cut=True):
    return side_ribs(z0, z1, bottom_cut, top_cut) + t_rails(z0, z1, bottom_cut, top_cut)


# ---------------- проём ----------------
def window(z0, z1):
    """Проём в передней стенке без дверей; верхние углы скошены (мост 44 мм не нужен)."""
    w, c = P.WIN_W / 2, P.WIN_CHAMF
    pts = [(-w, z0), (w, z0), (w, z1 - c), (w - c, z1), (-(w - c), z1), (-w, z1 - c)]
    return prism_xz(pts, -A - 2, -B + 0.6)


# ---------------- ромбовидные окна ----------------
# координаты (u, z): u — вдоль стенки; перед: u = x; бока: u = y (правая стенка), левая — зеркально.
# Окна обходят направляющие: боковые рёбра кабины (y = CAB_Y) и Т-рельсы задней стенки (x = ±WR_X).
ROWS = (31.0, 75.0)                                    # высоты центров рядов окон от низа секции (ромбы z 12..50 и 56..94)
FRONT_WIN = []                                         # спереди - только дверь
# (центр u, полуширина rx): задняя стенка обходит Т-рельсы (x = ±12), боковая - ребро кабины (y -9...-6)
BACK_WIN = [(-26.3, 10.5), (0.0, 7.5), (26.3, 10.5)]
SIDE_WIN = [(-23.5, 12.0), (16.5, 14.5)]               # передняя часть правой стенки закрыта вырезом для пайки


def _diamond(u, z, rx, rz):
    return prism_xz([(u - rx, z), (u, z - rz), (u + rx, z), (u, z + rz)], -A - 2, -B + 0.6)


def wall_windows():
    rz = P.DIA_RZ
    back = union([_diamond(u, z, rx, rz) for z in ROWS for u, rx in BACK_WIN])
    left = union([_diamond(-u, z, rx, rz) for z in ROWS for u, rx in SIDE_WIN])
    right = union([_diamond(u, z, rx, rz) for z in ROWS for u, rx in SIDE_WIN if u > 0])
    return union([back.rotate((0, 0, 180)), right.rotate((0, 0, 90)), left.rotate((0, 0, -90))])


# ---------------- дверь, наличник, панель вызова, площадка ----------------
def _frame_polys():
    w, c, b = P.WIN_W / 2, P.WIN_CHAMF, P.FRAME_B
    z0, z1 = P.SILL_Z, P.SILL_Z + P.DOOR_H
    outer = [(-(w + b), z0), (w + b, z0), (w + b, z1 + b - c), (w + b - c, z1 + b),
             (-(w + b - c), z1 + b), (-(w + b), z1 + b - c)]
    inner = [(-w, z0 - 1), (w, z0 - 1), (w, z1 - c), (w - c, z1), (-(w - c), z1), (-w, z1 - c)]
    return outer, inner


def landing():
    """Маленькая площадка перед дверью. Снизу 45°-подкос — печать без поддержек."""
    d, t, zs = P.LAND_D, P.LAND_T, P.SILL_Z
    pts = [(-A + 0.6, zs), (-A - d, zs), (-A - d, zs - t), (-A, zs - t - d), (-A + 0.6, zs - t - d)]
    return prism_yz(pts, -P.LAND_W / 2, P.LAND_W / 2)


def call_panel():
    """Панель вызова справа от двери: нижняя грань скошена 45°, отверстия под кнопку и светодиод."""
    z0, z1 = P.BTN_Z - 9, P.PANEL_TOP
    p = P.PANEL_P
    pts = [(-A + 0.4, z0), (-A + 0.4, z1), (-A - p, z1), (-A - p, z0 + p + 0.4)]
    return prism_yz(pts, P.PANEL_X0, P.PANEL_X1)


def call_holes():
    px = (P.PANEL_X0 + P.PANEL_X1) / 2
    y0, y1 = -A - P.PANEL_P - 1, -B + 1.0
    return cyl_y(P.BTN_D / 2, y0, y1, px, P.BTN_Z, 40) + cyl_y(P.LED_D / 2, y0, y1, px, P.LED_Z, 40)


def call_groove():
    """Паз на внутренней стороне стенки напротив панели: кнопка и светодиод утапливаются в него,
    спереди остаётся шкурка CALL_SKIN с отверстиями. Потолок паза скошен 45° (без поддержек)."""
    px = (P.PANEL_X0 + P.PANEL_X1) / 2
    w = P.CALL_GROOVE_W / 2
    y_out = -A - P.PANEL_P + P.CALL_SKIN
    d = -B - y_out
    z0, z1 = P.BTN_Z - 5, P.LED_Z + 9
    pts = [(-B + 1.0, z1 + 1.0), (y_out, z1 - d), (y_out, z0), (-B + 1.0, z0)]
    return prism_yz(pts, px - w, px + w)


def solder_window():
    """Вырез в правой боковой стенке у кнопок: доступ для пайки и крепления кнопки, выход проводов в нишу.
    Перекрывает ромбы этой стенки. Верх скошен 45°."""
    c = P.SOLD_CHAMF
    y0, y1, z0, z1 = P.SOLD_Y0, P.SOLD_Y1, P.SOLD_Z0, P.SOLD_Z1
    pts = [(y0, z0), (y1, z0), (y1, z1 - c), (y1 - c, z1), (y0 + c, z1), (y0, z1 - c)]
    return prism_yz(pts, B - 0.6, A + 1)


def cable_duct():
    """Внешняя кабельная ниша на правой стенке: передняя стенка (сторона портала), задняя, крючок в бок."""
    x0, x1 = A - 0.4, A + P.DUCT_D
    t, yb = P.DUCT_T, P.DUCT_Y_BACK
    front = box(x0, x1, -A, -A + t, 0, P.H)
    rear = box(x0, x1, yb - t, yb, 0, P.H)
    xe, ya, tt = A + P.HOOK_ARM, P.HOOK_Y0, P.HOOK_T
    pts = [(x0, ya), (xe, ya), (xe, ya + P.HOOK_LIP), (xe - tt, ya + P.HOOK_LIP), (xe - tt, ya + tt), (x0, ya + tt)]
    hook = prism_xy(pts, P.HOOK_Z, P.HOOK_Z + P.HOOK_H)
    return front + rear + hook


# ---------------- этажная секция ----------------
def floor_section():
    """Повторяемая секция-этаж: дверь без створок, наличник, маленькая площадка, панель вызова
    (отв. под кнопку и светодиод), крупные ромбовидные окна. Печать: стоя, как есть."""
    walls = (ring(A, B, P.J, P.H)           # полная стенка
             + ring(A, K_IN, 0, P.J)        # гнездо: только внешняя половина
             + ring(S_OUT, B, P.H, P.H + P.J - P.SPIGOT_RELIEF))  # шип: только внутренняя половина
    walls = walls - window(P.SILL_Z, P.SILL_Z + P.DOOR_H) - wall_windows()
    outer, inner = _frame_polys()
    frame = prism_xz(outer, -A - P.FRAME_P, -A + 0.4) - prism_xz(inner, -A - P.FRAME_P - 1, -A + 1)
    body = walls + frame + landing() + call_panel() + cable_duct()
    body = body - call_groove() - call_holes() - solder_window()
    return body + rails(P.J, P.H + P.J)


# ---------------- дно шахты ----------------
def dno():
    """Плоское дно 80x80 (печать: как есть). Шип под гнездо первой секции, «язык» направляющих
    и два упора, на которые садится кабина (пол кабины вровень с порогом первого этажа)."""
    d = P.DNO_T
    body = box(-A, A, -A, A, 0, d) + ring(S_OUT, B, d, d + P.J - P.SPIGOT_RELIEF)
    for sx in (-20.0, 20.0):
        body = body + box(sx - 4, sx + 4, P.CAB_Y - 12, P.CAB_Y - 4, d - 0.5, d + P.CAB_STOP - P.STOP_DROP)
    # боковой паз под шлейф (со стороны панели вызова): открыт к правому краю плиты, режет и шип
    body = body - box(P.DNO_HOLE_X0, A + 1, P.DNO_HOLE_Y0, P.DNO_HOLE_Y1, -1, d + P.J + 1)
    return body + rails(d, d + P.J, bottom_cut=False, top_cut=True)


# ---------------- крышка шахты ----------------
def cap():
    """Крышка: гнездо + стенки + крыша с отверстиями под нить. Сборочная ориентация
    (при печати переворачивается крышей вниз)."""
    z_roof0 = P.J + P.CAP_RING_H
    body = ring(A, K_IN, 0, P.J) + ring(A, B, P.J, z_roof0)
    body = body + box(-A, A, -A, A, z_roof0, z_roof0 + P.ROOF_T)
    for y in (P.CAB_Y, P.CW_Y):
        body = body - cyl(P.STRING_HOLE / 2, z_roof0 - 1, z_roof0 + P.ROOF_T + 1, 0, y, 32)
        for sx in (-12, 12):   # гнёзда под штифты из filament 1.75 мм
            body = body - cyl(0.95, z_roof0 + P.ROOF_T - 4, z_roof0 + P.ROOF_T + 1, sx, y, 24)
    return body


def roof_top():
    return P.J + P.CAP_RING_H + P.ROOF_T


# ---------------- кронштейн и шкив ----------------
def pulley_y():
    return (P.CAB_Y + P.CW_Y) / 2


def bracket():
    """Кронштейн шкива: плита + две щеки. Ставится на крышу (штифты из пруткового
    пластика 1.75 мм). Печать: плитой вниз."""
    R, yp = P.PULLEY_D / 2, pulley_y()
    y0, y1 = yp - R - 3, yp + R + 3
    plate_t = 3.0
    z_ax = plate_t + R + 3
    plate = box(-15, 15, y0, y1, 0, plate_t)
    cheeks = None
    for sgn in (1, -1):
        xa, xb = sorted((sgn * (P.PULLEY_W / 2 + 1), sgn * (P.PULLEY_W / 2 + 4)))
        base_box = box(xa, xb, y0, y1, 0, plate_t)
        disc = cyl_x(10, xa, xb, yp, z_ax, 48)
        # щека: выпуклая оболочка основания и диска оси
        import manifold3d as m
        ch = m.Manifold.batch_hull([base_box, disc])
        ch = ch - cyl_x(P.AXLE_D / 2, xa - 1, xb + 1, yp, z_ax, 32)
        cheeks = ch if cheeks is None else cheeks + ch
    br = plate + cheeks
    for y in (P.CAB_Y, P.CW_Y):
        br = br - cyl(P.STRING_HOLE / 2, -1, plate_t + 1, 0, y, 32)
        for sx in (-12, 12):
            br = br - cyl(0.95, -1, plate_t + 1, sx, y, 24)
    return br, z_ax


def pulley():
    """Шкив с V-канавкой, ось вдоль X (сборочная ориентация). Печать: лёжа на торце."""
    import numpy as np
    import manifold3d as m
    R, w = P.PULLEY_D / 2, P.PULLEY_W / 2
    h = P.AXLE_D / 2
    prof = [(h, -w), (R, -w), (R, -1.5), (R - P.PULLEY_GROOVE, 0), (R, 1.5), (R, w), (h, w)]
    wheel = m.Manifold.revolve(m.CrossSection([np.array(prof, dtype=float)]), 96)
    return wheel.rotate((0, 90, 0))   # ось Z -> ось X
