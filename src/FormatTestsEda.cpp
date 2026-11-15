// FormatTestsEda.cpp: self-tests of the electronics / PCB / EDA readers (EdaViewer, formats/), stage 15 ("15.eda" runs these alone)
#include "SelfTestCommon.h"
#include "EdaViewer.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

struct Fnv1a64 {
    uint64_t h = 14695981039346656037ULL;

    void AddBytes(const void* ptr, size_t len) {
        const auto* p = static_cast<const uint8_t*>(ptr);
        for (size_t i = 0; i < len; ++i) {
            h ^= static_cast<uint64_t>(p[i]);
            h *= 1099511628211ULL;
        }
    }
    void AddFloat(float f) {
        int32_t q = static_cast<int32_t>(std::llround(static_cast<double>(f) * 1000.0));
        AddBytes(&q, sizeof(q));
    }
    void AddU32(uint32_t v) { AddBytes(&v, sizeof(v)); }
    void AddWStr(const std::wstring& s) {
        uint32_t n = static_cast<uint32_t>(s.size());
        AddU32(n);
        if (n > 0) AddBytes(s.data(), s.size() * sizeof(wchar_t));
    }
};

static uint64_t HashEdaGeometry(const ParsedEdaBoard& b) {
    Fnv1a64 fnv;
    fnv.AddU32(static_cast<uint32_t>(b.traces.size()));
    for (const auto& t : b.traces) {
        fnv.AddFloat(t.x1); fnv.AddFloat(t.y1);
        fnv.AddFloat(t.x2); fnv.AddFloat(t.y2);
        fnv.AddFloat(t.width);
        fnv.AddU32(static_cast<uint32_t>(t.layer));
    }
    fnv.AddU32(static_cast<uint32_t>(b.pads.size()));
    for (const auto& p : b.pads) {
        fnv.AddFloat(p.x); fnv.AddFloat(p.y);
        fnv.AddFloat(p.w); fnv.AddFloat(p.h);
        fnv.AddFloat(p.drill);
        fnv.AddU32(static_cast<uint32_t>(p.layer));
        fnv.AddWStr(p.pinName);
    }
    fnv.AddU32(static_cast<uint32_t>(b.vias.size()));
    for (const auto& v : b.vias) {
        fnv.AddFloat(v.x); fnv.AddFloat(v.y);
        fnv.AddFloat(v.diameter); fnv.AddFloat(v.drill);
    }
    fnv.AddU32(static_cast<uint32_t>(b.circles.size()));
    for (const auto& c : b.circles) {
        fnv.AddFloat(c.cx); fnv.AddFloat(c.cy); fnv.AddFloat(c.radius);
    }
    fnv.AddU32(static_cast<uint32_t>(b.components.size()));
    for (const auto& c : b.components) {
        fnv.AddWStr(c.refDes);
        fnv.AddWStr(c.value);
        fnv.AddWStr(c.footprint);
        fnv.AddFloat(c.x); fnv.AddFloat(c.y);
        fnv.AddFloat(c.rotDeg);
        fnv.AddFloat(c.widthMm); fnv.AddFloat(c.lengthMm);
        fnv.AddU32(c.bottomSide ? 1u : 0u);
    }
    return fnv.h;
}

static uint64_t HashEdaBoardFull(const ParsedEdaBoard& b) {
    Fnv1a64 fnv;
    uint64_t gh = HashEdaGeometry(b);
    fnv.AddBytes(&gh, sizeof(gh));
    fnv.AddWStr(b.formatTitle);
    fnv.AddU32(static_cast<uint32_t>(b.kind));
    fnv.AddU32(static_cast<uint32_t>(b.netCount));
    fnv.AddU32(static_cast<uint32_t>(b.labels.size()));
    for (const auto& l : b.labels) {
        fnv.AddFloat(l.x); fnv.AddFloat(l.y);
        fnv.AddWStr(l.text);
    }
    return fnv.h;
}

static bool HasFakeQfn32(const ParsedEdaBoard& b) {
    for (const auto& c : b.components) {
        if (c.footprint == L"QFN-32") return true;
    }
    return false;
}

static void PutU16LE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}
static void PutU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}
static void PutI32LE(uint8_t* p, int32_t v) {
    PutU32LE(p, static_cast<uint32_t>(v));
}
static void PutU16BE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[1] = static_cast<uint8_t>(v & 0xFFu);
}
static void PutU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[3] = static_cast<uint8_t>(v & 0xFFu);
}
static void PutU64BE(uint8_t* p, uint64_t v) {
    PutU32BE(p, static_cast<uint32_t>(v >> 32));
    PutU32BE(p + 4, static_cast<uint32_t>(v & 0xFFFFFFFFULL));
}

static void AppendULeb128(std::vector<uint8_t>& out, uint64_t val) {
    do {
        uint8_t b = static_cast<uint8_t>(val & 0x7Fu);
        val >>= 7;
        if (val != 0) b |= 0x80u;
        out.push_back(b);
    } while (val != 0);
}

static void AppendSLeb128(std::vector<uint8_t>& out, int64_t val) {
    bool more = true;
    while (more) {
        uint8_t b = static_cast<uint8_t>(val & 0x7F);
        val >>= 7;
        if ((val == 0 && (b & 0x40) == 0) || (val == -1 && (b & 0x40) != 0)) {
            more = false;
        } else {
            b |= 0x80u;
        }
        out.push_back(b);
    }
}

static uint32_t CalcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

// Wraps raw payload in RFC 1950 zlib with stored (BTYPE=00) deflate blocks + Adler32
static std::vector<uint8_t> WrapZlibStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z = { 0x78, 0x01 };
    size_t pos = 0;
    do {
        size_t take = std::min<size_t>(raw.size() - pos, 65535);
        bool last = (pos + take >= raw.size());
        z.push_back(last ? 0x01u : 0x00u);
        uint16_t len = static_cast<uint16_t>(take);
        uint16_t nlen = static_cast<uint16_t>(~len);
        z.push_back(static_cast<uint8_t>(len & 0xFFu));
        z.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
        z.push_back(static_cast<uint8_t>(nlen & 0xFFu));
        z.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFFu));
        z.insert(z.end(), raw.begin() + static_cast<ptrdiff_t>(pos), raw.begin() + static_cast<ptrdiff_t>(pos + take));
        pos += take;
    } while (pos < raw.size());

    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) {
        s1 = (s1 + b) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    uint32_t adler = (s2 << 16) | s1;
    uint8_t tr[4] = {};
    PutU32BE(tr, adler);
    z.insert(z.end(), tr, tr + 4);
    return z;
}

// Wraps raw payload in RFC 1952 gzip with stored deflate blocks + CRC32 + ISIZE
static std::vector<uint8_t> WrapGzipStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> gz = { 0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0x03 };
    size_t pos = 0;
    do {
        size_t take = std::min<size_t>(raw.size() - pos, 65535);
        bool last = (pos + take >= raw.size());
        gz.push_back(last ? 0x01u : 0x00u);
        uint16_t len = static_cast<uint16_t>(take);
        uint16_t nlen = static_cast<uint16_t>(~len);
        gz.push_back(static_cast<uint8_t>(len & 0xFFu));
        gz.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
        gz.push_back(static_cast<uint8_t>(nlen & 0xFFu));
        gz.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFFu));
        gz.insert(gz.end(), raw.begin() + static_cast<ptrdiff_t>(pos), raw.begin() + static_cast<ptrdiff_t>(pos + take));
        pos += take;
    } while (pos < raw.size());

    uint8_t tail[8] = {};
    PutU32LE(tail, CalcCrc32(raw.data(), raw.size()));
    PutU32LE(tail + 4, static_cast<uint32_t>(raw.size()));
    gz.insert(gz.end(), tail, tail + 8);
    return gz;
}

// Builds an uncompressed Stored ZIP archive in memory
static std::vector<uint8_t> BuildStoredZip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<uint8_t> out;
    struct CdRec {
        std::string name;
        uint32_t crc = 0;
        uint32_t sz = 0;
        uint32_t locOff = 0;
    };
    std::vector<CdRec> cds;
    for (const auto& f : files) {
        CdRec cd;
        cd.name = f.first;
        cd.sz = static_cast<uint32_t>(f.second.size());
        cd.crc = CalcCrc32(reinterpret_cast<const uint8_t*>(f.second.data()), f.second.size());
        cd.locOff = static_cast<uint32_t>(out.size());

        uint8_t lhdr[30] = {};
        PutU32LE(lhdr + 0, 0x04034B50u);
        PutU16LE(lhdr + 4, 20);
        PutU32LE(lhdr + 14, cd.crc);
        PutU32LE(lhdr + 18, cd.sz);
        PutU32LE(lhdr + 22, cd.sz);
        PutU16LE(lhdr + 26, static_cast<uint16_t>(cd.name.size()));
        out.insert(out.end(), lhdr, lhdr + 30);
        out.insert(out.end(), cd.name.begin(), cd.name.end());
        out.insert(out.end(), f.second.begin(), f.second.end());
        cds.push_back(std::move(cd));
    }

    uint32_t cdStart = static_cast<uint32_t>(out.size());
    for (const auto& cd : cds) {
        uint8_t chdr[46] = {};
        PutU32LE(chdr + 0, 0x02014B50u);
        PutU16LE(chdr + 4, 20);
        PutU16LE(chdr + 6, 20);
        PutU32LE(chdr + 16, cd.crc);
        PutU32LE(chdr + 20, cd.sz);
        PutU32LE(chdr + 24, cd.sz);
        PutU16LE(chdr + 28, static_cast<uint16_t>(cd.name.size()));
        PutU32LE(chdr + 42, cd.locOff);
        out.insert(out.end(), chdr, chdr + 46);
        out.insert(out.end(), cd.name.begin(), cd.name.end());
    }
    uint32_t cdSize = static_cast<uint32_t>(out.size()) - cdStart;
    uint8_t eocd[22] = {};
    PutU32LE(eocd + 0, 0x06054B50u);
    PutU16LE(eocd + 8, static_cast<uint16_t>(cds.size()));
    PutU16LE(eocd + 10, static_cast<uint16_t>(cds.size()));
    PutU32LE(eocd + 12, cdSize);
    PutU32LE(eocd + 16, cdStart);
    out.insert(out.end(), eocd, eocd + 22);
    return out;
}

// Builds a POSIX ustar TAR archive in memory
static std::vector<uint8_t> BuildUstarTar(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<uint8_t> tar;
    for (const auto& f : files) {
        uint8_t hdr[512] = {};
        std::string name = f.first;
        if (name.size() <= 99) {
            std::memcpy(hdr, name.data(), name.size());
        } else {
            size_t slash = name.rfind('/', 154);
            std::string pfx = name.substr(0, slash);
            std::string leaf = name.substr(slash + 1);
            std::memcpy(hdr, leaf.data(), std::min<size_t>(leaf.size(), 99));
            std::memcpy(hdr + 345, pfx.data(), std::min<size_t>(pfx.size(), 154));
        }
        std::memcpy(hdr + 100, "0000644\0", 8);
        std::memcpy(hdr + 108, "0000000\0", 8);
        std::memcpy(hdr + 116, "0000000\0", 8);
        char szOct[16] = {};
        sprintf_s(szOct, "%011zo", f.second.size());
        std::memcpy(hdr + 124, szOct, 11);
        std::memcpy(hdr + 136, "14600000000\0", 12);
        std::memset(hdr + 148, ' ', 8);
        hdr[156] = '0';
        std::memcpy(hdr + 257, "ustar\00000", 8);

        uint32_t chk = 0;
        for (int i = 0; i < 512; ++i) chk += hdr[i];
        char chkOct[16] = {};
        sprintf_s(chkOct, "%06o", chk);
        std::memcpy(hdr + 148, chkOct, 6);
        hdr[154] = 0;
        hdr[155] = ' ';

        tar.insert(tar.end(), hdr, hdr + 512);
        tar.insert(tar.end(), f.second.begin(), f.second.end());
        size_t rem = f.second.size() % 512;
        if (rem != 0) tar.insert(tar.end(), 512 - rem, 0);
    }
    tar.insert(tar.end(), 1024, 0);
    return tar;
}

