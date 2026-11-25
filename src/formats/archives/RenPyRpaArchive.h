#pragma once
// RenPyRpaArchive.h — Ren'Py Visual Novel Archive (.rpa — RPA-3.0 / RPA-2.0) reader & extractor
// Parses RPA-3.0 `<16-hex-offset> <8-hex-key>\n` and RPA-2.0 `<16-hex-offset>\n` headers, inflates
// the zlib-compressed Python Pickle directory index via a strictly sandboxed, bounds-safe Pickle
// stack VM (protocols 0–5, zero code execution), de-obfuscates `(offset ^ key, length ^ key[, prefix])`
// file segments, validates safe relative paths, and extracts single/multi-segment files with prefix bytes.

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailRenPyRpa {

struct RpaChunk {
    uint64_t offset = 0;
    uint64_t length = 0;
    std::string prefix;
};

struct RpaParsedFile {
    std::wstring normPath;
    uint64_t totalSize = 0;
    std::vector<RpaChunk> chunks;
};

struct PVal {
    enum class Kind : uint8_t { None, Mark, Int, Bytes, List, Tuple, Dict };
    Kind kind = Kind::None;
    int64_t intVal = 0;
    std::string strVal;
    std::vector<PVal> seq;
    std::vector<std::pair<std::string, PVal>> dictItems;
};

// Unescapes Pickle protocol 0 quoted string literal (`'...'` or `"..."` with `\xNN`, `\n`, `\\`)
inline std::string UnescapePickleTextString(std::string_view s) {
    s = ReaderText::TrimSv(s);
    if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"'))) {
        s = s.substr(1, s.size() - 2);
    }
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == 't') out.push_back('\t');
            else if (n == '\\' || n == '\'' || n == '"') out.push_back(n);
            else if (n == 'x' && i + 2 < s.size()) {
                const int hi = ReaderText::HexVal(s[i + 1]);
                const int lo = ReaderText::HexVal(s[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                }
            } else {
                out.push_back(n);
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

inline bool DecodePickleIndexDict(const uint8_t* buf, size_t len, PVal& outRoot) {
    outRoot = {};
    if (!buf || len < 2 || len > 32u * 1024u * 1024u) return false;

    std::vector<PVal> stack;
    std::unordered_map<uint32_t, PVal> memo;
    uint32_t nextAutoMemo = 0;
    size_t pos = 0;
    size_t ops = 0;

    auto popToMark = [&](std::vector<PVal>& popped) -> bool {
        popped.clear();
        size_t mIdx = stack.size();
        while (mIdx > 0 && stack[mIdx - 1].kind != PVal::Kind::Mark) {
            --mIdx;
        }
        if (mIdx == 0) return false;
        popped.assign(
            std::make_move_iterator(stack.begin() + static_cast<ptrdiff_t>(mIdx)),
            std::make_move_iterator(stack.end()));
        stack.resize(mIdx - 1);
        return true;
    };

    auto readLine = [&](std::string_view& outLine) -> bool {
        const void* nlPtr = std::memchr(buf + pos, '\n', len - pos);
        if (!nlPtr) return false;
        const size_t nlOff = static_cast<size_t>(static_cast<const uint8_t*>(nlPtr) - buf);
        outLine = std::string_view(reinterpret_cast<const char*>(buf + pos), nlOff - pos);
        if (!outLine.empty() && outLine.back() == '\r') outLine.remove_suffix(1);
        pos = nlOff + 1u;
        return true;
    };

    while (pos < len) {
        if (++ops > 500000u || stack.size() > 65536u) return false;
        const uint8_t op = buf[pos++];

        switch (op) {
        case 0x80: // PROTO
            if (pos >= len || buf[pos] > 5u) return false;
            ++pos;
            break;
        case 0x95: // FRAME (8-byte length)
            if (len - pos < 8u) return false;
            pos += 8u;
            break;
        case '.': // STOP
            if (stack.size() != 1u || stack[0].kind != PVal::Kind::Dict) return false;
            outRoot = std::move(stack[0]);
            return true;

        case '(': { // MARK
            PVal m; m.kind = PVal::Kind::Mark;
            stack.push_back(std::move(m));
            break;
        }
        case '}': { // EMPTY_DICT
            PVal d; d.kind = PVal::Kind::Dict;
            stack.push_back(std::move(d));
            break;
        }
        case ']': { // EMPTY_LIST
            PVal l; l.kind = PVal::Kind::List;
            stack.push_back(std::move(l));
            break;
        }
        case ')': { // EMPTY_TUPLE
            PVal t; t.kind = PVal::Kind::Tuple;
            stack.push_back(std::move(t));
            break;
        }
        case 'N': // NONE
        case 0x88: // NEWTRUE
        case 0x89: { // NEWFALSE
            PVal n; n.kind = PVal::Kind::None;
            stack.push_back(std::move(n));
            break;
        }

        // Integers
        case 'K': { // BININT1
            if (pos >= len) return false;
            PVal v; v.kind = PVal::Kind::Int; v.intVal = buf[pos++];
            stack.push_back(std::move(v));
            break;
        }
        case 'M': { // BININT2
            if (len - pos < 2u) return false;
            PVal v; v.kind = PVal::Kind::Int; v.intVal = ByteIO::RdU16LE(buf + pos);
            pos += 2u;
            stack.push_back(std::move(v));
            break;
        }
        case 'J': { // BININT (i32LE)
            if (len - pos < 4u) return false;
            PVal v; v.kind = PVal::Kind::Int; v.intVal = static_cast<int32_t>(ByteIO::RdU32LE(buf + pos));
            pos += 4u;
            stack.push_back(std::move(v));
            break;
        }
        case 'I':
        case 'L': { // INT / LONG text line
            std::string_view ln;
            if (!readLine(ln)) return false;
            if (!ln.empty() && (ln.back() == 'L' || ln.back() == 'l')) ln.remove_suffix(1);
            int64_t iv = 0;
            if (!ReaderText::ParseInt64Sv(ln, iv)) return false;
            PVal v; v.kind = PVal::Kind::Int; v.intVal = iv;
            stack.push_back(std::move(v));
            break;
        }
        case 0x8A:
        case 0x8B: { // LONG1 / LONG4 (little-endian two's complement)
            uint32_t nBytes = 0;
            if (op == 0x8A) {
                if (pos >= len) return false;
                nBytes = buf[pos++];
            } else {
                if (len - pos < 4u) return false;
                nBytes = ByteIO::RdU32LE(buf + pos);
                pos += 4u;
            }
            if (nBytes > 8u || len - pos < nBytes) return false;
            uint64_t acc = 0;
            for (uint32_t k = 0; k < nBytes; ++k) {
                acc |= static_cast<uint64_t>(buf[pos + k]) << (k * 8u);
            }
            if (nBytes > 0 && nBytes < 8u && (buf[pos + nBytes - 1u] & 0x80u) != 0) {
                for (uint32_t k = nBytes; k < 8u; ++k) acc |= 0xFFULL << (k * 8u);
            }
            pos += nBytes;
            PVal v; v.kind = PVal::Kind::Int; v.intVal = static_cast<int64_t>(acc);
            stack.push_back(std::move(v));
            break;
        }

        // Strings & Bytes
        case 'U':
        case 'C':
        case 0x8C: { // 1-byte length string/bytes/unicode
            if (pos >= len) return false;
            const size_t slen = buf[pos++];
            if (len - pos < slen) return false;
            PVal v; v.kind = PVal::Kind::Bytes;
            v.strVal.assign(reinterpret_cast<const char*>(buf + pos), slen);
            pos += slen;
            stack.push_back(std::move(v));
            break;
        }
        case 'T':
        case 'X':
        case 'B': { // 4-byte length string/unicode/bytes
            if (len - pos < 4u) return false;
            const uint32_t slen = ByteIO::RdU32LE(buf + pos);
            pos += 4u;
            if (slen > 16u * 1024u * 1024u || len - pos < slen) return false;
            PVal v; v.kind = PVal::Kind::Bytes;
            v.strVal.assign(reinterpret_cast<const char*>(buf + pos), slen);
            pos += slen;
            stack.push_back(std::move(v));
            break;
        }
        case 0x8D:
        case 0x8E:
        case 0x96: { // 8-byte length unicode/bytes/bytearray
            if (len - pos < 8u) return false;
            const uint64_t slen = ByteIO::RdU64LE(buf + pos);
            pos += 8u;
            if (slen > 16ULL * 1024ULL * 1024ULL || slen > len - pos) return false;
            PVal v; v.kind = PVal::Kind::Bytes;
            v.strVal.assign(reinterpret_cast<const char*>(buf + pos), static_cast<size_t>(slen));
            pos += static_cast<size_t>(slen);
            stack.push_back(std::move(v));
            break;
        }
        case 'S':
        case 'V': { // Protocol 0 text/unicode line
            std::string_view ln;
            if (!readLine(ln)) return false;
            PVal v; v.kind = PVal::Kind::Bytes;
            v.strVal = (op == 'S') ? UnescapePickleTextString(ln) : std::string(ln);
            stack.push_back(std::move(v));
            break;
        }

        // Tuples & Lists & Dicts
        case 0x85:
        case 0x86:
        case 0x87: { // TUPLE1 / TUPLE2 / TUPLE3
            const size_t arity = static_cast<size_t>(op - 0x84u);
            if (stack.size() < arity) return false;
            PVal t; t.kind = PVal::Kind::Tuple;
            t.seq.reserve(arity);
            for (size_t k = stack.size() - arity; k < stack.size(); ++k) {
                if (stack[k].kind == PVal::Kind::Mark) return false;
                t.seq.push_back(std::move(stack[k]));
            }
            stack.resize(stack.size() - arity);
            stack.push_back(std::move(t));
            break;
        }
        case 't': { // TUPLE from MARK
            PVal t; t.kind = PVal::Kind::Tuple;
            if (!popToMark(t.seq)) return false;
            stack.push_back(std::move(t));
            break;
        }
        case 'l': { // LIST from MARK
            PVal l; l.kind = PVal::Kind::List;
            if (!popToMark(l.seq)) return false;
            stack.push_back(std::move(l));
            break;
        }
        case 'd': { // DICT from MARK
            std::vector<PVal> kv;
            if (!popToMark(kv) || (kv.size() & 1u) != 0) return false;
            PVal d; d.kind = PVal::Kind::Dict;
            for (size_t k = 0; k < kv.size(); k += 2u) {
                if (kv[k].kind != PVal::Kind::Bytes) return false;
                d.dictItems.emplace_back(std::move(kv[k].strVal), std::move(kv[k + 1u]));
            }
            stack.push_back(std::move(d));
            break;
        }
        case 'a': { // APPEND
            if (stack.size() < 2u || stack[stack.size() - 2u].kind != PVal::Kind::List) return false;
            PVal item = std::move(stack.back());
            stack.pop_back();
            stack.back().seq.push_back(std::move(item));
            break;
        }
        case 'e': { // APPENDS
            std::vector<PVal> items;
            if (!popToMark(items) || stack.empty() || stack.back().kind != PVal::Kind::List) return false;
            for (auto& it : items) stack.back().seq.push_back(std::move(it));
            break;
        }
        case 's': { // SETITEM
            if (stack.size() < 3u) return false;
            PVal val = std::move(stack.back()); stack.pop_back();
            PVal key = std::move(stack.back()); stack.pop_back();
            if (stack.back().kind != PVal::Kind::Dict || key.kind != PVal::Kind::Bytes) return false;
            stack.back().dictItems.emplace_back(std::move(key.strVal), std::move(val));
            break;
        }
        case 'u': { // SETITEMS
            std::vector<PVal> kv;
            if (!popToMark(kv) || (kv.size() & 1u) != 0 || stack.empty() || stack.back().kind != PVal::Kind::Dict) {
                return false;
            }
            for (size_t k = 0; k < kv.size(); k += 2u) {
                if (kv[k].kind != PVal::Kind::Bytes) return false;
                stack.back().dictItems.emplace_back(std::move(kv[k].strVal), std::move(kv[k + 1u]));
            }
            break;
        }

        // Memoization
        case 'q': { // BINPUT
            if (pos >= len || stack.empty()) return false;
            const uint32_t idx = buf[pos++];
            if (memo.size() < 65536u) memo[idx] = stack.back();
            nextAutoMemo = std::max(nextAutoMemo, idx + 1u);
            break;
        }
        case 'r': { // LONG_BINPUT
            if (len - pos < 4u || stack.empty()) return false;
            const uint32_t idx = ByteIO::RdU32LE(buf + pos);
            pos += 4u;
            if (memo.size() < 65536u) memo[idx] = stack.back();
            nextAutoMemo = std::max(nextAutoMemo, idx + 1u);
            break;
        }
        case 0x94: { // MEMOIZE
            if (stack.empty()) return false;
            if (memo.size() < 65536u) memo[nextAutoMemo++] = stack.back();
            break;
        }
        case 'p': { // PUT
            std::string_view ln;
            uint64_t idx = 0;
            if (!readLine(ln) || !ReaderText::ParseUint64Sv(ln, idx, 10) || stack.empty()) return false;
            if (idx < 65536ULL) memo[static_cast<uint32_t>(idx)] = stack.back();
            break;
        }
        case 'h': { // BINGET
            if (pos >= len) return false;
            auto it = memo.find(buf[pos++]);
            if (it == memo.end()) return false;
            stack.push_back(it->second);
            break;
        }
        case 'j': { // LONG_BINGET
            if (len - pos < 4u) return false;
            const uint32_t idx = ByteIO::RdU32LE(buf + pos);
            pos += 4u;
            auto it = memo.find(idx);
            if (it == memo.end()) return false;
            stack.push_back(it->second);
            break;
        }
        case 'g': { // GET
            std::string_view ln;
            uint64_t idx = 0;
            if (!readLine(ln) || !ReaderText::ParseUint64Sv(ln, idx, 10)) return false;
            auto it = memo.find(static_cast<uint32_t>(idx));
            if (it == memo.end()) return false;
            stack.push_back(it->second);
            break;
        }
        case '0': { // POP
            if (stack.empty() || stack.back().kind == PVal::Kind::Mark) return false;
            stack.pop_back();
            break;
        }

        default:
            // Refuse GLOBAL / REDUCE / BUILD / arbitrary object construction opcodes
            return false;
        }
    }
    return false;
}

inline bool ParseRenPyRpaIndex(
    const uint8_t* data,
    size_t size,
    std::wstring& outFormatName,
    std::vector<RpaParsedFile>& outFiles)
{
    outFiles.clear();
    if (!data || size < 26) return false;

    const bool isRpa3 = (std::memcmp(data, "RPA-3.0 ", 8) == 0);
    const bool isRpa2 = !isRpa3 && (std::memcmp(data, "RPA-2.0 ", 8) == 0);
    if (!isRpa3 && !isRpa2) return false;

    // Header line is ASCII up to '\n' within first 128 bytes
    const size_t scanLim = std::min<size_t>(size, 128u);
    const void* nlPtr = std::memchr(data, '\n', scanLim);
    if (!nlPtr) return false;
    const size_t hdrLen = static_cast<size_t>(static_cast<const uint8_t*>(nlPtr) - data);
    std::string_view hdrLine(reinterpret_cast<const char*>(data + 8), hdrLen - 8u);

    std::string_view offTok, keyTok;
    if (!ReaderText::NextTokenSv(hdrLine, offTok)) return false;
    uint64_t indexOffset = 0;
    uint64_t xorKey = 0;
    if (!ReaderText::ParseUint64Sv(offTok, indexOffset, 16)) return false;
    if (isRpa3) {
        if (!ReaderText::NextTokenSv(hdrLine, keyTok) || !ReaderText::ParseUint64Sv(keyTok, xorKey, 16)) {
            return false;
        }
    }

    if (indexOffset <= hdrLen || indexOffset >= size) return false;
    const size_t compIdxSize = size - static_cast<size_t>(indexOffset);
    if (compIdxSize > 16u * 1024u * 1024u) return false;

    std::vector<uint8_t> pickleBytes;
    if (!ZipDeflate::InflateZlib(data + static_cast<size_t>(indexOffset), compIdxSize, pickleBytes, 32u * 1024u * 1024u)) {
        return false;
    }

    PVal rootDict;
    if (!DecodePickleIndexDict(pickleBytes.data(), pickleBytes.size(), rootDict)) return false;

    for (const auto& kv : rootDict.dictItems) {
        if (!ReaderText::IsSafeRelativeSubFile(kv.first)) continue;
        std::wstring normP = ArchivePath::Normalize(PathUtils::Utf8ToWide(kv.first));
        if (normP.empty()) continue;

        const PVal& segList = kv.second;
        if (segList.kind != PVal::Kind::List && segList.kind != PVal::Kind::Tuple) return false;
        if (segList.seq.empty()) continue;

        RpaParsedFile pf;
        pf.normPath = std::move(normP);
        for (const PVal& tup : segList.seq) {
            if ((tup.kind != PVal::Kind::Tuple && tup.kind != PVal::Kind::List) || tup.seq.size() < 2u) {
                return false;
            }
            if (tup.seq[0].kind != PVal::Kind::Int || tup.seq[1].kind != PVal::Kind::Int) {
                return false;
            }
            const uint64_t realOff = static_cast<uint64_t>(tup.seq[0].intVal) ^ xorKey;
            const uint64_t realLen = static_cast<uint64_t>(tup.seq[1].intVal) ^ xorKey;
            std::string pfx;
            if (tup.seq.size() >= 3u && tup.seq[2].kind == PVal::Kind::Bytes) {
                pfx = tup.seq[2].strVal;
            }
            // Per Ren'Py renpy/loader.py: `dlen` (`realLen`) is the total segment byte length including `start` (`pfx`),
            // so `pfx` supplies the first `pfx.size()` bytes and `realLen - pfx.size()` bytes are read at `realOff`.
            if (!pfx.empty() && realLen < pfx.size()) return false;
            const uint64_t sliceBytes = realLen - pfx.size();
            if (realOff > size || sliceBytes > size - realOff) return false;
            if (pf.totalSize > (256ULL * 1024ULL * 1024ULL) - realLen) return false;

            pf.totalSize += realLen;
            pf.chunks.push_back({ realOff, sliceBytes, std::move(pfx) });
        }
        outFiles.push_back(std::move(pf));
        if (outFiles.size() >= 65536u) break;
    }

    if (outFiles.empty()) return false;
    outFormatName = isRpa3 ? L"Ren'Py Archive (RPA-3.0)" : L"Ren'Py Archive (RPA-2.0)";
    return true;
}

} // namespace DetailRenPyRpa

inline bool InspectRenPyRpaArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    std::wstring fmtName;
    std::vector<DetailRenPyRpa::RpaParsedFile> files;
    if (!DetailRenPyRpa::ParseRenPyRpaIndex(data, size, fmtName, files)) return false;

    outInfo.formatName = std::move(fmtName);
    outInfo.entries.reserve(files.size());
    for (const auto& f : files) {
        ArchiveItemEntry item;
        item.pathInArchive = f.normPath;
        item.name = ArchivePath::BaseName(f.normPath);
        item.uncompressedSize = f.totalSize;
        item.compressedSize = f.totalSize;
        item.rawDataOffset = f.chunks.front().offset;
        item.compressionMethod = (f.chunks.size() == 1u && f.chunks[0].prefix.empty()) ? 0 : 8;
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractRenPyRpaEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    std::wstring fmtName;
    std::vector<DetailRenPyRpa::RpaParsedFile> files;
    if (!DetailRenPyRpa::ParseRenPyRpaIndex(data, size, fmtName, files)) return false;

    for (const auto& f : files) {
        if (DetailDiskAndGameArc::PathEqualsCI(f.normPath, normTarget)) {
            if (f.totalSize > 128ULL * 1024ULL * 1024ULL) return false;
            outBytes.reserve(static_cast<size_t>(f.totalSize));
            for (const auto& ch : f.chunks) {
                if (!ch.prefix.empty()) {
                    outBytes.insert(outBytes.end(),
                        reinterpret_cast<const uint8_t*>(ch.prefix.data()),
                        reinterpret_cast<const uint8_t*>(ch.prefix.data()) + ch.prefix.size());
                }
                if (ch.offset > size || ch.length > size - ch.offset) {
                    outBytes.clear();
                    return false;
                }
                outBytes.insert(outBytes.end(),
                    data + static_cast<size_t>(ch.offset),
                    data + static_cast<size_t>(ch.offset + ch.length));
            }
            return true;
        }
    }
    return false;
}

} // namespace Explorer2::Formats
