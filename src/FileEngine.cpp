#include "FileEngine.h"
#include "ThreadUtils.h"
#include <chrono>
#include "AppLog.h"
#include "ArchiveExplorer.h"
#include "FtpClient.h"
#include "NetShares.h"
#include "RepoBrowser.h"
#include "PathUtils.h"
#include "TransferManager.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sddl.h>
#include <algorithm>
#include <cwctype>
#ifndef _WIN32
#include "ToolProcess.h"
#include "LinuxStorage.h"
#include "LinuxTrash.h"
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#endif

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace Explorer2 {

static std::wstring GetCurrentUserSidString() {
    static std::wstring s_cachedSid;
    static std::once_flag s_once;
    std::call_once(s_once, []() {
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            BYTE tokenBuf[256] = {};
            DWORD len = 0;
            if (GetTokenInformation(token, TokenUser, tokenBuf, sizeof(tokenBuf), &len)) {
                LPWSTR sidText = nullptr;
                if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenBuf)->User.Sid, &sidText) && sidText) {
                    s_cachedSid = sidText;
                    LocalFree(sidText);
                }
            }
            CloseHandle(token);
        }
    });
    return s_cachedSid;
}

#ifndef _WIN32
namespace {

struct LinuxMountInfo {
    std::wstring mountPoint;
    std::wstring fsType;
    std::wstring device;
    std::wstring label;
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
    bool isNetwork = false;
    bool isOptical = false;
    bool probeAsync = false;    // the size comes from FileEngine::ProbeLinuxMountAsync, not from here
};

std::string UnescapeProcMountField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() &&
            s[i + 1] >= '0' && s[i + 1] <= '7' &&
            s[i + 2] >= '0' && s[i + 2] <= '7' &&
            s[i + 3] >= '0' && s[i + 3] <= '7') {
            char ch = static_cast<char>(((s[i + 1] - '0') << 6) | ((s[i + 2] - '0') << 3) | (s[i + 3] - '0'));
            out.push_back(ch);
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::vector<LinuxMountInfo> EnumerateLinuxMounts(Lang lang) {
    std::vector<LinuxMountInfo> out;
    std::unordered_set<std::string> seenMounts;
    std::ifstream f("/proc/self/mounts");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string devRaw, mntRaw, fsType;
        if (!(iss >> devRaw >> mntRaw >> fsType)) continue;
        std::string mnt = UnescapeProcMountField(mntRaw);
        std::string dev = UnescapeProcMountField(devRaw);
        if (mnt.empty() || mnt[0] != '/') continue;
        if (mnt != "/") {
            if (mnt.rfind("/proc", 0) == 0 || mnt.rfind("/sys", 0) == 0 || mnt.rfind("/dev", 0) == 0) continue;
            if (mnt.rfind("/run", 0) == 0 && mnt.rfind("/run/media/", 0) != 0 && mnt.rfind("/run/user/", 0) == 0) continue;
            if (mnt.rfind("/snap/", 0) == 0 || mnt.rfind("/var/snap/", 0) == 0) continue;
            if (fsType == "proc" || fsType == "sysfs" || fsType == "devtmpfs" || fsType == "devpts" ||
                fsType == "tmpfs" || fsType == "cgroup" || fsType == "cgroup2" || fsType == "pstore" ||
                fsType == "efivarfs" || fsType == "bpf" || fsType == "autofs" || fsType == "mqueue" ||
                fsType == "hugetlbfs" || fsType == "debugfs" || fsType == "tracefs" || fsType == "securityfs" ||
                fsType == "configfs" || fsType == "fusectl" || fsType == "binfmt_misc" || fsType == "nsfs" ||
                fsType == "ramfs" || fsType == "rpc_pipefs" || fsType == "overlay" || fsType == "squashfs" ||
                fsType == "fuse.portal" || fsType == "fuse.gvfsd-fuse") {
                continue;
            }
            if (mnt.rfind("/run/", 0) == 0 && mnt.rfind("/run/media/", 0) != 0) continue;
        }
        if (!seenMounts.insert(mnt).second) continue;

        LinuxMountInfo mi;
        mi.mountPoint = PathUtils::Utf8ToWide(mnt);
        mi.fsType = PathUtils::Utf8ToWide(fsType);
        mi.device = PathUtils::Utf8ToWide(dev);
        mi.isNetwork = LinuxMounts::IsNetworkFsType(fsType);
        mi.isOptical = (fsType == "iso9660" || fsType == "udf");

        // statvfs() waits for the server of a network mount, and for the program behind a FUSE mount, for
        // as long as they take, up to forever. This runs on the window's thread (Ten komputer, the folder
        // tree), so such mounts are asked on a probe thread instead and show no size until it answers.
        mi.probeAsync = mi.isNetwork || fsType == "fuse" || fsType == "fuseblk" || fsType.rfind("fuse.", 0) == 0;
        if (!mi.probeAsync) {
            struct statvfs vfs = {};
            if (::statvfs(mnt.c_str(), &vfs) == 0) {
                const uint64_t bsz = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
                mi.totalBytes = static_cast<uint64_t>(vfs.f_blocks) * bsz;
                mi.freeBytes  = static_cast<uint64_t>(vfs.f_bavail) * bsz;
            }
        }

        if (mnt == "/") {
            mi.label = std::wstring(lang == Lang::PL ? L"System Linux (/)" : L"Linux Root (/)") + L" [" + mi.fsType + L"]";
        } else {
            std::wstring leaf = PathUtils::GetFileName(mi.mountPoint);
            if (leaf.empty()) leaf = mi.mountPoint;
            mi.label = leaf + L" (" + mi.mountPoint + L")";
        }
        out.push_back(std::move(mi));
    }

    if (out.empty()) {
        LinuxMountInfo root;
        root.mountPoint = L"/";
        root.fsType = L"ext4";
        root.label = (lang == Lang::PL) ? L"System Linux (/)" : L"Linux Root (/)";
        struct statvfs vfs = {};
        if (::statvfs("/", &vfs) == 0) {
            const uint64_t bsz = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
            root.totalBytes = static_cast<uint64_t>(vfs.f_blocks) * bsz;
            root.freeBytes  = static_cast<uint64_t>(vfs.f_bavail) * bsz;
        }
        out.push_back(std::move(root));
    }
    return out;
}

std::string UrlDecodeTrashPath(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hexVal(s[i + 1]);
            const int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

// A .trashinfo. A relative Path= is relative to relativeBase: the drive's top folder for a drive's own trash (GNOME
// writes it so), the folder holding the home trash for that one (LinuxTrash::RelativePathBase)
bool ParseFreeDesktopTrashInfo(const std::vector<uint8_t>& raw, RecycleBinRecord& outRecord, const std::string& relativeBase) {
    std::string txt(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (txt.find("[Trash Info]") == std::string::npos) return false;
    std::istringstream iss(txt);
    std::string line, origUtf8, delDate;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.rfind("Path=", 0) == 0) origUtf8 = UrlDecodeTrashPath(std::string_view(line).substr(5));
        else if (line.rfind("DeletionDate=", 0) == 0) delDate = line.substr(13);
    }
    if (origUtf8.empty()) return false;
    if (origUtf8[0] != '/') {
        origUtf8 = (relativeBase == "/" ? std::string() : relativeBase) + "/" + origUtf8;
    }

    FILETIME ft = {};
    int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
    if (std::sscanf(delDate.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) == 6) {
        struct tm lt = {};
        lt.tm_year = Y - 1900;
        lt.tm_mon = M - 1;
        lt.tm_mday = D;
        lt.tm_hour = h;
        lt.tm_min = m;
        lt.tm_sec = sec;
        lt.tm_isdst = -1;
        time_t t = ::mktime(&lt);
        if (t > 0) ft = Explorer2::Compat::FileTimeFromUnix(t);
    }
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) {
        GetSystemTimeAsFileTime(&ft);
    }

    outRecord.version = 3; // FreeDesktop .trashinfo
    outRecord.fileSize = 0;
    outRecord.deletedTime = ft;
    outRecord.originalPath = PathUtils::TrimTrailingSlashes(PathUtils::Utf8ToWide(origUtf8));
    return !outRecord.originalPath.empty();
}

// "used / total" as the folder tree shows it next to a mount
std::wstring LinuxDriveBadge(uint64_t usedBytes, uint64_t totalBytes) {
    if (totalBytes == 0) return L"";
    wchar_t buf[64] = {};
    if (totalBytes >= (1ULL << 40)) {
        swprintf_s(buf, L"%.1f / %.1f TB",
            static_cast<double>(usedBytes) / static_cast<double>(1ULL << 40),
            static_cast<double>(totalBytes) / static_cast<double>(1ULL << 40));
    } else if (totalBytes >= (10ULL << 30)) {
        swprintf_s(buf, L"%llu / %llu GB",
            static_cast<unsigned long long>((usedBytes + (1ULL << 29)) >> 30),
            static_cast<unsigned long long>((totalBytes + (1ULL << 29)) >> 30));
    } else if (totalBytes >= (1ULL << 30)) {
        swprintf_s(buf, L"%.1f / %.1f GB",
            static_cast<double>(usedBytes) / static_cast<double>(1ULL << 30),
            static_cast<double>(totalBytes) / static_cast<double>(1ULL << 30));
    } else {
        swprintf_s(buf, L"%llu / %llu MB",
            static_cast<unsigned long long>((usedBytes + (1ULL << 19)) >> 20),
            static_cast<unsigned long long>((totalBytes + (1ULL << 19)) >> 20));
    }
    return buf;
}

// "/a//b/" -> "/a/b", as the mount table spells paths ("/" stays "/")
std::string CollapseSlashes(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    for (const char ch : path) {
        if (ch == '/' && !out.empty() && out.back() == '/') continue;
        out.push_back(ch);
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

std::string RealPathOr(const std::string& path) {
    char* real = ::realpath(path.c_str(), nullptr);
    if (!real) return path;
    std::string out = CollapseSlashes(real);
    std::free(real);
    return out;
}

// One line of /proc/self/mountinfo per mount:
// "36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw,errors=continue"
std::vector<LinuxMounts::Mount> ReadMountInfo() {
    std::vector<LinuxMounts::Mount> out;
    std::ifstream f("/proc/self/mountinfo");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string mountId, parentId, devNumbers, root, mountPoint;
        if (!(iss >> mountId >> parentId >> devNumbers >> root >> mountPoint)) continue;
        std::string token, fsType, source;
        while (iss >> token) {
            if (token == "-") {
                iss >> fsType >> source;
                break;
            }
        }
        unsigned devMajor = 0, devMinor = 0;
        if (std::sscanf(devNumbers.c_str(), "%u:%u", &devMajor, &devMinor) != 2) continue;
        LinuxMounts::Mount m;
        m.mountPoint = UnescapeProcMountField(mountPoint);
        m.fsType = fsType;
        m.source = UnescapeProcMountField(source);
        m.device = static_cast<uint64_t>(makedev(devMajor, devMinor));
        out.push_back(std::move(m));
    }
    return out;
}

// A small file of /sys or /run/udev as text (empty when it cannot be read)
std::string ReadSmallTextFile(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string out;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > 256 * 1024) break;
    }
    ::close(fd);
    return out;
}

// A file system on a pluggable device that is not mounted (LinuxStorage::IsMountableUnmountedVolume says which count)
struct LinuxUnmountedVolume {
    std::wstring devNode;       // "/dev/sdb1"
    std::wstring label;         // the file system's label, or the device's model, or the device name
    std::wstring fsType;
    uint64_t sizeBytes = 0;
};

// Looks at the disks in /sys/block that can be plugged in (removable, on USB, or an SD card), at their partitions,
// and at what udev found on each (/run/udev/data, readable by everyone). No device is opened and nothing is run,
// so this is cheap enough for the drive list and for the periodic look at the drives.
std::vector<LinuxUnmountedVolume> EnumerateLinuxUnmountedVolumes() {
    std::vector<LinuxUnmountedVolume> out;
    std::vector<std::string> disks;
    if (DIR* d = ::opendir("/sys/block")) {
        while (struct dirent* e = ::readdir(d)) {
            const std::string nm = e->d_name;
            if (nm.empty() || nm[0] == '.') continue;
            if (nm.rfind("loop", 0) == 0 || nm.rfind("ram", 0) == 0 || nm.rfind("zram", 0) == 0 || nm.rfind("dm-", 0) == 0 ||
                nm.rfind("sr", 0) == 0 || nm.rfind("fd", 0) == 0 || nm.rfind("nbd", 0) == 0 || nm.rfind("md", 0) == 0) {
                continue;
            }
            disks.push_back(nm);
        }
        ::closedir(d);
    }
    if (disks.empty()) return out;
    std::sort(disks.begin(), disks.end());

    const std::vector<LinuxMounts::Mount> mounts = ReadMountInfo();    // as it is now, not the copy kept for a second
    for (const std::string& disk : disks) {
        const std::string base = "/sys/block/" + disk;
        LinuxStorage::BlockVolumeFacts diskFacts;
        diskFacts.diskRemovable = (LinuxStorage::Trim(ReadSmallTextFile(base + "/removable")) == "1");
        char realBuf[PATH_MAX] = {};
        const std::string realPath = ::realpath(base.c_str(), realBuf) ? std::string(realBuf) : std::string();
        diskFacts.diskOnUsb = (realPath.find("/usb") != std::string::npos);
        diskFacts.diskIsSdCard = (disk.rfind("mmcblk", 0) == 0 && LinuxStorage::Trim(ReadSmallTextFile(base + "/device/type")) == "SD");
        if (!(diskFacts.diskRemovable || diskFacts.diskOnUsb || diskFacts.diskIsSdCard)) continue;

        // The disk's partitions are folders inside its own, each with a file "partition"; a disk without a
        // partition table (many small sticks) carries its file system itself
        std::vector<std::string> volumes;
        if (DIR* pd = ::opendir(base.c_str())) {
            while (struct dirent* e = ::readdir(pd)) {
                const std::string nm = e->d_name;
                if (nm.size() <= disk.size() || nm.compare(0, disk.size(), disk) != 0) continue;
                if (::access((base + "/" + nm + "/partition").c_str(), F_OK) == 0) volumes.push_back(nm);
            }
            ::closedir(pd);
        }
        std::sort(volumes.begin(), volumes.end());
        const bool wholeDisk = volumes.empty();
        if (wholeDisk) volumes.push_back(disk);

        for (const std::string& vol : volumes) {
            const std::string vdir = wholeDisk ? base : (base + "/" + vol);
            LinuxStorage::BlockVolumeFacts v = diskFacts;
            v.name = vol;
            v.sizeBytes = std::strtoull(LinuxStorage::Trim(ReadSmallTextFile(vdir + "/size")).c_str(), nullptr, 10) * 512ULL;
            const std::string devNumbers = LinuxStorage::Trim(ReadSmallTextFile(vdir + "/dev"));      // "8:17"
            unsigned devMajor = 0, devMinor = 0;
            if (std::sscanf(devNumbers.c_str(), "%u:%u", &devMajor, &devMinor) != 2) continue;
            const std::string udevText = ReadSmallTextFile("/run/udev/data/b" + devNumbers);
            if (!udevText.empty()) {
                v.udevKnown = true;
                v.udev = LinuxStorage::ParseUdevDb(udevText);
            }
            const uint64_t devNo = static_cast<uint64_t>(makedev(devMajor, devMinor));
            const std::string devNode = "/dev/" + vol;
            for (const auto& m : mounts) {
                if (m.device == devNo || m.source == devNode) {
                    v.mounted = true;
                    break;
                }
            }
            if (!LinuxStorage::IsMountableUnmountedVolume(v)) continue;

            LinuxUnmountedVolume u;
            u.devNode = PathUtils::Utf8ToWide(devNode);
            u.fsType = PathUtils::Utf8ToWide(v.Udev("ID_FS_TYPE"));
            u.sizeBytes = v.sizeBytes;
            std::string label = LinuxStorage::VolumeLabelOf(v);
            if (label.empty()) {
                label = v.Udev("ID_MODEL");
                std::replace(label.begin(), label.end(), '_', ' ');
                label = LinuxStorage::Trim(label);
            }
            if (label.empty()) label = vol;
            u.label = PathUtils::Utf8ToWide(label);
            out.push_back(std::move(u));
        }
    }
    return out;
}

// "KINGSTON (sdb1, 14.9 GB) - niezamontowany": the name under which the drive list shows such a volume
std::wstring UnmountedVolumeTitle(const LinuxUnmountedVolume& uv, Lang lang) {
    std::wstring shortDev = uv.devNode;
    if (shortDev.rfind(L"/dev/", 0) == 0) shortDev.erase(0, 5);
    std::wstring title = uv.label;
    if (uv.label != shortDev) title += L" (" + shortDev + (uv.sizeBytes > 0 ? L", " + FileEngine::FormatSizeDetailed(uv.sizeBytes) : L"") + L")";
    else if (uv.sizeBytes > 0) title += L" (" + FileEngine::FormatSizeDetailed(uv.sizeBytes) + L")";
    return title + (lang == Lang::PL ? L" - niezamontowany" : L" - not mounted");
}

} // namespace

namespace LinuxMounts {

std::shared_ptr<const std::vector<Mount>> Current() {
    struct Cache {
        std::mutex mutex;
        std::shared_ptr<const std::vector<Mount>> table;
        ULONGLONG tick = 0;
    };
    // Never destroyed: walker and probe threads may still ask while the process ends
    static Cache* const s_cache = new Cache();
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lk(s_cache->mutex);
    if (!s_cache->table || now - s_cache->tick >= 1000) {
        s_cache->table = std::make_shared<std::vector<Mount>>(ReadMountInfo());
        s_cache->tick = now;
    }
    return s_cache->table;
}

bool IsNetworkFsType(const std::string& fsType) {
    static const char* const kNetworkTypes[] = {
        "nfs", "nfs4", "cifs", "smb3", "smbfs", "ncpfs", "afs", "9p", "ceph", "glusterfs", "lustre",
        "davfs", "sshfs", "vboxsf", "vmhgfs", "virtiofs", "fuse"
    };
    for (const char* t : kNetworkTypes) {
        if (fsType == t) return true;
    }
    return fsType.rfind("fuse.", 0) == 0;   // sshfs, rclone, gvfs, s3fs, ... (but not "fuseblk": a local disk)
}

const Mount* FindByDevice(const std::vector<Mount>& mounts, uint64_t device) {
    const Mount* found = nullptr;
    for (const auto& m : mounts) {
        if (m.device == device) found = &m;
    }
    return found;
}

} // namespace LinuxMounts

MountFence::MountFence(const std::wstring& rootDir) {
    m_rootNative = CollapseSlashes(Explorer2::Compat::NativePath(rootDir.c_str()));
    if (m_rootNative.empty()) return;
    struct stat lst = {};
    m_rootIsFolder = ::lstat(m_rootNative.c_str(), &lst) == 0 && S_ISDIR(lst.st_mode);
    // The walk lists what the root names (a link to a folder is followed there), so its device is the target's
    struct stat st = {};
    if (::stat(m_rootNative.c_str(), &st) == 0) {
        m_rootDevice = static_cast<uint64_t>(st.st_dev);
        m_rootKnown = true;
    }
    m_rootReal = RealPathOr(m_rootNative);
    const std::string prefix = (m_rootReal == "/") ? std::string("/") : (m_rootReal + "/");
    const auto mounts = LinuxMounts::Current();
    for (const auto& m : *mounts) {
        if (m.mountPoint.size() > prefix.size() && m.mountPoint.compare(0, prefix.size(), prefix) == 0) {
            m_mountsBelow.insert(m.mountPoint);
        }
    }
}

bool MountFence::IsMountRoot(const std::wstring& childPath, bool* outCannotTell) const {
    if (outCannotTell) *outCannotTell = false;
    const std::string native = CollapseSlashes(Explorer2::Compat::NativePath(childPath.c_str()));
    struct stat st = {};
    if (!m_rootKnown || ::lstat(native.c_str(), &st) != 0) {
        if (outCannotTell) *outCannotTell = true;
        return true;
    }
    if (static_cast<uint64_t>(st.st_dev) != m_rootDevice) return true;   // another file system
    if (m_mountsBelow.empty()) return false;
    // A bind mount of a folder of the same file system has the same device: the mount table tells. The walk
    // never follows a link, so the real path of a folder in it is the root's real path plus the rest.
    std::string canonical = native;
    const size_t n = m_rootNative.size();
    if (m_rootNative != "/" && native.size() > n && native.compare(0, n, m_rootNative) == 0 && native[n] == '/') {
        canonical = m_rootReal + native.substr(n);
    }
    return m_mountsBelow.count(canonical) != 0;
}

bool MountFence::IsMountPoint(const std::wstring& path) {
    const std::string native = CollapseSlashes(Explorer2::Compat::NativePath(path.c_str()));
    if (native.empty() || native[0] != '/') return false;
    if (native == "/") return true;
    const size_t slash = native.find_last_of('/');
    const std::string parent = (slash == 0) ? std::string("/") : native.substr(0, slash);
    // The mount table first: it answers without touching the mounted file system, whose server may not answer
    // (a stat of a switched-off NAS's mount point waits for minutes, and the window with it). The folder that
    // holds the mount point is resolved, not the mount point itself.
    const std::string realParent = RealPathOr(parent);
    const std::string leaf = native.substr(slash + 1);
    const std::string canonical = (realParent == "/") ? ("/" + leaf) : (realParent + "/" + leaf);
    {
        const auto mounts = LinuxMounts::Current();
        for (const auto& m : *mounts) {
            if (m.mountPoint == canonical || m.mountPoint == native) return true;
        }
    }
    struct stat st = {};
    if (::lstat(native.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) return false;
    struct stat pst = {};
    if (::lstat(parent.c_str(), &pst) == 0 && pst.st_dev != st.st_dev) return true;   // another file system
    return false;
}
#endif

// Reads HKCU\Network\<Letter>\RemotePath in <0.005ms without touching the network!
// Formats "\\NAS191725\Backup" on 'X' as "Backup (\\NAS191725) (X:)" matching Windows 10 Explorer.
static std::wstring GetMappedNetworkDriveLabel(wchar_t driveLetter, Lang lang) {
    wchar_t subKey[64] = {};
    swprintf_s(subKey, L"Network\\%c", static_cast<wchar_t>(towupper(driveLetter)));
    std::wstring dl = std::wstring(1, static_cast<wchar_t>(towupper(driveLetter))) + L":";

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t remoteBuf[MAX_PATH] = {};
        DWORD cb = sizeof(remoteBuf);
        if (RegQueryValueExW(hKey, L"RemotePath", nullptr, nullptr, reinterpret_cast<LPBYTE>(remoteBuf), &cb) == ERROR_SUCCESS && remoteBuf[0]) {
            RegCloseKey(hKey);
            std::wstring rem = PathUtils::TrimTrailingSlashes(remoteBuf);
            // Parse "\\Server\Share" -> "Share (\\Server) (X:)"
            if (rem.rfind(L"\\\\", 0) == 0) {
                size_t slash = rem.find(L'\\', 2);
                if (slash != std::wstring::npos && slash + 1 < rem.size()) {
                    std::wstring server = rem.substr(0, slash);
                    std::wstring share = rem.substr(slash + 1);
                    return share + L" (" + server + L") (" + dl + L")";
                }
            }
            return rem + L" (" + dl + L")";
        }
        RegCloseKey(hKey);
    }
    return (lang == Lang::PL ? L"Dysk sieciowy (" : L"Network Drive (") + dl + L")";
}

