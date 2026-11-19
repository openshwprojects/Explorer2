#pragma once
// WebArchiveWarc.h — Zero-dependency C++20 ISO 28500 Web ARChive (.warc, uncompressed & multi-member GZIP) reader.

#include "../../ByteIO.h"
#include "../../ArchiveTypes.h"
#include "../../ZipDeflate.h"
#include "../../Crc32.h"
#include "../../StreamCodecs.h"
#include "../../PathUtils.h"
#include "../common/ArchiveFormatCommon.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace Explorer2::Formats {

namespace DetailWarc {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline bool EndsWithCI(const std::wstring& s, const wchar_t* suffix) {
    const size_t sl = wcslen(suffix);
    if (s.size() < sl) return false;
    return _wcsicmp(s.c_str() + (s.size() - sl), suffix) == 0;
}

inline std::string AsciiLower(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

inline std::string_view TrimAsciiSV(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) sv.remove_suffix(1);
    return sv;
}

inline void CalendarToDosDateTime(
    int year, int month, int day,
    int hour, int minute, int second,
    uint16_t& outDate, uint16_t& outTime)
{
    outDate = 0;
    outTime = 0;
    if (year < 1980 || year > 2107 || month < 1 || month > 12 || day < 1 || day > 31) return;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return;
    if (second == 60) second = 59;
    outDate = static_cast<uint16_t>(((year - 1980) << 9) | (month << 5) | day);
    outTime = static_cast<uint16_t>((hour << 11) | (minute << 5) | (second / 2));
}

inline bool SkipGzipHeader(const uint8_t* src, size_t len, size_t& outDeflateStart) {
    if (!src || len < 18u || src[0] != 0x1Fu || src[1] != 0x8Bu || src[2] != 0x08u) return false;
    uint8_t flg = src[3];
    if ((flg & 0xE0u) != 0u) return false;
    size_t p = 10u;
    if ((flg & 0x04u) != 0u) { // FEXTRA
        if (p + 2u > len) return false;
        uint16_t xlen = RdU16LE(src + p);
        p += 2u + xlen;
    }
    if ((flg & 0x08u) != 0u) { // FNAME
        while (p < len && src[p] != 0u) ++p;
        ++p;
    }
    if ((flg & 0x10u) != 0u) { // FCOMMENT
        while (p < len && src[p] != 0u) ++p;
        ++p;
    }
    if ((flg & 0x02u) != 0u) { // FHCRC
        p += 2u;
    }
    if (p + 8u > len) return false;
    outDeflateStart = p;
    return true;
}

inline bool InflateConcatenatedGzipWarc(const uint8_t* data, size_t size, std::vector<uint8_t>& outAll) {
    outAll.clear();
    constexpr size_t kMaxWarcBytes = 128u * 1024u * 1024u;
    size_t mPos = 0;
    size_t members = 0;

    while (mPos < size && members < 65536u) {
        size_t defStart = 0;
        if (!SkipGzipHeader(data + mPos, size - mPos, defStart)) {
            return false;
        }
        std::vector<uint8_t> memberOut;
        if (!ZipDeflate::InflateGzip(data + mPos, size - mPos, memberOut, kMaxWarcBytes - outAll.size())) {
            return false;
        }

        // Check if InflateGzip stopped after one member or decoded all remaining members
        const uint32_t crc   = ::Explorer2::Checksum::Crc32(memberOut.data(), memberOut.size());
        const uint32_t isize = static_cast<uint32_t>(memberOut.size() & 0xFFFFFFFFu);

        size_t nextMemberPos = size;
        for (size_t t = mPos + defStart; t + 8u <= size; ++t) {
            if (RdU32LE(data + t) == crc && RdU32LE(data + t + 4u) == isize) {
                size_t candNext = t + 8u;
                if (candNext == size) {
                    nextMemberPos = size;
                    break;
                }
                size_t nextDef = 0;
                if (SkipGzipHeader(data + candNext, size - candNext, nextDef)) {
                    nextMemberPos = candNext;
                    break;
                }
            }
        }

        if (outAll.size() + memberOut.size() > kMaxWarcBytes) return false;
        outAll.insert(outAll.end(), memberOut.begin(), memberOut.end());
        mPos = nextMemberPos;
        ++members;
    }
    return !outAll.empty();
}

inline std::wstring SanitizeWarcSegment(std::string_view seg) {
    std::string clean;
    clean.reserve(seg.size());
    for (size_t i = 0; i < seg.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(seg[i]);
        if (c == '%' && i + 2u < seg.size() &&
            std::isxdigit(static_cast<unsigned char>(seg[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(seg[i + 2])))
        {
            char hex[3] = { seg[i + 1], seg[i + 2], 0 };
            unsigned long v = std::strtoul(hex, nullptr, 16);
            if (v >= 0x20u && v != '/' && v != '\\' && v != ':') {
                c = static_cast<unsigned char>(v);
                i += 2u;
            }
        }
        if (c < 0x20u || std::strchr("<>:\"/\\|?*", static_cast<char>(c)) != nullptr) {
            clean.push_back('_');
        } else {
            clean.push_back(static_cast<char>(c));
        }
    }
    while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) clean.pop_back();
    if (clean == "." || clean == "..") clean = "_";
    return PathUtils::Utf8ToWide(clean);
}

inline std::wstring MapWarcUriToArchivePath(std::string_view rawUri) {
    std::string_view u = TrimAsciiSV(rawUri);
    if (u.size() >= 2u && u.front() == '<' && u.back() == '>') {
        u.remove_prefix(1);
        u.remove_suffix(1);
        u = TrimAsciiSV(u);
    }
    size_t schemePos = u.find("://");
    if (schemePos != std::string_view::npos) {
        u.remove_prefix(schemePos + 3u);
    } else if (u.rfind("dns:", 0) == 0) {
        u.remove_prefix(4u);
    } else if (u.rfind("urn:", 0) == 0) {
        u.remove_prefix(4u);
    }

    size_t fragPos = u.find('#');
    if (fragPos != std::string_view::npos) u = u.substr(0, fragPos);

    std::string_view queryPart;
    size_t qPos = u.find('?');
    if (qPos != std::string_view::npos) {
        queryPart = u.substr(qPos + 1u);
        u = u.substr(0, qPos);
    }

    const bool trailingSlash = u.empty() || u.back() == '/' || u.find('/') == std::string_view::npos;
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start < u.size()) {
        size_t sl = u.find('/', start);
        std::string_view tok = (sl == std::string_view::npos) ? u.substr(start) : u.substr(start, sl - start);
        if (!tok.empty() && tok != "." && tok != "..") {
            std::wstring wseg = SanitizeWarcSegment(tok);
            if (!wseg.empty()) parts.push_back(std::move(wseg));
        }
        if (sl == std::string_view::npos) break;
        start = sl + 1u;
    }

    if (parts.empty()) {
        parts.push_back(L"unknown_host");
    }
    if (trailingSlash) {
        parts.push_back(L"index.html");
    }
    if (!queryPart.empty()) {
        std::wstring qSan = SanitizeWarcSegment(queryPart.substr(0, std::min<size_t>(queryPart.size(), 48u)));
        if (!qSan.empty()) {
            std::wstring& leaf = parts.back();
            size_t dot = leaf.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0u) {
                leaf = leaf.substr(0, dot) + L"_" + qSan + leaf.substr(dot);
            } else {
                leaf += L"_" + qSan;
            }
        }
    }

