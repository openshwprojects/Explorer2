#pragma once
// Fits.h — NASA Flexible Image Transport System (.fits / .fit / .fts) BITPIX 8/16/32/64/-32/-64 & IMAGE Extension Decoder

namespace Formats {

inline bool DecodeFITS(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 2880 || std::memcmp(data, "SIMPLE  =", 9) != 0) return false;

    auto trimFitsVal = [](std::string_view sv) -> std::string_view {
        size_t slash = std::string_view::npos;
        bool inStr = false;
        for (size_t i = 0; i < sv.size(); ++i) {
            if (sv[i] == '\'') inStr = !inStr;
            else if (sv[i] == '/' && !inStr) { slash = i; break; }
        }
        if (slash != std::string_view::npos) sv = sv.substr(0, slash);
        while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
        while (!sv.empty() && sv.back() == ' ') sv.remove_suffix(1);
        return sv;
    };

    // Walk Primary HDU and any subsequent XTENSION = 'IMAGE' HDUs when Primary HDU has NAXIS == 0
    size_t hduStart = 0;
    for (int hduIdx = 0; hduIdx < 32 && hduStart + 2880u <= size; ++hduIdx) {
        const bool isPrimary = (hduIdx == 0);
        if (!isPrimary && std::memcmp(data + hduStart, "XTENSION=", 9) != 0) break;

        bool isImageExt = isPrimary;
        int bitpix = 0, naxis = 0;
        int axes[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        int64_t pcount = 0, gcount = 1;
        double bscale = 1.0, bzero = 0.0;
        size_t hdrEnd = 0;

        for (size_t off = hduStart; off + 80u <= size; off += 80u) {
            std::string_view card(reinterpret_cast<const char*>(data + off), 80u);
            if (card.rfind("END     ", 0) == 0) {
                hdrEnd = hduStart + (((off - hduStart) + 80u + 2879u) / 2880u) * 2880u;
                break;
            }
            size_t eq = card.find('=');
            if (eq == std::string_view::npos || eq > 8u) continue;
            std::string_view key = card.substr(0, eq);
            while (!key.empty() && key.back() == ' ') key.remove_suffix(1);
            std::string_view valSv = trimFitsVal(card.substr(eq + 1u));
            if (valSv.empty()) continue;

            if (key == "XTENSION") {
                if (valSv.size() >= 2u && valSv.front() == '\'') {
                    valSv.remove_prefix(1);
                    size_t q = valSv.find('\'');
                    if (q != std::string_view::npos) valSv = valSv.substr(0, q);
                    while (!valSv.empty() && valSv.back() == ' ') valSv.remove_suffix(1);
                }
                isImageExt = (valSv == "IMAGE");
            } else if (key == "BITPIX") {
                bitpix = std::atoi(std::string(valSv).c_str());
            } else if (key == "NAXIS") {
                naxis = std::atoi(std::string(valSv).c_str());
            } else if (key.size() == 6u && key.rfind("NAXIS", 0) == 0 && key[5] >= '1' && key[5] <= '8') {
                axes[key[5] - '1'] = std::atoi(std::string(valSv).c_str());
            } else if (key == "PCOUNT") {
                pcount = std::atoll(std::string(valSv).c_str());
            } else if (key == "GCOUNT") {
                gcount = std::max<int64_t>(1, std::atoll(std::string(valSv).c_str()));
            } else if (key == "BSCALE") {
                bscale = std::strtod(std::string(valSv).c_str(), nullptr);
                if (!std::isfinite(bscale) || bscale == 0.0) bscale = 1.0;
            } else if (key == "BZERO") {
                bzero = std::strtod(std::string(valSv).c_str(), nullptr);
                if (!std::isfinite(bzero)) bzero = 0.0;
            }
        }

        if (hdrEnd == 0 || hdrEnd > size) return false;
        const int bytesPerElem = std::abs(bitpix) / 8;
        if (bitpix != 8 && bitpix != 16 && bitpix != 32 && bitpix != 64 && bitpix != -32 && bitpix != -64) {
            return false;
        }

        // Compute total HDU data payload size to advance cleanly if this HDU has NAXIS == 0 or is a BINTABLE
        uint64_t axisProd = (naxis > 0) ? 1ull : 0ull;
        for (int a = 0; a < std::min(naxis, 8); ++a) {
            if (axes[a] < 0) return false;
            axisProd *= static_cast<uint64_t>(axes[a]);
        }
        const uint64_t rawDataBytes = static_cast<uint64_t>(bytesPerElem) *
                                      static_cast<uint64_t>(gcount) *
                                      (static_cast<uint64_t>(std::max<int64_t>(0, pcount)) + axisProd);
        const uint64_t paddedDataBytes = ((rawDataBytes + 2879ull) / 2880ull) * 2880ull;

        const int w = (naxis >= 1) ? axes[0] : 0;
        const int h = (naxis >= 2) ? axes[1] : ((naxis == 1 && axes[0] > 0) ? 1 : 0);
        const int planes = (naxis >= 3) ? std::max(1, axes[2]) : 1;

        if (!isImageExt || naxis < 2 || w <= 0 || h <= 0) {
            if (paddedDataBytes > size - hdrEnd) break;
            hduStart = hdrEnd + static_cast<size_t>(paddedDataBytes);
            continue;
        }

        if (w > 8192 || h > 8192) return false;
        const size_t planeElems = static_cast<size_t>(w) * h;
        if (hdrEnd >= size || planeElems * bytesPerElem > size - hdrEnd) return false;

        const bool hasRGB = (planes >= 3 && planeElems * 3u * bytesPerElem <= size - hdrEnd);
        const size_t activePlanes = hasRGB ? 3u : 1u;
        const size_t totalActiveSamples = planeElems * activePlanes;

        const uint8_t* payload = data + hdrEnd;
        auto sampleVal = [&](size_t idx) -> double {
            const uint8_t* p = payload + idx * bytesPerElem;
            double raw = 0.0;
            if (bitpix == 8) {
                raw = static_cast<double>(p[0]);
            } else if (bitpix == 16) {
                raw = static_cast<double>(static_cast<int16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]));
            } else if (bitpix == 32) {
                int32_t v = static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 24) |
                                                 (static_cast<uint32_t>(p[1]) << 16) |
                                                 (static_cast<uint32_t>(p[2]) << 8)  |
                                                 static_cast<uint32_t>(p[3]));
                raw = static_cast<double>(v);
            } else if (bitpix == 64) {
                uint64_t u = (static_cast<uint64_t>(ReadU32BE(p)) << 32) | ReadU32BE(p + 4);
                raw = static_cast<double>(static_cast<int64_t>(u));
            } else if (bitpix == -32) {
                uint32_t u = ReadU32BE(p);
                float f = 0.0f;
                std::memcpy(&f, &u, 4);
                raw = std::isfinite(f) ? static_cast<double>(f) : 0.0;
            } else if (bitpix == -64) {
                uint64_t u = (static_cast<uint64_t>(ReadU32BE(p)) << 32) | ReadU32BE(p + 4);
                double d = 0.0;
                std::memcpy(&d, &u, 8);
                raw = std::isfinite(d) ? d : 0.0;
            }
            const double phys = raw * bscale + bzero;
            return std::isfinite(phys) ? phys : 0.0;
        };

        // Global normalization across all active planes (preserves true 3D RGB channel balance!)
        double minV = 1e300, maxV = -1e300;
        for (size_t i = 0; i < totalActiveSamples; ++i) {
            const double v = sampleVal(i);
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        if (minV > maxV) { minV = 0.0; maxV = 1.0; }
        const double span = (maxV > minV) ? (maxV - minV) : 1.0;

        outInfo.width = static_cast<uint32_t>(w);
        outInfo.height = static_cast<uint32_t>(h);
        outInfo.origWidth = outInfo.width;
        outInfo.origHeight = outInfo.height;
        outInfo.channels = hasRGB ? 3u : 1u;
        outInfo.bitsPerChannel = static_cast<uint32_t>(std::abs(bitpix));
        outInfo.formatName = L"NASA FITS Astronomical Image (.fits)";
        outInfo.bgraPixels.resize(planeElems * 4u);

        for (int y = 0; y < h; ++y) {
            const int dstY = h - 1 - y;
            for (int x = 0; x < w; ++x) {
                const size_t idx = static_cast<size_t>(y) * w + x;
                auto norm8 = [&](size_t sIdx) -> uint8_t {
                    double t = (sampleVal(sIdx) - minV) / span;
                    return static_cast<uint8_t>(std::clamp(t * 255.0 + 0.5, 0.0, 255.0));
                };
                const uint8_t r = norm8(idx);
                const uint8_t g = hasRGB ? norm8(planeElems + idx) : r;
                const uint8_t b = hasRGB ? norm8(planeElems * 2u + idx) : r;
                WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(dstY) * w + x) * 4u], r, g, b, 255u);
            }
        }
        outInfo.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
