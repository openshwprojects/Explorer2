#pragma once
// The freedesktop.org Trash on Linux, kept as GNOME and KDE keep it. An item on the drive of the home folder goes to
// the home trash ($XDG_DATA_HOME/Trash); an item on another drive - a USB stick, another partition - to that drive's
// own trash in its top folder: "<top>/.Trash/<uid>" when an administrator made "<top>/.Trash" (a sticky folder), else
// "<top>/.Trash-<uid>". Either way the item is moved by rename(), never copied. Drives kept in memory, the kernel's
// own and network drives have no trash, as in GNOME. A trash folder holds files/ (the items) and info/ (one
// .trashinfo per item: where it was, when it was deleted).
//
// Header-only and plain POSIX: the HAL's delete (Win32HalCore.cpp, which the readers tools link too), FileEngine (the
// Recycle Bin view) and TransferManager (the "no Recycle Bin here" question) use these same rules.
#ifndef _WIN32
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace Explorer2::LinuxTrash {

inline std::string ParentOf(const std::string& p) {
    const size_t sl = p.find_last_of('/');
    if (sl == std::string::npos) return ".";
    return sl == 0 ? std::string("/") : p.substr(0, sl);
}

inline std::string NameOf(const std::string& p) {
    const size_t sl = p.find_last_of('/');
    return sl == std::string::npos ? p : p.substr(sl + 1);
}

inline std::string WithoutTrailingSlashes(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

// The home trash: $XDG_DATA_HOME/Trash, else ~/.local/share/Trash
inline std::string HomeTrashDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return WithoutTrailingSlashes(xdg) + "/Trash";
    const char* home = std::getenv("HOME");
    if (home && *home) return WithoutTrailingSlashes(home) + "/.local/share/Trash";
    return "/tmp/Trash-" + std::to_string(::geteuid());
}

// Makes a folder and any missing folders above it with mode 0700, the mode the trash specification asks for (the
// names in a trash say what the user deleted). True when the folder exists afterwards.
inline bool MakePrivateDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size() && path[i] != '/') continue;
        const std::string sub = path.substr(0, i);
        if (sub.back() == '/') continue;
        if (::mkdir(sub.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    struct stat st = {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// The folder a relative Path= in a trash folder's .trashinfo files is relative to: the drive's top folder for
// "<top>/.Trash-<uid>" and "<top>/.Trash/<uid>", the folder the home trash is in ($XDG_DATA_HOME) for that one
inline std::string RelativePathBase(const std::string& trashDir) {
    const std::string dir = WithoutTrailingSlashes(trashDir);
    const std::string parent = ParentOf(dir);
    if (NameOf(dir).rfind(".Trash-", 0) == 0) return parent;
    if (NameOf(parent) == ".Trash") return ParentOf(parent);
    return parent;
}

// The mount table: each line of /proc/self/mounts as mount point and file system type, the "\040"-style escapes of
// the mount point decoded
struct MountEntry {
    std::string mountPoint;
    std::string fsType;
};

inline std::vector<MountEntry> ReadMounts() {
    std::vector<MountEntry> out;
    FILE* f = std::fopen("/proc/self/mounts", "re");
    if (!f) return out;
    char line[4096];
    while (std::fgets(line, sizeof(line), f)) {
        char src[1024] = {}, mnt[2048] = {}, type[256] = {};
        if (std::sscanf(line, "%1023s %2047s %255s", src, mnt, type) != 3) continue;
        std::string decoded;
        for (size_t i = 0; mnt[i]; ++i) {
            if (mnt[i] == '\\' && mnt[i + 1] >= '0' && mnt[i + 1] <= '3' && mnt[i + 2] >= '0' && mnt[i + 2] <= '7' &&
                mnt[i + 3] >= '0' && mnt[i + 3] <= '7') {
                decoded.push_back(static_cast<char>((mnt[i + 1] - '0') * 64 + (mnt[i + 2] - '0') * 8 + (mnt[i + 3] - '0')));
                i += 3;
            } else {
                decoded.push_back(mnt[i]);
            }
        }
        out.push_back({ decoded, type });
    }
    std::fclose(f);
    return out;
}

// File systems that get no trash, as in GNOME: kept in memory, the kernel's own, read-only image formats, and
// network or FUSE network ones (their server may be slow or gone). "fuseblk" - NTFS or exFAT on a USB disk through
// FUSE - is a local drive and has one.
inline bool FsTypeHasNoTrash(const std::string& type) {
    static const char* const kNone[] = {
        "tmpfs", "ramfs", "proc", "sysfs", "devtmpfs", "devpts", "cgroup", "cgroup2", "debugfs", "securityfs", "pstore",
        "bpf", "tracefs", "configfs", "fusectl", "mqueue", "hugetlbfs", "autofs", "binfmt_misc", "efivarfs", "nsfs",
        "rpc_pipefs", "squashfs", "iso9660", "udf", "nfs", "nfs4", "cifs", "smb3", "smbfs", "afs", "ceph", "9p",
        "davfs", "glusterfs", "lustre",
    };
    for (const char* t : kNone) {
        if (type == t) return true;
    }
    return type.rfind("fuse.", 0) == 0;     // sshfs, rclone, gvfsd-fuse ...
}

// The type of the file system mounted at `mountPoint` (the last mount there), or "" when the table does not say
inline std::string FsTypeAt(const std::string& mountPoint) {
    std::string type;
    for (const MountEntry& m : ReadMounts()) {
        if (m.mountPoint == mountPoint) type = m.fsType;
    }
    return type;
}

// The top folder of the drive that holds the folder `dir` (resolved, existing, on device `dev`): the highest folder
// above it on the same device
inline std::string MountTop(std::string dir, dev_t dev) {
    while (dir != "/") {
        const std::string up = ParentOf(dir);
        struct stat st = {};
        if (::stat(up.c_str(), &st) != 0 || st.st_dev != dev) break;
        dir = up;
    }
    return dir;
}

// A trash folder that is a real folder (not a link) of this user, its files/ and info/ made when create is set. A
// missing one is made when create is set; without create it counts when it could be made.
inline bool UsableTrashDir(const std::string& dir, bool create) {
    struct stat st = {};
    if (::lstat(dir.c_str(), &st) == 0) {
        if (!S_ISDIR(st.st_mode) || st.st_uid != ::geteuid()) return false;
        return !create || (MakePrivateDirs(dir + "/files") && MakePrivateDirs(dir + "/info"));
    }
    if (!create) return ::access(ParentOf(dir).c_str(), W_OK) == 0;
    return ::mkdir(dir.c_str(), 0700) == 0 && MakePrivateDirs(dir + "/files") && MakePrivateDirs(dir + "/info");
}

// The trash folder of the drive whose top folder is `top`, for this user: "<top>/.Trash/<uid>" when "<top>/.Trash"
// is a real folder with the sticky bit (made by an administrator for everyone), else "<top>/.Trash-<uid>". With
// create it is made (mode 0700) when missing. "" when there is none and none can be made (a read-only drive, or a
// drive whose files belong to someone else).
inline std::string TopDirTrash(const std::string& top, bool create) {
    const std::string base = (top == "/") ? std::string() : WithoutTrailingSlashes(top);
    const std::string uid = std::to_string(::geteuid());
    struct stat st = {};
    const std::string shared = base + "/.Trash";
    if (::lstat(shared.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX)) {
        const std::string mine = shared + "/" + uid;
        if (UsableTrashDir(mine, create)) return mine;
    }
    const std::string own = base + "/.Trash-" + uid;
    return UsableTrashDir(own, create) ? own : std::string();
}

// The trash folder the item at `itemPath` (absolute, existing) goes to: the home trash when the item is on the home
// trash's drive, else its own drive's trash. "" when it has none: a drive kept in memory, a system or network drive,
// a read-only one, or a mount point itself. With create the folder is made.
inline std::string TrashDirFor(const std::string& itemPath, bool create) {
    const std::string clean = WithoutTrailingSlashes(itemPath);
    struct stat stItem = {};
    if (clean.empty() || clean[0] != '/' || clean == "/" || ::lstat(clean.c_str(), &stItem) != 0) return {};
    // The folder the item is in, resolved (a link in the path leads to the drive the item really is on)
    char buf[PATH_MAX] = {};
    const std::string parentRaw = ParentOf(clean);
    const std::string parent = ::realpath(parentRaw.c_str(), buf) ? std::string(buf) : parentRaw;
    struct stat stParent = {};
    if (::stat(parent.c_str(), &stParent) != 0 || stParent.st_dev != stItem.st_dev) return {};   // a mount point

    // The home trash, when the item is on its drive. It may not exist yet: it is made inside the nearest folder
    // above it that does.
    const std::string home = HomeTrashDir();
    std::string probe = home;
    struct stat stHome = {};
    while (::stat(probe.c_str(), &stHome) != 0) {
        if (probe == "/" || probe.empty()) break;
        probe = ParentOf(probe);
    }
    if (::stat(probe.c_str(), &stHome) == 0 && stHome.st_dev == stItem.st_dev) {
        if (!create) return home;
        struct stat stFiles = {};
        if (MakePrivateDirs(home + "/files") && MakePrivateDirs(home + "/info") &&
            ::stat((home + "/files").c_str(), &stFiles) == 0 && stFiles.st_dev == stItem.st_dev) {
            return home;
        }
        return {};
    }

    // Another drive: the trash in its own top folder, unless it is a drive without one
    const std::string top = MountTop(parent, stItem.st_dev);
    if (FsTypeHasNoTrash(FsTypeAt(top))) return {};
    return TopDirTrash(top, create);
}

// Every trash folder there is: the home trash and those of the mounted drives that have one
inline std::vector<std::string> AllTrashDirs() {
    std::vector<std::string> out;
    auto add = [&](const std::string& dir) {
        struct stat st = {};
        if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid()) return;
        for (const std::string& known : out) {
            if (known == dir) return;
        }
        out.push_back(dir);
    };
    add(HomeTrashDir());
    const std::string uid = std::to_string(::geteuid());
    for (const MountEntry& m : ReadMounts()) {
        if (FsTypeHasNoTrash(m.fsType)) continue;      // never touched: a network drive may not answer
        const std::string base = (m.mountPoint == "/") ? std::string() : m.mountPoint;
        add(base + "/.Trash/" + uid);
        add(base + "/.Trash-" + uid);
    }
    return out;
}

// The .trashinfo of an item right inside a trash folder's files/, or "" for any other path
inline std::string InfoPathFor(const std::string& dataPath) {
    const std::string clean = WithoutTrailingSlashes(dataPath);
    const std::string filesDir = ParentOf(clean);
    if (NameOf(filesDir) != "files") return {};
    const std::string trashDir = ParentOf(filesDir);
    const bool isTrash = trashDir == HomeTrashDir() || NameOf(trashDir).rfind(".Trash-", 0) == 0 ||
                         NameOf(ParentOf(trashDir)) == ".Trash";
    return isTrash ? trashDir + "/info/" + NameOf(clean) + ".trashinfo" : std::string();
}

// Path= of a .trashinfo, URL-encoded as the specification asks
inline std::string UrlEncodePath(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 16);
    static const char kHex[] = "0123456789ABCDEF";
    for (unsigned char c : raw) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0xF]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

// Moves the item at srcPath into trashDir (which exists, with files/ and info/): its .trashinfo first, created
// exclusively so that the name is ours, then the item itself by rename(). In a drive's own trash Path= is relative
// to the drive's top folder, as GNOME writes it, so that the item finds its place when the drive is mounted
// elsewhere next time; in the home trash it is absolute. Returns 0, or the errno of the failure (EEXIST: no free
// name); on a failure the item stays where it was and no .trashinfo is left behind.
inline int MoveToTrash(const std::string& srcPath, const std::string& trashDir) {
    const std::string clean = WithoutTrailingSlashes(srcPath);
    // Path= names the item itself: its folder resolved, its own name as it is, so that a symbolic link is restored
    // as that link and not as the file it points to
    const std::string parentDir = ParentOf(clean);
    const std::string leafName = NameOf(clean);
    char realBuf[PATH_MAX] = {};
    const std::string parentAbs = ::realpath(parentDir.c_str(), realBuf) ? std::string(realBuf) : parentDir;
    const std::string origAbs = (parentAbs == "/") ? "/" + leafName : parentAbs + "/" + leafName;
    std::string pathValue = origAbs;
    if (WithoutTrailingSlashes(trashDir) != HomeTrashDir()) {
        const std::string base = RelativePathBase(trashDir);
        const std::string prefix = (base == "/") ? std::string("/") : base + "/";
        if (origAbs.size() > prefix.size() && origAbs.compare(0, prefix.size(), prefix) == 0) pathValue = origAbs.substr(prefix.size());
    }

    const std::string trashFiles = trashDir + "/files", trashInfo = trashDir + "/info";
    const std::string leaf = leafName.empty() ? std::string("trashed_item") : leafName;
    std::string stem = leaf, ext;
    const size_t dot = leaf.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        stem = leaf.substr(0, dot);
        ext = leaf.substr(dot);
    }
    std::string chosenName;
    int infoFd = -1, lastErr = EEXIST;
    for (int attempt = 1; attempt < 10000; ++attempt) {
        const std::string cand = (attempt == 1) ? leaf : (stem + "." + std::to_string(attempt) + ext);
        struct stat stExist = {};
        if (::lstat((trashFiles + "/" + cand).c_str(), &stExist) == 0) continue;
        infoFd = ::open((trashInfo + "/" + cand + ".trashinfo").c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (infoFd >= 0) {
            chosenName = cand;
            break;
        }
        lastErr = errno;
        if (lastErr != EEXIST) break;           // the info folder itself is the problem: another name does not help
    }
    if (infoFd < 0) return lastErr;
    const std::string infoPath = trashInfo + "/" + chosenName + ".trashinfo";

    const time_t now = ::time(nullptr);
    struct tm lt = {};
    localtime_r(&now, &lt);
    char dtBuf[64] = {};
    std::snprintf(dtBuf, sizeof(dtBuf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    const std::string infoBody = "[Trash Info]\nPath=" + UrlEncodePath(pathValue) + "\nDeletionDate=" + dtBuf + "\n";
    // An item in the trash without its complete .trashinfo could not be restored, so it is only moved once the
    // .trashinfo is written (a full disk shows up here)
    int err = 0;
    size_t off = 0;
    while (off < infoBody.size()) {
        const ssize_t wr = ::write(infoFd, infoBody.data() + off, infoBody.size() - off);
        if (wr < 0) {
            if (errno == EINTR) continue;
            err = errno;
            break;
        }
        off += static_cast<size_t>(wr);
    }
    if (::close(infoFd) != 0 && err == 0) err = errno;
    // rename() only: an item that turns out to be on another file system after all (a bind mount, which rename
    // refuses with EXDEV) stays where it is instead of being copied
    if (err == 0 && ::rename(clean.c_str(), (trashFiles + "/" + chosenName).c_str()) != 0) err = errno;
    if (err != 0) ::unlink(infoPath.c_str());
    return err;
}

} // namespace Explorer2::LinuxTrash
#endif
