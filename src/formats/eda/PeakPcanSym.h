#pragma once
// PeakPcanSym.h — PEAK-System PCAN-Explorer CAN / CAN-FD Symbol File (.sym v5.0 / v6.0)
// Parses `FormatVersion=5.0`/`6.0`, `{SEND}`/`{RECEIVE}`/`{SENDRECEIVE}`, `[Symbol]`, `ID=...h`,
// `Len=...`, and `Var=<name> <type> <startBit>,<bitLen>` into a CAN Bus Frame & Bit-Allocation Diagram.

namespace Formats {

namespace DetailPeakSym {

struct SymVar {
    std::string name;
    int startBit = 0;
    int bitLen   = 8;
};

struct SymFrame {
    std::string name;
    std::string canIdHex;
    int dlcBytes = 8;
    bool isCanFd = false;
    std::vector<SymVar> vars;
};

} // namespace DetailPeakSym

inline bool ParsePeakPcanSym(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 20u, (16u << 20))) return false;

    bool sawFormatVer = false;
    std::string fmtVer;
    std::vector<DetailPeakSym::SymFrame> frames;
    DetailPeakSym::SymFrame* curFrame = nullptr;

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        const size_t sl = rawLine.find("//");
        if (sl != std::string_view::npos) rawLine = rawLine.substr(0u, sl);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (ReaderText::StartsWithCI(line, "FormatVersion=")) {
            fmtVer = std::string(ReaderText::TrimSv(line.substr(14u)));
            double fv = 0.0;
            if (!ReaderText::ParseDoubleSv(fmtVer, fv) || fv < 3.0 || fv > 10.0) return false;
            sawFormatVer = true;
            continue;
        }

        if (line.front() == '{') {
            if (line.back() != '}') return false;
            curFrame = nullptr;
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']' || line.size() < 3u) return false;
            DetailPeakSym::SymFrame fr{};
            fr.name.assign(line.data() + 1u, line.size() - 2u);
            frames.push_back(std::move(fr));
            curFrame = &frames.back();
            continue;
        }

        if (!curFrame) continue;

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string key = ReaderText::ToUpperAscii(ReaderText::TrimSv(line.substr(0u, eq)));
        std::string_view rhs  = ReaderText::TrimSv(line.substr(eq + 1u));

        if (key == "ID") {
            curFrame->canIdHex.assign(rhs.data(), rhs.size());
        } else if (key == "LEN" || key == "DLC") {
            int len = 8;
            if (!ReaderText::ParseIntSv(rhs, len) || len < 0 || len > 64) return false;
            curFrame->dlcBytes = std::max(1, len);
        } else if (key == "TYPE") {
            std::string tUp = ReaderText::ToUpperAscii(rhs);
            if (tUp.find("FD") != std::string::npos) curFrame->isCanFd = true;
        } else if (key == "VAR") {
            // Var=PackVoltage unsigned 0,16 -u:V -f:0.1
            std::string_view vName, vType, bitSpec;
            if (!ReaderText::NextTokenSv(rhs, vName) ||
                !ReaderText::NextTokenSv(rhs, vType) ||
                !ReaderText::NextTokenSv(rhs, bitSpec)) {
                return false;
            }
            const size_t cm = bitSpec.find(',');
            if (cm == std::string_view::npos) return false;
            int sb = 0, bl = 0;
            if (!ReaderText::ParseIntSv(bitSpec.substr(0u, cm), sb) ||
                !ReaderText::ParseIntSv(bitSpec.substr(cm + 1u), bl) ||
                sb < 0 || bl <= 0 || sb + bl > 512) {
                return false;
            }
            curFrame->vars.push_back({ std::string(vName), sb, bl });
        }
    }

    if (!sawFormatVer || frames.empty()) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    const float busH = static_cast<float>(frames.size()) * 15.0f + 8.0f;
    // CAN_H and CAN_L vertical differential bus rails
    out.traces.push_back({ -22.0f, 6.0f, -22.0f, -busH, 0.35f, EdaLayerId::TopCopper, 1 });
    out.traces.push_back({ -19.5f, 6.0f, -19.5f, -busH, 0.35f, EdaLayerId::BottomCopper, 2 });
    out.labels.push_back({ -24.5f, 7.5f, 0.95f, EdaLayerId::TopSilk, L"CAN_H / CAN_L" });

    size_t totalSignals = 0u;
    for (size_t fi = 0u; fi < frames.size(); ++fi) {
        const auto& fr = frames[fi];
        const float y  = -static_cast<float>(fi) * 15.0f;
        const float frameW = 54.0f;
        const float cx = 16.0f;

        out.traces.push_back({ -22.0f, y, cx - frameW * 0.5f, y, 0.24f, EdaLayerId::SchWire, static_cast<int>(fi + 3u) });
        DetailSiliconAndBusEda::AddRectOutline(out, cx, y, frameW, 10.5f,
                                     fr.isCanFd ? EdaLayerId::InnerCopper : EdaLayerId::SchSymbol, 0.28f);

        std::wstring wHdr = PathUtils::Utf8ToWide(fr.name + " [" + fr.canIdHex + "] " +
                                                  std::to_string(fr.dlcBytes) + "B");
        out.labels.push_back({ cx - frameW * 0.47f, y + 2.8f, 0.92f, EdaLayerId::TopSilk, wHdr });

        const float totalBits = static_cast<float>(std::max(8, fr.dlcBytes * 8));
        for (size_t vi = 0u; vi < fr.vars.size(); ++vi) {
            const auto& vr = fr.vars[vi];
            const float x0 = (cx - frameW * 0.45f) + (static_cast<float>(vr.startBit) / totalBits) * (frameW * 0.90f);
            const float sw = std::max(2.2f, (static_cast<float>(vr.bitLen) / totalBits) * (frameW * 0.90f) - 0.4f);
            DetailSiliconAndBusEda::AddRectOutline(out, x0 + sw * 0.5f, y - 1.8f, sw, 4.0f, EdaLayerId::TopCopper, 0.18f);
            out.pads.push_back({ x0 + sw * 0.5f, y - 1.8f, 0.9f, 0.9f, 0.0f, 0.0f, 1u, EdaLayerId::TopCopper,
                                 std::to_wstring(vr.startBit), PathUtils::Utf8ToWide(vr.name) });
            out.labels.push_back({ x0 + 0.4f, y - 2.3f, 0.68f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(vr.name) });
            ++totalSignals;
        }

        EdaComponent3D c{};
        c.refDes = PathUtils::Utf8ToWide(fr.name);
        c.value  = PathUtils::Utf8ToWide(fr.canIdHex);
        c.footprint = fr.isCanFd ? L"PCAN_CANFD_FRAME" : L"PCAN_CAN_FRAME";
        c.x = cx; c.y = y;
        c.widthMm = frameW; c.lengthMm = 10.5f;
        c.pinCount = static_cast<int>(fr.vars.size());
        c.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(c));
    }

    out.netCount = frames.size() + totalSignals;
    wchar_t title[200] = {};
    swprintf_s(title, L"PEAK PCAN Symbol File (.sym v%ls, %zu frames, %zu signals)",
               PathUtils::Utf8ToWide(fmtVer).c_str(), frames.size(), totalSignals);
    out.formatTitle = title;
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
