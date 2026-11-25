#pragma once
// EagleDesignRulesDru.h — Autodesk / CadSoft EAGLE PCB Design Rules & Stackup (.dru) Reader
// Parses EAGLE `.dru` `key = value` rules:
//   - `description[en/de]` multilingual header notes
//   - `layerSetup` copper/core/prepreg/blind-buried via expression (e.g. `(1*16)`, `(1*2+15*16)`, `[2:1+(2*3)+(14*15)+16:15]`)
//   - `mtCopper` & `mtIsolate` 16-layer copper & dielectric thickness vectors (`mil` / `mm` / `um`)
//   - `mdWireWire`, `mdWirePad`, `mdWireVia`, `mdPadPad`, `mdPadVia`, `mdViaVia`, `msWidth`, `msDrill`, `rvPadTop`, `rlMinPadTop`
// Renders a scaled multi-layer PCB stackup cross-section (copper foils, core vs prepreg dielectrics,
// through & blind/buried via barrels) + a calibrated DRC clearance & annular-ring verification coupon!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailEagleDru {

inline bool ParseDruLengthMm(std::string_view tok, double& outMm) {
    tok = ReaderText::TrimSv(tok);
    if (tok.empty()) return false;
    size_t numEnd = 0u;
    while (numEnd < tok.size() &&
           ((tok[numEnd] >= '0' && tok[numEnd] <= '9') || tok[numEnd] == '.' || tok[numEnd] == '-' || tok[numEnd] == '+')) {
        ++numEnd;
    }
    if (numEnd == 0u) return false;
    double v = 0.0;
    if (!ReaderText::ParseDoubleSv(tok.substr(0u, numEnd), v) || !std::isfinite(v)) return false;
    std::string unit = ReaderText::ToLowerAscii(ReaderText::TrimSv(tok.substr(numEnd)));
    if (unit == "mm" || unit.empty()) outMm = v;
    else if (unit == "mil" || unit == "mils") outMm = v * 0.0254;
    else if (unit == "um" || unit == "mic")   outMm = v * 0.001;
    else if (unit == "in" || unit == "inch")  outMm = v * 25.4;
    else return false;
    return outMm >= 0.0 && outMm <= 1000.0;
}

struct ViaSpanSpec {
    int fromLayer = 1;
    int toLayer = 16;
    bool isBlindOrBuried = false;
};

// Parse EAGLE `layerSetup` expression (e.g. `(1*16)`, `(1*2+15*16)`, `[2:1+(2*3)+(14*15)+16:15]`)
// Extracting ordered active copper layer numbers (1..16), core vs prepreg bonding (`*` = core, `+` = prepreg),
// and blind/buried/through via spans!
inline void ParseLayerSetupExpr(
    std::string_view expr,
    std::vector<int>& outLayers,
    std::vector<bool>& outIsCoreAfterLayer,
    std::vector<ViaSpanSpec>& outViaSpans)
{
    outLayers.clear();
    outIsCoreAfterLayer.clear();
    outViaSpans.clear();

    char lastBondOp = '*'; // `*` = FR4 Core, `+` = Prepreg
    std::vector<size_t> parenStartIdx;

    size_t i = 0u;
    while (i < expr.size()) {
        const char c = expr[i];
        if (c == '[') {
            // Blind via `[depth:expr` or trailing `:depth]`
            ++i;
            int bLayer = 0;
            while (i < expr.size() && expr[i] >= '0' && expr[i] <= '9') {
                bLayer = bLayer * 10 + (expr[i++] - '0');
            }
            if (i < expr.size() && expr[i] == ':') {
                ++i;
                if (bLayer >= 1 && bLayer <= 16) {
                    outViaSpans.push_back({ 1, bLayer, true });
                }
            }
            continue;
        }
        if (c == ':') {
            ++i;
            int bLayer = 0;
            while (i < expr.size() && expr[i] >= '0' && expr[i] <= '9') {
                bLayer = bLayer * 10 + (expr[i++] - '0');
            }
            if (bLayer >= 1 && bLayer <= 16) {
                outViaSpans.push_back({ bLayer, 16, true });
            }
            continue;
        }
        if (c == '(') {
            parenStartIdx.push_back(outLayers.size());
            ++i;
            continue;
        }
        if (c == ')') {
            if (!parenStartIdx.empty()) {
                const size_t sIdx = parenStartIdx.back();
                parenStartIdx.pop_back();
                if (sIdx < outLayers.size() && outLayers.size() - sIdx >= 2u) {
                    const int l0 = outLayers[sIdx];
                    const int l1 = outLayers.back();
                    if (!(l0 == 1 && l1 == 16)) {
                        outViaSpans.push_back({ l0, l1, true });
                    }
                }
            }
            ++i;
            continue;
        }
        if (c == '*' || c == '+') {
            lastBondOp = c;
            if (!outIsCoreAfterLayer.empty()) {
                outIsCoreAfterLayer.back() = (c == '*');
            }
            ++i;
            continue;
        }
        if (c >= '0' && c <= '9') {
            int lnum = 0;
            while (i < expr.size() && expr[i] >= '0' && expr[i] <= '9') {
                lnum = lnum * 10 + (expr[i++] - '0');
            }
            if (lnum >= 1 && lnum <= 16 && std::find(outLayers.begin(), outLayers.end(), lnum) == outLayers.end()) {
                outLayers.push_back(lnum);
                outIsCoreAfterLayer.push_back(lastBondOp == '*');
            }
            continue;
        }
        ++i;
    }

    if (outLayers.size() < 2u) {
        outLayers = { 1, 16 };
        outIsCoreAfterLayer = { true, false };
    }
    // Always include standard through-hole via spanning outer layers `(first .. last)`
    outViaSpans.insert(outViaSpans.begin(), { outLayers.front(), outLayers.back(), false });
}

} // namespace DetailEagleDru

