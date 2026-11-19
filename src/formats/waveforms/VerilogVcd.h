#pragma once
// VerilogVcd.h — IEEE 1364 Verilog Value Change Dump (.vcd) Digital, Multi-Bit Bus & Real Waveform Parser

namespace Formats {

inline bool ParseVerilogVcdWaveforms(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("$var") == std::string_view::npos) return false;

    enum class VcdValKind : uint8_t { Scalar0 = 0, Scalar1 = 1, ScalarX = 2, ScalarZ = 3, Bus = 4, Real = 5 };
    struct VcdEvent {
        double time = 0.0;
        VcdValKind kind = VcdValKind::ScalarX;
        double realVal = 0.0;
        std::string busLabel;
    };
    struct VcdSig {
        std::string idCode;
        std::wstring fullName;
        std::string varType;
        int width = 1;
        bool isReal = false;
        std::vector<VcdEvent> events;
    };

    auto nextToken = [&](size_t& p) -> std::string_view {
        while (p < sv.size() && static_cast<unsigned char>(sv[p]) <= 0x20u) ++p;
        if (p >= sv.size()) return {};
        size_t s = p;
        while (p < sv.size() && static_cast<unsigned char>(sv[p]) > 0x20u) ++p;
        return sv.substr(s, p - s);
    };

    auto skipUntilEnd = [&](size_t& p) {
        while (p < sv.size()) {
            std::string_view t = nextToken(p);
            if (t.empty() || t == "$end") break;
        }
    };

    auto formatBitsToHex = [](std::string_view rawBits, int width, bool& outAllZ) -> std::string {
        outAllZ = false;
        std::string bits;
        bits.reserve(rawBits.size());
        for (char c : rawBits) {
            if (c == '_') continue;
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c == '0' || c == '1' || c == 'x' || c == 'z') bits.push_back(c);
        }
        if (bits.empty()) bits = "0";

        int targetW = std::clamp(width, 1, 256);
        if (static_cast<int>(bits.size()) < targetW) {
            char pad = (bits[0] == 'x' || bits[0] == 'z') ? bits[0] : '0';
            bits.insert(0, static_cast<size_t>(targetW - static_cast<int>(bits.size())), pad);
        } else if (static_cast<int>(bits.size()) > targetW) {
            bits.erase(0, bits.size() - static_cast<size_t>(targetW));
        }

        bool allZ = true;
        for (char c : bits) {
            if (c != 'z') { allZ = false; break; }
        }
        if (allZ) {
            outAllZ = true;
            return "Hi-Z";
        }

        size_t rem = bits.size() % 4u;
        if (rem != 0u) {
            char pad = (bits[0] == 'x' || bits[0] == 'z') ? bits[0] : '0';
            bits.insert(0, 4u - rem, pad);
        }

