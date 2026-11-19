#pragma once
#include "../../ByteIO.h"
#include "../../Lz4Block.h"
// Parquet.h — Apache Parquet Columnar Storage (.parquet, PAR1) Thrift Compact Footer,
//             ZSTD / LZ4 / LZ4_RAW / Snappy / Gzip Codecs, DATA_PAGE_V2, Delta & Dictionary Decoders,
//             and >32 MB Random-Access File Streaming (#31).

namespace Formats {

namespace ParquetDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64LE;

struct WinParquetRandomFile {
    HANDLE hFile = INVALID_HANDLE_VALUE;
    uint64_t fileSize = 0;

    ~WinParquetRandomFile() {
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    }

    bool Open(const std::wstring& path) {
        hFile = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li{};
        if (!GetFileSizeEx(hFile, &li) || li.QuadPart <= 0) return false;
        fileSize = static_cast<uint64_t>(li.QuadPart);
        return true;
    }

    bool ReadAt(uint64_t offset, uint8_t* dst, size_t len) const {
        if (offset > fileSize || len > fileSize - offset) return false;
        if (len == 0) return true;
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(hFile, pos, nullptr, FILE_BEGIN)) return false;
        size_t done = 0;
        while (done < len) {
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(len - done, 16 * 1024 * 1024));
            DWORD got = 0;
            if (!ReadFile(hFile, dst + done, chunk, &got, nullptr) || got == 0) return false;
            done += got;
        }
        return true;
    }
};

// Snappy pages (also used by the Avro and AI-data readers)
using ::Explorer2::StreamCodecs::SnappyDecompress;

// Parquet codec 5 (LZ4_HADOOP / LZ4_FRAME / fallback raw LZ4)
inline bool ParquetLz4HadoopOrFrameDecompress(const uint8_t* src, size_t srcLen, size_t expectedUncomp, std::vector<uint8_t>& out) {
    out.clear();
    if (!src || srcLen == 0) return false;
    const size_t maxOut = std::max<size_t>(expectedUncomp + 65536, 65536);

    // 1. Standard LZ4 frame magic (0x184D2204)
    if (srcLen >= 7 && RdU32LE(src) == 0x184D2204u) {
        if (StreamCodecs::DecompressLz4Frame(src, srcLen, out, maxOut)) return true;
    }

    // 2. Hadoop LZ4 block framing: [uint32_be expectedBlockUncomp][uint32_be subChunkCompLen][lz4_raw]...
    {
        size_t pos = 0;
        std::vector<uint8_t> tmp;
        tmp.reserve(expectedUncomp);
        bool okHadoop = true;
        while (pos < srcLen) {
            if (pos + 4 > srcLen) { okHadoop = false; break; }
            uint32_t blockUncomp = RdU32BE(src + pos);
            pos += 4;
            if (blockUncomp == 0 || blockUncomp > maxOut || tmp.size() + blockUncomp > maxOut) {
                okHadoop = false;
                break;
            }
            size_t decodedInBlock = 0;
            while (decodedInBlock < blockUncomp) {
                if (pos + 4 > srcLen) { okHadoop = false; break; }
                uint32_t subCompLen = RdU32BE(src + pos);
                pos += 4;
                if (subCompLen == 0 || subCompLen > srcLen - pos) { okHadoop = false; break; }
                size_t before = tmp.size();
                if (!::Explorer2::Lz4::DecodeBlockAppend(src + pos, subCompLen, tmp, tmp.size(), maxOut)) {
                    okHadoop = false;
                    break;
                }
                pos += subCompLen;
                decodedInBlock += (tmp.size() - before);
            }
            if (!okHadoop || decodedInBlock != blockUncomp) { okHadoop = false; break; }
        }
        if (okHadoop && (expectedUncomp == 0 || tmp.size() == expectedUncomp)) {
            out = std::move(tmp);
            return true;
        }
    }

    // 3. Raw LZ4 block fallback (legacy writers tagging LZ4_RAW as codec 5)
    out.clear();
    if (::Explorer2::Lz4::DecodeBlockAppend(src, srcLen, out, 0, maxOut) && (expectedUncomp == 0 || out.size() == expectedUncomp)) {
        return true;
    }
    out.clear();
    return false;
}

class ThriftCompactReader {
public:
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    ThriftCompactReader(const uint8_t* d, size_t s) : data(d), size(s), pos(0) {}

    bool ReadByte(uint8_t& out) {
        if (pos >= size) return false;
        out = data[pos++];
        return true;
    }

    bool ReadVarint64(uint64_t& out) {
        out = 0;
        int shift = 0;
        while (pos < size && shift < 70) {
            uint8_t b = data[pos++];
            out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) return true;
            shift += 7;
        }
        return false;
    }

    bool ReadVarint32(uint32_t& out) {
        uint64_t v = 0;
        if (!ReadVarint64(v)) return false;
        out = static_cast<uint32_t>(v);
        return true;
    }

    bool ReadZigZag16(int16_t& out) {
        uint32_t u = 0;
        if (!ReadVarint32(u)) return false;
        out = static_cast<int16_t>((u >> 1) ^ -static_cast<int32_t>(u & 1u));
        return true;
    }

    bool ReadZigZag32(int32_t& out) {
        uint32_t u = 0;
        if (!ReadVarint32(u)) return false;
        out = static_cast<int32_t>((u >> 1) ^ -static_cast<int32_t>(u & 1u));
        return true;
    }

    bool ReadZigZag64(int64_t& out) {
        uint64_t u = 0;
        if (!ReadVarint64(u)) return false;
        out = static_cast<int64_t>((u >> 1) ^ (~(u & 1ULL) + 1ULL));
        return true;
    }

    bool ReadBinary(std::string& out) {
        uint32_t len = 0;
        if (!ReadVarint32(len) || len > size - pos) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return true;
    }

    bool ReadFieldHeader(int16_t& ioLastFieldId, int16_t& outFieldId, uint8_t& outType) {
        uint8_t hdr = 0;
        if (!ReadByte(hdr)) return false;
        if (hdr == 0x00) {
            outType = 0;
            outFieldId = 0;
            return true;
        }
        outType = hdr & 0x0Fu;
        uint8_t delta = (hdr >> 4) & 0x0Fu;
        if (delta == 0) {
            if (!ReadZigZag16(outFieldId)) return false;
        } else {
            outFieldId = static_cast<int16_t>(ioLastFieldId + delta);
        }
        ioLastFieldId = outFieldId;
        return true;
    }

    bool ReadListHeader(uint8_t& outElemType, uint32_t& outCount) {
        uint8_t b = 0;
        if (!ReadByte(b)) return false;
        outElemType = b & 0x0Fu;
        uint32_t cnt = (b >> 4) & 0x0Fu;
        if (cnt == 15) {
            if (!ReadVarint32(cnt)) return false;
        }
        if (cnt > 1000000) return false;
        outCount = cnt;
        return true;
    }

    bool SkipValue(uint8_t type, int depth = 0) {
        if (depth > 24) return false;
        switch (type) {
            case 1:
            case 2:
                return true;
            case 3: {
                uint8_t b = 0;
                return ReadByte(b);
            }
            case 4:
            case 5:
            case 6: {
                uint64_t v = 0;
                return ReadVarint64(v);
            }
            case 7: {
                if (pos + 8 > size) return false;
                pos += 8;
                return true;
            }
            case 8: {
                uint32_t len = 0;
                if (!ReadVarint32(len) || len > size - pos) return false;
                pos += len;
                return true;
            }
            case 9:
            case 10: {
                uint8_t elemType = 0;
                uint32_t cnt = 0;
                if (!ReadListHeader(elemType, cnt)) return false;
                for (uint32_t i = 0; i < cnt; ++i) {
                    if (elemType == 1 || elemType == 2) {
                        uint8_t b = 0;
                        if (!ReadByte(b)) return false;
                    } else if (!SkipValue(elemType, depth + 1)) {
                        return false;
                    }
                }
                return true;
            }
            case 11: {
                uint32_t cnt = 0;
                if (!ReadVarint32(cnt) || cnt > 1000000) return false;
                if (cnt == 0) return true;
                uint8_t kv = 0;
                if (!ReadByte(kv)) return false;
                uint8_t kt = (kv >> 4) & 0x0Fu;
                uint8_t vt = kv & 0x0Fu;
                for (uint32_t i = 0; i < cnt; ++i) {
                    if ((kt == 1 || kt == 2) ? (pos >= size ? false : (++pos, true)) : !SkipValue(kt, depth + 1)) return false;
                    if ((vt == 1 || vt == 2) ? (pos >= size ? false : (++pos, true)) : !SkipValue(vt, depth + 1)) return false;
                }
                return true;
            }
            case 12: {
                int16_t lastId = 0;
                for (;;) {
                    int16_t fid = 0;
                    uint8_t ftype = 0;
                    if (!ReadFieldHeader(lastId, fid, ftype)) return false;
                    if (ftype == 0) return true;
                    if (!SkipValue(ftype, depth + 1)) return false;
                }
            }
            default:
                return false;
        }
    }
};

