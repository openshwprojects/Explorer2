#pragma once
// PyTorchCheckpoint.h — PyTorch Checkpoint (.pt / .pth / .ckpt) Reader (File-Backed ZIP + Pickle Protocols 0-5)

namespace Formats {
namespace AiDataDetail {

struct PickleVal {
    enum Kind { Other, Int, Float, Str, Tuple, Storage, Tensor } kind = Other;
    int64_t ival = 0;
    double fval = 0.0;
    std::string str;
    std::vector<PickleVal> items;
    std::string storageType;
    std::string storageKey;
    int64_t storageNumel = 0;
    std::vector<int64_t> shape;
    std::vector<int64_t> stride;
};

struct PyTorchTensorDesc {
    std::string name;
    std::string dtype;
    std::string storageKey;
    std::vector<int64_t> shape;
    std::vector<int64_t> stride;
};

inline bool WalkPyTorchPickle(
    const uint8_t* data,
    size_t size,
    std::vector<PyTorchTensorDesc>& outTensors,
    std::vector<std::pair<std::string, std::string>>& outScalarKv)
{
    if (!data || size < 2) return false;
    size_t pos = 0;
    if (data[0] == 0x80u) {
        if (data[1] > 5) return false;
        pos = 2;
    } else if (data[0] != '(' && data[0] != '}' && data[0] != ']' && data[0] != 'c' &&
               data[0] != 'd' && data[0] != 'l' && data[0] != 'I' && data[0] != 'L' &&
               data[0] != 'F' && data[0] != 'S' && data[0] != 'V') {
        return false;
    }

    std::vector<PickleVal> stack;
    std::vector<size_t> marks;
    std::unordered_map<uint32_t, PickleVal> memo;
    uint32_t nextMemoId = 0;
    bool sawStop = false;

    auto readLine = [&](std::string_view& outLine) -> bool {
        size_t nl = pos;
        while (nl < size && data[nl] != '\n') ++nl;
        if (nl >= size) return false;
        size_t end = nl;
        if (end > pos && data[end - 1] == '\r') --end;
        outLine = std::string_view(reinterpret_cast<const char*>(data + pos), end - pos);
        pos = nl + 1;
        return true;
    };

    auto popMark = [&](std::vector<PickleVal>& popped) -> bool {
        if (marks.empty()) return false;
        size_t m = marks.back();
        marks.pop_back();
        if (m > stack.size()) return false;
        popped.assign(stack.begin() + static_cast<ptrdiff_t>(m), stack.end());
        stack.resize(m);
        return true;
    };

    auto recordKv = [&](const PickleVal& k, const PickleVal& v) {
        if (k.kind != PickleVal::Str) return;
        if (v.kind == PickleVal::Tensor) {
            PyTorchTensorDesc td;
            td.name = k.str;
            td.dtype = v.storageType;
            td.storageKey = v.storageKey;
            td.shape = v.shape;
            td.stride = v.stride;
            outTensors.push_back(std::move(td));
        } else if (v.kind == PickleVal::Str || v.kind == PickleVal::Int || v.kind == PickleVal::Float) {
            std::string vs = (v.kind == PickleVal::Str) ? v.str :
                             (v.kind == PickleVal::Int) ? std::to_string(v.ival) :
                             PathUtils::WideToUtf8(FmtDouble(v.fval));
            outScalarKv.emplace_back(k.str, vs);
        }
    };

    while (pos < size && !sawStop) {
        uint8_t op = data[pos++];
        switch (op) {
            case 0x80: if (pos + 1 > size) return false; pos += 1; break; // PROTO
            case 0x95: if (pos + 8 > size) return false; pos += 8; break; // FRAME
            case '.': sawStop = true; break;                              // STOP
            case '(': marks.push_back(stack.size()); break;               // MARK
            case '}': case ']': stack.push_back(PickleVal{}); break;      // EMPTY_DICT / EMPTY_LIST
            case ')': { PickleVal t; t.kind = PickleVal::Tuple; stack.push_back(std::move(t)); break; }
            case 'N': case 0x88: case 0x89: stack.push_back(PickleVal{}); break; // NONE / NEWTRUE / NEWFALSE
            case 'I': case 'L': { // Protocol 0 INT / LONG
                std::string_view ln;
                if (!readLine(ln)) return false;
                if (!ln.empty() && (ln.back() == 'L' || ln.back() == 'l')) ln.remove_suffix(1);
                PickleVal v;
                if (ln == "00" || ln == "01") {
                    v.kind = PickleVal::Int;
                    v.ival = (ln == "01") ? 1 : 0;
                } else {
                    v.kind = PickleVal::Int;
                    std::from_chars(ln.data(), ln.data() + ln.size(), v.ival);
                }
                stack.push_back(v);
                break;
            }
            case 'F': { // Protocol 0 FLOAT
                std::string_view ln;
                if (!readLine(ln)) return false;
                std::string tmp(ln);
                PickleVal v; v.kind = PickleVal::Float; v.fval = std::strtod(tmp.c_str(), nullptr);
                stack.push_back(v);
                break;
            }
            case 'S': { // Protocol 0 STRING (quoted)
                std::string_view ln;
                if (!readLine(ln)) return false;
                if (ln.size() >= 2 && ((ln.front() == '\'' && ln.back() == '\'') || (ln.front() == '"' && ln.back() == '"'))) {
                    ln = ln.substr(1, ln.size() - 2);
                }
                PickleVal v; v.kind = PickleVal::Str; v.str.assign(ln.data(), std::min<size_t>(ln.size(), 4096));
                stack.push_back(std::move(v));
                break;
            }
            case 'V': { // Protocol 0 UNICODE
                std::string_view ln;
                if (!readLine(ln)) return false;
                PickleVal v; v.kind = PickleVal::Str; v.str.assign(ln.data(), std::min<size_t>(ln.size(), 4096));
                stack.push_back(std::move(v));
                break;
            }
            case 'K': { if (pos + 1 > size) return false; PickleVal v; v.kind = PickleVal::Int; v.ival = data[pos++]; stack.push_back(v); break; }
            case 'M': { if (pos + 2 > size) return false; PickleVal v; v.kind = PickleVal::Int; v.ival = RdU16LE(data + pos); pos += 2; stack.push_back(v); break; }
            case 'J': { if (pos + 4 > size) return false; PickleVal v; v.kind = PickleVal::Int; v.ival = RdI32LE(data + pos); pos += 4; stack.push_back(v); break; }
            case 0x8A: { // LONG1
                if (pos >= size) return false;
                uint8_t n = data[pos++];
                if (n > size - pos) return false;
                uint64_t uv = 0;
                for (uint8_t i = 0; i < std::min<uint8_t>(n, 8); ++i) uv |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
                if (n > 0 && n < 8 && (data[pos + n - 1] & 0x80u) != 0) {
                    uv |= (~0ULL) << (n * 8);
                }
                pos += n;
                PickleVal v; v.kind = PickleVal::Int; v.ival = static_cast<int64_t>(uv); stack.push_back(v);
                break;
            }
            case 0x8B: { // LONG4
                if (pos + 4 > size) return false;
                uint32_t n = RdU32LE(data + pos); pos += 4;
                if (n > size - pos) return false;
                uint64_t uv = 0;
                for (uint32_t i = 0; i < std::min<uint32_t>(n, 8u); ++i) uv |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
                if (n > 0 && n < 8 && (data[pos + n - 1] & 0x80u) != 0) {
                    uv |= (~0ULL) << (n * 8);
                }
                pos += n;
                PickleVal v; v.kind = PickleVal::Int; v.ival = static_cast<int64_t>(uv); stack.push_back(v);
                break;
            }
            case 'G': {
                if (pos + 8 > size) return false;
                PickleVal v; v.kind = PickleVal::Float; v.fval = RdF64BE(data + pos); pos += 8; stack.push_back(v);
                break;
            }
            case 0x8C: case 'C': case 'U': { // SHORT_BINUNICODE / SHORT_BINBYTES / SHORT_BINSTRING
                if (pos >= size) return false;
                uint8_t n = data[pos++];
                if (n > size - pos) return false;
                PickleVal v; v.kind = PickleVal::Str; v.str.assign(reinterpret_cast<const char*>(data + pos), n); pos += n;
                stack.push_back(std::move(v));
                break;
            }
            case 'X': case 'B': case 'T': { // BINUNICODE / BINBYTES / BINSTRING
                if (pos + 4 > size) return false;
                uint32_t n = RdU32LE(data + pos); pos += 4;
                if (n > size - pos) return false;
                PickleVal v; v.kind = PickleVal::Str; v.str.assign(reinterpret_cast<const char*>(data + pos), std::min<uint32_t>(n, 4096)); pos += n;
                stack.push_back(std::move(v));
                break;
            }
            case 0x8D: case 0x8E: case 0x96: { // BINUNICODE8 / BINBYTES8 / BYTEARRAY8
                if (pos + 8 > size) return false;
                uint64_t n = RdU64LE(data + pos); pos += 8;
                if (n > size - pos) return false;
                PickleVal v; v.kind = PickleVal::Str;
                v.str.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(std::min<uint64_t>(n, 4096)));
                pos += static_cast<size_t>(n);
                stack.push_back(std::move(v));
                break;
            }
            case 'c': { // GLOBAL module\nname\n
                std::string_view mod, nm;
                if (!readLine(mod) || !readLine(nm)) return false;
                PickleVal v; v.kind = PickleVal::Str;
                v.str = std::string(mod) + "." + std::string(nm);
                stack.push_back(std::move(v));
                break;
            }
            case 0x93: { // STACK_GLOBAL
                if (stack.size() < 2) return false;
                PickleVal nm = std::move(stack.back()); stack.pop_back();
                PickleVal mod = std::move(stack.back()); stack.pop_back();
                PickleVal v; v.kind = PickleVal::Str; v.str = mod.str + "." + nm.str;
                stack.push_back(std::move(v));
                break;
            }
            case 't': {
                std::vector<PickleVal> items;
                if (!popMark(items)) return false;
                PickleVal t; t.kind = PickleVal::Tuple; t.items = std::move(items);
                stack.push_back(std::move(t));
                break;
            }
            case 'l': { // LIST from mark
                std::vector<PickleVal> items;
                if (!popMark(items)) return false;
                PickleVal l; l.kind = PickleVal::Tuple; l.items = std::move(items);
                stack.push_back(std::move(l));
                break;
            }
            case 'd': { // DICT from mark
                std::vector<PickleVal> kvs;
                if (!popMark(kvs)) return false;
                for (size_t i = 0; i + 1 < kvs.size(); i += 2) recordKv(kvs[i], kvs[i + 1]);
                stack.push_back(PickleVal{});
                break;
            }
            case 0x85: case 0x86: case 0x87: { // TUPLE1 / TUPLE2 / TUPLE3
                size_t cnt = (op == 0x85) ? 1 : (op == 0x86 ? 2 : 3);
                if (stack.size() < cnt) return false;
                PickleVal t; t.kind = PickleVal::Tuple;
                t.items.assign(stack.end() - static_cast<ptrdiff_t>(cnt), stack.end());
                stack.resize(stack.size() - cnt);
                stack.push_back(std::move(t));
                break;
            }
            case 'P': { // PERSID (Protocol 0)
                std::string_view ln;
                if (!readLine(ln)) return false;
                PickleVal st; st.kind = PickleVal::Storage; st.storageKey = std::string(ln);
                stack.push_back(std::move(st));
                break;
            }
            case 'Q': { // BINPERSID
                if (stack.empty()) return false;
                PickleVal pid = std::move(stack.back()); stack.pop_back();
                PickleVal st;
                st.kind = PickleVal::Storage;
                if (pid.kind == PickleVal::Tuple && pid.items.size() >= 5) {
                    st.storageType = pid.items[1].str;
                    st.storageKey = (pid.items[2].kind == PickleVal::Int) ? std::to_string(pid.items[2].ival) : pid.items[2].str;
                    st.storageNumel = pid.items[4].ival;
                }
                stack.push_back(std::move(st));
                break;
            }
            case 0x92: { // NEWOBJ_EX: cls, args, kwargs -> obj
                if (stack.size() < 3) return false;
                stack.pop_back(); // kwargs
                PickleVal args = std::move(stack.back()); stack.pop_back();
                PickleVal fn = std::move(stack.back()); stack.pop_back();
                PickleVal res;
                if (args.kind == PickleVal::Tuple && !args.items.empty() && args.items[0].kind == PickleVal::Storage) {
                    res = std::move(args.items[0]);
                }
                (void)fn;
                stack.push_back(std::move(res));
                break;
            }
            case 'R': case 0x81: { // REDUCE / NEWOBJ
                if (stack.size() < 2) return false;
                PickleVal args = std::move(stack.back()); stack.pop_back();
                PickleVal fn = std::move(stack.back()); stack.pop_back();
                PickleVal res;
                if (fn.str.find("rebuild_tensor") != std::string::npos && args.kind == PickleVal::Tuple && args.items.size() >= 3) {
                    res.kind = PickleVal::Tensor;
                    if (args.items[0].kind == PickleVal::Storage) {
                        res.storageType = args.items[0].storageType;
                        res.storageKey = args.items[0].storageKey;
                    }
                    if (args.items[2].kind == PickleVal::Tuple) {
                        for (const auto& d : args.items[2].items) res.shape.push_back(d.ival);
                    }
                    if (args.items.size() >= 4 && args.items[3].kind == PickleVal::Tuple) {
                        for (const auto& d : args.items[3].items) res.stride.push_back(d.ival);
                    }
                    if (outTensors.size() < 512 && stack.empty()) {
                        PyTorchTensorDesc td{ "(root_tensor)", res.storageType, res.storageKey, res.shape, res.stride };
                        outTensors.push_back(std::move(td));
                    }
                } else if (args.kind == PickleVal::Tuple && args.items.size() == 1 && args.items[0].kind != PickleVal::Other) {
                    res = std::move(args.items[0]);
                }
                stack.push_back(std::move(res));
                break;
            }
            case 'b': { if (stack.size() < 2) return false; stack.pop_back(); break; } // BUILD
            case 's': { // SETITEM
                if (stack.size() < 3) return false;
                PickleVal v = std::move(stack.back()); stack.pop_back();
                PickleVal k = std::move(stack.back()); stack.pop_back();
                recordKv(k, v);
                break;
            }
            case 'u': { // SETITEMS
                std::vector<PickleVal> kvs;
                if (!popMark(kvs) || stack.empty()) return false;
                for (size_t i = 0; i + 1 < kvs.size(); i += 2) recordKv(kvs[i], kvs[i + 1]);
                break;
            }
            case 'a': { if (stack.size() < 2) return false; stack.pop_back(); break; } // APPEND
            case 'e': { std::vector<PickleVal> items; if (!popMark(items) || stack.empty()) return false; break; } // APPENDS
            case 'p': { // PUT (Protocol 0)
                std::string_view ln;
                if (!readLine(ln) || stack.empty()) return false;
                uint32_t mid = 0; std::from_chars(ln.data(), ln.data() + ln.size(), mid);
                memo[mid] = stack.back();
                break;
            }
            case 'g': { // GET (Protocol 0)
                std::string_view ln;
                if (!readLine(ln)) return false;
                uint32_t mid = 0; std::from_chars(ln.data(), ln.data() + ln.size(), mid);
                stack.push_back(memo[mid]);
                break;
            }
            case 'q': { if (pos >= size || stack.empty()) return false; memo[data[pos++]] = stack.back(); break; } // BINPUT
            case 'r': { if (pos + 4 > size || stack.empty()) return false; memo[RdU32LE(data + pos)] = stack.back(); pos += 4; break; } // LONG_BINPUT
            case 0x94: { if (stack.empty()) return false; memo[nextMemoId++] = stack.back(); break; } // MEMOIZE
            case 'h': { if (pos >= size) return false; stack.push_back(memo[data[pos++]]); break; } // BINGET
            case 'j': { if (pos + 4 > size) return false; stack.push_back(memo[RdU32LE(data + pos)]); pos += 4; break; } // LONG_BINGET
            case '0': { if (stack.empty()) return false; stack.pop_back(); break; } // POP
            case '1': { std::vector<PickleVal> discarded; if (!popMark(discarded)) return false; break; } // POP_MARK
            case '2': { if (stack.empty()) return false; stack.push_back(stack.back()); break; } // DUP
            case 0x8F: stack.push_back(PickleVal{}); break; // EMPTY_SET
            case 0x90: { std::vector<PickleVal> items; if (!popMark(items) || stack.empty()) return false; break; } // ADDITEMS
            case 0x91: { std::vector<PickleVal> items; if (!popMark(items)) return false; stack.push_back(PickleVal{}); break; } // FROZENSET
            case 0x97: case 0x98: break; // NEXT_BUFFER / READONLY_BUFFER (Protocol 5)
            default:
                return !outTensors.empty() || !outScalarKv.empty();
        }
    }
    if (sawStop && outTensors.empty() && outScalarKv.empty() && !stack.empty()) {
        const auto& top = stack.back();
        if (top.kind == PickleVal::Str || top.kind == PickleVal::Int || top.kind == PickleVal::Float) {
            PickleVal k; k.kind = PickleVal::Str; k.str = "(root)";
            recordKv(k, top);
        }
    }
    return sawStop;
}

inline bool ParsePyTorchCheckpoint(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::vector<PyTorchTensorDesc> tensors;
    std::vector<std::pair<std::string, std::string>> scalars;
    std::unordered_map<std::string, std::vector<uint8_t>> storageBlobs;
    std::wstring archiveVer;

    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    bool usedFile = false;
    if ((raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K') &&
        DetailStructuredDataDoc::ListZipPackage(raw, out, entries, usedFile) && !entries.empty())
    {
        SourceBytes zipSrc(raw, out);
        std::vector<uint8_t> pklBytes;
        for (const auto& e : entries) {
            if (e.isDirectory) continue;
            std::wstring pLow = PathUtils::ToLower(e.pathInArchive);
            if (pLow.size() >= 8 && pLow.substr(pLow.size() - 8) == L"data.pkl") {
                DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, e, pklBytes);
            } else if (pklBytes.empty() && PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive)) == L".pkl") {
                DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, e, pklBytes);
            } else if (pLow.size() >= 7 && pLow.substr(pLow.size() - 7) == L"version") {
                std::vector<uint8_t> vb;
                if (DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, e, vb)) {
                    archiveVer = PathUtils::Trim(CleanAsciiFixed(vb.data(), vb.size()));
                }
            } else if (pLow.find(L"/data/") != std::wstring::npos || pLow.find(L"\\data\\") != std::wstring::npos) {
                if (storageBlobs.size() < 64) {
                    if (e.uncompressedSize <= 4 * 1024 * 1024) {
                        std::vector<uint8_t> sb;
                        if (DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, e, sb)) {
                            storageBlobs[PathUtils::WideToUtf8(e.name)] = std::move(sb);
                        }
                    } else if (e.compressionMethod == 0 && e.uncompressedSize >= 4) {
                        // Stream first 64 bytes of large uncompressed weight tensor without loading whole tensor into RAM
                        uint8_t lfh[30] = {};
                        if (DetailBinarySysDoc::ReadSourceExact(zipSrc, e.localHeaderOffset, lfh, 30) && RdU32LE(lfh) == 0x04034B50u) {
                            uint64_t dataOff = e.localHeaderOffset + 30ULL + RdU16LE(lfh + 26) + RdU16LE(lfh + 28);
                            const size_t peekLen = static_cast<size_t>(std::min<uint64_t>(e.uncompressedSize, 64ULL));
                            std::vector<uint8_t> sb;
                            if (DetailBinarySysDoc::ReadSourceVec(zipSrc, dataOff, peekLen, sb)) {
                                storageBlobs[PathUtils::WideToUtf8(e.name)] = std::move(sb);
                            }
                        }
                    }
                }
            }
        }
        if (pklBytes.empty() || !WalkPyTorchPickle(pklBytes.data(), pklBytes.size(), tensors, scalars)) {
            return false;
        }
    } else if (raw.size() >= 2 && WalkPyTorchPickle(raw.data(), raw.size(), tensors, scalars)) {
        // Direct raw pickle checkpoint
    } else {
        return false;
    }

    SpreadsheetSheet sh;
    sh.name = L"PyTorch_StateDict";
    sh.rows.push_back({ L"Parameter / Key", L"Storage Type", L"Shape", L"Stride", L"Weight Preview / Value" });

    if (!archiveVer.empty()) {
        sh.rows.push_back({ L"(archive_version)", L"PyTorch ZIP", archiveVer, L"", L"" });
    }
    for (const auto& t : tensors) {
        std::wstring shStr = L"[";
        for (size_t i = 0; i < t.shape.size(); ++i) { if (i > 0) shStr += L", "; shStr += std::to_wstring(t.shape[i]); }
        shStr += L"]";
        std::wstring stStr = L"[";
        for (size_t i = 0; i < t.stride.size(); ++i) { if (i > 0) stStr += L", "; stStr += std::to_wstring(t.stride[i]); }
        stStr += L"]";

        std::wstring sample;
        auto it = storageBlobs.find(t.storageKey);
        if (it != storageBlobs.end() && !it->second.empty()) {
            const auto& sb = it->second;
            if (t.dtype.find("Half") != std::string::npos) {
                for (size_t k = 0; k + 2 <= sb.size() && k < 8; k += 2) {
                    if (!sample.empty()) sample += L", ";
                    sample += FmtDouble(HalfToFloat(RdU16LE(sb.data() + k)));
                }
            } else if (t.dtype.find("BFloat16") != std::string::npos) {
                for (size_t k = 0; k + 2 <= sb.size() && k < 8; k += 2) {
                    if (!sample.empty()) sample += L", ";
                    sample += FmtDouble(BFloat16ToFloat(RdU16LE(sb.data() + k)));
                }
            } else if (t.dtype.find("Double") != std::string::npos) {
                for (size_t k = 0; k + 8 <= sb.size() && k < 32; k += 8) {
                    if (!sample.empty()) sample += L", ";
                    sample += FmtDouble(RdF64LE(sb.data() + k));
                }
            } else if (t.dtype.find("Long") != std::string::npos) {
                for (size_t k = 0; k + 8 <= sb.size() && k < 32; k += 8) {
                    if (!sample.empty()) sample += L", ";
                    sample += std::to_wstring(RdI64LE(sb.data() + k));
                }
            } else {
                for (size_t k = 0; k + 4 <= sb.size() && k < 16; k += 4) {
                    if (!sample.empty()) sample += L", ";
                    sample += FmtDouble(RdF32LE(sb.data() + k));
                }
            }
        }
        sh.rows.push_back({ PathUtils::Utf8ToWide(t.name), PathUtils::Utf8ToWide(t.dtype), shStr, stStr, sample });
    }
    for (const auto& kv : scalars) {
        if (sh.rows.size() >= 500) break;
        sh.rows.push_back({ PathUtils::Utf8ToWide(kv.first), L"metadata", L"scalar", L"", PathUtils::Utf8ToWide(kv.second) });
    }
    if (sh.rows.size() <= 1) return false;
    FinalizeSheetCols(sh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"PyTorch Checkpoint (" + extLower + L")";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParsePyTorchCheckpoint(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParsePyTorchCheckpoint(raw, extLower, out); }

} // namespace Formats
