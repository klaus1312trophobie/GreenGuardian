#!/usr/bin/env python3
"""Green Guardian tent camera - printable case for ESP32-CAM on ESP32-CAM-MB,
lid with lens collet (camera module on a long flex cable), snap clip for a 19 mm tent pole.
All dimensions in mm. Output: print-oriented STL files (z up, resting on z = 0)."""
import os, sys, math
import numpy as np
from manifold3d import Manifold, CrossSection, JoinType, set_circular_segments

set_circular_segments(96)
OUT = sys.argv[1] if len(sys.argv) > 1 else "stl"
os.makedirs(OUT, exist_ok=True)

# ----------------------------------------------------------------------------- parameters
BOARD_X, BOARD_Y = 40.5, 27.0        # ESP32-CAM and ESP32-CAM-MB outline
CAV_X, CAV_Y = 42.0, 28.4            # cavity (0.75 / 0.7 clearance per side)
CAV_H = 40.0                         # ~19 board stack + room for camera module and folded flex
WALL, FLOOR = 2.0, 2.0
OUT_X, OUT_Y = CAV_X + 2 * WALL, CAV_Y + 2 * WALL
BODY_H = FLOOR + CAV_H
END_W, END_Z0, END_Z1 = 20.0, FLOOR, 25.0   # opening in both short walls: USB plug below, SD card slot above
BULGE, BULGE_Z = 3.0, 12.0           # lower part is 3 mm wider per long side up to z = 12, then tapers at 45 deg:
                                     # room for the two side buttons of the ESP32-CAM-MB (IO0 / RST)

LID_T = 2.4
SKIRT_CLEAR, SKIRT_T, SKIRT_H = 0.15, 1.6, 4.0
SNAP_Z = BODY_H - 2.6                # snap ridge / groove height
SNAP_LEN, GROOVE_D, BUMP_H = 12.0, 0.5, 0.5

BORE_R_OUT, BORE_R_IN = 8.5, 8.85    # tapered seat for the lens ring (dia 17.0 outside -> 17.7 inside)
RING_R0, RING_PLUG_H = 8.45, 3.4     # ring plug: same taper, 0.05 smaller
RING_TAPER = (BORE_R_IN - BORE_R_OUT) / LID_T
FINGER_L, FINGER_T = 8.0, 1.2
LENS_SIZES = [10, 11, 12, 13, 14]    # each ring grips lens barrels from N-0.2 to N+0.8 mm

TONGUE_T, TONGUE_H = 6.0, 10.0       # hinge tongue on the case
HINGE_TO_WALL, TONGUE_END = 12.5, 5.0
M3_HOLE_R = 1.7

POLE_D = 19.0
CLIP_ID, CLIP_WALL, CLIP_H = 18.5, 2.8, 20.0   # 0.5 undersize -> clamps on the pole
CLIP_GAP = 15.0
EAR_T, EAR_GAP = 4.0, 6.4
HINGE_DIST, EAR_END = 9.0, 4.5       # hinge axis from clip surface, ear length beyond the axis
NUT_AF, NUT_D = 5.7, 2.4             # M3 nut pocket


# ----------------------------------------------------------------------------- helpers
def rrect(w, h, r):
    return CrossSection.square([w - 2 * r, h - 2 * r], center=True).offset(r, JoinType.Round)

def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])

def prism_x(poly_yz, x0, x1):
    """polygon given as (y, z) points, extruded along x"""
    m = CrossSection([np.array(ccw(list(poly_yz)), dtype=np.float64)]).extrude(x1 - x0)
    return m.transform(np.array([[0, 0, 1, x0], [1, 0, 0, 0], [0, 1, 0, 0]], dtype=np.float64))

def prism_y(poly_xz, y0, y1):
    """polygon given as (x, z) points, extruded along y"""
    pts = np.array(ccw([(z, x) for x, z in poly_xz]), dtype=np.float64)
    m = CrossSection([pts]).extrude(y1 - y0)
    return m.transform(np.array([[0, 1, 0, 0], [0, 0, 1, y0], [1, 0, 0, 0]], dtype=np.float64))

def cyl_x(r, x0, x1, y, z):
    return prism_x([(y + r * math.cos(a), z + r * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 48, endpoint=False)], x0, x1)

def cyl_y(r, y0, y1, x, z, n=48, phase=0.0):
    return prism_y([(x + r * math.cos(a + phase), z + r * math.sin(a + phase)) for a in np.linspace(0, 2 * math.pi, n, endpoint=False)], y0, y1)


