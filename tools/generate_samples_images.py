#!/usr/bin/env python3
"""
generate_samples_images.py — Deterministic Workstation, Retro & Engine Image sample generator for Explorer2.

Generates genuine, specification-compliant binary sample files into `testdata/images/` (or a custom
`--out-dir` path) covering all 12 workstation/scientific/retro/engine image formats:
  1. Silicon Graphics / Alias|Wavefront PIX (.pix, .als)
  2. Utah Raster Toolkit RLE (.rle)
  3. ImageMagick Machine Independent File Format (.miff)
  4. Khoros Visualization Image File Format (.viff, .xv)
  5. Amiga Workbench OS3.5+ GlowIcons & NewIcons (.info)
  6. Atari ST DEGAS & DEGAS Elite RLE (.pi1, .pc1)
  7. Commodore 64 KoalaPainter Multicolor (.koa)
  8. Sinclair ZX Spectrum Screen Dump (.scr)
  9. Original Xbox NV2A Swizzled Packed Resource (.xpr)
 10. Godot Engine 3 ("GDST" .stex) & Godot Engine 4 ("GST2" .ctex)
 11. id Tech 5 / DOOM 3 BFG Binary Image (.bimage)
 12. X11 Window Dump v7 (.xwd)

Plus upgraded real-specification container samples for:
  - Clip Studio Paint (.clip) with real SQLite3 B-tree `CHNKSQLi` + multi-page overflow `CanvasPreview.ImageData`
  - Procreate (.procreate) with real Apple `bplist00` `NSKeyedArchiver` `Document.archive` + `bvx-` LZFSE/LZVN tiles
  - Valve Source 2 (.vtex_c) with `VTEX_EXTRA_DATA_COMPRESSED_MIP_SIZE` + raw LZ4-compressed mip0
  - Nintendo Wii U Café (.bflim) with big-endian `FLIM` footer + GX2 `tileMode=2` micro-tiled surface
  - Adobe Swatch Exchange (.ase) with RGB, CMYK, CIE LAB, and Grayscale swatch blocks
"""

from __future__ import annotations

import argparse
import io
import os
import plistlib
import sqlite3
import struct
import tempfile
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


def make_png_rgba(w: int, h: int, rgba_bytes: bytes, compress_level: int = 6) -> bytes:
    """Builds a standard 8-bit RGBA PNG file using only Python stdlib."""
    assert len(rgba_bytes) == w * h * 4

    def png_chunk(tag: bytes, body: bytes) -> bytes:
        crc = zlib.crc32(tag + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", crc)

    raw_scanlines = bytearray()
    stride = w * 4
    for y in range(h):
        raw_scanlines.append(0)  # Filter type 0 (None)
        raw_scanlines.extend(rgba_bytes[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw_scanlines), compress_level)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", idat)
        + png_chunk(b"IEND", b"")
    )


def packbits_encode(raw: bytes) -> bytes:
    """Standard Apple / Electronic Arts / DEGAS Elite PackBits encoder."""
    out = bytearray()
    n = len(raw)
    i = 0
    while i < n:
        run_end = i + 1
        while run_end < n and run_end - i < 128 and raw[run_end] == raw[i]:
            run_end += 1
        run_len = run_end - i
        if run_len >= 3:
            out.append((257 - run_len) & 0xFF)
            out.append(raw[i])
            i = run_end
        else:
            lit_start = i
            while i < n and i - lit_start < 128:
                if i + 2 < n and raw[i] == raw[i + 1] == raw[i + 2]:
                    break
                i += 1
            lit_len = i - lit_start
            out.append(lit_len - 1)
            out.extend(raw[lit_start:i])
    return bytes(out)


