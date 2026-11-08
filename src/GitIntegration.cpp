#include "GitIntegration.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace Explorer2 {

namespace {

// ---------------------------------------------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------------------------------------------
constexpr DWORD  kTimeoutNormalMs  = 20 * 1000;
constexpr DWORD  kTimeoutStatusMs  = 45 * 1000;     // status on a repo with 100k+ files can legitimately be slow
constexpr DWORD  kTimeoutNetworkMs = 120 * 1000;    // fetch / pull / push
constexpr size_t kMaxCapture       = 64u << 20;     // hard cap on bytes kept per stream (status of huge trees)
constexpr size_t kMaxOutputChars   = 64u * 1024;    // text exposed through SnapshotOutput()
constexpr size_t kMaxChanges       = 500;
constexpr int    kMaxCommits       = 60;
constexpr size_t kMaxBranches      = 400;

// ---------------------------------------------------------------------------------------------------------------
// git.exe discovery (cached)
// ---------------------------------------------------------------------------------------------------------------
std::mutex g_gitPathMutex;
std::wstring g_gitPath;                 // empty = not found
ULONGLONG g_gitLastProbeTick = 0;
bool g_gitProbed = false;

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetEnv(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring v(n, L'\0');
    DWORD got = GetEnvironmentVariableW(name, v.data(), n);
    v.resize(got);
    return v;
}

std::wstring ProbeGit() {
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
        std::wstring cand = dir + L"\\git.exe";
        if (FileExists(cand)) return cand;
    }
    const std::wstring roots[] = { GetEnv(L"ProgramFiles"), GetEnv(L"ProgramFiles(x86)"), L"C:\\Program Files",
                                   GetEnv(L"LOCALAPPDATA") + L"\\Programs" };
    for (const auto& r : roots) {
        if (r.empty() || r == L"\\Programs") continue;
        std::wstring cand = r + L"\\Git\\cmd\\git.exe";
        if (FileExists(cand)) return cand;
    }
    return {};
}

// Returns the path of git.exe or empty.
std::wstring GetGitPath() {
    std::lock_guard<std::mutex> lk(g_gitPathMutex);
    ULONGLONG now = GetTickCount64();
    if (!g_gitProbed || (g_gitPath.empty() && now - g_gitLastProbeTick > 5000)) {
        g_gitPath = ProbeGit();
        g_gitProbed = true;
        g_gitLastProbeTick = now;
    }
    return g_gitPath;
}

// ---------------------------------------------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------------------------------------------
std::wstring Utf8ToWide(const char* s, size_t len) {
    if (len == 0) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, nullptr, 0);   // invalid bytes become U+FFFD
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, (int)len, w.data(), n);
    return w;
}
std::wstring Utf8ToWide(const std::string& s) { return Utf8ToWide(s.data(), s.size()); }

// Splits keeping empty fields (also a trailing one).
std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    for (;;) {
        size_t p = s.find(sep, pos);
        if (p == std::wstring::npos) { out.push_back(s.substr(pos)); break; }
        out.push_back(s.substr(pos, p - pos));
        pos = p + 1;
    }
    return out;
}

void TrimInPlace(std::wstring& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == L'\r' || s[b] == L'\n' || s[b] == L' ' || s[b] == L'\t')) ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == L'\r' || s[e - 1] == L'\n' || s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == 0)) --e;
    s = s.substr(b, e - b);
}

// CRLF / lone CR (progress lines) -> LF, strip NULs, trim, cap length.
std::wstring NormalizeOutputText(const std::wstring& in) {
    std::wstring o;
    o.reserve(std::min(in.size(), kMaxOutputChars + 64));
    for (size_t i = 0; i < in.size(); ++i) {
        wchar_t c = in[i];
        if (c == 0) continue;
        if (c == L'\r') {
            if (i + 1 < in.size() && in[i + 1] == L'\n') continue;      // the \n follows
            c = L'\n';
        }
        o.push_back(c);
        if (o.size() > kMaxOutputChars + 4096) break;                   // enough, avoid copying megabytes
    }
    TrimInPlace(o);
    if (o.size() > kMaxOutputChars) {
        size_t cut = kMaxOutputChars;
        if (cut > 0 && o[cut - 1] >= 0xD800 && o[cut - 1] <= 0xDBFF) --cut;   // do not split a surrogate pair
        o.resize(cut);
        o += L"\n... (output truncated)";
    }
    return o;
}

