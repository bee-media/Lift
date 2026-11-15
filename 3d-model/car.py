"""Кабина и противовес (низ детали на z=0, центр по X=0; по Y — рабочее положение в шахте)."""
import params as P
from lib import box, cyl, union

B = P.IN_H


def cabin():
    """Кабина без дверей (передняя сторона открыта). Башмаки с пазами охватывают рёбра шахты."""
    w2, d2 = P.CAB_W / 2, P.CAB_D / 2
    y0, y1 = P.CAB_Y - d2, P.CAB_Y + d2
    body = box(-w2, w2, y0, y1, 0, P.CAB_Z)
    cavity = box(-w2 + P.CAB_WALL, w2 - P.CAB_WALL, y0 - 1, y1 - P.CAB_WALL,
                 P.CAB_FLOOR, P.CAB_Z - P.CAB_WALL)
    body = body - cavity
    body = body - cyl(P.CAB_STRING_HOLE / 2, P.CAB_Z - P.CAB_WALL - 1, P.CAB_Z + 1, 0, P.CAB_Y, 32)

    rib_tip = B - P.CR_H                       # 33.6 — торец ребра
    slot_bottom = rib_tip - 0.6                # дно паза в башмаке
    narrow = P.CR_T + 2 * P.RAIL_CL            # 4.0 — рабочая часть паза
    relief = narrow + 2.4                      # 6.4 — разгрузочная средняя часть
    shoes = []
    for s in (1, -1):
        xa, xb = sorted((s * (w2 - 0.5), s * P.SHOE_L))
        shoe = box(xa, xb, P.CAB_Y - P.SHOE_W / 2, P.CAB_Y + P.SHOE_W / 2, 0, P.CAB_Z)
        sa, sb = sorted((s * slot_bottom, s * (P.SHOE_L + 1)))
        z_lo, z_hi = P.SHOE_END, P.CAB_Z - P.SHOE_END
        cuts = [box(sa, sb, P.CAB_Y - narrow / 2, P.CAB_Y + narrow / 2, -1, P.CAB_Z + 1),
                box(sa, sb, P.CAB_Y - relief / 2, P.CAB_Y + relief / 2, z_lo, z_hi)]
        shoes.append(shoe - union(cuts))
    return body + union(shoes)


def counterweight():
    """Противовес: плита с двумя Т-пазами на задней грани (захватывает Т-рельсы шахты)."""
    w2 = P.CW_W / 2
    y_back, y_front = P.CW_Y_BACK, P.CW_Y_BACK - P.CW_D
    body = box(-w2, w2, y_front, y_back, 0, P.CW_Z)
    c = P.RAIL_CL
    head1 = B - P.WR_STEM_H                    # 34.6
    head0 = head1 - P.WR_HEAD_H                # 32.6
    for s in (1, -1):
        xc = s * P.WR_X
        stem = box(xc - P.WR_STEM_W / 2 - c, xc + P.WR_STEM_W / 2 + c, head1 + c - 0.1, y_back + 1, -1, P.CW_Z + 1)
        head = box(xc - P.WR_HEAD_W / 2 - c, xc + P.WR_HEAD_W / 2 + c, head0 - c, head1 + c, -1, P.CW_Z + 1)
        body = body - stem - head
    # паз под груз: открыт спереди, между Т-пазами; груз (гайки, пластина, монеты) приклеивается внутрь
    pw = P.CW_POCKET_W / 2
    top = P.CW_Z - P.CW_POCKET_TOP
    body = body - box(-pw, pw, y_front - 1, y_back - P.CW_POCKET_BACK, P.CW_POCKET_BOT, top)
    # отверстие под нить только в верхней перемычке; узел лежит в пазу над грузом
    body = body - cyl(P.CAB_STRING_HOLE / 2, top - 1, P.CW_Z + 1, 0, P.CW_Y, 32)
    return body
