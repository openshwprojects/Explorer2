#!/usr/bin/env python3
"""
generate_samples_eda_waveforms.py — Deterministic sample generator for Explorer2's
new & upgraded EDA, VLSI, FPGA, and Waveform parsers.

Default output directories (relative to repository root):
  - testdata/eda/
  - testdata/waveforms/

Pass `--out-root <dir>` to write into `<dir>/eda` and `<dir>/waveforms` instead.
"""

from __future__ import annotations

import argparse
import bz2
import gzip
import io
import json
import math
import pathlib
import struct
import zipfile


# ---------------------------------------------------------------------------
# Helpers: VZT varints, CCITT-16 CRC, deterministic gzip/zip
# ---------------------------------------------------------------------------

def encode_vzt_v32(val: int) -> bytes:
    """GTKWave `vzt_wr_emit_v32`: 7-bit little-endian varint with MSB=1 on final byte."""
    assert 0 <= val < (1 << 32)
    out = bytearray()
    while True:
        nxt = val >> 7
        if nxt == 0:
            out.append((val & 0x7F) | 0x80)
            break
        out.append(val & 0x7F)
        val = nxt
    return bytes(out)


def encode_vzt_v64(val: int) -> bytes:
    """GTKWave `vzt_wr_emit_v64`: 7-bit little-endian varint with MSB=1 on final byte."""
    assert 0 <= val < (1 << 64)
    out = bytearray()
    while True:
        nxt = val >> 7
        if nxt == 0:
            out.append((val & 0x7F) | 0x80)
            break
        out.append(val & 0x7F)
        val = nxt
    return bytes(out)


def gzip_deterministic(payload: bytes) -> bytes:
    buf = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=buf, compresslevel=9, mtime=0) as gz:
        gz.write(payload)
    return buf.getvalue()


def jbi_crc16_reflected(data: bytes) -> int:
    """Altera STAPL JBC `jbi_check_crc` reflected CCITT-16 (poly 0x8408, init 0xFFFF, ~crc)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return (~crc) & 0xFFFF


# ---------------------------------------------------------------------------
# 1. QSPICE Schematic (.qsch)
# ---------------------------------------------------------------------------

def build_qspice_qsch() -> bytes:
    """Mike Engelhardt's QSPICE `.qsch`: 0xFF 0xD8 0xFF 0xDB + CP1252 `«...»` S-record AST."""
    qsch_text = (
        "«schematic (0,0)\n"
        "  «component (400,1200) 0 0\n"
        "    «symbol V\n"
        "      «type: V»\n"
        "      «description: Input DC Voltage Source»\n"
        "      «ellipse (-150,-150) (150,150) 0 0 0 0x1000000 -1 -1»\n"
        "      «line (0,150) (0,300) 0 0 0x1000000 -1 -1»\n"
        "      «line (0,-150) (0,-300) 0 0 0x1000000 -1 -1»\n"
        "      «line (-40,70) (40,70) 0 0 0x1000000 -1 -1»\n"
        "      «line (0,30) (0,110) 0 0 0x1000000 -1 -1»\n"
        "      «line (-40,-70) (40,-70) 0 0 0x1000000 -1 -1»\n"
        "      «text (180,90) 1 7 0 0x1000000 -1 -1 \"V1\"»\n"
        "      «text (180,-90) 1 7 0 0x1000000 -1 -1 \"24V\"»\n"
        "      «pin (0,300) (0,0) 1 0 0 0x1000000 -1 \"+\"»\n"
        "      «pin (0,-300) (0,0) 1 0 0 0x1000000 -1 \"-\"»\n"
        "    »\n"
        "  »\n"
        "  «component (1100,1500) 0 0\n"
        "    «symbol M\n"
        "      «type: MN»\n"
        "      «rect (-120,-140) (180,140) 0 0 0 0x1000000 -1 -1»\n"
        "      «line (-200,0) (-80,0) 0 0 0x1000000 -1 -1»\n"
        "      «line (-80,-100) (-80,100) 0 0 0x1000000 -1 -1»\n"
        "      «line (-30,-100) (-30,100) 0 0 0x1000000 -1 -1»\n"
        "      «line (-30,70) (200,70) 0 0 0x1000000 -1 -1»\n"
        "      «line (-30,-70) (200,-70) 0 0 0x1000000 -1 -1»\n"
        "      «text (-120,180) 1 7 0 0x1000000 -1 -1 \"M1\"»\n"
        "      «text (-120,280) 1 7 0 0x1000000 -1 -1 \"BSC050N04\"»\n"
        "      «pin (-200,0) (0,0) 1 0 0 0x1000000 -1 \"G\"»\n"
        "      «pin (200,70) (0,0) 1 0 0 0x1000000 -1 \"D\"»\n"
        "      «pin (200,-70) (0,0) 1 0 0 0x1000000 -1 \"S\"»\n"
        "    »\n"
        "  »\n"
        "  «component (1500,1050) 0 0\n"
        "    «symbol D\n"
        "      «type: D»\n"
        "      «triangle (-90,-100) (90,-100) (0,100) 0 0 0x1000000 -1 -1»\n"
        "      «line (-95,100) (95,100) 0 0 0x1000000 -1 -1»\n"
        "      «line (0,-250) (0,-100) 0 0 0x1000000 -1 -1»\n"
        "      «line (0,100) (0,380) 0 0 0x1000000 -1 -1»\n"
        "      «text (130,40) 1 7 0 0x1000000 -1 -1 \"D1\"»\n"
        "      «text (130,-70) 1 7 0 0x1000000 -1 -1 \"MBRS340\"»\n"
        "      «pin (0,380) (0,0) 1 0 0 0x1000000 -1 \"K\"»\n"
        "      «pin (0,-250) (0,0) 1 0 0 0x1000000 -1 \"A\"»\n"
        "    »\n"
        "  »\n"
        "  «component (2050,1430) 0 0\n"
        "    «symbol L\n"
        "      «type: L»\n"
        "      «coil (-350,0) (350,0) 70 0 0x1000000 -1 -1»\n"
        "      «line (-450,0) (-350,0) 0 0 0x1000000 -1 -1»\n"
        "      «line (350,0) (450,0) 0 0 0x1000000 -1 -1»\n"
        "      «text (-120,140) 1 7 0 0x1000000 -1 -1 \"L1\"»\n"
        "      «text (-120,240) 1 7 0 0x1000000 -1 -1 \"10uH\"»\n"
        "      «pin (-450,0) (0,0) 1 0 0 0x1000000 -1 \"1\"»\n"
        "      «pin (450,0) (0,0) 1 0 0 0x1000000 -1 \"2\"»\n"
        "    »\n"
        "  »\n"
        "  «component (2700,1115) 0 0\n"
        "    «symbol C\n"
        "      «type: C»\n"
        "      «line (-110,45) (110,45) 0 0 0x1000000 -1 -1»\n"
        "      «line (-110,-45) (110,-45) 0 0 0x1000000 -1 -1»\n"
        "      «line (0,45) (0,315) 0 0 0x1000000 -1 -1»\n"
        "      «line (0,-45) (0,-315) 0 0 0x1000000 -1 -1»\n"
        "      «text (140,60) 1 7 0 0x1000000 -1 -1 \"C1\"»\n"
        "      «text (140,-50) 1 7 0 0x1000000 -1 -1 \"47uF\"»\n"
        "      «pin (0,315) (0,0) 1 0 0 0x1000000 -1 \"1\"»\n"
        "      «pin (0,-315) (0,0) 1 0 0 0x1000000 -1 \"2\"»\n"
        "    »\n"
        "  »\n"
        "  «component (3300,1115) 0 0\n"
        "    «symbol R\n"
        "      «type: R»\n"
        "      «rect (-70,-180) (70,180) 0 0 0 0x1000000 -1 -1»\n"
        "      «line (0,180) (0,315) 0 0 0x1000000 -1 -1»\n"
        "      «line (0,-180) (0,-315) 0 0 0x1000000 -1 -1»\n"
        "      «text (110,60) 1 7 0 0x1000000 -1 -1 \"Rload\"»\n"
        "      «text (110,-50) 1 7 0 0x1000000 -1 -1 \"5R\"»\n"
        "      «pin (0,315) (0,0) 1 0 0 0x1000000 -1 \"1\"»\n"
        "      «pin (0,-315) (0,0) 1 0 0 0x1000000 -1 \"2\"»\n"
        "    »\n"
        "  »\n"
        "  «wire (400,1500) (400,1570) \"VIN\"»\n"
        "  «wire (400,1570) (1300,1570) \"VIN\"»\n"
        "  «wire (700,1500) (900,1500) \"PWM_HG\"»\n"
        "  «wire (1300,1430) (1500,1430) \"SW\"»\n"
        "  «wire (1500,1430) (1600,1430) \"SW\"»\n"
        "  «wire (2500,1430) (2700,1430) \"VOUT\"»\n"
        "  «wire (2700,1430) (3300,1430) \"VOUT\"»\n"
        "  «wire (400,900) (400,800) \"0\"»\n"
        "  «wire (400,800) (1500,800) \"0\"»\n"
        "  «wire (1500,800) (2700,800) \"0\"»\n"
        "  «wire (2700,800) (3300,800) \"0\"»\n"
        "  «junction (1500,1430)»\n"
        "  «junction (2700,1430)»\n"
        "  «junction (1500,800)»\n"
        "  «junction (2700,800)»\n"
        "  «net (400,1570) 0 13 \"VIN\"»\n"
        "  «net (700,1500) 0 13 \"PWM_HG\"»\n"
        "  «net (1500,1430) 0 13 \"SW\"»\n"
        "  «net (3300,1430) 0 13 \"VOUT\"»\n"
        "  «net (1500,800) 0 0 \"0\"»\n"
        "  «text (400,1950) 1 7 1 0x1000000 -1 -1 \".tran 2m\"»\n"
        "»\n"
    )
    return b"\xFF\xD8\xFF\xDB" + qsch_text.encode("cp1252")


# ---------------------------------------------------------------------------
# 2. EAGLE Design Rules (.dru)
# ---------------------------------------------------------------------------

def build_eagle_dru() -> bytes:
    dru = """# Autodesk / CadSoft EAGLE 6-Layer HDI Impedance-Controlled Design Rules
description[de] = <b>6-Lagen HDI Leiterplatten-Designregeln</b>\\nBlind- und Buried-Vias (1-2, 2-3, 14-15, 15-16)
description[en] = <b>6-Layer HDI Controlled-Impedance Stackup</b>\\nLaser microvias [1:2]/[15:16] + buried cores (2*3) & (14*15)
layerSetup = [2:1+(2*3)+(14*15)+16:15]
mtCopper = 0.035mm 0.018mm 0.018mm 0.035mm 0.035mm 0.035mm 0.035mm 0.035mm 0.035mm 0.035mm 0.035mm 0.035mm 0.035mm 0.018mm 0.018mm 0.035mm
mtIsolate = 0.08mm 0.20mm 0.12mm 0.15mm 0.20mm 0.15mm 0.20mm 0.15mm 0.20mm 0.15mm 0.20mm 0.15mm 0.65mm 0.20mm 0.08mm
mdWireWire = 4mil
mdWirePad = 4mil
mdWireVia = 4mil
mdPadPad = 4mil
mdPadVia = 4mil
mdViaVia = 5mil
mdSmdPad = 0mil
mdSmdVia = 0mil
mdSmdSmd = 0mil
msWidth = 3.5mil
msDrill = 0.15mm
msMicroVia = 0.10mm
msBlindViaRatio = 0.750000
rvPadTop = 0.250000
rvPadInner = 0.250000
rvPadBottom = 0.250000
rlMinPadTop = 4mil
rlMaxPadTop = 16mil
rlMinViaOuter = 4mil
rlMaxViaOuter = 14mil
"""
    return dru.encode("utf-8")


# ---------------------------------------------------------------------------
# 3. Microwind CMOS IC Mask Layout (.msk)
# ---------------------------------------------------------------------------

def build_microwind_msk() -> bytes:
    msk = """VERSION 3.5
TITLE CMOS_NAND2_65NM_STDCELL
FOUNDRY cmos65nm.rul
LAMBDA 0.035
VIEW -6 -6 46 54
BB 0 0 36 42
* N-Well tub for dual parallel PMOS pull-up network
C NTUB 0 22 36 20
* VDD and VSS Metal-1 power rails
C ME1 0 37 36 5
C ME1 0 0 36 5
* P+ active diffusion (PMOS M_PA, M_PB) & N+ active diffusion (series NMOS M_NA, M_NB)
C DP 4 25 28 9
C DN 4 7 28 9
* Polysilicon gates for inputs A and B crossing both N+ and P+ active regions
C POLY 10 5 4 32
C POLY 22 5 4 32
* Source/Drain diffusion-to-Metal1 contacts
C CO 5 30 2 2
C CO 5 38 2 2
C CO 16 28 2 2
C CO 28 30 2 2
C CO 28 38 2 2
C CO 5 9 2 2
C CO 5 1 2 2
C CO 28 9 2 2
* Metal-1 local interconnect straps (VDD ties, VSS tie, output Y drain strap)
C ME1 4 29 4 11
C ME1 27 29 4 11
C ME1 4 1 4 10
C ME1 15 26 4 8
C ME1 15 18 16 4
C ME1 27 8 4 14
* Input A / B poly contact landing pads + Metal-2 vertical signal pins
C POLY 8 17 6 5
C CO 9 18 2 2
C ME1 8 17 5 5
C VIA 9 18 2 2
C ME2 8 14 4 12
C POLY 21 12 6 5
C CO 22 13 2 2
C ME1 21 12 5 5
C VIA 22 13 2 2
C ME2 21 10 4 12
C VIA 16 19 2 2
C ME2 15 16 4 14
* Signal & Supply Port Labels
T ME1 2 39 VDD
T ME1 2 2 VSS
T ME2 10 20 A
T ME2 23 15 B
T ME2 17 24 Y
END
"""
    return msk.encode("utf-8")


# ---------------------------------------------------------------------------
# 4. Berkeley SIS / ABC Boolean Equation Netlist (.eqn)
# ---------------------------------------------------------------------------

def build_berkeley_eqn() -> bytes:
    eqn = """# Berkeley SIS 2-Bit Carry-Lookahead ALU Slice with Parity Check
NAME = alu_slice_4bit;
INORDER = a0 a1 b0 b1 cin op_sub op_logic;
OUTORDER = y0 y1 cout parity_even;

b0_eff = b0 ^ op_sub;
b1_eff = b1 ^ op_sub;

p0 = a0 ^ b0_eff;
g0 = a0 * b0_eff;
p1 = a1 ^ b1_eff;
g1 = a1 * b1_eff;

c1 = g0 + (p0 * cin);
cout = g1 + (p1 * g0) + (p1 * p0 * cin);

arith0 = p0 ^ cin;
arith1 = p1 ^ c1;

logic0 = (a0 & b0) + (!op_sub * (a0 | b0));
logic1 = (a1 & b1) + (!op_sub * (a1 | b1));

y0 = (!op_logic * arith0) + (op_logic * logic0);
y1 = (!op_logic * arith1) + (op_logic * logic1);
parity_even = !(y0 ^ y1 ^ cout);
"""
    return eqn.encode("utf-8")


# ---------------------------------------------------------------------------
# 5. Altera StaPL Jam Byte-Code v2 (.jbc)
# ---------------------------------------------------------------------------