std::wstring FirstLine(const std::wstring& s) {
    std::wstring t = s;
    TrimInPlace(t);
    size_t nl = t.find_first_of(L"\r\n");
    return nl == std::wstring::npos ? t : t.substr(0, nl);
}

bool Contains(const std::wstring& hay, const wchar_t* needle) { return hay.find(needle) != std::wstring::npos; }

// Branch names / hashes come from the UI: reject anything git could mistake for an option or that is odd.
bool IsSafeRefArg(const std::wstring& a) {
    if (a.empty() || a[0] == L'-') return false;
    for (wchar_t c : a) if (c < 0x20 || c == 0x7F) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Process runner
// ---------------------------------------------------------------------------------------------------------------
struct RunOptions {
    bool readOnly = true;           // adds --no-optional-locks and GIT_OPTIONAL_LOCKS=0
    bool mergeStderr = false;       // stderr shares the stdout pipe (human readable action output)
    DWORD timeoutMs = kTimeoutNormalMs;
    size_t maxBytes = kMaxCapture;
};

struct RunResult {
    bool started = false;           // CreateProcess succeeded
    bool notFound = false;          // git.exe missing
    bool timedOut = false;
    bool cancelled = false;         // object is being destroyed
    DWORD exitCode = (DWORD)-1;
    std::string out, err;
    bool ok() const { return started && !timedOut && !cancelled && exitCode == 0; }
};

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

bool EnvNameIs(const wchar_t* entry, const wchar_t* name) {
    size_t n = wcslen(name);
    return _wcsnicmp(entry, name, n) == 0 && entry[n] == L'=';
}

// Current environment + our overrides, as a CREATE_UNICODE_ENVIRONMENT block.
std::wstring BuildEnvironment(bool readOnly) {
    std::wstring block;
    bool hasSshCmd = false;
    if (LPWCH env = GetEnvironmentStringsW()) {
        for (const wchar_t* p = env; *p;) {
            size_t len = wcslen(p);
            bool ours = EnvNameIs(p, L"GIT_TERMINAL_PROMPT") || EnvNameIs(p, L"GCM_INTERACTIVE") ||
                        (readOnly && EnvNameIs(p, L"GIT_OPTIONAL_LOCKS"));
            if (EnvNameIs(p, L"GIT_SSH_COMMAND")) hasSshCmd = true;
            if (!ours) { block.append(p, len); block.push_back(L'\0'); }
            p += len + 1;
        }
        FreeEnvironmentStringsW(env);
    }
    auto add = [&](const wchar_t* kv) { block += kv; block.push_back(L'\0'); };
    add(L"GIT_TERMINAL_PROMPT=0");          // never wait for a username/password on the (invisible) console
    add(L"GCM_INTERACTIVE=never");          // Git Credential Manager: no GUI prompt either
    if (!hasSshCmd) add(L"GIT_SSH_COMMAND=ssh -o BatchMode=yes");   // ssh must not ask for passphrases
    if (readOnly) add(L"GIT_OPTIONAL_LOCKS=0");
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

// Runs git with the standard prefix. Output is polled with PeekNamedPipe on this thread, so a large stdout and a
// large stderr can never dead-lock each other, and a grand-child that inherited the pipe can never keep us waiting
// after git.exe itself has exited.
RunResult RunGit(const std::wstring& root, const std::vector<std::wstring>& cmdArgs, const RunOptions& opt,
                 const std::atomic<bool>& stop) {
    RunResult res;
    const std::wstring gitPath = GetGitPath();
    if (gitPath.empty()) { res.notFound = true; return res; }

    std::wstring cl = L"\"" + gitPath + L"\"";
    AppendQuotedArg(cl, L"-C"); AppendQuotedArg(cl, root);
    AppendQuotedArg(cl, L"-c"); AppendQuotedArg(cl, L"core.quotepath=false");
    AppendQuotedArg(cl, L"-c"); AppendQuotedArg(cl, L"i18n.logOutputEncoding=UTF-8");
    if (opt.readOnly) AppendQuotedArg(cl, L"--no-optional-locks");
    for (const auto& a : cmdArgs) AppendQuotedArg(cl, a);

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr, nul = INVALID_HANDLE_VALUE;
    auto closeAll = [&]() {
        for (HANDLE* h : { &outR, &outW, &errR, &errW }) if (*h) { CloseHandle(*h); *h = nullptr; }
        if (nul != INVALID_HANDLE_VALUE) { CloseHandle(nul); nul = INVALID_HANDLE_VALUE; }
    };

    if (!CreatePipe(&outR, &outW, &sa, 1 << 16)) return res;
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);                 // parent end must not leak into the child
    if (!opt.mergeStderr) {
        if (!CreatePipe(&errR, &errW, &sa, 1 << 16)) { closeAll(); return res; }
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    }
    nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul == INVALID_HANDLE_VALUE) { closeAll(); return res; }
    HANDLE childErr = opt.mergeStderr ? outW : errW;

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

    std::wstring env = BuildEnvironment(opt.readOnly);
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;
    BOOL created = CreateProcessW(gitPath.c_str(), cl.data(), nullptr, nullptr, TRUE, flags, env.data(), nullptr,
                                  &si.StartupInfo, &pi);
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

    // Put git into a job so a timeout also kills helpers it spawned (ssh, git-remote-https). No kill-on-close
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
    DWORD code = (DWORD)-1;
    if (GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE) res.exitCode = code;
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    closeAll();
    return res;
}

