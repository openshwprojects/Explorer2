#pragma once
// RockwellPlcL5x.h — Rockwell Studio 5000 Logix Designer PLC Ladder Export (.l5x) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// Rockwell Studio 5000 Logix Designer PLC Ladder Export (.l5x)
inline bool ParseRockwellPlcL5xBoard(std::string_view sv, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    {
        if (sv.find("<RSLogix5000Content") == std::string_view::npos) return false;

        auto xmlAttr = [](std::string_view tag, std::string_view attr) -> std::string {
            std::string_view v;
            return GetXmlAttr(tag, attr, v) ? std::string(v) : std::string{};
        };

        std::string ctrlName, procType;
        {
            size_t cp = sv.find("<Controller ");
            if (cp != std::string_view::npos) {
                size_t gt = sv.find('>', cp);
                if (gt != std::string_view::npos) {
                    std::string_view ctag = sv.substr(cp + 1, gt - cp - 1);
                    ctrlName = xmlAttr(ctag, "Name");
                    procType = xmlAttr(ctag, "ProcessorType");
                }
            }
        }

        struct L5xTag { std::string name; std::string dataType; };
        std::vector<L5xTag> tags;
        {
            size_t tp = 0;
            while ((tp = sv.find("<Tag ", tp)) != std::string_view::npos && tags.size() < 64u) {
                size_t gt = sv.find('>', tp);
                if (gt == std::string_view::npos) break;
                std::string_view ttag = sv.substr(tp + 1, gt - tp - 1);
                std::string tnm = xmlAttr(ttag, "Name");
                std::string tdt = xmlAttr(ttag, "DataType");
                if (!tnm.empty()) {
                    tags.push_back({ std::move(tnm), tdt.empty() ? "BOOL" : std::move(tdt) });
                }
                tp = gt + 1;
            }
        }

        struct L5xInst {
            std::string op;
            std::string primaryArg;
            std::string extraArgs;
        };
        struct L5xRung {
            int number = 0;
            std::vector<std::vector<L5xInst>> branches; // branch[0] = main rung path, branch[1..] = parallel [a,b] paths
        };

        auto parseInstSeq = [](std::string_view seq) -> std::vector<L5xInst> {
            std::vector<L5xInst> res;
            size_t i = 0;
            while (i < seq.size() && res.size() < 10u) {
                while (i < seq.size() && !std::isalpha(static_cast<unsigned char>(seq[i]))) ++i;
                if (i >= seq.size()) break;
                size_t s = i;
                while (i < seq.size() && (std::isalnum(static_cast<unsigned char>(seq[i])) || seq[i] == '_')) ++i;
                std::string opUp(seq.substr(s, i - s));
                for (char& c : opUp) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

                while (i < seq.size() && seq[i] == ' ') ++i;
                if (i >= seq.size() || seq[i] != '(') continue;
                int pDepth = 0;
                size_t argStart = i + 1;
                size_t argEnd = argStart;
                for (; i < seq.size(); ++i) {
                    if (seq[i] == '(') ++pDepth;
                    else if (seq[i] == ')') {
                        if (--pDepth == 0) { argEnd = i++; break; }
                    }
                }
                std::string_view args = seq.substr(argStart, argEnd - argStart);
                size_t cm = args.find(',');
                L5xInst inst;
                inst.op = std::move(opUp);
                inst.primaryArg = std::string(cm == std::string_view::npos ? args : args.substr(0, cm));
                if (cm != std::string_view::npos) inst.extraArgs = std::string(args.substr(cm + 1));
                if (!inst.op.empty()) res.push_back(std::move(inst));
            }
            return res;
        };

        std::vector<L5xRung> rungs;
        {
            size_t rp = 0;
            while ((rp = sv.find("<Rung", rp)) != std::string_view::npos && rungs.size() < 24u) {
                size_t after = rp + 5;
                if (after < sv.size() && sv[after] != ' ' && sv[after] != '>') { rp = after; continue; }
                size_t rgt = sv.find('>', after);
                size_t rce = (rgt != std::string_view::npos) ? sv.find("</Rung>", rgt + 1) : std::string_view::npos;
                if (rce == std::string_view::npos) break;

                int rungNum = static_cast<int>(rungs.size());
                std::string rnAttr = xmlAttr(sv.substr(rp + 1, rgt - rp - 1), "Number");
                if (!rnAttr.empty()) rungNum = std::atoi(rnAttr.c_str());

                std::string_view rBody = sv.substr(rgt + 1, rce - rgt - 1);
                size_t tx0 = rBody.find("<Text>");
                size_t tx1 = (tx0 != std::string_view::npos) ? rBody.find("</Text>", tx0 + 6) : std::string_view::npos;
                if (tx0 != std::string_view::npos && tx1 != std::string_view::npos) {
                    std::string rtxt(rBody.substr(tx0 + 6, tx1 - tx0 - 6));
                    size_t cd = rtxt.find("[CDATA[");
                    if (cd != std::string::npos) {
                        rtxt.erase(0, cd + 7);
                        size_t ce = rtxt.rfind("]]>");
                        if (ce != std::string::npos) rtxt.resize(ce);
                    }

                    // Extract optional parallel branch [branchA,branchB] + series tail
                    L5xRung rg;
                    rg.number = rungNum;
                    size_t lb = rtxt.find('[');
                    size_t rb = (lb != std::string::npos) ? rtxt.rfind(']') : std::string::npos;
                    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
                        std::string prefix = rtxt.substr(0, lb);
                        std::string suffix = rtxt.substr(rb + 1);
                        std::string_view mid(rtxt.data() + lb + 1, rb - lb - 1);
                        int pDepth = 0;
                        size_t bs = 0;
                        for (size_t k = 0; k <= mid.size(); ++k) {
                            if (k < mid.size() && mid[k] == '(') ++pDepth;
                            else if (k < mid.size() && mid[k] == ')') --pDepth;
                            else if (pDepth == 0 && (k == mid.size() || mid[k] == ',')) {
                                std::string pathStr = prefix + std::string(mid.substr(bs, k - bs)) + (rg.branches.empty() ? suffix : "");
                                auto seq = parseInstSeq(pathStr);
                                if (!seq.empty()) rg.branches.push_back(std::move(seq));
                                bs = k + 1;
                            }
                        }
                    } else {
                        auto seq = parseInstSeq(rtxt);
                        if (!seq.empty()) rg.branches.push_back(std::move(seq));
                    }

                    if (!rg.branches.empty()) {
                        rungs.push_back(std::move(rg));
                    }
                }
                rp = rce + 7;
            }
        }

        // Zero-Cheat: NEVER synthesize fake rungs! Reject if zero rungs and zero controller tags.
        if (rungs.empty() && tags.empty()) {
            return false;
        }

        out.kind = EdaDocKind::KiCadSch;
        std::wstring ctrlHdr = ctrlName.empty() ? L"Logix5000" : PathUtils::Utf8ToWide(ctrlName);
        if (!procType.empty()) ctrlHdr += L" [" + PathUtils::Utf8ToWide(procType) + L"]";
        out.formatTitle = L"Rockwell Studio 5000 PLC Ladder — " + ctrlHdr +
                          L" (" + std::to_wstring(rungs.size()) + L" rungs, " + std::to_wstring(tags.size()) + L" tags)";

        const float leftRailX  = -44.0f;
        const float rightRailX =  44.0f;
        out.labels.push_back({ leftRailX, 14.0f, 1.05f, EdaLayerId::SchLabel, out.formatTitle });

        if (!rungs.empty()) {
            float curY = 6.0f;
            size_t compCounter = 0;

            for (size_t ri = 0; ri < rungs.size(); ++ri) {
                const auto& rg = rungs[ri];
                const float mainY = curY;

                wchar_t rLbl[32] = {};
                swprintf_s(rLbl, L"R%d", rg.number);
                out.labels.push_back({ leftRailX - 7.5f, mainY + 0.5f, 0.95f, EdaLayerId::SchLabel, rLbl });

                float branchJoinX = -10.0f;

                for (size_t bi = 0; bi < rg.branches.size(); ++bi) {
                    const auto& insts = rg.branches[bi];
                    const float ry = mainY - static_cast<float>(bi) * 9.5f;
                    float curX = (bi == 0u) ? leftRailX : (leftRailX + 5.0f);

                    if (bi > 0u) {
                        out.traces.push_back({ leftRailX + 5.0f, mainY, leftRailX + 5.0f, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                    }

                    size_t nonOutIdx = 0;
                    for (size_t k = 0; k < insts.size(); ++k) {
                        const auto& in = insts[k];
                        const bool isCoil = (in.op == "OTE" || in.op == "OTL" || in.op == "OTU");
                        const bool isContact = (in.op == "XIC" || in.op == "XIO" || in.op == "ONS");
                        const bool isOutBox = !isCoil && !isContact;

                        float symX = isCoil ? 33.0f
                                   : (isOutBox && k + 1 == insts.size()) ? 26.0f
                                   : (-28.0f + static_cast<float>(nonOutIdx++) * 15.5f);

                        if (isContact) {
                            out.traces.push_back({ curX, ry, symX - 1.6f, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                            out.traces.push_back({ symX - 1.6f, ry - 2.3f, symX - 1.6f, ry + 2.3f, 0.34f, EdaLayerId::SchSymbol, 0 });
                            out.traces.push_back({ symX + 1.6f, ry - 2.3f, symX + 1.6f, ry + 2.3f, 0.34f, EdaLayerId::SchSymbol, 0 });
                            if (in.op == "XIO") {
                                out.traces.push_back({ symX - 1.3f, ry - 2.0f, symX + 1.3f, ry + 2.0f, 0.26f, EdaLayerId::SchSymbol, 0 });
                            } else if (in.op == "ONS") {
                                out.traces.push_back({ symX, ry - 1.6f, symX, ry + 1.6f, 0.24f, EdaLayerId::SchSymbol, 0 });
                            }
                            curX = symX + 1.6f;
                            branchJoinX = std::max(branchJoinX, curX + 3.0f);
                        } else if (isCoil) {
                            out.traces.push_back({ curX, ry, symX - 2.3f, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                            out.circles.push_back({ symX, ry, 2.3f, 0.30f, false, EdaLayerId::SchSymbol });
                            if (in.op == "OTL" || in.op == "OTU") {
                                out.labels.push_back({ symX - 0.8f, ry + 0.5f, 0.85f, EdaLayerId::SchSymbol,
                                                       (in.op == "OTL") ? L"L" : L"U" });
                            }
                            curX = symX + 2.3f;
                        } else {
                            // Function block instruction box (TON, CTU, MOV, EQU, ...)
                            float bw = 11.5f, bh = 5.6f;
                            out.traces.push_back({ curX, ry, symX - bw * 0.5f, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                            drawSchRect(symX, ry, bw, bh, 0.30f, EdaLayerId::SchSymbol, 0);
                            out.labels.push_back({ symX - bw * 0.42f, ry + 0.8f, 0.85f, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(in.op) });
                            if (!in.extraArgs.empty()) {
                                std::string ex = in.extraArgs.substr(0, std::min<size_t>(in.extraArgs.size(), 14u));
                                out.labels.push_back({ symX - bw * 0.42f, ry - 1.3f, 0.75f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(ex) });
                            }
                            curX = symX + bw * 0.5f;
                            branchJoinX = std::max(branchJoinX, curX + 2.5f);
                        }

                        out.labels.push_back({ symX - 4.2f, ry + 3.8f, 0.90f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(in.primaryArg) });

                        if (compCounter < 128u) {
                            EdaComponent3D ic;
                            ic.refDes = PathUtils::Utf8ToWide(in.primaryArg.empty() ? in.op : in.primaryArg);
                            ic.value = PathUtils::Utf8ToWide(in.op);
                            ic.footprint = L"L5X_" + PathUtils::Utf8ToWide(in.op);
                            ic.x = symX; ic.y = ry;
                            ic.widthMm = isOutBox ? 11.5f : 4.5f;
                            ic.lengthMm = 5.0f;
                            out.components.push_back(std::move(ic));
                            ++compCounter;
                        }
                    }

                    if (bi == 0u) {
                        out.traces.push_back({ curX, ry, rightRailX, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                    } else {
                        float jx = std::min(branchJoinX, 20.0f);
                        out.traces.push_back({ curX, ry, jx, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                        out.traces.push_back({ jx, ry, jx, mainY, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                    }
                }

                curY -= static_cast<float>(rg.branches.size()) * 11.0f + 2.5f;
            }

            // Left (+24V / L1) and Right (0V / L2) PLC Ladder Power Rails
            out.traces.push_back({ leftRailX,  11.0f, leftRailX,  curY + 5.0f, 0.55f, EdaLayerId::SchWire, 1 });
            out.traces.push_back({ rightRailX, 11.0f, rightRailX, curY + 5.0f, 0.55f, EdaLayerId::SchWire, 2 });
        } else {
            // Tag-only Controller Data Block schematic
            const size_t nShow = std::min<size_t>(tags.size(), 16u);
            const float bh = static_cast<float>(nShow) * 3.4f + 6.0f;
            drawSchRect(0.0f, -bh * 0.5f + 6.0f, 38.0f, bh, 0.36f, EdaLayerId::SchSymbol, 0);
            for (size_t ti = 0; ti < nShow; ++ti) {
                float ty = 3.0f - static_cast<float>(ti) * 3.2f;
                out.traces.push_back({ -23.0f, ty, -19.0f, ty, 0.24f, EdaLayerId::SchWire, static_cast<int>(ti + 1) });
                out.pads.push_back({ -23.0f, ty, 0.55f, 0.55f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"TAG", L"" });
                out.labels.push_back({ -17.8f, ty + 0.5f, 0.88f, EdaLayerId::SchLabel,
                                       PathUtils::Utf8ToWide(tags[ti].name + " : " + tags[ti].dataType) });

                EdaComponent3D tc;
                tc.refDes = PathUtils::Utf8ToWide(tags[ti].name);
                tc.value = PathUtils::Utf8ToWide(tags[ti].dataType);
                tc.footprint = L"L5X_TAG";
                tc.x = 0.0f; tc.y = ty;
                tc.widthMm = 36.0f; tc.lengthMm = 2.8f;
                out.components.push_back(std::move(tc));
            }
        }

        out.netCount = std::max<size_t>(1u, rungs.size() + tags.size());
        return true;
    }
}

} // namespace Formats
