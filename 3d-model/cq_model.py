"""Параметрическая модель лифта на CadQuery (настоящие твёрдые тела B-rep, экспорт в STEP).

Размеры берутся из params.py - те же, что у STL-версии. Запуск:  python3 cq_model.py [N_этажей]
Результат: out/step/*.step (детали в позиции для печати) и out/step/assembly_Nfloors.step (сборка).
Система координат: X - ширина, Y - глубина (перед = -Y), Z - вверх, центр шахты в (0, 0).
"""
import math
import os
import sys

import cadquery as cq

import params as P

A = P.OUT / 2                   # 40.0  внешняя половина
B = P.IN_H                      # 37.6  внутренняя половина
S_OUT = B + P.HALF              # 38.7  внешняя грань шипа
K_IN = S_OUT + P.CL             # 38.9  внутренняя грань гнезда


# ------------------------------------------------------------------ примитивы
def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def cyl(r, z0, z1, x=0.0, y=0.0):
    """Вертикальный цилиндр."""
    return cq.Workplane("XY", origin=(x, y, z0)).circle(r).extrude(z1 - z0)


def cyl_x(r, x0, x1, y=0.0, z=0.0):
    return cq.Workplane("YZ", origin=(x0, 0, 0)).center(y, z).circle(r).extrude(x1 - x0)


def cyl_y(r, y0, y1, x=0.0, z=0.0):
    return cq.Workplane("XZ", origin=(0, y1, 0)).center(x, z).circle(r).extrude(y1 - y0)


def prism_xz(pts, y0, y1):
    """Многоугольник в плоскости XZ [(x, z)...], выдавлен по Y от y0 до y1."""
    return cq.Workplane("XZ", origin=(0, y1, 0)).polyline(pts).close().extrude(y1 - y0)


def prism_yz(pts, x0, x1):
    """Многоугольник в плоскости YZ [(y, z)...], выдавлен по X от x0 до x1."""
    return cq.Workplane("YZ", origin=(x0, 0, 0)).polyline(pts).close().extrude(x1 - x0)


def ring(a_out, a_in, z0, z1):
    return box(-a_out, a_out, -a_out, a_out, z0, z1).cut(box(-a_in, a_in, -a_in, a_in, z0 - 1, z1 + 1))


def mirror_x(w):
    return w.mirror("YZ")


def union(parts):
    r = parts[0]
    for p in parts[1:]:
        r = r.union(p)
    return r


def on_bed(w):
    return w.translate((0, 0, -w.val().BoundingBox().zmin))


# ------------------------------------------------------------------ направляющие
def _slab(u0, u1, a, b):
    """Полоса между линиями u+z=a и u+z=b для u в [u0, u1] (None = без ограничения)."""
    a = -1000.0 if a is None else a
    b = 1000.0 if b is None else b
    return [(u0, a - u0), (u1, a - u1), (u1, b - u1), (u0, b - u0)]


def side_ribs(z0, z1, bottom_cut, top_cut):
    """Рёбра кабины на боковых стенках, торцы срезаны под 45°."""
    yc = P.CAB_Y
    r = box(B - P.CR_H, B + 0.4, yc - P.CR_T / 2, yc + P.CR_T / 2, z0, z1 + P.CR_H)
    clip = prism_xz(_slab(B - P.CR_H - 1, B + 1,
                          z0 + B if bottom_cut else None,
                          z1 + B - P.RAIL_JOINT_GAP if top_cut else None), yc - 5, yc + 5)
    r = r.intersect(clip)
    return r.union(mirror_x(r))


def t_rails(z0, z1, bottom_cut, top_cut):
    """Два Т-рельса противовеса на задней стенке (x = ±WR_X)."""
    y_head1 = B - P.WR_STEM_H
    y_head0 = y_head1 - P.WR_HEAD_H
    stem = box(P.WR_X - P.WR_STEM_W / 2, P.WR_X + P.WR_STEM_W / 2, y_head1 - 0.2, B + 0.4, z0, z1 + 5)
    head = box(P.WR_X - P.WR_HEAD_W / 2, P.WR_X + P.WR_HEAD_W / 2, y_head0, y_head1, z0, z1 + 5)
    r = stem.union(head)
    clip = prism_yz(_slab(y_head0 - 1, B + 1,
                          z0 + B if bottom_cut else None,
                          z1 + B - P.RAIL_JOINT_GAP if top_cut else None), P.WR_X - 5, P.WR_X + 5)
    r = r.intersect(clip)
    return r.union(mirror_x(r))


