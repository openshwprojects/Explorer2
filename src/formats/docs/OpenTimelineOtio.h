#pragma once
// OpenTimelineOtio.h — OpenTimelineIO JSON (.otio) Reader

namespace Formats {
namespace DetailMedia100 {

// OpenTimelineIO (.otio): a JSON tree of schema objects, Timeline -> tracks (Stack) -> Track -> Clip / Gap /
// Transition / nested Stack, with markers on any item. Walked in document order; each item is one row, indented
// by its depth, with its own source range (value@rate) and its media reference.
inline void OtioWalk(const ::Explorer2::Json::Value& node, int depth, SpreadsheetSheet& sh, std::map<std::string, size_t>& counts) {
    using JVal = ::Explorer2::Json::Value;
    if (!node.IsObject() || depth > 32) return;
    std::string schema = node.GetString("OTIO_SCHEMA");
    std::string family = schema.substr(0, schema.find('.'));
    if (family.empty()) return;
    ++counts[family];

    auto rationalTime = [](const JVal* rt) -> std::string { // { value, rate } -> "value@rate"
        const JVal* v = rt ? rt->Find("value") : nullptr;
        if (!v || !v->IsNumber()) return {};
        const JVal* r = rt->Find("rate");
        return v->strVal + ((r && r->IsNumber()) ? "@" + r->strVal : std::string());
    };
    const JVal* range = node.Find(family == "Marker" ? "marked_range" : "source_range");
    std::string start = range ? rationalTime(range->Find("start_time")) : std::string();
    std::string duration = range ? rationalTime(range->Find("duration")) : std::string();
    std::string detail;
    if (family == "Track") {
        detail = node.GetString("kind");
    } else if (family == "Clip") {
        const JVal* ref = node.Find("media_reference");
        if (!ref) { // Clip.2: a map of references and the key of the active one
            const JVal* refs = node.Find("media_references");
            if (refs) ref = refs->Find(node.GetString("active_media_reference_key", "DEFAULT_MEDIA"));
        }
        if (ref && ref->IsObject()) {
            detail = ref->GetString("target_url");
            if (detail.empty() && !ref->GetString("target_url_base").empty()) {
                detail = ref->GetString("target_url_base") + ref->GetString("name_prefix") + "#" + ref->GetString("name_suffix");
            }
            if (detail.empty()) detail = ref->GetString("OTIO_SCHEMA");
        }
    } else if (family == "Transition") {
        detail = node.GetString("transition_type");
        std::string in = rationalTime(node.Find("in_offset")), outOff = rationalTime(node.Find("out_offset"));
        if (!in.empty() || !outOff.empty()) detail += " (in " + in + ", out " + outOff + ")";
    } else if (family == "Marker") {
        detail = node.GetString("color");
    }
    if (family != "TimeRange" && family != "RationalTime" && sh.rows.size() <= 5000) {
        sh.rows.push_back({ std::wstring(static_cast<size_t>(depth) * 2, L' ') + PathUtils::Utf8ToWide(schema),
                            PathUtils::Utf8ToWide(node.GetString("name")), PathUtils::Utf8ToWide(detail),
                            PathUtils::Utf8ToWide(start), PathUtils::Utf8ToWide(duration) });
    }
    if (const JVal* tracks = node.Find("tracks")) OtioWalk(*tracks, depth + 1, sh, counts);
    if (const JVal* children = node.Find("children"); children && children->IsArray()) {
        for (const auto& ch : children->arrVal) OtioWalk(ch, depth + 1, sh, counts);
    }
    if (const JVal* markers = node.Find("markers"); markers && markers->IsArray()) {
        for (const auto& m : markers->arrVal) OtioWalk(m, depth + 1, sh, counts);
    }
}

inline bool ParseOpenTimelineOtio(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using JVal = ::Explorer2::Json::Value;
    JVal root;
    ::Explorer2::Json::Limits limits;
    limits.maxNodes = 4000000;
    std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (!::Explorer2::Json::ParseDocument(text, root, limits) || !root.IsObject()) return false;
    if (root.GetString("OTIO_SCHEMA").empty()) return false;

    SpreadsheetSheet tlSh;
    tlSh.name = L"OTIO_Timeline";
    tlSh.rows.push_back({ L"Schema", L"Name", L"Kind / URL", L"Start", L"Duration" });
    std::map<std::string, size_t> counts;
    OtioWalk(root, 0, tlSh, counts);
    if (tlSh.rows.size() <= 1) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"OpenTimelineIO JSON (.otio)";
    FinalizeSheetCols(tlSh);
    out.sheets.push_back(std::move(tlSh));

    SpreadsheetSheet summary;
    summary.name = L"OTIO_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Root", PathUtils::Utf8ToWide(root.GetString("OTIO_SCHEMA")) + L" " + PathUtils::Utf8ToWide(root.GetString("name")) });
    if (const JVal* gst = root.Find("global_start_time"); gst && gst->IsObject()) {
        const JVal* v = gst->Find("value");
        const JVal* r = gst->Find("rate");
        if (v && v->IsNumber()) {
            summary.rows.push_back({ L"Global start time", PathUtils::Utf8ToWide(v->strVal + ((r && r->IsNumber()) ? "@" + r->strVal : std::string())) });
        }
    }
    for (const auto& kv : counts) summary.rows.push_back({ PathUtils::Utf8ToWide(kv.first), std::to_wstring(kv.second) });
    FinalizeSheetCols(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseOpenTimelineOtioDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseOpenTimelineOtio(raw, out); }

} // namespace Formats