def build_altera_jbc() -> bytes:
    """Constructs a genuine Altera StaPL Jam Byte-Code v2 (`JBC\\x01`) binary with valid CCITT-16 CRC."""
    # String table builder
    str_pool = bytearray()
    str_offsets: dict[str, int] = {}

    def add_str(s: str) -> int:
        if s in str_offsets:
            return str_offsets[s]
        off = len(str_pool)
        str_pool.extend(s.encode("ascii") + b"\x00")
        str_offsets[s] = off
        return off

    # Pre-populate strings (first action/proc names must be non-empty!)
    off_act_prog = add_str("PROGRAM")
    off_desc_prog = add_str("Program CFM0/CFM1 flash sectors and verify")
    off_act_ver = add_str("VERIFY")
    off_desc_ver = add_str("Verify Silicon IDCODE and CFM checksum")

    off_p0 = add_str("DO_ENTER_ISP")
    off_p1 = add_str("DO_CHECK_IDCODE")
    off_p2 = add_str("DO_PROGRAM_CFM")
    off_p3 = add_str("DO_VERIFY_CFM")
    off_p4 = add_str("DO_EXIT_ISP")

    # Note key-value strings (`key\0val\0...\0`)
    notes = (
        b"ALG_VERSION\x002.4\x00"
        b"DEVICE\x0010M08SAE144C8G\x00"
        b"FAMILY\x00MAX 10\x00\x00"
    )

    # Data section: JTAG TDI / expected TDO byte patterns
    #   +0x00: 10-bit IR ISC_ENABLE  (0x02CC)
    #   +0x04: 32-bit DR IDCODE TDI  (0x00000000)
    #   +0x08: 32-bit DR IDCODE TDO  (0x031820DD — Intel MAX 10 10M08)
    #   +0x0C: 10-bit IR ISC_PROGRAM (0x02F4)
    #   +0x10: 32-bit DR CFM word 0  (0xA5C35A3C)
    #   +0x14: 32-bit DR CFM word 1  (0x3C9669C3)
    #   +0x18: 10-bit IR BYPASS      (0x03FF)
    data_sec = (
        struct.pack(">I", 0x000002CC)
        + struct.pack(">I", 0x00000000)
        + struct.pack(">I", 0x031820DD)
        + struct.pack(">I", 0x000002F4)
        + struct.pack(">I", 0xA5C35A3C)
        + struct.pack(">I", 0x3C9669C3)
        + struct.pack(">I", 0x000003FF)
    )

    # JBC VM bytecode helpers (`(opcode >> 6) & 3` gives count of 32-bit BE args)
    def op_pshl(val: int) -> bytes:
        return b"\x40" + struct.pack(">I", val)  # 1 imm32

    def op_call(proc_idx: int) -> bytes:
        return b"\x43" + struct.pack(">I", proc_idx)  # 1 imm32

    def op_sint(jtag_state: int) -> bytes:
        return b"\x47" + struct.pack(">I", jtag_state)  # 1 imm32

    def op_is_mem(data_off: int) -> bytes:
        return b"\x52" + struct.pack(">I", data_off)  # 1 imm32 (IRSCAN TDI from dataSec)

    def op_ds_mem(data_off: int) -> bytes:
        return b"\x51" + struct.pack(">I", data_off)  # 1 imm32 (DRSCAN TDI from dataSec)

    def op_dsc_mem(tdi_off: int, tdo_off: int) -> bytes:
        return b"\x82" + struct.pack(">II", tdi_off, tdo_off)  # 2 imm32 (DRSCAN TDI + TDO)

    def op_wait(cycles: int, usec: int) -> bytes:
        return b"\x84" + struct.pack(">II", cycles, usec)  # 2 imm32

    def op_ret() -> bytes:
        return b"\x11"  # 0 imm32

    def op_exit(code: int) -> bytes:
        return op_pshl(code) + b"\x21"

    # Assemble procedures:
    # P0: DO_ENTER_ISP (calls P1 DO_CHECK_IDCODE, shifts 10-bit ISC_ENABLE IR)
    code_p0 = op_sint(1) + op_call(1) + op_pshl(10) + op_is_mem(0x00) + op_wait(100, 500) + op_ret()
    # P1: DO_CHECK_IDCODE (10-bit IDCODE IR=0x006 via ISS 0x17, 32-bit DRSCAN with expected TDO)
    code_p1 = op_pshl(10) + op_pshl(0x006) + b"\x17" + op_pshl(32) + op_dsc_mem(0x04, 0x08) + op_ret()
    # P2: DO_PROGRAM_CFM (10-bit ISC_PROGRAM IR + two 32-bit DRSCAN data bursts + calls P3)
    code_p2 = (
        op_pshl(10) + op_is_mem(0x0C)
        + op_pshl(32) + op_ds_mem(0x10)
        + op_wait(50, 200)
        + op_pshl(32) + op_ds_mem(0x14)
        + op_call(3)
        + op_ret()
    )
    # P3: DO_VERIFY_CFM (32-bit DRSCAN verify against expected TDO)
    code_p3 = op_pshl(32) + op_dsc_mem(0x10, 0x14) + op_ret()
    # P4: DO_EXIT_ISP (10-bit BYPASS IR + EXIT 0)
    code_p4 = op_pshl(10) + op_is_mem(0x18) + op_exit(0)

    proc_codes = [code_p0, code_p1, code_p2, code_p3, code_p4]
    proc_rel_offs = []
    code_blob = bytearray()
    for c in proc_codes:
        proc_rel_offs.append(len(code_blob))
        code_blob.extend(c)

    # Procedure table (5 entries * 13 bytes: `[nameOff:4BE][nextProc:4BE][attrs:1][codeRelOff:4BE]`)
    # Chain for PROGRAM (first=0): 0 -> 2 -> 4 -> 0xFFFFFFFF
    # Chain for VERIFY  (first=1): 1 -> 3 -> 4 -> 0xFFFFFFFF (share P4 via CALL or end at 3)
    NO_NEXT = 0xFFFFFFFF
    proc_entries = [
        (off_p0, 2, 0x01, proc_rel_offs[0]),
        (off_p1, 3, 0x01, proc_rel_offs[1]),
        (off_p2, 4, 0x01, proc_rel_offs[2]),
        (off_p3, NO_NEXT, 0x01, proc_rel_offs[3]),
        (off_p4, NO_NEXT, 0x01, proc_rel_offs[4]),
    ]
    proc_table_bytes = b"".join(
        struct.pack(">IIBI", nm, nxt, attr, coff) for (nm, nxt, attr, coff) in proc_entries
    )

    # Action table (2 entries * 12 bytes: `[nameOff:4BE][descOff:4BE][firstProcIdx:4BE]`)
    action_entries = [
        (off_act_prog, off_desc_prog, 0),
        (off_act_ver, off_desc_ver, 1),
    ]
    action_table_bytes = b"".join(
        struct.pack(">III", nm, desc, fp) for (nm, desc, fp) in action_entries
    )

    # 52-byte Altera JBC v2 header (`delta = 8`):
    #   +0x00: "JBC\x01"
    #   +0x04: device_family (0)
    #   +0x08: note_strings
    #   +0x0C: action_table
    #   +0x10: proc_table
    #   +0x14: string_table
    #   +0x18: sym_table
    #   +0x1C: data_section
    #   +0x20: code_section
    #   +0x24: debug_section (0)
    #   +0x28: action_count (2)
    #   +0x2C: proc_count (5)
    #   +0x30: sym_count (0)
    hdr_len = 52
    off_notes = hdr_len
    off_actions = off_notes + len(notes)
    off_procs = off_actions + len(action_table_bytes)
    off_strings = off_procs + len(proc_table_bytes)
    off_syms = off_strings + len(str_pool)
    off_data = off_syms
    off_code = off_data + len(data_sec)

    header = struct.pack(
        ">4sIIIIIIIIIIII",
        b"JBC\x01",
        0,
        off_notes,
        off_actions,
        off_procs,
        off_strings,
        off_syms,
        off_data,
        off_code,
        0,
        len(action_entries),
        len(proc_entries),
        0,
    )
    body = header + notes + action_table_bytes + proc_table_bytes + bytes(str_pool) + data_sec + bytes(code_blob)
    crc = jbi_crc16_reflected(body)
    return body + struct.pack(">H", crc)


# ---------------------------------------------------------------------------
# 6. UCLA / ISPD GSRC VLSI Placement Benchmark (.scl + .nodes + .pl + .nets)
# ---------------------------------------------------------------------------

def build_ispd_bookshelf() -> dict[str, bytes]:
    scl = """UCLA scl 1.0
# Created by generate_samples_eda_waveforms.py — RISC-V ALU + RegFile Floorplan

NumRows : 6

CoreRow Horizontal
  Coordinate    :   12
  Height        :   10
  Sitewidth     :    1
  Sitespacing   :    1
  Siteorient    :    N
  Sitesymmetry  :    Y
  SubrowOrigin  :   12   NumSites :  116
End
CoreRow Horizontal
  Coordinate    :   22
  Height        :   10
  Sitewidth     :    1
  Sitespacing   :    1
  Siteorient    :   FS
  Sitesymmetry  :    Y
  SubrowOrigin  :   12   NumSites :  116
End
CoreRow Horizontal
  Coordinate    :   32
  Height        :   10
  Sitewidth     :    1
  Sitespacing   :    1
  Siteorient    :    N
  Sitesymmetry  :    Y
  SubrowOrigin  :   12   NumSites :  116
End
CoreRow Horizontal
  Coordinate    :   42
  Height        :   10
  Sitewidth     :    1
  Sitespacing   :    1
  Siteorient    :   FS
  Sitesymmetry  :    Y
  SubrowOrigin  :   12   NumSites :   74
End
CoreRow Horizontal
  Coordinate    :   52
  Height        :   10
  Sitewidth     :    1
  Sitespacing   :    1
  Siteorient    :    N
  Sitesymmetry  :    Y
  SubrowOrigin  :   12   NumSites :   74
End
CoreRow Horizontal
  Coordinate    :   62
  Height        :   10
  Sitewidth     :    1
  Sitespacing   :    1
  Siteorient    :   FS
  Sitesymmetry  :    Y
  SubrowOrigin  :   12   NumSites :   74
End
"""

    nodes = """UCLA nodes 1.0
# 14 standard cells + 2 fixed macros + 6 perimeter I/O pads

NumNodes :      22
NumTerminals :   8

u_sram_rf      38   28   terminal
u_mul_mac      26   18   terminal
pad_clk         6    6   terminal
pad_rst_n       6    6   terminal
pad_op_a0       6    6   terminal
pad_op_b0       6    6   terminal
pad_alu_y0      6    6   terminal
pad_zero_flg    6    6   terminal
u_dec_0         8   10
u_dec_1         8   10
u_xor_0        10   10
u_xor_1        10   10
u_and_0         8   10
u_or_0          8   10
u_carry_0      12   10
u_carry_1      12   10
u_mux_0        10   10
u_mux_1        10   10
u_shf_0        12   10
u_ff_y0        10   10
u_ff_y1        10   10
u_cmp_z         8   10
"""

    pl = """UCLA pl 1.0

u_sram_rf      90   44 : N  /FIXED
u_mul_mac      58   54 : N  /FIXED
pad_clk         2   36 : N  /FIXED
pad_rst_n       2   52 : N  /FIXED
pad_op_a0      28    2 : N  /FIXED
pad_op_b0      58    2 : N  /FIXED
pad_alu_y0    132   28 : N  /FIXED
pad_zero_flg  132   52 : N  /FIXED
u_dec_0        14   12 : N
u_dec_1        26   12 : N
u_xor_0        40   12 : N
u_xor_1        54   12 : N
u_and_0        70   12 : N
u_or_0         82   12 : N
u_carry_0      16   22 : FS
u_carry_1      34   22 : FS
u_mux_0        52   22 : FS
u_mux_1        68   22 : FS
u_shf_0        20   32 : N
u_ff_y0        42   32 : N
u_ff_y1        20   42 : FS
u_cmp_z        38   42 : FS
"""

    nets = """UCLA nets 1.0

NumNets : 12
NumPins : 33

NetDegree : 4   clk_net
  pad_clk      O :  3.0  0.0
  u_sram_rf    I : -19.0 -8.0
  u_ff_y0      I : -5.0  0.0
  u_ff_y1      I : -5.0  0.0
NetDegree : 3   rst_net
  pad_rst_n    O :  3.0  0.0
  u_ff_y0      I :  0.0 -5.0
  u_ff_y1      I :  0.0 -5.0
NetDegree : 3   op_a0_net
  pad_op_a0    O :  0.0  3.0
  u_dec_0      I : -4.0  0.0
  u_xor_0      I : -5.0  2.0
NetDegree : 3   op_b0_net
  pad_op_b0    O :  0.0  3.0
  u_dec_1      I : -4.0  0.0
  u_xor_1      I : -5.0  2.0
NetDegree : 3   rf_rd_data0
  u_sram_rf    O : -19.0  4.0
  u_and_0      I :  4.0  0.0
  u_mux_0      I :  5.0  0.0
NetDegree : 3   mul_prod0
  u_mul_mac    O :  0.0 -9.0
  u_mux_1      I :  0.0  5.0
  u_shf_0      I :  6.0  0.0
NetDegree : 3   carry_chain0
  u_xor_0      O :  5.0  0.0
  u_carry_0    I : -6.0  0.0
  u_carry_1    I : -6.0  0.0
NetDegree : 3   alu_mux_bus0
  u_carry_1    O :  6.0  0.0
  u_or_0       O : -4.0  0.0
  u_mux_0      I : -5.0  0.0
NetDegree : 2   alu_reg_d0
  u_mux_0      O :  0.0  5.0
  u_ff_y0      I :  5.0  0.0
NetDegree : 2   alu_reg_d1
  u_mux_1      O :  0.0  5.0
  u_ff_y1      I :  5.0  0.0
NetDegree : 2   alu_out_y0
  u_ff_y0      O :  5.0  2.0
  pad_alu_y0   I : -3.0  0.0
NetDegree : 2   zero_flag_net
  u_cmp_z      O :  4.0  0.0
  pad_zero_flg I : -3.0  0.0
"""
    return {
        "ispd_riscv_alu.scl": scl.encode("utf-8"),
        "ispd_riscv_alu.nodes": nodes.encode("utf-8"),
        "ispd_riscv_alu.pl": pl.encode("utf-8"),
        "ispd_riscv_alu.nets": nets.encode("utf-8"),
    }


# ---------------------------------------------------------------------------
# 7. Yokogawa DL-Series Oscilloscope Waveform (.wvf + .hdr)
# ---------------------------------------------------------------------------

def build_yokogawa_wvf_pair() -> tuple[bytes, bytes]:
    n_samples = 512
    dt = 2.0e-6  # 500 kS/s (1.024 ms window)
    hdr = f"""//YOKOGAWA ASCII FILE FORMAT

$PublicInfo
FormatVersion   1.11
Model           DL750
Endian          Little
DataFormat      Trace
GroupNumber     1
TraceTotalNumber 3
DataOffset      0

$Group1
TraceNumber     3
TraceName       U_PHASE_V       V_PHASE_V       I_DCBUS_A
BlockSize       {n_samples}     {n_samples}     {n_samples}
VResolution     0.0250000       0.0250000       0.0050000
VOffset         0.0000000       0.0000000       12.500000
VDataType       IS2             IS2             IS2
VUnit           V               V               A
HResolution     {dt:.7E}        {dt:.7E}        {dt:.7E}
HOffset         0.0000000E+00   0.0000000E+00   0.0000000E+00
HUnit           s               s               s
"""
    ch_u = bytearray()
    ch_v = bytearray()
    ch_i = bytearray()
    for i in range(n_samples):
        t = i * dt
        # 1 kHz fundamental + 20 kHz PWM ripple on 3-phase inverter output
        v_u = 310.0 * math.sin(2.0 * math.pi * 1000.0 * t) + 18.0 * math.sin(2.0 * math.pi * 20000.0 * t)
        v_v = 310.0 * math.sin(2.0 * math.pi * 1000.0 * t - 2.0 * math.pi / 3.0) + 18.0 * math.sin(2.0 * math.pi * 20000.0 * t)
        i_dc = 14.0 + 4.5 * math.sin(2.0 * math.pi * 6000.0 * t) + (3.2 if (i % 25) < 14 else -2.1)

        raw_u = max(-32768, min(32767, int(round((v_u - 0.0) / 0.025))))
        raw_v = max(-32768, min(32767, int(round((v_v - 0.0) / 0.025))))
        raw_i = max(-32768, min(32767, int(round((i_dc - 12.5) / 0.005))))

        ch_u.extend(struct.pack("<h", raw_u))
        ch_v.extend(struct.pack("<h", raw_v))
        ch_i.extend(struct.pack("<h", raw_i))

    return hdr.encode("ascii"), bytes(ch_u + ch_v + ch_i)


