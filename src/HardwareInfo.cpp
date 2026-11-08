#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <dxgi1_4.h>
#include <winioctl.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <intrin.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <pdh.h>
#include <pdhmsg.h>

#include "HardwareInfo.h"
#include "FileEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <thread>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "mpr.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace Explorer2 {

namespace {

std::wstring Utf8OrAsciiToWide(const char* s) {
    if (!s || !*s) return L"";
    std::string trimmed(s);
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n')) {
        trimmed.pop_back();
    }
    size_t first = 0;
    while (first < trimmed.size() && (trimmed[first] == ' ' || trimmed[first] == '\t')) ++first;
    if (first > 0) trimmed.erase(0, first);
    if (trimmed.empty()) return L"";

    int len = MultiByteToWideChar(CP_UTF8, 0, trimmed.c_str(), static_cast<int>(trimmed.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, trimmed.c_str(), static_cast<int>(trimmed.size()), out.data(), len);
    return out;
}

std::wstring ReadRegSz(HKEY root, const wchar_t* subKey, const wchar_t* valName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return L"";
    wchar_t buf[512] = {};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    std::wstring res;
    if (RegQueryValueExW(hKey, valName, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        res = buf;
    }
    RegCloseKey(hKey);
    return res;
}

DWORD ReadRegDword(HKEY root, const wchar_t* subKey, const wchar_t* valName, DWORD defVal = 0) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return defVal;
    DWORD val = defVal;
    DWORD sz = sizeof(val);
    DWORD type = 0;
    RegQueryValueExW(hKey, valName, nullptr, &type, reinterpret_cast<LPBYTE>(&val), &sz);
    RegCloseKey(hKey);
    return val;
}

// NtQuerySystemInformation for per-core CPU utilization
struct SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION_NATIVE {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
};

using PFN_NtQuerySystemInformation = LONG(WINAPI*)(ULONG SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);
using PFN_RtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

// NVML dynamic function pointers for NVIDIA GPU live telemetry
using nvmlDevice_t = void*;
struct nvmlUtilization_t { unsigned int gpu; unsigned int memory; };
struct nvmlMemory_t { unsigned long long total; unsigned long long free; unsigned long long used; };

using PFN_nvmlInit_v2 = int(*)(void);
using PFN_nvmlDeviceGetHandleByIndex_v2 = int(*)(unsigned int, nvmlDevice_t*);
using PFN_nvmlDeviceGetUtilizationRates = int(*)(nvmlDevice_t, nvmlUtilization_t*);
using PFN_nvmlDeviceGetTemperature = int(*)(nvmlDevice_t, int, unsigned int*);
using PFN_nvmlDeviceGetFanSpeed = int(*)(nvmlDevice_t, unsigned int*);
using PFN_nvmlDeviceGetClockInfo = int(*)(nvmlDevice_t, int, unsigned int*);
using PFN_nvmlDeviceGetPowerUsage = int(*)(nvmlDevice_t, unsigned int*);
using PFN_nvmlDeviceGetPowerManagementLimit = int(*)(nvmlDevice_t, unsigned int*);
using PFN_nvmlDeviceGetMemoryInfo = int(*)(nvmlDevice_t, nvmlMemory_t*);
using PFN_nvmlSystemGetDriverVersion = int(*)(char*, unsigned int);

// Extract string by 1-based index from an SMBIOS structure's unformatted string table
std::wstring GetSmbiosString(const uint8_t* structStart, uint8_t formattedLen, const uint8_t* tableEnd, uint8_t strIndex) {
    if (strIndex == 0 || structStart + formattedLen >= tableEnd) return L"";
    const char* p = reinterpret_cast<const char*>(structStart + formattedLen);
    const char* end = reinterpret_cast<const char*>(tableEnd);
    uint8_t curIdx = 1;
    while (p < end && *p != '\0') {
        size_t slen = strnlen(p, static_cast<size_t>(end - p));
        if (curIdx == strIndex) {
            return Utf8OrAsciiToWide(p);
        }
        p += slen + 1;
        ++curIdx;
    }
    return L"";
}

// ---------------------------------------------------------------------------------------------
// Disk temperature / SMART helpers
// ---------------------------------------------------------------------------------------------

HANDLE OpenPhysicalDrive(int index, DWORD access) {
    wchar_t devPath[64] = {};
    swprintf_s(devPath, L"\\\\.\\PhysicalDrive%d", index);
    return CreateFileW(devPath, access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

// IOCTL_STORAGE_QUERY_PROPERTY / StorageDeviceTemperatureProperty: works without elevation on NVMe
// and on SATA drives whose miniport exposes the SMART temperature.
bool QueryStorageTemperature(HANDLE h, int& tempC, int& warnC, int& critC) {
    STORAGE_PROPERTY_QUERY q = {};
    q.PropertyId = StorageDeviceTemperatureProperty;
    q.QueryType  = PropertyStandardQuery;
    alignas(8) uint8_t buf[512] = {};
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &ret, nullptr)) return false;
    if (ret < sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR)) return false;
    auto d = reinterpret_cast<STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buf);
    if (d->InfoCount == 0) return false;
    int t = d->TemperatureInfo[0].Temperature;
    if (t <= -50 || t > 150) return false;
    tempC = t;
    if (d->WarningTemperature > 0 && d->WarningTemperature < 150) warnC = d->WarningTemperature;
    if (d->CriticalTemperature > 0 && d->CriticalTemperature < 200) critC = d->CriticalTemperature;
    return true;
}

// NVMe SMART / Health Information log page (Log Identifier 02h) through the Windows protocol-specific query.
bool QueryNvmeHealthLog(HANDLE h, uint8_t* out512) {
    constexpr DWORD kLen = 512;
    alignas(8) uint8_t buf[sizeof(STORAGE_PROPERTY_QUERY) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + kLen] = {};
    auto q = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buf);
    q->PropertyId = StorageDeviceProtocolSpecificProperty;
    q->QueryType  = PropertyStandardQuery;
    auto psd = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(q->AdditionalParameters);
    psd->ProtocolType = ProtocolTypeNvme;
    psd->DataType = NVMeDataTypeLogPage;
    psd->ProtocolDataRequestValue = 2; // NVME_LOG_PAGE_HEALTH_INFO
    psd->ProtocolDataRequestSubValue = 0;
    psd->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    psd->ProtocolDataLength = kLen;

    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, buf, sizeof(buf), buf, sizeof(buf), &ret, nullptr)) return false;
    auto desc = reinterpret_cast<STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buf);
    if (desc->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR) || desc->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) return false;
    auto rpsd = &desc->ProtocolSpecificData;
    if (rpsd->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) || rpsd->ProtocolDataLength < kLen) return false;
    std::memcpy(out512, reinterpret_cast<const uint8_t*>(rpsd) + rpsd->ProtocolDataOffset, kLen);
    return true;
}

// Legacy ATA SMART IOCTLs (SMART_RCV_DRIVE_DATA). The handle must be opened GENERIC_READ|GENERIC_WRITE,
// which in practice requires an elevated process.
bool AtaSmartRead(HANDLE h, int driveNo, uint8_t feature, uint8_t* out512) {
    SENDCMDINPARAMS in = {};
    in.cBufferSize = 512;
    in.bDriveNumber = static_cast<BYTE>(driveNo);
    in.irDriveRegs.bFeaturesReg     = feature;
    in.irDriveRegs.bSectorCountReg  = 1;
    in.irDriveRegs.bSectorNumberReg = 1;
    in.irDriveRegs.bCylLowReg       = 0x4F; // SMART signature
    in.irDriveRegs.bCylHighReg      = 0xC2;
    in.irDriveRegs.bDriveHeadReg    = static_cast<BYTE>(0xA0 | ((driveNo & 1) << 4));
    in.irDriveRegs.bCommandReg      = 0xB0; // SMART command

    alignas(8) uint8_t outBuf[sizeof(SENDCMDOUTPARAMS) + 512] = {};
    DWORD ret = 0;
    if (!DeviceIoControl(h, SMART_RCV_DRIVE_DATA, &in, sizeof(SENDCMDINPARAMS) - 1,
                         outBuf, sizeof(outBuf), &ret, nullptr)) return false;
    auto out = reinterpret_cast<SENDCMDOUTPARAMS*>(outBuf);
    if (out->DriverStatus.bDriverError != 0) return false;
    std::memcpy(out512, out->bBuffer, 512);
    return true;
}

const wchar_t* AtaAttrName(int id) {
    switch (id) {
    case 1:   return L"Raw Read Error Rate";
    case 2:   return L"Throughput Performance";
    case 3:   return L"Spin-Up Time";
    case 4:   return L"Start/Stop Count";
    case 5:   return L"Reallocated Sectors Count";
    case 7:   return L"Seek Error Rate";
    case 8:   return L"Seek Time Performance";
    case 9:   return L"Power-On Hours";
    case 10:  return L"Spin Retry Count";
    case 11:  return L"Calibration Retry Count";
    case 12:  return L"Power Cycle Count";
    case 170: return L"Available Reserved Space";
    case 171: return L"Program Fail Count";
    case 172: return L"Erase Fail Count";
    case 173: return L"Wear Leveling Count";
    case 174: return L"Unexpected Power Loss Count";
    case 175: return L"Power Loss Protection Failure";
    case 177: return L"Wear Leveling Count";
    case 178: return L"Used Reserved Block Count (Chip)";
    case 179: return L"Used Reserved Block Count (Total)";
    case 180: return L"Unused Reserved Block Count";
    case 181: return L"Program Fail Count (Total)";
    case 182: return L"Erase Fail Count (Total)";
    case 183: return L"Runtime Bad Block / SATA Downshift";
    case 184: return L"End-to-End Error";
    case 187: return L"Reported Uncorrectable Errors";
    case 188: return L"Command Timeout";
    case 189: return L"High Fly Writes";
    case 190: return L"Airflow Temperature";
    case 191: return L"G-Sense Error Rate";
    case 192: return L"Power-Off Retract Count";
    case 193: return L"Load/Unload Cycle Count";
    case 194: return L"Temperature";
    case 195: return L"Hardware ECC Recovered";
    case 196: return L"Reallocation Event Count";
    case 197: return L"Current Pending Sector Count";
    case 198: return L"Offline Uncorrectable Sector Count";
    case 199: return L"UDMA CRC Error Count";
    case 200: return L"Multi-Zone Error Rate";
    case 201: return L"Soft Read Error Rate";
    case 202: return L"Percent Lifetime Used";
    case 206: return L"Flying Height";
    case 218: return L"CRC Error Count";
    case 220: return L"Disk Shift";
    case 222: return L"Loaded Hours";
    case 223: return L"Load/Unload Retry Count";
    case 224: return L"Load Friction";
    case 226: return L"Load-in Time";
    case 231: return L"SSD Life Left";
    case 232: return L"Endurance Remaining";
    case 233: return L"Media Wearout Indicator";
    case 234: return L"Vendor NAND Writes";
    case 235: return L"Good Block Count";
    case 240: return L"Head Flying Hours";
    case 241: return L"Total LBAs Written";
    case 242: return L"Total LBAs Read";
    case 245: return L"Max Erase Count (Vendor)";
    case 246: return L"Total Host Sector Writes";
    case 247: return L"Host Program Page Count";
    case 248: return L"Background Program Page Count";
    case 249: return L"NAND Writes (1 GiB)";
    case 250: return L"Read Error Retry Rate";
    case 254: return L"Free Fall Event Count";
    default:  return L"Vendor Specific";
    }
}

// Parses SMART attribute (0xD0) + threshold (0xD1) sectors into table rows.
void ParseAtaAttributes(const uint8_t* attr, const uint8_t* thr, std::vector<HwSmartAttr>& out) {
    out.clear();
    for (int i = 0; i < 30; ++i) {
        const uint8_t* a = attr + 2 + i * 12;
        int id = a[0];
        if (id == 0) continue;

        HwSmartAttr row;
        row.id = id;
        row.name = AtaAttrName(id);
        row.hasNormalized = true;
        uint16_t flags = static_cast<uint16_t>(a[1] | (a[2] << 8));
        row.prefail = (flags & 0x0001) != 0;
        row.current = a[3];
        row.worst   = a[4];
        uint64_t raw = 0;
        for (int b = 5; b >= 0; --b) raw = (raw << 8) | a[5 + b];
        row.raw = raw;

        const uint8_t* t = thr + 2 + i * 12;
        row.threshold = (t[0] == id) ? t[1] : 0;
        if (t[0] != id) {
            for (int k = 0; k < 30; ++k) {
                const uint8_t* tk = thr + 2 + k * 12;
                if (tk[0] == id) { row.threshold = tk[1]; break; }
            }
        }

        // Raw text with the handful of well-known interpretations
        switch (id) {
        case 9: case 222: case 240:
            row.rawText = std::to_wstring(raw & 0xFFFFFFFFull) + L" h";
            break;
        case 190: case 194: {
            wchar_t b[48] = {};
            swprintf_s(b, L"%u\x00B0" L"C", static_cast<unsigned>(raw & 0xFF));
            row.rawText = b;
            break;
        }
        case 241: case 242: {
            uint64_t lbas = raw & 0xFFFFFFFFFFFFull;
            row.rawText = std::to_wstring(lbas) + L"  (\x2248 " + FileEngine::FormatSizeDetailed(lbas * 512ull) + L")";
            break;
        }
        default:
            row.rawText = std::to_wstring(raw & 0xFFFFFFFFFFFFull);
            break;
        }

        row.status = 0;
        if (row.threshold > 0 && row.current <= row.threshold && row.prefail) {
            row.status = 2;
        } else if ((id == 5 || id == 187 || id == 196 || id == 197 || id == 198) && (raw & 0xFFFFFFFFull) > 0) {
            row.status = 1;
        }
        out.push_back(std::move(row));
    }
}

