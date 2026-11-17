#pragma once
// Ole2Compound.h — Native Microsoft Compound File Binary (OLE2 / CFB, MS-CFB) Reader
// Supports v3 (512B sectors) & v4 (4096B sectors), DIFAT chains, FAT, MiniFAT, MiniStream,
// hierarchical Red-Black directory tree traversal, and MSI 6-bit stream name demangling.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "ByteIO.h"

namespace Explorer2::Ole2 {

struct DirEntry {
    std::wstring rawName;   // Exact UTF-16 code units from directory entry (needed for MSI demangling)
    std::wstring name;      // Cleaned display name (printable characters, control prefixes stripped)
    std::wstring fullPath;  // Hierarchical path inside compound file, e.g. "SubStorage/StreamName"
    uint8_t objectType = 0; // 1 = Storage, 2 = Stream, 5 = Root Entry
    uint32_t leftSibling  = 0xFFFFFFFFu;
    uint32_t rightSibling = 0xFFFFFFFFu;
    uint32_t childId      = 0xFFFFFFFFu;
    uint32_t startSector  = 0xFFFFFFFFu;
    uint64_t size         = 0;
};

struct CompoundFile {
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    uint16_t majorVersion = 3;
    size_t sectorSize = 512;
    size_t miniSectorSize = 64;
    uint32_t miniStreamCutoff = 4096;
    std::vector<uint32_t> fat;
    std::vector<uint32_t> miniFat;
    std::vector<uint8_t> miniStream;
    std::vector<DirEntry> entries;
    bool valid = false;
};

namespace Detail {

// little-endian loads (ByteIO.h)
inline uint16_t Rd16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }

inline uint32_t Rd32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }

inline uint64_t Rd64(const uint8_t* p) { return ::Explorer2::ByteIO::RdU64LE(p); }

constexpr uint32_t kEndOfChain = 0xFFFFFFFEu;
constexpr uint32_t kFreeSect   = 0xFFFFFFFFu;

inline bool ReadFatChain(
    const uint8_t* data,
    size_t dataSize,
    size_t sectorSize,
    const std::vector<uint32_t>& fat,
    uint32_t startSector,
    uint64_t wantedBytes,
    size_t maxBytes,
    std::vector<uint8_t>& out)
{
    out.clear();
    if (startSector >= kEndOfChain || sectorSize == 0) return wantedBytes == 0;
    const size_t cap = std::min<uint64_t>(wantedBytes, maxBytes);
    out.reserve(std::min<size_t>(cap, 1024 * 1024));

    const size_t maxSteps = (dataSize / sectorSize) + 4;
    uint32_t sec = startSector;
    size_t steps = 0;
    while (sec < kEndOfChain && out.size() < cap && steps++ < maxSteps) {
        const uint64_t byteOff = (static_cast<uint64_t>(sec) + 1ULL) * sectorSize;
        if (byteOff + sectorSize > dataSize) break;
        const size_t take = std::min<size_t>(sectorSize, cap - out.size());
        out.insert(out.end(), data + static_cast<size_t>(byteOff), data + static_cast<size_t>(byteOff) + take);
        if (sec >= fat.size()) break;
        const uint32_t next = fat[sec];
        if (next == sec) break;
        sec = next;
    }
    return !out.empty() || wantedBytes == 0;
}

inline void WalkRbTree(
    std::vector<DirEntry>& entries,
    uint32_t nodeId,
    const std::wstring& parentPath,
    std::vector<uint8_t>& visited,
    int depth)
{
    if (nodeId >= entries.size() || depth > 256 || visited[nodeId]) return;
    visited[nodeId] = 1;

    DirEntry& e = entries[nodeId];
    WalkRbTree(entries, e.leftSibling, parentPath, visited, depth + 1);

    if (e.objectType == 1 || e.objectType == 2) {
        e.fullPath = parentPath.empty() ? e.name : (parentPath + L"/" + e.name);
        if (e.objectType == 1 && e.childId < entries.size()) {
            WalkRbTree(entries, e.childId, e.fullPath, visited, depth + 1);
        }
    }

    WalkRbTree(entries, e.rightSibling, parentPath, visited, depth + 1);
}

} // namespace Detail