void FileEngine::ProbeNetworkDriveAsync(wchar_t driveLetter) {
    wchar_t dl = static_cast<wchar_t>(towupper(driveLetter));
    {
        std::lock_guard<std::mutex> lk(m_net->mutex);
        NetDriveInfo& info = m_net->drives[dl];
        // Asked again after a while: a NAS switched off (or back on) later is shown as it is now
        if (info.running || (info.probed && GetTickCount64() - info.tick < 30000)) {
            return;
        }
        info.running = true;
    }

    // The thread works on the shared block only, never on the engine itself (see NetDriveState)
    HWND notifyHwnd = m_notifyHwnd;
    std::thread([state = m_net, dl, notifyHwnd]() {
        wchar_t rootPath[] = { dl, L':', L'\\', L'\0' };
        ULARGE_INTEGER freeBytes = {}, totalBytes = {};
        BOOL ok = GetDiskFreeSpaceExW(rootPath, &freeBytes, &totalBytes, nullptr);
        bool changed = false;
        {
            std::lock_guard<std::mutex> lk(state->mutex);
            NetDriveInfo& info = state->drives[dl];
            changed = !info.probed || info.online != (ok != FALSE);
            info.probed = true;
            info.online = (ok != FALSE);
            info.totalBytes = ok ? totalBytes.QuadPart : 0ULL;
            info.freeBytes = ok ? freeBytes.QuadPart : 0ULL;
            info.running = false;
            info.tick = GetTickCount64();
        }
        if ((ok || changed) && notifyHwnd && state->wanted) {
            PostMessageW(notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
        }
    }).detach();
}

#ifndef _WIN32
void FileEngine::ProbeLinuxMountAsync(const std::wstring& mountPoint) {
    {
        std::lock_guard<std::mutex> lk(m_net->mutex);
        NetDriveInfo& info = m_net->mounts[mountPoint];
        // One probe at a time per mount, so a server that never answers holds one thread and not one per
        // visit; asked again after a while, so a share that comes back (or goes away) is shown as it is now
        if (info.running || (info.probed && GetTickCount64() - info.tick < 30000)) {
            return;
        }
        info.running = true;
    }

    // The thread works on the shared block only, never on the engine itself (see NetDriveState)
    HWND notifyHwnd = m_notifyHwnd;
    const std::string native = Explorer2::Compat::NativePath(mountPoint.c_str());
    try {
        std::thread([state = m_net, mountPoint, native, notifyHwnd]() {
            struct statvfs vfs = {};
            const bool ok = (::statvfs(native.c_str(), &vfs) == 0);
            uint64_t totalBytes = 0, freeBytes = 0;
            if (ok) {
                const uint64_t bsz = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
                totalBytes = static_cast<uint64_t>(vfs.f_blocks) * bsz;
                freeBytes = static_cast<uint64_t>(vfs.f_bavail) * bsz;
            }
            bool changed = false;
            {
                std::lock_guard<std::mutex> lk(state->mutex);
                NetDriveInfo& info = state->mounts[mountPoint];
                changed = !info.probed || info.online != ok || info.totalBytes != totalBytes || info.freeBytes != freeBytes;
                info.probed = true;
                info.online = ok;
                info.totalBytes = totalBytes;
                info.freeBytes = freeBytes;
                info.running = false;
                info.tick = GetTickCount64();
            }
            if (changed && notifyHwnd && state->wanted) {
                PostMessageW(notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
            }
        }).detach();
    } catch (...) {
        // No thread could be started: the next visit tries again
        std::lock_guard<std::mutex> lk(m_net->mutex);
        m_net->mounts[mountPoint].running = false;
    }
}

bool FileEngine::GetProbedMountSize(const std::wstring& mountPoint, uint64_t& outTotal, uint64_t& outFree, bool& outOnline) const {
    std::lock_guard<std::mutex> lk(m_net->mutex);
    auto it = m_net->mounts.find(mountPoint);
    if (it == m_net->mounts.end() || !it->second.probed) return false;
    outOnline = it->second.online;
    outTotal = it->second.online ? it->second.totalBytes : 0;
    outFree = it->second.online ? it->second.freeBytes : 0;
    return true;
}
#endif

std::wstring FileEngine::MappedNetworkDriveLabel(wchar_t driveLetter, Lang lang) {
    return GetMappedNetworkDriveLabel(driveLetter, lang);
}

bool FileEngine::IsDriveOffline(const std::wstring& path, Lang lang, std::wstring* outDisplayName) {
    if (path.size() < 2 || path[1] != L':') return false;
    wchar_t dl = static_cast<wchar_t>(towupper(path[0]));
    if (dl < L'A' || dl > L'Z') return false;

    static UINT s_cachedDriveTypes[26] = {};
    int idx = dl - L'A';
    UINT dtype = s_cachedDriveTypes[idx];
    wchar_t rootPath[] = { dl, L':', L'\\', L'\0' };
    if (dtype == 0) {
        dtype = GetDriveTypeW(rootPath);
        if (dtype == DRIVE_FIXED || dtype == DRIVE_REMOTE || dtype == DRIVE_CDROM) {
            s_cachedDriveTypes[idx] = dtype;
        }
    }
    if (dtype == DRIVE_FIXED) {
        return false; // Fixed internal SSDs/HDDs are online; skip synchronous volume query
    }
    if (dtype == DRIVE_REMOTE) {
        if (outDisplayName) *outDisplayName = GetMappedNetworkDriveLabel(dl, lang);
        ProbeNetworkDriveAsync(dl);
        std::lock_guard<std::mutex> lk(m_net->mutex);
        auto it = m_net->drives.find(dl);
        return (it == m_net->drives.end()) || !it->second.online;
    }
    if (dtype == DRIVE_CDROM) {
        wchar_t volName[MAX_PATH] = {};
        if (!GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0)) {
            if (outDisplayName) {
                *outDisplayName = (lang == Lang::PL ? L"Stacja dysków BD-ROM (" : L"BD-ROM Drive (") + std::wstring(1, dl) + L":)";
            }
            return true;
        }
    }
    return false;
}

namespace {

struct GlobalExtIconCache {
    std::mutex mtx;
    std::unordered_map<std::wstring, int> map;

    // The icon, if it was looked up before. Never blocks for long.
    bool TryGet(const std::wstring& extLower, int& outIndex) {
        const std::wstring key = (extLower.empty() || extLower == L".") ? L"__NO_EXT__" : extLower;
        std::lock_guard<std::mutex> lk(mtx);
        auto it = map.find(key);
        if (it == map.end()) return false;
        outIndex = it->second;
        return true;
    }

    // The icon; looks it up in the registry the first time (several milliseconds)
    int Resolve(const std::wstring& extLower) {
        std::wstring key = (extLower.empty() || extLower == L".") ? L"__NO_EXT__" : extLower;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = map.find(key);
            if (it != map.end()) return it->second;
        }

        int resolved = 0;
        if (extLower == L".exe" || extLower == L".com" || extLower == L".scr") {
            SHSTOCKICONINFO ssii = {};
            ssii.cbSize = sizeof(ssii);
            if (SUCCEEDED(SHGetStockIconInfo(SIID_APPLICATION, SHGSI_SYSICONINDEX, &ssii)) && ssii.iSysImageIndex > 0) {
                resolved = ssii.iSysImageIndex;
            } else {
                SHFILEINFOW sfi = {};
                if (SHGetFileInfoW(L"app.exe", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)) {
                    resolved = sfi.iIcon;
                }
            }
        } else {
            std::wstring dummyName = (extLower.empty() || extLower == L".") ? L"file" : (L"file" + extLower);
            SHFILEINFOW sfi = {};
            if (SHGetFileInfoW(dummyName.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)) {
                resolved = sfi.iIcon;
            }
        }

        std::lock_guard<std::mutex> lk(mtx);
        map[key] = resolved;
        return resolved;
    }

};

// Types looked up as soon as the icon worker starts, before any folder asks for them
const wchar_t* const kCommonIconExts[] = {
    L".exe", L".dll", L".lnk", L".msi", L".txt", L".log", L".cpp", L".h", L".rc", L".json", L".xml", L".pdf",
    L".zip", L".jpg", L".png", L".gif", L".mp4", L".mp3", L".docx", L".xlsx", L".html", L".url", L".bat"
};

GlobalExtIconCache& GetGlobalExtIconCache() {
    static GlobalExtIconCache s_cache;
    return s_cache;
}

} // namespace

FileEngine::FileEngine() = default;

FileEngine::~FileEngine() {
    Shutdown();
}

void FileEngine::Initialize(HWND notifyHwnd) {
    m_notifyHwnd = notifyHwnd;
    m_running = true;
    m_net->wanted = true;
    m_folderIconIndex = 3;
    m_watcherWakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    // The generic icon is needed at once (it stands in for types still being looked up); the common types
    // are looked up by the worker
    m_genericIconIndex = GetGlobalExtIconCache().Resolve(L"");
    for (const wchar_t* ext : kCommonIconExts) {
        int known = 0;
        if (!GetGlobalExtIconCache().TryGet(ext, known)) QueueExtIconLookup(ext);
    }
    m_extIconThread = std::thread(&FileEngine::ExtIconWorkerLoop, this);

    for (int i = 0; i < 4; ++i) {
        m_sizeThreads.emplace_back(&FileEngine::FolderSizeWorkerLoop, this);
    }
    m_searchThread = std::thread(&FileEngine::SearchWorkerLoop, this);
    m_watcherThread = std::thread(&FileEngine::DirWatcherWorkerLoop, this);
}

void FileEngine::QueueExtIconLookup(const std::wstring& extLower) {
    {
        std::lock_guard<std::mutex> lk(m_extIconMutex);
        if (!m_extIconQueued.insert(extLower).second) return;      // already waiting
        m_extIconQueue.push_back(extLower);
    }
    m_extIconCv.notify_all();
}

void FileEngine::ExtIconWorkerLoop() {
    // The shell wants COM on a thread that asks it for icons
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int sinceNotify = 0;
    for (;;) {
        std::wstring ext;
        {
            std::unique_lock<std::mutex> lk(m_extIconMutex);
            m_extIconCv.wait(lk, [this]() { return !m_running || !m_extIconQueue.empty(); });
            if (!m_running) break;
            ext = std::move(m_extIconQueue.front());
            m_extIconQueue.pop_front();
            m_extIconBusy = true;
        }

        const int icon = GetGlobalExtIconCache().Resolve(ext);
        {
            // Listings made from now on get the real icon
            std::lock_guard<std::mutex> lk(m_extMutex);
            const std::wstring base = ext.empty() ? std::wstring(L"__NO_EXT__") : ext;
            for (const wchar_t* suffix : { L"_PL", L"_EN" }) {
                auto it = m_extCache.find(base + suffix);
                if (it != m_extCache.end()) {
                    it->second.sysIconIndex = icon;
                    it->second.iconPending = false;
                }
            }
        }

        bool queueEmpty = false;
        {
            std::lock_guard<std::mutex> lk(m_extIconMutex);
            m_extIconQueued.erase(ext);
            queueEmpty = m_extIconQueue.empty();
        }

        // One message per batch: when the queue has run empty, and every 16 types during a long batch.
        // Posted before the lookup counts as finished, so that whoever waits for the lookups
        // (WaitUntilIconsResolved) finds the message already there.
        ++sinceNotify;
        if ((queueEmpty || sinceNotify >= 16) && m_notifyHwnd && m_running && !m_extIconNotifyPending.exchange(true)) {
            if (!PostMessageW(m_notifyHwnd, WM_APP_EXT_ICONS_READY, 0, 0)) m_extIconNotifyPending.store(false);
            sinceNotify = 0;
        }
        {
            std::lock_guard<std::mutex> lk(m_extIconMutex);
            m_extIconBusy = false;
        }
        m_extIconCv.notify_all();
    }
    if (SUCCEEDED(co)) CoUninitialize();
}

bool FileEngine::RefreshIconIndices(std::vector<FileEntry>& entries) const {
    bool changed = false;
    for (FileEntry& e : entries) {
        // Only entries that still show the stand-in icon; drives, folders and FTP or archive entries with an
        // icon of their own are left alone
        if (e.isDirectory || e.isDrive || e.sysIconIndex != m_genericIconIndex) continue;
        int icon = 0;
        if (GetGlobalExtIconCache().TryGet(e.ext, icon) && icon != e.sysIconIndex) {
            e.sysIconIndex = icon;
            changed = true;
        }
    }
    return changed;
}

bool FileEngine::IsTypeIconKnown(const std::wstring& extLower) {
    int icon = 0;
    return GetGlobalExtIconCache().TryGet(extLower, icon);
}

bool FileEngine::WaitUntilIconsResolved(unsigned int timeoutMs) {
    std::unique_lock<std::mutex> lk(m_extIconMutex);
    return m_extIconCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                [this]() { return !m_running || (m_extIconQueue.empty() && !m_extIconBusy); });
}

void FileEngine::Shutdown() {
    m_net->wanted = false;      // drive probes that are still under way drop their answers
    if (m_running.exchange(false)) {
        {
            // Taken first, so that the icon worker is either not waiting yet or gets the wake-up
            std::lock_guard<std::mutex> lock(m_extIconMutex);
        }
        m_extIconCv.notify_all();
        JoinServingCom(m_extIconThread);        // it may be inside a shell call (see ThreadUtils.h)
        // Each worker checks m_running and its generation under its own mutex before it waits. The changes
        // are made under that mutex too, so a worker that has just found nothing to do either sees them or
        // is already waiting and gets the wake-up: no wake-up is lost and no join below waits forever.
        {
            std::lock_guard<std::mutex> lock(m_sizeMutex);
            m_sizeGeneration.fetch_add(1);
        }
        {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            m_searchGeneration.fetch_add(1);
        }
        {
            std::lock_guard<std::mutex> lock(m_watcherMutex);
            m_watcherGeneration.fetch_add(1);
            if (m_watcherWakeEvent) {
                SetEvent(m_watcherWakeEvent);
            }
        }
        m_sizeCv.notify_all();
        m_searchCv.notify_all();
        m_watcherCv.notify_all();
        {
            // Taking the mutex first means the sweep thread is either not waiting yet (and will see that
            // the engine is stopping) or already waiting (and gets this wake-up)
            std::lock_guard<std::mutex> lock(m_undoMutex);
        }
        m_undoSweepCv.notify_all();
        if (m_undoSweepThread.joinable()) {
            m_undoSweepThread.join();
        }
        for (auto& t : m_sizeThreads) {
            if (t.joinable()) t.join();
        }
        m_sizeThreads.clear();
        if (m_searchThread.joinable()) {
            m_searchThread.join();
        }
        if (m_watcherThread.joinable()) {
            m_watcherThread.join();
        }
        if (m_watcherWakeEvent) {
            CloseHandle(m_watcherWakeEvent);
            m_watcherWakeEvent = nullptr;
        }
        CleanupAllUndoStaging();
    }
}

void FileEngine::WatchDirectory(const std::wstring& dirPath) {
    std::wstring clean = PathUtils::TrimTrailingSlashes(dirPath);
    if (clean == L"RECYCLE_BIN") {
        clean.clear();
        wchar_t sysDir[MAX_PATH] = {};
        std::wstring sid = GetCurrentUserSidString();
        if (!sid.empty() && GetWindowsDirectoryW(sysDir, MAX_PATH) >= 3 && sysDir[1] == L':') {
            std::wstring candidate = std::wstring(sysDir, 2) + L"\\$Recycle.Bin\\" + sid;
            DWORD attrs = GetFileAttributesW(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                clean = std::move(candidate);
            }
        }
    } else if (IsVirtualFolder(clean) || FtpClient::IsFtpPath(clean) || RepoBrowser::IsRepoUrl(clean) || ArchiveExplorer::IsVirtualArchivePath(clean)) {
        clean.clear();
    } else if (!clean.empty()) {
        DWORD attrs = GetFileAttributesW(clean.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            clean.clear();
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_watcherMutex);
#ifndef _WIN32
        if (m_watchedDir == clean) {    // names that differ only in case are different folders here
            return;
        }
#else
        if (_wcsicmp(m_watchedDir.c_str(), clean.c_str()) == 0) {
            return;
        }
#endif
        m_watchedDir = clean;
        m_watcherGeneration.fetch_add(1);
        // Set while the mutex is held: the watcher resets the event under this mutex when it takes up the new
        // generation, so it can never reset this wake-up first and then miss it (which stopped auto-refresh)
        if (m_watcherWakeEvent) {
            SetEvent(m_watcherWakeEvent);
        }
    }
    m_watcherCv.notify_all();
}

void FileEngine::DirWatcherWorkerLoop() {
#ifndef _WIN32
    // Every message makes the window list the folder again and repaint, which is slow on a slow machine.
    // So a change is reported once the folder has settled: after an event the loop waits until there has
    // been kQuietMs without one (but no longer than kMaxDelayMs after the first), posts one message, and
    // never posts more often than once per kMinGapMs. IN_MODIFY is not watched: a file being written (a
    // download) changes nothing that the listing shows until it is closed (IN_CLOSE_WRITE) or renamed.
    constexpr ULONGLONG kQuietMs = 300;
    constexpr ULONGLONG kMaxDelayMs = 1500;
    constexpr ULONGLONG kMinGapMs = 1000;
    uint64_t seenGen = 0;
    ULONGLONG lastPostTick = 0;
    while (m_running) {
        std::wstring watchDir;
        {
            std::unique_lock<std::mutex> lk(m_watcherMutex);
            m_watcherCv.wait(lk, [&]() {
                return !m_running || m_watcherGeneration.load() != seenGen;
            });
            if (!m_running) break;
            seenGen = m_watcherGeneration.load();
            watchDir = m_watchedDir;
            if (m_watcherWakeEvent) ResetEvent(m_watcherWakeEvent);
        }
        if (watchDir.empty()) continue;

        const std::string nativeDir = Explorer2::Compat::NativePath(watchDir.c_str());
        const int ifd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (ifd < 0) continue;

        const int wd = ::inotify_add_watch(
            ifd, nativeDir.c_str(),
            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB |
            IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR);
        if (wd < 0) {
            ::close(ifd);
            continue;
        }

        alignas(inotify_event) char buf[8192];
        ULONGLONG firstEventTick = 0;   // 0: no change waits to be reported
        ULONGLONG lastEventTick = 0;
        bool watchEnded = false;        // the folder itself was deleted or moved away
        while (m_running && m_watcherGeneration.load() == seenGen) {
            int timeoutMs = 100;        // the generation and m_running are looked at this often
            if (firstEventTick != 0) {
                const ULONGLONG now = GetTickCount64();
                ULONGLONG due = std::min(lastEventTick + kQuietMs, firstEventTick + kMaxDelayMs);
                if (lastPostTick != 0) due = std::max(due, lastPostTick + kMinGapMs);
                if (now >= due) {
                    if (m_notifyHwnd) PostMessageW(m_notifyHwnd, WM_APP_DIR_CHANGED, 0, 0);
                    lastPostTick = now;
                    firstEventTick = 0;
                    continue;
                }
                timeoutMs = static_cast<int>(std::min<ULONGLONG>(due - now, 100));
            } else if (watchEnded) {
                break;                  // reported; nothing is left to watch until the next folder
            }

            struct pollfd pfd = { ifd, POLLIN, 0 };
            const int pr = ::poll(&pfd, 1, timeoutMs);
            if (!m_running || m_watcherGeneration.load() != seenGen) break;
            if (pr <= 0 || !(pfd.revents & POLLIN)) continue;

            bool relevantChange = false;
            for (;;) {
                const ssize_t n = ::read(ifd, buf, sizeof(buf));
                if (n <= 0) break;
                ssize_t off = 0;
                while (off + static_cast<ssize_t>(sizeof(inotify_event)) <= n) {
                    const auto* ev = reinterpret_cast<const inotify_event*>(buf + off);
                    if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) {
                        watchEnded = true;
                        relevantChange = true;
                    } else if (ev->mask & IN_Q_OVERFLOW) {
                        relevantChange = true;      // events were lost: assume something changed
                    } else {
                        const char* nm = (ev->len > 0) ? ev->name : "";
                        if (std::strncmp(nm, ".explorer2_undo", 15) != 0) {
                            relevantChange = true;
                        }
                    }
                    off += static_cast<ssize_t>(sizeof(inotify_event)) + ev->len;
                }
            }
            if (relevantChange) {
                const ULONGLONG now = GetTickCount64();
                if (firstEventTick == 0) firstEventTick = now;
                lastEventTick = now;
            }
        }
        ::inotify_rm_watch(ifd, wd);
        ::close(ifd);
    }
#else
    alignas(DWORD) BYTE notifyBuf[16384] = {};
    uint64_t seenGen = 0;

    while (m_running) {
        std::wstring watchDir;
        {
            std::unique_lock<std::mutex> lk(m_watcherMutex);
            m_watcherCv.wait(lk, [&]() {
                return !m_running || m_watcherGeneration.load() != seenGen;
            });
            if (!m_running) break;
            seenGen = m_watcherGeneration.load();
            watchDir = m_watchedDir;
            if (m_watcherWakeEvent) ResetEvent(m_watcherWakeEvent);
        }

        if (watchDir.empty()) {
            continue;
        }

        std::wstring openPath = (watchDir.size() == 2 && watchDir[1] == L':') ? (watchDir + L"\\") : watchDir;
        HANDLE hDir = CreateFileW(
            openPath.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (hDir == INVALID_HANDLE_VALUE) {
            continue;
        }

        HANDLE hIoEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!hIoEvent) {
            CloseHandle(hDir);
            continue;
        }

        while (m_running && m_watcherGeneration.load() == seenGen) {
            OVERLAPPED ov = {};
            ov.hEvent = hIoEvent;
            DWORD bytesReturned = 0;
            BOOL ok = ReadDirectoryChangesW(
                hDir,
                notifyBuf,
                sizeof(notifyBuf),
                FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                &bytesReturned,
                &ov,
                nullptr);

            if (!ok && GetLastError() != ERROR_IO_PENDING) {
                break;
            }

            HANDLE handles[2] = { m_watcherWakeEvent, hIoEvent };
            DWORD waitRes = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (waitRes == WAIT_OBJECT_0 || !m_running || m_watcherGeneration.load() != seenGen) {
                CancelIo(hDir);
                GetOverlappedResult(hDir, &ov, &bytesReturned, TRUE);
                break;
            }

            if (waitRes == WAIT_OBJECT_0 + 1) {
                DWORD transferred = 0;
                if (GetOverlappedResult(hDir, &ov, &transferred, FALSE) && transferred > 0) {
                    bool relevantChange = false;
                    const BYTE* ptr = notifyBuf;
                    for (;;) {
                        const auto* fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(ptr);
                        std::wstring changedName(fni->FileName, fni->FileNameLength / sizeof(wchar_t));
                        if (_wcsicmp(changedName.c_str(), L".explorer2_undo") != 0 &&
                            changedName.rfind(L".explorer2_undo\\", 0) != 0) {
                            relevantChange = true;
                        }
                        if (fni->NextEntryOffset == 0) break;
                        ptr += fni->NextEntryOffset;
                    }

                    if (relevantChange) {
                        // Coalesce burst writes over a 250ms window before notifying UI
                        if (WaitForSingleObject(m_watcherWakeEvent, 250) == WAIT_TIMEOUT &&
                            m_running && m_watcherGeneration.load() == seenGen && m_notifyHwnd) {
                            PostMessageW(m_notifyHwnd, WM_APP_DIR_CHANGED, 0, 0);
                        }
                    }
                }
            }
        }

        CloseHandle(hIoEvent);
        CloseHandle(hDir);
    }
#endif
}

