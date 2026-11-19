#pragma once
// PowerPointLegacyPpt.h — Microsoft PowerPoint 97-2003 ([MS-PPT] & [MS-ODRAW]: .ppt, .pps) OLE2 Binary Reader

namespace Formats {
namespace LegacyOfficeDetail {

// ============================================================================
// Microsoft PowerPoint 97–2003 ([MS-PPT] & [MS-ODRAW]: .ppt, .pps)
// ============================================================================
struct PptSlideBucket {
    uint32_t persistRef = 0;
    std::vector<std::wstring> blocks;
    std::set<std::wstring> seenNormalized;

    void AddBlock(std::wstring text) {
        // Normalize line breaks to \n for deduplication across SlideListWithText and Slide shapes
        std::wstring norm;
        norm.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            wchar_t c = text[i];
            if (c == L'\r') {
                if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                norm.push_back(L'\n');
            } else if (c == 0x000B) {
                norm.push_back(L'\n');
            } else if (c >= 0x0020 || c == L'\n' || c == L'\t') {
                norm.push_back(c);
            }
        }
        std::wstring trimmed = TrimWideParagraph(norm);
        if (trimmed.empty()) return;
        if (seenNormalized.insert(trimmed).second) {
            blocks.push_back(std::move(norm));
        }
    }
};

inline std::wstring DecodePptUtf16Atom(const uint8_t* p, size_t byteLen) {
    const size_t nChars = std::min<size_t>(byteLen / 2u, 262144u);
    std::wstring s;
    s.reserve(nChars);
    for (size_t i = 0; i < nChars; ++i) {
        const wchar_t ch = static_cast<wchar_t>(RdU16LE(p + i * 2u));
        if (ch == 0) break;
        s.push_back(ch);
    }
    return s;
}

inline std::wstring DecodePptBytesAtom(const uint8_t* p, size_t byteLen) {
    const size_t nChars = std::min<size_t>(byteLen, 262144u);
    std::wstring s;
    s.reserve(nChars);
    for (size_t i = 0; i < nChars; ++i) {
        if (p[i] == 0) break;
        s.push_back(Cp1252ToWide(p[i]));
    }
    return s;
}

inline void WalkPptShapeRecords(const uint8_t* buf, size_t len, PptSlideBucket& slide, int depth = 0) {
    if (depth > 24 || len < 8) return;
    size_t pos = 0;
    while (pos + 8 <= len) {
        const uint16_t verInst = RdU16LE(buf + pos);
        const uint8_t recVer   = static_cast<uint8_t>(verInst & 0x0Fu);
        const uint16_t recType = RdU16LE(buf + pos + 2);
        const uint32_t recLen  = RdU32LE(buf + pos + 4);
        if (recLen > len - (pos + 8)) break;

        const uint8_t* payload = buf + pos + 8;
        if (recType == 0x0FA0u) { // RT_TextCharsAtom (UTF-16LE)
            slide.AddBlock(DecodePptUtf16Atom(payload, recLen));
        } else if (recType == 0x0FA8u) { // RT_TextBytesAtom (CP1252)
            slide.AddBlock(DecodePptBytesAtom(payload, recLen));
        } else if (recType == 0x0FBAu) { // RT_CString (UTF-16LE)
            slide.AddBlock(DecodePptUtf16Atom(payload, recLen));
        } else if (recVer == 0x0Fu || recType == 0xF00Du) {
            // Recurse into containers AND Escher msofbtClientTextbox (0xF00D, which may use recVer 0x00 or 0x0F)
            WalkPptShapeRecords(payload, recLen, slide, depth + 1);
        }

        pos += 8u + static_cast<size_t>(recLen);
    }
}

inline void ParsePptSlideListWithText(const uint8_t* buf, size_t len, std::vector<PptSlideBucket>& slides) {
    size_t pos = 0;
    while (pos + 8 <= len) {
        const uint16_t verInst = RdU16LE(buf + pos);
        const uint8_t recVer   = static_cast<uint8_t>(verInst & 0x0Fu);
        const uint16_t recType = RdU16LE(buf + pos + 2);
        const uint32_t recLen  = RdU32LE(buf + pos + 4);
        if (recLen > len - (pos + 8)) break;

        const uint8_t* payload = buf + pos + 8;
        if (recType == 0x03F3u && recLen >= 4u) { // RT_SlidePersistAtom starts a new slide entry
            PptSlideBucket sb;
            sb.persistRef = RdU32LE(payload);
            slides.push_back(std::move(sb));
        } else if (recType == 0x0FA0u) { // RT_TextCharsAtom
            if (slides.empty()) slides.emplace_back();
            slides.back().AddBlock(DecodePptUtf16Atom(payload, recLen));
        } else if (recType == 0x0FA8u) { // RT_TextBytesAtom
            if (slides.empty()) slides.emplace_back();
            slides.back().AddBlock(DecodePptBytesAtom(payload, recLen));
        } else if (recType == 0x0FBAu) { // RT_CString
            if (slides.empty()) slides.emplace_back();
            slides.back().AddBlock(DecodePptUtf16Atom(payload, recLen));
        } else if (recVer == 0x0Fu && !slides.empty()) {
            WalkPptShapeRecords(payload, recLen, slides.back(), 1);
        }

        pos += 8u + static_cast<size_t>(recLen);
    }
}

inline void ParsePptDocumentContainer(const uint8_t* buf, size_t len, std::vector<PptSlideBucket>& slides, int depth = 0) {
    if (depth > 8 || len < 8) return;
    size_t pos = 0;
    while (pos + 8 <= len) {
        const uint16_t verInst  = RdU16LE(buf + pos);
        const uint8_t recVer    = static_cast<uint8_t>(verInst & 0x0Fu);
        const uint16_t recInst  = static_cast<uint16_t>(verInst >> 4);
        const uint16_t recType  = RdU16LE(buf + pos + 2);
        const uint32_t recLen   = RdU32LE(buf + pos + 4);
        if (recLen > len - (pos + 8)) break;

        const uint8_t* payload = buf + pos + 8;
        if (recType == 0x0FF0u && recVer == 0x0Fu) {
            // RT_SlideListWithText: recInstance == 0 -> presentation slides (1 = masters, 2 = notes)
            if (recInst == 0) {
                ParsePptSlideListWithText(payload, recLen, slides);
            }
        } else if (recVer == 0x0Fu && recType != 0x040Cu && recType != 0x03F8u) {
            // Recurse into Document sub-containers (skipping MainMaster 0x03F8)
            ParsePptDocumentContainer(payload, recLen, slides, depth + 1);
        }

        pos += 8u + static_cast<size_t>(recLen);
    }
}

inline bool ParseLegacyPowerPointPpt(const Ole2::CompoundFile& cf, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // Reject encrypted PPT via Current User stream magic (0xF3D1C4DF)
    std::vector<uint8_t> curUser;
    if (ReadStreamByName(cf, L"Current User", curUser) && curUser.size() >= 16) {
        const uint32_t magic = RdU32LE(curUser.data() + 12);
        if (magic == 0xF3D1C4DFu) return false;
    }

    std::vector<uint8_t> ppt;
    if (!ReadStreamByName(cf, L"PowerPoint Document", ppt) || ppt.size() < 8) return false;

    std::vector<PptSlideBucket> slidesFromList;
    std::vector<PptSlideBucket> slideContainers;
    bool hasValidPptRoot = false;

    size_t pos = 0;
    while (pos + 8 <= ppt.size()) {
        const uint16_t verInst = RdU16LE(ppt.data() + pos);
        const uint8_t recVer   = static_cast<uint8_t>(verInst & 0x0Fu);
        const uint16_t recType = RdU16LE(ppt.data() + pos + 2);
        const uint32_t recLen  = RdU32LE(ppt.data() + pos + 4);
        if (recLen > ppt.size() - (pos + 8)) break;

        const uint8_t* payload = ppt.data() + pos + 8;
        if (recType == 0x2F14u) { // RT_CryptSession10Container (encrypted)
            return false;
        }
        if (recType == 0x03E8u && recVer == 0x0Fu) { // RT_Document
            hasValidPptRoot = true;
            ParsePptDocumentContainer(payload, recLen, slidesFromList, 0);
        } else if (recType == 0x03EEu && recVer == 0x0Fu) { // RT_Slide
            hasValidPptRoot = true;
            PptSlideBucket sb;
            WalkPptShapeRecords(payload, recLen, sb, 0);
            slideContainers.push_back(std::move(sb));
        }

        pos += 8u + static_cast<size_t>(recLen);
    }

    if (!hasValidPptRoot) return false;

    // Merge shape text from RT_Slide containers into SlideListWithText slides (deduplicating per slide)
    std::vector<PptSlideBucket> finalSlides;
    if (!slidesFromList.empty()) {
        finalSlides = std::move(slidesFromList);
        // Merge matching RT_Slide containers (aligned from latest/corresponding slide containers)
        const size_t offset = (slideContainers.size() > finalSlides.size())
            ? (slideContainers.size() - finalSlides.size())
            : 0;
        for (size_t i = 0; i < finalSlides.size() && (offset + i) < slideContainers.size(); ++i) {
            for (const auto& blk : slideContainers[offset + i].blocks) {
                finalSlides[i].AddBlock(blk);
            }
        }
    } else {
        finalSlides = std::move(slideContainers);
    }

    out.kind = OfficeFileKind::PowerPointPptx;
    out.isSpreadsheet = false;
    out.formatTitle = (extLower == L".pps")
        ? L"Microsoft PowerPoint 97-2003 Slide Show (.pps)"
        : L"Microsoft PowerPoint 97-2003 Presentation (.ppt)";

    for (size_t sIdx = 0; sIdx < finalSlides.size(); ++sIdx) {
        PushOfficeParagraph(out, L"--- Slide " + std::to_wstring(sIdx + 1) + L" ---", 1, false, false);
        bool firstLineOnSlide = true;
        for (const auto& blk : finalSlides[sIdx].blocks) {
            size_t start = 0;
            while (start <= blk.size()) {
                size_t nl = blk.find(L'\n', start);
                std::wstring_view line = (nl == std::wstring::npos)
                    ? std::wstring_view(blk).substr(start)
                    : std::wstring_view(blk).substr(start, nl - start);
                std::wstring cleanLine = TrimWideParagraph(line);
                if (!cleanLine.empty()) {
                    PushOfficeParagraph(out, std::move(cleanLine), firstLineOnSlide ? 2 : 0, !firstLineOnSlide, false);
                    firstLineOnSlide = false;
                }
                if (nl == std::wstring::npos) break;
                start = nl + 1;
            }
        }
    }

    if (out.paragraphs.empty()) {
        PushOfficeParagraph(out, L"(Empty PowerPoint Presentation)", 0, false, false);
    }
    out.valid = true;
    return true;
}

} // namespace LegacyOfficeDetail

inline bool ParsePowerPointLegacyPpt(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(raw.data(), raw.size(), cf)) return false;
    return LegacyOfficeDetail::ParseLegacyPowerPointPpt(cf, extLower, out);
}

} // namespace Formats
