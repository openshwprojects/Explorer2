#pragma once
// ProtelAsciiPcb.h — Protel 99SE / Advanced PCB 3.0 Pipe-Record & Autotrax ASCII PCB (.protel) reader
// Reuses shared DetailAltium::ParsePipeRecord, ParsePcbCoordStrMm, and MapAltiumPcbLayerStr for
// Protel 99SE ASCII records (Board, Net, Component, Track, Pad, Via, Arc, Text) and also supports
// classic Protel Autotrax ("PCB FILE 4" / "PCB FILE 5") ASCII boards.

namespace Formats {

namespace DetailProtelAscii {

inline bool ParseAutotraxPcb(std::string_view sv, ParsedEdaBoard& out) {
    size_t pos = 0;
    std::string_view line;
    bool inComp = false;
    std::wstring compRef, compFp, compVal;
    float compX = 0.0f, compY = 0.0f;
    int compPads = 0;

    auto milsToMm = [](float mils) -> float { return mils * 0.0254f; };
    auto mapTraxLayer = [](int lyr) -> EdaLayerId {
        if (lyr == 1) return EdaLayerId::TopCopper;
        if (lyr == 6) return EdaLayerId::BottomCopper;
        if (lyr == 7 || lyr == 8) return EdaLayerId::TopSilk;
        if (lyr == 11) return EdaLayerId::EdgeCuts;
        if (lyr == 12) return EdaLayerId::PadsThrough;
        return EdaLayerId::InnerCopper;
    };

    while (ReaderText::NextLineSv(sv, pos, line)) {
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;
        if (ReaderText::AsciiEqCI(line, "ENDPCB")) break;

        if (ReaderText::AsciiEqCI(line, "COMP")) {
            inComp = true;
            compRef.clear(); compFp.clear(); compVal.clear();
            compX = 0.0f; compY = 0.0f; compPads = 0;
            std::string_view lRef, lFp, lVal, lPos;
            if (ReaderText::NextLineSv(sv, pos, lRef)) compRef = PathUtils::Utf8ToWide(ReaderText::TrimSv(lRef));
            if (ReaderText::NextLineSv(sv, pos, lFp))  compFp  = PathUtils::Utf8ToWide(ReaderText::TrimSv(lFp));
            if (ReaderText::NextLineSv(sv, pos, lVal)) compVal = PathUtils::Utf8ToWide(ReaderText::TrimSv(lVal));
            if (ReaderText::NextLineSv(sv, pos, lPos)) {
                std::string_view tx, ty;
                float rx = 0, ry = 0;
                if (ReaderText::NextTokenSv(lPos, tx) && ReaderText::ParseFloatSv(tx, rx) &&
                    ReaderText::NextTokenSv(lPos, ty) && ReaderText::ParseFloatSv(ty, ry)) {
                    compX = milsToMm(rx);
                    compY = milsToMm(ry);
                }
            }
            continue;
        }
        if (ReaderText::AsciiEqCI(line, "ENDCOMP")) {
            if (inComp && !compRef.empty()) {
                EdaComponent3D c;
                c.refDes = compRef;
                c.value = compVal;
                c.footprint = compFp;
                c.x = compX; c.y = compY;
                c.pinCount = std::max(2, compPads);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
                out.components.push_back(std::move(c));
            }
            inComp = false;
            continue;
        }

        std::string_view rem = line, cmd;
        if (!ReaderText::NextTokenSv(rem, cmd)) continue;
        if (cmd == "CT" || cmd == "FT") {
            // Track: CT x1 y1 x2 y2 width layer
            std::string_view tx1, ty1, tx2, ty2, tw, tl;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0, w = 10.0f;
            int lyr = 1;
            if (ReaderText::NextTokenSv(rem, tx1) && ReaderText::ParseFloatSv(tx1, x1) &&
                ReaderText::NextTokenSv(rem, ty1) && ReaderText::ParseFloatSv(ty1, y1) &&
                ReaderText::NextTokenSv(rem, tx2) && ReaderText::ParseFloatSv(tx2, x2) &&
                ReaderText::NextTokenSv(rem, ty2) && ReaderText::ParseFloatSv(ty2, y2)) {
                if (ReaderText::NextTokenSv(rem, tw)) ReaderText::ParseFloatSv(tw, w);
                if (ReaderText::NextTokenSv(rem, tl)) ReaderText::ParseIntSv(tl, lyr);
                out.traces.push_back({
                    milsToMm(x1), milsToMm(y1), milsToMm(x2), milsToMm(y2),
                    std::max(0.12f, milsToMm(w)), mapTraxLayer(lyr), 0
                });
            }
        } else if (cmd == "CP" || cmd == "FP") {
            // Pad: CP x y xsize ysize shape hole ...
            std::string_view tx, ty, tsx, tsy, tsh, thole;
            float x = 0, y = 0, sx = 60, sy = 60, hole = 30;
            int shp = 1;
            if (ReaderText::NextTokenSv(rem, tx)  && ReaderText::ParseFloatSv(tx, x) &&
                ReaderText::NextTokenSv(rem, ty)  && ReaderText::ParseFloatSv(ty, y) &&
                ReaderText::NextTokenSv(rem, tsx) && ReaderText::ParseFloatSv(tsx, sx) &&
                ReaderText::NextTokenSv(rem, tsy) && ReaderText::ParseFloatSv(tsy, sy)) {
                if (ReaderText::NextTokenSv(rem, tsh))   ReaderText::ParseIntSv(tsh, shp);
                if (ReaderText::NextTokenSv(rem, thole)) ReaderText::ParseFloatSv(thole, hole);
                EdaPadPrim p;
                p.x = milsToMm(x); p.y = milsToMm(y);
                p.w = std::max(0.3f, milsToMm(sx)); p.h = std::max(0.3f, milsToMm(sy));
                p.drill = std::max(0.0f, milsToMm(hole));
                p.shape = (shp == 2) ? 1 : 0;
                p.layer = (p.drill > 0.0f) ? EdaLayerId::PadsThrough : EdaLayerId::TopCopper;
                out.pads.push_back(std::move(p));
                if (inComp) ++compPads;
            }
        } else if (cmd == "FV" || cmd == "CV") {
            std::string_view tx, ty, tdia, thole;
            float x = 0, y = 0, dia = 50, hole = 25;
            if (ReaderText::NextTokenSv(rem, tx) && ReaderText::ParseFloatSv(tx, x) &&
                ReaderText::NextTokenSv(rem, ty) && ReaderText::ParseFloatSv(ty, y)) {
                if (ReaderText::NextTokenSv(rem, tdia))  ReaderText::ParseFloatSv(tdia, dia);
                if (ReaderText::NextTokenSv(rem, thole)) ReaderText::ParseFloatSv(thole, hole);
                out.vias.push_back({ milsToMm(x), milsToMm(y), milsToMm(dia), milsToMm(hole), 0 });
            }
        }
    }
    if (out.traces.empty() && out.pads.empty() && out.components.empty()) return false;
    out.kind = EdaDocKind::EagleBrd;
    wchar_t title[128] = {};
    swprintf_s(title, L"Protel Autotrax PCB (%zu parts, %zu pads, %zu tracks)",
               out.components.size(), out.pads.size(), out.traces.size());
    out.formatTitle = title;
    return true;
}

} // namespace DetailProtelAscii

inline bool ParseProtelAsciiPcb(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 16 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    std::string_view trimmed = ReaderText::TrimSv(sv);
    if (ReaderText::StartsWithCI(trimmed, "PCB FILE ")) {
        return DetailProtelAscii::ParseAutotraxPcb(trimmed, out);
    }

    if (sv.find("|RECORD=") == std::string_view::npos && sv.find("RECORD=") == std::string_view::npos) {
        return false;
    }

    bool sawBoardHeader = false;
    std::vector<EdaComponent3D> comps;
    size_t netCount = 0;

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        line = ReaderText::TrimSv(line);
        if (line.empty() || line.find('=') == std::string_view::npos) continue;

        const DetailAltium::KvMap kv = DetailAltium::ParsePipeRecord(line);
        const std::string rec = PathUtils::ToLowerAscii(DetailAltium::GetKvStr(kv, "record"));
        if (rec.empty()) continue;

        if (rec == "board") {
            sawBoardHeader = true;
        } else if (rec == "net") {
            ++netCount;
        } else if (rec == "component") {
            EdaComponent3D c;
            c.refDes = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "sourcedesig"));
            if (c.refDes.empty()) c.refDes = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "name"));
            if (c.refDes.empty()) c.refDes = L"U" + std::to_wstring(comps.size() + 1);
            c.footprint = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "pattern"));
            c.value = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "comment"));
            c.x = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
            c.y = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
            c.rotDeg = static_cast<float>(std::strtod(DetailAltium::GetKvStr(kv, "rotation").c_str(), nullptr));
            const EdaLayerId lyr = DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer"));
            c.bottomSide = (lyr == EdaLayerId::BottomCopper || lyr == EdaLayerId::BottomSilk);
            c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
            out.labels.push_back({ c.x, c.y + c.lengthMm * 0.55f, 0.9f,
                                   c.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, c.refDes });
            comps.push_back(std::move(c));
        } else if (rec == "track") {
            const float x1 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x1"));
            const float y1 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y1"));
            const float x2 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x2"));
            const float y2 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y2"));
            const float w  = std::max(0.12f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "width"), 0.25f));
            const EdaLayerId lyr = DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer"));
            const int net = DetailAltium::GetKvInt(kv, "net", -1) + 1;
            out.traces.push_back({ x1, y1, x2, y2, w, lyr, net });
        } else if (rec == "pad") {
            EdaPadPrim p;
            p.x = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
            p.y = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
            p.w = std::max(0.3f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "xsize"), 1.4f));
            p.h = std::max(0.3f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "ysize"), 1.4f));
            p.drill = std::max(0.0f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "holesize"), 0.0f));
            p.rotDeg = static_cast<float>(std::strtod(DetailAltium::GetKvStr(kv, "rotation").c_str(), nullptr));
            const std::string shp = PathUtils::ToLowerAscii(DetailAltium::GetKvStr(kv, "shape"));
            p.shape = (shp.find("rect") != std::string::npos) ? 1
                    : (shp.find("oct") != std::string::npos)  ? 2 : 0;
            p.layer = (p.drill > 0.0f) ? EdaLayerId::PadsThrough
                                       : DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer"));
            p.pinName = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "name"));
            out.pads.push_back(std::move(p));
        } else if (rec == "via") {
            const float vx = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
            const float vy = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
            const float dia = std::max(0.4f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "diameter"), 0.8f));
            const float drl = std::max(0.2f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "holesize"), 0.4f));
            const int net = DetailAltium::GetKvInt(kv, "net", -1) + 1;
            out.vias.push_back({ vx, vy, dia, drl, net });
        } else if (rec == "arc") {
            std::string sx = DetailAltium::GetKvStr(kv, "location.x");
            if (sx.empty()) sx = DetailAltium::GetKvStr(kv, "x");
            std::string sy = DetailAltium::GetKvStr(kv, "location.y");
            if (sy.empty()) sy = DetailAltium::GetKvStr(kv, "y");
            const float cx  = DetailAltium::ParsePcbCoordStrMm(sx);
            const float cy  = DetailAltium::ParsePcbCoordStrMm(sy);
            const float rad = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "radius"));
            const float w   = std::max(0.15f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "width"), 0.2f));
            const EdaLayerId lyr = DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer"));
            if (rad > 0.05f) out.circles.push_back({ cx, cy, rad, w, false, lyr });
        }
    }

    out.components = std::move(comps);
    if (!sawBoardHeader && out.traces.empty() && out.pads.empty() && out.vias.empty()) return false;
    if (out.traces.empty() && out.pads.empty() && out.components.empty() && out.vias.empty()) return false;

    out.kind = EdaDocKind::EagleBrd;
    out.netCount = netCount;
    wchar_t title[144] = {};
    swprintf_s(title, L"Protel 99SE ASCII PCB (%zu parts, %zu pads, %zu tracks)",
               out.components.size(), out.pads.size(), out.traces.size());
    out.formatTitle = title;
    return true;
}

} // namespace Formats
