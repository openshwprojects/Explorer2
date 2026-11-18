#include "NetShares.h"

#include "PathUtils.h"
#include "VirtualFolders.h"

#include <winnetwk.h>
#ifndef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <errno.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace Explorer2::NetShares {

namespace {

constexpr ULONGLONG kAnswerFreshMs = 5000;      // a server that answered is not asked again for this long
constexpr ULONGLONG kNoAnswerFreshMs = 10000;   // nor one that did not (trying again at once returns at once)

struct ShareItem {
    std::wstring name;
    std::wstring comment;
};

struct ServerShares {
    ListState state = ListState::Loading;
    bool asked = false;              // a read was started; its answer is kept until Invalidate
    unsigned generation = 0;         // the answer of a read started before an Invalidate is dropped
    std::vector<ShareItem> shares;
    DWORD error = NO_ERROR;
    std::wstring errorText;          // the network provider's own words, when it gave some
};

enum class ProbeState { None, Running, Answered, NoAnswer };

struct Probe {
    ProbeState state = ProbeState::None;
    ULONGLONG tick = 0;              // when the answer came
    DWORD error = NO_ERROR;
    std::wstring remote;             // what a mapped drive points at ("\\server\share"), for the message
};

struct State {
    std::mutex mutex;
    std::condition_variable cv;
    std::map<std::wstring, ServerShares> servers;   // key: lower-case server name
    std::map<std::wstring, Probe> probes;           // key: lower-case "\\server\share" or "z:\"
    std::map<std::wstring, std::wstring> names;     // servers met in this session: lower-case -> as first written
    HWND notify = nullptr;
};

// Background threads hold their own reference: the state outlives a thread still waiting for a server at exit
std::shared_ptr<State> TheState() {
    static const std::shared_ptr<State> s = std::make_shared<State>();
    return s;
}

std::wstring ErrorText(DWORD err) {
    wchar_t buf[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0, buf, 511, nullptr);
    std::wstring s = buf;
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'.')) s.pop_back();
    if (s.empty()) s = L"error";
    return s + L" (" + std::to_wstring(err) + L")";
}

// The server (or the way to it) did not answer, as opposed to "no such folder" or "no access", which only a server
// that answers can say
bool IsNoAnswer(DWORD e) {
    switch (e) {
    case ERROR_REM_NOT_LIST: case ERROR_BAD_NETPATH: case ERROR_NETWORK_BUSY: case ERROR_DEV_NOT_EXIST:
    case ERROR_BAD_NET_RESP: case ERROR_UNEXP_NET_ERR: case ERROR_NETNAME_DELETED: case ERROR_SEM_TIMEOUT:
    case ERROR_NO_NET_OR_BAD_PATH: case ERROR_NO_NETWORK: case ERROR_CONNECTION_REFUSED:
    case ERROR_NETWORK_UNREACHABLE: case ERROR_HOST_UNREACHABLE: case ERROR_PORT_UNREACHABLE:
    case ERROR_CONNECTION_ABORTED: case ERROR_HOST_DOWN:
        return true;
    default:
        return false;
    }
}

// "\\server\share\x" -> "server", "share" (empty when the path is the server alone)
bool SplitUnc(const std::wstring& path, std::wstring& server, std::wstring& share) {
    server.clear();
    share.clear();
    if (!IsUncPath(path)) return false;
    const size_t s1 = path.find_first_of(L"\\/", 2);
    server = path.substr(2, s1 == std::wstring::npos ? std::wstring::npos : s1 - 2);
    if (s1 != std::wstring::npos) {
        const size_t s2 = path.find_first_of(L"\\/", s1 + 1);
        share = path.substr(s1 + 1, s2 == std::wstring::npos ? std::wstring::npos : s2 - s1 - 1);
    }
    return !server.empty();
}

// What is asked: the share's top folder, or the top of a mapped drive
std::wstring ProbeTarget(const std::wstring& path) {
    if (IsUncPath(path)) return ShareRoot(path);
    if (path.size() >= 2 && path[1] == L':') return std::wstring{ static_cast<wchar_t>(std::towupper(path[0])), L':', L'\\' };
    return {};
}

bool IsDriveTarget(const std::wstring& target) { return target.size() == 3 && target[1] == L':'; }

#ifndef _WIN32
struct LinuxMountShare {
    std::wstring server;
    std::wstring share;
    std::string mountPoint;
    std::string fsType;
};

