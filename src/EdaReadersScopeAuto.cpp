// EdaReadersScopeAuto.cpp — Digital/Analog Oscilloscopes, Logic Analyzers, Automotive CAN/LIN/AUTOSAR & Fieldbus Readers
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
#include "formats/common/WaveformPlotCommon.h"

#include "formats/waveforms/VerilogVcd.h"
#include "formats/waveforms/VectorCanBlf.h"
#include "formats/eda/VectorCanDbc.h"
#include "formats/waveforms/VectorCanAsc.h"
#include "formats/waveforms/GtkWaveFst.h"
#include "formats/waveforms/GhdlGhw.h"
#include "formats/waveforms/SigrokSessionSr.h"
#include "formats/waveforms/SaleaeLogicSal.h"
#include "formats/waveforms/TektronixWfm.h"
#include "formats/waveforms/RigolWaveformWfm.h"
#include "formats/waveforms/TektronixIsf.h"
#include "formats/waveforms/LeCroyWaveform.h"
#include "formats/waveforms/NiTdmsWaveform.h"
#include "formats/waveforms/AsamMdf.h"
#include "formats/eda/LinBusLdf.h"
#include "formats/eda/AutosarArxml.h"
#include "formats/eda/CanOpenEds.h"
#include "formats/eda/ProfinetGsdml.h"
#include "formats/eda/RockwellPlcL5x.h"

namespace EdaReaders {

#if defined(_MSC_VER)
#define E2_SCOPE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_SCOPE_NOINLINE __attribute__((noinline))
#else
#define E2_SCOPE_NOINLINE
#endif

namespace {

inline void ResetBoardPath(ParsedEdaBoard& outBoard, const std::wstring& fullPath) {
    outBoard = ParsedEdaBoard{};
    outBoard.filePath = fullPath;
}

E2_SCOPE_NOINLINE bool DispatchLogicSimAndScopeWaveforms(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled)
{
    outMatched = true;
    outStrictHandled = true;
    if (ext == L".vcd") return Formats::ParseVerilogVcdWaveforms(sv, outBoard);
    if (ext == L".tdms") return Formats::ParseNiTdmsBoard(data, size, outBoard);
    if (ext == L".fst") return Formats::ParseGtkWaveFstBoard(data, size, outBoard);
    if (ext == L".ghw") return Formats::ParseGhdlGhwBoard(data, size, outBoard);
    if (ext == L".trc") return Formats::ParseLeCroyWaveformBoard(data, size, outBoard);

    outStrictHandled = false;
    if (ext == L".sr") return Formats::ParseSigrokSessionSrBoard(data, size, ext, outBoard);
    if (ext == L".sal") return Formats::ParseSaleaeLogicSalBoard(data, size, ext, outBoard);
    if (ext == L".wfm") {
        if (Formats::ParseTektronixWfmBoard(data, size, outBoard)) return true;
        ResetBoardPath(outBoard, fullPath);
        if (Formats::ParseRigolWaveformWfmBoard(data, size, ext, outBoard)) return true;
        ResetBoardPath(outBoard, fullPath);
        return Formats::ParseTektronixIsfBoard(data, size, outBoard);
    }
    if (ext == L".isf") {
        if (Formats::ParseTektronixWfmBoard(data, size, outBoard)) return true;
        ResetBoardPath(outBoard, fullPath);
        return Formats::ParseTektronixIsfBoard(data, size, outBoard);
    }
    if (ext == L".mf4" || ext == L".mdf") return Formats::ParseAsamMdfWaveform(data, size, ext, outBoard);
    outMatched = false;
    return false;
}

E2_SCOPE_NOINLINE bool DispatchAutomotiveAndFieldbus(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled)
{
    outMatched = true;
    outStrictHandled = true;
    if (ext == L".blf") return Formats::ParseVectorCanBlfBoard(data, size, ext, outBoard);
    if (ext == L".dbc") {
        if (Formats::ParseVectorCanDbcBoard(data, size, ext, outBoard)) return true;
        ResetBoardPath(outBoard, fullPath);
        return Formats::ParseVectorCanBlfBoard(data, size, ext, outBoard);
    }
    if (ext == L".can") return Formats::ParseVectorCanBlfBoard(data, size, ext, outBoard) ||
                               Formats::ParseVectorCanDbcBoard(data, size, ext, outBoard) ||
                               Formats::ParseVectorCanAscBoard(data, size, ext, outBoard);

    outStrictHandled = false;
    if (ext == L".ldf") return Formats::ParseLinBusLdfBoard(sv, outBoard);
    if (ext == L".arxml") return Formats::ParseAutosarArxmlBoard(sv, outBoard);
    if (ext == L".eds" || ext == L".dcf") return Formats::ParseCanOpenEdsBoard(sv, ext, outBoard);
    if (ext == L".gsdml") return Formats::ParseProfinetGsdmlBoard(sv, outBoard);
    if (ext == L".l5x") return Formats::ParseRockwellPlcL5xBoard(sv, outBoard);
    outMatched = false;
    return false;
}

} // namespace

bool DispatchEdaScopeAndAutomotive(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled)
{
    bool ok = DispatchLogicSimAndScopeWaveforms(extLower, data, size, sv, fullPath, outBoard, outMatched, outStrictHandled);
    if (outMatched) return ok;
    return DispatchAutomotiveAndFieldbus(extLower, data, size, sv, fullPath, outBoard, outMatched, outStrictHandled);
}

bool DispatchEdaCanAscFallback(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    return Formats::ParseVectorCanBlfBoard(data, size, extLower, outBoard) ||
           Formats::ParseVectorCanAscBoard(data, size, extLower, outBoard);
}

} // namespace EdaReaders
} // namespace Explorer2
