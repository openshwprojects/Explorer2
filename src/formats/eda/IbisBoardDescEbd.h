#pragma once
// IbisBoardDescEbd.h — IBIS Electronic Board Description Multi-Die SI Topology (.ebd) Reader
// Parses `[Begin Board Description]` .. `[End Board Description]`, `[Reference Designator Map]`,
// and `[Path Description]` (`Pin`, `Len=... L=... R=... C=...` electrical transmission-line segments,
// nested `Fork`..`Endfork` tree branches, and `Node <ref>.<pin>` IC/connector endpoints).
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailIbisEbd {

struct EbdSegment {
    int parentNodeIdx = 0;
    int childNodeIdx  = 0;
    double lenVal = 0.0;
    double lVal   = 0.0;
    double rVal   = 0.0;
    double cVal   = 0.0;
    bool hasL = false, hasR = false, hasC = false;
};

struct EbdTreePoint {
    std::string endpointNode; // e.g. "U2.A4" or "J1.12" if `Node` attached here
    bool isForkJunction = false;
};

struct EbdPath {
    std::string name;
    std::string startPin;
    std::vector<EbdTreePoint> points;
    std::vector<EbdSegment> segments;
    size_t forkCount = 0u;
    double totalLen = 0.0;
};

inline double ParseSiNumber(std::string_view s) {
    s = ReaderText::TrimSv(s);
    if (s.empty()) return 0.0;
    size_t numEnd = 0;
    while (numEnd < s.size() &&
           ((s[numEnd] >= '0' && s[numEnd] <= '9') || s[numEnd] == '.' ||
            s[numEnd] == '+' || s[numEnd] == '-' || s[numEnd] == 'e' || s[numEnd] == 'E')) {
        ++numEnd;
    }
    double v = 0.0;
    if (numEnd == 0 || !ReaderText::ParseDoubleSv(s.substr(0, numEnd), v)) return 0.0;
    std::string_view suf = ReaderText::TrimSv(s.substr(numEnd));
    if (!suf.empty()) {
        const char u = suf.front();
        if (u == 'f' || u == 'F') v *= 1e-15;
        else if (u == 'p' || u == 'P') v *= 1e-12;
        else if (u == 'n' || u == 'N') v *= 1e-9;
        else if (u == 'u' || u == 'U') v *= 1e-6;
        else if (u == 'm')             v *= 1e-3;
        else if (u == 'k' || u == 'K') v *= 1e3;
        else if (u == 'M')             v *= 1e6;
        else if (u == 'G' || u == 'g') v *= 1e9;
    }
    return v;
}

} // namespace DetailIbisEbd

