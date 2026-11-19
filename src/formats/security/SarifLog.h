#pragma once
// SarifLog.h — OASIS SARIF 2.1.0 Static Analysis Log (.sarif) Reader

namespace Formats {
namespace DetailCloudSecOps {

// ---- SARIF 2.1.0 (runs[] with tool.driver.rules and results[]) ----

// "{0} is unused" + arguments -> "x is unused"
inline std::string ExpandSarifArguments(const std::string& tmpl, const JVal* args) {
    if (!args || !args->IsArray()) return tmpl;
    std::string s;
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '{') {
            size_t close = tmpl.find('}', i + 1);
            if (close != std::string::npos && close > i + 1 && close - i <= 4) {
                size_t idx = 0;
                bool digits = true;
                for (size_t k = i + 1; k < close; ++k) {
                    if (tmpl[k] < '0' || tmpl[k] > '9') { digits = false; break; }
                    idx = idx * 10 + static_cast<size_t>(tmpl[k] - '0');
                }
                if (digits && idx < args->arrVal.size()) {
                    s += args->arrVal[idx].IsString() ? args->arrVal[idx].strVal : ::Explorer2::Json::Compact(args->arrVal[idx], 80);
                    i = close;
                    continue;
                }
            }
        }
        s.push_back(tmpl[i]);
    }
    return s;
}