struct SchemaElem {
    int32_t type = -1;
    int32_t typeLength = 0;
    int32_t repetitionType = -1;
    std::string name;
    int32_t numChildren = 0;
    int32_t convertedType = -1;
    bool hasType = false;
};

struct ColStats {
    std::string minRaw;
    std::string maxRaw;
    int64_t nullCount = -1;
    bool hasMin = false;
    bool hasMax = false;
};

struct ColChunkMeta {
    int32_t type = 0;
    std::vector<int32_t> encodings;
    std::vector<std::string> pathInSchema;
    int32_t codec = 0;
    int64_t numValues = 0;
    int64_t totalUncompressedSize = 0;
    int64_t totalCompressedSize = 0;
    int64_t dataPageOffset = 0;
    int64_t dictionaryPageOffset = -1;
    ColStats stats;
    bool valid = false;
};

struct RowGroupMeta {
    std::vector<ColChunkMeta> columns;
    int64_t totalByteSize = 0;
    int64_t numRows = 0;
};

struct ParquetFooter {
    int32_t version = 1;
    std::vector<SchemaElem> schema;
    int64_t numRows = 0;
    std::vector<RowGroupMeta> rowGroups;
    std::vector<std::pair<std::string, std::string>> kvMeta;
    std::string createdBy;
};

inline const wchar_t* PhysicalTypeName(int32_t t, int32_t len = 0) {
    switch (t) {
        case 0: return L"BOOLEAN";
        case 1: return L"INT32";
        case 2: return L"INT64";
        case 3: return L"INT96";
        case 4: return L"FLOAT";
        case 5: return L"DOUBLE";
        case 6: return L"BYTE_ARRAY";
        case 7: return len > 0 ? L"FIXED_LEN_BYTE_ARRAY" : L"FIXED_BINARY";
        default: return L"GROUP";
    }
}

inline const wchar_t* RepetitionName(int32_t r) {
    switch (r) {
        case 0: return L"REQUIRED";
        case 1: return L"OPTIONAL";
        case 2: return L"REPEATED";
        default: return L"-";
    }
}

inline const wchar_t* CodecName(int32_t c) {
    switch (c) {
        case 0: return L"UNCOMPRESSED";
        case 1: return L"SNAPPY";
        case 2: return L"GZIP";
        case 3: return L"LZO";
        case 4: return L"BROTLI";
        case 5: return L"LZ4";
        case 6: return L"ZSTD";
        case 7: return L"LZ4_RAW";
        default: return L"UNKNOWN";
    }
}

inline const wchar_t* ConvertedTypeName(int32_t ct) {
    switch (ct) {
        case 0: return L"UTF8";
        case 1: return L"MAP";
        case 2: return L"MAP_KEY_VALUE";
        case 3: return L"LIST";
        case 4: return L"ENUM";
        case 5: return L"DECIMAL";
        case 6: return L"DATE";
        case 7: return L"TIME_MILLIS";
        case 8: return L"TIME_MICROS";
        case 9: return L"TIMESTAMP_MILLIS";
        case 10: return L"TIMESTAMP_MICROS";
        case 11: return L"UINT_8";
        case 12: return L"UINT_16";
        case 13: return L"UINT_32";
        case 14: return L"UINT_64";
        case 15: return L"INT_8";
        case 16: return L"INT_16";
        case 17: return L"INT_32";
        case 18: return L"INT_64";
        case 19: return L"JSON";
        case 20: return L"BSON";
        case 21: return L"INTERVAL";
        default: return L"-";
    }
}

inline std::wstring FormatRawStatValue(const std::string& bytes, int32_t physType, int32_t convType) {
    if (bytes.empty()) return L"";
    const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
    if (physType == 0 && bytes.size() >= 1) return p[0] ? L"true" : L"false";
    if (physType == 1 && bytes.size() >= 4) return std::to_wstring(static_cast<int32_t>(RdU32LE(p)));
    if (physType == 2 && bytes.size() >= 8) return std::to_wstring(static_cast<int64_t>(RdU64LE(p)));
    if (physType == 4 && bytes.size() >= 4) {
        float f = 0.0f;
        std::memcpy(&f, p, 4);
        wchar_t b[64] = {};
        swprintf_s(b, L"%.6g", f);
        return b;
    }
    if (physType == 5 && bytes.size() >= 8) {
        double d = 0.0;
        std::memcpy(&d, p, 8);
        wchar_t b[64] = {};
        swprintf_s(b, L"%.10g", d);
        return b;
    }
    bool printable = (convType == 0);
    if (!printable) {
        printable = true;
        for (unsigned char c : bytes) {
            if (c < 32 && c != '\t') { printable = false; break; }
        }
    }
    if (printable) {
        return PathUtils::Utf8ToWide(std::string_view(bytes.data(), std::min<size_t>(bytes.size(), 96)));
    }
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring h = L"0x";
    for (size_t i = 0; i < std::min<size_t>(bytes.size(), 8); ++i) {
        h.push_back(kHex[p[i] >> 4]);
        h.push_back(kHex[p[i] & 0x0F]);
    }
    return h;
}

inline bool ParseStatisticsStruct(ThriftCompactReader& tr, ColStats& st) {
    int16_t lastId = 0;
    for (;;) {
        int16_t fid = 0;
        uint8_t ftype = 0;
        if (!tr.ReadFieldHeader(lastId, fid, ftype)) return false;
        if (ftype == 0) return true;
        if ((fid == 1 || fid == 5) && ftype == 8) {
            if (!tr.ReadBinary(st.maxRaw)) return false;
            st.hasMax = true;
        } else if ((fid == 2 || fid == 6) && ftype == 8) {
            if (!tr.ReadBinary(st.minRaw)) return false;
            st.hasMin = true;
        } else if (fid == 3 && ftype == 6) {
            if (!tr.ReadZigZag64(st.nullCount)) return false;
        } else {
            if (!tr.SkipValue(ftype)) return false;
        }
    }
}

inline bool ParseColMetaStruct(ThriftCompactReader& tr, ColChunkMeta& cm) {
    int16_t lastId = 0;
    for (;;) {
        int16_t fid = 0;
        uint8_t ftype = 0;
        if (!tr.ReadFieldHeader(lastId, fid, ftype)) return false;
        if (ftype == 0) { cm.valid = true; return true; }
        if (fid == 1 && ftype == 5) {
            if (!tr.ReadZigZag32(cm.type)) return false;
        } else if (fid == 2 && ftype == 9) {
            uint8_t et = 0; uint32_t cnt = 0;
            if (!tr.ReadListHeader(et, cnt)) return false;
            for (uint32_t i = 0; i < cnt; ++i) {
                if (et == 5) { int32_t e = 0; if (!tr.ReadZigZag32(e)) return false; cm.encodings.push_back(e); }
                else if (!tr.SkipValue(et)) return false;
            }
        } else if (fid == 3 && ftype == 9) {
            uint8_t et = 0; uint32_t cnt = 0;
            if (!tr.ReadListHeader(et, cnt)) return false;
            for (uint32_t i = 0; i < cnt; ++i) {
                if (et == 8) { std::string s; if (!tr.ReadBinary(s)) return false; cm.pathInSchema.push_back(std::move(s)); }
                else if (!tr.SkipValue(et)) return false;
            }
        } else if (fid == 4 && ftype == 5) {
            if (!tr.ReadZigZag32(cm.codec)) return false;
        } else if (fid == 5 && ftype == 6) {
            if (!tr.ReadZigZag64(cm.numValues)) return false;
        } else if (fid == 6 && ftype == 6) {
            if (!tr.ReadZigZag64(cm.totalUncompressedSize)) return false;
        } else if (fid == 7 && ftype == 6) {
            if (!tr.ReadZigZag64(cm.totalCompressedSize)) return false;
        } else if (fid == 9 && ftype == 6) {
            if (!tr.ReadZigZag64(cm.dataPageOffset)) return false;
        } else if (fid == 11 && ftype == 6) {
            if (!tr.ReadZigZag64(cm.dictionaryPageOffset)) return false;
        } else if (fid == 12 && ftype == 12) {
            if (!ParseStatisticsStruct(tr, cm.stats)) return false;
        } else {
            if (!tr.SkipValue(ftype)) return false;
        }
    }
}

