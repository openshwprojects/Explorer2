#include "GitIntegration.h"
#include "SvnWorkingCopy.h"
#include "ToolProcess.h"

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
constexpr DWORD  kTimeoutSvnServerMs = 60 * 1000;   // svn log / svn status -u ask the repository server
constexpr ULONGLONG kSvnLogMaxAgeMs = 5 * 60 * 1000; // a Subversion history is read from the server at most this often
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

std::wstring ProbeGit() {
    using ToolProcess::GetEnv;
    std::vector<std::wstring> candidates;
    for (const std::wstring& r : { GetEnv(L"ProgramFiles"), GetEnv(L"ProgramFiles(x86)"), std::wstring(L"C:\\Program Files"),
                                   GetEnv(L"LOCALAPPDATA") + L"\\Programs" }) {
        if (r.empty() || r == L"\\Programs") continue;
        candidates.push_back(r + L"\\Git\\cmd\\git.exe");
    }
    return ToolProcess::FindExecutable(L"git.exe", candidates);
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

using ToolProcess::FirstLine;

bool Contains(const std::wstring& hay, const wchar_t* needle) { return hay.find(needle) != std::wstring::npos; }

// Branch names / hashes come from the UI: reject anything git could mistake for an option or that is odd.
bool IsSafeRefArg(const std::wstring& a) {
    if (a.empty() || a[0] == L'-') return false;
    for (wchar_t c : a) if (c < 0x20 || c == 0x7F) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Process runner (ToolProcess does the work; git adds its fixed prefix and environment)
// ---------------------------------------------------------------------------------------------------------------
struct RunOptions {
    bool readOnly = true;           // adds --no-optional-locks and GIT_OPTIONAL_LOCKS=0
    bool mergeStderr = false;       // stderr shares the stdout pipe (human readable action output)
    DWORD timeoutMs = kTimeoutNormalMs;
    size_t maxBytes = kMaxCapture;
};

using RunResult = ToolProcess::Result;

RunResult RunGit(const std::wstring& root, const std::vector<std::wstring>& cmdArgs, const RunOptions& opt,
                 const std::atomic<bool>& stop) {
    const std::wstring gitPath = GetGitPath();
    if (gitPath.empty()) { RunResult res; res.notFound = true; return res; }

    std::vector<std::wstring> args = { L"-C", root, L"-c", L"core.quotepath=false", L"-c", L"i18n.logOutputEncoding=UTF-8" };
    if (opt.readOnly) args.push_back(L"--no-optional-locks");
    args.insert(args.end(), cmdArgs.begin(), cmdArgs.end());

    ToolProcess::Options o;
    o.mergeStderr = opt.mergeStderr;
    o.timeoutMs = opt.timeoutMs;
    o.maxBytes = opt.maxBytes;
    GitIntegration::ApplyGitEnvironment(o, opt.readOnly);
    return ToolProcess::Run(gitPath, args, o, stop);
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
std::wstring GitIntegration::GitExePath() { return GetGitPath(); }

void GitIntegration::ApplyGitEnvironment(ToolProcess::Options& o, bool readOnly) {
    o.envOverride = { L"GIT_TERMINAL_PROMPT=0",         // never wait for a username/password on the (invisible) console
                      L"GCM_INTERACTIVE=never" };       // Git Credential Manager: no GUI prompt either
    if (readOnly) o.envOverride.push_back(L"GIT_OPTIONAL_LOCKS=0");
    o.envDefault = { L"GIT_SSH_COMMAND=ssh -o BatchMode=yes" };   // ssh must not ask for passphrases
}

std::wstring GitIntegration::FindRepoRoot(const std::wstring& folder, VcsKind* kind) {
    if (kind) *kind = VcsKind::Git;
    if (folder.empty() || IsVirtualFolder(folder) || folder.find(L"://") != std::wstring::npos) return {};
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
            std::wstring base = cur;
            if (base.back() != L'\\') base += L'\\';
            if (GetFileAttributesW((base + L".git").c_str()) != INVALID_FILE_ATTRIBUTES) return cur;   // dir or file
            // Subversion 1.7 and later keep a single .svn folder, at the root of the working copy
            const DWORD svnAttr = GetFileAttributesW((base + L".svn").c_str());
            if (svnAttr != INVALID_FILE_ATTRIBUTES && (svnAttr & FILE_ATTRIBUTE_DIRECTORY)) {
                if (kind) *kind = VcsKind::Svn;
                return cur;
            }
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
        m_abandon = true;               // the same for svn
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

// =================================================================================================================
// Public API
// =================================================================================================================
void GitIntegration::RequestRefresh(const std::wstring& repoRoot, VcsKind kind, bool full) {
    if (repoRoot.empty()) return;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_state.root != repoRoot || m_state.kind != kind) {   // never show another repo's data under the new root
            m_state = GitRepoState();
            m_state.root = repoRoot;
            m_state.kind = kind;
            ++m_version;
        }
        // coalesce: only the newest root matters (and a full refresh asked for while one is pending stays full)
        m_pendingRefreshFull = (m_hasPendingRefresh && m_pendingRefreshRoot == repoRoot && m_pendingRefreshFull) || full;
        m_pendingRefreshRoot = repoRoot;
        m_pendingRefreshKind = kind;
        m_hasPendingRefresh = true;
        if (m_running && !m_runningRoot.empty() && m_runningRoot != repoRoot) m_abandon = true;
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
        r.vcs = (m_state.root == repoRoot) ? m_state.kind : VcsKind::Git;   // actions act on the repo being shown
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
            req.vcs = m_pendingRefreshKind;
            req.full = m_pendingRefreshFull;
            m_hasPendingRefresh = false;                // requests arriving while we run schedule one more pass
            m_pendingRefreshFull = false;
        }
        m_running = true;
        m_runningRoot = req.root;
        m_abandon = m_stop.load();
        ++m_version;
        lk.unlock();

        if (req.kind == Request::Kind::Action) {
            if (req.vcs == VcsKind::Svn) DoActionSvn(req);
            else DoAction(req);
        } else if (req.vcs == VcsKind::Svn) {
            DoRefreshSvn(req.root, req.full);
        } else {
            DoRefresh(req.root);
        }

        lk.lock();
        m_running = false;
        m_runningRoot.clear();
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
    s.clientMissing = false;

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

    if (!GitIntegration::IsGitInstalled()) { s = GitRepoState(); s.root = root; s.clientMissing = true; publish(s); return; }

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

    if (st.notFound) { s = GitRepoState(); s.root = root; s.clientMissing = true; publish(s); return; }
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

// =================================================================================================================
// Subversion working copies (read-only)
// =================================================================================================================
void GitIntegration::DoRefreshSvn(const std::wstring& root, bool full) {
    SetBusy(L"svn status");
    GitRepoState prev;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_state.root == root && m_state.kind == VcsKind::Svn) prev = m_state;
    }
    auto publish = [&](const GitRepoState& st) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_hasPendingRefresh && m_pendingRefreshRoot != root) return;    // user already moved to another folder
        m_state = st;
        ++m_version;
    };
    auto fail = [&](const std::wstring& msg, bool invalidate) {
        GitRepoState s = prev;                          // fall back to the previous snapshot on soft failures
        s.root = root;
        s.kind = VcsKind::Svn;
        s.clientMissing = false;
        s.lastError = msg.empty() ? std::wstring(L"svn failed") : msg;
        if (invalidate || !prev.valid) s.valid = false;
        publish(s);
    };
    auto missing = [&]() {
        GitRepoState s;
        s.root = root;
        s.kind = VcsKind::Svn;
        s.clientMissing = true;
        publish(s);
    };
    if (Svn::FindSvnExe().empty()) { missing(); return; }

    // `svn info` and `svn status` read only the working copy, so they are quick; both run at once
    RunResult info, st;
    {
        std::thread infoThread([&] { info = Svn::Run(root, { L"info", L"--xml" }, kTimeoutNormalMs, m_abandon); });
        st = Svn::Run(root, { L"status", L"--xml", L"--ignore-externals" }, kTimeoutStatusMs, m_abandon, kMaxCapture);
        infoThread.join();
    }
    if (m_stop || m_abandon) return;                   // shutting down, or another folder is wanted
    if (info.notFound || st.notFound) { missing(); return; }
    if (!info.started || !st.started) { fail(L"Cannot start svn", false); return; }
    if (info.timedOut || st.timedOut) { fail(L"svn timed out", false); return; }
    if (!info.ok()) {
        const std::wstring err = FirstLine(ToolProcess::DecodeOutput(info.err));
        // E155007: not a working copy; E155021 / E155036: written by a newer / older client than this svn.exe
        fail(err, Contains(err, L"E155007") || Contains(err, L"E155021") || Contains(err, L"E155036"));
        return;
    }

    GitRepoState n;                                     // build fresh, publish once everything local is known
    n.kind = VcsKind::Svn;
    n.root = root;
    if (!Svn::ParseInfoXml(info.out, n.svn)) { fail(L"svn info printed nothing that could be read", false); return; }
    n.branch = Svn::PathInRepository(n.svn);
    n.remoteUrl = n.svn.url;
    n.headShort = L"r" + std::to_wstring(n.svn.revision);
    if (!st.ok()) {                                     // e.g. the working copy's database is busy: keep what was shown
        fail(FirstLine(ToolProcess::DecodeOutput(st.err)), false);
        return;
    }
    Svn::ParseStatusXml(st.out, n, kMaxChanges);

    // The history comes from the server: shown from the previous read while that is recent and the working copy
    // is still at the same revision; otherwise the local state is shown first and the history follows
    const ULONGLONG now = GetTickCount64();
    const bool logFresh = prev.valid && prev.logTick != 0 && prev.svn.revision == n.svn.revision &&
                          now - prev.logTick < kSvnLogMaxAgeMs;
    if (prev.valid) {
        n.commits = prev.commits;
        n.logError = prev.logError;
        n.logTick = prev.logTick;
    }
    n.valid = true;
    n.refreshedTick = now;
    publish(n);
    if (logFresh && !full) return;

    SetBusy(L"svn log");
    RunResult lg = Svn::Run(root, { L"log", L"--xml", L"-l", std::to_wstring(kMaxCommits) }, kTimeoutSvnServerMs, m_abandon);
    if (m_stop || m_abandon || lg.cancelled) return;
    if (lg.ok()) {
        Svn::ParseLogXml(lg.out, n.commits, Svn::NowUnix());
        n.logError.clear();
    } else {
        n.logError = lg.timedOut ? std::wstring(L"The server did not answer (svn log timed out).")
                                 : FirstLine(ToolProcess::DecodeOutput(lg.err));
        if (n.logError.empty()) n.logError = L"svn log failed";
    }
    n.logTick = GetTickCount64();                       // after a failure too: asked again in a few minutes, not every 20 s
    publish(n);
}

