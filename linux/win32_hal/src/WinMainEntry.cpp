// linux/win32_hal/src/WinMainEntry.cpp
// Linux main() entry point forwarding argc/argv UTF-8 command line to Explorer2's wWinMain().

#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <unistd.h>

namespace Explorer2::Hal {
void SetCommandLineFromMain(int argc, char** argv);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow);

namespace {

// Where the fatal-signal handler leaves its note: the folder AppLog writes its logs to. Worked out at start-up,
// because the handler itself may only call async-signal-safe functions (no getenv, no allocation, no stdio).
char g_crashNotePath[1024] = {};

void SetCrashNotePath() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string base;
    if (xdg && *xdg) base = xdg;
    else if (home && *home) base = std::string(home) + "/.local/share";
    else return;
    const std::string path = base + "/Explorer2/Logs/crash-" + std::to_string(::getpid()) + ".txt";
    if (path.size() < sizeof(g_crashNotePath)) std::memcpy(g_crashNotePath, path.c_str(), path.size() + 1);
}

// SIGSEGV, SIGBUS, SIGFPE, SIGILL and SIGABRT: one line to stderr and to the crash note, then the default action,
// so that the process still ends (and dumps core where that is enabled) as it would have without this handler.
// The process is in an unknown state here: only write, open, close, signal and raise are called.
void FatalSignalHandler(int sig) {
    char line[64];
    size_t n = 0;
    for (const char* p = "explorer2: fatal signal "; *p; ++p) line[n++] = *p;
    char digits[12];
    size_t nd = 0;
    unsigned v = (sig > 0) ? static_cast<unsigned>(sig) : 0u;
    do {
        digits[nd++] = static_cast<char>('0' + v % 10u);
        v /= 10u;
    } while (v != 0 && nd < sizeof(digits));
    while (nd > 0) line[n++] = digits[--nd];
    line[n++] = '\n';
    ssize_t wr = ::write(STDERR_FILENO, line, n);
    if (g_crashNotePath[0]) {
        const int fd = ::open(g_crashNotePath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (fd >= 0) {
            wr = ::write(fd, line, n);
            ::close(fd);
        }
    }
    (void)wr;
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void InstallFatalSignalHandlers() {
    SetCrashNotePath();
    // A stack of its own for the handler on the main thread, so that a stack overflow there is reported too
    static char s_altStack[65536];
    stack_t ss = {};
    ss.ss_sp = s_altStack;
    ss.ss_size = sizeof(s_altStack);
    ss.ss_flags = 0;
    (void)::sigaltstack(&ss, nullptr);
    struct sigaction sa = {};
    sa.sa_handler = FatalSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND | SA_ONSTACK;
    for (int sig : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT }) (void)::sigaction(sig, &sa, nullptr);
}

} // namespace

int main(int argc, char** argv) {
    // A write to a pipe or socket whose reader is gone (a curl upload that failed, a tool that exited early) has to
    // fail with EPIPE, as WriteFile fails on Windows, instead of killing the whole program. The programs started
    // from here get the default back before exec (CreateProcessW, ToolProcess::Run): an ignored signal is inherited.
    ::signal(SIGPIPE, SIG_IGN);
    InstallFatalSignalHandlers();

    Explorer2::Hal::SetCommandLineFromMain(argc, argv);

    // Build lpCmdLine (everything after argv[0], matching Win32 wWinMain semantics)
    std::wstring full = GetCommandLineW();
    const wchar_t* p = full.c_str();
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') {
            if (*p == L'\\' && p[1]) ++p;
            ++p;
        }
        if (*p == L'"') ++p;
    } else {
        while (*p && *p != L' ' && *p != L'\t') ++p;
    }
    while (*p == L' ' || *p == L'\t') ++p;
    std::wstring argsOnly(p);

    return wWinMain(GetModuleHandleW(nullptr), nullptr, argsOnly.data(), SW_SHOWNORMAL);
}
