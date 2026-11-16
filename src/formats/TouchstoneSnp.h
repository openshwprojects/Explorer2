#pragma once
// TouchstoneSnp.h — EIA/IBIS Touchstone RF Network Parameter (.s1p / .s2p) Frequency Response Parser

namespace Formats {

inline bool ParseTouchstoneSnpBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.empty()) return false;

    enum class TsFormat : uint8_t { MA = 0, DB = 1, RI = 2 };
    double freqUnitHz = 1e9; // Touchstone spec default: GHz
    std::wstring freqUnitLbl = L"GHz";
    std::string paramName = "S";
    TsFormat dataFmt = TsFormat::MA; // Touchstone spec default: MA
    double z0Ref = 50.0;             // Touchstone spec default: R 50
    bool hasOptionLine = false;

    std::wstring ext = PathUtils::GetExtension(out.filePath);
    int numPorts = (ext == L".s2p") ? 2 : ((ext == L".s1p") ? 1 : 0);

    std::vector<double> rawNums;
    rawNums.reserve(1024);
    bool stopMatrix = false;

    size_t pos = 0;
    while (pos < sv.size() && !stopMatrix) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;

        // Strip '!' comments anywhere on the line
        size_t bang = ln.find('!');
        if (bang != std::string_view::npos) ln = ln.substr(0, bang);

        while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
        while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln[0] == '[') {
            std::string low = PathUtils::ToLowerAscii(ln);
            if (low.rfind("[number of ports]", 0) == 0) {
                size_t rb = low.find(']');
                if (rb != std::string::npos) {
                    int np = std::atoi(low.c_str() + rb + 1);
                    if (np == 1 || np == 2) numPorts = np;
                }
            } else if (low.rfind("[noise data]", 0) == 0 || low.rfind("[end]", 0) == 0) {
                stopMatrix = true;
            }
            continue;
        }

        if (ln[0] == '#') {
            hasOptionLine = true;
            std::string opt = PathUtils::ToLowerAscii(ln.substr(1));
            size_t p = 0;
            while (p < opt.size()) {
                while (p < opt.size() && static_cast<unsigned char>(opt[p]) <= 0x20u) ++p;
                if (p >= opt.size()) break;
                size_t s = p;
                while (p < opt.size() && static_cast<unsigned char>(opt[p]) > 0x20u) ++p;
                std::string tok = opt.substr(s, p - s);

                if (tok == "hz")       { freqUnitHz = 1.0;  freqUnitLbl = L"Hz"; }
                else if (tok == "khz") { freqUnitHz = 1e3;  freqUnitLbl = L"kHz"; }
                else if (tok == "mhz") { freqUnitHz = 1e6;  freqUnitLbl = L"MHz"; }
                else if (tok == "ghz") { freqUnitHz = 1e9;  freqUnitLbl = L"GHz"; }
                else if (tok == "db")  { dataFmt = TsFormat::DB; }
                else if (tok == "ma")  { dataFmt = TsFormat::MA; }
                else if (tok == "ri")  { dataFmt = TsFormat::RI; }
                else if (tok == "s" || tok == "y" || tok == "z" || tok == "g" || tok == "h") {
                    paramName.assign(1, static_cast<char>(std::toupper(static_cast<unsigned char>(tok[0]))));
                } else if (tok == "r") {
                    while (p < opt.size() && static_cast<unsigned char>(opt[p]) <= 0x20u) ++p;
                    size_t rs = p;
                    while (p < opt.size() && static_cast<unsigned char>(opt[p]) > 0x20u) ++p;
                    if (p > rs) {
                        double rz = std::strtod(opt.substr(rs, p - rs).c_str(), nullptr);
                        if (std::isfinite(rz) && rz > 0.0) z0Ref = rz;
                    }
                }
            }
            continue;
        }

        // Parse floating-point tokens (supporting multi-line wrapped .s2p records)
        std::string lnStr(ln);
        const char* cp = lnStr.c_str();
        while (*cp) {
            while (*cp && static_cast<unsigned char>(*cp) <= 0x20u) ++cp;
            if (!*cp) break;
            char* ep = nullptr;
            double v = std::strtod(cp, &ep);
            if (ep == cp || !std::isfinite(v)) return false;
            rawNums.push_back(v);
            cp = ep;
            if (rawNums.size() >= 72000) { stopMatrix = true; break; }
        }
    }

    if (!hasOptionLine) return false;
    (void)freqUnitHz;

    if (numPorts == 0) {
        numPorts = (rawNums.size() >= 18 && (rawNums.size() % 9 == 0)) ? 2 : 1;
    }
    const size_t stride = (numPorts == 2) ? 9u : 3u;
    if (rawNums.size() < stride * 2u) return false;

    auto pairToDb = [&](double a, double b) -> double {
        if (dataFmt == TsFormat::DB) {
            return a;
        }
        if (dataFmt == TsFormat::MA) {
            return 20.0 * std::log10(std::max(std::fabs(a), 1e-6));
        }
        // TsFormat::RI
        return 20.0 * std::log10(std::max(std::hypot(a, b), 1e-6));
    };

    struct RfPt {
        double freq = 0.0;
        double s11Db = 0.0;
        double s21Db = 0.0;
    };
    std::vector<RfPt> pts;
    pts.reserve(rawNums.size() / stride);

    for (size_t i = 0; i + stride <= rawNums.size() && pts.size() < 4096; i += stride) {
        double f = rawNums[i];
        // Touchstone v1 .s2p noise data section starts after S-parameters with a lower restart frequency
        if (!pts.empty() && f <= pts.back().freq) {
            break;
        }
        RfPt pt;
        pt.freq = f;
        pt.s11Db = pairToDb(rawNums[i + 1], rawNums[i + 2]);
        if (numPorts == 2) {
            pt.s21Db = pairToDb(rawNums[i + 3], rawNums[i + 4]);
        }
        pts.push_back(pt);
    }

    if (pts.size() < 2) return false;

    double minF = pts.front().freq;
    double maxF = pts.back().freq;
    if (maxF <= minF) maxF = minF + 1.0;

    double rawDbMin = 1e300, rawDbMax = -1e300;
    for (const auto& p : pts) {
        rawDbMin = std::min(rawDbMin, p.s11Db);
        rawDbMax = std::max(rawDbMax, p.s11Db);
        if (numPorts == 2) {
            rawDbMin = std::min(rawDbMin, p.s21Db);
            rawDbMax = std::max(rawDbMax, p.s21Db);
        }
    }
    if (!std::isfinite(rawDbMin) || !std::isfinite(rawDbMax)) return false;

    double yMinDb = std::floor((rawDbMin - 2.0) / 5.0) * 5.0;
    double yMaxDb = std::ceil((rawDbMax + 2.0) / 5.0) * 5.0;
    if (yMaxDb - yMinDb < 10.0) {
        yMinDb -= 5.0;
        yMaxDb += 5.0;
    }
    const double dbSpan = yMaxDb - yMinDb;

    out.kind = EdaDocKind::KiCadSch;
    const wchar_t* fmtTag = (dataFmt == TsFormat::DB) ? L"DB" : ((dataFmt == TsFormat::MA) ? L"MA" : L"RI");
    wchar_t titleBuf[160] = {};
    swprintf_s(titleBuf, L"Touchstone %d-Port RF %hs-Parameters (%ls, R=%.0f Ohm, %zu pts)",
        numPorts, paramName.c_str(), fmtTag, z0Ref, pts.size());
    out.formatTitle = titleBuf;

    const float chartW = 120.0f;
    const float chartH = 60.0f;

    // Graticule border + grid divisions + frequency/dB axis labels
    out.traces.push_back({ 0.0f,   0.0f,   chartW, 0.0f,   0.26f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ chartW, 0.0f,   chartW, chartH, 0.26f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ chartW, chartH, 0.0f,   chartH, 0.26f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ 0.0f,   chartH, 0.0f,   0.0f,   0.26f, EdaLayerId::SchSymbol, 0 });

    const int nyGrid = 6;
    for (int gy = 0; gy <= nyGrid; ++gy) {
        float y = (chartH * static_cast<float>(gy)) / static_cast<float>(nyGrid);
        if (gy > 0 && gy < nyGrid) {
            out.traces.push_back({ 0.0f, y, chartW, y, 0.08f, EdaLayerId::BottomSilk, 0 });
        }
        double dbVal = yMinDb + (dbSpan * static_cast<double>(gy)) / static_cast<double>(nyGrid);
        wchar_t dbLbl[32] = {};
        swprintf_s(dbLbl, L"%+.0f dB", dbVal);
        out.labels.push_back({ -14.5f, y + 0.6f, 0.9f, EdaLayerId::SchLabel, dbLbl });
    }

    // 0 dB reference line if within range
    if (yMinDb < 0.0 && yMaxDb > 0.0) {
        float y0Db = static_cast<float>(((0.0 - yMinDb) / dbSpan) * chartH);
        out.traces.push_back({ 0.0f, y0Db, chartW, y0Db, 0.16f, EdaLayerId::InnerCopper, 0 });
    }

    const int nxGrid = 5;
    for (int gx = 0; gx <= nxGrid; ++gx) {
        float x = (chartW * static_cast<float>(gx)) / static_cast<float>(nxGrid);
        if (gx > 0 && gx < nxGrid) {
            out.traces.push_back({ x, 0.0f, x, chartH, 0.08f, EdaLayerId::BottomSilk, 0 });
        }
        double fVal = minF + ((maxF - minF) * static_cast<double>(gx)) / static_cast<double>(nxGrid);
        wchar_t fLbl[48] = {};
        swprintf_s(fLbl, L"%.3g %ls", fVal, freqUnitLbl.c_str());
        out.labels.push_back({ x - 4.0f, -3.5f, 0.9f, EdaLayerId::SchLabel, fLbl });
    }

    std::wstring legend = (numPorts == 2)
        ? (L"|" + PathUtils::Utf8ToWide(paramName) + L"11| Return Loss (Green)  &  |" +
           PathUtils::Utf8ToWide(paramName) + L"21| Insertion Gain/Loss (Red)")
        : (L"|" + PathUtils::Utf8ToWide(paramName) + L"11| Reflection Magnitude (dB vs Frequency)");
    out.labels.push_back({ 2.0f, chartH + 3.2f, 1.05f, EdaLayerId::SchLabel, legend });

    auto mapX = [&](double f) -> float {
        return static_cast<float>(((f - minF) / (maxF - minF)) * chartW);
    };
    auto mapY = [&](double db) -> float {
        double u = std::clamp((db - yMinDb) / dbSpan, 0.0, 1.0);
        return static_cast<float>(u * chartH);
    };

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        float x0 = mapX(pts[i].freq);
        float x1 = mapX(pts[i + 1].freq);
        out.traces.push_back({ x0, mapY(pts[i].s11Db), x1, mapY(pts[i + 1].s11Db), 0.34f, EdaLayerId::SchWire, 1 });
        if (numPorts == 2) {
            out.traces.push_back({ x0, mapY(pts[i].s21Db), x1, mapY(pts[i + 1].s21Db), 0.34f, EdaLayerId::TopCopper, 2 });
        }
    }

    // Populate BOM entries for network responses
    {
        EdaComponent3D c11;
        c11.refDes = PathUtils::Utf8ToWide(paramName) + L"11";
        c11.value = L"Return Loss (dB)";
        c11.footprint = fmtTag;
        c11.x = chartW * 0.25f;
        c11.y = mapY(pts[pts.size() / 4].s11Db);
        c11.widthMm = 3.0f; c11.lengthMm = 2.0f;
        out.components.push_back(std::move(c11));
        if (numPorts == 2) {
            EdaComponent3D c21;
            c21.refDes = PathUtils::Utf8ToWide(paramName) + L"21";
            c21.value = L"Insertion (dB)";
            c21.footprint = fmtTag;
            c21.x = chartW * 0.75f;
            c21.y = mapY(pts[(pts.size() * 3) / 4].s21Db);
            c21.widthMm = 3.0f; c21.lengthMm = 2.0f;
            out.components.push_back(std::move(c21));
        }
    }

    out.netCount = static_cast<size_t>(numPorts);
    return !out.traces.empty();
}

} // namespace Formats
