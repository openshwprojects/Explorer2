#pragma once
// ArmCmsisPdscDoc.h — ARM Open-CMSIS-Pack MCU Device, Memory Map & Component Description (.pdsc)
// Parses <package> vendor/name/releases, hierarchical <devices>/<family>/<subFamily>/<device>
// (inheriting <processor>, <memory>, <compile>, and <debug svd=...>), <boards>, and <components>
// into multi-sheet tables with strict XML tag-stack balance validation.

namespace Formats {

namespace DetailArmPdsc {

inline std::string GetAttrCI(std::string_view attrs, std::string_view key) {
    std::string_view rawVal;
    if (!XmlTagScan::FindAttr(attrs, key, rawVal, true, true)) return {};
    return XmlTagScan::DecodeText(rawVal);
}

inline bool ParseStartTagAttrs(
    std::string_view tagBody,
    std::string& outTagName,
    std::string_view& outAttrs,
    bool& outSelfClose)
{
    outTagName.clear();
    outAttrs = {};
    outSelfClose = false;

    tagBody = ReaderText::TrimSv(tagBody);
    if (!tagBody.empty() && tagBody.back() == '/') {
        outSelfClose = true;
        tagBody.remove_suffix(1u);
        tagBody = ReaderText::TrimSv(tagBody);
    }
    if (tagBody.empty()) return false;

    size_t p = 0u;
    while (p < tagBody.size() && static_cast<unsigned char>(tagBody[p]) > 0x20u && tagBody[p] != '=') ++p;
    outTagName.assign(tagBody.data(), p);
    const size_t colon = outTagName.find(':');
    if (colon != std::string::npos) outTagName.erase(0, colon + 1u);
    if (outTagName.empty()) return false;

    outAttrs = tagBody.substr(p);
    while (p < tagBody.size()) {
        while (p < tagBody.size() && static_cast<unsigned char>(tagBody[p]) <= 0x20u) ++p;
        if (p >= tagBody.size()) break;
        const size_t ks = p;
        while (p < tagBody.size() && tagBody[p] != '=' && static_cast<unsigned char>(tagBody[p]) > 0x20u) ++p;
        if (p == ks) return false;
        while (p < tagBody.size() && static_cast<unsigned char>(tagBody[p]) <= 0x20u) ++p;
        if (p >= tagBody.size() || tagBody[p] != '=') return false;
        ++p;
        while (p < tagBody.size() && static_cast<unsigned char>(tagBody[p]) <= 0x20u) ++p;
        if (p >= tagBody.size() || (tagBody[p] != '"' && tagBody[p] != '\'')) return false;
        const char qc = tagBody[p++];
        while (p < tagBody.size() && tagBody[p] != qc) ++p;
        if (p >= tagBody.size()) return false;
        ++p;
    }
    return true;
}

struct DevScopeProps {
    std::string family;
    std::string subFamily;
    std::string deviceName;
    std::string core;
    std::string fpu;
    uint64_t    clockHz = 0;
    std::string svd;
    std::string header;
    std::vector<std::string> memSummary;
};

} // namespace DetailArmPdsc

inline bool ParseArmCmsisPdscDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 32u, (32u << 20))) return false;

    bool sawPackageRoot = false;
    bool closedPackageRoot = false;
    std::string packVendor;
    std::string packName;
    std::string latestReleaseVer;

    SpreadsheetSheet devSheet;
    devSheet.name = L"Devices";
    devSheet.rows.push_back({ L"Device", L"Family", L"Core", L"Clock_MHz", L"Memory", L"SVD / Header" });

    SpreadsheetSheet memSheet;
    memSheet.name = L"Memory_Map";
    memSheet.rows.push_back({ L"Scope", L"Region", L"Access", L"Start", L"Size_KB", L"Startup" });

    SpreadsheetSheet compSheet;
    compSheet.name = L"Components_Boards";
    compSheet.rows.push_back({ L"Kind", L"Class / Vendor", L"Name / Group", L"Version / Rev", L"Details" });

    auto emitDeviceRow = [&](const DetailArmPdsc::DevScopeProps& ds) {
        if (ds.deviceName.empty() || devSheet.rows.size() >= 8192u) return;
        std::string famLbl = ds.family;
        if (!ds.subFamily.empty()) famLbl += " / " + ds.subFamily;
        std::string coreLbl = ds.core;
        if (!ds.fpu.empty() && ds.fpu != "NO_FPU" && ds.fpu != "0") coreLbl += " (" + ds.fpu + ")";
        wchar_t clkBuf[32] = {};
        if (ds.clockHz > 0ULL) swprintf_s(clkBuf, L"%.4g", static_cast<double>(ds.clockHz) / 1e6);
        std::string memJoined;
        for (const auto& ms : ds.memSummary) {
            if (!memJoined.empty()) memJoined += "; ";
            memJoined += ms;
        }
        const std::string& svdHdr = !ds.svd.empty() ? ds.svd : ds.header;
        devSheet.rows.push_back({
            PathUtils::Utf8ToWide(ds.deviceName),
            PathUtils::Utf8ToWide(famLbl),
            PathUtils::Utf8ToWide(coreLbl),
            clkBuf,
            PathUtils::Utf8ToWide(memJoined),
            PathUtils::Utf8ToWide(svdHdr)
        });
    };

    std::vector<std::string> tagStack;
    std::vector<DetailArmPdsc::DevScopeProps> devScopeStack;

    std::string curBoardVendor, curBoardName, curBoardRev, curBoardMcu;
    std::string curCompClass, curCompGroup, curCompSub, curCompVer, curCompDesc;
    std::string lastText;

    size_t pos = 0u;
    const size_t n = sv.size();
    while (pos < n) {
        const size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos) break;

        if (lt > pos) {
            std::string_view txt = ReaderText::TrimSv(sv.substr(pos, lt - pos));
            if (!txt.empty()) {
                lastText = XmlTagScan::DecodeText(txt);
            }
        }

        if (lt + 4u <= n && sv.substr(lt, 4u) == "<!--") {
            const size_t endCmt = sv.find("-->", lt + 4u);
            if (endCmt == std::string_view::npos) return false;
            pos = endCmt + 3u;
            continue;
        }
        if (lt + 9u <= n && sv.substr(lt, 9u) == "<![CDATA[") {
            const size_t endCd = sv.find("]]>", lt + 9u);
            if (endCd == std::string_view::npos) return false;
            pos = endCd + 3u;
            continue;
        }
        if (lt + 2u <= n && (sv[lt + 1u] == '?' || sv[lt + 1u] == '!')) {
            const size_t gt = sv.find('>', lt + 2u);
            if (gt == std::string_view::npos) return false;
            pos = gt + 1u;
            continue;
        }

        const size_t gt = sv.find('>', lt + 1u);
        if (gt == std::string_view::npos) return false;
        std::string_view inside = ReaderText::TrimSv(sv.substr(lt + 1u, gt - lt - 1u));
        pos = gt + 1u;
        if (inside.empty()) return false;

        // Closing tag </tag>
        if (inside.front() == '/') {
            std::string closeName(ReaderText::TrimSv(inside.substr(1u)));
            const size_t colon = closeName.find(':');
            if (colon != std::string::npos) closeName.erase(0, colon + 1u);
            if (tagStack.empty() || tagStack.back() != closeName) return false;

            const size_t depth = tagStack.size();
            if (depth == 2u && closeName == "vendor") {
                packVendor = lastText;
            } else if (depth == 2u && closeName == "name") {
                packName = lastText;
            } else if (closeName == "description" && depth >= 2u && tagStack[depth - 2u] == "component") {
                curCompDesc = lastText;
            } else if (closeName == "device" && !devScopeStack.empty()) {
                emitDeviceRow(devScopeStack.back());
                devScopeStack.pop_back();
            } else if ((closeName == "family" || closeName == "subFamily") && !devScopeStack.empty()) {
                devScopeStack.pop_back();
            } else if (closeName == "board") {
                if (!curBoardName.empty() && compSheet.rows.size() < 4096u) {
                    compSheet.rows.push_back({
                        L"Board",
                        PathUtils::Utf8ToWide(curBoardVendor),
                        PathUtils::Utf8ToWide(curBoardName),
                        PathUtils::Utf8ToWide(curBoardRev),
                        PathUtils::Utf8ToWide(curBoardMcu)
                    });
                }
                curBoardVendor.clear(); curBoardName.clear(); curBoardRev.clear(); curBoardMcu.clear();
            } else if (closeName == "component") {
                if ((!curCompClass.empty() || !curCompGroup.empty()) && compSheet.rows.size() < 4096u) {
                    std::string grp = curCompGroup;
                    if (!curCompSub.empty()) grp += ":" + curCompSub;
                    compSheet.rows.push_back({
                        L"Component",
                        PathUtils::Utf8ToWide(curCompClass),
                        PathUtils::Utf8ToWide(grp),
                        PathUtils::Utf8ToWide(curCompVer),
                        PathUtils::Utf8ToWide(curCompDesc)
                    });
                }
                curCompClass.clear(); curCompGroup.clear(); curCompSub.clear(); curCompVer.clear(); curCompDesc.clear();
            }

            tagStack.pop_back();
            if (closeName == "package" && tagStack.empty()) {
                closedPackageRoot = true;
            }
            lastText.clear();
            continue;
        }

        // Start or self-closing tag
        std::string tagName;
        std::string_view attrs;
        bool selfClose = false;
        if (!DetailArmPdsc::ParseStartTagAttrs(inside, tagName, attrs, selfClose)) {
            return false;
        }

        if (tagStack.empty()) {
            if (tagName != "package") return false;
            sawPackageRoot = true;
        }
        if (tagStack.size() >= 64u) return false;
        lastText.clear();

        if (tagName == "release" && latestReleaseVer.empty()) {
            latestReleaseVer = DetailArmPdsc::GetAttrCI(attrs, "version");
        } else if (tagName == "family") {
            DetailArmPdsc::DevScopeProps sc = devScopeStack.empty() ? DetailArmPdsc::DevScopeProps{} : devScopeStack.back();
            sc.family = DetailArmPdsc::GetAttrCI(attrs, "Dfamily");
            sc.subFamily.clear();
            sc.deviceName.clear();
            devScopeStack.push_back(std::move(sc));
        } else if (tagName == "subFamily") {
            DetailArmPdsc::DevScopeProps sc = devScopeStack.empty() ? DetailArmPdsc::DevScopeProps{} : devScopeStack.back();
            sc.subFamily = DetailArmPdsc::GetAttrCI(attrs, "DsubFamily");
            sc.deviceName.clear();
            devScopeStack.push_back(std::move(sc));
        } else if (tagName == "device") {
            DetailArmPdsc::DevScopeProps sc = devScopeStack.empty() ? DetailArmPdsc::DevScopeProps{} : devScopeStack.back();
            sc.deviceName = DetailArmPdsc::GetAttrCI(attrs, "Dname");
            devScopeStack.push_back(std::move(sc));
        } else if (tagName == "processor" && !devScopeStack.empty()) {
            auto& sc = devScopeStack.back();
            std::string c = DetailArmPdsc::GetAttrCI(attrs, "Dcore");
            if (!c.empty()) sc.core = c;
            std::string f = DetailArmPdsc::GetAttrCI(attrs, "Dfpu");
            if (!f.empty()) sc.fpu = f;
            std::string clk = DetailArmPdsc::GetAttrCI(attrs, "Dclock");
            if (!clk.empty()) sc.clockHz = std::strtoull(clk.c_str(), nullptr, 0);
        } else if (tagName == "debug" && !devScopeStack.empty()) {
            std::string svd = DetailArmPdsc::GetAttrCI(attrs, "svd");
            if (!svd.empty()) devScopeStack.back().svd = svd;
        } else if (tagName == "compile" && !devScopeStack.empty()) {
            std::string hdr = DetailArmPdsc::GetAttrCI(attrs, "header");
            if (!hdr.empty()) devScopeStack.back().header = hdr;
        } else if (tagName == "memory" && !devScopeStack.empty()) {
            std::string mName = DetailArmPdsc::GetAttrCI(attrs, "name");
            if (mName.empty()) mName = DetailArmPdsc::GetAttrCI(attrs, "id");
            std::string mAcc   = DetailArmPdsc::GetAttrCI(attrs, "access");
            std::string mStart = DetailArmPdsc::GetAttrCI(attrs, "start");
            std::string mSize  = DetailArmPdsc::GetAttrCI(attrs, "size");
            std::string mBoot  = DetailArmPdsc::GetAttrCI(attrs, "startup");
            uint64_t szBytes = mSize.empty() ? 0ULL : std::strtoull(mSize.c_str(), nullptr, 0);
            uint64_t szKb = szBytes / 1024ULL;
            devScopeStack.back().memSummary.push_back(mName + ":" + std::to_string(szKb) + "KB");

            if (memSheet.rows.size() < 4096u) {
                const auto& sc = devScopeStack.back();
                const std::string& scopeLbl = !sc.deviceName.empty() ? sc.deviceName
                                            : (!sc.subFamily.empty() ? sc.subFamily : sc.family);
                memSheet.rows.push_back({
                    PathUtils::Utf8ToWide(scopeLbl),
                    PathUtils::Utf8ToWide(mName),
                    PathUtils::Utf8ToWide(mAcc),
                    PathUtils::Utf8ToWide(mStart),
                    std::to_wstring(szKb),
                    (mBoot == "1" || mBoot == "true") ? L"Yes" : L""
                });
            }
        } else if (tagName == "board") {
            curBoardVendor = DetailArmPdsc::GetAttrCI(attrs, "vendor");
            curBoardName   = DetailArmPdsc::GetAttrCI(attrs, "name");
            curBoardRev    = DetailArmPdsc::GetAttrCI(attrs, "revision");
        } else if (tagName == "mountedDevice") {
            std::string dn = DetailArmPdsc::GetAttrCI(attrs, "Dname");
            if (!dn.empty()) curBoardMcu = dn;
        } else if (tagName == "component") {
            curCompClass = DetailArmPdsc::GetAttrCI(attrs, "Cclass");
            curCompGroup = DetailArmPdsc::GetAttrCI(attrs, "Cgroup");
            curCompSub   = DetailArmPdsc::GetAttrCI(attrs, "Csub");
            curCompVer   = DetailArmPdsc::GetAttrCI(attrs, "Cversion");
        }

        if (!selfClose) {
            tagStack.push_back(tagName);
        } else {
            if (tagName == "package" && tagStack.empty()) closedPackageRoot = true;
            if (tagName == "device" && !devScopeStack.empty()) {
                emitDeviceRow(devScopeStack.back());
                devScopeStack.pop_back();
            } else if ((tagName == "family" || tagName == "subFamily") && !devScopeStack.empty()) {
                devScopeStack.pop_back();
            }
        }
    }

    if (!sawPackageRoot || !closedPackageRoot || !tagStack.empty()) return false;
    if (packVendor.empty() || packName.empty()) return false;
    if (devSheet.rows.size() <= 1u && compSheet.rows.size() <= 1u) return false;

    outDoc.sheets.clear();
    outDoc.paragraphs.clear();
    if (devSheet.rows.size() > 1u) {
        FinalizeSheetCols(devSheet);
        outDoc.sheets.push_back(std::move(devSheet));
    }
    if (memSheet.rows.size() > 1u) {
        FinalizeSheetCols(memSheet);
        outDoc.sheets.push_back(std::move(memSheet));
    }
    if (compSheet.rows.size() > 1u) {
        FinalizeSheetCols(compSheet);
        outDoc.sheets.push_back(std::move(compSheet));
    }

    std::wstring title = L"ARM Open-CMSIS-Pack (.pdsc) — " +
                         PathUtils::Utf8ToWide(packVendor + "." + packName);
    if (!latestReleaseVer.empty()) {
        title += L" v" + PathUtils::Utf8ToWide(latestReleaseVer);
    }
    outDoc.formatTitle = std::move(title);
    outDoc.kind = OfficeFileKind::CsvTable;
    outDoc.isSpreadsheet = true;
    outDoc.valid = true;
    return true;
}

} // namespace Formats
