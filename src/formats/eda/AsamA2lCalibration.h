#pragma once
// AsamA2lCalibration.h — ASAM MCD-2 MC / ASAP2 Automotive ECU Calibration & Measurement Map (.a2l)
// Parses ASAP2_VERSION, /begin PROJECT, /begin MODULE, MEMORY_SEGMENT, COMPU_METHOD,
// CHARACTERISTIC (VALUE / CURVE / MAP / VAL_BLK + AXIS_DESCR), AXIS_PTS, and MEASUREMENT
// with strict /begin../end block stack validation and C/C++ comment stripping.

namespace Formats {

namespace DetailAsamA2l {

struct A2lToken {
    std::string text;
    bool quoted = false;
};

inline bool TokenizeA2l(std::string_view sv, std::vector<A2lToken>& outTokens) {
    outTokens.clear();
    size_t pos = 0;
    const size_t n = sv.size();
    while (pos < n) {
        unsigned char c = static_cast<unsigned char>(sv[pos]);
        if (c == 0) return false;
        if (c <= 0x20u) { ++pos; continue; }

        // C block comment /* ... */
        if (c == '/' && pos + 1 < n && sv[pos + 1] == '*') {
            pos += 2;
            bool closed = false;
            while (pos + 1 < n) {
                if (sv[pos] == '*' && sv[pos + 1] == '/') {
                    pos += 2;
                    closed = true;
                    break;
                }
                ++pos;
            }
            if (!closed) return false; // Unterminated /* comment
            continue;
        }
        // C++ line comment // ...
        if (c == '/' && pos + 1 < n && sv[pos + 1] == '/') {
            pos += 2;
            while (pos < n && sv[pos] != '\n') ++pos;
            continue;
        }
        // Quoted string "..." (ASAP2 escapes quotes as "" or \")
        if (c == '"') {
            ++pos;
            A2lToken tok;
            tok.quoted = true;
            bool closed = false;
            while (pos < n) {
                if (sv[pos] == '\\' && pos + 1 < n) {
                    tok.text.push_back(sv[pos + 1]);
                    pos += 2;
                } else if (sv[pos] == '"') {
                    if (pos + 1 < n && sv[pos + 1] == '"') {
                        tok.text.push_back('"');
                        pos += 2;
                    } else {
                        ++pos;
                        closed = true;
                        break;
                    }
                } else {
                    tok.text.push_back(sv[pos++]);
                }
            }
            if (!closed) return false;
            outTokens.push_back(std::move(tok));
            continue;
        }

        size_t s = pos;
        while (pos < n) {
            unsigned char ch = static_cast<unsigned char>(sv[pos]);
            if (ch <= 0x20u || ch == '"') break;
            if (ch == '/' && pos + 1 < n && (sv[pos + 1] == '*' || sv[pos + 1] == '/')) break;
            ++pos;
        }
        A2lToken tok;
        tok.text.assign(sv.data() + s, pos - s);
        outTokens.push_back(std::move(tok));
        if (outTokens.size() > 1500000u) return false;
    }
    return !outTokens.empty();
}

inline bool ParseA2lAddress(const std::string& s, uint64_t& outAddr) {
    if (s.empty()) return false;
    char* endp = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &endp, 0);
    if (endp == s.c_str() || *endp != '\0') return false;
    outAddr = static_cast<uint64_t>(v);
    return true;
}

inline bool ParseA2lDouble(const std::string& s, double& outVal) {
    if (s.empty()) return false;
    char* endp = nullptr;
    double v = std::strtod(s.c_str(), &endp);
    if (endp == s.c_str() || *endp != '\0' || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

struct A2lMemSeg {
    std::string name;
    std::string memType; // FLASH, ROM, RAM, EEPROM
    uint64_t addr = 0;
    uint64_t sizeBytes = 0;
};

struct A2lCharacteristic {
    std::string name;
    std::string kind; // VALUE, CURVE, MAP, VAL_BLK, CUBOID, ASCII
    uint64_t addr = 0;
    std::string compu;
    double lower = 0.0;
    double upper = 0.0;
    std::vector<std::string> axisRefs;
};

struct A2lMeasurement {
    std::string name;
    std::string dataType;
    std::string compu;
    uint64_t ecuAddr = 0;
    bool hasAddr = false;
    double lower = 0.0;
    double upper = 0.0;
};

struct A2lAxisPts {
    std::string name;
    uint64_t addr = 0;
    int maxPts = 0;
    std::string compu;
};

} // namespace DetailAsamA2l

inline bool ParseAsamA2lCalibration(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 32u, (32u << 20))) return false;