// ---------------------------------------------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------------------------------------------
// `## main...origin/main [ahead 1, behind 2]`, `## main`, `## HEAD (no branch)`, `## No commits yet on main`.
void ParseStatusHeader(std::wstring h, GitRepoState& s) {
    if (h.rfind(L"## ", 0) == 0) h.erase(0, 3);
    if (h.rfind(L"No commits yet on ", 0) == 0) { s.branch = h.substr(18); return; }
    if (h.rfind(L"Initial commit on ", 0) == 0) { s.branch = h.substr(18); return; }
    if (h.rfind(L"HEAD (no branch)", 0) == 0) { s.detachedHead = true; s.branch.clear(); return; }

    std::wstring track;
    size_t br = h.find(L" [");
    if (br != std::wstring::npos && h.back() == L']') { track = h.substr(br + 2, h.size() - br - 3); h.resize(br); }
    size_t dots = h.find(L"...");
    if (dots == std::wstring::npos) { s.branch = h; return; }
    s.branch = h.substr(0, dots);
    s.upstream = h.substr(dots + 3);
    for (const auto& part : Split(track, L',')) {
        std::wstring p = part;
        TrimInPlace(p);
        if (p.rfind(L"ahead ", 0) == 0) s.ahead = _wtoi(p.c_str() + 6);
        else if (p.rfind(L"behind ", 0) == 0) s.behind = _wtoi(p.c_str() + 7);
    }
}

bool IsUnmerged(wchar_t x, wchar_t y) {
    return x == L'U' || y == L'U' || (x == L'A' && y == L'A') || (x == L'D' && y == L'D');
}

