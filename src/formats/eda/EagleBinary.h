#pragma once
// EagleBinary.h — CadSoft EAGLE v4/v5 binary board & schematic (.brd, .sch, .lbr) reader.
// Included inside `namespace Explorer2` in src/EdaReaders.cpp.

inline bool ParseEagleBinaryV4V5(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (!data || size < 48 || data[0] != 0x10) return false;

    using ::Explorer2::ByteIO::RdU16LE;
    using ::Explorer2::ByteIO::RdU32LE;
    using ::Explorer2::ByteIO::RdI32LE;
    auto rdU16 = [](const uint8_t* p) { return RdU16LE(p); };
    auto rdU32 = [](const uint8_t* p) { return RdU32LE(p); };
    auto rdI32 = [](const uint8_t* p) { return RdI32LE(p); };

    const uint32_t numBlocks = rdU32(data + 4);
    const uint8_t majVer = data[8];
    const uint8_t minVer = data[9];
    if (numBlocks < 2 || numBlocks > 4000000u || majVer < 3 || majVer > 7) return false;
    const uint64_t blocksByteEnd = static_cast<uint64_t>(numBlocks) * 24ULL;
    if (blocksByteEnd > static_cast<uint64_t>(size)) return false;

    size_t strPoolStart = static_cast<size_t>(blocksByteEnd);
    size_t poolCursor = strPoolStart;
    if (poolCursor + 4 <= size && data[poolCursor] < 32) {
        poolCursor += 4;
    }

    auto readEagleStr = [&](const uint8_t* field, size_t maxLen) -> std::string {
        if (maxLen == 0) return {};
        if (field[0] == 0x7F && strPoolStart < size) {
            if (maxLen >= 5) {
                uint32_t off = rdU32(field + 1);
                if (off > 0 && strPoolStart + off < size && data[strPoolStart + off] >= 32 && data[strPoolStart + off] < 127) {
                    size_t p = strPoolStart + off;
                    size_t e = p;
                    while (e < size && data[e] != 0 && e - p < 128) ++e;
                    return std::string(reinterpret_cast<const char*>(data + p), e - p);
                }
            }
            while (poolCursor < size && data[poolCursor] < 32) ++poolCursor;
            size_t s = poolCursor;
            while (poolCursor < size && data[poolCursor] >= 32 && poolCursor - s < 128) ++poolCursor;
            std::string res(reinterpret_cast<const char*>(data + s), poolCursor - s);
            if (poolCursor < size && data[poolCursor] == 0) ++poolCursor;
            return res;
        }
        std::string s;
        for (size_t i = 0; i < maxLen; ++i) {
            uint8_t c = field[i];
            if (c == 0) break;
            if (c >= 32 && c < 127) s.push_back(static_cast<char>(c));
        }
        return s;
    };

    bool isSch = (ext == L".sch");
    for (uint32_t bi = 1; bi < std::min<uint32_t>(numBlocks, 64u); ++bi) {
        uint8_t op = data[bi * 24];
        if (op == 0x14) { isSch = true; break; }
        if (op == 0x16) { isSch = false; break; }
    }

    wchar_t verTitle[96] = {};
    swprintf_s(verTitle, isSch ? L"Eagle Binary Schematic (v%u.%u)" : L"Eagle Binary Board (v%u.%u)", majVer, minVer);
    out.kind = isSch ? EdaDocKind::EagleSch : EdaDocKind::EagleBrd;
    out.formatTitle = verTitle;

    std::unordered_map<int, EdaLayerId> customLayers;
    auto mapLayer = [&](uint8_t lyr) -> EdaLayerId {
        auto it = customLayers.find(static_cast<int>(lyr));
        if (it != customLayers.end()) return it->second;
        return MapEagleLayer(static_cast<int>(lyr), isSch);
    };

    struct EagleBinSymPin {
        float px = 0.0f, py = 0.0f;
        float bx = 0.0f, by = 0.0f;
        bool hasStem = false;
        bool hasDot = false;
        std::wstring pinName;
    };
    struct EagleBinSymText {
        float tx = 0.0f, ty = 0.0f, size = 1.2f;
        uint8_t layerNum = 94;
        std::string text;
    };
    struct EagleBinSymDef {
        std::string name;
        std::vector<EdaTraceSeg> localLines;
        std::vector<EdaCirclePrim> localCircles;
        std::vector<EagleBinSymPin> localPins;
        std::vector<EagleBinSymText> localTexts;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;

        void Expand(float x, float y) {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    };
    struct EagleBinGateDef {
        uint16_t symIdx = 0;
        float gx = 0.0f, gy = 0.0f;
        std::string name;
    };
    struct EagleBinDevSetDef {
        uint16_t libId = 1;
        std::string name;
        std::string prefix;
        std::vector<EagleBinGateDef> gates;
    };
    struct EagleBinSchPart {
        uint16_t u16_4 = 0;
        uint16_t u16_6 = 0;
        float ix = 0.0f, iy = 0.0f;
        uint16_t rotFlags = 0;
        std::string name;
        std::string value;
        bool instantiatedByGate = false;
    };
    struct EagleBinGateInst {
        uint16_t partIdx = 0;
        uint16_t gateIdx = 1;
        float ix = 0.0f, iy = 0.0f;
        uint16_t rotFlags = 0;
    };

    std::map<std::pair<uint16_t, uint16_t>, EaglePkgDef> libPackages;
    std::map<std::pair<uint16_t, uint16_t>, EagleBinSymDef> libSymbols;
    std::vector<std::pair<uint16_t, uint16_t>> globalSymOrder;
    std::map<std::pair<uint16_t, uint16_t>, EagleBinDevSetDef> libDevSets;
    std::vector<std::pair<uint16_t, uint16_t>> globalDevSetOrder;
    std::vector<EagleBinSchPart> schParts;
    std::vector<EagleBinGateInst> schGateInsts;
    std::unordered_map<uint16_t, std::string> schPartNames;

    uint16_t curLibId = 0, curPkgId = 0, curSymId = 0, curDevSetId = 0, curPartId = 0;
    uint32_t libEndBlock = 0, pkgEndBlock = 0, symEndBlock = 0, devSetEndBlock = 0;
    int curNetId = 0;
    size_t signalCount = 0;
    std::string pendingElemName;
    int lastCompIdx = -1;
    int lastSchPartIdx = -1;
    uint32_t elemAttrEndBlock = 0;
    uint32_t schPartAttrEndBlock = 0;
    bool anyElementPlaced = false;

    auto readEagleName8or16 = [&](const uint8_t* r) -> std::string {
        if (r[8] == 0x7F) {
            std::string s = readEagleStr(r + 8, 16);
            if (!s.empty()) return s;
        } else if (r[8] >= 33 && r[8] < 127) {
            bool allPrintable = true;
            for (size_t k = 8; k < 24 && r[k] != 0; ++k) {
                if (r[k] < 32 || r[k] >= 127) { allPrintable = false; break; }
            }
            if (allPrintable) {
                std::string s = readEagleStr(r + 8, 16);
                if (!s.empty()) return s;
            }
        }
        return readEagleStr(r + 16, 8);
    };

    auto decodeRotDeg = [](uint16_t rotFlags) -> float {
        uint16_t raw = rotFlags & 0x0FFFu;
        if (raw == 0) return 0.0f;
        if (raw == 90 || raw == 180 || raw == 270 || raw == 45 || raw == 135 || raw == 225 || raw == 315) {
            return static_cast<float>(raw);
        }
        return static_cast<float>(raw) * (360.0f / 4096.0f);
    };

    auto decodeDrillAndDiam = [&](const uint8_t* rec, float& outDrillMm, float& outDiamMm) {
        int32_t d32  = rdI32(rec + 12);
        int32_t od32 = rdI32(rec + 16);
        uint16_t hd16 = rdU16(rec + 12);
        uint16_t ho16 = rdU16(rec + 14);
        if (rec[14] == 0 && rec[15] == 0 && d32 > 0 && d32 < 500000 && od32 >= 0 && od32 < 500000) {
            outDrillMm = static_cast<float>(d32) * 1e-4f;
            outDiamMm  = (od32 > 0) ? (static_cast<float>(od32) * 1e-4f) : (outDrillMm * 1.8f);
        } else if (ho16 > 0 || hd16 > 0) {
            outDrillMm = static_cast<float>(hd16) * 2e-4f;
            outDiamMm  = (ho16 > 0) ? (static_cast<float>(ho16) * 2e-4f) : std::max(0.5f, outDrillMm * 1.8f);
        } else {
            outDrillMm = 0.4f;
            outDiamMm  = 0.8f;
        }
        outDrillMm = std::clamp(outDrillMm, 0.1f, 25.0f);
        outDiamMm  = std::clamp(std::max(outDiamMm, outDrillMm * 1.25f), 0.2f, 35.0f);
    };

    auto appendTessellatedArc = [](std::vector<EdaTraceSeg>& dst, float x1, float y1, float x2, float y2,
                                   float curveDeg, float w, EdaLayerId lid, int netId) {
        float dx = x2 - x1, dy = y2 - y1;
        float chord = std::hypot(dx, dy);
        if (std::fabs(curveDeg) < 2.0f || chord < 1e-4f) {
            dst.push_back({ x1, y1, x2, y2, w, lid, netId });
            return;
        }
        float rad = curveDeg * 3.14159265f / 180.0f;
        float halfSin = std::sin(std::fabs(rad) * 0.5f);
        if (halfSin < 1e-4f) {
            dst.push_back({ x1, y1, x2, y2, w, lid, netId });
            return;
        }
        float radius = (chord * 0.5f) / halfSin;
        float mx = (x1 + x2) * 0.5f, my = (y1 + y2) * 0.5f;
        float h = std::sqrt(std::max(0.0f, radius * radius - (chord * chord * 0.25f)));
        float sign = (std::fabs(rad) > 3.14159265f) ? -1.0f : 1.0f;
        if (rad < 0.0f) sign = -sign;
        float nx = -dy / chord, ny = dx / chord;
        float cx = mx + nx * h * sign;
        float cy = my + ny * h * sign;
        float a0 = std::atan2(y1 - cy, x1 - cx);
        int steps = std::clamp(static_cast<int>(std::fabs(curveDeg) / 15.0f), 4, 24);
        float px = x1, py = y1;
        for (int s = 1; s <= steps; ++s) {
            float a = a0 + rad * (static_cast<float>(s) / static_cast<float>(steps));
            float qx = (s == steps) ? x2 : (cx + radius * std::cos(a));
            float qy = (s == steps) ? y2 : (cy + radius * std::sin(a));
            dst.push_back({ px, py, qx, qy, w, lid, netId });
            px = qx;
            py = qy;
        }
    };

    for (uint32_t bi = 1; bi < numBlocks; ++bi) {
        const uint8_t* rec = data + static_cast<size_t>(bi) * 24;
        const uint8_t op = rec[0];
        const uint16_t activeLibId = (curLibId > 0) ? curLibId : 1u;
        const bool inPkgDef    = (bi < pkgEndBlock && curLibId > 0 && curPkgId > 0);
        const bool inSymDef    = (bi < symEndBlock && curSymId > 0);
        const bool inDevSetDef = (bi < devSetEndBlock && curDevSetId > 0);

        switch (op) {
        case 0x13: { // Layer definition
            uint8_t lnum = rec[3];
            std::string lname = readEagleStr(rec + 15, 9);
            std::string low = PathUtils::ToLowerAscii(lname);
            if (low.find("bottom") != std::string::npos || low == "bplace") {
                customLayers[lnum] = (low.find("place") != std::string::npos || low.find("names") != std::string::npos)
                    ? EdaLayerId::BottomSilk : EdaLayerId::BottomCopper;
            } else if (low.find("dimension") != std::string::npos || low.find("outline") != std::string::npos) {
                customLayers[lnum] = EdaLayerId::EdgeCuts;
            } else if (low == "nets" || low == "busses") {
                customLayers[lnum] = EdaLayerId::SchWire;
            } else if (low == "symbols") {
                customLayers[lnum] = EdaLayerId::SchSymbol;
            }
            break;
        }
        case 0x15: { // Library header
            uint32_t sub1 = rdU32(rec + 4);
            uint32_t sub2 = rdU32(rec + 8);
            uint32_t sub3 = rdU32(rec + 12);
            uint64_t totalSub = static_cast<uint64_t>(sub1) + sub2 + sub3;
            if (totalSub == 0) {
                uint16_t c16 = rdU16(rec + 2);
                if (c16 > 0) totalSub = c16;
            }
            if (totalSub > 0 && bi + 1 + totalSub <= numBlocks) {
                ++curLibId;
                curPkgId = 0;
                curSymId = 0;
                curDevSetId = 0;
                libEndBlock = bi + 1 + static_cast<uint32_t>(totalSub);
            }
            break;
        }
        case 0x17: { // DeviceSet header (in library) OR Element name record
            uint16_t dsSub1 = rdU16(rec + 2);
            uint16_t dsSub2 = rdU16(rec + 4);
            uint32_t dsTotal = static_cast<uint32_t>(dsSub1) + static_cast<uint32_t>(dsSub2);
            bool looksLikeDevSet = isSch && dsTotal > 0 && bi + 1 + dsTotal <= numBlocks &&
                                   (bi < libEndBlock || (bi + 1 < numBlocks && (data[(bi + 1) * 24] == 0x37 || data[(bi + 1) * 24] == 0x2E)));
            if (looksLikeDevSet) {
                ++curDevSetId;
                devSetEndBlock = bi + 1 + dsTotal;
                EagleBinDevSetDef ds;
                ds.libId = activeLibId;
                ds.prefix = readEagleStr(rec + 8, 6);
                ds.name = readEagleStr(rec + 16, 8);
                if (ds.name.empty()) ds.name = readEagleName8or16(rec);
                libDevSets[{ activeLibId, curDevSetId }] = std::move(ds);
                globalDevSetOrder.push_back({ activeLibId, curDevSetId });
                break;
            }
            std::string nm = readEagleStr(rec + 8, 16);
            if (nm.empty()) nm = readEagleStr(rec + 16, 8);
            if (!nm.empty()) {
                if (lastCompIdx >= 0 && bi <= elemAttrEndBlock &&
                    out.components[lastCompIdx].refDes.rfind(L"E", 0) == 0)
                {
                    out.components[lastCompIdx].refDes = PathUtils::Utf8ToWide(nm);
                } else {
                    pendingElemName = nm;
                }
            }
            break;
        }
        case 0x18: { // Schematic Symbol Definition Header
            uint16_t childCnt = rdU16(rec + 2);
            if (childCnt == 0) {
                uint32_t c32 = rdU32(rec + 4);
                if (c32 > 0 && c32 <= 65535u) childCnt = static_cast<uint16_t>(c32);
            }
            if (childCnt > 0 && bi + 1 + childCnt <= numBlocks) {
                ++curSymId;
                symEndBlock = bi + 1 + childCnt;
                EagleBinSymDef sym;
                sym.name = readEagleName8or16(rec);
                libSymbols[{ activeLibId, curSymId }] = std::move(sym);
                globalSymOrder.push_back({ activeLibId, curSymId });
            }
            break;
        }
        case 0x19: { // Package (inside 0x15 library) OR Signal/Net name
            uint16_t childCnt = rdU16(rec + 2);
            if (bi < libEndBlock && childCnt > 0 && bi + 1 + childCnt <= libEndBlock) {
                ++curPkgId;
                pkgEndBlock = bi + 1 + childCnt;
                libPackages[{ curLibId, curPkgId }] = EaglePkgDef{};
            } else {
                ++signalCount;
                curNetId = static_cast<int>(signalCount);
            }
            break;
        }
        case 0x1C: { // DeviceSet section / DeviceSet (in .sch library) OR Signal/Net
            uint16_t childCnt = rdU16(rec + 2);
            if (isSch && childCnt > 0 && bi + 1 + childCnt <= numBlocks &&
                (bi + 1 < numBlocks && (data[(bi + 1) * 24] == 0x37 || data[(bi + 1) * 24] == 0x2E)))
            {
                ++curDevSetId;
                devSetEndBlock = bi + 1 + childCnt;
                EagleBinDevSetDef ds;
                ds.libId = activeLibId;
                ds.name = readEagleName8or16(rec);
                libDevSets[{ activeLibId, curDevSetId }] = std::move(ds);
                globalDevSetOrder.push_back({ activeLibId, curDevSetId });
            } else if (!isSch || bi >= libEndBlock) {
                ++signalCount;
                curNetId = static_cast<int>(signalCount);
            }
            break;
        }
        case 0x1E: { // Bus / Signal
            if (!inSymDef && !inPkgDef && bi >= libEndBlock) {
                ++signalCount;
                curNetId = static_cast<int>(signalCount);
            }
            break;
        }
        case 0x1D: { // Symbol Section wrapper (in library) OR Schematic Part name
            uint32_t symSecSub = rdU32(rec + 4);
            if (symSecSub > 0 && bi + 1 + symSecSub <= numBlocks &&
                bi + 1 < numBlocks && data[(bi + 1) * 24] == 0x18)
            {
                if (curLibId == 0) {
                    curLibId = 1;
                    libEndBlock = std::max(libEndBlock, bi + 1 + symSecSub);
                }
                break;
            }
            ++curPartId;
            std::string pname = readEagleName8or16(rec);
            if (!pname.empty()) schPartNames[curPartId] = pname;
            break;
        }
        case 0x1F: { // Wire / Track (or Eagle v5 Schematic Net header when coordinates are 0 and +16 has net name)
            int32_t rx1 = rdI32(rec + 4), ry1 = rdI32(rec + 8), rx2 = rdI32(rec + 12), ry2 = rdI32(rec + 16);
            if (isSch && !inSymDef && !inPkgDef && rx1 == 0 && ry1 == 0 && rx2 == 0 && ry2 == 0 &&
                (rec[16] >= 33 || rec[16] == 0x7F))
            {
                ++signalCount;
                curNetId = static_cast<int>(signalCount);
                break;
            }
            float x1 = static_cast<float>(rx1) * 1e-4f;
            float y1 = static_cast<float>(ry1) * 1e-4f;
            float x2 = static_cast<float>(rx2) * 1e-4f;
            float y2 = static_cast<float>(ry2) * 1e-4f;
            if (std::fabs(x1) > 20000.0f || std::fabs(y1) > 20000.0f ||
                std::fabs(x2) > 20000.0f || std::fabs(y2) > 20000.0f) break;
            float w = std::clamp(static_cast<float>(rdU16(rec + 20)) * 2e-4f, 0.08f, 15.0f);
            EdaLayerId lid = mapLayer(rec[3]);
            if (inSymDef) {
                auto& sym = libSymbols[{ activeLibId, curSymId }];
                sym.localLines.push_back({ x1, y1, x2, y2, std::max(0.18f, w), EdaLayerId::SchSymbol, 0 });
                sym.Expand(x1, y1);
                sym.Expand(x2, y2);
            } else if (inPkgDef) {
                auto& pkg = libPackages[{ curLibId, curPkgId }];
                pkg.localSilk.push_back({ x1, y1, x2, y2, w, lid, 0 });
                pkg.minX = std::min({ pkg.minX, x1, x2 });
                pkg.maxX = std::max({ pkg.maxX, x1, x2 });
                pkg.minY = std::min({ pkg.minY, y1, y2 });
                pkg.maxY = std::max({ pkg.maxY, y1, y2 });
            } else if (out.traces.size() < 80000) {
                out.traces.push_back({ x1, y1, x2, y2, w, lid, curNetId });
            }
            break;
        }
        case 0x20:
        case 0x26: { // Rectangle (0x26 in Eagle v4/v5, 0x20 legacy)
            float x1 = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float y1 = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float x2 = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            float y2 = static_cast<float>(rdI32(rec + 16)) * 1e-4f;
            if (std::fabs(x1) > 20000.0f || std::fabs(x2) > 20000.0f ||
                std::fabs(y1) > 20000.0f || std::fabs(y2) > 20000.0f) break;
            EdaLayerId lid = inSymDef ? EdaLayerId::SchSymbol : mapLayer(rec[3]);
            auto pushRect = [&](std::vector<EdaTraceSeg>& dst) {
                dst.push_back({ x1, y1, x2, y1, 0.22f, lid, 0 });
                dst.push_back({ x2, y1, x2, y2, 0.22f, lid, 0 });
                dst.push_back({ x2, y2, x1, y2, 0.22f, lid, 0 });
                dst.push_back({ x1, y2, x1, y1, 0.22f, lid, 0 });
            };
            if (inSymDef) {
                auto& sym = libSymbols[{ activeLibId, curSymId }];
                pushRect(sym.localLines);
                sym.Expand(x1, y1);
                sym.Expand(x2, y2);
            } else if (inPkgDef) {
                auto& pkg = libPackages[{ curLibId, curPkgId }];
                pushRect(pkg.localSilk);
                pkg.minX = std::min({ pkg.minX, x1, x2 });
                pkg.maxX = std::max({ pkg.maxX, x1, x2 });
                pkg.minY = std::min({ pkg.minY, y1, y2 });
                pkg.maxY = std::max({ pkg.maxY, y1, y2 });
            } else if (out.traces.size() < 80000) {
                pushRect(out.traces);
            }
            break;
        }
        case 0x21: { // Polygon container (when nrChild > 0) OR Text
            uint16_t polyChildren = rdU16(rec + 2);
            if (polyChildren > 0 && rec[18] == 0) {
                // Child 0x22 / 0x1F wire records immediately follow and trace the polygon edges
                break;
            }
            float tx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float ty = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float sz = std::clamp(static_cast<float>(rdU16(rec + 12)) * 2e-4f, 0.8f, 5.0f);
            std::string txt = readEagleStr(rec + 18, 6);
            if (!txt.empty()) {
                if (inSymDef) {
                    libSymbols[{ activeLibId, curSymId }].localTexts.push_back({ tx, ty, sz, rec[3], std::move(txt) });
                } else if (!inPkgDef && out.labels.size() < 8000) {
                    out.labels.push_back({ tx, ty, sz, mapLayer(rec[3]), PathUtils::Utf8ToWide(txt) });
                }
            }
            break;
        }
        case 0x22: { // Wire in Eagle v4/v5 (.sch / symbol / package), or legacy .brd Dimension circle
            if (!isSch && !inSymDef && !inPkgDef && rec[3] == 20 && rdI32(rec + 16) == 0 &&
                rdI32(rec + 4) > 0 && rdI32(rec + 12) > 0 && rdI32(rec + 12) < rdI32(rec + 4))
            {
                float cx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
                float cy = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
                float r  = std::fabs(static_cast<float>(rdI32(rec + 12)) * 1e-4f);
                float w  = (rdU16(rec + 20) > 0) ? (static_cast<float>(rdU16(rec + 20)) * 2e-4f) : 0.20f;
                if (out.circles.size() < 20000) {
                    out.circles.push_back({ cx, cy, r, std::clamp(w, 0.08f, 10.0f), false, EdaLayerId::EdgeCuts });
                }
                break;
            }
            float x1 = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float y1 = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float x2 = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            float y2 = static_cast<float>(rdI32(rec + 16)) * 1e-4f;
            if (std::fabs(x1) > 20000.0f || std::fabs(y1) > 20000.0f ||
                std::fabs(x2) > 20000.0f || std::fabs(y2) > 20000.0f) break;
            float rawW = static_cast<float>(rdU16(rec + 20)) * 2e-4f;
            float w = std::clamp(rawW > 0.01f ? rawW : 0.22f, 0.10f, 15.0f);
            EdaLayerId lid = inSymDef ? EdaLayerId::SchSymbol : mapLayer(rec[3]);
            if (inSymDef) {
                auto& sym = libSymbols[{ activeLibId, curSymId }];
                sym.localLines.push_back({ x1, y1, x2, y2, w, EdaLayerId::SchSymbol, 0 });
                sym.Expand(x1, y1);
                sym.Expand(x2, y2);
            } else if (inPkgDef) {
                auto& pkg = libPackages[{ curLibId, curPkgId }];
                pkg.localSilk.push_back({ x1, y1, x2, y2, w, lid, 0 });
                pkg.minX = std::min({ pkg.minX, x1, x2 });
                pkg.maxX = std::max({ pkg.maxX, x1, x2 });
                pkg.minY = std::min({ pkg.minY, y1, y2 });
                pkg.maxY = std::max({ pkg.maxY, y1, y2 });
            } else if (out.traces.size() < 80000) {
                out.traces.push_back({ x1, y1, x2, y2, w, lid, curNetId });
            }
            break;
        }
        case 0x24: { // Arc
            float x1 = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float y1 = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float x2 = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            float y2 = static_cast<float>(rdI32(rec + 16)) * 1e-4f;
            if (std::fabs(x1) > 20000.0f || std::fabs(y1) > 20000.0f ||
                std::fabs(x2) > 20000.0f || std::fabs(y2) > 20000.0f) break;
            float rawW = static_cast<float>(rdU16(rec + 20)) * 2e-4f;
            float w = std::clamp(rawW > 0.01f ? rawW : 0.22f, 0.10f, 10.0f);
            int16_t cRaw = static_cast<int16_t>(rdU16(rec + 22));
            float curveDeg = 120.0f;
            if (std::abs(cRaw) >= 5 && std::abs(cRaw) <= 359) curveDeg = static_cast<float>(cRaw);
            else if (std::abs(cRaw) >= 360 && std::abs(cRaw) <= 3599) curveDeg = static_cast<float>(cRaw) * 0.1f;
            EdaLayerId lid = inSymDef ? EdaLayerId::SchSymbol : mapLayer(rec[3]);
            if (inSymDef) {
                auto& sym = libSymbols[{ activeLibId, curSymId }];
                appendTessellatedArc(sym.localLines, x1, y1, x2, y2, curveDeg, w, EdaLayerId::SchSymbol, 0);
                sym.Expand(x1, y1);
                sym.Expand(x2, y2);
            } else if (inPkgDef) {
                appendTessellatedArc(libPackages[{ curLibId, curPkgId }].localSilk, x1, y1, x2, y2, curveDeg, w, lid, 0);
            } else if (out.traces.size() < 80000) {
                appendTessellatedArc(out.traces, x1, y1, x2, y2, curveDeg, w, lid, curNetId);
            }
            break;
        }
        case 0x25: // Circle (Eagle v4/v5)
        case 0x29: { // Hole
            float cx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float cy = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float r  = std::fabs(static_cast<float>(rdI32(rec + 12)) * 1e-4f);
            if (std::fabs(cx) > 20000.0f || std::fabs(cy) > 20000.0f || r <= 0.01f || r > 5000.0f) break;
            float w = 0.20f;
            if (rdU16(rec + 20) > 0) w = static_cast<float>(rdU16(rec + 20)) * 2e-4f;
            else if (rdI32(rec + 16) > 0 && rdI32(rec + 16) < 200000) w = static_cast<float>(rdI32(rec + 16)) * 1e-4f;
            w = std::clamp(w, 0.08f, 10.0f);
            if (inSymDef) {
                auto& sym = libSymbols[{ activeLibId, curSymId }];
                sym.localCircles.push_back({ cx, cy, r, w, false, EdaLayerId::SchSymbol });
                sym.Expand(cx - r, cy - r);
                sym.Expand(cx + r, cy + r);
            } else if (!inPkgDef && out.circles.size() < 20000) {
                out.circles.push_back({ cx, cy, r, w, false,
                    (op == 0x29) ? EdaLayerId::EdgeCuts : mapLayer(rec[3]) });
            }
            break;
        }
        case 0x27: { // Junction / Text in .sch, or legacy Via in .brd
            if (isSch || inSymDef) {
                float jx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
                float jy = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
                if (std::fabs(jx) > 20000.0f || std::fabs(jy) > 20000.0f) break;
                if (inSymDef) {
                    std::string txt = readEagleStr(rec + 18, 6);
                    if (!txt.empty()) {
                        float sz = std::clamp(static_cast<float>(rdU16(rec + 12)) * 2e-4f, 0.8f, 4.0f);
                        libSymbols[{ activeLibId, curSymId }].localTexts.push_back({ jx, jy, sz, rec[3], std::move(txt) });
                    }
                } else if (out.circles.size() < 20000) {
                    out.circles.push_back({ jx, jy, 0.45f, 0.25f, true, EdaLayerId::SchWire });
                }
                break;
            }
            float vx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float vy = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            if (std::fabs(vx) > 20000.0f || std::fabs(vy) > 20000.0f) break;
            float drill = 0.4f, diam = 0.8f;
            decodeDrillAndDiam(rec, drill, diam);
            if (out.vias.size() < 40000) {
                out.vias.push_back({ vx, vy, diam, drill, curNetId });
            }
            break;
        }
        case 0x28: { // Via
            float vx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float vy = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            if (std::fabs(vx) > 20000.0f || std::fabs(vy) > 20000.0f) break;
            float drill = 0.4f, diam = 0.8f;
            decodeDrillAndDiam(rec, drill, diam);
            if (out.vias.size() < 40000) {
                out.vias.push_back({ vx, vy, diam, drill, curNetId });
            }
            break;
        }
        case 0x2A: { // Thru-Hole Pad
            float px = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float py = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            if (std::fabs(px) > 20000.0f || std::fabs(py) > 20000.0f) break;
            float drill = 0.8f, diam = 1.5f;
            decodeDrillAndDiam(rec, drill, diam);
            EdaPadPrim p;
            p.x = px; p.y = py;
            p.w = diam; p.h = diam;
            p.drill = drill;
            p.shape = (rec[2] == 0) ? 1 : 0;
            p.layer = EdaLayerId::PadsThrough;
            std::string pnm = readEagleStr(rec + 19, 5);
            if (!pnm.empty()) p.pinName = PathUtils::Utf8ToWide(pnm);
            if (inPkgDef) {
                auto& pkg = libPackages[{ curLibId, curPkgId }];
                pkg.localPads.push_back(p);
                pkg.minX = std::min(pkg.minX, px - diam * 0.5f);
                pkg.maxX = std::max(pkg.maxX, px + diam * 0.5f);
                pkg.minY = std::min(pkg.minY, py - diam * 0.5f);
                pkg.maxY = std::max(pkg.maxY, py + diam * 0.5f);
            } else if (out.pads.size() < 60000) {
                out.pads.push_back(p);
            }
            break;
        }
        case 0x2B: { // SMD Pad
            float px = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float py = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            if (std::fabs(px) > 20000.0f || std::fabs(py) > 20000.0f) break;
            int32_t dx32 = rdI32(rec + 12), dy32 = rdI32(rec + 16);
            float dx = 1.0f, dy = 1.0f;
            if (rec[14] == 0 && rec[15] == 0 && dx32 > 0 && dx32 < 500000 && dy32 > 0 && dy32 < 500000) {
                dx = static_cast<float>(dx32) * 1e-4f;
                dy = static_cast<float>(dy32) * 1e-4f;
            } else {
                dx = static_cast<float>(rdU16(rec + 12)) * 2e-4f;
                dy = static_cast<float>(rdU16(rec + 14)) * 2e-4f;
            }
            dx = std::clamp(dx, 0.15f, 30.0f);
            dy = std::clamp(dy, 0.15f, 30.0f);
            EdaPadPrim p;
            p.x = px; p.y = py;
            p.w = dx; p.h = dy;
            p.shape = 1;
            p.layer = (rec[3] == 16) ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
            std::string pnm = readEagleStr(rec + 19, 5);
            if (!pnm.empty()) p.pinName = PathUtils::Utf8ToWide(pnm);
            if (inPkgDef) {
                auto& pkg = libPackages[{ curLibId, curPkgId }];
                pkg.localPads.push_back(p);
                pkg.minX = std::min(pkg.minX, px - dx * 0.5f);
                pkg.maxX = std::max(pkg.maxX, px + dx * 0.5f);
                pkg.minY = std::min(pkg.minY, py - dy * 0.5f);
                pkg.maxY = std::max(pkg.maxY, py + dy * 0.5f);
            } else if (out.pads.size() < 60000) {
                out.pads.push_back(p);
            }
            break;
        }
        case 0x2C: { // Schematic Pin (inside 0x18 Symbol, or standalone flat test stream)
            float px = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float py = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            if (std::fabs(px) > 20000.0f || std::fabs(py) > 20000.0f) break;
            std::string pnm = readEagleStr(rec + 14, 10);

            uint8_t opt = (rec[12] != 0) ? rec[12] : rec[2];
            float pinLen = 2.54f;
            int rotQuarter = 0;
            if (opt != 0) {
                uint8_t lenCode = (opt >> 4) & 0x03u;
                rotQuarter = static_cast<int>((opt >> 6) & 0x03u);
                static constexpr float kPinLens[4] = { 0.0f, 2.54f, 5.08f, 7.62f };
                pinLen = kPinLens[lenCode];
            } else if (std::fabs(px) >= std::fabs(py)) {
                rotQuarter = (px > 0.5f) ? 2 : 0;
            } else {
                rotQuarter = (py > 0.5f) ? 3 : 1;
            }
            float dirX = (rotQuarter == 0) ? 1.0f : ((rotQuarter == 2) ? -1.0f : 0.0f);
            float dirY = (rotQuarter == 1) ? 1.0f : ((rotQuarter == 3) ? -1.0f : 0.0f);
            float bx = px + dirX * pinLen;
            float by = py + dirY * pinLen;

            if (inSymDef) {
                auto& sym = libSymbols[{ activeLibId, curSymId }];
                EagleBinSymPin sp;
                sp.px = px; sp.py = py;
                sp.bx = bx; sp.by = by;
                sp.hasStem = (pinLen > 0.05f);
                sp.hasDot  = (rec[1] & 0x01u) != 0u;
                if (!pnm.empty()) sp.pinName = PathUtils::Utf8ToWide(pnm);
                sym.localPins.push_back(std::move(sp));
                sym.Expand(px, py);
                sym.Expand(bx, by);
            } else if (out.pads.size() < 40000) {
                if (pinLen > 0.05f && rec[12] != 0) {
                    out.traces.push_back({ px, py, bx, by, 0.22f, EdaLayerId::SchSymbol, 0 });
                }
                EdaPadPrim p;
                p.x = px; p.y = py; p.w = 0.8f; p.h = 0.8f;
                p.layer = EdaLayerId::SchSymbol;
                if (!pnm.empty()) p.pinName = PathUtils::Utf8ToWide(pnm);
                out.pads.push_back(p);
            }
            break;
        }
        case 0x31: { // Text (Eagle v4/v5)
            float tx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float ty = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            if (std::fabs(tx) > 20000.0f || std::fabs(ty) > 20000.0f) break;
            float sz = std::clamp(static_cast<float>(rdU16(rec + 12)) * 2e-4f, 0.8f, 5.0f);
            std::string txt = readEagleStr(rec + 18, 6);
            if (txt.empty()) txt = readEagleStr(rec + 16, 8);
            if (!txt.empty()) {
                if (inSymDef) {
                    libSymbols[{ activeLibId, curSymId }].localTexts.push_back({ tx, ty, sz, rec[3], std::move(txt) });
                } else if (!inPkgDef && out.labels.size() < 8000) {
                    out.labels.push_back({ tx, ty, sz, mapLayer(rec[3]), PathUtils::Utf8ToWide(txt) });
                }
            }
            break;
        }
        case 0x37: { // Gate inside DeviceSet (references 1-based symIdx + gate offset)
            uint16_t symIdx = rdU16(rec + 12);
            if (symIdx == 0) symIdx = rdU16(rec + 2);
            float gx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float gy = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            std::string gname = readEagleStr(rec + 14, 10);
            if (gname.empty()) gname = readEagleStr(rec + 16, 8);
            if (inDevSetDef || curDevSetId > 0) {
                libDevSets[{ activeLibId, curDevSetId }].gates.push_back({ symIdx, gx, gy, std::move(gname) });
            } else if (symIdx > 0) {
                curDevSetId = 1;
                EagleBinDevSetDef ds;
                ds.libId = activeLibId;
                ds.gates.push_back({ symIdx, gx, gy, std::move(gname) });
                libDevSets[{ activeLibId, curDevSetId }] = std::move(ds);
                globalDevSetOrder.push_back({ activeLibId, curDevSetId });
            }
            break;
        }
        case 0x30: { // PCB Element placement
            uint16_t nrChild = rdU16(rec + 2);
            uint16_t libId   = rdU16(rec + 4);
            uint16_t pkgId   = rdU16(rec + 6);
            float ex = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float ey = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            uint16_t rotFlags = rdU16(rec + 16);
            if (std::fabs(ex) > 20000.0f || std::fabs(ey) > 20000.0f) break;

            EdaComponent3D c;
            std::string inlName = readEagleStr(rec + 18, 6);
            if (inlName.empty() && !pendingElemName.empty()) {
                inlName = std::move(pendingElemName);
                pendingElemName.clear();
            }
            c.refDes = inlName.empty() ? (L"E" + std::to_wstring(out.components.size() + 1)) : PathUtils::Utf8ToWide(inlName);
            c.footprint = L"PKG_" + std::to_wstring(pkgId);
            c.x = ex;
            c.y = ey;
            c.rotDeg = decodeRotDeg(rotFlags);
            c.bottomSide = (rotFlags & 0x1000u) != 0;

            auto itPkg = libPackages.find({ libId, pkgId });
            if (itPkg != libPackages.end()) {
                const auto& pkg = itPkg->second;
                c.widthMm  = std::max(1.2f, pkg.maxX - pkg.minX);
                c.lengthMm = std::max(1.0f, pkg.maxY - pkg.minY);
                c.pinCount = std::max(2, static_cast<int>(pkg.localPads.size()));
                float rad = c.rotDeg * 3.14159265f / 180.0f;
                float cs = std::cos(rad), sn = std::sin(rad);
                auto xform = [&](float lx, float ly, float& ox, float& oy) {
                    if (c.bottomSide) lx = -lx;
                    ox = ex + (lx * cs - ly * sn);
                    oy = ey + (lx * sn + ly * cs);
                };
                for (const auto& lp : pkg.localPads) {
                    EdaPadPrim wp = lp;
                    xform(lp.x, lp.y, wp.x, wp.y);
                    wp.rotDeg += c.rotDeg;
                    if (c.bottomSide && wp.layer == EdaLayerId::TopCopper) wp.layer = EdaLayerId::BottomCopper;
                    out.pads.push_back(wp);
                }
                for (const auto& ls : pkg.localSilk) {
                    EdaTraceSeg ws = ls;
                    xform(ls.x1, ls.y1, ws.x1, ws.y1);
                    xform(ls.x2, ls.y2, ws.x2, ws.y2);
                    if (c.bottomSide && ws.layer == EdaLayerId::TopSilk) ws.layer = EdaLayerId::BottomSilk;
                    out.traces.push_back(ws);
                }
            }

            c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
            lastCompIdx = static_cast<int>(out.components.size());
            elemAttrEndBlock = bi + std::max<uint32_t>(2u, nrChild);
            out.components.push_back(c);
            anyElementPlaced = true;
            break;
        }
        case 0x33: { // Element2 / Part2 (name[8] + value[14])
            std::string nm = readEagleStr(rec + 2, 8);
            std::string vl = readEagleStr(rec + 10, 14);
            if (lastSchPartIdx >= 0 && lastSchPartIdx < static_cast<int>(schParts.size()) && bi <= schPartAttrEndBlock) {
                auto& sp = schParts[lastSchPartIdx];
                if (!nm.empty()) sp.name = nm;
                if (!vl.empty()) sp.value = vl;
            } else if (lastCompIdx >= 0 && lastCompIdx < static_cast<int>(out.components.size())) {
                auto& c = out.components[lastCompIdx];
                if (!nm.empty()) c.refDes = PathUtils::Utf8ToWide(nm);
                if (!vl.empty()) c.value  = PathUtils::Utf8ToWide(vl);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
            }
            break;
        }
        case 0x34: { // Attribute
            std::string s1 = readEagleStr(rec + 8, 8);
            std::string s2 = readEagleStr(rec + 16, 8);
            if (lastSchPartIdx >= 0 && lastSchPartIdx < static_cast<int>(schParts.size()) && bi <= schPartAttrEndBlock + 2) {
                auto& sp = schParts[lastSchPartIdx];
                if (!s1.empty() && sp.name.empty()) sp.name = s1;
                else if (!s2.empty() && sp.value.empty()) sp.value = s2;
                else if (!s1.empty() && sp.value.empty()) sp.value = s1;
            } else if (lastCompIdx >= 0 && lastCompIdx < static_cast<int>(out.components.size()) && bi <= elemAttrEndBlock + 2) {
                auto& c = out.components[lastCompIdx];
                if (!s1.empty() && c.refDes.rfind(L"E", 0) == 0) c.refDes = PathUtils::Utf8ToWide(s1);
                else if (!s2.empty() && c.value.empty()) c.value = PathUtils::Utf8ToWide(s2);
                else if (!s1.empty() && c.value.empty()) c.value = PathUtils::Utf8ToWide(s1);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
            }
            break;
        }
        case 0x38: { // Schematic Part (Eagle v4/v5) or direct Schematic Instance
            uint16_t nrChild = rdU16(rec + 2);
            EagleBinSchPart sp;
            sp.u16_4 = rdU16(rec + 4);
            sp.u16_6 = rdU16(rec + 6);
            sp.ix = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            sp.iy = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            sp.rotFlags = rdU16(rec + 16);

            // Extract inline name/value: either +18[6] (direct placement) or +12[5]/+17[7] (Eagle v5 Part table)
            std::string nm18 = readEagleStr(rec + 18, 6);
            std::string nm12 = (rec[12] == 0x7F || (rec[12] >= 33 && rec[12] < 127)) ? readEagleStr(rec + 12, 5) : std::string{};
            std::string vl17 = (rec[17] == 0x7F || (rec[17] >= 33 && rec[17] < 127)) ? readEagleStr(rec + 17, 7) : std::string{};
            if (!nm12.empty() && std::fabs(sp.iy) > 5000.0f) {
                sp.name = nm12;
                sp.value = vl17;
            } else {
                sp.name = nm18.empty() ? nm12 : nm18;
            }
            if (sp.name.empty() && schPartNames.count(sp.u16_4)) sp.name = schPartNames[sp.u16_4];
            if (sp.name.empty() && !pendingElemName.empty()) {
                sp.name = std::move(pendingElemName);
                pendingElemName.clear();
            }

            lastSchPartIdx = static_cast<int>(schParts.size());
            schPartAttrEndBlock = bi + std::max<uint32_t>(2u, nrChild);
            schParts.push_back(std::move(sp));
            break;
        }
        case 0x39:
        case 0x3C: { // Schematic Gate Instance on Sheet
            if (!isSch && schParts.empty()) break;
            uint16_t partIdx = rdU16(rec + 4);
            uint16_t gateIdx = std::max<uint16_t>(1u, rdU16(rec + 6));
            float ix = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float iy = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            uint16_t rotFlags = rdU16(rec + 16);
            if (rotFlags == 0) rotFlags = rdU16(rec + 18);
            if (partIdx >= 1u && partIdx <= schParts.size() &&
                std::fabs(ix) <= 20000.0f && std::fabs(iy) <= 20000.0f)
            {
                schParts[partIdx - 1u].instantiatedByGate = true;
                schGateInsts.push_back({ partIdx, gateIdx, ix, iy, rotFlags });
            }
            break;
        }
        default:
            break;
        }
    }

    // Resolve and instantiate placed Schematic Parts & Gate Instances
    auto resolveSymByLibAndIdx = [&](uint16_t libId, uint16_t symIdx) -> const EagleBinSymDef* {
        if (symIdx == 0) return nullptr;
        if (libId > 0) {
            auto it = libSymbols.find({ libId, symIdx });
            if (it != libSymbols.end()) return &it->second;
        }
        auto it1 = libSymbols.find({ 1u, symIdx });
        if (it1 != libSymbols.end()) return &it1->second;
        if (symIdx <= globalSymOrder.size()) {
            auto itG = libSymbols.find(globalSymOrder[symIdx - 1u]);
            if (itG != libSymbols.end()) return &itG->second;
        }
        return nullptr;
    };

    auto resolveDevSet = [&](uint16_t libId, uint16_t dsIdx) -> const EagleBinDevSetDef* {
        if (dsIdx == 0) return nullptr;
        if (libId > 0) {
            auto it = libDevSets.find({ libId, dsIdx });
            if (it != libDevSets.end()) return &it->second;
        }
        if (dsIdx <= globalDevSetOrder.size()) {
            auto itG = libDevSets.find(globalDevSetOrder[dsIdx - 1u]);
            if (itG != libDevSets.end()) return &itG->second;
        }
        return nullptr;
    };

    auto instantiateSchPlacement = [&](const EagleBinSchPart& sp, uint16_t gateIdx, float ix, float iy, uint16_t rotFlags, bool applyGateOffset) {
        if (std::fabs(ix) > 20000.0f || std::fabs(iy) > 20000.0f) return;

        const EagleBinSymDef* sym = nullptr;
        float gateOffX = 0.0f, gateOffY = 0.0f;
        std::string dsName;

        const EagleBinDevSetDef* ds = (sp.u16_6 > 0) ? resolveDevSet(sp.u16_4, sp.u16_6) : resolveDevSet(1u, sp.u16_4);
        if (ds && !ds->gates.empty()) {
            dsName = ds->name;
            size_t gi = (gateIdx >= 1u && gateIdx <= ds->gates.size()) ? (gateIdx - 1u) : 0u;
            sym = resolveSymByLibAndIdx(ds->libId, ds->gates[gi].symIdx);
            if (applyGateOffset) {
                gateOffX = ds->gates[gi].gx;
                gateOffY = ds->gates[gi].gy;
            }
        }
        if (!sym) {
            if (sp.u16_6 > 0) sym = resolveSymByLibAndIdx(sp.u16_4, sp.u16_6);
            else sym = resolveSymByLibAndIdx(1u, sp.u16_4);
        }

        const bool mirror = (rotFlags & 0x1000u) != 0u;
        const float rotDeg = decodeRotDeg(rotFlags);
        const float rad = rotDeg * 3.14159265f / 180.0f;
        const float cs = std::cos(rad), sn = std::sin(rad);

        auto xform = [&](float lx, float ly, float& ox, float& oy) {
            float rx = lx + gateOffX;
            float ry = ly + gateOffY;
            if (mirror) rx = -rx;
            ox = ix + (rx * cs - ry * sn);
            oy = iy + (rx * sn + ry * cs);
        };

        EdaComponent3D c;
        c.refDes = sp.name.empty() ? (L"U" + std::to_wstring(out.components.size() + 1u)) : PathUtils::Utf8ToWide(sp.name);
        c.value  = PathUtils::Utf8ToWide(sp.value);
        c.footprint = !dsName.empty() ? PathUtils::Utf8ToWide(dsName)
                    : ((sym && !sym->name.empty()) ? PathUtils::Utf8ToWide(sym->name) : L"SCH_INST");
        c.x = ix;
        c.y = iy;
        c.rotDeg = rotDeg;
        c.bottomSide = mirror;

        if (sym && (!sym->localLines.empty() || !sym->localCircles.empty() || !sym->localPins.empty())) {
            float wMinX = 1e9f, wMinY = 1e9f, wMaxX = -1e9f, wMaxY = -1e9f;
            auto expW = [&](float wx, float wy) {
                wMinX = std::min(wMinX, wx); wMaxX = std::max(wMaxX, wx);
                wMinY = std::min(wMinY, wy); wMaxY = std::max(wMaxY, wy);
            };

            for (const auto& ln : sym->localLines) {
                EdaTraceSeg ws = ln;
                xform(ln.x1, ln.y1, ws.x1, ws.y1);
                xform(ln.x2, ln.y2, ws.x2, ws.y2);
                ws.layer = EdaLayerId::SchSymbol;
                expW(ws.x1, ws.y1);
                expW(ws.x2, ws.y2);
                if (out.traces.size() < 80000) out.traces.push_back(ws);
            }
            for (const auto& cr : sym->localCircles) {
                EdaCirclePrim wc = cr;
                xform(cr.cx, cr.cy, wc.cx, wc.cy);
                wc.layer = EdaLayerId::SchSymbol;
                expW(wc.cx - wc.radius, wc.cy - wc.radius);
                expW(wc.cx + wc.radius, wc.cy + wc.radius);
                if (out.circles.size() < 20000) out.circles.push_back(wc);
            }
            for (const auto& lp : sym->localPins) {
                float wx = 0.0f, wy = 0.0f, wbx = 0.0f, wby = 0.0f;
                xform(lp.px, lp.py, wx, wy);
                xform(lp.bx, lp.by, wbx, wby);
                expW(wx, wy);
                expW(wbx, wby);
                if (lp.hasStem && out.traces.size() < 80000) {
                    out.traces.push_back({ wx, wy, wbx, wby, 0.22f, EdaLayerId::SchSymbol, 0 });
                }
                if (lp.hasDot && out.circles.size() < 20000) {
                    out.circles.push_back({ wbx, wby, 0.55f, 0.20f, false, EdaLayerId::SchSymbol });
                }
                if (out.pads.size() < 60000) {
                    EdaPadPrim wp;
                    wp.x = wx;
                    wp.y = wy;
                    wp.w = 0.65f;
                    wp.h = 0.65f;
                    wp.rotDeg = rotDeg;
                    wp.shape = 0;
                    wp.layer = EdaLayerId::SchSymbol;
                    wp.pinName = lp.pinName;
                    out.pads.push_back(std::move(wp));
                }
                if (!lp.pinName.empty() && out.labels.size() < 8000) {
                    out.labels.push_back({ wbx + 0.35f, wby + 0.35f, 0.85f, EdaLayerId::SchLabel, lp.pinName });
                }
            }

            bool emittedName = false;
            for (const auto& lt : sym->localTexts) {
                float wtx = 0.0f, wty = 0.0f;
                xform(lt.tx, lt.ty, wtx, wty);
                std::string upperT = lt.text;
                for (char& ch : upperT) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
                if (upperT == ">NAME") {
                    out.labels.push_back({ wtx, wty, lt.size, EdaLayerId::SchLabel, c.refDes });
                    emittedName = true;
                } else if (upperT == ">VALUE") {
                    if (!c.value.empty()) out.labels.push_back({ wtx, wty, lt.size, EdaLayerId::SchLabel, c.value });
                } else if (out.labels.size() < 8000) {
                    out.labels.push_back({ wtx, wty, lt.size, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(lt.text) });
                }
            }

            if (wMinX <= wMaxX && wMinY <= wMaxY) {
                c.widthMm  = std::clamp(wMaxX - wMinX, 1.5f, 60.0f);
                c.lengthMm = std::clamp(wMaxY - wMinY, 1.5f, 60.0f);
            }
            c.pinCount = std::max(1, static_cast<int>(sym->localPins.size()));
            if (!emittedName && out.labels.size() < 8000) {
                std::wstring lbl = c.refDes + (c.value.empty() ? L"" : (L":" + c.value));
                out.labels.push_back({ ix - c.widthMm * 0.3f, iy + c.lengthMm * 0.5f + 1.0f, 1.05f, EdaLayerId::SchLabel, lbl });
            }
        } else {
            // Backward compatibility fallback ONLY when zero 0x18 symbols exist or symbol index is out of range
            c.widthMm = 6.0f;
            c.lengthMm = 4.0f;
            float x0 = ix - 3.0f, x1 = ix + 3.0f, y0 = iy - 2.0f, y1 = iy + 2.0f;
            out.traces.push_back({ x0, y0, x1, y0, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y0, x1, y1, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y1, x0, y1, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x0, y1, x0, y0, 0.22f, EdaLayerId::SchSymbol, 0 });
        }

        c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
        out.components.push_back(std::move(c));
        anyElementPlaced = true;
    };

    for (const auto& gi : schGateInsts) {
        instantiateSchPlacement(schParts[gi.partIdx - 1u], gi.gateIdx, gi.ix, gi.iy, gi.rotFlags, false);
    }
    for (const auto& sp : schParts) {
        if (!sp.instantiatedByGate) {
            const EagleBinDevSetDef* ds = (sp.u16_6 > 0) ? resolveDevSet(sp.u16_4, sp.u16_6) : resolveDevSet(1u, sp.u16_4);
            if (ds && ds->gates.size() > 1u) {
                for (size_t g = 0; g < ds->gates.size(); ++g) {
                    instantiateSchPlacement(sp, static_cast<uint16_t>(g + 1u), sp.ix, sp.iy, sp.rotFlags, true);
                }
            } else {
                instantiateSchPlacement(sp, 1u, sp.ix, sp.iy, sp.rotFlags, true);
            }
        }
    }

    if (!anyElementPlaced && !libPackages.empty()) {
        float offX = 0.0f;
        for (const auto& kv : libPackages) {
            const auto& pkg = kv.second;
            for (auto p : pkg.localPads) { p.x += offX; out.pads.push_back(p); }
            for (auto s : pkg.localSilk) { s.x1 += offX; s.x2 += offX; out.traces.push_back(s); }
            offX += std::max(8.0f, (pkg.maxX - pkg.minX) + 5.0f);
        }
    } else if (!anyElementPlaced && !libSymbols.empty()) {
        float offX = 0.0f;
        for (const auto& kv : libSymbols) {
            EagleBinSchPart dummy;
            dummy.u16_4 = kv.first.first;
            dummy.u16_6 = kv.first.second;
            dummy.name  = kv.second.name;
            instantiateSchPlacement(dummy, 1u, offX, 0.0f, 0u, false);
            float span = (kv.second.minX <= kv.second.maxX) ? (kv.second.maxX - kv.second.minX) : 10.0f;
            offX += std::max(14.0f, span + 8.0f);
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.circles.empty() && out.components.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, signalCount > 0 ? signalCount : out.components.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}
