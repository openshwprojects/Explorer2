#!/usr/bin/env python3
"""
generate_samples_3d_cad.py — Deterministic 3D & CAD sample generator for Explorer2.

Generates genuine, specification-compliant sample files into `testdata/3d/` and `testdata/cad/`
covering all 12 engineering/CFD/FEM/voxel/game 3D & CAD formats plus real-spec binary containers
for Cinema 4D HyperFile (.c4d), RAD Granny2 (.gr2), SolidWorks OLE2 (.sldprt), Autodesk Inventor
OLE2 RSeDb (.ipt), Siemens Solid Edge OLE2 Parasolid (.par), SpaceClaim OPC (.scdoc), and
Pixologic ZBrush RLE (.ztl, .zpr).
"""

from __future__ import annotations

import gzip
import io
import math
import os
import struct
import zipfile
import zlib
from pathlib import Path


ZIP_FIXED_DT = (2025, 1, 1, 0, 0, 0)


def write_zip_deterministic(entries: list[tuple[str, bytes]], compress: bool = True) -> bytes:
    buf = io.BytesIO()
    mode = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
    with zipfile.ZipFile(buf, "w", compression=mode) as zf:
        for arcname, payload in entries:
            zi = zipfile.ZipInfo(filename=arcname, date_time=ZIP_FIXED_DT)
            zi.compress_type = mode
            zi.external_attr = 0o644 << 16
            zf.writestr(zi, payload)
    return buf.getvalue()