def rails(z0, z1, bottom_cut=True, top_cut=True):
    return side_ribs(z0, z1, bottom_cut, top_cut).union(t_rails(z0, z1, bottom_cut, top_cut))


# ------------------------------------------------------------------ окна, дверь, площадка, панель
def door_opening(z0, z1):
    w, c = P.WIN_W / 2, P.WIN_CHAMF
    pts = [(-w, z0), (w, z0), (w, z1 - c), (w - c, z1), (-(w - c), z1), (-w, z1 - c)]
    return prism_xz(pts, -A - 2, -B + 0.6)


def _diamond(u, z, rx, rz):
    return prism_xz([(u - rx, z), (u, z - rz), (u + rx, z), (u, z + rz)], -A - 2, -B + 0.6)


ROWS = (31.0, 75.0)
BACK_WIN = [(-26.3, 10.5), (0.0, 7.5), (26.3, 10.5)]
SIDE_WIN = [(-23.5, 12.0), (16.5, 14.5)]


def _rot_z(w, ang):
    return w.rotate((0, 0, 0), (0, 0, 1), ang)


def wall_windows():
    rz = P.DIA_RZ
    back = union([_diamond(u, z, rx, rz) for z in ROWS for u, rx in BACK_WIN])
    left = union([_diamond(-u, z, rx, rz) for z in ROWS for u, rx in SIDE_WIN])
    right = union([_diamond(u, z, rx, rz) for z in ROWS for u, rx in SIDE_WIN if u > 0])
    return union([_rot_z(back, 180), _rot_z(right, 90), _rot_z(left, -90)])


def _frame_polys():
    w, c, b = P.WIN_W / 2, P.WIN_CHAMF, P.FRAME_B
    z0, z1 = P.SILL_Z, P.SILL_Z + P.DOOR_H
    outer = [(-(w + b), z0), (w + b, z0), (w + b, z1 + b - c), (w + b - c, z1 + b),
             (-(w + b - c), z1 + b), (-(w + b), z1 + b - c)]
    inner = [(-w, z0 - 1), (w, z0 - 1), (w, z1 - c), (w - c, z1), (-(w - c), z1), (-w, z1 - c)]
    return outer, inner


def landing():
    d, t, zs = P.LAND_D, P.LAND_T, P.SILL_Z
    pts = [(-A + 0.6, zs), (-A - d, zs), (-A - d, zs - t), (-A, zs - t - d), (-A + 0.6, zs - t - d)]
    return prism_yz(pts, -P.LAND_W / 2, P.LAND_W / 2)


def call_panel():
    z0, z1 = P.BTN_Z - 9, P.PANEL_TOP
    p = P.PANEL_P
    pts = [(-A + 0.4, z0), (-A + 0.4, z1), (-A - p, z1), (-A - p, z0 + p + 0.4)]
    return prism_yz(pts, P.PANEL_X0, P.PANEL_X1)


def call_holes():
    px = (P.PANEL_X0 + P.PANEL_X1) / 2
    y0, y1 = -A - P.PANEL_P - 1, -B + 1.0
    return cyl_y(P.BTN_D / 2, y0, y1, px, P.BTN_Z).union(cyl_y(P.LED_D / 2, y0, y1, px, P.LED_Z))


def call_groove():
    px = (P.PANEL_X0 + P.PANEL_X1) / 2
    w = P.CALL_GROOVE_W / 2
    y_out = -A - P.PANEL_P + P.CALL_SKIN
    d = -B - y_out
    z0, z1 = P.BTN_Z - 5, P.LED_Z + 9
    pts = [(-B + 1.0, z1 + 1.0), (y_out, z1 - d), (y_out, z0), (-B + 1.0, z0)]
    return prism_yz(pts, px - w, px + w)


def solder_window():
    """Вырез в правой боковой стенке у кнопок (перекрывает ромбы), верх скошен 45°."""
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
    hook = cq.Workplane("XY", origin=(0, 0, P.HOOK_Z)).polyline(pts).close().extrude(P.HOOK_H)
    return union([front, rear, hook])


