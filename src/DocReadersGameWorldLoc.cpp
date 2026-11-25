// DocReadersGameWorldLoc.cpp — Modular Modding Tables, Localization, Tilemap & Tool Readers
#include "DocReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Base64.h"
#include "ByteIO.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Explorer2 {

#include "formats/common/FormatPrologueCommon.h"
#include "formats/common/FormatLoaderDispatch.h"
#include "formats/common/BioWareTableCommon.h"

#include "formats/docs/UnrealLocresDoc.h"
#include "formats/docs/SimsStblTable.h"
#include "formats/docs/NintendoJSystemBcsv.h"
#include "formats/docs/ScsSiiUnitDoc.h"
#include "formats/docs/TiledTmxMapDoc.h"
#include "formats/docs/LdtkWorldDoc.h"
#include "formats/docs/AngelCodeBmfontDoc.h"

namespace DocReaders {

bool IsAnalyticsOrForensicsDocExtension(const std::wstring& extLower);
bool TryParseAnalyticsOrForensicsDoc(
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    ParsedOfficeDocument& outDoc);

namespace {
constexpr LoaderDispatch::DocEntry kGameWorldLocDocTable[] = {
    { L".locres", Formats::ParseUnrealLocresDoc     },
    { L".stbl",   Formats::ParseSimsStblTable       },
    { L".bcsv",   Formats::ParseNintendoJSystemBcsv },
    { L".sii",    Formats::ParseScsSiiUnitDoc       },
    { L".tmx",    Formats::ParseTiledTmxMapDoc      },
    { L".ldtk",   Formats::ParseLdtkWorldDoc        },
    { L".bmf",    Formats::ParseAngelCodeBmfontDoc  },
};
} // namespace

bool IsGameWorldOrLocDocExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasDocExt(kGameWorldLocDocTable, extLower) ||
           IsAnalyticsOrForensicsDocExtension(extLower);
}

bool TryParseGameWorldOrLocDoc(
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    ParsedOfficeDocument& outDoc)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchDoc(kGameWorldLocDocTable, raw, extLower, outDoc, matched);
    return matched ? ok : TryParseAnalyticsOrForensicsDoc(raw, extLower, fullPath, outDoc);
}

} // namespace DocReaders

} // namespace Explorer2
