#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <thread>

namespace Explorer2 {

// Waits for a worker thread that may be inside a shell call (file-type icons, thumbnails, shell menus).
// Some shell extensions are written for the main thread's COM apartment: COM carries their calls out on
// the main thread, by way of window messages. A main thread that simply blocked in join() would then wait
// for a worker that is waiting for the main thread. This keeps serving those calls while it waits.
// Called by the main thread when the program closes.
inline void JoinServingCom(std::thread& worker) {
    if (!worker.joinable()) return;
    HANDLE handle = static_cast<HANDLE>(worker.native_handle());
    DWORD index = 0;
    // Fails at once on a thread without COM; join() then waits the plain way
    CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS, INFINITE, 1, &handle, &index);
    worker.join();
}

} // namespace Explorer2