# ------------------------------------------------------------------ детали шахты
def floor_section():
    """Секция-этаж: замок, дверь, наличник, площадка, панель вызова с пазом, ромбы, направляющие."""
    walls = union([ring(A, B, P.J, P.H),
                   ring(A, K_IN, 0, P.J),
                   ring(S_OUT, B, P.H, P.H + P.J - P.SPIGOT_RELIEF)])
    walls = walls.cut(door_opening(P.SILL_Z, P.SILL_Z + P.DOOR_H)).cut(wall_windows())
    outer, inner = _frame_polys()
    frame = prism_xz(outer, -A - P.FRAME_P, -A + 0.4).cut(prism_xz(inner, -A - P.FRAME_P - 1, -A + 1))
    body = union([walls, frame, landing(), call_panel(), cable_duct()])
    body = body.cut(call_groove()).cut(call_holes()).cut(solder_window())
    return body.union(rails(P.J, P.H + P.J))


def dno():
    d = P.DNO_T
    body = box(-A, A, -A, A, 0, d).union(ring(S_OUT, B, d, d + P.J - P.SPIGOT_RELIEF))
    for sx in (-20.0, 20.0):
        body = body.union(box(sx - 4, sx + 4, P.CAB_Y - 12, P.CAB_Y - 4, d - 0.5, d + P.CAB_STOP - P.STOP_DROP))
    body = body.cut(box(P.DNO_HOLE_X0, A + 1, P.DNO_HOLE_Y0, P.DNO_HOLE_Y1, -1, d + P.J + 1))
    return body.union(rails(d, d + P.J, bottom_cut=False, top_cut=True))


def roof_top():
    return P.J + P.CAP_RING_H + P.ROOF_T


def cap():
    z_roof0 = P.J + P.CAP_RING_H
    body = ring(A, K_IN, 0, P.J).union(ring(A, B, P.J, z_roof0))
    body = body.union(box(-A, A, -A, A, z_roof0, z_roof0 + P.ROOF_T))
    for y in (P.CAB_Y, P.CW_Y):
        body = body.cut(cyl(P.STRING_HOLE / 2, z_roof0 - 1, z_roof0 + P.ROOF_T + 1, 0, y))
        for sx in (-12, 12):
            body = body.cut(cyl(0.95, z_roof0 + P.ROOF_T - 4, z_roof0 + P.ROOF_T + 1, sx, y))
    return body


# ------------------------------------------------------------------ кабина и противовес
def cabin():
    w2, d2 = P.CAB_W / 2, P.CAB_D / 2
    y0, y1 = P.CAB_Y - d2, P.CAB_Y + d2
    body = box(-w2, w2, y0, y1, 0, P.CAB_Z)
    body = body.cut(box(-w2 + P.CAB_WALL, w2 - P.CAB_WALL, y0 - 1, y1 - P.CAB_WALL,
                        P.CAB_FLOOR, P.CAB_Z - P.CAB_WALL))
    body = body.cut(cyl(P.CAB_STRING_HOLE / 2, P.CAB_Z - P.CAB_WALL - 1, P.CAB_Z + 1, 0, P.CAB_Y))
    slot_bottom = B - P.CR_H - 0.6
    narrow = P.CR_T + 2 * P.RAIL_CL
    relief = narrow + 2.4
    for s in (1, -1):
        xa, xb = sorted((s * (w2 - 0.5), s * P.SHOE_L))
        shoe = box(xa, xb, P.CAB_Y - P.SHOE_W / 2, P.CAB_Y + P.SHOE_W / 2, 0, P.CAB_Z)
        sa, sb = sorted((s * slot_bottom, s * (P.SHOE_L + 1)))
        z_lo, z_hi = P.SHOE_END, P.CAB_Z - P.SHOE_END
        shoe = shoe.cut(box(sa, sb, P.CAB_Y - narrow / 2, P.CAB_Y + narrow / 2, -1, P.CAB_Z + 1))
        shoe = shoe.cut(box(sa, sb, P.CAB_Y - relief / 2, P.CAB_Y + relief / 2, z_lo, z_hi))
        body = body.union(shoe)
    return body