# ==============================================================================
# MS-CFB v3 (OLE2 Compound Document) Builder — 512B sectors + MiniStream/FAT
# ==============================================================================
def build_ole2_compound(streams: list[tuple[str, bytes]]) -> bytes:
    """Builds a standard MS-CFB v3 compound file with valid Header, FAT, Directory, and MiniFAT/MiniStream."""
    sector_size = 512
    mini_sector_size = 64
    mini_cutoff = 4096

    ENDOFCHAIN = 0xFFFFFFFE
    FREESECT = 0xFFFFFFFF
    FATSECT = 0xFFFFFFFD

    # Separate mini-streams (< 4096 B) and regular streams (>= 4096 B)
    mini_stream_blob = bytearray()
    mini_fat: list[int] = []
    stream_StartSec: list[int] = []

    regular_blobs: list[bytes] = []
    regular_indices: list[int] = []

    for idx, (_name, data) in enumerate(streams):
        if len(data) == 0:
            stream_StartSec.append(ENDOFCHAIN)
        elif len(data) < mini_cutoff:
            start_ms = len(mini_stream_blob) // mini_sector_size
            n_ms = (len(data) + mini_sector_size - 1) // mini_sector_size
            padded = data + b"\x00" * (n_ms * mini_sector_size - len(data))
            mini_stream_blob.extend(padded)
            for k in range(n_ms):
                mini_fat.append((start_ms + k + 1) if (k + 1 < n_ms) else ENDOFCHAIN)
            stream_StartSec.append(start_ms)
        else:
            stream_StartSec.append(-1)
            regular_blobs.append(data)
            regular_indices.append(idx)

    # Sector layout:
    #   Sec 0: FAT sector 0
    #   Sec 1..D: Directory sectors
    #   Next: MiniFAT sector(s) (if mini_fat non-empty)
    #   Next: Root Entry MiniStream sector(s) (if mini_stream_blob non-empty)
    #   Next: Regular stream sector chains
    n_dir_entries = 1 + len(streams)  # Root Entry + streams
    n_dir_sectors = (n_dir_entries * 128 + sector_size - 1) // sector_size

    sec_cursor = 1
    first_dir_sec = sec_cursor
    sec_cursor += n_dir_sectors

    if mini_fat:
        n_minifat_secs = (len(mini_fat) * 4 + sector_size - 1) // sector_size
        first_minifat_sec = sec_cursor
        sec_cursor += n_minifat_secs
    else:
        n_minifat_secs = 0
        first_minifat_sec = ENDOFCHAIN

    if mini_stream_blob:
        n_ministream_secs = (len(mini_stream_blob) + sector_size - 1) // sector_size
        root_start_sec = sec_cursor
        sec_cursor += n_ministream_secs
    else:
        n_ministream_secs = 0
        root_start_sec = ENDOFCHAIN

    reg_sec_ranges: list[tuple[int, int]] = []
    for r_idx, blob in zip(regular_indices, regular_blobs):
        n_secs = (len(blob) + sector_size - 1) // sector_size
        stream_StartSec[r_idx] = sec_cursor
        reg_sec_ranges.append((sec_cursor, n_secs))
        sec_cursor += n_secs

    total_sectors = sec_cursor
    assert total_sectors <= 128, "Single FAT sector handles up to 128 sectors"

    # Build FAT sector 0
    fat_entries = [FREESECT] * 128
    fat_entries[0] = FATSECT

    def chain_range(start: int, count: int) -> None:
        for k in range(count):
            fat_entries[start + k] = (start + k + 1) if (k + 1 < count) else ENDOFCHAIN

    chain_range(first_dir_sec, n_dir_sectors)
    if n_minifat_secs > 0:
        chain_range(first_minifat_sec, n_minifat_secs)
    if n_ministream_secs > 0:
        chain_range(root_start_sec, n_ministream_secs)
    for st, cnt in reg_sec_ranges:
        chain_range(st, cnt)

    fat_bytes = struct.pack("<128I", *fat_entries)

    # Build balanced BST for Directory sibling links (1..len(streams))
    def build_bst(lo: int, hi: int) -> tuple[int, dict[int, tuple[int, int]]]:
        if lo > hi:
            return (0xFFFFFFFF, {})
        mid = (lo + hi) // 2
        left_id, left_map = build_bst(lo, mid - 1)
        right_id, right_map = build_bst(mid + 1, hi)
        merged = {mid: (left_id, right_id)}
        merged.update(left_map)
        merged.update(right_map)
        return (mid, merged)

    root_child, bst_links = build_bst(1, len(streams)) if streams else (0xFFFFFFFF, {})

    def pack_dir_entry(
        name: str,
        obj_type: int,
        color: int,
        left_sib: int,
        right_sib: int,
        child_id: int,
        start_sec: int,
        size_bytes: int,
    ) -> bytes:
        ent = bytearray(128)
        u16 = (name + "\x00").encode("utf-16le")
        assert len(u16) <= 64
        ent[0 : len(u16)] = u16
        struct.pack_into("<H", ent, 64, len(u16))
        ent[66] = obj_type
        ent[67] = color
        struct.pack_into("<III", ent, 68, left_sib, right_sib, child_id)
        struct.pack_into("<II", ent, 116, start_sec, size_bytes)
        return bytes(ent)

    dir_blob = bytearray()
    dir_blob.extend(
        pack_dir_entry(
            "Root Entry",
            5,
            0,
            0xFFFFFFFF,
            0xFFFFFFFF,
            root_child,
            root_start_sec,
            len(mini_stream_blob),
        )
    )
    for i, (sname, sdata) in enumerate(streams, start=1):
        lsib, rsib = bst_links[i]
        dir_blob.extend(
            pack_dir_entry(
                sname,
                2,
                1,
                lsib,
                rsib,
                0xFFFFFFFF,
                stream_StartSec[i - 1],
                len(sdata),
            )
        )
    while len(dir_blob) < n_dir_sectors * sector_size:
        dir_blob.extend(b"\x00" * 68 + struct.pack("<III", 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF) + b"\x00" * 48)

    # Build 512-byte MS-CFB Header
    hdr = bytearray(512)
    hdr[0:8] = b"\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1"
    struct.pack_into("<HH", hdr, 24, 0x003E, 0x0003)  # minorVer=0x3E, majorVer=3
    struct.pack_into("<H", hdr, 28, 0xFFFE)           # little-endian byte order
    struct.pack_into("<HH", hdr, 30, 9, 6)            # sectorShift=9 (512), miniShift=6 (64)
    struct.pack_into("<I", hdr, 44, 1)                # numFatSectors=1
    struct.pack_into("<I", hdr, 48, first_dir_sec)
    struct.pack_into("<I", hdr, 56, mini_cutoff)
    struct.pack_into("<II", hdr, 60, first_minifat_sec, n_minifat_secs)
    struct.pack_into("<II", hdr, 68, ENDOFCHAIN, 0)   # DIFAT
    struct.pack_into("<I", hdr, 76, 0)                # DIFAT[0] = sector 0
    for k in range(1, 109):
        struct.pack_into("<I", hdr, 76 + k * 4, FREESECT)

    out = bytearray()
    out.extend(hdr)
    out.extend(fat_bytes)
    out.extend(dir_blob)

    if n_minifat_secs > 0:
        mf_pad = mini_fat + [FREESECT] * (n_minifat_secs * 128 - len(mini_fat))
        out.extend(struct.pack(f"<{len(mf_pad)}I", *mf_pad))
    if n_ministream_secs > 0:
        out.extend(mini_stream_blob)
        rem = n_ministream_secs * sector_size - len(mini_stream_blob)
        if rem > 0:
            out.extend(b"\x00" * rem)
    for blob in regular_blobs:
        out.extend(blob)
        rem = ((len(blob) + sector_size - 1) // sector_size) * sector_size - len(blob)
        if rem > 0:
            out.extend(b"\x00" * rem)

    return bytes(out)


# ==============================================================================
# Spatial ACIS / Autodesk ShapeManager Binary (.sab) B-Rep Box Builder
# ==============================================================================
def build_acis_sab_box(hx: float, hy: float, hz: float, magic: bytes = b"ASM BinaryFile ") -> bytes:
    """Builds a genuine binary .sab B-Rep solid box (body -> lump -> shell -> 6 planar faces -> loops -> coedges -> edges -> vertices -> points)."""
    def t_ident(s: str) -> bytes:
        b = s.encode("ascii")
        return bytes([0x0D, len(b)]) + b

    def t_subident(s: str) -> bytes:
        b = s.encode("ascii")
        return bytes([0x0E, len(b)]) + b

    def t_str(s: str) -> bytes:
        b = s.encode("ascii")
        return bytes([0x07, len(b)]) + b

    def t_ptr(idx: int) -> bytes:
        return b"\x0C" + struct.pack("<i", idx)

    def t_pos(x: float, y: float, z: float) -> bytes:
        return b"\x13" + struct.pack("<3d", x, y, z)

    def t_vec(x: float, y: float, z: float) -> bytes:
        return b"\x14" + struct.pack("<3d", x, y, z)

    def t_sense(rev: bool) -> bytes:
        return t_ident("reversed" if rev else "forward")

    TERM = b"\x11"

    # 8 box corner coordinates
    corners = [
        (-hx, -hy, -hz),
        ( hx, -hy, -hz),
        ( hx,  hy, -hz),
        (-hx,  hy, -hz),
        (-hx, -hy,  hz),
        ( hx, -hy,  hz),
        ( hx,  hy,  hz),
        (-hx,  hy,  hz),
    ]

    # 12 undirected edges (start_corner, end_corner)
    edge_defs = [
        (0, 1), (1, 2), (2, 3), (3, 0),  # bottom ring (0..3)
        (4, 5), (5, 6), (6, 7), (7, 4),  # top ring    (4..7)
        (0, 4), (1, 5), (2, 6), (3, 7),  # pillars     (8..11)
    ]

    # 6 outward-oriented quad faces: (origin, normal, uAxis, [(edgeIdx, reversed), ...])
    face_defs = [
        ((0.0, 0.0, -hz), ( 0.0,  0.0, -1.0), (1.0, 0.0, 0.0), [(0, True),  (3, True),  (2, True),  (1, True)]),
        ((0.0, 0.0,  hz), ( 0.0,  0.0,  1.0), (1.0, 0.0, 0.0), [(4, False), (5, False), (6, False), (7, False)]),
        ((0.0, -hy, 0.0), ( 0.0, -1.0,  0.0), (1.0, 0.0, 0.0), [(0, False), (9, False), (4, True),  (8, True)]),
        (( hx, 0.0, 0.0), ( 1.0,  0.0,  0.0), (0.0, 1.0, 0.0), [(1, False), (10, False),(5, True),  (9, True)]),
        ((0.0,  hy, 0.0), ( 0.0,  1.0,  0.0), (-1.0,0.0, 0.0), [(2, False), (11, False),(6, True),  (10, True)]),
        ((-hx, 0.0, 0.0), (-1.0,  0.0,  0.0), (0.0,-1.0, 0.0), [(3, False), (8, False), (7, True),  (11, True)]),
    ]

    # Pre-assign sequential entity indices:
    # 0: body, 1: lump, 2: shell
    # 3..8: 6 faces
    # 9..14: 6 plane-surfaces
    # 15..20: 6 loops
    # 21..44: 24 coedges (4 per face)
    # 45..56: 12 edges
    # 57..68: 12 straight-curves
    # 69..76: 8 vertices
    # 77..84: 8 points
    ID_BODY = 0
    ID_LUMP = 1
    ID_SHELL = 2
    ID_FACE0 = 3
    ID_SURF0 = 9
    ID_LOOP0 = 15
    ID_COEDGE0 = 21
    ID_EDGE0 = 45
    ID_CURVE0 = 57
    ID_VERT0 = 69
    ID_POINT0 = 77
    TOTAL_RECS = 85

    recs: list[bytes] = []
    # 0: body -> lump(1)
    recs.append(t_ident("body") + t_ptr(-1) + t_ptr(ID_LUMP) + t_ptr(-1) + t_ptr(-1) + TERM)
    # 1: lump -> next(-1), shell(2), body(0)
    recs.append(t_ident("lump") + t_ptr(-1) + t_ptr(-1) + t_ptr(ID_SHELL) + t_ptr(ID_BODY) + TERM)
    # 2: shell -> next(-1), subshell(-1), face(3), lump(1)
    recs.append(t_ident("shell") + t_ptr(-1) + t_ptr(-1) + t_ptr(-1) + t_ptr(ID_FACE0) + t_ptr(-1) + t_ptr(ID_LUMP) + TERM)

    # 3..8: faces
    for fi in range(6):
        next_f = (ID_FACE0 + fi + 1) if fi < 5 else -1
        recs.append(
            t_ident("face")
            + t_ptr(-1)
            + t_ptr(next_f)
            + t_ptr(ID_LOOP0 + fi)
            + t_ptr(ID_SHELL)
            + t_ptr(-1)
            + t_ptr(ID_SURF0 + fi)
            + t_sense(False)
            + t_ident("single")
            + TERM
        )

    # 9..14: plane-surfaces
    for fi in range(6):
        orig, nrm, uax, _ = face_defs[fi]
        recs.append(
            t_ident("plane") + t_subident("surface") + t_ptr(-1) + t_pos(*orig) + t_vec(*nrm) + t_vec(*uax) + TERM
        )

    # 15..20: loops
    for fi in range(6):
        recs.append(
            t_ident("loop") + t_ptr(-1) + t_ptr(-1) + t_ptr(ID_COEDGE0 + fi * 4) + t_ptr(ID_FACE0 + fi) + TERM
        )

    # 21..44: 24 coedges
    for fi in range(6):
        _, _, _, c_list = face_defs[fi]
        base_ce = ID_COEDGE0 + fi * 4
        for k in range(4):
            e_idx, is_rev = c_list[k]
            nxt_ce = base_ce + ((k + 1) % 4)
            prv_ce = base_ce + ((k + 3) % 4)
            recs.append(
                t_ident("coedge")
                + t_ptr(-1)
                + t_ptr(nxt_ce)
                + t_ptr(prv_ce)
                + t_ptr(-1)
                + t_ptr(ID_EDGE0 + e_idx)
                + t_sense(is_rev)
                + t_ptr(ID_LOOP0 + fi)
                + TERM
            )

    # 45..56: 12 edges
    for ei, (c0, c1) in enumerate(edge_defs):
        recs.append(
            t_ident("edge")
            + t_ptr(-1)
            + t_ptr(ID_VERT0 + c0)
            + t_ptr(ID_VERT0 + c1)
            + t_ptr(-1)
            + t_ptr(ID_CURVE0 + ei)
            + t_sense(False)
            + TERM
        )

    # 57..68: 12 straight-curves
    for c0, c1 in edge_defs:
        p0, p1 = corners[c0], corners[c1]
        dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
        ln = math.sqrt(dx * dx + dy * dy + dz * dz)
        recs.append(
            t_ident("straight") + t_subident("curve") + t_ptr(-1) + t_pos(*p0) + t_vec(dx / ln, dy / ln, dz / ln) + TERM
        )

    # 69..76: 8 vertices
    for vi in range(8):
        recs.append(t_ident("vertex") + t_ptr(-1) + t_ptr(-1) + t_ptr(ID_POINT0 + vi) + TERM)

    # 77..84: 8 points
    for px, py, pz in corners:
        recs.append(t_ident("point") + t_ptr(-1) + t_pos(px, py, pz) + TERM)

    assert len(recs) == TOTAL_RECS

    out = bytearray()
    assert len(magic) in (14, 15)
    out.extend(magic)
    # Header: acisVer=20800, numRecords=85, numEntities=1, flags=0 (16 bytes)
    out.extend(struct.pack("<4i", 20800, TOTAL_RECS, 1, 0))
    # 3 header strings + 3 header doubles (units_mm, resabs, resnor)
    out.extend(t_str("Autodesk ShapeManager") + t_str("ASM 223.0") + t_str("2025"))
    out.extend(b"\x06" + struct.pack("<d", 1.0))
    out.extend(b"\x06" + struct.pack("<d", 1e-6))
    out.extend(b"\x06" + struct.pack("<d", 1e-10))
    for r in recs:
        out.extend(r)
    out.extend(t_ident("End-of-ASM-data" if magic.startswith(b"ASM") else "End-of-ACIS-data"))
    return bytes(out)


# ==============================================================================
# Siemens Parasolid Neutral Binary (.x_b) Solid Box Builder
# ==============================================================================
def build_parasolid_xb_box(hx: float, hy: float, hz: float) -> bytes:
    """Builds a genuine little-endian Parasolid Neutral Binary (.x_b) solid box."""
    out = bytearray()
    out.extend(b"PS\x00\x01")  # Little-Endian IEEE binary Parasolid prefix
    out.extend(
        b"**PART1;\n"
        b"MC=PC;SCH=SCH_32000_320;USFLD_SIZE=0;\n"
        b"**END_OF_HEADER**\n"
    )
    out.extend(b"L\x00")  # Little-endian binary stream marker

    def node_hdr(nt: int, nid: int) -> bytes:
        return struct.pack("<Hi", nt, nid)

    corners = [
        (-hx, -hy, -hz), ( hx, -hy, -hz), ( hx,  hy, -hz), (-hx,  hy, -hz),
        (-hx, -hy,  hz), ( hx, -hy,  hz), ( hx,  hy,  hz), (-hx,  hy,  hz),
    ]
    edge_defs = [
        (0, 1), (1, 2), (2, 3), (3, 0),
        (4, 5), (5, 6), (6, 7), (7, 4),
        (0, 4), (1, 5), (2, 6), (3, 7),
    ]
    face_defs = [
        ((0.0, 0.0, -hz), ( 0.0,  0.0, -1.0), (1.0, 0.0, 0.0), [(0, True),  (3, True),  (2, True),  (1, True)]),
        ((0.0, 0.0,  hz), ( 0.0,  0.0,  1.0), (1.0, 0.0, 0.0), [(4, False), (5, False), (6, False), (7, False)]),
        ((0.0, -hy, 0.0), ( 0.0, -1.0,  0.0), (1.0, 0.0, 0.0), [(0, False), (9, False), (4, True),  (8, True)]),
        (( hx, 0.0, 0.0), ( 1.0,  0.0,  0.0), (0.0, 1.0, 0.0), [(1, False), (10, False),(5, True),  (9, True)]),
        ((0.0,  hy, 0.0), ( 0.0,  1.0,  0.0), (-1.0,0.0, 0.0), [(2, False), (11, False),(6, True),  (10, True)]),
        ((-hx, 0.0, 0.0), (-1.0,  0.0,  0.0), (0.0,-1.0, 0.0), [(3, False), (8, False), (7, True),  (11, True)]),
    ]

    # Node IDs:
    # body=1, shell=2, points=10..17, verts=20..27, lines=30..41, edges=50..61,
    # planes=70..75, faces=80..85, loops=90..95, fins=100..123
    out.extend(node_hdr(12, 1) + struct.pack("<ii", -1, 2))
    out.extend(node_hdr(14, 2) + struct.pack("<i", 80))

    for i, (px, py, pz) in enumerate(corners):
        out.extend(node_hdr(29, 10 + i) + struct.pack("<3d", px, py, pz))
        out.extend(node_hdr(18, 20 + i) + struct.pack("<i", 10 + i))

    for ei, (c0, c1) in enumerate(edge_defs):
        p0, p1 = corners[c0], corners[c1]
        dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
        ln = math.sqrt(dx * dx + dy * dy + dz * dz)
        out.extend(node_hdr(30, 30 + ei) + struct.pack("<6d", p0[0], p0[1], p0[2], dx / ln, dy / ln, dz / ln))
        out.extend(node_hdr(19, 50 + ei) + struct.pack("<iiiB", 20 + c0, 20 + c1, 30 + ei, 0))

    for fi in range(6):
        orig, nrm, uax, fins = face_defs[fi]
        out.extend(node_hdr(35, 70 + fi) + struct.pack("<9d", *orig, *nrm, *uax))
        next_f = (80 + fi + 1) if fi < 5 else -1
        out.extend(node_hdr(15, 80 + fi) + struct.pack("<iiiiB", next_f, 90 + fi, 70 + fi, 1, 0))
        out.extend(node_hdr(16, 90 + fi) + struct.pack("<iii", 80 + fi, -1, 100 + fi * 4))
        for k in range(4):
            e_idx, is_rev = fins[k]
            c0, c1 = edge_defs[e_idx]
            head_v = 20 + (c0 if is_rev else c1)
            next_fin = 100 + fi * 4 + ((k + 1) % 4)
            out.extend(node_hdr(17, 100 + fi * 4 + k) + struct.pack("<iiiB", next_fin, 50 + e_idx, head_v, 1 if is_rev else 0))

    out.extend(node_hdr(0, 0))  # EOF node
    return bytes(out)


# ==============================================================================
# CAD Formats (#1–#2 + Upgraded Real-Container CAD Samples)
# ==============================================================================
def build_hsf_w3d_stream(quantized: bool = False) -> bytes:
    """Builds a binary Hoops Stream Format (W3D V00.55) 3D stream with segments, matrix, and TK_Shell."""
    out = bytearray(b"(W3D V00.55)\n")
    # Open root segment "model"
    seg_name = b"model"
    out.extend(b"(" + bytes([len(seg_name)]) + seg_name)
    # Modelling matrix (translate X=+1.5, Z=+0.5)
    mat = [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        1.5, 0.0, 0.5, 1.0,
    ]
    out.extend(b"M" + struct.pack("<16f", *mat))

    # Hexagonal prism vertices (12 points, 8 faces: 6 side quads + 2 hex caps)
    pts = []
    for z in (0.0, 2.5):
        for k in range(6):
            ang = k * math.pi / 3.0
            pts.append((math.cos(ang) * 1.2, math.sin(ang) * 1.2, z))

    flist: list[int] = [6, 5, 4, 3, 2, 1, 0, 6, 6, 7, 8, 9, 10, 11]
    for k in range(6):
        kn = (k + 1) % 6
        flist.extend([4, k, kn, 6 + kn, 6 + k])

    if not quantized:
        # TK_Shell ('S'): subop=0 (uncompressed), lod=0, ptCount, float32[3*N], flistLen, int32[flistLen]
        out.extend(b"S" + bytes([0x00, 0x00]) + struct.pack("<i", len(pts)))
        for px, py, pz in pts:
            out.extend(struct.pack("<3f", px, py, pz))
        out.extend(struct.pack("<i", len(flist)))
        out.extend(struct.pack(f"<{len(flist)}i", *flist))
        # Also add a TK_Polytriangle (0x14) fin strip
        strip_pts = [(0.0, 0.0, 2.5), (0.8, 0.0, 2.5), (0.0, 0.0, 3.3), (0.8, 0.0, 3.3)]
        out.extend(bytes([0x14, 0x00]) + struct.pack("<i", len(strip_pts)))
        for px, py, pz in strip_pts:
            out.extend(struct.pack("<3f", px, py, pz))
    else:
        # TK_Shell ('S') with 16-bit bounding-box quantized vertices (subop=0x01) + 8-bit CS_TRIVIAL faces (subop|=0x02)
        bbox = (-1.5, -1.5, 0.0, 1.5, 1.5, 2.5)
        out.extend(b"S" + bytes([0x03, 0x00]) + struct.pack("<i", len(pts)))
        out.extend(bytes([0x01]) + struct.pack("<6f", *bbox) + bytes([16]))
        for px, py, pz in pts:
            qx = int(round((px - bbox[0]) / (bbox[3] - bbox[0]) * 65535.0))
            qy = int(round((py - bbox[1]) / (bbox[4] - bbox[1]) * 65535.0))
            qz = int(round((pz - bbox[2]) / (bbox[5] - bbox[2]) * 65535.0))
            out.extend(struct.pack("<3H", qx, qy, qz))
        out.extend(struct.pack("<i", len(flist)) + bytes([0x01, 8]) + bytes(flist))

    out.extend(b")!")  # Close segment + Termination
    return bytes(out)


def gen_autodesk_dwf_samples(cad_dir: Path) -> list[Path]:
    w3d_raw = build_hsf_w3d_stream(quantized=False)
    dwf_zip = write_zip_deterministic([
        ("manifest.xml", b'<?xml version="1.0"?><dwf:Manifest xmlns:dwf="http://schemas.autodesk.com/dwf"/>'),
        ("com.autodesk.dwf.eModel_1/descriptor.xml", b"<eModel/>"),
        ("com.autodesk.dwf.eModel_1/bracket.w3d", w3d_raw),
    ])
    p_dwf = cad_dir / "bracket_assembly.dwf"
    p_dwf.write_bytes(b"(DWF V06.01)" + dwf_zip)

    w3d_q16 = build_hsf_w3d_stream(quantized=True)
    dwfx_zip = write_zip_deterministic([
        ("[Content_Types].xml", b'<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"/>'),
        ("_rels/.rels", b'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>'),
        ("dwf/documents/doc1/sections/flange.w3d", w3d_q16),
    ])
    p_dwfx = cad_dir / "flange_housing.dwfx"
    p_dwfx.write_bytes(dwfx_zip)
    return [p_dwf, p_dwfx]


def gen_additive_cli_samples(cad_dir: Path) -> list[Path]:
    # 1. ASCII .cli (hollow cylinder with stepped flange across 4 layers)
    lines = [
        "$$HEADERSTART",
        "$$ASCII",
        "$$UNITS/0.1",
        "$$VERSION/200",
        "$$LAYERS/4",
        "$$HEADEREND",
        "$$GEOMETRYSTART",
    ]
    for lay_i in range(4):
        z_raw = lay_i * 5.0  # * 0.1 = 0.5 mm layers
        r_out = 25.0 if lay_i < 2 else 18.0
        r_in = 8.0
        lines.append(f"$$LAYER/{z_raw:.1f}")
        outer_pts = []
        for k in range(12):
            a = 2.0 * math.pi * k / 12.0
            outer_pts.append(f"{r_out * math.cos(a):.2f},{r_out * math.sin(a):.2f}")
        outer_pts.append(outer_pts[0])
        lines.append(f"$$POLYLINE/1,1,{len(outer_pts)}," + ",".join(outer_pts))

        hole_pts = []
        for k in range(8):
            a = -2.0 * math.pi * k / 8.0  # CW hole
            hole_pts.append(f"{r_in * math.cos(a):.2f},{r_in * math.sin(a):.2f}")
        hole_pts.append(hole_pts[0])
        lines.append(f"$$POLYLINE/1,0,{len(hole_pts)}," + ",".join(hole_pts))
        lines.append("$$HATCHES/1,2,-15.0,0.0,-9.0,0.0,9.0,0.0,15.0,0.0")
    lines.append("$$GEOMETRYEND")
    p_ascii = cad_dir / "turbine_slice_ascii.cli"
    p_ascii.write_bytes(("\n".join(lines) + "\n").encode("ascii"))

    # 2. Binary .cli (opcodes 127 START LAYER LONG & 130 START POLYLINE LONG)
    b_out = bytearray(
        b"$$HEADERSTART\n$$BINARY\n$$UNITS/0.05\n$$LAYERS/3\n$$HEADEREND\n$$GEOMETRYSTART"
    )
    for lay_i in range(3):
        b_out.extend(struct.pack("<Hf", 127, float(lay_i * 10.0)))
        # Outer square CCW (dir=1)
        sq = [(-30.0, -30.0), (30.0, -30.0), (30.0, 30.0), (-30.0, 30.0), (-30.0, -30.0)]
        b_out.extend(struct.pack("<Hiii", 130, 1, 1, len(sq)))
        for x, y in sq:
            b_out.extend(struct.pack("<2f", x, y))
        # Inner square hole CW (dir=0)
        hl = [(-10.0, -10.0), (-10.0, 10.0), (10.0, 10.0), (10.0, -10.0), (-10.0, -10.0)]
        b_out.extend(struct.pack("<Hiii", 130, 1, 0, len(hl)))
        for x, y in hl:
            b_out.extend(struct.pack("<2f", x, y))
    p_bin = cad_dir / "impeller_slice_bin.cli"
    p_bin.write_bytes(bytes(b_out))
    return [p_ascii, p_bin]


def gen_upgraded_cad_containers(cad_dir: Path) -> list[Path]:
    created: list[Path] = []

    # 1. SolidWorks .sldprt: real MS-CFB OLE2 with RFC 1950 zlib `DisplayLists__Zip` tessellation
    verts = [
        (-2.0, 0.0, -1.5), ( 2.0, 0.0, -1.5), ( 2.0, 0.0,  1.5), (-2.0, 0.0,  1.5),
        (-1.4, 1.8, -1.0), ( 1.4, 1.8, -1.0), ( 1.4, 1.8,  1.0), (-1.4, 1.8,  1.0),
    ]
    tris = [
        (0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
        (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
        (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7),
    ]
    dl = bytearray()
    dl.extend(struct.pack("<II", 20, 1))          # dlVer=20, bodyCount=1
    dl.extend(struct.pack("<III", 0, 0, len(verts)))  # vtxFmt=0 (pos 12B), hasXf=0, nVerts=8
    for vx, vy, vz in verts:
        dl.extend(struct.pack("<3f", vx, vy, vz))
    dl.extend(struct.pack("<I", 1))               # batchCount=1
    dl.extend(struct.pack("<II", 0, len(tris) * 3))  # primType=0 (triList u32), nIndices
    for a, b, c in tris:
        dl.extend(struct.pack("<3I", a, b, c))
    dl_comp = zlib.compress(bytes(dl), level=6)
    sw_stream = struct.pack("<I", len(dl)) + dl_comp
    p_sldprt = cad_dir / "gearbox_cover.sldprt"
    p_sldprt.write_bytes(build_ole2_compound([
        ("Header2", b"SW2024_PART_HDR\x00"),
        ("DisplayLists__Zip", sw_stream),
    ]))
    created.append(p_sldprt)

    # 1b. SolidWorks .sldasm: multi-body OLE2 assembly with Parasolid XT partition stream
    xb_asm = build_parasolid_xb_box(3.2, 1.4, 2.2)
    asm_comp = struct.pack("<I", len(xb_asm)) + zlib.compress(xb_asm, level=6)
    p_sldasm = cad_dir / "solidworks_vtwin.sldasm"
    p_sldasm.write_bytes(build_ole2_compound([
        ("Header2", b"SW2024_ASM_HDR\x00"),
        ("Contents", asm_comp),
    ]))
    created.append(p_sldasm)

    # 2. Autodesk Inventor .ipt & .iam: real MS-CFB OLE2 with RSeDb + framed RSe zlib `BRep` ASM BinaryFile solid
    sab_bytes = build_acis_sab_box(2.5, 1.5, 1.0, magic=b"ASM BinaryFile ")
    rse_comp = zlib.compress(sab_bytes, level=6)
    rse_framed = struct.pack("<I", len(rse_comp)) + rse_comp
    p_ipt = cad_dir / "valve_manifold.ipt"
    p_ipt.write_bytes(build_ole2_compound([
        ("RSeDb", struct.pack("<4I", 1, 2024, 2, 0)),
        ("BRep", rse_framed),
    ]))
    created.append(p_ipt)

    sab_iam = build_acis_sab_box(4.0, 2.0, 1.2, magic=b"ASM BinaryFile ")
    iam_comp = zlib.compress(sab_iam, level=6)
    p_iam = cad_dir / "inventor_drive.iam"
    p_iam.write_bytes(build_ole2_compound([
        ("RSeDb", struct.pack("<4I", 1, 2024, 3, 0)),
        ("BRep", struct.pack("<I", len(iam_comp)) + iam_comp),
    ]))
    created.append(p_iam)

    # 3. Siemens Solid Edge .par & .psm: real MS-CFB OLE2 with `Build` / `Parasolid` zlib-compressed `.x_b` solid
    xb_bytes = build_parasolid_xb_box(1.8, 1.8, 1.2)
    par_comp = struct.pack("<I", len(xb_bytes)) + zlib.compress(xb_bytes, level=6)
    p_par = cad_dir / "bearing_block.par"
    p_par.write_bytes(build_ole2_compound([
        ("Build", b"SolidEdge ST2024\x00"),
        ("Parasolid", par_comp),
    ]))
    created.append(p_par)

    xb_psm = build_parasolid_xb_box(2.6, 0.4, 1.6)
    psm_comp = struct.pack("<I", len(xb_psm)) + zlib.compress(xb_psm, level=6)
    p_psm = cad_dir / "solidedge_bracket.psm"
    p_psm.write_bytes(build_ole2_compound([
        ("Build", b"SolidEdge SheetMetal ST2024\x00"),
        ("Parasolid", psm_comp),
    ]))
    created.append(p_psm)

    # 4. ANSYS SpaceClaim .scdoc: OPC ZIP containing document.xml + SpaceClaim/Geometry/part1.sab
    sc_sab = build_acis_sab_box(3.0, 1.0, 2.0, magic=b"ACIS BinaryFile")
    doc_xml = (
        b'<?xml version="1.0" encoding="utf-8"?>\n'
        b'<SpaceClaimDocument Version="2024">\n'
        b'  <Component Name="FinBlock" Geometry="SpaceClaim/Geometry/part1.sab">\n'
        b'    <Matrix>1 0 0 0.5  0 1 0 0  0 0 1 -0.25  0 0 0 1</Matrix>\n'
        b'  </Component>\n'
        b'</SpaceClaimDocument>\n'
    )
    scdoc_bytes = write_zip_deterministic([
        ("[Content_Types].xml", b'<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"/>'),
        ("SpaceClaim/document.xml", doc_xml),
        ("SpaceClaim/Geometry/part1.sab", sc_sab),
    ])
    p_scdoc = cad_dir / "heat_sink.scdoc"
    p_scdoc.write_bytes(scdoc_bytes)
    created.append(p_scdoc)

    return created


# ==============================================================================
# Engineering FEM / CFD / Aviation / Voxel / Game 3D Formats (#3–#12 + Upgraded 3D)
# ==============================================================================
def gen_fem_cfd_samples(d3_dir: Path) -> list[Path]:
    created: list[Path] = []

    # 3. Abaqus / CalculiX .inp (2 adjacent C3D8 hex elements + C3D4 roof tet + *ASSEMBLY *INSTANCE transform)
    inp_text = """** Abaqus / CalculiX Cantilever Beam Assembly
*HEADING
Cantilever Beam with Tip Cap
*PART, NAME=BEAM_PART
*NODE
1, 0.0, 0.0, 0.0
2, 2.0, 0.0, 0.0
3, 2.0, 1.0, 0.0
4, 0.0, 1.0, 0.0
5, 0.0, 0.0, 1.0
6, 2.0, 0.0, 1.0
7, 2.0, 1.0, 1.0
8, 0.0, 1.0, 1.0
9, 4.0, 0.0, 0.0
10, 4.0, 1.0, 0.0
11, 4.0, 0.0, 1.0
12, 4.0, 1.0, 1.0
13, 5.0, 0.5, 0.5
*ELEMENT, TYPE=C3D8, ELSET=EALL
1, 1, 2, 3, 4, 5, 6, 7, 8
2, 2, 9, 10, 3, 6, 11, 12, 7
*ELEMENT, TYPE=C3D4, ELSET=ETIP
3, 9, 10, 12, 13
4, 9, 12, 11, 13
*END PART
*ASSEMBLY, NAME=ASSY1
*INSTANCE, NAME=BEAM-1, PART=BEAM_PART
1.0, 0.5, 0.0
0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 15.0
*END INSTANCE
*END ASSEMBLY
"""
    p_inp = d3_dir / "cantilever_beam.inp"
    p_inp.write_bytes(inp_text.encode("ascii"))
    created.append(p_inp)

    # 4. ANSYS .cdb (NBLOCK fixed-width (3i8,6e20.13) + EBLOCK 19-int SOLID with hex & collapsed wedge)
    cdb_nodes = [
        (1, 0.0, 0.0, 0.0), (2, 2.0, 0.0, 0.0), (3, 2.0, 2.0, 0.0), (4, 0.0, 2.0, 0.0),
        (5, 0.0, 0.0, 3.0), (6, 2.0, 0.0, 3.0), (7, 2.0, 2.0, 3.0), (8, 0.0, 2.0, 3.0),
        (9, 3.5, 1.0, 0.0), (10, 3.5, 1.0, 3.0),
    ]
    cdb_lines = [
        "/COM,ANSYS Release 2024 R1 Archive",
        "/PREP7",
        "NBLOCK,6,SOLID,10,10",
        "(3i8,6e20.13)",
    ]
    for nid, x, y, z in cdb_nodes:
        cdb_lines.append(f"{nid:8d}{0:8d}{0:8d}{x:20.13E}{y:20.13E}{z:20.13E}")
    cdb_lines.extend([
        "N,R5.3,LOC,       -1,",
        "EBLOCK,19,SOLID,2,2",
        "(19i9)",
        # Elem 1: full 8-node brick (1..8)
        "".join(f"{v:9d}" for v in [1, 1, 1, 1, 0, 0, 0, 0, 8, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8]),
        # Elem 2: ANSYS collapsed-brick 6-node wedge (2, 9, 3, 3, 6, 10, 7, 7) sharing quad (2,3,7,6)!
        "".join(f"{v:9d}" for v in [1, 1, 1, 1, 0, 0, 0, 0, 8, 0, 2, 2, 9, 3, 3, 6, 10, 7, 7]),
        "       -1",
        "FINISH",
    ])
    p_cdb = d3_dir / "pressure_vessel.cdb"
    p_cdb.write_bytes(("\n".join(cdb_lines) + "\n").encode("ascii"))
    created.append(p_cdb)

    # 5. MSC / NX Nastran .nas (CORD2R, small-field GRID, large-field GRID*, exponentless floats, CHEXA +CONT, CPENTA)
    nas_lines = [
        "ID NASTRAN,WING_RIB",
        "SOL 101",
        "CEND",
        "BEGIN BULK",
        "$ Local rectangular coordinate system translated +5.0 in X",
        "CORD2R         1       0     5.0     0.0     0.0     5.0     0.0     1.0+C1     ",
        "+C1          6.0     0.0     0.0",
        "$ Small-field & exponentless float GRID cards in CID=1",
        "GRID           1       1     0.0     0.0     0.0",
        "GRID           2       1  2.5+0      0.0     0.0",
        "GRID           3       1  2.5+0   1.5+0      0.0",
        "GRID           4       1     0.0  1.5+0      0.0",
        "GRID           5       1     0.0     0.0  1.2+0 ",
        "GRID           6       1  2.5+0      0.0  1.2+0 ",
        "$ Large-field GRID* two-line cards",
        "GRID*                  7               1      2.500000+0      1.500000+0*G7",
        "*G7           1.200000+0",
        "GRID*                  8               1             0.0      1.500000+0*G8",
        "*G8           1.200000+0",
        "$ Free-field GRID cards for wedge tip",
        "GRID,9,1,4.0,0.75,0.0",
        "GRID,10,1,4.0,0.75,1.2",
        "$ Solid CHEXA + CPENTA sharing face (2,3,7,6)",
        "CHEXA        101       1       1       2       3       4       5       6+HX1    ",
        "+HX1           7       8",
        "CPENTA       102       1       2       9       3       6      10       7",
        "ENDDATA",
    ]
    p_nas = d3_dir / "wing_rib.nas"
    p_nas.write_bytes(("\n".join(nas_lines) + "\n").encode("ascii"))
    created.append(p_nas)

    # 6. ANSYS Fluent .cas (hexadecimal node & face zones; 2-cell duct where shared interior quad is culled)
    cas_text = """(0 "ANSYS Fluent 3D Combustor Duct Mesh")
(2 3)
(10 (0 1 c 0 3))
(10 (1 1 c 1 3)(
 0.0 0.0 0.0
 2.0 0.0 0.0
 2.0 1.5 0.0
 0.0 1.5 0.0
 0.0 0.0 1.2
 2.0 0.0 1.2
 2.0 1.5 1.2
 0.0 1.5 1.2
 4.2 0.2 0.1
 4.2 1.3 0.1
 4.2 0.2 1.1
 4.2 1.3 1.1
))
(12 (0 1 2 0))
(12 (2 1 2 1 4))
(13 (0 1 b 0))
(13 (3 1 1 2 4)(
 2 3 7 6 1 2
))
(13 (4 2 b 3 4)(
 1 4 3 2 1 0
 5 6 7 8 1 0
 1 2 6 5 1 0
 4 1 5 8 1 0
 3 4 8 7 1 0
 2 9 a 3 2 0
 6 7 c b 2 0
 2 6 b 9 2 0
 3 a c 7 2 0
 9 b c a 2 0
))
"""
    p_cas = d3_dir / "combustor_duct.cas"
    p_cas.write_bytes(cas_text.encode("ascii"))
    created.append(p_cas)

    # 7. Laminar Research X-Plane .obj8 (with nested ANIM_trans + ANIM_rotate)
    obj8_text = """I
800
OBJ

TEXTURE aileron.png
POINT_COUNTS 8 0 0 36

VT -1.50 -0.10 -0.40  0 -1  0  0.0 0.0
VT  1.50 -0.10 -0.40  0 -1  0  1.0 0.0
VT  1.50 -0.10  0.40  0 -1  0  1.0 1.0
VT -1.50 -0.10  0.40  0 -1  0  0.0 1.0
VT -1.50  0.15 -0.40  0  1  0  0.0 0.0
VT  1.50  0.15 -0.40  0  1  0  1.0 0.0
VT  1.50  0.02  0.40  0  1  0  1.0 1.0
VT -1.50  0.02  0.40  0  1  0  0.0 1.0

IDX10 0 1 2 0 2 3 4 6 5 4
IDX10 7 6 0 4 5 0 5 1 1 5
IDX10 6 1 6 2 2 6 7 2 7 3
IDX 3
IDX 7
IDX 4
IDX 3
IDX 4
IDX 0

ANIM_begin
ANIM_trans 0.0 1.25 -0.50 0.0 1.25 -0.50 0.0 1.0 none
ANIM_rotate 1.0 0.0 0.0 -12.0 12.0 -1.0 1.0 sim/flightmodel2/wing/aileron1_deg
TRIS 0 36
ANIM_end
"""
    p_obj8 = d3_dir / "cessna_aileron.obj8"
    p_obj8.write_bytes(obj8_text.encode("ascii"))
    created.append(p_obj8)

    return created


# ==============================================================================
# Voxel & Game Formats (#8–#12) + Upgraded Cinema4D / Granny2 / ZBrush
# ==============================================================================
def _nbt_str(s: str) -> bytes:
    b = s.encode("utf-8")
    return struct.pack(">H", len(b)) + b


def _nbt_tag_hdr(tag_type: int, name: str) -> bytes:
    return bytes([tag_type]) + _nbt_str(name)


def _encode_varint(v: int) -> bytes:
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def gen_voxel_game_samples(d3_dir: Path) -> list[Path]:
    created: list[Path] = []

    # 8. Minetest / Luanti Schematic (.mts, MTSM v4 BE)
    sx, sy, sz = 5, 6, 5
    mts = bytearray(b"MTSM")
    mts.extend(struct.pack(">HHHH", 4, sx, sy, sz))
    mts.extend(bytes([127] * sy))  # Y-slice probabilities
    names = ["air", "default:stonebrick", "default:wood"]
    mts.extend(struct.pack(">H", len(names)))
    for nm in names:
        mts.extend(_nbt_str(nm))

    n_vox = sx * sy * sz
    param0 = bytearray()
    for iz in range(sz):
        for iy in range(sy):
            for ix in range(sx):
                nid = 0
                if iy == 0:
                    nid = 1
                elif iy < 5 and (ix in (0, sx - 1) or iz in (0, sz - 1)) and not (ix == 2 and iz == 0 and iy in (1, 2)):
                    nid = 1
                elif iy == 5 and (ix % 2 == 0 or iz % 2 == 0):
                    nid = 2
                param0.extend(struct.pack(">H", nid))
    param1 = bytes([0x7F] * n_vox)
    param2 = bytes([0] * n_vox)
    mts.extend(zlib.compress(bytes(param0) + param1 + param2, level=6))
    p_mts = d3_dir / "watchtower.mts"
    p_mts.write_bytes(bytes(mts))
    created.append(p_mts)

    # 9a. Minecraft Sponge Schematic v2 (.schem)
    w, h, l = 5, 4, 5
    pal_body = (
        _nbt_tag_hdr(3, "minecraft:air") + struct.pack(">i", 0)
        + _nbt_tag_hdr(3, "minecraft:cobblestone") + struct.pack(">i", 1)
        + _nbt_tag_hdr(3, "minecraft:oak_planks[axis=y]") + struct.pack(">i", 2)
        + b"\x00"
    )
    bdata = bytearray()
    for iy in range(h):
        for iz in range(l):
            for ix in range(w):
                bid = 0
                if iy == 0:
                    bid = 1
                elif iy < 3 and (ix in (0, w - 1) or iz in (0, l - 1)):
                    bid = 2
                elif iy == 3 and 1 <= ix <= 3 and 1 <= iz <= 3:
                    bid = 2
                bdata.extend(_encode_varint(bid))
    schem_nbt = (
        _nbt_tag_hdr(10, "Schematic")
        + _nbt_tag_hdr(3, "Version") + struct.pack(">i", 2)
        + _nbt_tag_hdr(3, "DataVersion") + struct.pack(">i", 3465)
        + _nbt_tag_hdr(2, "Width") + struct.pack(">h", w)
        + _nbt_tag_hdr(2, "Height") + struct.pack(">h", h)
        + _nbt_tag_hdr(2, "Length") + struct.pack(">h", l)
        + _nbt_tag_hdr(10, "Palette") + pal_body
        + _nbt_tag_hdr(7, "BlockData") + struct.pack(">i", len(bdata)) + bytes(bdata)
        + b"\x00"
    )
    p_schem = d3_dir / "cottage_sponge.schem"
    p_schem.write_bytes(gzip.compress(schem_nbt, compresslevel=6, mtime=0))
    created.append(p_schem)

    # 9b. Minecraft MCEdit Alpha Schematic (.schematic)
    mw, mh, ml = 5, 5, 3
    mblocks = bytearray()
    for iy in range(mh):
        for iz in range(ml):
            for ix in range(mw):
                solid = (iz == 1) and (ix in (0, mw - 1) or iy == mh - 1)
                mblocks.append(98 if solid else 0)
    mdata = bytes([0] * len(mblocks))
    mcedit_nbt = (
        _nbt_tag_hdr(10, "Schematic")
        + _nbt_tag_hdr(2, "Width") + struct.pack(">h", mw)
        + _nbt_tag_hdr(2, "Height") + struct.pack(">h", mh)
        + _nbt_tag_hdr(2, "Length") + struct.pack(">h", ml)
        + _nbt_tag_hdr(8, "Materials") + _nbt_str("Alpha")
        + _nbt_tag_hdr(7, "Blocks") + struct.pack(">i", len(mblocks)) + bytes(mblocks)
        + _nbt_tag_hdr(7, "Data") + struct.pack(">i", len(mdata)) + mdata
        + b"\x00"
    )
    p_schematic = d3_dir / "archway_mcedit.schematic"
    p_schematic.write_bytes(gzip.compress(mcedit_nbt, compresslevel=6, mtime=0))
    created.append(p_schematic)

    # 9c. Vanilla Minecraft Structure (.nbt)
    portal_coords = [
        (0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0),
        (0, 1, 0), (3, 1, 0),
        (0, 2, 0), (3, 2, 0),
        (0, 3, 0), (3, 3, 0),
        (0, 4, 0), (1, 4, 0), (2, 4, 0),
    ]
    blocks_elems = bytearray()
    for bx, by, bz in portal_coords:
        blocks_elems.extend(
            _nbt_tag_hdr(9, "pos") + bytes([3]) + struct.pack(">iiii", 3, bx, by, bz)
            + _nbt_tag_hdr(3, "state") + struct.pack(">i", 1)
            + b"\x00"
        )
    struct_nbt = (
        _nbt_tag_hdr(10, "")
        + _nbt_tag_hdr(3, "DataVersion") + struct.pack(">i", 3465)
        + _nbt_tag_hdr(9, "size") + bytes([3]) + struct.pack(">iiii", 3, 4, 5, 1)
        + _nbt_tag_hdr(9, "palette") + bytes([10]) + struct.pack(">i", 2)
        + (_nbt_tag_hdr(8, "Name") + _nbt_str("minecraft:air") + b"\x00")
        + (_nbt_tag_hdr(8, "Name") + _nbt_str("minecraft:obsidian") + b"\x00")
        + _nbt_tag_hdr(9, "blocks") + bytes([10]) + struct.pack(">i", len(portal_coords))
        + bytes(blocks_elems)
        + b"\x00"
    )
    p_nbt = d3_dir / "ruined_portal.nbt"
    p_nbt.write_bytes(gzip.compress(struct_nbt, compresslevel=6, mtime=0))
    created.append(p_nbt)

    # 10. Sony PlayStation 1 TMD (.tmd — id=0x41, flags=0, SVECTORs + flat/Gouraud/textured/unlit GTE packets)
    tmd_verts = [
        (   0,  -80,  600),  # 0 nose
        (-280,   40, -300),  # 1 left wing
        ( 280,   40, -300),  # 2 right wing
        (   0, -140, -240),  # 3 canopy ridge
        (-120,   80, -320),  # 4 belly left
        ( 120,   80, -320),  # 5 belly right
    ]
    tmd_norms = [(0, -4096, 0), (0, 4096, 0), (-2896, -2896, 0), (2896, -2896, 0)]
    v_blob = b"".join(struct.pack("<4h", vx, vy, vz, 0) for vx, vy, vz in tmd_verts)
    n_blob = b"".join(struct.pack("<4h", nx, ny, nz, 0) for nx, ny, nz in tmd_norms)

    prims = bytearray()
    # Pkt 1: Flat untextured lit tri (mode=0x20, flag=0x00, ilen=3) -> canopy left (0, 3, 1)
    prims.extend(bytes([4, 3, 0x00, 0x20, 60, 140, 250, 0x20]) + struct.pack("<4H", 2, 0, 3, 1))
    # Pkt 2: Gouraud untextured lit tri (mode=0x30, flag=0x00, ilen=4) -> canopy right (0, 2, 3)
    prims.extend(bytes([6, 4, 0x00, 0x30, 60, 140, 250, 0x30]) + struct.pack("<6H", 3, 0, 3, 2, 0, 3))
    # Pkt 3: Flat textured lit quad (mode=0x2C, flag=0x00, ilen=7) -> rear hull quad (1, 3, 4, 5) -> Z-quad (1,3,4)+(3,5,4)
    prims.extend(
        bytes([9, 7, 0x00, 0x2C])
        + struct.pack("<BBHBBHBBHBBH", 0, 0, 0, 255, 0, 0, 0, 255, 0, 255, 255, 0)
        + struct.pack("<6H", 1, 1, 2, 4, 5, 0)
    )
    # Pkt 4 & 5: Unlit flat untextured tris (mode=0x20, flag=0x01, ilen=3) -> belly (0, 1, 4) & (0, 4, 5) & (0, 5, 2)
    for a, b, c in [(0, 1, 4), (0, 4, 5), (0, 5, 2)]:
        prims.extend(bytes([3, 3, 0x01, 0x20, 200, 200, 220, 0x20]) + struct.pack("<4H", a, b, c, 0))

    # Offsets relative to object table at +12: obj table = 28B
    vert_top = 28
    norm_top = vert_top + len(v_blob)
    prim_top = norm_top + len(n_blob)
    tmd_hdr = struct.pack("<3I", 0x00000041, 0, 1)
    tmd_obj = struct.pack("<6Ii", vert_top, len(tmd_verts), norm_top, len(tmd_norms), prim_top, 6, 0)
    p_tmd = d3_dir / "wipeout_ship.tmd"
    p_tmd.write_bytes(tmd_hdr + tmd_obj + v_blob + n_blob + bytes(prims))
    created.append(p_tmd)

    # 11. Westwood / EA SAGE W3D (.w3d — HIERARCHY + HLOD + 2 MESH chunks: hull + elevated rotated turret)
    def w3d_chunk(ctype: int, payload: bytes, sub: bool = False) -> bytes:
        sz = len(payload) | (0x80000000 if sub else 0)
        return struct.pack("<II", ctype, sz) + payload

    def fixed_str(s: str, n: int) -> bytes:
        b = s.encode("ascii")[: n - 1]
        return b + b"\x00" * (n - len(b))

    # Hierarchy: Root(0), Hull(1 -> parent 0), Turret(2 -> parent 1, translated Z=+1.1, rotated 30 deg around Z)
    ang_half = math.radians(15.0)
    piv0 = fixed_str("RootTransform", 16) + struct.pack("<i3f3f4f", -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1)
    piv1 = fixed_str("HullBone", 16)      + struct.pack("<i3f3f4f",  0, 0, 0, 0.4, 0, 0, 0, 0, 0, 0, 1)
    piv2 = fixed_str("TurretBone", 16)    + struct.pack("<i3f3f4f",  1, 0, 0, 0.9, 0, 0, 0.52, 0, 0, math.sin(ang_half), math.cos(ang_half))
    hier_hdr = struct.pack("<I", 0x00040001) + fixed_str("GDI_TANK", 16) + struct.pack("<I3f", 3, 0, 0, 0)
    hier_chunk = w3d_chunk(0x100, w3d_chunk(0x101, hier_hdr) + w3d_chunk(0x102, piv0 + piv1 + piv2), sub=True)

    def build_w3d_box_mesh(mname: str, cname: str, hx: float, hy: float, hz: float, skinned_bone: int | None) -> bytes:
        bv = [
            (-hx, -hy, -hz), ( hx, -hy, -hz), ( hx,  hy, -hz), (-hx,  hy, -hz),
            (-hx, -hy,  hz), ( hx, -hy,  hz), ( hx,  hy,  hz), (-hx,  hy,  hz),
        ]
        bt = [
            (0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
            (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
            (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7),
        ]
        mhdr = bytearray(116)
        struct.pack_into("<II", mhdr, 0, 0x00040002, 0x00020000 if skinned_bone is not None else 0)
        mhdr[8:24] = fixed_str(mname, 16)
        mhdr[24:40] = fixed_str(cname, 16)
        struct.pack_into("<II", mhdr, 40, len(bt), len(bv))
        v_pay = b"".join(struct.pack("<3f", *p) for p in bv)
        t_pay = b"".join(struct.pack("<3II3ff", a, b, c, 0, 0.0, 0.0, 1.0, 0.0) for a, b, c in bt)
        body = w3d_chunk(0x1F, bytes(mhdr)) + w3d_chunk(0x02, v_pay)
        if skinned_bone is not None:
            inf_pay = b"".join(struct.pack("<4H", skinned_bone, 0, 100, 0) for _ in bv)
            body += w3d_chunk(0x0E, inf_pay)
        body += w3d_chunk(0x20, t_pay)
        return w3d_chunk(0x00, body, sub=True)

    mesh_hull = build_w3d_box_mesh("HULL", "GDI_TANK", 2.2, 1.4, 0.45, skinned_bone=1)
    mesh_turret = build_w3d_box_mesh("TURRET", "GDI_TANK", 1.1, 0.95, 0.35, skinned_bone=None)
    sub1 = struct.pack("<I", 1) + fixed_str("GDI_TANK.HULL", 32)
    sub2 = struct.pack("<I", 2) + fixed_str("GDI_TANK.TURRET", 32)
    lod_arr = w3d_chunk(0x702, w3d_chunk(0x703, sub1) + w3d_chunk(0x703, sub2), sub=True)
    hlod_chunk = w3d_chunk(0x700, lod_arr, sub=True)

    p_w3d = d3_dir / "gdi_tank.w3d"
    p_w3d.write_bytes(hier_chunk + mesh_hull + mesh_turret + hlod_chunk)
    created.append(p_w3d)

    # 12. Bungie Halo CE Jointed Model Skeleton (.jms v8200)
    jms_verts = [
        (-1.0, -1.0, 0.0), ( 1.0, -1.0, 0.0), ( 1.0,  1.0, 0.0), (-1.0,  1.0, 0.0),
        (-0.8, -0.8, 1.6), ( 0.8, -0.8, 1.6), ( 0.8,  0.8, 1.6), (-0.8,  0.8, 1.6),
        ( 0.0, -1.15, 0.9),
    ]
    jms_tris = [
        (0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
        (0, 1, 8), (1, 5, 8), (5, 4, 8), (4, 0, 8),
        (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7),
    ]
    jms_lines = [
        "8200",
        "325194",
        "2",
        "frame root",
        "1",
        "-1",
        "0.000000\t0.000000\t0.000000\t1.000000",
        "0.000000\t0.000000\t0.000000",
        "frame visor",
        "-1",
        "-1",
        "0.000000\t0.000000\t0.000000\t1.000000",
        "0.000000\t-0.500000\t0.900000",
        "1",
        "cyborg_helmet",
        "<none>",
        "1",
        "head_marker",
        "0",
        "1",
        "0.0\t0.0\t0.0\t1.0",
        "0.0\t0.0\t1.2",
        "0.25",
        "1",
        "default",
        str(len(jms_verts)),
    ]
    for vi, (vx, vy, vz) in enumerate(jms_verts):
        jms_lines.extend([
            "0",
            f"{vx:.6f}\t{vy:.6f}\t{vz:.6f}",
            "0.000000\t0.000000\t1.000000",
            "1" if vi == 8 else "-1",
            "0.500000" if vi == 8 else "0.000000",
            "0.500000",
            "0.500000",
            "0",
        ])
    jms_lines.append(str(len(jms_tris)))
    for a, b, c in jms_tris:
        jms_lines.extend(["0", "0", f"{a}\t{b}\t{c}"])
    p_jms = d3_dir / "spartan_helmet.jms"
    p_jms.write_bytes(("\n".join(jms_lines) + "\n").encode("ascii"))
    created.append(p_jms)

    return created


def _packbits_rle_encode(raw: bytes) -> bytes:
    """Encodes raw bytes into ZBrush PackBits-compatible RLE packets."""
    out = bytearray()
    i = 0
    n = len(raw)
    while i < n:
        # Check run of identical bytes
        run = 1
        while i + run < n and run < 128 and raw[i + run] == raw[i]:
            run += 1
        if run >= 3:
            out.append((256 + (1 - run)) & 0xFF)
            out.append(raw[i])
            i += run
        else:
            lit_start = i
            lit = 0
            while i < n and lit < 128:
                r2 = 1
                while i + r2 < n and r2 < 4 and raw[i + r2] == raw[i]:
                    r2 += 1
                if r2 >= 4 and lit > 0:
                    break
                i += 1
                lit += 1
            out.append(lit - 1)
            out.extend(raw[lit_start : lit_start + lit])
    return bytes(out)


def _transpose_word_planes(raw: bytes) -> bytes:
    assert len(raw) % 4 == 0
    w = len(raw) // 4
    p0 = bytearray(w)
    p1 = bytearray(w)
    p2 = bytearray(w)
    p3 = bytearray(w)
    for i in range(w):
        p0[i] = raw[i * 4 + 0]
        p1[i] = raw[i * 4 + 1]
        p2[i] = raw[i * 4 + 2]
        p3[i] = raw[i * 4 + 3]
    return bytes(p0 + p1 + p2 + p3)


def gen_upgraded_dcc_game_samples(d3_dir: Path) -> list[Path]:
    created: list[Path] = []

    # 13. Maxon Cinema 4D HyperFile (.c4d — C4DC header + Opolygon=5100 + Matrix64 + Tpoint=5600 + Tpolygon=5604)
    def hf_chunk(cid: int, level: int, payload: bytes) -> bytes:
        return b"\x02" + struct.pack(">III", cid, level, len(payload)) + payload + b"\x03"

    c4d_pts = [
        (-1.5,  0.0, -1.5), ( 1.5,  0.0, -1.5), ( 1.5,  0.0,  1.5), (-1.5,  0.0,  1.5),
        (-0.9,  2.2, -0.9), ( 0.9,  2.2, -0.9), ( 0.9,  2.2,  0.9), (-0.9,  2.2,  0.9),
        ( 0.0,  3.1,  0.0),
    ]
    # Quads (a,b,c,d) and apex triangles (a,b,c,c)
    c4d_polys = [
        (0, 3, 2, 1),
        (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7),
        (4, 5, 8, 8), (5, 6, 8, 8), (6, 7, 8, 8), (7, 4, 8, 8),
    ]
    tpoint_pay = struct.pack(">I", len(c4d_pts)) + b"".join(struct.pack(">3d", x, y, z) for x, y, z in c4d_pts)
    tpoly_pay  = struct.pack(">I", len(c4d_polys)) + b"".join(struct.pack(">4I", a, b, c, d) for a, b, c, d in c4d_polys)
    # HYPERFILEVALUE_MATRIX64 (25 = 0x19): off=(0.5, 0, 0), v1=(1,0,0), v2=(0,1,0), v3=(0,0,1)
    mg_val = b"\x19" + struct.pack(">12d", 0.5, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
    opol_chunk = hf_chunk(5100, 1, mg_val + hf_chunk(5600, 2, tpoint_pay) + hf_chunk(5604, 2, tpoly_pay))
    p_c4d = d3_dir / "cinema_hyperfile.c4d"
    p_c4d.write_bytes(b"\x58C4DC" + struct.pack(">I", 2024) + opol_chunk)
    created.append(p_c4d)

    # 14. RAD Game Tools Granny2 (.gr2 v7 32-bit LE, 2 sections with LZ compression + PointerFixups + TypeDefinitions)
    sec0 = bytearray()
    fixups0: list[tuple[int, int, int]] = []  # (fromOff, toSec, toOff)

    def alloc0(b: bytes, align: int = 4) -> int:
        while len(sec0) % align != 0:
            sec0.append(0)
        off = len(sec0)
        sec0.extend(b)
        return off

    str_cache: dict[str, int] = {}

    def add_cstr(s: str) -> int:
        if s not in str_cache:
            str_cache[s] = alloc0(s.encode("ascii") + b"\x00", align=1)
        return str_cache[s]

    def emit_typedef(fields: list[tuple[int, str, int | None, int]]) -> int:
        """Each field: (memberType, name, refTypeOffInSec0, arrayWidth). sizeof(entry) = 32B in 32-bit GR2."""
        while len(sec0) % 4 != 0:
            sec0.append(0)
        base = len(sec0)
        sec0.extend(b"\x00" * (32 * (len(fields) + 1)))
        for idx, (mtype, fname, ref_off, arr_w) in enumerate(fields):
            foff = base + idx * 32
            name_off = add_cstr(fname)
            struct.pack_into("<i", sec0, foff + 0, mtype)
            fixups0.append((foff + 4, 0, name_off))
            if ref_off is not None:
                fixups0.append((foff + 8, 0, ref_off))
            struct.pack_into("<i", sec0, foff + 12, arr_w)
        # Trailing EndMember = 0 (already zeroed)
        return base

    # VertexType: Position float[3], Normal float[3]
    td_vertex = emit_typedef([
        (10, "Position", None, 3),  # Real32Member[3]
        (10, "Normal", None, 3),    # Real32Member[3]
    ])
    # GroupType: MaterialIndex int32, TriFirst int32, TriCount int32
    td_group = emit_typedef([
        (19, "MaterialIndex", None, 0),
        (19, "TriFirst", None, 0),
        (19, "TriCount", None, 0),
    ])
    # VertexDataType: Vertices (ReferenceToVariantArrayMember = 7)
    td_vdata = emit_typedef([
        (7, "Vertices", None, 0),
    ])
    # TriTopologyType: Groups (ReferenceToArrayMember = 3 -> td_group), Indices16 (ReferenceToArrayMember = 3)
    td_topo = emit_typedef([
        (3, "Groups", td_group, 0),
        (3, "Indices16", None, 0),
    ])
    # MeshType: Name (StringMember = 8), PrimaryVertexData (Ref = 2 -> td_vdata), PrimaryTopology (Ref = 2 -> td_topo)
    td_mesh = emit_typedef([
        (8, "Name", None, 0),
        (2, "PrimaryVertexData", td_vdata, 0),
        (2, "PrimaryTopology", td_topo, 0),
    ])
    # Root FileInfoType: Meshes (ArrayOfReferencesMember = 4 -> td_mesh)
    td_root = emit_typedef([
        (4, "Meshes", td_mesh, 0),
    ])

    # Section 1 (compressed with Granny LZ token stream): Vertex array + Group array + Indices16 array
    sec1 = bytearray()

    def alloc1(b: bytes, align: int = 4) -> int:
        while len(sec1) % align != 0:
            sec1.append(0)
        off = len(sec1)
        sec1.extend(b)
        return off

    gr2_verts = [
        ( 0.0,  1.4,  0.25, 0.0, 0.0, 1.0),
        (-1.0,  0.8,  0.10, 0.0, 0.0, 1.0),
        ( 1.0,  0.8,  0.10, 0.0, 0.0, 1.0),
        (-0.9, -0.5,  0.10, 0.0, 0.0, 1.0),
        ( 0.9, -0.5,  0.10, 0.0, 0.0, 1.0),
        ( 0.0, -1.5,  0.20, 0.0, 0.0, 1.0),
        ( 0.0,  0.1, -0.15, 0.0, 0.0,-1.0),
    ]
    gr2_tris = [
        (0, 1, 2), (1, 3, 4), (1, 4, 2), (3, 5, 4),
        (0, 2, 6), (2, 4, 6), (4, 5, 6), (5, 3, 6), (3, 1, 6), (1, 0, 6),
    ]
    vbuf_off1 = alloc1(b"".join(struct.pack("<6f", *v) for v in gr2_verts))
    gbuf_off1 = alloc1(struct.pack("<3i", 0, 0, len(gr2_tris)))
    ibuf_off1 = alloc1(b"".join(struct.pack("<3H", *t) for t in gr2_tris), align=2)
    while len(sec1) % 4 != 0:
        sec1.append(0)

    # Objects in Section 0 referencing Section 0 & Section 1 via pointer fixups:
    # 1) PrimaryVertexData (12B: typePtr@+0, count@+4, dataPtr@+8)
    pv_off0 = alloc0(struct.pack("<III", 0, len(gr2_verts), 0))
    fixups0.append((pv_off0 + 0, 0, td_vertex))
    fixups0.append((pv_off0 + 8, 1, vbuf_off1))

    # 2) PrimaryTopology (16B: gCount@+0, gPtr@+4, i16Count@+8, i16Ptr@+12)
    pt_off0 = alloc0(struct.pack("<IIII", 1, 0, len(gr2_tris) * 3, 0))
    fixups0.append((pt_off0 + 4, 1, gbuf_off1))
    fixups0.append((pt_off0 + 12, 1, ibuf_off1))

    # 3) Mesh (12B: Name@+0, PrimaryVertexData@+4, PrimaryTopology@+8)
    mesh_name_off = add_cstr("KnightShield")
    mesh_off0 = alloc0(struct.pack("<III", 0, 0, 0))
    fixups0.append((mesh_off0 + 0, 0, mesh_name_off))
    fixups0.append((mesh_off0 + 4, 0, pv_off0))
    fixups0.append((mesh_off0 + 8, 0, pt_off0))

    # 4) Mesh pointer array (4B) + RootObject (8B: meshCount=1, meshArrPtr)
    mesh_arr_off0 = alloc0(struct.pack("<I", 0))
    fixups0.append((mesh_arr_off0, 0, mesh_off0))
    root_obj_off0 = alloc0(struct.pack("<II", 1, 0))
    fixups0.append((root_obj_off0 + 4, 0, mesh_arr_off0))

    # Compress Section 1 using Granny LZ literal-token framing (control byte < 0x80 = literal run of ctrl+1 bytes)
    sec1_lz = bytearray()
    s1_raw = bytes(sec1)
    for pos1 in range(0, len(s1_raw), 64):
        chunk1 = s1_raw[pos1 : pos1 + 64]
        sec1_lz.append(len(chunk1) - 1)
        sec1_lz.extend(chunk1)

    fixup_blob0 = b"".join(struct.pack("<3I", fo, ts, to) for fo, ts, to in fixups0)

    # Assemble complete .gr2 binary:
    # 0x00..0x1F: Magicblock (32B)
    # 0x20..0x67: GrannyFileHeader (72B -> secArrRel = 0x48 -> 0x68)
    # 0x68..0xBF: 2 * 44B section headers (88B -> ends at 0xC0)
    magic_v7_32le = bytes([
        0x29, 0xDE, 0x6C, 0xC0, 0xBA, 0xA4, 0x53, 0x2B,
        0x25, 0xF5, 0xB7, 0xA5, 0xF6, 0x66, 0xE2, 0xEE,
    ])
    hdr_end = 0xC0
    sec0_data_off = hdr_end
    sec1_data_off = sec0_data_off + len(sec0)
    fix0_off = sec1_data_off + len(sec1_lz)
    total_gr2_size = fix0_off + len(fixup_blob0)

    gr2 = bytearray(hdr_end)
    gr2[0:16] = magic_v7_32le
    struct.pack_into("<IIII", gr2, 0x10, hdr_end, 0, 0, 0)
    # FileHeader at 0x20:
    struct.pack_into("<IIII", gr2, 0x20, 7, total_gr2_size, 0, 0x48)  # version=7, totalBytes, crc=0, secArrRel=0x48
    struct.pack_into("<I", gr2, 0x30, 2)                              # secArrCount=2
    struct.pack_into("<II", gr2, 0x34, 0, td_root)                    # rootType = (0, td_root)
    struct.pack_into("<II", gr2, 0x3C, 0, root_obj_off0)              # rootObj  = (0, root_obj_off0)

    # Section 0 header at 0x68 (uncompressed):
    struct.pack_into(
        "<11I", gr2, 0x68,
        0, sec0_data_off, len(sec0), len(sec0), 4, len(sec0), len(sec0), fix0_off, len(fixups0), 0, 0
    )
    # Section 1 header at 0x68 + 44 = 0x94 (compression=1 LZ):
    struct.pack_into(
        "<11I", gr2, 0x94,
        1, sec1_data_off, len(sec1_lz), len(s1_raw), 4, len(s1_raw), len(s1_raw), 0, 0, 0, 0
    )
    gr2.extend(sec0)
    gr2.extend(sec1_lz)
    gr2.extend(fixup_blob0)
    p_gr2 = d3_dir / "knight_shield.gr2"
    p_gr2.write_bytes(bytes(gr2))
    created.append(p_gr2)

    # 15 & 16. Pixologic ZBrush (.ztl with flags=3 RLE + word-plane transpose, .zpr with Proj->Tool->SubT RLE)
    zb_verts = [
        (-1.0, -1.0, -1.0), ( 1.0, -1.0, -1.0), ( 1.0,  1.0, -1.0), (-1.0,  1.0, -1.0),
        (-0.8, -0.8,  1.2), ( 0.8, -0.8,  1.2), ( 0.8,  0.8,  1.2), (-0.8,  0.8,  1.2),
    ]
    zb_polys = [
        (0, 3, 2, 1), (4, 5, 6, 7),
        (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7),
    ]
    subt_raw = (
        struct.pack("<IIII", 4, 0, len(zb_verts), len(zb_polys))
        + b"".join(struct.pack("<3f", *v) for v in zb_verts)
        + b"".join(struct.pack("<4I", *q) for q in zb_polys)
    )
    subt_planes_rle = _packbits_rle_encode(_transpose_word_planes(subt_raw))

    ztl_hdr = bytearray(128)
    banner_ztl = b"ZBrush File 4.8 - Pixologic ZTool\x00"
    ztl_hdr[0 : len(banner_ztl)] = banner_ztl
    struct.pack_into("<III", ztl_hdr, 116, 0x5A425246, 128, 1)  # magic, dirOff=128, chunkCount=1
    data_off_ztl = 128 + 20
    ztl_dir = b"SubT" + struct.pack("<4I", 3, data_off_ztl, len(subt_raw), len(subt_planes_rle))
    p_ztl = d3_dir / "sculpt_bust.ztl"
    p_ztl.write_bytes(bytes(ztl_hdr) + ztl_dir + subt_planes_rle)
    created.append(p_ztl)

    # .zpr Project with 4x4 SubTool placement matrix and nested Proj -> Tool -> SubT TLV chunks
    xf_mat = [1.2, 0, 0, 0.0,  0, 1.2, 0, 0.5,  0, 0, 1.2, 0.0,  0, 0, 0, 1.0]
    subt_xf_raw = (
        struct.pack("<II", 5, 1)
        + struct.pack("<16f", *xf_mat)
        + struct.pack("<II", len(zb_verts), len(zb_polys))
        + b"".join(struct.pack("<3f", *v) for v in zb_verts)
        + b"".join(struct.pack("<4I", *q) for q in zb_polys)
    )
    subt_rle1 = _packbits_rle_encode(subt_xf_raw)
    subt_tlv = b"SubT" + struct.pack("<3I", 1, len(subt_xf_raw), len(subt_rle1)) + subt_rle1
    tool_tlv = b"Tool" + struct.pack("<3I", 4, len(subt_tlv), len(subt_tlv)) + subt_tlv
    proj_tlv = b"Proj" + struct.pack("<3I", 4, len(tool_tlv), len(tool_tlv)) + tool_tlv

    zpr_hdr = bytearray(128)
    banner_zpr = b"ZBrush File 2022 - Pixologic ZProject\x00"
    zpr_hdr[0 : len(banner_zpr)] = banner_zpr
    struct.pack_into("<III", zpr_hdr, 116, 0x5A425246, 0, 0)
    p_zpr = d3_dir / "creature_project.zpr"
    p_zpr.write_bytes(bytes(zpr_hdr) + proj_tlv)
    created.append(p_zpr)

    return created



# ---------------------------------------------------------------------------
# Consolidated domain sample builders
# ---------------------------------------------------------------------------
import base64 as _b64_legacy
import gzip as _gzip_legacy
import pickle as _pickle_legacy

class _DomainSampleRouter:
    def __init__(self, td_root: Path, file_to_sub: dict[str, str]):
        self.td_root = td_root
        self.file_to_sub = file_to_sub

    def __truediv__(self, filename: str) -> Path:
        sub = self.file_to_sub[filename]
        d = self.td_root / sub
        d.mkdir(parents=True, exist_ok=True)
        return d / filename

def _norm3(x: float, y: float, z: float):
    ln = math.sqrt(x * x + y * y + z * z)
    return (0.0, 0.0, 1.0) if ln < 1e-9 else (x / ln, y / ln, z / ln)

def _tri_normal(a, b, c):
    e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    return _norm3(
        e1[1] * e2[2] - e1[2] * e2[1],
        e1[2] * e2[0] - e1[0] * e2[2],
        e1[0] * e2[1] - e1[1] * e2[0],
    )

def pad_to(buf: bytearray, align: int) -> None:
    rem = len(buf) % align
    if rem:
        buf.extend(b"\x00" * (align - rem))


def build_j3d_bmd(magic8: bytes, scale: float) -> bytes:
    # VTX1 chunk: 0x40 header + 2*16B attrFmt + 8*12B float3 cube positions
    vtx1 = bytearray(0x40)
    vtx1[0:4] = b"VTX1"
    fmt_off = 0x40
    vtx1 += struct.pack(">IIIB3x", 9, 1, 4, 0)       # GX_VA_POS, XYZ, f32, shift=0
    vtx1 += struct.pack(">IIIB3x", 0xFF, 0, 0, 0)    # GX_VA_NULL
    pos_off = len(vtx1)
    verts = [
        (-1.0 * scale, -1.0 * scale, -1.0 * scale),
        ( 1.0 * scale, -1.0 * scale, -1.0 * scale),
        ( 1.0 * scale,  1.0 * scale, -1.0 * scale),
        (-1.0 * scale,  1.0 * scale, -1.0 * scale),
        (-1.0 * scale, -1.0 * scale,  1.0 * scale),
        ( 1.0 * scale, -1.0 * scale,  1.0 * scale),
        ( 1.0 * scale,  1.0 * scale,  1.0 * scale),
        (-1.0 * scale,  1.0 * scale,  1.0 * scale),
    ]
    for x, y, z in verts:
        vtx1 += struct.pack(">fff", x, y, z)
    pad_to(vtx1, 32)
    struct.pack_into(">III", vtx1, 4, len(vtx1), fmt_off, pos_off)

    # SHP1 chunk: 0x2C header + 1 ShapeInit (0x28) + VtxDescList + PacketLoc + GX_QUADS display list
    shp1 = bytearray(0x2C)
    shp1[0:4] = b"SHP1"
    shape_init_off = len(shp1)
    sh_init = bytearray(0x28)
    struct.pack_into(">BBHHHH", sh_init, 0, 0, 0, 1, 0, 0, 0)  # mtxGroupCount=1, vtxDescByteOff=0, firstPktIdx=0
    shp1 += sh_init

    vtx_desc_off = len(shp1)
    shp1 += struct.pack(">II", 9, 3)      # GX_VA_POS, GX_INDEX16
    shp1 += struct.pack(">II", 0xFF, 0)   # GX_VA_NULL

    pkt_loc_off = len(shp1)
    shp1 += b"\x00" * 8  # placeholder (dlSize, dlRel)
    pad_to(shp1, 32)
    dl_base_off = len(shp1)

    # GX_QUADS (0x80) with 24 indices (6 faces * 4 corners)
    quads = [
        0, 3, 2, 1,
        4, 5, 6, 7,
        0, 1, 5, 4,
        2, 3, 7, 6,
        0, 4, 7, 3,
        1, 2, 6, 5,
    ]
    dl = bytearray()
    dl += struct.pack(">BH", 0x80, len(quads))
    for q in quads:
        dl += struct.pack(">H", q)
    pad_to(dl, 32)
    shp1 += dl
    pad_to(shp1, 32)

    struct.pack_into(">II", shp1, pkt_loc_off, len(dl), 0)
    struct.pack_into(">I", shp1, 0x04, len(shp1))
    struct.pack_into(">H", shp1, 0x08, 1)  # shapeCount = 1
    struct.pack_into(">I", shp1, 0x0C, shape_init_off)
    struct.pack_into(">I", shp1, 0x14, vtx_desc_off)
    struct.pack_into(">I", shp1, 0x20, dl_base_off)
    struct.pack_into(">I", shp1, 0x28, pkt_loc_off)

    total_len = 0x20 + len(vtx1) + len(shp1)
    hdr = bytearray(0x20)
    hdr[0:8] = magic8
    struct.pack_into(">II", hdr, 8, total_len, 2)
    return bytes(hdr + vtx1 + shp1)

def build_nw4r_mdl0(height_z: float) -> bytes:
    # MDL0 v9 (11 section slots at 0x10..0x3C; slot 2 = Vertices, slot 8 = Objects)
    out = bytearray(0x40)
    out[0:4] = b"MDL0"
    struct.pack_into(">I", out, 8, 9)  # version 9

    # Vertices ResourceGroup (1 entry)
    vtx_grp_off = len(out)
    out += struct.pack(">II", 0x28, 1)
    out += b"\x00" * 16  # root node
    vtx_data_abs = vtx_grp_off + 0x28
    out += struct.pack(">IIii", 0, 0, 0, vtx_data_abs - vtx_grp_off)

    # MDL0VertexData header (0x20 bytes) + 5 pyramid vertices (f32 XYZ)
    pts = [
        (-1.5, -1.5, 0.0),
        ( 1.5, -1.5, 0.0),
        ( 1.5,  1.5, 0.0),
        (-1.5,  1.5, 0.0),
        ( 0.0,  0.0, height_z),
    ]
    vtx_raw = b"".join(struct.pack(">fff", *p) for p in pts)
    vtx_blk = bytearray(0x20)
    struct.pack_into(">IiiiIIIbbH", vtx_blk, 0,
                     0x20 + len(vtx_raw), vtx_grp_off - vtx_data_abs, 0x20, 0,
                     0, 1, 4, 0, 12, len(pts))
    out += vtx_blk + vtx_raw
    pad_to(out, 32)

    # Objects ResourceGroup (1 entry)
    obj_grp_off = len(out)
    out += struct.pack(">II", 0x28, 1)
    out += b"\x00" * 16
    obj_data_abs = obj_grp_off + 0x28
    out += struct.pack(">IIii", 0, 0, 0, obj_data_abs - obj_grp_off)

    # MDL0Object header (0x60 bytes padded) + GX_TRIANGLES display list (6 tris = 18 u16 indices)
    tris = [
        0, 2, 1,  0, 3, 2,
        0, 1, 4,  1, 2, 4,
        2, 3, 4,  3, 0, 4,
    ]
    dl = bytearray(struct.pack(">BH", 0x90, len(tris)))
    for idx in tris:
        dl += struct.pack(">H", idx)
    pad_to(dl, 32)

    obj_hdr = bytearray(0x60)
    cp_lo = (3 << 9)  # Position = GX_INDEX16 (mode 3 at bits 9..10)
    struct.pack_into(">I", obj_hdr, 0x00, len(obj_hdr) + len(dl))
    struct.pack_into(">II", obj_hdr, 0x18, cp_lo, 0)
    struct.pack_into(">IIi", obj_hdr, 0x24, len(dl), len(dl), len(obj_hdr) - 0x24)
    struct.pack_into(">h", obj_hdr, 0x40, 0)  # vertexId = 0
    out += obj_hdr + dl
    pad_to(out, 32)

    struct.pack_into(">I", out, 0x04, len(out))
    struct.pack_into(">i", out, 0x10 + 2 * 4, vtx_grp_off)
    struct.pack_into(">i", out, 0x10 + 8 * 4, obj_grp_off)
    return bytes(out)

def build_nitro_dict_1(name16: bytes, val_u32: int) -> bytes:
    # 1-entry NITRO G3D Dict (36 bytes):
    # [0] rev=0, [1] count=1, [2..4] dictSize=36, [4..6] pad=8, [6..8] refOfs=16
    # [8..16] 2 PTree nodes (4B each), [16..20] valStride=4, namesOfs=24, [20..24] val_u32, [24..40] 16B name -> total 40B
    d = bytearray(40)
    struct.pack_into("<BBHHH", d, 0, 0, 1, 40, 8, 16)
    struct.pack_into("<HH", d, 16, 4, 24)
    struct.pack_into("<I", d, 20, val_u32)
    d[24:24 + min(16, len(name16))] = name16[:16]
    return bytes(d)

def build_ds_nsbmd(apex_z_fx16: int) -> bytes:
    # DS GPU display list: BEGIN_VTXS(TRIANGLES=0) + 4 triangles (12 VTX_16 cmds) + END_VTXS
    tris_fx = [
        (-4096, -4096, 0), (4096, -4096, 0), (0, 4096, 0),
        (-4096, -4096, 0), (0, 0, apex_z_fx16), (4096, -4096, 0),
        (4096, -4096, 0), (0, 0, apex_z_fx16), (0, 4096, 0),
        (0, 4096, 0), (0, 0, apex_z_fx16), (-4096, -4096, 0),
    ]
    dl = bytearray()
    # Pack 1: BEGIN_VTXS(0x40), VTX_16(0x23), VTX_16(0x23), VTX_16(0x23)
    dl += bytes([0x40, 0x23, 0x23, 0x23])
    dl += struct.pack("<I", 0)  # prim = TRIANGLES
    for x, y, z in tris_fx[0:3]:
        dl += struct.pack("<hh", x, y) + struct.pack("<hh", z, 0)
    # Next 3 triangles: 3 packs of [0x23, 0x23, 0x23, 0x00]
    for t in range(1, 4):
        dl += bytes([0x23, 0x23, 0x23, 0x00])
        for x, y, z in tris_fx[t * 3:(t + 1) * 3]:
            dl += struct.pack("<hh", x, y) + struct.pack("<hh", z, 0)
    # Final pack: END_VTXS(0x41), NOP, NOP, NOP
    dl += bytes([0x41, 0x00, 0x00, 0x00])

    # Polygon block: 16B header + dl
    poly_hdr = struct.pack("<HHIII", 0, 16, 0, 16, len(dl))
    shp_dict = build_nitro_dict_1(b"poly0", 40)
    shp_section = shp_dict + poly_hdr + dl

    # NITRO Model header (0x40 bytes) + shp_section
    mdl_hdr = bytearray(0x40)
    mdl_total = 0x40 + len(shp_section)
    struct.pack_into("<IIII", mdl_hdr, 0x00, mdl_total, 0x40, 0x40, 0x40)
    struct.pack_into("<i", mdl_hdr, 0x20, 0)  # posScaleShift = 0
    mdl_blob = bytes(mdl_hdr) + shp_section

    mdl0_dict = build_nitro_dict_1(b"trophy", 8 + 40)
    mdl0_blk_sz = 8 + len(mdl0_dict) + len(mdl_blob)
    mdl0_blk = b"MDL0" + struct.pack("<I", mdl0_blk_sz) + mdl0_dict + mdl_blob

    total_sz = 20 + len(mdl0_blk)
    bmd0_hdr = b"BMD0" + struct.pack("<HHIHHI", 0xFEFF, 2, total_sz, 16, 1, 20)
    return bmd0_hdr + mdl0_blk

def build_spring_s3o(turret_y: float) -> bytes:
    # 52B header + root piece ("base", quads) + child piece ("turret", triangles)
    out = bytearray(52)
    out[0:12] = b"Spring unit\x00"
    struct.pack_into("<Ifffff", out, 12, 0, 24.0, 18.0, 0.0, 8.0, 0.0)
    root_off = len(out)
    struct.pack_into("<I", out, 0x24, root_off)

    # Reserve 2 pieces (52B each) + child table (4B) + names
    out += bytearray(52 * 2)
    turret_off = root_off + 52
    child_tbl_off = len(out)
    out += struct.pack("<I", turret_off)
    name_base_off = len(out)
    out += b"chassis\x00"
    name_tur_off = len(out)
    out += b"turret\x00"
    pad_to(out, 4)

    # Root vertices: 4 quad corners on ground
    r_vtx_off = len(out)
    r_pts = [(-10.0, 0.0, -14.0), (10.0, 0.0, -14.0), (10.0, 0.0, 14.0), (-10.0, 0.0, 14.0)]
    for x, y, z in r_pts:
        out += struct.pack("<ffffffff", x, y, z, 0.0, 1.0, 0.0, 0.0, 0.0)
    r_idx_off = len(out)
    out += struct.pack("<IIII", 0, 1, 2, 3)

    # Child turret vertices: 4 tetrahedron points
    t_vtx_off = len(out)
    t_pts = [(-5.0, 0.0, -5.0), (5.0, 0.0, -5.0), (0.0, 0.0, 9.0), (0.0, 6.0, 0.0)]
    for x, y, z in t_pts:
        out += struct.pack("<ffffffff", x, y, z, 0.0, 1.0, 0.0, 0.0, 0.0)
    t_idx_off = len(out)
    t_tris = [0, 2, 1, 0, 1, 3, 1, 2, 3, 2, 0, 3]
    for idx in t_tris:
        out += struct.pack("<I", idx)

    # Fill root piece (primType=2 quads)
    struct.pack_into("<IIIIIIIIIIfff", out, root_off,
                     name_base_off, 1, child_tbl_off, len(r_pts), r_vtx_off, 0, 2, 4, r_idx_off, 0,
                     0.0, 0.0, 0.0)
    # Fill child turret piece (primType=0 triangles, offset y=turret_y)
    struct.pack_into("<IIIIIIIIIIfff", out, turret_off,
                     name_tur_off, 0, 0, len(t_pts), t_vtx_off, 0, 0, len(t_tris), t_idx_off, 0,
                     0.0, turret_y, 2.0)
    return bytes(out)

def build_blizzard_m3(apex_z: float) -> bytes:
    # Header (32B) + MODL (0x70B) + U8__ (5 * 32B verts) + U16_ (18 u16 indices) + REGN (40B) + RefTable (4 * 16B)
    out = bytearray(32)
    out[0:4] = b"43DM"

    modl_off = len(out)
    modl = bytearray(0x70)
    struct.pack_into("<I", modl, 0x64, 0x00020000)  # vertexFlags with UV0 -> 32B stride
    out += modl

    vtx_off = len(out)
    pts = [(-1.0, -1.0, 0.0), (1.0, -1.0, 0.0), (1.0, 1.0, 0.0), (-1.0, 1.0, 0.0), (0.0, 0.0, apex_z)]
    for x, y, z in pts:
        v = bytearray(32)
        struct.pack_into("<fff", v, 0, x, y, z)
        out += v
    vtx_byte_count = len(out) - vtx_off

    idx_off = len(out)
    tris = [0, 2, 1,  0, 3, 2,  0, 1, 4,  1, 2, 4,  2, 3, 4,  3, 0, 4]
    for i in tris:
        out += struct.pack("<H", i)
    pad_to(out, 16)

    regn_off = len(out)
    regn = bytearray(40)
    struct.pack_into("<IIII", regn, 8, 0, len(pts), 0, len(tris))
    out += regn

    ref_off = len(out)
    refs = [
        (b"LDOM", modl_off, 1, 29),
        (b"__8U", vtx_off, vtx_byte_count, 0),
        (b"_61U", idx_off, len(tris), 0),
        (b"NGER", regn_off, 1, 4),
    ]
    for tag, roff, rcnt, rver in refs:
        out += tag + struct.pack("<III", roff, rcnt, rver)

    struct.pack_into("<IIIII", out, 4, ref_off, len(refs), 1, 0, 0)
    return bytes(out)

def make_png_64x64_rgba(solid_coords) -> bytes:
    raw_rows = bytearray()
    mask = set(solid_coords)
    for py in range(64):
        raw_rows.append(0)  # Filter None
        for px in range(64):
            v = py * 64 + px
            vx, vy, vz = (v & 15), ((v >> 4) & 15), ((v >> 8) & 15)
            if (vx, vy, vz) in mask:
                raw_rows += bytes([210, 180, 90, 255])
            else:
                raw_rows += bytes([0, 0, 0, 0])

    def png_chunk(tag: bytes, body: bytes) -> bytes:
        crc = zlib.crc32(tag + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", 64, 64, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw_rows), 6)
    return b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", idat) + png_chunk(b"IEND", b"")

def build_goxel_gox(voxel_coords) -> bytes:
    def gox_chunk(tag: bytes, body: bytes) -> bytes:
        return tag + struct.pack("<I", len(body)) + body + struct.pack("<I", 0)

    png_bytes = make_png_64x64_rgba(voxel_coords)
    layr_body = struct.pack("<IIiiii", 1, 0, 0, 0, 0, 0) + struct.pack("<I", 0)
    return b"GOX " + struct.pack("<i", 2) + gox_chunk(b"BL16", png_bytes) + gox_chunk(b"LAYR", layr_body)

def build_binvox(dim: int, solid_set) -> bytes:
    hdr = f"#binvox 1\ndim {dim}\ntranslate 0.0 0.0 0.0\nscale 1.0\ndata\n".encode("ascii")
    vals = []
    for x in range(dim):
        for z in range(dim):
            for y in range(dim):
                vals.append(1 if (x, y, z) in solid_set else 0)
    rle = bytearray()
    i = 0
    while i < len(vals):
        v = vals[i]
        cnt = 1
        while i + cnt < len(vals) and vals[i + cnt] == v and cnt < 255:
            cnt += 1
        rle.append(v)
        rle.append(cnt)
        i += cnt
    return hdr + bytes(rle)

def gen_domain_valve_vvd(out_dir: Path):
    # Build an armored visor helmet dome + shoulder pauldrons as 2 LOD fixup spans:
    # Span A (lod=1, shared by LOD0 and LOD1): hexagonal visor dome (6*2 = 12 tris = 36 verts)
    # Span B (lod=0, high-detail rim & chest plate, LOD0 only): 8 tris = 24 verts
    tris_lod1 = []
    segs = 6
    apex = (0.0, 0.0, 26.0)
    ring1 = [(10.0 * math.cos(2 * math.pi * i / segs), 10.0 * math.sin(2 * math.pi * i / segs), 18.0) for i in range(segs)]
    ring2 = [(13.0 * math.cos(2 * math.pi * i / segs), 13.0 * math.sin(2 * math.pi * i / segs), 6.0) for i in range(segs)]
    for i in range(segs):
        j = (i + 1) % segs
        tris_lod1.append((apex, ring1[i], ring1[j]))
        tris_lod1.append((ring1[i], ring2[i], ring2[j]))
        tris_lod1.append((ring1[i], ring2[j], ring1[j]))

    tris_lod0_extra = [
        ((-18.0, -6.0, 4.0), (-10.0, -6.0, 8.0), (-10.0, 6.0, 8.0)),
        ((-18.0, -6.0, 4.0), (-10.0, 6.0, 8.0), (-18.0, 6.0, 4.0)),
        ((10.0, -6.0, 8.0), (18.0, -6.0, 4.0), (18.0, 6.0, 4.0)),
        ((10.0, -6.0, 8.0), (18.0, 6.0, 4.0), (10.0, 6.0, 8.0)),
    ]

    def pack_vvd_vert(pos, nrm, uv, bone0=0, w0=1.0):
        # mstudiovertex_t (48 bytes): float weight[3], uint8 bone[3], uint8 numbones, pos[3], nrm[3], uv[2]
        return struct.pack(
            "<3f4B3f3f2f",
            w0, 0.0, 0.0,
            bone0, 0, 0, 1,
            pos[0], pos[1], pos[2],
            nrm[0], nrm[1], nrm[2],
            uv[0], uv[1],
        )

    # Store pool in reverse order [extra_lod0 (12 verts), shared_lod1 (54 verts)] so fixups genuinely remap!
    pool_verts = []
    for tri in tris_lod0_extra:
        n = _tri_normal(*tri)
        for k, p in enumerate(tri):
            pool_verts.append(pack_vvd_vert(p, n, (0.5 * (k & 1), 0.5 * (k >> 1)), bone0=2, w0=1.0))
    extra_count = len(pool_verts)  # 12

    for tri in tris_lod1:
        n = _tri_normal(*tri)
        for k, p in enumerate(tri):
            pool_verts.append(pack_vvd_vert(p, n, ((p[0] + 15.0) / 30.0, p[2] / 26.0), bone0=1, w0=1.0))
    shared_count = len(pool_verts) - extra_count  # 54
    total_verts = len(pool_verts)  # 66

    # Fixup table (2 entries * 12 bytes = 24 bytes, padded to 16-byte boundary -> 64 + 32 = 96)
    # Fixup 0: lod=1 (included in LOD0 and LOD1), sourceVertexID=extra_count (12), numVertexes=shared_count (54)
    # Fixup 1: lod=0 (included in LOD0 only),     sourceVertexID=0,                numVertexes=extra_count (12)
    fixups = struct.pack("<3i3i", 1, extra_count, shared_count, 0, 0, extra_count)
    fixup_start = 64
    vert_start = 96
    pad_after_fixups = b"\x00" * (vert_start - (fixup_start + len(fixups)))
    tang_start = vert_start + total_verts * 48
    tangents = struct.pack("<4f", 1.0, 0.0, 0.0, 1.0) * total_verts

    lod_counts = [shared_count + extra_count, shared_count, 0, 0, 0, 0, 0, 0]
    hdr = struct.pack(
        "<4s3i8i4i",
        b"IDSV",
        4,              # version
        0x4A91C207,     # checksum
        2,              # numLODs
        *lod_counts,
        2,              # numFixups
        fixup_start,
        vert_start,
        tang_start,
    )
    assert len(hdr) == 64
    (out_dir / "combine_soldier.vvd").write_bytes(hdr + fixups + pad_after_fixups + b"".join(pool_verts) + tangents)

def gen_domain_raven_glm(out_dir: Path):
    def build_glm_surface(surf_idx: int, verts, tris):
        # mdxmSurface_t (40 bytes) + tris (12*nt) + verts (32*nv + 8*nv) + bonerefs (4*1)
        ofs_tris = 40
        tri_bytes = b"".join(struct.pack("<3i", a, b, c) for (a, b, c) in tris)
        ofs_verts = ofs_tris + len(tri_bytes)
        v_bytes = bytearray()
        uv_bytes = bytearray()
        for (px, py, pz) in verts:
            nx, ny, nz = _norm3(px, py, 0.25)
            # 1 bone weight = 255 on local bone ref 0 -> uiNmWeightsAndBoneIndexes = 0
            v_bytes += struct.pack("<3f3fI4B", nx, ny, nz, px, py, pz, 0, 255, 0, 0, 0)
            uv_bytes += struct.pack("<2f", (px + 4.0) / 8.0, (pz + 15.0) / 30.0)
        ofs_bones = ofs_verts + len(v_bytes) + len(uv_bytes)
        bone_bytes = struct.pack("<i", 0)
        ofs_end = ofs_bones + len(bone_bytes)
        shdr = struct.pack(
            "<10i",
            3,              # SURFACE_MDXM
            surf_idx,
            -40,            # ofsHeader
            len(verts),
            ofs_verts,
            len(tris),
            ofs_tris,
            1,              # numBoneReferences
            ofs_bones,
            ofs_end,
        )
        return shdr + tri_bytes + bytes(v_bytes) + bytes(uv_bytes) + bone_bytes

    # Surface 0: octagonal lightsaber grip cylinder (z = -12 .. +10, radius = 2.2)
    segs = 8
    grip_verts = []
    for z in (-12.0, 10.0):
        for i in range(segs):
            ang = 2.0 * math.pi * i / segs
            grip_verts.append((2.2 * math.cos(ang), 2.2 * math.sin(ang), z))
    grip_verts.append((0.0, 0.0, -13.5))  # pommel cap center (idx 16)
    grip_tris = []
    for i in range(segs):
        j = (i + 1) % segs
        grip_tris.append((i, segs + i, segs + j))
        grip_tris.append((i, segs + j, j))
        grip_tris.append((16, j, i))

    # Surface 1: flared emitter shroud crown (z = +10 .. +16.5, radius 2.2 -> 3.6)
    emit_verts = []
    for r, z in ((2.2, 10.0), (3.6, 16.5)):
        for i in range(segs):
            ang = 2.0 * math.pi * i / segs
            emit_verts.append((r * math.cos(ang), r * math.sin(ang), z))
    emit_verts.append((0.0, 0.0, 15.0))  # blade socket center (idx 16)
    emit_tris = []
    for i in range(segs):
        j = (i + 1) % segs
        emit_tris.append((i, segs + i, segs + j))
        emit_tris.append((i, segs + j, j))
        emit_tris.append((segs + i, 16, segs + j))

    s0 = build_glm_surface(0, grip_verts, grip_tris)
    s1 = build_glm_surface(1, emit_verts, emit_tris)

    # Surface hierarchy table (2 minimal entries after header @ 164)
    ofs_hier = 164
    hier_blob = b"\x00" * 160
    ofs_lods = ofs_hier + len(hier_blob)
    # mdxmLOD_t (4 bytes) + 2 surface offsets (8 bytes, relative to mdxmLODSurfOffset_t = ofs_lods + 4)
    surf0_rel = 8
    surf1_rel = 8 + len(s0)
    lod0_blob = struct.pack("<i2i", 4 + 8 + len(s0) + len(s1), surf0_rel, surf1_rel) + s0 + s1
    ofs_end = ofs_lods + len(lod0_blob)

    name_b = b"models/weapons2/saber_jedi/saber_w.glm".ljust(64, b"\x00")
    anim_b = b"*default".ljust(64, b"\x00")
    hdr = struct.pack(
        "<4si64s64s7i",
        b"2LGM",
        6,          # MDXM_VERSION
        name_b,
        anim_b,
        0,          # animIndex
        2,          # numBones
        1,          # numLODs
        ofs_lods,
        2,          # numSurfaces
        ofs_hier,
        ofs_end,
    )
    assert len(hdr) == 164
    (out_dir / "jedi_saber_hilt.glm").write_bytes(hdr + hier_blob + lod0_blob)

def gen_domain_unreal_3d(out_dir: Path):
    def pack_ue1_11_11_10(x: float, y: float, z: float) -> int:
        ix = max(-1024, min(1023, int(round(x * 8.0)))) & 0x7FF
        iy = max(-1024, min(1023, int(round(y * 8.0)))) & 0x7FF
        iz = max(-512, min(511, int(round(z * 4.0)))) & 0x3FF
        return ix | (iy << 11) | (iz << 22)

    # Crystal dorsal spire + twin swept wings (12 triangles = 36 vertices per frame, 2 frames)
    base_tris = [
        # Dorsal spire pyramid (4 tris)
        ((0.0, 0.0, 32.0), (-8.0, -8.0, 4.0), (8.0, -8.0, 4.0)),
        ((0.0, 0.0, 32.0), (8.0, -8.0, 4.0), (8.0, 8.0, 4.0)),
        ((0.0, 0.0, 32.0), (8.0, 8.0, 4.0), (-8.0, 8.0, 4.0)),
        ((0.0, 0.0, 32.0), (-8.0, 8.0, 4.0), (-8.0, -8.0, 4.0)),
        # Lower keel pyramid (4 tris)
        ((0.0, 0.0, -18.0), (8.0, -8.0, 4.0), (-8.0, -8.0, 4.0)),
        ((0.0, 0.0, -18.0), (8.0, 8.0, 4.0), (8.0, -8.0, 4.0)),
        ((0.0, 0.0, -18.0), (-8.0, 8.0, 4.0), (8.0, 8.0, 4.0)),
        ((0.0, 0.0, -18.0), (-8.0, -8.0, 4.0), (-8.0, 8.0, 4.0)),
        # Port & starboard swept fins (4 tris)
        ((-8.0, -4.0, 8.0), (-28.0, 6.0, 14.0), (-8.0, 6.0, 6.0)),
        ((-8.0, -4.0, 8.0), (-8.0, 6.0, 6.0), (-28.0, 6.0, 10.0)),
        ((8.0, -4.0, 8.0), (8.0, 6.0, 6.0), (28.0, 6.0, 14.0)),
        ((8.0, -4.0, 8.0), (28.0, 6.0, 10.0), (8.0, 6.0, 6.0)),
    ]

    num_verts = len(base_tris) * 3  # 36
    frame_size = num_verts * 4      # 144 bytes
    frames = bytearray()
    for f_idx, wing_lift in enumerate((0.0, 6.0)):
        for tri in base_tris:
            for (x, y, z) in tri:
                zf = z + (wing_lift if abs(x) > 20.0 else 0.0)
                frames += struct.pack("<I", pack_ue1_11_11_10(x, y, zf))

    hdr = struct.pack("<2H", 2, frame_size)
    (out_dir / "unreal_skith_a.3d").write_bytes(hdr + bytes(frames))

def gen_domain_motion_htr(out_dir: Path):
    text = """# Motion Analysis EVa HTR Skeletal Motion Capture
[Header]
FileType "htr"
DataType "HTRS"
FileVersion 1
NumSegments 9
NumFrames 3
DataFrameRate 60
EulerRotationOrder ZYX
CalibrationUnits mm
RotationUnits Degrees
GlobalAxisofGravity Y
BoneLengthAxis Y

[SegmentNames&Hierarchy]
Pelvis GLOBAL
Spine Pelvis
Head Spine
LeftArm Spine
LeftForearm LeftArm
RightArm Spine
RightForearm RightArm
LeftThigh Pelvis
RightThigh Pelvis

[BasePosition]
Pelvis       0.0   950.0   0.0    0.0   0.0    0.0   140.0
Spine        0.0   140.0   0.0    0.0   0.0    0.0   380.0
Head         0.0   380.0   0.0    0.0   0.0    0.0   210.0
LeftArm   -190.0   340.0   0.0    0.0   0.0   65.0   280.0
LeftForearm  0.0   280.0   0.0   15.0   0.0   20.0   250.0
RightArm   190.0   340.0   0.0    0.0   0.0  -65.0   280.0
RightForearm 0.0   280.0   0.0   15.0   0.0  -20.0   250.0
LeftThigh -105.0     0.0   0.0  -20.0   0.0  175.0   430.0
RightThigh 105.0     0.0   0.0   25.0   0.0 -175.0   430.0

[Pelvis]
1   0.0   965.0   10.0   5.0   8.0   0.0   1.000
2   4.0   990.0   35.0   8.0  12.0   2.0   1.000
3   8.0  1010.0   60.0  10.0  16.0   3.0   1.000

[LeftArm]
1 -190.0  340.0    0.0  12.0   0.0  55.0   1.000
2 -190.0  340.0    0.0  24.0   0.0  45.0   1.000
3 -190.0  340.0    0.0  36.0   0.0  35.0   1.000

[RightArm]
1  190.0  340.0    0.0 -12.0   0.0 -55.0   1.000
2  190.0  340.0    0.0 -24.0   0.0 -45.0   1.000
3  190.0  340.0    0.0 -36.0   0.0 -35.0   1.000

[EndOfFile]
"""
    (out_dir / "mocap_acrobat.htr").write_bytes(text.encode("utf-8"))

def gen_domain_acclaim_asf(out_dir: Path):
    text = """# Acclaim ASF v1.10 Bipedal Runner Skeleton
:version 1.10
:name cmu_runner
:units
  mass 1.0
  length 0.45
  angle deg
:root
  order TX TY TZ RX RY RZ
  axis XYZ
  position 0.0 18.0 0.0
  orientation 0.0 0.0 0.0
:bonedata
  begin
    id 1
    name lhipjoint
    direction -0.65 -0.75 0.0
    length 3.2
    axis 0.0 0.0 0.0 XYZ
  end
  begin
    id 2
    name lfemur
    direction -0.05 -0.98 0.18
    length 7.8
    axis 0.0 0.0 15.0 XYZ
    dof rx ry rz
  end
  begin
    id 3
    name ltibia
    direction 0.0 -0.97 -0.24
    length 7.4
    axis 0.0 0.0 10.0 XYZ
    dof rx
  end
  begin
    id 4
    name rhipjoint
    direction 0.65 -0.75 0.0
    length 3.2
    axis 0.0 0.0 0.0 XYZ
  end
  begin
    id 5
    name rfemur
    direction 0.05 -0.96 -0.26
    length 7.8
    axis 0.0 0.0 -15.0 XYZ
    dof rx ry rz
  end
  begin
    id 6
    name rtibia
    direction 0.0 -0.98 0.19
    length 7.4
    axis 0.0 0.0 -10.0 XYZ
    dof rx
  end
  begin
    id 7
    name lowerback
    direction 0.0 0.99 0.12
    length 4.5
    axis 0.0 0.0 0.0 XYZ
    dof rx ry rz
  end
  begin
    id 8
    name thorax
    direction 0.0 0.99 0.08
    length 5.2
    axis 0.0 0.0 0.0 XYZ
    dof rx ry rz
  end
  begin
    id 9
    name head
    direction 0.0 1.0 0.0
    length 3.6
    axis 0.0 0.0 0.0 XYZ
    dof rx ry rz
  end
  begin
    id 10
    name lhumerus
    direction -0.82 -0.55 0.15
    length 6.1
    axis 0.0 0.0 40.0 XYZ
    dof rx ry rz
  end
  begin
    id 11
    name rhumerus
    direction 0.82 -0.55 -0.15
    length 6.1
    axis 0.0 0.0 -40.0 XYZ
    dof rx ry rz
  end
:hierarchy
  begin
    root lhipjoint rhipjoint lowerback
    lhipjoint lfemur
    lfemur ltibia
    rhipjoint rfemur
    rfemur rtibia
    lowerback thorax
    thorax head lhumerus rhumerus
  end
"""
    (out_dir / "cmu_runner.asf").write_bytes(text.encode("utf-8"))

def gen_domain_tetgen_smesh(out_dir: Path):
    # Flanged hexagonal turbine hub (1-based node IDs, mix of quad & triangle PLC facets)
    lines = ["# TetGen PLC 3D surface mesh: hexagonal turbine hub", "20 3 0 1"]
    segs = 6
    nid = 1
    for z, r in ((0.0, 14.0), (5.0, 14.0), (18.0, 7.5)):
        for i in range(segs):
            ang = 2.0 * math.pi * i / segs
            lines.append(f"  {nid}  {r * math.cos(ang):.4f}  {r * math.sin(ang):.4f}  {z:.4f}  1")
            nid += 1
    lines.append(f"  19  0.0000  0.0000   0.0000  2")  # bottom center
    lines.append(f"  20  0.0000  0.0000  21.5000  3")  # nose cone apex

    facets = []
    for i in range(segs):
        j = (i + 1) % segs
        # Bottom cap triangles (19, j+1, i+1)
        facets.append(f"  3  19  {j + 1}  {i + 1}  2")
        # Outer flange quad (ring0 -> ring1)
        facets.append(f"  4  {i + 1}  {j + 1}  {6 + j + 1}  {6 + i + 1}  1")
        # Tapered hub quad (ring1 -> ring2)
        facets.append(f"  4  {6 + i + 1}  {6 + j + 1}  {12 + j + 1}  {12 + i + 1}  1")
        # Top cone triangles (ring2 -> 20)
        facets.append(f"  3  {12 + i + 1}  {12 + j + 1}  20  3")

    lines.append(f"{len(facets)} 1")
    lines.extend(facets)
    lines.append("0")  # 0 holes
    lines.append("0")  # 0 regions
    (out_dir / "turbine_plc.smesh").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))

def gen_domain_povray_pov(out_dir: Path):
    text = """// POV-Ray 3.7 Scene: Faceted Crystal Chalice (#declare + mesh + mesh2 + cylinder)
camera { location <0, 18, -38> look_at <0, 10, 0> }
light_source { <25, 45, -30> color rgb <1.0, 0.98, 0.92> }

#declare ChaliceFoot = mesh2 {
  vertex_vectors {
    9,
    < 0.0, 0.0,  0.0>,
    < 8.0, 0.0,  0.0>, < 5.65, 0.0,  5.65>, < 0.0, 0.0,  8.0>, <-5.65, 0.0,  5.65>,
    <-8.0, 0.0,  0.0>, <-5.65, 0.0, -5.65>, < 0.0, 0.0, -8.0>, < 5.65, 0.0, -5.65>
  }
  face_indices {
    8,
    <0, 1, 2>, 0, <0, 2, 3>, 0, <0, 3, 4>, 0, <0, 4, 5>, 0,
    <0, 5, 6>, 0, <0, 6, 7>, 0, <0, 7, 8>, 0, <0, 8, 1>, 0
  }
}

object { ChaliceFoot scale <1.0, 1.0, 1.0> }
cylinder { <0, 0, 0>, <0, 9.5, 0>, 1.6 texture { pigment { rgb <0.8, 0.85, 0.95> } } }

mesh {
  triangle { <0, 9.0, 0>, < 9.0, 21.0,  0.0>, < 0.0, 21.0,  9.0> }
  triangle { <0, 9.0, 0>, < 0.0, 21.0,  9.0>, <-9.0, 21.0,  0.0> }
  triangle { <0, 9.0, 0>, <-9.0, 21.0,  0.0>, < 0.0, 21.0, -9.0> }
  triangle { <0, 9.0, 0>, < 0.0, 21.0, -9.0>, < 9.0, 21.0,  0.0> }
  smooth_triangle {
    <-9.0, 21.0,  0.0>, <-1, 0, 0>,
    < 9.0, 21.0,  0.0>, < 1, 0, 0>,
    < 0.0, 23.5,  9.0>, < 0, 1, 0>
  }
  translate <0, 0.5, 0>
}
"""
    (out_dir / "crystal_chalice.pov").write_bytes(text.encode("utf-8"))

def gen_domain_renderman_rib(out_dir: Path):
    text = """##RenderMan RIB-Structure 1.1
version 3.03
Display "pavilion.tif" "file" "rgba"
Projection "perspective" "fov" [ 40 ]
WorldBegin
  # Stepped temple plinth (PointsPolygons with quads)
  AttributeBegin
    PointsPolygons [ 4 4 4 4 4 ]
      [ 0 1 2 3  0 4 5 1  1 5 6 2  2 6 7 3  3 7 4 0 ]
      "P" [
        -12 0 -12   12 0 -12   12 0  12  -12 0  12
        -10 3 -10   10 3 -10   10 3  10  -10 3  10
      ]
  AttributeEnd

  # Four corner columns via TransformBegin / Translate / Rotate + Cylinder
  TransformBegin
    Translate -8 3 -8
    Rotate -90 1 0 0
    Cylinder 1.2 0 14 360
  TransformEnd
  TransformBegin
    Translate 8 3 -8
    Rotate -90 1 0 0
    Cylinder 1.2 0 14 360
  TransformEnd
  TransformBegin
    Translate -8 3 8
    Rotate -90 1 0 0
    Cylinder 1.2 0 14 360
  TransformEnd
  TransformBegin
    Translate 8 3 8
    Rotate -90 1 0 0
    Cylinder 1.2 0 14 360
  TransformEnd

  # Domed roof cupola
  TransformBegin
    Translate 0 17 0
    Rotate -90 1 0 0
    Sphere 9.5 0 9.5 360
  TransformEnd
WorldEnd
"""
    (out_dir / "pixar_pavilion.rib").write_bytes(text.encode("utf-8"))

def gen_domain_radiance_rad(out_dir: Path):
    text = """# LBNL Radiance Architectural Daylighting Atrium Scene
void plastic limestone
0
0
5 0.72 0.70 0.65 0.02 0.05

void alias column_mat limestone

void glass skylight_glazing
0
0
3 0.88 0.91 0.90

# Floor slab polygon (16m x 12m)
limestone polygon atrium_floor
0
0
12
  -8.0 -6.0  0.0
   8.0 -6.0  0.0
   8.0  6.0  0.0
  -8.0  6.0  0.0

# Skylight oculus ring & dome sphere + structural columns & bench !genbox
column_mat cylinder col_nw
0
0
7  -5.5  3.5 0.0   -5.5  3.5 6.0   0.35

column_mat cylinder col_ne
0
0
7   5.5  3.5 0.0    5.5  3.5 6.0   0.35

column_mat cone spire_lantern
0
0
8   0.0  0.0 6.0    0.0  0.0 8.5   2.4  0.3

skylight_glazing ring oculus_collar
0
0
8   0.0  0.0 6.0    0.0  0.0 1.0   1.2  3.6

skylight_glazing sphere sun_orb
0
0
4   0.0  0.0 4.2   0.75

!genbox limestone bench_south 4.0 0.8 0.55 | xform -t -2.0 -4.0 0.0
"""
    (out_dir / "atrium_daylight.rad").write_bytes(text.encode("utf-8"))

def gen_domain_qubicle_qb(out_dir: Path):
    def encode_qb_matrix_rle(name: str, sx: int, sy: int, sz: int, px: int, py: int, pz: int, pred):
        nb = name.encode("ascii")
        out = bytearray(struct.pack("<B", len(nb)) + nb + struct.pack("<3I3i", sx, sy, sz, px, py, pz))
        for z in range(sz):
            slice_vals = []
            for y in range(sy):
                for x in range(sx):
                    slice_vals.append(0xFF4488CC if pred(x, y, z) else 0x00000000)
            i = 0
            n = len(slice_vals)
            while i < n:
                v = slice_vals[i]
                run = 1
                while i + run < n and slice_vals[i + run] == v:
                    run += 1
                if run >= 3 or v in (2, 6):
                    out += struct.pack("<3I", 2, run, v)
                else:
                    for _ in range(run):
                        out += struct.pack("<I", v)
                i += run
            out += struct.pack("<I", 6)  # NEXTSLICEFLAG
        return bytes(out)

    # Matrix 0: Armored mech cockpit torso (6x5x6 at (-3, 6, -3))
    m_torso = encode_qb_matrix_rle(
        "Torso", 6, 5, 6, -3, 6, -3,
        lambda x, y, z: not (y == 4 and (x in (0, 5) or z in (0, 5))),
    )
    # Matrix 1 & 2: Left & Right articulated legs (2x6x3 at (-4, 0, -1) and (2, 0, -1))
    m_leg_l = encode_qb_matrix_rle("LeftLeg", 2, 6, 3, -4, 0, -1, lambda x, y, z: z < 2 or y == 0)
    m_leg_r = encode_qb_matrix_rle("RightLeg", 2, 6, 3, 2, 0, -1, lambda x, y, z: z < 2 or y == 0)

    # Header: version=1.1.0.0 (0x0101), RGBA(0), RightHanded(1), RLE(1), VisMask(0), numMatrices=3
    hdr = struct.pack("<6I", 0x00000101, 0, 1, 1, 0, 3)
    (out_dir / "mech_walker.qb").write_bytes(hdr + m_torso + m_leg_l + m_leg_r)

def gen_domain_build_kvx(out_dir: Path):
    # 8x6x6 medical kit box with top carrying handle (Z=0..1 handle, Z=2..5 body)
    xsiz, ysiz, zsiz = 8, 6, 6
    xoff0 = (xsiz + 1) * 4 + xsiz * (ysiz + 1) * 2
    col_blobs = [[b"" for _ in range(ysiz)] for _ in range(xsiz)]

    for x in range(xsiz):
        for y in range(ysiz):
            slabs = bytearray()
            # Handle arch at y in (2,3), x in 2..5, z in 0..1
            if y in (2, 3) and x in (2, 5):
                slabs += bytes([0, 2, 0x3F, 240, 240])
            elif y in (2, 3) and x in (3, 4):
                slabs += bytes([0, 1, 0x3F, 240])
            # Main medkit body at z=2..5 (chamfer corners)
            if not (x in (0, xsiz - 1) and y in (0, ysiz - 1)):
                col_idx = 32 if (x in (3, 4) or y in (2, 3)) else 255
                slabs += bytes([2, 4, 0x3F, col_idx, col_idx, col_idx, col_idx])
            col_blobs[x][y] = bytes(slabs)

    xoffsets = []
    xyoffsets = bytearray()
    slab_stream = bytearray()
    cur_x_rel = xoff0
    for x in range(xsiz):
        xoffsets.append(cur_x_rel)
        cur_xy = 0
        for y in range(ysiz):
            xyoffsets += struct.pack("<H", cur_xy)
            slab_stream += col_blobs[x][y]
            cur_xy += len(col_blobs[x][y])
        xyoffsets += struct.pack("<H", cur_xy)
        cur_x_rel += cur_xy
    xoffsets.append(cur_x_rel)

    vox_hdr = struct.pack(
        "<6i",
        xsiz, ysiz, zsiz,
        xsiz * 128, ysiz * 128, zsiz * 128,
    )
    xoff_bytes = b"".join(struct.pack("<i", xo) for xo in xoffsets)
    mip0 = vox_hdr + xoff_bytes + bytes(xyoffsets) + bytes(slab_stream)
    vga_pal = bytes(((i & 0x3F) for i in range(768)))
    (out_dir / "build_medkit.kvx").write_bytes(struct.pack("<I", len(mip0)) + mip0 + vga_pal)

def gen_domain_voxlap_kv6(out_dir: Path):
    # 7x9x6 defense turret: pedestal base + rotating gun housing + twin forward barrels
    xsiz, ysiz, zsiz = 7, 9, 6
    blocks = bytearray()
    xlen = [0] * xsiz
    ylen = bytearray()
    total_blks = 0

    for x in range(xsiz):
        col_sum = 0
        for y in range(ysiz):
            col_z = []
            # Base plinth (z = 4..5, x in 1..5, y in 2..7)
            if 1 <= x <= 5 and 2 <= y <= 7:
                col_z.extend([4, 5])
            # Turret dome (z = 1..3, x in 1..5, y in 3..7)
            if 1 <= x <= 5 and 3 <= y <= 7:
                col_z.extend([1, 2, 3])
            # Twin cannon barrels extending toward y = 0..2 at z = 2, x in (2, 4)
            if x in (2, 4) and 0 <= y <= 2:
                col_z.append(2)
            col_z = sorted(set(col_z))
            for z in col_z:
                # 8-byte block: B, G, R, light(128), uint16 zpos, uint8 vis(0x3F), uint8 normal(0)
                blocks += struct.pack("<4BHBb", 90, 140, 110, 128, z, 0x3F, 0)
            ylen += struct.pack("<H", len(col_z))
            col_sum += len(col_z)
            total_blks += len(col_z)
        xlen[x] = col_sum

    hdr = struct.pack("<4s3I3fI", b"Kvxl", xsiz, ysiz, zsiz, 3.5, 4.5, 3.0, total_blks)
    xlen_bytes = b"".join(struct.pack("<i", xl) for xl in xlen)
    (out_dir / "voxlap_turret.kv6").write_bytes(hdr + bytes(blocks) + xlen_bytes + bytes(ylen))

def gen_domain_westwood_vxl(out_dir: Path):
    def build_vxl_limb(name: str, sx: int, sy: int, sz: int, min_b, max_b, tx_vxl: float, ty_vxl: float, tz_vxl: float, pred):
        limb_hdr = name.encode("ascii")[:15].ljust(16, b"\x00") + struct.pack("<3I", 0, 1, 0)
        num_cols = sx * sy
        span_starts = []
        span_ends = []
        span_data = bytearray()
        for y in range(sy):
            for x in range(sx):
                col_z = [z for z in range(sz) if pred(x, y, z)]
                if not col_z:
                    span_starts.append(-1)
                    span_ends.append(-1)
                    continue
                st = len(span_data)
                z_cur = 0
                i = 0
                while i < len(col_z):
                    skip = col_z[i] - z_cur
                    run = 1
                    while i + run < len(col_z) and col_z[i + run] == col_z[i] + run:
                        run += 1
                    span_data += bytes([skip, run])
                    for _ in range(run):
                        span_data += bytes([120, 12])  # (color, normal)
                    span_data += bytes([run])          # duplicate count trailer
                    z_cur = col_z[i] + run
                    i += run
                if z_cur < sz:
                    span_data += bytes([sz - z_cur, 0, 0])
                en = len(span_data) - 1
                span_starts.append(st)
                span_ends.append(en)

        st_bytes = b"".join(struct.pack("<i", s) for s in span_starts)
        en_bytes = b"".join(struct.pack("<i", e) for e in span_ends)
        body = st_bytes + en_bytes + bytes(span_data)

        det = 1.0
        # 3x4 identity + translation (tx_vxl, ty_vxl, tz_vxl)
        xf = [
            1.0, 0.0, 0.0, tx_vxl,
            0.0, 1.0, 0.0, ty_vxl,
            0.0, 0.0, 1.0, tz_vxl,
        ]
        tailer = struct.pack(
            "<3If12f3f3f4B",
            0,
            len(st_bytes),
            len(st_bytes) + len(en_bytes),
            det,
            *xf,
            *min_b,
            *max_b,
            sx, sy, sz, 4,  # NormalsMode=4 (RA2)
        )
        assert len(limb_hdr) == 28 and len(tailer) == 92
        return limb_hdr, body, tailer

    # Limb 0: Armored tank hull with treads (10x6x3)
    h0, b0, t0 = build_vxl_limb(
        "HULL", 10, 6, 3, (-5.0, -3.0, 0.0), (5.0, 3.0, 3.0), 0.0, 0.0, 0.0,
        lambda x, y, z: not (z == 0 and 1 <= y <= 4 and x in (0, 9)),
    )
    # Limb 1: Turret (6x4x2 elevated at Z=+3)
    h1, b1, t1 = build_vxl_limb(
        "TURRET", 6, 4, 2, (-3.0, -2.0, 0.0), (3.0, 2.0, 2.0), -0.5, 0.0, 3.0,
        lambda x, y, z: True,
    )
    # Limb 2: Main cannon barrel (6x2x1 extending forward along +X at Z=+3.5)
    h2, b2, t2 = build_vxl_limb(
        "BARREL", 6, 2, 1, (0.0, -0.6, 0.0), (6.0, 0.6, 0.8), 2.5, 0.0, 3.6,
        lambda x, y, z: True,
    )

    # Adjust tailer offsets for limbs 1 and 2 relative to concatenated body_blob
    off1 = len(b0)
    off2 = len(b0) + len(b1)
    def shift_tailer(t_bytes: bytes, delta: int) -> bytes:
        s0, s1, s2 = struct.unpack_from("<3I", t_bytes, 0)
        return struct.pack("<3I", s0 + delta, s1 + delta, s2 + delta) + t_bytes[12:]

    t1 = shift_tailer(t1, off1)
    t2 = shift_tailer(t2, off2)
    body_all = b0 + b1 + b2
    pal_768 = bytes(((i * 3) & 0xFF for i in range(768)))
    file_hdr = b"Voxel Animation\x00" + struct.pack("<4I2B", 1, 3, 3, len(body_all), 16, 31) + pal_768
    assert len(file_hdr) == 802
    (out_dir / "ra2_grizzly_tank.vxl").write_bytes(file_hdr + h0 + h1 + h2 + body_all + t0 + t1 + t2)

def gen_domain_truespace_cob(out_dir: Path):
    def cob_chunk(ctype: bytes, cid: int, pid: int, payload: bytes) -> bytes:
        return struct.pack("<4s2H3i", ctype, 0, 4, cid, pid, len(payload)) + payload

    def cob_name_axes_mat(name: str, tx=0.0, ty=0.0, tz=0.0) -> bytes:
        nb = name.encode("ascii")
        axes = struct.pack("<12f", 0, 0, 0,  1, 0, 0,  0, 1, 0,  0, 0, 1)
        mat3x4 = struct.pack("<12f", 1, 0, 0, tx,  0, 1, 0, ty,  0, 0, 1, tz)
        return struct.pack("<2h", 0, len(nb)) + nb + axes + mat3x4

    def build_polh(name: str, cid: int, pid: int, verts, faces, tx=0.0, ty=0.0, tz=0.0) -> bytes:
        pay = bytearray(cob_name_axes_mat(name, tx, ty, tz))
        pay += struct.pack("<i", len(verts))
        for (vx, vy, vz) in verts:
            pay += struct.pack("<3f", vx, vy, vz)
        pay += struct.pack("<i2f", 1, 0.0, 0.0)  # 1 UV
        pay += struct.pack("<i", len(faces))
        for (is_hole, loop) in faces:
            if is_hole:
                pay += struct.pack("<Bh", 0x08, len(loop))
            else:
                pay += struct.pack("<B2h", 0x00, len(loop), 0)
            for vi in loop:
                pay += struct.pack("<2i", vi, 0)
        return cob_chunk(b"PolH", cid, pid, bytes(pay))

    # Group root Id=10
    grou = cob_chunk(b"Grou", 10, 0, cob_name_axes_mat("ShuttleGroup", 0.0, 0.0, 2.0))

    # Fuselage PolH Id=11 (parent=10): nose cone + delta body + cockpit window hole on top deck!
    fuse_verts = [
        (0.0, 14.0, 0.0),     # 0 nose apex
        (-4.0, -6.0, -1.5),   # 1 port aft bottom
        (4.0, -6.0, -1.5),    # 2 stbd aft bottom
        (3.0, -6.0, 2.5),     # 3 stbd aft top
        (-3.0, -6.0, 2.5),    # 4 port aft top
        # Inner viewport hole on aft bulkhead (5..8)
        (-1.0, -6.0, -0.2),
        (1.0, -6.0, -0.2),
        (1.0, -6.0, 1.2),
        (-1.0, -6.0, 1.2),
    ]
    fuse_faces = [
        (False, [0, 2, 1]),
        (False, [0, 3, 2]),
        (False, [0, 4, 3]),
        (False, [0, 1, 4]),
        (False, [1, 2, 3, 4]),
        (True,  [5, 8, 7, 6]),  # F_HOLE cutout on aft bulkhead
    ]
    pol_fuse = build_polh("Fuselage", 11, 10, fuse_verts, fuse_faces)

    # Vertical tail fin PolH Id=12 (parent=10, offset tz=+2.5)
    fin_verts = [(0.0, 0.0, 0.0), (-0.4, -6.0, 0.0), (0.4, -6.0, 0.0), (0.0, -5.5, 5.5)]
    fin_faces = [(False, [0, 2, 1]), (False, [0, 3, 2]), (False, [0, 1, 3]), (False, [1, 2, 3])]
    pol_fin = build_polh("TailFin", 12, 10, fin_verts, fin_faces, 0.0, 0.0, 2.5)

    end_chunk = cob_chunk(b"END ", 99, 0, b"")
    hdr = b"Caligari V00.01BLH             \n"
    assert len(hdr) == 32
    (out_dir / "truespace_shuttle.cob").write_bytes(hdr + grou + pol_fuse + pol_fin + end_chunk)

def gen_domain_neutral_nff(out_dir: Path):
    text = """# Eric Haines SPD Neutral File Format: Satellite Dish & Tripod
v
from 0 8 24
at 0 4 0
up 0 1 0
angle 45
hither 0.5
resolution 800 600
b 0.05 0.08 0.14
l 15 25 20 1 1 1
f 0.85 0.88 0.92 0.7 0.3 32 0 1.0
# Parabolic dish octagonal rim + central feed horn + 3 tripod legs
p 4
 -5.0  6.0 -2.0
  5.0  6.0 -2.0
  5.0  6.0  2.0
 -5.0  6.0  2.0
pp 3
  0.0  5.5  0.0   0 1 0
  5.0  7.5 -5.0   0 1 0
  5.0  7.5  5.0   0 1 0
pp 3
  0.0  5.5  0.0   0 1 0
 -5.0  7.5  5.0   0 1 0
 -5.0  7.5 -5.0   0 1 0
s 0.0 10.5 0.0 1.1
c
  0.0 5.5 0.0 0.25
  0.0 9.6 0.0 0.12
c -4.0 0.0 -3.0 0.3   0.0 5.5 0.0 0.2
c  4.0 0.0 -3.0 0.3   0.0 5.5 0.0 0.2
c  0.0 0.0  4.5 0.3   0.0 5.5 0.0 0.2
"""
    (out_dir / "spd_geodesic.nff").write_bytes(text.encode("utf-8"))

def gen_domain_evolver_fe(out_dir: Path):
    # Hexagonal catenoid soap film between two wire rings (PARAMETER evaluation + signed oriented edges)
    lines = [
        "// Surface Evolver Catenoid Minimal Soap Film",
        "PARAMETER r_outer = 5.0",
        "PARAMETER r_waist = 2.8",
        "PARAMETER h_ring  = 4.0",
        "",
        "vertices",
    ]
    segs = 6
    vid = 1
    for z, r in ((-4.0, 5.0), (0.0, 2.8), (4.0, 5.0)):
        for i in range(segs):
            ang = 2.0 * math.pi * i / segs
            fixed = " fixed" if abs(z) > 1.0 else ""
            lines.append(f"  {vid:2d}  {r * math.cos(ang):8.4f}  {r * math.sin(ang):8.4f}  {z:7.4f}{fixed}")
            vid += 1

    lines.append("\nedges")
    # Ring edges: 1..6 (bottom), 7..12 (waist), 13..18 (top)
    eid = 1
    for ring in range(3):
        base = ring * segs + 1
        for i in range(segs):
            v0 = base + i
            v1 = base + ((i + 1) % segs)
            lines.append(f"  {eid:2d}  {v0:2d}  {v1:2d}")
            eid += 1
    # Vertical/meridian edges: 19..24 (bottom->waist), 25..30 (waist->top)
    for i in range(segs):
        lines.append(f"  {19 + i:2d}  {1 + i:2d}  {7 + i:2d}")
    for i in range(segs):
        lines.append(f"  {25 + i:2d}  {7 + i:2d}  {13 + i:2d}")

    lines.append("\nfaces")
    fid = 1
    for i in range(segs):
        ni = (i + 1) % segs
        lines.append(f"  {fid:2d}   {1 + i:3d}  {19 + ni:3d}  {-(7 + i):3d}  {-(19 + i):3d}  tension 1 color lightblue")
        fid += 1
        lines.append(f"  {fid:2d}   {7 + i:3d}  {25 + ni:3d}  {-(13 + i):3d}  {-(25 + i):3d}  tension 1 color cyan")
        fid += 1

    lines.append("\nread\ng 5;\n")
    (out_dir / "catenoid_soapfilm.fe").write_bytes(("\n".join(lines)).encode("utf-8"))

def gen_domain_movie_byu(out_dir: Path):
    # Supersonic turbofan engine nacelle cowling (2 parts, octagonal intake -> exhaust duct)
    segs = 8
    verts = []
    for x, r in ((-10.0, 4.2), (0.0, 5.4), (10.0, 3.8)):
        for i in range(segs):
            ang = 2.0 * math.pi * i / segs
            verts.append((x, r * math.cos(ang), r * math.sin(ang)))
    verts.append((0.0, 6.2, 0.0))   # 25: top pylon root
    verts.append((6.0, 8.8, 0.0))   # 26: top pylon tip aft
    verts.append((2.0, 8.8, 0.0))   # 27: top pylon tip fwd

    # Part 1: 16 cowling quad panels (1..16); Part 2: 2 pylon triangles (17..18)
    polys = []
    for ring in range(2):
        b0 = ring * segs + 1
        b1 = (ring + 1) * segs + 1
        for i in range(segs):
            ni = (i + 1) % segs
            polys.append([b0 + i, b0 + ni, b1 + ni, -(b1 + i)])
    polys.append([25, 26, -27])
    polys.append([25, 27, -26])

    nedges = sum(len(p) for p in polys)
    lines = [
        f"      2     {len(verts):2d}     {len(polys):2d}     {nedges:2d}",
        "      1     16     17     18",
    ]
    for (vx, vy, vz) in verts:
        lines.append(f"{vx:12.5E}{vy:12.5E}{vz:12.5E}")
    for p in polys:
        lines.append(" ".join(f"{idx:6d}" for idx in p))
    (out_dir / "nasa_nacelle.byu").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))

def gen_domain_cart3d_tri(out_dir: Path):
    # Supersonic Cranked-Arrow Delta Wing + Vertical Stabilizer (watertight 1-based Cart3D .triq)
    verts = [
        (0.0, 0.0, 0.0),      # 1 nose apex
        (14.0, -9.5, -0.4),   # 2 port wingtip
        (14.0,  9.5, -0.4),   # 3 stbd wingtip
        (16.0,  0.0,  1.2),   # 4 dorsal spine aft
        (16.0,  0.0, -0.9),   # 5 ventral keel aft
        (11.0,  0.0,  0.9),   # 6 fin root fwd
        (16.5,  0.0,  4.8),   # 7 fin tip
        (15.5, -0.35, 1.1),   # 8 fin base port
        (15.5,  0.35, 1.1),   # 9 fin base stbd
    ]
    tris = [
        # Wing upper & lower surfaces + aft base
        (1, 2, 4), (1, 4, 3),
        (1, 5, 2), (1, 3, 5),
        (2, 5, 4), (3, 4, 5),
        # Vertical tail fin
        (6, 8, 7), (6, 7, 9), (8, 9, 7), (6, 9, 8),
    ]
    lines = [f"{len(verts)} {len(tris)} 1"]
    for (vx, vy, vz) in verts:
        lines.append(f"  {vx:10.4f}  {vy:10.4f}  {vz:10.4f}")
    for (i0, i1, i2) in tris:
        lines.append(f"  {i0:3d}  {i1:3d}  {i2:3d}")
    # Component tags + Cp pressure scalar
    lines.append(" ".join("1" if k < 6 else "2" for k in range(len(tris))))
    lines.extend(f"  {-0.15 + 0.04 * i:.4f}" for i in range(len(verts)))
    (out_dir / "cart3d_delta_wing.tri").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))

def gen_domain_irrlicht_irrmesh(out_dir: Path):
    # Multi-buffer Irrlicht XML temple shrine: Buffer 0 ("standard" 9-tok plinth), Buffer 1 ("2tcoords" 11-tok obelisk roof)
    text = """<?xml version="1.0"?>
<mesh xmlns="http://irrlicht.sourceforge.net/IRRMESH_09_2007" version="1.0">
  <boundingBox minEdge="-8.0 0.0 -8.0" maxEdge="8.0 16.0 8.0"/>
  <buffer>
    <material><enum name="Type" value="solid"/></material>
    <vertices type="standard" vertexCount="8">
      -8.0  0.0 -8.0   0 -1  0  ffffffff  0.0 0.0
       8.0  0.0 -8.0   0 -1  0  ffffffff  1.0 0.0
       8.0  0.0  8.0   0 -1  0  ffffffff  1.0 1.0
      -8.0  0.0  8.0   0 -1  0  ffffffff  0.0 1.0
      -6.0  4.0 -6.0   0  1  0  ffccddee  0.0 0.0
       6.0  4.0 -6.0   0  1  0  ffccddee  1.0 0.0
       6.0  4.0  6.0   0  1  0  ffccddee  1.0 1.0
      -6.0  4.0  6.0   0  1  0  ffccddee  0.0 1.0
    </vertices>
    <indices indexCount="30">
      0 4 5  0 5 1
      1 5 6  1 6 2
      2 6 7  2 7 3
      3 7 4  3 4 0
      4 7 6  4 6 5
    </indices>
  </buffer>
  <buffer>
    <vertices type="2tcoords" vertexCount="5">
      -4.0  4.0 -4.0   0 1 0  ffffcc88  0 0  0 0
       4.0  4.0 -4.0   0 1 0  ffffcc88  1 0  1 0
       4.0  4.0  4.0   0 1 0  ffffcc88  1 1  1 1
      -4.0  4.0  4.0   0 1 0  ffffcc88  0 1  0 1
       0.0 16.0  0.0   0 1 0  ffffffff  0.5 0.5  0.5 0.5
    </vertices>
    <indices indexCount="12">
      0 4 1  1 4 2  2 4 3  3 4 0
    </indices>
  </buffer>
</mesh>
"""
    (out_dir / "irrlicht_temple.irrmesh").write_bytes(text.encode("utf-8"))

def gen_domain_sega_ninja_nj(out_dir: Path):
    def build_njcm_vlist_plist(verts, strips):
        # NJD_CV_VN (0x23): w0 = (sizeDwords << 16) | 0x23; w1 = (nbVerts << 16) | 0; 6 floats/vert
        size_dwords = 1 + len(verts) * 6
        vlist = bytearray(struct.pack("<2I", (size_dwords << 16) | 0x23, len(verts) << 16))
        for (px, py, pz) in verts:
            nx, ny, nz = _norm3(px, py + 1.0, pz)
            vlist += struct.pack("<6f", px, py, pz, nx, ny, nz)
        vlist += struct.pack("<I", 0x000000FF)  # NJD_CV_END

        # NJD_CS (0x40): u16 type(0x0040), u16 sizeWords, u16 nbStrips, then for each strip: i16 len + u16 indices
        strip_words = bytearray(struct.pack("<H", len(strips)))
        for st in strips:
            strip_words += struct.pack("<h", len(st))
            for vi in st:
                strip_words += struct.pack("<H", vi)
        size_u16 = len(strip_words) // 2
        plist = struct.pack("<2H", 0x0040, size_u16) + bytes(strip_words) + struct.pack("<H", 0x00FF)
        if len(plist) % 4 != 0:
            plist += b"\x00\x00"
        return bytes(vlist), plist

    # Root NJS_OBJECT @ 0 (52B): Hovercraft hull; Child NJS_OBJECT @ 52 (52B): Rear ducted fan pod
    hull_verts = [
        (0.0, 1.5, 9.0),     # 0 nose
        (-4.5, 0.0, 2.0),    # 1 port fwd
        (4.5, 0.0, 2.0),     # 2 stbd fwd
        (-5.0, 0.5, -7.0),   # 3 port aft
        (5.0, 0.5, -7.0),    # 4 stbd aft
        (0.0, 2.8, -5.0),    # 5 cockpit canopy
    ]
    hull_strips = [[0, 1, 2], [1, 3, 5, 4, 2], [0, 2, 5, 1]]
    vl0, pl0 = build_njcm_vlist_plist(hull_verts, hull_strips)

    pod_verts = [
        (-2.2, 0.0, -1.5), (2.2, 0.0, -1.5), (2.2, 3.2, -1.5), (-2.2, 3.2, -1.5),
        (0.0, 1.6, 2.0),
    ]
    pod_strips = [[0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4]]
    vl1, pl1 = build_njcm_vlist_plist(pod_verts, pod_strips)

    # Layout inside NJCM chunk:
    # 0..51:   Root NJS_OBJECT (model @ 104, child @ 52)
    # 52..103: Child NJS_OBJECT (model @ 128, pos = (0, 2.5, -5.5), BAMS rotY = 4096)
    # 104..127: Root NJS_CNK_MODEL (24B)
    # 128..151: Child NJS_CNK_MODEL (24B)
    # 152..: vl0, pl0, vl1, pl1
    off_vl0 = 152
    off_pl0 = off_vl0 + len(vl0)
    off_vl1 = off_pl0 + len(pl0)
    off_pl1 = off_vl1 + len(vl1)

    obj_root = struct.pack(
        "<2I3f3i3f2I",
        0, 104,
        0.0, 0.0, 0.0,
        0, 0, 0,
        1.0, 1.0, 1.0,
        52, 0,
    )
    obj_child = struct.pack(
        "<2I3f3i3f2I",
        0, 128,
        0.0, 2.5, -5.5,
        0, 2048, 0,  # BAMS Y rotation (~11.25 deg)
        1.0, 1.0, 1.0,
        0, 0,
    )
    mdl_root = struct.pack("<2I4f", off_vl0, off_pl0, 0.0, 1.0, 0.0, 10.0)
    mdl_child = struct.pack("<2I4f", off_vl1, off_pl1, 0.0, 1.5, 0.0, 4.0)
    chunk_body = obj_root + obj_child + mdl_root + mdl_child + vl0 + pl0 + vl1 + pl1
    njtl = b"NJTL" + struct.pack("<I2I", 8, 0, 0)  # Leading texture-list chunk
    njcm = b"NJCM" + struct.pack("<I", len(chunk_body)) + chunk_body
    (out_dir / "dreamcast_hovercraft.nj").write_bytes(njtl + njcm)

def gen_domain_ubertools_skb(out_dir: Path):
    def build_skb_v3_surface(surf_name: str, verts_weighted, tris):
        # skbSurface_t v3 = 96 bytes
        ofs_tris = 96
        tri_bytes = b"".join(struct.pack("<3i", a, b, c) for (a, b, c) in tris)
        ofs_verts = ofs_tris + len(tri_bytes)
        v_blob = bytearray()
        for (nrm, uv, weights) in verts_weighted:
            v_blob += struct.pack("<3f2fi", nrm[0], nrm[1], nrm[2], uv[0], uv[1], len(weights))
            for (bidx, bw, ox, oy, oz) in weights:
                v_blob += struct.pack("<if3f", bidx, bw, ox, oy, oz)
        ofs_collapse = ofs_verts + len(v_blob)
        collapse_bytes = b"".join(struct.pack("<i", i) for i in range(len(verts_weighted)))
        ofs_end = ofs_collapse + len(collapse_bytes)
        shdr = struct.pack(
            "<4s64s7i",
            b"SKL ",
            surf_name.encode("ascii").ljust(64, b"\x00"),
            len(tris),
            len(verts_weighted),
            0,
            ofs_tris,
            ofs_verts,
            ofs_collapse,
            ofs_end,
        )
        assert len(shdr) == 96
        return shdr + tri_bytes + bytes(v_blob) + collapse_bytes

    # 3 bones: 0 = grip_root (-1), 1 = crossguard (0), 2 = blade_mid (1)
    bones = [
        (-1, 0, b"tag_grip_root"),
        (0,  0, b"bone_crossguard"),
        (1,  0, b"bone_blade"),
    ]
    bone_blob = b"".join(struct.pack("<2i32s", p, fl, nm.ljust(32, b"\x00")) for (p, fl, nm) in bones)
    assert len(bone_blob) == 120

    # Surface 0: Hilt & Crossguard (9 vertices, 12 triangles)
    hilt_pts = [
        # Lower grip diamond @ z = -10, r = 1.6 (bone 0)
        (0, (-1.6, 0.0, -10.0)),
        (0, (0.0, -1.6, -10.0)),
        (0, (1.6, 0.0, -10.0)),
        (0, (0.0, 1.6, -10.0)),
        # Crossguard flared ring @ z = 0, x = +-4.2, y = +-1.8 (bone 1)
        (1, (-4.2, 0.0, 0.0)),
        (1, (0.0, -1.8, 0.0)),
        (1, (4.2, 0.0, 0.0)),
        (1, (0.0, 1.8, 0.0)),
        # Pommel tip @ z = -13.0 (bone 0)
        (0, (0.0, 0.0, -13.0)),
    ]
    hilt_verts = []
    for bidx, (px, py, pz) in hilt_pts:
        nx, ny, nz = _norm3(px, py, -0.3)
        hilt_verts.append(((nx, ny, nz), ((px + 5.0) / 10.0, (pz + 13.0) / 15.0), [(bidx, 1.0, px, py, pz)]))
    hilt_tris = []
    for i in range(4):
        j = (i + 1) % 4
        hilt_tris.append((i, 4 + i, 4 + j))
        hilt_tris.append((i, 4 + j, j))
        hilt_tris.append((8, j, i))

    # Surface 1: Broadsword Blade (5 vertices, 6 triangles, blended across bones 1 & 2)
    blade_verts = [
        ((0.0, -1.0, 0.0), (0.0, 0.0), [(1, 0.5, -2.2, 0.0, 0.5), (2, 0.5, -2.2, 0.0, 0.5)]),
        ((0.0, -1.0, 0.0), (0.5, 0.0), [(1, 0.5,  0.0, -0.6, 0.5), (2, 0.5, 0.0, -0.6, 0.5)]),
        ((0.0,  1.0, 0.0), (1.0, 0.0), [(1, 0.5,  2.2, 0.0, 0.5), (2, 0.5,  2.2, 0.0, 0.5)]),
        ((0.0,  1.0, 0.0), (0.5, 0.0), [(1, 0.5,  0.0,  0.6, 0.5), (2, 0.5, 0.0,  0.6, 0.5)]),
        ((0.0,  0.0, 1.0), (0.5, 1.0), [(2, 1.0,  0.0,  0.0, 22.0)]),
    ]
    blade_tris = [
        (0, 4, 1),
        (1, 4, 2),
        (2, 4, 3),
        (3, 4, 0),
        (0, 1, 2),
        (0, 2, 3),
    ]

    s0 = build_skb_v3_surface("s_broadsword_hilt", hilt_verts, hilt_tris)
    s1 = build_skb_v3_surface("s_broadsword_blade", blade_verts, blade_tris)

    ofs_bones = 92
    ofs_surfaces = ofs_bones + len(bone_blob)
    ofs_end = ofs_surfaces + len(s0) + len(s1)
    hdr = struct.pack(
        "<4si64s5i",
        b"SKL ",
        3,
        b"models/weapons/broadsword/fakk2_broadsword.skb".ljust(64, b"\x00"),
        2,
        len(bones),
        ofs_bones,
        ofs_surfaces,
        ofs_end,
    )
    assert len(hdr) == 92
    (out_dir / "fakk2_broadsword.skb").write_bytes(hdr + bone_blob + s0 + s1)

def gen_domain_ubertools_ska(out_dir: Path):
    # 6-bone articulated biped hierarchy: pelvis -> spine -> head, left_arm, right_arm, leg_r
    bones = [
        (-1, 0, b"Bip01_Pelvis",   (0.0, 0.0, 0.0, 1.0), (0.0,  0.0, 36.0)),
        (0,  0, b"Bip01_Spine",    (0.0, 0.0, 0.0, 1.0), (0.0,  0.0, 14.0)),
        (1,  0, b"Bip01_Head",     (0.0, 0.0, 0.0, 1.0), (0.0,  0.0, 12.0)),
        (1,  0, b"Bip01_L_Arm",    (0.0, 0.0, 0.258819, 0.965926), (-11.0, 2.0, 9.0)),
        (1,  0, b"Bip01_R_Arm",    (0.0, 0.0, -0.258819, 0.965926), (11.0, 2.0, 9.0)),
        (0,  0, b"Bip01_R_Foot",   (0.0, 0.0, 0.0, 1.0), (5.5,  0.0, -36.0)),
    ]
    bone_table = b"".join(
        struct.pack("<2i32s", par, fl, nm.ljust(32, b"\x00"))
        for (par, fl, nm, _, _) in bones
    )
    assert len(bone_table) == 6 * 40

    def pack_frame(z_bob: float):
        # 40B frame header: bounds[2][3] (24B) + radius (4B) + delta[3] (12B)
        fhdr = struct.pack("<6ff3f", -16.0, -10.0, 0.0, 16.0, 10.0, 64.0, 38.0, 0.0, 0.0, 0.0)
        bbytes = bytearray()
        for idx, (_, _, _, (qx, qy, qz, qw), (tx, ty, tz)) in enumerate(bones):
            use_z = tz + (z_bob if idx == 0 else 0.0)
            bbytes += struct.pack(
                "<4h3hH",
                int(round(qx * 32767.0)),
                int(round(qy * 32767.0)),
                int(round(qz * 32767.0)),
                int(round(qw * 32767.0)),
                int(round(tx * 64.0)),
                int(round(ty * 64.0)),
                int(round(use_z * 64.0)),
                0,
            )
        return fhdr + bytes(bbytes)

    frames_blob = pack_frame(0.0) + pack_frame(1.5)
    ofs_bones = 116
    ofs_frames = ofs_bones + len(bone_table)
    ofs_end = ofs_frames + len(frames_blob)
    hdr = struct.pack(
        "<4si64s3i2f3f3i",
        b"SKAN",
        3,
        b"models/julie/idle_stand.ska".ljust(64, b"\x00"),
        0,
        2,              # numFrames
        len(bones),     # numBones
        0.1,            # totaltime
        0.05,           # frametime
        0.0, 0.0, 0.0,  # totaldelta
        ofs_bones,
        ofs_frames,
        ofs_end,
    )
    assert len(hdr) == 116
    (out_dir / "fakk2_hero_idle.ska").write_bytes(hdr + bone_table + frames_blob)

def gen_domain_mohaa_skd(out_dir: Path):
    def pack_bone_file_data(name: bytes, parent: bytes, bone_type: int, offset_xyz):
        base_data = struct.pack("<3f", offset_xyz[0], offset_xyz[1], offset_xyz[2])
        ofs_base = 84
        ofs_end = ofs_base + len(base_data)
        bhdr = struct.pack(
            "<32s32s5i",
            name.ljust(32, b"\x00"),
            parent.ljust(32, b"\x00"),
            bone_type,
            ofs_base,
            ofs_end,
            ofs_end,
            ofs_end,
        )
        assert len(bhdr) == 84
        return bhdr + base_data

    bones_blob = (
        pack_bone_file_data(b"tag_weapon", b"worldbone", 1, (0.0, 0.0, 0.0))
        + pack_bone_file_data(b"tag_receiver", b"tag_weapon", 1, (12.0, 0.0, 1.5))
        + pack_bone_file_data(b"tag_muzzle", b"tag_receiver", 1, (26.0, 0.0, 0.5))
    )

    def pack_skd_surface(surf_name: str, verts, tris):
        ofs_tris = 100
        tri_bytes = b"".join(struct.pack("<3i", a, b, c) for (a, b, c) in tris)
        ofs_verts = ofs_tris + len(tri_bytes)
        v_blob = bytearray()
        for (nrm, uv, morphs, weights) in verts:
            v_blob += struct.pack("<3f2f2i", nrm[0], nrm[1], nrm[2], uv[0], uv[1], len(weights), len(morphs))
            for (midx, mx, my, mz) in morphs:
                v_blob += struct.pack("<i3f", midx, mx, my, mz)
            for (bidx, bw, ox, oy, oz) in weights:
                v_blob += struct.pack("<if3f", bidx, bw, ox, oy, oz)
        ofs_end = ofs_verts + len(v_blob)
        shdr = struct.pack(
            "<4s64s8i",
            b"SKMD",
            surf_name.encode("ascii").ljust(64, b"\x00"),
            len(tris),
            len(verts),
            0,
            ofs_tris,
            ofs_verts,
            0,
            ofs_end,
            0,
        )
        assert len(shdr) == 100
        return shdr + tri_bytes + bytes(v_blob)

    # Stock box prism around tag_weapon..tag_receiver (8 verts, 12 tris)
    stock_coords = [
        (0, (-8.0, -1.5, -2.5)), (0, (-8.0,  1.5, -2.5)), (0, (-8.0,  1.5,  1.2)), (0, (-8.0, -1.5,  1.2)),
        (1, ( 2.0, -1.2, -1.4)), (1, ( 2.0,  1.2, -1.4)), (1, ( 2.0,  1.2,  1.0)), (1, ( 2.0, -1.2,  1.0)),
    ]
    stock_verts = [
        ((0.0, 0.0, 1.0), (0.25, 0.5), [(0, 0.1, 0.0, 0.0)] if i == 4 else [], [(b, 1.0, x, y, z)])
        for i, (b, (x, y, z)) in enumerate(stock_coords)
    ]
    box_tris = [
        (0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1), (1, 5, 6), (1, 6, 2),
        (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0),
    ]

    # Barrel prism from tag_receiver -> tag_muzzle (5 verts, 6 tris)
    barrel_verts = [
        ((0.0, -1.0, 0.0), (0.0, 0.0), [], [(1, 1.0, 2.0, -0.5, 0.0)]),
        ((0.0,  1.0, 0.0), (0.5, 0.0), [], [(1, 1.0, 2.0,  0.5, 0.0)]),
        ((0.0,  0.0, 1.0), (1.0, 0.0), [], [(1, 1.0, 2.0,  0.0, 0.8)]),
        ((0.0,  0.0,-1.0), (0.5, 0.5), [], [(1, 1.0, 2.0,  0.0,-0.5)]),
        ((1.0,  0.0, 0.0), (0.5, 1.0), [], [(2, 1.0, 2.5,  0.0, 0.0)]),
    ]
    barrel_tris = [(0, 4, 1), (1, 4, 2), (2, 4, 3), (3, 4, 0), (0, 1, 2), (0, 2, 3)]

    s0 = pack_skd_surface("garand_wood", stock_verts, box_tris)
    s1 = pack_skd_surface("garand_steel", barrel_verts, barrel_tris)

    ofs_bones = 148
    ofs_surfs = ofs_bones + len(bones_blob)
    ofs_end = ofs_surfs + len(s0) + len(s1)
    hdr = struct.pack(
        "<4si64s5i10i4i",
        b"SKMD",
        5,
        b"models/weapons/m1_garand/garand.skd".ljust(64, b"\x00"),
        2,
        3,
        ofs_bones,
        ofs_surfs,
        ofs_end,
        *([0] * 10),
        0, 0,
        1, 0,
    )
    assert len(hdr) == 148
    (out_dir / "mohaa_garand_rifle.skd").write_bytes(hdr + bones_blob + s0 + s1)

def gen_domain_mohaa_skc(out_dir: Path):
    # 5 bones * 2 channels ("pos" + "rot") = 10 channels, 2 frames
    bdefs = [
        ("bip01",           (0.0, 0.0, 34.0), (0.0, 0.0, 0.0, 1.0)),
        ("bip01_spine",     (0.0, 0.0, 14.0), (0.0, 0.0, 0.0, 1.0)),
        ("bip01_spine_head",(0.0, 0.0, 11.0), (0.0, 0.0, 0.0, 1.0)),
        ("bip01_spine_larm",(-10.0, 3.0, 8.0),(0.0, 0.0, 0.130526, 0.991445)),
        ("bip01_spine_rarm",( 10.0, 3.0, 8.0),(0.0, 0.0,-0.130526, 0.991445)),
    ]
    chan_names = bytearray()
    f0_vals = bytearray()
    f1_vals = bytearray()
    for name, (px, py, pz), (qx, qy, qz, qw) in bdefs:
        chan_names += f"{name} pos".encode("ascii").ljust(32, b"\x00")
        chan_names += f"{name} rot".encode("ascii").ljust(32, b"\x00")
        f0_vals += struct.pack("<4f", px, py, pz, 0.0)
        f0_vals += struct.pack("<4f", qx, qy, qz, qw)
        f1_vals += struct.pack("<4f", px, py, pz + 1.0, 0.0)
        f1_vals += struct.pack("<4f", qx, qy, qz, qw)

    num_channels = 10
    num_frames = 2
    ofs_f0_chans = 48 + num_frames * 48
    ofs_f1_chans = ofs_f0_chans + len(f0_vals)
    ofs_chan_names = ofs_f1_chans + len(f1_vals)
    total_size = ofs_chan_names + len(chan_names)

    hdr = struct.pack(
        "<4s3if3ff3i",
        b"SKAN",
        13,
        0,
        total_size,
        0.05,
        0.0, 0.0, 0.0,
        0.0,
        num_channels,
        ofs_chan_names,
        num_frames,
    )
    assert len(hdr) == 48
    fr0 = struct.pack("<6ff3ffi", -14.0, -8.0, 0.0, 14.0, 8.0, 62.0, 34.0, 0.0, 0.0, 0.0, 0.0, ofs_f0_chans)
    fr1 = struct.pack("<6ff3ffi", -14.0, -8.0, 0.0, 14.0, 8.0, 63.0, 34.0, 0.0, 0.0, 0.0, 0.0, ofs_f1_chans)
    (out_dir / "mohaa_rifle_aim.skc").write_bytes(hdr + fr0 + fr1 + bytes(f0_vals) + bytes(f1_vals) + bytes(chan_names))

def gen_domain_raven_gla(out_dir: Path):
    def pack_comp_quat_bone(qx, qy, qz, qw, tx, ty, tz):
        u_w = max(0, min(65535, int(round((qw + 2.0) * 16383.0))))
        u_x = max(0, min(65535, int(round((qx + 2.0) * 16383.0))))
        u_y = max(0, min(65535, int(round((qy + 2.0) * 16383.0))))
        u_z = max(0, min(65535, int(round((qz + 2.0) * 16383.0))))
        u_tx = max(0, min(65535, int(round((tx + 512.0) * 64.0))))
        u_ty = max(0, min(65535, int(round((ty + 512.0) * 64.0))))
        u_tz = max(0, min(65535, int(round((tz + 512.0) * 64.0))))
        return struct.pack("<7H", u_w, u_x, u_y, u_z, u_tx, u_ty, u_tz)

    # 5 bones: model_root -> pelvis -> thoracic -> rhand, lhand
    skel_defs = [
        (b"model_root", -1, [1],    (0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0)),
        (b"pelvis",      0, [2],    (0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 38.0)),
        (b"thoracic",    1, [3, 4], (0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 16.0)),
        (b"rhand",       2, [],     (0.0, 0.0,-0.2, 0.98),(13.0, 4.0, 6.0)),
        (b"lhand",       2, [],     (0.0, 0.0, 0.2, 0.98),(-13.0, 4.0, 6.0)),
    ]
    num_bones = len(skel_defs)
    skel_entries = []
    for nm, par, chs, _, (tx, ty, tz) in skel_defs:
        ident_3x4 = struct.pack("<12f", 1,0,0,tx, 0,1,0,ty, 0,0,1,tz)
        inv_3x4   = struct.pack("<12f", 1,0,0,-tx, 0,1,0,-ty, 0,0,1,-tz)
        ch_bytes  = b"".join(struct.pack("<i", c) for c in chs)
        skel_entries.append(
            struct.pack("<64sIi", nm.ljust(64, b"\x00"), 0, par)
            + ident_3x4 + inv_3x4
            + struct.pack("<i", len(chs)) + ch_bytes
        )

    skel_offs = []
    cur_rel = 4 * num_bones
    for ent in skel_entries:
        skel_offs.append(cur_rel)
        cur_rel += len(ent)
    skel_blob = b"".join(struct.pack("<i", o) for o in skel_offs) + b"".join(skel_entries)

    pool_entries = [pack_comp_quat_bone(*q, *t) for (_, _, _, q, t) in skel_defs]
    # Add extra frame-1 pose for bone 3 (rhand saber swing)
    pool_entries.append(pack_comp_quat_bone(0.0, 0.25, 0.0, 0.968, 15.0, 6.0, 10.0))
    pool_blob = b"".join(pool_entries)

    def idx24(v: int) -> bytes:
        return bytes([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF])

    frames_blob = b"".join(idx24(i) for i in [0, 1, 2, 3, 4]) + b"".join(idx24(i) for i in [0, 1, 2, 5, 4])

    ofs_skel = 100
    ofs_frames = ofs_skel + len(skel_blob)
    ofs_pool = ofs_frames + len(frames_blob)
    ofs_end = ofs_pool + len(pool_blob)
    hdr = struct.pack(
        "<4si64sf6i",
        b"2LGA",
        6,
        b"models/players/_humanoid/_humanoid.gla".ljust(64, b"\x00"),
        1.0,
        2,
        ofs_frames,
        num_bones,
        ofs_pool,
        ofs_skel,
        ofs_end,
    )
    assert len(hdr) == 100
    (out_dir / "jedi_humanoid.gla").write_bytes(hdr + skel_blob + frames_blob + pool_blob)

def gen_domain_rtcw_mdc(out_dir: Path):
    # Octagonal German Stahlhelm dome + delta-compressed Frame 0 rim flare
    segs = 8
    verts = [(0.0, 0.0, 9.5)]  # crown apex (idx 0)
    for i in range(segs):
        a = 2.0 * math.pi * i / segs
        verts.append((6.0 * math.cos(a), 6.0 * math.sin(a), 3.0))
    for i in range(segs):
        a = 2.0 * math.pi * i / segs
        verts.append((7.2 * math.cos(a), 7.2 * math.sin(a), -2.5))
    tris = []
    for i in range(segs):
        j = (i + 1) % segs
        tris.append((0, 1 + i, 1 + j))
        tris.append((1 + i, 1 + segs + i, 1 + segs + j))
        tris.append((1 + i, 1 + segs + j, 1 + j))

    nv = len(verts)  # 17
    nt = len(tris)   # 24
    tri_bytes = b"".join(struct.pack("<3i", *t) for t in tris)
    st_bytes  = b"".join(struct.pack("<2f", (x + 8.0) / 16.0, (y + 8.0) / 16.0) for (x, y, _) in verts)
    xyz_bytes = b"".join(
        struct.pack("<3hH", int(round(x * 64.0)), int(round(y * 64.0)), int(round(z * 64.0)), 0)
        for (x, y, z) in verts
    )
    # 1 compressed delta frame: crown +2.0 Z -> (127 + 40 = 167), others neutral (127)
    comp_bytes = bytearray()
    for idx in range(nv):
        dz_b = 167 if idx == 0 else 127
        comp_bytes += struct.pack("<I", 127 | (127 << 8) | (dz_b << 16))
    base_map = struct.pack("<2h", 0, 0)       # 2 frames -> both use base frame 0
    comp_map = struct.pack("<2h", 0, -1)      # frame 0 applies comp frame 0 (+2.0 Z on apex!), frame 1 pure base

    ofs_tris = 124
    ofs_st   = ofs_tris + len(tri_bytes)
    ofs_xyz  = ofs_st + len(st_bytes)
    ofs_comp = ofs_xyz + len(xyz_bytes)
    ofs_bmap = ofs_comp + len(comp_bytes)
    ofs_cmap = ofs_bmap + len(base_map)
    s_end    = ofs_cmap + len(comp_map)
    shdr = struct.pack(
        "<i64s14i",
        7,
        b"h_helmet_shell".ljust(64, b"\x00"),
        0, 1, 1, 0, nv, nt,
        ofs_tris, 124, ofs_st, ofs_xyz, ofs_comp, ofs_bmap, ofs_cmap, s_end,
    )
    assert len(shdr) == 124
    surf_blob = shdr + tri_bytes + st_bytes + xyz_bytes + bytes(comp_bytes) + base_map + comp_map

    md3_frame = struct.pack("<6f3ff16s", -8, -8, -3, 8, 8, 12, 0, 0, 0, 12.0, b"idle".ljust(16, b"\x00"))
    assert len(md3_frame) == 56
    frames_blob = md3_frame * 2
    ofs_frames = 112
    ofs_surfs  = ofs_frames + len(frames_blob)
    ofs_end    = ofs_surfs + len(surf_blob)
    hdr = struct.pack(
        "<4si64s10i",
        b"IDPC", 2,
        b"models/players/infantry/helmet.mdc".ljust(64, b"\x00"),
        0, 2, 0, 1, 0,
        ofs_frames, ofs_frames, ofs_frames, ofs_surfs, ofs_end,
    )
    assert len(hdr) == 112
    (out_dir / "rtcw_helmet.mdc").write_bytes(hdr + frames_blob + surf_blob)

def gen_domain_heretic2_fm(out_dir: Path):
    def fm_sec(name: str, ver: int, payload: bytes) -> bytes:
        return struct.pack("<32s2i", name.encode("ascii").ljust(32, b"\x00"), ver, len(payload)) + payload

    # Mana crystal obelisk (6 vertices: bottom tip, 4 mid-belt corners, top apex; 8 triangles)
    pts = [
        (0.0, 0.0, -14.0),
        (-5.0, 0.0, 0.0),
        (0.0, -5.0, 0.0),
        (5.0, 0.0, 0.0),
        (0.0, 5.0, 0.0),
        (0.0, 0.0, 18.0),
    ]
    tris = [
        (0, 2, 1), (0, 3, 2), (0, 4, 3), (0, 1, 4),
        (5, 1, 2), (5, 2, 3), (5, 3, 4), (5, 4, 1),
    ]
    sx, sy, sz = 10.0 / 255.0, 10.0 / 255.0, 32.0 / 255.0
    tx, ty, tz = -5.0, -5.0, -14.0

    frame0 = bytearray(struct.pack("<3f3f16s", sx, sy, sz, tx, ty, tz, b"idle_spin".ljust(16, b"\x00")))
    for (x, y, z) in pts:
        bx = max(0, min(255, int(round((x - tx) / sx))))
        by = max(0, min(255, int(round((y - ty) / sy))))
        bz = max(0, min(255, int(round((z - tz) / sz))))
        frame0 += struct.pack("<4B", bx, by, bz, 0)

    hdr_pay = struct.pack("<10i", 128, 128, len(frame0), 1, len(pts), len(pts), len(tris), 0, 1, 1)
    skin_pay = b"!models/items/mana/skin.m8".ljust(64, b"\x00")
    st_pay = b"".join(struct.pack("<2h", 64, 64) for _ in pts)
    tri_pay = b"".join(struct.pack("<3h3h", a, b, c, a, b, c) for (a, b, c) in tris)

    blob = (
        fm_sec("header", 2, hdr_pay)
        + fm_sec("skin", 1, skin_pay)
        + fm_sec("st", 1, st_pay)
        + fm_sec("tris", 1, tri_pay)
        + fm_sec("frames", 1, bytes(frame0))
    )
    (out_dir / "heretic2_mana_crystal.fm").write_bytes(blob)

def gen_domain_valve_phy(out_dir: Path):
    def build_phy_solid(verts_m, tris):
        nt = len(tris)
        size_div_16 = 1 + nt
        c_point_offset = size_div_16 * 16
        bf_word = 0x3 | (size_div_16 << 8)
        ledge_hdr = struct.pack("<2iI2h", c_point_offset, 0, bf_word, nt, 0)
        tri_blob = b"".join(struct.pack("<4I", idx, a, b, c) for idx, (a, b, c) in enumerate(tris))
        pts_blob = b"".join(struct.pack("<4f", x, y, z, 0.0) for (x, y, z) in verts_m)
        ivps_body = ledge_hdr + tri_blob + pts_blob
        vphy_hdr = struct.pack("<4s2hi3fi", b"VPHY", 0, 0, 48 + len(ivps_body), 1.0, 1.0, 1.0, 0)
        assert len(vphy_hdr) == 28
        legacy_hdr = struct.pack("<3f3ff4i4s", 0, 0, 0, 1, 1, 1, 2.0, 0, 0, 0, 0, b"IVPS")
        assert len(legacy_hdr) == 48
        solid_payload = vphy_hdr + legacy_hdr + ivps_body
        return struct.pack("<i", len(solid_payload)) + solid_payload

    # 2 convex solids (supply crate base + sloped lid) in VPhysics meters + trailing keyvalues text
    box_v = [
        (-0.4, 0.0, -0.3), (0.4, 0.0, -0.3), (0.4, -0.5, -0.3), (-0.4, -0.5, -0.3),
        (-0.4, 0.0,  0.3), (0.4, 0.0,  0.3), (0.4, -0.5,  0.3), (-0.4, -0.5,  0.3),
    ]
    box_t = [
        (0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1), (1, 5, 6), (1, 6, 2),
        (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0),
    ]
    lid_v = [(-0.35, -0.5, -0.25), (0.35, -0.5, -0.25), (0.0, -0.5, 0.25), (0.0, -0.8, 0.0)]
    lid_t = [(0, 1, 2), (0, 1, 3), (1, 2, 3), (2, 0, 3)]

    phy_hdr = struct.pack("<4i", 16, 0, 2, 0x2007C0DE)
    kv_text = b'solid {\n"index" "0"\n"name" "crate_base"\n"mass" "45.0"\n}\n\x00'
    (out_dir / "hl2_supply_crate.phy").write_bytes(
        phy_hdr + build_phy_solid(box_v, box_t) + build_phy_solid(lid_v, lid_t) + kv_text
    )

def gen_domain_valve_vtx(out_dir: Path):
    # Shared hexagonal turret pod (8 shared vertices -> 12 triangles indexed ONLY in .dx90.vtx!)
    checksum = 0x5A17BEEF
    pts = [(0.0, 0.0, -10.0), (0.0, 0.0, 14.0)]
    for i in range(6):
        a = 2.0 * math.pi * i / 6
        pts.append((7.5 * math.cos(a), 7.5 * math.sin(a), 2.0))
    tris = []
    for i in range(6):
        j = (i + 1) % 6
        tris.append((0, 2 + j, 2 + i))
        tris.append((1, 2 + i, 2 + j))

    vvd_verts = b"".join(
        struct.pack("<3f4B3f3f2f", 1.0, 0.0, 0.0, 0, 0, 0, 1, x, y, z, *_norm3(x, y, z), 0.5, 0.5)
        for (x, y, z) in pts
    )
    vvd_hdr = struct.pack("<4s3i8i4i", b"IDSV", 4, checksum, 1, len(pts), 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0)
    (out_dir / "hl2_turret_pod.vvd").write_bytes(vvd_hdr + vvd_verts)

    # OptimizedModel v7 .dx90.vtx
    vtx_verts = b"".join(struct.pack("<4BH3B", 0, 0, 0, 1, vi, 0, 0, 0) for vi in range(len(pts)))
    vtx_idxs  = b"".join(struct.pack("<3H", a, b, c) for (a, b, c) in tris)
    sg_hdr    = struct.pack("<6iB", len(pts), 25, len(tris) * 3, 25 + len(vtx_verts), 0, 0, 0)
    mesh_hdr  = struct.pack("<2iB", 1, 9, 0)
    lod_hdr   = struct.pack("<2if", 1, 12, 0.0)
    mdl_hdr   = struct.pack("<2i", 1, 8)
    bp_hdr    = struct.pack("<2i", 1, 8)
    file_hdr  = struct.pack("<2i2H6i", 7, 16, 3, 3, 3, checksum, 1, 0, 1, 36)
    assert len(file_hdr) == 36
    (out_dir / "hl2_turret_pod.dx90.vtx").write_bytes(
        file_hdr + bp_hdr + mdl_hdr + lod_hdr + mesh_hdr + sg_hdr + vtx_verts + vtx_idxs
    )

def gen_domain_model3d_m3d(out_dir: Path):
    # Starfighter delta hull (6 float32 vertices + inline RGBA, uint8 indices, zlib-compressed)
    # flags: vc_s=2 (float32), vi_s=0 (u8), si_s=1 (u8), ci_s=3 (4B RGBA), ti_s=0, sk_s=0
    flags = 2 | (0 << 2) | (1 << 4) | (3 << 6)
    str_tab = b"starfighter\x00hull_alloy\x00"
    head_pay = struct.pack("<fI", 1.0, flags) + str_tab
    head_chunk = struct.pack("<4sI", b"HEAD", 8 + len(head_pay)) + head_pay

    pts = [
        (0.0,  0.0,  16.0),  # nose
        (-9.0, 0.0,  -8.0),  # left wingtip
        ( 9.0, 0.0,  -8.0),  # right wingtip
        (0.0,  3.2,  -4.0),  # dorsal canopy
        (0.0, -1.6,  -4.0),  # ventral keel
        (0.0,  0.5, -10.0),  # tail thruster
    ]
    vrts_pay = b"".join(struct.pack("<4fI", x, y, z, 1.0, 0xFFC08040) for (x, y, z) in pts)
    vrts_chunk = struct.pack("<4sI", b"VRTS", 8 + len(vrts_pay)) + vrts_pay

    tris = [
        (0, 3, 1), (0, 2, 3), (0, 1, 4), (0, 4, 2),
        (5, 1, 3), (5, 3, 2), (5, 4, 1), (5, 2, 4),
    ]
    mesh_pay = bytearray([0x00, 12])  # material switch -> "hull_alloy" @ str_tab[12]
    for (a, b, c) in tris:
        mesh_pay += bytes([0x30, a, b, c])
    mesh_chunk = struct.pack("<4sI", b"MESH", 8 + len(mesh_pay)) + bytes(mesh_pay)
    omd3_chunk = struct.pack("<4sI", b"OMD3", 8)

    comp = zlib.compress(head_chunk + vrts_chunk + mesh_chunk + omd3_chunk, 6)
    outer = struct.pack("<4sI", b"3DMO", 8 + len(comp)) + comp
    (out_dir / "raylib_starfighter.m3d").write_bytes(outer)

def gen_domain_imagine_tddd(out_dir: Path):
    def iff_be(tag: bytes, payload: bytes) -> bytes:
        pad = b"\x00" if (len(payload) & 1) else b""
        return struct.pack(">4sI", tag, len(payload)) + payload + pad

    fx = lambda v: int(round(v * 65536.0))
    # Square pyramid (5 points, 8 edges, 6 edge-referenced triangular faces)
    pts = [(-6.0, -6.0, 0.0), (6.0, -6.0, 0.0), (6.0, 6.0, 0.0), (-6.0, 6.0, 0.0), (0.0, 0.0, 14.0)]
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 0),  # base perimeter 0..3
        (0, 4), (1, 4), (2, 4), (3, 4),  # side ribs 4..7
        (0, 2),                          # base diagonal 8
    ]
    faces = [
        (0, 5, 4), (1, 6, 5), (2, 7, 6), (3, 4, 7),  # 4 side faces
        (0, 1, 8), (2, 3, 8),                        # 2 base faces
    ]

    posi = iff_be(b"POSI", struct.pack(">3i", fx(0.0), fx(0.0), fx(2.0)))
    name = iff_be(b"NAME", b"AmigaPyramid\x00")
    pnts = iff_be(b"PNTS", struct.pack(">H", len(pts)) + b"".join(struct.pack(">3i", fx(x), fx(y), fx(z)) for (x, y, z) in pts))
    edge = iff_be(b"EDGE", struct.pack(">H", len(edges)) + b"".join(struct.pack(">2H", a, b) for (a, b) in edges))
    face = iff_be(b"FACE", struct.pack(">H", len(faces)) + b"".join(struct.pack(">3H", e0, e1, e2) for (e0, e1, e2) in faces))
    desc = iff_be(b"DESC", name + posi + pnts + edge + face)
    tobj = iff_be(b"TOBJ", b"")
    obj  = iff_be(b"OBJ ", desc + tobj)
    form_body = b"TDDD" + obj
    (out_dir / "amiga_pyramid.tddd").write_bytes(struct.pack(">4sI", b"FORM", len(form_body)) + form_body)

