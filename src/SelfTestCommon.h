#pragma once
// Shared by the self-test files: the pass/fail counter, a few helpers, and the per-area format tests
// (FormatTests*.cpp, stage 15).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace Explorer2::SelfTestSuite {

struct TestCounter {
    int passed = 0;
    int failed = 0;
    bool quietPass = false;
    std::wstring log;

    void Record(bool ok, const wchar_t* name, const std::wstring& detail = L"") {
        if (ok) {
            ++passed;
            if (quietPass) return;
            log += L"[PASS] ";
        } else {
            ++failed;
            log += L"[FAIL] ";
        }
        log += name;
        if (!detail.empty()) {
            log += L" — " + detail;
        }
        log += L"\r\n";
    }
};

// %TEMP%\Explorer2_SelfTest (created if missing). Tests put their files in a subfolder of their own and
// remove it when done.
std::wstring ScratchTestDir();

// The repository's testdata folder: next to the executable, or in the current directory
std::wstring TestDataDir();

// Value of an environment variable, empty when it is not set (for tests of private files)
std::wstring TestEnvVar(const wchar_t* name);

// Stage 15: one function per reader family, each in its own file
void RunFormatTests3D(TestCounter& tc);        // FormatTests3D.cpp
void RunFormatTestsEda(TestCounter& tc);       // FormatTestsEda.cpp
void RunFormatTestsImages(TestCounter& tc);    // FormatTestsImages.cpp
void RunFormatTestsDocs(TestCounter& tc);      // FormatTestsDocs.cpp (office, data and text formats)
void RunFormatTestsDocsSystem(TestCounter& tc);     // FormatTestsDocsSystem.cpp (binary, media and system formats)
void RunFormatTestsArchives(TestCounter& tc);  // FormatTestsArchives.cpp

// Stage 12 (also alone as "vcs"): the version-control dock's back end, Subversion working copies (SelfTestVcs.cpp)
void RunVcsTests(TestCounter& tc);

// Stage 15 domain-split verification suites (standalone compilation units)
void RunFormatTests3DCadSci(TestCounter& tc);              // FormatTests3DCadSci.cpp
void RunFormatTests3DGameConsole(TestCounter& tc);         // FormatTests3DGameConsole.cpp
void RunFormatTestsImagesStudioRaw(TestCounter& tc);       // FormatTestsImagesStudioRaw.cpp
void RunFormatTestsImagesGpuGame(TestCounter& tc);         // FormatTestsImagesGpuGame.cpp
void RunFormatTestsEdaVlsiFpga(TestCounter& tc);           // FormatTestsEdaVlsiFpga.cpp
void RunFormatTestsEdaWaveformsBus(TestCounter& tc);       // FormatTestsEdaWaveformsBus.cpp
void RunFormatTestsDocsAnalyticsForensics(TestCounter& tc);// FormatTestsDocsAnalyticsForensics.cpp
void RunFormatTestsDocsGameLoc(TestCounter& tc);           // FormatTestsDocsGameLoc.cpp
void RunFormatTestsArchivesSystemPak(TestCounter& tc);     // FormatTestsArchivesSystemPak.cpp

} // namespace Explorer2::SelfTestSuite
