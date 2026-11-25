#!/usr/bin/env python3
"""Generates deterministic, specification-exact sample archives and filesystem images
in testdata/archives/ covering all 10 new archive/FS formats plus upgraded codecs:

  1. usb_media_volume.exfat       - Microsoft exFAT (11-sec checksum, FAT chain + NoFatChain, UTF-16LE tree)
  2. initrd_boot.romfs            - Linux ROMFS v1 ("-rom1fs-", BE superblock checksum, hardlink + subdirs)
  3. embedded_rootfs.cramfs       - Linux CramFS (0x28CD3D45 LE, 12B bitfield inodes, 4KB paged zlib)
  4. estsoft_bundle.alz           - ESTsoft ALZip ("ALZ\\x01", Stored 0 + BZip2 1 + Deflate 2 + CRC32)
  5. vintage_backup.zoo           - Rahul Dhesi ZOO v2.10 (Type 2 long names/dirs, Store 0 + LZW-13 1 + LH5 2)
  6. bbs_distro.arc               - SEA ARC / PKPAK (0x1A chain: Store 2, RLE90 3, Squeeze 4, Crunch 8, Squash 9)
  7. mac_classic_folder.sit       - Aladdin StuffIt Classic ("SIT!"+"rLau", folders 32/33, Store/RLE90/LZW14/Huff/Deflate)
  8. wiiu_course_pack.szs         - Nintendo Yaz0-compressed Big-Endian SARC (.szs)
  9. switch_ui_pack.sarc          - Nintendo Little-Endian SARC (.sarc)
 10. nds_pokemon_data.narc        - Nintendo DS Nitro Archive ("NARC" + BTAF + hierarchical BTNF + GMIF)
 11. steam_hl2_cache.gcf          - Valve Steam Legacy GCF v6 (fragmented multi-cluster chain + directory tree)
 12. macos_installer_bvx2.dmg     - Apple UDIF .dmg ("koly" + "mish" LZFSE bvx2 0x80000007 block runs -> FAT16)
 13. rar5_compressed_delta_e8.rar - RAR 5.0 v5 LZ+Huffman compressed archive with E8 + Delta filters
 14. win32_solid_bcj2.7z          - 7-Zip solid archive with kEncodedHeader + 4-stream BCJ2 (0x0303011B)
 15. dos_boot_floppy.img          - Raw FAT16 superfloppy disk image with VFAT long filenames
 16. ntfs_large_mft.ntfs          - Raw NTFS volume with fragmented $MFT runlist & file at MFT record 2060
 17. zip64_multicodec.zip         - ZIP64 archive (EOCD64 + 0x0001 extra) with BZip2 (12), LZMA (14), Deflate (8), 0-byte file
 18. hybrid_udf_bridge.iso        - Hybrid ISO 9660 bridge stub + full UDF 1.02 optical disc image

Usage:
    python tools/generate_samples_archives.py [output_dir]
"""

from __future__ import annotations

import base64
import bz2
import lzma
import struct
import sys
import zlib
from pathlib import Path


