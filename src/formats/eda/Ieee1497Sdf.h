#pragma once
// Ieee1497Sdf.h — IEEE 1497 Standard Delay Format (.sdf) Timing & Interconnect Arc Reader
// Parses `(DELAYFILE ...)`, `(TIMESCALE ...)`, `(CELL (CELLTYPE ...) (INSTANCE ...) (DELAY ...))`
// including `(IOPATH inPin outPin ...)`, `(PORT pin ...)`, and `(INTERCONNECT srcPin dstPin ...)`,
// placing cell instances topologically and routing actual timing-arc wires + arrowheads!
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool IsLikelyIeeeSdfBytes(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 4096));
    return ContainsTextCI(sv, "(DELAYFILE") || ContainsTextCI(sv, "SDFVERSION");
}

namespace DetailIeeeSdf {

struct SdfIoPath {
    std::string inPin;
    std::string outPin;
    double delayPs = 0.0;
    bool isPortDelay = false;
};

struct SdfInterconnect {
    std::string srcInst;
    std::string srcPin;
    std::string dstInst;
    std::string dstPin;
    double delayPs = 0.0;
};

struct SdfCell {
    std::string cellType;
    std::string instName;
    std::vector<SdfIoPath> ioPaths;
    size_t timingChecks = 0u;
    double maxDelayPs = 0.0;
};

inline std::string UnquoteSdf(std::string_view s) {
    s = ReaderText::TrimSv(s);
    if (s.size() >= 2u && s.front() == '"' && s.back() == '"') {
        s = s.substr(1u, s.size() - 2u);
    }
    return std::string(s);
}

// Extract typical/max numeric delay from `(0.12:0.15:0.18)` SNode
inline double ExtractDelvalScaled(const DetailSpecctra::SNode& node, double scaleToPs) {
    if (!node.isList) {
        std::string_view a = node.atom;
        // Pick middle (typical) of `min:typ:max` if present, else first number
        const size_t c1 = a.find(':');
        if (c1 != std::string_view::npos) {
            const size_t c2 = a.find(':', c1 + 1u);
            std::string_view mid = (c2 == std::string_view::npos) ? a.substr(c1 + 1u) : a.substr(c1 + 1u, c2 - c1 - 1u);
            double v = 0.0;
            if (ReaderText::ParseDoubleSv(mid, v) && std::isfinite(v)) return std::fabs(v) * scaleToPs;
            if (ReaderText::ParseDoubleSv(a.substr(0, c1), v) && std::isfinite(v)) return std::fabs(v) * scaleToPs;
        }
        double v = 0.0;
        if (ReaderText::ParseDoubleSv(a, v) && std::isfinite(v)) return std::fabs(v) * scaleToPs;
        return 0.0;
    }
    double best = 0.0;
    for (const auto& ch : node.list) {
        best = std::max(best, ExtractDelvalScaled(ch, scaleToPs));
    }
    return best;
}

// Strip `(posedge clk)` / `(negedge d)` port spec wrapper -> pin name string
inline std::string ExtractSdfPortName(const DetailSpecctra::SNode& n) {
    if (!n.isList) return UnquoteSdf(n.atom);
    if (n.list.size() >= 2u && !n.list[1].isList) return UnquoteSdf(n.list[1].atom);
    if (!n.list.empty() && !n.list[0].isList) return UnquoteSdf(n.list[0].atom);
    return {};
}

inline void SplitHierPin(const std::string& fullPin, std::string& outInst, std::string& outPin) {
    size_t sep = fullPin.find_last_of("/.");
    if (sep != std::string::npos && sep > 0 && sep + 1 < fullPin.size()) {
        outInst = fullPin.substr(0, sep);
        outPin  = fullPin.substr(sep + 1);
    } else {
        outInst = "TOP_IO";
        outPin  = fullPin;
    }
}

} // namespace DetailIeeeSdf