inline bool ParseIbisBoardDescEbd(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    using namespace DetailIbisEbd;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 28u, (16u << 20))) return false;

    bool sawBeginBoard = false;
    bool sawEndBoard   = false;
    std::string boardName;
    std::string manufacturer;
    std::string curSection;

    // `[Reference Designator Map]` -> `<refDes> -> <modelFile (componentName)>`
    struct RefDesInfo {
        std::string fileName;
        std::string compName;
    };
    std::unordered_map<std::string, RefDesInfo> refDesMap;
    std::vector<EbdPath> paths;
    std::vector<int> forkStack;
    int curTreeNode = 0;

    // Accumulate pending `Len=... L=... R=... C=...` parameters across `/` slash-delimited tokens on a line
    auto flushSegmentIfAny = [&](EbdPath& ep, bool& hasSeg, EbdSegment& seg) {
        if (!hasSeg) return;
        const int newIdx = static_cast<int>(ep.points.size());
        ep.points.push_back(EbdTreePoint{});
        seg.parentNodeIdx = curTreeNode;
        seg.childNodeIdx  = newIdx;
        ep.totalLen += seg.lenVal;
        ep.segments.push_back(seg);
        curTreeNode = newIdx;
        hasSeg = false;
        seg = EbdSegment{};
    };

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        const size_t bar = rawLine.find('|');
        if (bar != std::string_view::npos) rawLine = rawLine.substr(0u, bar);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (line.front() == '[') {
            const size_t rb = line.find(']');
            if (rb == std::string_view::npos) return false;
            std::string sec = ReaderText::ToUpperAscii(ReaderText::TrimSv(line.substr(1u, rb - 1u)));
            std::string_view arg = ReaderText::TrimSv(line.substr(rb + 1u));
            curSection = sec;

            if (sec == "BEGIN BOARD DESCRIPTION") {
                sawBeginBoard = true;
            } else if (sec == "END BOARD DESCRIPTION") {
                if (!sawBeginBoard || !forkStack.empty()) return false;
                sawEndBoard = true;
                break;
            } else if (sec == "BOARD NAME") {
                boardName.assign(arg);
            } else if (sec == "MANUFACTURER") {
                manufacturer.assign(arg);
            } else if (sec == "PATH DESCRIPTION") {
                if (!forkStack.empty()) return false;
                EbdPath ep{};
                ep.name.assign(arg);
                ep.points.push_back(EbdTreePoint{}); // root node 0 (driver `Pin`)
                curTreeNode = 0;
                paths.push_back(std::move(ep));
            }
            continue;
        }

        if (curSection == "REFERENCE DESIGNATOR MAP") {
            std::string_view rem = line, rd, fTok, cTok;
            if (ReaderText::NextTokenSv(rem, rd) && ReaderText::NextTokenSv(rem, fTok)) {
                RefDesInfo ri{ std::string(fTok), {} };
                if (ReaderText::NextTokenSv(rem, cTok)) ri.compName = std::string(cTok);
                refDesMap[ReaderText::ToUpperAscii(rd)] = std::move(ri);
            }
        } else if (curSection == "PATH DESCRIPTION" && !paths.empty()) {
            EbdPath& ep = paths.back();
            bool hasPendingSeg = false;
            EbdSegment pendingSeg{};

            std::string_view rest = line, tok;
            while (ReaderText::NextTokenSv(rest, tok)) {
                if (tok == "/") {
                    flushSegmentIfAny(ep, hasPendingSeg, pendingSeg);
                    continue;
                }
                const std::string up = ReaderText::ToUpperAscii(tok);
                if (up == "PIN") {
                    flushSegmentIfAny(ep, hasPendingSeg, pendingSeg);
                    std::string_view pTok;
                    if (!ReaderText::NextTokenSv(rest, pTok)) return false;
                    ep.startPin.assign(pTok);
                    curTreeNode = 0;
                } else if (up == "NODE") {
                    flushSegmentIfAny(ep, hasPendingSeg, pendingSeg);
                    std::string_view nTok;
                    if (!ReaderText::NextTokenSv(rest, nTok)) return false;
                    if (curTreeNode >= 0 && static_cast<size_t>(curTreeNode) < ep.points.size()) {
                        if (ep.points[static_cast<size_t>(curTreeNode)].endpointNode.empty()) {
                            ep.points[static_cast<size_t>(curTreeNode)].endpointNode.assign(nTok);
                        } else {
                            // Multiple nodes at same junction -> create zero-length stub leaf
                            const int leaf = static_cast<int>(ep.points.size());
                            ep.points.push_back({ std::string(nTok), false });
                            ep.segments.push_back({ curTreeNode, leaf, 0.0, 0.0, 0.0, 0.0, false, false, false });
                        }
                    }
                } else if (up == "FORK") {
                    flushSegmentIfAny(ep, hasPendingSeg, pendingSeg);
                    if (curTreeNode >= 0 && static_cast<size_t>(curTreeNode) < ep.points.size()) {
                        ep.points[static_cast<size_t>(curTreeNode)].isForkJunction = true;
                    }
                    forkStack.push_back(curTreeNode);
                    ++ep.forkCount;
                } else if (up == "ENDFORK") {
                    flushSegmentIfAny(ep, hasPendingSeg, pendingSeg);
                    if (forkStack.empty()) return false;
                    curTreeNode = forkStack.back();
                    forkStack.pop_back();
                } else {
                    const size_t eq = tok.find('=');
                    if (eq != std::string_view::npos) {
                        const std::string pKey = ReaderText::ToUpperAscii(tok.substr(0, eq));
                        std::string_view valSv = tok.substr(eq + 1u);
                        if (valSv.empty()) ReaderText::NextTokenSv(rest, valSv);
                        const double dv = ParseSiNumber(valSv);
                        if (pKey == "LEN") {
                            if (hasPendingSeg && pendingSeg.lenVal > 0.0) {
                                flushSegmentIfAny(ep, hasPendingSeg, pendingSeg);
                            }
                            pendingSeg.lenVal = std::max(0.0, dv);
                            hasPendingSeg = true;
                        } else if (pKey == "L") {
                            pendingSeg.lVal = dv; pendingSeg.hasL = true; hasPendingSeg = true;
                        } else if (pKey == "R") {
                            pendingSeg.rVal = dv; pendingSeg.hasR = true; hasPendingSeg = true;
                        } else if (pKey == "C") {
                            pendingSeg.cVal = dv; pendingSeg.hasC = true; hasPendingSeg = true;
                        }
                    }
                }
            }
            flushSegmentIfAny(ep, hasPendingSeg, pendingSeg);
        }
    }

    if (!sawBeginBoard || !sawEndBoard || !forkStack.empty() || paths.empty()) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadPcb;

    auto lookupRefModel = [&](std::string_view nodePin, std::string& outRef, std::string& outPin, std::string& outModel) {
        const size_t dot = nodePin.find('.');
        if (dot != std::string_view::npos) {
            outRef.assign(nodePin.substr(0, dot));
            outPin.assign(nodePin.substr(dot + 1u));
        } else {
            outRef = "BGA";
            outPin.assign(nodePin);
        }
        auto it = refDesMap.find(ReaderText::ToUpperAscii(outRef));
        if (it != refDesMap.end()) {
            outModel = it->second.compName.empty() ? it->second.fileName : it->second.compName;
        }
    };

    const size_t nShowPaths = std::min<size_t>(paths.size(), 24u);
    float basePathY = 0.0f;
    size_t placedComponents = 0u;

    for (size_t pi = 0; pi < nShowPaths; ++pi) {
        const EbdPath& ep = paths[pi];
        const size_t nNodes = ep.points.size();
        if (nNodes == 0u) continue;

        // Build adjacency list from parsed `Fork..Endfork` tree segments!
        std::vector<std::vector<size_t>> adj(nNodes);
        for (size_t si = 0; si < ep.segments.size(); ++si) {
            const int u = ep.segments[si].parentNodeIdx;
            if (u >= 0 && static_cast<size_t>(u) < nNodes) {
                adj[static_cast<size_t>(u)].push_back(si);
            }
        }

        // Compute recursive leaf count per node so `Fork` branches fan out vertically without overlap!
        std::vector<int> leafWeight(nNodes, 1);
        auto calcLeaves = [&](auto& self, int u) -> int {
            const auto& outs = adj[static_cast<size_t>(u)];
            if (outs.empty()) { leafWeight[static_cast<size_t>(u)] = 1; return 1; }
            int sum = 0;
            for (size_t si : outs) sum += self(self, ep.segments[si].childNodeIdx);
            leafWeight[static_cast<size_t>(u)] = std::max(1, sum);
            return leafWeight[static_cast<size_t>(u)];
        };
        const int totalLeaves = calcLeaves(calcLeaves, 0);
        const float branchPitchY = 6.5f;
        const float pathSpanH = static_cast<float>(std::max(1, totalLeaves)) * branchPitchY;

        std::vector<std::pair<float, float>> coords(nNodes, { 0.0f, basePathY });
        const int netId = static_cast<int>(pi + 1u);

        // Place root driver pad & component on the left (`x = 0`)
        {
            std::string dRef, dPin, dMdl;
            lookupRefModel(ep.startPin.empty() ? "J_BOARD.1" : ep.startPin, dRef, dPin, dMdl);
            const float rx = 0.0f, ry = basePathY;
            coords[0] = { rx, ry };

            DrawEdaBoxRect(out, rx - 8.5f, ry - 2.4f, rx - 0.8f, ry + 2.4f, 0.28f, EdaLayerId::TopSilk, 0);
            out.pads.push_back({
                rx, ry, 1.1f, 1.1f, 0.0f, 0.0f, 0, EdaLayerId::TopCopper,
                PathUtils::Utf8ToWide(dPin), PathUtils::Utf8ToWide(ep.name)
            });
            std::wstring drvTxt = PathUtils::Utf8ToWide(dRef + "." + dPin);
            if (!ep.name.empty()) drvTxt = PathUtils::Utf8ToWide(ep.name) + L" (" + drvTxt + L")";
            out.labels.push_back({ rx - 8.0f, ry + 2.8f, 0.85f, EdaLayerId::TopSilk, drvTxt });

            EdaComponent3D dc;
            dc.refDes = PathUtils::Utf8ToWide(dRef + "_" + dPin);
            dc.value = PathUtils::Utf8ToWide(dMdl.empty() ? ep.name : dMdl);
            dc.footprint = L"EBD_DRIVER";
            dc.x = rx - 4.6f; dc.y = ry;
            dc.widthMm = 7.7f; dc.lengthMm = 4.8f; dc.heightMm = 1.2f;
            dc.pinCount = 2;
            dc.category = EdaPartCategory::Connector;
            out.components.push_back(std::move(dc));
            ++placedComponents;
        }

        // Recursive tree layout & transmission-line segment drawing
        auto layoutSubtree = [&](auto& self, int u, float yCenter) -> void {
            const float ux = coords[static_cast<size_t>(u)].first;
            const float uy = coords[static_cast<size_t>(u)].second;
            const auto& outs = adj[static_cast<size_t>(u)];

            if (ep.points[static_cast<size_t>(u)].isForkJunction && outs.size() >= 2u) {
                out.vias.push_back({ ux, uy, 0.95f, 0.45f, netId });
            }

            // Endpoint IC / passive component if `Node <ref>.<pin>` was declared at `u`
            if (!ep.points[static_cast<size_t>(u)].endpointNode.empty()) {
                std::string nRef, nPin, nMdl;
                lookupRefModel(ep.points[static_cast<size_t>(u)].endpointNode, nRef, nPin, nMdl);
                out.pads.push_back({
                    ux, uy, 1.0f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper,
                    PathUtils::Utf8ToWide(nPin), PathUtils::Utf8ToWide(ep.name)
                });
                const float icX0 = ux + 0.8f, icX1 = ux + 9.5f;
                DrawEdaBoxRect(out, icX0, uy - 2.2f, icX1, uy + 2.2f, 0.26f, EdaLayerId::TopSilk, 0);
                std::wstring endLbl = PathUtils::Utf8ToWide(nRef + "." + nPin + (nMdl.empty() ? "" : (" [" + nMdl + "]")));
                out.labels.push_back({ icX0 + 0.5f, uy - 0.35f, 0.72f, EdaLayerId::SchLabel, endLbl });

                if (placedComponents < 128u) {
                    EdaComponent3D ec;
                    ec.refDes = PathUtils::Utf8ToWide(nRef + "_" + nPin);
                    ec.value = PathUtils::Utf8ToWide(nMdl.empty() ? "EBD_NODE" : nMdl);
                    ec.footprint = L"EBD_ENDPOINT";
                    ec.x = (icX0 + icX1) * 0.5f; ec.y = uy;
                    ec.widthMm = icX1 - icX0; ec.lengthMm = 4.4f; ec.heightMm = 1.3f;
                    ec.pinCount = 2;
                    ec.category = ClassifyComponent(PathUtils::Utf8ToWide(nRef), ec.footprint, ec.widthMm, ec.lengthMm, ec.heightMm, ec.pinCount);
                    out.components.push_back(std::move(ec));
                    ++placedComponents;
                }
            }

            if (outs.empty()) return;

            const float totalH = static_cast<float>(leafWeight[static_cast<size_t>(u)]) * branchPitchY;
            float curTop = yCenter + totalH * 0.5f;

            for (size_t si : outs) {
                const EbdSegment& seg = ep.segments[si];
                const int v = seg.childNodeIdx;
                const float childH = static_cast<float>(leafWeight[static_cast<size_t>(v)]) * branchPitchY;
                const float vy = (outs.size() == 1u) ? uy : (curTop - childH * 0.5f);
                curTop -= childH;

                // Horizontal length proportional to `Len=` (clamped 10..36 mm so tiny & long segments are readable)
                const float segDx = (seg.lenVal > 0.0)
                    ? std::clamp(8.0f + static_cast<float>(seg.lenVal) * (seg.lenVal < 0.2 ? 400.0f : 12.0f), 10.0f, 36.0f)
                    : 8.0f;
                const float vx = ux + segDx;
                coords[static_cast<size_t>(v)] = { vx, vy };

                // Orthogonal fork jog + transmission line segment (thicker microstrip for controlled-Z L/C segments)
                const bool isTline = (seg.hasL && seg.hasC && seg.cVal > 0.0);
                const EdaLayerId lyr = isTline ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper;
                const float traceW = isTline ? 0.42f : 0.28f;

                if (std::fabs(vy - uy) > 1e-3f) {
                    out.traces.push_back({ ux, uy, ux, vy, 0.26f, lyr, netId });
                }
                out.traces.push_back({ ux, vy, vx, vy, traceW, lyr, netId });

                // Annotate parsed `Len` / `Z0 = sqrt(L/C)` / `R` electrical parameters!
                wchar_t rlgcLbl[128] = {};
                if (isTline && seg.lVal > 0.0) {
                    const double z0 = std::sqrt(seg.lVal / seg.cVal);
                    swprintf_s(rlgcLbl, L"Len=%.3g Z0=%.1f\x03A9", seg.lenVal, z0);
                } else if (seg.hasR || seg.hasC || seg.hasL) {
                    swprintf_s(rlgcLbl, L"R=%.2g L=%.2g C=%.2g", seg.rVal, seg.lVal, seg.cVal);
                } else if (seg.lenVal > 0.0) {
                    swprintf_s(rlgcLbl, L"Len=%.3g", seg.lenVal);
                }
                if (rlgcLbl[0] != L'\0' && out.labels.size() < 320u) {
                    out.labels.push_back({ (ux + vx) * 0.5f - 3.5f, vy + 0.65f, 0.62f, EdaLayerId::SchLabel, rlgcLbl });
                }

                self(self, v, vy);
            }
        };

        layoutSubtree(layoutSubtree, 0, basePathY);
        basePathY -= (pathSpanH + 8.5f);
    }

    const std::wstring wBoard = PathUtils::Utf8ToWide(boardName.empty() ? "IBIS_EBD" : boardName);
    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"IBIS Board Description (.ebd — %ls, %zu paths, %zu ICs)",
               wBoard.c_str(), paths.size(), refDesMap.size());
    out.labels.push_back({ -8.5f, 8.5f, 1.25f, EdaLayerId::TopSilk, titleBuf });
    out.formatTitle = titleBuf;
    out.netCount = paths.size();
    FinalizeBoardBounds(out);
    out.valid = true;
    return !out.traces.empty();
}

} // namespace Formats
