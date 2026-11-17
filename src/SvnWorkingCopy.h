#pragma once
// SvnWorkingCopy.h - Subversion working copies for the version-control dock (GitIntegration): finding svn.exe, running
// it, and reading what it prints with --xml (info, status, log), plus the texts the dock's output panel shows.
// Everything here only reads: no update, commit or revert.
#include "GitIntegration.h"
#include "ToolProcess.h"
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::Svn {

// Cached lookup of svn.exe: PATH, then where TortoiseSVN (with its command-line tools), SlikSVN, CollabNet, VisualSVN
// and the Apache builds install it. A negative result is re-tried at most every few seconds, so a client installed
// while the program runs is picked up.
std::wstring FindSvnExe();

// Runs `svn <args> --non-interactive` in the working copy root (the paths it prints are relative to that folder).
// --non-interactive: a server that wants a password the client has not stored makes svn fail instead of waiting.
ToolProcess::Result Run(const std::wstring& wcRoot, const std::vector<std::wstring>& args, DWORD timeoutMs,
                        const std::atomic<bool>& stop, size_t maxBytes = 16u << 20);

// ---- svn's XML output (UTF-8)
bool ParseInfoXml(std::string_view xml, SvnInfo& out);                                    // svn info --xml
void ParseStatusXml(std::string_view xml, GitRepoState& state, size_t maxChanges);        // svn status --xml
void ParseLogXml(std::string_view xml, std::vector<GitCommit>& out, int64_t nowUnix);     // svn log --xml
std::wstring FormatRevisionText(std::string_view xml);      // svn log -v --xml -r N: header, changed paths, message
std::wstring FormatUpdatesText(std::string_view xml);       // svn status -u --xml: what the repository has that the copy has not
std::wstring DiffStatText(const std::string& unifiedDiff);  // svn diff: lines added / removed per file, like git diff --stat

// The path in the repository ("trunk", "branches/fix-42") from svn info, for the dock's title
std::wstring PathInRepository(const SvnInfo& info);

// "2026-11-10T09:12:44.123456Z" -> Unix time; 0 when the text is not such a time
int64_t ParseIsoTimeUtc(std::string_view iso);
// "5 minutes ago", "3 days ago": the wording git uses for its relative dates (English, like git's own)
std::wstring RelativeTime(int64_t unixTime, int64_t nowUnix);
int64_t NowUnix();

} // namespace Explorer2::Svn
