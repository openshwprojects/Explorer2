#pragma once
// UnrealAssetDoc.h — Unreal Engine 4/5 Asset Package (.uasset / .umap) Inspector
#include "../common/UnrealPackageCommon.h"

namespace Formats {

inline bool ParseUnrealAssetDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if (ext != L".uasset" && ext != L".umap" &&
        !(raw.size() >= 24 && DetailStructuredDataDoc::RdU32LE(raw.data()) == UnrealPkgDetail::kUnrealPackageTagLE)) {
        return false;
    }

    UnrealPkgDetail::ParsedUnrealSummary sum;
    if (!UnrealPkgDetail::ParseUnrealPackageSummary(raw.data(), raw.size(), sum)) {
        return false;
    }

    std::vector<UnrealPkgDetail::ParsedUnrealImport> imports;
    std::vector<UnrealPkgDetail::ParsedUnrealExport> exports;
    UnrealPkgDetail::ParseUnrealImportAndExportTables(raw.data(), raw.size(), sum, imports, exports);

    std::vector<uint8_t> combined, ubulk;
    UnrealPkgDetail::LoadCombinedUnrealStreams(raw.data(), raw.size(), sum, out.filePath, combined, ubulk);
    const size_t hdrCopy = (sum.totalHeaderSize > 0 && static_cast<size_t>(sum.totalHeaderSize) <= raw.size())
        ? static_cast<size_t>(sum.totalHeaderSize)
        : raw.size();

    std::vector<UnrealPkgDetail::ParsedUnrealProperty> props;
    UnrealPkgDetail::ParseUnrealTaggedProperties(combined.data(), combined.size(), hdrCopy, sum, imports, exports, props);

    auto w = [](std::string_view s) { return PathUtils::Utf8ToWide(s); };

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"Package_Summary";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"FPackageFileSummary Field", L"Value" });
    hdrSh.rows.push_back({ L"Engine Generation", (sum.legacyVer <= -8)
        ? (L"Unreal Engine 5 (v" + std::to_wstring(sum.verUe5) + L", UE4 v" + std::to_wstring(sum.verUe4) + L")")
        : (L"Unreal Engine 4 (v" + std::to_wstring(sum.verUe4) + L")") });
    hdrSh.rows.push_back({ L"Package Folder", sum.folderName.empty() ? L"None" : w(sum.folderName) });
    hdrSh.rows.push_back({ L"Total Header Size", std::to_wstring(sum.totalHeaderSize) + L" B" });
    hdrSh.rows.push_back({ L"Package Flags", DetailBinarySysDoc::Hex32(sum.pkgFlags) });
    hdrSh.rows.push_back({ L"NameMap Count", std::to_wstring(sum.nameCount) + L" @ offset " + std::to_wstring(sum.nameOffset) });
    hdrSh.rows.push_back({ L"ExportMap Count", std::to_wstring(sum.exportCount) + L" @ offset " + std::to_wstring(sum.exportOffset) });
    hdrSh.rows.push_back({ L"ImportMap Count", std::to_wstring(sum.importCount) + L" @ offset " + std::to_wstring(sum.importOffset) });
    if (!ubulk.empty()) {
        hdrSh.rows.push_back({ L"Bulk Data (.ubulk)", PathUtils::FormatByteSize(ubulk.size()) });
    }

    SpreadsheetSheet expSh;
    expSh.name = L"ExportMap";
    expSh.maxCols = 7;
    expSh.rows.push_back({ L"#", L"ObjectName", L"Class", L"Super", L"Outer", L"SerialSize", L"SerialOffset" });
    for (size_t i = 0; i < exports.size() && expSh.rows.size() <= 1024; ++i) {
        const auto& ex = exports[i];
        expSh.rows.push_back({
            std::to_wstring(i + 1),
            w(ex.objectName) + (ex.isAsset ? L" [Asset]" : L""),
            w(UnrealPkgDetail::ResolveUnrealPackageIndex(ex.classIndex, imports, exports)),
            w(UnrealPkgDetail::ResolveUnrealPackageIndex(ex.superIndex, imports, exports)),
            w(UnrealPkgDetail::ResolveUnrealPackageIndex(ex.outerIndex, imports, exports)),
            std::to_wstring(ex.serialSize) + L" B",
            std::to_wstring(ex.serialOffset)
        });
    }

    SpreadsheetSheet impSh;
    impSh.name = L"ImportMap";
    impSh.maxCols = 5;
    impSh.rows.push_back({ L"#", L"ClassPackage", L"ClassName", L"ObjectName", L"Outer" });
    for (size_t i = 0; i < imports.size() && impSh.rows.size() <= 1024; ++i) {
        const auto& im = imports[i];
        impSh.rows.push_back({
            L"-" + std::to_wstring(i + 1),
            w(im.classPackage),
            w(im.className),
            w(im.objectName),
            w(UnrealPkgDetail::ResolveUnrealPackageIndex(im.outerIndex, imports, exports))
        });
    }

    SpreadsheetSheet propSh;
    propSh.name = L"UObject_Properties";
    propSh.maxCols = 4;
    propSh.rows.push_back({ L"Export Object", L"Property Name", L"Property Type", L"Value" });
    for (const auto& pr : props) {
        propSh.rows.push_back({ w(pr.exportName), w(pr.name), w(pr.type), w(pr.value) });
    }

    SpreadsheetSheet nmSh;
    nmSh.name = L"NameMap";
    nmSh.maxCols = 2;
    nmSh.rows.push_back({ L"Name Index", L"FName Identifier" });
    for (size_t i = 0; i < std::min<size_t>(sum.names.size(), 1024); ++i) {
        nmSh.rows.push_back({ std::to_wstring(i), w(sum.names[i]) });
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = (ext == L".umap") ? L"Unreal Engine Level Map (.umap)" : L"Unreal Engine Asset Package (.uasset)";
    out.sheets.push_back(std::move(hdrSh));
    if (expSh.rows.size() > 1) out.sheets.push_back(std::move(expSh));
    if (impSh.rows.size() > 1) out.sheets.push_back(std::move(impSh));
    if (propSh.rows.size() > 1) out.sheets.push_back(std::move(propSh));
    if (nmSh.rows.size() > 1) out.sheets.push_back(std::move(nmSh));
    out.valid = true;
    return true;
}

} // namespace Formats