def gen_domain_celestia_cmod(out_dir: Path):
    # Binary CMOD octagonal asteroid (10 vertices: 2 poles + 8 equatorial crags; 16 triangles)
    mat_block = struct.pack("<3H3fH", 1001, 0, 7, 0.55, 0.50, 0.46, 1002)
    vdesc = struct.pack("<6H", 1005, 0, 2, 3, 2, 1006)  # Position f3, Normal f3
    pts = [(0.0, -9.0, 0.0), (0.0, 9.5, 0.0)]
    for i in range(8):
        a = 2.0 * math.pi * i / 8
        r = 12.0 if (i % 2 == 0) else 9.5
        pts.append((r * math.cos(a), 0.5 * (-1 if i & 1 else 1), r * math.sin(a)))
    tris = []
    for i in range(8):
        j = (i + 1) % 8
        tris.append((0, 2 + j, 2 + i))
        tris.append((1, 2 + i, 2 + j))

    v_blob = b"".join(struct.pack("<6f", x, y, z, *_norm3(x, y, z)) for (x, y, z) in pts)
    i_blob = b"".join(struct.pack("<3I", a, b, c) for (a, b, c) in tris)
    mesh_block = (
        struct.pack("<H", 1003)
        + vdesc
        + struct.pack("<HI", 1007, len(pts))
        + v_blob
        + struct.pack("<H2I", 0, 0, len(tris) * 3)
        + i_blob
        + struct.pack("<H", 1004)
    )
    (out_dir / "celestia_asteroid.cmod").write_bytes(b"#celmodel_binary" + mat_block + mesh_block)

