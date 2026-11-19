#pragma once
// RSerializedRds.h — R Serialized Data (.rds) Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 14. R Serialized Data (.rds — XDR 'X' / Native 'B', Gzipped or Raw)
// ============================================================================
struct RNode {
    uint8_t sexpType = 0;
    std::string sym;
    std::vector<std::wstring> strs; // atomic values or STRSXP strings
    std::vector<RNode> listItems;   // VECSXP elements
    std::vector<std::pair<std::string, RNode>> attrs;

    const RNode* FindAttr(std::string_view k) const {
        for (const auto& kv : attrs) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct RdsReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    bool be = true;
    std::vector<std::string> refTable; // 1-based reference table for SYMSXP / ENVSXP
    size_t nodesDecoded = 0;

    bool RdI32(int32_t& v) {
        if (pos + 4 > size) return false;
        v = be ? RdI32BE(data + pos) : RdI32LE(data + pos);
        pos += 4;
        return true;
    }
    bool RdF64(double& v) {
        if (pos + 8 > size) return false;
        v = be ? RdF64BE(data + pos) : RdF64LE(data + pos);
        pos += 8;
        return true;
    }

    bool ReadItem(RNode& out, int depth = 0) {
        if (depth > 48 || ++nodesDecoded > 50000) return false;
        int32_t flags = 0;
        if (!RdI32(flags)) return false;

        uint8_t stype = static_cast<uint8_t>(static_cast<uint32_t>(flags) & 0xFFu);
        bool hasAttr = (flags & (1 << 9)) != 0;
        bool hasTag  = (flags & (1 << 10)) != 0;
        out.sexpType = stype;

        if (stype == 254 || stype == 0) { // NILVALUE_SXP / NILSXP
            return true;
        }
        if (stype == 255) { // REFSXP
            int32_t refIdx = static_cast<int32_t>(static_cast<uint32_t>(flags) >> 8);
            if (refIdx == 0 && !RdI32(refIdx)) return false;
            if (refIdx >= 1 && static_cast<size_t>(refIdx - 1) < refTable.size()) {
                out.sym = refTable[static_cast<size_t>(refIdx - 1)];
            }
            return true;
        }
        if (stype == 1) { // SYMSXP -> followed by CHARSXP
            RNode ch;
            if (!ReadItem(ch, depth + 1)) return false;
            out.sym = ch.sym;
            refTable.push_back(out.sym);
            return true;
        }
        if (stype == 9) { // CHARSXP
            int32_t slen = 0;
            if (!RdI32(slen)) return false;
            if (slen == -1) {
                out.sym = "NA";
                return true;
            }
            if (slen < 0 || static_cast<size_t>(slen) > size - pos) return false;
            out.sym.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(slen));
            pos += static_cast<size_t>(slen);
            if (hasAttr) { RNode dummy; if (!ReadItem(dummy, depth + 1)) return false; }
            return true;
        }
        if (stype == 2) { // LISTSXP (dotted pairlist used for ATTRIB!)
            while (true) {
                if (hasAttr) { RNode dummy; if (!ReadItem(dummy, depth + 1)) return false; }
                std::string tagName;
                if (hasTag) {
                    RNode tagNode;
                    if (!ReadItem(tagNode, depth + 1)) return false;
                    tagName = tagNode.sym;
                }
                RNode car;
                if (!ReadItem(car, depth + 1)) return false;
                out.attrs.emplace_back(std::move(tagName), std::move(car));

                // Next CDR cell
                int32_t cdrFlags = 0;
                if (!RdI32(cdrFlags)) return false;
                uint8_t cdrType = static_cast<uint8_t>(static_cast<uint32_t>(cdrFlags) & 0xFFu);
                if (cdrType == 254 || cdrType == 0) break;
                if (cdrType != 2) {
                    pos -= 4;
                    RNode tail;
                    if (!ReadItem(tail, depth + 1)) return false;
                    break;
                }
                hasAttr = (cdrFlags & (1 << 9)) != 0;
                hasTag  = (cdrFlags & (1 << 10)) != 0;
            }
            return true;
        }

        // Vector types: LGLSXP(10), INTSXP(13), REALSXP(14), CPLXSXP(15), STRSXP(16), VECSXP(19), RAWSXP(24)
        if (stype == 10 || stype == 13 || stype == 14 || stype == 15 || stype == 16 || stype == 19 || stype == 24) {
            int32_t vlen = 0;
            if (!RdI32(vlen) || vlen < 0) return false;
            size_t len = static_cast<size_t>(vlen);

            if (stype == 10 || stype == 13) { // LGLSXP / INTSXP
                if (len * 4 > size - pos) return false;
                size_t cap = std::min<size_t>(len, 1000);
                out.strs.reserve(cap);
                for (size_t i = 0; i < len; ++i) {
                    int32_t iv = 0; RdI32(iv);
                    if (i < cap) {
                        if (iv == std::numeric_limits<int32_t>::min()) out.strs.push_back(L"NA");
                        else if (stype == 10) out.strs.push_back(iv != 0 ? L"TRUE" : L"FALSE");
                        else out.strs.push_back(std::to_wstring(iv));
                    }
                }
            } else if (stype == 14) { // REALSXP
                if (len * 8 > size - pos) return false;
                size_t cap = std::min<size_t>(len, 1000);
                out.strs.reserve(cap);
                for (size_t i = 0; i < len; ++i) {
                    uint64_t bits = be ? RdU64BE(data + pos) : RdU64LE(data + pos);
                    double dv = 0.0; RdF64(dv);
                    if (i < cap) {
                        if ((bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL && (bits & 0xFFFULL) == 0x7A2ULL) {
                            out.strs.push_back(L"NA");
                        } else {
                            out.strs.push_back(FmtDouble(dv));
                        }
                    }
                }
            } else if (stype == 15) { // CPLXSXP
                if (len * 16 > size - pos) return false;
                for (size_t i = 0; i < len; ++i) {
                    double re = 0.0, im = 0.0; RdF64(re); RdF64(im);
                    if (i < 500) out.strs.push_back(FmtDouble(re) + (im >= 0 ? L"+" : L"") + FmtDouble(im) + L"i");
                }
            } else if (stype == 24) { // RAWSXP
                if (len > size - pos) return false;
                for (size_t i = 0; i < std::min<size_t>(len, 500); ++i) {
                    wchar_t hb[8] = {}; swprintf_s(hb, L"%02X", data[pos + i]);
                    out.strs.push_back(hb);
                }
                pos += len;
            } else if (stype == 16) { // STRSXP
                if (len > 1000000) return false;
                for (size_t i = 0; i < len; ++i) {
                    RNode ch;
                    if (!ReadItem(ch, depth + 1)) return false;
                    if (i < 1000) out.strs.push_back(PathUtils::Utf8ToWide(ch.sym));
                }
            } else if (stype == 19) { // VECSXP (generic list / data.frame)
                if (len > 10000) return false;
                out.listItems.reserve(std::min<size_t>(len, 128));
                for (size_t i = 0; i < len; ++i) {
                    RNode elem;
                    if (!ReadItem(elem, depth + 1)) return false;
                    if (i < 128) out.listItems.push_back(std::move(elem));
                }
            }

            if (hasAttr) {
                RNode attrList;
                if (!ReadItem(attrList, depth + 1)) return false;
                out.attrs = std::move(attrList.attrs);
            }
            return true;
        }
        return false;
    }
};

inline bool ParseRdsData(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<uint8_t> uncomp;
    const uint8_t* ptr = raw.data();
    size_t len = raw.size();
    if (len >= 10 && ptr[0] == 0x1Fu && ptr[1] == 0x8Bu) {
        if (!ZipDeflate::InflateGzip(ptr, len, uncomp, 64 * 1024 * 1024) || uncomp.empty()) return false;
        ptr = uncomp.data();
        len = uncomp.size();
    }
    if (len < 14) return false;
    bool be = true;
    if (ptr[0] == 'X' && ptr[1] == '\n') be = true;
    else if (ptr[0] == 'B' && ptr[1] == '\n') be = false;
    else return false;

    RdsReader rr{ ptr, len, 2, be, {}, 0 };
    int32_t fmtVer = 0, rVer = 0, minVer = 0;
    if (!rr.RdI32(fmtVer) || !rr.RdI32(rVer) || !rr.RdI32(minVer)) return false;
    if (fmtVer != 2 && fmtVer != 3) return false;
    if (fmtVer == 3) {
        int32_t encLen = 0;
        if (!rr.RdI32(encLen) || encLen < 0 || static_cast<size_t>(encLen) > rr.size - rr.pos) return false;
        rr.pos += static_cast<size_t>(encLen);
    }

    RNode root;
    if (!rr.ReadItem(root, 0)) return false;

    SpreadsheetSheet sh;
    sh.name = L"RDS_Data";

    if (root.sexpType == 19 && !root.listItems.empty()) { // data.frame or list
        const RNode* namesAttr = root.FindAttr("names");
        const RNode* levelsCols = namesAttr;
        std::vector<std::wstring> hdr;
        size_t maxRows = 0;
        for (size_t c = 0; c < root.listItems.size(); ++c) {
            std::wstring cname = (levelsCols && c < levelsCols->strs.size()) ? levelsCols->strs[c] : (L"V" + std::to_wstring(c + 1));
            hdr.push_back(cname);
            maxRows = std::max(maxRows, root.listItems[c].strs.size());
        }
        sh.rows.push_back(std::move(hdr));
        size_t rCap = std::min<size_t>(maxRows, 500);
        for (size_t r = 0; r < rCap; ++r) {
            std::vector<std::wstring> row(root.listItems.size());
            for (size_t c = 0; c < root.listItems.size(); ++c) {
                const auto& colNode = root.listItems[c];
                const RNode* facLvls = colNode.FindAttr("levels");
                if (r < colNode.strs.size()) {
                    if (facLvls && !facLvls->strs.empty()) {
                        int lvlIdx = std::atoi(PathUtils::WideToUtf8(colNode.strs[r]).c_str());
                        if (lvlIdx >= 1 && static_cast<size_t>(lvlIdx - 1) < facLvls->strs.size()) {
                            row[c] = facLvls->strs[static_cast<size_t>(lvlIdx - 1)];
                        } else {
                            row[c] = colNode.strs[r];
                        }
                    } else {
                        row[c] = colNode.strs[r];
                    }
                }
            }
            sh.rows.push_back(std::move(row));
        }
    } else {
        const RNode* dimAttr = root.FindAttr("dim");
        if (dimAttr && dimAttr->strs.size() == 2) {
            size_t nr = static_cast<size_t>(std::max(1, _wtoi(dimAttr->strs[0].c_str())));
            size_t nc = static_cast<size_t>(std::max(1, _wtoi(dimAttr->strs[1].c_str())));
            std::vector<std::wstring> hdr = { L"Row \\ Col" };
            for (size_t c = 0; c < std::min<size_t>(nc, 64); ++c) hdr.push_back(L"[," + std::to_wstring(c + 1) + L"]");
            sh.rows.push_back(std::move(hdr));
            for (size_t r = 0; r < std::min<size_t>(nr, 256); ++r) {
                std::vector<std::wstring> rv = { L"[" + std::to_wstring(r + 1) + L",]" };
                for (size_t c = 0; c < std::min<size_t>(nc, 64); ++c) {
                    size_t idx = r + c * nr; // R matrices are column-major
                    rv.push_back(idx < root.strs.size() ? root.strs[idx] : L"");
                }
                sh.rows.push_back(std::move(rv));
            }
        } else {
            const RNode* namesAttr = root.FindAttr("names");
            sh.rows.push_back({ L"Index / Name", L"Value (RDS v" + std::to_wstring(fmtVer) + L")" });
            for (size_t i = 0; i < std::min<size_t>(root.strs.size(), 500); ++i) {
                std::wstring nm = (namesAttr && i < namesAttr->strs.size()) ? namesAttr->strs[i] : (L"[" + std::to_wstring(i + 1) + L"]");
                sh.rows.push_back({ nm, root.strs[i] });
            }
        }
    }
    FinalizeSheetCols(sh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"R Serialized Object (.rds)";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseRSerializedRds(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseRdsData(raw, out); }

} // namespace Formats
