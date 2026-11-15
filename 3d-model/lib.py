"""Мини-обёртка над manifold3d: боксы по границам, цилиндры, экспорт STL."""
import numpy as np
import manifold3d as m
import trimesh


def box(x0, x1, y0, y1, z0, z1):
    """Параллелепипед по границам (мм)."""
    return m.Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cyl(r, z0, z1, x=0.0, y=0.0, seg=48):
    """Вертикальный цилиндр."""
    return m.Manifold.cylinder(z1 - z0, r, r, seg).translate((x, y, z0))


def cyl_x(r, x0, x1, y=0.0, z=0.0, seg=48):
    """Цилиндр вдоль оси X."""
    c = m.Manifold.cylinder(x1 - x0, r, r, seg).rotate((0, 90, 0))
    return c.translate((x0, y, z))


def cyl_y(r, y0, y1, x=0.0, z=0.0, seg=48):
    """Цилиндр вдоль оси Y (от y0 к y1)."""
    c = m.Manifold.cylinder(y1 - y0, r, r, seg).rotate((-90, 0, 0))
    return c.translate((x, y0, z))


def union(parts):
    return m.Manifold.batch_boolean(list(parts), m.OpType.Add)


def mirror_x(p):
    return p.mirror((1, 0, 0))


def to_trimesh(p):
    mesh = p.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3],
                           faces=np.asarray(mesh.tri_verts), process=True)


def save_stl(p, path):
    t = to_trimesh(p)
    t.export(path)
    return t


def overlap_volume(a, b):
    return (a ^ b).volume()


def prism_xz(points, y0, y1):
    """Призма: многоугольник в плоскости XZ [(x, z), ...], выдавлен по Y от y0 до y1."""
    cs = m.CrossSection([np.array(points, dtype=float)])
    p = m.Manifold.extrude(cs, y1 - y0)       # ось выдавливания Z, ось "y" полигона = мировой Z
    p = p.rotate((90, 0, 0))                  # (x,y,z)->(x,-z,y)
    return p.translate((0, y1, 0))


def keep_above(p, normal, offset):
    """Оставить часть, где dot(point, normal) >= offset (normal нормируется)."""
    n = np.array(normal, dtype=float)
    k = np.linalg.norm(n)
    return p.trim_by_plane(tuple(n / k), offset / k)


def prism_yz(points, x0, x1):
    """Призма: многоугольник в плоскости YZ [(y, z), ...], выдавлен по X от x0 до x1."""
    cs = m.CrossSection([np.array(points, dtype=float)])
    p = m.Manifold.extrude(cs, x1 - x0)       # (a, b, c) = (y, z, x)
    p = p.transform(((0, 0, 1, x0), (1, 0, 0, 0), (0, 1, 0, 0)))   # x=c, y=a, z=b
    return p


def prism_xy(points, z0, z1):
    """Призма: многоугольник в плоскости XY [(x, y), ...], выдавлен по Z от z0 до z1."""
    cs = m.CrossSection([np.array(points, dtype=float)])
    return m.Manifold.extrude(cs, z1 - z0).translate((0, 0, z0))
