#pragma once
// IspdBookshelfPlc.h — UCLA / ISPD GSRC VLSI Standard-Cell & Macro Placement Benchmark (.scl / .nodes) Reader
// Parses `UCLA scl 1.0` (`CoreRow Horizontal`, `Coordinate`, `Height`, `Sitewidth`, `Sitespacing`,
// `SubrowOrigin`, `NumSites`, `End`) and `UCLA nodes 1.0` (`NumNodes`, `NumTerminals`, `<name> <w> <h> [terminal]`),
// automatically resolving companion `<stem>.scl`, `<stem>.nodes`, `<stem>.pl` (`UCLA pl 1.0` cell coordinates &
// orientations), and `<stem>.nets` (`UCLA nets 1.0` `NetDegree : <k>` pin lists with `%x %y` pin offsets)
// from the same directory to render a full VLSI floorplan with standard cells, fixed macros, IO pads, and
// half-perimeter / Steiner net routing!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailIspdBookshelf {

struct SclRow {
    double yCoord = 0.0;
    double height = 8.0;
    double siteWidth = 1.0;
    double siteSpacing = 1.0;
    double subrowOrigin = 0.0;
    uint32_t numSites = 0u;
};

struct BookshelfCell {
    std::string name;
    double w = 1.0;
    double h = 8.0;
    double x = 0.0;
    double y = 0.0;
    std::string orient = "N";
    bool isTerminal = false; // macro or fixed I/O pad
    bool isTerminalNi = false;
    bool hasPlacement = false;
};

struct BookshelfPin {
    std::string cellName;
    char dir = 'B'; // I, O, B
    double dx = 0.0; // offset from cell center
    double dy = 0.0;
};

struct BookshelfNet {
    std::string name;
    std::vector<BookshelfPin> pins;
};

inline bool ParseSclText(std::string_view sv, std::vector<SclRow>& outRows) {
    outRows.clear();
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    bool sawHeader = false;
    bool inRow = false;
    SclRow cur{};

    size_t pos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        const size_t hp = rawLine.find('#');
        if (hp != std::string_view::npos) rawLine = rawLine.substr(0u, hp);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (!sawHeader) {
            if (ReaderText::StartsWithCI(line, "UCLA scl")) {
                sawHeader = true;
                continue;
            }
            return false;
        }

        // Normalize `:` to space so `Coordinate : 12` and `SubrowOrigin : 0 NumSites : 200` tokenize uniformly
        std::string norm;
        norm.reserve(line.size());
        for (char c : line) norm.push_back(c == ':' ? ' ' : c);

        std::string_view rem(norm), tok;
        while (ReaderText::NextTokenSv(rem, tok)) {
            if (ReaderText::AsciiEqCI(tok, "NumRows")) {
                std::string_view v;
                ReaderText::NextTokenSv(rem, v);
            } else if (ReaderText::AsciiEqCI(tok, "CoreRow")) {
                std::string_view dir;
                ReaderText::NextTokenSv(rem, dir);
                cur = SclRow{};
                inRow = true;
            } else if (inRow && ReaderText::AsciiEqCI(tok, "Coordinate")) {
                std::string_view v;
                if (ReaderText::NextTokenSv(rem, v)) ReaderText::ParseDoubleSv(v, cur.yCoord);
            } else if (inRow && ReaderText::AsciiEqCI(tok, "Height")) {
                std::string_view v;
                if (ReaderText::NextTokenSv(rem, v)) ReaderText::ParseDoubleSv(v, cur.height);
            } else if (inRow && ReaderText::AsciiEqCI(tok, "Sitewidth")) {
                std::string_view v;
                if (ReaderText::NextTokenSv(rem, v)) ReaderText::ParseDoubleSv(v, cur.siteWidth);
            } else if (inRow && ReaderText::AsciiEqCI(tok, "Sitespacing")) {
                std::string_view v;
                if (ReaderText::NextTokenSv(rem, v)) ReaderText::ParseDoubleSv(v, cur.siteSpacing);
            } else if (inRow && ReaderText::AsciiEqCI(tok, "SubrowOrigin")) {
                std::string_view v;
                if (ReaderText::NextTokenSv(rem, v)) ReaderText::ParseDoubleSv(v, cur.subrowOrigin);
            } else if (inRow && ReaderText::AsciiEqCI(tok, "NumSites")) {
                std::string_view v;
                uint64_t ns = 0;
                if (ReaderText::NextTokenSv(rem, v) && ReaderText::ParseUint64Sv(v, ns, 10)) {
                    cur.numSites = static_cast<uint32_t>(ns);
                }
            } else if (inRow && ReaderText::AsciiEqCI(tok, "End")) {
                if (cur.numSites > 0u && cur.height > 0.0 && outRows.size() < 8192u) {
                    outRows.push_back(cur);
                }
                inRow = false;
            }
        }
    }

    return sawHeader && !outRows.empty();
}

