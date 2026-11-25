// DocReadersAnalyticsForensics.cpp — Modular Data, Finance, GIS, Pro-Audio & Forensics Readers
#include "DocReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "Ole2Compound.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
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
#include "formats/common/GpsTrackMap.h"
#define E2_DOC_FORMAT_CORE_ONLY
#include "formats/common/DocFormatCommon.h"
#undef E2_DOC_FORMAT_CORE_ONLY

#include "formats/data/ApacheIonBinary.h"
#include "formats/data/UbjsonDoc.h"
#include "formats/data/MozillaLz4Json.h"
#include "formats/data/QuickenQifFinance.h"
#include "formats/data/OpenFinancialOfx.h"
#include "formats/gis/MapboxVectorTileMvt.h"
#include "formats/gis/WellKnownGeometryWkt.h"
#include "formats/audio/CoreAudioCaf.h"
#include "formats/audio/WavPackWvAudio.h"
#include "formats/security/AppleBinaryCookies.h"
#include "formats/security/WindowsJumpListDest.h"

namespace DocReaders {

namespace {
constexpr LoaderDispatch::DocEntry kAnalyticsForensicsDocTable[] = {
    { L".ion",                      Formats::ParseApacheIonDoc          },
    { L".10n",                      Formats::ParseApacheIonDoc          },
    { L".ubj",                      Formats::ParseUbjsonDoc             },
    { L".ubjson",                   Formats::ParseUbjsonDoc             },
    { L".jsonlz4",                  Formats::ParseMozillaLz4Json        },
    { L".mozlz4",                   Formats::ParseMozillaLz4Json        },
    { L".baklz4",                   Formats::ParseMozillaLz4Json        },
    { L".qif",                      Formats::ParseQuickenQifFinance     },
    { L".ofx",                      Formats::ParseOpenFinancialOfx      },
    { L".qfx",                      Formats::ParseOpenFinancialOfx      },
    { L".mvt",                      Formats::ParseMapboxVectorTileMvt   },
    { L".wkt",                      Formats::ParseWellKnownGeometryDoc  },
    { L".wkb",                      Formats::ParseWellKnownGeometryDoc  },
    { L".caf",                      Formats::ParseCoreAudioCafDoc       },
    { L".wv",                       Formats::ParseWavPackWvAudio        },
    { L".binarycookies",            Formats::ParseAppleBinaryCookies    },
    { L".automaticdestinations-ms", Formats::ParseWindowsJumpListDest   },
};
} // namespace

bool IsAnalyticsOrForensicsDocExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasDocExt(kAnalyticsForensicsDocTable, extLower);
}

bool TryParseAnalyticsOrForensicsDoc(
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    ParsedOfficeDocument& outDoc)
{
    (void)fullPath;
    bool matched = false;
    return LoaderDispatch::DispatchDoc(kAnalyticsForensicsDocTable, raw, extLower, outDoc, matched);
}

} // namespace DocReaders

} // namespace Explorer2
