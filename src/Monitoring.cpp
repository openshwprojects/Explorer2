#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sddl.h>
#include <pdh.h>
#include <pdhmsg.h>

#include "Monitoring.h"
#include "HardwareInfo.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#ifndef _WIN32
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#pragma comment(lib, "pdh.lib")

namespace Explorer2 {

namespace {

constexpr const wchar_t* kDaemonMutexName = L"Local\\Explorer2.MonitorDaemon";
constexpr const wchar_t* kDaemonStopEventName = L"Local\\Explorer2.MonitorStop";
constexpr const wchar_t* kRunKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValueName = L"Explorer2Monitor";
constexpr const wchar_t* kTaskName = L"Explorer2Monitor";
constexpr const char* kCsvHeader =
    "ts,cpu,ram,ram_mb,gpu_load,gpu_temp,gpu_vram_mb,acpi_temp,rx_kbps,tx_kbps,disks,volumes,commit,procs,gpu_power,gpu_fan,gpu_clock\n";
constexpr const char* kSmartHeader =
    "ts,disk,health,temp,life,poh,cycles,written_gb,read_gb,realloc,pending,uncorrectable,crc,unsafe_shutdowns,media_errors,spare\n";

// Named objects must be reachable from a normal (medium integrity) window even when the recorder runs elevated:
// DACL grants the interactive user, the mandatory label is lowered to "low" so lower-integrity callers may write.
struct SharedSecurity {
    SECURITY_ATTRIBUTES sa = {};
    PSECURITY_DESCRIPTOR sd = nullptr;
    SharedSecurity() {
        ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;BA)(A;;GA;;;SY)(A;;GA;;;IU)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr);
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;
    }
    ~SharedSecurity() { if (sd) LocalFree(sd); }
    SECURITY_ATTRIBUTES* get() { return sd ? &sa : nullptr; }
};

std::wstring SettingsPath() {
    return MonitorStore::DataDir() + L"\\monitor.ini";
}

int DayKey(const tm& local) {
    return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

std::wstring DayFilePath(const wchar_t* prefix, const tm& local) {
    wchar_t name[64] = {};
    swprintf_s(name, L"\\%s-%04d-%02d-%02d.csv", prefix, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return MonitorStore::DataDir() + name;
}

tm LocalTime(int64_t ts) {
    tm out = {};
    time_t t = static_cast<time_t>(ts);
    localtime_s(&out, &t);
    return out;
}

std::string FormatLine(const MonitorSample& s) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%lld,%.1f,%.1f,%d,%.1f,%d,%d,%d,%.1f,%.1f,",
             static_cast<long long>(s.ts), s.cpuPct, s.ramPct, s.ramUsedMB, s.gpuLoadPct,
             s.gpuTempC, s.gpuVramMB, s.acpiTempC, s.rxKBps, s.txKBps);
    std::string line = buf;
    for (size_t i = 0; i < s.disks.size(); ++i) {
        const auto& d = s.disks[i];
        snprintf(buf, sizeof(buf), "%s%d|%d|%d|%.0f|%.0f|%.0f", i ? ";" : "", d.index, d.tempC, d.health,
                 d.readKBps, d.writeKBps, d.busyPct);
        line += buf;
    }
    line += ',';
    for (size_t i = 0; i < s.volumes.size(); ++i) {
        snprintf(buf, sizeof(buf), "%s%c|%.1f|%.1f", i ? ";" : "", static_cast<char>(s.volumes[i].letter),
                 s.volumes[i].usedPct, s.volumes[i].freeGB);
        line += buf;
    }
    snprintf(buf, sizeof(buf), ",%.1f,%d,%.1f,%d,%d\n", s.commitPct, s.procCount, s.gpuPowerW, s.gpuFanPct, s.gpuClockMHz);
    line += buf;
    return line;
}

std::string FormatSmartLine(const MonitorSmartPoint& p) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%lld,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d\n",
             static_cast<long long>(p.ts), p.disk, p.health, p.tempC, p.lifePct,
             static_cast<long long>(p.powerOnHours), static_cast<long long>(p.powerCycles),
             static_cast<long long>(p.writtenGB), static_cast<long long>(p.readGB),
             static_cast<long long>(p.reallocated), static_cast<long long>(p.pending),
             static_cast<long long>(p.uncorrectable), static_cast<long long>(p.crcErrors),
             static_cast<long long>(p.unsafeShutdowns), static_cast<long long>(p.mediaErrors), p.spare);
    return buf;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

