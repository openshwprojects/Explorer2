#pragma once
// ChmHelp.h — Microsoft Compiled HTML Help (.chm) Reader

namespace Formats {
namespace DetailMedia100 {

// ============================================================================
// 12. Microsoft Compiled HTML Help (.chm — ITSF / ITSP / PMGL, section 1 LZX-decoded)
// ============================================================================
inline bool ReadChmEncInt(const uint8_t*& p, const uint8_t* end, uint64_t& outVal) {
    outVal = 0;
    int steps = 0;
    while (p < end && steps++ < 10) {
        uint8_t b = *p++;
        outVal = (outVal << 7) | static_cast<uint64_t>(b & 0x7Fu);
        if ((b & 0x80u) == 0) return true;
    }
    return false;
}

inline bool ParseChmHelp(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 88 || std::memcmp(raw.data(), "ITSF", 4) != 0) return false;

    uint32_t itsfVer = DetailSys100::RdU32LE(raw.data() + 4);
    uint32_t hdrLen  = DetailSys100::RdU32LE(raw.data() + 8);
    uint32_t langId  = DetailSys100::RdU32LE(raw.data() + 20);
    if (itsfVer < 2 || itsfVer > 4) return false;

    uint64_t dirOff  = DetailSys100::RdU64LE(raw.data() + 72);
    uint64_t dirLen  = DetailSys100::RdU64LE(raw.data() + 80);
    uint64_t dataOff = (itsfVer >= 3 && hdrLen >= 96 && raw.size() >= 96)
        ? DetailSys100::RdU64LE(raw.data() + 88)
        : (dirOff + dirLen);

    if (dirOff >= raw.size() || dirOff + 84 > raw.size() || std::memcmp(raw.data() + static_cast<size_t>(dirOff), "ITSP", 4) != 0) {
        return false;
    }

    const uint8_t* itsp = raw.data() + static_cast<size_t>(dirOff);
    uint32_t itspVer      = DetailSys100::RdU32LE(itsp + 4);
    uint32_t itspHdrLen   = DetailSys100::RdU32LE(itsp + 8);
    uint32_t dirBlockSize = DetailSys100::RdU32LE(itsp + 16);
    uint32_t numDirBlocks = DetailSys100::RdU32LE(itsp + 48);
    if (dirBlockSize < 64 || dirBlockSize > 1048576 || itspHdrLen < 84) return false;

    struct ChmEntry {
        std::wstring path;
        uint64_t section = 0;
        uint64_t offset = 0;
        uint64_t length = 0;
    };
    std::vector<ChmEntry> entries;

