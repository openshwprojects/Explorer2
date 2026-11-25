#pragma once
// CycloneDxSbom.h — OWASP CycloneDX Software Bill of Materials (.cdx) Reader

namespace Formats {
namespace DetailCloudSecOps {

// CycloneDX XML: <bom><metadata>..</metadata><components><component type=".."><name/>..</component></components>
inline void CycloneDxXmlComponents(std::string_view body, SpreadsheetSheet& sh, size_t& count, int depth) {
    namespace X = ::Explorer2::XmlTagScan;
    if (depth > 16) return;
    X::ForEachChild(body, [&](std::string_view name, const X::Element& e) {
        if (name != "component") return true;
        ++count;
        SbomRow r;
        std::string group = X::ChildText(e.body, "group");
        r.name = group.empty() ? X::ChildText(e.body, "name") : group + "/" + X::ChildText(e.body, "name");
        r.version = X::ChildText(e.body, "version");
        r.type = X::AttrText(e.attrs, "type");
        X::Element lics;
        if (X::FindChild(e.body, "licenses", lics)) {
            X::ForEachChild(lics.body, [&](std::string_view ln, const X::Element& le) {
                if (ln == "expression") AppendJoined(r.license, std::string(X::TrimView(X::DecodeText(le.body))), ", ");
                else if (ln == "license") {
                    std::string one = X::ChildText(le.body, "id");
                    if (one.empty()) one = X::ChildText(le.body, "name");
                    AppendJoined(r.license, one, ", ");
                }
                return true;
            });
        }
        r.locator = X::ChildText(e.body, "purl");
        X::Element sup;
        if (X::FindChild(e.body, "supplier", sup)) r.supplier = X::ChildText(sup.body, "name");
        if (r.supplier.empty()) r.supplier = X::ChildText(e.body, "publisher");
        PushSbomRow(sh, r);
        X::Element nested;
        if (X::FindChild(e.body, "components", nested)) CycloneDxXmlComponents(nested.body, sh, count, depth + 1);
        return true;
    });
}

inline bool ParseCycloneDxXml(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    std::string xml = X::DocumentToUtf8(raw.data(), raw.size());
    X::Element bom;
    if (!X::FindChild(xml, "bom", bom)) return false;
    SpreadsheetSheet sh = NewSbomSheet();
    size_t count = 0;
    X::Element comps;
    if (X::FindChild(bom.body, "components", comps)) CycloneDxXmlComponents(comps.body, sh, count, 0);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"CycloneDX Software Bill of Materials (.cdx)";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));
    SpreadsheetSheet summary;
    summary.name = L"SBOM_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    std::string ns = X::AttrText(bom.attrs, "xmlns");
    size_t slash = ns.rfind('/');
    summary.rows.push_back({ L"Specification", L"CycloneDX " + W(slash == std::string::npos ? ns : ns.substr(slash + 1)) });
    summary.rows.push_back({ L"Serial number", W(X::AttrText(bom.attrs, "serialNumber")) });
    X::Element meta, subject;
    if (X::FindChild(bom.body, "metadata", meta) && X::FindChild(meta.body, "component", subject)) {
        summary.rows.push_back({ L"Subject", W(X::ChildText(subject.body, "name")) + L" " + W(X::ChildText(subject.body, "version")) });
    }
    summary.rows.push_back({ L"Components", std::to_wstring(count) });
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace DetailCloudSecOps

inline bool ParseCycloneDxSbomDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailCloudSecOps;
    std::string_view text = ::Explorer2::Json::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    size_t firstPos = text.find_first_not_of(" \t\r\n");
    char first = (firstPos == std::string_view::npos) ? '\0' : text[firstPos];
    if (first == '{') return ParseSbomJson(text, out, true);
    if (first == '<') return ParseCycloneDxXml(raw, out);
    return false;
}

} // namespace Formats