inline bool ParseNodesText(
    std::string_view sv,
    std::vector<BookshelfCell>& outCells,
    std::unordered_map<std::string, size_t>& outIdxMap)
{
    outCells.clear();
    outIdxMap.clear();
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    bool sawHeader = false;
    size_t pos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        const size_t hp = rawLine.find('#');
        if (hp != std::string_view::npos) rawLine = rawLine.substr(0u, hp);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (!sawHeader) {
            if (ReaderText::StartsWithCI(line, "UCLA nodes")) {
                sawHeader = true;
                continue;
            }
            return false;
        }

        if (ReaderText::StartsWithCI(line, "NumNodes") || ReaderText::StartsWithCI(line, "NumTerminals")) {
            continue;
        }

        std::string_view rem = line, tName, tW, tH, tKind;
        double w = 0.0, h = 0.0;
        if (!ReaderText::NextTokenSv(rem, tName) ||
            !ReaderText::NextTokenSv(rem, tW) ||
            !ReaderText::NextTokenSv(rem, tH) ||
            !ReaderText::ParseDoubleSv(tW, w) ||
            !ReaderText::ParseDoubleSv(tH, h)) {
            return false;
        }
        BookshelfCell c;
        c.name = std::string(tName);
        c.w = std::max(0.1, w);
        c.h = std::max(0.0, h);
        if (ReaderText::NextTokenSv(rem, tKind)) {
            if (ReaderText::AsciiEqCI(tKind, "terminal")) c.isTerminal = true;
            else if (ReaderText::AsciiEqCI(tKind, "terminal_NI")) { c.isTerminal = true; c.isTerminalNi = true; }
        }
        if (outCells.size() < 100000u) {
            outIdxMap[c.name] = outCells.size();
            outCells.push_back(std::move(c));
        }
    }

    return sawHeader && !outCells.empty();
}

inline void ParsePlText(
    std::string_view sv,
    std::vector<BookshelfCell>& cells,
    const std::unordered_map<std::string, size_t>& idxMap)
{
    sv = ReaderText::StripUtf8Bom(sv);
    if (!ReaderText::StartsWithCI(ReaderText::TrimSv(sv), "UCLA pl")) return;

    size_t pos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        const size_t hp = rawLine.find('#');
        if (hp != std::string_view::npos) rawLine = rawLine.substr(0u, hp);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || ReaderText::StartsWithCI(line, "UCLA pl")) continue;

        std::string_view rem = line, tName, tX, tY, tColon, tOrient;
        double x = 0.0, y = 0.0;
        if (ReaderText::NextTokenSv(rem, tName) &&
            ReaderText::NextTokenSv(rem, tX) &&
            ReaderText::NextTokenSv(rem, tY) &&
            ReaderText::ParseDoubleSv(tX, x) &&
            ReaderText::ParseDoubleSv(tY, y))
        {
            auto it = idxMap.find(std::string(tName));
            if (it != idxMap.end()) {
                BookshelfCell& c = cells[it->second];
                c.x = x;
                c.y = y;
                c.hasPlacement = true;
                if (ReaderText::NextTokenSv(rem, tColon)) {
                    if (tColon == ":" && ReaderText::NextTokenSv(rem, tOrient)) c.orient = std::string(tOrient);
                    else if (tColon != ":") c.orient = std::string(tColon);
                }
            }
        }
    }
}