def counterweight():
    w2 = P.CW_W / 2
    y_back, y_front = P.CW_Y_BACK, P.CW_Y_BACK - P.CW_D
    body = box(-w2, w2, y_front, y_back, 0, P.CW_Z)
    c = P.RAIL_CL
    head1 = B - P.WR_STEM_H
    head0 = head1 - P.WR_HEAD_H
    for s in (1, -1):
        xc = s * P.WR_X
        body = body.cut(box(xc - P.WR_STEM_W / 2 - c, xc + P.WR_STEM_W / 2 + c, head1 + c - 0.1, y_back + 1, -1, P.CW_Z + 1))
        body = body.cut(box(xc - P.WR_HEAD_W / 2 - c, xc + P.WR_HEAD_W / 2 + c, head0 - c, head1 + c, -1, P.CW_Z + 1))
    pw = P.CW_POCKET_W / 2
    top = P.CW_Z - P.CW_POCKET_TOP
    body = body.cut(box(-pw, pw, y_front - 1, y_back - P.CW_POCKET_BACK, P.CW_POCKET_BOT, top))
    body = body.cut(cyl(P.CAB_STRING_HOLE / 2, top - 1, P.CW_Z + 1, 0, P.CW_Y))
    return body


# ------------------------------------------------------------------ верх: кронштейн и шкив
def pulley_y():
    return (P.CAB_Y + P.CW_Y) / 2


def _rope_and_pin_holes(br, plate_t):
    for y in (P.CAB_Y, P.CW_Y):
        br = br.cut(cyl(P.STRING_HOLE / 2, -1, plate_t + 1, 0, y))
        for sx in (-12, 12):
            br = br.cut(cyl(0.95, -1, plate_t + 1, sx, y))
    return br


def bracket_motor():
    R, yp = P.PULLEY_D / 2, pulley_y()
    y0, y1 = yp - R - 3, yp + R + 3
    plate_t = 3.0
    z_ax = plate_t + R + 3
    fit = P.N20_FIT
    pw, ph = P.N20_W + 2 * fit, P.N20_H + 2 * fit
    pl = P.N20_L + fit
    yc = yp + P.N20_SHAFT_OFF
    x_face = 8.0
    x_back = x_face + pl
    wall = 2.0
    z_top = z_ax + 3.0
    plate = box(-15, x_back + wall + 1, y0, y1, 0, plate_t)
    ped = box(x_face - wall, x_back + wall, yc - pw / 2 - wall, yc + pw / 2 + wall, 0, z_top)
    pocket = box(x_face, x_back, yc - pw / 2, yc + pw / 2, z_ax - ph / 2, z_top + 1)
    # торцевые борта (у вала и у контактов) - низкие: вал и контакты мотора лежат над ними, без отверстий
    z_rim = z_ax - P.N20_RIM
    rim_cut = union([box(x_face - wall - 1, x_face, yc - pw / 2 - wall - 1, yc + pw / 2 + wall + 1, z_rim, z_top + 1),
                     box(x_back, x_back + wall + 1, yc - pw / 2 - wall - 1, yc + pw / 2 + wall + 1, z_rim, z_top + 1)])
    br = plate.union(ped).cut(pocket).cut(rim_cut)
    return _rope_and_pin_holes(br, plate_t), z_ax


def _wheel(inner_r):
    """Колесо с V-канавкой (ось вдоль X), inner_r = радиус отверстия (0 = без отверстия)."""
    R, w = P.PULLEY_D / 2, P.PULLEY_W / 2
    prof = [(inner_r, -w), (R, -w), (R, -1.5), (R - P.PULLEY_GROOVE, 0), (R, 1.5), (R, w), (inner_r, w)]
    wheel = cq.Workplane("XZ").polyline(prof).close().revolve(360, (0, 0, 0), (0, 1, 0))
    return wheel.rotate((0, 0, 0), (0, 1, 0), 90)       # ось Z -> ось X


def pulley_motor():
    w = P.PULLEY_W / 2
    r = P.N20_SHAFT_D / 2 + 0.1
    bore = cyl_x(r, -w - 1, w + 1).intersect(box(-w - 2, w + 2, -r - 1, P.N20_FLAT + 0.1, -r - 1, r + 1))
    return _wheel(0).cut(bore)


def pulley():
    return _wheel(P.AXLE_D / 2)


def _hull_profile(y0, y1, plate_t, yp, z_ax, rc):
    """Контур щеки: основание-прямоугольник + диск оси (выпуклая оболочка с дугой), в координатах (y, z)."""
    def tangent(py, pz, sign):
        dy, dz = py - yp, pz - z_ax
        d = math.hypot(dy, dz)
        phi = math.atan2(dz, dy) + sign * math.acos(rc / d)
        return yp + rc * math.cos(phi), z_ax + rc * math.sin(phi)
    tr = tangent(y1, plate_t, +1)
    tl = tangent(y0, plate_t, -1)
    return tr, tl


