#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// winsock2.h must come before anything that pulls in windows.h (avoids the legacy winsock.h).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include "LanScanner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace Explorer2 {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kDiscoveryThreads = 96;     // blocking echo calls -> many threads, tiny CPU cost
constexpr int kNameThreads = 24;          // reverse DNS / NBSTAT workers
constexpr int kPingTimeoutMs = 400;
constexpr int kNbstatTimeoutMs = 600;
constexpr int kScanBudgetMs = 15000;      // global wall-clock budget of one scan (incl. name resolution)
constexpr int kPortTimeoutMs = 300;
constexpr int kHttpTimeoutMs = 1500;
constexpr size_t kHttpMaxBytes = 64 * 1024;

// ---------------------------------------------------------------- small helpers

std::wstring Widen(const std::string& s, UINT cp = CP_UTF8) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string ToLowerA(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

std::string TrimA(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

std::wstring TrimW(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n' || s[b] == 0xFEFF)) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
    return s.substr(b, e - b);
}

std::wstring FormatIp(uint32_t h) {
    wchar_t buf[24];
    swprintf_s(buf, L"%u.%u.%u.%u", (h >> 24) & 255, (h >> 16) & 255, (h >> 8) & 255, h & 255);
    return buf;
}

bool ParseIp(const std::wstring& s, uint32_t& out) {
    unsigned v[4]; int consumed = 0;
    if (swscanf_s(s.c_str(), L"%u.%u.%u.%u%n", &v[0], &v[1], &v[2], &v[3], &consumed) != 4) return false;
    for (unsigned x : v) if (x > 255) return false;
    if (consumed != (int)s.size()) return false;
    out = (v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3];
    return true;
}

bool ParseIpA(const std::string& s, uint32_t& out) {
    return ParseIp(Widen(s), out);
}

uint32_t MaskFromPrefix(int prefix) {
    if (prefix <= 0) return 0;
    if (prefix >= 32) return 0xFFFFFFFFu;
    return ~((1u << (32 - prefix)) - 1u);
}

int PrefixFromMask(uint32_t mask) {
    int p = 0;
    while (p < 32 && (mask & (0x80000000u >> p))) ++p;
    return p;
}

std::wstring FormatMac(const uint8_t* m) {
    wchar_t buf[24];
    swprintf_s(buf, L"%02X-%02X-%02X-%02X-%02X-%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return buf;
}

// ---------------------------------------------------------------- OUI table

using OuiMap = std::unordered_map<uint32_t, std::wstring>;

// Only prefixes that are certain; anything else is supplied by the optional user file.
const OuiMap& BuiltinOui() {
    static const OuiMap m = {
        {0x005056, L"VMware"}, {0x000C29, L"VMware"}, {0x000569, L"VMware"},
        {0x080027, L"VirtualBox"},
        {0x00155D, L"Microsoft Hyper-V"},
        {0x00163E, L"Xen"},
        {0x001C42, L"Parallels"},
        {0x525400, L"QEMU/KVM"},
        {0xB827EB, L"Raspberry Pi"}, {0xDCA632, L"Raspberry Pi"}, {0xE45F01, L"Raspberry Pi"},
        {0xD83ADD, L"Raspberry Pi"},
        {0x240AC4, L"Espressif"}, {0x30AEA4, L"Espressif"}, {0x246F28, L"Espressif"},
        {0x3C71BF, L"Espressif"}, {0xA4CF12, L"Espressif"},
        {0x002722, L"Ubiquiti"}, {0x24A43C, L"Ubiquiti"}, {0x44D9E7, L"Ubiquiti"},
        {0x802AA8, L"Ubiquiti"}, {0xF09FC2, L"Ubiquiti"}, {0xFCECDA, L"Ubiquiti"},
        {0x000E58, L"Sonos"}, {0x5CAAFD, L"Sonos"}, {0xB8E937, L"Sonos"}, {0x7828CA, L"Sonos"},
    };
    return m;
}

// Parses "AABBCC  Vendor", "AA:BB:CC<TAB>Vendor", Wireshark manuf ("AA:BB:CC<TAB>Short<TAB>Long name")
// and IEEE oui.txt ("AA-BB-CC   (hex)<TAB>Vendor") lines.
void ParseOuiLine(const std::wstring& rawLine, OuiMap& out) {
    std::wstring line = TrimW(rawLine);
    if (line.empty() || line[0] == L'#') return;
    size_t keyEnd = line.find_first_of(L" \t");
    if (keyEnd == std::wstring::npos) return;
    std::wstring key = line.substr(0, keyEnd);
    if (key.find(L'/') != std::wstring::npos) return;          // wireshark sub-MA-L masks: skip
    std::wstring hex;
    for (wchar_t c : key) {
        if (c == L':' || c == L'-' || c == L'.') continue;
        hex.push_back(c);
    }
    if (hex.size() != 6) return;
    uint32_t v = 0;
    for (wchar_t c : hex) {
        int d;
        if (c >= L'0' && c <= L'9') d = c - L'0';
        else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
        else return;
        v = (v << 4) | (uint32_t)d;
    }
    std::wstring rest = TrimW(line.substr(keyEnd));
    for (const wchar_t* tag : {L"(hex)", L"(base 16)"}) {
        size_t tl = wcslen(tag);
        if (rest.compare(0, tl, tag) == 0) { rest = TrimW(rest.substr(tl)); break; }
    }
    // Wireshark manuf has tab separated short + long names: prefer the long one.
    size_t tab = rest.find(L'\t');
    if (tab != std::wstring::npos) {
        std::wstring shortName = TrimW(rest.substr(0, tab));
        std::wstring longName = TrimW(rest.substr(rest.find_last_of(L'\t') + 1));
        rest = longName.empty() ? shortName : longName;
    }
    if (!rest.empty()) out[v] = rest;
}

const OuiMap& UserOui() {
    static OuiMap table;
    static std::once_flag once;
    std::call_once(once, [] {
        wchar_t base[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return;
        std::wstring path = std::wstring(base) + L"\\Explorer2\\oui.txt";
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER sz{};
        std::string data;
        if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (64LL << 20)) {
            data.resize((size_t)sz.QuadPart);
            DWORD got = 0;
            if (!ReadFile(h, data.data(), (DWORD)data.size(), &got, nullptr)) got = 0;
            data.resize(got);
        }
        CloseHandle(h);
        std::wstring text = Widen(data);
        size_t pos = 0;
        while (pos < text.size()) {
            size_t e = text.find(L'\n', pos);
            if (e == std::wstring::npos) e = text.size();
            ParseOuiLine(text.substr(pos, e - pos), table);
            pos = e + 1;
        }
    });
    return table;
}

std::wstring LookupVendor(const uint8_t* mac, bool randomized) {
    uint32_t key = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
    // A locally administered address carries no manufacturer information, except for the
    // well-known virtualization prefix(es) in the built-in table.
    auto& b = BuiltinOui();
    auto it = b.find(key);
    if (it != b.end()) return it->second;
    if (randomized) return L"";
    auto& u = UserOui();
    auto it2 = u.find(key);
    return it2 != u.end() ? it2->second : L"";
}

// ---------------------------------------------------------------- network primitives

bool ReadMacViaArp(uint32_t dstHost, uint32_t srcHost, uint8_t mac[6]) {
    ULONG mac8[2] = {0, 0};
    ULONG len = 6;
    DWORD r = SendARP(htonl(dstHost), htonl(srcHost), mac8, &len);
    if (r != NO_ERROR || len < 6) return false;
    memcpy(mac, mac8, 6);
    for (int i = 0; i < 6; ++i) if (mac[i]) return true;
    return false;
}

// ARP cache of the OS (IPv4): every neighbour with a resolved MAC. No admin rights needed.
std::vector<std::pair<uint32_t, std::array<uint8_t, 6>>> ReadNeighbours() {
    std::vector<std::pair<uint32_t, std::array<uint8_t, 6>>> out;
    PMIB_IPNET_TABLE2 table = nullptr;
    if (GetIpNetTable2(AF_INET, &table) != NO_ERROR || !table) return out;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IPNET_ROW2& r = table->Table[i];
        if (r.PhysicalAddressLength != 6 || r.State == NlnsUnreachable || r.State == NlnsIncomplete) continue;
        std::array<uint8_t, 6> mac{};
        memcpy(mac.data(), r.PhysicalAddress, 6);
        bool zero = true, bcast = true;
        for (uint8_t b : mac) { zero = zero && b == 0; bcast = bcast && b == 0xFF; }
        if (zero || bcast) continue;
        out.emplace_back(ntohl(r.Address.Ipv4.sin_addr.s_addr), mac);
    }
    FreeMibTable(table);
    return out;
}

// Returns RTT in ms or -1.
int PingHost(HANDLE icmp, uint32_t dstHost) {
    if (icmp == INVALID_HANDLE_VALUE) return -1;
    static const char payload[32] = "Explorer2LanScanner0123456789ab";
    alignas(8) unsigned char reply[sizeof(ICMP_ECHO_REPLY) + 32 + 8 + 64];
    IPAddr dst = htonl(dstHost);
    DWORD n = IcmpSendEcho(icmp, dst, (LPVOID)payload, sizeof(payload), nullptr, reply, sizeof(reply),
                           kPingTimeoutMs);
    if (n == 0) return -1;
    auto* r = reinterpret_cast<ICMP_ECHO_REPLY*>(reply);
    if (r->Status != IP_SUCCESS || r->Address != dst) return -1;
    return (int)r->RoundTripTime;
}

std::wstring ReverseDns(uint32_t ipHost) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(ipHost);
    wchar_t host[NI_MAXHOST] = {};
    if (GetNameInfoW(reinterpret_cast<SOCKADDR*>(&sa), sizeof(sa), host, NI_MAXHOST, nullptr, 0, NI_NAMEREQD) != 0)
        return L"";
    std::wstring s = host;
    while (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}

// NetBIOS node status (UDP 137). Returns the machine's unique workstation name or empty.
std::wstring NetbiosName(uint32_t ipHost) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return L"";
    std::wstring result;
    do {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(137);
        sa.sin_addr.s_addr = htonl(ipHost);
        // connect() on UDP filters foreign senders and turns ICMP port-unreachable into a fast error.
        if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) break;

        uint8_t q[50] = {};
        q[0] = 0x4E; q[1] = 0x42;               // transaction id
        q[5] = 1;                               // QDCOUNT = 1
        q[12] = 0x20;                           // name length (32)
        q[13] = 'C'; q[14] = 'K';               // encoded wildcard name "*"
        for (int i = 15; i < 45; ++i) q[i] = 'A';
        q[45] = 0;                              // name terminator
        q[46] = 0x00; q[47] = 0x21;             // QTYPE = NBSTAT
        q[48] = 0x00; q[49] = 0x01;             // QCLASS = IN
        if (send(s, reinterpret_cast<const char*>(q), sizeof(q), 0) != (int)sizeof(q)) break;

        fd_set rd; FD_ZERO(&rd); FD_SET(s, &rd);
        timeval tv{0, kNbstatTimeoutMs * 1000};
        if (select(0, &rd, nullptr, nullptr, &tv) <= 0) break;
        uint8_t buf[1024];
        int n = recv(s, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n < 12 + 2 + 10 + 1) break;

        int pos = 12;                           // skip the echoed question name (labels or pointer)
        while (pos < n && buf[pos] != 0) {
            if ((buf[pos] & 0xC0) == 0xC0) { ++pos; break; }
            pos += buf[pos] + 1;
        }
        ++pos;
        pos += 10;                              // type, class, ttl, rdlength
        if (pos >= n) break;
        int count = buf[pos++];
        std::wstring workstation, fileServer;
        for (int i = 0; i < count && pos + 18 <= n; ++i, pos += 18) {
            uint8_t suffix = buf[pos + 15];
            uint16_t flags = (uint16_t)((buf[pos + 16] << 8) | buf[pos + 17]);
            if (flags & 0x8000) continue;       // group name
            std::string nm(reinterpret_cast<char*>(buf + pos), 15);
            while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\0')) nm.pop_back();
            if (nm.empty()) continue;
            if (suffix == 0x00 && workstation.empty()) workstation = Widen(nm, CP_ACP);
            else if (suffix == 0x20 && fileServer.empty()) fileServer = Widen(nm, CP_ACP);
        }
        result = !workstation.empty() ? workstation : fileServer;
    } while (false);
    closesocket(s);
    return result;
}

