#pragma once
// AutodeskInventor3D.h — Autodesk Inventor 3D Part & Assembly (.ipt, .iam) OLE2 reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseAutodeskInventor3D(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    int assemblyDepth = 0)
{
    using namespace CadPkgDetail;
    if (assemblyDepth > 4) return false;
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf)) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    bool isInventor = (extLower == L".ipt" || extLower == L".iam");
    std::wstring dir = fullPath.empty() ? std::wstring() : PathUtils::GetParentPath(fullPath);

    for (const auto& entry : cf.entries) {
        if (entry.objectType != 2 || entry.size == 0) continue;
        std::vector<uint8_t> rawStream;
        if (!Ole2::ExtractStream(cf, entry, rawStream, 64 * 1024 * 1024) || rawStream.empty()) continue;

        std::wstring lname = PathUtils::ToLower(entry.name);

        // Inventor Assembly (.iam) ComponentOccurrences stream:
        // ("IAMOCCS\0", uint32 version=1, uint32 count, [uint16 pathLen, UTF-16LE relPath, double mat4x4[16]] * count)
        if (extLower == L".iam" && rawStream.size() >= 16 && std::memcmp(rawStream.data(), "IAMOCCS\0", 8) == 0 && !dir.empty()) {
            uint32_t occVer = RdU32(rawStream.data() + 8);
            uint32_t occCnt = RdU32(rawStream.data() + 12);
            size_t pos = 16;
            if (occVer == 1 && occCnt > 0 && occCnt <= 2048) {
                for (uint32_t k = 0; k < occCnt; ++k) {
                    if (pos + 2 > rawStream.size()) break;
                    uint16_t wLen = RdU16(rawStream.data() + pos); pos += 2;
                    if (wLen == 0 || wLen > 512 || pos + static_cast<size_t>(wLen) * 2u + 128u > rawStream.size()) break;
                    std::wstring compRel;
                    compRel.reserve(wLen);
                    for (uint16_t c = 0; c < wLen; ++c) {
                        uint16_t ch = RdU16(rawStream.data() + pos + c * 2u);
                        if (ch != 0) compRel.push_back(static_cast<wchar_t>(ch));
                    }
                    pos += static_cast<size_t>(wLen) * 2u;
                    double mat[16] = {};
                    for (int m = 0; m < 16; ++m) mat[m] = RdF64(rawStream.data() + pos + m * 8u);
                    pos += 128u;

                    std::wstring compPath = dir + L"\\" + PathUtils::GetFileName(compRel);
                    std::vector<uint8_t> compBytes;
                    if (PathUtils::ReadFileBytes(compPath, 64 * 1024 * 1024, compBytes) && !compBytes.empty()) {
                        LoadedMesh3D compMesh;
                        std::wstring cExt = PathUtils::ToLower(PathUtils::GetExtension(compPath));
                        if (ParseAutodeskInventor3D(compBytes.data(), compBytes.size(), cExt, compPath, compMesh, assemblyDepth + 1)) {
                            AppendSubMeshMat4(compMesh, mat, mesh);
                        }
                    }
                }
            }
            continue;
        }

        // Try direct uncompressed stream decode first, then zlib-compressed RSe / Solid Edge segment
        if (TryDecodeCadPayloadBuffer(rawStream.data(), rawStream.size(), mesh)) {
            continue;
        }
        std::vector<uint8_t> inflated;
        if (TryInflateCadStream(rawStream, inflated)) {
            TryDecodeCadPayloadBuffer(inflated.data(), inflated.size(), mesh);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    if (extLower == L".ipt")      mesh.formatName = L"Autodesk Inventor 3D Part (.ipt)";
    else if (extLower == L".iam") mesh.formatName = L"Autodesk Inventor 3D Assembly (.iam)";
    else if (extLower == L".psm") mesh.formatName = L"Siemens Solid Edge Sheet Metal (.psm)";
    else                          mesh.formatName = isInventor ? L"Autodesk Inventor CAD" : L"Siemens Solid Edge Part (.par)";
    return true;
}

} // namespace Formats
