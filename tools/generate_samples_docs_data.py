#!/usr/bin/env python3
"""Generates deterministic, spec-accurate sample files for new & upgraded
Docs, Data, GIS, Audio, and Security formats under testdata/{data,gis,audio,security}/.

Every sample is synthesized directly from official binary/text specifications
with zero third-party copyrighted content and reproduces byte-identically.

Usage:
    python tools/generate_samples_docs_data.py
    python tools/generate_samples_docs_data.py <repo_root>
"""

import gzip
import hashlib
import json
import math
import struct
import sys
import zlib
from pathlib import Path


# ==============================================================================
# Shared Low-Level Binary Builders (Protobuf, LZ4 raw block, Ion, OLE2 CFB)
# ==============================================================================
def pb_varint(v: int) -> bytes:
    v &= (1 << 64) - 1
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def pb_zigzag(n: int) -> int:
    return (n << 1) ^ (n >> 63)


def pb_field_varint(fn: int, v: int) -> bytes:
    return pb_varint((fn << 3) | 0) + pb_varint(v)


def pb_field_bytes(fn: int, payload: bytes) -> bytes:
    return pb_varint((fn << 3) | 2) + pb_varint(len(payload)) + payload


def pb_field_f64(fn: int, val: float) -> bytes:
    return pb_varint((fn << 3) | 1) + struct.pack("<d", val)


def lz4_encode_literals_only(raw: bytes) -> bytes:
    """Encodes a 100% valid independent LZ4 raw block using a literal-only sequence."""
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
    out += raw
    return bytes(out)


def ion_varuint(v: int) -> bytes:
    chunks = [v & 0x7F]
    v >>= 7
    while v > 0:
        chunks.append(v & 0x7F)
        v >>= 7
    chunks.reverse()
    chunks[-1] |= 0x80  # Stop bit on final byte
    return bytes(chunks)


def ion_varint(v: int) -> bytes:
    neg = v < 0
    mag = -v if neg else v
    if mag < 0x40:
        return bytes([0x80 | (0x40 if neg else 0) | mag])
    low_bytes = []
    while mag >= 0x40:
        low_bytes.append(mag & 0x7F)
        mag >>= 7
    first = (0x40 if neg else 0) | (mag & 0x3F)
    low_bytes.reverse()
    low_bytes[-1] |= 0x80
    return bytes([first] + low_bytes)


def ion_tlv(tc: int, payload: bytes) -> bytes:
    ln = len(payload)
    if ln < 14:
        return bytes([(tc << 4) | ln]) + payload
    return bytes([(tc << 4) | 14]) + ion_varuint(ln) + payload


def ion_uint_bytes(v: int) -> bytes:
    if v == 0:
        return b""
    out = bytearray()
    while v > 0:
        out.append(v & 0xFF)
        v >>= 8
    out.reverse()
    return bytes(out)


def ion_symbol(sid: int) -> bytes:
    return ion_tlv(7, ion_uint_bytes(sid))


def ion_string(s: str) -> bytes:
    return ion_tlv(8, s.encode("utf-8"))


def ion_struct(fields) -> bytes:
    body = bytearray()
    for fsid, val_bytes in fields:
        body += ion_varuint(fsid) + val_bytes
    return ion_tlv(13, bytes(body))


def ion_list(items) -> bytes:
    return ion_tlv(11, b"".join(items))


def ion_annot(annot_sids, inner: bytes) -> bytes:
    ab = b"".join(ion_varuint(s) for s in annot_sids)
    return ion_tlv(14, ion_varuint(len(ab)) + ab + inner)


def build_ole2_cfb(streams):
    """Builds a clean v3 (512-byte sector) OLE2 Compound File Binary container
    with standard sector-allocated streams (>= 4096 cutoff bypassed by setting miniCutoff=0
    or padding streams cleanly via regular FAT chains)."""
    sector_size = 512
    # Directory entries: 0 = Root Entry, 1..N = Streams balanced in a simple right-leaning RB chain
    n_entries = 1 + len(streams)
    dir_sectors = (n_entries * 128 + sector_size - 1) // sector_size

    # Layout sectors:
    # Sector 0..F-1: FAT sectors (1 sector holds 128 sector IDs = 64 KB, plenty for JumpList)
    # Sector 1..1+dir_sectors-1: Directory sectors
    # Remaining sectors: stream payloads
    fat = [0xFFFFFFFF] * 128
    fat[0] = 0xFFFFFFFD  # FATSECT
    cur_sec = 1
    first_dir_sec = cur_sec
    for d in range(dir_sectors):
        fat[cur_sec] = (cur_sec + 1) if (d + 1 < dir_sectors) else 0xFFFFFFFE
        cur_sec += 1

    stream_meta = []
    stream_blob = bytearray()
    for name, data in streams:
        if len(data) == 0:
            stream_meta.append((name, 0xFFFFFFFE, 0))
            continue
        start_s = cur_sec
        n_sec = (len(data) + sector_size - 1) // sector_size
        for s in range(n_sec):
            fat[cur_sec] = (cur_sec + 1) if (s + 1 < n_sec) else 0xFFFFFFFE
            cur_sec += 1
        stream_meta.append((name, start_s, len(data)))
        stream_blob += data + b"\x00" * (n_sec * sector_size - len(data))

    # Build Directory bytes
    dir_buf = bytearray(dir_sectors * sector_size)

    def write_dir_entry(idx: int, name: str, obj_type: int, left_id: int, right_id: int, child_id: int, start_sec: int, size: int):
        off = idx * 128
        u16 = (name + "\x00").encode("utf-16le")[:64]
        dir_buf[off : off + len(u16)] = u16
        struct.pack_into("<HBBIII", dir_buf, off + 64, len(u16), obj_type, 1, left_id, right_id, child_id)
        struct.pack_into("<II", dir_buf, off + 116, start_sec, size)

    write_dir_entry(0, "Root Entry", 5, 0xFFFFFFFF, 0xFFFFFFFF, 1 if stream_meta else 0xFFFFFFFF, 0xFFFFFFFE, 0)
    for i, (sname, ssec, ssz) in enumerate(stream_meta, start=1):
        right_id = (i + 1) if i < len(stream_meta) else 0xFFFFFFFF
        write_dir_entry(i, sname, 2, 0xFFFFFFFF, right_id, 0xFFFFFFFF, ssec, ssz)

    # OLE2 512-byte Header (set miniStreamCutoff = 1 so all streams >= 1B read from regular FAT!)
    hdr = bytearray(512)
    hdr[0:8] = b"\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1"
    struct.pack_into(
        "<HHHHHHIIIIIIIIII",
        hdr,
        24,
        0x003E,      # minor ver
        0x0003,      # major ver 3 (512B sectors)
        0xFFFE,      # little-endian
        9,           # sector shift (512)
        6,           # mini sector shift (64)
        0,
        0,
        0,           # num dir sectors (0 in v3)
        1,           # num FAT sectors
        first_dir_sec,
        0,
        1,           # miniStreamCutoff = 1 byte -> streams read directly from FAT
        0xFFFFFFFE,  # first MiniFAT
        0,           # num MiniFAT
        0xFFFFFFFE,  # first DIFAT
        0,           # num DIFAT
    )
    for i in range(109):
        struct.pack_into("<I", hdr, 76 + i * 4, 0 if i == 0 else 0xFFFFFFFF)

    fat_bytes = b"".join(struct.pack("<I", x) for x in fat)
    return bytes(hdr) + fat_bytes + bytes(dir_buf) + bytes(stream_blob)


