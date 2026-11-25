// EdaReadersAsicBitstream.cpp — ASIC/VLSI Physical Design, Synthesis Netlists, FPGA Bitstreams, Constraints & Firmware Readers
// Compiled in its own translation unit to keep peak compiler memory low and parallel builds fast on MSVC and Linux GCC.
#include "EdaReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include "ByteIO.h"

#ifndef _WIN32
#include <dirent.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace Explorer2 {

#include "formats/common/FormatPrologueCommon.h"
#include "formats/common/EdaFormatCommon.h"
#include "formats/common/FirmwareMemoryMapEda.h"
#include "formats/common/FpgaPinConstraintsCommon.h"
#include "formats/common/AlteraBitstreamCommon.h"
#include "formats/common/TclTimingPinCommon.h"
#include "formats/common/WaveformPlotCommon.h"

#include "formats/eda/AsicLef.h"
#include "formats/eda/AsicDef.h"
#include "formats/eda/GdsiiStream.h"
#include "formats/eda/SemiOasis.h"
#include "formats/eda/JtagBsdl.h"
#include "formats/eda/CmsisSvd.h"
#include "formats/eda/EdifNetlist.h"
#include "formats/eda/BlifNetlist.h"
#include "formats/eda/AigerNetlist.h"
#include "formats/eda/SynopsysLibertyLib.h"
#include "formats/eda/Ieee1497Sdf.h"
#include "formats/eda/XilinxFpgaBit.h"
#include "formats/eda/LatticeFpgaBit.h"
#include "formats/eda/AlteraRpdBitstream.h"
#include "formats/eda/Stm32Dfu.h"
#include "formats/eda/VesaEdid.h"
#include "formats/eda/JedecSpd.h"
#include "formats/eda/DeviceTreeDtb.h"
#include "formats/eda/XilinxConstraintsXdc.h"
#include "formats/eda/SynopsysSdc.h"
#include "formats/eda/QuartusQsf.h"
#include "formats/eda/JedecPldJed.h"
#include "formats/eda/JtagSvf.h"
#include "formats/eda/XilinxXsvf.h"
#include "formats/eda/IntelFpgaSof.h"
#include "formats/eda/IntelFpgaPof.h"

namespace EdaReaders {

#if defined(_MSC_VER)
#define E2_ASIC_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_ASIC_NOINLINE __attribute__((noinline))
#else
#define E2_ASIC_NOINLINE
#endif

namespace {

inline void ResetBoardPath(ParsedEdaBoard& outBoard, const std::wstring& fullPath) {
    outBoard = ParsedEdaBoard{};
    outBoard.filePath = fullPath;
}

E2_ASIC_NOINLINE bool DispatchAsicPhysicalAndSynthesis(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled)
{
    outMatched = true;
    outStrictHandled = true;
    if (ext == L".lef") return Formats::ParseAsicLefBoard(sv, outBoard);
    if (ext == L".def") return Formats::ParseAsicDefBoard(sv, outBoard);
    if (ext == L".gds" || ext == L".gds2") return Formats::ParseGdsiiStreamBoard(data, size, outBoard);
    if (ext == L".bsdl" || ext == L".bsd") return Formats::ParseJtagBsdlBoard(sv, outBoard);
    if (ext == L".svd") return Formats::ParseCmsisSvdBoard(sv, outBoard);
    if (ext == L".aig" || ext == L".aag") return Formats::ParseAigerNetlistBoard(data, size, outBoard);
    if (ext == L".edif" || ext == L".edn") return Formats::ParseEdifNetlistBoard(sv, outBoard);
    if (ext == L".blif" || ext == L".eblif") return Formats::ParseBlifNetlistBoard(sv, outBoard);
    if (ext == L".lib") return Formats::ParseLibertyLibBoard(sv, outBoard);
    if (ext == L".sdf") return Formats::ParseIeee1497SdfBoard(sv, outBoard);
    if (ext == L".oas") {
        outStrictHandled = false;
        return Formats::ParseSemiOasisBoard(data, size, ext, outBoard);
    }
    outMatched = false;
    return false;
}

E2_ASIC_NOINLINE bool DispatchFpgaBitstreamsAndConstraints(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled)
{
    outMatched = true;
    outStrictHandled = true;
    if (ext == L".bit" || ext == L".bin") {
        if (Formats::ParseXilinxFpgaBitBoard(data, size, ext, outBoard)) return true;
        ResetBoardPath(outBoard, fullPath);
        return Formats::ParseLatticeFpgaBitBoard(data, size, ext, outBoard);
    }
    if (ext == L".rpd") {
        if (Formats::ParseAlteraRpdBitstreamBoard(data, size, ext, outBoard)) return true;
        ResetBoardPath(outBoard, fullPath);
        if (Formats::ParseXilinxFpgaBitBoard(data, size, ext, outBoard)) return true;
        ResetBoardPath(outBoard, fullPath);
        return Formats::ParseLatticeFpgaBitBoard(data, size, ext, outBoard);
    }
    if (ext == L".dfu" || ext == L".ufd") return Formats::ParseStm32DfuBoard(data, size, ext, outBoard);
    if (ext == L".edid") return Formats::ParseVesaEdidBoard(data, size, outBoard);
    if (ext == L".spd") return Formats::ParseJedecSpdBoard(data, size, outBoard);
    if (ext == L".dtb") return Formats::ParseFlattenedDeviceTreeBoard(data, size, outBoard);

    outStrictHandled = false;
    if (ext == L".xdc") return Formats::ParseXilinxConstraintsXdcBoard(data, size, ext, outBoard);
    if (ext == L".sdc") return Formats::ParseSynopsysSdcBoard(data, size, ext, outBoard);
    if (ext == L".qsf") return Formats::ParseQuartusQsfBoard(data, size, ext, outBoard);
    if (ext == L".jed") return Formats::ParseJedecPldJedBoard(data, size, ext, outBoard);
    if (ext == L".svf") return Formats::ParseJtagSvfBoard(data, size, ext, outBoard);
    if (ext == L".xsvf") return Formats::ParseXilinxXsvfBoard(data, size, ext, outBoard);
    if (ext == L".sof") return Formats::ParseIntelFpgaSofBoard(data, size, ext, outBoard);
    if (ext == L".pof") return Formats::ParseIntelFpgaPofBoard(data, size, ext, outBoard);
    outMatched = false;
    return false;
}

} // namespace

bool CheckLikelyAsicDefHeader(const uint8_t* data, size_t size) {
    return Formats::IsLikelyAsicDefBytes(data, size);
}

bool CheckLikelyLibertyHeader(const uint8_t* data, size_t size) {
    return Formats::IsLikelyLibertyLibBytes(data, size);
}

bool CheckLikelyIeeeSdfHeader(const uint8_t* data, size_t size) {
    return Formats::IsLikelyIeeeSdfBytes(data, size);
}

bool DispatchEdaAsicAndBitstream(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled)
{
    bool ok = DispatchAsicPhysicalAndSynthesis(extLower, data, size, sv, outBoard, outMatched, outStrictHandled);
    if (outMatched) return ok;
    return DispatchFpgaBitstreamsAndConstraints(extLower, data, size, fullPath, outBoard, outMatched, outStrictHandled);
}

} // namespace EdaReaders
} // namespace Explorer2
