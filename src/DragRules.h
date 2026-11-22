#pragma once
// Rules of dragging items, kept apart from the window so that the self-tests can check them (Stage16.Test30)
#include "PlatformCompat.h"     // <windows.h> on Windows, the HAL's on Linux
#include "PathUtils.h"
#include <cwctype>
#include <string>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace Explorer2 {

// The drop source's answer while a drag is under way (IDropSource::QueryContinueDrag): Escape, or a press of the
// other button, cancels; letting go of the button the drag is done with drops. Only the left button used to be asked
// about, so a drag with the right button would have dropped at once (GitHub issue #4).
inline HRESULT DragQueryContinue(BOOL escapePressed, DWORD keyState, bool rightButton) {
    const DWORD button = rightButton ? MK_RBUTTON : MK_LBUTTON;
    const DWORD other = rightButton ? MK_LBUTTON : MK_RBUTTON;
    if (escapePressed || (keyState & other)) return DRAGDROP_S_CANCEL;
    if (!(keyState & button)) return DRAGDROP_S_DROP;
    return S_OK;
}

// Whether items dragged from srcPath into targetDir stay on one drive - dropping them there moves them, as in Windows
// Explorer, and elsewhere copies them: the same drive letter, or on Linux the same device
inline bool DropStaysOnDrive(const std::wstring& srcPath, const std::wstring& targetDir) {
    if (srcPath.size() >= 2 && targetDir.size() >= 2 && srcPath[1] == L':' && targetDir[1] == L':') {
        return std::towupper(srcPath[0]) == std::towupper(targetDir[0]);
    }
#ifndef _WIN32
    if (!srcPath.empty() && !targetDir.empty() && srcPath[0] == L'/' && targetDir[0] == L'/') {
        std::wstring srcParent = PathUtils::GetParentPath(srcPath);
        if (srcParent.empty()) srcParent = L"/";
        struct stat stSrc = {}, stDst = {};
        return ::stat(Explorer2::Compat::NativePath(srcParent.c_str()).c_str(), &stSrc) == 0 &&
               ::stat(Explorer2::Compat::NativePath(targetDir.c_str()).c_str(), &stDst) == 0 &&
               stSrc.st_dev == stDst.st_dev;
    }
#endif
    return false;
}

// An item that is already in the Windows Recycle Bin (C:\$Recycle.Bin\<SID>\$R...): dropped on "Kosz" again, it is
// left alone instead of being sent to the bin a second time
inline bool IsInsideWindowsRecycleBin(const std::wstring& path) {
    std::wstring low;
    low.reserve(path.size());
    for (wchar_t c : path) low += (c == L'/') ? L'\\' : static_cast<wchar_t>(std::towlower(c));
    return low.find(L"\\$recycle.bin\\") != std::wstring::npos;
}

} // namespace Explorer2
