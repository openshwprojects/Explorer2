#pragma once
// SpdxSbom.h — Linux Foundation SPDX Software Bill of Materials (.spdx) Reader

namespace Formats {
namespace DetailCloudSecOps {

// SPDX tag-value: "Tag: value" lines; a value may be a multi-line <text>..</text> block
inline bool ParseSpdxTagValue(std::string_view sv, ParsedOfficeDocument& out) {
    SpreadsheetSheet sh = NewSbomSheet();
    size_t count = 0;
    std::string spdxVersion, docName, created;
    std::vector<std::string> creators;
    SbomRow cur;
    bool inPackage = false;
    auto flush = [&]() {
        if (inPackage) {
            ++count;
            PushSbomRow(sh, cur);
        }
        cur = SbomRow{};
        inPackage = false;
    };
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view line = sv.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        size_t colon = line.find(':');
        if (line.empty() || line.front() == '#' || colon == std::string_view::npos) continue;
        std::string_view tag = PathUtils::TrimAscii(line.substr(0, colon));
        std::string_view val = PathUtils::TrimAscii(line.substr(colon + 1));
        if (val.rfind("<text>", 0) == 0) { // multi-line value: skip to its end
            if (val.find("</text>") == std::string_view::npos) {
                size_t end = sv.find("</text>", pos);
                pos = (end == std::string_view::npos) ? sv.size() : sv.find('\n', end);
                if (pos == std::string_view::npos) pos = sv.size(); else ++pos;
            }
            continue;
        }
        if (tag == "SPDXVersion") spdxVersion = std::string(val);
        else if (tag == "DocumentName") docName = std::string(val);
        else if (tag == "Created") created = std::string(val);
        else if (tag == "Creator") creators.emplace_back(val);
        else if (tag == "PackageName") { flush(); inPackage = true; cur.name = std::string(val); }
        else if (!inPackage) continue;
        else if (tag == "PackageVersion") cur.version = std::string(val);
        else if (tag == "PrimaryPackagePurpose") cur.type = std::string(val);
        else if (tag == "PackageLicenseConcluded") cur.license = std::string(val);
        else if (tag == "PackageLicenseDeclared" && (cur.license.empty() || cur.license == "NOASSERTION")) cur.license = std::string(val);
        else if (tag == "PackageSupplier") cur.supplier = std::string(val);
        else if (tag == "PackageDownloadLocation" && cur.locator.empty() && val != "NOASSERTION" && val != "NONE") cur.locator = std::string(val);
        else if (tag == "ExternalRef") { // ExternalRef: PACKAGE-MANAGER purl pkg:npm/x@1
            size_t p1 = val.find(' ');
            size_t p2 = (p1 == std::string_view::npos) ? p1 : val.find(' ', p1 + 1);
            if (p2 != std::string_view::npos && val.substr(p1 + 1, p2 - p1 - 1) == "purl") {
                cur.locator = std::string(PathUtils::TrimAscii(val.substr(p2 + 1)));
            }
        }
        else if (tag == "FileName" || tag == "SnippetSPDXID" || tag == "LicenseID") flush(); // past the package's tags
    }
    flush();
    if (spdxVersion.rfind("SPDX-", 0) != 0) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Linux Foundation SPDX SBOM (.spdx)";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));
    SpreadsheetSheet summary;
    summary.name = L"SBOM_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Specification", W(spdxVersion) });
    summary.rows.push_back({ L"Document name", W(docName) });
    summary.rows.push_back({ L"Created", W(created) });
    std::string creatorList;
    for (const auto& c : creators) AppendJoined(creatorList, c, ", ");
    summary.rows.push_back({ L"Creators", W(creatorList) });
    summary.rows.push_back({ L"Packages", std::to_wstring(count) });
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace DetailCloudSecOps

inline bool ParseSpdxSbomDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailCloudSecOps;
    std::string_view text = ::Explorer2::Json::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    size_t firstPos = text.find_first_not_of(" \t\r\n");
    char first = (firstPos == std::string_view::npos) ? '\0' : text[firstPos];
    if (first == '{') return ParseSbomJson(text, out);
    if (first == '<') return false;
    return ParseSpdxTagValue(text, out);
}

} // namespace Formats
