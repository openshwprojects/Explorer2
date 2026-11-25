#pragma once
// QuickenQifFinance.h — Quicken Interchange Format (.qif) Financial Ledger Reader

namespace Formats {
namespace DetailQuickenQif {

inline bool ParseQifAmount(std::string_view s, double& outAmt) {
    std::string clean;
    clean.reserve(s.size());
    bool neg = false;
    for (char c : s) {
        if (c == ',' || c == ' ' || c == '$' || c == '\t') continue;
        if (c == '(' || c == '-') neg = true;
        else if (c == ')') continue;
        else clean.push_back(c);
    }
    if (clean.empty()) return false;
    char* ep = nullptr;
    double v = std::strtod(clean.c_str(), &ep);
    if (ep == clean.c_str()) return false;
    outAmt = neg ? -std::fabs(v) : v;
    return true;
}

inline std::wstring FmtMoney(double v) {
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.2f", v);
    return buf;
}

inline std::string AsciiLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

} // namespace DetailQuickenQif

inline bool ParseQuickenQifFinance(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv;
    if (!Formats::FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 6u)) return false;

    size_t firstNonWs = 0;
    while (firstNonWs < sv.size() && (sv[firstNonWs] == ' ' || sv[firstNonWs] == '\t' || sv[firstNonWs] == '\r' || sv[firstNonWs] == '\n')) {
        ++firstNonWs;
    }
    if (firstNonWs >= sv.size() || sv[firstNonWs] != '!') return false;
    std::string headTag = DetailQuickenQif::AsciiLower(sv.substr(firstNonWs, std::min<size_t>(16, sv.size() - firstNonWs)));
    if (headTag.rfind("!type:", 0) != 0 && headTag.rfind("!account", 0) != 0 && headTag.rfind("!option:", 0) != 0 && headTag.rfind("!clear:", 0) != 0) {
        return false;
    }

    struct QifSplit {
        std::wstring cat, memo, amt;
    };
    struct QifRec {
        std::wstring date, amount, cleared, numAction, payee, memo, category, address;
        std::wstring security, price, quantity, commission;
        std::wstring acctDesc, acctType, creditLimit, stmtBal;
        std::vector<QifSplit> splits;
        bool hasAny = false;
    };

    SpreadsheetSheet txnSh;
    txnSh.name = L"QIF_Transactions";
    txnSh.rows.push_back({
        L"Account", L"Section", L"Date", L"Num / Action", L"Payee / Security",
        L"Category", L"Cleared", L"Amount", L"Running Balance", L"Memo / Details"
    });

    SpreadsheetSheet splitSh;
    splitSh.name = L"QIF_Splits";
    splitSh.rows.push_back({ L"Txn #", L"Date", L"Payee", L"Split Category", L"Split Amount", L"Split Memo" });

    SpreadsheetSheet acctSh;
    acctSh.name = L"QIF_Accounts";
    acctSh.rows.push_back({ L"Account Name", L"Type", L"Description", L"Statement Balance", L"Credit Limit" });

    SpreadsheetSheet catSh;
    catSh.name = L"QIF_Categories";
    catSh.rows.push_back({ L"Kind", L"Name", L"Description", L"Flags" });

    std::wstring curAccount = L"Default";
    std::string curMode = "bank"; // account, cat, class, bank, cash, ccard, invst, otha, othl
    std::wstring curSectionLabel = L"Bank";
    std::map<std::wstring, double> runningBal;
    QifRec cur;
    size_t txnCount = 0;

    auto flushRecord = [&]() {
        if (!cur.hasAny) { cur = {}; return; }
        if (curMode == "account") {
            std::wstring aname = cur.numAction.empty() ? cur.payee : cur.numAction;
            if (!aname.empty()) curAccount = aname;
            acctSh.rows.push_back({ aname, cur.acctType, cur.acctDesc, cur.stmtBal, cur.creditLimit });
        } else if (curMode == "cat" || curMode == "class") {
            catSh.rows.push_back({
                curMode == "cat" ? L"Category" : L"Class",
                cur.numAction,
                cur.acctDesc,
                cur.cleared
            });
        } else {
            ++txnCount;
            double amtVal = 0.0;
            bool hasAmt = DetailQuickenQif::ParseQifAmount(PathUtils::WideToUtf8(cur.amount), amtVal);
            double& rbal = runningBal[curAccount];
            if (hasAmt) rbal += amtVal;

            std::wstring who = !cur.payee.empty() ? cur.payee : cur.security;
            std::wstring det = cur.memo;
            if (!cur.security.empty() && (!cur.quantity.empty() || !cur.price.empty())) {
                std::wstring invInfo = L"Qty=" + cur.quantity + L" @ " + cur.price;
                if (!cur.commission.empty()) invInfo += L" (Comm=" + cur.commission + L")";
                det = det.empty() ? invInfo : (det + L" [" + invInfo + L"]");
            }
            if (!cur.splits.empty()) {
                det = det.empty() ? (L"(" + std::to_wstring(cur.splits.size()) + L" splits)")
                                  : (det + L" (" + std::to_wstring(cur.splits.size()) + L" splits)");
            }

            if (txnSh.rows.size() <= 5000) {
                txnSh.rows.push_back({
                    curAccount,
                    curSectionLabel,
                    cur.date,
                    cur.numAction,
                    who,
                    cur.category,
                    cur.cleared,
                    hasAmt ? DetailQuickenQif::FmtMoney(amtVal) : cur.amount,
                    DetailQuickenQif::FmtMoney(rbal),
                    det
                });
            }
            for (const auto& sp : cur.splits) {
                if (splitSh.rows.size() <= 5000) {
                    splitSh.rows.push_back({ std::to_wstring(txnCount), cur.date, who, sp.cat, sp.amt, sp.memo });
                }
            }
        }
        cur = {};
    };