inline bool ParseFileMetaData(const uint8_t* buf, size_t len, ParquetFooter& out) {
    ThriftCompactReader tr(buf, len);
    int16_t lastId = 0;
    for (;;) {
        int16_t fid = 0;
        uint8_t ftype = 0;
        if (!tr.ReadFieldHeader(lastId, fid, ftype)) return false;
        if (ftype == 0) break;

        if (fid == 1 && ftype == 5) {
            if (!tr.ReadZigZag32(out.version)) return false;
        } else if (fid == 2 && ftype == 9) {
            uint8_t et = 0; uint32_t cnt = 0;
            if (!tr.ReadListHeader(et, cnt) || et != 12) return false;
            out.schema.reserve(std::min<uint32_t>(cnt, 4096));
            for (uint32_t i = 0; i < cnt; ++i) {
                SchemaElem se;
                int16_t sLast = 0;
                for (;;) {
                    int16_t sfid = 0; uint8_t sft = 0;
                    if (!tr.ReadFieldHeader(sLast, sfid, sft)) return false;
                    if (sft == 0) break;
                    if (sfid == 1 && sft == 5) { if (!tr.ReadZigZag32(se.type)) return false; se.hasType = true; }
                    else if (sfid == 2 && sft == 5) { if (!tr.ReadZigZag32(se.typeLength)) return false; }
                    else if (sfid == 3 && sft == 5) { if (!tr.ReadZigZag32(se.repetitionType)) return false; }
                    else if (sfid == 4 && sft == 8) { if (!tr.ReadBinary(se.name)) return false; }
                    else if (sfid == 5 && sft == 5) { if (!tr.ReadZigZag32(se.numChildren)) return false; }
                    else if (sfid == 6 && sft == 5) { if (!tr.ReadZigZag32(se.convertedType)) return false; }
                    else { if (!tr.SkipValue(sft)) return false; }
                }
                out.schema.push_back(std::move(se));
            }
        } else if (fid == 3 && ftype == 6) {
            if (!tr.ReadZigZag64(out.numRows)) return false;
        } else if (fid == 4 && ftype == 9) {
            uint8_t et = 0; uint32_t cnt = 0;
            if (!tr.ReadListHeader(et, cnt) || et != 12) return false;
            for (uint32_t i = 0; i < cnt; ++i) {
                RowGroupMeta rg;
                int16_t rgLast = 0;
                for (;;) {
                    int16_t rfid = 0; uint8_t rft = 0;
                    if (!tr.ReadFieldHeader(rgLast, rfid, rft)) return false;
                    if (rft == 0) break;
                    if (rfid == 1 && rft == 9) {
                        uint8_t cet = 0; uint32_t ccnt = 0;
                        if (!tr.ReadListHeader(cet, ccnt) || cet != 12) return false;
                        for (uint32_t c = 0; c < ccnt; ++c) {
                            ColChunkMeta cm;
                            int64_t fileOff = 0;
                            int16_t ccLast = 0;
                            for (;;) {
                                int16_t cfid = 0; uint8_t cft = 0;
                                if (!tr.ReadFieldHeader(ccLast, cfid, cft)) return false;
                                if (cft == 0) break;
                                if (cfid == 2 && cft == 6) { if (!tr.ReadZigZag64(fileOff)) return false; }
                                else if (cfid == 3 && cft == 12) { if (!ParseColMetaStruct(tr, cm)) return false; }
                                else { if (!tr.SkipValue(cft)) return false; }
                            }
                            if (cm.dataPageOffset == 0 && fileOff > 0) cm.dataPageOffset = fileOff;
                            rg.columns.push_back(std::move(cm));
                        }
                    } else if (rfid == 2 && rft == 6) {
                        if (!tr.ReadZigZag64(rg.totalByteSize)) return false;
                    } else if (rfid == 3 && rft == 6) {
                        if (!tr.ReadZigZag64(rg.numRows)) return false;
                    } else {
                        if (!tr.SkipValue(rft)) return false;
                    }
                }
                out.rowGroups.push_back(std::move(rg));
            }
        } else if (fid == 5 && ftype == 9) {
            uint8_t et = 0; uint32_t cnt = 0;
            if (!tr.ReadListHeader(et, cnt) || et != 12) return false;
            for (uint32_t i = 0; i < cnt; ++i) {
                std::string k, v;
                int16_t kLast = 0;
                for (;;) {
                    int16_t kfid = 0; uint8_t kft = 0;
                    if (!tr.ReadFieldHeader(kLast, kfid, kft)) return false;
                    if (kft == 0) break;
                    if (kfid == 1 && kft == 8) { if (!tr.ReadBinary(k)) return false; }
                    else if (kfid == 2 && kft == 8) { if (!tr.ReadBinary(v)) return false; }
                    else { if (!tr.SkipValue(kft)) return false; }
                }
                if (out.kvMeta.size() < 64) out.kvMeta.push_back({ std::move(k), std::move(v) });
            }
        } else if (fid == 6 && ftype == 8) {
            if (!tr.ReadBinary(out.createdBy)) return false;
        } else {
            if (!tr.SkipValue(ftype)) return false;
        }
    }
    return !out.schema.empty() && out.version >= 1 && out.version <= 2 && out.numRows >= 0;
}

struct LeafColDesc {
    std::string fullPath;
    std::string leafName;
    int32_t physType = 0;
    int32_t typeLength = 0;
    int32_t repType = 0;
    int32_t convType = -1;
    int maxDefLevel = 0;
    int maxRepLevel = 0;
};

inline void CollectLeafColumns(
    const std::vector<SchemaElem>& schema,
    size_t& idx,
    const std::string& prefix,
    int defLvl,
    int repLvl,
    bool isRoot,
    std::vector<LeafColDesc>& outLeaves)
{
    if (idx >= schema.size()) return;
    const SchemaElem& se = schema[idx++];
    int nextDef = defLvl + (!isRoot && (se.repetitionType == 1 || se.repetitionType == 2) ? 1 : 0);
    int nextRep = repLvl + (!isRoot && se.repetitionType == 2 ? 1 : 0);
    std::string curPath = isRoot ? "" : (prefix.empty() ? se.name : (prefix + "." + se.name));

    if (se.numChildren > 0 && !se.hasType) {
        for (int32_t c = 0; c < se.numChildren && idx < schema.size(); ++c) {
            CollectLeafColumns(schema, idx, curPath, nextDef, nextRep, false, outLeaves);
        }
    } else if (!isRoot || se.hasType) {
        LeafColDesc lc;
        lc.fullPath = curPath.empty() ? se.name : curPath;
        lc.leafName = se.name;
        lc.physType = se.type;
        lc.typeLength = se.typeLength;
        lc.repType = se.repetitionType;
        lc.convType = se.convertedType;
        lc.maxDefLevel = nextDef;
        lc.maxRepLevel = nextRep;
        outLeaves.push_back(std::move(lc));
    }
}

