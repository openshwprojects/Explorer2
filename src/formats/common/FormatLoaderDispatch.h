#pragma once
// FormatLoaderDispatch.h — Declarative, single-source-of-truth dispatch tables for modular
// domain reader translation units (3D Mesh, EDA/Waveform, Image, Archive, and Doc loaders).
// Included inside `namespace Explorer2` (after standard headers); contains no `#include` directives.

struct LoadedMesh3D;
struct ParsedEdaBoard;
struct ExtendedImageInfo;
struct ArchiveSummaryInfo;
struct ParsedOfficeDocument;

namespace LoaderDispatch {

using SniffBytesFn = bool (*)(const uint8_t* data, size_t size);

// ---------------------------------------------------------------------------
// 3D Mesh Loader Table
// ---------------------------------------------------------------------------
struct MeshEntry {
    using SimpleFn = bool (*)(const uint8_t*, size_t, LoadedMesh3D&);
    using PathFn   = bool (*)(const uint8_t*, size_t, const std::wstring&, LoadedMesh3D&);

    const wchar_t* ext = L"";
    SimpleFn simpleFn  = nullptr;
    PathFn   pathFn    = nullptr;

    constexpr MeshEntry(const wchar_t* e, SimpleFn f) noexcept : ext(e), simpleFn(f) {}
    constexpr MeshEntry(const wchar_t* e, PathFn f) noexcept   : ext(e), pathFn(f) {}
};

template <size_t N>
inline bool HasMeshExt(const MeshEntry (&table)[N], std::wstring_view extLower) noexcept {
    for (size_t i = 0; i < N; ++i) {
        if (extLower == table[i].ext) return true;
    }
    return false;
}

template <size_t N>
inline bool DispatchMesh(
    const MeshEntry (&table)[N],
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh,
    bool& outMatched)
{
    for (size_t i = 0; i < N; ++i) {
        if (extLower == table[i].ext) {
            outMatched = true;
            return table[i].pathFn ? table[i].pathFn(data, size, fullPath, outMesh)
                                   : table[i].simpleFn(data, size, outMesh);
        }
    }
    outMatched = false;
    return false;
}

// ---------------------------------------------------------------------------
// EDA / FPGA / Waveform Loader Table
// ---------------------------------------------------------------------------
struct EdaEntry {
    using SimpleFn = bool (*)(const uint8_t*, size_t, ParsedEdaBoard&);
    using PathFn   = bool (*)(const uint8_t*, size_t, const std::wstring&, ParsedEdaBoard&);

    const wchar_t* ext = L"";
    SimpleFn simpleFn  = nullptr;
    PathFn   pathFn    = nullptr;

    constexpr EdaEntry(const wchar_t* e, SimpleFn f) noexcept : ext(e), simpleFn(f) {}
    constexpr EdaEntry(const wchar_t* e, PathFn f) noexcept   : ext(e), pathFn(f) {}
};

template <size_t N>
inline bool HasEdaExt(const EdaEntry (&table)[N], std::wstring_view extLower) noexcept {
    for (size_t i = 0; i < N; ++i) {
        if (extLower == table[i].ext) return true;
    }
    return false;
}

template <size_t N>
inline bool DispatchEda(
    const EdaEntry (&table)[N],
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched)
{
    for (size_t i = 0; i < N; ++i) {
        if (extLower == table[i].ext) {
            outMatched = true;
            return table[i].pathFn ? table[i].pathFn(data, size, fullPath, outBoard)
                                   : table[i].simpleFn(data, size, outBoard);
        }
    }
    outMatched = false;
    return false;
}

// ---------------------------------------------------------------------------
// Extended Image / Console Texture Loader Table
// ---------------------------------------------------------------------------
struct ImageEntry {
    using SimpleFn = bool (*)(const uint8_t*, size_t, ExtendedImageInfo&);
    using MaxDimFn = bool (*)(const uint8_t*, size_t, uint32_t, ExtendedImageInfo&);

    const wchar_t* ext = L"";
    SniffBytesFn sniff = nullptr;
    SimpleFn simpleFn  = nullptr;
    MaxDimFn maxDimFn  = nullptr;