inline bool ParseIeee1497SdfBoard(std::string_view sv, ParsedEdaBoard& out) {
    using namespace DetailIeeeSdf;

    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.size() < 20u || sv.size() > (64u << 20)) return false;
    if (sv.find('\0') != std::string_view::npos) return false;

    // Strip `//` and `/* ... */` SDF comments before S-expression tokenization
    std::string cleaned;
    cleaned.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ) {
        if (i + 1u < sv.size() && sv[i] == '/' && sv[i + 1u] == '/') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (i + 1u < sv.size() && sv[i] == '/' && sv[i + 1u] == '*') {
            i += 2u;
            bool closed = false;
            while (i + 1u < sv.size()) {
                if (sv[i] == '*' && sv[i + 1u] == '/') { i += 2u; closed = true; break; }
                ++i;
            }
            if (!closed) return false;
            cleaned.push_back(' ');
            continue;
        }
        cleaned.push_back(sv[i++]);
    }

    std::vector<DetailSpecctra::SNode> roots;
    if (!DetailSpecctra::ParseSExprTree(cleaned, roots) || roots.empty()) {
        return false;
    }

    const DetailSpecctra::SNode* dfNode = nullptr;
    for (const auto& ch : roots) {
        if (ch.isList && !ch.list.empty() && !ch.list[0].isList &&
            ReaderText::AsciiEqCI(ch.list[0].atom, "DELAYFILE")) {
            dfNode = &ch;
            break;
        }
    }
    if (!dfNode) return false;

    std::string designName;
    double timescalePs = 1000.0; // default 1 ns = 1000 ps
    std::vector<SdfCell> cells;
    std::vector<SdfInterconnect> interconnects;

    auto walkDelayStmt = [&](auto& self, const DetailSpecctra::SNode& n, SdfCell& curCell) -> void {
        if (!n.isList || n.list.empty() || n.list[0].isList) return;
        const std::string kw = ReaderText::ToUpperAscii(n.list[0].atom);

        if (kw == "IOPATH" && n.list.size() >= 4u) {
            SdfIoPath io;
            io.inPin  = ExtractSdfPortName(n.list[1]);
            io.outPin = ExtractSdfPortName(n.list[2]);
            for (size_t k = 3u; k < n.list.size(); ++k) {
                io.delayPs = std::max(io.delayPs, ExtractDelvalScaled(n.list[k], timescalePs));
            }
            curCell.maxDelayPs = std::max(curCell.maxDelayPs, io.delayPs);
            if (curCell.ioPaths.size() < 32u) curCell.ioPaths.push_back(std::move(io));
            return;
        }
        if (kw == "PORT" && n.list.size() >= 3u) {
            SdfIoPath pt;
            pt.inPin = ExtractSdfPortName(n.list[1]);
            pt.isPortDelay = true;
            for (size_t k = 2u; k < n.list.size(); ++k) {
                pt.delayPs = std::max(pt.delayPs, ExtractDelvalScaled(n.list[k], timescalePs));
            }
            curCell.maxDelayPs = std::max(curCell.maxDelayPs, pt.delayPs);
            if (curCell.ioPaths.size() < 32u) curCell.ioPaths.push_back(std::move(pt));
            return;
        }
        if ((kw == "INTERCONNECT" || kw == "NETDELAY") && n.list.size() >= 3u) {
            if (kw == "INTERCONNECT" && n.list.size() >= 4u) {
                SdfInterconnect ic;
                SplitHierPin(ExtractSdfPortName(n.list[1]), ic.srcInst, ic.srcPin);
                SplitHierPin(ExtractSdfPortName(n.list[2]), ic.dstInst, ic.dstPin);
                for (size_t k = 3u; k < n.list.size(); ++k) {
                    ic.delayPs = std::max(ic.delayPs, ExtractDelvalScaled(n.list[k], timescalePs));
                }
                if (interconnects.size() < 256u) interconnects.push_back(std::move(ic));
            }
            return;
        }
        if (kw == "SETUP" || kw == "HOLD" || kw == "SETUPHOLD" || kw == "RECOVERY" ||
            kw == "REMOVAL" || kw == "RECREM" || kw == "WIDTH" || kw == "PERIOD") {
            ++curCell.timingChecks;
            return;
        }

        for (size_t i = 1u; i < n.list.size(); ++i) {
            if (n.list[i].isList) self(self, n.list[i], curCell);
        }
    };

    for (size_t i = 1u; i < dfNode->list.size(); ++i) {
        const auto& sec = dfNode->list[i];
        if (!sec.isList || sec.list.empty() || sec.list[0].isList) continue;
        const std::string tag = ReaderText::ToUpperAscii(sec.list[0].atom);

        if (tag == "DESIGN" && sec.list.size() >= 2u) {
            designName = UnquoteSdf(sec.list[1].atom);
        } else if (tag == "TIMESCALE" && sec.list.size() >= 2u) {
            // e.g. `(TIMESCALE 1ns)` or `(TIMESCALE 100 ps)`
            std::string ts;
            for (size_t k = 1u; k < sec.list.size(); ++k) {
                if (!sec.list[k].isList) ts += sec.list[k].atom;
            }
            double num = 1.0;
            size_t uOff = 0;
            while (uOff < ts.size() && ((ts[uOff] >= '0' && ts[uOff] <= '9') || ts[uOff] == '.')) ++uOff;
            if (uOff > 0) ReaderText::ParseDoubleSv(std::string_view(ts).substr(0, uOff), num);
            std::string unit = ReaderText::ToLowerAscii(std::string_view(ts).substr(uOff));
            if (unit == "fs")      timescalePs = num * 1e-3;
            else if (unit == "ps") timescalePs = num;
            else if (unit == "ns") timescalePs = num * 1e3;
            else if (unit == "us") timescalePs = num * 1e6;
        } else if (tag == "CELL") {
            SdfCell cell;
            for (size_t ci = 1u; ci < sec.list.size(); ++ci) {
                const auto& sub = sec.list[ci];
                if (!sub.isList || sub.list.empty() || sub.list[0].isList) continue;
                const std::string sk = ReaderText::ToUpperAscii(sub.list[0].atom);
                if (sk == "CELLTYPE" && sub.list.size() >= 2u) {
                    cell.cellType = UnquoteSdf(sub.list[1].atom);
                } else if (sk == "INSTANCE") {
                    cell.instName = (sub.list.size() >= 2u && !sub.list[1].isList)
                        ? UnquoteSdf(sub.list[1].atom)
                        : "TOP";
                } else {
                    walkDelayStmt(walkDelayStmt, sub, cell);
                }
            }
            if (!cell.cellType.empty() || !cell.ioPaths.empty() || cell.timingChecks > 0u) {
                if (cell.instName.empty()) cell.instName = "U" + std::to_string(cells.size() + 1u);
                if (cells.size() < 128u) cells.push_back(std::move(cell));
            }
        }
    }

    // Ensure every instance referenced by `(INTERCONNECT srcInst/pin dstInst/pin)` has a placed cell card
    std::unordered_map<std::string, size_t> cellIdxByInst;
    for (size_t i = 0; i < cells.size(); ++i) {
        // Keep genuine logic cells over empty top-level INTERCONNECT wrapper cell (`INSTANCE` == "" or "TOP")
        if (!cells[i].ioPaths.empty() || cellIdxByInst.find(cells[i].instName) == cellIdxByInst.end()) {
            cellIdxByInst[cells[i].instName] = i;
        }
    }
    for (const auto& ic : interconnects) {
        for (const std::string& inst : { ic.srcInst, ic.dstInst }) {
            if (cellIdxByInst.find(inst) == cellIdxByInst.end() && cells.size() < 128u) {
                SdfCell synth;
                synth.instName = inst;
                synth.cellType = (inst == "TOP_IO") ? "IO_PAD" : "STD_CELL";
                cellIdxByInst[inst] = cells.size();
                cells.push_back(std::move(synth));
            }
        }
    }

    // Drop pure wrapper cells that only held `(INTERCONNECT ...)` and have 0 IOPATHs & 0 references
    std::set<size_t> refSet;
    for (const auto& ic : interconnects) {
        refSet.insert(cellIdxByInst[ic.srcInst]);
        refSet.insert(cellIdxByInst[ic.dstInst]);
    }
    std::vector<SdfCell> renderCells;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (!cells[i].ioPaths.empty() || cells[i].timingChecks > 0u || refSet.count(i) > 0u || cells.size() == 1u) {
            renderCells.push_back(std::move(cells[i]));
        }
    }
    if (renderCells.empty() && interconnects.empty()) return false;

    cellIdxByInst.clear();
    for (size_t i = 0; i < renderCells.size(); ++i) cellIdxByInst[renderCells[i].instName] = i;

    // Topological column rank from `(INTERCONNECT src -> dst)` edges
    const size_t nC = renderCells.size();
    std::vector<int> colRank(nC, 0);
    for (int iter = 0; iter < 12; ++iter) {
        for (const auto& ic : interconnects) {
            auto itS = cellIdxByInst.find(ic.srcInst);
            auto itD = cellIdxByInst.find(ic.dstInst);
            if (itS != cellIdxByInst.end() && itD != cellIdxByInst.end() && itS->second != itD->second) {
                colRank[itD->second] = std::max(colRank[itD->second], std::min(16, colRank[itS->second] + 1));
            }
        }
    }

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    struct PinPos { float x; float y; };
    std::unordered_map<std::string, PinPos> inPinMap, outPinMap;
    std::map<int, size_t> rowInCol;
    const float colPitch = 34.0f;
    const float rowPitch = 16.0f;

    size_t totalIoPaths = 0u;
    for (size_t ci = 0; ci < nC; ++ci) {
        const auto& c = renderCells[ci];
        const int col = interconnects.empty() ? static_cast<int>(ci % 3u) : colRank[ci];
        const size_t row = interconnects.empty() ? (ci / 3u) : rowInCol[col]++;
        const float cx = static_cast<float>(col) * colPitch;
        const float cy = -static_cast<float>(row) * rowPitch;

        // Collect unique input & output pin names on this cell
        std::vector<std::string> inPins, outPins;
        auto addUnique = [](std::vector<std::string>& vec, const std::string& p) {
            if (!p.empty() && std::find(vec.begin(), vec.end(), p) == vec.end()) vec.push_back(p);
        };
        for (const auto& io : c.ioPaths) {
            addUnique(inPins, io.inPin);
            if (!io.isPortDelay) addUnique(outPins, io.outPin);
        }
        for (const auto& ic : interconnects) {
            if (ic.dstInst == c.instName) addUnique(inPins, ic.dstPin);
            if (ic.srcInst == c.instName) addUnique(outPins, ic.srcPin);
        }

        const size_t maxSide = std::max<size_t>({ 1u, inPins.size(), outPins.size() });
        const float cw = 18.0f;
        const float ch = std::max(7.0f, static_cast<float>(maxSide) * 2.4f + 3.2f);

        DrawEdaBoxRect(out, cx - cw * 0.5f, cy - ch * 0.5f, cx + cw * 0.5f, cy + ch * 0.5f, 0.30f, EdaLayerId::SchSymbol, 0);
        out.labels.push_back({ cx - cw * 0.45f, cy + ch * 0.5f - 1.7f, 0.82f, EdaLayerId::SchLabel,
                               PathUtils::Utf8ToWide(c.instName + (c.cellType.empty() ? "" : (":" + c.cellType))) });

        for (size_t pi = 0; pi < inPins.size(); ++pi) {
            const float px = cx - cw * 0.5f;
            const float py = cy + ch * 0.5f - 3.4f - static_cast<float>(pi) * 2.3f;
            inPinMap[c.instName + "/" + inPins[pi]] = { px, py };
            out.pads.push_back({ px, py, 0.70f, 0.70f, 0.0f, 0.0f, 1, EdaLayerId::InnerCopper,
                                 PathUtils::Utf8ToWide(inPins[pi]), PathUtils::Utf8ToWide(c.instName) });
            out.labels.push_back({ px + 0.7f, py - 0.3f, 0.60f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(inPins[pi]) });
        }
        for (size_t po = 0; po < outPins.size(); ++po) {
            const float px = cx + cw * 0.5f;
            const float py = cy + ch * 0.5f - 3.4f - static_cast<float>(po) * 2.3f;
            outPinMap[c.instName + "/" + outPins[po]] = { px, py };
            out.pads.push_back({ px, py, 0.70f, 0.70f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper,
                                 PathUtils::Utf8ToWide(outPins[po]), PathUtils::Utf8ToWide(c.instName) });
            out.labels.push_back({ px - 2.6f, py - 0.3f, 0.60f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(outPins[po]) });
        }

        // Draw internal `(IOPATH inPin -> outPin)` and `(PORT pin)` timing arcs inside the cell!
        for (const auto& io : c.ioPaths) {
            ++totalIoPaths;
            auto itIn = inPinMap.find(c.instName + "/" + io.inPin);
            if (itIn == inPinMap.end()) continue;
            if (io.isPortDelay) {
                // Port parasitic delay stub on input pin
                out.traces.push_back({ itIn->second.x - 2.6f, itIn->second.y, itIn->second.x, itIn->second.y, 0.24f, EdaLayerId::BottomCopper, 1 });
                continue;
            }
            auto itOut = outPinMap.find(c.instName + "/" + io.outPin);
            if (itOut == outPinMap.end()) continue;
            const float x0 = itIn->second.x + 2.4f, y0 = itIn->second.y;
            const float x1 = itOut->second.x - 2.8f, y1 = itOut->second.y;
            out.traces.push_back({ x0, y0, x1, y1, 0.20f, EdaLayerId::TopCopper, static_cast<int>(ci + 1u) });
            // Arrowhead inside cell
            out.traces.push_back({ x1 - 0.8f, y1 + 0.4f, x1, y1, 0.20f, EdaLayerId::TopCopper, static_cast<int>(ci + 1u) });
            out.traces.push_back({ x1 - 0.8f, y1 - 0.4f, x1, y1, 0.20f, EdaLayerId::TopCopper, static_cast<int>(ci + 1u) });
            if (io.delayPs > 0.0 && out.labels.size() < 320u) {
                wchar_t dLbl[48] = {};
                swprintf_s(dLbl, L"%.0fps", io.delayPs);
                out.labels.push_back({ (x0 + x1) * 0.5f - 1.5f, (y0 + y1) * 0.5f + 0.3f, 0.56f, EdaLayerId::TopSilk, dLbl });
            }
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(c.instName);
        comp.value = PathUtils::Utf8ToWide(c.cellType);
        comp.footprint = L"SDF_TIMING_CELL";
        comp.x = cx; comp.y = cy;
        comp.widthMm = cw; comp.lengthMm = ch; comp.heightMm = 1.2f;
        comp.pinCount = static_cast<int>(inPins.size() + outPins.size());
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));
    }

    // Route inter-cell `(INTERCONNECT srcInst/srcPin -> dstInst/dstPin)` wires + arrowheads!
    for (size_t ii = 0; ii < interconnects.size(); ++ii) {
        const auto& ic = interconnects[ii];
        auto itS = outPinMap.find(ic.srcInst + "/" + ic.srcPin);
        auto itD = inPinMap.find(ic.dstInst + "/" + ic.dstPin);
        if (itS == outPinMap.end() || itD == inPinMap.end()) continue;

        const float sx = itS->second.x, sy = itS->second.y;
        const float dx = itD->second.x, dy = itD->second.y;
        const float midX = (sx + dx) * 0.5f + static_cast<float>((ii % 5u) - 2) * 0.85f;
        const int netId = static_cast<int>(100u + ii);

        out.traces.push_back({ sx, sy, midX, sy, 0.24f, EdaLayerId::SchWire, netId });
        out.traces.push_back({ midX, sy, midX, dy, 0.24f, EdaLayerId::SchWire, netId });
        out.traces.push_back({ midX, dy, dx, dy, 0.24f, EdaLayerId::SchWire, netId });
        // Directional timing-arc arrowhead into destination pin
        out.traces.push_back({ dx - 1.1f, dy + 0.55f, dx, dy, 0.24f, EdaLayerId::SchWire, netId });
        out.traces.push_back({ dx - 1.1f, dy - 0.55f, dx, dy, 0.24f, EdaLayerId::SchWire, netId });

        if (ic.delayPs > 0.0 && out.labels.size() < 360u) {
            wchar_t icLbl[48] = {};
            swprintf_s(icLbl, L"%.0fps", ic.delayPs);
            out.labels.push_back({ midX + 0.4f, (sy + dy) * 0.5f, 0.60f, EdaLayerId::BottomCopper, icLbl });
        }
    }

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"IEEE 1497 SDF Timing Graph (%ls%zu cells, %zu IOPATHs, %zu interconnects)",
               designName.empty() ? L"" : (PathUtils::Utf8ToWide(designName) + L" — ").c_str(),
               renderCells.size(), totalIoPaths, interconnects.size());
    out.labels.push_back({ -9.0f, 8.0f, 1.20f, EdaLayerId::TopSilk, titleBuf });
    out.formatTitle = titleBuf;
    out.netCount = std::max<size_t>(1u, totalIoPaths + interconnects.size());
    return !out.traces.empty() || !out.components.empty();
}

} // namespace Formats
