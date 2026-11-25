#pragma once
// AppleXarPkg.h — macOS eXtensible Archive & installer package reader & extractor (.xar, .pkg — "xar!").

#include "../common/ArchiveFormatCommon.h"
#include <cstdlib>

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

inline std::string UnescapeXarXmlText(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) sv.remove_suffix(1);
    std::string out;
    out.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '&') {
            const size_t semi = sv.find(';', i + 1);
            if (semi != std::string_view::npos && semi - i <= 10) {
                const std::string_view ent = sv.substr(i + 1, semi - i - 1);
                if (ent == "amp") { out.push_back('&'); i = semi; continue; }
                if (ent == "lt") { out.push_back('<'); i = semi; continue; }
                if (ent == "gt") { out.push_back('>'); i = semi; continue; }
                if (ent == "quot") { out.push_back('"'); i = semi; continue; }
                if (ent == "apos") { out.push_back('\''); i = semi; continue; }
            }
        }
        out.push_back(sv[i]);
    }
    return out;
}

inline std::string ExtractXarXmlAttr(std::string_view attrText, std::string_view attrName) {
    size_t pos = 0;
    while (pos < attrText.size()) {
        while (pos < attrText.size() && (attrText[pos] == ' ' || attrText[pos] == '\t' || attrText[pos] == '\r' || attrText[pos] == '\n')) ++pos;
        if (pos >= attrText.size()) break;
        const size_t kStart = pos;
        while (pos < attrText.size() && attrText[pos] != '=' && attrText[pos] != ' ' && attrText[pos] != '\t' && attrText[pos] != '/') ++pos;
        const std::string_view key = attrText.substr(kStart, pos - kStart);
        while (pos < attrText.size() && (attrText[pos] == ' ' || attrText[pos] == '\t')) ++pos;
        if (pos < attrText.size() && attrText[pos] == '=') {
            ++pos;
            while (pos < attrText.size() && (attrText[pos] == ' ' || attrText[pos] == '\t')) ++pos;
            if (pos < attrText.size() && (attrText[pos] == '"' || attrText[pos] == '\'')) {
                const char q = attrText[pos++];
                const size_t vStart = pos;
                while (pos < attrText.size() && attrText[pos] != q) ++pos;
                const std::string_view val = attrText.substr(vStart, pos - vStart);
                if (pos < attrText.size()) ++pos;
                if (key == attrName) return UnescapeXarXmlText(val);
            }
        } else {
            ++pos;
        }
    }
    return {};
}

