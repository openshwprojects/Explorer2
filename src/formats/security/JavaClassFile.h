#pragma once
// JavaClassFile.h — Java JVM Bytecode Class File (.class — 0xCAFEBABE)

namespace Formats {

// ============================================================================
// 43. Java JVM Bytecode Class File (.class — 0xCAFEBABE Constant Pool, Fields & Methods)
// ============================================================================
inline bool ParseJavaClassFile(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 24 || DetailBinarySysDoc::RdU32BE(raw.data()) != 0xCAFEBABEu) return false;

    const uint16_t minorV = DetailBinarySysDoc::RdU16BE(raw.data() + 4);
    const uint16_t majorV = DetailBinarySysDoc::RdU16BE(raw.data() + 6);
    const uint16_t cpCnt  = DetailBinarySysDoc::RdU16BE(raw.data() + 8);
    if (majorV < 45 || majorV > 100 || cpCnt < 1) return false;

    std::vector<std::string> cpUtf8(cpCnt);
    std::vector<uint16_t> cpClassRef(cpCnt, 0);
    size_t pos = 10;

    for (uint16_t i = 1; i < cpCnt; ++i) {
        if (pos >= raw.size()) return false;
        const uint8_t tag = raw[pos++];
        switch (tag) {
            case 1: { // CONSTANT_Utf8
                if (pos + 2 > raw.size()) return false;
                const uint16_t len = DetailBinarySysDoc::RdU16BE(raw.data() + pos);
                pos += 2;
                if (pos + len > raw.size()) return false;
                cpUtf8[i].assign(reinterpret_cast<const char*>(raw.data() + pos), len);
                pos += len;
                break;
            }
            case 3: // Integer
            case 4: // Float
            case 9: // Fieldref
            case 10:// Methodref
            case 11:// InterfaceMethodref
            case 12:// NameAndType
            case 17:// Dynamic
            case 18:// InvokeDynamic
                if (pos + 4 > raw.size()) return false;
                pos += 4;
                break;
            case 5: // Long (2 slots!)
            case 6: // Double (2 slots!)
                if (pos + 8 > raw.size()) return false;
                pos += 8;
                ++i;
                break;
            case 7: // Class
            case 19:// Module
            case 20:// Package
                if (pos + 2 > raw.size()) return false;
                cpClassRef[i] = DetailBinarySysDoc::RdU16BE(raw.data() + pos);
                pos += 2;
                break;
            case 8: // String
            case 16:// MethodType
                if (pos + 2 > raw.size()) return false;
                pos += 2;
                break;
            case 15:// MethodHandle
                if (pos + 3 > raw.size()) return false;
                pos += 3;
                break;
            default:
                return false;
        }
    }