def gen_domain_softimage_xsi(out_dir: Path):
    xsi_txt = """xsi 0360txt 0032
SI_FileInfo {
    "ZeroEngine_ModSDK",
    "Explorer2_ModdingEE",
}
SI_Model MDL-hovertank_root {
    SI_Transform SRT-hovertank_root {
        1.0, 1.0, 1.0,
        0.0, 0.0, 0.0,
        0.0, 0.0, 0.0,
    }
    SI_Mesh MSH-chassis {
        SI_Shape SHP-chassis-ORG {
            2,
            "ORDERED",
            8,
            "POSITION",
            -6.0, 0.0, -9.0,
             6.0, 0.0, -9.0,
             6.0, 3.0, -9.0,
            -6.0, 3.0, -9.0,
            -5.0, 0.0,  9.0,
             5.0, 0.0,  9.0,
             5.0, 2.5,  9.0,
            -5.0, 2.5,  9.0,
            1,
            "NORMAL",
            0.0, 1.0, 0.0,
        }
        SI_PolygonList PL-chassis {
            6,
            "NORMAL",
            "mat_armor",
            24,
            4, 4, 4, 4, 4, 4,
            0, 1, 2, 3,
            4, 7, 6, 5,
            0, 4, 5, 1,
            1, 5, 6, 2,
            2, 6, 7, 3,
            3, 7, 4, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        }
    }
    SI_Model MDL-turret {
        SI_Transform SRT-turret {
            1.0, 1.0, 1.0,
            0.0, 0.0, 0.0,
            0.0, 3.0, 1.0,
        }
        SI_Mesh MSH-turret {
            SI_Shape SHP-turret-ORG {
                1,
                "ORDERED",
                4,
                "POSITION",
                -2.5, 0.0, -2.5,
                 2.5, 0.0, -2.5,
                 0.0, 0.0,  4.5,
                 0.0, 3.5,  0.0,
            }
            SI_TriangleList TL-turret {
                4,
                "",
                "mat_turret",
                0, 1, 2,
                0, 1, 3,
                1, 2, 3,
                2, 0, 3,
            }
        }
    }
}
"""
    (out_dir / "battlefront_hovertank.xsi").write_bytes(xsi_txt.encode("ascii"))

