#pragma once
// NumPyNpz.h — NumPy Compressed Tensor Archive (.npz) Reader

namespace Formats {
namespace AiDataDetail {

inline bool ParseNumPyNpz(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 22 || raw[0] != 'P' || raw[1] != 'K') return false;
    // The ZIP directory closes the archive: for a file larger than the bytes handed in, it and the arrays are read
    // from the file, a stored array (np.savez) only as far as its preview needs
    SourceBytes src(raw, out);
    const bool fromFile = src.HasMoreThanHead();
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    const bool listed = fromFile ? ZipDeflate::ListZipEntriesFromFile(out.filePath, entries)
                                 : ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries);
    if (!listed || entries.empty()) return false;
    auto entryFromFile = [&](const ZipDeflate::ArchiveItemMeta& e, std::vector<uint8_t>& bytes) -> bool {
        if (e.compressionMethod != 0 || e.isEncrypted) return ZipDeflate::ExtractZipEntryFromFile(out.filePath, e, bytes);
        std::vector<uint8_t> lh;
        const uint8_t* h = src.Read(e.localHeaderOffset, 30, lh);
        if (!h || RdU32LE(h) != 0x04034B50u) return false;
        const uint64_t dataOff = e.localHeaderOffset + 30 + RdU16LE(h + 26) + RdU16LE(h + 28);
        size_t got = 0;
        const uint8_t* p = src.ReadSome(dataOff, e.uncompressedSize, 4u << 20, bytes, got);
        if (!p) return false;
        if (p != bytes.data()) bytes.assign(p, p + got); // (bytes handed in come back as a pointer into them)
        return true;
    };

    SpreadsheetSheet summary;
    summary.name = L"NPZ_Archive";
    summary.rows.push_back({ L"Array Key", L"Archive Entry", L"Compressed", L"Uncompressed" });

    std::vector<SpreadsheetSheet> arraySheets;
    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        std::wstring ext = PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive));
        if (ext != L".npy") continue;
        std::vector<uint8_t> npyBytes;
        const bool extracted = fromFile ? entryFromFile(e, npyBytes) : ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, npyBytes);
        if (!extracted || npyBytes.empty()) continue;

        std::wstring key = e.name.empty() ? e.pathInArchive : e.name;
        if (key.size() > 4 && PathUtils::ToLower(key.substr(key.size() - 4)) == L".npy") {
            key.resize(key.size() - 4);
        }
        if (ParseSingleNpyBuffer(npyBytes.data(), npyBytes.size(), key, arraySheets, false)) {
            summary.rows.push_back({
                key,
                e.pathInArchive,
                PathUtils::FormatByteSize(e.compressedSize),
                PathUtils::FormatByteSize(e.uncompressedSize)
            });
        }
        if (arraySheets.size() >= 24) break;
    }

    if (arraySheets.empty()) return false;
    FinalizeSheetCols(summary);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"NumPy Compressed Tensor Archive (.npz)";
    out.sheets.push_back(std::move(summary));
    for (auto& s : arraySheets) out.sheets.push_back(std::move(s));
    if (fromFile && !src.ReadFailed()) out.sourceReadFully = true; // the listing and the previews came from the file
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseNumPyNpz(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return AiDataDetail::ParseNumPyNpz(raw, out);
}

} // namespace Formats
