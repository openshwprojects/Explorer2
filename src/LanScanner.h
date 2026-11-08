#pragma once
#include "Types.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// LAN scanner backend (UI-agnostic): ARP/ICMP host discovery, reverse DNS + NetBIOS names,
// MAC vendor lookup, SSDP/UPnP discovery and a small TCP port scan.
// Everything is asynchronous: Start*() returns immediately, results are polled through the
// thread-safe Snapshot*() / Is*() / GetScanProgress() accessors. No admin rights required.

namespace Explorer2 {

struct LanAdapterInfo {
    std::wstring name;          // friendly adapter name ("Ethernet", "Wi-Fi", ...)
    std::wstring ip;            // dotted IPv4 of this PC on the adapter
    std::wstring mask;          // dotted subnet mask
    std::wstring gateway;       // dotted default gateway (empty if none)
    uint32_t ipHostOrder = 0;
    uint32_t maskHostOrder = 0;
    int prefix = 24;
};

struct LanHost {
    std::wstring ip;
    uint32_t ipHostOrder = 0;
    std::wstring mac;              // "AA-BB-CC-DD-EE-FF" or empty
    std::wstring vendor;           // from OUI table, empty if unknown
    bool randomizedMac = false;    // locally-administered bit set (phones' private MAC)
    std::wstring hostName;         // reverse DNS (empty if none)
    std::wstring netbiosName;      // NBSTAT answer (empty if none)
    int rttMs = -1;                // ICMP echo RTT, -1 = no reply
    bool viaArp = false, viaPing = false, viaSsdp = false, isSelf = false, isGateway = false;
    std::vector<uint16_t> openPorts;
    bool portsScanned = false;
};

struct SsdpDevice {
    std::wstring ip;
    std::wstring location;
    std::wstring server;
    std::wstring st;
    std::wstring usn;
    // Filled from the UPnP device description XML (LOCATION) once it has been fetched:
    std::wstring friendlyName, manufacturer, modelName, modelDescription, deviceType;
};

class LanScanner {
public:
    LanScanner();
    ~LanScanner();   // cancels everything and joins every worker thread (may take up to ~2 s)
    LanScanner(const LanScanner&) = delete;
    LanScanner& operator=(const LanScanner&) = delete;

    // Up, IPv4, non-loopback, non-tunnel adapters (169.254.x.x skipped); adapters with a gateway first.
    static std::vector<LanAdapterInfo> EnumerateAdapters();

    // Async host discovery of the adapter's subnet; returns immediately and clears previous host results.
    // A running scan (if any) is cancelled and superseded.
    void StartScan(const LanAdapterInfo& adapter, int maxHosts = 1024);
    void CancelScan();
    bool IsScanning() const;
    void GetScanProgress(int& done, int& total) const;   // addresses processed / total addresses

    // SSDP / UPnP discovery (clears previous SSDP results). Binds to the adapter of the last StartScan
    // (or INADDR_ANY if StartScan was never called).
    void StartSsdp(int listenMs = 4000);
    bool IsSsdpRunning() const;

    // TCP connect scan of CommonPorts() against one host that is already in the host list
    // (results are ignored if the IP is not in the list). Sets openPorts + portsScanned.
    void StartPortScan(const std::wstring& ip);
    bool IsPortScanning() const;

    std::vector<LanHost> SnapshotHosts() const;       // sorted by IPv4, thread-safe copy
    std::vector<SsdpDevice> SnapshotSsdp() const;     // thread-safe copy

    static const std::vector<uint16_t>& CommonPorts();
    // Well-known service label for a port ("SSH", "SMB", "Printer (RAW 9100)" ...), empty if unknown.
    static std::wstring PortName(uint16_t port);

private:
    // Worker entry points. Each run is identified by a generation number; a run whose generation is
    // no longer current is cancelled and may not publish results.
    void ScanThreadMain(uint32_t gen, LanAdapterInfo adapter, int maxHosts);
    void SsdpThreadMain(uint32_t gen, int listenMs);
    void PortScanThreadMain(uint32_t gen, std::wstring ip);

    // Starts fn on a new thread stored in `slot`; the new thread first joins the previous occupant of
    // the slot (which is already cancelled), so the caller never blocks.
    void Launch(std::thread& slot, std::function<void()> fn);

    bool WithHostLocked(uint32_t ipHostOrder, const std::function<void(LanHost&)>& fn);

    mutable std::mutex mutex_;          // protects hosts_, ssdp_, lastAdapter_, haveAdapter_
    std::vector<LanHost> hosts_;
    std::vector<SsdpDevice> ssdp_;
    LanAdapterInfo lastAdapter_;
    bool haveAdapter_ = false;

    std::atomic<uint32_t> scanGen_{0}, ssdpGen_{0}, portGen_{0};
    std::atomic<bool> scanning_{false}, ssdpRunning_{false}, portScanning_{false};
    std::atomic<int> done_{0}, total_{0};

    std::mutex threadMutex_;            // protects the three thread handles
    std::thread scanThread_, ssdpThread_, portThread_;
    bool wsaOk_ = false;
};

} // namespace Explorer2
