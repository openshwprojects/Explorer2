#pragma once
#include "Types.h"
#include "Localization.h"
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace Explorer2 {

struct HwDimmSlot {
    std::wstring slotLocator;
    std::wstring bankLocator;
    std::wstring memoryType;   // DDR4, DDR5, LPDDR5, etc.
    std::wstring formFactor;   // DIMM, SODIMM
    std::wstring manufacturer;
    std::wstring partNumber;
    std::wstring serialNumber;
    uint64_t sizeBytes = 0;
    uint32_t speedMHz = 0;
    uint32_t configuredSpeedMHz = 0;
    uint16_t dataWidthBits = 0;
};

struct HwCpuInfo {
    std::wstring brand;
    std::wstring vendor;
    std::wstring architecture;
    std::wstring socketDesignation;
    std::wstring microcodeRev;
    int family = 0;
    int model = 0;
    int stepping = 0;
    uint32_t baseFreqMHz = 0;
    uint32_t maxFreqMHz = 0;
    uint32_t physicalCores = 0;
    uint32_t logicalThreads = 0;
    uint32_t packages = 1;
    uint32_t numaNodes = 1;

    // Cache topology
    uint32_t l1DataKB = 0;
    uint32_t l1DataCount = 0;
    uint32_t l1InstKB = 0;
    uint32_t l1InstCount = 0;
    uint32_t l2KB = 0;
    uint32_t l2Count = 0;
    uint32_t l3KB = 0;
    uint32_t l3Count = 0;
    uint32_t cacheLineBytes = 64;

    // Instruction Set Extensions
    std::vector<std::wstring> isaFeatures;

    // Live Telemetry
    float totalLoadPct = 0.0f;
    float kernelLoadPct = 0.0f;
    std::vector<float> coreLoadPct;
    std::vector<float> loadHistory; // 60 samples (0..100%)

    // Built-in Pro Benchmark results
    double benchSingleCoreGops = 0.0;
    double benchMultiCoreGops = 0.0;
    double benchMpRatio = 0.0;
    double benchTimeMs = 0.0;
};

struct HwGpuInfo {
    std::wstring name;
    std::wstring vendorName;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint32_t subSysId = 0;
    uint32_t revision = 0;
    uint64_t dedicatedVramBytes = 0;
    uint64_t sharedSysMemBytes = 0;
    uint64_t dedicatedSystemBytes = 0;

    // Live GPU Telemetry (DXGI + optional NVML)
    uint64_t liveVramUsedBytes = 0;
    uint64_t liveSharedUsedBytes = 0;
    bool hasNvmlTelemetry = false;
    uint32_t gpuLoadPct = 0;
    uint32_t memControllerLoadPct = 0;
    uint32_t tempCelsius = 0;
    uint32_t fanSpeedPct = 0;
    uint32_t coreClockMHz = 0;
    uint32_t memClockMHz = 0;
    float powerDrawWatts = 0.0f;
    float powerLimitWatts = 0.0f;
    std::wstring driverVersion;
};

struct HwDisplayInfo {
    std::wstring deviceName;
    std::wstring monitorName;
    std::wstring gpuAdapterName;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t refreshHz = 0;
    uint32_t bitsPerPel = 32;
    bool isPrimary = false;
};

struct HwRamInfo {
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t availBytes = 0;
    float usedPct = 0.0f;

    uint64_t commitTotalBytes = 0;
    uint64_t commitLimitBytes = 0;
    uint64_t commitPeakBytes = 0;
    uint64_t kernelPagedBytes = 0;
    uint64_t kernelNonPagedBytes = 0;
    uint64_t systemCacheBytes = 0;

    std::vector<HwDimmSlot> dimms;
    std::vector<float> loadHistory; // 60 samples (0..100%)

    // Built-in RAM Bandwidth Benchmark
    double benchReadGBps = 0.0;
    double benchWriteGBps = 0.0;
    double benchCopyGBps = 0.0;
};

struct HwMotherboardInfo {
    std::wstring boardManufacturer;
    std::wstring boardProduct;
    std::wstring boardVersion;
    std::wstring boardSerial;
    std::wstring biosVendor;
    std::wstring biosVersion;
    std::wstring biosReleaseDate;
    std::wstring systemManufacturer;
    std::wstring systemProductName;
    std::wstring systemSku;
    std::wstring chassisType;
    bool isUefiBoot = true;
    bool isSecureBoot = false;
};

// One row of the SMART table. ATA drives fill the normalized columns (current/worst/threshold);
// NVMe health-log entries only provide the pre-formatted raw text.
struct HwSmartAttr {
    int id = 0;                 // ATA attribute ID (0 for NVMe log entries)
    std::wstring name;
    bool hasNormalized = false;
    int current = 0;
    int worst = 0;
    int threshold = 0;
    uint64_t raw = 0;
    std::wstring rawText;
    bool prefail = false;
    int status = -1;            // -1 info, 0 OK, 1 warning, 2 failing
};

enum class HwSmartState : int {
    NotQueried = 0,
    Ok,                         // data read successfully
    NeedsAdmin,                 // access denied: ATA SMART needs an elevated process
    Unsupported                 // controller/bus does not expose SMART (USB bridge, RAID, virtual...)
};

struct HwPhysicalDisk {
    int index = 0;
    std::wstring model;
    std::wstring vendor;
    std::wstring serialNumber;
    std::wstring firmwareRev;
    std::wstring busType;   // NVMe, SATA, USB, RAID, SAS
    std::wstring mediaType; // SSD / NVMe, HDD, Removable
    uint64_t capacityBytes = 0;
    uint32_t bytesPerSector = 512;
    bool supportsTrim = false;
    bool supportsCommandQueueing = false;
    bool isNvme = false;
    bool isRotational = false;

    // Temperature (live, degrees Celsius; -1 = unknown)
    int tempC = -1;
    int tempPeakC = -1;         // highest value seen in this session
    int tempWarnC = -1;
    int tempCritC = -1;
    std::vector<float> tempHistory; // 60 samples

    // SMART / health
    HwSmartState smartState = HwSmartState::NotQueried;
    int healthLevel = -1;       // -1 unknown, 0 OK, 1 warning, 2 failing
    int lifeRemainingPct = -1;  // SSD wear (100 = new), -1 unknown
    int64_t powerOnHours = -1;
    int64_t powerCycles = -1;
    int64_t bytesWritten = -1;  // host writes (approx. for ATA)
    int64_t bytesRead = -1;
    std::vector<HwSmartAttr> smartAttrs;
};

struct HwThermalSensor {
    std::wstring name;
    float tempC = 0.0f;
    float warnC = 0.0f;
    float critC = 0.0f;
};

struct HwLogicalVolume {
    std::wstring rootPath;
    std::wstring label;
    std::wstring fileSystem;
    std::wstring driveTypeStr;
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
    uint64_t usedBytes = 0;
    float usedPct = 0.0f;
    uint32_t serialNumber = 0;
    bool isCompressed = false;
    bool isEncrypted = false;
    bool isReady = true;
};

struct HwNetAdapter {
    std::wstring friendlyName;
    std::wstring description;
    std::wstring macAddress;
    std::wstring ipv4;
    std::wstring ipv6;
    std::wstring gateway;
    std::wstring dnsServers;
    uint64_t txLinkSpeedBps = 0;
    uint64_t rxLinkSpeedBps = 0;
    uint64_t totalRxBytes = 0;
    uint64_t totalTxBytes = 0;
    double liveRxBytesPerSec = 0.0;
    double liveTxBytesPerSec = 0.0;
    bool isConnected = false;
    bool isWireless = false;
};

struct HwProcessEntry {
    uint32_t pid = 0;
    std::wstring name;
    uint64_t workingSetBytes = 0;
    uint64_t privateBytes = 0;
    uint32_t threads = 0;
};

struct HwSystemInfo {
    std::wstring osName;
    std::wstring osBuildStr;
    std::wstring computerName;
    std::wstring userName;
    std::wstring systemDir;
    std::wstring powerStatusStr;
    std::wstring uptimeStr;
    uint64_t uptimeSeconds = 0;
    uint32_t processCount = 0;
    uint32_t threadCount = 0;
    uint32_t handleCount = 0;
    std::vector<std::wstring> audioOutputs;
    std::vector<std::wstring> audioInputs;
    std::vector<HwProcessEntry> topProcesses;
};

class HardwareInspector {
public:
    HardwareInspector();
    ~HardwareInspector();

    // When syncForCli is false (default for UI), runs only < 0.5 ms in-memory/registry/CPUID queries
    // on the calling thread and offloads NVML, DXGI, Disk SMART, Network, Audio, Processes & PDH to a background thread.
    void InitializeStaticInventory(bool syncForCli = false);
    void EnsureDiskModelsLoaded();
    void RefreshFullInventory(bool syncForCli = false);
    void UpdateLiveTelemetry(bool syncForCli = false);

    // Non-blocking background refresh requests for UI commands
    void RequestAsyncRefresh(bool fullInventory, bool allSmart, int singleSmartDisk = -1);
    bool ApplyStagedData();
    bool IsAsyncBusy() const { return m_workerBusy.load(std::memory_order_relaxed); }
    bool HasStagedData() const { return m_hasStagedData.load(std::memory_order_relaxed); }
    void WaitForAsync();

    void RunCpuBenchmark();
    void RunRamBenchmark();

    std::wstring BuildFullTextReport(Lang lang) const;
    // Raw diagnostic dump (SMART tables, thermal zones, CPU deltas) used by `Explorer2.exe --dump-hw <file>`.
    std::wstring BuildDiagnosticsDump() const;

    const HwCpuInfo& GetCpu() const { return m_cpu; }
    const std::vector<HwGpuInfo>& GetGpus() const { return m_gpus; }
    const std::vector<HwDisplayInfo>& GetDisplays() const { return m_displays; }
    const HwRamInfo& GetRam() const { return m_ram; }
    const HwMotherboardInfo& GetMotherboard() const { return m_mobo; }
    const std::vector<HwPhysicalDisk>& GetPhysicalDisks() const { return m_physDisks; }
    const std::vector<HwLogicalVolume>& GetVolumes() const { return m_volumes; }
    const std::vector<HwNetAdapter>& GetNetworks() const { return m_networks; }
    const HwSystemInfo& GetSystem() const { return m_system; }

    const std::vector<float>& GetGpuLoadHistory() const { return m_gpuLoadHistory; }
    const std::vector<float>& GetNetRxHistoryKBps() const { return m_netRxHistoryKBps; }
    const std::vector<float>& GetNetTxHistoryKBps() const { return m_netTxHistoryKBps; }

    // Temperatures
    const std::vector<HwThermalSensor>& GetAcpiThermalZones() const { return m_thermalZones; }
    const std::vector<float>& GetAcpiTempHistory() const { return m_acpiTempHistory; }
    const std::vector<float>& GetGpuTempHistory() const { return m_gpuTempHistory; }

    // Reads SMART health + thermal data for one physical disk (synchronous helper for CLI/daemon; UI uses RequestAsyncRefresh).
    void RefreshDiskSmart(int physIndex);
    static bool IsProcessElevated();
    // Opens the classic Windows "Properties" dialog of a network connection (same as ncpa.cpl > right click > Properties).
    // Falls back to opening the Network Connections folder when the adapter cannot be matched.
    static void OpenNetworkAdapterProperties(HWND owner, const std::wstring& connectionName);

    bool IsInitialized() const { return m_initialized; }

private:
    void QueryCpuStatic();
    void QueryMotherboardAndSmbios();
    void QueryFastSystemAndRam(bool includeStaticOs);
    void QueryFastVolumes();
    void QueryGpusAndDisplays();
    void QueryPhysicalDisksAndVolumes();
    void QueryNetworkAdapters(bool updateThroughputOnly);
    void QuerySystemAndAudio(bool fullRefresh);

    void UpdateCpuLivePerCoreLoad();
    void UpdateGpuLiveTelemetry();
    void UpdateDiskTemperatures();
    void UpdateAcpiThermalZones();
    void RefreshDiskSmartOnVector(std::vector<HwPhysicalDisk>& disks, int physIndex);

    void StartWorkerThreadIfNeeded();
    void WorkerLoop();
    void PublishWorkerStateToStaged();

    bool m_initialized = false;
    HwCpuInfo m_cpu;
    std::vector<HwGpuInfo> m_gpus;
    std::vector<HwDisplayInfo> m_displays;
    HwRamInfo m_ram;
    HwMotherboardInfo m_mobo;
    std::vector<HwPhysicalDisk> m_physDisks;
    std::vector<HwLogicalVolume> m_volumes;
    std::vector<HwNetAdapter> m_networks;
    HwSystemInfo m_system;

    std::vector<float> m_gpuLoadHistory;
    std::vector<float> m_netRxHistoryKBps;
    std::vector<float> m_netTxHistoryKBps;

    std::vector<HwThermalSensor> m_thermalZones;
    std::vector<float> m_acpiTempHistory;
    std::vector<float> m_gpuTempHistory;
    ULONGLONG m_lastDiskTempTick = 0;

    // PDH query for "\Thermal Zone Information(*)\Temperature" (opaque handles)
    void* m_pdhThermalQuery = nullptr;
    void* m_pdhThermalCounter = nullptr;
    bool m_pdhThermalTried = false;

    // Previous per-core idle/kernel/user timestamps for delta CPU %
    struct CoreTimeSample {
        int64_t idle = 0;
        int64_t kernel = 0;
        int64_t user = 0;
    };
    std::vector<CoreTimeSample> m_prevCoreTimes;

    // Previous network byte counters & timestamp for live RX/TX throughput
    ULONGLONG m_prevNetTick = 0;
    uint64_t m_prevTotalRx = 0;
    uint64_t m_prevTotalTx = 0;
    ULONGLONG m_lastProcessSnapTick = 0;

    // Optional dynamically loaded NVML library for NVIDIA GPU sensors
    HMODULE m_hNvml = nullptr;
    bool m_nvmlReady = false;
    bool m_nvmlTried = false;

    // Background worker & lock-free / micro-locked staging state for non-blocking UI
    struct StagedData {
        std::vector<HwGpuInfo> gpus;
        std::vector<HwDisplayInfo> displays;
        std::vector<HwPhysicalDisk> physDisks;
        std::vector<HwLogicalVolume> volumes;
        std::vector<HwNetAdapter> networks;
        std::vector<std::wstring> audioOutputs;
        std::vector<std::wstring> audioInputs;
        std::vector<HwProcessEntry> topProcesses;
        std::vector<HwThermalSensor> thermalZones;
        std::vector<float> gpuLoadHistory;
        std::vector<float> netRxHistoryKBps;
        std::vector<float> netTxHistoryKBps;
        std::vector<float> acpiTempHistory;
        std::vector<float> gpuTempHistory;
    };

    // Worker-owned working copies (only accessed on m_workerThread)
    StagedData m_wState;
    // Mutex-protected staged snapshot (swapped to UI thread in ApplyStagedData)
    mutable std::mutex m_stageMutex;
    StagedData m_staged;
    std::atomic<bool> m_hasStagedData{ false };

    std::thread m_workerThread;
    bool m_workerStarted = false;
    mutable std::mutex m_workerMutex;
    std::condition_variable m_workerCv;
    std::condition_variable m_doneCv;
    std::atomic<bool> m_stopWorker{ false };
    std::atomic<bool> m_workerBusy{ false };
    std::atomic<bool> m_reqFullRefresh{ false };
    std::atomic<bool> m_reqAllSmart{ false };
    std::atomic<int>  m_reqSingleSmartDisk{ -1 };
    std::atomic<bool> m_reqLiveUpdate{ false };
};

} // namespace Explorer2
