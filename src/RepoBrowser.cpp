#include "RepoBrowser.h"

#include "FileEngine.h"
#include "GitIntegration.h"
#include "PathUtils.h"
#include "SvnWorkingCopy.h"
#include "ToolProcess.h"
#include "XmlTagScan.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include <shlobj.h>

namespace Explorer2::RepoBrowser {

namespace {

namespace X = XmlTagScan;

constexpr DWORD kInfoTimeoutMs  = 30 * 1000;        // svn info, git ls-remote
constexpr DWORD kListTimeoutMs  = 60 * 1000;        // svn list, git ls-tree
constexpr DWORD kCloneTimeoutMs = 15 * 60 * 1000;   // the first visit of a big git repository
constexpr DWORD kFetchTimeoutMs = 5 * 60 * 1000;    // git fetch, and reading one file

// One entry of a listing, before it is shown with the user's settings (extensions, language)
struct RawItem {
    std::wstring name;
    bool isDir = false;
    bool sizeKnown = false;
    uint64_t size = 0;
    int64_t unixTime = 0;       // last change; 0 = not known (git)
    bool submodule = false;
};

struct Listing {
    ListState state = ListState::Loading;
    Kind kind = Kind::Unknown;
    std::vector<RawItem> items;
    std::wstring message;
};

struct Root {
    std::wstring url;
    Kind kind = Kind::Unknown;
    size_t refParts = 1;        // git: how many parts of a ".../tree/<ref>" address the branch name takes
};

struct GitRepo {
    std::wstring dir;           // the clone in %TEMP%\Explorer2_Repos
    std::wstring commit;        // what is shown
    bool ready = false;
};

std::mutex g_mutex;
std::condition_variable g_cv;
std::map<std::wstring, Listing> g_listings;          // key: Key(url)
std::deque<std::wstring> g_queue;
std::vector<Root> g_roots;                           // repository roots met so far
std::map<std::wstring, GitRepo> g_git;               // key: repoUrl + "#" + ref
std::map<std::wstring, int64_t> g_svnFileRevs;       // file URL key -> last changed revision (for its temp copy)
std::thread g_worker;
std::atomic<bool> g_stop{ false };
bool g_started = false;
bool g_closed = false;                               // Shutdown at program exit: the worker is not started again
HWND g_notify = nullptr;
std::wstring g_cacheRoot;                            // empty = %TEMP%
// One git file read at a time: each may make git fetch into the shallow clone, and two fetches at once would trip
// over git's lock files (thumbnails ask for several files together)
std::mutex g_gitFetchMutex;

// ---------------------------------------------------------------------------------------------------------------
// URLs
// ---------------------------------------------------------------------------------------------------------------
std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring SchemeOf(const std::wstring& url) {
    const size_t p = url.find(L"://");
    return (p == std::wstring::npos || p == 0) ? std::wstring() : Lower(url.substr(0, p));
}

// The URL without surrounding blanks and trailing slashes (which do not change what it names)
std::wstring Key(const std::wstring& url) {
    std::wstring u = PathUtils::Trim(url);
    const size_t schemeEnd = u.find(L"://");
    const size_t keep = (schemeEnd == std::wstring::npos) ? 0 : schemeEnd + 3;
    while (u.size() > keep && (u.back() == L'/' || u.back() == L'\\')) u.pop_back();
    return u;
}

// Index of the '/' that starts the path (after "scheme://host"), npos when there is no path
size_t PathStart(const std::wstring& u) {
    const size_t schemeEnd = u.find(L"://");
    if (schemeEnd == std::wstring::npos) return std::wstring::npos;
    if (SchemeOf(u) == L"file") return schemeEnd + 3;                 // file:///C:/x - the path starts at once
    return u.find(L'/', schemeEnd + 3);
}

std::wstring HostOf(const std::wstring& u) {
    const size_t schemeEnd = u.find(L"://");
    if (schemeEnd == std::wstring::npos) return {};
    const size_t start = schemeEnd + 3;
    const size_t end = u.find(L'/', start);
    std::wstring host = u.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    const size_t at = host.rfind(L'@');                               // user@host
    if (at != std::wstring::npos) host.erase(0, at + 1);
    return Lower(host);
}

// "%20" and the like, back to text (the bytes are UTF-8); other characters stay as they are
std::wstring DecodePct(const std::wstring& s) {
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    std::wstring out;
    std::string bytes;
    auto flush = [&]() {
        if (!bytes.empty()) { out += PathUtils::Utf8ToWide(bytes); bytes.clear(); }
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            bytes.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
            i += 2;
        } else {
            flush();
            out.push_back(s[i]);
        }
    }
    flush();
    return out;
}

// Characters a name may hold that would change what a URL means; everything else stays readable
std::wstring EncodeName(const std::wstring& name) {
    std::wstring out;
    for (wchar_t c : name) {
        switch (c) {
        case L' ': out += L"%20"; break;
        case L'#': out += L"%23"; break;
        case L'?': out += L"%3F"; break;
        case L'%': out += L"%25"; break;
        default:
            if (c < 0x20) { wchar_t b[4] = {}; swprintf_s(b, L"%%%02X", static_cast<unsigned>(c)); out += b; }
            else out.push_back(c);
        }
    }
    return out;
}

bool StartsWithFolder(const std::wstring& url, const std::wstring& root) {
    if (url.size() < root.size() || _wcsnicmp(url.c_str(), root.c_str(), root.size()) != 0) return false;
    return url.size() == root.size() || url[root.size()] == L'/';
}

// The longest repository root met so far that holds `url` (call with g_mutex held)
const Root* KnownRootLocked(const std::wstring& url) {
    const Root* best = nullptr;
    for (const auto& r : g_roots) {
        if (StartsWithFolder(url, r.url) && (!best || r.url.size() > best->url.size())) best = &r;
    }
    return best;
}

void AddRootLocked(const std::wstring& url, Kind kind, size_t refParts = 1) {
    const std::wstring k = Key(url);
    for (auto& r : g_roots) {
        if (_wcsicmp(r.url.c_str(), k.c_str()) == 0) { r.kind = kind; r.refParts = refParts; return; }
    }
    g_roots.push_back({ k, kind, refParts });
}

void ForgetRootLocked(const std::wstring& url) {
    const std::wstring k = Key(url);
    g_roots.erase(std::remove_if(g_roots.begin(), g_roots.end(),
                                 [&](const Root& r) { return _wcsicmp(r.url.c_str(), k.c_str()) == 0; }),
                  g_roots.end());
}

// ---------------------------------------------------------------------------------------------------------------
// Files and folders
// ---------------------------------------------------------------------------------------------------------------
std::wstring TempRoot() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_cacheRoot.empty()) return g_cacheRoot;
    }
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp);
}

