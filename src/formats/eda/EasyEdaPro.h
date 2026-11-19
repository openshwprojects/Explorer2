#pragma once
// EasyEdaPro.h — EasyEDA / JLCPCB Pro (.epro, .eprj, .epcb, .esch, .esym, .efoo) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// Parse EasyEDA Pro JSON-Lines (.epcb, .esch, .esym, .efoo)
inline bool ParseEasyEdaProJsonLines(std::string_view sv, ParsedEdaBoard& out, float sheetOffX = 0.0f) {
    using namespace DetailEasyEda;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }

    bool sawDocType = false;
    bool isSch = false;
    constexpr float kMilToMm = 0.0254f; // EasyEDA Pro coordinates are in mils

    std::unordered_map<std::string, size_t> compIdxById;
    std::unordered_map<std::string, int> netIds;
    auto getNetId = [&](const std::string& net) -> int {
        if (net.empty()) return 0;
        auto it = netIds.find(net);
        if (it != netIds.end()) return it->second;
        int id = static_cast<int>(netIds.size() + 1);
        netIds[net] = id;
        return id;
    };

    size_t pos = 0;
    size_t validLines = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
        while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
        if (ln.empty()) continue;

        size_t lp = 0;
        JVal rec;
        if (!ParseJsonValue(ln, lp, 0, rec) || rec.kind != JVal::Kind::Array || rec.arrVal.empty() || rec.arrVal[0].kind != JVal::Kind::String) {
            if (!sawDocType) return false;
            continue;
        }
        ++validLines;
        const std::string& cmd = rec.arrVal[0].strVal;

        if (cmd == "DOCTYPE" && rec.arrVal.size() >= 2) {
            sawDocType = true;
            const std::string& dt = rec.arrVal[1].strVal;
            isSch = (dt == "SCH" || dt == "SYMBOL" || dt == "SCHLIB");
            out.kind = isSch ? EdaDocKind::KiCadSch : EdaDocKind::KiCadPcb;
            out.formatTitle = isSch ? L"EasyEDA Pro Schematic (.esch)" : L"EasyEDA Pro 3D PCB (.epcb)";
        } else if (cmd == "NET" && rec.arrVal.size() >= 2 && rec.arrVal[1].kind == JVal::Kind::String) {
            getNetId(rec.arrVal[1].strVal);
        } else if (!isSch && cmd == "COMPONENT" && rec.arrVal.size() >= 7) {
            // ["COMPONENT", id, fpName, layer, x_mil, y_mil, rot, {"Designator":"U1","Value":"ESP32"}]
            std::string cid = rec.arrVal[1].strVal;
            std::string fp  = rec.arrVal[2].strVal;
            int lyr = static_cast<int>(rec.arrVal[3].numVal);
            float cx = sheetOffX + static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            float cy = static_cast<float>(rec.arrVal[5].numVal) * kMilToMm;
            float rot = static_cast<float>(rec.arrVal[6].numVal);

            std::string des, val;
            if (rec.arrVal.size() >= 8 && rec.arrVal[7].kind == JVal::Kind::Object) {
                if (const JVal* d = rec.arrVal[7].Find("Designator")) des = d->strVal;
                if (const JVal* v = rec.arrVal[7].Find("Value")) val = v->strVal;
                else if (const JVal* m = rec.arrVal[7].Find("Manufacturer Part")) val = m->strVal;
            }
            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(des.empty() ? cid : des);
            comp.value = PathUtils::Utf8ToWide(val);
            comp.footprint = PathUtils::Utf8ToWide(fp.empty() ? "EPCB_FP" : fp);
            comp.x = cx; comp.y = cy;
            comp.rotDeg = rot;
            comp.bottomSide = (lyr == 2);
            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
            compIdxById[cid] = out.components.size();
            out.labels.push_back({ cx, cy, 0.9f, comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, comp.refDes });
            out.components.push_back(std::move(comp));
        } else if (!isSch && cmd == "PAD" && rec.arrVal.size() >= 10) {
            // ["PAD", id, compId, net, layer, pinNum, x_mil, y_mil, rot, ["RECT"|"ELLIPSE"|"OVAL", w_mil, h_mil], holeDiam_mil]
            int nid = getNetId(rec.arrVal[3].strVal);
            (void)nid;
            int lyr = static_cast<int>(rec.arrVal[4].numVal);
            std::string pinNum = rec.arrVal[5].strVal;
            float px = sheetOffX + static_cast<float>(rec.arrVal[6].numVal) * kMilToMm;
            float py = static_cast<float>(rec.arrVal[7].numVal) * kMilToMm;
            float rot = static_cast<float>(rec.arrVal[8].numVal);
            float pw = 1.2f, ph = 1.2f;
            uint8_t sh = 1;
            if (rec.arrVal[9].kind == JVal::Kind::Array && rec.arrVal[9].arrVal.size() >= 3) {
                const std::string& st = rec.arrVal[9].arrVal[0].strVal;
                sh = (st == "ELLIPSE" || st == "CIRCLE") ? 0 : ((st == "OVAL") ? 3 : 1);
                pw = std::max(0.15f, static_cast<float>(rec.arrVal[9].arrVal[1].numVal) * kMilToMm);
                ph = std::max(0.15f, static_cast<float>(rec.arrVal[9].arrVal[2].numVal) * kMilToMm);
            }
            float holeD = 0.0f;
            if (rec.arrVal.size() >= 11) {
                if (rec.arrVal[10].kind == JVal::Kind::Number) holeD = static_cast<float>(rec.arrVal[10].numVal) * kMilToMm;
                else if (rec.arrVal[10].kind == JVal::Kind::Array && rec.arrVal[10].arrVal.size() >= 2) holeD = static_cast<float>(rec.arrVal[10].arrVal[1].numVal) * kMilToMm;
            }
            EdaPadPrim pad{};
            pad.x = px; pad.y = py; pad.w = pw; pad.h = ph;
            pad.rotDeg = rot; pad.drill = std::max(0.0f, holeD); pad.shape = sh;
            pad.layer = (pad.drill > 0.05f || lyr == 12) ? EdaLayerId::PadsThrough : MapEasyEdaProLayer(lyr);
            pad.pinName = PathUtils::Utf8ToWide(pinNum);
            pad.netName = PathUtils::Utf8ToWide(rec.arrVal[3].strVal);
            out.pads.push_back(std::move(pad));
        } else if (!isSch && cmd == "VIA" && rec.arrVal.size() >= 7) {
            // ["VIA", id, net, x_mil, y_mil, hole_mil, diam_mil]
            int nid = getNetId(rec.arrVal[2].strVal);
            float vx = sheetOffX + static_cast<float>(rec.arrVal[3].numVal) * kMilToMm;
            float vy = static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            float hole = std::max(0.12f, static_cast<float>(rec.arrVal[5].numVal) * kMilToMm);
            float diam = std::max(hole * 1.4f, static_cast<float>(rec.arrVal[6].numVal) * kMilToMm);
            out.vias.push_back({ vx, vy, diam, hole, nid });
        } else if (!isSch && cmd == "LINE" && rec.arrVal.size() >= 9) {
            // ["LINE", id, net, layer, x1, y1, x2, y2, width]
            int nid = getNetId(rec.arrVal[2].strVal);
            EdaLayerId lid = MapEasyEdaProLayer(static_cast<int>(rec.arrVal[3].numVal));
            float x1 = sheetOffX + static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            float y1 = static_cast<float>(rec.arrVal[5].numVal) * kMilToMm;
            float x2 = sheetOffX + static_cast<float>(rec.arrVal[6].numVal) * kMilToMm;
            float y2 = static_cast<float>(rec.arrVal[7].numVal) * kMilToMm;
            float w  = std::clamp(static_cast<float>(rec.arrVal[8].numVal) * kMilToMm, 0.08f, 4.0f);
            out.traces.push_back({ x1, y1, x2, y2, w, lid, nid });
        } else if (!isSch && (cmd == "POLY" || cmd == "POUR" || cmd == "FILL") && rec.arrVal.size() >= 5) {
            // ["POLY", id, net, layer, width, [x0, y0, x1, y1, ...]]
            int nid = getNetId(rec.arrVal[2].strVal);
            EdaLayerId lid = MapEasyEdaProLayer(static_cast<int>(rec.arrVal[3].numVal));
            size_t arrIdx = (rec.arrVal[4].kind == JVal::Kind::Array) ? 4 : 5;
            float w = (arrIdx == 5 && rec.arrVal[4].kind == JVal::Kind::Number) ? std::clamp(static_cast<float>(rec.arrVal[4].numVal) * kMilToMm, 0.12f, 2.0f) : 0.22f;
            if (arrIdx < rec.arrVal.size() && rec.arrVal[arrIdx].kind == JVal::Kind::Array) {
                const auto& pts = rec.arrVal[arrIdx].arrVal;
                for (size_t i = 0; i + 3 < pts.size() && out.traces.size() < 120000; i += 2) {
                    out.traces.push_back({
                        sheetOffX + static_cast<float>(pts[i].numVal) * kMilToMm,
                        static_cast<float>(pts[i + 1].numVal) * kMilToMm,
                        sheetOffX + static_cast<float>(pts[i + 2].numVal) * kMilToMm,
                        static_cast<float>(pts[i + 3].numVal) * kMilToMm,
                        w, lid, nid
                    });
                }
            }
        } else if (isSch && cmd == "COMPONENT" && rec.arrVal.size() >= 6) {
            // ["COMPONENT", id, symName, x, y, rot]
            std::string cid = rec.arrVal[1].strVal;
            std::string sym = rec.arrVal[2].strVal;
            float cx = sheetOffX + static_cast<float>(rec.arrVal[3].numVal) * kMilToMm;
            float cy = static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            float rot = static_cast<float>(rec.arrVal[5].numVal);

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(cid);
            comp.footprint = PathUtils::Utf8ToWide(sym.empty() ? "ESCH_SYM" : sym);
            comp.x = cx; comp.y = cy; comp.rotDeg = rot;
            comp.widthMm = 6.0f; comp.lengthMm = 4.5f; comp.heightMm = 1.2f;
            float hw = 3.0f, hh = 2.25f;
            out.traces.push_back({ cx - hw, cy - hh, cx + hw, cy - hh, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + hw, cy - hh, cx + hw, cy + hh, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + hw, cy + hh, cx - hw, cy + hh, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - hw, cy + hh, cx - hw, cy - hh, 0.26f, EdaLayerId::SchSymbol, 0 });
            compIdxById[cid] = out.components.size();
            out.components.push_back(std::move(comp));
        } else if (isSch && cmd == "ATTR" && rec.arrVal.size() >= 5) {
            // ["ATTR", id, parentId, key, val]
            auto it = compIdxById.find(rec.arrVal[2].strVal);
            if (it != compIdxById.end()) {
                auto& c = out.components[it->second];
                const std::string& k = rec.arrVal[3].strVal;
                const std::string& v = rec.arrVal[4].strVal;
                if (k == "Designator" && !v.empty()) {
                    c.refDes = PathUtils::Utf8ToWide(v);
                    out.labels.push_back({ c.x - 2.5f, c.y + 2.8f, 0.95f, EdaLayerId::SchLabel, c.refDes });
                } else if ((k == "Value" || k == "Device" || k == "Name") && !v.empty() && c.value.empty()) {
                    c.value = PathUtils::Utf8ToWide(v);
                }
            }
        } else if (isSch && cmd == "PIN" && rec.arrVal.size() >= 7) {
            // ["PIN", id, parentId, pinNum, pinName, x, y]
            float px = sheetOffX + static_cast<float>(rec.arrVal[5].numVal) * kMilToMm;
            float py = static_cast<float>(rec.arrVal[6].numVal) * kMilToMm;
            EdaPadPrim pad{};
            pad.x = px; pad.y = py; pad.w = 0.65f; pad.h = 0.65f;
            pad.layer = EdaLayerId::SchSymbol;
            pad.pinName = PathUtils::Utf8ToWide(rec.arrVal[4].strVal.empty() ? rec.arrVal[3].strVal : rec.arrVal[4].strVal);
            out.pads.push_back(std::move(pad));
            auto it = compIdxById.find(rec.arrVal[2].strVal);
            if (it != compIdxById.end()) {
                const auto& c = out.components[it->second];
                float bx = std::clamp(px, c.x - 3.0f, c.x + 3.0f);
                float by = std::clamp(py, c.y - 2.25f, c.y + 2.25f);
                out.traces.push_back({ bx, by, px, py, 0.22f, EdaLayerId::SchSymbol, 0 });
            }
        } else if (isSch && cmd == "WIRE" && rec.arrVal.size() >= 3 && rec.arrVal[2].kind == JVal::Kind::Array) {
            // ["WIRE", id, [[x1, y1, x2, y2], ...], netName]
            int nid = (rec.arrVal.size() >= 4 && rec.arrVal[3].kind == JVal::Kind::String) ? getNetId(rec.arrVal[3].strVal) : 1;
            for (const auto& seg : rec.arrVal[2].arrVal) {
                if (seg.kind == JVal::Kind::Array && seg.arrVal.size() >= 4) {
                    out.traces.push_back({
                        sheetOffX + static_cast<float>(seg.arrVal[0].numVal) * kMilToMm,
                        static_cast<float>(seg.arrVal[1].numVal) * kMilToMm,
                        sheetOffX + static_cast<float>(seg.arrVal[2].numVal) * kMilToMm,
                        static_cast<float>(seg.arrVal[3].numVal) * kMilToMm,
                        0.25f, EdaLayerId::SchWire, nid
                    });
                }
            }
        } else if (isSch && (cmd == "NETLABEL" || cmd == "FLAG") && rec.arrVal.size() >= 5) {
            // ["NETLABEL", id, netName, x, y]
            std::string nn = rec.arrVal[2].strVal;
            getNetId(nn);
            float nx = sheetOffX + static_cast<float>(rec.arrVal[3].numVal) * kMilToMm;
            float ny = static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            out.vias.push_back({ nx, ny, 0.6f, 0.0f, 0 });
            out.labels.push_back({ nx + 0.3f, ny + 0.5f, 0.9f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(nn) });
        }
    }

    if (!sawDocType || (out.traces.empty() && out.pads.empty() && out.components.empty() && out.vias.empty())) {
        return false;
    }
    out.netCount = std::max<size_t>(1, netIds.empty() ? out.components.size() : netIds.size());
    return true;
}