// Non-blocking connect to ip:port; returns a connected (blocking-mode off) socket or INVALID_SOCKET.
SOCKET ConnectWithDeadline(uint32_t ipHost, uint16_t port, Clock::time_point deadline) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ipHost);
    if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    auto left = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
    if (left < 0) left = 0;
    fd_set wr, ex; FD_ZERO(&wr); FD_ZERO(&ex); FD_SET(s, &wr); FD_SET(s, &ex);
    timeval tv{(long)(left / 1000000), (long)(left % 1000000)};
    int r = select(0, nullptr, &wr, &ex, &tv);
    int err = 1; int el = sizeof(err);
    if (r > 0 && FD_ISSET(s, &wr)) getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &el);
    if (err != 0) { closesocket(s); return INVALID_SOCKET; }
    return s;
}

// ---------------------------------------------------------------- HTTP (plain) + XML helpers

struct ParsedUrl { uint32_t ip = 0; uint16_t port = 80; std::string path = "/"; std::string hostHeader; };

bool ParseHttpUrl(const std::string& url, ParsedUrl& out) {
    static const std::string kPrefix = "http://";
    if (url.size() <= kPrefix.size() || ToLowerA(url.substr(0, kPrefix.size())) != kPrefix) return false;
    size_t hostBegin = kPrefix.size();
    size_t slash = url.find('/', hostBegin);
    std::string hostPort = url.substr(hostBegin, slash == std::string::npos ? std::string::npos : slash - hostBegin);
    out.path = slash == std::string::npos ? "/" : url.substr(slash);
    out.hostHeader = hostPort;
    std::string host = hostPort;
    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        host = hostPort.substr(0, colon);
        int p = atoi(hostPort.c_str() + colon + 1);
        if (p <= 0 || p > 65535) return false;
        out.port = (uint16_t)p;
    }
    return ParseIpA(host, out.ip);   // only IPv4 literals (that is what UPnP devices advertise)
}