std::wstring Hash16(const std::wstring& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char b : PathUtils::WideToUtf8(s)) { h ^= b; h *= 1099511628211ULL; }
    wchar_t buf[20] = {};
    swprintf_s(buf, L"%016llx", static_cast<unsigned long long>(h));
    return buf;
}

void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring p = dir + L"\\" + name;
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);       // git keeps its pack files read-only
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) RemoveTree(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

std::wstring SafeFileName(std::wstring name) {
    for (auto& c : name) {
        if (c < 0x20 || wcschr(L"<>:\"/\\|?*", c)) c = L'_';
    }
    while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
    return name.empty() ? std::wstring(L"file") : name;
}

FILETIME UnixToFileTime(int64_t t) {
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<ULONGLONG>(t) * 10000000ULL + 116444736000000000ULL;
    return FILETIME{ u.LowPart, u.HighPart };
}

std::wstring LocalDateText(int64_t t) {
    FILETIME ft = UnixToFileTime(t);
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&ft, &utc)) return {};
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
    wchar_t b[32] = {};
    swprintf_s(b, L"%02u.%02u.%04u %02u:%02u", local.wDay, local.wMonth, local.wYear, local.wHour, local.wMinute);
    return b;
}

FileEntry MakeEntry(const std::wstring& dirUrl, const RawItem& raw, Kind kind, bool showExtensions, Lang lang) {
    const bool pl = (lang == Lang::PL);
    const wchar_t* tag = (kind == Kind::Git) ? L" (git)" : L" (SVN)";
    FileEntry e;
    e.name = raw.name;
    e.fullPath = JoinUrl(dirUrl, raw.name);
    e.normKey = PathUtils::NormalizePathKey(e.fullPath);
    e.isDirectory = raw.isDir;
    if (raw.unixTime > 0) {
        e.writeTime = UnixToFileTime(raw.unixTime);
        e.dateStr = LocalDateText(raw.unixTime);
    }
    if (raw.isDir) {
        e.displayName = e.name;
        e.typeName = raw.submodule ? (pl ? L"Moduł podrzędny git (inne repozytorium)" : L"git submodule (another repository)")
                                   : std::wstring(pl ? L"Folder plików" : L"File folder") + tag;
        return e;
    }
    const size_t dot = e.name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        e.ext = PathUtils::ToLower(e.name.substr(dot));
        e.displayName = showExtensions ? e.name : e.name.substr(0, dot);
    } else {
        e.displayName = e.name;
    }
    e.isImage = IsImageExtension(e.ext);
    e.isVideo = IsVideoExtension(e.ext);
    e.isAudio = IsAudioExtension(e.ext);
    if (raw.sizeKnown) {
        e.size = raw.size;
        e.sizeStr = FileEngine::FormatSizeKB(raw.size);
    }
    if (!e.ext.empty()) {
        std::wstring up = e.ext.substr(1);
        for (auto& c : up) c = static_cast<wchar_t>(std::towupper(c));
        e.typeName = (pl ? L"Plik " : L"File ") + up + tag;
    } else {
        e.typeName = std::wstring(pl ? L"Plik" : L"File") + tag;
    }
    return e;
}

