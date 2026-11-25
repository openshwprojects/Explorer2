#pragma once
// TclTimingPinCommon.h — Shared Tcl Line-Continuation Tokenizer & SDC/XDC/QSF Timing + Pinout Engine
// Eliminates duplicated Tcl/SDC/BGA/QFP parsing and rendering across XilinxConstraintsXdc.h,
// QuartusQsf.h, and SynopsysSdc.h.
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {
namespace TclTimingPin {

struct TclClockDef {
    std::string name;
    std::string targetPort;
    double periodNs = 10.0;
    double riseNs = 0.0;
    double fallNs = 5.0;
    bool isGenerated = false;
    std::string masterClk;
};

struct TclIoDelay {
    std::string portName;
    std::string clkName;
    double delayNs = 0.0;
    bool isOutput = false;
    bool isMin = false;
};

struct TclTimingException {
    std::string kind; // "false_path", "multicycle", "max_delay", "min_delay", "clock_groups"
    std::string fromObj;
    std::string toObj;
    double value = 0.0;
};

// Join backslash-continued lines and strip `#` comments outside quotes/braces, validating balanced [], {}, ""
inline bool ExtractLogicalTclCommands(
    std::string_view sv,
    std::vector<std::string>& outCmds)
{
    outCmds.clear();
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    int bracketDepth = 0;
    int braceDepth = 0;
    bool inQuote = false;

    std::string cur;
    size_t pos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        // Strip trailing \r
        while (!rawLine.empty() && rawLine.back() == '\r') rawLine.remove_suffix(1);

        // Strip comment `#` only if at start of command or preceded by whitespace/semicolon outside quotes & braces
        std::string cleaned;
        cleaned.reserve(rawLine.size());
        bool lineEsc = false;
        for (size_t i = 0; i < rawLine.size(); ++i) {
            const char c = rawLine[i];
            if (lineEsc) {
                cleaned.push_back(c);
                lineEsc = false;
                continue;
            }
            if (c == '\\') {
                if (i + 1 == rawLine.size()) {
                    // Line continuation backslash!
                    lineEsc = true;
                    break;
                }
                cleaned.push_back(c);
                lineEsc = true;
                continue;
            }
            if (c == '"' && braceDepth == 0) {
                inQuote = !inQuote;
                cleaned.push_back(c);
                continue;
            }
            if (!inQuote) {
                if (c == '{') ++braceDepth;
                else if (c == '}') {
                    --braceDepth;
                    if (braceDepth < 0) return false;
                } else if (c == '[' && braceDepth == 0) ++bracketDepth;
                else if (c == ']' && braceDepth == 0) {
                    --bracketDepth;
                    if (bracketDepth < 0) return false;
                } else if (c == '#' && braceDepth == 0 && bracketDepth == 0) {
                    const bool atCmdBoundary = cleaned.empty() ||
                        cleaned.back() == ' ' || cleaned.back() == '\t' || cleaned.back() == ';';
                    if (atCmdBoundary) break;
                }
            }
            cleaned.push_back(c);
        }

        if (!cur.empty()) cur.push_back(' ');
        cur += cleaned;

        // Continue accumulating if trailing backslash or open braces/brackets/quotes
        if (lineEsc || braceDepth > 0 || bracketDepth > 0 || inQuote) {
            continue;
        }

        // Split `cur` on top-level `;` outside quotes/braces/brackets
        size_t start = 0;
        int bd = 0, kd = 0;
        bool qt = false;
        for (size_t i = 0; i <= cur.size(); ++i) {
            if (i < cur.size()) {
                char c = cur[i];
                if (c == '\\' && i + 1 < cur.size()) { ++i; continue; }
                if (c == '"' && bd == 0) qt = !qt;
                else if (!qt) {
                    if (c == '{') ++bd;
                    else if (c == '}') --bd;
                    else if (c == '[' && bd == 0) ++kd;
                    else if (c == ']' && bd == 0) --kd;
                }
            }
            if (i == cur.size() || (cur[i] == ';' && !qt && bd == 0 && kd == 0)) {
                std::string_view seg = ReaderText::TrimSv(std::string_view(cur).substr(start, i - start));
                if (!seg.empty()) outCmds.emplace_back(seg);
                start = i + 1;
            }
        }
        cur.clear();
    }

    if (braceDepth != 0 || bracketDepth != 0 || inQuote) return false;
    std::string_view rem = ReaderText::TrimSv(cur);
    if (!rem.empty()) outCmds.emplace_back(rem);
    return true;
}

// Tokenize a single Tcl command keeping `{...}`, `[...]`, and `"..."` grouped cleanly
inline void TokenizeTclCommand(std::string_view cmd, std::vector<std::string>& outToks) {
    outToks.clear();
    size_t i = 0;
    const size_t n = cmd.size();
    while (i < n) {
        while (i < n && (cmd[i] == ' ' || cmd[i] == '\t' || cmd[i] == '\r' || cmd[i] == '\n')) ++i;
        if (i >= n) break;

        if (cmd[i] == '{') {
            int depth = 1;
            size_t start = ++i;
            while (i < n && depth > 0) {
                if (cmd[i] == '\\' && i + 1 < n) { i += 2; continue; }
                if (cmd[i] == '{') ++depth;
                else if (cmd[i] == '}') --depth;
                if (depth > 0) ++i;
            }
            outToks.emplace_back(cmd.substr(start, i - start));
            if (i < n && cmd[i] == '}') ++i;
        } else if (cmd[i] == '[') {
            int depth = 1;
            size_t start = i++;
            while (i < n && depth > 0) {
                if (cmd[i] == '\\' && i + 1 < n) { i += 2; continue; }
                if (cmd[i] == '[') ++depth;
                else if (cmd[i] == ']') --depth;
                ++i;
            }
            outToks.emplace_back(cmd.substr(start, i - start));
        } else if (cmd[i] == '"') {
            ++i;
            std::string t;
            while (i < n && cmd[i] != '"') {
                if (cmd[i] == '\\' && i + 1 < n) ++i;
                t.push_back(cmd[i++]);
            }
            if (i < n && cmd[i] == '"') ++i;
            outToks.push_back(std::move(t));
        } else {
            size_t start = i;
            while (i < n && cmd[i] != ' ' && cmd[i] != '\t' && cmd[i] != '\r' && cmd[i] != '\n') {
                if (cmd[i] == '[') {
                    int depth = 1;
                    ++i;
                    while (i < n && depth > 0) {
                        if (cmd[i] == '[') ++depth;
                        else if (cmd[i] == ']') --depth;
                        ++i;
                    }
                } else {
                    ++i;
                }
            }
            outToks.emplace_back(cmd.substr(start, i - start));
        }
    }
}

inline std::string LowerAscii(std::string_view sv) {
    std::string r(sv);
    for (char& c : r) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return r;
}

inline std::string UpperAscii(std::string_view sv) {
    std::string r(sv);
    for (char& c : r) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return r;
}

// Extract port/pin/clock/net names from e.g. `[get_ports {clk rst_n data[0]}]`, `[all_inputs]`, `{a b}`
inline void ExtractTargetNamesFromArg(std::string_view arg, std::vector<std::string>& outNames) {
    arg = ReaderText::TrimSv(arg);
    if (arg.empty()) return;
    if (arg.front() == '[' && arg.back() == ']') {
        arg = ReaderText::TrimSv(arg.substr(1, arg.size() - 2));
        std::vector<std::string> inner;
        TokenizeTclCommand(arg, inner);
        if (inner.empty()) return;
        std::string fn = LowerAscii(inner[0]);
        if (fn == "all_inputs") { outNames.push_back("ALL_INPUTS"); return; }
        if (fn == "all_outputs") { outNames.push_back("ALL_OUTPUTS"); return; }
        if (fn == "all_clocks") { outNames.push_back("ALL_CLOCKS"); return; }
        for (size_t i = 1; i < inner.size(); ++i) {
            if (!inner[i].empty() && inner[i][0] == '-') {
                // Flags like -filter {NAME =~ ...} take 1 arg unless boolean (-quiet, -hierarchical, -of_objects)
                std::string fl = LowerAscii(inner[i]);
                if (fl == "-filter" || fl == "-of_objects" || fl == "-of" || fl == "-patterns") {
                    ++i;
                }
                continue;
            }
            std::vector<std::string> sub;
            TokenizeTclCommand(inner[i], sub);
            if (sub.size() > 1) {
                for (auto& s : sub) {
                    if (!s.empty() && s[0] != '-') outNames.push_back(std::move(s));
                }
            } else if (!inner[i].empty()) {
                outNames.push_back(inner[i]);
            }
        }
        return;
    }

    // Plain name or brace-unwrapped space-separated list (but preserve bus `sig[3]`)
    std::vector<std::string> sub;
    TokenizeTclCommand(arg, sub);
    for (auto& s : sub) {
        if (!s.empty() && s[0] != '-') outNames.push_back(std::move(s));
    }
}

inline std::string ExtractFirstTargetName(std::string_view arg) {
    std::vector<std::string> nms;
    ExtractTargetNamesFromArg(arg, nms);
    return nms.empty() ? std::string{} : nms.front();
}

inline bool FindFlagValue(const std::vector<std::string>& toks, std::string_view flagLower, std::string& outVal) {
    for (size_t i = 1; i + 1 < toks.size(); ++i) {
        if (LowerAscii(toks[i]) == flagLower) {
            outVal = toks[i + 1];
            return true;
        }
    }
    return false;
}

inline bool HasFlag(const std::vector<std::string>& toks, std::string_view flagLower) {
    for (size_t i = 1; i < toks.size(); ++i) {
        if (LowerAscii(toks[i]) == flagLower) return true;
    }
    return false;
}

// Parse standard SDC / XDC timing commands (`create_clock`, `create_generated_clock`, `set_input_delay`, etc.)
inline bool TryConsumeTimingCommand(
    const std::vector<std::string>& toks,
    std::vector<TclClockDef>& clocks,
    std::vector<TclIoDelay>& ioDelays,
    std::vector<TclTimingException>& exceptions,
    size_t& miscTimingCmdCount)
{
    if (toks.empty()) return false;
    const std::string cmd = LowerAscii(toks[0]);

    if (cmd == "create_clock") {
        TclClockDef ck;
        std::string perStr;
        if (FindFlagValue(toks, "-period", perStr)) {
            double p = 0.0;
            if (!ReaderText::ParseDoubleSv(perStr, p) || !(p > 0.0)) return false;
            ck.periodNs = p;
            ck.fallNs = p * 0.5;
        } else {
            return false; // `create_clock` requires `-period`
        }
        FindFlagValue(toks, "-name", ck.name);
        std::string wfStr;
        if (FindFlagValue(toks, "-waveform", wfStr)) {
            std::vector<std::string> wfToks;
            TokenizeTclCommand(wfStr, wfToks);
            if (wfToks.size() >= 2) {
                ReaderText::ParseDoubleSv(wfToks[0], ck.riseNs);
                ReaderText::ParseDoubleSv(wfToks[1], ck.fallNs);
            }
        }
        // Last non-flag argument is target port/pin/net if present
        for (size_t i = 1; i < toks.size(); ++i) {
            std::string fl = LowerAscii(toks[i]);
            if (fl == "-period" || fl == "-name" || fl == "-waveform" || fl == "-comment") { ++i; continue; }
            if (!fl.empty() && fl[0] == '-') continue;
            ck.targetPort = ExtractFirstTargetName(toks[i]);
        }
        if (ck.name.empty()) ck.name = ck.targetPort.empty() ? ("clk_" + std::to_string(clocks.size() + 1)) : ck.targetPort;
        clocks.push_back(std::move(ck));
        return true;
    }

    if (cmd == "create_generated_clock") {
        TclClockDef ck;
        ck.isGenerated = true;
        FindFlagValue(toks, "-name", ck.name);
        std::string srcStr, divStr, mulStr;
        if (FindFlagValue(toks, "-source", srcStr)) ck.masterClk = ExtractFirstTargetName(srcStr);
        double divBy = 1.0, mulBy = 1.0;
        if (FindFlagValue(toks, "-divide_by", divStr)) ReaderText::ParseDoubleSv(divStr, divBy);
        if (FindFlagValue(toks, "-multiply_by", mulStr)) ReaderText::ParseDoubleSv(mulStr, mulBy);
        double basePeriod = 10.0;
        for (const auto& m : clocks) {
            if (m.name == ck.masterClk || m.targetPort == ck.masterClk) { basePeriod = m.periodNs; break; }
        }
        ck.periodNs = std::clamp(basePeriod * std::max(0.01, divBy) / std::max(0.01, mulBy), 0.05, 1e6);
        ck.fallNs = ck.periodNs * 0.5;
        if (!toks.empty() && toks.back()[0] != '-') ck.targetPort = ExtractFirstTargetName(toks.back());
        if (ck.name.empty()) ck.name = ck.targetPort.empty() ? ("gen_clk_" + std::to_string(clocks.size() + 1)) : ck.targetPort;
        clocks.push_back(std::move(ck));
        return true;
    }

    if (cmd == "set_input_delay" || cmd == "set_output_delay") {
        TclIoDelay io;
        io.isOutput = (cmd == "set_output_delay");
        io.isMin = HasFlag(toks, "-min");
        std::string clkStr;
        if (FindFlagValue(toks, "-clock", clkStr)) io.clkName = ExtractFirstTargetName(clkStr);

        bool gotDelay = false;
        std::vector<std::string> targets;
        for (size_t i = 1; i < toks.size(); ++i) {
            std::string fl = LowerAscii(toks[i]);
            if (fl == "-clock" || fl == "-reference_pin") { ++i; continue; }
            if (fl == "-max" || fl == "-min" || fl == "-clock_fall" || fl == "-add_delay" ||
                fl == "-network_latency_included" || fl == "-source_latency_included") {
                continue;
            }
            if (!fl.empty() && fl[0] == '-') continue;
            double dv = 0.0;
            if (!gotDelay && ReaderText::ParseDoubleSv(toks[i], dv)) {
                io.delayNs = dv;
                gotDelay = true;
            } else {
                ExtractTargetNamesFromArg(toks[i], targets);
            }
        }
        if (!gotDelay) return false;
        if (targets.empty()) targets.push_back(io.isOutput ? "OUT_PORT" : "IN_PORT");
        for (const auto& t : targets) {
            if (ioDelays.size() >= 128u) break;
            TclIoDelay item = io;
            item.portName = t;
            ioDelays.push_back(std::move(item));
        }
        return true;
    }

    if (cmd == "set_false_path" || cmd == "set_multicycle_path" || cmd == "set_max_delay" ||
        cmd == "set_min_delay" || cmd == "set_clock_groups")
    {
        TclTimingException ex;
        ex.kind = cmd.substr(4); // strip "set_"
        std::string fStr, tStr, gStr;
        if (FindFlagValue(toks, "-from", fStr) || FindFlagValue(toks, "-rise_from", fStr) || FindFlagValue(toks, "-fall_from", fStr))
            ex.fromObj = ExtractFirstTargetName(fStr);
        if (FindFlagValue(toks, "-to", tStr) || FindFlagValue(toks, "-rise_to", tStr) || FindFlagValue(toks, "-fall_to", tStr))
            ex.toObj = ExtractFirstTargetName(tStr);
        if (cmd == "set_clock_groups") {
            std::vector<std::string> grps;
            for (size_t i = 1; i + 1 < toks.size(); ++i) {
                if (LowerAscii(toks[i]) == "-group") grps.push_back(ExtractFirstTargetName(toks[i + 1]));
            }
            if (grps.size() >= 1) ex.fromObj = grps[0];
            if (grps.size() >= 2) ex.toObj = grps[1];
        }
        for (size_t i = 1; i < toks.size(); ++i) {
            double v = 0.0;
            if (toks[i][0] != '-' && ReaderText::ParseDoubleSv(toks[i], v)) { ex.value = v; break; }
        }
        if (exceptions.size() < 128u) exceptions.push_back(std::move(ex));
        return true;
    }

    if (cmd == "set_clock_uncertainty" || cmd == "set_clock_latency" || cmd == "set_clock_transition" ||
        cmd == "set_input_transition" || cmd == "set_load" || cmd == "set_driving_cell" ||
        cmd == "set_case_analysis" || cmd == "set_max_fanout" || cmd == "set_max_transition" ||
        cmd == "set_max_capacitance" || cmd == "set_operating_conditions" || cmd == "set_wire_load_model" ||
        cmd == "set_units" || cmd == "set_disable_timing" || cmd == "group_path" ||
        cmd == "derive_pll_clocks" || cmd == "derive_clock_uncertainty")
    {
        ++miscTimingCmdCount;
        return true;
    }

    return false;
}

// Check if `site` is a valid BGA ball ("A12", "AA4") OR numeric QFP/DIP pin ("84", "PIN_84", "P84")
inline bool NormalizePackagePinSite(std::string_view rawSite, std::string& outSite) {
    rawSite = ReaderText::TrimSv(rawSite);
    if (rawSite.empty()) return false;
    if (ReaderText::StartsWithCI(rawSite, "PIN_")) rawSite.remove_prefix(4);
    else if (ReaderText::StartsWithCI(rawSite, "BALL_")) rawSite.remove_prefix(5);

    int r = 0, c = 0, pnum = 0;
    if (FpgaPinCommon::ParseBgaCoord(rawSite, r, c)) {
        outSite = UpperAscii(rawSite);
        return true;
    }
    if (FpgaPinCommon::ParseNumericPackagePin(rawSite, pnum)) {
        outSite = std::to_string(pnum);
        return true;
    }
    return false;
}

// Render Timing Constraint Diagram (Clocks + Setup/Hold I/O Windows + CDC/Exception Paths)
inline bool RenderTimingDiagramBoard(
    const std::wstring& formatTitlePrefix,
    const std::vector<TclClockDef>& clocks,
    const std::vector<TclIoDelay>& ioDelays,
    const std::vector<TclTimingException>& exceptions,
    size_t miscTimingCmdCount,
    ParsedEdaBoard& outBoard)
{
    if (clocks.empty() && ioDelays.empty() && exceptions.empty() && miscTimingCmdCount == 0) {
        return false;
    }

    outBoard.kind = EdaDocKind::KiCadSch;

    const float frameW = 92.0f;
    float curY = 24.0f;

    // Header summary
    wchar_t hdrBuf[240] = {};
    swprintf_s(hdrBuf, L"%ls  [%zu clocks, %zu I/O delays, %zu exceptions]",
               formatTitlePrefix.c_str(), clocks.size(), ioDelays.size(), exceptions.size());
    outBoard.labels.push_back({ -frameW * 0.5f, curY + 4.0f, 1.35f, EdaLayerId::TopSilk, hdrBuf });

    // 1. Render clock waveform lanes scaled by relative period
    double maxPeriod = 10.0;
    for (const auto& ck : clocks) maxPeriod = std::max(maxPeriod, ck.periodNs);

    const size_t nClks = std::min<size_t>(clocks.size(), 12u);
    for (size_t ci = 0; ci < nClks; ++ci) {
        const auto& ck = clocks[ci];
        const float laneY = curY;
        const float halfAmp = 2.1f;
        const float xStart = -frameW * 0.45f + 16.0f;
        const float xEnd   = frameW * 0.48f;
        const float waveSpan = xEnd - xStart;

        const double freqMHz = (ck.periodNs > 1e-6) ? (1000.0 / ck.periodNs) : 0.0;
        wchar_t clkLbl[160] = {};
        swprintf_s(clkLbl, L"%hs (%.3g ns / %.1f MHz)%ls",
                   ck.name.c_str(), ck.periodNs, freqMHz, ck.isGenerated ? L" [GEN]" : L"");
        outBoard.labels.push_back({ -frameW * 0.48f, laneY - 0.4f, 0.95f, EdaLayerId::SchLabel, clkLbl });

        outBoard.pads.push_back({
            xStart - 1.5f, laneY, 1.1f, 1.1f, 0.0f, 0.0f, 0, EdaLayerId::InnerCopper,
            PathUtils::Utf8ToWide(ck.name), PathUtils::Utf8ToWide(ck.name)
        });

        // Number of cycles across window proportional to maxPeriod / ck.periodNs (clamped 2..12)
        const int nCycles = std::clamp(static_cast<int>(std::round((maxPeriod / std::max(0.05, ck.periodNs)) * 3.0)), 2, 12);
        const float cycW = waveSpan / static_cast<float>(nCycles);
        const float duty = std::clamp(static_cast<float>((ck.fallNs - ck.riseNs) / std::max(0.05, ck.periodNs)), 0.15f, 0.85f);
        const EdaLayerId lyr = ck.isGenerated ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper;
        const int netId = static_cast<int>(ci + 1);

        float x = xStart;
        for (int k = 0; k < nCycles; ++k) {
            const float xHighEnd = x + cycW * duty;
            const float xNext = x + cycW;
            outBoard.traces.push_back({ x, laneY - halfAmp, x, laneY + halfAmp, 0.28f, lyr, netId });
            outBoard.traces.push_back({ x, laneY + halfAmp, xHighEnd, laneY + halfAmp, 0.28f, lyr, netId });
            outBoard.traces.push_back({ xHighEnd, laneY + halfAmp, xHighEnd, laneY - halfAmp, 0.28f, lyr, netId });
            outBoard.traces.push_back({ xHighEnd, laneY - halfAmp, xNext, laneY - halfAmp, 0.28f, lyr, netId });
            x = xNext;
        }

        EdaComponent3D clkComp;
        clkComp.refDes = PathUtils::Utf8ToWide(ck.name);
        clkComp.value = clkLbl;
        clkComp.footprint = ck.isGenerated ? L"PLL_GEN_CLK" : L"SDC_CLOCK";
        clkComp.x = (xStart + xEnd) * 0.5f;
        clkComp.y = laneY;
        clkComp.widthMm = waveSpan;
        clkComp.lengthMm = halfAmp * 2.2f;
        clkComp.category = EdaPartCategory::Crystal;
        outBoard.components.push_back(std::move(clkComp));

        curY -= 6.5f;
    }

    // 2. Render I/O Delay setup/hold timing bars
    const size_t nIo = std::min<size_t>(ioDelays.size(), 16u);
    for (size_t ii = 0; ii < nIo; ++ii) {
        const auto& io = ioDelays[ii];
        const float y = curY;
        const float x0 = -frameW * 0.45f + 16.0f;
        const float barW = std::clamp(6.0f + static_cast<float>(std::fabs(io.delayNs)) * 3.5f, 5.0f, 54.0f);
        const EdaLayerId lyr = io.isOutput ? EdaLayerId::BottomCopper : EdaLayerId::SchWire;

        DrawEdaBoxRect(outBoard, x0, y - 1.1f, x0 + barW, y + 1.1f, 0.24f, lyr, static_cast<int>(nClks + ii + 1));
        // Hatch data-uncertainty window
        outBoard.traces.push_back({ x0, y - 1.1f, x0 + barW, y + 1.1f, 0.16f, lyr, static_cast<int>(nClks + ii + 1) });
        outBoard.pads.push_back({
            x0 - 1.5f, y, 0.95f, 0.95f, 0.0f, 0.0f, 1, lyr,
            PathUtils::Utf8ToWide(io.portName), PathUtils::Utf8ToWide(io.clkName)
        });

        wchar_t ioLbl[160] = {};
        swprintf_s(ioLbl, L"%ls %hs  %.3g ns @ %hs",
                   io.isOutput ? L"OUT" : L"IN",
                   io.portName.c_str(), io.delayNs,
                   io.clkName.empty() ? "clk" : io.clkName.c_str());
        outBoard.labels.push_back({ -frameW * 0.48f, y - 0.35f, 0.85f, EdaLayerId::SchLabel, ioLbl });
        curY -= 3.8f;
    }

    // 3. Render Timing Exceptions (false_path / multicycle / clock_groups)
    const size_t nEx = std::min<size_t>(exceptions.size(), 12u);
    for (size_t ei = 0; ei < nEx; ++ei) {
        const auto& ex = exceptions[ei];
        const float y = curY;
        const float x0 = -frameW * 0.45f + 16.0f;
        const float x1 = x0 + 42.0f;
        outBoard.traces.push_back({ x0, y, x1, y, 0.22f, EdaLayerId::BottomSilk, 0 });
        if (ex.kind == "false_path" || ex.kind == "clock_groups") {
            // Draw 'X' cut marker in the middle of the false/async path
            const float mx = (x0 + x1) * 0.5f;
            outBoard.traces.push_back({ mx - 1.2f, y - 1.2f, mx + 1.2f, y + 1.2f, 0.28f, EdaLayerId::BottomCopper, 0 });
            outBoard.traces.push_back({ mx - 1.2f, y + 1.2f, mx + 1.2f, y - 1.2f, 0.28f, EdaLayerId::BottomCopper, 0 });
        }
        wchar_t exLbl[180] = {};
        swprintf_s(exLbl, L"%hs: %hs -> %hs",
                   ex.kind.c_str(),
                   ex.fromObj.empty() ? "*" : ex.fromObj.c_str(),
                   ex.toObj.empty() ? "*" : ex.toObj.c_str());
        outBoard.labels.push_back({ -frameW * 0.48f, y - 0.35f, 0.82f, EdaLayerId::TopSilk, exLbl });
        curY -= 3.4f;
    }

    // Fallback card if file only contained misc constraints (`set_load`, `set_driving_cell`, etc.)
    if (outBoard.traces.empty() && miscTimingCmdCount > 0) {
        DrawEdaBoxRect(outBoard, -24.0f, -6.0f, 24.0f, 6.0f, 0.30f, EdaLayerId::SchSymbol, 1);
        outBoard.labels.push_back({ -20.0f, -0.5f, 1.0f, EdaLayerId::SchLabel,
                                    std::to_wstring(miscTimingCmdCount) + L" SDC environment constraints" });
    }

    const float yBottom = std::min(curY - 1.5f, 16.0f);
    DrawEdaBoxRect(outBoard, -frameW * 0.52f, yBottom, frameW * 0.52f, 27.0f, 0.32f, EdaLayerId::EdgeCuts, 0);

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"%ls (%zu clocks, %zu I/O delays, %zu exceptions)",
               formatTitlePrefix.c_str(), clocks.size(), ioDelays.size(), exceptions.size());
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(1u, clocks.size() + ioDelays.size());
    return !outBoard.traces.empty();
}

} // namespace TclTimingPin
} // namespace Formats
