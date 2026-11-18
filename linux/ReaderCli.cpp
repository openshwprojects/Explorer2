// ReaderCli.cpp - Explorer2's file readers without the window: a command-line runner for Linux.
//
//   explorer2-readers FILE...                        what each reader makes of each file, one line per reader
//   explorer2-readers --inspect-files LIST RESULT    the protocol of `Explorer2.exe --inspect-files`, so that
//                                                    tools/fuzz_samples.py --exe <this program> drives it unchanged
//
// Which readers run on a file, in which order, and what the summary says come from src/ReaderInspect.h, shared with
// the Windows harness. This file only adds what POSIX does differently: signals instead of structured exceptions,
// and a watchdog thread that ends the process when a reader does not return.
#include "ReaderGuard.h"
#include "ReaderInspect.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <functional>
#include <string>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#include <vector>

using namespace Explorer2;

namespace {

constexpr uint64_t kHangMs = 20000;            // a reader that takes longer than this does not return

int g_out = 2;                                 // where the X and H lines go: the result file, or stderr
std::atomic<long> g_index{ -1 };               // the file and the reader at work, for those lines
std::atomic<const char*> g_reader{ "(start-up)" };
std::atomic<uint64_t> g_stepStart{ 0 };        // when the reader at work started (ms), 0 while none is
char g_altStack[64 * 1024];                    // the crash handler's own stack: a reader may have used up its own

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()) + 1;
}

void WriteAll(int fd, const char* data, size_t n) {
    while (n > 0) {
        const ssize_t written = write(fd, data, n);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return;
        data += written;
        n -= static_cast<size_t>(written);
    }
}

// A line put together without allocating memory, as a signal handler must
struct RawLine {
    char text[384];
    size_t len = 0;
    RawLine& Put(const char* s) {
        while (s && *s && len < sizeof(text)) text[len++] = *s++;
        return *this;
    }
    RawLine& Num(long long v, unsigned base = 10) {
        if (v < 0) { Put("-"); v = -v; }
        char digits[24];
        size_t k = 0;
        unsigned long long u = static_cast<unsigned long long>(v);
        do { digits[k++] = "0123456789ABCDEF"[u % base]; u /= base; } while (u && k < sizeof(digits));
        while (k && len < sizeof(text)) text[len++] = digits[--k];
        return *this;
    }
};

const char* SignalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGABRT: return "SIGABRT";
    case SIGTRAP: return "SIGTRAP";
    default: return "SIGNAL";
    }
}

// A reader crashed: the X line names the file, the reader, the signal and the place (module and offset in it, which
// `addr2line -e explorer2-readers 0x<offset>` turns into a source line), and the process ends
void OnCrash(int sig, siginfo_t*, void* context) {
    uintptr_t pc = 0;
#if defined(__x86_64__)
    pc = static_cast<uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_RIP]);
#elif defined(__i386__)
    pc = static_cast<uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_EIP]);
#elif defined(__aarch64__)
    pc = static_cast<uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext.pc);
#else
    (void)context;
#endif
    const char* module = "?";
    uintptr_t offset = pc;
    Dl_info where{};
    if (pc && dladdr(reinterpret_cast<void*>(pc), &where) && where.dli_fname && *where.dli_fname) {
        const char* slash = std::strrchr(where.dli_fname, '/');
        module = slash ? slash + 1 : where.dli_fname;
        offset = pc - reinterpret_cast<uintptr_t>(where.dli_fbase);
    }
    RawLine line;
    line.Put("X\t").Num(g_index.load()).Put("\t").Put(g_reader.load()).Put("\t").Put(SignalName(sig)).Put("\t")
        .Put(module).Put("\t").Num(static_cast<long long>(offset), 16).Put("\n");
    WriteAll(g_out, line.text, line.len);
    _exit(3);
}

void InstallCrashHandlers() {
    stack_t alt{};
    alt.ss_sp = g_altStack;
    alt.ss_size = sizeof(g_altStack);
    sigaltstack(&alt, nullptr);
    struct sigaction action{};
    action.sa_sigaction = OnCrash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int sig : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP }) sigaction(sig, &action, nullptr);
}

// A reader that never returns is a fault too: the H line names it, and the process ends
void StartWatchdog() {
    std::thread([]() {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const uint64_t started = g_stepStart.load();
            if (started != 0 && NowMs() - started > kHangMs) {
                RawLine line;
                line.Put("H\t").Num(g_index.load()).Put("\t").Put(g_reader.load()).Put("\n");
                WriteAll(g_out, line.text, line.len);
                _exit(4);
            }
        }
    }).detach();
}

// What happened when one reader ran
struct StepResult {
    bool accepted = false;
    bool threw = false;            // it let an exception escape, or its own safety net (ReaderGuard) stopped one
    std::string thrown;
    uint64_t ms = 0;
};

