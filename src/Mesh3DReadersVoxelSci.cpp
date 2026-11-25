// Mesh3DReadersVoxelSci.cpp — Modular 3D, Game Mesh, Voxel & Scientific Mesh Readers
// Compiled in its own translation unit so Mesh3DReaders.cpp stays fast to build on both MSVC and 32-bit Linux GCC.
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
#include "formats/common/VoxelMesh3D.h"
#include "formats/common/AcclaimSkelCommon.h"

#include "formats/3d/ValveSourceVvd.h"
#include "formats/3d/RavenGhoul2Glm.h"
#include "formats/3d/UnrealVertexMesh3d.h"
#include "formats/3d/MotionAnalysisHtr.h"
#include "formats/3d/AcclaimAsf.h"
#include "formats/3d/AcclaimAmc.h"
#include "formats/3d/TetGenSmesh.h"
#include "formats/3d/PovRayMesh.h"
#include "formats/3d/RenderManRib.h"
#include "formats/3d/RadianceSceneRad.h"
#include "formats/3d/QubicleVoxQb.h"
#include "formats/3d/KenSilvermanKvx.h"
#include "formats/3d/VoxlapKv6.h"
#include "formats/3d/WestwoodVxl.h"
#include "formats/3d/TrueSpaceCob.h"
#include "formats/3d/NeutralFileNff.h"
#include "formats/3d/SurfaceEvolverFe.h"
#include "formats/3d/MovieByuMesh.h"
#include "formats/3d/Cart3dTriMesh.h"
#include "formats/3d/IrrlichtMesh.h"
#include "formats/3d/SegaNinjaNj.h"

namespace MeshReaders {

bool IsSkeletalScene3DExtension(const std::wstring& extLower);
bool TryParseSkeletalSceneMesh(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh);

namespace {
constexpr LoaderDispatch::MeshEntry kVoxelSciMeshTable[] = {
    { L".vvd",     Formats::ParseValveSourceVvd     },
    { L".glm",     Formats::ParseRavenGhoul2Glm     },
    { L".3d",      Formats::ParseUnrealVertexMesh3d },
    { L".htr",     Formats::ParseMotionAnalysisHtr  },
    { L".asf",     Formats::ParseAcclaimAsf         },
    { L".amc",     Formats::ParseAcclaimAmc         },
    { L".smesh",   Formats::ParseTetGenSmesh        },
    { L".pov",     Formats::ParsePovRayMesh         },
    { L".rib",     Formats::ParseRenderManRib       },
    { L".rad",     Formats::ParseRadianceSceneRad   },
    { L".qb",      Formats::ParseQubicleVoxQb       },
    { L".kvx",     Formats::ParseKenSilvermanKvx    },
    { L".kv6",     Formats::ParseVoxlapKv6          },
    { L".vxl",     Formats::ParseWestwoodVxl        },
    { L".cob",     Formats::ParseTrueSpaceCob       },
    { L".nff",     Formats::ParseNeutralFileNff     },
    { L".fe",      Formats::ParseSurfaceEvolverFe   },
    { L".byu",     Formats::ParseMovieByuMesh       },
    { L".tri",     Formats::ParseCart3dTriMesh      },
    { L".triq",    Formats::ParseCart3dTriMesh      },
    { L".irrmesh", Formats::ParseIrrlichtMesh       },
    { L".nj",      Formats::ParseSegaNinjaNj        },
};
} // namespace

bool IsModular3DModelExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasMeshExt(kVoxelSciMeshTable, extLower) ||
           IsSkeletalScene3DExtension(extLower);
}

bool TryParseModularMesh(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchMesh(kVoxelSciMeshTable, extLower, data, size, fullPath, outMesh, matched);
    return matched ? ok : TryParseSkeletalSceneMesh(extLower, data, size, fullPath, outMesh);
}

} // namespace MeshReaders

} // namespace Explorer2
