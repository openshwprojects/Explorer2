#pragma once
// OutlookMsg.h — Microsoft Outlook MAPI Message (.msg) Reader

namespace Formats {
namespace DetailMedia100 {

inline bool ParseOutlookMsg(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(raw.data(), raw.size(), cf)) return false;

    bool hasMapiMarker = false;
    for (const auto& e : cf.entries) {
        if (e.name.find(L"__substg1.0_") != std::wstring::npos ||
            e.name.find(L"__properties_version1.0") != std::wstring::npos ||
            e.name.find(L"__nameid_version1.0") != std::wstring::npos)
        {
            hasMapiMarker = true;
            break;
        }
    }
    if (!hasMapiMarker) return false;

    auto decodeSubstgStr = [&](const Ole2::DirEntry& e, const std::wstring& typeHex) -> std::wstring {
        std::vector<uint8_t> bytes;
        if (!Ole2::ExtractStream(cf, e, bytes, 4 * 1024 * 1024) || bytes.empty()) return {};
        if (typeHex == L"001f") {
            return Utf16BytesToWide(bytes.data(), bytes.size(), false);
        }
        if (typeHex == L"001e") {
            size_t n = bytes.size();
            while (n > 0 && bytes[n - 1] == 0) --n;
            return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(bytes.data()), n)));
        }
        return {};
    };

    std::map<std::wstring, std::wstring> rootProps;
    std::vector<uint8_t> htmlBodyBin, rtfCompressedBin;

    struct MsgRecip { std::wstring name, email; };
    struct MsgAttach { std::wstring filename, mime; uint64_t byteSize = 0; };
    std::map<std::wstring, MsgRecip> recips;
    std::map<std::wstring, MsgAttach> attachs;

    for (const auto& e : cf.entries) {
        if (e.objectType != 2 || e.fullPath.empty()) continue;
        std::wstring pathLow = PathUtils::ToLower(e.fullPath);
        size_t slash = pathLow.find(L'/');
        std::wstring parent = (slash != std::wstring::npos) ? pathLow.substr(0, slash) : L"";
        std::wstring leaf   = (slash != std::wstring::npos) ? pathLow.substr(slash + 1) : pathLow;

        if (leaf.size() == 20 && leaf.rfind(L"__substg1.0_", 0) == 0) {
            std::wstring propId   = leaf.substr(12, 4);
            std::wstring propType = leaf.substr(16, 4);

            if (parent.empty()) {
                if (propType == L"001f" || propType == L"001e") {
                    std::wstring val = decodeSubstgStr(e, propType);
                    if (!val.empty() && (propType == L"001f" || rootProps.find(propId) == rootProps.end())) {
                        rootProps[propId] = std::move(val);
                    }
                } else if (propId == L"1013" && propType == L"0102") {
                    Ole2::ExtractStream(cf, e, htmlBodyBin, 4 * 1024 * 1024);
                } else if (propId == L"1009" && propType == L"0102") {
                    Ole2::ExtractStream(cf, e, rtfCompressedBin, 8 * 1024 * 1024);
                }
            } else if (parent.rfind(L"__recip_version1.0_", 0) == 0) {
                auto& r = recips[parent];
                if (propType == L"001f" || propType == L"001e") {
                    std::wstring val = decodeSubstgStr(e, propType);
                    if (propId == L"3001" && !val.empty()) r.name = val;
                    else if ((propId == L"39fe" || propId == L"3003") && !val.empty()) r.email = val;
                }
            } else if (parent.rfind(L"__attach_version1.0_", 0) == 0) {
                auto& a = attachs[parent];
                if (propType == L"001f" || propType == L"001e") {
                    std::wstring val = decodeSubstgStr(e, propType);
                    if (propId == L"3707" && !val.empty()) a.filename = val;
                    else if (propId == L"3704" && a.filename.empty() && !val.empty()) a.filename = val;
                    else if (propId == L"370e" && !val.empty()) a.mime = val;
                } else if (propId == L"3701" && propType == L"0102") {
                    a.byteSize = e.size;
                }
            }
        }
    }

    auto getP = [&](const wchar_t* k1, const wchar_t* k2 = nullptr) -> std::wstring {
        auto it = rootProps.find(k1);
        if (it != rootProps.end() && !it->second.empty()) return it->second;
        if (k2) {
            auto it2 = rootProps.find(k2);
            if (it2 != rootProps.end()) return it2->second;
        }
        return {};
    };

    std::wstring subject    = getP(L"0037");
    std::wstring msgClass   = getP(L"001a");
    std::wstring senderName = getP(L"0c1a", L"0042");
    std::wstring senderMail = getP(L"5d01", L"0c1f");
    if (senderMail.empty()) senderMail = getP(L"0065");
    std::wstring dispTo     = getP(L"0e04");
    std::wstring dispCc     = getP(L"0e03");
    std::wstring msgId      = getP(L"1035");
    std::wstring transHdrs  = getP(L"007d");
    std::wstring plainBody  = getP(L"1000");

    if (plainBody.empty()) {
        std::wstring htmlStr = getP(L"1013");
        if (htmlStr.empty() && !htmlBodyBin.empty()) {
            htmlStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(htmlBodyBin.data()), htmlBodyBin.size()));
        }
        if (!htmlStr.empty()) {
            auto ps = StripHtmlToParagraphs(htmlStr, 200);
            for (const auto& p : ps) {
                if (!plainBody.empty()) plainBody += L"\n";
                plainBody += p;
            }
        }
    }
    if (plainBody.empty() && !rtfCompressedBin.empty()) { // only an RTF body (PR_RTF_COMPRESSED)
        std::vector<uint8_t> rtf;
        ParsedOfficeDocument rtfDoc;
        if (DecompressRtfLzfu(rtfCompressedBin, rtf) && ParseRtfDocument(rtf, rtfDoc)) {
            for (const auto& p : rtfDoc.paragraphs) {
                if (!plainBody.empty()) plainBody += L"\n";
                plainBody += p.text;
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Outlook MAPI Message (.msg)";

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"Email_Headers";
    hdrSh.rows.push_back({ L"Header / Property", L"Value" });
    if (!msgClass.empty())   hdrSh.rows.push_back({ L"Message Class", msgClass });
    if (!subject.empty())    hdrSh.rows.push_back({ L"Subject", subject });
    if (!senderName.empty()) hdrSh.rows.push_back({ L"Sender Name", senderName });
    if (!senderMail.empty()) hdrSh.rows.push_back({ L"Sender Email", senderMail });
    if (!dispTo.empty())     hdrSh.rows.push_back({ L"To", dispTo });
    if (!dispCc.empty())     hdrSh.rows.push_back({ L"CC", dispCc });
    if (!msgId.empty())      hdrSh.rows.push_back({ L"Message-ID", msgId });
    hdrSh.rows.push_back({ L"Attachments", std::to_wstring(attachs.size()) });
    if (!transHdrs.empty())  hdrSh.rows.push_back({ L"Transport Headers", transHdrs.substr(0, 512) });
    FinalizeSheetCols(hdrSh);
    out.sheets.push_back(std::move(hdrSh));

    SpreadsheetSheet bodySh;
    bodySh.name = L"Email_Body";
    bodySh.rows.push_back({ L"Line #", L"Text" });
    if (!plainBody.empty()) {
        size_t ls = 0;
        size_t lineNo = 0;
        while (ls < plainBody.size() && lineNo < 2000) {
            size_t le = plainBody.find(L'\n', ls);
            if (le == std::wstring::npos) le = plainBody.size();
            std::wstring ln = PathUtils::Trim(std::wstring_view(plainBody).substr(ls, le - ls));
            ls = (le < plainBody.size()) ? (le + 1) : plainBody.size();
            if (!ln.empty()) bodySh.rows.push_back({ std::to_wstring(++lineNo), ln });
        }
    }
    FinalizeSheetCols(bodySh);
    out.sheets.push_back(std::move(bodySh));

    if (!recips.empty()) {
        SpreadsheetSheet rSh;
        rSh.name = L"Recipients";
        rSh.rows.push_back({ L"Display Name", L"Email Address" });
        for (const auto& kv : recips) rSh.rows.push_back({ kv.second.name, kv.second.email });
        FinalizeSheetCols(rSh);
        out.sheets.push_back(std::move(rSh));
    }

    if (!attachs.empty()) {
        SpreadsheetSheet aSh;
        aSh.name = L"Attachments";
        aSh.rows.push_back({ L"Filename", L"MIME Type", L"Byte Size" });
        for (const auto& kv : attachs) {
            aSh.rows.push_back({
                kv.second.filename.empty() ? L"(attachment)" : kv.second.filename,
                kv.second.mime,
                PathUtils::FormatByteSize(kv.second.byteSize)
            });
        }
        FinalizeSheetCols(aSh);
        out.sheets.push_back(std::move(aSh));
    }

    // Also populate paragraphs for unified document text inspection
    if (!subject.empty()) out.paragraphs.push_back({ L"Subject: " + subject, 1, false, false });
    if (!senderName.empty() || !senderMail.empty()) {
        out.paragraphs.push_back({ L"From: " + senderName + (senderMail.empty() ? L"" : (L" <" + senderMail + L">")), 2, false, false });
    }
    if (!plainBody.empty()) out.paragraphs.push_back({ plainBody, 0, false, false });
    ComputeDocCounts(out);

    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseOutlookMsgDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseOutlookMsg(raw, out); }

} // namespace Formats
