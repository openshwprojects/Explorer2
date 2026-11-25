#pragma once
// PhotoshopPat.h — Adobe Photoshop ('8BPT') Seamless Pattern Decoder (.pat)

namespace Formats {

inline bool DecodePhotoshopPat(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 28) return false;
        if (size >= 28 && std::memcmp(data, "8BPT", 4) == 0) {
            size_t pos = 0;
            uint32_t patCount = 0;
            if (DetailLayeredAndHdrImg::RdU16BE(data + 4) == 1) {
                patCount = DetailLayeredAndHdrImg::RdU32BE(data + 6);
                pos = 10;
            } else if (DetailLayeredAndHdrImg::RdU32BE(data + 4) == 1) {
                patCount = DetailLayeredAndHdrImg::RdU32BE(data + 8);
                pos = 12;
            } else {
                return false;
            }
            if (patCount == 0 || patCount > 4096 || pos + 16 > size) return false;

            uint32_t patVer    = DetailLayeredAndHdrImg::RdU32BE(data + pos + 0);
            uint32_t imageMode = DetailLayeredAndHdrImg::RdU32BE(data + pos + 4);
            uint16_t patH      = DetailLayeredAndHdrImg::RdU16BE(data + pos + 8);
            uint16_t patW      = DetailLayeredAndHdrImg::RdU16BE(data + pos + 10);
            uint32_t nameChars = DetailLayeredAndHdrImg::RdU32BE(data + pos + 12);
            pos += 16;
            if (patVer != 1 || patW == 0 || patH == 0 || patW > 4096 || patH > 4096 || nameChars > 2048) {
                return false;
            }
            if (pos + static_cast<size_t>(nameChars) * 2 + 1 > size) return false;

            std::wstring patName;
            for (uint32_t i = 0; i < nameChars; ++i) {
                wchar_t wc = static_cast<wchar_t>(DetailLayeredAndHdrImg::RdU16BE(data + pos + static_cast<size_t>(i) * 2));
                if (wc != 0) patName.push_back(wc);
            }
            pos += static_cast<size_t>(nameChars) * 2;

            uint8_t idLen = data[pos++];
            if (pos + idLen > size) return false;
            pos += idLen;

            uint8_t palRgb[768] = {};
            uint16_t transIdx = 0xFFFFu;
            if (imageMode == 2) {
                if (pos + 772 > size) return false;
                std::memcpy(palRgb, data + pos, 768);
                transIdx = DetailLayeredAndHdrImg::RdU16BE(data + pos + 768);
                pos += 772;
            }

            if (pos + 5 <= size && DetailLayeredAndHdrImg::RdU32BE(data + pos) != 3 &&
                data[pos] == 0 && DetailLayeredAndHdrImg::RdU32BE(data + pos + 1) == 3)
            {
                ++pos;
            }
            if (pos + 28 > size) return false;

            uint32_t vmaVer     = DetailLayeredAndHdrImg::RdU32BE(data + pos + 0);
            uint32_t vmaLen     = DetailLayeredAndHdrImg::RdU32BE(data + pos + 4);
            int32_t  vTop       = static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(data + pos + 8));
            int32_t  vLeft      = static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(data + pos + 12));
            int32_t  vBottom    = static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(data + pos + 16));
            int32_t  vRight     = static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(data + pos + 20));
            uint32_t maxChans   = DetailLayeredAndHdrImg::RdU32BE(data + pos + 24);
            (void)vmaLen;
            pos += 28;
            if (vmaVer != 3 || maxChans > 64) return false;

            uint32_t tileW = (vRight > vLeft && (vRight - vLeft) <= 4096) ? static_cast<uint32_t>(vRight - vLeft) : patW;
            uint32_t tileH = (vBottom > vTop && (vBottom - vTop) <= 4096) ? static_cast<uint32_t>(vBottom - vTop) : patH;
            size_t tilePx = static_cast<size_t>(tileW) * tileH;

            struct DecodedChan {
                uint32_t slot = 0;
                std::vector<uint8_t> plane;
            };
            std::vector<DecodedChan> chans;
            uint32_t totalSlots = maxChans + 2u;

            for (uint32_t slot = 0; slot < totalSlots && pos + 4 <= size; ++slot) {
                uint32_t isWritten = DetailLayeredAndHdrImg::RdU32BE(data + pos);
                pos += 4;
                if (isWritten == 0) continue;
                if (pos + 4 > size) return false;
                uint32_t chanLen = DetailLayeredAndHdrImg::RdU32BE(data + pos);
                pos += 4;
                if (chanLen == 0) continue;
                if (chanLen < 23 || pos + chanLen > size) return false;
                size_t chanEnd = pos + chanLen;

                uint32_t pixDepth = DetailLayeredAndHdrImg::RdU32BE(data + pos + 0);
                int32_t  cTop     = static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(data + pos + 4));
                int32_t  cLeft    = static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(data + pos + 8));
                int32_t  cBottom  = static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(data + pos + 12));
                int32_t  cRight   = static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(data + pos + 16));
                uint16_t depth16  = DetailLayeredAndHdrImg::RdU16BE(data + pos + 20);
                uint8_t  comp     = data[pos + 22];
                const uint8_t* csrc = data + pos + 23;
                size_t clen = chanLen - 23;

                uint32_t effDepth = (depth16 == 8 || depth16 == 16) ? depth16 : pixDepth;
                uint32_t chW = (cRight > cLeft) ? static_cast<uint32_t>(cRight - cLeft) : tileW;
                uint32_t chH = (cBottom > cTop) ? static_cast<uint32_t>(cBottom - cTop) : tileH;
                if (chW == 0 || chH == 0 || chW > 4096 || chH > 4096 || (effDepth != 8 && effDepth != 16)) {
                    return false;
                }

                size_t sampleBytes = (effDepth == 16) ? 2u : 1u;
                size_t rawRowBytes = static_cast<size_t>(chW) * sampleBytes;
                std::vector<uint8_t> rawPlane(rawRowBytes * chH, 0);

                if (comp == 0) {
                    if (clen < rawPlane.size()) return false;
                    std::memcpy(rawPlane.data(), csrc, rawPlane.size());
                } else if (comp == 1) {
                    size_t tblBytes = static_cast<size_t>(chH) * 2u;
                    if (clen < tblBytes) return false;
                    size_t rpos = tblBytes;
                    for (uint32_t ry = 0; ry < chH; ++ry) {
                        uint16_t rowCnt = DetailLayeredAndHdrImg::RdU16BE(csrc + static_cast<size_t>(ry) * 2u);
                        if (rpos + rowCnt > clen) return false;
                        const uint8_t* rp = csrc + rpos;
                        const uint8_t* rend = rp + rowCnt;
                        uint8_t* dstRow = rawPlane.data() + static_cast<size_t>(ry) * rawRowBytes;
                        size_t dIdx = 0;
                        while (rp < rend && dIdx < rawRowBytes) {
                            int8_t n = static_cast<int8_t>(*rp++);
                            if (n >= 0) {
                                size_t cnt = static_cast<size_t>(n) + 1u;
                                if (rp + cnt > rend || dIdx + cnt > rawRowBytes) return false;
                                std::memcpy(dstRow + dIdx, rp, cnt);
                                rp += cnt;
                                dIdx += cnt;
                            } else if (n != -128) {
                                size_t cnt = static_cast<size_t>(1 - static_cast<int>(n));
                                if (rp >= rend || dIdx + cnt > rawRowBytes) return false;
                                uint8_t val = *rp++;
                                std::memset(dstRow + dIdx, val, cnt);
                                dIdx += cnt;
                            }
                        }
                        if (dIdx < rawRowBytes) return false;
                        rpos += rowCnt;
                    }
                } else {
                    return false;
                }

                DecodedChan dc;
                dc.slot = slot;
                dc.plane.assign(tilePx, (slot >= 3) ? 255 : 0);
                uint16_t maxV16 = 0u;
                if (sampleBytes == 2u) {
                    const size_t nChPx = static_cast<size_t>(chW) * chH;
                    for (size_t i = 0; i < nChPx; ++i) {
                        uint16_t s16 = DetailLayeredAndHdrImg::RdU16BE(&rawPlane[i * 2u]);
                        if (s16 > maxV16) maxV16 = s16;
                    }
                }
                const uint32_t white16 = (maxV16 <= 32768u) ? 32768u : 65535u;
                int offX = cLeft - vLeft;
                int offY = cTop - vTop;
                for (uint32_t y = 0; y < chH; ++y) {
                    int dy = offY + static_cast<int>(y);
                    if (dy < 0 || dy >= static_cast<int>(tileH)) continue;
                    for (uint32_t x = 0; x < chW; ++x) {
                        int dx = offX + static_cast<int>(x);
                        if (dx < 0 || dx >= static_cast<int>(tileW)) continue;
                        uint8_t v8 = 0;
                        if (sampleBytes == 2u) {
                            uint16_t v16 = DetailLayeredAndHdrImg::RdU16BE(&rawPlane[(static_cast<size_t>(y) * chW + x) * 2u]);
                            v8 = static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(v16) * 255u + (white16 / 2u)) / white16));
                        } else {
                            v8 = rawPlane[static_cast<size_t>(y) * chW + x];
                        }
                        dc.plane[static_cast<size_t>(dy) * tileW + dx] = v8;
                    }
                }
                chans.push_back(std::move(dc));
                pos = chanEnd;
            }

            auto findSlot = [&](uint32_t s) -> const std::vector<uint8_t>* {
                for (const auto& c : chans) {
                    if (c.slot == s) return &c.plane;
                }
                return nullptr;
            };

            const std::vector<uint8_t>* ch0 = findSlot(0);
            const std::vector<uint8_t>* ch1 = findSlot(1);
            const std::vector<uint8_t>* ch2 = findSlot(2);
            const std::vector<uint8_t>* ch3 = findSlot(3);
            const std::vector<uint8_t>* alphaPlane = nullptr;
            for (const auto& c : chans) {
                if ((imageMode == 1 && c.slot >= 1) ||
                    (imageMode == 2 && c.slot >= 1) ||
                    (imageMode == 3 && c.slot >= 3) ||
                    (imageMode == 4 && c.slot >= 4))
                {
                    alphaPlane = &c.plane;
                    break;
                }
            }

            if (imageMode == 1 && !ch0) return false;
            if (imageMode == 2 && !ch0) return false;
            if (imageMode == 3 && (!ch0 || !ch1 || !ch2)) return false;
            if (imageMode == 4 && (!ch0 || !ch1 || !ch2 || !ch3)) return false;
            if (imageMode != 1 && imageMode != 2 && imageMode != 3 && imageMode != 4) return false;

            std::vector<uint8_t> singleTile(tilePx * 4u);
            for (size_t i = 0; i < tilePx; ++i) {
                uint8_t r = 0, g = 0, b = 0, a = alphaPlane ? (*alphaPlane)[i] : 255;
                if (imageMode == 1) {
                    r = g = b = (*ch0)[i];
                } else if (imageMode == 2) {
                    uint8_t idx = (*ch0)[i];
                    r = palRgb[static_cast<size_t>(idx) * 3u + 0];
                    g = palRgb[static_cast<size_t>(idx) * 3u + 1];
                    b = palRgb[static_cast<size_t>(idx) * 3u + 2];
                    if (idx == transIdx) a = 0;
                } else if (imageMode == 3) {
                    r = (*ch0)[i];
                    g = (*ch1)[i];
                    b = (*ch2)[i];
                } else {
                    uint32_t c = (*ch0)[i], m = (*ch1)[i], y = (*ch2)[i], k = (*ch3)[i];
                    r = static_cast<uint8_t>((c * k + 127u) / 255u);
                    g = static_cast<uint8_t>((m * k + 127u) / 255u);
                    b = static_cast<uint8_t>((y * k + 127u) / 255u);
                }
                WritePremulBGRA(&singleTile[i * 4u], r, g, b, a);
            }

            uint32_t tilesX = std::max(1u, (128u + tileW - 1u) / tileW);
            uint32_t tilesY = std::max(1u, (128u + tileH - 1u) / tileH);
            uint32_t outW = tileW * tilesX;
            uint32_t outH = tileH * tilesY;

            outInfo.width = outW;
            outInfo.height = outH;
            outInfo.origWidth = tileW;
            outInfo.origHeight = tileH;
            outInfo.channels = 4;
            outInfo.bitsPerChannel = 8;
            outInfo.formatName = L"Adobe Photoshop Pattern (.pat)";
            if (!patName.empty()) outInfo.layerNames.push_back(patName);
            outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4u);

            for (uint32_t y = 0; y < outH; ++y) {
                uint32_t sy = y % tileH;
                for (uint32_t x = 0; x < outW; ++x) {
                    uint32_t sx = x % tileW;
                    std::memcpy(&outInfo.bgraPixels[(static_cast<size_t>(y) * outW + x) * 4u],
                                &singleTile[(static_cast<size_t>(sy) * tileW + sx) * 4u], 4u);
                }
            }
            outInfo.valid = true;
            return true;
        }
    return false;
}

} // namespace Formats
