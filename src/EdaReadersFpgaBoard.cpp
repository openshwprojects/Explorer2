// EdaReadersFpgaBoard.cpp — Modular EE, EDA, FPGA, VLSI, Automotive & Waveform Readers
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

#include "formats/eda/TektronixHexFirmware.h"
#include "formats/eda/VerilogReadmemh.h"
#include "formats/eda/XilinxBlockMemBmm.h"
#include "formats/eda/IceStormPcf.h"
#include "formats/eda/LatticeDiamondLpf.h"
#include "formats/eda/XilinxConstraintsUcf.h"
#include "formats/eda/XschemSchematic.h"
#include "formats/eda/FidoCadJCircuit.h"
#include "formats/eda/KiCadWorksheet.h"
#include "formats/eda/KiCadDesignRules.h"
#include "formats/eda/GenCadPcb.h"
#include "formats/waveforms/VerilogEvcdWave.h"
#include "formats/eda/AsamA2lCalibration.h"
#include "formats/eda/BerkeleyKiss2Fsm.h"

namespace EdaReaders {

bool IsSiliconOrDiagEdaExtension(const std::wstring& extLower);
bool TryParseSiliconOrDiagEda(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard);

namespace {
constexpr LoaderDispatch::EdaEntry kFpgaBoardEdaTable[] = {
    { L".tek",       Formats::ParseTektronixHexFirmware },
    { L".mem",       Formats::ParseVerilogReadmemh      },
    { L".bmm",       Formats::ParseXilinxBlockMemBmm    },
    { L".pcf",       Formats::ParseIceStormPcf          },
    { L".lpf",       Formats::ParseLatticeDiamondLpf    },
    { L".ucf",       Formats::ParseXilinxConstraintsUcf },
    { L".xsch",      Formats::ParseXschemSchematic      },
    { L".fcd",       Formats::ParseFidoCadJCircuit      },
    { L".kicad_wks", Formats::ParseKiCadWorksheet       },
    { L".kicad_dru", Formats::ParseKiCadDesignRules     },
    { L".gencad",    Formats::ParseGenCadPcb            },
    { L".evcd",      Formats::ParseVerilogEvcdWave      },
    { L".a2l",       Formats::ParseAsamA2lCalibration   },
    { L".kiss2",     Formats::ParseBerkeleyKiss2Fsm     },
};
} // namespace

bool IsFpgaOrBoardEdaExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasEdaExt(kFpgaBoardEdaTable, extLower) ||
           IsSiliconOrDiagEdaExtension(extLower);
}

bool TryParseFpgaOrBoardEda(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchEda(kFpgaBoardEdaTable, extLower, data, size, fullPath, outBoard, matched);
    return matched ? ok : TryParseSiliconOrDiagEda(extLower, data, size, fullPath, outBoard);
}

} // namespace EdaReaders

} // namespace Explorer2