// Decodes Windows Installer (.msi / .msp) 6-bit compressed UTF-16 stream names (0x3800..0x4840)
inline std::wstring DemangleMsiStreamName(const std::wstring& raw) {
    auto mapSixBit = [](uint16_t v) -> wchar_t {
        if (v < 10) return static_cast<wchar_t>(L'0' + v);
        if (v < 36) return static_cast<wchar_t>(L'A' + (v - 10));
        if (v < 62) return static_cast<wchar_t>(L'a' + (v - 36));
        return (v == 62) ? L'.' : L'_';
    };

    std::wstring out;
    out.reserve(raw.size() * 2);
    for (wchar_t ch : raw) {
        const uint16_t u = static_cast<uint16_t>(ch);
        if (u == 0x4840) {
            out.push_back(L'!'); // MSI table prefix marker
        } else if (u >= 0x3800 && u < 0x4800) {
            const uint16_t code = u - 0x3800;
            out.push_back(mapSixBit(code & 0x3F));
            out.push_back(mapSixBit((code >> 6) & 0x3F));
        } else if (u >= 0x4800 && u < 0x4840) {
            out.push_back(mapSixBit(u - 0x4800));
        } else if (u == 0x0005) {
            // OLE2 SummaryInformation / DocumentSummaryInformation prefix
        } else if (u >= 32 && u < 0xFFFE) {
            out.push_back(ch);
        }
    }
    return out;
}