# ==============================================================================
# 1. Data Format Samples (testdata/data/)
# ==============================================================================
def gen_data_samples(data_dir: Path, created: list):
    data_dir.mkdir(parents=True, exist_ok=True)

    # 1A. Amazon Ion 1.0 Binary (.10n) with local $ion_symbol_table + structs, decimals, timestamps
    bvm = b"\xE0\x01\x00\xEA"
    local_syms = ["order_id", "customer", "total_usd", "shipped", "created_at", "tags"]
    symtab = ion_annot(
        [3],  # $ion_symbol_table
        ion_struct([
            (7, ion_list([ion_string(s) for s in local_syms])),  # 7 = symbols -> IDs 10..15
        ]),
    )
    # Timestamp: 2026-04-15T14:30:00Z -> offset=+0 (0x80), Y=2026, M=4, D=15, H=14, Min=30, S=0
    ts_payload = ion_varint(0) + ion_varuint(2026) + ion_varuint(4) + ion_varuint(15) + ion_varuint(14) + ion_varuint(30) + ion_varuint(0)
    ts_tlv = ion_tlv(6, ts_payload)
    # Decimal 149.95 = 14995 * 10^-2 -> exp=-2, coeff=+14995 (0x3A93)
    dec1 = ion_tlv(5, ion_varint(-2) + b"\x3A\x93")
    dec2 = ion_tlv(5, ion_varint(-2) + b"\x09\xC3")  # 24.99
    rec1 = ion_struct([
        (10, ion_string("ORD-9001")),
        (11, ion_string("Ada Lovelace")),
        (12, dec1),
        (13, bytes([0x11])),  # bool true
        (14, ts_tlv),
        (15, ion_list([ion_symbol(10), ion_string("priority")])),
    ])
    rec2 = ion_struct([
        (10, ion_string("ORD-9002")),
        (11, ion_string("Alan Turing")),
        (12, dec2),
        (13, bytes([0x10])),  # bool false
        (14, ts_tlv),
        (15, ion_list([ion_string("standard")])),
    ])
    p_ion_bin = data_dir / "telemetry_orders.10n"
    p_ion_bin.write_bytes(bvm + symtab + rec1 + rec2)
    created.append(p_ion_bin)

    # 1B. Amazon Ion 1.0 Text (.ion)
    ion_txt = """$ion_1_0
// Service mesh catalog in Amazon Ion 1.0 text notation
service::{
  id: 'svc_gateway',
  host: "edge-eu-central-1.internal",
  port: 8443,
  latency_ms: 1.85d0,
  updated: 2026-05-10T08:15:30Z,
  healthy: true,
  protocols: [http2, grpc]
}
service::{
  id: 'svc_ledger',
  host: "ledger-core-01.internal",
  port: 9090,
  latency_ms: 0.42d0,
  updated: 2026-05-10T08:16:00Z,
  healthy: true,
  protocols: [grpc]
}
"""
    p_ion_txt = data_dir / "service_catalog.ion"
    p_ion_txt.write_bytes(ion_txt.encode("utf-8"))
    created.append(p_ion_txt)

    # 2. UBJSON Draft-12 (.ubj) — array [#2 of objects with ints, float64, high-precision H, and [$d#3] typed float32 array
    def ubj_str_raw(s: str) -> bytes:
        b = s.encode("utf-8")
        return b"U" + bytes([len(b)]) + b

    def ubj_str(s: str) -> bytes:
        return b"S" + ubj_str_raw(s)

    ubj_doc = bytearray(b"[#U\x02")
    for idx, (sensor, temp, active, samples) in enumerate([
        ("rack-north-01", 23.75, True, [23.5, 23.75, 23.8]),
        ("rack-south-02", 27.125, False, [26.9, 27.0, 27.125]),
    ], start=1):
        ubj_doc += b"{"
        ubj_doc += ubj_str_raw("id") + b"I" + struct.pack(">h", idx)
        ubj_doc += ubj_str_raw("sensor") + ubj_str(sensor)
        ubj_doc += ubj_str_raw("temp_c") + b"D" + struct.pack(">d", temp)
        ubj_doc += ubj_str_raw("online") + (b"T" if active else b"F")
        ubj_doc += ubj_str_raw("precise_v") + b"H" + ubj_str_raw("3.14159265358979323846")
        ubj_doc += ubj_str_raw("window") + b"[$d#U\x03" + b"".join(struct.pack(">f", x) for x in samples)
        ubj_doc += b"}"
    p_ubj = data_dir / "sensor_matrix.ubj"
    p_ubj.write_bytes(bytes(ubj_doc))
    created.append(p_ubj)

    # 3A & 3B. Mozilla Firefox LZ4 JSON (.jsonlz4 Bookmarks & .mozlz4 SessionStore)
    bm_json = json.dumps({
        "guid": "root________",
        "title": "",
        "id": 1,
        "children": [{
            "guid": "toolbar_____",
            "title": "Bookmarks Toolbar",
            "children": [
                {
                    "guid": "bm_docs_0001",
                    "title": "MDN Web Docs",
                    "uri": "https://developer.mozilla.org/",
                    "dateAdded": 1746000000000000,
                    "lastModified": 1746500000000000,
                    "keyword": "mdn",
                },
                {
                    "guid": "bm_spec_0002",
                    "title": "RFC 9110 HTTP Semantics",
                    "uri": "https://www.rfc-editor.org/rfc/rfc9110",
                    "dateAdded": 1746100000000000,
                    "lastModified": 1746600000000000,
                },
            ],
        }],
    }, separators=(",", ":")).encode("utf-8")
    p_jsonlz4 = data_dir / "firefox_bookmarks.jsonlz4"
    p_jsonlz4.write_bytes(b"mozLz40\x00" + struct.pack("<I", len(bm_json)) + lz4_encode_literals_only(bm_json))
    created.append(p_jsonlz4)

    sess_json = json.dumps({
        "version": ["sessionrestore", 1],
        "windows": [{
            "selected": 1,
            "tabs": [
                {
                    "index": 1,
                    "lastAccessed": 1746700000000,
                    "entries": [{"url": "https://sqlite.org/fileformat2.html", "title": "SQLite Database File Format"}],
                },
                {
                    "index": 1,
                    "lastAccessed": 1746700300000,
                    "entries": [{"url": "https://arrow.apache.org/docs/format/Columnar.html", "title": "Apache Arrow Columnar Format"}],
                },
            ],
        }],
    }, separators=(",", ":")).encode("utf-8")
    p_mozlz4 = data_dir / "firefox_session.mozlz4"
    p_mozlz4.write_bytes(b"mozLz40\x00" + struct.pack("<I", len(sess_json)) + lz4_encode_literals_only(sess_json))
    created.append(p_mozlz4)

    # 4. Quicken Interchange Format (.qif)
    qif_text = """!Account
NOperating Checking
TBank
DPrimary Engineering Account
$12500.00
^
!Type:Bank
D2026-03-01
T2500.00
CX
PClient Retainer Deposit
LConsulting:Revenue
MInvoice #2026-031
^
D2026-03-05
T-340.50
C*
N1042
PCloud Infrastructure Co
LHosting
MMonthly compute & storage split
SHosting:Compute
ECPU cluster nodes
$-240.50
SHosting:Storage
EObject store S3 backups
$-100.00
^
"""
    p_qif = data_dir / "checking_ledger.qif"
    p_qif.write_bytes(qif_text.encode("utf-8"))
    created.append(p_qif)

    # 5A. Open Financial Exchange v1 SGML (.ofx)
    ofx_v1 = """OFXHEADER:100
DATA:OFXSGML
VERSION:102
SECURITY:NONE
ENCODING:UTF-8
CHARSET:NONE
COMPRESSION:NONE
OLDFILEUID:NONE
NEWFILEUID:NONE

<OFX>
<SIGNONMSGSRSV1>
<SONRS>
<STATUS><CODE>0<SEVERITY>INFO</STATUS>
<DTSERVER>20260331120000
<LANGUAGE>ENG
<FI><ORG>Nordic Commercial Bank<FID>4401</FI>
</SONRS>
</SIGNONMSGSRSV1>
<BANKMSGSRSV1>
<STMTTRNRS>
<TRNUID>1001
<STATUS><CODE>0<SEVERITY>INFO</STATUS>
<STMTRS>
<CURDEF>EUR
<BANKACCTFROM>
<BANKID>37040044
<ACCTID>DE89370400440532013000
<ACCTTYPE>CHECKING
</BANKACCTFROM>
<BANKTRANLIST>
<DTSTART>20260301000000
<DTEND>20260331235959
<STMTTRN>
<TRNTYPE>CREDIT
<DTPOSTED>20260303093000
<TRNAMT>4800.00
<FITID>202603030001
<NAME>ACME Robotics GmbH
<MEMO>Milestone 2 Wire
</STMTTRN>
<STMTTRN>
<TRNTYPE>DEBIT
<DTPOSTED>20260312141500
<TRNAMT>-189.90
<FITID>202603120002
<CHECKNUM>5012
<NAME>Precision Lab Supplies
<MEMO>Logic analyzer probes &amp; cables
</STMTTRN>
</BANKTRANLIST>
<LEDGERBAL><BALAMT>19410.10<DTASOF>20260331235959</LEDGERBAL>
<AVAILBAL><BALAMT>19210.10<DTASOF>20260331235959</AVAILBAL>
</STMTRS>
</STMTTRNRS>
</BANKMSGSRSV1>
</OFX>
"""
    p_ofx = data_dir / "bank_statement_v1.ofx"
    p_ofx.write_bytes(ofx_v1.encode("utf-8"))
    created.append(p_ofx)

    # 5B. Open Financial Exchange v2 XML (.qfx)
    ofx_v2 = """<?xml version="1.0" encoding="UTF-8"?>
<?OFX OFXHEADER="200" VERSION="202" SECURITY="NONE" OLDFILEUID="NONE" NEWFILEUID="NONE"?>
<OFX>
  <SIGNONMSGSRSV1><SONRS><STATUS><CODE>0</CODE><SEVERITY>INFO</SEVERITY></STATUS><DTSERVER>20260401080000</DTSERVER><LANGUAGE>ENG</LANGUAGE><FI><ORG>Pacific Card Services</ORG><FID>9088</FID></FI></SONRS></SIGNONMSGSRSV1>
  <CCARDMSGSRSV1><CCSTMTTRNRS><TRNUID>2001</TRNUID><CCSTMTRS>
    <CURDEF>USD</CURDEF>
    <CCACCTFROM><ACCTID>4111-XXXX-XXXX-8842</ACCTID></CCACCTFROM>
    <BANKTRANLIST>
      <DTSTART>20260301</DTSTART><DTEND>20260331</DTEND>
      <STMTTRN><TRNTYPE>DEBIT</TRNTYPE><DTPOSTED>20260318112000</DTPOSTED><TRNAMT>-74.50</TRNAMT><FITID>CC9901</FITID><NAME>Transit Rail Pass</NAME><MEMO>Monthly Zone 1-3</MEMO></STMTTRN>
    </BANKTRANLIST>
    <LEDGERBAL><BALAMT>-74.50</BALAMT><DTASOF>20260331</DTASOF></LEDGERBAL>
  </CCSTMTRS></CCSTMTTRNRS></CCARDMSGSRSV1>
</OFX>
"""
    p_qfx = data_dir / "credit_card_v2.qfx"
    p_qfx.write_bytes(ofx_v2.encode("utf-8"))
    created.append(p_qfx)

    # Upgraded SPSS $FL3 ZSAV (.zsav)
    hdr = bytearray(176)
    hdr[0:4] = b"$FL3"
    hdr[4:60] = b"@(#) SPSS DATA FILE - Explorer2 ZSAV Sample".ljust(56, b" ")
    struct.pack_into("<iiiiid", hdr, 60, 2, 2, 2, 0, 2, 100.0)  # layout=2, nominalCaseSize=2, compressed=2 (ZSAV), ncases=2, bias=100.0
    hdr[88:97] = b"25 NOV 26"
    hdr[97:105] = b"12:00:00"
    hdr[105:169] = b"ZSAV Zlib Block Clinical Sample".ljust(64, b" ")
    # Variable 0: SCORE numeric (type 0), Variable 1: GROUP 8-byte string (type 8)
    var0 = struct.pack("<iiiiiiii", 2, 0, 0, 0, 0x050802, 0x050802, 0, 0)[:24] + b"SCORE   "
    var1 = struct.pack("<iiiiiiii", 2, 8, 0, 0, 0x010800, 0x010800, 0, 0)[:24] + b"GROUP   "
    end_rec = struct.pack("<ii", 999, 0)
    dict_bytes = bytes(hdr) + var0 + var1 + end_rec
    # Bytecode for 2 cases: Case 0 = (42 + 100 = code 142, literal 253 "CTRL    "), Case 1 = (85 + 100 = code 185, literal 253 "ACTIVE  ") + 252 EOF
    cluster = bytes([142, 253, 185, 253, 252, 0, 0, 0])
    uncomp_block = cluster + b"CTRL    " + b"ACTIVE  "
    comp_block = zlib.compress(uncomp_block)
    zhdr_ofs = len(dict_bytes)
    comp_ofs = zhdr_ofs + 24
    ztrl_ofs = comp_ofs + len(comp_block)
    ztrl = struct.pack("<qqii", -100, 0, 0x3FF000, 1) + struct.pack("<qqii", 0, comp_ofs, len(uncomp_block), len(comp_block))
    zhdr = struct.pack("<QQQ", zhdr_ofs, ztrl_ofs, len(ztrl))
    p_zsav = data_dir / "survey_compressed.zsav"
    p_zsav.write_bytes(dict_bytes + zhdr + comp_block + ztrl)
    created.append(p_zsav)

    # Upgraded SAS7BDAT with SASYZCRL (RLE) compressed rows (.sas7bdat)
    sas = bytearray(2048)
    sas[0:32] = bytes([
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0xC2,0xEA,0x81,0x60,
        0xB3,0x14,0x11,0xCF,0xBD,0x92,0x08,0x00, 0x09,0xC7,0x31,0x8C,0x18,0x1F,0x10,0x11,
    ])
    sas[32] = 0x22
    sas[35] = 0x22
    sas[37] = 0x01  # 32-bit LE
    sas[92:124] = b"CLINICAL_RLE".ljust(32, b" ")
    struct.pack_into("<III", sas, 196, 1024, 1024, 1)
    pg = memoryview(sas)[1024:2048]
    struct.pack_into("<HHH", pg, 16, 512, 7, 7)  # MIX page, 7 subheaders (5 meta + 2 RLE rows)
    cur_sh = 256

    def put_sas_sub(idx: int, payload: bytes, comp_flag: int = 0, type_flag: int = 0):
        nonlocal cur_sh
        pg[cur_sh : cur_sh + len(payload)] = payload
        ptr_off = 24 + idx * 12
        struct.pack_into("<IIBBH", pg, ptr_off, cur_sh, len(payload), comp_flag, type_flag, 0)
        cur_sh += (len(payload) + 7) & ~7

    sh_row = bytearray(64)
    struct.pack_into("<I", sh_row, 0, 0xF7F7F7F7)
    struct.pack_into("<II", sh_row, 20, 24, 2)  # rowLength=24 (8B double + 16B char), totalRows=2
    put_sas_sub(0, bytes(sh_row))
    put_sas_sub(1, struct.pack("<II", 0xF6F6F6F6, 2))
    # ColumnText containing SASYZCRL signature + column names ("MG_DOSE" at +10, "PATIENT_ARM" at +17)
    col_txt_body = struct.pack("<H", 28) + b"SASYZCRLDOSE_MGARM_NAME"
    put_sas_sub(2, struct.pack("<I", 0xFFFFFFFD) + col_txt_body)
    sh_nm = bytearray(28)
    struct.pack_into("<I", sh_nm, 0, 0xFFFFFFFF)
    struct.pack_into("<HHH", sh_nm, 12, 0, 10, 7)  # DOSE_MG
    struct.pack_into("<HHH", sh_nm, 20, 0, 17, 8)  # ARM_NAME
    put_sas_sub(3, bytes(sh_nm))
    sh_attr = bytearray(36)
    struct.pack_into("<I", sh_attr, 0, 0xFFFFFFFC)
    struct.pack_into("<IIHBB", sh_attr, 12, 0, 8, 0, 1, 0)   # col 0: Numeric 8B @ 0
    struct.pack_into("<IIHBB", sh_attr, 24, 8, 16, 0, 2, 0)  # col 1: Character 16B @ 8
    put_sas_sub(4, bytes(sh_attr))
    # RLE row encoding: 0x8F (COPY1 len=16 -> 8B double + 8B text) + 0xE6 (INSERT_BLANK2 len=8 spaces) -> 24 bytes!
    rle_row0 = bytes([0x8F]) + struct.pack("<d", 125.5) + b"TREAT_A " + bytes([0xE6])
    rle_row1 = bytes([0x8F]) + struct.pack("<d", 250.0) + b"PLACEBO " + bytes([0xE6])
    put_sas_sub(5, rle_row0, comp_flag=4, type_flag=1)
    put_sas_sub(6, rle_row1, comp_flag=4, type_flag=1)
    p_sas = data_dir / "clinical_rle.sas7bdat"
    p_sas.write_bytes(bytes(sas))
    created.append(p_sas)

    # Upgraded Stata 118 .dta with <map> 64-bit offsets + <strls> GSO records
    dta = bytearray()
    dta += b"<stata_dta><header><release>118</release><byteorder>LSF</byteorder>"
    dta += b"<K>" + struct.pack("<I", 2) + b"</K>"
    dta += b"<N>" + struct.pack("<Q", 2) + b"</N>"
    lbl_b = b"Panel With GSO strL Notes"
    dta += b"<label>" + struct.pack("<H", len(lbl_b)) + lbl_b + b"</label>"
    dta += b"<timestamp>\x00</timestamp></header>"
    off_map_tag = len(dta)
    dta += b"<map>" + b"\x00" * (14 * 8) + b"</map>"
    off_vtypes = len(dta)
    dta += b"<variable_types>" + struct.pack("<HH", 65526, 32768) + b"</variable_types>"  # double + strL
    off_vnames = len(dta)
    dta += b"<varnames>" + b"gdp_growth".ljust(129, b"\x00") + b"analyst_note".ljust(129, b"\x00") + b"</varnames>"
    off_srt = len(dta); dta += b"<sortlist>" + b"\x00" * 6 + b"</sortlist>"
    off_fmt = len(dta); dta += b"<formats>" + b"%9.2f".ljust(57, b"\x00") + b"%9s".ljust(57, b"\x00") + b"</formats>"
    off_vlb = len(dta); dta += b"<value_label_names>" + b"\x00" * (2 * 129) + b"</value_label_names>"
    off_varl = len(dta); dta += b"<varlabels>" + b"Real GDP Growth %".ljust(321, b"\x00") + b"Full Analyst Commentary".ljust(321, b"\x00") + b"</varlabels>"
    off_ch = len(dta); dta += b"<characteristics></characteristics>"
    off_data = len(dta)
    # Row 0: double 3.45 + strL ref (v=2, o=1): LE u16(2) + 6-byte LE(1)
    # Row 1: double 1.80 + strL ref (v=2, o=2): LE u16(2) + 6-byte LE(2)
    dta += b"<data>"
    dta += struct.pack("<dQ", 3.45, 2 | (1 << 16))
    dta += struct.pack("<dQ", 1.80, 2 | (2 << 16))
    dta += b"</data>"
    off_strls = len(dta)
    s1 = b"Strong industrial rebound across Q1 semiconductor exports\x00"
    s2 = b"Moderating consumer demand offset by renewable capex\x00"
    gso1 = b"GSO" + struct.pack("<IQBI", 2, 1, 0x82, len(s1)) + s1
    gso2 = b"GSO" + struct.pack("<IQBI", 2, 2, 0x82, len(s2)) + s2
    dta += b"<strls>" + gso1 + gso2 + b"</strls>"
    off_vlbls = len(dta); dta += b"<value_labels></value_labels>"
    off_end = len(dta); dta += b"</stata_dta>"
    off_eof = len(dta)
    map_vals = [0, off_map_tag, off_vtypes, off_vnames, off_srt, off_fmt, off_vlb, off_varl, off_ch, off_data, off_strls, off_vlbls, off_end, off_eof]
    dta[off_map_tag + 5 : off_map_tag + 5 + 14 * 8] = b"".join(struct.pack("<Q", x) for x in map_vals)
    p_dta = data_dir / "panel_notes.dta"
    p_dta.write_bytes(bytes(dta))
    created.append(p_dta)

    # Upgraded RocksDB PlainTable SST (.sst)
    # PlainTable records: [EncodeSize(kFullKey=0, len)][user_key][0x80 zero-seq Put][varint(val_len)][val]
    plain_data = bytearray()
    for k, v in [(b"acct:1001", b"balance=9450.25"), (b"acct:1002", b"balance=3120.00"), (b"cfg:region", b"eu-central-1")]:
        plain_data += bytes([len(k)]) + k + b"\x80" + pb_varint(len(v)) + v
    meta_off = len(plain_data)
    # Empty valid metaindex block: 1 restart at offset 0 -> [u32(0), u32(1)] + 5B trailer (ctype=0 + 4B crc)
    meta_blk = struct.pack("<II", 0, 1)
    idx_off = meta_off + len(meta_blk) + 5
    handles = pb_varint(meta_off) + pb_varint(len(meta_blk)) + pb_varint(idx_off) + pb_varint(0)
    footer = bytes([1]) + handles.ljust(40, b"\x00") + struct.pack("<IQ", 1, 0x8242229663BF9564)
    p_sst = data_dir / "plain_kv.sst"
    p_sst.write_bytes(bytes(plain_data) + meta_blk + b"\x00\x00\x00\x00\x00" + footer)
    created.append(p_sst)

    # Upgraded Apache Arrow Feather v1 ("FEA1") with UTF8, BOOL, INT16, UINT32, DATE, and null bitmaps
    # Data section right after "FEA1\x00\x00\x00\x00" (offset 8):
    # Col 0 ("host", UTF8, 3 rows, 1 null -> row 1 is NULL: bmp=0b101=0x05 padded to 8B, 4 int32 offsets padded to 8B, data="node-a" + "node-c")
    f_body = bytearray(b"FEA1\x00\x00\x00\x00")
    off_c0 = len(f_body)  # 8
    c0_bmp = bytes([0x05]) + b"\x00" * 7
    c0_offs = struct.pack("<iiii", 0, 6, 6, 12)  # 16 bytes (already 8B aligned)
    c0_str = b"node-anode-c"
    c0_raw = c0_bmp + c0_offs + c0_str
    f_body += c0_raw + b"\x00" * ((8 - (len(c0_raw) & 7)) & 7)
    # Col 1 ("healthy", BOOL, 3 rows, 0 nulls -> bitmask 0b101 = 0x05)
    off_c1 = len(f_body)
    f_body += bytes([0x05]) + b"\x00" * 7
    # Col 2 ("cpu_pct", INT16, 3 rows, 0 nulls -> [42, 99, 17])
    off_c2 = len(f_body)
    f_body += struct.pack("<hhhH", 42, 99, 17, 0)
    # Col 3 ("day", DATE=15, 3 rows, 0 nulls -> [20544, 20545, 20546] days since 1970-01-01)
    off_c3 = len(f_body)
    f_body += struct.pack("<iiii", 20544, 20545, 20546, 0)

    # Minimal forward-patching FlatBuffer builder for Feather v1 CTable (all uoffset32s > 0!)
    fb = bytearray(struct.pack("<I", 0))  # [0..4) root uoffset32

    def fb_align(al: int):
        while len(fb) % al != 0:
            fb.append(0)

    def fb_patch_uoff(field_pos: int, target_pos: int):
        assert target_pos > field_pos
        struct.pack_into("<I", fb, field_pos, target_pos - field_pos)

    # 1. CTable at top (slot 0=description uoff, slot 1=num_rows i64, slot 2=columns uoff, slot 3=version i32)
    fb_align(8)
    ct_vt = len(fb)
    fb.extend(struct.pack("<HHHHHH", 12, 24, 4, 8, 16, 20))
    fb_align(8)
    ct_pos = len(fb)
    fb.extend(struct.pack("<iIqIi", ct_pos - ct_vt, 0, 3, 0, 2))
    struct.pack_into("<I", fb, 0, ct_pos)

    # 2. Columns vector right after CTable
    col_specs = [
        ("host", 11, off_c0, 3, 1, len(c0_raw)),
        ("healthy", 0, off_c1, 3, 0, 1),
        ("cpu_pct", 2, off_c2, 3, 0, 6),
        ("report_date", 15, off_c3, 3, 0, 12),
    ]
    fb_align(4)
    vec_pos = len(fb)
    fb.extend(struct.pack("<I", len(col_specs)))
    vec_elem_slots = []
    for _ in col_specs:
        vec_elem_slots.append(len(fb))
        fb.extend(struct.pack("<I", 0))
    fb_patch_uoff(ct_pos + 16, vec_pos)

    def fb_add_string(s: str) -> int:
        fb_align(4)
        p = len(fb)
        b = s.encode("utf-8")
        fb.extend(struct.pack("<I", len(b)) + b + b"\x00")
        fb_align(4)
        return p

    s_desc = fb_add_string("Explorer2 Feather v1 Sample")
    fb_patch_uoff(ct_pos + 4, s_desc)

    for idx_c, (cname, ptype, doff, vlen, nnull, tbytes) in enumerate(col_specs):
        # Column table first, then its string & PrimitiveArray so both uoffsets are positive!
        fb_align(4)
        c_vt = len(fb)
        fb.extend(struct.pack("<HHHH", 8, 12, 4, 8))
        c_pos = len(fb)
        fb.extend(struct.pack("<iII", c_pos - c_vt, 0, 0))
        fb_patch_uoff(vec_elem_slots[idx_c], c_pos)

        sn = fb_add_string(cname)
        fb_patch_uoff(c_pos + 4, sn)

        fb_align(8)
        pa_vt = len(fb)
        fb.extend(struct.pack("<HHHHHHHH", 16, 40, 4, 5, 8, 16, 24, 32))
        fb_align(8)
        pa_pos = len(fb)
        fb.extend(struct.pack("<ibbHqqqq", pa_pos - pa_vt, ptype, 0, 0, doff, vlen, nnull, tbytes))
        fb_patch_uoff(c_pos + 8, pa_pos)

    fb_align(8)
    f_body += fb + struct.pack("<I", len(fb)) + b"FEA1"
    p_feather = data_dir / "metrics_v1.feather"
    p_feather.write_bytes(bytes(f_body))
    created.append(p_feather)


