#pragma once
// MatlabMat.h — MATLAB Level 5 & v7.3 MAT-File (.mat) Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 10. MATLAB Level 5 MAT-File (.mat — Numeric, Char, Cell, Struct & Sparse)
// ============================================================================
inline bool ReadMatTag(const uint8_t* data, size_t size, size_t& pos, bool be,
    uint32_t& outType, const uint8_t*& outPayload, size_t& outLen)
{
    if (pos + 4 > size) return false;
    uint32_t w0 = be ? RdU32BE(data + pos) : RdU32LE(data + pos);
    if ((w0 >> 16) != 0) { // Small Data Element (4B header + 4B inline)
        if (pos + 8 > size) return false;
        outLen = w0 >> 16;
        outType = w0 & 0xFFFFu;
        if (outLen > 4) return false;
        outPayload = data + pos + 4;
        pos += 8;
        return true;
    }
    if (pos + 8 > size) return false;
    outType = w0;
    uint32_t nBytes = be ? RdU32BE(data + pos + 4) : RdU32LE(data + pos + 4);
    pos += 8;
    if (nBytes > size - pos) return false;
    outPayload = data + pos;
    outLen = nBytes;
    size_t padded = (static_cast<size_t>(nBytes) + 7u) & ~size_t(7u);
    pos += (outType == 14 || outType == 15) ? nBytes : std::min(size - pos, padded);
    return true;
}

inline std::wstring ReadMatNumericAt(uint32_t t, const uint8_t* p, size_t l, bool be, size_t idx, size_t& elemStep) {
    switch (t) {
        case 1:  elemStep = 1; return (idx < l) ? std::to_wstring(static_cast<int8_t>(p[idx])) : L"";
        case 2:  elemStep = 1; return (idx < l) ? std::to_wstring(p[idx]) : L"";
        case 3:  elemStep = 2; return (idx * 2 + 2 <= l) ? std::to_wstring(be ? RdI16BE(p + idx * 2) : RdI16LE(p + idx * 2)) : L"";
        case 4:  elemStep = 2; return (idx * 2 + 2 <= l) ? std::to_wstring(be ? RdU16BE(p + idx * 2) : RdU16LE(p + idx * 2)) : L"";
        case 5:  elemStep = 4; return (idx * 4 + 4 <= l) ? std::to_wstring(be ? RdI32BE(p + idx * 4) : RdI32LE(p + idx * 4)) : L"";
        case 6:  elemStep = 4; return (idx * 4 + 4 <= l) ? std::to_wstring(be ? RdU32BE(p + idx * 4) : RdU32LE(p + idx * 4)) : L"";
        case 7:  elemStep = 4; return (idx * 4 + 4 <= l) ? FmtDouble(be ? RdF32BE(p + idx * 4) : RdF32LE(p + idx * 4)) : L"";
        case 9:  elemStep = 8; return (idx * 8 + 8 <= l) ? FmtDouble(be ? RdF64BE(p + idx * 8) : RdF64LE(p + idx * 8)) : L"";
        case 12: elemStep = 8; return (idx * 8 + 8 <= l) ? std::to_wstring(be ? RdI64BE(p + idx * 8) : RdI64LE(p + idx * 8)) : L"";
        case 13: elemStep = 8; return (idx * 8 + 8 <= l) ? std::to_wstring(be ? RdU64BE(p + idx * 8) : RdU64LE(p + idx * 8)) : L"";
        default: elemStep = 0; return L"";
    }
}