    size_t blocksStart = static_cast<size_t>(dirOff) + itspHdrLen;
    size_t maxBlocks = std::min<size_t>(numDirBlocks > 0 ? numDirBlocks : 1024, 4096);
    for (size_t b = 0; b < maxBlocks; ++b) {
        size_t bOff = blocksStart + b * dirBlockSize;
        if (bOff + 20 > raw.size()) break;
        size_t blkAvail = std::min<size_t>(dirBlockSize, raw.size() - bOff);
        const uint8_t* blk = raw.data() + bOff;
        if (std::memcmp(blk, "PMGL", 4) != 0) continue;

        uint32_t freeSpace = DetailSys100::RdU32LE(blk + 4);
        const uint8_t* p = blk + 20;
        const uint8_t* pEnd = (freeSpace <= blkAvail - 20) ? (blk + blkAvail - freeSpace) : (blk + blkAvail);

        while (p < pEnd && entries.size() < 4096) {
            uint64_t nameLen = 0, sec = 0, off = 0, len = 0;
            if (!ReadChmEncInt(p, pEnd, nameLen) || nameLen == 0 || nameLen > static_cast<uint64_t>(pEnd - p)) break;
            std::wstring path = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), static_cast<size_t>(nameLen)));
            p += static_cast<size_t>(nameLen);
            if (!ReadChmEncInt(p, pEnd, sec) || !ReadChmEncInt(p, pEnd, off) || !ReadChmEncInt(p, pEnd, len)) break;
            entries.push_back({ std::move(path), sec, off, len });
        }
    }

    // Extract uncompressed Section 0 preview text (e.g. /#STRINGS or uncompressed HTML topic)
    std::vector<std::wstring> sec0Previews;
    for (const auto& e : entries) {
        if (e.section == 0 && e.length > 0 && dataOff + e.offset + e.length <= raw.size()) {
            const uint8_t* sp = raw.data() + static_cast<size_t>(dataOff + e.offset);
            size_t slen = static_cast<size_t>(std::min<uint64_t>(e.length, 4096));
            if (e.path == L"/#STRINGS") {
                size_t idx = 1;
                while (idx < slen && sec0Previews.size() < 8) {
                    size_t nul = idx;
                    while (nul < slen && sp[nul] != 0) ++nul;
                    if (nul > idx) {
                        std::wstring s = PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sp + idx), nul - idx)));
                        if (s.size() >= 2) sec0Previews.push_back(L"#STRINGS: " + s);
                    }
                    idx = nul + 1;
                }
            } else if (!e.path.empty() && e.path[0] == L'/' && e.path.find(L".htm") != std::wstring::npos && sec0Previews.size() < 8) {
                auto ps = StripHtmlToParagraphs(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sp), slen)), 4);
                for (const auto& p : ps) sec0Previews.push_back(e.path + L": " + p);
            }
        }
    }

    // Section 1 ("MSCompressed") is one LZX stream holding the topics: window and reset interval come from its LZXC
    // control data, the compressed offset of every 32 KB frame from its reset table. The stream (whole-file access:
    // a help file may be larger than the bytes handed in) is decoded up to 32 MB.
    SourceBytes src(raw, out);
    auto findSec0 = [&](std::wstring_view path) -> const ChmEntry* {
        for (const auto& e : entries) if (e.section == 0 && e.path == path) return &e;
        return nullptr;
    };
    auto sec0Bytes = [&](const ChmEntry* e, std::vector<uint8_t>& buf, uint64_t maxLen) -> const uint8_t* {
        if (!e || e->length == 0 || e->length > maxLen) return nullptr;
        return src.Read(dataOff + e->offset, static_cast<size_t>(e->length), buf);
    };
    const ChmEntry* ctlE = findSec0(L"::DataSpace/Storage/MSCompressed/ControlData");
    const ChmEntry* rtE = findSec0(L"::DataSpace/Storage/MSCompressed/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable");
    const ChmEntry* contE = findSec0(L"::DataSpace/Storage/MSCompressed/Content");
    std::vector<uint8_t> ctlBuf, rtBuf, contBuf, sec1;
    const uint8_t* ctl = sec0Bytes(ctlE, ctlBuf, 4096);
    const uint8_t* rt = sec0Bytes(rtE, rtBuf, 16u << 20);
    const uint8_t* cont = sec0Bytes(contE, contBuf, 64u << 20);
    std::wstring lzxNote;
    if (ctl && ctlE->length >= 24 && std::memcmp(ctl + 4, "LZXC", 4) == 0 && rt && rtE->length >= 40 && cont) {
        const uint32_t ver = DetailSys100::RdU32LE(ctl + 8);
        uint64_t resetInterval = DetailSys100::RdU32LE(ctl + 12), windowSize = DetailSys100::RdU32LE(ctl + 16);
        if (ver == 2) {
            resetInterval *= 0x8000;
            windowSize *= 0x8000;
        }
        int windowBits = 0;
        for (int b = 15; b <= 21; ++b) if (windowSize == (uint64_t(1) << b)) windowBits = b;
        const uint32_t numFrames = DetailSys100::RdU32LE(rt + 4), entrySize = DetailSys100::RdU32LE(rt + 8);
        const uint32_t tableOff = DetailSys100::RdU32LE(rt + 12);
        const uint64_t uncompLen = DetailSys100::RdU64LE(rt + 16), frameLen = DetailSys100::RdU64LE(rt + 32);
        std::vector<uint64_t> frameOffsets;
        if (entrySize == 8) {
            for (uint64_t i = 0; i < numFrames && tableOff + (i + 1) * 8 <= rtE->length && frameOffsets.size() < (1u << 20); ++i) {
                frameOffsets.push_back(DetailSys100::RdU64LE(rt + tableOff + static_cast<size_t>(i) * 8));
            }
        }
        if (windowBits && resetInterval && resetInterval % 0x8000 == 0 && frameLen == 0x8000 && uncompLen > 0) {
            const size_t want = static_cast<size_t>(std::min<uint64_t>(uncompLen, 32u << 20));
            ::Explorer2::Lzx::Decoder dec(cont, static_cast<size_t>(contE->length), windowBits,
                                          static_cast<uint32_t>(resetInterval / 0x8000), frameOffsets);
            const bool whole = dec.Decode(want, sec1);
            lzxNote = L"LZX, " + std::to_wstring(windowSize / 1024) + L" KB window, reset every " +
                      std::to_wstring(resetInterval / 0x8000) + L" frames: " + PathUtils::FormatByteSize(contE->length) +
                      L" \u2192 " + PathUtils::FormatByteSize(uncompLen);
            if (!whole) lzxNote += L" (damaged: only the first " + PathUtils::FormatByteSize(sec1.size()) + L" decoded)";
            else if (want < uncompLen) lzxNote += L" (the first " + PathUtils::FormatByteSize(want) + L" decoded)";
        } else {
            lzxNote = L"LZX parameters not understood (window " + std::to_wstring(windowSize) + L" B)";
        }
    }

    // Topics: each HTML page in section 1 with its <title> and the start of its text, in the charset its <meta> names,
    // else UTF-8 when it is valid UTF-8, else the code page of the help file's language
    SpreadsheetSheet topics;
    topics.name = L"CHM_Topics";
    topics.rows.push_back({ L"Path", L"Title", L"Text" });
    if (!sec1.empty()) {
        unsigned ansiCp = 1252;
        wchar_t cpBuf[8] = {};
        if (GetLocaleInfoW(static_cast<LCID>(langId), LOCALE_IDEFAULTANSICODEPAGE, cpBuf, 8) > 0) {
            const unsigned cp = static_cast<unsigned>(wcstoul(cpBuf, nullptr, 10));
            if (cp != 0) ansiCp = cp;
        }
        auto topicText = [&](std::string_view html) -> std::wstring {
            const std::string head = PathUtils::ToLowerAscii(html.substr(0, 4096));
            const size_t cs = head.find("charset=");
            if (cs != std::string::npos) {
                size_t a = cs + 8, b = a;
                while (b < head.size() && head[b] != '"' && head[b] != '\'' && head[b] != ';' && head[b] != '>' && head[b] != ' ' && head[b] != '/') ++b;
                if (b > a) return DecodeCharsetText(html, head.substr(a, b - a));
            }
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, html.data(), static_cast<int>(html.size()), nullptr, 0) > 0) {
                return PathUtils::Utf8ToWide(html);
            }
            const int wn = MultiByteToWideChar(ansiCp, 0, html.data(), static_cast<int>(html.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(std::max(wn, 0)), L'\0');
            if (wn > 0) MultiByteToWideChar(ansiCp, 0, html.data(), static_cast<int>(html.size()), w.data(), wn);
            return w;
        };
        for (const auto& e : entries) {
            if (e.section != 1 || e.length == 0 || e.offset + e.length > sec1.size()) continue;
            const std::wstring lower = PathUtils::ToLower(e.path);
            if (!(lower.size() > 4 && (lower.compare(lower.size() - 4, 4, L".htm") == 0 || lower.compare(lower.size() - 5, 5, L".html") == 0))) continue;
            if (topics.rows.size() > 5000) break;
            const std::string_view html(reinterpret_cast<const char*>(sec1.data() + e.offset), static_cast<size_t>(std::min<uint64_t>(e.length, 2u << 20)));
            const std::wstring w = topicText(html);
            std::wstring title;
            const std::wstring wl = PathUtils::ToLower(w.substr(0, 8192));
            const size_t t0 = wl.find(L"<title");
            if (t0 != std::wstring::npos) {
                const size_t gt = wl.find(L'>', t0);
                const size_t t1 = (gt == std::wstring::npos) ? std::wstring::npos : wl.find(L"</title", gt);
                if (t1 != std::wstring::npos) title = PathUtils::Trim(::Explorer2::XmlTagScan::DecodeEntitiesWide(w.substr(gt + 1, t1 - gt - 1)));
            }
            std::wstring text;
            for (const auto& p : StripHtmlToParagraphs(w, 24)) {
                if (p == title) continue;
                if (!text.empty()) text += L' ';
                text += p;
                if (text.size() >= 400) break;
            }
            if (text.size() > 400) text = text.substr(0, 400) + L"\u2026";
            topics.rows.push_back({ e.path, title, text });
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Compiled HTML Help (.chm)";
    if (topics.rows.size() > 1) {
        FinalizeSheetCols(topics);
        out.sheets.push_back(std::move(topics));
    }

    SpreadsheetSheet hdr;
    hdr.name = L"CHM_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"ITSF / ITSP Version", std::to_wstring(itsfVer) + L" / " + std::to_wstring(itspVer) });
    hdr.rows.push_back({ L"Language LCID", DetailSys100::Hex32(langId) });
    hdr.rows.push_back({ L"Directory Block Size", std::to_wstring(dirBlockSize) + L" B (" + std::to_wstring(numDirBlocks) + L" blocks)" });
    hdr.rows.push_back({ L"Total Directory Entries", std::to_wstring(entries.size()) });
    if (!lzxNote.empty()) hdr.rows.push_back({ L"Compressed Section", lzxNote });
    for (const auto& pv : sec0Previews) {
        hdr.rows.push_back({ L"Embedded String / Topic", pv });
    }
    FinalizeSheetCols(hdr);
    out.sheets.push_back(std::move(hdr));

    SpreadsheetSheet dir;
    dir.name = L"CHM_Directory";
    dir.rows.push_back({ L"Path", L"Section", L"Offset", L"Uncompressed Size" });
    for (const auto& e : entries) {
        dir.rows.push_back({
            e.path,
            (e.section == 0) ? L"0 (Uncompressed)" : (L"1 (LZX #" + std::to_wstring(e.section) + L")"),
            std::to_wstring(e.offset),
            std::to_wstring(e.length)
        });
    }
    FinalizeSheetCols(dir);
    out.sheets.push_back(std::move(dir));
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseChmHelpDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseChmHelp(raw, out); }

} // namespace Formats
