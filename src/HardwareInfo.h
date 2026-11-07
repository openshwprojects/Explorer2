#pragma once
#include "Types.h"
#include "Localization.h"
#include <string>
#include <vector>
#include <cstdint>

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

    void InitializeStaticInventory();
    void RefreshFullInventory();
    void UpdateLiveTelemetry();

    void RunCpuBenchmark();
    void RunRamBenchmark();

    std::wstring BuildFullTextReport(Lang lang) const;

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

    bool IsInitialized() const { return m_initialized; }

private:
    void QueryCpuStatic();
    void QueryMotherboardAndSmbios();
    void QueryGpusAndDisplays();
    void QueryPhysicalDisksAndVolumes();
    void QueryNetworkAdapters(bool updateThroughputOnly);
    void QuerySystemAndAudio(bool fullRefresh);

    void UpdateCpuLivePerCoreLoad();
    void UpdateGpuLiveTelemetry();

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
};

} // namespace Explorer2