// `status --porcelain=v1 -b -z`: NUL-terminated records, "XY path"; for R/C the next record is the original path.
void ParseStatus(const std::wstring& text, GitRepoState& s) {
    s.changes.clear();
    s.stagedCount = s.modifiedCount = s.untrackedCount = s.conflictCount = 0;
    s.ahead = s.behind = 0;
    s.branch.clear(); s.upstream.clear(); s.detachedHead = false;

    size_t pos = 0;
    bool first = true;
    while (pos < text.size()) {
        size_t end = text.find(L'\0', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring rec = text.substr(pos, end - pos);
        pos = end + 1;
        if (first) {
            first = false;
            if (rec.rfind(L"##", 0) == 0) { ParseStatusHeader(rec, s); continue; }
        }
        if (rec.size() < 4 || rec[2] != L' ') continue;
        GitFileChange c;
        c.indexStatus = rec[0];
        c.workStatus = rec[1];
        c.path = rec.substr(3);
        if (c.indexStatus == L'R' || c.indexStatus == L'C' || c.workStatus == L'R' || c.workStatus == L'C') {
            size_t e2 = text.find(L'\0', pos);
            if (e2 == std::wstring::npos) e2 = text.size();
            c.origPath = text.substr(pos, e2 - pos);
            pos = e2 + 1;
        }
        if (c.indexStatus == L'!') continue;
        if (c.indexStatus == L'?') ++s.untrackedCount;
        else if (IsUnmerged(c.indexStatus, c.workStatus)) ++s.conflictCount;
        else {
            if (c.indexStatus != L' ') ++s.stagedCount;
            if (c.workStatus != L' ') ++s.modifiedCount;
        }
        if (s.changes.size() < kMaxChanges) s.changes.push_back(std::move(c));
    }
}

void ParseLog(const std::wstring& text, std::vector<GitCommit>& out) {
    out.clear();
    for (const auto& recRaw : Split(text, L'\x1e')) {
        std::wstring rec = recRaw;
        size_t b = 0;
        while (b < rec.size() && (rec[b] == L'\r' || rec[b] == L'\n')) ++b;      // `format:` separates with newlines
        rec.erase(0, b);
        if (rec.empty()) continue;
        auto f = Split(rec, L'\x1f');
        if (f.size() < 7) continue;
        GitCommit c;
        c.hash = f[0]; c.shortHash = f[1]; c.author = f[2]; c.relDate = f[3];
        c.unixTime = _wtoi64(f[4].c_str());
        c.refs = f[5]; c.subject = f[6];
        TrimInPlace(c.subject);
        out.push_back(std::move(c));
    }
}

void ParseBranches(const std::wstring& text, std::vector<GitBranch>& out) {
    out.clear();
    for (const auto& lineRaw : Split(text, L'\n')) {
        std::wstring line = lineRaw;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;
        // %(HEAD) \x1f name \x1f upstream \x1f track \x1f symref \x1f subject
        auto f = Split(line, L'\x1f');
        if (f.size() < 6) continue;
        if (!f[4].empty()) continue;                                    // symbolic ref such as origin/HEAD
        GitBranch b;
        b.isCurrent = (f[0] == L"*");
        b.name = f[1];
        b.upstream = f[2];
        b.track = f[3];
        b.lastSubject = f[5];
        // b.name is the FULL refname (refs/heads/x or refs/remotes/o/x); the caller shortens it and splits local/remote.
        out.push_back(std::move(b));
    }
}

} // namespace

// =================================================================================================================
// Static helpers
// =================================================================================================================
bool GitIntegration::IsGitInstalled() { return !GetGitPath().empty(); }

std::wstring GitIntegration::FindRepoRoot(const std::wstring& folder) {
    if (folder.empty()) return {};
    wchar_t full[MAX_PATH * 4];
    DWORD n = GetFullPathNameW(folder.c_str(), (DWORD)(sizeof(full) / sizeof(full[0])), full, nullptr);
    if (n == 0 || n >= sizeof(full) / sizeof(full[0])) return {};
    std::wstring cur(full, n);
    if (cur.size() < 3 || !(cur[1] == L':' || (cur[0] == L'\\' && cur[1] == L'\\'))) return {};   // virtual folders
    if (cur.rfind(L"\\\\?\\", 0) == 0 || cur.rfind(L"\\\\.\\", 0) == 0) return {};

    // A file path starts at its parent directory.
    DWORD attr = GetFileAttributesW(cur.c_str());
    bool startIsFile = (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));

    for (;;) {
        while (cur.size() > 3 && (cur.back() == L'\\' || cur.back() == L'/')) cur.pop_back();
        if (!startIsFile) {
            std::wstring dotGit = cur;
            if (dotGit.back() != L'\\') dotGit += L'\\';
            dotGit += L".git";
            if (GetFileAttributesW(dotGit.c_str()) != INVALID_FILE_ATTRIBUTES) return cur;   // dir or file
        }
        startIsFile = false;
        size_t slash = cur.find_last_of(L'\\');
        if (slash == std::wstring::npos) return {};
        if (slash <= 2 && cur[1] == L':') {             // "C:\dir" -> "C:\" ; "C:\" is the top
            if (cur.size() <= 3) return {};
            cur.resize(3);
            continue;
        }
        if (slash < 2) return {};
        // UNC: stop at "\\server\share"
        if (cur[0] == L'\\' && cur[1] == L'\\') {
            size_t s1 = cur.find(L'\\', 2);
            size_t s2 = (s1 == std::wstring::npos) ? s1 : cur.find(L'\\', s1 + 1);
            if (s2 == std::wstring::npos) return {};
        }
        cur.resize(slash);
    }
}

