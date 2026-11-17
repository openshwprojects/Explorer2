#include "SvnWorkingCopy.h"

#include "PathUtils.h"
#include "XmlTagScan.h"

#include <algorithm>
#include <climits>
#include <cwchar>
#include <mutex>

namespace Explorer2::Svn {

namespace {

namespace X = XmlTagScan;

constexpr size_t kMaxListed = 500;          // changed paths / incoming items written into an output text

std::wstring W(std::string_view utf8) { return PathUtils::Utf8ToWide(utf8); }

int64_t ToInt64(std::string_view s) {
    int64_t v = 0;
    bool any = false;
    for (char c : s) {
        if (c < '0' || c > '9') break;
        if (v > (INT64_MAX - 9) / 10) return 0;
        v = v * 10 + (c - '0');
        any = true;
    }
    return any ? v : 0;
}

// The letter `svn status` prints for an item status; ' ' for unchanged items, externals and ignored ones
wchar_t ItemCode(std::string_view item) {
    static const struct { const char* name; wchar_t code; } kItems[] = {
        { "added", L'A' }, { "conflicted", L'C' }, { "deleted", L'D' }, { "incomplete", L'!' }, { "missing", L'!' },
        { "modified", L'M' }, { "obstructed", L'~' }, { "replaced", L'R' }, { "unversioned", L'?' }, { "merged", L'G' } };
    for (const auto& k : kItems) {
        if (item == k.name) return k.code;
    }
    return L' ';
}

wchar_t PropsCode(std::string_view props) {
    if (props == "modified") return L'M';
    if (props == "conflicted") return L'C';
    return L' ';
}

std::wstring FirstNonEmptyLine(const std::wstring& text) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = text.substr(pos, eol - pos);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) line.pop_back();
        size_t b = 0;
        while (b < line.size() && (line[b] == L' ' || line[b] == L'\t')) ++b;
        if (b < line.size()) return line.substr(b);
        pos = eol + 1;
    }
    return {};
}

// Local time of a Unix time, "2026-11-10 10:12:44"
std::wstring LocalTimeText(int64_t unixTime) {
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<ULONGLONG>(unixTime) * 10000000ULL + 116444736000000000ULL;
    FILETIME ft{ u.LowPart, u.HighPart };
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&ft, &utc)) return {};
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
    wchar_t b[40] = {};
    swprintf_s(b, L"%04u-%02u-%02u %02u:%02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute,
               local.wSecond);
    return b;
}

// "%20" -> " " in a URL path shown to the user (the bytes are UTF-8)
std::wstring UriDecode(const std::wstring& s) {
    std::string bytes;
    bytes.reserve(s.size());
    const std::string utf8 = PathUtils::WideToUtf8(s);
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '%' && i + 2 < utf8.size() && hex(utf8[i + 1]) >= 0 && hex(utf8[i + 2]) >= 0) {
            bytes.push_back(static_cast<char>(hex(utf8[i + 1]) * 16 + hex(utf8[i + 2])));
            i += 2;
        } else {
            bytes.push_back(utf8[i]);
        }
    }
    return W(bytes);
}

std::mutex g_svnMutex;
std::wstring g_svnPath;                 // empty = not found
ULONGLONG g_svnProbeTick = 0;
bool g_svnProbed = false;

} // namespace

// =================================================================================================================
// svn.exe
// =================================================================================================================
std::wstring FindSvnExe() {
    std::lock_guard<std::mutex> lk(g_svnMutex);
    const ULONGLONG now = GetTickCount64();
    if (!g_svnProbed || (g_svnPath.empty() && now - g_svnProbeTick > 5000)) {
        std::vector<std::wstring> candidates;
        for (const std::wstring& base : { ToolProcess::GetEnv(L"ProgramFiles"), ToolProcess::GetEnv(L"ProgramW6432"),
                                          ToolProcess::GetEnv(L"ProgramFiles(x86)") }) {
            if (base.empty()) continue;
            for (const wchar_t* rel : { L"\\TortoiseSVN\\bin\\svn.exe", L"\\SlikSvn\\bin\\svn.exe",
                                        L"\\CollabNet\\Subversion Client\\svn.exe", L"\\VisualSVN Server\\bin\\svn.exe",
                                        L"\\Subversion\\bin\\svn.exe", L"\\Apache Subversion\\bin\\svn.exe" }) {
                candidates.push_back(base + rel);
            }
        }
        g_svnPath = ToolProcess::FindExecutable(L"svn.exe", candidates);
        g_svnProbed = true;
        g_svnProbeTick = now;
    }
    return g_svnPath;
}