# ----------------------------------------------------------------------------- case body
def make_body():
    def flared(w, h, r, z0):
        """w x h footprint from z0 up; below BULGE_Z it is 2*BULGE wider in y, then a 45 deg taper back to w x h"""
        wide = rrect(w, h + 2 * BULGE, r)
        m = wide.extrude(BULGE_Z - z0).translate([0, 0, z0])
        m += wide.extrude(BULGE, scale_top=(1.0, h / (h + 2 * BULGE))).translate([0, 0, BULGE_Z])
        return m + rrect(w, h, r).extrude(BODY_H + 1 - BULGE_Z - BULGE).translate([0, 0, BULGE_Z + BULGE])
    b = flared(OUT_X, OUT_Y, 2.5, 0) ^ box(-100, 100, -100, 100, 0, BODY_H)
    b -= flared(CAV_X, CAV_Y, 0.8, FLOOR)
    # opening in both short walls for USB plug and SD card (bridge with 45 deg corners)
    w, c = END_W / 2, 4.0
    b -= prism_x([(-w, END_Z0), (w, END_Z0), (w, END_Z1 - c), (w - c, END_Z1), (-w + c, END_Z1), (-w, END_Z1 - c)],
                 -OUT_X / 2 - 1, OUT_X / 2 + 1)
    # ventilation slots in both long walls
    for x in (-15, -9, -3, 3, 9, 15):
        b -= box(x - 1, x + 1, -OUT_Y / 2 - 1, OUT_Y / 2 + 1, 22, 34)
    # snap grooves for the lid (V profile) in both long walls
    for s in (1, -1):
        y = s * CAV_Y / 2
        b -= prism_x(sorted_ccw([(y - s * 0.02, SNAP_Z - GROOVE_D - 0.02), (y + s * GROOVE_D, SNAP_Z), (y - s * 0.02, SNAP_Z + GROOVE_D + 0.02)]),
                     -SNAP_LEN / 2 - 1, SNAP_LEN / 2 + 1)
    # hinge tongue on the -Y long wall, flush with the bottom (prints flat on the bed)
    y_wall = -OUT_Y / 2 - BULGE
    y_h = y_wall - HINGE_TO_WALL
    t = box(-TONGUE_T / 2, TONGUE_T / 2, y_h - TONGUE_END, y_wall + 0.5, 0, TONGUE_H)
    # small fillets left and right of the tongue root (kept short so they clear the clip ears)
    for s in (1, -1):
        g = CrossSection([np.array(ccw([(s * TONGUE_T / 2, y_wall + 0.5), (s * (TONGUE_T / 2 + 2), y_wall + 0.5), (s * TONGUE_T / 2, y_wall - 2)]))]).extrude(TONGUE_H)
        t += g
    b += t
    b -= cyl_x(M3_HOLE_R, -TONGUE_T / 2 - 6, TONGUE_T / 2 + 6, y_h, TONGUE_H / 2)
    return b

def ccw(pts):
    a = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
    return pts if a > 0 else pts[::-1]

sorted_ccw = ccw


# ----------------------------------------------------------------------------- lid (modelled in place on the body)
def make_lid():
    lid = rrect(OUT_X, OUT_Y, 2.5).extrude(LID_T).translate([0, 0, BODY_H])
    sx, sy = CAV_X - 2 * SKIRT_CLEAR, CAV_Y - 2 * SKIRT_CLEAR
    skirt = (rrect(sx, sy, 0.7) - rrect(sx - 2 * SKIRT_T, sy - 2 * SKIRT_T, 0.3)).extrude(SKIRT_H + 0.5)
    lid += skirt.translate([0, 0, BODY_H - SKIRT_H])
    for s in (1, -1):                       # snap ridges
        y = s * sy / 2
        lid += prism_x(ccw([(y - s * 0.3, SNAP_Z - BUMP_H - 0.3), (y + s * BUMP_H, SNAP_Z), (y - s * 0.3, SNAP_Z + BUMP_H + 0.3)]),
                       -SNAP_LEN / 2, SNAP_LEN / 2)
    # tapered seat for the lens ring, narrow end outside
    lid -= Manifold.cylinder(LID_T + 0.02, BORE_R_IN + 0.01 * RING_TAPER, BORE_R_OUT - 0.01 * RING_TAPER).translate([0, 0, BODY_H - 0.01])
    # pry notches at both short ends
    for s in (1, -1):
        lid -= box(s * OUT_X / 2 - 1.5, s * OUT_X / 2 + 1.5, -5, 5, BODY_H - 0.01, BODY_H + 1.0)
    return lid