// =================================================================================================================
// Lifetime
// =================================================================================================================
GitIntegration::GitIntegration() { m_worker = std::thread([this] { WorkerMain(); }); }

GitIntegration::~GitIntegration() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;                  // RunGit polls this flag and kills the child within a few milliseconds
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

// =================================================================================================================
// Public API
// =================================================================================================================
void GitIntegration::RequestRefresh(const std::wstring& repoRoot) {
    if (repoRoot.empty()) return;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_state.root != repoRoot) {                 // never show another repo's data under the new root
            m_state = GitRepoState();
            m_state.root = repoRoot;
            ++m_version;
        }
        m_pendingRefreshRoot = repoRoot;                // coalesce: only the newest root matters
        m_hasPendingRefresh = true;
    }
    m_cv.notify_all();
}

void GitIntegration::RunAction(const std::wstring& repoRoot, GitAction action, const std::wstring& arg) {
    if (repoRoot.empty() || action == GitAction::CopyHashOnlyNoop) return;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (action == GitAction::ShowCommit || action == GitAction::ShowDiffStat) {
            // A newer "show" supersedes queued ones (the user clicked through several commits quickly).
            m_actions.erase(std::remove_if(m_actions.begin(), m_actions.end(), [](const Request& r) {
                return r.action == GitAction::ShowCommit || r.action == GitAction::ShowDiffStat; }), m_actions.end());
        }
        Request r;
        r.kind = Request::Kind::Action;
        r.root = repoRoot; r.action = action; r.arg = arg;
        m_actions.push_back(std::move(r));
        ++m_version;                                    // IsBusy() just became true
    }
    m_cv.notify_all();
}

bool GitIntegration::IsBusy() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_running || m_hasPendingRefresh || !m_actions.empty();
}

std::wstring GitIntegration::BusyDescription() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_running) return m_busyDescription;
    if (!m_actions.empty() || m_hasPendingRefresh) return L"git ...";
    return {};
}

GitRepoState GitIntegration::SnapshotState() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_state;
}

std::wstring GitIntegration::SnapshotOutput() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_output;
}

bool GitIntegration::LastActionSucceeded() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_lastOk;
}

uint64_t GitIntegration::StateVersion() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_version;
}

// =================================================================================================================
// Worker
// =================================================================================================================
void GitIntegration::SetBusy(const std::wstring& description) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_busyDescription = description;
    ++m_version;
}

void GitIntegration::SetOutput(std::wstring text, bool ok) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_output = std::move(text);
    m_lastOk = ok;
    ++m_version;
}

void GitIntegration::WorkerMain() {
    std::unique_lock<std::mutex> lk(m_mutex);
    for (;;) {
        m_cv.wait(lk, [this] { return m_stop.load() || !m_actions.empty() || m_hasPendingRefresh; });
        if (m_stop) break;

        Request req;
        if (!m_actions.empty()) {                       // actions first: the user is waiting for their output
            req = std::move(m_actions.front());
            m_actions.pop_front();
        } else {
            req.kind = Request::Kind::Refresh;
            req.root = m_pendingRefreshRoot;
            m_hasPendingRefresh = false;                // requests arriving while we run schedule one more pass
        }
        m_running = true;
        ++m_version;
        lk.unlock();

        if (req.kind == Request::Kind::Action) DoAction(req);
        else DoRefresh(req.root);

        lk.lock();
        m_running = false;
        m_busyDescription.clear();
        ++m_version;
    }
}

