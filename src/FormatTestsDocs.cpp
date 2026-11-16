// FormatTestsDocs.cpp: self-tests of the office, data and text document readers (SpreadsheetDocViewer, formats/), stage 15 ("15.docsa" runs these alone)
#include "SelfTestCommon.h"
#include "SpreadsheetDocViewer.h"
#include "FileContentPane.h"
#include "PathUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

// ============================================================================
// Binary Writer & Checksum Helpers
// ============================================================================
inline void AppendU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
inline void AppendBytes(std::vector<uint8_t>& b, const void* p, size_t n) {
    const auto* u = static_cast<const uint8_t*>(p);
    b.insert(b.end(), u, u + n);
}
inline void AppendStr(std::vector<uint8_t>& b, std::string_view s) {
    AppendBytes(b, s.data(), s.size());
}

inline void AppendU16LE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}
inline void AppendU32LE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}
inline void AppendI32LE(std::vector<uint8_t>& b, int32_t v) { AppendU32LE(b, static_cast<uint32_t>(v)); }
inline void AppendU64LE(std::vector<uint8_t>& b, uint64_t v) {
    AppendU32LE(b, static_cast<uint32_t>(v & 0xFFFFFFFFULL));
    AppendU32LE(b, static_cast<uint32_t>(v >> 32));
}
inline void AppendI64LE(std::vector<uint8_t>& b, int64_t v) { AppendU64LE(b, static_cast<uint64_t>(v)); }
inline void AppendF32LE(std::vector<uint8_t>& b, float f) {
    uint32_t u = 0; std::memcpy(&u, &f, 4); AppendU32LE(b, u);
}
inline void AppendF64LE(std::vector<uint8_t>& b, double d) {
    uint64_t u = 0; std::memcpy(&u, &d, 8); AppendU64LE(b, u);
}

inline void AppendU16BE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
}
inline void AppendU32BE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
}
inline void AppendI32BE(std::vector<uint8_t>& b, int32_t v) { AppendU32BE(b, static_cast<uint32_t>(v)); }
inline void AppendU64BE(std::vector<uint8_t>& b, uint64_t v) {
    AppendU32BE(b, static_cast<uint32_t>(v >> 32));
    AppendU32BE(b, static_cast<uint32_t>(v & 0xFFFFFFFFULL));
}
inline void AppendF32BE(std::vector<uint8_t>& b, float f) {
    uint32_t u = 0; std::memcpy(&u, &f, 4); AppendU32BE(b, u);
}
inline void AppendF64BE(std::vector<uint8_t>& b, double d) {
    uint64_t u = 0; std::memcpy(&u, &d, 8); AppendU64BE(b, u);
}

inline void PutU16LE(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
inline void PutU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16); p[3] = static_cast<uint8_t>(v >> 24);
}
inline void PutU64LE(uint8_t* p, uint64_t v) {
    PutU32LE(p, static_cast<uint32_t>(v));
    PutU32LE(p + 4, static_cast<uint32_t>(v >> 32));
}
inline void PutF32LE(uint8_t* p, float f) { uint32_t u = 0; std::memcpy(&u, &f, 4); PutU32LE(p, u); }
inline void PutF64LE(uint8_t* p, double d) { uint64_t u = 0; std::memcpy(&u, &d, 8); PutU64LE(p, u); }

inline void PutU16BE(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
inline void PutU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);  p[3] = static_cast<uint8_t>(v);
}

inline uint32_t ComputeCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

// Raw RFC 1951 Stored Deflate stream (valid for any size)
inline std::vector<uint8_t> MakeDeflateStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    if (raw.empty()) {
        out = { 0x01, 0x00, 0x00, 0xFF, 0xFF };
        return out;
    }
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t chunk = std::min<size_t>(raw.size() - pos, 32768);
        bool isLast = (pos + chunk == raw.size());
        AppendU8(out, isLast ? 0x01u : 0x00u);
        uint16_t clen = static_cast<uint16_t>(chunk);
        AppendU16LE(out, clen);
        AppendU16LE(out, static_cast<uint16_t>(~clen));
        AppendBytes(out, raw.data() + pos, chunk);
        pos += chunk;
    }
    return out;
}

// RFC 1952 GZIP wrapper around Stored Deflate blocks
inline std::vector<uint8_t> MakeGzipStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> gz = { 0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0x03 };
    std::vector<uint8_t> def = MakeDeflateStored(raw);
    AppendBytes(gz, def.data(), def.size());
    AppendU32LE(gz, ComputeCrc32(raw.data(), raw.size()));
    AppendU32LE(gz, static_cast<uint32_t>(raw.size()));
    return gz;
}

// Uncompressed PKZIP archive builder (Store method 0 + Central Directory + EOCD)
inline std::vector<uint8_t> MakeZipStoreArchive(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::vector<uint8_t> zip;
    struct CdRec {
        std::string name;
        uint32_t crc = 0;
        uint32_t sz = 0;
        uint32_t localOff = 0;
    };
    std::vector<CdRec> cdRecs;

    for (const auto& kv : files) {
        CdRec r;
        r.name = kv.first;
        r.crc = ComputeCrc32(kv.second.data(), kv.second.size());
        r.sz = static_cast<uint32_t>(kv.second.size());
        r.localOff = static_cast<uint32_t>(zip.size());

        AppendU32LE(zip, 0x04034B50u);
        AppendU16LE(zip, 20); // version needed
        AppendU16LE(zip, 0);  // flags
        AppendU16LE(zip, 0);  // method = Store
        AppendU16LE(zip, 0);  // time
        AppendU16LE(zip, 0);  // date
        AppendU32LE(zip, r.crc);
        AppendU32LE(zip, r.sz);
        AppendU32LE(zip, r.sz);
        AppendU16LE(zip, static_cast<uint16_t>(r.name.size()));
        AppendU16LE(zip, 0);
        AppendStr(zip, r.name);
        AppendBytes(zip, kv.second.data(), kv.second.size());
        cdRecs.push_back(std::move(r));
    }

    uint32_t cdOff = static_cast<uint32_t>(zip.size());
    for (const auto& r : cdRecs) {
        AppendU32LE(zip, 0x02014B50u);
        AppendU16LE(zip, 20);
        AppendU16LE(zip, 20);
        AppendU16LE(zip, 0);
        AppendU16LE(zip, 0);
        AppendU16LE(zip, 0);
        AppendU16LE(zip, 0);
        AppendU32LE(zip, r.crc);
        AppendU32LE(zip, r.sz);
        AppendU32LE(zip, r.sz);
        AppendU16LE(zip, static_cast<uint16_t>(r.name.size()));
        AppendU16LE(zip, 0);
        AppendU16LE(zip, 0);
        AppendU16LE(zip, 0);
        AppendU16LE(zip, 0);
        AppendU32LE(zip, 0);
        AppendU32LE(zip, r.localOff);
        AppendStr(zip, r.name);
    }
    uint32_t cdSize = static_cast<uint32_t>(zip.size()) - cdOff;

    AppendU32LE(zip, 0x06054B50u);
    AppendU16LE(zip, 0);
    AppendU16LE(zip, 0);
    AppendU16LE(zip, static_cast<uint16_t>(cdRecs.size()));
    AppendU16LE(zip, static_cast<uint16_t>(cdRecs.size()));
    AppendU32LE(zip, cdSize);
    AppendU32LE(zip, cdOff);
    AppendU16LE(zip, 0);
    return zip;
}

// Minimal 512B-sector OLE2 Compound File Binary (MS-CFB v3) wrapping a single named stream
inline std::vector<uint8_t> MakeOle2SingleStream(std::wstring_view streamName, const std::vector<uint8_t>& streamData) {
    const size_t dataSectors = std::max<size_t>(1, (streamData.size() + 511) / 512);
    // Sector 0 = FAT, Sector 1 = Directory, Sectors 2..2+dataSectors-1 = Stream payload
    const size_t totalSectors = 2 + dataSectors;
    std::vector<uint8_t> cfb((1 + totalSectors) * 512, 0);

    static const uint8_t kMagic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    std::memcpy(cfb.data(), kMagic, 8);
    PutU16LE(cfb.data() + 24, 0x003Eu); // minor version
    PutU16LE(cfb.data() + 26, 0x0003u); // major version 3 (512B sectors)
    PutU16LE(cfb.data() + 28, 0xFFFEu); // little-endian byte order
    PutU16LE(cfb.data() + 30, 9);       // sector shift = 9 (512)
    PutU16LE(cfb.data() + 32, 6);       // mini-sector shift = 6 (64)
    PutU32LE(cfb.data() + 44, 1);       // 1 FAT sector
    PutU32LE(cfb.data() + 48, 1);       // first Directory sector = 1
    PutU32LE(cfb.data() + 56, 4096);    // mini-stream cutoff = 4096
    PutU32LE(cfb.data() + 60, 0xFFFFFFFEu); // no MiniFAT
    PutU32LE(cfb.data() + 68, 0xFFFFFFFEu); // no extra DIFAT
    for (int i = 0; i < 109; ++i) PutU32LE(cfb.data() + 76 + i * 4, 0xFFFFFFFFu);
    PutU32LE(cfb.data() + 76, 0); // DIFAT[0] = Sector 0

    // Sector 0 (offset 512): FAT
    uint8_t* fat = cfb.data() + 512;
    for (size_t i = 0; i < 128; ++i) PutU32LE(fat + i * 4, 0xFFFFFFFFu);
    PutU32LE(fat + 0 * 4, 0xFFFFFFFDu); // FATSECT
    PutU32LE(fat + 1 * 4, 0xFFFFFFFEu); // Directory ENDOFCHAIN
    for (size_t s = 0; s < dataSectors; ++s) {
        uint32_t nextSec = (s + 1 < dataSectors) ? static_cast<uint32_t>(2 + s + 1) : 0xFFFFFFFEu;
        PutU32LE(fat + (2 + s) * 4, nextSec);
    }

    // Sector 1 (offset 1024): Directory
    uint8_t* dir = cfb.data() + 1024;
    auto writeDirName = [](uint8_t* ent, std::wstring_view nm) {
        size_t n = std::min<size_t>(nm.size(), 31);
        for (size_t i = 0; i < n; ++i) PutU16LE(ent + i * 2, static_cast<uint16_t>(nm[i]));
        PutU16LE(ent + n * 2, 0);
        PutU16LE(ent + 64, static_cast<uint16_t>((n + 1) * 2));
    };

    // Entry 0: Root Entry (size = 0 so miniStream is empty and streams read directly from FAT sectors)
    writeDirName(dir, L"Root Entry");
    dir[66] = 5; // Root Storage
    dir[67] = 1; // Black
    PutU32LE(dir + 68, 0xFFFFFFFFu);
    PutU32LE(dir + 72, 0xFFFFFFFFu);
    PutU32LE(dir + 76, 1); // childId = 1
    PutU32LE(dir + 116, 0xFFFFFFFEu);
    PutU32LE(dir + 120, 0);

    // Entry 1: Stream
    uint8_t* ent1 = dir + 128;
    writeDirName(ent1, streamName);
    ent1[66] = 2; // Stream
    ent1[67] = 1;
    PutU32LE(ent1 + 68, 0xFFFFFFFFu);
    PutU32LE(ent1 + 72, 0xFFFFFFFFu);
    PutU32LE(ent1 + 76, 0xFFFFFFFFu);
    PutU32LE(ent1 + 116, 2); // startSector = 2
    PutU32LE(ent1 + 120, static_cast<uint32_t>(streamData.size()));

    if (!streamData.empty()) {
        std::memcpy(cfb.data() + 1536, streamData.data(), streamData.size());
    }
    return cfb;
}

// Multi-stream 512B-sector OLE2 Compound File Binary (MS-CFB v3) for .doc ("WordDocument" + "1Table") & .ppt
inline std::vector<uint8_t> MakeOle2MultiStream(const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& streams) {
    const size_t dirEntries = 1 + streams.size();
    const size_t dirSectors = (dirEntries + 3) / 4;
    size_t dataSectorsTotal = 0;
    std::vector<size_t> streamStartSec(streams.size(), 0);
    std::vector<size_t> streamSecCount(streams.size(), 0);
    for (size_t i = 0; i < streams.size(); ++i) {
        streamStartSec[i] = 1 + dirSectors + dataSectorsTotal;
        streamSecCount[i] = std::max<size_t>(1, (streams[i].second.size() + 511) / 512);
        dataSectorsTotal += streamSecCount[i];
    }
    const size_t totalSectors = 1 + dirSectors + dataSectorsTotal; // Sector 0 = FAT (supports up to 128 sectors = 64 KB)
    std::vector<uint8_t> cfb((1 + totalSectors) * 512, 0);

    static const uint8_t kMagic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    std::memcpy(cfb.data(), kMagic, 8);
    PutU16LE(cfb.data() + 24, 0x003Eu);
    PutU16LE(cfb.data() + 26, 0x0003u);
    PutU16LE(cfb.data() + 28, 0xFFFEu);
    PutU16LE(cfb.data() + 30, 9);
    PutU16LE(cfb.data() + 32, 6);
    PutU32LE(cfb.data() + 44, 1); // 1 FAT sector
    PutU32LE(cfb.data() + 48, 1); // first Directory sector = 1
    PutU32LE(cfb.data() + 56, 4096);
    PutU32LE(cfb.data() + 60, 0xFFFFFFFEu);
    PutU32LE(cfb.data() + 68, 0xFFFFFFFEu);
    for (int i = 0; i < 109; ++i) PutU32LE(cfb.data() + 76 + i * 4, 0xFFFFFFFFu);
    PutU32LE(cfb.data() + 76, 0);

    uint8_t* fat = cfb.data() + 512;
    for (size_t i = 0; i < 128; ++i) PutU32LE(fat + i * 4, 0xFFFFFFFFu);
    PutU32LE(fat + 0 * 4, 0xFFFFFFFDu); // FATSECT
    for (size_t ds = 0; ds < dirSectors; ++ds) {
        uint32_t next = (ds + 1 < dirSectors) ? static_cast<uint32_t>(1 + ds + 1) : 0xFFFFFFFEu;
        PutU32LE(fat + (1 + ds) * 4, next);
    }
    for (size_t i = 0; i < streams.size(); ++i) {
        for (size_t s = 0; s < streamSecCount[i]; ++s) {
            const size_t secIdx = streamStartSec[i] + s;
            uint32_t next = (s + 1 < streamSecCount[i]) ? static_cast<uint32_t>(secIdx + 1) : 0xFFFFFFFEu;
            PutU32LE(fat + secIdx * 4, next);
        }
    }

    auto writeDirName = [](uint8_t* ent, std::wstring_view nm) {
        size_t n = std::min<size_t>(nm.size(), 31);
        for (size_t i = 0; i < n; ++i) PutU16LE(ent + i * 2, static_cast<uint16_t>(nm[i]));
        PutU16LE(ent + n * 2, 0);
        PutU16LE(ent + 64, static_cast<uint16_t>((n + 1) * 2));
    };

    uint8_t* dir = cfb.data() + 1024;
    writeDirName(dir, L"Root Entry");
    dir[66] = 5;
    dir[67] = 1;
    PutU32LE(dir + 68, 0xFFFFFFFFu);
    PutU32LE(dir + 72, 0xFFFFFFFFu);
    PutU32LE(dir + 76, streams.empty() ? 0xFFFFFFFFu : 1u);
    PutU32LE(dir + 116, 0xFFFFFFFEu);
    PutU32LE(dir + 120, 0);

    for (size_t i = 0; i < streams.size(); ++i) {
        uint8_t* ent = dir + (1 + i) * 128;
        writeDirName(ent, streams[i].first);
        ent[66] = 2; // Stream
        ent[67] = 1;
        PutU32LE(ent + 68, 0xFFFFFFFFu);
        PutU32LE(ent + 72, (i + 1 < streams.size()) ? static_cast<uint32_t>(1 + i + 1) : 0xFFFFFFFFu);
        PutU32LE(ent + 76, 0xFFFFFFFFu);
        PutU32LE(ent + 116, static_cast<uint32_t>(streamStartSec[i]));
        PutU32LE(ent + 120, static_cast<uint32_t>(streams[i].second.size()));

        if (!streams[i].second.empty()) {
            std::memcpy(cfb.data() + (1 + streamStartSec[i]) * 512, streams[i].second.data(), streams[i].second.size());
        }
    }
    return cfb;
}

// ============================================================================
// Inspection & Hashing Helpers
// ============================================================================
struct TempFileRegistry {
    std::wstring dir;
    std::vector<std::wstring> files;

    explicit TempFileRegistry(const std::wstring& subName) {
        dir = ScratchTestDir() + L"\\" + subName;
        CreateDirectoryW(dir.c_str(), nullptr);
    }

    std::wstring Write(const std::wstring& fileName, const std::vector<uint8_t>& bytes) {
        std::wstring p = dir + L"\\" + fileName;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        files.push_back(p);
        return p;
    }

    std::wstring WriteText(const std::wstring& fileName, std::string_view utf8) {
        std::wstring p = dir + L"\\" + fileName;
        PathUtils::WriteFileBytes(p, utf8.data(), utf8.size());
        files.push_back(p);
        return p;
    }

    void Cleanup() {
        for (const auto& f : files) DeleteFileW(f.c_str());
        RemoveDirectoryW(dir.c_str());
    }
};

inline uint64_t HashParsedDoc(const ParsedOfficeDocument& d) {
    uint64_t h = 14695981039346656037ULL;
    auto mixByte = [&](uint8_t b) {
        h ^= b;
        h *= 1099511628211ULL;
    };
    auto mixWStr = [&](std::wstring_view w) {
        for (wchar_t ch : w) {
            uint16_t u = static_cast<uint16_t>(ch);
            mixByte(static_cast<uint8_t>(u & 0xFFu));
            mixByte(static_cast<uint8_t>(u >> 8));
        }
        mixByte(0xFFu);
    };
    auto mixDouble = [&](double v) {
        int64_t q = std::isfinite(v) ? static_cast<int64_t>(std::llround(v * 1e6)) : 0;
        for (int i = 0; i < 8; ++i) mixByte(static_cast<uint8_t>((static_cast<uint64_t>(q) >> (i * 8)) & 0xFFu));
    };

    mixByte(static_cast<uint8_t>(d.kind));
    mixByte(d.isSpreadsheet ? 1u : 0u);
    for (const auto& s : d.sheets) {
        mixWStr(s.name);
        for (const auto& r : s.rows) {
            for (const auto& c : r) mixWStr(c);
            mixByte(0xFEu);
        }
    }
    for (const auto& p : d.paragraphs) {
        mixByte(static_cast<uint8_t>(p.headingLevel));
        mixByte(p.isBullet ? 1u : 0u);
        mixWStr(p.text);
    }
    for (const auto& gp : d.geoPoints) {
        mixDouble(gp.lat);
        mixDouble(gp.lon);
        mixDouble(gp.elevM);
        mixWStr(gp.label);
    }
    return h;
}