    std::wstring joined;
    for (const auto& p : parts) {
        if (!joined.empty()) joined.push_back(L'/');
        joined += p;
    }
    return ArchivePath::Normalize(std::move(joined));
}

inline void ParseWarcIsoDateToDos(std::string_view iso, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    iso = TrimAsciiSV(iso);
    if (iso.size() < 19u || iso[4] != '-' || iso[7] != '-' || (iso[10] != 'T' && iso[10] != ' ')) return;
    auto dec = [&](size_t off, size_t n) -> int {
        int v = 0;
        for (size_t i = 0; i < n; ++i) {
            char c = iso[off + i];
            if (c < '0' || c > '9') return -1;
            v = v * 10 + (c - '0');
        }
        return v;
    };
    CalendarToDosDateTime(dec(0, 4), dec(5, 2), dec(8, 2), dec(11, 2), dec(14, 2), dec(17, 2), outDate, outTime);
}

inline bool DecodeHttpChunkedBody(const uint8_t* src, size_t len, std::vector<uint8_t>& outBody) {
    outBody.clear();
    size_t pos = 0;
    while (pos < len) {
        while (pos < len && (src[pos] == '\r' || src[pos] == '\n' || src[pos] == ' ' || src[pos] == '\t')) ++pos;
        if (pos >= len) break;

        uint64_t chunkSz = 0;
        bool anyHex = false;
        while (pos < len) {
            unsigned char c = src[pos];
            uint32_t d = 0;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = 10u + (c - 'a');
            else if (c >= 'A' && c <= 'F') d = 10u + (c - 'A');
            else break;
            anyHex = true;
            chunkSz = (chunkSz << 4u) | d;
            if (chunkSz > 128ULL * 1024 * 1024) return false;
            ++pos;
        }
        if (!anyHex) return false;

        // Skip optional chunk extensions up to LF
        while (pos < len && src[pos] != '\n') ++pos;
        if (pos >= len) return false;
        ++pos; // skip '\n'

        if (chunkSz == 0u) return true;
        if (chunkSz > len - pos) return false;
        outBody.insert(outBody.end(), src + pos, src + pos + static_cast<size_t>(chunkSz));
        pos += static_cast<size_t>(chunkSz);
        if (pos < len && src[pos] == '\r') ++pos;
        if (pos < len && src[pos] == '\n') ++pos;
    }
    return true;
}

} // namespace DetailWarc