struct ParsedPageHeader {
    int32_t pageType = -1; // 0=DATA_PAGE, 1=INDEX_PAGE, 2=DICTIONARY_PAGE, 3=DATA_PAGE_V2
    int32_t uncompressedSize = 0;
    int32_t compressedSize = 0;
    int32_t numValues = 0;
    int32_t encoding = 0;
    int32_t defLevelByteLenV2 = 0;
    int32_t repLevelByteLenV2 = 0;
    bool isCompressedV2 = true;
    size_t headerBytes = 0;
};

inline bool ParsePageHeader(const uint8_t* data, size_t maxAvail, ParsedPageHeader& ph) {
    ThriftCompactReader tr(data, maxAvail);
    int16_t lastId = 0;
    for (;;) {
        int16_t fid = 0; uint8_t ftype = 0;
        if (!tr.ReadFieldHeader(lastId, fid, ftype)) return false;
        if (ftype == 0) break;
        if (fid == 1 && ftype == 5) { if (!tr.ReadZigZag32(ph.pageType)) return false; }
        else if (fid == 2 && ftype == 5) { if (!tr.ReadZigZag32(ph.uncompressedSize)) return false; }
        else if (fid == 3 && ftype == 5) { if (!tr.ReadZigZag32(ph.compressedSize)) return false; }
        else if (fid == 5 && ftype == 12) { // DataPageHeader
            int16_t dLast = 0;
            for (;;) {
                int16_t dfid = 0; uint8_t dft = 0;
                if (!tr.ReadFieldHeader(dLast, dfid, dft)) return false;
                if (dft == 0) break;
                if (dfid == 1 && dft == 5) { if (!tr.ReadZigZag32(ph.numValues)) return false; }
                else if (dfid == 2 && dft == 5) { if (!tr.ReadZigZag32(ph.encoding)) return false; }
                else { if (!tr.SkipValue(dft)) return false; }
            }
        } else if (fid == 7 && ftype == 12) { // DictionaryPageHeader
            int16_t dLast = 0;
            for (;;) {
                int16_t dfid = 0; uint8_t dft = 0;
                if (!tr.ReadFieldHeader(dLast, dfid, dft)) return false;
                if (dft == 0) break;
                if (dfid == 1 && dft == 5) { if (!tr.ReadZigZag32(ph.numValues)) return false; }
                else if (dfid == 2 && dft == 5) { if (!tr.ReadZigZag32(ph.encoding)) return false; }
                else { if (!tr.SkipValue(dft)) return false; }
            }
        } else if (fid == 8 && ftype == 12) { // DataPageHeaderV2
            int16_t dLast = 0;
            for (;;) {
                int16_t dfid = 0; uint8_t dft = 0;
                if (!tr.ReadFieldHeader(dLast, dfid, dft)) return false;
                if (dft == 0) break;
                if (dfid == 1 && dft == 5) { if (!tr.ReadZigZag32(ph.numValues)) return false; }
                else if (dfid == 4 && dft == 5) { if (!tr.ReadZigZag32(ph.encoding)) return false; }
                else if (dfid == 5 && dft == 5) { if (!tr.ReadZigZag32(ph.defLevelByteLenV2)) return false; }
                else if (dfid == 6 && dft == 5) { if (!tr.ReadZigZag32(ph.repLevelByteLenV2)) return false; }
                else if (dfid == 7 && (dft == 1 || dft == 2)) { ph.isCompressedV2 = (dft == 1); }
                else { if (!tr.SkipValue(dft)) return false; }
            }
        } else {
            if (!tr.SkipValue(ftype)) return false;
        }
    }
    ph.headerBytes = tr.pos;
    return ph.pageType >= 0 && ph.pageType <= 3 && ph.compressedSize >= 0 && ph.uncompressedSize >= 0;
}

inline int BitWidthForMax(int maxVal) {
    int bw = 0;
    while ((1 << bw) <= maxVal) ++bw;
    return bw;
}

inline bool DecodeRleBitPackedStream(
    const uint8_t* buf,
    size_t len,
    int bitWidth,
    size_t wantedCount,
    std::vector<uint32_t>& outVals)
{
    outVals.clear();
    outVals.reserve(wantedCount);
    if (bitWidth == 0) {
        outVals.assign(wantedCount, 0);
        return true;
    }
    if (bitWidth < 0 || bitWidth > 32) return false;
    size_t pos = 0;
    const uint64_t mask = (bitWidth == 32) ? 0xFFFFFFFFULL : ((1ULL << bitWidth) - 1ULL);
    const size_t byteWidth = static_cast<size_t>((bitWidth + 7) / 8);

    while (pos < len && outVals.size() < wantedCount) {
        uint64_t hdr = 0;
        int shift = 0;
        while (pos < len && shift < 35) {
            uint8_t b = buf[pos++];
            hdr |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) break;
            shift += 7;
        }
        if ((hdr & 1ULL) == 0) {
            uint64_t runLen = hdr >> 1;
            if (pos + byteWidth > len) break;
            uint32_t val = 0;
            for (size_t b = 0; b < byteWidth; ++b) {
                val |= static_cast<uint32_t>(buf[pos++]) << (b * 8);
            }
            size_t take = static_cast<size_t>(std::min<uint64_t>(runLen, wantedCount - outVals.size()));
            outVals.insert(outVals.end(), take, val);
        } else {
            uint64_t groups = hdr >> 1;
            uint64_t count = groups * 8ULL;
            uint64_t totalBytes = groups * static_cast<uint64_t>(bitWidth);
            if (totalBytes > len - pos) totalBytes = len - pos;
            size_t bitPos = 0;
            const uint8_t* p = buf + pos;
            size_t maxBits = static_cast<size_t>(totalBytes) * 8;
            for (uint64_t i = 0; i < count && outVals.size() < wantedCount; ++i) {
                if (bitPos + static_cast<size_t>(bitWidth) > maxBits) break;
                size_t byteIdx = bitPos / 8;
                int bitOff = static_cast<int>(bitPos % 8);
                uint64_t raw64 = 0;
                size_t copyB = std::min<size_t>(8, static_cast<size_t>(totalBytes) - byteIdx);
                std::memcpy(&raw64, p + byteIdx, copyB);
                outVals.push_back(static_cast<uint32_t>((raw64 >> bitOff) & mask));
                bitPos += static_cast<size_t>(bitWidth);
            }
            pos += static_cast<size_t>(totalBytes);
        }
    }
    return !outVals.empty() || wantedCount == 0;
}

