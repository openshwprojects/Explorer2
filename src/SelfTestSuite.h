#pragma once
#include <string>

namespace Explorer2::SelfTestSuite {

// Runs the automated verification tests for a given stage (1..10) or "all" / "100x"
// Writes report to `outReportPath` (and stdout if console attached) and returns 0 on 100% pass.
int RunStageTests(const std::wstring& stageName, const std::wstring& outReportPath);

// Robustness diagnostics (`Explorer2.exe --inspect-files <listFile> <resultFile>`): runs every file reader that
// handles a file's extension on each file named in `listFile` (UTF-8, one path per line) and writes one line per
// step to `resultFile`, flushed as it goes so that the last line shows where a crash or a hang happened:
//   B <index> <reader>                 the reader is about to start on that file
//   E <index> <reader> <0|1> <ms>      it returned (1 = it accepted the file)
//   T <index> <reader> <message>       it let a C++ exception escape
//   X <index> <reader> <code> <module> <offset>   it crashed; the process ends with exit code 3
//   H <index> <reader>                 it ran for more than 20 seconds; the process ends with exit code 4
//   N <index>                          no reader handles that extension
//   D                                  every file was processed
// tools/fuzz_samples.py drives this with damaged copies of the samples in testdata/.
int InspectFiles(const std::wstring& listFile, const std::wstring& resultFile);

} // namespace Explorer2::SelfTestSuite
