#pragma once
// NetShares.h - network folders by their UNC path, and drives mapped to them.
//
// "\\server" alone is shown as the list of the server's shared folders (read-only, like a folder of shares). The
// list is read in the background: GetShares never waits for the network, and WM_APP_DIR_CHANGED is posted to the
// window given to SetNotifyWindow when it arrives.
//
// Before the window opens "\\server\share\..." or a drive mapped to a share ("Z:\..."), CheckReachable asks the
// server on a background thread and waits only a moment for the answer: Windows waits about 20 s for a host that
// does not answer, and that wait must not freeze the window. A late answer posts WM_APP_NET_REACHED.
#include "Localization.h"
#include "Types.h"
#include <string>
#include <vector>

namespace Explorer2::NetShares {

enum class ListState { Ready, Loading, Failed };
enum class Reach { Ok, Pending, Failed };

bool IsUncPath(const std::wstring& path);            // \\server or \\server\share\... (not \\?\ or \\.\ device paths)
bool IsServerRoot(const std::wstring& path);         // \\server alone: the list of its shares
bool IsNetworkPath(const std::wstring& path);        // a UNC path, or a path on a drive mapped to a share
std::wstring ServerName(const std::wstring& path);   // "server" of \\server\share\x ("" when the path is not UNC)
std::wstring ShareRoot(const std::wstring& path);    // "\\server\share" ("" for \\server alone or a non-UNC path)

void SetNotifyWindow(HWND hwnd);
void Shutdown();                                     // nothing is posted to the window any more

// Servers known without asking the network: those of mapped drives (HKCU\Network) and those met in this session,
// as written ("NAS191725"), sorted
std::vector<std::wstring> KnownServers();

// The shares of \\server when they are known (Ready); otherwise starts asking the server and says Loading, or says
// Failed with the reason in outMessage. Never waits for the network.
ListState GetShares(const std::wstring& serverRoot, Lang lang, std::vector<FileEntry>& out,
                    std::wstring* outMessage = nullptr);
ListState PeekState(const std::wstring& serverRoot, Lang lang, std::wstring* outMessage = nullptr);   // starts nothing
void Invalidate(const std::wstring& path);           // Refresh: the server is asked again (its shares, and whether it answers)

// Whether the server of a network path answers, waiting at most waitMs. Ok also for a path that is not on the
// network. Pending: still being asked (WM_APP_NET_REACHED is posted when the answer comes). Failed: outMessage says why.
Reach CheckReachable(const std::wstring& path, DWORD waitMs, Lang lang, std::wstring* outMessage = nullptr);

} // namespace Explorer2::NetShares
