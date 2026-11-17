// EdaReaders.cpp - the board / schematic readers behind EdaViewer: src/formats/*.h, dispatched by extension.
// Moved out of EdaViewer.cpp so that they build without the viewer, also on Linux (see linux/).
#include "EdaReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <unordered_map>

namespace Explorer2 {

namespace {

static EdaPartCategory ClassifyComponent(const std::wstring& refDes, const std::wstring& fp, float& outW, float& outL, float& outH, int& outPins) {
    std::wstring r = PathUtils::ToUpper(refDes);
    std::wstring f = PathUtils::ToUpper(fp);

    // Default dimensions based on common footprint names
    if (f.find(L"0402") != std::wstring::npos) { outW = 1.0f; outL = 0.5f; outH = 0.45f; }
    else if (f.find(L"0603") != std::wstring::npos) { outW = 1.6f; outL = 0.85f; outH = 0.55f; }
    else if (f.find(L"0805") != std::wstring::npos) { outW = 2.0f; outL = 1.25f; outH = 0.65f; }
    else if (f.find(L"1206") != std::wstring::npos) { outW = 3.2f; outL = 1.6f; outH = 0.75f; }
    else if (f.find(L"SOT-23") != std::wstring::npos || f.find(L"SOT23") != std::wstring::npos) {
        outW = 2.9f; outL = 1.6f; outH = 1.1f; outPins = std::max(outPins, 3);
        return EdaPartCategory::TransistorSOT;
    } else if (f.find(L"SOIC") != std::wstring::npos || f.find(L"SOP") != std::wstring::npos ||
               f.find(L"TSSOP") != std::wstring::npos || f.find(L"MSOP") != std::wstring::npos) {
        outW = std::max(outW, 5.0f); outL = std::max(outL, 4.0f); outH = 1.55f;
        outPins = std::max(outPins, 8);
        return EdaPartCategory::IC_Package;
    } else if (f.find(L"QFP") != std::wstring::npos || f.find(L"QFN") != std::wstring::npos ||
               f.find(L"BGA") != std::wstring::npos || f.find(L"DIP") != std::wstring::npos) {
        outW = std::max(outW, 7.0f); outL = std::max(outL, 7.0f); outH = 1.6f;
        outPins = std::max(outPins, 16);
        return EdaPartCategory::IC_Package;
    }

    if (!r.empty()) {
        if (r[0] == L'U' || (r.size() >= 2 && r[0] == L'I' && r[1] == L'C')) {
            outW = std::max(outW, 4.8f); outL = std::max(outL, 3.8f); outH = std::max(outH, 1.5f);
            outPins = std::max(outPins, 8);
            return EdaPartCategory::IC_Package;
        }
        if (r[0] == L'R') {
            return EdaPartCategory::ResistorSMD;
        }
        if (r[0] == L'C') {
            if (f.find(L"ELEC") != std::wstring::npos || f.find(L"CP_") != std::wstring::npos ||
                f.find(L"RADIAL") != std::wstring::npos || outW >= 5.0f) {
                outW = std::max(outW, 5.0f); outL = std::max(outL, 5.0f); outH = 5.5f;
                return EdaPartCategory::ElectrolyticCap;
            }
            return EdaPartCategory::CapacitorSMD;
        }
        if (r[0] == L'J' || (r.size() >= 2 && r[0] == L'C' && r[1] == L'N') || r[0] == L'P') {
            outW = std::max(outW, 5.0f); outL = std::max(outL, 2.54f); outH = 4.2f;
            return EdaPartCategory::Connector;
        }
        if (r[0] == L'Q' || r[0] == L'T') {
            outW = std::max(outW, 2.8f); outL = std::max(outL, 1.6f); outH = 1.1f;
            return EdaPartCategory::TransistorSOT;
        }
        if (r[0] == L'D' || r.rfind(L"LED", 0) == 0) {
            return EdaPartCategory::DiodeLED;
        }
        if (r[0] == L'L' || r.rfind(L"FB", 0) == 0) {
            outH = std::max(outH, 1.8f);
            return EdaPartCategory::Inductor;
        }
        if (r[0] == L'Y' || r[0] == L'X') {
            outW = std::max(outW, 5.0f); outL = std::max(outL, 3.2f); outH = 1.8f;
            return EdaPartCategory::Crystal;
        }
        if (r[0] == L'H' || r.rfind(L"MH", 0) == 0) {
            return EdaPartCategory::MountingHole;
        }
    }
    return EdaPartCategory::GenericSMD;
}

static void FinalizeBoardBounds(ParsedEdaBoard& b) {
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    bool hasEdgeCuts = false;

    auto expand = [&](float x, float y) {
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    };

    for (const auto& t : b.traces) {
        if (t.layer == EdaLayerId::EdgeCuts) {
            hasEdgeCuts = true;
            expand(t.x1, t.y1);
            expand(t.x2, t.y2);
        }
    }

    if (!hasEdgeCuts) {
        for (const auto& t : b.traces) { expand(t.x1, t.y1); expand(t.x2, t.y2); }
        for (const auto& p : b.pads)   { expand(p.x - p.w, p.y - p.h); expand(p.x + p.w, p.y + p.h); }
        for (const auto& v : b.vias)   { expand(v.x - v.diameter, v.y - v.diameter); expand(v.x + v.diameter, v.y + v.diameter); }
        for (const auto& c : b.components) {
            expand(c.x - c.widthMm, c.y - c.lengthMm);
            expand(c.x + c.widthMm, c.y + c.lengthMm);
        }
        for (const auto& cr : b.circles) {
            expand(cr.cx - cr.radius, cr.cy - cr.radius);
            expand(cr.cx + cr.radius, cr.cy + cr.radius);
        }
    }

    if (minX > maxX || minY > maxY) {
        b.minX = -30.0f; b.minY = -20.0f;
        b.maxX = 30.0f;  b.maxY = 20.0f;
    } else {
        float pad = hasEdgeCuts ? 0.0f : std::max(2.0f, std::min(maxX - minX, maxY - minY) * 0.06f);
        b.minX = minX - pad;
        b.minY = minY - pad;
        b.maxX = maxX + pad;
        b.maxY = maxY + pad;
        if (b.maxX - b.minX < 2.0f) { b.minX -= 5.0f; b.maxX += 5.0f; }
        if (b.maxY - b.minY < 2.0f) { b.minY -= 5.0f; b.maxY += 5.0f; }
    }
}

// Helper for XML attribute extraction in Eagle .brd / .sch files
static bool GetXmlAttr(std::string_view tag, std::string_view attrName, std::string_view& outVal) {
    return XmlTagScan::FindAttr(tag, attrName, outVal, /*byLocalName=*/false);
}

static float GetXmlFloat(std::string_view tag, std::string_view attrName, float defVal = 0.0f) {
    std::string_view v;
    if (!GetXmlAttr(tag, attrName, v) || v.empty()) return defVal;
    char buf[64] = {};
    size_t n = std::min<size_t>(v.size(), 63);
    std::memcpy(buf, v.data(), n);
    return std::strtof(buf, nullptr);
}

static int GetXmlInt(std::string_view tag, std::string_view attrName, int defVal = 0) {
    std::string_view v;
    if (!GetXmlAttr(tag, attrName, v) || v.empty()) return defVal;
    char buf[32] = {};
    size_t n = std::min<size_t>(v.size(), 31);
    std::memcpy(buf, v.data(), n);
    return std::atoi(buf);
}

static EdaLayerId MapEagleLayer(int eagleLayer, bool isSch) {
    if (isSch) {
        if (eagleLayer == 91 || eagleLayer == 92) return EdaLayerId::SchWire;
        if (eagleLayer == 94) return EdaLayerId::SchSymbol;
        return EdaLayerId::SchLabel;
    }
    switch (eagleLayer) {
    case 1:  return EdaLayerId::TopCopper;
    case 16: return EdaLayerId::BottomCopper;
    case 20: return EdaLayerId::EdgeCuts;
    case 21: case 25: case 27: case 51: return EdaLayerId::TopSilk;
    case 22: case 26: case 28: case 52: return EdaLayerId::BottomSilk;
    case 17: case 18: return EdaLayerId::PadsThrough;
    default:
        if (eagleLayer >= 2 && eagleLayer <= 15) return EdaLayerId::InnerCopper;
        return EdaLayerId::TopSilk;
    }
}

struct EaglePkgDef {
    std::vector<EdaPadPrim> localPads;
    std::vector<EdaTraceSeg> localSilk;
    float minX = -1.5f, minY = -1.0f, maxX = 1.5f, maxY = 1.0f;
};

#include "formats/EagleXml.h"
#include "formats/KiCadSExpr.h"
#include "formats/GerberDrill.h"
#include "formats/KiCadLegacySch.h"
#include "formats/AutoCadDxf.h"

static bool ParseEagleBinaryV4V5(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (!data || size < 48 || data[0] != 0x10) return false;

    auto rdU16 = [](const uint8_t* p) -> uint16_t {
        return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
    };
    auto rdU32 = [](const uint8_t* p) -> uint32_t {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    };
    auto rdI32 = [&](const uint8_t* p) -> int32_t {
        return static_cast<int32_t>(rdU32(p));
    };

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

    std::map<std::pair<uint16_t, uint16_t>, EaglePkgDef> libPackages;
    std::unordered_map<uint16_t, std::string> schPartNames;
    uint16_t curLibId = 0, curPkgId = 0, curPartId = 0;
    uint32_t libEndBlock = 0, pkgEndBlock = 0;
    int curNetId = 0;
    size_t signalCount = 0;
    std::string pendingElemName;
    int lastCompIdx = -1;
    uint32_t elemAttrEndBlock = 0;
    bool anyElementPlaced = false;

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

    for (uint32_t bi = 1; bi < numBlocks; ++bi) {
        const uint8_t* rec = data + static_cast<size_t>(bi) * 24;
        const uint8_t op = rec[0];
        const bool inPkgDef = (bi < pkgEndBlock && curLibId > 0 && curPkgId > 0);

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
            if (totalSub > 0 && bi + 1 + totalSub <= numBlocks) {
                ++curLibId;
                curPkgId = 0;
                libEndBlock = bi + 1 + static_cast<uint32_t>(totalSub);
            }
            break;
        }
        case 0x17: { // Element / DeviceSet name record
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
        case 0x1C:
        case 0x1E: { // Signal / Net
            ++signalCount;
            curNetId = static_cast<int>(signalCount);
            break;
        }
        case 0x1D: { // Schematic Part
            ++curPartId;
            std::string pname = readEagleStr(rec + 16, 8);
            if (pname.empty()) pname = readEagleStr(rec + 8, 16);
            if (!pname.empty()) schPartNames[curPartId] = pname;
            break;
        }
        case 0x1F: { // Wire / Track
            float x1 = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float y1 = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float x2 = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            float y2 = static_cast<float>(rdI32(rec + 16)) * 1e-4f;
            if (std::fabs(x1) > 20000.0f || std::fabs(y1) > 20000.0f ||
                std::fabs(x2) > 20000.0f || std::fabs(y2) > 20000.0f) break;
            float w = std::clamp(static_cast<float>(rdU16(rec + 20)) * 2e-4f, 0.08f, 15.0f);
            EdaLayerId lid = mapLayer(rec[3]);
            if (inPkgDef) {
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
        case 0x20: { // Rectangle
            float x1 = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float y1 = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float x2 = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            float y2 = static_cast<float>(rdI32(rec + 16)) * 1e-4f;
            if (std::fabs(x1) > 20000.0f || std::fabs(x2) > 20000.0f) break;
            EdaLayerId lid = mapLayer(rec[3]);
            auto pushRect = [&](std::vector<EdaTraceSeg>& dst) {
                dst.push_back({ x1, y1, x2, y1, 0.20f, lid, 0 });
                dst.push_back({ x2, y1, x2, y2, 0.20f, lid, 0 });
                dst.push_back({ x2, y2, x1, y2, 0.20f, lid, 0 });
                dst.push_back({ x1, y2, x1, y1, 0.20f, lid, 0 });
            };
            if (inPkgDef) pushRect(libPackages[{ curLibId, curPkgId }].localSilk);
            else if (out.traces.size() < 80000) pushRect(out.traces);
            break;
        }
        case 0x21: { // Text
            float tx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float ty = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float sz = std::clamp(static_cast<float>(rdU16(rec + 12)) * 2e-4f, 0.8f, 5.0f);
            std::string txt = readEagleStr(rec + 18, 6);
            if (!txt.empty() && !inPkgDef && out.labels.size() < 8000) {
                out.labels.push_back({ tx, ty, sz, mapLayer(rec[3]), PathUtils::Utf8ToWide(txt) });
            }
            break;
        }
        case 0x22: // Circle
        case 0x29: { // Hole
            float cx = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float cy = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float r  = std::fabs(static_cast<float>(rdI32(rec + 12)) * 1e-4f);
            if (std::fabs(cx) > 20000.0f || std::fabs(cy) > 20000.0f || r <= 0.01f || r > 5000.0f) break;
            float w = 0.20f;
            if (rdU16(rec + 20) > 0) w = static_cast<float>(rdU16(rec + 20)) * 2e-4f;
            else if (rdI32(rec + 16) > 0 && rdI32(rec + 16) < 200000) w = static_cast<float>(rdI32(rec + 16)) * 1e-4f;
            if (!inPkgDef && out.circles.size() < 20000) {
                out.circles.push_back({ cx, cy, r, std::clamp(w, 0.08f, 10.0f), false,
                    (op == 0x29) ? EdaLayerId::EdgeCuts : mapLayer(rec[3]) });
            }
            break;
        }
        case 0x27:
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
        case 0x2C: { // Schematic Pin
            float px = static_cast<float>(rdI32(rec + 4)) * 1e-4f;
            float py = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            if (std::fabs(px) <= 20000.0f && std::fabs(py) <= 20000.0f && out.pads.size() < 40000) {
                EdaPadPrim p;
                p.x = px; p.y = py; p.w = 0.8f; p.h = 0.8f;
                p.layer = EdaLayerId::SchSymbol;
                std::string pnm = readEagleStr(rec + 14, 10);
                if (!pnm.empty()) p.pinName = PathUtils::Utf8ToWide(pnm);
                out.pads.push_back(p);
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
        case 0x33: { // Element2 (name[8] + value[14])
            if (lastCompIdx >= 0 && lastCompIdx < static_cast<int>(out.components.size())) {
                std::string nm = readEagleStr(rec + 2, 8);
                std::string vl = readEagleStr(rec + 10, 14);
                auto& c = out.components[lastCompIdx];
                if (!nm.empty()) c.refDes = PathUtils::Utf8ToWide(nm);
                if (!vl.empty()) c.value  = PathUtils::Utf8ToWide(vl);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
            }
            break;
        }
        case 0x34: { // Attribute
            if (lastCompIdx >= 0 && lastCompIdx < static_cast<int>(out.components.size()) && bi <= elemAttrEndBlock + 2) {
                std::string s1 = readEagleStr(rec + 8, 8);
                std::string s2 = readEagleStr(rec + 16, 8);
                auto& c = out.components[lastCompIdx];
                if (!s1.empty() && c.refDes.rfind(L"E", 0) == 0) c.refDes = PathUtils::Utf8ToWide(s1);
                else if (!s2.empty() && c.value.empty()) c.value = PathUtils::Utf8ToWide(s2);
                else if (!s1.empty() && c.value.empty()) c.value = PathUtils::Utf8ToWide(s1);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
            }
            break;
        }
        case 0x38: { // Schematic Instance
            uint16_t nrChild = rdU16(rec + 2);
            uint16_t partId  = rdU16(rec + 4);
            float ix = static_cast<float>(rdI32(rec + 8)) * 1e-4f;
            float iy = static_cast<float>(rdI32(rec + 12)) * 1e-4f;
            uint16_t rotFlags = rdU16(rec + 16);
            if (std::fabs(ix) > 20000.0f || std::fabs(iy) > 20000.0f) break;

            EdaComponent3D c;
            std::string nm = readEagleStr(rec + 18, 6);
            if (nm.empty() && schPartNames.count(partId)) nm = schPartNames[partId];
            if (nm.empty() && !pendingElemName.empty()) { nm = std::move(pendingElemName); pendingElemName.clear(); }
            c.refDes = nm.empty() ? (L"U" + std::to_wstring(out.components.size() + 1)) : PathUtils::Utf8ToWide(nm);
            c.footprint = L"SCH_INST";
            c.x = ix;
            c.y = iy;
            c.rotDeg = decodeRotDeg(rotFlags);
            c.widthMm = 6.0f;
            c.lengthMm = 4.0f;
            c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
            lastCompIdx = static_cast<int>(out.components.size());
            elemAttrEndBlock = bi + std::max<uint32_t>(2u, nrChild);
            out.components.push_back(c);

            float x0 = ix - 3.0f, x1 = ix + 3.0f, y0 = iy - 2.0f, y1 = iy + 2.0f;
            out.traces.push_back({ x0, y0, x1, y0, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y0, x1, y1, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y1, x0, y1, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x0, y1, x0, y0, 0.22f, EdaLayerId::SchSymbol, 0 });
            break;
        }
        default:
            break;
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
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.circles.empty() && out.components.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, signalCount > 0 ? signalCount : out.components.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace

#include "formats/LtspiceAsc.h"
#include "formats/SpiceRawWave.h"
#include "formats/SpecctraDsn.h"
#include "formats/IpcD356.h"
#include "formats/IntelHex.h"
#include "formats/MotorolaSrec.h"
#include "formats/SpiceNetlist.h"
#include "formats/HpglPlot.h"
#include "formats/VerilogVcd.h"
#include "formats/TouchstoneSnp.h"
#include "formats/AsicLef.h"
#include "formats/AsicDef.h"
#include "formats/GdsiiStream.h"
#include "formats/JtagBsdl.h"
#include "formats/CmsisSvd.h"
#include "formats/ExtendedEda100.h"
#include "formats/ExtendedEda200.h"
#include "formats/EasyEda.h"
#include "formats/GateNetlistTdms.h"
#include "formats/Ipc2581OdbFritzing.h"

namespace EdaReaders {

bool IsEdaExtension(const std::wstring& extLower) {
    return extLower == L".kicad_pcb" || extLower == L".kicad_sch" || extLower == L".kicad_mod" ||
           extLower == L".brd"       || extLower == L".sch"       || extLower == L".lbr"       ||
           extLower == L".gbr"       || extLower == L".ger"       || extLower == L".gtl"       ||
           extLower == L".gbl"       || extLower == L".gts"       || extLower == L".gbs"       ||
           extLower == L".gto"       || extLower == L".gbo"       || extLower == L".gm1"       ||
           extLower == L".gko"       || extLower == L".gtp"       || extLower == L".gbp"       ||
           extLower == L".g2"        || extLower == L".g3"        || extLower == L".gp1"       ||
           extLower == L".gp2"       || extLower == L".oln"       || extLower == L".gbrjob"    ||
           extLower == L".drl"       || extLower == L".xln"       || extLower == L".exc"       ||
           extLower == L".dxf"       || extLower == L".asc"       || extLower == L".asy"       ||
           extLower == L".dsn"       || extLower == L".olb"       ||
           extLower == L".d356"      || extLower == L".ipc"       || extLower == L".hex"       ||
           extLower == L".ihex"      || extLower == L".srec"      || extLower == L".s19"       ||
           extLower == L".mot"       || extLower == L".cir"       || extLower == L".spice"     ||
           extLower == L".sp"        || extLower == L".net"       || extLower == L".hpgl"      ||
           extLower == L".plt"       || extLower == L".vcd"       || extLower == L".s1p"       ||
           extLower == L".s2p"       || extLower == L".lef"       || extLower == L".def"       ||
           extLower == L".gds"       || extLower == L".gds2"      || extLower == L".bsdl"      || extLower == L".bsd" ||
           extLower == L".svd"       || extLower == L".odb"       || extLower == L".ipc2581"   ||
           extLower == L".cvg"       || extLower == L".pcbdoc"    || extLower == L".schdoc"    ||
           extLower == L".pcblib"    || extLower == L".schlib"    ||
           extLower == L".blf"       || extLower == L".can"       || extLower == L".dbc"       ||
           extLower == L".fst"       || extLower == L".ghw"       || extLower == L".bit"       ||
           extLower == L".rpd"       || extLower == L".dfu"       || extLower == L".ufd"       ||
           extLower == L".edid"      || extLower == L".spd"       ||
           extLower == L".kicad_sym" || extLower == L".ses"       || extLower == L".pos"       ||
           extLower == L".cpl"       || extLower == L".mnt"       || extLower == L".ibs"       ||
           extLower == L".s3p"       || extLower == L".s4p"       || extLower == L".s8p"       ||
           extLower == L".oas"       || extLower == L".xdc"       || extLower == L".sdc"       ||
           extLower == L".qsf"       || extLower == L".jed"       || extLower == L".svf"       ||
           extLower == L".xsvf"      || extLower == L".sof"       || extLower == L".pof"       ||
           extLower == L".sr"        || extLower == L".sal"       || extLower == L".wfm"       ||
           extLower == L".isf"       || extLower == L".mf4"       || extLower == L".mdf"       ||
           extLower == L".ldf"       || extLower == L".arxml"     || extLower == L".eds"       ||
           extLower == L".dcf"       || extLower == L".gsdml"     || extLower == L".l5x"       ||
           extLower == L".raw"       || extLower == L".tr0"       || extLower == L".ac0"       || extLower == L".sw0"  ||
           extLower == L".epro"      || extLower == L".eprj"      || extLower == L".epcb"      || extLower == L".esch" ||
           extLower == L".esym"      || extLower == L".efoo"      || extLower == L".json"      ||
           extLower == L".edif"      || extLower == L".edn"       || extLower == L".blif"      || extLower == L".eblif"||
           extLower == L".aig"       || extLower == L".aag"       || extLower == L".lib"       || extLower == L".sdf"  ||
           extLower == L".tdms"      || extLower == L".tgz"       || extLower == L".fzz"       || extLower == L".fzpz" ||
           extLower == L".fz"        || extLower == L".fzp"       || extLower == L".pcb";
}

bool IsLikelyOdbPlusPlusTgz(const std::wstring& fullPath) {
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, 8 * 1024 * 1024, raw) || raw.size() < 16) return false;
    return Formats::IsLikelyOdbPlusPlusArchiveBytes(raw.data(), raw.size());
}

bool IsLikelyLibertyOrEdaSdfFile(const std::wstring& fullPath, const std::wstring& extLower) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 8192, hdr) || hdr.size() < 16) return false;
    if (extLower == L".lib") {
        return Formats::IsLikelyLibertyLibBytes(hdr.data(), hdr.size()) ||
               IsLikelyKiCadLegacySymLibBytes(hdr.data(), hdr.size());
    }
    if (extLower == L".sdf") return Formats::IsLikelyIeeeSdfBytes(hdr.data(), hdr.size());
    return false;
}

bool IsLikelyEasyEdaJsonFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 8192, hdr) || hdr.size() < 24) return false;
    return Formats::IsLikelyEasyEdaJsonBytes(hdr.data(), hdr.size());
}

bool IsLikelySpiceRawFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 1024, hdr) || hdr.size() < 24) return false;
    return Formats::IsLikelySpiceRawHeaderBytes(hdr.data(), hdr.size());
}

