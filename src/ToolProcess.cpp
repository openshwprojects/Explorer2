#include "ToolProcess.h"

#include <algorithm>
#include <cwchar>

namespace Explorer2::ToolProcess {

namespace {

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
}

} // namespace Explorer2::ToolProcess