FileEngine::ExtTypeInfo FileEngine::GetExtensionInfo(const std::wstring& extLower, bool isDir, Lang lang) {
    std::lock_guard<std::mutex> lock(m_extMutex);
    const Strings& s = GetStrings(lang);

    if (isDir) {
        std::wstring key = (lang == Lang::PL) ? L"__DIR_PL__" : L"__DIR_EN__";
        auto it = m_extCache.find(key);
        if (it != m_extCache.end()) return it->second;

        ExtTypeInfo info;
        info.typeName = s.typeFolder;
        info.sysIconIndex = m_folderIconIndex;
        return m_extCache.emplace(key, std::move(info)).first->second;
    }

    std::wstring key = (extLower.empty() ? L"__NO_EXT__" : extLower) + ((lang == Lang::PL) ? L"_PL" : L"_EN");
    auto it = m_extCache.find(key);
    if (it != m_extCache.end()) {
        return it->second;
    }

    ExtTypeInfo info;
    int knownIcon = 0;
    if (GetGlobalExtIconCache().TryGet(extLower, knownIcon)) {
        info.sysIconIndex = knownIcon;
    } else if (m_running) {
        // Not looked up yet: the generic icon for now, the real one from the worker (see RefreshIconIndices).
        // m_running is true only between Initialize, which starts the worker before any other thread that
        // lists folders, and Shutdown, which clears it before it stops the worker.
        info.sysIconIndex = m_genericIconIndex;
        info.iconPending = true;
        QueueExtIconLookup(extLower);
    } else {
        info.sysIconIndex = GetGlobalExtIconCache().Resolve(extLower);     // no worker (not initialized)
    }
    const bool pl = (lang == Lang::PL);
    if (extLower.empty() || extLower == L".") {
        info.typeName = s.typeFile;
    } else if (extLower == L".pdf") {
        info.typeName = pl ? L"Dokument PDF" : L"PDF Document";
    } else if (extLower == L".zip" || extLower == L".rar" || extLower == L".7z" || extLower == L".tar" || extLower == L".gz" || extLower == L".tgz") {
        std::wstring up = extLower.substr(1);
        for (auto& c : up) c = static_cast<wchar_t>(towupper(c));
        info.typeName = (pl ? L"Archiwum " : L"Archive ") + up;
    } else if (extLower == L".py") {
        info.typeName = L"Python Source File";
    } else if (extLower == L".cpp" || extLower == L".cc" || extLower == L".cxx") {
        info.typeName = L"C++ Source File";
    } else if (extLower == L".h" || extLower == L".hpp") {
        info.typeName = L"C/C++ Header File";
    } else if (extLower == L".json") {
        info.typeName = L"JSON Source File";
    } else if (extLower == L".yaml" || extLower == L".yml") {
        info.typeName = L"YAML Source File";
    } else if (extLower == L".xml") {
        info.typeName = pl ? L"Dokument XML" : L"XML Document";
    } else if (extLower == L".pptx" || extLower == L".ppsx" || extLower == L".odp") {
        info.typeName = pl ? L"Prezentacja multimedialna" : L"Slide Presentation";
    } else if (extLower == L".epub" || extLower == L".fb2") {
        info.typeName = pl ? L"Książka elektroniczna (eBook)" : L"eBook Document";
    } else if (extLower == L".sqlite" || extLower == L".sqlite3" || extLower == L".db") {
        info.typeName = pl ? L"Baza danych SQLite 3" : L"SQLite 3 Database";
    } else if (extLower == L".dbf") {
        info.typeName = pl ? L"Tabela bazy dBASE (.dbf)" : L"dBASE Database Table (.dbf)";
    } else if (extLower == L".dds") {
        info.typeName = pl ? L"Tekstura DirectDraw Surface (.dds)" : L"DirectDraw Surface Texture (.dds)";
    } else if (extLower == L".svg") {
        info.typeName = pl ? L"Grafika wektorowa SVG" : L"SVG Vector Graphic";
    } else if (extLower == L".dxf") {
        info.typeName = pl ? L"Rysunek CAD AutoCAD (.dxf)" : L"AutoCAD DXF Drawing (.dxf)";
    } else if (extLower == L".vtk") {
        info.typeName = pl ? L"Siatka 3D VTK (.vtk)" : L"VTK 3D Mesh (.vtk)";
    } else if (extLower == L".exe" || extLower == L".msi") {
        info.typeName = pl ? L"Aplikacja" : L"Application";
    } else if (extLower == L".dll") {
        info.typeName = pl ? L"Rozszerzenie aplikacji (DLL)" : L"Application Extension (DLL)";
    } else if (extLower == L".lnk") {
        info.typeName = pl ? L"Skrót" : L"Shortcut";
    } else {
        std::wstring up = extLower.substr(1);
        for (auto& c : up) c = static_cast<wchar_t>(towupper(c));
        info.typeName = pl ? (L"Plik " + up) : (up + L" File");
    }

    return m_extCache.emplace(key, std::move(info)).first->second;
}

std::vector<FileEntry> FileEngine::EnumerateDirectory(
    const std::wstring& dirPath,
    bool showHidden,
    bool showExtensions,
    Lang lang)
{
    std::vector<FileEntry> results;
    m_lastEnumerateError.store(0);

    if (dirPath == L"THIS_PC" || dirPath.empty()) {
#ifndef _WIN32
        for (const auto& mi : EnumerateLinuxMounts(lang)) {
            FileEntry entry;
            entry.fullPath = mi.mountPoint;
            entry.normKey = PathUtils::NormalizePathKey(mi.mountPoint);
            entry.isDirectory = true;
            entry.isDrive = true;
            entry.isNetworkDrive = mi.isNetwork;
            entry.isOpticalDrive = mi.isOptical;
            entry.sysIconIndex = m_folderIconIndex;
            entry.name = mi.label;
            entry.displayName = mi.label;
            uint64_t totalBytes = mi.totalBytes;
            if (mi.probeAsync) {
                // Network and FUSE mounts: the size the probe thread found, none until it has answered
                ProbeLinuxMountAsync(mi.mountPoint);
                uint64_t freeBytes = 0;
                bool online = false;
                if (GetProbedMountSize(mi.mountPoint, totalBytes, freeBytes, online)) entry.isOffline = !online;
            }
            entry.size = totalBytes;
            if (totalBytes > 0) entry.sizeStr = FormatSizeDetailed(totalBytes);
            entry.typeName = mi.isNetwork
                ? ((lang == Lang::PL ? L"Udział sieciowy (" : L"Network Mount (") + mi.fsType + L")")
                : (mi.isOptical
                    ? (lang == Lang::PL ? L"Stacja dysków optycznych" : L"Optical Drive")
                    : ((lang == Lang::PL ? L"Wolumin Linux (" : L"Linux Volume (") + mi.fsType + L")"));
            results.push_back(std::move(entry));
        }
        // Sticks and cards that are plugged in but not mounted: opening one mounts it (ExplorerApp::NavigateTo)
        for (const auto& uv : EnumerateLinuxUnmountedVolumes()) {
            FileEntry entry;
            entry.fullPath = std::wstring(kUnmountedVolumePrefix) + uv.devNode;
            entry.normKey = PathUtils::NormalizePathKey(entry.fullPath);
            entry.isDirectory = true;
            entry.isDrive = true;
            entry.sysIconIndex = m_folderIconIndex;
            entry.name = UnmountedVolumeTitle(uv, lang);
            entry.displayName = entry.name;
            entry.size = uv.sizeBytes;
            if (uv.sizeBytes > 0) entry.sizeStr = FormatSizeDetailed(uv.sizeBytes);
            entry.typeName = (lang == Lang::PL ? L"Nośnik niezamontowany (" : L"Volume not mounted (") + uv.fsType +
                             (lang == Lang::PL ? L") - otwórz, aby zamontować" : L") - open it to mount it");
            results.push_back(std::move(entry));
        }
#else
        DWORD drivesMask = GetLogicalDrives();
        wchar_t rootPath[] = L"A:\\";
        for (int i = 0; i < 26; ++i) {
            if (drivesMask & (1 << i)) {
                wchar_t dlChar = static_cast<wchar_t>(L'A' + i);
                rootPath[0] = dlChar;
                std::wstring driveLetter = std::wstring(1, dlChar) + L":";
                UINT dtype = GetDriveTypeW(rootPath);

                FileEntry entry;
                entry.fullPath = rootPath;
                entry.normKey = PathUtils::NormalizePathKey(rootPath);
                entry.isDirectory = true;
                entry.isDrive = true;
                entry.sysIconIndex = m_folderIconIndex;

                if (dtype == DRIVE_REMOTE) {
                    entry.isNetworkDrive = true;
                    entry.name = GetMappedNetworkDriveLabel(dlChar, lang);
                    entry.displayName = entry.name;
                    ProbeNetworkDriveAsync(dlChar);

                    bool online = false;
                    uint64_t netTotal = 0;
                    {
                        std::lock_guard<std::mutex> lk(m_net->mutex);
                        auto it = m_net->drives.find(dlChar);
                        if (it != m_net->drives.end()) {
                            online = it->second.online;
                            netTotal = it->second.totalBytes;
                        }
                    }
                    entry.isOffline = !online;
                    entry.size = netTotal;
                    entry.sizeStr = (online && netTotal > 0) ? FormatSizeDetailed(netTotal) : (lang == Lang::PL ? L"Offline" : L"Offline");
                    entry.typeName = online
                        ? (lang == Lang::PL ? L"Dysk sieciowy" : L"Network Drive")
                        : (lang == Lang::PL ? L"Odłączony dysk sieciowy" : L"Disconnected Network Drive");
                } else if (dtype == DRIVE_CDROM) {
                    entry.isOpticalDrive = true;
                    wchar_t volName[MAX_PATH] = {};
                    BOOL volOk = GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
                    entry.isOffline = !volOk;
                    if (volOk && volName[0]) {
                        entry.name = std::wstring(volName) + L" (" + driveLetter + L")";
                    } else {
                        entry.name = (lang == Lang::PL ? L"Stacja dysków BD-ROM (" : L"BD-ROM Drive (") + driveLetter + L")";
                    }
                    entry.displayName = entry.name;
                    entry.typeName = (lang == Lang::PL) ? L"Stacja dysków CD/DVD/BD-ROM" : L"CD/DVD/BD-ROM Drive";
                    if (volOk) {
                        ULARGE_INTEGER freeBytes = {}, totalBytes = {};
                        if (GetDiskFreeSpaceExW(rootPath, &freeBytes, &totalBytes, nullptr)) {
                            entry.size = totalBytes.QuadPart;
                            entry.sizeStr = FormatSizeDetailed(totalBytes.QuadPart);
                        }
                    }
                } else {
                    wchar_t volName[MAX_PATH] = {};
                    BOOL volOk = GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
                    entry.isOffline = !volOk;

                    ULARGE_INTEGER freeBytes = {}, totalBytes = {};
                    if (volOk) {
                        GetDiskFreeSpaceExW(rootPath, &freeBytes, &totalBytes, nullptr);
                    }

                    if (volName[0]) {
                        entry.name = std::wstring(volName) + L" (" + driveLetter + L")";
                    } else {
                        entry.name = (lang == Lang::PL ? L"Dysk lokalny (" : L"Local Disk (") + driveLetter + L")";
                    }
                    entry.displayName = entry.name;
                    entry.size = totalBytes.QuadPart;
                    if (totalBytes.QuadPart > 0) {
                        entry.sizeStr = FormatSizeDetailed(totalBytes.QuadPart);
                    }
                    entry.typeName = (lang == Lang::PL) ? L"Dysk lokalny" : L"Local Disk";
                }

                results.push_back(std::move(entry));
            }
        }
#endif

        // Append FileZilla / saved FTP & FTPS servers under Ten komputer
        auto ftpSites = FtpClient::LoadBookmarks();
        for (const auto& bm : ftpSites) {
            if (bm.url.empty()) continue;
            FileEntry entry;
            entry.name = L"[FTP] " + bm.name;
            entry.displayName = entry.name;
            entry.fullPath = bm.url;
            entry.normKey = PathUtils::ToLower(bm.url);
            entry.isDirectory = true;
            entry.isDrive = true;
            entry.isNetworkDrive = true;
            entry.sizeStr = bm.useFtps ? L"FTPS" : L"FTP/TLS";
            if (bm.fromFileZilla) {
                entry.typeName = (lang == Lang::PL) ? L"Serwer FTP / FTPS (FileZilla)" : L"FTP / FTPS Server (FileZilla)";
            } else {
                entry.typeName = (lang == Lang::PL) ? L"Serwer FTP / FTPS" : L"FTP / FTPS Server";
            }
            results.push_back(std::move(entry));
        }

        return results;
    }

    if (dirPath == L"RECYCLE_BIN") {
        const std::wstring sid = GetCurrentUserSidString();
        const ExtTypeInfo dirExtInfo = GetExtensionInfo(L"", true, lang);
        std::wstring lastExt = L"__INIT__";
        ExtTypeInfo lastExtInfo = {};

        DWORD drivesMask = GetLogicalDrives();
        wchar_t rootPath[] = L"A:\\";
        for (int i = 0; i < 26; ++i) {
            if (!(drivesMask & (1 << i))) continue;
            wchar_t dlChar = static_cast<wchar_t>(L'A' + i);
            rootPath[0] = dlChar;
            UINT dtype = GetDriveTypeW(rootPath);
            if (dtype != DRIVE_FIXED && dtype != DRIVE_REMOVABLE) continue;
            // Skip legacy A:/B: floppy slots unless explicitly fixed
            if (dtype == DRIVE_REMOVABLE && (dlChar == L'A' || dlChar == L'B')) continue;

            std::vector<std::wstring> binDirs;
            std::wstring baseBin = std::wstring(1, dlChar) + L":\\$Recycle.Bin";
            DWORD baseAttrs = GetFileAttributesW(baseBin.c_str());
            if (baseAttrs == INVALID_FILE_ATTRIBUTES || !(baseAttrs & FILE_ATTRIBUTE_DIRECTORY)) continue;

            if (!sid.empty()) {
                std::wstring sidBin = baseBin + L"\\" + sid;
                DWORD sidAttrs = GetFileAttributesW(sidBin.c_str());
                if (sidAttrs != INVALID_FILE_ATTRIBUTES && (sidAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
                    binDirs.push_back(std::move(sidBin));
                }
            }
            binDirs.push_back(std::move(baseBin)); // Also covers FAT32 / exFAT flat $Recycle.Bin

            for (const std::wstring& binDir : binDirs) {
                WIN32_FIND_DATAW ffd = {};
                HANDLE hFind = FindFirstFileExW(
                    (binDir + L"\\$I*").c_str(),
                    FindExInfoBasic,
                    &ffd,
                    FindExSearchNameMatch,
                    nullptr,
                    FIND_FIRST_EX_LARGE_FETCH);
                if (hFind == INVALID_HANDLE_VALUE) continue;

                do {
                    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    const wchar_t* fn = ffd.cFileName;
                    if (!fn || fn[0] != L'$' || (fn[1] != L'I' && fn[1] != L'i') || fn[2] == L'\0') continue;

                    std::wstring infoPath = binDir + L"\\" + fn;
                    std::wstring dataPath = binDir + L"\\$R" + (fn + 2);

                    WIN32_FILE_ATTRIBUTE_DATA fad = {};
                    if (!GetFileAttributesExW(dataPath.c_str(), GetFileExInfoStandard, &fad)) continue;

                    std::vector<uint8_t> raw;
                    RecycleBinRecord rec;
                    if (!PathUtils::ReadFileBytes(infoPath, 65536, raw) || !ParseRecycleBinInfoBytes(raw, rec)) continue;

                    FileEntry entry;
                    entry.name = PathUtils::GetFileName(rec.originalPath);
                    if (entry.name.empty()) entry.name = dataPath.substr(binDir.size() + 1);
                    entry.originalPath = rec.originalPath;
                    entry.fullPath = dataPath;
                    entry.normKey = PathUtils::NormalizePathKey(dataPath);
                    entry.attributes = fad.dwFileAttributes;
                    entry.isDirectory = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    entry.isHidden = false;
                    entry.writeTime = rec.deletedTime;
                    entry.dateStr = FormatFileTime(rec.deletedTime);

                    uint64_t diskSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                    entry.size = (rec.fileSize > 0) ? rec.fileSize : (entry.isDirectory ? 0ULL : diskSize);
                    entry.sizeStr = (entry.isDirectory && entry.size == 0) ? L"" : FormatSizeKB(entry.size);

                    if (!entry.isDirectory) {
                        entry.ext = PathUtils::GetExtension(entry.name);
                        entry.displayName = showExtensions ? entry.name : PathUtils::GetStem(entry.name, false);
                        entry.isImage = IsImageExtension(entry.ext);
                        entry.isVideo = IsVideoExtension(entry.ext);
                        entry.isAudio = IsAudioExtension(entry.ext);
                        if (entry.ext != lastExt) {
                            lastExt = entry.ext;
                            lastExtInfo = GetExtensionInfo(entry.ext, false, lang);
                        }
                        entry.sysIconIndex = lastExtInfo.sysIconIndex;
                    } else {
                        entry.displayName = entry.name;
                        entry.sysIconIndex = dirExtInfo.sysIconIndex;
                    }

                    std::wstring origDir = PathUtils::GetParentPath(rec.originalPath);
                    entry.typeName = !origDir.empty()
                        ? origDir
                        : (entry.isDirectory ? dirExtInfo.typeName : lastExtInfo.typeName);

                    results.push_back(std::move(entry));
                } while (FindNextFileW(hFind, &ffd));

                FindClose(hFind);
            }
        }

#ifndef _WIN32
        // FreeDesktop.org Trash: the home one (~/.local/share/Trash/{files,info}) and the drives' own (a USB stick's
        // ".Trash-<uid>"), each item with its .trashinfo
        for (const std::string& trashBase : LinuxTrash::AllTrashDirs()) {
            const std::string relativeBase = LinuxTrash::RelativePathBase(trashBase);
            const std::wstring trashInfoDir = PathUtils::Utf8ToWide(trashBase + "/info");
            const std::wstring trashFilesDir = PathUtils::Utf8ToWide(trashBase + "/files");
            WIN32_FIND_DATAW ffd = {};
            HANDLE hFind = FindFirstFileExW(
                (trashInfoDir + L"/*.trashinfo").c_str(),
                FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    std::wstring fn(ffd.cFileName);
                    if (fn.size() <= 10) continue;
                    std::wstring baseName = fn.substr(0, fn.size() - 10); // strip ".trashinfo"
                    std::wstring infoPath = trashInfoDir + L"/" + fn;
                    std::wstring dataPath = trashFilesDir + L"/" + baseName;

                    WIN32_FILE_ATTRIBUTE_DATA fad = {};
                    if (!GetFileAttributesExW(dataPath.c_str(), GetFileExInfoStandard, &fad)) continue;

                    std::vector<uint8_t> raw;
                    RecycleBinRecord rec;
                    if (!PathUtils::ReadFileBytes(infoPath, 65536, raw) || !ParseFreeDesktopTrashInfo(raw, rec, relativeBase)) continue;

                    FileEntry entry;
                    entry.name = PathUtils::GetFileName(rec.originalPath);
                    if (entry.name.empty()) entry.name = baseName;
                    entry.originalPath = rec.originalPath;
                    entry.fullPath = dataPath;
                    entry.normKey = PathUtils::NormalizePathKey(dataPath);
                    entry.attributes = fad.dwFileAttributes;
                    entry.isDirectory = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    entry.isHidden = false;
                    entry.writeTime = rec.deletedTime;
                    entry.dateStr = FormatFileTime(rec.deletedTime);

                    uint64_t diskSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                    entry.size = entry.isDirectory ? 0ULL : diskSize;
                    entry.sizeStr = (entry.isDirectory && entry.size == 0) ? L"" : FormatSizeKB(entry.size);

                    if (!entry.isDirectory) {
                        entry.ext = PathUtils::GetExtension(entry.name);
                        entry.displayName = showExtensions ? entry.name : PathUtils::GetStem(entry.name, false);
                        entry.isImage = IsImageExtension(entry.ext);
                        entry.isVideo = IsVideoExtension(entry.ext);
                        entry.isAudio = IsAudioExtension(entry.ext);
                        if (entry.ext != lastExt) {
                            lastExt = entry.ext;
                            lastExtInfo = GetExtensionInfo(entry.ext, false, lang);
                        }
                        entry.sysIconIndex = lastExtInfo.sysIconIndex;
                    } else {
                        entry.displayName = entry.name;
                        entry.sysIconIndex = dirExtInfo.sysIconIndex;
                    }

                    std::wstring origDir = PathUtils::GetParentPath(rec.originalPath);
                    entry.typeName = !origDir.empty()
                        ? origDir
                        : (entry.isDirectory ? dirExtInfo.typeName : lastExtInfo.typeName);
                    results.push_back(std::move(entry));
                } while (FindNextFileW(hFind, &ffd));
                FindClose(hFind);
            }
        }
#endif

        {
            std::lock_guard<std::mutex> lock(m_deleteStateMutex);
            if (!m_pendingDeleteKeys.empty()) {
                results.erase(std::remove_if(results.begin(), results.end(), [this](const FileEntry& e) {
                    return m_pendingDeleteKeys.count(e.normKey) != 0;
                }), results.end());
            }
        }
        SyncDirectorySizes(results);
        return results;
    }

    if (IsUncServerRoot(dirPath)) {
        // \\server: the folders it shares, read in the background (an empty list until they arrive)
        NetShares::GetShares(dirPath, lang, results);
        for (auto& entry : results) entry.sysIconIndex = m_folderIconIndex;
        return results;
    }

    const bool isFtp = FtpClient::IsFtpPath(dirPath);
    if (isFtp || RepoBrowser::IsRepoUrl(dirPath)) {
        if (isFtp) {
            FtpClient::EnumerateFtpDirectory(dirPath, showHidden, showExtensions, lang, results, nullptr);
        } else {
            // A repository folder: what is known now (an empty list while it is read in the background)
            RepoBrowser::GetListing(dirPath, showExtensions, lang, results);
            if (!showHidden) {
                results.erase(std::remove_if(results.begin(), results.end(), [](const FileEntry& e) {
                    return !e.name.empty() && e.name[0] == L'.';
                }), results.end());
            }
        }
        std::wstring lastExt = L"__INIT__";
        ExtTypeInfo lastExtInfo = {};
        for (auto& entry : results) {
            if (!entry.isDirectory && entry.sysIconIndex <= 0) {
                if (entry.ext != lastExt) {
                    lastExt = entry.ext;
                    lastExtInfo = GetExtensionInfo(entry.ext, false, lang);
                }
                entry.sysIconIndex = lastExtInfo.sysIconIndex;
            }
        }
        return results;
    }

    results.reserve(512);

    std::wstring cleanDir = PathUtils::TrimTrailingSlashes(dirPath);
    std::wstring prefix = PathUtils::EnsureTrailingSlash(cleanDir);
    std::wstring normDirPrefix = PathUtils::NormalizePathKey(cleanDir);
    if (!normDirPrefix.empty() && normDirPrefix.back() != L'\\') {
        normDirPrefix.push_back(L'\\');
    }
    std::wstring searchPattern = prefix + L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    DWORD findError = 0;
    // Fast-path: if cleanDir is a virtual archive path, skip the failed kernel FindFirstFileExW
    if (!ArchiveExplorer::IsVirtualArchivePath(cleanDir)) {
        hFind = FindFirstFileExW(
            searchPattern.c_str(),
            FindExInfoBasic,
            &ffd,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH);
        if (hFind == INVALID_HANDLE_VALUE) findError = GetLastError();
    }

    if (hFind == INVALID_HANDLE_VALUE) {
        std::wstring arcFile, innerDir;
        if (!ArchiveExplorer::SplitVirtualArchivePath(cleanDir, arcFile, innerDir)) {
            // Not inside an archive: the folder itself could not be listed (see LastEnumerateError)
            m_lastEnumerateError.store(findError);
        } else {
            std::vector<ArchiveItemEntry> arcChildren;
            if (ArchiveExplorer::EnumerateArchiveFolder(arcFile, innerDir, arcChildren)) {
                results.reserve(arcChildren.size());
                const ExtTypeInfo& dirExtInfo = GetExtensionInfo(L"", true, lang);
                std::wstring lastExt = L"__INIT__";
                ExtTypeInfo lastExtInfo = {};
                for (const auto& ac : arcChildren) {
                    FileEntry entry;
                    entry.name = ac.name;
                    entry.fullPath = prefix + ac.name;
                    entry.normKey = normDirPrefix + PathUtils::ToLower(ac.name);
                    entry.isDirectory = ac.isDirectory;
                    entry.size = ac.uncompressedSize;
                    if (!entry.isDirectory) {
                        entry.sizeStr = FormatSizeKB(entry.size);
                        entry.ext = PathUtils::GetExtension(entry.name);
                        entry.displayName = showExtensions ? entry.name : PathUtils::GetStem(entry.name, false);
                        entry.isImage = IsImageExtension(entry.ext);
                        entry.isVideo = IsVideoExtension(entry.ext);
                        entry.isAudio = IsAudioExtension(entry.ext);
                        if (entry.ext != lastExt) {
                            lastExt = entry.ext;
                            lastExtInfo = GetExtensionInfo(entry.ext, false, lang);
                        }
                        entry.typeName = lastExtInfo.typeName;
                        entry.sysIconIndex = lastExtInfo.sysIconIndex;
                    } else {
                        entry.displayName = entry.name;
                        if (entry.size > 0) entry.sizeStr = FormatSizeKB(entry.size);
                        entry.typeName = dirExtInfo.typeName;
                        entry.sysIconIndex = dirExtInfo.sysIconIndex;
                    }
                    if (ac.dosDate != 0) {
                        FILETIME localFt = {}, utcFt = {};
                        if (DosDateTimeToFileTime(ac.dosDate, ac.dosTime, &localFt) &&
                            LocalFileTimeToFileTime(&localFt, &utcFt)) {
                            entry.writeTime = utcFt;
                            entry.dateStr = FormatFileTime(utcFt);
                        }
                    }
                    results.push_back(std::move(entry));
                }
            }
        }
        return results;
    }

    const ExtTypeInfo& dirExtInfo = GetExtensionInfo(L"", true, lang);
    std::wstring lastExt = L"__INIT__";
    ExtTypeInfo lastExtInfo = {};

    do {
        const wchar_t* fileName = ffd.cFileName;
        if (fileName[0] == L'.' && (fileName[1] == L'\0' || (fileName[1] == L'.' && fileName[2] == L'\0'))) {
            continue;
        }
        if (wcscmp(fileName, L".explorer2_undo") == 0) {
            continue;
        }

        bool isHidden = (ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        if (!showHidden && isHidden) {
            continue;
        }

        FileEntry entry;
        entry.name = fileName;
        entry.fullPath = prefix + entry.name;
        entry.normKey = normDirPrefix + PathUtils::ToLower(entry.name);
        entry.attributes = ffd.dwFileAttributes;
        entry.isDirectory = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.isHidden = isHidden;
        entry.writeTime = ffd.ftLastWriteTime;
        entry.dateStr = FormatFileTime(ffd.ftLastWriteTime);

        if (!entry.isDirectory) {
            entry.size = (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow;
            entry.sizeStr = FormatSizeKB(entry.size);

            const wchar_t* dot = wcsrchr(fileName, L'.');
            if (dot && dot != fileName) {
                entry.ext = PathUtils::ToLower(dot);
                entry.displayName = showExtensions
                    ? entry.name
                    : entry.name.substr(0, static_cast<size_t>(dot - fileName));
            } else {
                entry.displayName = entry.name;
            }
            entry.isImage = IsImageExtension(entry.ext);
            entry.isVideo = IsVideoExtension(entry.ext);
            entry.isAudio = IsAudioExtension(entry.ext);

            if (entry.ext != lastExt) {
                lastExt = entry.ext;
                lastExtInfo = GetExtensionInfo(entry.ext, false, lang);
            }
            entry.typeName = lastExtInfo.typeName;
            entry.sysIconIndex = lastExtInfo.sysIconIndex;
        } else {
            entry.displayName = entry.name;
            entry.typeName = dirExtInfo.typeName;
            entry.sysIconIndex = dirExtInfo.sysIconIndex;
        }

        results.push_back(std::move(entry));
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);

    // Hide entries that a background delete is still working on, so a refresh during a long delete
    // does not make a half-deleted folder "come back" before it is really gone.
    {
        std::lock_guard<std::mutex> lock(m_deleteStateMutex);
        if (!m_pendingDeleteKeys.empty()) {
            results.erase(std::remove_if(results.begin(), results.end(), [this](const FileEntry& e) {
                return m_pendingDeleteKeys.count(e.normKey) != 0;
            }), results.end());
        }
    }

    // Single-lock sync of any cached folder sizes from our hash table!
    SyncDirectorySizes(results);
    return results;
}

void FileEngine::SortEntries(std::vector<FileEntry>& entries, SortColumn column, bool ascending) {
    std::sort(entries.begin(), entries.end(), [column, ascending](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }

        int cmp = 0;
        switch (column) {
        case SortColumn::Name:
            cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::DateModified:
            cmp = CompareFileTime(&a.writeTime, &b.writeTime);
            if (cmp == 0) cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Type:
            cmp = StrCmpLogicalW(a.typeName.c_str(), b.typeName.c_str());
            if (cmp == 0) cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Size:
            if (a.size < b.size) cmp = -1;
            else if (a.size > b.size) cmp = 1;
            else cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        }

        return ascending ? (cmp < 0) : (cmp > 0);
    });
}

bool FileEngine::MountVolume(const std::wstring& devNode, std::wstring& outMountPoint, std::wstring& outError) {
    outMountPoint.clear();
    outError.clear();
#ifdef _WIN32
    (void)devNode;
    outError = L"NO_TOOL";
    return false;
#else
    // Only a device node may become an argument of a command: "/dev/" and a plain name
    const std::string dev = PathUtils::WideToUtf8(devNode);
    bool plain = (dev.size() > 5 && dev.compare(0, 5, "/dev/") == 0);
    for (size_t i = 5; plain && i < dev.size(); ++i) {
        const char c = dev[i];
        plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    }
    if (!plain) {
        outError = L"bad device name";
        return false;
    }

    // Where the device is mounted, by what the mount table says (not by what a tool printed)
    auto mountPointNow = [&dev]() -> std::string {
        struct stat st = {};
        const bool isBlock = (::stat(dev.c_str(), &st) == 0) && S_ISBLK(st.st_mode);
        for (const auto& m : ReadMountInfo()) {
            if ((isBlock && m.device == static_cast<uint64_t>(st.st_rdev)) || m.source == dev) return m.mountPoint;
        }
        return {};
    };

    std::string mountPoint = mountPointNow();       // someone else may have mounted it meanwhile
    if (mountPoint.empty()) {
        ToolProcess::Options opt;
        opt.timeoutMs = 45000;
        opt.mergeStderr = true;
        opt.envOverride = { L"LC_ALL=C", L"LANG=C" };
        static const std::atomic<bool> s_never{ false };
        std::string said;
        bool anyTool = false;

        const std::wstring udisksctl = ToolProcess::FindExecutable(L"udisksctl", { L"/usr/bin/udisksctl", L"/bin/udisksctl" });
        if (!udisksctl.empty()) {
            anyTool = true;
            const ToolProcess::Result res = ToolProcess::Run(udisksctl, { L"mount", L"--block-device", devNode, L"--no-user-interaction" }, opt, s_never);
            said = res.out;
            mountPoint = mountPointNow();
            if (mountPoint.empty() && res.ok()) mountPoint = LinuxStorage::ParseMountedAt(res.out);
        }
        if (mountPoint.empty()) {
            const std::wstring gio = ToolProcess::FindExecutable(L"gio", { L"/usr/bin/gio", L"/bin/gio" });
            if (!gio.empty()) {
                anyTool = true;
                const ToolProcess::Result res = ToolProcess::Run(gio, { L"mount", L"-d", devNode }, opt, s_never);
                if (said.empty()) said = res.out;
                mountPoint = mountPointNow();
                if (mountPoint.empty() && res.ok()) mountPoint = LinuxStorage::ParseMountedAt(res.out);
            }
        }
        if (mountPoint.empty()) {
            outError = anyTool ? ToolProcess::FirstLine(ToolProcess::DecodeOutput(said)) : std::wstring(L"NO_TOOL");
            return false;
        }
    }
    outMountPoint = PathUtils::Utf8ToWide(mountPoint);
    return true;
#endif
}

std::wstring FileEngine::UnmountedVolumesSignature() {
#ifdef _WIN32
    return L"";
#else
    std::wstring sig;
    for (const auto& uv : EnumerateLinuxUnmountedVolumes()) {
        sig += uv.devNode + L"|" + uv.label + L"|" + std::to_wstring(uv.sizeBytes) + L";";
    }
    return sig;
#endif
}

std::vector<std::pair<std::wstring, std::wstring>> FileEngine::GetDefaultQuickAccessFolders(Lang lang) {
    const Strings& s = GetStrings(lang);
    std::vector<std::pair<std::wstring, std::wstring>> list;

    auto addKnown = [&](REFKNOWNFOLDERID rfid, const wchar_t* label) {
        PWSTR pszPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, nullptr, &pszPath)) && pszPath) {
            list.emplace_back(label, PathUtils::TrimTrailingSlashes(pszPath));
            CoTaskMemFree(pszPath);
        }
    };
    addKnown(FOLDERID_Desktop, s.navDesktop);
    addKnown(FOLDERID_Downloads, s.navDownloads);
    addKnown(FOLDERID_Documents, s.navDocuments);
    addKnown(FOLDERID_Pictures, s.navPictures);
    return list;
}

