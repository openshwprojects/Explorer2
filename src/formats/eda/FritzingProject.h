#pragma once
// FritzingProject.h — Fritzing Sketch & Part Archive (.fzz / .fz / .fzpz / .fzp) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailBoardFabEda {

// ============================================================================
// Fritzing Sketch (.fzz / .fz) & Part Bundle (.fzpz / .fzp) Parser
// ============================================================================
struct FritzingPartSvgFootprint {
    float widthMm = 3.0f;
    float heightMm = 2.0f;
    struct PinPad {
        std::string id;
        float rxMm = 0.0f, ryMm = 0.0f;
        float wMm = 1.0f, hMm = 1.0f;
        float drillMm = 0.0f;
        uint8_t shape = 0;
    };
    std::vector<PinPad> pads;
};

inline float ParseSvgDimMm(std::string_view sv, float viewScaleMmPerUnit = 0.254f) {
    if (sv.empty()) return 0.0f;
    std::string s(sv);
    char* ep = nullptr;
    float val = std::strtof(s.c_str(), &ep);
    if (!ep || ep == s.c_str()) return 0.0f;
    std::string_view unit(ep);
    while (!unit.empty() && unit.front() == ' ') unit.remove_prefix(1);
    if (DetailSiliconAndBusEda::ContainsAsciiI(unit, "mm")) return val;
    if (DetailSiliconAndBusEda::ContainsAsciiI(unit, "cm")) return val * 10.0f;
    if (DetailSiliconAndBusEda::ContainsAsciiI(unit, "in")) return val * 25.4f;
    if (DetailSiliconAndBusEda::ContainsAsciiI(unit, "mil")) return val * 0.0254f;
    if (DetailSiliconAndBusEda::ContainsAsciiI(unit, "pt")) return val * (25.4f / 72.0f);
    if (DetailSiliconAndBusEda::ContainsAsciiI(unit, "px")) return val * (25.4f / 90.0f);
    return val * viewScaleMmPerUnit;
}

inline FritzingPartSvgFootprint ParseFritzingPcbSvg(std::string_view svg) {
    FritzingPartSvgFootprint fp;
    float scaleMm = 25.4f / 90.0f; // Fritzing default 90 DPI
    size_t svgTag = svg.find("<svg");
    if (svgTag != std::string_view::npos) {
        size_t gt = svg.find('>', svgTag);
        if (gt != std::string_view::npos) {
            std::string_view hdr = svg.substr(svgTag, gt - svgTag);
            std::string_view wSv, hSv, vbSv;
            float wMm = 0.0f, hMm = 0.0f;
            if (GetXmlAttr(hdr, "width", wSv))  wMm = ParseSvgDimMm(wSv, scaleMm);
            if (GetXmlAttr(hdr, "height", hSv)) hMm = ParseSvgDimMm(hSv, scaleMm);
            if (wMm > 0.2f && hMm > 0.2f) {
                fp.widthMm = wMm;
                fp.heightMm = hMm;
                if (GetXmlAttr(hdr, "viewBox", vbSv)) {
                    float vx = 0, vy = 0, vw = 0, vh = 0;
                    std::string vbs(vbSv);
                    if (sscanf_s(vbs.c_str(), "%f %f %f %f", &vx, &vy, &vw, &vh) == 4 && vw > 1e-3f) {
                        scaleMm = wMm / vw;
                    }
                }
            }
        }
    }

    size_t pos = 0;
    while (pos < svg.size() && fp.pads.size() < 256) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string_view::npos) break;
        size_t gt = svg.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = svg.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        if (tag.empty() || tag[0] == '/' || tag[0] == '?' || tag[0] == '!') continue;

        size_t ne = tag.find_first_of(" \t\r\n/");
        std::string_view tn = (ne == std::string_view::npos) ? tag : tag.substr(0, ne);
        std::string_view idSv;
        GetXmlAttr(tag, "id", idSv);
        if (idSv.find("connector") == std::string_view::npos) continue;

        if (tn == "circle") {
            float cx = GetXmlFloat(tag, "cx") * scaleMm;
            float cy = GetXmlFloat(tag, "cy") * scaleMm;
            float r  = GetXmlFloat(tag, "r", 1.5f) * scaleMm;
            float sw = GetXmlFloat(tag, "stroke-width", 0.5f) * scaleMm;
            float outerD = std::max(0.5f, (r + sw * 0.5f) * 2.0f);
            float drillD = std::max(0.0f, (r - sw * 0.5f) * 2.0f);
            fp.pads.push_back({ std::string(idSv), cx - fp.widthMm * 0.5f, -(cy - fp.heightMm * 0.5f), outerD, outerD, drillD, 0 });
        } else if (tn == "rect") {
            float rx = GetXmlFloat(tag, "x") * scaleMm;
            float ry = GetXmlFloat(tag, "y") * scaleMm;
            float rw = std::max(0.4f, GetXmlFloat(tag, "width", 2.0f) * scaleMm);
            float rh = std::max(0.4f, GetXmlFloat(tag, "height", 2.0f) * scaleMm);
            float cx = rx + rw * 0.5f;
            float cy = ry + rh * 0.5f;
            fp.pads.push_back({ std::string(idSv), cx - fp.widthMm * 0.5f, -(cy - fp.heightMm * 0.5f), rw, rh, 0.0f, 1 });
        }
    }
    return fp;
}

