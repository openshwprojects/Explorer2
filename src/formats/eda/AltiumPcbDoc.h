#pragma once
// AltiumPcbDoc.h — Altium Designer 3D PCB & Footprint Library (.pcbdoc / .pcblib) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailAltium {

inline bool ParseAltiumPcbOle2(const Explorer2::Ole2::CompoundFile& cf, ParsedEdaBoard& out) {
    constexpr float kAltiumUnitToMm = 0.00000254f; // 10,000 internal units = 1 mil = 0.0254 mm
    size_t netCount = 0;

    // 1. Board6/Data (board outline vertices VX0/VY0..VXn/VYn)
    if (const auto* brdEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Board6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *brdEnt, bytes, 16 * 1024 * 1024)) {
            std::vector<KvMap> bRecs;
            WalkLengthPrefixedBlocks(bytes, bRecs, false);
            for (const auto& kv : bRecs) {
                std::vector<std::pair<float, float>> verts;
                for (int vi = 0; vi < 256; ++vi) {
                    std::string kx = "vx" + std::to_string(vi);
                    std::string ky = "vy" + std::to_string(vi);
                    auto ix = kv.find(kx), iy = kv.find(ky);
                    if (ix == kv.end() || iy == kv.end()) break;
                    verts.push_back({ ParsePcbCoordStrMm(ix->second), ParsePcbCoordStrMm(iy->second) });
                }
                if (verts.size() >= 3) {
                    for (size_t k = 0; k < verts.size(); ++k) {
                        const auto& p0 = verts[k];
                        const auto& p1 = verts[(k + 1) % verts.size()];
                        out.traces.push_back({ p0.first, p0.second, p1.first, p1.second, 0.35f, EdaLayerId::EdgeCuts, 0 });
                    }
                }
            }
        }
    }

    // 2. Nets6/Data
    if (const auto* netEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Nets6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *netEnt, bytes, 16 * 1024 * 1024)) {
            std::vector<KvMap> nRecs;
            WalkLengthPrefixedBlocks(bytes, nRecs, false);
            for (const auto& kv : nRecs) {
                if (!GetKvStr(kv, "name").empty()) ++netCount;
            }
        }
    }

    // 3. Components6/Data
    if (const auto* cmpEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Components6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *cmpEnt, bytes, 16 * 1024 * 1024)) {
            std::vector<KvMap> cRecs;
            WalkLengthPrefixedBlocks(bytes, cRecs, false);
            for (const auto& kv : cRecs) {
                std::string des = GetKvStr(kv, "sourcedesignator");
                if (des.empty()) des = GetKvStr(kv, "designator");
                if (des.empty()) des = GetKvStr(kv, "name");
                std::string pat = GetKvStr(kv, "pattern");
                if (des.empty() && pat.empty()) continue;

                EdaComponent3D c;
                c.refDes = PathUtils::Utf8ToWide(des.empty() ? pat : des);
                c.value = PathUtils::Utf8ToWide(GetKvStr(kv, "comment"));
                c.footprint = PathUtils::Utf8ToWide(pat.empty() ? "ALTIUM_PKG" : pat);
                c.x = ParsePcbCoordStrMm(GetKvStr(kv, "x"));
                c.y = ParsePcbCoordStrMm(GetKvStr(kv, "y"));
                c.rotDeg = static_cast<float>(std::strtod(GetKvStr(kv, "rotation").c_str(), nullptr));
                c.bottomSide = (MapAltiumPcbLayerStr(GetKvStr(kv, "layer")) == EdaLayerId::BottomCopper);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
                out.components.push_back(c);
            }
        }
    }

    // 4. Tracks6/Data (binary recordType == 4 OR pipe-delimited ASCII fallback)
    if (const auto* trkEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Tracks6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *trkEnt, bytes, 32 * 1024 * 1024) && !bytes.empty()) {
            size_t pos = 0;
            size_t binTracks = 0;
            while (pos + 5 <= bytes.size() && out.traces.size() < 80000) {
                uint8_t recType = bytes[pos];
                uint32_t blkLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 4 || blkLen < 33 || blkLen > bytes.size() - (pos + 5)) break;
                const uint8_t* pay = bytes.data() + pos + 5;
                EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                uint16_t netRaw = DetailSiliconAndBusEda::RdU16LE(pay + 3);
                int netId = (netRaw == 0xFFFFu) ? 0 : static_cast<int>(netRaw + 1);
                float x1 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                float y1 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                float x2 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 21)) * kAltiumUnitToMm;
                float y2 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 25)) * kAltiumUnitToMm;
                float w  = std::max(0.06f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 29)) * kAltiumUnitToMm);
                out.traces.push_back({ x1, y1, x2, y2, w, lid, netId });
                pos += 5 + blkLen;
                ++binTracks;
            }
            if (binTracks == 0) {
                std::vector<KvMap> tRecs;
                WalkLengthPrefixedBlocks(bytes, tRecs, false);
                for (const auto& kv : tRecs) {
                    if (kv.find("x1") != kv.end() && kv.find("x2") != kv.end()) {
                        float x1 = ParsePcbCoordStrMm(GetKvStr(kv, "x1"));
                        float y1 = ParsePcbCoordStrMm(GetKvStr(kv, "y1"));
                        float x2 = ParsePcbCoordStrMm(GetKvStr(kv, "x2"));
                        float y2 = ParsePcbCoordStrMm(GetKvStr(kv, "y2"));
                        float w  = std::max(0.10f, ParsePcbCoordStrMm(GetKvStr(kv, "width"), 0.25f));
                        out.traces.push_back({ x1, y1, x2, y2, w, MapAltiumPcbLayerStr(GetKvStr(kv, "layer")), 1 });
                    }
                }
            }
        }
    }

    // 5. Vias6/Data (recordType == 3)
    if (const auto* viaEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Vias6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *viaEnt, bytes, 16 * 1024 * 1024)) {
            size_t pos = 0;
            while (pos + 5 <= bytes.size() && out.vias.size() < 40000) {
                uint8_t recType = bytes[pos];
                uint32_t blkLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 3 || blkLen < 29 || blkLen > bytes.size() - (pos + 5)) break;
                const uint8_t* pay = bytes.data() + pos + 5;
                uint16_t netRaw = DetailSiliconAndBusEda::RdU16LE(pay + 3);
                float vx = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                float vy = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                float diam = std::max(0.25f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 21)) * kAltiumUnitToMm);
                float hole = std::max(0.12f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 25)) * kAltiumUnitToMm);
                out.vias.push_back({ vx, vy, diam, hole, (netRaw == 0xFFFFu) ? 0 : static_cast<int>(netRaw + 1) });
                pos += 5 + blkLen;
            }
        }
    }

    // 6. Pads6/Data (recordType == 2 + subrecords)
    if (const auto* padEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Pads6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *padEnt, bytes, 32 * 1024 * 1024)) {
            size_t pos = 0;
            while (pos + 5 <= bytes.size() && out.pads.size() < 60000) {
                if (bytes[pos] != 2) break;
                ++pos;
                auto readSub = [&](const uint8_t*& outPtr, uint32_t& outLen) -> bool {
                    if (pos + 4 > bytes.size()) return false;
                    outLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos) & 0x00FFFFFFu;
                    pos += 4;
                    if (outLen > bytes.size() - pos) return false;
                    outPtr = bytes.data() + pos;
                    pos += outLen;
                    return true;
                };
                const uint8_t* s0 = nullptr; uint32_t l0 = 0;
                const uint8_t* s1 = nullptr; uint32_t l1 = 0;
                if (!readSub(s0, l0) || !readSub(s1, l1)) break;

                std::wstring padName;
                if (l0 >= 1) {
                    size_t strOff = (l0 > 1 && s0[0] == l0 - 1) ? 1 : 0;
                    size_t strLen = l0 - strOff;
                    while (strLen > 0 && s0[strOff + strLen - 1] == 0) --strLen;
                    padName = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(s0 + strOff), strLen));
                }

                const uint8_t* geom = nullptr;
                uint32_t geomLen = 0;
                if (l1 >= 33) {
                    // Compact 2-subrecord pad representation (designator + geometry)
                    geom = s1;
                    geomLen = l1;
                } else {
                    // Standard Altium 6-subrecord pad representation (sub0..sub3 strings, sub4 geometry, sub5 stack)
                    const uint8_t *s2 = nullptr, *s3 = nullptr, *s4 = nullptr, *s5 = nullptr;
                    uint32_t l2 = 0, l3 = 0, l4 = 0, l5 = 0;
                    if (!readSub(s2, l2) || !readSub(s3, l3) || !readSub(s4, l4)) break;
                    geom = s4;
                    geomLen = l4;
                    // Optional 6th subrecord if next byte is not start of next pad record
                    if (pos + 4 <= bytes.size() && (pos + 4 == bytes.size() || bytes[pos] != 2 ||
                        (DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu) > 64))
                    {
                        readSub(s5, l5);
                    }
                }

                if (geom && geomLen >= 29) {
                    EdaPadPrim p;
                    p.layer = MapAltiumPcbLayerByte(geom[0]);
                    p.x = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 13)) * kAltiumUnitToMm;
                    p.y = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 17)) * kAltiumUnitToMm;
                    p.w = std::max(0.15f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 21)) * kAltiumUnitToMm);
                    p.h = std::max(0.15f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 25)) * kAltiumUnitToMm);
                    if (geomLen >= 49) {
                        p.drill = std::max(0.0f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 45)) * kAltiumUnitToMm);
                        uint8_t sh = (geomLen >= 50) ? geom[49] : 2;
                        p.shape = (sh == 1) ? 0 : ((sh == 2) ? 1 : 2);
                    } else if (geomLen >= 33) {
                        p.drill = std::max(0.0f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 29)) * kAltiumUnitToMm);
                        p.shape = (p.drill > 0.05f) ? 0 : 1;
                    }
                    if (p.drill > 0.05f) p.layer = EdaLayerId::PadsThrough;
                    if (geomLen >= 60) {
                        double rotD = 0.0;
                        std::memcpy(&rotD, geom + 52, 8);
                        if (std::isfinite(rotD) && std::fabs(rotD) <= 360.0) p.rotDeg = static_cast<float>(rotD);
                    }
                    p.pinName = padName;
                    out.pads.push_back(p);
                }
            }
        }
    }

    // 7. Arcs6/Data (recordType == 1)
    if (const auto* arcEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Arcs6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *arcEnt, bytes, 16 * 1024 * 1024)) {
            size_t pos = 0;
            while (pos + 5 <= bytes.size() && out.traces.size() < 80000) {
                uint8_t recType = bytes[pos];
                uint32_t blkLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 1 || blkLen < 45 || blkLen > bytes.size() - (pos + 5)) break;
                const uint8_t* pay = bytes.data() + pos + 5;
                EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                float cx = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                float cy = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                float r  = std::max(0.1f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 21)) * kAltiumUnitToMm);
                double a0Deg = 0.0, a1Deg = 360.0;
                std::memcpy(&a0Deg, pay + 25, 8);
                std::memcpy(&a1Deg, pay + 33, 8);
                float w = std::max(0.08f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 41)) * kAltiumUnitToMm);
                if (!std::isfinite(a0Deg) || !std::isfinite(a1Deg) || std::fabs(a1Deg - a0Deg) >= 359.5) {
                    out.circles.push_back({ cx, cy, r, w, false, lid });
                } else {
                    constexpr double kDeg2Rad = 3.141592653589793 / 180.0;
                    float xs = cx + r * static_cast<float>(std::cos(a0Deg * kDeg2Rad));
                    float ys = cy + r * static_cast<float>(std::sin(a0Deg * kDeg2Rad));
                    float xe = cx + r * static_cast<float>(std::cos(a1Deg * kDeg2Rad));
                    float ye = cy + r * static_cast<float>(std::sin(a1Deg * kDeg2Rad));
                    DetailSiliconAndBusEda::AddTessellatedArc(out, xs, ys, xe, ye, cx, cy, false, w, lid, 0);
                }
                pos += 5 + blkLen;
            }
        }
    }

    // 8. Fills6/Data (recordType == 6: rectangular copper fills [6][blkLen:u32][layer:1]..[net:u16 +3]..[x1:i32 +13][y1:i32 +17][x2:i32 +21][y2:i32 +25][rot:f64 +29])
    if (const auto* fillEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Fills6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *fillEnt, bytes, 16 * 1024 * 1024)) {
            size_t pos = 0;
            while (pos + 5 <= bytes.size() && out.pads.size() < 60000) {
                uint8_t recType = bytes[pos];
                uint32_t blkLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 6 || blkLen < 29 || blkLen > bytes.size() - (pos + 5)) break;
                const uint8_t* pay = bytes.data() + pos + 5;
                EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                float x1 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                float y1 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                float x2 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 21)) * kAltiumUnitToMm;
                float y2 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 25)) * kAltiumUnitToMm;
                float rotDeg = 0.0f;
                if (blkLen >= 37) {
                    double rd = 0.0;
                    std::memcpy(&rd, pay + 29, 8);
                    if (std::isfinite(rd) && std::fabs(rd) <= 360.0) rotDeg = static_cast<float>(rd);
                }
                EdaPadPrim fp{};
                fp.x = (x1 + x2) * 0.5f;
                fp.y = (y1 + y2) * 0.5f;
                fp.w = std::max(0.15f, std::fabs(x2 - x1));
                fp.h = std::max(0.15f, std::fabs(y2 - y1));
                fp.rotDeg = rotDeg;
                fp.shape = 1;
                fp.layer = lid;
                out.pads.push_back(std::move(fp));
                pos += 5 + blkLen;
            }
        }
    }

    // Helper to hatch a copper polygon / region into out.traces
    auto emitAltiumCopperPolygon = [&](const std::vector<std::pair<float, float>>& pts, EdaLayerId lid, int netId) {
        if (pts.size() < 3) return;
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (size_t i = 0; i < pts.size() && out.traces.size() < 120000; ++i) {
            const auto& p0 = pts[i];
            const auto& p1 = pts[(i + 1) % pts.size()];
            minX = std::min(minX, p0.first); maxX = std::max(maxX, p0.first);
            minY = std::min(minY, p0.second); maxY = std::max(maxY, p0.second);
            out.traces.push_back({ p0.first, p0.second, p1.first, p1.second, 0.16f, lid, netId });
        }
        if (maxY > minY + 0.3f && maxX > minX + 0.3f) {
            float spanY = maxY - minY;
            int nScan = std::clamp(static_cast<int>(spanY / 0.55f), 2, 36);
            float stepY = spanY / static_cast<float>(nScan + 1);
            float strokeW = std::clamp(stepY * 0.75f, 0.14f, 0.60f);
            std::vector<float> xHits;
            for (int si = 1; si <= nScan; ++si) {
                float sy = minY + stepY * static_cast<float>(si);
                xHits.clear();
                for (size_t i = 0; i < pts.size(); ++i) {
                    const auto& a = pts[i];
                    const auto& b = pts[(i + 1) % pts.size()];
                    if ((a.second <= sy && b.second > sy) || (b.second <= sy && a.second > sy)) {
                        float t = (sy - a.second) / (b.second - a.second);
                        xHits.push_back(a.first + t * (b.first - a.first));
                    }
                }
                std::sort(xHits.begin(), xHits.end());
                for (size_t k = 0; k + 1 < xHits.size() && out.traces.size() < 120000; k += 2) {
                    if (xHits[k + 1] > xHits[k] + 0.05f) {
                        out.traces.push_back({ xHits[k], sy, xHits[k + 1], sy, strokeW, lid, netId });
                    }
                }
            }
        }
    };

    // 9. Polygons6/Data (copper pour polygons stored as pipe-delimited VX0/VY0..VXn/VYn records)
    if (const auto* polyEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Polygons6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *polyEnt, bytes, 32 * 1024 * 1024)) {
            std::vector<KvMap> pRecs;
            WalkLengthPrefixedBlocks(bytes, pRecs, false);
            for (const auto& kv : pRecs) {
                EdaLayerId lid = MapAltiumPcbLayerStr(GetKvStr(kv, "layer"));
                int netId = std::max(0, GetKvInt(kv, "net", -1) + 1);
                std::vector<std::pair<float, float>> verts;
                for (int vi = 0; vi < 1024; ++vi) {
                    auto ix = kv.find("vx" + std::to_string(vi));
                    auto iy = kv.find("vy" + std::to_string(vi));
                    if (ix == kv.end() || iy == kv.end()) break;
                    verts.push_back({ ParsePcbCoordStrMm(ix->second), ParsePcbCoordStrMm(iy->second) });
                }
                emitAltiumCopperPolygon(verts, lid, netId);
            }
        }
    }

    // 10. Regions6/Data (recordType == 11: [11][blkLen:u32][hdr:18+][propLen:u32][props][vCount:u32]([vx_f64:8][vy_f64:8])*)
    if (const auto* regEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Regions6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *regEnt, bytes, 32 * 1024 * 1024)) {
            size_t pos = 0;
            while (pos + 5 <= bytes.size()) {
                uint8_t recType = bytes[pos];
                uint32_t blkLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 11 || blkLen < 26 || blkLen > bytes.size() - (pos + 5)) break;
                const uint8_t* pay = bytes.data() + pos + 5;
                EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                uint16_t netRaw = DetailSiliconAndBusEda::RdU16LE(pay + 3);
                int netId = (netRaw == 0xFFFFu) ? 0 : static_cast<int>(netRaw + 1);
                // After 18-byte common header: uint32 propLen + propStr + uint32 vertCount + vertCount * 16B (f64 x, f64 y)
                uint32_t propLen = DetailSiliconAndBusEda::RdU32LE(pay + 18);
                size_t vOff = 22 + static_cast<size_t>(propLen);
                if (vOff + 4 <= blkLen) {
                    uint32_t vCnt = DetailSiliconAndBusEda::RdU32LE(pay + vOff);
                    vOff += 4;
                    if (vCnt >= 3 && vCnt <= 4096 && vOff + static_cast<size_t>(vCnt) * 16u <= blkLen) {
                        std::vector<std::pair<float, float>> verts;
                        verts.reserve(vCnt);
                        for (uint32_t vi = 0; vi < vCnt; ++vi) {
                            double dx = 0.0, dy = 0.0;
                            std::memcpy(&dx, pay + vOff + vi * 16u, 8);
                            std::memcpy(&dy, pay + vOff + vi * 16u + 8u, 8);
                            verts.push_back({ static_cast<float>(dx * kAltiumUnitToMm), static_cast<float>(dy * kAltiumUnitToMm) });
                        }
                        emitAltiumCopperPolygon(verts, lid, netId);
                    }
                }
                pos += 5 + blkLen;
            }
        }
    }

    // 11. WideStrings6/Data + Texts6/Data (recordType == 5 silkscreen labels)
    std::unordered_map<int, std::wstring> wideStrings;
    if (const auto* wsEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"WideStrings6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *wsEnt, bytes, 16 * 1024 * 1024) && !bytes.empty()) {
            std::vector<KvMap> wsRecs;
            WalkLengthPrefixedBlocks(bytes, wsRecs, false);
            for (const auto& kv : wsRecs) {
                for (const auto& pair : kv) {
                    if (pair.first.rfind("encodedtext", 0) == 0 && pair.first.size() > 11) {
                        int idx = std::atoi(pair.first.c_str() + 11);
                        std::wstring ws;
                        size_t cp = 0;
                        const std::string& val = pair.second;
                        while (cp < val.size()) {
                            size_t cm = val.find(',', cp);
                            if (cm == std::string::npos) cm = val.size();
                            int ch = std::atoi(val.substr(cp, cm - cp).c_str());
                            if (ch > 0 && ch < 0x10000) ws.push_back(static_cast<wchar_t>(ch));
                            cp = cm + 1;
                        }
                        if (!ws.empty()) wideStrings[idx] = std::move(ws);
                    }
                }
            }
            if (wideStrings.empty()) {
                // Binary PcbLib/AD18+ WideStrings6 format: ([idx:u32][byteLen:u32][utf16le...])*
                size_t p = 0;
                while (p + 8 <= bytes.size() && wideStrings.size() < 16000) {
                    uint32_t idx = DetailSiliconAndBusEda::RdU32LE(bytes.data() + p);
                    uint32_t bLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + p + 4);
                    p += 8;
                    if (bLen > bytes.size() - p || (bLen & 1u) != 0) break;
                    std::wstring ws;
                    for (uint32_t i = 0; i + 2 <= bLen; i += 2) {
                        uint16_t cu = DetailSiliconAndBusEda::RdU16LE(bytes.data() + p + i);
                        if (cu != 0) ws.push_back(static_cast<wchar_t>(cu));
                    }
                    if (!ws.empty()) wideStrings[static_cast<int>(idx)] = std::move(ws);
                    p += bLen;
                }
            }
        }
    }
    if (const auto* txtEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Texts6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *txtEnt, bytes, 16 * 1024 * 1024)) {
            size_t pos = 0;
            int txtOrd = 0;
            while (pos + 5 <= bytes.size() && out.labels.size() < 8000) {
                uint8_t recType = bytes[pos];
                uint32_t geomLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 5 || geomLen < 25 || geomLen > bytes.size() - (pos + 5)) break;
                const uint8_t* geom = bytes.data() + pos + 5;
                pos += 5 + geomLen;
                if (pos + 4 > bytes.size()) break;
                uint32_t strBlkLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos) & 0x00FFFFFFu;
                pos += 4;
                if (strBlkLen > bytes.size() - pos) break;
                const uint8_t* strPtr = bytes.data() + pos;
                pos += strBlkLen;

                EdaLayerId lid = MapAltiumPcbLayerByte(geom[0]);
                float tx = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 13)) * kAltiumUnitToMm;
                float ty = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 17)) * kAltiumUnitToMm;
                float th = std::clamp(static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 21)) * kAltiumUnitToMm, 0.7f, 4.0f);

                std::wstring labelW;
                auto itWs = wideStrings.find(txtOrd);
                if (itWs != wideStrings.end()) {
                    labelW = itWs->second;
                } else if (strBlkLen >= 2) {
                    size_t sLen = std::min<size_t>(strPtr[0], strBlkLen - 1);
                    labelW = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(strPtr + 1), sLen));
                }
                if (!labelW.empty()) {
                    out.labels.push_back({ tx, ty, th, lid, std::move(labelW) });
                }
                ++txtOrd;
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty() && out.circles.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, netCount > 0 ? netCount : out.components.size());
    return true;
}