ToolProcess::Result Run(const std::wstring& wcRoot, const std::vector<std::wstring>& args, DWORD timeoutMs,
                        const std::atomic<bool>& stop, size_t maxBytes) {
    const std::wstring exe = FindSvnExe();
    if (exe.empty()) {
        ToolProcess::Result r;
        r.notFound = true;
        return r;
    }
    std::vector<std::wstring> all = args;
    all.push_back(L"--non-interactive");
    ToolProcess::Options o;
    o.workDir = wcRoot;
    o.timeoutMs = timeoutMs;
    o.maxBytes = maxBytes;
    return ToolProcess::Run(exe, all, o, stop);
}

// =================================================================================================================
// Times
// =================================================================================================================
int64_t ParseIsoTimeUtc(std::string_view s) {
    // YYYY-MM-DDTHH:MM:SS[.fraction]Z
    if (s.size() < 19) return 0;
    auto num = [&](size_t pos, size_t len, int& out) {
        int v = 0;
        for (size_t i = pos; i < pos + len; ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            v = v * 10 + (s[i] - '0');
        }
        out = v;
        return true;
    };
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (!num(0, 4, y) || s[4] != '-' || !num(5, 2, mo) || s[7] != '-' || !num(8, 2, d) || (s[10] != 'T' && s[10] != ' ') ||
        !num(11, 2, h) || s[13] != ':' || !num(14, 2, mi) || s[16] != ':' || !num(17, 2, se)) {
        return 0;
    }
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return 0;
    // Days since 1970-01-01 of a civil date (H. Hinnant's days_from_civil)
    const int yy = y - (mo <= 2 ? 1 : 0);
    const int era = yy / 400;
    const int yoe = yy - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + se;
}

std::wstring RelativeTime(int64_t t, int64_t now) {
    if (t <= 0) return {};
    if (now < t) return L"in the future";
    auto ago = [](int64_t n, const wchar_t* one, const wchar_t* many) {
        return std::to_wstring(n) + L" " + (n == 1 ? one : many) + L" ago";
    };
    // The steps and roundings of git's show_date_relative(), so both panels word dates alike
    int64_t diff = now - t;
    if (diff < 90) return ago(diff, L"second", L"seconds");
    diff = (diff + 30) / 60;
    if (diff < 90) return ago(diff, L"minute", L"minutes");
    diff = (diff + 30) / 60;
    if (diff < 36) return ago(diff, L"hour", L"hours");
    diff = (diff + 12) / 24;
    if (diff < 14) return ago(diff, L"day", L"days");
    if (diff < 70) return ago((diff + 3) / 7, L"week", L"weeks");
    if (diff < 365) return ago((diff + 15) / 30, L"month", L"months");
    if (diff < 1825) {
        const int64_t totalMonths = (diff * 12 * 2 + 365) / (365 * 2);
        const int64_t years = totalMonths / 12, months = totalMonths % 12;
        if (months == 0) return ago(years, L"year", L"years");
        return std::to_wstring(years) + (years == 1 ? L" year, " : L" years, ") + std::to_wstring(months) +
               (months == 1 ? L" month ago" : L" months ago");
    }
    return ago((diff + 183) / 365, L"year", L"years");
}

int64_t NowUnix() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

// =================================================================================================================
// XML readers
// =================================================================================================================
bool ParseInfoXml(std::string_view xml, SvnInfo& out) {
    out = SvnInfo();
    X::Element entry;
    if (!X::FindChild(xml, "entry", entry, false)) return false;
    out.revision = ToInt64(X::AttrText(entry.attrs, "revision"));
    out.url = W(X::ChildText(entry.body, "url"));
    out.relativeUrl = W(X::ChildText(entry.body, "relative-url"));
    X::Element part;
    if (X::FindChild(entry.body, "repository", part)) {
        out.repositoryRoot = W(X::ChildText(part.body, "root"));
        out.uuid = W(X::ChildText(part.body, "uuid"));
    }
    if (X::FindChild(entry.body, "wc-info", part)) {
        out.wcRoot = W(X::ChildText(part.body, "wcroot-abspath"));
        std::replace(out.wcRoot.begin(), out.wcRoot.end(), L'/', L'\\');
    }
    if (X::FindChild(entry.body, "commit", part)) {
        out.lastChangedRevision = ToInt64(X::AttrText(part.attrs, "revision"));
        out.lastChangedAuthor = W(X::ChildText(part.body, "author"));
        out.lastChangedTime = ParseIsoTimeUtc(X::ChildText(part.body, "date"));
    }
    return !out.url.empty();
}