inline bool DocContains(const ParsedOfficeDocument& d, std::wstring_view needle) {
    if (d.formatTitle.find(needle) != std::wstring::npos) return true;
    for (const auto& s : d.sheets) {
        if (s.name.find(needle) != std::wstring::npos) return true;
        for (const auto& r : s.rows) {
            for (const auto& c : r) {
                if (c.find(needle) != std::wstring::npos) return true;
            }
        }
    }
    for (const auto& p : d.paragraphs) {
        if (p.text.find(needle) != std::wstring::npos) return true;
    }
    for (const auto& gp : d.geoPoints) {
        if (gp.label.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

inline bool DocContainsCI(const ParsedOfficeDocument& d, std::wstring_view needle) {
    const std::wstring nl = PathUtils::ToLower(needle);
    if (PathUtils::ToLower(d.formatTitle).find(nl) != std::wstring::npos) return true;
    for (const auto& s : d.sheets) {
        if (PathUtils::ToLower(s.name).find(nl) != std::wstring::npos) return true;
        for (const auto& r : s.rows) {
            for (const auto& c : r) {
                if (PathUtils::ToLower(c).find(nl) != std::wstring::npos) return true;
            }
        }
    }
    for (const auto& p : d.paragraphs) {
        if (PathUtils::ToLower(p.text).find(nl) != std::wstring::npos) return true;
    }
    return false;
}

inline bool SheetContains(const ParsedOfficeDocument& d, std::wstring_view sheetName, std::wstring_view needle) {
    for (const auto& s : d.sheets) {
        if (s.name == sheetName) {
            for (const auto& r : s.rows) {
                for (const auto& c : r) {
                    if (c.find(needle) != std::wstring::npos) return true;
                }
            }
        }
    }
    return false;
}

inline bool InspectFails(const std::wstring& path) {
    ParsedOfficeDocument doc;
    return !SpreadsheetDocViewer::InspectOfficeFile(path, doc) || !doc.valid;
}

// ============================================================================
// Wire Protocol Mini-Builders (Thrift Compact, Protobuf, FlatBuffers)
// ============================================================================
struct ThriftCompactWriter {
    std::vector<uint8_t> buf;
    std::vector<int16_t> fieldStack = { 0 };

    void WriteVarint(uint64_t v) {
        while (v >= 0x80u) {
            buf.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
            v >>= 7;
        }
        buf.push_back(static_cast<uint8_t>(v));
    }
    void WriteZigZag32(int32_t v) {
        uint32_t zz = (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31);
        WriteVarint(zz);
    }
    void WriteZigZag64(int64_t v) {
        uint64_t zz = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
        WriteVarint(zz);
    }
    void FieldHdr(int16_t fid, uint8_t ctype) {
        int16_t prev = fieldStack.back();
        int16_t delta = static_cast<int16_t>(fid - prev);
        if (delta > 0 && delta <= 15) {
            buf.push_back(static_cast<uint8_t>((delta << 4) | (ctype & 0x0Fu)));
        } else {
            buf.push_back(static_cast<uint8_t>(ctype & 0x0Fu));
            uint32_t zz = (static_cast<uint32_t>(static_cast<int32_t>(fid)) << 1) ^ static_cast<uint32_t>(static_cast<int32_t>(fid) >> 31);
            WriteVarint(zz);
        }
        fieldStack.back() = fid;
    }
    void WriteI32(int16_t fid, int32_t v) { FieldHdr(fid, 5); WriteZigZag32(v); }
    void WriteI64(int16_t fid, int64_t v) { FieldHdr(fid, 6); WriteZigZag64(v); }
    void WriteBinary(int16_t fid, std::string_view s) {
        FieldHdr(fid, 8);
        WriteVarint(s.size());
        AppendStr(buf, s);
    }
    void BeginStructField(int16_t fid) { FieldHdr(fid, 12); fieldStack.push_back(0); }
    void BeginListElemStruct() { fieldStack.push_back(0); }
    void EndStruct() { buf.push_back(0x00); fieldStack.pop_back(); }
    void BeginList(int16_t fid, uint8_t elemType, uint32_t count) {
        FieldHdr(fid, 9);
        if (count < 15) {
            buf.push_back(static_cast<uint8_t>((count << 4) | (elemType & 0x0Fu)));
        } else {
            buf.push_back(static_cast<uint8_t>(0xF0u | (elemType & 0x0Fu)));
            WriteVarint(count);
        }
    }
};

struct ProtoWriter {
    std::vector<uint8_t> buf;
    void WriteVarint(uint64_t v) {
        while (v >= 0x80u) {
            buf.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
            v >>= 7;
        }
        buf.push_back(static_cast<uint8_t>(v));
    }
    void WriteTag(uint32_t fieldNum, uint8_t wireType) {
        WriteVarint((static_cast<uint64_t>(fieldNum) << 3) | (wireType & 0x07u));
    }
    void AddVarint(uint32_t fn, uint64_t v) { WriteTag(fn, 0); WriteVarint(v); }
    void AddZigZag(uint32_t fn, int64_t v) {
        uint64_t zz = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
        AddVarint(fn, zz);
    }
    void AddString(uint32_t fn, std::string_view s) {
        WriteTag(fn, 2);
        WriteVarint(s.size());
        AppendStr(buf, s);
    }
    void AddBytes(uint32_t fn, const void* p, size_t n) {
        WriteTag(fn, 2);
        WriteVarint(n);
        AppendBytes(buf, p, n);
    }
    void AddMsg(uint32_t fn, const ProtoWriter& sub) {
        AddBytes(fn, sub.buf.data(), sub.buf.size());
    }
};

// Forward-patching FlatBuffer builder: vtables precede tables, child objects follow parent uoffset slots
struct ForwardFbBuilder {
    std::vector<uint8_t> buf;

    explicit ForwardFbBuilder(const char* fileId4 = nullptr) {
        buf.resize(8, 0);
        if (fileId4) std::memcpy(buf.data() + 4, fileId4, 4);
    }

    void Align(size_t a) {
        while ((buf.size() % a) != 0) buf.push_back(0);
    }

    void PatchRoot(size_t rootTablePos) {
        PutU32LE(buf.data(), static_cast<uint32_t>(rootTablePos));
    }

    void PatchUOffset(size_t slotAddr, size_t targetPos) {
        PutU32LE(buf.data() + slotAddr, static_cast<uint32_t>(targetPos - slotAddr));
    }

    size_t EmitString(std::string_view s) {
        Align(4);
        size_t pos = buf.size();
        AppendU32LE(buf, static_cast<uint32_t>(s.size()));
        AppendStr(buf, s);
        AppendU8(buf, 0);
        return pos;
    }

    size_t EmitBytesVec(const uint8_t* p, size_t n) {
        Align(4);
        size_t pos = buf.size();
        AppendU32LE(buf, static_cast<uint32_t>(n));
        if (n > 0) AppendBytes(buf, p, n);
        return pos;
    }

    size_t EmitI32Vec(const std::vector<int32_t>& vals) {
        Align(4);
        size_t pos = buf.size();
        AppendU32LE(buf, static_cast<uint32_t>(vals.size()));
        for (int32_t v : vals) AppendI32LE(buf, v);
        return pos;
    }

    // Emits a vector of N placeholder uoffset_t slots; returns {vecPos, firstSlotAddr}
    std::pair<size_t, size_t> EmitOffsetVec(uint32_t count) {
        Align(4);
        size_t pos = buf.size();
        AppendU32LE(buf, count);
        size_t firstSlot = buf.size();
        for (uint32_t i = 0; i < count; ++i) AppendU32LE(buf, 0);
        return { pos, firstSlot };
    }

    // Emits a vtable + table with fixed 8-byte aligned slots for every present slot index in `presentSlots`
    struct BuiltTable {
        size_t tablePos = 0;
        std::vector<size_t> slotAddrs; // indexed by slot number
    };

    BuiltTable EmitTable(uint16_t numSlots, const std::vector<std::pair<uint16_t, uint8_t>>& slotSizes) {
        std::vector<uint16_t> relOffs(numSlots, 0);
        uint16_t curOff = 4; // after 4B vtable soffset_t
        for (const auto& kv : slotSizes) {
            uint16_t slot = kv.first;
            uint8_t sz = kv.second;
            uint16_t align = std::min<uint16_t>(sz, 8);
            if (align > 1) {
                while ((curOff % align) != 0) ++curOff;
            }
            relOffs[slot] = curOff;
            curOff = static_cast<uint16_t>(curOff + sz);
        }
        while ((curOff % 4) != 0) ++curOff;

        Align(4);
        size_t vtPos = buf.size();
        uint16_t vtSize = static_cast<uint16_t>(4 + numSlots * 2);
        AppendU16LE(buf, vtSize);
        AppendU16LE(buf, curOff);
        for (uint16_t r : relOffs) AppendU16LE(buf, r);

        Align(8);
        BuiltTable bt;
        bt.tablePos = buf.size();
        bt.slotAddrs.resize(numSlots, 0);
        buf.resize(bt.tablePos + curOff, 0);
        PutU32LE(buf.data() + bt.tablePos, static_cast<uint32_t>(bt.tablePos - vtPos));
        for (uint16_t s = 0; s < numSlots; ++s) {
            if (relOffs[s] != 0) bt.slotAddrs[s] = bt.tablePos + relOffs[s];
        }
        return bt;
    }
};

// ============================================================================
// Synthetic Binary Format Builders
// ============================================================================

// 1. Legacy Excel OLE2 BIFF8 Workbook (.xls) with SST CONTINUE boundary spanning
static std::vector<uint8_t> BuildBiff8Xls(
    std::string_view sheetNameAscii,
    std::string_view sstStr0,
    std::string_view sstSplitPartA,
    std::string_view sstSplitPartB,
    double numVal,
    int32_t rkIntVal,
    std::string_view formulaCachedStr,
    bool boolVal)
{
    std::vector<uint8_t> wb;
    auto emitRec = [&](uint16_t id, const std::vector<uint8_t>& pay) {
        AppendU16LE(wb, id);
        AppendU16LE(wb, static_cast<uint16_t>(pay.size()));
        AppendBytes(wb, pay.data(), pay.size());
    };

    // BOF (0x0809): BIFF8 Workbook Globals (ver=0x0600, type=0x0005)
    {
        std::vector<uint8_t> p(16, 0);
        PutU16LE(p.data(), 0x0600);
        PutU16LE(p.data() + 2, 0x0005);
        emitRec(0x0809, p);
    }

    // BOUNDSHEET (0x0085) — placeholder bofOffset patched once Globals EOF is emitted
    size_t boundSheetBofPatch = wb.size() + 4;
    {
        std::vector<uint8_t> p;
        AppendU32LE(p, 0); // bofOffset placeholder
        AppendU8(p, 0);    // visible
        AppendU8(p, 0);    // worksheet
        AppendU8(p, static_cast<uint8_t>(sheetNameAscii.size()));
        AppendU8(p, 0x00); // compressed Latin1
        AppendStr(p, sheetNameAscii);
        emitRec(0x0085, p);
    }

    // SST (0x00FC) + CONTINUE (0x003C): String #1 spans across the SST -> CONTINUE record boundary!
    {
        std::vector<uint8_t> sstPay;
        AppendU32LE(sstPay, 2); // cstTotal
        AppendU32LE(sstPay, 2); // cstUnique
        // String 0 (fits inside SST)
        AppendU16LE(sstPay, static_cast<uint16_t>(sstStr0.size()));
        AppendU8(sstPay, 0x00); // fHighByte = 0
        AppendStr(sstPay, sstStr0);
        // String 1 header + Part A (truncated right at end of SST record!)
        uint16_t totalSplitLen = static_cast<uint16_t>(sstSplitPartA.size() + sstSplitPartB.size());
        AppendU16LE(sstPay, totalSplitLen);
        AppendU8(sstPay, 0x00); // fHighByte = 0 in SST
        AppendStr(sstPay, sstSplitPartA);
        emitRec(0x00FC, sstPay);

        // CONTINUE (0x003C): byte 0 = new compression flag (0x01 = UTF-16LE!) for Part B!
        std::vector<uint8_t> contPay;
        AppendU8(contPay, 0x01); // switch to UTF-16LE across CONTINUE boundary to test flag transition!
        for (char c : sstSplitPartB) {
            AppendU16LE(contPay, static_cast<uint16_t>(static_cast<unsigned char>(c)));
        }
        emitRec(0x003C, contPay);
    }

    // Globals EOF (0x000A)
    emitRec(0x000A, {});

    // Patch BOUNDSHEET offset to point at Worksheet BOF
    uint32_t wsBofOffset = static_cast<uint32_t>(wb.size());
    PutU32LE(wb.data() + boundSheetBofPatch, wsBofOffset);

    // Worksheet BOF (0x0809, ver=0x0600, type=0x0010)
    {
        std::vector<uint8_t> p(16, 0);
        PutU16LE(p.data(), 0x0600);
        PutU16LE(p.data() + 2, 0x0010);
        emitRec(0x0809, p);
    }

    // Row 0, Col 0: LABELSST (0x00FD) -> SST[0]
    {
        std::vector<uint8_t> p;
        AppendU16LE(p, 0); AppendU16LE(p, 0); AppendU16LE(p, 0); AppendU32LE(p, 0);
        emitRec(0x00FD, p);
    }
    // Row 0, Col 1: LABELSST (0x00FD) -> SST[1] (CONTINUE-spanning string!)
    {
        std::vector<uint8_t> p;
        AppendU16LE(p, 0); AppendU16LE(p, 1); AppendU16LE(p, 0); AppendU32LE(p, 1);
        emitRec(0x00FD, p);
    }
    // Row 1, Col 0: NUMBER (0x0203)
    {
        std::vector<uint8_t> p;
        AppendU16LE(p, 1); AppendU16LE(p, 0); AppendU16LE(p, 0); AppendF64LE(p, numVal);
        emitRec(0x0203, p);
    }
    // Row 1, Col 1: RK (0x027E) -> 30-bit signed int (bit1=1, bit0=0)
    {
        std::vector<uint8_t> p;
        AppendU16LE(p, 1); AppendU16LE(p, 1); AppendU16LE(p, 0);
        uint32_t rk = (static_cast<uint32_t>(rkIntVal) << 2) | 0x02u;
        AppendU32LE(p, rk);
        emitRec(0x027E, p);
    }
    // Row 1, Col 2..3: MULRK (0x00BD) -> two cent-scaled ints (bit1=1, bit0=1 -> /100.0)
    {
        std::vector<uint8_t> p;
        AppendU16LE(p, 1); // row 1
        AppendU16LE(p, 2); // colFirst = 2
        AppendU16LE(p, 0); AppendU32LE(p, (static_cast<uint32_t>(12345) << 2) | 0x03u); // 123.45
        AppendU16LE(p, 0); AppendU32LE(p, (static_cast<uint32_t>(67800) << 2) | 0x03u); // 678
        AppendU16LE(p, 3); // colLast = 3
        emitRec(0x00BD, p);
    }
    // Row 2, Col 0: FORMULA (0x0006) with cached string result + STRING (0x0207)
    {
        std::vector<uint8_t> fp(20, 0);
        PutU16LE(fp.data(), 2);     // row 2
        PutU16LE(fp.data() + 2, 0); // col 0
        fp[6] = 0x00;               // string result marker
        PutU16LE(fp.data() + 12, 0xFFFFu);
        emitRec(0x0006, fp);

        std::vector<uint8_t> sp;
        AppendU16LE(sp, static_cast<uint16_t>(formulaCachedStr.size()));
        AppendU8(sp, 0x00);
        AppendStr(sp, formulaCachedStr);
        emitRec(0x0207, sp);
    }
    // Row 2, Col 1: BOOLERR (0x0205)
    {
        std::vector<uint8_t> bp = { 2, 0, 1, 0, 0, 0, static_cast<uint8_t>(boolVal ? 1 : 0), 0 };
        emitRec(0x0205, bp);
    }

    // Worksheet EOF
    emitRec(0x000A, {});
    return MakeOle2SingleStream(L"Workbook", wb);
}

// 2. SQLite 3 Multi-Page B-Tree Database (.sqlite & .gpkg)
static void AppendSqliteVarint(std::vector<uint8_t>& out, uint64_t v) {
    if (v <= 0x7FULL) {
        out.push_back(static_cast<uint8_t>(v));
        return;
    }
    uint8_t tmp[9] = {};
    int n = 0;
    uint64_t cur = v;
    while (cur > 0 && n < 8) {
        tmp[n++] = static_cast<uint8_t>(cur & 0x7Fu);
        cur >>= 7;
    }
    for (int i = n - 1; i >= 1; --i) out.push_back(static_cast<uint8_t>(tmp[i] | 0x80u));
    out.push_back(tmp[0]);
}

static std::vector<uint8_t> MakeSqliteRecord(const std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& cols) {
    std::vector<uint8_t> hdrBody;
    std::vector<uint8_t> payBody;
    for (const auto& c : cols) {
        AppendSqliteVarint(hdrBody, c.first);
        AppendBytes(payBody, c.second.data(), c.second.size());
    }
    // Total header size includes the varint length of hdrSize itself (1 byte for < 127)
    uint64_t hdrSize = 1 + hdrBody.size();
    std::vector<uint8_t> rec;
    AppendSqliteVarint(rec, hdrSize);
    AppendBytes(rec, hdrBody.data(), hdrBody.size());
    AppendBytes(rec, payBody.data(), payBody.size());
    return rec;
}

static void WriteSqliteLeafPageCells(uint8_t* pageBase, size_t hdrOff, const std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& cells) {
    size_t cellWritePos = 4096;
    std::vector<uint16_t> ptrs;
    for (const auto& kv : cells) {
        std::vector<uint8_t> cell;
        AppendSqliteVarint(cell, kv.second.size());
        AppendSqliteVarint(cell, kv.first); // rowid
        AppendBytes(cell, kv.second.data(), kv.second.size());
        cellWritePos -= cell.size();
        std::memcpy(pageBase + cellWritePos, cell.data(), cell.size());
        ptrs.push_back(static_cast<uint16_t>(cellWritePos));
    }
    pageBase[hdrOff + 0] = 0x0Du; // Table B-Tree Leaf Page
    PutU16BE(pageBase + hdrOff + 1, 0);
    PutU16BE(pageBase + hdrOff + 3, static_cast<uint16_t>(ptrs.size()));
    PutU16BE(pageBase + hdrOff + 5, static_cast<uint16_t>(cellWritePos));
    pageBase[hdrOff + 7] = 0;
    for (size_t i = 0; i < ptrs.size(); ++i) {
        PutU16BE(pageBase + hdrOff + 8 + i * 2, ptrs[i]);
    }
}

static std::vector<uint8_t> BuildSqliteBtreeDb(
    std::string_view tableName,
    std::string_view label0, double val0,
    std::string_view label1, double val1)
{
    std::vector<uint8_t> db(8192, 0); // 2 pages x 4096B
    std::memcpy(db.data(), "SQLite format 3\0", 16);
    PutU16BE(db.data() + 16, 4096); // pageSize
    db[18] = 1; db[19] = 1; db[20] = 0; db[21] = 64; db[22] = 32; db[23] = 32;
    PutU32BE(db.data() + 28, 2);       // pageCount = 2
    PutU32BE(db.data() + 40, 7);       // schema cookie
    PutU32BE(db.data() + 44, 4);       // schema format 4
    PutU32BE(db.data() + 56, 1);       // UTF-8 encoding
    PutU32BE(db.data() + 96, 3045000); // SQLite 3.45.0

    std::string ddl = "CREATE TABLE " + std::string(tableName) + " (id INTEGER PRIMARY KEY, label TEXT, reading REAL)";
    auto textCol = [](std::string_view s) -> std::pair<uint64_t, std::vector<uint8_t>> {
        return { 13ULL + 2ULL * s.size(), std::vector<uint8_t>(s.begin(), s.end()) };
    };
    auto realCol = [](double d) -> std::pair<uint64_t, std::vector<uint8_t>> {
        std::vector<uint8_t> b; AppendF64BE(b, d); return { 7ULL, std::move(b) };
    };

    // Page 1 (hdrOff = 100): sqlite_master row (type='table', name, tbl_name, rootpage=2, sql=ddl)
    std::vector<uint8_t> masterRec = MakeSqliteRecord({
        textCol("table"),
        textCol(tableName),
        textCol(tableName),
        { 1ULL, { 2 } }, // int8 rootpage = 2
        textCol(ddl)
    });
    WriteSqliteLeafPageCells(db.data(), 100, { { 1, masterRec } });

    // Page 2 (hdrOff = 0): user table rows (id is INTEGER PRIMARY KEY -> NULL serial type 0 aliases rowid!)
    std::vector<uint8_t> r0 = MakeSqliteRecord({ { 0ULL, {} }, textCol(label0), realCol(val0) });
    std::vector<uint8_t> r1 = MakeSqliteRecord({ { 0ULL, {} }, textCol(label1), realCol(val1) });
    WriteSqliteLeafPageCells(db.data() + 4096, 0, { { 101, r0 }, { 102, r1 } });
    return db;
}

static std::vector<uint8_t> BuildGeoPackageDb(
    std::string_view name0, double lat0, double lon0,
    std::string_view name1, double lat1, double lon1)
{
    std::vector<uint8_t> db(8192, 0);
    std::memcpy(db.data(), "SQLite format 3\0", 16);
    PutU16BE(db.data() + 16, 4096);
    db[18] = 1; db[19] = 1;
    PutU32BE(db.data() + 28, 2);
    PutU32BE(db.data() + 56, 1);
    std::memcpy(db.data() + 68, "GP10", 4); // GeoPackage 1.0 application_id

    auto textCol = [](std::string_view s) -> std::pair<uint64_t, std::vector<uint8_t>> {
        return { 13ULL + 2ULL * s.size(), std::vector<uint8_t>(s.begin(), s.end()) };
    };
    auto gpkgPointCol = [](double lon, double lat) -> std::pair<uint64_t, std::vector<uint8_t>> {
        std::vector<uint8_t> b;
        // GeoPackage Binary Header: 'G','P', version=0, flags=0x01 (little-endian, no envelope), srs_id=4326
        AppendU8(b, 'G'); AppendU8(b, 'P'); AppendU8(b, 0x00); AppendU8(b, 0x01);
        AppendI32LE(b, 4326);
        // Standard WKB Point (LE=1, wkbType=1, X=lon, Y=lat)
        AppendU8(b, 0x01);
        AppendU32LE(b, 1);
        AppendF64LE(b, lon);
        AppendF64LE(b, lat);
        return { 12ULL + 2ULL * b.size(), std::move(b) };
    };

    std::string ddl = "CREATE TABLE landmarks (fid INTEGER PRIMARY KEY, title TEXT, geom POINT)";
    std::vector<uint8_t> masterRec = MakeSqliteRecord({
        textCol("table"), textCol("landmarks"), textCol("landmarks"), { 1ULL, { 2 } }, textCol(ddl)
    });
    WriteSqliteLeafPageCells(db.data(), 100, { { 1, masterRec } });

    std::vector<uint8_t> r0 = MakeSqliteRecord({ { 0ULL, {} }, textCol(name0), gpkgPointCol(lon0, lat0) });
    std::vector<uint8_t> r1 = MakeSqliteRecord({ { 0ULL, {} }, textCol(name1), gpkgPointCol(lon1, lat1) });
    WriteSqliteLeafPageCells(db.data() + 4096, 0, { { 1, r0 }, { 2, r1 } });
    return db;
}

// 3. Apache Parquet (.parquet — Thrift Compact Footer + PLAIN Data Pages)
static std::vector<uint8_t> BuildParquetFile(
    int32_t id0, double temp0, std::string_view tag0,
    int32_t id1, double temp1, std::string_view tag1)
{
    std::vector<uint8_t> out;
    AppendStr(out, "PAR1");

    auto appendDataPage = [&](const std::vector<uint8_t>& plainBytes, int32_t nVals) -> std::pair<int64_t, int64_t> {
        int64_t pageOff = static_cast<int64_t>(out.size());
        ThriftCompactWriter ph;
        ph.WriteI32(1, 0); // DATA_PAGE
        ph.WriteI32(2, static_cast<int32_t>(plainBytes.size()));
        ph.WriteI32(3, static_cast<int32_t>(plainBytes.size()));
        ph.BeginStructField(5); // DataPageHeader
        ph.WriteI32(1, nVals);
        ph.WriteI32(2, 0); // PLAIN
        ph.WriteI32(3, 0);
        ph.WriteI32(4, 0);
        ph.EndStruct();
        ph.EndStruct();
        AppendBytes(out, ph.buf.data(), ph.buf.size());
        AppendBytes(out, plainBytes.data(), plainBytes.size());
        int64_t totalSz = static_cast<int64_t>(out.size()) - pageOff;
        return { pageOff, totalSz };
    };

    std::vector<uint8_t> col0; AppendI32LE(col0, id0); AppendI32LE(col0, id1);
    std::vector<uint8_t> col1; AppendF64LE(col1, temp0); AppendF64LE(col1, temp1);
    std::vector<uint8_t> col2;
    AppendU32LE(col2, static_cast<uint32_t>(tag0.size())); AppendStr(col2, tag0);
    AppendU32LE(col2, static_cast<uint32_t>(tag1.size())); AppendStr(col2, tag1);

    auto [off0, sz0] = appendDataPage(col0, 2);
    auto [off1, sz1] = appendDataPage(col1, 2);
    auto [off2, sz2] = appendDataPage(col2, 2);

    ThriftCompactWriter fm;
    fm.WriteI32(1, 2); // version = 2
    fm.BeginList(2, 12, 4); // schema: root + 3 REQUIRED leaf columns
    // Root
    fm.BeginListElemStruct();
    fm.WriteBinary(4, "telemetry_schema");
    fm.WriteI32(5, 3); // num_children = 3
    fm.EndStruct();
    // Leaf 0: sensor_id (INT32 = 1)
    fm.BeginListElemStruct();
    fm.WriteI32(1, 1); fm.WriteI32(3, 0); fm.WriteBinary(4, "sensor_id");
    fm.EndStruct();
    // Leaf 1: celsius (DOUBLE = 5)
    fm.BeginListElemStruct();
    fm.WriteI32(1, 5); fm.WriteI32(3, 0); fm.WriteBinary(4, "celsius");
    fm.EndStruct();
    // Leaf 2: unit_tag (BYTE_ARRAY = 6, UTF8 = 0)
    fm.BeginListElemStruct();
    fm.WriteI32(1, 6); fm.WriteI32(3, 0); fm.WriteBinary(4, "unit_tag"); fm.WriteI32(6, 0);
    fm.EndStruct();

    fm.WriteI64(3, 2); // num_rows = 2
    fm.BeginList(4, 12, 1); // 1 RowGroup
    fm.BeginListElemStruct();
    fm.BeginList(1, 12, 3); // 3 ColumnChunks

    auto writeChunk = [&](int32_t physType, std::string_view colName, int64_t off, int64_t sz) {
        fm.BeginListElemStruct();
        fm.WriteI64(2, off);
        fm.BeginStructField(3); // ColumnMetaData
        fm.WriteI32(1, physType);
        fm.BeginList(2, 5, 1); fm.WriteZigZag32(0); // PLAIN
        fm.BeginList(3, 8, 1); fm.WriteVarint(colName.size()); AppendStr(fm.buf, colName);
        fm.WriteI32(4, 0); // UNCOMPRESSED
        fm.WriteI64(5, 2); // num_values = 2
        fm.WriteI64(6, sz);
        fm.WriteI64(7, sz);
        fm.WriteI64(9, off);
        fm.EndStruct();
        fm.EndStruct();
    };
    writeChunk(1, "sensor_id", off0, sz0);
    writeChunk(5, "celsius", off1, sz1);
    writeChunk(6, "unit_tag", off2, sz2);

    fm.WriteI64(2, sz0 + sz1 + sz2);
    fm.WriteI64(3, 2);
    fm.EndStruct(); // End RowGroup
    fm.WriteBinary(6, "Explorer2Stage15ParquetWriter");
    fm.EndStruct(); // End FileMetaData

    AppendBytes(out, fm.buf.data(), fm.buf.size());
    AppendU32LE(out, static_cast<uint32_t>(fm.buf.size()));
    AppendStr(out, "PAR1");
    return out;
}

// 4. Apache Avro OCF (.avro — "null" and "deflate" codecs)
static void AppendAvroZigZag(std::vector<uint8_t>& b, int64_t v) {
    uint64_t zz = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
    while (zz >= 0x80u) {
        b.push_back(static_cast<uint8_t>((zz & 0x7Fu) | 0x80u));
        zz >>= 7;
    }
    b.push_back(static_cast<uint8_t>(zz));
}
static void AppendAvroBytes(std::vector<uint8_t>& b, std::string_view s) {
    AppendAvroZigZag(b, static_cast<int64_t>(s.size()));
    AppendStr(b, s);
}

static std::vector<uint8_t> BuildAvroOcfFile(
    bool useDeflate,
    int64_t seq0, std::string_view host0, double cpu0,
    int64_t seq1, std::string_view host1, double cpu1)
{
    std::vector<uint8_t> out = { 'O', 'b', 'j', 0x01 };
    std::string schemaJson =
        R"({"type":"record","name":"NodeMetric","fields":[{"name":"seq","type":"long"},{"name":"host","type":"string"},{"name":"cpu","type":"double"}]})";

    // Header metadata map with 2 entries: avro.schema & avro.codec
    AppendAvroZigZag(out, 2);
    AppendAvroBytes(out, "avro.schema");
    AppendAvroBytes(out, schemaJson);
    AppendAvroBytes(out, "avro.codec");
    AppendAvroBytes(out, useDeflate ? "deflate" : "null");
    AppendAvroZigZag(out, 0); // end of map

    uint8_t sync[16] = { 0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x17,0x28,0x39,0x4A,0x5B,0x6C,0x7D,0x8E,0x9F,0x10 };
    AppendBytes(out, sync, 16);

    std::vector<uint8_t> blockRaw;
    AppendAvroZigZag(blockRaw, seq0); AppendAvroBytes(blockRaw, host0); AppendF64LE(blockRaw, cpu0);
    AppendAvroZigZag(blockRaw, seq1); AppendAvroBytes(blockRaw, host1); AppendF64LE(blockRaw, cpu1);

    std::vector<uint8_t> blockPay = useDeflate ? MakeDeflateStored(blockRaw) : blockRaw;
    AppendAvroZigZag(out, 2); // 2 objects
    AppendAvroZigZag(out, static_cast<int64_t>(blockPay.size()));
    AppendBytes(out, blockPay.data(), blockPay.size());
    AppendBytes(out, sync, 16);
    return out;
}

// 5. MongoDB BSON (.bson — Nested Document, Array, ObjectId, UTC Datetime, Double, Int32)
static std::vector<uint8_t> BuildBsonDoc(std::string_view svcName, int32_t portVal, double latencyMs, uint8_t oidTail) {
    std::vector<uint8_t> body;
    // 0x07 ObjectId "_id"
    AppendU8(body, 0x07); AppendStr(body, "_id"); AppendU8(body, 0);
    uint8_t oid[12] = { 0x65,0xA0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99, oidTail };
    AppendBytes(body, oid, 12);

    // 0x02 UTF-8 "service"
    AppendU8(body, 0x02); AppendStr(body, "service"); AppendU8(body, 0);
    AppendI32LE(body, static_cast<int32_t>(svcName.size() + 1));
    AppendStr(body, svcName); AppendU8(body, 0);

    // 0x10 Int32 "port"
    AppendU8(body, 0x10); AppendStr(body, "port"); AppendU8(body, 0);
    AppendI32LE(body, portVal);

    // 0x09 UTC Datetime "created_at" (2025-01-15 00:00:00 UTC = 1736899200000 ms)
    AppendU8(body, 0x09); AppendStr(body, "created_at"); AppendU8(body, 0);
    AppendI64LE(body, 1736899200000LL);

    // 0x03 Nested Document "metrics" { "latency_ms": double }
    std::vector<uint8_t> sub;
    AppendU8(sub, 0x01); AppendStr(sub, "latency_ms"); AppendU8(sub, 0);
    AppendF64LE(sub, latencyMs);
    std::vector<uint8_t> subDoc;
    AppendI32LE(subDoc, static_cast<int32_t>(4 + sub.size() + 1));
    AppendBytes(subDoc, sub.data(), sub.size());
    AppendU8(subDoc, 0);

    AppendU8(body, 0x03); AppendStr(body, "metrics"); AppendU8(body, 0);
    AppendBytes(body, subDoc.data(), subDoc.size());

    std::vector<uint8_t> doc;
    AppendI32LE(doc, static_cast<int32_t>(4 + body.size() + 1));
    AppendBytes(doc, body.data(), body.size());
    AppendU8(doc, 0x00);
    return doc;
}

// 6. NumPy .npy (v1.0 2D float64 matrix)
static std::vector<uint8_t> BuildNpy2D(double a00, double a01, double a10, double a11) {
    std::string hdr = "{'descr': '<f8', 'fortran_order': False, 'shape': (2, 2), }";
    while (((10 + hdr.size() + 1) % 64) != 0) hdr.push_back(' ');
    hdr.push_back('\n');

    std::vector<uint8_t> out = { 0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0 };
    AppendU16LE(out, static_cast<uint16_t>(hdr.size()));
    AppendStr(out, hdr);
    AppendF64LE(out, a00); AppendF64LE(out, a01);
    AppendF64LE(out, a10); AppendF64LE(out, a11);
    return out;
}

// 7. RFC 8949 CBOR Array of 2 Maps
static std::vector<uint8_t> BuildCborArray(std::string_view dev0, uint16_t rpm0, std::string_view dev1, uint16_t rpm1) {
    std::vector<uint8_t> b;
    auto putText = [&](std::string_view s) {
        AppendU8(b, static_cast<uint8_t>(0x60u | s.size())); // major 3, len < 24
        AppendStr(b, s);
    };
    AppendU8(b, 0x82u); // array(2)
    for (int i = 0; i < 2; ++i) {
        std::string_view dv = (i == 0) ? dev0 : dev1;
        uint16_t rv = (i == 0) ? rpm0 : rpm1;
        AppendU8(b, 0xA2u); // map(2)
        putText("device");
        putText(dv);
        putText("rpm");
        AppendU8(b, 0x19u); // uint16 BE
        AppendU16BE(b, rv);
    }
    return b;
}

// 8. MessagePack Array of 2 Maps
static std::vector<uint8_t> BuildMsgPackArray(std::string_view ch0, double gain0, std::string_view ch1, double gain1) {
    std::vector<uint8_t> b;
    auto putStr = [&](std::string_view s) {
        AppendU8(b, static_cast<uint8_t>(0xA0u | s.size())); // fixstr
        AppendStr(b, s);
    };
    AppendU8(b, 0x92u); // fixarray(2)
    for (int i = 0; i < 2; ++i) {
        AppendU8(b, 0x82u); // fixmap(2)
        putStr("channel");
        putStr(i == 0 ? ch0 : ch1);
        putStr("gain_db");
        AppendU8(b, 0xCBu); // float64 BE
        AppendF64BE(b, i == 0 ? gain0 : gain1);
    }
    return b;
}

// 9. ONNX Protobuf Model (.onnx)
static std::vector<uint8_t> BuildOnnxModel(std::string_view graphName, std::string_view opType, float w0, float w1) {
    ProtoWriter node;
    node.AddString(1, "input_x");
    node.AddString(1, "weight_w");
    node.AddString(2, "output_y");
    node.AddString(3, "layer0_op");
    node.AddString(4, opType);

    ProtoWriter initTensor;
    initTensor.AddVarint(1, 2); // dims = [2]
    initTensor.AddVarint(2, 1); // FLOAT32
    initTensor.AddString(8, "weight_w");
    std::vector<uint8_t> rawFloats;
    AppendF32LE(rawFloats, w0);
    AppendF32LE(rawFloats, w1);
    initTensor.AddBytes(9, rawFloats.data(), rawFloats.size());

    ProtoWriter dim0; dim0.AddVarint(1, 1);
    ProtoWriter dim1; dim1.AddVarint(1, 2);
    ProtoWriter shape; shape.AddMsg(1, dim0); shape.AddMsg(1, dim1);
    ProtoWriter ttype; ttype.AddVarint(1, 1); ttype.AddMsg(2, shape);
    ProtoWriter typeProto; typeProto.AddMsg(1, ttype);
    ProtoWriter inVi; inVi.AddString(1, "input_x"); inVi.AddMsg(2, typeProto);

    ProtoWriter graph;
    graph.AddMsg(1, node);
    graph.AddString(2, graphName);
    graph.AddMsg(5, initTensor);
    graph.AddMsg(11, inVi);

    ProtoWriter opset;
    opset.AddString(1, "ai.onnx");
    opset.AddVarint(2, 18);

    ProtoWriter model;
    model.AddVarint(1, 9); // ir_version = 9
    model.AddString(2, "Explorer2TestSuite");
    model.AddString(3, "2.4");
    model.AddMsg(7, graph);
    model.AddMsg(8, opset);
    return model.buf;
}

// 10. PyTorch ZIP + Pickle VM Checkpoint (.pt)
static std::vector<uint8_t> BuildPyTorchCheckpoint(std::string_view paramName, float w0, float w1, float w2, float w3) {
    // Pickle Protocol 4 building state_dict = { paramName: _rebuild_tensor_v2(BINPERSID, 0, (2, 2), (2, 1), False, ()) }
    std::vector<uint8_t> pkl;
    auto putShortStr = [&](std::string_view s) {
        AppendU8(pkl, 0x8Cu);
        AppendU8(pkl, static_cast<uint8_t>(s.size()));
        AppendStr(pkl, s);
    };

    AppendU8(pkl, 0x80u); AppendU8(pkl, 4); // PROTO 4
    AppendU8(pkl, '}');                     // EMPTY_DICT
    putShortStr(paramName);                 // key

    // GLOBAL torch._utils._rebuild_tensor_v2
    AppendU8(pkl, 'c');
    AppendStr(pkl, "torch._utils\n_rebuild_tensor_v2\n");

    // Args tuple: (BINPERSID(("storage", FloatStorage, "0", "cpu", 4)), 0, (2, 2), (2, 1), False, ())
    AppendU8(pkl, '('); // MARK for args tuple
    AppendU8(pkl, '('); // MARK for persistent_id tuple
    putShortStr("storage");
    AppendU8(pkl, 'c'); AppendStr(pkl, "torch\nFloatStorage\n");
    putShortStr("0");
    putShortStr("cpu");
    AppendU8(pkl, 'K'); AppendU8(pkl, 4);
    AppendU8(pkl, 't'); // TUPLE -> persistent_id
    AppendU8(pkl, 'Q'); // BINPERSID -> Storage

    AppendU8(pkl, 'K'); AppendU8(pkl, 0); // storage_offset = 0
    AppendU8(pkl, 'K'); AppendU8(pkl, 2); AppendU8(pkl, 'K'); AppendU8(pkl, 2); AppendU8(pkl, 0x86u); // shape (2, 2)
    AppendU8(pkl, 'K'); AppendU8(pkl, 2); AppendU8(pkl, 'K'); AppendU8(pkl, 1); AppendU8(pkl, 0x86u); // stride (2, 1)
    AppendU8(pkl, 0x89u); // NEWFALSE
    AppendU8(pkl, ')');   // EMPTY_TUPLE
    AppendU8(pkl, 't');   // TUPLE -> args
    AppendU8(pkl, 'R');   // REDUCE -> Tensor
    AppendU8(pkl, 's');   // SETITEM -> records into state_dict!
    AppendU8(pkl, '.');   // STOP

    std::vector<uint8_t> weights;
    AppendF32LE(weights, w0); AppendF32LE(weights, w1);
    AppendF32LE(weights, w2); AppendF32LE(weights, w3);

    return MakeZipStoreArchive({
        { "archive/version", { '3', '\n' } },
        { "archive/data.pkl", std::move(pkl) },
        { "archive/data/0", std::move(weights) }
    });
}

// 11. TensorFlow Lite FlatBuffer Model (.tflite)
static std::vector<uint8_t> BuildTfLiteModel(std::string_view tensorName, int32_t builtinOpCode, float firstWeight) {
    ForwardFbBuilder fb("TFL3");

    // Root Model table: slots 0(u32), 1(off), 2(off), 3(off), 4(off)
    auto model = fb.EmitTable(5, { {0, 4}, {1, 4}, {2, 4}, {3, 4}, {4, 4} });
    fb.PatchRoot(model.tablePos);
    PutU32LE(fb.buf.data() + model.slotAddrs[0], 3);
    fb.PatchUOffset(model.slotAddrs[3], fb.EmitString("Stage15_TFLite"));

    // OperatorCodes vector (1 element)
    auto [ocVec, ocSlot0] = fb.EmitOffsetVec(1);
    fb.PatchUOffset(model.slotAddrs[1], ocVec);
    auto oc0 = fb.EmitTable(4, { {0, 1}, {3, 4} });
    fb.PatchUOffset(ocSlot0, oc0.tablePos);
    fb.buf[oc0.slotAddrs[0]] = static_cast<uint8_t>(builtinOpCode);
    PutU32LE(fb.buf.data() + oc0.slotAddrs[3], static_cast<uint32_t>(builtinOpCode));

    // Buffers vector (2 elements: buffer 0 empty, buffer 1 has 2 float32 weights)
    auto [bufVec, bufSlot0] = fb.EmitOffsetVec(2);
    fb.PatchUOffset(model.slotAddrs[4], bufVec);
    auto b0 = fb.EmitTable(1, {});
    fb.PatchUOffset(bufSlot0, b0.tablePos);
    auto b1 = fb.EmitTable(1, { {0, 4} });
    fb.PatchUOffset(bufSlot0 + 4, b1.tablePos);
    std::vector<uint8_t> wbytes;
    AppendF32LE(wbytes, firstWeight);
    AppendF32LE(wbytes, firstWeight + 1.0f);
    fb.PatchUOffset(b1.slotAddrs[0], fb.EmitBytesVec(wbytes.data(), wbytes.size()));

    // SubGraphs vector (1 element)
    auto [sgVec, sgSlot0] = fb.EmitOffsetVec(1);
    fb.PatchUOffset(model.slotAddrs[2], sgVec);
    auto sg = fb.EmitTable(5, { {0, 4}, {3, 4}, {4, 4} });
    fb.PatchUOffset(sgSlot0, sg.tablePos);
    fb.PatchUOffset(sg.slotAddrs[4], fb.EmitString("main"));

    // Tensor 0 inside SubGraph
    auto [tVec, tSlot0] = fb.EmitOffsetVec(1);
    fb.PatchUOffset(sg.slotAddrs[0], tVec);
    auto t0 = fb.EmitTable(4, { {0, 4}, {1, 1}, {2, 4}, {3, 4} });
    fb.PatchUOffset(tSlot0, t0.tablePos);
    fb.PatchUOffset(t0.slotAddrs[0], fb.EmitI32Vec({ 1, 2 }));
    fb.buf[t0.slotAddrs[1]] = 0; // FLOAT32
    PutU32LE(fb.buf.data() + t0.slotAddrs[2], 1); // buffer #1
    fb.PatchUOffset(t0.slotAddrs[3], fb.EmitString(tensorName));

    // Operator 0 inside SubGraph
    auto [opVec, opSlot0] = fb.EmitOffsetVec(1);
    fb.PatchUOffset(sg.slotAddrs[3], opVec);
    auto op0 = fb.EmitTable(3, { {0, 4}, {1, 4}, {2, 4} });
    fb.PatchUOffset(opSlot0, op0.tablePos);
    PutU32LE(fb.buf.data() + op0.slotAddrs[0], 0);
    fb.PatchUOffset(op0.slotAddrs[1], fb.EmitI32Vec({ 0 }));
    fb.PatchUOffset(op0.slotAddrs[2], fb.EmitI32Vec({ 0 }));
    return fb.buf;
}

// 12. Apache Arrow IPC Random-Access File (.arrow — "ARROW1" + RecordBatch + Footer)
static std::vector<uint8_t> BuildArrowIpcFile(std::string_view colName, int32_t i0, int32_t i1, double d0, double d1) {
    // Build RecordBatch Message FlatBuffer
    ForwardFbBuilder msgFb;
    auto msg = msgFb.EmitTable(4, { {0, 2}, {1, 1}, {2, 4}, {3, 8} });
    msgFb.PatchRoot(msg.tablePos);
    PutU16LE(msgFb.buf.data() + msg.slotAddrs[0], 4);
    msgFb.buf[msg.slotAddrs[1]] = 3; // RecordBatch
    PutU64LE(msgFb.buf.data() + msg.slotAddrs[3], 32); // bodyLength = 32

    auto rb = msgFb.EmitTable(3, { {0, 8}, {1, 4}, {2, 4} });
    msgFb.PatchUOffset(msg.slotAddrs[2], rb.tablePos);
    PutU64LE(msgFb.buf.data() + rb.slotAddrs[0], 2); // 2 rows

    // 2 FieldNodes (16B each)
    msgFb.Align(8);
    size_t fnVec = msgFb.buf.size();
    AppendU32LE(msgFb.buf, 2);
    for (int k = 0; k < 2; ++k) { AppendI64LE(msgFb.buf, 2); AppendI64LE(msgFb.buf, 0); }
    msgFb.PatchUOffset(rb.slotAddrs[1], fnVec);

    // 4 Buffers (16B each: validity0, data0 [8B], validity1, data1 [16B])
    msgFb.Align(8);
    size_t bfVec = msgFb.buf.size();
    AppendU32LE(msgFb.buf, 4);
    AppendI64LE(msgFb.buf, 0);  AppendI64LE(msgFb.buf, 0);
    AppendI64LE(msgFb.buf, 0);  AppendI64LE(msgFb.buf, 8);
    AppendI64LE(msgFb.buf, 16); AppendI64LE(msgFb.buf, 0);
    AppendI64LE(msgFb.buf, 16); AppendI64LE(msgFb.buf, 16);
    msgFb.PatchUOffset(rb.slotAddrs[2], bfVec);
    msgFb.Align(8);

    std::vector<uint8_t> file = { 'A', 'R', 'R', 'O', 'W', '1', 0, 0 };
    int64_t blkOff = static_cast<int64_t>(file.size());
    AppendU32LE(file, 0xFFFFFFFFu);
    AppendU32LE(file, static_cast<uint32_t>(msgFb.buf.size()));
    AppendBytes(file, msgFb.buf.data(), msgFb.buf.size());
    int32_t metaLen = static_cast<int32_t>(8 + msgFb.buf.size());

    // 32-byte RecordBatch body: [0..7] = 2 x int32, [8..15] = pad, [16..31] = 2 x float64
    AppendI32LE(file, i0); AppendI32LE(file, i1);
    AppendU64LE(file, 0);
    AppendF64LE(file, d0); AppendF64LE(file, d1);

    // Footer FlatBuffer
    ForwardFbBuilder footFb;
    auto foot = footFb.EmitTable(4, { {0, 2}, {1, 4}, {3, 4} });
    footFb.PatchRoot(foot.tablePos);
    PutU16LE(footFb.buf.data() + foot.slotAddrs[0], 4);

    auto schema = footFb.EmitTable(2, { {1, 4} });
    footFb.PatchUOffset(foot.slotAddrs[1], schema.tablePos);
    auto [fVec, fSlot0] = footFb.EmitOffsetVec(2);
    footFb.PatchUOffset(schema.slotAddrs[1], fVec);

    // Field 0: Int32
    auto fld0 = footFb.EmitTable(4, { {0, 4}, {1, 1}, {2, 1}, {3, 4} });
    footFb.PatchUOffset(fSlot0, fld0.tablePos);
    footFb.PatchUOffset(fld0.slotAddrs[0], footFb.EmitString(colName));
    footFb.buf[fld0.slotAddrs[1]] = 0;
    footFb.buf[fld0.slotAddrs[2]] = 2; // Int
    auto intTbl = footFb.EmitTable(2, { {0, 4}, {1, 1} });
    footFb.PatchUOffset(fld0.slotAddrs[3], intTbl.tablePos);
    PutU32LE(footFb.buf.data() + intTbl.slotAddrs[0], 32);
    footFb.buf[intTbl.slotAddrs[1]] = 1;

    // Field 1: Float64
    auto fld1 = footFb.EmitTable(4, { {0, 4}, {1, 1}, {2, 1}, {3, 4} });
    footFb.PatchUOffset(fSlot0 + 4, fld1.tablePos);
    footFb.PatchUOffset(fld1.slotAddrs[0], footFb.EmitString("score_f64"));
    footFb.buf[fld1.slotAddrs[1]] = 0;
    footFb.buf[fld1.slotAddrs[2]] = 3; // FloatingPoint
    auto fpTbl = footFb.EmitTable(1, { {0, 2} });
    footFb.PatchUOffset(fld1.slotAddrs[3], fpTbl.tablePos);
    PutU16LE(footFb.buf.data() + fpTbl.slotAddrs[0], 2); // DOUBLE

    // recordBatches Block struct vector (24B: int64 offset, int32 metaLen, int32 pad, int64 bodyLen)
    footFb.Align(8);
    size_t rbBlkVec = footFb.buf.size();
    AppendU32LE(footFb.buf, 1);
    AppendI64LE(footFb.buf, blkOff);
    AppendI32LE(footFb.buf, metaLen);
    AppendI32LE(footFb.buf, 0);
    AppendI64LE(footFb.buf, 32);
    footFb.PatchUOffset(foot.slotAddrs[3], rbBlkVec);

    AppendBytes(file, footFb.buf.data(), footFb.buf.size());
    AppendU32LE(file, static_cast<uint32_t>(footFb.buf.size()));
    AppendStr(file, "ARROW1");
    return file;
}

// 13. Apache ORC (.orc — Protobuf Footer + PostScript)
static std::vector<uint8_t> BuildOrcFile(std::string_view colName, uint64_t rowCount, int64_t minInt, int64_t maxInt) {
    std::vector<uint8_t> out = { 'O', 'R', 'C' };

    ProtoWriter stripe;
    stripe.AddVarint(1, 3);  // offset
    stripe.AddVarint(2, 16); // indexLen
    stripe.AddVarint(3, 64); // dataLen
    stripe.AddVarint(4, 24); // footerLen
    stripe.AddVarint(5, rowCount);

    ProtoWriter tRoot;
    tRoot.AddVarint(1, 12); // STRUCT
    tRoot.AddVarint(2, 1);
    tRoot.AddString(3, colName);

    ProtoWriter tCol1;
    tCol1.AddVarint(1, 4); // LONG

    ProtoWriter stRoot; stRoot.AddVarint(1, rowCount);
    ProtoWriter intStat; intStat.AddZigZag(1, minInt); intStat.AddZigZag(2, maxInt);
    ProtoWriter stCol1; stCol1.AddVarint(1, rowCount); stCol1.AddMsg(2, intStat);

    ProtoWriter footer;
    footer.AddMsg(3, stripe);
    footer.AddMsg(4, tRoot);
    footer.AddMsg(4, tCol1);
    footer.AddVarint(6, rowCount);
    footer.AddMsg(7, stRoot);
    footer.AddMsg(7, stCol1);

    ProtoWriter ps;
    ps.AddVarint(1, footer.buf.size());
    ps.AddVarint(2, 0); // NONE
    ps.AddVarint(3, 65536);
    ps.AddVarint(4, 0); ps.AddVarint(4, 12);
    ps.AddString(8000, "ORC");

    AppendBytes(out, footer.buf.data(), footer.buf.size());
    AppendBytes(out, ps.buf.data(), ps.buf.size());
    AppendU8(out, static_cast<uint8_t>(ps.buf.size()));
    return out;
}

// 14. HDF5 v2 (.h5 — Superblock v2 + OHDR v2 Root Group + Link + Contiguous Float64 Dataset)
static std::vector<uint8_t> BuildHdf5File(std::string_view dsName, double v0, double v1, double v2, double v3) {
    std::vector<uint8_t> h5(512, 0);
    static const uint8_t kSig[8] = { 0x89, 'H', 'D', 'F', '\r', '\n', 0x1A, '\n' };
    std::memcpy(h5.data(), kSig, 8);
    h5[8] = 2; // Superblock v2
    h5[9] = 8; // offSize = 8
    h5[10] = 8; // lenSize = 8
    PutU64LE(h5.data() + 12, 0);          // baseAddr
    PutU64LE(h5.data() + 20, UINT64_MAX); // extAddr
    PutU64LE(h5.data() + 28, 512);        // eofAddr
    PutU64LE(h5.data() + 36, 64);         // rootObjAddr = 64

    // Root Group OHDR v2 at offset 64: Link message -> Dataset at offset 160
    std::vector<uint8_t> linkMsg;
    AppendU8(linkMsg, 1); // version 1
    AppendU8(linkMsg, 0); // flags = 0 (1B name len, hard link)
    AppendU8(linkMsg, static_cast<uint8_t>(dsName.size()));
    AppendStr(linkMsg, dsName);
    AppendU64LE(linkMsg, 160); // target OHDR at 160

    uint8_t* rg = h5.data() + 64;
    std::memcpy(rg, "OHDR", 4);
    rg[4] = 2; rg[5] = 0;
    rg[6] = static_cast<uint8_t>(4 + linkMsg.size());
    rg[7] = 0x06; // Link message
    PutU16LE(rg + 8, static_cast<uint16_t>(linkMsg.size()));
    rg[10] = 0;
    std::memcpy(rg + 11, linkMsg.data(), linkMsg.size());

    // Dataset OHDR v2 at offset 160: Dataspace [2,2], Datatype float64 LE, Contiguous Layout @ 320
    std::vector<uint8_t> chunk;
    // Msg 1: Dataspace (0x01), ver=2, rank=2, dims=[2, 2]
    AppendU8(chunk, 0x01); AppendU16LE(chunk, 20); AppendU8(chunk, 0);
    AppendU8(chunk, 2); AppendU8(chunk, 2); AppendU8(chunk, 0); AppendU8(chunk, 1);
    AppendU64LE(chunk, 2); AppendU64LE(chunk, 2);
    // Msg 2: Datatype (0x03), float v1 (0x11), LE, size=8
    AppendU8(chunk, 0x03); AppendU16LE(chunk, 20); AppendU8(chunk, 0);
    AppendU8(chunk, 0x11); AppendU8(chunk, 0x20); AppendU8(chunk, 0); AppendU8(chunk, 0);
    AppendU32LE(chunk, 8);
    for (int i = 0; i < 12; ++i) AppendU8(chunk, 0);
    // Msg 3: Data Layout (0x08), ver=3, class=1 (Contiguous), addr=320, size=32
    AppendU8(chunk, 0x08); AppendU16LE(chunk, 18); AppendU8(chunk, 0);
    AppendU8(chunk, 3); AppendU8(chunk, 1);
    AppendU64LE(chunk, 320); AppendU64LE(chunk, 32);

    uint8_t* ds = h5.data() + 160;
    std::memcpy(ds, "OHDR", 4);
    ds[4] = 2; ds[5] = 0;
    ds[6] = static_cast<uint8_t>(chunk.size());
    std::memcpy(ds + 7, chunk.data(), chunk.size());

    PutF64LE(h5.data() + 320, v0);
    PutF64LE(h5.data() + 328, v1);
    PutF64LE(h5.data() + 336, v2);
    PutF64LE(h5.data() + 344, v3);
    return h5;
}

// 15. MATLAB Level 5 MAT-File (.mat — Column-Major mxDOUBLE_CLASS 2x2 Matrix)
static std::vector<uint8_t> BuildMatlabMat5(std::string_view varName, double r0c0, double r1c0, double r0c1, double r1c1) {
    std::vector<uint8_t> mat(128, ' ');
    std::string desc = "MATLAB 5.0 MAT-file, Explorer2 Stage 15";
    std::memcpy(mat.data(), desc.data(), desc.size());
    PutU16LE(mat.data() + 124, 0x0100);
    mat[126] = 'I'; mat[127] = 'M';

    std::vector<uint8_t> body;
    // 1. Array Flags (miUINT32 = 6, 8 bytes): mxDOUBLE_CLASS = 6
    AppendU32LE(body, 6); AppendU32LE(body, 8);
    AppendU32LE(body, 6); AppendU32LE(body, 0);
    // 2. Dimensions (miINT32 = 5, 8 bytes): [2, 2]
    AppendU32LE(body, 5); AppendU32LE(body, 8);
    AppendI32LE(body, 2); AppendI32LE(body, 2);
    // 3. Array Name (miINT8 = 1, 8-byte padded)
    AppendU32LE(body, 1); AppendU32LE(body, static_cast<uint32_t>(varName.size()));
    AppendStr(body, varName);
    while ((body.size() % 8) != 0) AppendU8(body, 0);
    // 4. Real Part (miDOUBLE = 9, 32 bytes, column-major: (1,1), (2,1), (1,2), (2,2))
    AppendU32LE(body, 9); AppendU32LE(body, 32);
    AppendF64LE(body, r0c0); AppendF64LE(body, r1c0);
    AppendF64LE(body, r0c1); AppendF64LE(body, r1c1);

    AppendU32LE(mat, 14); // miMATRIX
    AppendU32LE(mat, static_cast<uint32_t>(body.size()));
    AppendBytes(mat, body.data(), body.size());
    return mat;
}

// 16. SPSS System File (.sav — $FL2 + Variable Records + Bytecode-Compressed Cases)
static std::vector<uint8_t> BuildSpssSav(int32_t score0, std::string_view city0, int32_t score1, std::string_view city1) {
    std::vector<uint8_t> sav(176, ' ');
    std::memcpy(sav.data(), "$FL2@(#) SPSS DATA FILE", 23);
    PutU32LE(sav.data() + 60, 2);  // layout_code = 2 (LE)
    PutU32LE(sav.data() + 64, 2);  // nominal_case_size = 2 slots
    PutU32LE(sav.data() + 68, 1);  // compressed = 1 (bytecode)
    PutU32LE(sav.data() + 72, 0);
    PutU32LE(sav.data() + 76, 2);  // ncases = 2
    PutF64LE(sav.data() + 80, 100.0); // bias = 100.0
    std::memcpy(sav.data() + 88, "15 NOV 2612:00:00Stage15 Survey", 31);

    auto emitVar = [&](int32_t width, const char name8[8]) {
        AppendI32LE(sav, 2);     // recType = 2
        AppendI32LE(sav, width); // 0 = numeric, 8 = A8 string
        AppendI32LE(sav, 0);     // hasLabel = 0
        AppendI32LE(sav, 0);     // nMissing = 0
        AppendI32LE(sav, 0x050800);
        AppendI32LE(sav, 0x050800);
        AppendBytes(sav, name8, 8);
    };
    emitVar(0, "SCORE   ");
    emitVar(8, "CITY    ");

    AppendI32LE(sav, 999);
    AppendI32LE(sav, 0);

    // Bytecode cluster of 8 opcodes: [score0+100, 253(literal city0), score1+100, 253(literal city1), 252(EOF), 0, 0, 0]
    uint8_t cluster[8] = {
        static_cast<uint8_t>(score0 + 100), 253,
        static_cast<uint8_t>(score1 + 100), 253,
        252, 0, 0, 0
    };
    AppendBytes(sav, cluster, 8);
    auto pad8 = [](std::string_view s) {
        std::array<uint8_t, 8> a = { ' ',' ',' ',' ',' ',' ',' ',' ' };
        std::memcpy(a.data(), s.data(), std::min<size_t>(s.size(), 8));
        return a;
    };
    auto c0 = pad8(city0); AppendBytes(sav, c0.data(), 8);
    auto c1 = pad8(city1); AppendBytes(sav, c1.data(), 8);
    return sav;
}

// 17. Stata Dataset (.dta — Modern Release 118 <stata_dta>)
static std::vector<uint8_t> BuildStataDta118(double gdp0, std::string_view iso0, double gdp1, std::string_view iso1) {
    std::vector<uint8_t> b;
    AppendStr(b, "<stata_dta><header><release>118</release><byteorder>LSF</byteorder><K>");
    AppendU16LE(b, 2);
    AppendStr(b, "</K><N>");
    AppendU64LE(b, 2);
    AppendStr(b, "</N><label>");
    AppendU16LE(b, 5); AppendStr(b, "Macro");
    AppendStr(b, "</label></header><variable_types>");
    AppendU16LE(b, 65526); // double
    AppendU16LE(b, 8);     // str8
    AppendStr(b, "</variable_types><varnames>");
    std::vector<uint8_t> vn(258, 0);
    std::memcpy(vn.data(), "gdp_usd", 7);
    std::memcpy(vn.data() + 129, "country", 7);
    AppendBytes(b, vn.data(), vn.size());
    AppendStr(b, "</varnames><varlabels>");
    std::vector<uint8_t> vl(642, 0);
    AppendBytes(b, vl.data(), vl.size());
    AppendStr(b, "</varlabels><data>");

    auto writeStr8 = [&](std::string_view s) {
        uint8_t s8[8] = {};
        std::memcpy(s8, s.data(), std::min<size_t>(s.size(), 8));
        AppendBytes(b, s8, 8);
    };
    AppendF64LE(b, gdp0); writeStr8(iso0);
    AppendF64LE(b, gdp1); writeStr8(iso1);
    AppendStr(b, "</data></stata_dta>");
    return b;
}

// 18. SAS7BDAT Binary Dataset (.sas7bdat — 32-bit LE MIX Page)
static std::vector<uint8_t> BuildSas7bdat(std::string_view dsName, double dose0, std::string_view arm0, double dose1, std::string_view arm1) {
    std::vector<uint8_t> sas(2048, 0); // 1024B header + 1024B Page 0
    static const uint8_t kMagic[32] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0xC2,0xEA,0x81,0x60,
        0xB3,0x14,0x11,0xCF,0xBD,0x92,0x08,0x00, 0x09,0xC7,0x31,0x8C,0x18,0x1F,0x10,0x11
    };
    std::memcpy(sas.data(), kMagic, 32);
    sas[32] = 0x22; sas[35] = 0x22; sas[37] = 0x01; // 32-bit, little-endian
    std::memcpy(sas.data() + 92, dsName.data(), std::min<size_t>(dsName.size(), 32));
    PutU32LE(sas.data() + 196, 1024); // headerLen
    PutU32LE(sas.data() + 200, 1024); // pageSize
    PutU32LE(sas.data() + 204, 1);    // pageCount

    uint8_t* pg = sas.data() + 1024;
    PutU16LE(pg + 16, 512); // MIX page
    PutU16LE(pg + 18, 7);   // blockCount
    PutU16LE(pg + 20, 5);   // subCount = 5

    // Place 5 subheaders starting at page offset 512 (rows live at page offset 88)
    size_t curSh = 512;
    auto addSub = [&](size_t idx, const std::vector<uint8_t>& sh) {
        std::memcpy(pg + curSh, sh.data(), sh.size());
        uint8_t* ptr = pg + 24 + idx * 12;
        PutU32LE(ptr, static_cast<uint32_t>(curSh));
        PutU32LE(ptr + 4, static_cast<uint32_t>(sh.size()));
        curSh += (sh.size() + 7u) & ~size_t(7u);
    };

    // Sub 0: RowSize (0xF7F7F7F7, 64B)
    std::vector<uint8_t> shRow(64, 0);
    PutU32LE(shRow.data(), 0xF7F7F7F7u);
    PutU32LE(shRow.data() + 20, 16); // rowLength = 16
    PutU32LE(shRow.data() + 24, 2);  // totalRows = 2
    PutU32LE(shRow.data() + 60, 2);  // mixRowCount = 2
    addSub(0, shRow);

    // Sub 1: ColumnSize (0xF6F6F6F6, 8B)
    std::vector<uint8_t> shColSz(8, 0);
    PutU32LE(shColSz.data(), 0xF6F6F6F6u);
    PutU32LE(shColSz.data() + 4, 2);
    addSub(1, shColSz);

    // Sub 2: ColumnText (0xFFFFFFFD): "\0\0DOSE_MGCOHORT"
    std::vector<uint8_t> shTxt;
    AppendU32LE(shTxt, 0xFFFFFFFDu);
    AppendU16LE(shTxt, 16);
    AppendStr(shTxt, "DOSE_MGCOHORT ");
    addSub(2, shTxt);

    // Sub 3: ColumnName (0xFFFFFFFF)
    std::vector<uint8_t> shNm(28, 0);
    PutU32LE(shNm.data(), 0xFFFFFFFFu);
    PutU16LE(shNm.data() + 12, 0); PutU16LE(shNm.data() + 14, 2); PutU16LE(shNm.data() + 16, 7); // "DOSE_MG"
    PutU16LE(shNm.data() + 20, 0); PutU16LE(shNm.data() + 22, 9); PutU16LE(shNm.data() + 24, 6); // "COHORT"
    addSub(3, shNm);

    // Sub 4: ColumnAttrs (0xFFFFFFFC)
    std::vector<uint8_t> shAt(36, 0);
    PutU32LE(shAt.data(), 0xFFFFFFFCu);
    PutU32LE(shAt.data() + 12, 0); PutU32LE(shAt.data() + 16, 8); shAt[22] = 1; // Numeric 8B @ 0
    PutU32LE(shAt.data() + 24, 8); PutU32LE(shAt.data() + 28, 8); shAt[34] = 2; // Char 8B @ 8
    addSub(4, shAt);

    // Uncompressed rows at offset 88
    auto writeRow = [&](size_t off, double d, std::string_view s) {
        PutF64LE(pg + off, d);
        std::memset(pg + off + 8, ' ', 8);
        std::memcpy(pg + off + 8, s.data(), std::min<size_t>(s.size(), 8));
    };
    writeRow(88, dose0, arm0);
    writeRow(104, dose1, arm1);
    return sas;
}

// 19. R Serialized Data (.rds — XDR 'X'\n v2 VECSXP data.frame, raw or GZipped)
static std::vector<uint8_t> BuildRdsDataFrame(
    bool gzipWrap,
    std::string_view col0Name, double v0, double v1,
    std::string_view col1Name, std::string_view s0, std::string_view s1)
{
    std::vector<uint8_t> r;
    AppendU8(r, 'X'); AppendU8(r, '\n');
    AppendI32BE(r, 2);          // format v2
    AppendI32BE(r, 0x00040300); // R 4.3.0
    AppendI32BE(r, 0x00030500);

    auto emitCharSxp = [&](std::string_view s) {
        AppendI32BE(r, 9);
        AppendI32BE(r, static_cast<int32_t>(s.size()));
        AppendStr(r, s);
    };

    // Root VECSXP (19) with ATTRIB bit (1 << 9), length = 2
    AppendI32BE(r, 19 | (1 << 9));
    AppendI32BE(r, 2);

    // Col 0: REALSXP (14), len = 2
    AppendI32BE(r, 14);
    AppendI32BE(r, 2);
    AppendF64BE(r, v0);
    AppendF64BE(r, v1);

    // Col 1: STRSXP (16), len = 2
    AppendI32BE(r, 16);
    AppendI32BE(r, 2);
    emitCharSxp(s0);
    emitCharSxp(s1);

    // ATTRIB pairlist: LISTSXP (2) with HAS_TAG (1 << 10) -> SYMSXP(1) "names" -> STRSXP(16) [col0Name, col1Name] -> NILVALUE_SXP(254)
    AppendI32BE(r, 2 | (1 << 10));
    AppendI32BE(r, 1); // SYMSXP
    emitCharSxp("names");
    AppendI32BE(r, 16);
    AppendI32BE(r, 2);
    emitCharSxp(col0Name);
    emitCharSxp(col1Name);
    AppendI32BE(r, 254); // NILVALUE_SXP

    return gzipWrap ? MakeGzipStored(r) : r;
}

// 20. DuckDB Database (.duckdb — BinaryMetadataDeserializer Catalog + RowGroup + Uncompressed Column Segments)
static std::vector<uint8_t> BuildDuckDbFile(
    uint64_t fmtVer,
    uint64_t checkpointIter,
    std::string_view tableName,
    std::string_view col0Name, int64_t id0, int64_t id1,
    std::string_view col1Name, double val0, double val1,
    std::string_view col2Name, std::string_view tag0, std::string_view tag1)
{
    // 12288B headers + Block 0 (4096B MetaBlock) + Block 1 (4096B Columnar Data Block) = 20480B
    constexpr uint64_t kBlockAlloc = 4096;
    std::vector<uint8_t> d(12288 + 2 * kBlockAlloc, 0);
    std::memcpy(d.data() + 8, "DUCK", 4);
    PutU64LE(d.data() + 12, fmtVer);
    std::string eng = "v1.1.3";
    std::memcpy(d.data() + 52, eng.data(), eng.size());

    // DatabaseHeader 0 at 4096 (inactive iter=1) & DatabaseHeader 1 at 8192 (active checkpointIter)
    PutU64LE(d.data() + 4096 + 8, 1);
    PutU64LE(d.data() + 4096 + 16, UINT64_MAX);

    PutU64LE(d.data() + 8192 + 8, checkpointIter);
    PutU64LE(d.data() + 8192 + 16, 0);           // metaBlock = block 0, subIdx 0
    PutU64LE(d.data() + 8192 + 24, UINT64_MAX);  // freeList
    PutU64LE(d.data() + 8192 + 32, 2);           // blockCount = 2
    PutU64LE(d.data() + 8192 + 40, kBlockAlloc); // 4 KB blockAlloc
    PutU64LE(d.data() + 8192 + 48, 2048);        // vectorSize

    auto putVarU64 = [](std::vector<uint8_t>& b, uint64_t v) {
        do {
            uint8_t byte = static_cast<uint8_t>(v & 0x7Fu);
            v >>= 7;
            if (v != 0) byte |= 0x80u;
            b.push_back(byte);
        } while (v != 0);
    };
    auto putVarI64 = [&](std::vector<uint8_t>& b, int64_t v) {
        uint64_t zz = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
        putVarU64(b, zz);
    };
    auto putFid = [](std::vector<uint8_t>& b, uint16_t fid) { AppendU16LE(b, fid); };
    auto putEnd = [](std::vector<uint8_t>& b) { AppendU16LE(b, 0xFFFFu); };
    auto putStr = [&](std::vector<uint8_t>& b, std::string_view s) {
        putVarU64(b, s.size());
        AppendStr(b, s);
    };
    auto putMetaPtr = [&](std::vector<uint8_t>& b, uint64_t blkPtr, uint32_t subOff) {
        putFid(b, 100); putVarU64(b, blkPtr);
        putFid(b, 101); putVarU64(b, subOff);
        putEnd(b);
    };
    auto putColDef = [&](std::vector<uint8_t>& b, std::string_view cname, uint8_t typeId) {
        putFid(b, 100); putStr(b, cname);
        putFid(b, 101);
        putFid(b, 100); putVarU64(b, typeId);
        putEnd(b);
        putEnd(b);
    };
    auto makeColDataObj = [&](int64_t dataBlkId, uint32_t dataOff) {
        std::vector<uint8_t> cb;
        putFid(cb, 100); putVarU64(cb, 1); // 1 DataPointer
        putFid(cb, 100); putVarU64(cb, 0); // row_start = 0
        putFid(cb, 101); putVarU64(cb, 2); // tuple_count = 2
        putFid(cb, 102);                   // BlockPointer
        putFid(cb, 100); putVarI64(cb, dataBlkId);
        putFid(cb, 101); putVarU64(cb, dataOff);
        putEnd(cb);
        putFid(cb, 103); putVarU64(cb, 1); // UNCOMPRESSED = 1
        putEnd(cb);
        putEnd(cb);
        return cb;
    };

    // Place ColumnData objects at fixed sub-offsets inside MetaBlock 0:
    // chunkStart = 12288 + 8 = 12296; subOff 8..511 = Root Catalog; 512 = PTD; 768 = Col0; 896 = Col1; 1024 = Col2
    const uint32_t kPtdOff  = 512;
    const uint32_t kCol0Off = 768;
    const uint32_t kCol1Off = 896;
    const uint32_t kCol2Off = 1024;

    std::vector<uint8_t> rootCat;
    // Entry 0: Schema "main"
    putFid(rootCat, 100);
    putFid(rootCat, 100); putVarU64(rootCat, 2); // SCHEMA
    putFid(rootCat, 102); putStr(rootCat, "main");
    putFid(rootCat, 104); AppendU8(rootCat, 0);
    putEnd(rootCat);
    putEnd(rootCat);

    // Entry 1: User Table
    putFid(rootCat, 100);
    putFid(rootCat, 100); putVarU64(rootCat, 1); // TABLE
    putFid(rootCat, 102); putStr(rootCat, "main");
    putFid(rootCat, 104); AppendU8(rootCat, 0);
    putFid(rootCat, 200); putStr(rootCat, tableName);
    putFid(rootCat, 201); // ColumnList
    putFid(rootCat, 100); putVarU64(rootCat, 3);
    putColDef(rootCat, col0Name, 14); // BIGINT
    putColDef(rootCat, col1Name, 23); // DOUBLE
    putColDef(rootCat, col2Name, 25); // VARCHAR
    putEnd(rootCat);
    putEnd(rootCat);
    putFid(rootCat, 101); putMetaPtr(rootCat, 0, kPtdOff);
    putEnd(rootCat);

    // End of Catalog
    putEnd(rootCat);

    std::vector<uint8_t> ptd;
    putFid(ptd, 100); putVarU64(ptd, 2); // totalRows = 2
    putFid(ptd, 101); putVarU64(ptd, 1); // 1 inline RowGroup
    putFid(ptd, 100); putVarU64(ptd, 0);
    putFid(ptd, 101); putVarU64(ptd, 2);
    putFid(ptd, 102); putVarU64(ptd, 3);
    putMetaPtr(ptd, 0, kCol0Off);
    putMetaPtr(ptd, 0, kCol1Off);
    putMetaPtr(ptd, 0, kCol2Off);
    putEnd(ptd);
    putEnd(ptd);

    const auto cd0 = makeColDataObj(1, 0);
    const auto cd1 = makeColDataObj(1, 16);
    const auto cd2 = makeColDataObj(1, 32);

    uint8_t* chunk0 = d.data() + 12288 + 8;
    PutU64LE(chunk0 + 0, UINT64_MAX); // next_block_pointer = none
    std::memcpy(chunk0 + 8,        rootCat.data(), rootCat.size());
    std::memcpy(chunk0 + kPtdOff,  ptd.data(),     ptd.size());
    std::memcpy(chunk0 + kCol0Off, cd0.data(),     cd0.size());
    std::memcpy(chunk0 + kCol1Off, cd1.data(),     cd1.size());
    std::memcpy(chunk0 + kCol2Off, cd2.data(),     cd2.size());

    // Physical Block 1 (offset 16384): 8B block checksum + uncompressed column vectors
    uint8_t* data1 = d.data() + 16384 + 8;
    PutU64LE(data1 + 0,  static_cast<uint64_t>(id0));
    PutU64LE(data1 + 8,  static_cast<uint64_t>(id1));
    PutF64LE(data1 + 16, val0);
    PutF64LE(data1 + 24, val1);
    auto writeInlineStringT = [](uint8_t* dst, std::string_view sv) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(sv.size(), 12));
        PutU32LE(dst, n);
        std::memcpy(dst + 4, sv.data(), n);
    };
    writeInlineStringT(data1 + 32, tag0);
    writeInlineStringT(data1 + 48, tag1);
    return d;
}

