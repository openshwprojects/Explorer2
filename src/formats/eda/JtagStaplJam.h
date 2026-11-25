#pragma once
// JtagStaplJam.h — JEDEC JESD-71 STAPL / Intel-Altera JAM JTAG Programming & Scan File (.jam) Reader
// Parses `NOTE`, `ACTION` step lists, `PROCEDURE ... ENDPROC` (`IRSCAN`, `DRSCAN`, `STATE`, `WAIT`, `CALL`),
// and `CRC`, wiring actual `ACTION -> PROCEDURE` call edges AND decoding `IRSCAN` / `DRSCAN` bit lengths &
// `$hex` vectors to plot real 4-lane JTAG `TCK` / `TMS` / `TDI` / `TDO` scan waveforms!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailJtagJam {

struct JamScanOp {
    bool isIr = false;
    uint32_t bitLen = 0;
    std::vector<uint8_t> tdiBytes;
    std::vector<uint8_t> tdoBytes;
    std::vector<uint8_t> maskBytes;
};

struct JamProc {
    std::string name;
    size_t irScans = 0u;
    size_t drScans = 0u;
    size_t waits   = 0u;
    uint64_t totalScanBits = 0ULL;
    std::vector<std::string> calledProcs;
    std::vector<JamScanOp> scans;
};

struct JamAction {
    std::string name;
    std::vector<std::string> steps;
};

inline bool NextJamStmt(std::string_view sv, size_t& pos, std::string& outStmt) {
    outStmt.clear();
    const size_t n = sv.size();
    bool inQuote = false;
    while (pos < n) {
        const char c = sv[pos++];
        if (!inQuote && c == '\'') {
            while (pos < n && sv[pos] != '\n') ++pos;
            continue;
        }
        if (c == '"') {
            inQuote = !inQuote;
            outStmt.push_back(c);
            continue;
        }
        if (!inQuote && c == ';') {
            return true;
        }
        outStmt.push_back((static_cast<unsigned char>(c) <= 0x20u && !inQuote) ? ' ' : c);
    }
    if (inQuote) return false;
    std::string_view rem = ReaderText::TrimSv(outStmt);
    if (!rem.empty()) return false;
    return false;
}

// Parse STAPL `$hex` literal (e.g. `$A5F0` or `$00112233`) into big-endian bytes matching `JtagTapTracker::ShiftBytesBE`
inline void ParseStaplDollarHex(std::string_view expr, uint32_t bitLen, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    const size_t dlr = expr.find('$');
    if (dlr == std::string_view::npos) return;
    std::string hexDigits;
    for (size_t i = dlr + 1u; i < expr.size(); ++i) {
        const char c = expr[i];
        if (ReaderText::HexVal(c) >= 0) hexDigits.push_back(c);
        else if (c == '_' || c == ' ') continue;
        else break;
    }
    if (hexDigits.empty()) return;
    const size_t needBytes = std::max<size_t>(1u, (static_cast<size_t>(bitLen) + 7u) / 8u);
    const size_t needNibbles = needBytes * 2u;
    if (hexDigits.size() < needNibbles) {
        hexDigits.insert(0u, needNibbles - hexDigits.size(), '0');
    } else if (hexDigits.size() > needNibbles) {
        hexDigits = hexDigits.substr(hexDigits.size() - needNibbles);
    }
    outBytes.resize(needBytes, 0u);
    for (size_t b = 0; b < needBytes; ++b) {
        const int hi = ReaderText::HexVal(hexDigits[b * 2u]);
        const int lo = ReaderText::HexVal(hexDigits[b * 2u + 1u]);
        outBytes[b] = static_cast<uint8_t>((hi << 4) | lo);
    }
}

} // namespace DetailJtagJam