void GitIntegration::DoActionSvn(const Request& req) {
    std::vector<std::wstring> args;
    DWORD timeout = kTimeoutNormalMs;
    size_t maxBytes = 16u << 20;
    switch (req.action) {
    case GitAction::ShowCommit: {
        const bool digits = !req.arg.empty() && req.arg.size() <= 10 &&
                            std::all_of(req.arg.begin(), req.arg.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
        if (!digits) { SetOutput(L"Invalid revision number.", false); return; }
        args = { L"log", L"-v", L"--xml", L"-r", req.arg };
        timeout = kTimeoutSvnServerMs;
        break;
    }
    case GitAction::ShowDiffStat:
        // --internal-diff: a diff program set in the user's svn configuration must not be started (it may be a GUI)
        args = { L"diff", L"--internal-diff" };
        timeout = kTimeoutStatusMs;
        maxBytes = 32u << 20;
        break;
    case GitAction::CheckUpdates:
        args = { L"status", L"-u", L"--xml", L"--ignore-externals" };
        timeout = kTimeoutSvnServerMs;
        break;
    default:
        SetOutput(L"This action is not available for Subversion working copies: Explorer2 only reads them.", false);
        return;
    }

    std::wstring desc = L"svn";
    for (const auto& a : args) { desc += L' '; desc += a; }
    SetBusy(desc);

    RunResult r = Svn::Run(req.root, args, timeout, m_stop, maxBytes);
    if (r.cancelled) return;

    std::wstring text;
    const bool ok = r.ok();
    if (r.notFound) {
        text = L"svn.exe was not found.";
    } else if (!r.started) {
        text = L"Cannot start svn.";
    } else if (r.timedOut) {
        text = L"[svn timed out after " + std::to_wstring(timeout / 1000) + L" s and was terminated]";
    } else if (!ok) {
        text = NormalizeOutputText(ToolProcess::DecodeOutput(r.err));
        if (text.empty()) text = L"svn failed (exit code " + std::to_wstring(static_cast<int>(r.exitCode)) + L").";
    } else if (req.action == GitAction::ShowCommit) {
        text = Svn::FormatRevisionText(r.out);
    } else if (req.action == GitAction::CheckUpdates) {
        text = Svn::FormatUpdatesText(r.out);
    } else {
        text = Svn::DiffStatText(r.out);
        if (text.empty()) text = L"No local changes.";
        else if (r.out.size() >= maxBytes) text += L"\n(the diff was larger than " + std::to_wstring(maxBytes >> 20) + L" MB; this covers its beginning)";
    }
    SetOutput(NormalizeOutputText(text), ok);
}

} // namespace Explorer2