inline bool ParseFritzingPartXmlAndSvg(
    std::string_view fzpXml,
    const std::unordered_map<std::string, std::string>& svgByName,
    ParsedEdaBoard& out)
{
    if (fzpXml.find("<module") == std::string_view::npos || fzpXml.find("</module>") == std::string_view::npos) {
        return false;
    }
    std::string titleStr = "FritzingPart";
    size_t t1 = fzpXml.find("<title>");
    size_t t2 = (t1 != std::string_view::npos) ? fzpXml.find("</title>", t1 + 7) : std::string_view::npos;
    if (t1 != std::string_view::npos && t2 != std::string_view::npos) {
        titleStr = std::string(fzpXml.substr(t1 + 7, t2 - (t1 + 7)));
    }

    // Locate pcbView image if available
    FritzingPartSvgFootprint fp;
    for (const auto& kv : svgByName) {
        if (kv.first.find("pcb") != std::string::npos) {
            fp = ParseFritzingPcbSvg(kv.second);
            if (!fp.pads.empty()) break;
        }
    }

    // Count <connector id="..." name="..."> entries
    std::vector<std::pair<std::string, std::string>> conns;
    size_t p = 0;
    while ((p = fzpXml.find("<connector ", p)) != std::string_view::npos && conns.size() < 256) {
        size_t gt = fzpXml.find('>', p);
        if (gt == std::string_view::npos) break;
        std::string_view ctag = fzpXml.substr(p, gt - p);
        std::string_view cid, cname;
        GetXmlAttr(ctag, "id", cid);
        GetXmlAttr(ctag, "name", cname);
        if (!cid.empty()) conns.push_back({ std::string(cid), std::string(cname.empty() ? cid : cname) });
        p = gt + 1;
    }
    if (fp.pads.empty() && conns.empty()) return false;

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"Fritzing Part Package (" + PathUtils::Utf8ToWide(titleStr) + L")";

    if (fp.pads.empty()) {
        // Synthesize true dual-row 2.54mm DIP/header pads from actual <connector> definitions
        size_t half = (conns.size() + 1) / 2;
        fp.widthMm = std::max(4.0f, static_cast<float>(half) * 2.54f);
        fp.heightMm = 7.62f;
        for (size_t i = 0; i < conns.size(); ++i) {
            bool topRow = (i >= half);
            size_t col = topRow ? (conns.size() - 1 - i) : i;
            float rx = -fp.widthMm * 0.5f + 1.27f + static_cast<float>(col) * 2.54f;
            float ry = topRow ? 3.81f : -3.81f;
            fp.pads.push_back({ conns[i].second, rx, ry, 1.5f, 1.5f, 0.8f, static_cast<uint8_t>(i == 0 ? 1 : 0) });
        }
    }

    float hw = fp.widthMm * 0.5f + 1.0f, hh = fp.heightMm * 0.5f + 1.0f;
    out.traces.push_back({ -hw, -hh,  hw, -hh, 0.22f, EdaLayerId::TopSilk, 0 });
    out.traces.push_back({  hw, -hh,  hw,  hh, 0.22f, EdaLayerId::TopSilk, 0 });
    out.traces.push_back({  hw,  hh, -hw,  hh, 0.22f, EdaLayerId::TopSilk, 0 });
    out.traces.push_back({ -hw,  hh, -hw, -hh, 0.22f, EdaLayerId::TopSilk, 0 });

    for (size_t i = 0; i < fp.pads.size(); ++i) {
        const auto& pd = fp.pads[i];
        std::wstring pinNm = (i < conns.size()) ? PathUtils::Utf8ToWide(conns[i].second) : PathUtils::Utf8ToWide(pd.id);
        EdaLayerId lid = (pd.drillMm > 0.05f) ? EdaLayerId::PadsThrough : EdaLayerId::TopCopper;
        out.pads.push_back({ pd.rxMm, pd.ryMm, pd.wMm, pd.hMm, 0.0f, pd.drillMm, pd.shape, lid, pinNm, L"" });
    }

    EdaComponent3D comp;
    comp.refDes = L"PART1";
    comp.value = PathUtils::Utf8ToWide(titleStr);
    comp.footprint = L"FRITZING_FZP";
    comp.widthMm = fp.widthMm;
    comp.lengthMm = fp.heightMm;
    comp.pinCount = static_cast<int>(fp.pads.size());
    comp.category = ClassifyComponent(comp.refDes, comp.value, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
    out.components.push_back(std::move(comp));
    out.netCount = std::max<size_t>(1, out.pads.size());
    return true;
}

inline bool ParseFritzingSketchXml(
    std::string_view fzXml,
    const std::unordered_map<std::string, FritzingPartSvgFootprint>& customFootprints,
    ParsedEdaBoard& out)
{
    for (unsigned char c : fzXml) {
        if (c == 0) return false;
    }
    if (fzXml.find("<module") == std::string_view::npos ||
        fzXml.find("<instances>") == std::string_view::npos ||
        fzXml.find("</module>") == std::string_view::npos)
    {
        return false;
    }

    // Fritzing scene coordinates are in 90-DPI pixels (1 px = 25.4 / 90 mm = 0.282222 mm)
    constexpr float kPxToMm = 25.4f / 90.0f;

    struct FzWire {
        float x = 0, y = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        float widthMm = 0.40f;
        bool bottomLayer = false;
        bool isSch = false;
        std::string c0TargetModel, c0TargetConn;
        std::string c1TargetModel, c1TargetConn;
    };
    struct FzPart {
        std::string modelIndex;
        std::string moduleId;
        std::string title;
        std::string valueProp;
        std::string pkgProp;
        float xMm = 0.0f, yMm = 0.0f, rotDeg = 0.0f;
        float boardWMm = 0.0f, boardHMm = 0.0f;
        bool bottomSide = false;
        bool hasPcbPos = false, hasAnyPos = false;
        std::set<std::string> usedConnectorIds;
        std::vector<std::tuple<std::string, std::string, std::string>> connects; // (myConn, dstModel, dstConn)
    };

    std::vector<FzPart> parts;
    std::vector<FzWire> wires;
    bool anyPcbGeometry = false;

    size_t pos = 0;
    while ((pos = fzXml.find("<instance ", pos)) != std::string_view::npos && (parts.size() + wires.size()) < 4096) {
        size_t instEnd = fzXml.find("</instance>", pos);
        if (instEnd == std::string_view::npos) return false; // Truncated XML!
        std::string_view blk = fzXml.substr(pos, instEnd + 11 - pos);
        pos = instEnd + 11;

        size_t hdrGt = blk.find('>');
        if (hdrGt == std::string_view::npos) continue;
        std::string_view instHdr = blk.substr(0, hdrGt);
        std::string_view modIdSv, modIdxSv;
        GetXmlAttr(instHdr, "moduleIdRef", modIdSv);
        GetXmlAttr(instHdr, "modelIndex", modIdxSv);
        std::string modId(modIdSv);
        std::string modIdx(modIdxSv);

        // Extract <title>...</title>
        std::string title;
        size_t t1 = blk.find("<title>");
        size_t t2 = (t1 != std::string_view::npos) ? blk.find("</title>", t1 + 7) : std::string_view::npos;
        if (t1 != std::string_view::npos && t2 != std::string_view::npos) {
            title = std::string(blk.substr(t1 + 7, t2 - (t1 + 7)));
        }

        // Properties
        std::string valProp, pkgProp;
        float propWMm = 0.0f, propHMm = 0.0f, holeD = 0.4f;
        size_t pp = 0;
        while ((pp = blk.find("<property ", pp)) != std::string_view::npos) {
            size_t pgt = blk.find('>', pp);
            if (pgt == std::string_view::npos) break;
            std::string_view ptag = blk.substr(pp, pgt - pp);
            pp = pgt + 1;
            std::string_view pn, pv;
            GetXmlAttr(ptag, "name", pn);
            GetXmlAttr(ptag, "value", pv);
            if (DetailSiliconAndBusEda::ContainsAsciiI(pn, "package")) pkgProp = std::string(pv);
            else if (DetailSiliconAndBusEda::ContainsAsciiI(pn, "width")) propWMm = ParseSvgDimMm(pv, 1.0f);
            else if (DetailSiliconAndBusEda::ContainsAsciiI(pn, "height")) propHMm = ParseSvgDimMm(pv, 1.0f);
            else if (DetailSiliconAndBusEda::ContainsAsciiI(pn, "hole size")) holeD = std::max(0.2f, std::strtof(std::string(pv).c_str(), nullptr));
            else if (valProp.empty() &&
                     (DetailSiliconAndBusEda::ContainsAsciiI(pn, "resistance") ||
                      DetailSiliconAndBusEda::ContainsAsciiI(pn, "capacitance") ||
                      DetailSiliconAndBusEda::ContainsAsciiI(pn, "voltage") ||
                      DetailSiliconAndBusEda::ContainsAsciiI(pn, "chip label") ||
                      DetailSiliconAndBusEda::ContainsAsciiI(pn, "part number") ||
                      DetailSiliconAndBusEda::ContainsAsciiI(pn, "type")))
            {
                valProp = std::string(pv);
            }
        }

        // Helper to extract <pcbView> (preferred) or <schematicView> / <breadboardView>
        auto findViewBlock = [&](const char* vTag) -> std::string_view {
            std::string openT = std::string("<") + vTag;
            std::string closeT = std::string("</") + vTag + ">";
            size_t vs = blk.find(openT);
            if (vs == std::string_view::npos) return {};
            size_t ve = blk.find(closeT, vs);
            if (ve == std::string_view::npos) return {};
            return blk.substr(vs, ve + closeT.size() - vs);
        };

        std::string_view viewBlk = findViewBlock("pcbView");
        bool usedPcb = !viewBlk.empty();
        if (viewBlk.empty()) viewBlk = findViewBlock("schematicView");
        if (viewBlk.empty()) viewBlk = findViewBlock("breadboardView");
        if (viewBlk.empty()) continue;

        size_t gPos = viewBlk.find("<geometry ");
        if (gPos == std::string_view::npos) continue;
        size_t gGt = viewBlk.find('>', gPos);
        if (gGt == std::string_view::npos) continue;
        std::string_view gTag = viewBlk.substr(gPos, gGt - gPos);

        std::string_view lyrSv;
        GetXmlAttr(viewBlk.substr(0, viewBlk.find('>')), "layer", lyrSv);
        bool isBot = DetailSiliconAndBusEda::ContainsAsciiI(lyrSv, "copper0") || DetailSiliconAndBusEda::ContainsAsciiI(lyrSv, "bottom");

        const bool isWire = DetailSiliconAndBusEda::ContainsAsciiI(modId, "WireModuleID") ||
                            DetailSiliconAndBusEda::ContainsAsciiI(modId, "TraceModuleID") ||
                            viewBlk.find("<wireExtras") != std::string_view::npos;
        const bool isBoard = DetailSiliconAndBusEda::ContainsAsciiI(modId, "RectanglePCB") ||
                             DetailSiliconAndBusEda::ContainsAsciiI(modId, "PCBModuleID");
        const bool isVia   = DetailSiliconAndBusEda::ContainsAsciiI(modId, "ViaModuleID");

        if (isWire) {
            FzWire w;
            w.x  = GetXmlFloat(gTag, "x")  * kPxToMm;
            w.y  = -GetXmlFloat(gTag, "y") * kPxToMm;
            w.x1 = GetXmlFloat(gTag, "x1") * kPxToMm;
            w.y1 = -GetXmlFloat(gTag, "y1") * kPxToMm;
            w.x2 = GetXmlFloat(gTag, "x2") * kPxToMm;
            w.y2 = -GetXmlFloat(gTag, "y2") * kPxToMm;
            w.bottomLayer = isBot;
            w.isSch = !usedPcb;
            size_t wePos = viewBlk.find("<wireExtras ");
            if (wePos != std::string_view::npos) {
                size_t weGt = viewBlk.find('>', wePos);
                if (weGt != std::string_view::npos) {
                    float mils = GetXmlFloat(viewBlk.substr(wePos, weGt - wePos), "mils", 16.0f);
                    w.widthMm = std::clamp(mils * 0.0254f, 0.15f, 3.0f);
                }
            }
            if (usedPcb) anyPcbGeometry = true;
            wires.push_back(std::move(w));
            continue;
        }

        if (isVia) {
            float vx = GetXmlFloat(gTag, "x") * kPxToMm;
            float vy = -GetXmlFloat(gTag, "y") * kPxToMm;
            out.vias.push_back({ vx, vy, std::max(0.8f, holeD * 1.8f), holeD, 0 });
            if (usedPcb) anyPcbGeometry = true;
            continue;
        }

        if (isBoard) {
            float bx = GetXmlFloat(gTag, "x") * kPxToMm;
            float by = -GetXmlFloat(gTag, "y") * kPxToMm;
            float bw = (propWMm > 1.0f) ? propWMm : std::max(20.0f, GetXmlFloat(gTag, "width", 50.0f) * kPxToMm);
            float bh = (propHMm > 1.0f) ? propHMm : std::max(15.0f, GetXmlFloat(gTag, "height", 40.0f) * kPxToMm);
            out.traces.push_back({ bx,      by,      bx + bw, by,      0.35f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ bx + bw, by,      bx + bw, by - bh, 0.35f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ bx + bw, by - bh, bx,      by - bh, 0.35f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ bx,      by - bh, bx,      by,      0.35f, EdaLayerId::EdgeCuts, 0 });
            if (usedPcb) anyPcbGeometry = true;
            continue;
        }

        FzPart fp;
        fp.modelIndex = modIdx;
        fp.moduleId = modId;
        fp.title = title.empty() ? ("U" + modIdx) : title;
        fp.valueProp = valProp;
        fp.pkgProp = pkgProp;
        fp.xMm = GetXmlFloat(gTag, "x") * kPxToMm;
        fp.yMm = -GetXmlFloat(gTag, "y") * kPxToMm;
        fp.bottomSide = isBot;
        fp.hasPcbPos = usedPcb;
        fp.hasAnyPos = true;
        if (usedPcb) anyPcbGeometry = true;

        // Parse 2D rotation from <transform m11="..." m12="..."/> if present
        size_t trPos = viewBlk.find("<transform ");
        if (trPos != std::string_view::npos) {
            size_t trGt = viewBlk.find('>', trPos);
            if (trGt != std::string_view::npos) {
                std::string_view trTag = viewBlk.substr(trPos, trGt - trPos);
                float m11 = GetXmlFloat(trTag, "m11", 1.0f);
                float m12 = GetXmlFloat(trTag, "m12", 0.0f);
                fp.rotDeg = static_cast<float>(std::atan2(m12, m11) * (180.0 / 3.14159265358979323846));
            }
        }

        // Parse <connectors><connector id="connector0">...<connect modelIndex="..." connectorId="..."/>
        size_t cp = 0;
        while ((cp = viewBlk.find("<connector ", cp)) != std::string_view::npos) {
            size_t cEnd = viewBlk.find("</connector>", cp);
            if (cEnd == std::string_view::npos) break;
            std::string_view cBlk = viewBlk.substr(cp, cEnd - cp);
            cp = cEnd + 12;
            std::string_view myCid;
            GetXmlAttr(cBlk.substr(0, cBlk.find('>')), "connectorId", myCid);
            if (myCid.empty()) GetXmlAttr(cBlk.substr(0, cBlk.find('>')), "id", myCid);
            if (myCid.empty()) continue;
            fp.usedConnectorIds.insert(std::string(myCid));

            size_t kp = 0;
            while ((kp = cBlk.find("<connect ", kp)) != std::string_view::npos) {
                size_t kgt = cBlk.find('>', kp);
                if (kgt == std::string_view::npos) break;
                std::string_view kTag = cBlk.substr(kp, kgt - kp);
                kp = kgt + 1;
                std::string_view dstMod, dstCid, kLyr;
                GetXmlAttr(kTag, "modelIndex", dstMod);
                GetXmlAttr(kTag, "connectorId", dstCid);
                GetXmlAttr(kTag, "layer", kLyr);
                if (!dstMod.empty() && !dstCid.empty()) {
                    fp.connects.emplace_back(std::string(myCid), std::string(dstMod), std::string(dstCid));
                }
            }
        }

        parts.push_back(std::move(fp));
    }

    if (parts.empty() && wires.empty() && out.traces.empty() && out.vias.empty()) {
        return false;
    }

    out.kind = anyPcbGeometry ? EdaDocKind::KiCadPcb : EdaDocKind::KiCadSch;
    out.formatTitle = L"Fritzing Electronics Sketch (.fzz / .fz)";

    // Emit wires / copper traces
    int wireNetSeq = 1;
    for (const auto& w : wires) {
        float ax = w.x + w.x1, ay = w.y + w.y1;
        float bx = w.x + w.x2, by = w.y + w.y2;
        if (std::hypot(bx - ax, by - ay) < 0.02f) continue;
        EdaLayerId lid = w.isSch ? EdaLayerId::SchWire : (w.bottomLayer ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper);
        out.traces.push_back({ ax, ay, bx, by, w.widthMm, lid, wireNetSeq++ });
    }

    // Place components & their copper/schematic pads
    std::set<std::string> uniqueNetPairs;
    for (const auto& p : parts) {
        const FritzingPartSvgFootprint* customFp = nullptr;
        auto itFp = customFootprints.find(p.moduleId);
        if (itFp != customFootprints.end() && !itFp->second.pads.empty()) {
            customFp = &itFp->second;
        }

        size_t pinCnt = customFp ? customFp->pads.size() : std::max<size_t>(2, p.usedConnectorIds.size());
        float wMm = customFp ? customFp->widthMm  : std::max(3.2f, static_cast<float>((pinCnt + 1) / 2) * 2.54f);
        float hMm = customFp ? customFp->heightMm : ((pinCnt > 4) ? 7.62f : 2.54f);
        float cx = p.xMm + wMm * 0.5f;
        float cy = p.yMm - hMm * 0.5f;

        if (customFp) {
            for (const auto& pd : customFp->pads) {
                EdaLayerId lid = (pd.drillMm > 0.05f) ? EdaLayerId::PadsThrough
                               : (p.bottomSide ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper);
                out.pads.push_back({ cx + pd.rxMm, cy + pd.ryMm, pd.wMm, pd.hMm, p.rotDeg, pd.drillMm,
                                     pd.shape, lid, PathUtils::Utf8ToWide(pd.id), L"" });
            }
        } else {
            bool smd = DetailSiliconAndBusEda::ContainsAsciiI(p.pkgProp, "0402") ||
                       DetailSiliconAndBusEda::ContainsAsciiI(p.pkgProp, "0603") ||
                       DetailSiliconAndBusEda::ContainsAsciiI(p.pkgProp, "0805") ||
                       DetailSiliconAndBusEda::ContainsAsciiI(p.pkgProp, "1206") ||
                       DetailSiliconAndBusEda::ContainsAsciiI(p.pkgProp, "SMD")  ||
                       DetailSiliconAndBusEda::ContainsAsciiI(p.pkgProp, "SOIC") ||
                       DetailSiliconAndBusEda::ContainsAsciiI(p.pkgProp, "SOT");
            size_t half = (pinCnt + 1) / 2;
            for (size_t i = 0; i < pinCnt; ++i) {
                float rx = 0.0f, ry = 0.0f;
                if (pinCnt == 2) {
                    rx = (i == 0) ? -wMm * 0.38f : wMm * 0.38f;
                    ry = 0.0f;
                } else {
                    bool topRow = (i >= half);
                    size_t col = topRow ? (pinCnt - 1 - i) : i;
                    rx = -wMm * 0.5f + 1.27f + static_cast<float>(col) * 2.54f;
                    ry = topRow ? (hMm * 0.5f - 0.5f) : (-hMm * 0.5f + 0.5f);
                }
                EdaLayerId lid = smd ? (p.bottomSide ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper)
                                     : EdaLayerId::PadsThrough;
                out.pads.push_back({ cx + rx, cy + ry, smd ? 1.1f : 1.4f, smd ? 1.3f : 1.4f, p.rotDeg,
                                     smd ? 0.0f : 0.8f, static_cast<uint8_t>(smd || i == 0 ? 1 : 0),
                                     lid, std::to_wstring(i + 1), L"" });
            }
        }

        // Silkscreen component outline + RefDes label
        float hw = wMm * 0.5f, hh = hMm * 0.5f;
        EdaLayerId silkL = p.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
        out.traces.push_back({ cx - hw, cy - hh, cx + hw, cy - hh, 0.18f, silkL, 0 });
        out.traces.push_back({ cx + hw, cy - hh, cx + hw, cy + hh, 0.18f, silkL, 0 });
        out.traces.push_back({ cx + hw, cy + hh, cx - hw, cy + hh, 0.18f, silkL, 0 });
        out.traces.push_back({ cx - hw, cy + hh, cx - hw, cy - hh, 0.18f, silkL, 0 });
        out.labels.push_back({ cx - hw, cy + hh + 0.6f, 0.95f, silkL, PathUtils::Utf8ToWide(p.title) });

        for (const auto& cn : p.connects) {
            std::string a = p.modelIndex + ":" + std::get<0>(cn);
            std::string b = std::get<1>(cn) + ":" + std::get<2>(cn);
            if (a > b) std::swap(a, b);
            uniqueNetPairs.insert(a + "->" + b);
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(p.title);
        comp.value = PathUtils::Utf8ToWide(p.valueProp.empty() ? p.moduleId : p.valueProp);
        comp.footprint = PathUtils::Utf8ToWide(p.pkgProp.empty() ? "FRITZING_PART" : p.pkgProp);
        comp.x = cx;
        comp.y = cy;
        comp.rotDeg = p.rotDeg;
        comp.widthMm = wMm;
        comp.lengthMm = hMm;
        comp.bottomSide = p.bottomSide;
        comp.pinCount = static_cast<int>(pinCnt);
        comp.category = ClassifyComponent(
            comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
        out.components.push_back(std::move(comp));
    }

    out.netCount = std::max<size_t>(1, !uniqueNetPairs.empty() ? uniqueNetPairs.size() : wires.size());
    return !out.traces.empty() || !out.pads.empty() || !out.components.empty();
}

inline bool ParseFritzingFile(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;

    // ZIP container (.fzz or .fzpz)
    if (size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

        std::string fzSketchXml;
        std::vector<std::string> fzpTexts;
        std::unordered_map<std::string, std::string> svgByName;

        for (const auto& e : entries) {
            if (e.isDirectory || e.uncompressedSize == 0 || e.uncompressedSize > 16 * 1024 * 1024) continue;
            std::string low = PathUtils::ToLowerAscii(PathUtils::WideToUtf8(e.pathInArchive));
            for (char& ch : low) if (ch == '\\') ch = '/';
            size_t sl = low.find_last_of('/');
            std::string base = (sl == std::string::npos) ? low : low.substr(sl + 1);

            if (low.size() >= 3 && low.substr(low.size() - 3) == ".fz") {
                std::vector<uint8_t> raw;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, raw, 16 * 1024 * 1024) && !raw.empty()) {
                    fzSketchXml.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
            } else if (low.size() >= 4 && low.substr(low.size() - 4) == ".fzp") {
                std::vector<uint8_t> raw;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, raw, 8 * 1024 * 1024) && !raw.empty()) {
                    fzpTexts.emplace_back(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
            } else if (low.size() >= 4 && low.substr(low.size() - 4) == ".svg") {
                std::vector<uint8_t> raw;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, raw, 8 * 1024 * 1024) && !raw.empty()) {
                    svgByName[base] = std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
            }
        }

        // Map moduleId -> custom PCB SVG footprint from bundled .fzp + .svg files
        std::unordered_map<std::string, FritzingPartSvgFootprint> customFps;
        for (const auto& fzp : fzpTexts) {
            size_t mPos = fzp.find("<module ");
            if (mPos == std::string::npos) continue;
            size_t mGt = fzp.find('>', mPos);
            if (mGt == std::string::npos) continue;
            std::string_view modIdSv;
            if (!GetXmlAttr(std::string_view(fzp).substr(mPos, mGt - mPos), "moduleId", modIdSv)) continue;

            // Find pcbView <layers image="pcb/foo.svg">
            size_t pv = fzp.find("<pcbView>");
            if (pv != std::string::npos) {
                size_t lyr = fzp.find("image=", pv);
                if (lyr != std::string::npos && lyr < fzp.find("</pcbView>", pv)) {
                    size_t q1 = fzp.find('"', lyr);
                    size_t q2 = (q1 != std::string::npos) ? fzp.find('"', q1 + 1) : std::string::npos;
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        std::string imgRef = PathUtils::ToLowerAscii(fzp.substr(q1 + 1, q2 - q1 - 1));
                        size_t sl = imgRef.find_last_of('/');
                        std::string stem = (sl == std::string::npos) ? imgRef : imgRef.substr(sl + 1);
                        for (const auto& skv : svgByName) {
                            if (skv.first.find(stem) != std::string::npos ||
                                (skv.first.find("pcb") != std::string::npos && svgByName.size() <= 4))
                            {
                                customFps[std::string(modIdSv)] = ParseFritzingPcbSvg(skv.second);
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (!fzSketchXml.empty()) {
            return ParseFritzingSketchXml(fzSketchXml, customFps, out);
        }
        if (!fzpTexts.empty()) {
            return ParseFritzingPartXmlAndSvg(fzpTexts[0], svgByName, out);
        }
        return false;
    }

    // Plain XML (.fz or .fzp)
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (extLower == L".fzp" || (sv.find("<connectors>") != std::string_view::npos && sv.find("<instances>") == std::string_view::npos)) {
        std::unordered_map<std::string, std::string> emptySvgs;
        return ParseFritzingPartXmlAndSvg(sv, emptySvgs, out);
    }
    std::unordered_map<std::string, FritzingPartSvgFootprint> emptyFps;
    return ParseFritzingSketchXml(sv, emptyFps, out);
}

} // namespace DetailBoardFabEda

inline bool ParseFritzingProjectBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 12) return false;
    return DetailBoardFabEda::ParseFritzingFile(data, size, extLower, out);
}

} // namespace Formats