    if (pos + 8 > raw.size()) return false;
    const uint16_t accessFlags = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 0);
    const uint16_t thisClass   = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 2);
    const uint16_t superClass  = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 4);
    const uint16_t ifaceCount  = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 6);
    pos += 8;

    auto getUtf8 = [&](uint16_t idx) -> std::wstring {
        if (idx > 0 && idx < cpUtf8.size()) return PathUtils::Utf8ToWide(cpUtf8[idx]);
        return L"";
    };
    auto getClassName = [&](uint16_t clsIdx) -> std::wstring {
        if (clsIdx == 0 || clsIdx >= cpClassRef.size()) return L"(none)";
        std::wstring s = getUtf8(cpClassRef[clsIdx]);
        for (wchar_t& ch : s) if (ch == L'/') ch = L'.';
        return s.empty() ? L"(unknown)" : s;
    };

    if (pos + static_cast<size_t>(ifaceCount) * 2 > raw.size()) return false;
    std::wstring ifaceList;
    for (uint16_t i = 0; i < ifaceCount; ++i) {
        const uint16_t ii = DetailBinarySysDoc::RdU16BE(raw.data() + pos + i * 2);
        if (i > 0) ifaceList += L", ";
        ifaceList += getClassName(ii);
    }
    pos += static_cast<size_t>(ifaceCount) * 2;

    auto fmtMemberFlags = [](uint16_t f, bool isMethod) -> std::wstring {
        std::wstring s;
        if (f & 0x0001u) s += L"public ";
        if (f & 0x0002u) s += L"private ";
        if (f & 0x0004u) s += L"protected ";
        if (f & 0x0008u) s += L"static ";
        if (f & 0x0010u) s += L"final ";
        if (isMethod && (f & 0x0020u)) s += L"synchronized ";
        if (!isMethod && (f & 0x0040u)) s += L"volatile ";
        if (isMethod && (f & 0x0100u)) s += L"native ";
        if (f & 0x0400u) s += L"abstract ";
        return PathUtils::Trim(s);
    };

    SpreadsheetSheet memSh;
    memSh.name = L"Fields_And_Methods";
    memSh.maxCols = 5;
    memSh.rows.push_back({ L"Member Kind", L"Modifiers", L"Name", L"JVM Descriptor", L"Bytecode / Details" });

    uint16_t fieldsCnt = 0, methodsCnt = 0;
    if (pos + 2 <= raw.size()) {
        fieldsCnt = DetailBinarySysDoc::RdU16BE(raw.data() + pos);
        pos += 2;
        for (uint16_t f = 0; f < fieldsCnt && pos + 8 <= raw.size(); ++f) {
            const uint16_t ff   = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 0);
            const uint16_t fn   = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 2);
            const uint16_t fd   = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 4);
            const uint16_t acnt = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 6);
            pos += 8;
            for (uint16_t a = 0; a < acnt && pos + 6 <= raw.size(); ++a) {
                const uint32_t alen = DetailBinarySysDoc::RdU32BE(raw.data() + pos + 2);
                pos += 6;
                if (pos + alen > raw.size()) { pos = raw.size(); break; }
                pos += alen;
            }
            if (memSh.rows.size() <= 2000) {
                memSh.rows.push_back({ L"Field", fmtMemberFlags(ff, false), getUtf8(fn), getUtf8(fd), L"-" });
            }
        }
    }

    if (pos + 2 <= raw.size()) {
        methodsCnt = DetailBinarySysDoc::RdU16BE(raw.data() + pos);
        pos += 2;
        for (uint16_t m = 0; m < methodsCnt && pos + 8 <= raw.size(); ++m) {
            const uint16_t mf   = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 0);
            const uint16_t mn   = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 2);
            const uint16_t md   = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 4);
            const uint16_t acnt = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 6);
            pos += 8;
            std::wstring codeInfo = L"(abstract/native)";
            for (uint16_t a = 0; a < acnt && pos + 6 <= raw.size(); ++a) {
                const uint16_t aname = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 0);
                const uint32_t alen  = DetailBinarySysDoc::RdU32BE(raw.data() + pos + 2);
                pos += 6;
                if (pos + alen > raw.size()) { pos = raw.size(); break; }
                if (aname < cpUtf8.size() && cpUtf8[aname] == "Code") {
                    if (majorV >= 46 && alen >= 8) {
                        const uint16_t maxStack  = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 0);
                        const uint16_t maxLocals = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 2);
                        const uint32_t codeLen   = DetailBinarySysDoc::RdU32BE(raw.data() + pos + 4);
                        codeInfo = L"stack=" + std::to_wstring(maxStack) + L", locals=" + std::to_wstring(maxLocals) + L", code=" + std::to_wstring(codeLen) + L" B";
                    } else if (alen >= 4) {
                        codeInfo = L"stack=" + std::to_wstring(raw[pos]) + L", locals=" + std::to_wstring(raw[pos + 1]) + L", code=" + std::to_wstring(DetailBinarySysDoc::RdU16BE(raw.data() + pos + 2)) + L" B";
                    }
                }
                pos += alen;
            }
            if (memSh.rows.size() <= 2000) {
                memSh.rows.push_back({ L"Method", fmtMemberFlags(mf, true), getUtf8(mn), getUtf8(md), codeInfo });
            }
        }
    }

    std::wstring sourceFile;
    if (pos + 2 <= raw.size()) {
        const uint16_t clsAttrs = DetailBinarySysDoc::RdU16BE(raw.data() + pos);
        pos += 2;
        for (uint16_t a = 0; a < clsAttrs && pos + 6 <= raw.size(); ++a) {
            const uint16_t aname = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 0);
            const uint32_t alen  = DetailBinarySysDoc::RdU32BE(raw.data() + pos + 2);
            pos += 6;
            if (pos + alen > raw.size()) break;
            if (aname < cpUtf8.size() && cpUtf8[aname] == "SourceFile" && alen == 2) {
                sourceFile = getUtf8(DetailBinarySysDoc::RdU16BE(raw.data() + pos));
            }
            pos += alen;
        }
    }

    std::wstring jdkVer = (majorV >= 49) ? (L"Java SE " + std::to_wstring(majorV - 44)) : (L"Java 1." + std::to_wstring(majorV - 44));
    std::wstring clsFlags;
    if (accessFlags & 0x0001u) clsFlags += L"public ";
    if (accessFlags & 0x0010u) clsFlags += L"final ";
    if (accessFlags & 0x0200u) clsFlags += L"interface ";
    if (accessFlags & 0x0400u) clsFlags += L"abstract ";
    if (accessFlags & 0x2000u) clsFlags += L"@interface ";
    if (accessFlags & 0x4000u) clsFlags += L"enum ";
    if (accessFlags & 0x8000u) clsFlags += L"module ";

    SpreadsheetSheet clsSh;
    clsSh.name = L"JVM_Class";
    clsSh.maxCols = 2;
    clsSh.rows = {
        { L"JVM Class Property", L"Value" },
        { L"Class File Version", std::to_wstring(majorV) + L"." + std::to_wstring(minorV) + L" (" + jdkVer + L")" },
        { L"Class Name", getClassName(thisClass) },
        { L"Superclass", getClassName(superClass) },
        { L"Class Modifiers", clsFlags.empty() ? L"(package-private)" : PathUtils::Trim(clsFlags) },
        { L"Implemented Interfaces", ifaceList.empty() ? L"(none)" : ifaceList },
        { L"Constant Pool Count", std::to_wstring(cpCnt) },
        { L"Declared Fields / Methods", std::to_wstring(fieldsCnt) + L" / " + std::to_wstring(methodsCnt) }
    };
    if (!sourceFile.empty()) clsSh.rows.push_back({ L"Source File", sourceFile });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Java Bytecode Class (.class)";
    out.sheets.push_back(std::move(clsSh));
    if (memSh.rows.size() > 1) out.sheets.push_back(std::move(memSh));
    out.valid = true;
    return true;
}


} // namespace Formats