StepResult RunStep(size_t i, const char* reader, const std::function<bool()>& run) {
    g_index = static_cast<long>(i);
    g_reader = reader;
    StepResult result;
    const uint64_t started = NowMs();
    g_stepStart = started;
    const uint64_t stoppedBefore = ReaderGuard::StoppedCount();
    try {
        result.accepted = run();
    } catch (const std::exception& e) {
        result.threw = true;
        result.thrown = e.what();
    } catch (...) {
        result.threw = true;
        result.thrown = "unknown C++ exception";
    }
    if (!result.threw && ReaderGuard::StoppedCount() != stoppedBefore) {
        result.threw = true;
        result.thrown = ReaderGuard::LastMessage();
    }
    g_stepStart = 0;
    result.ms = NowMs() - started;
    if (result.thrown.size() > 300) result.thrown.resize(300);
    return result;
}

// --inspect-files LIST RESULT: for each file, B when a reader starts, then E (accepted or not, and how long it took)
// and S (what it made of the file), or T when it let an exception escape; N for a file no reader claims; D at the
// end. A crash or a hang ends the process with an X or H line, and the driver goes on behind that file.
int InspectFiles(const char* listFile, const char* resultFile) {
    std::vector<std::wstring> paths;
    if (!ReaderInspect::ReadPathList(PathUtils::Utf8ToWide(listFile), paths)) return 2;
    g_out = open(resultFile, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (g_out < 0) return 2;
    auto writeLine = [](const std::string& s) { WriteAll(g_out, s.data(), s.size()); };

    for (size_t i = 0; i < paths.size(); ++i) {
        const std::wstring& path = paths[i];
        bool anyReader = false;
        std::string summary;
        auto step = [&](const char* reader, const std::function<bool()>& run) {
            anyReader = true;
            summary.clear();
            writeLine("B\t" + std::to_string(i) + "\t" + reader + "\n");
            const StepResult r = RunStep(i, reader, run);
            if (r.threw) {
                writeLine("T\t" + std::to_string(i) + "\t" + reader + "\t" + r.thrown + "\n");
                return;
            }
            writeLine("E\t" + std::to_string(i) + "\t" + reader + "\t" + (r.accepted ? "1" : "0") + "\t" + std::to_string(r.ms) + "\n");
            if (!summary.empty()) writeLine(ReaderInspect::SummaryLine(i, reader, summary));
        };
        if (ReaderInspect::IsSelfCheckFile(path)) {
            step("selfcheck", [&]() { return ReaderInspect::RunSelfCheckReader(path); });
            continue;
        }
        ReaderInspect::RunReaders(path, i, summary, step);
        if (!anyReader) writeLine("N\t" + std::to_string(i) + "\n");
    }
    writeLine("D\n");
    close(g_out);
    g_out = 2;
    return 0;
}

// FILE...: one line per reader that claims a file. Exit code 0 when some reader read every file.
int ShowFiles(int count, char** files) {
    int unread = 0;
    for (int k = 0; k < count; ++k) {
        const std::wstring path = PathUtils::Utf8ToWide(files[k]);
        std::printf("%s\n", files[k]);
        bool anyReader = false;
        bool read = false;
        std::string summary;
        auto step = [&](const char* reader, const std::function<bool()>& run) {
            anyReader = true;
            summary.clear();
            const StepResult r = RunStep(static_cast<size_t>(k), reader, run);
            if (r.threw) {
                std::printf("  %-8s exception: %s\n", reader, r.thrown.c_str());
            } else {
                read = read || r.accepted;
                std::printf("  %-8s %-11s %5llu ms  %s\n", reader, r.accepted ? "read" : "turned down",
                            static_cast<unsigned long long>(r.ms), summary.c_str());
            }
            std::fflush(stdout);
        };
        ReaderInspect::RunReaders(path, static_cast<size_t>(k), summary, step);
        if (!anyReader) std::printf("  (no reader for this kind of file)\n");
        if (!read) ++unread;
    }
    return unread == 0 ? 0 : 1;
}

void Usage() {
    std::fprintf(stderr,
                 "Explorer2's file readers (images, 3D models, boards, documents, archives) without the window.\n"
                 "\n"
                 "  explorer2-readers FILE...                       what each reader makes of each file\n"
                 "  explorer2-readers --inspect-files LIST RESULT   the protocol of Explorer2.exe --inspect-files\n"
                 "                                                  (driven by tools/fuzz_samples.py --exe)\n"
                 "\n"
                 "Environment: EXPLORER2_INSPECT_PIXELS=<folder> saves decoded images as <index>.bgra,\n"
                 "EXPLORER2_INSPECT_DOCS=<folder> saves document text as <index>.txt,\n"
                 "EXPLORER2_LOG_STDERR=1 prints the readers' log.\n");
}

} // namespace

int main(int argc, char** argv) {
    InstallCrashHandlers();
    StartWatchdog();
    if (argc == 4 && std::strcmp(argv[1], "--inspect-files") == 0) return InspectFiles(argv[2], argv[3]);
    if (argc < 2 || argv[1][0] == '-') {
        Usage();
        return 2;
    }
    return ShowFiles(argc - 1, argv + 1);
}
