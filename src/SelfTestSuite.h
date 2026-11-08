#pragma once
#include <string>

namespace Explorer2::SelfTestSuite {

// Runs the automated verification tests for a given stage (1..10) or "all" / "100x"
// Writes report to `outReportPath` (and stdout if console attached) and returns 0 on 100% pass.
int RunStageTests(const std::wstring& stageName, const std::wstring& outReportPath);

} // namespace Explorer2::SelfTestSuite