inline bool ParseSarifLog(std::string_view text, ParsedOfficeDocument& out) {
    SpreadsheetSheet sh;
    sh.name = L"SARIF_Findings";
    sh.rows.push_back({ L"Level", L"Rule", L"File", L"Line", L"Message", L"Tool" });
    std::map<std::string, size_t> levelCounts;
    std::vector<std::wstring> runTools;
    size_t results = 0;

    struct RunCtx {
        JVal tool, artifacts;
        bool haveTool = false;
        std::wstring toolName;
        std::map<std::string, const JVal*, std::less<>> rulesById;
        const JVal* rules = nullptr;
    };
    auto setTool = [](RunCtx& ctx, JVal&& tool) {
        ctx.tool = std::move(tool);
        ctx.haveTool = true;
        const JVal* driver = ctx.tool.Find("driver");
        std::string name = driver ? driver->GetString("name") : std::string();
        std::string ver = driver ? driver->GetString("semanticVersion", driver->GetString("version")) : std::string();
        ctx.toolName = W(name) + (ver.empty() ? L"" : L" " + W(ver));
        ctx.rules = driver ? driver->Find("rules") : nullptr;
        if (ctx.rules && ctx.rules->IsArray()) {
            for (const auto& r : ctx.rules->arrVal) {
                std::string id = r.GetString("id");
                if (!id.empty()) ctx.rulesById.emplace(std::move(id), &r);
            }
        }
    };
    auto ruleFor = [](const RunCtx& ctx, const JVal& res) -> const JVal* {
        if (!ctx.rules || !ctx.rules->IsArray()) return nullptr;
        int idx = res.GetInt("ruleIndex", -1);
        if (idx < 0) {
            if (const JVal* rr = res.Find("rule")) idx = rr->GetInt("index", -1);
        }
        if (idx >= 0 && static_cast<size_t>(idx) < ctx.rules->arrVal.size()) return &ctx.rules->arrVal[static_cast<size_t>(idx)];
        std::string id = res.GetString("ruleId");
        if (id.empty()) {
            if (const JVal* rr = res.Find("rule")) id = rr->GetString("id");
        }
        auto it = ctx.rulesById.find(id);
        return (it == ctx.rulesById.end()) ? nullptr : it->second;
    };
    auto handleResult = [&](const RunCtx& ctx, const JVal& r, bool addRow) {
        if (!r.IsObject()) return;
        ++results;
        const JVal* rule = ruleFor(ctx, r);
        // SARIF 3.27.10: the result's level, else "none" for kinds other than fail, else the rule's default, else warning
        std::string level = r.GetString("level");
        if (level.empty()) {
            std::string kind = r.GetString("kind");
            if (!kind.empty() && kind != "fail") level = "none";
            else if (rule) level = Str(Path(rule, { "defaultConfiguration", "level" }));
            if (level.empty()) level = "warning";
        }
        ++levelCounts[level];
        if (!addRow || sh.rows.size() > kMaxRows) return;

        std::string ruleId = r.GetString("ruleId");
        if (ruleId.empty()) ruleId = Str(Path(&r, { "rule", "id" }));
        if (ruleId.empty() && rule) ruleId = rule->GetString("id");
        std::string msg;
        if (const JVal* m = r.Find("message"); m && m->IsObject()) {
            msg = m->GetString("text");
            if (msg.empty() && rule) {
                std::string mid = m->GetString("id");
                if (!mid.empty()) msg = Str(Path(rule, { "messageStrings", mid, "text" }));
            }
            msg = ExpandSarifArguments(msg, m->Find("arguments"));
        }
        std::string uri, line;
        if (const JVal* locs = r.Find("locations"); locs && locs->IsArray() && !locs->arrVal.empty()) {
            const JVal& loc = locs->arrVal[0];
            if (const JVal* pl = loc.Find("physicalLocation")) {
                if (const JVal* al = pl->Find("artifactLocation")) {
                    uri = al->GetString("uri");
                    int ai = al->GetInt("index", -1);
                    if (uri.empty() && ai >= 0 && ctx.artifacts.IsArray() && static_cast<size_t>(ai) < ctx.artifacts.arrVal.size()) {
                        uri = Str(Path(&ctx.artifacts.arrVal[static_cast<size_t>(ai)], { "location", "uri" }));
                    }
                }
                if (const JVal* reg = pl->Find("region")) {
                    int sl = reg->GetInt("startLine", -1), sc = reg->GetInt("startColumn", -1);
                    if (sl > 0) line = std::to_string(sl) + (sc > 0 ? ":" + std::to_string(sc) : std::string());
                }
            }
            if (uri.empty()) {
                if (const JVal* ll = loc.Find("logicalLocations"); ll && ll->IsArray() && !ll->arrVal.empty()) {
                    uri = ll->arrVal[0].GetString("fullyQualifiedName", ll->arrVal[0].GetString("name"));
                }
            }
        }
        for (char& c : msg) {
            if (c == '\r' || c == '\n') c = ' ';
        }
        sh.rows.push_back({ W(level), W(ruleId), W(uri), W(line), W(msg), ctx.toolName });
    };

    JParser parser(text, StreamLimits());
    JVal root;
    root.kind = JVal::Kind::Object;
    bool sawRuns = false;
    size_t runCount = 0;
    bool complete = parser.StreamObject([&](const std::string& key) {
        if (key == "runs" && !sawRuns && parser.Peek() == '[') {
            sawRuns = true;
            return parser.StreamArrayItems([&]() {
                if (parser.Peek() != '{') {
                    JVal skipped;
                    return parser.ParseValue(skipped);
                }
                ++runCount;
                RunCtx ctx;
                std::vector<JVal> pending; // results seen before the run's tool (rules) was
                bool ok = parser.StreamObject([&](const std::string& rk) {
                    if (rk == "results" && parser.Peek() == '[') {
                        return parser.StreamArray([&](JVal&& r) {
                            if (ctx.haveTool) handleResult(ctx, r, true);
                            else if (pending.size() < kMaxRows) pending.push_back(std::move(r));
                            else handleResult(ctx, r, false);
                        });
                    }
                    JVal v;
                    if (!parser.ParseValue(v)) return false;
                    if (rk == "tool" && !ctx.haveTool) setTool(ctx, std::move(v));
                    else if (rk == "artifacts") ctx.artifacts = std::move(v);
                    return true;
                }, 2);
                for (const auto& r : pending) handleResult(ctx, r, true);
                runTools.push_back(ctx.toolName);
                return ok;
            });
        }
        JVal v;
        if (!parser.ParseValue(v)) return false;
        root.objVal.emplace_back(key, std::move(v));
        return true;
    }) && parser.AtEnd();
    if (!sawRuns || (!complete && results == 0)) return false;

    std::wstring firstTool = runTools.empty() ? std::wstring() : runTools[0];
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"OASIS SARIF Static Analysis Report (" + (firstTool.empty() ? std::wstring(L".sarif") : firstTool) + L")";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));

    SpreadsheetSheet summary;
    summary.name = L"SARIF_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"SARIF version", W(root.GetString("version")) });
    summary.rows.push_back({ L"Runs", std::to_wstring(runCount) });
    for (size_t i = 0; i < runTools.size() && i < 50; ++i) {
        summary.rows.push_back({ L"Run " + std::to_wstring(i + 1) + L" tool", runTools[i].empty() ? L"(unnamed)" : runTools[i] });
    }
    summary.rows.push_back({ L"Results", std::to_wstring(results) + (results > kMaxRows ? L" (first " + std::to_wstring(kMaxRows) + L" listed)" : L"") });
    for (const auto& lc : levelCounts) summary.rows.push_back({ L"Level: " + W(lc.first), std::to_wstring(lc.second) });
    NoteIncomplete(summary, complete, L"results");
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace DetailCloudSecOps

inline bool ParseSarifLogDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailCloudSecOps;
    std::string_view text = ::Explorer2::Json::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    size_t firstPos = text.find_first_not_of(" \t\r\n");
    char first = (firstPos == std::string_view::npos) ? '\0' : text[firstPos];
    return first == '{' && ParseSarifLog(text, out);
}

} // namespace Formats