    std::vector<DetailAsamA2l::A2lToken> toks;
    if (!DetailAsamA2l::TokenizeA2l(sv, toks)) return false;

    bool sawAsap2Ver = false;
    int verMaj = 1, verMin = 0;
    bool sawProject = false;
    bool sawModule = false;
    std::string projectName;
    std::string moduleName;

    std::unordered_map<std::string, std::string> compuUnits; // compuName -> unit
    std::vector<DetailAsamA2l::A2lMemSeg> memSegs;
    std::vector<DetailAsamA2l::A2lCharacteristic> chars;
    std::vector<DetailAsamA2l::A2lMeasurement> meas;
    std::vector<DetailAsamA2l::A2lAxisPts> axes;

    std::vector<std::pair<std::string, size_t>> blockStack; // (UPPER_TAG, startIndexAfterTag)

    for (size_t i = 0; i < toks.size(); ++i) {
        if (!toks[i].quoted && ReaderText::ToUpperAscii(toks[i].text) == "ASAP2_VERSION" && blockStack.empty()) {
            if (i + 2 >= toks.size()) return false;
            double mj = 0, mn = 0;
            if (!DetailAsamA2l::ParseA2lDouble(toks[i + 1].text, mj) ||
                !DetailAsamA2l::ParseA2lDouble(toks[i + 2].text, mn) || mj < 1.0) {
                return false;
            }
            verMaj = static_cast<int>(mj);
            verMin = static_cast<int>(mn);
            sawAsap2Ver = true;
            i += 2;
            continue;
        }

        if (!toks[i].quoted && ReaderText::ToUpperAscii(toks[i].text) == "/BEGIN") {
            if (i + 1 >= toks.size() || toks[i + 1].quoted) return false;
            std::string tag = ReaderText::ToUpperAscii(toks[i + 1].text);
            if (blockStack.size() >= 32) return false;
            blockStack.emplace_back(tag, i + 2);

            if (tag == "PROJECT" && i + 2 < toks.size()) {
                sawProject = true;
                projectName = toks[i + 2].text;
            } else if (tag == "MODULE" && i + 2 < toks.size()) {
                sawModule = true;
                moduleName = toks[i + 2].text;
            }
            ++i;
            continue;
        }

        if (!toks[i].quoted && ReaderText::ToUpperAscii(toks[i].text) == "/END") {
            if (i + 1 >= toks.size() || toks[i + 1].quoted || blockStack.empty()) return false;
            std::string tag = ReaderText::ToUpperAscii(toks[i + 1].text);
            if (blockStack.back().first != tag) return false; // Mismatched /begin .. /end!

            const size_t bStart = blockStack.back().second;
            const size_t bEnd   = i; // exclusive of "/end"
            blockStack.pop_back();

            // Extract leaf/domain blocks when they close
            if (tag == "MEMORY_SEGMENT" && bEnd - bStart >= 7) {
                // MEMORY_SEGMENT <name> "<long>" <prgType> <memType> <attr> <addr> <size> ...
                DetailAsamA2l::A2lMemSeg ms;
                ms.name = toks[bStart].text;
                ms.memType = ReaderText::ToUpperAscii(toks[bStart + 3].text);
                if (DetailAsamA2l::ParseA2lAddress(toks[bStart + 5].text, ms.addr) &&
                    DetailAsamA2l::ParseA2lAddress(toks[bStart + 6].text, ms.sizeBytes) &&
                    ms.sizeBytes > 0) {
                    memSegs.push_back(std::move(ms));
                }
            } else if (tag == "COMPU_METHOD" && bEnd - bStart >= 5) {
                // COMPU_METHOD <name> "<long>" <convType> "<fmt>" "<unit>"
                compuUnits[toks[bStart].text] = toks[bStart + 4].text;
            } else if (tag == "CHARACTERISTIC" && bEnd - bStart >= 8) {
                // CHARACTERISTIC <name> "<long>" <type> <addr> <deposit> <maxDiff> <compu> <lower> <upper>
                DetailAsamA2l::A2lCharacteristic ch;
                ch.name = toks[bStart].text;
                ch.kind = ReaderText::ToUpperAscii(toks[bStart + 2].text);
                if (!DetailAsamA2l::ParseA2lAddress(toks[bStart + 3].text, ch.addr)) return false;
                ch.compu = toks[bStart + 6].text;
                DetailAsamA2l::ParseA2lDouble(toks[bStart + 7].text, ch.lower);
                if (bStart + 8 < bEnd) DetailAsamA2l::ParseA2lDouble(toks[bStart + 8].text, ch.upper);
                for (size_t k = bStart + 8; k + 1 < bEnd; ++k) {
                    if (!toks[k].quoted && ReaderText::ToUpperAscii(toks[k].text) == "AXIS_PTS_REF") {
                        ch.axisRefs.push_back(toks[k + 1].text);
                    }
                }
                if (chars.size() < 256) chars.push_back(std::move(ch));
            } else if (tag == "AXIS_PTS" && bEnd - bStart >= 9) {
                // AXIS_PTS <name> "<long>" <addr> <inputQty> <deposit> <maxDiff> <compu> <maxPts> <lower> <upper>
                DetailAsamA2l::A2lAxisPts ax;
                ax.name = toks[bStart].text;
                if (!DetailAsamA2l::ParseA2lAddress(toks[bStart + 2].text, ax.addr)) return false;
                ax.compu = toks[bStart + 6].text;
                double mp = 0;
                if (DetailAsamA2l::ParseA2lDouble(toks[bStart + 7].text, mp)) ax.maxPts = static_cast<int>(mp);
                if (axes.size() < 256) axes.push_back(std::move(ax));
            } else if (tag == "MEASUREMENT" && bEnd - bStart >= 8) {
                // MEASUREMENT <name> "<long>" <datatype> <compu> <res> <acc> <lower> <upper> ... ECU_ADDRESS <hex>
                DetailAsamA2l::A2lMeasurement m;
                m.name = toks[bStart].text;
                m.dataType = ReaderText::ToUpperAscii(toks[bStart + 2].text);
                m.compu = toks[bStart + 3].text;
                DetailAsamA2l::ParseA2lDouble(toks[bStart + 6].text, m.lower);
                DetailAsamA2l::ParseA2lDouble(toks[bStart + 7].text, m.upper);
                for (size_t k = bStart + 8; k + 1 < bEnd; ++k) {
                    if (!toks[k].quoted && ReaderText::ToUpperAscii(toks[k].text) == "ECU_ADDRESS") {
                        if (DetailAsamA2l::ParseA2lAddress(toks[k + 1].text, m.ecuAddr)) {
                            m.hasAddr = true;
                        }
                    }
                }
                if (meas.size() < 256) meas.push_back(std::move(m));
            }

            ++i;
            continue;
        }
    }