const HwSmartAttr* FindAttr(const std::vector<HwSmartAttr>& v, int id) {
    for (const auto& a : v) if (a.id == id) return &a;
    return nullptr;
}

uint64_t ReadLe64(const uint8_t* p) {
    uint64_t v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

} // namespace

HardwareInspector::HardwareInspector() {
    m_cpu.loadHistory.assign(60, 0.0f);
    m_ram.loadHistory.assign(60, 0.0f);
    m_gpuLoadHistory.assign(60, 0.0f);
    m_netRxHistoryKBps.assign(60, 0.0f);
    m_netTxHistoryKBps.assign(60, 0.0f);
    m_acpiTempHistory.assign(60, 0.0f);
    m_gpuTempHistory.assign(60, 0.0f);

    m_wState.gpuLoadHistory.assign(60, 0.0f);
    m_wState.netRxHistoryKBps.assign(60, 0.0f);
    m_wState.netTxHistoryKBps.assign(60, 0.0f);
    m_wState.acpiTempHistory.assign(60, 0.0f);
    m_wState.gpuTempHistory.assign(60, 0.0f);
}

HardwareInspector::~HardwareInspector() {
    m_stopWorker.store(true, std::memory_order_release);
    m_workerCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    if (m_pdhThermalQuery) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(m_pdhThermalQuery));
        m_pdhThermalQuery = nullptr;
        m_pdhThermalCounter = nullptr;
    }
    if (m_hNvml) {
        FreeLibrary(m_hNvml);
        m_hNvml = nullptr;
    }
}

void HardwareInspector::StartWorkerThreadIfNeeded() {
    if (m_workerStarted) return;
    m_workerStarted = true;
    m_wState.physDisks = m_physDisks;
    m_wState.volumes = m_volumes;
    m_workerThread = std::thread(&HardwareInspector::WorkerLoop, this);
}

void HardwareInspector::PublishWorkerStateToStaged() {
    std::lock_guard<std::mutex> lk(m_stageMutex);
    m_staged = m_wState;
    m_hasStagedData.store(true, std::memory_order_release);
}

bool HardwareInspector::ApplyStagedData() {
    if (!m_hasStagedData.exchange(false, std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> lk(m_stageMutex);
    m_gpus = m_staged.gpus;
    m_displays = m_staged.displays;
    m_physDisks = m_staged.physDisks;
    m_volumes = m_staged.volumes;
    m_networks = m_staged.networks;
    m_system.audioOutputs = m_staged.audioOutputs;
    m_system.audioInputs = m_staged.audioInputs;
    m_system.topProcesses = m_staged.topProcesses;
    m_thermalZones = m_staged.thermalZones;
    m_gpuLoadHistory = m_staged.gpuLoadHistory;
    m_netRxHistoryKBps = m_staged.netRxHistoryKBps;
    m_netTxHistoryKBps = m_staged.netTxHistoryKBps;
    m_acpiTempHistory = m_staged.acpiTempHistory;
    m_gpuTempHistory = m_staged.gpuTempHistory;
    return true;
}

void HardwareInspector::RequestAsyncRefresh(bool fullInventory, bool allSmart, int singleSmartDisk) {
    StartWorkerThreadIfNeeded();
    if (fullInventory) m_reqFullRefresh.store(true, std::memory_order_release);
    if (allSmart) m_reqAllSmart.store(true, std::memory_order_release);
    if (singleSmartDisk >= 0) m_reqSingleSmartDisk.store(singleSmartDisk, std::memory_order_release);
    m_reqLiveUpdate.store(true, std::memory_order_release);
    m_workerBusy.store(true, std::memory_order_release);
    m_workerCv.notify_one();
}

void HardwareInspector::WaitForAsync() {
    std::unique_lock<std::mutex> lk(m_workerMutex);
    m_doneCv.wait(lk, [this]() {
        return !m_workerBusy.load(std::memory_order_acquire) &&
               !m_reqFullRefresh.load(std::memory_order_acquire) &&
               !m_reqAllSmart.load(std::memory_order_acquire) &&
               m_reqSingleSmartDisk.load(std::memory_order_acquire) < 0 &&
               !m_reqLiveUpdate.load(std::memory_order_acquire);
    });
}

void HardwareInspector::WorkerLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    while (!m_stopWorker.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lk(m_workerMutex);
            m_workerCv.wait(lk, [this]() {
                return m_stopWorker.load(std::memory_order_acquire) ||
                       m_reqFullRefresh.load(std::memory_order_acquire) ||
                       m_reqAllSmart.load(std::memory_order_acquire) ||
                       m_reqSingleSmartDisk.load(std::memory_order_acquire) >= 0 ||
                       m_reqLiveUpdate.load(std::memory_order_acquire);
            });
        }
        if (m_stopWorker.load(std::memory_order_acquire)) break;

        m_workerBusy.store(true, std::memory_order_release);

        bool doFull  = m_reqFullRefresh.exchange(false, std::memory_order_acq_rel);
        bool doAllSm = m_reqAllSmart.exchange(false, std::memory_order_acq_rel);
        int singleSm = m_reqSingleSmartDisk.exchange(-1, std::memory_order_acq_rel);
        bool doLive  = m_reqLiveUpdate.exchange(false, std::memory_order_acq_rel);

        // 1. One-time NVML initialization on background thread (never blocks UI!)
        if (!m_nvmlTried) {
            m_nvmlTried = true;
            m_hNvml = LoadLibraryW(L"nvml.dll");
            if (!m_hNvml) {
                wchar_t sysDir[MAX_PATH] = {};
                if (GetSystemDirectoryW(sysDir, MAX_PATH) > 0) {
                    std::wstring nvmlPath = std::wstring(sysDir) + L"\\nvml.dll";
                    m_hNvml = LoadLibraryW(nvmlPath.c_str());
                }
            }
            if (m_hNvml) {
                auto fnInit = reinterpret_cast<PFN_nvmlInit_v2>(GetProcAddress(m_hNvml, "nvmlInit_v2"));
                if (fnInit && fnInit() == 0) {
                    m_nvmlReady = true;
                }
            }
        }
        if (m_stopWorker.load(std::memory_order_acquire)) break;

        // 2. Full hardware & OS/Audio/Network inventory
        if (doFull) {
            QueryGpusAndDisplays();
            if (m_stopWorker.load(std::memory_order_acquire)) break;
            QueryPhysicalDisksAndVolumes();
            if (m_stopWorker.load(std::memory_order_acquire)) break;
            QueryNetworkAdapters(false);
            if (m_stopWorker.load(std::memory_order_acquire)) break;
            QuerySystemAndAudio(true);
            if (m_stopWorker.load(std::memory_order_acquire)) break;
            UpdateGpuLiveTelemetry();
            // Publish immediately so GPUs, Displays, Networks, Audio & Processes appear before SMART IOCTLs run
            PublishWorkerStateToStaged();
        }

        // 3. SMART disk attributes (can take 50-500ms on SATA/USB drives; runs purely in background)
        if (doAllSm) {
            for (size_t i = 0; i < m_wState.physDisks.size(); ++i) {
                if (m_stopWorker.load(std::memory_order_acquire)) break;
                RefreshDiskSmartOnVector(m_wState.physDisks, m_wState.physDisks[i].index);
            }
            PublishWorkerStateToStaged();
        } else if (singleSm >= 0) {
            RefreshDiskSmartOnVector(m_wState.physDisks, singleSm);
            PublishWorkerStateToStaged();
        }
        if (m_stopWorker.load(std::memory_order_acquire)) break;

        // 4. Periodic live telemetry (GPU VRAM/NVML, NIC throughput, Top Processes, Disk & ACPI Temperatures)
        if (doLive || doFull) {
            if (!doFull) {
                UpdateGpuLiveTelemetry();
                QueryNetworkAdapters(true);
                QuerySystemAndAudio(false);
            }
            ULONGLONG nowTick = GetTickCount64();
            if (doFull || m_lastDiskTempTick == 0 || nowTick - m_lastDiskTempTick >= 3000) {
                m_lastDiskTempTick = nowTick;
                if (!m_stopWorker.load(std::memory_order_acquire)) UpdateDiskTemperatures();
                if (!m_stopWorker.load(std::memory_order_acquire)) UpdateAcpiThermalZones();
            }
            PublishWorkerStateToStaged();
        }

        {
            std::lock_guard<std::mutex> lk(m_workerMutex);
            bool morePending = m_reqFullRefresh.load(std::memory_order_relaxed) ||
                               m_reqAllSmart.load(std::memory_order_relaxed) ||
                               m_reqSingleSmartDisk.load(std::memory_order_relaxed) >= 0 ||
                               m_reqLiveUpdate.load(std::memory_order_relaxed);
            if (!morePending) {
                m_workerBusy.store(false, std::memory_order_release);
                m_doneCv.notify_all();
            }
        }
    }

    CoUninitialize();
}

void HardwareInspector::InitializeStaticInventory(bool syncForCli) {
    if (m_initialized) {
        if (syncForCli) {
            WaitForAsync();
            ApplyStagedData();
        }
        return;
    }
    m_initialized = true;

    // 1. Run ONLY microsecond (< 0.4 ms total) in-memory / CPUID / cached-kernel queries on the calling thread:
    QueryCpuStatic();
    QueryMotherboardAndSmbios();
    QueryFastSystemAndRam(true);
    UpdateCpuLivePerCoreLoad();
    EnsureDiskModelsLoaded();
    QueryFastVolumes();

    // 2. Offload NVML, DXGI GPUs/Displays, Disk SMART, Network Adapters, Audio, Processes & PDH to background worker:
    RequestAsyncRefresh(true, true, -1);

    if (syncForCli) {
        WaitForAsync();
        ApplyStagedData();
    }
}

void HardwareInspector::EnsureDiskModelsLoaded() {
    if (!m_physDisks.empty()) return;
    for (int d = 0; d < 16; ++d) {
        std::wstring devPath = L"\\\\.\\PhysicalDrive" + std::to_wstring(d);
        HANDLE hDisk = CreateFileW(devPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hDisk == INVALID_HANDLE_VALUE) continue;

        HwPhysicalDisk pd;
        pd.index = d;
        pd.tempHistory.assign(60, 0.0f);

        STORAGE_PROPERTY_QUERY query = {};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType  = PropertyStandardQuery;
        BYTE propBuf[1024] = {};
        DWORD bytesRet = 0;
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), propBuf, sizeof(propBuf), &bytesRet, nullptr)) {
            auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(propBuf);
            if (desc->VendorIdOffset && desc->VendorIdOffset < sizeof(propBuf)) {
                pd.vendor = Utf8OrAsciiToWide(reinterpret_cast<const char*>(propBuf + desc->VendorIdOffset));
            }
            if (desc->ProductIdOffset && desc->ProductIdOffset < sizeof(propBuf)) {
                pd.model = Utf8OrAsciiToWide(reinterpret_cast<const char*>(propBuf + desc->ProductIdOffset));
            }
            if (desc->ProductRevisionOffset && desc->ProductRevisionOffset < sizeof(propBuf)) {
                pd.firmwareRev = Utf8OrAsciiToWide(reinterpret_cast<const char*>(propBuf + desc->ProductRevisionOffset));
            }
            if (desc->SerialNumberOffset && desc->SerialNumberOffset < sizeof(propBuf)) {
                pd.serialNumber = Utf8OrAsciiToWide(reinterpret_cast<const char*>(propBuf + desc->SerialNumberOffset));
            }
            pd.supportsCommandQueueing = (desc->CommandQueueing != FALSE);
            switch (desc->BusType) {
            case BusTypeNvme:   pd.busType = L"NVMe PCIe"; pd.mediaType = L"NVMe SSD"; pd.isNvme = true; break;
            case BusTypeSata:   pd.busType = L"SATA";      pd.mediaType = L"SATA SSD / HDD"; break;
            case BusTypeUsb:    pd.busType = L"USB";       pd.mediaType = L"USB Flash / External"; break;
            case BusTypeRAID:   pd.busType = L"RAID";      pd.mediaType = L"RAID Array"; break;
            case BusTypeSas:    pd.busType = L"SAS";       pd.mediaType = L"Enterprise SAS"; break;
            case BusTypeSpaces: pd.busType = L"Spaces";    pd.mediaType = L"Storage Pool"; break;
            default:            pd.busType = L"Storage";   pd.mediaType = L"Disk Drive"; break;
            }
        }

        STORAGE_PROPERTY_QUERY trimQ = {};
        trimQ.PropertyId = StorageDeviceTrimProperty;
        trimQ.QueryType  = PropertyStandardQuery;
        DEVICE_TRIM_DESCRIPTOR trimDesc = {};
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &trimQ, sizeof(trimQ), &trimDesc, sizeof(trimDesc), &bytesRet, nullptr)) {
            pd.supportsTrim = (trimDesc.TrimEnabled != FALSE);
            if (pd.supportsTrim && pd.busType == L"SATA") pd.mediaType = L"SATA SSD";
            else if (!pd.supportsTrim && pd.busType == L"SATA") pd.mediaType = L"SATA HDD / Drive";
        }

        STORAGE_PROPERTY_QUERY seekQ = {};
        seekQ.PropertyId = StorageDeviceSeekPenaltyProperty;
        seekQ.QueryType  = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR seekDesc = {};
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &seekQ, sizeof(seekQ), &seekDesc, sizeof(seekDesc), &bytesRet, nullptr)) {
            pd.isRotational = (seekDesc.IncursSeekPenalty != FALSE);
        }

        DISK_GEOMETRY_EX geom = {};
        if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geom, sizeof(geom), &bytesRet, nullptr)) {
            pd.capacityBytes = static_cast<uint64_t>(geom.DiskSize.QuadPart);
            pd.bytesPerSector = geom.Geometry.BytesPerSector;
        }

        CloseHandle(hDisk);
        if (!pd.model.empty() || pd.capacityBytes > 0) {
            if (pd.model.empty()) pd.model = L"Physical Drive #" + std::to_wstring(d);
            m_physDisks.push_back(pd);
        }
    }
}

