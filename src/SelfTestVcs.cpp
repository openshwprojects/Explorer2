// SelfTestVcs.cpp - the version-control dock's back end: Subversion working copies (svn --xml readers, the texts of
// the output panel, finding .svn / .git) and, when a Subversion client is installed, a real repository end to end.
#include "SelfTestCommon.h"
#include "GitIntegration.h"
#include "PathUtils.h"
#include "RepoBrowser.h"
#include "SvnWorkingCopy.h"
#include "ToolProcess.h"

#include <atomic>
#include <string>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

// The XML below follows what svn 1.14 prints (same elements, attributes on their own lines)
const char* const kInfoXml = R"svn(<?xml version="1.0" encoding="UTF-8"?>
<info>
<entry
   kind="dir"
   path="."
   revision="1234">
<url>https://svn.example.org/repos/project/branches/fix%20spaces</url>
<relative-url>^/branches/fix%20spaces</relative-url>
<repository>
<root>https://svn.example.org/repos/project</root>
<uuid>13f79535-47bb-0310-9956-ffa450edef68</uuid>
</repository>
<wc-info>
<wcroot-abspath>C:/work/project</wcroot-abspath>
<schedule>normal</schedule>
<depth>infinity</depth>
</wc-info>
<commit
   revision="1230">
<author>alice</author>
<date>2026-11-10T09:12:44.123456Z</date>
</commit>
</entry>
</info>
)svn";

const char* const kStatusXml = R"svn(<?xml version="1.0" encoding="UTF-8"?>
<status>
<target
   path=".">
<entry
   path="src\main.c">
<wc-status
   item="modified"
   revision="1234"
   props="none">
<commit
   revision="1230">
<author>alice</author>
<date>2026-11-10T09:12:44.123456Z</date>
</commit>
</wc-status>
</entry>
<entry
   path="docs\zażółć.txt">
<wc-status
   props="none"
   item="unversioned">
</wc-status>
</entry>
<entry
   path="added.c">
<wc-status
   item="added"
   props="none"
   revision="-1">
</wc-status>
</entry>
<entry
   path="conflict.c">
<wc-status
   item="conflicted"
   props="none"
   revision="1234">
</wc-status>
</entry>
<entry
   path="lib">
<wc-status
   item="normal"
   revision="1234"
   props="modified">
</wc-status>
</entry>
<entry
   path="moved.txt">
<wc-status
   item="missing"
   revision="1234"
   props="none"
   tree-conflicted="true">
</wc-status>
</entry>
<entry
   path="vendor">
<wc-status
   item="external"
   props="none">
</wc-status>
</entry>
<entry
   path="old &amp; new.txt">
<wc-status
   item="deleted"
   revision="1234"
   props="none">
</wc-status>
</entry>
</target>
<changelist
   name="review">
<entry
   path="src\util.c">
<wc-status
   item="modified"
   revision="1234"
   props="none">
</wc-status>
</entry>
</changelist>
</status>
)svn";

const char* const kLogXml = R"svn(<?xml version="1.0" encoding="UTF-8"?>
<log>
<logentry
   revision="1234">
<author>alice</author>
<date>2026-11-10T09:12:44.123456Z</date>
<msg>Fix the parser &lt;b&gt; &amp; tests

Second paragraph of the message.</msg>
</logentry>
<logentry
   revision="1233">
<author>bob</author>
<date>2026-11-09T09:12:44.000000Z</date>
<msg>

  Leading blank lines before the subject</msg>
</logentry>
<logentry
   revision="1232">
<date>2026-11-01T00:00:00.000000Z</date>
</logentry>
</log>
)svn";

const char* const kLogVerboseXml = R"svn(<?xml version="1.0" encoding="UTF-8"?>
<log>
<logentry
   revision="1234">
<author>alice</author>
<date>2026-11-10T09:12:44.123456Z</date>
<paths>
<path
   action="M"
   prop-mods="false"
   text-mods="true"
   kind="file">/trunk/src/main.c</path>
<path
   copyfrom-path="/trunk/src/old.c"
   copyfrom-rev="1233"
   action="A"
   prop-mods="false"
   text-mods="false"
   kind="file">/trunk/src/new.c</path>
</paths>
<msg>Fix the parser</msg>
</logentry>
</log>
)svn";

const char* const kUpdatesXml = R"svn(<?xml version="1.0" encoding="UTF-8"?>
<status>
<target
   path=".">
<entry
   path="src\main.c">
<wc-status
   item="normal"
   revision="1234"
   props="none">
