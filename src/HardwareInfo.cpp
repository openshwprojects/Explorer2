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

} // namespace

HardwareInspector::HardwareInspector() {
    m_cpu.loadHistory.assign(60, 0.0f);
    m_ram.loadHistory.assign(60, 0.0f);
    m_gpuLoadHistory.assign(60, 0.0f);
    m_netRxHistoryKBps.assign(60, 0.0f);
    m_netTxHistoryKBps.assign(60, 0.0f);
}

HardwareInspector::~HardwareInspector() {
    if (m_hNvml) {
        FreeLibrary(m_hNvml);
        m_hNvml = nullptr;
    }
}

void HardwareInspector::InitializeStaticInventory() {
    if (m_initialized) return;
    m_initialized = true;

    // Try loading NVML for NVIDIA GPUs
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

    RefreshFullInventory();
}

void HardwareInspector::RefreshFullInventory() {
    QueryCpuStatic();
    QueryMotherboardAndSmbios();
    QueryGpusAndDisplays();
    QueryPhysicalDisksAndVolumes();
    QueryNetworkAdapters(false);
    QuerySystemAndAudio(true);
    UpdateLiveTelemetry();
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
    m_gpus.clear();
    m_displays.clear();

    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pFactory))) && pFactory) {
        UINT idx = 0;
        IDXGIAdapter1* pAdapter = nullptr;
        while (pFactory->EnumAdapters1(idx, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
                // Skip software Microsoft Basic Render Driver unless it's the only adapter
                bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || desc.VendorId == 0x1414;
                if (!isSoftware || m_gpus.empty()) {
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

                    m_gpus.push_back(gpu);
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
                m_displays.push_back(disp);
            }
        }
        dd.cb = sizeof(dd);
    }
}

void HardwareInspector::QueryPhysicalDisksAndVolumes() {
    m_physDisks.clear();
    m_volumes.clear();

    // 1. Query Physical Drives (\\.\PhysicalDrive0..15) via zero-access handle + IOCTL_STORAGE_QUERY_PROPERTY
    for (int d = 0; d < 16; ++d) {
        wchar_t devPath[64] = {};
        swprintf_s(devPath, L"\\\\.\\PhysicalDrive%d", d);
        HANDLE hDisk = CreateFileW(devPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hDisk == INVALID_HANDLE_VALUE) continue;

        HwPhysicalDisk pd;
        pd.index = d;

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
            case BusTypeNvme:   pd.busType = L"NVMe PCIe"; pd.mediaType = L"NVMe SSD"; break;
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
            if (dtype != DRIVE_REMOTE && GetVolumeInformationW(root.c_str(), vName, MAX_PATH, &serial, &maxComp, &fsFlags, fsName, MAX_PATH)) {
                vol.label = vName;
                vol.fileSystem = fsName;
                vol.serialNumber = serial;
                vol.isCompressed = (fsFlags & FILE_VOLUME_IS_COMPRESSED) != 0;
                vol.isEncrypted  = (fsFlags & FILE_SUPPORTS_ENCRYPTION) != 0;
            } else if (dtype == DRIVE_REMOTE) {
                vol.fileSystem = L"SMB / CIFS";
            }
        } else {
            vol.isReady = false;
        }
        m_volumes.push_back(vol);
    }
}

void HardwareInspector::QueryNetworkAdapters(bool updateThroughputOnly) {
    ULONGLONG nowTick = GetTickCount64();
    double dtSec = (m_prevNetTick > 0 && nowTick > m_prevNetTick) ? (static_cast<double>(nowTick - m_prevNetTick) / 1000.0) : 0.0;

    if (!updateThroughputOnly || m_networks.empty()) {
        m_networks.clear();
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

                m_networks.push_back(net);
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
            for (auto& net : m_networks) {
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
            if (!m_netRxHistoryKBps.empty()) {
                m_netRxHistoryKBps.erase(m_netRxHistoryKBps.begin());
                m_netRxHistoryKBps.push_back(rxKBps);
            }
            if (!m_netTxHistoryKBps.empty()) {
                m_netTxHistoryKBps.erase(m_netTxHistoryKBps.begin());
                m_netTxHistoryKBps.push_back(txKBps);
            }
        }
        m_prevTotalRx = sumRx;
        m_prevTotalTx = sumTx;
        m_prevNetTick = nowTick;
    }
}

void HardwareInspector::QuerySystemAndAudio(bool fullRefresh) {
    if (fullRefresh) {
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
                        // Windows 11 stores "Windows 10" in ProductName for compatibility; fix display name accurately!
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

        m_system.audioOutputs.clear();
        UINT numOut = waveOutGetNumDevs();
        for (UINT i = 0; i < numOut; ++i) {
            WAVEOUTCAPSW woc = {};
            if (waveOutGetDevCapsW(i, &woc, sizeof(woc)) == MMSYSERR_NOERROR) {
                m_system.audioOutputs.push_back(woc.szPname);
            }
        }

        m_system.audioInputs.clear();
        UINT numIn = waveInGetNumDevs();
        for (UINT i = 0; i < numIn; ++i) {
            WAVEINCAPSW wic = {};
            if (waveInGetDevCapsW(i, &wic, sizeof(wic)) == MMSYSERR_NOERROR) {
                m_system.audioInputs.push_back(wic.szPname);
            }
        }
    }

    // Uptime
    m_system.uptimeSeconds = GetTickCount64() / 1000ULL;
    uint64_t d = m_system.uptimeSeconds / 86400;
    uint64_t h = (m_system.uptimeSeconds % 86400) / 3600;
    uint64_t m = (m_system.uptimeSeconds % 3600) / 60;
    uint64_t s = m_system.uptimeSeconds % 60;
    wchar_t upBuf[128] = {};
    swprintf_s(upBuf, L"%llud %02lluh %02llum %02llus", d, h, m, s);
    m_system.uptimeStr = upBuf;

    // Power status
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

    // Top Processes snapshot (throttled to every 2.0 seconds so live 2Hz loop stays ultra-light)
    ULONGLONG now = GetTickCount64();
    if (fullRefresh || now - m_lastProcessSnapTick >= 2000) {
        m_lastProcessSnapTick = now;
        m_system.topProcesses.clear();
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
                            m_system.topProcesses.push_back(entry);
                        }
                        CloseHandle(hProc);
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);

            std::sort(m_system.topProcesses.begin(), m_system.topProcesses.end(),
                      [](const HwProcessEntry& a, const HwProcessEntry& b) {
                          return a.workingSetBytes > b.workingSetBytes;
                      });
            if (m_system.topProcesses.size() > 10) {
                m_system.topProcesses.resize(10);
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
                    for (auto& g : m_gpus) {
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
        for (auto& g : m_gpus) {
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

    if (!m_gpuLoadHistory.empty()) {
        m_gpuLoadHistory.erase(m_gpuLoadHistory.begin());
        m_gpuLoadHistory.push_back(maxGpuPct);
    }
}

void HardwareInspector::UpdateLiveTelemetry() {
    if (!m_initialized) InitializeStaticInventory();

    UpdateCpuLivePerCoreLoad();
    UpdateGpuLiveTelemetry();

    // Update Live RAM & Kernel Performance Counters
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

    QueryNetworkAdapters(true);
    QuerySystemAndAudio(false);
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