// Builds a spec-compliant MS-CFB v3 (512B sector) OLE2 Compound File in memory.
// Supports both root-level streams (e.g. "FileHeader") and 1-level storage/stream paths (e.g. "Board6/Data").
static std::vector<uint8_t> BuildOle2CompoundFile(const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& streams) {
    struct Node {
        std::wstring name;
        uint8_t objType = 2; // 5=Root, 1=Storage, 2=Stream
        uint32_t left = 0xFFFFFFFFu, right = 0xFFFFFFFFu, child = 0xFFFFFFFFu;
        uint32_t startSec = 0xFFFFFFFEu;
        uint32_t byteSize = 0;
        std::vector<uint8_t> payload;
    };
    std::vector<Node> nodes(1);
    nodes[0].name = L"Root Entry";
    nodes[0].objType = 5;

    std::vector<uint32_t> rootChildren;
    auto findStorage = [&](const std::wstring& stgName) -> int {
        for (size_t i = 1; i < nodes.size(); ++i) {
            if (nodes[i].objType == 1 && nodes[i].name == stgName) return static_cast<int>(i);
        }
        return -1;
    };

    std::vector<std::vector<uint32_t>> stgChildren(64);
    for (const auto& s : streams) {
        size_t slash = s.first.find(L'/');
        if (slash == std::wstring::npos) {
            uint32_t id = static_cast<uint32_t>(nodes.size());
            Node n;
            n.name = s.first;
            n.objType = 2;
            n.payload = s.second;
            n.byteSize = static_cast<uint32_t>(s.second.size());
            nodes.push_back(std::move(n));
            rootChildren.push_back(id);
        } else {
            std::wstring stg = s.first.substr(0, slash);
            std::wstring leaf = s.first.substr(slash + 1);
            int stgId = findStorage(stg);
            if (stgId < 0) {
                stgId = static_cast<int>(nodes.size());
                Node sn;
                sn.name = stg;
                sn.objType = 1;
                nodes.push_back(std::move(sn));
                rootChildren.push_back(static_cast<uint32_t>(stgId));
            }
            uint32_t leafId = static_cast<uint32_t>(nodes.size());
            Node ln;
            ln.name = leaf;
            ln.objType = 2;
            ln.payload = s.second;
            ln.byteSize = static_cast<uint32_t>(s.second.size());
            nodes.push_back(std::move(ln));
            stgChildren[stgId].push_back(leafId);
        }
    }

    auto linkSiblings = [&](const std::vector<uint32_t>& ids) -> uint32_t {
        if (ids.empty()) return 0xFFFFFFFFu;
        for (size_t i = 0; i + 1 < ids.size(); ++i) {
            nodes[ids[i]].right = ids[i + 1];
        }
        return ids[0];
    };
    nodes[0].child = linkSiblings(rootChildren);
    for (size_t i = 1; i < nodes.size(); ++i) {
        if (nodes[i].objType == 1) {
            nodes[i].child = linkSiblings(stgChildren[i]);
        }
    }

    // Sector layout:
    // Sector 0: FAT (up to 128 sectors)
    // Sector 1..dirSecs: Directory entries (4 per 512B sector)
    // Subsequent sectors: Stream payloads
    uint32_t dirSecs = static_cast<uint32_t>((nodes.size() + 3) / 4);
    uint32_t nextSec = 1 + dirSecs;
    std::vector<uint32_t> fat(128, 0xFFFFFFFFu);
    fat[0] = 0xFFFFFFFDu; // FATSECT
    for (uint32_t d = 0; d < dirSecs; ++d) {
        fat[1 + d] = (d + 1 < dirSecs) ? (2 + d) : 0xFFFFFFFEu;
    }

    std::vector<uint8_t> streamData;
    for (auto& n : nodes) {
        if (n.objType != 2 || n.byteSize == 0) continue;
        n.startSec = nextSec;
        uint32_t secsNeeded = (n.byteSize + 511u) / 512u;
        for (uint32_t s = 0; s < secsNeeded; ++s) {
            uint32_t cur = nextSec + s;
            if (cur >= fat.size()) fat.resize(cur + 128, 0xFFFFFFFFu);
            fat[cur] = (s + 1 < secsNeeded) ? (cur + 1) : 0xFFFFFFFEu;
        }
        nextSec += secsNeeded;
        size_t oldSz = streamData.size();
        streamData.resize(oldSz + secsNeeded * 512u, 0);
        std::memcpy(streamData.data() + oldSz, n.payload.data(), n.payload.size());
    }

    std::vector<uint8_t> file(512 + (1 + dirSecs) * 512 + streamData.size(), 0);
    // 512B Header
    static const uint8_t kOleMagic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    std::memcpy(file.data(), kOleMagic, 8);
    PutU16LE(file.data() + 24, 0x003Eu); // minorVer
    PutU16LE(file.data() + 26, 0x0003u); // majorVer = 3 (512B sectors)
    PutU16LE(file.data() + 28, 0xFFFEu); // byteOrder LE
    PutU16LE(file.data() + 30, 9);       // sectorShift = 9 (512)
    PutU16LE(file.data() + 32, 6);       // miniSectorShift = 6 (64)
    PutU32LE(file.data() + 44, 1);       // numFatSectors = 1
    PutU32LE(file.data() + 48, 1);       // firstDirSector = 1
    PutU32LE(file.data() + 56, 4096);    // miniStreamCutoff (Root Entry has size=0 so ExtractStream uses FAT directly)
    PutU32LE(file.data() + 60, 0xFFFFFFFEu); // firstMiniFat
    PutU32LE(file.data() + 68, 0xFFFFFFFEu); // firstDifat
    PutU32LE(file.data() + 76, 0);       // DIFAT[0] = Sector 0
    for (int i = 1; i < 109; ++i) PutU32LE(file.data() + 76 + i * 4, 0xFFFFFFFFu);

    // Sector 0: FAT
    for (size_t i = 0; i < 128; ++i) {
        PutU32LE(file.data() + 512 + i * 4, fat[i]);
    }

    // Sector 1..dirSecs: Directory Entries
    uint8_t* dirBase = file.data() + 1024;
    for (size_t i = 0; i < dirSecs * 4; ++i) {
        uint8_t* ent = dirBase + i * 128;
        PutU32LE(ent + 68, 0xFFFFFFFFu);
        PutU32LE(ent + 72, 0xFFFFFFFFu);
        PutU32LE(ent + 76, 0xFFFFFFFFu);
        if (i >= nodes.size()) continue;
        const Node& n = nodes[i];
        size_t wlen = std::min<size_t>(n.name.size(), 31);
        for (size_t c = 0; c < wlen; ++c) {
            PutU16LE(ent + c * 2, static_cast<uint16_t>(n.name[c]));
        }
        PutU16LE(ent + 64, static_cast<uint16_t>((wlen + 1) * 2));
        ent[66] = n.objType;
        ent[67] = 1; // black node
        PutU32LE(ent + 68, n.left);
        PutU32LE(ent + 72, n.right);
        PutU32LE(ent + 76, n.child);
        PutU32LE(ent + 116, n.startSec);
        PutU32LE(ent + 120, n.byteSize);
    }

    // Stream data sectors
    if (!streamData.empty()) {
        std::memcpy(file.data() + 1024 + dirSecs * 512, streamData.data(), streamData.size());
    }
    return file;
}

static void AppendAltiumLenBlock(std::vector<uint8_t>& stream, const std::string& pipeTxt) {
    uint32_t len = static_cast<uint32_t>(pipeTxt.size() + 1);
    uint8_t hdr[4] = {};
    PutU32LE(hdr, len & 0x00FFFFFFu);
    stream.insert(stream.end(), hdr, hdr + 4);
    stream.insert(stream.end(), pipeTxt.begin(), pipeTxt.end());
    stream.push_back(0);
}

static void AppendEagleRec(std::vector<uint8_t>& buf, const uint8_t rec[24]) {
    buf.insert(buf.end(), rec, rec + 24);
}

static std::vector<uint8_t> BuildEagleV5Board(bool variantB) {
    std::vector<uint8_t> buf(24, 0); // placeholder for 0x10 header

    // 0x16 Board header
    { uint8_t r[24] = { 0x16 }; AppendEagleRec(buf, r); }
    // 0x13 Layers: 1=Top, 16=Bottom, 20=Dimension
    {
        uint8_t l1[24] = { 0x13, 0, 0, 1 };  std::memcpy(l1 + 15, "Top", 3);       AppendEagleRec(buf, l1);
        uint8_t l2[24] = { 0x13, 0, 0, 16 }; std::memcpy(l2 + 15, "Bottom", 6);    AppendEagleRec(buf, l2);
        uint8_t l3[24] = { 0x13, 0, 0, 20 }; std::memcpy(l3 + 15, "Dimension", 9); AppendEagleRec(buf, l3);
    }

    // 0x15 Library with 1 package (0x19 + 2x 0x2B SMD pads)
    {
        uint8_t lib[24] = { 0x15 };
        PutU32LE(lib + 4, 3); // 1 package + 2 child SMD pads
        AppendEagleRec(buf, lib);

        uint8_t pkg[24] = { 0x19 };
        PutU16LE(pkg + 2, 2); // 2 children
        std::memcpy(pkg + 16, "SOIC8", 5);
        AppendEagleRec(buf, pkg);

        uint8_t smd1[24] = { 0x2B, 0, 0, 1 };
        PutI32LE(smd1 + 4, -12000); PutI32LE(smd1 + 8, 0);
        PutI32LE(smd1 + 12, 8000);  PutI32LE(smd1 + 16, 14000);
        smd1[19] = '1';
        AppendEagleRec(buf, smd1);

        uint8_t smd2[24] = { 0x2B, 0, 0, 1 };
        PutI32LE(smd2 + 4, 12000);  PutI32LE(smd2 + 8, 0);
        PutI32LE(smd2 + 12, 8000);  PutI32LE(smd2 + 16, 14000);
        smd2[19] = '2';
        AppendEagleRec(buf, smd2);
    }

    // Board outline rectangle on layer 20 (0.1 um units: 40x30mm vs 75x50mm)
    {
        uint8_t rect[24] = { 0x20, 0, 0, 20 };
        PutI32LE(rect + 4, 0);
        PutI32LE(rect + 8, 0);
        PutI32LE(rect + 12, variantB ? 750000 : 400000);
        PutI32LE(rect + 16, variantB ? 500000 : 300000);
        AppendEagleRec(buf, rect);
    }

    // Mounting hole circle (0x22)
    {
        uint8_t cir[24] = { 0x22, 0, 0, 20 };
        PutI32LE(cir + 4, 35000);
        PutI32LE(cir + 8, 35000);
        PutI32LE(cir + 12, variantB ? 18000 : 15000);
        PutU16LE(cir + 20, 1000);
        AppendEagleRec(buf, cir);
    }

    // Elements (0x30 + 0x33): 2 parts in A, 4 parts in B
    struct ElemSpec { const char* ref; const char* val; int32_t x; int32_t y; uint16_t rot; };
    std::vector<ElemSpec> elems = variantB
        ? std::vector<ElemSpec>{
            { "U1", "STM32F4", 180000, 250000, 0 },
            { "U2", "W25Q128", 420000, 260000, 1024 },
            { "C1", "100nF",   280000, 140000, 0 },
            { "J1", "USB_C",   680000, 250000, 2048 }
        }
        : std::vector<ElemSpec>{
            { "U1", "ATMEGA328", 150000, 150000, 0 },
            { "R1", "10k",       290000, 150000, 1024 }
        };

    for (const auto& es : elems) {
        uint8_t el[24] = { 0x30 };
        PutU16LE(el + 2, 1);
        PutU16LE(el + 4, 1); // libId = 1
        PutU16LE(el + 6, 1); // pkgId = 1
        PutI32LE(el + 8, es.x);
        PutI32LE(el + 12, es.y);
        PutU16LE(el + 16, es.rot);
        AppendEagleRec(buf, el);

        uint8_t el2[24] = { 0x33 };
        std::memcpy(el2 + 2, es.ref, std::min<size_t>(std::strlen(es.ref), 8));
        std::memcpy(el2 + 10, es.val, std::min<size_t>(std::strlen(es.val), 14));
        AppendEagleRec(buf, el2);
    }

    // Signals (0x19) + Copper Tracks (0x1F) + Vias (0x28)
    int numTracks = variantB ? 5 : 2;
    for (int ti = 0; ti < numTracks; ++ti) {
        uint8_t sig[24] = { 0x19 };
        std::string sn = "NET" + std::to_string(ti + 1);
        std::memcpy(sig + 8, sn.data(), sn.size());
        AppendEagleRec(buf, sig);

        uint8_t trk[24] = { 0x1F, 0, 0, static_cast<uint8_t>((ti % 2 == 0) ? 1 : 16) };
        PutI32LE(trk + 4,  50000 + ti * 80000);
        PutI32LE(trk + 8,  60000 + ti * 30000);
        PutI32LE(trk + 12, 160000 + ti * 95000);
        PutI32LE(trk + 16, 150000 + ti * 40000);
        PutU16LE(trk + 20, static_cast<uint16_t>(variantB ? 1500 : 1250)); // half-width in 0.1 um -> 0.30 / 0.25 mm
        AppendEagleRec(buf, trk);

        uint8_t via[24] = { 0x28 };
        PutI32LE(via + 4, 160000 + ti * 95000);
        PutI32LE(via + 8, 150000 + ti * 40000);
        PutI32LE(via + 12, 3500); // 0.35 mm drill
        PutI32LE(via + 16, 7500); // 0.75 mm outer diam
        AppendEagleRec(buf, via);
    }

    // Fill 0x10 header with exact block count and Eagle v5.11 version
    buf[0] = 0x10;
    PutU32LE(buf.data() + 4, static_cast<uint32_t>(buf.size() / 24));
    buf[8] = 5;
    buf[9] = 11;
    return buf;
}

static std::vector<uint8_t> BuildEagleV5Schematic(bool variantB) {
    std::vector<uint8_t> buf(24, 0);
    { uint8_t r[24] = { 0x14 }; AppendEagleRec(buf, r); } // Schematic header

    int nInst = variantB ? 3 : 1;
    for (int i = 0; i < nInst; ++i) {
        uint8_t inst[24] = { 0x38 };
        PutU16LE(inst + 2, 1);
        PutU16LE(inst + 4, static_cast<uint16_t>(i + 1));
        PutI32LE(inst + 8,  100000 + i * 220000);
        PutI32LE(inst + 12, 120000 + (variantB ? i * 80000 : 0));
        std::string nm = "IC" + std::to_string(i + 1);
        std::memcpy(inst + 18, nm.data(), nm.size());
        AppendEagleRec(buf, inst);

        uint8_t wire[24] = { 0x1F, 0, 0, 91 }; // Layer 91 = Nets
        PutI32LE(wire + 4,  130000 + i * 220000);
        PutI32LE(wire + 8,  120000 + (variantB ? i * 80000 : 0));
        PutI32LE(wire + 12, 240000 + i * 220000);
        PutI32LE(wire + 16, 180000 + (variantB ? i * 80000 : 0));
        PutU16LE(wire + 20, 800);
        AppendEagleRec(buf, wire);
    }

    buf[0] = 0x10;
    PutU32LE(buf.data() + 4, static_cast<uint32_t>(buf.size() / 24));
    buf[8] = 5;
    buf[9] = 12;
    return buf;
}