# ==============================================================================
# Shared Checksum, Bitstream & Classic Codec Helpers
# ==============================================================================
def crc32_ieee(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def crc16_arc(data: bytes, crc: int = 0) -> int:
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xA001) if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def encode_rle90(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        b = data[i]
        run = 1
        while i + run < n and data[i + run] == b and run < 255:
            run += 1
        if b == 0x90:
            out += b"\x90\x00"
            if run > 1:
                out += bytes([0x90, run])
            i += run
        elif run >= 4:
            out.append(b)
            out += bytes([0x90, run])
            i += run
        else:
            for _ in range(run):
                out.append(b)
            i += run
    return bytes(out)


def encode_unix_compress_lzw(data: bytes, max_bits: int = 12, block_clear: bool = True) -> bytes:
    """Exact UNIX `ncompress` LSB-first n_bits-byte group aligned LZW encoder."""
    if not data:
        return b""
    max_max_code = 1 << max_bits
    n_bits = 9
    max_code = (1 << n_bits) - 1
    free_ent = 257 if block_clear else 256
    dictionary: dict[tuple[int, int], int] = {}

    out = bytearray()
    g_buf = bytearray(max_bits)
    g_bit_off = 0

    def flush_group():
        nonlocal g_buf, g_bit_off
        if g_bit_off > 0:
            n_bytes = (g_bit_off + 7) // 8
            out.extend(g_buf[:n_bytes])
            g_buf = bytearray(max_bits)
            g_bit_off = 0

    def put_code(code: int):
        nonlocal n_bits, max_code, g_bit_off
        r_off = g_bit_off
        byte_idx = r_off >> 3
        bit_shift = r_off & 7
        val = code << bit_shift
        g_buf[byte_idx] |= val & 0xFF
        g_buf[byte_idx + 1] |= (val >> 8) & 0xFF
        if byte_idx + 2 < len(g_buf):
            g_buf[byte_idx + 2] |= (val >> 16) & 0xFF
        g_bit_off += n_bits
        if g_bit_off == (n_bits << 3):
            flush_group()
        if free_ent > max_code:
            flush_group()
            n_bits += 1
            max_code = max_max_code if n_bits == max_bits else ((1 << n_bits) - 1)

    ent = data[0]
    for b in data[1:]:
        key = (ent, b)
        if key in dictionary:
            ent = dictionary[key]
        else:
            put_code(ent)
            if free_ent < max_max_code:
                dictionary[key] = free_ent
                free_ent += 1
            ent = b
    put_code(ent)
    flush_group()
    return bytes(out)


class MsbBitWriter:
    def __init__(self) -> None:
        self.buf = bytearray()
        self.cur = 0
        self.bits = 0

    def write(self, val: int, nbits: int) -> None:
        for i in range(nbits - 1, -1, -1):
            self.cur = (self.cur << 1) | ((val >> i) & 1)
            self.bits += 1
            if self.bits == 8:
                self.buf.append(self.cur)
                self.cur = 0
                self.bits = 0

    def finish(self) -> tuple[bytes, int]:
        """Returns (bytes, last_byte_valid_bits 1..8)."""
        if self.bits == 0:
            return bytes(self.buf), 8
        last_bits = self.bits
        self.buf.append((self.cur << (8 - self.bits)) & 0xFF)
        self.cur = 0
        self.bits = 0
        return bytes(self.buf), last_bits


def build_canonical_codes_msb(lengths: list[int]) -> list[tuple[int, int]]:
    max_l = max(lengths, default=0)
    cnt = [0] * (max_l + 1)
    for l in lengths:
        if l > 0:
            cnt[l] += 1
    first = [0] * (max_l + 1)
    code = 0
    for l in range(1, max_l + 1):
        code = (code + cnt[l - 1]) << 1
        first[l] = code
    res = [(0, 0)] * len(lengths)
    for sym, l in enumerate(lengths):
        if l > 0:
            res[sym] = (first[l], l)
            first[l] += 1
    return res


# ==============================================================================
# 1. Microsoft exFAT (.exfat)
# ==============================================================================
def gen_01_exfat(out_dir: Path) -> str:
    fname = "usb_media_volume.exfat"
    bps_shift = 9   # 512 B/sec
    spc_shift = 2   # 4 sec/cluster = 2048 B/cluster
    bps = 1 << bps_shift
    bpc = bps << spc_shift
    fat_off_sec = 24
    fat_len_sec = 4
    heap_off_sec = 32
    cluster_count = 48
    total_sectors = heap_off_sec + cluster_count * (1 << spc_shift)

    img = bytearray(total_sectors * bps)

    # Clusters:
    # 2 = Allocation Bitmap (1 cluster)
    # 3 = Up-case Table (1 cluster)
    # 4 = Root Directory (1 cluster)
    # 5 = Subdir "docs" (1 cluster, NoFatChain)
    # 6, 7 = Contiguous file "docs/specification.txt" (NoFatChain = 0x02, 2 clusters)
    # 8 -> 10 -> 9 = Non-contiguous FAT-chained file "firmware_boot.ini" (NoFatChain = 0x00, 3 clusters!)
    spec_data = (
        b"exFAT Specification Sample\n"
        + b"Contiguous NoFatChain=0x02 stream across clusters 6..7!\n" * 45
    )
    assert bpc < len(spec_data) <= 2 * bpc
    chained_data = (
        b"[ExFatBootConfig]\nMode=NonContiguousFatChain\n"
        + b"ClusterHop=8->10->9_Verified_OK!\n" * 140
    )
    assert 2 * bpc < len(chained_data) <= 3 * bpc

    # Boot sector (Sector 0)
    vbr = bytearray(bps)
    vbr[0:3] = b"\xEB\x76\x90"
    vbr[3:11] = b"EXFAT   "
    struct.pack_into("<Q", vbr, 64, 0)                  # PartitionOffset
    struct.pack_into("<Q", vbr, 72, total_sectors)      # VolumeLength
    struct.pack_into("<I", vbr, 80, fat_off_sec)        # FatOffset
    struct.pack_into("<I", vbr, 84, fat_len_sec)        # FatLength
    struct.pack_into("<I", vbr, 88, heap_off_sec)       # ClusterHeapOffset
    struct.pack_into("<I", vbr, 92, cluster_count)      # ClusterCount
    struct.pack_into("<I", vbr, 96, 4)                  # FirstClusterOfRootDirectory = 4
    struct.pack_into("<I", vbr, 100, 0x20261125)        # VolumeSerialNumber
    struct.pack_into("<H", vbr, 104, 0x0100)            # FileSystemRevision 1.00
    struct.pack_into("<H", vbr, 106, 0)                 # VolumeFlags
    vbr[108] = bps_shift
    vbr[109] = spc_shift
    vbr[110] = 1                                        # NumberOfFats
    vbr[510] = 0x55
    vbr[511] = 0xAA
    img[0:bps] = vbr

    # Extended boot sectors 1..8 (0x55AA signature at +510..511)
    for s in range(1, 9):
        struct.pack_into("<I", img, s * bps + 508, 0xAA550000)

    # Compute 11-sector exFAT Boot Checksum into Sector 11 (and copy Sectors 0..11 to Backup 12..23)
    csum = 0
    for idx in range(11 * bps):
        if idx in (106, 107, 112):
            continue
        b = img[idx]
        csum = (((csum >> 1) | ((csum & 1) << 31)) + b) & 0xFFFFFFFF
    sec11 = struct.pack("<I", csum) * (bps // 4)
    img[11 * bps : 12 * bps] = sec11
    img[12 * bps : 24 * bps] = img[0 : 12 * bps]

    # FAT table at fat_off_sec
    fat_base = fat_off_sec * bps
    fat_entries = {
        0: 0xFFFFFFF8,
        1: 0xFFFFFFFF,
        2: 0xFFFFFFFF,
        3: 0xFFFFFFFF,
        4: 0xFFFFFFFF,
        5: 0xFFFFFFFF,
        8: 10,
        10: 9,
        9: 0xFFFFFFFF,
    }
    for cl, nxt in fat_entries.items():
        struct.pack_into("<I", img, fat_base + cl * 4, nxt)

    def clus_off(c: int) -> int:
        return (heap_off_sec * bps) + (c - 2) * bpc

    # Cluster 2: Allocation bitmap
    img[clus_off(2) : clus_off(2) + 2] = b"\xFF\x03"

    # Helper to build exFAT 0x85 + 0xC0 + 0xC1 directory entry set (with exact entrySetChecksum!)
    def make_exfat_entry_set(name: str, is_dir: bool, first_clus: int, data_len: int, no_fat_chain: bool) -> bytes:
        u16 = name.encode("utf-16le")
        n_chars = len(u16) // 2
        fn_Visual = (n_chars + 14) // 15
        sec_cnt = 1 + fn_Visual

        name_hash = 0
        for ch in name.upper():
            v = ord(ch)
            for b in (v & 0xFF, (v >> 8) & 0xFF):
                name_hash = (((name_hash >> 1) | ((name_hash & 1) << 15)) + b) & 0xFFFF

        e85 = bytearray(32)
        e85[0] = 0x85
        e85[1] = sec_cnt
        struct.pack_into("<H", e85, 4, 0x0010 if is_dir else 0x0020)
        dos_ts = ((2026 - 1980) << 25) | (11 << 21) | (25 << 16) | (12 << 11)
        struct.pack_into("<I", e85, 8, dos_ts)
        struct.pack_into("<I", e85, 12, dos_ts)

        ec0 = bytearray(32)
        ec0[0] = 0xC0
        ec0[1] = 0x01 | (0x02 if no_fat_chain else 0x00)
        ec0[3] = n_chars
        struct.pack_into("<H", ec0, 4, name_hash)
        struct.pack_into("<Q", ec0, 8, data_len)
        struct.pack_into("<I", ec0, 20, first_clus)
        alloc_len = ((data_len + bpc - 1) // bpc) * bpc if data_len > 0 else bpc
        struct.pack_into("<Q", ec0, 24, alloc_len)

        raw_set = bytearray(e85 + ec0)
        for s in range(fn_Visual):
            ec1 = bytearray(32)
            ec1[0] = 0xC1
            chunk = u16[s * 30 : (s + 1) * 30]
            ec1[2 : 2 + len(chunk)] = chunk
            raw_set += ec1

        chk = 0
        for i, b in enumerate(raw_set):
            if i in (2, 3):
                continue
            chk = (((chk >> 1) | ((chk & 1) << 15)) + b) & 0xFFFF
        struct.pack_into("<H", raw_set, 2, chk)
        return bytes(raw_set)

    # Cluster 4: Root Directory (Volume Label 0x83 + Bitmap 0x81 + UpCase 0x82 + "docs" dir + "firmware_boot.ini")
    root = bytearray()
    lbl = bytearray(32)
    lbl[0] = 0x83
    lbl[1] = 9
    lbl[2:20] = "EXFAT_USB".encode("utf-16le")
    root += lbl
    root += make_exfat_entry_set("docs", True, 5, bpc, True)
    root += make_exfat_entry_set("firmware_boot.ini", False, 8, len(chained_data), False)
    img[clus_off(4) : clus_off(4) + len(root)] = root

    # Cluster 5: "docs" Subdirectory
    sub = make_exfat_entry_set("specification.txt", False, 6, len(spec_data), True)
    img[clus_off(5) : clus_off(5) + len(sub)] = sub

    # Clusters 6..7: contiguous "docs/specification.txt"
    img[clus_off(6) : clus_off(6) + len(spec_data)] = spec_data

    # Clusters 8 -> 10 -> 9: non-contiguous "firmware_boot.ini"
    img[clus_off(8) : clus_off(8) + bpc] = chained_data[0:bpc]
    img[clus_off(10) : clus_off(10) + bpc] = chained_data[bpc : 2 * bpc]
    rem = chained_data[2 * bpc :]
    img[clus_off(9) : clus_off(9) + len(rem)] = rem

    (out_dir / fname).write_bytes(img)
    return fname


# ==============================================================================
# 2. Linux ROMFS v1 (.romfs)
# ==============================================================================
def gen_02_romfs(out_dir: Path) -> str:
    fname = "initrd_boot.romfs"

    def pad16(b: bytes) -> bytes:
        rem = len(b) & 15
        return b if rem == 0 else b + b"\x00" * (16 - rem)

    def pad_name16(name: str) -> bytes:
        raw = name.encode("utf-8") + b"\x00"
        return pad16(raw)

    def inode_csum(hdr_and_name: bytes) -> int:
        s = 0
        for i in range(0, len(hdr_and_name), 4):
            s = (s + struct.unpack_from(">I", hdr_and_name, i)[0]) & 0xFFFFFFFF
        return (-s) & 0xFFFFFFFF

    def make_inode(next_off: int, ftype: int, exec_bit: bool, spec: int, name: str, payload: bytes = b"") -> bytes:
        nm = pad_name16(name)
        next_fh = (next_off & ~0xF) | (0x8 if exec_bit else 0) | (ftype & 0x7)
        hdr = bytearray(struct.pack(">4I", next_fh, spec, len(payload), 0) + nm)
        struct.pack_into(">I", hdr, 12, inode_csum(hdr))
        return bytes(hdr) + pad16(payload)

    # Layout:
    # 0x000: Superblock "-rom1fs-" + size + csum + pad_name16("INITRD_ROMFS") = 16 + 16 = 32 (0x20)
    # 0x020: Root entry 0: dir "etc" (spec -> child at 0x060, next -> 0x040) = 32B
    # 0x040: Root entry 1: dir "bin" (spec -> child at 0x0A0, next -> 0) = 32B
    # 0x060: etc child 0: regular file "hostname" ("explorer2-romfs-node\n", next -> 0)
    # 0x0A0: bin child 0: regular file "init.sh" ("#!/bin/sh\necho ROMFS_OK\n", next -> 0x0E0)
    # 0x0E0: bin child 1: hardlink "sh_alias" -> spec = 0x0A0 (next -> 0)
    host_payload = b"explorer2-romfs-node\n"
    init_payload = b"#!/bin/sh\necho ROMFS_BOOT_VERIFIED_OK\n"

    sb_prefix = b"-rom1fs-" + struct.pack(">2I", 0, 0) + pad_name16("INITRD_V1")
    assert len(sb_prefix) == 32

    etc_off = 0x20
    bin_off = 0x40
    hostname_off = 0x60
    hostname_blk = make_inode(0, 2, False, 0, "hostname", host_payload)
    init_off = hostname_off + len(hostname_blk)
    init_blk_tmp = make_inode(0, 2, True, 0, "init.sh", init_payload)
    alias_off = init_off + len(init_blk_tmp)
    init_blk = make_inode(alias_off, 2, True, 0, "init.sh", init_payload)
    alias_blk = make_inode(0, 0, True, init_off, "sh_alias", b"")

    etc_blk = make_inode(bin_off, 1, False, hostname_off, "etc", b"")
    bin_blk = make_inode(0, 1, False, init_off, "bin", b"")
    assert len(etc_blk) == 32 and len(bin_blk) == 32

    body = bytearray(sb_prefix + etc_blk + bin_blk + hostname_blk + init_blk + alias_blk)
    full_size = len(body)
    struct.pack_into(">I", body, 8, full_size)

    chk_len = min(512, full_size)
    s = 0
    for i in range(0, chk_len, 4):
        chunk = body[i : i + 4].ljust(4, b"\x00")
        s = (s + struct.unpack(">I", chunk)[0]) & 0xFFFFFFFF
    struct.pack_into(">I", body, 12, (-s) & 0xFFFFFFFF)

    (out_dir / fname).write_bytes(body)
    return fname


# ==============================================================================
# 3. Linux CramFS (.cramfs)
# ==============================================================================
def gen_03_cramfs(out_dir: Path) -> str:
    fname = "embedded_rootfs.cramfs"

    def pack_inode_le(mode: int, uid: int, size: int, gid: int, namelen_bytes: int, offset_bytes: int) -> bytes:
        assert (namelen_bytes & 3) == 0 and (offset_bytes & 3) == 0
        w0 = (mode & 0xFFFF) | ((uid & 0xFFFF) << 16)
        w1 = (size & 0xFFFFFF) | ((gid & 0xFF) << 24)
        w2 = ((namelen_bytes // 4) & 0x3F) | (((offset_bytes // 4) & 0x03FFFFFF) << 6)
        return struct.pack("<3I", w0, w1, w2)

    def pad_name4(s: str) -> bytes:
        b = s.encode("utf-8")
        rem = len(b) & 3
        if rem != 0 or len(b) == 0:
            b += b"\x00" * ((4 - rem) & 3)
        return b

    def pack_paged_file(base_off: int, raw: bytes) -> bytes:
        num_pages = (len(raw) + 4095) // 4096
        ptrs = []
        pages = bytearray()
        cur_end = base_off + num_pages * 4
        for p in range(num_pages):
            pg = raw[p * 4096 : (p + 1) * 4096]
            comp = zlib.compress(pg, 6)
            pages += comp
            cur_end += len(comp)
            ptrs.append(cur_end)
        out = b"".join(struct.pack("<I", x) for x in ptrs) + bytes(pages)
        if len(out) & 3:
            out += b"\x00" * (4 - (len(out) & 3))
        return out

    conf_data = b"hostname=cramfs-router\nwatchdog=enabled\n"
    readme_data = (b"CramFS Multi-Page Zlib Verification Line 0123456789ABCDEF\n" * 110)
    assert len(readme_data) > 5000  # 2 compressed 4KB pages!

    # Reserve 76B superblock -> pad to 80B
    # Root dir has 2 entries: subdir "etc" + file "readme_multi.txt"
    nm_etc = pad_name4("etc")
    nm_readme = pad_name4("readme_multi.txt")
    nm_sysconf = pad_name4("system.conf")

    root_dir_off = 80
    root_dir_len = (12 + len(nm_etc)) + (12 + len(nm_readme))
    etc_dir_off = root_dir_off + root_dir_len
    etc_dir_len = 12 + len(nm_sysconf)

    f1_off = etc_dir_off + etc_dir_len
    f1_blob = pack_paged_file(f1_off, conf_data)
    f2_off = f1_off + len(f1_blob)
    f2_blob = pack_paged_file(f2_off, readme_data)
    total_size = f2_off + len(f2_blob)

    root_dir_bytes = (
        pack_inode_le(0o040755, 0, etc_dir_len, 0, len(nm_etc), etc_dir_off) + nm_etc
        + pack_inode_le(0o100644, 0, len(readme_data), 0, len(nm_readme), f2_off) + nm_readme
    )
    etc_dir_bytes = pack_inode_le(0o100644, 0, len(conf_data), 0, len(nm_sysconf), f1_off) + nm_sysconf

    root_inode = pack_inode_le(0o040755, 0, len(root_dir_bytes), 0, 0, root_dir_off)
    sb = bytearray(80)
    struct.pack_into("<I", sb, 0, 0x28CD3D45)
    struct.pack_into("<I", sb, 4, total_size)
    struct.pack_into("<I", sb, 8, 0x00000001)  # CRAMFS_FLAG_FSID_VERSION_2
    struct.pack_into("<I", sb, 12, 0)
    sb[16:32] = b"Compressed ROMFS"
    struct.pack_into("<4I", sb, 32, 0, 0, 2, 3)
    sb[48:64] = b"CRAMFS_ROOT\x00\x00\x00\x00\x00"
    sb[64:76] = root_inode

    img = bytes(sb) + root_dir_bytes + etc_dir_bytes + f1_blob + f2_blob
    assert len(img) == total_size
    (out_dir / fname).write_bytes(img)
    return fname


# ==============================================================================
# 4. ESTsoft ALZip (.alz)
# ==============================================================================
def gen_04_alz(out_dir: Path) -> str:
    fname = "estsoft_bundle.alz"
    out = bytearray(b"ALZ\x01" + struct.pack("<HH", 10, 0))

    files = [
        ("manifest.txt", 0, 0x10, b"ALZip Stored Entry OK\n"),
        ("notes/bzip2_doc.txt", 1, 0x20, b"ALZip BZip2 Compressed Stream Payload! " * 12),
        ("src/deflate_code.c", 2, 0x40, b"int alz_deflate_verify(void) { return 42; }\n" * 16),
    ]
    dos_timedate = (((2026 - 1980) << 9 | (11 << 5) | 25) << 16) | (12 << 11)

    for rel_name, method, desc_sz_flag, raw in files:
        if method == 0:
            comp = raw
        elif method == 1:
            comp = bz2.compress(raw, 9)
        else:
            co = zlib.compressobj(6, zlib.DEFLATED, -15)
            comp = co.compress(raw) + co.flush()

        name_b = rel_name.encode("utf-8")
        crc = crc32_ieee(raw)
        width = {0x10: 1, 0x20: 2, 0x40: 4, 0x80: 8}[desc_sz_flag]
        fmt_sz = {1: "<B", 2: "<H", 4: "<I", 8: "<Q"}[width]

        out += b"BLZ\x01"
        out += struct.pack("<HBI BB", len(name_b), 0x20, dos_timedate, desc_sz_flag, 0)
        out += struct.pack("<BBI", method, 0, crc)
        out += struct.pack(fmt_sz, len(comp))
        out += struct.pack(fmt_sz, len(raw))
        out += name_b
        out += comp

    out += b"CLZ\x01" + struct.pack("<3I", 0, 0, 0)
    out += b"ELZ\x01"
    (out_dir / fname).write_bytes(out)
    return fname


# ==============================================================================
# 5. Rahul Dhesi ZOO (.zoo — Store 0, 13-bit LZW 1, LH5 2)
# ==============================================================================
def encode_zoo_lzw13(data: bytes) -> bytes:
    """Rahul Dhesi ZOO Method 1: 9..13-bit LSB-first continuous LZW (Clear=256, EOF=257, FirstFree=258)."""
    n_bits = 9
    max_code = 512
    free_code = 258
    dictionary: dict[tuple[int, int], int] = {}
    bit_buf = 0
    bit_cnt = 0
    out = bytearray()

    def put(code: int):
        nonlocal bit_buf, bit_cnt
        bit_buf |= (code & ((1 << n_bits) - 1)) << bit_cnt
        bit_cnt += n_bits
        while bit_cnt >= 8:
            out.append(bit_buf & 0xFF)
            bit_buf >>= 8
            bit_cnt -= 8

    put(256)  # Initial CLEAR
    if data:
        ent = data[0]
        for b in data[1:]:
            key = (ent, b)
            if key in dictionary:
                ent = dictionary[key]
            else:
                put(ent)
                if free_code < 8192:
                    dictionary[key] = free_code
                    free_code += 1
                    if free_code > max_code and n_bits < 13:
                        n_bits += 1
                        max_code <<= 1
                ent = b
        put(ent)
    put(257)  # Z_EOF
    if bit_cnt > 0:
        out.append(bit_buf & 0xFF)
    return bytes(out)


def encode_lh5_stream(data: bytes) -> bytes:
    """Valid LHA/ZOO LH5 (np=14, pbit=4) block-adaptive canonical Huffman + LZ77 encoder."""
    bw = MsbBitWriter()
    # Tokenize into literals (0..255) and simple LZ77 matches (sym = 256 + len - 3, dist0 = dist - 1)
    tokens: list[tuple[int, int]] = []
    i = 0
    n = len(data)
    while i < n:
        best_len = 0
        best_dist = 0
        win_start = max(0, i - 4096)
        for j in range(i - 1, win_start - 1, -1):
            ml = 0
            while ml < 60 and i + ml < n and data[j + ml] == data[i + ml]:
                ml += 1
            if ml >= 4 and ml > best_len:
                best_len = ml
                best_dist = i - j
        if best_len >= 4:
            tokens.append((256 + best_len - 3, best_dist - 1))
            i += best_len
        else:
            tokens.append((data[i], 0))
            i += 1

    bw.write(len(tokens), 16)  # blockRemaining

    # Build simple 2-level or balanced canonical lengths for used C-symbols (<= 510)
    used_c = sorted({t[0] for t in tokens})
    if len(used_c) == 1:
        # Single C-symbol shortcut: pt_len single 0, c_len n=0 + 9-bit sym
        bw.write(0, 5)
        bw.write(0, 5)
        bw.write(0, 9)
        bw.write(used_c[0], 9)
        c_codes = {}
    else:
        k = 1
        while (1 << k) < len(used_c):
            k += 1
        c_lens = [0] * 510
        full_slots = 1 << k
        short_cnt = full_slots - len(used_c)
        for idx, sym in enumerate(used_c):
            c_lens[sym] = (k - 1) if idx < short_cnt else k
        c_codes_list = build_canonical_codes_msb(c_lens)
        c_codes = {sym: c_codes_list[sym] for sym in used_c}

        # Encode c_lens[0 .. max_c] using pt_tbl where pt symbols used are {0 (zero), k+1, k+2}
        max_c = used_c[-1] + 1
        pt_used = sorted({0} | {c_lens[s] + 2 for s in used_c})
        pk = 1
        while (1 << pk) < len(pt_used):
            pk += 1
        pt_lens = [0] * 19
        p_short = (1 << pk) - len(pt_used)
        for idx, ps in enumerate(pt_used):
            pt_lens[ps] = (pk - 1) if idx < p_short else pk
        pt_codes = build_canonical_codes_msb(pt_lens)

        pt_n = pt_used[-1] + 1
        bw.write(pt_n, 5)
        idx_p = 0
        while idx_p < pt_n:
            pl = pt_lens[idx_p]
            assert pl < 7
            bw.write(pl, 3)
            idx_p += 1
            if idx_p == 3:
                bw.write(0, 2)  # 0 skip after iSpecial=3

        bw.write(max_c, 9)
        for s in range(max_c):
            cl = c_lens[s]
            sym_pt = 0 if cl == 0 else (cl + 2)
            code_v, code_l = pt_codes[sym_pt]
            bw.write(code_v, code_l)

    # Distance pTbl (np=14, pbit=4)
    def dist_slot(d0: int) -> int:
        if d0 == 0:
            return 0
        return d0.bit_length()

    used_p = sorted({dist_slot(t[1]) for t in tokens if t[0] >= 256} | {0})
    if len(used_p) == 1:
        bw.write(0, 4)
        bw.write(used_p[0], 4)
        p_codes = {}
    else:
        pk = 1
        while (1 << pk) < len(used_p):
            pk += 1
        p_lens = [0] * 14
        pshort = (1 << pk) - len(used_p)
        for idx, ps in enumerate(used_p):
            p_lens[ps] = (pk - 1) if idx < pshort else pk
        p_codes_list = build_canonical_codes_msb(p_lens)
        p_codes = {ps: p_codes_list[ps] for ps in used_p}
        pn = used_p[-1] + 1
        bw.write(pn, 4)
        for idx_p in range(pn):
            bw.write(p_lens[idx_p], 3)

    # Emit tokens
    for sym, d0 in tokens:
        if c_codes:
            cv, cl = c_codes[sym]
            bw.write(cv, cl)
        if sym >= 256:
            ps = dist_slot(d0)
            if p_codes:
                pv, pl = p_codes[ps]
                bw.write(pv, pl)
            if ps > 1:
                bw.write(d0 & ((1 << (ps - 1)) - 1), ps - 1)

    raw_bytes, _ = bw.finish()
    return raw_bytes


def gen_05_zoo(out_dir: Path) -> str:
    fname = "vintage_backup.zoo"
    entries_spec = [
        ("", "readme.txt", "README.TXT", 0, b"Rahul Dhesi ZOO 2.10 Stored File OK!\n"),
        ("docs", "manual_lzw.txt", "MANUAL.TXT", 1, b"ZOO 13-bit LZW Compression Test Line! " * 14),
        ("src/core", "engine_lh5.c", "ENGINE.C", 2, b"/* ZOO LH5 Huffman+LZ77 */ int zoo_ok = 1;\n" * 16),
    ]

    zoo_start = 42
    hdr = bytearray(42)
    hdr[0:20] = b"ZOO 2.10 Archive.\x1a\x00\x00"
    struct.pack_into("<III BB", hdr, 20, 0xFDC4A7DC, zoo_start, (-zoo_start) & 0xFFFFFFFF, 2, 1)

    out = bytearray(hdr)
    for idx, (dname, lfname, sname, method, raw) in enumerate(entries_spec):
        if method == 0:
            comp = raw
        elif method == 1:
            comp = encode_zoo_lzw13(raw)
        else:
            comp = encode_lh5_stream(raw)

        lf_b = lfname.encode("utf-8") + b"\x00"
        dn_b = (dname.encode("utf-8") + b"\x00") if dname else b""
        var_ext = struct.pack("<BB", len(lf_b), len(dn_b)) + lf_b + dn_b

        de_off = len(out)
        de_len = 56 + len(var_ext)
        data_off = de_off + de_len
        next_off = data_off + len(comp)

        de = bytearray(de_len)
        struct.pack_into("<I BB II HHH II BB BB IH", de, 0,
                         0xFDC4A7DC, 2, method, next_off, data_off,
                         0x5B79, 0x6000, crc16_arc(raw),
                         len(raw), len(comp), 2, 1, 0, 0, 0, 0)
        sn_b = sname.encode("ascii")[:12]
        de[38 : 38 + len(sn_b)] = sn_b
        struct.pack_into("<HBH", de, 51, len(var_ext), 0, 0)
        de[56:] = var_ext
        out += de
        out += comp

    # Terminal EOF direntry (next == 0)
    eof_de = bytearray(56)
    struct.pack_into("<I BB", eof_de, 0, 0xFDC4A7DC, 2, 0)
    out += eof_de

    (out_dir / fname).write_bytes(out)
    return fname


# ==============================================================================
# 6. SEA ARC / PKPAK (.arc — Methods 2, 3, 4, 8, 9)
# ==============================================================================
def encode_arc_squeeze4(data: bytes) -> bytes:
    rle = encode_rle90(data)
    syms = sorted(set(rle)) + [256]  # 256 = SPEOF
    # Build simple right-leaning binary Huffman tree of internal nodes 0..len(syms)-2
    nodes: list[tuple[int, int]] = []
    codes: dict[int, list[int]] = {}
    for idx, s in enumerate(syms[:-1]):
        is_last_pair = (idx == len(syms) - 2)
        right = -(syms[-1] + 1) if is_last_pair else (idx + 1)
        nodes.append((-(s + 1), right))
        codes[s] = [1] * idx + [0]
    codes[syms[-1]] = [1] * (len(syms) - 1)

    out = bytearray(struct.pack("<H", len(nodes)))
    for lc, rc in nodes:
        out += struct.pack("<hh", lc, rc)

    cur_byte = 0
    bit_idx = 0
    for b in list(rle) + [256]:
        for bit in codes[b]:
            if bit:
                cur_byte |= 1 << bit_idx
            bit_idx += 1
            if bit_idx == 8:
                out.append(cur_byte)
                cur_byte = 0
                bit_idx = 0
    if bit_idx > 0:
        out.append(cur_byte)
    return bytes(out)


def gen_06_sea_arc(out_dir: Path) -> str:
    fname = "bbs_distro.arc"
    members = [
        ("README.DOC", 2, b"SEA ARC 5.21 Unpacked README Document.\r\n"),
        ("BANNER.TXT", 3, b"=" * 40 + b"\r\n  WELCOME TO EXPLORER2 BBS!  \r\n" + b"=" * 40 + b"\r\n"),
        ("SQUEEZE.TXT", 4, b"Richard Greenlaw USQ Huffman + RLE90 Squeeze Verified! " * 6),
        ("CRUNCH.TXT", 8, b"SEA ARC Method 8 Dynamic 12-bit LZW + RLE90 Crunch Verified! " * 12),
        ("SQUASH.TXT", 9, b"PKPAK Method 9 13-bit Squash LZW Stream Verified! " * 12),
    ]

    arc = bytearray()
    for name, method, raw in members:
        if method == 2:
            comp = raw
        elif method == 3:
            comp = encode_rle90(raw)
        elif method == 4:
            comp = encode_arc_squeeze4(raw)
        elif method == 8:
            comp = bytes([12]) + encode_unix_compress_lzw(encode_rle90(raw), 12, True)
        elif method == 9:
            comp = encode_unix_compress_lzw(raw, 13, True)
        else:
            raise ValueError(method)

        name13 = name.encode("ascii")[:12].ljust(13, b"\x00")
        arc += bytes([0x1A, method]) + name13
        arc += struct.pack("<I HH H I", len(comp), 0x5B79, 0x6000, crc16_arc(raw), len(raw))
        arc += comp

    arc += b"\x1A\x00"  # EOF marker
    (out_dir / fname).write_bytes(arc)
    return fname


# ==============================================================================
# 7. Aladdin StuffIt Classic (.sit — Folder 32/33 + Store 0, RLE90 1, LZW-14 2, Huff 3, Deflate 14)
# ==============================================================================
def encode_stuffit_huff3(data: bytes) -> bytes:
    bw = MsbBitWriter()
    syms = sorted(set(data))
    if len(syms) == 1:
        bw.write(1, 1)
        bw.write(syms[0], 8)
        raw, _ = bw.finish()
        return raw

    # Serialize right-leaning prefix tree: 0 = branch, 1 + 8b = leaf
    codes: dict[int, list[int]] = {}
    for idx, s in enumerate(syms[:-1]):
        bw.write(0, 1)  # internal branch
        bw.write(1, 1)  # left leaf
        bw.write(s, 8)
        codes[s] = [1] * idx + [0]
    bw.write(1, 1)  # final right leaf
    bw.write(syms[-1], 8)
    codes[syms[-1]] = [1] * (len(syms) - 1)

    for b in data:
        for bit in codes[b]:
            bw.write(bit, 1)
    raw, _ = bw.finish()
    return raw


def gen_07_stuffit_sit(out_dir: Path) -> str:
    fname = "mac_classic_folder.sit"

    def make_sit_hdr(rm: int, dm: int, name: str, d_uncomp: int, d_comp: int, d_crc: int) -> bytes:
        hdr = bytearray(112)
        hdr[0] = rm
        hdr[1] = dm
        nb = name.encode("mac_roman")[:63]
        hdr[2] = len(nb)
        hdr[3 : 3 + len(nb)] = nb
        hdr[66:70] = b"TEXT"
        hdr[70:74] = b"ttxt"
        mac_time = 2082844800 + 1764028800
        struct.pack_into(">II IIII HH", hdr, 76,
                         mac_time, mac_time,
                         0, d_uncomp, 0, d_comp,
                         0, d_crc)
        struct.pack_into(">H", hdr, 110, crc16_arc(bytes(hdr[:110])))
        return bytes(hdr)

    items = bytearray()
    # Root item 1: Folder "MacApp" (32 .. 33) containing 4 compressed files
    items += make_sit_hdr(32, 32, "MacApp", 0, 0, 0)

    folder_files = [
        ("Banner.txt", 1, b"*" * 32 + b"\rClassic Mac RLE90 StuffIt Fork!\r" + b"*" * 32),
        ("Manual.txt", 2, b"StuffIt Classic 14-bit LZW Data Fork Payload! " * 12),
        ("Huffman.txt", 3, b"StuffIt Prefix-Tree MSB Huffman Method 3 Verified! " * 8),
        ("Deflate.txt", 14, b"StuffIt Deluxe Method 14 Raw Deflate Stream Verified! " * 10),
    ]
    for nm, dm, raw in folder_files:
        if dm == 1:
            comp = encode_rle90(raw)
        elif dm == 2:
            comp = encode_unix_compress_lzw(raw, 14, True)
        elif dm == 3:
            comp = encode_stuffit_huff3(raw)
        elif dm == 14:
            co = zlib.compressobj(6, zlib.DEFLATED, -15)
            comp = co.compress(raw) + co.flush()
        else:
            comp = raw
        items += make_sit_hdr(0, dm, nm, len(raw), len(comp), crc16_arc(raw))
        items += comp

    items += make_sit_hdr(33, 33, "", 0, 0, 0)  # Folder End

    # Root item 2: Stored "About.txt"
    about_raw = b"StuffIt Root Stored File OK!\n"
    items += make_sit_hdr(0, 0, "About.txt", len(about_raw), len(about_raw), crc16_arc(about_raw))
    items += about_raw

    total_len = 22 + len(items)
    top = struct.pack(">4s H I 4s B 7s", b"SIT!", 2, total_len, b"rLau", 1, b"\x00" * 7)
    (out_dir / fname).write_bytes(top + bytes(items))
    return fname


# ==============================================================================
# 8. Nintendo SARC (.sarc LE & Yaz0-compressed .szs BE)
# ==============================================================================
def build_sarc_bytes(entries: list[tuple[str, bytes]], big_endian: bool) -> bytes:
    endian = ">" if big_endian else "<"
    hash_key = 0x00000065

    def sarc_hash(name: str) -> int:
        h = 0
        for ch in name.encode("utf-8"):
            h = (h * hash_key + ch) & 0xFFFFFFFF
        return h

    sorted_ents = sorted(entries, key=lambda kv: sarc_hash(kv[0]))
    sfnt_strs = bytearray()
    name_offsets = []
    for name, _ in sorted_ents:
        assert (len(sfnt_strs) & 3) == 0
        name_offsets.append(len(sfnt_strs) // 4)
        nb = name.encode("utf-8") + b"\x00"
        if len(nb) & 3:
            nb += b"\x00" * (4 - (len(nb) & 3))
        sfnt_strs += nb

    data_blob = bytearray()
    data_ranges = []
    for _, payload in sorted_ents:
        if len(data_blob) & 15:
            data_blob += b"\x00" * (16 - (len(data_blob) & 15))
        st = len(data_blob)
        data_blob += payload
        data_ranges.append((st, len(data_blob)))

    sarc_hdr_len = 20
    sfat_hdr_len = 12
    nodes_len = 16 * len(sorted_ents)
    sfnt_hdr_len = 8
    pre_data = sarc_hdr_len + sfat_hdr_len + nodes_len + sfnt_hdr_len + len(sfnt_strs)
    data_offset = (pre_data + 63) & ~63
    pad_before_data = data_offset - pre_data
    total_size = data_offset + len(data_blob)

    out = bytearray()
    out += b"SARC" + struct.pack(endian + "HHIIHH", sarc_hdr_len, 0xFEFF, total_size, data_offset, 0x0100, 0)
    out += b"SFAT" + struct.pack(endian + "HHI", sfat_hdr_len, len(sorted_ents), hash_key)
    for idx, (name, _) in enumerate(sorted_ents):
        out += struct.pack(
            endian + "4I",
            sarc_hash(name),
            0x01000000 | name_offsets[idx],
            data_ranges[idx][0],
            data_ranges[idx][1],
        )
    out += b"SFNT" + struct.pack(endian + "HH", sfnt_hdr_len, 0)
    out += sfnt_strs
    out += b"\x00" * pad_before_data
    out += data_blob
    return bytes(out)


def encode_yaz0(raw: bytes) -> bytes:
    """Real LZ77 Yaz0 big-endian compressor."""
    out = bytearray(b"Yaz0" + struct.pack(">III", len(raw), 0, 0))
    i = 0
    n = len(raw)
    while i < n:
        grp_pos = len(out)
        out.append(0)
        grp_byte = 0
        for bit in range(7, -1, -1):
            if i >= n:
                break
            best_l = 0
            best_d = 0
            w_start = max(0, i - 512)
            for j in range(i - 1, w_start - 1, -1):
                ml = 0
                while ml < 273 and i + ml < n and raw[j + ml] == raw[i + ml]:
                    ml += 1
                if ml >= 3 and ml > best_l:
                    best_l = ml
                    best_d = i - j
                    if best_l >= 64:
                        break
            if best_l >= 3:
                d_enc = best_d - 1
                if best_l < 18:
                    out.append(((best_l - 2) << 4) | ((d_enc >> 8) & 0x0F))
                    out.append(d_enc & 0xFF)
                else:
                    out.append((d_enc >> 8) & 0x0F)
                    out.append(d_enc & 0xFF)
                    out.append((best_l - 18) & 0xFF)
                i += best_l
            else:
                grp_byte |= 1 << bit
                out.append(raw[i])
                i += 1
        out[grp_pos] = grp_byte
    return bytes(out)


def gen_08_nintendo_sarc_and_szs(out_dir: Path) -> list[str]:
    sarc_be = build_sarc_bytes(
        [
            ("course/course_map.bin", b"KMP_HEADER_WIIU_BE_" + b"\x01\x02\x03\x04" * 32),
            ("course/course_info.json", b'{"track":"Mario_Circuit","laps":3,"engine":"SARC_BE"}\n'),
        ],
        big_endian=True,
    )
    szs_name = "wiiu_course_pack.szs"
    (out_dir / szs_name).write_bytes(encode_yaz0(sarc_be))

    sarc_le = build_sarc_bytes(
        [
            ("ui/hud_layout.bflyt", b"FLYT_SWITCH_LE_PAYLOAD_" * 8),
            ("ui/hud_intro.bflan", b"FLAN_SWITCH_LE_ANIM_" * 8),
        ],
        big_endian=False,
    )
    sarc_name = "switch_ui_pack.sarc"
    (out_dir / sarc_name).write_bytes(sarc_le)
    return [szs_name, sarc_name]


# ==============================================================================
# 9. Nintendo DS Nitro Archive (.narc)
# ==============================================================================
def gen_09_nintendo_narc(out_dir: Path) -> str:
    fname = "nds_pokemon_data.narc"
    f0 = b"NDS_SPECIES_BASE_STATS_RECORD_001\x00\x11\x22\x33"
    f1 = b"Welcome to NitroSDK NARC hierarchical FNT!\n"

    # GMIF image body (4B aligned)
    gmif_body = bytearray()
    fat = []
    for blob in (f0, f1):
        st = len(gmif_body)
        gmif_body += blob
        end = len(gmif_body)
        fat.append((st, end))
        if len(gmif_body) & 3:
            gmif_body += b"\x00" * (4 - (len(gmif_body) & 3))

    btaf = b"BTAF" + struct.pack("<IHH", 12 + len(fat) * 8, len(fat), 0)
    for st, end in fat:
        btaf += struct.pack("<II", st, end)

    # Hierarchical BTNF:
    # Dir 0 (Root 0xF000): subdirs "data" (0xF001) and "scripts" (0xF002), totalDirs = 3
    # Dir 1 (0xF001, parent 0xF000): firstFileId = 0 -> "stats.bin"
    # Dir 2 (0xF002, parent 0xF000): firstFileId = 1 -> "intro.txt"
    sub0 = bytes([0x80 | 4]) + b"data" + struct.pack("<H", 0xF001) + bytes([0x80 | 7]) + b"scripts" + struct.pack("<H", 0xF002) + b"\x00"
    sub1 = bytes([9]) + b"stats.bin" + b"\x00"
    sub2 = bytes([9]) + b"intro.txt" + b"\x00"

    main_tbl_len = 3 * 8
    off0 = main_tbl_len
    off1 = off0 + len(sub0)
    off2 = off1 + len(sub1)
    fnt_body = bytearray(
        struct.pack("<IHH", off0, 0, 3)
        + struct.pack("<IHH", off1, 0, 0xF000)
        + struct.pack("<IHH", off2, 1, 0xF000)
        + sub0 + sub1 + sub2
    )
    while len(fnt_body) & 3:
        fnt_body.append(0xFF)

    btnf = b"BTNF" + struct.pack("<I", 8 + len(fnt_body)) + bytes(fnt_body)
    gmif = b"GMIF" + struct.pack("<I", 8 + len(gmif_body)) + bytes(gmif_body)

    total_len = 16 + len(btaf) + len(btnf) + len(gmif)
    narc_hdr = b"NARC" + struct.pack("<HHIHH", 0xFFFE, 0x0100, total_len, 16, 3)
    (out_dir / fname).write_bytes(narc_hdr + btaf + btnf + gmif)
    return fname


# ==============================================================================
# 10. Valve Steam Legacy GCF v6 (.gcf)
# ==============================================================================
def gen_10_valve_gcf(out_dir: Path) -> str:
    fname = "steam_hl2_cache.gcf"
    block_size = 512
    block_count = 4
    term = 0xFFFFFFFF

    cfg_bytes = b'unbindall\nbind "w" "+forward"\ncl_showfps "1"\n'
    # Multi-cluster file spanning 3 non-contiguous clusters: 2 -> 0 -> 3!
    large_bytes = (
        b"CLUSTER_PART_A_" + b"A" * (512 - 15)
        + b"CLUSTER_PART_B_" + b"B" * (512 - 15)
        + b"CLUSTER_PART_C_TAIL_OK!\n"
    )
    assert 1024 < len(large_bytes) < 1536

    # Directory items:
    # 0: Root "" (firstChild = 1)
    # 1: Dir "hl2" (parent = 0, next = 0, firstChild = 2)
    # 2: Dir "cfg" (parent = 1, next = 3, firstChild = 4)
    # 3: Dir "materials" (parent = 1, next = 0, firstChild = 5)
    # 4: File "config.cfg" (parent = 2, next = 0, blockEntry = 0, cluster = 1)
    # 5: File "readme_large.txt" (parent = 3, next = 0, blockEntry = 1, clusters = 2 -> 0 -> 3)
    names = ["", "hl2", "cfg", "materials", "config.cfg", "readme_large.txt"]
    str_pool = bytearray()
    name_offs = []
    for s in names:
        name_offs.append(len(str_pool))
        str_pool += s.encode("utf-8") + b"\x00"
    while len(str_pool) & 3:
        str_pool.append(0)

    dir_items = [
        # (nameOff, itemSize, checksumIdx, flags, parentIdx, nextIdx, firstIdx)
        (name_offs[0], 2, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0, 1),
        (name_offs[1], 2, 0xFFFFFFFF, 0x00000000, 0, 0, 2),
        (name_offs[2], 1, 0xFFFFFFFF, 0x00000000, 1, 3, 4),
        (name_offs[3], 1, 0xFFFFFFFF, 0x00000000, 1, 0, 5),
        (name_offs[4], len(cfg_bytes), 0, 0x00004000, 2, 0, 0),
        (name_offs[5], len(large_bytes), 1, 0x00004000, 3, 0, 0),
    ]
    dir_entries_bin = b"".join(struct.pack("<7I", *it) for it in dir_items)
    dir_total_size = 28 + len(dir_entries_bin) + len(str_pool)
    dir_hdr = struct.pack("<7I", 4, 220, 6, len(dir_items), 2, 0x8000, dir_total_size)
    dir_sec = dir_hdr + dir_entries_bin + bytes(str_pool)

    # BlockEntries (4 * 28B):
    # BE 0 -> dirIdx 4 (config.cfg), cluster 1
    # BE 1 -> dirIdx 5 (readme_large.txt), cluster chain 2 -> 0 -> 3
    be_list = [
        (0x200F8000, 0, len(cfg_bytes), 1, block_count, block_count, 4),
        (0x200F8000, 0, len(large_bytes), 2, block_count, block_count, 5),
        (0, 0, 0, term, block_count, block_count, 0xFFFFFFFF),
        (0, 0, 0, term, block_count, block_count, 0xFFFFFFFF),
    ]
    be_hdr = struct.pack("<8I", block_count, 4, 3, 0, 0, 0, 0, 0)
    be_sec = be_hdr + b"".join(struct.pack("<7I", *b) for b in be_list)

    # FragMap: cluster 0 -> 3, cluster 1 -> term, cluster 2 -> 0, cluster 3 -> term
    frag_map = [3, term, 0, term]
    frag_hdr = struct.pack("<4I", block_count, 0, 1, 0)
    frag_sec = frag_hdr + b"".join(struct.pack("<I", x) for x in frag_map)

    # DirMap: [4, 4, 4, 4, 0, 1]
    dir_map_sec = struct.pack("<2I", 1, 0) + struct.pack("<6I", block_count, block_count, block_count, block_count, 0, 1)

    # Checksum section (empty 16-byte dummy body)
    csum_body = struct.pack("<4I", 0x14893721, 0x00000001, 0, 0)
    csum_sec = struct.pack("<2I", 1, len(csum_body)) + csum_body

    # Compute exact firstBlockOffset
    meta_before_data = 44 + len(be_sec) + len(frag_sec) + len(dir_sec) + len(dir_map_sec) + len(csum_sec) + 24
    first_blk_off = (meta_before_data + 511) & ~511
    pad_len = first_blk_off - meta_before_data

    db_csum = (block_count + block_size + first_blk_off + 4) & 0xFFFFFFFF
    db_hdr = struct.pack("<6I", 6, block_count, block_size, first_blk_off, 4, db_csum)

    # Populate clusters 0..3:
    # Cluster 2 = large[0:512], Cluster 0 = large[512:1024], Cluster 3 = large[1024:], Cluster 1 = cfg
    clusters = bytearray(block_count * block_size)
    clusters[2 * block_size : 3 * block_size] = large_bytes[0:512]
    clusters[0 * block_size : 1 * block_size] = large_bytes[512:1024]
    tail = large_bytes[1024:]
    clusters[3 * block_size : 3 * block_size + len(tail)] = tail
    clusters[1 * block_size : 1 * block_size + len(cfg_bytes)] = cfg_bytes

    total_file_size = first_blk_off + len(clusters)
    gcf_hdr = struct.pack("<11I", 1, 1, 6, 220, 6, 0, 0, total_file_size, block_size, block_count, 0)

    full = gcf_hdr + be_sec + frag_sec + dir_sec + dir_map_sec + csum_sec + db_hdr + (b"\x00" * pad_len) + bytes(clusters)
    assert len(full) == total_file_size
    (out_dir / fname).write_bytes(full)
    return fname


# ==============================================================================
# 11 & 15. Raw FAT16 Superfloppy (.img) & Apple UDIF LZFSE bvx2 (.dmg)
# ==============================================================================
def build_fat16_volume_bytes(vol_label: bytes, files: list[tuple[str, str, bytes]]) -> bytes:
    """Builds a real spec-compliant FAT16 volume with VFAT long filenames & subdirectories."""
    bps = 512
    spc = 2  # 1024 B/cluster
    bpc = bps * spc
    rsvd_sec = 4
    num_fats = 2
    root_ents = 64
    root_secs = (root_ents * 32) // bps  # 4
    # FAT16 requires CountOfClusters >= 4085! 4120 clusters * 1KB = ~4.1 MB (compresses to ~2 KB in LZFSE!)
    total_clusters = 4120
    fat_secs = ((total_clusters + 2) * 2 + bps - 1) // bps  # 17 sectors
    total_secs = rsvd_sec + num_fats * fat_secs + root_secs + total_clusters * spc

    img = bytearray(total_secs * bps)
    # Boot sector
    img[0:3] = b"\xEB\x3C\x90"
    img[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", img, 11,
                     bps, spc, rsvd_sec, num_fats, root_ents,
                     total_secs if total_secs < 65536 else 0,
                     0xF8, fat_secs, 32, 64, 0,
                     total_secs if total_secs >= 65536 else 0)
    img[36] = 0x80
    img[38] = 0x29
    struct.pack_into("<I", img, 39, 0x20261125)
    img[43:54] = vol_label.ljust(11, b" ")[:11]
    img[54:62] = b"FAT16   "
    img[510:512] = b"\x55\xAA"

    fat1_off = rsvd_sec * bps
    fat2_off = (rsvd_sec + fat_secs) * bps
    root_off = (rsvd_sec + num_fats * fat_secs) * bps
    data_off = root_off + root_secs * bps

    def set_fat16(cl: int, val: int):
        struct.pack_into("<H", img, fat1_off + cl * 2, val)
        struct.pack_into("<H", img, fat2_off + cl * 2, val)

    set_fat16(0, 0xFFF8)
    set_fat16(1, 0xFFFF)
    next_clus = 2

    def alloc_chain(payload: bytes) -> int:
        nonlocal next_clus
        if not payload:
            return 0
        n_cl = (len(payload) + bpc - 1) // bpc
        first = next_clus
        for k in range(n_cl):
            c = first + k
            nxt = 0xFFFF if k + 1 == n_cl else (c + 1)
            set_fat16(c, nxt)
            chunk = payload[k * bpc : (k + 1) * bpc]
            c_off = data_off + (c - 2) * bpc
            img[c_off : c_off + len(chunk)] = chunk
        next_clus += n_cl
        return first

    def make_vfat_entries(long_name: str, short11: bytes, attr: int, first_cl: int, fsize: int) -> bytes:
        chk = 0
        for b in short11:
            chk = (((chk & 1) << 7) + (chk >> 1) + b) & 0xFF
        u16 = [ord(c) for c in long_name] + [0x0000]
        while len(u16) % 13 != 0:
            u16.append(0xFFFF)
        n_slots = len(u16) // 13
        ents = bytearray()
        for seq in range(n_slots, 0, -1):
            part = u16[(seq - 1) * 13 : seq * 13]
            lfn = bytearray(32)
            lfn[0] = seq | (0x40 if seq == n_slots else 0)
            for idx, pos in enumerate([1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]):
                struct.pack_into("<H", lfn, pos, part[idx])
            lfn[11] = 0x0F
            lfn[13] = chk
            ents += lfn
        sfn = bytearray(32)
        sfn[0:11] = short11
        sfn[11] = attr
        struct.pack_into("<HH", sfn, 22, 0x6000, 0x5B79)
        struct.pack_into("<HI", sfn, 26, first_cl, fsize)
        ents += sfn
        return bytes(ents)

    # Group files by top-level directory ("" or subdir)
    subdirs: dict[str, bytearray] = {}
    root_buf = bytearray()

    for idx, (dir_name, leaf_name, raw) in enumerate(files):
        cl = alloc_chain(raw)
        short11 = f"FILE{idx:04d}BIN".encode("ascii")
        de = make_vfat_entries(leaf_name, short11, 0x20, cl, len(raw))
        if not dir_name:
            root_buf += de
        else:
            subdirs.setdefault(dir_name, bytearray()).extend(de)

    for d_idx, (dname, dbuf) in enumerate(subdirs.items()):
        # Pad directory cluster
        if len(dbuf) < bpc:
            dbuf.extend(b"\x00" * (bpc - len(dbuf)))
        dcl = alloc_chain(bytes(dbuf))
        dshort = f"SUBD{d_idx:04d}   ".encode("ascii")
        root_buf += make_vfat_entries(dname, dshort, 0x10, dcl, 0)

    img[root_off : root_off + len(root_buf)] = root_buf
    return bytes(img)


def encode_lzfse_bvx2_stream(raw: bytes) -> bytes:
    """Genuine Apple LZFSE `bvx2` bit-packed FSE + LZ77 compressor (with `bvx$` trailer)."""
    kLExtra = [0] * 16 + [2, 3, 5, 8]
    kLBase = list(range(16)) + [16, 20, 28, 60]
    kMExtra = [0] * 16 + [3, 5, 8, 11]
    kMBase = list(range(16)) + [16, 24, 56, 312]
    kDExtra = [i // 4 for i in range(64)]
    kDBase = []
    cur_d = 0
    for eb in kDExtra:
        kDBase.append(cur_d)
        cur_d += 1 << eb

    def find_sym(val: int, bases: list[int], extras: list[int]) -> tuple[int, int, int]:
        for s in range(len(bases) - 1, -1, -1):
            if val >= bases[s]:
                return s, val - bases[s], extras[s]
        return 0, 0, 0

    def encode_bvx2_freq_bits(f: int) -> tuple[int, int]:
        # Inverse of kFreqNbits / kFreqValue in LzfseDecode.h!
        small = {
            0: (0b00, 2),
            1: (0b10, 2),
            2: (0b001, 3),
            3: (0b101, 3),
            4: (0b00011, 5),
            5: (0b01011, 5),
            6: (0b10011, 5),
            7: (0b11011, 5),
        }
        if f in small:
            return small[f]
        if 8 <= f <= 23:
            return (((f - 8) << 4) | 0b0111, 8)
        return (((f - 24) << 4) | 0b1111, 14)

    def normalize_freqs(counts: list[int], total_states: int) -> list[int]:
        nz = [i for i, c in enumerate(counts) if c > 0]
        res = [0] * len(counts)
        if not nz:
            res[0] = total_states
            return res
        for i in nz:
            res[i] = 1
        cur_sum = len(nz)
        while cur_sum < total_states:
            rem = total_states - cur_sum
            best_i = max(
                (i for i in nz if res[i] <= rem),
                key=lambda i: (counts[i] / res[i], -res[i]),
            )
            cur_sum += res[best_i]
            res[best_i] *= 2
        assert cur_sum == total_states
        return res

    def build_fse_enc_map(freqs: list[int], n_states: int) -> list[list[tuple[int, int, int]]]:
        """Exact inverse of InitFseLitDecoderTable / InitFseValueDecoderTable:
        For each symbol, maps target decoder state `dst_state` -> `(prev_state_idx, bits_to_emit, k_bits)`.
        Wait: in FSE encoding, encoder holds current decoder state `s` (which will become `next_state` in decoder!)
        and finds the table entry `idx` for `sym` whose transition `[delta .. delta + (1<<k) - 1]` contains `s`!"""
        log_states = (n_states - 1).bit_length()
        by_sym: list[list[tuple[int, int, int, int]]] = [[] for _ in freqs]
        idx = 0
        for sym, f in enumerate(freqs):
            if f == 0:
                continue
            k = log_states - (f.bit_length() - 1)
            j0 = (2 * f) - (n_states >> k)
            for j in range(f):
                if j < j0:
                    nb = k
                    delta = ((f + j) << k) - n_states
                else:
                    nb = k - 1
                    delta = (j - j0) << (k - 1)
                # Decoder at state `idx` pulls `nb` bits (`0 .. (1<<nb)-1`) and transitions to `delta + bits`!
                by_sym[sym].append((idx, delta, delta + (1 << nb), nb))
                idx += 1
        return by_sym

    class ForwardFseBitStream:
        def __init__(self) -> None:
            self.bytes = bytearray()
            self.accum = 0
            self.nbits = 0

        def push(self, val: int, k: int) -> None:
            if k == 0:
                return
            self.accum |= (val & ((1 << k) - 1)) << self.nbits
            self.nbits += k
            while self.nbits >= 8:
                self.bytes.append(self.accum & 0xFF)
                self.accum >>= 8
                self.nbits -= 8

        def finish(self, empty_ok: bool = False) -> tuple[bytes, int]:
            if empty_ok and len(self.bytes) == 0 and self.nbits == 0:
                return b"", 0
            # Prepend 8 zero guard bytes at the front of the backward-read buffer
            if self.nbits == 0:
                return (b"\x00" * 8) + bytes(self.bytes), 0
            nbits_init = self.nbits - 8  # -7 .. -1
            self.bytes.append(self.accum & 0xFF)
            self.accum = 0
            self.nbits = 0
            return (b"\x00" * 8) + bytes(self.bytes), nbits_init

    def encode_bvx2_single_block(literals: bytes, matches: list[tuple[int, int, int]], raw_len: int) -> bytes:
        # Build frequency tables
        lit_counts = [0] * 256
        for b in literals:
            lit_counts[b] += 1
        l_counts = [0] * 20
        m_counts = [0] * 20
        d_counts = [0] * 64
        lmd_syms: list[tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]] = []
        for L, M, rawD in matches:
            ls = find_sym(L, kLBase, kLExtra)
            ms = find_sym(M, kMBase, kMExtra)
            ds = find_sym(rawD, kDBase, kDExtra)
            l_counts[ls[0]] += 1
            m_counts[ms[0]] += 1
            d_counts[ds[0]] += 1
            lmd_syms.append((ls, ms, ds))

        lit_freq = normalize_freqs(lit_counts, 1024)
        l_freq = normalize_freqs(l_counts, 64)
        m_freq = normalize_freqs(m_counts, 64)
        d_freq = normalize_freqs(d_counts, 256)

        lit_map = build_fse_enc_map(lit_freq, 1024)
        l_map = build_fse_enc_map(l_freq, 64)
        m_map = build_fse_enc_map(m_freq, 64)
        d_map = build_fse_enc_map(d_freq, 256)

        def fse_step(cur_state: int, sym: int, enc_map) -> tuple[int, int, int]:
            for idx_st, lo, hi, nb in enc_map[sym]:
                if lo <= cur_state < hi:
                    return idx_st, cur_state - lo, nb
            raise RuntimeError(f"FSE transition not found for sym={sym} state={cur_state}")

        lit_stream = ForwardFseBitStream()
        lit_states = [0, 0, 0, 0]
        if literals:
            order = []
            quad_end = len(literals) & ~3
            for q in range(0, quad_end, 4):
                order.extend([(0, literals[q]), (1, literals[q + 1]), (2, literals[q + 2]), (3, literals[q + 3])])
            rem_i = quad_end
            for lane in range(len(literals) - quad_end):
                order.append((lane, literals[rem_i + lane]))

            for lane, b_val in reversed(order):
                new_st, bits_v, nb = fse_step(lit_states[lane], b_val, lit_map)
                lit_stream.push(bits_v, nb)
                lit_states[lane] = new_st
        lit_bytes, lit_bits_init = lit_stream.finish(empty_ok=not literals)

        lmd_stream = ForwardFseBitStream()
        l_st, m_st, d_st = 0, 0, 0
        for (ls, ms, ds) in reversed(lmd_syms):
            d_st, ds_bits, ds_nb = fse_step(d_st, ds[0], d_map)
            lmd_stream.push((ds_bits << ds[2]) | ds[1], ds_nb + ds[2])

            m_st, ms_bits, ms_nb = fse_step(m_st, ms[0], m_map)
            lmd_stream.push((ms_bits << ms[2]) | ms[1], ms_nb + ms[2])

            l_st, ls_bits, ls_nb = fse_step(l_st, ls[0], l_map)
            lmd_stream.push((ls_bits << ls[2]) | ls[1], ls_nb + ls[2])

        lmd_bytes, lmd_bits_init = lmd_stream.finish(empty_ok=not lmd_syms)
        assert len(lit_bytes) <= 15 and len(lmd_bytes) <= 15

        all_freqs = l_freq + m_freq + d_freq + lit_freq
        assert len(all_freqs) == 360
        f_stream = ForwardFseBitStream()
        for fv in all_freqs:
            code_v, code_nb = encode_bvx2_freq_bits(fv)
            f_stream.push(code_v, code_nb)
        if f_stream.nbits > 0:
            f_stream.bytes.append(f_stream.accum & 0xFF)
        freq_bytes = bytes(f_stream.bytes)

        hdr_size = 32 + len(freq_bytes)
        v0 = (
            (len(literals) & 0xFFFFF)
            | ((len(lit_bytes) & 0xFFFFF) << 20)
            | ((len(matches) & 0xFFFFF) << 40)
            | (((lit_bits_init + 7) & 0x7) << 60)
        )
        v1 = (
            (lit_states[0] & 0x3FF)
            | ((lit_states[1] & 0x3FF) << 10)
            | ((lit_states[2] & 0x3FF) << 20)
            | ((lit_states[3] & 0x3FF) << 30)
            | ((len(lmd_bytes) & 0xFFFFF) << 40)
            | (((lmd_bits_init + 7) & 0x7) << 60)
        )
        v2 = (
            (hdr_size & 0xFFFFFFFF)
            | ((l_st & 0x3FF) << 32)
            | ((m_st & 0x3FF) << 42)
            | ((d_st & 0x3FF) << 52)
        )
        hdr = b"bvx2" + struct.pack("<IQQQ", raw_len, v0, v1, v2) + freq_bytes
        assert len(hdr) == hdr_size
        return hdr + lit_bytes + lmd_bytes

    out = bytearray()
    pos = 0
    n_raw = len(raw)
    while pos < n_raw:
        # Check for RLE run >= 6 bytes
        if pos + 6 <= n_raw and raw[pos : pos + 6] == bytes([raw[pos]]) * 6:
            b0 = raw[pos]
            end_p = pos + 6
            max_p = min(n_raw, pos + 2000)
            while end_p < max_p and raw[end_p] == b0:
                end_p += 1
            run_len = end_p - pos
            out += encode_bvx2_single_block(bytes([b0]), [(1, run_len - 1, 1)], run_len)
            pos = end_p
        else:
            end_p = pos + 1
            max_lit = min(n_raw, pos + 10)
            while end_p < max_lit:
                if end_p + 6 <= n_raw and raw[end_p : end_p + 6] == bytes([raw[end_p]]) * 6:
                    break
                end_p += 1
            seg = raw[pos:end_p]
            out += encode_bvx2_single_block(seg, [], len(seg))
            pos = end_p
    out += b"bvx$"
    return bytes(out)


def gen_11_and_15_fat_and_lzfse_dmg(out_dir: Path) -> list[str]:
    # 15. Standalone raw FAT16 .img
    fat_raw = build_fat16_volume_bytes(
        b"DOS_FLOPPY",
        [
            ("System", "AUTOEXEC.BAT", b"@ECHO OFF\r\nPATH C:\\DOS\r\nECHO FAT16_RAW_IMG_OK\r\n"),
            ("Docs", "LongFileName_Notes.txt", b"Raw FAT16 Superfloppy Image Inspection & Extraction Verified!\n" * 6),
        ],
    )
    img_name = "dos_boot_floppy.img"
    (out_dir / img_name).write_bytes(fat_raw)

    # 12. Apple UDIF .dmg with LZFSE bvx2 (0x80000007) compressed runs!
    dmg_vol = build_fat16_volume_bytes(
        b"MACOS_LZFSE",
        [
            ("Applications", "AppInfo.plist", b'<?xml version="1.0"?><plist><dict><key>CFBundleName</key><string>Explorer2</string></dict></plist>\n'),
            ("", "README.txt", b"Apple UDIF DMG LZFSE (0x80000007 bvx2) Decompression Verified!\n" * 8),
        ],
    )
    # Split into 32 KB runs: compress non-zero runs with LZFSE bvx2 (0x80000007) and all-zero runs with BT_ZERO (0)!
    data_fork = bytearray()
    mish_entries = bytearray()
    step_bytes = 32 * 1024
    n_runs = 0
    for byte_off in range(0, len(dmg_vol), step_bytes):
        seg = dmg_vol[byte_off : byte_off + step_bytes]
        sec_num = byte_off // 512
        sec_cnt = len(seg) // 512
        if any(seg):
            comp = encode_lzfse_bvx2_stream(seg)
            c_off = len(data_fork)
            data_fork += comp
            mish_entries += struct.pack(">IIQQQQ", 0x80000007, 0, sec_num, sec_cnt, c_off, len(comp))
        else:
            mish_entries += struct.pack(">IIQQQQ", 0x00000000, 0, sec_num, sec_cnt, 0, 0)
        n_runs += 1
    mish_entries += struct.pack(">IIQQQQ", 0xFFFFFFFF, 0, 0, 0, 0, 0)
    n_runs += 1

    total_secs = len(dmg_vol) // 512
    mish = bytearray(204)
    mish[0:4] = b"mish"
    struct.pack_into(">IQQQ", mish, 4, 1, 0, total_secs, 0)
    struct.pack_into(">I", mish, 200, n_runs)
    mish_b64 = base64.b64encode(bytes(mish) + bytes(mish_entries)).decode("ascii")

    xml_plist = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<plist version="1.0"><dict><key>resource-fork</key><dict><key>blkx</key><array>'
        f"<dict><key>Data</key><data>{mish_b64}</data></dict>"
        "</array></dict></dict></plist>\n"
    ).encode("utf-8")

    xml_off = len(data_fork)
    koly = bytearray(512)
    koly[0:4] = b"koly"
    struct.pack_into(">I H", koly, 4, 4, 512)
    struct.pack_into(">QQQ", koly, 24, 0, len(data_fork), 0)
    struct.pack_into(">QQ", koly, 216, xml_off, len(xml_plist))
    struct.pack_into(">Q", koly, 492, total_secs)

    dmg_name = "macos_installer_bvx2.dmg"
    (out_dir / dmg_name).write_bytes(bytes(data_fork) + xml_plist + bytes(koly))
    return [img_name, dmg_name]


# ==============================================================================
# 13. RAR 5.0 Compressed v5 LZ+Huffman + E8 & Delta Filters (.rar)
# ==============================================================================
def encode_vint(v: int) -> bytes:
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


def rar5_apply_e8_encode(raw: bytes) -> bytes:
    """Inverse of RAR5 FILTER_E8 (pre-encodes E8 relative calls to absolute addresses)."""
    buf = bytearray(raw)
    file_size = 0x1000000
    cur_pos = 0
    while cur_pos + 4 < len(buf):
        b = buf[cur_pos]
        cur_pos += 1
        if b == 0xE8:
            offset = cur_pos % file_size
            rel = struct.unpack_from("<I", buf, cur_pos)[0]
            addr = (rel + offset) & 0xFFFFFFFF
            # Ensure addr falls in [0 .. file_size) so decoder subtracts offset back to `rel`!
            if (addr & 0x80000000) == 0 and addr < file_size:
                struct.pack_into("<I", buf, cur_pos, addr)
            cur_pos += 4
    return bytes(buf)


def rar5_apply_delta_encode(raw: bytes, channels: int) -> bytes:
    """Inverse of RAR5 FILTER_DELTA: planarizes channels as `prev - cur`."""
    out = bytearray()
    for ch in range(channels):
        prev = 0
        for pos in range(ch, len(raw), channels):
            cur = raw[pos]
            out.append((prev - cur) & 0xFF)
            prev = cur
    return bytes(out)


def encode_rar5_compressed_stream(pre_filtered: bytes, filter_spec: tuple[int, int] | None) -> bytes:
    """Encodes a genuine RAR5 v50 Huffman+LZ77 block (`tablePresent=1, lastBlock=1`)."""
    # Tokenize into literals (0..255) and matches (len=4 -> slot 2 -> sym 264, dist <= 4 -> dSlot = dist-1)
    tokens: list[tuple[int, int]] = []
    if filter_spec is not None:
        tokens.append((256, 0))  # Register filter at offset 0 covering full file!

    i = 0
    n = len(pre_filtered)
    while i < n:
        matched = False
        for dist in (1, 2, 3, 4):
            if i >= dist and i + 4 <= n and pre_filtered[i : i + 4] == pre_filtered[i - dist : i - dist + 4]:
                ml = 4
                while ml < 9 and i + ml < n and pre_filtered[i + ml] == pre_filtered[i - dist + ml]:
                    ml += 1
                tokens.append((262 + (ml - 2), dist - 1))  # dSlot 0..3 -> distance = dSlot + 1 <= 4 (no bonus!)
                i += ml
                matched = True
                break
        if not matched:
            tokens.append((pre_filtered[i], 0))
            i += 1

    used_ld = sorted({t[0] for t in tokens})
    ld_lens = [0] * 306
    if len(used_ld) == 1:
        ld_lens[used_ld[0]] = 1
    else:
        k = 1
        while (1 << k) < len(used_ld):
            k += 1
        short_c = (1 << k) - len(used_ld)
        for idx, s in enumerate(used_ld):
            ld_lens[s] = (k - 1) if idx < short_c else k
    ld_codes = build_canonical_codes_msb(ld_lens)

    dd_lens = [2, 2, 2, 2] + [0] * 60
    dd_codes = build_canonical_codes_msb(dd_lens)
    ldd_lens = [1] + [0] * 15
    rd_lens = [1] + [0] * 43

    full_table = ld_lens + dd_lens + ldd_lens + rd_lens
    assert len(full_table) == 430

    # BD pre-tree: encode literal code lengths 0..14 directly with fixed 4-bit BD codes (16 symbols used -> 4 bits each!)
    bd_used = sorted(set(full_table))
    bd_lens = [0] * 20
    if len(bd_used) == 1:
        bd_lens[bd_used[0]] = 1
    else:
        bk = 1
        while (1 << bk) < len(bd_used):
            bk += 1
        bshort = (1 << bk) - len(bd_used)
        for idx, bs in enumerate(bd_used):
            bd_lens[bs] = (bk - 1) if idx < bshort else bk
    bd_codes = build_canonical_codes_msb(bd_lens)

    bw = MsbBitWriter()
    # 1. Write BD[20] 4-bit lengths
    for bl in bd_lens:
        assert bl < 15
        bw.write(bl, 4)
    # 2. Write 430 main lengths via BD
    for tl in full_table:
        cv, cl = bd_codes[tl]
        bw.write(cv, cl)
    # 3. Write LZ + Filter symbols
    for sym, dslot in tokens:
        cv, cl = ld_codes[sym]
        bw.write(cv, cl)
        if sym == 256 and filter_spec is not None:
            ftype, chans = filter_spec
            # blockStart = 0 (byteCount=1 -> 2b '00' + 8b '0')
            bw.write(0, 2)
            bw.write(0, 8)
            # blockLength = n (2 bytes LE -> 2b '01' + low8 + high8)
            bw.write(1, 2)
            bw.write(n & 0xFF, 8)
            bw.write((n >> 8) & 0xFF, 8)
            bw.write(ftype, 3)
            if ftype == 0:
                bw.write(chans - 1, 5)
        elif sym >= 262:
            dv, dl = dd_codes[dslot]
            bw.write(dv, dl)

    blk_data, last_valid_bits = bw.finish()
    blk_size = len(blk_data)
    assert blk_size < 65536
    # byteCount = 2 -> ((flags >> 3) & 3) == 1 -> 0x08; tablePresent=0x80, lastBlock=0x40
    blk_flags = 0x80 | 0x40 | 0x08 | ((last_valid_bits - 1) & 7)
    chk = (0x5A ^ blk_flags ^ (blk_size & 0xFF) ^ ((blk_size >> 8) & 0xFF)) & 0xFF
    return bytes([blk_flags, chk, blk_size & 0xFF, (blk_size >> 8) & 0xFF]) + blk_data


def gen_13_rar5_compressed(out_dir: Path) -> str:
    fname = "rar5_compressed_delta_e8.rar"

    def wrap_rar5_hdr(body: bytes, data: bytes = b"") -> bytes:
        sz_vint = encode_vint(len(body))
        crc = crc32_ieee(sz_vint + body)
        return struct.pack("<I", crc) + sz_vint + body + data

    # File 1: x86 code with E8 CALL instructions -> compressed with E8 filter (type=1)
    x86_raw = (
        b"\x55\x89\xE5"
        + b"\xE8\x20\x00\x00\x00"
        + b"\x90\x90\x90\x90\x90\x90\x90\x90"
        + b"\xE8\x10\x00\x00\x00"
        + b"\x31\xC0\x5D\xC3"
    ) * 4
    x86_comp = encode_rar5_compressed_stream(rar5_apply_e8_encode(x86_raw), (1, 1))

    # File 2: 2-channel 16-bit audio ramp -> compressed with 2-channel Delta filter (type=0, channels=2)
    delta_raw = b"".join(struct.pack("<HH", (k * 3) & 0xFFFF, (k * 5) & 0xFFFF) for k in range(64))
    delta_comp = encode_rar5_compressed_stream(rar5_apply_delta_encode(delta_raw, 2), (0, 2))

    out = bytearray(b"Rar!\x1A\x07\x01\x00")
    # Main Archive Header (Type=1, Flags=0, ArcFlags=0)
    out += wrap_rar5_hdr(encode_vint(1) + encode_vint(0) + encode_vint(0))

    for rel_name, raw, comp in [
        ("bin/x86_e8_stub.bin", x86_raw, x86_comp),
        ("audio/stereo_delta.pcm", delta_raw, delta_comp),
    ]:
        nb = rel_name.encode("utf-8")
        comp_info = (0 << 10) | (3 << 7) | 0  # algVer=0 (128KB), method=3, solid=0
        body = (
            encode_vint(2)                # Type = File
            + encode_vint(0x0002)         # HdrFlags = has DataSize
            + encode_vint(len(comp))      # DataSize
            + encode_vint(0x0004)         # FileFlags = has CRC32
            + encode_vint(len(raw))       # UnpackedSize
            + encode_vint(0x20)           # Attributes
            + struct.pack("<I", crc32_ieee(raw))
            + encode_vint(comp_info)
            + encode_vint(0)              # HostOS = Windows
            + encode_vint(len(nb))
            + nb
        )
        out += wrap_rar5_hdr(body, comp)

    # End of Archive Header (Type=5, Flags=0, EndFlags=0)
    out += wrap_rar5_hdr(encode_vint(5) + encode_vint(0) + encode_vint(0))
    (out_dir / fname).write_bytes(out)
    return fname


# ==============================================================================
# 14. 7-Zip Solid BCJ2 (0x0303011B) Archive (.7z)
# ==============================================================================
def encode_bcj2_4streams(raw: bytes, base_ip: int = 0) -> tuple[bytes, bytes, bytes, bytes]:
    """Exact 7-Zip `Bcj2Enc` 4-stream splitter (`main`, `call` BE, `jump` BE, `rc` range stream)."""
    main_s = bytearray()
    call_s = bytearray()
    jump_s = bytearray()

    # Exact 7-Zip LZMA/BCJ2 Range Encoder (`RangeEnc_EncodeBit`)
    rc_low = 0
    rc_range = 0xFFFFFFFF
    rc_cache = 0
    rc_cache_size = 1
    rc_bytes = bytearray()

    def shift_low():
        nonlocal rc_low, rc_cache, rc_cache_size
        high32 = (rc_low >> 32) & 0xFF
        if (rc_low & 0xFFFFFFFF) < 0xFF000000 or high32 != 0:
            temp = rc_cache
            while True:
                rc_bytes.append((temp + high32) & 0xFF)
                temp = 0xFF
                rc_cache_size -= 1
                if rc_cache_size == 0:
                    break
            rc_cache = ((rc_low >> 24) & 0xFF)
        rc_cache_size += 1
        rc_low = ((rc_low & 0x00FFFFFF) << 8)

    def encode_bit(probs: list[int], idx: int, bit: int):
        nonlocal rc_range, rc_low
        p = probs[idx]
        bound = (rc_range >> 11) * p
        if bit == 0:
            rc_range = bound
            probs[idx] = p + ((2048 - p) >> 5)
        else:
            rc_low += bound
            rc_range -= bound
            probs[idx] = p - (p >> 5)
        if rc_range < 0x01000000:
            rc_range <<= 8
            shift_low()

    probs = [1024] * 258
    prev_b = 0
    i = 0
    n = len(raw)
    while i < n:
        b = raw[i]
        main_s.append(b)
        i += 1
        if i == n:
            break

        is_call = (b == 0xE8)
        is_jmp = (b == 0xE9)
        is_jcc = (prev_b == 0x0F and (b & 0xF0) == 0x80)
        if not (is_call or is_jmp or is_jcc):
            prev_b = b
            continue

        prob_idx = prev_b if is_call else (256 if is_jmp else 257)
        if i + 4 <= n:
            encode_bit(probs, prob_idx, 1)
            rel = struct.unpack_from("<I", raw, i)[0]
            cur_ip = (base_ip + i + 4) & 0xFFFFFFFF
            abs_dest = (rel + cur_ip) & 0xFFFFFFFF
            if is_call:
                call_s += struct.pack(">I", abs_dest)
            else:
                jump_s += struct.pack(">I", abs_dest)
            prev_b = raw[i + 3]
            i += 4
        else:
            encode_bit(probs, prob_idx, 0)
            prev_b = b

    for _ in range(5):
        shift_low()
    return bytes(main_s), bytes(call_s), bytes(jump_s), bytes(rc_bytes)


def push_7z_num(buf: bytearray, val: int) -> None:
    first = 0
    mask = 0x80
    extra = bytearray()
    for i in range(8):
        if val < (1 << (7 * (i + 1))):
            first |= (val >> (8 * i)) & (mask - 1)
            buf.append(first)
            buf.extend(extra)
            return
        first |= mask
        mask >>= 1
        extra.append((val >> (8 * i)) & 0xFF)
    buf.append(0xFF)
    buf.extend(struct.pack("<Q", val))


def gen_14_7z_bcj2(out_dir: Path) -> str:
    fname = "win32_solid_bcj2.7z"
    exe_bytes = (
        b"MZ\x90\x00" + b"\x00" * 60
        + b"\x55\x89\xE5"
        + b"\xE8\x40\x00\x00\x00"           # CALL +0x40
        + b"\x85\xC0"
        + b"\x0F\x85\x24\x00\x00\x00"       # JNZ +0x24
        + b"\xE9\x80\x00\x00\x00"           # JMP +0x80
        + b"\xE8\xEC\xFF\xFF\xFF"           # CALL -0x14
        + b"\x31\xC0\x5D\xC3"
    ) * 3
    doc_bytes = b"7-Zip Solid BCJ2 (0x0303011B) 4-Stream Extraction Verified!\n"
    solid = exe_bytes + doc_bytes

    s_main, s_call, s_jump, s_rc = encode_bcj2_4streams(solid, 0)

    p_main = lzma.compress(s_main, format=lzma.FORMAT_RAW, filters=[{"id": lzma.FILTER_LZMA2, "dict_size": 65536}])
    lz1_flt = [{"id": lzma.FILTER_LZMA1, "lc": 3, "lp": 0, "pb": 2, "dict_size": 65536}]
    p_call = lzma.compress(s_call, format=lzma.FORMAT_RAW, filters=lz1_flt)
    p_jump = lzma.compress(s_jump, format=lzma.FORMAT_RAW, filters=lz1_flt)
    lz1_props = bytes([0x5D]) + struct.pack("<I", 65536)

    packed_blobs = p_main + p_call + p_jump + s_rc

    # Build kHeader (0x01)
    kh = bytearray([0x01, 0x04])  # kHeader, kMainStreamsInfo
    # PackInfo (0x06)
    kh.append(0x06)
    push_7z_num(kh, 0)
    push_7z_num(kh, 4)
    kh.append(0x09)
    for b in (p_main, p_call, p_jump, s_rc):
        push_7z_num(kh, len(b))
    kh.append(0x00)

    # UnPackInfo (0x07) -> 1 folder, 4 coders:
    #   [0] LZMA2 (0x21, 1->1, prop=0x14 for 64KB dict)
    #   [1] LZMA1 (0x030101, 1->1, 5B props)
    #   [2] LZMA1 (0x030101, 1->1, 5B props)
    #   [3] BCJ2  (0x0303011B, 4->1 complex)
    kh += bytes([0x07, 0x0B])
    push_7z_num(kh, 1)
    kh.append(0x00)  # external = 0
    push_7z_num(kh, 4)
    # Coder 0: LZMA2
    kh += bytes([0x21, 0x21, 0x01, 0x14])
    # Coder 1: LZMA1
    kh += bytes([0x23, 0x03, 0x01, 0x01, 0x05]) + lz1_props
    # Coder 2: LZMA1
    kh += bytes([0x23, 0x03, 0x01, 0x01, 0x05]) + lz1_props
    # Coder 3: BCJ2 (idSize=4, isComplex=0x10 -> 0x14)
    kh += bytes([0x14, 0x03, 0x03, 0x01, 0x1B])
    push_7z_num(kh, 4)  # numInStreams = 4 (indices 3, 4, 5, 6)
    push_7z_num(kh, 1)  # numOutStreams = 1 (index 3)
    # 3 bindPairs: (in=3, out=0), (in=4, out=1), (in=5, out=2)
    for in_idx, out_idx in ((3, 0), (4, 1), (5, 2)):
        push_7z_num(kh, in_idx)
        push_7z_num(kh, out_idx)
    # 4 packedStreamIndices: [0, 1, 2, 6]
    for pi in (0, 1, 2, 6):
        push_7z_num(kh, pi)

    # kCodersUnPackSize (0x0C): out0=len(s_main), out1=len(s_call), out2=len(s_jump), out3=len(solid)
    kh.append(0x0C)
    for usz in (len(s_main), len(s_call), len(s_jump), len(solid)):
        push_7z_num(kh, usz)
    kh.append(0x00)  # end UnPackInfo

    # SubStreamsInfo (0x08): 2 files in folder 0 + CRCs
    kh += bytes([0x08, 0x0D])
    push_7z_num(kh, 2)
    kh.append(0x09)
    push_7z_num(kh, len(exe_bytes))
    kh += bytes([0x0A, 0x01])  # kCRC, allDefined=1
    kh += struct.pack("<II", crc32_ieee(exe_bytes), crc32_ieee(doc_bytes))
    kh.append(0x00)  # end SubStreamsInfo
    kh.append(0x00)  # end MainStreamsInfo

    # FilesInfo (0x05)
    kh.append(0x05)
    push_7z_num(kh, 2)
    names_u16 = "bin/app_x86.exe\x00docs/readme.txt\x00".encode("utf-16le")
    kh.append(0x11)
    push_7z_num(kh, 1 + len(names_u16))
    kh.append(0x00)
    kh += names_u16
    kh.append(0x00)  # end FilesInfo
    kh.append(0x00)  # end kHeader

    # Compress kHeader into 0x17 kEncodedHeader via LZMA1
    packed_hdr = lzma.compress(bytes(kh), format=lzma.FORMAT_RAW, filters=lz1_flt)
    eh = bytearray([0x17, 0x06])
    push_7z_num(eh, len(packed_blobs))
    push_7z_num(eh, 1)
    eh.append(0x09)
    push_7z_num(eh, len(packed_hdr))
    eh += bytes([0x00, 0x07, 0x0B, 0x01, 0x00, 0x01, 0x23, 0x03, 0x01, 0x01, 0x05]) + lz1_props
    eh.append(0x0C)
    push_7z_num(eh, len(kh))
    eh += bytes([0x00, 0x00])

    next_hdr_off = len(packed_blobs) + len(packed_hdr)
    next_hdr_crc = crc32_ieee(bytes(eh))
    start_hdr = struct.pack("<QQI", next_hdr_off, len(eh), next_hdr_crc)
    sig_hdr = b"7z\xBC\xAF\x27\x1C\x00\x04" + struct.pack("<I", crc32_ieee(start_hdr)) + start_hdr

    (out_dir / fname).write_bytes(sig_hdr + packed_blobs + packed_hdr + bytes(eh))
    return fname


# ==============================================================================
# 16. NTFS Volume with Fragmented $MFT Runlist & File Beyond Record 2048 (.ntfs)
# ==============================================================================
def gen_16_ntfs_large_mft(out_dir: Path) -> str:
    fname = "ntfs_large_mft.ntfs"
    bps = 512
    spc = 2  # 1024 B/cluster, 1024 B per MFT record!
    bpc = 1024
    # Segment 0 of $MFT: LCN 4, 16 clusters (records 0..15)
    # Segment 1 of $MFT: LCN 30, 2050 clusters (records 16..2065)!
    # File payload for record 2060 at LCN 2100 (non-resident runlist)!
    total_clusters = 2110
    img = bytearray(total_clusters * bpc)

    # NTFS Boot Sector at LBA 0
    img[0:3] = b"\xEB\x52\x90"
    img[3:11] = b"NTFS    "
    struct.pack_into("<HB", img, 11, bps, spc)
    img[21] = 0xF8
    struct.pack_into("<Q", img, 40, total_clusters * spc - 1)
    struct.pack_into("<Q", img, 48, 4)     # $MFT LCN = 4
    struct.pack_into("<Q", img, 56, 2)     # $MFTMirr
    img[64] = 0xF6                         # -10 -> 2^10 = 1024 B MFT record!
    img[68] = 1
    img[510:512] = b"\x55\xAA"

    def apply_usa_fixup(rec: bytearray) -> bytes:
        usa_off = 48
        usa_cnt = 3  # seq + 2 sector words
        seq = 0x0001
        w0 = struct.unpack_from("<H", rec, 510)[0]
        w1 = struct.unpack_from("<H", rec, 1022)[0]
        struct.pack_into("<HH", rec, 4, usa_off, usa_cnt)
        struct.pack_into("<3H", rec, usa_off, seq, w0, w1)
        struct.pack_into("<H", rec, 510, seq)
        struct.pack_into("<H", rec, 1022, seq)
        return bytes(rec)

    def make_fn_attr(parent_mft: int, name: str, file_sz: int, is_dir: bool) -> bytes:
        u16 = name.encode("utf-16le")
        body = bytearray(66 + len(u16))
        struct.pack_into("<Q", body, 0, (1 << 48) | parent_mft)
        ft = 134000000000000000
        struct.pack_into("<4Q", body, 8, ft, ft, ft, ft)
        struct.pack_into("<QQ", body, 40, file_sz, file_sz)
        struct.pack_into("<I", body, 56, 0x10000000 if is_dir else 0x20)
        body[64] = len(name)
        body[65] = 1  # Win32 namespace
        body[66:] = u16
        val_off = 24
        tot = (val_off + len(body) + 7) & ~7
        attr = bytearray(tot)
        struct.pack_into("<IIBBHHHIHH", attr, 0, 0x30, tot, 0, 0, 0, 0, 1, len(body), val_off, 0)
        attr[val_off : val_off + len(body)] = body
        return bytes(attr)

    def encode_runlist(runs: list[tuple[int, int]]) -> bytes:
        """runs = [(length_clusters, abs_lcn), ...]"""
        out = bytearray()
        prev_lcn = 0
        for length, lcn in runs:
            dlcn = lcn - prev_lcn
            prev_lcn = lcn
            lb = length.to_bytes(4, "little", signed=False).rstrip(b"\x00") or b"\x00"
            # Signed minimal little-endian for dlcn
            for nb in range(1, 5):
                lo = -(1 << (8 * nb - 1))
                hi = (1 << (8 * nb - 1)) - 1
                if lo <= dlcn <= hi:
                    ob = dlcn.to_bytes(nb, "little", signed=True)
                    break
            out.append((len(ob) << 4) | len(lb))
            out += lb + ob
        out.append(0x00)
        return bytes(out)

    def make_nonres_data_attr(runs: list[tuple[int, int]], real_size: int, total_clus: int) -> bytes:
        rl = encode_runlist(runs)
        run_off = 64
        tot = (run_off + len(rl) + 7) & ~7
        attr = bytearray(tot)
        struct.pack_into("<IIBBHHH", attr, 0, 0x80, tot, 1, 0, 0, 0, 2)
        struct.pack_into("<QQHH", attr, 16, 0, total_clus - 1, run_off, 0)
        alloc = total_clus * bpc
        struct.pack_into("<QQQ", attr, 40, alloc, real_size, real_size)
        attr[run_off : run_off + len(rl)] = rl
        return bytes(attr)

    def build_mft_record(flags: int, attrs: bytes) -> bytes:
        rec = bytearray(1024)
        rec[0:4] = b"FILE"
        attr_off = 56
        end_pos = attr_off + len(attrs) + 8
        struct.pack_into("<HHHH", rec, 16, 1, 1, attr_off, flags)
        struct.pack_into("<II", rec, 24, end_pos, 1024)
        rec[attr_off : attr_off + len(attrs)] = attrs
        struct.pack_into("<I", rec, attr_off + len(attrs), 0xFFFFFFFF)
        return apply_usa_fixup(rec)

    # Record 0 ($MFT): fragmented runlist [(16, 4), (2050, 30)] -> 2,066 total MFT records!
    mft_data_attr = make_nonres_data_attr([(16, 4), (2050, 30)], 2066 * 1024, 2066)
    img[4 * bpc : 5 * bpc] = build_mft_record(0x01, mft_data_attr)

    # Record 5 (Root "." directory)
    img[(4 + 5) * bpc : (4 + 6) * bpc] = build_mft_record(0x03, make_fn_attr(5, ".", 0, True))

    # Record 2055 (Directory "DeepRecords" inside Root 5 -> located in segment 1 at LCN 30 + (2055 - 16)!)
    rec_2055_off = (30 + (2055 - 16)) * bpc
    img[rec_2055_off : rec_2055_off + bpc] = build_mft_record(0x03, make_fn_attr(5, "DeepRecords", 0, True))

    # Record 2060 (File "DeepRecords/record_2060.txt" > MFT 2048!)
    deep_payload = b"NTFS Fragmented $MFT Runlist Record #2060 (> 2048) Extracted Successfully!\n" * 6
    img[2100 * bpc : 2100 * bpc + len(deep_payload)] = deep_payload
    rec_2060_off = (30 + (2060 - 16)) * bpc
    f_attrs = make_fn_attr(2055, "record_2060.txt", len(deep_payload), False) + make_nonres_data_attr(
        [(1, 2100)], len(deep_payload), 1
    )
    img[rec_2060_off : rec_2060_off + bpc] = build_mft_record(0x01, f_attrs)

    (out_dir / fname).write_bytes(img)
    return fname


# ==============================================================================
# 17. ZIP64 + BZip2 (12) + LZMA (14) + Deflate (8) + 0-byte Entry (.zip)
# ==============================================================================
def gen_17_zip64_multicodec(out_dir: Path) -> str:
    fname = "zip64_multicodec.zip"
    lz_flt = [{"id": lzma.FILTER_LZMA1, "lc": 3, "lp": 0, "pb": 2, "dict_size": 65536}]
    lz_props = bytes([0x5D]) + struct.pack("<I", 65536)

    raw_bz = b"ZIP64 BZip2 (Method 12) Payload Verified OK!\n" * 10
    comp_bz = bz2.compress(raw_bz, 9)

    raw_lz = b"ZIP64 PKWARE LZMA (Method 14) Payload Verified OK!\n" * 10
    # PKWARE LZMA method 14 header: [verMajor=19, verMinor=0, propSize=5 LE16, props[5]] + raw LZMA1
    comp_lz = struct.pack("<BBH", 19, 0, 5) + lz_props + lzma.compress(raw_lz, format=lzma.FORMAT_RAW, filters=lz_flt)

    raw_df = b"ZIP64 Deflate (Method 8) Payload Verified OK!\n" * 6
    co = zlib.compressobj(6, zlib.DEFLATED, -15)
    comp_df = co.compress(raw_df) + co.flush()

    entries = [
        ("codecs/bzip2_file.txt", 12, raw_bz, comp_bz),
        ("codecs/lzma_file.txt", 14, raw_lz, comp_lz),
        ("codecs/deflate_file.txt", 8, raw_df, comp_df),
        ("empty_zero.txt", 0, b"", b""),
    ]

    body = bytearray()
    cd = bytearray()
    for name, method, raw, comp in entries:
        loc_off = len(body)
        nb = name.encode("utf-8")
        crc = crc32_ieee(raw)
        # ZIP64 extra field (0x0001): 8B uncompSize + 8B compSize (+ 8B localOff in CD)
        loc_extra = struct.pack("<HHQQ", 0x0001, 16, len(raw), len(comp))
        body += struct.pack(
            "<I HHH HH III HH",
            0x04034B50, 45, 0x0800, method, 0x6000, 0x5B79,
            crc, 0xFFFFFFFF, 0xFFFFFFFF, len(nb), len(loc_extra),
        ) + nb + loc_extra + comp

        cd_extra = struct.pack("<HHQQQ", 0x0001, 24, len(raw), len(comp), loc_off)
        cd += struct.pack(
            "<I HHHHHH III HHH HH II",
            0x02014B50, 45, 45, 0x0800, method, 0x6000, 0x5B79,
            crc, 0xFFFFFFFF, 0xFFFFFFFF, len(nb), len(cd_extra), 0,
            0, 0, 0x20, 0xFFFFFFFF,
        ) + nb + cd_extra

    cd_off = len(body)
    cd_sz = len(cd)
    eocd64_off = cd_off + cd_sz
    eocd64 = struct.pack(
        "<I Q HH II QQQQ",
        0x06064B50, 44, 45, 45, 0, 0,
        len(entries), len(entries), cd_sz, cd_off,
    )
    locator64 = struct.pack("<I I Q I", 0x06074B50, 0, eocd64_off, 1)
    eocd32 = struct.pack("<I HHHH II H", 0x06054B50, 0, 0, 0xFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)

    (out_dir / fname).write_bytes(bytes(body) + bytes(cd) + eocd64 + locator64 + eocd32)
    return fname


# ==============================================================================
# 18. Hybrid ISO 9660 Bridge Stub + Full UDF 1.02 Optical Image (.iso)
# ==============================================================================
def gen_18_hybrid_udf_iso(out_dir: Path) -> str:
    fname = "hybrid_udf_bridge.iso"
    sec_sz = 2048
    total_secs = 280
    img = bytearray(total_secs * sec_sz)

    # --- Part A: ISO 9660 Bridge Stub (only 1 file: README.TXT at LBA 24) ---
    bridge_txt = b"This disc contains a UDF file system and requires an operating system that supports UDF.\r\n"
    img[24 * sec_sz : 24 * sec_sz + len(bridge_txt)] = bridge_txt

    def iso_both32(v: int) -> bytes:
        return struct.pack("<I", v) + struct.pack(">I", v)

    def iso_both16(v: int) -> bytes:
        return struct.pack("<H", v) + struct.pack(">H", v)

    def iso_dir_rec(lba: int, length: int, flags: int, ident: bytes) -> bytes:
        rec_len = 33 + len(ident) + (0 if (len(ident) & 1) else 1)
        r = bytearray(rec_len)
        r[0] = rec_len
        r[2:10] = iso_both32(lba)
        r[10:18] = iso_both32(length)
        r[25] = flags
        r[28:32] = iso_both16(1)
        r[32] = len(ident)
        r[33 : 33 + len(ident)] = ident
        return bytes(r)

    iso_root_lba = 22
    iso_root = bytearray(sec_sz)
    r_dot = iso_dir_rec(iso_root_lba, sec_sz, 0x02, b"\x00")
    r_dotdot = iso_dir_rec(iso_root_lba, sec_sz, 0x02, b"\x01")
    r_readme = iso_dir_rec(24, len(bridge_txt), 0x00, b"README.TXT;1")
    cur_r = 0
    for rec in (r_dot, r_dotdot, r_readme):
        iso_root[cur_r : cur_r + len(rec)] = rec
        cur_r += len(rec)
    img[iso_root_lba * sec_sz : (iso_root_lba + 1) * sec_sz] = iso_root

    pvd = bytearray(sec_sz)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[40:72] = b"UDF_BRIDGE_STUB".ljust(32, b" ")
    pvd[80:88] = iso_both32(total_secs)
    pvd[120:124] = iso_both16(1)
    pvd[124:128] = iso_both16(1)
    pvd[128:132] = iso_both16(sec_sz)
    pvd[156 : 156 + 34] = iso_dir_rec(iso_root_lba, sec_sz, 0x02, b"\x00")
    img[16 * sec_sz : 17 * sec_sz] = pvd

    tvd = bytearray(sec_sz)
    tvd[0] = 255
    tvd[1:6] = b"CD001"
    tvd[6] = 1
    img[17 * sec_sz : 18 * sec_sz] = tvd

    # --- Part B: ECMA-167 / UDF 1.02 VRS + AVDP + MVDS + FSD + 3 Real Files ---
    img[18 * sec_sz : 18 * sec_sz + 7] = b"\x00BEA01\x01"
    img[19 * sec_sz : 19 * sec_sz + 7] = b"\x00NSR02\x01"
    img[20 * sec_sz : 20 * sec_sz + 7] = b"\x00TEA01\x01"

    def stamp_udf_tag(buf: bytearray, tag_id: int, loc_lba: int) -> bytes:
        struct.pack_into("<HH BB HHH I", buf, 0, tag_id, 2, 0, 0, 0, 0, 0, loc_lba)
        s = sum(buf[i] for i in range(16) if i != 4) & 0xFF
        buf[4] = s
        return bytes(buf)

    def dstring(text: str, field_len: int) -> bytes:
        b = b"\x08" + text.encode("latin-1")[: field_len - 2]
        out = bytearray(field_len)
        out[0 : len(b)] = b
        out[field_len - 1] = len(b)
        return bytes(out)

    # Physical Partition 0 starts at sector 40 (length 200 sectors)
    part_start = 40
    part_len = 200

    # MVDS at sectors 32..35: Partition Descriptor (tag 5) at 32, LVD (tag 6) at 33, Term (tag 8) at 34
    pd = bytearray(sec_sz)
    struct.pack_into("<H", pd, 22, 0)  # PartitionNumber = 0
    struct.pack_into("<II", pd, 188, part_start, part_len)
    img[32 * sec_sz : 33 * sec_sz] = stamp_udf_tag(pd, 5, 32)

    lvd = bytearray(sec_sz)
    lvd[84:212] = dstring("HYBRID_UDF_DISC", 128)
    struct.pack_into("<I", lvd, 212, sec_sz)
    struct.pack_into("<H", lvd, 240, 0x0102)
    # FSD LongAD at +248: len=2048, lba=0, partRef=0
    struct.pack_into("<IIH", lvd, 248, sec_sz, 0, 0)
    # Type 1 Partition Map (6 bytes): [type=1, len=6, volSeq=1, partNum=0]
    struct.pack_into("<II", lvd, 264, 6, 1)
    lvd[440:446] = struct.pack("<BBHH", 1, 6, 1, 0)
    img[33 * sec_sz : 34 * sec_sz] = stamp_udf_tag(lvd, 6, 33)

    td = bytearray(sec_sz)
    img[34 * sec_sz : 35 * sec_sz] = stamp_udf_tag(td, 8, 34)

    # AVDP (tag 2) at sector 256 pointing to MVDS (loc=32, len=4*2048)
    avdp = bytearray(sec_sz)
    struct.pack_into("<IIII", avdp, 16, 4 * sec_sz, 32, 4 * sec_sz, 32)
    img[256 * sec_sz : 257 * sec_sz] = stamp_udf_tag(avdp, 2, 256)

    def put_part_lba(lba: int, block: bytes):
        off = (part_start + lba) * sec_sz
        img[off : off + len(block)] = block

    # Partition LBA 0: File Set Descriptor (tag 256), Root ICB at LBA 1
    fsd = bytearray(sec_sz)
    struct.pack_into("<IIH", fsd, 400, sec_sz, 1, 0)  # RootDirectoryICB at lba=1, partRef=0
    put_part_lba(0, stamp_udf_tag(fsd, 256, 0))

    def make_fid(lba_parent_tag: int, is_dir: bool, is_parent: bool, child_lba: int, name: str) -> bytes:
        comp_name = b"" if is_parent else (b"\x08" + name.encode("latin-1"))
        l_fi = len(comp_name)
        fid_len = (38 + l_fi + 3) & ~3
        fid = bytearray(fid_len)
        chars = (0x02 if is_dir else 0) | (0x08 if is_parent else 0)
        fid[18] = chars
        fid[19] = l_fi
        struct.pack_into("<IIH", fid, 20, sec_sz, child_lba, 0)
        fid[38 : 38 + l_fi] = comp_name
        return stamp_udf_tag(fid, 257, lba_parent_tag)

    def make_fe(lba: int, file_type: int, inline_or_ext: bytes, data_lba: int | None, info_len: int) -> bytes:
        fe = bytearray(sec_sz)
        fe[27] = file_type  # 4 = directory, 5 = regular file
        if data_lba is None:
            # Inline ICB (allocType = 3)
            struct.pack_into("<H", fe, 34, 3)
            struct.pack_into("<Q", fe, 56, len(inline_or_ext))
            struct.pack_into("<II", fe, 168, 0, len(inline_or_ext))
            fe[176 : 176 + len(inline_or_ext)] = inline_or_ext
        else:
            # ShortAD (allocType = 0): [extLen:4][extLba:4]
            struct.pack_into("<H", fe, 34, 0)
            struct.pack_into("<Q", fe, 56, info_len)
            struct.pack_into("<II", fe, 168, 0, 8)
            struct.pack_into("<II", fe, 176, info_len, data_lba)
        return stamp_udf_tag(fe, 261, lba)

    # UDF Files:
    # LBA 1: Root Dir FE -> Setup/ (LBA 2), udf_guide.txt (LBA 5), license.txt (LBA 6)
    # LBA 2: Setup Dir FE -> autorun.inf (LBA 3), payload.bin (LBA 4)
    f_autorun = b"[autorun]\r\nopen=setup.exe\r\nlabel=UDF_Full_Filesystem\r\n"
    f_payload = b"UDF_PAYLOAD_STREAM_OK_" * 40  # Stored externally at LBA 10
    f_guide = b"Hybrid ISO+UDF Disc: UDF 1.02 inspected instead of 1-file ISO9660 bridge!\n"
    f_license = b"MIT License - Deterministic Sample UDF Image.\n"

    put_part_lba(10, f_payload.ljust(sec_sz, b"\x00"))
    put_part_lba(3, make_fe(3, 5, f_autorun, None, len(f_autorun)))
    put_part_lba(4, make_fe(4, 5, b"", 10, len(f_payload)))
    put_part_lba(5, make_fe(5, 5, f_guide, None, len(f_guide)))
    put_part_lba(6, make_fe(6, 5, f_license, None, len(f_license)))

    setup_fids = make_fid(2, True, True, 1, "") + make_fid(2, False, False, 3, "autorun.inf") + make_fid(2, False, False, 4, "payload.bin")
    put_part_lba(2, make_fe(2, 4, setup_fids, None, len(setup_fids)))

    root_fids = (
        make_fid(1, True, True, 1, "")
        + make_fid(1, True, False, 2, "Setup")
        + make_fid(1, False, False, 5, "udf_guide.txt")
        + make_fid(1, False, False, 6, "license.txt")
    )
    put_part_lba(1, make_fe(1, 4, root_fids, None, len(root_fids)))

    (out_dir / fname).write_bytes(img)
    return fname



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


def build_ensemble_bar(root_dir: str, rel_name: str, payload: bytes) -> bytes:
    hdr = bytearray(0x124)
    hdr[0:4] = b"ESPN"
    struct.pack_into("<II", hdr, 4, 2, 0x44332211)
    f_off = 0x124
    ftr_off = f_off + len(payload)

    root_u16 = root_dir.encode("utf-16-le")
    name_u16 = rel_name.encode("utf-16-le")
    rec_rel = 4 + len(root_u16) + 4 + 4
    rec = struct.pack("<IIIIII", f_off, len(payload), len(payload), 0, 0, len(rel_name)) + name_u16
    ftr = struct.pack("<I", len(root_dir)) + root_u16 + struct.pack("<II", 1, rec_rel) + rec

    struct.pack_into("<IIII", hdr, 0x114, 1, 0, len(ftr), ftr_off)
    return bytes(hdr) + payload + ftr

def gen_domain_renpy_rpa(out_dir: Path):
    xor_key = 0x5A3C9E71
    # Header is fixed 34 bytes: "RPA-3.0 %016x %08x\n"
    entries_raw = [
        ("game/script.rpy", b"label start:\n", b"    scene bg clubroom\n    \"Monika\" \"Welcome to Explorer2!\"\n    return\n"),
        ("game/screens.rpy", b"", b"screen main_menu():\n    tag menu\n    textbutton \"Start\" action Start()\n"),
        ("images/ui/logo_meta.txt", b"PNG_META:", b"width=256;height=128;format=RGBA8\n"),
    ]
    body = bytearray(b"RPA-3.0 0000000000000000 00000000\n")
    assert len(body) == 34
    index = {}
    for path_str, prefix_b, tail_b in entries_raw:
        body += b"\x00\xAA"  # deterministic padding between blobs
        off = len(body)
        body += tail_b
        total_len = len(prefix_b) + len(tail_b)
        index[path_str] = [(off ^ xor_key, total_len ^ xor_key, prefix_b)]

    idx_off = len(body)
    hdr = f"RPA-3.0 {idx_off:016x} {xor_key:08x}\n".encode("ascii")
    assert len(hdr) == 34
    body[0:34] = hdr
    z_idx = zlib.compress(_pickle_legacy.dumps(index, protocol=4), 9)
    (out_dir / "visual_novel_assets.rpa").write_bytes(bytes(body) + z_idx)

def gen_domain_descent_hog(out_dir: Path):
    files = [
        ("level01.rdl", b"PLGP\x06\x00\x00\x00DESCENT_MINE_GEOMETRY_V1\x00\x01\x02\x03"),
        ("level01.hxm", b"HMX1\x02\x00\x00\x00ROBOT_POLYMORPH_AI_TABLE"),
        ("briefing.txb", b"PTRO_BRIEFING: Destroy the reactor core in Sector 7 and escape!\n"),
    ]
    out = bytearray(b"DHF")
    for name, payload in files:
        nb = name.encode("ascii")[:12]
        out += nb.ljust(13, b"\x00")
        out += struct.pack("<I", len(payload))
        out += payload
    (out_dir / "descent_mines.hog").write_bytes(bytes(out))

def gen_domain_volition_vpp(out_dir: Path):
    def pad2048(b: bytearray):
        rem = len(b) % 2048
        if rem:
            b += b"\x00" * (2048 - rem)

    files = [
        ("weapons.tbl", b"#Primary Weapons\n$Name: \"Rail Driver\"\n$Mass: 0.4\n$Velocity: 275.0\n#End\n"),
        ("mars_colony.rfl", b"\xCE\x0A\x89\x51RFL_GEOMOD_SECTOR_HEADER_V1\x00\x10\x20\x30"),
        ("hud_reticle.tga", bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0]) + b"\xFF\x00\x00" * 4),
    ]
    dir_blob = bytearray()
    for name, payload in files:
        nb = name.encode("ascii")[:59]
        dir_blob += nb.ljust(60, b"\x00") + struct.pack("<I", len(payload))
    pad2048(dir_blob)

    data_blob = bytearray()
    for _, payload in files:
        data_blob += payload
        pad2048(data_blob)

    total_size = 2048 + len(dir_blob) + len(data_blob)
    hdr_sec = bytearray(struct.pack("<IIII", 0x51890ACE, 1, len(files), total_size))
    pad2048(hdr_sec)
    assert len(hdr_sec) == 2048
    (out_dir / "red_faction_mars.vpp").write_bytes(bytes(hdr_sec + dir_blob + data_blob))

def gen_domain_westwood_mix(out_dir: Path):
    def hash_td(name: str) -> int:
        up = name.upper().encode("ascii")
        h = 0
        i = 0
        while i < len(up):
            grp = 0
            for b in range(4):
                if i < len(up):
                    grp |= up[i] << (8 * b)
                    i += 1
            h = (((h << 1) | (h >> 31)) + grp) & 0xFFFFFFFF
        return h

    # Build Olaf van der Spek XCC "local mix database.dat" entry so custom mod names resolve!
    custom_names = ["rules.ini", "grizzly_vox.ini", "prism_tower.aud", "local mix database.dat"]
    xcc_hdr = b"XCC by Olaf van der Spek\x1a\x04\x17\x27\x10\x19\x80\x00"  # 32 bytes
    assert len(xcc_hdr) == 32
    names_blob = b"".join(nm.encode("ascii") + b"\x00" for nm in custom_names)
    lmd_bytes = xcc_hdr + struct.pack("<5I", 52 + len(names_blob), 0, 0, 0, len(custom_names)) + names_blob

    raw_items = [
        ("rules.ini", b"[General]\nName=Allied Assault\nBuildSpeed=0.8\n[MTNK]\nStrength=400\n"),
        ("grizzly_vox.ini", b"[MTNK]\nImage=GTNK\nVoxel=yes\nPrimary=105mm\n"),
        ("prism_tower.aud", b"WESTWOOD_IMA_ADPCM_TEST_STREAM_0123456789ABCDEF"),
        ("local mix database.dat", lmd_bytes),
    ]
    # Sort SubBlocks by signed/unsigned 32-bit hash ID as standard MIX tools do
    indexed = []
    body = bytearray()
    for nm, pay in raw_items:
        off = len(body)
        body += pay
        indexed.append((hash_td(nm), off, len(pay)))
    indexed.sort(key=lambda t: t[0])

    # Flags-prefixed RA/TS MIX header: u16(0), u16(flags=0), u16(count), u32(bodySize)
    mix_hdr = struct.pack("<HHHI", 0, 0, len(indexed), len(body))
    subblocks = b"".join(struct.pack("<III", cid, roff, rlen) for (cid, roff, rlen) in indexed)
    (out_dir / "conquer_allied.mix").write_bytes(mix_hdr + subblocks + bytes(body))

def gen_domain_volition_vp(out_dir: Path):
    f1 = b"$Name: GTF Ulysses\n$Max Velocity: 0.0, 0.0, 70.0\n"
    f2 = b"#Mission Info\n$Name: Surrender Belisarius!\n"
    off1 = 16
    off2 = off1 + len(f1)
    dir_off = off2 + len(f2)

    def vp_ent(off: int, sz: int, name: bytes, ts: int) -> bytes:
        return struct.pack("<ii32sI", off, sz, name.ljust(32, b"\x00"), ts)

    entries = [
        vp_ent(0, 0, b"data", 0),
        vp_ent(0, 0, b"tables", 0),
        vp_ent(off1, len(f1), b"ships.tbl", 938736000),
        vp_ent(0, 0, b"..", 0),
        vp_ent(0, 0, b"missions", 0),
        vp_ent(off2, len(f2), b"sm1-01.fs2", 938739600),
        vp_ent(0, 0, b"..", 0),
        vp_ent(0, 0, b"..", 0),
    ]
    hdr = struct.pack("<4siii", b"VPVP", 2, dir_off, len(entries))
    (out_dir / "freespace2_root.vp").write_bytes(hdr + f1 + f2 + b"".join(entries))

def gen_domain_lucasarts_gob(out_dir: Path):
    cog_txt = b"# Jedi Knight COG Script\nsymbols\nmessage startup\nend\ncode\nstartup: Return;\nend\n"
    dat_txt = b"SABER_BLADE_COLOR=GREEN\n"
    hdr_sz = 16 + 2 * 136
    off1 = hdr_sz
    off2 = off1 + len(cog_txt)
    hdr = struct.pack("<4sIII", b"GOB ", 20, 12, 2)
    e1 = struct.pack("<II128s", off1, len(cog_txt), b"cog\\kyle_saber.cog".ljust(128, b"\x00"))
    e2 = struct.pack("<II128s", off2, len(dat_txt), b"misc\\saber.dat".ljust(128, b"\x00"))
    (out_dir / "jedi_knight_patch.gob").write_bytes(hdr + e1 + e2 + cog_txt + dat_txt)

def gen_domain_lucasarts_lab(out_dir: Path):
    s1 = b"manny_suit.cos\x00"
    s2 = b"rubacava\\dock.set\x00"
    pool = s1 + s2
    p1 = b"-- GrimE Lua/Costume\nmanny.head = 'skull'\n"
    p2 = b"SET_LIGHTS 4\nAMBIENT 0.25 0.30 0.45\n"
    data_start = 16 + 2 * 16 + len(pool)
    hdr = struct.pack("<4sIII", b"LABN", 0x00010000, 2, len(pool))
    e1 = struct.pack("<IIII", 0, data_start, len(p1), 0x54534F43)
    e2 = struct.pack("<IIII", len(s1), data_start + len(p1), len(p2), 0x20544553)
    (out_dir / "grim_fandango_data.lab").write_bytes(hdr + e1 + e2 + pool + p1 + p2)

def gen_domain_terminal_reality_pod(out_dir: Path):
    p1 = b"[Vehicle]\nName=GraveDigger\nMassKg=4500\n"
    p2 = b"WORLD_FOG 0.02 0.02 0.05\n"
    n1 = b"trucks\\gravedigger.trk\x00"
    n2 = b"world\\arena.lvl\x00"
    pool = n1 + n2

    off1 = 288
    off2 = off1 + len(p1)
    dir_off = off2 + len(p2)

    hdr = bytearray(288)
    hdr[0:4] = b"POD3"
    hdr[8 : 8 + 26] = b"Terminal Reality POD3 Pak\x00"
    struct.pack_into("<I", hdr, 0x058, 2)
    struct.pack_into("<I", hdr, 0x108, dir_off)
    struct.pack_into("<I", hdr, 0x110, len(pool))

    e1 = struct.pack("<5I", 0, len(p1), off1, 1035000000, 0)
    e2 = struct.pack("<5I", len(n1), len(p2), off2, 1035003600, 0)
    (out_dir / "bloodrayne_assets.pod").write_bytes(bytes(hdr) + p1 + p2 + e1 + e2 + pool)

def gen_domain_gothic_vdf(out_dir: Path):
    def vdf_ent(name: bytes, off_or_child: int, sz: int, flags: int) -> bytes:
        # ZenGin VDFS pads 64-byte names with ASCII spaces (0x20)!
        return name.ljust(64, b" ") + struct.pack("<IIII", off_or_child, sz, flags, 0x20)

    pay1 = b"INSTANCE ItMw_1H_Sword_01 (C_Item) { damageTotal = 45; };\n"
    pay2 = b"[ZEN_MESH]\nPolys=128\nMaterial=NW_HARBOR_STONE\n"
    cat_off = 296
    data_off1 = cat_off + 4 * 80
    data_off2 = data_off1 + len(pay1)

    # 4 catalog entries: [0]=_WORK (DIR->1, LAST), [1]=DATA (DIR->2, LAST), [2]=SWORD.D, [3]=HARBOR.MRM (LAST)
    cat = (
        vdf_ent(b"_WORK", 1, 0, 0x80000000 | 0x40000000)
        + vdf_ent(b"DATA", 2, 0, 0x80000000 | 0x40000000)
        + vdf_ent(b"SWORD.D", data_off1, len(pay1), 0)
        + vdf_ent(b"HARBOR.MRM", data_off2, len(pay2), 0x40000000)
    )
    comment = b"Gothic II Night of the Raven Addon VDFS".ljust(256, b"\x1a")
    dos_ts = ((2003 - 1980) << 25) | (8 << 21) | (22 << 16) | (14 << 11)
    hdr = struct.pack("<16s6I", b"PSVDSC_V2.00\n\r\n\r", 4, 2, dos_ts, len(pay1) + len(pay2), cat_off, 0x50)
    (out_dir / "gothic2_khorinis.vdf").write_bytes(comment + hdr + cat + pay1 + pay2)

def gen_domain_lithtech_rez(out_dir: Path):
    def str_to_typ(ext: str) -> int:
        v = 0
        for ch in ext:
            v = ((v << 8) | ord(ch)) & 0xFFFFFFFF
        return v

    banner = (
        b"\r\n" + b"RezMgr Version 1 Copyright (C) 1995 MONOLITH INC.".ljust(60, b" ")
        + b"\r\n" + b"NOLF UNITY GADGETS PACK".ljust(60, b" ")
        + b"\r\n\x1a"
    )
    assert len(banner) == 127

    pay1 = b"Name = \"Lipstick Explosive\"\nDamage = 120\n"
    pay2 = b"LTB_MODEL_STUB_V9"
    off1 = 168
    off2 = off1 + len(pay1)
    sub_dir_pos = off2 + len(pay2)

    # Subdirectory "ATTRIBUTES": 1 resource entry WEAPONS.TXT
    sub_blk = (
        struct.pack("<7I", 0, off1, len(pay1), 975000000, 101, str_to_typ("TXT"), 0)
        + b"WEAPONS\x00\x00"
    )
    root_dir_pos = sub_dir_pos + len(sub_blk)
    # Root directory: subdir ATTRIBUTES + resource CATE_ARCHER.LTB
    root_blk = (
        struct.pack("<4I", 1, sub_dir_pos, len(sub_blk), 975000000) + b"ATTRIBUTES\x00"
        + struct.pack("<7I", 0, off2, len(pay2), 975000100, 102, str_to_typ("LTB"), 0) + b"CATE_ARCHER\x00\x00"
    )
    bin_hdr = struct.pack("<10IB", 1, root_dir_pos, len(root_blk), 975000000, root_dir_pos + len(root_blk), 975000000, 0, 16, 16, 1, 1)
    assert len(banner) + len(bin_hdr) == 168
    (out_dir / "nolf_gadgets.rez").write_bytes(banner + bin_hdr + pay1 + pay2 + sub_blk + root_blk)

def gen_domain_rff(out_dir: _DomainSampleRouter) -> None:
    # RFF v3.1 (0x0301): 32B header + encrypted payload ("BLOOD_INI_DATA\n") + XOR-encrypted 48B directory
    plain = b"[Episode1]\nTitle = The Way of All Flesh\n"
    enc_payload = bytearray(plain)
    for i in range(min(len(enc_payload), 256)):
        enc_payload[i] ^= (i >> 1)

    dir_off = 32 + len(enc_payload)
    ent = bytearray(48)
    struct.pack_into("<IIIIB", ent, 0x10, 32, len(plain), len(plain), 858000000, 0x10)  # flag 0x10 = encrypted
    ent[0x21:0x24] = b"INI"
    ent[0x24:0x29] = b"BLOOD"

    key = dir_off << 1
    for i in range(len(ent)):
        ent[i] ^= ((key >> 1) & 0xFF)
        key += 1

    hdr = bytearray(32)
    hdr[0:4] = b"RFF\x1a"
    struct.pack_into("<HHII", hdr, 4, 0x0301, 0, dir_off, 1)
    (out_dir / "blood_cryptic.rff").write_bytes(bytes(hdr) + bytes(enc_payload) + bytes(ent))

def gen_domain_sin(out_dir: _DomainSampleRouter) -> None:
    p1 = b"map base1\nskill 2\n"
    p2 = b"shader metal_wall { surfaceparm metalsteps }\n"
    off1 = 12
    off2 = off1 + len(p1)
    dir_off = off2 + len(p2)

    def spak_ent(path: bytes, off: int, sz: int) -> bytes:
        b = bytearray(128)
        b[0:len(path)] = path
        struct.pack_into("<II", b, 120, off, sz)
        return bytes(b)

    directory = spak_ent(b"cfg/autoexec.cfg", off1, len(p1)) + spak_ent(b"scripts/metal.shader", off2, len(p2))
    hdr = b"SPAK" + struct.pack("<II", dir_off, len(directory))
    (out_dir / "sin_base_pak.sin").write_bytes(hdr + p1 + p2 + directory)

def gen_domain_bar(out_dir: _DomainSampleRouter) -> None:
    payload = b"<proto version='2'><unit name='Falconet'><hp>150</hp></unit></proto>\n"
    (out_dir / "aoe3_art_pack.bar").write_bytes(build_ensemble_bar("Data", "protoy.xml", payload))

def gen_domain_drs(out_dir: _DomainSampleRouter) -> None:
    # 64B header + 1 table header (12B: "anib" -> bina) + 1 file entry (12B: id 50100) + payload
    payload = b"// AoE2 Palette & UI Table\n255 0 0\n0 255 0\n"
    hdr = bytearray(64)
    cpr = b"Copyright (c) 1997 Ensemble Studios.\x1a"
    hdr[0:len(cpr)] = cpr
    hdr[40:44] = b"1.00"
    hdr[44:49] = b"tribe"
    tbl_off = 64 + 12
    dat_off = tbl_off + 12
    struct.pack_into("<II", hdr, 56, 1, dat_off)
    tbl_hdr = b"anib" + struct.pack("<II", tbl_off, 1)
    file_ent = struct.pack("<III", 50100, dat_off, len(payload))
    (out_dir / "aoe2_interfac.drs").write_bytes(bytes(hdr) + tbl_hdr + file_ent + payload)

def gen_domain_lod(out_dir: _DomainSampleRouter) -> None:
    # 92B (0x5C) header + 2 * 32B entries (1 zlib-compressed .txt, 1 uncompressed .cfg)
    plain_txt = b"Name\tAttack\tDefense\nArchangel\t30\t30\n"
    z_txt = zlib.compress(plain_txt, 6)
    raw_cfg = b"SoundVolume=100\n"

    dir_end = 0x5C + 2 * 32
    off1 = dir_end
    off2 = off1 + len(z_txt)

    hdr = bytearray(0x5C)
    hdr[0:4] = b"LOD\x00"
    struct.pack_into("<II", hdr, 4, 200, 2)

    e1 = bytearray(32)
    e1[0:11] = b"crtraits.tx"  # wait: let's write full "crtraits.txt\x00" (13B <= 16B)
    e1[0:13] = b"crtraits.txt\x00"
    struct.pack_into("<IIII", e1, 16, off1, len(plain_txt), 2, len(z_txt))

    e2 = bytearray(32)
    e2[0:11] = b"heroes3.cfg"
    struct.pack_into("<IIII", e2, 16, off2, len(raw_cfg), 1, 0)

    (out_dir / "h3bitmap_core.lod").write_bytes(bytes(hdr + e1 + e2) + z_txt + raw_cfg)

def gen_domain_zfs(out_dir: _DomainSampleRouter) -> None:
    xor_key = 0xA55A3C69
    key_bytes = struct.pack("<I", xor_key)
    plain = b"[Mission01]\nObjective = Escort Convoy\n"
    enc = bytes(plain[i] ^ key_bytes[i & 3] for i in range(len(plain)))

    dir_off = 28
    dat_off = dir_off + 4 + 36
    hdr = b"ZFSF" + struct.pack("<IIIIII", 1, 0, 4, 1, xor_key, dir_off)
    page_next = struct.pack("<I", 0)
    ent = bytearray(36)
    ent[0:11] = b"misn01.msn\x00"
    struct.pack_into("<IIIII", ent, 16, dat_off, 1, len(plain), 880000000, 0)
    (out_dir / "i76_scripts.zfs").write_bytes(hdr + page_next + bytes(ent) + enc)

def gen_domain_bun(out_dir: _DomainSampleRouter) -> None:
    payload = b"GUYBRUSH: Look behind you, a three-headed monkey!\n"
    dat_off = 12
    dir_off = dat_off + len(payload)
    hdr = b"LB83" + struct.pack(">II", dir_off, 1)
    ent = bytearray(32)
    ent[0:15] = b"plunder_dlg.txt"
    struct.pack_into(">II", ent, 24, dat_off, len(payload))
    (out_dir / "comi_vox_bundle.bun").write_bytes(hdr + payload + bytes(ent))


def _run_consolidated_domain_samples(td_root: Path) -> None:

    router = _DomainSampleRouter(td_root, {'visual_novel_assets.rpa': 'archives', 'descent_mines.hog': 'archives', 'red_faction_mars.vpp': 'archives', 'conquer_allied.mix': 'archives', 'freespace2_root.vp': 'archives', 'jedi_knight_patch.gob': 'archives', 'grim_fandango_data.lab': 'archives', 'bloodrayne_assets.pod': 'archives', 'gothic2_khorinis.vdf': 'archives', 'nolf_gadgets.rez': 'archives', 'blood_cryptic.rff': 'archives', 'sin_base_pak.sin': 'archives', 'aoe3_art_pack.bar': 'archives', 'aoe2_interfac.drs': 'archives', 'h3bitmap_core.lod': 'archives', 'i76_scripts.zfs': 'archives', 'comi_vox_bundle.bun': 'archives'})

    for fn in [gen_domain_renpy_rpa, gen_domain_descent_hog, gen_domain_volition_vpp, gen_domain_westwood_mix, gen_domain_volition_vp, gen_domain_lucasarts_gob, gen_domain_lucasarts_lab, gen_domain_terminal_reality_pod, gen_domain_gothic_vdf, gen_domain_lithtech_rez, gen_domain_rff, gen_domain_sin, gen_domain_bar, gen_domain_drs, gen_domain_lod, gen_domain_zfs, gen_domain_bun]:

        fn(router)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else (repo_root / "testdata" / "archives")
    out_dir.mkdir(parents=True, exist_ok=True)

    generated: list[str] = []
    generated.append(gen_01_exfat(out_dir))
    generated.append(gen_02_romfs(out_dir))
    generated.append(gen_03_cramfs(out_dir))
    generated.append(gen_04_alz(out_dir))
    generated.append(gen_05_zoo(out_dir))
    generated.append(gen_06_sea_arc(out_dir))
    generated.append(gen_07_stuffit_sit(out_dir))
    generated.extend(gen_08_nintendo_sarc_and_szs(out_dir))
    generated.append(gen_09_nintendo_narc(out_dir))
    generated.append(gen_10_valve_gcf(out_dir))
    generated.extend(gen_11_and_15_fat_and_lzfse_dmg(out_dir))
    generated.append(gen_13_rar5_compressed(out_dir))
    generated.append(gen_14_7z_bcj2(out_dir))
    generated.append(gen_16_ntfs_large_mft(out_dir))
    generated.append(gen_17_zip64_multicodec(out_dir))
    generated.append(gen_18_hybrid_udf_iso(out_dir))

    print(f"Generated {len(generated)} deterministic archive/FS samples in {out_dir}:")
    for fn in generated:
        sz = (out_dir / fn).stat().st_size
        print(f"  - {fn} ({sz} bytes)")
    return 0
    if len(__import__('sys').argv) == 1:
        _run_consolidated_domain_samples(Path(__file__).resolve().parent.parent / 'testdata')


if __name__ == "__main__":
    raise SystemExit(main())