std::vector<TreeNode> FileEngine::BuildNavigationTree(
    Lang lang,
    const std::vector<std::pair<std::wstring, std::wstring>>& pinnedQuickAccess,
    unsigned sections)
{
    std::vector<TreeNode> nodes;
    const Strings& s = GetStrings(lang);

    // 1. Quick Access Header (expanded by default)
    const bool showQuickAccess = (sections & kNavQuickAccess) != 0;
    if (showQuickAccess) {
        TreeNode qaHeader;
        qaHeader.label = s.navQuickAccess;
        qaHeader.normKey = L"__HEADER_QA__";
        qaHeader.indent = 0;
        qaHeader.isHeader = true;
        qaHeader.isQuickAccess = true;
        qaHeader.hasChildren = true;
        qaHeader.isExpanded = true;
        nodes.push_back(qaHeader);
    }

    // A pin that is one of the user's own folders is shown under the name the program has for it in its language
    // ("Pulpit"), as on the very first start. The settings file keeps only the path, and a label made from the path
    // is the folder's name on disk ("Desktop"), which is what every later start used to show.
    std::vector<std::pair<std::wstring, std::wstring>> ownFolders;
    if (showQuickAccess) {
        ownFolders = GetDefaultQuickAccessFolders(lang);
        for (auto& own : ownFolders) own.second = PathUtils::NormalizePathKey(own.second);
    }

    for (const auto& [label, path] : pinnedQuickAccess) {
        if (!showQuickAccess) break;
        TreeNode n;
        n.label = label;
        n.path = path;
        n.normKey = PathUtils::NormalizePathKey(path);
        for (const auto& [ownLabel, ownKey] : ownFolders) {
            if (ownKey == n.normKey) {
                n.label = ownLabel;
                break;
            }
        }
        n.indent = 1;
        n.isQuickAccess = true;
        n.hasChildren = true;
        n.isExpanded = false;
        nodes.push_back(std::move(n));
    }

    wchar_t userProfile[MAX_PATH] = {};
    if ((sections & kNavCloudFolders) && GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH)) {
        std::wstring dropboxPath = std::wstring(userProfile) + L"\\Dropbox";
        if (GetFileAttributesW(dropboxPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            TreeNode n;
            n.label = L"Dropbox";
            n.path = dropboxPath;
            n.normKey = PathUtils::NormalizePathKey(dropboxPath);
            n.indent = 0;
            n.hasChildren = true;
            n.isExpanded = false;
            nodes.push_back(std::move(n));
        }
        std::wstring oneDrivePath = std::wstring(userProfile) + L"\\OneDrive";
        if (GetFileAttributesW(oneDrivePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            TreeNode n;
            n.label = L"OneDrive - Personal";
            n.path = oneDrivePath;
            n.normKey = PathUtils::NormalizePathKey(oneDrivePath);
            n.indent = 0;
            n.hasChildren = true;
            n.isExpanded = false;
            nodes.push_back(std::move(n));
        }
    }

    // Recycle Bin ("Kosz") virtual folder at top level
    if (sections & kNavRecycleBin) {
        TreeNode binNode;
        binNode.label = s.navRecycleBin;
        binNode.path = L"RECYCLE_BIN";
        binNode.normKey = L"__RECYCLE_BIN__";
        binNode.indent = 0;
        binNode.hasChildren = false;
        binNode.isExpanded = false;
        nodes.push_back(std::move(binNode));
    }

    // 2. This PC ("Ten komputer", expanded by default)
    TreeNode pcHeader;
    pcHeader.label = s.navThisPC;
    pcHeader.path = L"THIS_PC";
    pcHeader.normKey = L"__HEADER_PC__";
    pcHeader.indent = 0;
    pcHeader.isHeader = true;
    pcHeader.hasChildren = true;
    pcHeader.isExpanded = true;
    nodes.push_back(pcHeader);

#ifndef _WIN32
    for (const auto& mi : EnumerateLinuxMounts(lang)) {
        TreeNode driveNode;
        driveNode.path = mi.mountPoint;
        driveNode.normKey = PathUtils::NormalizePathKey(mi.mountPoint);
        driveNode.label = mi.label;
        driveNode.indent = 1;
        driveNode.isDrive = true;
        driveNode.isNetworkDrive = mi.isNetwork;
        driveNode.isOpticalDrive = mi.isOptical;
        driveNode.hasChildren = true;
        driveNode.isExpanded = false;
        uint64_t totalBytes = mi.totalBytes;
        uint64_t freeBytes = mi.freeBytes;
        if (mi.probeAsync) {
            // Network and FUSE mounts: the size the probe thread found, none until it has answered
            ProbeLinuxMountAsync(mi.mountPoint);
            bool online = false;
            if (GetProbedMountSize(mi.mountPoint, totalBytes, freeBytes, online)) driveNode.isOffline = !online;
        }
        driveNode.totalBytes = totalBytes;
        driveNode.freeBytes = freeBytes;
        driveNode.size = (totalBytes >= freeBytes) ? (totalBytes - freeBytes) : 0;
        driveNode.sizeStr = LinuxDriveBadge(driveNode.size, totalBytes);
        nodes.push_back(std::move(driveNode));
    }
    // Sticks and cards that are plugged in but not mounted: a click mounts one and opens it
    for (const auto& uv : EnumerateLinuxUnmountedVolumes()) {
        TreeNode volNode;
        volNode.path = std::wstring(kUnmountedVolumePrefix) + uv.devNode;
        volNode.normKey = PathUtils::NormalizePathKey(volNode.path);
        volNode.label = UnmountedVolumeTitle(uv, lang);
        volNode.indent = 1;
        volNode.isDrive = true;
        volNode.hasChildren = false;
        volNode.isExpanded = false;
        nodes.push_back(std::move(volNode));
    }
#else
    DWORD drivesMask = GetLogicalDrives();
    wchar_t rootPath[] = L"A:\\";
    for (int i = 0; i < 26; ++i) {
        if (drivesMask & (1 << i)) {
            wchar_t dlChar = static_cast<wchar_t>(L'A' + i);
            rootPath[0] = dlChar;
            std::wstring driveLetter = std::wstring(1, dlChar) + L":";
            UINT dtype = GetDriveTypeW(rootPath);

            TreeNode driveNode;
            driveNode.path = rootPath;
            driveNode.normKey = PathUtils::NormalizePathKey(rootPath);
            driveNode.indent = 1;
            driveNode.isDrive = true;
            driveNode.hasChildren = true;
            driveNode.isExpanded = false;

            auto formatDriveTreeBadge = [](uint64_t usedBytes, uint64_t totalBytes) -> std::wstring {
                if (totalBytes == 0) return L"";
                wchar_t buf[64] = {};
                if (totalBytes >= (1ULL << 40)) {
                    double uTb = static_cast<double>(usedBytes) / static_cast<double>(1ULL << 40);
                    double tTb = static_cast<double>(totalBytes) / static_cast<double>(1ULL << 40);
                    swprintf_s(buf, L"%.1f / %.1f TB", uTb, tTb);
                } else if (totalBytes >= (10ULL << 30)) {
                    uint64_t uGb = (usedBytes + (1ULL << 29)) >> 30;
                    uint64_t tGb = (totalBytes + (1ULL << 29)) >> 30;
                    swprintf_s(buf, L"%llu / %llu GB", static_cast<unsigned long long>(uGb), static_cast<unsigned long long>(tGb));
                } else if (totalBytes >= (1ULL << 30)) {
                    double uGb = static_cast<double>(usedBytes) / static_cast<double>(1ULL << 30);
                    double tGb = static_cast<double>(totalBytes) / static_cast<double>(1ULL << 30);
                    swprintf_s(buf, L"%.1f / %.1f GB", uGb, tGb);
                } else {
                    uint64_t uMb = (usedBytes + (1ULL << 19)) >> 20;
                    uint64_t tMb = (totalBytes + (1ULL << 19)) >> 20;
                    swprintf_s(buf, L"%llu / %llu MB", static_cast<unsigned long long>(uMb), static_cast<unsigned long long>(tMb));
                }
                return buf;
            };

            if (dtype == DRIVE_REMOTE) {
                driveNode.isNetworkDrive = true;
                driveNode.label = GetMappedNetworkDriveLabel(dlChar, lang);
                ProbeNetworkDriveAsync(dlChar);
                bool online = false;
                uint64_t netTotal = 0, netFree = 0;
                {
                    std::lock_guard<std::mutex> lk(m_net->mutex);
                    auto it = m_net->drives.find(dlChar);
                    if (it != m_net->drives.end()) {
                        online = it->second.online;
                        netTotal = it->second.totalBytes;
                        netFree = it->second.freeBytes;
                    }
                }
                driveNode.isOffline = !online;
                if (online && netTotal > 0) {
                    driveNode.totalBytes = netTotal;
                    driveNode.freeBytes = netFree;
                    driveNode.size = (netTotal >= netFree) ? (netTotal - netFree) : 0;
                    driveNode.sizeStr = formatDriveTreeBadge(driveNode.size, driveNode.totalBytes);
                }
            } else if (dtype == DRIVE_CDROM) {
                driveNode.isOpticalDrive = true;
                wchar_t volName[MAX_PATH] = {};
                BOOL volOk = GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
                driveNode.isOffline = !volOk;
                driveNode.label = (volOk && volName[0])
                    ? (std::wstring(volName) + L" (" + driveLetter + L")")
                    : ((lang == Lang::PL ? L"Stacja dysków BD-ROM (" : L"BD-ROM Drive (") + driveLetter + L")");
                if (volOk) {
                    ULARGE_INTEGER freeB = {}, totalB = {};
                    if (GetDiskFreeSpaceExW(rootPath, &freeB, &totalB, nullptr) && totalB.QuadPart > 0) {
                        driveNode.totalBytes = totalB.QuadPart;
                        driveNode.freeBytes = freeB.QuadPart;
                        driveNode.size = (totalB.QuadPart >= freeB.QuadPart) ? (totalB.QuadPart - freeB.QuadPart) : 0;
                        driveNode.sizeStr = formatDriveTreeBadge(driveNode.size, driveNode.totalBytes);
                    }
                }
            } else {
                wchar_t volName[MAX_PATH] = {};
                BOOL volOk = GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
                driveNode.isOffline = !volOk;
                if (volName[0]) {
                    driveNode.label = std::wstring(volName) + L" (" + driveLetter + L")";
                } else {
                    driveNode.label = (lang == Lang::PL ? L"Dysk lokalny (" : L"Local Disk (") + driveLetter + L")";
                }
                if (volOk) {
                    ULARGE_INTEGER freeB = {}, totalB = {};
                    if (GetDiskFreeSpaceExW(rootPath, &freeB, &totalB, nullptr) && totalB.QuadPart > 0) {
                        driveNode.totalBytes = totalB.QuadPart;
                        driveNode.freeBytes = freeB.QuadPart;
                        driveNode.size = (totalB.QuadPart >= freeB.QuadPart) ? (totalB.QuadPart - freeB.QuadPart) : 0;
                        driveNode.sizeStr = formatDriveTreeBadge(driveNode.size, driveNode.totalBytes);
                    }
                }
            }
            nodes.push_back(std::move(driveNode));
        }
    }
#endif

    // FTP / FileZilla (FTPS) section under Ten komputer
    if (sections & kNavFtpServers) {
        auto ftpSites = FtpClient::LoadBookmarks();
        TreeNode ftpGroup;
        ftpGroup.label = (lang == Lang::PL) ? L"FTP / FileZilla (FTPS)" : L"FTP / FileZilla (FTPS)";
        ftpGroup.path = L"THIS_PC";
        ftpGroup.normKey = L"__HEADER_FTP__";
        ftpGroup.indent = 1;
        ftpGroup.isFtpNode = true;
        ftpGroup.hasChildren = !ftpSites.empty();
        ftpGroup.isExpanded = true;
        if (!ftpSites.empty()) {
            ftpGroup.sizeStr = std::to_wstring(ftpSites.size()) + (lang == Lang::PL ? L" serw." : L" srv.");
        }
        nodes.push_back(std::move(ftpGroup));

        for (const auto& bm : ftpSites) {
            if (bm.url.empty()) continue;
            TreeNode ftpNode;
            ftpNode.label = bm.name;
            ftpNode.path = bm.url;
            ftpNode.normKey = PathUtils::ToLower(bm.url);
            ftpNode.indent = 2;
            ftpNode.isFtpNode = true;
            ftpNode.hasChildren = true;
            ftpNode.isExpanded = false;
            ftpNode.sizeStr = bm.useFtps ? L"FTPS" : L"FTP/TLS";
            nodes.push_back(std::move(ftpNode));
        }
    }

    SyncTreeNodeSizes(nodes);
    return nodes;
}

std::vector<TreeNode> FileEngine::EnumerateSubfoldersForTree(
    const std::wstring& parentPath,
    int childIndent,
    bool isQuickAccessBranch)
{
    std::vector<TreeNode> subNodes;
    if (parentPath.empty() || IsVirtualFolder(parentPath)) return subNodes;

    if (FtpClient::IsFtpPath(parentPath)) {
        std::vector<FileEntry> ftpEntries;
        if (FtpClient::EnumerateFtpDirectory(parentPath, false, true, Lang::PL, ftpEntries, nullptr)) {
            for (const auto& e : ftpEntries) {
                if (!e.isDirectory) continue;
                TreeNode child;
                child.label = e.name;
                child.path = e.fullPath;
                child.normKey = PathUtils::ToLower(e.fullPath);
                child.indent = childIndent;
                child.isFtpNode = true;
                child.hasChildren = true;
                child.isExpanded = false;
                subNodes.push_back(std::move(child));
            }
            std::sort(subNodes.begin(), subNodes.end(), [](const TreeNode& a, const TreeNode& b) {
                return StrCmpLogicalW(a.label.c_str(), b.label.c_str()) < 0;
            });
        }
        return subNodes;
    }

    std::wstring prefix = PathUtils::EnsureTrailingSlash(PathUtils::TrimTrailingSlashes(parentPath));
    std::wstring pattern = prefix + L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &ffd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    if (hFind == INVALID_HANDLE_VALUE) return subNodes;

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (ffd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        const wchar_t* name = ffd.cFileName;
        if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) continue;
        if (wcscmp(name, L"$RECYCLE.BIN") == 0 || wcscmp(name, L"System Volume Information") == 0) continue;

        TreeNode child;
        child.label = name;
        child.path = prefix + name;
        child.normKey = PathUtils::NormalizePathKey(child.path);
        child.indent = childIndent;
        child.isQuickAccess = isQuickAccessBranch;
        child.hasChildren = true;
        child.isExpanded = false;
        subNodes.push_back(std::move(child));
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);

    std::sort(subNodes.begin(), subNodes.end(), [](const TreeNode& a, const TreeNode& b) {
        return StrCmpLogicalW(a.label.c_str(), b.label.c_str()) < 0;
    });
    return subNodes;
}

namespace {

const wchar_t kUndoLockName[] = L"session.lock";

// "s_<pid>_<tick>": the folder in which one running Explorer2 keeps its undo copies
bool IsUndoSessionDirName(const wchar_t* name) {
    return name[0] == L's' && name[1] == L'_' && iswdigit(name[2]) != 0;
}

// "undo_<n>_<name>" directly in the staging folder: how copies were kept before each session had a folder
// of its own. Nothing writes there any more, so whatever is found is left over.
bool IsLegacyUndoCopyName(const wchar_t* name) {
    return wcsncmp(name, L"undo_", 5) == 0 && iswdigit(name[5]) != 0;
}

uint64_t FileTimeToU64(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

} // namespace

std::wstring FileEngine::GetUndoStagingDirForPath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') {
        std::wstring root = path.substr(0, 2) + L"\\";
        return root + L".explorer2_undo";
    }