static std::vector<uint8_t> BuildVectorBlfFile(bool variantB) {
    // Build inner uncompressed LOBJ CAN_MESSAGE (type 1) records, then compress inside LOG_CONTAINER (type 10, zlib=2)
    std::vector<uint8_t> inner;
    auto addCanMsg = [&](uint64_t tsNs, uint16_t ch, uint32_t canId, const std::vector<uint8_t>& payload) {
        uint8_t rec[48] = {};
        std::memcpy(rec, "LOBJ", 4);
        PutU16LE(rec + 4, 32); // ObjectHeader v1 = 32B
        PutU16LE(rec + 6, 1);
        PutU32LE(rec + 8, 48); // 32B header + 16B CAN_MESSAGE payload
        PutU32LE(rec + 12, 1); // BL_OBJ_TYPE_CAN_MESSAGE
        PutU32LE(rec + 16, 2); // flags = 2 (1 ns timestamps)
        PutU64BE(rec + 24, 0);
        // Little-endian 64-bit timestamp at +24
        PutU32LE(rec + 24, static_cast<uint32_t>(tsNs & 0xFFFFFFFFULL));
        PutU32LE(rec + 28, static_cast<uint32_t>(tsNs >> 32));

        PutU16LE(rec + 32, ch);
        rec[34] = 0x01; // Tx
        rec[35] = static_cast<uint8_t>(std::min<size_t>(payload.size(), 8));
        PutU32LE(rec + 36, canId);
        for (size_t i = 0; i < std::min<size_t>(payload.size(), 8); ++i) {
            rec[40 + i] = payload[i];
        }
        inner.insert(inner.end(), rec, rec + 48);
    };

    if (!variantB) {
        addCanMsg(1000000ULL, 1, 0x120u, { 0x10, 0x80, 0xFF, 0x00 });
        addCanMsg(5000000ULL, 1, 0x120u, { 0x40, 0x20, 0x10, 0xF0 });
    } else {
        addCanMsg(1000000ULL,  1, 0x100u, { 0xAA, 0x55, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 });
        addCanMsg(4000000ULL,  1, 0x245u, { 0xDE, 0xAD, 0xBE, 0xEF });
        addCanMsg(9000000ULL,  2, 0x3A0u, { 0x00, 0x7F, 0xFF, 0x80, 0x12 });
        addCanMsg(14000000ULL, 1, 0x100u, { 0x11, 0x22, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x99 });
    }

    std::vector<uint8_t> zInner = WrapZlibStored(inner);

    // LOG_CONTAINER LOBJ: 16B base LOBJ header + 16B container header + zInner
    uint32_t contObjSize = static_cast<uint32_t>(16 + 16 + zInner.size());
    std::vector<uint8_t> cont(32, 0);
    std::memcpy(cont.data(), "LOBJ", 4);
    PutU16LE(cont.data() + 4, 16); // base header size = 16
    PutU16LE(cont.data() + 6, 1);
    PutU32LE(cont.data() + 8, contObjSize);
    PutU32LE(cont.data() + 12, 10); // BL_OBJ_TYPE_LOG_CONTAINER
    PutU16LE(cont.data() + 16, 2);  // compressionMethod = 2 (zlib)
    PutU32LE(cont.data() + 24, static_cast<uint32_t>(inner.size()));
    cont.insert(cont.end(), zInner.begin(), zInner.end());
    while (cont.size() % 4 != 0) cont.push_back(0);

    // 144-byte LOGG file header
    std::vector<uint8_t> blf(144, 0);
    std::memcpy(blf.data(), "LOGG", 4);
    PutU32LE(blf.data() + 4, 144);
    blf[8] = 5; // API version
    PutU32LE(blf.data() + 16, static_cast<uint32_t>(144 + cont.size()));
    PutU32LE(blf.data() + 24, static_cast<uint32_t>(144 + inner.size()));
    PutU32LE(blf.data() + 32, variantB ? 4u : 2u); // objectCount
    blf.insert(blf.end(), cont.begin(), cont.end());
    return blf;
}

static std::vector<uint8_t> BuildGtkWaveFstFile(bool variantB) {
    std::vector<uint8_t> fst;

    auto emitBlock = [&](uint8_t blkType, const std::vector<uint8_t>& pay) {
        fst.push_back(blkType);
        uint8_t lenBe[8] = {};
        PutU64BE(lenBe, static_cast<uint64_t>(pay.size() + 8));
        fst.insert(fst.end(), lenBe, lenBe + 8);
        fst.insert(fst.end(), pay.begin(), pay.end());
    };

    // 1. FST_BL_HDR (type 0, 329-byte standard payload)
    {
        std::vector<uint8_t> hdr(329, 0);
        PutU64BE(hdr.data() + 0, 0);
        PutU64BE(hdr.data() + 8, variantB ? 400ULL : 150ULL);
        double e = 2.718281828459045;
        std::memcpy(hdr.data() + 16, &e, 8);
        PutU64BE(hdr.data() + 32, 1); // num_scopes
        PutU64BE(hdr.data() + 40, variantB ? 3 : 2); // num_vars
        PutU64BE(hdr.data() + 48, variantB ? 3 : 2); // maxhandle
        PutU64BE(hdr.data() + 56, 1); // num_vc_blocks
        hdr[64] = static_cast<uint8_t>(static_cast<int8_t>(-9)); // 1ns
        const char* wr = variantB ? "Verilator FST" : "Icarus FST";
        std::memcpy(hdr.data() + 65, wr, std::strlen(wr));
        emitBlock(0, hdr);
    }

    // 2. FST_BL_HIER (type 4) or FST_BL_HIER_GZ (type 5)
    {
        std::vector<uint8_t> hier;
        // Scope "tb"
        hier.push_back(254); hier.push_back(0);
        hier.insert(hier.end(), { 't','b',0, 0 });
        auto addVar = [&](const char* nm, uint32_t bits) {
            hier.push_back(13); // FST_VT_VCD_WIRE
            hier.push_back(0);  // varDir
            hier.insert(hier.end(), nm, nm + std::strlen(nm) + 1);
            AppendULeb128(hier, bits);
            AppendULeb128(hier, 0); // alias = 0 -> allocates new handle
        };
        addVar("clk", 1);
        addVar("rst_n", 1);
        if (variantB) addVar("valid", 1);
        hier.push_back(255); // upscope

        if (variantB) {
            std::vector<uint8_t> gz = WrapGzipStored(hier);
            std::vector<uint8_t> hgz(8, 0);
            PutU64BE(hgz.data(), hier.size());
            hgz.insert(hgz.end(), gz.begin(), gz.end());
            emitBlock(5, hgz);
        } else {
            emitBlock(4, hier);
        }
    }

    // 3. FST_BL_VCDATA (type 1)
    {
        std::vector<uint8_t> vc(24, 0);
        PutU64BE(vc.data() + 0, 0);
        PutU64BE(vc.data() + 8, variantB ? 400ULL : 150ULL);

        // Initial bits: "00" vs "101"
        std::string initStr = variantB ? "101" : "00";
        AppendULeb128(vc, initStr.size());
        AppendULeb128(vc, initStr.size());
        vc.insert(vc.end(), initStr.begin(), initStr.end());

        size_t nHandles = variantB ? 3 : 2;
        AppendULeb128(vc, nHandles);
        vc.push_back('!'); // uncompressed waves (uncompWaveLen == 0)

        // Build per-handle wave chunks:
        // 1-bit encoding: vli = (deltaTimeIdx << 2) | (bitVal << 1) | 0
        auto makeWave1Bit = [](const std::vector<std::pair<uint64_t, int>>& steps) {
            std::vector<uint8_t> w;
            AppendULeb128(w, 0); // uncompWaveLen = 0 (raw)
            for (const auto& st : steps) {
                uint64_t vli = (st.first << 2) | (static_cast<uint64_t>(st.second & 1) << 1);
                AppendULeb128(w, vli);
            }
            return w;
        };

        std::vector<std::vector<uint8_t>> chunks;
        if (!variantB) {
            chunks.push_back(makeWave1Bit({ {1, 1}, {1, 0}, {1, 1} }));
            chunks.push_back(makeWave1Bit({ {2, 1} }));
        } else {
            chunks.push_back(makeWave1Bit({ {1, 0}, {1, 1}, {1, 0}, {1, 1}, {1, 0} }));
            chunks.push_back(makeWave1Bit({ {1, 1}, {3, 0} }));
            chunks.push_back(makeWave1Bit({ {2, 0}, {1, 1}, {2, 0} }));
        }

        std::vector<uint8_t> posTbl;
        for (const auto& c : chunks) {
            vc.insert(vc.end(), c.begin(), c.end());
            AppendULeb128(posTbl, c.size());
        }
        vc.insert(vc.end(), posTbl.begin(), posTbl.end());
        uint8_t ptLenBe[8] = {};
        PutU64BE(ptLenBe, posTbl.size());
        vc.insert(vc.end(), ptLenBe, ptLenBe + 8);

        // Time table deltas
        std::vector<uint64_t> tDeltas = variantB
            ? std::vector<uint64_t>{ 0, 50, 50, 100, 100, 100 }
            : std::vector<uint64_t>{ 0, 50, 50, 50 };
        std::vector<uint8_t> timeRaw;
        for (uint64_t td : tDeltas) AppendULeb128(timeRaw, td);

        vc.insert(vc.end(), timeRaw.begin(), timeRaw.end());
        uint8_t trl[24] = {};
        PutU64BE(trl + 0, timeRaw.size());
        PutU64BE(trl + 8, timeRaw.size());
        PutU64BE(trl + 16, tDeltas.size());
        vc.insert(vc.end(), trl, trl + 24);
        emitBlock(1, vc);
    }

    return fst;
}

static std::vector<uint8_t> BuildGhdlGhwFile(bool variantB) {
    std::vector<uint8_t> ghw;
    // 16-byte GHW header
    const uint8_t hdr[16] = { 'G','H','D','L','w','a','v','e','\n', 16, 0, 1, 1, 1, 1, 0 };
    ghw.insert(ghw.end(), hdr, hdr + 16);

    // 1. STR\0 section (strings: 1="'0'", 2="'1'", 3="bit", 4="top", 5="clk", 6="data", [7="irq"])
    std::vector<std::string> strs = { "'0'", "'1'", "bit", "top", "clk", "data" };
    if (variantB) strs.push_back("irq");
    {
        uint8_t sh[16] = { 'S','T','R',0, 0,0,0,0 };
        PutU32LE(sh + 8, static_cast<uint32_t>(strs.size()));
        uint32_t totChars = 0;
        for (const auto& s : strs) totChars += static_cast<uint32_t>(s.size() + 1);
        PutU32LE(sh + 12, totChars);
        ghw.insert(ghw.end(), sh, sh + 16);
        for (const auto& s : strs) {
            ghw.insert(ghw.end(), s.begin(), s.end());
            ghw.push_back(0); // prev_len = 0 for next string
        }
        ghw.insert(ghw.end(), { 'E','O','S',0 });
    }

    // 2. TYP\0 section (type 1 = ghw_rtik_type_b2 = 22: name=3, lit0=1 ('0'), lit1=2 ('1'))
    {
        uint8_t th[12] = { 'T','Y','P',0, 0,0,0,0 };
        PutU32LE(th + 8, 1);
        ghw.insert(ghw.end(), th, th + 12);
        ghw.push_back(22); // ghw_rtik_type_b2
        AppendULeb128(ghw, 3); // "bit"
        AppendULeb128(ghw, 1); // "'0'"
        AppendULeb128(ghw, 2); // "'1'"
        ghw.insert(ghw.end(), { 'E','O','S',0 });
    }

    // 3. HIE\0 section
    uint32_t nSigs = variantB ? 3u : 2u;
    {
        uint8_t hh[20] = { 'H','I','E',0, 0,0,0,0 };
        PutU32LE(hh + 8, 1);
        PutU32LE(hh + 12, nSigs);
        PutU32LE(hh + 16, nSigs);
        ghw.insert(ghw.end(), hh, hh + 20);
        ghw.push_back(1); // design scope
        AppendULeb128(ghw, 4); // "top"
        for (uint32_t i = 0; i < nSigs; ++i) {
            ghw.push_back(16); // ghw_hie_signal
            AppendULeb128(ghw, 5 + i); // "clk", "data", ["irq"]
            AppendULeb128(ghw, 1);     // typeId = 1
            AppendULeb128(ghw, 1 + i); // basicSigId = 1..nSigs
        }
        ghw.push_back(15); // upscope
        ghw.push_back(0);  // eoh
        ghw.insert(ghw.end(), { 'E','O','S',0 });
    }

    // 4. SNP\0 section (initial values at t = 0 fs)
    {
        uint8_t sn[16] = { 'S','N','P',0, 0,0,0,0 };
        ghw.insert(ghw.end(), sn, sn + 16);
        ghw.push_back(0);
        ghw.push_back(variantB ? 1 : 0);
        if (variantB) ghw.push_back(0);
        ghw.insert(ghw.end(), { 'E','S','N',0 });
    }

    // 5. CYC\0 section (transitions at 10ns, 20ns, ...)
    {
        uint8_t cy[12] = { 'C','Y','C',0 };
        uint64_t tStartFs = 10000000ULL; // 10 ns
        PutU32LE(cy + 4, static_cast<uint32_t>(tStartFs & 0xFFFFFFFFULL));
        PutU32LE(cy + 8, static_cast<uint32_t>(tStartFs >> 32));
        ghw.insert(ghw.end(), cy, cy + 12);

        if (!variantB) {
            // t=10ns: sig 1 -> '1', sig 2 -> '1'
            AppendULeb128(ghw, 1); ghw.push_back(1);
            AppendULeb128(ghw, 1); ghw.push_back(1);
            ghw.push_back(0);
            AppendSLeb128(ghw, 10000000LL); // +10ns -> 20ns
            // t=20ns: sig 1 -> '0'
            AppendULeb128(ghw, 1); ghw.push_back(0);
            ghw.push_back(0);
            AppendSLeb128(ghw, -1); // end CYC
        } else {
            // t=10ns: sig 1 -> '1', sig 3 -> '1'
            AppendULeb128(ghw, 1); ghw.push_back(1);
            AppendULeb128(ghw, 2); ghw.push_back(1);
            ghw.push_back(0);
            AppendSLeb128(ghw, 15000000LL); // +15ns -> 25ns
            // t=25ns: sig 1 -> '0', sig 2 -> '0'
            AppendULeb128(ghw, 1); ghw.push_back(0);
            AppendULeb128(ghw, 1); ghw.push_back(0);
            ghw.push_back(0);
            AppendSLeb128(ghw, 20000000LL); // +20ns -> 45ns
            // t=45ns: sig 1 -> '1', sig 2 -> '1', sig 3 -> '0'
            AppendULeb128(ghw, 1); ghw.push_back(1);
            AppendULeb128(ghw, 1); ghw.push_back(1);
            AppendULeb128(ghw, 1); ghw.push_back(0);
            ghw.push_back(0);
            AppendSLeb128(ghw, -1);
        }
        ghw.insert(ghw.end(), { 'E','C','Y',0, 'T','A','I',0 });
    }
    return ghw;
}