inline bool ParseCompoundFile(const uint8_t* data, size_t size, CompoundFile& out, bool demangleMsi = false) {
    out = {};
    static const uint8_t kMagic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    if (!data || size < 512 || std::memcmp(data, kMagic, 8) != 0) return false;

    const uint16_t majorVer      = Detail::Rd16(data + 26);
    const uint16_t sectorShift   = Detail::Rd16(data + 30);
    const uint16_t miniShift     = Detail::Rd16(data + 32);
    if ((sectorShift != 9 && sectorShift != 12) || miniShift != 6) return false;

    const size_t sectorSize      = static_cast<size_t>(1) << sectorShift;
    const size_t miniSectorSize  = static_cast<size_t>(1) << miniShift;
    const uint32_t numFatSecs    = Detail::Rd32(data + 44);
    const uint32_t firstDirSec   = Detail::Rd32(data + 48);
    const uint32_t miniCutoff    = Detail::Rd32(data + 56);
    const uint32_t firstMiniFat  = Detail::Rd32(data + 60);
    const uint32_t numMiniFat    = Detail::Rd32(data + 64);
    const uint32_t firstDifat    = Detail::Rd32(data + 68);
    const uint32_t numDifat      = Detail::Rd32(data + 72);

    out.data = data;
    out.dataSize = size;
    out.majorVersion = majorVer;
    out.sectorSize = sectorSize;
    out.miniSectorSize = miniSectorSize;
    out.miniStreamCutoff = (miniCutoff > 0) ? miniCutoff : 4096;

    // 1. Collect DIFAT sector indices (109 in header + optional chained DIFAT sectors)
    const size_t maxTotalSectors = size / sectorSize;
    std::vector<uint32_t> difat;
    difat.reserve(std::min<size_t>(numFatSecs + 16, 16384));
    for (int i = 0; i < 109 && difat.size() < numFatSecs; ++i) {
        const uint32_t s = Detail::Rd32(data + 76 + i * 4);
        if (s < Detail::kEndOfChain) difat.push_back(s);
    }
    uint32_t difatSec = firstDifat;
    uint32_t difatSteps = 0;
    const size_t entriesPerSector = sectorSize / 4;
    while (difatSec < Detail::kEndOfChain && difatSteps < numDifat && difatSteps < maxTotalSectors) {
        const uint64_t off = (static_cast<uint64_t>(difatSec) + 1ULL) * sectorSize;
        if (off + sectorSize > size) break;
        for (size_t i = 0; i + 1 < entriesPerSector && difat.size() < numFatSecs; ++i) {
            const uint32_t s = Detail::Rd32(data + static_cast<size_t>(off) + i * 4);
            if (s < Detail::kEndOfChain) difat.push_back(s);
        }
        difatSec = Detail::Rd32(data + static_cast<size_t>(off) + (entriesPerSector - 1) * 4);
        ++difatSteps;
    }

    // 2. Build FAT
    out.fat.reserve(difat.size() * entriesPerSector);
    for (uint32_t fs : difat) {
        const uint64_t off = (static_cast<uint64_t>(fs) + 1ULL) * sectorSize;
        if (off + sectorSize > size) continue;
        for (size_t i = 0; i < entriesPerSector; ++i) {
            out.fat.push_back(Detail::Rd32(data + static_cast<size_t>(off) + i * 4));
        }
        if (out.fat.size() > maxTotalSectors + entriesPerSector) break;
    }

    // Fallback for synthetic minimal OLE2 files whose FAT header entry was left unpopulated
    if (out.fat.empty()) {
        out.fat.resize(maxTotalSectors, Detail::kEndOfChain);
    }

    // 3. Read Directory stream
    std::vector<uint8_t> dirBytes;
    const uint64_t maxDirBytes = std::min<uint64_t>(size, 16ULL * 1024 * 1024);
    if (!Detail::ReadFatChain(data, size, sectorSize, out.fat, firstDirSec, maxDirBytes, static_cast<size_t>(maxDirBytes), dirBytes)) {
        // Direct sector fallback if firstDirSec points directly inside file
        const uint64_t directOff = (static_cast<uint64_t>(firstDirSec) + 1ULL) * sectorSize;
        if (directOff + 128 <= size) {
            const size_t avail = std::min<size_t>(size - static_cast<size_t>(directOff), sectorSize * 4);
            dirBytes.assign(data + static_cast<size_t>(directOff), data + static_cast<size_t>(directOff) + avail);
        }
    }
    if (dirBytes.size() < 128) return false;

    const size_t entryCount = std::min<size_t>(dirBytes.size() / 128, 32768);
    out.entries.resize(entryCount);
    for (size_t i = 0; i < entryCount; ++i) {
        const uint8_t* ent = dirBytes.data() + i * 128;
        const uint16_t cbName = Detail::Rd16(ent + 64);
        const uint8_t objType = ent[66];
        DirEntry& de = out.entries[i];
        de.objectType   = objType;
        de.leftSibling  = Detail::Rd32(ent + 68);
        de.rightSibling = Detail::Rd32(ent + 72);
        de.childId      = Detail::Rd32(ent + 76);
        de.startSector  = Detail::Rd32(ent + 116);
        de.size         = (sectorShift >= 12) ? Detail::Rd64(ent + 120) : Detail::Rd32(ent + 120);

        if (cbName >= 2 && cbName <= 64 && (objType == 1 || objType == 2 || objType == 5)) {
            for (size_t c = 0; c + 2 <= cbName; c += 2) {
                const wchar_t wc = static_cast<wchar_t>(Detail::Rd16(ent + c));
                if (wc == 0) break;
                de.rawName.push_back(wc);
            }
            if (demangleMsi) {
                de.name = DemangleMsiStreamName(de.rawName);
            } else {
                for (wchar_t wc : de.rawName) {
                    if (wc >= 32 && wc < 0xFFFE) de.name.push_back(wc);
                }
            }
        }
    }

    // Populate hierarchical fullPath via Root Entry child Red-Black tree; also sweep any unlinked entries
    std::vector<uint8_t> visited(entryCount, 0);
    if (!out.entries.empty() && out.entries[0].objectType == 5 && out.entries[0].childId < entryCount) {
        visited[0] = 1;
        Detail::WalkRbTree(out.entries, out.entries[0].childId, L"", visited, 0);
    }
    for (size_t i = 1; i < entryCount; ++i) {
        if (!visited[i] && (out.entries[i].objectType == 1 || out.entries[i].objectType == 2) && !out.entries[i].name.empty()) {
            out.entries[i].fullPath = out.entries[i].name;
        }
    }

    // 4. Read MiniFAT & Root MiniStream
    if (firstMiniFat < Detail::kEndOfChain && numMiniFat > 0) {
        std::vector<uint8_t> mfBytes;
        const uint64_t wantedMf = static_cast<uint64_t>(numMiniFat) * sectorSize;
        if (Detail::ReadFatChain(data, size, sectorSize, out.fat, firstMiniFat, wantedMf, 16 * 1024 * 1024, mfBytes)) {
            out.miniFat.reserve(mfBytes.size() / 4);
            for (size_t i = 0; i + 4 <= mfBytes.size(); i += 4) {
                out.miniFat.push_back(Detail::Rd32(mfBytes.data() + i));
            }
        }
    }
    if (!out.entries.empty() && out.entries[0].objectType == 5 &&
        out.entries[0].startSector < Detail::kEndOfChain && out.entries[0].size > 0)
    {
        Detail::ReadFatChain(data, size, sectorSize, out.fat,
            out.entries[0].startSector, out.entries[0].size, 64 * 1024 * 1024, out.miniStream);
    }

    out.valid = true;
    return true;
}

