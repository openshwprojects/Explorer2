#pragma once
// WebAssemblyWasm.h — W3C WebAssembly Binary Module (.wasm) Section, Type, Import, Export, Function & Code Inspector

namespace Formats {

namespace DetailWasm {

inline bool ReadU32Leb(const uint8_t*& p, const uint8_t* end, uint32_t& outVal) {
    outVal = 0;
    int shift = 0;
    for (int i = 0; i < 5; ++i) {
        if (p >= end) return false;
        const uint8_t b = *p++;
        if (i == 4 && (b & 0xF0u) != 0) return false;
        outVal |= static_cast<uint32_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) return true;
        shift += 7;
    }
    return false;
}

inline bool ReadName(const uint8_t*& p, const uint8_t* end, std::wstring& outStr) {
    uint32_t len = 0;
    if (!ReadU32Leb(p, end, len) || static_cast<size_t>(end - p) < len) return false;
    outStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), len));
    p += len;
    return true;
}

inline const wchar_t* ValTypeName(uint8_t vt) {
    switch (vt) {
        case 0x7F: return L"i32";
        case 0x7E: return L"i64";
        case 0x7D: return L"f32";
        case 0x7C: return L"f64";
        case 0x7B: return L"v128";
        case 0x70: return L"funcref";
        case 0x6F: return L"externref";
        default:   return L"val";
    }
}

inline bool ReadLimits(const uint8_t*& p, const uint8_t* end, std::wstring& outLim, bool isPages) {
    if (p >= end) return false;
    const uint8_t flags = *p++;
    uint32_t minV = 0, maxV = 0;
    if (!ReadU32Leb(p, end, minV)) return false;
    const bool hasMax = (flags & 0x01u) != 0;
    if (hasMax && !ReadU32Leb(p, end, maxV)) return false;
    outLim = L"min=" + std::to_wstring(minV);
    if (hasMax) outLim += L", max=" + std::to_wstring(maxV);
    if (isPages) {
        outLim += L" pages (" + PathUtils::FormatByteSize(static_cast<uint64_t>(minV) * 65536ULL) + L")";
    }
    if (flags & 0x02u) outLim += L" [shared]";
    if (flags & 0x04u) outLim += L" [memory64]";
    return true;
}

} // namespace DetailWasm

inline bool ParseWebAssemblyWasmDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 8 || raw[0] != 0x00 || raw[1] != 'a' || raw[2] != 's' || raw[3] != 'm') return false;

    uint32_t version = 0;
    std::memcpy(&version, raw.data() + 4, 4);
    if (version != 1 && version != 0x0001000Du) return false;

    static const wchar_t* const kSecNames[14] = {
        L"Custom (0)", L"Type (1)", L"Import (2)", L"Function (3)",
        L"Table (4)", L"Memory (5)", L"Global (6)", L"Export (7)",
        L"Start (8)", L"Element (9)", L"Code (10)", L"Data (11)",
        L"DataCount (12)", L"Tag (13)"
    };

    SpreadsheetSheet secSheet;
    secSheet.name = L"WASM_Sections";
    secSheet.maxCols = 6;
    secSheet.rows.push_back({ L"Section #", L"Section ID", L"Section Name", L"Offset", L"Payload Size", L"Decoded Summary" });
    secSheet.rows.push_back({ L"Header", L"0x0061736D", L"\\0asm Magic", L"0x000000", L"8 B", L"Version " + std::to_wstring(version) });

    std::vector<std::wstring> funcSignatures;
    std::vector<uint32_t> importedFuncSigIdx;
    std::vector<uint32_t> definedFuncSigIdx;
    std::map<uint32_t, std::wstring> debugFuncNames;
    std::wstring debugModuleName;

    struct ImportRow { std::wstring moduleName, fieldName, kind, detail; };
    struct ExportRow { std::wstring name, kind; uint32_t index = 0; };
    struct CodeInfo  { uint32_t bodyBytes = 0; uint32_t codeBytes = 0; std::wstring localsDesc; };

    std::vector<ImportRow> imports;
    std::vector<ExportRow> exports;
    std::vector<CodeInfo>  codes;
    std::vector<std::wstring> memDescs;
    std::vector<std::wstring> globalDescs;
    std::vector<std::wstring> customDescs;

    const uint8_t* ptr = raw.data() + 8;
    const uint8_t* const fileEnd = raw.data() + raw.size();
    int secIdx = 1;

    while (ptr < fileEnd && secIdx <= 1024) {
        const uint8_t id = *ptr++;
        if (id > 13) return false;

        uint32_t payloadLen = 0;
        if (!DetailWasm::ReadU32Leb(ptr, fileEnd, payloadLen)) return false;
        if (static_cast<size_t>(fileEnd - ptr) < payloadLen) return false;

        const size_t secOffset = static_cast<size_t>(ptr - raw.data());
        const uint8_t* sp = ptr;
        const uint8_t* const sEnd = ptr + payloadLen;
        std::wstring summary;

        if (id == 0) { // Custom section
            std::wstring cname;
            if (DetailWasm::ReadName(sp, sEnd, cname)) {
                summary = L"Custom \"" + cname + L"\"";
                if (cname == L"name") {
                    uint32_t namedFuncs = 0;
                    while (sp < sEnd) {
                        const uint8_t subId = *sp++;
                        uint32_t subLen = 0;
                        if (!DetailWasm::ReadU32Leb(sp, sEnd, subLen) || static_cast<size_t>(sEnd - sp) < subLen) break;
                        const uint8_t* np = sp;
                        const uint8_t* nEnd = sp + subLen;
                        if (subId == 0) {
                            DetailWasm::ReadName(np, nEnd, debugModuleName);
                        } else if (subId == 1) {
                            uint32_t cnt = 0;
                            if (DetailWasm::ReadU32Leb(np, nEnd, cnt)) {
                                for (uint32_t k = 0; k < cnt && np < nEnd && k < 4096; ++k) {
                                    uint32_t fidx = 0;
                                    std::wstring fname;
                                    if (!DetailWasm::ReadU32Leb(np, nEnd, fidx) || !DetailWasm::ReadName(np, nEnd, fname)) break;
                                    debugFuncNames[fidx] = fname;
                                    ++namedFuncs;
                                }
                            }
                        }
                        sp += subLen;
                    }
                    if (!debugModuleName.empty()) summary += L" (module=" + debugModuleName + L")";
                    if (namedFuncs > 0) summary += L" (" + std::to_wstring(namedFuncs) + L" function names)";
                }
                customDescs.push_back(summary + L" [" + std::to_wstring(payloadLen) + L" B]");
            }
        } else if (id == 1) { // Type section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                for (uint32_t i = 0; i < count && sp < sEnd && i < 4096; ++i) {
                    const uint8_t form = *sp++;
                    if (form != 0x60) {
                        funcSignatures.push_back(L"(non-func type)");
                        break;
                    }
                    uint32_t pCnt = 0, rCnt = 0;
                    if (!DetailWasm::ReadU32Leb(sp, sEnd, pCnt) || static_cast<size_t>(sEnd - sp) < pCnt) break;
                    std::wstring sig = L"(";
                    for (uint32_t k = 0; k < pCnt; ++k) {
                        if (k > 0) sig += L", ";
                        sig += DetailWasm::ValTypeName(*sp++);
                    }
                    sig += L") -> (";
                    if (!DetailWasm::ReadU32Leb(sp, sEnd, rCnt) || static_cast<size_t>(sEnd - sp) < rCnt) break;
                    for (uint32_t k = 0; k < rCnt; ++k) {
                        if (k > 0) sig += L", ";
                        sig += DetailWasm::ValTypeName(*sp++);
                    }
                    sig += L")";
                    funcSignatures.push_back(std::move(sig));
                }
                summary = std::to_wstring(funcSignatures.size()) + L" function signature(s)";
            }
        } else if (id == 2) { // Import section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                for (uint32_t i = 0; i < count && sp < sEnd && i < 4096; ++i) {
                    ImportRow ir;
                    if (!DetailWasm::ReadName(sp, sEnd, ir.moduleName) || !DetailWasm::ReadName(sp, sEnd, ir.fieldName) || sp >= sEnd) break;
                    const uint8_t kind = *sp++;
                    if (kind == 0) {
                        uint32_t sigIdx = 0;
                        if (!DetailWasm::ReadU32Leb(sp, sEnd, sigIdx)) break;
                        ir.kind = L"func";
                        ir.detail = (sigIdx < funcSignatures.size()) ? funcSignatures[sigIdx] : (L"type[" + std::to_wstring(sigIdx) + L"]");
                        importedFuncSigIdx.push_back(sigIdx);
                    } else if (kind == 1) {
                        if (sp >= sEnd) break;
                        const uint8_t elemType = *sp++;
                        std::wstring lim;
                        if (!DetailWasm::ReadLimits(sp, sEnd, lim, false)) break;
                        ir.kind = L"table";
                        ir.detail = std::wstring(DetailWasm::ValTypeName(elemType)) + L" " + lim;
                    } else if (kind == 2) {
                        ir.kind = L"memory";
                        if (!DetailWasm::ReadLimits(sp, sEnd, ir.detail, true)) break;
                    } else if (kind == 3) {
                        if (sp + 2 > sEnd) break;
                        const uint8_t vt = *sp++;
                        const uint8_t mut = *sp++;
                        ir.kind = L"global";
                        ir.detail = std::wstring(mut ? L"mut " : L"const ") + DetailWasm::ValTypeName(vt);
                    } else {
                        break;
                    }
                    imports.push_back(std::move(ir));
                }
                summary = std::to_wstring(imports.size()) + L" import(s)";
            }
        } else if (id == 3) { // Function section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                for (uint32_t i = 0; i < count && sp < sEnd && i < 16384; ++i) {
                    uint32_t sigIdx = 0;
                    if (!DetailWasm::ReadU32Leb(sp, sEnd, sigIdx)) break;
                    definedFuncSigIdx.push_back(sigIdx);
                }
                summary = std::to_wstring(definedFuncSigIdx.size()) + L" defined function(s)";
            }
        } else if (id == 4) { // Table section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                summary = std::to_wstring(count) + L" table(s)";
            }
        } else if (id == 5) { // Memory section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                for (uint32_t i = 0; i < count && sp < sEnd && i < 64; ++i) {
                    std::wstring lim;
                    if (!DetailWasm::ReadLimits(sp, sEnd, lim, true)) break;
                    memDescs.push_back(lim);
                }
                summary = std::to_wstring(memDescs.size()) + L" linear memory(" + (memDescs.empty() ? L"" : memDescs[0]) + L")";
            }
        } else if (id == 6) { // Global section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                for (uint32_t i = 0; i < count && sp < sEnd && i < 2048; ++i) {
                    if (sp + 2 > sEnd) break;
                    const uint8_t vt = *sp++;
                    const uint8_t mut = *sp++;
                    while (sp < sEnd && *sp != 0x0B) ++sp;
                    if (sp < sEnd && *sp == 0x0B) ++sp;
                    globalDescs.push_back(std::wstring(mut ? L"mut " : L"const ") + DetailWasm::ValTypeName(vt));
                }
                summary = std::to_wstring(globalDescs.size()) + L" global(s)";
            }
        } else if (id == 7) { // Export section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                for (uint32_t i = 0; i < count && sp < sEnd && i < 4096; ++i) {
                    ExportRow er;
                    if (!DetailWasm::ReadName(sp, sEnd, er.name) || sp >= sEnd) break;
                    const uint8_t kind = *sp++;
                    if (!DetailWasm::ReadU32Leb(sp, sEnd, er.index)) break;
                    er.kind = (kind == 0) ? L"func" : ((kind == 1) ? L"table" : ((kind == 2) ? L"memory" : ((kind == 3) ? L"global" : L"other")));
                    exports.push_back(std::move(er));
                }
                summary = std::to_wstring(exports.size()) + L" export(s)";
            }
        } else if (id == 8) { // Start section
            uint32_t startIdx = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, startIdx)) {
                summary = L"Start function index " + std::to_wstring(startIdx);
            }
        } else if (id == 9) { // Element section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                summary = std::to_wstring(count) + L" element segment(s)";
            }
        } else if (id == 10) { // Code section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                for (uint32_t i = 0; i < count && sp < sEnd && i < 16384; ++i) {
                    uint32_t bodySz = 0;
                    if (!DetailWasm::ReadU32Leb(sp, sEnd, bodySz) || static_cast<size_t>(sEnd - sp) < bodySz) break;
                    const uint8_t* bp = sp;
                    const uint8_t* bEnd = sp + bodySz;
                    uint32_t localGroupCnt = 0;
                    std::wstring locDesc;
                    if (DetailWasm::ReadU32Leb(bp, bEnd, localGroupCnt)) {
                        for (uint32_t g = 0; g < localGroupCnt && bp < bEnd && g < 256; ++g) {
                            uint32_t nLoc = 0;
                            if (!DetailWasm::ReadU32Leb(bp, bEnd, nLoc) || bp >= bEnd) break;
                            const uint8_t vt = *bp++;
                            if (!locDesc.empty()) locDesc += L", ";
                            locDesc += std::to_wstring(nLoc) + L"x" + DetailWasm::ValTypeName(vt);
                        }
                    }
                    CodeInfo ci;
                    ci.bodyBytes = bodySz;
                    ci.codeBytes = static_cast<uint32_t>(bEnd - bp);
                    ci.localsDesc = locDesc.empty() ? L"none" : locDesc;
                    codes.push_back(std::move(ci));
                    sp += bodySz;
                }
                summary = std::to_wstring(codes.size()) + L" function body(s)";
            }
        } else if (id == 11) { // Data section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                summary = std::to_wstring(count) + L" data segment(s)";
            }
        } else if (id == 12) { // DataCount section
            uint32_t count = 0;
            if (DetailWasm::ReadU32Leb(sp, sEnd, count)) {
                summary = std::to_wstring(count) + L" declared data segment(s)";
            }
        }

        wchar_t offBuf[32] = {};
        swprintf_s(offBuf, L"0x%06zX", secOffset);
        secSheet.rows.push_back({
            std::to_wstring(secIdx++),
            std::to_wstring(id),
            kSecNames[id],
            offBuf,
            std::to_wstring(payloadLen) + L" B",
            summary
        });

        ptr += payloadLen;
    }

    auto resolveFuncSig = [&](uint32_t funcIdx) -> std::wstring {
        uint32_t sigIdx = 0xFFFFFFFFu;
        if (funcIdx < importedFuncSigIdx.size()) {
            sigIdx = importedFuncSigIdx[funcIdx];
        } else {
            const size_t defIdx = funcIdx - importedFuncSigIdx.size();
            if (defIdx < definedFuncSigIdx.size()) sigIdx = definedFuncSigIdx[defIdx];
        }
        return (sigIdx < funcSignatures.size()) ? funcSignatures[sigIdx] : L"";
    };

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"W3C WebAssembly Binary Module (.wasm)";
    out.sheets.push_back(std::move(secSheet));

    if (!funcSignatures.empty()) {
        SpreadsheetSheet tySh;
        tySh.name = L"Types";
        tySh.maxCols = 2;
        tySh.rows.push_back({ L"Type Index", L"Function Signature" });
        for (size_t i = 0; i < funcSignatures.size() && i < 2000; ++i) {
            tySh.rows.push_back({ L"type[" + std::to_wstring(i) + L"]", funcSignatures[i] });
        }
        out.sheets.push_back(std::move(tySh));
    }

    if (!imports.empty() || !exports.empty()) {
        SpreadsheetSheet ieSh;
        ieSh.name = L"Imports_And_Exports";
        ieSh.maxCols = 5;
        ieSh.rows.push_back({ L"Direction", L"Module / Name", L"Field / Symbol", L"Kind", L"Signature / Details" });
        for (size_t i = 0; i < imports.size() && ieSh.rows.size() <= 2000; ++i) {
            ieSh.rows.push_back({ L"Import", imports[i].moduleName, imports[i].fieldName, imports[i].kind, imports[i].detail });
        }
        for (size_t i = 0; i < exports.size() && ieSh.rows.size() <= 4000; ++i) {
            std::wstring det = L"index " + std::to_wstring(exports[i].index);
            if (exports[i].kind == L"func") {
                std::wstring sig = resolveFuncSig(exports[i].index);
                if (!sig.empty()) det += L" " + sig;
            }
            ieSh.rows.push_back({ L"Export", L"(self)", exports[i].name, exports[i].kind, det });
        }
        out.sheets.push_back(std::move(ieSh));
    }

    if (!definedFuncSigIdx.empty() || !codes.empty()) {
        SpreadsheetSheet fnSh;
        fnSh.name = L"Functions";
        fnSh.maxCols = 5;
        fnSh.rows.push_back({ L"Func Index", L"Debug Name", L"Signature", L"Locals", L"Bytecode Size" });
        const size_t n = std::max(definedFuncSigIdx.size(), codes.size());
        for (size_t i = 0; i < n && i < 2000; ++i) {
            const uint32_t fIdx = static_cast<uint32_t>(importedFuncSigIdx.size() + i);
            auto itName = debugFuncNames.find(fIdx);
            const std::wstring fname = (itName != debugFuncNames.end()) ? itName->second : (L"$func" + std::to_wstring(fIdx));
            const std::wstring sig = resolveFuncSig(fIdx);
            const std::wstring locs = (i < codes.size()) ? codes[i].localsDesc : L"-";
            const std::wstring bsz  = (i < codes.size()) ? (std::to_wstring(codes[i].codeBytes) + L" B (body " + std::to_wstring(codes[i].bodyBytes) + L" B)") : L"-";
            fnSh.rows.push_back({ std::to_wstring(fIdx), fname, sig, locs, bsz });
        }
        out.sheets.push_back(std::move(fnSh));
    }

    if (!memDescs.empty() || !globalDescs.empty() || !customDescs.empty()) {
        SpreadsheetSheet mgSh;
        mgSh.name = L"Memory_Globals_Custom";
        mgSh.maxCols = 3;
        mgSh.rows.push_back({ L"Category", L"Index", L"Descriptor" });
        for (size_t i = 0; i < memDescs.size(); ++i)    mgSh.rows.push_back({ L"Linear Memory", std::to_wstring(i), memDescs[i] });
        for (size_t i = 0; i < globalDescs.size(); ++i) mgSh.rows.push_back({ L"Global", std::to_wstring(i), globalDescs[i] });
        for (size_t i = 0; i < customDescs.size(); ++i) mgSh.rows.push_back({ L"Custom Section", std::to_wstring(i), customDescs[i] });
        out.sheets.push_back(std::move(mgSh));
    }

    out.valid = !out.sheets.empty();
    return out.valid;
}

} // namespace Formats
