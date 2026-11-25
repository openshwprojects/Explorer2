#pragma once
// The 3D model readers behind Model3DViewer (src/formats/*.h, dispatched by extension in Mesh3DReaders.cpp),
// without the viewer. No Windows headers: they also build on Linux (see linux/).
#include "Mesh3DTypes.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace Explorer2::MeshReaders {

// Extensions the 3D view opens (lower case, with the dot)
bool Is3DModelExtension(const std::wstring& extLower);
// Whether a .dxf holds 3D geometry, and whether a .shp / .geojson / GeoTIFF holds a 3D surface (they open in other
// views otherwise)
bool IsLikely3DDxfFile(const std::wstring& fullPath);
bool IsLikely3DGeoOrDemFile(const std::wstring& fullPath, const std::wstring& extLower);
bool IsLikelyQuakeMapFile(const std::wstring& fullPath);

// Reads a model. A load that is not the newest (activeGen != generation; activeGen may be null) stops early.
LoadedMesh3D ParseMeshFromDisk(const std::wstring& fullPath, uint64_t generation, const std::atomic<uint64_t>* activeGen);
bool InspectMeshFile(const std::wstring& fullPath, LoadedMesh3D& outMesh);

bool IsModular3DModelExtension(const std::wstring& extLower);
bool TryParseModularMesh(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh);

// Domain dispatchers compiled in dedicated translation units (Mesh3DReadersCadBrep / Mesh3DReadersRiggedEngine)
bool CheckLikely3DDxfHeader(const uint8_t* data, size_t size);
bool DispatchMeshCadAndBrep(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh,
    bool& outMatched);
bool DispatchMeshRiggedAndEngine(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh,
    bool& outMatched);

} // namespace Explorer2::MeshReaders
