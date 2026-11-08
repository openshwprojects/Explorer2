#pragma once
#include "Types.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Explorer2 {

// ---------------------------------------------------------------------------------------------
// System monitoring: one background "recorder" process samples the machine (CPU / RAM / GPU /
// temperatures / drive health / disk I/O / free space / network) and appends one CSV row per
// interval to  %LOCALAPPDATA%\Explorer2\Monitoring\samples-YYYY-MM-DD.csv
// Every hour it also stores a SMART snapshot per drive in smart-YYYY-MM-DD.csv.
// Any number of Explorer2 windows (separate processes) only READ those files, so there is exactly
// one writer no matter how many windows are open or whether any window is open at all.
// ---------------------------------------------------------------------------------------------

struct MonitorDiskPoint {
    int index = 0;           // physical drive number (\\.\PhysicalDriveN)
    int tempC = -1;          // -1 = unknown
    int health = -1;         // -1 unknown, 0 OK, 1 warning, 2 failing
    float readKBps = -1.0f;  // average over the sampling interval (PDH \PhysicalDisk)
    float writeKBps = -1.0f;
    float busyPct = -1.0f;
};

struct MonitorVolumePoint {
    wchar_t letter = L'?';
    float usedPct = 0.0f;
    float freeGB = 0.0f;
};

struct MonitorSample {
    int64_t ts = 0;                 // Unix time (UTC seconds)
    float cpuPct = -1.0f;           // average over the sampling interval
    float ramPct = -1.0f;
    int ramUsedMB = -1;
    float commitPct = -1.0f;        // committed memory / commit limit
    int procCount = -1;
    float gpuLoadPct = -1.0f;
    int gpuTempC = -1;
    int gpuVramMB = -1;
    float gpuPowerW = -1.0f;
    int gpuFanPct = -1;
    int gpuClockMHz = -1;
    int acpiTempC = -1;             // hottest ACPI thermal zone
    float rxKBps = -1.0f;           // average network throughput over the interval
    float txKBps = -1.0f;
    std::vector<MonitorDiskPoint> disks;
    std::vector<MonitorVolumePoint> volumes;
};

// Hourly SMART snapshot of one physical drive (-1 = not available for this drive)
struct MonitorSmartPoint {
    int64_t ts = 0;
    int disk = 0;
    int health = -1;                // 0 OK, 1 warning, 2 failing, -1 unreadable (e.g. SATA without admin rights)
    int tempC = -1;
    int lifePct = -1;               // SSD life remaining
    int64_t powerOnHours = -1;
    int64_t powerCycles = -1;
    int64_t writtenGB = -1;
    int64_t readGB = -1;
    int64_t reallocated = -1;       // ATA 5
    int64_t pending = -1;           // ATA 197
    int64_t uncorrectable = -1;     // ATA 198
    int64_t crcErrors = -1;         // ATA 199
    int64_t unsafeShutdowns = -1;   // NVMe log / ATA 192
    int64_t mediaErrors = -1;       // NVMe media & data integrity errors
    int spare = -1;                 // NVMe available spare %
};

struct MonitorSettings {
    bool enabled = false;           // user wants the recorder to run
    bool elevated = false;          // run the recorder as administrator (needed for SATA SMART)
    int intervalSec = 60;
    int retentionDays = 90;
};

class MonitorStore {
public:
    static std::wstring DataDir();                        // created on demand
    static MonitorSettings LoadSettings();
    static void SaveSettings(const MonitorSettings& s);

    static bool AppendSample(const MonitorSample& s);     // recorder process only
    static bool AppendSmart(const MonitorSmartPoint& p);  // recorder process only
    static void PruneOldFiles(int retentionDays);

    // Reads every sample with fromTs <= ts <= toTs (ascending). Safe while the recorder is writing.
    // (The UI uses MonitorHistory below, which is incremental; this stays for tools / tests.)
    static std::vector<MonitorSample> LoadRange(int64_t fromTs, int64_t toTs);
    static std::vector<MonitorSmartPoint> LoadSmartRange(int64_t fromTs, int64_t toTs);
    static uint64_t TotalBytesOnDisk();
};

// Incremental, cached reader for the UI: finished days are parsed once, today's file is only read from the
// byte offset where the previous read stopped. Rows() are valid until the next Update().
class MonitorHistory {
public:
    void Update(int64_t fromTs, int64_t toTs);            // cheap when nothing changed
    const std::vector<const MonitorSample*>& Rows() const { return m_rows; }
    uint64_t Version() const { return m_version; }        // changes when the visible rows change
    void Clear();

private:
    struct Day {
        uint64_t parsedBytes = 0;                         // offset just after the last complete line
        std::vector<MonitorSample> samples;
    };
    std::map<int, Day> m_days;                            // key = YYYYMMDD (local date)
    std::vector<const MonitorSample*> m_rows;
    uint64_t m_version = 0;
};

class MonitorDaemon {
public:
    // Entry point of the hidden recorder process (`Explorer2.exe --monitor-daemon`). Returns when stopped.
    static int Run();

    static bool IsRunning();                              // named mutex probe, works across processes / integrity levels
    static bool Start(bool elevated);                     // spawns the recorder unless one is already running
    static void RequestStop();                            // signals the running recorder to exit

    // 0 = off, 1 = HKCU Run key, 2 = scheduled task with highest privileges (recorder runs as administrator)
    static int AutostartMode();
    static void SetAutostart(bool enable, bool elevated);
};

} // namespace Explorer2
