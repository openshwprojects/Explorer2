#include "ToolProcess.h"
#include "PathUtils.h"

#include <algorithm>
#include <cwchar>
#ifndef _WIN32
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace Explorer2::ToolProcess {

namespace {

#ifndef _WIN32
bool ExecutableExists(const std::wstring& p) {
    const std::string u = Compat::NativePath(p.c_str());
    struct stat st = {};
    if (::stat(u.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;
    return ::access(u.c_str(), X_OK) == 0;
}

bool DrainFd(int fd, std::string& dst, size_t maxBytes) {
    if (fd < 0) return false;
    bool any = false;
    char buf[65536];
    for (;;) {
        const ssize_t got = ::read(fd, buf, sizeof(buf));
        if (got <= 0) return any;
        any = true;
        if (dst.size() < maxBytes) {
            dst.append(buf, std::min<size_t>(static_cast<size_t>(got), maxBytes - dst.size()));
        }
        // else: keep draining so the child never blocks on a full pipe, but drop the data
    }
}

std::string NormalizeLinuxToolArg(const std::wstring& a) {
    std::string u = PathUtils::WideToUtf8(a);
    // When internal paths were joined with '\\' on top of a POSIX absolute prefix (e.g. "/tmp/x\\y" or "-o/tmp/x\\y"),
    // normalize '\\' -> '/' if the parent directory exists on disk.
    auto tryFix = [](std::string& s, size_t prefixLen) {
        if (s.size() <= prefixLen + 1 || s[prefixLen] != '/' || s.find('\\', prefixLen) == std::string::npos) return;
        std::string cand = s.substr(prefixLen);
        std::replace(cand.begin(), cand.end(), '\\', '/');
        const size_t slash = cand.find_last_of('/');
        const std::string parent = (slash == std::string::npos || slash == 0) ? "/" : cand.substr(0, slash);
        struct stat st = {};
        if (::stat(cand.c_str(), &st) == 0 || ::stat(parent.c_str(), &st) == 0) {
            s = s.substr(0, prefixLen) + cand;
        }
    };
    tryFix(u, 0);
    if (u.rfind("-o/", 0) == 0) tryFix(u, 2);
    return u;
}
#else
bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void AppendQuotedArg(std::wstring& cl, const std::wstring& a) {
    cl += L' ';
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) { cl += a; return; }
    // Standard CommandLineToArgvW-compatible quoting.
    cl += L'"';
    for (auto it = a.begin();; ++it) {
        size_t bs = 0;
        while (it != a.end() && *it == L'\\') { ++it; ++bs; }
        if (it == a.end()) { cl.append(bs * 2, L'\\'); break; }
        if (*it == L'"') { cl.append(bs * 2 + 1, L'\\'); cl.push_back(L'"'); }
        else { cl.append(bs, L'\\'); cl.push_back(*it); }
    }
    cl += L'"';
}

bool EnvNameIs(const wchar_t* entry, const std::wstring& name) {
    return _wcsnicmp(entry, name.c_str(), name.size()) == 0 && entry[name.size()] == L'=';
}

std::wstring EnvName(const std::wstring& kv) { return kv.substr(0, kv.find(L'=')); }

// Current environment + the caller's variables, as a CREATE_UNICODE_ENVIRONMENT block.
std::wstring BuildEnvironment(const Options& opt) {
    std::wstring block;
    std::vector<bool> defaultPresent(opt.envDefault.size(), false);
    if (LPWCH env = GetEnvironmentStringsW()) {
        for (const wchar_t* p = env; *p;) {
            size_t len = wcslen(p);
            bool overridden = false;
            for (const auto& kv : opt.envOverride) overridden = overridden || EnvNameIs(p, EnvName(kv));
            for (size_t k = 0; k < opt.envDefault.size(); ++k) if (EnvNameIs(p, EnvName(opt.envDefault[k]))) defaultPresent[k] = true;
            if (!overridden) { block.append(p, len); block.push_back(L'\0'); }
            p += len + 1;
        }
        FreeEnvironmentStringsW(env);
    }
    for (const auto& kv : opt.envOverride) { block += kv; block.push_back(L'\0'); }
    for (size_t k = 0; k < opt.envDefault.size(); ++k) {
        if (!defaultPresent[k]) { block += opt.envDefault[k]; block.push_back(L'\0'); }
    }
    block.push_back(L'\0');
    return block;
}

// Moves everything currently readable from `h` into `dst` (never blocks). Returns true when bytes were read.
bool Drain(HANDLE h, std::string& dst, size_t maxBytes) {
    if (!h) return false;
    bool any = false;
    char buf[65536];
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) return any;
        DWORD got = 0;
        if (!ReadFile(h, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0) return any;
        any = true;
        if (dst.size() < maxBytes) dst.append(buf, std::min<size_t>(got, maxBytes - dst.size()));
        // else: keep draining so the child never blocks on a full pipe, but drop the data
    }
}
#endif

void TrimInPlace(std::wstring& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == L'\r' || s[b] == L'\n' || s[b] == L' ' || s[b] == L'\t')) ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == L'\r' || s[e - 1] == L'\n' || s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == 0)) --e;
    s = s.substr(b, e - b);
}

} // namespace

