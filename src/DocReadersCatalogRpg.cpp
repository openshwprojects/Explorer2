// DocReadersCatalogRpg.cpp — Modular Modding Tables, Localization & Embedded Pack Readers
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
#include "formats/common/GettextCatalogCommon.h"
#include "formats/docs/BioWare2daTable.h"
#include "formats/docs/BioWareTlkTable.h"
#include "formats/docs/BioWareGffStruct.h"
#include "formats/docs/GettextMoCatalog.h"
#include "formats/docs/GettextPoCatalog.h"
#include "formats/docs/ArmCmsisPdscDoc.h"

namespace DocReaders {

bool IsGameWorldOrLocDocExtension(const std::wstring& extLower);
bool TryParseGameWorldOrLocDoc(
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    ParsedOfficeDocument& outDoc);

namespace {
constexpr LoaderDispatch::DocEntry kCatalogRpgDocTable[] = {
    { L".2da",  Formats::ParseBioWare2daTable  },
    { L".tlk",  Formats::ParseBioWareTlkTable  },
    { L".gff",  Formats::ParseBioWareGffStruct },
    { L".mo",   Formats::ParseGettextMoCatalog },
    { L".po",   Formats::ParseGettextPoCatalog },
    { L".pot",  Formats::ParseGettextPoCatalog },
    { L".pdsc", Formats::ParseArmCmsisPdscDoc  },
};
} // namespace

bool IsCatalogOrRpgDocExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasDocExt(kCatalogRpgDocTable, extLower) ||
           IsGameWorldOrLocDocExtension(extLower);
}

bool TryParseCatalogOrRpgDoc(
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    ParsedOfficeDocument& outDoc)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchDoc(kCatalogRpgDocTable, raw, extLower, outDoc, matched);
    return matched ? ok : TryParseGameWorldOrLocDoc(raw, extLower, fullPath, outDoc);
}

} // namespace DocReaders

} // namespace Explorer2