// ---------------------------------------------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------------------------------------------
std::wstring Err(const ToolProcess::Result& r, const wchar_t* tool) {
    if (r.notFound) return std::wstring(tool) + L" was not found.";
    if (r.timedOut) return std::wstring(L"The server did not answer in time (") + tool + L").";
    std::wstring e = ToolProcess::FirstLine(ToolProcess::DecodeOutput(r.err));
    return e.empty() ? std::wstring(tool) + L" failed." : e;
}

ToolProcess::Result RunGitTool(const std::vector<std::wstring>& args, DWORD timeoutMs, bool readOnly,
                               const std::wstring& stdoutFile = {}) {
    ToolProcess::Options o;
    o.timeoutMs = timeoutMs;
    o.stdoutFile = stdoutFile;
    GitIntegration::ApplyGitEnvironment(o, readOnly);
    std::vector<std::wstring> all = { L"-c", L"core.quotepath=false" };
    all.insert(all.end(), args.begin(), args.end());
    return ToolProcess::Run(GitIntegration::GitExePath(), all, o, g_stop);
}

ToolProcess::Result RunSvnTool(const std::vector<std::wstring>& args, DWORD timeoutMs, const std::wstring& stdoutFile = {}) {
    const std::wstring exe = Svn::FindSvnExe();
    std::vector<std::wstring> all = args;
    all.push_back(L"--non-interactive");
    ToolProcess::Options o;
    o.timeoutMs = timeoutMs;
    o.stdoutFile = stdoutFile;
    return ToolProcess::Run(exe, all, o, g_stop);
}

// svn reads "url@rev" as a peg revision: a final "@" keeps an "@" in a name from being taken for one
std::wstring Peg(const std::wstring& url) { return url + L"@"; }

// ---------------------------------------------------------------------------------------------------------------
// Which kind a URL is, and its repository root
// ---------------------------------------------------------------------------------------------------------------
Kind KindOf(const std::wstring& url, std::wstring* rootOut = nullptr, size_t* refPartsOut = nullptr);   // below

Kind ResolveKind(const std::wstring& url, std::wstring& message) {
    const std::wstring u = Key(url);
    std::wstring knownRoot;
    const Kind guess = KindOf(u, &knownRoot);
    if (!knownRoot.empty()) return guess;                           // a repository met before holds it
    const bool haveSvn = !Svn::FindSvnExe().empty();
    const bool haveGit = GitIntegration::IsGitInstalled();
    if (guess == Kind::Git) {
        if (!haveGit) { message = L"git is not installed: it is needed to browse git repositories."; return Kind::Unknown; }
        GitUrlParts parts;
        SplitGitUrlText(u, 1, parts);
        size_t refParts = 1;
        if (!parts.ref.empty() && !parts.innerPath.empty()) {
            // A branch name may hold '/' (".../tree/releases/v2/src"): the server's branch and tag names say where it ends
            const ToolProcess::Result ls = RunGitTool({ L"ls-remote", L"--heads", L"--tags", parts.repoUrl }, kInfoTimeoutMs, true);
            if (g_stop) return Kind::Unknown;
            if (ls.ok()) refParts = RefParts(parts.ref + L"/" + parts.innerPath, ls.out);
            if (refParts > 1) SplitGitUrlText(u, refParts, parts);
        }
        std::lock_guard<std::mutex> lk(g_mutex);
        AddRootLocked(parts.base, Kind::Git, refParts);
        return Kind::Git;
    }
    // Subversion (or not known yet): `svn info` says whether it is a repository, and where its root is
    std::wstring svnError;
    if (haveSvn) {
        const ToolProcess::Result info = RunSvnTool({ L"info", L"--xml", Peg(url) }, kInfoTimeoutMs);
        if (g_stop) return Kind::Unknown;
        SvnInfo si;
        if (info.ok() && Svn::ParseInfoXml(info.out, si)) {
            std::lock_guard<std::mutex> lk(g_mutex);
            AddRootLocked(si.repositoryRoot.empty() ? url : si.repositoryRoot, Kind::Svn);
            return Kind::Svn;
        }
        svnError = Err(info, L"svn");
    }
    if (guess == Kind::Svn) {
        message = haveSvn ? svnError : L"Subversion (svn.exe) is not installed: it is needed to browse Subversion repositories.";
        return Kind::Unknown;
    }
    std::wstring gitError;
    if (haveGit) {
        const ToolProcess::Result ls = RunGitTool({ L"ls-remote", L"--symref", url, L"HEAD" }, kInfoTimeoutMs, true);
        if (g_stop) return Kind::Unknown;
        if (ls.ok()) {
            std::lock_guard<std::mutex> lk(g_mutex);
            AddRootLocked(url, Kind::Git);
            return Kind::Git;
        }
        gitError = Err(ls, L"git");
    }
    if (!haveSvn && !haveGit) {
        message = L"Install Subversion (svn.exe) or git to browse repositories.";
    } else {
        message = L"This address is not a Subversion or git repository that can be read.";
        if (!svnError.empty()) message += L"\nsvn: " + svnError;
        if (!gitError.empty()) message += L"\ngit: " + gitError;
    }
    return Kind::Unknown;
}