def gen_domain_descent3_oof(out_dir: Path):
    def oof_chunk(tag: bytes, payload: bytes) -> bytes:
        return struct.pack("<4si", tag, len(payload)) + payload

    def build_sobj_v2300(sub_num: int, parent: int, offset, name: bytes, verts, faces):
        fixed = struct.pack(
            "<2i3ff3f3ff2i3f",
            sub_num, parent,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 0.0,
            offset[0], offset[1], offset[2],
            12.0, 0, 0,
            0.0, 0.0, 0.0,
        )
        assert len(fixed) == 72
        v_xyz = b"".join(struct.pack("<3f", *v) for v in verts)
        v_nrm = b"".join(struct.pack("<3f", *_norm3(*v)) for v in verts)
        v_alp = b"".join(struct.pack("<f", 1.0) for _ in verts)
        f_blob = bytearray()
        for f_idxs in faces:
            f_blob += struct.pack("<3f2iH", 0.0, 1.0, 0.0, len(f_idxs), 1, 0)
            for vi in f_idxs:
                f_blob += struct.pack("<i2f", vi, 0.5, 0.5)
            f_blob += struct.pack("<2f", 1.0, 1.0)  # v>=2100 lightmap diff
        body = (
            fixed
            + name + b"\x00" + b"$gun\x00"
            + struct.pack("<3i", 0, 0, 0)
            + struct.pack("<i", len(verts)) + v_xyz + v_nrm + v_alp
            + struct.pack("<i", len(faces)) + bytes(f_blob)
        )
        return oof_chunk(b"SOBJ", body)

    # Pyro-GL fuselage (5 verts, 6 tris) + child left wing pod @ (-8, 0, -2) (4 verts, 4 tris)
    fuse_v = [(0.0, 0.0, 14.0), (-4.0, 0.0, -6.0), (4.0, 0.0, -6.0), (0.0, 3.0, -4.0), (0.0, -2.0, -4.0)]
    fuse_f = [(0, 3, 1), (0, 2, 3), (0, 1, 4), (0, 4, 2), (1, 3, 2, 4)]  # includes 1 quad face!
    pod_v  = [(-1.5, 0.0, 5.0), (-3.0, 0.0, -3.0), (0.0, 0.0, -3.0), (-1.5, 2.0, -1.0)]
    pod_f  = [(0, 1, 2), (0, 3, 1), (0, 2, 3), (1, 3, 2)]

    ohdr = oof_chunk(b"OHDR", struct.pack("<if6fi", 2, 18.0, -12, -3, -9, 5, 4, 14, 1))
    s0 = build_sobj_v2300(0, -1, (0.0, 0.0, 0.0), b"fuselage", fuse_v, fuse_f)
    s1 = build_sobj_v2300(1,  0, (-8.0, 0.0, -2.0), b"left_pod", pod_v, pod_f)
    (out_dir / "descent3_pyro_gl.oof").write_bytes(struct.pack("<4si", b"PSPO", 2300) + ohdr + s0 + s1)

