// EdaReadersSiliconDiag.cpp — Modular EE, EDA, FPGA, VLSI, Automotive & Waveform Readers
#include "EdaReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
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
#include "formats/common/FormatLoaderDispatch.h"
#include "formats/common/EdaFormatCommon.h"
#include "formats/common/FirmwareMemoryMapEda.h"
#include "formats/common/FpgaPinConstraintsCommon.h"

#include "formats/eda/MicrochipLiberoPdc.h"
#include "formats/eda/GowinFpgaCst.h"
#include "formats/eda/JtagStaplJam.h"
#include "formats/eda/PcbRndLihata.h"
#include "formats/eda/IpcNc349DrillXnc.h"
#include "formats/eda/IcLvsCdlNetlist.h"
#include "formats/eda/SynopsysItfStackup.h"
#include "formats/eda/SynopsysSaifPower.h"
#include "formats/eda/IbisBoardDescEbd.h"
#include "formats/eda/AsamOdxDiag.h"
#include "formats/eda/PeakPcanSym.h"
#include "formats/waveforms/OpenLogicSnifferOls.h"
#include "formats/waveforms/GtkWaveVztWave.h"
#include "formats/waveforms/SigMfRfCapture.h"

namespace EdaReaders {

bool IsCircuitBenchEdaExtension(const std::wstring& extLower);
bool TryParseCircuitBenchEda(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard);

namespace {
constexpr LoaderDispatch::EdaEntry kSiliconDiagEdaTable[] = {
    { L".pdc",        Formats::ParseMicrochipLiberoPdc  },
    { L".cst",        Formats::ParseGowinFpgaCst        },
    { L".jam",        Formats::ParseJtagStaplJam        },
    { L".lht",        Formats::ParsePcbRndLihata        },
    { L".xnc",        Formats::ParseIpcNc349DrillXnc    },
    { L".cdl",        Formats::ParseIcLvsCdlNetlist     },
    { L".itf",        Formats::ParseSynopsysItfStackup  },
    { L".saif",       Formats::ParseSynopsysSaifPower   },
    { L".ebd",        Formats::ParseIbisBoardDescEbd    },
    { L".odx",        Formats::ParseAsamOdxDiag         },
    { L".sym",        Formats::ParsePeakPcanSym         },
    { L".ols",        Formats::ParseOpenLogicSnifferOls },
    { L".vzt",        Formats::ParseGtkWaveVztWave      },
    { L".sigmf-meta", Formats::ParseSigMfRfCapture      },
};
} // namespace

bool IsSiliconOrDiagEdaExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasEdaExt(kSiliconDiagEdaTable, extLower) ||
           IsCircuitBenchEdaExtension(extLower);
}

bool TryParseSiliconOrDiagEda(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchEda(kSiliconDiagEdaTable, extLower, data, size, fullPath, outBoard, matched);
    return matched ? ok : TryParseCircuitBenchEda(extLower, data, size, fullPath, outBoard);
}

} // namespace EdaReaders

} // namespace Explorer2