bool ParseLine(const std::string& line, MonitorSample& out) {
    if (line.empty() || line[0] < '0' || line[0] > '9') return false;   // header / garbage
    auto f = Split(line, ',');
    if (f.size() < 12) return false;                                      // partially written row
    MonitorSample s;
    s.ts = _atoi64(f[0].c_str());
    if (s.ts <= 0) return false;
    s.cpuPct = static_cast<float>(atof(f[1].c_str()));
    s.ramPct = static_cast<float>(atof(f[2].c_str()));
    s.ramUsedMB = atoi(f[3].c_str());
    s.gpuLoadPct = static_cast<float>(atof(f[4].c_str()));
    s.gpuTempC = atoi(f[5].c_str());
    s.gpuVramMB = atoi(f[6].c_str());
    s.acpiTempC = atoi(f[7].c_str());
    s.rxKBps = static_cast<float>(atof(f[8].c_str()));
    s.txKBps = static_cast<float>(atof(f[9].c_str()));
    if (!f[10].empty()) {
        for (const auto& item : Split(f[10], ';')) {
            auto p = Split(item, '|');
            if (p.size() < 3) continue;
            MonitorDiskPoint d;
            d.index = atoi(p[0].c_str());
            d.tempC = atoi(p[1].c_str());
            d.health = atoi(p[2].c_str());
            if (p.size() >= 6) {   // columns added in later versions; older rows simply lack them
                d.readKBps = static_cast<float>(atof(p[3].c_str()));
                d.writeKBps = static_cast<float>(atof(p[4].c_str()));
                d.busyPct = static_cast<float>(atof(p[5].c_str()));
            }
            s.disks.push_back(d);
        }
    }
    std::string vols = f[11];
    while (!vols.empty() && (vols.back() == '\r' || vols.back() == '\n')) vols.pop_back();
    if (!vols.empty()) {
        for (const auto& item : Split(vols, ';')) {
            auto p = Split(item, '|');
            if (p.size() < 3 || p[0].empty()) continue;
            MonitorVolumePoint v;
            v.letter = static_cast<wchar_t>(p[0][0]);
            v.usedPct = static_cast<float>(atof(p[1].c_str()));
            v.freeGB = static_cast<float>(atof(p[2].c_str()));
            s.volumes.push_back(v);
        }
    }
    if (f.size() >= 17) {
        s.commitPct = static_cast<float>(atof(f[12].c_str()));
        s.procCount = atoi(f[13].c_str());
        s.gpuPowerW = static_cast<float>(atof(f[14].c_str()));
        s.gpuFanPct = atoi(f[15].c_str());
        s.gpuClockMHz = atoi(f[16].c_str());
    }
    out = std::move(s);
    return true;
}

bool ParseSmartLine(const std::string& line, MonitorSmartPoint& out) {
    if (line.empty() || line[0] < '0' || line[0] > '9') return false;
    auto f = Split(line, ',');
    if (f.size() < 16) return false;
    MonitorSmartPoint p;
    p.ts = _atoi64(f[0].c_str());
    if (p.ts <= 0) return false;
    p.disk = atoi(f[1].c_str());
    p.health = atoi(f[2].c_str());
    p.tempC = atoi(f[3].c_str());
    p.lifePct = atoi(f[4].c_str());
    p.powerOnHours = _atoi64(f[5].c_str());
    p.powerCycles = _atoi64(f[6].c_str());
    p.writtenGB = _atoi64(f[7].c_str());
    p.readGB = _atoi64(f[8].c_str());
    p.reallocated = _atoi64(f[9].c_str());
    p.pending = _atoi64(f[10].c_str());
    p.uncorrectable = _atoi64(f[11].c_str());
    p.crcErrors = _atoi64(f[12].c_str());
    p.unsafeShutdowns = _atoi64(f[13].c_str());
    p.mediaErrors = _atoi64(f[14].c_str());
    p.spare = atoi(f[15].c_str());
    out = p;
    return true;
}

// Appends text to a file other processes may be reading at the same time (single WriteFile call per row)
bool AppendText(const std::wstring& path, const char* header, const std::string& line) {
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
#ifndef _WIN32
    // The header goes only into a file whose size was read as 0: a size that cannot be read must not put a header
    // line between two rows. (The Win32 layer opens a FILE_APPEND_DATA handle with O_APPEND and without a stdio
    // buffer, so the single WriteFile below is a single write(2) at the end of the file.)
    if (!GetFileSizeEx(h, &size)) size.QuadPart = 1;
#else
    GetFileSizeEx(h, &size);
#endif
    std::string payload;
    if (size.QuadPart == 0) payload = header;
    payload += line;
    DWORD written = 0;
    bool ok = WriteFile(h, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) != FALSE;
    CloseHandle(h);
    return ok;
}

// Reads bytes [offset, end) of a file (shared with the writer); returns the file size through outSize
bool ReadFileTail(const std::wstring& path, uint64_t offset, std::string& out, uint64_t& outSize) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    outSize = static_cast<uint64_t>(size.QuadPart);
    out.clear();
    if (outSize > offset) {
        LARGE_INTEGER pos;
        pos.QuadPart = static_cast<LONGLONG>(offset);
        SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
        out.resize(static_cast<size_t>(outSize - offset));
        DWORD got = 0;
        ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
        out.resize(got);
    }
    CloseHandle(h);
    return true;
}

// ---- recorder helpers -------------------------------------------------------------------------

// \PhysicalDisk(*) throughput + busy time through PDH (works without elevation)
class DiskIoSampler {
public:
    struct Io { int index = -1; float readKBps = 0, writeKBps = 0, busyPct = 0; };