# ----------------------------------------------------------------------------- lens ring (print orientation, front face on the bed)
def make_ring(n):
    r_root, r_tip = (n + 0.9) / 2, (n - 0.3) / 2
    z1, z2 = RING_PLUG_H, RING_PLUG_H + FINGER_L
    prof = [(r_root, 0), (RING_R0, 0), (RING_R0 + RING_TAPER * z1, z1), (r_root + FINGER_T, z1),
            (r_tip + FINGER_T, z2), (r_tip, z2), (r_root, z1)]
    ring = CrossSection([np.array(ccw(prof))]).revolve()
    for k in range(6):                      # slots -> six spring fingers
        ring -= box(0, 12, -0.5, 0.5, z1 + 0.6, z2 + 1).rotate([0, 0, 60 * k + 30])
    return ring


# ----------------------------------------------------------------------------- pole clip (pole axis = z)
R_IN = CLIP_ID / 2
R_OUT = R_IN + CLIP_WALL
X_H = -(R_OUT + HINGE_DIST)                 # hinge axis (parallel to y) at z = CLIP_H / 2

def make_clip():
    c = (CrossSection.circle(R_OUT) - CrossSection.circle(R_IN)).extrude(CLIP_H)
    g = CLIP_GAP / 2
    x_in = math.sqrt(R_IN ** 2 - g ** 2)
    c -= CrossSection([np.array(ccw([(0, -g), (x_in, -g), (R_OUT + 1, -g - 4), (R_OUT + 1, g + 4), (x_in, g), (0, g)]))]).extrude(CLIP_H + 2).translate([0, 0, -1])
    x_end = X_H - EAR_END
    for s in (1, -1):
        y0, y1 = sorted((s * EAR_GAP / 2, s * (EAR_GAP / 2 + EAR_T)))
        c += box(x_end, -8.0, y0, y1, 0, CLIP_H)
    # chamfer the free ear corners (top and bottom)
    ch, y = 3.0, EAR_GAP / 2 + EAR_T + 1
    c -= prism_y([(x_end - 0.1, CLIP_H - ch), (x_end - 0.1, CLIP_H + 0.1), (x_end + ch, CLIP_H + 0.1)], -y, y)
    c -= prism_y([(x_end - 0.1, ch), (x_end - 0.1, -0.1), (x_end + ch, -0.1)], -y, y)
    c -= cyl_y(M3_HOLE_R, -y, y, X_H, CLIP_H / 2)
    # M3 nut pocket in the +y ear (hexagon, corner pointing up -> prints without support)
    y_out = EAR_GAP / 2 + EAR_T
    c -= cyl_y(NUT_AF / math.sqrt(3), y_out - NUT_D, y_out + 1, X_H, CLIP_H / 2, n=6, phase=math.pi / 2)
    return c


# ----------------------------------------------------------------------------- export
def write_stl(m, name):
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties, dtype=np.float32)[:, :3]
    t = np.asarray(mesh.tri_verts, dtype=np.uint32)
    tri = v[t]
    nrm = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    rec = np.zeros(len(t), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec["n"], rec["v"] = nrm, tri
    path = os.path.join(OUT, name)
    with open(path, "wb") as f:
        f.write(name.encode().ljust(80, b" "))
        f.write(np.uint32(len(t)).tobytes())
        f.write(rec.tobytes())
    return path

def on_bed(m):
    lo = m.bounding_box()[:3]
    return m.translate([0, 0, -lo[2]])

def build():
    body, lid, clip = make_body(), make_lid(), make_clip()
    rings = {n: make_ring(n) for n in LENS_SIZES}
    return body, lid, clip, rings

if __name__ == "__main__":
    body, lid, clip, rings = build()
    parts = {"case_body.stl": body,
             "case_lid.stl": on_bed(lid.rotate([180, 0, 0])),
             "pole_clip_19mm.stl": clip}
    for n, r in rings.items():
        parts[f"lens_ring_{n}mm.stl"] = r
    plate = Manifold()
    for i, n in enumerate(LENS_SIZES):
        plate += rings[n].translate([i * 22.0, 0, 0])
    parts["lens_rings_10-14mm_plate.stl"] = plate
    for name, m in parts.items():
        bb = m.bounding_box()
        print(f"{name:32s} status={m.status()} tris={m.num_tri():6d} vol={m.volume() / 1000:6.2f} cm3 "
              f"size={bb[3] - bb[0]:.1f} x {bb[4] - bb[1]:.1f} x {bb[5] - bb[2]:.1f}  zmin={bb[2]:.3f}")
        write_stl(m, name)