inline bool ExtractStream(const CompoundFile& cf, const DirEntry& entry, std::vector<uint8_t>& outBytes, size_t maxBytes = 64 * 1024 * 1024) {
    outBytes.clear();
    if (!cf.valid || entry.objectType != 2) return false;
    if (entry.size == 0) return true;

    const size_t cap = static_cast<size_t>(std::min<uint64_t>(entry.size, maxBytes));
    if (entry.size < cf.miniStreamCutoff && !cf.miniStream.empty()) {
        outBytes.reserve(cap);
        const size_t maxSteps = (cf.miniStream.size() / cf.miniSectorSize) + 4;
        uint32_t ms = entry.startSector;
        size_t steps = 0;
        while (ms < Detail::kEndOfChain && outBytes.size() < cap && steps++ < maxSteps) {
            const uint64_t off = static_cast<uint64_t>(ms) * cf.miniSectorSize;
            if (off + cf.miniSectorSize > cf.miniStream.size()) {
                if (off < cf.miniStream.size()) {
                    const size_t avail = std::min<size_t>(cf.miniStream.size() - static_cast<size_t>(off), cap - outBytes.size());
                    outBytes.insert(outBytes.end(), cf.miniStream.data() + static_cast<size_t>(off), cf.miniStream.data() + static_cast<size_t>(off) + avail);
                }
                break;
            }
            const size_t take = std::min<size_t>(cf.miniSectorSize, cap - outBytes.size());
            outBytes.insert(outBytes.end(), cf.miniStream.data() + static_cast<size_t>(off), cf.miniStream.data() + static_cast<size_t>(off) + take);
            if (ms < cf.miniFat.size()) {
                const uint32_t next = cf.miniFat[ms];
                if (next == ms) break;
                ms = next;
            } else {
                ++ms; // Contiguous fallback when MiniFAT was omitted
            }
        }
        if (!outBytes.empty()) return true;
    }

    return Detail::ReadFatChain(cf.data, cf.dataSize, cf.sectorSize, cf.fat, entry.startSector, entry.size, cap, outBytes);
}

inline const DirEntry* FindStreamCaseInsensitive(const CompoundFile& cf, std::wstring_view targetName) {
    auto eq = [](wchar_t a, wchar_t b) {
        if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
        return a == b;
    };
    for (const auto& e : cf.entries) {
        if (e.objectType != 2) continue;
        if (e.name.size() == targetName.size() && std::equal(e.name.begin(), e.name.end(), targetName.begin(), eq)) return &e;
        if (e.fullPath.size() == targetName.size() && std::equal(e.fullPath.begin(), e.fullPath.end(), targetName.begin(), eq)) return &e;
    }
    return nullptr;
}

} // namespace Explorer2::Ole2