std::vector<LinuxMountShare> EnumerateLinuxNetworkMounts() {
    std::vector<LinuxMountShare> out;
    std::ifstream mf("/proc/self/mounts");
    std::string line;
    while (std::getline(mf, line)) {
        std::istringstream iss(line);
        std::string dev, mnt, fstype;
        if (!(iss >> dev >> mnt >> fstype)) continue;
        if (fstype != "cifs" && fstype != "smb3" && fstype != "smbfs" && fstype != "nfs" && fstype != "nfs4") continue;
        LinuxMountShare ms;
        ms.mountPoint = mnt;
        ms.fsType = fstype;
        if (dev.size() > 2 && dev[0] == '/' && dev[1] == '/') {
            const size_t sl = dev.find('/', 2);
            ms.server = PathUtils::Utf8ToWide(dev.substr(2, sl == std::string::npos ? std::string::npos : sl - 2));
            if (sl != std::string::npos && sl + 1 < dev.size()) {
                const size_t sl2 = dev.find('/', sl + 1);
                ms.share = PathUtils::Utf8ToWide(dev.substr(sl + 1, sl2 == std::string::npos ? std::string::npos : sl2 - sl - 1));
            }
        } else {
            const size_t col = dev.find(':');
            if (col != std::string::npos && col > 0) {
                ms.server = PathUtils::Utf8ToWide(dev.substr(0, col));
                std::string exp = dev.substr(col + 1);
                while (!exp.empty() && exp.front() == '/') exp.erase(exp.begin());
                const size_t sl = exp.find('/');
                ms.share = PathUtils::Utf8ToWide(exp.substr(0, sl));
            }
        }
        if (!ms.server.empty() && !ms.share.empty()) out.push_back(std::move(ms));
    }
    return out;
}

bool IsLocalHostNameLinux(const std::wstring& server) {
    const std::wstring low = PathUtils::ToLower(server);
    if (low == L"localhost" || low == L"127.0.0.1" || low == L"::1" || low == L"[::1]") return true;
    char hn[256] = {};
    if (::gethostname(hn, sizeof(hn) - 1) == 0 && hn[0]) {
        if (_wcsicmp(server.c_str(), PathUtils::Utf8ToWide(hn).c_str()) == 0) return true;
    }
    return false;
}

DWORD ProbeUncServerTcpLinux(const std::wstring& server, int timeoutMs) {
    if (IsLocalHostNameLinux(server)) {
        // Localhost always answers immediately (either SMB port 445 is open -> NO_ERROR, or not shared -> ERROR_BAD_NET_NAME)
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
        if (fd >= 0) {
            sockaddr_in sin = {};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(445);
            sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&sin), sizeof(sin));
            bool open = (rc == 0);
            if (!open && errno == EINPROGRESS) {
                struct pollfd pfd = { fd, POLLOUT, 0 };
                if (::poll(&pfd, 1, 50) > 0) {
                    int soErr = 0;
                    socklen_t sl = sizeof(soErr);
                    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &sl) == 0 && soErr == 0) open = true;
                }
            }
            ::close(fd);
            if (open) return NO_ERROR;
        }
        return ERROR_BAD_NET_NAME; // Server answered; requested share does not exist
    }

    ADDRINFOW hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    ADDRINFOW* res = nullptr;
    if (GetAddrInfoW(server.c_str(), L"445", &hints, &res) != 0 || !res) {
        return ERROR_BAD_NETPATH;
    }
    int fd = ::socket(res->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd < 0) {
        FreeAddrInfoW(res);
        return ERROR_BAD_NETPATH;
    }
    int rc = ::connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen));
    FreeAddrInfoW(res);
    if (rc == 0) {
        ::close(fd);
        return NO_ERROR;
    }
    if (errno == EINPROGRESS) {
        struct pollfd pfd = { fd, POLLOUT, 0 };
        const int pr = ::poll(&pfd, 1, timeoutMs);
        int soErr = ETIMEDOUT;
        if (pr > 0) {
            socklen_t sl = sizeof(soErr);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &sl);
        }
        ::close(fd);
        if (pr > 0 && soErr == 0) return NO_ERROR;
        if (pr > 0 && soErr == ECONNREFUSED) return ERROR_REM_NOT_LIST;
        return ERROR_BAD_NETPATH;
    }
    ::close(fd);
    return ERROR_BAD_NETPATH;
}
#endif