def gen_domain_bohemia_p3d(out_dir: Path):
    # ArmA watchtower bunker (8 points, 5 quad walls/roof + 2 roof gable tris -> 12 tris)
    pts = [
        (-3.0, 0.0, -3.0), (3.0, 0.0, -3.0), (3.0, 0.0, 3.0), (-3.0, 0.0, 3.0),
        (-2.6, 4.5, -2.6), (2.6, 4.5, -2.6), (2.6, 4.5, 2.6), (-2.6, 4.5, 2.6),
        (0.0,  6.2,  0.0),
    ]
    quads = [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    tris  = [(4, 5, 8), (5, 6, 8), (6, 7, 8), (7, 4, 8)]

    pts_blob = b"".join(struct.pack("<3fI", x, y, z, 0) for (x, y, z) in pts)
    nrm_blob = struct.pack("<3f", 0.0, 1.0, 0.0)
    faces_blob = bytearray()
    for (a, b, c, d) in quads:
        vt = b"".join(struct.pack("<2i2f", vi, 0, 0.5, 0.5) for vi in (a, b, c, d))
        faces_blob += struct.pack("<I", 4) + vt + struct.pack("<I", 0) + b"ca\\structures\\wall_co.paa\x00\x00"
    for (a, b, c) in tris:
        vt = b"".join(struct.pack("<2i2f", vi, 0, 0.5, 0.5) for vi in (a, b, c, 0))
        faces_blob += struct.pack("<I", 3) + vt + struct.pack("<I", 0) + b"ca\\structures\\roof_co.paa\x00\x00"

    p3dm_hdr = struct.pack("<4s6I", b"P3DM", 28, 256, len(pts), 1, len(quads) + len(tris), 0)
    lod_res  = struct.pack("<f", 0.0)
    mlod_hdr = struct.pack("<4s2I", b"MLOD", 257, 1)
    (out_dir / "arma_watchtower.p3d").write_bytes(mlod_hdr + p3dm_hdr + pts_blob + nrm_blob + bytes(faces_blob) + lod_res)

def gen_domain_darkplaces_dpm(out_dir: Path):
    # 2-bone Nexuiz plasma rifle (grip root @ (0,0,0) + barrel child @ (16,0,2), 6 verts, 8 tris)
    bones = (
        struct.pack(">32siI", b"tag_grip".ljust(32, b"\x00"), -1, 0)
        + struct.pack(">32siI", b"tag_barrel".ljust(32, b"\x00"), 0, 0)
    )
    bone_poses = (
        struct.pack(">12f", 1,0,0,0.0,  0,1,0,0.0, 0,0,1,0.0)
        + struct.pack(">12f", 1,0,0,16.0, 0,1,0,0.0, 0,0,1,2.0)
    )
    # 6 vertices: 4 around grip (bone 0), 1 mid blended 0.5/0.5, 1 muzzle tip on bone 1
    v_specs = [
        [(0, 1.0, (-4.0, -2.0, 0.0))],
        [(0, 1.0, (-4.0,  2.0, 0.0))],
        [(0, 1.0, (-4.0,  0.0, 3.0))],
        [(0, 1.0, (-4.0,  0.0,-2.0))],
        [(0, 0.5, ( 2.0,  0.0, 1.0)), (1, 0.5, (0.0, 0.0, 1.0))],
        [(1, 1.0, (12.0,  0.0, 0.0))],  # world X = 16 + 12 = 28!
    ]
    verts_blob = bytearray()
    for wlist in v_specs:
        verts_blob += struct.pack(">I", len(wlist))
        for (bidx, inf, (lx, ly, lz)) in wlist:
            # DPM pre-multiplies local origin by influence!
            verts_blob += struct.pack(">3ff3fI", lx * inf, ly * inf, lz * inf, inf, 0.0, 0.0, 1.0, bidx)
    uv_blob = struct.pack(">2f", 0.5, 0.5) * len(v_specs)
    tris = [(0, 1, 2), (0, 3, 1), (0, 2, 5), (2, 1, 5), (1, 3, 5), (3, 0, 5), (0, 4, 2), (1, 2, 4)]
    idx_blob = b"".join(struct.pack(">3I", a, b, c) for (a, b, c) in tris)

    ofs_bones = 80
    ofs_frames = ofs_bones + len(bones)
    ofs_bposes = ofs_frames + 68
    ofs_meshes = ofs_bposes + len(bone_poses)
    ofs_verts  = ofs_meshes + 56
    ofs_uvs    = ofs_verts + len(verts_blob)
    ofs_idxs   = ofs_uvs + len(uv_blob)
    total_sz   = ofs_idxs + len(idx_blob)

    frame0 = struct.pack(">32s6f2fI", b"idle".ljust(32, b"\x00"), -5, -3, -3, 30, 3, 5, 30.0, 31.0, ofs_bposes)
    assert len(frame0) == 68
    mesh0 = struct.pack(">32s6I", b"models/weapons/nex".ljust(32, b"\x00"), len(v_specs), len(tris), ofs_verts, ofs_uvs, ofs_idxs, 0)
    assert len(mesh0) == 56
    hdr = struct.pack(
        ">16s2I6f2f6I",
        b"DARKPLACESMODEL\x00", 2, total_sz,
        -5, -3, -3, 30, 3, 5, 30.0, 31.0,
        2, 1, 1, ofs_bones, ofs_meshes, ofs_frames,
    )
    assert len(hdr) == 80
    (out_dir / "nexuiz_plasma.dpm").write_bytes(
        hdr + bones + frame0 + bone_poses + mesh0 + bytes(verts_blob) + uv_blob + idx_blob
    )

def gen_domain_iqe(out_dir: _DomainSampleRouter) -> None:
    txt = """# Inter-Quake Export
joint "root" -1
\tpq 0.0 0.0 0.0 0.0 0.0 0.0 1.0
joint "turret" 0
\tpq 0.0 0.0 1.2 0.0 0.0 0.0 1.0

mesh "hull"
\tmaterial "models/bot/hull_plate"
\tvp -1.0 -1.0 0.0
\tvp  1.0 -1.0 0.0
\tvp  1.0  1.0 0.0
\tvp -1.0  1.0 0.0
\tvp  0.0  0.0 1.6
\tfm 0 2 1
\tfm 0 3 2
\tfm 0 1 4
\tfm 1 2 4
\tfm 2 3 4
\tfm 3 0 4
"""
    (out_dir / "arena_bot.iqe").write_bytes(txt.encode("utf-8"))

def gen_domain_vta(out_dir: _DomainSampleRouter) -> None:
    txt = """version 1
nodes
  0 "ValveBiped.Bip01_Head1" -1
  1 "ValveBiped.Bip01_Jaw" 0
end
skeleton
  time 0 # basis
    0 0.0000 0.0000 64.0000 0.0000 0.0000 0.0000
    1 0.0000 2.5000 60.0000 0.0000 0.0000 0.0000
  time 1 # smile_open
    0 0.0000 0.0000 64.0000 0.0000 0.0000 0.0000
    1 0.0000 2.5000 58.5000 0.1500 0.0000 0.0000
end
vertexanimation
  time 0 # basis shape
    0 -2.0 3.0 62.0  0.0 1.0 0.0
    1  2.0 3.0 62.0  0.0 1.0 0.0
    2  0.0 3.5 65.0  0.0 1.0 0.0
    3 -2.0 3.0 62.0  0.0 1.0 0.0
    4  0.0 2.8 59.0  0.0 1.0 0.0
    5  2.0 3.0 62.0  0.0 1.0 0.0
  time 1 # smile_open
    0 -2.6 3.2 62.4  0.0 1.0 0.0
    1  2.6 3.2 62.4  0.0 1.0 0.0
    4  0.0 2.4 57.6  0.0 1.0 0.0
end
"""
    (out_dir / "hl2_face_flex.vta").write_bytes(txt.encode("utf-8"))

def gen_domain_bmd(out_dir: _DomainSampleRouter) -> None:
    (out_dir / "windwaker_crate.bmd").write_bytes(build_j3d_bmd(b"J3D2bmd3", 50.0))

def gen_domain_mdl0(out_dir: _DomainSampleRouter) -> None:
    (out_dir / "kart_itembox.mdl0").write_bytes(build_nw4r_mdl0(2.4))

def gen_domain_nsbmd(out_dir: _DomainSampleRouter) -> None:
    (out_dir / "ds_trophy.nsbmd").write_bytes(build_ds_nsbmd(6144))

def gen_domain_pie(out_dir: _DomainSampleRouter) -> None:
    txt = """PIE 3
TYPE 200
TEXTURE 0 page-14-droid-hubs.png 256 256
LEVELS 1
LEVEL 1
POINTS 6
\t-18.0 0.0 -24.0
\t 18.0 0.0 -24.0
\t 22.0 0.0  20.0
\t-22.0 0.0  20.0
\t  0.0 14.0 -6.0
\t  0.0 10.0  28.0
POLYGONS 7
\t200 4 0 3 2 1 0.0 0.0 0.0 1.0 1.0 1.0 1.0 0.0
\t200 3 0 1 4 0.0 0.0 1.0 0.0 0.5 1.0
\t200 3 1 2 4 0.0 0.0 1.0 0.0 0.5 1.0
\t200 3 2 5 4 0.0 0.0 1.0 0.0 0.5 1.0
\t200 3 2 3 5 0.0 0.0 1.0 0.0 0.5 1.0
\t200 3 3 0 4 0.0 0.0 1.0 0.0 0.5 1.0
\t200 3 3 4 5 0.0 0.0 1.0 0.0 0.5 1.0
CONNECTORS 1
\t0.0 14.0 4.0
"""
    (out_dir / "viper_droid.pie").write_bytes(txt.encode("utf-8"))

def gen_domain_s3o(out_dir: _DomainSampleRouter) -> None:
    (out_dir / "arm_flash_tank.s3o").write_bytes(build_spring_s3o(7.5))

def gen_domain_3do(out_dir: _DomainSampleRouter) -> None:
    txt = """# Jedi Knight 3DO model
SECTION: HEADER
3DO 2.1

SECTION: MODELRESOURCE
MATERIALS 1
0: imp_crate.mat

SECTION: GEOMETRYDEF
RADIUS 1.250000
INSERT OFFSET 0.000000 0.000000 0.000000
GEOSETS 1
GEOSET 0
MESHES 1
MESH 0
NAME crate_body
RADIUS 1.250000
GEOMETRYMODE 4
LIGHTINGMODE 3
TEXTUREMODE 1
VERTICES 5
0: -0.500000 -0.500000 0.000000 0.0
1:  0.500000 -0.500000 0.000000 0.0
2:  0.500000  0.500000 0.000000 0.0
3: -0.500000  0.500000 0.000000 0.0
4:  0.000000  0.000000 0.850000 0.0
TEXTURE VERTICES 4
0: 0.0 0.0
1: 1.0 0.0
2: 1.0 1.0
3: 0.0 1.0
FACES 5
0: 0 0x0000 4 3 1 0.0 4  0, 0  3, 3  2, 2  1, 1
1: 0 0x0000 4 3 1 0.0 3  0, 0  1, 1  4, 2
2: 0 0x0000 4 3 1 0.0 3  1, 0  2, 1  4, 2
3: 0 0x0000 4 3 1 0.0 3  2, 0  3, 1  4, 2
4: 0 0x0000 4 3 1 0.0 3  3, 0  0, 1  4, 2

SECTION: HIERARCHYDEF
HIERARCHY NODES 1
0: 0x0000 0x0001 0 -1 -1 -1 0  0.0 0.0 0.25  0.0 0.0 0.0  0.0 0.0 0.0  crate_body
"""
    (out_dir / "stormtrooper_crate.3do").write_bytes(txt.encode("utf-8"))

def gen_domain_t3d(out_dir: _DomainSampleRouter) -> None:
    txt = """Begin Map
Begin Actor Class=Brush Name=Brush1
    CsgOper=CSG_Add
    Location=(X=64.000000,Y=0.000000,Z=128.000000)
    Begin Brush Name=Model2
        Begin PolyList
            Begin Polygon Texture=BaseWall Flags=0
                Origin   -00128.000000,-00128.000000,-00128.000000
                Normal   +00000.000000,+00000.000000,-00001.000000
                Vertex   -00128.000000,-00128.000000,-00128.000000
                Vertex   -00128.000000,+00128.000000,-00128.000000
                Vertex   +00128.000000,+00128.000000,-00128.000000
                Vertex   +00128.000000,-00128.000000,-00128.000000
            End Polygon
            Begin Polygon Texture=BaseWall Flags=0
                Vertex   -00128.000000,-00128.000000,+00128.000000
                Vertex   +00128.000000,-00128.000000,+00128.000000
                Vertex   +00128.000000,+00128.000000,+00128.000000
                Vertex   -00128.000000,+00128.000000,+00128.000000
            End Polygon
            Begin Polygon Texture=TrimSteel Flags=0
                Vertex   -00128.000000,-00128.000000,-00128.000000
                Vertex   +00128.000000,-00128.000000,-00128.000000
                Vertex   +00128.000000,-00128.000000,+00128.000000
                Vertex   -00128.000000,-00128.000000,+00128.000000
            End Polygon
        End PolyList
    End Brush
End Actor
End Map
"""
    (out_dir / "ut99_room_brush.t3d").write_bytes(txt.encode("utf-8"))

def gen_domain_ogex(out_dir: _DomainSampleRouter) -> None:
    txt = """// OpenGEX 2.0 Mech Turret Sample
Metric (key = "up") { string { "z" } }
GeometryNode $node_base {
    Name { string { "TurretPod" } }
    ObjectRef { ref { $geo_pod } }
    Translation { float[3] { { 0.0, 0.0, 1.5 } } }
}
GeometryObject $geo_pod {
    Mesh (primitive = "triangles") {
        VertexArray (attrib = "position") {
            float[3] {
                { -1.2, -1.0, 0.0 },
                {  1.2, -1.0, 0.0 },
                {  1.2,  1.0, 0.0 },
                { -1.2,  1.0, 0.0 },
                {  0.0,  0.2, 1.8 }
            }
        }
        IndexArray {
            uint32[3] {
                { 0, 2, 1 },
                { 0, 3, 2 },
                { 0, 1, 4 },
                { 1, 2, 4 },
                { 2, 3, 4 },
                { 3, 0, 4 }
            }
        }
    }
}
"""
    (out_dir / "mech_turret.ogex").write_bytes(txt.encode("utf-8"))

def gen_domain_m3(out_dir: _DomainSampleRouter) -> None:
    (out_dir / "terran_beacon.m3").write_bytes(build_blizzard_m3(2.5))

def gen_domain_gox(out_dir: _DomainSampleRouter) -> None:
    # 2x2x2 cube + 1 crown voxel on top -> interior shared faces culled!
    coords = [(x, y, z) for x in range(2) for y in range(2) for z in range(2)] + [(0, 0, 2)]
    (out_dir / "voxel_knight.gox").write_bytes(build_goxel_gox(coords))

def gen_domain_binvox(out_dir: _DomainSampleRouter) -> None:
    # 4x4x4 grid with an L-shaped 4-voxel solid cluster
    solid = {(1, 1, 1), (2, 1, 1), (1, 2, 1), (1, 1, 2)}
    (out_dir / "stanford_bunny_grid.binvox").write_bytes(build_binvox(4, solid))


def _run_consolidated_domain_samples(td_root: Path) -> None:

    router = _DomainSampleRouter(td_root, {'combine_soldier.vvd': '3d', 'jedi_saber_hilt.glm': '3d', 'unreal_skith_a.3d': '3d', 'mocap_acrobat.htr': '3d', 'cmu_runner.asf': '3d', 'turbine_plc.smesh': '3d', 'crystal_chalice.pov': '3d', 'pixar_pavilion.rib': '3d', 'atrium_daylight.rad': '3d', 'mech_walker.qb': '3d', 'build_medkit.kvx': '3d', 'voxlap_turret.kv6': '3d', 'ra2_grizzly_tank.vxl': '3d', 'truespace_shuttle.cob': '3d', 'spd_geodesic.nff': '3d', 'catenoid_soapfilm.fe': '3d', 'nasa_nacelle.byu': '3d', 'cart3d_delta_wing.tri': '3d', 'irrlicht_temple.irrmesh': '3d', 'dreamcast_hovercraft.nj': '3d', 'fakk2_broadsword.skb': '3d', 'fakk2_hero_idle.ska': '3d', 'mohaa_garand_rifle.skd': '3d', 'mohaa_rifle_aim.skc': '3d', 'jedi_humanoid.gla': '3d', 'rtcw_helmet.mdc': '3d', 'heretic2_mana_crystal.fm': '3d', 'hl2_supply_crate.phy': '3d', 'hl2_turret_pod.vvd': '3d', 'hl2_turret_pod.dx90.vtx': '3d', 'raylib_starfighter.m3d': '3d', 'amiga_pyramid.tddd': '3d', 'celestia_asteroid.cmod': '3d', 'battlefront_hovertank.xsi': '3d', 'descent3_pyro_gl.oof': '3d', 'arma_watchtower.p3d': '3d', 'nexuiz_plasma.dpm': '3d', 'arena_bot.iqe': '3d', 'hl2_face_flex.vta': '3d', 'windwaker_crate.bmd': '3d', 'kart_itembox.mdl0': '3d', 'ds_trophy.nsbmd': '3d', 'viper_droid.pie': '3d', 'arm_flash_tank.s3o': '3d', 'stormtrooper_crate.3do': '3d', 'ut99_room_brush.t3d': '3d', 'mech_turret.ogex': '3d', 'terran_beacon.m3': '3d', 'voxel_knight.gox': '3d', 'stanford_bunny_grid.binvox': '3d'})

    for fn in [gen_domain_valve_vvd, gen_domain_raven_glm, gen_domain_unreal_3d, gen_domain_motion_htr, gen_domain_acclaim_asf, gen_domain_tetgen_smesh, gen_domain_povray_pov, gen_domain_renderman_rib, gen_domain_radiance_rad, gen_domain_qubicle_qb, gen_domain_build_kvx, gen_domain_voxlap_kv6, gen_domain_westwood_vxl, gen_domain_truespace_cob, gen_domain_neutral_nff, gen_domain_evolver_fe, gen_domain_movie_byu, gen_domain_cart3d_tri, gen_domain_irrlicht_irrmesh, gen_domain_sega_ninja_nj, gen_domain_ubertools_skb, gen_domain_ubertools_ska, gen_domain_mohaa_skd, gen_domain_mohaa_skc, gen_domain_raven_gla, gen_domain_rtcw_mdc, gen_domain_heretic2_fm, gen_domain_valve_phy, gen_domain_valve_vtx, gen_domain_model3d_m3d, gen_domain_imagine_tddd, gen_domain_celestia_cmod, gen_domain_softimage_xsi, gen_domain_descent3_oof, gen_domain_bohemia_p3d, gen_domain_darkplaces_dpm, gen_domain_iqe, gen_domain_vta, gen_domain_bmd, gen_domain_mdl0, gen_domain_nsbmd, gen_domain_pie, gen_domain_s3o, gen_domain_3do, gen_domain_t3d, gen_domain_ogex, gen_domain_m3, gen_domain_gox, gen_domain_binvox]:

        fn(router)


def main() -> None:
    repo_root = Path(__file__).resolve().parent.parent
    d3_dir = repo_root / "testdata" / "3d"
    cad_dir = repo_root / "testdata" / "cad"
    d3_dir.mkdir(parents=True, exist_ok=True)
    cad_dir.mkdir(parents=True, exist_ok=True)

    generated: list[Path] = []
    generated.extend(gen_autodesk_dwf_samples(cad_dir))
    generated.extend(gen_additive_cli_samples(cad_dir))
    generated.extend(gen_upgraded_cad_containers(cad_dir))
    generated.extend(gen_fem_cfd_samples(d3_dir))
    generated.extend(gen_voxel_game_samples(d3_dir))
    generated.extend(gen_upgraded_dcc_game_samples(d3_dir))

    print(f"Generated {len(generated)} deterministic 3D & CAD sample files:")
    for p in generated:
        rel = p.relative_to(repo_root).as_posix()
        print(f"  {rel} ({p.stat().st_size} bytes)")
    if len(__import__('sys').argv) == 1:
        _run_consolidated_domain_samples(Path(__file__).resolve().parent.parent / 'testdata')


if __name__ == "__main__":
    main()
