#pragma once
// TerraformState.h — Terraform Infrastructure State (.tfstate) Reader

namespace Formats {
namespace DetailCloudSecOps {

// ---- Terraform state (v4: resources[] with instances[]; v1-v3: modules[] with a resources map) ----
inline bool ParseTerraformState(std::string_view text, ParsedOfficeDocument& out) {
    SpreadsheetSheet sh;
    sh.name = L"Resources";
    sh.rows.push_back({ L"Address", L"Mode", L"Type", L"Name", L"Provider", L"ID", L"Module" });
    size_t resources = 0, instances = 0;
    std::map<std::string, size_t> byType;

    // provider["registry.terraform.io/hashicorp/aws"].west -> registry.terraform.io/hashicorp/aws (west)
    auto providerName = [](const std::string& p) {
        size_t q1 = p.find('"');
        size_t q2 = (q1 == std::string::npos) ? std::string::npos : p.find('"', q1 + 1);
        if (q2 == std::string::npos) return W(p);
        std::wstring s = W(std::string_view(p).substr(q1 + 1, q2 - q1 - 1));
        size_t alias = p.find("].", q2);
        if (alias != std::string::npos) s += L" (" + W(std::string_view(p).substr(alias + 2)) + L")";
        return s;
    };
    auto addRow = [&](std::string addr, const std::string& mode, const std::string& type, const std::string& name,
                      const std::string& provider, std::wstring id, const std::string& module) {
        ++byType[type.empty() ? std::string("(none)") : type];
        if (sh.rows.size() > kMaxRows) return;
        sh.rows.push_back({ W(addr), W(mode), W(type), W(name), providerName(provider), std::move(id), W(module) });
    };
    auto handleResource = [&](const JVal& r) {
        if (!r.IsObject()) return;
        ++resources;
        std::string mode = r.GetString("mode"), type = r.GetString("type"), name = r.GetString("name");
        std::string module = r.GetString("module"), provider = r.GetString("provider");
        std::string base = module.empty() ? std::string() : module + ".";
        if (mode == "data") base += "data.";
        base += type + "." + name;
        const JVal* insts = r.Find("instances");
        if (!insts || !insts->IsArray() || insts->arrVal.empty()) {
            addRow(base, mode, type, name, provider, {}, module);
            return;
        }
        for (const auto& in : insts->arrVal) {
            ++instances;
            std::string addr = base;
            if (const JVal* key = in.Find("index_key")) {
                if (key->IsString()) addr += "[\"" + key->strVal + "\"]";
                else if (key->IsNumber()) addr += "[" + key->strVal + "]";
            }
            addRow(addr, mode, type, name, provider, Cell(Path(&in, { "attributes", "id" })), module);
        }
    };

    JParser parser(text, StreamLimits());
    JVal root;
    root.kind = JVal::Kind::Object;
    bool sawResources = false;
    bool complete = parser.StreamObject([&](const std::string& key) {
        if (key == "resources" && !sawResources && parser.Peek() == '[') {
            sawResources = true;
            return parser.StreamArray([&](JVal&& r) { handleResource(r); });
        }
        JVal v;
        if (!parser.ParseValue(v)) return false;
        root.objVal.emplace_back(key, std::move(v));
        return true;
    }) && parser.AtEnd();

    // Legacy states: modules[].resources is a map of address -> { type, provider, primary: { id } }
    if (const JVal* mods = root.Find("modules"); mods && mods->IsArray()) {
        sawResources = true;
        for (const auto& m : mods->arrVal) {
            std::string module;
            if (const JVal* path = m.Find("path"); path && path->IsArray()) {
                for (const auto& seg : path->arrVal) {
                    if (!seg.IsString() || seg.strVal == "root") continue;
                    module += (module.empty() ? "module." : ".module.") + seg.strVal;
                }
            }
            const JVal* res = m.Find("resources");
            if (!res || !res->IsObject()) continue;
            for (const auto& kv : res->objVal) {
                ++resources;
                ++instances;
                const std::string& addr = kv.first;
                std::string mode = (addr.rfind("data.", 0) == 0) ? "data" : "managed";
                std::string type = kv.second.GetString("type");
                size_t lastDot = addr.rfind('.');
                std::string name = (lastDot == std::string::npos) ? addr : addr.substr(lastDot + 1);
                addRow(module.empty() ? addr : module + "." + addr, mode, type, name, kv.second.GetString("provider"),
                       Cell(Path(&kv.second, { "primary", "id" })), module);
            }
        }
    }
    if (!sawResources && !root.Find("terraform_version") && !root.Find("lineage")) return false;
    if (!complete && resources == 0) return false;

    std::string tfVer = root.GetString("terraform_version");
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Terraform Infrastructure State (.tfstate" + (tfVer.empty() ? L"" : L" — v" + W(tfVer)) + L")";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));

    SpreadsheetSheet summary;
    summary.name = L"State";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"State format version", Cell(root.Find("version")) });
    summary.rows.push_back({ L"Terraform version", W(tfVer) });
    summary.rows.push_back({ L"Serial", Cell(root.Find("serial")) });
    summary.rows.push_back({ L"Lineage", Cell(root.Find("lineage")) });
    summary.rows.push_back({ L"Resources", std::to_wstring(resources) });
    summary.rows.push_back({ L"Instances", std::to_wstring(instances) });
    for (const auto& tc : byType) summary.rows.push_back({ L"Type: " + W(tc.first), std::to_wstring(tc.second) });
    if (const JVal* outputs = root.Find("outputs"); outputs && outputs->IsObject()) {
        for (const auto& kv : outputs->objVal) {
            bool sensitive = kv.second.GetBool("sensitive", false);
            summary.rows.push_back({ L"Output: " + W(kv.first), sensitive ? L"(sensitive)" : Cell(kv.second.Find("value")) });
        }
    }
    NoteIncomplete(summary, complete, L"resources");
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace DetailCloudSecOps

inline bool ParseTerraformStateDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailCloudSecOps;
    std::string_view text = ::Explorer2::Json::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    size_t firstPos = text.find_first_not_of(" \t\r\n");
    char first = (firstPos == std::string_view::npos) ? '\0' : text[firstPos];
    return first == '{' && ParseTerraformState(text, out);
}

} // namespace Formats
