#pragma once
// SynopsysSdc.h — Synopsys Design Timing Constraints (.sdc) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseSynopsysSdcBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (ext == L".sdc") {
        for (unsigned char c : sv) {
            if (c == 0) return false;
        }
        // Validate balanced Tcl square/curly brackets so corrupt/truncated Tcl scripts fail cleanly
        {
            int sq = 0, cb = 0;
            bool inQ = false;
            for (size_t i = 0; i < sv.size(); ++i) {
                char c = sv[i];
                if (!inQ && c == '#') {
                    while (i < sv.size() && sv[i] != '\n') ++i;
                    continue;
                }
                if (c == '\\' && i + 1 < sv.size()) { ++i; continue; }
                if (c == '"') { inQ = !inQ; continue; }
                if (!inQ) {
                    if (c == '[') ++sq;
                    else if (c == ']') { if (--sq < 0) return false; }
                    else if (c == '{') ++cb;
                    else if (c == '}') { if (--cb < 0) return false; }
                }
            }
            if (inQ || sq != 0 || cb != 0) return false;
        }

        struct PinAssign { int row = 0, col = 0; std::wstring sig; std::wstring iostd; };
        struct ClockSpec { std::wstring name; double periodNs = 10.0; };
        struct IoDelaySpec { std::wstring port; double delayNs = 0.0; bool isOutput = false; };

        std::vector<PinAssign> pins;
        std::map<std::wstring, std::wstring> sigIoStd;
        std::vector<ClockSpec> clocks;
        std::vector<IoDelaySpec> ioDelays;
        size_t exceptionCount = 0;
        std::string partDevice;
        int maxRow = 7, maxCol = 7;

        size_t pos = 0;
        while (pos < sv.size() && pins.size() < 2048) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;
            size_t f = ln.find_first_not_of(" \t\r");
            if (f == std::string::npos || ln[f] == '#') continue;

            auto extractFlagArg = [&](const char* flag) -> std::string {
                size_t fp = ln.find(flag);
                if (fp == std::string::npos) return {};
                size_t s1 = ln.find_first_not_of(" \t\"", fp + std::strlen(flag));
                if (s1 == std::string::npos) return {};
                size_t s2 = ln.find_first_of(" \t\"\r]", s1);
                return ln.substr(s1, (s2 == std::string::npos ? ln.size() : s2) - s1);
            };
            auto extractGetPorts = [&]() -> std::string {
                size_t gp = ln.find("get_ports");
                if (gp == std::string::npos) gp = ln.find("get_pins");
                if (gp == std::string::npos) return {};
                size_t s1 = ln.find_first_not_of(" \t{[\"", gp + 9);
                size_t s2 = (s1 != std::string::npos) ? ln.find_first_of(" \t}]\"\r", s1) : std::string::npos;
                return (s1 != std::string::npos) ? ln.substr(s1, (s2 == std::string::npos ? ln.size() : s2) - s1) : std::string{};
            };

            if (ln.find("create_clock") != std::string::npos || ln.find("create_generated_clock") != std::string::npos) {
                std::string clkNm = extractFlagArg("-name");
                if (clkNm.empty()) clkNm = extractGetPorts();
                if (clkNm.empty()) clkNm = "clk" + std::to_string(clocks.size() + 1);
                double per = 10.0;
                std::string perS = extractFlagArg("-period");
                if (!perS.empty()) per = std::max(0.05, std::strtod(perS.c_str(), nullptr));
                else {
                    std::string divS = extractFlagArg("-divide_by");
                    if (!divS.empty() && !clocks.empty()) per = clocks.front().periodNs * std::max(1, std::atoi(divS.c_str()));
                }
                clocks.push_back({ PathUtils::Utf8ToWide(clkNm), per });
            } else if (ln.find("set_input_delay") != std::string::npos || ln.find("set_output_delay") != std::string::npos) {
                bool isOut = (ln.find("set_output_delay") != std::string::npos);
                std::string prt = extractGetPorts();
                // Find numeric delay token (first standalone float/number not preceded by -clock)
                double dly = 2.0;
                for (size_t ti = 0; ti < ln.size(); ++ti) {
                    if ((ln[ti] == '-' && ti + 1 < ln.size() && std::isdigit(static_cast<unsigned char>(ln[ti + 1]))) ||
                        (std::isdigit(static_cast<unsigned char>(ln[ti])) && (ti == 0 || ln[ti - 1] == ' ' || ln[ti - 1] == '\t'))) {
                        dly = std::strtod(ln.c_str() + ti, nullptr);
                        break;
                    }
                }
                if (ioDelays.size() < 64) {
                    ioDelays.push_back({ PathUtils::Utf8ToWide(prt.empty() ? "io" : prt), dly, isOut });
                }
            } else if (ln.find("set_false_path") != std::string::npos ||
                       ln.find("set_multicycle_path") != std::string::npos ||
                       ln.find("set_clock_groups") != std::string::npos ||
                       ln.find("set_max_delay") != std::string::npos) {
                ++exceptionCount;
            }

        }

        if (pins.empty() && clocks.empty() && ioDelays.empty() && exceptionCount == 0) {
            return false;
        }

        // Timing-only SDC/XDC diagram: render true period-scaled clock waveforms + I/O setup/hold budget bars!
        if (pins.empty()) {
            out.kind = EdaDocKind::KiCadSch;
            out.formatTitle = (ext == L".xdc")
                ? L"Xilinx Timing Constraints (.xdc — "
                : L"Synopsys Design Timing Constraints (.sdc — ";
            out.formatTitle += std::to_wstring(clocks.size()) + L" clocks, " + std::to_wstring(ioDelays.size()) + L" I/O delays)";

            double maxPer = 1.0;
            for (const auto& ck : clocks) maxPer = std::max(maxPer, ck.periodNs);
            const float x0 = -30.0f, plotW = 80.0f;
            float y = 24.0f;

            for (size_t ci = 0; ci < clocks.size() && ci < 16; ++ci) {
                const auto& ck = clocks[ci];
                double freqMhz = 1000.0 / std::max(0.01, ck.periodNs);
                wchar_t clkLbl[160] = {};
                swprintf_s(clkLbl, L"%s (T=%.3g ns, %.1f MHz)", ck.name.c_str(), ck.periodNs, freqMhz);
                out.labels.push_back({ -54.0f, y, 0.95f, EdaLayerId::SchLabel, clkLbl });

                // Number of cycles across window proportional to maxPer / ck.periodNs
                double cycles = std::clamp((maxPer * 3.0) / std::max(0.05, ck.periodNs), 1.5, 24.0);
                float halfPeriodW = static_cast<float>(plotW / (cycles * 2.0));
                float cx = x0;
                bool high = true;
                EdaLayerId lid = (ci & 1) ? EdaLayerId::TopCopper : EdaLayerId::SchWire;
                while (cx < x0 + plotW - 0.05f) {
                    float nx = std::min(x0 + plotW, cx + halfPeriodW);
                    float yLvl = y + (high ? 2.0f : -2.0f);
                    float yOpp = y + (high ? -2.0f : 2.0f);
                    out.traces.push_back({ cx, yLvl, nx, yLvl, 0.28f, lid, static_cast<int>(ci + 1) });
                    if (nx < x0 + plotW - 0.02f) {
                        out.traces.push_back({ nx, yLvl, nx, yOpp, 0.26f, lid, static_cast<int>(ci + 1) });
                    }
                    cx = nx;
                    high = !high;
                }
                y -= 6.5f;
            }

            for (size_t di = 0; di < ioDelays.size() && di < 16; ++di) {
                const auto& io = ioDelays[di];
                float barFrac = std::clamp(static_cast<float>(std::fabs(io.delayNs) / std::max(1.0, maxPer)), 0.08f, 0.95f);
                float bw = plotW * barFrac;
                EdaLayerId lid = io.isOutput ? EdaLayerId::BottomCopper : EdaLayerId::InnerCopper;
                DetailEda100::AddRectOutline(out, x0 + bw * 0.5f, y, bw, 3.0f, lid, 0.26f);
                wchar_t dLbl[128] = {};
                swprintf_s(dLbl, L"%s %s: %.2f ns", io.isOutput ? L"OUT" : L"IN", io.port.c_str(), io.delayNs);
                out.labels.push_back({ -54.0f, y, 0.9f, EdaLayerId::SchLabel, dLbl });
                y -= 5.2f;
            }
            if (exceptionCount > 0) {
                out.labels.push_back({ x0, y, 0.95f, EdaLayerId::TopSilk,
                    L"Timing Exceptions (false/multicycle/clock_groups): " + std::to_wstring(exceptionCount) });
            }
            out.netCount = std::max<size_t>(1, clocks.size() + ioDelays.size());
            return !out.traces.empty() || !out.labels.empty();
        }

        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = (ext == L".qsf") ? L"Intel Quartus FPGA Pinout & Settings (.qsf" :
                          (ext == L".xdc") ? L"Xilinx Vivado FPGA Constraints & Ballout (.xdc" :
                                             L"Synopsys Design Constraints (.sdc";
        if (!partDevice.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(partDevice);
        out.formatTitle += L")";

        int gridR = std::clamp(maxRow + 1, 6, 32);
        int gridC = std::clamp(maxCol + 1, 6, 32);
        float pitch = 1.6f;
        float pkgW = gridC * pitch + 4.0f, pkgH = gridR * pitch + 4.0f;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, pkgW, pkgH, EdaLayerId::EdgeCuts, 0.4f);

        for (int r = 0; r < gridR; ++r) {
            for (int c = 0; c < gridC; ++c) {
                float bx = (c - (gridC - 1) * 0.5f) * pitch;
                float by = ((gridR - 1) * 0.5f - r) * pitch;
                out.circles.push_back({ bx, by, 0.42f, 0.12f, false, EdaLayerId::BottomSilk });
            }
        }
        for (const auto& pa : pins) {
            float bx = (pa.col - (gridC - 1) * 0.5f) * pitch;
            float by = ((gridR - 1) * 0.5f - pa.row) * pitch;
            auto itStd = sigIoStd.find(pa.sig);
            std::wstring stdStr = (itStd != sigIoStd.end()) ? itStd->second : L"";
            EdaLayerId lid = (stdStr.find(L"LVDS") != std::wstring::npos || stdStr.find(L"DIFF") != std::wstring::npos)
                ? EdaLayerId::TopCopper : EdaLayerId::PadsThrough;
            out.pads.push_back({ bx, by, 0.95f, 0.95f, 0.0f, 0.0f, 0, lid, pa.sig, stdStr });
            out.labels.push_back({ bx, by, 0.65f, EdaLayerId::TopSilk, pa.sig });
        }
        EdaComponent3D fpgaDie;
        fpgaDie.refDes = partDevice.empty() ? L"U1_FPGA" : PathUtils::Utf8ToWide(partDevice);
        fpgaDie.footprint = L"FBGA";
        fpgaDie.widthMm = pkgW * 0.45f; fpgaDie.lengthMm = pkgH * 0.45f; fpgaDie.heightMm = 1.8f;
        fpgaDie.pinCount = static_cast<int>(pins.size());
        fpgaDie.category = EdaPartCategory::IC_Package;
        out.components.push_back(fpgaDie);
        out.netCount = std::max<size_t>(1, pins.size() + clocks.size());
        return true;
    }
    return false;
}

} // namespace Formats