inline bool ParseOrExtractWarc(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!data || size < 16u) return false;

    const std::wstring fnLower = PathUtils::ToLower(PathUtils::GetFileName(archivePath));
    const bool hasWarcMagic = (size >= 10u &&
                               (std::memcmp(data, "WARC/1.0\r\n", 10) == 0 ||
                                std::memcmp(data, "WARC/1.1\r\n", 10) == 0 ||
                                std::memcmp(data, "WARC/1.0\n", 9) == 0 ||
                                std::memcmp(data, "WARC/1.1\n", 9) == 0));
    const bool isWarcExt = (extLower == L".warc") || DetailWarc::EndsWithCI(fnLower, L".warc.gz");
    if (!hasWarcMagic && !isWarcExt) return false;

    std::vector<uint8_t> gzDecoded;
    const uint8_t* buf = data;
    size_t len = size;
    bool isGzipped = false;

    if (!hasWarcMagic && size >= 18u && data[0] == 0x1Fu && data[1] == 0x8Bu && data[2] == 0x08u) {
        if (!DetailWarc::InflateConcatenatedGzipWarc(data, size, gzDecoded)) return false;
        buf = gzDecoded.data();
        len = gzDecoded.size();
        isGzipped = true;
    }

    if (len < 16u || std::memcmp(buf, "WARC/1.", 7) != 0) return false;

    std::wstring wantNorm = extractTargetNorm ? ArchivePath::Normalize(*extractTargetNorm) : std::wstring{};
    std::unordered_set<std::wstring> usedPathsLower;
    auto dedupPath = [&](std::wstring base) -> std::wstring {
        if (base.empty()) base = L"record.bin";
        std::wstring low = PathUtils::ToLower(base);
        if (usedPathsLower.insert(low).second) return base;
        size_t slash = base.find_last_of(L'/');
        size_t dot   = base.find_last_of(L'.');
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot <= slash + 1u)) {
            dot = base.size();
        }
        for (size_t idx = 2; idx < 100000u; ++idx) {
            std::wstring cand = base.substr(0, dot) + L"_" + std::to_wstring(idx) + base.substr(dot);
            if (usedPathsLower.insert(PathUtils::ToLower(cand)).second) return cand;
        }
        return base;
    };

    size_t pos = 0;
    size_t recordCount = 0;
    std::string warcVersion = "1.0";

    while (pos < len && recordCount < 50000u) {
        while (pos < len && (buf[pos] == '\r' || buf[pos] == '\n')) ++pos;
        if (pos >= len) break;

        if (len - pos < 9u || std::memcmp(buf + pos, "WARC/", 5) != 0) {
            return false;
        }

        // Version line
        size_t vEnd = pos;
        while (vEnd < len && buf[vEnd] != '\n') ++vEnd;
        if (vEnd >= len) return false;
        std::string_view verLine = DetailWarc::TrimAsciiSV(std::string_view(reinterpret_cast<const char*>(buf + pos), vEnd - pos));
        if (verLine != "WARC/1.0" && verLine != "WARC/1.1" && verLine != "WARC/0.18") return false;
        if (recordCount == 0u) warcVersion.assign(verLine.substr(5));
        pos = vEnd + 1u;

        // Header lines
        std::string warcType, targetUri, warcDate, warcRecordId, warcFilename, contentType;
        uint64_t contentLen = 0;
        bool hasContentLen = false;
        bool sawBlankLine = false;

        while (pos < len) {
            size_t lEnd = pos;
            while (lEnd < len && buf[lEnd] != '\n') ++lEnd;
            if (lEnd >= len) return false;
            std::string_view line = DetailWarc::TrimAsciiSV(std::string_view(reinterpret_cast<const char*>(buf + pos), lEnd - pos));
            pos = lEnd + 1u;
            if (line.empty()) {
                sawBlankLine = true;
                break;
            }
            size_t colon = line.find(':');
            if (colon == std::string_view::npos) continue;
            std::string key = DetailWarc::AsciiLower(DetailWarc::TrimAsciiSV(line.substr(0, colon)));
            std::string_view val = DetailWarc::TrimAsciiSV(line.substr(colon + 1u));
            if (key == "warc-type") warcType = DetailWarc::AsciiLower(val);
            else if (key == "warc-target-uri") targetUri.assign(val);
            else if (key == "warc-date") warcDate.assign(val);
            else if (key == "warc-record-id") warcRecordId.assign(val);
            else if (key == "warc-filename") warcFilename.assign(val);
            else if (key == "content-type") contentType = DetailWarc::AsciiLower(val);
            else if (key == "content-length") {
                if (val.empty()) return false;
                uint64_t parsed = 0;
                for (char c : val) {
                    if (c < '0' || c > '9') return false;
                    parsed = parsed * 10ULL + static_cast<uint64_t>(c - '0');
                    if (parsed > 256ULL * 1024 * 1024) return false;
                }
                contentLen = parsed;
                hasContentLen = true;
            }
        }

        if (!sawBlankLine || !hasContentLen || contentLen > len - pos) {
            return false;
        }

        const size_t blockOff = pos;
        const size_t blockLen = static_cast<size_t>(contentLen);
        pos += blockLen;

        // Require record separator (\r\n\r\n or \n\n or EOF)
        if (pos < len) {
            if (buf[pos] != '\r' && buf[pos] != '\n') return false;
            while (pos < len && (buf[pos] == '\r' || buf[pos] == '\n')) ++pos;
        }

        // Map record to clean archive path
        std::wstring entryPath;
        if (warcType == "warcinfo") {
            entryPath = L"warcinfo.txt";
        } else if (warcType == "response" || warcType == "resource" || warcType == "conversion") {
            entryPath = targetUri.empty()
                ? (L"records/" + PathUtils::Utf8ToWide(warcType.empty() ? "record" : warcType) + L".bin")
                : DetailWarc::MapWarcUriToArchivePath(targetUri);
        } else if (warcType == "request") {
            entryPath = L"_requests/" + (targetUri.empty() ? L"request" : DetailWarc::MapWarcUriToArchivePath(targetUri)) + L".http";
        } else if (warcType == "metadata") {
            entryPath = L"_metadata/" + (targetUri.empty() ? L"metadata.txt" : (DetailWarc::MapWarcUriToArchivePath(targetUri) + L".meta.txt"));
        } else if (warcType == "revisit") {
            entryPath = L"_revisits/" + (targetUri.empty() ? L"revisit.txt" : (DetailWarc::MapWarcUriToArchivePath(targetUri) + L".revisit.txt"));
        } else {
            entryPath = targetUri.empty() ? L"records/record.bin" : DetailWarc::MapWarcUriToArchivePath(targetUri);
        }
        entryPath = dedupPath(ArchivePath::Normalize(std::move(entryPath)));

        // Slice payload (extract clean HTTP response body for WARC response records)
        size_t payloadOff = blockOff;
        size_t payloadLen = blockLen;
        bool isHttpChunked = false;
        std::vector<uint8_t> chunkedDecoded;

        const bool isHttpResp = (warcType == "response") &&
                                (contentType.find("application/http") != std::string::npos ||
                                 (blockLen >= 7u && std::memcmp(buf + blockOff, "HTTP/1.", 7) == 0));
        if (isHttpResp && blockLen >= 4u) {
            std::string_view blkSV(reinterpret_cast<const char*>(buf + blockOff), blockLen);
            size_t hdrSep = blkSV.find("\r\n\r\n");
            size_t sepBytes = 4u;
            if (hdrSep == std::string_view::npos) {
                hdrSep = blkSV.find("\n\n");
                sepBytes = 2u;
            }
            if (hdrSep != std::string_view::npos) {
                std::string httpHdrLower = DetailWarc::AsciiLower(blkSV.substr(0, hdrSep));
                payloadOff = blockOff + hdrSep + sepBytes;
                payloadLen = blockLen - (hdrSep + sepBytes);

                // Detect Transfer-Encoding: chunked
                size_t tePos = httpHdrLower.find("transfer-encoding:");
                if (tePos != std::string::npos) {
                    size_t teEnd = httpHdrLower.find('\n', tePos);
                    std::string_view teVal = std::string_view(httpHdrLower).substr(
                        tePos + 18u, (teEnd == std::string::npos ? httpHdrLower.size() : teEnd) - (tePos + 18u));
                    if (teVal.find("chunked") != std::string_view::npos) {
                        if (DetailWarc::DecodeHttpChunkedBody(buf + payloadOff, payloadLen, chunkedDecoded)) {
                            isHttpChunked = true;
                        }
                    }
                }
            }
        }

        if (extractTargetNorm && outExtracted) {
            if (_wcsicmp(entryPath.c_str(), wantNorm.c_str()) == 0) {
                if (isHttpChunked) {
                    *outExtracted = std::move(chunkedDecoded);
                } else {
                    outExtracted->assign(buf + payloadOff, buf + payloadOff + payloadLen);
                }
                return true;
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive = entryPath;
            item.name = ArchivePath::BaseName(entryPath);
            item.isDirectory = false;
            item.uncompressedSize = isHttpChunked ? chunkedDecoded.size() : payloadLen;
            item.compressedSize   = isGzipped ? ((len > 0) ? ((static_cast<uint64_t>(size) * blockLen) / len) : blockLen) : payloadLen;
            DetailWarc::ParseWarcIsoDateToDos(warcDate, item.dosDate, item.dosTime);
            if (!isGzipped && !isHttpChunked) {
                item.rawDataOffset = payloadOff;
                item.compressionMethod = 0;
            } else {
                item.compressionMethod = 8;
            }
            outInfo->entries.push_back(std::move(item));
        }
        ++recordCount;
        (void)warcRecordId;
        (void)warcFilename;
    }

    if (recordCount == 0u) return false;
    if (extractTargetNorm) return false;
    if (!outInfo) return false;

    outInfo->formatName = L"ISO 28500 WARC " + PathUtils::Utf8ToWide(warcVersion) + (isGzipped ? L" (GZIP)" : L"");
    outInfo->valid = !outInfo->entries.empty();
    return outInfo->valid;
}

} // namespace Explorer2::Formats
