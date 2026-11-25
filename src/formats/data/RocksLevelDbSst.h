#pragma once
// RocksLevelDbSst.h — RocksDB / LevelDB Sorted String Table (.sst / .ldb) Reader

namespace Formats {

// ============================================================================
// #83. RocksDB / LevelDB Sorted String Table (.sst / .ldb)
// ============================================================================
// Decodes LevelDB & RocksDB Block-Based SSTables (plain, Snappy, zlib, bzip2, LZ4/LZ4HC, Zstandard)
// AND RocksDB PlainTable SSTables (kPlain & kPrefix key coding + 0x80 zero-sequence & 8B internal tags).
inline bool ParseRocksLevelDbSst(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    (void)ext;
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
    const uint64_t footerMagic = DetailStructuredDataDoc::RdU64LE(footEnd - 8);
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
    const uint32_t formatVersion = isLevelDb ? 0u : DetailStructuredDataDoc::RdU32LE(footEnd - 12);
    const uint8_t* handles = isLevelDb ? footEnd - 48 : footEnd - 52; // 40 bytes either way
    size_t fpos = 0;
    uint64_t metaOff = 0, metaSz = 0, idxOff = 0, idxSz = 0;
    const bool handlesOk = DetailStructuredDataDoc::ReadVarint64(handles, 40, fpos, metaOff) &&
                           DetailStructuredDataDoc::ReadVarint64(handles, 40, fpos, metaSz) &&
                           DetailStructuredDataDoc::ReadVarint64(handles, 40, fpos, idxOff) &&
                           DetailStructuredDataDoc::ReadVarint64(handles, 40, fpos, idxSz);

    // A block's contents, decompressed; the 5-byte trailer (type + checksum) follows the stored bytes
    std::map<int, size_t> unreadByType;
    auto readBlock = [&](uint64_t off, uint64_t size, std::vector<uint8_t>& store, const uint8_t*& data, size_t& len) -> bool {
        if (size > 64u * 1024 * 1024) return false;
        std::vector<uint8_t> fileBytes;
        const uint8_t* src = readRange(off, size + 5, fileBytes);
        if (!src) return false;
        const int ctype = src[static_cast<size_t>(size)];
        if (ctype == 0) {
            if (!fileBytes.empty() && src == fileBytes.data()) {
                store = std::move(fileBytes);
                src = store.data();
            }
            data = src;
            len = static_cast<size_t>(size);
            return true;
        }
        bool ok = false;
        const size_t sz = static_cast<size_t>(size);
        if (ctype == 1) { // Snappy
            ok = ::Explorer2::StreamCodecs::SnappyDecompress(src, sz, store, 64 * 1024 * 1024);
        } else if (ctype == 2) { // zlib / raw deflate (RocksDB formatVersion >= 2 prefixes varint32 size)
            size_t p = 0;
            uint64_t expect = 0;
            const bool sized = formatVersion >= 2 && DetailStructuredDataDoc::ReadVarint64(src, sz, p, expect) && expect <= 64u * 1024 * 1024;
            if (sized && p < sz) {
                size_t cap = std::max<size_t>(static_cast<size_t>(expect) + 64u, 4096u);
                ok = ZipDeflate::InflateRaw(src + p, sz - p, store, cap) ||
                     ZipDeflate::InflateZlib(src + p, sz - p, store, cap);
            }
            if (!ok && sz > 0) {
                ok = ZipDeflate::InflateZlib(src, sz, store, 64 * 1024 * 1024) ||
                     ZipDeflate::InflateRaw(src, sz, store, 64 * 1024 * 1024);
            }
        } else if (ctype == 3) { // bzip2
            size_t p = 0;
            uint64_t expect = 0;
            const bool hasRawBz = (sz >= 3 && src[0] == 'B' && src[1] == 'Z' && src[2] == 'h');
            const bool sized = !hasRawBz && formatVersion >= 2 && DetailStructuredDataDoc::ReadVarint64(src, sz, p, expect) && expect <= 64u * 1024 * 1024;
            const size_t offB = sized ? p : 0;
            if (offB < sz) {
                ok = ::Explorer2::StreamCodecs::DecompressBzip2Stream(src + offB, sz - offB, store, 64 * 1024 * 1024);
            }
        } else if (ctype == 4 || ctype == 5 || ctype == 7) { // LZ4 / LZ4HC / ZSTD: format 2+ puts the size first
            size_t p = 0;
            uint64_t expect = 0;
            const bool isRawZstd = (ctype == 7 && sz >= 4 && DetailStructuredDataDoc::RdU32LE(src) == 0xFD2FB528u);
            const bool sized = !isRawZstd && formatVersion >= 2 && DetailStructuredDataDoc::ReadVarint64(src, sz, p, expect) && expect <= 64u * 1024 * 1024;
            if (ctype == 7) {
                ok = ::Explorer2::StreamCodecs::DecompressZstdFrame(src + (sized ? p : 0), sz - (sized ? p : 0), store, 64 * 1024 * 1024);
            } else if (sized) {
                ok = ::Explorer2::Lz4::DecodeBlockExact(src + p, sz - p, static_cast<size_t>(expect), store);
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

    auto entriesEnd = [](const uint8_t* b, size_t n, std::vector<uint32_t>* restarts) -> size_t {
        if (n < 4) return SIZE_MAX;
        uint32_t nr = DetailStructuredDataDoc::RdU32LE(b + n - 4);
        if ((nr & 0x80000000u) != 0 || nr == 0 || 4ull * (nr + 1) > n) return SIZE_MAX;
        size_t end = n - 4 * (static_cast<size_t>(nr) + 1);
        if (restarts) {
            restarts->resize(nr);
            for (uint32_t i = 0; i < nr; ++i) (*restarts)[i] = DetailStructuredDataDoc::RdU32LE(b + end + 4 * i);
        }
        return end;
    };

    auto decodeOpName = [](uint32_t type) -> const wchar_t* {
        static const struct { uint32_t t; const wchar_t* name; } kOps[] = {
            { 0x0, L"Delete" }, { 0x1, L"Put" }, { 0x2, L"Merge" }, { 0x7, L"SingleDelete" },
            { 0xF, L"RangeDelete" }, { 0x11, L"BlobIndex" }, { 0x14, L"DeleteWithTimestamp" }, { 0x16, L"WideColumns" },
        };
        for (const auto& o : kOps) {
            if (o.t == type) return o.name;
        }
        return nullptr;
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
            if (!DetailStructuredDataDoc::ReadVarint64(b, end, pos, shared) || !DetailStructuredDataDoc::ReadVarint64(b, end, pos, nonShared)) break;
            if (!valueDelta && !DetailStructuredDataDoc::ReadVarint64(b, end, pos, valLen)) break;
            if (nonShared > end - pos) break;
            pos += static_cast<size_t>(nonShared);
            uint64_t off = 0, size = 0;
            size_t vpos = pos;
            if (!valueDelta || restart) {
                if (!DetailStructuredDataDoc::ReadVarint64(b, end, vpos, off) || !DetailStructuredDataDoc::ReadVarint64(b, end, vpos, size)) break;
            } else {
                uint64_t zz = 0;
                if (!DetailStructuredDataDoc::ReadVarint64(b, end, vpos, zz)) break;
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
    auto printable = [](const uint8_t* p, size_t n) { return DetailStructuredDataDoc::IsMostlyPrintableUtf8(p, n); };
    size_t entries = 0, blocksRead = 0;

    if (isPlain && handlesOk) {
        // Resolve PlainTable data end offset & optional properties from metaindex
        uint64_t plainDataEnd = std::min<uint64_t>(metaOff, fileSize - footLen);
        uint64_t fixedKeyLen = 0;
        uint64_t plainEncType = 0; // 0 = kPlain, 1 = kPrefix
        {
            std::vector<uint8_t> mStore;
            const uint8_t* mb = nullptr;
            size_t mn = 0;
            if (metaSz > 0 && metaOff + metaSz + 5 <= fileSize && readBlock(metaOff, metaSz, mStore, mb, mn)) {
                size_t mend = entriesEnd(mb, mn, nullptr);
                std::string mkey;
                size_t mp = 0;
                uint64_t propOff = 0, propSz = 0;
                bool hasProp = false;
                while (mend != SIZE_MAX && mp < mend) {
                    uint64_t sh = 0, nsh = 0, vl = 0;
                    if (!DetailStructuredDataDoc::ReadVarint64(mb, mend, mp, sh) ||
                        !DetailStructuredDataDoc::ReadVarint64(mb, mend, mp, nsh) ||
                        !DetailStructuredDataDoc::ReadVarint64(mb, mend, mp, vl)) break;
                    if (sh > mkey.size() || nsh > mend - mp || vl > mend - mp - nsh) break;
                    mkey.resize(static_cast<size_t>(sh));
                    mkey.append(reinterpret_cast<const char*>(mb + mp), static_cast<size_t>(nsh));
                    mp += static_cast<size_t>(nsh);
                    size_t hp = mp;
                    uint64_t bo = 0, bs = 0;
                    if (DetailStructuredDataDoc::ReadVarint64(mb, mp + static_cast<size_t>(vl), hp, bo) &&
                        DetailStructuredDataDoc::ReadVarint64(mb, mp + static_cast<size_t>(vl), hp, bs)) {
                        if (bs > 0 && bo < plainDataEnd) plainDataEnd = bo;
                        if (mkey == "rocksdb.properties") { propOff = bo; propSz = bs; hasProp = true; }
                    }
                    mp += static_cast<size_t>(vl);
                }
                if (hasProp && propSz > 0 && propOff + propSz + 5 <= fileSize) {
                    std::vector<uint8_t> pStore;
                    const uint8_t* pb = nullptr;
                    size_t pn = 0;
                    if (readBlock(propOff, propSz, pStore, pb, pn)) {
                        size_t pend = entriesEnd(pb, pn, nullptr);
                        std::string pkey;
                        size_t pp = 0;
                        while (pend != SIZE_MAX && pp < pend) {
                            uint64_t sh = 0, nsh = 0, vl = 0;
                            if (!DetailStructuredDataDoc::ReadVarint64(pb, pend, pp, sh) ||
                                !DetailStructuredDataDoc::ReadVarint64(pb, pend, pp, nsh) ||
                                !DetailStructuredDataDoc::ReadVarint64(pb, pend, pp, vl)) break;
                            if (sh > pkey.size() || nsh > pend - pp || vl > pend - pp - nsh) break;
                            pkey.resize(static_cast<size_t>(sh));
                            pkey.append(reinterpret_cast<const char*>(pb + pp), static_cast<size_t>(nsh));
                            pp += static_cast<size_t>(nsh);
                            size_t vp = pp;
                            uint64_t pv = 0;
                            if (DetailStructuredDataDoc::ReadVarint64(pb, pp + static_cast<size_t>(vl), vp, pv)) {
                                if (pkey == "rocksdb.data.size" && pv <= fileSize) plainDataEnd = pv;
                                else if (pkey == "rocksdb.fixed.key.length") fixedKeyLen = pv;
                                else if (pkey == "rocksdb.plain.table.encoding.type") plainEncType = pv;
                            }
                            pp += static_cast<size_t>(vl);
                        }
                    }
                }
            }
        }

        const size_t scanLen = static_cast<size_t>(std::min<uint64_t>(plainDataEnd, 32ULL * 1024ULL * 1024ULL));
        std::vector<uint8_t> plainBuf;
        const uint8_t* pd = (scanLen > 0) ? readRange(0, scanLen, plainBuf) : nullptr;
        if (pd) {
            auto decodePlainSize = [](const uint8_t* p, size_t end, size_t& cur, uint8_t& outEntryType, uint64_t& outSz) -> bool {
                if (cur >= end) return false;
                uint8_t b0 = p[cur++];
                outEntryType = (b0 >> 6) & 0x03u;
                outSz = b0 & 0x1Fu;
                if (b0 & 0x20u) {
                    uint64_t hi = 0;
                    if (!DetailStructuredDataDoc::ReadVarint64(p, end, cur, hi)) return false;
                    outSz |= (hi << 5);
                }
                return true;
            };

            size_t pos = 0;
            std::string curPrefix;
            std::string userKeyBytes;
            while (pos < scanLen && entries < 100000) {
                std::wstring op = L"Put", seq = L"0";
                if (footerMagic == kLegacyPlainMagic) {
                    uint64_t ikeyLen = 0;
                    if (!DetailStructuredDataDoc::ReadVarint64(pd, scanLen, pos, ikeyLen) || ikeyLen < 8 || ikeyLen > scanLen - pos) break;
                    userKeyBytes.assign(reinterpret_cast<const char*>(pd + pos), static_cast<size_t>(ikeyLen - 8));
                    uint64_t tag = DetailStructuredDataDoc::RdU64LE(pd + pos + static_cast<size_t>(ikeyLen - 8));
                    if (const wchar_t* onm = decodeOpName(static_cast<uint32_t>(tag & 0xFFu))) op = onm;
                    seq = std::to_wstring(tag >> 8);
                    pos += static_cast<size_t>(ikeyLen);
                } else if (fixedKeyLen > 0) {
                    if (fixedKeyLen > scanLen - pos) break;
                    userKeyBytes.assign(reinterpret_cast<const char*>(pd + pos), static_cast<size_t>(fixedKeyLen));
                    pos += static_cast<size_t>(fixedKeyLen);
                    if (pos >= scanLen) break;
                    if ((pd[pos] & 0x80u) != 0) {
                        uint8_t t = pd[pos++] & 0x7Fu;
                        if (t != 0) { if (const wchar_t* onm = decodeOpName(t)) op = onm; }
                        seq = L"0";
                    } else {
                        if (pos + 8 > scanLen) break;
                        uint64_t tag = DetailStructuredDataDoc::RdU64LE(pd + pos);
                        pos += 8;
                        if (const wchar_t* onm = decodeOpName(static_cast<uint32_t>(tag & 0xFFu))) op = onm;
                        seq = std::to_wstring(tag >> 8);
                    }
                } else {
                    uint8_t et = 0;
                    uint64_t ksz = 0;
                    if (!decodePlainSize(pd, scanLen, pos, et, ksz) || ksz > scanLen - pos) break;
                    if (plainEncType == 1) { // kPrefix
                        if (et == 0) { // kFullKey: prefix or full key
                            curPrefix.assign(reinterpret_cast<const char*>(pd + pos), static_cast<size_t>(ksz));
                            pos += static_cast<size_t>(ksz);
                            userKeyBytes = curPrefix;
                            if (pos < scanLen && ((pd[pos] >> 6) & 0x03u) == 2u) {
                                uint8_t et2 = 0; uint64_t ssz = 0;
                                if (!decodePlainSize(pd, scanLen, pos, et2, ssz) || ssz > scanLen - pos) break;
                                userKeyBytes.append(reinterpret_cast<const char*>(pd + pos), static_cast<size_t>(ssz));
                                pos += static_cast<size_t>(ssz);
                            }
                        } else if (et == 1) { // kPrefixFromPreviousKey
                            userKeyBytes = curPrefix;
                            userKeyBytes.append(reinterpret_cast<const char*>(pd + pos), static_cast<size_t>(ksz));
                            pos += static_cast<size_t>(ksz);
                        } else {
                            break;
                        }
                    } else {
                        if (et != 0) break;
                        userKeyBytes.assign(reinterpret_cast<const char*>(pd + pos), static_cast<size_t>(ksz));
                        pos += static_cast<size_t>(ksz);
                    }
                    if (pos >= scanLen) break;
                    if ((pd[pos] & 0x80u) != 0) {
                        uint8_t t = pd[pos++] & 0x7Fu;
                        if (t != 0) { if (const wchar_t* onm = decodeOpName(t)) op = onm; }
                        seq = L"0";
                    } else {
                        if (pos + 8 > scanLen) break;
                        uint64_t tag = DetailStructuredDataDoc::RdU64LE(pd + pos);
                        pos += 8;
                        if (const wchar_t* onm = decodeOpName(static_cast<uint32_t>(tag & 0xFFu))) op = onm;
                        seq = std::to_wstring(tag >> 8);
                    }
                }

                uint64_t valLen = 0;
                if (!DetailStructuredDataDoc::ReadVarint64(pd, scanLen, pos, valLen) || valLen > scanLen - pos) break;
                const uint8_t* val = pd + pos;
                pos += static_cast<size_t>(valLen);
                ++entries;
                if (kvSh.rows.size() <= 5000) {
                    const uint8_t* uk = reinterpret_cast<const uint8_t*>(userKeyBytes.data());
                    size_t ulen = userKeyBytes.size();
                    std::wstring userKey = printable(uk, ulen)
                        ? PathUtils::Utf8ToWide(std::string_view(userKeyBytes.data(), ulen))
                        : DetailStructuredDataDoc::HexBytes(uk, ulen, 32);
                    const size_t vShow = std::min<size_t>(static_cast<size_t>(valLen), 160);
                    std::wstring valText = printable(val, vShow)
                        ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(val), vShow))
                        : DetailStructuredDataDoc::HexBytes(val, static_cast<size_t>(valLen), 24);
                    kvSh.rows.push_back({ std::move(userKey), std::move(op), std::move(seq), std::move(valText) });
                }
            }
        }
    }

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
            if (!DetailStructuredDataDoc::ReadVarint64(b, end, pos, shared) || !DetailStructuredDataDoc::ReadVarint64(b, end, pos, nonShared) ||
                !DetailStructuredDataDoc::ReadVarint64(b, end, pos, valLen)) break;
            if (shared > key.size() || nonShared > end - pos || valLen > end - pos - nonShared) break;
            key.resize(static_cast<size_t>(shared));
            key.append(reinterpret_cast<const char*>(b + pos), static_cast<size_t>(nonShared));
            pos += static_cast<size_t>(nonShared);
            const uint8_t* val = b + pos;
            pos += static_cast<size_t>(valLen);
            ++entries;
            if (kvSh.rows.size() > 5000) continue;

            std::wstring userKey, op = L"-", seq = L"-";
            size_t userLen = key.size();
            if (key.size() >= 8) {
                const uint64_t tag = DetailStructuredDataDoc::RdU64LE(reinterpret_cast<const uint8_t*>(key.data() + key.size() - 8));
                if (const wchar_t* onm = decodeOpName(static_cast<uint32_t>(tag & 0xFFu))) {
                    op = onm;
                    userLen = key.size() - 8;
                    seq = std::to_wstring(tag >> 8);
                }
            }
            const uint8_t* uk = reinterpret_cast<const uint8_t*>(key.data());
            userKey = printable(uk, userLen) ? PathUtils::Utf8ToWide(std::string_view(key.data(), userLen))
                                             : DetailStructuredDataDoc::HexBytes(uk, userLen, 32);
            const size_t vShow = std::min<size_t>(static_cast<size_t>(valLen), 160);
            std::wstring valText = printable(val, vShow) ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(val), vShow))
                                                         : DetailStructuredDataDoc::HexBytes(val, static_cast<size_t>(valLen), 24);
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
        hdrSh.rows.push_back({ L"Entries", std::to_wstring(entries) + (entries + 1 > kvSh.rows.size() ? L" (first " + std::to_wstring(kvSh.rows.size() - 1) + L" listed)" : L"") });
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
