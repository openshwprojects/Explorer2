#pragma once
// FileLockInspector.h — Multi-engine File & Directory Lock Inspector and Surgical Interactive Unlocker
//
// Detects what prevents deleting, renaming, moving, or overwriting one or more files or directories
// (including all nested directory contents):
//   1. Windows Restart Manager (rstrtmgr.dll, loaded dynamically for Win7 SP1 / N-edition safety)
//   2. NT System Handle Table (NtQuerySystemInformation SystemExtendedHandleInformation) with a
//      pipe-deadlock-proof batched worker thread + NtQueryObject volume pre-filtering — catches open
//      directory handles, ReadDirectoryChangesW folder watchers, and CWD ("cd") locks, and records
//      remote handle values for surgical non-destructive handle unlocking (DUPLICATE_CLOSE_SOURCE)
//   3. Process Executable, Loaded DLL & Memory-Mapped Section Snapshot (VirtualQueryEx + dynamic
//      NtQueryVirtualMemory MemoryMappedFilenameInformation + Toolhelp32) — catches running .exe,
//      loaded .dll/.ocx/.pyd, and CreateFileMappingW mapped data files even after CloseHandle(hFile)
//   4. Read-Only attributes, NTFS DELETE ACL / Administrator permission blocks, Windows Services,
//      and Explorer2's own preview / media / active-folder holds
//   (On Linux: scans /proc/[pid]/fd, /proc/[pid]/cwd, /proc/[pid]/exe, /proc/[pid]/maps, and POSIX permissions.)

#include "Types.h"
#include "Localization.h"
#include "TransferManager.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Explorer2 {

enum LockReasonBits : uint32_t {
    kLockReasonNone        = 0u,
    kLockReasonOpenHandle  = 1u << 0, // Open file handle or memory-mapped data section
    kLockReasonFolderOrCwd = 1u << 1, // Process Current Working Directory (CWD) or open directory handle
    kLockReasonRunningExe  = 1u << 2, // Running executable image lives at/inside target
    kLockReasonLoadedDll   = 1u << 3, // Loaded DLL / shared library lives at/inside target
    kLockReasonService     = 1u << 4, // Windows Service holding file/directory
    kLockReasonSelfPreview = 1u << 5  // Held inside this Explorer2 window (preview / player / current folder)
};

struct LockedHandleDescriptor {
    uintptr_t    handleValue = 0;
    uint32_t     grantedAccess = 0;
    uint32_t     handleAttributes = 0; // bit 0 (0x1) = OBJ_PROTECT_CLOSE
    std::wstring path;
    bool         isDirectory = false;
};

struct LockingProcessEntry {
    uint32_t                            pid = 0;
    uint64_t                            startTimeTicks = 0;
    std::wstring                        processName;
    std::wstring                        exeFullPath;
    std::wstring                        windowTitle;
    std::wstring                        serviceName;
    uint32_t                            reasonMask = kLockReasonNone;
    bool                                isSelfProcess = false;
    bool                                isSystemCritical = false;
    bool                                isShellProcess = false;
    bool                                isElevatedOrProtected = false;
    std::vector<std::wstring>           lockedPaths;
    std::vector<LockedHandleDescriptor> openHandles;

    bool DefaultCheckedForUnlock() const {
        return !isSelfProcess && !isSystemCritical && !isShellProcess;
    }

    bool HasClosableHandles() const {
        if (isSelfProcess || isSystemCritical) return false;
        for (const auto& h : openHandles) {
            if (h.handleValue != 0 && (h.handleAttributes & 0x1u) == 0u) return true;
        }
        return false;
    }
};

struct PermissionBlockerEntry {
    std::wstring path;
    DWORD        win32Error = 0;
    bool         isReadOnlyAttr = false;
    bool         isAclAccessDenied = false;
    std::wstring detail;
};

struct SelfHoldInfo {
    std::wstring mediaPath;
    std::wstring previewPath;
    std::wstring activeFolderPath;
};

struct FileLockReport {
    std::vector<std::wstring>           targetPaths;
    std::vector<LockingProcessEntry>    processes;
    std::vector<PermissionBlockerEntry> permissionBlockers;
    size_t                              scannedFiles = 0;
    size_t                              scannedDirs = 0;
    uint64_t                            elapsedMs = 0;