// ---------------------------------------------------------------------------------------------------------------
// git: the shallow clone that holds the folder structure
// ---------------------------------------------------------------------------------------------------------------
std::wstring GitKey(const GitUrlParts& p) { return p.repoUrl + L"#" + p.ref; }

// Makes the clone (or brings it up to date) on the worker thread; false with a message when that fails
bool EnsureGitRepo(const GitUrlParts& parts, GitRepo& out, std::wstring& message) {
    const std::wstring key = GitKey(parts);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_git.find(key);
        if (it != g_git.end() && it->second.ready) { out = it->second; return true; }
    }
    const std::wstring dir = TempRoot() + L"Explorer2_Repos\\" + Hash16(key);
    SHCreateDirectoryExW(nullptr, (TempRoot() + L"Explorer2_Repos").c_str(), nullptr);   // with the folders above it
    std::wstring commit;
    const DWORD gitAttr = GetFileAttributesW((dir + L"\\.git").c_str());
    if (gitAttr != INVALID_FILE_ATTRIBUTES && (gitAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        // A clone from an earlier visit: fetch what is new; offline, show what it has
        std::lock_guard<std::mutex> fetchLock(g_gitFetchMutex);       // not while a file is being fetched into it
        const ToolProcess::Result f = RunGitTool({ L"-C", dir, L"fetch", L"--quiet", L"--depth", L"1", L"--filter=blob:none", L"origin",
                                                   parts.ref.empty() ? std::wstring(L"HEAD") : parts.ref }, kFetchTimeoutMs, false);
        if (g_stop) return false;
        const ToolProcess::Result rev = RunGitTool({ L"-C", dir, L"rev-parse", f.ok() ? L"FETCH_HEAD" : L"HEAD" }, kListTimeoutMs, true);
        if (g_stop) return false;
        if (rev.ok()) commit = ToolProcess::FirstLine(ToolProcess::DecodeOutput(rev.out));
        if (commit.empty()) RemoveTree(dir);                          // broken: clone it again below
    }
    if (commit.empty()) {
        std::vector<std::wstring> args = { L"clone", L"--quiet", L"--no-checkout", L"--depth", L"1", L"--filter=blob:none", L"--single-branch" };
        if (!parts.ref.empty()) { args.push_back(L"--branch"); args.push_back(parts.ref); }
        args.push_back(parts.repoUrl);
        args.push_back(dir);
        const ToolProcess::Result c = RunGitTool(args, kCloneTimeoutMs, false);
        if (g_stop) { RemoveTree(dir); return false; }
        if (!c.ok()) {
            message = Err(c, L"git");
            RemoveTree(dir);
            return false;
        }
        const ToolProcess::Result rev = RunGitTool({ L"-C", dir, L"rev-parse", L"HEAD" }, kListTimeoutMs, true);
        if (rev.ok()) commit = ToolProcess::FirstLine(ToolProcess::DecodeOutput(rev.out));
        if (commit.empty()) { message = L"The repository has no commits."; return false; }
    }
    GitRepo repo;
    repo.dir = dir;
    repo.commit = commit;
    repo.ready = true;
    std::lock_guard<std::mutex> lk(g_mutex);
    g_git[key] = repo;
    out = repo;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Reading a listing (worker thread)
// ---------------------------------------------------------------------------------------------------------------
bool ParseSvnListRaw(std::string_view xml, std::vector<RawItem>& out) {
    out.clear();
    bool sawList = false;
    X::ForEachElement(xml, "list", [&](const X::Element& list) {
        sawList = true;
        X::ForEachElement(list.body, "entry", [&](const X::Element& e) {
            RawItem it;
            it.isDir = (X::AttrText(e.attrs, "kind") == "dir");
            it.name = PathUtils::Utf8ToWide(X::ChildText(e.body, "name"));
            if (it.name.empty()) return true;
            const std::string size = X::ChildText(e.body, "size");
            if (!it.isDir && !size.empty()) {
                it.sizeKnown = true;
                it.size = std::strtoull(size.c_str(), nullptr, 10);
            }
            X::Element commit;
            if (X::FindChild(e.body, "commit", commit)) it.unixTime = Svn::ParseIsoTimeUtc(X::ChildText(commit.body, "date"));
            out.push_back(std::move(it));
            return true;
        });
        return false;                                   // one folder was asked for
    });
    return sawList;
}

// Revisions of the files of a listing (they name the temp copies, so a changed file is fetched again)
void RememberSvnRevisions(std::string_view xml, const std::wstring& dirUrl) {
    std::lock_guard<std::mutex> lk(g_mutex);
    X::ForEachElement(xml, "entry", [&](const X::Element& e) {
        if (X::AttrText(e.attrs, "kind") != "file") return true;
        X::Element commit;
        if (!X::FindChild(e.body, "commit", commit)) return true;
        const int64_t rev = std::strtoll(X::AttrText(commit.attrs, "revision").c_str(), nullptr, 10);
        g_svnFileRevs[Key(JoinUrl(dirUrl, PathUtils::Utf8ToWide(X::ChildText(e.body, "name"))))] = rev;
        return true;
    });
}

bool ParseGitLsTreeRaw(std::string_view text, std::vector<RawItem>& out) {
    out.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\0', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view rec = text.substr(pos, end - pos);
        pos = end + 1;
        // "<mode> SP <type> SP <object> TAB <name>"
        const size_t tab = rec.find('\t');
        if (tab == std::string_view::npos) continue;
        const std::string_view meta = rec.substr(0, tab);
        const size_t sp1 = meta.find(' ');
        const size_t sp2 = (sp1 == std::string_view::npos) ? sp1 : meta.find(' ', sp1 + 1);
        if (sp2 == std::string_view::npos) continue;
        const std::string_view type = meta.substr(sp1 + 1, sp2 - sp1 - 1);
        RawItem it;
        it.name = PathUtils::Utf8ToWide(rec.substr(tab + 1));
        if (it.name.empty()) continue;
        if (type == "tree") it.isDir = true;
        else if (type == "commit") { it.isDir = true; it.submodule = true; }
        else if (type != "blob") continue;
        out.push_back(std::move(it));
    }
    return true;
}