    if (!blockStack.empty()) return false; // Unclosed /begin block!
    if (!sawProject || !sawModule) return false;
    if (chars.empty() && meas.empty() && axes.empty() && memSegs.empty()) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    const size_t leftCount  = chars.size() + axes.size();
    const size_t rightCount = meas.size();
    const size_t maxRows    = std::max<size_t>({ 2u, leftCount, rightCount, memSegs.size() + 1u });
    const float rowPitch    = 11.0f;
    const float ecuH        = std::max(28.0f, static_cast<float>(maxRows) * rowPitch);
    const float ecuW        = 34.0f;
    const float ecuCx       = 0.0f;
    const float ecuCy       = 0.0f;

    // Central ECU MCU / XCP Slave block
    DetailSiliconAndBusEda::AddRectOutline(out, ecuCx, ecuCy, ecuW, ecuH, EdaLayerId::SchSymbol, 0.35f);
    EdaComponent3D ecuComp{};
    ecuComp.refDes = L"ECU_" + PathUtils::Utf8ToWide(moduleName.empty() ? "MCU" : moduleName);
    ecuComp.value = PathUtils::Utf8ToWide(projectName);
    ecuComp.footprint = L"ASAP2_MODULE";
    ecuComp.x = ecuCx;
    ecuComp.y = ecuCy;
    ecuComp.widthMm = ecuW;
    ecuComp.lengthMm = ecuH;
    ecuComp.category = EdaPartCategory::IC_Package;
    ecuComp.pinCount = static_cast<int>(leftCount + rightCount);
    out.components.push_back(ecuComp);