// 21. ESRI Shapefile (.shp) + Sibling dBASE (.dbf)
static std::vector<uint8_t> BuildShpPointFile(double lat0, double lon0, double lat1, double lon1) {
    // 100B header + 2 Point records (8B record hdr + 20B content = 28B each -> 156B = 78 16-bit words)
    std::vector<uint8_t> shp(100, 0);
    PutU32BE(shp.data(), 9994);
    PutU32BE(shp.data() + 24, 78);
    PutU32LE(shp.data() + 28, 1000);
    PutU32LE(shp.data() + 32, 1); // Point
    PutF64LE(shp.data() + 36, std::min(lon0, lon1));
    PutF64LE(shp.data() + 44, std::min(lat0, lat1));
    PutF64LE(shp.data() + 52, std::max(lon0, lon1));
    PutF64LE(shp.data() + 60, std::max(lat0, lat1));

    auto addPt = [&](uint32_t recNum, double lon, double lat) {
        AppendU32BE(shp, recNum);
        AppendU32BE(shp, 10); // 20 bytes = 10 words
        AppendI32LE(shp, 1);  // Point
        AppendF64LE(shp, lon);
        AppendF64LE(shp, lat);
    };
    addPt(1, lon0, lat0);
    addPt(2, lon1, lat1);
    return shp;
}