#ifndef _WIN32
    if (!path.empty() && (path[0] == L'/' || path.rfind(L"\\\\?\\/", 0) == 0)) {
        std::string native = Explorer2::Compat::NativePath(path.c_str());
        struct stat stPath = {};
        std::string probe = native;
        while (probe.size() > 1 && ::lstat(probe.c_str(), &stPath) != 0) {
            size_t sl = probe.find_last_of('/');
            probe = (sl == 0 || sl == std::string::npos) ? "/" : probe.substr(0, sl);
        }
        if (::lstat(probe.c_str(), &stPath) == 0) {
            const dev_t wantDev = stPath.st_dev;
            const std::string uidTag = std::to_string(::geteuid());
            const char* home = std::getenv("HOME");
            if (home && *home) {
                struct stat stHome = {};
                if (::stat(home, &stHome) == 0 && stHome.st_dev == wantDev && ::access(home, W_OK) == 0) {
                    std::string cacheBase = std::string(home) + "/.cache";
                    ::mkdir(cacheBase.c_str(), 0700);
                    return PathUtils::Utf8ToWide(cacheBase + "/.explorer2_undo");
                }
            }
            struct stat stTmp = {};
            if (::stat("/tmp", &stTmp) == 0 && stTmp.st_dev == wantDev && ::access("/tmp", W_OK) == 0) {
                return PathUtils::Utf8ToWide("/tmp/.explorer2_undo_" + uidTag);
            }
            size_t sl = native.find_last_of('/');
            std::string parent = (sl == 0 || sl == std::string::npos) ? "/" : native.substr(0, sl);
            if (::access(parent.c_str(), W_OK) == 0) {
                return PathUtils::Utf8ToWide((parent == "/" ? "" : parent) + "/.explorer2_undo");
            }
        }
    }
#endif
    return L"";
}

// Returns this session's folder inside one staging folder, creating both on first use.
// Returns "" when undo copies cannot be kept there; the delete itself is not affected by that.
std::wstring FileEngine::EnsureUndoSessionDir(const std::wstring& stagingRoot) {
    if (stagingRoot.empty()) return L"";

    std::wstring sessionDir;
    {
        std::lock_guard<std::mutex> lock(m_undoMutex);
        auto it = m_undoSessions.find(stagingRoot);
        if (it != m_undoSessions.end()) {
            const DWORD attrs = GetFileAttributesW(it->second.dir.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return it->second.dir;
            // The folder was removed behind our back: start a new one
            if (it->second.lock != INVALID_HANDLE_VALUE) CloseHandle(it->second.lock);
            m_undoSessions.erase(it);
        }

        const DWORD rootAttrs = GetFileAttributesW(stagingRoot.c_str());
        if (rootAttrs == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryW(stagingRoot.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return L"";
            SetFileAttributesW(stagingRoot.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        } else if (!(rootAttrs & FILE_ATTRIBUTE_DIRECTORY) || (rootAttrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return L"";   // not a plain folder: nothing is ever written through a link
        }

        wchar_t leaf[64] = {};
        swprintf_s(leaf, L"\\s_%lu_%llx", GetCurrentProcessId(), static_cast<unsigned long long>(GetTickCount64()));
        UndoSession session;
        session.dir = stagingRoot + leaf;
        if (!CreateDirectoryW(session.dir.c_str(), nullptr)) return L"";

        // The lock file is how a later start tells a live session from an abandoned one: nobody else can
        // delete it while this handle is open, and Windows removes it when the process ends, however it ends.
        session.lock = CreateFileW((session.dir + L"\\" + kUndoLockName).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (session.lock == INVALID_HANDLE_VALUE) {
            // Without a lock a later start would take these copies for leftovers, so keep none here
            RemoveDirectoryW(session.dir.c_str());
            return L"";
        }
#ifndef _WIN32
        // Linux has no share modes: any process may delete an open file, and nothing removes the lock file
        // of a process that crashed. The lock file is held with flock() for the whole session instead. The
        // kernel drops that lock when the process ends, however it ends, and a sweep that cannot take it
        // knows the session is alive (SweepStaleUndoStaging).
        const int lockFd = Explorer2::Compat::FdOf(session.lock);
        if (lockFd < 0 || ::flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
            CloseHandle(session.lock);              // removes the lock file (FILE_FLAG_DELETE_ON_CLOSE)
            RemoveDirectoryW(session.dir.c_str());
            return L"";
        }
        // A program started from here must not inherit the descriptor and keep the session "alive"
        (void)::fcntl(lockFd, F_SETFD, ::fcntl(lockFd, F_GETFD) | FD_CLOEXEC);
#endif
        sessionDir = session.dir;
        m_undoSessions.emplace(stagingRoot, std::move(session));
    }

    QueueUndoSweep(stagingRoot);   // first use of this drive in this run: clear what earlier sessions left on it
    return sessionDir;
}

void FileEngine::QueueUndoSweep(const std::wstring& stagingRoot) {
    if (stagingRoot.empty() || !m_running) return;
    {
        std::lock_guard<std::mutex> lock(m_undoMutex);
        if (!m_undoSweepRequested.insert(stagingRoot).second) return;   // once per folder and run
        m_undoSweepQueue.push_back(stagingRoot);
        if (!m_undoSweepThread.joinable()) {
            m_undoSweepThread = std::thread(&FileEngine::UndoSweepWorkerLoop, this);
        }
    }
    m_undoSweepCv.notify_one();
}

void FileEngine::StartUndoStagingSweep() {
#ifndef _WIN32
    // Linux has one "drive", "/", but GetUndoStagingDirForPath picks the staging folder by the file system
    // a path is on: ~/.cache for the one that holds the home folder, /tmp for the one that holds /tmp. Asking
    // for "/" would give the /tmp folder whenever /home is a separate file system, and the folder that holds
    // the copies of everything deleted in the home folder would never be swept. So both are queued here; a
    // staging folder on any other file system is swept the first time it is used, like a removable drive.
    const char* home = std::getenv("HOME");
    if (home && *home) QueueUndoSweep(GetUndoStagingDirForPath(PathUtils::Utf8ToWide(home)));
    QueueUndoSweep(GetUndoStagingDirForPath(L"/tmp"));
#else
    wchar_t drives[512] = {};
    const DWORD len = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives) - 1), drives);
    if (len == 0 || len >= std::size(drives)) return;
    for (const wchar_t* d = drives; *d; d += wcslen(d) + 1) {
        // Only disks that are always there. A removable or network drive is swept when it is first used,
        // so that starting the app never waits for a drive that is asleep or missing.
        if (GetDriveTypeW(d) == DRIVE_FIXED) QueueUndoSweep(GetUndoStagingDirForPath(d));
    }
#endif
}

void FileEngine::UndoSweepWorkerLoop() {
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);
    while (m_running) {
        std::wstring root;
        std::wstring ownDir;
        {
            std::unique_lock<std::mutex> lock(m_undoMutex);
            m_undoSweepCv.wait(lock, [this]() { return !m_running || !m_undoSweepQueue.empty(); });
            if (!m_running) break;
            root = std::move(m_undoSweepQueue.front());
            m_undoSweepQueue.pop_front();
            auto it = m_undoSessions.find(root);
            if (it != m_undoSessions.end()) ownDir = it->second.dir;
        }
        const size_t removed = SweepStaleUndoStaging(root, ownDir, 15, &m_running);
        if (removed > 0) {
            AppLog::Info(L"Delete", L"Removed " + std::to_wstring(removed) +
                                    L" leftover undo item(s) of earlier sessions from " + root);
        }
    }
}

size_t FileEngine::SweepStaleUndoStaging(const std::wstring& stagingRoot, const std::wstring& ownSessionDir,
                                         unsigned minAgeSeconds, const std::atomic<bool>* keepRunning) {
    const DWORD rootAttrs = GetFileAttributesW(stagingRoot.c_str());
    if (rootAttrs == INVALID_FILE_ATTRIBUTES || !(rootAttrs & FILE_ATTRIBUTE_DIRECTORY) ||
        (rootAttrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return 0;   // absent, or not a plain folder: never clean up through a link
    }

    struct Candidate {
        std::wstring path;
        bool isSession = false;
    };
    std::vector<Candidate> candidates;
    FILETIME nowFt = {};
    GetSystemTimeAsFileTime(&nowFt);
    const uint64_t now = FileTimeToU64(nowFt);
    const uint64_t minAge = static_cast<uint64_t>(minAgeSeconds) * 10000000ULL;

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileExW((stagingRoot + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;   // a link is never ours
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::wstring full = stagingRoot + L"\\" + fd.cFileName;
        if (isDir && IsUndoSessionDirName(fd.cFileName)) {
            if (!ownSessionDir.empty() && lstrcmpiW(full.c_str(), ownSessionDir.c_str()) == 0) continue;
            const uint64_t created = FileTimeToU64(fd.ftCreationTime);
            if (now >= created && (now - created) < minAge) continue;       // may be a session that is just starting
            candidates.push_back({ std::move(full), true });
        } else if (!isDir && IsLegacyUndoCopyName(fd.cFileName)) {
            candidates.push_back({ std::move(full), false });
        }
        // Anything else was not put here by Explorer2 and stays
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    size_t removed = 0;
    for (const auto& c : candidates) {
        if (keepRunning && !keepRunning->load()) break;
        if (c.isSession) {
#ifndef _WIN32
            // A running session holds an flock() on its lock file (EnsureUndoSessionDir). Taking that lock
            // here proves its owner has ended; it is kept while the folder is removed. A lock that cannot be
            // taken (EWOULDBLOCK: a running Explorer2), or any other failure, means the session stays, as does
            // a lock file this process holds open without delete sharing (opened the Windows way). No lock
            // file at all means the owner ended before it could make one.
            const std::string lockNative = Explorer2::Compat::NativePath((c.path + L"\\" + kUndoLockName).c_str());
            const int lockFd = ::open(lockNative.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            if (lockFd < 0) {
                if (errno != ENOENT) continue;
            } else {
                struct stat lockSt = {};
                const bool heldHere = ::fstat(lockFd, &lockSt) == 0 &&
                    Explorer2::Compat::IsDeleteBlockedByOpenHandle(static_cast<uint64_t>(lockSt.st_dev),
                                                                   static_cast<uint64_t>(lockSt.st_ino));
                if (heldHere || ::flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
                    ::close(lockFd);
                    continue;
                }
                const bool sessionRemoved = DeletePathPermanently(c.path);
                ::close(lockFd);
                if (sessionRemoved) ++removed;
                continue;
            }
#else
            // Abandoned means: the lock file is gone (its owner ended) or can be deleted (nobody holds it).
            // Any other answer, such as "in use" or "access denied", is treated as a live session.
            const std::wstring lockPath = c.path + L"\\" + kUndoLockName;
            if (GetFileAttributesW(lockPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                if (!DeleteFileW(lockPath.c_str())) continue;
            } else {
                const DWORD err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) continue;
            }
#endif
        }
        if (DeletePathPermanently(c.path)) ++removed;
    }

    RemoveDirectoryW(stagingRoot.c_str());   // goes only once nothing at all is left in it
    return removed;
}

void FileEngine::BeginDelete(const std::vector<std::wstring>& paths, bool permanent) {
    if (paths.empty()) return;

    if (!permanent) {
        UndoDeleteBatch batch;
        static std::atomic<uint64_t> s_undoCounter{ 1 };
        std::wstring lastRoot;
        std::wstring lastSessionDir;

        for (const auto& origPath : paths) {
            DWORD attrs = GetFileAttributesW(origPath.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                const std::wstring stagingRoot = GetUndoStagingDirForPath(origPath);
                if (stagingRoot.empty()) continue;
                if (stagingRoot != lastRoot) {
                    lastRoot = stagingRoot;
                    lastSessionDir = EnsureUndoSessionDir(stagingRoot);
                }
                if (lastSessionDir.empty()) continue;   // no undo copies on this drive; the delete goes ahead anyway

                uint64_t id = s_undoCounter.fetch_add(1);
                std::wstring baseName = PathUtils::GetFileName(origPath);
                std::wstring backupPath = lastSessionDir + L"\\undo_" + std::to_wstring(id) + L"_" + baseName;
                if (CreateHardLinkW(backupPath.c_str(), origPath.c_str(), nullptr)) {
                    batch.originalToTrashPaths.push_back({ origPath, backupPath });
                }
            } else if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                // A folder: it goes to the Recycle Bin whole, and undoing takes it back out of there
                batch.recycledFolders.push_back(PathUtils::TrimTrailingSlashes(origPath));
            }
        }

        if (!batch.originalToTrashPaths.empty() || !batch.recycledFolders.empty()) {
            std::lock_guard<std::mutex> lock(m_undoMutex);
            m_undoStack.push_back(std::move(batch));
            if (m_undoStack.size() > 64) {
                for (const auto& pair : m_undoStack.front().originalToTrashPaths) {
                    DeletePathPermanently(pair.second);
                }
                m_undoStack.erase(m_undoStack.begin());
            }
        }
    }

    // Hide the items from listings until the delete job has really removed them
    std::lock_guard<std::mutex> stateLock(m_deleteStateMutex);
    for (const auto& p : paths) m_pendingDeleteKeys.insert(PathUtils::NormalizePathKey(p));
}

void FileEngine::EndDelete(const std::vector<std::wstring>& paths) {
    std::lock_guard<std::mutex> stateLock(m_deleteStateMutex);
    for (const auto& p : paths) m_pendingDeleteKeys.erase(PathUtils::NormalizePathKey(p));
}

void FileEngine::AddPendingHiddenPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_deleteStateMutex);
    m_pendingDeleteKeys.insert(PathUtils::NormalizePathKey(path));
}

void FileEngine::RemovePendingHiddenPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_deleteStateMutex);
    m_pendingDeleteKeys.erase(PathUtils::NormalizePathKey(path));
}

bool FileEngine::UndoLastDelete(std::wstring& outRestoredDir) {
    UndoDeleteBatch batch;
    {
        std::lock_guard<std::mutex> lock(m_undoMutex);
        if (m_undoStack.empty()) return false;
        batch = std::move(m_undoStack.back());
        m_undoStack.pop_back();
    }

    // Two names of one and the same file on disk (hard links)?
    auto isSameFile = [](const std::wstring& a, const std::wstring& b) {
        BY_HANDLE_FILE_INFORMATION ia = {}, ib = {};
        bool same = false;
        HANDLE ha = CreateFileW(a.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        HANDLE hb = CreateFileW(b.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE &&
            GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib)) {
            same = ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
                   ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
        }
        if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
        if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
        return same;
    };

    bool anyRestored = false;
    for (const auto& pair : batch.originalToTrashPaths) {
        const std::wstring& origPath = pair.first;
        const std::wstring& backupPath = pair.second;

        // Something may be in the old place by now. It is never replaced. If it is the very file that was to
        // be deleted (the delete did not go through), only the spare name is dropped; if it is a different
        // file, the deleted one comes back next to it as "name (restored).ext".
        bool restored = MoveFileExW(backupPath.c_str(), origPath.c_str(), MOVEFILE_COPY_ALLOWED) != FALSE;
        if (!restored) {
            const DWORD err = GetLastError();
            if (err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS) {
                if (isSameFile(backupPath, origPath)) {
                    restored = DeleteFileW(backupPath.c_str()) != FALSE;
                } else {
                    const size_t slash = origPath.find_last_of(L"\\/");   // Linux paths use '/'
                    size_t dot = origPath.find_last_of(L'.');
                    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot <= slash + 1)) dot = origPath.size();
                    for (int n = 1; n < 100 && !restored; ++n) {
                        const std::wstring tag = (n == 1) ? std::wstring(L" (restored)") : (L" (restored " + std::to_wstring(n) + L")");
                        const std::wstring target = origPath.substr(0, dot) + tag + origPath.substr(dot);
                        restored = MoveFileExW(backupPath.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED) != FALSE;
                        if (!restored && GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_EXISTS) break;
                    }
                }
            }
        }
        if (restored) {
            anyRestored = true;
            if (outRestoredDir.empty()) {
                outRestoredDir = PathUtils::GetParentPath(origPath);
            }
        }
    }

    // Folders: each comes back from the Recycle Bin, its newest item from that place (a folder deleted there
    // before, under the same name, stays in the bin). A folder that is there again by now is not replaced: the
    // restored one gets " (restored)" added (RestoreRecycleBinItem).
    if (!batch.recycledFolders.empty()) {
        const std::vector<FileEntry> bin = EnumerateDirectory(L"RECYCLE_BIN", true, true, Lang::EN);
        auto ticks = [](const FILETIME& ft) { return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; };
        for (const std::wstring& folder : batch.recycledFolders) {
            const std::wstring key = PathUtils::NormalizePathKey(folder);
            const FileEntry* newest = nullptr;
            for (const FileEntry& e : bin) {
                if (!e.isDirectory || PathUtils::NormalizePathKey(e.originalPath) != key) continue;
                if (!newest || ticks(e.writeTime) > ticks(newest->writeTime)) newest = &e;
            }
            std::wstring restoredPath;
            if (newest && RestoreRecycleBinItem(newest->fullPath, Lang::EN, restoredPath)) {
                anyRestored = true;
                if (outRestoredDir.empty()) outRestoredDir = PathUtils::GetParentPath(restoredPath);
            }
        }
    }
    return anyRestored;
}

// Normal exit: this session's undo copies go, together with its folder in every staging folder it used
void FileEngine::CleanupAllUndoStaging() {
    std::unordered_map<std::wstring, UndoSession> sessions;
    {
        std::lock_guard<std::mutex> lock(m_undoMutex);
        m_undoStack.clear();
        sessions.swap(m_undoSessions);
    }
    for (auto& entry : sessions) {
        UndoSession& session = entry.second;
        if (session.lock != INVALID_HANDLE_VALUE) {
            CloseHandle(session.lock);   // the lock file goes with its handle
            session.lock = INVALID_HANDLE_VALUE;
        }
        DeletePathPermanently(session.dir);
        RemoveDirectoryW(entry.first.c_str());   // succeeds only when no other session keeps copies there
    }
}

namespace {

// Extended-length form (\\?\C:\... / \\?\UNC\server\share\...) so deep trees (> MAX_PATH) and names with
// trailing dots / spaces can be deleted too.
std::wstring ToExtendedPath(const std::wstring& p) {
    if (p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    if (p.size() >= 2 && p[1] == L':') return L"\\\\?\\" + p;
    return p;
}

bool IsTransientDeleteError(DWORD e) {
    return e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED || e == ERROR_LOCK_VIOLATION || e == ERROR_DIR_NOT_EMPTY;
}

} // namespace

// Small synchronous deletes (undo staging, a source after an FTP upload) go through the same delete engine
// as the queued jobs, so there is exactly one implementation of "remove this from disk".
bool FileEngine::DeletePathPermanently(const std::wstring& path, std::vector<DeleteFailure>* failures) {
    std::vector<TransferFileError> errors;
    const bool ok = TransferManager::DeleteTreeNow(path, failures ? &errors : nullptr);
    if (failures) {
        for (const auto& e : errors) failures->push_back({ e.srcPath, e.win32Error });
    }
    return ok;
}

bool FileEngine::CopyFilesToClipboard(HWND hwnd, const std::vector<std::wstring>& paths, bool isCut) {
    if (paths.empty()) return false;

    size_t totalChars = 1;
    for (const auto& p : paths) {
        totalChars += p.size() + 1;
    }

    size_t dropBytes = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dropBytes);
    if (!hGlobal) return false;

    auto* df = static_cast<DROPFILES*>(GlobalLock(hGlobal));
    if (!df) {
        GlobalFree(hGlobal);
        return false;
    }
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;

    wchar_t* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
    for (const auto& p : paths) {
        memcpy(dst, p.c_str(), (p.size() + 1) * sizeof(wchar_t));
        dst += p.size() + 1;
    }
    *dst = L'\0';
    GlobalUnlock(hGlobal);

    UINT cfDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HGLOBAL hEffect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (hEffect) {
        DWORD* pEffect = static_cast<DWORD*>(GlobalLock(hEffect));
        if (pEffect) {
            *pEffect = isCut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
            GlobalUnlock(hEffect);
        }
    }

    if (!OpenClipboard(hwnd)) {
        GlobalFree(hGlobal);
        if (hEffect) GlobalFree(hEffect);
        return false;
    }

    EmptyClipboard();
    SetClipboardData(CF_HDROP, hGlobal);
    if (hEffect) {
        SetClipboardData(cfDropEffect, hEffect);
    }
    CloseClipboard();
    return true;
}

bool FileEngine::ReadFilesFromClipboard(HWND hwnd, std::vector<std::wstring>& outPaths, bool& outIsMove) {
    outPaths.clear();
    outIsMove = false;
    if (!OpenClipboard(hwnd)) return false;

    UINT cfDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HANDLE hEffect = GetClipboardData(cfDropEffect);
    if (hEffect) {
        DWORD* pEffect = static_cast<DWORD*>(GlobalLock(hEffect));
        if (pEffect) {
            outIsMove = ((*pEffect & DROPEFFECT_MOVE) != 0) && ((*pEffect & DROPEFFECT_COPY) == 0);
            GlobalUnlock(hEffect);
        }
    }

    HDROP hDrop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (hDrop) {
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        outPaths.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
            std::wstring path(len, L'\0');
            DragQueryFileW(hDrop, i, path.data(), len + 1);
            if (!path.empty()) outPaths.push_back(std::move(path));
        }
    }
    CloseClipboard();
    return !outPaths.empty();
}

bool FileEngine::CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
    return true;
}

// Items of one folder are passed relative to it, as Windows Explorer does; items from different folders (search
// results) relative to the desktop, the root of the shell namespace.
IDataObject* FileEngine::CreateShellDataObject(const std::vector<std::wstring>& paths) {
    std::vector<PIDLIST_ABSOLUTE> absolute;
    absolute.reserve(paths.size());
    for (const auto& p : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) absolute.push_back(pidl);
    }
    if (absolute.empty()) return nullptr;

    IDataObject* data = nullptr;
    PIDLIST_ABSOLUTE parent = ILClone(absolute[0]);
    bool oneParent = parent && ILRemoveLastID(parent);
    for (size_t i = 1; oneParent && i < absolute.size(); ++i) {
        oneParent = ILIsParent(parent, absolute[i], TRUE) != FALSE;
    }
    std::vector<PCUITEMID_CHILD> children;
    children.reserve(absolute.size());
    if (oneParent) {
        for (PIDLIST_ABSOLUTE pidl : absolute) children.push_back(ILFindLastID(pidl));
        SHCreateDataObject(parent, static_cast<UINT>(children.size()), children.data(), nullptr, IID_PPV_ARGS(&data));
    } else {
        PIDLIST_ABSOLUTE desktop = static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(sizeof(USHORT)));
        if (desktop) {
            desktop->mkid.cb = 0;   // the empty ID list is the desktop
            for (PIDLIST_ABSOLUTE pidl : absolute) children.push_back(reinterpret_cast<PCUITEMID_CHILD>(pidl));
            SHCreateDataObject(desktop, static_cast<UINT>(children.size()), children.data(), nullptr, IID_PPV_ARGS(&data));
            CoTaskMemFree(desktop);
        }
    }
    if (parent) ILFree(parent);
    for (PIDLIST_ABSOLUTE pidl : absolute) ILFree(pidl);   // the data object keeps its own copies
    return data;
}

#ifdef _WIN32
// The items are "dropped" on a handler as Windows Explorer's Send to menu does it (shell32's SHSimulateDrop, which the
// SDK no longer declares): DragEnter + Drop with the left button "held". Only the given effects are offered - never
// a move unless asked for, so no handler takes the files away.
static bool DropItemsOn(IDropTarget* target, const std::vector<std::wstring>& paths, DWORD allowedEffects) {
    IDataObject* data = FileEngine::CreateShellDataObject(paths);
    if (!data) return false;
    const POINTL pt = { 0, 0 };
    DWORD effect = allowedEffects;
    bool dropped = false;
    if (SUCCEEDED(target->DragEnter(data, MK_LBUTTON, pt, &effect)) && effect != DROPEFFECT_NONE) {
        effect = allowedEffects;
        dropped = SUCCEEDED(target->Drop(data, MK_LBUTTON, pt, &effect));
    } else {
        target->DragLeave();
    }
    data->Release();
    return dropped;
}

// The same, on the handler Windows registers for a kind of SendTo entry (the class behind ".ZFSendToTarget" etc.)
static bool DropItemsOnHandler(const CLSID& handler, const std::vector<std::wstring>& paths, DWORD allowedEffects) {
    IDropTarget* target = nullptr;
    if (FAILED(CoCreateInstance(handler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&target))) || !target) return false;
    const bool dropped = DropItemsOn(target, paths, allowedEffects);
    target->Release();
    return dropped;
}
#endif

