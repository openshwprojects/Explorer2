#pragma once
// The names Explorer2 uses for its virtual folders in place of a path. No Windows headers: the archive readers use
// this too, and they also build on Linux.
#include <string>

namespace Explorer2 {

// "\\server" alone (trailing separators allowed): not a folder of the file system but the list of the server's
// shared folders (NetShares). "\\?\..." and "\\.\..." are device paths, not servers.
static inline bool IsUncServerRoot(const std::wstring& p) {
    if (p.size() < 3 || p[0] != L'\\' || p[1] != L'\\') return false;
    if (p[2] == L'\\' || p[2] == L'/' || p[2] == L'?' || p[2] == L'.') return false;
    size_t end = p.size();
    while (end > 2 && (p[end - 1] == L'\\' || p[end - 1] == L'/')) --end;
    return p.find_first_of(L"\\/", 2) >= end;
}

// "MOUNT_VOLUME:/dev/sdb1" (Linux): a file system on a pluggable device (a USB stick, a card) that is not mounted
// yet. It is listed with the drives; opening it mounts the volume and goes to the folder it was mounted at.
inline constexpr wchar_t kUnmountedVolumePrefix[] = L"MOUNT_VOLUME:";
static inline bool IsUnmountedVolumePath(const std::wstring& p) {
    return p.rfind(kUnmountedVolumePrefix, 0) == 0;
}
// The device of such a path ("/dev/sdb1"), or empty
static inline std::wstring UnmountedVolumeDevice(const std::wstring& p) {
    return IsUnmountedVolumePath(p) ? p.substr(sizeof(kUnmountedVolumePrefix) / sizeof(wchar_t) - 1) : std::wstring();
}

// "NETWORK": the "Sieć" (Network) place in front of a \\server path; opening it shows the LAN scanner
static inline bool IsVirtualFolder(const std::wstring& p) {
    return p == L"THIS_PC" || p == L"RECYCLE_BIN" || p == L"NETWORK" || IsUncServerRoot(p) || IsUnmountedVolumePath(p);
}

} // namespace Explorer2
