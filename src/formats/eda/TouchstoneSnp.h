#pragma once
// TouchstoneSnp.h — Unified IEEE / IBIS Touchstone 1.0 & 2.0 N-Port S/Y/Z/G/H-Parameter (.s1p..s16p, .snp, .ts) Reader
// Supports 1-port through 16-port+ network analyzer parameter matrices (MA, DB, RI in HZ/KHZ/MHZ/GHZ),
// Touchstone 2.0 `[Version]`, `[Number of Ports]`, `[Two-Port Data Order]`, `[Matrix Format]`, `[Network Data]`,
// and mixed-mode differential `SDD11`/`SDD21` extraction for multi-port differential channels.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailTouchstone {

inline int InferPortCountFromExt(const std::wstring& extLower) {
    // `.s<N>p` where N = 1..99
    if (extLower.size() >= 4 && extLower[0] == L'.' && extLower[1] == L's' && extLower.back() == L'p') {
        int n = 0;
        for (size_t i = 2; i + 1 < extLower.size(); ++i) {
            if (extLower[i] < L'0' || extLower[i] > L'9') return 0;
            n = n * 10 + static_cast<int>(extLower[i] - L'0');
        }
        if (n >= 1 && n <= 64) return n;
    }
    return 0;
}

struct ComplexPair {
    double re = 0.0;
    double im = 0.0;
};

inline ComplexPair PairToComplex(double a, double b, int fmtMode) {
    // fmtMode: 0 = DB (dB / deg), 1 = MA (mag / deg), 2 = RI (real / imag)
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    if (fmtMode == 2) {
        return { a, b };
    }
    const double mag = (fmtMode == 0) ? std::pow(10.0, a / 20.0) : a;
    const double rad = b * kDegToRad;
    return { mag * std::cos(rad), mag * std::sin(rad) };
}

inline double ComplexToDb(ComplexPair c) {
    const double mag = std::hypot(c.re, c.im);
    return 20.0 * std::log10(std::max(1e-15, mag));
}

} // namespace DetailTouchstone