std::string DecodeChunked(const std::string& in) {
    std::string out;
    size_t pos = 0;
    while (pos < in.size()) {
        size_t eol = in.find("\r\n", pos);
        if (eol == std::string::npos) break;
        unsigned long len = strtoul(in.c_str() + pos, nullptr, 16);
        pos = eol + 2;
        if (len == 0) break;
        out.append(in, pos, std::min<size_t>(len, in.size() - pos));
        pos += len + 2;
    }
    return out;
}

// GET with an overall wall-clock timeout; returns the (de-chunked) body of a 200 response.
bool HttpGetBody(const std::string& url, std::string& body) {
    ParsedUrl u;
    if (!ParseHttpUrl(url, u)) return false;
    auto deadline = Clock::now() + std::chrono::milliseconds(kHttpTimeoutMs);
    SOCKET s = ConnectWithDeadline(u.ip, u.port, deadline);
    if (s == INVALID_SOCKET) return false;
    bool ok = false;
    do {
        std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.hostHeader +
                          "\r\nUser-Agent: Explorer2/1.0 UPnP/1.1\r\nAccept: */*\r\nConnection: close\r\n\r\n";
        if (send(s, req.data(), (int)req.size(), 0) != (int)req.size()) break;
        std::string raw;
        char buf[4096];
        while (raw.size() < kHttpMaxBytes + 2048) {
            auto left = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
            if (left <= 0) break;
            fd_set rd; FD_ZERO(&rd); FD_SET(s, &rd);
            timeval tv{(long)(left / 1000000), (long)(left % 1000000)};
            if (select(0, &rd, nullptr, nullptr, &tv) <= 0) break;
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;                               // closed (or error): we got what there is
            raw.append(buf, (size_t)n);
        }
        size_t hdrEnd = raw.find("\r\n\r\n");
        if (hdrEnd == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) break;
        std::string head = ToLowerA(raw.substr(0, hdrEnd));
        size_t sp = head.find(' ');
        if (sp == std::string::npos || head.compare(sp + 1, 3, "200") != 0) break;
        body = raw.substr(hdrEnd + 4);
        if (head.find("transfer-encoding: chunked") != std::string::npos) body = DecodeChunked(body);
        if (body.size() > kHttpMaxBytes) body.resize(kHttpMaxBytes);
        ok = true;
    } while (false);
    closesocket(s);
    return ok;
}

