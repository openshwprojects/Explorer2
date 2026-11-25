// DocReadersSciAiData.cpp — Columnar/Analytics, AI/ML Tensors, Statistical & Scientific Format Readers
// Compiled in its own translation unit so DocReaders.cpp stays fast and low-memory on MSVC and Linux GCC.
#include "DocReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include "formats/common/AiModelDispatch.h"
#ifdef _WIN32
#include <wincrypt.h>
#include <bcrypt.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Explorer2 {

#include "formats/common/GpsTrackMap.h"
#include "formats/common/DocFormatCommon.h"
#include "formats/data/Parquet.h"
#include "formats/data/Avro.h"
#include "formats/data/Bson.h"
#include "formats/data/WekaArff.h"
#include "formats/data/MatrixMarket.h"
#include "formats/data/NdJson.h"
#include "formats/data/HttpArchiveHar.h"
#include "formats/data/BitTorrent.h"
#include "formats/data/BioMedicalEdf.h"
#include "formats/ai/NumPyNpy.h"
#include "formats/ai/NumPyNpz.h"
#include "formats/ai/JupyterIpynb.h"
#include "formats/data/CborDoc.h"
#include "formats/data/MessagePackDoc.h"
#include "formats/ai/PyTorchCheckpoint.h"
#include "formats/ai/TfLiteModel.h"
#include "formats/data/ApacheArrowIpc.h"
#include "formats/data/FeatherTable.h"
#include "formats/data/ApacheOrc.h"
#include "formats/data/Hdf5Data.h"
#include "formats/data/MatlabMat.h"
#include "formats/data/SpssSav.h"
#include "formats/data/StataDta.h"
#include "formats/data/Sas7bdat.h"
#include "formats/data/RSerializedRds.h"
#include "formats/data/DuckDbDatabase.h"
#include "formats/gis/GarminFit.h"
#include "formats/data/NetCdfData.h"
#include "formats/gis/WmoGrib.h"
#include "formats/data/NiftiVolume.h"

namespace DocReaders {

#if defined(_MSC_VER)
#define E2_DOCSCI_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_DOCSCI_NOINLINE __attribute__((noinline))
#else
#define E2_DOCSCI_NOINLINE
#endif

namespace {

E2_DOCSCI_NOINLINE bool DispatchAnalyticsAndAi(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".avro")    return Formats::ParseApacheAvro(raw, out);
    if (extLower == L".bson")    return Formats::ParseBsonDocument(raw, out);
    if (extLower == L".arff")    return Formats::ParseWekaArffDoc(raw, out);
    if (extLower == L".mtx")     return Formats::ParseMatrixMarketDoc(raw, out);
    if (extLower == L".ndjson" || extLower == L".jsonl") return Formats::ParseNdJsonDoc(raw, out);
    if (extLower == L".har")     return Formats::ParseHttpArchiveHarDoc(raw, out);
    if (extLower == L".torrent") return Formats::ParseBitTorrentDoc(raw, out);
    if (extLower == L".edf")     return Formats::ParseBioMedicalEdfDoc(raw, out);

    if (extLower == L".npy" || (raw.size() >= 10 && std::memcmp(raw.data(), "\x93NUMPY", 6) == 0)) {
        return Formats::ParseNumPyNpy(raw, out);
    }
    if (extLower == L".npz") return Formats::ParseNumPyNpz(raw, out);
    if (extLower == L".ipynb") return Formats::ParseJupyterIpynb(raw, out);
    if (extLower == L".cbor") return Formats::ParseCborDoc(raw, out);
    if (extLower == L".msgpack" || extLower == L".mpk") return Formats::ParseMessagePackDoc(raw, extLower, out);
    if (extLower == L".onnx" || extLower == L".gguf" || extLower == L".safetensors") {
        return ::Explorer2::AiModels::ParseAiModelMemoryToDocument(raw.data(), raw.size(), extLower, out);
    }
    if (extLower == L".pt" || extLower == L".pth" || extLower == L".ckpt") return Formats::ParsePyTorchCheckpoint(raw, extLower, out);
    if (extLower == L".tflite") return Formats::ParseTfLiteModel(raw, out);
    if (extLower == L".feather") return Formats::ParseFeatherTable(raw, extLower, out);
    if (extLower == L".arrow" || extLower == L".ipc") return Formats::ParseApacheArrowIpc(raw, extLower, out);
    if (extLower == L".orc") return Formats::ParseApacheOrc(raw, out);
    return false;
}

E2_DOCSCI_NOINLINE bool DispatchStatsAndSciGrids(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".h5" || extLower == L".hdf5") return Formats::ParseHdf5Doc(raw, extLower, out);
    if (extLower == L".mat") return Formats::ParseMatlabMat(raw, out);
    if (extLower == L".sav" || extLower == L".zsav") return Formats::ParseSpssSav(raw, out);
    if (extLower == L".dta") return Formats::ParseStataDta(raw, out);
    if (extLower == L".sas7bdat") return Formats::ParseSas7bdat(raw, out);
    if (extLower == L".rds") return Formats::ParseRSerializedRds(raw, out);
    if (extLower == L".duckdb") return Formats::ParseDuckDbDatabase(raw, out);

    if (extLower == L".fit" || (raw.size() >= 14 && (raw[0] == 12 || raw[0] == 14) && std::memcmp(raw.data() + 8, ".FIT", 4) == 0)) {
        return Formats::ParseGarminFitDoc(raw, out);
    }
    if (extLower == L".nc" || extLower == L".cdf" ||
        (raw.size() >= 16 && (std::memcmp(raw.data(), "CDF\x01", 4) == 0 ||
                              std::memcmp(raw.data(), "CDF\x02", 4) == 0 ||
                              std::memcmp(raw.data(), "CDF\x05", 4) == 0)))
    {
        return Formats::ParseNetCdfData(raw, extLower, out);
    }
    if (extLower == L".grib" || extLower == L".grib2" || extLower == L".grb" ||
        (raw.size() >= 16 && std::memcmp(raw.data(), "GRIB", 4) == 0 && (raw[7] == 1 || raw[7] == 2)))
    {
        return Formats::ParseWmoGribDoc(raw, out);
    }
    if (extLower == L".nii" || extLower == L".nii.gz") {
        return Formats::ParseNiftiVolumeDoc(raw, out);
    }
    return false;
}

} // namespace

bool DispatchDocDirectColumnarFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& outDoc, bool& outMatched) {
    outMatched = true;
    if (extLower == L".parquet") return Formats::ParseApacheParquetFile(fullPath, outDoc);
    if (extLower == L".feather") return Formats::ParseFeatherTableFile(fullPath, extLower, outDoc);
    if (extLower == L".arrow" || extLower == L".ipc") return Formats::ParseApacheArrowIpcFile(fullPath, extLower, outDoc);
    outMatched = false;
    return false;
}

bool DispatchDocSciAiAndData(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& outDoc) {
    return DispatchAnalyticsAndAi(raw, extLower, outDoc) || DispatchStatsAndSciGrids(raw, extLower, outDoc);
}

} // namespace DocReaders
} // namespace Explorer2
