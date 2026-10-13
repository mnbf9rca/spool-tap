"""Rough CoreS3 SE mounts and removable NFC reader docks."""

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


def shelf_mount():
    """One-piece hook for a 50 mm shelf, CoreS3 SE, and Grove NFC reader."""
    # The 25 mm top hook carries the weight; the 50 mm plate bears against the shelf front.
    mount = cq.Workplane("XY").box(120, 50, 4.5, centered=(True, False, False))
    mount = mount.union(cq.Workplane("XY").box(120, 4, 29, centered=(True, False, False)).translate((0, 50, -24)))

    # The open ring spaces the CoreS3 circuit board away from the shelf face.
    core_x, core_y = 28, 25
    ring = cq.Workplane("XY").center(core_x, core_y).box(58, 54, 3, centered=(True, True, False)).translate((0, 0, 4.5))
    case_opening = cq.Workplane("XY").center(core_x, core_y).box(50, 50, 3, centered=(True, True, False)).edges("|Z").fillet(3.6).translate((0, 0, 7.5))
    for x in (core_x - 22, core_x + 22):
        for y in (core_y - 22, core_y + 22):
            pad = cq.Workplane("XY").center(x, y).circle(4.5).extrude(3).translate((0, 0, 7.5))
            ring = ring.union(pad.intersect(case_opening))
    mount = mount.union(ring)
    mount = mount.cut(cq.Workplane("XY").center(core_x, core_y).box(38, 38, 9, centered=(True, True, False)).translate((0, 0, -0.5)))
    # Rear grid turns the 38 mm unsupported top edge into short bridges without filling the board cavity.
    for x in (core_x - 9.5, core_x, core_x + 9.5):
        mount = mount.union(cq.Workplane("XY").center(x, core_y).box(1.8, 39, 4.5, centered=(True, True, False)))
        # Taper each rib toward the ring so its front edge is supported at the bridge layer.
        mount = mount.union(cq.Workplane("YZ").polyline([(5.5, 4.5), (5.5, 7.5), (9.5, 4.5)]).close().extrude(1.8).translate((x - 0.9, 0, 0)))
    mount = mount.union(cq.Workplane("XY").center(core_x, core_y).box(39, 1.8, 4.5, centered=(True, True, False)))
    for x in (core_x - 22, core_x + 22):
        for y in (core_y - 22, core_y + 22):
            mount = mount.cut(cq.Workplane("XY").center(x, y).circle(3.6).extrude(5).translate((0, 0, -0.1)))
            shaft = cq.Workplane("XY").center(x, y).circle(1.7).extrude(7).translate((0, 0, 4.4))
            mount = mount.cut(shaft)
            assert mount.intersect(shaft).val().Volume() < 1e-6

    # Capture only the bottom 20 mm, leaving the reader top and center Grove exit free.
    reader_x = -42
    mount = mount.union(cq.Workplane("XY").center(reader_x, 25).box(30, 50, 3, centered=(True, True, False)).translate((0, 0, 4.5)))
    for x in (reader_x - 13.4, reader_x + 13.4):
        mount = mount.union(cq.Workplane("XY").center(x, 11).box(2, 20, 10.1, centered=(True, True, False)).translate((0, 0, 7.5)))
    for x in (reader_x - 11.2, reader_x + 11.2):
        mount = mount.union(cq.Workplane("XY").center(x, 11).box(3.8, 20, 1.5, centered=(True, True, False)).translate((0, 0, 16.1)))
    for x in (reader_x - 11, reader_x + 11):
        mount = mount.union(cq.Workplane("XY").center(x, 1.25).box(5, 2.5, 8.6, centered=(True, True, False)).translate((0, 0, 7.5)))

    mount = mount.clean()
    reader_clearance = cq.Workplane("XY").center(reader_x, 26.5).box(24.4, 48, 8.3, centered=(True, True, False)).translate((0, 0, 7.5))
    assert mount.intersect(reader_clearance).val().Volume() < 1e-6
    assert mount.val().isValid() and len(mount.solids().vals()) == 1
    # Put the broad outside face of the top hook on the print bed.
    return mount.rotate((0, 0, 0), (1, 0, 0), -90).translate((0, 24, 54))


if __name__ == "__main__":
    cq.exporters.export(cores3_wedge(), str(OUT / "cores3_wedge_45.stl"), tolerance=0.08, angularTolerance=0.1)
    cq.exporters.export(nfc_dock(), str(OUT / "nfc_dock.stl"), tolerance=0.08, angularTolerance=0.1)
    cq.exporters.export(shelf_mount(), str(OUT / "cores3_shelf_mount.stl"), tolerance=0.08, angularTolerance=0.1)
