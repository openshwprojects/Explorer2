#pragma once
// ReaderInspect.h - what `--inspect-files` does with one file: every reader that claims the file's extension
// runs on it, and a line says what it made of the file. Shared by the Windows harness (SelfTestSuite.cpp) and the
// Linux runner (linux/ReaderCli.cpp), which differ only in how they catch crashes and hangs (and PDF, Windows only).
#include "ArchiveReaders.h"
#include "DocReaders.h"
#include "EdaReaders.h"
#include "ImageFormatLoader.h"
#include "Mesh3DReaders.h"
#include "PathUtils.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::ReaderInspect {

// What a reader made of a file, for the "S" line of --inspect-files: the counts it found, the start of
// the content, and a hash of everything it produced. Two different inputs that give the same hash were
// not read at all: the reader made its output up.
struct InspectHash {
    uint64_t h = 14695981039346656037ULL;
    void Add(const void* data, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    }
    // Text is hashed as UTF-16 on every system, so that a file read alike on Windows and Linux gives one hash
    void Add(const std::wstring& s) {
        if constexpr (sizeof(wchar_t) == 2) {
            Add(s.data(), s.size() * sizeof(wchar_t));
        } else {
            for (const wchar_t ch : s) {
                const uint32_t c = static_cast<uint32_t>(ch);
                uint16_t units[2] = { static_cast<uint16_t>(c), 0 };
                size_t count = 1;
                if (c >= 0x10000 && c <= 0x10FFFF) {
                    units[0] = static_cast<uint16_t>(0xD800 + ((c - 0x10000) >> 10));
                    units[1] = static_cast<uint16_t>(0xDC00 + ((c - 0x10000) & 0x3FF));
                    count = 2;
                }
                Add(units, count * sizeof(uint16_t));
            }
        }
    }
    std::string Hex() const { char b[24] = {}; sprintf_s(b, "%016llx", static_cast<unsigned long long>(h)); return b; }
};

inline std::string InspectText(const std::wstring& w, size_t maxChars = 60) {
    std::wstring cut = w.substr(0, std::min(w.size(), maxChars));
    for (wchar_t& ch : cut) if (ch < 0x20) ch = L' ';
    return PathUtils::WideToUtf8(cut);
}

inline std::string DescribeImage(const ExtendedImageInfo& info) {
    InspectHash hash;
    hash.Add(info.bgraPixels.data(), info.bgraPixels.size());
    std::vector<uint32_t> colours;
    for (size_t i = 0; i + 3 < info.bgraPixels.size() && colours.size() < 4096; i += 4 * std::max<size_t>(1, info.bgraPixels.size() / 4 / 4096)) {
        uint32_t c = 0;
        std::memcpy(&c, info.bgraPixels.data() + i, 4);
        colours.push_back(c);
    }
    std::sort(colours.begin(), colours.end());
    const size_t distinct = static_cast<size_t>(std::unique(colours.begin(), colours.end()) - colours.begin());
    char b[256] = {};
    sprintf_s(b, " | file %ux%u, shown %ux%u, %zu colours in a sample, layers %zu | hash ", info.origWidth, info.origHeight,
              info.width, info.height, distinct, info.layerNames.size());
    return InspectText(info.formatName) + b + hash.Hex();
}

inline std::string DescribeMesh(const LoadedMesh3D& mesh) {
    InspectHash hash;
    hash.Add(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vec3f));
    for (const auto& t : mesh.tris) { hash.Add(&t.i0, 4); hash.Add(&t.i1, 4); hash.Add(&t.i2, 4); }
    char b[256] = {};
    sprintf_s(b, " | %zu vertices, %zu triangles | box %.4g %.4g %.4g .. %.4g %.4g %.4g | hash ", mesh.vertices.size(), mesh.tris.size(),
              mesh.bboxMin.x, mesh.bboxMin.y, mesh.bboxMin.z, mesh.bboxMax.x, mesh.bboxMax.y, mesh.bboxMax.z);
    return InspectText(mesh.formatName) + b + hash.Hex() + " | " + InspectText(mesh.statusMessage);
}

inline std::string DescribeBoard(const ParsedEdaBoard& board) {
    InspectHash hash;
    // field by field: padding bytes and the pads' string heap pointers are not data
    for (const auto& t : board.traces) {
        hash.Add(&t.x1, sizeof(float) * 5); // x1, y1, x2, y2, width
        hash.Add(&t.layer, sizeof(t.layer));
        hash.Add(&t.netId, sizeof(t.netId));
    }
    for (const auto& p : board.pads) {
        hash.Add(&p.x, sizeof(float) * 6); // x, y, w, h, rotDeg, drill
        hash.Add(&p.shape, sizeof(p.shape));
        hash.Add(&p.layer, sizeof(p.layer));
        hash.Add(p.pinName);
        hash.Add(p.netName);
    }
    for (const auto& v : board.vias) {
        hash.Add(&v.x, sizeof(float) * 4); // x, y, diameter, drill
        hash.Add(&v.netId, sizeof(v.netId));
    }
    for (const auto& l : board.labels) hash.Add(l.text);
    for (const auto& c : board.components) { hash.Add(c.refDes); hash.Add(c.value); hash.Add(&c.x, sizeof(float) * 2); }
    char b[256] = {};
    sprintf_s(b, " | %zu traces, %zu pads, %zu vias, %zu circles, %zu labels, %zu components | hash ", board.traces.size(), board.pads.size(),
              board.vias.size(), board.circles.size(), board.labels.size(), board.components.size());
    std::string first;
    if (!board.components.empty()) first = InspectText(board.components[0].refDes + L" " + board.components[0].value);
    else if (!board.labels.empty()) first = InspectText(board.labels[0].text);
    return InspectText(board.formatTitle) + b + hash.Hex() + " | " + first;
}

