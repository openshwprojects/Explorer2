// linux/Stage15Runner.cpp - Headless Linux runner for Explorer2's Stage 15 Format & Viewer Verification Suite.
#include "SelfTestCommon.h"
#include "FileContentPane.h"
#include "FileEngine.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "formats/common/AiModelDispatch.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace Explorer2 {

// Provided for FormatTestsDocs.cpp & Viewers when linking Stage15Runner without the full FileEngine/FtpClient app graph
AiModelInfoSummary FileContentPane::InspectAiModelFile(const std::wstring& path, const std::wstring& ext) try {
    return AiModels::InspectAiModelFileToSummary<AiModelInfoSummary>(path, ext);
} E2_READER_CATCH(L"AI model reader", path, { return AiModelInfoSummary(); })

bool FileEngine::CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    return PathUtils::CopyWideToClipboard(hwnd, text);
}

std::wstring FileEngine::FormatSizeDetailed(uint64_t bytes) {
    return PathUtils::FormatByteSize(bytes);
}

std::wstring FileEngine::FormatSizeKB(uint64_t bytes) {
    uint64_t kb = (bytes == 0) ? 0 : ((bytes + 1023ULL) / 1024ULL);
    return std::to_wstring(kb) + L" KB";
}

namespace SelfTestSuite {

std::wstring ScratchTestDir() {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"Explorer2_SelfTest_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring TestDataDir() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = PathUtils::GetParentPath(exePath);
    std::wstring d1 = exeDir + L"/testdata";
    if (GetFileAttributesW(d1.c_str()) != INVALID_FILE_ATTRIBUTES) return d1;
    std::wstring d2 = PathUtils::GetParentPath(exeDir) + L"/testdata";
    if (GetFileAttributesW(d2.c_str()) != INVALID_FILE_ATTRIBUTES) return d2;
    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring d3 = std::wstring(cwd) + L"/testdata";
    if (GetFileAttributesW(d3.c_str()) != INVALID_FILE_ATTRIBUTES) return d3;
    const char* envTd = std::getenv("EXPLORER2_TESTDATA");
    if (envTd && *envTd) return PathUtils::Utf8ToWide(envTd);
    return d3;
}

std::wstring TestEnvVar(const wchar_t* name) {
    wchar_t buf[1024] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return (n > 0 && n < 1024) ? std::wstring(buf, n) : std::wstring();
}

} // namespace SelfTestSuite
} // namespace Explorer2

int main(int argc, char** argv) {
    using namespace Explorer2;
    using namespace Explorer2::SelfTestSuite;

    std::string which = (argc >= 2 && argv[1]) ? argv[1] : "all";
    std::string reportPath = (argc >= 3 && argv[2]) ? argv[2] : "";

    TestCounter tc;
    const ULONGLONG t0 = GetTickCount64();

    const bool runAll = (which == "all");
    const bool run15  = (runAll || which == "15" || which == "stage15");
    if (run15 || which == "15.3d" || which == "3d")         RunFormatTests3D(tc);
    if (run15 || which == "15.eda" || which == "eda")       RunFormatTestsEda(tc);
    if (run15 || which == "15.images" || which == "images") RunFormatTestsImages(tc);
    if (run15 || which == "15.docs" || which == "15.docsa" || which == "docsa") RunFormatTestsDocs(tc);
    if (run15 || which == "15.docs" || which == "15.docsb" || which == "docsb") RunFormatTestsDocsB(tc);
    if (run15 || which == "15.archives" || which == "archives") RunFormatTestsArchives(tc);
    if (runAll || which == "vcs" || which == "12.vcs")      RunVcsTests(tc);

    const ULONGLONG elapsed = GetTickCount64() - t0;
    wchar_t summary[256] = {};
    swprintf_s(summary, L"=== STAGE 15 LINUX SUMMARY (%S): %d PASSED, %d FAILED in %llu ms ===\r\n",
               which.c_str(), tc.passed, tc.failed, static_cast<unsigned long long>(elapsed));
    tc.log += summary;

    const std::string utf8 = PathUtils::WideToUtf8(tc.log);
    if (!reportPath.empty()) {
        PathUtils::WriteFileBytes(PathUtils::Utf8ToWide(reportPath), utf8.data(), utf8.size());
    }
    // Print failed lines + summary to stdout
    size_t pos = 0;
    while (pos < utf8.size()) {
        size_t eol = utf8.find('\n', pos);
        std::string line = (eol == std::string::npos) ? utf8.substr(pos) : utf8.substr(pos, eol - pos + 1);
        pos = (eol == std::string::npos) ? utf8.size() : eol + 1;
        if (line.rfind("[FAIL]", 0) == 0 || line.rfind("===", 0) == 0) {
            std::fwrite(line.data(), 1, line.size(), stdout);
        }
    }
    std::fflush(stdout);
    return (tc.failed == 0 && tc.passed > 0) ? 0 : 1;
}
