#pragma once
// IbisModel.h — ANSI/EIA-656 IBIS Signal Integrity Model (.ibs) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseIbisModelBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // IBIS Signal Integrity Model (.ibs — ANSI/EIA-656 v3.2–v7.2)
    if (ext == L".ibs") {
        for (size_t i = 0; i < size; ++i) {
            if (data[i] == 0) return false;
        }
        if (sv.find("[IBIS Ver]") == std::string_view::npos &&
            sv.find("[ibis ver]") == std::string_view::npos &&
            sv.find("[Component]") == std::string_view::npos &&
            sv.find("[Model]") == std::string_view::npos)
        {
            return false;
        }

        auto parseIbisVal = [](std::string_view tok, double& outV) -> bool {
            while (!tok.empty() && static_cast<unsigned char>(tok.front()) <= 0x20u) tok.remove_prefix(1);
            while (!tok.empty() && static_cast<unsigned char>(tok.back()) <= 0x20u) tok.remove_suffix(1);
            if (tok.empty() || ReaderText::StartsWithCI(tok, "NA")) return false;
            char buf[64] = {};
            std::memcpy(buf, tok.data(), std::min<size_t>(tok.size(), 63));
            char* ep = nullptr;
            double v = std::strtod(buf, &ep);
            if (!ep || ep == buf || !std::isfinite(v)) return false;
            switch (*ep) {
            case 'f': case 'F': if (ep[1] != 'a' && ep[1] != 'A') v *= 1e-15; break;
            case 'p': v *= 1e-12; break;
            case 'n': v *= 1e-9;  break;
            case 'u': v *= 1e-6;  break;
            case 'm': v *= 1e-3;  break;
            case 'k': case 'K': v *= 1e3; break;
            case 'M': v *= 1e6;   break;
            case 'G': case 'g': v *= 1e9; break;
            case 'T': case 't': v *= 1e12; break;
            default: break;
            }
            outV = v;
            return true;
        };

        auto splitWs = [](std::string_view ln, std::vector<std::string_view>& toks) {
            toks.clear();
            size_t p = 0;
            while (p < ln.size()) {
                while (p < ln.size() && static_cast<unsigned char>(ln[p]) <= 0x20u) ++p;
                if (p >= ln.size()) break;
                size_t s = p;
                while (p < ln.size() && static_cast<unsigned char>(ln[p]) > 0x20u) ++p;
                toks.push_back(ln.substr(s, p - s));
            }
        };

        struct IbisPinEntry {
            std::string pinName;
            std::string sigName;
            std::string modelName;
            double rPin = 0.0, lPin = 0.0, cPin = 0.0;
            bool hasRlc = false;
        };
        struct IbisCurvePt {
            double x = 0.0;
            double yTyp = 0.0, yMin = 0.0, yMax = 0.0;
            bool hasMin = false, hasMax = false;
        };
        struct IbisCurveTable {
            std::string kind;
            bool isVt = false;
            std::vector<IbisCurvePt> pts;
        };

        std::string ibisVer, compName, mfgName, activeModel, modelType;
        char commentChar = '|';
        double rPkg = 0.0, lPkg = 0.0, cPkg = 0.0;
        bool hasPkgRlc = false;
        double vinl = 0.8, vinh = 2.0, cComp = 0.0;
        bool hasThresh = false, hasRamp = false;
        double dvDtRise = 0.0, dvDtFall = 0.0;

        std::vector<IbisPinEntry> pins;
        std::vector<IbisCurveTable> curves;
        int curSec = 0;

        std::vector<std::string_view> toks;
        size_t pos = 0;
        while (pos < sv.size()) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;

            while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
            while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
            if (ln.empty() || ln.front() == commentChar) continue;

            size_t cmtP = ln.find(commentChar);
            if (cmtP != std::string_view::npos) {
                ln = ln.substr(0, cmtP);
                while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
                if (ln.empty()) continue;
            }

            if (ln.front() == '[') {
                size_t rb = ln.find(']');
                if (rb == std::string_view::npos) continue;
                std::string kw = DetailBoardPlacementEda::LowerAsciiSymSes(ln.substr(1, rb - 1));
                std::string_view arg = ln.substr(rb + 1);
                while (!arg.empty() && static_cast<unsigned char>(arg.front()) <= 0x20u) arg.remove_prefix(1);

                curSec = 0;
                if (kw == "end") break;
                else if (kw == "ibis ver") ibisVer = std::string(arg);
                else if (kw == "comment char") {
                    if (!arg.empty()) commentChar = arg[0];
                } else if (kw == "component") compName = std::string(arg);
                else if (kw == "manufacturer") mfgName = std::string(arg);
                else if (kw == "package") curSec = 1;
                else if (kw == "pin") curSec = 2;
                else if (kw == "model") {
                    if (activeModel.empty() && !arg.empty()) activeModel = std::string(arg);
                    curSec = 3;
                } else if (kw == "ramp") curSec = 4;
                else if (kw == "pulldown" || kw == "pullup" || kw == "gnd clamp" || kw == "power clamp" ||
                         kw == "rising waveform" || kw == "falling waveform")
                {
                    if (curves.size() < 24) {
                        IbisCurveTable ct;
                        ct.kind = std::string(ln.substr(1, rb - 1));
                        ct.isVt = (kw.find("waveform") != std::string::npos);
                        curves.push_back(std::move(ct));
                        curSec = 5;
                    }
                }
                continue;
            }

            if (curSec == 1) {
                splitWs(ln, toks);
                if (toks.size() >= 2) {
                    std::string k = DetailBoardPlacementEda::LowerAsciiSymSes(toks[0]);
                    double v = 0.0;
                    if (parseIbisVal(toks[1], v)) {
                        if (k == "r_pkg") { rPkg = v; hasPkgRlc = true; }
                        else if (k == "l_pkg") { lPkg = v; hasPkgRlc = true; }
                        else if (k == "c_pkg") { cPkg = v; hasPkgRlc = true; }
                    }
                }
            } else if (curSec == 2 && pins.size() < 256) {
                splitWs(ln, toks);
                if (toks.size() >= 3) {
                    IbisPinEntry pe;
                    pe.pinName   = std::string(toks[0]);
                    pe.sigName   = std::string(toks[1]);
                    pe.modelName = std::string(toks[2]);
                    if (toks.size() >= 6) {
                        bool okR = parseIbisVal(toks[3], pe.rPin);
                        bool okL = parseIbisVal(toks[4], pe.lPin);
                        bool okC = parseIbisVal(toks[5], pe.cPin);
                        pe.hasRlc = (okR || okL || okC);
                    }
                    pins.push_back(std::move(pe));
                }
            } else if (curSec == 3) {
                size_t eq = ln.find('=');
                std::string normLn(ln);
                if (eq != std::string_view::npos) normLn[eq] = ' ';
                splitWs(normLn, toks);
                if (toks.size() >= 2) {
                    std::string k = DetailBoardPlacementEda::LowerAsciiSymSes(toks[0]);
                    double v = 0.0;
                    if (k == "model_type" && modelType.empty()) modelType = std::string(toks[1]);
                    else if (k == "vinl" && parseIbisVal(toks[1], v)) { vinl = v; hasThresh = true; }
                    else if (k == "vinh" && parseIbisVal(toks[1], v)) { vinh = v; hasThresh = true; }
                    else if (k == "c_comp" && parseIbisVal(toks[1], v)) { cComp = v; }
                }
            } else if (curSec == 4) {
                std::string normLn(ln);
                for (char& ch : normLn) if (ch == '=' || ch == '/') ch = ' ';
                splitWs(normLn, toks);
                if (toks.size() >= 3) {
                    std::string k0 = DetailBoardPlacementEda::LowerAsciiSymSes(toks[0]);
                    std::string k1 = DetailBoardPlacementEda::LowerAsciiSymSes(toks[1]);
                    double dv = 0.0, dt = 0.0;
                    if (k0 == "dv" && parseIbisVal(toks[2], dv) && toks.size() >= 4 && parseIbisVal(toks[3], dt) && dt > 1e-15) {
                        if (k1 == "dt_r") { dvDtRise = (dv / dt) * 1e-9; hasRamp = true; }
                        else if (k1 == "dt_f") { dvDtFall = (dv / dt) * 1e-9; hasRamp = true; }
                    }
                }
            } else if (curSec == 5 && !curves.empty()) {
                if (ln.find('=') != std::string_view::npos) continue;
                splitWs(ln, toks);
                if (toks.size() >= 2 && curves.back().pts.size() < 1024) {
                    IbisCurvePt pt;
                    if (parseIbisVal(toks[0], pt.x) && parseIbisVal(toks[1], pt.yTyp)) {
                        if (toks.size() >= 3) pt.hasMin = parseIbisVal(toks[2], pt.yMin);
                        if (toks.size() >= 4) pt.hasMax = parseIbisVal(toks[3], pt.yMax);
                        curves.back().pts.push_back(pt);
                    }
                }
            }
        }

        size_t validViCurves = 0, validVtCurves = 0;
        for (auto& ct : curves) {
            if (ct.pts.size() >= 2) {
                std::sort(ct.pts.begin(), ct.pts.end(), [](const IbisCurvePt& a, const IbisCurvePt& b) { return a.x < b.x; });
                if (ct.isVt) ++validVtCurves;
                else         ++validViCurves;
            }
        }

        if (validViCurves == 0 && validVtCurves == 0 && pins.empty() && !hasRamp) {
            return false;
        }

        out.kind = EdaDocKind::KiCadSch;
        std::wstring title = L"IBIS High-Speed I/O Buffer Model (";
        if (!compName.empty()) title += PathUtils::Utf8ToWide(compName);
        else if (!activeModel.empty()) title += PathUtils::Utf8ToWide(activeModel);
        else title += L".ibs";
        if (!ibisVer.empty()) title += L", v" + PathUtils::Utf8ToWide(ibisVer);
        title += L")";
        out.formatTitle = title;

        const bool showVi = (validViCurves > 0);
        const bool showVt = (validVtCurves > 0 || (validViCurves == 0 && hasRamp));
        const float viX0 = -44.0f, viW = showVt ? 40.0f : 84.0f;
        const float vtX0 = showVi ? 4.0f : -44.0f, vtW = showVi ? 40.0f : 84.0f;
        const float plotY0 = -16.0f, plotH = 38.0f;
        out.labels.push_back({ -44.0f, plotY0 + plotH + 7.0f, 1.12f, EdaLayerId::SchLabel, title });

        if (showVi) {
            DetailIndustrialAndWaveEda::DrawGraticule(out, viX0, plotY0, viW, plotH, 8, 8);
            double vMin = 1e300, vMax = -1e300, iMin = 1e300, iMax = -1e300;
            for (const auto& ct : curves) {
                if (ct.isVt || ct.pts.size() < 2) continue;
                for (const auto& p : ct.pts) {
                    vMin = std::min(vMin, p.x); vMax = std::max(vMax, p.x);
                    double yc = std::clamp(p.yTyp, -0.5, 0.5);
                    iMin = std::min(iMin, yc); iMax = std::max(iMax, yc);
                }
            }
            if (!(vMax > vMin + 1e-9)) { vMin -= 1.0; vMax += 1.0; }
            if (!(iMax > iMin + 1e-9)) { iMin -= 0.01; iMax += 0.01; }

            wchar_t viHdr[128] = {};
            swprintf_s(viHdr, L"V-I Characteristics [%.2g..%.2g V, %.1f..%.1f mA]", vMin, vMax, iMin * 1e3, iMax * 1e3);
            out.labels.push_back({ viX0, plotY0 + plotH + 1.5f, 0.95f, EdaLayerId::SchLabel, viHdr });

            auto mapViX = [&](double v) -> float {
                return viX0 + 1.0f + static_cast<float>(std::clamp((v - vMin) / (vMax - vMin), 0.0, 1.0)) * (viW - 2.0f);
            };
            auto mapViY = [&](double iAmp) -> float {
                double ic = std::clamp(iAmp, iMin, iMax);
                return plotY0 + 1.0f + static_cast<float>(std::clamp((ic - iMin) / (iMax - iMin), 0.0, 1.0)) * (plotH - 2.0f);
            };

            if (iMin < 0.0 && iMax > 0.0) {
                float yZero = mapViY(0.0);
                out.traces.push_back({ viX0, yZero, viX0 + viW, yZero, 0.14f, EdaLayerId::SchSymbol, 0 });
            }

            int cIdx = 0;
            for (const auto& ct : curves) {
                if (ct.isVt || ct.pts.size() < 2) continue;
                std::string kl = DetailBoardPlacementEda::LowerAsciiSymSes(ct.kind);
                EdaLayerId lid = (kl == "pulldown") ? EdaLayerId::TopCopper
                               : (kl == "pullup")   ? EdaLayerId::BottomCopper
                               : (kl == "gnd clamp")? EdaLayerId::SchWire
                                                    : EdaLayerId::InnerCopper;
                for (size_t i = 1; i < ct.pts.size(); ++i) {
                    const auto& p0 = ct.pts[i - 1];
                    const auto& p1 = ct.pts[i];
                    out.traces.push_back({ mapViX(p0.x), mapViY(p0.yTyp), mapViX(p1.x), mapViY(p1.yTyp), 0.34f, lid, cIdx + 1 });
                    if (p0.hasMin && p1.hasMin) {
                        out.traces.push_back({ mapViX(p0.x), mapViY(p0.yMin), mapViX(p1.x), mapViY(p1.yMin), 0.14f, lid, cIdx + 1 });
                    }
                    if (p0.hasMax && p1.hasMax) {
                        out.traces.push_back({ mapViX(p0.x), mapViY(p0.yMax), mapViX(p1.x), mapViY(p1.yMax), 0.14f, lid, cIdx + 1 });
                    }
                }
                ++cIdx;
            }
        }

        if (showVt) {
            DetailIndustrialAndWaveEda::DrawGraticule(out, vtX0, plotY0, vtW, plotH, 8, 8);
            if (validVtCurves > 0) {
                double tMin = 1e300, tMax = -1e300, vMin = 1e300, vMax = -1e300;
                for (const auto& ct : curves) {
                    if (!ct.isVt || ct.pts.size() < 2) continue;
                    for (const auto& p : ct.pts) {
                        tMin = std::min(tMin, p.x); tMax = std::max(tMax, p.x);
                        vMin = std::min(vMin, p.yTyp); vMax = std::max(vMax, p.yTyp);
                    }
                }
                if (!(tMax > tMin + 1e-15)) { tMax = tMin + 1e-9; }
                if (!(vMax > vMin + 1e-9))  { vMin -= 0.5; vMax += 0.5; }

                wchar_t vtHdr[128] = {};
                swprintf_s(vtHdr, L"V-T Switching Waveforms [0..%.2f ns, %.2f..%.2f V]", (tMax - tMin) * 1e9, vMin, vMax);
                out.labels.push_back({ vtX0, plotY0 + plotH + 1.5f, 0.95f, EdaLayerId::SchLabel, vtHdr });

                auto mapVtX = [&](double t) -> float {
                    return vtX0 + 1.0f + static_cast<float>(std::clamp((t - tMin) / (tMax - tMin), 0.0, 1.0)) * (vtW - 2.0f);
                };
                auto mapVtY = [&](double v) -> float {
                    return plotY0 + 1.0f + static_cast<float>(std::clamp((v - vMin) / (vMax - vMin), 0.0, 1.0)) * (plotH - 2.0f);
                };

                if (hasThresh) {
                    if (vinl >= vMin && vinl <= vMax) {
                        float yl = mapVtY(vinl);
                        out.traces.push_back({ vtX0, yl, vtX0 + vtW, yl, 0.14f, EdaLayerId::SchSymbol, 0 });
                        out.labels.push_back({ vtX0 + 0.8f, yl + 0.4f, 0.72f, EdaLayerId::SchLabel, L"Vinl" });
                    }
                    if (vinh >= vMin && vinh <= vMax) {
                        float yh = mapVtY(vinh);
                        out.traces.push_back({ vtX0, yh, vtX0 + vtW, yh, 0.14f, EdaLayerId::SchSymbol, 0 });
                        out.labels.push_back({ vtX0 + 0.8f, yh + 0.4f, 0.72f, EdaLayerId::SchLabel, L"Vinh" });
                    }
                }

                int wIdx = 10;
                for (const auto& ct : curves) {
                    if (!ct.isVt || ct.pts.size() < 2) continue;
                    bool isRise = (DetailBoardPlacementEda::LowerAsciiSymSes(ct.kind).find("rising") != std::string::npos);
                    EdaLayerId lid = isRise ? EdaLayerId::SchWire : EdaLayerId::InnerCopper;
                    for (size_t i = 1; i < ct.pts.size(); ++i) {
                        const auto& p0 = ct.pts[i - 1];
                        const auto& p1 = ct.pts[i];
                        out.traces.push_back({ mapVtX(p0.x), mapVtY(p0.yTyp), mapVtX(p1.x), mapVtY(p1.yTyp), 0.36f, lid, wIdx });
                        if (p0.hasMin && p1.hasMin) {
                            out.traces.push_back({ mapVtX(p0.x), mapVtY(p0.yMin), mapVtX(p1.x), mapVtY(p1.yMin), 0.14f, lid, wIdx });
                        }
                        if (p0.hasMax && p1.hasMax) {
                            out.traces.push_back({ mapVtX(p0.x), mapVtY(p0.yMax), mapVtX(p1.x), mapVtY(p1.yMax), 0.14f, lid, wIdx });
                        }
                    }
                    ++wIdx;
                }
            } else if (hasRamp) {
                out.labels.push_back({ vtX0, plotY0 + plotH + 1.5f, 0.95f, EdaLayerId::SchLabel, L"[Ramp] Slew-Rate Edges" });
                float rFrac = std::clamp(static_cast<float>(3.3 / std::max(0.2, dvDtRise)) / 5.0f, 0.08f, 0.85f);
                float fFrac = std::clamp(static_cast<float>(3.3 / std::max(0.2, dvDtFall)) / 5.0f, 0.08f, 0.85f);
                float xR0 = vtX0 + 4.0f, xR1 = xR0 + (vtW - 8.0f) * rFrac, xEnd = vtX0 + vtW - 4.0f;
                float yLo = plotY0 + 4.0f, yHi = plotY0 + plotH - 4.0f;
                out.traces.push_back({ xR0, yLo, xR1, yHi, 0.34f, EdaLayerId::SchWire, 10 });
                out.traces.push_back({ xR1, yHi, xEnd, yHi, 0.34f, EdaLayerId::SchWire, 10 });
                float xF1 = xR0 + (vtW - 8.0f) * fFrac;
                out.traces.push_back({ xR0, yHi, xF1, yLo, 0.34f, EdaLayerId::InnerCopper, 11 });
                out.traces.push_back({ xF1, yLo, xEnd, yLo, 0.34f, EdaLayerId::InnerCopper, 11 });
            }
        }

        {
            std::wstring meta = L"Model: " + PathUtils::Utf8ToWide(activeModel.empty() ? "IO_BUF" : activeModel);
            if (!modelType.empty()) meta += L" [" + PathUtils::Utf8ToWide(modelType) + L"]";
            if (cComp > 0.0) {
                wchar_t cb[48] = {};
                swprintf_s(cb, L" | C_comp=%.2f pF", cComp * 1e12);
                meta += cb;
            }
            if (hasPkgRlc) {
                wchar_t pb[80] = {};
                swprintf_s(pb, L" | Pkg RLC=(%.0f m\x03A9, %.2f nH, %.2f pF)", rPkg * 1e3, lPkg * 1e9, cPkg * 1e12);
                meta += pb;
            }
            out.labels.push_back({ -44.0f, plotY0 + plotH + 4.2f, 1.02f, EdaLayerId::SchLabel, meta });
        }

        if (!pins.empty()) {
            const size_t nShowPins = std::min<size_t>(pins.size(), 24u);
            const float stripW = std::max(28.0f, static_cast<float>(nShowPins) * 3.4f + 4.0f);
            const float stripCx = 0.0f, stripCy = plotY0 - 8.5f, stripH = 5.5f;
            DetailSiliconAndBusEda::AddRectOutline(out, stripCx, stripCy, stripW, stripH, EdaLayerId::SchSymbol, 0.28f);
            float startX = stripCx - stripW * 0.5f + 2.8f;
            float pitch = (nShowPins > 1) ? ((stripW - 5.6f) / static_cast<float>(nShowPins - 1)) : 0.0f;
            for (size_t pi = 0; pi < nShowPins; ++pi) {
                float px = startX + static_cast<float>(pi) * pitch;
                float py = stripCy - stripH * 0.5f - 1.5f;
                out.traces.push_back({ px, stripCy - stripH * 0.5f, px, py, 0.20f, EdaLayerId::SchWire, 0 });
                out.pads.push_back({ px, py, 0.55f, 0.55f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol,
                                     PathUtils::Utf8ToWide(pins[pi].pinName), PathUtils::Utf8ToWide(pins[pi].sigName) });
                if (nShowPins <= 16) {
                    out.labels.push_back({ px - 0.9f, stripCy - 0.3f, 0.68f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(pins[pi].sigName) });
                }
            }
            EdaComponent3D pkgComp;
            pkgComp.refDes = PathUtils::Utf8ToWide(compName.empty() ? "IBIS_IC" : compName);
            pkgComp.value = std::to_wstring(pins.size()) + L" pins";
            pkgComp.footprint = PathUtils::Utf8ToWide(mfgName.empty() ? "IBIS_PKG" : mfgName);
            pkgComp.x = stripCx; pkgComp.y = stripCy;
            pkgComp.widthMm = stripW; pkgComp.lengthMm = stripH;
            pkgComp.pinCount = static_cast<int>(pins.size());
            out.components.push_back(std::move(pkgComp));
        }

        out.netCount = std::max<size_t>(1u, validViCurves + validVtCurves + (pins.empty() ? 0u : 1u));
        return !out.traces.empty();
    }
    return false;
}

} // namespace Formats