std::wstring PathInRepository(const SvnInfo& info) {
    std::wstring rel = info.relativeUrl;
    if (rel.rfind(L"^/", 0) == 0) {
        rel.erase(0, 2);
    } else if (rel.empty() && !info.repositoryRoot.empty() && info.url.rfind(info.repositoryRoot, 0) == 0) {
        rel = info.url.substr(info.repositoryRoot.size());          // svn before 1.8 prints no relative-url
        while (!rel.empty() && rel.front() == L'/') rel.erase(0, 1);
    } else if (rel.empty()) {
        return info.url;
    }
    rel = UriDecode(rel);
    return rel.empty() ? L"/" : rel;
}

void ParseStatusXml(std::string_view xml, GitRepoState& s, size_t maxChanges) {
    s.changes.clear();
    s.stagedCount = s.modifiedCount = s.untrackedCount = s.conflictCount = 0;
    X::ForEachElement(xml, "entry", [&](const X::Element& e) {
        X::Element ws;
        if (!X::FindChild(e.body, "wc-status", ws)) return true;
        const std::string item = X::AttrText(ws.attrs, "item");
        if (item == "external" || item == "ignored") return true;          // not changes of this working copy
        GitFileChange c;
        c.indexStatus = ItemCode(item);
        c.workStatus = PropsCode(X::AttrText(ws.attrs, "props"));
        c.treeConflict = (X::AttrText(ws.attrs, "tree-conflicted") == "true");
        if (c.indexStatus == L' ' && c.workStatus == L' ' && !c.treeConflict) return true;   // unchanged (svn status -v)
        c.path = W(X::AttrText(e.attrs, "path"));
        if (c.indexStatus == L'C' || c.workStatus == L'C' || c.treeConflict) ++s.conflictCount;
        else if (c.indexStatus == L'?') ++s.untrackedCount;
        else ++s.modifiedCount;
        if (s.changes.size() < maxChanges) s.changes.push_back(std::move(c));
        return true;
    });
}

void ParseLogXml(std::string_view xml, std::vector<GitCommit>& out, int64_t nowUnix) {
    out.clear();
    X::ForEachElement(xml, "logentry", [&](const X::Element& e) {
        const std::string rev = X::AttrText(e.attrs, "revision");
        if (ToInt64(rev) <= 0) return true;
        GitCommit c;
        c.hash = W(rev);
        c.shortHash = L"r" + c.hash;
        c.author = W(X::ChildText(e.body, "author"));            // empty for an anonymous commit
        c.unixTime = ParseIsoTimeUtc(X::ChildText(e.body, "date"));
        c.relDate = RelativeTime(c.unixTime, nowUnix);
        c.subject = FirstNonEmptyLine(W(X::ChildText(e.body, "msg")));
        out.push_back(std::move(c));
        return true;
    });
}

// =================================================================================================================
// Output panel texts
// =================================================================================================================
std::wstring FormatRevisionText(std::string_view xml) {
    std::wstring out;
    X::ForEachElement(xml, "logentry", [&](const X::Element& e) {
        const std::wstring author = W(X::ChildText(e.body, "author"));
        const int64_t t = ParseIsoTimeUtc(X::ChildText(e.body, "date"));
        out += L"r" + W(X::AttrText(e.attrs, "revision")) + L" | " + (author.empty() ? std::wstring(L"(no author)") : author);
        if (t != 0) out += L" | " + LocalTimeText(t) + L" (" + RelativeTime(t, NowUnix()) + L")";
        out += L"\n";
        X::Element paths;
        if (X::FindChild(e.body, "paths", paths)) {
            out += L"\nChanged paths:\n";
            size_t count = 0;
            X::ForEachChild(paths.body, [&](std::string_view name, const X::Element& p) {
                if (name != "path") return true;
                if (++count > kMaxListed) return false;
                std::wstring line = L"   " + W(X::AttrText(p.attrs, "action")) + L" " + W(X::TrimView(X::DecodeText(p.body)));
                const std::string from = X::AttrText(p.attrs, "copyfrom-path");
                if (!from.empty()) line += L" (from " + W(from) + L":" + W(X::AttrText(p.attrs, "copyfrom-rev")) + L")";
                out += line + L"\n";
                return true;
            });
            if (count > kMaxListed) out += L"   ...\n";
        }
        const std::wstring msg = W(X::ChildText(e.body, "msg"));
        out += L"\n" + (msg.empty() ? std::wstring(L"(no log message)") : msg) + L"\n";
        return false;                                               // one revision was asked for
    });
    return out.empty() ? std::wstring(L"The repository has no such revision.") : out;
}

