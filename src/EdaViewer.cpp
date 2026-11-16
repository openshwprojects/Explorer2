#include "EdaViewer.h"
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

static D2D1_COLOR_F EdaColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct ProjectedQuad3D {
    D2D1_POINT_2F pts[4] = {};
    float depth = 0.0f;
    uint32_t fillRgb = 0x1E293B;
    uint32_t strokeRgb = 0x0F172A;
    float alpha = 1.0f;
    bool drawStroke = false;
};

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
    size_t pos = 0;
    while (pos < tag.size()) {
        size_t found = tag.find(attrName, pos);
        if (found == std::string_view::npos) return false;
        if (found > 0) {
            char prev = tag[found - 1];
            if (prev != ' ' && prev != '\t' && prev != '\r' && prev != '\n') {
                pos = found + attrName.size();
                continue;
            }
        }
        size_t eq = found + attrName.size();
        while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
        if (eq >= tag.size() || tag[eq] != '=') {
            pos = eq;
            continue;
        }
        ++eq;
        while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
        if (eq >= tag.size() || (tag[eq] != '"' && tag[eq] != '\'')) return false;
        char quote = tag[eq++];
        size_t endQ = tag.find(quote, eq);
        if (endQ == std::string_view::npos) return false;
        outVal = tag.substr(eq, endQ - eq);
        return true;
    }
    return false;
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

EdaViewer::~EdaViewer() {
    Shutdown();
}

void EdaViewer::Initialize(HWND notifyHwnd, ID2D1Factory* d2dFactory) {
    m_notifyHwnd = notifyHwnd;
    m_d2dFactory = d2dFactory;
}

void EdaViewer::Shutdown() {
    Clear();
}

bool EdaViewer::IsEdaExtension(const std::wstring& extLower) {
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

bool EdaViewer::IsLikelyOdbPlusPlusTgz(const std::wstring& fullPath) {
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, 8 * 1024 * 1024, raw) || raw.size() < 16) return false;
    return Formats::IsLikelyOdbPlusPlusArchiveBytes(raw.data(), raw.size());
}

bool EdaViewer::IsLikelyLibertyOrEdaSdfFile(const std::wstring& fullPath, const std::wstring& extLower) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 8192, hdr) || hdr.size() < 16) return false;
    if (extLower == L".lib") {
        return Formats::IsLikelyLibertyLibBytes(hdr.data(), hdr.size()) ||
               IsLikelyKiCadLegacySymLibBytes(hdr.data(), hdr.size());
    }
    if (extLower == L".sdf") return Formats::IsLikelyIeeeSdfBytes(hdr.data(), hdr.size());
    return false;
}

bool EdaViewer::IsLikelyEasyEdaJsonFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 8192, hdr) || hdr.size() < 24) return false;
    return Formats::IsLikelyEasyEdaJsonBytes(hdr.data(), hdr.size());
}

bool EdaViewer::IsLikelySpiceRawFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 1024, hdr) || hdr.size() < 24) return false;
    return Formats::IsLikelySpiceRawHeaderBytes(hdr.data(), hdr.size());
}

bool EdaViewer::IsLikelyAsicDefFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 4096, hdr) || hdr.size() < 16) return false;
    return Formats::IsLikelyAsicDefBytes(hdr.data(), hdr.size());
}

bool EdaViewer::InspectEdaFile(const std::wstring& fullPath, ParsedEdaBoard& outBoard) try {
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

void EdaViewer::Clear() {
    m_currentPath.clear();
    m_board = ParsedEdaBoard{};
    m_selectedCompIdx = -1;
}

void EdaViewer::Fit2DView() {
    m_zoom2D = 1.0f;
    m_pan2DX = 0.0f;
    m_pan2DY = 0.0f;
}

void EdaViewer::Reset3DCamera() {
    m_yaw3D = 0.62f;
    m_pitch3D = 0.68f;
    m_zoom3D = 1.0f;
    m_pan3DX = 0.0f;
    m_pan3DY = 0.0f;
}

void EdaViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_currentPath == fullPath && !m_currentPath.empty()) {
        return;
    }
    ParsedEdaBoard board;
    InspectEdaFile(fullPath, board);
    ShowLoadedBoard(fullPath, std::move(board));
}

void EdaViewer::ShowLoadedBoard(const std::wstring& fullPath, ParsedEdaBoard&& board) {
    m_currentPath = fullPath;
    m_selectedCompIdx = -1;
    m_bomScrollY = 0.0f;
    m_board = std::move(board);
    Fit2DView();
    Reset3DCamera();

    // Default to 3D PCB view if it's a board with components, or 2D CAD for schematics/gerbers (user can toggle anytime!)
    m_view3D = false;
}