inline bool ParseTouchstoneSnpBoard(
    std::string_view sv,
    const std::wstring& extLower,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailTouchstone;

    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.size() < 12 || sv.size() > (64u << 20)) return false;
    if (sv.find('\0') != std::string_view::npos) return false;

    int nPorts = InferPortCountFromExt(extLower);
    double freqScale = 1e9; // Touchstone default = GHZ
    char paramKind = 'S';
    int fmtMode = 1;        // Touchstone default = MA (0=DB, 1=MA, 2=RI)
    double z0Ohms = 50.0;
    bool twoPortOrder21_12 = true; // Touchstone 1.0 2-port order is S11 S21 S12 S22
    int matrixFormat = 0;          // 0 = Full, 1 = Lower, 2 = Upper
    bool sawOptionLine = false;
    bool inReferenceBlock = false;
    bool inNoiseData = false;

    std::vector<double> rawNums;
    rawNums.reserve(4096);

    size_t pos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        // Strip `!` Touchstone comments
        const size_t bang = rawLine.find('!');
        if (bang != std::string_view::npos) rawLine = rawLine.substr(0, bang);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        // Option line `# [HZ|KHZ|MHZ|GHZ] [S|Y|Z|G|H] [MA|DB|RI] [R <n>]`
        if (line.front() == '#') {
            sawOptionLine = true;
            std::string_view rem = line.substr(1), tok;
            while (ReaderText::NextTokenSv(rem, tok)) {
                if (ReaderText::AsciiEqCI(tok, "HZ"))       freqScale = 1.0;
                else if (ReaderText::AsciiEqCI(tok, "KHZ")) freqScale = 1e3;
                else if (ReaderText::AsciiEqCI(tok, "MHZ")) freqScale = 1e6;
                else if (ReaderText::AsciiEqCI(tok, "GHZ")) freqScale = 1e9;
                else if (tok.size() == 1 && (tok[0] == 'S' || tok[0] == 's' || tok[0] == 'Y' || tok[0] == 'y' ||
                                             tok[0] == 'Z' || tok[0] == 'z' || tok[0] == 'G' || tok[0] == 'g' ||
                                             tok[0] == 'H' || tok[0] == 'h')) {
                    paramKind = static_cast<char>(std::toupper(static_cast<unsigned char>(tok[0])));
                } else if (ReaderText::AsciiEqCI(tok, "DB")) fmtMode = 0;
                else if (ReaderText::AsciiEqCI(tok, "MA")) fmtMode = 1;
                else if (ReaderText::AsciiEqCI(tok, "RI")) fmtMode = 2;
                else if (ReaderText::AsciiEqCI(tok, "R")) {
                    std::string_view rTok;
                    double rv = 50.0;
                    if (ReaderText::NextTokenSv(rem, rTok) && ReaderText::ParseDoubleSv(rTok, rv) && rv > 0.0) {
                        z0Ohms = rv;
                    }
                }
            }
            continue;
        }

        // Touchstone 2.0 bracketed keywords
        if (line.front() == '[') {
            const size_t rb = line.find(']');
            if (rb == std::string_view::npos) return false;
            std::string_view kw = ReaderText::TrimSv(line.substr(1, rb - 1));
            std::string_view rest = ReaderText::TrimSv(line.substr(rb + 1));

            inReferenceBlock = false;
            if (ReaderText::AsciiEqCI(kw, "End")) {
                break;
            } else if (ReaderText::AsciiEqCI(kw, "Number of Ports")) {
                int np = 0;
                if (!ReaderText::ParseIntSv(rest, np) || np < 1 || np > 64) return false;
                nPorts = np;
            } else if (ReaderText::AsciiEqCI(kw, "Two-Port Data Order")) {
                if (ContainsTextCI(rest, "12_21")) twoPortOrder21_12 = false;
                else if (ContainsTextCI(rest, "21_12")) twoPortOrder21_12 = true;
            } else if (ReaderText::AsciiEqCI(kw, "Matrix Format")) {
                if (ReaderText::AsciiEqCI(rest, "Lower")) matrixFormat = 1;
                else if (ReaderText::AsciiEqCI(rest, "Upper")) matrixFormat = 2;
                else matrixFormat = 0;
            } else if (ReaderText::AsciiEqCI(kw, "Reference")) {
                // Reference impedances may follow on same line or next line(s) before `[Network Data]`
                if (rest.empty()) inReferenceBlock = true;
            } else if (ReaderText::AsciiEqCI(kw, "Noise Data")) {
                inNoiseData = true;
            } else if (ReaderText::AsciiEqCI(kw, "Network Data")) {
                inNoiseData = false;
            }
            continue;
        }

        if (inReferenceBlock || inNoiseData) continue;

        // Parse numeric matrix tokens on this line; in Touchstone 1.0, noise data starts when frequency stops increasing
        std::string_view rem = line, tok;
        while (ReaderText::NextTokenSv(rem, tok)) {
            double v = 0.0;
            if (!ReaderText::ParseDoubleSv(tok, v) || !std::isfinite(v)) return false;
            rawNums.push_back(v);
            if (rawNums.size() > 4000000u) break;
        }
    }

    if (!sawOptionLine || rawNums.size() < 6u) return false;

    // Infer N if `.snp` / `.ts` had no `[Number of Ports]` keyword:
    // Each frequency row has `1 + 2 * entriesPerFreq` numbers (`3, 9, 19, 33, ...`)
    if (nPorts <= 0) {
        for (int candN : { 2, 4, 1, 3, 6, 8, 12, 16 }) {
            const size_t stride = 1u + 2u * static_cast<size_t>(candN) * static_cast<size_t>(candN);
            if (rawNums.size() >= stride * 2u && (rawNums.size() % stride) == 0u) {
                // Check strictly increasing frequencies
                bool mono = true;
                for (size_t r = 1u; r < std::min<size_t>(rawNums.size() / stride, 8u); ++r) {
                    if (!(rawNums[r * stride] > rawNums[(r - 1u) * stride])) { mono = false; break; }
                }
                if (mono) { nPorts = candN; break; }
            }
        }
        if (nPorts <= 0) return false;
    }

    const size_t pairsPerPoint = (matrixFormat == 0)
        ? (static_cast<size_t>(nPorts) * static_cast<size_t>(nPorts))
        : (static_cast<size_t>(nPorts) * static_cast<size_t>(nPorts + 1) / 2u);
    const size_t rowStride = 1u + 2u * pairsPerPoint;
    if (rawNums.size() < rowStride * 2u) return false;

    struct FreqSample {
        double freqHz = 0.0;
        std::vector<ComplexPair> mat; // N x N row-major
    };
    std::vector<FreqSample> pts;
    pts.reserve(rawNums.size() / rowStride);

    const size_t maxRows = rawNums.size() / rowStride;
    for (size_t ri = 0; ri < maxRows; ++ri) {
        const double fVal = rawNums[ri * rowStride] * freqScale;
        if (fVal < 0.0) break;
        // Touchstone 1.0 appends optional 2-port noise table after network data with restarting frequency
        if (!pts.empty() && fVal <= pts.back().freqHz) {
            break;
        }

        FreqSample fs;
        fs.freqHz = fVal;
        fs.mat.assign(static_cast<size_t>(nPorts * nPorts), { 0.0, 0.0 });

        const double* p = &rawNums[ri * rowStride + 1u];
        if (nPorts == 2 && matrixFormat == 0) {
            const ComplexPair c0 = PairToComplex(p[0], p[1], fmtMode);
            const ComplexPair c1 = PairToComplex(p[2], p[3], fmtMode);
            const ComplexPair c2 = PairToComplex(p[4], p[5], fmtMode);
            const ComplexPair c3 = PairToComplex(p[6], p[7], fmtMode);
            fs.mat[0] = c0; // S11
            if (twoPortOrder21_12) {
                fs.mat[2] = c1; // S21
                fs.mat[1] = c2; // S12
            } else {
                fs.mat[1] = c1; // S12
                fs.mat[2] = c2; // S21
            }
            fs.mat[3] = c3; // S22
        } else if (matrixFormat == 0) {
            for (int r = 0; r < nPorts; ++r) {
                for (int c = 0; c < nPorts; ++c) {
                    const size_t idx = static_cast<size_t>(r * nPorts + c);
                    fs.mat[idx] = PairToComplex(p[idx * 2u], p[idx * 2u + 1u], fmtMode);
                }
            }
        } else if (matrixFormat == 1) { // Lower triangular (symmetric)
            size_t k = 0;
            for (int r = 0; r < nPorts; ++r) {
                for (int c = 0; c <= r; ++c, ++k) {
                    const ComplexPair cp = PairToComplex(p[k * 2u], p[k * 2u + 1u], fmtMode);
                    fs.mat[static_cast<size_t>(r * nPorts + c)] = cp;
                    fs.mat[static_cast<size_t>(c * nPorts + r)] = cp;
                }
            }
        } else { // Upper triangular (symmetric)
            size_t k = 0;
            for (int r = 0; r < nPorts; ++r) {
                for (int c = r; c < nPorts; ++c, ++k) {
                    const ComplexPair cp = PairToComplex(p[k * 2u], p[k * 2u + 1u], fmtMode);
                    fs.mat[static_cast<size_t>(r * nPorts + c)] = cp;
                    fs.mat[static_cast<size_t>(c * nPorts + r)] = cp;
                }
            }
        }

        pts.push_back(std::move(fs));
    }

    if (pts.size() < 2u) return false;

    // Build plotted dB magnitude curves (NO fake 3x2 mm EdaComponent3D boxes!)
    struct PlottedCurve {
        std::wstring name;
        EdaLayerId layer;
        std::vector<double> dbVals;
    };
    std::vector<PlottedCurve> curves;

    auto atMat = [&](const FreqSample& fs, int r1Based, int c1Based) -> ComplexPair {
        return fs.mat[static_cast<size_t>((r1Based - 1) * nPorts + (c1Based - 1))];
    };

    // Always include S11 return loss and (for >=2 ports) S21 insertion loss
    {
        PlottedCurve s11{ std::wstring(1, static_cast<wchar_t>(paramKind)) + L"11 (Return Loss)", EdaLayerId::TopCopper, {} };
        s11.dbVals.reserve(pts.size());
        for (const auto& fs : pts) s11.dbVals.push_back(ComplexToDb(atMat(fs, 1, 1)));
        curves.push_back(std::move(s11));
    }
    if (nPorts >= 2) {
        PlottedCurve s21{ std::wstring(1, static_cast<wchar_t>(paramKind)) + L"21 (Insertion Loss)", EdaLayerId::BottomCopper, {} };
        s21.dbVals.reserve(pts.size());
        for (const auto& fs : pts) s21.dbVals.push_back(ComplexToDb(atMat(fs, 2, 1)));
        curves.push_back(std::move(s21));
    }
    if (nPorts == 3) {
        PlottedCurve s31{ std::wstring(1, static_cast<wchar_t>(paramKind)) + L"31 (Coupled)", EdaLayerId::SchWire, {} };
        s31.dbVals.reserve(pts.size());
        for (const auto& fs : pts) s31.dbVals.push_back(ComplexToDb(atMat(fs, 3, 1)));
        curves.push_back(std::move(s31));
    }
    if (nPorts >= 4 && paramKind == 'S') {
        // Mixed-mode differential SDD11 & SDD21 (standard port pairing 1+3 vs 2+4, or 1+2->3+4:
        // IEEE P370 / VNA 4-port convention: Diff Pair 1 = (Port 1, Port 3) or (Port 1, Port 2) —
        // compute SDD11 = 0.5*(S11 - S13 - S31 + S33) and SDD21 = 0.5*(S21 - S23 - S41 + S43))
        PlottedCurve sdd11{ L"SDD11 (Diff Return Loss)", EdaLayerId::SchWire, {} };
        PlottedCurve sdd21{ L"SDD21 (Diff Insertion Loss)", EdaLayerId::InnerCopper, {} };
        sdd11.dbVals.reserve(pts.size());
        sdd21.dbVals.reserve(pts.size());
        for (const auto& fs : pts) {
            const ComplexPair m11 = atMat(fs, 1, 1), m13 = atMat(fs, 1, 3), m31 = atMat(fs, 3, 1), m33 = atMat(fs, 3, 3);
            const ComplexPair d11{ 0.5 * (m11.re - m13.re - m31.re + m33.re), 0.5 * (m11.im - m13.im - m31.im + m33.im) };
            sdd11.dbVals.push_back(ComplexToDb(d11));

            const ComplexPair m21 = atMat(fs, 2, 1), m23 = atMat(fs, 2, 3), m41 = atMat(fs, 4, 1), m43 = atMat(fs, 4, 3);
            const ComplexPair d21{ 0.5 * (m21.re - m23.re - m41.re + m43.re), 0.5 * (m21.im - m23.im - m41.im + m43.im) };
            sdd21.dbVals.push_back(ComplexToDb(d21));
        }
        curves.push_back(std::move(sdd11));
        curves.push_back(std::move(sdd21));
    }

    outBoard.kind = EdaDocKind::KiCadSch;

    const float plotX0 = -48.0f, plotW = 72.0f;
    const float plotY0 = -22.0f, plotH = 44.0f;
    DetailIndustrialAndWaveEda::DrawGraticule(outBoard, plotX0, plotY0, plotW, plotH, 10, 8);

    double fMin = pts.front().freqHz;
    double fMax = pts.back().freqHz;
    if (!(fMax > fMin)) fMax = fMin + 1.0;

    double dbMin = 0.0, dbMax = -60.0;
    bool firstDb = true;
    for (const auto& c : curves) {
        for (double v : c.dbVals) {
            const double vc = std::clamp(v, -120.0, 40.0);
            if (firstDb) { dbMin = vc; dbMax = vc; firstDb = false; }
            else { dbMin = std::min(dbMin, vc); dbMax = std::max(dbMax, vc); }
        }
    }
    if (dbMax - dbMin < 3.0) { dbMin -= 3.0; dbMax += 3.0; }

    const size_t stride = std::max<size_t>(1u, pts.size() / 1024u);
    for (size_t ci = 0; ci < curves.size(); ++ci) {
        const auto& c = curves[ci];
        bool hasPrev = false;
        float prevX = 0.0f, prevY = 0.0f;
        for (size_t i = 0; i < pts.size(); i += stride) {
            const double u = (pts[i].freqHz - fMin) / (fMax - fMin);
            const double v = (std::clamp(c.dbVals[i], dbMin, dbMax) - dbMin) / (dbMax - dbMin);
            const float px = plotX0 + 1.0f + static_cast<float>(u) * (plotW - 2.0f);
            const float py = plotY0 + 1.0f + static_cast<float>(v) * (plotH - 2.0f);
            if (hasPrev) {
                outBoard.traces.push_back({ prevX, prevY, px, py, 0.32f, c.layer, static_cast<int>(ci + 1) });
            }
            prevX = px; prevY = py; hasPrev = true;
        }
        if (hasPrev && (pts.size() - 1u) % stride != 0u) {
            const size_t last = pts.size() - 1u;
            const double u = (pts[last].freqHz - fMin) / (fMax - fMin);
            const double v = (std::clamp(c.dbVals[last], dbMin, dbMax) - dbMin) / (dbMax - dbMin);
            const float px = plotX0 + 1.0f + static_cast<float>(u) * (plotW - 2.0f);
            const float py = plotY0 + 1.0f + static_cast<float>(v) * (plotH - 2.0f);
            outBoard.traces.push_back({ prevX, prevY, px, py, 0.32f, c.layer, static_cast<int>(ci + 1) });
        }

        wchar_t legBuf[128] = {};
        swprintf_s(legBuf, L"%ls [%.1f .. %.1f dB]",
                   c.name.c_str(),
                   *std::min_element(c.dbVals.begin(), c.dbVals.end()),
                   *std::max_element(c.dbVals.begin(), c.dbVals.end()));
        outBoard.labels.push_back({
            plotX0 + 1.5f, plotY0 - 3.2f - static_cast<float>(ci) * 2.4f, 0.90f, c.layer, legBuf
        });
    }

    // Smith Chart for S11 on the right side (cx = +44, cy = 0, r = 17)
    if (paramKind == 'S') {
        const float smCx = 44.0f, smCy = 0.0f, smR = 17.0f;
        outBoard.circles.push_back({ smCx, smCy, smR, 0.28f, false, EdaLayerId::EdgeCuts });
        // Constant resistance circles r = 0.5, 1.0, 2.0
        for (float rNorm : { 0.5f, 1.0f, 2.0f }) {
            const float rad = smR / (1.0f + rNorm);
            const float cx  = smCx + smR * (rNorm / (1.0f + rNorm));
            outBoard.circles.push_back({ cx, smCy, rad, 0.12f, false, EdaLayerId::BottomSilk });
        }
        outBoard.traces.push_back({ smCx - smR, smCy, smCx + smR, smCy, 0.12f, EdaLayerId::BottomSilk, 0 });
        outBoard.labels.push_back({ smCx - 7.5f, smCy + smR + 1.8f, 0.95f, EdaLayerId::TopSilk, L"S11 Smith Chart" });

        bool hasPrev = false;
        float prevX = 0.0f, prevY = 0.0f;
        for (size_t i = 0; i < pts.size(); i += stride) {
            const ComplexPair g = atMat(pts[i], 1, 1);
            const float gx = smCx + std::clamp(static_cast<float>(g.re), -1.15f, 1.15f) * smR;
            const float gy = smCy + std::clamp(static_cast<float>(g.im), -1.15f, 1.15f) * smR;
            if (hasPrev) {
                outBoard.traces.push_back({ prevX, prevY, gx, gy, 0.30f, EdaLayerId::TopCopper, 1 });
            }
            prevX = gx; prevY = gy; hasPrev = true;
        }
    }

    wchar_t hdrBuf[220] = {};
    swprintf_s(hdrBuf, L"Touchstone %d-Port %c-Parameters (%.4g .. %.4g GHz, Z0=%.4g \x03A9, %zu pts)",
               nPorts, paramKind, fMin * 1e-9, fMax * 1e-9, z0Ohms, pts.size());
    outBoard.labels.push_back({ plotX0, plotY0 + plotH + 2.2f, 1.15f, EdaLayerId::TopSilk, hdrBuf });
    outBoard.formatTitle = hdrBuf;
    outBoard.netCount = curves.size();
    return !outBoard.traces.empty();
}

// Overload for legacy 2-arg call sites (`ParseTouchstoneSnpBoard(sv, outBoard)`)
inline bool ParseTouchstoneSnpBoard(std::string_view sv, ParsedEdaBoard& outBoard) {
    return ParseTouchstoneSnpBoard(sv, L"", outBoard);
}

} // namespace Formats
