#pragma once
// RocksLevelDbSst.h — RocksDB / LevelDB Sorted String Table (.sst / .ldb) Reader

namespace Formats {

// ============================================================================
// #83. RocksDB / LevelDB Sorted String Table (.sst / .ldb)
// ============================================================================
// The footer names the index block, whose entries are the handles of the data blocks; each data block (stored
// plain, or Snappy / LZ4 / Zstandard compressed) is read in turn and its prefix-compressed entries become rows,
// internal keys split into user key, sequence number and operation. RocksDB format 4+ index values are
// delta-encoded handles. PlainTable files have no blocks: only their footer is shown.
inline bool ParseRocksLevelDbSst200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    (void)ext;
    // Bytes past those handed in (RocksDB tables are often 64 MB) are read from the file itself
    SourceBytes source(raw, out);
    const uint64_t fileSize = source.Size();
    auto readRange = [&](uint64_t off, uint64_t len, std::vector<uint8_t>& buf) -> const uint8_t* {
        return (len > 64u * 1024 * 1024) ? nullptr : source.Read(off, static_cast<size_t>(len), buf);
    };
    if (fileSize < 48) return false;
    std::vector<uint8_t> footerBuf;
    const size_t footLen = static_cast<size_t>(std::min<uint64_t>(fileSize, 53));
    const uint8_t* foot = readRange(fileSize - footLen, footLen, footerBuf);
    if (!foot) return false;
    const uint8_t* footEnd = foot + footLen;
    const uint64_t footerMagic = DetailDocs200::RdU64LE(footEnd - 8);
    constexpr uint64_t kLevelDbMagic = 0xDB4775248B80FB57ULL;      // LevelDB, RocksDB legacy block-based
    constexpr uint64_t kBlockBasedMagic = 0x88E241B785F4CFF7ULL;   // RocksDB block-based
    constexpr uint64_t kPlainMagic = 0x8242229663BF9564ULL;        // RocksDB PlainTable
    constexpr uint64_t kLegacyPlainMagic = 0x4F3418EB7A8F13B8ULL;
    const bool isLevelDb = footerMagic == kLevelDbMagic;
    const bool isBlockBased = footerMagic == kBlockBasedMagic;
    const bool isPlain = footerMagic == kPlainMagic || footerMagic == kLegacyPlainMagic;
    if (!isLevelDb && !isBlockBased && !isPlain) return false;
    if ((isBlockBased || isPlain) && footLen < 53) return false;

    const std::wstring fmt = isLevelDb ? L"LevelDB Table (0xDB4775248B80FB57)"
                           : isBlockBased ? L"RocksDB Block-Based SSTable (0x88E241B785F4CFF7)"
                                          : L"RocksDB PlainTable SST";
    // LevelDB footer: two handles in 40 bytes + magic. RocksDB: checksum type, the handles, format version, magic.
    const uint32_t formatVersion = isLevelDb ? 0u : DetailDocs200::RdU32LE(footEnd - 12);
    const uint8_t* handles = isLevelDb ? footEnd - 48 : footEnd - 52; // 40 bytes either way
    size_t fpos = 0;
    uint64_t metaOff = 0, metaSz = 0, idxOff = 0, idxSz = 0;
    const bool handlesOk = DetailDocs200::ReadVarint64(handles, 40, fpos, metaOff) &&
                           DetailDocs200::ReadVarint64(handles, 40, fpos, metaSz) &&
                           DetailDocs200::ReadVarint64(handles, 40, fpos, idxOff) &&
                           DetailDocs200::ReadVarint64(handles, 40, fpos, idxSz);