void HardwareInspector::QueryFastVolumes() {
    m_volumes.clear();
    wchar_t driveStrings[512] = {};
    DWORD dLen = GetLogicalDriveStringsW(511, driveStrings);
    const wchar_t* p = driveStrings;
    while (p < driveStrings + dLen && *p) {
        std::wstring root = p;
        p += wcslen(p) + 1;

        UINT dtype = GetDriveTypeW(root.c_str());
        if (dtype == DRIVE_CDROM) continue;

        HwLogicalVolume vol;
        vol.rootPath = root;
        if (dtype == DRIVE_FIXED) vol.driveTypeStr = L"Lokalny (Fixed)";
        else if (dtype == DRIVE_REMOTE) vol.driveTypeStr = L"Sieciowy (NAS/SMB)";
        else if (dtype == DRIVE_REMOVABLE) vol.driveTypeStr = L"Wymienny (USB)";
        else vol.driveTypeStr = L"Wolumin";

        if (dtype == DRIVE_REMOTE) {
            // Never call GetDiskFreeSpaceExW on DRIVE_REMOTE synchronously (blocks for seconds on offline NAS shares!)
            std::wstring netSubKey = L"Network\\" + std::wstring(1, root[0]);
            vol.label = ReadRegSz(HKEY_CURRENT_USER, netSubKey.c_str(), L"RemotePath");
            vol.fileSystem = L"SMB / CIFS";
            vol.isReady = false;
        } else {
            ULARGE_INTEGER avail = {}, total = {}, freeB = {};
            if (GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeB) && total.QuadPart > 0) {
                vol.totalBytes = total.QuadPart;
                vol.freeBytes  = freeB.QuadPart;
                vol.usedBytes  = (total.QuadPart >= freeB.QuadPart) ? (total.QuadPart - freeB.QuadPart) : 0;
                vol.usedPct    = static_cast<float>(static_cast<double>(vol.usedBytes) * 100.0 / static_cast<double>(vol.totalBytes));
                vol.isReady    = true;

                wchar_t vName[MAX_PATH] = {};
                wchar_t fsName[MAX_PATH] = {};
                DWORD serial = 0, maxComp = 0, fsFlags = 0;
                if (GetVolumeInformationW(root.c_str(), vName, MAX_PATH, &serial, &maxComp, &fsFlags, fsName, MAX_PATH)) {
                    vol.label = vName;
                    vol.fileSystem = fsName;
                    vol.serialNumber = serial;
                    vol.isCompressed = (fsFlags & FILE_VOLUME_IS_COMPRESSED) != 0;
                    vol.isEncrypted  = (fsFlags & FILE_SUPPORTS_ENCRYPTION) != 0;
                }
            } else {
                vol.isReady = false;
            }
        }
        m_volumes.push_back(std::move(vol));
    }
}

void HardwareInspector::QueryFastSystemAndRam(bool includeStaticOs) {
    if (includeStaticOs) {
        m_system.osName = ReadRegSz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
        std::wstring dispVer = ReadRegSz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            auto fnVer = reinterpret_cast<PFN_RtlGetVersion>(GetProcAddress(hNtdll, "RtlGetVersion"));
            if (fnVer) {
                RTL_OSVERSIONINFOW osvi = {};
                osvi.dwOSVersionInfoSize = sizeof(osvi);
                if (fnVer(&osvi) == 0) {
                    if (osvi.dwBuildNumber >= 22000 && m_system.osName.find(L"Windows 10") != std::wstring::npos) {
                        size_t pos = m_system.osName.find(L"Windows 10");
                        m_system.osName.replace(pos, 10, L"Windows 11");
                    }
                    wchar_t bbuf[128] = {};
                    swprintf_s(bbuf, L"%u.%u.%u %s", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber, dispVer.c_str());
                    m_system.osBuildStr = bbuf;
                }
            }
        }

        wchar_t compBuf[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD cSz = MAX_COMPUTERNAME_LENGTH + 1;
        if (GetComputerNameW(compBuf, &cSz)) m_system.computerName = compBuf;

        wchar_t usrBuf[256] = {};
        DWORD uSz = 256;
        if (GetUserNameW(usrBuf, &uSz)) m_system.userName = usrBuf;
    }

    // Live RAM & Kernel Performance Counters (< 0.02 ms)
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        m_ram.totalBytes = ms.ullTotalPhys;
        m_ram.availBytes = ms.ullAvailPhys;
        m_ram.usedBytes  = (ms.ullTotalPhys >= ms.ullAvailPhys) ? (ms.ullTotalPhys - ms.ullAvailPhys) : 0;
        m_ram.usedPct    = (ms.ullTotalPhys > 0) ? (static_cast<float>(static_cast<double>(m_ram.usedBytes) * 100.0 / static_cast<double>(ms.ullTotalPhys))) : 0.0f;

        if (!m_ram.loadHistory.empty()) {
            m_ram.loadHistory.erase(m_ram.loadHistory.begin());
            m_ram.loadHistory.push_back(m_ram.usedPct);
        }
    }

    PERFORMANCE_INFORMATION pi = {};
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi))) {
        uint64_t pg = pi.PageSize;
        m_ram.commitTotalBytes     = static_cast<uint64_t>(pi.CommitTotal) * pg;
        m_ram.commitLimitBytes     = static_cast<uint64_t>(pi.CommitLimit) * pg;
        m_ram.commitPeakBytes      = static_cast<uint64_t>(pi.CommitPeak) * pg;
        m_ram.kernelPagedBytes     = static_cast<uint64_t>(pi.KernelPaged) * pg;
        m_ram.kernelNonPagedBytes  = static_cast<uint64_t>(pi.KernelNonpaged) * pg;
        m_ram.systemCacheBytes     = static_cast<uint64_t>(pi.SystemCache) * pg;
        m_system.processCount      = pi.ProcessCount;
        m_system.threadCount       = pi.ThreadCount;
        m_system.handleCount       = pi.HandleCount;
    }

    // Uptime (< 0.005 ms)
    m_system.uptimeSeconds = GetTickCount64() / 1000ULL;
    uint64_t d = m_system.uptimeSeconds / 86400;
    uint64_t h = (m_system.uptimeSeconds % 86400) / 3600;
    uint64_t m = (m_system.uptimeSeconds % 3600) / 60;
    uint64_t s = m_system.uptimeSeconds % 60;
    wchar_t upBuf[128] = {};
    swprintf_s(upBuf, L"%llud %02lluh %02llum %02llus", d, h, m, s);
    m_system.uptimeStr = upBuf;

    // Power status (< 0.01 ms)
    SYSTEM_POWER_STATUS sps = {};
    if (GetSystemPowerStatus(&sps)) {
        if (sps.BatteryFlag == 128 || sps.BatteryFlag == 255) {
            m_system.powerStatusStr = L"Zasilanie sieciowe AC (Stacja robocza / Desktop)";
        } else {
            wchar_t pbuf[128] = {};
            swprintf_s(pbuf, L"Bateria: %u%% (%s)",
                       static_cast<unsigned int>(sps.BatteryLifePercent),
                       sps.ACLineStatus == 1 ? L"Ładowanie AC" : L"Na baterii");
            m_system.powerStatusStr = pbuf;
        }
    }
}

void HardwareInspector::RefreshFullInventory(bool syncForCli) {
    QueryCpuStatic();
    QueryMotherboardAndSmbios();
    QueryFastSystemAndRam(true);
    UpdateCpuLivePerCoreLoad();
    RequestAsyncRefresh(true, true, -1);
    if (syncForCli) {
        WaitForAsync();
        ApplyStagedData();
    }
}

void HardwareInspector::QueryCpuStatic() {
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 0);
    int maxLeaf = cpuInfo[0];

    char vendorBuf[13] = {};
    std::memcpy(vendorBuf + 0, &cpuInfo[1], 4);
    std::memcpy(vendorBuf + 4, &cpuInfo[3], 4);
    std::memcpy(vendorBuf + 8, &cpuInfo[2], 4);
    m_cpu.vendor = Utf8OrAsciiToWide(vendorBuf);

    // CPUID Brand String (0x80000002..0x80000004)
    __cpuid(cpuInfo, 0x80000000);
    unsigned int maxExtLeaf = static_cast<unsigned int>(cpuInfo[0]);
    if (maxExtLeaf >= 0x80000004) {
        char brandBuf[64] = {};
        int b0[4] = {}, b1[4] = {}, b2[4] = {};
        __cpuid(b0, 0x80000002);
        __cpuid(b1, 0x80000003);
        __cpuid(b2, 0x80000004);
        std::memcpy(brandBuf + 0,  b0, 16);
        std::memcpy(brandBuf + 16, b1, 16);
        std::memcpy(brandBuf + 32, b2, 16);
        m_cpu.brand = Utf8OrAsciiToWide(brandBuf);
    }
    if (m_cpu.brand.empty()) {
        m_cpu.brand = ReadRegSz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    }

    m_cpu.baseFreqMHz = ReadRegDword(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", 0);

    // Family / Model / Stepping & ISA features
    m_cpu.isaFeatures.clear();
    if (maxLeaf >= 1) {
        __cpuid(cpuInfo, 1);
        int steppingId = cpuInfo[0] & 0xF;
        int modelId = (cpuInfo[0] >> 4) & 0xF;
        int familyId = (cpuInfo[0] >> 8) & 0xF;
        int extModel = (cpuInfo[0] >> 16) & 0xF;
        int extFamily = (cpuInfo[0] >> 20) & 0xFF;
        if (familyId == 6 || familyId == 15) modelId += (extModel << 4);
        if (familyId == 15) familyId += extFamily;
        m_cpu.family = familyId;
        m_cpu.model = modelId;
        m_cpu.stepping = steppingId;

        int ecx = cpuInfo[2];
        int edx = cpuInfo[3];
        if (edx & (1 << 23)) m_cpu.isaFeatures.push_back(L"MMX");
        if (edx & (1 << 25)) m_cpu.isaFeatures.push_back(L"SSE");
        if (edx & (1 << 26)) m_cpu.isaFeatures.push_back(L"SSE2");
        if (ecx & (1 << 0))  m_cpu.isaFeatures.push_back(L"SSE3");
        if (ecx & (1 << 9))  m_cpu.isaFeatures.push_back(L"SSSE3");
        if (ecx & (1 << 19)) m_cpu.isaFeatures.push_back(L"SSE4.1");
        if (ecx & (1 << 20)) m_cpu.isaFeatures.push_back(L"SSE4.2");
        if (ecx & (1 << 25)) m_cpu.isaFeatures.push_back(L"AES-NI");
        if (ecx & (1 << 28)) m_cpu.isaFeatures.push_back(L"AVX");
        if (ecx & (1 << 12)) m_cpu.isaFeatures.push_back(L"FMA3");
        if (ecx & (1 << 29)) m_cpu.isaFeatures.push_back(L"F16C");
        if (ecx & (1 << 30)) m_cpu.isaFeatures.push_back(L"RDRAND");
    }
    if (maxLeaf >= 7) {
        __cpuidex(cpuInfo, 7, 0);
        int ebx = cpuInfo[1];
        if (ebx & (1 << 5))  m_cpu.isaFeatures.push_back(L"AVX2");
        if (ebx & (1 << 3))  m_cpu.isaFeatures.push_back(L"BMI1");
        if (ebx & (1 << 8))  m_cpu.isaFeatures.push_back(L"BMI2");
        if (ebx & (1 << 29)) m_cpu.isaFeatures.push_back(L"SHA");
        if (ebx & (1 << 16)) m_cpu.isaFeatures.push_back(L"AVX-512");
    }
    if (IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED)) {
        m_cpu.isaFeatures.push_back(L"VT-x / AMD-V");
    }

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    m_cpu.logicalThreads = si.dwNumberOfProcessors;
    m_cpu.architecture = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? L"x64 (AMD64 / 64-bit)" :
                         (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ? L"ARM64" : L"x86";

    // Query exact Physical Cores, Packages, NUMA Nodes & L1/L2/L3 Cache Topology
    m_cpu.physicalCores = 0;
    m_cpu.packages = 0;
    m_cpu.numaNodes = 0;
    m_cpu.l1DataKB = m_cpu.l1DataCount = 0;
    m_cpu.l1InstKB = m_cpu.l1InstCount = 0;
    m_cpu.l2KB = m_cpu.l2Count = 0;
    m_cpu.l3KB = m_cpu.l3Count = 0;

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &len);
    if (len > 0) {
        std::vector<uint8_t> buf(len);
        if (GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len)) {
            uint8_t* ptr = buf.data();
            uint8_t* end = buf.data() + len;
            while (ptr < end) {
                auto info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
                if (info->Size == 0) break;
                switch (info->Relationship) {
                case RelationProcessorCore:
                    ++m_cpu.physicalCores;
                    break;
                case RelationProcessorPackage:
                    ++m_cpu.packages;
                    break;
                case RelationNumaNode:
                    ++m_cpu.numaNodes;
                    break;
                case RelationCache: {
                    const CACHE_RELATIONSHIP& c = info->Cache;
                    uint32_t kb = c.CacheSize / 1024;
                    m_cpu.cacheLineBytes = c.LineSize;
                    if (c.Level == 1) {
                        if (c.Type == CacheData || c.Type == CacheUnified) {
                            m_cpu.l1DataKB += kb;
                            ++m_cpu.l1DataCount;
                        } else if (c.Type == CacheInstruction) {
                            m_cpu.l1InstKB += kb;
                            ++m_cpu.l1InstCount;
                        }
                    } else if (c.Level == 2) {
                        m_cpu.l2KB += kb;
                        ++m_cpu.l2Count;
                    } else if (c.Level == 3) {
                        m_cpu.l3KB += kb;
                        ++m_cpu.l3Count;
                    }
                    break;
                }
                default:
                    break;
                }
                ptr += info->Size;
            }
        }
    }
    if (m_cpu.physicalCores == 0) m_cpu.physicalCores = std::max(1u, m_cpu.logicalThreads / 2);
    if (m_cpu.packages == 0) m_cpu.packages = 1;
    if (m_cpu.numaNodes == 0) m_cpu.numaNodes = 1;
}