std::wstring GetEnv(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring v(n, L'\0');
    DWORD got = GetEnvironmentVariableW(name, v.data(), n);
    v.resize(got);
    return v;
}

std::wstring FindExecutable(const wchar_t* exeName, const std::vector<std::wstring>& candidates) {
#ifndef _WIN32
    if (!exeName || !*exeName) return {};
    std::wstring primary(exeName);
    std::wstring stripped;
    if (primary.size() > 4 && _wcsicmp(primary.c_str() + primary.size() - 4, L".exe") == 0) {
        stripped = primary.substr(0, primary.size() - 4);
    }
    // PATH walk done by hand: never search relative or empty entries (anti-binary-planting).
    std::wstring path = GetEnv(L"PATH");
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t sep = path.find(L':', pos);
        if (sep == std::wstring::npos) sep = path.size();
        std::wstring dir = path.substr(pos, sep - pos);
        pos = sep + 1;
        dir.erase(std::remove(dir.begin(), dir.end(), L'"'), dir.end());
        while (dir.size() > 1 && (dir.back() == L'/' || dir.back() == L'\\')) dir.pop_back();
        if (dir.empty() || dir[0] != L'/') continue;
        const std::wstring prefix = (dir == L"/") ? std::wstring(L"/") : (dir + L"/");
        if (ExecutableExists(prefix + primary)) return prefix + primary;
        if (!stripped.empty() && ExecutableExists(prefix + stripped)) return prefix + stripped;
    }
    for (const auto& cand : candidates) {
        if (!cand.empty() && ExecutableExists(cand)) return cand;
    }
    return {};
#else
    // PATH walk done by hand: SearchPathW would also look in the current directory (binary planting).
    std::wstring path = GetEnv(L"PATH");
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t semi = path.find(L';', pos);
        if (semi == std::wstring::npos) semi = path.size();
        std::wstring dir = path.substr(pos, semi - pos);
        pos = semi + 1;
        dir.erase(std::remove(dir.begin(), dir.end(), L'"'), dir.end());
        while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
        if (dir.size() < 3 || dir[1] != L':') continue;     // skip relative / empty entries
        std::wstring cand = dir + L"\\" + exeName;
        if (FileExists(cand)) return cand;
    }
    for (const auto& cand : candidates) {
        if (!cand.empty() && FileExists(cand)) return cand;
    }
    return {};
#endif
}

std::wstring DecodeOutput(const std::string& bytes) {
    if (bytes.empty()) return {};
    const int len = static_cast<int>(std::min<size_t>(bytes.size(), 0x7FFFFFFF));
    for (const UINT cp : { static_cast<UINT>(CP_UTF8), static_cast<UINT>(CP_ACP) }) {
        const DWORD flags = (cp == CP_UTF8) ? MB_ERR_INVALID_CHARS : 0;
        const int n = MultiByteToWideChar(cp, flags, bytes.data(), len, nullptr, 0);
        if (n <= 0) continue;
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(cp, flags, bytes.data(), len, w.data(), n);
        return w;
    }
    return {};
}

std::wstring FirstLine(const std::wstring& text) {
    std::wstring t = text;
    TrimInPlace(t);
    size_t nl = t.find_first_of(L"\r\n");
    return nl == std::wstring::npos ? t : t.substr(0, nl);
}

