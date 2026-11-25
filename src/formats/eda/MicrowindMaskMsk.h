#pragma once
// MicrowindMaskMsk.h — Microwind CMOS IC Physical Mask Layout (.msk) Reader
// Parses Microwind ASCII VLSI mask layouts:
//   - `VERSION`, `LAMBDA <um>`, `VIEW <x> <y> <w> <h>`, `BB <xmin> <ymin> <xmax> <ymax>`, `TITLE`
//   - `C <layer> <x> <y> <w> <h>` (CIF-style lambda mask boxes across `NTUB`/`NWELL`, `DN`/`NDIF`,
//     `DP`/`PDIF`, `POLY`/`PO`, `POLY2`, `CO`/`CONT`, `ME1`..`ME6`, `VIA`..`VIA5`, `PAD`)
//   - `P <layer> <x0> <y0> <x1> <y1> ...` (polygons)
//   - `T <layer> <x> <y> <signal>` / `S <x> <y> <layer> <signal>` (node/port text labels)
// Automatically detects MOSFET channel intersections (`POLY` crossing `DN`/`DP` diffusion) and maps
// contacts/vias to `EdaViaPrim` and I/O labels to `EdaPadPrim`!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailMicrowindMsk {

enum class MskLayerKind : uint8_t {
    Well = 0,
    NDiff,
    PDiff,
    Poly,
    Contact,
    Metal1,
    Metal2,
    MetalUpper,
    Via,
    Pad,
    Other
};

inline bool ClassifyMicrowindLayer(std::string_view raw, MskLayerKind& outKind, EdaLayerId& outEda) {
    const std::string up = ReaderText::ToUpperAscii(raw);
    if (up == "NTUB" || up == "NWELL" || up == "NW" || up == "PTUB" || up == "PWELL") {
        outKind = MskLayerKind::Well; outEda = EdaLayerId::BottomSilk; return true;
    }
    if (up == "DN" || up == "NDIF" || up == "NDIFF" || up == "ACTN") {
        outKind = MskLayerKind::NDiff; outEda = EdaLayerId::InnerCopper; return true;
    }
    if (up == "DP" || up == "PDIF" || up == "PDIFF" || up == "ACTP") {
        outKind = MskLayerKind::PDiff; outEda = EdaLayerId::SchSymbol; return true;
    }
    if (up == "POLY" || up == "PO" || up == "POLY1" || up == "POLY2" || up == "POLYG") {
        outKind = MskLayerKind::Poly; outEda = EdaLayerId::BottomCopper; return true;
    }
    if (up == "CO" || up == "CONT" || up == "CONTACT") {
        outKind = MskLayerKind::Contact; outEda = EdaLayerId::PadsThrough; return true;
    }
    if (up == "ME1" || up == "MET1" || up == "M1" || up == "METAL1") {
        outKind = MskLayerKind::Metal1; outEda = EdaLayerId::TopCopper; return true;
    }
    if (up == "ME2" || up == "MET2" || up == "M2" || up == "METAL2") {
        outKind = MskLayerKind::Metal2; outEda = EdaLayerId::SchWire; return true;
    }
    if (up == "ME3" || up == "ME4" || up == "ME5" || up == "ME6" ||
        up == "MET3" || up == "MET4" || up == "MET5" || up == "MET6" ||
        up == "M3" || up == "M4" || up == "M5" || up == "M6") {
        outKind = MskLayerKind::MetalUpper; outEda = EdaLayerId::InnerCopper; return true;
    }
    if (up == "VIA" || up == "VIA1" || up == "VIA2" || up == "VIA3" || up == "VIA4" || up == "VIA5") {
        outKind = MskLayerKind::Via; outEda = EdaLayerId::PadsThrough; return true;
    }
    if (up == "PAD" || up == "PASS" || up == "OVG") {
        outKind = MskLayerKind::Pad; outEda = EdaLayerId::TopSilk; return true;
    }
    return false;
}

struct MskBox {
    MskLayerKind kind = MskLayerKind::Other;
    EdaLayerId edaLayer = EdaLayerId::TopCopper;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

} // namespace DetailMicrowindMsk

