#pragma once
// Ipc2581.h — IPC-2581 Rev B/C Digital Twin PCB XML (.cvg / .ipc2581) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailBoardFabEda {

// ============================================================================
// IPC-2581 Rev B / C XML Parser (.cvg / .ipc2581)
// ============================================================================
inline bool ParseIpc2581RevBC(std::string_view sv, ParsedEdaBoard& out) {
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }
    if (sv.find("<IPC-2581") == std::string_view::npos && sv.find("<Ecad") == std::string_view::npos) {
        return false;
    }
    // Strict closing-tag check so truncated XML files are rejected cleanly
    if (sv.find("</IPC-2581>") == std::string_view::npos && sv.find("</Ecad>") == std::string_view::npos &&
        sv.find(":IPC-2581>") == std::string_view::npos) {
        return false;
    }

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"IPC-2581 Digital Twin PCB (.cvg / .ipc2581)";

    float unitScale = 1.0f; // MILLIMETER default
    struct StdPrim { float w = 1.0f; float h = 1.0f; uint8_t shape = 0; };
    struct PkgBox { float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f; bool valid = false; };

    std::unordered_map<std::string, float> lineDescMap;
    std::unordered_map<std::string, StdPrim> stdPrimMap;
    std::unordered_map<std::string, float> padstackHoleMap;
    std::unordered_map<std::string, PkgBox> pkgBoxMap;
    std::unordered_map<std::string, EdaLayerId> layerMap;
    std::set<std::string> netNames;

    std::string curEntryLineId, curEntryStdId, curPadstackDefId, curPackageName, revStr;
    EdaLayerId activeLayer = EdaLayerId::TopCopper;
    float activeLineWidth = 0.25f;
    int activeNetId = 0;
    bool setIsVia = false;

    bool inProfile = false;
    bool inPoly = false;
    bool inCutout = false;
    std::vector<std::pair<float, float>> polyPts;
    EdaLayerId polyLayer = EdaLayerId::EdgeCuts;

    bool inComponent = false;
    EdaComponent3D curComp;
    bool compHasLoc = false;

    bool inPad = false;
    bool padIsVia = false;
    EdaPadPrim curPad;
    bool padHasLoc = false;

    auto trackPkgPt = [&](float xMm, float yMm) {
        if (curPackageName.empty()) return;
        PkgBox& b = pkgBoxMap[curPackageName];
        b.minX = std::min(b.minX, xMm); b.maxX = std::max(b.maxX, xMm);
        b.minY = std::min(b.minY, yMm); b.maxY = std::max(b.maxY, yMm);
        b.valid = true;
    };

    size_t pos = 0;
    while (pos < sv.size() && out.traces.size() < 55000) {
        size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos) break;
        if (lt + 4 <= sv.size() && sv.substr(lt, 4) == "<!--") {
            size_t ce = sv.find("-->", lt + 4);
            if (ce == std::string_view::npos) return false;
            pos = ce + 3;
            continue;
        }
        size_t gt = sv.find('>', lt + 1);
        if (gt == std::string_view::npos) return false;
        std::string_view rawTag = sv.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        if (rawTag.empty() || rawTag[0] == '?' || rawTag[0] == '!') continue;

        bool isClosing = (rawTag[0] == '/');
        bool selfClosing = (!isClosing && rawTag.back() == '/');
        std::string_view tag = isClosing ? rawTag.substr(1) : (selfClosing ? rawTag.substr(0, rawTag.size() - 1) : rawTag);
        while (!tag.empty() && (tag.front() == ' ' || tag.front() == '\t')) tag.remove_prefix(1);

        size_t nameEnd = tag.find_first_of(" \t\r\n/");
        std::string_view tname = (nameEnd == std::string_view::npos) ? tag : tag.substr(0, nameEnd);
        size_t colon = tname.find(':');
        if (colon != std::string_view::npos) tname = tname.substr(colon + 1);

        if (isClosing) {
            if (tname == "EntryLineDesc") curEntryLineId.clear();
            else if (tname == "EntryStandard") curEntryStdId.clear();
            else if (tname == "PadstackDef" || tname == "PadStackDef") curPadstackDefId.clear();
            else if (tname == "Package") curPackageName.clear();
            else if (tname == "Set") setIsVia = false;
            else if (tname == "Profile") inProfile = false;
            else if (tname == "Cutout") inCutout = false;
            else if (tname == "Polygon") {
                if (curPackageName.empty() && polyPts.size() >= 3) {
                    if (std::hypot(polyPts.back().first - polyPts.front().first, polyPts.back().second - polyPts.front().second) > 1e-3f) {
                        out.traces.push_back({ polyPts.back().first, polyPts.back().second,
                                               polyPts.front().first, polyPts.front().second,
                                               (polyLayer == EdaLayerId::EdgeCuts) ? 0.30f : 0.20f, polyLayer, activeNetId });
                    }
                    if (!inProfile && !inCutout &&
                        (polyLayer == EdaLayerId::TopCopper || polyLayer == EdaLayerId::BottomCopper || polyLayer == EdaLayerId::InnerCopper))
                    {
                        HatchPolygonHorizontal(polyPts, polyLayer, activeNetId, out);
                    }
                }
                polyPts.clear();
                inPoly = false;
            } else if (tname == "Component") {
                if (inComponent && (!curComp.refDes.empty() || compHasLoc)) {
                    std::string pkgKey = PathUtils::WideToUtf8(curComp.footprint);
                    if (pkgBoxMap.count(pkgKey) && pkgBoxMap[pkgKey].valid) {
                        curComp.widthMm  = std::clamp(pkgBoxMap[pkgKey].maxX - pkgBoxMap[pkgKey].minX, 0.8f, 60.0f);
                        curComp.lengthMm = std::clamp(pkgBoxMap[pkgKey].maxY - pkgBoxMap[pkgKey].minY, 0.8f, 60.0f);
                    }
                    curComp.category = ClassifyComponent(
                        curComp.refDes, curComp.footprint, curComp.widthMm, curComp.lengthMm, curComp.heightMm, curComp.pinCount);
                    out.components.push_back(curComp);
                }
                inComponent = false;
            } else if (tname == "Pad" || tname == "Via") {
                if (inPad && padHasLoc) {
                    if (padIsVia) {
                        float d = std::max(0.2f, curPad.drill > 0.0f ? curPad.drill : curPad.w * 0.5f);
                        out.vias.push_back({ curPad.x, curPad.y, std::max(curPad.w, d * 1.6f), d, activeNetId });
                    } else {
                        out.pads.push_back(curPad);
                    }
                }
                inPad = false;
                padIsVia = false;
            }
            continue;
        }

        if (tname == "IPC-2581") {
            std::string_view rv;
            if (GetXmlAttr(tag, "revision", rv)) revStr = std::string(rv);
        } else if (tname == "CadHeader" || tname == "Units") {
            std::string_view u;
            if (GetXmlAttr(tag, "units", u)) {
                if (DetailSiliconAndBusEda::ContainsAsciiI(u, "INCH")) unitScale = 25.4f;
                else if (DetailSiliconAndBusEda::ContainsAsciiI(u, "MICRON")) unitScale = 0.001f;
                else if (DetailSiliconAndBusEda::ContainsAsciiI(u, "MIL") && !DetailSiliconAndBusEda::ContainsAsciiI(u, "MILLI")) unitScale = 0.0254f;
                else unitScale = 1.0f;
            }
        } else if (tname == "EntryLineDesc") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && !selfClosing) curEntryLineId = std::string(id);
        } else if (tname == "LineDesc") {
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            if (lw > 0.0f) {
                float wMm = std::max(0.05f, lw * unitScale);
                if (!curEntryLineId.empty()) lineDescMap[curEntryLineId] = wMm;
                activeLineWidth = wMm;
            }
        } else if (tname == "LineDescRef") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && lineDescMap.count(std::string(id))) {
                activeLineWidth = lineDescMap[std::string(id)];
            }
        } else if (tname == "EntryStandard") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && !selfClosing) curEntryStdId = std::string(id);
        } else if (!curEntryStdId.empty() &&
                   (tname == "Circle" || tname == "RectCenter" || tname == "RectRound" ||
                    tname == "Oval" || tname == "Square" || tname == "Donut" || tname == "Thermal"))
        {
            StdPrim sp;
            if (tname == "Circle" || tname == "Donut" || tname == "Thermal") {
                float d = GetXmlFloat(tag, "diameter", GetXmlFloat(tag, "outerDiameter", 1.0f)) * unitScale;
                sp = { std::max(0.15f, d), std::max(0.15f, d), 0 };
            } else if (tname == "Square") {
                float s = std::max(0.15f, GetXmlFloat(tag, "size", 1.0f) * unitScale);
                sp = { s, s, 1 };
            } else {
                float w = std::max(0.15f, GetXmlFloat(tag, "width", 1.0f) * unitScale);
                float h = std::max(0.15f, GetXmlFloat(tag, "height", 1.0f) * unitScale);
                uint8_t sh = (tname == "Oval") ? 3 : ((tname == "RectRound") ? 2 : 1);
                sp = { w, h, sh };
            }
            stdPrimMap[curEntryStdId] = sp;
        } else if (tname == "PadstackDef" || tname == "PadStackDef") {
            std::string_view nm;
            if (GetXmlAttr(tag, "name", nm) && !selfClosing) curPadstackDefId = std::string(nm);
        } else if (!curPadstackDefId.empty() && (tname == "PadstackHoleDef" || tname == "PadStackHoleDef")) {
            float hd = GetXmlFloat(tag, "diameter", 0.0f) * unitScale;
            if (hd > 0.05f) padstackHoleMap[curPadstackDefId] = hd;
        } else if (tname == "Package") {
            std::string_view nm;
            if (GetXmlAttr(tag, "name", nm) && !selfClosing) curPackageName = std::string(nm);
        } else if (tname == "Layer") {
            std::string_view nm, fn, sd;
            if (GetXmlAttr(tag, "name", nm)) {
                GetXmlAttr(tag, "layerFunction", fn);
                GetXmlAttr(tag, "side", sd);
                layerMap[std::string(nm)] = DetailSiliconAndBusEda::ClassifyIpcLayerName(nm, fn, sd);
            }
        } else if (tname == "Profile") {
            if (!selfClosing) inProfile = true;
        } else if (tname == "Cutout") {
            if (!selfClosing) inCutout = true;
        } else if (tname == "Polygon") {
            if (!selfClosing) {
                inPoly = true;
                polyPts.clear();
                polyLayer = inProfile ? EdaLayerId::EdgeCuts : activeLayer;
            }
        } else if (inPoly && tname == "PolyBegin") {
            float px = GetXmlFloat(tag, "x") * unitScale;
            float py = GetXmlFloat(tag, "y") * unitScale;
            if (!curPackageName.empty()) trackPkgPt(px, py);
            else polyPts.push_back({ px, py });
        } else if (inPoly && tname == "PolyStepSegment") {
            float nx = GetXmlFloat(tag, "x") * unitScale;
            float ny = GetXmlFloat(tag, "y") * unitScale;
            if (!curPackageName.empty()) {
                trackPkgPt(nx, ny);
            } else if (!polyPts.empty()) {
                float w = (polyLayer == EdaLayerId::EdgeCuts) ? 0.30f : std::max(0.12f, activeLineWidth);
                out.traces.push_back({ polyPts.back().first, polyPts.back().second, nx, ny, w, polyLayer, activeNetId });
                polyPts.push_back({ nx, ny });
            }
        } else if (inPoly && tname == "PolyStepCurve") {
            float nx = GetXmlFloat(tag, "x") * unitScale;
            float ny = GetXmlFloat(tag, "y") * unitScale;
            float cx = GetXmlFloat(tag, "centerX") * unitScale;
            float cy = GetXmlFloat(tag, "centerY") * unitScale;
            std::string_view cwSv;
            bool cw = GetXmlAttr(tag, "clockwise", cwSv) && DetailSiliconAndBusEda::ContainsAsciiI(cwSv, "true");
            if (!curPackageName.empty()) {
                trackPkgPt(nx, ny);
            } else if (!polyPts.empty()) {
                float w = (polyLayer == EdaLayerId::EdgeCuts) ? 0.30f : std::max(0.12f, activeLineWidth);
                DetailSiliconAndBusEda::AddTessellatedArc(out, polyPts.back().first, polyPts.back().second, nx, ny, cx, cy, cw, w, polyLayer, activeNetId);
                polyPts.push_back({ nx, ny });
            }
        } else if (tname == "LayerFeature") {
            std::string_view lref;
            if (GetXmlAttr(tag, "layerRef", lref)) {
                auto it = layerMap.find(std::string(lref));
                activeLayer = (it != layerMap.end()) ? it->second : DetailSiliconAndBusEda::ClassifyIpcLayerName(lref);
            }
        } else if (tname == "Set" || tname == "LogicalNet") {
            std::string_view netSv, usageSv;
            if (GetXmlAttr(tag, "padUsage", usageSv)) {
                setIsVia = DetailSiliconAndBusEda::ContainsAsciiI(usageSv, "VIA");
            }
            if (GetXmlAttr(tag, "net", netSv) && !netSv.empty() && netSv != "No Net") {
                netNames.insert(std::string(netSv));
                activeNetId = static_cast<int>(netNames.size());
            } else if (tname == "LogicalNet" && GetXmlAttr(tag, "name", netSv) && !netSv.empty()) {
                netNames.insert(std::string(netSv));
            }
        } else if (tname == "Line" && curPackageName.empty()) {
            float x1 = GetXmlFloat(tag, "startX") * unitScale;
            float y1 = GetXmlFloat(tag, "startY") * unitScale;
            float x2 = GetXmlFloat(tag, "endX") * unitScale;
            float y2 = GetXmlFloat(tag, "endY") * unitScale;
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            float w = (lw > 0.0f) ? (lw * unitScale) : activeLineWidth;
            EdaLayerId lid = inProfile ? EdaLayerId::EdgeCuts : activeLayer;
            out.traces.push_back({ x1, y1, x2, y2, std::max(0.08f, w), lid, activeNetId });
        } else if (tname == "Arc" && curPackageName.empty()) {
            float xs = GetXmlFloat(tag, "startX") * unitScale;
            float ys = GetXmlFloat(tag, "startY") * unitScale;
            float xe = GetXmlFloat(tag, "endX") * unitScale;
            float ye = GetXmlFloat(tag, "endY") * unitScale;
            float xc = GetXmlFloat(tag, "centerX") * unitScale;
            float yc = GetXmlFloat(tag, "centerY") * unitScale;
            std::string_view cwSv;
            bool cw = GetXmlAttr(tag, "clockwise", cwSv) && DetailSiliconAndBusEda::ContainsAsciiI(cwSv, "true");
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            float w = (lw > 0.0f) ? (lw * unitScale) : activeLineWidth;
            DetailSiliconAndBusEda::AddTessellatedArc(out, xs, ys, xe, ye, xc, yc, cw, std::max(0.08f, w),
                inProfile ? EdaLayerId::EdgeCuts : activeLayer, activeNetId);
        } else if ((tname == "Pad" || tname == "Via") && curPackageName.empty() && curPadstackDefId.empty()) {
            curPad = EdaPadPrim{};
            curPad.w = 1.0f; curPad.h = 1.0f; curPad.shape = 1;
            curPad.layer = (activeLayer == EdaLayerId::Drills) ? EdaLayerId::PadsThrough : activeLayer;
            curPad.rotDeg = GetXmlFloat(tag, "rotation", 0.0f);
            padIsVia = (tname == "Via") || setIsVia;
            std::string_view psRef;
            if (GetXmlAttr(tag, "padstackDefRef", psRef)) {
                curPad.pinName = PathUtils::Utf8ToWide(psRef);
                auto itH = padstackHoleMap.find(std::string(psRef));
                if (itH != padstackHoleMap.end()) {
                    curPad.drill = itH->second;
                    curPad.layer = EdaLayerId::PadsThrough;
                }
            }
            std::string_view xSv, ySv;
            if (GetXmlAttr(tag, "x", xSv) && GetXmlAttr(tag, "y", ySv)) {
                curPad.x = GetXmlFloat(tag, "x") * unitScale;
                curPad.y = GetXmlFloat(tag, "y") * unitScale;
                padHasLoc = true;
            } else {
                padHasLoc = false;
            }
            if (selfClosing) {
                if (padHasLoc) {
                    if (padIsVia) out.vias.push_back({ curPad.x, curPad.y, curPad.w, std::max(0.25f, curPad.drill), activeNetId });
                    else out.pads.push_back(curPad);
                }
                padIsVia = false;
            } else {
                inPad = true;
            }
        } else if (inPad && tname == "StandardPrimitiveRef") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && stdPrimMap.count(std::string(id))) {
                const auto& sp = stdPrimMap[std::string(id)];
                curPad.w = sp.w; curPad.h = sp.h; curPad.shape = sp.shape;
            }
        } else if (inPad && tname == "PinRef") {
            std::string_view pinSv;
            if (GetXmlAttr(tag, "pin", pinSv)) curPad.pinName = PathUtils::Utf8ToWide(pinSv);
        } else if (tname == "Hole" && curPackageName.empty()) {
            float d = std::max(0.15f, GetXmlFloat(tag, "diameter", 0.4f) * unitScale);
            std::string_view plSv;
            bool nonPlated = GetXmlAttr(tag, "platingStatus", plSv) && DetailSiliconAndBusEda::ContainsAsciiI(plSv, "NONPLATED");
            if (inPad) {
                curPad.drill = d;
                curPad.layer = EdaLayerId::PadsThrough;
            } else {
                float hx = GetXmlFloat(tag, "x") * unitScale;
                float hy = GetXmlFloat(tag, "y") * unitScale;
                out.vias.push_back({ hx, hy, nonPlated ? d * 1.15f : d * 1.8f, d, activeNetId });
            }
        } else if (tname == "Component") {
            curComp = EdaComponent3D{};
            compHasLoc = false;
            std::string_view refSv, partSv, pkgSv, lyrSv;
            if (GetXmlAttr(tag, "refDes", refSv)) curComp.refDes = PathUtils::Utf8ToWide(refSv);
            if (GetXmlAttr(tag, "part", partSv)) curComp.value = PathUtils::Utf8ToWide(partSv);
            if (GetXmlAttr(tag, "packageRef", pkgSv)) curComp.footprint = PathUtils::Utf8ToWide(pkgSv);
            if (GetXmlAttr(tag, "layerRef", lyrSv)) {
                EdaLayerId cl = DetailSiliconAndBusEda::ClassifyIpcLayerName(lyrSv);
                curComp.bottomSide = (cl == EdaLayerId::BottomCopper || cl == EdaLayerId::BottomSilk);
            }
            curComp.rotDeg = GetXmlFloat(tag, "rotation", 0.0f);
            std::string_view xSv, ySv;
            if (GetXmlAttr(tag, "x", xSv) && GetXmlAttr(tag, "y", ySv)) {
                curComp.x = GetXmlFloat(tag, "x") * unitScale;
                curComp.y = GetXmlFloat(tag, "y") * unitScale;
                compHasLoc = true;
            }
            if (selfClosing) {
                if (!curComp.refDes.empty() && compHasLoc) {
                    curComp.category = ClassifyComponent(
                        curComp.refDes, curComp.footprint, curComp.widthMm, curComp.lengthMm, curComp.heightMm, curComp.pinCount);
                    out.components.push_back(curComp);
                }
            } else {
                inComponent = true;
            }
        } else if (tname == "Location") {
            float lx = GetXmlFloat(tag, "x") * unitScale;
            float ly = GetXmlFloat(tag, "y") * unitScale;
            if (inPad) { curPad.x = lx; curPad.y = ly; padHasLoc = true; }
            else if (inComponent) { curComp.x = lx; curComp.y = ly; compHasLoc = true; }
        } else if (tname == "Xform") {
            float rot = GetXmlFloat(tag, "rotation", -9999.0f);
            std::string_view mirSv;
            bool mir = GetXmlAttr(tag, "mirror", mirSv) && DetailSiliconAndBusEda::ContainsAsciiI(mirSv, "true");
            if (inPad && rot > -9000.0f) curPad.rotDeg = rot;
            if (inComponent) {
                if (rot > -9000.0f) curComp.rotDeg = rot;
                if (mir) curComp.bottomSide = true;
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty()) {
        return false;
    }
    if (!revStr.empty()) {
        out.formatTitle = L"IPC-2581 Rev " + PathUtils::Utf8ToWide(revStr) + L" Digital Twin PCB";
    }
    out.netCount = std::max<size_t>(1, netNames.empty() ? out.components.size() : netNames.size());
    return true;
}

} // namespace DetailBoardFabEda

inline bool ParseIpc2581Board(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.size() < 12) return false;
    return DetailBoardFabEda::ParseIpc2581RevBC(sv, out);
}

} // namespace Formats