    // A block's contents, decompressed; the 5-byte trailer (type + checksum) follows the stored bytes
    std::map<int, size_t> unreadByType;
    auto readBlock = [&](uint64_t off, uint64_t size, std::vector<uint8_t>& store, const uint8_t*& data, size_t& len) -> bool {
        if (size > 64u * 1024 * 1024) return false;
        std::vector<uint8_t> fileBytes;
        const uint8_t* src = readRange(off, size + 5, fileBytes);
        if (!src) return false;
        const int ctype = src[static_cast<size_t>(size)];
        if (ctype == 0) {
            if (!fileBytes.empty() && src == fileBytes.data()) { // keep bytes read from the file alive
                store = std::move(fileBytes);
                src = store.data();
            }
            data = src;
            len = static_cast<size_t>(size);
            return true;
        }
        bool ok = false;
        if (ctype == 1) {
            ok = ::Explorer2::StreamCodecs::SnappyDecompress(src, static_cast<size_t>(size), store, 64 * 1024 * 1024);
        } else if (ctype == 4 || ctype == 5 || ctype == 7) { // LZ4 / LZ4HC / ZSTD: format 2+ puts the size first
            size_t p = 0;
            uint64_t expect = 0;
            const bool sized = formatVersion >= 2 && DetailDocs200::ReadVarint64(src, static_cast<size_t>(size), p, expect) && expect <= 64u * 1024 * 1024;
            if (ctype == 7) {
                ok = ::Explorer2::StreamCodecs::DecompressZstdFrame(src + (sized ? p : 0), static_cast<size_t>(size) - (sized ? p : 0), store, 64 * 1024 * 1024);
            } else if (sized) {
                ok = ::Explorer2::Lz4::DecodeBlockExact(src + p, static_cast<size_t>(size) - p, static_cast<size_t>(expect), store);
            }
        }
        if (!ok) {
            ++unreadByType[ctype];
            return false;
        }
        data = store.data();
        len = store.size();
        return true;
    };
    // The restart array closing a block: its entries end where it starts
    auto entriesEnd = [](const uint8_t* b, size_t n, std::vector<uint32_t>* restarts) -> size_t {
        if (n < 4) return SIZE_MAX;
        uint32_t nr = DetailDocs200::RdU32LE(b + n - 4);
        if ((nr & 0x80000000u) != 0 || nr == 0 || 4ull * (nr + 1) > n) return SIZE_MAX; // (a hash-indexed block is not read)
        size_t end = n - 4 * (static_cast<size_t>(nr) + 1);
        if (restarts) {
            restarts->resize(nr);
            for (uint32_t i = 0; i < nr; ++i) (*restarts)[i] = DetailDocs200::RdU32LE(b + end + 4 * i);
        }
        return end;
    };

    // Data block handles from the index block
    std::vector<std::pair<uint64_t, uint64_t>> dataBlocks;
    if (handlesOk && !isPlain) {
        std::vector<uint8_t> store;
        const uint8_t* b = nullptr;
        size_t n = 0;
        std::vector<uint32_t> restarts;
        size_t end = readBlock(idxOff, idxSz, store, b, n) ? entriesEnd(b, n, &restarts) : SIZE_MAX;
        const bool valueDelta = isBlockBased && formatVersion >= 4;
        size_t pos = 0, ri = 0;
        uint64_t prevOff = 0, prevSize = 0;
        while (end != SIZE_MAX && pos < end && dataBlocks.size() < 100000) {
            const bool restart = ri < restarts.size() && restarts[ri] == pos;
            if (restart) ++ri;
            uint64_t shared = 0, nonShared = 0, valLen = 0;
            if (!DetailDocs200::ReadVarint64(b, end, pos, shared) || !DetailDocs200::ReadVarint64(b, end, pos, nonShared)) break;
            if (!valueDelta && !DetailDocs200::ReadVarint64(b, end, pos, valLen)) break;
            if (nonShared > end - pos) break;
            pos += static_cast<size_t>(nonShared);
            uint64_t off = 0, size = 0;
            size_t vpos = pos;
            if (!valueDelta || restart) {
                if (!DetailDocs200::ReadVarint64(b, end, vpos, off) || !DetailDocs200::ReadVarint64(b, end, vpos, size)) break;
            } else {
                uint64_t zz = 0;
                if (!DetailDocs200::ReadVarint64(b, end, vpos, zz)) break;
                const int64_t delta = static_cast<int64_t>(zz >> 1) ^ -static_cast<int64_t>(zz & 1);
                off = prevOff + prevSize + 5;
                size = static_cast<uint64_t>(static_cast<int64_t>(prevSize) + delta);
            }
            if (valueDelta) {
                pos = vpos;
            } else {
                if (valLen > end - pos) break;
                pos += static_cast<size_t>(valLen);
            }
            if (off >= fileSize || size > fileSize - off) break;
            dataBlocks.push_back({ off, size });
            prevOff = off;
            prevSize = size;
        }
    }