inline bool ParseMicrowindMaskMsk(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailMicrowindMsk;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 16u, (32u << 20))) return false;

    constexpr float kLambdaToMm = 0.4f; // 1 lambda -> 0.4 mm visual scale on IC layout canvas
    double lambdaUm = 0.06;
    bool sawMskHeader = false;
    bool hasBb = false;
    float bbX0 = 0, bbY0 = 0, bbX1 = 0, bbY1 = 0;
    std::string designTitle;

    std::vector<MskBox> boxes;
    std::vector<MskBox> polyBoxes, nDiffBoxes, pDiffBoxes;
    std::set<std::string> signalNames;

    outBoard = ParsedEdaBoard{};
    outBoard.kind = EdaDocKind::KiCadPcb;

    size_t pos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || line.front() == '*' || line.front() == '#' ||
            (line.size() >= 2u && line[0] == '/' && line[1] == '/')) {
            continue;
        }

        std::string_view rem = line, cmd;
        if (!ReaderText::NextTokenSv(rem, cmd)) continue;
        const std::string upCmd = ReaderText::ToUpperAscii(cmd);

        if (upCmd == "VERSION" || upCmd == "FOUNDRY" || upCmd == "RULE" || upCmd == "GRID" ||
            upCmd == "STEP" || upCmd == "SIMU" || upCmd == "END") {
            sawMskHeader = true;
            continue;
        }
        if (upCmd == "TITLE" || upCmd == "NAME") {
            sawMskHeader = true;
            designTitle = std::string(ReaderText::TrimSv(rem));
            continue;
        }
        if (upCmd == "LAMBDA") {
            sawMskHeader = true;
            std::string_view vTok;
            double lv = 0.0;
            if (ReaderText::NextTokenSv(rem, vTok) && ReaderText::ParseDoubleSv(vTok, lv) && lv > 0.0) {
                lambdaUm = lv;
            }
            continue;
        }
        if (upCmd == "VIEW" || upCmd == "BB") {
            sawMskHeader = true;
            std::string_view t0, t1, t2, t3;
            float a = 0, b = 0, c = 0, d = 0;
            if (ReaderText::NextTokenSv(rem, t0) && ReaderText::NextTokenSv(rem, t1) &&
                ReaderText::NextTokenSv(rem, t2) && ReaderText::NextTokenSv(rem, t3) &&
                ReaderText::ParseFloatSv(t0, a) && ReaderText::ParseFloatSv(t1, b) &&
                ReaderText::ParseFloatSv(t2, c) && ReaderText::ParseFloatSv(t3, d))
            {
                if (upCmd == "BB") {
                    hasBb = true;
                    bbX0 = std::min(a, c) * kLambdaToMm;
                    bbX1 = std::max(a, c) * kLambdaToMm;
                    bbY0 = std::min(b, d) * kLambdaToMm;
                    bbY1 = std::max(b, d) * kLambdaToMm;
                }
            }
            continue;
        }
        if (upCmd == "C" || upCmd == "B" || upCmd == "BOX") {
            // `C <layer> <x> <y> <w> <h>` (lower-left corner `(x,y)` + width `w` + height `h` in lambda!)
            std::string_view lyrTok, tx, ty, tw, th;
            float x = 0, y = 0, w = 0, h = 0;
            if (!ReaderText::NextTokenSv(rem, lyrTok) ||
                !ReaderText::NextTokenSv(rem, tx) || !ReaderText::NextTokenSv(rem, ty) ||
                !ReaderText::NextTokenSv(rem, tw) || !ReaderText::NextTokenSv(rem, th) ||
                !ReaderText::ParseFloatSv(tx, x) || !ReaderText::ParseFloatSv(ty, y) ||
                !ReaderText::ParseFloatSv(tw, w) || !ReaderText::ParseFloatSv(th, h)) {
                return false;
            }
            MskLayerKind kind = MskLayerKind::Other;
            EdaLayerId edaLyr = EdaLayerId::TopCopper;
            if (!ClassifyMicrowindLayer(lyrTok, kind, edaLyr)) return false;

            const float x0 = std::min(x, x + w) * kLambdaToMm;
            const float x1 = std::max(x, x + w) * kLambdaToMm;
            const float y0 = std::min(y, y + h) * kLambdaToMm;
            const float y1 = std::max(y, y + h) * kLambdaToMm;
            if (x1 - x0 < 0.01f || y1 - y0 < 0.01f) continue;

            MskBox mb{ kind, edaLyr, x0, y0, x1, y1 };
            if (kind == MskLayerKind::Poly)  polyBoxes.push_back(mb);
            if (kind == MskLayerKind::NDiff) nDiffBoxes.push_back(mb);
            if (kind == MskLayerKind::PDiff) pDiffBoxes.push_back(mb);
            if (boxes.size() < 65536u) boxes.push_back(mb);
            continue;
        }
        if (upCmd == "P" || upCmd == "POLY") {
            // `P <layer> <x0> <y0> <x1> <y1> ...`
            std::string_view lyrTok;
            if (!ReaderText::NextTokenSv(rem, lyrTok)) return false;
            MskLayerKind kind = MskLayerKind::Other;
            EdaLayerId edaLyr = EdaLayerId::TopCopper;
            if (!ClassifyMicrowindLayer(lyrTok, kind, edaLyr)) return false;

            std::vector<std::pair<float, float>> pts;
            std::string_view tx, ty;
            while (ReaderText::NextTokenSv(rem, tx) && ReaderText::NextTokenSv(rem, ty)) {
                float px = 0, py = 0;
                if (!ReaderText::ParseFloatSv(tx, px) || !ReaderText::ParseFloatSv(ty, py)) return false;
                pts.push_back({ px * kLambdaToMm, py * kLambdaToMm });
            }
            if (pts.size() >= 3u) {
                for (size_t k = 0; k < pts.size(); ++k) {
                    const auto& a = pts[k];
                    const auto& bPt = pts[(k + 1u) % pts.size()];
                    outBoard.traces.push_back({ a.first, a.second, bPt.first, bPt.second, 0.22f, edaLyr, static_cast<int>(kind) + 1 });
                }
                DetailBoardFabEda::HatchPolygonHorizontal(pts, edaLyr, static_cast<int>(kind) + 1, outBoard);
            }
            continue;
        }
        if (upCmd == "T" || upCmd == "TEXT" || upCmd == "S" || upCmd == "V" || upCmd == "CLK" || upCmd == "PULSE") {
            // `T <layer> <x> <y> <signal>` OR `S <x> <y> <layer> <signal>` stimulus/label
            std::string_view t0, t1, t2;
            if (ReaderText::NextTokenSv(rem, t0) && ReaderText::NextTokenSv(rem, t1) && ReaderText::NextTokenSv(rem, t2)) {
                float lx = 0, ly = 0;
                std::string_view sigRest = ReaderText::TrimSv(rem);
                bool parsedXY = false;
                if (ReaderText::ParseFloatSv(t1, lx) && ReaderText::ParseFloatSv(t2, ly)) {
                    parsedXY = true;
                } else if (ReaderText::ParseFloatSv(t0, lx) && ReaderText::ParseFloatSv(t1, ly)) {
                    parsedXY = true;
                }
                if (parsedXY && !sigRest.empty()) {
                    std::string_view sigTok;
                    ReaderText::NextTokenSv(sigRest, sigTok);
                    if (!sigTok.empty()) {
                        std::string sig(sigTok);
                        signalNames.insert(sig);
                        const float wx = lx * kLambdaToMm;
                        const float wy = ly * kLambdaToMm;
                        outBoard.pads.push_back({
                            wx, wy, 0.90f, 0.90f, 0.0f, 0.0f, 1u, EdaLayerId::TopCopper,
                            PathUtils::Utf8ToWide(sig), PathUtils::Utf8ToWide(sig)
                        });
                        outBoard.labels.push_back({ wx + 0.5f, wy + 0.4f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(sig) });
                    }
                }
            }
            continue;
        }

        // Reject arbitrary non-Microwind text files (e.g. binary/shader/image `.msk` files)
        if (!sawMskHeader && boxes.empty()) return false;
    }

    if (boxes.empty() && outBoard.traces.empty()) return false;
    if (!sawMskHeader && boxes.size() < 2u) return false;

    if (hasBb && bbX1 > bbX0 && bbY1 > bbY0) {
        DrawEdaBoxRect(outBoard, bbX0, bbY0, bbX1, bbY1, 0.28f, EdaLayerId::EdgeCuts, 0);
    }

    // Emit physical mask rectangles, contacts, and vias
    for (const auto& b : boxes) {
        const float cx = (b.x0 + b.x1) * 0.5f;
        const float cy = (b.y0 + b.y1) * 0.5f;
        const float bw = b.x1 - b.x0;
        const float bh = b.y1 - b.y0;

        if (b.kind == MskLayerKind::Contact || b.kind == MskLayerKind::Via) {
            const float dia = std::max(0.45f, std::min(bw, bh));
            outBoard.vias.push_back({ cx, cy, dia, dia * 0.5f, static_cast<int>(b.kind) + 1 });
            DrawEdaBoxRect(outBoard, b.x0, b.y0, b.x1, b.y1, 0.16f, EdaLayerId::TopSilk, 0);
        } else if (b.kind == MskLayerKind::Well) {
            DrawEdaBoxRect(outBoard, b.x0, b.y0, b.x1, b.y1, 0.18f, b.edaLayer, 0);
        } else {
            DrawEdaBoxRect(outBoard, b.x0, b.y0, b.x1, b.y1, 0.22f, b.edaLayer, static_cast<int>(b.kind) + 1);
            std::vector<std::pair<float, float>> rectPts = {
                { b.x0, b.y0 }, { b.x1, b.y0 }, { b.x1, b.y1 }, { b.x0, b.y1 }
            };
            DetailBoardFabEda::HatchPolygonHorizontal(rectPts, b.edaLayer, static_cast<int>(b.kind) + 1, outBoard);
        }
    }

    // Geometric LVS extraction: detect NMOS (`POLY` ∩ `DN`) and PMOS (`POLY` ∩ `DP`) active channel regions!
    size_t nmosCount = 0u, pmosCount = 0u;
    auto detectMosIntersections = [&](const std::vector<MskBox>& diffVec, bool isPmos) {
        for (const auto& po : polyBoxes) {
            for (const auto& df : diffVec) {
                const float ix0 = std::max(po.x0, df.x0);
                const float ix1 = std::min(po.x1, df.x1);
                const float iy0 = std::max(po.y0, df.y0);
                const float iy1 = std::min(po.y1, df.y1);
                if (ix1 > ix0 + 0.02f && iy1 > iy0 + 0.02f && outBoard.components.size() < 256u) {
                    const size_t idx = isPmos ? ++pmosCount : ++nmosCount;
                    EdaComponent3D mos;
                    mos.refDes = (isPmos ? L"MP" : L"MN") + std::to_wstring(idx);
                    wchar_t wlBuf[64] = {};
                    const float wLam = std::max(ix1 - ix0, iy1 - iy0) / kLambdaToMm;
                    const float lLam = std::min(ix1 - ix0, iy1 - iy0) / kLambdaToMm;
                    swprintf_s(wlBuf, L"W=%.1fL L=%.1fL", wLam, lLam);
                    mos.value = wlBuf;
                    mos.footprint = isPmos ? L"CMOS_PMOS_CH" : L"CMOS_NMOS_CH";
                    mos.x = (ix0 + ix1) * 0.5f;
                    mos.y = (iy0 + iy1) * 0.5f;
                    mos.widthMm = ix1 - ix0;
                    mos.lengthMm = iy1 - iy0;
                    mos.heightMm = 0.4f;
                    mos.pinCount = 4;
                    mos.category = EdaPartCategory::TransistorSOT;
                    outBoard.components.push_back(std::move(mos));
                }
            }
        }
    };
    detectMosIntersections(nDiffBoxes, false);
    detectMosIntersections(pDiffBoxes, true);

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"Microwind CMOS Mask Layout (.msk — \x03BB=%.3g um, %zu boxes, %zu NMOS, %zu PMOS%ls)",
               lambdaUm, boxes.size(), nmosCount, pmosCount,
               designTitle.empty() ? L"" : (L", " + PathUtils::Utf8ToWide(designTitle)).c_str());
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(1u, signalNames.empty() ? (nmosCount + pmosCount + 1u) : signalNames.size());
    return true;
}

} // namespace Formats
