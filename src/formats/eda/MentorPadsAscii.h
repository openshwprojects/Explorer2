#pragma once
// MentorPadsAscii.h — Mentor Graphics / Siemens PADS Layout ASCII (.pads, *PADS-PCB*) reader
// Parses MILS/METRIC/INCHES/BASIC units, *PARTDECAL* footprint terminals & padstacks, *PART*
// component placements (top & mirrored bottom), *LINES*/*BOARD* outlines, and *SIGNAL*/*ROUTE*
// multi-layer copper tracks and layer-transition vias.

namespace Formats {

namespace DetailMentorPads {

struct PadsPin {
    float rx = 0.0f;
    float ry = 0.0f;
    float wMm = 1.2f;
    float hMm = 1.2f;
    float drillMm = 0.0f;
    uint8_t shape = 0; // 0=circle, 1=rect, 3=oval
    std::wstring pinNum;
};

struct PadsDecal {
    std::vector<PadsPin> pins;
};

inline float UnitScaleToMm(std::string_view tok, float curScale) {
    if (ReaderText::AsciiEqCI(tok, "METRIC") || ReaderText::AsciiEqCI(tok, "MM")) return 1.0f;
    if (ReaderText::AsciiEqCI(tok, "MILS") || ReaderText::AsciiEqCI(tok, "MIL")) return 0.0254f;
    if (ReaderText::AsciiEqCI(tok, "INCHES") || ReaderText::AsciiEqCI(tok, "INCH")) return 25.4f;
    if (ReaderText::AsciiEqCI(tok, "BASIC")) return 1.0f / 1500.0f;
    return curScale;
}

inline EdaLayerId MapPadsLayer(int lyr) {
    if (lyr <= 1) return EdaLayerId::TopCopper;
    if (lyr == 2) return EdaLayerId::BottomCopper;
    if (lyr >= 26 && lyr <= 29) return EdaLayerId::TopSilk;
    return EdaLayerId::InnerCopper;
}

} // namespace DetailMentorPads

inline bool ParseMentorPadsAscii(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 20 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    if (sv.find("*PADS-") == std::string_view::npos && sv.find("!PADS-") == std::string_view::npos) {
        return false;
    }

    float scaleMm = 0.0254f; // Default PADS unit is MILS
    enum class Sec { None, PartDecal, Part, Lines, Route, Signal, Other };
    Sec sec = Sec::None;

    std::unordered_map<std::string, DetailMentorPads::PadsDecal> decals;
    std::string activeDecal;
    float decalScale = 0.0254f;
    int pendingPadPinIdx = -1;
    int pendingPadLines = 0;

    int pendingLinePts = 0;
    float pendingLineWidth = 0.2f;
    EdaLayerId pendingLineLayer = EdaLayerId::EdgeCuts;
    bool hasPrevLinePt = false;
    float prevLx = 0.0f, prevLy = 0.0f;

    int curNetId = 0;
    std::set<std::string> netNames;

    size_t pos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        // Check header / unit declarations
        if (line.front() == '!') {
            std::string_view rem = line.substr(1), tk;
            while (ReaderText::NextTokenSv(rem, tk)) {
                scaleMm = DetailMentorPads::UnitScaleToMm(tk, scaleMm);
            }
            continue;
        }
        if (ReaderText::AsciiEqCI(line, "MILS") || ReaderText::AsciiEqCI(line, "METRIC") ||
            ReaderText::AsciiEqCI(line, "INCHES") || ReaderText::AsciiEqCI(line, "BASIC")) {
            scaleMm = DetailMentorPads::UnitScaleToMm(line, scaleMm);
            continue;
        }

        if (line.front() == '*') {
            pendingLinePts = 0;
            pendingPadLines = 0;
            if (ReaderText::StartsWithCI(line, "*END*")) break;
            if (ReaderText::StartsWithCI(line, "*PADS-")) {
                std::string_view rem = line, tk;
                while (ReaderText::NextTokenSv(rem, tk)) scaleMm = DetailMentorPads::UnitScaleToMm(tk, scaleMm);
                continue;
            }
            if (ReaderText::StartsWithCI(line, "*PARTDECAL*")) {
                sec = Sec::PartDecal;
                activeDecal.clear();
            } else if (ReaderText::StartsWithCI(line, "*PART*") || ReaderText::StartsWithCI(line, "*PARTS*")) {
                sec = Sec::Part;
            } else if (ReaderText::StartsWithCI(line, "*LINES*") || ReaderText::StartsWithCI(line, "*BOARD*")) {
                sec = Sec::Lines;
            } else if (ReaderText::StartsWithCI(line, "*ROUTE*")) {
                sec = Sec::Route;
            } else if (ReaderText::StartsWithCI(line, "*SIGNAL*")) {
                sec = Sec::Signal;
                std::string_view rem = line.substr(8), netTok;
                if (ReaderText::NextTokenSv(rem, netTok)) {
                    netNames.insert(std::string(netTok));
                    curNetId = static_cast<int>(netNames.size());
                }
            } else {
                sec = Sec::Other;
            }
            continue;
        }

        if (sec == Sec::PartDecal) {
            std::string_view rem = line, t0;
            if (!ReaderText::NextTokenSv(rem, t0)) continue;

            if (pendingPadLines > 0) {
                --pendingPadLines;
                // Padstack layer row: <layer> <size> <shape: R|S|O|RF> [<finOri> <finLen> <drillOff> <drill>]
                std::string_view tSize, tShape;
                float szRaw = 0.0f;
                if (ReaderText::NextTokenSv(rem, tSize) && ReaderText::ParseFloatSv(tSize, szRaw) && szRaw > 0.0f) {
                    ReaderText::NextTokenSv(rem, tShape);
                    const float szMm = szRaw * decalScale;
                    uint8_t shp = 0;
                    if (ReaderText::StartsWithCI(tShape, "S") || ReaderText::StartsWithCI(tShape, "R")) {
                        shp = (tShape == "R" || tShape == "r") ? uint8_t(0) : uint8_t(1); // PADS 'R'=Round, 'S'=Square!
                    } else if (ReaderText::StartsWithCI(tShape, "O")) {
                        shp = 3;
                    }
                    float drillMm = 0.0f;
                    std::string_view extra;
                    while (ReaderText::NextTokenSv(rem, extra)) {
                        float dv = 0.0f;
                        if (ReaderText::ParseFloatSv(extra, dv) && dv > 0.0f && dv < szRaw) {
                            drillMm = dv * decalScale;
                        }
                    }
                    auto& pins = decals[activeDecal].pins;
                    if (pendingPadPinIdx == 0) {
                        for (auto& p : pins) { p.wMm = szMm; p.hMm = szMm; p.shape = shp; if (drillMm > 0) p.drillMm = drillMm; }
                    } else if (pendingPadPinIdx > 0 && static_cast<size_t>(pendingPadPinIdx) <= pins.size()) {
                        auto& p = pins[static_cast<size_t>(pendingPadPinIdx - 1)];
                        p.wMm = szMm; p.hMm = szMm; p.shape = shp;
                        if (drillMm > 0) p.drillMm = drillMm;
                    }
                }
                continue;
            }

            const bool inlineTerm = (t0.front() == 'T' || t0.front() == 't') && t0.size() > 1 &&
                (std::isdigit(static_cast<unsigned char>(t0[1])) || t0[1] == '-' || t0[1] == '+');
            const bool spacedTerm = (t0 == "T" || t0 == "t");
            if (inlineTerm || spacedTerm)
            {
                // Terminal record: T<x> <y> <nmx> <nmy> <pinName>  OR  T <x> <y> <nmx> <nmy> <pinName>
                float tx = 0.0f, ty = 0.0f;
                std::string_view tX, tY, tNmx, tNmy, tPin;
                const bool haveX = inlineTerm
                    ? ReaderText::ParseFloatSv(t0.substr(1), tx)
                    : (ReaderText::NextTokenSv(rem, tX) && ReaderText::ParseFloatSv(tX, tx));
                if (!activeDecal.empty() && haveX &&
                    ReaderText::NextTokenSv(rem, tY) && ReaderText::ParseFloatSv(tY, ty))
                {
                    ReaderText::NextTokenSv(rem, tNmx);
                    ReaderText::NextTokenSv(rem, tNmy);
                    ReaderText::NextTokenSv(rem, tPin);
                    DetailMentorPads::PadsPin pin;
                    pin.rx = tx * decalScale;
                    pin.ry = ty * decalScale;
                    pin.pinNum = PathUtils::Utf8ToWide(tPin.empty() ? std::string_view("1") : tPin);
                    decals[activeDecal].pins.push_back(std::move(pin));
                }
            } else if (ReaderText::AsciiEqCI(t0, "PAD")) {
                std::string_view tPinIdx, tCount;
                int pIdx = 0, nLyr = 0;
                if (ReaderText::NextTokenSv(rem, tPinIdx) && ReaderText::ParseIntSv(tPinIdx, pIdx) &&
                    ReaderText::NextTokenSv(rem, tCount)  && ReaderText::ParseIntSv(tCount, nLyr) && nLyr > 0) {
                    pendingPadPinIdx = pIdx;
                    pendingPadLines = std::min(nLyr, 32);
                }
            } else {
                // Decal header: <name> <U: I|M> ...
                std::string_view tUnit;
                if (ReaderText::NextTokenSv(rem, tUnit) &&
                    (tUnit == "I" || tUnit == "M" || ReaderText::AsciiEqCI(tUnit, "MILS") || ReaderText::AsciiEqCI(tUnit, "METRIC")))
                {
                    activeDecal = ReaderText::ToUpperAscii(t0);
                    decalScale = (tUnit == "M" || ReaderText::AsciiEqCI(tUnit, "METRIC")) ? 1.0f : 0.0254f;
                    decals[activeDecal] = {};
                }
            }
        } else if (sec == Sec::Part) {
            // <refDes> <partType[@decal]> <x> <y> [<rotDeg> <glue> <mirror: N|M>]
            std::string_view rem = line, tRef, tType, tX, tY, tRot, tGlue, tMir;
            float x = 0, y = 0, rot = 0;
            if (!ReaderText::NextTokenSv(rem, tRef) || !ReaderText::NextTokenSv(rem, tType) ||
                !ReaderText::NextTokenSv(rem, tX)   || !ReaderText::ParseFloatSv(tX, x) ||
                !ReaderText::NextTokenSv(rem, tY)   || !ReaderText::ParseFloatSv(tY, y)) {
                continue;
            }
            if (ReaderText::NextTokenSv(rem, tRot)) ReaderText::ParseFloatSv(tRot, rot);
            ReaderText::NextTokenSv(rem, tGlue);
            ReaderText::NextTokenSv(rem, tMir);

            const bool bottom = (tMir == "M" || tMir == "m");
            const size_t atPos = tType.find('@');
            std::string_view valSv = (atPos == std::string_view::npos) ? tType : tType.substr(0, atPos);
            std::string_view fpSv  = (atPos == std::string_view::npos) ? tType : tType.substr(atPos + 1);

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(tRef);
            comp.value = PathUtils::Utf8ToWide(valSv);
            comp.footprint = PathUtils::Utf8ToWide(fpSv);
            comp.x = x * scaleMm;
            comp.y = y * scaleMm;
            comp.rotDeg = rot;
            comp.bottomSide = bottom;
            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);

            // Emit decal pads if decal was defined in *PARTDECAL*, else 2-pad SMD default
            auto dit = decals.find(ReaderText::ToUpperAscii(fpSv));
            if (dit == decals.end()) dit = decals.find(ReaderText::ToUpperAscii(tType));
            if (dit != decals.end() && !dit->second.pins.empty()) {
                comp.pinCount = static_cast<int>(dit->second.pins.size());
                const float rad = rot * (3.1415926535f / 180.0f);
                const float cs = std::cos(rad), sn = std::sin(rad);
                for (const auto& dp : dit->second.pins) {
                    const float lx = bottom ? -dp.rx : dp.rx;
                    const float ly = dp.ry;
                    EdaPadPrim pad;
                    pad.x = comp.x + lx * cs - ly * sn;
                    pad.y = comp.y + lx * sn + ly * cs;
                    pad.w = dp.wMm;
                    pad.h = dp.hMm;
                    pad.rotDeg = rot;
                    pad.drill = dp.drillMm;
                    pad.shape = dp.shape;
                    pad.layer = (dp.drillMm > 0.0f) ? EdaLayerId::PadsThrough
                              : (bottom ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper);
                    pad.pinName = dp.pinNum;
                    out.pads.push_back(std::move(pad));
                }
            } else {
                const float dx = comp.widthMm * 0.35f;
                for (int s : { -1, 1 }) {
                    EdaPadPrim pad;
                    pad.x = comp.x + s * dx;
                    pad.y = comp.y;
                    pad.w = 0.9f; pad.h = 1.1f;
                    pad.shape = 1;
                    pad.layer = bottom ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                    out.pads.push_back(std::move(pad));
                }
            }
            out.labels.push_back({ comp.x, comp.y + comp.lengthMm * 0.6f, 0.9f,
                                   bottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, comp.refDes });
            out.components.push_back(std::move(comp));
        } else if (sec == Sec::Lines) {
            std::string_view rem = line, t0;
            if (!ReaderText::NextTokenSv(rem, t0)) continue;
            float px = 0, py = 0;
            std::string_view t1;
            if (pendingLinePts > 0 && ReaderText::ParseFloatSv(t0, px) &&
                ReaderText::NextTokenSv(rem, t1) && ReaderText::ParseFloatSv(t1, py))
            {
                px *= scaleMm;
                py *= scaleMm;
                if (hasPrevLinePt) {
                    out.traces.push_back({ prevLx, prevLy, px, py, pendingLineWidth, pendingLineLayer, 0 });
                }
                prevLx = px; prevLy = py;
                hasPrevLinePt = true;
                --pendingLinePts;
            } else {
                // Header: <name> <BOARD|LINES|COPPER> <numPts> [<width> <layer>]
                std::string_view tKind, tCnt, tWid, tLyr;
                int nPts = 0;
                if (ReaderText::NextTokenSv(rem, tKind) &&
                    ReaderText::NextTokenSv(rem, tCnt) && ReaderText::ParseIntSv(tCnt, nPts) && nPts >= 2)
                {
                    pendingLinePts = std::min(nPts, 65536);
                    hasPrevLinePt = false;
                    float wRaw = 0.0f;
                    if (ReaderText::NextTokenSv(rem, tWid) && ReaderText::ParseFloatSv(tWid, wRaw) && wRaw > 0.0f) {
                        pendingLineWidth = std::max(0.15f, wRaw * scaleMm);
                    } else {
                        pendingLineWidth = 0.25f;
                    }
                    if (ReaderText::AsciiEqCI(tKind, "BOARD") || ReaderText::AsciiEqCI(tKind, "OUTLINE")) {
                        pendingLineLayer = EdaLayerId::EdgeCuts;
                    } else if (ReaderText::AsciiEqCI(tKind, "COPPER")) {
                        int lyr = 1;
                        if (ReaderText::NextTokenSv(rem, tLyr)) ReaderText::ParseIntSv(tLyr, lyr);
                        pendingLineLayer = DetailMentorPads::MapPadsLayer(lyr);
                    } else {
                        pendingLineLayer = EdaLayerId::TopSilk;
                    }
                }
            }
        } else if (sec == Sec::Route || sec == Sec::Signal) {
            // Segment: <x1> <y1> <l1> <x2> <y2> <l2> <width>
            std::string_view rem = line, tx1, ty1, tl1, tx2, ty2, tl2, tw;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0, wRaw = 10.0f;
            int l1 = 1, l2 = 1;
            if (ReaderText::NextTokenSv(rem, tx1) && ReaderText::ParseFloatSv(tx1, x1) &&
                ReaderText::NextTokenSv(rem, ty1) && ReaderText::ParseFloatSv(ty1, y1) &&
                ReaderText::NextTokenSv(rem, tl1) && ReaderText::ParseIntSv(tl1, l1) &&
                ReaderText::NextTokenSv(rem, tx2) && ReaderText::ParseFloatSv(tx2, x2) &&
                ReaderText::NextTokenSv(rem, ty2) && ReaderText::ParseFloatSv(ty2, y2) &&
                ReaderText::NextTokenSv(rem, tl2) && ReaderText::ParseIntSv(tl2, l2))
            {
                if (ReaderText::NextTokenSv(rem, tw)) ReaderText::ParseFloatSv(tw, wRaw);
                const float sx1 = x1 * scaleMm, sy1 = y1 * scaleMm;
                const float sx2 = x2 * scaleMm, sy2 = y2 * scaleMm;
                const float wMm = std::clamp(wRaw * scaleMm, 0.12f, 5.0f);
                out.traces.push_back({ sx1, sy1, sx2, sy2, wMm, DetailMentorPads::MapPadsLayer(l1), curNetId });
                if (l1 != l2) {
                    out.vias.push_back({ sx2, sy2, std::max(0.6f, wMm * 2.2f), std::max(0.3f, wMm * 1.1f), curNetId });
                }
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.components.empty()) return false;
    out.kind = EdaDocKind::KiCadPcb;
    out.netCount = netNames.size();
    wchar_t title[128] = {};
    swprintf_s(title, L"Mentor PADS ASCII PCB (%zu parts, %zu pads, %zu nets)",
               out.components.size(), out.pads.size(), out.netCount);
    out.formatTitle = title;
    return true;
}

} // namespace Formats
