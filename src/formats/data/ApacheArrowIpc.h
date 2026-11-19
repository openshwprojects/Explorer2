#pragma once
// ApacheArrowIpc.h — Apache Arrow IPC File & Stream (.arrow / .ipc) Reader

namespace Formats {
namespace AiDataDetail {

inline bool ParseApacheArrowIpc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() >= 18 && std::memcmp(raw.data(), "ARROW1", 6) == 0) {
        return ParseArrowRandomAccessMemory(raw, extLower, L"Apache Arrow Columnar IPC", out);
    }

    std::vector<ArrowColSchema> cols;
    std::unordered_map<int64_t, std::vector<std::wstring>> dictMap;
    std::vector<std::vector<std::wstring>> dataRows;
    std::wstring compLabel;
    int64_t totalRows = 0;
    size_t batchCount = 0;


    if (raw.size() >= 12) {
        // 3. Arrow IPC Stream Format (0xFFFFFFFF + uint32 metaLen + Message FlatBuffer + Body)
        size_t pos = 0;
        while (pos + 8 <= raw.size()) {
            uint32_t w0 = RdU32LE(raw.data() + pos);
            uint32_t metaLen = 0;
            if (w0 == 0xFFFFFFFFu) {
                metaLen = RdU32LE(raw.data() + pos + 4);
                pos += 8;
            } else {
                if (pos == 0 && w0 > raw.size()) return false;
                metaLen = w0;
                pos += 4;
            }
            if (metaLen == 0) break;
            if (metaLen > raw.size() - pos) return false;

            FbTable msg;
            if (!GetFbRootTable(raw.data() + pos, metaLen, msg)) return false;
            pos += metaLen;

            uint8_t hdrType = msg.GetU8(1, 0);
            int64_t bodyLen = msg.GetI64(3, 0);
            if (bodyLen < 0 || static_cast<uint64_t>(bodyLen) > raw.size() - pos) return false;

            if (hdrType == 1) {
                FbTable schemaTbl;
                if (msg.GetTable(2, schemaTbl)) ParseArrowSchemaFb(schemaTbl, cols);
            } else if (hdrType == 2 && !cols.empty()) {
                FbTable db;
                if (!msg.GetTable(2, db) ||
                    !DecodeArrowDictionaryBatch(db, raw.data() + pos, static_cast<size_t>(bodyLen), cols, dictMap)) return false;
            } else if (hdrType == 3 && !cols.empty()) {
                ++batchCount;
                FbTable rb;
                if (!msg.GetTable(2, rb) ||
                    !DecodeArrowRecordBatch(rb, raw.data() + pos, static_cast<size_t>(bodyLen), cols, dictMap, dataRows, totalRows, &compLabel)) {
                    return false;
                }
            } else if (pos == metaLen + 8 && cols.empty()) {
                return false;
            }
            pos += static_cast<size_t>(bodyLen);
        }
    }

    return FinalizeArrowSheets(extLower, compLabel, batchCount, totalRows, cols, dataRows, out);
}

inline bool ParseApacheArrowIpcFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
    ParquetDetail::WinParquetRandomFile rf;
    if (!rf.Open(fullPath) || rf.fileSize < 12) return false;

    if (rf.fileSize <= 16ULL * 1024ULL * 1024ULL) {
        std::vector<uint8_t> raw(static_cast<size_t>(rf.fileSize));
        if (!rf.ReadAt(0, raw.data(), raw.size())) return false;
        return ParseApacheArrowIpc(raw, extLower, out);
    }

    uint8_t head8[8] = {};
    if (!rf.ReadAt(0, head8, 8)) return false;
    if (std::memcmp(head8, "ARROW1", 6) == 0) {
        return ParseArrowRandomAccessFile(rf, extLower, L"Apache Arrow Columnar IPC", out);
    }

    std::vector<ArrowColSchema> cols;
    std::unordered_map<int64_t, std::vector<std::wstring>> dictMap;
    std::vector<std::vector<std::wstring>> dataRows;
    std::wstring compLabel;
    int64_t totalRows = 0;
    size_t batchCount = 0;

    // Large >16 MB Arrow IPC Stream (0xFFFFFFFF + metaLen + Message + Body)
    uint64_t pos = 0;
    std::vector<uint8_t> metaBuf, bodyBuf;
    while (pos + 8 <= rf.fileSize) {
        uint8_t pfx[8] = {};
        if (!rf.ReadAt(pos, pfx, 8)) break;
        uint32_t w0 = RdU32LE(pfx);
        uint32_t metaLen = 0;
        if (w0 == 0xFFFFFFFFu) {
            metaLen = RdU32LE(pfx + 4);
            pos += 8;
        } else {
            if (pos == 0 && w0 > 16 * 1024 * 1024) return false;
            metaLen = w0;
            pos += 4;
        }
        if (metaLen == 0) break;
        if (metaLen > 16 * 1024 * 1024 || metaLen > rf.fileSize - pos) return false;

        metaBuf.resize(metaLen);
        if (!rf.ReadAt(pos, metaBuf.data(), metaLen)) return false;
        pos += metaLen;

        FbTable msg;
        if (!GetFbRootTable(metaBuf.data(), metaLen, msg)) return false;
        uint8_t hdrType = msg.GetU8(1, 0);
        int64_t bodyLen = msg.GetI64(3, 0);
        if (bodyLen < 0 || static_cast<uint64_t>(bodyLen) > rf.fileSize - pos) return false;

        if (hdrType == 1) {
            FbTable schemaTbl;
            if (msg.GetTable(2, schemaTbl)) ParseArrowSchemaFb(schemaTbl, cols);
        } else if (hdrType == 2 && !cols.empty() && bodyLen <= 64LL * 1024LL * 1024LL) {
            bodyBuf.resize(static_cast<size_t>(bodyLen));
            if (bodyLen > 0 && !rf.ReadAt(pos, bodyBuf.data(), bodyBuf.size())) return false;
            FbTable db;
            if (!msg.GetTable(2, db) || !DecodeArrowDictionaryBatch(db, bodyBuf.data(), bodyBuf.size(), cols, dictMap)) return false;
        } else if (hdrType == 3 && !cols.empty()) {
            ++batchCount;
            FbTable rb;
            if (!msg.GetTable(2, rb)) return false;
            if (dataRows.size() < 500 && bodyLen <= 128LL * 1024LL * 1024LL) {
                bodyBuf.resize(static_cast<size_t>(bodyLen));
                if (bodyLen > 0 && !rf.ReadAt(pos, bodyBuf.data(), bodyBuf.size())) return false;
                if (!DecodeArrowRecordBatch(rb, bodyBuf.data(), bodyBuf.size(), cols, dictMap, dataRows, totalRows, &compLabel)) {
                    return false;
                }
            } else {
                totalRows += std::max<int64_t>(0, rb.GetI64(0, 0));
            }
        } else if (cols.empty()) {
            return false;
        }
        pos += static_cast<uint64_t>(bodyLen);
    }

    return FinalizeArrowSheets(extLower, compLabel, batchCount, totalRows, cols, dataRows, out);
}

} // namespace AiDataDetail

inline bool ParseApacheArrowIpc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParseApacheArrowIpc(raw, extLower, out); }
inline bool ParseApacheArrowIpcFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParseApacheArrowIpcFile(fullPath, extLower, out); }

} // namespace Formats