    constexpr ImageEntry(const wchar_t* e, SimpleFn f, SniffBytesFn s = nullptr) noexcept
        : ext(e), sniff(s), simpleFn(f) {}
    constexpr ImageEntry(const wchar_t* e, MaxDimFn f, SniffBytesFn s = nullptr) noexcept
        : ext(e), sniff(s), maxDimFn(f) {}
};

template <size_t N>
inline bool HasImageExt(const ImageEntry (&table)[N], std::wstring_view extLower) noexcept {
    for (size_t i = 0; i < N; ++i) {
        if (extLower == table[i].ext) return true;
    }
    return false;
}

template <size_t N>
inline bool DispatchImage(
    const ImageEntry (&table)[N],
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo,
    bool& outMatched)
{
    for (size_t i = 0; i < N; ++i) {
        const auto& e = table[i];
        if (extLower == e.ext || (e.sniff && e.sniff(data, size))) {
            outMatched = true;
            return e.maxDimFn ? e.maxDimFn(data, size, maxDimension, outInfo)
                              : e.simpleFn(data, size, outInfo);
        }
    }
    outMatched = false;
    return false;
}

// ---------------------------------------------------------------------------
// Archive Inspect + Extract Unified Loader Table
// ---------------------------------------------------------------------------
struct ArchiveEntry {
    using InspectSimpleFn = bool (*)(const uint8_t*, size_t, ArchiveSummaryInfo&);
    using InspectExtFn    = bool (*)(const uint8_t*, size_t, const std::wstring&, ArchiveSummaryInfo&);
    using ExtractSimpleFn = bool (*)(const uint8_t*, size_t, const std::wstring&, std::vector<uint8_t>&);
    using ExtractExtFn    = bool (*)(const uint8_t*, size_t, const std::wstring&, const std::wstring&, std::vector<uint8_t>&);

    const wchar_t*  ext           = L"";
    SniffBytesFn    sniff         = nullptr;
    InspectSimpleFn inspectSimple = nullptr;
    InspectExtFn    inspectExt    = nullptr;
    ExtractSimpleFn extractSimple = nullptr;
    ExtractExtFn    extractExt    = nullptr;

    constexpr ArchiveEntry(
        const wchar_t* e,
        InspectSimpleFn ins,
        ExtractSimpleFn extFn,
        SniffBytesFn s = nullptr) noexcept
        : ext(e), sniff(s), inspectSimple(ins), extractSimple(extFn) {}

    constexpr ArchiveEntry(
        const wchar_t* e,
        InspectExtFn ins,
        ExtractExtFn extFn,
        SniffBytesFn s = nullptr) noexcept
        : ext(e), sniff(s), inspectExt(ins), extractExt(extFn) {}
};

template <size_t N>
inline bool HasArchiveExt(const ArchiveEntry (&table)[N], std::wstring_view extLower) noexcept {
    for (size_t i = 0; i < N; ++i) {
        if (extLower == table[i].ext) return true;
    }
    return false;
}

template <size_t N>
inline bool DispatchArchiveInspect(
    const ArchiveEntry (&table)[N],
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo,
    bool& outMatched)
{
    for (size_t i = 0; i < N; ++i) {
        const auto& e = table[i];
        if (extLower == e.ext || (e.sniff && e.sniff(data, size))) {
            outMatched = true;
            return e.inspectExt ? e.inspectExt(data, size, extLower, outInfo)
                                : e.inspectSimple(data, size, outInfo);
        }
    }
    outMatched = false;
    return false;
}

template <size_t N>
inline bool DispatchArchiveExtract(
    const ArchiveEntry (&table)[N],
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes,
    bool& outMatched)
{
    for (size_t i = 0; i < N; ++i) {
        const auto& e = table[i];
        if (extLower == e.ext || (e.sniff && e.sniff(data, size))) {
            outMatched = true;
            return e.extractExt ? e.extractExt(data, size, extLower, normTarget, outBytes)
                                : e.extractSimple(data, size, normTarget, outBytes);
        }
    }
    outMatched = false;
    return false;
}

// ---------------------------------------------------------------------------
// Spreadsheet / Structured Document Loader Table
// ---------------------------------------------------------------------------
struct DocEntry {
    using SimpleFn = bool (*)(const std::vector<uint8_t>&, ParsedOfficeDocument&);
    using ExtFn    = bool (*)(const std::vector<uint8_t>&, const std::wstring&, ParsedOfficeDocument&);

    const wchar_t* ext = L"";
    SimpleFn simpleFn  = nullptr;
    ExtFn    extFn     = nullptr;

    constexpr DocEntry(const wchar_t* e, SimpleFn f) noexcept : ext(e), simpleFn(f) {}
    constexpr DocEntry(const wchar_t* e, ExtFn f) noexcept    : ext(e), extFn(f) {}
};

template <size_t N>
inline bool HasDocExt(const DocEntry (&table)[N], std::wstring_view extLower) noexcept {
    for (size_t i = 0; i < N; ++i) {
        if (extLower == table[i].ext) return true;
    }
    return false;
}

template <size_t N>
inline bool DispatchDoc(
    const DocEntry (&table)[N],
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    ParsedOfficeDocument& outDoc,
    bool& outMatched)
{
    for (size_t i = 0; i < N; ++i) {
        const auto& e = table[i];
        if (extLower == e.ext) {
            outMatched = true;
            return e.extFn ? e.extFn(raw, extLower, outDoc)
                           : e.simpleFn(raw, outDoc);
        }
    }
    outMatched = false;
    return false;
}

} // namespace LoaderDispatch
