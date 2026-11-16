#pragma once
// BitTorrent.h — BEP-0003 Recursive Bencode Parser + CNG SHA-1 info_hash + Magnet URI & Multi-File Manifest

namespace Formats {

namespace TorrentDetail {

struct BencodeNode {
    enum class Kind { Int, Bytes, List, Dict };
    Kind kind = Kind::Int;
    int64_t intVal = 0;
    std::string_view bytesVal;
    std::vector<BencodeNode> listVal;
    std::vector<std::pair<std::string_view, BencodeNode>> dictVal;

    const BencodeNode* FindKey(std::string_view key) const {
        if (kind != Kind::Dict) return nullptr;
        for (const auto& kv : dictVal) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

struct BencodeParseCtx {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    size_t infoSpanStart = 0;
    size_t infoSpanEnd = 0;
    bool   hasInfoDict = false;

    bool ParseString(std::string_view& outStr) {
        if (pos >= size || data[pos] < '0' || data[pos] > '9') return false;
        const size_t digitsStart = pos;
        uint64_t len = 0;
        while (pos < size && data[pos] >= '0' && data[pos] <= '9') {
            if (pos > digitsStart && data[digitsStart] == '0') return false; // reject leading zero like "03:abc"
            len = len * 10 + static_cast<uint64_t>(data[pos] - '0');
            if (len > size) return false;
            ++pos;
        }
        if (pos >= size || data[pos] != ':') return false;
        ++pos; // skip ':'
        if (pos + static_cast<size_t>(len) > size) return false;
        outStr = std::string_view(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(len));
        pos += static_cast<size_t>(len);
        return true;
    }

    bool ParseNode(BencodeNode& outNode, int depth) {
        if (depth > 32 || pos >= size) return false;
        const uint8_t tag = data[pos];

        if (tag == 'i') {
            ++pos;
            if (pos >= size) return false;
            bool neg = false;
            if (data[pos] == '-') {
                neg = true;
                ++pos;
                if (pos >= size || data[pos] == '0') return false; // reject "i-0e" and "i-03e"
            }
            if (pos >= size || data[pos] < '0' || data[pos] > '9') return false;
            const size_t dStart = pos;
            uint64_t mag = 0;
            while (pos < size && data[pos] >= '0' && data[pos] <= '9') {
                if (pos > dStart && data[dStart] == '0') return false; // reject "i03e"
                const uint64_t next = mag * 10 + static_cast<uint64_t>(data[pos] - '0');
                if (next < mag) return false;
                mag = next;
                ++pos;
            }
            if (pos >= size || data[pos] != 'e') return false;
            ++pos;
            if (!neg && mag > 9223372036854775807ULL) return false;
            if (neg && mag > 9223372036854775808ULL) return false;
            outNode.kind = BencodeNode::Kind::Int;
            outNode.intVal = neg
                ? (mag == 9223372036854775808ULL ? (-9223372036854775807LL - 1LL) : -static_cast<int64_t>(mag))
                : static_cast<int64_t>(mag);
            return true;
        }

        if (tag >= '0' && tag <= '9') {
            outNode.kind = BencodeNode::Kind::Bytes;
            return ParseString(outNode.bytesVal);
        }

        if (tag == 'l') {
            ++pos;
            outNode.kind = BencodeNode::Kind::List;
            while (pos < size && data[pos] != 'e') {
                BencodeNode elem;
                if (!ParseNode(elem, depth + 1)) return false;
                outNode.listVal.push_back(std::move(elem));
            }
            if (pos >= size || data[pos] != 'e') return false;
            ++pos;
            return true;
        }

        if (tag == 'd') {
            ++pos;
            outNode.kind = BencodeNode::Kind::Dict;
            while (pos < size && data[pos] != 'e') {
                std::string_view key;
                if (!ParseString(key)) return false;
                const size_t valStart = pos;
                BencodeNode val;
                if (!ParseNode(val, depth + 1)) return false;
                const size_t valEnd = pos;
                if (depth == 0 && key == "info" && val.kind == BencodeNode::Kind::Dict) {
                    infoSpanStart = valStart;
                    infoSpanEnd = valEnd;
                    hasInfoDict = true;
                }
                outNode.dictVal.emplace_back(key, std::move(val));
            }
            if (pos >= size || data[pos] != 'e') return false;
            ++pos;
            return true;
        }

        return false;
    }
};

inline bool ComputeSha1Hex(const uint8_t* data, size_t len, std::wstring& outHex) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    uint8_t digest[20] = {};
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) >= 0) {
            if (BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0) >= 0 &&
                BCryptFinishHash(hHash, digest, 20, 0) >= 0)
            {
                ok = true;
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    if (!ok) return false;

    static const wchar_t kHex[] = L"0123456789abcdef";
    outHex.clear();
    outHex.reserve(40);
    for (int i = 0; i < 20; ++i) {
        outHex.push_back(kHex[(digest[i] >> 4) & 0x0Fu]);
        outHex.push_back(kHex[digest[i] & 0x0Fu]);
    }
    return true;
}

inline std::wstring UrlEncodeUtf8(std::string_view sv) {
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(sv.size() * 2);
    for (unsigned char c : sv) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<wchar_t>(c));
        } else {
            out.push_back(L'%');
            out.push_back(kHex[(c >> 4) & 0x0Fu]);
            out.push_back(kHex[c & 0x0Fu]);
        }
    }
    return out;
}