static std::vector<uint8_t> BuildSiblingDbf(std::string_view stn0, std::string_view stn1) {
    // 32B header + 1 x 32B field descriptor ("STATION", C, 12) + 1B 0x0D = 65B header; 2 records x 13B
    std::vector<uint8_t> dbf(65, 0);
    dbf[0] = 0x03;
    PutU32LE(dbf.data() + 4, 2);  // 2 records
    PutU16LE(dbf.data() + 8, 65); // headerBytes
    PutU16LE(dbf.data() + 10, 13); // recordBytes = 1 + 12
    std::memcpy(dbf.data() + 32, "STATION", 7);
    dbf[32 + 11] = 'C';
    dbf[32 + 16] = 12;
    dbf[64] = 0x0D;

    auto addRec = [&](std::string_view s) {
        AppendU8(dbf, ' ');
        uint8_t f[12]; std::memset(f, ' ', 12);
        std::memcpy(f, s.data(), std::min<size_t>(s.size(), 12));
        AppendBytes(dbf, f, 12);
    };
    addRec(stn0);
    addRec(stn1);
    return dbf;
}

// 22. Garmin FIT Activity (.fit)
static std::vector<uint8_t> BuildGarminFitFile(double lat0, double lon0, uint8_t hr0, double lat1, double lon1, uint8_t hr1) {
    std::vector<uint8_t> pay;
    // Definition Message for local type 0 -> global msg 20 (record), 5 fields: ts(253), lat(0), lon(1), alt(2), hr(3)
    AppendU8(pay, 0x40); // Definition, local 0
    AppendU8(pay, 0);    // reserved
    AppendU8(pay, 0);    // little-endian
    AppendU16LE(pay, 20); // record
    AppendU8(pay, 5);    // 5 fields
    AppendU8(pay, 253); AppendU8(pay, 4); AppendU8(pay, 0x86); // uint32 timestamp
    AppendU8(pay, 0);   AppendU8(pay, 4); AppendU8(pay, 0x85); // sint32 lat
    AppendU8(pay, 1);   AppendU8(pay, 4); AppendU8(pay, 0x85); // sint32 lon
    AppendU8(pay, 2);   AppendU8(pay, 2); AppendU8(pay, 0x84); // uint16 altitude
    AppendU8(pay, 3);   AppendU8(pay, 1); AppendU8(pay, 0x02); // uint8 heart_rate

    constexpr double kDegToSemi = 2147483648.0 / 180.0;
    auto emitRec = [&](uint32_t ts, double lat, double lon, uint16_t rawAlt, uint8_t hr) {
        AppendU8(pay, 0x00); // Data msg local 0
        AppendU32LE(pay, ts);
        AppendI32LE(pay, static_cast<int32_t>(std::llround(lat * kDegToSemi)));
        AppendI32LE(pay, static_cast<int32_t>(std::llround(lon * kDegToSemi)));
        AppendU16LE(pay, rawAlt);
        AppendU8(pay, hr);
    };
    emitRec(1100000000u, lat0, lon0, 3500u, hr0); // alt = 3500/5 - 500 = 200m
    emitRec(1100000010u, lat1, lon1, 3600u, hr1); // alt = 220m

    std::vector<uint8_t> fit(14, 0);
    fit[0] = 14;
    fit[1] = 0x20;
    PutU16LE(fit.data() + 2, 2140);
    PutU32LE(fit.data() + 4, static_cast<uint32_t>(pay.size()));
    std::memcpy(fit.data() + 8, ".FIT", 4);
    AppendBytes(fit, pay.data(), pay.size());
    AppendU16LE(fit, 0); // CRC trailer
    return fit;
}

// 23. NetCDF Classic (.nc — CDF\x01 with lat, lon, temp float32 arrays)
static std::vector<uint8_t> BuildNetCdfFile(float lat0, float lon0, float temp0, float lat1, float lon1, float temp1) {
    std::vector<uint8_t> nc = { 'C', 'D', 'F', 0x01 };
    AppendU32BE(nc, 0); // numrecs = 0

    auto putName = [&](std::string_view s) {
        AppendU32BE(nc, static_cast<uint32_t>(s.size()));
        AppendStr(nc, s);
        while ((nc.size() % 4) != 0) AppendU8(nc, 0);
    };

    // Dimensions: 1 dim "station" = 2
    AppendU32BE(nc, 0x0000000Au);
    AppendU32BE(nc, 1);
    putName("station");
    AppendU32BE(nc, 2);

    // Global attributes: empty (0, 0)
    AppendU32BE(nc, 0); AppendU32BE(nc, 0);

    // Variables: 3 float32 (ncType = 5) vars: "lat", "lon", "air_temp"
    AppendU32BE(nc, 0x0000000Bu);
    AppendU32BE(nc, 3);

    size_t offPatch[3] = {};
    const char* vnames[3] = { "lat", "lon", "air_temp" };
    for (int i = 0; i < 3; ++i) {
        putName(vnames[i]);
        AppendU32BE(nc, 1); // 1 dim
        AppendU32BE(nc, 0); // dimid 0
        AppendU32BE(nc, 0); AppendU32BE(nc, 0); // empty var attrs
        AppendU32BE(nc, 5); // NC_FLOAT
        AppendU32BE(nc, 8); // vsize = 8 bytes
        offPatch[i] = nc.size();
        AppendU32BE(nc, 0); // begin placeholder
    }

    PutU32BE(nc.data() + offPatch[0], static_cast<uint32_t>(nc.size()));
    AppendF32BE(nc, lat0); AppendF32BE(nc, lat1);
    PutU32BE(nc.data() + offPatch[1], static_cast<uint32_t>(nc.size()));
    AppendF32BE(nc, lon0); AppendF32BE(nc, lon1);
    PutU32BE(nc.data() + offPatch[2], static_cast<uint32_t>(nc.size()));
    AppendF32BE(nc, temp0); AppendF32BE(nc, temp1);
    return nc;
}

