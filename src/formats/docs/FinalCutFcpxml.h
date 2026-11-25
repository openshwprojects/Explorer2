#pragma once
// FinalCutFcpxml.h — Final Cut Pro XML Timeline (.fcpxml) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseFcpXml(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view xml(reinterpret_cast<const char*>(raw.data()), raw.size());
    size_t root = xml.find("<fcpxml");
    if (root == std::string_view::npos) return false;
    size_t rGt = xml.find('>', root);
    if (rGt == std::string_view::npos) return false;
    std::string ver = ExtractXmlAttr(xml.substr(root, rGt - root), "version");

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Final Cut Pro XML (.fcpxml v" + PathUtils::Utf8ToWide(ver) + L")";

    SpreadsheetSheet assets;
    assets.name = L"Assets_&_Formats";
    assets.rows.push_back({ L"Kind", L"ID", L"Name", L"Source / Resolution", L"Duration" });

    size_t p = 0;
    while ((p = xml.find("<format ", p)) != std::string_view::npos) {
        size_t gt = xml.find('>', p);
        if (gt == std::string_view::npos) break;
        std::string_view tag = xml.substr(p, gt - p);
        std::string w = ExtractXmlAttr(tag, "width");
        std::string h = ExtractXmlAttr(tag, "height");
        assets.rows.push_back({
            L"Format",
            PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "id")),
            ::Explorer2::XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "name"))),
            PathUtils::Utf8ToWide(w + "x" + h),
            PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "frameDuration"))
        });
        p = gt + 1;
    }

    p = 0;
    while ((p = xml.find("<asset ", p)) != std::string_view::npos) {
        size_t gt = xml.find('>', p);
        if (gt == std::string_view::npos) break;
        std::string_view tag = xml.substr(p, gt - p);
        std::string src = ExtractXmlAttr(tag, "src");
        if (src.empty()) {
            size_t cl = xml.find("</asset>", gt);
            if (cl != std::string_view::npos) {
                size_t mr = xml.substr(gt, cl - gt).find("<media-rep");
                if (mr != std::string_view::npos) {
                    size_t mrGt = xml.find('>', gt + mr);
                    if (mrGt != std::string_view::npos) src = ExtractXmlAttr(xml.substr(gt + mr, mrGt - (gt + mr)), "src");
                }
            }
        }
        assets.rows.push_back({
            L"Asset",
            PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "id")),
            ::Explorer2::XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "name"))),
            ::Explorer2::XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(src)),
            PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "duration"))
        });
        p = gt + 1;
    }

    SpreadsheetSheet clips;
    clips.name = L"Timeline_Clips";
    clips.rows.push_back({ L"Element", L"Name", L"Ref", L"Offset", L"Start", L"Duration" });
    static const char* kClipTags[] = { "asset-clip", "clip", "mc-clip", "video", "audio", "title" };
    for (const char* ct : kClipTags) {
        std::string openT = std::string("<") + ct + " ";
        size_t cp = 0;
        while ((cp = xml.find(openT, cp)) != std::string_view::npos && clips.rows.size() < 2048) {
            size_t gt = xml.find('>', cp);
            if (gt == std::string_view::npos) break;
            std::string_view tag = xml.substr(cp, gt - cp);
            clips.rows.push_back({
                PathUtils::Utf8ToWide(ct),
                ::Explorer2::XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "name"))),
                PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "ref")),
                PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "offset")),
                PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "start")),
                PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "duration"))
            });
            cp = gt + 1;
        }
    }

    FinalizeSheetCols(assets);
    out.sheets.push_back(std::move(assets));
    if (clips.rows.size() > 1) { FinalizeSheetCols(clips); out.sheets.push_back(std::move(clips)); }
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseFinalCutFcpxmlDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseFcpXml(raw, out); }

} // namespace Formats