Listing ReadListing(const std::wstring& url) {
    Listing l;
    l.state = ListState::Failed;
    l.kind = ResolveKind(url, l.message);
    if (l.kind == Kind::Unknown || g_stop) return l;

    if (l.kind == Kind::Svn) {
        const ToolProcess::Result r = RunSvnTool({ L"list", L"--xml", Peg(url) }, kListTimeoutMs);
        if (g_stop) return l;
        if (!r.ok() || !ParseSvnListRaw(r.out, l.items)) {
            l.message = r.ok() ? std::wstring(L"svn list printed nothing that could be read.") : Err(r, L"svn");
            return l;
        }
        RememberSvnRevisions(r.out, url);
        l.state = ListState::Ready;
        return l;
    }

    GitUrlParts parts;
    if (!SplitGitUrl(url, parts)) { l.message = L"Not a git repository address."; return l; }
    GitRepo repo;
    if (!EnsureGitRepo(parts, repo, l.message)) {
        // no such branch, or no connection: the address is looked at afresh next time
        if (!g_stop) { std::lock_guard<std::mutex> lk(g_mutex); ForgetRootLocked(parts.base); }
        return l;
    }
    const std::wstring tree = parts.innerPath.empty() ? repo.commit : (repo.commit + L":" + parts.innerPath);
    const ToolProcess::Result r = RunGitTool({ L"-C", repo.dir, L"ls-tree", L"-z", tree }, kListTimeoutMs, true);
    if (g_stop) return l;
    if (!r.ok()) {
        const std::wstring e = Err(r, L"git");
        l.message = (e.find(L"not a tree") != std::wstring::npos || e.find(L"Not a valid object") != std::wstring::npos)
            ? std::wstring(L"This is not a folder of the repository: a file, or a git submodule (another repository, with an address of its own).")
            : e;
        return l;
    }
    ParseGitLsTreeRaw(r.out, l.items);
    l.state = ListState::Ready;
    return l;
}

void WorkerMain() {
    std::unique_lock<std::mutex> lk(g_mutex);
    for (;;) {
        g_cv.wait(lk, [] { return g_stop.load() || !g_queue.empty(); });
        if (g_stop) return;
        const std::wstring url = g_queue.front();
        g_queue.pop_front();
        lk.unlock();
        Listing result = ReadListing(url);
        lk.lock();
        if (g_stop) return;
        auto it = g_listings.find(Key(url));
        if (it != g_listings.end() && it->second.state == ListState::Loading) it->second = std::move(result);
        if (HWND h = g_notify) PostMessageW(h, WM_APP_DIR_CHANGED, 0, 0);
    }
}

void StartWorkerLocked() {
    if (g_started || g_closed) return;
    g_stop = false;
    g_started = true;
    g_worker = std::thread(WorkerMain);
}

} // namespace

// =================================================================================================================
// URLs
// =================================================================================================================
bool IsRepoUrl(const std::wstring& path) {
    const std::wstring scheme = SchemeOf(path);
    if (scheme.empty() || path.size() <= scheme.size() + 3) return false;
    static const wchar_t* const kSchemes[] = { L"svn", L"svn+ssh", L"git", L"git+ssh", L"ssh", L"http", L"https", L"file" };
    for (const wchar_t* s : kSchemes) {
        if (scheme == s) return true;
    }
    return false;
}