std::string DecodeXmlEntities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out.push_back(s[i]); continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out.push_back(s[i]); continue; }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                                   ? strtoul(ent.c_str() + 2, nullptr, 16) : strtoul(ent.c_str() + 1, nullptr, 10);
            if (cp > 0 && cp < 0x110000) {   // encode as UTF-8
                if (cp < 0x80) out.push_back((char)cp);
                else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
                else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
                else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
            }
        } else { out.push_back('&'); continue; }
        i = semi;
    }
    return out;
}

// First <tag>value</tag> (case-insensitive tag name, no namespace prefix). The root device always
// precedes embedded devices in UPnP descriptions, so the first hit describes the device itself.
std::wstring XmlTag(const std::string& xml, const std::string& lowerXml, const char* tag) {
    std::string open = std::string("<") + ToLowerA(tag) + ">";
    std::string close = std::string("</") + ToLowerA(tag) + ">";
    size_t b = lowerXml.find(open);
    if (b == std::string::npos) return L"";
    b += open.size();
    size_t e = lowerXml.find(close, b);
    if (e == std::string::npos) return L"";
    return TrimW(Widen(DecodeXmlEntities(xml.substr(b, e - b))));
}

// ---------------------------------------------------------------- SSDP parsing

struct SsdpReply { std::string location, server, st, usn; };

bool ParseSsdpReply(const std::string& msg, SsdpReply& out) {
    bool isReply = msg.compare(0, 9, "HTTP/1.1 ") == 0 || msg.compare(0, 9, "HTTP/1.0 ") == 0;
    bool isNotify = msg.compare(0, 7, "NOTIFY ") == 0;
    if (!isReply && !isNotify) return false;
    size_t pos = msg.find("\r\n");
    while (pos != std::string::npos && pos + 2 < msg.size()) {
        pos += 2;
        size_t eol = msg.find("\r\n", pos);
        std::string line = msg.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = ToLowerA(TrimA(line.substr(0, colon)));
            std::string val = TrimA(line.substr(colon + 1));
            if (key == "location") out.location = val;
            else if (key == "server") out.server = val;
            else if (key == "st" || key == "nt") out.st = val;
            else if (key == "usn") out.usn = val;
        }
        pos = eol;
    }
    return !(out.usn.empty() && out.location.empty());
}

} // namespace

// ================================================================ LanScanner

LanScanner::LanScanner() {
    WSADATA wsa{};
    wsaOk_ = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
}

LanScanner::~LanScanner() {
    // Bump every generation: all workers see "stale" and bail out at their next check.
    ++scanGen_; ++ssdpGen_; ++portGen_;
    std::thread a, b, c;
    {
        std::lock_guard<std::mutex> lk(threadMutex_);
        a = std::move(scanThread_); b = std::move(ssdpThread_); c = std::move(portThread_);
    }
    if (a.joinable()) a.join();   // each thread transitively joined its predecessors
    if (b.joinable()) b.join();
    if (c.joinable()) c.join();
    if (wsaOk_) WSACleanup();
}

void LanScanner::Launch(std::thread& slot, std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(threadMutex_);
    std::thread prev = std::move(slot);
    slot = std::thread([prev = std::move(prev), fn = std::move(fn)]() mutable {
        if (prev.joinable()) prev.join();
        fn();
    });
}

bool LanScanner::WithHostLocked(uint32_t ip, const std::function<void(LanHost&)>& fn) {
    // caller must hold mutex_
    for (auto& h : hosts_) {
        if (h.ipHostOrder == ip) { fn(h); return true; }
    }
    return false;
}

std::vector<LanAdapterInfo> LanScanner::EnumerateAdapters() {
    std::vector<LanAdapterInfo> result;
    ULONG size = 32 * 1024;
    std::vector<unsigned char> buf;
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 4 && rc == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buf.assign(size, 0);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc != NO_ERROR) return result;

    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL) continue;

        std::wstring gateway;
        for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
            if (g->Address.lpSockaddr && g->Address.lpSockaddr->sa_family == AF_INET) {
                auto* sin = reinterpret_cast<sockaddr_in*>(g->Address.lpSockaddr);
                gateway = FormatIp(ntohl(sin->sin_addr.s_addr));
                break;
            }
        }
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            if ((ip >> 24) == 127 || (ip >> 16) == 0xA9FE || ip == 0) continue;   // loopback / link-local
            LanAdapterInfo info;
            info.name = a->FriendlyName ? a->FriendlyName : L"";
            info.prefix = std::clamp<int>(u->OnLinkPrefixLength, 0, 32);
            info.ipHostOrder = ip;
            info.maskHostOrder = MaskFromPrefix(info.prefix);
            info.ip = FormatIp(ip);
            info.mask = FormatIp(info.maskHostOrder);
            info.gateway = gateway;
            result.push_back(std::move(info));
        }
    }
    // Adapters with a default gateway (the "real" LAN/Internet ones) first, stable otherwise.
    std::stable_sort(result.begin(), result.end(), [](const LanAdapterInfo& x, const LanAdapterInfo& y) {
        return x.gateway.empty() < y.gateway.empty();
    });
    return result;
}