void StartProbe(const std::shared_ptr<State>& st, const std::wstring& key, const std::wstring& target) {
    std::thread([st, key, target]() {
        std::wstring remote;
        if (IsDriveTarget(target)) {
            const wchar_t local[3] = { target[0], L':', 0 };
            wchar_t buf[MAX_PATH] = {};
            DWORD len = MAX_PATH;
            if (WNetGetConnectionW(local, buf, &len) == NO_ERROR) remote = buf;
        }
#ifndef _WIN32
        DWORD err = NO_ERROR;
        std::wstring server, share;
        if (SplitUnc(target, server, share)) {
            bool foundMount = false;
            for (const auto& m : EnumerateLinuxNetworkMounts()) {
                if (_wcsicmp(m.server.c_str(), server.c_str()) == 0 &&
                    (share.empty() || _wcsicmp(m.share.c_str(), share.c_str()) == 0)) {
                    struct stat mst = {};
                    err = (::stat(m.mountPoint.c_str(), &mst) == 0) ? NO_ERROR : ERROR_BAD_NETPATH;
                    foundMount = true;
                    break;
                }
            }
            if (!foundMount) {
                err = ProbeUncServerTcpLinux(server, 1200);
            }
        } else {
            const DWORD attrs = GetFileAttributesW(target.c_str());
            err = (attrs == INVALID_FILE_ATTRIBUTES) ? GetLastError() : NO_ERROR;
        }
#else
        const DWORD attrs = GetFileAttributesW(target.c_str());
        const DWORD err = (attrs == INVALID_FILE_ATTRIBUTES) ? GetLastError() : NO_ERROR;
#endif
        HWND notify = nullptr;
        {
            std::lock_guard<std::mutex> lk(st->mutex);
            Probe& p = st->probes[key];
            p.state = IsNoAnswer(err) ? ProbeState::NoAnswer : ProbeState::Answered;
            p.error = err;
            p.tick = GetTickCount64();
            p.remote = remote;
            notify = st->notify;
        }
        st->cv.notify_all();
        if (notify) PostMessageW(notify, WM_APP_NET_REACHED, 0, 0);
    }).detach();
}

std::wstring NoAnswerMessage(const std::wstring& target, const Probe& p, Lang lang) {
    const bool pl = (lang == Lang::PL);
    std::wstring what = target;
    if (IsDriveTarget(target)) {
        what = std::wstring(pl ? L"dyskiem sieciowym " : L"the network drive ") + target.substr(0, 2);
        if (!p.remote.empty()) what += L" (" + p.remote + L")";
    }
    return std::wstring(pl ? L"Brak połączenia z " : L"No connection to ") + what + L": " + ErrorText(p.error);
}

DWORD ReadShares(const std::wstring& server, std::vector<ShareItem>& out, std::wstring& errorText) {
#ifndef _WIN32
    for (const auto& m : EnumerateLinuxNetworkMounts()) {
        if (_wcsicmp(m.server.c_str(), server.c_str()) == 0) {
            bool dup = false;
            for (const auto& ex : out) {
                if (_wcsicmp(ex.name.c_str(), m.share.c_str()) == 0) { dup = true; break; }
            }
            if (!dup) {
                ShareItem s;
                s.name = m.share;
                s.comment = PathUtils::Utf8ToWide(m.fsType + " @ " + m.mountPoint);
                out.push_back(std::move(s));
            }
        }
    }
    if (!out.empty() || IsLocalHostNameLinux(server)) return NO_ERROR;
    const DWORD probe = ProbeUncServerTcpLinux(server, 1200);
    if (probe == NO_ERROR) return NO_ERROR;
    errorText = L"SMB/NFS shares not mounted or server unreachable";
    return probe;
#else
    std::wstring remote = L"\\\\" + server;
    NETRESOURCEW nr = {};
    nr.dwScope = RESOURCE_GLOBALNET;
    nr.dwType = RESOURCETYPE_DISK;
    nr.dwDisplayType = RESOURCEDISPLAYTYPE_SERVER;
    nr.dwUsage = RESOURCEUSAGE_CONTAINER;
    nr.lpRemoteName = remote.data();
    HANDLE h = nullptr;
    DWORD r = WNetOpenEnumW(RESOURCE_GLOBALNET, RESOURCETYPE_DISK, 0, &nr, &h);
    if (r == NO_ERROR) {
        std::vector<BYTE> buf(64 * 1024);
        for (;;) {
            DWORD count = 0xFFFFFFFF;
            DWORD size = static_cast<DWORD>(buf.size());
            r = WNetEnumResourceW(h, &count, buf.data(), &size);
            if (r == ERROR_MORE_DATA && size > buf.size()) {     // not even one entry fits: size is what it needs
                buf.resize(size);
                continue;
            }
            if (r != NO_ERROR) break;
            const NETRESOURCEW* items = reinterpret_cast<const NETRESOURCEW*>(buf.data());
            for (DWORD i = 0; i < count; ++i) {
                if (!items[i].lpRemoteName) continue;
                const std::wstring rn = items[i].lpRemoteName;   // "\\server\share"
                const size_t slash = rn.find_last_of(L"\\/");
                if (slash == std::wstring::npos || slash < 2 || slash + 1 >= rn.size()) continue;
                ShareItem s;
                s.name = rn.substr(slash + 1);
                if (items[i].lpComment) s.comment = items[i].lpComment;
                out.push_back(std::move(s));
            }
        }
        WNetCloseEnum(h);
        if (r == ERROR_NO_MORE_ITEMS) r = NO_ERROR;
    }
    if (r == ERROR_EXTENDED_ERROR) {
        DWORD code = 0;
        wchar_t text[512] = {}, provider[128] = {};
        if (WNetGetLastErrorW(&code, text, 511, provider, 127) == NO_ERROR && text[0]) errorText = text;
    }
    return r;
#endif
}