    DiskIoSampler() {
        if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS) { m_query = nullptr; return; }
        bool ok = PdhAddEnglishCounterW(m_query, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", 0, &m_read) == ERROR_SUCCESS &&
                  PdhAddEnglishCounterW(m_query, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", 0, &m_write) == ERROR_SUCCESS &&
                  PdhAddEnglishCounterW(m_query, L"\\PhysicalDisk(*)\\% Disk Time", 0, &m_busy) == ERROR_SUCCESS;
        if (!ok) { PdhCloseQuery(m_query); m_query = nullptr; }
    }
    ~DiskIoSampler() { if (m_query) PdhCloseQuery(m_query); }

    void Collect() { if (m_query) PdhCollectQueryData(m_query); }

    std::vector<Io> Read() {
        std::map<int, Io> byIndex;
        if (m_query) {
            fill(m_read, byIndex, 0);
            fill(m_write, byIndex, 1);
            fill(m_busy, byIndex, 2);
        }
        std::vector<Io> out;
        for (auto& kv : byIndex) out.push_back(kv.second);
        return out;
    }

private:
    void fill(PDH_HCOUNTER counter, std::map<int, Io>& dst, int field) {
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != PDH_MORE_DATA || bytes == 0) return;
        std::vector<uint8_t> buf(bytes);
        auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) return;
        for (DWORD i = 0; i < count; ++i) {
            const wchar_t* n = items[i].szName;
            if (!n || n[0] < L'0' || n[0] > L'9') continue;                  // skip "_Total"
            if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
            int idx = _wtoi(n);
            Io& io = dst[idx];
            io.index = idx;
            double v = items[i].FmtValue.doubleValue;
            if (field == 0) io.readKBps = static_cast<float>(v / 1024.0);
            else if (field == 1) io.writeKBps = static_cast<float>(v / 1024.0);
            else io.busyPct = static_cast<float>(std::clamp(v, 0.0, 100.0));
        }
    }

    PDH_HQUERY m_query = nullptr;
    PDH_HCOUNTER m_read = nullptr, m_write = nullptr, m_busy = nullptr;
};

const HwSmartAttr* FindAttrById(const std::vector<HwSmartAttr>& v, int id) {
    for (const auto& a : v) if (a.id == id) return &a;
    return nullptr;
}
const HwSmartAttr* FindAttrByName(const std::vector<HwSmartAttr>& v, const wchar_t* name) {
    for (const auto& a : v) if (_wcsicmp(a.name.c_str(), name) == 0) return &a;
    return nullptr;
}

MonitorSmartPoint BuildSmartPoint(const HwPhysicalDisk& d, int64_t ts) {
    MonitorSmartPoint p;
    p.ts = ts;
    p.disk = d.index;
    p.tempC = d.tempC;
    p.health = (d.smartState == HwSmartState::Ok) ? d.healthLevel : -1;
    p.lifePct = d.lifeRemainingPct;
    p.powerOnHours = d.powerOnHours;
    p.powerCycles = d.powerCycles;
    p.writtenGB = d.bytesWritten >= 0 ? d.bytesWritten / (1024ll * 1024ll * 1024ll) : -1;
    p.readGB = d.bytesRead >= 0 ? d.bytesRead / (1024ll * 1024ll * 1024ll) : -1;
    if (d.smartState != HwSmartState::Ok) return p;

    auto low32 = [](const HwSmartAttr* a) -> int64_t { return a ? static_cast<int64_t>(a->raw & 0xFFFFFFFFull) : -1; };
    if (d.isNvme) {
        p.unsafeShutdowns = low32(FindAttrByName(d.smartAttrs, L"Unsafe Shutdowns"));
        p.mediaErrors = low32(FindAttrByName(d.smartAttrs, L"Media and Data Integrity Errors"));
        if (const auto* a = FindAttrByName(d.smartAttrs, L"Available Spare")) p.spare = static_cast<int>(a->raw);
    } else {
        p.reallocated = low32(FindAttrById(d.smartAttrs, 5));
        p.pending = low32(FindAttrById(d.smartAttrs, 197));
        p.uncorrectable = low32(FindAttrById(d.smartAttrs, 198));
        p.crcErrors = low32(FindAttrById(d.smartAttrs, 199));
        p.unsafeShutdowns = low32(FindAttrById(d.smartAttrs, 192));
    }
    return p;
}