// ---------------------------------------------------------------- scan control / snapshots

void LanScanner::StartScan(const LanAdapterInfo& adapterIn, int maxHosts) {
    LanAdapterInfo adapter = adapterIn;
    if (adapter.ipHostOrder == 0) ParseIp(adapter.ip, adapter.ipHostOrder);
    if (adapter.maskHostOrder == 0) {
        if (!ParseIp(adapter.mask, adapter.maskHostOrder)) adapter.maskHostOrder = MaskFromPrefix(adapter.prefix);
    }
    adapter.prefix = PrefixFromMask(adapter.maskHostOrder);

    uint32_t gen = ++scanGen_;   // invalidates any previous scan
    {
        std::lock_guard<std::mutex> lk(mutex_);
        hosts_.clear();
        lastAdapter_ = adapter;
        haveAdapter_ = true;
    }
    done_ = 0;
    total_ = 0;
    scanning_ = true;
    Launch(scanThread_, [this, gen, adapter, maxHosts] { ScanThreadMain(gen, adapter, maxHosts); });
}

void LanScanner::CancelScan() {
    ++scanGen_;
    scanning_ = false;
}

bool LanScanner::IsScanning() const { return scanning_.load(); }

void LanScanner::GetScanProgress(int& done, int& total) const {
    done = done_.load();
    total = total_.load();
}

bool LanScanner::IsSsdpRunning() const { return ssdpRunning_.load(); }
bool LanScanner::IsPortScanning() const { return portScanning_.load(); }

std::vector<LanHost> LanScanner::SnapshotHosts() const {
    std::vector<LanHost> copy;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        copy = hosts_;
    }
    std::sort(copy.begin(), copy.end(), [](const LanHost& a, const LanHost& b) { return a.ipHostOrder < b.ipHostOrder; });
    return copy;
}

std::vector<SsdpDevice> LanScanner::SnapshotSsdp() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return ssdp_;
}

// ---------------------------------------------------------------- ports

const std::vector<uint16_t>& LanScanner::CommonPorts() {
    static const std::vector<uint16_t> ports = {
        21, 22, 23, 25, 53, 80, 110, 135, 139, 143, 443, 445, 515, 548, 554, 631, 1883, 2049,
        3000, 3306, 3389, 5000, 5001, 5357, 5432, 5900, 6379, 7000, 8000, 8008, 8009, 8080,
        8123, 8443, 8883, 9100, 32400, 62078};
    return ports;
}

std::wstring LanScanner::PortName(uint16_t port) {
    switch (port) {
    case 21: return L"FTP";
    case 22: return L"SSH";
    case 23: return L"Telnet";
    case 25: return L"SMTP";
    case 53: return L"DNS";
    case 80: return L"HTTP";
    case 110: return L"POP3";
    case 135: return L"RPC";
    case 139: return L"NetBIOS";
    case 143: return L"IMAP";
    case 443: return L"HTTPS";
    case 445: return L"SMB";
    case 515: return L"Printer (LPD)";
    case 548: return L"AFP";
    case 554: return L"RTSP";
    case 631: return L"IPP / CUPS";
    case 1883: return L"MQTT";
    case 2049: return L"NFS";
    case 3000: return L"HTTP (dev)";
    case 3306: return L"MySQL";
    case 3389: return L"RDP";
    case 5000: return L"HTTP (alt 5000)";
    case 5001: return L"HTTPS (alt 5001)";
    case 5357: return L"WSD";
    case 5432: return L"PostgreSQL";
    case 5900: return L"VNC";
    case 6379: return L"Redis";
    case 7000: return L"AirPlay";
    case 8000: return L"HTTP (alt 8000)";
    case 8008: return L"Chromecast HTTP";
    case 8009: return L"Chromecast";
    case 8080: return L"HTTP (alt 8080)";
    case 8123: return L"Home Assistant";
    case 8443: return L"HTTPS (alt 8443)";
    case 8883: return L"MQTT (TLS)";
    case 9100: return L"Printer (RAW 9100)";
    case 32400: return L"Plex";
    case 62078: return L"iOS lockdown";
    default: return L"";
    }
}

void LanScanner::StartPortScan(const std::wstring& ip) {
    uint32_t gen = ++portGen_;
    uint32_t ipHost = 0;
    if (ParseIp(ip, ipHost)) {
        std::lock_guard<std::mutex> lk(mutex_);
        WithHostLocked(ipHost, [](LanHost& h) { h.openPorts.clear(); h.portsScanned = false; });
    }
    portScanning_ = true;
    Launch(portThread_, [this, gen, ip] { PortScanThreadMain(gen, ip); });
}