inline bool ParseAltiumPcbLibOle2(const Explorer2::Ole2::CompoundFile& cf, ParsedEdaBoard& out) {
    // Altium .PcbLib stores each footprint inside <FootprintDir>/Data:
    // Header block: [blkLen:u32][pascalLen:1][patternName] followed by packed binary primitive records (1=Arc, 2=Pad, 3=Via, 4=Track, 6=Fill)
    constexpr float kAltiumUnitToMm = 0.00000254f;
    size_t fpCount = 0;
    float offX = 0.0f;

    for (const auto& e : cf.entries) {
        if (e.objectType != 2 || e.size < 10) continue;
        std::wstring lowPath = PathUtils::ToLower(e.fullPath);
        if (lowPath.find(L"library/") == 0 || lowPath == L"fileversioninfo/data" || lowPath.find(L"/data") == std::wstring::npos) {
            continue;
        }
        if (lowPath.find(L"/primitives") != std::wstring::npos || lowPath.find(L"/uniqueparameters") != std::wstring::npos ||
            lowPath.find(L"/widestrings") != std::wstring::npos) {
            continue;
        }

        std::vector<uint8_t> bytes;
        if (!Explorer2::Ole2::ExtractStream(cf, e, bytes, 16 * 1024 * 1024) || bytes.size() < 10) continue;

        uint32_t nameBlkLen = DetailSiliconAndBusEda::RdU32LE(bytes.data()) & 0x00FFFFFFu;
        if (nameBlkLen == 0 || 4 + nameBlkLen > bytes.size()) continue;
        size_t patLen = std::min<size_t>(bytes[4], nameBlkLen - 1);
        std::wstring patName = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(bytes.data() + 5), patLen));
        if (patName.empty()) {
            size_t sl = e.fullPath.find(L'/');
            patName = (sl != std::wstring::npos) ? e.fullPath.substr(0, sl) : e.name;
        }

        size_t pos = 4 + nameBlkLen;
        size_t primAdded = 0;
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        auto expFp = [&](float x, float y) {
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        };

        while (pos < bytes.size()) {
            uint8_t recType = bytes[pos++];
            if (recType == 0) break;
            if (recType == 2) { // Pad (6 subrecords or 2 compact subrecords)
                auto readSub = [&](const uint8_t*& outPtr, uint32_t& outLen) -> bool {
                    if (pos + 4 > bytes.size()) return false;
                    outLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos) & 0x00FFFFFFu;
                    pos += 4;
                    if (outLen > bytes.size() - pos) return false;
                    outPtr = bytes.data() + pos;
                    pos += outLen;
                    return true;
                };
                const uint8_t *s0 = nullptr, *s1 = nullptr;
                uint32_t l0 = 0, l1 = 0;
                if (!readSub(s0, l0) || !readSub(s1, l1)) break;
                std::wstring padDes;
                if (l0 >= 1) {
                    size_t so = (l0 > 1 && s0[0] == l0 - 1) ? 1 : 0;
                    padDes = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(s0 + so), l0 - so));
                }
                const uint8_t* geom = s1;
                uint32_t geomLen = l1;
                if (l1 < 29) {
                    const uint8_t *s2 = nullptr, *s3 = nullptr, *s4 = nullptr, *s5 = nullptr;
                    uint32_t l2 = 0, l3 = 0, l4 = 0, l5 = 0;
                    if (!readSub(s2, l2) || !readSub(s3, l3) || !readSub(s4, l4)) break;
                    geom = s4; geomLen = l4;
                    if (pos + 4 <= bytes.size() && (bytes[pos] != 2 && bytes[pos] != 4 && bytes[pos] != 1 && bytes[pos] != 6)) {
                        readSub(s5, l5);
                    }
                }
                if (geom && geomLen >= 29) {
                    EdaPadPrim p{};
                    p.layer = MapAltiumPcbLayerByte(geom[0]);
                    p.x = offX + static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 13)) * kAltiumUnitToMm;
                    p.y = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 17)) * kAltiumUnitToMm;
                    p.w = std::max(0.15f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 21)) * kAltiumUnitToMm);
                    p.h = std::max(0.15f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 25)) * kAltiumUnitToMm);
                    if (geomLen >= 49) p.drill = std::max(0.0f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 45)) * kAltiumUnitToMm);
                    else if (geomLen >= 33) p.drill = std::max(0.0f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(geom + 29)) * kAltiumUnitToMm);
                    p.shape = (p.drill > 0.05f) ? 0 : 1;
                    if (p.drill > 0.05f) p.layer = EdaLayerId::PadsThrough;
                    p.pinName = padDes;
                    expFp(p.x - p.w * 0.5f, p.y - p.h * 0.5f);
                    expFp(p.x + p.w * 0.5f, p.y + p.h * 0.5f);
                    out.pads.push_back(std::move(p));
                    ++primAdded;
                }
            } else if (pos + 4 <= bytes.size()) {
                uint32_t blkLen = DetailSiliconAndBusEda::RdU32LE(bytes.data() + pos) & 0x00FFFFFFu;
                pos += 4;
                if (blkLen > bytes.size() - pos) break;
                const uint8_t* pay = bytes.data() + pos;
                pos += blkLen;
                if (recType == 4 && blkLen >= 33) { // Track
                    EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                    float x1 = offX + static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                    float y1 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                    float x2 = offX + static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 21)) * kAltiumUnitToMm;
                    float y2 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 25)) * kAltiumUnitToMm;
                    float w  = std::max(0.06f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 29)) * kAltiumUnitToMm);
                    expFp(x1, y1); expFp(x2, y2);
                    out.traces.push_back({ x1, y1, x2, y2, w, lid, 0 });
                    ++primAdded;
                } else if (recType == 1 && blkLen >= 45) { // Arc
                    EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                    float cx = offX + static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                    float cy = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                    float r  = std::max(0.1f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 21)) * kAltiumUnitToMm);
                    float w  = std::max(0.08f, static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 41)) * kAltiumUnitToMm);
                    expFp(cx - r, cy - r); expFp(cx + r, cy + r);
                    out.circles.push_back({ cx, cy, r, w, false, lid });
                    ++primAdded;
                } else if (recType == 6 && blkLen >= 29) { // Fill
                    EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                    float x1 = offX + static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                    float y1 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                    float x2 = offX + static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 21)) * kAltiumUnitToMm;
                    float y2 = static_cast<float>(DetailSiliconAndBusEda::RdI32LE(pay + 25)) * kAltiumUnitToMm;
                    EdaPadPrim fp{};
                    fp.x = (x1 + x2) * 0.5f; fp.y = (y1 + y2) * 0.5f;
                    fp.w = std::max(0.15f, std::fabs(x2 - x1)); fp.h = std::max(0.15f, std::fabs(y2 - y1));
                    fp.shape = 1; fp.layer = lid;
                    expFp(x1, y1); expFp(x2, y2);
                    out.pads.push_back(std::move(fp));
                    ++primAdded;
                }
            } else {
                break;
            }
        }

        if (primAdded > 0) {
            float fw = (minX <= maxX) ? std::max(1.5f, maxX - minX) : 4.0f;
            float fh = (minY <= maxY) ? std::max(1.2f, maxY - minY) : 3.0f;
            float fcx = (minX <= maxX) ? ((minX + maxX) * 0.5f) : offX;
            float fcy = (minY <= maxY) ? ((minY + maxY) * 0.5f) : 0.0f;

            EdaComponent3D comp;
            comp.refDes = patName;
            comp.footprint = patName;
            comp.x = fcx; comp.y = fcy;
            comp.widthMm = fw; comp.lengthMm = fh; comp.heightMm = 1.3f;
            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
            out.components.push_back(std::move(comp));
            out.labels.push_back({ fcx - fw * 0.5f, fcy + fh * 0.5f + 1.0f, 1.0f, EdaLayerId::TopSilk, patName });

            offX += fw + 8.0f;
            ++fpCount;
        }
    }

    if (fpCount == 0) return false;
    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"Altium PCB Footprint Library (.PcbLib — " + std::to_wstring(fpCount) + L" footprints)";
    out.netCount = fpCount;
    return true;
}

} // namespace DetailAltium

inline bool ParseAltiumPcbDocBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 8 || (extLower != L".pcbdoc" && extLower != L".pcblib")) {
        return false;
    }
    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"Altium Designer 3D PCB (.PcbDoc)";

    // Case 1: Native Microsoft OLE2 Compound File Binary (D0 CF 11 E0 A1 B1 1A E1)
    static const uint8_t kOle2Magic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    if (size >= 512 && std::memcmp(data, kOle2Magic, 8) == 0) {
        Explorer2::Ole2::CompoundFile cf;
        if (!Explorer2::Ole2::ParseCompoundFile(data, size, cf)) return false;
        if (extLower == L".pcblib") {
            return DetailAltium::ParseAltiumPcbLibOle2(cf, out);
        }
        return DetailAltium::ParseAltiumPcbOle2(cf, out);
    }

    // Case 2: Pipe-delimited ASCII Altium export (|RECORD=...|...)
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (!DetailSiliconAndBusEda::ContainsAsciiI(sv, "RECORD=")) return false;

    std::vector<DetailAltium::KvMap> asciiRecords;
    size_t pos = 0;
    while (pos < sv.size() && asciiRecords.size() < 20000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        if (!DetailSiliconAndBusEda::ContainsAsciiI(ln, "RECORD=")) continue;
        DetailAltium::KvMap kv = DetailAltium::ParsePipeRecord(ln);
        if (!kv.empty()) asciiRecords.push_back(std::move(kv));
    }
    if (asciiRecords.empty()) return false;

    // ASCII .PcbDoc records: Track, Pad, Via, Arc, Component
    for (const auto& kv : asciiRecords) {
        std::string rec = PathUtils::ToLowerAscii(DetailAltium::GetKvStr(kv, "record"));
        if (rec == "track") {
            float x1 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x1"));
            float y1 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y1"));
            float x2 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x2"));
            float y2 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y2"));
            float w  = std::max(0.08f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "width"), 0.25f));
            out.traces.push_back({ x1, y1, x2, y2, w, DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer")), 1 });
        } else if (rec == "via") {
            float vx = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
            float vy = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
            float d  = std::max(0.25f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "diameter"), 0.8f));
            float h  = std::max(0.12f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "holesize"), 0.4f));
            out.vias.push_back({ vx, vy, d, h, 1 });
        } else if (rec == "pad") {
            EdaPadPrim p;
            p.x = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
            p.y = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
            p.w = std::max(0.2f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "xsize"), 1.2f));
            p.h = std::max(0.2f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "ysize"), 1.2f));
            p.drill = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "holesize"), 0.0f);
            p.layer = (p.drill > 0.05f) ? EdaLayerId::PadsThrough : DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer"));
            p.shape = 1;
            p.pinName = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "name"));
            out.pads.push_back(p);
        } else if (rec == "component") {
            std::string des = DetailAltium::GetKvStr(kv, "sourcedesignator");
            if (des.empty()) des = DetailAltium::GetKvStr(kv, "designator");
            if (des.empty()) des = DetailAltium::GetKvStr(kv, "name");
            if (!des.empty()) {
                EdaComponent3D c;
                c.refDes = PathUtils::Utf8ToWide(des);
                c.value = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "comment"));
                c.footprint = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "pattern"));
                c.x = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
                c.y = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
                c.rotDeg = static_cast<float>(std::strtod(DetailAltium::GetKvStr(kv, "rotation").c_str(), nullptr));
                c.bottomSide = (DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer")) == EdaLayerId::BottomCopper);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
                out.components.push_back(c);
            }
        }
    }


    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, out.components.size());
    return true;
}

} // namespace Formats
