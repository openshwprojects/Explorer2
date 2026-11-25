#pragma once
// GettextPoCatalog.h — GNU Gettext Portable Object Source & Template Catalog (.po / .pot)
// Parses msgctxt, msgid, msgid_plural, msgstr / msgstr[N], multi-line C-escaped string continuations,
// #, flags (fuzzy, c-format), #: source references, and #~ obsolete comments via GettextCatalogCommon.h.

namespace Formats {

namespace DetailGettextPo {

inline bool ParsePoQuotedString(std::string_view sv, std::string& outUnescaped) {
    sv = ReaderText::TrimSv(sv);
    if (sv.size() < 2u || sv.front() != '"' || sv.back() != '"') return false;
    size_t i = 1u;
    const size_t end = sv.size() - 1u;
    while (i < end) {
        char c = sv[i++];
        if (c == '"') return false; // Unescaped quote in middle of line!
        if (c == '\\') {
            if (i >= sv.size() - 1u) {
                // Trailing backslash right before closing quote -> unterminated string!
                return false;
            }
            char esc = sv[i++];
            switch (esc) {
            case 'n':  outUnescaped.push_back('\n'); break;
            case 'r':  outUnescaped.push_back('\r'); break;
            case 't':  outUnescaped.push_back('\t'); break;
            case '"':  outUnescaped.push_back('"');  break;
            case '\\': outUnescaped.push_back('\\'); break;
            default:   outUnescaped.push_back(esc);  break;
            }
        } else {
            outUnescaped.push_back(c);
        }
    }
    return true;
}

} // namespace DetailGettextPo

inline bool ParseGettextPoCatalog(
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    ParsedOfficeDocument& outDoc)
{
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 14u, (32u << 20), 8192u)) return false;

    enum class ActiveField { None, Ctxt, MsgId, MsgIdPlural, MsgStr, MsgStrPlural };

    bool hasId = false;
    bool hasStr = false;
    ActiveField active = ActiveField::None;
    size_t activePluralIdx = 0u;

    std::string curCtxt;
    std::string curId;
    std::string curIdPlural;
    bool hasIdPlural = false;
    std::string curStr;
    std::map<size_t, std::string> curPluralStrs;
    std::string curFlags;
    std::string curSrcRef;

    GettextCommon::CatalogHeaderMeta meta{};
    std::vector<GettextCommon::CatalogEntry> entries;
    size_t validStanzas = 0u;

    auto flushStanza = [&]() -> bool {
        if (!hasId && !hasStr) {
            curCtxt.clear(); curFlags.clear(); curSrcRef.clear();
            return true;
        }
        if (!hasId || !hasStr) return false; // Incomplete stanza (e.g. msgid without msgstr)!

        ++validStanzas;
        if (curId.empty() && curCtxt.empty() && !hasIdPlural) {
            GettextCommon::ParseHeaderMetadataBlock(curStr, meta);
        } else if (entries.size() < 16384u) {
            GettextCommon::CatalogEntry e{};
            e.context = PathUtils::Utf8ToWide(curCtxt);
            if (hasIdPlural) {
                std::string joinedId = curId;
                joinedId.push_back('\0');
                joinedId += curIdPlural;
                e.msgid = GettextCommon::JoinNulSeparatedForms(joinedId);

                std::string joinedStr;
                bool first = true;
                for (const auto& [pIdx, pVal] : curPluralStrs) {
                    (void)pIdx;
                    if (!first) joinedStr.push_back('\0');
                    first = false;
                    joinedStr += pVal;
                }
                e.msgstr = GettextCommon::JoinNulSeparatedForms(joinedStr);
            } else {
                e.msgid  = PathUtils::Utf8ToWide(curId);
                e.msgstr = PathUtils::Utf8ToWide(curStr);
            }

            std::string note;
            if (!curFlags.empty()) note = curFlags;
            if (!curSrcRef.empty()) {
                if (!note.empty()) note += " | ";
                note += curSrcRef;
            }
            e.notes = PathUtils::Utf8ToWide(note);
            entries.push_back(std::move(e));
        }

        hasId = false;
        hasStr = false;
        active = ActiveField::None;
        curCtxt.clear();
        curId.clear();
        curIdPlural.clear();
        hasIdPlural = false;
        curStr.clear();
        curPluralStrs.clear();
        curFlags.clear();
        curSrcRef.clear();
        return true;
    };

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) {
            if (!flushStanza()) return false;
            continue;
        }

        if (line.front() == '#') {
            if (hasId || hasStr) {
                if (!flushStanza()) return false;
            }
            if (line.rfind("#~", 0) == 0) continue; // Obsolete entry comment
            if (line.rfind("#,", 0) == 0) {
                std::string_view fl = ReaderText::TrimSv(line.substr(2u));
                if (!fl.empty()) {
                    if (!curFlags.empty()) curFlags += ", ";
                    curFlags.append(fl.data(), fl.size());
                }
            } else if (line.rfind("#:", 0) == 0 && curSrcRef.empty()) {
                std::string_view sr = ReaderText::TrimSv(line.substr(2u));
                curSrcRef.append(sr.data(), sr.size());
            }
            continue;
        }

        if (line.front() == '"') {
            if (active == ActiveField::None) return false;
            std::string* dst = nullptr;
            switch (active) {
            case ActiveField::Ctxt:         dst = &curCtxt; break;
            case ActiveField::MsgId:        dst = &curId; break;
            case ActiveField::MsgIdPlural:  dst = &curIdPlural; break;
            case ActiveField::MsgStr:       dst = &curStr; break;
            case ActiveField::MsgStrPlural: dst = &curPluralStrs[activePluralIdx]; break;
            default: return false;
            }
            if (!DetailGettextPo::ParsePoQuotedString(line, *dst)) return false;
            continue;
        }

        if (line.rfind("msgctxt", 0) == 0 && line.size() > 7u && (line[7] == ' ' || line[7] == '\t')) {
            if (hasId || hasStr) {
                if (!flushStanza()) return false;
            }
            curCtxt.clear();
            if (!DetailGettextPo::ParsePoQuotedString(line.substr(7u), curCtxt)) return false;
            active = ActiveField::Ctxt;
            continue;
        }

        if (line.rfind("msgid_plural", 0) == 0 && line.size() > 12u && (line[12] == ' ' || line[12] == '\t')) {
            if (!hasId || hasStr) return false;
            curIdPlural.clear();
            if (!DetailGettextPo::ParsePoQuotedString(line.substr(12u), curIdPlural)) return false;
            hasIdPlural = true;
            active = ActiveField::MsgIdPlural;
            continue;
        }

        if (line.rfind("msgid", 0) == 0 && line.size() > 5u && (line[5] == ' ' || line[5] == '\t')) {
            if (hasId || hasStr) {
                if (!flushStanza()) return false;
            }
            curId.clear();
            if (!DetailGettextPo::ParsePoQuotedString(line.substr(5u), curId)) return false;
            hasId = true;
            active = ActiveField::MsgId;
            continue;
        }

        if (line.rfind("msgstr[", 0) == 0) {
            if (!hasId) return false;
            const size_t rb = line.find(']', 7u);
            if (rb == std::string_view::npos) return false;
            int idx = -1;
            if (!ReaderText::ParseIntSv(line.substr(7u, rb - 7u), idx) || idx < 0 || idx > 15) return false;
            activePluralIdx = static_cast<size_t>(idx);
            std::string& slot = curPluralStrs[activePluralIdx];
            slot.clear();
            if (!DetailGettextPo::ParsePoQuotedString(line.substr(rb + 1u), slot)) return false;
            hasStr = true;
            active = ActiveField::MsgStrPlural;
            continue;
        }

        if (line.rfind("msgstr", 0) == 0 && line.size() > 6u && (line[6] == ' ' || line[6] == '\t')) {
            if (!hasId) return false;
            curStr.clear();
            if (!DetailGettextPo::ParsePoQuotedString(line.substr(6u), curStr)) return false;
            hasStr = true;
            active = ActiveField::MsgStr;
            continue;
        }

        return false; // Unknown syntax -> reject non-PO text cleanly
    }

    if (!flushStanza() || validStanzas == 0u) return false;

    const wchar_t* pfx = (extLower == L".pot")
        ? L"GNU Gettext POT Template (.pot)"
        : L"GNU Gettext PO Catalog (.po)";
    return GettextCommon::EmitCatalogDocument(outDoc, pfx, meta, entries);
}

} // namespace Formats