    SpreadsheetSheet kvSh;
    kvSh.name = L"SST_KeyValues";
    kvSh.maxCols = 4;
    kvSh.rows.push_back({ L"User Key", L"Op / Type", L"Sequence #", L"Value Preview" });
    auto printable = [](const uint8_t* p, size_t n) { return DetailDocs200::IsMostlyPrintableUtf8(p, n); };
    size_t entries = 0, blocksRead = 0;
    for (const auto& h : dataBlocks) {
        std::vector<uint8_t> store;
        const uint8_t* b = nullptr;
        size_t n = 0;
        if (!readBlock(h.first, h.second, store, b, n)) continue;
        const size_t end = entriesEnd(b, n, nullptr);
        if (end == SIZE_MAX) continue;
        ++blocksRead;
        std::string key;
        size_t pos = 0;
        while (pos < end) {
            uint64_t shared = 0, nonShared = 0, valLen = 0;
            if (!DetailDocs200::ReadVarint64(b, end, pos, shared) || !DetailDocs200::ReadVarint64(b, end, pos, nonShared) ||
                !DetailDocs200::ReadVarint64(b, end, pos, valLen)) break;
            if (shared > key.size() || nonShared > end - pos || valLen > end - pos - nonShared) break;
            key.resize(static_cast<size_t>(shared));
            key.append(reinterpret_cast<const char*>(b + pos), static_cast<size_t>(nonShared));
            pos += static_cast<size_t>(nonShared);
            const uint8_t* val = b + pos;
            pos += static_cast<size_t>(valLen);
            ++entries;
            if (kvSh.rows.size() > 5000) continue;

            // Internal key: user key + 8 bytes (sequence << 8 | type)
            std::wstring userKey, op = L"-", seq = L"-";
            size_t userLen = key.size();
            if (key.size() >= 8) {
                const uint64_t tag = DetailDocs200::RdU64LE(reinterpret_cast<const uint8_t*>(key.data() + key.size() - 8));
                const uint32_t type = static_cast<uint32_t>(tag & 0xFFu);
                static const struct { uint32_t t; const wchar_t* name; } kOps[] = {
                    { 0x0, L"Delete" }, { 0x1, L"Put" }, { 0x2, L"Merge" }, { 0x7, L"SingleDelete" },
                    { 0xF, L"RangeDelete" }, { 0x11, L"BlobIndex" }, { 0x14, L"DeleteWithTimestamp" }, { 0x16, L"WideColumns" },
                };
                for (const auto& o : kOps) {
                    if (o.t == type) { op = o.name; break; }
                }
                if (op != L"-") {
                    userLen = key.size() - 8;
                    seq = std::to_wstring(tag >> 8);
                }
            }
            const uint8_t* uk = reinterpret_cast<const uint8_t*>(key.data());
            userKey = printable(uk, userLen) ? PathUtils::Utf8ToWide(std::string_view(key.data(), userLen))
                                             : DetailDocs200::HexBytes(uk, userLen, 32);
            const size_t vShow = std::min<size_t>(static_cast<size_t>(valLen), 160);
            std::wstring valText = printable(val, vShow) ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(val), vShow))
                                                         : DetailDocs200::HexBytes(val, static_cast<size_t>(valLen), 24);
            kvSh.rows.push_back({ userKey, op, seq, valText });
        }
    }

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"SST_Footer";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"SSTable Field", L"Value" });
    hdrSh.rows.push_back({ L"Table Format", fmt });
    if (!isLevelDb) hdrSh.rows.push_back({ L"Format Version", std::to_wstring(formatVersion) });
    hdrSh.rows.push_back({ L"File Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });
    if (handlesOk) {
        hdrSh.rows.push_back({ L"Metaindex Block Handle", L"offset=" + std::to_wstring(metaOff) + L", size=" + std::to_wstring(metaSz) });
        hdrSh.rows.push_back({ L"Index Block Handle", L"offset=" + std::to_wstring(idxOff) + L", size=" + std::to_wstring(idxSz) });
    }
    if (isPlain) {
        hdrSh.rows.push_back({ L"Entries", L"PlainTable layout: entries are not listed" });
    } else {
        hdrSh.rows.push_back({ L"Data Blocks", std::to_wstring(dataBlocks.size()) + L" in the index, " + std::to_wstring(blocksRead) + L" read" });
        hdrSh.rows.push_back({ L"Entries", std::to_wstring(entries) + (entries + 1 > kvSh.rows.size() ? L" (first " + std::to_wstring(kvSh.rows.size() - 1) + L" listed)" : L"") });
        static const struct { int t; const wchar_t* name; } kCodecs[] = {
            { 1, L"Snappy" }, { 2, L"zlib" }, { 3, L"bzip2" }, { 4, L"LZ4" }, { 5, L"LZ4HC" }, { 6, L"XPRESS" }, { 7, L"Zstandard" },
        };
        for (const auto& u : unreadByType) {
            std::wstring codec = L"type " + std::to_wstring(u.first);
            for (const auto& k : kCodecs) {
                if (k.t == u.first) codec = k.name;
            }
            hdrSh.rows.push_back({ L"Blocks Not Decoded", std::to_wstring(u.second) + L" (" + codec + L")" });
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = fmt;
    if (source.ReadPastHead() && !source.ReadFailed()) out.sourceReadFully = true;
    if (kvSh.rows.size() > 1) out.sheets.push_back(std::move(kvSh));
    out.sheets.push_back(std::move(hdrSh));
    out.valid = true;
    return true;
}


} // namespace Formats