// The handler of ".ZFSendToTarget" files ("Folder skompresowany (zip)" in the SendTo folder). This used to be
// PowerShell's Compress-Archive in a hidden window: only the first item, a file name with an apostrophe ended the
// quoted path and ran the rest as a command, and a hidden PowerShell is just what antivirus programs look for
// (forum post #47: Defender flagged v1.0.32 as Trojan:Win32/Wacatac.B!ml).
bool FileEngine::CompressWithWindowsZip(const std::vector<std::wstring>& paths) {
#ifdef _WIN32
    static const CLSID kCompressedFolderSendTo = { 0x888DCA60, 0xFC0A, 0x11CF, { 0x8F, 0x0F, 0x00, 0xC0, 0x4F, 0xD7, 0xD0, 0x62 } };
    return DropItemsOnHandler(kCompressedFolderSendTo, paths, DROPEFFECT_COPY);
#else
    (void)paths;
    return false;
#endif
}

bool FileEngine::DropOnShellItem(const std::wstring& itemPath, const std::vector<std::wstring>& paths) {
#ifdef _WIN32
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(itemPath.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) return false;
    IShellFolder* folder = nullptr;
    PCUITEMID_CHILD child = nullptr;
    IDropTarget* target = nullptr;
    if (SUCCEEDED(SHBindToParent(pidl, IID_PPV_ARGS(&folder), &child)) && folder) {
        folder->GetUIObjectOf(nullptr, 1, &child, IID_IDropTarget, nullptr, reinterpret_cast<void**>(&target));
        folder->Release();
    }
    ILFree(pidl);
    if (!target) return false;
    const bool dropped = DropItemsOn(target, paths, DROPEFFECT_COPY | DROPEFFECT_LINK);
    target->Release();
    return dropped;
#else
    (void)itemPath; (void)paths;
    return false;
#endif
}

bool FileEngine::HasMapiMailClient() {
#ifdef _WIN32
    // The mail program MAPI hands messages to: the default value of Software\Clients\Mail (Outlook, Thunderbird...).
    // Windows' own Mail app does not register there.
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        wchar_t name[256] = {};
        DWORD size = sizeof(name);
        if (RegGetValueW(root, L"SOFTWARE\\Clients\\Mail", nullptr, RRF_RT_REG_SZ, nullptr, name, &size) == ERROR_SUCCESS && name[0]) {
            return true;
        }
    }
#endif
    return false;
}

// The handler of ".MAPIMail" files ("Adresat poczty" in the SendTo folder, sendmail.dll): a new message of the mail
// program with the files attached, as Windows Explorer's "E-mail" button does
bool FileEngine::SendToMailRecipient(const std::vector<std::wstring>& paths) {
#ifdef _WIN32
    static const CLSID kMailRecipient = { 0x9E56BE60, 0xC50F, 0x11CF, { 0x9A, 0x2C, 0x00, 0xA0, 0xC9, 0x0A, 0x90, 0xCE } };
    return HasMapiMailClient() && DropItemsOnHandler(kMailRecipient, paths, DROPEFFECT_COPY);
#else
    (void)paths;
    return false;
#endif
}

std::wstring FileEngine::ResolveAddressInput(std::wstring_view rawInput, const std::wstring& currentDir) {
    std::wstring s = PathUtils::Trim(rawInput);
    if (s.size() >= 2 && ((s.front() == L'"' && s.back() == L'"') || (s.front() == L'\'' && s.back() == L'\''))) {
        s = PathUtils::Trim(std::wstring_view(s).substr(1, s.size() - 2));
    }
    if (s.empty()) return L"";

    if (s.find(L'%') != std::wstring::npos) {
        wchar_t expanded[2048] = {};
        DWORD n = ExpandEnvironmentStringsW(s.c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
        if (n > 0 && n < std::size(expanded)) {
            s = PathUtils::Trim(expanded);
        }
    }

    std::wstring clean = PathUtils::TrimTrailingSlashes(s);
#ifndef _WIN32
    if (clean == L"~" || clean.rfind(L"~/", 0) == 0 || clean.rfind(L"~\\", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            std::wstring homeW = PathUtils::TrimTrailingSlashes(PathUtils::Utf8ToWide(home));
            clean = (clean.size() == 1) ? homeW : (homeW + L"/" + clean.substr(2));
        }
    }
#endif
    std::wstring low = PathUtils::ToLower(clean);

    // 1. Recycle Bin ("Kosz") aliases — matches Windows Explorer address bar behavior
    if (low == L"kosz" ||
        low == L"recycle bin" ||
        low == L"recyclebin" ||
        low == L"recycle_bin" ||
        low == L"dustbin" ||
        low == L"trash" ||
        low == L"shell:recyclebinfolder" ||
        low == L"::{645ff040-5081-101b-9f08-00aa002f954e}") {
        return L"RECYCLE_BIN";
    }

    // 2. This PC ("Ten komputer") aliases
    if (low == L"ten komputer" ||
        low == L"this pc" ||
        low == L"this_pc" ||
        low == L"mój komputer" ||
        low == L"moj komputer" ||
        low == L"my computer" ||
        low == L"komputer" ||
        low == L"computer" ||
        low == L"shell:mycomputerfolder" ||
        low == L"::{20d04fe0-3aea-1069-a2d8-08002b30309d}") {
        return L"THIS_PC";
    }

    // 2b. Network ("Sieć") aliases: Explorer2's network place is the LAN scanner
    if (low == L"sieć" ||
        low == L"siec" ||
        low == L"network" ||
        low == L"shell:networkplacesfolder" ||
        low == L"::{f02c1a0d-be21-4350-88b0-7367fc96ef3c}") {
        return L"NETWORK";
    }

    // 3. Standard user Known Folders when typed as bare shell aliases in top bar
    auto resolveKnown = [](REFKNOWNFOLDERID rfid) -> std::wstring {
        PWSTR psz = nullptr;
        std::wstring out;
        if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, nullptr, &psz)) && psz) {
            out = PathUtils::TrimTrailingSlashes(psz);
            CoTaskMemFree(psz);
        }
        return out;
    };
    if (low == L"pulpit" || low == L"desktop" || low == L"shell:desktop") {
        std::wstring p = resolveKnown(FOLDERID_Desktop);
        if (!p.empty()) return p;
    } else if (low == L"pobrane" || low == L"downloads" || low == L"shell:downloads") {
        std::wstring p = resolveKnown(FOLDERID_Downloads);
        if (!p.empty()) return p;
    } else if (low == L"dokumenty" || low == L"documents" || low == L"moje dokumenty" || low == L"shell:personal") {
        std::wstring p = resolveKnown(FOLDERID_Documents);
        if (!p.empty()) return p;
    } else if (low == L"obrazy" || low == L"pictures" || low == L"shell:mypictures") {
        std::wstring p = resolveKnown(FOLDERID_Pictures);
        if (!p.empty()) return p;
    }

    // 4. A share's name typed while the list of a server's shares is shown
    if (IsUncServerRoot(currentDir) && clean.find_first_of(L"\\/:") == std::wstring::npos) {
        return PathUtils::TrimTrailingSlashes(currentDir) + L"\\" + clean;
    }

    // 5. Relative child path inside current local folder (e.g. ".\Kosz" or typing a subfolder name)
    if (!FtpClient::IsFtpPath(clean) && !RepoBrowser::IsRepoUrl(clean) &&
        !(clean.size() >= 2 && clean[1] == L':') &&
#ifndef _WIN32
        (clean.empty() || clean[0] != L'/') &&
#endif
        clean.rfind(L"\\\\", 0) != 0 &&
        !currentDir.empty() && !IsVirtualFolder(currentDir) && !FtpClient::IsFtpPath(currentDir) && !RepoBrowser::IsRepoUrl(currentDir))
    {
        std::wstring relPart = clean;
        if (relPart.rfind(L".\\", 0) == 0 || relPart.rfind(L"./", 0) == 0) {
            relPart = relPart.substr(2);
        }
        if (!relPart.empty()) {
            std::wstring candidate = PathUtils::EnsureTrailingSlash(currentDir) + relPart;
            wchar_t fullBuf[MAX_PATH * 4] = {};
            DWORD fn = GetFullPathNameW(candidate.c_str(), static_cast<DWORD>(std::size(fullBuf)), fullBuf, nullptr);
            std::wstring resolved = (fn > 0 && fn < std::size(fullBuf))
                ? PathUtils::TrimTrailingSlashes(fullBuf)
                : PathUtils::TrimTrailingSlashes(candidate);
            DWORD attrs = GetFileAttributesW(resolved.c_str());
            if ((attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) ||
                ArchiveExplorer::IsVirtualArchivePath(resolved)) {
                return resolved;
            }
        }
    }

    return clean;
}

bool FileEngine::ParseRecycleBinInfoBytes(const std::vector<uint8_t>& raw, RecycleBinRecord& outRecord, const std::wstring& infoPath) {
    outRecord = {};
#ifndef _WIN32
    if (raw.size() >= 12 && std::memcmp(raw.data(), "[Trash Info]", 12) == 0) {
        // <trash>/info/<name>.trashinfo: the trash folder is two levels up
        const std::string trashDir = infoPath.empty()
            ? LinuxTrash::HomeTrashDir()
            : LinuxTrash::ParentOf(LinuxTrash::ParentOf(Explorer2::Compat::NativePath(infoPath.c_str())));
        return ParseFreeDesktopTrashInfo(raw, outRecord, LinuxTrash::RelativePathBase(trashDir));
    }
#else
    (void)infoPath;
#endif
    if (raw.size() < 26) return false;

    uint64_t version = 0;
    uint64_t fileSize = 0;
    FILETIME deletedTime = {};
    std::memcpy(&version, raw.data(), sizeof(version));
    std::memcpy(&fileSize, raw.data() + 8, sizeof(fileSize));
    std::memcpy(&deletedTime, raw.data() + 16, sizeof(deletedTime));

    std::wstring orig;
    if (version == 1) {
        // Windows Vista / Windows 7 SP1 fixed 544-byte layout: 24-byte header + wchar_t[260]
#ifndef _WIN32
        const bool looksUtf32 = (raw.size() >= 28 && raw[26] == 0 && raw[27] == 0);
        if (!looksUtf32) {
            size_t maxU16 = std::min<size_t>((raw.size() - 24) / 2, 260);
            for (size_t i = 0; i < maxU16; ++i) {
                uint16_t u = static_cast<uint16_t>(raw[24 + i * 2] | (raw[24 + i * 2 + 1] << 8));
                if (u == 0) break;
                orig.push_back(static_cast<wchar_t>(u));
            }
        } else
#endif
        {
            size_t maxChars = std::min<size_t>((raw.size() - 24) / sizeof(wchar_t), 260);
            if (maxChars == 0) return false;
            std::vector<wchar_t> wbuf(maxChars, L'\0');
            std::memcpy(wbuf.data(), raw.data() + 24, maxChars * sizeof(wchar_t));
            size_t len = 0;
            while (len < maxChars && wbuf[len] != L'\0') ++len;
            orig.assign(wbuf.data(), len);
        }
    } else if (version == 2) {
        // Windows 10 / Windows 11 variable-length layout: 24-byte header + uint32_t charCount + wchar_t[charCount]
        if (raw.size() < 28) return false;
        uint32_t chars = 0;
        std::memcpy(&chars, raw.data() + 24, sizeof(chars));
#ifndef _WIN32
        if (chars > 0 && chars <= 32768 && 28 + static_cast<size_t>(chars) * sizeof(wchar_t) <= raw.size() &&
            (chars < 2 || (raw[30] == 0 && raw[31] == 0))) {
            std::vector<wchar_t> wbuf(chars, L'\0');
            std::memcpy(wbuf.data(), raw.data() + 28, static_cast<size_t>(chars) * sizeof(wchar_t));
            size_t len = 0;
            while (len < chars && wbuf[len] != L'\0') ++len;
            orig.assign(wbuf.data(), len);
        } else if (chars > 0 && chars <= 32768 && 28 + static_cast<size_t>(chars) * 2 <= raw.size()) {
            for (uint32_t i = 0; i < chars; ++i) {
                uint16_t u = static_cast<uint16_t>(raw[28 + i * 2] | (raw[28 + i * 2 + 1] << 8));
                if (u == 0) break;
                orig.push_back(static_cast<wchar_t>(u));
            }
        } else {
            return false;
        }
#else
        if (chars == 0 || chars > 32768 || 28 + static_cast<size_t>(chars) * sizeof(wchar_t) > raw.size()) {
            return false;
        }
        std::vector<wchar_t> wbuf(chars, L'\0');
        std::memcpy(wbuf.data(), raw.data() + 28, static_cast<size_t>(chars) * sizeof(wchar_t));
        size_t len = 0;
        while (len < chars && wbuf[len] != L'\0') ++len;
        orig.assign(wbuf.data(), len);
#endif
    } else {
        return false;
    }

    if (orig.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        orig = L"\\\\" + orig.substr(8);
    } else if (orig.rfind(L"\\\\?\\", 0) == 0 && orig.size() > 6 && orig[5] == L':') {
        orig.erase(0, 4);
    }
    orig = PathUtils::TrimTrailingSlashes(orig);
    if (orig.empty()) return false;

    outRecord.version = version;
    outRecord.fileSize = fileSize;
    outRecord.deletedTime = deletedTime;
    outRecord.originalPath = std::move(orig);
    return true;
}

bool FileEngine::GetRecycleBinCompanionInfoPath(const std::wstring& dataPath, std::wstring& outInfoPath) {
    outInfoPath.clear();
    std::wstring clean = PathUtils::TrimTrailingSlashes(dataPath);
    if (clean.empty()) return false;

    std::wstring parentDir = PathUtils::GetParentPath(clean);
    std::wstring leaf = PathUtils::GetFileName(clean);
#ifndef _WIN32
    {
        // An item in any trash's files/: the home trash or a drive's own
        const std::string info = LinuxTrash::InfoPathFor(Explorer2::Compat::NativePath(clean.c_str()));
        if (!leaf.empty() && !info.empty()) {
            outInfoPath = PathUtils::Utf8ToWide(info);
            return true;
        }
    }
#endif
    if (parentDir.empty() || leaf.size() < 3 || leaf[0] != L'$' || (leaf[1] != L'R' && leaf[1] != L'r')) {
        return false;
    }

    std::wstring parentNorm = PathUtils::NormalizePathKey(parentDir);
    if (parentNorm.find(L"\\$recycle.bin") == std::wstring::npos) {
        return false;
    }

    outInfoPath = PathUtils::EnsureTrailingSlash(parentDir) + L"$I" + leaf.substr(2);
    return true;
}

