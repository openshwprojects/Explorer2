#pragma once
// GtaImg.h - Rockstar's IMG archives of the RenderWare-era Grand Theft Auto games (.img: gta3.img, gta_int.img,
// player.img...). They hold the models (.dff), textures (.txd), collisions (.col) and animations (.ifp).
//
// Version 2 (GTA San Andreas): "VER2", the number of entries, then 32-byte entries: the data's offset (uint32)
// and size (uint16 + uint16, the second one normally 0) in 2048-byte sectors, and a name of 24 bytes.
// Version 1 (GTA III, Vice City; also Bully and Manhunt): the .img holds only the data; its directory is the
// .dir file of the same name next to it, made of the same 32-byte entries (offset and size as two uint32).
// Both store files as they are, padded to whole sectors: a file comes out with that padding, as with the
// games' own tools.
// GTA IV's version 3 is not read: its directory is encrypted with a key from the game.

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace GtaImgDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;

inline constexpr uint64_t kSector = 2048;

// A name of a directory entry: up to 24 bytes, ended by a zero; printable characters only
inline bool EntryName(const uint8_t* p, std::string& out) {
    out.assign(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), 24));
    if (out.empty()) return false;
    for (unsigned char c : out) {
        if (c < 0x20 || c == 0x7F || c == '/' || c == '\\') return false;
    }
    return true;
}

inline void AddEntry(ArchiveSummaryInfo& outInfo, const std::string& name, uint64_t offset, uint64_t length) {
    ArchiveItemEntry item;
    item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(name));
    if (item.pathInArchive.empty()) return;
    item.name = ArchivePath::BaseName(item.pathInArchive);
    item.uncompressedSize = length;
    item.compressedSize = length;
    item.rawDataOffset = offset;
    item.compressionMethod = 0;
    outInfo.entries.push_back(std::move(item));
}

// Version 2: the directory is at the start of the .img itself
inline bool ParseVer2(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 8 || std::memcmp(data, "VER2", 4) != 0) return false;
    const uint32_t count = RdU32LE(data + 4);
    if (count == 0 || count > 1000000 || 8ull + static_cast<uint64_t>(count) * 32 > size) return false;
    outInfo.formatName = L"GTA IMG v2 (San Andreas)";
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = data + 8 + static_cast<size_t>(i) * 32;
        const uint64_t offset = RdU32LE(e) * kSector;
        const uint16_t streaming = RdU16LE(e + 4);
        const uint16_t inArchive = RdU16LE(e + 6);
        const uint64_t length = static_cast<uint64_t>(inArchive ? inArchive : streaming) * kSector;
        std::string name;
        if (!EntryName(e + 8, name) || offset > size) continue;
        AddEntry(outInfo, name, offset, std::min<uint64_t>(length, size - offset));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// Version 1: the directory is the .dir next to the .img; every entry has to fit in the .img
inline bool ParseVer1(const std::vector<uint8_t>& dir, size_t imgSize, ArchiveSummaryInfo& outInfo) {
    if (dir.size() < 32 || (dir.size() % 32) != 0 || dir.size() / 32 > 1000000) return false;
    ArchiveSummaryInfo info;
    info.formatName = L"GTA IMG v1 (GTA III / Vice City, .img + .dir)";
    for (size_t at = 0; at + 32 <= dir.size(); at += 32) {
        const uint8_t* e = dir.data() + at;
        const uint64_t offset = RdU32LE(e) * kSector;
        const uint64_t length = RdU32LE(e + 4) * kSector;
        std::string name;
        if (!EntryName(e + 8, name)) return false;          // a .dir that is no directory of this kind
        if (offset > imgSize || length > imgSize - offset) return false;
        AddEntry(info, name, offset, length);
    }
    if (info.entries.empty()) return false;
    info.valid = true;
    outInfo = std::move(info);
    return true;
}

} // namespace GtaImgDetail

// archivePath: the .img itself, to find the .dir of a version 1 archive next to it
inline bool InspectGtaImg(const std::wstring* archivePath, const uint8_t* data, size_t size, const std::wstring& extLower,
                          ArchiveSummaryInfo& outInfo) {
    if (!data) return false;
    if (GtaImgDetail::ParseVer2(data, size, outInfo)) return true;
    if (extLower != L".img" || !archivePath || archivePath->size() < 4) return false;
    const std::wstring stem = archivePath->substr(0, archivePath->size() - 4);
    for (const wchar_t* ext : { L".dir", L".DIR", L".Dir" }) {
        std::vector<uint8_t> dir;
        if (GetFileAttributesW((stem + ext).c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        if (PathUtils::ReadFileBytes(stem + ext, 64ull << 20, dir) && GtaImgDetail::ParseVer1(dir, size, outInfo)) return true;
    }
    return false;
}

} // namespace Explorer2::Formats
