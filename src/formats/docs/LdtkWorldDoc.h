#pragma once
// LdtkWorldDoc.h — LDtk (Level Designer Toolkit) 2D Game World Project (.ldtk)
// Uses shared `Json::ParseDocument` to inspect `jsonVersion`, `worldLayout`, `levels`
// (`pxWid`/`pxHei`, `layerInstances`, `entityInstances`), and `defs` (`layers`, `entities`, `tilesets`).

namespace Formats {

inline bool ParseLdtkWorldDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 24u, (64u << 20))) return false;

    Json::Value root;
    if (!Json::ParseDocument(sv, root) || !root.IsObject()) return false;

    const std::string jsonVer = root.GetString("jsonVersion");
    const Json::Value* levelsArr = root.Find("levels");
    if (jsonVer.empty() || !levelsArr || !levelsArr->IsArray()) return false;

    SpreadsheetSheet lvlSheet;
    lvlSheet.name = L"Levels";
    lvlSheet.rows.push_back({ L"Identifier", L"WorldXY", L"Size_Px", L"Layers", L"Entities" });

    for (const auto& lv : levelsArr->arrVal) {
        if (!lv.IsObject()) return false;
        const std::string ident = lv.GetString("identifier", "Level");
        const int wx = lv.GetInt("worldX", 0);
        const int wy = lv.GetInt("worldY", 0);
        const int pw = lv.GetInt("pxWid", 0);
        const int ph = lv.GetInt("pxHei", 0);
        if (pw <= 0 || ph <= 0) return false;

        size_t layerCnt = 0u;
        size_t entCnt   = 0u;
        if (const Json::Value* li = lv.Find("layerInstances"); li && li->IsArray()) {
            layerCnt = li->arrVal.size();
            for (const auto& lyr : li->arrVal) {
                if (const Json::Value* ei = lyr.Find("entityInstances"); ei && ei->IsArray()) {
                    entCnt += ei->arrVal.size();
                }
            }
        }

        if (lvlSheet.rows.size() < 4096u) {
            lvlSheet.rows.push_back({
                PathUtils::Utf8ToWide(ident),
                std::to_wstring(wx) + L"," + std::to_wstring(wy),
                std::to_wstring(pw) + L"x" + std::to_wstring(ph),
                std::to_wstring(layerCnt),
                std::to_wstring(entCnt)
            });
        }
    }

    SpreadsheetSheet defSheet;
    defSheet.name = L"Definitions";
    defSheet.rows.push_back({ L"Category", L"Identifier", L"Grid / Size", L"Details" });

    if (const Json::Value* defsObj = root.Find("defs"); defsObj && defsObj->IsObject()) {
        if (const Json::Value* dl = defsObj->Find("layers"); dl && dl->IsArray()) {
            for (const auto& d : dl->arrVal) {
                if (!d.IsObject() || defSheet.rows.size() >= 4096u) continue;
                defSheet.rows.push_back({
                    L"LayerDef",
                    PathUtils::Utf8ToWide(d.GetString("identifier")),
                    std::to_wstring(d.GetInt("gridSize", 16)) + L"px",
                    PathUtils::Utf8ToWide(d.GetString("__type", d.GetString("type")))
                });
            }
        }
        if (const Json::Value* de = defsObj->Find("entities"); de && de->IsArray()) {
            for (const auto& d : de->arrVal) {
                if (!d.IsObject() || defSheet.rows.size() >= 4096u) continue;
                defSheet.rows.push_back({
                    L"EntityDef",
                    PathUtils::Utf8ToWide(d.GetString("identifier")),
                    std::to_wstring(d.GetInt("width", 16)) + L"x" + std::to_wstring(d.GetInt("height", 16)),
                    L"maxCount=" + std::to_wstring(d.GetInt("maxCount", 0))
                });
            }
        }
        if (const Json::Value* dt = defsObj->Find("tilesets"); dt && dt->IsArray()) {
            for (const auto& d : dt->arrVal) {
                if (!d.IsObject() || defSheet.rows.size() >= 4096u) continue;
                defSheet.rows.push_back({
                    L"TilesetDef",
                    PathUtils::Utf8ToWide(d.GetString("identifier")),
                    std::to_wstring(d.GetInt("pxWid", 0)) + L"x" + std::to_wstring(d.GetInt("pxHei", 0)),
                    PathUtils::Utf8ToWide(d.GetString("relPath"))
                });
            }
        }
    }

    if (lvlSheet.rows.size() <= 1u && defSheet.rows.size() <= 1u) return false;

    outDoc.sheets.clear();
    outDoc.paragraphs.clear();
    if (lvlSheet.rows.size() > 1u) {
        FinalizeSheetCols(lvlSheet);
        outDoc.sheets.push_back(std::move(lvlSheet));
    }
    if (defSheet.rows.size() > 1u) {
        FinalizeSheetCols(defSheet);
        outDoc.sheets.push_back(std::move(defSheet));
    }

    const std::string layout = root.GetString("worldLayout", "Free");
    wchar_t title[180] = {};
    swprintf_s(title, L"LDtk 2D Game World (.ldtk v%ls, %zu levels, %ls)",
               PathUtils::Utf8ToWide(jsonVer).c_str(), levelsArr->arrVal.size(),
               PathUtils::Utf8ToWide(layout).c_str());
    outDoc.formatTitle = title;
    outDoc.kind = OfficeFileKind::CsvTable;
    outDoc.isSpreadsheet = true;
    outDoc.valid = true;
    return true;
}

} // namespace Formats