inline std::string DescribeDoc(const ParsedOfficeDocument& doc) {
    InspectHash hash;
    size_t rows = 0;
    for (const auto& s : doc.sheets) {
        hash.Add(s.name);
        rows += s.rows.size();
        for (const auto& r : s.rows) for (const auto& c : r) hash.Add(c);
    }
    for (const auto& p : doc.paragraphs) hash.Add(p.text);
    for (const auto& g : doc.geoPoints) { // field by field: the struct's padding and its string's heap pointer are not data
        hash.Add(&g.lat, sizeof(g.lat));
        hash.Add(&g.lon, sizeof(g.lon));
        hash.Add(&g.elevM, sizeof(g.elevM));
        hash.Add(&g.cumDistKm, sizeof(g.cumDistKm));
        hash.Add(&g.rowIndex, sizeof(g.rowIndex));
        hash.Add(g.label);
    }
    char b[256] = {};
    sprintf_s(b, " | %zu sheets, %zu rows, %zu paragraphs, %zu track points | hash ", doc.sheets.size(), rows, doc.paragraphs.size(), doc.geoPoints.size());
    std::wstring first;
    if (!doc.sheets.empty()) {
        for (size_t r = 0; r < doc.sheets[0].rows.size() && r < 3; ++r) {
            for (const auto& c : doc.sheets[0].rows[r]) first += c + L" / ";
            first += L"// ";
        }
    } else if (!doc.paragraphs.empty()) {
        first = doc.paragraphs[0].text;
    }
    return InspectText(doc.formatTitle) + b + hash.Hex() + " | " + InspectText(first, 160);
}

inline std::string DescribeArchive(const ArchiveSummaryInfo& arc) {
    InspectHash hash;
    std::wstring names;
    for (size_t i = 0; i < arc.entries.size(); ++i) {
        hash.Add(arc.entries[i].pathInArchive);
        hash.Add(&arc.entries[i].uncompressedSize, 8);
        if (i < 4) names += arc.entries[i].pathInArchive + L" (" + std::to_wstring(arc.entries[i].uncompressedSize) + L") ";
    }
    char b[128] = {};
    sprintf_s(b, " | %zu entries | hash ", arc.entries.size());
    return InspectText(arc.formatName) + b + hash.Hex() + " | " + InspectText(names, 160);
}

// The list of files to read: one UTF-8 path per line
inline bool ReadPathList(const std::wstring& listFile, std::vector<std::wstring>& paths) {
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(listFile, 64 * 1024 * 1024, raw)) return false;
    std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (!line.empty()) paths.push_back(PathUtils::Utf8ToWide(line));
        pos = eol + 1;
    }
    return true;
}

// The "S" line: what a reader made of file `i`, on one line
inline std::string SummaryLine(size_t i, const char* reader, std::string summary) {
    for (char& ch : summary) if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
    if (summary.size() > 480) summary.resize(480);
    return "S\t" + std::to_string(i) + "\t" + reader + "\t" + summary + "\n";
}

// Self-check of the harness itself (tools/fuzz_samples.py runs it first): three file names make a "reader" crash,
// throw and hang on purpose, to prove that each kind of fault is caught and reported.
inline bool IsSelfCheckFile(const std::wstring& path) { return path.find(L"__inspect_selfcheck_") != std::wstring::npos; }
inline bool RunSelfCheckReader(const std::wstring& path) {
    if (path.find(L"__inspect_selfcheck_crash") != std::wstring::npos) {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    } else if (path.find(L"__inspect_selfcheck_throw") != std::wstring::npos) {
        throw std::runtime_error("selfcheck exception");
    } else if (path.find(L"__inspect_selfcheck_hang") != std::wstring::npos) {
        for (;;) Sleep(1000);
    }
    return true;
}

// Runs one reader. The harness times it and catches what it throws; `run` says whether the reader accepted the
// file and leaves what it made of it in `summary`.
using StepFn = std::function<void(const char* reader, const std::function<bool()>& run)>;