void EdaViewer::StepAnimation() {
    if (!m_view3D || !m_autoRotate3D) return;
    ULONGLONG now = GetTickCount64();
    float dt = (m_lastAnimTick > 0 && now > m_lastAnimTick)
        ? std::min(0.1f, static_cast<float>(now - m_lastAnimTick) * 0.001f)
        : 0.016f;
    m_lastAnimTick = now;
    m_yaw3D += dt * 0.75f;
    if (m_yaw3D > 6.2831853f) m_yaw3D -= 6.2831853f;
}

void EdaViewer::Draw2DCanvas(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& canvasR,
    const ParsedEdaBoard& board,
    Lang lang)
{
    const bool isSch = (board.kind == EdaDocKind::KiCadSch || board.kind == EdaDocKind::EagleSch);
    brush->SetColor(isSch ? EdaColor(0xFFFDF7) : EdaColor(0x0B1320));
    rt->FillRectangle(canvasR, brush);

    float availW = std::max(40.0f, canvasR.right - canvasR.left - 32.0f);
    float availH = std::max(40.0f, canvasR.bottom - canvasR.top - 32.0f);
    float bw = std::max(1.0f, board.maxX - board.minX);
    float bh = std::max(1.0f, board.maxY - board.minY);
    float baseScale = std::min(availW / bw, availH / bh);
    float scale = baseScale * m_zoom2D;

    float cx = (canvasR.left + canvasR.right) * 0.5f + m_pan2DX;
    float cy = (canvasR.top + canvasR.bottom) * 0.5f + m_pan2DY;
    float midX = (board.minX + board.maxX) * 0.5f;
    float midY = (board.minY + board.maxY) * 0.5f;

    auto toScreen = [&](float bx, float by) -> D2D1_POINT_2F {
        return D2D1::Point2F(cx + (bx - midX) * scale, cy - (by - midY) * scale);
    };

    // Subtle CAD grid
    float gridStepMm = (scale > 15.0f) ? 1.27f : ((scale > 4.0f) ? 5.0f : 10.0f);
    float stepPx = gridStepMm * scale;
    if (stepPx >= 10.0f) {
        brush->SetColor(isSch ? EdaColor(0xE2E8F0, 0.7f) : EdaColor(0x1E293B, 0.8f));
        for (float gx = std::fmod(cx - canvasR.left, stepPx) + canvasR.left; gx < canvasR.right; gx += stepPx) {
            rt->DrawLine(D2D1::Point2F(gx, canvasR.top), D2D1::Point2F(gx, canvasR.bottom), brush, 0.5f);
        }
        for (float gy = std::fmod(cy - canvasR.top, stepPx) + canvasR.top; gy < canvasR.bottom; gy += stepPx) {
            rt->DrawLine(D2D1::Point2F(canvasR.left, gy), D2D1::Point2F(canvasR.right, gy), brush, 0.5f);
        }
    }

    // Board outline substrate fill in 2D PCB mode
    if (!isSch) {
        D2D1_POINT_2F pMin = toScreen(board.minX, board.maxY);
        D2D1_POINT_2F pMax = toScreen(board.maxX, board.minY);
        D2D1_RECT_F brdR = D2D1::RectF(pMin.x, pMin.y, pMax.x, pMax.y);
        brush->SetColor(EdaColor(0x0F291E, 0.85f));
        rt->FillRectangle(brdR, brush);
        brush->SetColor(EdaColor(0xFACC15, 0.9f));
        rt->DrawRectangle(brdR, brush, 1.4f);
    }

    // 1. Bottom Copper traces
    if (m_showBotCu) {
        brush->SetColor(EdaColor(0x3B82F6, 0.85f));
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::BottomCopper) continue;
            D2D1_POINT_2F p1 = toScreen(t.x1, t.y1);
            D2D1_POINT_2F p2 = toScreen(t.x2, t.y2);
            rt->DrawLine(p1, p2, brush, std::max(1.2f, t.width * scale));
        }
    }

    // 2. Top Copper & Schematic traces
    if (m_showTopCu) {
        for (const auto& t : board.traces) {
            if (t.layer == EdaLayerId::BottomCopper || t.layer == EdaLayerId::TopSilk || t.layer == EdaLayerId::BottomSilk) continue;
            if (t.layer == EdaLayerId::EdgeCuts) brush->SetColor(EdaColor(0xFACC15));
            else if (t.layer == EdaLayerId::SchWire) brush->SetColor(EdaColor(0x15803D));
            else if (t.layer == EdaLayerId::SchSymbol) brush->SetColor(EdaColor(0x991B1B));
            else if (t.layer == EdaLayerId::InnerCopper) brush->SetColor(EdaColor(0xA855F7, 0.8f));
            else brush->SetColor(EdaColor(0xEF4444, 0.9f));

            D2D1_POINT_2F p1 = toScreen(t.x1, t.y1);
            D2D1_POINT_2F p2 = toScreen(t.x2, t.y2);
            rt->DrawLine(p1, p2, brush, std::max(1.2f, t.width * scale));
        }
    }

    // 3. Silkscreen lines & circles
    if (m_showSilk) {
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::TopSilk && t.layer != EdaLayerId::BottomSilk) continue;
            brush->SetColor(t.layer == EdaLayerId::BottomSilk ? EdaColor(0xC084FC, 0.8f) : EdaColor(0xF8FAFC, 0.9f));
            rt->DrawLine(toScreen(t.x1, t.y1), toScreen(t.x2, t.y2), brush, std::max(1.0f, t.width * scale));
        }
        for (const auto& c : board.circles) {
            D2D1_POINT_2F pc = toScreen(c.cx, c.cy);
            float r = std::max(1.5f, c.radius * scale);
            brush->SetColor(EdaColor(0xF8FAFC, 0.85f));
            rt->DrawEllipse(D2D1::Ellipse(pc, r, r), brush, std::max(1.0f, c.width * scale));
        }
    }

    // 4. Pads & Vias
    if (m_showPads) {
        for (const auto& p : board.pads) {
            if (p.layer == EdaLayerId::TopCopper && !m_showTopCu) continue;
            if (p.layer == EdaLayerId::BottomCopper && !m_showBotCu) continue;
            D2D1_POINT_2F pc = toScreen(p.x, p.y);
            float hw = std::max(1.8f, p.w * scale * 0.5f);
            float hh = std::max(1.8f, p.h * scale * 0.5f);
            if (p.layer == EdaLayerId::PadsThrough) brush->SetColor(EdaColor(0x10B981));
            else if (p.layer == EdaLayerId::BottomCopper) brush->SetColor(EdaColor(0x3B82F6));
            else brush->SetColor(EdaColor(0xF59E0B));

            if (p.shape == 0) {
                rt->FillEllipse(D2D1::Ellipse(pc, hw, hh), brush);
            } else {
                D2D1_RECT_F pr = D2D1::RectF(pc.x - hw, pc.y - hh, pc.x + hw, pc.y + hh);
                rt->FillRoundedRectangle(D2D1::RoundedRect(pr, 1.5f, 1.5f), brush);
            }
            if (p.drill > 0.05f) {
                float dr = std::max(1.0f, p.drill * scale * 0.5f);
                brush->SetColor(EdaColor(0x0B1320));
                rt->FillEllipse(D2D1::Ellipse(pc, dr, dr), brush);
            }
        }

        for (const auto& v : board.vias) {
            D2D1_POINT_2F pc = toScreen(v.x, v.y);
            float r = std::max(2.0f, v.diameter * scale * 0.5f);
            float dr = std::max(0.9f, v.drill * scale * 0.5f);
            brush->SetColor(EdaColor(0xEAB308));
            rt->FillEllipse(D2D1::Ellipse(pc, r, r), brush);
            brush->SetColor(EdaColor(0x0B1320));
            rt->FillEllipse(D2D1::Ellipse(pc, dr, dr), brush);
        }
    }

    // 5. Component RefDes Labels & Selection Highlight
    if (m_showSilk) {
        for (size_t i = 0; i < board.components.size(); ++i) {
            const auto& c = board.components[i];
            D2D1_POINT_2F pc = toScreen(c.x, c.y);
            float hw = std::max(4.0f, c.widthMm * scale * 0.5f);
            float hh = std::max(3.0f, c.lengthMm * scale * 0.5f);

            if (static_cast<int>(i) == m_selectedCompIdx) {
                D2D1_RECT_F selR = D2D1::RectF(pc.x - hw - 3.0f, pc.y - hh - 3.0f, pc.x + hw + 3.0f, pc.y + hh + 3.0f);
                brush->SetColor(EdaColor(0x22D3EE, 0.28f));
                rt->FillRoundedRectangle(D2D1::RoundedRect(selR, 3.0f, 3.0f), brush);
                brush->SetColor(EdaColor(0x22D3EE));
                rt->DrawRoundedRectangle(D2D1::RoundedRect(selR, 3.0f, 3.0f), brush, 2.0f);
            }

            if (scale >= 2.2f && !c.refDes.empty()) {
                brush->SetColor(isSch ? EdaColor(0x0F172A) : (c.bottomSide ? EdaColor(0x93C5FD) : EdaColor(0xFEF08A)));
                D2D1_RECT_F tr = D2D1::RectF(pc.x - 45.0f, pc.y - 10.0f, pc.x + 45.0f, pc.y + 10.0f);
                fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(c.refDes.c_str(), static_cast<UINT32>(c.refDes.size()), fmtSmall, tr, brush);
                fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
    }

    // 6. Board & Schematic Text Labels (board.labels)
    if ((m_showSilk || isSch) && fmtSmall && !board.labels.empty()) {
        rt->PushAxisAlignedClip(canvasR, D2D1_ANTIALIAS_MODE_ALIASED);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        const size_t maxLabels = 2048;
        size_t drawn = 0;
        for (const auto& lbl : board.labels) {
            if (lbl.text.empty()) continue;
            D2D1_POINT_2F pt = toScreen(lbl.x, lbl.y);
            if (pt.x < canvasR.left || pt.x > canvasR.right ||
                pt.y < canvasR.top  || pt.y > canvasR.bottom) {
                continue;
            }
            if (scale >= 2.2f && m_showSilk && board.components.size() <= 256) {
                bool dupCompRef = false;
                for (const auto& c : board.components) {
                    if (c.refDes == lbl.text &&
                        std::fabs(c.x - lbl.x) < 0.05f &&
                        std::fabs(c.y - lbl.y) < 0.05f) {
                        dupCompRef = true;
                        break;
                    }
                }
                if (dupCompRef) continue;
            }

            if (isSch) {
                if (lbl.layer == EdaLayerId::SchWire) brush->SetColor(EdaColor(0x166534));
                else if (lbl.layer == EdaLayerId::SchSymbol) brush->SetColor(EdaColor(0x7F1D1D));
                else brush->SetColor(EdaColor(0x1E293B));
            } else {
                if (lbl.layer == EdaLayerId::BottomCopper || lbl.layer == EdaLayerId::BottomSilk) {
                    brush->SetColor(EdaColor(0x93C5FD));
                } else if (lbl.layer == EdaLayerId::InnerCopper) {
                    brush->SetColor(EdaColor(0xD8B4FE));
                } else if (lbl.layer == EdaLayerId::EdgeCuts) {
                    brush->SetColor(EdaColor(0xFDE047));
                } else {
                    brush->SetColor(EdaColor(0xF8FAFC));
                }
            }

            D2D1_RECT_F tr = D2D1::RectF(pt.x, pt.y - 9.0f, std::min(canvasR.right, pt.x + 260.0f), pt.y + 11.0f);
            if (tr.right > tr.left + 2.0f) {
                rt->DrawTextW(lbl.text.c_str(), static_cast<UINT32>(lbl.text.size()), fmtSmall, tr, brush);
                if (++drawn >= maxLabels) break;
            }
        }
        rt->PopAxisAlignedClip();
    }
    (void)fmtBold;
    (void)lang;
}

void EdaViewer::Draw3DPcbViewport(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& canvasR,
    const ParsedEdaBoard& board,
    Lang lang)
{
    // Studio Dark 3D background
    brush->SetColor(EdaColor(0x0F172A));
    rt->FillRectangle(canvasR, brush);

    float cx = (canvasR.left + canvasR.right) * 0.5f + m_pan3DX;
    float cy = (canvasR.top + canvasR.bottom) * 0.5f + m_pan3DY;
    float availW = std::max(80.0f, canvasR.right - canvasR.left);
    float availH = std::max(80.0f, canvasR.bottom - canvasR.top);

    float bw = std::max(4.0f, board.maxX - board.minX);
    float bh = std::max(4.0f, board.maxY - board.minY);
    float midX = (board.minX + board.maxX) * 0.5f;
    float midY = (board.minY + board.maxY) * 0.5f;
    float maxSpan = std::max(bw, bh);
    float scale = (std::min(availW, availH) * 0.72f / maxSpan) * m_zoom3D;

    float cyaw = std::cos(m_yaw3D), syaw = std::sin(m_yaw3D);
    float cpitch = std::cos(m_pitch3D), spitch = std::sin(m_pitch3D);

    auto project3D = [&](float wx, float wy, float wz, float* outDepth = nullptr) -> D2D1_POINT_2F {
        float lx = wx - midX;
        float ly = wy - midY;
        float lz = wz;
        // Rotate around Z (Yaw) then tilt around X (Pitch)
        float rx = lx * cyaw - ly * syaw;
        float ry = lx * syaw + ly * cyaw;
        float sy2 = ry * cpitch - lz * spitch;
        float sz2 = ry * spitch + lz * cpitch;
        if (outDepth) *outDepth = sz2;
        float persp = 1.0f / std::max(0.35f, 1.0f - (sz2 / (maxSpan * 3.5f)));
        return D2D1::Point2F(cx + rx * scale * persp, cy - sy2 * scale * persp);
    };

    // Solder mask color palette: Green, Blue, Red, Matte Black, Purple, White
    static const uint32_t kMaskTopColors[6]  = { 0x14532D, 0x1E3A8A, 0x7F1D1D, 0x18181B, 0x4C1D95, 0xE2E8F0 };
    static const uint32_t kMaskEdgeColors[6] = { 0xA16207, 0x854D0E, 0x854D0E, 0x52525B, 0x713F12, 0x94A3B8 };
    uint32_t maskRgb = kMaskTopColors[m_solderMaskPreset % 6];
    uint32_t edgeRgb = kMaskEdgeColors[m_solderMaskPreset % 6];

    std::vector<ProjectedQuad3D> quads;
    quads.reserve(board.components.size() * 10 + board.pads.size() + 16);

    auto pushQuad3D = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, uint32_t fill, uint32_t stroke, bool drawStrk = true, float alpha = 1.0f) {
        ProjectedQuad3D q;
        float d0 = 0, d1 = 0, d2 = 0, d3 = 0;
        q.pts[0] = project3D(p0.x, p0.y, p0.z, &d0);
        q.pts[1] = project3D(p1.x, p1.y, p1.z, &d1);
        q.pts[2] = project3D(p2.x, p2.y, p2.z, &d2);
        q.pts[3] = project3D(p3.x, p3.y, p3.z, &d3);
        q.depth = (d0 + d1 + d2 + d3) * 0.25f;
        q.fillRgb = fill;
        q.strokeRgb = stroke;
        q.drawStroke = drawStrk;
        q.alpha = alpha;
        quads.push_back(q);
    };

    auto pushBox3D = [&](float cx3, float cy3, float zMin, float zMax, float w, float l, float rotDeg,
                         uint32_t topRgb, uint32_t sideRgb, uint32_t strokeRgb) {
        float hw = w * 0.5f, hl = l * 0.5f;
        float rad = rotDeg * 3.14159265f / 180.0f;
        float cs = std::cos(rad), sn = std::sin(rad);
        auto corner = [&](float dx, float dy, float z) -> Vec3 {
            return { cx3 + (dx * cs - dy * sn), cy3 + (dx * sn + dy * cs), z };
        };
        Vec3 b0 = corner(-hw, -hl, zMin), b1 = corner(hw, -hl, zMin), b2 = corner(hw, hl, zMin), b3 = corner(-hw, hl, zMin);
        Vec3 t0 = corner(-hw, -hl, zMax), t1 = corner(hw, -hl, zMax), t2 = corner(hw, hl, zMax), t3 = corner(-hw, hl, zMax);
        pushQuad3D(b0, b1, t1, t0, sideRgb, strokeRgb, true);
        pushQuad3D(b1, b2, t2, t1, sideRgb, strokeRgb, true);
        pushQuad3D(b2, b3, t3, t2, sideRgb, strokeRgb, true);
        pushQuad3D(b3, b0, t0, t3, sideRgb, strokeRgb, true);
        pushQuad3D(t0, t1, t2, t3, topRgb, strokeRgb, true);
    };

    // 1. Draw 3D FR4 PCB Board Substrate (-0.8 mm to +0.8 mm)
    float zBot = -0.8f, zTop = 0.8f;
    Vec3 brdB0{ board.minX, board.minY, zBot }, brdB1{ board.maxX, board.minY, zBot },
         brdB2{ board.maxX, board.maxY, zBot }, brdB3{ board.minX, board.maxY, zBot };
    Vec3 brdT0{ board.minX, board.minY, zTop }, brdT1{ board.maxX, board.minY, zTop },
         brdT2{ board.maxX, board.maxY, zTop }, brdT3{ board.minX, board.maxY, zTop };

    // Board side edges (FR4 epoxy glass) + Top/Bottom solder mask planes
    pushQuad3D(brdB0, brdB1, brdT1, brdT0, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB1, brdB2, brdT2, brdT1, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB2, brdB3, brdT3, brdT2, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB3, brdB0, brdT0, brdT3, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdT0, brdT1, brdT2, brdT3, maskRgb, 0x22C55E, true, 0.96f);

    // 2. Render 3D Surface Pads (ENIG Gold / HASL Silver)
    if (m_showPads) {
        for (const auto& p : board.pads) {
            float pz = (p.layer == EdaLayerId::BottomCopper) ? (zBot - 0.04f) : (zTop + 0.04f);
            float hw = p.w * 0.5f, hh = p.h * 0.5f;
            uint32_t padRgb = (p.layer == EdaLayerId::PadsThrough) ? 0xFBBF24 : 0xE2E8F0;
            pushQuad3D(
                { p.x - hw, p.y - hh, pz },
                { p.x + hw, p.y - hh, pz },
                { p.x + hw, p.y + hh, pz },
                { p.x - hw, p.y + hh, pz },
                padRgb, 0x94A3B8, false);
        }
    }

    // 3. Render 3D Extruded Electronic Components (ICs, SMD Resistors/Capacitors, Connectors, Transistors, LEDs)
    if (m_show3DParts) {
        for (size_t i = 0; i < board.components.size(); ++i) {
            const auto& c = board.components[i];
            if (c.category == EdaPartCategory::MountingHole) continue;

            bool isSelected = (static_cast<int>(i) == m_selectedCompIdx);
            float baseZ = c.bottomSide ? (zBot - c.heightMm) : zTop;
            float topZ  = c.bottomSide ? zBot : (zTop + c.heightMm);
            uint32_t stroke = isSelected ? 0x22D3EE : 0x0F172A;

            switch (c.category) {
            case EdaPartCategory::ResistorSMD: {
                // Silver metallic end-caps + dark resistive ceramic body in center
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.03f, c.widthMm * 0.62f, c.lengthMm * 0.96f, c.rotDeg,
                    isSelected ? 0x0284C7 : 0x1E293B, 0x0F172A, stroke);
                break;
            }
            case EdaPartCategory::CapacitorSMD: {
                // Silver metallic end-caps + warm MLCC tan/brown ceramic body
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.03f, c.widthMm * 0.66f, c.lengthMm * 0.96f, c.rotDeg,
                    isSelected ? 0x0284C7 : 0xB45309, 0x78350F, stroke);
                break;
            }
            case EdaPartCategory::ElectrolyticCap: {
                // Black plastic base + tall aluminum cylinder body
                pushBox3D(c.x, c.y, baseZ, baseZ + 0.6f, c.widthMm, c.lengthMm, c.rotDeg, 0x1E293B, 0x0F172A, stroke);
                pushBox3D(c.x, c.y, baseZ + 0.6f, topZ, c.widthMm * 0.82f, c.lengthMm * 0.82f, c.rotDeg + 22.5f,
                    isSelected ? 0x38BDF8 : 0x94A3B8, 0x475569, stroke);
                break;
            }
            case EdaPartCategory::IC_Package:
            case EdaPartCategory::TransistorSOT: {
                // Dark molded epoxy IC package + metallic lead frame base
                pushBox3D(c.x, c.y, baseZ, baseZ + 0.25f, c.widthMm * 1.08f, c.lengthMm * 1.08f, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ + 0.15f, topZ, c.widthMm * 0.88f, c.lengthMm * 0.88f, c.rotDeg,
                    isSelected ? 0x0369A1 : 0x1E293B, 0x0F172A, stroke);
                break;
            }
            case EdaPartCategory::Connector: {
                // Beige/Black header housing + gold pin posts
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg,
                    isSelected ? 0x0284C7 : 0xD6D3D1, 0x78716C, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.6f, c.widthMm * 0.7f, c.lengthMm * 0.25f, c.rotDeg, 0xFACC15, 0xCA8A04, stroke);
                break;
            }
            case EdaPartCategory::DiodeLED: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0x22C55E, 0x15803D, stroke);
                break;
            }
            case EdaPartCategory::Crystal: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xE2E8F0, 0x94A3B8, stroke);
                break;
            }
            default: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg,
                    isSelected ? 0x0284C7 : 0x334155, 0x1E293B, stroke);
                break;
            }
            }
        }
    }

    // Painter's algorithm sort (back to front)
    std::sort(quads.begin(), quads.end(), [](const ProjectedQuad3D& a, const ProjectedQuad3D& b) {
        return a.depth < b.depth;
    });

    for (const auto& q : quads) {
        if (m_d2dFactory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink)) && sink) {
                    sink->BeginFigure(q.pts[0], D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(q.pts[1]);
                    sink->AddLine(q.pts[2]);
                    sink->AddLine(q.pts[3]);
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();

                    brush->SetColor(EdaColor(q.fillRgb, q.alpha));
                    rt->FillGeometry(path, brush);
                    if (q.drawStroke) {
                        brush->SetColor(EdaColor(q.strokeRgb, 0.85f));
                        rt->DrawGeometry(path, brush, 0.85f);
                    }
                }
                path->Release();
            }
        }
    }

    // Overlay 3D Copper Traces on Top Surface
    if (m_showTopCu) {
        brush->SetColor(EdaColor(0x22C55E, 0.75f));
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::TopCopper) continue;
            D2D1_POINT_2F p1 = project3D(t.x1, t.y1, zTop + 0.03f);
            D2D1_POINT_2F p2 = project3D(t.x2, t.y2, zTop + 0.03f);
            rt->DrawLine(p1, p2, brush, std::max(1.0f, t.width * scale * 0.8f));
        }
    }

    // Overlay 3D Component Silkscreen Designators on Top of 3D Packages
    if (m_showSilk && scale >= 2.2f) {
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (const auto& c : board.components) {
            if (c.refDes.empty()) continue;
            float labelZ = m_show3DParts ? (c.bottomSide ? (zBot - c.heightMm - 0.1f) : (zTop + c.heightMm + 0.1f)) : (zTop + 0.1f);
            D2D1_POINT_2F pt = project3D(c.x, c.y, labelZ);
            brush->SetColor(EdaColor(0xF8FAFC));
            rt->DrawTextW(c.refDes.c_str(), static_cast<UINT32>(c.refDes.size()), fmtSmall,
                D2D1::RectF(pt.x - 35.0f, pt.y - 8.0f, pt.x + 35.0f, pt.y + 8.0f), brush);
        }
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    (void)fmtBold;
    (void)lang;
}

void EdaViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F /*mousePt*/)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();

    float tbH = 30.0f;
    float statusH = 26.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(EdaColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(EdaColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool active) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, br, label, active, m_hoverBtnId == id);
        bx += w + 4.0f;
    };

    addBtn(BTN_TOGGLE_2D_3D, pl ? 88.0f : 82.0f,
        m_view3D ? (pl ? L"\xD83E\xDDCA Widok 3D PCB" : L"\xD83E\xDDCA 3D PCB View")
                 : (pl ? L"\xD83D\xDCD0 Widok 2D CAD" : L"\xD83D\xDCD0 2D CAD View"),
        m_view3D);

    if (m_view3D) {
        addBtn(BTN_TOGGLE_3D_PARTS, pl ? 78.0f : 72.0f, pl ? L"Elementy 3D" : L"3D Parts", m_show3DParts);
        addBtn(BTN_SOLDER_MASK_COLOR, pl ? 74.0f : 70.0f, pl ? L"Kolor maski" : L"Mask Color", false);
        addBtn(BTN_AUTO_ROTATE_3D, pl ? 70.0f : 64.0f, pl ? L"Auto-obrót" : L"Turntable", m_autoRotate3D);
    } else {
        addBtn(BTN_LAYER_TOPCU, 54.0f, L"Top Cu", m_showTopCu);
        addBtn(BTN_LAYER_BOTCU, 54.0f, L"Bot Cu", m_showBotCu);
        addBtn(BTN_LAYER_SILK,  46.0f, L"Silk",   m_showSilk);
        addBtn(BTN_LAYER_PADS,  52.0f, L"Pads",   m_showPads);
    }
    addBtn(BTN_TOGGLE_BOM, 56.0f, L"BOM", m_showBomPanel);
    addBtn(BTN_FIT_RESET,  pl ? 58.0f : 52.0f, pl ? L"Dopasuj" : L"Fit", false);

    D2D1_RECT_F mainR = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom, rect.bottom - statusH));
    if (m_showBomPanel && (mainR.right - mainR.left) > 260.0f) {
        float bomW = std::min(190.0f, (mainR.right - mainR.left) * 0.42f);
        m_canvasRect = D2D1::RectF(mainR.left, mainR.top, mainR.right - bomW, mainR.bottom);
        m_bomRect = D2D1::RectF(mainR.right - bomW, mainR.top, mainR.right, mainR.bottom);
    } else {
        m_canvasRect = mainR;
        m_bomRect = {};
    }

    rt->PushAxisAlignedClip(m_canvasRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (m_view3D) {
        Draw3DPcbViewport(rt, brush, fmtSmall, fmtBold, m_canvasRect, m_board, lang);
    } else {
        Draw2DCanvas(rt, brush, fmtSmall, fmtBold, m_canvasRect, m_board, lang);
    }
    rt->PopAxisAlignedClip();

    // Optional Right-Side BOM Component List
    if (m_showBomPanel && m_bomRect.right > m_bomRect.left) {
        brush->SetColor(EdaColor(0xF8FAFC));
        rt->FillRectangle(m_bomRect, brush);
        brush->SetColor(EdaColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(m_bomRect.left, m_bomRect.top), D2D1::Point2F(m_bomRect.left, m_bomRect.bottom), brush, 1.0f);

        rt->PushAxisAlignedClip(m_bomRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float rowH = 20.0f;
        for (size_t i = 0; i < m_board.components.size(); ++i) {
            float ry = m_bomRect.top + i * rowH - m_bomScrollY;
            if (ry + rowH < m_bomRect.top || ry > m_bomRect.bottom) continue;
            D2D1_RECT_F rr = D2D1::RectF(m_bomRect.left + 2.0f, ry, m_bomRect.right - 2.0f, ry + rowH);
            if (static_cast<int>(i) == m_selectedCompIdx) {
                brush->SetColor(EdaColor(0xDBEAFE));
                rt->FillRectangle(rr, brush);
            }
            const auto& c = m_board.components[i];
            std::wstring lbl = c.refDes + (c.value.empty() ? L"" : (L"  " + c.value)) + (c.footprint.empty() ? L"" : (L" (" + c.footprint + L")"));
            brush->SetColor(EdaColor(0x0F172A));
            rt->DrawTextW(lbl.c_str(), static_cast<UINT32>(lbl.size()), fmtSmall,
                D2D1::RectF(rr.left + 4.0f, rr.top, rr.right - 4.0f, rr.bottom), brush);
        }
        rt->PopAxisAlignedClip();
    }

    // Bottom Status Strip
    D2D1_RECT_F stR = D2D1::RectF(rect.left, mainR.bottom, rect.right, rect.bottom);
    brush->SetColor(EdaColor(0xF1F5F9));
    rt->FillRectangle(stR, brush);
    brush->SetColor(EdaColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(stR.left, stR.top), D2D1::Point2F(stR.right, stR.top), brush, 1.0f);

    wchar_t stBuf[320] = {};
    if (m_selectedCompIdx >= 0 && m_selectedCompIdx < static_cast<int>(m_board.components.size())) {
        const auto& c = m_board.components[m_selectedCompIdx];
        swprintf_s(stBuf,
            L"\x2713 %s  |  Val: %s  |  Pkg: %s  |  Pos: (%.2f, %.2f mm) %s",
            c.refDes.c_str(), c.value.c_str(), c.footprint.c_str(), c.x, c.y, c.bottomSide ? L"[Bottom]" : L"[Top]");
    } else {
        swprintf_s(stBuf,
            pl ? L"%s   \x2022   Elementy: %zu   |   Pady/Via: %zu   |   Ścieżki: %zu   |   Wymiary: %.1f \xD7 %.1f mm"
               : L"%s   \x2022   Parts: %zu   |   Pads/Vias: %zu   |   Traces: %zu   |   Board: %.1f \xD7 %.1f mm",
            m_board.formatTitle.c_str(),
            m_board.components.size(),
            m_board.pads.size() + m_board.vias.size(),
            m_board.traces.size(),
            std::abs(m_board.maxX - m_board.minX),
            std::abs(m_board.maxY - m_board.minY));
    }
    brush->SetColor(EdaColor(0x334155));
    rt->DrawTextW(stBuf, static_cast<UINT32>(wcslen(stBuf)), fmtSmall,
        D2D1::RectF(stR.left + 8.0f, stR.top, stR.right - 8.0f, stR.bottom), brush);
    (void)fmtRegular;
}

bool EdaViewer::OnMouseDown(float x, float y, bool rightButton, Lang /*lang*/, std::wstring* /*outStatusToast*/) {
    if (!rightButton) {
        for (const auto& hit : m_toolbarHits) {
            if (PathUtils::PtInRectF(x, y, hit.rect)) {
                switch (hit.id) {
                case BTN_TOGGLE_2D_3D:      m_view3D = !m_view3D; break;
                case BTN_LAYER_TOPCU:       m_showTopCu = !m_showTopCu; break;
                case BTN_LAYER_BOTCU:       m_showBotCu = !m_showBotCu; break;
                case BTN_LAYER_SILK:        m_showSilk = !m_showSilk; break;
                case BTN_LAYER_PADS:        m_showPads = !m_showPads; break;
                case BTN_TOGGLE_3D_PARTS:   m_show3DParts = !m_show3DParts; break;
                case BTN_SOLDER_MASK_COLOR: m_solderMaskPreset = (m_solderMaskPreset + 1) % 6; break;
                case BTN_AUTO_ROTATE_3D:    m_autoRotate3D = !m_autoRotate3D; m_lastAnimTick = GetTickCount64(); break;
                case BTN_TOGGLE_BOM:        m_showBomPanel = !m_showBomPanel; break;
                case BTN_FIT_RESET:
                    if (m_view3D) Reset3DCamera();
                    else Fit2DView();
                    break;
                }
                return true;
            }
        }

        if (m_showBomPanel && PathUtils::PtInRectF(x, y, m_bomRect)) {
            int idx = static_cast<int>((y - m_bomRect.top + m_bomScrollY) / 20.0f);
            if (idx >= 0 && idx < static_cast<int>(m_board.components.size())) {
                m_selectedCompIdx = idx;
            }
            return true;
        }
    }

    if (PathUtils::PtInRectF(x, y, m_canvasRect)) {
        m_lastMouseX = x;
        m_lastMouseY = y;
        if (rightButton) {
            m_draggingRight = true;
        } else {
            m_draggingLeft = true;
        }
        return true;
    }
    return false;
}

bool EdaViewer::OnMouseMove(float x, float y, WPARAM keyFlags) {
    if (m_draggingLeft || m_draggingRight) {
        float dx = x - m_lastMouseX;
        float dy = y - m_lastMouseY;
        m_lastMouseX = x;
        m_lastMouseY = y;
        if (m_view3D) {
            if (m_draggingRight || (keyFlags & MK_SHIFT)) {
                m_pan3DX += dx;
                m_pan3DY += dy;
            } else {
                m_yaw3D += dx * 0.01f;
                m_pitch3D = std::clamp(m_pitch3D + dy * 0.01f, 0.1f, 1.48f);
            }
        } else {
            m_pan2DX += dx;
            m_pan2DY += dy;
        }
        return true;
    }

    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        return true;
    }
    return false;
}

bool EdaViewer::OnMouseUp() {
    if (m_draggingLeft || m_draggingRight) {
        m_draggingLeft = false;
        m_draggingRight = false;
        return true;
    }
    return false;
}

bool EdaViewer::OnMouseWheel(short delta, float x, float y) {
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    if (m_showBomPanel && PathUtils::PtInRectF(x, y, m_bomRect)) {
        float maxScroll = std::max(0.0f, static_cast<float>(m_board.components.size()) * 20.0f - (m_bomRect.bottom - m_bomRect.top));
        m_bomScrollY = std::clamp(m_bomScrollY - notches * 40.0f, 0.0f, maxScroll);
        return true;
    }

    if (PathUtils::PtInRectF(x, y, m_canvasRect)) {
        float factor = std::pow(1.18f, static_cast<float>(notches));
        if (m_view3D) {
            m_zoom3D = std::clamp(m_zoom3D * factor, 0.25f, 18.0f);
        } else {
            m_zoom2D = std::clamp(m_zoom2D * factor, 0.2f, 40.0f);
        }
        return true;
    }
    return false;
}

} // namespace Explorer2
