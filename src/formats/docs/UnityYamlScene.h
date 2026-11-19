#pragma once
// UnityYamlScene.h — Unity Engine YAML Prefab & Scene (.prefab / .unity)

namespace Formats {

inline bool ParseUnityYamlSceneDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #99. Unity YAML Prefab & Scene Hierarchy (.prefab / .unity)
    if (ext == L".prefab" || ext == L".unity") {
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("--- !u!") == std::string_view::npos && sv.find("%YAML") == std::string_view::npos) return false;

        auto unityClassName = [](int cid) -> std::wstring {
            switch (cid) {
            case 1:   return L"GameObject";
            case 4:   return L"Transform";
            case 20:  return L"Camera";
            case 23:  return L"MeshRenderer";
            case 33:  return L"MeshFilter";
            case 54:  return L"Rigidbody";
            case 65:  return L"BoxCollider";
            case 82:  return L"AudioSource";
            case 104: return L"RenderSettings";
            case 108: return L"Light";
            case 114: return L"MonoBehaviour";
            case 137: return L"SkinnedMeshRenderer";
            case 212: return L"SpriteRenderer";
            case 222: return L"CanvasRenderer";
            case 223: return L"Canvas";
            case 224: return L"RectTransform";
            default:  return L"ClassID_" + std::to_wstring(cid);
            }
        };

        SpreadsheetSheet sh;
        sh.name = L"Unity_Hierarchy";
        sh.maxCols = 4;
        sh.rows.push_back({ L"FileID", L"Unity Class", L"m_Name", L"Position / Reference" });

        size_t pos = 0;
        while ((pos = sv.find("--- !u!", pos)) != std::string_view::npos && sh.rows.size() < 500) {
            pos += 7;
            int cid = std::atoi(std::string(sv.substr(pos, std::min<size_t>(12, sv.size() - pos))).c_str());
            size_t amp = sv.find('&', pos);
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) break;
            std::string fid = (amp != std::string_view::npos && amp < eol)
                              ? std::string(sv.substr(amp + 1, eol - amp - 1)) : "0";
            if (!fid.empty() && fid.back() == '\r') fid.pop_back();

            size_t nextObj = sv.find("--- !u!", eol);
            std::string_view blk = sv.substr(eol, (nextObj == std::string_view::npos ? sv.size() : nextObj) - eol);

            auto yamlField = [&](std::string_view key) -> std::string {
                size_t kp = blk.find(key);
                if (kp == std::string_view::npos) return {};
                kp += key.size();
                size_t ke = blk.find('\n', kp);
                std::string_view v = blk.substr(kp, (ke == std::string_view::npos ? blk.size() : ke) - kp);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.remove_suffix(1);
                return std::string(v);
            };

            std::string mName = yamlField("m_Name:");
            std::string detail = yamlField("m_LocalPosition:");
            if (detail.empty()) detail = yamlField("m_GameObject:");
            sh.rows.push_back({ PathUtils::Utf8ToWide(fid), unityClassName(cid),
                                PathUtils::Utf8ToWide(mName), PathUtils::Utf8ToWide(detail) });
            pos = eol;
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (ext == L".unity") ? L"Unity Engine YAML Scene (.unity)" : L"Unity Engine YAML Prefab (.prefab)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