inline void ParseNetsText(std::string_view sv, std::vector<BookshelfNet>& outNets) {
    outNets.clear();
    sv = ReaderText::StripUtf8Bom(sv);
    if (!ReaderText::StartsWithCI(ReaderText::TrimSv(sv), "UCLA nets")) return;

    size_t pos = 0u;
    std::string_view rawLine;
    int remainingPins = 0;

    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        const size_t hp = rawLine.find('#');
        if (hp != std::string_view::npos) rawLine = rawLine.substr(0u, hp);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || ReaderText::StartsWithCI(line, "UCLA nets") ||
            ReaderText::StartsWithCI(line, "NumNets") || ReaderText::StartsWithCI(line, "NumPins")) {
            continue;
        }

        if (ReaderText::StartsWithCI(line, "NetDegree")) {
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos) continue;
            std::string_view rhs = ReaderText::TrimSv(line.substr(colon + 1u));
            std::string_view degTok, netNm;
            int deg = 0;
            if (ReaderText::NextTokenSv(rhs, degTok) && ReaderText::ParseIntSv(degTok, deg) && deg > 0) {
                remainingPins = deg;
                BookshelfNet net;
                if (ReaderText::NextTokenSv(rhs, netNm)) net.name = std::string(netNm);
                else net.name = "n" + std::to_string(outNets.size());
                if (outNets.size() < 32768u) outNets.push_back(std::move(net));
            }
            continue;
        }

        if (remainingPins > 0 && !outNets.empty()) {
            // `<cellName> <I|O|B> [: <dx> <dy>]`
            std::string_view rem = line, cTok, dTok;
            if (ReaderText::NextTokenSv(rem, cTok)) {
                BookshelfPin pin;
                pin.cellName = std::string(cTok);
                if (ReaderText::NextTokenSv(rem, dTok) && !dTok.empty()) pin.dir = dTok[0];
                const size_t colon = rem.find(':');
                if (colon != std::string_view::npos) {
                    std::string_view offRem = rem.substr(colon + 1u), tx, ty;
                    if (ReaderText::NextTokenSv(offRem, tx) && ReaderText::NextTokenSv(offRem, ty)) {
                        ReaderText::ParseDoubleSv(tx, pin.dx);
                        ReaderText::ParseDoubleSv(ty, pin.dy);
                    }
                }
                outNets.back().pins.push_back(std::move(pin));
                --remainingPins;
            }
        }
    }
}

} // namespace DetailIspdBookshelf