Result Run(const std::wstring& exePath, const std::vector<std::wstring>& args, const Options& opt,
           const std::atomic<bool>& stop) {
    Result res;
    if (exePath.empty()) { res.notFound = true; return res; }

#ifndef _WIN32
    const std::string exeUtf8 = Compat::NativePath(exePath.c_str());
    struct stat st = {};
    if (::stat(exeUtf8.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
        res.notFound = true;
        return res;
    }

    int outPipe[2] = { -1, -1 };
    int errPipe[2] = { -1, -1 };
    int execPipe[2] = { -1, -1 };
    int outFileFd = -1;
    int nulFd = -1;

    auto closeFd = [](int& fd) {
        if (fd >= 0) { ::close(fd); fd = -1; }
    };
    auto closeAll = [&]() {
        closeFd(outPipe[0]); closeFd(outPipe[1]);
        closeFd(errPipe[0]); closeFd(errPipe[1]);
        closeFd(execPipe[0]); closeFd(execPipe[1]);
        closeFd(outFileFd);
        closeFd(nulFd);
    };

    const bool toFile = !opt.stdoutFile.empty();
    const bool merge = opt.mergeStderr && !toFile;
    if (toFile) {
        const std::string outPathUtf8 = Compat::NativePath(opt.stdoutFile.c_str());
        outFileFd = ::open(outPathUtf8.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (outFileFd < 0) { closeAll(); return res; }
    } else {
        if (::pipe2(outPipe, O_CLOEXEC) != 0) { closeAll(); return res; }
    }
    if (!merge) {
        if (::pipe2(errPipe, O_CLOEXEC) != 0) { closeAll(); return res; }
    }
    if (::pipe2(execPipe, O_CLOEXEC) != 0) { closeAll(); return res; }
    nulFd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (nulFd < 0) { closeAll(); return res; }

    // Build argv & envp in parent before fork() so the child performs only async-signal-safe calls
    std::vector<std::string> argvStorage;
    argvStorage.reserve(1 + args.size());
    argvStorage.push_back(exeUtf8);
    for (const auto& a : args) argvStorage.push_back(NormalizeLinuxToolArg(a));
    std::vector<char*> argvPtrs;
    argvPtrs.reserve(argvStorage.size() + 1);
    for (auto& s : argvStorage) argvPtrs.push_back(s.data());
    argvPtrs.push_back(nullptr);

    auto envNameOf = [](const std::string& kv) -> std::string {
        const size_t eq = kv.find('=');
        return (eq == std::string::npos) ? kv : kv.substr(0, eq);
    };
    std::vector<std::string> overUtf8, defUtf8;
    for (const auto& kv : opt.envOverride) overUtf8.push_back(PathUtils::WideToUtf8(kv));
    for (const auto& kv : opt.envDefault) defUtf8.push_back(PathUtils::WideToUtf8(kv));

    std::vector<std::string> envStorage;
    std::vector<bool> defPresent(defUtf8.size(), false);
    if (environ) {
        for (char** ep = environ; *ep; ++ep) {
            std::string entry(*ep);
            const std::string ename = envNameOf(entry);
            bool overridden = false;
            for (const auto& ov : overUtf8) {
                if (ename == envNameOf(ov)) { overridden = true; break; }
            }
            for (size_t k = 0; k < defUtf8.size(); ++k) {
                if (ename == envNameOf(defUtf8[k])) defPresent[k] = true;
            }
            if (!overridden) envStorage.push_back(std::move(entry));
        }
    }
    for (const auto& ov : overUtf8) envStorage.push_back(ov);
    for (size_t k = 0; k < defUtf8.size(); ++k) {
        if (!defPresent[k]) envStorage.push_back(defUtf8[k]);
    }
    std::vector<char*> envPtrs;
    envPtrs.reserve(envStorage.size() + 1);
    for (auto& s : envStorage) envPtrs.push_back(s.data());
    envPtrs.push_back(nullptr);

    const std::string workDirUtf8 = opt.workDir.empty() ? std::string() : Compat::NativePath(opt.workDir.c_str());
    const int childOutFd = toFile ? outFileFd : outPipe[1];
    const int childErrFd = merge ? childOutFd : errPipe[1];

    const pid_t pid = ::fork();
    if (pid < 0) {
        closeAll();
        return res;
    }
    if (pid == 0) {
        // The Linux main() ignores SIGPIPE, and an ignored signal stays ignored across exec: the tool gets the
        // default action back (signal() is async-signal-safe)
        ::signal(SIGPIPE, SIG_DFL);
        ::setpgid(0, 0);
        auto writeErrAndExit = [&](int err) -> void {
            ssize_t wr = ::write(execPipe[1], &err, sizeof(err));
            (void)wr;
            ::_exit(127);
        };
        if (!workDirUtf8.empty() && ::chdir(workDirUtf8.c_str()) != 0) {
            writeErrAndExit(errno);
        }
        if (::dup2(nulFd, STDIN_FILENO) < 0 ||
            ::dup2(childOutFd, STDOUT_FILENO) < 0 ||
            ::dup2(childErrFd, STDERR_FILENO) < 0)
        {
            writeErrAndExit(errno);
        }
        // Close any extra descriptors inherited from other threads (except execPipe[1], which has FD_CLOEXEC)
        if (DIR* d = ::opendir("/proc/self/fd")) {
            const int dirFd = ::dirfd(d);
            while (dirent* e = ::readdir(d)) {
                if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
                const int fd = std::atoi(e->d_name);
                if (fd >= 3 && fd != execPipe[1] && fd != dirFd) ::close(fd);
            }
            ::closedir(d);
        } else {
            for (int fd = 3; fd < 1024; ++fd) {
                if (fd != execPipe[1]) ::close(fd);
            }
        }
        ::execve(exeUtf8.c_str(), argvPtrs.data(), envPtrs.data());
        writeErrAndExit(errno);
    }

    // Parent: ensure child process group is established and close child-side descriptors
    (void)::setpgid(pid, pid);
    closeFd(execPipe[1]);
    closeFd(outPipe[1]);
    closeFd(errPipe[1]);
    closeFd(outFileFd);
    closeFd(nulFd);

    int childExecErr = 0;
    ssize_t errRead = 0;
    do {
        errRead = ::read(execPipe[0], &childExecErr, sizeof(childExecErr));
    } while (errRead < 0 && errno == EINTR);
    closeFd(execPipe[0]);

    if (errRead > 0) {
        int wstatus = 0;
        (void)::waitpid(pid, &wstatus, 0);
        res.notFound = (childExecErr == ENOENT || childExecErr == ENOTDIR);
        closeAll();
        return res;
    }

    res.started = true;
    if (outPipe[0] >= 0) {
        const int fl = ::fcntl(outPipe[0], F_GETFL, 0);
        if (fl >= 0) (void)::fcntl(outPipe[0], F_SETFL, fl | O_NONBLOCK);
    }
    if (errPipe[0] >= 0) {
        const int fl = ::fcntl(errPipe[0], F_GETFL, 0);
        if (fl >= 0) (void)::fcntl(errPipe[0], F_SETFL, fl | O_NONBLOCK);
    }

    int wstatus = 0;
    bool reaped = false;
    auto killChildGroup = [&]() {
        (void)::kill(-pid, SIGKILL);
        (void)::kill(pid, SIGKILL);
        if (!reaped) {
            while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {}
            reaped = true;
        }
    };

    const ULONGLONG startTick = GetTickCount64();
    bool exited = false;
    for (;;) {
        bool got = DrainFd(outPipe[0], res.out, opt.maxBytes);
        got |= DrainFd(errPipe[0], res.err, opt.maxBytes);
        if (exited) {
            if (!got) break;
            continue;
        }
        if (stop.load()) {
            killChildGroup();
            res.cancelled = true;
            break;
        }
        if (GetTickCount64() - startTick > opt.timeoutMs) {
            killChildGroup();
            res.timedOut = true;
            break;
        }
        const pid_t w = ::waitpid(pid, &wstatus, WNOHANG);
        if (w == pid) {
            reaped = true;
            exited = true;
            continue;
        }
        if (!got) {
            struct pollfd pfds[2] = {};
            nfds_t nfds = 0;
            if (outPipe[0] >= 0) { pfds[nfds].fd = outPipe[0]; pfds[nfds].events = POLLIN; ++nfds; }
            if (errPipe[0] >= 0) { pfds[nfds].fd = errPipe[0]; pfds[nfds].events = POLLIN; ++nfds; }
            if (nfds > 0) (void)::poll(pfds, nfds, 4);
            else ::usleep(4000);
        }
    }

    if (!reaped) {
        while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {}
        reaped = true;
    }
    if (!res.timedOut && !res.cancelled) {
        if (WIFEXITED(wstatus)) res.exitCode = static_cast<DWORD>(WEXITSTATUS(wstatus));
        else if (WIFSIGNALED(wstatus)) res.exitCode = static_cast<DWORD>(128 + WTERMSIG(wstatus));
    }
    closeAll();
    return res;
#else
    std::wstring cl = L"\"" + exePath + L"\"";
    for (const auto& a : args) AppendQuotedArg(cl, a);

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr, nul = INVALID_HANDLE_VALUE;
    auto closeAll = [&]() {
        for (HANDLE* h : { &outR, &outW, &errR, &errW }) if (*h) { CloseHandle(*h); *h = nullptr; }
        if (nul != INVALID_HANDLE_VALUE) { CloseHandle(nul); nul = INVALID_HANDLE_VALUE; }
    };

    const bool toFile = !opt.stdoutFile.empty();
    const bool merge = opt.mergeStderr && !toFile;
    if (toFile) {
        outW = CreateFileW(opt.stdoutFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (outW == INVALID_HANDLE_VALUE) { outW = nullptr; return res; }
    } else {
        if (!CreatePipe(&outR, &outW, &sa, 1 << 16)) return res;
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);             // parent end must not leak into the child
    }
    if (!merge) {
        if (!CreatePipe(&errR, &errW, &sa, 1 << 16)) { closeAll(); return res; }
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    }
    nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul == INVALID_HANDLE_VALUE) { closeAll(); return res; }
    HANDLE childErr = merge ? outW : errW;

    // Restrict inheritance to exactly the three std handles (other threads of the app may spawn processes too).
    const DWORD inheritCount = (outW == childErr) ? 2 : 3;
    HANDLE listed[3] = { nul, outW, childErr };
    if (inheritCount == 2) { listed[1] = outW; listed[2] = nullptr; }

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto* attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool attrOk = InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize) != FALSE;
    if (attrOk && !UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, listed,
                                             inheritCount * sizeof(HANDLE), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attrList);
        attrOk = false;
    }
    if (!attrOk) { closeAll(); return res; }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul;
    si.StartupInfo.hStdOutput = outW;
    si.StartupInfo.hStdError = childErr;
    si.lpAttributeList = attrList;

    std::wstring env = BuildEnvironment(opt);
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;
    BOOL created = CreateProcessW(exePath.c_str(), cl.data(), nullptr, nullptr, TRUE, flags, env.data(),
                                  opt.workDir.empty() ? nullptr : opt.workDir.c_str(), &si.StartupInfo, &pi);
    DWORD createErr = GetLastError();
    DeleteProcThreadAttributeList(attrList);
    // The child owns its copies now; closing ours is what lets us see EOF-style behaviour and frees the handles.
    CloseHandle(outW); outW = nullptr;
    if (errW) { CloseHandle(errW); errW = nullptr; }
    CloseHandle(nul); nul = INVALID_HANDLE_VALUE;

    if (!created) {
        res.notFound = (createErr == ERROR_FILE_NOT_FOUND || createErr == ERROR_PATH_NOT_FOUND);
        closeAll();
        return res;
    }
    res.started = true;

    // Put the tool into a job so a timeout also kills helpers it spawned (ssh, git-remote-https). No kill-on-close
    // limit: on a normal exit we deliberately leave background helpers (e.g. fsmonitor) alone.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job && !AssignProcessToJobObject(job, pi.hProcess)) { CloseHandle(job); job = nullptr; }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    auto kill = [&]() {
        if (job) TerminateJobObject(job, 1); else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    };

    const ULONGLONG startTick = GetTickCount64();
    bool exited = false;
    for (;;) {
        bool got = Drain(outR, res.out, opt.maxBytes);
        got |= Drain(errR, res.err, opt.maxBytes);
        if (exited) { if (!got) break; continue; }      // final drain after exit
        if (stop.load()) { kill(); res.cancelled = true; break; }
        if (GetTickCount64() - startTick > opt.timeoutMs) { kill(); res.timedOut = true; break; }
        if (!got && WaitForSingleObject(pi.hProcess, 4) == WAIT_OBJECT_0) exited = true;
    }
    DWORD code = static_cast<DWORD>(-1);
    if (GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE) res.exitCode = code;
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    closeAll();
    return res;
#endif
}

} // namespace Explorer2::ToolProcess
