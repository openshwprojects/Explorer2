// EdaReadersCircuitBench.cpp — Modular Circuit Schematic, IC Mask, VLSI Placement, Byte-Code & Scope/LA Readers
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
#include "formats/common/WaveformPlotCommon.h"

#include "formats/eda/QspiceSchematicQsch.h"
#include "formats/eda/EagleDesignRulesDru.h"
#include "formats/eda/MicrowindMaskMsk.h"
#include "formats/eda/BerkeleyEqnLogic.h"
#include "formats/eda/AlteraJamByteCodeJbc.h"
#include "formats/eda/IspdBookshelfPlc.h"
#include "formats/waveforms/YokogawaWaveformWvf.h"
#include "formats/waveforms/DreamSourceLabDsl.h"

namespace EdaReaders {

namespace {
constexpr LoaderDispatch::EdaEntry kCircuitBenchEdaTable[] = {
    { L".qsch",  Formats::ParseQspiceSchematicQsch },
    { L".dru",   Formats::ParseEagleDesignRulesDru },
    { L".msk",   Formats::ParseMicrowindMaskMsk    },
    { L".eqn",   Formats::ParseBerkeleyEqnLogic    },
    { L".jbc",   Formats::ParseAlteraJamByteCodeJbc},
    { L".scl",   Formats::ParseIspdBookshelfPlc    },
    { L".nodes", Formats::ParseIspdBookshelfPlc    },
    { L".wvf",   Formats::ParseYokogawaWaveformWvf },
    { L".dsl",   Formats::ParseDreamSourceLabDsl   },
};
} // namespace

bool IsCircuitBenchEdaExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasEdaExt(kCircuitBenchEdaTable, extLower);
}

bool TryParseCircuitBenchEda(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard)
{
    bool matched = false;
    return LoaderDispatch::DispatchEda(kCircuitBenchEdaTable, extLower, data, size, fullPath, outBoard, matched);
}

} // namespace EdaReaders

} // namespace Explorer2