inline bool ParseEasyEdaProFile(const uint8_t* data, size_t size, const std::wstring& fullPath, const std::wstring& ext, ParsedEdaBoard& out) {
    if (!data || size < 16 || size > 64 * 1024 * 1024) return false;

    // 1. EasyEDA Pro .epro ZIP project archive (contains PCB/*.epcb and/or SHEET/*/*.esch + project.json)
    if (ext == L".epro" || (size >= 22 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4)) {
        std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries) || zEntries.empty()) {
            return false;
        }
        std::vector<std::string> epcbPaths, eschPaths;
        std::string projTitle;
        for (const auto& ze : zEntries) {
            if (ze.isDirectory) continue;
            std::string pUtf8 = PathUtils::WideToUtf8(ze.pathInArchive);
            std::wstring pLow = PathUtils::ToLower(ze.pathInArchive);
            if (pLow.size() > 5 && pLow.substr(pLow.size() - 5) == L".epcb") epcbPaths.push_back(pUtf8);
            else if (pLow.size() > 5 && pLow.substr(pLow.size() - 5) == L".esch") eschPaths.push_back(pUtf8);
            else if (pLow == L"project.json") {
                std::vector<uint8_t> pj;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, pUtf8.c_str(), pj) && !pj.empty()) {
                    size_t p = 0;
                    DetailEasyEda::JVal jv;
                    std::string_view psv(reinterpret_cast<const char*>(pj.data()), pj.size());
                    if (DetailEasyEda::ParseJsonValue(psv, p, 0, jv) && jv.kind == DetailEasyEda::JVal::Kind::Object) {
                        if (const auto* t = jv.Find("title"); t && t->kind == DetailEasyEda::JVal::Kind::String) projTitle = t->strVal;
                    }
                }
            }
        }
        if (epcbPaths.empty() && eschPaths.empty()) return false;

        if (!epcbPaths.empty()) {
            std::vector<uint8_t> pcbBytes;
            if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, epcbPaths[0].c_str(), pcbBytes) || pcbBytes.empty()) {
                return false;
            }
            std::string_view psv(reinterpret_cast<const char*>(pcbBytes.data()), pcbBytes.size());
            if (!ParseEasyEdaProJsonLines(psv, out)) return false;
            out.formatTitle = L"EasyEDA Pro Project (.epro — PCB";
            if (!projTitle.empty()) out.formatTitle += L": " + PathUtils::Utf8ToWide(projTitle);
            out.formatTitle += L")";
            return true;
        }

        float offX = 0.0f;
        size_t sheetsOk = 0;
        for (size_t i = 0; i < eschPaths.size() && i < 12; ++i) {
            std::vector<uint8_t> schBytes;
            if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, eschPaths[i].c_str(), schBytes) || schBytes.empty()) continue;
            std::string_view ssv(reinterpret_cast<const char*>(schBytes.data()), schBytes.size());
            if (ParseEasyEdaProJsonLines(ssv, out, offX)) {
                offX += 140.0f;
                ++sheetsOk;
            }
        }
        if (sheetsOk == 0) return false;
        out.formatTitle = L"EasyEDA Pro Project (.epro — " + std::to_wstring(sheetsOk) + L" Schematic Sheet" + (sheetsOk > 1 ? L"s)" : L")");
        return true;
    }

    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // 2. EasyEDA Pro .eprj project manifest JSON
    if (ext == L".eprj") {
        if (!DetailEasyEda::IsBalancedJsonText(sv)) return false;
        size_t p = 0;
        DetailEasyEda::JVal root;
        if (!DetailEasyEda::ParseJsonValue(sv, p, 0, root) || root.kind != DetailEasyEda::JVal::Kind::Object) return false;
        const auto* boards = root.Find("boards");
        const auto* schs   = root.Find("schematics");
        if (!boards && !schs && !root.Find("config")) return false;

        // Try loading companion PCB/*.epcb from disk if present alongside .eprj
        if (!fullPath.empty()) {
            size_t sep = fullPath.find_last_of(L"\\/");
            if (sep != std::wstring::npos) {
                std::wstring dir = fullPath.substr(0, sep);
                WIN32_FIND_DATAW fd{};
                HANDLE hFind = FindFirstFileW((dir + L"\\*.epcb").c_str(), &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    std::wstring epcbFile = dir + L"\\" + fd.cFileName;
                    FindClose(hFind);
                    std::vector<uint8_t> eb;
                    if (PathUtils::ReadFileBytes(epcbFile, 16 * 1024 * 1024, eb) && !eb.empty()) {
                        std::string_view esv(reinterpret_cast<const char*>(eb.data()), eb.size());
                        if (ParseEasyEdaProJsonLines(esv, out)) {
                            out.formatTitle = L"EasyEDA Pro Project (.eprj + .epcb)";
                            return true;
                        }
                    }
                }
            }
        }

        // Render structured project hierarchy card diagram from .eprj manifest
        std::string title;
        if (const auto* t = root.Find("title"); t && t->kind == DetailEasyEda::JVal::Kind::String) title = t->strVal;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"EasyEDA Pro Project Manifest (.eprj" + (title.empty() ? std::wstring(L")") : (L" — " + PathUtils::Utf8ToWide(title) + L")"));

        float y = 20.0f;
        size_t nodeCnt = 0;
        auto addProjBlock = [&](const std::string& kindTag, const std::string& itemName) {
            float x0 = 0.0f, x1 = 44.0f, y0 = y - 3.5f, y1 = y + 3.5f;
            out.traces.push_back({ x0, y0, x1, y0, 0.25f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y0, x1, y1, 0.25f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y1, x0, y1, 0.25f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x0, y1, x0, y0, 0.25f, EdaLayerId::SchSymbol, 0 });
            out.labels.push_back({ x0 + 1.5f, y, 1.0f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide("[" + kindTag + "] " + itemName) });
            EdaComponent3D c;
            c.refDes = PathUtils::Utf8ToWide(kindTag + std::to_string(nodeCnt + 1));
            c.value = PathUtils::Utf8ToWide(itemName);
            c.footprint = PathUtils::Utf8ToWide(kindTag);
            c.x = 22.0f; c.y = y; c.widthMm = 44.0f; c.lengthMm = 7.0f;
            out.components.push_back(std::move(c));
            y -= 10.0f;
            ++nodeCnt;
        };
        if (boards && boards->kind == DetailEasyEda::JVal::Kind::Object) {
            for (const auto& kv : boards->objVal) addProjBlock("PCB", kv.first);
        }
        if (schs && schs->kind == DetailEasyEda::JVal::Kind::Object) {
            for (const auto& kv : schs->objVal) addProjBlock("SCH", kv.first);
        }
        if (nodeCnt == 0) return false;
        out.netCount = nodeCnt;
        return true;
    }

    // 3. EasyEDA Pro JSON-Lines (.epcb, .esch, .esym, .efoo)
    if (ext == L".epcb" || ext == L".esch" || ext == L".esym" || ext == L".efoo" ||
        sv.find("[\"DOCTYPE\"") != std::string_view::npos || sv.find("[ \"DOCTYPE\"") != std::string_view::npos) {
        return ParseEasyEdaProJsonLines(sv, out);
    }
    return false;
}

} // namespace Formats