inline bool ParseJtagStaplJam(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    using namespace DetailJtagJam;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 20u, (32u << 20))) return false;

    std::string deviceName;
    std::string algVersion;
    std::string crcHex;
    std::vector<JamAction> actions;
    std::vector<JamProc>   procs;
    std::unordered_map<std::string, uint64_t> intConsts;
    bool inProc = false;

    size_t pos = 0u;
    std::string rawStmt;
    while (NextJamStmt(sv, pos, rawStmt)) {
        std::string_view s = ReaderText::TrimSv(rawStmt);
        if (s.empty()) continue;

        std::string_view kw;
        if (!ReaderText::NextTokenSv(s, kw)) continue;
        const std::string up = ReaderText::ToUpperAscii(kw);

        if (up == "NOTE") {
            const size_t q1 = s.find('"');
            const size_t q2 = (q1 == std::string_view::npos) ? std::string_view::npos : s.find('"', q1 + 1u);
            const size_t q3 = (q2 == std::string_view::npos) ? std::string_view::npos : s.find('"', q2 + 1u);
            const size_t q4 = (q3 == std::string_view::npos) ? std::string_view::npos : s.find('"', q3 + 1u);
            if (q4 != std::string_view::npos) {
                std::string k = ReaderText::ToUpperAscii(s.substr(q1 + 1u, q2 - q1 - 1u));
                std::string v(s.substr(q3 + 1u, q4 - q3 - 1u));
                if (k == "DEVICE" || k == "TARGET" || k == "PART") deviceName = v;
                else if (k == "ALG_VERSION" || k == "STAPL_VERSION") algVersion = v;
            }
        } else if (up == "CRC") {
            std::string_view cTok;
            if (!ReaderText::NextTokenSv(s, cTok) || cTok.empty()) return false;
            crcHex.assign(cTok.data(), cTok.size());
            break;
        } else if (up == "INTEGER" && !inProc) {
            // Track scalar `INTEGER len = 10;` constants used in `IRSCAN len, $...;`
            const size_t eq = s.find('=');
            if (eq != std::string_view::npos) {
                std::string_view nm = ReaderText::TrimSv(s.substr(0u, eq));
                std::string_view vl = ReaderText::TrimSv(s.substr(eq + 1u));
                uint64_t iv = 0;
                if (!nm.empty() && ReaderText::ParseUint64Sv(vl, iv, 10)) {
                    intConsts[ReaderText::ToUpperAscii(nm)] = iv;
                }
            }
        } else if (up == "ACTION" && !inProc) {
            // `ACTION PROGRAM = DO_INIT, DO_ERASE OPTIONAL, DO_PROGRAM RECOMMENDED, DO_VERIFY;`
            const size_t eq = s.find('=');
            if (eq == std::string_view::npos) return false;
            std::string_view actName = ReaderText::TrimSv(s.substr(0u, eq));
            if (actName.empty()) return false;

            JamAction act;
            act.name = std::string(actName);
            std::string_view rhs = s.substr(eq + 1u);
            size_t cp = 0u;
            while (cp < rhs.size()) {
                const size_t comma = rhs.find(',', cp);
                std::string_view item = (comma == std::string_view::npos) ? rhs.substr(cp) : rhs.substr(cp, comma - cp);
                cp = (comma == std::string_view::npos) ? rhs.size() : (comma + 1u);
                std::string_view stepTok;
                if (ReaderText::NextTokenSv(item, stepTok) && !stepTok.empty()) {
                    act.steps.emplace_back(stepTok);
                }
            }
            if (actions.size() < 64u) actions.push_back(std::move(act));
        } else if (up == "PROCEDURE") {
            if (inProc) return false;
            std::string_view pName;
            if (!ReaderText::NextTokenSv(s, pName) || pName.empty()) return false;
            // Strip `USES ...` if attached
            JamProc pr;
            pr.name = std::string(pName);
            procs.push_back(std::move(pr));
            inProc = true;
        } else if (up == "ENDPROC") {
            if (!inProc) return false;
            inProc = false;
        } else if (inProc && !procs.empty()) {
            JamProc& cur = procs.back();
            if (up == "IRSCAN" || up == "DRSCAN") {
                const bool isIr = (up == "IRSCAN");
                if (isIr) ++cur.irScans;
                else      ++cur.drScans;

                // `<IRSCAN|DRSCAN> <bitLen>, [$hex | boolArray], [COMPARE $tdo, $mask];`
                const size_t c1 = s.find(',');
                std::string_view lenSv = ReaderText::TrimSv((c1 == std::string_view::npos) ? s : s.substr(0u, c1));
                uint64_t bits = 0ULL;
                if (!ReaderText::ParseUint64Sv(lenSv, bits, 10)) {
                    auto itC = intConsts.find(ReaderText::ToUpperAscii(lenSv));
                    bits = (itC != intConsts.end()) ? itC->second : (isIr ? 10ULL : 32ULL);
                }
                bits = std::clamp<uint64_t>(bits, 1ULL, 65536ULL);
                cur.totalScanBits += bits;

                if (cur.scans.size() < 32u) {
                    JamScanOp op;
                    op.isIr = isIr;
                    op.bitLen = static_cast<uint32_t>(std::min<uint64_t>(bits, 64ULL));
                    if (c1 != std::string_view::npos) {
                        std::string_view rest = s.substr(c1 + 1u);
                        const size_t cmpPos = ReaderText::ToUpperAscii(rest).find("COMPARE");
                        std::string_view tdiPart = (cmpPos == std::string::npos) ? rest : rest.substr(0u, cmpPos);
                        ParseStaplDollarHex(tdiPart, op.bitLen, op.tdiBytes);
                        if (cmpPos != std::string::npos) {
                            std::string_view cmpPart = rest.substr(cmpPos + 7u);
                            const size_t cm2 = cmpPart.find(',');
                            ParseStaplDollarHex((cm2 == std::string_view::npos) ? cmpPart : cmpPart.substr(0u, cm2), op.bitLen, op.tdoBytes);
                            if (cm2 != std::string_view::npos) {
                                ParseStaplDollarHex(cmpPart.substr(cm2 + 1u), op.bitLen, op.maskBytes);
                            }
                        }
                    }
                    if (op.tdiBytes.empty()) {
                        const size_t nb = (op.bitLen + 7u) / 8u;
                        op.tdiBytes.assign(nb, isIr ? 0x55u : 0xA5u);
                    }
                    cur.scans.push_back(std::move(op));
                }
            } else if (up == "WAIT") {
                ++cur.waits;
            } else if (up == "CALL") {
                std::string_view callee;
                if (ReaderText::NextTokenSv(s, callee) && !callee.empty()) {
                    cur.calledProcs.emplace_back(callee);
                }
            }
        }
    }

    if (inProc || (actions.empty() && procs.empty())) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    // 1. Render Top-Half ACTION -> PROCEDURE Call-Graph DAG (`y` >= 28.0 mm so JTAG waveforms sit cleanly below!)
    const float dagBaseY = 66.0f;
    const float actX = -38.0f;
    const float procX = 14.0f;

    std::unordered_map<std::string, size_t> procIdxByUpper;
    const size_t nProcs = std::min<size_t>(procs.size(), 24u);
    for (size_t pi = 0; pi < nProcs; ++pi) {
        procIdxByUpper[ReaderText::ToUpperAscii(procs[pi].name)] = pi;
        const float py = dagBaseY - static_cast<float>(pi) * 6.8f;
        const float pw = 28.0f, ph = 4.6f;

        DrawEdaBoxRect(out, procX, py - ph * 0.5f, procX + pw, py + ph * 0.5f, 0.28f, EdaLayerId::InnerCopper, 0);
        wchar_t pLbl[160] = {};
        swprintf_s(pLbl, L"%hs (IR:%zu DR:%zu %llub)",
                   procs[pi].name.c_str(), procs[pi].irScans, procs[pi].drScans,
                   static_cast<unsigned long long>(procs[pi].totalScanBits));
        out.labels.push_back({ procX + 0.9f, py - 0.35f, 0.78f, EdaLayerId::SchLabel, pLbl });
        out.pads.push_back({ procX, py, 0.85f, 0.85f, 0.0f, 0.0f, 1, EdaLayerId::InnerCopper, PathUtils::Utf8ToWide(procs[pi].name), L"PROC" });

        EdaComponent3D pc;
        pc.refDes = L"PROC_" + std::to_wstring(pi + 1u);
        pc.value = PathUtils::Utf8ToWide(procs[pi].name);
        pc.footprint = L"STAPL_PROC";
        pc.x = procX + pw * 0.5f; pc.y = py;
        pc.widthMm = pw; pc.lengthMm = ph; pc.heightMm = 1.1f;
        pc.pinCount = 2;
        pc.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(pc));
    }

    const size_t nActs = std::min<size_t>(actions.size(), 16u);
    size_t callEdges = 0u;
    for (size_t ai = 0; ai < nActs; ++ai) {
        const float ay = dagBaseY - static_cast<float>(ai) * 8.2f;
        const float aw = 22.0f, ah = 5.0f;
        DrawEdaBoxRect(out, actX, ay - ah * 0.5f, actX + aw, ay + ah * 0.5f, 0.32f, EdaLayerId::TopCopper, 0);
        out.labels.push_back({ actX + 1.0f, ay - 0.35f, 0.88f, EdaLayerId::SchLabel, L"ACTION " + PathUtils::Utf8ToWide(actions[ai].name) });
        out.pads.push_back({ actX + aw, ay, 0.90f, 0.90f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, PathUtils::Utf8ToWide(actions[ai].name), L"ACTION" });

        EdaComponent3D ac;
        ac.refDes = L"ACT_" + std::to_wstring(ai + 1u);
        ac.value = PathUtils::Utf8ToWide(actions[ai].name);
        ac.footprint = L"STAPL_ACTION";
        ac.x = actX + aw * 0.5f; ac.y = ay;
        ac.widthMm = aw; ac.lengthMm = ah; ac.heightMm = 1.2f;
        ac.pinCount = std::max<int>(1, static_cast<int>(actions[ai].steps.size()));
        ac.category = EdaPartCategory::Connector;
        out.components.push_back(std::move(ac));

        // Wire actual `ACTION -> PROCEDURE` call edges from `actions[ai].steps`!
        for (size_t si = 0; si < actions[ai].steps.size(); ++si) {
            auto it = procIdxByUpper.find(ReaderText::ToUpperAscii(actions[ai].steps[si]));
            if (it == procIdxByUpper.end()) continue;
            const float py = dagBaseY - static_cast<float>(it->second) * 6.8f;
            const float midX = -6.0f + static_cast<float>((callEdges % 7u)) * 1.4f;
            const int netId = static_cast<int>(10u + ai);
            out.traces.push_back({ actX + aw, ay, midX, ay, 0.22f, EdaLayerId::SchWire, netId });
            out.traces.push_back({ midX, ay, midX, py, 0.22f, EdaLayerId::SchWire, netId });
            out.traces.push_back({ midX, py, procX, py, 0.22f, EdaLayerId::SchWire, netId });
            // Arrowhead into target PROCEDURE pin
            out.traces.push_back({ procX - 1.1f, py + 0.55f, procX, py, 0.22f, EdaLayerId::SchWire, netId });
            out.traces.push_back({ procX - 1.1f, py - 0.55f, procX, py, 0.22f, EdaLayerId::SchWire, netId });
            ++callEdges;
        }
    }

    // Also wire intra-procedure `CALL <callee>` arcs on the right side of PROCEDURE cards
    for (size_t pi = 0; pi < nProcs; ++pi) {
        for (const auto& callee : procs[pi].calledProcs) {
            auto it = procIdxByUpper.find(ReaderText::ToUpperAscii(callee));
            if (it == procIdxByUpper.end() || it->second == pi) continue;
            const float y0 = dagBaseY - static_cast<float>(pi) * 6.8f;
            const float y1 = dagBaseY - static_cast<float>(it->second) * 6.8f;
            const float rx = procX + 28.0f + 2.5f + static_cast<float>(pi % 4u) * 1.2f;
            out.traces.push_back({ procX + 28.0f, y0, rx, y0, 0.18f, EdaLayerId::InnerCopper, 5 });
            out.traces.push_back({ rx, y0, rx, y1, 0.18f, EdaLayerId::InnerCopper, 5 });
            out.traces.push_back({ rx, y1, procX + 28.0f, y1, 0.18f, EdaLayerId::InnerCopper, 5 });
            ++callEdges;
        }
    }

    // 2. Simulate IEEE 1149.1 TAP state machine over parsed `IRSCAN` / `DRSCAN` vectors & render 4-lane JTAG waveforms!
    DetailIndustrialAndWaveEda::JtagTapTracker tap;
    tap.StepTo(0x00u, true); // Test-Logic-Reset
    tap.StepTo(0x01u, false); // Run-Test/Idle

    size_t totalIrOps = 0u, totalDrOps = 0u;
    for (const auto& pr : procs) {
        totalIrOps += pr.irScans;
        totalDrOps += pr.drScans;
        for (const auto& sc : pr.scans) {
            if (tap.cycles.size() >= DetailIndustrialAndWaveEda::JtagTapTracker::kMaxVisualCycles) break;
            const uint8_t shiftSt = sc.isIr ? 0x0Bu : 0x04u; // Shift-IR (0x0B) vs Shift-DR (0x04)
            tap.ShiftBytesBE(
                shiftSt,
                0x01u, // Run-Test/Idle
                std::min<uint32_t>(sc.bitLen, 32u),
                sc.tdiBytes.empty() ? nullptr : sc.tdiBytes.data(),
                sc.tdoBytes.empty() ? nullptr : sc.tdoBytes.data(),
                sc.maskBytes.empty() ? nullptr : &sc.maskBytes,
                true);
        }
        if (pr.waits > 0u && tap.cycles.size() < DetailIndustrialAndWaveEda::JtagTapTracker::kMaxVisualCycles) {
            tap.EmitIdleClocks(4u, 0x01u);
        }
    }

    if (!tap.cycles.empty()) {
        wchar_t waveSummary[160] = {};
        swprintf_s(waveSummary, L"JTAG Scan Waveform (%zu IRSCAN, %zu DRSCAN, %zu TCK cycles)",
                   totalIrOps, totalDrOps, tap.cycles.size());
        DetailIndustrialAndWaveEda::RenderJtag4LaneWaveforms(out, tap.cycles, waveSummary);
    }

    const std::wstring wDev = PathUtils::Utf8ToWide(deviceName.empty() ? "JTAG_TAP" : deviceName);
    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"JEDEC STAPL / Altera JAM (.jam — %ls, %zu actions, %zu procs, %zu IR/%zu DR scans)",
               wDev.c_str(), actions.size(), procs.size(), totalIrOps, totalDrOps);
    out.formatTitle = titleBuf;
    out.netCount = std::max<size_t>(4u, callEdges + 4u);
    FinalizeBoardBounds(out);
    out.valid = true;
    return !out.traces.empty();
}

} // namespace Formats