void LanScanner::PortScanThreadMain(uint32_t gen, std::wstring ip) {
    auto stale = [&] { return portGen_.load() != gen; };
    auto finish = [&] { if (!stale()) portScanning_ = false; };
    uint32_t ipHost = 0;
    if (stale() || !ParseIp(ip, ipHost)) { finish(); return; }

    // All sockets of a chunk are connected in parallel (non-blocking) and watched with one select().
    constexpr size_t kChunk = 48;   // below FD_SETSIZE (64)
    const auto& ports = CommonPorts();
    std::vector<uint16_t> open;
    for (size_t base = 0; base < ports.size() && !stale(); base += kChunk) {
        struct Pending { SOCKET s; uint16_t port; };
        std::vector<Pending> pending;
        auto deadline = Clock::now() + std::chrono::milliseconds(kPortTimeoutMs);
        for (size_t i = base; i < std::min(ports.size(), base + kChunk); ++i) {
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET) continue;
            u_long nb = 1;
            ioctlsocket(s, FIONBIO, &nb);
            sockaddr_in sa{};
            sa.sin_family = AF_INET;
            sa.sin_port = htons(ports[i]);
            sa.sin_addr.s_addr = htonl(ipHost);
            if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
                open.push_back(ports[i]);
                closesocket(s);
            } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
                pending.push_back({s, ports[i]});
            } else {
                closesocket(s);
            }
        }
        while (!pending.empty() && !stale()) {
            auto left = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
            if (left <= 0) break;
            fd_set wr, ex; FD_ZERO(&wr); FD_ZERO(&ex);
            for (auto& p : pending) { FD_SET(p.s, &wr); FD_SET(p.s, &ex); }
            left = std::min<long long>(left, 50000);   // slices keep cancellation responsive
            timeval tv{0, (long)left};
            if (select(0, nullptr, &wr, &ex, &tv) <= 0) continue;
            for (size_t i = 0; i < pending.size();) {
                bool w = FD_ISSET(pending[i].s, &wr) != 0, x = FD_ISSET(pending[i].s, &ex) != 0;
                if (!w && !x) { ++i; continue; }
                int err = 1; int el = sizeof(err);
                if (w) getsockopt(pending[i].s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &el);
                if (err == 0) open.push_back(pending[i].port);
                closesocket(pending[i].s);
                pending.erase(pending.begin() + (ptrdiff_t)i);
            }
        }
        for (auto& p : pending) closesocket(p.s);
    }
    std::sort(open.begin(), open.end());
    if (!stale()) {
        std::lock_guard<std::mutex> lk(mutex_);
        WithHostLocked(ipHost, [&](LanHost& h) { h.openPorts = open; h.portsScanned = true; });
    }
    finish();
}

// ---------------------------------------------------------------- host discovery

