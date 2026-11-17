#pragma once
// What the archive readers produce and the archive pane shows. No Windows headers: the readers also build on
// Linux (see linux/).
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer2 {

// Paths of entries inside archives: forward slashes, no leading or trailing slash, no leading "./"
namespace ArchivePath {
inline std::wstring Normalize(std::wstring p) {
    for (wchar_t& ch : p) {
        if (ch == L'\\') ch = L'/';
    }
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    while (!p.empty() && p.back() == L'/') p.pop_back();
    while (p.rfind(L"./", 0) == 0) p.erase(0, 2);
    return p;
}

inline std::wstring BaseName(const std::wstring& p) {
    size_t slash = p.find_last_of(L'/');
    return (slash == std::wstring::npos) ? p : p.substr(slash + 1);
}
} // namespace ArchivePath

struct ArchiveItemEntry {
    std::wstring pathInArchive; // Forward-slash normalized relative path, no leading/trailing slash
    std::wstring name;          // Base name
    uint64_t uncompressedSize = 0;
    uint64_t compressedSize = 0;
    uint32_t crc32 = 0;
    uint16_t dosTime = 0;
    uint16_t dosDate = 0;
    bool isDirectory = false;
    // Internal offsets for fast direct extraction
    uint64_t rawDataOffset = 0;
    uint16_t compressionMethod = 0;
};

struct ArchiveSummaryInfo {
    std::wstring formatName; // e.g. L"ZIP", L"RAR4", L"RAR5", L"7-Zip", L"TAR.GZ"
    size_t totalFiles = 0;
    size_t totalFolders = 0;
    uint64_t totalUncompressedBytes = 0;
    uint64_t totalCompressedBytes = 0;
    std::vector<ArchiveItemEntry> entries;
    bool valid = false;
};

// How a run of an external archive tool (7z.exe, UnRAR.exe, tar.exe) ended
enum class ArchiveToolRun { Succeeded, Failed, TimedOut };

} // namespace Explorer2