// Compact inline formatter for nested miMATRIX elements inside MATLAB cell & struct arrays
inline std::wstring SummarizeMatNestedMatrix(const uint8_t* mdata, size_t msize, bool be, int depth = 0) {
    if (!mdata || msize < 16 || depth > 6) return L"[]";
    size_t pos = 0;
    uint32_t t = 0; const uint8_t* p = nullptr; size_t l = 0;
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 8) return L"[]";
    uint8_t mxClass = static_cast<uint8_t>((be ? RdU32BE(p) : RdU32LE(p)) & 0xFFu);
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 4) return L"[]";
    uint64_t totalElems = 1;
    for (size_t i = 0; i + 4 <= l; i += 4) {
        totalElems *= static_cast<uint64_t>(std::max<int32_t>(0, be ? RdI32BE(p + i) : RdI32LE(p + i)));
    }
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l)) return L"[]"; // skip name
    if (totalElems == 0) return L"[]";

    if (mxClass == 4) { // char
        if (!ReadMatTag(mdata, msize, pos, be, t, p, l)) return L"''";
        if (t == 16 || t == 1 || t == 2) return CleanAsciiFixed(p, std::min<size_t>(l, 128));
        std::wstring ws;
        for (size_t i = 0; i + 2 <= std::min<size_t>(l, 256); i += 2) {
            uint16_t ch = be ? RdU16BE(p + i) : RdU16LE(p + i);
            if (ch == 0) break;
            ws.push_back(static_cast<wchar_t>(ch));
        }
        return ws;
    }
    if (mxClass >= 6 && mxClass <= 15) {
        if (!ReadMatTag(mdata, msize, pos, be, t, p, l)) return L"[]";
        size_t step = 0;
        ReadMatNumericAt(t, p, l, be, 0, step);
        if (step == 0 || l < step) return L"[]";
        size_t cnt = l / step;
        if (cnt == 1) return ReadMatNumericAt(t, p, l, be, 0, step);
        std::wstring s = L"[";
        for (size_t i = 0; i < std::min<size_t>(cnt, 4); ++i) {
            if (i) s += L", ";
            s += ReadMatNumericAt(t, p, l, be, i, step);
        }
        if (cnt > 4) s += L", ...";
        s += L"]";
        return s;
    }
    if (mxClass == 1) { // nested cell
        std::wstring s = L"{";
        for (uint64_t i = 0; i < std::min<uint64_t>(totalElems, 4) && pos < msize; ++i) {
            if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || t != 14) break;
            size_t pad = ((l + 7u) & ~size_t(7u)) - l;
            if (pad <= msize - pos) pos += pad;
            if (i) s += L", ";
            s += SummarizeMatNestedMatrix(p, l, be, depth + 1);
        }
        s += L"}";
        return s;
    }
    if (mxClass == 2) return L"<struct>";
    if (mxClass == 5) return L"<sparse>";
    return L"<array>";
}

