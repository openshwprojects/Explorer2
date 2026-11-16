// FormatTestsBatch70.cpp — Dedicated Stage 15 verification suite for the 70-format roadmap
#include "SelfTestCommon.h"
#include "Types.h"
#include "PathUtils.h"
#include "ReaderGuard.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include "Model3DViewer.h"
#include "ImageFormatLoader.h"
#include "EdaViewer.h"
#include "SpreadsheetDocViewer.h"
#include "PdfViewer.h"
#include "ArchiveExplorer.h"
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

uint64_t HashMeshGeometry(const LoadedMesh3D& m) {
    uint64_t h = 14695981039346656037ULL;
    auto mixBytes = [&](const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= static_cast<uint64_t>(b[i]);
            h *= 1099511628211ULL;
        }
    };
    mixBytes(&m.rawVertexCount, sizeof(m.rawVertexCount));
    mixBytes(&m.rawTriangleCount, sizeof(m.rawTriangleCount));
    for (const auto& v : m.vertices) {
        int32_t qx = static_cast<int32_t>(std::lround(v.x * 10000.0f));
        int32_t qy = static_cast<int32_t>(std::lround(v.y * 10000.0f));
        int32_t qz = static_cast<int32_t>(std::lround(v.z * 10000.0f));
        mixBytes(&qx, 4); mixBytes(&qy, 4); mixBytes(&qz, 4);
    }
    for (const auto& t : m.tris) {
        mixBytes(&t.i0, 4); mixBytes(&t.i1, 4); mixBytes(&t.i2, 4);
    }
    return h;
}

uint64_t HashBgraBytes(const std::vector<uint8_t>& bytes) {
    uint64_t h = 14695981039346656037ULL;
    for (uint8_t b : bytes) {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ULL;
    }
    return h;
}

void PushU16LE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void PushU32LE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

} // namespace

void RunFormatTests3DBatch70(TestCounter& tc) {
    const std::wstring workDir = ScratchTestDir() + L"\\fmt3d_b70";
    CreateDirectoryW(workDir.c_str(), nullptr);
    std::vector<std::wstring> createdFiles;

    auto writeBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = workDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdFiles.push_back(p);
        return p;
    };
    auto writeText = [&](const wchar_t* name, const char* text) -> std::wstring {
        std::wstring p = workDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, text, std::strlen(text));
        createdFiles.push_back(p);
        return p;
    };

    const std::wstring tdDir = TestDataDir();

    #include "FormatTests3D_Batch70.h"

    for (const auto& f : createdFiles) {
        DeleteFileW(f.c_str());
    }
    RemoveDirectoryW(workDir.c_str());
}

void RunFormatTestsImagesBatch70(TestCounter& tc) {
    #include "FormatTestsImages_Batch70.h"
}

void RunFormatTestsEdaBatch70(TestCounter& tc) {
    #include "FormatTestsEda_Batch70.h"
}

void RunFormatTestsDocsBatch70(TestCounter& tc) {
    #include "FormatTestsDocs_Batch70.h"
}

void RunFormatTestsArchivesBatch70(TestCounter& tc) {
    #include "FormatTestsArchives_Batch70.h"
}

} // namespace Explorer2::SelfTestSuite