# ==============================================================================
# 2. GIS Format Samples (testdata/gis/)
# ==============================================================================
def gen_gis_samples(gis_dir: Path, created: list):
    gis_dir.mkdir(parents=True, exist_ok=True)

    # 6. Mapbox Vector Tile v2.1 (.mvt, gzipped Protobuf)
    def mvt_cmd(cmd_id: int, count: int) -> int:
        return (cmd_id & 0x7) | (count << 3)

    def mvt_packed_u32(vals) -> bytes:
        return b"".join(pb_varint(x) for x in vals)

    # Layer 1: "buildings" (POLYGON: MoveTo(1) + LineTo(3) + ClosePath(1))
    geom_poly = [
        mvt_cmd(1, 1), pb_zigzag(512), pb_zigzag(512),
        mvt_cmd(2, 3), pb_zigzag(128), pb_zigzag(0), pb_zigzag(0), pb_zigzag(128), pb_zigzag(-128), pb_zigzag(0),
        mvt_cmd(7, 1),
    ]
    feat_poly = (
        pb_field_varint(1, 101)
        + pb_field_bytes(2, mvt_packed_u32([0, 0, 1, 1]))
        + pb_field_varint(3, 3)  # POLYGON
        + pb_field_bytes(4, mvt_packed_u32(geom_poly))
    )
    val_name = pb_field_bytes(1, b"Central Library")
    val_height = pb_field_f64(3, 28.5)
    layer_buildings = (
        pb_field_varint(15, 2)
        + pb_field_bytes(1, b"buildings")
        + pb_field_bytes(2, feat_poly)
        + pb_field_bytes(3, b"name")
        + pb_field_bytes(3, b"height_m")
        + pb_field_bytes(4, val_name)
        + pb_field_bytes(4, val_height)
        + pb_field_varint(5, 4096)
    )
    # Layer 2: "transit_stops" (POINT: MoveTo(2))
    geom_pts = [mvt_cmd(1, 2), pb_zigzag(600), pb_zigzag(620), pb_zigzag(340), pb_zigzag(180)]
    feat_pts = (
        pb_field_varint(1, 202)
        + pb_field_bytes(2, mvt_packed_u32([0, 0]))
        + pb_field_varint(3, 1)  # POINT
        + pb_field_bytes(4, mvt_packed_u32(geom_pts))
    )
    layer_stops = (
        pb_field_varint(15, 2)
        + pb_field_bytes(1, b"transit_stops")
        + pb_field_bytes(2, feat_pts)
        + pb_field_bytes(3, b"line")
        + pb_field_bytes(4, pb_field_bytes(1, b"U2 Metro"))
        + pb_field_varint(5, 4096)
    )
    raw_mvt = pb_field_bytes(3, layer_buildings) + pb_field_bytes(3, layer_stops)
    gz_buf = gzip.compress(raw_mvt, mtime=0)
    p_mvt = gis_dir / "city_transit_tile.mvt"
    p_mvt.write_bytes(gz_buf)
    created.append(p_mvt)

    # 7A. OGC Well-Known Text / PostGIS EWKT (.wkt)
    wkt_text = """# Alpine pass GPS route & summit boundary (WGS84 / EPSG:4326)
SRID=4326;LINESTRING Z (10.453100 46.528600 2757.0, 10.458900 46.531200 2760.5, 10.465400 46.534800 2742.0);
SRID=4326;POLYGON ((10.450000 46.525000, 10.470000 46.525000, 10.470000 46.540000, 10.450000 46.540000, 10.450000 46.525000));
SRID=4326;MULTIPOINT ((10.453100 46.528600), (10.465400 46.534800));
"""
    p_wkt = gis_dir / "alpine_route.wkt"
    p_wkt.write_bytes(wkt_text.encode("utf-8"))
    created.append(p_wkt)

    # 7B. OGC Well-Known Binary / PostGIS EWKB (.wkb)
    # Feature 1: EWKB LE LINESTRING (0x20000002) SRID=4326, 3 vertices around Gdansk Harbor
    ewkb1 = struct.pack("<BIII", 1, 0x20000002, 4326, 3) + b"".join(
        struct.pack("<dd", lon, lat)
        for (lon, lat) in [(18.665000, 54.405000), (18.672500, 54.408200), (18.681000, 54.412000)]
    )
    # Feature 2: EWKB LE POINT Z (0xA0000001) SRID=4326
    ewkb2 = struct.pack("<BII", 1, 0xA0000001, 4326) + struct.pack("<ddd", 18.665000, 54.405000, 14.5)
    p_wkb = gis_dir / "harbor_zones.wkb"
    p_wkb.write_bytes(ewkb1 + ewkb2)
    created.append(p_wkb)


