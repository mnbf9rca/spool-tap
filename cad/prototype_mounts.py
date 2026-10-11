"""Rough CoreS3 SE wedge and removable NFC reader dock."""

from pathlib import Path

import cadquery as cq


OUT = Path(__file__).resolve().parent
# shortcut: pocket and screw clearances are provisional; tune if hardware testing shows interference.


def cores3_wedge():
    # The CORE One display bar offers only about 34 mm of flat steel.
    back = cq.Workplane("XY").box(72, 34, 7, centered=(True, True, False))
    for x in (-18, 18):
        # Epoxy the magnet into this outward-facing pocket with its face slightly proud.
        back = back.cut(cq.Workplane("XY").center(x, 0).circle(16.2).extrude(2.85))
        back = back.cut(cq.Workplane("XY").center(x, 0).circle(2).extrude(4.3).translate((0, 0, 2.8)))

    # The rear ring reaches both ends of the uprights and clears the screw recesses.
    ring = cq.Workplane("XY").box(58, 54, 3, centered=(True, True, False)).translate((0, 0, -6))
    case_opening = cq.Workplane("XY").box(50, 50, 3, centered=(True, True, False)).edges("|Z").fillet(3.6).translate((0, 0, -3))
    for x in (-22, 22):
        for y in (-22, 22):
            pad = cq.Workplane("XY").center(x, y).circle(4.5).extrude(3).translate((0, 0, -3))
            ring = ring.union(pad.intersect(case_opening))
    ring = ring.cut(cq.Workplane("XY").box(38, 38, 7, centered=(True, True, False)).translate((0, 0, -6.5)))
    assert len(ring.solids().vals()) == 1
    ring = ring.rotate((0, 0, 0), (1, 0, 0), -45).translate((0, 0, 27))

    side = cq.Workplane("YZ").polyline([(-16, 4), (17, 4), (18, 8), (-19, 46), (-24, 42)]).close().extrude(5)
    face_clearance = cq.Workplane("XY").box(120, 120, 60, centered=(True, True, False)).translate((0, 0, -3))
    face_clearance = face_clearance.rotate((0, 0, 0), (1, 0, 0), -45).translate((0, 0, 27))
    side = side.cut(face_clearance)
    # Keep the uprights flush with both ends of the ring.
    for y in (-102, 102):
        edge_clearance = cq.Workplane("XY").box(120, 150, 200).translate((0, y, 0))
        edge_clearance = edge_clearance.rotate((0, 0, 0), (1, 0, 0), -45).translate((0, 0, 27))
        side = side.cut(edge_clearance)
    mount = back.union(ring).union(side.translate((24, 0, 0))).union(side.translate((-29, 0, 0)))

    # Screw heads seat behind the ring; recesses in the ring would break its edge.
    # Glue in the magnets after assembly.
    for x in (-22, 22):
        for y in (-22, 22):
            access = cq.Workplane("XY").center(x, y).circle(3.6).extrude(70).translate((0, 0, -76))
            access = access.rotate((0, 0, 0), (1, 0, 0), -45).translate((0, 0, 27))
            shaft = cq.Workplane("XY").center(x, y).circle(1.7).extrude(78).translate((0, 0, -76))
            shaft = shaft.rotate((0, 0, 0), (1, 0, 0), -45).translate((0, 0, 27))
            mount = mount.cut(access).cut(shaft)
            assert mount.intersect(shaft).val().Volume() < 1e-6
    mount = mount.clean()
    assert mount.val().isValid() and len(mount.solids().vals()) == 1
    return mount


def nfc_dock():
    base = cq.Workplane("XY").box(30, 56, 4.5, centered=(True, True, False))
    for y in (-18, 18):
        base = base.cut(cq.Workplane("XY").center(0, y).rect(8.8, 8.8).extrude(1.85))
        base = base.cut(cq.Workplane("XY").center(0, y).circle(1.5).extrude(2.8).translate((0, 0, 1.8)))

    # Short rails leave the top of the reader free to grip and its connector open below.
    for x in (-13.4, 13.4):
        base = base.union(cq.Workplane("XY").box(2, 42, 10, centered=(True, True, False)).translate((x, -4, 4.5)))
    for x in (-11.2, 11.2):
        base = base.union(cq.Workplane("XY").box(3.8, 42, 1.5, centered=(True, True, False)).translate((x, -4, 13)))
    for x in (-11, 11):
        base = base.union(cq.Workplane("XY").box(5, 2.5, 7, centered=(True, True, False)).translate((x, -25.25, 4.5)))
    dock = base.clean()
    assert dock.val().isValid() and len(dock.solids().vals()) == 1
    return dock


if __name__ == "__main__":
    cq.exporters.export(cores3_wedge(), str(OUT / "cores3_wedge_45.stl"), tolerance=0.08, angularTolerance=0.1)
    cq.exporters.export(nfc_dock(), str(OUT / "nfc_dock.stl"), tolerance=0.08, angularTolerance=0.1)
