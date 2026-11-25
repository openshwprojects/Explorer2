#pragma once
// FpgaPinConstraintsCommon.h — Shared FPGA Package Ball-Grid / QFP Pinout Map Builder
// Used by IceStormPcf, LatticeDiamondLpf, XilinxConstraintsUcf, XilinxConstraintsXdc, QuartusQsf, and SynopsysSdc.
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {
namespace FpgaPinCommon {

struct FpgaPinEntry {
    std::string signal;
    std::string site;      // e.g. "A12", "B4", "84", "P23", "PIN_84"
    std::string ioStd;     // e.g. "LVCMOS33", "LVDS"
    std::string bank;
    bool hasPullup = false;
    bool isClock = false;
};

struct FpgaClockSpec {
    std::string name;
    double freqMhz = 0.0;
};

// Parse BGA row/col like "A1", "AA14", "R3" -> (rowIdx >= 0, colIdx >= 0), or false if numeric QFP pin
inline bool ParseBgaCoord(std::string_view loc, int& outRow, int& outCol) {
    loc = ReaderText::TrimSv(loc);
    if (loc.size() >= 5 && ReaderText::StartsWithCI(loc, "BALL_")) loc.remove_prefix(5);
    else if (loc.size() >= 4 && ReaderText::StartsWithCI(loc, "PIN_")) loc.remove_prefix(4);
    if (loc.size() < 2 || loc.size() > 5) return false;
    size_t i = 0;
    int row = 0;
    while (i < loc.size() && ((loc[i] >= 'A' && loc[i] <= 'Z') || (loc[i] >= 'a' && loc[i] <= 'z'))) {
        const int ch = (loc[i] >= 'a') ? (loc[i] - 'a') : (loc[i] - 'A');
        row = row * 26 + (ch + 1);
        ++i;
    }
    if (i == 0 || i == loc.size() || i > 2) return false;
    int col = 0;
    for (; i < loc.size(); ++i) {
        if (loc[i] < '0' || loc[i] > '9') return false;
        col = col * 10 + (loc[i] - '0');
    }
    if (row <= 0 || row > 40 || col <= 0 || col > 60) return false;
    // Treat "P<N>" with large pin number or explicit QFP P-prefix above BGA column range as QFP pin
    if (row == 16 && col > 36) return false;
    outRow = row - 1;
    outCol = col - 1;
    return true;
}

// Parse numeric QFP/TQFP/DIP pin number from "84", "PIN_84", "P84" -> true if numeric pin >= 1
inline bool ParseNumericPackagePin(std::string_view loc, int& outPinNum) {
    loc = ReaderText::TrimSv(loc);
    if (loc.empty()) return false;
    if (ReaderText::StartsWithCI(loc, "PIN_")) loc.remove_prefix(4);
    else if (loc.size() >= 2 && (loc[0] == 'P' || loc[0] == 'p') && loc[1] >= '0' && loc[1] <= '9') loc.remove_prefix(1);

    if (loc.empty() || loc.size() > 5) return false;
    int val = 0;
    for (char c : loc) {
        if (c < '0' || c > '9') return false;
        val = val * 10 + (c - '0');
    }
    if (val <= 0 || val > 4096) return false;
    outPinNum = val;
    return true;
}

inline std::wstring RowIndexToLetters(int r) {
    std::wstring res;
    int v = r + 1;
    while (v > 0) {
        int rem = (v - 1) % 26;
        res.insert(res.begin(), static_cast<wchar_t>(L'A' + rem));
        v = (v - 1) / 26;
    }
    return res;
}

inline EdaLayerId LayerForPinEntry(const FpgaPinEntry& p) {
    if (p.isClock || ContainsTextCI(p.signal, "clk") || ContainsTextCI(p.signal, "clock")) {
        return EdaLayerId::InnerCopper;
    }
    if (ContainsTextCI(p.ioStd, "LVDS") || ContainsTextCI(p.ioStd, "TMDS") ||
        ContainsTextCI(p.ioStd, "SSTL") || ContainsTextCI(p.ioStd, "HSTL") ||
        ContainsTextCI(p.ioStd, "DIFF")) {
        return EdaLayerId::InnerCopper;
    }
    if (p.hasPullup || ContainsTextCI(p.signal, "rst") || ContainsTextCI(p.signal, "reset")) {
        return EdaLayerId::BottomCopper;
    }
    return EdaLayerId::TopCopper;
}

inline bool EmitFpgaPinoutBoard(
    const std::wstring& formatTitlePrefix,
    const std::wstring& deviceName,
    const std::vector<FpgaPinEntry>& pins,
    const std::vector<FpgaClockSpec>& clocks,
    ParsedEdaBoard& outBoard)
{
    if (pins.empty()) return false;
    outBoard.kind = EdaDocKind::KiCadPcb;

    struct ResolvedBga { size_t idx; int row; int col; };
    struct ResolvedQfp { size_t idx; int pinNum; bool hasNum; };

    std::vector<ResolvedBga> bgaPins;
    std::vector<ResolvedQfp> qfpPins;
    int maxRow = 0, maxCol = 0;
    int maxPinNum = 0;

    for (size_t i = 0; i < pins.size(); ++i) {
        int pnum = 0, r = 0, c = 0;
        // Pure digits ("84", "PIN_84") are unambiguously numeric QFP/DIP pins
        std::string_view rawSite = ReaderText::TrimSv(pins[i].site);
        bool startsWithDigitOrPin = (!rawSite.empty() && rawSite[0] >= '0' && rawSite[0] <= '9') ||
                                    (ReaderText::StartsWithCI(rawSite, "PIN_") && rawSite.size() > 4 && rawSite[4] >= '0' && rawSite[4] <= '9');
        if (startsWithDigitOrPin && ParseNumericPackagePin(rawSite, pnum)) {
            qfpPins.push_back({ i, pnum, true });
            maxPinNum = std::max(maxPinNum, pnum);
        } else if (ParseBgaCoord(rawSite, r, c)) {
            bgaPins.push_back({ i, r, c });
            maxRow = std::max(maxRow, r);
            maxCol = std::max(maxCol, c);
        } else if (ParseNumericPackagePin(rawSite, pnum)) {
            qfpPins.push_back({ i, pnum, true });
            maxPinNum = std::max(maxPinNum, pnum);
        } else {
            qfpPins.push_back({ i, static_cast<int>(i + 1), false });
        }
    }

    const bool useBgaGrid = (!bgaPins.empty() && bgaPins.size() >= qfpPins.size());
    float pkgHalf = 18.0f;

    if (useBgaGrid) {
        const float pitch = 1.6f;
        std::set<int> usedRows, usedCols;
        for (const auto& bp : bgaPins) {
            usedRows.insert(bp.row);
            usedCols.insert(bp.col);
        }
        const int gridRows = std::max(4, maxRow + 1);
        const int gridCols = std::max(4, maxCol + 1);
        const float spanW = static_cast<float>(gridCols - 1) * pitch;
        const float spanH = static_cast<float>(gridRows - 1) * pitch;
        pkgHalf = std::max(spanW, spanH) * 0.5f + 3.6f;

        // Package substrate outline + Pin A1 corner index dot (no fake inner wire-bonded die!)
        DrawEdaBoxRect(outBoard, -pkgHalf, -pkgHalf, pkgHalf, pkgHalf, 0.35f, EdaLayerId::EdgeCuts, 0);
        outBoard.circles.push_back({ -pkgHalf + 1.4f, pkgHalf - 1.4f, 0.55f, 0.22f, true, EdaLayerId::TopSilk });

        // Row & column coordinate labels for populated BGA rows/cols only (never 1,024 fake empty balls!)
        for (int r : usedRows) {
            const float py = (static_cast<float>(gridRows - 1) * 0.5f - static_cast<float>(r)) * pitch;
            outBoard.labels.push_back({ -pkgHalf + 0.9f, py - 0.3f, 0.65f, EdaLayerId::BottomSilk, RowIndexToLetters(r) });
        }
        for (int c : usedCols) {
            const float px = (static_cast<float>(c) - static_cast<float>(gridCols - 1) * 0.5f) * pitch;
            outBoard.labels.push_back({ px - 0.35f, pkgHalf - 1.6f, 0.65f, EdaLayerId::BottomSilk, std::to_wstring(c + 1) });
        }

        for (size_t k = 0; k < bgaPins.size(); ++k) {
            const auto& bp = bgaPins[k];
            const auto& p = pins[bp.idx];
            const float px = (static_cast<float>(bp.col) - static_cast<float>(gridCols - 1) * 0.5f) * pitch;
            const float py = (static_cast<float>(gridRows - 1) * 0.5f - static_cast<float>(bp.row)) * pitch;

            const std::wstring wSig = PathUtils::Utf8ToWide(p.signal);
            const std::wstring wLoc = PathUtils::Utf8ToWide(p.site.empty() ? std::to_string(bp.idx + 1) : p.site);
            const EdaLayerId padLayer = LayerForPinEntry(p);

            outBoard.pads.push_back({
                px, py, 0.92f, 0.92f, 0.0f, 0.0f, 0, padLayer, wLoc, wSig
            });

            const float radLen = std::hypot(px, py);
            const float ux = (radLen > 0.1f) ? (px / radLen) : 1.0f;
            const float uy = (radLen > 0.1f) ? (py / radLen) : 0.0f;
            outBoard.traces.push_back({
                px, py, px + ux * 0.65f, py + uy * 0.65f, 0.18f, padLayer, static_cast<int>(bp.idx + 1)
            });

            if (outBoard.labels.size() < 320u) {
                outBoard.labels.push_back({ px + 0.52f, py + 0.32f, 0.58f, EdaLayerId::SchLabel, wSig });
            }
        }

        for (size_t q = 0; q < qfpPins.size(); ++q) {
            const auto& p = pins[qfpPins[q].idx];
            const float px = -pkgHalf + 2.5f + static_cast<float>(q) * 1.6f;
            const float py = -pkgHalf - 1.5f;
            outBoard.pads.push_back({
                px, py, 0.8f, 1.3f, 0.0f, 0.0f, 1, LayerForPinEntry(p),
                PathUtils::Utf8ToWide(p.site), PathUtils::Utf8ToWide(p.signal)
            });
        }
    } else {
        // True QFP / TQFP / LQFP 4-side perimeter layout ordered counter-clockwise by parsed pin number 1..N!
        std::vector<ResolvedQfp> ordered = qfpPins;
        for (const auto& bp : bgaPins) {
            int pnum = 0;
            const bool okNum = ParseNumericPackagePin(pins[bp.idx].site, pnum);
            ordered.push_back({ bp.idx, okNum ? pnum : static_cast<int>(bp.idx + 1), okNum });
            if (okNum) maxPinNum = std::max(maxPinNum, pnum);
        }
        std::stable_sort(ordered.begin(), ordered.end(), [&](const ResolvedQfp& a, const ResolvedQfp& b) {
            if (a.hasNum != b.hasNum) return a.hasNum > b.hasNum;
            if (a.pinNum != b.pinNum) return a.pinNum < b.pinNum;
            return a.idx < b.idx;
        });

        int totalPkgPins = static_cast<int>(ordered.size());
        if (maxPinNum > totalPkgPins && maxPinNum <= 512) {
            static const int kStdQfpSizes[] = { 16, 20, 24, 28, 32, 44, 48, 64, 80, 100, 128, 144, 176, 208, 240, 256, 304, 384, 512 };
            totalPkgPins = ((maxPinNum + 3) / 4) * 4;
            for (int sz : kStdQfpSizes) {
                if (sz >= maxPinNum && sz >= static_cast<int>(ordered.size())) {
                    totalPkgPins = sz;
                    break;
                }
            }
        } else {
            totalPkgPins = std::max(4, ((totalPkgPins + 3) / 4) * 4);
        }

        const int perSide = std::max(1, totalPkgPins / 4);
        const float leadPitch = (perSide > 36) ? 0.85f : 1.15f;
        pkgHalf = std::max(9.0f, static_cast<float>(perSide + 1) * leadPitch * 0.5f + 1.8f);

        // QFP molded body outline + Pin-1 index circle at top-left (no fake inner wire-bonded die!)
        DrawEdaBoxRect(outBoard, -pkgHalf, -pkgHalf, pkgHalf, pkgHalf, 0.35f, EdaLayerId::EdgeCuts, 0);
        DrawEdaBoxRect(outBoard, -pkgHalf + 0.9f, -pkgHalf + 0.9f, pkgHalf - 0.9f, pkgHalf - 0.9f, 0.16f, EdaLayerId::TopSilk, 0);
        outBoard.circles.push_back({ -pkgHalf + 2.2f, pkgHalf - 2.2f, 0.65f, 0.24f, true, EdaLayerId::TopSilk });

        for (size_t k = 0; k < ordered.size(); ++k) {
            const auto& qp = ordered[k];
            const auto& p = pins[qp.idx];

            const int zeroIdx = qp.hasNum
                ? std::clamp(qp.pinNum - 1, 0, totalPkgPins - 1)
                : static_cast<int>(k % static_cast<size_t>(totalPkgPins));
            const int side = std::min(3, zeroIdx / perSide);
            const int off  = std::min(perSide - 1, zeroIdx % perSide);
            const float t  = (perSide > 1)
                ? (static_cast<float>(off) / static_cast<float>(perSide - 1) - 0.5f)
                : 0.0f;
            const float span = static_cast<float>(perSide - 1) * leadPitch;

            float bodyX = 0.0f, bodyY = 0.0f;
            float padX = 0.0f, padY = 0.0f;
            float padW = 1.45f, padH = 0.62f;
            float lblX = 0.0f, lblY = 0.0f;

            if (side == 0) { // Left side: top -> bottom (Pin 1..perSide)
                bodyX = -pkgHalf;       bodyY = -t * span;
                padX  = -pkgHalf - 1.1f; padY = bodyY;
                padW  = 1.5f;           padH = 0.60f;
                lblX  = padX - 5.8f;    lblY = padY - 0.25f;
            } else if (side == 1) { // Bottom side: left -> right
                bodyX = t * span;       bodyY = -pkgHalf;
                padX  = bodyX;          padY  = -pkgHalf - 1.1f;
                padW  = 0.60f;          padH  = 1.5f;
                lblX  = padX - 0.4f;    lblY  = padY - 2.1f;
            } else if (side == 2) { // Right side: bottom -> top
                bodyX = pkgHalf;        bodyY = t * span;
                padX  = pkgHalf + 1.1f; padY  = bodyY;
                padW  = 1.5f;           padH = 0.60f;
                lblX  = padX + 1.2f;    lblY  = padY - 0.25f;
            } else { // Top side: right -> left
                bodyX = -t * span;      bodyY = pkgHalf;
                padX  = bodyX;          padY  = pkgHalf + 1.1f;
                padW  = 0.60f;          padH  = 1.5f;
                lblX  = padX - 0.4f;    lblY  = padY + 1.2f;
            }

            const std::wstring wSig = PathUtils::Utf8ToWide(p.signal);
            const std::wstring wLoc = PathUtils::Utf8ToWide(p.site.empty() ? std::to_string(zeroIdx + 1) : p.site);
            const EdaLayerId padLayer = LayerForPinEntry(p);

            outBoard.traces.push_back({
                bodyX, bodyY, padX, padY, 0.30f, EdaLayerId::TopSilk, static_cast<int>(qp.idx + 1)
            });
            outBoard.pads.push_back({
                padX, padY, padW, padH, 0.0f, 0.0f, 1, padLayer, wLoc, wSig
            });

            if (outBoard.labels.size() < 320u) {
                std::wstring tag = wLoc + L":" + wSig;
                outBoard.labels.push_back({ lblX, lblY, 0.58f, EdaLayerId::SchLabel, tag });
            }
        }
    }

    const std::wstring devLabel = deviceName.empty() ? L"FPGA_PKG" : deviceName;
    outBoard.labels.push_back({ -4.5f, 0.8f, 1.15f, EdaLayerId::TopSilk, devLabel });
    outBoard.labels.push_back({
        -4.5f, -1.2f, 0.85f, EdaLayerId::BottomSilk,
        std::to_wstring(pins.size()) + (useBgaGrid ? L" BGA balls" : L" QFP pins")
    });

    // Optional clock constraint annotations below package
    for (size_t ci = 0; ci < std::min<size_t>(clocks.size(), 6u); ++ci) {
        wchar_t clkBuf[128] = {};
        swprintf_s(clkBuf, L"CLK %hs @ %.2f MHz", clocks[ci].name.c_str(), clocks[ci].freqMhz);
        outBoard.labels.push_back({
            -pkgHalf, -pkgHalf - 3.6f - static_cast<float>(ci) * 1.8f,
            0.80f, EdaLayerId::SchLabel, clkBuf
        });
    }

    EdaComponent3D comp;
    comp.refDes = L"U1";
    comp.value = devLabel;
    comp.footprint = useBgaGrid ? L"FPGA_BGA" : L"FPGA_QFP";
    comp.x = 0.0f;
    comp.y = 0.0f;
    comp.widthMm = pkgHalf * 2.0f;
    comp.lengthMm = pkgHalf * 2.0f;
    comp.heightMm = 2.1f;
    comp.pinCount = static_cast<int>(pins.size());
    comp.category = EdaPartCategory::IC_Package;
    outBoard.components.push_back(std::move(comp));

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"%ls%ls (%zu I/O pins%ls)",
        formatTitlePrefix.c_str(),
        deviceName.empty() ? L"" : (L" — " + deviceName).c_str(),
        pins.size(),
        clocks.empty() ? L"" : (L", " + std::to_wstring(clocks.size()) + L" clocks").c_str());
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = pins.size() + clocks.size();
    return true;
}

} // namespace FpgaPinCommon
} // namespace Formats
