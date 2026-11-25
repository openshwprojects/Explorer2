// EdaReadersVlsiWave.cpp — Modular Electronics, PCB, VLSI, FPGA, Automotive & Waveform Readers
// Compiled in its own translation unit so EdaReaders.cpp stays fast to build on both MSVC and 32-bit Linux GCC.
#include "EdaReaders.h"
#include "FormatSupport.h"
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
#include "formats/common/FormatLoaderDispatch.h"
#include "formats/common/EdaFormatCommon.h"
#include "formats/common/FirmwareMemoryMapEda.h"

#include "formats/eda/TiTxtFirmware.h"
#include "formats/eda/XilinxMemoryCoe.h"
#include "formats/eda/QuartusMemoryMif.h"
#include "formats/eda/MentorPadsAscii.h"
#include "formats/eda/ProtelAsciiPcb.h"
#include "formats/eda/CadstarArchiveCpa.h"
#include "formats/eda/MagicVlsiMag.h"
#include "formats/eda/BerkeleyEspressoPla.h"
#include "formats/eda/IscasBenchNetlist.h"
#include "formats/eda/KayakCanKcd.h"
#include "formats/waveforms/SocketCanLog.h"
#include "formats/eda/AgilentCitifile.h"
#include "formats/waveforms/IeeeComtradeCff.h"
#include "formats/waveforms/GtkWaveLxt2.h"
#include "formats/eda/Ieee1481Spef.h"
#include "formats/eda/Ieee1801Upf.h"

namespace EdaReaders {

bool IsFpgaOrBoardEdaExtension(const std::wstring& extLower);
bool TryParseFpgaOrBoardEda(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard);

namespace {
constexpr LoaderDispatch::EdaEntry kVlsiWaveEdaTable[] = {
    { L".titxt",   Formats::ParseTiTxtFirmware       },
    { L".coe",     Formats::ParseXilinxMemoryCoe     },
    { L".mif",     Formats::ParseQuartusMemoryMif    },
    { L".pads",    Formats::ParseMentorPadsAscii     },
    { L".protel",  Formats::ParseProtelAsciiPcb      },
    { L".cpa",     Formats::ParseCadstarArchiveCpa   },
    { L".mag",     Formats::ParseMagicVlsiMag        },
    { L".pla",     Formats::ParseBerkeleyEspressoPla },
    { L".bench",   Formats::ParseIscasBenchNetlist   },
    { L".kcd",     Formats::ParseKayakCanKcd         },
    { L".candump", Formats::ParseSocketCanLog        },
    { L".cti",     Formats::ParseAgilentCitifile     },
    { L".cff",     Formats::ParseIeeeComtradeCff     },
    { L".lxt2",    Formats::ParseGtkWaveLxt2         },
    { L".spef",    Formats::ParseIeee1481Spef        },
    { L".upf",     Formats::ParseIeee1801Upf         },
};
} // namespace

bool IsModularEdaExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasEdaExt(kVlsiWaveEdaTable, extLower) ||
           IsFpgaOrBoardEdaExtension(extLower);
}

bool TryParseModularEda(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchEda(kVlsiWaveEdaTable, extLower, data, size, fullPath, outBoard, matched);
    return matched ? ok : TryParseFpgaOrBoardEda(extLower, data, size, fullPath, outBoard);
}

} // namespace EdaReaders

} // namespace Explorer2