inline bool ParseXarPkg(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 28 || std::memcmp(data, "xar!", 4) != 0) return false;
    uint16_t hdrSize   = RdU16BE(data + 4);
    uint64_t tocCompSz = RdU64BE(data + 8);
    uint64_t tocUncSz  = RdU64BE(data + 16);
    if (hdrSize < 28 || hdrSize > size || tocCompSz > size - hdrSize || tocUncSz > 64ULL * 1024 * 1024) {
        return false;
    }

    outInfo.formatName = (extLower == L".pkg") ? L"macOS Installer Package (.pkg / XAR)" : L"eXtensible Archive (.xar)";

    std::vector<uint8_t> xmlBytes;
    if (tocCompSz == tocUncSz && tocCompSz > 8 && data[hdrSize] == '<') {
        xmlBytes.assign(data + hdrSize, data + hdrSize + static_cast<size_t>(tocCompSz));
    } else if (!ZipDeflate::InflateZlib(data + hdrSize, static_cast<size_t>(tocCompSz), xmlBytes, static_cast<size_t>(tocUncSz) + 64u)) {
        return false;
    }

    const uint64_t heapBase = static_cast<uint64_t>(hdrSize) + tocCompSz;
    const std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());

    struct ParsedXarNode {
        int parentIdx = -1;
        std::string name;
        std::string type;
        std::string encodingStyle;
        uint64_t uncompSize = 0;
        uint64_t compLength = 0;
        uint64_t relOffset  = 0;
        bool hasSize   = false;
        bool hasLength = false;
        bool hasOffset = false;
        bool hasData   = false;
    };

    struct OpenFileFrame {
        int nodeIdx = -1;
        std::vector<std::string> subTags; // strictly relative tags inside this <file> element
    };

    std::vector<ParsedXarNode> nodes;
    std::vector<OpenFileFrame> fileStack;

    size_t pos = 0;
    size_t textStart = 0;
    while (pos < sv.size() && nodes.size() < 30000u) {
        const size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos) break;

        const std::string_view textBefore = sv.substr(textStart, lt - textStart);

        // Skip XML comments <!-- ... -->
        if (lt + 4 <= sv.size() && sv.substr(lt, 4) == "<!--") {
            const size_t cEnd = sv.find("-->", lt + 4);
            if (cEnd == std::string_view::npos) break;
            pos = cEnd + 3;
            textStart = pos;
            continue;
        }
        // Skip CDATA <![CDATA[ ... ]]>
        if (lt + 9 <= sv.size() && sv.substr(lt, 9) == "<![CDATA[") {
            const size_t cEnd = sv.find("]]>", lt + 9);
            if (cEnd == std::string_view::npos) break;
            pos = cEnd + 3;
            textStart = pos;
            continue;
        }
        // Skip processing instructions <? ... ?> and <!DOCTYPE ...>
        if (lt + 2 <= sv.size() && (sv[lt + 1] == '?' || sv[lt + 1] == '!')) {
            const size_t gt = sv.find('>', lt + 2);
            if (gt == std::string_view::npos) break;
            pos = gt + 1;
            textStart = pos;
            continue;
        }

        // Find matching '>' respecting quoted attribute strings
        size_t gt = lt + 1;
        char inQuote = 0;
        while (gt < sv.size()) {
            const char c = sv[gt];
            if (inQuote != 0) {
                if (c == inQuote) inQuote = 0;
            } else if (c == '"' || c == '\'') {
                inQuote = c;
            } else if (c == '>') {
                break;
            }
            ++gt;
        }
        if (gt >= sv.size()) break;

        std::string_view inside = sv.substr(lt + 1, gt - lt - 1);
        while (!inside.empty() && (inside.front() == ' ' || inside.front() == '\t' || inside.front() == '\r' || inside.front() == '\n')) inside.remove_prefix(1);
        while (!inside.empty() && (inside.back() == ' ' || inside.back() == '\t' || inside.back() == '\r' || inside.back() == '\n')) inside.remove_suffix(1);

        pos = gt + 1;
        textStart = pos;
        if (inside.empty()) continue;

        bool isClosing = false;
        bool isSelfClosing = false;
        if (inside.front() == '/') {
            isClosing = true;
            inside.remove_prefix(1);
            while (!inside.empty() && (inside.front() == ' ' || inside.front() == '\t')) inside.remove_prefix(1);
        } else if (inside.back() == '/') {
            isSelfClosing = true;
            inside.remove_suffix(1);
        }

        size_t tnEnd = 0;
        while (tnEnd < inside.size() && inside[tnEnd] != ' ' && inside[tnEnd] != '\t' && inside[tnEnd] != '\r' && inside[tnEnd] != '\n') {
            ++tnEnd;
        }
        const std::string tagName(inside.substr(0, tnEnd));
        const std::string_view attrPart = inside.substr(tnEnd);
        if (tagName.empty()) continue;

        if (!isClosing) {
            if (tagName == "file") {
                ParsedXarNode node;
                node.parentIdx = fileStack.empty() ? -1 : fileStack.back().nodeIdx;
                // Support optional attribute form <file name="..."> if present
                std::string attrName = ExtractXarXmlAttr(attrPart, "name");
                if (!attrName.empty()) node.name = std::move(attrName);
                std::string attrType = ExtractXarXmlAttr(attrPart, "type");
                if (!attrType.empty()) node.type = std::move(attrType);

                const int idx = static_cast<int>(nodes.size());
                nodes.push_back(std::move(node));
                if (!isSelfClosing) {
                    fileStack.push_back(OpenFileFrame{ idx, {} });
                }
            } else if (!fileStack.empty()) {
                auto& frame = fileStack.back();
                auto& cur   = nodes[static_cast<size_t>(frame.nodeIdx)];
                if (frame.subTags.empty() && tagName == "data") {
                    cur.hasData = true;
                } else if (frame.subTags.size() == 1 && frame.subTags[0] == "data" && tagName == "encoding") {
                    std::string st = ExtractXarXmlAttr(attrPart, "style");
                    if (!st.empty()) cur.encodingStyle = std::move(st);
                }
                if (!isSelfClosing) {
                    frame.subTags.push_back(tagName);
                }
            }
        } else {
            if (tagName == "file") {
                if (!fileStack.empty()) {
                    fileStack.pop_back();
                }
            } else if (!fileStack.empty()) {
                auto& frame = fileStack.back();
                auto& cur   = nodes[static_cast<size_t>(frame.nodeIdx)];
                if (frame.subTags.size() == 1 && frame.subTags[0] == tagName) {
                    if (tagName == "name") {
                        cur.name = UnescapeXarXmlText(textBefore);
                    } else if (tagName == "type") {
                        cur.type = UnescapeXarXmlText(textBefore);
                    }
                } else if (frame.subTags.size() == 2 && frame.subTags[0] == "data" && frame.subTags[1] == tagName) {
                    const std::string valStr = UnescapeXarXmlText(textBefore);
                    if (!valStr.empty()) {
                        char* endPtr = nullptr;
                        const unsigned long long v = std::strtoull(valStr.c_str(), &endPtr, 10);
                        if (endPtr != valStr.c_str()) {
                            if (tagName == "size") { cur.uncompSize = v; cur.hasSize = true; }
                            else if (tagName == "length") { cur.compLength = v; cur.hasLength = true; }
                            else if (tagName == "offset") { cur.relOffset = v; cur.hasOffset = true; }
                        }
                    }
                }

                // Pop matching subTag (resilient to minor malformed inner tags)
                for (size_t k = frame.subTags.size(); k > 0; --k) {
                    if (frame.subTags[k - 1] == tagName) {
                        frame.subTags.resize(k - 1);
                        break;
                    }
                }
            }
        }
    }

    std::vector<std::wstring> resolvedPaths(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        const std::wstring wName = PathUtils::Utf8ToWide(n.name);
        if (wName.empty()) continue;
        std::wstring fullRel = wName;
        if (n.parentIdx >= 0 && static_cast<size_t>(n.parentIdx) < i && !resolvedPaths[static_cast<size_t>(n.parentIdx)].empty()) {
            fullRel = resolvedPaths[static_cast<size_t>(n.parentIdx)] + L"/" + wName;
        }
        resolvedPaths[i] = ArchivePath::Normalize(fullRel);
        if (resolvedPaths[i].empty()) continue;

        const bool isDir = (n.type == "directory");
        ArchiveItemEntry item;
        item.pathInArchive = resolvedPaths[i];
        item.name = ArchivePath::BaseName(item.pathInArchive);
        item.isDirectory = isDir;
        if (!isDir && n.hasData) {
            item.uncompressedSize = n.hasSize ? n.uncompSize : 0ULL;
            item.compressedSize   = n.hasLength ? n.compLength : item.uncompressedSize;
            if (n.relOffset > size - heapBase || item.compressedSize > size - heapBase - n.relOffset) {
                return false;
            }
            item.rawDataOffset = heapBase + n.relOffset;

            const bool isOctet = (n.encodingStyle.find("octet-stream") != std::string::npos);
            const bool isBzip2 = (n.encodingStyle.find("bzip2") != std::string::npos);
            const bool isXz    = (n.encodingStyle.find("xz") != std::string::npos || n.encodingStyle.find("lzma") != std::string::npos);
            if (isOctet || (n.encodingStyle.empty() && item.compressedSize == item.uncompressedSize)) {
                item.compressionMethod = 0;
            } else if (isBzip2) {
                item.compressionMethod = 12;
            } else if (isXz) {
                item.compressionMethod = 95;
            } else {
                item.compressionMethod = 8;
            }
        } else {
            item.uncompressedSize = 0;
            item.compressedSize   = 0;
            item.rawDataOffset    = 0;
            item.compressionMethod = 0;
        }
        outInfo.entries.push_back(std::move(item));
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectAppleXarPkg(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailUnixAndLegacyArc::ParseXarPkg(data, size, extLower, outInfo);
}

inline bool ExtractAppleXarPkgEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (data && size >= 28 && std::memcmp(data, "xar!", 4) == 0) {
        ArchiveSummaryInfo info;
        if (DetailUnixAndLegacyArc::ParseXarPkg(data, size, extLower, info)) {
            const std::wstring wantNorm = ArchivePath::Normalize(want);
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), wantNorm.c_str()) == 0) {
                    if (e.uncompressedSize == 0 && e.compressedSize == 0) {
                        outBytes.clear();
                        return true;
                    }
                    if (e.rawDataOffset > size || e.compressedSize > size - e.rawDataOffset ||
                        e.uncompressedSize > 256ULL * 1024 * 1024) {
                        return false;
                    }
                    const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                    const size_t cLen = static_cast<size_t>(e.compressedSize);
                    const size_t uLen = static_cast<size_t>(e.uncompressedSize);
                    if (e.compressionMethod == 0) {
                        if (uLen > size - static_cast<size_t>(e.rawDataOffset)) return false;
                        outBytes.assign(p, p + uLen);
                        return true;
                    }
                    if (e.compressionMethod == 12 || (cLen >= 3 && std::memcmp(p, "BZh", 3) == 0)) {
                        return StreamCodecs::DecompressBzip2Stream(p, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen;
                    }
                    if (e.compressionMethod == 95 || (cLen >= 6 && std::memcmp(p, "\xFD" "7zXZ\0", 6) == 0)) {
                        return StreamCodecs::DecompressXzStream(p, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen;
                    }
                    if ((ZipDeflate::InflateZlib(p, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen) ||
                        (ZipDeflate::InflateRaw(p, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen))
                    {
                        return true;
                    }
                    return false;
                }
            }
        }
        return false;
    }
    return false;
}

} // namespace Explorer2::Formats
