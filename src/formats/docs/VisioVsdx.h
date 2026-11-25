#pragma once
// VisioVsdx.h — Microsoft Visio Drawing Package (.vsdx) Structural Shape & Connector Reader

namespace Formats {
namespace DetailVisioVsdx {

inline std::wstring ExtractVisioText(std::string_view textBody) {
    namespace X = ::Explorer2::XmlTagScan;
    std::string out;
    size_t pos = 0;
    X::XmlTag t;
    size_t lastEnd = 0;
    while (X::NextTag(textBody, pos, t)) {
        if (t.tagStart > lastEnd) {
            out += X::DecodeText(textBody.substr(lastEnd, t.tagStart - lastEnd));
        }
        lastEnd = t.afterTag;
    }
    if (lastEnd < textBody.size()) {
        out += X::DecodeText(textBody.substr(lastEnd));
    }
    return PathUtils::Trim(PathUtils::Utf8ToWide(out));
}

inline void ScanVisioCellsInBlock(
    std::string_view block,
    std::string& pinX,
    std::string& pinY,
    std::string& width,
    std::string& height,
    std::string& label)
{
    namespace X = ::Explorer2::XmlTagScan;
    X::ForEachChild(block, [&](std::string_view name, const X::Element& el) {
        if (name == "Cell") {
            std::string n = X::AttrText(el.attrs, "N");
            std::string v = X::AttrText(el.attrs, "V");
            if (v.empty()) v = X::ChildText(el.body, "V");
            if (n == "PinX") pinX = std::move(v);
            else if (n == "PinY") pinY = std::move(v);
            else if (n == "Width") width = std::move(v);
            else if (n == "Height") height = std::move(v);
            else if (n == "Label" && label.empty()) label = std::move(v);
        } else if (name == "Section") {
            X::ForEachChild(el.body, [&](std::string_view sn, const X::Element& sel) {
                if (sn == "Cell") {
                    std::string cn = X::AttrText(sel.attrs, "N");
                    std::string cv = X::AttrText(sel.attrs, "V");
                    if (cn == "PinX" && pinX.empty()) pinX = std::move(cv);
                    else if (cn == "PinY" && pinY.empty()) pinY = std::move(cv);
                    else if (cn == "Width" && width.empty()) width = std::move(cv);
                    else if (cn == "Height" && height.empty()) height = std::move(cv);
                    else if ((cn == "Label" || cn == "Prompt" || cn == "Value") && label.empty()) label = std::move(cv);
                } else if (sn == "Row") {
                    std::string rn = X::AttrText(sel.attrs, "N");
                    X::ForEachChild(sel.body, [&](std::string_view rcn, const X::Element& rcel) {
                        if (rcn == "Cell") {
                            std::string cn = X::AttrText(rcel.attrs, "N");
                            std::string cv = X::AttrText(rcel.attrs, "V");
                            if ((cn == "Label" || (rn == "Label" && cn == "Value")) && label.empty() && !cv.empty()) {
                                label = std::move(cv);
                            }
                        }
                        return true;
                    });
                }
                return true;
            });
        }
        return true;
    });
}

inline void ParseVisioShapesRecursive(
    std::string_view shapesBody,
    const std::wstring& pageLabel,
    const std::wstring& parentPrefix,
    SpreadsheetSheet& shapeSh,
    std::unordered_map<std::string, std::wstring>& shapeIdToName,
    int depth)
{
    namespace X = ::Explorer2::XmlTagScan;
    if (depth > 12 || shapeSh.rows.size() > 2000) return;

    X::ForEachChild(shapesBody, [&](std::string_view tagName, const X::Element& shEl) {
        if (tagName != "Shape" || shapeSh.rows.size() > 2000) return true;
        const std::string id    = X::AttrText(shEl.attrs, "ID");
        const std::string nameU = X::AttrText(shEl.attrs, "NameU");
        const std::string name  = X::AttrText(shEl.attrs, "Name");
        const std::string type  = X::AttrText(shEl.attrs, "Type");
        const std::string mast  = X::AttrText(shEl.attrs, "Master");

        std::string pinX, pinY, width, height, cellLabel;
        ScanVisioCellsInBlock(shEl.body, pinX, pinY, width, height, cellLabel);

        std::wstring textVal;
        X::Element txtEl;
        if (X::FindChild(shEl.body, "Text", txtEl)) {
            textVal = ExtractVisioText(txtEl.body);
        }
        if (textVal.empty() && !cellLabel.empty()) {
            textVal = PathUtils::Utf8ToWide(cellLabel);
        }

        std::string dispNameUtf8 = !name.empty() ? name : (!nameU.empty() ? nameU : (!mast.empty() ? ("Master#" + mast) : ("Shape#" + id)));
        std::wstring dispName = parentPrefix + PathUtils::Utf8ToWide(dispNameUtf8);
        if (!id.empty()) {
            shapeIdToName[id] = dispName + L" (#" + PathUtils::Utf8ToWide(id) + L")";
        }

        std::string geom;
        if (!pinX.empty() || !pinY.empty()) geom = "(" + pinX + ", " + pinY + ")";
        if (!width.empty() || !height.empty()) {
            if (!geom.empty()) geom += " ";
            geom += width + "x" + height;
        }

        shapeSh.rows.push_back({
            pageLabel,
            PathUtils::Utf8ToWide(id),
            dispName,
            PathUtils::Utf8ToWide(type.empty() ? "Shape" : type),
            PathUtils::Utf8ToWide(geom),
            textVal
        });

        X::Element childShapes;
        if (X::FindChild(shEl.body, "Shapes", childShapes)) {
            ParseVisioShapesRecursive(childShapes.body, pageLabel, dispName + L" / ", shapeSh, shapeIdToName, depth + 1);
        }
        return true;
    });
}

} // namespace DetailVisioVsdx

inline bool ParseVisioVsdxDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if (ext != L".vsdx" && ext != L".vsdm" && ext != L".vstx" && ext != L".vstm") return false;

    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    bool usedFile = false;
    if (!DetailStructuredDataDoc::ListZipPackage(raw, out, entries, usedFile)) return false;

