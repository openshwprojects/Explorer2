// Mesh3DReadersEngineeringGame.cpp — Modular CAD, CAE/CFD/FEM, Voxel & Classic Game 3D Readers
#include "Mesh3DReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "StreamCodecs.h"
#include "ByteIO.h"
#include "ReaderTextUtils.h"

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
#include "formats/common/VoxelMesh3D.h"

#include "formats/cad/AutodeskDwfCad.h"
#include "formats/cad/AdditiveCliSlice.h"
#include "formats/common/FemSolidSurfaceCommon.h"
#include "formats/3d/AbaqusInpMesh.h"
#include "formats/3d/AnsysCdbMesh.h"
#include "formats/3d/NastranBulkNas.h"
#include "formats/3d/AnsysFluentCasMesh.h"
#include "formats/3d/XPlaneObj8Mesh.h"
#include "formats/3d/MinetestSchematicMts.h"
#include "formats/3d/MinecraftSchematic.h"
#include "formats/3d/PlayStationTmdMesh.h"
#include "formats/3d/WestwoodW3dMesh.h"
#include "formats/3d/HaloJmsMesh.h"

namespace MeshReaders {

namespace {
constexpr LoaderDispatch::MeshEntry kEngineeringGameMeshTable[] = {
    { L".dwf",       Formats::ParseAutodeskDwfCad       },
    { L".dwfx",      Formats::ParseAutodeskDwfCad       },
    { L".cli",       Formats::ParseAdditiveCliSlice     },
    { L".inp",       Formats::ParseAbaqusInpMesh        },
    { L".cdb",       Formats::ParseAnsysCdbMesh         },
    { L".nas",       Formats::ParseNastranBulkNas       },
    { L".cas",       Formats::ParseAnsysFluentCasMesh   },
    { L".obj8",      Formats::ParseXPlaneObj8Mesh       },
    { L".mts",       Formats::ParseMinetestSchematicMts },
    { L".schem",     Formats::ParseMinecraftSchematic   },
    { L".schematic", Formats::ParseMinecraftSchematic   },
    { L".nbt",       Formats::ParseMinecraftSchematic   },
    { L".tmd",       Formats::ParsePlayStationTmdMesh   },
    { L".w3d",       Formats::ParseWestwoodW3dMesh      },
    { L".jms",       Formats::ParseHaloJmsMesh          },
};
} // namespace

bool IsEngineeringOrGame3DExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasMeshExt(kEngineeringGameMeshTable, extLower);
}

bool TryParseEngineeringOrGameMesh(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh)
{
    bool matched = false;
    return LoaderDispatch::DispatchMesh(kEngineeringGameMeshTable, extLower, data, size, fullPath, outMesh, matched);
}

} // namespace MeshReaders

} // namespace Explorer2