static std::vector<uint8_t> BuildXilinxBitFile(bool variantB) {
    std::vector<uint8_t> bit;
    // Initial 13-byte Xilinx sync header: [0x00, 0x09] + 9 bytes + [0x00, 0x01]
    const uint8_t syncHdr[13] = { 0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01 };
    bit.insert(bit.end(), syncHdr, syncHdr + 13);

    auto addField = [&](char key, const std::string& val) {
        bit.push_back(static_cast<uint8_t>(key));
        uint8_t lbe[2] = {};
        PutU16BE(lbe, static_cast<uint16_t>(val.size() + 1));
        bit.insert(bit.end(), lbe, lbe + 2);
        bit.insert(bit.end(), val.begin(), val.end());
        bit.push_back(0);
    };
    addField('a', variantB ? "pcie_dma_top;UserID=0xFFFFFFFF" : "blinky_led;UserID=0x12345678");
    addField('b', variantB ? "xc7k325tffg900-2" : "xc7a35tcpg236-1");
    addField('c', "2026/11/15");
    addField('d', "10:30:00");

    // Configuration packet words (big-endian 32-bit):
    // Dummy + Sync 0xAA995566 + Type1 Write IDCODE (reg 12, op 2, cnt 1 = 0x30018001) + Type1 Write FDRI (reg 2, op 2, cnt=0 = 0x30004000) + Type2 cnt
    std::vector<uint32_t> words = {
        0xFFFFFFFFu,
        0xAA995566u,
        0x20000000u,
        0x30018001u,
        variantB ? 0x03651093u : 0x0362D093u,
        0x30004000u,
        0x40000000u | (variantB ? 256u : 128u)
    };
    uint32_t nFdri = variantB ? 256u : 128u;
    for (uint32_t i = 0; i < nFdri; ++i) {
        if (variantB) {
            // High utilization across nearly all CLB tiles
            words.push_back(0xA5F03C96u ^ (i * 0x01010101u));
        } else {
            // Sparse utilization in first quarter of CLB tiles only
            words.push_back((i < 24) ? (0x0F0F00FFu ^ i) : 0x00000000u);
        }
    }

    bit.push_back('e');
    uint8_t blenBe[4] = {};
    PutU32BE(blenBe, static_cast<uint32_t>(words.size() * 4));
    bit.insert(bit.end(), blenBe, blenBe + 4);
    for (uint32_t w : words) {
        uint8_t wb[4] = {};
        PutU32BE(wb, w);
        bit.insert(bit.end(), wb, wb + 4);
    }
    return bit;
}

static std::vector<uint8_t> BuildStm32DfuFile(bool variantB) {
    struct Elem { uint32_t addr; std::vector<uint8_t> data; };
    std::vector<Elem> elems;
    if (!variantB) {
        std::vector<uint8_t> sec0(256, 0xFF);
        for (size_t i = 0; i < 64; ++i) sec0[i] = static_cast<uint8_t>(i ^ 0x5Au); // 25% code fill
        elems.push_back({ 0x08000000u, std::move(sec0) });
    } else {
        std::vector<uint8_t> boot(256, 0x20); // 100% code fill
        std::vector<uint8_t> app(512, 0xFF);
        for (size_t i = 0; i < 420; ++i) app[i] = static_cast<uint8_t>((i * 7) & 0xFEu); // 82% fill
        std::vector<uint8_t> cfg(128, 0xFF);
        for (size_t i = 0; i < 48; ++i) cfg[i] = 0xA5;
        elems.push_back({ 0x08000000u, std::move(boot) });
        elems.push_back({ 0x08008000u, std::move(app) });
        elems.push_back({ 0x08060000u, std::move(cfg) });
    }

    uint32_t targetPayloadSz = 0;
    for (const auto& e : elems) targetPayloadSz += 8u + static_cast<uint32_t>(e.data.size());

    std::vector<uint8_t> dfu(11 + 274, 0);
    std::memcpy(dfu.data(), "DfuSe", 5);
    dfu[5] = 0x01; // bVersion
    PutU32LE(dfu.data() + 6, 11u + 274u + targetPayloadSz);
    dfu[10] = 1; // bTargets = 1

    uint8_t* tp = dfu.data() + 11;
    std::memcpy(tp, "Target", 6);
    tp[6] = 0; // alt setting
    PutU32LE(tp + 7, 1); // bTargetNamed = true
    const char* tnm = variantB ? "Internal Flash STM32H7" : "Internal Flash STM32F4";
    std::memcpy(tp + 11, tnm, std::strlen(tnm));
    PutU32LE(tp + 266, targetPayloadSz);
    PutU32LE(tp + 270, static_cast<uint32_t>(elems.size()));

    for (const auto& e : elems) {
        uint8_t eh[8] = {};
        PutU32LE(eh + 0, e.addr);
        PutU32LE(eh + 4, static_cast<uint32_t>(e.data.size()));
        dfu.insert(dfu.end(), eh, eh + 8);
        dfu.insert(dfu.end(), e.data.begin(), e.data.end());
    }

    // 16-byte USB DFU Suffix ("UFD" at +8..+10)
    uint8_t sfx[16] = {};
    PutU16LE(sfx + 0, variantB ? 0x0200u : 0x011Au); // bcdDevice
    PutU16LE(sfx + 2, 0xDF11u); // idProduct (STM32 DFU)
    PutU16LE(sfx + 4, 0x0483u); // idVendor  (STMicroelectronics)
    PutU16LE(sfx + 6, 0x011Au); // bcdDFU
    sfx[8] = 'U'; sfx[9] = 'F'; sfx[10] = 'D';
    sfx[11] = 16;
    PutU32LE(sfx + 12, CalcCrc32(dfu.data(), dfu.size()));
    dfu.insert(dfu.end(), sfx, sfx + 16);
    return dfu;
}

static std::vector<uint8_t> BuildVesaEdidFile(bool qhd144WithCta) {
    std::vector<uint8_t> edid(qhd144WithCta ? 256 : 128, 0);
    static const uint8_t kMagic[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    std::memcpy(edid.data(), kMagic, 8);

    // 3-letter PNP ID: "DEL" (4,5,12) vs "GSM" (7,19,13)
    uint16_t mfg = qhd144WithCta
        ? static_cast<uint16_t>((7u << 10) | (19u << 5) | 13u)
        : static_cast<uint16_t>((4u << 10) | (5u << 5) | 12u);
    PutU16BE(edid.data() + 8, mfg);
    PutU16LE(edid.data() + 10, qhd144WithCta ? 0x5B09u : 0xA0B1u);
    PutU32LE(edid.data() + 12, qhd144WithCta ? 987654u : 123456u);
    edid[16] = 24;
    edid[17] = 35; // 2025
    edid[18] = 1;
    edid[19] = 4;  // EDID 1.4
    edid[20] = qhd144WithCta ? 0xB5u : 0xA2u; // Digital 10bpc DP vs 8bpc HDMI-a
    edid[21] = qhd144WithCta ? 60 : 53;
    edid[22] = qhd144WithCta ? 34 : 30;
    edid[23] = 120; // Gamma 2.20

    // Chromaticity: sRGB vs Wide DCI-P3
    edid[27] = qhd144WithCta ? 174 : 164; // Rx
    edid[28] = qhd144WithCta ? 82  : 84;  // Ry
    edid[29] = qhd144WithCta ? 68  : 77;  // Gx
    edid[30] = qhd144WithCta ? 177 : 154; // Gy
    edid[31] = qhd144WithCta ? 38  : 38;  // Bx
    edid[32] = qhd144WithCta ? 15  : 15;  // By
    edid[33] = 80; edid[34] = 84;         // White D65

    // Descriptor 0 (bytes 54..71): Detailed Timing Descriptor
    uint8_t* d0 = edid.data() + 54;
    if (!qhd144WithCta) {
        // 1920x1080 @ 60.00 Hz (148.50 MHz, hTotal=2200, vTotal=1125, 527x296 mm)
        PutU16LE(d0, 14850);
        int ha = 1920, hb = 280, va = 1080, vb = 45, hm = 527, vm = 296;
        d0[2] = static_cast<uint8_t>(ha & 0xFF);
        d0[3] = static_cast<uint8_t>(hb & 0xFF);
        d0[4] = static_cast<uint8_t>(((ha >> 8) << 4) | (hb >> 8));
        d0[5] = static_cast<uint8_t>(va & 0xFF);
        d0[6] = static_cast<uint8_t>(vb & 0xFF);
        d0[7] = static_cast<uint8_t>(((va >> 8) << 4) | (vb >> 8));
        d0[12] = static_cast<uint8_t>(hm & 0xFF);
        d0[13] = static_cast<uint8_t>(vm & 0xFF);
        d0[14] = static_cast<uint8_t>(((hm >> 8) << 4) | (vm >> 8));
    } else {
        // 2560x1440 @ 144.01 Hz (589.54 MHz, hTotal=2720, vTotal=1505, 597x336 mm)
        PutU16LE(d0, 58954);
        int ha = 2560, hb = 160, va = 1440, vb = 65, hm = 597, vm = 336;
        d0[2] = static_cast<uint8_t>(ha & 0xFF);
        d0[3] = static_cast<uint8_t>(hb & 0xFF);
        d0[4] = static_cast<uint8_t>(((ha >> 8) << 4) | (hb >> 8));
        d0[5] = static_cast<uint8_t>(va & 0xFF);
        d0[6] = static_cast<uint8_t>(vb & 0xFF);
        d0[7] = static_cast<uint8_t>(((va >> 8) << 4) | (vb >> 8));
        d0[12] = static_cast<uint8_t>(hm & 0xFF);
        d0[13] = static_cast<uint8_t>(vm & 0xFF);
        d0[14] = static_cast<uint8_t>(((hm >> 8) << 4) | (vm >> 8));
    }

    // Descriptor 1 (bytes 72..89): Monitor Name (tag 0xFC)
    uint8_t* d1 = edid.data() + 72;
    d1[3] = 0xFC;
    const char* mname = qhd144WithCta ? "27GP850\n     " : "U2422H\n      ";
    std::memcpy(d1 + 5, mname, 13);

    // Descriptor 2 (bytes 90..107): Range Limits (tag 0xFD)
    uint8_t* d2 = edid.data() + 90;
    d2[3] = 0xFD;
    d2[5] = 48;
    d2[6] = qhd144WithCta ? 144 : 60;
    d2[9] = qhd144WithCta ? 60 : 17;

    edid[126] = qhd144WithCta ? 1 : 0;
    uint8_t sum0 = 0;
    for (int i = 0; i < 127; ++i) sum0 = static_cast<uint8_t>(sum0 + edid[i]);
    edid[127] = static_cast<uint8_t>(0u - sum0);

    if (qhd144WithCta) {
        uint8_t* cta = edid.data() + 128;
        cta[0] = 0x02; // CTA-861
        cta[1] = 0x03;
        cta[2] = 16;   // DTDs start at +16
        cta[3] = 0xF0;
        // Video Data Block (tag=2, len=3) + Audio Data Block (tag=1, len=3) + HDMI VSDB (tag=3, len=3)
        cta[4] = static_cast<uint8_t>((2u << 5) | 3u);
        cta[5] = 16; cta[6] = 31; cta[7] = 4;
        cta[8] = static_cast<uint8_t>((1u << 5) | 3u);
        cta[9] = 0x09; cta[10] = 0x07; cta[11] = 0x07;
        cta[12] = static_cast<uint8_t>((3u << 5) | 3u);
        cta[13] = 0x03; cta[14] = 0x0C; cta[15] = 0x00;
        uint8_t sum1 = 0;
        for (int i = 0; i < 127; ++i) sum1 = static_cast<uint8_t>(sum1 + cta[i]);
        cta[127] = static_cast<uint8_t>(0u - sum1);
    }
    return edid;
}

static std::vector<uint8_t> BuildJedecSpdFile(bool ddr5_32gb) {
    if (!ddr5_32gb) {
        // 512B JEDEC DDR4-3200 CL22 8GB (1Rx8) Samsung M378A1K43EB2-CWE
        std::vector<uint8_t> spd(512, 0);
        spd[0] = 0x23;
        spd[1] = 0x11;
        spd[2] = 0x0C; // DDR4 SDRAM
        spd[3] = 0x02; // UDIMM
        spd[4] = 0x05; // 8 Gb die density (256 << 5 = 8192 Mb)
        spd[12] = 0x01; // 1 rank, x8 device width
        spd[13] = 0x03; // 64-bit primary bus -> 8 chips
        spd[18] = 5;    // tCKAVGmin = 5 * 125 ps = 625 ps -> 3200 MT/s
        spd[24] = 110;  // tAAmin = 110 * 125 ps = 13750 ps -> CL22
        spd[320] = 0x80; // Bank 1 (0 continuations)
        spd[321] = 0xCE; // Samsung
        const char* pn = "M378A1K43EB2-CWE    ";
        std::memcpy(spd.data() + 329, pn, 20);
        return spd;
    } else {
        // 1024B JEDEC DDR5-5600 32GB (2Rx8) SK Hynix HMCG88AGBUA081N
        std::vector<uint8_t> spd(1024, 0);
        spd[0] = 0x30;
        spd[1] = 0x10;
        spd[2] = 0x12; // DDR5 SDRAM
        spd[3] = 0x02; // UDIMM
        spd[4] = 0x04; // 16 Gb die density
        spd[6] = 0x20; // x8 I/O width (1 << 5)
        PutU16LE(spd.data() + 20, 357);   // tCKAVGmin = 357 ps -> 5600 MT/s
        PutU16LE(spd.data() + 30, 16422); // tAAmin = 16422 ps -> CL46
        spd[234] = 0x08; // 2 package ranks per channel ((1 << 3))
        spd[235] = 0x22; // 2 sub-channels x 32-bit = 64-bit -> 16 DRAM chips
        spd[512] = 0x80;
        spd[513] = 0xAD; // SK Hynix
        const char* pn = "HMCG88AGBUA081N";
        std::memcpy(spd.data() + 521, pn, std::strlen(pn));
        return spd;
    }
}

static bool WriteAndInspect(const std::wstring& path, const void* bytes, size_t len, ParsedEdaBoard& out) {
    if (len == 0) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    } else {
        if (!PathUtils::WriteFileBytes(path, bytes, len)) return false;
    }
    bool ok = EdaViewer::InspectEdaFile(path, out) && out.valid;
    DeleteFileW(path.c_str());
    return ok;
}

} // namespace