void HardwareInspector::QueryMotherboardAndSmbios() {
    m_ram.dimms.clear();

    // Registry fallbacks first
    const wchar_t* biosKey = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    m_mobo.boardManufacturer = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardManufacturer");
    m_mobo.boardProduct      = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardProduct");
    m_mobo.boardVersion      = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardVersion");
    m_mobo.biosVendor        = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"BIOSVendor");
    m_mobo.biosVersion       = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"BIOSVersion");
    m_mobo.biosReleaseDate   = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"BIOSReleaseDate");
    m_mobo.systemManufacturer = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"SystemManufacturer");
    m_mobo.systemProductName  = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"SystemProductName");
    m_mobo.systemSku          = ReadRegSz(HKEY_LOCAL_MACHINE, biosKey, L"SystemSKU");

    DWORD uefiSecure = ReadRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", L"UEFISecureBootEnabled", 0);
    m_mobo.isSecureBoot = (uefiSecure != 0);

    // Raw SMBIOS Table Parser ('RSMB') for BIOS, Baseboard, CPU Socket/MaxMHz, and Physical RAM DIMM Modules (Type 17)
    UINT smbiosSize = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (smbiosSize > 8) {
        std::vector<uint8_t> raw(smbiosSize);
        if (GetSystemFirmwareTable('RSMB', 0, raw.data(), smbiosSize) == smbiosSize) {
            struct RawSMBIOSDataHeader {
                uint8_t Used20CallingMethod;
                uint8_t SMBIOSMajorVersion;
                uint8_t SMBIOSMinorVersion;
                uint8_t DmiRevision;
                uint32_t Length;
            };
            auto hdr = reinterpret_cast<const RawSMBIOSDataHeader*>(raw.data());
            const uint8_t* ptr = raw.data() + sizeof(RawSMBIOSDataHeader);
            const uint8_t* end = raw.data() + std::min<size_t>(smbiosSize, sizeof(RawSMBIOSDataHeader) + hdr->Length);

            while (ptr + 4 <= end) {
                uint8_t type = ptr[0];
                uint8_t len  = ptr[1];
                if (len < 4 || ptr + len > end) break;

                // Find end of unformatted string-set (double-null terminated)
                const uint8_t* next = ptr + len;
                while (next + 1 < end && !(next[0] == 0 && next[1] == 0)) {
                    ++next;
                }
                next += 2;

                if (type == 0 && len >= 0x12) { // BIOS Information
                    std::wstring v = GetSmbiosString(ptr, len, next, ptr[4]);
                    std::wstring ver = GetSmbiosString(ptr, len, next, ptr[5]);
                    std::wstring dt = GetSmbiosString(ptr, len, next, ptr[8]);
                    if (!v.empty()) m_mobo.biosVendor = v;
                    if (!ver.empty()) m_mobo.biosVersion = ver;
                    if (!dt.empty()) m_mobo.biosReleaseDate = dt;
                } else if (type == 1 && len >= 0x08) { // System Information
                    std::wstring mf = GetSmbiosString(ptr, len, next, ptr[4]);
                    std::wstring pr = GetSmbiosString(ptr, len, next, ptr[5]);
                    if (!mf.empty()) m_mobo.systemManufacturer = mf;
                    if (!pr.empty()) m_mobo.systemProductName = pr;
                } else if (type == 2 && len >= 0x08) { // Baseboard (Motherboard) Information
                    std::wstring mf = GetSmbiosString(ptr, len, next, ptr[4]);
                    std::wstring pr = GetSmbiosString(ptr, len, next, ptr[5]);
                    std::wstring vr = GetSmbiosString(ptr, len, next, ptr[6]);
                    std::wstring sn = GetSmbiosString(ptr, len, next, ptr[7]);
                    if (!mf.empty()) m_mobo.boardManufacturer = mf;
                    if (!pr.empty()) m_mobo.boardProduct = pr;
                    if (!vr.empty()) m_mobo.boardVersion = vr;
                    if (!sn.empty()) m_mobo.boardSerial = sn;
                } else if (type == 4 && len >= 0x18) { // Processor Information (Socket & Max Speed)
                    std::wstring sock = GetSmbiosString(ptr, len, next, ptr[4]);
                    uint16_t maxMHz = *reinterpret_cast<const uint16_t*>(ptr + 0x14);
                    uint16_t curMHz = *reinterpret_cast<const uint16_t*>(ptr + 0x16);
                    if (!sock.empty()) m_cpu.socketDesignation = sock;
                    if (maxMHz > 0) m_cpu.maxFreqMHz = maxMHz;
                    if (curMHz > 0 && m_cpu.baseFreqMHz == 0) m_cpu.baseFreqMHz = curMHz;
                } else if (type == 17 && len >= 0x15) { // Physical Memory Device (DIMM Stick)
                    uint16_t rawSize = *reinterpret_cast<const uint16_t*>(ptr + 0x0C);
                    uint64_t sizeBytes = 0;
                    if (rawSize != 0 && rawSize != 0xFFFF) {
                        if (rawSize == 0x7FFF && len >= 0x20) {
                            uint32_t extSizeMB = (*reinterpret_cast<const uint32_t*>(ptr + 0x1C)) & 0x7FFFFFFFu;
                            sizeBytes = static_cast<uint64_t>(extSizeMB) * 1024ULL * 1024ULL;
                        } else if (rawSize & 0x8000) {
                            sizeBytes = static_cast<uint64_t>(rawSize & 0x7FFF) * 1024ULL;
                        } else {
                            sizeBytes = static_cast<uint64_t>(rawSize) * 1024ULL * 1024ULL;
                        }
                    }

                    if (sizeBytes > 0) {
                        HwDimmSlot dimm;
                        dimm.sizeBytes = sizeBytes;
                        dimm.dataWidthBits = *reinterpret_cast<const uint16_t*>(ptr + 0x0A);
                        uint8_t ff = ptr[0x0E];
                        dimm.formFactor = (ff == 0x0D) ? L"SODIMM" : L"DIMM";
                        dimm.slotLocator = GetSmbiosString(ptr, len, next, ptr[0x10]);
                        dimm.bankLocator = GetSmbiosString(ptr, len, next, ptr[0x11]);

                        uint8_t memType = ptr[0x12];
                        switch (memType) {
                        case 0x18: dimm.memoryType = L"DDR3"; break;
                        case 0x1A: dimm.memoryType = L"DDR4"; break;
                        case 0x1E: dimm.memoryType = L"LPDDR4"; break;
                        case 0x22: dimm.memoryType = L"DDR5"; break;
                        case 0x23: dimm.memoryType = L"LPDDR5"; break;
                        default:   dimm.memoryType = L"DDR"; break;
                        }

                        if (len >= 0x17) {
                            dimm.speedMHz = *reinterpret_cast<const uint16_t*>(ptr + 0x15);
                        }
                        if (len >= 0x1B) {
                            dimm.manufacturer = GetSmbiosString(ptr, len, next, ptr[0x17]);
                            dimm.serialNumber = GetSmbiosString(ptr, len, next, ptr[0x18]);
                            dimm.partNumber   = GetSmbiosString(ptr, len, next, ptr[0x1A]);
                        }
                        if (len >= 0x22) {
                            dimm.configuredSpeedMHz = *reinterpret_cast<const uint16_t*>(ptr + 0x20);
                        }
                        if (dimm.configuredSpeedMHz == 0) dimm.configuredSpeedMHz = dimm.speedMHz;
                        m_ram.dimms.push_back(dimm);
                    }
                }

                if (type == 127) break;
                ptr = next;
            }
        }
    }
}

void HardwareInspector::QueryGpusAndDisplays() {
    m_wState.gpus.clear();
    m_wState.displays.clear();

    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pFactory))) && pFactory) {
        UINT idx = 0;
        IDXGIAdapter1* pAdapter = nullptr;
        while (pFactory->EnumAdapters1(idx, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
                // Skip software Microsoft Basic Render Driver unless it's the only adapter
                bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || desc.VendorId == 0x1414;
                if (!isSoftware || m_wState.gpus.empty()) {
                    HwGpuInfo gpu;
                    gpu.name = desc.Description;
                    gpu.vendorId = desc.VendorId;
                    gpu.deviceId = desc.DeviceId;
                    gpu.subSysId = desc.SubSysId;
                    gpu.revision = desc.Revision;
                    gpu.dedicatedVramBytes = desc.DedicatedVideoMemory;
                    gpu.dedicatedSystemBytes = desc.DedicatedSystemMemory;
                    gpu.sharedSysMemBytes = desc.SharedSystemMemory;

                    if (desc.VendorId == 0x10DE) gpu.vendorName = L"NVIDIA Corporation";
                    else if (desc.VendorId == 0x1002 || desc.VendorId == 0x1022) gpu.vendorName = L"AMD / Radeon";
                    else if (desc.VendorId == 0x8086) gpu.vendorName = L"Intel Corporation";
                    else gpu.vendorName = L"Graphics Adapter";

                    m_wState.gpus.push_back(gpu);
                }
            }
            pAdapter->Release();
            ++idx;
        }
        pFactory->Release();
    }

    // Enumerate active displays / monitors
    DISPLAY_DEVICEW dd = {};
    dd.cb = sizeof(dd);
    for (DWORD devIdx = 0; EnumDisplayDevicesW(nullptr, devIdx, &dd, 0); ++devIdx) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            DEVMODEW dm = {};
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsExW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0)) {
                HwDisplayInfo disp;
                disp.deviceName = dd.DeviceName;
                disp.gpuAdapterName = dd.DeviceString;
                disp.width = dm.dmPelsWidth;
                disp.height = dm.dmPelsHeight;
                disp.refreshHz = dm.dmDisplayFrequency;
                disp.bitsPerPel = dm.dmBitsPerPel;
                disp.isPrimary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

                DISPLAY_DEVICEW monDd = {};
                monDd.cb = sizeof(monDd);
                if (EnumDisplayDevicesW(dd.DeviceName, 0, &monDd, 0)) {
                    disp.monitorName = monDd.DeviceString;
                } else {
                    disp.monitorName = L"Generic PnP Monitor";
                }
                m_wState.displays.push_back(disp);
            }
        }
        dd.cb = sizeof(dd);
    }
}