namespace {

// What the address itself says (no roots met before); takes no lock. The branch name of a ".../tree/<ref>/..." link
// takes `refParts` parts of the path.
bool SplitGitUrlFromText(const std::wstring& url, GitUrlParts& out, size_t refParts = 1) {
    out = GitUrlParts();
    const std::wstring scheme = SchemeOf(url);
    const std::wstring low = Lower(url);
    // 2. "....git" ends the repository's address
    for (size_t p = low.find(L".git"); p != std::wstring::npos; p = low.find(L".git", p + 1)) {
        const size_t after = p + 4;
        if (after == low.size() || low[after] == L'/') {
            out.repoUrl = url.substr(0, after);
            out.base = out.repoUrl;
            if (after < url.size()) out.innerPath = DecodePct(url.substr(after + 1));
            return true;
        }
    }
    // 3. Hosts where the first two parts of the path name the repository (and .../tree/<ref>/... names a folder)
    const std::wstring host = HostOf(url);
    const bool github = (host == L"github.com" || host == L"www.github.com");
    const bool gitlab = (host == L"gitlab.com" || host == L"www.gitlab.com");
    const bool known = github || gitlab || host == L"bitbucket.org" || host == L"codeberg.org";
    if (known && (scheme == L"https" || scheme == L"http")) {
        const size_t ps = PathStart(url);
        if (ps == std::wstring::npos) return false;
        std::vector<std::wstring> seg;
        size_t pos = ps + 1;
        while (pos <= url.size()) {
            size_t slash = url.find(L'/', pos);
            if (slash == std::wstring::npos) slash = url.size();
            if (slash > pos) seg.push_back(url.substr(pos, slash - pos));
            pos = slash + 1;
        }
        if (seg.size() < 2) return false;
        out.repoUrl = url.substr(0, ps) + L"/" + seg[0] + L"/" + seg[1];
        out.base = out.repoUrl;
        size_t next = 2;
        if (gitlab && seg.size() > next && seg[next] == L"-") ++next;      // gitlab.com/o/r/-/tree/<ref>/...
        if (seg.size() > next + 1 && (seg[next] == L"tree" || seg[next] == L"blob")) {
            const size_t want = next + 1 + (refParts < 1 ? 1 : refParts);
            const size_t refEnd = (want < seg.size()) ? want : seg.size();   // past the ref's last part
            for (size_t i = next + 1; i < refEnd; ++i) out.ref += (i > next + 1 ? L"/" : L"") + DecodePct(seg[i]);
            for (size_t i = 2; i < refEnd; ++i) out.base += L"/" + seg[i];
            next = refEnd;
        }
        for (size_t i = next; i < seg.size(); ++i) out.innerPath += (out.innerPath.empty() ? L"" : L"/") + DecodePct(seg[i]);
        return true;
    }
    // 4. git:// and ssh:// are git's own: the whole address is the repository
    if (scheme == L"git" || scheme == L"git+ssh" || scheme == L"ssh") {
        out.repoUrl = url;
        out.base = url;
        return true;
    }
    return false;
}

Kind GuessKindFromText(const std::wstring& url) {
    const std::wstring scheme = SchemeOf(url);
    if (scheme == L"svn" || scheme == L"svn+ssh") return Kind::Svn;
    if (scheme == L"git" || scheme == L"git+ssh" || scheme == L"ssh") return Kind::Git;
    GitUrlParts parts;
    if (SplitGitUrlFromText(url, parts)) return Kind::Git;
    return Kind::Unknown;
}

// The kind of a root met before that holds the URL, else what the address says (Unknown: has to be asked). A link
// that names a branch (".../tree/<ref>/...") is not taken for a folder of a root without that branch, such as the
// repository's top visited before.
Kind KindOf(const std::wstring& url, std::wstring* rootOut, size_t* refPartsOut) {
    GitUrlParts fromText;
    const bool namesRef = SplitGitUrlFromText(url, fromText) && !fromText.ref.empty();
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        const Root* r = KnownRootLocked(url);
        if (r && !(namesRef && r->url.size() < Key(fromText.base).size())) {
            if (rootOut) *rootOut = r->url;
            if (refPartsOut) *refPartsOut = r->refParts;
            return r->kind;
        }
    }
    return GuessKindFromText(url);
}

} // namespace

bool SplitGitUrl(const std::wstring& rawUrl, GitUrlParts& out) {
    out = GitUrlParts();
    const std::wstring url = Key(rawUrl);
    std::wstring knownRoot;
    size_t refParts = 1;
    if (KindOf(url, &knownRoot, &refParts) != Kind::Git) return false;
    if (knownRoot.empty()) return SplitGitUrlFromText(url, out);
    // A root met before: its own address may name a ref (.../tree/<ref>), which the folders below it keep
    GitUrlParts fromRoot;
    if (SplitGitUrlFromText(knownRoot, fromRoot, refParts) && _wcsicmp(Key(fromRoot.base).c_str(), knownRoot.c_str()) == 0) {
        out = fromRoot;
    } else {
        out.repoUrl = knownRoot;
        out.base = knownRoot;
    }
    out.innerPath = (url.size() > knownRoot.size()) ? DecodePct(url.substr(knownRoot.size() + 1)) : std::wstring();
    return true;
}

bool SplitGitUrlText(const std::wstring& url, size_t refParts, GitUrlParts& out) {
    return SplitGitUrlFromText(Key(url), out, refParts);
}