inline std::wstring FormatBytesHuman(uint64_t bytes) {
    wchar_t buf[96] = {};
    if (bytes >= (1ULL << 30)) {
        swprintf_s(buf, L"%.2f GiB (%llu B)", static_cast<double>(bytes) / 1073741824.0, static_cast<unsigned long long>(bytes));
    } else if (bytes >= (1ULL << 20)) {
        swprintf_s(buf, L"%.2f MiB (%llu B)", static_cast<double>(bytes) / 1048576.0, static_cast<unsigned long long>(bytes));
    } else if (bytes >= (1ULL << 10)) {
        swprintf_s(buf, L"%.2f KiB (%llu B)", static_cast<double>(bytes) / 1024.0, static_cast<unsigned long long>(bytes));
    } else {
        swprintf_s(buf, L"%llu B", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

inline std::wstring FormatUnixUtc(int64_t unixSec) {
    if (unixSec <= 0) return std::to_wstring(unixSec);
    const uint64_t ftTicks = (static_cast<uint64_t>(unixSec) + 11644473600ULL) * 10000000ULL;
    FILETIME ft{};
    ft.dwLowDateTime  = static_cast<DWORD>(ftTicks & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(ftTicks >> 32);
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st)) return std::to_wstring(unixSec);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u UTC",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

} // namespace TorrentDetail

inline bool ParseBitTorrentDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 11 || raw[0] != 'd') return false;

    TorrentDetail::BencodeParseCtx ctx;
    ctx.data = raw.data();
    ctx.size = raw.size();

    TorrentDetail::BencodeNode root;
    if (!ctx.ParseNode(root, 0) || root.kind != TorrentDetail::BencodeNode::Kind::Dict) return false;
    // Allow optional trailing CR/LF whitespace, reject arbitrary trailing garbage
    while (ctx.pos < ctx.size && (raw[ctx.pos] == '\r' || raw[ctx.pos] == '\n')) ++ctx.pos;
    if (ctx.pos != ctx.size || !ctx.hasInfoDict || ctx.infoSpanEnd <= ctx.infoSpanStart) return false;

    const auto* infoDict = root.FindKey("info");
    if (!infoDict || infoDict->kind != TorrentDetail::BencodeNode::Kind::Dict) return false;

    const auto* pieceLenNode = infoDict->FindKey("piece length");
    if (!pieceLenNode || pieceLenNode->kind != TorrentDetail::BencodeNode::Kind::Int || pieceLenNode->intVal <= 0) {
        return false;
    }

    const auto* piecesNode = infoDict->FindKey("pieces");
    const auto* metaVerNode = infoDict->FindKey("meta version");
    const bool isV2 = (metaVerNode && metaVerNode->kind == TorrentDetail::BencodeNode::Kind::Int && metaVerNode->intVal == 2);
    uint64_t pieceCount = 0;
    if (piecesNode) {
        if (piecesNode->kind != TorrentDetail::BencodeNode::Kind::Bytes ||
            piecesNode->bytesVal.empty() || (piecesNode->bytesVal.size() % 20u) != 0)
        {
            return false;
        }
        pieceCount = piecesNode->bytesVal.size() / 20u;
    } else if (!isV2) {
        return false;
    }

    std::wstring infoHashHex;
    if (!TorrentDetail::ComputeSha1Hex(raw.data() + ctx.infoSpanStart, ctx.infoSpanEnd - ctx.infoSpanStart, infoHashHex)) {
        return false;
    }

    std::string_view torrentNameSv;
    if (const auto* nm = infoDict->FindKey("name"); nm && nm->kind == TorrentDetail::BencodeNode::Kind::Bytes) {
        torrentNameSv = nm->bytesVal;
    }
    const std::wstring torrentName = torrentNameSv.empty() ? L"(Unnamed Torrent)" : PathUtils::Utf8ToWide(torrentNameSv);

    // Collect single-file or multi-file manifest
    struct FileRow {
        std::wstring path;
        uint64_t     length = 0;
    };
    std::vector<FileRow> fileManifest;
    uint64_t totalBytes = 0;
    bool isMultiFile = false;

    if (const auto* filesNode = infoDict->FindKey("files"); filesNode && filesNode->kind == TorrentDetail::BencodeNode::Kind::List) {
        isMultiFile = true;
        if (filesNode->listVal.empty()) return false;
        for (const auto& fItem : filesNode->listVal) {
            if (fItem.kind != TorrentDetail::BencodeNode::Kind::Dict) return false;
            const auto* fLen = fItem.FindKey("length");
            const auto* fPath = fItem.FindKey("path");
            if (!fLen || fLen->kind != TorrentDetail::BencodeNode::Kind::Int || fLen->intVal < 0) return false;
            if (!fPath || fPath->kind != TorrentDetail::BencodeNode::Kind::List || fPath->listVal.empty()) return false;

            std::wstring joined;
            for (const auto& seg : fPath->listVal) {
                if (seg.kind != TorrentDetail::BencodeNode::Kind::Bytes) return false;
                if (!joined.empty()) joined += L"/";
                joined += PathUtils::Utf8ToWide(seg.bytesVal);
            }
            const uint64_t ulen = static_cast<uint64_t>(fLen->intVal);
            totalBytes += ulen;
            if (fileManifest.size() < 5000) fileManifest.push_back({ std::move(joined), ulen });
        }
    } else if (const auto* lenNode = infoDict->FindKey("length"); lenNode && lenNode->kind == TorrentDetail::BencodeNode::Kind::Int && lenNode->intVal >= 0) {
        totalBytes = static_cast<uint64_t>(lenNode->intVal);
        fileManifest.push_back({ torrentName, totalBytes });
    } else if (!isV2) {
        return false;
    }

    std::wstring magnetUri = L"magnet:?xt=urn:btih:" + infoHashHex;
    if (!torrentNameSv.empty()) {
        magnetUri += L"&dn=" + TorrentDetail::UrlEncodeUtf8(torrentNameSv);
    }
    if (const auto* ann = root.FindKey("announce"); ann && ann->kind == TorrentDetail::BencodeNode::Kind::Bytes && !ann->bytesVal.empty()) {
        magnetUri += L"&tr=" + TorrentDetail::UrlEncodeUtf8(ann->bytesVal);
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"BitTorrent Bencoded Metainfo (.torrent)";

    SpreadsheetSheet metaSheet;
    metaSheet.name = L"Torrent_Metainfo";
    metaSheet.maxCols = 2;
    metaSheet.rows.push_back({ L"Field", L"Value" });
    metaSheet.rows.push_back({ L"Name", torrentName });
    metaSheet.rows.push_back({ L"Info Hash (SHA-1)", infoHashHex });
    metaSheet.rows.push_back({ L"Magnet URI", magnetUri });
    metaSheet.rows.push_back({ L"Mode", isMultiFile ? (L"Multi-File (" + std::to_wstring(fileManifest.size()) + L" files)") : L"Single-File" });
    metaSheet.rows.push_back({ L"Total Size", TorrentDetail::FormatBytesHuman(totalBytes) });
    metaSheet.rows.push_back({ L"Piece Length", TorrentDetail::FormatBytesHuman(static_cast<uint64_t>(pieceLenNode->intVal)) });
    if (piecesNode) {
        metaSheet.rows.push_back({ L"Pieces (SHA-1)", std::to_wstring(pieceCount) + L" pieces (" + std::to_wstring(piecesNode->bytesVal.size()) + L" B)" });
    }

    if (const auto* priv = infoDict->FindKey("private"); priv && priv->kind == TorrentDetail::BencodeNode::Kind::Int) {
        metaSheet.rows.push_back({ L"Private Tracker", priv->intVal != 0 ? L"Yes (private=1)" : L"No (DHT/PEX allowed)" });
    }
    if (const auto* ann = root.FindKey("announce"); ann && ann->kind == TorrentDetail::BencodeNode::Kind::Bytes) {
        metaSheet.rows.push_back({ L"Primary Tracker (announce)", PathUtils::Utf8ToWide(ann->bytesVal) });
    }
    if (const auto* annList = root.FindKey("announce-list"); annList && annList->kind == TorrentDetail::BencodeNode::Kind::List) {
        int tierIdx = 1;
        for (const auto& tier : annList->listVal) {
            if (tier.kind != TorrentDetail::BencodeNode::Kind::List) continue;
            std::wstring urls;
            for (const auto& u : tier.listVal) {
                if (u.kind != TorrentDetail::BencodeNode::Kind::Bytes) continue;
                if (!urls.empty()) urls += L", ";
                urls += PathUtils::Utf8ToWide(u.bytesVal);
            }
            if (!urls.empty()) {
                metaSheet.rows.push_back({ L"Tracker Tier " + std::to_wstring(tierIdx++), urls });
            }
        }
    }
    if (const auto* cdate = root.FindKey("creation date"); cdate && cdate->kind == TorrentDetail::BencodeNode::Kind::Int) {
        metaSheet.rows.push_back({ L"Creation Date", TorrentDetail::FormatUnixUtc(cdate->intVal) });
    }
    if (const auto* cby = root.FindKey("created by"); cby && cby->kind == TorrentDetail::BencodeNode::Kind::Bytes) {
        metaSheet.rows.push_back({ L"Created By", PathUtils::Utf8ToWide(cby->bytesVal) });
    }
    if (const auto* comm = root.FindKey("comment"); comm && comm->kind == TorrentDetail::BencodeNode::Kind::Bytes) {
        metaSheet.rows.push_back({ L"Comment", PathUtils::Utf8ToWide(comm->bytesVal) });
    }
    out.sheets.push_back(std::move(metaSheet));

    if (!fileManifest.empty()) {
        SpreadsheetSheet filesSheet;
        filesSheet.name = L"Files";
        filesSheet.maxCols = 4;
        filesSheet.rows.push_back({ L"#", L"Relative Path", L"Size (Bytes)", L"Formatted Size" });
        for (size_t i = 0; i < fileManifest.size(); ++i) {
            filesSheet.rows.push_back({
                std::to_wstring(i + 1),
                fileManifest[i].path,
                std::to_wstring(fileManifest[i].length),
                TorrentDetail::FormatBytesHuman(fileManifest[i].length)
            });
        }
        out.sheets.push_back(std::move(filesSheet));
    }

    out.valid = true;
    return true;
}

} // namespace Formats