# ---------------------------------------------------------------------------
# 8. DreamSourceLab DSView Logic Analyzer Session (.dsl)
# ---------------------------------------------------------------------------

def build_dreamsourcelab_dsl() -> bytes:
    """ZIP archive containing `header` INI + `L-<probe>-<block>` packed 1-bit SPI Flash capture."""
    samples_per_block = 256  # 2 blocks -> 512 samples total @ 50 MHz
    n_blocks = 2
    total_samples = samples_per_block * n_blocks

    # Synthesize SPI Mode 0 transaction: CS_N asserted, command 0x9F (JEDEC ID), response 0xEF 0x40 0x18
    tx_bytes = [0x9F, 0x00, 0x00, 0x00]
    rx_bytes = [0xFF, 0xEF, 0x40, 0x18]

    cs_n = [1] * total_samples
    sclk = [0] * total_samples
    mosi = [0] * total_samples
    miso = [1] * total_samples

    cursor = 24
    for bi in range(4):
        txb = tx_bytes[bi]
        rxb = rx_bytes[bi]
        for bit in range(7, -1, -1):
            for k in range(12):
                idx = cursor + k
                if idx < total_samples - 20:
                    cs_n[idx] = 0
                    sclk[idx] = 1 if 5 <= k <= 9 else 0
                    mosi[idx] = (txb >> bit) & 1
                    miso[idx] = (rxb >> bit) & 1
            cursor += 12
        cursor += 8  # inter-byte pause with CS_N still low
    for k in range(24, min(cursor, total_samples - 20)):
        cs_n[k] = 0

    probes = [cs_n, sclk, mosi, miso]

    def pack_bits(bits: list[int]) -> bytes:
        out = bytearray((len(bits) + 7) // 8)
        for idx, b in enumerate(bits):
            if b:
                out[idx >> 3] |= 1 << (idx & 7)
        return bytes(out)

    header_ini = (
        "[version]\n"
        "version = 2\n"
        "[header]\n"
        "driver = DSLogic\n"
        "device mode = 0\n"
        "capturefile = data\n"
        "total samples = 512\n"
        "samplerate = 50 MHz\n"
        "total probes = 4\n"
        "total blocks = 2\n"
        "probe0 = CS_N\n"
        "probe1 = SCLK\n"
        "probe2 = MOSI\n"
        "probe3 = MISO\n"
        "trigger0 = 0f\n"
    ).encode("utf-8")

    zbuf = io.BytesIO()
    fixed_dt = (2025, 1, 1, 0, 0, 0)
    with zipfile.ZipFile(zbuf, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        zi_hdr = zipfile.ZipInfo("header", date_time=fixed_dt)
        zi_hdr.compress_type = zipfile.ZIP_DEFLATED
        zf.writestr(zi_hdr, header_ini)

        for p_idx, p_bits in enumerate(probes):
            for blk_idx in range(n_blocks):
                sub = p_bits[blk_idx * samples_per_block : (blk_idx + 1) * samples_per_block]
                zi = zipfile.ZipInfo(f"L-{p_idx}-{blk_idx}", date_time=fixed_dt)
                zi.compress_type = zipfile.ZIP_DEFLATED
                zf.writestr(zi, pack_bits(sub))

    return zbuf.getvalue()


# ---------------------------------------------------------------------------
# 9. Genuine GTKWave VZTD v1 Compressed Simulation Waveform (.vzt)
# ---------------------------------------------------------------------------

def encode_vzt_bp_rle(words: list[int]) -> bytes:
    """GTKWave `vzt_write.c` bitplane RLE: alternating 0-runs and 1-runs across `len(words)*32` bits."""
    bits: list[int] = []
    for w in words:
        for b in range(32):
            bits.append((w >> b) & 1)
    out = bytearray()
    cur_bit = 0
    pos = 0
    n = len(bits)
    while pos < n:
        run = 0
        while pos + run < n and bits[pos + run] == cur_bit:
            run += 1
        out.extend(encode_vzt_v32(run))
        pos += run
        cur_bit ^= 1
    return bytes(out)


def build_gtkwave_vzt() -> bytes:
    """
    Constructs a 100% genuine GTKWave `VZTD` v1 (`0x565A5444`, version `0x0001`, `granule_size = 32`)
    file matching `vzt_read.c`:
      - Prefix-compressed `facnames` + `facgeometry` (`[rows, msb, lsb, flags]` BE32)
      - 7 real facilities (4x 1-bit AXI handshake signals, 2x 8-bit AXI address/data buses,
        1x `DOUBLE` analog core voltage) + 1 `ALIAS` facility (`numfacs = 8`, `numrealfacs = 7`)
      - Two compressed value blocks:
          * Block 0 (`0..150 ns`, 16 ticks): gzip-compressed with `bp_rle = 0` raw bitplane words
          * Block 1 (`160..310 ns`, 16 ticks): bzip2-compressed with `bp_rle = 1` RLE bitplane words
    """
    SYM_F_BITS = 0
    SYM_F_DOUBLE = 1 << 1
    SYM_F_ALIAS = 1 << 3

    # (name, rows, msb, lsb, flags)
    facilities = [
        ("axi_tb.aclk", 0, 0, 0, SYM_F_BITS),
        ("axi_tb.aresetn", 0, 0, 0, SYM_F_BITS),
        ("axi_tb.awvalid", 0, 0, 0, SYM_F_BITS),
        ("axi_tb.awready", 0, 0, 0, SYM_F_BITS),
        ("axi_tb.awaddr", 0, 7, 0, SYM_F_BITS),
        ("axi_tb.wdata", 0, 7, 0, SYM_F_BITS),
        ("axi_tb.vcore_v", 0, 63, 0, SYM_F_DOUBLE),
        ("axi_tb.clk_bus_alias", 0, 0, 0, SYM_F_ALIAS),  # aliases facility 0 (`axi_tb.aclk`)
    ]
    num_facs = len(facilities)
    num_real_facs = 7
    granule_size = 32

    # 1. Prefix-compressed facility names (`fac[0]\0` + `[u16BE prefix_len][suffix\0]` for i >= 1)
    facname_raw = bytearray()
    longest_name = 0
    prev_name = ""
    for i, (nm, *_) in enumerate(facilities):
        longest_name = max(longest_name, len(nm))
        if i == 0:
            facname_raw.extend(nm.encode("ascii") + b"\x00")
        else:
            pfx = 0
            max_p = min(len(prev_name), len(nm))
            while pfx < max_p and prev_name[pfx] == nm[pfx]:
                pfx += 1
            facname_raw.extend(struct.pack(">H", pfx))
            facname_raw.extend(nm[pfx:].encode("ascii") + b"\x00")
        prev_name = nm

    z_facnames = gzip_deterministic(bytes(facname_raw))

    # 2. Facility geometry (`num_facs * 16` bytes: `[rows, msb, lsb, flags]` BE32)
    geom_raw = bytearray()
    for _, rows, msb, lsb, flags in facilities:
        geom_raw.extend(struct.pack(">IiII", rows, msb, lsb, flags))
    z_geom = gzip_deterministic(bytes(geom_raw))

    # Compute bit-lane offsets:
    # Real bit facilities:
    #   0: aclk    (1) -> val off 0
    #   1: aresetn (1) -> val off 1
    #   2: awvalid (1) -> val off 2
    #   3: awready (1) -> val off 3
    #   4: awaddr  (8) -> val off 4..11
    #   5: wdata   (8) -> val off 12..19
    # Total value lanes = 20; X/Z lanes follow at 20..39 -> `total_values = 40`!
    bit_facs = [0, 1, 2, 3, 4, 5]
    bit_widths = {0: 1, 1: 1, 2: 1, 3: 1, 4: 8, 5: 8}

    def encode_vzt_block(ticks: list[int], use_rle: bool, use_bz2: bool, phase_off: int) -> bytes:
        n_ticks = len(ticks)
        assert 1 <= n_ticks <= granule_size  # `num_sections == 1`
        num_sections = 1

        # Build per-lane 32-bit value word and X/Z word across `t = 0..n_ticks-1`
        lane_words: list[int] = []
        xz_words: list[int] = []

        for fi in bit_facs:
            w_len = bit_widths[fi]
            for b in range(w_len):
                # Note: bit index `b = 0` is MSB of multi-bit vector in GTKWave (`curBits[b]`)
                bit_pos_from_lsb = (w_len - 1) - b
                vw = 0
                xw = 0
                for t in range(n_ticks):
                    gt = t + phase_off
                    val_bit = 0
                    xz_bit = 0
                    if fi == 0:  # aclk toggles every tick
                        val_bit = gt & 1
                    elif fi == 1:  # aresetn low for first 3 ticks of block 0, then high
                        val_bit = 0 if gt < 3 else 1
                    elif fi == 2:  # awvalid pulses
                        val_bit = 1 if (4 <= (gt % 10) <= 7 and gt >= 3) else 0
                    elif fi == 3:  # awready handshakes
                        val_bit = 1 if (6 <= (gt % 10) <= 8 and gt >= 3) else 0
                    elif fi == 4:  # awaddr[7:0] (high-Z before reset release at gt == 0..1!)
                        if gt < 2:
                            xz_bit = 1
                            val_bit = 1  # 'z' when xz=1 and val=1
                        else:
                            addr_val = (0x10 + ((gt // 2) * 4)) & 0xFF
                            val_bit = (addr_val >> bit_pos_from_lsb) & 1
                    elif fi == 5:  # wdata[7:0]
                        data_val = (0xA0 + gt * 7) & 0xFF
                        val_bit = (data_val >> bit_pos_from_lsb) & 1
                    if val_bit:
                        vw |= 1 << t
                    if xz_bit:
                        xw |= 1 << t
                lane_words.append(vw)
                xz_words.append(xw)

        all_lane_words = lane_words + xz_words
        assert len(all_lane_words) == 40

        # Deduplicate into unique dictionary entries (`valDict`) + `vindex`
        dict_words: list[int] = []
        word_to_idx: dict[int, int] = {}
        vindex: list[int] = []
        for w in all_lane_words:
            if w not in word_to_idx:
                word_to_idx[w] = len(dict_words)
                dict_words.append(w)
            vindex.append(word_to_idx[w])

        # Analog `DOUBLE` string table for `axi_tb.vcore_v` (`totalStrChans == 1`)
        str_dict: list[str] = []
        str_to_idx: dict[str, int] = {}
        sindex: list[int] = []
        for t in range(n_ticks):
            gt = t + phase_off
            vcore = 0.850 + 0.035 * math.sin(gt * 0.45)
            sv = f"{vcore:.4f}"
            if sv not in str_to_idx:
                str_to_idx[sv] = len(str_dict)
                str_dict.append(sv)
            sindex.append(str_to_idx[sv])

        # Serialize uncompressed VZT block (`vzt_rd_block_vch_decode` order)
        blk = bytearray()
        blk.extend(encode_vzt_v32(n_ticks))
        blk.extend(encode_vzt_v64(ticks[0]))
        for t in range(1, n_ticks):
            blk.extend(encode_vzt_v64(ticks[t] - ticks[t - 1]))

        blk.extend(encode_vzt_v32(num_sections))
        blk.extend(encode_vzt_v32(len(dict_words)))
        if use_rle:
            blk.append(1)  # bp_rle = 1
            blk.extend(encode_vzt_bp_rle(dict_words))
        else:
            blk.append(0)  # bp_rle = 0
            for dw in dict_words:
                blk.extend(struct.pack(">I", dw))

        blk.extend(encode_vzt_v32(len(str_dict)))
        for s in str_dict:
            blk.extend(s.encode("ascii") + b"\x00")

        for idx in vindex:
            blk.extend(encode_vzt_v32(idx))
        for sidx in sindex:
            blk.extend(encode_vzt_v32(sidx))

        raw_blk = bytes(blk)
        if use_bz2:
            comp_blk = b"\x01" + bz2.compress(raw_blk, compresslevel=9)
        else:
            comp_blk = b"\x00" + gzip_deterministic(raw_blk)

        blk_hdr = struct.pack(">IIQQ", len(raw_blk), len(comp_blk), ticks[0], ticks[-1])
        return blk_hdr + comp_blk

    blk0_ticks = [t * 10 for t in range(16)]         # 0 .. 150 ns
    blk1_ticks = [160 + t * 10 for t in range(16)]   # 160 .. 310 ns

    block0 = encode_vzt_block(blk0_ticks, use_rle=False, use_bz2=False, phase_off=0)
    block1 = encode_vzt_block(blk1_ticks, use_rle=True, use_bz2=True, phase_off=16)

    # 36-byte VZTD v1 file header:
    #   [0..3]   "VZTD"
    #   [4..5]   version = 1 (u16BE)
    #   [6]      granule_size = 32 (u8)
    #   [7..10]  numfacs (u32BE)
    #   [11..14] numrealfacs (u32BE)
    #   [15..18] numfacbytes (u32BE)
    #   [19..22] longestname (u32BE)
    #   [23..26] zfacnamesize (u32BE)
    #   [27..30] zfacname_predec_size (u32BE)
    #   [31..34] zfacgeometrysize (u32BE)
    #   [35]     timescale = -9 (1 ns, i8)
    file_hdr = struct.pack(
        ">4sHBIIIIIIIb",
        b"VZTD",
        1,
        granule_size,
        num_facs,
        num_real_facs,
        28,
        longest_name,
        len(z_facnames),
        len(facname_raw),
        len(z_geom),
        -9,
    )
    assert len(file_hdr) == 36
    return file_hdr + z_facnames + z_geom + block0 + block1


# ---------------------------------------------------------------------------
# 10. SigMF Baseband IQ RF Capture (.sigmf-meta + .sigmf-data)
# ---------------------------------------------------------------------------

def build_sigmf_pair() -> tuple[bytes, bytes]:
    sample_rate = 2_000_000.0  # 2 MS/s
    center_freq = 915_000_000.0  # 915 MHz ISM band
    n_samples = 2048

    meta = {
        "global": {
            "core:datatype": "ci16_le",
            "core:sample_rate": sample_rate,
            "core:version": "1.0.0",
            "core:hw": "Ettus USRP B210 (915 MHz ISM LoRa/FSK Receiver)",
            "core:description": "915 MHz LoRa CSS Up-Chirp Preamble + GFSK Telemetry Ack",
        },
        "captures": [
            {
                "core:sample_start": 0,
                "core:frequency": center_freq,
                "core:datetime": "2025-05-12T10:15:00Z",
            }
        ],
        "annotations": [
            {
                "core:sample_start": 128,
                "core:sample_count": 1024,
                "core:freq_lower_edge": 914_875_000.0,
                "core:freq_upper_edge": 915_125_000.0,
                "core:label": "LoRa_SF7_BW250k_UpChirp",
            },
            {
                "core:sample_start": 1344,
                "core:sample_count": 512,
                "core:freq_lower_edge": 915_200_000.0,
                "core:freq_upper_edge": 915_350_000.0,
                "core:label": "GFSK_ACK_Burst",
            },
        ],
    }

    iq_bytes = bytearray()
    for n in range(n_samples):
        t = n / sample_rate
        if 128 <= n < 1152:
            # Linear FM LoRa up-chirp sweeping -125 kHz -> +125 kHz
            u = (n - 128) / 1024.0
            phase = 2.0 * math.pi * ((-125_000.0 * t) + 0.5 * (250_000.0 / (1024.0 / sample_rate)) * ((t - 128 / sample_rate) ** 2))
            amp = 22000.0 * (0.5 - 0.5 * math.cos(math.pi * min(1.0, u * 16.0)))
            i_val = int(round(amp * math.cos(phase)))
            q_val = int(round(amp * math.sin(phase)))
        elif 1344 <= n < 1856:
            # Offset +275 kHz GFSK burst
            sym = 1.0 if ((n - 1344) // 32) % 2 == 0 else -1.0
            freq = 275_000.0 + sym * 35_000.0
            phase = 2.0 * math.pi * freq * t
            i_val = int(round(16500.0 * math.cos(phase)))
            q_val = int(round(16500.0 * math.sin(phase)))
        else:
            # Low-amplitude deterministic receiver noise floor
            i_val = int(round(320.0 * math.sin(0.37 * n) + 180.0 * math.cos(1.13 * n)))
            q_val = int(round(320.0 * math.cos(0.41 * n) - 180.0 * math.sin(1.07 * n)))

        iq_bytes.extend(struct.pack("<hh", i_val, q_val))

    meta_bytes = (json.dumps(meta, indent=2) + "\n").encode("utf-8")
    return meta_bytes, bytes(iq_bytes)


# ---------------------------------------------------------------------------
# 11. Genuine Cadence OrCAD Capture OLE2 Schematic (.dsn) & Symbol Lib (.olb)
# ---------------------------------------------------------------------------

def _orcad_str_zero(s: str) -> bytes:
    raw = s.encode("ascii")
    return struct.pack("<H", len(raw)) + raw + b"\x00"


def _wrap_orcad_struct(type_id: int, props: list[tuple[int, int]], body: bytes) -> bytes:
    short_pfx = bytearray([type_id])
    short_pfx.extend(struct.pack("<H", len(props)))
    for ni, vi in props:
        short_pfx.extend(struct.pack("<II", ni, vi))
    hdr = struct.pack("<BII", type_id, len(short_pfx) + len(body), 0)
    return hdr + bytes(short_pfx) + body


def _build_ole2_v3(dir_nodes: list[tuple[str, int, int, int, int, bytes]]) -> bytes:
    """
    Minimal spec-exact MS-CFB v3 (512B sector, cutoff=0) builder.
    Each item in `dir_nodes`: `(name, obj_type, left_id, right_id, child_id, stream_bytes)`.
    """
    NOSTREAM = 0xFFFFFFFF
    ENDOFCHAIN = 0xFFFFFFFE
    FATSECT = 0xFFFFFFFD

    n_dir_sectors = (len(dir_nodes) + 3) // 4
    # Sector 0 = FAT; Sectors 1..n_dir_sectors = Directory; subsequent sectors = streams
    fat: list[int] = [FATSECT]
    for s in range(1, 1 + n_dir_sectors):
        fat.append(s + 1 if s < n_dir_sectors else ENDOFCHAIN)

    stream_starts: list[int] = []
    stream_blobs = bytearray()
    for _, obj_type, _, _, _, data in dir_nodes:
        if obj_type != 2 or len(data) == 0:
            stream_starts.append(ENDOFCHAIN)
            continue
        first_sec = len(fat)
        stream_starts.append(first_sec)
        n_sec = (len(data) + 511) // 512
        for k in range(n_sec):
            fat.append(first_sec + k + 1 if k + 1 < n_sec else ENDOFCHAIN)
        padded = data + b"\x00" * (n_sec * 512 - len(data))
        stream_blobs.extend(padded)

    assert len(fat) <= 128
    fat.extend([NOSTREAM] * (128 - len(fat)))
    fat_sector = b"".join(struct.pack("<I", x) for x in fat)

    dir_bytes = bytearray()
    for idx, (name, obj_type, left_id, right_id, child_id, data) in enumerate(dir_nodes):
        u16 = (name.encode("utf-16le") + b"\x00\x00")[:64]
        name_len = len(u16)
        u16_pad = u16 + b"\x00" * (64 - len(u16))
        start_sec = ENDOFCHAIN if obj_type != 2 else stream_starts[idx]
        sz = len(data) if obj_type == 2 else 0
        entry = struct.pack(
            "<64sHBBIII36sIQ",
            u16_pad,
            name_len,
            obj_type,
            1,  # black node
            left_id,
            right_id,
            child_id,
            b"\x00" * 36,
            start_sec,
            sz,
        )
        assert len(entry) == 128
        dir_bytes.extend(entry)

    while len(dir_bytes) < n_dir_sectors * 512:
        dir_bytes.extend(
            b"\x00" * 68 + struct.pack("<III", NOSTREAM, NOSTREAM, NOSTREAM) + b"\x00" * 48
        )

    hdr = bytearray(512)
    hdr[0:8] = b"\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1"
    struct.pack_into("<HHHH", hdr, 24, 0x003E, 0x0003, 0xFFFE, 9)
    struct.pack_into("<H", hdr, 32, 6)
    struct.pack_into("<II", hdr, 44, 1, 1)  # 1 FAT sector, first Dir sector = 1
    struct.pack_into("<IIIII", hdr, 56, 0, ENDOFCHAIN, 0, ENDOFCHAIN, 0)  # cutoff = 0
    struct.pack_into("<I", hdr, 76, 0)  # DIFAT[0] = sector 0
    for i in range(1, 109):
        struct.pack_into("<I", hdr, 76 + i * 4, NOSTREAM)

    return bytes(hdr) + fat_sector + bytes(dir_bytes) + bytes(stream_blobs)


def _build_orcad_library_stream(strings: list[str]) -> bytes:
    lib = bytearray(b" " * 32)
    sig = b"OrCAD Windows Design"
    lib[0 : len(sig)] = sig
    lib[len(sig)] = 0
    lib.extend(struct.pack("<HHIII", 3, 2, 0, 0, 0))
    lib.extend(struct.pack("<H", 1))  # 0 LOGFONTAs
    lib.extend(struct.pack("<H", 24))
    lib.extend(b"\x00" * (24 * 2 + 8))
    for _ in range(8):
        lib.extend(_orcad_str_zero(""))
    lib.extend(b"\x00" * 152)  # PageSettings
    lib.extend(struct.pack("<I", len(strings)))
    for s in strings:
        lib.extend(_orcad_str_zero(s))
    lib.extend(struct.pack("<H", 0))
    return bytes(lib)


def _build_orcad_libpart(part_name: str, lib_name: str, w: int, h: int, pins: list[tuple[str, int, int, int, int]]) -> bytes:
    body = bytearray()
    body.extend(_orcad_str_zero(part_name))
    body.extend(_orcad_str_zero(lib_name))
    body.extend(b"\x00" * 4)
    # 1 Rect primitive (`primType == 40`, repeated byte `40`, `[size=24, 0, x1, y1, x2, y2]`)
    body.extend(struct.pack("<HBB", 1, 40, 40))
    body.extend(struct.pack("<IIiiii", 24, 0, 0, 0, w, h))
    # SymbolPinScalar (`typeId == 26`) entries
    body.extend(struct.pack("<H", len(pins)))
    for p_name, sx, sy, hx, hy in pins:
        pb = _orcad_str_zero(p_name) + struct.pack("<iiii12s", sx, sy, hx, hy, b"\x00" * 12)
        body.extend(_wrap_orcad_struct(26, [], pb))
    body.extend(struct.pack("<H", 0))  # 0 display props
    return _wrap_orcad_struct(24, [], bytes(body))


def build_orcad_dsn_and_olb() -> tuple[bytes, bytes]:
    NO = 0xFFFFFFFF
    orc_lib = _build_orcad_library_stream(["Value", "STM32G474", "INA240A2", "Part Reference"])

    part_mcu = _build_orcad_libpart(
        "STM32G474.Normal",
        "MCU.OLB",
        60,
        50,
        [("VDD", 0, 10, -10, 10), ("PA5_SCK", 60, 15, 70, 15), ("PA7_MOSI", 60, 30, 70, 30), ("VSS", 0, 40, -10, 40)],
    )
    part_csa = _build_orcad_libpart(
        "INA240A2.Normal",
        "ANALOG.OLB",
        40,
        30,
        [("IN+", 0, 10, -10, 10), ("IN-", 0, 20, -10, 20), ("OUT", 40, 15, 50, 15)],
    )
    cache_stm = b"\x00\x00\x00\x00" + part_mcu + part_csa

    # Build Structure::Page (typeId=10)
    pg = bytearray()
    pg.extend(_orcad_str_zero("PAGE1_MCU_ADC"))
    pg.extend(_orcad_str_zero("A"))
    pset = bytearray(152)
    struct.pack_into("<III", pset, 24, 9700, 7200, 10)
    pg.extend(pset)
    pg.extend(struct.pack("<HHHH", 0, 0, 0, 2))  # 2 net table entries
    pg.extend(_orcad_str_zero("SPI_SCK") + struct.pack("<I", 1))
    pg.extend(_orcad_str_zero("SHUNT_OUT") + struct.pack("<I", 2))

    # 2 WireScalar (typeId=20) segments with Alias (typeId=49)
    pg.extend(struct.pack("<H", 2))
    for wid, x1, y1, x2, y2, net_nm in [(1, 170, 115, 250, 115, "SPI_SCK"), (2, 170, 130, 250, 130, "SHUNT_OUT")]:
        wb = bytearray(29)
        struct.pack_into("<I", wb, 4, wid)
        struct.pack_into("<iiii", wb, 12, x1, y1, x2, y2)
        wb.extend(struct.pack("<H", 1))
        ab = struct.pack("<ii12s", x1 + 15, y1, b"\x00" * 12) + _orcad_str_zero(net_nm)
        wb.extend(_wrap_orcad_struct(49, [], ab))
        wb.extend(b"\x00" * 10)
        pg.extend(_wrap_orcad_struct(20, [], bytes(wb)))

    # 2 PlacedInstance (typeId=13) symbols: U1 (STM32G474) & U2 (INA240A2)
    pg.extend(struct.pack("<H", 2))
    for pkg_nm, ref_des, val_idx, lx, ly in [("STM32G474.Normal", "U1", 1, 100, 100), ("INA240A2.Normal", "U2", 2, 260, 115)]:
        ib = bytearray(b"\x00" * 8)
        ib.extend(_orcad_str_zero(pkg_nm))
        tail = bytearray(22)
        struct.pack_into("<HH", tail, 12, lx, ly)
        ib.extend(tail)
        ib.append(0)
        ib.extend(_orcad_str_zero(ref_des))
        ib.extend(b"\x00" * 16)
        ib.extend(_orcad_str_zero(pkg_nm.split(".")[0]))
        ib.extend(b"\x00\x00")
        pg.extend(_wrap_orcad_struct(13, [(0, val_idx)], bytes(ib)))

    pg.extend(struct.pack("<8H", 0, 0, 0, 0, 0, 0, 0, 0))
    page_stm = _wrap_orcad_struct(10, [], bytes(pg))

    dsn_bytes = _build_ole2_v3([
        ("Root Entry", 5, NO, NO, 1, b""),
        ("Library",    2, NO, 2,  NO, orc_lib),
        ("Cache",      2, NO, 3,  NO, cache_stm),
        ("Views",      1, NO, NO, 4,  b""),
        ("SCHEMATIC1", 1, NO, NO, 5,  b""),
        ("Pages",      1, NO, NO, 6,  b""),
        ("PAGE1_MCU",  2, NO, NO, NO, page_stm),
    ])

    olb_bytes = _build_ole2_v3([
        ("Root Entry", 5, NO, NO, 1, b""),
        ("Library",    2, NO, 2,  NO, orc_lib),
        ("Packages",   1, NO, NO, 3, b""),
        ("INA240_CSA", 2, NO, 4,  NO, b"\x00\x00\x00\x00" + part_csa),
        ("STM32G474",  2, NO, NO, NO, b"\x00\x00\x00\x00" + part_mcu),
    ])
    return dsn_bytes, olb_bytes


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def generate_all(out_root: pathlib.Path) -> list[pathlib.Path]:
    eda_dir = out_root / "eda"
    wav_dir = out_root / "waveforms"
    eda_dir.mkdir(parents=True, exist_ok=True)
    wav_dir.mkdir(parents=True, exist_ok=True)

    written: list[pathlib.Path] = []

    def write_one(path: pathlib.Path, content: bytes) -> None:
        path.write_bytes(content)
        written.append(path)

    # 1..6: EDA / VLSI / FPGA samples
    write_one(eda_dir / "buck_converter.qsch", build_qspice_qsch())
    write_one(eda_dir / "multilayer_6l_hd.dru", build_eagle_dru())
    write_one(eda_dir / "cmos_nand2_cell.msk", build_microwind_msk())
    write_one(eda_dir / "alu_slice_4bit.eqn", build_berkeley_eqn())
    write_one(eda_dir / "max10_isp_prog.jbc", build_altera_jbc())
    for fname, blob in build_ispd_bookshelf().items():
        write_one(eda_dir / fname, blob)
    orc_dsn, orc_olb = build_orcad_dsn_and_olb()
    write_one(eda_dir / "orcad_capture_mcu.dsn", orc_dsn)
    write_one(eda_dir / "orcad_analog_lib.olb", orc_olb)

    # 7..10: Oscilloscope / Logic Analyzer / GTKWave VZT / SigMF RF samples
    wvf_hdr, wvf_bin = build_yokogawa_wvf_pair()
    write_one(wav_dir / "yokogawa_dl750_motor.hdr", wvf_hdr)
    write_one(wav_dir / "yokogawa_dl750_motor.wvf", wvf_bin)
    write_one(wav_dir / "dslogic_spi_flash.dsl", build_dreamsourcelab_dsl())
    write_one(wav_dir / "riscv_axi_bus.vzt", build_gtkwave_vzt())
    sigmf_meta, sigmf_data = build_sigmf_pair()
    write_one(wav_dir / "ism915_lora_burst.sigmf-meta", sigmf_meta)
    write_one(wav_dir / "ism915_lora_burst.sigmf-data", sigmf_data)

    return written


import zlib

from pathlib import Path


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


def _tekhex_rec(rec_type: str, addr_hex: str, data_hex: str = "") -> str:
    addr_len_nib = f"{len(addr_hex) & 0xF:X}"
    body_no_csum_len = 2 + 1 + 2 + 1 + len(addr_hex) + len(data_hex)
    ll = f"{body_no_csum_len:02X}"
    raw_for_sum = ll + rec_type + addr_len_nib + addr_hex + data_hex
    csum = sum(int(ch, 16) for ch in raw_for_sum) & 0xFF
    return f"%{ll}{rec_type}{csum:02X}{addr_len_nib}{addr_hex}{data_hex}\n"

def gen_domain_ti_txt(out_dir: Path):
    # MSP430G2553 bootloader @8000 (code), @1000 (info flash calibration), @FFE0 (interrupt + reset vector 0x8000)
    lines = ["@8000"]
    code_bytes = [
        (0x31 + (i * 13)) & 0xFF for i in range(64)
    ]
    for off in range(0, len(code_bytes), 16):
        lines.append(" ".join(f"{b:02X}" for b in code_bytes[off:off + 16]))
    lines.append("@1000")
    cal_bytes = [0xAA, 0x55, 0x86, 0x8D, 0x01, 0x00, 0x10, 0x20, 0xFF, 0xFF, 0x3C, 0x42, 0x19, 0x7A, 0x88, 0x99]
    lines.append(" ".join(f"{b:02X}" for b in cal_bytes))
    lines.append("@FFE0")
    ivt = [0xFF] * 30 + [0x00, 0x80]  # Reset vector @ 0xFFFE = 0x8000 (little-endian: 00 80)
    lines.append(" ".join(f"{b:02X}" for b in ivt[:16]))
    lines.append(" ".join(f"{b:02X}" for b in ivt[16:]))
    lines.append("q")
    (out_dir / "msp430_bootloader.titxt").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))

def gen_domain_xilinx_coe(out_dir: Path):
    # Symmetric 25-tap Hamming-windowed sinc low-pass FIR filter for Xilinx FIR Compiler
    taps = []
    n = 25
    mid = (n - 1) / 2.0
    for i in range(n):
        x = i - mid
        sinc = 1.0 if abs(x) < 1e-9 else math.sin(0.35 * math.pi * x) / (0.35 * math.pi * x)
        win = 0.54 - 0.46 * math.cos(2.0 * math.pi * i / (n - 1))
        taps.append(int(round(sinc * win * 32767.0)))
    tap_str = ",\n  ".join(str(v) for v in taps)
    text = f"""; Xilinx Vivado FIR Compiler 25-Tap Low-Pass Filter
radix = 10;
coefwidth = 16;
coefdata =
  {tap_str};
"""
    (out_dir / "vivado_fir_lut.coe").write_bytes(text.encode("utf-8"))

def gen_domain_quartus_mif(out_dir: Path):
    # 64 x 12-bit quarter-wave DDS sine lookup ROM + zero-filled guard band [38..3F]
    lines = [
        "% Intel Quartus Prime DDS Quarter-Wave Sine ROM %",
        "-- 64 words x 12-bit unsigned hex",
        "DEPTH = 64;",
        "WIDTH = 12;",
        "ADDRESS_RADIX = HEX;",
        "DATA_RADIX = HEX;",
        "",
        "CONTENT",
        "BEGIN",
    ]
    for a in range(0x38):
        s = int(round((0.5 + 0.49 * math.sin(2.0 * math.pi * a / 56.0)) * 4095.0))
        lines.append(f"  {a:02X} : {s:03X};")
    lines.append("  [38..3F] : 000;")
    lines.append("END;")
    (out_dir / "cyclone_sine_rom.mif").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))

def gen_domain_mentor_pads(out_dir: Path):
    text = """!PADS-POWERPCB-V9.5-MILS! DESIGN DATABASE ASCII FILE 1.0
*PADS-PCB*
MILS

*PARTDECAL*
SOIC8 I 0 0 0 8 1
T-100  75 -100  75 1
T-100  25 -100  25 2
T-100 -25 -100 -25 3
T-100 -75 -100 -75 4
T 100 -75  100 -75 5
T 100 -25  100 -25 6
T 100  25  100  25 7
T 100  75  100  75 8
PAD 0 1
1 55 R

R0805 I 0 0 0 2 1
T-40 0 -40 0 1
T 40 0  40 0 2
PAD 0 1
1 48 S

*PART*
U1 LNA_AMP@SOIC8 600 500 0 U N
R1 50R@R0805     250 575 0 U N
R2 100R@R0805    950 425 90 U M
C1 100pF@R0805   600 220 0 U N

*LINES*
OUTLINE1 BOARD 5 10
0 0
1200 0
1200 900
0 900
0 0

*ROUTE*
*SIGNAL* RF_IN 12
100 575 1 210 575 1 12
290 575 1 500 575 1 12
*SIGNAL* RF_OUT 12
700 425 1 820 425 2 12
820 425 2 950 385 2 12
*SIGNAL* VCC_3V3 18
600 575 1 600 780 1 18

*END*
"""
    (out_dir / "mentor_rf_board.pads").write_bytes(text.encode("utf-8"))

def gen_domain_protel_pcb(out_dir: Path):
    text = """|RECORD=Board|FILENAME=protel99_psu.ddb|KIND=Protel_Advanced_PCB|VERSION=3.00|
|RECORD=Net|ID=0|NAME=VIN_12V|
|RECORD=Net|ID=1|NAME=VOUT_5V|
|RECORD=Net|ID=2|NAME=GND|
|RECORD=Component|ID=0|SOURCEDESIG=U1|PATTERN=SOT-223|COMMENT=LM1117-5.0|X=20mm|Y=15mm|ROTATION=0|LAYER=TOP|
|RECORD=Component|ID=1|SOURCEDESIG=C1|PATTERN=0805|COMMENT=10uF|X=10mm|Y=15mm|ROTATION=90|LAYER=TOP|
|RECORD=Component|ID=2|SOURCEDESIG=C2|PATTERN=0805|COMMENT=22uF|X=30mm|Y=15mm|ROTATION=90|LAYER=TOP|
|RECORD=Component|ID=3|SOURCEDESIG=D1|PATTERN=SOD-123|COMMENT=SS14|X=20mm|Y=8mm|ROTATION=180|LAYER=BOTTOM|
|RECORD=Pad|NAME=1|X=17.7mm|Y=12mm|XSIZE=1.4mm|YSIZE=2.0mm|HOLESIZE=0mm|SHAPE=RECTANGULAR|LAYER=TOP|
|RECORD=Pad|NAME=2|X=20.0mm|Y=12mm|XSIZE=1.4mm|YSIZE=2.0mm|HOLESIZE=0mm|SHAPE=RECTANGULAR|LAYER=TOP|
|RECORD=Pad|NAME=3|X=22.3mm|Y=12mm|XSIZE=1.4mm|YSIZE=2.0mm|HOLESIZE=0mm|SHAPE=RECTANGULAR|LAYER=TOP|
|RECORD=Pad|NAME=4|X=20.0mm|Y=18mm|XSIZE=3.2mm|YSIZE=2.2mm|HOLESIZE=0mm|SHAPE=RECTANGULAR|LAYER=TOP|
|RECORD=Pad|NAME=J1|X=4mm|Y=15mm|XSIZE=2.2mm|YSIZE=2.2mm|HOLESIZE=1.0mm|SHAPE=ROUND|LAYER=MULTILAYER|
|RECORD=Pad|NAME=J2|X=36mm|Y=15mm|XSIZE=2.2mm|YSIZE=2.2mm|HOLESIZE=1.0mm|SHAPE=ROUND|LAYER=MULTILAYER|
|RECORD=Track|X1=0mm|Y1=0mm|X2=40mm|Y2=0mm|WIDTH=0.25mm|LAYER=KEEPOUT|
|RECORD=Track|X1=40mm|Y1=0mm|X2=40mm|Y2=26mm|WIDTH=0.25mm|LAYER=KEEPOUT|
|RECORD=Track|X1=40mm|Y1=26mm|X2=0mm|Y2=26mm|WIDTH=0.25mm|LAYER=KEEPOUT|
|RECORD=Track|X1=0mm|Y1=26mm|X2=0mm|Y2=0mm|WIDTH=0.25mm|LAYER=KEEPOUT|
|RECORD=Track|X1=4mm|Y1=15mm|X2=17.7mm|Y2=12mm|WIDTH=0.6mm|LAYER=TOP|NET=0|
|RECORD=Track|X1=22.3mm|Y1=12mm|X2=36mm|Y2=15mm|WIDTH=0.6mm|LAYER=TOP|NET=1|
|RECORD=Via|X=20mm|Y=21mm|DIAMETER=0.9mm|HOLESIZE=0.45mm|NET=2|
|RECORD=Arc|X=4mm|Y=22mm|RADIUS=1.6mm|WIDTH=0.2mm|LAYER=TOPOVERLAY|
"""
    (out_dir / "protel99_psu.protel").write_bytes(text.encode("utf-8"))

def gen_domain_cadstar_cpa(out_dir: Path):
    text = """CADSTAR PCB ARCHIVE V16.0
(CADSTAR_PCB
  (HEADER
    (RESOLUTION MM 1000)
  )
  (ASSIGNMENTS
    (LAYERDEFS
      (LAYER L1 "Top Elec" ELEC)
      (LAYER L2 "Bottom Elec" ELEC)
      (LAYER L3 "Top Silk" DOC)
      (LAYER L4 "Board Outline" BOARD)
    )
    (CODEDEFS
      (ROUTECODE W_SIG 300)
      (ROUTECODE W_PWR 800)
    )
  )
  (LAYOUT
    (BOARD
      (CLOSEDLINE (PT 0 0) (PT 48000 0) (PT 48000 32000) (PT 0 32000))
    )
    (COMPONENTS
      (COMP C1 U1 DRV8833_QFN16 (PT 24000 16000) (ORIENT 0))
      (COMP C2 Q1 MOSFET_SOT23 (PT 12000 10000) (ORIENT 90))
      (COMP C3 C1 CAP_0805 (PT 24000 25000) (ORIENT 0))
      (COMP C4 R1 RES_0603 (PT 36000 16000) (ORIENT 180) (MIRROR))
    )
    (NETS
      (NET N1 VMOT_24V
        (TRACK L1 W_PWR (PT 4000 25000) (PT 24000 25000) (PT 24000 18000))
        (VIA (PT 24000 21000))
      )
      (NET N2 PHASE_A
        (TRACK L1 W_SIG (PT 26000 16000) (PT 36000 16000) (PT 44000 12000))
      )
      (NET N3 PHASE_B
        (TRACK L2 W_SIG (PT 22000 16000) (PT 12000 10000) (PT 4000 10000))
        (VIA (PT 16000 13000))
      )
    )
  )
)
"""
    (out_dir / "zuken_motor_ctrl.cpa").write_bytes(text.encode("utf-8"))

def gen_domain_magic_mag(out_dir: Path):
    text = """magic
tech sky130A
magscale 1 2
timestamp 1700000000
<< nwell >>
rect -10 48 90 108
<< pdiff >>
rect 10 62 70 92
<< ndiff >>
rect 10 12 70 40
<< polysilicon >>
rect 24 4 32 100
rect 48 4 56 100
<< localinterconnect >>
rect 0 96 80 106
rect 0 0 80 10
rect 60 22 72 76
<< met1 >>
rect -6 94 86 108
rect -6 -2 86 12
rect 22 44 34 56
rect 46 44 58 56
rect 62 44 84 56
<< mcon >>
rect 26 48 30 52
rect 50 48 54 52
rect 64 48 68 52
rect 14 98 18 102
rect 38 2 42 6
<< labels >>
rlabel met1 -6 94 4 108 1 VPWR
rlabel met1 -6 -2 4 12 1 VGND
rlabel met1 22 44 34 56 1 A
rlabel met1 46 44 58 56 1 B
rlabel met1 76 44 84 56 3 Y
<< end >>
"""
    (out_dir / "sky130_nand2_cell.mag").write_bytes(text.encode("utf-8"))

def gen_domain_espresso_pla(out_dir: Path):
    # 4-input ALU Opcode Control Decoder (op[3:0] -> add_en, sub_en, shift_en, branch_en)
    text = """# Berkeley Espresso PLA: 4-bit RISC ALU Control Decoder
.i 4
.o 4
.ilb op3 op2 op1 op0
.ob add_en sub_en shift_en branch_en
.p 6
.type fd
0000 1000
0001 1000
0010 0100
01-0 0010
100- 0001
1111 1100
.e
"""
    (out_dir / "alu_decoder.pla").write_bytes(text.encode("utf-8"))

def gen_domain_iscas_bench(out_dir: Path):
    # Authentic ISCAS-85 c17 combinational benchmark + sequential DFF status latch
    text = """# ISCAS-85 c17 + DFF Registered Parity Netlist
INPUT(N1)
INPUT(N2)
INPUT(N3)
INPUT(N6)
INPUT(N7)
OUTPUT(N22)
OUTPUT(N23)
OUTPUT(Q_PAR)

N10 = NAND(N1, N3)
N11 = NAND(N3, N6)
N16 = NAND(N2, N11)
N19 = NAND(N11, N7)
N22 = NAND(N10, N16)
N23 = NAND(N16, N19)
P_XOR = XOR(N22, N23)
Q_PAR = DFF(P_XOR)
"""
    (out_dir / "iscas85_c17_adder.bench").write_bytes(text.encode("utf-8"))

def gen_domain_kayak_kcd(out_dir: Path):
    text = """<?xml version="1.0" encoding="UTF-8"?>
<NetworkDefinition xmlns="http://kayak.2codeornot2code.org/1.0" version="1.0">
  <Document name="EV Powertrain CAN" version="2.1"/>
  <Node id="1" name="VCU_Master"/>
  <Node id="2" name="Inverter_MCU"/>
  <Node id="3" name="BMS_Pack"/>
  <Bus name="PT_CAN_FD" baudrate="500000">
    <Message id="0x101" name="TorqueCommand" length="8" interval="10">
      <Producer><NodeRef id="1"/></Producer>
      <Signal name="ReqTorqueNm" offset="0" length="16"><Value slope="0.1" intercept="-500" unit="Nm"/></Signal>
      <Signal name="DriveMode" offset="16" length="4"/>
      <Signal name="AliveCounter" offset="56" length="8"/>
    </Message>
    <Message id="0x185" name="MotorTelemetry" length="8" interval="10">
      <Producer><NodeRef id="2"/></Producer>
      <Signal name="RotorRPM" offset="0" length="16"><Value slope="1" unit="rpm"/></Signal>
      <Signal name="StatorTempC" offset="16" length="8"><Value slope="1" intercept="-40" unit="C"/></Signal>
      <Signal name="PhaseCurrentA" offset="24" length="16"><Value slope="0.1" unit="A"/></Signal>
    </Message>
    <Message id="0x220" name="BatteryPackState" length="8" interval="50">
      <Producer><NodeRef id="3"/></Producer>
      <Signal name="PackVoltageV" offset="0" length="16"><Value slope="0.05" unit="V"/></Signal>
      <Signal name="StateOfCharge" offset="16" length="8"><Value slope="0.5" unit="%"/></Signal>
      <Signal name="MaxDischargeKw" offset="24" length="16"><Value slope="0.1" unit="kW"/></Signal>
    </Message>
  </Bus>
</NetworkDefinition>
"""
    (out_dir / "ev_powertrain_bus.kcd").write_bytes(text.encode("utf-8"))

def gen_domain_socketcan_candump(out_dir: Path):
    lines = [
        "# SocketCAN candump -l capture: OBD-II + EV Inverter CAN FD",
        "(1700000100.000000) can0 7DF#02010C0000000000",
        "(1700000100.004200) can0 7E8#04410C1AF8000000",
        "(1700000100.010000) can0 18DAF110#064100BE3FA81300",
        "(1700000100.020000) can0 1F0##1102030405060708090A0B0C0",
        "(1700000100.050000) can0 7DF#02010D0000000000",
        "(1700000100.054100) can0 7E8#03410D5800000000",
        "(1700000100.100000) can0 1F0##124384C6074889CA0B4C8DCF0",
        "(1700000100.150000) can0 7E8#04410C2260000000",
        "(1700000100.200000) vcan1 123#R4",
        "(1700000100.250000) can0 18DAF110#0441057B00000000",
    ]
    (out_dir / "obd2_drive_cycle.candump").write_bytes(("\n".join(lines) + "\n").encode("ascii"))

def gen_domain_agilent_cti(out_dir: Path):
    n_pts = 41
    freqs = [2.0e9 + (1.0e9 * i) / (n_pts - 1) for i in range(n_pts)]
    f0 = 2.45e9
    bw = 0.12e9

    s11_ri = []
    s21_ri = []
    for f in freqs:
        # Coupled-resonator bandpass transfer function around 2.45 GHz
        x = (f - f0) / (0.5 * bw)
        denom_re = 1.0 - 0.5 * (x * x - 1.0)
        denom_im = 1.41421356 * x
        d2 = denom_re * denom_re + denom_im * denom_im
        # S21 = 1 / (denom_re + j*denom_im)
        t_re = denom_re / d2
        t_im = -denom_im / d2
        mag2 = min(0.9999, t_re * t_re + t_im * t_im)
        r_mag = math.sqrt(max(0.0, 1.0 - mag2))
        r_ang = math.atan2(t_im, t_re) + 0.5 * math.pi * (1.0 if x >= 0 else -1.0)
        s11_ri.append((r_mag * math.cos(r_ang), r_mag * math.sin(r_ang)))
        s21_ri.append((t_re, t_im))

    lines = [
        "CITIFILE A.01.01",
        "! Keysight PNA 2.45 GHz ISM Bandpass Filter 2-Port S-Parameters",
        "NAME BPF_2G45_ISM",
        f"VAR FREQ MAG {n_pts}",
        "DATA S[1,1] RI",
        "DATA S[2,1] RI",
        "VAR_LIST_BEGIN",
    ]
    for f in freqs:
        lines.append(f"{f:.1f}")
    lines.append("VAR_LIST_END")

    for block in (s11_ri, s21_ri):
        lines.append("BEGIN")
        for re_v, im_v in block:
            lines.append(f"{re_v:.7f},{im_v:.7f}")
        lines.append("END")

    (out_dir / "keysight_bandpass.cti").write_bytes(("\n".join(lines) + "\n").encode("ascii"))

def gen_domain_comtrade_cff(out_dir: Path):
    n_samples = 64
    fs = 1600.0  # 32 samples per 50 Hz cycle
    cfg_lines = [
        "--- file type: CFG ---",
        "WARSAW_110KV_SUB,IED_REL670_L1,2013",
        "6,4A,2D",
        "1,VA_BUS,A,LINE1,kV,0.01,0.0,0.0,-32767,32767,110.0,0.1,P",
        "2,VB_BUS,B,LINE1,kV,0.01,0.0,0.0,-32767,32767,110.0,0.1,P",
        "3,VC_BUS,C,LINE1,kV,0.01,0.0,0.0,-32767,32767,110.0,0.1,P",
        "4,IA_FAULT,A,LINE1,A,0.5,0.0,0.0,-32767,32767,1000.0,1.0,P",
        "1,DIST_Z1_TRIP,,,0",
        "2,CB_52A_OPEN,,,0",
        "50.0",
        "1",
        f"{fs:.1f},{n_samples}",
        "22/11/2026,12:00:00.000000",
        "22/11/2026,12:00:00.012500",
        "ASCII",
        "1.0",
        "--- file type: DAT ASCII ---",
    ]
    dat_lines = []
    for k in range(n_samples):
        t_s = k / fs
        t_us = int(round(t_s * 1e6))
        wt = 2.0 * math.pi * 50.0 * t_s
        in_fault = 20 <= k < 48
        cleared = k >= 48
        va_kv = (35.0 if in_fault else 90.0) * math.sin(wt)
        vb_kv = 90.0 * math.sin(wt - 2.0 * math.pi / 3.0)
        vc_kv = 90.0 * math.sin(wt + 2.0 * math.pi / 3.0)
        ia_amp = (0.0 if cleared else (4200.0 if in_fault else 320.0)) * math.sin(wt - 0.35)
        raw_va = int(round(va_kv / 0.01))
        raw_vb = int(round(vb_kv / 0.01))
        raw_vc = int(round(vc_kv / 0.01))
        raw_ia = int(round(ia_amp / 0.5))
        d_trip = 1 if k >= 28 else 0
        d_open = 1 if cleared else 0
        dat_lines.append(f"{k + 1},{t_us},{raw_va},{raw_vb},{raw_vc},{raw_ia},{d_trip},{d_open}")

    (out_dir / "substation_fault_110kv.cff").write_bytes(
        ("\n".join(cfg_lines + dat_lines) + "\n").encode("ascii")
    )

def gen_domain_gtkwave_lxt2(out_dir: Path):
    # 6 facilities: clk, rst_n, pc[31:0], alu_op[3:0], vdd_core (DOUBLE), clk_alias (ALIAS -> 0)
    names = [
        "top.clk",
        "top.rst_n",
        "top.pc",
        "top.alu_op",
        "top.vdd_core",
        "top.clk_alias",
    ]
    facname_raw = bytearray()
    prev = ""
    longest = 0
    for nm in names:
        longest = max(longest, len(nm))
        pfx = 0
        while pfx < len(prev) and pfx < len(nm) and prev[pfx] == nm[pfx]:
            pfx += 1
        sfx = nm[pfx:].encode("ascii") + b"\x00"
        facname_raw += struct.pack(">H", pfx) + sfx
        prev = nm
    zfacnames = gzip.compress(bytes(facname_raw), mtime=0)

    # Geometry: (rows, msb, lsb, flags)
    geoms = [
        (0, 0, 0, 0x00),   # 0: top.clk (1-bit)
        (0, 0, 0, 0x00),   # 1: top.rst_n (1-bit)
        (0, 31, 0, 0x00),  # 2: top.pc[31:0]
        (0, 3, 0, 0x00),   # 3: top.alu_op[3:0]
        (0, 0, 0, 0x02),   # 4: top.vdd_core (SYM_F_DOUBLE)
        (0, 0, 0, 0x08),   # 5: top.clk_alias (SYM_F_ALIAS -> fac 0)
    ]
    geom_raw = b"".join(struct.pack(">IIII", r, m, l, fl) for (r, m, l, fl) in geoms)
    zgeom = gzip.compress(geom_raw, mtime=0)

    # Build 1 granule block with 10 timestamps (0..90 ns)
    times = [t * 10 for t in range(10)]
    dict_strs = [
        b"10000000\x00",  # idx 0: PC = 0x80
        b"1010\x00",      # idx 1: alu_op = 0xA
        b"0.850\x00",     # idx 2: vdd = 0.850 V
        b"0.885\x00",     # idx 3: vdd = 0.885 V
        b"0.842\x00",     # idx 4: vdd = 0.842 V
    ]
    dict_blob = b"".join(dict_strs)

    # 32-bit granule bitmasks (granule_size = 32):
    # map 0 (clk): changes at all 10 ticks (0x03FF)
    # map 1 (rst_n): changes at t=0 ('0') and t=2 ('1') -> (1<<0)|(1<<2) = 0x0005
    # map 2 (pc): changes at t=0,2,4,6,8 -> 0x0155
    # map 3 (alu_op): changes at t=0,3,6,9 -> (1<<0)|(1<<3)|(1<<6)|(1<<9) = 0x0249
    # map 4 (vdd_core): changes at t=0,4,8 -> (1<<0)|(1<<4)|(1<<8) = 0x0111
    masks = [0x03FF, 0x0005, 0x0155, 0x0249, 0x0111]
    map_blob = b"".join(struct.pack(">I", m) for m in masks)

    # Opcodes per changing non-alias fac:
    # fac 0 (clk): ENC_0 (0), then 9x ENC_INV (2)
    op_clk = bytes([0] + [2] * 9)
    # fac 1 (rst_n): ENC_0 (0), ENC_1 (1)
    op_rst = bytes([0, 1])
    # fac 2 (pc): t0->DICT8(0) [18,0], t2->ADD4(10), t4->ADD4(10), t6->ADD4(10), t8->ADD4(10)
    op_pc = bytes([18, 0, 10, 10, 10, 10])
    # fac 3 (alu_op): t0->ENC_0(0), t3->DICT8(1) [18,1], t6->LSH1(4), t9->INV(2)
    op_alu = bytes([0, 18, 1, 4, 2])
    # fac 4 (vdd_core): t0->DICT8(2) [18,2], t4->DICT8(3) [18,3], t8->DICT8(4) [18,4]
    op_vdd = bytes([18, 2, 18, 3, 18, 4])

    fac_ops = [op_clk, op_rst, op_pc, op_alu, op_vdd]
    curpos = []
    running = 0
    for op in fac_ops:
        curpos.append(running)
        running += len(op)

    gran = bytearray()
    gran.append(0)  # LXT2_RD_GRAN_SECT_TIME
    gran.append(len(times))
    for tv in times:
        gran += struct.pack(">Q", tv)
    gran.append(1)  # fac_map_index_width = 1 byte (5 non-alias facs -> map indices 1..5)
    for m_idx in range(1, 6):
        gran.append(m_idx)
    gran.append(1)  # fac_curpos_width = 1 byte
    for cp in curpos:
        gran.append(cp)
    for op in fac_ops:
        gran += op

    blk_uncomp = bytes(gran) + map_blob + dict_blob + struct.pack(">III", len(dict_strs), len(dict_blob), len(masks))
    blk_comp = gzip.compress(blk_uncomp, mtime=0)
    blk_hdr = struct.pack(">QQQQ", len(blk_uncomp), len(blk_comp), times[0], times[-1])

    hdr = struct.pack(
        ">HHBIIIIIIb",
        0x1380,                 # hdrid
        0x0001,                 # version
        32,                     # granule_size = 32
        len(names),             # numfacs = 6
        len(facname_raw),       # numfacbytes
        longest,                # longestname
        len(zfacnames),         # zfacnamesize
        len(facname_raw),       # zfacname_predec_size
        len(zgeom),             # zfacgeometrysize
        -9,                     # timescale = 10^-9 s (1 ns)
    )
    assert len(hdr) == 30
    (out_dir / "riscv_pipeline.lxt2").write_bytes(hdr + zfacnames + zgeom + blk_hdr + blk_comp)

def gen_domain_ieee_spef(out_dir: Path):
    text = """*SPEF "IEEE 1481-2009"
*DESIGN "riscv_alu_tile"
*DATE "Sun Nov 22 12:00:00 2026"
*VENDOR "Explorer2 RC Extractor"
*PROGRAM "StarRC_Synth"
*VERSION "1.0"
*DESIGN_FLOW "PIN_CAP NONE" "NAME_SCOPE LOCAL"
*DIVIDER /
*DELIMITER :
*BUS_DELIMITER [ ]
*T_UNIT 1 PS
*C_UNIT 1 FF
*R_UNIT 1 OHM
*L_UNIT 1 NH

*NAME_MAP
*1 clk_core
*2 u_clk_buf_l1
*3 u_pc_reg_0
*4 u_pc_reg_1
*5 alu_carry_n3
*6 u_cla_g0
*7 u_sum_xor_7

*PORTS
*1 I *C 0.0 20.0

*D_NET *1 42.6
*CONN
*P *1 I *C 0.0 20.0
*I *2:Z O *C 8.0 20.0 *D CLKBUF_X8
*I *3:CK I *C 38.0 24.0 *D DFFRPQ_X2
*I *4:CK I *C 42.0 16.0 *D DFFRPQ_X2
*CAP
1 *2:Z 8.4
2 *1:1 11.2
3 *3:CK 11.5
4 *4:CK 11.5
*RES
1 *2:Z *1:1 6.8
2 *1:1 *3:CK 9.4
3 *1:1 *4:CK 10.2
*END

*D_NET *5 24.8
*CONN
*I *6:Y O *C 10.0 6.0 *D AOI21_X2
*I *7:A I *C 36.0 6.0 *D XOR2_X2
*CAP
1 *6:Y 7.2
2 *5:1 8.1
3 *7:A 6.0
4 *5:1 *1:1 3.5
*RES
1 *6:Y *5:1 14.5
2 *5:1 *7:A 12.0
*END
"""
    (out_dir / "asic_clock_tree.spef").write_bytes(text.encode("utf-8"))

def gen_domain_ieee_upf(out_dir: Path):
    text = """# IEEE 1801-2018 Low-Power Intent for Heterogeneous Edge AI SoC
upf_version 2.1
set_design_top edge_ai_soc_top

create_supply_port VDD_AON -direction in
create_supply_port VDD_CPU -direction in
create_supply_port VDD_NPU -direction in
create_supply_port VSS     -direction in

create_supply_net VDD_AON
create_supply_net VDD_CPU
create_supply_net VDD_CPU_SW
create_supply_net VDD_NPU
create_supply_net VDD_NPU_SW
create_supply_net VSS

create_power_domain PD_TOP -include_scope \\
    -elements {u_pmu u_always_on_rtc}
set_domain_supply_net PD_TOP -primary_power_net VDD_AON -primary_ground_net VSS

create_power_domain PD_CPU \\
    -elements {u_riscv_cluster u_l2_sram}
set_domain_supply_net PD_CPU -primary_power_net VDD_CPU_SW -primary_ground_net VSS

create_power_domain PD_NPU \\
    -elements {u_systolic_mac u_weight_buf}
set_domain_supply_net PD_NPU -primary_power_net VDD_NPU_SW -primary_ground_net VSS

create_power_switch psw_cpu_hdr \\
    -domain PD_CPU \\
    -input_supply_port {vin VDD_CPU} \\
    -output_supply_port {vout VDD_CPU_SW} \\
    -control_port {sleep_en u_pmu/cpu_pwr_en}

create_power_switch psw_npu_hdr \\
    -domain PD_NPU \\
    -input_supply_port {vin VDD_NPU} \\
    -output_supply_port {vout VDD_NPU_SW} \\
    -control_port {sleep_en u_pmu/npu_pwr_en}

set_isolation iso_cpu_out -domain PD_CPU -isolation_power_net VDD_AON -clamp_value 0 -applies_to outputs
set_level_shifter ls_cpu_to_top -domain PD_CPU -applies_to outputs -rule low_to_high
set_retention ret_cpu_state -domain PD_CPU -retention_power_net VDD_AON

set_isolation iso_npu_out -domain PD_NPU -isolation_power_net VDD_AON -clamp_value 0 -applies_to outputs

create_pst soc_pst -supplies {VDD_AON VDD_CPU_SW VDD_NPU_SW}
add_pst_state FULL_TURBO    -pst soc_pst -state {0.90V 0.85V 0.80V}
add_pst_state NPU_GATED     -pst soc_pst -state {0.90V 0.85V OFF}
add_pst_state CPU_RETENTION -pst soc_pst -state {0.90V OFF   OFF}
"""
    (out_dir / "soc_power_islands.upf").write_bytes(text.encode("utf-8"))

def gen_domain_tektronix_tek(out_dir: Path):
    lines = [
        _tekhex_rec("6", "1000", "0102030405060708090A0B0C0D0E0F10"),
        _tekhex_rec("6", "1010", "A55AA55ADEADBEEFCAFEBABE12345678"),
        _tekhex_rec("6", "8000", "31FF0F210010CD2080C3008076767676"),
        _tekhex_rec("8", "8000", ""),
    ]
    (out_dir / "z80_monitor_rom.tek").write_bytes("".join(lines).encode("ascii"))

def gen_domain_verilog_mem(out_dir: Path):
    mem_txt = """// RISC-V RV32I BootROM + Trap Vector ($readmemh / Vivado updatemem)
/* Reset section @ 0x0000 */
@00000000
0000_0297 0102_8293 3052_9073 0000_006F
DEAD_BEEF 1234_5678 9ABC_DEF0 0FED_CBA9

// Machine trap handler @ 0x0100
@00000100
3420_2F73 000F_5463 3020_0073 0000_0013
"""
    (out_dir / "riscv_bootrom.mem").write_bytes(mem_txt.encode("ascii"))

def gen_domain_xilinx_bmm(out_dir: Path):
    bmm_txt = """// MicroBlaze LMB 16 KB BRAM floorplan (4 x RAMB36 byte lanes)
ADDRESS_MAP microblaze_0 MICROBLAZE-LE 100
    ADDRESS_SPACE lmb_bram_combined RAMB32 [0x00000000:0x00003FFF]
        BUS_BLOCK
            mb_sys/lmb_bram/RAMB36_0 [31:24] PLACED = X0Y4;
            mb_sys/lmb_bram/RAMB36_1 [23:16] PLACED = X0Y5;
            mb_sys/lmb_bram/RAMB36_2 [15:8]  PLACED = X1Y4;
            mb_sys/lmb_bram/RAMB36_3 [7:0]   PLACED = X1Y5;
        END_BUS_BLOCK;
    END_ADDRESS_SPACE;
END_ADDRESS_MAP;
"""
    (out_dir / "microblaze_lmb.bmm").write_bytes(bmm_txt.encode("ascii"))

def gen_domain_icestorm_pcf(out_dir: Path):
    pcf_txt = """# iCEBreaker V1.0e iCE40UP5K-SG48 QFN48 Pin Constraints
set_io -nowarn clk_12mhz     35
set_frequency clk_12mhz      12.0
set_io -nowarn -pullup yes btn_n 10
set_io --warn-no-port led_red_n  11
set_io --warn-no-port led_grn_n  37
set_io uart_rx 6
set_io uart_tx 9
set_io spi_sck 15
set_io spi_ssn 16
"""
    (out_dir / "icebreaker_up5k.pcf").write_bytes(pcf_txt.encode("ascii"))

def gen_domain_lattice_lpf(out_dir: Path):
    lpf_txt = """# ULX3S ECP5-85F CABGA381 Logical Preferences
BLOCK RESETPATHS ;
BLOCK ASYNCPATHS ;
SYSCONFIG CONFIG_IOVOLTAGE=3.3 COMPRESS_CONFIG=ON ;

LOCATE COMP "clk_25mhz" SITE "G2" ;
IOBUF PORT "clk_25mhz" IO_TYPE=LVCMOS33 ;
FREQUENCY PORT "clk_25mhz" 25.0 MHz ;

LOCATE COMP "ftdi_rxd" SITE "L4" ;
IOBUF PORT "ftdi_rxd" IO_TYPE=LVCMOS33 ;
LOCATE COMP "ftdi_txd" SITE "M1" ;
IOBUF PORT "ftdi_txd" IO_TYPE=LVCMOS33 PULLMODE=UP ;
LOCATE COMP "sdram_clk" SITE "F19" ;
IOBUF PORT "sdram_clk" IO_TYPE=LVCMOS33 DRIVE=8 ;
LOCATE COMP "gpdi_dp[0]" SITE "A16" ;
IOBUF PORT "gpdi_dp[0]" IO_TYPE=LVCMOS33D ;
"""
    (out_dir / "ulx3s_ecp5.lpf").write_bytes(lpf_txt.encode("ascii"))

def gen_domain_xilinx_ucf(out_dir: Path):
    ucf_txt = """# Digilent Atlys Spartan-6 XC6SLX45 CSG324 User Constraints
CONFIG VCCAUX = "3.3" ;

NET "clk_100mhz" LOC = "L15" | IOSTANDARD = LVCMOS33 | TNM_NET = "sys_clk_pin" ;
TIMESPEC "TS_sys_clk_pin" = PERIOD "sys_clk_pin" 10.0 ns HIGH 50% ;

NET "rst_n"    LOC = "T15" | IOSTANDARD = LVCMOS33 | PULLUP ;
NET "uart_rxd" LOC = "A16" | IOSTANDARD = LVCMOS33 ;
NET "uart_txd" LOC = "B16" | IOSTANDARD = LVCMOS33 | SLEW = FAST ;
NET "led<0>"   LOC = "U18" | IOSTANDARD = LVCMOS33 ;
NET "led<1>"   LOC = "M14" | IOSTANDARD = LVCMOS33 ;
"""
    (out_dir / "spartan6_atlys.ucf").write_bytes(ucf_txt.encode("ascii"))

def gen_domain_xschem_xsch(out_dir: Path):
    xsch_txt = """v {xschem version=3.4.4 file_version=1.2}
G {}
K {}
V {}
S {}
E {}
T {SKY130 CMOS Inverter} 40 -140 0 0 0.4 0.4 {}
N 120 -80 120 -40 {lab=VDD}
N 120 20 120 60 {lab=VOUT}
N 120 120 120 160 {lab=VSS}
N 40 40 80 40 {lab=VIN}
C {sky130_fd_pr/pfet_01v8.sym} 100 -10 0 0 {name=M1 W=2.0 L=0.15 model=pfet_01v8}
C {sky130_fd_pr/nfet_01v8.sym} 100 90 0 0 {name=M2 W=1.0 L=0.15 model=nfet_01v8}
C {devices/capa.sym} 200 90 0 0 {name=C1 value=50f}
C {devices/lab_pin.sym} 40 40 0 0 {name=p1 lab=VIN}
C {devices/lab_pin.sym} 120 40 0 1 {name=p2 lab=VOUT}
"""
    (out_dir / "sky130_cmos_inv.xsch").write_bytes(xsch_txt.encode("ascii"))

def gen_domain_fidocadj_fcd(out_dir: Path):
    fcd_txt = """[FIDOCAD]
FJC C 1.0
RV 20 20 180 120 3
PA 40 70 10 10 4 1 2
PA 80 70 10 10 4 0 2
PA 120 70 10 10 4 0 2
PA 160 70 10 10 4 0 2
PL 40 70 80 70 4 2
PL 80 70 120 70 4 1
PL 120 70 160 70 4 2
MC 100 45 0 0 ihram.res
FCJ
TY 100 35 4 3 0 0 3 SansSerif R1
TY 100 52 4 3 0 0 3 SansSerif 4k7
SA 80 70
"""
    (out_dir / "audio_preamp_pcb.fcd").write_bytes(fcd_txt.encode("ascii"))

def gen_domain_kicad_wks(out_dir: Path):
    wks_txt = """(kicad_wks
  (version 20231120)
  (generator "pl_editor")
  (setup (textsize 1.5 1.5) (linewidth 0.18) (textlinewidth 0.15)
    (left_margin 10) (right_margin 10) (top_margin 10) (bottom_margin 10))
  (rect (name "border_outer") (start 0 0 ltcorner) (end 0 0 rbcorner) (linewidth 0.25))
  (rect (name "title_block")  (start 110 32 rbcorner) (end 0 0 rbcorner) (linewidth 0.22))
  (line (name "tb_hdiv")      (start 110 16 rbcorner) (end 0 16 rbcorner) (repeat 2) (incry -8))
  (line (name "zone_top")     (start 50 0 ltcorner) (end 50 -3 ltcorner) (repeat 4) (incrx 50))
  (tbtext "1" (name "zone_num") (pos 25 -1.5 ltcorner) (repeat 4) (incrx 50))
  (tbtext "COMPANY: CERN OHL HARDWARE" (name "company") (pos 106 24 rbcorner) (font (size 2.0 2.0)))
  (tbtext "TITLE: ${TITLE}  REV: ${REVISION}" (name "title") (pos 106 10 rbcorner))
  (polygon (name "logo_tri") (pos 14 24 rbcorner) (pts (xy 0 0) (xy 8 0) (xy 4 -6)))
)
"""
    (out_dir / "cern_ohl_a4.kicad_wks").write_bytes(wks_txt.encode("ascii"))

def gen_domain_kicad_dru(out_dir: Path):
    dru_txt = """(version 1)

# High-speed DDR4 90-ohm differential impedance & 400V HV creepage rules
(rule "DDR4_DiffPair_90R"
  (layer "F.Cu")
  (constraint track_width (min 0.11mm) (opt 0.125mm) (max 0.16mm))
  (constraint diff_pair_gap (min 0.14mm) (opt 0.15mm) (max 0.18mm))
  (constraint skew (max 1.25mm))
  (condition "A.NetClass == 'DDR4_DIFF'"))

(rule "HV_Primary_Creepage"
  (severity error)
  (constraint clearance (min 2.5mm))
  (constraint track_width (min 0.8mm) (opt 1.2mm))
  (condition "A.HasNetclass('HV_400V')"))

(rule "HDI_Laser_MicroVia"
  (layer "In1.Cu")
  (constraint via_diameter (min 0.25mm) (opt 0.30mm))
  (constraint via_drill (min 0.10mm) (opt 0.12mm))
  (condition "A.Type == 'Via'"))
"""
    (out_dir / "ddr4_hv_stackup.kicad_dru").write_bytes(dru_txt.encode("ascii"))

def gen_domain_gencad(out_dir: Path):
    gc_txt = """$HEADER
GENCAD 1.4
USER "KiCad Pcbnew 8.0"
DRAWING "CAN_FD_TRANSCEIVER_REV_B"
UNITS MM
ORIGIN 0 0
$ENDHEADER
$BOARD
LINE 0 0 50 0
LINE 50 0 50 30
LINE 50 30 0 30
LINE 0 30 0 0
$ENDBOARD
$PADS
PAD P_SOIC 1 0
RECTANGLE -0.8 -0.3 1.6 0.6
PAD P_SMD0603 RECTANGLE 0
RECTANGLE -0.45 -0.45 0.9 0.9
PAD V_STD ROUND 0.3
CIRCLE 0 0 0.35
$ENDPADS
$SHAPES
SHAPE SOIC8
RECTANGLE -2.0 -2.5 4.0 5.0
PIN 1 P_SOIC -2.7  1.905 TOP 0 0
PIN 2 P_SOIC -2.7  0.635 TOP 0 0
PIN 3 P_SOIC -2.7 -0.635 TOP 0 0
PIN 4 P_SOIC -2.7 -1.905 TOP 0 0
PIN 5 P_SOIC  2.7 -1.905 TOP 0 0
PIN 6 P_SOIC  2.7 -0.635 TOP 0 0
PIN 7 P_SOIC  2.7  0.635 TOP 0 0
PIN 8 P_SOIC  2.7  1.905 TOP 0 0
SHAPE R0603
PIN 1 P_SMD0603 -0.8 0 TOP 0 0
PIN 2 P_SMD0603  0.8 0 TOP 0 0
$ENDSHAPES
$DEVICES
DEVICE "DEV_TCAN1042"
PART "TCAN1042HGV"
TYPE IC
DEVICE "DEV_R120"
VALUE "120R"
TYPE RES
$ENDDEVICES
$COMPONENTS
COMPONENT U1
DEVICE "DEV_TCAN1042"
PLACE 20.0 15.0
LAYER TOP
ROTATION 0
SHAPE SOIC8 0 0
COMPONENT R1
DEVICE "DEV_R120"
PLACE 35.0 15.0
LAYER BOTTOM
ROTATION 90
SHAPE R0603 MIRRORX 0
$ENDCOMPONENTS
$SIGNALS
SIGNAL CAN_H
NODE U1 7
NODE R1 1
SIGNAL CAN_L
NODE U1 6
NODE R1 2
SIGNAL VCC_5V
NODE U1 3
$ENDSIGNALS
$TRACKS
TRACK T_DIFF 0.25
TRACK T_PWR  0.50
$ENDTRACKS
$ROUTES
ROUTE CAN_H
TRACK T_DIFF
LAYER TOP
WIRE 22.7 15.635 30.0 15.8
VIA V_STD 30.0 15.8 ALL 0.3
LAYER BOTTOM
WIRE 30.0 15.8 35.0 15.8
ROUTE CAN_L
TRACK T_DIFF
LAYER TOP
WIRE 22.7 14.365 30.0 14.2
VIA V_STD 30.0 14.2 ALL 0.3
LAYER BOTTOM
WIRE 30.0 14.2 35.0 14.2
$ENDROUTES
"""
    (out_dir / "can_fd_transceiver.gencad").write_bytes(gc_txt.encode("ascii"))

def gen_domain_verilog_evcd(out_dir: Path):
    evcd_txt = """$date
  Nov 22, 2026
$end
$version
  Icarus Verilog $dumpports EVCD Exporter
$end
$timescale 1ns $end
$scope module i2c_slave_dut $end
$var port 1 <0 SCL $end
$var port 1 <1 SDA $end
$var port [7:0] <2 RX_BYTE $end
$upscope $end
$enddefinitions $end
$dumpports
pD 6 0 <0
pU 0 5 <1
pL 6 0 <2
$end
#10
pU 0 6 <0
pD 6 0 <1
#20
pD 6 0 <0
#30
pU 0 6 <0
pHLHLHLHL 6 6 <2
#40
pD 6 0 <0
pL 6 0 <1
#50
pU 0 6 <0
pZ 0 0 <1
pHHHHHHHH 0 6 <2
$vcdclose #60 $end
"""
    (out_dir / "i2c_bidirectional.evcd").write_bytes(evcd_txt.encode("ascii"))

def gen_domain_asam_a2l(out_dir: Path):
    a2l_txt = """/* TriCore TC387 Traction Inverter ECU XCP Calibration Database */
ASAP2_VERSION 1 71
/begin PROJECT EV_TRACTION_INV "800V SiC Traction Inverter Control"
  /begin HEADER "Release 2026.11" VERSION "V4.2" /end HEADER
  /begin MODULE TC387_FOC "Field Oriented Control Core0"
    /begin MOD_PAR "Aurora Calibration Flash + DSPR0"
      /begin MEMORY_SEGMENT CAL_FLASH "Calibration Flash" DATA FLASH INTERN 0x80180000 0x00010000 -1 -1 -1 -1 -1 /end MEMORY_SEGMENT
      /begin MEMORY_SEGMENT DSPR0_RAM "Core0 Fast RAM" VARIABLES RAM INTERN 0x70000000 0x0003C000 -1 -1 -1 -1 -1 /end MEMORY_SEGMENT
    /end MOD_PAR

    /begin COMPU_METHOD CM_RPM "Speed RPM" LINEAR "%6.1" "rpm"
      COEFFS_LINEAR 0.25 0.0
    /end COMPU_METHOD
    /begin COMPU_METHOD CM_NM "Torque Nm" LINEAR "%6.2" "Nm"
      COEFFS_LINEAR 0.1 -500.0
    /end COMPU_METHOD

    /begin AXIS_PTS AX_RPM_BP "Rotor speed breakpoints"
      0x80180100 N_RotorSpeed REC_U16 0.0 CM_RPM 16 0.0 18000.0
    /end AXIS_PTS

    /begin CHARACTERISTIC K_DeadTimeNs "SiC Gate Deadtime"
      VALUE 0x80180010 Scalar_U16 0.0 NO_COMPU_METHOD 150.0 1200.0
    /end CHARACTERISTIC

    /begin CHARACTERISTIC M_IqMaxMap "Peak Q-Axis Torque Map"
      MAP 0x80180400 Map_I16 0.0 CM_NM -450.0 450.0
      /begin AXIS_DESCR COM_AXIS N_RotorSpeed CM_RPM 16 0.0 18000.0
        AXIS_PTS_REF AX_RPM_BP
      /end AXIS_DESCR
    /end CHARACTERISTIC

    /begin MEASUREMENT N_RotorSpeed "Measured Resolver Speed"
      UWORD CM_RPM 1 0.0 0.0 18000.0
      ECU_ADDRESS 0x70001020
    /end MEASUREMENT

    /begin MEASUREMENT Tq_EstAirgap "Estimated Electromagnetic Torque"
      FLOAT32_IEEE CM_NM 1 0.0 -450.0 450.0
      ECU_ADDRESS 0x70001024
    /end MEASUREMENT
  /end MODULE
/end PROJECT
"""
    (out_dir / "ev_traction_inverter.a2l").write_bytes(a2l_txt.encode("ascii"))

def gen_domain_berkeley_kiss2(out_dir: Path):
    kiss_txt = """# AXI4-Lite Read Channel Handshake Controller FSM (UC Berkeley KISS2)
.i 2
.o 2
.p 7
.s 4
.r IDLE
0- IDLE  IDLE  00
1- IDLE  ARACK 10
-0 ARACK RWAIT 01
-1 ARACK RRESP 01
-0 RWAIT RWAIT 01
-1 RWAIT RRESP 01
-- RRESP IDLE  00
.e
"""
    (out_dir / "axi_lite_ctrl.kiss2").write_bytes(kiss_txt.encode("ascii"))

def gen_domain_pdc(out_dir: _DomainSampleRouter) -> None:
    txt = """# Microchip PolarFire SoC MPFS095T I/O Constraints
set_iobank -bank_name Bank0 -vcci 1.80 -fixed true
set_io -port_name REF_CLK_50M \\
    -pin_name E2 \\
    -io_std LVCMOS18 \\
    -DIRECTION INPUT
set_io -port_name UART_TX -pin_name F4 -io_std LVCMOS18 -DIRECTION OUTPUT
set_io -port_name UART_RX -pin_name G4 -io_std LVCMOS18 -RES_PULL Up -DIRECTION INPUT
set_io -port_name SPI_SCK -pin_name H6 -io_std LVCMOS18 -DIRECTION OUTPUT
"""
    (out_dir / "polarfire_soc_io.pdc").write_bytes(txt.encode("utf-8"))

def gen_domain_cst(out_dir: _DomainSampleRouter) -> None:
    txt = """// Tang Nano 9K GW1NR-LV9QN88PC6/I5 Pinout
IO_LOC "sys_clk" 52;
IO_PORT "sys_clk" IO_TYPE=LVCMOS33 PULL_MODE=UP;
IO_LOC "sys_rst_n" 4;
IO_PORT "sys_rst_n" IO_TYPE=LVCMOS18 PULL_MODE=UP;
IO_LOC "led[0]" 10;
IO_PORT "led[0]" IO_TYPE=LVCMOS18 DRIVE=8;
IO_LOC "led[1]" 11;
IO_PORT "led[1]" IO_TYPE=LVCMOS18 DRIVE=8;
"""
    (out_dir / "tang_nano_9k.cst").write_bytes(txt.encode("utf-8"))

def gen_domain_jam(out_dir: _DomainSampleRouter) -> None:
    txt = """NOTE "CREATOR" "Quartus Prime JAM Composer";
NOTE "DEVICE" "10M08SAE144";
NOTE "ALG_VERSION" "2.0";

ACTION PROGRAM = CHECK_IDCODE, ERASE_CFM, PROG_CFM;
ACTION VERIFY = CHECK_IDCODE, VERIFY_CFM;

PROCEDURE CHECK_IDCODE;
    BOOLEAN id_buf[32];
    STATE RESET;
    IRSCAN 10, $206;
    DRSCAN 32, $00000000, CAPTURE id_buf[31..0];
    WAIT 100 USEC;
ENDPROC;

PROCEDURE ERASE_CFM;
    IRSCAN 10, $2F2;
    DRSCAN 16, $FFFF;
    WAIT 350000 USEC;
ENDPROC;

PROCEDURE PROG_CFM;
    IRSCAN 10, $2F4;
    DRSCAN 128, $A5A5A5A55A5A5A5AC3C3C3C33C3C3C3C;
ENDPROC;

PROCEDURE VERIFY_CFM;
    IRSCAN 10, $2F6;
    DRSCAN 64, $0123456789ABCDEF;
ENDPROC;

CRC 4B9E;
"""
    (out_dir / "max10_jtag_prog.jam").write_bytes(txt.encode("utf-8"))

def gen_domain_lht(out_dir: _DomainSampleRouter) -> None:
    txt = """li:pcb-rnd-board-v8 {
    ha:data {
        li:objects {
            ha:subc.1 {
                x = 12.5mm; y = 8.0mm;
                ha:attributes { refdes = U1; }
                ha:data {
                    li:objects {
                        ha:padstack_ref.1 { x = -1.27mm; y = 0.0mm; ha:attributes { term = 1; } }
                        ha:padstack_ref.2 { x =  1.27mm; y = 0.0mm; ha:attributes { term = 2; } }
                    }
                }
            }
        }
        li:layers {
            ha:top-copper {
                purpose = copper;
                li:objects {
                    ha:line.1 { x1 = 4.0mm; y1 = 8.0mm; x2 = 11.23mm; y2 = 8.0mm; thickness = 0.35mm; }
                    ha:line.2 { x1 = 13.77mm; y1 = 8.0mm; x2 = 22.0mm; y2 = 8.0mm; thickness = 0.35mm; }
                }
            }
            ha:outline {
                purpose = uroute;
                li:objects {
                    ha:line.10 { x1 = 0.0mm; y1 = 0.0mm; x2 = 26.0mm; y2 = 0.0mm; thickness = 0.2mm; }
                    ha:line.11 { x1 = 26.0mm; y1 = 0.0mm; x2 = 26.0mm; y2 = 16.0mm; thickness = 0.2mm; }
                    ha:line.12 { x1 = 26.0mm; y1 = 16.0mm; x2 = 0.0mm; y2 = 16.0mm; thickness = 0.2mm; }
                    ha:line.13 { x1 = 0.0mm; y1 = 16.0mm; x2 = 0.0mm; y1_dummy = 0; y2 = 0.0mm; thickness = 0.2mm; }
                }
            }
        }
    }
}
"""
    (out_dir / "sensor_breakout.lht").write_bytes(txt.encode("utf-8"))

def gen_domain_xnc(out_dir: _DomainSampleRouter) -> None:
    txt = """M48
METRIC
T01C0.800
T02C2.000
%
T01
G05
X10.000Y10.000
X12.540Y10.000
X15.080Y10.000
T02
G00X0.000Y0.000
M15
G01X40.000Y0.000
G01X40.000Y25.000
G01X0.000Y25.000
G01X0.000Y0.000
M16
M30
"""
    (out_dir / "panel_rout_drill.xnc").write_bytes(txt.encode("utf-8"))

def gen_domain_cdl(out_dir: _DomainSampleRouter) -> None:
    txt = """*.BIPOLAR
*.RESI = 2000
*.SCALE METER
.GLOBAL VDD VSS

.SUBCKT NAND2_X1 A B Y VDD VSS
MP0 Y A VDD VDD pmos_svt W=420n L=45n
MP1 Y B VDD VDD pmos_svt W=420n L=45n
MN0 Y A net_mid VSS nmos_svt W=280n L=45n
+ M=1
MN1 net_mid B VSS VSS nmos_svt W=280n L=45n
.ENDS NAND2_X1
"""
    (out_dir / "cmos_nand2_lvs.cdl").write_bytes(txt.encode("utf-8"))

def gen_domain_itf(out_dir: _DomainSampleRouter) -> None:
    txt = """$$ 7nm FinFET BEOL RC Extraction Stackup
TECHNOLOGY = finfet_7nm_1p9m

DIELECTRIC FOX { THICKNESS = 0.25 ER = 3.9 }
CONDUCTOR M1   { THICKNESS = 0.036 WMIN = 0.020 SMIN = 0.020 RPSQ = 1.45 }
DIELECTRIC ILD1 { THICKNESS = 0.045 ER = 2.7 }
CONDUCTOR M2   { THICKNESS = 0.042 WMIN = 0.024 SMIN = 0.024 RPSQ = 1.15 }
DIELECTRIC ILD2 { THICKNESS = 0.090 ER = 2.9 }
CONDUCTOR M3   { THICKNESS = 0.085 WMIN = 0.040 SMIN = 0.040 RPSQ = 0.52 }

VIA V1 { FROM = M1 TO = M2 RPV = 8.5 }
VIA V2 { FROM = M2 TO = M3 RPV = 4.2 }
"""
    (out_dir / "finfet_7nm_rc.itf").write_bytes(txt.encode("utf-8"))

def gen_domain_saif(out_dir: _DomainSampleRouter) -> None:
    txt = """(SAIFILE
  (SAIFVERSION "2.0")
  (DIRECTION "backward")
  (DESIGN "riscv_alu_core")
  (TIMESCALE 1 ps)
  (DURATION 10000)
  (INSTANCE u_alu
    (NET
      (clk_i     (T0 5000) (T1 5000) (TX 0) (TC 2000))
      (carry_q   (T0 7400) (T1 2600) (TX 0) (TC 640))
      (zero_flag (T0 9100) (T1  900) (TX 0) (TC 110))
    )
  )
)
"""
    (out_dir / "alu_power_toggle.saif").write_bytes(txt.encode("utf-8"))

def gen_domain_ebd(out_dir: _DomainSampleRouter) -> None:
    txt = """| JEDEC DDR4 SO-DIMM Rank-0 Signal Integrity Topology
[Begin Board Description] 1.0
[Manufacturer] DeepCircuit Labs
[Board Name] DDR4_260PIN_SODIMM
[Number Of Pins] 260
[Reference Designator Map]
U1 mt40a1g8_ddr4.ibs
U2 mt40a1g8_ddr4.ibs

[Path Description] DQ0_FLYBY
Pin J1.A11
Len = 0.012  L = 3.2n  R = 0.08  C = 1.1p /
Fork
  Len = 0.002  L = 0.6n  R = 0.02  C = 0.3p /
  Node U1.G2
Endfork
Len = 0.015  L = 3.8n  R = 0.10  C = 1.3p /
Node U2.G2

[Path Description] DQS0_P
Pin J1.A13
Len = 0.014  L = 3.5n  R = 0.09  C = 1.2p /
Node U1.F7

[End Board Description]
"""
    (out_dir / "ddr4_sodimm_rank.ebd").write_bytes(txt.encode("utf-8"))

def gen_domain_odx(out_dir: _DomainSampleRouter) -> None:
    txt = """<?xml version="1.0" encoding="UTF-8"?>
<ODX MODEL-VERSION="2.2.0">
  <DIAG-LAYER-CONTAINER ID="DLC.PT">
    <BASE-VARIANTS>
      <BASE-VARIANT ID="BV.VCU_INV">
        <SHORT-NAME>VCU_TractionInverter</SHORT-NAME>
        <DIAG-DATA-DICTIONARY-SPEC>
          <DATA-OBJECT-PROPS>
            <DATA-OBJECT-PROP ID="DOP.HV_VOLT"><SHORT-NAME>HV_DcBusVoltage_V</SHORT-NAME></DATA-OBJECT-PROP>
            <DATA-OBJECT-PROP ID="DOP.STATOR_TEMP"><SHORT-NAME>StatorWindingTemp_C</SHORT-NAME></DATA-OBJECT-PROP>
          </DATA-OBJECT-PROPS>
        </DIAG-DATA-DICTIONARY-SPEC>
        <DIAG-COMMS>
          <DIAG-SERVICE ID="SVC.RDBI_VIN" SEMANTIC="IDENTIFICATION">
            <SHORT-NAME>ReadDataByIdentifier_0xF190_VIN</SHORT-NAME>
          </DIAG-SERVICE>
          <DIAG-SERVICE ID="SVC.RDBI_HV" SEMANTIC="DATA-READ">
            <SHORT-NAME>ReadDataByIdentifier_0x2201_HVTelemetry</SHORT-NAME>
          </DIAG-SERVICE>
          <DIAG-SERVICE ID="SVC.CLR_DTC" SEMANTIC="CLEAR-FAULT">
            <SHORT-NAME>ClearDiagnosticInformation_0x14</SHORT-NAME>
          </DIAG-SERVICE>
        </DIAG-COMMS>
      </BASE-VARIANT>
    </BASE-VARIANTS>
  </DIAG-LAYER-CONTAINER>
</ODX>
"""
    (out_dir / "uds_powertrain_ecu.odx").write_bytes(txt.encode("utf-8"))

def gen_domain_sym(out_dir: _DomainSampleRouter) -> None:
    txt = """FormatVersion=6.0 // Do not edit this line!
Title="BMS_CANFD_Telemetry"

{SENDRECEIVE}

[BMS_PackStatus]
ID=18FF50E5h
Type=CANFD
Len=8
CycleTime=20
Var=PackVoltage unsigned 0,16 -u:V -f:0.1
Var=PackCurrent signed 16,16 -u:A -f:0.1
Var=StateOfCharge unsigned 32,8 -u:% -f:0.5
Var=ContactorClosed bit 40,1

[BMS_CellLimits]
ID=18FF51E5h
Len=8
Var=MaxCellMv unsigned 0,16 -u:mV
Var=MinCellMv unsigned 16,16 -u:mV
"""
    (out_dir / "bms_canfd_bus.sym").write_bytes(txt.encode("utf-8"))

def gen_domain_ols(out_dir: _DomainSampleRouter) -> None:
    txt = """;Rate: 100000000
;Channels: 8
;EnabledChannels: 15
;Size: 8
00@0
01@10
03@20
07@30
05@45
01@60
09@75
00@90
"""
    (out_dir / "spi_flash_capture.ols").write_bytes(txt.encode("utf-8"))


def _run_consolidated_domain_samples(td_root: Path) -> None:

    router = _DomainSampleRouter(td_root, {'msp430_bootloader.titxt': 'eda', 'vivado_fir_lut.coe': 'eda', 'cyclone_sine_rom.mif': 'eda', 'mentor_rf_board.pads': 'eda', 'protel99_psu.protel': 'eda', 'zuken_motor_ctrl.cpa': 'eda', 'sky130_nand2_cell.mag': 'eda', 'alu_decoder.pla': 'eda', 'iscas85_c17_adder.bench': 'eda', 'ev_powertrain_bus.kcd': 'eda', 'obd2_drive_cycle.candump': 'waveforms', 'keysight_bandpass.cti': 'waveforms', 'substation_fault_110kv.cff': 'waveforms', 'riscv_pipeline.lxt2': 'waveforms', 'asic_clock_tree.spef': 'eda', 'soc_power_islands.upf': 'eda', 'z80_monitor_rom.tek': 'eda', 'riscv_bootrom.mem': 'eda', 'microblaze_lmb.bmm': 'eda', 'icebreaker_up5k.pcf': 'eda', 'ulx3s_ecp5.lpf': 'eda', 'spartan6_atlys.ucf': 'eda', 'sky130_cmos_inv.xsch': 'eda', 'audio_preamp_pcb.fcd': 'eda', 'cern_ohl_a4.kicad_wks': 'eda', 'ddr4_hv_stackup.kicad_dru': 'eda', 'can_fd_transceiver.gencad': 'eda', 'i2c_bidirectional.evcd': 'waveforms', 'ev_traction_inverter.a2l': 'eda', 'axi_lite_ctrl.kiss2': 'eda', 'polarfire_soc_io.pdc': 'eda', 'tang_nano_9k.cst': 'eda', 'max10_jtag_prog.jam': 'eda', 'sensor_breakout.lht': 'eda', 'panel_rout_drill.xnc': 'eda', 'cmos_nand2_lvs.cdl': 'eda', 'finfet_7nm_rc.itf': 'eda', 'alu_power_toggle.saif': 'waveforms', 'ddr4_sodimm_rank.ebd': 'eda', 'uds_powertrain_ecu.odx': 'eda', 'bms_canfd_bus.sym': 'eda', 'spi_flash_capture.ols': 'waveforms'})

    for fn in [gen_domain_ti_txt, gen_domain_xilinx_coe, gen_domain_quartus_mif, gen_domain_mentor_pads, gen_domain_protel_pcb, gen_domain_cadstar_cpa, gen_domain_magic_mag, gen_domain_espresso_pla, gen_domain_iscas_bench, gen_domain_kayak_kcd, gen_domain_socketcan_candump, gen_domain_agilent_cti, gen_domain_comtrade_cff, gen_domain_gtkwave_lxt2, gen_domain_ieee_spef, gen_domain_ieee_upf, gen_domain_tektronix_tek, gen_domain_verilog_mem, gen_domain_xilinx_bmm, gen_domain_icestorm_pcf, gen_domain_lattice_lpf, gen_domain_xilinx_ucf, gen_domain_xschem_xsch, gen_domain_fidocadj_fcd, gen_domain_kicad_wks, gen_domain_kicad_dru, gen_domain_gencad, gen_domain_verilog_evcd, gen_domain_asam_a2l, gen_domain_berkeley_kiss2, gen_domain_pdc, gen_domain_cst, gen_domain_jam, gen_domain_lht, gen_domain_xnc, gen_domain_cdl, gen_domain_itf, gen_domain_saif, gen_domain_ebd, gen_domain_odx, gen_domain_sym, gen_domain_ols]:

        fn(router)


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description="Generate EDA & Waveform verification samples.")
    ap.add_argument(
        "--out-root",
        type=pathlib.Path,
        default=repo_root / "testdata",
        help="Target testdata root containing eda/ and waveforms/ subdirectories.",
    )
    args = ap.parse_args()

    files = generate_all(args.out_root.resolve())
    print(f"Generated {len(files)} sample files under {args.out_root.resolve()}:")
    for p in files:
        print(f"  {p.relative_to(args.out_root.resolve()).as_posix()} ({p.stat().st_size} bytes)")
    return 0
    if len(__import__('sys').argv) == 1:
        _run_consolidated_domain_samples(Path(__file__).resolve().parent.parent / 'testdata')


if __name__ == "__main__":
    raise SystemExit(main())