// Every reader that claims `path` runs on it, in this order: image, mesh, board, document, `afterDocReaders`
// (the Windows harness reads PDF there), archive. `i` is the file's index in the list (dump file names).
inline void RunReaders(const std::wstring& path, size_t i, std::string& summary, const StepFn& step,
                       const std::function<void()>& afterDocReaders = {}) {
    const std::wstring ext = PathUtils::GetExtension(path);
    if (ImageFormatLoader::IsExtendedImageExtension(ext)) {
        step("image", [&]() {
            ExtendedImageInfo info;
            const bool ok = ImageFormatLoader::DecodeImageFile(path, 512, info) && info.valid;
            summary = DescribeImage(info);
            // With EXPLORER2_INSPECT_PIXELS set to a folder, the decoded picture is saved there as
            // <index>.bgra (width and height as two little-endian uint32, then the BGRA rows), so that
            // it can be compared with another decoder
            wchar_t pixelDir[MAX_PATH] = {};
            const DWORD dirLen = GetEnvironmentVariableW(L"EXPLORER2_INSPECT_PIXELS", pixelDir, MAX_PATH);
            if (ok && dirLen > 0 && dirLen < MAX_PATH) {
                std::vector<uint8_t> dump(8 + info.bgraPixels.size());
                std::memcpy(dump.data(), &info.width, 4);
                std::memcpy(dump.data() + 4, &info.height, 4);
                if (!info.bgraPixels.empty()) std::memcpy(dump.data() + 8, info.bgraPixels.data(), info.bgraPixels.size());
                PathUtils::WriteFileBytes(std::wstring(pixelDir) + L"\\" + std::to_wstring(i) + L".bgra", dump.data(), dump.size());
            }
            return ok;
        });
    }
    if (MeshReaders::Is3DModelExtension(ext)) {
        step("mesh", [&]() {
            LoadedMesh3D mesh;
            const bool ok = MeshReaders::InspectMeshFile(path, mesh) && mesh.loadedOk;
            summary = DescribeMesh(mesh);
            return ok;
        });
    }
    if (EdaReaders::IsEdaExtension(ext)) {
        step("eda", [&]() {
            ParsedEdaBoard board;
            const bool ok = EdaReaders::InspectEdaFile(path, board) && board.valid;
            summary = DescribeBoard(board);
            return ok;
        });
    }
    // A .pdb with the MSF signature is debug symbols, which the content pane shows as a document
    bool msfPdb = false;
    if (ext == L".pdb") {
        std::vector<uint8_t> hdr;
        msfPdb = PathUtils::ReadFileBytes(path, 32, hdr) && hdr.size() >= 19 && std::memcmp(hdr.data(), "Microsoft C/C++ MSF", 19) == 0;
    }
    if (DocReaders::IsSpreadsheetOrDocExtension(ext) || msfPdb) {
        step("doc", [&]() {
            ParsedOfficeDocument doc;
            const bool ok = DocReaders::InspectOfficeFile(path, doc) && doc.valid;
            summary = DescribeDoc(doc);
            // With EXPLORER2_INSPECT_DOCS set to a folder, everything the reader produced is saved there as
            // <index>.txt (UTF-8: the title, then each sheet's rows tab-separated, then the paragraphs), so that
            // it can be compared with another tool's output
            wchar_t docDir[MAX_PATH] = {};
            const DWORD docDirLen = GetEnvironmentVariableW(L"EXPLORER2_INSPECT_DOCS", docDir, MAX_PATH);
            if (docDirLen > 0 && docDirLen < MAX_PATH) {
                std::wstring text = doc.formatTitle + L"\n";
                for (const auto& s : doc.sheets) {
                    text += L"== " + s.name + L"\n";
                    for (const auto& r : s.rows) {
                        for (size_t c = 0; c < r.size(); ++c) text += (c ? L"\t" : L"") + r[c];
                        text += L"\n";
                    }
                }
                for (const auto& p : doc.paragraphs) text += p.text + L"\n";
                const std::string utf8 = PathUtils::WideToUtf8(text);
                PathUtils::WriteFileBytes(std::wstring(docDir) + L"\\" + std::to_wstring(i) + L".txt",
                                          reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size());
            }
            return ok;
        });
    }
    if (afterDocReaders) afterDocReaders();   // PDF on Windows
    if (ArchiveReaders::IsArchiveExtension(ext)) {
        step("archive", [&]() {
            ArchiveSummaryInfo arc;
            const bool listed = ArchiveReaders::InspectArchiveFile(path, arc) && arc.valid;
            summary = DescribeArchive(arc);
            if (!listed) return false;
            // Unpack the first file as well, for the formats Explorer2 unpacks with its own code
            if (arc.formatName.rfind(L"ZIP", 0) == 0 || arc.formatName.rfind(L"TAR", 0) == 0) {
                for (const auto& entry : arc.entries) {
                    if (entry.isDirectory) continue;
                    std::vector<uint8_t> bytes;
                    ArchiveReaders::ExtractSingleEntryToMemory(path, entry.pathInArchive, bytes);
                    break;
                }
            }
            return true;
        });
    }
}

} // namespace Explorer2::ReaderInspect