inline bool ParseEagleDesignRulesDru(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailEagleDru;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 20u, (8u << 20))) return false;

    std::string descEn;
    std::string layerSetupExpr;
    std::vector<double> mtCopperMm;
    std::vector<double> mtIsolateMm;

    double mdWireWire = 0.1524, mdWirePad = 0.1524, mdPadVia = 0.1524;
    double msWidth = 0.1524, msDrill = 0.35;
    double rvPadTop = 0.25, rlMinPadTop = 0.20, rlMaxPadTop = 0.50;
    size_t recognizedDruKeys = 0u;

    // Join multi-line values (e.g. `description[en] = ...` with `\n` escapes) line by line
    size_t pos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || line.front() == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;

        std::string_view key = ReaderText::TrimSv(line.substr(0u, eq));
        std::string_view val = ReaderText::TrimSv(line.substr(eq + 1u));
        if (key.empty()) continue;

        if (ReaderText::StartsWithCI(key, "description")) {
            ++recognizedDruKeys;
            if (descEn.empty() || ContainsTextCI(key, "[en]")) {
                // Strip HTML `<b>...</b>` tags and `\n` escapes for a clean title string
                std::string clean;
                for (size_t i = 0; i < val.size() && clean.size() < 80u; ++i) {
                    if (val[i] == '<') {
                        while (i < val.size() && val[i] != '>') ++i;
                        continue;
                    }
                    if (val[i] == '\\' && i + 1u < val.size() && val[i + 1u] == 'n') {
                        i += 1u;
                        if (!clean.empty()) break;
                        continue;
                    }
                    if (val[i] >= 0x20 && val[i] < 0x7F) clean.push_back(val[i]);
                }
                std::string_view ct = ReaderText::TrimSv(clean);
                if (!ct.empty()) descEn.assign(ct);
            }
        } else if (key == "layerSetup") {
            layerSetupExpr.assign(val);
            ++recognizedDruKeys;
        } else if (key == "mtCopper") {
            ++recognizedDruKeys;
            std::string_view rem = val, tk;
            while (ReaderText::NextTokenSv(rem, tk)) {
                double m = 0.035;
                if (ParseDruLengthMm(tk, m)) mtCopperMm.push_back(m);
            }
        } else if (key == "mtIsolate") {
            ++recognizedDruKeys;
            std::string_view rem = val, tk;
            while (ReaderText::NextTokenSv(rem, tk)) {
                double m = 0.20;
                if (ParseDruLengthMm(tk, m)) mtIsolateMm.push_back(m);
            }
        } else if (key == "mdWireWire") { if (ParseDruLengthMm(val, mdWireWire)) ++recognizedDruKeys; }
        else if (key == "mdWirePad")    { if (ParseDruLengthMm(val, mdWirePad))  ++recognizedDruKeys; }
        else if (key == "mdPadVia")     { if (ParseDruLengthMm(val, mdPadVia))   ++recognizedDruKeys; }
        else if (key == "msWidth")      { if (ParseDruLengthMm(val, msWidth))    ++recognizedDruKeys; }
        else if (key == "msDrill")      { if (ParseDruLengthMm(val, msDrill))    ++recognizedDruKeys; }
        else if (key == "rvPadTop")     { if (ReaderText::ParseDoubleSv(val, rvPadTop)) ++recognizedDruKeys; }
        else if (key == "rlMinPadTop")  { if (ParseDruLengthMm(val, rlMinPadTop)) ++recognizedDruKeys; }
        else if (key == "rlMaxPadTop")  { if (ParseDruLengthMm(val, rlMaxPadTop)) ++recognizedDruKeys; }
        else if (ReaderText::StartsWithCI(key, "md") || ReaderText::StartsWithCI(key, "mt") ||
                 ReaderText::StartsWithCI(key, "ms") || ReaderText::StartsWithCI(key, "rv") ||
                 ReaderText::StartsWithCI(key, "rl") || ReaderText::StartsWithCI(key, "ps") ||
                 ReaderText::StartsWithCI(key, "sr") || ReaderText::StartsWithCI(key, "dp")) {
            ++recognizedDruKeys;
        }
    }

    if (recognizedDruKeys < 3u || (layerSetupExpr.empty() && mtCopperMm.empty() && mtIsolateMm.empty())) {
        return false;
    }

    std::vector<int> copperLayers;
    std::vector<bool> isCoreAfter;
    std::vector<ViaSpanSpec> viaSpans;
    ParseLayerSetupExpr(layerSetupExpr, copperLayers, isCoreAfter, viaSpans);

    outBoard = ParsedEdaBoard{};
    outBoard.kind = EdaDocKind::KiCadPcb;

    // 1. Left Side (`x = -46 .. +6`): Scaled Multi-Layer PCB Stackup Cross-Section
    const float stackX0 = -46.0f;
    const float stackX1 = 6.0f;
    float curY = 24.0f;
    double totalBoardThickMm = 0.0;

    std::unordered_map<int, std::pair<float, float>> layerYBounds; // layerNum -> (yTop, yBot)

    for (size_t li = 0; li < copperLayers.size(); ++li) {
        const int lnum = copperLayers[li];
        const size_t vecIdx = static_cast<size_t>(std::clamp(lnum - 1, 0, 15));
        const double cuMm = (vecIdx < mtCopperMm.size() && mtCopperMm[vecIdx] > 0.0) ? mtCopperMm[vecIdx] : 0.035;
        totalBoardThickMm += cuMm;

        // Visual foil height magnified cleanly (`1.6 .. 2.8 mm` on diagram) while preserving relative weight
        const float cuVisH = std::clamp(static_cast<float>(cuMm * 36.0), 1.5f, 3.2f);
        const float yTop = curY;
        const float yBot = curY - cuVisH;
        layerYBounds[lnum] = { yTop, yBot };

        const EdaLayerId cuLayer = (li == 0u) ? EdaLayerId::TopCopper
                                 : (li + 1u == copperLayers.size()) ? EdaLayerId::BottomCopper
                                                                    : EdaLayerId::InnerCopper;
        DrawEdaBoxRect(outBoard, stackX0, yBot, stackX1, yTop, 0.28f, cuLayer, lnum);
        // Horizontal copper foil fill hatch
        outBoard.traces.push_back({ stackX0 + 0.4f, (yTop + yBot) * 0.5f, stackX1 - 0.4f, (yTop + yBot) * 0.5f, cuVisH * 0.55f, cuLayer, lnum });

        wchar_t cuLbl[128] = {};
        swprintf_s(cuLbl, L"L%d %ls (%.0f um Cu)",
                   lnum,
                   (lnum == 1) ? L"Top" : ((lnum == 16) ? L"Bottom" : L"Inner"),
                   cuMm * 1000.0);
        outBoard.labels.push_back({ stackX0 - 19.5f, (yTop + yBot) * 0.5f - 0.35f, 0.82f, cuLayer, cuLbl });
        curY = yBot;

        // Dielectric isolation layer between copper layer `li` and `li + 1`
        if (li + 1u < copperLayers.size()) {
            const double isoMm = (vecIdx < mtIsolateMm.size() && mtIsolateMm[vecIdx] > 0.0)
                ? mtIsolateMm[vecIdx]
                : ((li < mtIsolateMm.size() && mtIsolateMm[li] > 0.0) ? mtIsolateMm[li] : 0.20);
            totalBoardThickMm += isoMm;

            const bool isCore = (li < isCoreAfter.size()) ? isCoreAfter[li] : true;
            const float isoVisH = std::clamp(static_cast<float>(isoMm * 10.0), 3.0f, 11.0f);
            const float iyTop = curY;
            const float iyBot = curY - isoVisH;

            DrawEdaBoxRect(outBoard, stackX0 + 0.6f, iyBot, stackX1 - 0.6f, iyTop, 0.16f, EdaLayerId::EdgeCuts, 0);
            // Diagonal weave hatch for Core (`*`) vs dashed horizontal ply for Prepreg (`+`)
            if (isCore) {
                for (float hx = stackX0 + 3.0f; hx + 3.0f < stackX1; hx += 6.0f) {
                    outBoard.traces.push_back({ hx, iyBot + 0.4f, hx + 2.5f, iyTop - 0.4f, 0.12f, EdaLayerId::BottomSilk, 0 });
                }
            } else {
                outBoard.traces.push_back({ stackX0 + 2.0f, (iyTop + iyBot) * 0.5f, stackX1 - 2.0f, (iyTop + iyBot) * 0.5f, 0.12f, EdaLayerId::TopSilk, 0 });
            }

            wchar_t isoLbl[96] = {};
            swprintf_s(isoLbl, L"%ls %.3f mm", isCore ? L"FR4 Core" : L"Prepreg", isoMm);
            outBoard.labels.push_back({ stackX0 + 2.5f, (iyTop + iyBot) * 0.5f - 0.35f, 0.72f, EdaLayerId::SchLabel, isoLbl });
            curY = iyBot;
        }
    }

    // Draw Plated Via Barrels across the stackup cross-section (Through + Blind/Buried vias from `layerSetup`)
    for (size_t vi = 0; vi < std::min<size_t>(viaSpans.size(), 5u); ++vi) {
        const auto& vs = viaSpans[vi];
        auto itA = layerYBounds.find(vs.fromLayer);
        auto itB = layerYBounds.find(vs.toLayer);
        if (itA == layerYBounds.end() || itB == layerYBounds.end()) continue;
        const float vyTop = std::max(itA->second.first, itB->second.first);
        const float vyBot = std::min(itA->second.second, itB->second.second);
        const float vx = stackX1 - 7.0f - static_cast<float>(vi) * 7.5f;
        const float barrelHalf = 1.15f;

        DrawEdaBoxRect(outBoard, vx - barrelHalf, vyBot, vx + barrelHalf, vyTop, 0.28f, EdaLayerId::PadsThrough, 20);
        outBoard.vias.push_back({ vx, vyTop, 2.2f, static_cast<float>(msDrill), 20 });
        outBoard.vias.push_back({ vx, vyBot, 2.2f, static_cast<float>(msDrill), 20 });
    }

    // 2. Right Side (`x = +16 .. +52`): Calibrated DRC Clearance & Annular-Ring Verification Coupon!
    const float coupX0 = 16.0f, coupY0 = curY, coupX1 = 52.0f, coupY1 = 24.0f;
    DrawEdaBoxRect(outBoard, coupX0, coupY0, coupX1, coupY1, 0.30f, EdaLayerId::EdgeCuts, 0);
    outBoard.labels.push_back({ coupX0 + 1.5f, coupY1 - 2.4f, 0.95f, EdaLayerId::TopSilk, L"DRC Verification Coupon (10x)" });

    // Magnify sub-mm DRC rules by 10x on coupon so min trace width, clearance, drill & restring are geometrically faithful!
    const float visScale = 10.0f;
    const float wScaled = std::clamp(static_cast<float>(msWidth * visScale), 0.35f, 4.5f);
    const float clrScaled = std::clamp(static_cast<float>(mdWireWire * visScale), 0.45f, 5.0f);
    const float drlScaled = std::clamp(static_cast<float>(msDrill * visScale), 1.2f, 8.0f);
    const double restringMm = std::clamp(msDrill * rvPadTop, rlMinPadTop, std::max(rlMinPadTop, rlMaxPadTop));
    const float padDiaScaled = drlScaled + 2.0f * std::clamp(static_cast<float>(restringMm * visScale), 0.6f, 5.0f);

    // Min trace-to-trace pair
    const float trY0 = coupY1 - 7.5f;
    outBoard.traces.push_back({ coupX0 + 3.0f, trY0, coupX1 - 3.0f, trY0, wScaled, EdaLayerId::TopCopper, 1 });
    outBoard.traces.push_back({ coupX0 + 3.0f, trY0 - wScaled - clrScaled, coupX1 - 3.0f, trY0 - wScaled - clrScaled, wScaled, EdaLayerId::TopCopper, 2 });

    wchar_t wRuleLbl[128] = {};
    swprintf_s(wRuleLbl, L"msWidth=%.3f mm | mdWireWire=%.3f mm", msWidth, mdWireWire);
    outBoard.labels.push_back({ coupX0 + 3.0f, trY0 + 1.6f, 0.72f, EdaLayerId::SchLabel, wRuleLbl });

    // Min drill + annular ring via/pad pair
    const float padY = trY0 - wScaled - clrScaled - 7.5f;
    const float padCx1 = coupX0 + 10.0f;
    const float padCx2 = padCx1 + padDiaScaled + std::clamp(static_cast<float>(mdPadVia * visScale), 0.8f, 6.0f);
    outBoard.pads.push_back({
        padCx1, padY, padDiaScaled, padDiaScaled, 0.0f, drlScaled, 0u, EdaLayerId::PadsThrough, L"P1", L"DRC_PAD"
    });
    outBoard.pads.push_back({
        padCx2, padY, padDiaScaled, padDiaScaled, 0.0f, drlScaled, 1u, EdaLayerId::TopCopper, L"P2", L"DRC_SMD"
    });

    wchar_t dRuleLbl[128] = {};
    swprintf_s(dRuleLbl, L"msDrill=%.3f mm | Annular=%.3f mm | mdPadVia=%.3f mm", msDrill, restringMm, mdPadVia);
    outBoard.labels.push_back({ coupX0 + 3.0f, padY - padDiaScaled * 0.5f - 2.4f, 0.72f, EdaLayerId::SchLabel, dRuleLbl });

    EdaComponent3D stackComp;
    stackComp.refDes = L"STACKUP";
    stackComp.value = std::to_wstring(copperLayers.size()) + L"-Layer PCB";
    stackComp.footprint = L"EAGLE_DRU_STACKUP";
    stackComp.x = (stackX0 + stackX1) * 0.5f;
    stackComp.y = (24.0f + curY) * 0.5f;
    stackComp.widthMm = stackX1 - stackX0;
    stackComp.lengthMm = std::max(6.0f, 24.0f - curY);
    stackComp.heightMm = static_cast<float>(std::max(0.4, totalBoardThickMm));
    stackComp.pinCount = static_cast<int>(copperLayers.size());
    stackComp.category = EdaPartCategory::IC_Package;
    outBoard.components.push_back(std::move(stackComp));

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"EAGLE Design Rules (.dru — %zu-layer %hs, %.2f mm stackup%ls)",
               copperLayers.size(),
               layerSetupExpr.empty() ? "(1*16)" : layerSetupExpr.c_str(),
               totalBoardThickMm,
               descEn.empty() ? L"" : (L", " + PathUtils::Utf8ToWide(descEn)).c_str());
    outBoard.labels.push_back({ stackX0, 26.8f, 1.15f, EdaLayerId::TopSilk, titleBuf });
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = copperLayers.size() + viaSpans.size();
    return true;
}

} // namespace Formats