# ==============================================================================
# 3. Audio Format Samples (testdata/audio/)
# ==============================================================================
def gen_audio_samples(audio_dir: Path, created: list):
    audio_dir.mkdir(parents=True, exist_ok=True)

    # 8. Apple Core Audio Format (.caf) — 48 kHz Stereo 32-bit Float LPCM + chan + info + mark + pakt + data
    def caf_chunk(fourcc: bytes, body: bytes) -> bytes:
        return fourcc + struct.pack(">q", len(body)) + body

    desc = struct.pack(">d4sIIIII", 48000.0, b"lpcm", 0x03, 8, 1, 2, 32)  # Float | LE
    chan = struct.pack(">III", (101 << 16) | 2, 0x03, 0)  # Stereo (L, R)
    info_kv = [(b"title", b"Aurora Borealis Stem"), (b"artist", b"Explorer2 Synth Lab"), (b"tempo", b"120")]
    info_body = struct.pack(">I", len(info_kv)) + b"".join(k + b"\x00" + v + b"\x00" for k, v in info_kv)
    mark_body = struct.pack(">II", 0, 2) + (
        b"CueP" + struct.pack(">dI8sI", 0.0, 1, b"\x00" * 8, 0)
        + b"CueP" + struct.pack(">dI8sI", 120.0, 2, b"\x00" * 8, 0)
    )
    n_frames = 240
    pakt_body = struct.pack(">qqii", n_frames, n_frames, 0, 0)
    pcm = bytearray(struct.pack(">I", 0))  # mEditCount = 0
    for i in range(n_frames):
        t = i / 48000.0
        pcm += struct.pack("<ff", 0.25 * math.sin(2.0 * math.pi * 440.0 * t), 0.25 * math.sin(2.0 * math.pi * 554.37 * t))
    caf_bytes = (
        b"caff" + struct.pack(">HH", 1, 0)
        + caf_chunk(b"desc", desc)
        + caf_chunk(b"chan", chan)
        + caf_chunk(b"info", info_body)
        + caf_chunk(b"mark", mark_body)
        + caf_chunk(b"pakt", pakt_body)
        + caf_chunk(b"data", bytes(pcm))
    )
    p_caf = audio_dir / "studio_stem.caf"
    p_caf.write_bytes(caf_bytes)
    created.append(p_caf)

    # 9. WavPack Audio (.wv) — wvpk v0x0410 96 kHz 24-bit Stereo + metadata sub-blocks + APEv2 tag
    def wv_subblock(sub_id: int, payload: bytes) -> bytes:
        odd = len(payload) & 1
        padded = payload + (b"\x00" if odd else b"")
        words = len(padded) // 2
        hdr_id = sub_id | (0x40 if odd else 0)
        return bytes([hdr_id, words]) + padded

    md5_raw = hashlib.md5(b"Explorer2-WavPack-Deterministic-PCM").digest()
    wv_subs = (
        wv_subblock(0x0D, bytes([2, 0x03]))            # 2 channels, FL|FR mask
        + wv_subblock(0x25, bytes([0x00, 0x08, 0x00])) # HIGH profile
        + wv_subblock(0x26, md5_raw)                   # 16-byte Audio MD5
        + wv_subblock(0x27, struct.pack("<I", 96000)[:3])  # Custom/explicit 96000 Hz
    )
    # flags: bytes_per_sample=3 (bits 0..1 = 2 -> 24-bit), INITIAL_BLOCK(0x800)|FINAL_BLOCK(0x1000)|JOINT_STEREO(0x10), rate_idx=13 (96000)
    wv_flags = 2 | 0x10 | 0x0800 | 0x1000 | (13 << 23)
    ck_size = 24 + len(wv_subs)
    wv_hdr = b"wvpk" + struct.pack("<IHBBIIIII", ck_size, 0x0410, 0, 0, 96000, 0, 4096, wv_flags, 0xA5C30199)
    # APEv2 tag at EOF
    ape_items = b""
    for k, v in [(b"Title", b"Lossless Studio Take"), (b"Artist", b"Explorer2 DSP"), (b"Album", b"Reference Suite")]:
        ape_items += struct.pack("<II", len(v), 0) + k + b"\x00" + v
    ape_tag_size = len(ape_items) + 32
    ape_hdr = b"APETAGEX" + struct.pack("<IIII8s", 2000, ape_tag_size, 3, 0xA0000000, b"\x00" * 8)
    ape_ftr = b"APETAGEX" + struct.pack("<IIII8s", 2000, ape_tag_size, 3, 0x80000000, b"\x00" * 8)
    p_wv = audio_dir / "master_take.wv"
    p_wv.write_bytes(wv_hdr + wv_subs + ape_hdr + ape_items + ape_ftr)
    created.append(p_wv)

    # PLS Playlist (.pls)
    pls_txt = """[playlist]
NumberOfEntries=2
File1=https://stream.example.org/synthwave-hi.ogg
Title1=Nightdrive Synthwave FM (320k Ogg)
Length1=-1
File2= studio_stem.caf
Title2=Aurora Borealis Stem (Local Reference)
Length2=5
Version=2
"""
    p_pls = audio_dir / "radio_streams.pls"
    p_pls.write_bytes(pls_txt.encode("utf-8"))
    created.append(p_pls)

    # Upgraded SNES SPC700 (.spc) with ID666 + 64KB RAM BRR sample directory + 8-voice S-DSP + Extended xid6
    spc = bytearray(0x10200)
    spc[0:33] = b"SNES-SPC700 Sound File Data v0.30"
    spc[33:35] = b"\x1A\x1A"
    spc[35] = 26  # ID666 present
    spc[36] = 30  # v0.30
    struct.pack_into("<HBBBH", spc, 37, 0x0400, 0x10, 0x00, 0x00, 0x01EF)  # PC=$0400, SP=$EF
    spc[0x2E : 0x2E + 32] = b"Overworld Theme".ljust(32, b"\x00")
    spc[0x4E : 0x4E + 32] = b"Chrono Horizon".ljust(32, b"\x00")
    spc[0x6E : 0x6E + 16] = b"Explorer2SPC".ljust(16, b"\x00")
    spc[0x9E : 0x9E + 11] = b"11/25/2026\x00"
    spc[0xA9 : 0xA9 + 3] = b"120"
    spc[0xAC : 0xAC + 5] = b"05000"
    spc[0xB1 : 0xB1 + 32] = b"Nobuo Reference Synth".ljust(32, b"\x00")
    # Place BRR sample directory at RAM $0200 (DIR = 0x02) with 2 valid BRR samples at $1000 and $1012
    ram_base = 0x100
    struct.pack_into("<HHHH", spc, ram_base + 0x0200, 0x1000, 0x1009, 0x1012, 0x1012)
    spc[ram_base + 0x1000] = 0x00  # BRR block 0
    spc[ram_base + 0x1009] = 0x03  # BRR block 1 (end + loop)
    spc[ram_base + 0x1012] = 0x01  # BRR sample 1 (1-shot end)
    # S-DSP registers at 0x10100: Voice 0 & Voice 1 active
    dsp = memoryview(spc)[0x10100:0x10180]
    dsp[0x00], dsp[0x01] = 100, 100  # V0 Vol L/R
    struct.pack_into("<H", dsp, 0x02, 0x1000)  # V0 Pitch = 32000 Hz
    dsp[0x04], dsp[0x05], dsp[0x06] = 0, 0xFF, 0xE0  # SRCN=0, ADSR enabled
    dsp[0x10], dsp[0x11] = 80, 90   # V1 Vol L/R
    struct.pack_into("<H", dsp, 0x12, 0x0C00)  # V1 Pitch = 24000 Hz
    dsp[0x14], dsp[0x15], dsp[0x17] = 1, 0x00, 0x7F  # SRCN=1, Direct GAIN
    dsp[0x0C], dsp[0x1C] = 127, 127  # MVOL L/R
    dsp[0x2C], dsp[0x3C] = 40, 40    # EVOL L/R
    dsp[0x4C], dsp[0x3D], dsp[0x5D] = 0x03, 0x01, 0x02  # KON=V0|V1, EON=V0, DIR=$0200
    for tap_i, coeff in enumerate([127, 0, 0, 0, 0, 0, 0, 0]):
        dsp[0x0F + tap_i * 0x10] = coeff
    # Extended ID666 xid6 chunk at 0x10200
    ost_str = b"Chrono Horizon Original Soundtrack\x00\x00"  # 36B (4-byte aligned)
    xid6_body = struct.pack("<BBH", 0x10, 1, len(ost_str) - 1) + ost_str + struct.pack("<BBH", 0x11, 0, 2)
    xid6 = b"xid6" + struct.pack("<I", len(xid6_body)) + xid6_body
    p_spc = audio_dir / "snes_overworld.spc"
    p_spc.write_bytes(bytes(spc) + xid6)
    created.append(p_spc)

    # Upgraded VGM v1.70 (.vgm) + VGZ (.vgz)
    vgm_hdr = bytearray(0x80)
    vgm_hdr[0:4] = b"Vgm "
    struct.pack_into("<I", vgm_hdr, 0x08, 0x00000170)  # v1.70
    struct.pack_into("<I", vgm_hdr, 0x0C, 3579545)     # SN76489 PSG clock
    struct.pack_into("<I", vgm_hdr, 0x18, 88200)       # Total samples (2.0 s @ 44.1 kHz)
    struct.pack_into("<I", vgm_hdr, 0x24, 60)          # 60 Hz NTSC
    struct.pack_into("<I", vgm_hdr, 0x2C, 7670454)     # YM2612 OPN2 clock
    struct.pack_into("<I", vgm_hdr, 0x34, 0x80 - 0x34) # VGM data offset -> 0x80
    vgm_cmds = (
        b"\x67\x66\x00" + struct.pack("<I", 4) + b"\x80\x90\xA0\xB0"  # YM2612 PCM data block
        + b"\x50\x90"          # SN76489 write
        + b"\x52\x22\x08"      # YM2612 port 0 write
        + b"\x53\xB4\xC0"      # YM2612 port 1 write
        + b"\xE0\x00\x00\x00\x00"
        + b"\x62\x62\x66"      # 2 NTSC waits + End of Sound Data
    )
    gd3_strings = [
        "Emerald Hill Zone", "Emerald Hill Zone",
        "Mega Drive Tech Demo", "Mega Drive Tech Demo",
        "Sega Mega Drive / Genesis", "Sega Mega Drive",
        "Masato Reference", "1992/11/21", "Explorer2 VGM Gen", "FM + PSG + DAC stream",
    ]
    gd3_payload = b"".join((s + "\x00").encode("utf-16le") for s in gd3_strings) + b"\x00\x00"
    gd3_chunk = b"Gd3 " + struct.pack("<II", 0x00000100, len(gd3_payload)) + gd3_payload
    gd3_off = len(vgm_hdr) + len(vgm_cmds)
    total_vgm_len = gd3_off + len(gd3_chunk)
    struct.pack_into("<I", vgm_hdr, 0x04, total_vgm_len - 4)
    struct.pack_into("<I", vgm_hdr, 0x14, gd3_off - 0x14)
    vgm_bytes = bytes(vgm_hdr) + vgm_cmds + gd3_chunk
    p_vgm = audio_dir / "genesis_stage1.vgm"
    p_vgm.write_bytes(vgm_bytes)
    created.append(p_vgm)

    p_vgz = audio_dir / "arcade_fm.vgz"
    p_vgz.write_bytes(gzip.compress(vgm_bytes, mtime=0))
    created.append(p_vgz)

    # Upgraded NSF (.nsf) & NSFE (.nsfe)
    nsf = bytearray(128)
    nsf[0:5] = b"NESM\x1A"
    nsf[5] = 1   # version 1
    nsf[6] = 4   # 4 songs
    nsf[7] = 1   # start song 1
    struct.pack_into("<HHH", nsf, 8, 0x8000, 0x8000, 0x8003)
    nsf[0x0E : 0x0E + 32] = b"Akumajo VRC6 Prelude".ljust(32, b"\x00")
    nsf[0x2E : 0x2E + 32] = b"Famicom Sound Team".ljust(32, b"\x00")
    nsf[0x4E : 0x4E + 32] = b"1989 Konami Style Demo".ljust(32, b"\x00")
    struct.pack_into("<H", nsf, 0x6E, 16639)  # NTSC 60.0988 Hz
    nsf[0x70:0x78] = bytes([0, 1, 2, 3, 4, 5, 6, 7])  # bankswitch active
    struct.pack_into("<H", nsf, 0x78, 19997)  # PAL 50.0070 Hz
    nsf[0x7A] = 0x02  # Dual NTSC/PAL
    nsf[0x7B] = 0x11  # VRC6 (0x01) | Namco 163 (0x10)
    code_6502 = b"\x4C\x06\x80\x4C\x06\x80\x60"  # JMP $8006; JMP $8006; RTS
    p_nsf = audio_dir / "famicom_vrc6.nsf"
    p_nsf.write_bytes(bytes(nsf) + code_6502)
    created.append(p_nsf)

    def nsfe_chunk(cid: bytes, body: bytes) -> bytes:
        return struct.pack("<I", len(body)) + cid + body

    nsfe_bytes = (
        b"NSFE"
        + nsfe_chunk(b"INFO", struct.pack("<HHHBBBB", 0x8000, 0x8000, 0x8003, 0, 0x05, 3, 0))  # VRC6 + FDS, 3 tracks
        + nsfe_chunk(b"BANK", bytes([0, 1, 2, 3, 4, 5, 6, 7]))
        + nsfe_chunk(b"DATA", code_6502)
        + nsfe_chunk(b"auth", b"Clocktower Suite\x00Explorer2 2A03\x002026 Synth\x00NSFE Ripper\x00")
        + nsfe_chunk(b"tlbl", b"01. Opening Gate\x0002. Clockwork Hall\x0003. Boss Confrontation\x00")
        + nsfe_chunk(b"time", struct.pack("<iii", 95000, 142000, 110000))
        + nsfe_chunk(b"fade", struct.pack("<iii", 5000, 5000, 3000))
        + nsfe_chunk(b"plst", bytes([0, 1, 2]))
        + nsfe_chunk(b"NEND", b"")
    )
    p_nsfe = audio_dir / "castlevania_suite.nsfe"
    p_nsfe.write_bytes(nsfe_bytes)
    created.append(p_nsfe)

    # Upgraded PSID v4 Multi-SID (.sid)
    sid = bytearray(0x7C)
    sid[0:4] = b"PSID"
    struct.pack_into(">HHHHHHHHI", sid, 4, 4, 0x007C, 0x0000, 0x1000, 0x1003, 2, 1, 0, 0)
    sid[0x16 : 0x16 + 32] = b"Cybernetic Stereo 8580".ljust(32, b"\x00")
    sid[0x36 : 0x36 + 32] = b"Martin Hubb Style".ljust(32, b"\x00")
    sid[0x56 : 0x56 + 32] = b"2026 C64 Dual-SID Demo".ljust(32, b"\x00")
    # Flags: PAL (01 << 2 = 0x04) | Primary MOS8580 (10 << 4 = 0x20) | 2nd MOS8580 (10 << 6 = 0x80) | 3rd MOS6581 (01 << 8 = 0x100)
    struct.pack_into(">HBBBB", sid, 0x76, 0x01A4, 0x20, 0x40, 0x42, 0x44)  # 2nd=$D420, 3rd=$D440
    prg_body = struct.pack("<H", 0x1000) + b"\x78\xA9\x00\x60\xEA\x60"  # embedded load=$1000 + SEI; LDA #0; RTS
    p_sid = audio_dir / "c64_dual_sid.sid"
    p_sid.write_bytes(bytes(sid) + prg_body)
    created.append(p_sid)


