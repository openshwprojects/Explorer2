#pragma once
// XilinxConstraintsXdc.h — Xilinx Vivado Design Constraints (.xdc) Reader
// Uses shared TclTimingPinCommon.h (with backslash line continuations, BGA + numeric QFP pin support,
// and SDC/XDC timing constraint diagram fallback) and FpgaPinConstraintsCommon.h.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseXilinxConstraintsXdcBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& ext,
    ParsedEdaBoard& out)
{
    if (ext != L".xdc" || !data || size < 10 || size > (16u << 20)) return false;

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    std::vector<std::string> cmds;
    if (!TclTimingPin::ExtractLogicalTclCommands(sv, cmds) || cmds.empty()) return false;

    std::vector<FpgaPinCommon::FpgaPinEntry> pins;
    std::unordered_map<std::string, size_t> pinBySignal;
    std::vector<TclTimingPin::TclClockDef> tclClocks;
    std::vector<TclTimingPin::TclIoDelay> ioDelays;
    std::vector<TclTimingPin::TclTimingException> exceptions;
    size_t miscTiming = 0;
    size_t recognizedCmds = 0;

    auto ensurePin = [&](const std::string& sig) -> FpgaPinCommon::FpgaPinEntry& {
        auto it = pinBySignal.find(sig);
        if (it != pinBySignal.end()) return pins[it->second];
        FpgaPinCommon::FpgaPinEntry pe;
        pe.signal = sig;
        if (ContainsTextCI(sig, "clk") || ContainsTextCI(sig, "clock")) pe.isClock = true;
        pinBySignal.emplace(sig, pins.size());
        pins.push_back(std::move(pe));
        return pins.back();
    };

    for (const auto& cmdStr : cmds) {
        std::vector<std::string> toks;
        TclTimingPin::TokenizeTclCommand(cmdStr, toks);
        if (toks.empty()) continue;

        const std::string verb = TclTimingPin::LowerAscii(toks[0]);
        if (verb == "set_property") {
            ++recognizedCmds;
            // Supports both `set_property PACKAGE_PIN A12 [get_ports clk]` AND
            // `set_property -dict { PACKAGE_PIN A12 IOSTANDARD LVCMOS33 } [get_ports {clk}]`
            std::string dictStr;
            std::vector<std::string> targets;
            if (!toks.empty()) TclTimingPin::ExtractTargetNamesFromArg(toks.back(), targets);
            if (targets.empty()) continue;

            if (TclTimingPin::FindFlagValue(toks, "-dict", dictStr)) {
                std::vector<std::string> kv;
                TclTimingPin::TokenizeTclCommand(dictStr, kv);
                for (size_t k = 0; k + 1 < kv.size(); k += 2) {
                    const std::string prop = TclTimingPin::UpperAscii(kv[k]);
                    const std::string& val = kv[k + 1];
                    for (const auto& sig : targets) {
                        auto& pe = ensurePin(sig);
                        if (prop == "PACKAGE_PIN" || prop == "LOC") {
                            std::string normSite;
                            pe.site = TclTimingPin::NormalizePackagePinSite(val, normSite) ? normSite : val;
                        } else if (prop == "IOSTANDARD") {
                            pe.ioStd = val;
                        } else if (prop == "PULLUP" && (ReaderText::AsciiEqCI(val, "true") || val == "1")) {
                            pe.hasPullup = true;
                        }
                    }
                }
            } else if (toks.size() >= 4) {
                // Find first two non-flag positional args (`<prop> <value>`) before `[get_ports ...]`
                std::vector<std::string> posArgs;
                for (size_t i = 1; i + 1 < toks.size(); ++i) {
                    if (!toks[i].empty() && toks[i][0] == '-') continue;
                    posArgs.push_back(toks[i]);
                }
                if (posArgs.size() >= 2) {
                    const std::string prop = TclTimingPin::UpperAscii(posArgs[0]);
                    const std::string& val = posArgs[1];
                    for (const auto& sig : targets) {
                        auto& pe = ensurePin(sig);
                        if (prop == "PACKAGE_PIN" || prop == "LOC") {
                            std::string normSite;
                            pe.site = TclTimingPin::NormalizePackagePinSite(val, normSite) ? normSite : val;
                        } else if (prop == "IOSTANDARD") {
                            pe.ioStd = val;
                        } else if (prop == "PULLUP" && (ReaderText::AsciiEqCI(val, "true") || val == "1")) {
                            pe.hasPullup = true;
                        }
                    }
                }
            }
        } else if (TclTimingPin::TryConsumeTimingCommand(toks, tclClocks, ioDelays, exceptions, miscTiming)) {
            ++recognizedCmds;
        } else if (verb == "create_pblock" || verb == "add_cells_to_pblock" || verb == "resize_pblock" ||
                   verb == "current_design" || verb == "set_Operating_conditions" || verb == "set") {
            ++recognizedCmds;
        }
    }

    if (recognizedCmds == 0) return false;

    // Filter pins that actually received a physical site assignment (`PACKAGE_PIN` / `LOC`)
    std::vector<FpgaPinCommon::FpgaPinEntry> assignedPins;
    for (auto& pe : pins) {
        if (!pe.site.empty()) {
            for (const auto& ck : tclClocks) {
                if (ck.targetPort == pe.signal || ck.name == pe.signal) pe.isClock = true;
            }
            assignedPins.push_back(std::move(pe));
        }
    }

    if (!assignedPins.empty()) {
        std::vector<FpgaPinCommon::FpgaClockSpec> clkSpecs;
        for (const auto& ck : tclClocks) {
            if (ck.periodNs > 0.0) clkSpecs.push_back({ ck.name, 1000.0 / ck.periodNs });
        }
        return FpgaPinCommon::EmitFpgaPinoutBoard(
            L"Xilinx Vivado Design Constraints (.xdc)",
            L"XILINX_VIVADO_PKG",
            assignedPins,
            clkSpecs,
            out);
    }

    // Timing-only `.xdc` file (clocks / I/O delays / CDC exceptions without PACKAGE_PINs)
    return TclTimingPin::RenderTimingDiagramBoard(
        L"Xilinx Vivado Timing Constraints (.xdc)",
        tclClocks,
        ioDelays,
        exceptions,
        miscTiming,
        out);
}

} // namespace Formats