void RunFormatTestsEda(TestCounter& tc) {
    const std::wstring dir = ScratchTestDir() + L"\\eda_stage15";
    CreateDirectoryW(dir.c_str(), nullptr);

    // ------------------------------------------------------------------------
    // 1. Binary Eagle v5 .brd & .sch (24-byte blocks) + corrupt rejection
    // ------------------------------------------------------------------------
    {
        auto brdA = BuildEagleV5Board(false);
        auto brdB = BuildEagleV5Board(true);
        auto schA = BuildEagleV5Schematic(false);
        auto schB = BuildEagleV5Schematic(true);

        ParsedEdaBoard bA, bB, sA, sB, bBad1, bBad2, bEmpty;
        bool okBrdA = WriteAndInspect(dir + L"\\eagle_a.brd", brdA.data(), brdA.size(), bA);
        bool okBrdB = WriteAndInspect(dir + L"\\eagle_b.brd", brdB.data(), brdB.size(), bB);
        bool okSchA = WriteAndInspect(dir + L"\\eagle_a.sch", schA.data(), schA.size(), sA);
        bool okSchB = WriteAndInspect(dir + L"\\eagle_b.sch", schB.data(), schB.size(), sB);

        // Corrupt Eagle: header claims 999999 blocks on a 48B file, or truncated 10B file, or empty
        std::vector<uint8_t> badEagle(48, 0);
        badEagle[0] = 0x10;
        PutU32LE(badEagle.data() + 4, 999999u);
        badEagle[8] = 5;
        bool rejBad  = !WriteAndInspect(dir + L"\\corrupt.brd", badEagle.data(), badEagle.size(), bBad1);
        bool rejTrnc = !WriteAndInspect(dir + L"\\trunc.brd", brdA.data(), 12, bBad2);
        bool rejEmp  = !WriteAndInspect(dir + L"\\empty.brd", nullptr, 0, bEmpty);

        bool pass = okBrdA && okBrdB && okSchA && okSchB &&
                    bA.components.size() == 2 && bB.components.size() == 4 &&
                    bA.vias.size() == 2 && bB.vias.size() == 5 &&
                    std::fabs(bA.components[0].x - 15.0f) < 0.05f &&
                    std::fabs(bB.components[3].x - 68.0f) < 0.05f &&
                    HashEdaGeometry(bA) != HashEdaGeometry(bB) &&
                    HashEdaBoardFull(sA) != HashEdaBoardFull(sB) &&
                    rejBad && rejTrnc && rejEmp;

        tc.Record(pass, L"Stage15.Eda.EagleBinaryV5_BrdAndSch",
            L"brdParts=" + std::to_wstring(bA.components.size()) + L"/" + std::to_wstring(bB.components.size()) +
            L" schParts=" + std::to_wstring(sA.components.size()) + L"/" + std::to_wstring(sB.components.size()));
    }

    // ------------------------------------------------------------------------
    // 2. IPC-2581 (.ipc2581) & ODB++ (.odb ZIP + .odb Gzipped TAR) — zero fake QFN-32
    // ------------------------------------------------------------------------
    {
        const std::string ipcA =
            "<?xml version=\"1.0\"?>\n"
            "<IPC-2581 revision=\"B\"><Content/><Ecad name=\"BoardA\"><CadHeader units=\"MILLIMETER\"/>"
            "<CadData><Layer name=\"TOP\" layerFunction=\"CONDUCTOR\" side=\"TOP\"/>"
            "<Step name=\"PCB\"><Profile><Polygon><PolyBegin x=\"0\" y=\"0\"/>"
            "<PolyStepSegment x=\"50\" y=\"0\"/><PolyStepSegment x=\"50\" y=\"40\"/>"
            "<PolyStepSegment x=\"0\" y=\"40\"/><PolyStepSegment x=\"0\" y=\"0\"/></Polygon></Profile>"
            "<Component refDes=\"U1\" part=\"STM32G4\" packageRef=\"LQFP-48\" layerRef=\"TOP\"><Location x=\"15\" y=\"20\"/><Xform rotation=\"90\"/></Component>"
            "<Component refDes=\"C1\" part=\"100nF\" packageRef=\"0603\" layerRef=\"TOP\"><Location x=\"28\" y=\"18\"/></Component>"
            "<LayerFeature layerRef=\"TOP\"><Set net=\"VCC\"><Features><LineDesc lineWidth=\"0.3\"/>"
            "<Line startX=\"5\" startY=\"5\" endX=\"15\" endY=\"20\"/><Pad><Location x=\"15\" y=\"20\"/></Pad>"
            "</Features></Set></LayerFeature></Step></CadData></Ecad></IPC-2581>\n";

        const std::string ipcB =
            "<?xml version=\"1.0\"?>\n"
            "<IPC-2581 revision=\"B\"><Ecad name=\"BoardB\"><CadHeader units=\"INCH\"/>"
            "<CadData><Layer name=\"TOP\" layerFunction=\"CONDUCTOR\" side=\"TOP\"/>"
            "<Layer name=\"BOT\" layerFunction=\"CONDUCTOR\" side=\"BOTTOM\"/>"
            "<Step name=\"PCB\"><Profile><Polygon><PolyBegin x=\"0\" y=\"0\"/>"
            "<PolyStepSegment x=\"3.0\" y=\"0\"/><PolyStepSegment x=\"3.0\" y=\"2.0\"/>"
            "<PolyStepSegment x=\"0\" y=\"2.0\"/><PolyStepSegment x=\"0\" y=\"0\"/></Polygon></Profile>"
            "<Component refDes=\"U1\" part=\"RP2040\" packageRef=\"QFN-56\" layerRef=\"TOP\"><Location x=\"1.2\" y=\"1.0\"/></Component>"
            "<Component refDes=\"R1\" part=\"4k7\" packageRef=\"0402\" layerRef=\"TOP\"><Location x=\"2.1\" y=\"0.8\"/></Component>"
            "<Component refDes=\"Y1\" part=\"12MHz\" packageRef=\"HC49\" layerRef=\"BOT\"><Location x=\"0.7\" y=\"1.4\"/></Component>"
            "<LayerFeature layerRef=\"BOT\"><Set net=\"GND\"><Features><LineDesc lineWidth=\"0.012\"/>"
            "<Line startX=\"0.2\" startY=\"0.2\" endX=\"2.5\" endY=\"1.7\"/>"
            "<Line startX=\"2.5\" startY=\"1.7\" endX=\"1.2\" endY=\"1.0\"/>"
            "<Hole x=\"0.5\" y=\"0.5\" diameter=\"0.035\"/>"
            "</Features></Set></LayerFeature></Step></CadData></Ecad></IPC-2581>\n";

        auto odbZip = BuildStoredZip({
            { "odb_a/matrix/matrix", "LAYER {\n ROW=1\n CONTEXT=BOARD\n TYPE=SIGNAL\n NAME=top\n}\n" },
            { "odb_a/steps/pcb/profile", "UNITS=MM\nOB 0 0\nOS 45 0\nOS 45 30\nOS 0 30\nOS 0 0\nOE\n" },
            { "odb_a/steps/pcb/layers/top/features", "UNITS=MM\n$0 r250\n$1 rect1200x800\nL 4 5 20 15 0 P 0\nP 20 15 1 P 0\nP 32 15 1 P 0\n" },
            { "odb_a/steps/pcb/layers/comp_+_top/components", "UNITS=MM\nCMP 0 20.0 15.0 0 N U1 ADS1115\nCMP 1 32.0 15.0 90 N R1 10K\n" }
        });

        auto odbTarRaw = BuildUstarTar({
            { "odb_b/matrix/matrix", "LAYER {\n ROW=1\n CONTEXT=BOARD\n TYPE=SIGNAL\n NAME=top\n}\nLAYER {\n ROW=2\n CONTEXT=BOARD\n TYPE=SIGNAL\n NAME=bot\n}\n" },
            { "odb_b/steps/pcb/profile", "UNITS=INCH\nOB 0 0\nOS 3.5 0\nOS 3.5 2.2\nOS 0 2.2\nOS 0 0\nOE\n" },
            { "odb_b/steps/pcb/layers/top/features", "UNITS=INCH\n$0 r12\n$1 r45\nL 0.2 0.3 1.8 1.1 0 P 0\nL 1.8 1.1 2.9 1.6 0 P 0\nA 0.5 0.5 0.9 0.9 0.5 0.9 0 P Y\nP 1.8 1.1 1 P 0\n" },
            { "odb_b/steps/pcb/layers/comp_+_top/components",
              "UNITS=INCH\nCMP 0 0.8 0.9 0 N U1 ESP32\nCMP 1 1.8 1.1 180 N U2 CH340C\nCMP 2 2.4 0.6 0 N C1 10uF\nCMP 3 3.1 1.1 270 N J1 USBC\n" }
        });
        auto odbTgz = WrapGzipStored(odbTarRaw);

        ParsedEdaBoard iA, iB, oA, oB, badIpc, badOdb;
        bool okIA = WriteAndInspect(dir + L"\\a.ipc2581", ipcA.data(), ipcA.size(), iA);
        bool okIB = WriteAndInspect(dir + L"\\b.ipc2581", ipcB.data(), ipcB.size(), iB);
        bool okOA = WriteAndInspect(dir + L"\\a.odb", odbZip.data(), odbZip.size(), oA);
        bool okOB = WriteAndInspect(dir + L"\\b.odb", odbTgz.data(), odbTgz.size(), oB);

        const char* junk = "NOT AN IPC OR ODB ARCHIVE X100 Y200 D01*";
        bool rejIpc = !WriteAndInspect(dir + L"\\bad.ipc2581", junk, std::strlen(junk), badIpc);
        bool rejOdb = !WriteAndInspect(dir + L"\\bad.odb", junk, std::strlen(junk), badOdb);

        bool pass = okIA && okIB && okOA && okOB &&
                    !HasFakeQfn32(iA) && !HasFakeQfn32(iB) && !HasFakeQfn32(oA) && !HasFakeQfn32(oB) &&
                    iA.components.size() == 2 && iB.components.size() == 3 &&
                    oA.components.size() == 2 && oB.components.size() == 4 &&
                    std::fabs(iA.components[0].x - 15.0f) < 0.05f &&
                    std::fabs(oA.components[0].x - 20.0f) < 0.05f &&
                    HashEdaGeometry(iA) != HashEdaGeometry(iB) &&
                    HashEdaGeometry(oA) != HashEdaGeometry(oB) &&
                    rejIpc && rejOdb;

        tc.Record(pass, L"Stage15.Eda.Ipc2581AndOdbPlusPlus",
            L"ipc=" + std::to_wstring(iA.components.size()) + L"/" + std::to_wstring(iB.components.size()) +
            L" odb=" + std::to_wstring(oA.components.size()) + L"/" + std::to_wstring(oB.components.size()));
    }

    // ------------------------------------------------------------------------
    // 3. Altium OLE2 Compound Files (.SchDoc & .PcbDoc) + corrupt rejection
    // ------------------------------------------------------------------------
    {
        auto buildSchOle = [](bool variantB) {
            std::vector<uint8_t> fh;
            AppendAltiumLenBlock(fh, "|HEADER=Protel for Windows - Schematic Capture Binary File Version 5.0|WEIGHT=10|");
            AppendAltiumLenBlock(fh, "|RECORD=31|SHEETSTYLE=4|"); // index 0 = Sheet
            AppendAltiumLenBlock(fh, "|RECORD=1|LIBREFERENCE=LM358|LOCATION.X=100|LOCATION.Y=200|ORIENTATION=0|"); // index 1
            AppendAltiumLenBlock(fh, "|RECORD=34|OWNERINDEX=1|TEXT=U1|LOCATION.X=100|LOCATION.Y=230|");
            AppendAltiumLenBlock(fh, "|RECORD=41|OWNERINDEX=1|NAME=Comment|TEXT=LM358DR|");
            AppendAltiumLenBlock(fh, "|RECORD=2|OWNERINDEX=1|NAME=OUT|DESIGNATOR=1|LOCATION.X=130|LOCATION.Y=200|PINLENGTH=20|PINCONGLOMERATE=0|");
            AppendAltiumLenBlock(fh, "|RECORD=27|LOCATIONCOUNT=2|X1=150|Y1=200|X2=260|Y2=200|");
            AppendAltiumLenBlock(fh, "|RECORD=25|TEXT=VOUT_A|LOCATION.X=190|LOCATION.Y=205|");
            if (variantB) {
                AppendAltiumLenBlock(fh, "|RECORD=1|LIBREFERENCE=RES|LOCATION.X=280|LOCATION.Y=200|ORIENTATION=1|"); // index 7
                AppendAltiumLenBlock(fh, "|RECORD=34|OWNERINDEX=7|TEXT=R5|LOCATION.X=280|LOCATION.Y=225|");
                AppendAltiumLenBlock(fh, "|RECORD=27|LOCATIONCOUNT=3|X1=260|Y1=200|X2=280|Y2=200|X3=280|Y3=120|");
                AppendAltiumLenBlock(fh, "|RECORD=29|LOCATION.X=260|LOCATION.Y=200|");
            }
            return BuildOle2CompoundFile({ { L"FileHeader", std::move(fh) } });
        };

        auto buildPcbOle = [](bool variantB) {
            std::vector<uint8_t> brd, cmps, nets, trks, vias, pads;
            AppendAltiumLenBlock(brd, variantB
                ? "|VX0=0mil|VY0=0mil|VX1=3000mil|VY1=0mil|VX2=3000mil|VY2=2000mil|VX3=0mil|VY3=2000mil|"
                : "|VX0=0mil|VY0=0mil|VX1=1800mil|VY1=0mil|VX2=1800mil|VY2=1200mil|VX3=0mil|VY3=1200mil|");
            AppendAltiumLenBlock(nets, "|NAME=GND|");
            AppendAltiumLenBlock(nets, "|NAME=VCC_3V3|");

            AppendAltiumLenBlock(cmps, "|SOURCEDESIGNATOR=U1|PATTERN=LQFP-64|COMMENT=STM32F405|X=600mil|Y=500mil|ROTATION=0|LAYER=TOP|");
            AppendAltiumLenBlock(cmps, "|SOURCEDESIGNATOR=C1|PATTERN=0603|COMMENT=100nF|X=950mil|Y=520mil|ROTATION=90|LAYER=TOP|");
            if (variantB) {
                AppendAltiumLenBlock(cmps, "|SOURCEDESIGNATOR=U2|PATTERN=SOIC-8|COMMENT=CAN_TRX|X=1800mil|Y=900mil|ROTATION=180|LAYER=BOTTOM|");
                AppendAltiumLenBlock(cmps, "|SOURCEDESIGNATOR=J1|PATTERN=HDR_1X4|COMMENT=CAN_BUS|X=2650mil|Y=1000mil|ROTATION=270|LAYER=TOP|");
            }

            // Binary Tracks6/Data (recordType == 4, 36B payload; 10,000 internal units = 1 mil = 0.0254 mm)
            int nTracks = variantB ? 4 : 2;
            for (int i = 0; i < nTracks; ++i) {
                uint8_t rec[5 + 36] = {};
                rec[0] = 4;
                PutU32LE(rec + 1, 36);
                uint8_t* p = rec + 5;
                p[0] = (i % 2 == 0) ? 1 : 32; // Top / Bottom copper
                PutU16LE(p + 3, static_cast<uint16_t>(i));
                PutI32LE(p + 13, (200 + i * 400) * 10000);
                PutI32LE(p + 17, (300 + i * 180) * 10000);
                PutI32LE(p + 21, (750 + i * 500) * 10000);
                PutI32LE(p + 25, (650 + i * 240) * 10000);
                PutI32LE(p + 29, 10 * 10000); // 10 mil width
                trks.insert(trks.end(), rec, rec + sizeof(rec));
            }

            // Binary Vias6/Data (recordType == 3, 32B payload)
            {
                uint8_t vrec[5 + 32] = {};
                vrec[0] = 3;
                PutU32LE(vrec + 1, 32);
                PutI32LE(vrec + 5 + 13, (variantB ? 1500 : 750) * 10000);
                PutI32LE(vrec + 5 + 17, 650 * 10000);
                PutI32LE(vrec + 5 + 21, 32 * 10000);
                PutI32LE(vrec + 5 + 25, 16 * 10000);
                vias.insert(vias.end(), vrec, vrec + sizeof(vrec));
            }

            // Binary Pads6/Data (recordType == 2 + subrecord0 name + subrecord1 36B geometry)
            {
                pads.push_back(2);
                uint8_t s0[4 + 2] = {};
                PutU32LE(s0, 2); s0[4] = 1; s0[5] = '1';
                pads.insert(pads.end(), s0, s0 + sizeof(s0));
                uint8_t s1[4 + 36] = {};
                PutU32LE(s1, 36);
                s1[4 + 0] = 74; // Multi-layer pad
                PutI32LE(s1 + 4 + 13, (variantB ? 2650 : 600) * 10000);
                PutI32LE(s1 + 4 + 17, (variantB ? 1000 : 500) * 10000);
                PutI32LE(s1 + 4 + 21, 60 * 10000);
                PutI32LE(s1 + 4 + 25, 60 * 10000);
                PutI32LE(s1 + 4 + 29, 32 * 10000);
                pads.insert(pads.end(), s1, s1 + sizeof(s1));
            }

            return BuildOle2CompoundFile({
                { L"Board6/Data", std::move(brd) },
                { L"Nets6/Data", std::move(nets) },
                { L"Components6/Data", std::move(cmps) },
                { L"Tracks6/Data", std::move(trks) },
                { L"Vias6/Data", std::move(vias) },
                { L"Pads6/Data", std::move(pads) }
            });
        };

        auto schA = buildSchOle(false);
        auto schB = buildSchOle(true);
        auto pcbA = buildPcbOle(false);
        auto pcbB = buildPcbOle(true);

        ParsedEdaBoard sA, sB, pA, pB, badSch, badPcb;
        bool okSA = WriteAndInspect(dir + L"\\a.schdoc", schA.data(), schA.size(), sA);
        bool okSB = WriteAndInspect(dir + L"\\b.schdoc", schB.data(), schB.size(), sB);
        bool okPA = WriteAndInspect(dir + L"\\a.pcbdoc", pcbA.data(), pcbA.size(), pA);
        bool okPB = WriteAndInspect(dir + L"\\b.pcbdoc", pcbB.data(), pcbB.size(), pB);

        std::vector<uint8_t> corruptOle(512, 0xCC);
        bool rejSch = !WriteAndInspect(dir + L"\\bad.schdoc", corruptOle.data(), corruptOle.size(), badSch);
        bool rejPcb = !WriteAndInspect(dir + L"\\bad.pcbdoc", corruptOle.data(), corruptOle.size(), badPcb);

        bool pass = okSA && okSB && okPA && okPB &&
                    sA.components.size() == 1 && sB.components.size() == 2 &&
                    pA.components.size() == 2 && pB.components.size() == 4 &&
                    !pA.traces.empty() && !pA.vias.empty() && !pA.pads.empty() &&
                    HashEdaGeometry(sA) != HashEdaGeometry(sB) &&
                    HashEdaGeometry(pA) != HashEdaGeometry(pB) &&
                    rejSch && rejPcb;

        tc.Record(pass, L"Stage15.Eda.AltiumOle2_SchDocAndPcbDoc",
            L"sch=" + std::to_wstring(sA.components.size()) + L"/" + std::to_wstring(sB.components.size()) +
            L" pcb=" + std::to_wstring(pA.components.size()) + L"/" + std::to_wstring(pB.components.size()));
    }

    // ------------------------------------------------------------------------
    // 4. Vector CAN (.dbc, CAN .asc, and Binary .blf with zlib LOG_CONTAINER)
    // ------------------------------------------------------------------------
    {
        const std::string dbcA =
            "VERSION \"\"\n"
            "BO_ 256 EngineMsg: 8 ECM\n"
            " SG_ EngineSpeed : 0|16@1+ (0.125,0) [0|8000] \"rpm\" Vector__XXX\n"
            " SG_ CoolantTemp : 16|8@1+ (1,-40) [-40|215] \"degC\" Vector__XXX\n";
        const std::string dbcB =
            "VERSION \"\"\n"
            "BO_ 512 BrakeMsg: 8 ABS\n"
            " SG_ WheelSpeedFL : 7|16@0+ (0.01,0) [0|300] \"km/h\" Vector__XXX\n"
            " SG_ BrakePressure : 24|12@1+ (0.5,0) [0|200] \"bar\" Vector__XXX\n"
            "BO_ 768 BatteryMsg: 8 BMS\n"
            " SG_ PackVoltage : 0|16@1+ (0.1,0) [0|1000] \"V\" Vector__XXX\n";

        const std::string ascA =
            "date Sun Nov 15 10:00:00 2026\nbase hex timestamps absolute\n"
            "0.001000 1 100 Rx d 4 10 20 30 40\n"
            "0.005000 1 100 Rx d 4 80 90 A0 B0\n";
        const std::string ascB =
            "date Sun Nov 15 10:00:00 2026\nbase hex timestamps absolute\n"
            "0.001000 1 200 Rx d 8 FF 00 FF 00 11 22 33 44\n"
            "0.004000 CANFD 1 Tx 350 1 0 9 12 01 02 03 04 05 06 07 08 09 0A 0B 0C\n"
            "0.009000 2 410 Rx d 2 AA 55\n";

        auto blfA = BuildVectorBlfFile(false);
        auto blfB = BuildVectorBlfFile(true);

        ParsedEdaBoard dA, dB, aA, aB, lA, lB, badBlf, badDbc, badAsc;
        bool okDA = WriteAndInspect(dir + L"\\a.dbc", dbcA.data(), dbcA.size(), dA);
        bool okDB = WriteAndInspect(dir + L"\\b.dbc", dbcB.data(), dbcB.size(), dB);
        bool okAA = WriteAndInspect(dir + L"\\a.asc", ascA.data(), ascA.size(), aA);
        bool okAB = WriteAndInspect(dir + L"\\b.asc", ascB.data(), ascB.size(), aB);
        bool okLA = WriteAndInspect(dir + L"\\a.blf", blfA.data(), blfA.size(), lA);
        bool okLB = WriteAndInspect(dir + L"\\b.blf", blfB.data(), blfB.size(), lB);

        const char* junk = "NOT A VALID CAN LOG OR DBC";
        bool rejBlf = !WriteAndInspect(dir + L"\\bad.blf", junk, std::strlen(junk), badBlf);
        bool rejDbc = !WriteAndInspect(dir + L"\\bad.dbc", junk, std::strlen(junk), badDbc);
        bool rejAsc = !WriteAndInspect(dir + L"\\bad.asc", junk, std::strlen(junk), badAsc);

        bool pass = okDA && okDB && okAA && okAB && okLA && okLB &&
                    dA.components.size() == 1 && dB.components.size() == 2 &&
                    lA.components.size() == 1 && lB.components.size() == 3 &&
                    HashEdaGeometry(dA) != HashEdaGeometry(dB) &&
                    HashEdaGeometry(aA) != HashEdaGeometry(aB) &&
                    HashEdaGeometry(lA) != HashEdaGeometry(lB) &&
                    rejBlf && rejDbc && rejAsc;

        tc.Record(pass, L"Stage15.Eda.VectorCan_DbcAscAndBlf",
            L"blfIDs=" + std::to_wstring(lA.components.size()) + L"/" + std::to_wstring(lB.components.size()));
    }

    // ------------------------------------------------------------------------
    // 5. GTKWave .fst & GHDL .ghw Digital Waveform Viewers (no fake square waves)
    // ------------------------------------------------------------------------
    {
        auto fstA = BuildGtkWaveFstFile(false);
        auto fstB = BuildGtkWaveFstFile(true);
        auto ghwA = BuildGhdlGhwFile(false);
        auto ghwB = BuildGhdlGhwFile(true);

        ParsedEdaBoard fA, fB, gA, gB, badFst, badGhw;
        bool okFA = WriteAndInspect(dir + L"\\a.fst", fstA.data(), fstA.size(), fA);
        bool okFB = WriteAndInspect(dir + L"\\b.fst", fstB.data(), fstB.size(), fB);
        bool okGA = WriteAndInspect(dir + L"\\a.ghw", ghwA.data(), ghwA.size(), gA);
        bool okGB = WriteAndInspect(dir + L"\\b.ghw", ghwB.data(), ghwB.size(), gB);

        const uint8_t badBytes[64] = { 0xDE, 0xAD, 0xBE, 0xEF, 'X', '1', '0', 'Y', '2', '0', 'D', '0', '1' };
        bool rejFst = !WriteAndInspect(dir + L"\\bad.fst", badBytes, sizeof(badBytes), badFst);
        bool rejGhw = !WriteAndInspect(dir + L"\\bad.ghw", badBytes, sizeof(badBytes), badGhw);

        bool pass = okFA && okFB && okGA && okGB &&
                    fA.netCount == 2 && fB.netCount == 3 &&
                    gA.netCount == 2 && gB.netCount == 3 &&
                    HashEdaGeometry(fA) != HashEdaGeometry(fB) &&
                    HashEdaGeometry(gA) != HashEdaGeometry(gB) &&
                    rejFst && rejGhw;

        tc.Record(pass, L"Stage15.Eda.DigitalWaveforms_FstAndGhw",
            L"fstTraces=" + std::to_wstring(fA.traces.size()) + L"/" + std::to_wstring(fB.traces.size()) +
            L" ghwTraces=" + std::to_wstring(gA.traces.size()) + L"/" + std::to_wstring(gB.traces.size()));
    }

    // ------------------------------------------------------------------------
    // 6. Xilinx FPGA .bit & STM32 DfuSe .dfu Firmware Images + corrupt rejection
    // ------------------------------------------------------------------------
    {
        auto bitA = BuildXilinxBitFile(false);
        auto bitB = BuildXilinxBitFile(true);
        auto dfuA = BuildStm32DfuFile(false);
        auto dfuB = BuildStm32DfuFile(true);

        ParsedEdaBoard xA, xB, uA, uB, badBit, badDfu;
        bool okXA = WriteAndInspect(dir + L"\\a.bit", bitA.data(), bitA.size(), xA);
        bool okXB = WriteAndInspect(dir + L"\\b.bit", bitB.data(), bitB.size(), xB);
        bool okUA = WriteAndInspect(dir + L"\\a.dfu", dfuA.data(), dfuA.size(), uA);
        bool okUB = WriteAndInspect(dir + L"\\b.dfu", dfuB.data(), dfuB.size(), uB);

        const uint8_t garbage[64] = { 0x00, 0x01, 0x02, 0x03, 0x55, 0xAA };
        bool rejBit = !WriteAndInspect(dir + L"\\bad.bit", garbage, sizeof(garbage), badBit);
        bool rejDfu = !WriteAndInspect(dir + L"\\bad.dfu", garbage, sizeof(garbage), badDfu);

        bool pass = okXA && okXB && okUA && okUB &&
                    xA.formatTitle.find(L"xc7a35t") != std::wstring::npos &&
                    xB.formatTitle.find(L"xc7k325t") != std::wstring::npos &&
                    xB.pads.size() > xA.pads.size() &&
                    uA.components.size() == 1 && uB.components.size() == 3 &&
                    uA.formatTitle.find(L"0483:DF11") != std::wstring::npos &&
                    HashEdaGeometry(xA) != HashEdaGeometry(xB) &&
                    HashEdaGeometry(uA) != HashEdaGeometry(uB) &&
                    rejBit && rejDfu;

        tc.Record(pass, L"Stage15.Eda.FpgaBitstreamAndStm32Dfu",
            L"clbPads=" + std::to_wstring(xA.pads.size()) + L"/" + std::to_wstring(xB.pads.size()) +
            L" dfuSectors=" + std::to_wstring(uA.components.size()) + L"/" + std::to_wstring(uB.components.size()));
    }

    // ------------------------------------------------------------------------
    // 7. VESA E-EDID (.edid) & JEDEC DDR4/DDR5 SPD (.spd) + corrupt rejection
    // ------------------------------------------------------------------------
    {
        auto edid1080 = BuildVesaEdidFile(false);
        auto edid1440 = BuildVesaEdidFile(true);
        auto spdDdr4  = BuildJedecSpdFile(false);
        auto spdDdr5  = BuildJedecSpdFile(true);

        ParsedEdaBoard eA, eB, mA, mB, badEdid, badSpd1, badSpd2;
        bool okEA = WriteAndInspect(dir + L"\\1080p.edid", edid1080.data(), edid1080.size(), eA);
        bool okEB = WriteAndInspect(dir + L"\\1440p.edid", edid1440.data(), edid1440.size(), eB);
        bool okMA = WriteAndInspect(dir + L"\\ddr4.spd", spdDdr4.data(), spdDdr4.size(), mA);
        bool okMB = WriteAndInspect(dir + L"\\ddr5.spd", spdDdr5.data(), spdDdr5.size(), mB);

        std::vector<uint8_t> corruptEdid(128, 0x42);
        std::vector<uint8_t> wrongSizeSpd(300, 0x12);
        std::vector<uint8_t> badTypeSpd(512, 0);
        badTypeSpd[2] = 0xFE; // invalid DRAM type
        bool rejEdid = !WriteAndInspect(dir + L"\\bad.edid", corruptEdid.data(), corruptEdid.size(), badEdid);
        bool rejSpd1 = !WriteAndInspect(dir + L"\\bad1.spd", wrongSizeSpd.data(), wrongSizeSpd.size(), badSpd1);
        bool rejSpd2 = !WriteAndInspect(dir + L"\\bad2.spd", badTypeSpd.data(), badTypeSpd.size(), badSpd2);

        bool pass = okEA && okEB && okMA && okMB &&
                    eA.formatTitle.find(L"1920x1080") != std::wstring::npos &&
                    eB.formatTitle.find(L"2560x1440") != std::wstring::npos &&
                    mA.components.size() == 9 &&  // 8 DDR4 chips + 1 SPD EEPROM
                    mB.components.size() == 18 && // 16 DDR5 chips + PMIC + SPD Hub
                    mA.formatTitle.find(L"8 GB DDR4-3200") != std::wstring::npos &&
                    mA.formatTitle.find(L"Samsung") != std::wstring::npos &&
                    mA.formatTitle.find(L"M378A1K43EB2-CWE") != std::wstring::npos &&
                    mB.formatTitle.find(L"32 GB DDR5-5600") != std::wstring::npos &&
                    mB.formatTitle.find(L"SK Hynix") != std::wstring::npos &&
                    mB.formatTitle.find(L"HMCG88AGBUA081N") != std::wstring::npos &&
                    HashEdaGeometry(eA) != HashEdaGeometry(eB) &&
                    HashEdaGeometry(mA) != HashEdaGeometry(mB) &&
                    rejEdid && rejSpd1 && rejSpd2;

        tc.Record(pass, L"Stage15.Eda.VesaEdidAndJedecSpd",
            L"spdChips=" + std::to_wstring(mA.components.size()) + L"/" + std::to_wstring(mB.components.size()) +
            L" | " + mA.formatTitle + L" vs " + mB.formatTitle);
    }

    // ------------------------------------------------------------------------
    // 8. SEMI P39 OASIS (.oas): START offset-flag=0 (12 varuints), POLYGON + CIRCLE
    //    + 2D repetition vs Deflate CBLOCK (recId=34) RECTANGLE + PATH + empty/garbage rejection
    // ------------------------------------------------------------------------
    {
        auto appendOasisSInt = [](std::vector<uint8_t>& b, int64_t v) {
            uint64_t u = (v < 0) ? ((static_cast<uint64_t>(-v) << 1) | 1ULL) : (static_cast<uint64_t>(v) << 1);
            AppendULeb128(b, u);
        };
        auto appendOasisStr = [](std::vector<uint8_t>& b, const std::string& s) {
            AppendULeb128(b, s.size());
            b.insert(b.end(), s.begin(), s.end());
        };
        auto compressRawDeflateFixed = [](const std::vector<uint8_t>& raw) -> std::vector<uint8_t> {
            std::vector<uint8_t> out;
            uint32_t bitBuf = 0;
            int bitCnt = 0;
            auto wrBits = [&](uint32_t val, int n) {
                bitBuf |= (val << bitCnt);
                bitCnt += n;
                while (bitCnt >= 8) {
                    out.push_back(static_cast<uint8_t>(bitBuf & 0xFFu));
                    bitBuf >>= 8;
                    bitCnt -= 8;
                }
            };
            auto wrHuff = [&](uint32_t code, int n) {
                for (int i = n - 1; i >= 0; --i) wrBits((code >> i) & 1u, 1);
            };
            wrBits(1u, 1); // BFINAL = 1
            wrBits(1u, 2); // BTYPE = 01 (Fixed Huffman)
            for (uint8_t sym : raw) {
                if (sym <= 143u) wrHuff(0x30u + sym, 8);
                else wrHuff(0x190u + (sym - 144u), 9);
            }
            wrHuff(0u, 7); // End-of-block (256)
            if (bitCnt > 0) out.push_back(static_cast<uint8_t>(bitBuf & 0xFFu));
            return out;
        };

        // Layout A: START with offset-flag=0 (12 table flag/offset varuints!), CELLNAME + LAYERNAME + PROPERTY,
        //           POLYGON (Manhattan H-first point-list + 2x2 grid repetition) + CIRCLE
        std::vector<uint8_t> oasA;
        {
            const char* magic = "%SEMI-OASIS\r\n";
            oasA.insert(oasA.end(), magic, magic + 13);
            oasA.push_back(1u); // START
            appendOasisStr(oasA, "1.0");
            AppendULeb128(oasA, 0);    // oasis-real type 0: positive integer
            AppendULeb128(oasA, 1000); // 1000 dbu/um
            AppendULeb128(oasA, 0);    // offset-flag == 0 -> 12 varuints follow inside START!
            for (int k = 0; k < 12; ++k) AppendULeb128(oasA, 0);

            // CELLNAME (recId=3, implicit ref 0)
            oasA.push_back(3u);
            appendOasisStr(oasA, "CORE_TOP");

            // LAYERNAME (recId=11): "M1", exact layer 1 (type 3), exact datatype 0 (type 3)
            oasA.push_back(11u);
            appendOasisStr(oasA, "M1");
            AppendULeb128(oasA, 3); AppendULeb128(oasA, 1);
            AppendULeb128(oasA, 3); AppendULeb128(oasA, 0);

            // PROPERTY (recId=28): UUUU=1 value, V=0, C=1, T=0, S=0 => (1<<4)|0x04 = 0x14
            oasA.push_back(28u);
            oasA.push_back(0x14u);
            appendOasisStr(oasA, "TECH_NODE");
            AppendULeb128(oasA, 8);  // prop-value type 8 = varuint
            AppendULeb128(oasA, 28); // 28nm

            // CELL (recId=13, refNum=0)
            oasA.push_back(13u);
            AppendULeb128(oasA, 0);

            // POLYGON (recId=21): info = L(0x01)|D(0x02)|P(0x20)|X(0x10)|Y(0x08)|R(0x04) = 0x3F
            oasA.push_back(21u);
            oasA.push_back(0x3Fu);
            AppendULeb128(oasA, 1); // layer 1
            AppendULeb128(oasA, 0); // datatype 0
            AppendULeb128(oasA, 0); // point-list type 0: Manhattan H-first
            AppendULeb128(oasA, 5); // 5 explicit deltas (6-vertex L-polygon)
            appendOasisSInt(oasA, 20000);
            appendOasisSInt(oasA, 15000);
            appendOasisSInt(oasA, -8000);
            appendOasisSInt(oasA, 10000);
            appendOasisSInt(oasA, -12000);
            appendOasisSInt(oasA, 0); // X = 0
            appendOasisSInt(oasA, 0); // Y = 0
            // Repetition type 1: (xDim+2) x (yDim+2) = 2x2 grid, spacing 35000 dbu
            AppendULeb128(oasA, 1);
            AppendULeb128(oasA, 0);
            AppendULeb128(oasA, 0);
            AppendULeb128(oasA, 35000);
            AppendULeb128(oasA, 35000);

            // CIRCLE (recId=26): info = L(0x01)|D(0x02)|r(0x20)|X(0x10)|Y(0x08) = 0x3B
            oasA.push_back(26u);
            oasA.push_back(0x3Bu);
            AppendULeb128(oasA, 2);     // layer 2
            AppendULeb128(oasA, 0);     // datatype 0
            AppendULeb128(oasA, 6000);  // radius = 6 um
            appendOasisSInt(oasA, 10000);
            appendOasisSInt(oasA, 10000);

            // END (recId=2)
            oasA.push_back(2u);
            appendOasisStr(oasA, "");
            AppendULeb128(oasA, 0);
        }

        // Layout B: START with IEEE float32 unit & offset-flag=1 + Deflate CBLOCK (recId=34) with RECTANGLE + PATH
        std::vector<uint8_t> oasB;
        {
            const char* magic = "%SEMI-OASIS\r\n";
            oasB.insert(oasB.end(), magic, magic + 13);
            oasB.push_back(1u); // START
            appendOasisStr(oasB, "1.0");
            AppendULeb128(oasB, 6); // oasis-real type 6: IEEE 754 float32 LE
            float unitF = 1000.0f;
            uint8_t fbytes[4] = {};
            std::memcpy(fbytes, &unitF, 4);
            oasB.insert(oasB.end(), fbytes, fbytes + 4);
            AppendULeb128(oasB, 1); // offset-flag == 1 (table offsets in END, not START)

            std::vector<uint8_t> inner;
            // CELL by inline name (recId=14)
            inner.push_back(14u);
            appendOasisStr(inner, "CBLOCK_CELL");

            // RECTANGLE (recId=20): info = W(0x40)|H(0x20)|X(0x10)|Y(0x08)|D(0x02)|L(0x01) = 0x7B
            inner.push_back(20u);
            inner.push_back(0x7Bu);
            AppendULeb128(inner, 1);
            AppendULeb128(inner, 0);
            AppendULeb128(inner, 42000);
            AppendULeb128(inner, 24000);
            appendOasisSInt(inner, 4000);
            appendOasisSInt(inner, 6000);

            // PATH (recId=22): info = halfW(0x40)|P(0x20)|X(0x10)|Y(0x08)|D(0x02)|L(0x01) = 0x7B
            inner.push_back(22u);
            inner.push_back(0x7Bu);
            AppendULeb128(inner, 2);
            AppendULeb128(inner, 0);
            AppendULeb128(inner, 1500); // half-width = 1.5 um -> width = 3.0 um
            AppendULeb128(inner, 0);    // Manhattan H-first
            AppendULeb128(inner, 2);    // 2 deltas (3 vertices)
            appendOasisSInt(inner, 55000);
            appendOasisSInt(inner, 32000);
            appendOasisSInt(inner, 0);
            appendOasisSInt(inner, 0);

            std::vector<uint8_t> compInner = compressRawDeflateFixed(inner);
            oasB.push_back(34u); // CBLOCK
            AppendULeb128(oasB, 0); // comp-type 0 = RFC 1951 raw Deflate
            AppendULeb128(oasB, inner.size());
            AppendULeb128(oasB, compInner.size());
            oasB.insert(oasB.end(), compInner.begin(), compInner.end());

            oasB.push_back(2u); // END
            for (int k = 0; k < 12; ++k) AppendULeb128(oasB, 0);
            appendOasisStr(oasB, "");
            AppendULeb128(oasB, 0);
        }

        // Empty START+END OASIS (must NOT synthesize fake 4x4 checkerboard!)
        std::vector<uint8_t> oasEmpty;
        {
            const char* magic = "%SEMI-OASIS\r\n";
            oasEmpty.insert(oasEmpty.end(), magic, magic + 13);
            oasEmpty.push_back(1u);
            appendOasisStr(oasEmpty, "1.0");
            AppendULeb128(oasEmpty, 0);
            AppendULeb128(oasEmpty, 1000);
            AppendULeb128(oasEmpty, 1);
            oasEmpty.push_back(2u);
            for (int k = 0; k < 12; ++k) AppendULeb128(oasEmpty, 0);
            appendOasisStr(oasEmpty, "");
            AppendULeb128(oasEmpty, 0);
        }

        ParsedEdaBoard bOasA, bOasB, bOasEmpty, bOasGarbage;
        bool okOasA = WriteAndInspect(dir + L"\\layout_a.oas", oasA.data(), oasA.size(), bOasA);
        bool okOasB = WriteAndInspect(dir + L"\\layout_b.oas", oasB.data(), oasB.size(), bOasB);
        bool rejEmpty = !WriteAndInspect(dir + L"\\empty.oas", oasEmpty.data(), oasEmpty.size(), bOasEmpty);
        const char* badOas = "NOT A SEMI P39 OASIS STREAM";
        bool rejGarbage = !WriteAndInspect(dir + L"\\garbage.oas", badOas, std::strlen(badOas), bOasGarbage);

        // Layout A has 2x2 * 6 polygon edges = 24 traces + 1 circle; Layout B has 4 rect edges + 2 path segments = 6 traces, 0 circles
        bool passOas = okOasA && okOasB &&
                       bOasA.traces.size() >= 24 && bOasA.circles.size() == 1 &&
                       bOasB.traces.size() >= 6 && bOasB.circles.empty() &&
                       HashEdaGeometry(bOasA) != HashEdaGeometry(bOasB) &&
                       rejEmpty && rejGarbage;

        tc.Record(passOas, L"Stage15.Eda.SemiOasis_StartOffsets_Repetition_And_DeflateCBlock",
            L"oasATraces=" + std::to_wstring(bOasA.traces.size()) +
            L" oasBTraces=" + std::to_wstring(bOasB.traces.size()) +
            L" rejEmpty=" + std::to_wstring(rejEmpty));
    }

    // ------------------------------------------------------------------------
    // 9. Intel / Altera FPGA Bitstream (.sof / .pof): Packet Directory + Real Sector Density
    // ------------------------------------------------------------------------
    {
        auto buildAlteraSof = [](bool isPof, const std::string& devPart, const std::vector<uint8_t>& sramPayload) {
            std::vector<uint8_t> out;
            const char* m = isPof ? "POF" : "SOF";
            out.push_back(static_cast<uint8_t>(m[0]));
            out.push_back(static_cast<uint8_t>(m[1]));
            out.push_back(static_cast<uint8_t>(m[2]));
            out.push_back(0u);
            uint8_t cnt[4] = {};
            PutU32LE(cnt, 3u);
            out.insert(out.end(), cnt, cnt + 4);

            auto appendPkt = [&](uint16_t tag, const void* ptr, uint32_t len) {
                uint8_t hdr[6] = {};
                PutU16LE(hdr + 0, tag);
                PutU32LE(hdr + 2, len);
                out.insert(out.end(), hdr, hdr + 6);
                const auto* bp = static_cast<const uint8_t*>(ptr);
                out.insert(out.end(), bp, bp + len);
            };
            const std::string tool = "Quartus Prime Version 23.1.0";
            appendPkt(1u, tool.data(), static_cast<uint32_t>(tool.size() + 1));
            appendPkt(2u, devPart.data(), static_cast<uint32_t>(devPart.size() + 1));
            appendPkt(6u, sramPayload.data(), static_cast<uint32_t>(sramPayload.size()));
            return out;
        };

        std::vector<uint8_t> cfgSparse(128, 0x00u);
        for (size_t i = 0; i < 32; ++i) cfgSparse[i] = 0x15u; // Only top sectors configured

        std::vector<uint8_t> cfgDense(576, 0xFFu);
        for (size_t i = 0; i < cfgDense.size(); ++i) cfgDense[i] = (i % 3 == 0) ? 0xFFu : 0x7Eu;

        std::vector<uint8_t> cfgPof(256, 0xFFu); // In Flash POF, 0xFF = erased, 0x00 = programmed!
        for (size_t i = 64; i < 192; ++i) cfgPof[i] = 0x03u;

        auto sof1 = buildAlteraSof(false, "EP4CE22F17C6", cfgSparse);
        auto sof2 = buildAlteraSof(false, "5CSEMA5F31C6", cfgDense);
        auto pof1 = buildAlteraSof(true,  "10M50DAF484",  cfgPof);

        ParsedEdaBoard s1, s2, p1, badSof1, badSof2;
        bool okS1 = WriteAndInspect(dir + L"\\cyclone4.sof", sof1.data(), sof1.size(), s1);
        bool okS2 = WriteAndInspect(dir + L"\\cyclone5.sof", sof2.data(), sof2.size(), s2);
        bool okP1 = WriteAndInspect(dir + L"\\max10.pof",    pof1.data(), pof1.size(), p1);

        const uint8_t nonAltera[64] = { 'B', 'A', 'D', '!', 1, 0, 0, 0, 2, 0, 8, 0, 0, 0 };
        std::vector<uint8_t> truncSof = { 'S', 'O', 'F', 0, 2, 0, 0, 0, 2, 0, 0xFF, 0x0F, 0, 0, 'E', 'P' };
        bool rejSofGarbage = !WriteAndInspect(dir + L"\\garbage.sof", nonAltera, sizeof(nonAltera), badSof1);
        bool rejSofTrunc   = !WriteAndInspect(dir + L"\\trunc.sof", truncSof.data(), truncSof.size(), badSof2);

        bool passSof = okS1 && okS2 && okP1 &&
                       s1.formatTitle.find(L"EP4CE22F17C6") != std::wstring::npos &&
                       s2.formatTitle.find(L"5CSEMA5F31C6") != std::wstring::npos &&
                       p1.formatTitle.find(L"10M50DAF484") != std::wstring::npos &&
                       s2.pads.size() > s1.pads.size() &&
                       HashEdaGeometry(s1) != HashEdaGeometry(s2) &&
                       HashEdaGeometry(s1) != HashEdaGeometry(p1) &&
                       rejSofGarbage && rejSofTrunc;

        tc.Record(passSof, L"Stage15.Eda.AlteraBitstream_SofAndPof_PacketFloorplan",
            L"sofPads=" + std::to_wstring(s1.pads.size()) + L"/" + std::to_wstring(s2.pads.size()) +
            L" pofPads=" + std::to_wstring(p1.pads.size()));
    }

    // ------------------------------------------------------------------------
    // 10. Digital Oscilloscope Waveforms: Tektronix :WFM#003 INT16 & Tektronix .isf MSB
    // ------------------------------------------------------------------------
    {
        // Build Tektronix WFM#003 (838B header + 128 x INT16 sine capture)
        std::vector<uint8_t> wfm(838 + 128 * 2, 0);
        wfm[0] = 0x0Fu; wfm[1] = 0x0Fu; // Little-endian marker
        std::memcpy(wfm.data() + 2, ":WFM#003", 8);
        wfm[10] = 4u;
        PutU32LE(wfm.data() + 11, static_cast<uint32_t>(wfm.size() - 15));
        wfm[15] = 2u; // 2 bytes per point (INT16)
        PutU32LE(wfm.data() + 16, 838u); // byteOffsetToBeginningOfCurveBuffer
        double dimScale = 0.001; // 1 mV / LSB
        double dimOff   = 0.0;
        std::memcpy(wfm.data() + 168, &dimScale, 8);
        std::memcpy(wfm.data() + 176, &dimOff, 8);
        PutI32LE(wfm.data() + 240, 0); // EXPLICIT_INT16
        PutU32LE(wfm.data() + (838 - 30) + 14, 0u);   // dataStartOffset
        PutU32LE(wfm.data() + (838 - 30) + 18, 256u); // postchargeStartOffset
        for (int i = 0; i < 128; ++i) {
            int16_t sv = static_cast<int16_t>(std::llround(2500.0 * std::sin(i * 0.15)));
            PutU16LE(wfm.data() + 838 + i * 2, static_cast<uint16_t>(sv));
        }

        // Build Tektronix ISF (BYT_NR 2; BYT_OR MSB; CURVE #3128 + 64 MSB INT16 ramp/step samples)
        std::string isfHdr = ":WFMPRE:BYT_NR 2;BN_FMT RI;BYT_OR MSB;NR_PT 64;YMULT 0.005;YOFF 0.0;YZERO 0.0;:CURVE #3128";
        std::vector<uint8_t> isf(isfHdr.begin(), isfHdr.end());
        for (int i = 0; i < 64; ++i) {
            int16_t rv = static_cast<int16_t>((i < 32) ? (-1200 + i * 80) : (1200 - (i - 32) * 40));
            uint8_t be2[2] = {};
            PutU16BE(be2, static_cast<uint16_t>(rv));
            isf.push_back(be2[0]);
            isf.push_back(be2[1]);
        }

        ParsedEdaBoard bWfm, bIsf, badWfm, badIsf;
        bool okWfm = WriteAndInspect(dir + L"\\ch1_sine.wfm", wfm.data(), wfm.size(), bWfm);
        bool okIsf = WriteAndInspect(dir + L"\\ch2_ramp.isf", isf.data(), isf.size(), bIsf);

        std::vector<uint8_t> randGarbage(900, 0x37u);
        const char* txtGarbage = "Random text file with numbers 10, 20, 30 that is not an oscilloscope capture";
        bool rejWfm = !WriteAndInspect(dir + L"\\bad.wfm", randGarbage.data(), randGarbage.size(), badWfm);
        bool rejIsf = !WriteAndInspect(dir + L"\\bad.isf", txtGarbage, std::strlen(txtGarbage), badIsf);

        bool passScope = okWfm && okIsf &&
                         bWfm.formatTitle.find(L"WFM#003") != std::wstring::npos &&
                         bIsf.formatTitle.find(L"ISF") != std::wstring::npos &&
                         bWfm.traces.size() > 100 &&
                         bIsf.traces.size() > 50 &&
                         HashEdaGeometry(bWfm) != HashEdaGeometry(bIsf) &&
                         rejWfm && rejIsf;

        tc.Record(passScope, L"Stage15.Eda.Oscilloscope_TekWfm003_And_IsfMsb",
            L"wfmTraces=" + std::to_wstring(bWfm.traces.size()) +
            L" isfTraces=" + std::to_wstring(bIsf.traces.size()));
    }

    RemoveDirectoryW(dir.c_str());
}

} // namespace Explorer2::SelfTestSuite
