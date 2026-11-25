#pragma once
// Ieee1801Upf.h — IEEE Std 1801 Unified Power Format (.upf) Low-Power SoC Intent Reader
// Parses Tcl-structured IEEE 1801 power intent commands (with `\` continuations & `{...}` lists):
//   create_power_domain, create_supply_port, create_supply_net, connect_supply_net,
//   create_supply_set, associate_supply_set, add_port_state, create_power_switch,
//   set_isolation, set_retention, set_level_shifter, create_pst, and add_pst_state,
// rendering SoC multi-voltage power islands, supply rails, MTCMOS power switches, ISO/LS/RET
// boundary cells, and the Power State Table (PST) matrix.
// Included inside namespace Explorer2 in EdaReadersVlsiWave.cpp (do NOT put #include <...> here).

namespace Formats {

namespace DetailIeeeUpf {

// Splits an entire UPF file into Tcl statements, handling `#` comments, `\` line continuations,
// `{...}` balanced brace blocks, `"..."` strings, and `;` command separators.
inline bool SplitTclCommands(std::string_view sv, std::vector<std::vector<std::string>>& outCmds) {
    outCmds.clear();
    size_t i = 0;
    const size_t n = sv.size();

    std::vector<std::string> curWords;
    std::string curTok;
    bool inQuote = false;
    int braceDepth = 0;

    auto flushWord = [&]() {
        if (!curTok.empty()) {
            curWords.push_back(std::move(curTok));
            curTok.clear();
        }
    };
    auto flushCmd = [&]() {
        flushWord();
        if (!curWords.empty()) {
            outCmds.push_back(std::move(curWords));
            curWords.clear();
        }
    };

    while (i < n) {
        const char c = sv[i];

        if (!inQuote && braceDepth == 0) {
            // Backslash-newline continuation
            if (c == '\\') {
                if (i + 1 < n && sv[i + 1] == '\n') { i += 2; continue; }
                if (i + 2 < n && sv[i + 1] == '\r' && sv[i + 2] == '\n') { i += 3; continue; }
            }
            if (c == '#' && curTok.empty() && (curWords.empty())) {
                while (i < n && sv[i] != '\n') ++i;
                continue;
            }
            if (c == '\n' || c == '\r' || c == ';') {
                flushCmd();
                ++i;
                continue;
            }
            if (c == ' ' || c == '\t') {
                flushWord();
                ++i;
                continue;
            }
            if (c == '"') {
                inQuote = true;
                ++i;
                continue;
            }
            if (c == '{') {
                braceDepth = 1;
                ++i;
                continue;
            }
            if (c == '}') {
                return false; // Unmatched closing brace!
            }
            curTok.push_back(c);
            ++i;
        } else if (inQuote) {
            if (c == '\\' && i + 1 < n) {
                curTok.push_back(sv[i + 1]);
                i += 2;
                continue;
            }
            if (c == '"') {
                inQuote = false;
                if (curTok.empty()) curWords.emplace_back("");
                else flushWord();
                ++i;
                continue;
            }
            curTok.push_back(c);
            ++i;
        } else {
            // Inside `{ ... }`
            if (c == '{') {
                ++braceDepth;
                curTok.push_back(c);
                ++i;
            } else if (c == '}') {
                --braceDepth;
                if (braceDepth == 0) {
                    if (curTok.empty()) curWords.emplace_back("");
                    else flushWord();
                } else {
                    curTok.push_back(c);
                }
                ++i;
            } else {
                curTok.push_back(c);
                ++i;
            }
        }
    }
    if (braceDepth != 0 || inQuote) return false;
    flushCmd();
    return true;
}

inline void SplitWsWords(std::string_view s, std::vector<std::string>& out) {
    out.clear();
    size_t p = 0;
    while (p < s.size()) {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
        if (p >= s.size()) break;
        const size_t st = p;
        while (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\r' && s[p] != '\n') ++p;
        out.emplace_back(s.substr(st, p - st));
    }
}

inline std::string FindFlagArg(const std::vector<std::string>& cmd, std::string_view flag) {
    for (size_t i = 1; i + 1 < cmd.size(); ++i) {
        if (ReaderText::AsciiEqCI(cmd[i], flag)) return cmd[i + 1];
    }
    return {};
}

struct PowerDomain {
    std::string name;
    bool isTopScope = false;
    std::vector<std::string> elements;
    std::string primaryPower;
    std::string primaryGround = "VSS";
    std::vector<std::string> isoRules;
    std::vector<std::string> lsRules;
    std::vector<std::string> retRules;
};

struct PowerSwitch {
    std::string name;
    std::string domain;
    std::string inSupply;
    std::string outSupply;
    std::string ctrlSignal;
};

struct PstRow {
    std::string stateName;
    std::vector<std::string> railStates;
};

} // namespace DetailIeeeUpf

inline bool ParseIeee1801Upf(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 24 || size > 32u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    std::vector<std::vector<std::string>> cmds;
    if (!DetailIeeeUpf::SplitTclCommands(sv, cmds) || cmds.empty()) return false;

    std::string designTop = "soc_top";
    std::vector<DetailIeeeUpf::PowerDomain> domains;
    std::unordered_map<std::string, size_t> domIdx;
    std::vector<std::string> supplyNets;
    std::unordered_map<std::string, size_t> supplyNetIdx;
    std::unordered_map<std::string, std::string> supplySetPower;
    std::vector<DetailIeeeUpf::PowerSwitch> switches;
    std::vector<std::string> pstSupplies;
    std::vector<DetailIeeeUpf::PstRow> pstRows;
    size_t policyCount = 0;

    auto ensureSupplyNet = [&](const std::string& net) {
        if (net.empty()) return;
        if (supplyNetIdx.find(net) == supplyNetIdx.end() && supplyNets.size() < 32) {
            supplyNetIdx[net] = supplyNets.size();
            supplyNets.push_back(net);
        }
    };

    for (const auto& c : cmds) {
        if (c.empty()) continue;
        const std::string& kw = c[0];

        if (ReaderText::AsciiEqCI(kw, "set_design_top") || ReaderText::AsciiEqCI(kw, "set_scope")) {
            if (c.size() >= 2 && c[1] != "." && c[1] != "/") designTop = c[1];
        } else if (ReaderText::AsciiEqCI(kw, "create_power_domain")) {
            if (c.size() < 2 || c[1].empty() || c[1].front() == '-') return false;
            DetailIeeeUpf::PowerDomain pd;
            pd.name = c[1];
            for (size_t i = 2; i < c.size(); ++i) {
                if (ReaderText::AsciiEqCI(c[i], "-include_scope")) pd.isTopScope = true;
                else if (ReaderText::AsciiEqCI(c[i], "-elements") && i + 1 < c.size()) {
                    DetailIeeeUpf::SplitWsWords(c[++i], pd.elements);
                } else if (ReaderText::AsciiEqCI(c[i], "-supply") && i + 1 < c.size()) {
                    std::vector<std::string> sp;
                    DetailIeeeUpf::SplitWsWords(c[++i], sp);
                    if (sp.size() >= 2 && ReaderText::AsciiEqCI(sp[0], "primary")) {
                        auto sit = supplySetPower.find(sp[1]);
                        pd.primaryPower = (sit != supplySetPower.end()) ? sit->second : sp[1];
                        ensureSupplyNet(pd.primaryPower);
                    }
                }
            }
            if (domIdx.find(pd.name) == domIdx.end() && domains.size() < 32) {
                domIdx[pd.name] = domains.size();
                domains.push_back(std::move(pd));
            }
        } else if (ReaderText::AsciiEqCI(kw, "create_supply_net") || ReaderText::AsciiEqCI(kw, "create_supply_port")) {
            if (c.size() < 2 || c[1].empty() || c[1].front() == '-') return false;
            ensureSupplyNet(c[1]);
            ++policyCount;
        } else if (ReaderText::AsciiEqCI(kw, "create_supply_set")) {
            if (c.size() < 2 || c[1].empty()) return false;
            ++policyCount;
            const std::string setName = c[1];
            for (size_t i = 2; i + 1 < c.size(); ++i) {
                if (ReaderText::AsciiEqCI(c[i], "-function")) {
                    std::vector<std::string> fn;
                    DetailIeeeUpf::SplitWsWords(c[i + 1], fn);
                    if (fn.size() >= 2 && ReaderText::AsciiEqCI(fn[0], "power")) {
                        supplySetPower[setName] = fn[1];
                        ensureSupplyNet(fn[1]);
                    }
                }
            }
        } else if (ReaderText::AsciiEqCI(kw, "set_domain_supply_net")) {
            if (c.size() >= 2) {
                ++policyCount;
                auto dit = domIdx.find(c[1]);
                const std::string pwr = DetailIeeeUpf::FindFlagArg(c, "-primary_power_net");
                const std::string gnd = DetailIeeeUpf::FindFlagArg(c, "-primary_ground_net");
                ensureSupplyNet(pwr);
                ensureSupplyNet(gnd);
                if (dit != domIdx.end()) {
                    if (!pwr.empty()) domains[dit->second].primaryPower = pwr;
                    if (!gnd.empty()) domains[dit->second].primaryGround = gnd;
                }
            }
        } else if (ReaderText::AsciiEqCI(kw, "associate_supply_set")) {
            if (c.size() >= 2) {
                ++policyCount;
                const std::string hdl = DetailIeeeUpf::FindFlagArg(c, "-handle");
                const size_t dot = hdl.find('.');
                const std::string dName = (dot == std::string::npos) ? hdl : hdl.substr(0, dot);
                auto dit = domIdx.find(dName);
                if (dit != domIdx.end()) {
                    auto sit = supplySetPower.find(c[1]);
                    const std::string pwr = (sit != supplySetPower.end()) ? sit->second : c[1];
                    domains[dit->second].primaryPower = pwr;
                    ensureSupplyNet(pwr);
                }
            }
        } else if (ReaderText::AsciiEqCI(kw, "create_power_switch")) {
            if (c.size() < 2 || c[1].empty()) return false;
            ++policyCount;
            DetailIeeeUpf::PowerSwitch sw;
            sw.name = c[1];
            sw.domain = DetailIeeeUpf::FindFlagArg(c, "-domain");
            std::vector<std::string> inW, outW, ctrlW;
            DetailIeeeUpf::SplitWsWords(DetailIeeeUpf::FindFlagArg(c, "-input_supply_port"), inW);
            DetailIeeeUpf::SplitWsWords(DetailIeeeUpf::FindFlagArg(c, "-output_supply_port"), outW);
            DetailIeeeUpf::SplitWsWords(DetailIeeeUpf::FindFlagArg(c, "-control_port"), ctrlW);
            if (!inW.empty())   { sw.inSupply   = inW.back();   ensureSupplyNet(sw.inSupply); }
            if (!outW.empty())  { sw.outSupply  = outW.back();  ensureSupplyNet(sw.outSupply); }
            if (!ctrlW.empty()) { sw.ctrlSignal = ctrlW.back(); }
            if (!sw.outSupply.empty()) {
                auto dit = domIdx.find(sw.domain);
                if (dit != domIdx.end() && domains[dit->second].primaryPower.empty()) {
                    domains[dit->second].primaryPower = sw.outSupply;
                }
            }
            switches.push_back(std::move(sw));
        } else if (ReaderText::AsciiEqCI(kw, "set_isolation")) {
            if (c.size() >= 2) {
                ++policyCount;
                auto dit = domIdx.find(DetailIeeeUpf::FindFlagArg(c, "-domain"));
                if (dit != domIdx.end()) domains[dit->second].isoRules.push_back(c[1]);
            }
        } else if (ReaderText::AsciiEqCI(kw, "set_level_shifter")) {
            if (c.size() >= 2) {
                ++policyCount;
                auto dit = domIdx.find(DetailIeeeUpf::FindFlagArg(c, "-domain"));
                if (dit != domIdx.end()) domains[dit->second].lsRules.push_back(c[1]);
            }
        } else if (ReaderText::AsciiEqCI(kw, "set_retention")) {
            if (c.size() >= 2) {
                ++policyCount;
                auto dit = domIdx.find(DetailIeeeUpf::FindFlagArg(c, "-domain"));
                if (dit != domIdx.end()) domains[dit->second].retRules.push_back(c[1]);
            }
        } else if (ReaderText::AsciiEqCI(kw, "create_pst")) {
            ++policyCount;
            DetailIeeeUpf::SplitWsWords(DetailIeeeUpf::FindFlagArg(c, "-supplies"), pstSupplies);
            for (const auto& s : pstSupplies) ensureSupplyNet(s);
        } else if (ReaderText::AsciiEqCI(kw, "add_pst_state")) {
            if (c.size() >= 2) {
                ++policyCount;
                DetailIeeeUpf::PstRow row;
                row.stateName = c[1];
                DetailIeeeUpf::SplitWsWords(DetailIeeeUpf::FindFlagArg(c, "-state"), row.railStates);
                if (pstRows.size() < 24) pstRows.push_back(std::move(row));
            }
        }
    }

    if (domains.empty() || policyCount == 0) return false;
    if (supplyNets.empty()) ensureSupplyNet("VDD");

    out.kind = EdaDocKind::KiCadSch;
    const float spanW = std::max(78.0f, static_cast<float>(domains.size()) * 28.0f);

    // 1. Horizontal SoC Supply Distribution Rails at top
    std::unordered_map<std::string, float> railY;
    for (size_t ri = 0; ri < supplyNets.size(); ++ri) {
        const float y = 30.0f + static_cast<float>(supplyNets.size() - 1u - ri) * 2.6f;
        railY[supplyNets[ri]] = y;
        const bool isGnd = (ReaderText::ToUpperAscii(supplyNets[ri]).find("VSS") != std::string::npos ||
                            ReaderText::ToUpperAscii(supplyNets[ri]).find("GND") != std::string::npos);
        const EdaLayerId rLayer = isGnd ? EdaLayerId::BottomCopper
                                : ((ri % 2u == 0) ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper);
        out.traces.push_back({ 0.0f, y, spanW, y, 0.36f, rLayer, static_cast<int>(ri + 1) });
        out.labels.push_back({ -14.0f, y, 0.90f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(supplyNets[ri]) });
    }

    // 2. Voltage Islands (Power Domains) + MTCMOS Power Switches + ISO / LS / RET Boundary Cells
    for (size_t di = 0; di < domains.size(); ++di) {
        const auto& pd = domains[di];
        const float x0 = 2.0f + static_cast<float>(di) * 28.0f;
        const float x1 = x0 + 24.0f;
        const float cx = (x0 + x1) * 0.5f;
        const float yBot = 2.0f, yTop = 22.0f;

        DrawEdaBoxRect(out, x0, yBot, x1, yTop, 0.32f, EdaLayerId::EdgeCuts, static_cast<int>(di + 1));
        out.labels.push_back({ x0 + 1.2f, yTop - 2.0f, 1.05f, EdaLayerId::TopSilk,
                               PathUtils::Utf8ToWide(pd.name) + (pd.isTopScope ? L" [TOP/AON]" : L" [ISLAND]") });

        EdaComponent3D domComp;
        domComp.refDes = PathUtils::Utf8ToWide(pd.name);
        domComp.value = PathUtils::Utf8ToWide(pd.primaryPower.empty() ? "VDD" : pd.primaryPower);
        domComp.footprint = L"POWER_DOMAIN";
        domComp.x = cx; domComp.y = (yBot + yTop) * 0.5f;
        domComp.widthMm = 24.0f; domComp.lengthMm = 20.0f; domComp.heightMm = 1.2f;
        domComp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(domComp));

        // Connect primary supply tap (or MTCMOS power switch if switched domain!)
        bool hasSwitch = false;
        for (const auto& sw : switches) {
            if (sw.domain == pd.name) {
                hasSwitch = true;
                const float swY = 25.6f;
                DrawEdaBoxRect(out, cx - 4.8f, swY - 1.8f, cx + 4.8f, swY + 1.8f, 0.24f, EdaLayerId::SchSymbol, static_cast<int>(di + 1));
                out.labels.push_back({ cx - 4.2f, swY + 0.1f, 0.72f, EdaLayerId::SchLabel, L"PSW:" + PathUtils::Utf8ToWide(sw.name) });
                auto inIt = railY.find(sw.inSupply);
                if (inIt != railY.end()) {
                    out.traces.push_back({ cx, inIt->second, cx, swY + 1.8f, 0.28f, EdaLayerId::TopCopper, static_cast<int>(di + 1) });
                    out.vias.push_back({ cx, inIt->second, 0.65f, 0.0f, static_cast<int>(di + 1) });
                }
                out.traces.push_back({ cx, swY - 1.8f, cx, yTop, 0.28f, EdaLayerId::InnerCopper, static_cast<int>(di + 1) });

                EdaComponent3D swComp;
                swComp.refDes = PathUtils::Utf8ToWide(sw.name);
                swComp.value = L"MTCMOS_HDR";
                swComp.footprint = L"POWER_SWITCH";
                swComp.x = cx; swComp.y = swY;
                swComp.widthMm = 9.6f; swComp.lengthMm = 3.6f; swComp.heightMm = 1.0f;
                swComp.category = EdaPartCategory::TransistorSOT;
                out.components.push_back(std::move(swComp));
                break;
            }
        }
        if (!hasSwitch && !pd.primaryPower.empty()) {
            auto pit = railY.find(pd.primaryPower);
            if (pit != railY.end()) {
                out.traces.push_back({ cx, pit->second, cx, yTop, 0.28f, EdaLayerId::TopCopper, static_cast<int>(di + 1) });
                out.vias.push_back({ cx, pit->second, 0.65f, 0.0f, static_cast<int>(di + 1) });
            }
        }

        // Sub-block instances inside domain
        for (size_t ei = 0; ei < std::min<size_t>(pd.elements.size(), 3u); ++ei) {
            const float ey = yTop - 5.5f - static_cast<float>(ei) * 4.2f;
            DrawEdaBoxRect(out, x0 + 2.0f, ey - 1.4f, x1 - 2.0f, ey + 1.4f, 0.20f, EdaLayerId::SchSymbol, 0);
            out.labels.push_back({ x0 + 2.8f, ey, 0.78f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(pd.elements[ei]) });
        }

        // ISO / LS / RET boundary badges along bottom of island
        float bx = x0 + 1.8f;
        auto drawBadge = [&](const wchar_t* tag, EdaLayerId lyr) {
            DrawEdaBoxRect(out, bx, yBot + 0.8f, bx + 6.4f, yBot + 3.2f, 0.20f, lyr, 0);
            out.labels.push_back({ bx + 0.6f, yBot + 2.0f, 0.68f, lyr, tag });
            bx += 7.2f;
        };
        if (!pd.isoRules.empty()) drawBadge(L"ISO", EdaLayerId::TopCopper);
        if (!pd.lsRules.empty())  drawBadge(L"LVL_SH", EdaLayerId::InnerCopper);
        if (!pd.retRules.empty()) drawBadge(L"RET_FF", EdaLayerId::TopSilk);
    }

