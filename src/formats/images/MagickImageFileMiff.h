#pragma once
// MagickImageFileMiff.h — ImageMagick Machine Independent File Format (.miff) Decoder
// Supports DirectClass & PseudoClass, depth=8|16, matte/alpha-trait, Gray/RGB/CMYK, and None|RLE|Zip|BZip compression.

namespace Formats {

namespace DetailMiff {

inline std::string ToLowerMiff(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

inline uint8_t ReadMiffSample8(const uint8_t* p, uint32_t depthBytes) noexcept {
    if (depthBytes == 1u) return p[0];
    if (depthBytes == 2u) return static_cast<uint8_t>((static_cast<uint32_t>(ByteIO::RdU16BE(p)) * 255u + 32767u) / 65535u);
    return static_cast<uint8_t>(ByteIO::RdU32BE(p) >> 24u);
}

inline bool DecompressMiffBlockStream(
    const uint8_t* src,
    size_t srcLen,
    uint32_t rows,
    size_t rowBytes,
    bool isZip,
    std::vector<uint8_t>& outRaw)
{
    const size_t totalBytes = static_cast<size_t>(rows) * rowBytes;
    outRaw.clear();

    // 1. Standard ImageMagick MIFF per-scanline framing: [uint32_t chunkLen BE][compressed scanline]...
    if (srcLen >= 8u) {
        const uint32_t firstLen = ByteIO::RdU32BE(src);
        if (firstLen >= 2u && firstLen <= srcLen - 4u) {
            outRaw.reserve(totalBytes);
            size_t pos = 0;
            bool allRowsOk = true;
            std::vector<uint8_t> rowDec;
            for (uint32_t y = 0; y < rows; ++y) {
                if (pos + 4u > srcLen) { allRowsOk = false; break; }
                const uint32_t cLen = ByteIO::RdU32BE(src + pos);
                pos += 4u;
                if (cLen == 0 || cLen > srcLen - pos) { allRowsOk = false; break; }
                const bool ok = isZip
                    ? ZipDeflate::InflateZlib(src + pos, cLen, rowDec, rowBytes + 256u)
                    : ::Explorer2::StreamCodecs::DecompressBzip2Stream(src + pos, cLen, rowDec, rowBytes + 256u);
                if (!ok || rowDec.size() < rowBytes) { allRowsOk = false; break; }
                outRaw.insert(outRaw.end(), rowDec.begin(), rowDec.begin() + static_cast<ptrdiff_t>(rowBytes));
                pos += cLen;
            }
            if (allRowsOk && outRaw.size() == totalBytes) return true;
        }
    }

    // 2. Single contiguous Zlib / BZip2 stream covering the entire image
    outRaw.clear();
    const bool okWhole = isZip
        ? ZipDeflate::InflateZlib(src, srcLen, outRaw, totalBytes + 4096u)
        : ::Explorer2::StreamCodecs::DecompressBzip2Stream(src, srcLen, outRaw, totalBytes + 4096u);
    return okWhole && outRaw.size() >= totalBytes;
}

} // namespace DetailMiff

inline bool DecodeMagickImageFileMiff(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace DetailMiff;
    if (!data || size < 24u) return false;

    const size_t maxHdrScan = std::min<size_t>(size, 65536u);
    std::string_view headView(reinterpret_cast<const char*>(data), maxHdrScan);

    size_t p = 0;
    while (p < headView.size() && static_cast<unsigned char>(headView[p]) <= 0x20u) ++p;
    if (p + 14u > headView.size() || ToLowerMiff(headView.substr(p, 14u)) != "id=imagemagick") {
        return false;
    }

    // Locate MIFF header terminator: ":\x1A" (standard) or "{\f}" / "\f\n:"
    size_t hdrEnd = std::string_view::npos;
    size_t payloadOff = 0;
    for (size_t i = p; i < maxHdrScan; ++i) {
        if (data[i] == ':' && i + 1u < size && data[i + 1u] == 0x1Au) {
            hdrEnd = i;
            payloadOff = i + 2u;
            break;
        }
        if (data[i] == 0x1Au && i > p && (data[i - 1u] == '\n' || data[i - 1u] == '\r')) {
            hdrEnd = i;
            payloadOff = i + 1u;
            break;
        }
        if (data[i] == '\f' && i + 1u < size && (data[i + 1u] == '\n' || data[i + 1u] == ':')) {
            size_t t = i + 1u;
            while (t < size && (data[t] == '\n' || data[t] == '\r' || data[t] == ':' || data[t] == 0x1Au)) ++t;
            hdrEnd = i;
            payloadOff = t;
            break;
        }
    }
    if (hdrEnd == std::string_view::npos || payloadOff >= size) return false;

    // Parse key=value pairs in [p .. hdrEnd)
    uint32_t cols = 0, rows = 0, depth = 8u, colors = 0;
    std::string imgClass = "directclass";
    std::string colorspace = "srgb";
    std::string compression = "none";
    bool hasAlpha = false;

    std::string_view hv(reinterpret_cast<const char*>(data + p), hdrEnd - p);
    size_t cur = 0;
    while (cur < hv.size()) {
        while (cur < hv.size() && static_cast<unsigned char>(hv[cur]) <= 0x20u) ++cur;
        if (cur >= hv.size()) break;
        if (hv[cur] == '{') { // { comment block }
            size_t closeB = hv.find('}', cur + 1u);
            if (closeB == std::string_view::npos) break;
            cur = closeB + 1u;
            continue;
        }
        if (hv[cur] == '#') {
            size_t eol = hv.find('\n', cur + 1u);
            if (eol == std::string_view::npos) break;
            cur = eol + 1u;
            continue;
        }
        size_t eq = hv.find('=', cur);
        if (eq == std::string_view::npos) break;
        std::string key = ToLowerMiff(hv.substr(cur, eq - cur));
        while (!key.empty() && static_cast<unsigned char>(key.back()) <= 0x20u) key.pop_back();
        cur = eq + 1u;
        while (cur < hv.size() && (hv[cur] == ' ' || hv[cur] == '\t')) ++cur;

        std::string val;
        if (cur < hv.size() && (hv[cur] == '{' || hv[cur] == '"' || hv[cur] == '\'')) {
            const char closeCh = (hv[cur] == '{') ? '}' : hv[cur];
            ++cur;
            size_t endV = hv.find(closeCh, cur);
            if (endV == std::string_view::npos) endV = hv.size();
            val.assign(hv.substr(cur, endV - cur));
            cur = (endV < hv.size()) ? (endV + 1u) : hv.size();
        } else {
            size_t endV = cur;
            while (endV < hv.size() && static_cast<unsigned char>(hv[endV]) > 0x20u) ++endV;
            val.assign(hv.substr(cur, endV - cur));
            cur = endV;
        }

        const std::string valLow = ToLowerMiff(val);
        if (key == "columns" || key == "width") cols = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "rows" || key == "height") rows = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "depth") depth = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "colors") colors = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "class") imgClass = valLow;
        else if (key == "colorspace") colorspace = valLow;
        else if (key == "compression") compression = valLow;
        else if (key == "matte" || key == "alpha") {
            if (valLow == "true" || valLow == "1" || valLow == "yes" || valLow == "on") hasAlpha = true;
        } else if (key == "alpha-trait") {
            if (valLow != "undefined" && valLow != "none" && valLow != "false") hasAlpha = true;
        }
    }

    if (cols == 0 || rows == 0 || cols > 8192u || rows > 8192u) return false;
    if (depth != 8u && depth != 16u && depth != 32u) return false;
    const uint32_t depthBytes = depth / 8u;
    const bool isPseudo = (imgClass == "pseudoclass");
    const bool isGray   = (colorspace == "gray" || colorspace == "grey" || colorspace == "lineargray");
    const bool isCmyk   = (colorspace == "cmyk");

    // Read uncompressed colormap when class=PseudoClass
    std::vector<std::array<uint8_t, 3>> palette;
    uint32_t idxBytes = 1u;
    if (isPseudo) {
        if (colors == 0 || colors > 65536u) return false;
        idxBytes = (colors > 256u) ? 2u : 1u;
        const size_t cmapBytes = static_cast<size_t>(colors) * 3u * depthBytes;
        if (cmapBytes > size - payloadOff) return false;
        palette.resize(colors);
        const uint8_t* cp = data + payloadOff;
        for (uint32_t i = 0; i < colors; ++i) {
            palette[i][0] = ReadMiffSample8(cp + (static_cast<size_t>(i) * 3u + 0u) * depthBytes, depthBytes);
            palette[i][1] = ReadMiffSample8(cp + (static_cast<size_t>(i) * 3u + 1u) * depthBytes, depthBytes);
            palette[i][2] = ReadMiffSample8(cp + (static_cast<size_t>(i) * 3u + 2u) * depthBytes, depthBytes);
        }
        payloadOff += cmapBytes;
    }

    const uint32_t colorSamplesPerPx = isPseudo ? 0u : (isCmyk ? 4u : (isGray ? 1u : 3u));
    const size_t packetBytes = isPseudo
        ? (idxBytes + (hasAlpha ? depthBytes : 0u))
        : (static_cast<size_t>(colorSamplesPerPx + (hasAlpha ? 1u : 0u)) * depthBytes);
    const size_t totalPixels = static_cast<size_t>(cols) * rows;
    if (packetBytes == 0 || totalPixels > 64ull * 1024ull * 1024ull) return false;

    const size_t rowRawBytes = static_cast<size_t>(cols) * packetBytes;
    const size_t expectedRawBytes = totalPixels * packetBytes;

    std::vector<uint8_t> unpacked;
    const uint8_t* rawPixels = nullptr;

    if (compression == "none" || compression.empty()) {
        if (expectedRawBytes > size - payloadOff) return false;
        rawPixels = data + payloadOff;
    } else if (compression == "zip" || compression == "zlib") {
        if (!DecompressMiffBlockStream(data + payloadOff, size - payloadOff, rows, rowRawBytes, true, unpacked)) {
            return false;
        }
        rawPixels = unpacked.data();
    } else if (compression == "bzip" || compression == "bzip2") {
        if (!DecompressMiffBlockStream(data + payloadOff, size - payloadOff, rows, rowRawBytes, false, unpacked)) {
            return false;
        }
        rawPixels = unpacked.data();
    } else if (compression == "runlengthencoded" || compression == "rle") {
        // ImageMagick MIFF RLE packet: [pixelPacket (packetBytes)][length (1 byte, run = length + 1)]
        unpacked.resize(expectedRawBytes);
        size_t sp = payloadOff;
        size_t decodedPx = 0;
        const size_t rleEntrySize = packetBytes + 1u;
        while (decodedPx < totalPixels && sp + rleEntrySize <= size) {
            const uint8_t* pkt = data + sp;
            const size_t runLen = static_cast<size_t>(data[sp + packetBytes]) + 1u;
            sp += rleEntrySize;
            const size_t take = std::min(runLen, totalPixels - decodedPx);
            for (size_t k = 0; k < take; ++k) {
                std::memcpy(&unpacked[(decodedPx + k) * packetBytes], pkt, packetBytes);
            }
            decodedPx += take;
        }
        if (decodedPx != totalPixels) return false;
        rawPixels = unpacked.data();
    } else {
        return false;
    }

    outInfo = {};
    outInfo.width = cols;
    outInfo.height = rows;
    outInfo.origWidth = cols;
    outInfo.origHeight = rows;
    outInfo.channels = hasAlpha ? 4u : (isGray && !isPseudo ? 1u : 3u);
    outInfo.bitsPerChannel = depth;
    outInfo.formatName = L"ImageMagick MIFF Raster (.miff)";
    outInfo.bgraPixels.resize(totalPixels * 4u);

    for (size_t i = 0; i < totalPixels; ++i) {
        const uint8_t* px = rawPixels + i * packetBytes;
        uint8_t r = 0, g = 0, b = 0, a = 255u;
        if (isPseudo) {
            const uint32_t idx = (idxBytes == 2u) ? ByteIO::RdU16BE(px) : px[0];
            if (idx < palette.size()) {
                r = palette[idx][0];
                g = palette[idx][1];
                b = palette[idx][2];
            }
            if (hasAlpha) a = ReadMiffSample8(px + idxBytes, depthBytes);
        } else if (isGray) {
            r = g = b = ReadMiffSample8(px, depthBytes);
            if (hasAlpha) a = ReadMiffSample8(px + depthBytes, depthBytes);
        } else if (isCmyk) {
            const uint8_t c = ReadMiffSample8(px + 0u * depthBytes, depthBytes);
            const uint8_t m = ReadMiffSample8(px + 1u * depthBytes, depthBytes);
            const uint8_t y = ReadMiffSample8(px + 2u * depthBytes, depthBytes);
            const uint8_t k = ReadMiffSample8(px + 3u * depthBytes, depthBytes);
            r = static_cast<uint8_t>(((255u - c) * (255u - k) + 127u) / 255u);
            g = static_cast<uint8_t>(((255u - m) * (255u - k) + 127u) / 255u);
            b = static_cast<uint8_t>(((255u - y) * (255u - k) + 127u) / 255u);
            if (hasAlpha) a = ReadMiffSample8(px + 4u * depthBytes, depthBytes);
        } else {
            r = ReadMiffSample8(px + 0u * depthBytes, depthBytes);
            g = ReadMiffSample8(px + 1u * depthBytes, depthBytes);
            b = ReadMiffSample8(px + 2u * depthBytes, depthBytes);
            if (hasAlpha) a = ReadMiffSample8(px + 3u * depthBytes, depthBytes);
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