std::wstring FormatUpdatesText(std::string_view xml) {
    std::wstring lines;
    size_t count = 0;
    X::ForEachElement(xml, "entry", [&](const X::Element& e) {
        X::Element rs;
        if (!X::FindChild(e.body, "repos-status", rs)) return true;
        const wchar_t code = ItemCode(X::AttrText(rs.attrs, "item"));
        const wchar_t props = PropsCode(X::AttrText(rs.attrs, "props"));
        if (code == L' ' && props == L' ') return true;
        if (++count <= kMaxListed) {
            lines += L"   ";
            lines += code;
            lines += props;
            lines += L"  " + W(X::AttrText(e.attrs, "path")) + L"\n";
        }
        return true;
    });
    std::wstring against;
    X::Element ag;
    if (X::FindChild(xml, "against", ag, false)) against = W(X::AttrText(ag.attrs, "revision"));
    // Short lines: the dock's output panel does not wrap
    const std::wstring head = against.empty() ? std::wstring() : (L" (it is at r" + against + L")");
    if (count == 0) return L"Up to date with the repository" + head + L".";
    std::wstring out = std::to_wstring(count) + (count == 1 ? L" item" : L" items") + L" changed in the repository" + head +
                       L":\n\n" + lines;
    if (count > kMaxListed) out += L"   ...\n";
    out += L"\nAn update in your Subversion client brings them in.";
    return out;
}

std::wstring DiffStatText(const std::string& diff) {
    struct FileStat {
        std::wstring path;
        int added = 0, removed = 0;
        bool binary = false, props = false, inHunk = false;
    };
    std::vector<FileStat> files;
    bool inProps = false;
    size_t pos = 0;
    while (pos < diff.size()) {
        size_t eol = diff.find('\n', pos);
        if (eol == std::string::npos) eol = diff.size();
        std::string_view line(diff.data() + pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.rfind("Index: ", 0) == 0) {
            FileStat f;
            f.path = ToolProcess::DecodeOutput(std::string(line.substr(7)));
            files.push_back(std::move(f));
            inProps = false;
            continue;
        }
        if (line.rfind("Property changes on: ", 0) == 0) {
            const std::wstring p = ToolProcess::DecodeOutput(std::string(line.substr(21)));
            if (files.empty() || files.back().path != p) {
                FileStat f;
                f.path = p;
                files.push_back(std::move(f));
            }
            files.back().props = true;
            inProps = true;
            continue;
        }
        if (files.empty() || inProps) continue;
        FileStat& f = files.back();
        if (line.rfind("Cannot display: file marked as a binary type.", 0) == 0) { f.binary = true; continue; }
        if (line.rfind("@@", 0) == 0) { f.inHunk = true; continue; }
        if (!f.inHunk) continue;                                    // "===", "--- path", "+++ path" headers
        if (line.empty()) continue;
        if (line[0] == '+') ++f.added;
        else if (line[0] == '-') ++f.removed;
    }
    if (files.empty()) return {};

    size_t width = 0;
    int most = 0;
    int added = 0, removed = 0;
    for (const auto& f : files) {
        width = std::max(width, std::min<size_t>(f.path.size(), 60));
        most = std::max(most, f.added + f.removed);
        added += f.added;
        removed += f.removed;
    }
    constexpr int kBar = 40;
    std::wstring out;
    for (const auto& f : files) {
        std::wstring name = f.path;
        if (name.size() > 60) name = L"..." + name.substr(name.size() - 57);
        out += name + std::wstring(width - std::min(width, name.size()), L' ') + L" | ";
        if (f.binary) {
            out += L"Bin";
        } else if (f.added + f.removed > 0) {
            int plus = f.added, minus = f.removed;
            if (most > kBar) {
                plus = static_cast<int>((static_cast<int64_t>(f.added) * kBar + most - 1) / most);
                minus = static_cast<int>((static_cast<int64_t>(f.removed) * kBar + most - 1) / most);
            }
            out += std::to_wstring(f.added + f.removed) + L" " + std::wstring(static_cast<size_t>(plus), L'+') +
                   std::wstring(static_cast<size_t>(minus), L'-');
        } else if (!f.props) {
            out += L"0";
        }
        if (f.props) out += (f.binary || f.added + f.removed > 0) ? L" (and properties)" : L"properties";
        out += L"\n";
    }
    out += std::to_wstring(files.size()) + (files.size() == 1 ? L" file changed, " : L" files changed, ") +
           std::to_wstring(added) + (added == 1 ? L" insertion(+), " : L" insertions(+), ") + std::to_wstring(removed) +
           (removed == 1 ? L" deletion(-)" : L" deletions(-)");
    return out;
}

} // namespace Explorer2::Svn