void LanScanner::ScanThreadMain(uint32_t gen, LanAdapterInfo adapter, int maxHosts) {
    auto stale = [&] { return scanGen_.load() != gen; };
    auto finish = [&] { if (!stale()) scanning_ = false; };
    if (stale()) return;                       // superseded before we even started

    const auto deadline = Clock::now() + std::chrono::milliseconds(kScanBudgetMs);
    const uint32_t self = adapter.ipHostOrder;
    uint32_t gateway = 0;
    bool haveGateway = ParseIp(adapter.gateway, gateway);

    // ---- build the address list (network and broadcast excluded, clamped to maxHosts)
    std::vector<uint32_t> addrs;
    {
        uint32_t mask = adapter.maskHostOrder;
        uint32_t net = self & mask, bcast = net | ~mask;
        maxHosts = std::clamp(maxHosts, 1, 65534);
        if (self != 0 && bcast - net >= 2) {
            uint64_t first = (uint64_t)net + 1, last = (uint64_t)bcast - 1;
            uint64_t count = last - first + 1;
            uint64_t start = first;
            if (count > (uint64_t)maxHosts) {
                // Window centred on our own address, shifted to stay inside the subnet.
                uint64_t half = (uint64_t)maxHosts / 2;
                start = self > first + half ? self - half : first;
                if (start + maxHosts - 1 > last) start = last - maxHosts + 1;
                count = (uint64_t)maxHosts;
            }
            for (uint64_t i = 0; i < count; ++i) addrs.push_back((uint32_t)(start + i));
            if (haveGateway && (gateway & mask) == net && gateway != net && gateway != bcast &&
                std::find(addrs.begin(), addrs.end(), gateway) == addrs.end())
                addrs.push_back(gateway);       // always probe the router
        }
    }
    total_ = (int)addrs.size();
    if (addrs.empty()) { finish(); return; }

    // SSDP results that arrived before/while scanning mark hosts too.
    auto ssdpSeen = [&](uint32_t ip) {
        std::wstring s = FormatIp(ip);
        for (auto& d : ssdp_) if (d.ip == s) return true;
        return false;
    };

    // ---- name resolution queue + pool (second pool, fed as hosts appear)
    struct NameQueue {
        std::mutex m;
        std::condition_variable cv;
        std::deque<uint32_t> q;
        std::unordered_set<uint32_t> queued;   // every ip ever enqueued (names are resolved once)
        bool closed = false;
    } nq;
    std::atomic<int> pendingNames{0};
    std::atomic<bool> stopNames{false};

    auto nameWorker = [&] {
        for (;;) {
            uint32_t ip;
            {
                std::unique_lock<std::mutex> lk(nq.m);
                nq.cv.wait(lk, [&] { return !nq.q.empty() || nq.closed || stopNames.load(); });
                if (stopNames.load() || nq.q.empty()) return;
                ip = nq.q.front();
                nq.q.pop_front();
            }
            if (!stale() && Clock::now() < deadline) {
                std::wstring nb = NetbiosName(ip);
                std::wstring dns = (stale() || Clock::now() >= deadline) ? L"" : ReverseDns(ip);
                if (!stale() && (!nb.empty() || !dns.empty())) {
                    std::lock_guard<std::mutex> lk(mutex_);
                    if (!stale()) WithHostLocked(ip, [&](LanHost& h) { h.netbiosName = nb; h.hostName = dns; });
                }
            }
            --pendingNames;
        }
    };
    std::vector<std::thread> nameThreads;
    for (int i = 0; i < kNameThreads; ++i) nameThreads.emplace_back(nameWorker);

    // Finds-or-creates the host entry for `ip`, lets `mutate` fill it and schedules name resolution once.
    auto upsert = [&](uint32_t ip, const std::function<void(LanHost&)>& mutate) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (stale()) return;
            LanHost* h = nullptr;
            for (auto& x : hosts_) if (x.ipHostOrder == ip) { h = &x; break; }
            if (!h) {
                hosts_.emplace_back();
                h = &hosts_.back();
                h->ipHostOrder = ip;
                h->ip = FormatIp(ip);
                h->isGateway = haveGateway && ip == gateway;
                h->viaSsdp = ssdpSeen(ip);
            }
            mutate(*h);
        }
        std::lock_guard<std::mutex> lk(nq.m);
        if (nq.closed || !nq.queued.insert(ip).second) return;
        ++pendingNames;
        nq.q.push_back(ip);
        nq.cv.notify_one();
    };
    auto setMac = [](LanHost& h, const uint8_t* mac) {
        h.mac = FormatMac(mac);
        h.randomizedMac = (mac[0] & 0x02) != 0;
        h.vendor = LookupVendor(mac, h.randomizedMac);
        h.viaArp = true;
    };

    // ---- phase 1: ICMP echo of every address. SendARP/echo of an address nobody owns blocks for ~3 s
    // (the OS retries ARP), which made a /24 take ~18 s. An echo has a hard timeout and, as a side
    // effect, makes the OS ARP-resolve the target - the MAC is then read from the neighbour cache.
    std::atomic<size_t> next{0};
    auto pingWorker = [&] {
        HANDLE icmp = IcmpCreateFile();
        for (;;) {
            size_t idx = next.fetch_add(1);
            if (idx >= addrs.size() || stale() || Clock::now() >= deadline) break;
            uint32_t ip = addrs[idx];
            if (ip == self) {
                uint8_t mac[6] = {};
                bool haveMac = ReadMacViaArp(ip, self, mac);   // answers instantly for a local address
                upsert(ip, [&](LanHost& h) {
                    h.isSelf = true;
                    h.rttMs = 0;
                    if (haveMac) setMac(h, mac);
                });
            } else {
                int rtt = PingHost(icmp, ip);
                if (rtt >= 0) upsert(ip, [&](LanHost& h) { h.rttMs = rtt; h.viaPing = true; });
            }
            if (!stale()) ++done_;
        }
        if (icmp != INVALID_HANDLE_VALUE) IcmpCloseHandle(icmp);
    };
    {
        std::vector<std::thread> pool;
        int n = std::min<int>(kDiscoveryThreads, (int)addrs.size());
        for (int i = 0; i < n; ++i) pool.emplace_back(pingWorker);
        for (auto& t : pool) t.join();
    }
    if (!stale()) done_ = total_.load();     // budget-expired remainder counts as processed

    // ---- phase 2: ARP results from the neighbour cache (finds hosts that drop ICMP, and fills MACs).
    // Read once immediately, again after ~1 s to catch slow (e.g. Wi-Fi power-save) responders.
    {
        std::unordered_set<uint32_t> inRange(addrs.begin(), addrs.end());
        std::unordered_set<uint32_t> applied;
        for (int round = 0; round < 2 && !stale() && Clock::now() < deadline; ++round) {
            if (round == 1) {
                for (int i = 0; i < 20 && !stale(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            for (auto& [ip, mac] : ReadNeighbours()) {
                if (stale()) break;
                if (ip == self || !inRange.count(ip) || !applied.insert(ip).second) continue;
                std::array<uint8_t, 6> m = mac;
                upsert(ip, [&](LanHost& h) { setMac(h, m.data()); });
            }
        }
    }
    { std::lock_guard<std::mutex> lk(nq.m); nq.closed = true; }
    nq.cv.notify_all();

    // ---- wait for names, bounded by the global budget
    while (!stale() && Clock::now() < deadline && pendingNames.load() > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    finish();                                  // UI sees "done" now; stragglers are joined below
    stopNames = true;
    nq.cv.notify_all();
    for (auto& t : nameThreads) t.join();
}

// ---------------------------------------------------------------- SSDP

void LanScanner::StartSsdp(int listenMs) {
    uint32_t gen = ++ssdpGen_;
    listenMs = std::clamp(listenMs, 500, 30000);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        ssdp_.clear();
    }
    ssdpRunning_ = true;
    Launch(ssdpThread_, [this, gen, listenMs] { SsdpThreadMain(gen, listenMs); });
}

void LanScanner::SsdpThreadMain(uint32_t gen, int listenMs) {
    auto stale = [&] { return ssdpGen_.load() != gen; };
    auto finish = [&] { if (!stale()) ssdpRunning_ = false; };
    if (stale()) return;

    LanAdapterInfo adapter;
    bool haveAdapter;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        adapter = lastAdapter_;
        haveAdapter = haveAdapter_;
        if (stale()) return;
        ssdp_.clear();   // StartSsdp's own clear may precede a late reply of a superseded run
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { finish(); return; }
    {
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = 0;
        local.sin_addr.s_addr = haveAdapter ? htonl(adapter.ipHostOrder) : htonl(INADDR_ANY);
        if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local));
        }
        if (haveAdapter) {
            in_addr ifAddr{};
            ifAddr.s_addr = htonl(adapter.ipHostOrder);
            setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&ifAddr), sizeof(ifAddr));
        }
        int ttl = 2;
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));
    }

    sockaddr_in mcast{};
    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &mcast.sin_addr);
    auto makeSearch = [](const char* st) {
        return std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") +
               st + "\r\nUSER-AGENT: Windows/10 UPnP/1.1 Explorer2/1.0\r\n\r\n";
    };
    const std::string searchAll = makeSearch("ssdp:all"), searchRoot = makeSearch("upnp:rootdevice");

    const auto start = Clock::now();
    const auto end = start + std::chrono::milliseconds(listenMs);
    const int sendAtMs[3] = {0, 600, 1300};
    int sent = 0;

    std::vector<std::string> locations;       // unique LOCATION urls, fetched afterwards
    std::vector<char> rx(8192);
    while (!stale() && Clock::now() < end) {
        auto elapsedMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        while (sent < 3 && elapsedMs >= sendAtMs[sent]) {
            sendto(s, searchAll.data(), (int)searchAll.size(), 0, reinterpret_cast<sockaddr*>(&mcast), sizeof(mcast));
            sendto(s, searchRoot.data(), (int)searchRoot.size(), 0, reinterpret_cast<sockaddr*>(&mcast), sizeof(mcast));
            ++sent;
        }
        fd_set rd; FD_ZERO(&rd); FD_SET(s, &rd);
        timeval tv{0, 50 * 1000};
        if (select(0, &rd, nullptr, nullptr, &tv) <= 0) continue;
        sockaddr_in from{};
        int fl = sizeof(from);
        int n = recvfrom(s, rx.data(), (int)rx.size(), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;                    // e.g. WSAECONNRESET from a previous send
        SsdpReply rep;
        if (!ParseSsdpReply(std::string(rx.data(), (size_t)n), rep)) continue;

        uint32_t fromIp = ntohl(from.sin_addr.s_addr);
        SsdpDevice dev;
        dev.ip = FormatIp(fromIp);
        dev.location = Widen(rep.location);
        dev.server = Widen(rep.server);
        dev.st = Widen(rep.st);
        dev.usn = Widen(rep.usn);
        bool isNew = false;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (stale()) break;
            isNew = std::none_of(ssdp_.begin(), ssdp_.end(), [&](const SsdpDevice& d) {
                return d.ip == dev.ip && d.usn == dev.usn && (!dev.usn.empty() || d.st == dev.st);
            });
            if (isNew) ssdp_.push_back(dev);

            // Hosts seen only through SSDP: add (or flag) them when they are in the local subnet.
            bool sameSubnet = haveAdapter && (fromIp & adapter.maskHostOrder) == (adapter.ipHostOrder & adapter.maskHostOrder);
            if (sameSubnet && fromIp != adapter.ipHostOrder) {
                bool found = WithHostLocked(fromIp, [](LanHost& h) { h.viaSsdp = true; });
                if (!found) {
                    LanHost h;
                    h.ip = dev.ip;
                    h.ipHostOrder = fromIp;
                    h.viaSsdp = true;
                    uint32_t gw = 0;
                    h.isGateway = ParseIp(adapter.gateway, gw) && gw == fromIp;
                    uint8_t mac[6];
                    // Safe to call under the lock: the device just talked to us, so ARP is cached.
                    if (ReadMacViaArp(fromIp, adapter.ipHostOrder, mac)) {
                        h.mac = FormatMac(mac);
                        h.viaArp = true;
                        h.randomizedMac = (mac[0] & 0x02) != 0;
                        h.vendor = LookupVendor(mac, h.randomizedMac);
                    }
                    hosts_.push_back(std::move(h));
                }
            }
        }
        if (isNew && !rep.location.empty() &&
            std::find(locations.begin(), locations.end(), rep.location) == locations.end())
            locations.push_back(rep.location);
    }
    closesocket(s);
    if (stale()) { return; }

    // ---- fetch device descriptions in parallel (bounded: <=16 threads, 1.5 s each)
    std::atomic<size_t> next{0};
    auto fetchWorker = [&] {
        for (;;) {
            size_t i = next.fetch_add(1);
            if (i >= locations.size() || stale()) return;
            std::string body;
            if (!HttpGetBody(locations[i], body)) continue;
            std::string lower = ToLowerA(body);
            std::wstring loc = Widen(locations[i]);
            std::wstring fn = XmlTag(body, lower, "friendlyName"), mf = XmlTag(body, lower, "manufacturer"),
                         mn = XmlTag(body, lower, "modelName"), md = XmlTag(body, lower, "modelDescription"),
                         dt = XmlTag(body, lower, "deviceType");
            std::lock_guard<std::mutex> lk(mutex_);
            if (stale()) return;
            for (auto& d : ssdp_) {
                if (d.location != loc) continue;
                d.friendlyName = fn; d.manufacturer = mf; d.modelName = mn; d.modelDescription = md; d.deviceType = dt;
            }
        }
    };
    {
        std::vector<std::thread> pool;
        int n = std::min<int>(16, (int)locations.size());
        for (int i = 0; i < n; ++i) pool.emplace_back(fetchWorker);
        for (auto& t : pool) t.join();
    }
    finish();
}

} // namespace Explorer2