    // 3. Power State Table (PST) Matrix below islands
    if (!pstRows.empty()) {
        const float pstTop = -2.5f;
        out.labels.push_back({ 2.0f, pstTop + 1.4f, 0.95f, EdaLayerId::TopSilk, L"Power State Table (PST)" });
        for (size_t r = 0; r < pstRows.size(); ++r) {
            const float ry = pstTop - 2.6f - static_cast<float>(r) * 3.0f;
            out.labels.push_back({ 2.0f, ry, 0.82f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(pstRows[r].stateName) });
            for (size_t c = 0; c < pstRows[r].railStates.size(); ++c) {
                const float cx = 18.0f + static_cast<float>(c) * 14.0f;
                const bool isOff = ReaderText::AsciiEqCI(pstRows[r].railStates[c], "OFF");
                DrawEdaBoxRect(out, cx, ry - 1.1f, cx + 12.5f, ry + 1.1f, 0.18f,
                               isOff ? EdaLayerId::BottomSilk : EdaLayerId::TopCopper, 0);
                out.labels.push_back({ cx + 1.0f, ry, 0.72f,
                                       isOff ? EdaLayerId::BottomSilk : EdaLayerId::TopCopper,
                                       PathUtils::Utf8ToWide(pstRows[r].railStates[c]) });
            }
        }
    }

    out.netCount = supplyNets.size() + switches.size();
    wchar_t title[200] = {};
    swprintf_s(title, L"IEEE 1801 UPF [%hs] (%zu domains, %zu supplies, %zu switches, %zu PST states)",
        designTop.c_str(), domains.size(), supplyNets.size(), switches.size(), pstRows.size());
    out.formatTitle = title;
    return !out.traces.empty();
}

} // namespace Formats