inline bool ParseMatMatrixElement(
    const uint8_t* mdata, size_t msize, bool be,
    SpreadsheetSheet& summarySh, std::vector<SpreadsheetSheet>& varSheets)
{
    size_t pos = 0;
    uint32_t t = 0; const uint8_t* p = nullptr; size_t l = 0;

    // 1. Array Flags (miUINT32 = 6, 8 bytes)
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 8) return false;
    uint32_t flags0 = be ? RdU32BE(p) : RdU32LE(p);
    uint32_t nzmax  = be ? RdU32BE(p + 4) : RdU32LE(p + 4);
    uint8_t mxClass = static_cast<uint8_t>(flags0 & 0xFFu);
    if (mxClass == 0 || mxClass > 17) return false;

    // 2. Dimensions Array (miINT32 = 5)
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 4) return false;
    std::vector<int32_t> dims;
    for (size_t i = 0; i + 4 <= l; i += 4) dims.push_back(be ? RdI32BE(p + i) : RdI32LE(p + i));

    // 3. Array Name
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l)) return false;
    std::wstring varName = CleanAsciiFixed(p, l);
    if (varName.empty()) varName = L"ans";

    std::wstring dimStr = L"[";
    uint64_t totalElems = 1;
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) dimStr += L"x";
        dimStr += std::to_wstring(dims[i]);
        totalElems *= static_cast<uint64_t>(std::max<int32_t>(0, dims[i]));
    }
    dimStr += L"]";

    const wchar_t* clsNames[] = {
        L"unknown", L"cell", L"struct", L"object", L"char", L"sparse", L"double", L"single",
        L"int8", L"uint8", L"int16", L"uint16", L"int32", L"uint32", L"int64", L"uint64"
    };
    std::wstring clsStr = (mxClass < 16) ? clsNames[mxClass] : L"array";
    std::wstring preview;

    auto readChildMatrixAligned = [&](const uint8_t*& cp, size_t& cl) -> bool {
        uint32_t ct = 0;
        if (!ReadMatTag(mdata, msize, pos, be, ct, cp, cl)) return false;
        if (ct != 14) return false;
        size_t pad = ((cl + 7u) & ~size_t(7u)) - cl;
        if (pad <= msize - pos) pos += pad;
        return true;
    };

    if (mxClass == 1 && totalElems > 0) { // mxCELL_CLASS
        size_t nRows = dims.size() >= 1 ? static_cast<size_t>(std::max<int32_t>(1, dims[0])) : 1;
        size_t nCols = dims.size() >= 2 ? static_cast<size_t>(std::max<int32_t>(1, dims[1])) : 1;
        size_t cap = static_cast<size_t>(std::min<uint64_t>(totalElems, 1024));
        std::vector<std::wstring> cells;
        cells.reserve(cap);
        for (size_t i = 0; i < cap && pos < msize; ++i) {
            const uint8_t* cp = nullptr; size_t cl = 0;
            if (!readChildMatrixAligned(cp, cl)) break;
            cells.push_back(SummarizeMatNestedMatrix(cp, cl, be, 0));
        }
        preview = L"{";
        for (size_t i = 0; i < std::min<size_t>(cells.size(), 4); ++i) {
            if (i) preview += L", ";
            preview += cells[i];
        }
        if (cells.size() > 4) preview += L", ...";
        preview += L"}";

        if (varSheets.size() < 16 && !cells.empty()) {
            SpreadsheetSheet vsh;
            vsh.name = varName;
            size_t rCap = std::min<size_t>(nRows, 128);
            size_t cCap = std::min<size_t>(nCols, 32);
            std::vector<std::wstring> hr = { L"Row \\ Col" };
            for (size_t c = 0; c < cCap; ++c) hr.push_back(L"{:," + std::to_wstring(c + 1) + L"}");
            vsh.rows.push_back(std::move(hr));
            for (size_t r = 0; r < rCap; ++r) {
                std::vector<std::wstring> rv = { L"{" + std::to_wstring(r + 1) + L",:}" };
                for (size_t c = 0; c < cCap; ++c) {
                    size_t idx = r + c * nRows; // column-major
                    rv.push_back(idx < cells.size() ? cells[idx] : L"");
                }
                vsh.rows.push_back(std::move(rv));
            }
            FinalizeSheetCols(vsh);
            varSheets.push_back(std::move(vsh));
        }
    } else if (mxClass == 2 && totalElems > 0) { // mxSTRUCT_CLASS
        // 4. Field Name Length (miINT32 = 5)
        if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 4) return false;
        int32_t maxFieldLen = be ? RdI32BE(p) : RdI32LE(p);
        if (maxFieldLen <= 0 || maxFieldLen > 256) return false;
        // 5. Field Names (miINT8 = 1)
        if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || (l % static_cast<size_t>(maxFieldLen)) != 0) return false;
        size_t nFields = l / static_cast<size_t>(maxFieldLen);
        std::vector<std::wstring> fieldNames;
        fieldNames.reserve(nFields);
        for (size_t f = 0; f < nFields; ++f) {
            fieldNames.push_back(CleanAsciiFixed(p + f * static_cast<size_t>(maxFieldLen), static_cast<size_t>(maxFieldLen)));
        }

        SpreadsheetSheet vsh;
        vsh.name = varName;
        std::vector<std::wstring> hr = { L"Element #" };
        for (const auto& fn : fieldNames) hr.push_back(fn);
        vsh.rows.push_back(std::move(hr));

        size_t elemCap = static_cast<size_t>(std::min<uint64_t>(totalElems, 256));
        for (size_t e = 0; e < elemCap && pos < msize; ++e) {
            std::vector<std::wstring> rv = { std::to_wstring(e + 1) };
            for (size_t f = 0; f < nFields; ++f) {
                const uint8_t* cp = nullptr; size_t cl = 0;
                if (!readChildMatrixAligned(cp, cl)) { rv.push_back(L""); continue; }
                std::wstring valStr = SummarizeMatNestedMatrix(cp, cl, be, 0);
                if (e == 0 && f < 3) {
                    if (!preview.empty()) preview += L", ";
                    preview += fieldNames[f] + L"=" + valStr;
                }
                rv.push_back(std::move(valStr));
            }
            vsh.rows.push_back(std::move(rv));
        }
        if (varSheets.size() < 16 && vsh.rows.size() > 1) {
            FinalizeSheetCols(vsh);
            varSheets.push_back(std::move(vsh));
        }
    } else if (mxClass == 5) { // mxSPARSE_CLASS (CSC: ir, jc, pr)
        const uint8_t *irPtr = nullptr, *jcPtr = nullptr, *prPtr = nullptr;
        size_t irLen = 0, jcLen = 0, prLen = 0;
        uint32_t irType = 0, jcType = 0, prType = 0;
        if (!ReadMatTag(mdata, msize, pos, be, irType, irPtr, irLen)) return false;
        if (!ReadMatTag(mdata, msize, pos, be, jcType, jcPtr, jcLen)) return false;
        if (!ReadMatTag(mdata, msize, pos, be, prType, prPtr, prLen)) return false;

        size_t nCols = dims.size() >= 2 ? static_cast<size_t>(std::max<int32_t>(0, dims[1])) : 0;
        size_t jcCount = jcLen / 4;
        size_t irCount = irLen / 4;
        size_t prStep = 0;
        ReadMatNumericAt(prType, prPtr, prLen, be, 0, prStep);
        size_t prCount = (prStep > 0) ? (prLen / prStep) : 0;

        SpreadsheetSheet vsh;
        vsh.name = varName;
        vsh.rows.push_back({ L"Row (1-based)", L"Col (1-based)", L"Non-Zero Value" });

        size_t nnzActual = 0;
        for (size_t c = 0; c < nCols && c + 1 < jcCount; ++c) {
            int32_t kStart = be ? RdI32BE(jcPtr + c * 4) : RdI32LE(jcPtr + c * 4);
            int32_t kEnd   = be ? RdI32BE(jcPtr + (c + 1) * 4) : RdI32LE(jcPtr + (c + 1) * 4);
            if (kStart < 0 || kEnd < kStart) continue;
            for (int32_t k = kStart; k < kEnd && static_cast<size_t>(k) < irCount && static_cast<size_t>(k) < prCount; ++k) {
                int32_t r0 = be ? RdI32BE(irPtr + static_cast<size_t>(k) * 4) : RdI32LE(irPtr + static_cast<size_t>(k) * 4);
                std::wstring vStr = ReadMatNumericAt(prType, prPtr, prLen, be, static_cast<size_t>(k), prStep);
                if (vsh.rows.size() <= 500) {
                    vsh.rows.push_back({ std::to_wstring(r0 + 1), std::to_wstring(c + 1), vStr });
                }
                if (nnzActual < 3) {
                    if (!preview.empty()) preview += L", ";
                    preview += L"(" + std::to_wstring(r0 + 1) + L"," + std::to_wstring(c + 1) + L")=" + vStr;
                }
                ++nnzActual;
            }
        }
        preview = L"nnz=" + std::to_wstring(nnzActual) + L" (nzmax=" + std::to_wstring(nzmax) + L") " + preview;
        if (varSheets.size() < 16 && vsh.rows.size() > 1) {
            FinalizeSheetCols(vsh);
            varSheets.push_back(std::move(vsh));
        }
    } else if (pos < msize && ReadMatTag(mdata, msize, pos, be, t, p, l)) {
        if (mxClass == 4) { // mxCHAR_CLASS
            if (t == 16 || t == 1 || t == 2) {
                preview = CleanAsciiFixed(p, std::min<size_t>(l, 256));
            } else if (t == 4 || t == 17) {
                for (size_t i = 0; i + 2 <= std::min<size_t>(l, 512); i += 2) {
                    uint16_t ch = be ? RdU16BE(p + i) : RdU16LE(p + i);
                    if (ch == 0) break;
                    preview.push_back(static_cast<wchar_t>(ch));
                }
            }
            if (varSheets.size() < 16) {
                SpreadsheetSheet vsh;
                vsh.name = varName;
                vsh.rows.push_back({ varName + L" (char " + dimStr + L")" });
                vsh.rows.push_back({ preview });
                FinalizeSheetCols(vsh);
                varSheets.push_back(std::move(vsh));
            }
        } else if (mxClass >= 6 && mxClass <= 15 && totalElems > 0) {
            size_t step = 0;
            ReadMatNumericAt(t, p, l, be, 0, step);
            if (step > 0 && l >= step) {
                size_t availElems = l / step;
                for (size_t i = 0; i < std::min<size_t>(availElems, 4); ++i) {
                    if (i > 0) preview += L", ";
                    preview += ReadMatNumericAt(t, p, l, be, i, step);
                }
                if (varSheets.size() < 16) {
                    SpreadsheetSheet vsh;
                    vsh.name = varName;
                    size_t nRows = dims.size() >= 1 ? static_cast<size_t>(std::max<int32_t>(1, dims[0])) : 1;
                    size_t nCols = dims.size() >= 2 ? static_cast<size_t>(std::max<int32_t>(1, dims[1])) : 1;
                    size_t rCap = std::min<size_t>(nRows, 256);
                    size_t cCap = std::min<size_t>(nCols, 64);
                    std::vector<std::wstring> hr = { L"Row \\ Col" };
                    for (size_t c = 0; c < cCap; ++c) hr.push_back(L"(:," + std::to_wstring(c + 1) + L")");
                    vsh.rows.push_back(std::move(hr));
                    for (size_t r = 0; r < rCap; ++r) {
                        std::vector<std::wstring> rv = { L"(" + std::to_wstring(r + 1) + L",:)" };
                        for (size_t c = 0; c < cCap; ++c) {
                            size_t idx = r + c * nRows;
                            rv.push_back(idx < availElems ? ReadMatNumericAt(t, p, l, be, idx, step) : L"");
                        }
                        vsh.rows.push_back(std::move(rv));
                    }
                    FinalizeSheetCols(vsh);
                    varSheets.push_back(std::move(vsh));
                }
            }
        }
    }

    summarySh.rows.push_back({ varName, clsStr, dimStr, preview });
    return true;
}

