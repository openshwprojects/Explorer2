#pragma once
// ToolProcess.h - runs a command-line tool (git.exe, svn.exe) without a window and collects what it prints.
//
// The child gets NUL as stdin and pipes for stdout / stderr, inherits only those three handles, and runs in a job
// object, so a timeout or a stop request also ends the helpers it started (ssh, git-remote-https). Output is polled
// on the calling thread, so a large stdout and a large stderr can never dead-lock each other, and a grand-child that
// inherited a pipe can never keep the caller waiting after the tool itself has exited.
#include "Types.h"
#include <atomic>
#include <string>
#include <vector>

namespace Explorer2::ToolProcess {

struct Options {
    std::wstring workDir;                       // the child's current directory; empty = this process's
    bool mergeStderr = false;                   // stderr shares the stdout pipe (human readable action output)
    DWORD timeoutMs = 20 * 1000;
    size_t maxBytes = 64u << 20;                // bytes kept per stream; more is read and dropped
    std::vector<std::wstring> envOverride;      // "NAME=value": replaces an inherited variable of that name
    std::vector<std::wstring> envDefault;       // "NAME=value": only when the variable is not set already
    // When set, stdout goes into this file (created or replaced) instead of Result::out: a file of any size
    // (svn cat, git cat-file) without holding it in memory. stderr always has its own pipe then.
    std::wstring stdoutFile;
};

struct Result {
    bool started = false;                       // CreateProcess succeeded
    bool notFound = false;                      // the executable is missing
    bool timedOut = false;
    bool cancelled = false;                     // the stop flag was raised
    DWORD exitCode = static_cast<DWORD>(-1);
    std::string out, err;
    bool ok() const { return started && !timedOut && !cancelled && exitCode == 0; }
};

// Runs `exePath` with `args` (each quoted the way CommandLineToArgvW splits them) and waits for it, for at most
// opt.timeoutMs, polling `stop` every few milliseconds. Re-entrant: several threads may run tools at once.
Result Run(const std::wstring& exePath, const std::vector<std::wstring>& args, const Options& opt,
           const std::atomic<bool>& stop);

// The value of an environment variable, empty when it is not set
std::wstring GetEnv(const wchar_t* name);

// Finds `exeName` in the absolute directories of PATH (never the current directory: that would let a file planted
// in a browsed folder run), then among `candidates` (full paths of usual install locations). Empty when not found.
std::wstring FindExecutable(const wchar_t* exeName, const std::vector<std::wstring>& candidates);

// Text a tool printed: UTF-8 when it is valid UTF-8 (git, and everything svn writes as XML), otherwise the ANSI
// code page (messages a tool writes in the console's language)
std::wstring DecodeOutput(const std::string& bytes);

// The first non-empty line of `text`, trimmed
std::wstring FirstLine(const std::wstring& text);

} // namespace Explorer2::ToolProcess