void GitIntegration::DoRefresh(const std::wstring& root) {
    SetBusy(L"git status");
    GitRepoState prev;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_state.root == root) prev = m_state;
    }
    GitRepoState s = prev;                              // fall back to the previous snapshot on soft failures
    s.root = root;
    s.gitMissing = false;

    auto publish = [&](GitRepoState& st) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_hasPendingRefresh && m_pendingRefreshRoot != root) return;    // user already moved to another repo
        m_state = st;
        ++m_version;
    };
    auto fail = [&](const std::wstring& msg, bool invalidate) {
        s.lastError = msg;
        if (invalidate || !prev.valid) s.valid = false;
        publish(s);
    };

    if (!GitIntegration::IsGitInstalled()) { s = GitRepoState(); s.root = root; s.gitMissing = true; publish(s); return; }

    // All queries are independent and each costs a process start (~60 ms on Windows), so they run concurrently
    // on short-lived helper threads, each writing only to its own result slot (RunGit is re-entrant). Helpers are
    // always joined before this function returns and they honour m_stop, so destruction stays bounded.
    RunResult st, lg, br, sh, rm;
    {
        RunOptions statusOpt;
        statusOpt.timeoutMs = kTimeoutStatusMs;
        RunOptions plain;
        std::vector<std::thread> helpers;
        auto spawn = [&](RunResult& dst, const RunOptions& o, std::vector<std::wstring> args) {
            helpers.emplace_back([&dst, root, o, args = std::move(args), this] { dst = RunGit(root, args, o, m_stop); });
        };
        spawn(st, statusOpt, { L"status", L"--porcelain=v1", L"-b", L"-z" });
        // `log` fails on an empty repository ("does not have any commits yet"): handled below, not an error.
        spawn(lg, plain, { L"log", L"-n", std::to_wstring(kMaxCommits), L"--no-color",
                           L"--pretty=format:%H%x1f%h%x1f%an%x1f%ar%x1f%ct%x1f%D%x1f%s%x1e" });
        // NOTE: for-each-ref has its own escape syntax: "%1f" (not "%x1f" as in log).
        spawn(br, plain, { L"for-each-ref", L"--sort=refname",
                           L"--format=%(HEAD)%1f%(refname)%1f%(upstream:short)%1f%(upstream:track)%1f%(symref)%1f%(contents:subject)",
                           L"refs/heads", L"refs/remotes" });
        spawn(sh, plain, { L"stash", L"list", L"--format=%h" });
        spawn(rm, plain, { L"remote", L"get-url", L"origin" });
        for (auto& t : helpers) t.join();
    }
    if (m_stop) return;

    if (st.notFound) { s = GitRepoState(); s.root = root; s.gitMissing = true; publish(s); return; }
    if (!st.started) { fail(L"Cannot start git", false); return; }
    if (st.timedOut) { fail(L"git status timed out", false); return; }
    if (!st.ok()) {
        std::wstring err = FirstLine(Utf8ToWide(st.err));
        if (err.empty()) err = L"git status failed";
        fail(err, Contains(err, L"not a git repository") || Contains(err, L"dubious ownership"));
        return;
    }

    GitRepoState n;                                     // build fresh, publish once everything is known
    n.root = root;
    ParseStatus(Utf8ToWide(st.out), n);
    if (lg.ok()) ParseLog(Utf8ToWide(lg.out), n.commits);
    else if (!lg.timedOut && !Contains(Utf8ToWide(lg.err), L"does not have any commits")) n.lastError = FirstLine(Utf8ToWide(lg.err));
    if (!n.commits.empty()) n.headShort = n.commits.front().shortHash;

    if (br.ok()) {
        // Full refnames were requested so local / remote can be told apart reliably; shorten them here.
        std::vector<GitBranch> all;
        ParseBranches(Utf8ToWide(br.out), all);
        std::vector<GitBranch> locals, remotes;
        for (auto& b : all) {
            if (b.name.rfind(L"refs/heads/", 0) == 0) { b.name.erase(0, 11); locals.push_back(std::move(b)); }
            else if (b.name.rfind(L"refs/remotes/", 0) == 0) { b.name.erase(0, 13); b.isRemote = true; b.upstream.clear(); remotes.push_back(std::move(b)); }
        }
        n.branches = std::move(locals);
        for (auto& r : remotes) n.branches.push_back(std::move(r));
        if (n.branches.size() > kMaxBranches) n.branches.resize(kMaxBranches);
    }

    if (sh.ok()) {
        int count = 0;
        for (char c : sh.out) if (c == '\n') ++count;
        if (!sh.out.empty() && sh.out.back() != '\n') ++count;
        n.stashCount = count;
    }
    if (rm.ok()) { n.remoteUrl = Utf8ToWide(rm.out); TrimInPlace(n.remoteUrl); }

    n.valid = true;
    n.refreshedTick = GetTickCount64();
    publish(n);
}