// Parquet v2 DELTA_BINARY_PACKED (encoding = 5) for INT32 and INT64
inline bool DecodeDeltaBinaryPackedInts(
    const uint8_t* buf,
    size_t len,
    size_t& ioPos,
    size_t maxValues,
    std::vector<int64_t>& outInts)
{
    outInts.clear();
    auto readUleb = [&](uint64_t& v) -> bool {
        v = 0;
        int sh = 0;
        while (ioPos < len && sh < 70) {
            uint8_t b = buf[ioPos++];
            v |= static_cast<uint64_t>(b & 0x7Fu) << sh;
            if ((b & 0x80u) == 0) return true;
            sh += 7;
        }
        return false;
    };
    auto readZigZag = [&](int64_t& sv) -> bool {
        uint64_t u = 0;
        if (!readUleb(u)) return false;
        sv = static_cast<int64_t>((u >> 1) ^ (~(u & 1ULL) + 1ULL));
        return true;
    };

    uint64_t blockSize = 0, numMiniBlocks = 0, totalValues = 0;
    int64_t firstVal = 0;
    if (!readUleb(blockSize) || !readUleb(numMiniBlocks) || !readUleb(totalValues) || !readZigZag(firstVal)) {
        return false;
    }
    if (totalValues == 0) return true;
    if (blockSize == 0 || numMiniBlocks == 0 || (blockSize % numMiniBlocks) != 0 || (blockSize / numMiniBlocks) % 8 != 0) {
        return false;
    }

    const uint64_t valsPerMini = blockSize / numMiniBlocks;
    const size_t targetTotal = static_cast<size_t>(std::min<uint64_t>(totalValues, 200000));
    outInts.reserve(std::min(targetTotal, maxValues));
    outInts.push_back(firstVal);
    int64_t curVal = firstVal;
    uint64_t decodedTotal = 1;

    while (decodedTotal < totalValues && ioPos < len) {
        int64_t minDelta = 0;
        if (!readZigZag(minDelta)) return false;
        if (ioPos + numMiniBlocks > len) return false;
        std::vector<uint8_t> bitWidths(buf + ioPos, buf + ioPos + static_cast<size_t>(numMiniBlocks));
        ioPos += static_cast<size_t>(numMiniBlocks);

        for (uint64_t mb = 0; mb < numMiniBlocks && decodedTotal < totalValues; ++mb) {
            uint8_t bw = bitWidths[static_cast<size_t>(mb)];
            if (bw > 64) return false;
            size_t mbBytes = static_cast<size_t>((valsPerMini * bw) / 8ULL);
            if (ioPos + mbBytes > len) return false;

            const uint8_t* mp = buf + ioPos;
            size_t bitCursor = 0;
            for (uint64_t k = 0; k < valsPerMini; ++k) {
                uint64_t delta = 0;
                if (bw > 0) {
                    size_t bIdx = bitCursor >> 3;
                    int bOff = static_cast<int>(bitCursor & 7u);
                    // Read up to 9 bytes safely for 64-bit unaligned bit-unpack
                    uint64_t lo = 0;
                    size_t cpy = std::min<size_t>(8, mbBytes - bIdx);
                    std::memcpy(&lo, mp + bIdx, cpy);
                    if (bOff == 0) {
                        delta = (bw == 64) ? lo : (lo & ((1ULL << bw) - 1ULL));
                    } else {
                        uint64_t hi = (bIdx + 8 < mbBytes) ? mp[bIdx + 8] : 0ULL;
                        uint64_t merged = (lo >> bOff) | (hi << (64 - bOff));
                        delta = (bw == 64) ? merged : (merged & ((1ULL << bw) - 1ULL));
                    }
                    bitCursor += bw;
                }
                if (decodedTotal < totalValues) {
                    curVal = static_cast<int64_t>(static_cast<uint64_t>(curVal) + static_cast<uint64_t>(minDelta) + delta);
                    if (outInts.size() < maxValues) outInts.push_back(curVal);
                    ++decodedTotal;
                }
            }
            ioPos += mbBytes;
        }
    }
    return !outInts.empty();
}

// Parquet v2 DELTA_LENGTH_BYTE_ARRAY (encoding = 6)
inline bool DecodeDeltaLengthByteArray(
    const uint8_t* buf,
    size_t len,
    size_t& ioPos,
    int32_t convType,
    size_t nonNullCount,
    std::vector<std::wstring>& outVals)
{
    std::vector<int64_t> lengths;
    if (!DecodeDeltaBinaryPackedInts(buf, len, ioPos, nonNullCount, lengths)) return false;
    outVals.clear();
    outVals.reserve(lengths.size());
    for (int64_t sl : lengths) {
        if (sl < 0 || static_cast<size_t>(sl) > len - ioPos) return false;
        std::string s(reinterpret_cast<const char*>(buf + ioPos), static_cast<size_t>(sl));
        ioPos += static_cast<size_t>(sl);
        outVals.push_back(FormatRawStatValue(s, 6, convType));
    }
    return true;
}

// Parquet v2 DELTA_BYTE_ARRAY (encoding = 7: prefix-compressed strings)
inline bool DecodeDeltaByteArray(
    const uint8_t* buf,
    size_t len,
    size_t& ioPos,
    int32_t convType,
    size_t nonNullCount,
    std::vector<std::wstring>& outVals)
{
    std::vector<int64_t> prefixLens;
    if (!DecodeDeltaBinaryPackedInts(buf, len, ioPos, nonNullCount, prefixLens)) return false;
    std::vector<int64_t> suffixLens;
    if (!DecodeDeltaBinaryPackedInts(buf, len, ioPos, nonNullCount, suffixLens)) return false;
    const size_t n = std::min(prefixLens.size(), suffixLens.size());
    outVals.clear();
    outVals.reserve(n);
    std::string prev;
    for (size_t i = 0; i < n; ++i) {
        int64_t pl = prefixLens[i];
        int64_t sl = suffixLens[i];
        if (pl < 0 || sl < 0 || static_cast<size_t>(pl) > prev.size() || static_cast<size_t>(sl) > len - ioPos) {
            return false;
        }
        std::string cur = prev.substr(0, static_cast<size_t>(pl));
        cur.append(reinterpret_cast<const char*>(buf + ioPos), static_cast<size_t>(sl));
        ioPos += static_cast<size_t>(sl);
        outVals.push_back(FormatRawStatValue(cur, 6, convType));
        prev = std::move(cur);
    }
    return true;
}

// Parquet v2 BYTE_STREAM_SPLIT (encoding = 9) for FLOAT / DOUBLE / INT32 / INT64 / FIXED_LEN_BYTE_ARRAY
inline bool DecodeByteStreamSplit(
    const uint8_t* buf,
    size_t len,
    size_t& ioPos,
    const LeafColDesc& col,
    size_t nonNullCount,
    std::vector<std::wstring>& outVals)
{
    size_t elemW = 0;
    if (col.physType == 1 || col.physType == 4) elemW = 4;
    else if (col.physType == 2 || col.physType == 5) elemW = 8;
    else if (col.physType == 7 && col.typeLength > 0) elemW = static_cast<size_t>(col.typeLength);
    if (elemW == 0 || nonNullCount * elemW > len - ioPos) return false;

    outVals.clear();
    outVals.reserve(nonNullCount);
    const uint8_t* base = buf + ioPos;
    std::string tmp(elemW, '\0');
    for (size_t i = 0; i < nonNullCount; ++i) {
        for (size_t b = 0; b < elemW; ++b) {
            tmp[b] = static_cast<char>(base[b * nonNullCount + i]);
        }
        outVals.push_back(FormatRawStatValue(tmp, col.physType, col.convType));
    }
    ioPos += nonNullCount * elemW;
    return true;
}

inline bool DecodePlainValues(
    const uint8_t* buf,
    size_t len,
    size_t& ioPos,
    const LeafColDesc& col,
    size_t count,
    std::vector<std::wstring>& outVals)
{
    outVals.clear();
    outVals.reserve(count);
    size_t bitIdx = 0;
    for (size_t i = 0; i < count; ++i) {
        if (col.physType == 0) {
            size_t bOff = ioPos + (bitIdx / 8);
            if (bOff >= len) break;
            bool bit = ((buf[bOff] >> (bitIdx % 8)) & 1u) != 0;
            ++bitIdx;
            outVals.push_back(bit ? L"true" : L"false");
        } else if (col.physType == 1) {
            if (ioPos + 4 > len) break;
            int32_t v = static_cast<int32_t>(RdU32LE(buf + ioPos));
            ioPos += 4;
            outVals.push_back(std::to_wstring(v));
        } else if (col.physType == 2) {
            if (ioPos + 8 > len) break;
            int64_t v = static_cast<int64_t>(RdU64LE(buf + ioPos));
            ioPos += 8;
            outVals.push_back(std::to_wstring(v));
        } else if (col.physType == 3) {
            if (ioPos + 12 > len) break;
            uint64_t nanos = RdU64LE(buf + ioPos);
            uint32_t jday = RdU32LE(buf + ioPos + 8);
            ioPos += 12;
            int64_t unixSec = (static_cast<int64_t>(jday) - 2440588LL) * 86400LL + static_cast<int64_t>(nanos / 1000000000ULL);
            outVals.push_back(std::to_wstring(unixSec));
        } else if (col.physType == 4) {
            if (ioPos + 4 > len) break;
            float f = 0.0f;
            std::memcpy(&f, buf + ioPos, 4);
            ioPos += 4;
            wchar_t b[64] = {};
            swprintf_s(b, L"%.6g", f);
            outVals.push_back(b);
        } else if (col.physType == 5) {
            if (ioPos + 8 > len) break;
            double d = 0.0;
            std::memcpy(&d, buf + ioPos, 8);
            ioPos += 8;
            wchar_t b[64] = {};
            swprintf_s(b, L"%.12g", d);
            outVals.push_back(b);
        } else if (col.physType == 6) {
            if (ioPos + 4 > len) break;
            uint32_t slen = RdU32LE(buf + ioPos);
            ioPos += 4;
            if (slen > len - ioPos) break;
            std::string s(reinterpret_cast<const char*>(buf + ioPos), slen);
            ioPos += slen;
            outVals.push_back(FormatRawStatValue(s, 6, col.convType));
        } else if (col.physType == 7 && col.typeLength > 0) {
            size_t fl = static_cast<size_t>(col.typeLength);
            if (ioPos + fl > len) break;
            std::string s(reinterpret_cast<const char*>(buf + ioPos), fl);
            ioPos += fl;
            outVals.push_back(FormatRawStatValue(s, 7, col.convType));
        } else {
            break;
        }
    }
    if (col.physType == 0) {
        ioPos += (bitIdx + 7) / 8;
    }
    return outVals.size() == count;
}