# ==============================================================================
# 4. Security & Forensics Samples (testdata/security/)
# ==============================================================================
def gen_security_samples(sec_dir: Path, created: list):
    sec_dir.mkdir(parents=True, exist_ok=True)

    # 10. Apple Safari Binary Cookies (.binarycookies)
    def build_cookie_rec(domain: str, name: str, path: str, value: str, comment: str, flags: int, cre_cf: float, exp_cf: float) -> bytes:
        hdr_len = 56
        cur = hdr_len
        blobs = bytearray()

        def add_s(s: str) -> int:
            nonlocal cur
            if not s:
                return 0
            off = cur
            b = s.encode("utf-8") + b"\x00"
            blobs.extend(b)
            cur += len(b)
            return off

        c_off = add_s(comment)
        u_off = add_s(domain)
        n_off = add_s(name)
        p_off = add_s(path)
        v_off = add_s(value)
        rec = struct.pack(
            "<IIIIIIIIIIdd",
            hdr_len + len(blobs),
            0,
            flags,
            0,
            u_off,
            n_off,
            p_off,
            v_off,
            c_off,
            0,
            exp_cf,
            cre_cf,
        )
        assert len(rec) == 56
        return rec + bytes(blobs)

    # CFAbsoluteTime for 2026-05-01 00:00:00 UTC = 1777593600 - 978307200 = 799286400.0
    c1 = build_cookie_rec(".github.com", "user_session", "/", "ghs_98AF71C02E", "Session auth", 0x01 | 0x04 | 0x20, 799286400.0, 801878400.0)
    c2 = build_cookie_rec(".developer.apple.com", "ADCDownloadAuth", "/services", "eyJhbGciOiJSUzI1NiJ9", "", 0x01 | 0x04 | 0x18, 799286500.0, 799372900.0)
    page1_hdr_len = 8 + 2 * 4 + 4
    page1 = struct.pack(">I", 0x00000100) + struct.pack("<IIII", 2, page1_hdr_len, page1_hdr_len + len(c1), 0) + c1 + c2
    checksum = sum(page1[::4]) & 0xFFFFFFFF
    cook_bytes = b"cook" + struct.pack(">II", 1, len(page1)) + page1 + struct.pack(">II", 0, checksum)
    p_cookies = sec_dir / "safari_jar.binarycookies"
    p_cookies.write_bytes(cook_bytes)
    created.append(p_cookies)

    # 11. Windows AutomaticDestinations Jump List (.automaticdestinations-ms)
    def build_ms_shllink(local_path: str, args: str, working_dir: str, ft_write: int, file_sz: int) -> bytes:
        clsid = bytes([0x01,0x14,0x02,0x00,0x00,0x00,0x00,0x00, 0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46])
        # LinkFlags: HasLinkInfo(0x02) | HasWorkingDir(0x10) | HasArguments(0x20) | IsUnicode(0x80)
        lflags = 0x02 | 0x10 | 0x20 | 0x80
        shdr = struct.pack("<I16sIIQQQIIIHHI", 0x4C, clsid, lflags, 0x20, ft_write, ft_write, ft_write, file_sz, 0, 1, 0, 0, 0) + b"\x00" * 4
        assert len(shdr) == 76
        # LinkInfo (VolumeIDAndLocalBasePath = 0x01)
        vol = struct.pack("<IIII", 17, 3, 0x8842CAFE, 16) + b"\x00"
        base_ansi = local_path.encode("ascii") + b"\x00"
        suf_ansi = b"\x00"
        li_hdr_sz = 28
        vol_off = li_hdr_sz
        base_off = vol_off + len(vol)
        suf_off = base_off + len(base_ansi)
        li_sz = suf_off + len(suf_ansi)
        link_info = struct.pack("<IIIIIII", li_sz, li_hdr_sz, 0x01, vol_off, base_off, 0, suf_off) + vol + base_ansi + suf_ansi

        def counted_u16(s: str) -> bytes:
            return struct.pack("<H", len(s)) + s.encode("utf-16le")

        return shdr + link_info + counted_u16(working_dir) + counted_u16(args) + struct.pack("<I", 0)

    def build_destlist_v4_entry(entry_id: int, pin_status: int, access_cnt: int, ft_last: int, host: bytes, path_str: str) -> bytes:
        vol_guid = bytes.fromhex("11223344556677889900aabbccddeeff")
        obj_guid = bytes.fromhex("a1b2c3d4e5f60718293a4b5c6d7e8f90")
        hdr = bytearray(130)
        struct.pack_into("<Q", hdr, 0, 0xCAFEBABE00000000 | entry_id)
        hdr[8:24] = vol_guid
        hdr[24:40] = obj_guid
        hdr[40:56] = vol_guid
        hdr[56:72] = obj_guid
        hdr[72:88] = host.ljust(16, b"\x00")[:16]
        struct.pack_into("<IQ", hdr, 88, entry_id, 0)
        struct.pack_into("<QIiIQH", hdr, 100, ft_last, pin_status, -1, access_cnt, 0, len(path_str))
        return bytes(hdr) + path_str.encode("utf-16le") + struct.pack("<I", 0)

    ft1 = 134180000000000000  # 2026 UTC FILETIME
    ft2 = 134181000000000000
    lnk1 = build_ms_shllink(r"C:\Projects\Explorer2\README.md", "--preview", r"C:\Projects\Explorer2", ft1, 18432)
    lnk2 = build_ms_shllink(r"D:\Forensics\capture_01.evtx", "--verify", r"D:\Forensics", ft2, 69632)
    dl_hdr = struct.pack("<IIIIQQ", 4, 2, 1, 0, 2, 19)  # v4 (Win10/11), 2 entries, 1 pinned
    dl_e1 = build_destlist_v4_entry(1, 0, 14, ft1, b"WS-ANALYST01", r"C:\Projects\Explorer2\README.md")
    dl_e2 = build_destlist_v4_entry(2, 0xFFFFFFFF, 5, ft2, b"WS-ANALYST01", r"D:\Forensics\capture_01.evtx")
    cfb_bytes = build_ole2_cfb([
        ("1", lnk1),
        ("2", lnk2),
        ("DestList", dl_hdr + dl_e1 + dl_e2),
    ])
    p_jl = sec_dir / "recent_docs.automaticdestinations-ms"
    p_jl.write_bytes(cfb_bytes)
    created.append(p_jl)

    # 12. Real Windows BinXML Event Log (.evtx) with TemplateInstance (0x0C) + TemplateDef + Typed Substitutions (0x0D)
    chunk = bytearray(65536)
    chunk[0:8] = b"ElfChnk\x00"
    struct.pack_into("<QQQQ", chunk, 8, 1, 1, 1, 1)
    struct.pack_into("<III", chunk, 40, 128, 1, 0)

    # Place BinXML Name records inside the chunk starting at offset 0x2000
    name_cursor = 0x2000

    def add_evtx_name(s: str) -> int:
        nonlocal name_cursor
        off = name_cursor
        u16 = (s + "\x00").encode("utf-16le")
        rec = struct.pack("<IHH", 0, 0x1234, len(s)) + u16
        chunk[off : off + len(rec)] = rec
        name_cursor += (len(rec) + 3) & ~3
        return off

    n_event    = add_evtx_name("Event")
    n_system   = add_evtx_name("System")
    n_provider = add_evtx_name("Provider")
    n_name_a   = add_evtx_name("Name")
    n_eventid  = add_evtx_name("EventID")
    n_level    = add_evtx_name("Level")
    n_timecr   = add_evtx_name("TimeCreated")
    n_systime  = add_evtx_name("SystemTime")
    n_channel  = add_evtx_name("Channel")
    n_computer = add_evtx_name("Computer")
    n_evdata   = add_evtx_name("EventData")
    n_data     = add_evtx_name("Data")

    def bxml_open(name_off: int, has_attrs: bool = False) -> bytes:
        tok = 0x41 if has_attrs else 0x01
        return bytes([tok]) + struct.pack("<HII", 0xFFFF, 6, name_off)

    def bxml_attr(name_off: int, val_tokens: bytes) -> bytes:
        return bytes([0x06]) + struct.pack("<I", name_off) + val_tokens

    def bxml_subst(sub_id: int, val_type: int) -> bytes:
        return bytes([0x0D]) + struct.pack("<HB", sub_id, val_type)

    # Template skeleton:
    # <Event><System>
    #   <Provider Name="%0"/><EventID>%1</EventID><Level>%2</Level>
    #   <TimeCreated SystemTime="%3"/><Channel>%4</Channel><Computer>%5</Computer>
    # </System><EventData>
    #   <Data Name="TargetUserName">%6</Data><Data Name="SubjectUserSid">%7</Data><Data Name="LogonType">%8</Data>
    # </EventData></Event>
    def bxml_lit_wstr(s: str) -> bytes:
        return bytes([0x05, 0x01]) + struct.pack("<H", len(s)) + s.encode("utf-16le")

    prov_attrs = bxml_attr(n_name_a, bxml_subst(0, 0x01))
    tc_attrs   = bxml_attr(n_systime, bxml_subst(3, 0x11))  # 0x11 = FILETIME
    d0_attrs   = bxml_attr(n_name_a, bxml_lit_wstr("TargetUserName"))
    d1_attrs   = bxml_attr(n_name_a, bxml_lit_wstr("SubjectUserSid"))
    d2_attrs   = bxml_attr(n_name_a, bxml_lit_wstr("LogonType"))

    tmpl_bxml = (
        b"\x0F\x01\x01\x00"  # FragmentHeader
        + bxml_open(n_event) + b"\x02"
        + bxml_open(n_system) + b"\x02"
        + bxml_open(n_provider, True) + struct.pack("<I", len(prov_attrs)) + prov_attrs + b"\x03"
        + bxml_open(n_eventid) + b"\x02" + bxml_subst(1, 0x06) + b"\x04"   # 0x06 = UInt16
        + bxml_open(n_level) + b"\x02" + bxml_subst(2, 0x04) + b"\x04"     # 0x04 = UInt8
        + bxml_open(n_timecr, True) + struct.pack("<I", len(tc_attrs)) + tc_attrs + b"\x03"
        + bxml_open(n_channel) + b"\x02" + bxml_subst(4, 0x01) + b"\x04"   # 0x01 = UnicodeString
        + bxml_open(n_computer) + b"\x02" + bxml_subst(5, 0x01) + b"\x04"
        + b"\x04"  # </System>
        + bxml_open(n_evdata) + b"\x02"
        + bxml_open(n_data, True) + struct.pack("<I", len(d0_attrs)) + d0_attrs + b"\x02" + bxml_subst(6, 0x01) + b"\x04"
        + bxml_open(n_data, True) + struct.pack("<I", len(d1_attrs)) + d1_attrs + b"\x02" + bxml_subst(7, 0x13) + b"\x04"  # 0x13 = SID
        + bxml_open(n_data, True) + struct.pack("<I", len(d2_attrs)) + d2_attrs + b"\x02" + bxml_subst(8, 0x08) + b"\x04"  # 0x08 = UInt32
        + b"\x04"  # </EventData>
        + b"\x04"  # </Event>
        + b"\x00"  # EOF
    )

    # SID S-1-5-21-1000001-2000002-3000003-1104
    sid_bytes = bytes([1, 5, 0, 0, 0, 0, 0, 5]) + struct.pack("<5I", 21, 1000001, 2000002, 3000003, 1104)
    subst_items = [
        (0x01, "Microsoft-Windows-Security-Auditing".encode("utf-16le")),
        (0x06, struct.pack("<H", 4624)),               # EventID 4624 (Successful Logon)
        (0x04, bytes([0])),                            # Level 0 (LogAlways / Information)
        (0x11, struct.pack("<Q", ft2)),                # TimeCreated FILETIME
        (0x01, "Security".encode("utf-16le")),
        (0x01, "DC01.corp.local".encode("utf-16le")),
        (0x01, "alice.admin".encode("utf-16le")),
        (0x13, sid_bytes),
        (0x08, struct.pack("<I", 10)),                 # LogonType 10 (RemoteInteractive / RDP)
    ]
    subst_desc = b"".join(struct.pack("<HBB", len(b), vt, 0) for vt, b in subst_items)
    subst_vals = b"".join(b for _, b in subst_items)

    # Place TemplateDef at chunk offset 0x3000
    tmpl_def_off = 0x3000
    tmpl_guid = bytes.fromhex("33221100554477668899aabbccddeeff")
    tmpl_def = struct.pack("<I16sI", 0, tmpl_guid, len(tmpl_bxml)) + tmpl_bxml
    chunk[tmpl_def_off : tmpl_def_off + len(tmpl_def)] = tmpl_def

    # Event Record at chunk offset 512 referencing TemplateDef @ 0x3000
    inst_stream = (
        b"\x0F\x01\x01\x00"  # FragmentHeader
        + b"\x0C\x01"        # TemplateInstance
        + struct.pack("<III", 0, tmpl_def_off, len(subst_items))
        + subst_desc
        + subst_vals
    )
    pad_len = (4 - (len(inst_stream) & 3)) & 3
    rec_body = inst_stream + (b"\x00" * pad_len)
    rec_total_sz = 24 + len(rec_body) + 4
    ev_rec = struct.pack("<IIQQ", 0x00002A2A, rec_total_sz, 1, ft2) + rec_body + struct.pack("<I", rec_total_sz)
    chunk[512 : 512 + len(ev_rec)] = ev_rec
    struct.pack_into("<I", chunk, 48, 512 + len(ev_rec))  # freeSpaceOff

    evtx_file_hdr = bytearray(4096)
    evtx_file_hdr[0:8] = b"ElfFile\x00"
    struct.pack_into("<QQQIIHHH", evtx_file_hdr, 8, 0, 0, 2, 128, 1, 4096, 1, 0)
    # Fix exact ElfFile header fields: +8 firstChunk(u64), +16 lastChunk(u64), +24 nextRecId(u64), +32 hdrSize(u32=128), +36 minVer(u16=1), +38 majVer(u16=3), +40 hdrBlkSz(u16=4096), +42 numChunks(u16=1)
    struct.pack_into("<QQQIHHHH", evtx_file_hdr, 8, 0, 0, 2, 128, 1, 3, 4096, 1)
    p_evtx = sec_dir / "security_logon.evtx"
    p_evtx.write_bytes(bytes(evtx_file_hdr) + bytes(chunk))
    created.append(p_evtx)



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


