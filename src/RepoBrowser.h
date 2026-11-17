#pragma once
// RepoBrowser.h - Subversion and git repositories as read-only folders of the main view. A repository URL typed in the
// address bar (svn://, svn+ssh://, git://, ssh://, http(s)://, file://) is listed like a folder; its files open,
// preview and copy out through a copy in the temp folder. Nothing is ever written to a repository.
//
// Subversion: every folder is its own URL. `svn info` finds the repository root once, `svn list --xml` lists a
// folder (sizes and dates included) and `svn cat` reads a file.
// git: a remote repository cannot be listed without fetching it, so the first visit makes a shallow clone that holds
// the folder structure only (--depth 1 --filter=blob:none --no-checkout) under %TEMP%\Explorer2_Repos. `git ls-tree`
// lists a folder from it, and `git cat-file` reads a file, which git then downloads alone. Sizes are not shown for git:
// knowing them would mean downloading every file. GitHub / GitLab folder links (.../tree/<branch>/<path>) open that
// branch at that folder; when the branch name may hold '/', `git ls-remote` says where it ends.
//
// Listings are read on a background thread and kept: GetListing never waits for the network, and WM_APP_DIR_CHANGED
// is posted to the window given to SetNotifyWindow when a listing arrives. FetchFileToTemp does wait (worker threads,
// and opening a single file).
#include "Localization.h"
#include "Types.h"
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::RepoBrowser {

enum class Kind { Unknown, Svn, Git };
enum class ListState { Ready, Loading, Failed };

// A path this module handles: svn://, svn+ssh://, git://, git+ssh://, ssh://, http(s):// and file:// URLs (ftp:// is
// FtpClient's)
bool IsRepoUrl(const std::wstring& path);

void SetNotifyWindow(HWND hwnd);
// Stops the background work and the tool it runs. At program exit nothing starts it again (listings asked for later
// stay Loading); `allowRestart` is for the self-tests, which browse again afterwards.
void Shutdown(bool allowRestart = false);
// Where the git clones and the temp copies of files go (a folder ending in '\'); empty = %TEMP% (self-tests)
void SetCacheRoot(const std::wstring& folder);

// The folder's entries when they are known (Ready); otherwise starts reading them and says Loading, or says Failed
// with the reason in outMessage. Never waits for the network.
ListState GetListing(const std::wstring& url, bool showExtensions, Lang lang, std::vector<FileEntry>& out,
                     std::wstring* outMessage = nullptr);
ListState PeekState(const std::wstring& url, std::wstring* outMessage = nullptr);   // without starting anything
// Refresh: the folder is read again; for git, the repository is fetched again first
void Invalidate(const std::wstring& url);

// A file of a repository as a local copy in the temp folder (made once per file version, then reused)
bool FetchFileToTemp(const std::wstring& url, std::wstring& outLocalPath, std::wstring* outError = nullptr);

std::wstring ParentUrl(const std::wstring& url);        // empty at the root of the repository
std::wstring LeafName(const std::wstring& url);         // the last part, %XX decoded
std::wstring JoinUrl(const std::wstring& dirUrl, const std::wstring& name);
std::wstring RootUrl(const std::wstring& url);          // the repository root (or the URL itself while not known)
std::wstring RootLabel(const std::wstring& url);        // "SVN: svn.example.org/repos/x", "git: owner/repo"

// ---- for the self-tests
struct GitUrlParts {
    std::wstring repoUrl;       // what git clones
    std::wstring ref;           // branch or tag from a .../tree/<ref>/... link; empty = the default branch
    std::wstring base;          // the URL of the repository's top folder (repoUrl, or repoUrl + "/tree/<ref>")
    std::wstring innerPath;     // folder or file inside the repository, '/' separated, decoded; empty = the top
};
bool SplitGitUrl(const std::wstring& url, GitUrlParts& out);   // false when the URL does not say it is git
// What the address alone says, the branch name of a ".../tree/<ref>/..." link taking `refParts` '/'-separated parts
bool SplitGitUrlText(const std::wstring& url, size_t refParts, GitUrlParts& out);
// How many parts of "<ref>/<path>" are the branch or tag name, from `git ls-remote` output (1 when none matches)
size_t RefParts(const std::wstring& refAndPath, std::string_view lsRemoteOutput);
Kind GuessKind(const std::wstring& url);                       // from the URL alone (Unknown: has to be asked)
bool ParseSvnListXml(std::string_view xml, const std::wstring& dirUrl, bool showExtensions, Lang lang,
                     std::vector<FileEntry>& out);
bool ParseGitLsTree(std::string_view text, const std::wstring& dirUrl, bool showExtensions, Lang lang,
                    std::vector<FileEntry>& out);

} // namespace Explorer2::RepoBrowser
