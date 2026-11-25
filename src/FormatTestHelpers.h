#pragma once
// FormatTestHelpers.h — Shared deterministic content hashing, scratch-file RAII,
// binary builders (delegating to TestBytes.h), and clean-rejection assertions for Stage 15 format suites.
#include "SelfTestCommon.h"
#include "TestBytes.h"
#include "Types.h"
#include "PathUtils.h"
#include "ReaderGuard.h"
#include "Model3DViewer.h"
#include "ImageFormatLoader.h"
#include "EdaViewer.h"
#include "SpreadsheetDocViewer.h"
#include "ArchiveExplorer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Explorer2::SelfTestSuite::FormatTestUtil {

inline uint64_t HashMeshGeometry(const LoadedMesh3D& m) {
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

inline uint64_t HashBgraBytes(const std::vector<uint8_t>& bytes) {
    uint64_t h = 14695981039346656037ULL;
    for (uint8_t b : bytes) {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ULL;
    }
    return h;
}

inline uint64_t HashEdaBoard(const ParsedEdaBoard& b) {
    uint64_t h = 14695981039346656037ULL;
    auto mix32 = [&](int32_t v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        for (int i = 0; i < 4; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    };
    for (wchar_t c : b.formatTitle) mix32(static_cast<int32_t>(c));
    for (const auto& t : b.traces) {
        mix32(static_cast<int32_t>(std::lround(t.x1 * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(t.y1 * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(t.x2 * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(t.y2 * 1000.0f)));
    }
    for (const auto& p : b.pads) {
        mix32(static_cast<int32_t>(std::lround(p.x * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(p.y * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(p.w * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(p.h * 1000.0f)));
    }
    for (const auto& c : b.components) {
        for (wchar_t ch : c.refDes) mix32(static_cast<int32_t>(ch));
        mix32(static_cast<int32_t>(std::lround(c.x * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(c.y * 1000.0f)));
    }
    for (const auto& cr : b.circles) {
        mix32(static_cast<int32_t>(std::lround(cr.cx * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(cr.cy * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(cr.radius * 1000.0f)));
    }
    return h;
}

inline uint64_t HashDocContent(const ParsedOfficeDocument& d) {
    uint64_t h = 14695981039346656037ULL;
    auto mixW = [&](const std::wstring& s) {
        for (wchar_t c : s) { h ^= static_cast<uint64_t>(c); h *= 1099511628211ULL; }
    };
    mixW(d.formatTitle);
    for (const auto& sh : d.sheets) {
        mixW(sh.name);
        for (const auto& r : sh.rows) {
            for (const auto& cell : r) mixW(cell);
        }
    }
    for (const auto& p : d.paragraphs) mixW(p.text);
    return h;
}

inline uint64_t HashArchiveToc(const ArchiveSummaryInfo& a) {
    uint64_t h = 14695981039346656037ULL;
    auto mixW = [&](const std::wstring& s) {
        for (wchar_t c : s) { h ^= static_cast<uint64_t>(c); h *= 1099511628211ULL; }
    };
    mixW(a.formatName);
    for (const auto& e : a.entries) {
        mixW(e.pathInArchive);
        for (int i = 0; i < 8; ++i) {
            h ^= static_cast<uint64_t>((e.uncompressedSize >> (i * 8)) & 0xFFu);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

inline void PushU16LE(std::vector<uint8_t>& b, uint16_t v) { TestBytes::AppendU16LE(b, v); }
inline void PushI16LE(std::vector<uint8_t>& b, int16_t v)  { TestBytes::AppendU16LE(b, static_cast<uint16_t>(v)); }
inline void PushU32LE(std::vector<uint8_t>& b, uint32_t v) { TestBytes::AppendU32LE(b, v); }
inline void PushI32LE(std::vector<uint8_t>& b, int32_t v)  { TestBytes::AppendI32LE(b, v); }
inline void PushU64LE(std::vector<uint8_t>& b, uint64_t v) { TestBytes::AppendU64LE(b, v); }
inline void PushU16BE(std::vector<uint8_t>& b, uint16_t v) { TestBytes::AppendU16BE(b, v); }
inline void PushU32BE(std::vector<uint8_t>& b, uint32_t v) { TestBytes::AppendU32BE(b, v); }
inline void PushU64BE(std::vector<uint8_t>& b, uint64_t v) { TestBytes::AppendU64BE(b, v); }
inline void PushF32LE(std::vector<uint8_t>& b, float f)    { TestBytes::AppendF32LE(b, f); }
inline void PushF32BE(std::vector<uint8_t>& b, float f)    { TestBytes::AppendF32BE(b, f); }
inline void PushF64LE(std::vector<uint8_t>& b, double d)   { TestBytes::AppendF64LE(b, d); }
inline void PushF64BE(std::vector<uint8_t>& b, double d)   { TestBytes::AppendF64BE(b, d); }

inline void PushFixedAscii(std::vector<uint8_t>& b, const char* s, size_t fieldLen) {
    const size_t n = s ? std::min(std::strlen(s), fieldLen > 0 ? fieldLen - 1 : 0) : 0;
    const size_t base = b.size();
    b.insert(b.end(), fieldLen, 0u);
    if (n > 0) std::memcpy(b.data() + base, s, n);
}

struct ScratchScope {
    std::wstring dir;
    std::vector<std::wstring> files;

    explicit ScratchScope(const wchar_t* leafName) : dir(ScratchTestDir() + L"\\" + leafName) {
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    ~ScratchScope() {
        for (const auto& f : files) DeleteFileW(f.c_str());
        RemoveDirectoryW(dir.c_str());
    }
    std::wstring WriteBytes(const wchar_t* name, const std::vector<uint8_t>& bytes) {
        std::wstring p = dir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        files.push_back(p);
        return p;
    }
    std::wstring WriteText(const wchar_t* name, const char* text) {
        std::wstring p = dir + L"\\" + name;
        PathUtils::WriteFileBytes(p, text, std::strlen(text));
        files.push_back(p);
        return p;
    }
};

inline void AssertMeshRejected(TestCounter& tc, const wchar_t* testName, const std::wstring& path) {
    const uint64_t guardBefore = ReaderGuard::StoppedCount();
    LoadedMesh3D bad;
    const bool ok = Model3DViewer::InspectMeshFile(path, bad);
    const bool noEscaped = (ReaderGuard::StoppedCount() == guardBefore);
    tc.Record(!ok && !bad.loadedOk && bad.tris.empty() && noEscaped, testName,
              !noEscaped ? L"reader threw escaped C++ exception" : (ok ? L"unexpectedly accepted corrupt 3D input" : L"rejected cleanly"));
}

inline void AssertImgRejected(TestCounter& tc, const wchar_t* testName, const std::vector<uint8_t>& badBytes, const wchar_t* ext) {
    const uint64_t guardBefore = ReaderGuard::StoppedCount();
    ExtendedImageInfo badInfo;
    const bool ok = ImageFormatLoader::DecodeImageMemory(badBytes.data(), badBytes.size(), ext, 0, badInfo);
    const bool noEscaped = (ReaderGuard::StoppedCount() == guardBefore);
    tc.Record(!ok && !badInfo.valid && badInfo.bgraPixels.empty() && noEscaped, testName,
              !noEscaped ? L"reader threw escaped C++ exception" : (ok ? L"unexpectedly decoded corrupt image input" : L"rejected cleanly"));
}

inline void AssertEdaRejected(TestCounter& tc, const wchar_t* testName, const std::wstring& path) {
    const uint64_t guardBefore = ReaderGuard::StoppedCount();
    ParsedEdaBoard badBrd;
    const bool ok = EdaViewer::InspectEdaFile(path, badBrd);
    const bool noEscaped = (ReaderGuard::StoppedCount() == guardBefore);
    tc.Record(!ok && !badBrd.valid && badBrd.traces.empty() && badBrd.pads.empty() &&
              badBrd.vias.empty() && badBrd.circles.empty() && badBrd.components.empty() && noEscaped,
              testName, !noEscaped ? L"reader threw escaped C++ exception" : (ok ? L"unexpectedly accepted corrupt EDA input" : L"rejected cleanly"));
}

inline void AssertDocRejected(TestCounter& tc, const wchar_t* testName, const std::wstring& path) {
    const uint64_t guardBefore = ReaderGuard::StoppedCount();
    ParsedOfficeDocument badDoc;
    const bool ok = SpreadsheetDocViewer::InspectOfficeFile(path, badDoc);
    const bool noEscaped = (ReaderGuard::StoppedCount() == guardBefore);
    tc.Record(!ok && !badDoc.valid && badDoc.sheets.empty() && badDoc.paragraphs.empty() && noEscaped, testName,
              !noEscaped ? L"reader threw escaped C++ exception" : (ok ? L"unexpectedly accepted corrupt document" : L"rejected cleanly"));
}

inline void AssertArcRejected(TestCounter& tc, const wchar_t* testName, const std::wstring& path) {
    const uint64_t guardBefore = ReaderGuard::StoppedCount();
    ArchiveSummaryInfo badArc;
    const bool ok = ArchiveExplorer::InspectArchiveFile(path, badArc);
    const bool noEscaped = (ReaderGuard::StoppedCount() == guardBefore);
    tc.Record(!ok && !badArc.valid && badArc.entries.empty() && noEscaped, testName,
              !noEscaped ? L"reader threw escaped C++ exception" : (ok ? L"unexpectedly accepted corrupt archive" : L"rejected cleanly"));
}

} // namespace Explorer2::SelfTestSuite::FormatTestUtil