void HardwareInspector::QueryPhysicalDisksAndVolumes() {
    // Keep temperature history / peak / existing SMART across a full inventory refresh
    std::vector<HwPhysicalDisk> previousDisks = std::move(m_wState.physDisks);
    m_wState.physDisks.clear();
    m_wState.volumes.clear();

    // 1. Query Physical Drives (\\.\PhysicalDrive0..15) via zero-access handle + IOCTL_STORAGE_QUERY_PROPERTY
    for (int d = 0; d < 16; ++d) {
        HANDLE hDisk = OpenPhysicalDrive(d, 0);
        if (hDisk == INVALID_HANDLE_VALUE) continue;

        HwPhysicalDisk pd;
        pd.index = d;
        pd.tempHistory.assign(60, 0.0f);

        STORAGE_PROPERTY_QUERY spq = {};
        spq.PropertyId = StorageDeviceProperty;
        spq.QueryType  = PropertyStandardQuery;
        uint8_t propBuf[1024] = {};
        DWORD bytesRet = 0;
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), propBuf, sizeof(propBuf), &bytesRet, nullptr)) {
            auto desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(propBuf);
            if (desc->VendorIdOffset && desc->VendorIdOffset < sizeof(propBuf)) {
                pd.vendor = Utf8OrAsciiToWide(reinterpret_cast<const char*>(propBuf + desc->VendorIdOffset));
            }
            if (desc->ProductIdOffset && desc->ProductIdOffset < sizeof(propBuf)) {
                pd.model = Utf8OrAsciiToWide(reinterpret_cast<const char*>(propBuf + desc->ProductIdOffset));
            }
            if (desc->ProductRevisionOffset && desc->ProductRevisionOffset < sizeof(propBuf)) {
                pd.firmwareRev = Utf8OrAsciiToWide(reinterpret_cast<const char*>(propBuf + desc->ProductRevisionOffset));
            }
            if (desc->SerialNumberOffset && desc->SerialNumberOffset < sizeof(propBuf)) {
                pd.serialNumber = Utf8OrAsciiToWide(reinterpret_cast<const char*>(propBuf + desc->SerialNumberOffset));
            }
            pd.supportsCommandQueueing = (desc->CommandQueueing != FALSE);

            switch (desc->BusType) {
            case BusTypeNvme:   pd.busType = L"NVMe PCIe"; pd.mediaType = L"NVMe SSD"; pd.isNvme = true; break;
            case BusTypeSata:   pd.busType = L"SATA";      pd.mediaType = L"SATA SSD / HDD"; break;
            case BusTypeUsb:    pd.busType = L"USB";       pd.mediaType = L"USB Flash / External"; break;
            case BusTypeRAID:   pd.busType = L"RAID";      pd.mediaType = L"RAID Array"; break;
            case BusTypeSas:    pd.busType = L"SAS";       pd.mediaType = L"Enterprise SAS"; break;
            case BusTypeSpaces: pd.busType = L"Spaces";    pd.mediaType = L"Storage Pool"; break;
            default:            pd.busType = L"Storage";   pd.mediaType = L"Disk Drive"; break;
            }
        }

        // Check TRIM support (if TRIM is enabled or BusType is NVMe, it's definitely an SSD)
        STORAGE_PROPERTY_QUERY trimQ = {};
        trimQ.PropertyId = StorageDeviceTrimProperty;
        trimQ.QueryType  = PropertyStandardQuery;
        DEVICE_TRIM_DESCRIPTOR trimDesc = {};
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &trimQ, sizeof(trimQ), &trimDesc, sizeof(trimDesc), &bytesRet, nullptr)) {
            pd.supportsTrim = (trimDesc.TrimEnabled != FALSE);
            if (pd.supportsTrim && pd.busType == L"SATA") {
                pd.mediaType = L"SATA SSD";
            } else if (!pd.supportsTrim && pd.busType == L"SATA") {
                pd.mediaType = L"SATA HDD / Drive";
            }
        }

        // Rotational media detection (seek penalty => spinning HDD)
        STORAGE_PROPERTY_QUERY seekQ = {};
        seekQ.PropertyId = StorageDeviceSeekPenaltyProperty;
        seekQ.QueryType  = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR seekDesc = {};
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &seekQ, sizeof(seekQ), &seekDesc, sizeof(seekDesc), &bytesRet, nullptr)) {
            pd.isRotational = (seekDesc.IncursSeekPenalty != FALSE);
        }

        DISK_GEOMETRY_EX geom = {};
        if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geom, sizeof(geom), &bytesRet, nullptr)) {
            pd.capacityBytes = static_cast<uint64_t>(geom.DiskSize.QuadPart);
            pd.bytesPerSector = geom.Geometry.BytesPerSector;
        }

        // Initial temperature reading (no elevation needed where the driver supports it)
        int tNow = -1, tWarn = -1, tCrit = -1;
        if (QueryStorageTemperature(hDisk, tNow, tWarn, tCrit)) {
            pd.tempC = tNow;
            pd.tempPeakC = tNow;
            pd.tempWarnC = tWarn;
            pd.tempCritC = tCrit;
        }

        CloseHandle(hDisk);
        if (!pd.model.empty() || pd.capacityBytes > 0) {
            if (pd.model.empty()) pd.model = L"Physical Drive #" + std::to_wstring(d);
            for (const auto& old : previousDisks) {
                if (old.index == pd.index && old.model == pd.model) {
                    pd.tempHistory = old.tempHistory;
                    pd.tempPeakC = std::max(pd.tempPeakC, old.tempPeakC);
                    if (old.smartState != HwSmartState::NotQueried) {
                        pd.smartState = old.smartState;
                        pd.healthLevel = old.healthLevel;
                        pd.lifeRemainingPct = old.lifeRemainingPct;
                        pd.powerOnHours = old.powerOnHours;
                        pd.powerCycles = old.powerCycles;
                        pd.bytesWritten = old.bytesWritten;
                        pd.bytesRead = old.bytesRead;
                        pd.smartAttrs = old.smartAttrs;
                    }
                    break;
                }
            }
            m_wState.physDisks.push_back(pd);
        }
    }

    // 2. Enumerate Logical Volumes
    wchar_t driveStrings[512] = {};
    DWORD dLen = GetLogicalDriveStringsW(511, driveStrings);
    const wchar_t* p = driveStrings;
    while (p < driveStrings + dLen && *p) {
        std::wstring root = p;
        p += wcslen(p) + 1;

        UINT dtype = GetDriveTypeW(root.c_str());
        if (dtype == DRIVE_CDROM) continue;

        HwLogicalVolume vol;
        vol.rootPath = root;
        if (dtype == DRIVE_FIXED) vol.driveTypeStr = L"Lokalny (Fixed)";
        else if (dtype == DRIVE_REMOTE) vol.driveTypeStr = L"Sieciowy (NAS/SMB)";
        else if (dtype == DRIVE_REMOVABLE) vol.driveTypeStr = L"Wymienny (USB)";
        else vol.driveTypeStr = L"Wolumin";

        if (dtype == DRIVE_REMOTE) {
            // Never call GetDiskFreeSpaceExW on DRIVE_REMOTE: unreachable SMB/NAS shares block for 5-15s in mrxsmb.sys!
            std::wstring netSubKey = L"Network\\" + std::wstring(1, root[0]);
            vol.label = ReadRegSz(HKEY_CURRENT_USER, netSubKey.c_str(), L"RemotePath");
            vol.fileSystem = L"SMB / CIFS";
            vol.isReady = false;
        } else {
            ULARGE_INTEGER avail = {}, total = {}, freeB = {};
            if (GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeB) && total.QuadPart > 0) {
                vol.totalBytes = total.QuadPart;
                vol.freeBytes  = freeB.QuadPart;
                vol.usedBytes  = (total.QuadPart >= freeB.QuadPart) ? (total.QuadPart - freeB.QuadPart) : 0;
                vol.usedPct    = static_cast<float>(static_cast<double>(vol.usedBytes) * 100.0 / static_cast<double>(vol.totalBytes));
                vol.isReady    = true;

                wchar_t vName[MAX_PATH] = {};
                wchar_t fsName[MAX_PATH] = {};
                DWORD serial = 0, maxComp = 0, fsFlags = 0;
                if (GetVolumeInformationW(root.c_str(), vName, MAX_PATH, &serial, &maxComp, &fsFlags, fsName, MAX_PATH)) {
                    vol.label = vName;
                    vol.fileSystem = fsName;
                    vol.serialNumber = serial;
                    vol.isCompressed = (fsFlags & FILE_VOLUME_IS_COMPRESSED) != 0;
                    vol.isEncrypted  = (fsFlags & FILE_SUPPORTS_ENCRYPTION) != 0;
                }
            } else {
                vol.isReady = false;
            }
        }
        m_wState.volumes.push_back(vol);
    }
}

bool HardwareInspector::IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev = {};
    DWORD sz = sizeof(elev);
    bool res = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &sz) && elev.TokenIsElevated;
    CloseHandle(token);
    return res;
}

void HardwareInspector::RefreshDiskSmart(int physIndex) {
    if (m_workerStarted) {
        RequestAsyncRefresh(false, false, physIndex);
        WaitForAsync();
        ApplyStagedData();
    } else {
        RefreshDiskSmartOnVector(m_physDisks, physIndex);
    }
}

void HardwareInspector::RefreshDiskSmartOnVector(std::vector<HwPhysicalDisk>& disks, int physIndex) {
    HwPhysicalDisk* pd = nullptr;
    for (auto& d : disks) {
        if (d.index == physIndex) { pd = &d; break; }
    }
    if (!pd) return;

    pd->smartAttrs.clear();
    pd->smartState = HwSmartState::Unsupported;
    pd->healthLevel = -1;
    pd->lifeRemainingPct = -1;
    pd->powerOnHours = -1;
    pd->powerCycles = -1;
    pd->bytesWritten = -1;
    pd->bytesRead = -1;

    // Temperature straight from the storage stack (no elevation required where supported)
    {
        HANDLE h = OpenPhysicalDrive(physIndex, 0);
        if (h != INVALID_HANDLE_VALUE) {
            int t = -1, w = -1, c = -1;
            if (QueryStorageTemperature(h, t, w, c)) {
                pd->tempC = t;
                pd->tempPeakC = std::max(pd->tempPeakC, t);
                if (w > 0) pd->tempWarnC = w;
                if (c > 0) pd->tempCritC = c;
            }
            CloseHandle(h);
        }
    }

    if (pd->isNvme) {
        uint8_t log[512] = {};
        bool ok = false;
        DWORD err = 0;
        HANDLE h = OpenPhysicalDrive(physIndex, 0);
        if (h != INVALID_HANDLE_VALUE) {
            ok = QueryNvmeHealthLog(h, log);
            if (!ok) err = GetLastError();
            CloseHandle(h);
        }
        if (!ok) {
            h = OpenPhysicalDrive(physIndex, GENERIC_READ | GENERIC_WRITE);
            if (h != INVALID_HANDLE_VALUE) {
                ok = QueryNvmeHealthLog(h, log);
                if (!ok) err = GetLastError();
                CloseHandle(h);
            } else {
                err = GetLastError();
            }
        }
        if (!ok) {
            pd->smartState = (err == ERROR_ACCESS_DENIED) ? HwSmartState::NeedsAdmin : HwSmartState::Unsupported;
            return;
        }

        pd->smartState = HwSmartState::Ok;
        uint8_t critWarn = log[0];
        int compositeC = static_cast<int>(static_cast<uint16_t>(log[1] | (log[2] << 8))) - 273;
        int spare = log[3];
        int spareThr = log[4];
        int pctUsed = log[5];
        uint64_t unitsRead    = ReadLe64(log + 32);
        uint64_t unitsWritten = ReadLe64(log + 48);
        uint64_t hostReads    = ReadLe64(log + 64);
        uint64_t hostWrites   = ReadLe64(log + 80);
        uint64_t busyMin      = ReadLe64(log + 96);
        uint64_t cycles       = ReadLe64(log + 112);
        uint64_t poh          = ReadLe64(log + 128);
        uint64_t unsafeSd     = ReadLe64(log + 144);
        uint64_t mediaErr     = ReadLe64(log + 160);
        uint64_t errLogs      = ReadLe64(log + 176);
        uint32_t warnTime     = static_cast<uint32_t>(log[192] | (log[193] << 8) | (log[194] << 16) | (log[195] << 24));
        uint32_t critTime     = static_cast<uint32_t>(log[196] | (log[197] << 8) | (log[198] << 16) | (log[199] << 24));

        if (compositeC > -50 && compositeC < 150) {
            pd->tempC = compositeC;
            pd->tempPeakC = std::max(pd->tempPeakC, compositeC);
        }
        pd->lifeRemainingPct = std::clamp(100 - pctUsed, 0, 100);
        pd->powerOnHours = static_cast<int64_t>(poh);
        pd->powerCycles = static_cast<int64_t>(cycles);
        pd->bytesWritten = static_cast<int64_t>(unitsWritten * 512000ull);
        pd->bytesRead = static_cast<int64_t>(unitsRead * 512000ull);

        auto add = [&](const wchar_t* name, const std::wstring& text, int status, uint64_t raw = 0) {
            HwSmartAttr a;
            a.name = name;
            a.rawText = text;
            a.raw = raw;
            a.status = status;
            pd->smartAttrs.push_back(std::move(a));
        };
        auto tempText = [](int c) { wchar_t b[32] = {}; swprintf_s(b, L"%d\x00B0" L"C", c); return std::wstring(b); };
        auto pctText = [](int p) { return std::to_wstring(p) + L"%"; };
        auto sizeText = [](uint64_t units, uint64_t perUnit) {
            return std::to_wstring(units) + L"  (\x2248 " + FileEngine::FormatSizeDetailed(units * perUnit) + L")";
        };

        wchar_t cwBuf[48] = {};
        swprintf_s(cwBuf, L"0x%02X%s", critWarn, critWarn == 0 ? L"  (OK)" : L"");
        add(L"Critical Warning", cwBuf, critWarn ? 2 : 0, critWarn);
        add(L"Composite Temperature", tempText(compositeC), -1, static_cast<uint64_t>(std::max(0, compositeC)));
        add(L"Available Spare", pctText(spare), spare <= spareThr ? 2 : 0, static_cast<uint64_t>(spare));
        add(L"Available Spare Threshold", pctText(spareThr), -1, static_cast<uint64_t>(spareThr));
        add(L"Percentage Used", pctText(pctUsed), pctUsed >= 100 ? 2 : (pctUsed >= 90 ? 1 : 0), static_cast<uint64_t>(pctUsed));
        add(L"Data Units Read", sizeText(unitsRead, 512000ull), -1, unitsRead);
        add(L"Data Units Written", sizeText(unitsWritten, 512000ull), -1, unitsWritten);
        add(L"Host Read Commands", std::to_wstring(hostReads), -1, hostReads);
        add(L"Host Write Commands", std::to_wstring(hostWrites), -1, hostWrites);
        add(L"Controller Busy Time", std::to_wstring(busyMin) + L" min", -1, busyMin);
        add(L"Power Cycles", std::to_wstring(cycles), -1, cycles);
        add(L"Power On Hours", std::to_wstring(poh) + L" h", -1, poh);
        add(L"Unsafe Shutdowns", std::to_wstring(unsafeSd), -1, unsafeSd);
        add(L"Media and Data Integrity Errors", std::to_wstring(mediaErr), mediaErr > 0 ? 1 : 0, mediaErr);
        add(L"Error Information Log Entries", std::to_wstring(errLogs), -1, errLogs);
        add(L"Warning Composite Temp Time", std::to_wstring(warnTime) + L" min", -1, warnTime);
        add(L"Critical Composite Temp Time", std::to_wstring(critTime) + L" min", critTime > 0 ? 1 : 0, critTime);
        for (int s = 0; s < 8; ++s) {
            uint16_t k = static_cast<uint16_t>(log[200 + s * 2] | (log[201 + s * 2] << 8));
            if (k == 0) continue;
            std::wstring nm = L"Temperature Sensor " + std::to_wstring(s + 1);
            add(nm.c_str(), tempText(static_cast<int>(k) - 273), -1, k);
        }
    } else if (pd->busType == L"SATA" || pd->busType == L"Storage") {
        HANDLE h = OpenPhysicalDrive(physIndex, GENERIC_READ | GENERIC_WRITE);
        if (h == INVALID_HANDLE_VALUE) {
            pd->smartState = (GetLastError() == ERROR_ACCESS_DENIED) ? HwSmartState::NeedsAdmin : HwSmartState::Unsupported;
            return;
        }
        // Make sure SMART is enabled (harmless if it already is)
        {
            SENDCMDINPARAMS en = {};
            en.cBufferSize = 0;
            en.bDriveNumber = static_cast<BYTE>(physIndex);
            en.irDriveRegs.bFeaturesReg  = 0xD8;
            en.irDriveRegs.bCylLowReg    = 0x4F;
            en.irDriveRegs.bCylHighReg   = 0xC2;
            en.irDriveRegs.bDriveHeadReg = static_cast<BYTE>(0xA0 | ((physIndex & 1) << 4));
            en.irDriveRegs.bCommandReg   = 0xB0;
            SENDCMDOUTPARAMS eo = {};
            DWORD r = 0;
            DeviceIoControl(h, SMART_SEND_DRIVE_COMMAND, &en, sizeof(SENDCMDINPARAMS) - 1, &eo, sizeof(eo), &r, nullptr);
        }
        uint8_t attrBuf[512] = {};
        uint8_t thrBuf[512] = {};
        bool okA = AtaSmartRead(h, physIndex, 0xD0, attrBuf);
        bool okT = okA && AtaSmartRead(h, physIndex, 0xD1, thrBuf);
        CloseHandle(h);
        if (!okA) {
            pd->smartState = HwSmartState::Unsupported;
            return;
        }
        if (!okT) std::memset(thrBuf, 0, sizeof(thrBuf));

        pd->smartState = HwSmartState::Ok;
        ParseAtaAttributes(attrBuf, thrBuf, pd->smartAttrs);

        if (auto a = FindAttr(pd->smartAttrs, 9))  pd->powerOnHours = static_cast<int64_t>(a->raw & 0xFFFFFFFFull);
        if (auto a = FindAttr(pd->smartAttrs, 12)) pd->powerCycles  = static_cast<int64_t>(a->raw & 0xFFFFFFFFull);
        if (auto a = FindAttr(pd->smartAttrs, 241)) pd->bytesWritten = static_cast<int64_t>((a->raw & 0xFFFFFFFFFFFFull) * 512ull);
        if (auto a = FindAttr(pd->smartAttrs, 242)) pd->bytesRead    = static_cast<int64_t>((a->raw & 0xFFFFFFFFFFFFull) * 512ull);
        for (int id : { 177, 231, 233 }) {
            if (auto a = FindAttr(pd->smartAttrs, id)) {
                pd->lifeRemainingPct = std::clamp(a->current, 0, 100);
                break;
            }
        }
        if (pd->tempC < 0) {
            const HwSmartAttr* ta = FindAttr(pd->smartAttrs, 194);
            if (!ta) ta = FindAttr(pd->smartAttrs, 190);
            if (ta) {
                int t = static_cast<int>(ta->raw & 0xFF);
                if (t > 0 && t < 150) {
                    pd->tempC = t;
                    pd->tempPeakC = std::max(pd->tempPeakC, t);
                }
            }
        }
    } else {
        pd->smartState = HwSmartState::Unsupported;
        return;
    }

    int level = 0;
    for (const auto& a : pd->smartAttrs) level = std::max(level, a.status);
    pd->healthLevel = level;
}