MonitorSample SampleNow(const HardwareInspector& hw, const std::vector<DiskIoSampler::Io>& io) {
    MonitorSample s;
    s.ts = static_cast<int64_t>(time(nullptr));

    s.cpuPct = hw.GetCpu().totalLoadPct;
    const auto& ram = hw.GetRam();
    s.ramPct = ram.usedPct;
    s.ramUsedMB = static_cast<int>(ram.usedBytes / (1024ull * 1024ull));
    if (ram.commitLimitBytes > 0) {
        s.commitPct = static_cast<float>(100.0 * static_cast<double>(ram.commitTotalBytes) / static_cast<double>(ram.commitLimitBytes));
    }
    s.procCount = static_cast<int>(hw.GetSystem().processCount);

    for (const auto& g : hw.GetGpus()) {
        if (s.gpuVramMB < 0) s.gpuVramMB = static_cast<int>(g.liveVramUsedBytes / (1024ull * 1024ull));
        if (g.hasNvmlTelemetry) {
            s.gpuLoadPct = static_cast<float>(g.gpuLoadPct);
            s.gpuTempC = g.tempCelsius > 0 ? static_cast<int>(g.tempCelsius) : -1;
            s.gpuVramMB = static_cast<int>(g.liveVramUsedBytes / (1024ull * 1024ull));
            s.gpuPowerW = g.powerDrawWatts;
            s.gpuFanPct = static_cast<int>(g.fanSpeedPct);
            s.gpuClockMHz = static_cast<int>(g.coreClockMHz);
            break;
        }
    }

    float hottest = -1.0f;
    for (const auto& z : hw.GetAcpiThermalZones()) hottest = std::max(hottest, z.tempC);
    if (hottest > 0.0f) s.acpiTempC = static_cast<int>(hottest + 0.5f);

    double rx = 0.0, tx = 0.0;
    for (const auto& n : hw.GetNetworks()) {
        rx += n.liveRxBytesPerSec / 1024.0;
        tx += n.liveTxBytesPerSec / 1024.0;
    }
    s.rxKBps = static_cast<float>(rx);
    s.txKBps = static_cast<float>(tx);

    for (const auto& d : hw.GetPhysicalDisks()) {
        MonitorDiskPoint p;
        p.index = d.index;
        p.tempC = d.tempC;
        p.health = (d.smartState == HwSmartState::Ok) ? d.healthLevel : -1;
        for (const auto& x : io) {
            if (x.index == d.index) { p.readKBps = x.readKBps; p.writeKBps = x.writeKBps; p.busyPct = x.busyPct; break; }
        }
        s.disks.push_back(p);
    }

    wchar_t drives[512] = {};
    DWORD len = GetLogicalDriveStringsW(511, drives);
    for (const wchar_t* p = drives; p < drives + len && *p; p += wcslen(p) + 1) {
        if (GetDriveTypeW(p) != DRIVE_FIXED) continue;      // never touch network / optical drives from the background
        ULARGE_INTEGER avail = {}, total = {}, freeB = {};
        if (!GetDiskFreeSpaceExW(p, &avail, &total, &freeB) || total.QuadPart == 0) continue;
        MonitorVolumePoint v;
        v.letter = p[0];
        v.freeGB = static_cast<float>(static_cast<double>(freeB.QuadPart) / (1024.0 * 1024.0 * 1024.0));
        v.usedPct = static_cast<float>(100.0 * static_cast<double>(total.QuadPart - freeB.QuadPart) / static_cast<double>(total.QuadPart));
        s.volumes.push_back(v);
    }
    return s;
}

// ---- process helpers for autostart / elevation --------------------------------------------------

std::wstring SelfExePath() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return exe;
}

#ifdef _WIN32
// Runs a program elevated (UAC prompt) and waits for it
bool RunElevatedAndWait(const std::wstring& file, const std::wstring& params, DWORD& exitCode) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb = L"runas";
    sei.lpFile = file.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false;
    bool ok = WaitForSingleObject(sei.hProcess, 30000) == WAIT_OBJECT_0 && GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return ok;
}