    bool HasAnyBlockers() const {
        return !processes.empty() || !permissionBlockers.empty();
    }
    bool HasReadOnlyBlockers() const {
        for (const auto& b : permissionBlockers) {
            if (b.isReadOnlyAttr) return true;
        }
        return false;
    }
    bool HasAclOrElevatedBlockers() const {
        for (const auto& b : permissionBlockers) {
            if (b.isAclAccessDenied) return true;
        }
        for (const auto& p : processes) {
            if ((p.isElevatedOrProtected && !p.isSystemCritical) || !p.serviceName.empty()) return true;
        }
        return false;
    }
    bool HasClosableHandles() const {
        for (const auto& p : processes) {
            if (p.HasClosableHandles() || p.isSelfProcess) return true;
        }
        return false;
    }
    bool HasServices() const {
        for (const auto& p : processes) {
            if (!p.serviceName.empty()) return true;
        }
        return false;
    }
    bool HasUnclosableImageLocks() const {
        for (const auto& p : processes) {
            if ((p.reasonMask & (kLockReasonRunningExe | kLockReasonLoadedDll)) != 0) return true;
        }
        return false;
    }
};

enum class LockFailureOpKind : uint8_t {
    None   = 0, // Inspection / unlock mode ("Check what is locking...") — NEVER deletes user files
    Delete = 1, // Delete job failed — retry deletes; Admin UAC may permanently delete / schedule reboot delete
    Rename = 2, // Inline Rename failed — retry re-runs StartRenameJob; NEVER deletes user files
    Move   = 3, // Move transfer failed — retry re-runs Move job; NEVER deletes user files
    Copy   = 4  // Copy transfer failed — retry re-runs Copy job; NEVER deletes user files
};

struct LockDialogCallbacks {
    bool                            isDeleteFailureMode = false;
    LockFailureOpKind               failureOp = LockFailureOpKind::None;
    std::wstring                    operationSummary;
    size_t                          failedDeleteCount = 0;
    std::vector<TransferFileError>  deleteErrors;
    std::function<SelfHoldInfo()>   querySelfHold;
    std::function<void()>           releaseSelfHold;
    std::function<void()>           retryDeleteJob;
    std::function<void()>           onAdminDeleteSucceeded;

    LockFailureOpKind EffectiveOp() const {
        if (failureOp != LockFailureOpKind::None) return failureOp;
        return isDeleteFailureMode ? LockFailureOpKind::Delete : LockFailureOpKind::None;
    }
};