inline bool DecompressPageBytes(
    int32_t codec,
    const uint8_t* compPtr,
    size_t compLen,
    size_t uncompLen,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    const size_t maxCap = std::max<size_t>(uncompLen + 65536, 65536);
    if (codec == 0) { // UNCOMPRESSED
        if (uncompLen > 0 && compLen != uncompLen) return false;
        outBytes.assign(compPtr, compPtr + compLen);
        return true;
    }
    if (codec == 1) { // SNAPPY
        return SnappyDecompress(compPtr, compLen, outBytes, maxCap);
    }
    if (codec == 2) { // GZIP
        return ZipDeflate::InflateGzip(compPtr, compLen, outBytes, maxCap);
    }
    if (codec == 5) { // LZ4 (Hadoop block framing / LZ4 frame)
        return ParquetLz4HadoopOrFrameDecompress(compPtr, compLen, uncompLen, outBytes);
    }
    if (codec == 6) { // ZSTD (RFC 8878 frame)
        return StreamCodecs::DecompressZstdFrame(compPtr, compLen, outBytes, maxCap) &&
               (uncompLen == 0 || outBytes.size() == uncompLen);
    }
    if (codec == 7) { // LZ4_RAW (unframed LZ4 block)
        return ::Explorer2::Lz4::DecodeBlockAppend(compPtr, compLen, outBytes, outBytes.size(), maxCap) &&
               (uncompLen == 0 || outBytes.size() == uncompLen);
    }
    return false;
}

// Decodes column preview from a contiguous byte slice starting at `sliceFileOffset`
inline bool DecodeColumnPreviewSlice(
    const uint8_t* slicePtr,
    size_t sliceLen,
    uint64_t sliceFileOffset,
    const LeafColDesc& leaf,
    const ColChunkMeta& cm,
    size_t maxRows,
    std::vector<std::wstring>& outCells)
{
    outCells.clear();
    if (!cm.valid || !slicePtr || sliceLen == 0) return false;

    auto fileOffToLocal = [&](int64_t foff, size_t& outLoc) -> bool {
        if (foff < 0 || static_cast<uint64_t>(foff) < sliceFileOffset) return false;
        uint64_t rel = static_cast<uint64_t>(foff) - sliceFileOffset;
        if (rel >= sliceLen) return false;
        outLoc = static_cast<size_t>(rel);
        return true;
    };

    std::vector<std::wstring> dictValues;
    auto readDictAt = [&](size_t locOff, size_t& outAfterDict) -> bool {
        outAfterDict = locOff;
        if (locOff >= sliceLen) return false;
        ParsedPageHeader dph;
        if (!ParsePageHeader(slicePtr + locOff, std::min<size_t>(512, sliceLen - locOff), dph)) return false;
        if (dph.pageType != 2) return false;
        size_t payOff = locOff + dph.headerBytes;
        size_t compSz = static_cast<size_t>(dph.compressedSize);
        if (payOff + compSz > sliceLen) return false;
        outAfterDict = payOff + compSz;
        std::vector<uint8_t> dBuf;
        if (!DecompressPageBytes(cm.codec, slicePtr + payOff, compSz, static_cast<size_t>(dph.uncompressedSize), dBuf)) {
            return false;
        }
        size_t dPos = 0;
        return DecodePlainValues(dBuf.data(), dBuf.size(), dPos, leaf, static_cast<size_t>(std::max(0, dph.numValues)), dictValues);
    };

    size_t pageCursor = 0;
    if (!fileOffToLocal(cm.dataPageOffset, pageCursor)) return false;

    if (cm.dictionaryPageOffset >= 4) {
        size_t dictLoc = 0;
        if (fileOffToLocal(cm.dictionaryPageOffset, dictLoc)) {
            size_t afterDict = 0;
            if (!readDictAt(dictLoc, afterDict)) return false;
            if (dictLoc == pageCursor) pageCursor = afterDict;
        }
    }

    const size_t targetVals = (cm.numValues > 0)
        ? std::min<size_t>(maxRows, static_cast<size_t>(cm.numValues))
        : maxRows;
    int pagesRead = 0;
    bool pageDecodeFailed = false;
    while (pageCursor < sliceLen && outCells.size() < targetVals && pagesRead++ < 16) {
        ParsedPageHeader ph;
        if (!ParsePageHeader(slicePtr + pageCursor, std::min<size_t>(512, sliceLen - pageCursor), ph)) {
            if (outCells.empty()) pageDecodeFailed = true;
            break;
        }
        size_t payOff = pageCursor + ph.headerBytes;
        size_t compSz = static_cast<size_t>(ph.compressedSize);
        if (payOff + compSz > sliceLen) {
            if (outCells.empty()) pageDecodeFailed = true;
            break;
        }
        pageCursor = payOff + compSz;

        if (ph.pageType == 2) {
            std::vector<uint8_t> dBuf;
            if (!DecompressPageBytes(cm.codec, slicePtr + payOff, compSz, static_cast<size_t>(ph.uncompressedSize), dBuf)) {
                pageDecodeFailed = true;
                break;
            }
            size_t dPos = 0;
            if (!DecodePlainValues(dBuf.data(), dBuf.size(), dPos, leaf, static_cast<size_t>(std::max(0, ph.numValues)), dictValues)) {
                pageDecodeFailed = true;
                break;
            }
            continue;
        }
        if (ph.pageType != 0 && ph.pageType != 3) continue;

        size_t numValues = static_cast<size_t>(std::min<int32_t>(std::max<int32_t>(0, ph.numValues), 100000));
        if (numValues == 0) continue;

        std::vector<uint8_t> pageBuf;
        size_t valPos = 0;
        std::vector<uint32_t> defLevels;

        if (ph.pageType == 0) { // DATA_PAGE v1
            if (!DecompressPageBytes(cm.codec, slicePtr + payOff, compSz, static_cast<size_t>(ph.uncompressedSize), pageBuf)) {
                pageDecodeFailed = true;
                break;
            }
            if (leaf.maxRepLevel > 0) {
                if (valPos + 4 > pageBuf.size()) { pageDecodeFailed = true; break; }
                uint32_t repBytes = RdU32LE(pageBuf.data() + valPos);
                valPos += 4;
                if (repBytes > pageBuf.size() - valPos) { pageDecodeFailed = true; break; }
                valPos += repBytes;
            }
            if (leaf.maxDefLevel > 0) {
                if (valPos + 4 > pageBuf.size()) { pageDecodeFailed = true; break; }
                uint32_t defBytes = RdU32LE(pageBuf.data() + valPos);
                valPos += 4;
                if (defBytes > pageBuf.size() - valPos) { pageDecodeFailed = true; break; }
                DecodeRleBitPackedStream(pageBuf.data() + valPos, defBytes,
                    BitWidthForMax(leaf.maxDefLevel), numValues, defLevels);
                valPos += defBytes;
            }
        } else { // DATA_PAGE_V2
            size_t repLen = static_cast<size_t>(std::max(0, ph.repLevelByteLenV2));
            size_t defLen = static_cast<size_t>(std::max(0, ph.defLevelByteLenV2));
            if (repLen + defLen > compSz) { pageDecodeFailed = true; break; }
            if (leaf.maxDefLevel > 0 && defLen > 0) {
                DecodeRleBitPackedStream(slicePtr + payOff + repLen, defLen,
                    BitWidthForMax(leaf.maxDefLevel), numValues, defLevels);
            }
            const uint8_t* valComp = slicePtr + payOff + repLen + defLen;
            size_t valCompLen = compSz - repLen - defLen;
            size_t valUncompLen = (static_cast<size_t>(ph.uncompressedSize) >= repLen + defLen)
                ? (static_cast<size_t>(ph.uncompressedSize) - repLen - defLen) : 0;
            int32_t effCodec = ph.isCompressedV2 ? cm.codec : 0;
            if (!DecompressPageBytes(effCodec, valComp, valCompLen, valUncompLen, pageBuf)) {
                pageDecodeFailed = true;
                break;
            }
            valPos = 0;
        }

        size_t nonNullCount = numValues;
        if (leaf.maxDefLevel > 0 && !defLevels.empty()) {
            nonNullCount = 0;
            for (size_t i = 0; i < std::min(numValues, defLevels.size()); ++i) {
                if (static_cast<int>(defLevels[i]) >= leaf.maxDefLevel) ++nonNullCount;
            }
        }

        std::vector<std::wstring> nonNullVals;
        bool encOk = false;
        if (nonNullCount == 0) {
            encOk = true;
        } else if (ph.encoding == 0) { // PLAIN
            encOk = DecodePlainValues(pageBuf.data(), pageBuf.size(), valPos, leaf, nonNullCount, nonNullVals);
        } else if ((ph.encoding == 8 || ph.encoding == 2) && !dictValues.empty()) { // RLE_DICTIONARY / PLAIN_DICTIONARY
            if (valPos < pageBuf.size()) {
                int bw = pageBuf[valPos++];
                std::vector<uint32_t> dictIdx;
                if (DecodeRleBitPackedStream(pageBuf.data() + valPos, pageBuf.size() - valPos, bw, nonNullCount, dictIdx)) {
                    nonNullVals.reserve(dictIdx.size());
                    encOk = true;
                    for (uint32_t idx : dictIdx) {
                        if (idx >= dictValues.size()) { encOk = false; break; }
                        nonNullVals.push_back(dictValues[idx]);
                    }
                }
            }
        } else if (ph.encoding == 3 && leaf.physType == 0) { // RLE BOOLEAN
            size_t rleOff = valPos;
            size_t rleLen = pageBuf.size() - valPos;
            if (ph.pageType == 0 && rleLen >= 4) {
                uint32_t bl = RdU32LE(pageBuf.data() + valPos);
                if (4 + bl <= rleLen) { rleOff += 4; rleLen = bl; }
            }
            std::vector<uint32_t> bits;
            encOk = DecodeRleBitPackedStream(pageBuf.data() + rleOff, rleLen, 1, nonNullCount, bits);
            for (uint32_t b : bits) nonNullVals.push_back(b ? L"true" : L"false");
        } else if (ph.encoding == 5 && (leaf.physType == 1 || leaf.physType == 2)) { // DELTA_BINARY_PACKED
            std::vector<int64_t> ints;
            encOk = DecodeDeltaBinaryPackedInts(pageBuf.data(), pageBuf.size(), valPos, nonNullCount, ints);
            if (encOk) {
                nonNullVals.reserve(ints.size());
                for (int64_t iv : ints) {
                    if (leaf.physType == 1) nonNullVals.push_back(std::to_wstring(static_cast<int32_t>(iv)));
                    else nonNullVals.push_back(std::to_wstring(iv));
                }
            }
        } else if (ph.encoding == 6 && leaf.physType == 6) { // DELTA_LENGTH_BYTE_ARRAY
            encOk = DecodeDeltaLengthByteArray(pageBuf.data(), pageBuf.size(), valPos, leaf.convType, nonNullCount, nonNullVals);
        } else if (ph.encoding == 7 && (leaf.physType == 6 || leaf.physType == 7)) { // DELTA_BYTE_ARRAY
            encOk = DecodeDeltaByteArray(pageBuf.data(), pageBuf.size(), valPos, leaf.convType, nonNullCount, nonNullVals);
        } else if (ph.encoding == 9) { // BYTE_STREAM_SPLIT
            encOk = DecodeByteStreamSplit(pageBuf.data(), pageBuf.size(), valPos, leaf, nonNullCount, nonNullVals);
        }

        if (!encOk) {
            pageDecodeFailed = true;
            break;
        }

        size_t nnIdx = 0;
        size_t takeSlots = std::min(numValues, targetVals - outCells.size());
        for (size_t i = 0; i < takeSlots; ++i) {
            bool isNull = (leaf.maxDefLevel > 0 && i < defLevels.size() && static_cast<int>(defLevels[i]) < leaf.maxDefLevel);
            if (isNull) {
                outCells.push_back(L"NULL");
            } else if (nnIdx < nonNullVals.size()) {
                outCells.push_back(std::move(nonNullVals[nnIdx++]));
            } else {
                break;
            }
        }
    }

    return !outCells.empty() || (!pageDecodeFailed && cm.numValues == 0);
}