</wc-status>
<repos-status
   item="modified"
   props="none">
</repos-status>
</entry>
<entry
   path="local.c">
<wc-status
   item="modified"
   revision="1234"
   props="none">
</wc-status>
</entry>
<entry
   path="docs\new.md">
<repos-status
   item="added"
   props="none">
</repos-status>
</entry>
<against
   revision="1240"/>
</target>
</status>
)svn";

// What `svn diff --internal-diff` prints: a text change (with a removed line that itself starts with "--"), a binary
// file and a property-only change
const char* const kDiffText =
    "Index: src/main.c\n"
    "===================================================================\n"
    "--- src/main.c\t(revision 1234)\n"
    "+++ src/main.c\t(working copy)\n"
    "@@ -1,4 +1,4 @@\n"
    " int main() {\n"
    "-    return 0;\n"
    "+    int x = 1;\n"
    "+    return x;\n"
    " }\n"
    "--- an old line that started with two dashes\n"
    "Index: logo.png\n"
    "===================================================================\n"
    "Cannot display: file marked as a binary type.\n"
    "svn:mime-type = application/octet-stream\n"
    "Index: lib\n"
    "===================================================================\n"
    "--- lib\t(revision 1234)\n"
    "+++ lib\t(working copy)\n"
    "\n"
    "Property changes on: lib\n"
    "___________________________________________________________________\n"
    "Added: svn:ignore\n"
    "## -0,0 +1 ##\n"
    "+*.o\n";

constexpr int64_t kNow = 1794906764;            // 2026-11-17T09:12:44Z

const GitFileChange* FindChange(const GitRepoState& s, const std::wstring& path) {
    for (const auto& c : s.changes) if (c.path == path) return &c;
    return nullptr;
}

bool Has(const std::wstring& text, const std::wstring& part) { return text.find(part) != std::wstring::npos; }

void DeleteTreeForTest(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring p = dir + L"\\" + name;
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);       // svn keeps its pristine copies read-only
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTreeForTest(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

bool MakeDirs(const std::wstring& path) {
    size_t pos = 3;
    while (pos < path.size()) {
        size_t slash = path.find(L'\\', pos);
        if (slash == std::wstring::npos) slash = path.size();
        CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
        pos = slash + 1;
    }
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Waits (bounded) until the worker has nothing left to do
bool WaitIdle(const GitIntegration& g, DWORD maxMs) {
    const ULONGLONG start = GetTickCount64();
    while (g.IsBusy()) {
        if (GetTickCount64() - start > maxMs) return false;
        Sleep(30);
    }
    return true;
}

// A repository folder's listing, waiting (bounded) while RepoBrowser reads it in the background
RepoBrowser::ListState WaitListing(const std::wstring& url, std::vector<FileEntry>& out, std::wstring* message = nullptr) {
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        const RepoBrowser::ListState s = RepoBrowser::GetListing(url, true, Lang::EN, out, message);
        if (s != RepoBrowser::ListState::Loading || GetTickCount64() - start > 120000) return s;
        Sleep(50);
    }
}

const FileEntry* FindEntry(const std::vector<FileEntry>& items, const std::wstring& name) {
    for (const auto& e : items) if (e.name == name) return &e;
    return nullptr;
}

std::string ReadAll(const std::wstring& path) {
    std::vector<uint8_t> bytes;
    if (!PathUtils::ReadFileBytes(path, 1 << 20, bytes)) return {};
    return std::string(bytes.begin(), bytes.end());
}

const char* const kSvnListXml = R"svn(<?xml version="1.0" encoding="UTF-8"?>
<lists>
<list
   path="https://svn.example.org/repos/x/trunk">
<entry
   kind="dir">
<name>src</name>
<commit
   revision="1230">
<author>alice</author>
<date>2026-11-10T09:12:44.123456Z</date>
</commit>
</entry>
<entry
   kind="file">
<name>README.md</name>
<size>1536</size>
<commit
   revision="1234">
<author>bob</author>
<date>2026-11-16T08:00:00.000000Z</date>
</commit>
</entry>
<entry
   kind="file">
<name>logo &amp; icon.png</name>
<size>0</size>
<commit
   revision="1">
<author>alice</author>
<date>2026-01-01T00:00:00.000000Z</date>
</commit>
</entry>
</list>
</lists>
)svn";

} // namespace

