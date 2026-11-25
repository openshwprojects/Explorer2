#pragma once
// OpenFinancialOfx.h — Open Financial Exchange v1 SGML & v2 XML (.ofx, .qfx) Reader

namespace Formats {
namespace DetailOpenFinancialOfx {

inline std::string UpperAscii(std::string_view s) {
    std::string u(s);
    for (char& c : u) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return u;
}

inline std::wstring FormatOfxDate(std::string_view rawDt) {
    while (!rawDt.empty() && (rawDt.front() == ' ' || rawDt.front() == '\t')) rawDt.remove_prefix(1);
    while (!rawDt.empty() && (rawDt.back() == ' ' || rawDt.back() == '\t' || rawDt.back() == '\r' || rawDt.back() == '\n')) rawDt.remove_suffix(1);
    size_t digits = 0;
    while (digits < rawDt.size() && rawDt[digits] >= '0' && rawDt[digits] <= '9') ++digits;
    if (digits >= 14) {
        return PathUtils::Utf8ToWide(rawDt.substr(0, 4)) + L"-" +
               PathUtils::Utf8ToWide(rawDt.substr(4, 2)) + L"-" +
               PathUtils::Utf8ToWide(rawDt.substr(6, 2)) + L" " +
               PathUtils::Utf8ToWide(rawDt.substr(8, 2)) + L":" +
               PathUtils::Utf8ToWide(rawDt.substr(10, 2)) + L":" +
               PathUtils::Utf8ToWide(rawDt.substr(12, 2));
    }
    if (digits >= 8) {
        return PathUtils::Utf8ToWide(rawDt.substr(0, 4)) + L"-" +
               PathUtils::Utf8ToWide(rawDt.substr(4, 2)) + L"-" +
               PathUtils::Utf8ToWide(rawDt.substr(6, 2));
    }
    return PathUtils::Utf8ToWide(rawDt);
}

inline bool IsOfxContainerTag(const std::string& t) {
    static const char* const kContainers[] = {
        "OFX", "SIGNONMSGSRSV1", "SIGNONMSGSRSV2", "SONRS", "STATUS", "FI",
        "BANKMSGSRSV1", "BANKMSGSRSV2", "STMTTRNRS", "STMTRS", "BANKACCTFROM", "BANKACCTTO",
        "CCARDMSGSRSV1", "CCARDMSGSRSV2", "CCSTMTTRNRS", "CCSTMTRS", "CCACCTFROM", "CCACCTTO",
        "BANKTRANLIST", "STMTTRN", "LEDGERBAL", "AVAILBAL", "PAYEE", "CURRENCY", "ORIGCURRENCY",
        "INVSTMTMSGSRSV1", "INVSTMTMSGSRSV2", "INVSTMTTRNRS", "INVSTMTRS", "INVACCTFROM",
        "INVPOSLIST", "POSSTOCK", "POSMF", "POSOPT", "POSDEBT", "POSOTHER", "INVPOS", "SECID",
        "SECLISTMSGSRSV1", "SECLIST", "STOCKINFO", "MFINFO", "OPTINFO", "DEBTINFO", "OTHERINFO", "SECINFO",
        "INVTRANLIST", "INVBANKTRAN", "BUYSTOCK", "SELLSTOCK", "BUYMF", "SELLMF", "INCOME", "REINVEST",
        "INVBUY", "INVSELL", "INVTRAN", "INVBAL", "BALLIST", "BAL"
    };
    for (const char* c : kContainers) {
        if (t == c) return true;
    }
    return false;
}

} // namespace DetailOpenFinancialOfx

inline bool ParseOpenFinancialOfx(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::string_view sv;
    if (!Formats::FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 16u)) return false;

    std::string headUpper = DetailOpenFinancialOfx::UpperAscii(sv.substr(0, std::min<size_t>(1024, sv.size())));
    const bool hasOfxHeader = (headUpper.find("OFXHEADER:") != std::string::npos) || (headUpper.find("<?OFX") != std::string::npos);
    size_t ofxStart = headUpper.find("<OFX>");
    if (ofxStart == std::string::npos) ofxStart = headUpper.find("<OFX ");
    if (ofxStart == std::string::npos) {
        std::string fullUpper = DetailOpenFinancialOfx::UpperAscii(sv.substr(0, std::min<size_t>(16384, sv.size())));
        ofxStart = fullUpper.find("<OFX>");
        if (ofxStart == std::string::npos) ofxStart = fullUpper.find("<OFX ");
    }
    if (ofxStart == std::string::npos) return false;
    (void)hasOfxHeader;