    namespace X = ::Explorer2::XmlTagScan;
    std::unordered_map<std::wstring, const ZipDeflate::ArchiveItemMeta*> byLowerPath;
    bool hasVisioPart = false;
    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        std::wstring lp = PathUtils::ToLower(e.pathInArchive);
        std::replace(lp.begin(), lp.end(), L'\\', L'/');
        if (!lp.empty() && lp.front() == L'/') lp.erase(lp.begin());
        if (lp.find(L"visio/") == 0) hasVisioPart = true;
        byLowerPath[lp] = &e;
    }
    if (!hasVisioPart) return false;

    // 1. Resolve relationship IDs from visio/pages/_rels/pages.xml.rels
    std::unordered_map<std::string, std::wstring> relToTarget;
    if (auto it = byLowerPath.find(L"visio/pages/_rels/pages.xml.rels"); it != byLowerPath.end()) {
        std::vector<uint8_t> relBytes;
        if (DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, *it->second, relBytes) && !relBytes.empty()) {
            std::string rxml = X::DocumentToUtf8(relBytes.data(), relBytes.size());
            X::Element relsRoot;
            if (X::FindChild(rxml, "Relationships", relsRoot)) {
                X::ForEachChild(relsRoot.body, [&](std::string_view rn, const X::Element& rel) {
                    if (rn == "Relationship") {
                        std::string rid = X::AttrText(rel.attrs, "Id");
                        std::string tgt = X::AttrText(rel.attrs, "Target");
                        if (!rid.empty() && !tgt.empty()) {
                            std::wstring wtgt = PathUtils::ToLower(PathUtils::Utf8ToWide(tgt));
                            std::replace(wtgt.begin(), wtgt.end(), L'\\', L'/');
                            if (!wtgt.empty() && wtgt.front() == L'/') wtgt.erase(wtgt.begin());
                            else if (wtgt.find(L"visio/") != 0) wtgt = L"visio/pages/" + wtgt;
                            relToTarget[rid] = std::move(wtgt);
                        }
                    }
                    return true;
                });
            }
        }
    }

    // 2. Parse visio/pages/pages.xml for ordered page titles & targets
    struct PageTask { std::wstring label; const ZipDeflate::ArchiveItemMeta* entry = nullptr; };
    std::vector<PageTask> pages;
    std::unordered_set<const ZipDeflate::ArchiveItemMeta*> queuedEntries;

    if (auto it = byLowerPath.find(L"visio/pages/pages.xml"); it != byLowerPath.end()) {
        std::vector<uint8_t> pxmlBytes;
        if (DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, *it->second, pxmlBytes) && !pxmlBytes.empty()) {
            std::string pxml = X::DocumentToUtf8(pxmlBytes.data(), pxmlBytes.size());
            X::Element pagesRoot;
            if (X::FindChild(pxml, "Pages", pagesRoot)) {
                X::ForEachChild(pagesRoot.body, [&](std::string_view pn, const X::Element& pel) {
                    if (pn != "Page") return true;
                    std::string pName  = X::AttrText(pel.attrs, "Name");
                    std::string pNameU = X::AttrText(pel.attrs, "NameU");
                    std::string pId    = X::AttrText(pel.attrs, "ID");
                    std::string label8 = !pName.empty() ? pName : (!pNameU.empty() ? pNameU : ("Page " + pId));
                    X::Element relEl;
                    std::string rid;
                    if (X::FindChild(pel.body, "Rel", relEl)) {
                        rid = X::AttrText(relEl.attrs, "id");
                    }
                    auto rit = relToTarget.find(rid);
                    if (rit != relToTarget.end()) {
                        auto eit = byLowerPath.find(rit->second);
                        if (eit != byLowerPath.end() && queuedEntries.insert(eit->second).second) {
                            pages.push_back({ PathUtils::Utf8ToWide(label8), eit->second });
                        }
                    }
                    return true;
                });
            }
        }
    }

    // Fallback: collect any remaining visio/pages/page*.xml parts
    for (const auto& e : entries) {
        if (e.isDirectory || queuedEntries.count(&e)) continue;
        std::wstring lp = PathUtils::ToLower(e.pathInArchive);
        std::replace(lp.begin(), lp.end(), L'\\', L'/');
        if (lp.find(L"visio/pages/page") != std::wstring::npos && PathUtils::GetExtension(lp) == L".xml") {
            queuedEntries.insert(&e);
            pages.push_back({ e.name, &e });
        }
    }

    SpreadsheetSheet shapeSh;
    shapeSh.name = L"Visio_Shapes";
    shapeSh.maxCols = 6;
    shapeSh.rows.push_back({ L"Page", L"Shape ID", L"Name / Master", L"Type", L"Geometry (Pin / Size)", L"Text / Label" });

    SpreadsheetSheet connSh;
    connSh.name = L"Visio_Connections";
    connSh.maxCols = 5;
    connSh.rows.push_back({ L"Page", L"From Shape", L"From Cell", L"To Shape", L"To Cell" });

    for (const auto& pg : pages) {
        std::vector<uint8_t> pgBytes;
        if (!pg.entry || !DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, *pg.entry, pgBytes) || pgBytes.empty()) {
            continue;
        }
        std::string xml = X::DocumentToUtf8(pgBytes.data(), pgBytes.size());
        X::Element pageContents;
        std::string_view rootBody = xml;
        if (X::FindChild(xml, "PageContents", pageContents)) {
            rootBody = pageContents.body;
        }

        std::unordered_map<std::string, std::wstring> shapeIdToName;
        X::Element shapesEl;
        if (X::FindChild(rootBody, "Shapes", shapesEl)) {
            DetailVisioVsdx::ParseVisioShapesRecursive(shapesEl.body, pg.label, L"", shapeSh, shapeIdToName, 0);
        }

        X::Element connsEl;
        if (X::FindChild(rootBody, "Connects", connsEl)) {
            X::ForEachChild(connsEl.body, [&](std::string_view cn, const X::Element& cel) {
                if (cn != "Connect" || connSh.rows.size() > 2000) return true;
                std::string fromSheet = X::AttrText(cel.attrs, "FromSheet");
                std::string fromCell  = X::AttrText(cel.attrs, "FromCell");
                std::string toSheet   = X::AttrText(cel.attrs, "ToSheet");
                std::string toCell    = X::AttrText(cel.attrs, "ToCell");
                auto fmtSheet = [&](const std::string& sid) -> std::wstring {
                    auto it = shapeIdToName.find(sid);
                    return (it != shapeIdToName.end()) ? it->second : (L"Shape #" + PathUtils::Utf8ToWide(sid));
                };
                connSh.rows.push_back({
                    pg.label,
                    fmtSheet(fromSheet),
                    PathUtils::Utf8ToWide(fromCell),
                    fmtSheet(toSheet),
                    PathUtils::Utf8ToWide(toCell)
                });
                return true;
            });
        }
    }

    if (shapeSh.rows.size() == 1) {
        for (const auto& e : entries) {
            if (!e.isDirectory && shapeSh.rows.size() < 200) {
                shapeSh.rows.push_back({ e.pathInArchive, L"-", L"Visio Package Part", L"Part", PathUtils::FormatByteSize(e.uncompressedSize), L"" });
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Visio Drawing Package (" + ext + L")";
    out.sheets.push_back(std::move(shapeSh));
    if (connSh.rows.size() > 1) {
        out.sheets.push_back(std::move(connSh));
    }
    out.valid = true;
    return true;
}

} // namespace Formats