// 24. WMO GRIB Edition 2 (.grib2 — 2x2 Lat/Lon Simple-Packed Temperature Grid)
static std::vector<uint8_t> BuildGrib2File(float refKelvin, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3) {
    std::vector<uint8_t> secs;

    // Section 1: Identification (21B)
    std::vector<uint8_t> s1(21, 0);
    PutU32BE(s1.data(), 21); s1[4] = 1;
    PutU16BE(s1.data() + 12, 2026); s1[14] = 11; s1[15] = 15; s1[16] = 12; s1[17] = 0;
    AppendBytes(secs, s1.data(), s1.size());

    // Section 3: Grid Definition (72B, Template 3.0 Lat/Lon 2x2)
    std::vector<uint8_t> s3(72, 0);
    PutU32BE(s3.data(), 72); s3[4] = 3;
    PutU32BE(s3.data() + 6, 4);   // 4 points
    PutU16BE(s3.data() + 12, 0);  // Template 0
    PutU32BE(s3.data() + 30, 2);  // Ni = 2
    PutU32BE(s3.data() + 34, 2);  // Nj = 2
    PutU32BE(s3.data() + 46, 50000000u); // La1 = 50.0N
    PutU32BE(s3.data() + 50, 14000000u); // Lo1 = 14.0E
    PutU32BE(s3.data() + 55, 51000000u); // La2 = 51.0N
    PutU32BE(s3.data() + 59, 15000000u); // Lo2 = 15.0E
    AppendBytes(secs, s3.data(), s3.size());

    // Section 4: Product Definition (34B, Discipline 0, Cat 0, Param 0 = Temperature (K))
    std::vector<uint8_t> s4(34, 0);
    PutU32BE(s4.data(), 34); s4[4] = 4;
    s4[9] = 0; s4[10] = 0;
    AppendBytes(secs, s4.data(), s4.size());

    // Section 5: Data Representation (21B, Template 5.0 Simple Packing, E=0, D=0, 8 bits/val)
    std::vector<uint8_t> s5(21, 0);
    PutU32BE(s5.data(), 21); s5[4] = 5;
    PutU32BE(s5.data() + 5, 4); // 4 values
    PutU16BE(s5.data() + 9, 0); // Simple packing
    { uint32_t ru = 0; std::memcpy(&ru, &refKelvin, 4); PutU32BE(s5.data() + 11, ru); }
    PutU16BE(s5.data() + 15, 0); // E = 0
    PutU16BE(s5.data() + 17, 0); // D = 0
    s5[19] = 8;                  // 8 bits per value
    AppendBytes(secs, s5.data(), s5.size());

    // Section 6: Bit-Map (6B, indicator 255 = no bitmap)
    std::vector<uint8_t> s6 = { 0, 0, 0, 6, 6, 255 };
    AppendBytes(secs, s6.data(), s6.size());

    // Section 7: Data Section (5B header + 4 x 8-bit values = 9B)
    std::vector<uint8_t> s7 = { 0, 0, 0, 9, 7, d0, d1, d2, d3 };
    AppendBytes(secs, s7.data(), s7.size());

    uint64_t totalLen = 16 + secs.size() + 4;
    std::vector<uint8_t> grib = { 'G', 'R', 'I', 'B', 0, 0, 0, 2 };
    AppendU64BE(grib, totalLen);
    AppendBytes(grib, secs.data(), secs.size());
    AppendStr(grib, "7777");
    return grib;
}

// 25. NIfTI-1 3D Neuroimaging Volume (.nii / .nii.gz)
static std::vector<uint8_t> BuildNifti1Volume(bool gzipWrap, std::string_view descrip, float baseVoxVal) {
    std::vector<uint8_t> nii(352 + 4 * 4 * 3 * 4, 0); // 352B offset + 4x4x3 float32 voxels
    PutU32LE(nii.data(), 348);        // sizeof_hdr = 348
    PutU16LE(nii.data() + 40, 3);     // 3D
    PutU16LE(nii.data() + 42, 4);     // nx = 4
    PutU16LE(nii.data() + 44, 4);     // ny = 4
    PutU16LE(nii.data() + 46, 3);     // nz = 3
    PutU16LE(nii.data() + 70, 16);    // DT_FLOAT32
    PutU16LE(nii.data() + 72, 32);    // 32 bits/voxel
    PutF32LE(nii.data() + 80, 1.5f);  // pixdim[1]
    PutF32LE(nii.data() + 84, 1.5f);  // pixdim[2]
    PutF32LE(nii.data() + 88, 2.0f);  // pixdim[3]
    PutF32LE(nii.data() + 108, 352.0f); // vox_offset = 352
    PutF32LE(nii.data() + 112, 1.0f); // scl_slope = 1.0
    PutF32LE(nii.data() + 116, 0.0f); // scl_inter = 0.0
    nii[123] = 2; // NIFTI_UNITS_MM
    std::memcpy(nii.data() + 148, descrip.data(), std::min<size_t>(descrip.size(), 79));
    PutU16LE(nii.data() + 254, 1);    // sform_code = 1
    PutF32LE(nii.data() + 280, 1.5f);
    PutF32LE(nii.data() + 300, 1.5f);
    PutF32LE(nii.data() + 320, 2.0f);
    std::memcpy(nii.data() + 344, "n+1\0", 4);

    for (size_t i = 0; i < 48; ++i) {
        PutF32LE(nii.data() + 352 + i * 4, baseVoxVal + static_cast<float>(i));
    }
    return gzipWrap ? MakeGzipStored(nii) : nii;
}

// 26. Microsoft Access Jet4/ACE (.accdb) & Jet3.5 (.mdb) Multi-Page Databases
struct SynthJetCol {
    uint8_t type;
    uint16_t colId;
    uint16_t varIdx;
    uint16_t displayIdx;
    bool isFixed;
    uint16_t fixedOff;
    uint16_t colLen;
    std::string name;
};

static void WriteJet4TdefPage(uint8_t* pg, uint8_t tblType, uint16_t numVarCols, const std::vector<SynthJetCol>& cols) {
    pg[0] = 0x02u;
    pg[1] = 0x01u;
    PutU32LE(pg + 4, 0u);
    PutU32LE(pg + 16, 2u);
    pg[40] = tblType;
    PutU16LE(pg + 41, static_cast<uint16_t>(cols.size()));
    PutU16LE(pg + 43, numVarCols);
    PutU16LE(pg + 45, static_cast<uint16_t>(cols.size()));
    PutU32LE(pg + 47, 0u);
    PutU32LE(pg + 51, 0u);

    size_t pos = 63u;
    for (const auto& c : cols) {
        uint8_t* cb = pg + pos;
        cb[0] = c.type;
        PutU16LE(cb + 5, c.colId);
        PutU16LE(cb + 7, c.varIdx);
        PutU16LE(cb + 9, c.displayIdx);
        cb[15] = c.isFixed ? 0x01u : 0x02u;
        PutU16LE(cb + 21, c.fixedOff);
        PutU16LE(cb + 23, c.colLen);
        pos += 25u;
    }
    for (const auto& c : cols) {
        PutU16LE(pg + pos, static_cast<uint16_t>(c.name.size() * 2u));
        pos += 2u;
        for (char ch : c.name) {
            PutU16LE(pg + pos, static_cast<uint16_t>(static_cast<uint8_t>(ch)));
            pos += 2u;
        }
    }
    PutU32LE(pg + 8, static_cast<uint32_t>(pos));
}

static void WriteJet4DataPage(uint8_t* pg, uint32_t ownerTdefPg, const std::vector<std::vector<uint8_t>>& rows) {
    pg[0] = 0x01u;
    pg[1] = 0x01u;
    PutU32LE(pg + 4, ownerTdefPg);
    PutU16LE(pg + 12, static_cast<uint16_t>(rows.size()));
    size_t curEnd = 4096u;
    for (size_t i = 0; i < rows.size(); ++i) {
        curEnd -= rows[i].size();
        std::memcpy(pg + curEnd, rows[i].data(), rows[i].size());
        PutU16LE(pg + 14u + i * 2u, static_cast<uint16_t>(curEnd));
    }
}

static std::vector<uint8_t> BuildJet4AceAccdb(
    std::string_view userTableName,
    int32_t id0, double reading0, std::string_view loc0, int32_t batch0,
    int32_t id1, double reading1, std::string_view loc1)
{
    std::vector<uint8_t> db(6 * 4096, 0);
    db[0] = 0x00u; db[1] = 0x01u;
    std::memcpy(db.data() + 4, "Standard ACE DB", 15);
    PutU32LE(db.data() + 0x14, 2u); // ACE 12+

    // Page 2: MSysObjects TDEF (Id LONG, Type INT16, Flags LONG, Name TEXT)
    WriteJet4TdefPage(db.data() + 2 * 4096, 0x4Eu, 1u, {
        { 0x04u, 0u, 0u, 0u, true,  0u, 4u,   "Id"    },
        { 0x03u, 1u, 0u, 1u, true,  4u, 2u,   "Type"  },
        { 0x04u, 2u, 0u, 2u, true,  6u, 4u,   "Flags" },
        { 0x0Au, 3u, 0u, 3u, false, 0u, 255u, "Name"  }
    });

    // Page 3: MSysObjects DATA mapping Id=4 -> userTableName (Type=1, Flags=0)
    std::vector<uint8_t> catRow;
    AppendU16LE(catRow, 4u); // 4 cols
    AppendI32LE(catRow, 4);  // Id = TDEF page 4
    AppendU16LE(catRow, 1u); // Type = 1 (local table)
    AppendI32LE(catRow, 0);  // Flags = 0
    uint16_t catVar0 = static_cast<uint16_t>(catRow.size());
    AppendU8(catRow, 0xFFu); AppendU8(catRow, 0xFEu); // Jet4 compressed Unicode prefix
    AppendStr(catRow, userTableName);
    uint16_t catEod = static_cast<uint16_t>(catRow.size());
    AppendU16LE(catRow, catEod);
    AppendU16LE(catRow, catVar0);
    AppendU16LE(catRow, 1u);   // numVarCols = 1
    AppendU8(catRow, 0x0Fu);   // null bitmap: all 4 columns non-null
    WriteJet4DataPage(db.data() + 3 * 4096, 2u, { catRow });

    // Page 4: User Table TDEF (SensorId LONG, Reading DOUBLE, Active BOOL, Location TEXT, BatchCode LONG)
    WriteJet4TdefPage(db.data() + 4 * 4096, 0x53u, 1u, {
        { 0x04u, 0u, 0u, 0u, true,  0u,  4u,   "SensorId"  },
        { 0x07u, 1u, 0u, 1u, true,  4u,  8u,   "Reading"   },
        { 0x01u, 2u, 0u, 2u, true,  0u,  1u,   "Active"    },
        { 0x0Au, 3u, 0u, 3u, false, 0u,  128u, "Location"  },
        { 0x04u, 4u, 0u, 4u, true,  12u, 4u,   "BatchCode" }
    });

    auto makeUserRow = [](int32_t sid, double rd, bool active, std::string_view loc, int32_t batch, bool batchNotNull) {
        std::vector<uint8_t> r;
        AppendU16LE(r, 5u); // 5 cols
        AppendI32LE(r, sid);
        AppendF64LE(r, rd);
        AppendI32LE(r, batch);
        uint16_t v0 = static_cast<uint16_t>(r.size());
        AppendU8(r, 0xFFu); AppendU8(r, 0xFEu);
        AppendStr(r, loc);
        uint16_t eod = static_cast<uint16_t>(r.size());
        AppendU16LE(r, eod);
        AppendU16LE(r, v0);
        AppendU16LE(r, 1u);
        uint8_t bmp = 0x01u | 0x02u | (active ? 0x04u : 0x00u) | 0x08u | (batchNotNull ? 0x10u : 0x00u);
        AppendU8(r, bmp);
        return r;
    };

    // Page 5: User Table DATA (Row 0 has Active=TRUE & non-null BatchCode; Row 1 has Active=FALSE & NULL BatchCode)
    WriteJet4DataPage(db.data() + 5 * 4096, 4u, {
        makeUserRow(id0, reading0, true,  loc0, batch0, true),
        makeUserRow(id1, reading1, false, loc1, 0,      false)
    });
    return db;
}

static std::vector<uint8_t> BuildJet35Mdb(std::string_view tableName, int32_t codeVal, std::string_view labelVal) {
    std::vector<uint8_t> db(4 * 2048, 0); // 4 x 2KB pages
    db[0] = 0x00u; db[1] = 0x01u;
    std::memcpy(db.data() + 4, "Standard Jet DB", 15);
    PutU32LE(db.data() + 0x14, 0u); // Jet 3.5 (2048B pages)

    // Page 3: User Table TDEF (Jet3 43B header + 2 x 18B cols + 1B-prefixed ASCII names)
    uint8_t* tpg = db.data() + 3 * 2048;
    tpg[0] = 0x02u; tpg[1] = 0x01u;
    PutU32LE(tpg + 12, 1u); // num_rows
    tpg[20] = 0x53u;        // 'S' user table
    PutU16LE(tpg + 21, 2u); // max_cols
    PutU16LE(tpg + 23, 1u); // num_var_cols
    PutU16LE(tpg + 25, 2u); // num_cols
    PutU32LE(tpg + 31, 0u); // num_real_idx

    // Col 0: Code (LONG 0x04, fixed @ 0, 4B), Col 1: Label (TEXT 0x0A, varIdx 0)
    uint8_t* c0 = tpg + 43;
    c0[0] = 0x04u; PutU16LE(c0 + 1, 0u); PutU16LE(c0 + 5, 0u); c0[13] = 0x01u; PutU16LE(c0 + 14, 0u); PutU16LE(c0 + 16, 4u);
    uint8_t* c1 = tpg + 43 + 18;
    c1[0] = 0x0Au; PutU16LE(c1 + 1, 1u); PutU16LE(c1 + 3, 0u); PutU16LE(c1 + 5, 1u); c1[13] = 0x02u; PutU16LE(c1 + 16, 64u);

    size_t np = 43 + 36;
    tpg[np++] = 4u; std::memcpy(tpg + np, "Code", 4); np += 4;
    tpg[np++] = 5u; std::memcpy(tpg + np, "Label", 5); np += 5;
    PutU32LE(tpg + 8, static_cast<uint32_t>(np));

    // Page 2: MSysObjects TDEF + Page 1: MSysObjects DATA + User Table DATA
    uint8_t* mpg = db.data() + 2 * 2048;
    mpg[0] = 0x02u; mpg[1] = 0x01u; mpg[20] = 0x4Eu;
    PutU16LE(mpg + 21, 2u); PutU16LE(mpg + 23, 1u); PutU16LE(mpg + 25, 2u);
    uint8_t* mc0 = mpg + 43;
    mc0[0] = 0x04u; PutU16LE(mc0 + 1, 0u); PutU16LE(mc0 + 5, 0u); mc0[13] = 0x01u; PutU16LE(mc0 + 14, 0u); PutU16LE(mc0 + 16, 4u);
    uint8_t* mc1 = mpg + 43 + 18;
    mc1[0] = 0x0Au; PutU16LE(mc1 + 1, 1u); PutU16LE(mc1 + 3, 0u); PutU16LE(mc1 + 5, 1u); mc1[13] = 0x02u; PutU16LE(mc1 + 16, 64u);
    size_t mnp = 43 + 36;
    mpg[mnp++] = 2u; std::memcpy(mpg + mnp, "Id", 2); mnp += 2;
    mpg[mnp++] = 4u; std::memcpy(mpg + mnp, "Name", 4); mnp += 4;
    PutU32LE(mpg + 8, static_cast<uint32_t>(mnp));

    auto writeJet3RowPage = [](uint8_t* dpg, uint32_t ownerTdef, int32_t iVal, std::string_view sVal) {
        std::vector<uint8_t> r;
        AppendU8(r, 2u); // 2 cols (1B in Jet3)
        AppendI32LE(r, iVal);
        uint8_t v0 = static_cast<uint8_t>(r.size());
        AppendStr(r, sVal);
        uint8_t eod = static_cast<uint8_t>(r.size());
        AppendU8(r, eod);
        AppendU8(r, v0);
        AppendU8(r, 1u); // numVar = 1
        AppendU8(r, 0x03u); // null bitmap

        dpg[0] = 0x01u; dpg[1] = 0x01u;
        PutU32LE(dpg + 4, ownerTdef);
        PutU16LE(dpg + 8, 1u);
        size_t start = 2048u - r.size();
        std::memcpy(dpg + start, r.data(), r.size());
        PutU16LE(dpg + 10, static_cast<uint16_t>(start));
    };

    db.resize(5 * 2048, 0);
    writeJet3RowPage(db.data() + 1 * 2048, 2u, 3, tableName);
    writeJet3RowPage(db.data() + 4 * 2048, 3u, codeVal, labelVal);
    return db;
}

// 27. Mapbox MBTiles SQLite3 B-Tree Database (.mbtiles)
static std::vector<uint8_t> BuildMbTilesDb(
    std::string_view tsName,
    std::string_view fmtStr,
    std::string_view boundsStr, // may be empty to test TMS tile -> WGS84 derivation!
    std::string_view centerStr,
    int16_t z0, int16_t x0, int16_t y0,
    int16_t z1, int16_t x1, int16_t y1)
{
    std::vector<uint8_t> db(3 * 4096, 0);
    std::memcpy(db.data(), "SQLite format 3\0", 16);
    PutU16BE(db.data() + 16, 4096);
    db[18] = 1; db[19] = 1; db[21] = 64; db[22] = 32; db[23] = 32;
    PutU32BE(db.data() + 28, 3);
    PutU32BE(db.data() + 40, 1);
    PutU32BE(db.data() + 44, 4);
    PutU32BE(db.data() + 56, 1); // UTF-8
    std::memcpy(db.data() + 68, "MPBX", 4);

    auto textCol = [](std::string_view s) -> std::pair<uint64_t, std::vector<uint8_t>> {
        return { 13ULL + 2ULL * s.size(), std::vector<uint8_t>(s.begin(), s.end()) };
    };
    auto int16Col = [](int16_t v) -> std::pair<uint64_t, std::vector<uint8_t>> {
        uint16_t u = static_cast<uint16_t>(v);
        return { 2ULL, { static_cast<uint8_t>(u >> 8), static_cast<uint8_t>(u & 0xFFu) } };
    };
    auto blobCol = [](const std::vector<uint8_t>& b) -> std::pair<uint64_t, std::vector<uint8_t>> {
        return { 12ULL + 2ULL * b.size(), b };
    };

    // Page 1: sqlite_master -> metadata (rootpage=2) & tiles (rootpage=3)
    std::vector<uint8_t> mMeta = MakeSqliteRecord({
        textCol("table"), textCol("metadata"), textCol("metadata"), { 1ULL, { 2 } },
        textCol("CREATE TABLE metadata (name TEXT, value TEXT)")
    });
    std::vector<uint8_t> mTiles = MakeSqliteRecord({
        textCol("table"), textCol("tiles"), textCol("tiles"), { 1ULL, { 3 } },
        textCol("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)")
    });
    WriteSqliteLeafPageCells(db.data(), 100, { { 1, mMeta }, { 2, mTiles } });

    // Page 2: metadata rows
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> metaCells;
    uint64_t rid = 1;
    metaCells.push_back({ rid++, MakeSqliteRecord({ textCol("name"), textCol(tsName) }) });
    metaCells.push_back({ rid++, MakeSqliteRecord({ textCol("format"), textCol(fmtStr) }) });
    metaCells.push_back({ rid++, MakeSqliteRecord({ textCol("minzoom"), textCol("4") }) });
    metaCells.push_back({ rid++, MakeSqliteRecord({ textCol("maxzoom"), textCol("12") }) });
    if (!boundsStr.empty()) {
        metaCells.push_back({ rid++, MakeSqliteRecord({ textCol("bounds"), textCol(boundsStr) }) });
    }
    if (!centerStr.empty()) {
        metaCells.push_back({ rid++, MakeSqliteRecord({ textCol("center"), textCol(centerStr) }) });
    }
    WriteSqliteLeafPageCells(db.data() + 4096, 0, metaCells);

    // Page 3: tiles rows
    std::vector<uint8_t> fakePng = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<uint8_t> t0 = MakeSqliteRecord({ int16Col(z0), int16Col(x0), int16Col(y0), blobCol(fakePng) });
    std::vector<uint8_t> t1 = MakeSqliteRecord({ int16Col(z1), int16Col(x1), int16Col(y1), blobCol(fakePng) });
    WriteSqliteLeafPageCells(db.data() + 8192, 0, { { 1, t0 }, { 2, t1 } });
    return db;
}

} // namespace