void HardwareInspector::UpdateDiskTemperatures() {
    for (auto& pd : m_wState.physDisks) {
        HANDLE h = OpenPhysicalDrive(pd.index, 0);
        int t = -1, w = -1, c = -1;
        bool ok = false;
        if (h != INVALID_HANDLE_VALUE) {
            ok = QueryStorageTemperature(h, t, w, c);
            CloseHandle(h);
        }
        if (!ok && !pd.isNvme && pd.smartState == HwSmartState::Ok && IsProcessElevated()) {
            // Elevated: fall back to the SMART temperature attribute of SATA drives
            HANDLE rw = OpenPhysicalDrive(pd.index, GENERIC_READ | GENERIC_WRITE);
            if (rw != INVALID_HANDLE_VALUE) {
                uint8_t attrBuf[512] = {};
                if (AtaSmartRead(rw, pd.index, 0xD0, attrBuf)) {
                    uint8_t thrBuf[512] = {};
                    std::vector<HwSmartAttr> tmp;
                    ParseAtaAttributes(attrBuf, thrBuf, tmp);
                    const HwSmartAttr* ta = FindAttr(tmp, 194);
                    if (!ta) ta = FindAttr(tmp, 190);
                    if (ta) {
                        int v = static_cast<int>(ta->raw & 0xFF);
                        if (v > 0 && v < 150) { t = v; ok = true; }
                    }
                }
                CloseHandle(rw);
            }
        }
        if (!ok) continue;
        pd.tempC = t;
        pd.tempPeakC = std::max(pd.tempPeakC, t);
        if (w > 0) pd.tempWarnC = w;
        if (c > 0) pd.tempCritC = c;
        if (!pd.tempHistory.empty()) {
            pd.tempHistory.erase(pd.tempHistory.begin());
            pd.tempHistory.push_back(static_cast<float>(t));
        }
    }
}

void HardwareInspector::UpdateAcpiThermalZones() {
    if (!m_pdhThermalQuery && !m_pdhThermalTried) {
        m_pdhThermalTried = true;
        PDH_HQUERY q = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &q) == ERROR_SUCCESS) {
            PDH_HCOUNTER c = nullptr;
            if (PdhAddEnglishCounterW(q, L"\\Thermal Zone Information(*)\\Temperature", 0, &c) == ERROR_SUCCESS) {
                m_pdhThermalQuery = q;
                m_pdhThermalCounter = c;
            } else {
                PdhCloseQuery(q);
            }
        }
    }
    if (!m_pdhThermalQuery) return;

    auto query = static_cast<PDH_HQUERY>(m_pdhThermalQuery);
    auto counter = static_cast<PDH_HCOUNTER>(m_pdhThermalCounter);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) return;

    DWORD bufSize = 0, itemCount = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(counter, PDH_FMT_LONG, &bufSize, &itemCount, nullptr);
    if (st != PDH_MORE_DATA || bufSize == 0) return;
    std::vector<uint8_t> buf(bufSize);
    auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_LONG, &bufSize, &itemCount, items) != ERROR_SUCCESS) return;

    m_wState.thermalZones.clear();
    float maxC = 0.0f;
    for (DWORD i = 0; i < itemCount; ++i) {
        if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        float c = static_cast<float>(items[i].FmtValue.longValue) - 273.15f;
        if (c < 1.0f || c > 150.0f) continue;
        std::wstring nm = items[i].szName ? items[i].szName : L"";
        if (nm == L"_Total") continue;
        size_t pos = nm.find(L"_TZ.");
        if (pos != std::wstring::npos) nm = nm.substr(pos + 4);
        while (!nm.empty() && nm[0] == L'\\') nm.erase(0, 1);

        HwThermalSensor s;
        s.name = L"ACPI " + nm;
        s.tempC = c;
        s.warnC = 70.0f;
        s.critC = 90.0f;
        m_wState.thermalZones.push_back(std::move(s));
        maxC = std::max(maxC, c);
    }
    if (!m_wState.acpiTempHistory.empty() && maxC > 0.0f) {
        m_wState.acpiTempHistory.erase(m_wState.acpiTempHistory.begin());
        m_wState.acpiTempHistory.push_back(maxC);
    }
}

void HardwareInspector::OpenNetworkAdapterProperties(HWND owner, const std::wstring& connectionName) {
    bool opened = false;

    // Shell namespace "Network Connections" ({7007ACC7-3202-11D1-AAD2-00805FC1270E})
    IShellFolder* desktop = nullptr;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop)) && desktop) {
        PIDLIST_RELATIVE netPidl = nullptr;
        if (SUCCEEDED(desktop->ParseDisplayName(owner, nullptr,
                const_cast<LPWSTR>(L"::{7007ACC7-3202-11D1-AAD2-00805FC1270E}"), nullptr, &netPidl, nullptr)) && netPidl) {
            IShellFolder* netFolder = nullptr;
            if (SUCCEEDED(desktop->BindToObject(netPidl, nullptr, IID_PPV_ARGS(&netFolder))) && netFolder) {
                IEnumIDList* en = nullptr;
                if (SUCCEEDED(netFolder->EnumObjects(owner, SHCONTF_NONFOLDERS | SHCONTF_FOLDERS | SHCONTF_INCLUDEHIDDEN, &en)) && en) {
                    PITEMID_CHILD child = nullptr;
                    ULONG fetched = 0;
                    while (!opened && en->Next(1, &child, &fetched) == S_OK && child) {
                        STRRET sr = {};
                        if (SUCCEEDED(netFolder->GetDisplayNameOf(child, SHGDN_NORMAL, &sr))) {
                            wchar_t nameBuf[MAX_PATH] = {};
                            if (SUCCEEDED(StrRetToBufW(&sr, child, nameBuf, MAX_PATH)) && _wcsicmp(nameBuf, connectionName.c_str()) == 0) {
                                PCUITEMID_CHILD arr[1] = { child };
                                IContextMenu* cm = nullptr;
                                if (SUCCEEDED(netFolder->GetUIObjectOf(owner, 1, arr, IID_IContextMenu, nullptr, reinterpret_cast<void**>(&cm))) && cm) {
                                    HMENU menu = CreatePopupMenu();
                                    if (menu && SUCCEEDED(cm->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL))) {
                                        CMINVOKECOMMANDINFO ici = {};
                                        ici.cbSize = sizeof(ici);
                                        ici.hwnd = owner;
                                        ici.lpVerb = "properties";
                                        ici.nShow = SW_SHOWNORMAL;
                                        opened = SUCCEEDED(cm->InvokeCommand(&ici));
                                    }
                                    if (menu) DestroyMenu(menu);
                                    cm->Release();
                                }
                            }
                        }
                        CoTaskMemFree(child);
                        child = nullptr;
                    }
                    en->Release();
                }
                netFolder->Release();
            }
            CoTaskMemFree(netPidl);
        }
        desktop->Release();
    }

    if (!opened) {
        // Fallback: classic Network Connections window, user picks the adapter
        ShellExecuteW(owner, L"open", L"ncpa.cpl", nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void HardwareInspector::QueryNetworkAdapters(bool updateThroughputOnly) {
    ULONGLONG nowTick = GetTickCount64();
    double dtSec = (m_prevNetTick > 0 && nowTick > m_prevNetTick) ? (static_cast<double>(nowTick - m_prevNetTick) / 1000.0) : 0.0;

    if (!updateThroughputOnly || m_wState.networks.empty()) {
        m_wState.networks.clear();
        ULONG bufLen = 32768;
        std::vector<uint8_t> buf(bufLen);
        ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS;
        DWORD ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            buf.resize(bufLen);
            ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &bufLen);
        }
        if (ret == NO_ERROR) {
            for (auto aa = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); aa != nullptr; aa = aa->Next) {
                if (aa->IfType == IF_TYPE_SOFTWARE_LOOPBACK || aa->IfType == IF_TYPE_TUNNEL) continue;

                HwNetAdapter net;
                net.friendlyName = aa->FriendlyName ? aa->FriendlyName : L"";
                net.description  = aa->Description ? aa->Description : L"";
                net.isConnected  = (aa->OperStatus == IfOperStatusUp);
                net.isWireless   = (aa->IfType == IF_TYPE_IEEE80211);
                net.txLinkSpeedBps = (aa->TransmitLinkSpeed != UINT64_MAX) ? aa->TransmitLinkSpeed : 0;
                net.rxLinkSpeedBps = (aa->ReceiveLinkSpeed != UINT64_MAX) ? aa->ReceiveLinkSpeed : 0;

                if (aa->PhysicalAddressLength > 0) {
                    wchar_t macBuf[64] = {};
                    std::wstring mac;
                    for (ULONG i = 0; i < aa->PhysicalAddressLength; ++i) {
                        swprintf_s(macBuf, L"%s%02X", (i > 0 ? L"-" : L""), aa->PhysicalAddress[i]);
                        mac += macBuf;
                    }
                    net.macAddress = mac;
                }

                for (auto ua = aa->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
                    if (!ua->Address.lpSockaddr) continue;
                    wchar_t ipBuf[128] = {};
                    if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                        auto sin = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
                        if (InetNtopW(AF_INET, &sin->sin_addr, ipBuf, 128) && net.ipv4.empty()) {
                            net.ipv4 = ipBuf;
                        }
                    } else if (ua->Address.lpSockaddr->sa_family == AF_INET6) {
                        auto sin6 = reinterpret_cast<sockaddr_in6*>(ua->Address.lpSockaddr);
                        if (InetNtopW(AF_INET6, &sin6->sin6_addr, ipBuf, 128) && net.ipv6.empty()) {
                            net.ipv6 = ipBuf;
                        }
                    }
                }

                for (auto gw = aa->FirstGatewayAddress; gw != nullptr; gw = gw->Next) {
                    if (gw->Address.lpSockaddr && gw->Address.lpSockaddr->sa_family == AF_INET) {
                        wchar_t gwBuf[128] = {};
                        auto sin = reinterpret_cast<sockaddr_in*>(gw->Address.lpSockaddr);
                        if (InetNtopW(AF_INET, &sin->sin_addr, gwBuf, 128)) {
                            net.gateway = gwBuf;
                            break;
                        }
                    }
                }

                for (auto dns = aa->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
                    if (dns->Address.lpSockaddr && dns->Address.lpSockaddr->sa_family == AF_INET) {
                        wchar_t dnsBuf[128] = {};
                        auto sin = reinterpret_cast<sockaddr_in*>(dns->Address.lpSockaddr);
                        if (InetNtopW(AF_INET, &sin->sin_addr, dnsBuf, 128)) {
                            if (!net.dnsServers.empty()) net.dnsServers += L", ";
                            net.dnsServers += dnsBuf;
                        }
                    }
                }

                m_wState.networks.push_back(net);
            }
        }
    }

    // Update RX/TX byte counters & live bandwidth using GetIfTable2
    PMIB_IF_TABLE2 ifTable = nullptr;
    if (GetIfTable2(&ifTable) == NO_ERROR && ifTable) {
        uint64_t sumRx = 0;
        uint64_t sumTx = 0;
        for (ULONG i = 0; i < ifTable->NumEntries; ++i) {
            const MIB_IF_ROW2& row = ifTable->Table[i];
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) continue;
            if (!row.InterfaceAndOperStatusFlags.FilterInterface && row.OperStatus == IfOperStatusUp) {
                sumRx += row.InOctets;
                sumTx += row.OutOctets;
            }
            for (auto& net : m_wState.networks) {
                if (!net.description.empty() && _wcsicmp(net.description.c_str(), row.Description) == 0) {
                    if (dtSec > 0.05 && net.totalRxBytes > 0 && row.InOctets >= net.totalRxBytes) {
                        net.liveRxBytesPerSec = static_cast<double>(row.InOctets - net.totalRxBytes) / dtSec;
                        net.liveTxBytesPerSec = static_cast<double>(row.OutOctets - net.totalTxBytes) / dtSec;
                    }
                    net.totalRxBytes = row.InOctets;
                    net.totalTxBytes = row.OutOctets;
                    break;
                }
            }
        }
        FreeMibTable(ifTable);

        if (dtSec > 0.05 && m_prevTotalRx > 0 && sumRx >= m_prevTotalRx) {
            float rxKBps = static_cast<float>((sumRx - m_prevTotalRx) / dtSec / 1024.0);
            float txKBps = static_cast<float>((sumTx - m_prevTotalTx) / dtSec / 1024.0);
            if (!m_wState.netRxHistoryKBps.empty()) {
                m_wState.netRxHistoryKBps.erase(m_wState.netRxHistoryKBps.begin());
                m_wState.netRxHistoryKBps.push_back(rxKBps);
            }
            if (!m_wState.netTxHistoryKBps.empty()) {
                m_wState.netTxHistoryKBps.erase(m_wState.netTxHistoryKBps.begin());
                m_wState.netTxHistoryKBps.push_back(txKBps);
            }
        }
        m_prevTotalRx = sumRx;
        m_prevTotalTx = sumTx;
        m_prevNetTick = nowTick;
    }
}

