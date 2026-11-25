#pragma once
// SemiOasis.h — SEMI P39 OASIS Wafer Mask Layout (.oas) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseSemiOasisBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    // SEMI P39 OASIS Wafer Mask Layout (.oas — "%SEMI-OASIS\r\n")
    if (ext == L".oas" || (size >= 13 && std::memcmp(data, "%SEMI-OASIS\r\n", 13) == 0)) {
        if (size < 13 || std::memcmp(data, "%SEMI-OASIS\r\n", 13) != 0) return false;
        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = L"SEMI P39 OASIS Silicon IC Mask (.oas)";

        struct OasisPt { int64_t x = 0, y = 0; };
        struct OasisShape {
            uint8_t kind = 0; // 1=rect, 2=poly, 3=path, 4=circle, 5=text, 6=place
            uint64_t layer = 0;
            int64_t x = 0, y = 0;
            uint64_t w = 0, h = 0;
            std::vector<OasisPt> pts; // relative to (x, y)
            std::string str;
            double mag = 1.0, deg = 0.0;
            bool flip = false;
            std::vector<OasisPt> repOffsets; // includes (0,0)
        };
        struct OasisCell {
            std::string name;
            std::vector<OasisShape> shapes;
        };
        struct OasisModal {
            bool xyAbs = true;
            uint64_t layer = 0, datatype = 0, textLayer = 0, textType = 0;
            int64_t geomX = 0, geomY = 0, textX = 0, textY = 0, placeX = 0, placeY = 0;
            uint64_t rectW = 0, rectH = 0, circleR = 0, pathHalfW = 0;
            int64_t pathStartExt = 0, pathEndExt = 0;
            uint64_t ctrapType = 0, trapW = 0, trapH = 0;
            std::vector<OasisPt> polyPts, pathPts;
            std::vector<OasisPt> modalRep = { { 0, 0 } };
            std::string textStr, placeCell;
        };

        struct OasisParser {
            bool ok = true;
            bool sawStart = false;
            bool stopStream = false;
            double unit = 1000.0;
            uint64_t nextCellRef = 0, nextTextRef = 0;
            std::vector<std::pair<uint64_t, std::string>> cellNames;
            std::vector<std::pair<uint64_t, std::string>> textStrings;
            std::vector<OasisCell> cells;
            OasisModal mod;

            uint8_t RdByte(const uint8_t* b, size_t n, size_t& p) {
                if (p >= n) { ok = false; return 0; }
                return b[p++];
            }
            uint64_t RdU(const uint8_t* b, size_t n, size_t& p) {
                uint64_t v = 0;
                int sh = 0;
                while (p < n && sh < 64) {
                    uint8_t c = b[p++];
                    v |= static_cast<uint64_t>(c & 0x7Fu) << sh;
                    if ((c & 0x80u) == 0) return v;
                    sh += 7;
                }
                ok = false;
                return 0;
            }
            int64_t RdS(const uint8_t* b, size_t n, size_t& p) {
                uint64_t u = RdU(b, n, p);
                int64_t m = static_cast<int64_t>(u >> 1);
                return (u & 1ULL) ? -m : m;
            }
            std::string RdStr(const uint8_t* b, size_t n, size_t& p) {
                uint64_t len = RdU(b, n, p);
                if (!ok || len > n - p || len > 1048576ULL) { ok = false; return {}; }
                std::string s(reinterpret_cast<const char*>(b + p), static_cast<size_t>(len));
                p += static_cast<size_t>(len);
                return s;
            }
            double RdReal(const uint8_t* b, size_t n, size_t& p) {
                uint64_t tp = RdU(b, n, p);
                if (!ok) return 1.0;
                switch (tp) {
                case 0: return static_cast<double>(RdU(b, n, p));
                case 1: return -static_cast<double>(RdU(b, n, p));
                case 2: { uint64_t d = RdU(b, n, p); return (d != 0) ? (1.0 / static_cast<double>(d)) : 1.0; }
                case 3: { uint64_t d = RdU(b, n, p); return (d != 0) ? (-1.0 / static_cast<double>(d)) : -1.0; }
                case 4: {
                    uint64_t num = RdU(b, n, p), den = RdU(b, n, p);
                    return (den != 0) ? (static_cast<double>(num) / static_cast<double>(den)) : 1.0;
                }
                case 5: {
                    uint64_t num = RdU(b, n, p), den = RdU(b, n, p);
                    return (den != 0) ? (-static_cast<double>(num) / static_cast<double>(den)) : -1.0;
                }
                case 6: {
                    if (p + 4 > n) { ok = false; return 1.0; }
                    uint32_t bits = DetailIndustrialAndWaveEda::RdU32LE(b + p); p += 4;
                    float fv = 1.0f; std::memcpy(&fv, &bits, 4);
                    return static_cast<double>(fv);
                }
                case 7: {
                    if (p + 8 > n) { ok = false; return 1.0; }
                    uint64_t bits = DetailIndustrialAndWaveEda::RdU64LE(b + p); p += 8;
                    double dv = 1.0; std::memcpy(&dv, &bits, 8);
                    return dv;
                }
                default: ok = false; return 1.0;
                }
            }
            void SkipInterval(const uint8_t* b, size_t n, size_t& p) {
                uint64_t it = RdU(b, n, p);
                if (it == 0) return;
                if (it >= 1 && it <= 3) { RdU(b, n, p); return; }
                if (it == 4) { RdU(b, n, p); RdU(b, n, p); return; }
                ok = false;
            }
            OasisPt DirMag(uint64_t dir, int64_t mag) {
                switch (dir & 7ULL) {
                case 0: return {  mag,    0 }; // E
                case 1: return {    0,  mag }; // N
                case 2: return { -mag,    0 }; // W
                case 3: return {    0, -mag }; // S
                case 4: return {  mag,  mag }; // NE
                case 5: return { -mag,  mag }; // NW
                case 6: return { -mag, -mag }; // SW
                default:return {  mag, -mag }; // SE
                }
            }
            OasisPt RdGDelta(const uint8_t* b, size_t n, size_t& p) {
                uint64_t u = RdU(b, n, p);
                if ((u & 1ULL) == 0) {
                    return DirMag((u >> 1) & 7ULL, static_cast<int64_t>(u >> 4));
                }
                int64_t xm = static_cast<int64_t>(u >> 2);
                int64_t dx = (u & 2ULL) ? -xm : xm;
                int64_t dy = RdS(b, n, p);
                return { dx, dy };
            }
            void RdPointList(const uint8_t* b, size_t n, size_t& p, bool isPoly, std::vector<OasisPt>& outPts) {
                uint64_t ptype = RdU(b, n, p);
                uint64_t vcnt = RdU(b, n, p);
                if (!ok || ptype > 5 || vcnt == 0 || vcnt > 65536ULL) { ok = false; return; }
                outPts.clear();
                outPts.reserve(static_cast<size_t>(vcnt) + 2);
                outPts.push_back({ 0, 0 });
                int64_t cx = 0, cy = 0;
                int64_t prevDx = 0, prevDy = 0;
                for (uint64_t i = 0; i < vcnt && ok; ++i) {
                    int64_t dx = 0, dy = 0;
                    if (ptype == 0) {
                        int64_t d = RdS(b, n, p);
                        if ((i & 1ULL) == 0) dx = d; else dy = d;
                    } else if (ptype == 1) {
                        int64_t d = RdS(b, n, p);
                        if ((i & 1ULL) == 0) dy = d; else dx = d;
                    } else if (ptype == 2) {
                        uint64_t u = RdU(b, n, p);
                        OasisPt dm = DirMag(u & 3ULL, static_cast<int64_t>(u >> 2));
                        dx = dm.x; dy = dm.y;
                    } else if (ptype == 3) {
                        uint64_t u = RdU(b, n, p);
                        OasisPt dm = DirMag(u & 7ULL, static_cast<int64_t>(u >> 3));
                        dx = dm.x; dy = dm.y;
                    } else if (ptype == 4) {
                        OasisPt dm = RdGDelta(b, n, p);
                        dx = dm.x; dy = dm.y;
                    } else {
                        OasisPt dm = RdGDelta(b, n, p);
                        prevDx += dm.x; prevDy += dm.y;
                        dx = prevDx; dy = prevDy;
                    }
                    cx += dx; cy += dy;
                    outPts.push_back({ cx, cy });
                }
                if (isPoly && (ptype == 0 || ptype == 1) && cx != 0 && cy != 0) {
                    bool nextIsHoriz = (ptype == 0) ? ((vcnt & 1ULL) == 0) : ((vcnt & 1ULL) != 0);
                    if (nextIsHoriz) outPts.push_back({ 0, cy });
                    else outPts.push_back({ cx, 0 });
                }
            }
            void RdRepetition(const uint8_t* b, size_t n, size_t& p, std::vector<OasisPt>& rep) {
                uint64_t rtype = RdU(b, n, p);
                if (!ok) return;
                if (rtype == 0) return; // reuse modalRep
                rep.clear();
                constexpr size_t kMaxRep = 512;
                if (rtype == 1) {
                    uint64_t xd = RdU(b, n, p) + 2ULL;
                    uint64_t yd = RdU(b, n, p) + 2ULL;
                    int64_t xs = static_cast<int64_t>(RdU(b, n, p));
                    int64_t ys = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t iy = 0; iy < yd && rep.size() < kMaxRep; ++iy)
                        for (uint64_t ix = 0; ix < xd && rep.size() < kMaxRep; ++ix)
                            rep.push_back({ static_cast<int64_t>(ix) * xs, static_cast<int64_t>(iy) * ys });
                } else if (rtype == 2) {
                    uint64_t xd = RdU(b, n, p) + 2ULL;
                    int64_t xs = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t ix = 0; ix < xd && rep.size() < kMaxRep; ++ix)
                        rep.push_back({ static_cast<int64_t>(ix) * xs, 0 });
                } else if (rtype == 3) {
                    uint64_t yd = RdU(b, n, p) + 2ULL;
                    int64_t ys = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t iy = 0; iy < yd && rep.size() < kMaxRep; ++iy)
                        rep.push_back({ 0, static_cast<int64_t>(iy) * ys });
                } else if (rtype >= 4 && rtype <= 7) {
                    bool isX = (rtype == 4 || rtype == 5);
                    bool hasGrid = (rtype == 5 || rtype == 7);
                    uint64_t dim = RdU(b, n, p) + 2ULL;
                    int64_t gf = hasGrid ? static_cast<int64_t>(RdU(b, n, p)) : 1;
                    if (dim > 65536ULL) { ok = false; return; }
                    int64_t acc = 0;
                    rep.push_back({ 0, 0 });
                    for (uint64_t i = 1; i < dim && ok; ++i) {
                        acc += static_cast<int64_t>(RdU(b, n, p)) * gf;
                        if (rep.size() < kMaxRep) rep.push_back(isX ? OasisPt{ acc, 0 } : OasisPt{ 0, acc });
                    }
                } else if (rtype == 8) {
                    uint64_t nd = RdU(b, n, p) + 2ULL;
                    uint64_t md = RdU(b, n, p) + 2ULL;
                    OasisPt nv = RdGDelta(b, n, p);
                    OasisPt mv = RdGDelta(b, n, p);
                    for (uint64_t im = 0; im < md && rep.size() < kMaxRep; ++im)
                        for (uint64_t in = 0; in < nd && rep.size() < kMaxRep; ++in)
                            rep.push_back({ static_cast<int64_t>(in) * nv.x + static_cast<int64_t>(im) * mv.x,
                                            static_cast<int64_t>(in) * nv.y + static_cast<int64_t>(im) * mv.y });
                } else if (rtype == 9) {
                    uint64_t dim = RdU(b, n, p) + 2ULL;
                    OasisPt dv = RdGDelta(b, n, p);
                    for (uint64_t i = 0; i < dim && rep.size() < kMaxRep; ++i)
                        rep.push_back({ static_cast<int64_t>(i) * dv.x, static_cast<int64_t>(i) * dv.y });
                } else if (rtype == 10 || rtype == 11) {
                    uint64_t cnt = RdU(b, n, p) + 2ULL;
                    int64_t gf = (rtype == 11) ? static_cast<int64_t>(RdU(b, n, p)) : 1;
                    if (cnt > 65536ULL) { ok = false; return; }
                    int64_t ax = 0, ay = 0;
                    rep.push_back({ 0, 0 });
                    for (uint64_t i = 1; i < cnt && ok; ++i) {
                        OasisPt dv = RdGDelta(b, n, p);
                        ax += dv.x * gf; ay += dv.y * gf;
                        if (rep.size() < kMaxRep) rep.push_back({ ax, ay });
                    }
                } else {
                    ok = false;
                }
                if (rep.empty()) rep.push_back({ 0, 0 });
            }
            std::string LookupMap(const std::vector<std::pair<uint64_t, std::string>>& tbl, uint64_t ref, const char* pfx) {
                for (const auto& kv : tbl) if (kv.first == ref) return kv.second;
                return std::string(pfx) + std::to_string(ref);
            }
            void SkipPropValue(const uint8_t* b, size_t n, size_t& p) {
                uint64_t vt = RdU(b, n, p);
                if (!ok) return;
                if (vt <= 3 || vt == 8 || vt == 13 || vt == 14 || vt == 15) { RdU(b, n, p); }
                else if (vt == 4 || vt == 5) { RdU(b, n, p); RdU(b, n, p); }
                else if (vt == 6) { if (p + 4 > n) ok = false; else p += 4; }
                else if (vt == 7) { if (p + 8 > n) ok = false; else p += 8; }
                else if (vt == 9) { RdS(b, n, p); }
                else if (vt >= 10 && vt <= 12) { RdStr(b, n, p); }
                else { ok = false; }
            }

            void ParseStream(const uint8_t* b, size_t n, size_t startPos, int depth) {
                size_t p = startPos;
                while (p < n && ok && !stopStream && cells.size() < 4096) {
                    uint8_t recId = b[p++];
                    if (recId == 0 || recId == 29) continue; // PAD / repeat PROPERTY
                    if (recId == 2) { stopStream = true; break; } // END
                    if (recId == 1) { // START
                        std::string ver = RdStr(b, n, p);
                        double u = RdReal(b, n, p);
                        uint64_t offFlag = RdU(b, n, p);
                        if (!ok || ver.empty() || ver[0] != '1' || !(u > 0.0) || !std::isfinite(u)) {
                            ok = false; break;
                        }
                        unit = u;
                        if (offFlag == 0) {
                            for (int k = 0; k < 12 && ok; ++k) RdU(b, n, p);
                        }
                        sawStart = true;
                        continue;
                    }
                    if (recId == 3) { cellNames.push_back({ nextCellRef++, RdStr(b, n, p) }); continue; }
                    if (recId == 4) { std::string s = RdStr(b, n, p); uint64_t r = RdU(b, n, p); cellNames.push_back({ r, s }); continue; }
                    if (recId == 5) { textStrings.push_back({ nextTextRef++, RdStr(b, n, p) }); continue; }
                    if (recId == 6) { std::string s = RdStr(b, n, p); uint64_t r = RdU(b, n, p); textStrings.push_back({ r, s }); continue; }
                    if (recId == 7 || recId == 9) { RdStr(b, n, p); continue; }
                    if (recId == 8 || recId == 10) { RdStr(b, n, p); RdU(b, n, p); continue; }
                    if (recId == 11 || recId == 12) { RdStr(b, n, p); SkipInterval(b, n, p); SkipInterval(b, n, p); continue; }
                    if (recId == 13 || recId == 14) { // CELL
                        std::string cname = (recId == 13) ? LookupMap(cellNames, RdU(b, n, p), "CELL_") : RdStr(b, n, p);
                        cells.push_back({ std::move(cname), {} });
                        mod = OasisModal{};
                        continue;
                    }
                    if (recId == 15) { mod.xyAbs = true; continue; }
                    if (recId == 16) { mod.xyAbs = false; continue; }
                    if (recId == 28) { // PROPERTY
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x04u) {
                            if (info & 0x02u) RdU(b, n, p);
                            else RdStr(b, n, p);
                        }
                        if ((info & 0x08u) == 0) {
                            uint64_t cnt = (info >> 4) & 0x0Fu;
                            if (cnt == 15ULL) cnt = RdU(b, n, p);
                            if (cnt > 65536ULL) { ok = false; break; }
                            for (uint64_t k = 0; k < cnt && ok; ++k) SkipPropValue(b, n, p);
                        }
                        continue;
                    }
                    if (recId == 30) { RdU(b, n, p); RdStr(b, n, p); continue; }
                    if (recId == 31) { RdU(b, n, p); RdStr(b, n, p); RdU(b, n, p); continue; }
                    if (recId == 32) { RdU(b, n, p); RdStr(b, n, p); continue; }
                    if (recId == 33) {
                        uint8_t info = RdByte(b, n, p);
                        RdU(b, n, p);
                        if (info & 0x01u) mod.layer = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        RdStr(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        if (info & 0x04u) RdRepetition(b, n, p, mod.modalRep);
                        continue;
                    }
                    if (recId == 34) { // CBLOCK (RFC 1951 raw Deflate)
                        uint64_t compType = RdU(b, n, p);
                        uint64_t uncompSz = RdU(b, n, p);
                        uint64_t compSz   = RdU(b, n, p);
                        if (!ok || depth > 3 || compType != 0 || compSz > n - p || uncompSz > 32 * 1024 * 1024ULL) {
                            ok = false; break;
                        }
                        std::vector<uint8_t> cblockBuf;
                        if (!ZipDeflate::InflateRaw(b + p, static_cast<size_t>(compSz), cblockBuf, static_cast<size_t>(uncompSz) + 64)) {
                            ok = false; break;
                        }
                        p += static_cast<size_t>(compSz);
                        ParseStream(cblockBuf.data(), cblockBuf.size(), 0, depth + 1);
                        continue;
                    }

                    if (cells.empty()) cells.push_back({ "TOP", {} });
                    auto& curShapes = cells.back().shapes;

                    if (recId == 17 || recId == 18) { // PLACEMENT
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x80u) {
                            mod.placeCell = (info & 0x40u) ? LookupMap(cellNames, RdU(b, n, p), "CELL_") : RdStr(b, n, p);
                        }
                        double mag = 1.0, deg = 0.0;
                        bool flip = (info & 0x01u) != 0;
                        bool hasRep = false;
                        if (recId == 17) {
                            deg = static_cast<double>(((info >> 1) & 3u) * 90u);
                            if (info & 0x20u) { int64_t d = RdS(b, n, p); mod.placeX = mod.xyAbs ? d : (mod.placeX + d); }
                            if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.placeY = mod.xyAbs ? d : (mod.placeY + d); }
                            hasRep = (info & 0x08u) != 0;
                        } else {
                            if (info & 0x20u) mag = RdReal(b, n, p);
                            if (info & 0x10u) deg = RdReal(b, n, p);
                            if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.placeX = mod.xyAbs ? d : (mod.placeX + d); }
                            if (info & 0x04u) { int64_t d = RdS(b, n, p); mod.placeY = mod.xyAbs ? d : (mod.placeY + d); }
                            hasRep = (info & 0x02u) != 0;
                        }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (hasRep) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 6;
                            sh.x = mod.placeX; sh.y = mod.placeY;
                            sh.str = mod.placeCell;
                            sh.mag = mag; sh.deg = deg; sh.flip = flip;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 19) { // TEXT
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x40u) {
                            mod.textStr = (info & 0x20u) ? LookupMap(textStrings, RdU(b, n, p), "TXT_") : RdStr(b, n, p);
                        }
                        if (info & 0x01u) mod.textLayer = RdU(b, n, p);
                        if (info & 0x02u) mod.textType  = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.textX = mod.xyAbs ? d : (mod.textX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.textY = mod.xyAbs ? d : (mod.textY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && !mod.textStr.empty() && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 5;
                            sh.layer = mod.textLayer;
                            sh.x = mod.textX; sh.y = mod.textY;
                            sh.str = mod.textStr;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 20) { // RECTANGLE
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x40u) mod.rectW    = RdU(b, n, p);
                        if (info & 0x80u) mod.rectH    = mod.rectW;
                        else if (info & 0x20u) mod.rectH = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 1; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.rectW; sh.h = mod.rectH;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 21) { // POLYGON
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x20u) RdPointList(b, n, p, true, mod.polyPts);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.polyPts.size() >= 3 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 2; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.pts = mod.polyPts;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 22) { // PATH
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer     = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype  = RdU(b, n, p);
                        if (info & 0x40u) mod.pathHalfW = RdU(b, n, p);
                        if (info & 0x80u) {
                            uint8_t es = RdByte(b, n, p);
                            uint8_t sSch = (es >> 2) & 3u, eSch = es & 3u;
                            if (sSch == 3u) mod.pathStartExt = RdS(b, n, p);
                            else if (sSch == 1u) mod.pathStartExt = 0;
                            else if (sSch == 2u) mod.pathStartExt = static_cast<int64_t>(mod.pathHalfW);
                            if (eSch == 3u) mod.pathEndExt = RdS(b, n, p);
                            else if (eSch == 1u) mod.pathEndExt = 0;
                            else if (eSch == 2u) mod.pathEndExt = static_cast<int64_t>(mod.pathHalfW);
                        }
                        if (info & 0x20u) RdPointList(b, n, p, false, mod.pathPts);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.pathPts.size() >= 2 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 3; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.pathHalfW * 2ULL;
                            sh.pts = mod.pathPts;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 23 || recId == 24 || recId == 25) { // TRAPEZOID / CTRAPEZOID
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        int64_t da = 0, db = 0;
                        bool vert = false;
                        if (recId == 25) {
                            if (info & 0x80u) mod.ctrapType = RdU(b, n, p);
                            if (info & 0x40u) mod.trapW = RdU(b, n, p);
                            if (info & 0x20u) mod.trapH = RdU(b, n, p);
                            if (mod.ctrapType >= 16 && mod.ctrapType <= 19) mod.trapH = mod.trapW;
                            else if (mod.ctrapType >= 20 && mod.ctrapType <= 23) mod.trapW = mod.trapH;
                            else if (mod.ctrapType == 24) mod.trapH = mod.trapW;
                        } else {
                            vert = (info & 0x80u) != 0;
                            if (info & 0x40u) mod.trapW = RdU(b, n, p);
                            if (info & 0x20u) mod.trapH = RdU(b, n, p);
                            da = RdS(b, n, p);
                            if (recId == 23) db = RdS(b, n, p);
                        }
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            int64_t tw = static_cast<int64_t>(mod.trapW), th = static_cast<int64_t>(mod.trapH);
                            OasisShape sh;
                            sh.kind = 2; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            if (recId == 25) {
                                sh.pts = { { 0, 0 }, { tw, 0 }, { tw, th }, { 0, th } };
                            } else if (!vert) {
                                int64_t x0b = std::max<int64_t>(0, -da), x0t = std::max<int64_t>(0, da);
                                int64_t x1b = tw - std::max<int64_t>(0, db), x1t = tw + std::min<int64_t>(0, db);
                                sh.pts = { { x0b, 0 }, { x1b, 0 }, { x1t, th }, { x0t, th } };
                            } else {
                                int64_t y0l = std::max<int64_t>(0, -da), y0r = std::max<int64_t>(0, da);
                                int64_t y1l = th - std::max<int64_t>(0, db), y1r = th + std::min<int64_t>(0, db);
                                sh.pts = { { 0, y0l }, { tw, y0r }, { tw, y1r }, { 0, y1l } };
                            }
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 26) { // CIRCLE
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x20u) mod.circleR  = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.circleR > 0 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 4; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.circleR;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else {
                        ok = false;
                        break;
                    }
                }
            }
        };

        OasisParser parser;
        parser.ParseStream(data, size, 13, 0);
        if (!parser.ok || !parser.sawStart || parser.cells.empty()) return false;

        double toUm = 1.0 / parser.unit;
        auto mapLayer = [](uint64_t lyr) -> EdaLayerId {
            switch (lyr & 3ULL) {
            case 1: return EdaLayerId::TopCopper;
            case 2: return EdaLayerId::BottomCopper;
            case 3: return EdaLayerId::TopSilk;
            default: return EdaLayerId::InnerCopper;
            }
        };
        auto findCellIdx = [&](const std::string& nm) -> int {
            for (size_t i = 0; i < parser.cells.size(); ++i) {
                if (parser.cells[i].name == nm) return static_cast<int>(i);
            }
            return -1;
        };

        // Determine root cells (cells not instantiated via PLACEMENT by any other cell)
        std::vector<bool> referenced(parser.cells.size(), false);
        for (const auto& c : parser.cells) {
            for (const auto& s : c.shapes) {
                if (s.kind == 6) {
                    int idx = findCellIdx(s.str);
                    if (idx >= 0) referenced[static_cast<size_t>(idx)] = true;
                }
            }
        }

        struct EmitCtx {
            const OasisParser& P;
            ParsedEdaBoard& out;
            double toUm;
            void EmitCell(int cellIdx, double oxUm, double oyUm, double scale, double rotDeg, bool flipY, int depth) {
                if (depth > 8 || out.traces.size() >= 25000) return;
                const auto& cell = P.cells[static_cast<size_t>(cellIdx)];
                double rad = rotDeg * 3.14159265358979323846 / 180.0;
                double cs = std::cos(rad), sn = std::sin(rad);
                auto xform = [&](double lxUm, double lyUm) -> std::pair<float, float> {
                    double sy = flipY ? -lyUm : lyUm;
                    double sx = lxUm * scale, syy = sy * scale;
                    double rx = oxUm + sx * cs - syy * sn;
                    double ry = oyUm + sx * sn + syy * cs;
                    return { static_cast<float>(rx), static_cast<float>(ry) };
                };
                auto mapL = [](uint64_t lyr) -> EdaLayerId {
                    switch (lyr & 3ULL) {
                    case 1: return EdaLayerId::TopCopper;
                    case 2: return EdaLayerId::BottomCopper;
                    case 3: return EdaLayerId::TopSilk;
                    default: return EdaLayerId::InnerCopper;
                    }
                };

                for (const auto& sh : cell.shapes) {
                    if (out.traces.size() >= 25000) break;
                    EdaLayerId lid = mapL(sh.layer);
                    for (const auto& ro : sh.repOffsets) {
                        if (out.traces.size() >= 25000) break;
                        double bx = static_cast<double>(sh.x + ro.x) * toUm;
                        double by = static_cast<double>(sh.y + ro.y) * toUm;
                        if (sh.kind == 1) { // RECTANGLE
                            double rw = static_cast<double>(sh.w) * toUm;
                            double rh = static_cast<double>(sh.h) * toUm;
                            if (rw <= 0.0 && rh <= 0.0) continue;
                            auto p0 = xform(bx, by);
                            auto p1 = xform(bx + rw, by);
                            auto p2 = xform(bx + rw, by + rh);
                            auto p3 = xform(bx, by + rh);
                            out.traces.push_back({ p0.first, p0.second, p1.first, p1.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p1.first, p1.second, p2.first, p2.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p2.first, p2.second, p3.first, p3.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p3.first, p3.second, p0.first, p0.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                        } else if (sh.kind == 2 && sh.pts.size() >= 3) { // POLYGON / TRAPEZOID
                            size_t m = sh.pts.size();
                            for (size_t k = 0; k < m; ++k) {
                                size_t kn = (k + 1) % m;
                                auto a = xform(bx + static_cast<double>(sh.pts[k].x) * toUm, by + static_cast<double>(sh.pts[k].y) * toUm);
                                auto b = xform(bx + static_cast<double>(sh.pts[kn].x) * toUm, by + static_cast<double>(sh.pts[kn].y) * toUm);
                                out.traces.push_back({ a.first, a.second, b.first, b.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            }
                        } else if (sh.kind == 3 && sh.pts.size() >= 2) { // PATH
                            float pw = std::clamp(static_cast<float>(static_cast<double>(sh.w) * toUm * scale), 0.12f, 4.0f);
                            for (size_t k = 1; k < sh.pts.size(); ++k) {
                                auto a = xform(bx + static_cast<double>(sh.pts[k - 1].x) * toUm, by + static_cast<double>(sh.pts[k - 1].y) * toUm);
                                auto b = xform(bx + static_cast<double>(sh.pts[k].x) * toUm,     by + static_cast<double>(sh.pts[k].y) * toUm);
                                out.traces.push_back({ a.first, a.second, b.first, b.second, pw, lid, static_cast<int>(sh.layer + 1) });
                            }
                        } else if (sh.kind == 4 && out.circles.size() < 8192) { // CIRCLE
                            auto c = xform(bx, by);
                            float r = std::max(0.05f, static_cast<float>(static_cast<double>(sh.w) * toUm * scale));
                            out.circles.push_back({ c.first, c.second, r, std::min(0.20f, r * 0.35f), false, lid });
                        } else if (sh.kind == 5 && out.labels.size() < 4096) { // TEXT
                            auto c = xform(bx, by);
                            out.labels.push_back({ c.first, c.second, 0.85f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(sh.str) });
                        } else if (sh.kind == 6) { // PLACEMENT
                            auto c = xform(bx, by);
                            int subIdx = -1;
                            for (size_t ci = 0; ci < P.cells.size(); ++ci) {
                                if (P.cells[ci].name == sh.str) { subIdx = static_cast<int>(ci); break; }
                            }
                            if (subIdx >= 0 && subIdx != cellIdx) {
                                EmitCell(subIdx, c.first, c.second, scale * sh.mag, rotDeg + sh.deg, flipY ^ sh.flip, depth + 1);
                            } else if (out.pads.size() < 4096) {
                                std::wstring wnm = PathUtils::Utf8ToWide(sh.str);
                                float sz = static_cast<float>(2.0 * sh.mag * scale);
                                out.pads.push_back({ c.first, c.second, sz, sz, static_cast<float>(rotDeg + sh.deg), 0.0f, 0, EdaLayerId::TopSilk, wnm, wnm });
                                out.labels.push_back({ c.first, c.second, 0.7f, EdaLayerId::TopSilk, wnm });
                            }
                        }
                    }
                }
            }
        };

        (void)mapLayer;
        EmitCtx emitter{ parser, out, toUm };
        bool anyRoot = false;
        for (size_t i = 0; i < parser.cells.size(); ++i) {
            if (!referenced[i]) {
                anyRoot = true;
                emitter.EmitCell(static_cast<int>(i), 0.0, 0.0, 1.0, 0.0, false, 0);
            }
        }
        if (!anyRoot) {
            emitter.EmitCell(0, 0.0, 0.0, 1.0, 0.0, false, 0);
        }

        if (out.traces.empty() && out.circles.empty() && out.pads.empty() && out.labels.empty()) {
            return false;
        }
        return true;
    }
    return false;
}

} // namespace Formats
