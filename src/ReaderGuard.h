#pragma once
// A damaged or hostile file must never take the program down.
//
// The file readers (images, 3D models, boards, documents, archives) work on whatever bytes a file holds.
// When one of them lets a C++ exception escape, for example "out of memory" because a size field in the
// file is nonsense, the exception would end a worker thread and with it the whole program. Every reader's
// entry point therefore ends in E2_READER_CATCH: the exception stops there, the file counts as unreadable,
// and the log gets a line saying which reader gave up on which file and why.
//
// This is a safety net, not a licence for sloppy readers: `Explorer2.exe --inspect-files` (driven by
// tools/fuzz_samples.py) still reports every exception that was stopped here as a fault to be fixed.
#include "AppLog.h"
#include "PathUtils.h"
#include <atomic>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>

namespace Explorer2::ReaderGuard {

inline std::atomic<uint64_t> g_stoppedCount{ 0 };
inline std::mutex g_lastMutex;
inline std::string g_lastMessage;

// Records one exception that a reader let escape. Never throws.
inline void NoteStopped(const wchar_t* reader, const std::wstring& file, const char* what) noexcept {
    try {
        const char* text = (what && *what) ? what : "unknown exception";
        {
            std::lock_guard<std::mutex> lock(g_lastMutex);
            g_lastMessage = text;
        }
        g_stoppedCount.fetch_add(1);
        AppLog::Warn(L"Reader", std::wstring(reader) + L" gave up on " + file + L": " + PathUtils::Utf8ToWide(text));
    } catch (...) {
    }
}

// How many exceptions have been stopped so far, and the text of the last one
inline uint64_t StoppedCount() noexcept { return g_stoppedCount.load(); }
inline std::string LastMessage() {
    std::lock_guard<std::mutex> lock(g_lastMutex);
    return g_lastMessage;
}

} // namespace Explorer2::ReaderGuard

// Closes a function-try-block:  bool Reader::Read(const std::wstring& path, Result& out) try { ... }
//                               E2_READER_CATCH(L"Board reader", path, { out = Result(); return false; })
#define E2_READER_CATCH(readerName, file, onFailure)                                                 \
    catch (const std::exception& e2ReaderException) {                                                \
        ::Explorer2::ReaderGuard::NoteStopped(readerName, file, e2ReaderException.what());           \
        onFailure                                                                                    \
    } catch (...) {                                                                                  \
        ::Explorer2::ReaderGuard::NoteStopped(readerName, file, nullptr);                            \
        onFailure                                                                                    \
    }
