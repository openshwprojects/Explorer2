#pragma once
// BlifNetlist.h — Berkeley BLIF & Yosys EBLIF Gate-Level Netlist (.blif / .eblif) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// 2. Berkeley BLIF / Yosys EBLIF Gate-Level Netlist (.blif / .eblif)
inline bool ParseBlifNetlistBoard(std::string_view sv, ParsedEdaBoard& out) {
    using namespace DetailGateAndDaqEda;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }
    if (sv.find(".model") == std::string_view::npos || sv.find(".end") == std::string_view::npos) {
        return false;
    }

    // Join '\' continuation lines & strip '#' comments
    std::vector<std::string> lines;
    std::string cur;
    size_t pos = 0;
    while (pos < sv.size() && lines.size() < 50000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view raw = sv.substr(pos, eol - pos);
        pos = eol + 1;
        size_t hash = raw.find('#');
        if (hash != std::string_view::npos) raw = raw.substr(0, hash);
        while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
        if (!raw.empty() && raw.back() == '\\') {
            raw.remove_suffix(1);
            cur.append(raw.data(), raw.size());
            cur.push_back(' ');
            continue;
        }
        cur.append(raw.data(), raw.size());
        size_t s = cur.find_first_not_of(" \t");
        if (s != std::string::npos) lines.push_back(cur.substr(s));
        cur.clear();
    }

    auto splitWs = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> t;
        size_t p = 0;
        while (p < s.size()) {
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
            if (p >= s.size()) break;
            size_t e = p;
            while (e < s.size() && s[e] != ' ' && s[e] != '\t') ++e;
            t.push_back(s.substr(p, e - p));
            p = e;
        }
        return t;
    };

    std::string modelName;
    bool sawEnd = false;
    std::vector<GateNode> dagNodes;
    int lutCounter = 0, ffCounter = 0, subCounter = 0;

    for (size_t li = 0; li < lines.size(); ++li) {
        auto toks = splitWs(lines[li]);
        if (toks.empty()) continue;
        const std::string& kw = toks[0];
        if (kw == ".model" && toks.size() >= 2) {
            modelName = toks[1];
        } else if (kw == ".end") {
            sawEnd = true;
            break;
        } else if (kw == ".inputs") {
            for (size_t i = 1; i < toks.size(); ++i) {
                GateNode gn;
                gn.instName = toks[i]; gn.cellType = "INPUT"; gn.isPrimaryIn = true;
                gn.pins.push_back({ "O", toks[i], true, false });
                dagNodes.push_back(std::move(gn));
            }
        } else if (kw == ".outputs") {
            for (size_t i = 1; i < toks.size(); ++i) {
                GateNode gn;
                gn.instName = toks[i]; gn.cellType = "OUTPUT"; gn.isPrimaryOut = true;
                gn.pins.push_back({ "I", toks[i], false, false });
                dagNodes.push_back(std::move(gn));
            }
        } else if (kw == ".latch" && toks.size() >= 3) {
            GateNode gn;
            gn.instName = "DFF_" + std::to_string(++ffCounter);
            gn.cellType = "DFF";
            gn.isSeq = true;
            gn.pins.push_back({ "D", toks[1], false, false });
            if (toks.size() >= 5 && toks[4] != "NIL") gn.pins.push_back({ "CLK", toks[4], false, false });
            gn.pins.push_back({ "Q", toks[2], true, false });
            dagNodes.push_back(std::move(gn));
        } else if (kw == ".names" && toks.size() >= 2) {
            // Collect PLA cover rows following .names
            std::vector<std::string> cover;
            while (li + 1 < lines.size() && !lines[li + 1].empty() && lines[li + 1][0] != '.') {
                cover.push_back(lines[++li]);
            }
            size_t nIn = toks.size() - 2;
            std::string outNet = toks.back();
            GateNode gn;
            gn.instName = "G" + std::to_string(++lutCounter);
            if (nIn == 0) gn.cellType = cover.empty() ? "CONST0" : "CONST1";
            else if (nIn == 1) gn.cellType = (!cover.empty() && cover[0][0] == '0') ? "INV" : "BUF";
            else if (nIn == 2 && cover.size() == 1 && cover[0].rfind("11", 0) == 0) gn.cellType = "AND2";
            else if (nIn == 2 && cover.size() == 2 && ((cover[0].rfind("10", 0) == 0 && cover[1].rfind("01", 0) == 0) ||
                                                       (cover[0].rfind("01", 0) == 0 && cover[1].rfind("10", 0) == 0))) gn.cellType = "XOR2";
            else if (nIn == 2 && cover.size() == 2) gn.cellType = "OR2";
            else gn.cellType = "LUT" + std::to_string(nIn);

            for (size_t k = 0; k < nIn; ++k) {
                gn.pins.push_back({ "I" + std::to_string(k), toks[1 + k], false, gn.cellType == "INV" });
            }
            gn.pins.push_back({ "Y", outNet, true, false });
            dagNodes.push_back(std::move(gn));
        } else if ((kw == ".gate" || kw == ".subckt") && toks.size() >= 3) {
            GateNode gn;
            gn.cellType = toks[1];
            gn.instName = "U" + std::to_string(++subCounter);
            std::string cl = LowerAsciiCopy(gn.cellType);
            gn.isSeq = (cl.find("dff") != std::string::npos || cl.find("fd") != std::string::npos || cl.find("sdff") != std::string::npos);
            for (size_t k = 2; k < toks.size(); ++k) {
                size_t eq = toks[k].find('=');
                if (eq == std::string::npos) continue;
                std::string pn = toks[k].substr(0, eq);
                std::string nn = toks[k].substr(eq + 1);
                bool isOut = (pn == "Y" || pn == "Q" || pn == "O" || pn == "Z" || pn == "ZN" || pn == "CO" || pn == "OUT");
                gn.pins.push_back({ pn, nn, isOut, false });
            }
            // EBLIF optional .cname <instName> right after .subckt
            if (li + 1 < lines.size() && lines[li + 1].rfind(".cname ", 0) == 0) {
                auto ct = splitWs(lines[++li]);
                if (ct.size() >= 2) gn.instName = ct[1];
            }
            dagNodes.push_back(std::move(gn));
        }
    }

    if (!sawEnd || dagNodes.empty()) return false;
    std::wstring title = L"BLIF / EBLIF Gate Netlist (" + PathUtils::Utf8ToWide(modelName.empty() ? "top" : modelName) + L")";
    return RenderGateDagToBoard(title, dagNodes, out);
}

} // namespace Formats