size_t RefParts(const std::wstring& refAndPath, std::string_view lsRemoteOutput) {
    size_t bestLen = 0, parts = 1;
    size_t pos = 0;
    while (pos < lsRemoteOutput.size()) {
        size_t end = lsRemoteOutput.find('\n', pos);
        if (end == std::string_view::npos) end = lsRemoteOutput.size();
        std::string_view line = lsRemoteOutput.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const size_t tab = line.find('\t');                             // "<object> TAB refs/heads/<name>"
        if (tab == std::string_view::npos) continue;
        std::string_view ref = line.substr(tab + 1);
        if (ref.ends_with("^{}")) ref.remove_suffix(3);                 // the commit an annotated tag points at
        if (ref.starts_with("refs/heads/")) ref.remove_prefix(11);
        else if (ref.starts_with("refs/tags/")) ref.remove_prefix(10);
        else continue;
        const std::wstring name = PathUtils::Utf8ToWide(ref);
        if (name.size() <= bestLen || refAndPath.compare(0, name.size(), name) != 0) continue;
        if (refAndPath.size() > name.size() && refAndPath[name.size()] != L'/') continue;
        bestLen = name.size();
        parts = 1 + static_cast<size_t>(std::count(name.begin(), name.end(), L'/'));
    }
    return parts;
}

Kind GuessKind(const std::wstring& url) { return KindOf(Key(url)); }

std::wstring JoinUrl(const std::wstring& dirUrl, const std::wstring& name) {
    return Key(dirUrl) + L"/" + EncodeName(name);
}

std::wstring LeafName(const std::wstring& url) {
    const std::wstring u = Key(url);
    const size_t ps = PathStart(u);
    const size_t slash = u.rfind(L'/');
    if (ps == std::wstring::npos || slash == std::wstring::npos || slash < ps) return HostOf(u);
    return DecodePct(u.substr(slash + 1));
}

std::wstring RootUrl(const std::wstring& url) {
    const std::wstring u = Key(url);
    std::wstring knownRoot;
    const Kind kind = KindOf(u, &knownRoot);
    if (!knownRoot.empty()) return knownRoot;
    GitUrlParts parts;
    if (kind == Kind::Git && SplitGitUrl(u, parts)) return Key(parts.base);
    return u;
}

std::wstring ParentUrl(const std::wstring& url) {
    const std::wstring u = Key(url);
    std::wstring knownRoot;
    const Kind kind = KindOf(u, &knownRoot);
    // the top of a repository has no parent here; an address not known yet goes up its path
    if (!knownRoot.empty() && _wcsicmp(knownRoot.c_str(), u.c_str()) == 0) return {};
    if (kind == Kind::Git) {
        GitUrlParts parts;
        if (SplitGitUrl(u, parts) && parts.innerPath.empty()) return {};
    }
    const size_t ps = PathStart(u);
    const size_t slash = u.rfind(L'/');
    if (ps == std::wstring::npos || slash == std::wstring::npos || slash <= ps) return {};
    return u.substr(0, slash);
}

std::wstring RootLabel(const std::wstring& url) {
    const std::wstring root = RootUrl(url);
    const Kind kind = KindOf(Key(url));
    const size_t schemeEnd = root.find(L"://");
    std::wstring shown = (schemeEnd == std::wstring::npos) ? root : root.substr(schemeEnd + 3);
    if (kind == Kind::Git) {
        GitUrlParts parts;
        if (SplitGitUrl(root, parts)) {
            const size_t se = parts.repoUrl.find(L"://");
            shown = (se == std::wstring::npos) ? parts.repoUrl : parts.repoUrl.substr(se + 3);
            if (!parts.ref.empty()) shown += L" @ " + parts.ref;
        }
        return L"git: " + DecodePct(shown);
    }
    return (kind == Kind::Svn ? L"SVN: " : L"") + DecodePct(shown);
}

// =================================================================================================================
// Listings
// =================================================================================================================
void SetNotifyWindow(HWND hwnd) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_notify = hwnd;
}

void SetCacheRoot(const std::wstring& folder) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_cacheRoot = folder;
    if (!g_cacheRoot.empty() && g_cacheRoot.back() != L'\\') g_cacheRoot += L'\\';
    g_git.clear();                                   // clones made under another root are not looked for there
}

void Shutdown(bool allowRestart) {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_stop = true;
        g_closed = !allowRestart;   // at exit: a folder preview still running must not start the worker again
        g_notify = nullptr;
    }
    g_cv.notify_all();
    if (g_worker.joinable()) g_worker.join();
    std::lock_guard<std::mutex> lk(g_mutex);
    g_started = false;
    g_queue.clear();
    for (auto it = g_listings.begin(); it != g_listings.end();) {      // whatever was being read is read again later
        if (it->second.state == ListState::Loading) it = g_listings.erase(it);
        else ++it;
    }
}

ListState GetListing(const std::wstring& url, bool showExtensions, Lang lang, std::vector<FileEntry>& out, std::wstring* outMessage) {
    out.clear();
    const std::wstring key = Key(url);
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_listings.find(key);
    if (it == g_listings.end()) {
        g_listings[key] = Listing();
        g_queue.push_back(key);
        StartWorkerLocked();
        g_cv.notify_one();
        return ListState::Loading;
    }
    const Listing& l = it->second;
    if (outMessage) *outMessage = l.message;
    if (l.state == ListState::Ready) {
        out.reserve(l.items.size());
        for (const auto& raw : l.items) out.push_back(MakeEntry(key, raw, l.kind, showExtensions, lang));
    }
    return l.state;
}