bool TaskExists() {
    HKEY hKey = nullptr;
    std::wstring regPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\TaskCache\\Tree\\" + std::wstring(kTaskName);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    std::wstring taskFile = L"C:\\Windows\\System32\\Tasks\\" + std::wstring(kTaskName);
    DWORD attr = GetFileAttributesW(taskFile.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}
#endif

#ifndef _WIN32
// ---- autostart (Linux) ------------------------------------------------------------------------------
// The desktop starts at login what ~/.config/autostart lists (the XDG autostart rule that GNOME, KDE, Xfce and the
// others follow): the counterpart of the Run key used on Windows
std::string XdgAutostartEntryPath() {
    std::string base;
    const char* config = std::getenv("XDG_CONFIG_HOME");
    if (config && config[0] == '/') {
        base = config;
    } else {
        const char* home = std::getenv("HOME");
        if (!home || home[0] != '/') return std::string();
        base = std::string(home) + "/.config";
    }
    return base + "/autostart/explorer2-monitor.desktop";
}

// One argument of a desktop entry's Exec line, quoted, with the escapes the Desktop Entry Specification asks for:
// '"', '`' and '$' get a backslash inside the quotes, and every backslash is doubled once more for the file
std::string DesktopExecArgument(const std::string& arg) {
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"' || c == '`' || c == '$') {
            out += "\\\\";
            out.push_back(c);
        } else if (c == '\\') {
            out += "\\\\\\\\";
        } else if (c == '%') {
            out += "%%";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

// ---- recorder lock and stop request (Linux) --------------------------------------------------------
// The Win32 layer keeps named mutexes and events inside one process, so a window could neither see a running recorder
// nor stop it, and every start of a window added one more recorder. Instead the recorder holds an exclusive flock() on
// a lock file for its whole life and keeps its process id in that file. The kernel drops the lock when the recorder
// exits or crashes, so a file left behind never makes a dead recorder look alive.

const std::string& DaemonLockPath() {
    static const std::string path = []() {
        const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
        struct stat st = {};
        if (runtimeDir && runtimeDir[0] == '/' && ::stat(runtimeDir, &st) == 0 && S_ISDIR(st.st_mode)) {
            return std::string(runtimeDir) + "/explorer2-monitor.lock";
        }
        return Compat::NativePath((MonitorStore::DataDir() + L"\\explorer2-monitor.lock").c_str());
    }();
    return path;
}

constexpr int kLockHeldByOther = -2;

// Takes the recorder lock for this process. Returns the descriptor that holds it (kept open while the recorder runs),
// kLockHeldByOther when another recorder runs, or -1 when the lock file cannot be used.
int AcquireDaemonLock() {
    const int fd = ::open(DaemonLockPath().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    // A window that checks whether a recorder runs holds a shared lock for a few microseconds, and that refuses an
    // exclusive one too. Only a recorder keeps the lock, so a refusal that lasts half a second means one runs.
    int lastError = 0;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
            char text[32] = {};
            const int len = std::snprintf(text, sizeof(text), "%ld\n", static_cast<long>(::getpid()));
            if (len > 0 && ::pwrite(fd, text, static_cast<size_t>(len), 0) == len) {
                const int truncated = ::ftruncate(fd, len);   // drops the end of a longer process id written before
                (void)truncated;
            }
            return fd;
        }
        lastError = errno;
        if (lastError != EWOULDBLOCK) break;
        Sleep(10);
    }
    ::close(fd);
    return (lastError == EWOULDBLOCK) ? kLockHeldByOther : -1;
}

// True while some process holds the recorder lock
bool DaemonLockHeld() {
    const int fd = ::open(DaemonLockPath().c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;   // no file: no recorder has run yet
    const bool held = ::flock(fd, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    ::close(fd);                // also gives the shared lock back when it was granted
    return held;
}

// Whether /proc says that process was started as a recorder (its arguments are separated by NUL characters)
bool IsRecorderProcess(pid_t pid) {
    char path[64] = {};
    std::snprintf(path, sizeof(path), "/proc/%ld/cmdline", static_cast<long>(pid));
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[4096];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    if (n <= 0) return false;
    const std::string args(buf, static_cast<size_t>(n));
    size_t start = 0;
    while (start < args.size()) {
        size_t end = args.find('\0', start);
        if (end == std::string::npos) end = args.size();
        if (args.compare(start, end - start, "--monitor-daemon") == 0) return true;
        start = end + 1;
    }
    return false;
}

// The process id kept in the lock file, or 0. It is read only while the lock is held and trusted only when that
// process is a recorder, so an id the system has given to another program since is never signalled.
pid_t RunningDaemonPid() {
    const int fd = ::open(DaemonLockPath().c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    pid_t pid = 0;
    if (::flock(fd, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK) {
        char text[32] = {};
        const ssize_t n = ::pread(fd, text, sizeof(text) - 1, 0);
        const long value = (n > 0) ? std::strtol(text, nullptr, 10) : 0;
        if (value > 1 && IsRecorderProcess(static_cast<pid_t>(value))) pid = static_cast<pid_t>(value);
    }
    ::close(fd);
    return pid;
}

// RequestStop() sends SIGTERM. The handler only sets a flag and writes one byte into a pipe, which is all a signal
// handler may safely do; the recorder sleeps in poll() on that pipe, so it wakes at once and leaves its loop between
// two rows instead of being killed in the middle of one.
volatile sig_atomic_t g_stopRequested = 0;
int g_stopPipe[2] = { -1, -1 };

void OnStopSignal(int) {
    const int savedErrno = errno;
    g_stopRequested = 1;
    if (g_stopPipe[1] >= 0) {
        const char wake = 1;
        const ssize_t written = ::write(g_stopPipe[1], &wake, 1);
        (void)written;
    }
    errno = savedErrno;
}

void InstallStopHandler() {
    if (::pipe2(g_stopPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        g_stopPipe[0] = -1;
        g_stopPipe[1] = -1;
    }
    struct sigaction action = {};
    action.sa_handler = OnStopSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
    // The signal mask is inherited through fork() and exec(): the window's thread that started this process may
    // have had these two blocked
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    ::pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
}

// Sleeps up to ms milliseconds; returns true as soon as the recorder was asked to stop
bool WaitForStopRequest(DWORD ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!g_stopRequested) {
        const long long left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return false;
        if (g_stopPipe[0] < 0) {
            Sleep(static_cast<DWORD>(std::min<long long>(left, 250)));   // no pipe: look at the flag 4 times a second
            continue;
        }
        pollfd pfd = {};
        pfd.fd = g_stopPipe[0];
        pfd.events = POLLIN;
        ::poll(&pfd, 1, static_cast<int>(left));   // the byte, a signal or the time: the flag decides
    }
    return true;
}
#endif

} // namespace

// =============================================================================================
// MonitorStore
// =============================================================================================

std::wstring MonitorStore::DataDir() {
    std::wstring dir;
    PWSTR base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        dir = base;
        CoTaskMemFree(base);
    } else {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        dir = tmp;
        while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
    }
    dir += L"\\Explorer2\\Monitoring";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

MonitorSettings MonitorStore::LoadSettings() {
    MonitorSettings s;
    std::wstring path = SettingsPath();
    s.enabled = GetPrivateProfileIntW(L"Monitor", L"Enabled", 0, path.c_str()) != 0;
    s.elevated = GetPrivateProfileIntW(L"Monitor", L"Elevated", 0, path.c_str()) != 0;
    s.intervalSec = static_cast<int>(GetPrivateProfileIntW(L"Monitor", L"IntervalSec", 60, path.c_str()));
    s.retentionDays = static_cast<int>(GetPrivateProfileIntW(L"Monitor", L"RetentionDays", 90, path.c_str()));
    s.intervalSec = std::clamp(s.intervalSec, 5, 3600);
    s.retentionDays = std::clamp(s.retentionDays, 1, 3650);
    return s;
}

void MonitorStore::SaveSettings(const MonitorSettings& s) {
    std::wstring path = SettingsPath();
    WritePrivateProfileStringW(L"Monitor", L"Enabled", s.enabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Monitor", L"Elevated", s.elevated ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Monitor", L"IntervalSec", std::to_wstring(s.intervalSec).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Monitor", L"RetentionDays", std::to_wstring(s.retentionDays).c_str(), path.c_str());
}

bool MonitorStore::AppendSample(const MonitorSample& s) {
    return AppendText(DayFilePath(L"samples", LocalTime(s.ts)), kCsvHeader, FormatLine(s));
}

bool MonitorStore::AppendSmart(const MonitorSmartPoint& p) {
    return AppendText(DayFilePath(L"smart", LocalTime(p.ts)), kSmartHeader, FormatSmartLine(p));
}

void MonitorStore::PruneOldFiles(int retentionDays) {
    int64_t cutoff = static_cast<int64_t>(time(nullptr)) - static_cast<int64_t>(retentionDays) * 86400;
    int cutoffKey = DayKey(LocalTime(cutoff));

    for (const wchar_t* prefix : { L"samples", L"smart" }) {
        std::wstring pattern = DataDir() + L"\\" + prefix + L"-*.csv";
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            int y = 0, m = 0, d = 0;
            std::wstring fmt = std::wstring(prefix) + L"-%d-%d-%d.csv";
            if (swscanf_s(fd.cFileName, fmt.c_str(), &y, &m, &d) == 3) {
                if (y * 10000 + m * 100 + d < cutoffKey) DeleteFileW((DataDir() + L"\\" + fd.cFileName).c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

std::vector<MonitorSmartPoint> MonitorStore::LoadSmartRange(int64_t fromTs, int64_t toTs) {
    std::vector<MonitorSmartPoint> out;
    if (toTs < fromTs) return out;

    tm cur = LocalTime(fromTs);
    cur.tm_hour = 0; cur.tm_min = 0; cur.tm_sec = 0; cur.tm_isdst = -1;
    for (int guard = 0; guard < 4000; ++guard) {
        time_t dayStart = mktime(&cur);
        if (dayStart == static_cast<time_t>(-1) || static_cast<int64_t>(dayStart) > toTs) break;

#ifndef _WIN32
        std::ifstream in(Compat::NativePath(DayFilePath(L"smart", cur).c_str()), std::ios::binary);
#else
        std::ifstream in(DayFilePath(L"smart", cur), std::ios::binary);
#endif
        if (in) {
            std::string line;
            while (std::getline(in, line)) {
                MonitorSmartPoint p;
                if (ParseSmartLine(line, p) && p.ts >= fromTs && p.ts <= toTs) out.push_back(p);
            }
        }
        cur.tm_mday += 1;
        cur.tm_isdst = -1;
    }
    std::sort(out.begin(), out.end(), [](const MonitorSmartPoint& a, const MonitorSmartPoint& b) { return a.ts < b.ts; });
    return out;
}

uint64_t MonitorStore::TotalBytesOnDisk() {
    uint64_t total = 0;
    for (const wchar_t* prefix : { L"samples", L"smart" }) {
        std::wstring pattern = DataDir() + L"\\" + prefix + L"-*.csv";
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            total += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return total;
}

// =============================================================================================
// MonitorHistory (incremental reader used by the UI)
// =============================================================================================

void MonitorHistory::Clear() {
    m_days.clear();
    m_rows.clear();
    ++m_version;
}

void MonitorHistory::Update(int64_t fromTs, int64_t toTs) {
    bool changed = false;
    std::vector<int> needed;

    tm cur = LocalTime(fromTs);
    cur.tm_hour = 0; cur.tm_min = 0; cur.tm_sec = 0; cur.tm_isdst = -1;
    for (int guard = 0; guard < 4000; ++guard) {
        time_t dayStart = mktime(&cur);
        if (dayStart == static_cast<time_t>(-1) || static_cast<int64_t>(dayStart) > toTs) break;

        int key = DayKey(cur);
        needed.push_back(key);
        std::wstring path = DayFilePath(L"samples", cur);

        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            if (m_days.erase(key) > 0) changed = true;
        } else {
            uint64_t size = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            Day& day = m_days[key];
            if (size < day.parsedBytes) {            // file was replaced / truncated
                day.samples.clear();
                day.parsedBytes = 0;
                changed = true;
            }
            if (size > day.parsedBytes) {
                std::string chunk;
                uint64_t actualSize = 0;
                if (ReadFileTail(path, day.parsedBytes, chunk, actualSize)) {
                    size_t lastNl = chunk.rfind('\n');
                    if (lastNl != std::string::npos) {
                        size_t pos = 0;
                        while (pos <= lastNl) {
                            size_t nl = chunk.find('\n', pos);
                            MonitorSample s;
                            if (ParseLine(chunk.substr(pos, nl - pos), s)) day.samples.push_back(std::move(s));
                            pos = nl + 1;
                        }
                        day.parsedBytes += lastNl + 1;
                        changed = true;
                    }
                }
            }
        }
        cur.tm_mday += 1;
        cur.tm_isdst = -1;
    }

    for (auto it = m_days.begin(); it != m_days.end();) {
        if (std::find(needed.begin(), needed.end(), it->first) == needed.end()) {
            it = m_days.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    std::vector<const MonitorSample*> rows;
    for (const auto& kv : m_days) {
        for (const auto& s : kv.second.samples) {
            if (s.ts >= fromTs && s.ts <= toTs) rows.push_back(&s);
        }
    }
    bool sameRows = rows.size() == m_rows.size() &&
                    (rows.empty() || (rows.front() == m_rows.front() && rows.back() == m_rows.back()));
    if (changed || !sameRows) {
        m_rows.swap(rows);
        ++m_version;
    }
}

// =============================================================================================
// MonitorDaemon
// =============================================================================================

bool MonitorDaemon::IsRunning() {
#ifndef _WIN32
    return DaemonLockHeld();
#else
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kDaemonMutexName);
    if (!m) return false;
    CloseHandle(m);
    return true;
#endif
}

bool MonitorDaemon::Start(bool elevated) {
    if (IsRunning()) return true;
    std::wstring exe = SelfExePath();

    if (elevated) {
        // Standard UAC prompt; the recorder then runs with administrator rights (needed for SATA SMART)
        HINSTANCE r = ShellExecuteW(nullptr, L"runas", exe.c_str(), L"--monitor-daemon", nullptr, SW_HIDE);
        return reinterpret_cast<INT_PTR>(r) > 32;
    }

    std::wstring cmd = L"\"" + exe + L"\" --monitor-daemon";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | BELOW_NORMAL_PRIORITY_CLASS,
                             nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void MonitorDaemon::RequestStop() {
#ifndef _WIN32
    const pid_t pid = RunningDaemonPid();
    if (pid > 0) ::kill(pid, SIGTERM);
#else
    HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, kDaemonStopEventName);
    if (!e) return;
    SetEvent(e);
    CloseHandle(e);
#endif
}

int MonitorDaemon::AutostartMode() {
#ifndef _WIN32
    const std::string entry = XdgAutostartEntryPath();
    struct stat st = {};
    return (!entry.empty() && ::stat(entry.c_str(), &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
#else
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        bool present = RegQueryValueExW(key, kRunValueName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(key);
        if (present) return 1;
    }
    return TaskExists() ? 2 : 0;
#endif
}

void MonitorDaemon::SetAutostart(bool enable, bool elevated) {
#ifndef _WIN32
    // Linux has neither the HKCU Run key nor the Task Scheduler used below (running that code would hand
    // "schtasks.exe" to xdg-open): an XDG autostart entry starts the recorder at login instead. It runs as the user;
    // there is no elevated flavour.
    (void)elevated;
    const std::string entry = XdgAutostartEntryPath();
    if (entry.empty()) return;
    if (!enable) {
        (void)::unlink(entry.c_str());
        return;
    }
    const std::string folder = entry.substr(0, entry.find_last_of('/'));
    (void)::mkdir(folder.substr(0, folder.find_last_of('/')).c_str(), 0700);    // ~/.config
    (void)::mkdir(folder.c_str(), 0755);                                         // ~/.config/autostart
    const std::string exe = Compat::NativePath(SelfExePath().c_str());
    if (exe.empty()) return;
    const std::string text =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Explorer2 monitoring recorder\n"
        "Comment=Records the history shown on the Explorer2 Monitoring tab\n"
        "Exec=" + DesktopExecArgument(exe) + " --monitor-daemon\n"
        "Terminal=false\n"
        "NoDisplay=true\n"
        "X-GNOME-Autostart-enabled=true\n";
    const std::string temp = entry + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "w");
    if (!f) return;
    const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !written || ::rename(temp.c_str(), entry.c_str()) != 0) (void)::unlink(temp.c_str());
#else
    // Always clear the Run-key flavour first; the scheduled-task flavour is removed/created through UAC
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kRunValueName);
        if (enable && !elevated) {
            std::wstring cmd = L"\"" + SelfExePath() + L"\" --monitor-daemon";
            RegSetValueExW(key, kRunValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(key);
    }

    bool taskPresent = TaskExists();
    DWORD code = 0;
    if (enable && elevated) {
        // Task XML: runs at logon with highest privileges, never killed after 72 h, also on battery
        std::wstring exe = SelfExePath();
        std::wstring escaped;
        for (wchar_t c : exe) {
            if (c == L'&') escaped += L"&amp;"; else if (c == L'<') escaped += L"&lt;"; else if (c == L'>') escaped += L"&gt;"; else escaped += c;
        }
        std::wstring xml =
            L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
            L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
            L"<Triggers><LogonTrigger><Enabled>true</Enabled></LogonTrigger></Triggers>\r\n"
            L"<Principals><Principal id=\"Author\"><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
            L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
            L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Enabled>true</Enabled></Settings>\r\n"
            L"<Actions Context=\"Author\"><Exec><Command>" + escaped + L"</Command><Arguments>--monitor-daemon</Arguments></Exec></Actions>\r\n"
            L"</Task>\r\n";
        std::wstring xmlPath = MonitorStore::DataDir() + L"\\task.xml";
        HANDLE f = CreateFileW(xmlPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            const uint16_t bom = 0xFEFF;
            DWORD w = 0;
            WriteFile(f, &bom, sizeof(bom), &w, nullptr);
            WriteFile(f, xml.data(), static_cast<DWORD>(xml.size() * sizeof(wchar_t)), &w, nullptr);
            CloseHandle(f);
            RunElevatedAndWait(L"schtasks.exe", L"/Create /F /TN " + std::wstring(kTaskName) + L" /XML \"" + xmlPath + L"\"", code);
            DeleteFileW(xmlPath.c_str());
        }
    } else if (taskPresent) {
        RunElevatedAndWait(L"schtasks.exe", L"/Delete /F /TN " + std::wstring(kTaskName), code);
    }
#endif
}

int MonitorDaemon::Run() {
#ifndef _WIN32
    // Single recorder per user session, however many windows are (or are not) open: see DaemonLockPath
    const int lockFd = AcquireDaemonLock();
    if (lockFd == kLockHeldByOther) return 0;
    if (lockFd < 0) return 1;
    InstallStopHandler();
    // Below normal priority, like BELOW_NORMAL_PRIORITY_CLASS on Windows. Linux keeps the nice value per thread;
    // the hardware worker threads started below inherit it from this one.
    const int niceResult = ::setpriority(PRIO_PROCESS, 0, 10);
    (void)niceResult;
#else
    // Single recorder per user session, however many windows are (or are not) open
    SharedSecurity security;
    HANDLE mutex = CreateMutexW(security.get(), TRUE, kDaemonMutexName);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }
    HANDLE stopEvent = CreateEventW(security.get(), TRUE, FALSE, kDaemonStopEventName);
    if (stopEvent) ResetEvent(stopEvent);

    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#endif

    {
        HardwareInspector hw;
        hw.InitializeStaticInventory(true);
        DiskIoSampler io;

        // SMART is read once per hour (it changes slowly): health badge + one snapshot row per drive
        auto snapshotSmart = [&hw]() {
            hw.RequestAsyncRefresh(false, true, -1);
            hw.WaitForAsync();
            hw.ApplyStagedData();
            int64_t now = static_cast<int64_t>(time(nullptr));
            for (const auto& d : hw.GetPhysicalDisks()) MonitorStore::AppendSmart(BuildSmartPoint(d, now));
        };
        snapshotSmart();
        ULONGLONG lastSmartTick = GetTickCount64();
        int lastPruneDay = -1;

        io.Collect();   // PDH rate counters need two collections
        // Warm-up so the first CPU / network / disk deltas cover a real interval
#ifndef _WIN32
        if (!WaitForStopRequest(2000)) {
#else
        if (!stopEvent || WaitForSingleObject(stopEvent, 2000) == WAIT_TIMEOUT) {
#endif
            while (true) {
                hw.UpdateLiveTelemetry(true);
                io.Collect();

                if (GetTickCount64() - lastSmartTick > 3600ull * 1000ull) {
                    snapshotSmart();
                    lastSmartTick = GetTickCount64();
                }

                MonitorSettings settings = MonitorStore::LoadSettings();
#ifndef _WIN32
                // Safety net for a stop request that did not arrive: the window that switched recording off saved
                // Enabled=0 before it signalled, so the recorder ends itself before its next row. A missing key (the
                // file being rewritten at this moment) is not a reason to stop.
                if (GetPrivateProfileIntW(L"Monitor", L"Enabled", 1, SettingsPath().c_str()) == 0) break;
#endif
                MonitorStore::AppendSample(SampleNow(hw, io.Read()));

                tm today = LocalTime(static_cast<int64_t>(time(nullptr)));
                if (today.tm_yday != lastPruneDay) {
                    lastPruneDay = today.tm_yday;
                    MonitorStore::PruneOldFiles(settings.retentionDays);
                }

                DWORD waitMs = static_cast<DWORD>(settings.intervalSec) * 1000u;
#ifndef _WIN32
                if (WaitForStopRequest(waitMs)) break;
#else
                if (!stopEvent) { Sleep(waitMs); continue; }
                if (WaitForSingleObject(stopEvent, waitMs) != WAIT_TIMEOUT) break;
#endif
            }
        }
    }

#ifndef _WIN32
    ::close(lockFd);   // gives the lock back: a new recorder may start now
#else
    if (stopEvent) CloseHandle(stopEvent);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
#endif
    return 0;
}

} // namespace Explorer2
