#pragma once
// SynopsysLibertyLib.h — Synopsys Liberty Standard-Cell Timing Library (.lib) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// 4. Synopsys Liberty Standard-Cell Timing Library (.lib)
inline bool IsLikelyLibertyLibBytes(const uint8_t* data, size_t size) {
    if (!data || size < 24) return false;
    if (size >= 8 && std::memcmp(data, "!<arch>\n", 8) == 0) return false; // COFF archive .lib!
    size_t scan = std::min<size_t>(size, 8192);
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == 0) return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    return (sv.find("library (") != std::string_view::npos || sv.find("library(") != std::string_view::npos) &&
           sv.find('{') != std::string_view::npos;
}

inline bool ParseLibertyLibBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (!IsLikelyLibertyLibBytes(reinterpret_cast<const uint8_t*>(sv.data()), sv.size())) return false;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }

    // Strip /* ... */ comments and validate balanced { ... } braces
    std::string clean;
    clean.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '/' && i + 1 < sv.size() && sv[i + 1] == '*') {
            i += 2;
            while (i + 1 < sv.size() && !(sv[i] == '*' && sv[i + 1] == '/')) ++i;
            ++i;
            clean.push_back(' ');
        } else if (sv[i] == '/' && i + 1 < sv.size() && sv[i + 1] == '/') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            clean.push_back('\n');
        } else {
            clean.push_back(sv[i]);
        }
    }

    int braceDepth = 0;
    bool inQ = false, sawBrace = false;
    for (size_t i = 0; i < clean.size(); ++i) {
        char c = clean[i];
        if (c == '\\' && i + 1 < clean.size()) { ++i; continue; }
        if (c == '"') { inQ = !inQ; continue; }
        if (!inQ) {
            if (c == '{') { ++braceDepth; sawBrace = true; }
            else if (c == '}') { if (--braceDepth < 0) return false; }
        }
    }
    if (!sawBrace || inQ || braceDepth != 0) return false;

    struct LibPin {
        std::string name;
        std::string dir = "input";
        std::string func;
        std::vector<std::pair<std::string, double>> timingFrom; // (related_pin, midDelayNs)
    };
    struct LibCell {
        std::string name;
        double area = 1.0;
        std::vector<LibPin> pins;
    };

    std::string libName;
    std::vector<LibCell> cells;

    // Recursive group walker: <groupType> ( <arg> ) { ... } and <attr> : <val> ;
    size_t p = 0;
    auto skipWs = [&]() {
        while (p < clean.size() && (static_cast<unsigned char>(clean[p]) <= 0x20u || clean[p] == ';')) ++p;
    };
    auto readIdentOrStr = [&]() -> std::string {
        skipWs();
        if (p >= clean.size()) return {};
        if (clean[p] == '"') {
            ++p;
            std::string s;
            while (p < clean.size() && clean[p] != '"') {
                if (clean[p] == '\\' && p + 1 < clean.size()) ++p;
                s.push_back(clean[p++]);
            }
            if (p < clean.size()) ++p;
            return s;
        }
        size_t s = p;
        while (p < clean.size() && static_cast<unsigned char>(clean[p]) > 0x20u &&
               clean[p] != '(' && clean[p] != ')' && clean[p] != '{' && clean[p] != '}' && clean[p] != ':' && clean[p] != ';') ++p;
        return clean.substr(s, p - s);
    };
    auto readParenArg = [&]() -> std::string {
        skipWs();
        if (p >= clean.size() || clean[p] != '(') return {};
        int d = 1; ++p;
        size_t s = p;
        while (p < clean.size() && d > 0) {
            if (clean[p] == '(') ++d;
            else if (clean[p] == ')') --d;
            ++p;
        }
        std::string raw = clean.substr(s, (p - 1) - s);
        // Strip quotes/spaces
        std::string outS;
        for (char c : raw) if (c != '"' && c != ' ' && c != '\t' && c != '\r' && c != '\n') outS.push_back(c);
        return outS;
    };
    auto skipBraceBlock = [&]() {
        skipWs();
        if (p >= clean.size() || clean[p] != '{') return;
        int d = 1; ++p;
        bool q = false;
        while (p < clean.size() && d > 0) {
            if (clean[p] == '\\' && p + 1 < clean.size()) { p += 2; continue; }
            if (clean[p] == '"') q = !q;
            else if (!q && clean[p] == '{') ++d;
            else if (!q && clean[p] == '}') --d;
            ++p;
        }
    };

    // Find outer library(...) { ... }
    while (p < clean.size()) {
        std::string kw = readIdentOrStr();
        if (kw.empty()) break;
        if (kw == "library") {
            libName = readParenArg();
            skipWs();
            if (p < clean.size() && clean[p] == '{') ++p;
            break;
        }
    }
    if (p >= clean.size()) return false;

    while (p < clean.size() && cells.size() < 128) {
        skipWs();
        if (p >= clean.size() || clean[p] == '}') break;
        std::string kw = readIdentOrStr();
        if (kw.empty()) { ++p; continue; }
        skipWs();
        if (p < clean.size() && clean[p] == ':') {
            ++p;
            readIdentOrStr(); // simple attribute value
            continue;
        }
        if (p < clean.size() && clean[p] == '(') {
            std::string arg = readParenArg();
            skipWs();
            if (p < clean.size() && clean[p] == '{') {
                if (kw == "cell") {
                    ++p; // enter cell {
                    LibCell cell;
                    cell.name = arg;
                    while (p < clean.size()) {
                        skipWs();
                        if (p >= clean.size() || clean[p] == '}') { ++p; break; }
                        std::string ckw = readIdentOrStr();
                        skipWs();
                        if (p < clean.size() && clean[p] == ':') {
                            ++p;
                            std::string cv = readIdentOrStr();
                            if (ckw == "area") cell.area = std::strtod(cv.c_str(), nullptr);
                        } else if (p < clean.size() && clean[p] == '(') {
                            std::string parg = readParenArg();
                            skipWs();
                            if (p < clean.size() && clean[p] == '{') {
                                if (ckw == "pin") {
                                    ++p;
                                    LibPin pin;
                                    pin.name = parg;
                                    while (p < clean.size()) {
                                        skipWs();
                                        if (p >= clean.size() || clean[p] == '}') { ++p; break; }
                                        std::string pkw = readIdentOrStr();
                                        skipWs();
                                        if (p < clean.size() && clean[p] == ':') {
                                            ++p;
                                            std::string pv = readIdentOrStr();
                                            if (pkw == "direction") pin.dir = DetailGateAndDaqEda::LowerAsciiCopy(pv);
                                            else if (pkw == "function") pin.func = pv;
                                        } else if (p < clean.size() && clean[p] == '(') {
                                            readParenArg();
                                            skipWs();
                                            if (p < clean.size() && clean[p] == '{') {
                                                if (pkw == "timing") {
                                                    size_t tStart = p;
                                                    skipBraceBlock();
                                                    std::string_view tblk(clean.data() + tStart, p - tStart);
                                                    // Extract related_pin and first numeric value in cell_rise / cell_fall values("...")
                                                    std::string relPin;
                                                    size_t rp = tblk.find("related_pin");
                                                    if (rp != std::string_view::npos) {
                                                        size_t q1 = tblk.find('"', rp);
                                                        size_t q2 = (q1 != std::string_view::npos) ? tblk.find('"', q1 + 1) : std::string_view::npos;
                                                        if (q1 != std::string_view::npos && q2 != std::string_view::npos) {
                                                            relPin = std::string(tblk.substr(q1 + 1, q2 - q1 - 1));
                                                        }
                                                    }
                                                    double dlyVal = 0.05;
                                                    size_t vp = tblk.find("values");
                                                    if (vp != std::string_view::npos) {
                                                        size_t q1 = tblk.find('"', vp);
                                                        if (q1 != std::string_view::npos) {
                                                            dlyVal = std::strtod(std::string(tblk.substr(q1 + 1, 32)).c_str(), nullptr);
                                                        }
                                                    }
                                                    if (!relPin.empty() && pin.timingFrom.size() < 8) {
                                                        pin.timingFrom.push_back({ relPin, dlyVal });
                                                    }
                                                } else {
                                                    skipBraceBlock();
                                                }
                                            }
                                        } else {
                                            ++p;
                                        }
                                    }
                                    cell.pins.push_back(std::move(pin));
                                } else {
                                    skipBraceBlock();
                                }
                            }
                        } else {
                            ++p;
                        }
                    }
                    if (!cell.pins.empty()) cells.push_back(std::move(cell));
                } else {
                    skipBraceBlock();
                }
            }
        }
    }

    if (cells.empty()) return false;

    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"Synopsys Liberty Timing Library (.lib — " + PathUtils::Utf8ToWide(libName.empty() ? "stdcells" : libName) +
                      L", " + std::to_wstring(cells.size()) + L" cells)";

    const size_t cols = std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(cells.size())))));
    float curX = 0.0f, curY = 0.0f;
    size_t totalArcs = 0;

    for (size_t ci = 0; ci < cells.size(); ++ci) {
        const auto& cell = cells[ci];
        if (ci > 0 && (ci % cols) == 0) {
            curX = 0.0f;
            curY -= 22.0f;
        }
        const float boxW = 16.0f, boxH = 12.0f;
        float cx = curX + boxW * 0.5f, cy = curY;

        out.traces.push_back({ curX,        cy - boxH * 0.5f, curX + boxW, cy - boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX + boxW, cy - boxH * 0.5f, curX + boxW, cy + boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX + boxW, cy + boxH * 0.5f, curX,        cy + boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX,        cy + boxH * 0.5f, curX,        cy - boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });

        wchar_t hdr[128] = {};
        swprintf_s(hdr, L"%hs (area=%.2f)", cell.name.c_str(), cell.area);
        out.labels.push_back({ curX, cy + boxH * 0.5f + 0.9f, 0.95f, EdaLayerId::SchLabel, hdr });

        std::unordered_map<std::string, std::pair<float, float>> pinCoords;
        std::vector<const LibPin*> ins, outs;
        for (const auto& lp : cell.pins) {
            if (lp.dir == "output") outs.push_back(&lp);
            else ins.push_back(&lp);
        }
        for (size_t i = 0; i < ins.size(); ++i) {
            float py = cy + boxH * 0.35f - static_cast<float>(i) * (boxH * 0.7f / std::max<size_t>(1, ins.size()));
            out.traces.push_back({ curX - 2.4f, py, curX, py, 0.24f, EdaLayerId::SchWire, 0 });
            out.pads.push_back({ curX - 2.4f, py, 0.65f, 0.65f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(ins[i]->name), L"" });
            out.labels.push_back({ curX + 0.5f, py - 0.3f, 0.8f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(ins[i]->name) });
            pinCoords[ins[i]->name] = { curX, py };
        }
        for (size_t i = 0; i < outs.size(); ++i) {
            float py = cy + boxH * 0.25f - static_cast<float>(i) * 3.0f;
            out.traces.push_back({ curX + boxW, py, curX + boxW + 2.4f, py, 0.24f, EdaLayerId::SchWire, 0 });
            out.pads.push_back({ curX + boxW + 2.4f, py, 0.65f, 0.65f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(outs[i]->name), PathUtils::Utf8ToWide(outs[i]->func) });
            std::string fnLbl = outs[i]->name + (outs[i]->func.empty() ? "" : ("=" + outs[i]->func));
            out.labels.push_back({ curX + 3.5f, py - 0.3f, 0.78f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(fnLbl) });
            pinCoords[outs[i]->name] = { curX + boxW, py };

            for (const auto& tf : outs[i]->timingFrom) {
                auto itSrc = pinCoords.find(tf.first);
                if (itSrc != pinCoords.end()) {
                    out.traces.push_back({ itSrc->second.first + 0.4f, itSrc->second.second,
                                           curX + boxW - 0.4f, py, 0.16f, EdaLayerId::TopCopper, static_cast<int>(totalArcs + 1) });
                    ++totalArcs;
                }
            }
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(cell.name);
        comp.footprint = L"LIBERTY_CELL";
        comp.x = cx; comp.y = cy;
        comp.widthMm = boxW; comp.lengthMm = boxH; comp.heightMm = 1.0f;
        comp.pinCount = static_cast<int>(cell.pins.size());
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));

        curX += 26.0f;
    }

    out.netCount = std::max<size_t>(1, totalArcs > 0 ? totalArcs : cells.size());
    return true;
}

} // namespace Formats