bool IsLikelyAsicDefFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 4096, hdr) || hdr.size() < 16) return false;
    return Formats::IsLikelyAsicDefBytes(hdr.data(), hdr.size());
}

bool InspectEdaFile(const std::wstring& fullPath, ParsedEdaBoard& outBoard) try {
    outBoard = ParsedEdaBoard{};
    outBoard.filePath = fullPath;

    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, 32 * 1024 * 1024, raw) || raw.empty()) {
        return false;
    }

    std::wstring ext = PathUtils::GetExtension(fullPath);
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    if (ext == L".dxf") {
        return ParseAutoCadDxf(sv, outBoard);
    }

    // Modular EDA & Firmware formats (Format15 .. Format20)
    {
        bool handledExt = true;
        bool okMod = false;
        if (ext == L".asc" || ext == L".asy") okMod = Formats::ParseLtspiceBytes(raw.data(), raw.size(), fullPath, ext, outBoard);
        else if (ext == L".raw" || ext == L".tr0" || ext == L".ac0" || ext == L".sw0") okMod = Formats::ParseSpiceWaveformFile(raw.data(), raw.size(), ext, outBoard);
        else if (ext == L".dsn" || ext == L".olb") okMod = Formats::ParseOrCadOrSpecctraDsn(raw.data(), raw.size(), outBoard);
        else if (ext == L".d356" || ext == L".ipc") okMod = Formats::ParseIpcD356(sv, outBoard);
        else if (ext == L".hex" || ext == L".ihex") okMod = Formats::ParseIntelHexBoard(sv, outBoard);
        else if (ext == L".srec" || ext == L".s19" || ext == L".mot") okMod = Formats::ParseMotorolaSrecBoard(sv, outBoard);
        else if (ext == L".cir" || ext == L".spice" || ext == L".sp" || ext == L".net") okMod = Formats::ParseSpiceNetlistBoard(sv, outBoard);
        else if (ext == L".hpgl" || ext == L".plt") okMod = Formats::ParseHpglPlotterBoard(sv, outBoard);
        else if (ext == L".vcd") okMod = Formats::ParseVerilogVcdWaveforms(sv, outBoard);
        else if (ext == L".s1p" || ext == L".s2p") okMod = Formats::ParseTouchstoneSnpBoard(sv, outBoard);
        else if (ext == L".lef") okMod = Formats::ParseAsicLefBoard(sv, outBoard);
        else if (ext == L".def") okMod = Formats::ParseAsicDefBoard(sv, outBoard);
        else if (ext == L".gds" || ext == L".gds2") okMod = Formats::ParseGdsiiStreamBoard(raw.data(), raw.size(), outBoard);
        else if (ext == L".bsdl" || ext == L".bsd") okMod = Formats::ParseJtagBsdlBoard(sv, outBoard);
        else if (ext == L".svd") okMod = Formats::ParseCmsisSvdBoard(sv, outBoard);
        else if (ext == L".odb" || ext == L".tgz" || ext == L".ipc2581" || ext == L".cvg" ||
                 ext == L".fzz" || ext == L".fzpz" || ext == L".fz" || ext == L".fzp" || ext == L".pcb")
            okMod = Formats::ParseMfgIpcOdbFritzingGeda(raw.data(), raw.size(), ext, outBoard);
        else if (ext == L".pcbdoc" || ext == L".schdoc" || ext == L".pcblib" || ext == L".schlib") okMod = Formats::ParseAltiumDocBoard(raw.data(), raw.size(), ext, outBoard);
        else if (ext == L".epro" || ext == L".eprj" || ext == L".epcb" || ext == L".esch" ||
                 ext == L".esym" || ext == L".efoo" || ext == L".json")
            okMod = Formats::ParseEasyEdaFile(raw.data(), raw.size(), fullPath, ext, outBoard);
        else if (ext == L".edif" || ext == L".edn" || ext == L".blif" || ext == L".eblif" ||
                 ext == L".aig"  || ext == L".aag" || ext == L".lib"  || ext == L".sdf"   || ext == L".tdms")
            okMod = Formats::ParseGateNetlistOrTdmsFile(raw.data(), raw.size(), ext, outBoard);
        else if (ext == L".dbc" || ext == L".blf" || ext == L".can") okMod = Formats::ParseVectorCanBoard(raw.data(), raw.size(), ext, outBoard);
        else if (ext == L".fst" || ext == L".ghw" || ext == L".bit" || ext == L".rpd" ||
                 ext == L".dfu" || ext == L".ufd" || ext == L".edid" || ext == L".spd")
            okMod = Formats::ParseHardwareBinBoard(raw.data(), raw.size(), ext, outBoard);
        else {
            handledExt = false;
            okMod = Formats::ParseExtendedEda200(raw.data(), raw.size(), ext, outBoard);
        }

        if (!okMod && ext == L".asc") {
            outBoard = ParsedEdaBoard{};
            outBoard.filePath = fullPath;
            okMod = Formats::ParseVectorCanBoard(raw.data(), raw.size(), ext, outBoard);
        }
        if (okMod) {
            FinalizeBoardBounds(outBoard);
            outBoard.valid = true;
            return true;
        }
        if (handledExt) {
            outBoard = ParsedEdaBoard{};
            return false;
        }
    }

    if (DetailKiCadLegacy::StripUtf8BomView(sv).rfind("EESchema Schematic", 0) == 0) {
        return ParseKiCadLegacySch(sv, fullPath, outBoard);
    }

    if (ext == L".kicad_pcb" || ext == L".kicad_sch" || ext == L".kicad_mod" ||
        sv.find("(kicad_pcb") != std::string_view::npos ||
        sv.find("(kicad_sch") != std::string_view::npos ||
        sv.find("(footprint ") != std::string_view::npos)
    {
        return ParseKiCadSExpr(sv, ext, outBoard);
    }

    if (sv.find("<eagle") != std::string_view::npos || sv.find("<board>") != std::string_view::npos ||
        sv.find("<schematic>") != std::string_view::npos || ext == L".brd" || ext == L".sch" || ext == L".lbr")
    {
        if (sv.find('<') != std::string_view::npos && ParseEagleXml(sv, ext, outBoard)) {
            return true;
        }
        outBoard = ParsedEdaBoard{};
        outBoard.filePath = fullPath;
        if (ParseEagleBinaryV4V5(raw.data(), raw.size(), ext, outBoard)) {
            return true;
        }
        outBoard = ParsedEdaBoard{};
        return false;
    }

    const bool isGerberOrDrillExt =
        ext == L".gbr" || ext == L".ger" || ext == L".gtl" || ext == L".gbl" ||
        ext == L".gts" || ext == L".gbs" || ext == L".gto" || ext == L".gbo" ||
        ext == L".gm1" || ext == L".gko" || ext == L".gtp" || ext == L".gbp" ||
        ext == L".g2"  || ext == L".g3"  || ext == L".gp1" || ext == L".gp2" ||
        ext == L".oln" || ext == L".gbrjob" || ext == L".drl" || ext == L".xln" || ext == L".exc";
    if (!isGerberOrDrillExt && sv.find("%FS") == std::string_view::npos && sv.rfind("M48", 0) != 0) {
        outBoard = ParsedEdaBoard{};
        return false;
    }

    return ParseGerberOrDrill(sv, ext, outBoard);
} E2_READER_CATCH(L"Board reader", fullPath, { outBoard = ParsedEdaBoard(); return false; })

} // namespace EdaReaders

} // namespace Explorer2