def ue_fstring_utf8(s: str) -> bytes:
    if not s:
        return struct.pack("<i", 0)
    raw = s.encode("utf-8") + b"\x00"
    return struct.pack("<i", len(raw)) + raw

def gen_domain_bioware_2da(out_dir: Path):
    # Columns: label, modelname, sabercolor (3 cols), 2 rows ("0", "1")
    cols_blob = b"label\tmodelname\tsabercolor\t\x00"
    rows_blob = b"0\t1\t"
    # String pool of unique NUL-terminated cell values
    cells = [b"DoubleSaber", b"w_dblsbr_001", b"Blue", b"ShortSaber", b"w_shsbr_002", b"Purple"]
    pool = bytearray()
    offs = []
    for c in cells:
        offs.append(len(pool))
        pool += c + b"\x00"
    blob = (
        b"2DA V2.b\n"
        + cols_blob
        + struct.pack("<I", 2)
        + rows_blob
        + struct.pack("<6HH", *offs, len(pool))
        + bytes(pool)
    )
    (out_dir / "kotor_lightsabers.2da").write_bytes(blob)

def gen_domain_bioware_tlk(out_dir: Path):
    s0 = b"Statement: HK-47 is ready to serve, Master."
    s1 = b"The Force will be with you. Always."
    pool_off = 20 + 2 * 40
    hdr = struct.pack("<8sIII", b"TLK V3.0", 0, 2, pool_off)
    e0 = struct.pack("<I16sIIIIf", 0x07, b"vo_hk47_001".ljust(16, b"\x00"), 0, 0, 0, len(s0), 3.25)
    e1 = struct.pack("<I16sIIIIf", 0x07, b"vo_bast_002".ljust(16, b"\x00"), 0, 0, len(s0), len(s1), 2.80)
    (out_dir / "kotor_dialog.tlk").write_bytes(hdr + e0 + e1 + s0 + s1)

