#pragma once
// UnrealAssetDoc.h — Unreal Engine 4/5 Asset Package (.uasset / .umap)

namespace Formats {

inline bool ParseUnrealAssetDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #98. Unreal Engine 4/5 Asset Package (.uasset / .umap — magic 0x9E2A83C1)
    if (ext == L".uasset" || ext == L".umap" || (raw.size() >= 24 && DetailDocs200::RdU32LE(raw.data()) == 0x9E2A83C1u)) {
        if (raw.size() < 24) return false;
        uint32_t tag = DetailDocs200::RdU32LE(raw.data());
        if (tag != 0x9E2A83C1u && tag != 0xC1832A9Eu) return false;

        int32_t legVer  = DetailDocs200::RdI32LE(raw.data() + 4);
        int32_t ue4Ver  = DetailDocs200::RdI32LE(raw.data() + 12);
        size_t pos = 16;
        int32_t ue5Ver = 0;
        if (legVer <= -8 && pos + 4 <= raw.size()) { ue5Ver = DetailDocs200::RdI32LE(raw.data() + pos); pos += 4; }
        if (pos + 8 <= raw.size()) pos += 4; // FileVersionLicenseeUE
        // Skip CustomVersionContainer (count * 20 bytes)
        if (legVer <= -2 && pos + 4 <= raw.size()) {
            uint32_t custCnt = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (custCnt < 4096 && pos + custCnt * 20ull <= raw.size()) pos += custCnt * 20ull;
        }
        uint32_t totalHdrSz = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        if (pos + 4 <= raw.size()) pos += 4;

        auto readFString = [&](size_t& p) -> std::wstring {
            if (p + 4 > raw.size()) return {};
            int32_t slen = DetailDocs200::RdI32LE(raw.data() + p); p += 4;
            if (slen > 0 && slen < 1024 && p + static_cast<size_t>(slen) <= raw.size()) {
                std::wstring s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + p), slen - 1));
                p += static_cast<size_t>(slen);
                return s;
            }
            if (slen < 0 && -slen < 1024 && p + static_cast<size_t>(-slen) * 2 <= raw.size()) {
                size_t bytes = static_cast<size_t>(-slen) * 2;
                std::wstring s = DetailDocs200::ReadUtf16LE(raw.data() + p, bytes);
                p += bytes;
                return s;
            }
            return {};
        };

        std::wstring folderName = readFString(pos);
        uint32_t pkgFlags = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        if (pos + 4 <= raw.size()) pos += 4;
        uint32_t nameCnt  = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        uint32_t nameOff  = (pos + 8 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos + 4) : 0;

        SpreadsheetSheet hdrSh;
        hdrSh.name = L"Package_Summary";
        hdrSh.maxCols = 2;
        hdrSh.rows.push_back({ L"FPackageFileSummary Field", L"Value" });
        hdrSh.rows.push_back({ L"Engine Generation", (legVer <= -8) ? (L"Unreal Engine 5 (v" + std::to_wstring(ue5Ver) + L")")
                                                                    : (L"Unreal Engine 4 (v" + std::to_wstring(ue4Ver) + L")") });
        hdrSh.rows.push_back({ L"Package Folder", folderName.empty() ? L"None" : folderName });
        hdrSh.rows.push_back({ L"Total Header Size", std::to_wstring(totalHdrSz) + L" B" });
        hdrSh.rows.push_back({ L"Package Flags", DetailSys100::Hex32(pkgFlags) });
        hdrSh.rows.push_back({ L"NameMap Count", std::to_wstring(nameCnt) + L" @ offset " + std::to_wstring(nameOff) });

        SpreadsheetSheet nmSh;
        nmSh.name = L"NameMap";
        nmSh.maxCols = 2;
        nmSh.rows.push_back({ L"Name Index", L"FName Identifier" });
        if (nameCnt > 0 && nameOff > 0 && nameOff < raw.size()) {
            size_t np = nameOff;
            for (uint32_t i = 0; i < std::min<uint32_t>(nameCnt, 300) && np + 4 <= raw.size(); ++i) {
                std::wstring nm = readFString(np);
                if (nm.empty()) break;
                if (ue4Ver >= 504 && np + 4 <= raw.size()) np += 4; // NonCasePreservingHash + CasePreservingHash (2x uint16)
                nmSh.rows.push_back({ std::to_wstring(i), nm });
            }
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (ext == L".umap") ? L"Unreal Engine Level Map (.umap)" : L"Unreal Engine Asset Package (.uasset)";
        out.sheets.push_back(std::move(hdrSh));
        if (nmSh.rows.size() > 1) out.sheets.push_back(std::move(nmSh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