std::wstring ListMessage(const std::wstring& server, const ServerShares& s, Lang lang) {
    if (s.state != ListState::Failed) return {};
    const bool pl = (lang == Lang::PL);
    const std::wstring why = s.errorText.empty() ? ErrorText(s.error) : s.errorText;
    return std::wstring(pl ? L"Serwer \\\\" : L"The server \\\\") + server +
           (pl ? L" nie podał listy udostępnionych folderów: " : L" did not give its list of shared folders: ") + why;
}

} // namespace

bool IsUncPath(const std::wstring& path) {
    return path.size() > 2 && path[0] == L'\\' && path[1] == L'\\' && path[2] != L'\\' && path[2] != L'/' &&
           path[2] != L'?' && path[2] != L'.';
}

bool IsServerRoot(const std::wstring& path) { return IsUncServerRoot(path); }

bool IsNetworkPath(const std::wstring& path) {
    if (IsUncPath(path)) return true;
    if (path.size() < 2 || path[1] != L':') return false;
    const wchar_t root[4] = { path[0], L':', L'\\', 0 };
    return GetDriveTypeW(root) == DRIVE_REMOTE;           // the drive's type is known locally: no network wait
}

std::wstring ServerName(const std::wstring& path) {
    std::wstring server, share;
    SplitUnc(path, server, share);
    return server;
}

std::wstring ShareRoot(const std::wstring& path) {
    std::wstring server, share;
    if (!SplitUnc(path, server, share) || share.empty()) return {};
    return L"\\\\" + server + L"\\" + share;
}

void SetNotifyWindow(HWND hwnd) {
    const std::shared_ptr<State> st = TheState();
    std::lock_guard<std::mutex> lk(st->mutex);
    st->notify = hwnd;
}

void Shutdown() { SetNotifyWindow(nullptr); }

