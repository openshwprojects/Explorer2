#pragma once
// Ieee1497Sdf.h — IEEE 1497 Standard Delay Format (.sdf) Timing Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// 5. IEEE 1497 Standard Delay Format (.sdf — (DELAYFILE ...))
inline bool IsLikelyIeeeSdfBytes(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    size_t scan = std::min<size_t>(size, 4096);
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == 0) return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    while (!sv.empty() && static_cast<unsigned char>(sv.front()) <= 0x20u) sv.remove_prefix(1);
    return sv.rfind("(DELAYFILE", 0) == 0 || sv.find("(DELAYFILE") != std::string_view::npos;
}

inline bool ParseIeee1497SdfBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (!IsLikelyIeeeSdfBytes(reinterpret_cast<const uint8_t*>(sv.data()), sv.size())) return false;

    std::vector<DetailSpecctra::SNode> roots;
    if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.empty()) return false;

    const DetailSpecctra::SNode* dfRoot = nullptr;
    for (const auto& r : roots) {
        if (DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(r)) == "delayfile") { dfRoot = &r; break; }
    }
    if (!dfRoot) return false;

    std::string designName;
    struct SdfArc { std::string fromPin, toPin; double dly = 0.0; };
    struct SdfCell {
        std::string cellType, instName;
        std::vector<SdfArc> iopaths;
        size_t timingChecks = 0;
    };
    std::vector<SdfCell> cells;
    std::vector<SdfArc> interconnects;

    auto extractTripleTyp = [](const DetailSpecctra::SNode& tripleList) -> double {
        if (!tripleList.isList || tripleList.list.empty()) return 0.0;
        const std::string& raw = tripleList.list[0].atom; // e.g. "0.12:0.15:0.19" or "0.15"
        size_t c1 = raw.find(':');
        if (c1 != std::string::npos) {
            size_t c2 = raw.find(':', c1 + 1);
            std::string mid = (c2 != std::string::npos) ? raw.substr(c1 + 1, c2 - c1 - 1) : raw.substr(c1 + 1);
            if (!mid.empty()) return std::strtod(mid.c_str(), nullptr);
        }
        return std::strtod(raw.c_str(), nullptr);
    };

    for (size_t i = 1; i < dfRoot->list.size(); ++i) {
        const auto& sec = dfRoot->list[i];
        std::string sk = DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(sec));
        if (sk == "design" && sec.list.size() >= 2) {
            designName = sec.list[1].atom;
        } else if (sk == "cell") {
            SdfCell cell;
            for (size_t ci = 1; ci < sec.list.size(); ++ci) {
                const auto& csub = sec.list[ci];
                std::string ck = DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(csub));
                if (ck == "celltype" && csub.list.size() >= 2) cell.cellType = csub.list[1].atom;
                else if (ck == "instance" && csub.list.size() >= 2) cell.instName = csub.list[1].atom;
                else if (ck == "timingcheck") cell.timingChecks += csub.list.size() - 1;
                else if (ck == "delay") {
                    for (size_t di = 1; di < csub.list.size(); ++di) {
                        const auto& absNode = csub.list[di];
                        std::string ak = DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(absNode));
                        if (ak == "absolute" || ak == "increment") {
                            for (size_t pi = 1; pi < absNode.list.size(); ++pi) {
                                const auto& pNode = absNode.list[pi];
                                std::string pk = DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(pNode));
                                if (pk == "iopath" && pNode.list.size() >= 4) {
                                    std::string inP = pNode.list[1].isList ? DetailGateTdms::EdifIdName(pNode.list[1]) : pNode.list[1].atom;
                                    std::string outP = pNode.list[2].isList ? DetailGateTdms::EdifIdName(pNode.list[2]) : pNode.list[2].atom;
                                    double d = extractTripleTyp(pNode.list[3]);
                                    cell.iopaths.push_back({ inP, outP, d });
                                } else if (pk == "interconnect" && pNode.list.size() >= 4) {
                                    interconnects.push_back({ pNode.list[1].atom, pNode.list[2].atom, extractTripleTyp(pNode.list[3]) });
                                }
                            }
                        }
                    }
                }
            }
            if (!cell.cellType.empty() || !cell.iopaths.empty()) cells.push_back(std::move(cell));
        }
    }

    if (cells.empty() && interconnects.empty()) return false;

    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"IEEE 1497 Standard Delay Format (.sdf";
    if (!designName.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(designName);
    out.formatTitle += L")";

    float curX = 0.0f;
    for (size_t i = 0; i < cells.size() && i < 128; ++i) {
        const auto& c = cells[i];
        if (c.iopaths.empty() && c.instName.empty() && !interconnects.empty()) continue;
        float boxW = 14.0f, boxH = std::max(7.0f, static_cast<float>(c.iopaths.size()) * 3.0f + 3.0f);
        float cx = curX + boxW * 0.5f, cy = 0.0f;
        out.traces.push_back({ curX,        cy - boxH * 0.5f, curX + boxW, cy - boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX + boxW, cy - boxH * 0.5f, curX + boxW, cy + boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX + boxW, cy + boxH * 0.5f, curX,        cy + boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX,        cy + boxH * 0.5f, curX,        cy - boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });

        std::string instTitle = (c.instName.empty() ? ("U" + std::to_string(i + 1)) : c.instName) + " (" + c.cellType + ")";
        out.labels.push_back({ curX, cy + boxH * 0.5f + 0.9f, 0.95f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(instTitle) });

        for (size_t k = 0; k < c.iopaths.size(); ++k) {
            float py = cy + boxH * 0.3f - static_cast<float>(k) * 2.8f;
            out.traces.push_back({ curX - 2.0f, py, curX + boxW + 2.0f, py, 0.24f, EdaLayerId::TopCopper, static_cast<int>(k + 1) });
            out.pads.push_back({ curX - 2.0f, py, 0.6f, 0.6f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(c.iopaths[k].fromPin), L"" });
            out.pads.push_back({ curX + boxW + 2.0f, py, 0.6f, 0.6f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(c.iopaths[k].toPin), L"" });
            wchar_t arcLbl[96] = {};
            swprintf_s(arcLbl, L"%hs->%hs: %.3f ns", c.iopaths[k].fromPin.c_str(), c.iopaths[k].toPin.c_str(), c.iopaths[k].dly);
            out.labels.push_back({ curX + 0.8f, py + 0.3f, 0.78f, EdaLayerId::TopSilk, arcLbl });
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(c.instName.empty() ? c.cellType : c.instName);
        comp.value = PathUtils::Utf8ToWide(c.cellType);
        comp.footprint = L"SDF_CELL";
        comp.x = cx; comp.y = cy;
        comp.widthMm = boxW; comp.lengthMm = boxH;
        comp.pinCount = std::max(2, static_cast<int>(c.iopaths.size() * 2));
        out.components.push_back(std::move(comp));
        curX += 22.0f;
    }

    // Also render INTERCONNECT delay bars below cells
    float iy = -10.0f;
    for (size_t i = 0; i < interconnects.size() && i < 32; ++i) {
        float w = std::clamp(static_cast<float>(interconnects[i].dly) * 30.0f, 3.0f, 40.0f);
        out.traces.push_back({ 0.0f, iy, w, iy, 0.32f, EdaLayerId::SchWire, static_cast<int>(i + 1) });
        wchar_t icLbl[160] = {};
        swprintf_s(icLbl, L"NET %hs -> %hs (%.3f ns)", interconnects[i].fromPin.c_str(), interconnects[i].toPin.c_str(), interconnects[i].dly);
        out.labels.push_back({ w + 1.0f, iy - 0.3f, 0.82f, EdaLayerId::SchLabel, icLbl });
        iy -= 2.8f;
    }

    out.netCount = std::max<size_t>(1, interconnects.size() + cells.size());
    return !out.traces.empty();
}

} // namespace Formats