void RunVcsTests(TestCounter& tc) {
    // ---- svn info
    {
        SvnInfo si;
        const bool ok = Svn::ParseInfoXml(kInfoXml, si);
        const bool good = ok && si.revision == 1234 && si.url == L"https://svn.example.org/repos/project/branches/fix%20spaces" &&
                          si.repositoryRoot == L"https://svn.example.org/repos/project" &&
                          si.uuid == L"13f79535-47bb-0310-9956-ffa450edef68" && si.wcRoot == L"C:\\work\\project" &&
                          si.lastChangedRevision == 1230 && si.lastChangedAuthor == L"alice" && si.lastChangedTime == 1794301964 &&
                          Svn::PathInRepository(si) == L"branches/fix spaces";
        tc.Record(good, L"Stage12.Vcs.SvnInfoXml",
                  L"rev=" + std::to_wstring(si.revision) + L" path=" + Svn::PathInRepository(si) + L" wc=" + si.wcRoot +
                  L" last=r" + std::to_wstring(si.lastChangedRevision) + L" " + si.lastChangedAuthor);
        SvnInfo none;
        tc.Record(!Svn::ParseInfoXml("svn: E155007: 'C:\\x' is not a working copy", none) && none.url.empty(),
                  L"Stage12.Vcs.SvnInfoXmlRejectsErrorText");
        SvnInfo old;                                         // svn 1.7 prints no relative-url
        std::string xml17 = kInfoXml;
        const size_t a = xml17.find("<relative-url>"), b = xml17.find("</relative-url>");
        xml17.erase(a, b + 15 - a);
        tc.Record(Svn::ParseInfoXml(xml17, old) && Svn::PathInRepository(old) == L"branches/fix spaces",
                  L"Stage12.Vcs.SvnInfoXmlWithoutRelativeUrl", Svn::PathInRepository(old));
    }

    // ---- svn status
    {
        GitRepoState s;
        Svn::ParseStatusXml(kStatusXml, s, 500);
        const GitFileChange* mainC = FindChange(s, L"src\\main.c");
        const GitFileChange* unv = FindChange(s, L"docs\\za\x017C\x00F3\x0142\x0107.txt");
        const GitFileChange* props = FindChange(s, L"lib");
        const GitFileChange* tree = FindChange(s, L"moved.txt");
        const GitFileChange* amp = FindChange(s, L"old & new.txt");
        const GitFileChange* inList = FindChange(s, L"src\\util.c");
        const bool good = s.changes.size() == 8 && !FindChange(s, L"vendor") &&
                          mainC && mainC->indexStatus == L'M' && mainC->workStatus == L' ' &&
                          unv && unv->indexStatus == L'?' &&
                          props && props->indexStatus == L' ' && props->workStatus == L'M' &&
                          tree && tree->indexStatus == L'!' && tree->treeConflict &&
                          amp && amp->indexStatus == L'D' && inList && inList->indexStatus == L'M' &&
                          s.modifiedCount == 5 && s.untrackedCount == 1 && s.conflictCount == 2 && s.stagedCount == 0;
        tc.Record(good, L"Stage12.Vcs.SvnStatusXml",
                  L"changes=" + std::to_wstring(s.changes.size()) + L" modified=" + std::to_wstring(s.modifiedCount) +
                  L" untracked=" + std::to_wstring(s.untrackedCount) + L" conflicts=" + std::to_wstring(s.conflictCount));
        GitRepoState capped;
        Svn::ParseStatusXml(kStatusXml, capped, 3);
        tc.Record(capped.changes.size() == 3 && capped.modifiedCount + capped.untrackedCount + capped.conflictCount == 8,
                  L"Stage12.Vcs.SvnStatusXmlCapKeepsCounts");
    }

    // ---- svn log
    {
        std::vector<GitCommit> log;
        Svn::ParseLogXml(kLogXml, log, kNow);
        const bool good = log.size() == 3 &&
                          log[0].hash == L"1234" && log[0].shortHash == L"r1234" && log[0].author == L"alice" &&
                          log[0].subject == L"Fix the parser <b> & tests" && log[0].unixTime == 1794301964 &&
                          log[0].relDate == L"7 days ago" &&
                          log[1].subject == L"Leading blank lines before the subject" && log[1].relDate == L"8 days ago" &&
                          log[2].author.empty() && log[2].subject.empty() && log[2].relDate == L"2 weeks ago";
        tc.Record(good, L"Stage12.Vcs.SvnLogXml",
                  log.empty() ? std::wstring(L"no entries") : (log[0].subject + L" | " + log[0].relDate));
    }

    // ---- times
    {
        const bool good = Svn::ParseIsoTimeUtc("2000-02-29T23:59:59.000000Z") == 951868799 &&
                          Svn::ParseIsoTimeUtc("2026-11-17T09:12:44Z") == kNow && Svn::ParseIsoTimeUtc("yesterday") == 0 &&
                          Svn::ParseIsoTimeUtc("2026-13-01T00:00:00Z") == 0 &&
                          Svn::RelativeTime(kNow - 5, kNow) == L"5 seconds ago" &&
                          Svn::RelativeTime(kNow - 3600, kNow) == L"60 minutes ago" &&
                          Svn::RelativeTime(kNow - 86400, kNow) == L"24 hours ago" &&
                          Svn::RelativeTime(kNow - 86400 * 400, kNow) == L"1 year, 1 month ago" &&
                          Svn::RelativeTime(kNow + 60, kNow) == L"in the future";
        tc.Record(good, L"Stage12.Vcs.SvnTimes", Svn::RelativeTime(kNow - 86400 * 400, kNow));
    }

    // ---- output panel texts
    {
        const std::wstring rev = Svn::FormatRevisionText(kLogVerboseXml);
        tc.Record(Has(rev, L"r1234 | alice | ") && Has(rev, L"   M /trunk/src/main.c\n") &&
                  Has(rev, L"   A /trunk/src/new.c (from /trunk/src/old.c:1233)\n") && Has(rev, L"\nFix the parser\n"),
                  L"Stage12.Vcs.SvnRevisionText");
        tc.Record(Has(Svn::FormatRevisionText("<?xml version=\"1.0\"?><log></log>"), L"no such revision"),
                  L"Stage12.Vcs.SvnRevisionTextUnknownRevision");

        const std::wstring upd = Svn::FormatUpdatesText(kUpdatesXml);
        tc.Record(Has(upd, L"2 items changed in the repository") && Has(upd, L"r1240") && Has(upd, L"   M   src\\main.c\n") &&
                  Has(upd, L"   A   docs\\new.md\n") && !Has(upd, L"local.c"),
                  L"Stage12.Vcs.SvnUpdatesText");
        tc.Record(Svn::FormatUpdatesText("<status><target path=\".\"><against revision=\"9\"/></target></status>") ==
                      L"Up to date with the repository (it is at r9).",
                  L"Stage12.Vcs.SvnUpdatesTextUpToDate");

        const std::wstring stat = Svn::DiffStatText(kDiffText);
        tc.Record(stat.rfind(L"src/main.c | 4 ++--\n", 0) == 0 && Has(stat, L"\nlogo.png   | Bin\n") &&
                  Has(stat, L"\nlib        | properties\n") && Has(stat, L"\n3 files changed, 2 insertions(+), 2 deletions(-)"),
                  L"Stage12.Vcs.SvnDiffStat", stat);
        tc.Record(Svn::DiffStatText("").empty(), L"Stage12.Vcs.SvnDiffStatEmpty");
    }

    // ---- repository URLs as folders (RepoBrowser): which ones, how they split, how they go up
    {
        using RepoBrowser::Kind;
        const bool urls = RepoBrowser::IsRepoUrl(L"svn://host/r") && RepoBrowser::IsRepoUrl(L"https://github.com/a/b") &&
                          RepoBrowser::IsRepoUrl(L"file:///C:/repos/x") && !RepoBrowser::IsRepoUrl(L"ftp://host/x") &&
                          !RepoBrowser::IsRepoUrl(L"C:\\work") && !RepoBrowser::IsRepoUrl(L"https://");
        tc.Record(urls, L"Stage12.Vcs.RepoUrlRecognised");

        RepoBrowser::GitUrlParts gh, gl, dotGit, gitScheme, none;
        const bool split =
            RepoBrowser::SplitGitUrl(L"https://github.com/owner/repo/tree/main/src/lib", gh) &&
            gh.repoUrl == L"https://github.com/owner/repo" && gh.ref == L"main" &&
            gh.base == L"https://github.com/owner/repo/tree/main" && gh.innerPath == L"src/lib" &&
            RepoBrowser::SplitGitUrl(L"https://gitlab.com/group/proj/-/tree/dev/docs", gl) &&
            gl.repoUrl == L"https://gitlab.com/group/proj" && gl.ref == L"dev" && gl.innerPath == L"docs" &&
            RepoBrowser::SplitGitUrl(L"https://example.org/x/project.git/src/a%20b", dotGit) &&
            dotGit.repoUrl == L"https://example.org/x/project.git" && dotGit.innerPath == L"src/a b" && dotGit.ref.empty() &&
            RepoBrowser::SplitGitUrl(L"git://host/repo", gitScheme) && gitScheme.repoUrl == L"git://host/repo" &&
            gitScheme.innerPath.empty() && !RepoBrowser::SplitGitUrl(L"https://svn.example.org/repos/x", none);
        tc.Record(split, L"Stage12.Vcs.RepoGitUrlSplit",
                  L"github: " + gh.repoUrl + L" ref=" + gh.ref + L" path=" + gh.innerPath + L"; gitlab: " + gl.base);

        // a branch name may hold '/': the server's branch and tag names (git ls-remote) say where it ends
        const char* kLsRemote = "1111111111111111111111111111111111111111\tHEAD\n"
                                "2222222222222222222222222222222222222222\trefs/heads/main\n"
                                "3333333333333333333333333333333333333333\trefs/heads/releases/v2\r\n"
                                "4444444444444444444444444444444444444444\trefs/heads/releases/v2/hotfix\n"
                                "5555555555555555555555555555555555555555\trefs/tags/v1.0\n"
                                "6666666666666666666666666666666666666666\trefs/tags/v1.0^{}\n";
        RepoBrowser::GitUrlParts slash, gitlabSlash;
        const bool slashRefs =
            RepoBrowser::RefParts(L"releases/v2/src/lib", kLsRemote) == 2 &&
            RepoBrowser::RefParts(L"releases/v2/hotfix/x", kLsRemote) == 3 &&
            RepoBrowser::RefParts(L"releases/v2", kLsRemote) == 2 &&
            RepoBrowser::RefParts(L"main/src", kLsRemote) == 1 &&
            RepoBrowser::RefParts(L"releases/v20/x", kLsRemote) == 1 &&
            RepoBrowser::RefParts(L"v1.0/docs", kLsRemote) == 1 &&
            RepoBrowser::SplitGitUrlText(L"https://github.com/o/r/tree/releases/v2/src/lib", 2, slash) &&
            slash.repoUrl == L"https://github.com/o/r" && slash.ref == L"releases/v2" &&
            slash.base == L"https://github.com/o/r/tree/releases/v2" && slash.innerPath == L"src/lib" &&
            RepoBrowser::SplitGitUrlText(L"https://gitlab.com/g/p/-/tree/feature/x", 2, gitlabSlash) &&
            gitlabSlash.ref == L"feature/x" && gitlabSlash.base == L"https://gitlab.com/g/p/-/tree/feature/x" &&
            gitlabSlash.innerPath.empty();
        tc.Record(slashRefs, L"Stage12.Vcs.RepoGitSlashBranch", slash.ref + L" | " + slash.base + L" | " + slash.innerPath);

        const bool kinds = RepoBrowser::GuessKind(L"svn://host/r/trunk") == Kind::Svn &&
                           RepoBrowser::GuessKind(L"svn+ssh://host/r") == Kind::Svn &&
                           RepoBrowser::GuessKind(L"https://host/r.git") == Kind::Git &&
                           RepoBrowser::GuessKind(L"https://github.com/a/b") == Kind::Git &&
                           RepoBrowser::GuessKind(L"https://svn.example.org/repos/x") == Kind::Unknown;
        tc.Record(kinds, L"Stage12.Vcs.RepoKindFromAddress");

        const std::wstring joined = RepoBrowser::JoinUrl(L"svn://host/r/trunk/", L"a b#c.txt");
        const bool paths = joined == L"svn://host/r/trunk/a%20b%23c.txt" && RepoBrowser::LeafName(joined) == L"a b#c.txt" &&
                           RepoBrowser::ParentUrl(L"https://github.com/o/r/src/x") == L"https://github.com/o/r/src" &&
                           RepoBrowser::ParentUrl(L"https://github.com/o/r").empty() &&
                           RepoBrowser::ParentUrl(L"https://github.com/o/r/tree/main/src") == L"https://github.com/o/r/tree/main" &&
                           RepoBrowser::ParentUrl(L"https://github.com/o/r/tree/main").empty() &&
                           RepoBrowser::ParentUrl(L"svn://host/r/trunk") == L"svn://host/r" &&
                           RepoBrowser::RootLabel(L"https://github.com/o/r/tree/main/src") == L"git: github.com/o/r @ main";
        tc.Record(paths, L"Stage12.Vcs.RepoUrlPaths", joined + L" | " + RepoBrowser::RootLabel(L"https://github.com/o/r/tree/main/src"));
    }

    // ---- listings: svn list --xml, git ls-tree -z
    {
        std::vector<FileEntry> svnItems;
        const bool parsed = RepoBrowser::ParseSvnListXml(kSvnListXml, L"https://svn.example.org/repos/x/trunk", true, Lang::EN, svnItems);
        const FileEntry* dir = FindEntry(svnItems, L"src");
        const FileEntry* readme = FindEntry(svnItems, L"README.md");
        const FileEntry* logo = FindEntry(svnItems, L"logo & icon.png");
        const bool good = parsed && svnItems.size() == 3 && dir && dir->isDirectory && readme && !readme->isDirectory &&
                          readme->size == 1536 && !readme->sizeStr.empty() && !readme->dateStr.empty() && readme->ext == L".md" &&
                          readme->fullPath == L"https://svn.example.org/repos/x/trunk/README.md" &&
                          logo && logo->isImage && logo->fullPath == L"https://svn.example.org/repos/x/trunk/logo%20&%20icon.png";
        tc.Record(good, L"Stage12.Vcs.RepoSvnListXml", readme ? (readme->sizeStr + L" | " + readme->dateStr + L" | " + readme->typeName) : std::wstring());

        const std::string lsTree = std::string("100644 blob 3b18e512dba79e4c8300dd08aeb37f8e728b8dad\tREADME.md") + '\0' +
                                   "040000 tree 9bdc1a5b8e5d1b0c2a1e6f8b3c4d5e6f7a8b9c0d\tsrc" + '\0' +
                                   "160000 commit 1111111111111111111111111111111111111111\tthird_party" + '\0' +
                                   "120000 blob 2222222222222222222222222222222222222222\tlink" + '\0';
        std::vector<FileEntry> gitItems;
        RepoBrowser::ParseGitLsTree(lsTree, L"https://github.com/o/r", true, Lang::EN, gitItems);
        const FileEntry* gReadme = FindEntry(gitItems, L"README.md");
        const FileEntry* gSrc = FindEntry(gitItems, L"src");
        const FileEntry* gSub = FindEntry(gitItems, L"third_party");
        const bool gitGood = gitItems.size() == 4 && gReadme && !gReadme->isDirectory && gReadme->sizeStr.empty() &&
                             gSrc && gSrc->isDirectory && gSrc->fullPath == L"https://github.com/o/r/src" &&
                             gSub && gSub->isDirectory && Has(gSub->typeName, L"submodule") && FindEntry(gitItems, L"link");
        tc.Record(gitGood, L"Stage12.Vcs.RepoGitLsTree", L"entries=" + std::to_wstring(gitItems.size()));
    }

    // ---- finding the working copy: .svn (a folder) or .git, the nearest one wins
    const std::wstring base = ScratchTestDir() + L"\\vcs";
    DeleteTreeForTest(base);
    {
        const bool made = MakeDirs(base + L"\\wc\\.svn") && MakeDirs(base + L"\\wc\\sub\\deeper") &&
                          MakeDirs(base + L"\\wc\\sub\\inner\\.git") && MakeDirs(base + L"\\wc\\sub\\inner\\x") &&
                          MakeDirs(base + L"\\both\\.git") && MakeDirs(base + L"\\both\\.svn") && MakeDirs(base + L"\\fake");
        const std::wstring fakeSvnFile = base + L"\\fake\\.svn";       // a file, not a working copy
        PathUtils::WriteFileBytes(fakeSvnFile, reinterpret_cast<const uint8_t*>("x"), 1);
        VcsKind k1 = VcsKind::Git, k2 = VcsKind::Svn, k3 = VcsKind::Svn, k4 = VcsKind::Svn;
        const std::wstring r1 = GitIntegration::FindRepoRoot(base + L"\\wc\\sub\\deeper", &k1);
        const std::wstring r2 = GitIntegration::FindRepoRoot(base + L"\\wc\\sub\\inner\\x", &k2);
        const std::wstring r3 = GitIntegration::FindRepoRoot(base + L"\\both", &k3);
        const std::wstring r4 = GitIntegration::FindRepoRoot(base + L"\\fake", &k4);
        const bool good = made && r1 == base + L"\\wc" && k1 == VcsKind::Svn && r2 == base + L"\\wc\\sub\\inner" && k2 == VcsKind::Git &&
                          r3 == base + L"\\both" && k3 == VcsKind::Git && r4 != base + L"\\fake" && k4 != VcsKind::Svn;
        tc.Record(good, L"Stage12.Vcs.FindWorkingCopyRoot", L"svn=" + r1 + L" git=" + r2 + L" both=" + r3);
    }

    // ---- the worker on a folder that only looks like a working copy: it must report, not hang
    {
        GitIntegration g;
        const std::wstring root = base + L"\\wc";
        g.RequestRefresh(root, VcsKind::Svn);
        const bool idle = WaitIdle(g, 60000);
        const GitRepoState s = g.SnapshotState();
        const bool haveSvn = !Svn::FindSvnExe().empty();
        const bool good = idle && s.kind == VcsKind::Svn && s.root == root &&
                          (haveSvn ? (!s.valid && !s.lastError.empty()) : s.clientMissing);
        tc.Record(good, L"Stage12.Vcs.SvnWorkerOnFakeWorkingCopy",
                  haveSvn ? (L"svn found; error: " + s.lastError) : std::wstring(L"svn.exe not installed: reported as missing"));
    }

    // ---- a real repository, when a Subversion client with svnadmin is installed
    {
        const std::wstring svnExe = Svn::FindSvnExe();
        const std::wstring svnadmin = svnExe.empty() ? std::wstring()
                                                     : PathUtils::GetParentPath(svnExe) + L"\\svnadmin.exe";
        if (svnExe.empty() || GetFileAttributesW(svnadmin.c_str()) == INVALID_FILE_ATTRIBUTES) {
            tc.Record(true, L"Stage12.Vcs.SvnRealWorkingCopy", L"skipped: no svn.exe with svnadmin.exe installed");
        } else {
            std::atomic<bool> stop{ false };
            ToolProcess::Options o;
            o.timeoutMs = 30000;
            const std::wstring repo = base + L"\\repo", wc = base + L"\\live";
            std::wstring url = L"file:///" + repo;
            for (wchar_t& c : url) if (c == L'\\') c = L'/';
            bool setup = ToolProcess::Run(svnadmin, { L"create", repo }, o, stop).ok() &&
                         Svn::Run(base, { L"checkout", url, wc }, 30000, stop).ok();
            const std::string text = "hello\n";
            setup = setup && PathUtils::WriteFileBytes(wc + L"\\a.txt", reinterpret_cast<const uint8_t*>(text.data()), text.size()) &&
                    Svn::Run(wc, { L"add", L"a.txt" }, 30000, stop).ok() &&
                    Svn::Run(wc, { L"commit", L"-m", L"First revision" }, 30000, stop).ok() &&
                    Svn::Run(wc, { L"update" }, 30000, stop).ok();
            const std::string changed = "hello\nworld\n";
            setup = setup && PathUtils::WriteFileBytes(wc + L"\\a.txt", reinterpret_cast<const uint8_t*>(changed.data()), changed.size()) &&
                    PathUtils::WriteFileBytes(wc + L"\\new.txt", reinterpret_cast<const uint8_t*>(text.data()), text.size());
            GitRepoState s;
            std::wstring diff;
            if (setup) {
                GitIntegration g;
                g.RequestRefresh(wc, VcsKind::Svn, true);
                WaitIdle(g, 60000);
                s = g.SnapshotState();
                g.RunAction(wc, GitAction::ShowDiffStat);
                WaitIdle(g, 60000);
                diff = g.SnapshotOutput();
            }
            const GitFileChange* a = FindChange(s, L"a.txt");
            const GitFileChange* n = FindChange(s, L"new.txt");
            const bool good = setup && s.valid && s.kind == VcsKind::Svn && s.svn.revision == 1 && s.headShort == L"r1" &&
                              a && a->indexStatus == L'M' && n && n->indexStatus == L'?' &&
                              !s.commits.empty() && s.commits[0].subject == L"First revision" && s.commits[0].shortHash == L"r1" &&
                              Has(diff, L"a.txt | 1 +");
            tc.Record(good, L"Stage12.Vcs.SvnRealWorkingCopy",
                      setup ? (L"rev=" + std::to_wstring(s.svn.revision) + L" changes=" + std::to_wstring(s.changes.size()) +
                               L" commits=" + std::to_wstring(s.commits.size()) + L" error=" + s.lastError + s.logError)
                            : std::wstring(L"could not create the test repository"));

            // The same repository browsed by its URL, as a folder of the main view would be
            RepoBrowser::SetCacheRoot(base + L"\\cache");
            std::vector<FileEntry> items;
            std::wstring message, local;
            const bool listed = setup && WaitListing(url, items, &message) == RepoBrowser::ListState::Ready;
            const FileEntry* aTxt = FindEntry(items, L"a.txt");
            const bool fetched = listed && RepoBrowser::FetchFileToTemp(url + L"/a.txt", local) && ReadAll(local) == "hello\n";
            RepoBrowser::Shutdown(true);
            RepoBrowser::SetCacheRoot(L"");
            tc.Record(listed && aTxt && aTxt->size == 6 && fetched && RepoBrowser::ParentUrl(url).empty(),
                      L"Stage12.Vcs.SvnBrowseRepositoryUrl", L"items=" + std::to_wstring(items.size()) + L" " + message);
        }
    }

    // ---- a git repository browsed by its URL: a local bare repository, cloned shallow without the files' contents
    {
        const std::wstring gitExe = GitIntegration::GitExePath();
        if (gitExe.empty()) {
            tc.Record(true, L"Stage12.Vcs.GitBrowseRepositoryUrl", L"skipped: git is not installed");
        } else {
            std::atomic<bool> stop{ false };
            auto git = [&](const std::wstring& dir, const std::vector<std::wstring>& args) {
                ToolProcess::Options o;
                o.timeoutMs = 60000;
                GitIntegration::ApplyGitEnvironment(o, false);
                std::vector<std::wstring> all = { L"-C", dir, L"-c", L"user.name=Explorer2 self-test", L"-c", L"user.email=selftest@example.invalid",
                                                  L"-c", L"commit.gpgsign=false", L"-c", L"init.defaultBranch=main" };
                all.insert(all.end(), args.begin(), args.end());
                return ToolProcess::Run(gitExe, all, o, stop).ok();
            };
            const std::wstring work = base + L"\\gitwork", bare = base + L"\\origin.git";
            const std::string readme = "hello git\n", mainC = "int main() { return 0; }\n";
            bool setup = MakeDirs(work + L"\\src") &&
                         PathUtils::WriteFileBytes(work + L"\\README.md", readme.data(), readme.size()) &&
                         PathUtils::WriteFileBytes(work + L"\\src\\main.c", mainC.data(), mainC.size()) &&
                         git(work, { L"init", L"-q" }) && git(work, { L"add", L"." }) && git(work, { L"commit", L"-q", L"-m", L"first" }) &&
                         git(base, { L"clone", L"-q", L"--bare", work, bare }) &&
                         git(bare, { L"config", L"uploadpack.allowFilter", L"true" }) &&
                         git(bare, { L"config", L"uploadpack.allowAnySHA1InWant", L"true" });
            std::wstring url = L"file:///" + bare;
            for (wchar_t& c : url) if (c == L'\\') c = L'/';

            RepoBrowser::SetCacheRoot(base + L"\\cache");
            std::vector<FileEntry> rootItems, srcItems;
            std::wstring message, local;
            const bool rootListed = setup && WaitListing(url, rootItems, &message) == RepoBrowser::ListState::Ready;
            const bool srcListed = rootListed && WaitListing(url + L"/src", srcItems, &message) == RepoBrowser::ListState::Ready;
            const FileEntry* r = FindEntry(rootItems, L"README.md");
            const FileEntry* s = FindEntry(rootItems, L"src");
            const FileEntry* m = FindEntry(srcItems, L"main.c");
            const bool fetched = srcListed && RepoBrowser::FetchFileToTemp(url + L"/src/main.c", local) && ReadAll(local) == mainC;
            const bool good = rootListed && rootItems.size() == 2 && r && !r->isDirectory && s && s->isDirectory &&
                              srcListed && srcItems.size() == 1 && m && fetched &&
                              RepoBrowser::ParentUrl(url + L"/src") == url && RepoBrowser::ParentUrl(url).empty();
            // a folder that is not there says so instead of listing nothing
            std::vector<FileEntry> missing;
            const bool missingFails = rootListed && WaitListing(url + L"/nope", missing) == RepoBrowser::ListState::Failed;
            RepoBrowser::Shutdown(true);
            RepoBrowser::SetCacheRoot(L"");
            tc.Record(good && missingFails, L"Stage12.Vcs.GitBrowseRepositoryUrl",
                      setup ? (L"root=" + std::to_wstring(rootItems.size()) + L" src=" + std::to_wstring(srcItems.size()) +
                               L" fetched=" + (fetched ? L"yes" : L"no") + L" " + message)
                            : std::wstring(L"could not create the test repository"));
        }
    }
    DeleteTreeForTest(base);
}

} // namespace Explorer2::SelfTestSuite