    // Parse key:value OFX v1 header lines before <OFX>
    std::wstring ofxHdrVersion = L"200 (XML)";
    if (ofxStart > 0) {
        std::string_view pre = sv.substr(0, ofxStart);
        size_t lp = 0;
        while (lp < pre.size()) {
            size_t eol = pre.find('\n', lp);
            std::string_view ln = (eol == std::string_view::npos) ? pre.substr(lp) : pre.substr(lp, eol - lp);
            lp = (eol == std::string_view::npos) ? pre.size() : (eol + 1);
            if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
            if (DetailOpenFinancialOfx::UpperAscii(ln).rfind("VERSION:", 0) == 0) {
                ofxHdrVersion = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(8))) + L" (SGML)";
            }
        }
    }

    struct OfxTxn {
        std::wstring acct, trnType, dtPosted, trnAmt, fitId, checkNum, name, memo, sic, cur;
    };
    struct OfxPos {
        std::wstring acct, posKind, uniqueId, uniqueIdType, units, unitPrice, mktVal, dtPriceAsOf;
    };
    struct OfxSec {
        std::wstring ticker, secName;
    };

    std::wstring fiOrg, fiFid, dtServer, curDef = L"USD";
    std::wstring bankId, acctId, acctType, dtStart, dtEnd;
    std::wstring ledgerAmt, ledgerDt, availAmt, availDt;
    std::vector<OfxTxn> txns;
    std::vector<OfxPos> positions;
    std::map<std::wstring, OfxSec> secMap; // uniqueId -> security metadata

    std::vector<std::string> stack;
    OfxTxn curTxn;
    OfxPos curPos;
    std::wstring curSecId, curSecName, curTicker;

    auto inCtx = [&](const char* tag) -> bool {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (*it == tag) return true;
        }
        return false;
    };

    size_t p = ofxStart;
    while (p < sv.size()) {
        size_t lt = sv.find('<', p);
        if (lt == std::string_view::npos) break;
        size_t gt = sv.find('>', lt + 1);
        if (gt == std::string_view::npos) break;

        std::string_view rawTag = sv.substr(lt + 1, gt - lt - 1);
        p = gt + 1;
        if (rawTag.empty() || rawTag[0] == '?' || rawTag[0] == '!') continue;

        bool isClose = (rawTag[0] == '/');
        if (isClose) rawTag.remove_prefix(1);
        bool selfClose = (!rawTag.empty() && rawTag.back() == '/');
        if (selfClose) rawTag.remove_suffix(1);
        size_t sp = rawTag.find_first_of(" \t\r\n");
        std::string tag = DetailOpenFinancialOfx::UpperAscii(sp == std::string_view::npos ? rawTag : rawTag.substr(0, sp));
        if (tag.empty()) continue;

        if (isClose) {
            if (tag == "STMTTRN") {
                if (!curTxn.trnAmt.empty() || !curTxn.fitId.empty() || !curTxn.name.empty()) {
                    if (curTxn.acct.empty()) curTxn.acct = acctId;
                    if (curTxn.cur.empty()) curTxn.cur = curDef;
                    if (txns.size() < 5000) txns.push_back(std::move(curTxn));
                }
                curTxn = {};
            } else if (tag == "POSSTOCK" || tag == "POSMF" || tag == "POSOPT" || tag == "POSDEBT" || tag == "POSOTHER") {
                if (!curPos.uniqueId.empty() || !curPos.mktVal.empty()) {
                    curPos.acct = acctId;
                    curPos.posKind = PathUtils::Utf8ToWide(tag.substr(3));
                    if (positions.size() < 2000) positions.push_back(std::move(curPos));
                }
                curPos = {};
            } else if (tag == "SECINFO") {
                if (!curSecId.empty()) secMap[curSecId] = { curTicker, curSecName };
                curSecId.clear(); curSecName.clear(); curTicker.clear();
            }
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                if (*it == tag) {
                    stack.erase(std::next(it).base(), stack.end());
                    break;
                }
            }
            continue;
        }

        if (DetailOpenFinancialOfx::IsOfxContainerTag(tag)) {
            if (tag == "STMTTRN") curTxn = {};
            else if (tag == "POSSTOCK" || tag == "POSMF" || tag == "POSOPT" || tag == "POSDEBT" || tag == "POSOTHER") curPos = {};
            else if (tag == "SECINFO") { curSecId.clear(); curSecName.clear(); curTicker.clear(); }
            if (!selfClose) stack.push_back(tag);
            continue;
        }

        // Leaf element: value runs up to the next '<' (works identically for OFX v1 unclosed SGML & OFX v2 closed XML!)
        size_t nextLt = sv.find('<', p);
        std::string_view rawVal = (nextLt == std::string_view::npos) ? sv.substr(p) : sv.substr(p, nextLt - p);
        p = (nextLt == std::string_view::npos) ? sv.size() : nextLt;
        std::string decUtf8 = ::Explorer2::XmlTagScan::DecodeText(rawVal);
        std::wstring val = PathUtils::Trim(PathUtils::Utf8ToWide(decUtf8));
        if (val.empty()) continue;

        if (inCtx("STMTTRN")) {
            if (tag == "TRNTYPE") curTxn.trnType = val;
            else if (tag == "DTPOSTED") curTxn.dtPosted = DetailOpenFinancialOfx::FormatOfxDate(decUtf8);
            else if (tag == "TRNAMT") curTxn.trnAmt = val;
            else if (tag == "FITID") curTxn.fitId = val;
            else if (tag == "CHECKNUM" || (tag == "REFNUM" && curTxn.checkNum.empty())) curTxn.checkNum = val;
            else if (tag == "NAME") curTxn.name = val;
            else if (tag == "MEMO") curTxn.memo = val;
            else if (tag == "SIC") curTxn.sic = val;
            else if (tag == "CURSYM") curTxn.cur = val;
        } else if (inCtx("INVPOS")) {
            if (tag == "UNIQUEID") curPos.uniqueId = val;
            else if (tag == "UNIQUEIDTYPE") curPos.uniqueIdType = val;
            else if (tag == "UNITS") curPos.units = val;
            else if (tag == "UNITPRICE") curPos.unitPrice = val;
            else if (tag == "MKTVAL") curPos.mktVal = val;
            else if (tag == "DTPRICEASOF") curPos.dtPriceAsOf = DetailOpenFinancialOfx::FormatOfxDate(decUtf8);
        } else if (inCtx("SECINFO")) {
            if (tag == "UNIQUEID") curSecId = val;
            else if (tag == "SECNAME") curSecName = val;
            else if (tag == "TICKER") curTicker = val;
        } else if (inCtx("LEDGERBAL")) {
            if (tag == "BALAMT") ledgerAmt = val;
            else if (tag == "DTASOF") ledgerDt = DetailOpenFinancialOfx::FormatOfxDate(decUtf8);
        } else if (inCtx("AVAILBAL")) {
            if (tag == "BALAMT") availAmt = val;
            else if (tag == "DTASOF") availDt = DetailOpenFinancialOfx::FormatOfxDate(decUtf8);
        } else if (inCtx("FI")) {
            if (tag == "ORG") fiOrg = val;
            else if (tag == "FID") fiFid = val;
        } else if (inCtx("BANKACCTFROM") || inCtx("CCACCTFROM") || inCtx("INVACCTFROM")) {
            if (tag == "BANKID" || tag == "BROKERID") bankId = val;
            else if (tag == "ACCTID") acctId = val;
            else if (tag == "ACCTTYPE") acctType = val;
            if (inCtx("CCACCTFROM") && acctType.empty()) acctType = L"CREDITCARD";
            if (inCtx("INVACCTFROM") && acctType.empty()) acctType = L"INVESTMENT";
        } else if (inCtx("BANKTRANLIST") || inCtx("INVTRANLIST")) {
            if (tag == "DTSTART") dtStart = DetailOpenFinancialOfx::FormatOfxDate(decUtf8);
            else if (tag == "DTEND") dtEnd = DetailOpenFinancialOfx::FormatOfxDate(decUtf8);
        } else {
            if (tag == "CURDEF") curDef = val;
            else if (tag == "DTSERVER") dtServer = DetailOpenFinancialOfx::FormatOfxDate(decUtf8);
        }
    }

    if (!txns.empty()) {
        SpreadsheetSheet txnSh;
        txnSh.name = L"OFX_Transactions";
        txnSh.rows.push_back({ L"Account", L"Type", L"Date Posted", L"Amount", L"Currency", L"Name / Payee", L"Memo", L"Check #", L"FITID" });
        for (const auto& t : txns) {
            txnSh.rows.push_back({ t.acct, t.trnType, t.dtPosted, t.trnAmt, t.cur, t.name, t.memo, t.checkNum, t.fitId });
        }
        FinalizeSheetCols(txnSh);
        out.sheets.push_back(std::move(txnSh));
    }

    if (!positions.empty()) {
        SpreadsheetSheet posSh;
        posSh.name = L"OFX_Positions";
        posSh.rows.push_back({ L"Account", L"Asset Class", L"Ticker / Security", L"ID Type:ID", L"Units", L"Unit Price", L"Market Value", L"Price As-Of" });
        for (const auto& pos : positions) {
            std::wstring secDisp = pos.uniqueId;
            auto it = secMap.find(pos.uniqueId);
            if (it != secMap.end()) {
                secDisp = it->second.ticker.empty() ? it->second.secName : (it->second.ticker + L" — " + it->second.secName);
            }
            posSh.rows.push_back({
                pos.acct, pos.posKind, secDisp,
                pos.uniqueIdType.empty() ? pos.uniqueId : (pos.uniqueIdType + L":" + pos.uniqueId),
                pos.units, pos.unitPrice, pos.mktVal, pos.dtPriceAsOf
            });
        }
        FinalizeSheetCols(posSh);
        out.sheets.push_back(std::move(posSh));
    }

    SpreadsheetSheet sumSh;
    sumSh.name = L"OFX_Account_Summary";
    sumSh.rows.push_back({ L"OFX Field", L"Value" });
    sumSh.rows.push_back({ L"OFX Version", ofxHdrVersion });
    if (!fiOrg.empty() || !fiFid.empty()) sumSh.rows.push_back({ L"Financial Institution", fiOrg + (fiFid.empty() ? L"" : (L" (FID " + fiFid + L")")) });
    if (!dtServer.empty()) sumSh.rows.push_back({ L"Server Timestamp", dtServer });
    if (!bankId.empty()) sumSh.rows.push_back({ L"Routing / Broker ID", bankId });
    if (!acctId.empty()) sumSh.rows.push_back({ L"Account ID", acctId });
    if (!acctType.empty()) sumSh.rows.push_back({ L"Account Type", acctType });
    sumSh.rows.push_back({ L"Default Currency", curDef });
    if (!dtStart.empty() || !dtEnd.empty()) sumSh.rows.push_back({ L"Statement Period", dtStart + L" .. " + dtEnd });
    if (!ledgerAmt.empty()) sumSh.rows.push_back({ L"Ledger Balance", ledgerAmt + L" " + curDef + (ledgerDt.empty() ? L"" : (L" (as of " + ledgerDt + L")")) });
    if (!availAmt.empty()) sumSh.rows.push_back({ L"Available Balance", availAmt + L" " + curDef + (availDt.empty() ? L"" : (L" (as of " + availDt + L")")) });
    sumSh.rows.push_back({ L"Statement Transactions", std::to_wstring(txns.size()) });
    if (!positions.empty()) sumSh.rows.push_back({ L"Investment Positions", std::to_wstring(positions.size()) });
    FinalizeSheetCols(sumSh);
    out.sheets.push_back(std::move(sumSh));

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Open Financial Exchange Statement (" + extLower + L")";
    out.valid = true;
    return true;
}

} // namespace Formats