        std::string hex = "0x";
        for (size_t i = 0; i < bits.size(); i += 4u) {
            int val = 0;
            bool hasX = false, hasZ = false;
            for (size_t b = 0; b < 4u; ++b) {
                char c = bits[i + b];
                if (c == 'x') hasX = true;
                else if (c == 'z') hasZ = true;
                else val = (val << 1) | (c == '1' ? 1 : 0);
            }
            if (hasX) hex.push_back('X');
            else if (hasZ) hex.push_back('Z');
            else hex.push_back("0123456789ABCDEF"[val & 0xF]);
        }
        return hex;
    };

    std::vector<std::string> scopeStack;
    std::string timescaleStr;
    std::vector<VcdSig> sigs;
    std::unordered_map<std::string, std::vector<size_t>> idToSigs;

    size_t pos = 0;
    bool inDefs = true;
    double curTime = 0.0;
    double tMin = 0.0;
    double tMax = 0.0;
    bool seenTime = false;
    size_t totalValueChanges = 0;

    auto pushSigEvent = [&](std::string_view idSv, const VcdEvent& ev) {
        auto it = idToSigs.find(std::string(idSv));
        if (it == idToSigs.end()) return;
        if (!seenTime) {
            seenTime = true;
            tMin = curTime;
            tMax = curTime;
        }
        for (size_t idx : it->second) {
            auto& evs = sigs[idx].events;
            if (!evs.empty() && evs.back().time == ev.time) {
                evs.back() = ev;
            } else if (evs.size() < 4096) {
                evs.push_back(ev);
            }
            ++totalValueChanges;
        }
    };

    while (pos < sv.size()) {
        std::string_view tok = nextToken(pos);
        if (tok.empty()) break;

        if (inDefs) {
            if (tok == "$timescale") {
                std::string ts;
                while (pos < sv.size()) {
                    std::string_view t = nextToken(pos);
                    if (t.empty() || t == "$end") break;
                    ts += std::string(t);
                }
                timescaleStr = std::move(ts);
            } else if (tok == "$scope") {
                std::string_view stype = nextToken(pos);
                std::string_view sname = nextToken(pos);
                (void)stype;
                if (!sname.empty() && sname != "$end") {
                    scopeStack.emplace_back(sname);
                    skipUntilEnd(pos);
                }
            } else if (tok == "$upscope") {
                if (!scopeStack.empty()) scopeStack.pop_back();
                skipUntilEnd(pos);
            } else if (tok == "$var") {
                std::string_view vtype = nextToken(pos);
                std::string_view wTok  = nextToken(pos);
                std::string_view idTok = nextToken(pos);
                std::string refName;
                while (pos < sv.size()) {
                    std::string_view t = nextToken(pos);
                    if (t.empty() || t == "$end") break;
                    if (!refName.empty() && t[0] != '[') refName.push_back('_');
                    refName.append(t.data(), t.size());
                }
                if (!vtype.empty() && !wTok.empty() && !idTok.empty() && !refName.empty() && sigs.size() < 48) {
                    int w = std::atoi(std::string(wTok).c_str());
                    if (w <= 0) w = 1;
                    std::string fullPath;
                    for (const auto& sc : scopeStack) {
                        fullPath += sc;
                        fullPath.push_back('.');
                    }
                    fullPath += refName;

                    VcdSig sg;
                    sg.idCode = std::string(idTok);
                    sg.fullName = PathUtils::Utf8ToWide(fullPath);
                    sg.varType = std::string(vtype);
                    sg.width = w;
                    sg.isReal = (vtype == "real" || vtype == "realtime");
                    size_t idx = sigs.size();
                    sigs.push_back(std::move(sg));
                    idToSigs[std::string(idTok)].push_back(idx);
                }
            } else if (tok == "$enddefinitions") {
                skipUntilEnd(pos);
                inDefs = false;
            } else if (tok == "$dumpvars" || tok[0] == '#') {
                // Headerless transition into value changes
                inDefs = false;
                if (tok[0] == '#') {
                    double tv = std::strtod(std::string(tok.substr(1)).c_str(), nullptr);
                    if (std::isfinite(tv) && tv >= 0.0) {
                        curTime = tv;
                        if (!seenTime) { seenTime = true; tMin = tv; tMax = tv; }
                        else { tMin = std::min(tMin, tv); tMax = std::max(tMax, tv); }
                    }
                }
            } else if (tok[0] == '$') {
                skipUntilEnd(pos);
            }
            continue;
        }

        // Value-change section
        if (tok[0] == '#') {
            char numBuf[64] = {};
            size_t n = std::min<size_t>(tok.size() - 1, 63);
            std::memcpy(numBuf, tok.data() + 1, n);
            double tv = std::strtod(numBuf, nullptr);
            if (std::isfinite(tv) && tv >= 0.0) {
                curTime = tv;
                if (!seenTime) {
                    seenTime = true;
                    tMin = tv;
                    tMax = tv;
                } else {
                    tMin = std::min(tMin, tv);
                    tMax = std::max(tMax, tv);
                }
            }
        } else if (tok == "$comment") {
            skipUntilEnd(pos);
        } else if (tok[0] == '$') {
            // $dumpvars, $dumpon, $dumpoff, $dumpall, $end — value tokens inside block are normal VCD changes
            continue;
        } else if ((tok[0] == '0' || tok[0] == '1' || tok[0] == 'x' || tok[0] == 'X' ||
                    tok[0] == 'z' || tok[0] == 'Z') && tok.size() >= 2)
        {
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[0])));
            std::string_view idSv = tok.substr(1);
            auto it = idToSigs.find(std::string(idSv));
            if (it != idToSigs.end()) {
                for (size_t idx : it->second) {
                    VcdEvent ev;
                    ev.time = curTime;
                    if (sigs[idx].width > 1 && !sigs[idx].isReal) {
                        bool allZ = false;
                        char bitStr[2] = { c, 0 };
                        ev.busLabel = formatBitsToHex(bitStr, sigs[idx].width, allZ);
                        ev.kind = allZ ? VcdValKind::ScalarZ : VcdValKind::Bus;
                    } else {
                        ev.kind = (c == '1') ? VcdValKind::Scalar1
                                : (c == '0') ? VcdValKind::Scalar0
                                : (c == 'z') ? VcdValKind::ScalarZ
                                             : VcdValKind::ScalarX;
                    }
                    auto& evs = sigs[idx].events;
                    if (!evs.empty() && evs.back().time == ev.time) evs.back() = std::move(ev);
                    else if (evs.size() < 4096) evs.push_back(std::move(ev));
                    ++totalValueChanges;
                }
                if (!seenTime) { seenTime = true; tMin = curTime; tMax = curTime; }
            }
        } else if ((tok[0] == 'b' || tok[0] == 'B') && tok.size() >= 2) {
            std::string_view bitSv = tok.substr(1);
            std::string_view idSv = nextToken(pos);
            if (!idSv.empty()) {
                auto it = idToSigs.find(std::string(idSv));
                if (it != idToSigs.end()) {
                    for (size_t idx : it->second) {
                        VcdEvent ev;
                        ev.time = curTime;
                        if (sigs[idx].width <= 1 && !sigs[idx].isReal) {
                            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(bitSv.back())));
                            ev.kind = (c == '1') ? VcdValKind::Scalar1
                                    : (c == '0') ? VcdValKind::Scalar0
                                    : (c == 'z') ? VcdValKind::ScalarZ
                                                 : VcdValKind::ScalarX;
                        } else {
                            bool allZ = false;
                            ev.busLabel = formatBitsToHex(bitSv, sigs[idx].width, allZ);
                            ev.kind = allZ ? VcdValKind::ScalarZ : VcdValKind::Bus;
                        }
                        auto& evs = sigs[idx].events;
                        if (!evs.empty() && evs.back().time == ev.time) evs.back() = std::move(ev);
                        else if (evs.size() < 4096) evs.push_back(std::move(ev));
                        ++totalValueChanges;
                    }
                    if (!seenTime) { seenTime = true; tMin = curTime; tMax = curTime; }
                }
            }
        } else if ((tok[0] == 'r' || tok[0] == 'R') && tok.size() >= 2) {
            char numBuf[64] = {};
            size_t n = std::min<size_t>(tok.size() - 1, 63);
            std::memcpy(numBuf, tok.data() + 1, n);
            double rv = std::strtod(numBuf, nullptr);
            std::string_view idSv = nextToken(pos);
            if (!idSv.empty() && std::isfinite(rv)) {
                VcdEvent ev;
                ev.time = curTime;
                ev.kind = VcdValKind::Real;
                ev.realVal = rv;
                pushSigEvent(idSv, ev);
            }
        }
    }

    if (sigs.empty() || totalValueChanges == 0) return false;

    out.kind = EdaDocKind::KiCadSch;
    std::wstring tsWide = timescaleStr.empty() ? L"" : (L" [" + PathUtils::Utf8ToWide(timescaleStr) + L"]");
    out.formatTitle = L"IEEE 1364 Verilog VCD Logic Waveforms (" + std::to_wstring(sigs.size()) + L" signals" + tsWide + L")";

    if (tMax <= tMin) {
        tMax = tMin + 10.0;
    }
    const double tSpan = tMax - tMin;
    static constexpr float kWaveX0 = 12.0f;
    static constexpr float kWaveX1 = 195.0f;
    static constexpr float kWaveW  = kWaveX1 - kWaveX0;
    auto mapX = [&](double t) -> float {
        double u = std::clamp((t - tMin) / tSpan, 0.0, 1.0);
        return kWaveX0 + static_cast<float>(u * kWaveW);
    };

    const float rowPitch = 9.0f;
    const float halfH = 2.6f;
    const float topY = 6.0f;
    const float botY = -static_cast<float>(sigs.size()) * rowPitch + 2.0f;

    // Graticule frame & vertical time grid lines across [12..195]
    out.traces.push_back({ kWaveX0, topY, kWaveX1, topY, 0.22f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ kWaveX1, topY, kWaveX1, botY, 0.22f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ kWaveX1, botY, kWaveX0, botY, 0.22f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ kWaveX0, botY, kWaveX0, topY, 0.22f, EdaLayerId::SchSymbol, 0 });

    const int nGrid = 8;
    for (int g = 0; g <= nGrid; ++g) {
        float gx = kWaveX0 + (kWaveW * static_cast<float>(g)) / static_cast<float>(nGrid);
        if (g > 0 && g < nGrid) {
            out.traces.push_back({ gx, botY, gx, topY, 0.08f, EdaLayerId::BottomSilk, 0 });
        }
        double gt = tMin + (tSpan * static_cast<double>(g)) / static_cast<double>(nGrid);
        wchar_t tBuf[64] = {};
        if (std::fabs(gt - std::round(gt)) < 1e-6) {
            swprintf_s(tBuf, L"#%.0f", gt);
        } else {
            swprintf_s(tBuf, L"#%.2f", gt);
        }
        out.labels.push_back({ gx - 2.0f, topY + 2.2f, 0.9f, EdaLayerId::SchLabel, tBuf });
    }
    if (!timescaleStr.empty()) {
        out.labels.push_back({ -26.0f, topY + 2.2f, 0.95f, EdaLayerId::SchLabel, L"Timescale: " + PathUtils::Utf8ToWide(timescaleStr) });
    }

    for (size_t si = 0; si < sigs.size(); ++si) {
        const auto& s = sigs[si];
        const float yMid = -static_cast<float>(si) * rowPitch;
        const float yLo  = yMid - halfH;
        const float yHi  = yMid + halfH;
        const int netId  = static_cast<int>(si + 1);

        // Signal label on left + BOM entry
        std::wstring sigLbl = s.fullName;
        if (s.width > 1 && sigLbl.find(L'[') == std::wstring::npos) {
            sigLbl += L"[" + std::to_wstring(s.width - 1) + L":0]";
        }
        out.labels.push_back({ -28.0f, yMid + 0.6f, 1.0f, EdaLayerId::SchLabel, sigLbl });

        EdaComponent3D chComp;
        chComp.refDes = sigLbl;
        chComp.value = PathUtils::Utf8ToWide(s.varType) + L" (" + std::to_wstring(s.width) + L"b)";
        chComp.footprint = L"VCD_CH";
        chComp.x = -14.0f;
        chComp.y = yMid;
        chComp.widthMm = 6.0f;
        chComp.lengthMm = 3.0f;
        chComp.pinCount = s.width;
        out.components.push_back(std::move(chComp));

        if (s.events.empty()) {
            out.traces.push_back({ kWaveX0, yMid, kWaveX1, yMid, 0.16f, EdaLayerId::InnerCopper, netId });
            continue;
        }

        if (s.isReal || s.events.front().kind == VcdValKind::Real) {
            // Analog real-valued signal trace
            double rMin = 1e300, rMax = -1e300;
            for (const auto& ev : s.events) {
                rMin = std::min(rMin, ev.realVal);
                rMax = std::max(rMax, ev.realVal);
            }
            double rSpan = (rMax > rMin + 1e-12) ? (rMax - rMin) : 1.0;
            auto mapRY = [&](double rv) -> float {
                if (rMax <= rMin + 1e-12) return yMid;
                double n = (rv - rMin) / rSpan;
                return yLo + static_cast<float>(n * (2.0 * halfH));
            };

            for (size_t i = 0; i < s.events.size(); ++i) {
                float x0 = mapX(s.events[i].time);
                float x1 = (i + 1 < s.events.size()) ? mapX(s.events[i + 1].time) : kWaveX1;
                float y0 = mapRY(s.events[i].realVal);
                if (x1 > x0) {
                    out.traces.push_back({ x0, y0, x1, y0, 0.28f, EdaLayerId::TopCopper, netId });
                }
                if (i + 1 < s.events.size()) {
                    float y1 = mapRY(s.events[i + 1].realVal);
                    if (std::fabs(y1 - y0) > 0.02f) {
                        out.traces.push_back({ x1, y0, x1, y1, 0.24f, EdaLayerId::TopCopper, netId });
                    }
                }
            }
            wchar_t rngBuf[64] = {};
            swprintf_s(rngBuf, L"[%.3g..%.3g]", rMin, rMax);
            out.labels.push_back({ kWaveX1 + 2.0f, yMid + 0.5f, 0.85f, EdaLayerId::SchLabel, rngBuf });
        } else if (s.width > 1) {
            // Multi-bit bus envelope with double-edge X cross-overs + hex bus value labels
            for (size_t i = 0; i < s.events.size(); ++i) {
                const auto& ev = s.events[i];
                float x0 = mapX(ev.time);
                float x1 = (i + 1 < s.events.size()) ? mapX(s.events[i + 1].time) : kWaveX1;
                if (x1 <= x0 + 0.05f) continue;

                float dxL = (i > 0) ? std::min(0.95f, (x1 - x0) * 0.22f) : 0.0f;
                float dxR = (i + 1 < s.events.size()) ? std::min(0.95f, (x1 - x0) * 0.22f) : 0.0f;

                if (ev.kind == VcdValKind::ScalarZ) {
                    out.traces.push_back({ x0, yMid, x1, yMid, 0.22f, EdaLayerId::BottomCopper, netId });
                } else {
                    const EdaLayerId busLayer = (ev.busLabel.find('X') != std::string::npos)
                        ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper;

                    if (i > 0 && dxL > 0.02f) {
                        // Left half of cross-over opening from center
                        out.traces.push_back({ x0, yMid, x0 + dxL, yHi, 0.26f, busLayer, netId });
                        out.traces.push_back({ x0, yMid, x0 + dxL, yLo, 0.26f, busLayer, netId });
                    } else if (i == 0) {
                        out.traces.push_back({ x0, yLo, x0, yHi, 0.26f, busLayer, netId });
                    }

                    if (x1 - dxR > x0 + dxL) {
                        out.traces.push_back({ x0 + dxL, yHi, x1 - dxR, yHi, 0.28f, busLayer, netId });
                        out.traces.push_back({ x0 + dxL, yLo, x1 - dxR, yLo, 0.28f, busLayer, netId });
                    }

                    if (i + 1 < s.events.size() && dxR > 0.02f) {
                        // Right half of cross-over closing to center at transition edge
                        out.traces.push_back({ x1 - dxR, yHi, x1, yMid, 0.26f, busLayer, netId });
                        out.traces.push_back({ x1 - dxR, yLo, x1, yMid, 0.26f, busLayer, netId });
                    }

                    if (x1 - x0 >= 5.5f && !ev.busLabel.empty() && out.labels.size() < 1500) {
                        float lblX = x0 + dxL + 0.8f;
                        out.labels.push_back({ lblX, yMid + 0.5f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(ev.busLabel) });
                    }
                }
            }
        } else {
            // 1-bit digital logic trace (0, 1, X, Z)
            auto mapY = [&](VcdValKind k) -> float {
                if (k == VcdValKind::Scalar1) return yHi;
                if (k == VcdValKind::Scalar0) return yLo;
                return yMid;
            };
            for (size_t i = 0; i < s.events.size(); ++i) {
                const auto& ev = s.events[i];
                float x0 = mapX(ev.time);
                float x1 = (i + 1 < s.events.size()) ? mapX(s.events[i + 1].time) : kWaveX1;
                float y0 = mapY(ev.kind);
                EdaLayerId lid = (ev.kind == VcdValKind::ScalarX) ? EdaLayerId::TopCopper
                               : (ev.kind == VcdValKind::ScalarZ) ? EdaLayerId::BottomCopper
                                                                  : EdaLayerId::SchWire;
                if (x1 > x0) {
                    out.traces.push_back({ x0, y0, x1, y0, 0.30f, lid, netId });
                }
                if (i + 1 < s.events.size()) {
                    float y1 = mapY(s.events[i + 1].kind);
                    if (std::fabs(y1 - y0) > 0.02f) {
                        out.traces.push_back({ x1, y0, x1, y1, 0.28f, EdaLayerId::SchWire, netId });
                    }
                }
            }
        }
    }

    out.netCount = sigs.size();
    return !out.traces.empty();
}

} // namespace Formats
