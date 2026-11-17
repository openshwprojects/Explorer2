#pragma once
// The 3D model readers behind Model3DViewer (src/formats/*.h, dispatched by extension in Mesh3DReaders.cpp),
// without the viewer. No Windows headers: they also build on Linux (see linux/).
#include "Mesh3DTypes.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace Explorer2::MeshReaders {

// Extensions the 3D view opens (lower case, with the dot)
bool Is3DModelExtension(const std::wstring& extLower);
// Whether a .dxf holds 3D geometry, and whether a .shp / .geojson / GeoTIFF holds a 3D surface (they open in other
// views otherwise)
bool IsLikely3DDxfFile(const std::wstring& fullPath);
bool IsLikely3DGeoOrDemFile(const std::wstring& fullPath, const std::wstring& extLower);

// Reads a model. A load that is not the newest (activeGen != generation; activeGen may be null) stops early.
LoadedMesh3D ParseMeshFromDisk(const std::wstring& fullPath, uint64_t generation, const std::atomic<uint64_t>* activeGen);
bool InspectMeshFile(const std::wstring& fullPath, LoadedMesh3D& outMesh);

} // namespace Explorer2::MeshReaders