void HardwareInspector::QuerySystemAndAudio(bool fullRefresh) {
    if (fullRefresh) {
        m_wState.audioOutputs.clear();
        UINT numOut = waveOutGetNumDevs();
        for (UINT i = 0; i < numOut; ++i) {
            WAVEOUTCAPSW woc = {};
            if (waveOutGetDevCapsW(i, &woc, sizeof(woc)) == MMSYSERR_NOERROR) {
                m_wState.audioOutputs.push_back(woc.szPname);
            }
        }

        m_wState.audioInputs.clear();
        UINT numIn = waveInGetNumDevs();
        for (UINT i = 0; i < numIn; ++i) {
            WAVEINCAPSW wic = {};
            if (waveInGetDevCapsW(i, &wic, sizeof(wic)) == MMSYSERR_NOERROR) {
                m_wState.audioInputs.push_back(wic.szPname);
            }
        }
    }

    // Top Processes snapshot (throttled to every 2.0 seconds, runs on background worker thread)
    ULONGLONG now = GetTickCount64();
    if (fullRefresh || now - m_lastProcessSnapTick >= 2000) {
        m_lastProcessSnapTick = now;
        m_wState.topProcesses.clear();
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4) continue;
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        PROCESS_MEMORY_COUNTERS_EX pmc = {};
                        pmc.cb = sizeof(pmc);
                        if (GetProcessMemoryInfo(hProc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
                            HwProcessEntry entry;
                            entry.pid = pe.th32ProcessID;
                            entry.name = pe.szExeFile;
                            entry.workingSetBytes = pmc.WorkingSetSize;
                            entry.privateBytes = pmc.PrivateUsage;
                            entry.threads = pe.cntThreads;
                            m_wState.topProcesses.push_back(entry);
                        }
                        CloseHandle(hProc);
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);

            std::sort(m_wState.topProcesses.begin(), m_wState.topProcesses.end(),
                      [](const HwProcessEntry& a, const HwProcessEntry& b) {
                          return a.workingSetBytes > b.workingSetBytes;
                      });
            if (m_wState.topProcesses.size() > 10) {
                m_wState.topProcesses.resize(10);
            }
        }
    }
}

void HardwareInspector::UpdateCpuLivePerCoreLoad() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return;
    auto fnQuery = reinterpret_cast<PFN_NtQuerySystemInformation>(GetProcAddress(hNtdll, "NtQuerySystemInformation"));
    if (!fnQuery) return;

    uint32_t count = std::max(1u, m_cpu.logicalThreads);
    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION_NATIVE> sppi(count);
    ULONG retLen = 0;
    // SystemProcessorPerformanceInformation = 8
    if (fnQuery(8, sppi.data(), static_cast<ULONG>(sppi.size() * sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION_NATIVE)), &retLen) != 0) {
        return;
    }

    if (m_prevCoreTimes.size() != count) {
        m_prevCoreTimes.resize(count);
        m_cpu.coreLoadPct.assign(count, 0.0f);
        for (uint32_t i = 0; i < count; ++i) {
            m_prevCoreTimes[i].idle   = sppi[i].IdleTime.QuadPart;
            m_prevCoreTimes[i].kernel = sppi[i].KernelTime.QuadPart;
            m_prevCoreTimes[i].user   = sppi[i].UserTime.QuadPart;
        }
        return;
    }

    double sumLoad = 0.0;
    double sumKernel = 0.0;
    for (uint32_t i = 0; i < count; ++i) {
        int64_t dIdle   = sppi[i].IdleTime.QuadPart   - m_prevCoreTimes[i].idle;
        int64_t dKernel = sppi[i].KernelTime.QuadPart - m_prevCoreTimes[i].kernel;
        int64_t dUser   = sppi[i].UserTime.QuadPart   - m_prevCoreTimes[i].user;

        m_prevCoreTimes[i].idle   = sppi[i].IdleTime.QuadPart;
        m_prevCoreTimes[i].kernel = sppi[i].KernelTime.QuadPart;
        m_prevCoreTimes[i].user   = sppi[i].UserTime.QuadPart;

        int64_t dTotal = dKernel + dUser; // Note: KernelTime includes IdleTime on Windows NT
        if (dTotal > 0) {
            int64_t dBusy = std::max<int64_t>(0, dTotal - dIdle);
            int64_t dKernBusy = std::max<int64_t>(0, dKernel - dIdle);
            float pct = std::clamp(static_cast<float>(static_cast<double>(dBusy) * 100.0 / static_cast<double>(dTotal)), 0.0f, 100.0f);
            float kpct = std::clamp(static_cast<float>(static_cast<double>(dKernBusy) * 100.0 / static_cast<double>(dTotal)), 0.0f, 100.0f);
            m_cpu.coreLoadPct[i] = pct;
            sumLoad += pct;
            sumKernel += kpct;
        }
    }

    m_cpu.totalLoadPct  = static_cast<float>(sumLoad / count);
    m_cpu.kernelLoadPct = static_cast<float>(sumKernel / count);
    if (!m_cpu.loadHistory.empty()) {
        m_cpu.loadHistory.erase(m_cpu.loadHistory.begin());
        m_cpu.loadHistory.push_back(m_cpu.totalLoadPct);
    }
}

void HardwareInspector::UpdateGpuLiveTelemetry() {
    // 1. Live VRAM usage for all GPUs via DXGI 1.4 IDXGIAdapter3::QueryVideoMemoryInfo
    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pFactory))) && pFactory) {
        UINT idx = 0;
        IDXGIAdapter1* pAdapter = nullptr;
        while (pFactory->EnumAdapters1(idx, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
                IDXGIAdapter3* pAdapter3 = nullptr;
                if (SUCCEEDED(pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&pAdapter3))) && pAdapter3) {
                    DXGI_QUERY_VIDEO_MEMORY_INFO localInfo = {};
                    DXGI_QUERY_VIDEO_MEMORY_INFO sharedInfo = {};
                    pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo);
                    pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &sharedInfo);
                    for (auto& g : m_wState.gpus) {
                        if (g.deviceId == desc.DeviceId && g.vendorId == desc.VendorId) {
                            g.liveVramUsedBytes   = localInfo.CurrentUsage;
                            g.liveSharedUsedBytes = sharedInfo.CurrentUsage;
                            break;
                        }
                    }
                    pAdapter3->Release();
                }
            }
            pAdapter->Release();
            ++idx;
        }
        pFactory->Release();
    }

    // 2. If NVML is available, query NVIDIA GPU sensors (Core %, Temp °C, Clocks MHz, Power W, Fan %)
    float maxGpuPct = 0.0f;
    if (m_nvmlReady && m_hNvml) {
        auto fnGetH   = reinterpret_cast<PFN_nvmlDeviceGetHandleByIndex_v2>(GetProcAddress(m_hNvml, "nvmlDeviceGetHandleByIndex_v2"));
        auto fnUtil   = reinterpret_cast<PFN_nvmlDeviceGetUtilizationRates>(GetProcAddress(m_hNvml, "nvmlDeviceGetUtilizationRates"));
        auto fnTemp   = reinterpret_cast<PFN_nvmlDeviceGetTemperature>(GetProcAddress(m_hNvml, "nvmlDeviceGetTemperature"));
        auto fnFan    = reinterpret_cast<PFN_nvmlDeviceGetFanSpeed>(GetProcAddress(m_hNvml, "nvmlDeviceGetFanSpeed"));
        auto fnClk    = reinterpret_cast<PFN_nvmlDeviceGetClockInfo>(GetProcAddress(m_hNvml, "nvmlDeviceGetClockInfo"));
        auto fnPwr    = reinterpret_cast<PFN_nvmlDeviceGetPowerUsage>(GetProcAddress(m_hNvml, "nvmlDeviceGetPowerUsage"));
        auto fnPwrLim = reinterpret_cast<PFN_nvmlDeviceGetPowerManagementLimit>(GetProcAddress(m_hNvml, "nvmlDeviceGetPowerManagementLimit"));
        auto fnMem    = reinterpret_cast<PFN_nvmlDeviceGetMemoryInfo>(GetProcAddress(m_hNvml, "nvmlDeviceGetMemoryInfo"));
        auto fnDrv    = reinterpret_cast<PFN_nvmlSystemGetDriverVersion>(GetProcAddress(m_hNvml, "nvmlSystemGetDriverVersion"));

        char drvVer[80] = {};
        std::wstring nvDrvStr;
        if (fnDrv && fnDrv(drvVer, sizeof(drvVer)) == 0) {
            nvDrvStr = Utf8OrAsciiToWide(drvVer);
        }

        unsigned int nvIdx = 0;
        for (auto& g : m_wState.gpus) {
            if (g.vendorId != 0x10DE) continue;
            nvmlDevice_t dev = nullptr;
            if (fnGetH && fnGetH(nvIdx, &dev) == 0 && dev) {
                g.hasNvmlTelemetry = true;
                if (!nvDrvStr.empty()) g.driverVersion = nvDrvStr;

                nvmlUtilization_t util = {};
                if (fnUtil && fnUtil(dev, &util) == 0) {
                    g.gpuLoadPct = util.gpu;
                    g.memControllerLoadPct = util.memory;
                    maxGpuPct = std::max(maxGpuPct, static_cast<float>(util.gpu));
                }
                unsigned int temp = 0;
                if (fnTemp && fnTemp(dev, 0, &temp) == 0) g.tempCelsius = temp;
                unsigned int fan = 0;
                if (fnFan && fnFan(dev, &fan) == 0) g.fanSpeedPct = fan;
                unsigned int clkGfx = 0, clkMem = 0;
                if (fnClk && fnClk(dev, 0, &clkGfx) == 0) g.coreClockMHz = clkGfx;
                if (fnClk && fnClk(dev, 2, &clkMem) == 0) g.memClockMHz = clkMem;
                unsigned int pwrMw = 0, pwrLimMw = 0;
                if (fnPwr && fnPwr(dev, &pwrMw) == 0) g.powerDrawWatts = pwrMw / 1000.0f;
                if (fnPwrLim && fnPwrLim(dev, &pwrLimMw) == 0) g.powerLimitWatts = pwrLimMw / 1000.0f;
                nvmlMemory_t mem = {};
                if (fnMem && fnMem(dev, &mem) == 0 && mem.used > 0) {
                    g.liveVramUsedBytes = mem.used;
                }
                ++nvIdx;
            }
        }
    }

    if (!m_wState.gpuLoadHistory.empty()) {
        m_wState.gpuLoadHistory.erase(m_wState.gpuLoadHistory.begin());
        m_wState.gpuLoadHistory.push_back(maxGpuPct);
    }

    float maxGpuTemp = 0.0f;
    for (const auto& g : m_wState.gpus) {
        if (g.hasNvmlTelemetry) maxGpuTemp = std::max(maxGpuTemp, static_cast<float>(g.tempCelsius));
    }
    if (!m_wState.gpuTempHistory.empty() && maxGpuTemp > 0.0f) {
        m_wState.gpuTempHistory.erase(m_wState.gpuTempHistory.begin());
        m_wState.gpuTempHistory.push_back(maxGpuTemp);
    }
}