    out.labels.push_back({ ecuCx - ecuW * 0.44f, ecuCy + ecuH * 0.5f - 3.0f, 1.5f, EdaLayerId::TopSilk,
                           ecuComp.refDes });

    // Render MEMORY_SEGMENT sub-bands inside ECU block
    for (size_t mi = 0; mi < memSegs.size() && mi < 8; ++mi) {
        const auto& ms = memSegs[mi];
        float by = ecuCy + ecuH * 0.5f - 8.5f - static_cast<float>(mi) * 5.2f;
        EdaLayerId segLayer = (ms.memType == "RAM") ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper;
        DetailSiliconAndBusEda::AddRectOutline(out, ecuCx, by, ecuW - 5.0f, 3.8f, segLayer, 0.20f);
        wchar_t segLbl[128] = {};
        swprintf_s(segLbl, L"%ls (%ls 0x%llX)",
                   PathUtils::Utf8ToWide(ms.name).c_str(),
                   PathUtils::Utf8ToWide(ms.memType).c_str(),
                   static_cast<unsigned long long>(ms.addr));
        out.labels.push_back({ ecuCx - ecuW * 0.40f, by - 0.4f, 0.85f, EdaLayerId::SchLabel, segLbl });
    }

    int netId = 1;
    // Left column: Calibration CHARACTERISTICs + AXIS_PTS -> wired into ECU left edge
    const float leftCx = -46.0f;
    const float cardW  = 24.0f;
    const float cardH  = 7.2f;
    size_t lIdx = 0;

    for (const auto& ch : chars) {
        float y = ecuCy + ecuH * 0.5f - 6.0f - static_cast<float>(lIdx++) * rowPitch;
        DetailSiliconAndBusEda::AddRectOutline(out, leftCx, y, cardW, cardH, EdaLayerId::TopCopper, 0.24f);

        // Mini MAP grid or CURVE polyline icon inside calibration block
        if (ch.kind == "MAP") {
            out.traces.push_back({ leftCx + 6.0f, y - 2.0f, leftCx + 10.5f, y - 2.0f, 0.14f, EdaLayerId::TopSilk, 0 });
            out.traces.push_back({ leftCx + 6.0f, y + 2.0f, leftCx + 10.5f, y + 2.0f, 0.14f, EdaLayerId::TopSilk, 0 });
            out.traces.push_back({ leftCx + 8.2f, y - 2.5f, leftCx + 8.2f,  y + 2.5f, 0.14f, EdaLayerId::TopSilk, 0 });
        } else if (ch.kind == "CURVE") {
            out.traces.push_back({ leftCx + 6.0f, y - 2.0f, leftCx + 8.2f, y + 0.5f, 0.18f, EdaLayerId::TopSilk, 0 });
            out.traces.push_back({ leftCx + 8.2f, y + 0.5f, leftCx + 10.5f, y + 2.2f, 0.18f, EdaLayerId::TopSilk, 0 });
        }

        wchar_t addrHex[32] = {};
        swprintf_s(addrHex, L"0x%llX", static_cast<unsigned long long>(ch.addr));
        std::wstring wName = PathUtils::Utf8ToWide(ch.name);
        out.labels.push_back({ leftCx - cardW * 0.45f, y + 1.0f, 0.95f, EdaLayerId::SchLabel, wName });
        out.labels.push_back({ leftCx - cardW * 0.45f, y - 1.6f, 0.78f, EdaLayerId::TopSilk,
                               PathUtils::Utf8ToWide(ch.kind) + L" @ " + addrHex });

        float pinX = ecuCx - ecuW * 0.5f;
        out.traces.push_back({ leftCx + cardW * 0.5f, y, pinX, y, 0.22f, EdaLayerId::SchWire, netId++ });
        out.pads.push_back({ pinX, y, 0.7f, 0.7f, 0.0f, 0.0f, 1u, EdaLayerId::TopCopper, addrHex, wName });

        EdaComponent3D c{};
        c.refDes = L"CAL_" + wName;
        c.value = PathUtils::Utf8ToWide(ch.kind);
        c.footprint = addrHex;
        c.x = leftCx; c.y = y;
        c.widthMm = cardW; c.lengthMm = cardH;
        c.category = EdaPartCategory::GenericSMD;
        out.components.push_back(std::move(c));
    }