template <typename ReadChunkFn>
inline bool BuildParquetDocument(
    uint64_t totalFileSize,
    uint64_t footerStart,
    const ParquetFooter& footer,
    ReadChunkFn&& readChunkSlice,
    ParsedOfficeDocument& out)
{
    std::vector<LeafColDesc> leaves;
    size_t sIdx = 0;
    CollectLeafColumns(footer.schema, sIdx, "", 0, 0, true, leaves);
    if (leaves.empty()) return false;

    // Validate column offsets lie within [4 .. footerStart]
    for (const auto& rg : footer.rowGroups) {
        for (const auto& cm : rg.columns) {
            if (!cm.valid) continue;
            int64_t firstOff = (cm.dictionaryPageOffset >= 4 && cm.dictionaryPageOffset < cm.dataPageOffset)
                ? cm.dictionaryPageOffset : cm.dataPageOffset;
            if (cm.numValues > 0) {
                if (firstOff < 4 || static_cast<uint64_t>(firstOff) >= footerStart) return false;
                if (cm.totalCompressedSize < 0 ||
                    static_cast<uint64_t>(firstOff) + static_cast<uint64_t>(cm.totalCompressedSize) > footerStart) {
                    return false;
                }
            }
        }
    }

    SpreadsheetSheet schemaSheet;
    schemaSheet.name = L"Parquet_Schema_And_Stats";
    schemaSheet.maxCols = 8;
    schemaSheet.rows.push_back({
        L"Column / Metric", L"Physical Type", L"Converted Type", L"Repetition",
        L"Codec", L"Values", L"Comp / Uncomp Size", L"Min .. Max (Nulls)"
    });

    schemaSheet.rows.push_back({
        L"[File] Total Rows", std::to_wstring(footer.numRows),
        L"v" + std::to_wstring(footer.version),
        std::to_wstring(footer.rowGroups.size()) + L" row group(s)",
        std::to_wstring(leaves.size()) + L" leaf col(s)",
        L"-",
        PathUtils::FormatByteSize(totalFileSize),
        footer.createdBy.empty() ? L"-" : PathUtils::Utf8ToWide(footer.createdBy)
    });

    for (size_t i = 0; i < leaves.size(); ++i) {
        const auto& lc = leaves[i];
        int32_t codec = 0;
        int64_t totalVals = 0, totalComp = 0, totalUncomp = 0, totalNulls = 0;
        bool hasNulls = false;
        std::wstring minStr, maxStr;

        for (const auto& rg : footer.rowGroups) {
            if (i < rg.columns.size() && rg.columns[i].valid) {
                const auto& cm = rg.columns[i];
                codec = cm.codec;
                totalVals += cm.numValues;
                totalComp += cm.totalCompressedSize;
                totalUncomp += cm.totalUncompressedSize;
                if (cm.stats.nullCount >= 0) { totalNulls += cm.stats.nullCount; hasNulls = true; }
                if (minStr.empty() && cm.stats.hasMin) {
                    minStr = FormatRawStatValue(cm.stats.minRaw, lc.physType, lc.convType);
                }
                if (maxStr.empty() && cm.stats.hasMax) {
                    maxStr = FormatRawStatValue(cm.stats.maxRaw, lc.physType, lc.convType);
                }
            }
        }

        std::wstring statSummary = L"-";
        if (!minStr.empty() || !maxStr.empty() || hasNulls) {
            statSummary = (minStr.empty() ? L"?" : minStr) + L" .. " + (maxStr.empty() ? L"?" : maxStr);
            if (hasNulls) statSummary += L" (nulls=" + std::to_wstring(totalNulls) + L")";
        }

        schemaSheet.rows.push_back({
            PathUtils::Utf8ToWide(lc.fullPath),
            PhysicalTypeName(lc.physType, lc.typeLength),
            ConvertedTypeName(lc.convType),
            RepetitionName(lc.repType),
            CodecName(codec),
            std::to_wstring(totalVals),
            std::to_wstring(totalComp) + L" B / " + std::to_wstring(totalUncomp) + L" B",
            statSummary
        });
    }

    SpreadsheetSheet dataSheet;
    dataSheet.name = L"Parquet_Data_Preview";
    if (!leaves.empty() && !footer.rowGroups.empty()) {
        constexpr size_t kMaxPreviewRows = 500;
        std::vector<std::wstring> hdrRow;
        hdrRow.reserve(leaves.size());
        for (const auto& lc : leaves) hdrRow.push_back(PathUtils::Utf8ToWide(lc.fullPath));
        dataSheet.maxCols = hdrRow.size();
        dataSheet.rows.push_back(std::move(hdrRow));

        std::vector<std::vector<std::wstring>> colData(leaves.size());
        size_t maxDecodedRows = 0;
        const auto& rg0 = footer.rowGroups[0];
        std::vector<uint8_t> chunkBuf;

        for (size_t c = 0; c < leaves.size() && c < rg0.columns.size(); ++c) {
            const auto& cm = rg0.columns[c];
            if (!cm.valid || cm.numValues <= 0) continue;
            uint64_t startOff = static_cast<uint64_t>((cm.dictionaryPageOffset >= 4 && cm.dictionaryPageOffset < cm.dataPageOffset)
                ? cm.dictionaryPageOffset : cm.dataPageOffset);
            uint64_t endOff = footerStart;
            if (cm.totalCompressedSize > 0) {
                endOff = std::min<uint64_t>(footerStart, startOff + static_cast<uint64_t>(cm.totalCompressedSize));
            }
            size_t readLen = static_cast<size_t>(std::min<uint64_t>(endOff - startOff, 16ULL * 1024ULL * 1024ULL));
            const uint8_t* ptr = readChunkSlice(startOff, readLen, chunkBuf);
            if (!ptr) return false;
            if (!DecodeColumnPreviewSlice(ptr, readLen, startOff, leaves[c], cm, kMaxPreviewRows, colData[c])) {
                return false; // Corrupt page payload or truncated compressed page!
            }
            if (colData[c].size() > maxDecodedRows) maxDecodedRows = colData[c].size();
        }

        if (rg0.numRows > 0 && !rg0.columns.empty() && maxDecodedRows == 0) {
            return false;
        }

        for (size_t r = 0; r < maxDecodedRows; ++r) {
            std::vector<std::wstring> row(leaves.size());
            for (size_t c = 0; c < leaves.size(); ++c) {
                if (r < colData[c].size()) row[c] = std::move(colData[c][r]);
            }
            dataSheet.rows.push_back(std::move(row));
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apache Parquet Columnar (" +
        (footer.createdBy.empty() ? std::wstring(L".parquet") : PathUtils::Utf8ToWide(footer.createdBy)) + L")";
    out.sheets.push_back(std::move(schemaSheet));
    if (dataSheet.rows.size() >= 2) {
        out.sheets.push_back(std::move(dataSheet));
    }
    out.valid = true;
    return true;
}

inline bool ParseLegacyPar1TokenContainer(const uint8_t* data, size_t size, ParsedOfficeDocument& out) {
    if (size < 12 || size > 65536) return false;
    bool allValidIdentOrNul = true;
    std::vector<std::string> tokens;
    std::string cur;
    for (size_t i = 4; i < size - 8; ++i) {
        uint8_t b = data[i];
        if (b == 0) {
            if (!cur.empty()) { tokens.push_back(std::move(cur)); cur.clear(); }
        } else if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
                   (b >= '0' && b <= '9') || b == '_' || b == '-' || b == '.') {
            cur.push_back(static_cast<char>(b));
        } else {
            allValidIdentOrNul = false;
            break;
        }
    }
    if (!cur.empty()) tokens.push_back(std::move(cur));
    if (!allValidIdentOrNul || tokens.size() < 2) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apache Parquet Columnar (.parquet)";
    SpreadsheetSheet sheet;
    sheet.name = L"Parquet_Schema_And_Stats";
    sheet.maxCols = 3;
    sheet.rows.push_back({ L"#", L"Column / Metadata Token", L"Segment" });
    for (size_t i = 0; i < tokens.size(); ++i) {
        sheet.rows.push_back({
            std::to_wstring(i + 1),
            PathUtils::Utf8ToWide(tokens[i]),
            (i + 1 == tokens.size()) ? L"Created By" : L"Schema Column"
        });
    }
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}

} // namespace ParquetDetail

inline bool ParseApacheParquet(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 12 || std::memcmp(raw.data(), "PAR1", 4) != 0 ||
        std::memcmp(raw.data() + raw.size() - 4, "PAR1", 4) != 0) {
        return false;
    }

    uint32_t footerLen = ParquetDetail::RdU32LE(raw.data() + raw.size() - 8);
    if (footerLen == 0 || static_cast<uint64_t>(footerLen) + 12ULL > raw.size()) {
        return false;
    }
    size_t footerStart = raw.size() - 8 - footerLen;

    ParquetDetail::ParquetFooter footer;
    if (ParquetDetail::ParseFileMetaData(raw.data() + footerStart, footerLen, footer)) {
        auto sliceFn = [&](uint64_t off, size_t len, std::vector<uint8_t>&) -> const uint8_t* {
            if (off + len > raw.size()) return nullptr;
            return raw.data() + static_cast<size_t>(off);
        };
        return ParquetDetail::BuildParquetDocument(raw.size(), footerStart, footer, sliceFn, out);
    }

    return ParquetDetail::ParseLegacyPar1TokenContainer(raw.data(), raw.size(), out);
}