def gen_domain_bioware_gff(out_dir: Path):
    # Struct 0 (root): 4 fields -> FieldIndices [0, 1, 2, 3]
    # Struct 1 (equipped item in Equip_ItemList[0]): 1 field -> Field 4 (direct index!)
    structs = (
        struct.pack("<III", 0xFFFFFFFF, 0, 4)
        + struct.pack("<III", 2, 4, 1)
    )
    labels = b"".join(s.ljust(16, b"\x00") for s in [
        b"Tag", b"TemplateResRef", b"HitPoints", b"Equip_ItemList", b"EquippedRes",
    ])
    # FieldData:
    #   +0  CExoString "HK47_DROID" (4 + 10 = 14B)
    #   +14 CResRef    "n_hk47"     (1 + 6  = 7B)
    #   +21 CResRef    "w_blstr_004"(1 + 11 = 12B)
    fdata = (
        struct.pack("<I10s", 10, b"HK47_DROID")
        + bytes([6]) + b"n_hk47"
        + bytes([11]) + b"w_blstr_004"
    )
    fields = (
        struct.pack("<III", 10, 0, 0)    # [0] Tag = CExoString @ fdata+0
        + struct.pack("<III", 11, 1, 14) # [1] TemplateResRef = CResRef @ fdata+14
        + struct.pack("<III", 2,  2, 180)# [2] HitPoints = WORD 180
        + struct.pack("<III", 15, 3, 0)  # [3] Equip_ItemList = List @ lindx+0
        + struct.pack("<III", 11, 4, 21) # [4] EquippedRes = CResRef @ fdata+21
    )
    findx = struct.pack("<4I", 0, 1, 2, 3)
    lindx = struct.pack("<2I", 1, 1)  # count=1, structIdx=1

    st_off = 56
    fl_off = st_off + len(structs)
    lb_off = fl_off + len(fields)
    fd_off = lb_off + len(labels)
    fi_off = fd_off + len(fdata)
    li_off = fi_off + len(findx)

    hdr = struct.pack(
        "<4s4s12I",
        b"UTC ", b"V3.2",
        st_off, 2,
        fl_off, 5,
        lb_off, 5,
        fd_off, len(fdata),
        fi_off, len(findx),
        li_off, len(lindx),
    )
    assert len(hdr) == 56
    (out_dir / "hk47_creature.gff").write_bytes(hdr + structs + fields + labels + fdata + findx + lindx)

def gen_domain_gettext_mo(out_dir: Path):
    pairs = [
        (b"", b"Project-Id-Version: GodotMod 2.0\nLanguage: pl\nContent-Type: text/plain; charset=UTF-8\n"),
        (b"HUD\x04Health", "Życie".encode("utf-8")),
        (b"%d Enemy\x00%d Enemies", "1 Wróg\x00%d Wrogów".encode("utf-8")),
    ]
    n = len(pairs)
    orig_tbl_off = 28
    trans_tbl_off = orig_tbl_off + n * 8
    str_start = trans_tbl_off + n * 8

    pool = bytearray()
    orig_tbl = bytearray()
    trans_tbl = bytearray()
    for o_bytes, t_bytes in pairs:
        orig_tbl += struct.pack("<II", len(o_bytes), str_start + len(pool))
        pool += o_bytes + b"\x00"
        trans_tbl += struct.pack("<II", len(t_bytes), str_start + len(pool))
        pool += t_bytes + b"\x00"

    hdr = struct.pack("<7I", 0x950412DE, 0, n, orig_tbl_off, trans_tbl_off, 0, 0)
    (out_dir / "godot_mod_pl.mo").write_bytes(hdr + bytes(orig_tbl) + bytes(trans_tbl) + bytes(pool))