# ==============================================================================
# 1. Silicon Graphics / Alias|Wavefront PIX (.pix, .als)
# ==============================================================================
def gen_alias_pix_samples(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    hdr24 = struct.pack(">5H", w, h, 0, 0, 24)
    pkts24 = bytearray()
    palette_bgr = [
        (32, 64, 220),   # Red-ish
        (60, 200, 40),   # Green-ish
        (230, 110, 30),  # Blue-ish
        (40, 210, 245),  # Gold
    ]
    for y in range(h):
        for q in range(4):
            b, g, r = palette_bgr[(q + (y // 4)) & 3]
            pkts24.extend((4, b, g, r))

    p_pix = out_dir / "alias_wavefront.pix"
    p_pix.write_bytes(hdr24 + bytes(pkts24))

    # 8-bit matte .als
    hdr8 = struct.pack(">5H", w, h, 0, 0, 8)
    pkts8 = bytearray()
    for y in range(h):
        lum = (y * 17) & 0xFF
        pkts8.extend((w, lum))
    p_als = out_dir / "alias_matte.als"
    p_als.write_bytes(hdr8 + bytes(pkts8))
    return [p_pix, p_als]


# ==============================================================================
# 2. Utah Raster Toolkit RLE (.rle)
# ==============================================================================
def gen_utah_rle_sample(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    # Header: magic=0xCC52 LE, xpos=0, ypos=0, w=16, h=16, flags=0x04 (H_ALPHA), ncolors=3, pixelbits=8, ncmap=0, cmaplen=0
    hdr = struct.pack("<HHHHHBBBBB", 0xCC52, 0, 0, w, h, 0x04, 3, 8, 0, 0)
    bg = bytes([20, 40, 80])  # ncolors=3 (odd -> 15 + 3 = 18 even offset!)

    ops = bytearray()
    for y in range(h):
        # Channel 0 (R): RunData (op 6) of 16 pixels
        ops.extend((2, 0))  # SetColor(0)
        ops.extend((6, w - 1, (y * 15 + 30) & 0xFF, 0))
        # Channel 1 (G): ByteData (op 5) of 16 literal pixels
        ops.extend((2, 1))  # SetColor(1)
        ops.extend((5, w - 1))
        ops.extend(((x * 16) & 0xFF) for x in range(w))
        # Channel 2 (B): SkipPixels(4) + RunData(12) using 0x40 long-form opcode to test 16-bit datum!
        ops.extend((2, 2))  # SetColor(2)
        ops.extend((0x40 | 3, 0))
        ops.extend(struct.pack("<H", 4))
        ops.extend((6, 11, 210, 0))
        # Alpha channel (255 -> -1): solid 255
        ops.extend((2, 255))
        ops.extend((6, w - 1, 255, 0))
        if y + 1 < h:
            ops.extend((1, 1))  # SkipLines(1)
    ops.extend((7, 0))  # EOFOp

    p_rle = out_dir / "utah_toolkit.rle"
    p_rle.write_bytes(hdr + bg + bytes(ops))
    return [p_rle]


# ==============================================================================
# 3. ImageMagick MIFF (.miff)
# ==============================================================================
def gen_imagemagick_miff_sample(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    header = (
        b"id=ImageMagick  version=1.0\n"
        b"{Deterministic Explorer2 MIFF Zip sample}\n"
        b"class=DirectClass  columns=16  rows=16  depth=8\n"
        b"colorspace=sRGB  matte=True  compression=Zip\n"
        b":\x1a"
    )
    payload = bytearray()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row.extend(((x * 17) & 0xFF, (y * 17) & 0xFF, ((x ^ y) * 17) & 0xFF, 255))
        zrow = zlib.compress(bytes(row), 6)
        payload.extend(struct.pack(">I", len(zrow)))
        payload.extend(zrow)

    p_miff = out_dir / "imagemagick_zip.miff"
    p_miff.write_bytes(header + bytes(payload))
    return [p_miff]


# ==============================================================================
# 4. Khoros Visualization Image File Format (.viff, .xv)
# ==============================================================================
def gen_khoros_viff_samples(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    # Scientific float32 3-band little-endian VIFF
    hdr = bytearray(1024)
    hdr[0] = 0xAB  # XV_FILE_MAGIC_NUM
    hdr[1] = 0x01  # XV_FILE_TYPE_XVIFF
    hdr[2] = 0x01  # release
    hdr[3] = 0x03  # version
    hdr[4] = 0x08  # VFF_DEP_NSORDER (x86 little-endian)
    comment = b"Explorer2 Khoros VIFF float32 multi-band sample"
    hdr[8 : 8 + len(comment)] = comment
    struct.pack_into("<II", hdr, 520, w, h)        # row_size, col_size
    struct.pack_into("<IIII", hdr, 548, 1, 0, 1, 3) # loc=IMPLICIT, locDim=0, numImages=1, numBands=3
    struct.pack_into("<II", hdr, 564, 5, 0)         # VFF_TYP_FLOAT=5, VFF_ESC_NONE=0
    struct.pack_into("<IIII", hdr, 572, 0, 0, 0, 0) # no LUT map

    bands = bytearray()
    for band in range(3):
        for y in range(h):
            for x in range(w):
                if band == 0:
                    val = x / float(w - 1)
                elif band == 1:
                    val = y / float(h - 1)
                else:
                    val = 0.5
                bands.extend(struct.pack("<f", val))

    p_viff = out_dir / "khoros_scientific.viff"
    p_viff.write_bytes(bytes(hdr) + bytes(bands))

    # Colormapped big-endian IEEE .xv
    hdr_xv = bytearray(1024)
    hdr_xv[0:5] = bytes([0xAB, 0x01, 0x01, 0x03, 0x04])  # 0x04 = IEEE big-endian
    struct.pack_into(">II", hdr_xv, 520, w, h)
    struct.pack_into(">IIII", hdr_xv, 548, 1, 0, 1, 1)
    struct.pack_into(">II", hdr_xv, 564, 1, 0)            # VFF_TYP_1_BYTE=1
    struct.pack_into(">IIII", hdr_xv, 572, 1, 1, 3, 16)   # EACHBAND, 1_BYTE LUT, 3 channels, 16 entries
    lut = bytearray(3 * 16)
    for i in range(16):
        lut[0 * 16 + i] = (i * 17) & 0xFF
        lut[1 * 16 + i] = ((15 - i) * 17) & 0xFF
        lut[2 * 16 + i] = ((i * 9 + 40) & 0xFF)
    idx_plane = bytes(((x + y) & 0x0F) for y in range(h) for x in range(w))
    p_xv = out_dir / "khoros_colormapped.xv"
    p_xv.write_bytes(bytes(hdr_xv) + bytes(lut) + idx_plane)
    return [p_viff, p_xv]


# ==============================================================================
# 5. Amiga Workbench Icon (.info — Classic Planar + OS3.5 GlowIcons FORM..ICON)
# ==============================================================================
def gen_amiga_icon_sample(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    # 78-byte Classic Amiga DiskObject header:
    #   +0: 0xE310 (magic), +2: 0x0001 (version)
    #   +4..47: Gadget (44 bytes; gadgetFlags at +16, gadgetRender at +22, selectRender at +26)
    #   +48: do_Type(4=WBTOOL), +49: pad, +50: defaultTool(0), +54: toolTypes(0),
    #   +58: currentX, +62: currentY, +66: drawerData(0), +70: toolWindow(0), +74: stackSize(4096)
    disk_obj = bytearray(78)
    struct.pack_into(">HH", disk_obj, 0, 0xE310, 0x0001)
    struct.pack_into(">HH", disk_obj, 4 + 8, w, h)
    struct.pack_into(">I", disk_obj, 4 + 18, 1)  # non-zero GadgetRender pointer
    disk_obj[48] = 4
    struct.pack_into(">I", disk_obj, 74, 4096)

    # 20-byte Classic planar Image struct (16x16, 2 planes = 64 bytes)
    img_hdr = struct.pack(">hhhhhBBII", 0, 0, w, h, 2, 0x03, 0x00, 1, 0)
    planar_bits = bytes([0xAA, 0x55] * 32)

    # Appended AmigaOS 3.5+ GlowIcons IFF FORM....ICON container
    face_payload = bytes([w - 1, h - 1, 0x01, 0x11, 0x00, 0x03])  # 16x16, transparent=1
    face_chunk = b"FACE" + struct.pack(">I", len(face_payload)) + face_payload

    palette = bytes([
        0,   0,   0,     # 0: transparent
        255, 190, 40,    # 1: Amiga Glow gold
        40,  130, 240,   # 2: Workbench blue
        245, 245, 250,   # 3: Highlight white
    ])
    indices = bytearray()
    for y in range(h):
        for x in range(w):
            if x == 0 or y == 0 or x == w - 1 or y == h - 1:
                indices.append(0)
            elif 4 <= x <= 11 and 4 <= y <= 11:
                indices.append(1 if (x + y) % 3 else 3)
            else:
                indices.append(2)
    z_idx = zlib.compress(bytes(indices), 6)
    # IMAG header: transIdx=0, numCols-1=3, flags=0x03 (trans|palette), imgComp=2(zlib), palComp=0(raw), bpp=8, imgSz, palSz
    imag_hdr = struct.pack(">BBBBBBHH", 0, 3, 0x03, 2, 0, 8, len(z_idx), len(palette))
    imag_body = imag_hdr + z_idx + palette
    imag_pad = b"\x00" if (len(imag_body) & 1) else b""
    imag_chunk = b"IMAG" + struct.pack(">I", len(imag_body)) + imag_body + imag_pad

    form_body = b"ICON" + face_chunk + imag_chunk
    form_iff = b"FORM" + struct.pack(">I", len(form_body)) + form_body

    p_info = out_dir / "amiga_glowicon.info"
    p_info.write_bytes(bytes(disk_obj) + img_hdr + planar_bits + form_iff)
    return [p_info]


# ==============================================================================
# 6. Atari ST DEGAS (.pi1) & DEGAS Elite Compressed (.pc1)
# ==============================================================================
def gen_atari_degas_samples(out_dir: Path) -> list[Path]:
    # 16-color Atari ST 9-bit palette (0x0RGB with components 0..7)
    st_pal = [
        0x000, 0x700, 0x070, 0x007,
        0x770, 0x707, 0x077, 0x777,
        0x333, 0x730, 0x370, 0x037,
        0x505, 0x055, 0x752, 0x257,
    ]
    pal_bytes = b"".join(struct.pack(">H", c) for c in st_pal)

    w, h, planes = 320, 200, 4
    # Build planar scanlines (40 bytes per plane per scanline)
    plane_rows: list[list[bytearray]] = [[bytearray(w // 8) for _ in range(planes)] for _ in range(h)]
    for y in range(h):
        for x in range(w):
            idx = ((x // 20) + (y // 25)) & 0x0F
            byte_i = x >> 3
            bit_m = 0x80 >> (x & 7)
            for p in range(planes):
                if (idx >> p) & 1:
                    plane_rows[y][p][byte_i] |= bit_m

    # Uncompressed .pi1: 16-pixel groups of 4 interleaved big-endian words
    vram = bytearray()
    for y in range(h):
        for g in range(w // 16):
            for p in range(planes):
                vram.extend(plane_rows[y][p][g * 2 : g * 2 + 2])

    p_pi1 = out_dir / "atari_degas_low.pi1"
    p_pi1.write_bytes(struct.pack(">H", 0x0000) + pal_bytes + bytes(vram))

    # PackBits-compressed DEGAS Elite .pc1 (resWord = 0x8000)
    rle_stream = bytearray()
    for y in range(h):
        for p in range(planes):
            rle_stream.extend(packbits_encode(bytes(plane_rows[y][p])))
    p_pc1 = out_dir / "atari_degas_elite.pc1"
    p_pc1.write_bytes(struct.pack(">H", 0x8000) + pal_bytes + bytes(rle_stream))
    return [p_pi1, p_pc1]


# ==============================================================================
# 7. Commodore 64 KoalaPainter Multicolor (.koa — exact 10,003 bytes)
# ==============================================================================
def gen_c64_koala_sample(out_dir: Path) -> list[Path]:
    load_addr = struct.pack("<H", 0x6000)
    bitmap = bytearray(8000)
    screen_ram = bytearray(1000)
    color_ram = bytearray(1000)
    bg_color = bytes([0x06])  # C64 VIC-II Blue background

    for cy in range(25):
        for cx in range(40):
            cell = cy * 40 + cx
            c1 = ((cx + cy) % 14) + 1
            c2 = ((cx * 2 + cy) % 14) + 1
            c3 = ((cx + cy * 3) % 14) + 1
            screen_ram[cell] = ((c1 & 0x0F) << 4) | (c2 & 0x0F)
            color_ram[cell] = c3 & 0x0F
            for ry in range(8):
                # Four 2-bit multicolor crumbs [00, 01, 10, 11] = 0x1B rotated by row
                crumbs = [((ry + k) & 3) for k in range(4)]
                bitmap[cell * 8 + ry] = (crumbs[0] << 6) | (crumbs[1] << 4) | (crumbs[2] << 2) | crumbs[3]

    blob = load_addr + bytes(bitmap) + bytes(screen_ram) + bytes(color_ram) + bg_color
    assert len(blob) == 10003
    p_koa = out_dir / "c64_koala_multicolor.koa"
    p_koa.write_bytes(blob)
    return [p_koa]


# ==============================================================================
# 8. Sinclair ZX Spectrum Screen Dump (.scr — exact 6,912 bytes)
# ==============================================================================
def gen_zx_spectrum_scr_sample(out_dir: Path) -> list[Path]:
    vram = bytearray(6912)
    for y in range(192):
        for bx in range(32):
            off = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | bx
            vram[off] = 0xF0 if ((y // 4 + bx) & 1) == 0 else 0x3C

    for cy in range(24):
        for cx in range(32):
            ink = ((cx + cy) % 7) + 1
            paper = (cx // 8) & 0x07
            if ink == paper:
                ink = (ink + 1) & 0x07
            bright = 0x40 if ((cx ^ cy) & 1) else 0x00
            vram[6144 + cy * 32 + cx] = bright | (paper << 3) | ink

    p_scr = out_dir / "zx_spectrum_loading.scr"
    p_scr.write_bytes(bytes(vram))
    return [p_scr]


# ==============================================================================
# 9. Original Xbox NV2A Swizzled XPR0 Texture (.xpr)
# ==============================================================================
def _xbox_nv2a_swizzle_index(x: int, y: int, w: int, h: int) -> int:
    off = 0
    bit = 1
    sx, sy = x, y
    mw, mh = w >> 1, h >> 1
    while mw or mh:
        if mw:
            if sx & 1:
                off |= bit
            sx >>= 1
            bit <<= 1
            mw >>= 1
        if mh:
            if sy & 1:
                off |= bit
            sy >>= 1
            bit <<= 1
            mh >>= 1
    return off


def gen_xbox_xpr_sample(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    hdr_size = 32  # 12B XPR0 header + 20B D3DTexture descriptor
    tex_bytes = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            swz = _xbox_nv2a_swizzle_index(x, y, w, h)
            b = (x * 16) & 0xFF
            g = (y * 16) & 0xFF
            r = 220 if (x < 8 and y < 8) else 50
            tex_bytes[swz * 4 : swz * 4 + 4] = bytes([b, g, r, 255])

    total_size = hdr_size + len(tex_bytes)
    # D3DTexture descriptor:
    #   Common = 0x00040001 (type 4 = Texture, refcount 1)
    #   Data   = 0 (offset relative to hdr_size)
    #   Lock   = 0
    #   Format = (dims=2 << 4) | (gpuFmt=0x06 A8R8G8B8 << 8) | (mips=1 << 16) | (uLog2=4 << 20) | (vLog2=4 << 24)
    dw_format = (2 << 4) | (0x06 << 8) | (1 << 16) | (4 << 20) | (4 << 24)
    xpr_hdr = (
        b"XPR0"
        + struct.pack("<II", total_size, hdr_size)
        + struct.pack("<5I", 0x00040001, 0, 0, dw_format, 0)
    )
    assert len(xpr_hdr) == hdr_size
    p_xpr = out_dir / "xbox_nv2a_swizzled.xpr"
    p_xpr.write_bytes(xpr_hdr + bytes(tex_bytes))
    return [p_xpr]


# ==============================================================================
# 10. Godot 3 StreamTexture (.stex) & Godot 4 CompressedTexture2D (.ctex)
# ==============================================================================
def gen_godot_texture_samples(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    rgba = bytearray()
    for y in range(h):
        for x in range(w):
            rgba.extend((71, 140, 191, 255) if ((x // 4 + y // 4) & 1) == 0 else (50, 56, 67, 255))

    # Godot 3 "GDST" uncompressed FORMAT_RGBA8 (5)
    gdst_hdr = b"GDST" + struct.pack("<HHHHII", w, w, h, h, 0, 5)
    p_stex = out_dir / "godot3_stream.stex"
    p_stex.write_bytes(gdst_hdr + bytes(rgba))

    # Godot 4 "GST2" lossless embedded PNG chunk (dataFormat = 1, ver = 1)
    png_blob = make_png_rgba(w, h, bytes(rgba))
    gst2_hdr = (
        b"GST2"
        + struct.pack("<5I", 1, w, h, 0, 0)
        + struct.pack("<3I", 0, 0, 0)
        + struct.pack("<IHHI", 1, w, h, 0)
        + struct.pack("<I", 5)  # FORMAT_RGBA8
        + struct.pack("<I", len(png_blob))
    )
    p_ctex = out_dir / "godot4_compressed.ctex"
    p_ctex.write_bytes(gst2_hdr + png_blob)
    return [p_stex, p_ctex]


# ==============================================================================
# 11. id Tech 5 / DOOM 3 BFG Binary Image (.bimage — CFM_YCOCG_DXT5)
# ==============================================================================
def _pack_rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _encode_solid_dxt5_block(r: int, g: int, b: int, a: int) -> bytes:
    # Alpha block: a0=a, a1=a, 48 bits of index 0
    alpha_blk = bytes([a, a, 0, 0, 0, 0, 0, 0])
    c565 = _pack_rgb565(r, g, b)
    color_blk = struct.pack("<HHI", c565, c565, 0)
    return alpha_blk + color_blk


def gen_idtech_bimage_sample(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    # Encode an authentic CFM_YCOCG_DXT5 surface!
    # In id Tech 5 YCoCg-DXT5: Co -> R, Cg -> G, scale=1 -> B=0, Y -> Alpha
    # Target RGB = (200, 120, 40) => Y=120, Co=80 -> stored R=208, Cg=0 -> stored G=128, B=0, A=120
    blk_orange = _encode_solid_dxt5_block(208, 128, 0, 120)
    # Target RGB = (40, 160, 200) => Y=140, Co=-80 -> stored R=48, Cg=20 -> stored G=148, B=0, A=140
    blk_cyan = _encode_solid_dxt5_block(48, 148, 0, 140)

    dxt5_data = bytearray()
    for by in range(h // 4):
        for bx in range(w // 4):
            dxt5_data.extend(blk_orange if ((bx + by) & 1) == 0 else blk_cyan)

    # Big-endian BIM v10 header (36 bytes + 20-byte mip0 header = 56 bytes):
    #   [0..7] timestamp, [8..11] "BIM\x0a", [12..15] TT_2D(0), [16..19] FMT_DXT5(8),
    #   [20..23] CFM_YCOCG_DXT5(2), [24..27] w, [28..31] h, [32..35] numLevels(1)
    #   Mip 0 header (20 bytes BE): level=0, destZ=0, width=w, height=h, dataBytes=len(dxt5_data)
    bim_hdr = (
        struct.pack(">Q", 0x0123456789ABCDEF)
        + b"BIM\x0a"
        + struct.pack(">6I", 0, 8, 2, w, h, 1)
        + struct.pack(">5I", 0, 0, w, h, len(dxt5_data))
    )
    p_bim = out_dir / "doom3_bfg_ycocg.bimage"
    p_bim.write_bytes(bim_hdr + bytes(dxt5_data))
    return [p_bim]


# ==============================================================================
# 12. X11 Window Dump v7 (.xwd)
# ==============================================================================
def gen_xwd_sample(out_dir: Path) -> list[Path]:
    w, h = 16, 16
    win_name = b"Explorer2_X11_XWD\x00"
    hdr_size = 100 + len(win_name)
    # 25 big-endian uint32 fields (XWDFileHeader v7, TrueColor 24-bit in 32-bpp ZPixmap)
    xwd_hdr = struct.pack(
        ">25I",
        hdr_size,      # header_size
        7,             # file_version == XWD_FILE_VERSION (7)
        2,             # pixmap_format == ZPixmap (2)
        24,            # pixmap_depth
        w,             # pixmap_width
        h,             # pixmap_height
        0,             # xoffset
        1,             # byte_order == MSBFirst (1)
        32,            # bitmap_unit
        1,             # bitmap_bit_order == MSBFirst
        32,            # bitmap_pad
        32,            # bits_per_pixel
        w * 4,         # bytes_per_line
        4,             # visual_class == TrueColor (4)
        0x00FF0000,    # red_mask
        0x0000FF00,    # green_mask
        0x000000FF,    # blue_mask
        8,             # bits_per_rgb
        256,           # colormap_entries
        0,             # ncolors
        w,             # window_width
        h,             # window_height
        0,             # window_x
        0,             # window_y
        0,             # window_bdrwidth
    ) + win_name

    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            r = (x * 16 + 15) & 0xFF
            g = (y * 16 + 15) & 0xFF
            b = 180
            pixels.extend(struct.pack(">I", (r << 16) | (g << 8) | b))

    p_xwd = out_dir / "x11_window_dump.xwd"
    p_xwd.write_bytes(xwd_hdr + bytes(pixels))
    return [p_xwd]


# ==============================================================================
# 13. Upgraded Real-Spec Samples (.clip, .procreate, .vtex_c, .bflim, .ase)
# ==============================================================================
def gen_clip_studio_sample(out_dir: Path) -> list[Path]:
    """Builds a real CSFCHUNK .clip container with a genuine SQLite3 CHNKSQLi B-tree + overflow chain."""
    w, h = 24, 24
    # Pseudo-random-looking deterministic pattern so PNG exceeds 1 SQLite 512-byte page -> tests multi-page overflow!
    rgba = bytearray()
    for y in range(h):
        for x in range(w):
            r = ((x * 73 + y * 29 + (x * y * 13)) ^ 0x5A) & 0xFF
            g = ((x * 41 + y * 97 + 19) ^ 0xA5) & 0xFF
            b = ((x * 113 + y * 53 + 7) ^ 0x3C) & 0xFF
            rgba.extend((r, g, b, 255))
    # Store uncompressed zlib level 0 so PNG is ~2.4 KB (> 4x 512-byte SQLite pages -> guaranteed multi-page overflow!)
    png_blob = make_png_rgba(w, h, bytes(rgba), compress_level=0)

    with tempfile.NamedTemporaryFile(suffix=".sqlite", delete=False) as tf:
        tmp_db = tf.name
    try:
        conn = sqlite3.connect(tmp_db)
        cur = conn.cursor()
        cur.execute("PRAGMA page_size = 512;")
        cur.execute("PRAGMA journal_mode = OFF;")
        cur.execute("CREATE TABLE Canvas (MainId INTEGER PRIMARY KEY, CanvasWidth REAL, CanvasHeight REAL, CanvasUnit INTEGER);")
        cur.execute("INSERT INTO Canvas VALUES (1, 24.0, 24.0, 0);")
        cur.execute("CREATE TABLE Layer (_PW_ID INTEGER PRIMARY KEY, LayerName TEXT, LayerOpacity INTEGER, LayerVisibility INTEGER);")
        cur.execute("INSERT INTO Layer VALUES (1, 'Ink Linework', 255, 1);")
        cur.execute("INSERT INTO Layer VALUES (2, 'Color Wash', 200, 1);")
        cur.execute("CREATE TABLE CanvasPreview (_PW_ID INTEGER PRIMARY KEY, ImageData BLOB, ImageWidth INTEGER, ImageHeight INTEGER);")
        cur.execute("INSERT INTO CanvasPreview VALUES (1, ?, ?, ?);", (sqlite3.Binary(png_blob), w, h))
        conn.commit()
        conn.close()
        sqlite_bytes = Path(tmp_db).read_bytes()
    finally:
        if os.path.exists(tmp_db):
            os.remove(tmp_db)

    head_data = struct.pack(">4I", 100, w, h, 0) + b"\x00" * 24
    chnk_head = b"CHNKHead" + struct.pack(">Q", len(head_data)) + head_data
    chnk_sqli = b"CHNKSQLi" + struct.pack(">Q", len(sqlite_bytes)) + sqlite_bytes
    chnk_foot = b"CHNKFoot" + struct.pack(">Q", 0)
    total_body = chnk_head + chnk_sqli + chnk_foot
    csf_hdr = b"CSFCHUNK" + struct.pack(">QQ", 24 + len(total_body), 24)

    p_clip = out_dir / "clip_studio_sqlite_preview.clip"
    p_clip.write_bytes(csf_hdr + total_body)
    return [p_clip]


def _encode_lzvn_bvx_stream(raw: bytes) -> bytes:
    """Encodes `raw` into a genuine Apple LZFSE/LZVN `bvx-` ... `bvx$` stream using LRG_L / SML_L + EOS(0x06)."""
    payload = bytearray()
    pos = 0
    n = len(raw)
    while pos < n:
        rem = n - pos
        if rem >= 16:
            take = min(rem, 271)  # 0xE0 LRG_L encodes L = byte + 16 (16..271)
            payload.append(0xE0)
            payload.append(take - 16)
            payload.extend(raw[pos : pos + take])
            pos += take
        else:
            payload.append(0xE0 | rem)  # 0xE1..0xEF SML_L encodes L = 1..15
            payload.extend(raw[pos : pos + rem])
            pos += rem
    payload.append(0x06)  # LZVN EOS opcode (+ 7 zero padding bytes per Apple LZVN convention)
    payload.extend(b"\x00" * 7)
    return b"bvx-" + struct.pack("<II", len(raw), len(payload)) + bytes(payload) + b"bvx$"


def gen_procreate_sample(out_dir: Path) -> list[Path]:
    """Builds a real Procreate 5+ archive with `bplist00` NSKeyedArchiver `Document.archive` + LZFSE `bvx-` tiles."""
    w, h, tile_sz = 16, 16, 16
    vis_uuid = "11111111-2222-3333-4444-555555555555"
    hid_uuid = "99999999-8888-7777-6666-555555555555"
    cmp_uuid = "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE"

    U = plistlib.UID
    # NSKeyedArchiver object table ($objects)
    objects = [
        "$null",  # 0
        {         # 1: root SilicaDocument
            "$class": U(10),
            "size": U(2),
            "tileSize": tile_sz,
            "orientation": 1,
            "flippedHorizontally": False,
            "flippedVertically": False,
            "backgroundHidden": False,
            "backgroundColor": U(3),
            "layers": U(4),
            "composite": U(8),
        },
        "{16, 16}",                                    # 2: CGSize string
        struct.pack("<4f", 0.1, 0.15, 0.25, 1.0),      # 3: RGBA float32 background color
        {                                              # 4: NSMutableArray of layers (top-to-bottom)
            "$class": U(11),
            "NS.objects": [U(5), U(6)],
        },
        {                                              # 5: Visible top layer
            "$class": U(12),
            "UUID": U(7),
            "name": U(13),
            "hidden": False,
            "opacity": 1.0,
            "extendedBlend": 0,
        },
        {                                              # 6: Hidden layer (must NOT be rendered!)
            "$class": U(12),
            "UUID": U(9),
            "name": U(14),
            "hidden": True,
            "opacity": 1.0,
            "extendedBlend": 0,
        },
        vis_uuid,                                      # 7
        {                                              # 8: Separate composite SilicaLayer (must be EXCLUDED!)
            "$class": U(12),
            "UUID": U(15),
            "name": U(16),
            "hidden": False,
            "opacity": 1.0,
        },
        hid_uuid,                                      # 9
        {"$classname": "SilicaDocument", "$classes": ["SilicaDocument", "NSObject"]},  # 10
        {"$classname": "NSMutableArray", "$classes": ["NSMutableArray", "NSArray", "NSObject"]},  # 11
        {"$classname": "SilicaLayer", "$classes": ["SilicaLayer", "NSObject"]},  # 12
        "Paint Layer",                                 # 13
        "Hidden Draft",                                # 14
        cmp_uuid,                                      # 15
        "Composite Layer",                             # 16
    ]
    archive_dict = {
        "$version": 100000,
        "$archiver": "NSKeyedArchiver",
        "$top": {"root": U(1)},
        "$objects": objects,
    }
    doc_bplist = plistlib.dumps(archive_dict, fmt=plistlib.FMT_BINARY)

    # Visible layer tile: crisp emerald green square in center, transparent border showing dark-blue canvas bg
    vis_tile = bytearray(tile_sz * tile_sz * 4)
    for y in range(tile_sz):
        for x in range(tile_sz):
            if 3 <= x <= 12 and 3 <= y <= 12:
                vis_tile[(y * tile_sz + x) * 4 : (y * tile_sz + x + 1) * 4] = bytes([40, 220, 120, 255])

    # Hidden & composite tiles filled with bright magenta so tests verify they are ignored/excluded!
    magenta_tile = bytes([255, 0, 255, 255] * (tile_sz * tile_sz))

    zip_bytes = write_zip_deterministic(
        [
            ("Document.archive", doc_bplist),
            (f"{vis_uuid}/0~0.chunk", _encode_lzvn_bvx_stream(bytes(vis_tile))),
            (f"{hid_uuid}/0~0.chunk", _encode_lzvn_bvx_stream(magenta_tile)),
            (f"{cmp_uuid}/0~0.chunk", _encode_lzvn_bvx_stream(magenta_tile)),
        ],
        compress=False,
    )
    p_pro = out_dir / "procreate_bplist_lzfse.procreate"
    p_pro.write_bytes(zip_bytes)
    return [p_pro]


def _lz4_encode_literals_only(raw: bytes) -> bytes:
    """Encodes a valid raw LZ4 block (literal sequence) for deterministic container testing."""
    n = len(raw)
    out = bytearray()
    if n < 15:
        out.append(n << 4)
    else:
        out.append(15 << 4)
        rem = n - 15
        while rem >= 255:
            out.append(255)
            rem -= 255
        out.append(rem)
    out.extend(raw)
    return bytes(out)


def gen_source2_vtex_sample(out_dir: Path) -> list[Path]:
    """Builds a Valve Source 2 .vtex_c v12 resource with VTEX_EXTRA_DATA_COMPRESSED_MIP_SIZE + LZ4 mip0."""
    w, h = 8, 8
    raw_rgba = bytearray()
    for y in range(h):
        for x in range(w):
            raw_rgba.extend((230, 120, 40, 255) if ((x + y) & 1) == 0 else (40, 150, 240, 255))
    lz4_mip0 = _lz4_encode_literals_only(bytes(raw_rgba))

    # Layout:
    #   [0..15]  ResourceHeader (fileSize, hdrVer=12, dirOffset=8 -> abs 16, dirCount=1)
    #   [16..27] BlockEntry ("DATA", relOff=4 -> abs 28, blockSize=68)
    #   [28..67] VTex_t (40 bytes: ver=1, flags=0, refl=(0,0,0,0), w=8, h=8, depth=1, fmt=4 RGBA8888, mips=1,
    #                    picmip=0, extraOffset=8 -> abs (28+32)+8 = 68, extraCount=1)
    #   [68..79] VTexExtraData entry (type=4 COMPRESSED_MIP_SIZE, relOff=8 -> abs (68+4)+8 = 80, size=16)
    #   [80..95] CompressedMipTable (compressed=1, codec=1 LZ4, mipCount=1, sz0=len(lz4_mip0))
    #   [96..]   LZ4-compressed mip0 stream!
    vtex_t = (
        struct.pack("<HH4fHHHBB", 1, 0, 0.5, 0.5, 0.5, 1.0, w, h, 1, 4, 1)
        + struct.pack("<I", 0)
        + struct.pack("<iI", 8, 1)
    )
    assert len(vtex_t) == 40
    extra_entry = struct.pack("<IiI", 4, 8, 16)
    comp_tbl = struct.pack("<IiIi", 1, 1, 1, len(lz4_mip0))
    data_blk = vtex_t + extra_entry + comp_tbl
    assert len(data_blk) == 68

    total_len = 28 + len(data_blk) + len(lz4_mip0)
    res_hdr = struct.pack("<IHHI", total_len, 12, 0, 8) + struct.pack("<I", 1)
    blk_dir = b"DATA" + struct.pack("<iI", 8, len(data_blk))
    blob = res_hdr + blk_dir + data_blk + lz4_mip0
    assert len(blob) == total_len

    p_vtex = out_dir / "source2_lz4_mip0.vtex_c"
    p_vtex.write_bytes(blob)
    return [p_vtex]


def _gx2_micro_tile_index_32bpp(x: int, y: int) -> int:
    x0, x1, x2 = x & 1, (x >> 1) & 1, (x >> 2) & 1
    y0, y1, y2 = y & 1, (y >> 1) & 1, (y >> 2) & 1
    return x0 | (x1 << 1) | (y0 << 2) | (x2 << 3) | (y1 << 4) | (y2 << 5)


def gen_wiiu_bflim_sample(out_dir: Path) -> list[Path]:
    """Builds a Nintendo Wii U Café big-endian .bflim with GX2 `tileMode=2` (1D_TILED_THIN1) swizzling."""
    w, h = 16, 16  # 16 pixels * 4 Bpp = 64B = exact GX2 1D micro-tile pitch alignment!
    swizzled = bytearray(w * h * 4)
    tiles_per_row = w // 8
    for y in range(h):
        for x in range(w):
            tx, ty = x >> 3, y >> 3
            idx = _gx2_micro_tile_index_32bpp(x & 7, y & 7)
            dst_off = ((ty * tiles_per_row + tx) * 64 + idx) * 4
            r = 230 if x < 8 else 35
            g = 60 if y < 8 else 210
            b = 240 if ((x ^ y) & 4) else 50
            swizzled[dst_off : dst_off + 4] = bytes([r, g, b, 255])

    total_sz = len(swizzled) + 0x28
    # Big-endian Cafe FLIM header (0x14 bytes) + imag block (0x14 bytes):
    #   BOM at +4: [0xFF, 0xFE] => RdU16LE == 0xFEFF? Wait: in NintendoWiiUBflim.h:
    #     bomLE = RdU16LE(flim + 4); be = (bomLE == 0xFFFE) => bytes [0xFE, 0xFF] = big-endian BOM 0xFEFF!
    flim_hdr = (
        b"FLIM"
        + bytes([0xFE, 0xFF])
        + struct.pack(">HIIHH", 0x14, 0x02020000, total_sz, 1, 0)
    )
    # imag block: "imag" + size(0x10) + width + height + align(64) + flimFmt(0x09=RGBA8) + swzTile(2=1D_TILED_THIN1) + dataLen
    imag_blk = b"imag" + struct.pack(">IHHHBB", 0x10, w, h, 64, 0x09, 0x02) + struct.pack(">I", len(swizzled))
    blob = bytes(swizzled) + flim_hdr + imag_blk
    assert len(blob) == total_sz

    p_bflim = out_dir / "wiiu_gx2_swizzled.bflim"
    p_bflim.write_bytes(blob)
    return [p_bflim]


def gen_adobe_ase_sample(out_dir: Path) -> list[Path]:
    """Builds an Adobe Swatch Exchange ('ASEF' .ase) file exercising RGB, CMYK, LAB, and Gray swatch blocks."""
    def make_color_block(name: str, model4: bytes, values_be: bytes, color_type: int = 2) -> bytes:
        utf16_name = (name + "\x00").encode("utf-16-be")
        char_len = len(name) + 1
        body = struct.pack(">H", char_len) + utf16_name + model4 + values_be + struct.pack(">H", color_type)
        return struct.pack(">HI", 0x0001, len(body)) + body

    blocks = [
        make_color_block("Crimson RGB", b"RGB ", struct.pack(">3f", 0.88, 0.16, 0.24)),
        make_color_block("Process Cyan CMYK", b"CMYK", struct.pack(">4f", 0.85, 0.10, 0.05, 0.02)),
        make_color_block("Ochre CIE LAB", b"LAB ", struct.pack(">3f", 0.72, 18.0, 54.0)),
        make_color_block("Neutral Slate Gray", b"Gray", struct.pack(">f", 0.45)),
    ]
    ase_blob = b"ASEF" + struct.pack(">HHI", 1, 0, len(blocks)) + b"".join(blocks)
    p_ase = out_dir / "adobe_swatch_exchange.ase"
    p_ase.write_bytes(ase_blob)
    return [p_ase]


def gen_avif_sample(out_dir: Path) -> list[Path]:
    """Builds a 100% spec-compliant AOMedia AV1 v1.0.0 lossless Screen-Content Palette + Auxiliary Alpha .avif."""
    class _MsacEnc:
        def __init__(self) -> None:
            self.low = 0
            self.rng = 32768
            self.total_shifts = 0

        def encode_symbol(self, s: int, cdf: list[int], n: int) -> None:
            u = self.rng if s == 0 else ((((self.rng >> 8) * ((32768 - cdf[s - 1]) >> 6)) >> 1) + 4 * (n - s))
            v = (((self.rng >> 8) * ((32768 - cdf[s]) >> 6)) >> 1) + 4 * (n - 1 - s)
            self.low += v
            self.rng = u - v
            d = 15 - (self.rng.bit_length() - 1)
            self.low <<= d
            self.rng <<= d
            self.total_shifts += d

        def encode_literal(self, val: int, nbits: int) -> None:
            for i in range(nbits - 1, -1, -1):
                self.encode_symbol((val >> i) & 1, [16384, 32768, 0], 2)

        def finish_tile(self) -> bytes:
            total_bits = 15 + self.total_shifts
            inv_val = self.low + (self.rng >> 1)
            raw_bits = ((1 << total_bits) - 1) ^ inv_val
            out_bits = (raw_bits << 1) | 1
            n_out = total_bits + 1
            rem = (8 - (n_out % 8)) % 8
            out_bits <<= rem
            return out_bits.to_bytes((n_out + rem) // 8, "big")

    def _encode_cmap2(enc: _MsacEnc, cmap: list[list[int]], cdfs: list[list[int]]) -> None:
        enc.encode_literal(cmap[0][0], 1)
        ctx_map = {2: 0, 5: 4, 6: 3, 7: 2, 8: 1, 9: 0, 10: 1}
        for diag in range(1, 15):
            for c in range(min(diag, 7), max(diag - 7, 0) - 1, -1):
                r = diag - c
                sc = [0, 0]
                od = [0, 1]
                if c > 0: sc[cmap[r][c - 1]] += 2
                if r > 0 and c > 0: sc[cmap[r - 1][c - 1]] += 1
                if r > 0: sc[cmap[r - 1][c]] += 2
                if sc[1] > sc[0]:
                    sc[0], sc[1] = sc[1], sc[0]
                    od[0], od[1] = od[1], od[0]
                hv = sc[0] + sc[1] * 2
                enc.encode_symbol(od.index(cmap[r][c]), cdfs[ctx_map.get(hv, 0)], 2)

    pal_y_idx_2 = [[p, 32768, 0] for p in [28710, 16384, 10553, 27036, 31603]]
    pal_uv_idx_2 = [[p, 32768, 0] for p in [29089, 16384, 8713, 29257, 31610]]

    enc_a = _MsacEnc()
    enc_a.encode_symbol(0, [19132, 25510, 30392, 32768, 0], 4)
    enc_a.encode_symbol(1, [31671, 32768, 0], 2)
    enc_a.encode_symbol(0, [15588, 17027, 19338, 20218, 20682, 21110, 21825, 23244, 24189, 28165, 29093, 30466, 32768, 0], 13)
    enc_a.encode_symbol(1, [31676, 32768, 0], 2)
    enc_a.encode_symbol(0, [7952, 13000, 18149, 21478, 25527, 29241, 32768, 0], 7)
    enc_a.encode_literal(128, 8)
    enc_a.encode_literal(2, 2)
    enc_a.encode_literal(255 - 128 - 1, 7)
    _encode_cmap2(enc_a, [[1 if x < 4 else 0 for x in range(8)] for _ in range(8)], pal_y_idx_2)
    tile_a = enc_a.finish_tile()

    enc_c = _MsacEnc()
    enc_c.encode_symbol(0, [19132, 25510, 30392, 32768, 0], 4)
    enc_c.encode_symbol(1, [31671, 32768, 0], 2)
    enc_c.encode_symbol(0, [15588, 17027, 19338, 20218, 20682, 21110, 21825, 23244, 24189, 28165, 29093, 30466, 32768, 0], 13)
    enc_c.encode_symbol(0, [22631, 24152, 25378, 25661, 25986, 26520, 27055, 27923, 28244, 30059, 30941, 31961, 32768, 0], 13)
    enc_c.encode_symbol(1, [31676, 32768, 0], 2)
    enc_c.encode_symbol(0, [7952, 13000, 18149, 21478, 25527, 29241, 32768, 0], 7)
    enc_c.encode_literal(120, 8)
    enc_c.encode_literal(1, 2)
    enc_c.encode_literal(180 - 120 - 1, 6)
    enc_c.encode_symbol(1, [21488, 32768, 0], 2)
    enc_c.encode_symbol(0, [8713, 19979, 27128, 29609, 31331, 32272, 32768, 0], 7)
    enc_c.encode_literal(20, 8)
    enc_c.encode_literal(3, 2)
    enc_c.encode_literal(200 - 20, 8)
    enc_c.encode_literal(0, 1)
    enc_c.encode_literal(250, 8)
    enc_c.encode_literal(20, 8)
    cmap_c = [[0 if x < 4 else 1 for x in range(8)] for _ in range(8)]
    _encode_cmap2(enc_c, cmap_c, pal_y_idx_2)
    _encode_cmap2(enc_c, cmap_c, pal_uv_idx_2)
    tile_c = enc_c.finish_tile()

    def _pack_bits(bit_str: str) -> bytes:
        s = bit_str.replace(" ", "")
        while len(s) % 8:
            s += "0"
        return int(s, 2).to_bytes(len(s) // 8, "big")

    def _obu(obu_type: int, payload: bytes) -> bytes:
        return bytes([(obu_type << 3) | 0x02, len(payload)]) + payload

    seq_a = _pack_bits("000 1 1 00000 0010 0010 111 111 0 0 0 0 0 0 0 1 0 1 0 1")
    seq_c = _pack_bits("001 1 1 00000 0010 0010 111 111 0 0 0 0 0 0 0 1 00000001 00001101 00000000 0 0 1")
    item_a = _obu(1, seq_a) + _obu(6, bytes([0xC4, 0x00, 0x00]) + tile_a)
    item_c = _obu(1, seq_c) + _obu(6, bytes([0xC4, 0x00, 0x00]) + tile_c)

    def _box(t4: bytes, body: bytes) -> bytes:
        return struct.pack(">I4s", 8 + len(body), t4) + body

    def _fbox(t4: bytes, ver: int, flags: int, body: bytes) -> bytes:
        return _box(t4, struct.pack(">I", (ver << 24) | flags) + body)

    ftyp = _box(b"ftyp", b"avif\x00\x00\x00\x00avifmif1miaf")
    hdlr = _fbox(b"hdlr", 0, 0, b"\x00\x00\x00\x00pict\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00")
    pitm = _fbox(b"pitm", 0, 0, struct.pack(">H", 1))
    iinf = _fbox(b"iinf", 0, 0, struct.pack(">H", 2)
                 + _fbox(b"infe", 2, 0, struct.pack(">HH4s", 1, 0, b"av01") + b"Color\x00")
                 + _fbox(b"infe", 2, 0, struct.pack(">HH4s", 2, 0, b"av01") + b"Alpha\x00"))
    iref = _fbox(b"iref", 0, 0, _box(b"auxl", struct.pack(">HHH", 2, 1, 1)))
    ipco = _box(b"ipco",
                _fbox(b"ispe", 0, 0, struct.pack(">II", 8, 8))
                + _fbox(b"pixi", 0, 0, bytes([3, 8, 8, 8]))
                + _box(b"av1C", bytes([0x81, 0x20, 0x00, 0x00]) + _obu(1, seq_c))
                + _box(b"colr", b"nclx" + struct.pack(">HHH", 1, 13, 0) + bytes([0x80]))
                + _fbox(b"pixi", 0, 0, bytes([1, 8]))
                + _box(b"av1C", bytes([0x81, 0x00, 0x0C, 0x00]) + _obu(1, seq_a))
                + _fbox(b"auxC", 0, 0, b"urn:mpeg:mpegB:cicp:systems:auxiliary:alpha\x00"))
    ipma = _fbox(b"ipma", 0, 0, struct.pack(">I", 2)
                 + struct.pack(">HB", 1, 4) + bytes([1, 2, 0x83, 4])
                 + struct.pack(">HB", 2, 4) + bytes([1, 5, 0x86, 7]))
    iprp = _box(b"iprp", ipco + ipma)

    def _iloc(off_c: int, off_a: int) -> bytes:
        return _fbox(b"iloc", 0, 0, bytes([0x44, 0x00]) + struct.pack(">H", 2)
                     + struct.pack(">HHHII", 1, 0, 1, off_c, len(item_c))
                     + struct.pack(">HHHII", 2, 0, 1, off_a, len(item_a)))

    meta0 = _fbox(b"meta", 0, 0, hdlr + pitm + _iloc(0, 0) + iinf + iref + iprp)
    mdat_off = len(ftyp) + len(meta0) + 8
    meta = _fbox(b"meta", 0, 0, hdlr + pitm + _iloc(mdat_off, mdat_off + len(item_c)) + iinf + iref + iprp)
    p_avif = out_dir / "hdr_sunset_alpha.avif"
    p_avif.write_bytes(ftyp + meta + _box(b"mdat", item_c + item_a))
    return [p_avif]


def generate_all(out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    generated: list[Path] = []
    generated.extend(gen_alias_pix_samples(out_dir))
    generated.extend(gen_utah_rle_sample(out_dir))
    generated.extend(gen_imagemagick_miff_sample(out_dir))
    generated.extend(gen_khoros_viff_samples(out_dir))
    generated.extend(gen_amiga_icon_sample(out_dir))
    generated.extend(gen_atari_degas_samples(out_dir))
    generated.extend(gen_c64_koala_sample(out_dir))
    generated.extend(gen_zx_spectrum_scr_sample(out_dir))
    generated.extend(gen_xbox_xpr_sample(out_dir))
    generated.extend(gen_godot_texture_samples(out_dir))
    generated.extend(gen_idtech_bimage_sample(out_dir))
    generated.extend(gen_xwd_sample(out_dir))
    generated.extend(gen_clip_studio_sample(out_dir))
    generated.extend(gen_procreate_sample(out_dir))
    generated.extend(gen_source2_vtex_sample(out_dir))
    generated.extend(gen_wiiu_bflim_sample(out_dir))
    generated.extend(gen_adobe_ase_sample(out_dir))
    generated.extend(gen_avif_sample(out_dir))
    return generated



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


def gen_domain_ps2_tm2(out_dir: Path):
    w, h = 16, 16
    # Build logical 256-entry RGBA32 palette (with PS2 GS 0x80 = 100% opaque alpha)
    logical_pal = []
    for i in range(256):
        r = (i * 17) & 0xFF
        g = (i * 43) & 0xFF
        b = (255 - i) & 0xFF
        logical_pal.append((r, g, b, 0x80))
    # Entry 10 (in CSM1 swapped bank 8..15 <-> 16..23!) = pure red (255, 0, 0, 0x80)
    logical_pal[10] = (255, 0, 0, 0x80)
    logical_pal[18] = (0, 255, 0, 0x80)

    # Encode physical CSM1-swizzled CLUT
    csm1_clut = bytearray(256 * 4)
    for i in range(256):
        sub = i & 0x18
        raw_i = (i ^ 0x18) if (sub == 0x08 or sub == 0x10) else i
        csm1_clut[raw_i * 4 : raw_i * 4 + 4] = bytes(logical_pal[i])

    # Pixel indices: top half uses index 10 (must decode to pure red after CSM1 unswizzle!), bottom half gradient
    pixels = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            pixels[y * w + x] = 10 if (y < 8 and x < 8) else ((x * 8 + y * 5) & 0xFF)

    file_hdr = struct.pack("<4sBBH8x", b"TIM2", 4, 0, 1)
    img_sz = len(pixels)
    clut_sz = len(csm1_clut)
    hdr_sz = 48
    total_sz = hdr_sz + img_sz + clut_sz
    # ClutType = 0x03 (RGBA32, bit 6 = 0 -> CSM1!), ImageType = 0x05 (IDTEX8)
    pic_hdr = struct.pack(
        "<IIIHHBBBBHHQQII",
        total_sz, clut_sz, img_sz, hdr_sz, 256,
        0, 1, 0x03, 0x05, w, h,
        0, 0, 0, 0,
    )
    assert len(pic_hdr) == 48
    (out_dir / "ps2_hud_icon.tm2").write_bytes(file_hdr + pic_hdr + bytes(pixels) + bytes(csm1_clut))

def gen_domain_nintendo_tpl(out_dir: Path):
    # 8x8 GX_TF_CMPR (0x0E): 1 macro-tile = four 4x4 big-endian S3TC sub-blocks (32 bytes)
    # Sub-block 0 (top-left):     Red   (0xF800) > Blue  (0x001F), rowBits = 0x00 (all c0 = Red)
    # Sub-block 1 (top-right):    Green (0x07E0) > Blue  (0x001F), rowBits = 0x55 (all c1 = Blue)
    # Sub-block 2 (bottom-left):  White (0xFFFF) > Black (0x0000), rowBits = 0xAA (all c2 = 5/8 grey)
    # Sub-block 3 (bottom-right): Yellow(0xFFE0) > Cyan  (0x07FF), rowBits = 0x1B (0,1,2,3 gradient)
    cmpr_tile = (
        struct.pack(">HH4B", 0xF800, 0x001F, 0x00, 0x00, 0x00, 0x00)
        + struct.pack(">HH4B", 0x07E0, 0x001F, 0x55, 0x55, 0x55, 0x55)
        + struct.pack(">HH4B", 0xFFFF, 0x0000, 0xAA, 0xAA, 0xAA, 0xAA)
        + struct.pack(">HH4B", 0xFFE0, 0x07FF, 0x1B, 0x1B, 0x1B, 0x1B)
    )
    assert len(cmpr_tile) == 32

    # Layout: 12B file hdr + 8B img table = 20B; imgHdr @ 0x20 (36B -> ends @ 0x44); pixels @ 0x60 (32B aligned)
    tpl_hdr = struct.pack(">III", 0x0020AF30, 1, 0x0C) + struct.pack(">II", 0x20, 0)
    tpl_hdr = tpl_hdr.ljust(0x20, b"\x00")
    img_hdr = struct.pack(
        ">HHIIIIIIfBBBB",
        8, 8, 0x0E, 0x60,
        0, 0, 1, 1, 0.0, 0, 0, 0, 0,
    )
    assert len(img_hdr) == 36
    blob = (tpl_hdr + img_hdr).ljust(0x60, b"\x00") + cmpr_tile
    (out_dir / "wii_banner_cmpr.tpl").write_bytes(blob)

def gen_domain_nintendo_bti(out_dir: Path):
    # 8x8 GX_TF_RGBA8 (0x06): four 4x4 dual-cacheline (64B) tiles = 256 bytes
    tiles = bytearray()
    for ty in range(2):
        for tx in range(2):
            ar_half = bytearray()
            gb_half = bytearray()
            for dy in range(4):
                for dx in range(4):
                    x, y = tx * 4 + dx, ty * 4 + dy
                    r = 250 if (tx == 0 and ty == 0) else (x * 28)
                    g = 220 if (tx == 1 and ty == 0) else (y * 28)
                    b = 240 if (ty == 1) else 40
                    a = 255
                    ar_half += bytes((a, r))
                    gb_half += bytes((g, b))
            tiles += ar_half + gb_half
    assert len(tiles) == 256

    bti_hdr = struct.pack(
        ">BBHHBBBBHIBBBBBBbbBBhI",
        0x06, 0x02, 8, 8,       # fmt=RGBA8, alpha=2, w=8, h=8
        0, 0, 0, 0, 0, 0,       # wrapS/T=CLAMP, indexTex=0, tlutFmt=0, numColors=0, palOff=0
        0, 0, 0, 0, 1, 1,       # mipEn=0, edgeLod=0, bias=0, aniso=0, minF=1, magF=1
        0, 0, 1, 0, 0, 0x20,    # minLod=0, maxLod=0, mipCnt=1, unk=0, lodBias=0, imgOff=0x20
    )
    assert len(bti_hdr) == 32
    (out_dir / "windwaker_rupee.bti").write_bytes(bti_hdr + bytes(tiles))

def gen_domain_psp_gim(out_dir: Path):
    def gim_surf_chunk(blk_id: int, fmt: int, order: int, w: int, h: int, bpp: int, pix_bytes: bytes) -> bytes:
        # 48B GimImageHeader + 16B offsets array = 64B (0x40) before pixels
        img_hdr = struct.pack(
            "<HHHHHHHHHHIIII",
            0x30, 0, fmt, order, w, h, bpp, 16, 8, 2,
            0, 0x30, 0x40, 0x40 + len(pix_bytes),
        ).ljust(0x30, b"\x00")
        ofs_tbl = struct.pack("<IIII", 0x40, 0, 0, 0)
        body = img_hdr + ofs_tbl + pix_bytes
        tot = 16 + len(body)
        return struct.pack("<HHIII", blk_id, 0, tot, tot, 16) + body

    # 32x8 INDEX8 (stride=32, 2 PSP 16x8 blocks: left 16x8 = index 1 Red, right 16x8 = index 2 Cyan!)
    swizzled = bytes([1] * 128 + [2] * 128)
    pal_rgba = bytearray(256 * 4)
    pal_rgba[1 * 4 : 2 * 4] = bytes((255, 20, 40, 255))
    pal_rgba[2 * 4 : 3 * 4] = bytes((0, 220, 255, 255))

    img_chk = gim_surf_chunk(0x0004, 5, 1, 32, 8, 8, swizzled)
    pal_chk = gim_surf_chunk(0x0005, 3, 0, 256, 1, 32, bytes(pal_rgba))
    pic_body = img_chk + pal_chk
    pic_chk = struct.pack("<HHIII", 0x0003, 0, 16 + len(pic_body), 16 + len(pic_body), 16) + pic_body
    root_chk = struct.pack("<HHIII", 0x0002, 0, 16 + len(pic_chk), 16 + len(pic_chk), 16) + pic_chk
    magic = b"MIG.00.1PSP\x00\x00\x00\x00\x00"
    (out_dir / "psp_xmb_icon.gim").write_bytes(magic + root_chk)

def gen_domain_ubertools_ftx(out_dir: Path):
    w, h = 16, 16
    rgba = bytearray()
    for y in range(h):
        for x in range(w):
            a = 128 if (x >= 12 and y >= 12) else 255
            rgba += bytes(((x * 16) & 0xFF, (y * 14) & 0xFF, 200, a))
    (out_dir / "alice_vorpal_blade.ftx").write_bytes(struct.pack("<iii", w, h, 1) + bytes(rgba))

def gen_domain_heretic2_m8(out_dir: Path):
    w0, h0 = 16, 16
    widths  = [w0, 8, 4] + [0] * 13
    heights = [h0, 8, 4] + [0] * 13
    offsets = [1040, 1040 + 256, 1040 + 256 + 64] + [0] * 13

    pal = bytearray(256 * 3)
    for i in range(256):
        pal[i * 3 : i * 3 + 3] = bytes(((i * 9) & 0xFF, (i * 15) & 0xFF, (i * 23) & 0xFF))
    pal[1 * 3 : 2 * 3] = bytes((40, 180, 255))   # Mana blue
    pal[255 * 3 : 256 * 3] = bytes((255, 0, 255))  # Magenta cutout key

    hdr = (
        struct.pack("<i32s", 2, b"textures/palace/mana_rune".ljust(32, b"\x00"))
        + struct.pack("<16I", *widths)
        + struct.pack("<16I", *heights)
        + struct.pack("<16I", *offsets)
        + b"\x00" * 32
        + bytes(pal)
        + struct.pack("<3i", 0, 0, 0)
    )
    assert len(hdr) == 1040

    mip0 = bytes([1 if (i % 16 < 12) else 255 for i in range(256)])
    mip1 = bytes([1] * 64)
    mip2 = bytes([1] * 16)
    (out_dir / "heretic2_mana_rune.m8").write_bytes(hdr + mip0 + mip1 + mip2)

def gen_domain_heretic2_m32(out_dir: Path):
    w0, h0 = 16, 16
    widths  = [w0, 8] + [0] * 14
    heights = [h0, 8] + [0] * 14
    offsets = [968, 968 + 1024] + [0] * 14

    hdr = (
        struct.pack("<i", 4)
        + b"textures/sof/hud_crosshair".ljust(128, b"\x00")
        + b"\x00" * (128 * 3)
        + struct.pack("<16I", *widths)
        + struct.pack("<16I", *heights)
        + struct.pack("<16I", *offsets)
        + struct.pack("<3i2fi", 0, 0, 0, 1.0, 1.0, 0)
        + b"\x00" * 128
        + struct.pack("<5f2i", 1.0, 1.0, 0.0, 0.0, 1.0, 0, 0)
        + b"\x00" * 80
    )
    assert len(hdr) == 968

    mip0 = bytearray()
    for y in range(h0):
        for x in range(w0):
            on_cross = (x in (7, 8)) or (y in (7, 8))
            mip0 += bytes((50, 250, 90, 255) if on_cross else (10, 20, 10, 64))
    mip1 = bytes((50, 250, 90, 255)) * 64
    (out_dir / "sof_hud_crosshair.m32").write_bytes(hdr + bytes(mip0) + mip1)

def gen_domain_sega_gvr(out_dir: Path):
    # 8x8 GX_TF_RGB565 (0x04): four 4x4 tiles (32B each = 128B)
    # Tile 0 (top-left): gold ring (0xFFE0); other tiles: Sonic blue (0x001F)
    tile_gold = struct.pack(">16H", *([0xFFE0] * 16))
    tile_blue = struct.pack(">16H", *([0x001F] * 16))
    pix = tile_gold + tile_blue * 3
    gbix = struct.pack("<4sII", b"GBIX", 8, 1001) + b"\x00\x00\x00\x00"
    gvrt = struct.pack("<4sIHBB", b"GVRT", 8 + len(pix), 0, 0x00, 0x04) + struct.pack(">HH", 8, 8) + pix
    (out_dir / "sonic_ring_gc.gvr").write_bytes(gbix + gvrt)

def gen_domain_tex0(out_dir: _DomainSampleRouter) -> None:
    # 8x8 RGBA8 (GX_TF_RGBA8 = 0x06): 4 tiles of 4x4 (64B each = 256B), pixel (0,0) = pure cyan (R=0,G=220,B=255,A=255)
    tiles = bytearray(256)
    for t in range(4):
        base = t * 64
        for i in range(16):
            r = 0 if (t == 0 and i == 0) else (40 + t * 30)
            g = 220 if (t == 0 and i == 0) else 100
            b = 255 if (t == 0 and i == 0) else 180
            tiles[base + i * 2 + 0] = 255
            tiles[base + i * 2 + 1] = r
            tiles[base + 32 + i * 2 + 0] = g
            tiles[base + 32 + i * 2 + 1] = b
    total = 0x40 + len(tiles)
    hdr = bytearray(0x40)
    hdr[0:4] = b"TEX0"
    struct.pack_into(">IIiIiIHHII", hdr, 0x04, total, 3, 0, 0x40, 0, 0, 8, 8, 6, 1)
    (out_dir / "brawl_emblem.tex0").write_bytes(bytes(hdr + tiles))

def gen_domain_bclim(out_dir: _DomainSampleRouter) -> None:
    # 8x8 RGBA8 (ctrFmt = 9, LE): 64 Morton-ordered u32 [A,B,G,R in LE -> RdU32LE = (R<<24)|(G<<16)|(B<<8)|A]
    pix = bytearray(64 * 4)
    for m in range(64):
        u = (240 << 24) | ((m * 3) << 16) | (60 << 8) | 255
        struct.pack_into("<I", pix, m * 4, u)
    total_sz = len(pix) + 0x28
    clim = b"CLIM" + struct.pack("<HHIIHH", 0xFEFF, 0x14, 0x02020000, total_sz, 1, 0)
    imag = b"imag" + struct.pack("<IHHII", 0x10, 8, 8, 9, len(pix))
    (out_dir / "3ds_badge_morton.bclim").write_bytes(bytes(pix) + clim + imag)

def gen_domain_bflim(out_dir: _DomainSampleRouter) -> None:
    # 8x8 RGBA8 (flimFmt = 0x09, BE BOM 0xFEFF): 64 pixels * 4B + 0x28 FLIM/imag footer
    pix = bytearray()
    for i in range(64):
        pix += bytes([30, 180, (i * 4) & 0xFF, 255])
    total_sz = len(pix) + 0x28
    flim = b"FLIM" + struct.pack(">HHIIHH", 0xFEFF, 0x14, 0x02020000, total_sz, 1, 0)
    imag = b"imag" + struct.pack(">IHHHBB", 0x10, 8, 8, 64, 0x09, 0x00) + struct.pack(">I", len(pix))
    (out_dir / "wiiu_banner_icon.bflim").write_bytes(bytes(pix) + flim + imag)

def gen_domain_dtx(out_dir: _DomainSampleRouter) -> None:
    # LT2 (version = -5, 164B header), 8x8 BPP_32 (extra[2] = 3)
    hdr = bytearray(164)
    struct.pack_into("<IiHHHHII", hdr, 0, 0, -5, 8, 8, 1, 0, 1, 0)
    hdr[0x1E] = 3  # BPP_32
    pix = bytearray()
    for y in range(8):
        for x in range(8):
            pix += bytes([40 + x * 20, 120 + y * 12, 210, 255])  # BGRA
    (out_dir / "nolf_crate_wall.dtx").write_bytes(bytes(hdr + pix))

def gen_domain_swl(out_dir: _DomainSampleRouter) -> None:
    # 0x4CC header + 4 mip levels for 16x16 (256 + 64 + 16 + 4 = 340B)
    hdr = bytearray(0x4CC)
    name = b"sin/sec_panel_01\x00"
    hdr[0:len(name)] = name
    struct.pack_into("<II", hdr, 0x80, 16, 16)
    for i in range(256):
        hdr[0x88 + i * 4 + 0] = (i * 7) & 0xFF
        hdr[0x88 + i * 4 + 1] = (i * 3 + 40) & 0xFF
        hdr[0x88 + i * 4 + 2] = 180
        hdr[0x88 + i * 4 + 3] = 255
    m0 = 0x4CC
    m1 = m0 + 256
    m2 = m1 + 64
    m3 = m2 + 16
    struct.pack_into("<IIII", hdr, 0x4BC, m0, m1, m2, m3)
    mips = bytes([(i * 5 + 1) & 0xFE for i in range(340)])
    (out_dir / "sin_sec_panel.swl").write_bytes(bytes(hdr) + mips)

def gen_domain_fsh(out_dir: _DomainSampleRouter) -> None:
    # SHPI header (16B) + 1 dir entry (8B -> off 24) + record 0x7D BGRA32 (16B + 8*8*4B)
    w, h = 8, 8
    pix = bytearray()
    for i in range(w * h):
        pix += bytes([20, 140, 250, 255])  # BGRA
    rec_hdr = bytearray(16)
    rec_hdr[0] = 0x7D
    struct.pack_into("<HHHHHH", rec_hdr, 4, w, h, 0, 0, 0, 0)
    total_sz = 24 + len(rec_hdr) + len(pix)
    shpi = b"SHPI" + struct.pack("<II", total_sz, 1) + b"GIMX" + b"car0" + struct.pack("<I", 24)
    (out_dir / "nfs_car_decal.fsh").write_bytes(shpi + bytes(rec_hdr) + bytes(pix))

def gen_domain_ctxr(out_dir: _DomainSampleRouter) -> None:
    w, h = 8, 8
    hdr = bytearray(0x84)
    hdr[0:4] = b"TXRC"
    struct.pack_into(">IHHH", hdr, 0x04, 7, w, h, 1)
    hdr[0x26] = 1
    pix_len = w * h * 4
    struct.pack_into(">I", hdr, 0x80, pix_len)
    pix = bytearray()
    for i in range(w * h):
        pix += bytes([255, 60, 210, (80 + i * 2) & 0xFF])  # ARGB
    (out_dir / "mgs2_codec_icon.ctxr").write_bytes(bytes(hdr + pix))

def gen_domain_iwi(out_dir: _DomainSampleRouter) -> None:
    # IWi v6 (CoD4, 28B header): fmt=0x01 (ARGB32/BGRA in LE), 4x4, small 2x2 mip first + 4x4 Level-0 at tail!
    w, h = 4, 4
    mip1 = bytes([10, 20, 30, 255] * 4)        # 2x2 = 16B
    mip0 = bytes([50, 190, 240, 255] * 16)     # 4x4 = 64B at tail!
    hdr = bytearray(28)
    hdr[0:4] = b"IWi\x06"
    hdr[4] = 0x01  # ARGB32
    hdr[5] = 0x00
    struct.pack_into("<HHHIIII", hdr, 6, w, h, 1, 28 + len(mip1), 28, 28, 28)
    (out_dir / "cod4_reticle.iwi").write_bytes(bytes(hdr) + mip1 + mip0)

def gen_domain_bmf(out_dir: _DomainSampleRouter) -> None:
    def bmf_blk(bid: int, body: bytes) -> bytes:
        return struct.pack("<BI", bid, len(body)) + body

    font_name = b"PixelOperator8\x00"
    info_body = struct.pack("<hBBHBBBBBBBB", -16, 0x40, 0, 100, 1, 0, 0, 0, 0, 1, 1, 0) + font_name
    common_body = struct.pack("<HHHHHHBBB", 18, 14, 256, 128, 1, 0, 0, 1, 2)
    pages_body = b"hud_pixel_font_0.png\x00"
    chars_body = (
        struct.pack("<IHHHHhhhBB", 65, 0, 0, 9, 14, 0, 2, 10, 0, 15) +
        struct.pack("<IHHHHhhhBB", 86, 10, 0, 9, 14, 0, 2, 10, 0, 15)
    )
    kern_body = struct.pack("<IIh", 65, 86, -2)
    payload = b"BMF\x03" + bmf_blk(1, info_body) + bmf_blk(2, common_body) + bmf_blk(3, pages_body) + bmf_blk(4, chars_body) + bmf_blk(5, kern_body)
    (out_dir / "hud_pixel_font.bmf").write_bytes(payload)


def _run_consolidated_domain_samples(td_root: Path) -> None:

    router = _DomainSampleRouter(td_root, {'ps2_hud_icon.tm2': 'images', 'wii_banner_cmpr.tpl': 'images', 'windwaker_rupee.bti': 'images', 'psp_xmb_icon.gim': 'images', 'alice_vorpal_blade.ftx': 'images', 'heretic2_mana_rune.m8': 'images', 'sof_hud_crosshair.m32': 'images', 'sonic_ring_gc.gvr': 'images', 'brawl_emblem.tex0': 'images', '3ds_badge_morton.bclim': 'images', 'wiiu_banner_icon.bflim': 'images', 'nolf_crate_wall.dtx': 'images', 'sin_sec_panel.swl': 'images', 'nfs_car_decal.fsh': 'images', 'mgs2_codec_icon.ctxr': 'images', 'cod4_reticle.iwi': 'images', 'hud_pixel_font.bmf': 'images'})

    for fn in [gen_domain_ps2_tm2, gen_domain_nintendo_tpl, gen_domain_nintendo_bti, gen_domain_psp_gim, gen_domain_ubertools_ftx, gen_domain_heretic2_m8, gen_domain_heretic2_m32, gen_domain_sega_gvr, gen_domain_tex0, gen_domain_bclim, gen_domain_bflim, gen_domain_dtx, gen_domain_swl, gen_domain_fsh, gen_domain_ctxr, gen_domain_iwi, gen_domain_bmf]:

        fn(router)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate deterministic image format test samples.")
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory (defaults to <repo_root>/testdata/images).",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    target_dir = args.out_dir.resolve() if args.out_dir is not None else (repo_root / "testdata" / "images")
    generated = generate_all(target_dir)

    print(f"Generated {len(generated)} deterministic image sample files in {target_dir}:")
    for p in generated:
        try:
            disp = p.relative_to(repo_root).as_posix()
        except ValueError:
            disp = str(p)
        print(f"  {disp} ({p.stat().st_size} bytes)")
    if len(__import__('sys').argv) == 1:
        _run_consolidated_domain_samples(Path(__file__).resolve().parent.parent / 'testdata')


if __name__ == "__main__":
    main()