inline bool ParseApacheParquetFile(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    ParquetDetail::WinParquetRandomFile rf;
    if (!rf.Open(fullPath) || rf.fileSize < 12) return false;

    // Fast path for files <= 16 MB: read once into RAM
    if (rf.fileSize <= 16ULL * 1024ULL * 1024ULL) {
        std::vector<uint8_t> raw(static_cast<size_t>(rf.fileSize));
        if (!rf.ReadAt(0, raw.data(), raw.size())) return false;
        return ParseApacheParquet(raw, out);
    }

    // Large >16 MB (including >32 MB / multi-GB) random-access path
    uint8_t head4[4] = {};
    uint8_t tail8[8] = {};
    if (!rf.ReadAt(0, head4, 4) || !rf.ReadAt(rf.fileSize - 8, tail8, 8)) return false;
    if (std::memcmp(head4, "PAR1", 4) != 0 || std::memcmp(tail8 + 4, "PAR1", 4) != 0) return false;

    uint32_t footerLen = ParquetDetail::RdU32LE(tail8);
    if (footerLen == 0 || footerLen > 64u * 1024u * 1024u || static_cast<uint64_t>(footerLen) + 12ULL > rf.fileSize) {
        return false;
    }
    uint64_t footerStart = rf.fileSize - 8ULL - footerLen;
    std::vector<uint8_t> footerBuf(footerLen);
    if (!rf.ReadAt(footerStart, footerBuf.data(), footerLen)) return false;

    ParquetDetail::ParquetFooter footer;
    if (!ParquetDetail::ParseFileMetaData(footerBuf.data(), footerBuf.size(), footer)) return false;

    auto sliceFn = [&](uint64_t off, size_t len, std::vector<uint8_t>& scratch) -> const uint8_t* {
        scratch.resize(len);
        if (!rf.ReadAt(off, scratch.data(), len)) return nullptr;
        return scratch.data();
    };
    return ParquetDetail::BuildParquetDocument(rf.fileSize, footerStart, footer, sliceFn, out);
}

} // namespace Formats