inline bool ParseIspdBookshelfPlc(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailIspdBookshelf;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 14u, (32u << 20))) return false;

    std::vector<SclRow> rows;
    std::vector<BookshelfCell> cells;
    std::unordered_map<std::string, size_t> cellIdxMap;
    std::vector<BookshelfNet> nets;

    const std::wstring ext = PathUtils::GetExtension(fullPath);
    const bool primaryIsScl = (ext == L".scl");
    const bool primaryIsNodes = (ext == L".nodes");

    if (primaryIsScl) {
        if (!ParseSclText(sv, rows)) return false;
    } else if (primaryIsNodes) {
        if (!ParseNodesText(sv, cells, cellIdxMap)) return false;
    } else {
        if (!ParseSclText(sv, rows) && !ParseNodesText(sv, cells, cellIdxMap)) return false;
    }

    // Resolve companion `.scl`, `.nodes`, `.pl`, and `.nets` files sharing the same benchmark stem!
    std::wstring stemPath;
    if (!fullPath.empty()) {
        const size_t dot = fullPath.find_last_of(L'.');
        const size_t sep = fullPath.find_last_of(L"\\/");
        if (dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep)) {
            stemPath = fullPath.substr(0u, dot);
        }
    }

    if (!stemPath.empty()) {
        auto tryReadCompanion = [&](const wchar_t* compExt, std::vector<uint8_t>& outBytes) -> bool {
            return PathUtils::ReadFileBytes(stemPath + compExt, 32u * 1024u * 1024u, outBytes) && !outBytes.empty();
        };

        std::vector<uint8_t> compBuf;
        if (rows.empty() && tryReadCompanion(L".scl", compBuf)) {
            ParseSclText(std::string_view(reinterpret_cast<const char*>(compBuf.data()), compBuf.size()), rows);
        }
        if (cells.empty() && tryReadCompanion(L".nodes", compBuf)) {
            ParseNodesText(std::string_view(reinterpret_cast<const char*>(compBuf.data()), compBuf.size()), cells, cellIdxMap);
        }
        if (!cells.empty() && tryReadCompanion(L".pl", compBuf)) {
            ParsePlText(std::string_view(reinterpret_cast<const char*>(compBuf.data()), compBuf.size()), cells, cellIdxMap);
        }
        if (!cells.empty() && tryReadCompanion(L".nets", compBuf)) {
            ParseNetsText(std::string_view(reinterpret_cast<const char*>(compBuf.data()), compBuf.size()), nets);
        }
    }

    // Auto-legalize any unplaced cells onto `rows` (or a clean floorplan grid when standalone `.nodes` has no `.scl`/`.pl`)!
    double coreMinX = 1e30, coreMinY = 1e30, coreMaxX = -1e30, coreMaxY = -1e30;
    for (const auto& r : rows) {
        const double rw = r.numSites * ((r.siteSpacing > 0.0) ? r.siteSpacing : r.siteWidth);
        coreMinX = std::min(coreMinX, r.subrowOrigin);
        coreMaxX = std::max(coreMaxX, r.subrowOrigin + rw);
        coreMinY = std::min(coreMinY, r.yCoord);
        coreMaxY = std::max(coreMaxY, r.yCoord + r.height);
    }

    if (!cells.empty()) {
        size_t unplacedCount = 0u;
        for (const auto& c : cells) if (!c.hasPlacement) ++unplacedCount;

        if (unplacedCount > 0u) {
            if (!rows.empty()) {
                size_t curRow = 0u;
                double curX = rows[0].subrowOrigin;
                size_t termIdx = 0u;
                for (auto& c : cells) {
                    if (c.hasPlacement) continue;
                    if (c.isTerminal) {
                        // Place fixed macros / IO pads around core perimeter
                        c.x = coreMinX + (termIdx * 24.0);
                        c.y = coreMaxY + 12.0;
                        c.hasPlacement = true;
                        ++termIdx;
                    } else {
                        const double rowEnd = rows[curRow].subrowOrigin + rows[curRow].numSites * rows[curRow].siteSpacing;
                        if (curX + c.w > rowEnd && curRow + 1u < rows.size()) {
                            ++curRow;
                            curX = rows[curRow].subrowOrigin;
                        }
                        c.x = curX;
                        c.y = rows[curRow].yCoord;
                        c.hasPlacement = true;
                        curX += c.w + rows[curRow].siteSpacing;
                    }
                }
            } else {
                // Standalone `.nodes` without `.scl` or `.pl`: row-packed floorplan
                double totalWidth = 0.0;
                for (const auto& c : cells) totalWidth += c.w;
                const double targetRowW = std::max(40.0, std::sqrt(totalWidth * 12.0));
                double curX = 0.0, curY = 0.0, rowMaxH = 8.0;
                for (auto& c : cells) {
                    if (c.hasPlacement) continue;
                    if (curX > 0.0 && curX + c.w > targetRowW) {
                        curX = 0.0;
                        curY += rowMaxH + 4.0;
                        rowMaxH = 8.0;
                    }
                    c.x = curX;
                    c.y = curY;
                    c.hasPlacement = true;
                    curX += c.w + 2.0;
                    rowMaxH = std::max(rowMaxH, c.h);
                }
            }
        }
    }

    for (const auto& c : cells) {
        if (!c.hasPlacement) continue;
        coreMinX = std::min(coreMinX, c.x);
        coreMaxX = std::max(coreMaxX, c.x + c.w);
        coreMinY = std::min(coreMinY, c.y);
        coreMaxY = std::max(coreMaxY, c.y + std::max(1.0, c.h));
    }
    if (coreMinX > coreMaxX) { return false; }

    // Normalize Bookshelf DBU / site units onto an ~84 mm VLSI die view
    const double spanDbu = std::max({ 1.0, coreMaxX - coreMinX, coreMaxY - coreMinY });
    const double scaleMm = 84.0 / spanDbu;
    auto toMmX = [&](double x) -> float { return static_cast<float>((x - coreMinX) * scaleMm); };
    auto toMmY = [&](double y) -> float { return static_cast<float>((y - coreMinY) * scaleMm); };

    outBoard = ParsedEdaBoard{};
    outBoard.kind = EdaDocKind::KiCadPcb;

    // 1. Render `CoreRow Horizontal` standard-cell placement rows & site ticks
    const size_t rowStride = std::max<size_t>(1u, rows.size() / 256u);
    for (size_t ri = 0u; ri < rows.size(); ri += rowStride) {
        const auto& r = rows[ri];
        const double rw = r.numSites * ((r.siteSpacing > 0.0) ? r.siteSpacing : r.siteWidth);
        const float x0 = toMmX(r.subrowOrigin);
        const float x1 = toMmX(r.subrowOrigin + rw);
        const float y0 = toMmY(r.yCoord);
        const float y1 = toMmY(r.yCoord + r.height);
        DrawEdaBoxRect(outBoard, x0, y0, x1, y1, 0.14f, EdaLayerId::BottomSilk, 0);

        // Site grid ticks on first few rows if standalone `.scl`
        if (cells.empty() && ri < 16u && r.numSites > 1u) {
            const uint32_t tickStep = std::max<uint32_t>(1u, r.numSites / 32u);
            for (uint32_t s = tickStep; s < r.numSites; s += tickStep) {
                const float sx = toMmX(r.subrowOrigin + s * ((r.siteSpacing > 0.0) ? r.siteSpacing : r.siteWidth));
                outBoard.traces.push_back({ sx, y0, sx, y1, 0.08f, EdaLayerId::BottomSilk, 0 });
            }
        }
    }

    // Outer die boundary
    DrawEdaBoxRect(outBoard, toMmX(coreMinX) - 2.0f, toMmY(coreMinY) - 2.0f,
                   toMmX(coreMaxX) + 2.0f, toMmY(coreMaxY) + 2.0f, 0.36f, EdaLayerId::EdgeCuts, 0);

    // 2. Render Standard Cells, Hard Macros, and Fixed I/O Terminals
    const double stdRowH = rows.empty() ? 8.0 : rows[0].height;
    size_t macroCount = 0u, stdCellCount = 0u, ioPinCount = 0u;
    const size_t cellStride = std::max<size_t>(1u, cells.size() / 2048u);

    for (size_t ci = 0u; ci < cells.size(); ci += cellStride) {
        const auto& c = cells[ci];
        if (c.isTerminal && c.h > stdRowH * 1.5) ++macroCount;
        else if (c.isTerminal) ++ioPinCount;
        else ++stdCellCount;

        const float x0 = toMmX(c.x);
        const float y0 = toMmY(c.y);
        const float bw = std::max(0.45f, static_cast<float>(c.w * scaleMm));
        const float bh = std::max(0.45f, static_cast<float>(std::max(0.5, c.h) * scaleMm));

        if (c.isTerminal && c.h <= stdRowH * 1.5) {
            // Fixed perimeter I/O pad
            outBoard.pads.push_back({
                x0 + bw * 0.5f, y0 + bh * 0.5f,
                std::max(0.85f, bw), std::max(0.85f, bh),
                0.0f, 0.0f, 1u, EdaLayerId::TopCopper,
                PathUtils::Utf8ToWide(c.name), L"IO_TERM"
            });
            if (outBoard.labels.size() < 160u) {
                outBoard.labels.push_back({ x0 + bw + 0.4f, y0, 0.68f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(c.name) });
            }
        } else {
            const bool isMacro = c.isTerminal || (c.h > stdRowH * 1.5);
            const EdaLayerId lid = isMacro ? EdaLayerId::InnerCopper : EdaLayerId::BottomCopper;
            DrawEdaBoxRect(outBoard, x0, y0, x0 + bw, y0 + bh, isMacro ? 0.28f : 0.18f, lid, isMacro ? 2 : 1);
            if (isMacro && outBoard.labels.size() < 180u) {
                outBoard.labels.push_back({ x0 + 0.5f, y0 + bh * 0.5f - 0.35f, 0.75f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(c.name) });
            }
        }

        if (outBoard.components.size() < 512u) {
            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(c.name);
            comp.value  = PathUtils::Utf8ToWide(c.orient);
            comp.footprint = c.isTerminal ? L"BOOKSHELF_MACRO" : L"BOOKSHELF_STDCELL";
            comp.x = x0 + bw * 0.5f;
            comp.y = y0 + bh * 0.5f;
            comp.widthMm = bw; comp.lengthMm = bh; comp.heightMm = c.isTerminal ? 1.1f : 0.4f;
            comp.pinCount = 4;
            comp.category = EdaPartCategory::IC_Package;
            outBoard.components.push_back(std::move(comp));
        }
    }

    // 3. Render Half-Perimeter / L-Steiner Netlist Routing from companion `.nets`!
    const size_t netStride = std::max<size_t>(1u, nets.size() / 1024u);
    double totalHpwl = 0.0;
    for (size_t ni = 0u; ni < nets.size(); ni += netStride) {
        const auto& net = nets[ni];
        if (net.pins.size() < 2u) continue;

        std::vector<std::pair<float, float>> pts;
        pts.reserve(net.pins.size());
        double minNx = 1e30, maxNx = -1e30, minNy = 1e30, maxNy = -1e30;

        for (const auto& p : net.pins) {
            auto it = cellIdxMap.find(p.cellName);
            if (it == cellIdxMap.end()) continue;
            const auto& c = cells[it->second];
            const double pxDbu = c.x + c.w * 0.5 + p.dx;
            const double pyDbu = c.y + c.h * 0.5 + p.dy;
            minNx = std::min(minNx, pxDbu); maxNx = std::max(maxNx, pxDbu);
            minNy = std::min(minNy, pyDbu); maxNy = std::max(maxNy, pyDbu);
            pts.push_back({ toMmX(pxDbu), toMmY(pyDbu) });
        }
        if (pts.size() < 2u) continue;
        totalHpwl += (maxNx - minNx) + (maxNy - minNy);

        // Route L-shaped Steiner branches from driver (`pts[0]`) to each sink
        const int netId = static_cast<int>((ni % 64u) + 3u);
        const float sx = pts[0].first, sy = pts[0].second;
        for (size_t k = 1u; k < pts.size() && outBoard.traces.size() < 60000u; ++k) {
            const float dx = pts[k].first, dy = pts[k].second;
            outBoard.traces.push_back({ sx, sy, dx, sy, 0.16f, EdaLayerId::TopCopper, netId });
            outBoard.traces.push_back({ dx, sy, dx, dy, 0.16f, EdaLayerId::SchWire, netId });
        }
    }

    std::wstring benchName = L"UCLA_BOOKSHELF";
    if (!stemPath.empty()) {
        const size_t sep = stemPath.find_last_of(L"\\/");
        benchName = (sep == std::wstring::npos) ? stemPath : stemPath.substr(sep + 1u);
    }

    wchar_t titleBuf[256] = {};
    swprintf_s(titleBuf, L"ISPD Bookshelf VLSI Placement (%ls — %zu rows, %zu cells, %zu nets%ls)",
               benchName.c_str(), rows.size(), cells.size(), nets.size(),
               totalHpwl > 0.0 ? (L", HPWL=" + std::to_wstring(static_cast<unsigned long long>(totalHpwl))).c_str() : L"");
    outBoard.labels.push_back({ toMmX(coreMinX), toMmY(coreMaxY) + 3.2f, 1.15f, EdaLayerId::TopSilk, titleBuf });
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(1u, nets.empty() ? (rows.size() + cells.size()) : nets.size());
    return !outBoard.traces.empty() || !outBoard.pads.empty() || !outBoard.components.empty();
}

} // namespace Formats
