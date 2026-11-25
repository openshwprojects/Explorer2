#pragma once
// QuartusQsf.h — Intel / Altera Quartus Settings & Pin Assignment File (.qsf) Reader
// Uses shared TclTimingPinCommon.h (supporting `\` line continuations, `PIN_A12` BGA balls AND `PIN_84`
// numeric QFP/TQFP pins) and FpgaPinConstraintsCommon.h.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseQuartusQsfBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& ext,
    ParsedEdaBoard& out)
{
    if (ext != L".qsf" || !data || size < 12 || size > (16u << 20)) return false;

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    std::vector<std::string> cmds;
    if (!TclTimingPin::ExtractLogicalTclCommands(sv, cmds) || cmds.empty()) return false;

    std::string familyName, devicePart, topEntity;
    std::vector<FpgaPinCommon::FpgaPinEntry> pins;
    std::unordered_map<std::string, size_t> pinBySignal;
    size_t validQsfCmds = 0;

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
        if (verb == "set_location_assignment") {
            // `set_location_assignment PIN_A12 -to clk` OR `set_location_assignment PIN_84 -to led[0]`
            std::string toSig;
            if (!TclTimingPin::FindFlagValue(toks, "-to", toSig) || toSig.empty()) continue;
            std::string rawLoc;
            for (size_t i = 1; i < toks.size(); ++i) {
                const std::string fl = TclTimingPin::LowerAscii(toks[i]);
                if (fl == "-to" || fl == "-comment" || fl == "-tag") { ++i; continue; }
                if (!fl.empty() && fl[0] == '-') continue;
                rawLoc = toks[i];
                break;
            }
            if (rawLoc.empty()) continue;
            ++validQsfCmds;

            std::string normSite;
            if (TclTimingPin::NormalizePackagePinSite(rawLoc, normSite)) {
                ensurePin(toSig).site = normSite;
            } else if (ReaderText::StartsWithCI(rawLoc, "IOBANK_") || ReaderText::StartsWithCI(rawLoc, "PIN_")) {
                ensurePin(toSig).site = rawLoc;
            }
        } else if (verb == "set_instance_assignment") {
            ++validQsfCmds;
            std::string nameVal, toSig;
            if (TclTimingPin::FindFlagValue(toks, "-name", nameVal) &&
                TclTimingPin::FindFlagValue(toks, "-to", toSig) && !toSig.empty())
            {
                const std::string upName = TclTimingPin::UpperAscii(nameVal);
                // Value is the non-flag token right after `-name <NAME>`
                std::string instVal;
                for (size_t i = 1; i + 2 < toks.size(); ++i) {
                    if (TclTimingPin::LowerAscii(toks[i]) == "-name") {
                        instVal = toks[i + 2];
                        break;
                    }
                }
                if (upName == "IO_STANDARD" && !instVal.empty() && instVal[0] != '-') {
                    ensurePin(toSig).ioStd = instVal;
                } else if (upName == "WEAK_PULL_UP_RESISTOR" && ReaderText::AsciiEqCI(instVal, "ON")) {
                    ensurePin(toSig).hasPullup = true;
                }
            }
        } else if (verb == "set_global_assignment") {
            ++validQsfCmds;
            std::string nameVal;
            if (TclTimingPin::FindFlagValue(toks, "-name", nameVal)) {
                const std::string upName = TclTimingPin::UpperAscii(nameVal);
                std::string gVal;
                for (size_t i = 1; i + 2 < toks.size(); ++i) {
                    if (TclTimingPin::LowerAscii(toks[i]) == "-name") {
                        gVal = toks[i + 2];
                        break;
                    }
                }
                if (upName == "DEVICE" && !gVal.empty()) devicePart = gVal;
                else if (upName == "FAMILY" && !gVal.empty()) familyName = gVal;
                else if (upName == "TOP_LEVEL_ENTITY" && !gVal.empty()) topEntity = gVal;
            }
        } else if (verb == "source" || verb == "set_parameter" || verb == "export_assignments") {
            ++validQsfCmds;
        }
    }

    if (validQsfCmds == 0) return false;

    std::vector<FpgaPinCommon::FpgaPinEntry> assignedPins;
    for (auto& pe : pins) {
        if (!pe.site.empty()) assignedPins.push_back(std::move(pe));
    }

    const std::string devStr = !devicePart.empty() ? devicePart
                             : (!familyName.empty() ? familyName : topEntity);

    if (!assignedPins.empty()) {
        return FpgaPinCommon::EmitFpgaPinoutBoard(
            L"Intel Quartus Settings & Pinout (.qsf)",
            PathUtils::Utf8ToWide(devStr.empty() ? "ALTERA_FPGA" : devStr),
            assignedPins,
            {},
            out);
    }

    // Valid project-only `.qsf` with `DEVICE`/`FAMILY`/`TOP_LEVEL_ENTITY` but 0 location assignments
    if (!devicePart.empty() || !familyName.empty() || !topEntity.empty()) {
        out.kind = EdaDocKind::KiCadSch;
        DrawEdaBoxRect(out, -24.0f, -10.0f, 24.0f, 10.0f, 0.32f, EdaLayerId::EdgeCuts, 0);
        const std::wstring wDev = PathUtils::Utf8ToWide(devStr);
        out.labels.push_back({ -21.0f, 3.0f, 1.25f, EdaLayerId::TopSilk, L"Quartus Project: " + wDev });
        if (!topEntity.empty()) {
            out.labels.push_back({ -21.0f, -2.0f, 0.95f, EdaLayerId::SchLabel, L"Top Entity: " + PathUtils::Utf8ToWide(topEntity) });
        }
        EdaComponent3D comp;
        comp.refDes = L"U1";
        comp.value = wDev;
        comp.footprint = L"QUARTUS_QSF";
        comp.widthMm = 48.0f; comp.lengthMm = 20.0f; comp.heightMm = 1.6f;
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));
        out.formatTitle = L"Intel Quartus Project Settings (.qsf — " + wDev + L")";
        out.netCount = 1;
        return true;
    }

    return false;
}

} // namespace Formats
