#pragma once
// MultiPortTouchstone.h — Multi-Port RF S-Parameter Touchstone v1.0/v2.0 (.s3p / .s4p / .s8p) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseMultiPortTouchstoneBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    (void)sv;
    // #65. Multi-Port Single-Ended & Mixed-Mode Differential RF Touchstone (.s3p / .s4p / .s8p — v1.0 & v2.0)
    if (ext == L".s3p" || ext == L".s4p" || ext == L".s8p") {
        for (size_t i = 0; i < size; ++i) {
            if (data[i] == 0) return false;
        }

        int nPorts = (ext == L".s3p") ? 3 : ((ext == L".s8p") ? 8 : 4);
        enum class TsFmt : uint8_t { MA = 0, DB = 1, RI = 2 };
        TsFmt dataFmt = TsFmt::MA;
        std::wstring freqUnit = L"GHz";
        std::string paramKind = "S";
        double z0Ref = 50.0;
        bool hasOptionLine = false;
        bool isV2 = false;
        bool inNetData = false;
        int matrixFmt = 0;

        std::vector<double> rawNums;
        rawNums.reserve(4096);
        bool stopScan = false;

        size_t pos = 0;
        while (pos < sv.size() && !stopScan && rawNums.size() < 400000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;

            size_t excl = ln.find('!');
            if (excl != std::string_view::npos) ln = ln.substr(0, excl);
            while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
            while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
            if (ln.empty()) continue;

            if (ln.front() == '[') {
                std::string low = DetailKiCadSymSesPos::LowerAsciiSymSes(ln);
                if (low.rfind("[version]", 0) == 0) {
                    isV2 = true;
                } else if (low.rfind("[number of ports]", 0) == 0) {
                    isV2 = true;
                    size_t rb = low.find(']');
                    if (rb != std::string::npos) {
                        int np = std::atoi(low.c_str() + rb + 1);
                        if (np >= 1 && np <= 32) nPorts = np;
                    }
                } else if (low.rfind("[matrix format]", 0) == 0) {
                    isV2 = true;
                    if (low.find("lower") != std::string::npos) matrixFmt = 1;
                    else if (low.find("upper") != std::string::npos) matrixFmt = 2;
                    else matrixFmt = 0;
                } else if (low.rfind("[network data]", 0) == 0) {
                    isV2 = true;
                    inNetData = true;
                } else if (low.rfind("[noise data]", 0) == 0 || low.rfind("[end]", 0) == 0) {
                    stopScan = true;
                }
                continue;
            }

            if (ln.front() == '#') {
                hasOptionLine = true;
                std::string opt = DetailKiCadSymSesPos::LowerAsciiSymSes(ln.substr(1));
                size_t p = 0;
                while (p < opt.size()) {
                    while (p < opt.size() && static_cast<unsigned char>(opt[p]) <= 0x20u) ++p;
                    if (p >= opt.size()) break;
                    size_t s = p;
                    while (p < opt.size() && static_cast<unsigned char>(opt[p]) > 0x20u) ++p;
                    std::string tok = opt.substr(s, p - s);
                    if (tok == "hz")       freqUnit = L"Hz";
                    else if (tok == "khz") freqUnit = L"kHz";
                    else if (tok == "mhz") freqUnit = L"MHz";
                    else if (tok == "ghz") freqUnit = L"GHz";
                    else if (tok == "db")  dataFmt = TsFmt::DB;
                    else if (tok == "ma")  dataFmt = TsFmt::MA;
                    else if (tok == "ri")  dataFmt = TsFmt::RI;
                    else if (tok == "s" || tok == "y" || tok == "z" || tok == "g" || tok == "h") {
                        paramKind.assign(1, static_cast<char>(std::toupper(static_cast<unsigned char>(tok[0]))));
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

            if (isV2 && !inNetData) continue;

            std::string tmp(ln);
            const char* cp = tmp.c_str();
            while (*cp) {
                while (*cp && static_cast<unsigned char>(*cp) <= 0x20u) ++cp;
                if (!*cp) break;
                char* ep = nullptr;
                double v = std::strtod(cp, &ep);
                if (ep == cp || !std::isfinite(v)) return false;
                rawNums.push_back(v);
                cp = ep;
            }
        }

        if (!hasOptionLine) return false;

        const size_t complexEntries = (matrixFmt == 0)
            ? static_cast<size_t>(nPorts * nPorts)
            : static_cast<size_t>((nPorts * (nPorts + 1)) / 2);
        const size_t stride = 1u + complexEntries * 2u;
        if (rawNums.size() < stride * 2u) return false;

        struct Cplx { double re = 0.0, im = 0.0; };
        auto pairToCplx = [&](double a, double b) -> Cplx {
            if (dataFmt == TsFmt::RI) return { a, b };
            double mag = (dataFmt == TsFmt::DB) ? std::pow(10.0, a / 20.0) : a;
            double rad = b * (3.14159265358979323846 / 180.0);
            return { mag * std::cos(rad), mag * std::sin(rad) };
        };
        auto cplxDb = [](Cplx c) -> double {
            return 20.0 * std::log10(std::max(1e-6, std::hypot(c.re, c.im)));
        };

        struct MultiPortSample {
            double freq = 0.0;
            double c0Db = 0.0;
            double c1Db = 0.0;
            double c2Db = 0.0;
            double c3Db = 0.0;
        };
        std::vector<MultiPortSample> samples;
        const size_t nRows = std::min<size_t>(rawNums.size() / stride, 4096u);
        samples.reserve(nRows);

        std::vector<Cplx> mat(static_cast<size_t>(nPorts * nPorts));
        auto S = [&](int r1Based, int c1Based) -> Cplx {
            return mat[static_cast<size_t>((r1Based - 1) * nPorts + (c1Based - 1))];
        };

        for (size_t ri = 0; ri < nRows; ++ri) {
            const double* row = &rawNums[ri * stride];
            double f = row[0];
            if (!samples.empty() && f <= samples.back().freq) break;

            if (matrixFmt == 0) {
                for (int r = 0; r < nPorts; ++r) {
                    for (int c = 0; c < nPorts; ++c) {
                        size_t idx = static_cast<size_t>(r * nPorts + c);
                        mat[idx] = pairToCplx(row[1u + idx * 2u], row[2u + idx * 2u]);
                    }
                }
            } else if (matrixFmt == 1) {
                size_t k = 0;
                for (int r = 0; r < nPorts; ++r) {
                    for (int c = 0; c <= r; ++c, ++k) {
                        Cplx v = pairToCplx(row[1u + k * 2u], row[2u + k * 2u]);
                        mat[static_cast<size_t>(r * nPorts + c)] = v;
                        mat[static_cast<size_t>(c * nPorts + r)] = v;
                    }
                }
            } else {
                size_t k = 0;
                for (int r = 0; r < nPorts; ++r) {
                    for (int c = r; c < nPorts; ++c, ++k) {
                        Cplx v = pairToCplx(row[1u + k * 2u], row[2u + k * 2u]);
                        mat[static_cast<size_t>(r * nPorts + c)] = v;
                        mat[static_cast<size_t>(c * nPorts + r)] = v;
                    }
                }
            }

            MultiPortSample smp;
            smp.freq = f;
            if (nPorts >= 4) {
                Cplx s11 = S(1,1), s13 = S(1,3), s31 = S(3,1), s33 = S(3,3);
                Cplx s21 = S(2,1), s23 = S(2,3), s41 = S(4,1), s43 = S(4,3);
                Cplx sdd11{ 0.5 * (s11.re - s13.re - s31.re + s33.re), 0.5 * (s11.im - s13.im - s31.im + s33.im) };
                Cplx sdd21{ 0.5 * (s21.re - s23.re - s41.re + s43.re), 0.5 * (s21.im - s23.im - s41.im + s43.im) };
                smp.c0Db = cplxDb(sdd11);
                smp.c1Db = cplxDb(sdd21);
                smp.c2Db = cplxDb(s21);
                smp.c3Db = cplxDb(S(3, 1));
            } else {
                smp.c0Db = cplxDb(S(1, 1));
                smp.c1Db = (nPorts >= 2) ? cplxDb(S(2, 1)) : smp.c0Db;
                smp.c2Db = (nPorts >= 3) ? cplxDb(S(3, 1)) : smp.c1Db;
                smp.c3Db = smp.c2Db;
            }
            samples.push_back(smp);
        }

        if (samples.size() < 2) return false;

        double minF = samples.front().freq;
        double maxF = samples.back().freq;
        if (maxF <= minF) maxF = minF + 1.0;

        double dbMin = 1e300, dbMax = -1e300;
        const int nTraces = (nPorts >= 4) ? 4 : std::min(nPorts, 3);
        for (const auto& s : samples) {
            dbMin = std::min({ dbMin, s.c0Db, s.c1Db, s.c2Db });
            dbMax = std::max({ dbMax, s.c0Db, s.c1Db, s.c2Db });
            if (nTraces >= 4) { dbMin = std::min(dbMin, s.c3Db); dbMax = std::max(dbMax, s.c3Db); }
        }
        dbMin = std::max(-90.0, std::floor((dbMin - 2.0) / 5.0) * 5.0);
        dbMax = std::min(25.0,  std::ceil((dbMax + 2.0) / 5.0) * 5.0);
        if (dbMax - dbMin < 10.0) { dbMin -= 5.0; dbMax += 5.0; }
        const double dbSpan = dbMax - dbMin;

        out.kind = EdaDocKind::KiCadSch;
        const wchar_t* fmtName = (dataFmt == TsFmt::DB) ? L"DB" : ((dataFmt == TsFmt::RI) ? L"RI" : L"MA");
        wchar_t titleBuf[180] = {};
        swprintf_s(titleBuf, L"Touchstone %d-Port RF %hs-Matrix (%ls, R=%.0f \x03A9, %zu pts%ls)",
            nPorts, paramKind.c_str(), fmtName, z0Ref, samples.size(),
            (nPorts >= 4) ? L", Mixed-Mode SDD" : L"");
        out.formatTitle = titleBuf;

        const float x0 = -40.0f, y0 = -24.0f, w = 84.0f, h = 48.0f;
        DetailEda200::DrawGraticule(out, x0, y0, w, h, 8, 6);

        for (int gy = 0; gy <= 6; ++gy) {
            float yy = y0 + (h * static_cast<float>(gy)) / 6.0f;
            double dv = dbMin + (dbSpan * static_cast<double>(gy)) / 6.0;
            wchar_t dl[32] = {};
            swprintf_s(dl, L"%+.0f dB", dv);
            out.labels.push_back({ x0 - 11.5f, yy - 0.3f, 0.82f, EdaLayerId::SchLabel, dl });
        }
        for (int gx = 0; gx <= 4; ++gx) {
            float xx = x0 + (w * static_cast<float>(gx)) / 4.0f;
            double fv = minF + ((maxF - minF) * static_cast<double>(gx)) / 4.0;
            wchar_t fl[48] = {};
            swprintf_s(fl, L"%.3g %ls", fv, freqUnit.c_str());
            out.labels.push_back({ xx - 3.2f, y0 - 2.8f, 0.82f, EdaLayerId::SchLabel, fl });
        }

        auto mapX = [&](double f) -> float {
            return x0 + static_cast<float>(std::clamp((f - minF) / (maxF - minF), 0.0, 1.0)) * w;
        };
        auto mapY = [&](double db) -> float {
            return y0 + static_cast<float>(std::clamp((db - dbMin) / dbSpan, 0.0, 1.0)) * h;
        };

        for (size_t i = 1; i < samples.size(); ++i) {
            const auto& p0 = samples[i - 1];
            const auto& p1 = samples[i];
            float sx0 = mapX(p0.freq), sx1 = mapX(p1.freq);
            out.traces.push_back({ sx0, mapY(p0.c0Db), sx1, mapY(p1.c0Db), 0.34f, EdaLayerId::TopCopper, 1 });
            out.traces.push_back({ sx0, mapY(p0.c1Db), sx1, mapY(p1.c1Db), 0.36f, EdaLayerId::SchWire, 2 });
            if (nTraces >= 3) {
                out.traces.push_back({ sx0, mapY(p0.c2Db), sx1, mapY(p1.c2Db), 0.24f, EdaLayerId::BottomCopper, 3 });
            }
            if (nTraces >= 4) {
                out.traces.push_back({ sx0, mapY(p0.c3Db), sx1, mapY(p1.c3Db), 0.22f, EdaLayerId::InnerCopper, 4 });
            }
        }

        const wchar_t* cNamesDiff[4] = { L"SDD11 (Diff Return Loss)", L"SDD21 (Diff Insertion)", L"S21 (Single-Ended)", L"S31 (NEXT Crosstalk)" };
        const wchar_t* cNames3P[3]   = { L"S11 (Return Loss)", L"S21 (Port 1->2)", L"S31 (Port 1->3)" };
        out.labels.push_back({ x0, y0 + h + 1.8f, 0.96f, EdaLayerId::SchLabel,
            (nPorts >= 4) ? L"Traces: SDD21 (Green), SDD11 (Red), S21 (Blue), S31 (Purple)"
                          : L"Traces: S21 (Green), S11 (Red), S31 (Blue)" });

        for (int ci = 0; ci < nTraces; ++ci) {
            EdaComponent3D cc;
            cc.refDes = (nPorts >= 4) ? cNamesDiff[ci] : cNames3P[ci];
            cc.value = fmtName;
            cc.footprint = L"TOUCHSTONE_PORT";
            size_t mid = samples.size() / 2;
            double mv = (ci == 0) ? samples[mid].c0Db : (ci == 1 ? samples[mid].c1Db : (ci == 2 ? samples[mid].c2Db : samples[mid].c3Db));
            cc.x = mapX(samples[mid].freq);
            cc.y = mapY(mv);
            cc.widthMm = 3.0f; cc.lengthMm = 2.0f;
            out.components.push_back(std::move(cc));
        }

        out.netCount = static_cast<size_t>(nPorts);
        return !out.traces.empty();
    }
    return false;
}

} // namespace Formats