def gen_domain_gettext_po(out_dir: Path):
    po_txt = """# KiCad PCB Editor Localization Catalog
msgid ""
msgstr ""
"Project-Id-Version: KiCad 8.0\\n"
"Language: de\\n"
"Content-Type: text/plain; charset=UTF-8\\n"

#: pcbnew/router/pns_tool.cpp:142
#, fuzzy, c-format
msgctxt "Router"
msgid "Interactive Push & Shove (%d vias)"
msgstr "Interaktives Schieben & Drängen (%d Durchkontaktierungen)"

#: pcbnew/drc/drc_engine.cpp:88
msgid "%d DRC violation"
msgid_plural "%d DRC violations"
msgstr[0] "%d DRC-Fehler"
msgstr[1] "%d DRC-Fehler"

#~ msgid "Obsolete Entry"
#~ msgstr "Veraltet"
"""
    (out_dir / "kicad_router_de.po").write_bytes(po_txt.encode("utf-8"))

def gen_domain_arm_cmsis_pdsc(out_dir: Path):
    pdsc_xml = """<?xml version="1.0" encoding="UTF-8"?>
<package schemaVersion="1.7.28" xmlns:xs="http://www.w3.org/2001/XMLSchema-instance">
  <vendor>STMicroelectronics</vendor>
  <name>STM32H7xx_DFP</name>
  <description>STMicroelectronics STM32H7 Series Device Support</description>
  <releases>
    <release version="3.1.1" date="2026-05-10">Added dual-core H745/H755 updates</release>
  </releases>
  <devices>
    <family Dfamily="STM32H7 Series" Dvendor="STMicroelectronics:13">
      <processor Dcore="Cortex-M7" Dfpu="DP_FPU" Dmpu="MPU" Dclock="480000000"/>
      <subFamily DsubFamily="STM32H743">
        <debug svd="CMSIS/SVD/STM32H743.svd"/>
        <device Dname="STM32H743ZITx">
          <memory name="FLASH_BANK1" access="rx" start="0x08000000" size="0x00100000" startup="1"/>
          <memory name="DTCMRAM"     access="rwx" start="0x20000000" size="0x00020000" startup="0"/>
          <memory name="AXI_SRAM"    access="rwx" start="0x24000000" size="0x00080000" startup="0"/>
        </device>
      </subFamily>
    </family>
  </devices>
  <boards>
    <board vendor="STMicroelectronics" name="NUCLEO-H743ZI2" revision="MB1364-E01">
      <mountedDevice Dname="STM32H743ZITx"/>
    </board>
  </boards>
  <components>
    <component Cclass="Device" Cgroup="Startup" Cversion="1.11.0">
      <description>System Startup for STM32H7xx</description>
    </component>
  </components>
</package>
"""
    (out_dir / "STMicroelectronics.STM32H7xx_DFP.pdsc").write_bytes(pdsc_xml.encode("utf-8"))

def gen_domain_locres(out_dir: _DomainSampleRouter) -> None:
    magic = bytes([
        0x0E, 0x14, 0x74, 0x75, 0x67, 0x4A, 0x03, 0xFC,
        0x4A, 0x15, 0x90, 0x9D, 0xC3, 0x37, 0x7F, 0x1B
    ])
    # Version 2 (Optimized_CRC32): entriesCount=2, 1 namespace "GameUI", 2 keys -> string pool with refCounts
    ns_blob = bytearray()
    ns_blob += struct.pack("<II", 2, 1)  # entriesCount=2, nsCount=1
    ns_blob += struct.pack("<I", 0x11223344) + ue_fstring_utf8("GameUI")
    ns_blob += struct.pack("<I", 2)      # keyCount=2
    ns_blob += struct.pack("<I", 0xA001) + ue_fstring_utf8("BTN_START") + struct.pack("<Ii", 0x99887766, 0)
    ns_blob += struct.pack("<I", 0xA002) + ue_fstring_utf8("BTN_QUIT")  + struct.pack("<Ii", 0x55443322, 1)

    str_off = 25 + len(ns_blob)
    pool = struct.pack("<I", 2)
    pool += ue_fstring_utf8("Start Campaign") + struct.pack("<i", 1)
    pool += ue_fstring_utf8("Exit to Desktop") + struct.pack("<i", 1)

    hdr = magic + struct.pack("<Bq", 2, str_off)
    (out_dir / "ue5_game_en.locres").write_bytes(hdr + bytes(ns_blob) + pool)

def gen_domain_stbl(out_dir: _DomainSampleRouter) -> None:
    # STBL v5 (21B header): [0..4]"STBL", [4]ver=5, [5]comp=0, [6..14]u64 count=2, [14..16]0, [16..20]mnLen, [20]0
    s1 = b"Reticulating Splines..."
    s2 = b"Sul Sul! Welcome Home!"
    entries = (
        struct.pack("<IBH", 0x811C9DC5, 0, len(s1)) + s1 +
        struct.pack("<IBH", 0xCAFEBABE, 0, len(s2)) + s2
    )
    hdr = b"STBL" + struct.pack("<BBQHIB", 5, 0, 2, 0, len(s1) + len(s2), 0)
    (out_dir / "sims4_strings_eng.stbl").write_bytes(hdr + entries)

def gen_domain_bcsv(out_dir: _DomainSampleRouter) -> None:
    # Big-endian BCSV: 2 rows, 3 fields (0: StrPoolOff=6 "ObjName", 1: S32=0 "CoinCount", 2: F32=2 "GravityScale")
    # Header (16B) + 3*12B fields (36B) -> dataOff = 52, rowStride = 12 -> rows = 24B -> strPool at 76
    hdr = struct.pack(">IIII", 2, 3, 52, 12)
    f0 = struct.pack(">IIHBB", 0x5F21A010, 0xFFFFFFFF, 0, 0, 6)  # StrPoolOff @ +0
    f1 = struct.pack(">IIHBB", 0x3B1100C2, 0xFFFFFFFF, 4, 0, 0)  # S32 @ +4
    f2 = struct.pack(">IIHBB", 0x7E881204, 0xFFFFFFFF, 8, 0, 2)  # F32 @ +8
    s_pool = b"StarPieceCluster\x00LaunchStar\x00"
    r0 = struct.pack(">Iif", 0, 5, 1.0)
    r1 = struct.pack(">Iif", 17, 25, 0.75)
    (out_dir / "galaxy_stage_obj.bcsv").write_bytes(hdr + f0 + f1 + f2 + r0 + r1 + s_pool)

def gen_domain_sii(out_dir: _DomainSampleRouter) -> None:
    txt = """SiiNunit
{
accessory_engine_data : dc16_730.scania.s_2016.engine
{
\tname: "Scania DC16 108 730 Euro 6 V8"
\tprice: 29400
\tunlock: 25
\tinfo[]: "730 @@hp@@ (537@@kw@@)"
\ttorque: 3500
\tvolume: 16.4
}
}
"""
    (out_dir / "ets2_truck_engine.sii").write_bytes(txt.encode("utf-8"))

def gen_domain_tmx(out_dir: _DomainSampleRouter) -> None:
    txt = """<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.10.2" orientation="orthogonal" renderorder="right-down" width="4" height="3" tilewidth="16" tileheight="16">
 <tileset firstgid="1" name="overworld_grass" tilewidth="16" tileheight="16" tilecount="64" columns="8">
  <image source="overworld_grass.png" width="128" height="128"/>
 </tileset>
 <layer id="1" name="Ground" width="4" height="3">
  <data encoding="csv">
1,2,2,1,
5,0,0,6,
9,10,10,11
</data>
 </layer>
 <objectgroup id="2" name="Spawns">
  <object id="10" name="HeroStart" class="PlayerSpawn" x="16" y="32" width="16" height="16"/>
  <object id="11" name="TownChest" class="LootChest" x="48" y="16" width="16" height="16"/>
 </objectgroup>
</map>
"""
    (out_dir / "rpg_overworld.tmx").write_bytes(txt.encode("utf-8"))

def gen_domain_ldtk(out_dir: _DomainSampleRouter) -> None:
    txt = """{
  "jsonVersion": "1.5.3",
  "worldLayout": "GridVania",
  "worldGridWidth": 256,
  "worldGridHeight": 192,
  "defs": {
    "layers": [
      { "identifier": "Collisions", "__type": "IntGrid", "gridSize": 16 }
    ],
    "entities": [
      { "identifier": "SaveStatue", "width": 16, "height": 32, "maxCount": 4 }
    ],
    "tilesets": [
      { "identifier": "CavernTiles", "pxWid": 256, "pxHei": 256, "relPath": "atlas/cavern.png" }
    ]
  },
  "levels": [
    {
      "identifier": "Level_0_Entrance",
      "worldX": 0,
      "worldY": 0,
      "pxWid": 512,
      "pxHei": 384,
      "layerInstances": [
        {
          "__identifier": "Entities",
          "entityInstances": [
            { "__identifier": "SaveStatue" }
          ]
        }
      ]
    },
    {
      "identifier": "Level_1_Crypt",
      "worldX": 512,
      "worldY": 0,
      "pxWid": 768,
      "pxHei": 384,
      "layerInstances": []
    }
  ]
}
"""
    (out_dir / "metroidvania_world.ldtk").write_bytes(txt.encode("utf-8"))


def _run_consolidated_domain_samples(td_root: Path) -> None:

    router = _DomainSampleRouter(td_root, {'kotor_lightsabers.2da': 'data', 'kotor_dialog.tlk': 'docs', 'hk47_creature.gff': 'data', 'godot_mod_pl.mo': 'docs', 'kicad_router_de.po': 'docs', 'STMicroelectronics.STM32H7xx_DFP.pdsc': 'docs', 'ue5_game_en.locres': 'docs', 'sims4_strings_eng.stbl': 'docs', 'galaxy_stage_obj.bcsv': 'data', 'ets2_truck_engine.sii': 'docs', 'rpg_overworld.tmx': 'docs', 'metroidvania_world.ldtk': 'docs'})

    for fn in [gen_domain_bioware_2da, gen_domain_bioware_tlk, gen_domain_bioware_gff, gen_domain_gettext_mo, gen_domain_gettext_po, gen_domain_arm_cmsis_pdsc, gen_domain_locres, gen_domain_stbl, gen_domain_bcsv, gen_domain_sii, gen_domain_tmx, gen_domain_ldtk]:

        fn(router)


def main() -> int:
    repo_root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
    td_root = repo_root / "testdata"
    created = []
    gen_data_samples(td_root / "data", created)
    gen_gis_samples(td_root / "gis", created)
    gen_audio_samples(td_root / "audio", created)
    gen_security_samples(td_root / "security", created)
    print(f"Generated {len(created)} sample files:")
    for p in created:
        print(f"  {p.relative_to(repo_root).as_posix()} ({p.stat().st_size} B)")
    return 0
    if len(__import__('sys').argv) == 1:
        _run_consolidated_domain_samples(Path(__file__).resolve().parent.parent / 'testdata')


if __name__ == "__main__":
    raise SystemExit(main())