std::vector<std::wstring> KnownServers() {
    std::map<std::wstring, std::wstring> byKey;      // lower-case -> as written
    // Mapped drives: HKCU\Network\<letter>\RemotePath = "\\server\share" (read locally, no network)
    HKEY net = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Network", 0, KEY_READ, &net) == ERROR_SUCCESS) {
        wchar_t sub[64] = {};
        for (DWORD i = 0;; ++i) {
            DWORD subLen = 64;
            if (RegEnumKeyExW(net, i, sub, &subLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            wchar_t remote[MAX_PATH] = {};
            DWORD cb = sizeof(remote) - sizeof(wchar_t);
            if (RegGetValueW(net, sub, L"RemotePath", RRF_RT_REG_SZ, nullptr, remote, &cb) == ERROR_SUCCESS) {
                const std::wstring server = ServerName(remote);
                if (!server.empty()) byKey.emplace(PathUtils::ToLower(server), server);
            }
        }
        RegCloseKey(net);
    }
    {
        const std::shared_ptr<State> st = TheState();
        std::lock_guard<std::mutex> lk(st->mutex);
        for (const auto& [key, name] : st->names) byKey.emplace(key, name);
    }
    std::vector<std::wstring> out;
    for (const auto& [key, name] : byKey) out.push_back(name);
    return out;
}

ListState GetShares(const std::wstring& serverRoot, Lang lang, std::vector<FileEntry>& out, std::wstring* outMessage) {
    out.clear();
    std::wstring server, share;
    if (!SplitUnc(serverRoot, server, share)) return ListState::Failed;
    const std::wstring key = PathUtils::ToLower(server);
    const std::shared_ptr<State> st = TheState();
    std::lock_guard<std::mutex> lk(st->mutex);
    st->names.emplace(key, server);
    ServerShares& s = st->servers[key];
    if (!s.asked) {
        s.asked = true;
        s.state = ListState::Loading;
        const unsigned generation = ++s.generation;
        std::thread([st, key, server, generation]() {
            std::vector<ShareItem> shares;
            std::wstring text;
            const DWORD err = ReadShares(server, shares, text);
            HWND notify = nullptr;
            {
                std::lock_guard<std::mutex> lk2(st->mutex);
                ServerShares& sv = st->servers[key];
                if (sv.generation != generation) return;
                sv.shares = std::move(shares);
                sv.error = err;
                sv.errorText = text;
                sv.state = (err == NO_ERROR) ? ListState::Ready : ListState::Failed;
                notify = st->notify;
            }
            if (notify) PostMessageW(notify, WM_APP_DIR_CHANGED, 0, 0);
        }).detach();
        return ListState::Loading;
    }
    if (outMessage) *outMessage = ListMessage(server, s, lang);
    if (s.state == ListState::Ready) {
        const bool pl = (lang == Lang::PL);
        out.reserve(s.shares.size());
        for (const auto& item : s.shares) {
            FileEntry e;
            e.name = item.name;
            e.displayName = item.name;
            e.fullPath = L"\\\\" + server + L"\\" + item.name;
            e.normKey = PathUtils::NormalizePathKey(e.fullPath);
            e.isDirectory = true;
            e.typeName = pl ? L"Udział sieciowy" : L"Network share";
            if (!item.comment.empty()) e.typeName += L" - " + item.comment;
            out.push_back(std::move(e));
        }
    }
    return s.state;
}

ListState PeekState(const std::wstring& serverRoot, Lang lang, std::wstring* outMessage) {
    std::wstring server, share;
    if (!SplitUnc(serverRoot, server, share)) return ListState::Failed;
    const std::shared_ptr<State> st = TheState();
    std::lock_guard<std::mutex> lk(st->mutex);
    auto it = st->servers.find(PathUtils::ToLower(server));
    if (it == st->servers.end() || !it->second.asked) return ListState::Loading;
    if (outMessage) *outMessage = ListMessage(server, it->second, lang);
    return it->second.state;
}

void Invalidate(const std::wstring& path) {
    const std::shared_ptr<State> st = TheState();
    std::lock_guard<std::mutex> lk(st->mutex);
    std::wstring server, share;
    if (SplitUnc(path, server, share)) {
        const std::wstring key = PathUtils::ToLower(server);
        auto it = st->servers.find(key);
        if (it != st->servers.end()) {
            it->second.asked = false;
            ++it->second.generation;
        }
        const std::wstring prefix = L"\\\\" + key + L"\\";
        for (auto& [probeKey, probe] : st->probes) {
            if (probeKey.rfind(prefix, 0) == 0 && probe.state != ProbeState::Running) probe.state = ProbeState::None;
        }
        return;
    }
    const std::wstring target = ProbeTarget(path);
    if (target.empty()) return;
    auto it = st->probes.find(PathUtils::ToLower(target));
    if (it != st->probes.end() && it->second.state != ProbeState::Running) it->second.state = ProbeState::None;
}

Reach CheckReachable(const std::wstring& path, DWORD waitMs, Lang lang, std::wstring* outMessage) {
    if (!IsNetworkPath(path)) return Reach::Ok;
    const std::wstring target = ProbeTarget(path);
    if (target.empty()) return Reach::Ok;                  // \\server alone: its list of shares is read in the background
    const std::wstring key = PathUtils::ToLower(target);
    const std::shared_ptr<State> st = TheState();
    std::unique_lock<std::mutex> lk(st->mutex);
    if (IsUncPath(path)) {
        const std::wstring server = ServerName(path);
        st->names.emplace(PathUtils::ToLower(server), server);
    }
    Probe& p = st->probes[key];                           // std::map: the reference stays valid while others are added
    const ULONGLONG now = GetTickCount64();
    const bool fresh = (p.state == ProbeState::Answered && now - p.tick < kAnswerFreshMs) ||
                       (p.state == ProbeState::NoAnswer && now - p.tick < kNoAnswerFreshMs);
    if (!fresh && p.state != ProbeState::Running) {
        p.state = ProbeState::Running;
        StartProbe(st, key, target);
    }
    if (p.state == ProbeState::Running && waitMs > 0) {
        st->cv.wait_for(lk, std::chrono::milliseconds(waitMs), [&p] { return p.state != ProbeState::Running; });
    }
    if (p.state == ProbeState::Running) return Reach::Pending;
    if (p.state == ProbeState::NoAnswer) {
        if (outMessage) *outMessage = NoAnswerMessage(target, p, lang);
        return Reach::Failed;
    }
    return Reach::Ok;
}

} // namespace Explorer2::NetShares
