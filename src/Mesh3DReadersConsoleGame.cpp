// Mesh3DReadersConsoleGame.cpp — Modular 3D Modding, Console & Voxel Readers
#include "Mesh3DReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "StreamCodecs.h"
#include "ByteIO.h"

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

#include "formats/common/FormatPrologueCommon.h"
#include "formats/common/FormatLoaderDispatch.h"
#include "formats/common/MeshBuild3D.h"
#include "formats/common/NintendoGxMeshCommon.h"
#include "formats/common/VoxelSurfaceMeshCommon.h"

#include "formats/3d/InterQuakeIqe.h"
#include "formats/3d/ValveSourceVta.h"
#include "formats/3d/NintendoJ3dModel.h"
#include "formats/3d/NintendoNw4rMdl0.h"
#include "formats/3d/NintendoDsNsbmd.h"
#include "formats/3d/Warzone2100Pie.h"
#include "formats/3d/SpringRtsS3o.h"
#include "formats/3d/LucasArtsSith3do.h"
#include "formats/3d/UnrealBrushT3d.h"
#include "formats/3d/OpenGexMesh.h"
#include "formats/3d/BlizzardM3Mesh.h"
#include "formats/3d/RadGrannyGr2.h"
#include "formats/3d/GoxelGoxModel.h"
#include "formats/3d/BinvoxGrid3d.h"

namespace MeshReaders {

bool IsEngineeringOrGame3DExtension(const std::wstring& extLower);
bool TryParseEngineeringOrGameMesh(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh);

namespace {
constexpr LoaderDispatch::MeshEntry kConsoleGameMeshTable[] = {
    { L".iqe",    Formats::ParseInterQuakeIqe     },
    { L".vta",    Formats::ParseValveSourceVta    },
    { L".bmd",    Formats::ParseNintendoJ3dModel  },
    { L".bdl",    Formats::ParseNintendoJ3dModel  },
    { L".mdl0",   Formats::ParseNintendoNw4rMdl0  },
    { L".nsbmd",  Formats::ParseNintendoDsNsbmd   },
    { L".pie",    Formats::ParseWarzone2100Pie    },
    { L".s3o",    Formats::ParseSpringRtsS3o      },
    { L".3do",    Formats::ParseLucasArtsSith3do  },
    { L".t3d",    Formats::ParseUnrealBrushT3d    },
    { L".ogex",   Formats::ParseOpenGexMesh       },
    { L".m3",     Formats::ParseBlizzardM3Mesh    },
    { L".gr2",     Formats::ParseRadGrannyGr2     },
    { L".gox",    Formats::ParseGoxelGoxModel     },
    { L".binvox", Formats::ParseBinvoxGrid3d      },
};
} // namespace

bool IsConsoleOrGame3DExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasMeshExt(kConsoleGameMeshTable, extLower) ||
           IsEngineeringOrGame3DExtension(extLower);
}

bool TryParseConsoleOrGameMesh(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh)
{
    bool matched = false;
    if (LoaderDispatch::DispatchMesh(kConsoleGameMeshTable, extLower, data, size, fullPath, outMesh, matched)) {
        return true;
    }
    if (matched) return false;
    return TryParseEngineeringOrGameMesh(extLower, data, size, fullPath, outMesh);
}

} // namespace MeshReaders

} // namespace Explorer2