    size_t pos = firstNonWs;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        std::string_view line = (eol == std::string_view::npos) ? sv.substr(pos) : sv.substr(pos, eol - pos);
        pos = (eol == std::string_view::npos) ? sv.size() : (eol + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;

        if (line[0] == '!') {
            flushRecord();
            std::string dir = DetailQuickenQif::AsciiLower(line);
            while (!dir.empty() && (dir.back() == ' ' || dir.back() == '\t')) dir.pop_back();
            if (dir == "!account") {
                curMode = "account";
            } else if (dir.rfind("!type:", 0) == 0) {
                std::string t = dir.substr(6);
                while (!t.empty() && t.front() == ' ') t.erase(t.begin());
                if (t == "cat") { curMode = "cat"; }
                else if (t == "class") { curMode = "class"; }
                else if (t == "invst") { curMode = "invst"; curSectionLabel = L"Investment"; }
                else if (t == "ccard") { curMode = "ccard"; curSectionLabel = L"Credit Card"; }
                else if (t == "cash")  { curMode = "cash";  curSectionLabel = L"Cash"; }
                else if (t == "oth a") { curMode = "otha";  curSectionLabel = L"Asset"; }
                else if (t == "oth l") { curMode = "othl";  curSectionLabel = L"Liability"; }
                else { curMode = "bank"; curSectionLabel = PathUtils::Utf8ToWide(line.substr(6)); }
            }
            continue;
        }

        char tag = line[0];
        if (tag == '^') {
            flushRecord();
            continue;
        }

        std::wstring val = PathUtils::Trim(PathUtils::Utf8ToWide(line.substr(1)));
        cur.hasAny = true;
        if (curMode == "account") {
            if (tag == 'N') cur.numAction = val;
            else if (tag == 'T') cur.acctType = val;
            else if (tag == 'D') cur.acctDesc = val;
            else if (tag == 'L') cur.creditLimit = val;
            else if (tag == '$' || tag == 'B') cur.stmtBal = val;
        } else if (curMode == "cat" || curMode == "class") {
            if (tag == 'N') cur.numAction = val;
            else if (tag == 'D') cur.acctDesc = val;
            else if (tag == 'E' || tag == 'I' || tag == 'T') {
                if (!cur.cleared.empty()) cur.cleared += L", ";
                cur.cleared += (tag == 'E') ? L"Expense" : (tag == 'I' ? L"Income" : L"Tax-Related");
            }
        } else {
            switch (tag) {
                case 'D': cur.date = val; break;
                case 'T': cur.amount = val; break;
                case 'U': if (cur.amount.empty()) cur.amount = val; break;
                case 'C': cur.cleared = val; break;
                case 'N': cur.numAction = val; break;
                case 'P': cur.payee = val; break;
                case 'M': cur.memo = val; break;
                case 'L': cur.category = val; break;
                case 'A': if (cur.address.empty()) cur.address = val; break;
                case 'Y': cur.security = val; break;
                case 'I': cur.price = val; break;
                case 'Q': cur.quantity = val; break;
                case 'O': cur.commission = val; break;
                case 'S': cur.splits.push_back({ val, L"", L"" }); break;
                case 'E': if (!cur.splits.empty()) cur.splits.back().memo = val; break;
                case '$': if (!cur.splits.empty()) cur.splits.back().amt = val; break;
                default: break;
            }
        }
    }
    flushRecord();

    if (txnSh.rows.size() <= 1 && acctSh.rows.size() <= 1 && catSh.rows.size() <= 1) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Quicken Interchange Format Ledger (.qif)";
    if (txnSh.rows.size() > 1)   { FinalizeSheetCols(txnSh);   out.sheets.push_back(std::move(txnSh)); }
    if (splitSh.rows.size() > 1) { FinalizeSheetCols(splitSh); out.sheets.push_back(std::move(splitSh)); }
    if (acctSh.rows.size() > 1)  { FinalizeSheetCols(acctSh);  out.sheets.push_back(std::move(acctSh)); }
    if (catSh.rows.size() > 1)   { FinalizeSheetCols(catSh);   out.sheets.push_back(std::move(catSh)); }
    out.valid = true;
    return true;
}

} // namespace Formats
