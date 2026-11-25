// Mesh3DReadersSkeletalScene.cpp — Skeletal Character & Scene Graph 3D Readers
#include "Mesh3DReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"

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
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Explorer2 {

#include "formats/common/FormatLoaderDispatch.h"
#include "formats/common/MeshBuild3D.h"
#include "formats/common/UberToolsSkelCommon.h"

#include "formats/3d/UberToolsSkb.h"
#include "formats/3d/UberToolsSka.h"
#include "formats/3d/MohaaSkd.h"
#include "formats/3d/MohaaSkc.h"
#include "formats/3d/RavenGhoul2Gla.h"
#include "formats/3d/RtcwMdc.h"
#include "formats/3d/Heretic2Fm.h"
#include "formats/3d/ValveSourcePhy.h"
#include "formats/3d/ValveSourceVtx.h"
#include "formats/3d/Model3DM3d.h"
#include "formats/3d/ImagineTddd.h"
#include "formats/3d/CelestiaCmod.h"
#include "formats/3d/SoftimageXsi.h"
#include "formats/3d/Descent3Oof.h"
#include "formats/3d/BohemiaMlodP3d.h"
#include "formats/3d/DarkPlacesDpm.h"

namespace MeshReaders {

bool IsConsoleOrGame3DExtension(const std::wstring& extLower);
bool TryParseConsoleOrGameMesh(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh);

namespace {
constexpr LoaderDispatch::MeshEntry kSkeletalSceneMeshTable[] = {
    { L".skb",  Formats::ParseUberToolsSkb    },
    { L".ska",  Formats::ParseUberToolsSka    },
    { L".skd",  Formats::ParseMohaaSkd        },
    { L".skc",  Formats::ParseMohaaSkc        },
    { L".gla",  Formats::ParseRavenGhoul2Gla  },
    { L".mdc",  Formats::ParseRtcwMdc         },
    { L".fm",   Formats::ParseHeretic2Fm      },
    { L".phy",  Formats::ParseValveSourcePhy  },
    { L".vtx",  Formats::ParseValveSourceVtx  },
    { L".m3d",  Formats::ParseModel3DM3d      },
    { L".tddd", Formats::ParseImagineTddd     },
    { L".iob",  Formats::ParseImagineTddd     },
    { L".cmod", Formats::ParseCelestiaCmod    },
    { L".xsi",  Formats::ParseSoftimageXsi    },
    { L".oof",  Formats::ParseDescent3Oof     },
    { L".p3d",  Formats::ParseBohemiaMlodP3d  },
    { L".dpm",  Formats::ParseDarkPlacesDpm   },
};
} // namespace

bool IsSkeletalScene3DExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasMeshExt(kSkeletalSceneMeshTable, extLower) ||
           IsConsoleOrGame3DExtension(extLower);
}

bool TryParseSkeletalSceneMesh(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchMesh(kSkeletalSceneMeshTable, extLower, data, size, fullPath, outMesh, matched);
    return matched ? ok : TryParseConsoleOrGameMesh(extLower, data, size, fullPath, outMesh);
}

} // namespace MeshReaders

} // namespace Explorer2