void GitIntegration::DoAction(const Request& req) {
    const std::wstring& root = req.root;
    const std::atomic<bool>& stop = m_stop;
    std::vector<std::wstring> args;
    RunOptions ro;
    ro.mergeStderr = true;
    ro.maxBytes = 4u << 20;
    bool mutating = false;

    switch (req.action) {
    case GitAction::Fetch:
        args = { L"fetch", L"--all", L"--prune" };
        ro.readOnly = false; ro.timeoutMs = kTimeoutNetworkMs; mutating = true; break;
    case GitAction::Pull:
        args = { L"pull", L"--ff-only" };
        ro.readOnly = false; ro.timeoutMs = kTimeoutNetworkMs; mutating = true; break;
    case GitAction::Push:
        args = { L"push" };
        ro.readOnly = false; ro.timeoutMs = kTimeoutNetworkMs; mutating = true; break;
    case GitAction::ShowCommit:
        if (!IsSafeRefArg(req.arg)) { SetOutput(L"Invalid commit id.", false); return; }
        args = { L"show", L"--stat", L"--format=fuller", L"--no-color", req.arg };
        break;
    case GitAction::ShowDiffStat:
        args = { L"diff", L"--stat", L"--no-color" };
        break;
    case GitAction::Checkout: {
        if (!IsSafeRefArg(req.arg)) { SetOutput(L"Invalid branch name.", false); return; }
        mutating = true;
        ro.readOnly = false;
        // Remote branch ("origin/x") that has no local namesake -> create a tracking branch.
        RunOptions probe;
        probe.maxBytes = 4096;
        RunResult isLocal = RunGit(root, { L"show-ref", L"--verify", L"--quiet", L"refs/heads/" + req.arg }, probe, stop);
        if (isLocal.cancelled) return;
        bool remote = false;
        if (!isLocal.ok()) {
            RunResult isRemote = RunGit(root, { L"show-ref", L"--verify", L"--quiet", L"refs/remotes/" + req.arg }, probe, stop);
            if (isRemote.cancelled) return;
            remote = isRemote.ok();
        }
        if (remote) args = { L"switch", L"--track", req.arg };
        else args = { L"switch", req.arg };       // git refuses (and explains) if local changes would be overwritten
        break;
    }
    default:
        return;
    }

    std::wstring desc = L"git";
    for (const auto& a : args) { desc += L' '; desc += a; }
    SetBusy(desc);

    RunResult r = RunGit(root, args, ro, stop);
    if (r.cancelled) return;

    std::wstring text = NormalizeOutputText(Utf8ToWide(r.out));
    bool ok = r.ok();
    if (r.notFound) { text = L"git.exe was not found."; }
    else if (!r.started) { text = L"Cannot start git."; }
    else if (r.timedOut) {
        if (!text.empty()) text += L"\n";
        text += L"[git timed out after " + std::to_wstring(ro.timeoutMs / 1000) + L" s and was terminated]";
    } else if (!ok && text.empty()) {
        text = L"git failed (exit code " + std::to_wstring((int)r.exitCode) + L").";
    } else if (ok && text.empty() && req.action != GitAction::ShowDiffStat) {
        text = L"Done.";
    }
    SetOutput(std::move(text), ok);

    if (mutating) RequestRefresh(root);                 // branch / ahead-behind / file list changed
}

} // namespace Explorer2
