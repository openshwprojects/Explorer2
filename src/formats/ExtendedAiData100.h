#pragma once
// ExtendedAiData100.h — 12 AI/ML Models, Scientific Tensors, Columnar Tables & Notebooks (51–62)
// Covers: .onnx, .tflite, .pt/.pth/.ckpt, .npy/.npz, .h5/.hdf5/.mat, .feather/.arrow/.ipc,
//         .orc, .duckdb, .msgpack/.mpk, .cbor, .ipynb, .sav/.dta/.sas7bdat/.rds

namespace Formats {

inline bool ParseExtendedAiDataDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    // 54. NumPy Array (.npy — magic "\x93NUMPY" & .npz)
    if (extLower == L".npy" || (raw.size() >= 10 && std::memcmp(raw.data(), "\x93NUMPY", 6) == 0)) {
        uint8_t maj = raw[6];
        uint32_t hdrLen = (maj >= 2 && raw.size() >= 12)
            ? DetailSys100::RdU32LE(raw.data() + 8)
            : DetailSys100::RdU16LE(raw.data() + 8);
        size_t hdrStart = (maj >= 2) ? 12 : 10;
        std::string hdrDict;
        if (hdrStart + hdrLen <= raw.size()) {
            hdrDict.assign(reinterpret_cast<const char*>(raw.data() + hdrStart), hdrLen);
        }
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"NumPy N-Dimensional Tensor (.npy)";
        SpreadsheetSheet sh;
        sh.name = L"NumPy_Array";
        sh.maxCols = 2;
        sh.rows.push_back({ L"NumPy Tensor Field", L"Value" });
        sh.rows.push_back({ L"Format Version", std::to_wstring(maj) + L".0" });
        sh.rows.push_back({ L"Header Descriptor", PathUtils::Trim(PathUtils::Utf8ToWide(hdrDict)) });
        sh.rows.push_back({ L"Raw Data Payload", PathUtils::FormatByteSize(raw.size() > hdrStart + hdrLen ? raw.size() - (hdrStart + hdrLen) : 0) });

        // Sample first float32 / float64 / int32 elements if present
        size_t dataOff = hdrStart + hdrLen;
        if (hdrDict.find("f4") != std::string::npos && dataOff + 4 <= raw.size()) {
            size_t count = std::min<size_t>(16, (raw.size() - dataOff) / 4);
            for (size_t i = 0; i < count; ++i) {
                float v = 0.0f;
                std::memcpy(&v, raw.data() + dataOff + i * 4, 4);
                wchar_t b[64] = {};
                swprintf_s(b, L"%.6f", v);
                sh.rows.push_back({ L"Element [" + std::to_wstring(i) + L"] (float32)", b });
            }
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // 61. Jupyter Notebook (.ipynb — JSON with "cells" & "nbformat")
    if (extLower == L".ipynb") {
        out.kind = OfficeFileKind::WordDocx;
        out.isSpreadsheet = false;
        out.formatTitle = L"Jupyter Interactive Notebook (.ipynb)";

        out.paragraphs.push_back({ L"Jupyter Interactive Notebook (.ipynb)", 1, false, false });
        size_t pos = 0;
        int cellIdx = 1;
        while ((pos = sv.find("\"cell_type\"", pos)) != std::string_view::npos && out.paragraphs.size() < 200) {
            size_t colon = sv.find(':', pos);
            size_t q1 = (colon != std::string_view::npos) ? sv.find('"', colon + 1) : std::string_view::npos;
            size_t q2 = (q1 != std::string_view::npos) ? sv.find('"', q1 + 1) : std::string_view::npos;
            std::string ctype = (q1 != std::string_view::npos && q2 != std::string_view::npos)
                ? std::string(sv.substr(q1 + 1, q2 - q1 - 1)) : "code";

            out.paragraphs.push_back({ L"Cell [" + std::to_wstring(cellIdx++) + L"] — " + PathUtils::Utf8ToWide(ctype), 2, false, false });

            size_t srcKey = sv.find("\"source\"", pos);
            if (srcKey != std::string_view::npos && srcKey < pos + 1024) {
                size_t b1 = sv.find('[', srcKey);
                size_t b2 = (b1 != std::string_view::npos) ? sv.find(']', b1) : std::string_view::npos;
                if (b1 != std::string_view::npos && b2 != std::string_view::npos) {
                    std::string_view sblk = sv.substr(b1 + 1, b2 - b1 - 1);
                    out.paragraphs.push_back({ PathUtils::Trim(PathUtils::Utf8ToWide(sblk)), 0, false, false });
                }
            }
            pos += 11;
        }
        out.wordCount = out.paragraphs.size() * 8;
        out.charCount = raw.size();
        out.valid = !out.paragraphs.empty();
        return out.valid;
    }

    // 51, 52, 53, 55, 56, 57, 58, 59, 60, 62: ONNX, TFLite, PyTorch, HDF5, Arrow, ORC, DuckDB, MsgPack, CBOR, Stats
    if (extLower == L".onnx" || extLower == L".tflite" || extLower == L".pt" || extLower == L".pth" ||
        extLower == L".ckpt" || extLower == L".npz"    || extLower == L".h5" || extLower == L".hdf5" ||
        extLower == L".mat"  || extLower == L".feather"|| extLower == L".arrow" || extLower == L".ipc" ||
        extLower == L".orc"  || extLower == L".duckdb" || extLower == L".msgpack" || extLower == L".mpk" ||
        extLower == L".cbor" || extLower == L".sav"    || extLower == L".dta" || extLower == L".sas7bdat" ||
        extLower == L".rds")
    {
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;

        std::wstring title = L"AI / Data Science Container (" + extLower + L")";
        if (extLower == L".onnx") title = L"ONNX Neural Network Graph (.onnx)";
        else if (extLower == L".tflite") title = L"TensorFlow Lite FlatBuffer Model (.tflite)";
        else if (extLower == L".pt" || extLower == L".pth" || extLower == L".ckpt") title = L"PyTorch Checkpoint Archive (" + extLower + L")";
        else if (extLower == L".h5" || extLower == L".hdf5" || extLower == L".mat") title = L"HDF5 / MATLAB Hierarchical Data (" + extLower + L")";
        else if (extLower == L".feather" || extLower == L".arrow" || extLower == L".ipc") title = L"Apache Arrow Columnar IPC Table (" + extLower + L")";
        else if (extLower == L".orc") title = L"Apache ORC Columnar Storage (.orc)";
        else if (extLower == L".duckdb") title = L"DuckDB Analytical Database (.duckdb)";
        else if (extLower == L".msgpack" || extLower == L".mpk") title = L"MessagePack Binary Serialization (" + extLower + L")";
        else if (extLower == L".cbor") title = L"RFC 8949 CBOR Binary Object (.cbor)";
        out.formatTitle = title;

        SpreadsheetSheet sh;
        sh.name = L"Model_And_Schema";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Property / Attribute", L"Value" });
        sh.rows.push_back({ L"Format", title });
        sh.rows.push_back({ L"Total File Size", PathUtils::FormatByteSize(raw.size()) });

        // Extract readable ASCII identifiers (layer names, tensor keys, column names, ops)
        std::string cur;
        for (size_t i = 0; i < raw.size() && sh.rows.size() < 48; ++i) {
            uint8_t b = raw[i];
            if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_' || b == '.' || b == '/') {
                cur.push_back(static_cast<char>(b));
            } else {
                if (cur.size() >= 4 && cur.size() <= 64) {
                    sh.rows.push_back({ L"Identifier / Tensor / Column", PathUtils::Utf8ToWide(cur) });
                }
                cur.clear();
            }
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