void HardwareInspector::UpdateLiveTelemetry(bool syncForCli) {
    if (!m_initialized) InitializeStaticInventory(syncForCli);

    ApplyStagedData();
    UpdateCpuLivePerCoreLoad();
    QueryFastSystemAndRam(false);

    // Offload heavier live sensors (GPU DXGI/NVML, NIC GetIfTable2, TopProcesses, Disk & ACPI Temperatures) to background worker
    if (!m_workerBusy.load(std::memory_order_relaxed) || syncForCli) {
        StartWorkerThreadIfNeeded();
        m_reqLiveUpdate.store(true, std::memory_order_release);
        m_workerBusy.store(true, std::memory_order_release);
        m_workerCv.notify_one();
    }
    if (syncForCli) {
        WaitForAsync();
        ApplyStagedData();
    }
}

void HardwareInspector::RunCpuBenchmark() {
    if (!m_initialized) InitializeStaticInventory();

    constexpr uint64_t kItersPerThread = 25000000ULL;
    auto workerKernel = [](uint64_t seed) -> uint64_t {
        uint64_t x = 0x9E3779B97F4A7C15ULL ^ seed;
        double d1 = 1.00000011920928955078, d2 = 0.99999988079071044922;
        for (uint64_t i = 0; i < kItersPerThread; ++i) {
            x ^= x >> 12;
            x ^= x << 25;
            x ^= x >> 27;
            x *= 0x2545F4914F6CDD1DULL;
            d1 = d1 * d2 + 0.0000001;
        }
        return x ^ static_cast<uint64_t>(d1 * 1000.0);
    };

    // 1. Single-Core Test
    auto t0 = std::chrono::high_resolution_clock::now();
    volatile uint64_t sink1 = workerKernel(1ULL);
    (void)sink1;
    auto t1 = std::chrono::high_resolution_clock::now();
    double singleSec = std::max(0.0001, std::chrono::duration<double>(t1 - t0).count());
    double opsPerIter = 8.0;
    m_cpu.benchSingleCoreGops = (static_cast<double>(kItersPerThread) * opsPerIter / singleSec) / 1e9;

    // 2. Multi-Core Test across all logical threads
    uint32_t numThreads = std::max(1u, m_cpu.logicalThreads);
    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    auto t2 = std::chrono::high_resolution_clock::now();
    for (uint32_t t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            volatile uint64_t s = workerKernel(t + 100ULL);
            (void)s;
        });
    }
    for (auto& th : threads) th.join();
    auto t3 = std::chrono::high_resolution_clock::now();
    double multiSec = std::max(0.0001, std::chrono::duration<double>(t3 - t2).count());

    m_cpu.benchMultiCoreGops = (static_cast<double>(kItersPerThread) * numThreads * opsPerIter / multiSec) / 1e9;
    m_cpu.benchMpRatio = (m_cpu.benchSingleCoreGops > 0.0) ? (m_cpu.benchMultiCoreGops / m_cpu.benchSingleCoreGops) : 1.0;
    m_cpu.benchTimeMs = (singleSec + multiSec) * 1000.0;
}

void HardwareInspector::RunRamBenchmark() {
    if (!m_initialized) InitializeStaticInventory();

    constexpr size_t kBufSize = 128ULL * 1024ULL * 1024ULL; // 128 MB buffer
    std::vector<uint64_t> bufA(kBufSize / sizeof(uint64_t), 0x123456789ABCDEF0ULL);
    std::vector<uint64_t> bufB(kBufSize / sizeof(uint64_t), 0ULL);

    // 1. Sequential Write
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int pass = 0; pass < 4; ++pass) {
        std::fill(bufB.begin(), bufB.end(), static_cast<uint64_t>(pass + 1));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double writeSec = std::max(0.0001, std::chrono::duration<double>(t1 - t0).count());
    m_ram.benchWriteGBps = (4.0 * static_cast<double>(kBufSize) / writeSec) / (1024.0 * 1024.0 * 1024.0);

    // 2. Sequential Copy
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int pass = 0; pass < 4; ++pass) {
        std::memcpy(bufB.data(), bufA.data(), kBufSize);
        bufA[0] += pass;
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double copySec = std::max(0.0001, std::chrono::duration<double>(t3 - t2).count());
    m_ram.benchCopyGBps = (4.0 * static_cast<double>(kBufSize) / copySec) / (1024.0 * 1024.0 * 1024.0);

    // 3. Sequential Read
    uint64_t acc = 0;
    auto t4 = std::chrono::high_resolution_clock::now();
    for (int pass = 0; pass < 4; ++pass) {
        const uint64_t* p = bufB.data();
        size_t n = bufB.size();
        for (size_t i = 0; i < n; i += 4) {
            acc += p[i] + p[i + 1] + p[i + 2] + p[i + 3];
        }
    }
    volatile uint64_t sink = acc;
    (void)sink;
    auto t5 = std::chrono::high_resolution_clock::now();
    double readSec = std::max(0.0001, std::chrono::duration<double>(t5 - t4).count());
    m_ram.benchReadGBps = (4.0 * static_cast<double>(kBufSize) / readSec) / (1024.0 * 1024.0 * 1024.0);
}

std::wstring HardwareInspector::BuildDiagnosticsDump() const {
    std::wostringstream ss;
    ss << L"[CPU] total=" << m_cpu.totalLoadPct << L"% kernel=" << m_cpu.kernelLoadPct << L"%  threads=" << m_cpu.logicalThreads << L"\r\n";
    for (size_t i = 0; i < m_cpu.coreLoadPct.size(); ++i) ss << L"  core" << i << L"=" << m_cpu.coreLoadPct[i] << L"%";
    ss << L"\r\n\r\n[ACPI THERMAL ZONES]\r\n";
    for (const auto& z : m_thermalZones) ss << L"  " << z.name << L" = " << z.tempC << L" C\r\n";
    if (m_thermalZones.empty()) ss << L"  (none)\r\n";
    ss << L"\r\n[DISKS]\r\n";
    for (const auto& d : m_physDisks) {
        ss << L"  #" << d.index << L" " << d.model << L"  bus=" << d.busType << L" nvme=" << d.isNvme << L" rotational=" << d.isRotational
           << L" temp=" << d.tempC << L" smartState=" << static_cast<int>(d.smartState) << L" healthLevel=" << d.healthLevel
           << L" life=" << d.lifeRemainingPct << L" poh=" << d.powerOnHours << L"\r\n";
        for (const auto& a : d.smartAttrs) {
            ss << L"      [" << a.id << L"] " << a.name << L"  cur=" << a.current << L" worst=" << a.worst << L" thr=" << a.threshold
               << L"  raw=" << a.rawText << L"  status=" << a.status << L"\r\n";
        }
    }
    return ss.str();
}

std::wstring HardwareInspector::BuildFullTextReport(Lang lang) const {
    std::wostringstream ss;
    ss << L"====================================================================\r\n";
    ss << (lang == Lang::PL ? L"   RAPORT SPRZĘTOWY PRO  —  EXPLORER2 HARDWARE WORKSTATION\r\n"
                            : L"   PRO HARDWARE REPORT  —  EXPLORER2 HARDWARE WORKSTATION\r\n");
    ss << L"====================================================================\r\n\r\n";

    ss << L"[SYSTEM & OS]\r\n";
    ss << L"  OS:            " << m_system.osName << L" (" << m_system.osBuildStr << L")\r\n";
    ss << L"  Computer / PC: " << m_system.computerName << L"  (User: " << m_system.userName << L")\r\n";
    ss << L"  Uptime:        " << m_system.uptimeStr << L"\r\n";
    ss << L"  Power:         " << m_system.powerStatusStr << L"\r\n\r\n";

    ss << L"[PROCESOR (CPU)]\r\n";
    ss << L"  Model:         " << m_cpu.brand << L"\r\n";
    ss << L"  Vendor:        " << m_cpu.vendor << L"  |  Arch: " << m_cpu.architecture << L"\r\n";
    ss << L"  Cores/Threads: " << m_cpu.physicalCores << L" Physical Cores / " << m_cpu.logicalThreads << L" Logical Threads\r\n";
    ss << L"  Clocks:        Base " << m_cpu.baseFreqMHz << L" MHz  |  Max Turbo " << m_cpu.maxFreqMHz << L" MHz\r\n";
    ss << L"  Cache:         L1D " << m_cpu.l1DataKB << L" KB | L1I " << m_cpu.l1InstKB << L" KB | L2 "
       << (m_cpu.l2KB / 1024) << L" MB | L3 " << (m_cpu.l3KB / 1024) << L" MB\r\n\r\n";

    ss << L"[PŁYTA GŁÓWNA I BIOS (MOTHERBOARD & SMBIOS)]\r\n";
    ss << L"  Motherboard:   " << m_mobo.boardManufacturer << L" " << m_mobo.boardProduct << L" (" << m_mobo.boardVersion << L")\r\n";
    ss << L"  BIOS / UEFI:   " << m_mobo.biosVendor << L"  Ver: " << m_mobo.biosVersion << L"  Date: " << m_mobo.biosReleaseDate << L"\r\n\r\n";

    ss << L"[PAMIĘĆ RAM (MEMORY & DIMM SLOTS)]\r\n";
    ss << L"  Total RAM:     " << FileEngine::FormatSizeDetailed(m_ram.totalBytes)
       << L"  (Used: " << FileEngine::FormatSizeDetailed(m_ram.usedBytes)
       << L" / Free: " << FileEngine::FormatSizeDetailed(m_ram.availBytes) << L")\r\n";
    for (size_t i = 0; i < m_ram.dimms.size(); ++i) {
        const auto& d = m_ram.dimms[i];
        ss << L"  Slot [" << d.slotLocator << L"]: " << FileEngine::FormatSizeDetailed(d.sizeBytes)
           << L" " << d.memoryType << L" @ " << d.configuredSpeedMHz << L" MT/s  |  "
           << d.manufacturer << L" " << d.partNumber << L"\r\n";
    }
    ss << L"\r\n";

    ss << L"[KARTY GRAFICZNE (GPUs & DISPLAYS)]\r\n";
    for (const auto& g : m_gpus) {
        ss << L"  GPU:           " << g.name << L"  |  VRAM: " << FileEngine::FormatSizeDetailed(g.dedicatedVramBytes) << L"\r\n";
        if (g.hasNvmlTelemetry) {
            ss << L"                 Temp: " << g.tempCelsius << L" C  |  Core: " << g.coreClockMHz
               << L" MHz  |  Power: " << static_cast<int>(g.powerDrawWatts) << L" W\r\n";
        }
    }
    for (const auto& disp : m_displays) {
        ss << L"  Monitor:       " << disp.monitorName << L"  (" << disp.width << L"x" << disp.height
           << L" @ " << disp.refreshHz << L" Hz)\r\n";
    }
    ss << L"\r\n";

    ss << L"[DYSKI FIZYCZNE I WOLUMINY (STORAGE)]\r\n";
    for (const auto& pd : m_physDisks) {
        ss << L"  Disk #" << pd.index << L":       " << pd.model << L"  [" << pd.busType << L" / " << pd.mediaType
           << L"]  " << FileEngine::FormatSizeDetailed(pd.capacityBytes)
           << L"  (FW: " << pd.firmwareRev << L", TRIM: " << (pd.supportsTrim ? L"Yes" : L"No") << L")\r\n";
        if (pd.tempC >= 0) ss << L"                 Temp: " << pd.tempC << L" C (peak " << pd.tempPeakC << L" C)\r\n";
        if (pd.smartState == HwSmartState::Ok) {
            ss << L"                 SMART: " << (pd.healthLevel == 2 ? L"FAILING" : (pd.healthLevel == 1 ? L"WARNING" : L"OK"));
            if (pd.powerOnHours >= 0) ss << L"  |  Power-On: " << pd.powerOnHours << L" h";
            if (pd.lifeRemainingPct >= 0) ss << L"  |  Life left: " << pd.lifeRemainingPct << L"%";
            ss << L"\r\n";
        }
    }
    for (const auto& v : m_volumes) {
        ss << L"  Volume " << v.rootPath << L" (" << v.label << L") [" << v.fileSystem << L"]: "
           << FileEngine::FormatSizeDetailed(v.usedBytes) << L" / " << FileEngine::FormatSizeDetailed(v.totalBytes)
           << L"  (Free: " << FileEngine::FormatSizeDetailed(v.freeBytes) << L")\r\n";
    }
    ss << L"\r\n";

    ss << L"[SIEĆ (NETWORK INTERFACES)]\r\n";
    for (const auto& net : m_networks) {
        ss << L"  [" << net.friendlyName << L"] " << net.description << L"\r\n";
        ss << L"     IPv4: " << net.ipv4 << L"  |  MAC: " << net.macAddress
           << L"  |  RX: " << FileEngine::FormatSizeDetailed(net.totalRxBytes)
           << L"  |  TX: " << FileEngine::FormatSizeDetailed(net.totalTxBytes) << L"\r\n";
    }

    return ss.str();
}

} // namespace Explorer2