namespace FileLockInspector {

struct RemoteHandleCloseRequest {
    uint32_t     pid = 0;
    uint64_t     expectedStartTicks = 0;
    uintptr_t    handleValue = 0;
    uint32_t     handleAttributes = 0;
    std::wstring expectedPath;
};

struct ElevatedUnlockRequest {
    std::vector<RemoteHandleCloseRequest> handlesToClose;
    std::vector<std::wstring>             servicesToStop;
    std::vector<uint32_t>                 pidsToKill;
    std::vector<uint64_t>                 pidStartTicks;
    std::vector<std::wstring>             pathsToClearReadOnly;
    std::vector<std::wstring>             pathsToDeleteNow;
    std::vector<std::wstring>             pathsToDeleteOnReboot;
};

// Inspects all process locks, open directory/CWD handles, running EXEs/DLLs, memory-mapped sections,
// Read-Only flags, and ACL DELETE-access denials preventing operations on `targetPaths` (and nested contents).
void InspectFileLocks(
    const std::vector<std::wstring>& targetPaths,
    FileLockReport& outReport,
    const SelfHoldInfo* selfHold = nullptr,
    std::atomic<bool>* cancelFlag = nullptr);

// Surgically closes open file/directory handles inside remote non-critical processes via DUPLICATE_CLOSE_SOURCE
// after double TOCTOU verification (PID creation time + pipe-safe live path re-verification + OBJ_PROTECT_CLOSE check).
// The target application remains alive and running!
bool CloseRemoteFileHandles(
    const std::vector<RemoteHandleCloseRequest>& requests,
    size_t* outClosedCount = nullptr,
    bool* outAnyAccessDenied = nullptr,
    std::vector<RemoteHandleCloseRequest>* outElevationNeeded = nullptr);

// Stops Windows Services holding locks via ControlService(SERVICE_CONTROL_STOP) with bounded wait up to `waitTimeoutMs`.
bool StopLockingServices(
    const std::vector<std::wstring>& serviceNames,
    DWORD waitTimeoutMs = 6000,
    std::vector<std::wstring>* outFailedServices = nullptr,
    bool* outAnyAccessDenied = nullptr);

// Schedules files and directories (contents first, directories bottom-up) for deletion at next Windows boot
// via MoveFileExW(dosPath, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT).
bool ScheduleDeleteOnReboot(
    const std::vector<std::wstring>& targetPaths,
    size_t* outScheduledCount = nullptr,
    bool* outAccessDenied = nullptr);

// Sends WM_CLOSE (or SIGTERM on Linux) to non-critical processes and waits up to `waitTimeoutMs` for exit.
// When `expectedStartTicks` is provided (same length as `pids`), any non-zero entry must match the live
// process creation timestamp so recycled PIDs are never touched (TOCTOU protection).
bool CloseProcessesGracefully(
    const std::vector<uint32_t>& pids,
    DWORD waitTimeoutMs = 2200,
    std::vector<uint32_t>* outStillRunning = nullptr,
    const std::vector<uint64_t>* expectedStartTicks = nullptr);

// Terminates non-critical processes via TerminateProcess (or SIGKILL on Linux) and waits up to `waitTimeoutMs`.
// When `expectedStartTicks` is provided (same length as `pids`), any non-zero entry must match the live
// process creation timestamp so recycled PIDs are never terminated (TOCTOU protection).
bool TerminateLockingProcesses(
    const std::vector<uint32_t>& pids,
    DWORD waitTimeoutMs = 1500,
    std::vector<uint32_t>* outFailedPids = nullptr,
    bool* outAnyAccessDenied = nullptr,
    const std::vector<uint64_t>* expectedStartTicks = nullptr);

// Clears FILE_ATTRIBUTE_READONLY on target files/directories (recursively up to bounded depth).
size_t ClearReadOnlyAttributes(const std::vector<std::wstring>& targetPaths);

// Prompts standard Windows UAC ("runas") invoking `Explorer2.exe --admin-unlock <reqFile> <resFile>`
// while pumping UI messages via MsgWaitForMultipleObjects so the parent window never freezes.
bool RunElevatedAdminRequest(
    HWND ownerHwnd,
    const ElevatedUnlockRequest& req,
    std::wstring* outErrorSummary = nullptr);

// Convenience wrapper preserving legacy signature for elevated PID termination and/or permanent tree deletion.
bool RunElevatedUnlockAndDelete(
    HWND ownerHwnd,
    const std::vector<uint32_t>& elevatedPidsToKill,
    const std::vector<std::wstring>& pathsToDelete,
    std::wstring* outErrorSummary = nullptr,
    const std::vector<uint64_t>* elevatedStartTicks = nullptr);

// CLI handler for `--admin-unlock <reqFile> <resFile>` (invoked via UAC runas).
int HandleAdminUnlockCli(const std::wstring& reqFilePath, const std::wstring& resFilePath);

// CLI helper for automated `--self-test` (`--hold-lock-test <file|cwd|window|mmap|dirwatch_pipe|dll> <path> <readyFile> <maxMs>`).
// Works on both Windows and Linux and always self-terminates after `maxMs` (clamped to <= 15000 ms).
int HandleLockTestHelperCli(
    const std::wstring& mode,
    const std::wstring& targetPath,
    const std::wstring& readyFilePath,
    int maxDurationMs);

std::wstring FormatLockReasonMask(uint32_t mask, bool pl);
std::wstring FormatLockReportText(const FileLockReport& report, bool pl);

// Opens the interactive modal Lock Inspector & Surgical Unlocker dialog.
void ShowFileLockInspectorDialog(
    HWND ownerHwnd,
    HINSTANCE hInst,
    HFONT fallbackFont,
    Lang lang,
    const std::vector<std::wstring>& targetPaths,
    const LockDialogCallbacks& callbacks);

} // namespace FileLockInspector

} // namespace Explorer2