def bracket():
    """Пассивный кронштейн (ось M3, без мотора)."""
    R, yp = P.PULLEY_D / 2, pulley_y()
    y0, y1 = yp - R - 3, yp + R + 3
    plate_t = 3.0
    z_ax = plate_t + R + 3
    rc = 10.0
    plate = box(-15, 15, y0, y1, 0, plate_t)
    tr, tl = _hull_profile(y0, y1, plate_t, yp, z_ax, rc)
    top = (yp, z_ax + rc)
    cheeks = []
    for sgn in (1, -1):
        xa, xb = sorted((sgn * (P.PULLEY_W / 2 + 1), sgn * (P.PULLEY_W / 2 + 4)))
        ch = (cq.Workplane("YZ", origin=(xa, 0, 0)).moveTo(y0, 0).lineTo(y1, 0).lineTo(y1, plate_t)
              .lineTo(*tr).threePointArc(top, tl).lineTo(y0, plate_t).close().extrude(xb - xa))
        ch = ch.cut(cyl_x(P.AXLE_D / 2, xa - 1, xb + 1, yp, z_ax))
        cheeks.append(ch)
    br = plate.union(cheeks[0]).union(cheeks[1])
    return _rope_and_pin_holes(br, plate_t), z_ax


# ------------------------------------------------------------------ экспорт
def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out', 'step')
    os.makedirs(out, exist_ok=True)
    bm, zm_ax = bracket_motor()
    parts = {
        '1_dno_shakhty': dno(),
        '2_section_etazh': floor_section(),
        '3_potolok_s_otverstiyami': on_bed(cap().rotate((0, 0, 0), (1, 0, 0), 180)),
        '4a_kronshtein_motor_N20': bm,
        '5a_shkiv_na_val_motora': on_bed(pulley_motor().rotate((0, 0, 0), (0, 1, 0), -90)),
        '4b_kronshtein_bez_motora': bracket()[0],
        '5b_shkiv_na_os_M3': on_bed(pulley().rotate((0, 0, 0), (0, 1, 0), -90)),
        '6_cabin_kabina': cabin(),
        '7_counterweight_protivoves': counterweight(),
    }
    for name, w in parts.items():
        cq.exporters.export(w, os.path.join(out, name + '.step'))
        s = w.val()
        bb = s.BoundingBox()
        print(f'{name:30s} {bb.xlen:6.1f} x {bb.ylen:6.1f} x {bb.zlen:6.1f} мм  объём {s.Volume()/1000:6.1f} см3  '
              f'тел={len(w.solids().vals())} валидно={s.isValid()}')

    # сборка N этажей
    D = P.DNO_T
    cap_z = D + N * P.H
    roof = cap_z + roof_top()
    k = 1 if N > 1 else 0
    asm = cq.Assembly(name=f'lift_{N}floors')
    col = lambda r, g, b: cq.Color(r, g, b, 1)
    asm.add(dno(), name='dno', color=col(.8, .82, .85))
    sec = floor_section()
    for i in range(N):
        asm.add(sec, name=f'etazh_{i + 1}', loc=cq.Location((0, 0, D + i * P.H)), color=col(.8, .82, .85))
    asm.add(cap(), name='potolok', loc=cq.Location((0, 0, cap_z)), color=col(.8, .82, .85))
    asm.add(bm, name='kronshtein', loc=cq.Location((0, 0, roof)), color=col(.55, .57, .6))
    asm.add(pulley_motor(), name='shkiv', loc=cq.Location((0, pulley_y(), roof + zm_ax)), color=col(.9, .65, .24))
    asm.add(cabin(), name='kabina', loc=cq.Location((0, 0, D + k * P.H + P.CAB_STOP)), color=col(.23, .51, .77))
    cw_z = cap_z + P.J + P.CAP_RING_H - P.CW_Z - 0.5 - k * P.H
    asm.add(counterweight(), name='protivoves', loc=cq.Location((0, 0, cw_z)), color=col(.75, .22, .17))
    path = os.path.join(out, f'assembly_{N}floors.step')
    asm.save(path)
    print('готово:', out)


if __name__ == '__main__':
    main()
