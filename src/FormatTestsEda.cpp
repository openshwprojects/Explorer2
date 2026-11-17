// FormatTestsEda.cpp: self-tests of the electronics / PCB / EDA readers (EdaViewer, formats/), stage 15 ("15.eda" runs these alone)
#include "Crc32.h"
#include "SelfTestCommon.h"
#include "TestBytes.h"
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

using namespace ::Explorer2::TestBytes; // byte writers (TestBytes.h)

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
    PutU32LE(tail, Checksum::Crc32(raw.data(), raw.size()));
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
        cd.crc = Checksum::Crc32(reinterpret_cast<const uint8_t*>(f.second.data()), f.second.size());
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
    PutU32LE(sfx + 12, Checksum::Crc32(dfu.data(), dfu.size()));
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

    // Gerber job file read as JSON: "Path" names the files (the spec's key), no outline is drawn without a Size,
    // copper layers are told apart by count, the project revision reaches the title, broken JSON is turned down
    {
        const std::string job =
            R"({"Header":{"GenerationSoftware":{"Vendor":"KiCad","Application":"Pcbnew","Version":"8.0"}},)"
            R"("GeneralSpecs":{"ProjectId":{"Name":"Probe","GUID":"1","Revision":"A"},"LayerNumber":2,"BoardThickness":1.6},)"
            R"("FilesAttributes":[{"Path":"probe-F_Cu.gtl","FileFunction":"Copper,L1,Top"}],)"
            R"("MaterialStackup":[{"Type":"Copper","Thickness":0.035,"Name":"F.Cu"},{"Type":"Dielectric","Thickness":1.5,"Material":"FR4","Name":"Core"},)"
            R"({"Type":"Copper","Thickness":0.035,"Name":"B.Cu"}]})";
        const std::string broken = R"({"GeneralSpecs":{"Size":{"X":10,"Y":10}})";
        ParsedEdaBoard jb, bad;
        bool ok = WriteAndInspect(dir + L"\\probe.gbrjob", job.data(), job.size(), jb);
        bool sawFile = false, sawEdge = false, sawTop = false, sawBottom = false;
        for (const auto& l : jb.labels) {
            if (l.text.find(L"probe-F_Cu.gtl [Copper,L1,Top]") != std::wstring::npos) sawFile = true;
        }
        for (const auto& t : jb.traces) {
            if (t.layer == EdaLayerId::EdgeCuts) sawEdge = true;
            if (t.layer == EdaLayerId::TopCopper) sawTop = true;
            if (t.layer == EdaLayerId::BottomCopper) sawBottom = true;
        }
        bool badRejected = !WriteAndInspect(dir + L"\\broken.gbrjob", broken.data(), broken.size(), bad);
        tc.Record(ok && sawFile && !sawEdge && sawTop && sawBottom && jb.formatTitle.find(L"Probe A") != std::wstring::npos && badRejected,
                  L"Stage15.Eda.GerberJob_JsonPathKeyNoInventedOutline", jb.formatTitle);
    }

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

    // ------------------------------------------------------------------------
    // 11. Xilinx XAPP503 Binary JTAG (.xsvf) & IEEE 1149.1 ASCII (.svf)
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };
        auto appendBE32 = [](std::vector<uint8_t>& v, uint32_t x) {
            uint8_t b[4] = {};
            PutU32BE(b, x);
            v.insert(v.end(), b, b + 4);
        };
        auto appendBE16 = [](std::vector<uint8_t>& v, uint16_t x) {
            uint8_t b[2] = {};
            PutU16BE(b, x);
            v.insert(v.end(), b, b + 2);
        };

        // Program A: XCOMMENT + XSTATE + XENDIR + XENDDR + XREPEAT + XSIR + XSDRSIZE + XTDOMASK + XSDRTDO + XRUNTEST + XCOMPLETE
        std::vector<uint8_t> xsvfA;
        xsvfA.push_back(0x16u); // XCOMMENT
        const char* cmtA = "XC7A35T_IDCODE_SCAN";
        xsvfA.insert(xsvfA.end(), cmtA, cmtA + std::strlen(cmtA) + 1);
        xsvfA.insert(xsvfA.end(), { 0x12u, 0x00u }); // XSTATE Test-Logic-Reset
        xsvfA.insert(xsvfA.end(), { 0x12u, 0x01u }); // XSTATE Run-Test/Idle
        xsvfA.insert(xsvfA.end(), { 0x13u, 0x00u }); // XENDIR Idle
        xsvfA.insert(xsvfA.end(), { 0x14u, 0x00u }); // XENDDR Idle
        xsvfA.insert(xsvfA.end(), { 0x07u, 0x20u }); // XREPEAT 32
        xsvfA.insert(xsvfA.end(), { 0x02u, 0x06u, 0x09u }); // XSIR 6-bit IDCODE (0x09)
        xsvfA.push_back(0x08u); appendBE32(xsvfA, 32u);     // XSDRSIZE 32 bits
        xsvfA.insert(xsvfA.end(), { 0x01u, 0x0Fu, 0xFFu, 0xFFu, 0xFFu }); // XTDOMASK
        xsvfA.insert(xsvfA.end(), { 0x09u,
                                    0x00u, 0x00u, 0x00u, 0x00u,           // XSDRTDO TDI
                                    0x03u, 0x62u, 0xD0u, 0x93u });        // XSDRTDO TDO expected
        xsvfA.push_back(0x04u); appendBE32(xsvfA, 4u);      // XRUNTEST 4 clocks
        xsvfA.push_back(0x00u);                             // XCOMPLETE

        // Program B: XCOMMENT + XSTATE + XSIR2 + XSDRSIZE + XTDOMASK + XSDRB/XSDRC/XSDRE + XSETSDRMASKS + XSDRINC + XWAIT + XCOMPLETE
        std::vector<uint8_t> xsvfB;
        xsvfB.push_back(0x16u); // XCOMMENT
        const char* cmtB = "ZYNQ_CFG_BURST";
        xsvfB.insert(xsvfB.end(), cmtB, cmtB + std::strlen(cmtB) + 1);
        xsvfB.insert(xsvfB.end(), { 0x12u, 0x00u, 0x12u, 0x01u });
        xsvfB.push_back(0x15u); appendBE16(xsvfB, 12u); xsvfB.insert(xsvfB.end(), { 0x0Au, 0x53u }); // XSIR2 12-bit
        xsvfB.push_back(0x08u); appendBE32(xsvfB, 16u); // XSDRSIZE 16 bits
        xsvfB.insert(xsvfB.end(), { 0x01u, 0xFFu, 0xFFu }); // XTDOMASK
        xsvfB.insert(xsvfB.end(), { 0x0Cu, 0xAAu, 0x55u }); // XSDRB
        xsvfB.insert(xsvfB.end(), { 0x0Du, 0xF0u, 0x0Fu }); // XSDRC
        xsvfB.insert(xsvfB.end(), { 0x0Eu, 0x3Cu, 0xC3u }); // XSDRE
        xsvfB.insert(xsvfB.end(), { 0x0Au, 0x00u, 0xFFu, 0x00u, 0x0Fu }); // XSETSDRMASKS (dataMaskOnes = 4 -> 1B per inc)
        xsvfB.insert(xsvfB.end(), { 0x0Bu, 0x12u, 0x30u, 0x02u, 0x05u, 0x0Au }); // XSDRINC (2 increments x 1B)
        xsvfB.insert(xsvfB.end(), { 0x17u, 0x01u, 0x01u }); appendBE32(xsvfB, 100u); // XWAIT
        xsvfB.push_back(0x00u); // XCOMPLETE

        ParsedEdaBoard bxA, bxB, bSvf, badX1, badX2, badX3, badX4, badX5, badX6, badSvf;
        bool okXA = WriteAndInspect(dir + L"\\fpga_idcode.xsvf", xsvfA.data(), xsvfA.size(), bxA);
        bool okXB = WriteAndInspect(dir + L"\\zynq_burst.xsvf",  xsvfB.data(), xsvfB.size(), bxB);

        const std::string validSvf =
            "! IEEE 1149.1 SVF\n"
            "TRST OFF;\n"
            "ENDIR IDLE;\n"
            "ENDDR IDLE;\n"
            "STATE RESET IDLE;\n"
            "SIR 6 TDI (09);\n"
            "SDR 32 TDI (00000000) TDO (0362D093) MASK (0FFFFFFF);\n"
            "RUNTEST IDLE 4 TCK;\n";
        bool okSvf = WriteAndInspect(dir + L"\\scan.svf", validSvf.data(), validSvf.size(), bSvf);

        // Strict negative rejection: ASCII text, random garbage, unknown opcode 0x05, truncated operand, unterminated XCOMMENT, invalid XSTATE
        const char* asciiNonXsvf = "HELLO WORLD THIS IS NOT XSVF BYTECODE";
        const uint8_t randBytes[16] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x99, 0x42, 0x77, 0x88, 0x31, 0x64 };
        const uint8_t unknownOp[]   = { 0x12u, 0x01u, 0x05u, 0x00u };
        const uint8_t truncSdr[]    = { 0x08u, 0x00u, 0x00u, 0x00u, 0x20u, 0x09u, 0xAAu, 0xBBu };
        const uint8_t untermCmt[]   = { 0x16u, 'N', 'O', '_', 'N', 'U', 'L' };
        const uint8_t badTapSt[]    = { 0x12u, 0x1Fu, 0x00u };

        bool rejXsvf = !WriteAndInspect(dir + L"\\ascii.xsvf",   asciiNonXsvf, std::strlen(asciiNonXsvf), badX1) &&
                       !WriteAndInspect(dir + L"\\random.xsvf",  randBytes, sizeof(randBytes), badX2) &&
                       !WriteAndInspect(dir + L"\\bad_op.xsvf",  unknownOp, sizeof(unknownOp), badX3) &&
                       !WriteAndInspect(dir + L"\\trunc.xsvf",   truncSdr, sizeof(truncSdr), badX4) &&
                       !WriteAndInspect(dir + L"\\unterm.xsvf",  untermCmt, sizeof(untermCmt), badX5) &&
                       !WriteAndInspect(dir + L"\\bad_st.xsvf",  badTapSt, sizeof(badTapSt), badX6) &&
                       !WriteAndInspect(dir + L"\\garbage.svf",  asciiNonXsvf, std::strlen(asciiNonXsvf), badSvf);

        bool passXsvf = okXA && okXB && okSvf &&
                        bxA.netCount == 4 && bxB.netCount == 4 && bSvf.netCount == 4 &&
                        hasLabelSubstr(bxA, L"TCK") && hasLabelSubstr(bxA, L"TMS") &&
                        hasLabelSubstr(bxA, L"TDI") && hasLabelSubstr(bxA, L"TDO") &&
                        hasLabelSubstr(bxA, L"XC7A35T_IDCODE_SCAN") &&
                        hasLabelSubstr(bxB, L"ZYNQ_CFG_BURST") &&
                        bxA.traces.size() > 80 && bxB.traces.size() > 80 &&
                        HashEdaGeometry(bxA) != HashEdaGeometry(bxB) &&
                        rejXsvf;

        tc.Record(passXsvf, L"Stage15.Eda.XilinxXsvf_Xapp503BytecodeAnd4LaneJtag",
            L"xsvfATraces=" + std::to_wstring(bxA.traces.size()) +
            L" xsvfBTraces=" + std::to_wstring(bxB.traces.size()));
    }

    // ------------------------------------------------------------------------
    // 12. Saleae Logic 2 Capture (.sal — Multi-Channel Digital & Analog <SALEAE>)
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        auto buildSalDigitalV0 = [](uint32_t initState, double beginT, double endT, const std::vector<double>& trans) -> std::string {
            std::vector<uint8_t> buf(44 + trans.size() * 8, 0);
            std::memcpy(buf.data(), "<SALEAE>", 8);
            PutI32LE(buf.data() + 8, 0);  // version = 0
            PutI32LE(buf.data() + 12, 0); // type = 0 (digital)
            PutU32LE(buf.data() + 16, initState);
            std::memcpy(buf.data() + 20, &beginT, 8);
            std::memcpy(buf.data() + 28, &endT, 8);
            uint64_t nt = static_cast<uint64_t>(trans.size());
            PutU32LE(buf.data() + 36, static_cast<uint32_t>(nt & 0xFFFFFFFFULL));
            PutU32LE(buf.data() + 40, static_cast<uint32_t>(nt >> 32));
            for (size_t i = 0; i < trans.size(); ++i) {
                std::memcpy(buf.data() + 44 + i * 8, &trans[i], 8);
            }
            return std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
        };

        auto buildSalAnalogV0 = [](double beginT, uint64_t sampleRate, uint64_t downsample, const std::vector<float>& samples) -> std::string {
            std::vector<uint8_t> buf(48 + samples.size() * 4, 0);
            std::memcpy(buf.data(), "<SALEAE>", 8);
            PutI32LE(buf.data() + 8, 0);  // version = 0
            PutI32LE(buf.data() + 12, 1); // type = 1 (analog)
            std::memcpy(buf.data() + 16, &beginT, 8);
            PutU32LE(buf.data() + 24, static_cast<uint32_t>(sampleRate & 0xFFFFFFFFULL));
            PutU32LE(buf.data() + 28, static_cast<uint32_t>(sampleRate >> 32));
            PutU32LE(buf.data() + 32, static_cast<uint32_t>(downsample & 0xFFFFFFFFULL));
            PutU32LE(buf.data() + 36, static_cast<uint32_t>(downsample >> 32));
            uint64_t ns = static_cast<uint64_t>(samples.size());
            PutU32LE(buf.data() + 40, static_cast<uint32_t>(ns & 0xFFFFFFFFULL));
            PutU32LE(buf.data() + 44, static_cast<uint32_t>(ns >> 32));
            for (size_t i = 0; i < samples.size(); ++i) {
                std::memcpy(buf.data() + 48 + i * 4, &samples[i], 4);
            }
            return std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
        };

        // Capture A: 3 digital channels (SCK clock, MOSI data, idle-high CS_N with 0 transitions) + 1 analog sine wave (3.3V rail)
        std::vector<double> sckEdges;
        for (int i = 1; i <= 16; ++i) sckEdges.push_back(i * 0.00005);
        std::vector<double> mosiEdges = { 0.00008, 0.00022, 0.00038, 0.00055, 0.00071, 0.00088 };
        std::vector<float> vddSine(64);
        for (size_t i = 0; i < vddSine.size(); ++i) {
            vddSine[i] = 3.30f + 0.25f * static_cast<float>(std::sin(static_cast<double>(i) * 0.22));
        }

        const std::string metaJsonA =
            "{\"version\":16,\"data\":{\"sampleRate\":{\"digital\":10000000,\"analog\":1250000},\"channels\":["
            "{\"type\":\"Digital\",\"index\":0,\"name\":\"SPI_SCK\"},"
            "{\"type\":\"Digital\",\"index\":1,\"name\":\"SPI_MOSI\"},"
            "{\"type\":\"Digital\",\"index\":2,\"name\":\"SPI_CS_N\"},"
            "{\"type\":\"Analog\",\"index\":0,\"name\":\"VDD_3V3\"}"
            "]}}";

        auto salZipA = BuildStoredZip({
            { "meta.json",     metaJsonA },
            { "digital-2.bin", buildSalDigitalV0(1u, 0.0, 0.001, {}) }, // unsorted order + 0 transitions valid header
            { "digital-0.bin", buildSalDigitalV0(0u, 0.0, 0.001, sckEdges) },
            { "digital-1.bin", buildSalDigitalV0(0u, 0.0, 0.001, mosiEdges) },
            { "analog-0.bin",  buildSalAnalogV0(0.0, 64000ULL, 1ULL, vddSine) },
        });

        // Capture B: 1 digital UART channel (rowsSettings style meta.json) + 2 analog channels (ramp + cosine)
        std::vector<double> uartEdges = { 0.00010, 0.00020, 0.00040, 0.00050, 0.00080, 0.00090 };
        std::vector<float> anaRamp(48), anaCos(48);
        for (size_t i = 0; i < 48; ++i) {
            anaRamp[i] = -2.50f + 5.00f * (static_cast<float>(i) / 47.0f);
            anaCos[i]  = 2.50f + 2.50f * static_cast<float>(std::cos(static_cast<double>(i) * 0.18));
        }
        const std::string metaJsonB =
            "{\"rowsSettings\":["
            "{\"name\":\"UART_TX\",\"channel\":{\"category\":\"Hardware\",\"type\":\"Digital\",\"deviceChannel\":0}},"
            "{\"name\":\"DAC_RAMP\",\"channel\":{\"category\":\"Hardware\",\"type\":\"Analog\",\"deviceChannel\":0}},"
            "{\"name\":\"REF_COS\",\"channel\":{\"category\":\"Hardware\",\"type\":\"Analog\",\"deviceChannel\":1}}"
            "]}";

        auto salZipB = BuildStoredZip({
            { "meta.json",     metaJsonB },
            { "digital-0.bin", buildSalDigitalV0(1u, 0.0, 0.0012, uartEdges) },
            { "analog-0.bin",  buildSalAnalogV0(0.0, 40000ULL, 1ULL, anaRamp) },
            { "analog-1.bin",  buildSalAnalogV0(0.0, 40000ULL, 1ULL, anaCos) },
        });

        // Strict negative cases: meta.json-only ZIP, missing <SALEAE> magic, wrong type field, truncated digital, truncated analog, non-ZIP
        auto salMetaOnly = BuildStoredZip({ { "meta.json", metaJsonA } });

        std::string badMagicDig = buildSalDigitalV0(0u, 0.0, 0.001, sckEdges);
        std::memcpy(badMagicDig.data(), "NOT_SAL!", 8);
        auto salBadMagic = BuildStoredZip({ { "meta.json", metaJsonA }, { "digital-0.bin", badMagicDig } });

        std::string wrongTypeDig = buildSalDigitalV0(0u, 0.0, 0.001, sckEdges);
        PutI32LE(reinterpret_cast<uint8_t*>(wrongTypeDig.data()) + 12, 1); // analog type inside digital-0.bin
        auto salWrongType = BuildStoredZip({ { "digital-0.bin", wrongTypeDig } });

        std::string truncDig = buildSalDigitalV0(0u, 0.0, 0.001, sckEdges);
        truncDig.resize(52); // claims 16 transitions (44 + 128B), truncated at 52B
        auto salTruncDig = BuildStoredZip({ { "digital-0.bin", truncDig } });

        std::string truncAna = buildSalAnalogV0(0.0, 64000ULL, 1ULL, vddSine);
        truncAna.resize(64); // claims 64 float32 samples (48 + 256B), truncated at 64B
        auto salTruncAna = BuildStoredZip({ { "analog-0.bin", truncAna } });

        ParsedEdaBoard bsA, bsB, badS1, badS2, badS3, badS4, badS5, badS6;
        bool okSalA = WriteAndInspect(dir + L"\\spi_mixed.sal", salZipA.data(), salZipA.size(), bsA);
        bool okSalB = WriteAndInspect(dir + L"\\uart_dual_ana.sal", salZipB.data(), salZipB.size(), bsB);

        const uint8_t rawNonZip[32] = { '<', 'S', 'A', 'L', 'E', 'A', 'E', '>', 0, 0, 0, 0 };
        bool rejSal = !WriteAndInspect(dir + L"\\meta_only.sal",  salMetaOnly.data(), salMetaOnly.size(), badS1) &&
                      !WriteAndInspect(dir + L"\\bad_magic.sal",  salBadMagic.data(), salBadMagic.size(), badS2) &&
                      !WriteAndInspect(dir + L"\\wrong_type.sal", salWrongType.data(), salWrongType.size(), badS3) &&
                      !WriteAndInspect(dir + L"\\trunc_dig.sal",  salTruncDig.data(), salTruncDig.size(), badS4) &&
                      !WriteAndInspect(dir + L"\\trunc_ana.sal",  salTruncAna.data(), salTruncAna.size(), badS5) &&
                      !WriteAndInspect(dir + L"\\not_zip.sal",    rawNonZip, sizeof(rawNonZip), badS6);

        bool passSal = okSalA && okSalB &&
                       bsA.netCount == 4 && bsB.netCount == 3 &&
                       bsA.formatTitle.find(L"3D, 1A") != std::wstring::npos &&
                       bsB.formatTitle.find(L"1D, 2A") != std::wstring::npos &&
                       hasLabelSubstr(bsA, L"D0:SPI_SCK") &&
                       hasLabelSubstr(bsA, L"D1:SPI_MOSI") &&
                       hasLabelSubstr(bsA, L"D2:SPI_CS_N") &&
                       hasLabelSubstr(bsA, L"A0:VDD_3V3") &&
                       hasLabelSubstr(bsA, L"V]") &&
                       hasLabelSubstr(bsB, L"D0:UART_TX") &&
                       hasLabelSubstr(bsB, L"A0:DAC_RAMP") &&
                       hasLabelSubstr(bsB, L"A1:REF_COS") &&
                       bsA.traces.size() > 80 && bsB.traces.size() > 80 &&
                       HashEdaGeometry(bsA) != HashEdaGeometry(bsB) &&
                       rejSal;

        tc.Record(passSal, L"Stage15.Eda.SaleaeLogic2_MultiChannelDigitalAndAnalogSal",
            L"salATraces=" + std::to_wstring(bsA.traces.size()) +
            L" salBTraces=" + std::to_wstring(bsB.traces.size()));
    }

    // ------------------------------------------------------------------------
    // 13. ASAM MDF 3.30 (.mdf) & MDF 4.10 (.mf4) ECU Telemetry + ##CC + ##DZ Transpose
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        auto buildMdf33 = [](uint32_t numRecs) -> std::vector<uint8_t> {
            // Offsets: ID=0(64), HD=64(164), DG=228(28), CG=256(26), CN0=282(228), CN1=510(228), CN2=738(228), CC=966(62), TX=1028(16), DATA=1044
            std::vector<uint8_t> buf(1044u + numRecs * 8u, 0);
            std::memcpy(buf.data() + 0,  "MDF     ", 8);
            std::memcpy(buf.data() + 8,  "3.30    ", 8);
            std::memcpy(buf.data() + 16, "Explorer", 8);
            PutU16LE(buf.data() + 24, 0u);   // Little-Endian
            PutU16LE(buf.data() + 28, 330u); // Version 3.30

            // HD at 64 (size 164)
            buf[64] = 'H'; buf[65] = 'D';
            PutU16LE(buf.data() + 66, 164u);
            PutU32LE(buf.data() + 68, 228u); // firstDG
            PutU16LE(buf.data() + 80, 1u);   // numDG

            // DG at 228 (size 28)
            buf[228] = 'D'; buf[229] = 'G';
            PutU16LE(buf.data() + 230, 28u);
            PutU32LE(buf.data() + 232, 0u);  // nextDG
            PutU32LE(buf.data() + 236, 256u);// firstCG
            PutU32LE(buf.data() + 244, numRecs > 0u ? 1044u : 0u); // dataBlock
            PutU16LE(buf.data() + 248, 1u);  // numCG
            PutU16LE(buf.data() + 250, 0u);  // numRecIds = 0 (sorted)

            // CG at 256 (size 26)
            buf[256] = 'C'; buf[257] = 'G';
            PutU16LE(buf.data() + 258, 26u);
            PutU32LE(buf.data() + 260, 0u);   // nextCG
            PutU32LE(buf.data() + 264, 282u); // firstCN
            PutU16LE(buf.data() + 274, 3u);   // numCh
            PutU16LE(buf.data() + 276, 8u);   // recordByteSize = 8 (float32 t + uint16 rpm + sint16 boost)
            PutU32LE(buf.data() + 278, numRecs);

            auto writeCn = [&](size_t off, uint32_t nextCn, uint32_t convCc, uint16_t cnType,
                               const char* shortNm, uint16_t startBit, uint16_t nBits, uint16_t sigDt, uint32_t longTx) {
                buf[off] = 'C'; buf[off + 1] = 'N';
                PutU16LE(buf.data() + off + 2, 228u);
                PutU32LE(buf.data() + off + 4, nextCn);
                PutU32LE(buf.data() + off + 8, convCc);
                PutU16LE(buf.data() + off + 24, cnType);
                std::memcpy(buf.data() + off + 26, shortNm, std::strlen(shortNm));
                PutU16LE(buf.data() + off + 186, startBit);
                PutU16LE(buf.data() + off + 188, nBits);
                PutU16LE(buf.data() + off + 190, sigDt);
                PutU32LE(buf.data() + off + 218, longTx);
            };
            writeCn(282u, 510u, 0u,   1u, "t_master",  0u,  32u, 2u, 0u);
            writeCn(510u, 738u, 966u, 0u, "rpm_short", 32u, 16u, 0u, 1028u);
            writeCn(738u, 0u,   0u,   0u, "Boost_kPa", 48u, 16u, 1u, 0u);

            // CC at 966 (size 62): linear P1=500.0, P2=0.25, unit="rpm"
            buf[966] = 'C'; buf[967] = 'C';
            PutU16LE(buf.data() + 968, 62u);
            std::memcpy(buf.data() + 966 + 22, "rpm", 3);
            PutU16LE(buf.data() + 966 + 42, 0u); // parametric linear
            PutU16LE(buf.data() + 966 + 44, 2u); // 2 parameters
            double p1 = 500.0, p2 = 0.25;
            std::memcpy(buf.data() + 966 + 46, &p1, 8);
            std::memcpy(buf.data() + 966 + 54, &p2, 8);

            // TX at 1028 (size 16): "Engine_RPM"
            buf[1028] = 'T'; buf[1029] = 'X';
            PutU16LE(buf.data() + 1030, 16u);
            std::memcpy(buf.data() + 1032, "Engine_RPM", 10);

            for (uint32_t r = 0; r < numRecs; ++r) {
                size_t ro = 1044u + static_cast<size_t>(r) * 8u;
                float tf = static_cast<float>(r) * 0.02f;
                uint16_t rawRpm = static_cast<uint16_t>(r * 300u);
                int16_t boost = static_cast<int16_t>(-20 + static_cast<int>(r) * 5);
                std::memcpy(buf.data() + ro + 0, &tf, 4);
                PutU16LE(buf.data() + ro + 4, rawRpm);
                PutU16LE(buf.data() + ro + 6, static_cast<uint16_t>(boost));
            }
            return buf;
        };

        auto buildMdf41 = [](bool withData) -> std::vector<uint8_t> {
            std::vector<uint8_t> buf(64, 0);
            std::memcpy(buf.data() + 0,  "MDF     ", 8);
            std::memcpy(buf.data() + 8,  "4.10    ", 8);
            std::memcpy(buf.data() + 16, "Explorer", 8);
            PutU16LE(buf.data() + 28, 410u);

            auto appendMdf4Blk = [&](const char id4[5], const std::vector<uint64_t>& links, const std::vector<uint8_t>& pay) -> uint64_t {
                while ((buf.size() & 7u) != 0u) buf.push_back(0);
                uint64_t off = static_cast<uint64_t>(buf.size());
                uint64_t blkLen = 24ULL + static_cast<uint64_t>(links.size()) * 8ULL + static_cast<uint64_t>(pay.size());
                uint8_t hdr[24] = {};
                std::memcpy(hdr, id4, 4);
                PutU32LE(hdr + 8,  static_cast<uint32_t>(blkLen & 0xFFFFFFFFULL));
                PutU32LE(hdr + 12, static_cast<uint32_t>(blkLen >> 32));
                uint64_t lc = static_cast<uint64_t>(links.size());
                PutU32LE(hdr + 16, static_cast<uint32_t>(lc & 0xFFFFFFFFULL));
                PutU32LE(hdr + 20, static_cast<uint32_t>(lc >> 32));
                buf.insert(buf.end(), hdr, hdr + 24);
                for (uint64_t lk : links) {
                    uint8_t lb[8] = {};
                    PutU32LE(lb + 0, static_cast<uint32_t>(lk & 0xFFFFFFFFULL));
                    PutU32LE(lb + 4, static_cast<uint32_t>(lk >> 32));
                    buf.insert(buf.end(), lb, lb + 8);
                }
                buf.insert(buf.end(), pay.begin(), pay.end());
                return off;
            };
            auto appendTx = [&](const std::string& s) -> uint64_t {
                std::vector<uint8_t> p(s.begin(), s.end());
                p.push_back(0);
                return appendMdf4Blk("##TX", {}, p);
            };

            // Reserve ##HD at offset 64 (6 links, 32B data)
            std::vector<uint8_t> hdData(32, 0);
            uint64_t hdOff = appendMdf4Blk("##HD", { 0, 0, 0, 0, 0, 0 }, hdData);
            (void)hdOff;

            uint64_t txSpd  = appendTx("WheelSpeed_FL");
            uint64_t txUnit = appendTx("km/h");
            uint64_t txBat  = appendTx("Battery_mV");

            // ##CC linear conversion: P1 = -10.0, P2 = 0.05 -> raw [200..2150] => [0..97.5 km/h]
            std::vector<uint8_t> ccPay(40, 0);
            ccPay[0] = 1u; // linear
            PutU16LE(ccPay.data() + 6, 2u); // val_count = 2
            double cp1 = -10.0, cp2 = 0.05;
            std::memcpy(ccPay.data() + 24, &cp1, 8);
            std::memcpy(ccPay.data() + 32, &cp2, 8);
            uint64_t ccOff = appendMdf4Blk("##CC", { 0, txUnit, txUnit, 0 }, ccPay);

            auto makeCnPay = [](uint8_t cnType, uint8_t dType, uint32_t byteOff, uint32_t bitCnt) {
                std::vector<uint8_t> p(72, 0);
                p[0] = cnType;
                p[2] = dType;
                PutU32LE(p.data() + 4, byteOff);
                PutU32LE(p.data() + 8, bitCnt);
                return p;
            };

            uint64_t cnBat  = appendMdf4Blk("##CN", { 0,     0, txBat, 0, 0,     0, 0, 0 }, makeCnPay(0u, 0u, 10u, 16u));
            uint64_t cnSpd  = appendMdf4Blk("##CN", { cnBat, 0, txSpd, 0, ccOff, 0, txUnit, 0 }, makeCnPay(0u, 0u, 8u, 16u));
            uint64_t cnTime = appendMdf4Blk("##CN", { cnSpd, 0, 0,     0, 0,     0, 0, 0 }, makeCnPay(2u, 4u, 0u, 64u));

            uint64_t dlOff = 0ULL;
            const uint64_t nCycles = withData ? 40ULL : 0ULL;
            if (withData) {
                // 40 records x 12 bytes = 480 bytes, byte-transposed with M = 12 columns inside ##DZ + ##DL
                const size_t N = 40u, M = 12u;
                std::vector<uint8_t> rawRecs(N * M, 0);
                for (size_t r = 0; r < N; ++r) {
                    double tSec = static_cast<double>(r) * 0.005;
                    uint16_t rawSpd = static_cast<uint16_t>(200u + r * 50u);
                    uint16_t rawBat = static_cast<uint16_t>(12000u + (r % 8u) * 150u);
                    std::memcpy(rawRecs.data() + r * M + 0, &tSec, 8);
                    PutU16LE(rawRecs.data() + r * M + 8,  rawSpd);
                    PutU16LE(rawRecs.data() + r * M + 10, rawBat);
                }
                std::vector<uint8_t> transposed(N * M, 0);
                for (size_t r = 0; r < N; ++r) {
                    for (size_t c = 0; c < M; ++c) {
                        transposed[c * N + r] = rawRecs[r * M + c];
                    }
                }
                std::vector<uint8_t> zStream = WrapZlibStored(transposed);
                std::vector<uint8_t> dzPay(24 + zStream.size(), 0);
                dzPay[0] = 'D'; dzPay[1] = 'T';
                dzPay[2] = 1u; // Deflate + Byte Transpose
                PutU32LE(dzPay.data() + 4,  static_cast<uint32_t>(M));
                PutU32LE(dzPay.data() + 8,  static_cast<uint32_t>(rawRecs.size()));
                PutU32LE(dzPay.data() + 16, static_cast<uint32_t>(zStream.size()));
                std::memcpy(dzPay.data() + 24, zStream.data(), zStream.size());
                uint64_t dzOff = appendMdf4Blk("##DZ", {}, dzPay);

                std::vector<uint8_t> dlPay(16, 0);
                PutU32LE(dlPay.data() + 4, 1u); // count = 1
                dlOff = appendMdf4Blk("##DL", { 0ULL, dzOff }, dlPay);
            }

            std::vector<uint8_t> cgPay(32, 0);
            PutU32LE(cgPay.data() + 8,  static_cast<uint32_t>(nCycles));
            PutU32LE(cgPay.data() + 24, 12u); // cg_data_bytes = 12
            uint64_t cgOff = appendMdf4Blk("##CG", { 0ULL, cnTime, 0ULL, 0ULL }, cgPay);

            std::vector<uint8_t> dgPay(8, 0); // rec_id_size = 0
            uint64_t dgOff = appendMdf4Blk("##DG", { 0ULL, cgOff, dlOff, 0ULL }, dgPay);

            // Patch hd_dg_first at offset 64 + 24 = 88
            PutU32LE(buf.data() + 88, static_cast<uint32_t>(dgOff & 0xFFFFFFFFULL));
            PutU32LE(buf.data() + 92, static_cast<uint32_t>(dgOff >> 32));
            return buf;
        };

        auto mdf3Valid = buildMdf33(32u);
        auto mdf3Empty = buildMdf33(0u);
        auto mf4Valid  = buildMdf41(true);
        auto mf4Empty  = buildMdf41(false);

        std::vector<uint8_t> hdrOnlyMdf(64, 0);
        std::memcpy(hdrOnlyMdf.data(), "MDF     4.10    ", 16);

        ParsedEdaBoard bMdf3, bMf4, badM1, badM2, badM3, badM4;
        bool okMdf3 = WriteAndInspect(dir + L"\\ecu_v33.mdf", mdf3Valid.data(), mdf3Valid.size(), bMdf3);
        bool okMf4  = WriteAndInspect(dir + L"\\ecu_v41.mf4", mf4Valid.data(),  mf4Valid.size(),  bMf4);
        bool rejMdf = !WriteAndInspect(dir + L"\\empty_rec.mdf", mdf3Empty.data(), mdf3Empty.size(), badM1) &&
                      !WriteAndInspect(dir + L"\\empty_rec.mf4", mf4Empty.data(),  mf4Empty.size(),  badM2) &&
                      !WriteAndInspect(dir + L"\\hdr_only.mdf",  hdrOnlyMdf.data(), hdrOnlyMdf.size(), badM3) &&
                      !WriteAndInspect(dir + L"\\hdr_only.mf4",  hdrOnlyMdf.data(), hdrOnlyMdf.size(), badM4);

        bool passMdf = okMdf3 && okMf4 && rejMdf &&
                       bMdf3.kind == EdaDocKind::KiCadSch && bMf4.kind == EdaDocKind::KiCadSch &&
                       bMdf3.netCount == 2u && bMf4.netCount == 2u &&
                       bMdf3.components.size() == 2u && bMf4.components.size() == 2u &&
                       hasLabelSubstr(bMdf3, L"Engine_RPM") && hasLabelSubstr(bMdf3, L"Boost_kPa") &&
                       hasLabelSubstr(bMdf3, L"rpm") && hasLabelSubstr(bMdf3, L"500") &&
                       hasLabelSubstr(bMf4, L"WheelSpeed_FL") && hasLabelSubstr(bMf4, L"Battery_mV") &&
                       hasLabelSubstr(bMf4, L"km/h") && hasLabelSubstr(bMf4, L"97.5") &&
                       bMdf3.traces.size() > 60u && bMf4.traces.size() > 70u &&
                       HashEdaGeometry(bMdf3) != HashEdaGeometry(bMf4);

        tc.Record(passMdf, L"Stage15.Eda.AsamMdf3AndMdf4_CcConversionAndDzTranspose",
            L"mdf3Traces=" + std::to_wstring(bMdf3.traces.size()) +
            L" mf4Traces=" + std::to_wstring(bMf4.traces.size()));
    }

    // ------------------------------------------------------------------------
    // 14. Automotive & Industrial Fieldbus Schematics (.ldf, .arxml, .eds, .dcf, .gsdml, .l5x)
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };
        auto hasFakePcbNode = [](const ParsedEdaBoard& b) -> bool {
            if (b.kind == EdaDocKind::KiCadPcb) return true;
            for (const auto& c : b.components) {
                if (c.footprint == L"ECU_NODE" || c.refDes == L"Master_ECU") return true;
            }
            return false;
        };

        const std::string validLdf =
            "LIN_description_file;\n"
            "LIN_protocol_version = \"2.1\";\n"
            "LIN_speed = 19.2 kbps;\n"
            "Nodes {\n"
            "  Master: BCM_Master, 5 ms, 0.1 ms;\n"
            "  Slaves: Wiper_Slave, RainSensor_Slave;\n"
            "}\n"
            "Signals {\n"
            "  WiperCmd: 4, 0, BCM_Master, Wiper_Slave;\n"
            "  RainLevel: 8, 0, RainSensor_Slave, BCM_Master;\n"
            "}\n"
            "Frames {\n"
            "  Frm_WiperCtrl: 0x12, BCM_Master, 4 { WiperCmd, 0; }\n"
            "  Frm_RainStat: 0x24, RainSensor_Slave, 2 { RainLevel, 0; }\n"
            "}\n"
            "Schedule_tables {\n"
            "  NormalTable { Frm_WiperCtrl delay 10 ms; Frm_RainStat delay 10 ms; }\n"
            "}\n";

        const std::string validArxml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<AUTOSAR xmlns=\"http://autosar.org/schema/r4.0\">\n"
            "  <AR-PACKAGES><AR-PACKAGE><SHORT-NAME>SysPkg</SHORT-NAME><ELEMENTS>\n"
            "    <CAN-CLUSTER><SHORT-NAME>ChassisCAN</SHORT-NAME></CAN-CLUSTER>\n"
            "    <ECU-INSTANCE><SHORT-NAME>BrakeECU</SHORT-NAME></ECU-INSTANCE>\n"
            "    <ECU-INSTANCE><SHORT-NAME>SteerECU</SHORT-NAME></ECU-INSTANCE>\n"
            "    <APPLICATION-SW-COMPONENT-TYPE><SHORT-NAME>AbsCtrlSwc</SHORT-NAME>\n"
            "      <PORTS>\n"
            "        <P-PORT-PROTOTYPE><SHORT-NAME>WheelTorqueOut</SHORT-NAME></P-PORT-PROTOTYPE>\n"
            "        <R-PORT-PROTOTYPE><SHORT-NAME>VehSpeedIn</SHORT-NAME></R-PORT-PROTOTYPE>\n"
            "      </PORTS>\n"
            "    </APPLICATION-SW-COMPONENT-TYPE>\n"
            "    <I-SIGNAL><SHORT-NAME>Sig_WheelTorque</SHORT-NAME></I-SIGNAL>\n"
            "  </ELEMENTS></AR-PACKAGE></AR-PACKAGES>\n"
            "</AUTOSAR>\n";

        const std::string validEds =
            "[DeviceInfo]\n"
            "VendorName=MaxonMotor\n"
            "ProductName=EPOS4_Servo\n"
            "BaudRate_500=1\n"
            "BaudRate_1000=1\n"
            "[MandatoryObjects]\n"
            "SupportedObjects=3\n"
            "[1000]\n"
            "ParameterName=Device Type\n"
            "ObjectType=0x7\n"
            "DataType=0x0007\n"
            "AccessType=ro\n"
            "DefaultValue=0x00020192\n"
            "[1400]\n"
            "ParameterName=RPDO1 Comm Param\n"
            "ObjectType=0x9\n"
            "[1400sub1]\n"
            "ParameterName=COB-ID Used\n"
            "DefaultValue=0x201\n"
            "[1600sub1]\n"
            "ParameterName=Map Controlword\n"
            "DefaultValue=0x60400010\n"
            "[1800]\n"
            "ParameterName=TPDO1 Comm Param\n"
            "ObjectType=0x9\n"
            "[1800sub1]\n"
            "ParameterName=COB-ID Used\n"
            "DefaultValue=0x181\n"
            "[1A00sub1]\n"
            "ParameterName=Map Statusword\n"
            "DefaultValue=0x60410010\n";

        const std::string validGsdml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<ISO15745Profile xmlns=\"http://www.profibus.com/GSDML/2003/11/DeviceProfile\">\n"
            "  <ProfileBody>\n"
            "    <DeviceIdentity VendorID=\"0x002A\" DeviceID=\"0x0105\">\n"
            "      <VendorName Value=\"Siemens AG\"/>\n"
            "    </DeviceIdentity>\n"
            "    <DeviceFunction><Family MainFamily=\"I/O\" ProductFamily=\"ET200SP\"/></DeviceFunction>\n"
            "    <ApplicationProcess>\n"
            "      <DeviceAccessPointList>\n"
            "        <DeviceAccessPointItem ID=\"DAP_1\" PhysicalSlots=\"0..16\" MinDeviceInterval=\"32\">\n"
            "          <ModuleInfo><Name TextId=\"T_DAP_CPU\"/></ModuleInfo>\n"
            "        </DeviceAccessPointItem>\n"
            "      </DeviceAccessPointList>\n"
            "      <ModuleList>\n"
            "        <ModuleItem ID=\"MOD_DI16\">\n"
            "          <ModuleInfo><Name TextId=\"T_MOD_DI16\"/></ModuleInfo>\n"
            "          <VirtualSubmoduleList><VirtualSubmoduleItem ID=\"SUB_DI\">\n"
            "            <IOData>\n"
            "              <Input><DataItem DataType=\"OctetString\" Length=\"2\" TextId=\"T_IN\"/></Input>\n"
            "              <Output><DataItem DataType=\"OctetString\" Length=\"4\" TextId=\"T_OUT\"/></Output>\n"
            "            </IOData>\n"
            "          </VirtualSubmoduleItem></VirtualSubmoduleList>\n"
            "        </ModuleItem>\n"
            "      </ModuleList>\n"
            "      <ExternalTextList><PrimaryLanguage>\n"
            "        <Text TextId=\"T_DAP_CPU\" Value=\"IM 155-6 PN HF\"/>\n"
            "        <Text TextId=\"T_MOD_DI16\" Value=\"DI 16x24VDC ST\"/>\n"
            "      </PrimaryLanguage></ExternalTextList>\n"
            "    </ApplicationProcess>\n"
            "  </ProfileBody>\n"
            "</ISO15745Profile>\n";

        const std::string validL5x =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<RSLogix5000Content SchemaRevision=\"1.0\">\n"
            "  <Controller Name=\"Packaging_PLC\" ProcessorType=\"1756-L83E\">\n"
            "    <Tags><Tag Name=\"Conveyor_Speed\" DataType=\"DINT\"/></Tags>\n"
            "    <Programs><Program Name=\"MainProgram\"><Routines>\n"
            "      <Routine Name=\"MainRoutine\" Type=\"RLL\"><RLLContent>\n"
            "        <Rung Number=\"0\" Type=\"N\">\n"
            "          <Text><![CDATA[[XIC(Start_Btn),XIC(Auto_Latch)]XIO(EStop_NC)TON(Warmup_Tmr,500,0)OTE(Auto_Latch);]]></Text>\n"
            "        </Rung>\n"
            "      </RLLContent></Routine>\n"
            "    </Routines></Program></Programs>\n"
            "  </Controller>\n"
            "</RSLogix5000Content>\n";

        ParsedEdaBoard bLdf, bArx, bEds, bDcf, bGsd, bL5x;
        bool okLdf = WriteAndInspect(dir + L"\\body_bus.ldf",    validLdf.data(),   validLdf.size(),   bLdf);
        bool okArx = WriteAndInspect(dir + L"\\chassis.arxml",   validArxml.data(), validArxml.size(), bArx);
        bool okEds = WriteAndInspect(dir + L"\\epos4.eds",       validEds.data(),   validEds.size(),   bEds);
        bool okDcf = WriteAndInspect(dir + L"\\epos4_node.dcf",  validEds.data(),   validEds.size(),   bDcf);
        bool okGsd = WriteAndInspect(dir + L"\\et200sp.gsdml",   validGsdml.data(), validGsdml.size(), bGsd);
        bool okL5x = WriteAndInspect(dir + L"\\packaging.l5x",   validL5x.data(),   validL5x.size(),   bL5x);

        // Strict negative rejection: empty shells with 0 domain entities MUST return false (no fake fallbacks!)
        const char* emptyLdf   = "LIN_description_file;\nLIN_speed = 19.2 kbps;\n";
        const char* emptyArxml = "<AUTOSAR></AUTOSAR>";
        const char* emptyEds   = "[DeviceInfo]\nVendorName=Nobody\n";
        const char* emptyGsdml = "<ISO15745Profile><ProfileBody></ProfileBody></ISO15745Profile>";
        const char* emptyL5x   = "<RSLogix5000Content><Controller Name=\"EmptyPLC\"></Controller></RSLogix5000Content>";

        ParsedEdaBoard badF1, badF2, badF3, badF4, badF5;
        bool rejFieldbus = !WriteAndInspect(dir + L"\\empty.ldf",   emptyLdf,   std::strlen(emptyLdf),   badF1) &&
                           !WriteAndInspect(dir + L"\\empty.arxml", emptyArxml, std::strlen(emptyArxml), badF2) &&
                           !WriteAndInspect(dir + L"\\empty.eds",   emptyEds,   std::strlen(emptyEds),   badF3) &&
                           !WriteAndInspect(dir + L"\\empty.gsdml", emptyGsdml, std::strlen(emptyGsdml), badF4) &&
                           !WriteAndInspect(dir + L"\\empty.l5x",   emptyL5x,   std::strlen(emptyL5x),   badF5);

        bool passFieldbus = okLdf && okArx && okEds && okDcf && okGsd && okL5x && rejFieldbus &&
                            bLdf.kind == EdaDocKind::KiCadSch && bArx.kind == EdaDocKind::KiCadSch &&
                            bEds.kind == EdaDocKind::KiCadSch && bGsd.kind == EdaDocKind::KiCadSch &&
                            bL5x.kind == EdaDocKind::KiCadSch &&
                            !hasFakePcbNode(bLdf) && !hasFakePcbNode(bArx) &&
                            !hasFakePcbNode(bEds) && !hasFakePcbNode(bGsd) &&
                            hasLabelSubstr(bLdf, L"BCM_Master") && hasLabelSubstr(bLdf, L"Wiper_Slave") &&
                            hasLabelSubstr(bLdf, L"19.2") &&
                            hasLabelSubstr(bArx, L"ChassisCAN") && hasLabelSubstr(bArx, L"BrakeECU") &&
                            hasLabelSubstr(bArx, L"AbsCtrlSwc") &&
                            hasLabelSubstr(bEds, L"EPOS4_Servo") && hasLabelSubstr(bEds, L"RPDO") &&
                            hasLabelSubstr(bEds, L"TPDO") &&
                            hasLabelSubstr(bGsd, L"IM 155-6 PN HF") && hasLabelSubstr(bGsd, L"DI 16x24VDC ST") &&
                            hasLabelSubstr(bL5x, L"Packaging_PLC") && hasLabelSubstr(bL5x, L"Start_Btn") &&
                            hasLabelSubstr(bL5x, L"Warmup_Tmr");

        tc.Record(passFieldbus, L"Stage15.Eda.FieldbusAndPlcSchematics_LdfArxmlEdsGsdmlL5x",
            L"ldfTr=" + std::to_wstring(bLdf.traces.size()) +
            L" arxTr=" + std::to_wstring(bArx.traces.size()) +
            L" l5xTr=" + std::to_wstring(bL5x.traces.size()));
    }

    // ------------------------------------------------------------------------
    // 15. GDSII Hierarchical Layout (.gds), IEEE 1364 VCD Bus/Real (.vcd), & Touchstone RF (.s1p/.s2p)
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        // Build binary GDSII with IBM System/370 excess-64 floats, child cell BITCELL, and root cell TOP_ARRAY (SREF + AREF)
        std::vector<uint8_t> gds;
        auto pushGdsRec = [&](uint8_t rType, uint8_t dType, const std::vector<uint8_t>& pay) {
            std::vector<uint8_t> aligned = pay;
            if (aligned.size() & 1u) aligned.push_back(0);
            uint16_t len = static_cast<uint16_t>(4u + aligned.size());
            uint8_t hdr[4] = {};
            PutU16BE(hdr, len);
            hdr[2] = rType;
            hdr[3] = dType;
            gds.insert(gds.end(), hdr, hdr + 4);
            gds.insert(gds.end(), aligned.begin(), aligned.end());
        };
        auto strPay = [](const char* s) {
            return std::vector<uint8_t>(s, s + std::strlen(s));
        };
        auto u16Pay = [](uint16_t v) {
            std::vector<uint8_t> p(2);
            PutU16BE(p.data(), v);
            return p;
        };
        auto xyPay = [](const std::vector<std::pair<int32_t, int32_t>>& pts) {
            std::vector<uint8_t> p(pts.size() * 8u);
            for (size_t i = 0; i < pts.size(); ++i) {
                PutU32BE(p.data() + i * 8u + 0u, static_cast<uint32_t>(pts[i].first));
                PutU32BE(p.data() + i * 8u + 4u, static_cast<uint32_t>(pts[i].second));
            }
            return p;
        };

        pushGdsRec(0x00, 0x02, u16Pay(600u));              // HEADER v6.0
        pushGdsRec(0x01, 0x02, std::vector<uint8_t>(24, 0)); // BGNLIB
        pushGdsRec(0x02, 0x06, strPay("SRAM_IC_LIB"));     // LIBNAME
        // UNITS: 1e-3 user units/DB unit, 1e-9 meters/DB unit (1 DB unit = 1 nm, 1000 DB units = 1 um)
        const std::vector<uint8_t> ibmUnits = {
            0x3E, 0x41, 0x89, 0x37, 0x4B, 0xC6, 0xA7, 0xEF, // 1e-3 in IBM System/370 excess-64
            0x39, 0x44, 0xB8, 0x2F, 0xA0, 0x9B, 0x5A, 0x53  // 1e-9 in IBM System/370 excess-64
        };
        pushGdsRec(0x03, 0x05, ibmUnits);
        std::vector<uint8_t> gdsHeaderOnly = gds;
        {
            uint8_t endLib[4] = { 0x00, 0x04, 0x04, 0x00 };
            gdsHeaderOnly.insert(gdsHeaderOnly.end(), endLib, endLib + 4);
        }

        // Child structure BITCELL: 10um x 5um BOUNDARY + TEXT "VDD_RAIL"
        pushGdsRec(0x05, 0x02, std::vector<uint8_t>(24, 0)); // BGNSTR
        pushGdsRec(0x06, 0x06, strPay("BITCELL"));
        pushGdsRec(0x08, 0x00, {});                          // BOUNDARY
        pushGdsRec(0x0D, 0x02, u16Pay(1u));                  // LAYER 1
        pushGdsRec(0x0E, 0x02, u16Pay(0u));                  // DATATYPE 0
        pushGdsRec(0x10, 0x03, xyPay({ {0,0}, {10000,0}, {10000,5000}, {0,5000}, {0,0} }));
        pushGdsRec(0x11, 0x00, {});                          // ENDEL
        pushGdsRec(0x0C, 0x00, {});                          // TEXT
        pushGdsRec(0x0D, 0x02, u16Pay(2u));                  // LAYER 2
        pushGdsRec(0x10, 0x03, xyPay({ {2000, 2500} }));
        pushGdsRec(0x19, 0x06, strPay("VDD_RAIL"));          // STRING
        pushGdsRec(0x11, 0x00, {});                          // ENDEL
        pushGdsRec(0x07, 0x00, {});                          // ENDSTR

        // Top structure TOP_ARRAY: places BITCELL via mirrored+scaled+rotated SREF + 3x2 AREF
        pushGdsRec(0x05, 0x02, std::vector<uint8_t>(24, 0)); // BGNSTR
        pushGdsRec(0x06, 0x06, strPay("TOP_ARRAY"));
        pushGdsRec(0x0A, 0x00, {});                          // SREF
        pushGdsRec(0x12, 0x06, strPay("BITCELL"));           // SNAME
        pushGdsRec(0x1A, 0x01, u16Pay(0x8000u));             // STRANS X-reflection
        pushGdsRec(0x1B, 0x05, { 0x41, 0x20, 0,0,0,0,0,0 }); // MAG = 2.0
        pushGdsRec(0x1C, 0x05, { 0x42, 0x5A, 0,0,0,0,0,0 }); // ANGLE = 90.0 deg
        pushGdsRec(0x10, 0x03, xyPay({ {50000, 30000} }));
        pushGdsRec(0x11, 0x00, {});                          // ENDEL

        pushGdsRec(0x0B, 0x00, {});                          // AREF
        pushGdsRec(0x12, 0x06, strPay("BITCELL"));           // SNAME
        pushGdsRec(0x13, 0x02, { 0x00, 0x03, 0x00, 0x02 });  // COLROW: 3 cols, 2 rows
        pushGdsRec(0x10, 0x03, xyPay({ {100000, 0}, {160000, 0}, {100000, 40000} }));
        pushGdsRec(0x11, 0x00, {});                          // ENDEL
        pushGdsRec(0x07, 0x00, {});                          // ENDSTR
        pushGdsRec(0x04, 0x00, {});                          // ENDLIB

        // IEEE 1364 VCD with multi-char IDs, 8-bit bus b10101100 (0xAC), and real trace
        const std::string validVcd =
            "$timescale 1ns $end\n"
            "$scope module axi_tb $end\n"
            "$var wire 1 $% aclk $end\n"
            "$var reg 8 !# wdata [7:0] $end\n"
            "$var real 64 &* pll_vctrl $end\n"
            "$upscope $end\n"
            "$enddefinitions $end\n"
            "$dumpvars\n"
            "0$%\n"
            "b10101100 !#\n"
            "r0.85 &*\n"
            "$end\n"
            "#2500\n1$%\nr1.65 &*\n"
            "#5000\n0$%\nb00111100 !#\nr2.40 &*\n"
            "#7500\n1$%\nb11110000 !#\nr1.10 &*\n"
            "#10000\n0$%\n";

        const char* hdrOnlyVcd =
            "$timescale 1ps $end\n$scope module empty $end\n$var wire 1 ! clk $end\n$upscope $end\n$enddefinitions $end\n";

        // Touchstone .s2p in MA (wrapped across lines) & RI formats, plus .s1p
        const std::string validS2pMa =
            "! 2-Port Bandpass Filter S-Parameters\n"
            "# GHz S MA R 50\n"
            "1.0  0.85 -120.0   0.12  45.0\n"
            "     0.12   45.0   0.84 -118.0\n"
            "2.0  0.10  -30.0   0.92 -15.0\n"
            "     0.92  -15.0   0.11 -28.0\n"
            "3.0  0.05    5.0   0.96 -85.0\n"
            "     0.96  -85.0   0.05   4.0\n"
            "4.0  0.75   95.0   0.18 -160.0\n"
            "     0.18 -160.0   0.74  92.0\n";

        const std::string validS2pRi =
            "# MHz S RI R 50\n"
            "100  0.60 -0.40  0.10 0.05  0.10 0.05  0.58 -0.38\n"
            "500  0.08  0.04  0.88 -0.22 0.88 -0.22 0.07  0.03\n"
            "900  0.70  0.30  0.15 -0.10 0.15 -0.10 0.69  0.29\n";

        const std::string validS1pDb =
            "# GHz S DB R 50\n"
            "1.0 -3.2 -40.0\n"
            "2.4 -18.5 12.0\n"
            "5.0 -6.1 110.0\n";

        const char* badS2pTxt = "1.0 2.0 3.0 4.0 5.0 6.0 7.0 8.0 9.0\n"; // missing '#' option line

        ParsedEdaBoard bGds, badGds, bVcd, badVcd, bS2pMa, bS2pRi, bS1p, badS2p;
        bool okGds   = WriteAndInspect(dir + L"\\sram_top.gds",   gds.data(), gds.size(), bGds);
        bool rejGds  = !WriteAndInspect(dir + L"\\hdr_only.gds",  gdsHeaderOnly.data(), gdsHeaderOnly.size(), badGds);
        bool okVcd   = WriteAndInspect(dir + L"\\axi_bus.vcd",    validVcd.data(), validVcd.size(), bVcd);
        bool rejVcd  = !WriteAndInspect(dir + L"\\hdr_only.vcd",  hdrOnlyVcd, std::strlen(hdrOnlyVcd), badVcd);
        bool okS2pMa = WriteAndInspect(dir + L"\\bpf_ma.s2p",     validS2pMa.data(), validS2pMa.size(), bS2pMa);
        bool okS2pRi = WriteAndInspect(dir + L"\\bpf_ri.s2p",     validS2pRi.data(), validS2pRi.size(), bS2pRi);
        bool okS1p   = WriteAndInspect(dir + L"\\ant.s1p",        validS1pDb.data(), validS1pDb.size(), bS1p);
        bool rejSnp  = !WriteAndInspect(dir + L"\\no_opt.s2p",    badS2pTxt, std::strlen(badS2pTxt), badS2p);

        // Verify Touchstone MA & RI curves are non-flat (distinct Y coordinates across S21/S11 segments)
        auto hasCurveVariation = [](const ParsedEdaBoard& b) -> bool {
            float mnY = 1e9f, mxY = -1e9f;
            for (const auto& t : b.traces) {
                if (t.layer == EdaLayerId::TopCopper || t.layer == EdaLayerId::BottomCopper || t.layer == EdaLayerId::SchWire) {
                    if (std::fabs(t.y2 - t.y1) > 0.5f) {
                        mnY = std::min({ mnY, t.y1, t.y2 });
                        mxY = std::max({ mxY, t.y1, t.y2 });
                    }
                }
            }
            return mxY > mnY + 2.0f;
        };

        // Verify VCD timestamps were normalized rather than exploding to curTime * 0.5f (= 5000)
        bool vcdNormalized = (bVcd.maxX < 250.0f && bVcd.minX > -80.0f);

        bool passItem18 = okGds && rejGds &&
                          (bGds.maxX - bGds.minX) > 90.0f && (bGds.maxY - bGds.minY) > 20.0f &&
                          bGds.traces.size() >= 28u && hasLabelSubstr(bGds, L"VDD_RAIL") &&
                          okVcd && rejVcd && vcdNormalized &&
                          hasLabelSubstr(bVcd, L"wdata") &&
                          (hasLabelSubstr(bVcd, L"AC") || hasLabelSubstr(bVcd, L"ac")) &&
                          okS2pMa && okS2pRi && okS1p && rejSnp &&
                          hasCurveVariation(bS2pMa) && hasCurveVariation(bS2pRi) && hasCurveVariation(bS1p) &&
                          hasLabelSubstr(bS2pMa, L"S11") && hasLabelSubstr(bS2pMa, L"S21");

        tc.Record(passItem18, L"Stage15.Eda.GdsiiHierarchy_VcdMultiBitBus_TouchstoneMaRi",
            L"gdsSpanX=" + std::to_wstring(static_cast<int>(bGds.maxX - bGds.minX)) +
            L" vcdMaxX=" + std::to_wstring(static_cast<int>(bVcd.maxX)) +
            L" s2pMaTr=" + std::to_wstring(bS2pMa.traces.size()));
    }

    // ------------------------------------------------------------------------
    // 16. SPICE Netlist (.cir), Eagle/KiCad Schematics (.sch/.kicad_sch), & Checksummed HEX/SREC
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        const std::string validSpice =
            "Two-Stage Amplifier & Filter Testbench\n"
            "V1 VIN 0 DC 5.0 AC 1.0\n"
            "R1 VIN N_BASE 10k\n"
            "+ TC1=0.001\n"
            "C1 VIN N_BASE 100n\n"
            "L1 VCC N_COL 47u\n"
            "D1 N_COL VCC D1N4148\n"
            "Q1 N_COL N_BASE 0 Q2N2222\n"
            "M1 VOUT N_COL 0 0 NMOS_FAST W=10u L=1u\n"
            "X1 VOUT VFILT 0 LPF_STAGE params: fc=1e6\n"
            ".model D1N4148 D(IS=2.52n)\n"
            ".model Q2N2222 NPN(BF=200)\n"
            ".tran 1n 500n\n"
            ".end\n";

        const char* proseFakeCir =
            "This document describes a quarterly sales forecast and is not a SPICE circuit.\n"
            "Please review section 3 before Friday.\n";

        const std::string validEagleSch =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<eagle version=\"9.6.2\"><drawing><schematic>\n"
            "  <libraries><library name=\"analog\">\n"
            "    <symbols><symbol name=\"OPAMP\">\n"
            "      <wire x1=\"-5.08\" y1=\"5.08\" x2=\"-5.08\" y2=\"-5.08\" width=\"0.254\" layer=\"94\"/>\n"
            "      <wire x1=\"-5.08\" y1=\"-5.08\" x2=\"5.08\" y2=\"0\" width=\"0.254\" layer=\"94\"/>\n"
            "      <wire x1=\"5.08\" y1=\"0\" x2=\"-5.08\" y2=\"5.08\" width=\"0.254\" layer=\"94\"/>\n"
            "      <pin name=\"IN+\" x=\"-10.16\" y=\"2.54\" length=\"middle\" rot=\"R0\"/>\n"
            "      <pin name=\"IN-\" x=\"-10.16\" y=\"-2.54\" length=\"middle\" rot=\"R0\"/>\n"
            "      <pin name=\"OUT\" x=\"10.16\" y=\"0\" length=\"middle\" rot=\"R180\"/>\n"
            "    </symbol></symbols>\n"
            "    <devicesets><deviceset name=\"TL072\" prefix=\"U\">\n"
            "      <gates><gate name=\"G$1\" symbol=\"OPAMP\" x=\"0\" y=\"0\"/></gates>\n"
            "    </deviceset></devicesets>\n"
            "  </library></libraries>\n"
            "  <parts><part name=\"U1\" library=\"analog\" deviceset=\"TL072\" device=\"\" value=\"TL072\"/></parts>\n"
            "  <sheets><sheet>\n"
            "    <instances><instance part=\"U1\" gate=\"G$1\" x=\"30.48\" y=\"25.4\" rot=\"MR90\"/></instances>\n"
            "    <nets><net name=\"AUDIO_IN\">\n"
            "      <segment>\n"
            "        <wire x1=\"12.7\" y1=\"15.24\" x2=\"27.94\" y2=\"15.24\" width=\"0.1524\" layer=\"91\"/>\n"
            "        <label x=\"15.24\" y=\"15.24\" size=\"1.778\" layer=\"95\"/>\n"
            "      </segment>\n"
            "    </net></nets>\n"
            "  </sheet></sheets>\n"
            "</schematic></drawing></eagle>\n";

        const std::string validKiCadSch =
            "(kicad_sch (version 20230121) (generator eeschema)\n"
            "  (lib_symbols\n"
            "    (symbol \"Device:R\"\n"
            "      (symbol \"R_0_1\"\n"
            "        (rectangle (start -1.016 -2.54) (end 1.016 2.54) (stroke (width 0.254))))\n"
            "      (symbol \"R_1_1\"\n"
            "        (pin passive line (at 0 3.81 270) (length 1.27) (name \"~\") (number \"1\"))\n"
            "        (pin passive line (at 0 -3.81 90) (length 1.27) (name \"~\") (number \"2\")))))\n"
            "  (symbol (lib_id \"Device:R\") (at 100 80 0) (unit 1)\n"
            "    (property \"Reference\" \"R1\" (at 102.5 79 0))\n"
            "    (property \"Value\" \"4.7k\" (at 102.5 81.5 0)))\n"
            "  (wire (pts (xy 100 76.19) (xy 100 68)))\n"
            "  (wire (pts (xy 100 83.81) (xy 100 92)))\n"
            "  (label \"VCC_3V3\" (at 100 68 0))\n"
            ")\n";

        auto makeIhexLine = [](uint8_t recType, uint16_t addr, const std::vector<uint8_t>& pay) -> std::string {
            uint8_t sum = static_cast<uint8_t>(pay.size()) +
                          static_cast<uint8_t>((addr >> 8) & 0xFFu) +
                          static_cast<uint8_t>(addr & 0xFFu) + recType;
            char hdr[16] = {};
            sprintf_s(hdr, ":%02X%04X%02X", static_cast<unsigned>(pay.size()), static_cast<unsigned>(addr), static_cast<unsigned>(recType));
            std::string s(hdr);
            for (uint8_t b : pay) {
                sum = static_cast<uint8_t>(sum + b);
                char hb[4] = {};
                sprintf_s(hb, "%02X", static_cast<unsigned>(b));
                s += hb;
            }
            uint8_t cksum = static_cast<uint8_t>((0u - sum) & 0xFFu);
            char cb[8] = {};
            sprintf_s(cb, "%02X\n", static_cast<unsigned>(cksum));
            s += cb;
            return s;
        };

        std::vector<uint8_t> fwRow0(16), fwRow1(16);
        for (size_t i = 0; i < 16; ++i) {
            fwRow0[i] = static_cast<uint8_t>(0x10u + i * 3u);
            fwRow1[i] = static_cast<uint8_t>(0xA0u ^ i);
        }
        std::string validHex =
            makeIhexLine(0x04u, 0x0000u, { 0x08u, 0x04u }) + // Extended Linear Address 0x08040000
            makeIhexLine(0x00u, 0x0000u, fwRow0) +
            makeIhexLine(0x00u, 0x0010u, fwRow1) +
            makeIhexLine(0x05u, 0x0000u, { 0x08u, 0x04u, 0x01u, 0x01u }) + // Entry 0x08040101
            makeIhexLine(0x01u, 0x0000u, {});

        std::string badCksumHex = validHex;
        // Corrupt checksum of first data record
        size_t nlPos = badCksumHex.find('\n');
        size_t nlPos2 = badCksumHex.find('\n', nlPos + 1);
        badCksumHex[nlPos2 - 1] = (badCksumHex[nlPos2 - 1] == '0') ? '1' : '0';
        const std::string eofOnlyHex = makeIhexLine(0x01u, 0x0000u, {});

        auto makeSrecLine = [](char sType, int addrBytes, uint32_t addr, const std::vector<uint8_t>& pay) -> std::string {
            uint8_t byteCnt = static_cast<uint8_t>(addrBytes + static_cast<int>(pay.size()) + 1);
            uint8_t sum = byteCnt;
            char hdr[8] = {};
            sprintf_s(hdr, "S%c%02X", sType, static_cast<unsigned>(byteCnt));
            std::string s(hdr);
            for (int k = addrBytes - 1; k >= 0; --k) {
                uint8_t ab = static_cast<uint8_t>((addr >> (k * 8)) & 0xFFu);
                sum = static_cast<uint8_t>(sum + ab);
                char hb[4] = {};
                sprintf_s(hb, "%02X", static_cast<unsigned>(ab));
                s += hb;
            }
            for (uint8_t b : pay) {
                sum = static_cast<uint8_t>(sum + b);
                char hb[4] = {};
                sprintf_s(hb, "%02X", static_cast<unsigned>(b));
                s += hb;
            }
            uint8_t cksum = static_cast<uint8_t>((~sum) & 0xFFu);
            char cb[8] = {};
            sprintf_s(cb, "%02X\n", static_cast<unsigned>(cksum));
            s += cb;
            return s;
        };

        std::string validSrec =
            makeSrecLine('0', 2, 0x0000u, { 'B','O','O','T' }) +
            makeSrecLine('3', 4, 0x08040000u, fwRow0) +
            makeSrecLine('3', 4, 0x08040010u, fwRow1) +
            makeSrecLine('7', 4, 0x08040100u, {});

        std::string badCksumSrec = validSrec;
        size_t snl1 = badCksumSrec.find('\n');
        size_t snl2 = badCksumSrec.find('\n', snl1 + 1);
        badCksumSrec[snl2 - 1] = (badCksumSrec[snl2 - 1] == 'F') ? 'E' : 'F';

        ParsedEdaBoard bCir, badCir, bEagS, bKiS, bHex, badHex1, badHex2, bSrec, badSrec;
        bool okCir   = WriteAndInspect(dir + L"\\amp_filter.cir",   validSpice.data(),    validSpice.size(),    bCir);
        bool rejCir  = !WriteAndInspect(dir + L"\\prose.cir",       proseFakeCir,         std::strlen(proseFakeCir), badCir);
        bool okEagS  = WriteAndInspect(dir + L"\\opamp_xml.sch",    validEagleSch.data(), validEagleSch.size(), bEagS);
        bool okKiS   = WriteAndInspect(dir + L"\\resistor.kicad_sch", validKiCadSch.data(), validKiCadSch.size(), bKiS);
        bool okHex   = WriteAndInspect(dir + L"\\stm32_app.hex",    validHex.data(),      validHex.size(),      bHex);
        bool rejHex  = !WriteAndInspect(dir + L"\\corrupt.hex",     badCksumHex.data(),   badCksumHex.size(),   badHex1) &&
                       !WriteAndInspect(dir + L"\\eof_only.hex",    eofOnlyHex.data(),    eofOnlyHex.size(),    badHex2);
        bool okSrec  = WriteAndInspect(dir + L"\\stm32_app.srec",   validSrec.data(),     validSrec.size(),     bSrec);
        bool rejSrec = !WriteAndInspect(dir + L"\\corrupt.srec",    badCksumSrec.data(),  badCksumSrec.size(),  badSrec);

        bool passItem19 = okCir && rejCir &&
                          bCir.netCount >= 4u && bCir.components.size() >= 6u && bCir.traces.size() > 30u &&
                          hasLabelSubstr(bCir, L"R1") && hasLabelSubstr(bCir, L"M1") &&
                          okEagS && bEagS.pads.size() >= 3u && bEagS.traces.size() >= 4u &&
                          hasLabelSubstr(bEagS, L"AUDIO_IN") &&
                          okKiS && bKiS.pads.size() >= 2u && bKiS.traces.size() >= 6u &&
                          hasLabelSubstr(bKiS, L"VCC_3V3") && hasLabelSubstr(bKiS, L"R1") &&
                          okHex && rejHex && okSrec && rejSrec &&
                          hasLabelSubstr(bHex, L"0x08040000") && hasLabelSubstr(bSrec, L"0x08040000") &&
                          bHex.components.size() >= 1u && bSrec.components.size() >= 1u;

        tc.Record(passItem19, L"Stage15.Eda.SpiceNetlist_EagleKiCadSchSymbols_ChecksummedHexSrec",
            L"spiceNets=" + std::to_wstring(bCir.netCount) +
            L" eagPads=" + std::to_wstring(bEagS.pads.size()) +
            L" kiPads=" + std::to_wstring(bKiS.pads.size()));
    }

    // ------------------------------------------------------------------------
    // 17. ARM CMSIS-SVD (.svd): CPU/NVIC, derivedFrom, dim Arrays, Clusters & Bitfields
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        const std::string validSvd =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<device schemaVersion=\"1.3\">\n"
            "  <name>STM32H743</name>\n"
            "  <cpu>\n"
            "    <name>CM7</name>\n"
            "    <fpuPresent>1</fpuPresent>\n"
            "    <mpuPresent>1</mpuPresent>\n"
            "    <icachePresent>1</icachePresent>\n"
            "    <nvicPrioBits>4</nvicPrioBits>\n"
            "  </cpu>\n"
            "  <peripherals>\n"
            "    <peripheral>\n"
            "      <name>RCC</name>\n"
            "      <baseAddress>0x58024400</baseAddress>\n"
            "      <interrupt><name>RCC_IRQ</name><value>5</value></interrupt>\n"
            "      <registers>\n"
            "        <register>\n"
            "          <name>CR</name>\n"
            "          <addressOffset>0x00</addressOffset>\n"
            "          <size>32</size>\n"
            "          <fields>\n"
            "            <field><name>HSION</name><bitOffset>0</bitOffset><bitWidth>1</bitWidth></field>\n"
            "            <field><name>HSIKERON</name><lsb>1</lsb><msb>1</msb></field>\n"
            "            <field><name>PLL1ON</name><bitRange>[24:24]</bitRange></field>\n"
            "          </fields>\n"
            "        </register>\n"
            "      </registers>\n"
            "    </peripheral>\n"
            "    <peripheral>\n"
            "      <name>GPIOA</name>\n"
            "      <baseAddress>0x58020000</baseAddress>\n"
            "      <registers>\n"
            "        <cluster>\n"
            "          <name>PORT</name>\n"
            "          <addressOffset>0x00</addressOffset>\n"
            "          <register>\n"
            "            <dim>2</dim><dimIncrement>0x04</dimIncrement><dimIndex>0,1</dimIndex>\n"
            "            <name>CFG%s</name><addressOffset>0x00</addressOffset><size>32</size>\n"
            "            <fields><field><name>MODE</name><bitRange>[3:0]</bitRange></field></fields>\n"
            "          </register>\n"
            "        </cluster>\n"
            "      </registers>\n"
            "    </peripheral>\n"
            "    <peripheral derivedFrom=\"GPIOA\">\n"
            "      <dim>2</dim><dimIncrement>0x0400</dimIncrement><dimIndex>B,C</dimIndex>\n"
            "      <name>GPIO%s</name>\n"
            "      <baseAddress>0x58020400</baseAddress>\n"
            "    </peripheral>\n"
            "  </peripherals>\n"
            "</device>\n";

        const char* emptySvd   = "<?xml version=\"1.0\"?><device><name>EmptyMcu</name><peripherals></peripherals></device>";
        const char* genericXml = "<?xml version=\"1.0\"?><catalog><book id=\"1\"><name>NotAnMcu</name></book></catalog>";

        ParsedEdaBoard bSvd, badSvd1, badSvd2;
        bool okSvd  = WriteAndInspect(dir + L"\\stm32h743.svd", validSvd.data(), validSvd.size(), bSvd);
        bool rejSvd = !WriteAndInspect(dir + L"\\empty.svd", emptySvd, std::strlen(emptySvd), badSvd1) &&
                      !WriteAndInspect(dir + L"\\book.svd", genericXml, std::strlen(genericXml), badSvd2);

        // Expect CPU + 4 peripherals (RCC, GPIOA, GPIOB, GPIOC) = >= 5 components, bitfield labels & IRQ label
        bool passSvd = okSvd && rejSvd &&
                       bSvd.components.size() >= 5u &&
                       hasLabelSubstr(bSvd, L"STM32H743") &&
                       hasLabelSubstr(bSvd, L"CM7") &&
                       hasLabelSubstr(bSvd, L"GPIOB") &&
                       hasLabelSubstr(bSvd, L"GPIOC") &&
                       hasLabelSubstr(bSvd, L"PLL1ON") &&
                       hasLabelSubstr(bSvd, L"RCC_IRQ");

        tc.Record(passSvd, L"Stage15.Eda.CmsisSvd_CpuNvicDerivedFromDimAndBitfields",
            L"svdComps=" + std::to_wstring(bSvd.components.size()) +
            L" svdPads=" + std::to_wstring(bSvd.pads.size()));
    }

    // ------------------------------------------------------------------------
    // 18. KiCad .kicad_sym (extends/arc/polyline), Specctra .ses, & Pick-and-Place .pos/.cpl
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        const std::string validKiSym =
            "(kicad_symbol_lib (version 20220914) (generator kicad_symbol_editor)\n"
            "  (symbol \"DRV8301\" (in_bom yes) (on_board yes)\n"
            "    (property \"Reference\" \"U\" (at 0 10.16 0))\n"
            "    (property \"Value\" \"DRV8301\" (at 0 -10.16 0))\n"
            "    (symbol \"DRV8301_0_1\"\n"
            "      (rectangle (start -7.62 7.62) (end 7.62 -7.62) (stroke (width 0.254)))\n"
            "      (polyline (pts (xy -3.0 2.0) (xy 0.0 0.0) (xy -3.0 -2.0)) (stroke (width 0.2)))\n"
            "      (arc (start 1.0 -2.0) (mid 3.0 0.0) (end 1.0 2.0) (stroke (width 0.2))))\n"
            "    (symbol \"DRV8301_1_1\"\n"
            "      (pin input inverted_clock (at -10.16 2.54 0) (length 2.54) (name \"SCLK\") (number \"1\"))\n"
            "      (pin bidirectional line (at -10.16 -2.54 0) (length 2.54) (name \"SDIO\") (number \"2\"))\n"
            "      (pin output line (at 10.16 0 180) (length 2.54) (name \"GH_A\") (number \"3\"))))\n"
            "  (symbol \"DRV8303\" (extends \"DRV8301\")\n"
            "    (property \"Value\" \"DRV8303\" (at 0 -10.16 0)))\n"
            ")\n";
        const char* emptyKiSym = "(kicad_symbol_lib (version 20220914) (generator kicad_symbol_editor))\n";

        const std::string validSes =
            "(session \"motor_ctrl.ses\"\n"
            "  (base_design \"motor_ctrl.dsn\")\n"
            "  (placement\n"
            "    (resolution mil 10)\n"
            "    (component \"LQFP-48\" (place U1 5000 4000 front 90))\n"
            "    (component \"C_0603\"  (place C4 9500 4200 back 180)))\n"
            "  (routes\n"
            "    (resolution mil 10)\n"
            "    (library_out\n"
            "      (padstack \"Via30_15\" (shape (circle F.Cu 300)) (shape (circle B.Cu 300))))\n"
            "    (network_out\n"
            "      (net VCC_3V3\n"
            "        (via \"Via30_15\" 7000 4000)\n"
            "        (wire (path F.Cu 120 5000 4000 7000 4000))\n"
            "        (wire (path B.Cu 120 7000 4000 9500 4200)))\n"
            "      (net SPI_SCK\n"
            "        (wire (path F.Cu 80 5000 4500 8800 4500)))))\n"
            ")\n";
        const char* emptySes = "(session \"empty.ses\" (base_design \"none.dsn\"))\n";

        const std::string validPos =
            "### Module positions - created on 2026-11-16 ###\n"
            "## Unit = mil, Angle = deg.\n"
            "Designator,Comment,Package,Mid X,Mid Y,Rotation,Layer\n"
            "U1,\"STM32G431, Cortex-M4\",LQFP-32,1000.0,800.0,45.0,top\n"
            "Q1,\"BSS138, N-MOS\",SOT-23,1600.0,950.0,90.0,top\n"
            "C1,\"100nF, X7R\",C_0603,1250.0,600.0,180.0,bottom\n";
        const char* prosePos = "Quarterly revenue,Region,Notes\nNorth,East,No PCB coordinates here\n";

        ParsedEdaBoard bSym, badSym, bSes, badSes, bPos, badPos;
        bool okSym  = WriteAndInspect(dir + L"\\gate_drv.kicad_sym", validKiSym.data(), validKiSym.size(), bSym);
        bool rejSym = !WriteAndInspect(dir + L"\\empty.kicad_sym", emptyKiSym, std::strlen(emptyKiSym), badSym);
        bool okSes  = WriteAndInspect(dir + L"\\routed.ses", validSes.data(), validSes.size(), bSes);
        bool rejSes = !WriteAndInspect(dir + L"\\empty.ses", emptySes, std::strlen(emptySes), badSes);
        bool okPos  = WriteAndInspect(dir + L"\\pnp_jlcpcb.cpl", validPos.data(), validPos.size(), bPos);
        bool rejPos = !WriteAndInspect(dir + L"\\prose.pos", prosePos, std::strlen(prosePos), badPos);

        bool passItem18b = okSym && rejSym &&
                           bSym.components.size() == 2u && bSym.pads.size() == 6u && !bSym.circles.empty() &&
                           hasLabelSubstr(bSym, L"DRV8303") &&
                           okSes && rejSes &&
                           bSes.components.size() == 2u && bSes.vias.size() == 1u && bSes.netCount == 2u &&
                           okPos && rejPos &&
                           bPos.components.size() == 3u && bPos.pads.size() >= 37u &&
                           std::fabs(bPos.components[0].x - 25.4f) < 0.15f;

        tc.Record(passItem18b, L"Stage15.Eda.KiCadSym_SpecctraSes_PickAndPlacePosCpl",
            L"symPads=" + std::to_wstring(bSym.pads.size()) +
            L" sesVias=" + std::to_wstring(bSes.vias.size()) +
            L" posPads=" + std::to_wstring(bPos.pads.size()));
    }

    // ------------------------------------------------------------------------
    // 19. IBIS Buffer Model v6 (.ibs) & Touchstone 2.0 Differential 4-Port (.s4p)
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        const std::string validIbs =
            "[IBIS Ver]      6.1\n"
            "[File Name]     xc7k_io.ibs\n"
            "[Component]     XC7K325T_FFG900\n"
            "[Manufacturer]  AMD Xilinx\n"
            "[Package]\n"
            "R_pkg           45.0m     30.0m     65.0m\n"
            "L_pkg           1.8nH     1.2nH     2.5nH\n"
            "C_pkg           0.65pF    0.45pF    0.90pF\n"
            "[Pin]  signal_name  model_name   R_pin   L_pin   C_pin\n"
            "A1     TX_P         LVCMOS33_F   42.0m   1.5n    0.6p\n"
            "A2     TX_N         LVCMOS33_F   44.0m   1.6n    0.6p\n"
            "A3     GND          GND          NA      NA      NA\n"
            "A4     VCCO         POWER        NA      NA      NA\n"
            "[Model]         LVCMOS33_F\n"
            "Model_type      I/O\n"
            "Vinl = 0.8V\n"
            "Vinh = 2.0V\n"
            "C_comp          3.2pF     2.5pF     4.1pF\n"
            "[Pulldown]\n"
            "-3.30   -85.0mA   -60.0mA   -110.0mA\n"
            " 0.00     0.0mA     0.0mA      0.0mA\n"
            " 1.65    34.0mA    24.0mA     46.0mA\n"
            " 3.30    52.0mA    38.0mA     68.0mA\n"
            "[Pullup]\n"
            "-3.30    82.0mA    58.0mA    105.0mA\n"
            " 0.00     0.0mA     0.0mA      0.0mA\n"
            " 1.65   -31.0mA   -22.0mA    -42.0mA\n"
            " 3.30   -48.0mA   -35.0mA    -64.0mA\n"
            "[Rising Waveform]\n"
            "R_fixture = 50.0\n"
            "V_fixture = 0.0\n"
            "0.00nS   0.02V   0.01V   0.03V\n"
            "0.35nS   0.95V   0.70V   1.20V\n"
            "0.70nS   2.45V   2.05V   2.75V\n"
            "1.20nS   3.26V   3.10V   3.32V\n"
            "[Falling Waveform]\n"
            "R_fixture = 50.0\n"
            "V_fixture = 3.3\n"
            "0.00nS   3.28V   3.12V   3.33V\n"
            "0.40nS   1.85V   2.15V   1.45V\n"
            "0.85nS   0.25V   0.45V   0.12V\n"
            "1.20nS   0.03V   0.06V   0.01V\n"
            "[End]\n";
        const char* emptyIbs = "[IBIS Ver] 5.0\n[Component] EmptyShell\n[End]\n";

        // Touchstone 2.0 4-port PCIe channel (.s4p) with tab-delimited option line & [Network Data]
        const std::string validS4p =
            "! PCIe Gen4 Differential Pair (Port 1-3 = Lane+, Port 2-4 = Lane-)\n"
            "[Version] 2.0\n"
            "#\tGHz\tS\tMA\tR\t50\n"
            "[Number of Ports] 4\n"
            "[Number of Frequencies] 3\n"
            "[Network Data]\n"
            "1.0   0.08 -20  0.02 90  0.91 -35  0.03 140\n"
            "      0.02  90  0.08 -22 0.03 140  0.90 -36\n"
            "      0.91 -35  0.03 140 0.09 -25  0.02  88\n"
            "      0.03 140  0.90 -36 0.02  88  0.09 -24\n"
            "5.0   0.18 -75  0.04 40  0.74 -130 0.05  60\n"
            "      0.04  40  0.19 -77 0.05  60  0.73 -132\n"
            "      0.74 -130 0.05  60 0.18 -78  0.04  42\n"
            "      0.05  60  0.73 -132 0.04 42  0.19 -79\n"
            "10.0  0.32 -140 0.07 -10 0.52  110 0.08 -25\n"
            "      0.07 -10  0.33 -142 0.08 -25 0.51  108\n"
            "      0.52  110 0.08 -25 0.31 -144 0.07 -12\n"
            "      0.08 -25  0.51  108 0.07 -12 0.32 -145\n"
            "[End]\n";

        ParsedEdaBoard bIbs, badIbs, bS4p;
        bool okIbs  = WriteAndInspect(dir + L"\\xc7k_io.ibs", validIbs.data(), validIbs.size(), bIbs);
        bool rejIbs = !WriteAndInspect(dir + L"\\empty.ibs", emptyIbs, std::strlen(emptyIbs), badIbs);
        bool okS4p  = WriteAndInspect(dir + L"\\pcie_ch.s4p", validS4p.data(), validS4p.size(), bS4p);

        bool passItem19b = okIbs && rejIbs &&
                           bIbs.pads.size() == 4u && bIbs.traces.size() >= 25u &&
                           hasLabelSubstr(bIbs, L"XC7K325T_FFG900") &&
                           hasLabelSubstr(bIbs, L"V-I") && hasLabelSubstr(bIbs, L"V-T") &&
                           okS4p && bS4p.traces.size() >= 16u &&
                           hasLabelSubstr(bS4p, L"SDD21") && hasLabelSubstr(bS4p, L"SDD11");

        tc.Record(passItem19b, L"Stage15.Eda.IbisViVtPinout_And_TouchstoneS4pDifferential",
            L"ibsTr=" + std::to_wstring(bIbs.traces.size()) +
            L" s4pTr=" + std::to_wstring(bS4p.traces.size()));
    }

    // ------------------------------------------------------------------------
    // 20. KiCad Legacy Schematic (.sch) & EESchema-LIBRARY (.lib) Matrix Transforms
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        const std::string customSymLib =
            "EESchema-LIBRARY Version 2.4\n"
            "#encoding utf-8\n"
            "DEF STM32F030 U 0 40 Y Y 1 F N\n"
            "F0 \"U\" 0 250 50 H V C CNN\n"
            "F1 \"STM32F030\" 0 -250 50 H V C CNN\n"
            "DRAW\n"
            "S -200 200 200 -200 0 1 10 f\n"
            "C -140 140 25 0 1 6 N\n"
            "P 3 0 1 6 -200 50 -160 0 -200 -50 N\n"
            "X NRST 1 -350 100 150 R 50 50 1 1 I\n"
            "X PA0 2 -350 -100 150 R 50 50 1 1 B\n"
            "X SPI_MOSI 3 350 0 150 L 50 50 1 1 O\n"
            "ENDDRAW\n"
            "ENDDEF\n"
            "#End Library\n";

        const std::string legacySch =
            "EESchema Schematic File Version 4\n"
            "EELAYER 30 0\n"
            "EELAYER END\n"
            "$Descr A4 11693 8268\n"
            "Title \"STM32 Sensor Node\"\n"
            "Rev \"B\"\n"
            "$EndDescr\n"
            "$Comp\n"
            "L custom_mcu:STM32F030 U1\n"
            "U 1 1 61A01234\n"
            "P 4000 3500\n"
            "F 0 \"U1\" H 4000 3800 50  0000 C CNN\n"
            "F 1 \"STM32F030F4\" H 4000 3200 50  0000 C CNN\n"
            "F 2 \"Package_SO:TSSOP-20\" H 4000 3100 50  0001 C CNN\n"
            "\t1    4000 3500\n"
            "\t0    1    1    0\n"
            "$EndComp\n"
            "$Comp\n"
            "L Device:R R1\n"
            "U 1 1 61A01235\n"
            "P 2800 3500\n"
            "F 0 \"R1\" H 2800 3650 50  0000 C CNN\n"
            "F 1 \"10k\" H 2800 3350 50  0000 C CNN\n"
            "\t1    2800 3500\n"
            "\t1    0    0    -1\n"
            "$EndComp\n"
            "$Comp\n"
            "L power:GND #PWR01\n"
            "U 1 1 61A01236\n"
            "P 2800 4200\n"
            "F 0 \"#PWR01\" H 2800 4200 50  0001 C CNN\n"
            "F 1 \"GND\" H 2800 4100 50  0000 C CNN\n"
            "\t1    2800 4200\n"
            "\t1    0    0    -1\n"
            "$EndComp\n"
            "Wire Wire Line\n"
            "\t2800 3650 2800 4200\n"
            "Connection ~ 2800 3650\n"
            "NoConn ~ 4000 3850\n"
            "Text GLabel 4500 3500 2    50   Output ~ 0\n"
            "SPI_MOSI\n"
            "$Sheet\n"
            "S 5600 3000 1200 900\n"
            "F0 \"PowerStage\" 60\n"
            "F1 \"power_stage.sch\" 60\n"
            "F2 \"VIN_12V\" I L 5600 3300 50\n"
            "$EndSheet\n"
            "$EndSCHEMATC\n";

        const std::wstring libPath = dir + L"\\custom_mcu-cache.lib";
        const std::wstring schPath = dir + L"\\sensor_node.sch";
        PathUtils::WriteFileBytes(libPath, customSymLib.data(), customSymLib.size());
        PathUtils::WriteFileBytes(schPath, legacySch.data(), legacySch.size());

        ParsedEdaBoard bLegLib, bLegSch, badLegSch;
        bool isLikelyLib = EdaViewer::IsLikelyLibertyOrEdaSdfFile(libPath, L".lib");
        bool okLegLib = EdaViewer::InspectEdaFile(libPath, bLegLib) && bLegLib.valid;
        bool okLegSch = EdaViewer::InspectEdaFile(schPath, bLegSch) && bLegSch.valid;
        DeleteFileW(schPath.c_str());
        DeleteFileW(libPath.c_str());

        const char* badSchTxt = "Random text file not an EESchema or Eagle schematic\n";
        bool rejLegSch = !WriteAndInspect(dir + L"\\garbage.sch", badSchTxt, std::strlen(badSchTxt), badLegSch);

        // U1 + R1 + Sheet (Power symbol #PWR01 excluded from BOM components!) => 3 components, U1 resolved from companion .lib with 3 pins + R1 2 pins + Sheet 1 pin = 6 pads
        bool passLeg = isLikelyLib && okLegLib && okLegSch && rejLegSch &&
                       bLegLib.components.size() == 1u && bLegLib.pads.size() == 3u &&
                       bLegSch.components.size() == 3u && bLegSch.pads.size() >= 6u &&
                       bLegSch.vias.size() == 1u &&
                       hasLabelSubstr(bLegSch, L"STM32 Sensor Node") &&
                       hasLabelSubstr(bLegSch, L"SPI_MOSI") &&
                       hasLabelSubstr(bLegSch, L"PowerStage");

        tc.Record(passLeg, L"Stage15.Eda.KiCadLegacySchMatrix_And_SymLib",
            L"schComps=" + std::to_wstring(bLegSch.components.size()) +
            L" schPads=" + std::to_wstring(bLegSch.pads.size()));
    }

    // ------------------------------------------------------------------------
    // 21. FPGA .rpd Sync Framing, Real Cadence OrCAD Binary (.dsn/.olb), & Full HP-GL/2 (.hpgl/.plt)
    // ------------------------------------------------------------------------
    {
        auto hasLabelSubstr = [](const ParsedEdaBoard& b, std::wstring_view sub) -> bool {
            for (const auto& l : b.labels) {
                if (l.text.find(sub) != std::wstring::npos) return true;
            }
            return false;
        };

        // A) FPGA .rpd: Altera Active Serial LSb-reversed (32x 0xFF + 0x56 + payload) & Xilinx SPI bit-reversed (0x5599AA66 + IDCODE)
        std::vector<uint8_t> alteraRpd(512, 0xFFu);
        alteraRpd[32] = 0x56u; // Bit-reversed 0x6A Altera AS header marker
        for (size_t i = 33; i < 320; ++i) alteraRpd[i] = static_cast<uint8_t>((i * 37u) ^ 0x5Au);

        auto bitRev8 = [](uint8_t x) -> uint8_t {
            x = static_cast<uint8_t>(((x & 0xF0u) >> 4) | ((x & 0x0Fu) << 4));
            x = static_cast<uint8_t>(((x & 0xCCu) >> 2) | ((x & 0x33u) << 2));
            x = static_cast<uint8_t>(((x & 0xAAu) >> 1) | ((x & 0x55u) << 1));
            return x;
        };
        auto pushRevWord = [&](std::vector<uint8_t>& v, uint32_t w) {
            v.push_back(bitRev8(static_cast<uint8_t>((w >> 24) & 0xFFu)));
            v.push_back(bitRev8(static_cast<uint8_t>((w >> 16) & 0xFFu)));
            v.push_back(bitRev8(static_cast<uint8_t>((w >> 8)  & 0xFFu)));
            v.push_back(bitRev8(static_cast<uint8_t>(w & 0xFFu)));
        };
        std::vector<uint8_t> xilinxRpd(24, 0xFFu);
        pushRevWord(xilinxRpd, 0xAA995566u); // -> 0x5599AA66 on SPI flash!
        pushRevWord(xilinxRpd, 0x20000000u); // Type-1 NOOP
        pushRevWord(xilinxRpd, 0x30018001u); // Type-1 Write IDCODE (reg 12, cnt 1)
        pushRevWord(xilinxRpd, 0x0362D093u); // Artix-7 IDCODE
        pushRevWord(xilinxRpd, 0x30004040u); // Type-1 Write FDRI (reg 2, 64 words)
        for (int i = 0; i < 64; ++i) pushRevWord(xilinxRpd, 0x12345678u ^ static_cast<uint32_t>(i * 0x01030507u));

        std::vector<uint8_t> randomNoiseRpd(256);
        for (size_t i = 0; i < randomNoiseRpd.size(); ++i) randomNoiseRpd[i] = static_cast<uint8_t>(0x37u + (i % 13u));
        const char* proseRpd = "This is a plain text document renamed to .rpd and must not be accepted as FPGA flash data!\n";

        ParsedEdaBoard bAltRpd, bXilRpd, badRpd1, badRpd2;
        bool okAltRpd = WriteAndInspect(dir + L"\\cyclone_as.rpd", alteraRpd.data(), alteraRpd.size(), bAltRpd);
        bool okXilRpd = WriteAndInspect(dir + L"\\artix7_spi.rpd", xilinxRpd.data(), xilinxRpd.size(), bXilRpd);
        bool rejRpd   = !WriteAndInspect(dir + L"\\noise.rpd", randomNoiseRpd.data(), randomNoiseRpd.size(), badRpd1) &&
                        !WriteAndInspect(dir + L"\\prose.rpd", proseRpd, std::strlen(proseRpd), badRpd2);

        // B) Real Cadence OrCAD Capture v17.4 Binary OLE2 (.dsn with Library + Cache LibraryPart + Page FutureData prefix chains)
        auto appendOrCadStrZero = [](std::vector<uint8_t>& v, const std::string& s) {
            uint8_t l2[2] = {};
            PutU16LE(l2, static_cast<uint16_t>(s.size()));
            v.insert(v.end(), l2, l2 + 2);
            v.insert(v.end(), s.begin(), s.end());
            v.push_back(0u);
        };
        auto wrapOrCadStruct = [](uint8_t typeId, const std::vector<std::pair<uint32_t, uint32_t>>& props, const std::vector<uint8_t>& body) {
            std::vector<uint8_t> shortPfx;
            shortPfx.push_back(typeId);
            uint8_t pc[2] = {};
            PutU16LE(pc, props.empty() ? static_cast<uint16_t>(0u) : static_cast<uint16_t>(props.size()));
            shortPfx.insert(shortPfx.end(), pc, pc + 2);
            for (const auto& [ni, vi] : props) {
                uint8_t kv[8] = {};
                PutU32LE(kv + 0, ni);
                PutU32LE(kv + 4, vi);
                shortPfx.insert(shortPfx.end(), kv, kv + 8);
            }
            std::vector<uint8_t> out(9);
            out[0] = typeId;
            PutU32LE(out.data() + 1, static_cast<uint32_t>(shortPfx.size() + body.size()));
            PutU32LE(out.data() + 5, 0u);
            out.insert(out.end(), shortPfx.begin(), shortPfx.end());
            out.insert(out.end(), body.begin(), body.end());
            return out;
        };

        // Build OrCAD "Library" stream (32B "OrCAD Windows Design" + header + 1 font + someLen=24 + 8 partFields + 152B PageSettings + strLst)
        std::vector<uint8_t> orcLib(32, 0x20u);
        std::memcpy(orcLib.data(), "OrCAD Windows Design", 20);
        orcLib[20] = 0u;
        {
            uint8_t hdr[16] = { 3,0, 2,0,  0,0,0,0,  0,0,0,0,  0,0,0,0 };
            orcLib.insert(orcLib.end(), hdr, hdr + 16);
            uint8_t fl[2] = { 1, 0 }; // textFontLen = 1 -> 0 LOGFONTAs
            orcLib.insert(orcLib.end(), fl, fl + 2);
            uint8_t sl[2] = { 24, 0 };
            orcLib.insert(orcLib.end(), sl, sl + 2);
            orcLib.insert(orcLib.end(), 24 * 2 + 8, 0u);
            for (int i = 0; i < 8; ++i) appendOrCadStrZero(orcLib, "");
            orcLib.insert(orcLib.end(), 152, 0u); // PageSettings
            uint8_t cnt4[4] = { 2, 0, 0, 0 };     // strLst: [0]="Value", [1]="ADS1115"
            orcLib.insert(orcLib.end(), cnt4, cnt4 + 4);
            appendOrCadStrZero(orcLib, "Value");
            appendOrCadStrZero(orcLib, "ADS1115");
            orcLib.push_back(0); orcLib.push_back(0); // aliasLstLen = 0
        }

        // Build OrCAD "Cache" stream containing LibraryPart (typeId=24) "ADS1115.Normal" with Rect(40,40) + 3 SymbolPinScalar(26)
        std::vector<uint8_t> orcCache = { 0,0,0,0 };
        {
            std::vector<uint8_t> lpBody;
            appendOrCadStrZero(lpBody, "ADS1115.Normal");
            appendOrCadStrZero(lpBody, "ANALOG.OLB");
            lpBody.insert(lpBody.end(), 4, 0u);
            uint8_t np[2] = { 1, 0 }; // 1 primitive: Rect (40, 40)
            lpBody.insert(lpBody.end(), np, np + 2);
            lpBody.push_back(40u); lpBody.push_back(40u);
            uint8_t rectBuf[24] = {};
            PutU32LE(rectBuf + 0, 24u);
            PutI32LE(rectBuf + 8,  0);  PutI32LE(rectBuf + 12, 0);
            PutI32LE(rectBuf + 16, 40); PutI32LE(rectBuf + 20, 30);
            lpBody.insert(lpBody.end(), rectBuf, rectBuf + 24);

            uint8_t nPins[2] = { 3, 0 };
            lpBody.insert(lpBody.end(), nPins, nPins + 2);
            auto addSymPin = [&](const char* nm, int32_t sx, int32_t sy, int32_t hx, int32_t hy) {
                std::vector<uint8_t> pb;
                appendOrCadStrZero(pb, nm);
                uint8_t coords[28] = {};
                PutI32LE(coords + 0, sx); PutI32LE(coords + 4, sy);
                PutI32LE(coords + 8, hx); PutI32LE(coords + 12, hy);
                pb.insert(pb.end(), coords, coords + 28);
                auto wrapped = wrapOrCadStruct(26u, {}, pb);
                lpBody.insert(lpBody.end(), wrapped.begin(), wrapped.end());
            };
            addSymPin("AIN0", 0, 10, -10, 10);
            addSymPin("AIN1", 0, 20, -10, 20);
            addSymPin("SDA",  40, 15, 50, 15);

            uint8_t z2[2] = { 0, 0 }; // 0 display props
            lpBody.insert(lpBody.end(), z2, z2 + 2);
            auto lpStruct = wrapOrCadStruct(24u, {}, lpBody);
            orcCache.insert(orcCache.end(), lpStruct.begin(), lpStruct.end());
        }

        // Build OrCAD "Views/SCHEMATIC1/Pages/PAGE1" stream (Structure::Page == 10)
        std::vector<uint8_t> orcPage;
        {
            std::vector<uint8_t> pgBody;
            appendOrCadStrZero(pgBody, "ADC_FRONTEND");
            appendOrCadStrZero(pgBody, "A");
            std::vector<uint8_t> pset(152, 0u);
            PutU32LE(pset.data() + 24, 9700u); // width in mils
            PutU32LE(pset.data() + 28, 7200u); // height in mils
            PutU32LE(pset.data() + 32, 10u);   // pinToPin = 10
            pgBody.insert(pgBody.end(), pset.begin(), pset.end());

            auto pushU16 = [&](uint16_t v) { uint8_t b2[2]; PutU16LE(b2, v); pgBody.insert(pgBody.end(), b2, b2 + 2); };
            pushU16(0); pushU16(0); pushU16(0); // TitleBlocks=0, T0x34=0, T0x35=0
            pushU16(1); // 1 net mapping
            appendOrCadStrZero(pgBody, "I2C_SDA");
            uint8_t nid[4] = { 1,0,0,0 };
            pgBody.insert(pgBody.end(), nid, nid + 4);

            // 2 WireScalar (typeId=20) with an Alias (typeId=49) on wire 0
            pushU16(2);
            {
                std::vector<uint8_t> wb(29, 0u);
                PutU32LE(wb.data() + 4, 1u);
                PutI32LE(wb.data() + 12, 150); PutI32LE(wb.data() + 16, 115);
                PutI32LE(wb.data() + 20, 220); PutI32LE(wb.data() + 24, 115);
                uint8_t na[2] = { 1, 0 };
                wb.insert(wb.end(), na, na + 2);
                std::vector<uint8_t> ab(20, 0u);
                PutI32LE(ab.data() + 0, 170); PutI32LE(ab.data() + 4, 115);
                appendOrCadStrZero(ab, "I2C_SDA");
                auto aSt = wrapOrCadStruct(49u, {}, ab);
                wb.insert(wb.end(), aSt.begin(), aSt.end());
                wb.insert(wb.end(), 10, 0u); // 0 dispProps + lineWidth/Style
                auto wSt = wrapOrCadStruct(20u, {}, wb);
                pgBody.insert(pgBody.end(), wSt.begin(), wSt.end());
            }
            {
                std::vector<uint8_t> wb(29 + 12, 0u);
                PutU32LE(wb.data() + 4, 2u);
                PutI32LE(wb.data() + 12, 50); PutI32LE(wb.data() + 16, 110);
                PutI32LE(wb.data() + 20, 90); PutI32LE(wb.data() + 24, 110);
                auto wSt = wrapOrCadStruct(20u, {}, wb);
                pgBody.insert(pgBody.end(), wSt.begin(), wSt.end());
            }

            // 1 PlacedInstance (typeId=13) referencing "ADS1115.Normal" at (100, 100) with prop (0="Value" -> 1="ADS1115")
            pushU16(1);
            {
                std::vector<uint8_t> ib(8, 0u);
                appendOrCadStrZero(ib, "ADS1115.Normal");
                uint8_t tail1[22] = {};
                PutU16LE(tail1 + 12, 100u); // locX
                PutU16LE(tail1 + 14, 100u); // locY
                ib.insert(ib.end(), tail1, tail1 + 22); // dbId(4)+unk(8)+locX(2)+locY(2)+flags(4)+nDisp(2=0)
                ib.push_back(0u);
                appendOrCadStrZero(ib, "U3");
                ib.insert(ib.end(), 16, 0u); // unk(14)+nT10(2=0)
                appendOrCadStrZero(ib, "ADS1115");
                ib.insert(ib.end(), 2, 0u);
                auto iSt = wrapOrCadStruct(13u, { { 0u, 1u } }, ib);
                pgBody.insert(pgBody.end(), iSt.begin(), iSt.end());
            }

            // Remaining counts = 0 (Ports, Globals, OffPage, ERC, BusEntries, GraphicInsts, len10, len11)
            for (int z = 0; z < 8; ++z) pushU16(0);
            orcPage = wrapOrCadStruct(10u, {}, pgBody);
        }

        auto realOrcOle = BuildOle2CompoundFile({
            { L"Library",                      orcLib   },
            { L"Cache",                        orcCache },
            { L"Views/SCHEMATIC1/Pages/PAGE1", orcPage  }
        });

        ParsedEdaBoard bRealOrc;
        bool okRealOrc = WriteAndInspect(dir + L"\\real_orcad_v17.dsn", realOrcOle.data(), realOrcOle.size(), bRealOrc);

        // C) HP-GL/2 (.hpgl/.plt): IP/SC scaling, AA arc, EA/RA rect, PM/FP polygon, DT+LB with embedded "PU"/"CI" text, & prose rejection
        const std::string validHpgl =
            "IN;IP0,0,4000,4000;SC0,100,0,100;\n"
            "WU0;PW0.35,1;SP1;\n"
            "PA10,10;PD50,10,50,40;\n"
            "AA50,55,180;\n"
            "EA85,85;\n"
            "SP2;PU20,70;SI0.25,0.35;DT#;LBTOP_COPPER_PU100,200_CI50#;\n"
            "SP3;PU60,20;PM0;PD80,20,70,40;PM2;FP;\n";
        const char* prosePlt =
            "Project schedule and parts delivery notes for the Paris office.\n"
            "Please pack clean items carefully before Tuesday.\n";

        ParsedEdaBoard bHpgl, badHpgl;
        bool okHpgl  = WriteAndInspect(dir + L"\\fab_plot.plt", validHpgl.data(), validHpgl.size(), bHpgl);
        bool rejHpgl = !WriteAndInspect(dir + L"\\notes.plt", prosePlt, std::strlen(prosePlt), badHpgl);

        bool passItem21 = okAltRpd && okXilRpd && rejRpd &&
                          bAltRpd.formatTitle.find(L"Altera AS") != std::wstring::npos &&
                          bXilRpd.formatTitle.find(L"Xilinx") != std::wstring::npos &&
                          hasLabelSubstr(bXilRpd, L"0x0362D093") &&
                          HashEdaGeometry(bAltRpd) != HashEdaGeometry(bXilRpd) &&
                          okRealOrc && bRealOrc.components.size() == 1u && bRealOrc.pads.size() == 3u &&
                          hasLabelSubstr(bRealOrc, L"U3 (ADS1115)") &&
                          hasLabelSubstr(bRealOrc, L"I2C_SDA") &&
                          okHpgl && rejHpgl &&
                          bHpgl.netCount == 3u && bHpgl.traces.size() >= 20u &&
                          hasLabelSubstr(bHpgl, L"TOP_COPPER_PU100,200_CI50");

        tc.Record(passItem21, L"Stage15.Eda.FpgaRpdSync_RealOrCadBin_And_Hpgl2Full",
            L"orcPads=" + std::to_wstring(bRealOrc.pads.size()) +
            L" hpglTr=" + std::to_wstring(bHpgl.traces.size()));
    }

    RemoveDirectoryW(dir.c_str());
    RunFormatTestsEdaBatch70(tc);
}

} // namespace Explorer2::SelfTestSuite