    for (const auto& ax : axes) {
        float y = ecuCy + ecuH * 0.5f - 6.0f - static_cast<float>(lIdx++) * rowPitch;
        DetailSiliconAndBusEda::AddRectOutline(out, leftCx, y, cardW, cardH * 0.85f, EdaLayerId::InnerCopper, 0.22f);
        wchar_t addrHex[32] = {};
        swprintf_s(addrHex, L"0x%llX", static_cast<unsigned long long>(ax.addr));
        std::wstring wName = PathUtils::Utf8ToWide(ax.name);
        out.labels.push_back({ leftCx - cardW * 0.45f, y + 0.6f, 0.90f, EdaLayerId::SchLabel,
                               wName + L" [" + std::to_wstring(ax.maxPts) + L" pts]" });
        float pinX = ecuCx - ecuW * 0.5f;
        out.traces.push_back({ leftCx + cardW * 0.5f, y, pinX, y, 0.20f, EdaLayerId::SchWire, netId++ });
        out.pads.push_back({ pinX, y, 0.65f, 0.65f, 0.0f, 0.0f, 0u, EdaLayerId::InnerCopper, addrHex, wName });
    }

    // Right column: Live XCP/CCP MEASUREMENTs <- wired from ECU right edge
    const float rightCx = 46.0f;
    for (size_t ri = 0; ri < meas.size(); ++ri) {
        const auto& m = meas[ri];
        float y = ecuCy + ecuH * 0.5f - 6.0f - static_cast<float>(ri) * rowPitch;
        DetailSiliconAndBusEda::AddRectOutline(out, rightCx, y, cardW, cardH, EdaLayerId::BottomCopper, 0.24f);

        std::string unitStr;
        auto uit = compuUnits.find(m.compu);
        if (uit != compuUnits.end()) unitStr = uit->second;

        std::wstring wName = PathUtils::Utf8ToWide(m.name);
        wchar_t rngBuf[96] = {};
        swprintf_s(rngBuf, L"%ls [%.4g..%.4g %ls]",
                   PathUtils::Utf8ToWide(m.dataType).c_str(), m.lower, m.upper,
                   PathUtils::Utf8ToWide(unitStr).c_str());
        out.labels.push_back({ rightCx - cardW * 0.45f, y + 1.0f, 0.95f, EdaLayerId::SchLabel, wName });
        out.labels.push_back({ rightCx - cardW * 0.45f, y - 1.6f, 0.78f, EdaLayerId::TopSilk, rngBuf });

        float pinX = ecuCx + ecuW * 0.5f;
        out.traces.push_back({ pinX, y, rightCx - cardW * 0.5f, y, 0.22f, EdaLayerId::SchWire, netId++ });
        wchar_t addrHex[32] = L"XCP";
        if (m.hasAddr) swprintf_s(addrHex, L"0x%llX", static_cast<unsigned long long>(m.ecuAddr));
        out.pads.push_back({ pinX, y, 0.7f, 0.7f, 0.0f, 0.0f, 0u, EdaLayerId::BottomCopper, addrHex, wName });

        EdaComponent3D c{};
        c.refDes = L"MEAS_" + wName;
        c.value = PathUtils::Utf8ToWide(m.dataType);
        c.footprint = addrHex;
        c.x = rightCx; c.y = y;
        c.widthMm = cardW; c.lengthMm = cardH;
        c.category = EdaPartCategory::GenericSMD;
        out.components.push_back(std::move(c));
    }

    out.netCount = static_cast<size_t>(netId - 1);
    wchar_t title[220] = {};
    if (sawAsap2Ver) {
        swprintf_s(title, L"ASAM MCD-2 MC ECU Map (.a2l, ASAP2 v%d.%d, %zu cal, %zu meas) — %ls",
                   verMaj, verMin, chars.size(), meas.size(),
                   PathUtils::Utf8ToWide(projectName).c_str());
    } else {
        swprintf_s(title, L"ASAM MCD-2 MC ECU Map (.a2l, %zu cal, %zu meas) — %ls",
                   chars.size(), meas.size(),
                   PathUtils::Utf8ToWide(projectName).c_str());
    }
    out.formatTitle = title;
    return true;
}

} // namespace Formats