inline bool ParseMatlabMat(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    // Check v7.3 HDF5 first (at 0 or 512)
    if ((raw.size() >= 36 && std::memcmp(raw.data(), "\x89HDF\r\n\x1A\n", 8) == 0) ||
        (raw.size() >= 548 && std::memcmp(raw.data() + 512, "\x89HDF\r\n\x1A\n", 8) == 0))
    {
        return ParseHdf5Doc(raw, L".mat", out);
    }

    if (raw.size() < 128) return false;
    bool be = false;
    if (raw[126] == 'I' && raw[127] == 'M') be = false;
    else if (raw[126] == 'M' && raw[127] == 'I') be = true;
    else return false;

    uint16_t matVer = be ? RdU16BE(raw.data() + 124) : RdU16LE(raw.data() + 124);
    if (matVer != 0x0100 && matVer != 0x0200) return false;

    SpreadsheetSheet summary;
    summary.name = L"MAT_Workspace";
    summary.rows.push_back({ L"Variable", L"MATLAB Class", L"Dimensions", L"Preview" });
    summary.rows.push_back({ L"(header)", L"Level 5 MAT", be ? L"Big-Endian (MI)" : L"Little-Endian (IM)", CleanAsciiFixed(raw.data(), 116) });

    std::vector<SpreadsheetSheet> varSheets;
    size_t pos = 128;
    size_t validVars = 0;
    while (pos + 8 <= raw.size()) {
        uint32_t t = 0; const uint8_t* p = nullptr; size_t l = 0;
        if (!ReadMatTag(raw.data(), raw.size(), pos, be, t, p, l)) return false;
        if (t == 14) { // miMATRIX
            if (!ParseMatMatrixElement(p, l, be, summary, varSheets)) return false;
            ++validVars;
        } else if (t == 15) { // miCOMPRESSED (zlib)
            std::vector<uint8_t> uncomp;
            if (!ZipDeflate::InflateZlib(p, l, uncomp, 32 * 1024 * 1024) &&
                !ZipDeflate::InflateRaw(p, l, uncomp, 32 * 1024 * 1024))
            {
                return false; // Corrupt zlib compressed variable!
            }
            size_t ipos = 0; uint32_t it = 0; const uint8_t* ip = nullptr; size_t il = 0;
            if (!ReadMatTag(uncomp.data(), uncomp.size(), ipos, be, it, ip, il) || it != 14) return false;
            if (!ParseMatMatrixElement(ip, il, be, summary, varSheets)) return false;
            ++validVars;
        } else {
            return false;
        }
        pos = (pos + 7u) & ~size_t(7u);
    }

    if (validVars == 0) return false;

    FinalizeSheetCols(summary);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MATLAB Level 5 Workspace (.mat)";
    out.sheets.push_back(std::move(summary));
    for (auto& s : varSheets) out.sheets.push_back(std::move(s));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseMatlabMat(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseMatlabMat(raw, out); }

} // namespace Formats
