#pragma once
// KiCadLegacySch.h — KiCad 4/5 Legacy EESchema Schematic (.sch) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool ParseKiCadLegacySch(std::string_view txt, const std::wstring& fullPath, ParsedEdaBoard& out) {
    using namespace DetailKiCadLegacy;
    txt = StripUtf8BomView(txt);
    if (txt.rfind("EESchema Schematic", 0) != 0) {
        return false;
    }
    for (unsigned char c : txt) {
        if (c == 0) return false;
    }

    out.kind = EdaDocKind::KiCadSch;
    constexpr float kMilToMm = 0.0254f;

    std::unordered_map<std::string, LegSymDef> companionLibs;
    LoadCompanionLegacyLibs(fullPath, companionLibs);

    auto resolveSym = [&](std::string_view rawSymName, LegSymDef& tmpBuiltin) -> const LegSymDef* {
        std::string key = NormalizeSymLookupKey(rawSymName);
        auto it = companionLibs.find(key);
        if (it != companionLibs.end()) return &it->second;
        if (GetBuiltinLegacySym(rawSymName, tmpBuiltin)) return &tmpBuiltin;
        return nullptr;
    };

    std::string sheetTitle;
    std::set<std::string> netNames;

    bool inDescr = false;
    bool inComp  = false;
    bool inSheet = false;

    struct PendingComp {
        std::string symName;
        std::wstring refDes;
        std::wstring valStr;
        std::wstring fpStr;
        float px = 0, py = 0;
        int unit = 1, conv = 1;
        int ma = 1, mb = 0, mc = 0, md = -1;
    } curComp;

    struct PendingSheet {
        float x = 0, y = 0, w = 400, h = 300;
        std::wstring name, fileName;
        int pins = 0;
    } curSheet;

    std::vector<std::string> toks;
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string_view::npos) eol = txt.size();
        std::string_view rawLn = txt.substr(pos, eol - pos);
        pos = eol + 1;
        while (!rawLn.empty() && rawLn.back() == '\r') rawLn.remove_suffix(1);
        std::string_view ln = rawLn;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.empty()) continue;

        if (ln.rfind("$Descr ", 0) == 0) {
            inDescr = true;
            continue;
        }
        if (ln == "$EndDescr") {
            inDescr = false;
            continue;
        }
        if (inDescr) {
            if (ln.rfind("Title ", 0) == 0) {
                TokenizeLegacyLine(ln, toks);
                if (toks.size() >= 2 && !toks[1].empty()) {
                    sheetTitle = toks[1];
                    out.labels.push_back({ 4.0f, -2.5f, 1.15f, EdaLayerId::SchLabel, L"Title: " + PathUtils::Utf8ToWide(sheetTitle) });
                }
            }
            continue;
        }

        if (ln == "$Comp") {
            inComp = true;
            curComp = PendingComp{};
            continue;
        }
        if (ln == "$EndComp") {
            if (inComp) {
                LegSymDef builtinDef;
                const LegSymDef* sym = resolveSym(curComp.symName, builtinDef);
                StampLegacySymInstance(
                    sym, curComp.px, curComp.py,
                    curComp.ma, curComp.mb, curComp.mc, curComp.md,
                    curComp.unit, curComp.conv,
                    curComp.refDes, curComp.valStr,
                    curComp.fpStr.empty() ? PathUtils::Utf8ToWide(curComp.symName) : curComp.fpStr,
                    true, out);
            }
            inComp = false;
            continue;
        }
        if (inComp) {
            TokenizeLegacyLine(ln, toks);
            if (toks.empty()) continue;
            if (toks[0] == "L" && toks.size() >= 3) {
                curComp.symName = toks[1];
                curComp.refDes = PathUtils::Utf8ToWide(toks[2]);
            } else if (toks[0] == "U" && toks.size() >= 3) {
                curComp.unit = std::max(1, std::atoi(toks[1].c_str()));
                curComp.conv = std::max(1, std::atoi(toks[2].c_str()));
            } else if (toks[0] == "P" && toks.size() >= 3) {
                curComp.px = static_cast<float>(std::atof(toks[1].c_str()));
                curComp.py = static_cast<float>(std::atof(toks[2].c_str()));
            } else if (toks[0] == "F" && toks.size() >= 3) {
                int fIdx = std::atoi(toks[1].c_str());
                if (fIdx == 0 && !toks[2].empty()) curComp.refDes = PathUtils::Utf8ToWide(toks[2]);
                else if (fIdx == 1) curComp.valStr = PathUtils::Utf8ToWide(toks[2]);
                else if (fIdx == 2 && !toks[2].empty()) curComp.fpStr = PathUtils::Utf8ToWide(toks[2]);
            } else if (toks.size() == 4) {
                // 2x2 integer orientation matrix: a b c d (each in {-1, 0, 1})
                int a = std::atoi(toks[0].c_str());
                int b = std::atoi(toks[1].c_str());
                int c = std::atoi(toks[2].c_str());
                int d = std::atoi(toks[3].c_str());
                if (std::abs(a) <= 1 && std::abs(b) <= 1 && std::abs(c) <= 1 && std::abs(d) <= 1 && (a * d - b * c) != 0) {
                    curComp.ma = a; curComp.mb = b; curComp.mc = c; curComp.md = d;
                }
            }
            continue;
        }

        if (ln == "$Sheet") {
            inSheet = true;
            curSheet = PendingSheet{};
            continue;
        }
        if (ln == "$EndSheet") {
            if (inSheet) {
                float x0 = curSheet.x * kMilToMm, y0 = -curSheet.y * kMilToMm;
                float x1 = (curSheet.x + curSheet.w) * kMilToMm, y1 = -(curSheet.y + curSheet.h) * kMilToMm;
                out.traces.push_back({ x0, y0, x1, y0, 0.32f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x1, y0, x1, y1, 0.32f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x1, y1, x0, y1, 0.32f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x0, y1, x0, y0, 0.32f, EdaLayerId::SchSymbol, 0 });
                std::wstring stLbl = curSheet.name.empty() ? L"SubSheet" : curSheet.name;
                if (!curSheet.fileName.empty()) stLbl += L" [" + curSheet.fileName + L"]";
                out.labels.push_back({ x0 + 0.5f, y0 + 1.0f, 1.05f, EdaLayerId::SchLabel, stLbl });

                EdaComponent3D sc;
                sc.refDes = curSheet.name.empty() ? L"SHEET" : curSheet.name;
                sc.value = curSheet.fileName;
                sc.footprint = L"HIER_SHEET";
                sc.x = (x0 + x1) * 0.5f; sc.y = (y0 + y1) * 0.5f;
                sc.widthMm = std::fabs(x1 - x0); sc.lengthMm = std::fabs(y0 - y1);
                sc.pinCount = std::max(1, curSheet.pins);
                out.components.push_back(std::move(sc));
            }
            inSheet = false;
            continue;
        }
        if (inSheet) {
            TokenizeLegacyLine(ln, toks);
            if (toks.size() >= 5 && toks[0] == "S") {
                curSheet.x = static_cast<float>(std::atof(toks[1].c_str()));
                curSheet.y = static_cast<float>(std::atof(toks[2].c_str()));
                curSheet.w = static_cast<float>(std::atof(toks[3].c_str()));
                curSheet.h = static_cast<float>(std::atof(toks[4].c_str()));
            } else if (toks.size() >= 2 && toks[0] == "F0") {
                curSheet.name = PathUtils::Utf8ToWide(toks[1]);
            } else if (toks.size() >= 2 && toks[0] == "F1") {
                curSheet.fileName = PathUtils::Utf8ToWide(toks[1]);
            } else if (toks.size() >= 6 && toks[0].size() >= 2 && toks[0][0] == 'F') {
                float px = static_cast<float>(std::atof(toks[4].c_str())) * kMilToMm;
                float py = -static_cast<float>(std::atof(toks[5].c_str())) * kMilToMm;
                out.pads.push_back({ px, py, 0.65f, 0.65f, 0, 0, 1, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(toks[1]), L"" });
                out.labels.push_back({ px + 0.6f, py - 0.25f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(toks[1]) });
                ++curSheet.pins;
            }
            continue;
        }

        if (ln == "Wire Wire Line" || ln == "Wire Bus Line" || ln == "Wire Notes Line" ||
            ln == "Entry Wire Line" || ln == "Entry Bus Bus")
        {
            size_t eol2 = txt.find('\n', pos);
            if (eol2 == std::string_view::npos) eol2 = txt.size();
            std::string coords(txt.substr(pos, eol2 - pos));
            pos = eol2 + 1;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(coords.c_str(), "%f %f %f %f", &x1, &y1, &x2, &y2) == 4) {
                bool isBus = (ln.find("Bus") != std::string_view::npos);
                bool isNotes = (ln.find("Notes") != std::string_view::npos);
                float w = isBus ? 0.48f : (isNotes ? 0.16f : 0.25f);
                EdaLayerId lid = isNotes ? EdaLayerId::SchLabel : EdaLayerId::SchWire;
                out.traces.push_back({ x1 * kMilToMm, -y1 * kMilToMm, x2 * kMilToMm, -y2 * kMilToMm, w, lid, 1 });
            }
            continue;
        }

        if (ln.rfind("Connection ~ ", 0) == 0) {
            float jx = 0, jy = 0;
            if (sscanf_s(std::string(ln.substr(13)).c_str(), "%f %f", &jx, &jy) == 2) {
                out.circles.push_back({ jx * kMilToMm, -jy * kMilToMm, 0.45f, 0.24f, true, EdaLayerId::SchWire });
                out.vias.push_back({ jx * kMilToMm, -jy * kMilToMm, 0.65f, 0.0f, 1 });
            }
            continue;
        }

        if (ln.rfind("NoConn ~ ", 0) == 0) {
            float nx = 0, ny = 0;
            if (sscanf_s(std::string(ln.substr(9)).c_str(), "%f %f", &nx, &ny) == 2) {
                float bx = nx * kMilToMm, by = -ny * kMilToMm;
                out.traces.push_back({ bx - 0.6f, by - 0.6f, bx + 0.6f, by + 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ bx - 0.6f, by + 0.6f, bx + 0.6f, by - 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
            }
            continue;
        }

        if (ln.rfind("Text ", 0) == 0) {
            TokenizeLegacyLine(ln, toks);
            size_t eol2 = txt.find('\n', pos);
            if (eol2 == std::string_view::npos) eol2 = txt.size();
            std::string_view payload = txt.substr(pos, eol2 - pos);
            pos = eol2 + 1;
            while (!payload.empty() && (payload.back() == '\r' || payload.back() == ' ')) payload.remove_suffix(1);

            if (toks.size() >= 4 && !payload.empty()) {
                const std::string& kind = toks[1]; // Label, GLabel, HLabel, Notes
                float lx = static_cast<float>(std::atof(toks[2].c_str())) * kMilToMm;
                float ly = -static_cast<float>(std::atof(toks[3].c_str())) * kMilToMm;
                bool isNet = (kind == "Label" || kind == "GLabel" || kind == "HLabel");
                if (isNet) netNames.insert(std::string(payload));

                if (kind == "GLabel" || kind == "HLabel") {
                    out.traces.push_back({ lx, ly, lx + 0.7f, ly + 0.65f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx + 0.7f, ly + 0.65f, lx + 3.8f, ly + 0.65f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx + 3.8f, ly + 0.65f, lx + 3.8f, ly - 0.65f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx + 3.8f, ly - 0.65f, lx + 0.7f, ly - 0.65f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx + 0.7f, ly - 0.65f, lx, ly, 0.20f, EdaLayerId::SchSymbol, 0 });
                }
                if (out.labels.size() < 4096) {
                    out.labels.push_back({ lx + 0.25f, ly + 0.35f, 0.95f, isNet ? EdaLayerId::SchWire : EdaLayerId::SchLabel, PathUtils::Utf8ToWide(payload) });
                }
            }
            continue;
        }
    }

    if (out.traces.empty() && out.components.empty() && out.pads.empty() && out.circles.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }

    out.formatTitle = L"KiCad Legacy Schematic (.sch";
    if (!sheetTitle.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(sheetTitle);
    out.formatTitle += L")";
    out.netCount = std::max<size_t>(1u, netNames.empty() ? out.components.size() : netNames.size());

    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

inline bool ParseKiCadLegacySch(std::string_view txt, ParsedEdaBoard& out) {
    return ParseKiCadLegacySch(txt, out.filePath, out);
}