// ============================================================================
// Stage 15 ("15.docsa") Self-Test Runner
// ============================================================================
void RunFormatTestsDocs(TestCounter& tc) {
    TempFileRegistry tmp(L"docsa_stage15");
    const std::wstring tdDir = TestDataDir();
    const std::vector<uint8_t> kGarbage = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF, 0x13, 0x37, 0x42, 0x99, 0xAA, 0x55 };

    // ------------------------------------------------------------------------
    // 0. Repository testdata/ Regression & Real Multi-Page B-Tree Verification
    // ------------------------------------------------------------------------
    {
        ParsedOfficeDocument chinook;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\chinook_sample.sqlite", chinook);
        bool hasDecodedTableRows = chinook.sheets.size() >= 2 && chinook.sheets[1].rows.size() >= 2;
        tc.Record(ok && chinook.valid && chinook.sheets.size() >= 10 && hasDecodedTableRows &&
                  DocContainsCI(chinook, L"album") && DocContainsCI(chinook, L"artist") && DocContainsCI(chinook, L"track"),
            L"DocsA.TestData.ChinookSqliteBTree",
            L"sheets=" + std::to_wstring(chinook.sheets.size()));

        ParsedOfficeDocument pq;
        bool pqOk = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\telemetry_columnar.parquet", pq);
        tc.Record(pqOk && pq.valid && !pq.sheets.empty() && DocContains(pq, L"sensor_id"),
            L"DocsA.TestData.TelemetryParquet");

        ParsedOfficeDocument av;
        bool avOk = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\device_events.avro", av);
        tc.Record(avOk && av.valid && !av.sheets.empty() && DocContains(av, L"device_id"),
            L"DocsA.TestData.DeviceEventsAvro");

        ParsedOfficeDocument bs;
        bool bsOk = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\config_document.bson", bs);
        tc.Record(bsOk && bs.valid && !bs.sheets.empty() && DocContains(bs, L"Explorer2"),
            L"DocsA.TestData.ConfigDocumentBson");

        std::wstring repoXlsx = tdDir + L"\\spreadsheet_workbook.xlsx";
        if (GetFileAttributesW(repoXlsx.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ParsedOfficeDocument xlsxFix;
            bool xOk = SpreadsheetDocViewer::InspectOfficeFile(repoXlsx, xlsxFix) &&
                       xlsxFix.valid && xlsxFix.isSpreadsheet &&
                       !xlsxFix.sheets.empty() && !xlsxFix.sheets[0].rows.empty();
            tc.Record(xOk, L"DocsA.TestData.SpreadsheetWorkbookXlsx",
                L"sheets=" + std::to_wstring(xlsxFix.sheets.size()));
        }

        std::wstring repoOds = tdDir + L"\\calc_spreadsheet.ods";
        if (GetFileAttributesW(repoOds.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ParsedOfficeDocument odsFix;
            bool oOk = SpreadsheetDocViewer::InspectOfficeFile(repoOds, odsFix) &&
                       odsFix.valid && odsFix.isSpreadsheet &&
                       odsFix.formatTitle == L"OpenDocument Spreadsheet (.ods)" &&
                       !odsFix.sheets.empty();
            bool noTrailingBloat = oOk;
            for (const auto& sh : odsFix.sheets) {
                if (sh.rows.empty() || sh.rows.size() >= 500 || sh.maxCols == 0 || sh.maxCols >= 64) {
                    noTrailingBloat = false;
                }
            }
            tc.Record(noTrailingBloat, L"DocsA.TestData.CalcSpreadsheetOdsNoTrailingBloat",
                odsFix.sheets.empty()
                    ? L"sheets=0"
                    : (L"rows=" + std::to_wstring(odsFix.sheets[0].rows.size()) +
                       L" maxCols=" + std::to_wstring(odsFix.sheets[0].maxCols)));
        }
    }

    // ------------------------------------------------------------------------
    // 1. Legacy Excel OLE2 BIFF8 (.xls) — SST CONTINUE Spanning + RK/MULRK/FORMULA
    // ------------------------------------------------------------------------
    {
        std::wstring pA = tmp.Write(L"book_a.xls", BuildBiff8Xls("RevenueQ1", "NorthRegion", "SplitHead_", "TailAlpha", 4250.75, -915, "FX_OK_A", true));
        std::wstring pB = tmp.Write(L"book_b.xls", BuildBiff8Xls("InventoryQ4", "SouthDepot", "CrossRec_", "OmegaEnd", 9812.5, 2048, "FX_OK_B", false));
        std::wstring pBad = tmp.Write(L"bad.xls", kGarbage);

        ParsedOfficeDocument dA, dB;
        bool okA = SpreadsheetDocViewer::InspectOfficeFile(pA, dA);
        bool okB = SpreadsheetDocViewer::InspectOfficeFile(pB, dB);
        bool cellsOk = okA && okB && dA.valid && dB.valid &&
            dA.sheets.size() == 1 && dA.sheets[0].name == L"RevenueQ1" &&
            SheetContains(dA, L"RevenueQ1", L"NorthRegion") &&
            SheetContains(dA, L"RevenueQ1", L"SplitHead_TailAlpha") &&
            SheetContains(dA, L"RevenueQ1", L"4250.75") &&
            SheetContains(dA, L"RevenueQ1", L"-915") &&
            SheetContains(dA, L"RevenueQ1", L"123.45") &&
            SheetContains(dA, L"RevenueQ1", L"FX_OK_A") &&
            SheetContains(dA, L"RevenueQ1", L"TRUE") &&
            SheetContains(dB, L"InventoryQ4", L"CrossRec_OmegaEnd") &&
            SheetContains(dB, L"InventoryQ4", L"FALSE") &&
            HashParsedDoc(dA) != HashParsedDoc(dB) &&
            InspectFails(pBad);
        tc.Record(cellsOk, L"DocsA.Biff8Xls.Ole2SstContinueAndRecords");
    }

    // ------------------------------------------------------------------------
    // 2. SQLite 3 Multi-Page B-Tree (.sqlite) & OGC GeoPackage (.gpkg)
    // ------------------------------------------------------------------------
    {
        std::wstring sA = tmp.Write(L"db_a.sqlite", BuildSqliteBtreeDb("thermocouples", "probe_alpha", 23.5, "probe_beta", 88.25));
        std::wstring sB = tmp.Write(L"db_b.sqlite", BuildSqliteBtreeDb("barometers", "station_west", 1013.25, "station_east", 998.5));
        std::wstring sBad = tmp.Write(L"bad.sqlite", kGarbage);

        ParsedOfficeDocument dA, dB;
        bool okA = SpreadsheetDocViewer::InspectOfficeFile(sA, dA);
        bool okB = SpreadsheetDocViewer::InspectOfficeFile(sB, dB);
        tc.Record(okA && okB && dA.valid && dB.valid &&
                  SheetContains(dA, L"thermocouples", L"probe_alpha") &&
                  SheetContains(dA, L"thermocouples", L"101") &&
                  SheetContains(dA, L"thermocouples", L"88.25") &&
                  SheetContains(dB, L"barometers", L"station_west") &&
                  SheetContains(dB, L"barometers", L"1013.25") &&
                  HashParsedDoc(dA) != HashParsedDoc(dB) &&
                  InspectFails(sBad),
            L"DocsA.Sqlite3.MultiPageBTreeRecords");

        std::wstring gA = tmp.Write(L"map_a.gpkg", BuildGeoPackageDb("PragueCastle", 50.0909, 14.4005, "CharlesBridge", 50.0865, 14.4114));
        std::wstring gB = tmp.Write(L"map_b.gpkg", BuildGeoPackageDb("ViennaOpera", 48.2032, 16.3691, "PraterPark", 48.2167, 16.3958));
        std::wstring gBad = tmp.Write(L"bad.gpkg", kGarbage);

        ParsedOfficeDocument gdA, gdB;
        bool gOkA = SpreadsheetDocViewer::InspectOfficeFile(gA, gdA);
        bool gOkB = SpreadsheetDocViewer::InspectOfficeFile(gB, gdB);
        tc.Record(gOkA && gOkB && gdA.valid && gdB.valid &&
                  gdA.hasGeoTrack && gdA.geoPoints.size() == 2 &&
                  std::fabs(gdA.geoPoints[0].lat - 50.0909) < 1e-4 &&
                  std::fabs(gdA.geoPoints[0].lon - 14.4005) < 1e-4 &&
                  std::fabs(gdB.geoPoints[0].lat - 48.2032) < 1e-4 &&
                  SheetContains(gdA, L"landmarks", L"PragueCastle") &&
                  HashParsedDoc(gdA) != HashParsedDoc(gdB) &&
                  InspectFails(gBad),
            L"DocsA.GeoPackage.SqliteWkbGeometryPoints");
    }

    // ------------------------------------------------------------------------
    // 3. Apache Parquet (.parquet — Thrift Compact Footer + Data Page Decoder)
    // ------------------------------------------------------------------------
    {
        std::wstring pA = tmp.Write(L"part_a.parquet", BuildParquetFile(701, 36.5, "rack_north", 702, 41.25, "rack_south"));
        std::wstring pB = tmp.Write(L"part_b.parquet", BuildParquetFile(901, -12.75, "cryo_bay1", 902, -18.5, "cryo_bay2"));
        std::wstring pBad = tmp.Write(L"bad.parquet", kGarbage);

        ParsedOfficeDocument dA, dB;
        bool okA = SpreadsheetDocViewer::InspectOfficeFile(pA, dA);
        bool okB = SpreadsheetDocViewer::InspectOfficeFile(pB, dB);
        tc.Record(okA && okB && dA.valid && dB.valid &&
                  SheetContains(dA, L"Parquet_Schema_And_Stats", L"INT32") &&
                  SheetContains(dA, L"Parquet_Schema_And_Stats", L"DOUBLE") &&
                  SheetContains(dA, L"Parquet_Data_Preview", L"701") &&
                  SheetContains(dA, L"Parquet_Data_Preview", L"36.5") &&
                  SheetContains(dA, L"Parquet_Data_Preview", L"rack_north") &&
                  SheetContains(dB, L"Parquet_Data_Preview", L"cryo_bay2") &&
                  HashParsedDoc(dA) != HashParsedDoc(dB) &&
                  InspectFails(pBad),
            L"DocsA.Parquet.ThriftFooterAndPlainPages");
    }

    // ------------------------------------------------------------------------
    // 4. Apache Avro OCF (.avro — Null & Deflate Data Blocks)
    // ------------------------------------------------------------------------
    {
        std::wstring pA = tmp.Write(L"ev_null.avro", BuildAvroOcfFile(false, 1001, "edge-gw-01", 0.42, 1002, "edge-gw-02", 0.87));
        std::wstring pB = tmp.Write(L"ev_deflate.avro", BuildAvroOcfFile(true, 5001, "core-db-09", 0.15, 5002, "core-db-10", 0.93));
        std::wstring pBad = tmp.Write(L"bad.avro", kGarbage);

        ParsedOfficeDocument dA, dB;
        bool okA = SpreadsheetDocViewer::InspectOfficeFile(pA, dA);
        bool okB = SpreadsheetDocViewer::InspectOfficeFile(pB, dB);
        tc.Record(okA && okB && dA.valid && dB.valid &&
                  SheetContains(dA, L"Avro_Records", L"1001") &&
                  SheetContains(dA, L"Avro_Records", L"edge-gw-01") &&
                  SheetContains(dB, L"Avro_Records", L"5002") &&
                  SheetContains(dB, L"Avro_Records", L"core-db-10") &&
                  HashParsedDoc(dA) != HashParsedDoc(dB) &&
                  InspectFails(pBad),
            L"DocsA.Avro.OcfNullAndDeflateBlocks");
    }

    // ------------------------------------------------------------------------
    // 5. MongoDB BSON (.bson — Nested Subdocument, ObjectId, UTC Datetime)
    // ------------------------------------------------------------------------
    {
        std::wstring pA = tmp.Write(L"doc_a.bson", BuildBsonDoc("auth-api", 8443, 12.5, 0x0A));
        std::wstring pB = tmp.Write(L"doc_b.bson", BuildBsonDoc("billing-worker", 9090, 48.75, 0x0B));
        std::wstring pBad = tmp.Write(L"bad.bson", kGarbage);

        ParsedOfficeDocument dA, dB;
        bool okA = SpreadsheetDocViewer::InspectOfficeFile(pA, dA);
        bool okB = SpreadsheetDocViewer::InspectOfficeFile(pB, dB);
        tc.Record(okA && okB && dA.valid && dB.valid &&
                  DocContains(dA, L"auth-api") && DocContains(dA, L"8443") && DocContains(dA, L"2025-01-15") &&
                  DocContains(dB, L"billing-worker") && DocContains(dB, L"48.75") &&
                  HashParsedDoc(dA) != HashParsedDoc(dB) &&
                  InspectFails(pBad),
            L"DocsA.Bson.RecursiveTypedElements");
    }

    // ------------------------------------------------------------------------
    // 6. NumPy (.npy & .npz) + Jupyter Notebook (.ipynb) + CBOR + MessagePack
    // ------------------------------------------------------------------------
    {
        auto npyA = BuildNpy2D(1.25, 2.5, 3.75, 5.0);
        auto npyB = BuildNpy2D(10.0, 20.0, 30.0, 40.0);
        std::wstring pNpyA = tmp.Write(L"mat_a.npy", npyA);
        std::wstring pNpyB = tmp.Write(L"mat_b.npy", npyB);
        std::wstring pNpyBad = tmp.Write(L"bad.npy", kGarbage);

        ParsedOfficeDocument dA, dB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(pNpyA, dA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(pNpyB, dB) &&
                  dA.valid && dB.valid &&
                  DocContains(dA, L"3.75") && DocContains(dB, L"40") &&
                  HashParsedDoc(dA) != HashParsedDoc(dB) && InspectFails(pNpyBad),
            L"DocsA.NumPy.Npy2DTensors");

        std::wstring pNpzA = tmp.Write(L"arch_a.npz", MakeZipStoreArchive({ { "layer1.npy", npyA } }));
        std::wstring pNpzB = tmp.Write(L"arch_b.npz", MakeZipStoreArchive({ { "layer2.npy", npyB } }));
        std::wstring pNpzBad = tmp.Write(L"bad.npz", kGarbage);
        ParsedOfficeDocument nzA, nzB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(pNpzA, nzA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(pNpzB, nzB) &&
                  nzA.valid && nzB.valid &&
                  DocContains(nzA, L"layer1") && DocContains(nzB, L"layer2") &&
                  HashParsedDoc(nzA) != HashParsedDoc(nzB) && InspectFails(pNpzBad),
            L"DocsA.NumPy.NpzArchive");

        std::wstring nbA = tmp.WriteText(L"nb_a.ipynb",
            R"({"nbformat":4,"nbformat_minor":5,"metadata":{"kernelspec":{"display_name":"Python 3.12"}},"cells":[{"cell_type":"markdown","source":["# Gradient Descent Report"]},{"cell_type":"code","execution_count":1,"source":["loss = 0.0125"],"outputs":[{"text":["converged_ok\n"]}]}]})");
        std::wstring nbB = tmp.WriteText(L"nb_b.ipynb",
            R"({"nbformat":4,"nbformat_minor":5,"metadata":{"kernelspec":{"display_name":"Julia 1.10"}},"cells":[{"cell_type":"markdown","source":["# Spectral FFT Analysis"]},{"cell_type":"code","execution_count":2,"source":["peak_hz = 440.0"],"outputs":[{"text":["harmonic_detected\n"]}]}]})");
        std::wstring nbBad = tmp.Write(L"bad.ipynb", kGarbage);
        ParsedOfficeDocument inA, inB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(nbA, inA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(nbB, inB) &&
                  inA.valid && inB.valid && !inA.isSpreadsheet &&
                  DocContains(inA, L"Gradient Descent Report") && DocContains(inA, L"converged_ok") &&
                  DocContains(inB, L"Spectral FFT Analysis") &&
                  HashParsedDoc(inA) != HashParsedDoc(inB) && InspectFails(nbBad),
            L"DocsA.Jupyter.IpynbStructuredCells");

        std::wstring cbA = tmp.Write(L"a.cbor", BuildCborArray("turbine_1", 3600, "turbine_2", 3650));
        std::wstring cbB = tmp.Write(L"b.cbor", BuildCborArray("pump_west", 1450, "pump_east", 1500));
        std::wstring cbBad = tmp.WriteText(L"bad.cbor", "not valid cbor multi-root ascii");
        ParsedOfficeDocument cdA, cdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(cbA, cdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(cbB, cdB) &&
                  cdA.valid && cdB.valid &&
                  SheetContains(cdA, L"CBOR_Table", L"turbine_1") && SheetContains(cdA, L"CBOR_Table", L"3600") &&
                  SheetContains(cdB, L"CBOR_Table", L"pump_west") &&
                  HashParsedDoc(cdA) != HashParsedDoc(cdB) && InspectFails(cbBad),
            L"DocsA.Cbor.Rfc8949TreeAndTable");

        std::wstring mpA = tmp.Write(L"a.msgpack", BuildMsgPackArray("mic_left", -3.5, "mic_right", -1.25));
        std::wstring mpB = tmp.Write(L"b.msgpack", BuildMsgPackArray("sub_lfe", 6.0, "center_ch", 0.5));
        std::wstring mpBad = tmp.Write(L"bad.msgpack", { 0xC1, 0xFF, 0x00 }); // 0xC1 never-used opcode
        ParsedOfficeDocument mdA, mdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(mpA, mdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(mpB, mdB) &&
                  mdA.valid && mdB.valid &&
                  SheetContains(mdA, L"MsgPack_Table", L"mic_left") && SheetContains(mdA, L"MsgPack_Table", L"-3.5") &&
                  SheetContains(mdB, L"MsgPack_Table", L"sub_lfe") &&
                  HashParsedDoc(mdA) != HashParsedDoc(mdB) && InspectFails(mpBad),
            L"DocsA.MsgPack.BinaryMapArray");
    }

    // ------------------------------------------------------------------------
    // 7. Neural Net & Columnar Formats (.onnx, .pt, .tflite, .arrow, .orc, .h5)
    // ------------------------------------------------------------------------
    {
        std::wstring oxA = tmp.Write(L"net_a.onnx", BuildOnnxModel("ResNetBlock", "Conv", 0.125f, -0.5f));
        std::wstring oxB = tmp.Write(L"net_b.onnx", BuildOnnxModel("TransformerMHA", "MatMul", 1.75f, 2.25f));
        std::wstring oxBad = tmp.Write(L"bad.onnx", kGarbage);
        ParsedOfficeDocument odA, odB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(oxA, odA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(oxB, odB) &&
                  odA.valid && odB.valid &&
                  SheetContains(odA, L"ONNX_Graph_Nodes", L"Conv") &&
                  SheetContains(odA, L"ONNX_Model_And_Tensors", L"0.125") &&
                  SheetContains(odB, L"ONNX_Graph_Nodes", L"MatMul") &&
                  HashParsedDoc(odA) != HashParsedDoc(odB) && InspectFails(oxBad),
            L"DocsA.Onnx.ProtobufGraphAndWeights");

        std::wstring ptA = tmp.Write(L"ckpt_a.pt", BuildPyTorchCheckpoint("encoder.fc1.weight", 0.25f, 0.5f, 0.75f, 1.0f));
        std::wstring ptB = tmp.Write(L"ckpt_b.pt", BuildPyTorchCheckpoint("decoder.proj.bias", -2.0f, -4.0f, 6.0f, 8.0f));
        std::wstring ptBad = tmp.Write(L"bad.pt", kGarbage);
        ParsedOfficeDocument pdA, pdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(ptA, pdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(ptB, pdB) &&
                  pdA.valid && pdB.valid &&
                  SheetContains(pdA, L"PyTorch_StateDict", L"encoder.fc1.weight") &&
                  SheetContains(pdA, L"PyTorch_StateDict", L"0.25") &&
                  SheetContains(pdB, L"PyTorch_StateDict", L"decoder.proj.bias") &&
                  HashParsedDoc(pdA) != HashParsedDoc(pdB) && InspectFails(ptBad),
            L"DocsA.PyTorch.ZipPickleAndStorageWeights");

        std::wstring tfA = tmp.Write(L"mobilenet.tflite", BuildTfLiteModel("conv_stem/weights", 3, 0.5f));   // 3 = CONV_2D
        std::wstring tfB = tmp.Write(L"classifier.tflite", BuildTfLiteModel("dense_head/kernel", 9, 3.25f)); // 9 = FULLY_CONNECTED
        std::wstring tfBad = tmp.Write(L"bad.tflite", kGarbage);
        ParsedOfficeDocument tdA, tdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(tfA, tdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(tfB, tdB) &&
                  tdA.valid && tdB.valid &&
                  SheetContains(tdA, L"TFLite_Tensors", L"conv_stem/weights") &&
                  SheetContains(tdA, L"TFLite_Operators", L"CONV_2D") &&
                  SheetContains(tdB, L"TFLite_Operators", L"FULLY_CONNECTED") &&
                  HashParsedDoc(tdA) != HashParsedDoc(tdB) && InspectFails(tfBad),
            L"DocsA.TfLite.FlatBufferSubgraphAndOps");

        std::wstring arA = tmp.Write(L"batch_a.arrow", BuildArrowIpcFile("user_id", 42, 43, 99.5, 87.25));
        std::wstring arB = tmp.Write(L"batch_b.arrow", BuildArrowIpcFile("order_id", 9001, 9002, 14.75, 29.5));
        std::wstring arBad = tmp.Write(L"bad.arrow", kGarbage);
        ParsedOfficeDocument adA, adB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(arA, adA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(arB, adB) &&
                  adA.valid && adB.valid &&
                  SheetContains(adA, L"Arrow_Schema", L"user_id") &&
                  SheetContains(adA, L"Arrow_Data", L"99.5") &&
                  SheetContains(adB, L"Arrow_Data", L"9002") &&
                  HashParsedDoc(adA) != HashParsedDoc(adB) && InspectFails(arBad),
            L"DocsA.Arrow.IpcSchemaAndRecordBatch");

        std::wstring orA = tmp.Write(L"warehouse_a.orc", BuildOrcFile("tx_amount", 1250, -50, 9800));
        std::wstring orB = tmp.Write(L"warehouse_b.orc", BuildOrcFile("session_ms", 8400, 10, 60000));
        std::wstring orBad = tmp.Write(L"bad.orc", kGarbage);
        ParsedOfficeDocument orcA, orcB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(orA, orcA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(orB, orcB) &&
                  orcA.valid && orcB.valid &&
                  SheetContains(orcA, L"ORC_Schema_And_Stats", L"tx_amount") &&
                  SheetContains(orcA, L"ORC_Schema_And_Stats", L"9800") &&
                  SheetContains(orcB, L"ORC_Schema_And_Stats", L"session_ms") &&
                  HashParsedDoc(orcA) != HashParsedDoc(orcB) && InspectFails(orBad),
            L"DocsA.Orc.ProtobufFooterAndStripes");

        std::wstring h5A = tmp.Write(L"sim_a.h5", BuildHdf5File("velocity", 1.5, 2.5, 3.5, 4.5));
        std::wstring h5B = tmp.Write(L"sim_b.h5", BuildHdf5File("pressure", 101.25, 102.5, 103.75, 105.0));
        std::wstring h5Bad = tmp.Write(L"bad.h5", kGarbage);
        ParsedOfficeDocument hdA, hdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(h5A, hdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(h5B, hdB) &&
                  hdA.valid && hdB.valid &&
                  SheetContains(hdA, L"HDF5_Datasets", L"/velocity") &&
                  SheetContains(hdA, L"velocity", L"3.5") &&
                  SheetContains(hdB, L"pressure", L"105") &&
                  HashParsedDoc(hdA) != HashParsedDoc(hdB) && InspectFails(h5Bad),
            L"DocsA.Hdf5.SuperblockObjectHeadersAndDataset");
    }

    // ------------------------------------------------------------------------
    // 8. Scientific & Statistical Formats (.mat, .sav, .dta, .sas7bdat, .rds, .duckdb)
    // ------------------------------------------------------------------------
    {
        std::wstring mA = tmp.Write(L"ws_a.mat", BuildMatlabMat5("stiffness", 11.0, 21.0, 12.0, 22.0));
        std::wstring mB = tmp.Write(L"ws_b.mat", BuildMatlabMat5("damping", 0.05, 0.15, 0.25, 0.35));
        std::wstring mBad = tmp.Write(L"bad.mat", kGarbage);
        ParsedOfficeDocument mdA, mdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(mA, mdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(mB, mdB) &&
                  mdA.valid && mdB.valid &&
                  SheetContains(mdA, L"MAT_Workspace", L"stiffness") &&
                  SheetContains(mdA, L"stiffness", L"21") &&
                  SheetContains(mdB, L"damping", L"0.35") &&
                  HashParsedDoc(mdA) != HashParsedDoc(mdB) && InspectFails(mBad),
            L"DocsA.Matlab.Level5MatrixElements");

        std::wstring svA = tmp.Write(L"survey_a.sav", BuildSpssSav(85, "PRAGUE", 92, "BRNO"));
        std::wstring svB = tmp.Write(L"survey_b.sav", BuildSpssSav(41, "BERLIN", 77, "MUNICH"));
        std::wstring svBad = tmp.Write(L"bad.sav", kGarbage);
        ParsedOfficeDocument sdA, sdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(svA, sdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(svB, sdB) &&
                  sdA.valid && sdB.valid &&
                  SheetContains(sdA, L"SPSS_Data", L"85") && SheetContains(sdA, L"SPSS_Data", L"PRAGUE") &&
                  SheetContains(sdB, L"SPSS_Data", L"MUNICH") &&
                  HashParsedDoc(sdA) != HashParsedDoc(sdB) && InspectFails(svBad),
            L"DocsA.Spss.SavDictionaryAndBytecodeCases");

        std::wstring dtA = tmp.Write(L"panel_a.dta", BuildStataDta118(315.5, "CZE", 4250.25, "DEU"));
        std::wstring dtB = tmp.Write(L"panel_b.dta", BuildStataDta118(890.75, "POL", 512.0, "AUT"));
        std::wstring dtBad = tmp.Write(L"bad.dta", kGarbage);
        ParsedOfficeDocument ddA, ddB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(dtA, ddA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(dtB, ddB) &&
                  ddA.valid && ddB.valid &&
                  SheetContains(ddA, L"Stata_Data", L"315.5") && SheetContains(ddA, L"Stata_Data", L"CZE") &&
                  SheetContains(ddB, L"Stata_Data", L"POL") &&
                  HashParsedDoc(ddA) != HashParsedDoc(ddB) && InspectFails(dtBad),
            L"DocsA.Stata.Dta118TypedObservations");

        std::wstring saA = tmp.Write(L"trial_a.sas7bdat", BuildSas7bdat("ONCOLOGY", 25.0, "ACTIVE", 0.0, "PLACEBO"));
        std::wstring saB = tmp.Write(L"trial_b.sas7bdat", BuildSas7bdat("CARDIO", 150.5, "HIGHDOSE", 75.25, "LOWDOSE"));
        std::wstring saBad = tmp.Write(L"bad.sas7bdat", kGarbage);
        ParsedOfficeDocument sasA, sasB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(saA, sasA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(saB, sasB) &&
                  sasA.valid && sasB.valid &&
                  SheetContains(sasA, L"ONCOLOGY", L"25") && SheetContains(sasA, L"ONCOLOGY", L"PLACEBO") &&
                  SheetContains(sasB, L"CARDIO", L"HIGHDOSE") &&
                  HashParsedDoc(sasA) != HashParsedDoc(sasB) && InspectFails(saBad),
            L"DocsA.Sas7bdat.SubheadersAndMixPageRows");

        std::wstring rdA = tmp.Write(L"frame_raw.rds", BuildRdsDataFrame(false, "t_stat", 2.45, -1.96, "group", "ctrl", "treat"));
        std::wstring rdB = tmp.Write(L"frame_gz.rds", BuildRdsDataFrame(true, "p_val", 0.001, 0.042, "gene", "BRCA1", "TP53"));
        std::wstring rdBad = tmp.Write(L"bad.rds", kGarbage);
        ParsedOfficeDocument rdsA, rdsB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(rdA, rdsA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(rdB, rdsB) &&
                  rdsA.valid && rdsB.valid &&
                  SheetContains(rdsA, L"RDS_Data", L"t_stat") && SheetContains(rdsA, L"RDS_Data", L"treat") &&
                  SheetContains(rdsB, L"RDS_Data", L"BRCA1") && SheetContains(rdsB, L"RDS_Data", L"0.042") &&
                  HashParsedDoc(rdsA) != HashParsedDoc(rdsB) && InspectFails(rdBad),
            L"DocsA.Rds.XdrDataFrameRawAndGzip");

        std::wstring dkA = tmp.Write(L"wh_a.duckdb", BuildDuckDbFile(64, 7, "events", "event_id", 1001, 1002, "latency_ms", 4.25, 19.75, "region", "eu-central", "us-east"));
        std::wstring dkB = tmp.Write(L"wh_b.duckdb", BuildDuckDbFile(65, 19, "ledger", "acct_id", 9001, 9002, "balance", 12500.5, -340.25, "currency", "CHF", "USD"));
        // Anti-cheat negative test: "DUCK" magic + DatabaseHeader + raw ASCII "CREATE TABLE" without binary MetaBlock catalog must fail!
        std::vector<uint8_t> dkRawAscii(16384, 0);
        std::memcpy(dkRawAscii.data() + 8, "DUCK", 4);
        PutU64LE(dkRawAscii.data() + 12, 64);
        PutU64LE(dkRawAscii.data() + 8192 + 8, 5);
        PutU64LE(dkRawAscii.data() + 8192 + 16, 0);
        PutU64LE(dkRawAscii.data() + 8192 + 32, 1);
        PutU64LE(dkRawAscii.data() + 8192 + 40, 4096);
        const char* fakeSql = "CREATE TABLE fake_scraped_table (id INTEGER, secret VARCHAR);";
        std::memcpy(dkRawAscii.data() + 12304, fakeSql, std::strlen(fakeSql));
        std::wstring dkCheat = tmp.Write(L"cheat_ascii.duckdb", dkRawAscii);
        std::wstring dkBad = tmp.Write(L"bad.duckdb", kGarbage);

        ParsedOfficeDocument dkdA, dkdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(dkA, dkdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(dkB, dkdB) &&
                  dkdA.valid && dkdB.valid &&
                  SheetContains(dkdA, L"DuckDB_Header_And_Catalog", L"main.events") &&
                  SheetContains(dkdA, L"events", L"1001") &&
                  SheetContains(dkdA, L"events", L"19.75") &&
                  SheetContains(dkdA, L"events", L"eu-central") &&
                  SheetContains(dkdB, L"DuckDB_Header_And_Catalog", L"19") &&
                  SheetContains(dkdB, L"DuckDB_Header_And_Catalog", L"main.ledger") &&
                  SheetContains(dkdB, L"ledger", L"9001") &&
                  SheetContains(dkdB, L"ledger", L"12500.5") &&
                  SheetContains(dkdB, L"ledger", L"CHF") &&
                  HashParsedDoc(dkdA) != HashParsedDoc(dkdB) &&
                  InspectFails(dkCheat) && InspectFails(dkBad),
            L"DocsA.DuckDb.MainHeaderAndCatalogDdl");
    }

    // ------------------------------------------------------------------------
    // 9. Geo-Science & Telemetry (.shp + .dbf, .fit, .kmz, .nc, .grib2, .nii/.nii.gz)
    // ------------------------------------------------------------------------
    {
        std::wstring shpA = tmp.Write(L"sites_a.shp", BuildShpPointFile(50.0755, 14.4378, 49.1951, 16.6068));
        tmp.Write(L"sites_a.dbf", BuildSiblingDbf("PRAGUE_HQ", "BRNO_NODE"));
        std::wstring shpB = tmp.Write(L"sites_b.shp", BuildShpPointFile(47.3769, 8.5417, 46.2044, 6.1432));
        tmp.Write(L"sites_b.dbf", BuildSiblingDbf("ZURICH_LAB", "GENEVA_CERN"));
        std::wstring shpBad = tmp.Write(L"bad.shp", kGarbage);

        ParsedOfficeDocument shA, shB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(shpA, shA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(shpB, shB) &&
                  shA.valid && shB.valid && shA.hasGeoTrack && shA.geoPoints.size() == 2 &&
                  SheetContains(shA, L"Shape_Features", L"PRAGUE_HQ") &&
                  SheetContains(shB, L"Shape_Features", L"GENEVA_CERN") &&
                  HashParsedDoc(shA) != HashParsedDoc(shB) && InspectFails(shpBad),
            L"DocsA.Shapefile.GeometryAndSiblingDbfJoin");

        std::wstring ftA = tmp.Write(L"run_a.fit", BuildGarminFitFile(50.1000, 14.4000, 148, 50.1050, 14.4100, 162));
        std::wstring ftB = tmp.Write(L"ride_b.fit", BuildGarminFitFile(45.9237, 6.8694, 172, 45.9300, 6.8800, 185));
        std::wstring ftBad = tmp.Write(L"bad.fit", kGarbage);

        ParsedOfficeDocument fdA, fdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(ftA, fdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(ftB, fdB) &&
                  fdA.valid && fdB.valid && fdA.hasGeoTrack && fdA.geoPoints.size() == 2 &&
                  SheetContains(fdA, L"FIT_Track_Records", L"148") &&
                  SheetContains(fdB, L"FIT_Track_Records", L"185") &&
                  HashParsedDoc(fdA) != HashParsedDoc(fdB) && InspectFails(ftBad),
            L"DocsA.GarminFit.BinaryRecordsAndGpsTrack");

        std::string kmlXmlA = R"(<?xml version="1.0"?><kml><Document><Placemark><name>TrailA</name><LineString><coordinates>14.41,50.08,210 14.42,50.09,235</coordinates></LineString></Placemark></Document></kml>)";
        std::string kmlXmlB = R"(<?xml version="1.0"?><kml><Document><Placemark><name>TrailB</name><LineString><coordinates>-122.41,37.77,15 -122.40,37.78,45</coordinates></LineString></Placemark></Document></kml>)";
        std::wstring kmzA = tmp.Write(L"track_a.kmz", MakeZipStoreArchive({ { "doc.kml", std::vector<uint8_t>(kmlXmlA.begin(), kmlXmlA.end()) } }));
        std::wstring kmzB = tmp.Write(L"track_b.kmz", MakeZipStoreArchive({ { "doc.kml", std::vector<uint8_t>(kmlXmlB.begin(), kmlXmlB.end()) } }));
        std::wstring kmzBad = tmp.Write(L"bad.kmz", kGarbage);
        ParsedOfficeDocument kdA, kdB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(kmzA, kdA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(kmzB, kdB) &&
                  kdA.valid && kdB.valid && kdA.hasGeoTrack && kdB.hasGeoTrack &&
                  HashParsedDoc(kdA) != HashParsedDoc(kdB) && InspectFails(kmzBad),
            L"DocsA.Kmz.ZippedKmlTrack");

        std::wstring ncA = tmp.Write(L"ocean_a.nc", BuildNetCdfFile(35.5f, -40.0f, 18.25f, 36.0f, -39.5f, 19.5f));
        std::wstring ncB = tmp.Write(L"polar_b.nc", BuildNetCdfFile(78.0f, 15.0f, -6.5f, 78.5f, 16.0f, -8.25f));
        std::wstring ncBad = tmp.Write(L"bad.nc", kGarbage);
        ParsedOfficeDocument ndA, ndB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(ncA, ndA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(ncB, ndB) &&
                  ndA.valid && ndB.valid && ndA.hasGeoTrack && ndA.geoPoints.size() == 2 &&
                  SheetContains(ndA, L"NetCDF_Data_Preview", L"18.25") &&
                  SheetContains(ndB, L"NetCDF_Data_Preview", L"-8.25") &&
                  HashParsedDoc(ndA) != HashParsedDoc(ndB) && InspectFails(ncBad),
            L"DocsA.NetCdf.ClassicVariablesAndGeoGrid");

        std::wstring grA = tmp.Write(L"fcst_a.grib2", BuildGrib2File(270.0f, 3, 5, 7, 9));     // 273..279 K
        std::wstring grB = tmp.Write(L"fcst_b.grib2", BuildGrib2File(290.0f, 2, 4, 8, 10));    // 292..300 K
        std::wstring grBad = tmp.Write(L"bad.grib2", kGarbage);
        ParsedOfficeDocument gribA, gribB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(grA, gribA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(grB, gribB) &&
                  gribA.valid && gribB.valid &&
                  SheetContains(gribA, L"GRIB_Messages", L"Temperature (K)") &&
                  SheetContains(gribA, L"GRIB_Messages", L"273") &&
                  SheetContains(gribB, L"GRIB_Messages", L"300") &&
                  HashParsedDoc(gribA) != HashParsedDoc(gribB) && InspectFails(grBad),
            L"DocsA.Grib2.SimplePackingGridUnpack");

        std::wstring niA = tmp.Write(L"brain_t1.nii", BuildNifti1Volume(false, "T1_MPRAGE_ISO", 10.0f));
        std::wstring niB = tmp.Write(L"brain_flair.nii.gz", BuildNifti1Volume(true, "T2_FLAIR_AXIAL", 200.0f));
        std::wstring niBad = tmp.Write(L"bad.nii.gz", kGarbage);
        ParsedOfficeDocument niiA, niiB;
        tc.Record(SpreadsheetDocViewer::InspectOfficeFile(niA, niiA) &&
                  SpreadsheetDocViewer::InspectOfficeFile(niB, niiB) &&
                  niiA.valid && niiB.valid &&
                  SheetContains(niiA, L"NIfTI_Header_And_Stats", L"T1_MPRAGE_ISO") &&
                  SheetContains(niiA, L"NIfTI_Header_And_Stats", L"57") && // max = 10 + 47 = 57
                  SheetContains(niiB, L"NIfTI_Header_And_Stats", L"GZip Compressed") &&
                  SheetContains(niiB, L"NIfTI_Header_And_Stats", L"T2_FLAIR_AXIAL") &&
                  !SheetContains(niiA, L"NIfTI_Center_Slice", L"NaN") &&
                  HashParsedDoc(niiA) != HashParsedDoc(niiB) && InspectFails(niBad),
            L"DocsA.Nifti.RawAndGzipVoxelVolumes");
    }

    // ------------------------------------------------------------------------
    // 10. OpenDocument Text (.odt) vs Word (.docx), Access (.accdb/.mdb), MBTiles (.mbtiles)
    // ------------------------------------------------------------------------
    {
        // A. OpenDocument Text (.odt) — Headings, <text:s text:c="N"/>, Lists, Tables, Numeric XML Entities
        const std::string odtMime = "application/vnd.oasis.opendocument.text";
        const std::string odtXmlA =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0">)"
            R"(<office:body><office:text>)"
            R"(<text:h text:outline-level="1">Odyssey Flight Plan</text:h>)"
            R"(<text:p>Telemetry:<text:s text:c="3"/>Nominal &amp; &#x26A1; Ready</text:p>)"
            R"(<text:list><text:list-item><text:p>Gimbal Actuator Verified</text:p></text:list-item></text:list>)"
            R"(<table:table><table:table-row>)"
            R"(<table:table-cell><text:p>Orbit</text:p></table:table-cell>)"
            R"(<table:table-cell><text:p>408 km</text:p></table:table-cell>)"
            R"(</table:table-row></table:table>)"
            R"(</office:text></office:body></office:document-content>)";
        const std::string odtXmlB =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0">)"
            R"(<office:body><office:text>)"
            R"(<text:h text:outline-level="2">Bathymetry Expedition</text:h>)"
            R"(<text:p>Depth:<text:tab/>&#916; = 10928 m</text:p>)"
            R"(<table:table><table:table-row>)"
            R"(<table:table-cell><text:p>Trench</text:p></table:table-cell>)"
            R"(<table:table-cell><text:p>Mariana</text:p></table:table-cell>)"
            R"(</table:table-row></table:table>)"
            R"(</office:text></office:body></office:document-content>)";

        std::wstring odtA = tmp.Write(L"plan_a.odt", MakeZipStoreArchive({
            { "mimetype", std::vector<uint8_t>(odtMime.begin(), odtMime.end()) },
            { "content.xml", std::vector<uint8_t>(odtXmlA.begin(), odtXmlA.end()) }
        }));
        std::wstring odtB = tmp.Write(L"dive_b.odt", MakeZipStoreArchive({
            { "mimetype", std::vector<uint8_t>(odtMime.begin(), odtMime.end()) },
            { "content.xml", std::vector<uint8_t>(odtXmlB.begin(), odtXmlB.end()) }
        }));
        std::wstring odtBad = tmp.Write(L"bad.odt", kGarbage);
        std::wstring odtWrongZip = tmp.Write(L"unrelated.odt", MakeZipStoreArchive({
            { "readme.txt", { 'n', 'o', 't', '_', 'o', 'd', 'f' } }
        }));

        ParsedOfficeDocument odDocA, odDocB;
        bool odtOk = SpreadsheetDocViewer::InspectOfficeFile(odtA, odDocA) &&
                     SpreadsheetDocViewer::InspectOfficeFile(odtB, odDocB) &&
                     odDocA.valid && odDocB.valid &&
                     odDocA.formatTitle == L"OpenDocument Text (.odt)" &&
                     odDocB.formatTitle == L"OpenDocument Text (.odt)" &&
                     !DocContains(odDocA, L"Empty Word document") &&
                     !DocContains(odDocA, L"Pusty dokument") &&
                     odDocA.paragraphs.size() == 4 &&
                     odDocA.paragraphs[0].headingLevel == 1 &&
                     odDocA.paragraphs[0].text == L"Odyssey Flight Plan" &&
                     odDocA.paragraphs[1].text == L"Telemetry:   Nominal & \x26A1 Ready" &&
                     odDocA.paragraphs[2].isBullet &&
                     odDocA.paragraphs[2].text == L"Gimbal Actuator Verified" &&
                     odDocA.paragraphs[3].text == L"Orbit | 408 km" &&
                     DocContains(odDocB, L"Bathymetry Expedition") &&
                     DocContains(odDocB, L"Trench | Mariana") &&
                     HashParsedDoc(odDocA) != HashParsedDoc(odDocB) &&
                     InspectFails(odtBad) && InspectFails(odtWrongZip);
        tc.Record(odtOk, L"DocsA.OdfWriterOdt.ContentXmlParagraphsAndTables");

        std::wstring repoOdt = tdDir + L"\\writer_document.odt";
        if (GetFileAttributesW(repoOdt.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ParsedOfficeDocument repoOdtDoc;
            bool okRepo = SpreadsheetDocViewer::InspectOfficeFile(repoOdt, repoOdtDoc) &&
                          repoOdtDoc.valid &&
                          repoOdtDoc.formatTitle == L"OpenDocument Text (.odt)" &&
                          !DocContains(repoOdtDoc, L"Empty Word document") &&
                          !repoOdtDoc.paragraphs.empty();
            tc.Record(okRepo, L"DocsA.TestData.WriterDocumentOdt");
        }

        // B. Microsoft Access (.accdb Jet4/ACE & .mdb Jet3.5) — MSysObjects + TDEF + Typed DATA Rows + Null Bitmap
        std::wstring accA = tmp.Write(L"plant_a.accdb",
            BuildJet4AceAccdb("Sensors", 501, 73.25, "BoilerRoom_North", 9100, 502, -14.5, "ChillerBay_West"));
        std::wstring accB = tmp.Write(L"warehouse_b.accdb",
            BuildJet4AceAccdb("Inventory", 801, 1499.75, "Aisle_Alpha", 4200, 802, 24.5, "LoadingDock_South"));
        std::wstring mdbC = tmp.Write(L"legacy97.mdb",
            BuildJet35Mdb("LegacyParts", 309, "brass_valve_97"));

        // 1-page header-only .accdb with zero TDEF pages must strictly fail
        std::vector<uint8_t> hdrOnlyAccdb(4096, 0);
        hdrOnlyAccdb[1] = 0x01u;
        std::memcpy(hdrOnlyAccdb.data() + 4, "Standard ACE DB", 15);
        PutU32LE(hdrOnlyAccdb.data() + 0x14, 2u);
        std::wstring accHdrOnly = tmp.Write(L"header_only.accdb", hdrOnlyAccdb);
        std::wstring accBad = tmp.Write(L"bad.accdb", kGarbage);

        ParsedOfficeDocument acDocA, acDocB, mdbDocC;
        bool accOk = SpreadsheetDocViewer::InspectOfficeFile(accA, acDocA) &&
                     SpreadsheetDocViewer::InspectOfficeFile(accB, acDocB) &&
                     SpreadsheetDocViewer::InspectOfficeFile(mdbC, mdbDocC) &&
                     acDocA.valid && acDocB.valid && mdbDocC.valid &&
                     SheetContains(acDocA, L"Sensors", L"501") &&
                     SheetContains(acDocA, L"Sensors", L"73.25") &&
                     SheetContains(acDocA, L"Sensors", L"TRUE") &&
                     SheetContains(acDocA, L"Sensors", L"FALSE") &&
                     SheetContains(acDocA, L"Sensors", L"BoilerRoom_North") &&
                     SheetContains(acDocA, L"Sensors", L"9100") &&
                     SheetContains(acDocA, L"Sensors", L"NULL") &&
                     SheetContains(acDocB, L"Inventory", L"Aisle_Alpha") &&
                     SheetContains(acDocB, L"Inventory", L"1499.75") &&
                     SheetContains(mdbDocC, L"LegacyParts", L"309") &&
                     SheetContains(mdbDocC, L"LegacyParts", L"brass_valve_97") &&
                     HashParsedDoc(acDocA) != HashParsedDoc(acDocB) &&
                     InspectFails(accHdrOnly) && InspectFails(accBad);
        tc.Record(accOk, L"DocsA.AccessDb.Jet4AceAndJet3TdefDataPages");

        // C. Mapbox MBTiles (.mbtiles) — Real SQLite3 B-Tree metadata + tiles Tables + TMS Geo Bounds
        std::wstring mbA = tmp.Write(L"prague.mbtiles",
            BuildMbTilesDb("Prague_Basemap", "pbf", "14.22,49.94,14.70,50.18", "14.42,50.08,11", 8, 138, 168, 8, 138, 169));
        std::wstring mbB = tmp.Write(L"tokyo_tms_derived.mbtiles",
            BuildMbTilesDb("Tokyo_Harbor_Raster", "png", "", "", 8, 227, 155, 8, 227, 156));
        std::wstring mbNonMbtiles = tmp.Write(L"not_map.mbtiles",
            BuildSqliteBtreeDb("thermocouples", "probe_1", 21.0, "probe_2", 22.0));
        std::wstring mbBad = tmp.Write(L"bad.mbtiles", kGarbage);

        ParsedOfficeDocument mbDocA, mbDocB;
        bool mbOk = SpreadsheetDocViewer::InspectOfficeFile(mbA, mbDocA) &&
                    SpreadsheetDocViewer::InspectOfficeFile(mbB, mbDocB) &&
                    mbDocA.valid && mbDocB.valid &&
                    mbDocA.hasGeoTrack && mbDocB.hasGeoTrack &&
                    !mbDocA.geoPoints.empty() && !mbDocB.geoPoints.empty() &&
                    std::fabs(mbDocA.geoPoints[0].lat - 49.94) < 1e-3 &&
                    std::fabs(mbDocA.geoPoints[0].lon - 14.22) < 1e-3 &&
                    mbDocB.geoPoints[0].lon > 130.0 && // Derived from TMS tile x=227 at z=8 (~139.2E Tokyo)
                    SheetContains(mbDocA, L"metadata", L"Prague_Basemap") &&
                    SheetContains(mbDocA, L"tiles", L"138") &&
                    SheetContains(mbDocB, L"metadata", L"Tokyo_Harbor_Raster") &&
                    SheetContains(mbDocB, L"tiles", L"227") &&
                    HashParsedDoc(mbDocA) != HashParsedDoc(mbDocB) &&
                    InspectFails(mbNonMbtiles) && InspectFails(mbBad);
        tc.Record(mbOk, L"DocsA.MbTiles.SqliteBTreeMetadataAndTiles");
    }

    // ------------------------------------------------------------------------
    // 14. Group D Batch 2: ECMA-376 Excel (.xlsx) & OASIS Calc (.ods)
    // ------------------------------------------------------------------------
    {
        auto strBytes = [](std::string_view sv) -> std::vector<uint8_t> {
            return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(sv.data()),
                                        reinterpret_cast<const uint8_t*>(sv.data()) + sv.size());
        };

        // A. ECMA-376 OpenXML (.xlsx): custom workbook.xml.rels paths, <rPh> furigana stripping,
        //    styles.xml built-in + custom Date / Percent formats, 1900 vs date1904="1" serials,
        //    t="b", t="str", t="e", t="d", t="inlineStr" multi-<r><t>, formula <f> fallback, sparse rows/cols.
        const std::string wbRelsA =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rIdRev\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/revenue_q4.xml\"/>"
            "<Relationship Id=\"rIdOps\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"/xl/worksheets/ops_2026.xml\"/>"
            "<Relationship Id=\"rIdSS\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>"
            "<Relationship Id=\"rIdSt\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
            "</Relationships>";

        const std::string wbXmlA =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<workbookPr date1904=\"0\"/>"
            "<sheets>"
            "<sheet name=\"Q4 &amp; Revenue\" sheetId=\"1\" r:id=\"rIdRev\"/>"
            "<x:sheet r:id=\"rIdOps\" sheetId=\"2\" name=\"Ops_2026\"/>"
            "</sheets>"
            "</workbook>";

        // Shared string 0 has East Asian <rPh> furigana ("トウキョウ" = \xE3\x83\x88\xE3\x82\xA6\xE3\x82\xAD\xE3\x83\xA7\xE3\x82\xA6)
        // which MUST be stripped before concatenating <t> runs!
        const std::string sharedStringsA =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"2\" uniqueCount=\"2\">"
            "<si><t>Tokyo </t><t>HQ</t><rPh sb=\"0\" eb=\"5\"><t>\xE3\x83\x88\xE3\x82\xA6\xE3\x82\xAD\xE3\x83\xA7\xE3\x82\xA6</t></rPh></si>"
            "<si><r><t>Ops&#10;Ready &amp; &#x2605;</t></r></si>"
            "</sst>";

        const std::string stylesXmlA =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<numFmts count=\"2\">"
            "<numFmt numFmtId=\"164\" formatCode=\"[$-409]yyyy\\-mm\\-dd;@\"/>"
            "<numFmt numFmtId=\"165\" formatCode=\"0.0%\"/>"
            "</numFmts>"
            "<cellXfs count=\"5\">"
            "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>"
            "<xf numFmtId=\"14\" fontId=\"0\" fillId=\"0\" borderId=\"0\" applyNumberFormat=\"1\"/>"
            "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" applyNumberFormat=\"1\"/>"
            "<xf numFmtId=\"10\" fontId=\"0\" fillId=\"0\" borderId=\"0\" applyNumberFormat=\"1\"/>"
            "<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" applyNumberFormat=\"1\"/>"
            "</cellXfs>"
            "</styleSheet>";

        // 45658 in 1900 date system == 2025-01-01; 60 == 1900-02-29 (Lotus leap-day compatibility)
        const std::string sheetRevXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<sheetData>"
            "<row r=\"1\">"
            "<c r=\"A1\" t=\"s\"><v>0</v></c>"
            "<c r=\"B1\" s=\"1\"><v>45658</v></c>"
            "<c r=\"C1\" s=\"2\"><v>60</v></c>"
            "<c r=\"D1\" s=\"3\"><v>0.425</v></c>"
            "<c r=\"E1\" s=\"4\"><v>0.875</v></c>"
            "</row>"
            "<row r=\"4\">"
            "<c r=\"A4\" t=\"b\"><v>1</v></c>"
            "<c r=\"B4\" t=\"str\"><v>CachedFormula_OK</v></c>"
            "<c r=\"C4\" t=\"inlineStr\"><is><r><t>Multi</t></r><r><t>Run_Inline</t></r><rPh><t>FURIGANA_NOPE</t></rPh></is></c>"
            "<c r=\"D4\" t=\"e\"><v>#DIV/0!</v></c>"
            "<c r=\"F4\"><f>SUM(D1:E1)</f></c>"
            "</row>"
            "</sheetData>"
            "</worksheet>";

        const std::string sheetOpsXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<sheetData>"
            "<row r=\"1\">"
            "<c r=\"A1\" t=\"s\"><v>1</v></c>"
            "<c r=\"B1\" t=\"d\"><v>2026-05-20</v></c>"
            "</row>"
            "</sheetData>"
            "</worksheet>";

        std::wstring xlsxPathA = tmp.Write(L"custom_rels_a.xlsx", MakeZipStoreArchive({
            { "xl/_rels/workbook.xml.rels", strBytes(wbRelsA) },
            { "xl/workbook.xml",            strBytes(wbXmlA) },
            { "xl/sharedStrings.xml",       strBytes(sharedStringsA) },
            { "xl/styles.xml",              strBytes(stylesXmlA) },
            { "xl/worksheets/revenue_q4.xml", strBytes(sheetRevXml) },
            { "xl/worksheets/ops_2026.xml",   strBytes(sheetOpsXml) },
        }));

        // Workbook B uses date1904="1" (serial 0 == 1904-01-01, serial 44196 == 2025-01-01) and numFmtId=9 (0%)
        const std::string wbRelsB =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rIdMac\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/mac_1904.xml\"/>"
            "<Relationship Id=\"rIdSt\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
            "</Relationships>";
        const std::string wbXmlB =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<workbookPr date1904=\"1\"/>"
            "<sheets><sheet name=\"Mac1904\" sheetId=\"1\" r:id=\"rIdMac\"/></sheets>"
            "</workbook>";
        const std::string stylesXmlB =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<cellXfs count=\"3\">"
            "<xf numFmtId=\"0\"/><xf numFmtId=\"14\" applyNumberFormat=\"1\"/><xf numFmtId=\"9\" applyNumberFormat=\"1\"/>"
            "</cellXfs></styleSheet>";
        const std::string sheetMacXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<sheetData><row r=\"1\">"
            "<c r=\"A1\" s=\"1\"><v>0</v></c>"
            "<c r=\"B1\" s=\"1\"><v>44196</v></c>"
            "<c r=\"C1\" t=\"b\"><v>0</v></c>"
            "<c r=\"D1\" s=\"2\"><v>0.75</v></c>"
            "</row></sheetData></worksheet>";

        std::wstring xlsxPathB = tmp.Write(L"date1904_b.xlsx", MakeZipStoreArchive({
            { "xl/_rels/workbook.xml.rels", strBytes(wbRelsB) },
            { "xl/workbook.xml",            strBytes(wbXmlB) },
            { "xl/styles.xml",              strBytes(stylesXmlB) },
            { "xl/worksheets/mac_1904.xml", strBytes(sheetMacXml) },
        }));

        std::wstring xlsxBad = tmp.Write(L"bad.xlsx", kGarbage);
        std::wstring xlsxWrongZip = tmp.Write(L"unrelated.xlsx", MakeZipStoreArchive({
            { "readme.txt", strBytes("Not an OpenXML workbook") }
        }));

        ParsedOfficeDocument xDocA, xDocB;
        bool xlsxOk = SpreadsheetDocViewer::InspectOfficeFile(xlsxPathA, xDocA) &&
                      SpreadsheetDocViewer::InspectOfficeFile(xlsxPathB, xDocB) &&
                      xDocA.valid && xDocB.valid &&
                      xDocA.sheets.size() == 2 &&
                      xDocA.sheets[0].name == L"Q4 & Revenue" &&
                      xDocA.sheets[1].name == L"Ops_2026" &&
                      xDocA.sheets[0].rows.size() == 4 && // row 1 + 2 gap rows (r=2,3) + row 4
                      xDocA.sheets[0].rows[0].size() == 5 &&
                      xDocA.sheets[0].rows[0][0] == L"Tokyo HQ" &&
                      !DocContains(xDocA, L"\x30C8\x30A6\x30AD\x30E7\x30A6") &&
                      !DocContains(xDocA, L"FURIGANA_NOPE") &&
                      xDocA.sheets[0].rows[0][1] == L"2025-01-01" &&
                      xDocA.sheets[0].rows[0][2] == L"1900-02-29" &&
                      xDocA.sheets[0].rows[0][3] == L"42.50%" &&
                      xDocA.sheets[0].rows[0][4] == L"87.5%" &&
                      xDocA.sheets[0].rows[3].size() == 6 &&
                      xDocA.sheets[0].rows[3][0] == L"TRUE" &&
                      xDocA.sheets[0].rows[3][1] == L"CachedFormula_OK" &&
                      xDocA.sheets[0].rows[3][2] == L"MultiRun_Inline" &&
                      xDocA.sheets[0].rows[3][3] == L"#DIV/0!" &&
                      xDocA.sheets[0].rows[3][4].empty() &&
                      xDocA.sheets[0].rows[3][5] == L"=SUM(D1:E1)" &&
                      SheetContains(xDocA, L"Ops_2026", L"Ops\nReady & \x2605") &&
                      SheetContains(xDocA, L"Ops_2026", L"2026-05-20") &&
                      xDocB.sheets.size() == 1 &&
                      xDocB.sheets[0].rows[0][0] == L"1904-01-01" &&
                      xDocB.sheets[0].rows[0][1] == L"2025-01-01" &&
                      xDocB.sheets[0].rows[0][2] == L"FALSE" &&
                      xDocB.sheets[0].rows[0][3] == L"75%" &&
                      HashParsedDoc(xDocA) != HashParsedDoc(xDocB) &&
                      InspectFails(xlsxBad) && InspectFails(xlsxWrongZip);
        tc.Record(xlsxOk, L"DocsA.ExcelXlsx.RelsFuriganaStyles1904AndCellTypes");

        // B. OASIS OpenDocument Spreadsheet (.ods): 1024 trailing empty column guard, 1048575 trailing empty row guard,
        //    number-columns-repeated gap & value repeat, covered-table-cell alignment, <text:s text:c="3"/>, multi-<text:p>,
        //    office:value / office:boolean-value fallbacks, and corrupt/wrong-mimetype ZIP rejection.
        const std::string odsMime = "application/vnd.oasis.opendocument.spreadsheet";
        const std::string odsXmlA =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\">"
            "<office:body><office:spreadsheet>"
            "<table:table table:name=\"Calc_Matrix\">"
            "<table:table-row>"
            "<table:table-cell office:value-type=\"string\"><text:p>Item<text:s text:c=\"3\"/>Code</text:p></table:table-cell>"
            "<table:covered-table-cell table:number-columns-repeated=\"2\"/>"
            "<table:table-cell office:value-type=\"string\"><text:p>Alpha</text:p><text:p>Beta</text:p></table:table-cell>"
            "<table:table-cell table:number-columns-repeated=\"1024\"/>"
            "</table:table-row>"
            "<table:table-row table:number-rows-repeated=\"2\">"
            "<table:table-cell table:number-columns-repeated=\"1024\"/>"
            "</table:table-row>"
            "<table:table-row>"
            "<table:table-cell table:number-columns-repeated=\"2\" office:value-type=\"string\"><text:p>REP</text:p></table:table-cell>"
            "<table:table-cell table:number-columns-repeated=\"2\"/>"
            "<table:table-cell office:value-type=\"float\" office:value=\"98.75\"/>"
            "<table:table-cell office:value-type=\"boolean\" office:boolean-value=\"true\"/>"
            "<table:table-cell table:number-columns-repeated=\"1024\"/>"
            "</table:table-row>"
            "<table:table-row table:number-rows-repeated=\"1048572\">"
            "<table:table-cell table:number-columns-repeated=\"1024\"/>"
            "</table:table-row>"
            "</table:table>"
            "</office:spreadsheet></office:body></office:document-content>";

        const std::string odsXmlB =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\">"
            "<office:body><office:spreadsheet>"
            "<table:table table:name=\"Lab_Summary\">"
            "<table:table-row>"
            "<table:table-cell office:value-type=\"date\" office:date-value=\"2026-09-01\"/>"
            "<table:table-cell office:value-type=\"string\"><text:p>Calibrated</text:p></table:table-cell>"
            "<table:table-cell table:number-columns-repeated=\"1024\"/>"
            "</table:table-row>"
            "<table:table-row table:number-rows-repeated=\"1048575\"/>"
            "</table:table>"
            "</office:spreadsheet></office:body></office:document-content>";

        std::wstring odsPathA = tmp.Write(L"matrix_a.ods", MakeZipStoreArchive({
            { "mimetype",    strBytes(odsMime) },
            { "content.xml", strBytes(odsXmlA) },
        }));
        std::wstring odsPathB = tmp.Write(L"summary_b.ods", MakeZipStoreArchive({
            { "mimetype",    strBytes(odsMime) },
            { "content.xml", strBytes(odsXmlB) },
        }));
        std::wstring odsBad = tmp.Write(L"bad.ods", kGarbage);
        std::wstring odsWrongMime = tmp.Write(L"wrong_mime.ods", MakeZipStoreArchive({
            { "mimetype",    strBytes("application/epub+zip") },
            { "content.xml", strBytes(odsXmlA) },
        }));

        ParsedOfficeDocument oDocA, oDocB;
        bool odsOk = SpreadsheetDocViewer::InspectOfficeFile(odsPathA, oDocA) &&
                     SpreadsheetDocViewer::InspectOfficeFile(odsPathB, oDocB) &&
                     oDocA.valid && oDocB.valid &&
                     oDocA.formatTitle == L"OpenDocument Spreadsheet (.ods)" &&
                     oDocA.sheets.size() == 1 &&
                     oDocA.sheets[0].name == L"Calc_Matrix" &&
                     oDocA.sheets[0].rows.size() == 4 && // Trailing 1048572 empty rows NOT materialized!
                     oDocA.sheets[0].maxCols == 6 &&     // Trailing 1024 empty cols NOT materialized!
                     oDocA.sheets[0].rows[0].size() == 4 &&
                     oDocA.sheets[0].rows[0][0] == L"Item   Code" &&
                     oDocA.sheets[0].rows[0][1].empty() &&
                     oDocA.sheets[0].rows[0][2].empty() &&
                     oDocA.sheets[0].rows[0][3] == L"Alpha\nBeta" &&
                     oDocA.sheets[0].rows[1].empty() &&
                     oDocA.sheets[0].rows[2].empty() &&
                     oDocA.sheets[0].rows[3].size() == 6 &&
                     oDocA.sheets[0].rows[3][0] == L"REP" &&
                     oDocA.sheets[0].rows[3][1] == L"REP" &&
                     oDocA.sheets[0].rows[3][2].empty() &&
                     oDocA.sheets[0].rows[3][3].empty() &&
                     oDocA.sheets[0].rows[3][4] == L"98.75" &&
                     oDocA.sheets[0].rows[3][5] == L"TRUE" &&
                     SheetContains(oDocB, L"Lab_Summary", L"2026-09-01") &&
                     SheetContains(oDocB, L"Lab_Summary", L"Calibrated") &&
                     HashParsedDoc(oDocA) != HashParsedDoc(oDocB) &&
                     InspectFails(odsBad) && InspectFails(odsWrongMime);
        tc.Record(odsOk, L"DocsA.CalcOds.TrailingRepeatGuardsCoveredCellsAndRichText");
    }

    // ------------------------------------------------------------------------
    // 15. AI Model Inspector (.onnx, .gguf, .safetensors) & Legacy Office OLE2 (.doc, .dot, .ppt, .pps)
    // ------------------------------------------------------------------------
    {
        // A. AI Model Inspector (FileContentPane::InspectAiModelFile + SpreadsheetDocViewer::InspectOfficeFile)
        // 1) .onnx positive + anti-cheat ASCII-scraper / invalid-wiretype negative tests
        std::wstring onnxPath = tmp.Write(L"stage15_matmul.onnx", BuildOnnxModel("TransformerBlock0", "MatMul", 0.25f, -0.75f));
        std::string fakeOnnxAscii = "Conv MatMul Relu Softmax BatchNormalization TorchExport";
        std::wstring onnxAsciiCheat = tmp.Write(L"ascii_cheat.onnx",
            std::vector<uint8_t>(fakeOnnxAscii.begin(), fakeOnnxAscii.end()));
        std::vector<uint8_t> onnxBadWire = { 0x0E, 0x01, 0x0F, 0xFF, 0x3E, 0x12 }; // illegal wire types 6 & 7
        std::wstring onnxWireBadPath = tmp.Write(L"bad_wire.onnx", onnxBadWire);

        auto onnxInfo = FileContentPane::InspectAiModelFile(onnxPath, L".onnx");
        auto onnxCheatInfo = FileContentPane::InspectAiModelFile(onnxAsciiCheat, L".onnx");
        auto onnxWireBadInfo = FileContentPane::InspectAiModelFile(onnxWireBadPath, L".onnx");

        // 2) .gguf v3 with STRING, UINT32, FLOAT32, BOOL, recursive ARRAY (tokenizer) + quantized Q4_K & F32 tensor table
        auto buildGgufV3 = []() {
            std::vector<uint8_t> g;
            auto putGgufStr = [&](std::string_view s) {
                AppendU64LE(g, s.size());
                AppendStr(g, s);
            };
            AppendStr(g, "GGUF");
            AppendU32LE(g, 3); // v3
            AppendU64LE(g, 2); // 2 tensors
            AppendU64LE(g, 6); // 6 KV entries

            // KV 1: general.architecture = "llama" (STRING=8)
            putGgufStr("general.architecture"); AppendU32LE(g, 8); putGgufStr("llama");
            // KV 2: general.name = "TinyLlama-Stage15" (STRING=8)
            putGgufStr("general.name"); AppendU32LE(g, 8); putGgufStr("TinyLlama-Stage15");
            // KV 3: llama.context_length = 8192 (UINT32=4)
            putGgufStr("llama.context_length"); AppendU32LE(g, 4); AppendU32LE(g, 8192);
            // KV 4: llama.rope.freq_base = 10000.0f (FLOAT32=6)
            putGgufStr("llama.rope.freq_base"); AppendU32LE(g, 6); AppendF32LE(g, 10000.0f);
            // KV 5: tokenizer.ggml.add_bos_token = true (BOOL=7)
            putGgufStr("tokenizer.ggml.add_bos_token"); AppendU32LE(g, 7); AppendU8(g, 1);
            // KV 6: tokenizer.ggml.tokens = ["<unk>", "<s>", "</s>", "hello"] (ARRAY=9 of STRING=8)
            putGgufStr("tokenizer.ggml.tokens"); AppendU32LE(g, 9); AppendU32LE(g, 8); AppendU64LE(g, 4);
            putGgufStr("<unk>"); putGgufStr("<s>"); putGgufStr("</s>"); putGgufStr("hello");

            // Tensor 0: blk.0.attn_q.weight, 2D [64, 64] = 4096 params, Q4_K (12) -> 4096/256 * 144 = 2304 B
            putGgufStr("blk.0.attn_q.weight");
            AppendU32LE(g, 2);
            AppendU64LE(g, 64); AppendU64LE(g, 64);
            AppendU32LE(g, 12); // Q4_K
            AppendU64LE(g, 0);

            // Tensor 1: output_norm.weight, 1D [64] = 64 params, F32 (0) -> 256 B
            putGgufStr("output_norm.weight");
            AppendU32LE(g, 1);
            AppendU64LE(g, 64);
            AppendU32LE(g, 0); // F32
            AppendU64LE(g, 2304);

            while ((g.size() % 32u) != 0) AppendU8(g, 0);
            g.insert(g.end(), 2304 + 256, 0x3Cu);
            return g;
        };
        std::wstring ggufPath = tmp.Write(L"tinyllama_q4k.gguf", buildGgufV3());
        // Truncated/scraped GGUF cheat: "GGUF" + v3 + claims 4 KVs + raw "llama" string without valid KV table
        std::vector<uint8_t> ggufCheat;
        AppendStr(ggufCheat, "GGUF"); AppendU32LE(ggufCheat, 3); AppendU64LE(ggufCheat, 1); AppendU64LE(ggufCheat, 4);
        AppendStr(ggufCheat, "llama\0qwen2\0mistral\0");
        std::wstring ggufCheatPath = tmp.Write(L"cheat_scan.gguf", ggufCheat);

        auto ggufInfo = FileContentPane::InspectAiModelFile(ggufPath, L".gguf");
        auto ggufCheatInfo = FileContentPane::InspectAiModelFile(ggufCheatPath, L".gguf");
        ParsedOfficeDocument ggufDoc;
        bool ggufOfficeOk = SpreadsheetDocViewer::InspectOfficeFile(ggufPath, ggufDoc);

        // 3) .safetensors positive + forged out-of-bounds data_offsets negative test
        auto buildSafeTensors = [](uint64_t claimedEndOffset, size_t payloadBytes) {
            std::string hdr =
                "{\"__metadata__\":{\"format\":\"pt\",\"modelspec.title\":\"FluxAdapterV2\"},"
                "\"lora.down.weight\":{\"dtype\":\"F32\",\"shape\":[2,2],\"data_offsets\":[0,16]},"
                "\"lora.alpha\":{\"dtype\":\"BF16\",\"shape\":[2],\"data_offsets\":[16," + std::to_string(claimedEndOffset) + "]}}";
            std::vector<uint8_t> st;
            AppendU64LE(st, hdr.size());
            AppendStr(st, hdr);
            st.insert(st.end(), payloadBytes, 0x3Fu);
            return st;
        };
        std::wstring stGoodPath = tmp.Write(L"flux_lora.safetensors", buildSafeTensors(20, 20));
        std::wstring stForgedPath = tmp.Write(L"forged_oob.safetensors", buildSafeTensors(4096, 20));

        auto stInfo = FileContentPane::InspectAiModelFile(stGoodPath, L".safetensors");
        auto stForgedInfo = FileContentPane::InspectAiModelFile(stForgedPath, L".safetensors");
        ParsedOfficeDocument stDoc;
        bool stOfficeOk = SpreadsheetDocViewer::InspectOfficeFile(stGoodPath, stDoc);

        bool aiOk =
            onnxInfo.valid && !onnxInfo.tensors.empty() && onnxInfo.totalParameters >= 2 &&
            onnxInfo.precisionSummary.find(L"FLOAT32") != std::wstring::npos &&
            !onnxCheatInfo.valid && !onnxWireBadInfo.valid &&
            InspectFails(onnxAsciiCheat) && InspectFails(onnxWireBadPath) &&
            ggufInfo.valid && ggufInfo.arch == L"llama" && ggufInfo.modelTitle == L"TinyLlama-Stage15" &&
            ggufInfo.contextLength == L"8192" && ggufInfo.tensorCount == 2 && ggufInfo.totalParameters == 4160 &&
            ggufInfo.tensors.size() == 2 &&
            ggufInfo.precisionSummary.find(L"Q4_K") != std::wstring::npos &&
            !ggufCheatInfo.valid && InspectFails(ggufCheatPath) &&
            ggufOfficeOk && ggufDoc.valid &&
            SheetContains(ggufDoc, L"GGUF_Metadata", L"TinyLlama-Stage15") &&
            SheetContains(ggufDoc, L"GGUF_Tensors", L"blk.0.attn_q.weight") &&
            stInfo.valid && stInfo.modelTitle == L"FluxAdapterV2" &&
            stInfo.tensorCount == 2 && stInfo.totalParameters == 6 && stInfo.tensors.size() == 2 &&
            stInfo.precisionSummary.find(L"BF16") != std::wstring::npos &&
            !stForgedInfo.valid && InspectFails(stForgedPath) &&
            stOfficeOk && stDoc.valid &&
            SheetContains(stDoc, L"SafeTensors_Metadata", L"FluxAdapterV2") &&
            SheetContains(stDoc, L"SafeTensors_Tensors", L"lora.down.weight");
        tc.Record(aiOk, L"DocsA.AiModels.OnnxGgufSafetensorsAndNegativeGuards");

        // B. Legacy Microsoft Word (.doc/.dot [MS-DOC] Clx Piece Table) & PowerPoint (.ppt/.pps [MS-PPT] Record Tree + Escher)
        auto buildWord97Doc = [](bool encryptedFlag) {
            // Piece 0 (CP1252 compressed): "Executive \x93Smart\x94 Summary \x97 Q4\r"
            const std::string p0 = "Executive \x93" "Smart\x94" " Summary \x97" " Q4\r";
            // Piece 1 (UTF-16LE uncompressed): L"Portal: \x0013HYPERLINK \"https://corp.local\"\x0014VisiblePortalLink\x0015\r"
            const std::wstring p1 = L"Portal: \x0013" L"HYPERLINK \"https://corp.local\"\x0014" L"VisiblePortalLink\x0015\r";
            // Piece 2 (CP1252 compressed Word table): "Metric\x07Target\x07\x07Latency\x0712 ms\x07\x07"
            const std::string p2 = "Metric\x07" "Target\x07\x07" "Latency\x07" "12 ms\x07\x07";

            const uint32_t ccp0 = static_cast<uint32_t>(p0.size());
            const uint32_t ccp1 = static_cast<uint32_t>(p1.size());
            const uint32_t ccp2 = static_cast<uint32_t>(p2.size());
            const uint32_t ccpTotal = ccp0 + ccp1 + ccp2;

            // WordDocument stream: 1024B FIB header + piece data at 1024+
            std::vector<uint8_t> wdoc(1024, 0);
            const uint32_t off0 = static_cast<uint32_t>(wdoc.size());
            AppendStr(wdoc, p0);
            if ((wdoc.size() & 1u) != 0) AppendU8(wdoc, 0);
            const uint32_t off1 = static_cast<uint32_t>(wdoc.size());
            for (wchar_t ch : p1) AppendU16LE(wdoc, static_cast<uint16_t>(ch));
            const uint32_t off2 = static_cast<uint32_t>(wdoc.size());
            AppendStr(wdoc, p2);

            // 1Table stream: Clx = optional RgPrc (0x01, cb=2) + Pcdt (0x02, lcb=40)
            std::vector<uint8_t> tbl;
            AppendU8(tbl, 0x01u); AppendU16LE(tbl, 2); AppendU8(tbl, 0); AppendU8(tbl, 0);
            AppendU8(tbl, 0x02u);
            // PlcPcd for 3 pieces: 4 * u32 aCP (16B) + 3 * 8B Pcd (24B) = 40B
            AppendU32LE(tbl, 40);
            AppendU32LE(tbl, 0);
            AppendU32LE(tbl, ccp0);
            AppendU32LE(tbl, ccp0 + ccp1);
            AppendU32LE(tbl, ccpTotal);
            auto appendPcd = [&](uint32_t fcEncoded) {
                AppendU16LE(tbl, 0);
                AppendU32LE(tbl, fcEncoded);
                AppendU16LE(tbl, 0);
            };
            appendPcd((off0 * 2u) | 0x40000000u); // CP1252 compressed
            appendPcd(off1);                      // UTF-16LE uncompressed
            appendPcd((off2 * 2u) | 0x40000000u); // CP1252 compressed

            // Populate [MS-DOC] §2.5.2 FIB: FibBase(32) + csw(14 -> 28B) + cslw(22 -> 88B) + cbRgFcLcb(93 -> 744B)
            PutU16LE(wdoc.data() + 0, 0xA5ECu); // wIdent (Word 97-2003)
            PutU16LE(wdoc.data() + 2, 0x00C1u); // nFib
            uint16_t flags = 0x0200u;           // fWhichTblStm = 1 ("1Table")
            if (encryptedFlag) flags |= 0x0100u;// fEncrypted
            PutU16LE(wdoc.data() + 10, flags);
            PutU16LE(wdoc.data() + 32, 14);     // csw = 14 -> FibRgLw97 starts at 34 + 28 = 62
            PutU16LE(wdoc.data() + 62, 22);     // cslw = 22 -> ccpText at 64 + 3*4 = 76; cbRgFcLcb at 64 + 88 = 152
            PutU32LE(wdoc.data() + 76, ccpTotal);
            PutU16LE(wdoc.data() + 152, 93);    // FibRgFcLcb starts at 154; fcClx is pair #33 at 154 + 33*8 = 418
            PutU32LE(wdoc.data() + 418, 0);
            PutU32LE(wdoc.data() + 422, static_cast<uint32_t>(tbl.size()));

            return MakeOle2MultiStream({
                { L"WordDocument", std::move(wdoc) },
                { L"1Table",       std::move(tbl)  }
            });
        };

        auto buildPpt97 = [](bool encryptedSession) {
            auto makeRec = [](uint16_t verInst, uint16_t recType, const std::vector<uint8_t>& payload) {
                std::vector<uint8_t> r;
                AppendU16LE(r, verInst);
                AppendU16LE(r, recType);
                AppendU32LE(r, static_cast<uint32_t>(payload.size()));
                r.insert(r.end(), payload.begin(), payload.end());
                return r;
            };
            auto makeUtf16Atom = [&](uint16_t recType, std::wstring_view ws) {
                std::vector<uint8_t> p;
                for (wchar_t ch : ws) AppendU16LE(p, static_cast<uint16_t>(ch));
                return makeRec(0x0000u, recType, p);
            };
            auto makeAsciiAtom = [&](uint16_t recType, std::string_view sv) {
                return makeRec(0x0000u, recType, std::vector<uint8_t>(sv.begin(), sv.end()));
            };
            auto concatRecs = [](std::initializer_list<std::vector<uint8_t>> parts) {
                std::vector<uint8_t> out;
                for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
                return out;
            };

            // RT_SlideListWithText (0x0FF0, ver=0xF, inst=0) with 2 slides
            std::vector<uint8_t> persistDummy(20, 0);
            auto slwt = makeRec(0x000Fu, 0x0FF0u, concatRecs({
                makeRec(0x0000u, 0x03F3u, persistDummy), // Slide 1 PersistAtom
                makeUtf16Atom(0x0FA0u, L"Q4 Architecture Keynote\rScalable Storage Engine"),
                makeRec(0x0000u, 0x03F3u, persistDummy), // Slide 2 PersistAtom
                makeAsciiAtom(0x0FA8u, "Zero-Copy Networking")
            }));
            auto docContainer = makeRec(0x000Fu, 0x03E8u, slwt); // RT_Document

            // Slide 1 container (RT_Slide 0x03EE -> RT_Drawing 0x040C -> msofbtDgContainer 0xF002 -> msofbtSpContainer 0xF004 -> msofbtClientTextbox 0xF00D)
            auto clientTb = makeRec(0x000Fu, 0xF00Du, makeUtf16Atom(0x0FA0u, L"Diagram Callout: RDMA Fabric"));
            auto spCont   = makeRec(0x000Fu, 0xF004u, clientTb);
            auto dgCont   = makeRec(0x000Fu, 0xF002u, spCont);
            auto ppDraw   = makeRec(0x000Fu, 0x040Cu, dgCont);
            auto slide1   = makeRec(0x000Fu, 0x03EEu, ppDraw);
            // Slide 2 container duplicating "Zero-Copy Networking" (tests per-slide deduplication!) + unique shape callout
            auto slide2Tb = makeRec(0x000Fu, 0xF00Du, concatRecs({
                makeAsciiAtom(0x0FA8u, "Zero-Copy Networking"),
                makeUtf16Atom(0x0FA0u, L"Latency P99 < 80 us")
            }));
            auto slide2   = makeRec(0x000Fu, 0x03EEu, makeRec(0x000Fu, 0x040Cu, slide2Tb));

            std::vector<uint8_t> pptStream = concatRecs({ docContainer, slide1, slide2 });

            std::vector<uint8_t> curUser(28, 0);
            PutU16LE(curUser.data() + 0, 0x0000u);
            PutU16LE(curUser.data() + 2, 0x0FF6u); // RT_CurrentUserAtom
            PutU32LE(curUser.data() + 4, 20);
            PutU32LE(curUser.data() + 8, 20);
            PutU32LE(curUser.data() + 12, encryptedSession ? 0xF3D1C4DFu : 0xE391C05Fu);

            return MakeOle2MultiStream({
                { L"PowerPoint Document", std::move(pptStream) },
                { L"Current User",        std::move(curUser)   }
            });
        };

        std::wstring docGoodPath = tmp.Write(L"report_97.doc", buildWord97Doc(false));
        std::wstring docEncPath  = tmp.Write(L"encrypted_97.doc", buildWord97Doc(true));
        std::wstring pptGoodPath = tmp.Write(L"keynote_97.ppt", buildPpt97(false));
        std::wstring pptEncPath  = tmp.Write(L"encrypted_97.ppt", buildPpt97(true));

        ParsedOfficeDocument wDoc, pDoc;
        bool legacyOk =
            SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".doc") &&
            SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".dot") &&
            SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ppt") &&
            SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".pps") &&
            SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".gguf") &&
            SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".safetensors") &&
            SpreadsheetDocViewer::InspectOfficeFile(docGoodPath, wDoc) && wDoc.valid &&
            DocContains(wDoc, L"Executive \x201CSmart\x201D Summary \x2014 Q4") &&
            DocContains(wDoc, L"Portal: VisiblePortalLink") &&
            !DocContains(wDoc, L"HYPERLINK") &&
            DocContains(wDoc, L"Metric | Target") &&
            DocContains(wDoc, L"Latency | 12 ms") &&
            SheetContains(wDoc, L"Word_Tables", L"12 ms") &&
            InspectFails(docEncPath) &&
            SpreadsheetDocViewer::InspectOfficeFile(pptGoodPath, pDoc) && pDoc.valid &&
            DocContains(pDoc, L"--- Slide 1 ---") &&
            DocContains(pDoc, L"Q4 Architecture Keynote") &&
            DocContains(pDoc, L"Scalable Storage Engine") &&
            DocContains(pDoc, L"Diagram Callout: RDMA Fabric") &&
            DocContains(pDoc, L"--- Slide 2 ---") &&
            DocContains(pDoc, L"Zero-Copy Networking") &&
            DocContains(pDoc, L"Latency P99 < 80 us") &&
            InspectFails(pptEncPath);
        tc.Record(legacyOk, L"DocsA.LegacyOfficeOle.WordDocClxPieceTableAndPowerPointPpt");
    }

    tmp.Cleanup();
    RunFormatTestsDocsBatch70(tc);
}

} // namespace Explorer2::SelfTestSuite