bool FileEngine::RestoreRecycleBinItem(
    const std::wstring& dataPath,
    Lang lang,
    std::wstring& outRestoredPath,
    DWORD* outError)
{
    outRestoredPath.clear();
    if (outError) *outError = 0;

    std::wstring infoPath;
    if (!GetRecycleBinCompanionInfoPath(dataPath, infoPath)) {
        if (outError) *outError = ERROR_INVALID_PARAMETER;
        return false;
    }

    DWORD dataAttrs = GetFileAttributesW(dataPath.c_str());
    if (dataAttrs == INVALID_FILE_ATTRIBUTES) {
        if (outError) *outError = GetLastError();
        return false;
    }
    const bool isDir = (dataAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

    std::vector<uint8_t> raw;
    RecycleBinRecord rec;
    if (!PathUtils::ReadFileBytes(infoPath, 65536, raw) || !ParseRecycleBinInfoBytes(raw, rec, infoPath) || rec.originalPath.empty()) {
        if (outError) *outError = ERROR_INVALID_DATA;
        return false;
    }

    std::wstring origParent = PathUtils::GetParentPath(rec.originalPath);
    if (!origParent.empty() && GetFileAttributesW(origParent.c_str()) == INVALID_FILE_ATTRIBUTES) {
        int shRes = SHCreateDirectoryExW(nullptr, origParent.c_str(), nullptr);
        if (shRes != ERROR_SUCCESS && shRes != ERROR_ALREADY_EXISTS && shRes != ERROR_FILE_EXISTS) {
            if (outError) *outError = static_cast<DWORD>(shRes);
            return false;
        }
    }

    std::wstring targetPath = rec.originalPath;
    if (GetFileAttributesW(targetPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring origName = PathUtils::GetFileName(rec.originalPath);
        std::wstring stem = isDir ? origName : PathUtils::GetStem(origName, false);
        std::wstring ext  = isDir ? L"" : PathUtils::GetExtension(origName);
        if (!isDir && !ext.empty() && origName.size() >= ext.size()) {
            // Preserve original extension casing
            ext = origName.substr(origName.size() - ext.size());
        }
        const std::wstring tagBase = (lang == Lang::PL) ? L" (przywrócony" : L" (restored";
        std::wstring parentSlash = PathUtils::EnsureTrailingSlash(origParent);
        bool foundSlot = false;
        for (int idx = 1; idx < 1000; ++idx) {
            std::wstring suffix = (idx == 1) ? (tagBase + L")") : (tagBase + L" " + std::to_wstring(idx) + L")");
            std::wstring candidate = parentSlash + stem + suffix + ext;
            if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
                targetPath = std::move(candidate);
                foundSlot = true;
                break;
            }
        }
        if (!foundSlot) {
            if (outError) *outError = ERROR_FILE_EXISTS;
            return false;
        }
    }

    if (!MoveFileExW(dataPath.c_str(), targetPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
        if (outError) *outError = GetLastError();
        return false;
    }

    SetFileAttributesW(infoPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(infoPath.c_str());

    outRestoredPath = std::move(targetPath);
    return true;
}

bool FileEngine::DeleteRecycleBinItemPermanently(const std::wstring& dataPath, DWORD* outError) {
    if (outError) *outError = 0;

    std::wstring infoPath;
    if (!GetRecycleBinCompanionInfoPath(dataPath, infoPath)) {
        if (outError) *outError = ERROR_INVALID_PARAMETER;
        return false;
    }

    if (GetFileAttributesW(dataPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::vector<TransferFileError> errs;
        if (!TransferManager::DeleteTreeNow(dataPath, &errs)) {
            if (outError) *outError = errs.empty() ? ERROR_ACCESS_DENIED : errs[0].win32Error;
            return false;
        }
    }

    if (GetFileAttributesW(infoPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(infoPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(infoPath.c_str())) {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
                if (outError) *outError = err;
                return false;
            }
        }
    }
    return true;
}

bool FileEngine::CreateNewFolder(const std::wstring& parentDir, const std::wstring& baseName, std::wstring& outCreatedPath) {
    if (parentDir.empty() || IsVirtualFolder(parentDir)) return false;
    std::wstring cleanParent = PathUtils::EnsureTrailingSlash(parentDir);

    for (int i = 1; i < 500; ++i) {
        std::wstring candidate = cleanParent + baseName;
        if (i > 1) {
            candidate += L" (" + std::to_wstring(i) + L")";
        }
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (CreateDirectoryW(candidate.c_str(), nullptr)) {
                outCreatedPath = candidate;
                return true;
            }
            return false;
        }
    }
    return false;
}

namespace {

bool MoveFileWithRetry(const std::wstring& srcPath, const std::wstring& dstPath, bool isDirectory, bool replaceExisting, DWORD* outError) {
    if (outError) *outError = 0;
    const std::wstring extSrc = ToExtendedPath(srcPath);
    const std::wstring extDst = ToExtendedPath(dstPath);
    // Directory moves on Windows must use flags = 0; file moves can use MOVEFILE_COPY_ALLOWED / MOVEFILE_REPLACE_EXISTING
    const DWORD flags = isDirectory ? 0 : (MOVEFILE_COPY_ALLOWED | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0));

    DWORD lastErr = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (MoveFileExW(extSrc.c_str(), extDst.c_str(), flags)) return true;
        lastErr = GetLastError();
        if (replaceExisting && !isDirectory && (lastErr == ERROR_ACCESS_DENIED || lastErr == ERROR_SHARING_VIOLATION)) {
            DWORD dstAttrs = GetFileAttributesW(extDst.c_str());
            if (dstAttrs != INVALID_FILE_ATTRIBUTES && (dstAttrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
                SetFileAttributesW(extDst.c_str(), FILE_ATTRIBUTE_NORMAL);
            }
        }
        if (lastErr == ERROR_ACCESS_DENIED) {
            DWORD srcAttrs = GetFileAttributesW(extSrc.c_str());
            if (srcAttrs != INVALID_FILE_ATTRIBUTES && (srcAttrs & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesW(extSrc.c_str(), srcAttrs & ~FILE_ATTRIBUTE_READONLY);
            }
        }
        if (attempt < 7 && (lastErr == ERROR_SHARING_VIOLATION || lastErr == ERROR_LOCK_VIOLATION || lastErr == ERROR_ACCESS_DENIED)) {
            Sleep(35 * (attempt + 1));
            continue;
        }
        break;
    }
    if (outError) *outError = lastErr;
    return false;
}

std::wstring NextFreeCollisionPath(const std::wstring& dstDir, const std::wstring& fileName) {
    std::wstring stem = fileName;
    std::wstring ext;
    size_t dot = fileName.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        stem = fileName.substr(0, dot);
        ext = fileName.substr(dot);
    }
    std::wstring base = PathUtils::EnsureTrailingSlash(dstDir);
    for (int idx = 2; idx < 10000; ++idx) {
        std::wstring cand = base + stem + L" (" + std::to_wstring(idx) + L")" + ext;
        if (GetFileAttributesW(ToExtendedPath(cand).c_str()) == INVALID_FILE_ATTRIBUTES) return cand;
    }
    return base + stem + L"_" + std::to_wstring(GetTickCount64()) + ext;
}

bool MergeDirectoriesRecursive(const std::wstring& srcDir, const std::wstring& dstDir, std::atomic<int>* progressCounter, MergeFolderResult& outResult) {
    const std::wstring extSrcDir = ToExtendedPath(srcDir);
    const std::wstring extDstDir = ToExtendedPath(dstDir);

    CreateDirectoryW(extDstDir.c_str(), nullptr);

    std::vector<WIN32_FIND_DATAW> entries;
    {
        std::wstring pattern = PathUtils::EnsureTrailingSlash(extSrcDir) + L"*";
        WIN32_FIND_DATAW ffd = {};
        HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (hFind == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND) {
                outResult.failures.push_back({ srcDir, e });
                return false;
            }
        } else {
            do {
                if (ffd.cFileName[0] == L'.' &&
                    (ffd.cFileName[1] == L'\0' || (ffd.cFileName[1] == L'.' && ffd.cFileName[2] == L'\0'))) {
                    continue;
                }
                entries.push_back(ffd);
            } while (FindNextFileW(hFind, &ffd));
            FindClose(hFind);   // close handle before moving or removing anything in srcDir
        }
    }

    bool allChildrenOk = true;
#ifndef _WIN32
    const MountFence fence(srcDir);
#endif
    for (const auto& ffd : entries) {
        const std::wstring childName = ffd.cFileName;
        const std::wstring srcChild = PathUtils::EnsureTrailingSlash(srcDir) + childName;
        const std::wstring dstChild = PathUtils::EnsureTrailingSlash(dstDir) + childName;
        const bool isSrcDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isSrcReparse = (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

#ifndef _WIN32
        // Another file system mounted in the folder (a USB disk, a network share, a bind mount) is not part
        // of it: it is neither moved nor entered (the fallback below would move its files one by one) and
        // stays where it is, reported as not merged
        if (isSrcDir && !isSrcReparse && fence.IsMountRoot(srcChild)) {
            outResult.failures.push_back({ srcChild, ERROR_NOT_SUPPORTED });
            allChildrenOk = false;
            continue;
        }
#endif

        const std::wstring extDstChild = ToExtendedPath(dstChild);
        const DWORD dstAttrs = GetFileAttributesW(extDstChild.c_str());

        if (dstAttrs == INVALID_FILE_ATTRIBUTES) {
            // Fast path: move the file or the entire subfolder tree in O(1) metadata time
            DWORD err = 0;
            if (MoveFileWithRetry(srcChild, dstChild, isSrcDir, false, &err)) {
                ++outResult.movedItems;
                if (progressCounter) ++(*progressCounter);
                continue;
            }
            if (isSrcDir && !isSrcReparse) {
                // Fallback if a handle inside prevented whole-directory move: merge contents recursively
                if (!MergeDirectoriesRecursive(srcChild, dstChild, progressCounter, outResult)) {
                    allChildrenOk = false;
                }
                continue;
            }
            outResult.failures.push_back({ srcChild, err });
            allChildrenOk = false;
            continue;
        }

        const bool isDstDir = (dstAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isSrcDir && !isSrcReparse && isDstDir) {
            if (!MergeDirectoriesRecursive(srcChild, dstChild, progressCounter, outResult)) {
                allChildrenOk = false;
            }
            continue;
        }

        if (!isSrcDir && !isDstDir) {
            // Shell cache files in source can be discarded if destination already has them
            if (_wcsicmp(childName.c_str(), L"Thumbs.db") == 0 || _wcsicmp(childName.c_str(), L"desktop.ini") == 0) {
                const std::wstring extSrcChild = ToExtendedPath(srcChild);
                SetFileAttributesW(extSrcChild.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(extSrcChild.c_str())) continue;
            }

            // Check if the two files have identical size and last-write timestamp
            WIN32_FILE_ATTRIBUTE_DATA dstData = {};
            bool identical = false;
            if (GetFileAttributesExW(extDstChild.c_str(), GetFileExInfoStandard, &dstData)) {
                identical = (dstData.nFileSizeHigh == ffd.nFileSizeHigh &&
                             dstData.nFileSizeLow == ffd.nFileSizeLow &&
                             CompareFileTime(&dstData.ftLastWriteTime, &ffd.ftLastWriteTime) == 0);
            }
            if (identical) {
                DWORD err = 0;
                if (MoveFileWithRetry(srcChild, dstChild, false, true, &err)) {
                    ++outResult.movedItems;
                    if (progressCounter) ++(*progressCounter);
                    continue;
                }
            }

            // Different files with the same name: keep both by saving as "name (2).ext" so zero data is lost
            const std::wstring freeDst = NextFreeCollisionPath(dstDir, childName);
            DWORD err = 0;
            if (MoveFileWithRetry(srcChild, freeDst, false, false, &err)) {
                ++outResult.movedItems;
                ++outResult.renamedCollisions;
                if (progressCounter) ++(*progressCounter);
                if (outResult.collisionNotes.size() < 20) {
                    outResult.collisionNotes.push_back(childName + L" -> " + PathUtils::GetFileName(freeDst));
                }
            } else {
                outResult.failures.push_back({ srcChild, err });
                allChildrenOk = false;
            }
            continue;
        }

        // File vs folder name clash or reparse point: move under a non-colliding name
        const std::wstring freeDst = NextFreeCollisionPath(dstDir, childName);
        DWORD err = 0;
        if (MoveFileWithRetry(srcChild, freeDst, isSrcDir, false, &err)) {
            ++outResult.movedItems;
            ++outResult.renamedCollisions;
            if (progressCounter) ++(*progressCounter);
        } else {
            outResult.failures.push_back({ srcChild, err });
            allChildrenOk = false;
        }
    }

    if (!allChildrenOk) return false;

    DWORD srcAttrs = GetFileAttributesW(extSrcDir.c_str());
    if (srcAttrs == INVALID_FILE_ATTRIBUTES) return true;
    if (srcAttrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) {
        SetFileAttributesW(extSrcDir.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    DWORD rmErr = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (RemoveDirectoryW(extSrcDir.c_str())) return true;
        rmErr = GetLastError();
        if (rmErr == ERROR_FILE_NOT_FOUND || rmErr == ERROR_PATH_NOT_FOUND) return true;
        if (attempt < 7 && IsTransientDeleteError(rmErr)) Sleep(40 * (attempt + 1));
        else break;
    }
    outResult.failures.push_back({ srcDir, rmErr });
    return false;
}

} // namespace

bool FileEngine::RenameFileOrFolder(const std::wstring& oldPath, const std::wstring& newName, std::wstring& outNewPath, DWORD* outError) {
    if (outError) *outError = 0;
    if (newName.empty()) { if (outError) *outError = ERROR_INVALID_NAME; return false; }
    if (FtpClient::IsFtpPath(oldPath)) {
        bool ok = FtpClient::RenameFtpItem(oldPath, newName, outNewPath, nullptr);
        if (!ok && outError) *outError = ERROR_ACCESS_DENIED;
        return ok;
    }
    std::wstring parent = PathUtils::GetParentPath(oldPath);
    if (parent.empty()) { if (outError) *outError = ERROR_BAD_PATHNAME; return false; }

    outNewPath = PathUtils::EnsureTrailingSlash(parent) + newName;
    DWORD srcAttrs = GetFileAttributesW(ToExtendedPath(oldPath).c_str());
    bool isDir = (srcAttrs != INVALID_FILE_ATTRIBUTES) && ((srcAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
    return MoveFileWithRetry(oldPath, outNewPath, isDir, false, outError);
}

void FileEngine::MergeDirectoryContents(const std::wstring& srcDir, const std::wstring& dstDir, std::atomic<int>* progressCounter, MergeFolderResult& outResult) {
    if (_wcsicmp(PathUtils::NormalizePathKey(srcDir).c_str(), PathUtils::NormalizePathKey(dstDir).c_str()) == 0) return;
    MergeDirectoriesRecursive(srcDir, dstDir, progressCounter, outResult);
    outResult.srcRemoved = (GetFileAttributesW(ToExtendedPath(srcDir).c_str()) == INVALID_FILE_ATTRIBUTES);
}

void FileEngine::OpenFileOrProperties(HWND hwnd, const std::wstring& path, bool propertiesOnly) {
    if (path == L"RECYCLE_BIN" || path == L"THIS_PC") {
        PIDLIST_ABSOLUTE pidl = nullptr;
        const int csidl = (path == L"RECYCLE_BIN") ? CSIDL_BITBUCKET : CSIDL_DRIVES;
        if (SUCCEEDED(SHGetSpecialFolderLocation(hwnd, csidl, &pidl)) && pidl) {
            SHELLEXECUTEINFOW sei = {};
            sei.cbSize = sizeof(sei);
            sei.hwnd = hwnd;
            sei.fMask = SEE_MASK_INVOKEIDLIST;
            sei.lpVerb = propertiesOnly ? L"properties" : L"open";
            sei.lpIDList = pidl;
            sei.nShow = SW_SHOWNORMAL;
            ShellExecuteExW(&sei);
            CoTaskMemFree(pidl);
        }
        return;
    }
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.hwnd = hwnd;
    sei.lpVerb = propertiesOnly ? L"properties" : L"open";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = propertiesOnly ? SEE_MASK_INVOKEIDLIST : 0;
    ShellExecuteExW(&sei);
}

void FileEngine::OpenInClassicExplorer(const std::wstring& path, bool selectItem) {
#ifndef _WIN32
    std::wstring target = path;
    if (path == L"RECYCLE_BIN") target = PathUtils::Utf8ToWide(LinuxTrash::HomeTrashDir() + "/files");
    else if (path == L"THIS_PC") target = L"/";
    else if (selectItem) {
        std::wstring par = PathUtils::GetParentPath(path);
        if (!par.empty()) target = par;
    }
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    if (path == L"RECYCLE_BIN") {
        ShellExecuteW(nullptr, L"open", L"explorer.exe", L"shell:RecycleBinFolder", nullptr, SW_SHOWNORMAL);
        return;
    }
    if (path == L"THIS_PC") {
        ShellExecuteW(nullptr, L"open", L"explorer.exe", L"shell:MyComputerFolder", nullptr, SW_SHOWNORMAL);
        return;
    }
    std::wstring params = selectItem ? (L"/select,\"" + path + L"\"") : (L"\"" + path + L"\"");
    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
#endif
}

void FileEngine::OpenTerminalInFolder(const std::wstring& dirPath) {
    if (dirPath.empty() || IsVirtualFolder(dirPath)) return;
#ifndef _WIN32
    const DWORD dirAttrs = GetFileAttributesW(dirPath.c_str());
    if (dirAttrs == INVALID_FILE_ATTRIBUTES || !(dirAttrs & FILE_ATTRIBUTE_DIRECTORY)) return;
    // The terminal is started directly, with the folder as its working directory (CreateProcessW changes
    // into lpCurrentDirectory before it starts the program). No shell is involved, so the folder's name is
    // never read as a command, whatever quotes, "$(...)" or ";" it contains.
    for (const wchar_t* term : { L"x-terminal-emulator", L"gnome-terminal", L"konsole", L"xfce4-terminal", L"xterm" }) {
        const std::wstring exe = ToolProcess::FindExecutable(term, {});
        if (exe.empty()) continue;
        std::wstring cmdLine = L"\"" + exe + L"\"";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(exe.c_str(), cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, dirPath.c_str(), &si, &pi)) {
            if (pi.hThread) CloseHandle(pi.hThread);
            if (pi.hProcess) CloseHandle(pi.hProcess);
            return;
        }
    }
#else
    ShellExecuteW(nullptr, L"open", L"powershell.exe", nullptr, dirPath.c_str(), SW_SHOWNORMAL);
#endif
}

std::wstring FileEngine::FormatSizeKB(uint64_t bytes) {
    uint64_t kb = (bytes == 0) ? 0 : ((bytes + 1023) / 1024);
    std::wstring raw = std::to_wstring(kb);
    std::wstring formatted;
    int count = 0;
    for (int i = static_cast<int>(raw.size()) - 1; i >= 0; --i) {
        if (count > 0 && (count % 3) == 0) {
            formatted.insert(formatted.begin(), L' ');
        }
        formatted.insert(formatted.begin(), raw[i]);
        ++count;
    }
    return formatted + L" KB";
}

std::wstring FileEngine::FormatSizeDetailed(uint64_t bytes) {
    wchar_t buf[64] = {};
    if (bytes >= (1ULL << 30)) {
        double gb = static_cast<double>(bytes) / static_cast<double>(1ULL << 30);
        swprintf_s(buf, L"%.2f GB", gb);
    } else if (bytes >= (1ULL << 20)) {
        double mb = static_cast<double>(bytes) / static_cast<double>(1ULL << 20);
        swprintf_s(buf, L"%.1f MB", mb);
    } else {
        return FormatSizeKB(bytes);
    }
    return buf;
}

std::wstring FileEngine::FormatFileTime(const FILETIME& ft) {
    FILETIME localFt = {};
    SYSTEMTIME st = {};
    FileTimeToLocalFileTime(&ft, &localFt);
    FileTimeToSystemTime(&localFt, &st);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%02d.%02d.%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    return buf;
}

bool FileEngine::IsAnyFolderSizeCalculating() const {
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    return !m_sizeQueue.empty() || m_activeSizeWorkers.load(std::memory_order_relaxed) > 0;
}

bool FileEngine::IsFolderInFlight(const std::wstring& normKey) const {
    if (normKey.empty()) return false;
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    return m_folderSizeInFlight.find(normKey) != m_folderSizeInFlight.end();
}

std::wstring FileEngine::GetActiveScanningSamplePath() const {
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    return m_currentScanSample;
}

bool FileEngine::HasAnyCachedFolderSizes(const std::vector<FileEntry>& entries) const {
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    for (const auto& e : entries) {
        if (e.isDirectory && m_folderSizeCache.find(e.normKey) != m_folderSizeCache.end()) {
            return true;
        }
    }
    return false;
}

#ifndef _WIN32
void FileEngine::SyncProbedMountSizes(std::vector<FileEntry>& entries) {
    // Linux network and FUSE mounts listed in Ten komputer: their size arrives from the probe thread
    std::lock_guard<std::mutex> lk(m_net->mutex);
    if (m_net->mounts.empty()) return;
    for (auto& item : entries) {
        if (!item.isDrive || item.fullPath.empty()) continue;
        auto it = m_net->mounts.find(item.fullPath);
        if (it == m_net->mounts.end() || !it->second.probed) continue;
        item.isOffline = !it->second.online;
        if (it->second.online && it->second.totalBytes > 0) {
            item.size = it->second.totalBytes;
            item.sizeStr = FormatSizeDetailed(it->second.totalBytes);
        }
    }
}
#endif

void FileEngine::SyncDirectorySizes(std::vector<FileEntry>& entries) {
    m_sizeNotifyPending.store(false, std::memory_order_relaxed);
#ifndef _WIN32
    SyncProbedMountSizes(entries);
#endif
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    for (auto& item : entries) {
        if (!item.isDirectory || item.normKey.empty()) continue;
        auto it = m_folderSizeCache.find(item.normKey);
        if (it != m_folderSizeCache.end()) {
            item.size = it->second;
            item.sizeStr = FormatSizeDetailed(it->second);
        } else if (m_folderSizeInFlight.find(item.normKey) != m_folderSizeInFlight.end()) {
            item.sizeStr = L"...";
        }
    }
}

void FileEngine::SyncTreeNodeSizes(std::vector<TreeNode>& treeNodes) {
    // 1. Update network drive capacities if background probe completed
    {
        std::lock_guard<std::mutex> lk(m_net->mutex);
        for (auto& node : treeNodes) {
            if (!node.isDrive || !node.isNetworkDrive || node.path.empty()) continue;
            wchar_t dl = static_cast<wchar_t>(towupper(node.path[0]));
            auto it = m_net->drives.find(dl);
            if (it != m_net->drives.end() && it->second.probed) {
                node.isOffline = !it->second.online;
                if (it->second.online && it->second.totalBytes > 0) {
                    node.totalBytes = it->second.totalBytes;
                    node.freeBytes = it->second.freeBytes;
                    node.size = (node.totalBytes >= node.freeBytes) ? (node.totalBytes - node.freeBytes) : 0;
                    wchar_t buf[64] = {};
                    if (node.totalBytes >= (1ULL << 40)) {
                        double uTb = static_cast<double>(node.size) / static_cast<double>(1ULL << 40);
                        double tTb = static_cast<double>(node.totalBytes) / static_cast<double>(1ULL << 40);
                        swprintf_s(buf, L"%.1f / %.1f TB", uTb, tTb);
                    } else {
                        uint64_t uGb = (node.size + (1ULL << 29)) >> 30;
                        uint64_t tGb = (node.totalBytes + (1ULL << 29)) >> 30;
                        swprintf_s(buf, L"%llu / %llu GB", static_cast<unsigned long long>(uGb), static_cast<unsigned long long>(tGb));
                    }
                    node.sizeStr = buf;
                }
            }
        }
#ifndef _WIN32
        // 1b. Linux network and FUSE mounts: their size arrives from the probe thread (ProbeLinuxMountAsync)
        if (!m_net->mounts.empty()) {
            for (auto& node : treeNodes) {
                if (!node.isDrive || node.path.empty()) continue;
                auto it = m_net->mounts.find(node.path);
                if (it == m_net->mounts.end() || !it->second.probed) continue;
                node.isOffline = !it->second.online;
                if (it->second.online && it->second.totalBytes > 0) {
                    node.totalBytes = it->second.totalBytes;
                    node.freeBytes = it->second.freeBytes;
                    node.size = (node.totalBytes >= node.freeBytes) ? (node.totalBytes - node.freeBytes) : 0;
                    node.sizeStr = LinuxDriveBadge(node.size, node.totalBytes);
                }
            }
        }
#endif
    }

    // 2. Sync folder sizes from m_folderSizeCache onto folder tree nodes
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    for (auto& node : treeNodes) {
        if (node.isHeader || node.isDrive || node.isFtpNode || node.normKey.empty()) continue;
        auto it = m_folderSizeCache.find(node.normKey);
        if (it != m_folderSizeCache.end()) {
            node.size = it->second;
            node.sizeStr = FormatSizeDetailed(it->second);
        } else if (m_folderSizeInFlight.find(node.normKey) != m_folderSizeInFlight.end()) {
            node.sizeStr = L"...";
        }
    }
}

void FileEngine::RequestTreeFolderSizes(const std::vector<TreeNode>& treeNodes, bool forceRefresh) {
    uint64_t gen = m_sizeGeneration.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_sizeMutex);
        for (const auto& node : treeNodes) {
            if (node.isHeader || node.isDrive || node.isFtpNode || node.isOffline || node.normKey.empty() || node.path.empty()) continue;
            if (IsVirtualFolder(node.path) || FtpClient::IsFtpPath(node.path) || RepoBrowser::IsRepoUrl(node.path) || ArchiveExplorer::IsVirtualArchivePath(node.path)) continue;
            // Skip massive cloud sync roots (Dropbox / OneDrive at indent 0) during automatic background tree sizing
            // so the 4 folder-size worker threads remain immediately available for active directory browsing!
            if (!forceRefresh && node.indent == 0 && !node.isQuickAccess) continue;
            if (!forceRefresh && m_folderSizeCache.find(node.normKey) != m_folderSizeCache.end()) {
                continue;
            }
            if (m_folderSizeInFlight.find(node.normKey) != m_folderSizeInFlight.end()) {
                continue;
            }
            m_folderSizeInFlight[node.normKey] = true;
            m_sizeQueue.push_back(FolderSizeTask{ node.path, node.normKey, gen });
        }
    }
    m_sizeCv.notify_all();
}

bool FileEngine::GetFolderTreemapChildren(const std::wstring& normKey, std::vector<TreemapChildNode>& outChildren) const {
    if (normKey.empty()) return false;
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    auto it = m_folderChildrenCache.find(normKey);
    if (it == m_folderChildrenCache.end() || it->second.empty()) {
        return false;
    }
    outChildren = it->second;
    return true;
}

void FileEngine::RequestFolderSizes(const std::vector<FileEntry>& entries, bool forceRefresh) {
    uint64_t gen = m_sizeGeneration.load(std::memory_order_relaxed);
    if (forceRefresh) {
        // Increment generation so any in-progress recursive scans from the previous click abort immediately!
        gen = m_sizeGeneration.fetch_add(1) + 1;
    }

    {
        std::lock_guard<std::mutex> lock(m_sizeMutex);
        if (forceRefresh) {
            m_sizeQueue.clear();
            m_folderSizeInFlight.clear();
            for (const auto& e : entries) {
                if (!e.isDirectory || e.normKey.empty()) continue;
                std::wstring prefix = e.normKey + L"\\";
                for (auto it = m_folderSizeCache.begin(); it != m_folderSizeCache.end(); ) {
                    if (it->first == e.normKey || it->first.rfind(prefix, 0) == 0) {
                        it = m_folderSizeCache.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (auto it = m_folderChildrenCache.begin(); it != m_folderChildrenCache.end(); ) {
                    if (it->first == e.normKey || it->first.rfind(prefix, 0) == 0) {
                        it = m_folderChildrenCache.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        for (const auto& e : entries) {
            if (!e.isDirectory || e.normKey.empty() || e.isDrive) continue;
            if (FtpClient::IsFtpPath(e.fullPath) || RepoBrowser::IsRepoUrl(e.fullPath) || ArchiveExplorer::IsVirtualArchivePath(e.fullPath)) continue;
            if (!forceRefresh &&
                m_folderSizeCache.find(e.normKey) != m_folderSizeCache.end() &&
                m_folderChildrenCache.find(e.normKey) != m_folderChildrenCache.end()) {
                continue;
            }
            if (m_folderSizeInFlight.find(e.normKey) != m_folderSizeInFlight.end()) {
                continue;
            }
            m_folderSizeInFlight[e.normKey] = true;
            m_sizeQueue.push_back(FolderSizeTask{ e.fullPath, e.normKey, gen });
        }
    }
    m_sizeCv.notify_all();
}

#ifndef _WIN32
namespace {
// The fence of the folder a size worker is measuring (each worker measures one folder at a time); set by
// FolderSizeWorkerLoop around ComputeFolderSizeRecursive
thread_local const MountFence* t_sizeWalkFence = nullptr;
} // namespace
#endif

uint64_t FileEngine::ComputeFolderSizeRecursive(const std::wstring& dirPath, int depth, uint64_t generation, bool& outAborted) {
    if (!m_running || generation != m_sizeGeneration.load(std::memory_order_relaxed)) {
        outAborted = true;
        return 0;
    }
    if (dirPath.empty() || IsVirtualFolder(dirPath) || FtpClient::IsFtpPath(dirPath) || RepoBrowser::IsRepoUrl(dirPath)) {
        return 0;
    }

    if (depth <= 2) {
        std::lock_guard<std::mutex> lock(m_sizeMutex);
        m_currentScanSample = dirPath;
    }

    std::wstring prefix = PathUtils::EnsureTrailingSlash(dirPath);
    std::wstring pattern = prefix + L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &ffd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }

    uint64_t totalBytes = 0;
    std::vector<TreemapChildNode> directChildren;
    if (depth <= 4) {
        directChildren.reserve(64);
    }
    std::wstring dirKey = (depth <= 4) ? PathUtils::NormalizePathKey(dirPath) : L"";

    do {
        if (!m_running || generation != m_sizeGeneration.load(std::memory_order_relaxed)) {
            outAborted = true;
            break;
        }
        const wchar_t* name = ffd.cFileName;
        if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
            continue;
        }
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;
        }

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring childDir = prefix + name;
#ifndef _WIN32
            // Another file system mounted inside (a USB disk, a network share, a bind mount) is not part of
            // this folder's size, and a share that does not answer would hold the measurement up forever
            if (t_sizeWalkFence && t_sizeWalkFence->IsMountRoot(childDir)) continue;
#endif
            uint64_t subBytes = ComputeFolderSizeRecursive(childDir, depth + 1, generation, outAborted);
            if (outAborted) break;
            totalBytes += subBytes;

            std::wstring subKey;
            if (depth <= 4) {
                subKey = PathUtils::NormalizePathKey(childDir);
                std::lock_guard<std::mutex> lock(m_sizeMutex);
                m_folderSizeCache[subKey] = subBytes;
                if (depth == 0) {
                    // Stream partial size of top-level folder while scanning its subfolders!
                    m_folderSizeCache[dirKey] = totalBytes;
                }
            }
            if (depth <= 4 && subBytes > 0) {
                TreemapChildNode cn;
                cn.name = name;
                cn.fullPath = std::move(childDir);
                cn.normKey = std::move(subKey);
                cn.size = subBytes;
                cn.isDirectory = true;
                directChildren.push_back(std::move(cn));

                // Periodically publish partial treemap children for depth 0/1 so huge folders populate live!
                if (depth <= 1) {
                    uint64_t nowTick = GetTickCount64();
                    uint64_t prevTick = m_lastPartialNotifyTick.load(std::memory_order_relaxed);
                    if (nowTick - prevTick >= 150 &&
                        m_lastPartialNotifyTick.compare_exchange_strong(prevTick, nowTick, std::memory_order_relaxed)) {
                        std::vector<TreemapChildNode> snapshot = directChildren;
                        std::sort(snapshot.begin(), snapshot.end(),
                            [](const TreemapChildNode& a, const TreemapChildNode& b) { return a.size > b.size; });
                        if (snapshot.size() > 48) snapshot.resize(48);
                        {
                            std::lock_guard<std::mutex> lock(m_sizeMutex);
                            m_folderChildrenCache[dirKey] = std::move(snapshot);
                        }
                        if (m_notifyHwnd && !m_sizeNotifyPending.exchange(true, std::memory_order_relaxed)) {
                            PostMessageW(m_notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
                        }
                    }
                }
            }
        } else {
            uint64_t fsize = (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow;
            totalBytes += fsize;
            if (depth <= 4 && fsize > 0) {
                TreemapChildNode cn;
                cn.name = name;
                cn.fullPath = prefix + name;
                const wchar_t* dot = wcsrchr(name, L'.');
                if (dot) cn.ext = PathUtils::ToLower(dot);
                cn.size = fsize;
                cn.isDirectory = false;
                directChildren.push_back(std::move(cn));
            }
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);

    if (!outAborted && depth <= 4 && !directChildren.empty()) {
        std::sort(directChildren.begin(), directChildren.end(),
            [](const TreemapChildNode& a, const TreemapChildNode& b) { return a.size > b.size; });

        constexpr size_t kMaxTreemapChildren = 52;
        if (directChildren.size() > kMaxTreemapChildren) {
            uint64_t restBytes = 0;
            size_t restCount = directChildren.size() - (kMaxTreemapChildren - 1);
            for (size_t i = kMaxTreemapChildren - 1; i < directChildren.size(); ++i) {
                restBytes += directChildren[i].size;
            }
            directChildren.resize(kMaxTreemapChildren - 1);
            if (restBytes > 0) {
                TreemapChildNode otherNode;
                otherNode.name = L"[+" + std::to_wstring(restCount) + L" mniejszych]";
                otherNode.fullPath = dirPath;
                otherNode.ext = L".other";
                otherNode.size = restBytes;
                otherNode.isDirectory = false;
                directChildren.push_back(std::move(otherNode));
            }
        }

        std::lock_guard<std::mutex> lock(m_sizeMutex);
        m_folderChildrenCache[dirKey] = std::move(directChildren);
    }

    return totalBytes;
}

void FileEngine::FolderSizeWorkerLoop() {
    while (m_running) {
        FolderSizeTask task;
        {
            std::unique_lock<std::mutex> lock(m_sizeMutex);
            m_sizeCv.wait(lock, [this]() { return !m_running || !m_sizeQueue.empty(); });
            if (!m_running) break;

            task = std::move(m_sizeQueue.front());
            m_sizeQueue.pop_front();
            m_activeSizeWorkers.fetch_add(1, std::memory_order_relaxed);
        }

        bool aborted = false;
#ifndef _WIN32
        const MountFence fence(task.path);
        t_sizeWalkFence = &fence;
#endif
        uint64_t totalBytes = ComputeFolderSizeRecursive(task.path, 0, task.generation, aborted);
#ifndef _WIN32
        t_sizeWalkFence = nullptr;
#endif

        if (m_running && !aborted && task.generation == m_sizeGeneration.load(std::memory_order_relaxed)) {
            {
                std::lock_guard<std::mutex> lock(m_sizeMutex);
                m_folderSizeCache[task.normKey] = totalBytes;
                m_folderSizeInFlight.erase(task.normKey);
            }
            if (m_notifyHwnd && !m_sizeNotifyPending.exchange(true, std::memory_order_relaxed)) {
                PostMessageW(m_notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
            }
        }
        if (m_activeSizeWorkers.fetch_sub(1, std::memory_order_relaxed) == 1 && m_notifyHwnd) {
            // Ensure final repaint when last active worker finishes
            PostMessageW(m_notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
        }
    }
}

namespace {

bool FastSubstrMatch(const wchar_t* hay, const std::wstring& needle, bool caseSensitive) {
    if (needle.empty()) return true;
    size_t nLen = needle.size();
    const wchar_t* nPtr = needle.c_str();
    if (caseSensitive) {
        return wcsstr(hay, nPtr) != nullptr;
    }
    wchar_t firstLo = static_cast<wchar_t>(towlower(nPtr[0]));
    for (const wchar_t* p = hay; *p != L'\0'; ++p) {
        if (static_cast<wchar_t>(towlower(*p)) == firstLo) {
            bool match = true;
            for (size_t i = 1; i < nLen; ++i) {
                wchar_t hc = p[i];
                if (hc == L'\0' || static_cast<wchar_t>(towlower(hc)) != nPtr[i]) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}

uint32_t ClassifyExtCategory(const std::wstring& extLo) {
    if (extLo.empty()) return SEARCH_CAT_NONE;
    if (IsImageExtension(extLo)) return SEARCH_CAT_IMAGES;
    if (IsVideoExtension(extLo) || IsAudioExtension(extLo)) return SEARCH_CAT_MEDIA;
    if (extLo == L".cs" || extLo == L".cpp" || extLo == L".h" || extLo == L".hpp" || extLo == L".c" ||
        extLo == L".py" || extLo == L".js" || extLo == L".ts" || extLo == L".json" || extLo == L".xml" ||
        extLo == L".html" || extLo == L".css" || extLo == L".md" || extLo == L".txt" || extLo == L".ini" ||
        extLo == L".yaml" || extLo == L".yml" || extLo == L".shader" || extLo == L".hlsl" || extLo == L".glsl" ||
        extLo == L".sql" || extLo == L".log" || extLo == L".cmake" || extLo == L".sln" || extLo == L".vcxproj") {
        return SEARCH_CAT_CODE;
    }
    if (extLo == L".zip" || extLo == L".7z" || extLo == L".rar" || extLo == L".tar" ||
        extLo == L".gz" || extLo == L".iso" || extLo == L".cab" || extLo == L".tgz") {
        return SEARCH_CAT_ARCHIVES;
    }
    if (extLo == L".safetensors" || extLo == L".ckpt" || extLo == L".onnx" || extLo == L".pt" ||
        extLo == L".pth" || extLo == L".gguf" || extLo == L".bin" || extLo == L".db" || extLo == L".sqlite" ||
        extLo == L".fbx" || extLo == L".obj" || extLo == L".blend" || extLo == L".gltf" || extLo == L".glb") {
        return SEARCH_CAT_MODELS;
    }
    if (extLo == L".exe" || extLo == L".dll" || extLo == L".msi" || extLo == L".bat" ||
        extLo == L".cmd" || extLo == L".ps1" || extLo == L".sys") {
        return SEARCH_CAT_APPS;
    }
    if (extLo == L".pdf" || extLo == L".doc" || extLo == L".docx" || extLo == L".xls" ||
        extLo == L".xlsx" || extLo == L".ppt" || extLo == L".pptx" || extLo == L".rtf" || extLo == L".csv") {
        return SEARCH_CAT_DOCS;
    }
    return SEARCH_CAT_NONE;
}

bool FileContentContainsText(const std::wstring& filePath, const std::wstring& needleLower) {
    if (needleLower.empty()) return true;
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    // Convert needleLower to UTF-8 using WideCharToMultiByte so Polish/Unicode characters work properly
    std::string utf8Needle;
    int u8Len = WideCharToMultiByte(CP_UTF8, 0, needleLower.c_str(), static_cast<int>(needleLower.size()), nullptr, 0, nullptr, nullptr);
    if (u8Len > 0) {
        utf8Needle.resize(static_cast<size_t>(u8Len));
        WideCharToMultiByte(CP_UTF8, 0, needleLower.c_str(), static_cast<int>(needleLower.size()), utf8Needle.data(), u8Len, nullptr, nullptr);
        for (char& c : utf8Needle) {
            if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
        }
    }

    // Fast path for PNG AI Prompt / Metadata chunks (tEXt / iTXt)
    std::wstring ext = PathUtils::GetExtension(filePath);
    if (ext == L".png") {
        uint8_t sig[8] = {};
        DWORD got = 0;
        if (ReadFile(hFile, sig, 8, &got, nullptr) && got == 8 &&
            sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G')
        {
            bool foundInPng = false;
            for (int cIdx = 0; cIdx < 64; ++cIdx) {
                uint8_t hdr[8] = {};
                if (!ReadFile(hFile, hdr, 8, &got, nullptr) || got != 8) break;
                uint32_t len = (static_cast<uint32_t>(hdr[0]) << 24) |
                               (static_cast<uint32_t>(hdr[1]) << 16) |
                               (static_cast<uint32_t>(hdr[2]) << 8) |
                               static_cast<uint32_t>(hdr[3]);
                char type[5] = { static_cast<char>(hdr[4]), static_cast<char>(hdr[5]), static_cast<char>(hdr[6]), static_cast<char>(hdr[7]), '\0' };
                if (std::strcmp(type, "IEND") == 0) break;
                if ((std::strcmp(type, "tEXt") == 0 || std::strcmp(type, "iTXt") == 0) && len > 0 && len <= 1048576) {
                    std::vector<char> cbuf(len);
                    if (!ReadFile(hFile, cbuf.data(), len, &got, nullptr) || got != len) break;
                    for (char& ch : cbuf) {
                        if (ch >= 'A' && ch <= 'Z') ch += ('a' - 'A');
                    }
                    if (!utf8Needle.empty() && cbuf.size() >= utf8Needle.size()) {
                        if (std::search(cbuf.begin(), cbuf.end(), utf8Needle.begin(), utf8Needle.end()) != cbuf.end()) {
                            foundInPng = true;
                            break;
                        }
                    }
                    LARGE_INTEGER skipCrc = {};
                    skipCrc.QuadPart = 4;
                    SetFilePointerEx(hFile, skipCrc, nullptr, FILE_CURRENT);
                } else {
                    LARGE_INTEGER skip = {};
                    skip.QuadPart = static_cast<LONGLONG>(len) + 4;
                    if (!SetFilePointerEx(hFile, skip, nullptr, FILE_CURRENT)) break;
                }
            }
            CloseHandle(hFile);
            return foundInPng;
        }
        LARGE_INTEGER zero = {};
        SetFilePointerEx(hFile, zero, nullptr, FILE_BEGIN);
    }

    constexpr size_t kChunkSize = 32768;
    size_t maxOverlap = std::max(utf8Needle.size(), needleLower.size() * sizeof(wchar_t));
    if (maxOverlap > 512) maxOverlap = 512;

    std::vector<char> windowBuf;
    windowBuf.reserve(kChunkSize + maxOverlap);
    char readBuf[kChunkSize];
    DWORD bytesRead = 0;
    DWORD totalRead = 0;
    constexpr DWORD kMaxScanBytes = 4 * 1024 * 1024; // Up to 4 MB per text file
    bool found = false;

    while (totalRead < kMaxScanBytes && ReadFile(hFile, readBuf, sizeof(readBuf), &bytesRead, nullptr) && bytesRead > 0) {
        totalRead += bytesRead;
        windowBuf.insert(windowBuf.end(), readBuf, readBuf + bytesRead);

        // 1. Check UTF-8 / ASCII match
        if (!utf8Needle.empty() && windowBuf.size() >= utf8Needle.size()) {
            auto it = std::search(windowBuf.begin(), windowBuf.end(), utf8Needle.begin(), utf8Needle.end(),
                [](char a, char b) {
                    if (a >= 'A' && a <= 'Z') a += ('a' - 'A');
                    return a == b;
                });
            if (it != windowBuf.end()) {
                found = true;
                break;
            }
        }

        // 2. Check UTF-16LE match
#ifndef _WIN32
        size_t u16Count = windowBuf.size() / sizeof(uint16_t);
        if (u16Count >= needleLower.size()) {
            const auto* rawBytes = reinterpret_cast<const uint8_t*>(windowBuf.data());
            for (size_t pos = 0; pos + needleLower.size() <= u16Count; ++pos) {
                bool match = true;
                for (size_t k = 0; k < needleLower.size(); ++k) {
                    uint16_t cu = static_cast<uint16_t>(rawBytes[(pos + k) * 2] | (rawBytes[(pos + k) * 2 + 1] << 8));
                    if (static_cast<wchar_t>(towlower(static_cast<wint_t>(cu))) != needleLower[k]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
#endif
        size_t wCount = windowBuf.size() / sizeof(wchar_t);
        if (wCount >= needleLower.size()) {
            const wchar_t* wPtr = reinterpret_cast<const wchar_t*>(windowBuf.data());
            auto wit = std::search(wPtr, wPtr + wCount, needleLower.begin(), needleLower.end(),
                [](wchar_t a, wchar_t b) {
                    return static_cast<wchar_t>(towlower(a)) == b;
                });
            if (wit != wPtr + wCount) {
                found = true;
                break;
            }
        }

        // Keep overlap tail for next chunk boundary
        if (windowBuf.size() > maxOverlap) {
            windowBuf.erase(windowBuf.begin(), windowBuf.end() - static_cast<ptrdiff_t>(maxOverlap));
        }
    }
    CloseHandle(hFile);
    return found;
}

} // namespace

void FileEngine::StartAsyncSearch(
    const std::wstring& rootDir,
    const SearchOptions& opts,
    bool showHidden,
    bool showExtensions,
    Lang lang)
{
    m_searchGeneration.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_searchRootDir = rootDir;
        m_searchOpts = opts;
        m_searchShowHidden = showHidden;
        m_searchShowExtensions = showExtensions;
        m_searchLang = lang;
        m_hasPendingSearchJob = true;
        m_searchAccumResults.clear();
        m_searchProgress = SearchProgressInfo{};
        m_searchProgress.isRunning = true;
        m_searchProgress.currentFolder = rootDir;
        m_searchRunning.store(true, std::memory_order_relaxed);
    }
    m_searchCv.notify_one();
}

void FileEngine::CancelAsyncSearch() {
    m_searchGeneration.fetch_add(1);
    m_searchRunning.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_searchMutex);
    m_hasPendingSearchJob = false;
    m_searchProgress.isRunning = false;
}

bool FileEngine::IsSearchRunning() const {
    return m_searchRunning.load(std::memory_order_relaxed);
}

void FileEngine::SyncSearchResults(std::vector<FileEntry>& outResults, SearchProgressInfo& outProgress) {
    m_searchNotifyPending.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        outResults = m_searchAccumResults;
        outProgress = m_searchProgress;
    }
    RefreshIconIndices(outResults);     // results found before their type's icon was known
}

void FileEngine::SearchWorkerLoop() {
    while (m_running) {
        std::wstring rootDir;
        SearchOptions opts;
        bool showHidden = true;
        bool showExtensions = true;
        Lang lang = Lang::PL;
        uint64_t myGen = 0;

        {
            std::unique_lock<std::mutex> lock(m_searchMutex);
            m_searchCv.wait(lock, [this]() { return !m_running || m_hasPendingSearchJob; });
            if (!m_running) break;

            m_hasPendingSearchJob = false;
            rootDir = m_searchRootDir;
            opts = m_searchOpts;
            showHidden = m_searchShowHidden;
            showExtensions = m_searchShowExtensions;
            lang = m_searchLang;
            myGen = m_searchGeneration.load(std::memory_order_relaxed);
        }

        if (rootDir.empty() || IsVirtualFolder(rootDir) || FtpClient::IsFtpPath(rootDir) || RepoBrowser::IsRepoUrl(rootDir)) {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            m_searchProgress.isRunning = false;
            m_searchProgress.isFinished = true;
            m_searchRunning.store(false, std::memory_order_relaxed);
            if (m_notifyHwnd) PostMessageW(m_notifyHwnd, WM_APP_SEARCH_UPDATED, 0, 0);
            continue;
        }

        ULONGLONG startTick = GetTickCount64();
        ULONGLONG lastNotifyTick = startTick;

        std::wstring needle = opts.caseSensitive ? opts.query : PathUtils::ToLower(opts.query);
        bool hasWildcard = (needle.find(L'*') != std::wstring::npos || needle.find(L'?') != std::wstring::npos);

        // Parse custom extension list (e.g. ".cs;.cpp;png")
        std::vector<std::wstring> customExts;
        if (opts.useTypeFilter && !opts.customExtList.empty()) {
            std::wstring cur;
            for (wchar_t ch : opts.customExtList) {
                if (ch == L';' || ch == L',' || ch == L' ') {
                    if (!cur.empty()) {
                        if (cur[0] != L'.') cur.insert(cur.begin(), L'.');
                        customExts.push_back(PathUtils::ToLower(cur));
                        cur.clear();
                    }
                } else if (ch != L'*') {
                    cur.push_back(ch);
                }
            }
            if (!cur.empty()) {
                if (cur[0] != L'.') cur.insert(cur.begin(), L'.');
                customExts.push_back(PathUtils::ToLower(cur));
            }
        }

        ULARGE_INTEGER ftMin = {}, ftMax = {};
        ftMin.LowPart = opts.dateFromFt.dwLowDateTime;
        ftMin.HighPart = opts.dateFromFt.dwHighDateTime;
        ftMax.LowPart = opts.dateToFt.dwLowDateTime;
        ftMax.HighPart = opts.dateToFt.dwHighDateTime;

        std::wstring rootClean = PathUtils::EnsureTrailingSlash(PathUtils::TrimTrailingSlashes(rootDir));
        std::deque<std::wstring> dirQueue;
        dirQueue.push_back(PathUtils::TrimTrailingSlashes(rootDir));

        uint64_t scannedFolders = 0;
        uint64_t scannedFiles = 0;
        uint64_t matchedCount = 0;
        std::vector<FileEntry> batchMatches;
        batchMatches.reserve(128);

        while (!dirQueue.empty() && m_running && myGen == m_searchGeneration.load(std::memory_order_relaxed)) {
            std::wstring curDir = std::move(dirQueue.front());
            dirQueue.pop_front();
            ++scannedFolders;

            std::wstring prefix = PathUtils::EnsureTrailingSlash(curDir);
            std::wstring pattern = prefix + L"*";

            WIN32_FIND_DATAW ffd;
            HANDLE hFind = FindFirstFileExW(
                pattern.c_str(),
                FindExInfoBasic,
                &ffd,
                FindExSearchNameMatch,
                nullptr,
                FIND_FIRST_EX_LARGE_FETCH);

            if (hFind == INVALID_HANDLE_VALUE) continue;

            // Relative subfolder label for results display
            std::wstring relFolder;
            if (curDir.size() > rootClean.size() && _wcsnicmp(curDir.c_str(), rootClean.c_str(), rootClean.size()) == 0) {
                relFolder = curDir.substr(rootClean.size());
            }

            do {
                if (!m_running || myGen != m_searchGeneration.load(std::memory_order_relaxed)) {
                    break;
                }
                const wchar_t* name = ffd.cFileName;
                if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
                    continue;
                }

                bool isHidden = (ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
                if (isHidden && !showHidden) continue;

                bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (isDir) {
                    if (opts.recursive && !(ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                        if (wcscmp(name, L"$RECYCLE.BIN") != 0 && wcscmp(name, L"System Volume Information") != 0) {
                            dirQueue.push_back(prefix + name);
                        }
                    }
                    if (!opts.includeFolders) continue;
                } else {
                    ++scannedFiles;
                    if (!opts.includeFiles) continue;
                }

                // 1. Fast Date Filter (64-bit integer compare before any string allocation)
                if (opts.useDateFilter) {
                    ULARGE_INTEGER ftItem = {};
                    ftItem.LowPart = ffd.ftLastWriteTime.dwLowDateTime;
                    ftItem.HighPart = ffd.ftLastWriteTime.dwHighDateTime;
                    if (opts.hasDateFrom && ftItem.QuadPart < ftMin.QuadPart) continue;
                    if (opts.hasDateTo && ftItem.QuadPart > ftMax.QuadPart) continue;
                }

                // 2. Fast Size Filter (64-bit integer compare before any string allocation)
                uint64_t fsize = isDir ? 0 : ((static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow);
                if (opts.useSizeFilter && !isDir) {
                    if (fsize < opts.sizeMinBytes || fsize > opts.sizeMaxBytes) continue;
                } else if (opts.useSizeFilter && isDir) {
                    continue; // Size range applies to files
                }

                // 3. Extension / Type Filter
                std::wstring extLo;
                if (!isDir) {
                    const wchar_t* dot = wcsrchr(name, L'.');
                    if (dot) extLo = PathUtils::ToLower(dot);
                }
                if (opts.useTypeFilter) {
                    if (isDir) continue;
                    bool typeOk = false;
                    if (opts.categoryMask != SEARCH_CAT_NONE) {
                        uint32_t cat = ClassifyExtCategory(extLo);
                        if ((cat & opts.categoryMask) != 0) typeOk = true;
                    }
                    if (!typeOk && !customExts.empty()) {
                        for (const auto& ce : customExts) {
                            if (extLo == ce) { typeOk = true; break; }
                        }
                    }
                    if (!typeOk && opts.categoryMask == SEARCH_CAT_NONE && customExts.empty()) {
                        typeOk = true;
                    }
                    if (!typeOk) continue;
                }

                // 4. Query Match (Filename or File Content)
                bool nameMatched = false;
                if (needle.empty()) {
                    nameMatched = true;
                } else if (hasWildcard) {
                    nameMatched = PathMatchSpecW(name, needle.c_str()) != FALSE;
                } else {
                    nameMatched = FastSubstrMatch(name, needle, opts.caseSensitive);
                }

                if (!nameMatched) {
                    if (opts.searchContent && !isDir && !needle.empty() && fsize > 0 && fsize <= 4 * 1024 * 1024) {
                        uint32_t cat = ClassifyExtCategory(extLo);
                        if (cat == SEARCH_CAT_CODE || extLo == L".txt" || extLo == L".csv" || extLo == L".cfg") {
                            nameMatched = FileContentContainsText(prefix + name, needle);
                        }
                    }
                    if (!nameMatched) continue;
                }

                // Construct matched FileEntry
                FileEntry entry;
                entry.name = name;
                entry.fullPath = prefix + name;
                entry.normKey = PathUtils::NormalizePathKey(entry.fullPath);
                entry.attributes = ffd.dwFileAttributes;
                entry.writeTime = ffd.ftLastWriteTime;
                entry.isDirectory = isDir;
                entry.isHidden = isHidden;
                entry.dateStr = FormatFileTime(entry.writeTime);
                entry.ext = std::move(extLo);

                if (isDir) {
                    entry.displayName = entry.name;
                    const auto& extInfo = GetExtensionInfo(L"", true, lang);
                    entry.typeName = extInfo.typeName;
                    entry.sysIconIndex = extInfo.sysIconIndex;
                } else {
                    if (!showExtensions && !entry.ext.empty() && entry.name.size() > entry.ext.size()) {
                        entry.displayName = entry.name.substr(0, entry.name.size() - entry.ext.size());
                    } else {
                        entry.displayName = entry.name;
                    }
                    entry.size = fsize;
                    entry.sizeStr = FormatSizeKB(entry.size);
                    entry.isImage = IsImageExtension(entry.ext);
                    entry.isVideo = IsVideoExtension(entry.ext);
                    entry.isAudio = IsAudioExtension(entry.ext);

                    const auto& extInfo = GetExtensionInfo(entry.ext, false, lang);
                    entry.typeName = extInfo.typeName;
                    entry.sysIconIndex = extInfo.sysIconIndex;
                }

                if (!relFolder.empty()) {
                    entry.typeName += L"  [" + relFolder + L"]";
                }

                batchMatches.push_back(std::move(entry));
                ++matchedCount;

                ULONGLONG nowTick = GetTickCount64();
                if (nowTick - lastNotifyTick >= 75 || batchMatches.size() >= 200) {
                    lastNotifyTick = nowTick;
                    {
                        std::lock_guard<std::mutex> lock(m_searchMutex);
                        if (m_searchAccumResults.size() < 25000) {
                            for (auto& bm : batchMatches) {
                                m_searchAccumResults.push_back(std::move(bm));
                            }
                        }
                        batchMatches.clear();
                        m_searchProgress.matchedCount = matchedCount;
                        m_searchProgress.scannedFiles = scannedFiles;
                        m_searchProgress.scannedFolders = scannedFolders;
                        m_searchProgress.elapsedMs = nowTick - startTick;
                        m_searchProgress.currentFolder = curDir;
                    }
                    if (m_notifyHwnd && !m_searchNotifyPending.exchange(true, std::memory_order_relaxed)) {
                        PostMessageW(m_notifyHwnd, WM_APP_SEARCH_UPDATED, 0, 0);
                    }
                }
            } while (FindNextFileW(hFind, &ffd));

            FindClose(hFind);

            ULONGLONG nowTick = GetTickCount64();
            if (nowTick - lastNotifyTick >= 75) {
                lastNotifyTick = nowTick;
                {
                    std::lock_guard<std::mutex> lock(m_searchMutex);
                    if (!batchMatches.empty() && m_searchAccumResults.size() < 25000) {
                        for (auto& bm : batchMatches) {
                            m_searchAccumResults.push_back(std::move(bm));
                        }
                    }
                    batchMatches.clear();
                    m_searchProgress.matchedCount = matchedCount;
                    m_searchProgress.scannedFiles = scannedFiles;
                    m_searchProgress.scannedFolders = scannedFolders;
                    m_searchProgress.elapsedMs = nowTick - startTick;
                    m_searchProgress.currentFolder = curDir;
                }
                if (m_notifyHwnd && !m_searchNotifyPending.exchange(true, std::memory_order_relaxed)) {
                    PostMessageW(m_notifyHwnd, WM_APP_SEARCH_UPDATED, 0, 0);
                }
            }
        }

        if (m_running && myGen == m_searchGeneration.load(std::memory_order_relaxed)) {
            ULONGLONG endTick = GetTickCount64();
            {
                std::lock_guard<std::mutex> lock(m_searchMutex);
                if (!batchMatches.empty() && m_searchAccumResults.size() < 25000) {
                    for (auto& bm : batchMatches) {
                        m_searchAccumResults.push_back(std::move(bm));
                    }
                }
                batchMatches.clear();
                m_searchProgress.matchedCount = matchedCount;
                m_searchProgress.scannedFiles = scannedFiles;
                m_searchProgress.scannedFolders = scannedFolders;
                m_searchProgress.elapsedMs = endTick - startTick;
                m_searchProgress.isRunning = false;
                m_searchProgress.isFinished = true;
                m_searchRunning.store(false, std::memory_order_relaxed);
            }
            if (m_notifyHwnd) {
                PostMessageW(m_notifyHwnd, WM_APP_SEARCH_UPDATED, 0, 0);
            }
        }
    }
}

} // namespace Explorer2