ListState PeekState(const std::wstring& url, std::wstring* outMessage) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_listings.find(Key(url));
    if (it == g_listings.end()) return ListState::Loading;
    if (outMessage) *outMessage = it->second.message;
    return it->second.state;
}

void Invalidate(const std::wstring& url) {
    const std::wstring key = Key(url);
    GitUrlParts parts;
    const bool git = SplitGitUrl(key, parts);          // only for an address that is git's
    std::lock_guard<std::mutex> lk(g_mutex);
    if (git) {
        // the repository is fetched again, and all its folders are listed from what that brings
        auto g = g_git.find(GitKey(parts));
        if (g != g_git.end()) g->second.ready = false;
        const std::wstring base = Key(parts.base);
        for (auto it = g_listings.begin(); it != g_listings.end();) {
            if (StartsWithFolder(it->first, base) && it->second.state != ListState::Loading) it = g_listings.erase(it);
            else ++it;
        }
    }
    auto it = g_listings.find(key);
    if (it != g_listings.end() && it->second.state != ListState::Loading) g_listings.erase(it);
}

// =================================================================================================================
// Files
// =================================================================================================================
bool FetchFileToTemp(const std::wstring& url, std::wstring& outLocalPath, std::wstring* outError) {
    outLocalPath.clear();
    const std::wstring key = Key(url);
    auto fail = [&](const std::wstring& m) { if (outError) *outError = m; return false; };

    const Kind kind = KindOf(key);
    int64_t svnRev = 0;
    GitRepo repo;
    GitUrlParts parts;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto rv = g_svnFileRevs.find(key);
        if (rv != g_svnFileRevs.end()) svnRev = rv->second;
    }
    if (kind == Kind::Git) {
        if (!SplitGitUrl(key, parts) || parts.innerPath.empty()) return fail(L"Not a file of a git repository.");
        std::lock_guard<std::mutex> lk(g_mutex);
        auto g = g_git.find(GitKey(parts));
        if (g == g_git.end() || !g->second.ready) return fail(L"The repository is still being read.");
        repo = g->second;
    } else if (kind != Kind::Svn) {
        return fail(L"Not a file of a Subversion or git repository.");
    }

    const std::wstring version = (kind == Kind::Git) ? (repo.commit + L":" + parts.innerPath) : (key + L"@" + std::to_wstring(svnRev));
    const std::wstring dir = TempRoot() + L"Explorer2_RepoFiles\\" + Hash16(version);
    const std::wstring dest = dir + L"\\" + SafeFileName(LeafName(key));
    const DWORD attr = GetFileAttributesW(dest.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) { outLocalPath = dest; return true; }
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);              // with the folders above it

    // Written under another name and renamed when complete: a copy that broke off is never taken for the file
    const std::wstring part = dest + L"." + std::to_wstring(GetCurrentThreadId()) + L".part";
    ToolProcess::Result r;
    if (kind == Kind::Git) {
        std::lock_guard<std::mutex> fetchLock(g_gitFetchMutex);
        const DWORD again = GetFileAttributesW(dest.c_str());           // another thread may have just made it
        if (again != INVALID_FILE_ATTRIBUTES && !(again & FILE_ATTRIBUTE_DIRECTORY)) { outLocalPath = dest; return true; }
        r = RunGitTool({ L"-C", repo.dir, L"cat-file", L"blob", repo.commit + L":" + parts.innerPath }, kFetchTimeoutMs, true, part);
    } else {
        r = RunSvnTool({ L"cat", svnRev > 0 ? (key + L"@" + std::to_wstring(svnRev)) : Peg(key) }, kFetchTimeoutMs, part);
    }
    if (!r.ok()) {
        DeleteFileW(part.c_str());
        return fail(Err(r, kind == Kind::Git ? L"git" : L"svn"));
    }
    if (!MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(part.c_str());
        if (GetFileAttributesW(dest.c_str()) == INVALID_FILE_ATTRIBUTES) return fail(L"The temporary copy could not be written.");
    }
    outLocalPath = dest;
    return true;
}

// =================================================================================================================
// Readers (also for the self-tests)
// =================================================================================================================
bool ParseSvnListXml(std::string_view xml, const std::wstring& dirUrl, bool showExtensions, Lang lang, std::vector<FileEntry>& out) {
    out.clear();
    std::vector<RawItem> raw;
    if (!ParseSvnListRaw(xml, raw)) return false;
    for (const auto& r : raw) out.push_back(MakeEntry(dirUrl, r, Kind::Svn, showExtensions, lang));
    return true;
}

bool ParseGitLsTree(std::string_view text, const std::wstring& dirUrl, bool showExtensions, Lang lang, std::vector<FileEntry>& out) {
    out.clear();
    std::vector<RawItem> raw;
    ParseGitLsTreeRaw(text, raw);
    for (const auto& r : raw) out.push_back(MakeEntry(dirUrl, r, Kind::Git, showExtensions, lang));
    return true;
}

} // namespace Explorer2::RepoBrowser
