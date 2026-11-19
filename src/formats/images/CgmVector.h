#pragma once
// CgmVector.h — ISO/IEC 8632 Computer Graphics Metafile Clear-Text & Binary Vector Renderer (.cgm)

namespace Formats {

// ============================================================================
// #15. Computer Graphics Metafile (.cgm — ISO/IEC 8632 Clear-Text & Binary)
// ============================================================================
inline bool DecodeCgmVector(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 6) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8u * 1024u * 1024u));
    const bool isBinaryCgm = ((DetailImg100::RdU16BE(data) & 0xFFE0u) == 0x0020u); // Class 0, ID 1 = BEGIN METAFILE

    // Case-insensitive Clear-Text CGM header probe ("BEGMF" / "BEG_MF")
    bool isTextCgm = false;
    if (!isBinaryCgm) {
        size_t probeLen = std::min<size_t>(sv.size(), 2048u);
        std::string headNorm;
        headNorm.reserve(probeLen);
        for (size_t i = 0; i < probeLen; ++i) {
            char c = sv[i];
            if (c == '_' || c == '$') continue;
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
            headNorm.push_back(c);
        }
        isTextCgm = (headNorm.find("BEGMF") != std::string::npos);
    }
    if (!isBinaryCgm && !isTextCgm) return false;

    uint8_t colTable[256][3] = {};
    // ISO 8632 default indexed palette
    static const uint8_t kDefaultCgmPal[8][3] = {
        {248, 250, 252}, {15,  23,  42},  {220, 38,  38},  {22,  163, 74},
        {37,  99,  235}, {234, 179, 8},   {192, 38,  211}, {8,   145, 178}
    };
    for (int i = 0; i < 8; ++i) {
        colTable[i][0] = kDefaultCgmPal[i][0];
        colTable[i][1] = kDefaultCgmPal[i][1];
        colTable[i][2] = kDefaultCgmPal[i][2];
    }
    for (int i = 8; i < 256; ++i) {
        colTable[i][0] = static_cast<uint8_t>((i * 53) & 0xFF);
        colTable[i][1] = static_cast<uint8_t>((i * 97) & 0xFF);
        colTable[i][2] = static_cast<uint8_t>((i * 193) & 0xFF);
    }

    uint8_t bgR = 248, bgG = 250, bgB = 252;
    uint8_t lineR = 15, lineG = 23, lineB = 42;
    uint8_t fillR = 59, fillG = 130, fillB = 246;
    uint8_t edgeR = 15, edgeG = 23, edgeB = 42;
    int interiorStyle = 1; // 0=hollow, 1=solid, 2=pattern, 3=hatch, 4=empty
    bool edgeVisible = false;
    float lineWidthVdc = 0.0f;

    bool hasExplicitExtent = false;
    float vx0 = 0.0f, vy0 = 0.0f, vx1 = 1000.0f, vy1 = 1000.0f;

    struct CgmCmd {
        int kind = 0; // 0=polyline, 1=disjoint, 2=polygonRings
        bool doFill = false;
        bool doOutline = false;
        uint8_t fR = 0, fG = 0, fB = 0;
        uint8_t sR = 0, sG = 0, sB = 0;
        float widthVdc = 0.0f;
        std::vector<std::vector<DetailImg200::Pt2f>> rings;
        std::vector<std::pair<DetailImg200::Pt2f, DetailImg200::Pt2f>> explicitEdges;
    };
    std::vector<CgmCmd> cmds;

    auto makeArc3Pt = [](float x1, float y1, float x2, float y2, float x3, float y3, int closeType, std::vector<DetailImg200::Pt2f>& outPts) {
        outPts.clear();
        float d = 2.0f * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));
        if (std::abs(d) < 1e-4f) {
            outPts.push_back({ x1, y1 });
            outPts.push_back({ x2, y2 });
            outPts.push_back({ x3, y3 });
            return;
        }
        float s1 = x1 * x1 + y1 * y1;
        float s2 = x2 * x2 + y2 * y2;
        float s3 = x3 * x3 + y3 * y3;
        float ux = (s1 * (y2 - y3) + s2 * (y3 - y1) + s3 * (y1 - y2)) / d;
        float uy = (s1 * (x3 - x2) + s2 * (x1 - x3) + s3 * (x2 - x1)) / d;
        float rad = std::sqrt((x1 - ux) * (x1 - ux) + (y1 - uy) * (y1 - uy));

        const float twoPi = 6.283185307f;
        auto wrap2Pi = [&](float a) -> float {
            while (a < 0.0f) a += twoPi;
            while (a >= twoPi) a -= twoPi;
            return a;
        };
        float a1 = std::atan2(y1 - uy, x1 - ux);
        float a2 = std::atan2(y2 - uy, x2 - ux);
        float a3 = std::atan2(y3 - uy, x3 - ux);
        float d12 = wrap2Pi(a2 - a1);
        float d13 = wrap2Pi(a3 - a1);
        float sweep = (d12 <= d13) ? d13 : -(twoPi - d13);

        const int steps = 36;
        for (int s = 0; s <= steps; ++s) {
            float ang = a1 + sweep * (static_cast<float>(s) / static_cast<float>(steps));
            outPts.push_back({ ux + rad * std::cos(ang), uy + rad * std::sin(ang) });
        }
        if (closeType == 0) outPts.push_back({ ux, uy }); // Pie closure
    };

    auto makeArcCtr = [](float cx, float cy, float dx1, float dy1, float dx2, float dy2, float rad, int closeType, std::vector<DetailImg200::Pt2f>& outPts) {
        outPts.clear();
        if (rad <= 0.0f) return;
        const float twoPi = 6.283185307f;
        float a1 = std::atan2(dy1, dx1);
        float a2 = std::atan2(dy2, dx2);
        float sweep = a2 - a1;
        while (sweep <= 0.0f) sweep += twoPi;
        const int steps = 36;
        for (int s = 0; s <= steps; ++s) {
            float ang = a1 + sweep * (static_cast<float>(s) / static_cast<float>(steps));
            outPts.push_back({ cx + rad * std::cos(ang), cy + rad * std::sin(ang) });
        }
        if (closeType == 0) outPts.push_back({ cx, cy }); // Pie closure
    };

    auto pushClosedShape = [&](std::vector<DetailImg200::Pt2f>&& ring) {
        if (ring.size() < 3) return;
        CgmCmd cmd;
        cmd.kind = 2;
        cmd.doFill = (interiorStyle != 0 && interiorStyle != 4);
        cmd.doOutline = (interiorStyle == 0 || edgeVisible);
        cmd.fR = fillR; cmd.fG = fillG; cmd.fB = fillB;
        cmd.sR = (interiorStyle == 0) ? fillR : edgeR;
        cmd.sG = (interiorStyle == 0) ? fillG : edgeG;
        cmd.sB = (interiorStyle == 0) ? fillB : edgeB;
        cmd.widthVdc = lineWidthVdc;
        cmd.rings.push_back(std::move(ring));
        cmds.push_back(std::move(cmd));
    };

    if (isBinaryCgm) {
        int vdcType = 0;       // 0=integer, 1=real
        int intPrec = 16;      // 8, 16, 24, 32
        int vdcIntPrec = 16;   // 8, 16, 24, 32
        bool explicitVdcIntPrec = false;
        int vdcRealForm = 1;   // 0=IEEE float, 1=fixed-point (ISO default 16.16)
        int vdcRealExp  = 16;
        int vdcRealMant = 16;
        int colPrec = 8;       // 8 or 16
        int colIdxPrec = 8;    // 8, 16, 24, 32
        int colorMode = 0;     // 0=indexed, 1=direct RGB

        auto readSignedInt = [](const uint8_t* p, int bits) -> int32_t {
            if (bits == 8) return static_cast<int8_t>(p[0]);
            if (bits == 16) return static_cast<int16_t>(DetailImg100::RdU16BE(p));
            if (bits == 24) {
                int32_t v = (static_cast<int32_t>(p[0]) << 16) | (static_cast<int32_t>(p[1]) << 8) | p[2];
                if (v & 0x800000) v -= 0x1000000;
                return v;
            }
            return static_cast<int32_t>(DetailImg100::RdU32BE(p));
        };

        auto vdcScalarSize = [&]() -> size_t {
            if (vdcType == 0) return static_cast<size_t>(std::clamp(vdcIntPrec, 8, 32) / 8);
            if (vdcRealForm == 0 && vdcRealExp == 12 && vdcRealMant == 52) return 8u;
            if (vdcRealForm == 1 && vdcRealExp == 32 && vdcRealMant == 32) return 8u;
            return 4u;
        };

        auto readVdcScalar = [&](const uint8_t* p) -> float {
            if (vdcType == 0) {
                return static_cast<float>(readSignedInt(p, vdcIntPrec));
            }
            if (vdcRealForm == 0) {
                if (vdcRealExp == 12 && vdcRealMant == 52) {
                    uint64_t hi = DetailImg100::RdU32BE(p);
                    uint64_t lo = DetailImg100::RdU32BE(p + 4);
                    uint64_t u = (hi << 32) | lo;
                    double d = 0.0;
                    std::memcpy(&d, &u, 8);
                    return static_cast<float>(d);
                }
                uint32_t u = DetailImg100::RdU32BE(p);
                float f = 0.0f;
                std::memcpy(&f, &u, 4);
                return f;
            }
            if (vdcRealExp == 32 && vdcRealMant == 32) {
                int32_t whole = static_cast<int32_t>(DetailImg100::RdU32BE(p));
                uint32_t frac = DetailImg100::RdU32BE(p + 4);
                return static_cast<float>(whole) + static_cast<float>(frac) / 4294967296.0f;
            }
            // Fixed-point 16.16
            int32_t raw = static_cast<int32_t>(DetailImg100::RdU32BE(p));
            return static_cast<float>(raw) / 65536.0f;
        };

        auto readColor = [&](const uint8_t* p, size_t len, bool forceDirect, uint8_t& outR, uint8_t& outG, uint8_t& outB) {
            if (colorMode == 1 || forceDirect) {
                if (colPrec == 16 && len >= 6) {
                    outR = p[0]; outG = p[2]; outB = p[4];
                } else if (len >= 3) {
                    outR = p[0]; outG = p[1]; outB = p[2];
                }
            } else {
                uint32_t idx = 1;
                if (colIdxPrec == 8 && len >= 1) idx = p[0];
                else if (colIdxPrec == 16 && len >= 2) idx = DetailImg100::RdU16BE(p);
                else if (colIdxPrec == 24 && len >= 3) idx = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
                else if (colIdxPrec == 32 && len >= 4) idx = DetailImg100::RdU32BE(p);
                else if (len >= 1) idx = p[0];
                idx &= 0xFFu;
                outR = colTable[idx][0];
                outG = colTable[idx][1];
                outB = colTable[idx][2];
            }
        };

        size_t p = 0;
        std::vector<uint8_t> partBuf;
        while (p + 2 <= size && cmds.size() < 200000) {
            uint16_t hdr = DetailImg100::RdU16BE(data + p);
            p += 2;
            int cls = (hdr >> 12) & 0x0F;
            int eid = (hdr >> 5) & 0x7F;
            uint32_t shortLen = hdr & 0x1Fu;

            const uint8_t* payload = nullptr;
            size_t plen = 0;
            if (shortLen < 31u) {
                if (p + shortLen > size) break;
                payload = data + p;
                plen = shortLen;
                p += static_cast<size_t>(shortLen) + (shortLen & 1u);
            } else {
                // Partitioned long-form command header (bit 15 = more partitions)
                partBuf.clear();
                bool ok = true;
                for (int parts = 0; parts < 2048; ++parts) {
                    if (p + 2 > size) { ok = false; break; }
                    uint16_t pw = DetailImg100::RdU16BE(data + p);
                    p += 2;
                    bool more = ((pw & 0x8000u) != 0);
                    uint32_t chunk = pw & 0x7FFFu;
                    if (p + chunk > size || partBuf.size() + chunk > 16u * 1024u * 1024u) { ok = false; break; }
                    partBuf.insert(partBuf.end(), data + p, data + p + chunk);
                    p += static_cast<size_t>(chunk) + (chunk & 1u);
                    if (!more) break;
                }
                if (!ok) break;
                payload = partBuf.data();
                plen = partBuf.size();
            }

            if (cls == 0 && eid == 2) break; // END METAFILE

            if (cls == 1) {
                int stepI = std::clamp(intPrec, 8, 32) / 8;
                if (eid == 3 && plen >= 2) { // VDC TYPE (0=integer, 1=real)
                    uint16_t vt = DetailImg100::RdU16BE(payload);
                    if (vt <= 1) vdcType = vt;
                } else if (eid == 4 && plen >= static_cast<size_t>(stepI)) { // INTEGER PRECISION
                    int v = readSignedInt(payload, intPrec);
                    if (v == 8 || v == 16 || v == 24 || v == 32) {
                        intPrec = v;
                        if (!explicitVdcIntPrec) vdcIntPrec = v;
                    }
                } else if (eid == 5 && plen >= static_cast<size_t>(stepI) * 3u) { // REAL PRECISION
                    int rf = readSignedInt(payload, intPrec);
                    int re = readSignedInt(payload + stepI, intPrec);
                    int rm = readSignedInt(payload + stepI * 2, intPrec);
                    if (rf == 0 || rf == 1) { vdcRealForm = rf; vdcRealExp = re; vdcRealMant = rm; }
                } else if (eid == 7 && plen >= static_cast<size_t>(stepI)) { // COLOUR PRECISION
                    int v = readSignedInt(payload, intPrec);
                    if (v == 8 || v == 16) colPrec = v;
                } else if (eid == 8 && plen >= static_cast<size_t>(stepI)) { // COLOUR INDEX PRECISION
                    int v = readSignedInt(payload, intPrec);
                    if (v == 8 || v == 16 || v == 24 || v == 32) colIdxPrec = v;
                }
            } else if (cls == 2) {
                if (eid == 2 && plen >= 2) { // COLOUR SELECTION MODE (0=indexed, 1=direct)
                    uint16_t cm = DetailImg100::RdU16BE(payload);
                    if (cm <= 1) colorMode = cm;
                } else if (eid == 6) { // VDC EXTENT
                    size_t vs = vdcScalarSize();
                    if (plen >= vs * 4u) {
                        vx0 = readVdcScalar(payload + 0 * vs);
                        vy0 = readVdcScalar(payload + 1 * vs);
                        vx1 = readVdcScalar(payload + 2 * vs);
                        vy1 = readVdcScalar(payload + 3 * vs);
                        hasExplicitExtent = true;
                    }
                } else if (eid == 7) { // BACKGROUND COLOUR (always direct RGB)
                    readColor(payload, plen, true, bgR, bgG, bgB);
                }
            } else if (cls == 3) {
                int stepI = std::clamp(intPrec, 8, 32) / 8;
                if (eid == 1 && plen >= 1) { // VDC INTEGER PRECISION (or 0/1 VDC TYPE alias)
                    int v = (plen == 2) ? static_cast<int16_t>(DetailImg100::RdU16BE(payload)) : readSignedInt(payload, intPrec);
                    if (v == 8 || v == 16 || v == 24 || v == 32) {
                        vdcIntPrec = v;
                        explicitVdcIntPrec = true;
                    } else if (v == 0 || v == 1) {
                        vdcType = v;
                    }
                } else if (eid == 2 && plen >= static_cast<size_t>(stepI) * 3u) { // VDC REAL PRECISION
                    int rf = readSignedInt(payload, intPrec);
                    int re = readSignedInt(payload + stepI, intPrec);
                    int rm = readSignedInt(payload + stepI * 2, intPrec);
                    if (rf == 0 || rf == 1) { vdcRealForm = rf; vdcRealExp = re; vdcRealMant = rm; }
                }
            } else if (cls == 5) {
                if (eid == 3) { // LINE WIDTH
                    size_t vs = vdcScalarSize();
                    if (plen >= vs) lineWidthVdc = std::abs(readVdcScalar(payload));
                } else if (eid == 4) { // LINE COLOUR
                    readColor(payload, plen, false, lineR, lineG, lineB);
                } else if (eid == 22 && plen >= 2) { // INTERIOR STYLE
                    interiorStyle = DetailImg100::RdU16BE(payload);
                } else if (eid == 23) { // FILL COLOUR
                    readColor(payload, plen, false, fillR, fillG, fillB);
                } else if (eid == 27 && plen >= 2) { // EDGE VISIBILITY
                    edgeVisible = (DetailImg100::RdU16BE(payload) != 0);
                } else if (eid == 29) { // EDGE COLOUR
                    readColor(payload, plen, false, edgeR, edgeG, edgeB);
                } else if (eid == 34) { // COLOUR TABLE
                    size_t idxBytes = static_cast<size_t>(std::clamp(colIdxPrec, 8, 32) / 8);
                    size_t compBytes = (colPrec == 16) ? 2u : 1u;
                    if (plen >= idxBytes + compBytes * 3u) {
                        uint32_t startIdx = (idxBytes == 1) ? payload[0] : DetailImg100::RdU16BE(payload);
                        size_t q = idxBytes;
                        while (q + compBytes * 3u <= plen && startIdx < 256u) {
                            colTable[startIdx][0] = payload[q];
                            colTable[startIdx][1] = payload[q + compBytes];
                            colTable[startIdx][2] = payload[q + compBytes * 2u];
                            ++startIdx;
                            q += compBytes * 3u;
                        }
                    }
                }
            } else if (cls == 4) {
                size_t vs = vdcScalarSize();
                size_t ptBytes = vs * 2u;
                if ((eid == 1 || eid == 2) && plen >= ptBytes * 2u) { // POLYLINE / DISJOINT POLYLINE
                    size_t nPts = plen / ptBytes;
                    std::vector<DetailImg200::Pt2f> pts(nPts);
                    for (size_t k = 0; k < nPts; ++k) {
                        pts[k] = { readVdcScalar(payload + k * ptBytes), readVdcScalar(payload + k * ptBytes + vs) };
                    }
                    CgmCmd cmd;
                    cmd.kind = (eid == 2) ? 1 : 0;
                    cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB;
                    cmd.widthVdc = lineWidthVdc;
                    cmd.rings.push_back(std::move(pts));
                    cmds.push_back(std::move(cmd));
                } else if (eid == 7 && plen >= ptBytes * 3u) { // POLYGON (scanline filled)
                    size_t nPts = plen / ptBytes;
                    std::vector<DetailImg200::Pt2f> ring(nPts);
                    for (size_t k = 0; k < nPts; ++k) {
                        ring[k] = { readVdcScalar(payload + k * ptBytes), readVdcScalar(payload + k * ptBytes + vs) };
                    }
                    pushClosedShape(std::move(ring));
                } else if (eid == 8 && plen >= (ptBytes + 2u) * 3u) { // POLYGON SET
                    size_t tupleBytes = ptBytes + 2u;
                    size_t nTuples = plen / tupleBytes;
                    CgmCmd cmd;
                    cmd.kind = 2;
                    cmd.doFill = (interiorStyle != 0 && interiorStyle != 4);
                    cmd.fR = fillR; cmd.fG = fillG; cmd.fB = fillB;
                    cmd.sR = edgeR; cmd.sG = edgeG; cmd.sB = edgeB;
                    cmd.widthVdc = lineWidthVdc;

                    std::vector<DetailImg200::Pt2f> curRing;
                    std::vector<uint16_t> curFlags;
                    auto flushRing = [&]() {
                        if (curRing.size() >= 3) {
                            for (size_t k = 0; k < curRing.size(); ++k) {
                                uint16_t fl = curFlags[k];
                                if (fl == 1 || fl == 3 || (edgeVisible && (fl == 1 || fl == 3))) {
                                    cmd.explicitEdges.push_back({ curRing[k], curRing[(k + 1) % curRing.size()] });
                                }
                            }
                            cmd.rings.push_back(std::move(curRing));
                        }
                        curRing.clear();
                        curFlags.clear();
                    };
                    for (size_t k = 0; k < nTuples; ++k) {
                        const uint8_t* tp = payload + k * tupleBytes;
                        DetailImg200::Pt2f pt = { readVdcScalar(tp), readVdcScalar(tp + vs) };
                        uint16_t flag = DetailImg100::RdU16BE(tp + ptBytes);
                        curRing.push_back(pt);
                        curFlags.push_back(flag);
                        if (flag == 2 || flag == 3) flushRing();
                    }
                    flushRing();
                    if (!cmd.rings.empty()) cmds.push_back(std::move(cmd));
                } else if (eid == 11 && plen >= ptBytes * 2u) { // RECTANGLE
                    float x1 = readVdcScalar(payload + 0 * vs);
                    float y1 = readVdcScalar(payload + 1 * vs);
                    float x2 = readVdcScalar(payload + 2 * vs);
                    float y2 = readVdcScalar(payload + 3 * vs);
                    pushClosedShape({ {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2} });
                } else if (eid == 12 && plen >= vs * 3u) { // CIRCLE
                    float cx = readVdcScalar(payload + 0 * vs);
                    float cy = readVdcScalar(payload + 1 * vs);
                    float rad = std::abs(readVdcScalar(payload + 2 * vs));
                    if (rad > 0.0f) {
                        std::vector<DetailImg200::Pt2f> ring;
                        ring.reserve(48);
                        for (int s = 0; s < 48; ++s) {
                            float a = static_cast<float>(s) * 6.283185307f / 48.0f;
                            ring.push_back({ cx + rad * std::cos(a), cy + rad * std::sin(a) });
                        }
                        pushClosedShape(std::move(ring));
                    }
                } else if ((eid == 13 || eid == 14) && plen >= vs * 6u) { // CIRCULAR ARC 3 POINT / CLOSE
                    float x1 = readVdcScalar(payload + 0 * vs), y1 = readVdcScalar(payload + 1 * vs);
                    float x2 = readVdcScalar(payload + 2 * vs), y2 = readVdcScalar(payload + 3 * vs);
                    float x3 = readVdcScalar(payload + 4 * vs), y3 = readVdcScalar(payload + 5 * vs);
                    int closeType = (eid == 14 && plen >= vs * 6u + 2u) ? DetailImg100::RdU16BE(payload + vs * 6u) : -1;
                    std::vector<DetailImg200::Pt2f> pts;
                    makeArc3Pt(x1, y1, x2, y2, x3, y3, closeType, pts);
                    if (eid == 14) {
                        pushClosedShape(std::move(pts));
                    } else if (pts.size() >= 2) {
                        CgmCmd cmd;
                        cmd.kind = 0;
                        cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB;
                        cmd.widthVdc = lineWidthVdc;
                        cmd.rings.push_back(std::move(pts));
                        cmds.push_back(std::move(cmd));
                    }
                } else if ((eid == 15 || eid == 16) && plen >= vs * 7u) { // CIRCULAR ARC CENTRE / CLOSE
                    float cx  = readVdcScalar(payload + 0 * vs), cy  = readVdcScalar(payload + 1 * vs);
                    float dx1 = readVdcScalar(payload + 2 * vs), dy1 = readVdcScalar(payload + 3 * vs);
                    float dx2 = readVdcScalar(payload + 4 * vs), dy2 = readVdcScalar(payload + 5 * vs);
                    float rad = std::abs(readVdcScalar(payload + 6 * vs));
                    int closeType = (eid == 16 && plen >= vs * 7u + 2u) ? DetailImg100::RdU16BE(payload + vs * 7u) : -1;
                    std::vector<DetailImg200::Pt2f> pts;
                    makeArcCtr(cx, cy, dx1, dy1, dx2, dy2, rad, closeType, pts);
                    if (eid == 16) {
                        pushClosedShape(std::move(pts));
                    } else if (pts.size() >= 2) {
                        CgmCmd cmd;
                        cmd.kind = 0;
                        cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB;
                        cmd.widthVdc = lineWidthVdc;
                        cmd.rings.push_back(std::move(pts));
                        cmds.push_back(std::move(cmd));
                    }
                } else if (eid == 17 && plen >= vs * 6u) { // ELLIPSE (center + 2 conjugate diameter endpoints)
                    float cx = readVdcScalar(payload + 0 * vs), cy = readVdcScalar(payload + 1 * vs);
                    float x1 = readVdcScalar(payload + 2 * vs), y1 = readVdcScalar(payload + 3 * vs);
                    float x2 = readVdcScalar(payload + 4 * vs), y2 = readVdcScalar(payload + 5 * vs);
                    float ux = x1 - cx, uy = y1 - cy;
                    float wx = x2 - cx, wy = y2 - cy;
                    std::vector<DetailImg200::Pt2f> ring;
                    ring.reserve(48);
                    for (int s = 0; s < 48; ++s) {
                        float a = static_cast<float>(s) * 6.283185307f / 48.0f;
                        float cs = std::cos(a), sn = std::sin(a);
                        ring.push_back({ cx + cs * ux + sn * wx, cy + cs * uy + sn * wy });
                    }
                    pushClosedShape(std::move(ring));
                }
            }
        }
    } else {
        // ISO/IEC 8632-4 Clear-Text Encoding parser (';' or '/' statement terminators, '_'/'$' null chars)
        int colorMode = 0; // 0=indexed, 1=direct

        auto parseTextColor = [&](const std::vector<std::string>& toks, size_t startIdx, bool forceDirect, uint8_t& outR, uint8_t& outG, uint8_t& outB) {
            if ((colorMode == 1 || forceDirect || toks.size() >= startIdx + 3) && toks.size() >= startIdx + 3) {
                outR = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[startIdx + 0]), 0, 255));
                outG = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[startIdx + 1]), 0, 255));
                outB = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[startIdx + 2]), 0, 255));
            } else if (toks.size() > startIdx) {
                uint32_t idx = static_cast<uint32_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[startIdx]), 0, 255));
                outR = colTable[idx][0];
                outG = colTable[idx][1];
                outB = colTable[idx][2];
            }
        };

        size_t pos = 0;
        while (pos < sv.size() && cmds.size() < 200000) {
            std::string stmt;
            char inQuote = 0;
            bool inComment = false;
            while (pos < sv.size()) {
                char c = sv[pos++];
                if (inComment) {
                    if (c == '%') inComment = false;
                    continue;
                }
                if (inQuote) {
                    if (c == inQuote) inQuote = 0;
                    continue;
                }
                if (c == '%') { inComment = true; continue; }
                if (c == '\'' || c == '"') { inQuote = c; stmt.push_back(' '); continue; }
                if (c == ';' || c == '/') break;
                stmt.push_back(c);
            }

            std::vector<std::string> toks;
            size_t tp = 0;
            while (tp < stmt.size()) {
                while (tp < stmt.size() && (stmt[tp] == ' ' || stmt[tp] == '\t' || stmt[tp] == '\r' || stmt[tp] == '\n' ||
                                            stmt[tp] == ',' || stmt[tp] == '(' || stmt[tp] == ')')) {
                    ++tp;
                }
                size_t te = tp;
                while (te < stmt.size() && stmt[te] != ' ' && stmt[te] != '\t' && stmt[te] != '\r' && stmt[te] != '\n' &&
                       stmt[te] != ',' && stmt[te] != '(' && stmt[te] != ')') {
                    ++te;
                }
                if (te > tp) {
                    std::string tok;
                    tok.reserve(te - tp);
                    for (size_t k = tp; k < te; ++k) {
                        char ch = stmt[k];
                        if (ch == '_' || ch == '$') continue;
                        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
                        tok.push_back(ch);
                    }
                    if (!tok.empty()) toks.push_back(std::move(tok));
                }
                tp = te;
            }
            if (toks.empty()) continue;

            const std::string& kw = toks[0];
            if (kw == "ENDMF") break;
            if (kw == "VDCEXT" && toks.size() >= 5) {
                vx0 = DetailImg200::ParseAsciiFloatOrZero(toks[1]);
                vy0 = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                vx1 = DetailImg200::ParseAsciiFloatOrZero(toks[3]);
                vy1 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                hasExplicitExtent = true;
            } else if (kw == "COLRMODE" && toks.size() >= 2) {
                colorMode = (toks[1] == "DIRECT" || toks[1] == "1") ? 1 : 0;
            } else if (kw == "COLRTABLE" && toks.size() >= 5) {
                int startIdx = static_cast<int>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[1]), 0, 255));
                for (size_t k = 2; k + 2 < toks.size() && startIdx < 256; k += 3, ++startIdx) {
                    colTable[startIdx][0] = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[k + 0]), 0, 255));
                    colTable[startIdx][1] = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[k + 1]), 0, 255));
                    colTable[startIdx][2] = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[k + 2]), 0, 255));
                }
            } else if (kw == "BACKCOLR" && toks.size() >= 4) {
                parseTextColor(toks, 1, true, bgR, bgG, bgB);
            } else if (kw == "LINECOLR" && toks.size() >= 2) {
                parseTextColor(toks, 1, false, lineR, lineG, lineB);
            } else if (kw == "FILLCOLR" && toks.size() >= 2) {
                parseTextColor(toks, 1, false, fillR, fillG, fillB);
            } else if (kw == "EDGECOLR" && toks.size() >= 2) {
                parseTextColor(toks, 1, false, edgeR, edgeG, edgeB);
            } else if (kw == "EDGEVIS" && toks.size() >= 2) {
                edgeVisible = (toks[1] == "ON" || toks[1] == "1");
            } else if (kw == "INTSTYLE" && toks.size() >= 2) {
                if (toks[1] == "HOLLOW" || toks[1] == "0") interiorStyle = 0;
                else if (toks[1] == "EMPTY" || toks[1] == "4") interiorStyle = 4;
                else interiorStyle = 1;
            } else if (kw == "LINEWIDTH" && toks.size() >= 2) {
                lineWidthVdc = std::abs(DetailImg200::ParseAsciiFloatOrZero(toks[1]));
            } else if ((kw == "LINE" || kw == "POLYLINE" || kw == "DISJTLINE") && toks.size() >= 5) {
                std::vector<DetailImg200::Pt2f> pts;
                for (size_t k = 1; k + 1 < toks.size(); k += 2) {
                    pts.push_back({ DetailImg200::ParseAsciiFloatOrZero(toks[k]), DetailImg200::ParseAsciiFloatOrZero(toks[k + 1]) });
                }
                if (pts.size() >= 2) {
                    CgmCmd cmd;
                    cmd.kind = (kw == "DISJTLINE") ? 1 : 0;
                    cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB;
                    cmd.widthVdc = lineWidthVdc;
                    cmd.rings.push_back(std::move(pts));
                    cmds.push_back(std::move(cmd));
                }
            } else if (kw == "POLYGON" && toks.size() >= 7) {
                std::vector<DetailImg200::Pt2f> ring;
                for (size_t k = 1; k + 1 < toks.size(); k += 2) {
                    ring.push_back({ DetailImg200::ParseAsciiFloatOrZero(toks[k]), DetailImg200::ParseAsciiFloatOrZero(toks[k + 1]) });
                }
                pushClosedShape(std::move(ring));
            } else if (kw == "RECT" && toks.size() >= 5) {
                float x1 = DetailImg200::ParseAsciiFloatOrZero(toks[1]), y1 = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float x2 = DetailImg200::ParseAsciiFloatOrZero(toks[3]), y2 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                pushClosedShape({ {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2} });
            } else if (kw == "CIRCLE" && toks.size() >= 4) {
                float cx = DetailImg200::ParseAsciiFloatOrZero(toks[1]), cy = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float rad = std::abs(DetailImg200::ParseAsciiFloatOrZero(toks[3]));
                if (rad > 0.0f) {
                    std::vector<DetailImg200::Pt2f> ring;
                    ring.reserve(48);
                    for (int s = 0; s < 48; ++s) {
                        float a = static_cast<float>(s) * 6.283185307f / 48.0f;
                        ring.push_back({ cx + rad * std::cos(a), cy + rad * std::sin(a) });
                    }
                    pushClosedShape(std::move(ring));
                }
            } else if ((kw == "ARC3PT" || kw == "ARC3PTCLOSE") && toks.size() >= 7) {
                float x1 = DetailImg200::ParseAsciiFloatOrZero(toks[1]), y1 = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float x2 = DetailImg200::ParseAsciiFloatOrZero(toks[3]), y2 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                float x3 = DetailImg200::ParseAsciiFloatOrZero(toks[5]), y3 = DetailImg200::ParseAsciiFloatOrZero(toks[6]);
                int closeType = (kw == "ARC3PTCLOSE") ? ((toks.size() >= 8 && (toks[7] == "CHORD" || toks[7] == "1")) ? 1 : 0) : -1;
                std::vector<DetailImg200::Pt2f> pts;
                makeArc3Pt(x1, y1, x2, y2, x3, y3, closeType, pts);
                if (kw == "ARC3PTCLOSE") pushClosedShape(std::move(pts));
                else if (pts.size() >= 2) {
                    CgmCmd cmd; cmd.kind = 0; cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB; cmd.widthVdc = lineWidthVdc;
                    cmd.rings.push_back(std::move(pts)); cmds.push_back(std::move(cmd));
                }
            } else if ((kw == "ARCCTR" || kw == "ARCCTRCLOSE") && toks.size() >= 8) {
                float cx  = DetailImg200::ParseAsciiFloatOrZero(toks[1]), cy  = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float dx1 = DetailImg200::ParseAsciiFloatOrZero(toks[3]), dy1 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                float dx2 = DetailImg200::ParseAsciiFloatOrZero(toks[5]), dy2 = DetailImg200::ParseAsciiFloatOrZero(toks[6]);
                float rad = std::abs(DetailImg200::ParseAsciiFloatOrZero(toks[7]));
                int closeType = (kw == "ARCCTRCLOSE") ? ((toks.size() >= 9 && (toks[8] == "CHORD" || toks[8] == "1")) ? 1 : 0) : -1;
                std::vector<DetailImg200::Pt2f> pts;
                makeArcCtr(cx, cy, dx1, dy1, dx2, dy2, rad, closeType, pts);
                if (kw == "ARCCTRCLOSE") pushClosedShape(std::move(pts));
                else if (pts.size() >= 2) {
                    CgmCmd cmd; cmd.kind = 0; cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB; cmd.widthVdc = lineWidthVdc;
                    cmd.rings.push_back(std::move(pts)); cmds.push_back(std::move(cmd));
                }
            } else if (kw == "ELLIPSE" && toks.size() >= 7) {
                float cx = DetailImg200::ParseAsciiFloatOrZero(toks[1]), cy = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float x1 = DetailImg200::ParseAsciiFloatOrZero(toks[3]), y1 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                float x2 = DetailImg200::ParseAsciiFloatOrZero(toks[5]), y2 = DetailImg200::ParseAsciiFloatOrZero(toks[6]);
                float ux = x1 - cx, uy = y1 - cy;
                float wx = x2 - cx, wy = y2 - cy;
                std::vector<DetailImg200::Pt2f> ring;
                ring.reserve(48);
                for (int s = 0; s < 48; ++s) {
                    float a = static_cast<float>(s) * 6.283185307f / 48.0f;
                    float cs = std::cos(a), sn = std::sin(a);
                    ring.push_back({ cx + cs * ux + sn * wx, cy + cs * uy + sn * wy });
                }
                pushClosedShape(std::move(ring));
            }
        }
    }

    if (cmds.empty()) return false;

    if (!hasExplicitExtent || std::abs(vx1 - vx0) < 1e-6f || std::abs(vy1 - vy0) < 1e-6f) {
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (const auto& cmd : cmds) {
            for (const auto& ring : cmd.rings) {
                for (const auto& pt : ring) {
                    minX = std::min(minX, pt.x); maxX = std::max(maxX, pt.x);
                    minY = std::min(minY, pt.y); maxY = std::max(maxY, pt.y);
                }
            }
        }
        if (minX > maxX || minY > maxY) return false;
        float spanX = std::max(1.0f, maxX - minX);
        float spanY = std::max(1.0f, maxY - minY);
        vx0 = minX - spanX * 0.06f; vx1 = maxX + spanX * 0.06f;
        vy0 = minY - spanY * 0.06f; vy1 = maxY + spanY * 0.06f;
    }

    float spanX = vx1 - vx0;
    float spanY = vy1 - vy0;
    float absW = std::max(1.0f, std::abs(spanX));
    float absH = std::max(1.0f, std::abs(spanY));
    uint32_t W = 512, H = 384;
    if (absW >= absH) {
        W = 512;
        H = std::clamp(static_cast<uint32_t>(std::round(512.0f * (absH / absW))), 128u, 512u);
    } else {
        H = 512;
        W = std::clamp(static_cast<uint32_t>(std::round(512.0f * (absW / absH))), 128u, 512u);
    }

    std::vector<uint8_t> canvas(static_cast<size_t>(W) * H * 4u);
    DetailImg200::FillRectBGRA(canvas, W, H, 0, 0, static_cast<int>(W), static_cast<int>(H), bgR, bgG, bgB);

    const float padPx = 16.0f;
    const float drawW = static_cast<float>(W) - padPx * 2.0f;
    const float drawH = static_cast<float>(H) - padPx * 2.0f;
    auto mapXf = [&](float x) -> float { return padPx + ((x - vx0) / spanX) * drawW; };
    auto mapYf = [&](float y) -> float { return static_cast<float>(H) - padPx - ((y - vy0) / spanY) * drawH; };

    int renderedPrims = 0;
    for (const auto& cmd : cmds) {
        int thick = (cmd.widthVdc > 0.0f)
            ? std::clamp(static_cast<int>(std::round((cmd.widthVdc / absW) * drawW)), 1, 10)
            : 2;

        if (cmd.kind == 0) { // POLYLINE
            for (const auto& ring : cmd.rings) {
                for (size_t k = 0; k + 1 < ring.size(); ++k) {
                    DetailImg200::DrawLineBGRA(
                        canvas, W, H,
                        static_cast<int>(std::round(mapXf(ring[k].x))),
                        static_cast<int>(std::round(mapYf(ring[k].y))),
                        static_cast<int>(std::round(mapXf(ring[k + 1].x))),
                        static_cast<int>(std::round(mapYf(ring[k + 1].y))),
                        cmd.sR, cmd.sG, cmd.sB, thick);
                    ++renderedPrims;
                }
            }
        } else if (cmd.kind == 1) { // DISJOINT POLYLINE
            for (const auto& ring : cmd.rings) {
                for (size_t k = 0; k + 1 < ring.size(); k += 2) {
                    DetailImg200::DrawLineBGRA(
                        canvas, W, H,
                        static_cast<int>(std::round(mapXf(ring[k].x))),
                        static_cast<int>(std::round(mapYf(ring[k].y))),
                        static_cast<int>(std::round(mapXf(ring[k + 1].x))),
                        static_cast<int>(std::round(mapYf(ring[k + 1].y))),
                        cmd.sR, cmd.sG, cmd.sB, thick);
                    ++renderedPrims;
                }
            }
        } else if (cmd.kind == 2) { // POLYGON / POLYGON SET / CLOSED SHAPE
            std::vector<std::vector<DetailImg200::Pt2f>> mappedRings;
            mappedRings.reserve(cmd.rings.size());
            for (const auto& ring : cmd.rings) {
                if (ring.size() < 3) continue;
                std::vector<DetailImg200::Pt2f> mr;
                mr.reserve(ring.size());
                for (const auto& pt : ring) mr.push_back({ mapXf(pt.x), mapYf(pt.y) });
                mappedRings.push_back(std::move(mr));
            }
            if (cmd.doFill && DetailImg200::FillPolygonRingsBGRA(canvas, W, H, mappedRings, true, cmd.fR, cmd.fG, cmd.fB)) {
                ++renderedPrims;
            }
            if (cmd.doOutline) {
                for (const auto& mr : mappedRings) {
                    for (size_t k = 0; k < mr.size(); ++k) {
                        const auto& a = mr[k];
                        const auto& b = mr[(k + 1) % mr.size()];
                        DetailImg200::DrawLineBGRA(
                            canvas, W, H,
                            static_cast<int>(std::round(a.x)), static_cast<int>(std::round(a.y)),
                            static_cast<int>(std::round(b.x)), static_cast<int>(std::round(b.y)),
                            cmd.sR, cmd.sG, cmd.sB, thick);
                        ++renderedPrims;
                    }
                }
            }
            for (const auto& ed : cmd.explicitEdges) {
                DetailImg200::DrawLineBGRA(
                    canvas, W, H,
                    static_cast<int>(std::round(mapXf(ed.first.x))),  static_cast<int>(std::round(mapYf(ed.first.y))),
                    static_cast<int>(std::round(mapXf(ed.second.x))), static_cast<int>(std::round(mapYf(ed.second.y))),
                    cmd.sR, cmd.sG, cmd.sB, thick);
                ++renderedPrims;
            }
        }
    }

    if (renderedPrims == 0) return false;

    outInfo.width = W;
    outInfo.height = H;
    outInfo.origWidth = W;
    outInfo.origHeight = H;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ISO 8632 Computer Graphics Metafile (.cgm)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}

} // namespace Formats
