#pragma once
// What the Linux build learns about disks from tools and files, as text: udisks2 (udisksctl, and the answers of its
// D-Bus methods as gdbus and busctl print them), smartctl --json, and the udev database. Parsing and decisions
// only, no system calls: the self-tests run all of this on Windows as well, because the Linux build cannot be tried
// on the machine it is written on. The readers are deliberately tolerant about layout (indentation, type notes,
// plain or JSON output), since the exact text differs between versions of the tools.
#include "JsonDom.h"
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::LinuxStorage {

inline std::string Trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return std::string(s.substr(a, b - a));
}

// 'text' or "text" without its quotes
inline std::string Unquote(std::string s) {
    if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"'))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// "306.15" whatever the C locale's decimal sign is (strtod follows the locale)
inline double ParseDecimal(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); ++i; }
    double v = 0.0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10.0 + (s[i] - '0'); ++i; }
    if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
        ++i;
        double scale = 0.1;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v += (s[i] - '0') * scale; scale *= 0.1; ++i; }
    }
    return neg ? -v : v;
}

// ---- "udisksctl dump" ----------------------------------------------------------------------------------------
//
//   /org/freedesktop/UDisks2/drives/Samsung_SSD_860_EVO_S3Z2NB0K123456:
//     org.freedesktop.UDisks2.Drive:
//       Model:                      Samsung SSD 860 EVO 500GB
//     org.freedesktop.UDisks2.Drive.Ata:
//       SmartTemperature:           306.15
//
// One object per path; per interface its properties as they are printed. The further elements of a list are on
// lines of their own, indented more than the property names: they are appended to the value, separated by spaces.

struct UdisksObject {
    std::string path;
    std::map<std::string, std::map<std::string, std::string>> interfaces;

    bool Has(const std::string& iface) const { return interfaces.count(iface) != 0; }
    std::string Get(const std::string& iface, const std::string& key) const {
        const auto i = interfaces.find(iface);
        if (i == interfaces.end()) return {};
        const auto p = i->second.find(key);
        return (p == i->second.end()) ? std::string() : p->second;
    }
};

inline std::vector<UdisksObject> ParseUdisksDump(const std::string& text) {
    std::vector<UdisksObject> out;
    std::string iface, lastKey;
    size_t propIndent = std::string::npos;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        const std::string t = Trim(line);
        if (t.empty()) continue;
        const size_t indent = line.find_first_not_of(" \t");

        if (indent == 0 && t[0] == '/' && t.back() == ':') {
            out.emplace_back();
            out.back().path = t.substr(0, t.size() - 1);
            iface.clear();
            lastKey.clear();
            continue;
        }
        if (out.empty()) continue;
        if (t.back() == ':' && t.find(' ') == std::string::npos && t.rfind("org.", 0) == 0) {
            iface = t.substr(0, t.size() - 1);
            out.back().interfaces[iface];
            lastKey.clear();
            propIndent = std::string::npos;
            continue;
        }
        if (iface.empty()) continue;

        // "Key:   value" - the key is one word and sits at the indentation of the interface's first property
        const size_t colon = t.find(':');
        bool isProp = (colon != std::string::npos && colon > 0);
        for (size_t i = 0; isProp && i < colon; ++i) {
            const char c = t[i];
            isProp = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        }
        if (isProp && (propIndent == std::string::npos || indent == propIndent)) {
            propIndent = indent;
            lastKey = t.substr(0, colon);
            out.back().interfaces[iface][lastKey] = Unquote(Trim(std::string_view(t).substr(colon + 1)));
        } else if (!lastKey.empty()) {
            std::string& value = out.back().interfaces[iface][lastKey];
            if (!value.empty()) value += ' ';
            value += Unquote(t);
        }
    }
    return out;
}

// What udisks2 knows about the health of the drive behind a device node such as "/dev/sda" or "/dev/nvme0n1"
struct UdisksDriveSmart {
    bool driveFound = false;        // udisks has a drive object for the device
    std::string drivePath;          // /org/freedesktop/UDisks2/drives/...
    bool isAta = false;             // the drive has the Drive.Ata interface (SATA, and USB bridges that pass ATA through)
    bool isNvme = false;            // the drive has the NVMe.Controller interface (udisks 2.10 and newer)
    bool smartSupported = false;
    bool smartEnabled = false;
    uint64_t updated = 0;           // Unix time of the last reading; 0 = never, and the other values mean nothing
    bool failing = false;           // the drive itself says it is about to fail
    double temperatureK = 0.0;      // 0 = unknown
    uint64_t powerOnSeconds = 0;    // ATA
    uint64_t powerOnHours = 0;      // NVMe
    int64_t badSectors = -1;        // pending and reallocated, -1 = unknown
    int attrsFailing = -1;
    std::string criticalWarning;    // NVMe: what the controller warns about; empty = nothing
};

inline UdisksDriveSmart FindUdisksDriveSmart(const std::vector<UdisksObject>& objects, const std::string& devNode) {
    static const char kBlock[] = "org.freedesktop.UDisks2.Block";
    static const char kAta[] = "org.freedesktop.UDisks2.Drive.Ata";
    static const char kNvme[] = "org.freedesktop.UDisks2.NVMe.Controller";
    UdisksDriveSmart r;
    std::string drivePath;
    for (const auto& o : objects) {
        if (!o.Has(kBlock)) continue;
        if (o.Get(kBlock, "Device") != devNode && o.Get(kBlock, "PreferredDevice") != devNode) continue;
        drivePath = o.Get(kBlock, "Drive");
        break;
    }
    if (drivePath.size() < 2) return r;      // "/" = the device belongs to no drive
    const UdisksObject* drive = nullptr;
    for (const auto& o : objects) {
        if (o.path == drivePath) { drive = &o; break; }
    }
    if (!drive) return r;
    r.driveFound = true;
    r.drivePath = drive->path;

    auto number = [&](const char* iface, const char* key) { return std::strtoull(drive->Get(iface, key).c_str(), nullptr, 10); };
    auto flag = [&](const char* iface, const char* key) {
        const std::string v = drive->Get(iface, key);
        return v == "true" || v == "True" || v == "TRUE" || v == "1";
    };
    if (drive->Has(kAta)) {
        r.isAta = true;
        r.smartSupported = flag(kAta, "SmartSupported");
        r.smartEnabled = flag(kAta, "SmartEnabled");
        r.updated = number(kAta, "SmartUpdated");
        r.failing = flag(kAta, "SmartFailing");
        r.temperatureK = ParseDecimal(drive->Get(kAta, "SmartTemperature"));
        r.powerOnSeconds = number(kAta, "SmartPowerOnSeconds");
        const std::string bad = drive->Get(kAta, "SmartNumBadSectors");
        if (!bad.empty()) r.badSectors = std::strtoll(bad.c_str(), nullptr, 10);
        const std::string failing = drive->Get(kAta, "SmartNumAttributesFailing");
        if (!failing.empty()) r.attrsFailing = std::atoi(failing.c_str());
    }
    if (drive->Has(kNvme)) {
        r.isNvme = true;
        r.smartSupported = true;
        r.smartEnabled = true;
        r.updated = number(kNvme, "SmartUpdated");
        r.temperatureK = ParseDecimal(drive->Get(kNvme, "SmartTemperature"));
        r.powerOnHours = number(kNvme, "SmartPowerOnHours");
        r.criticalWarning = Trim(drive->Get(kNvme, "SmartCriticalWarning"));
        if (r.criticalWarning == "[]") r.criticalWarning.clear();
    }
    return r;
}

// ---- Answers of D-Bus methods, as gdbus ("([(byte 0x05, 'name', uint16 51, 100, ...") and busctl (plain:
// a(ysqiiixia{sv}) 2 5 "name" 51 100 ...; or JSON) print them -------------------------------------------------
//
// Only the numbers and the quoted strings matter, in their order; type notes (byte, uint16, int64, @a{sv}), brackets
// and commas are skipped. That reads all three notations with one piece of code.

struct DbusToken {
    bool isString = false;
    std::string text;           // a string's content
    uint64_t value = 0;         // a number's magnitude
    bool negative = false;
};

inline std::vector<DbusToken> TokenizeDbusText(const std::string& s) {
    std::vector<DbusToken> out;
    const size_t n = s.size();
    auto isAlpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    size_t i = 0;
    while (i < n) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            DbusToken t;
            t.isString = true;
            size_t j = i + 1;
            while (j < n && s[j] != c) {
                if (s[j] == '\\' && j + 1 < n) ++j;
                t.text.push_back(s[j]);
                ++j;
            }
            out.push_back(std::move(t));
            i = (j < n) ? j + 1 : n;
        } else if (isAlpha(c)) {
            while (i < n && (isAlpha(s[i]) || isDigit(s[i]))) ++i;     // a type note or a bare word: byte, uint16, true
        } else if (isDigit(c) || (c == '-' && i + 1 < n && isDigit(s[i + 1]))) {
            DbusToken t;
            t.negative = (c == '-');
            size_t j = i + (t.negative ? 1 : 0);
            if (s[j] == '0' && j + 1 < n && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
                j += 2;
                for (; j < n; ++j) {
                    const int h = Json::HexDigitValue(s[j]);
                    if (h < 0) break;
                    t.value = t.value * 16 + static_cast<uint64_t>(h);
                }
            } else {
                while (j < n && isDigit(s[j])) { t.value = t.value * 10 + static_cast<uint64_t>(s[j] - '0'); ++j; }
                if (j + 1 < n && s[j] == '.' && isDigit(s[j + 1])) {       // a double: the whole part is enough here
                    ++j;
                    while (j < n && isDigit(s[j])) ++j;
                }
            }
            out.push_back(std::move(t));
            i = j;
        } else {
            ++i;
        }
    }
    return out;
}

// One row of Drive.Ata.SmartGetAttributes: (id, name, flags, value, worst, threshold, pretty, pretty_unit, {})
struct UdisksAtaAttr {
    int id = 0;
    std::string name;           // libatasmart's name, such as "reallocated-sector-count"
    unsigned flags = 0;         // bit 0: a failing value predicts a failure of the drive
    int value = -1;             // normalized, -1 = unknown
    int worst = -1;
    int threshold = -1;
    int64_t pretty = 0;         // the raw value turned into prettyUnit
    int prettyUnit = 0;         // 0 unknown, 1 plain number, 2 milliseconds, 3 sectors, 4 millikelvin, 5 / 6 percent, 7 MB
};

inline std::vector<UdisksAtaAttr> ParseUdisksAtaAttributes(const std::string& text) {
    const std::vector<DbusToken> tk = TokenizeDbusText(text);
    std::vector<UdisksAtaAttr> out;
    auto signedValue = [](const DbusToken& t) {
        const int64_t v = static_cast<int64_t>(t.value & 0x7FFFFFFFFFFFFFFFull);
        return t.negative ? -v : v;
    };
    for (size_t i = 1; i + 6 < tk.size(); ++i) {
        if (!tk[i].isString || tk[i - 1].isString) continue;
        bool sixNumbers = true;
        for (size_t k = 1; k <= 6; ++k) sixNumbers = sixNumbers && !tk[i + k].isString;
        if (!sixNumbers) continue;
        if (tk[i - 1].negative || tk[i - 1].value < 1 || tk[i - 1].value > 255) continue;
        UdisksAtaAttr a;
        a.id = static_cast<int>(tk[i - 1].value);
        a.name = tk[i].text;
        a.flags = static_cast<unsigned>(tk[i + 1].value & 0xFFFFu);
        a.value = static_cast<int>(signedValue(tk[i + 2]));
        a.worst = static_cast<int>(signedValue(tk[i + 3]));
        a.threshold = static_cast<int>(signedValue(tk[i + 4]));
        a.pretty = signedValue(tk[i + 5]);
        a.prettyUnit = static_cast<int>(signedValue(tk[i + 6]));
        if (a.prettyUnit < 0 || a.prettyUnit > 32) continue;
        out.push_back(std::move(a));
        i += 6;
    }
    return out;
}

// NVMe.Controller.SmartGetAttributes: a dictionary; every known key with the numbers that follow it
inline std::map<std::string, std::vector<uint64_t>> ParseUdisksNvmeAttributes(const std::string& text) {
    static const char* const kKeys[] = {
        "avail_spare", "spare_thresh", "percent_used", "total_data_read", "total_data_written", "ctrl_busy_time",
        "power_cycles", "unsafe_shutdowns", "media_errors", "num_err_log_entries", "temp_sensors", "wctemp", "cctemp",
        "warning_temp_time", "critical_temp_time"
    };
    std::map<std::string, std::vector<uint64_t>> out;
    std::string current;
    for (const DbusToken& t : TokenizeDbusText(text)) {
        if (t.isString) {
            bool known = false;
            for (const char* k : kKeys) {
                if (t.text == k) {
                    current = k;
                    out[current];
                    known = true;
                    break;
                }
            }
            // A key this code does not know ends the numbers of the key before it. The words of busctl's JSON
            // notation ("type", "data") and type codes ("y", "aq") are not keys.
            if (!known && t.text != "type" && t.text != "data" && t.text.size() > 2) current.clear();
        } else if (!current.empty() && !t.negative) {
            out[current].push_back(t.value);
        }
    }
    return out;
}

// ---- NVMe health values, whichever tool they came from -------------------------------------------------------
struct NvmeHealth {
    int64_t criticalWarning = -1;       // the bit mask of the health log, -1 = not known as a number
    std::string criticalWarningText;    // udisks names the warnings instead ("spare, temperature")
    int temperatureC = -1000;           // -1000 = unknown
    int availSpare = -1, spareThresh = -1, percentUsed = -1;
    int64_t bytesRead = -1, bytesWritten = -1;
    int64_t hostReads = -1, hostWrites = -1;
    int64_t ctrlBusyMinutes = -1;
    int64_t powerCycles = -1, powerOnHours = -1, unsafeShutdowns = -1;
    int64_t mediaErrors = -1, errLogEntries = -1;
    int64_t warnTempMinutes = -1, critTempMinutes = -1;
    std::vector<int> sensorsC;
};

inline int KelvinToC(uint64_t kelvin) { return static_cast<int>(kelvin) - 273; }

inline NvmeHealth NvmeHealthFromUdisks(const UdisksDriveSmart& drive, const std::map<std::string, std::vector<uint64_t>>& attrs) {
    NvmeHealth h;
    auto one = [&](const char* key) -> int64_t {
        const auto it = attrs.find(key);
        // busctl's plain notation counts the elements of an array first; a single value is the last number
        return (it == attrs.end() || it->second.empty()) ? -1 : static_cast<int64_t>(it->second.back());
    };
    h.criticalWarningText = drive.criticalWarning;
    if (drive.temperatureK > 200.0 && drive.temperatureK < 500.0) h.temperatureC = static_cast<int>(drive.temperatureK + 0.5) - 273;
    h.availSpare = static_cast<int>(one("avail_spare"));
    h.spareThresh = static_cast<int>(one("spare_thresh"));
    h.percentUsed = static_cast<int>(one("percent_used"));
    h.bytesRead = one("total_data_read");
    h.bytesWritten = one("total_data_written");
    h.ctrlBusyMinutes = one("ctrl_busy_time");
    h.powerCycles = one("power_cycles");
    h.powerOnHours = drive.powerOnHours > 0 ? static_cast<int64_t>(drive.powerOnHours) : -1;
    h.unsafeShutdowns = one("unsafe_shutdowns");
    h.mediaErrors = one("media_errors");
    h.errLogEntries = one("num_err_log_entries");
    h.warnTempMinutes = one("warning_temp_time");
    h.critTempMinutes = one("critical_temp_time");
    const auto sensors = attrs.find("temp_sensors");
    if (sensors != attrs.end()) {
        for (uint64_t k : sensors->second) {
            if (k > 200 && k < 500) h.sensorsC.push_back(KelvinToC(k));     // 0 = no such sensor; a small number = a count
        }
    }
    return h;
}

// ---- "smartctl --json -a <device>" ---------------------------------------------------------------------------
struct SmartctlAtaAttr {
    int id = 0;
    std::string name;
    int value = 0, worst = 0, thresh = 0;
    uint64_t raw = 0;
    std::string rawString;
    bool prefail = false;
    std::string whenFailed;     // "", "past" or "now"
};

struct SmartctlReport {
    bool parsed = false;            // the text was a JSON document
    bool permissionDenied = false;  // smartctl could not open the device as this user
    bool hasData = false;           // attributes or an NVMe health log were in it
    bool hasPassed = false;
    bool passed = true;
    int temperatureC = -1000;
    int64_t powerOnHours = -1;
    int64_t powerCycles = -1;
    std::vector<SmartctlAtaAttr> ata;
    bool hasNvme = false;
    NvmeHealth nvme;
    std::string message;            // what smartctl itself said went wrong
};

inline SmartctlReport ParseSmartctlJson(const std::string& text) {
    SmartctlReport r;
    if (text.find("Permission denied") != std::string::npos || text.find("requires root") != std::string::npos) r.permissionDenied = true;
    Json::Value doc;
    if (text.empty() || !Json::ParseDocument(text, doc) || !doc.IsObject()) return r;
    r.parsed = true;

    if (const Json::Value* sc = doc.Find("smartctl")) {
        if (const Json::Value* msgs = sc->Find("messages"); msgs && msgs->IsArray()) {
            for (const Json::Value& m : msgs->arrVal) {
                const std::string s = m.GetString("string");
                if (s.empty()) continue;
                if (!r.message.empty()) r.message += "; ";
                r.message += s;
                if (s.find("Permission denied") != std::string::npos) r.permissionDenied = true;
            }
        }
    }
    if (const Json::Value* st = doc.Find("smart_status")) {
        if (const Json::Value* p = st->Find("passed"); p && p->IsBool()) {
            r.hasPassed = true;
            r.passed = p->boolVal;
        }
    }
    if (const Json::Value* t = doc.Find("temperature")) r.temperatureC = t->GetInt("current", -1000);
    if (const Json::Value* p = doc.Find("power_on_time")) r.powerOnHours = p->GetInt64("hours", -1);
    r.powerCycles = doc.GetInt64("power_cycle_count", -1);

    if (const Json::Value* a = doc.Find("ata_smart_attributes")) {
        if (const Json::Value* table = a->Find("table"); table && table->IsArray()) {
            for (const Json::Value& row : table->arrVal) {
                SmartctlAtaAttr x;
                x.id = row.GetInt("id", 0);
                if (x.id < 1 || x.id > 255) continue;
                x.name = row.GetString("name");
                x.value = row.GetInt("value", 0);
                x.worst = row.GetInt("worst", 0);
                x.thresh = row.GetInt("thresh", 0);
                x.whenFailed = row.GetString("when_failed");
                if (const Json::Value* f = row.Find("flags")) x.prefail = f->GetBool("prefailure", false);
                if (const Json::Value* raw = row.Find("raw")) {
                    if (const Json::Value* v = raw->Find("value")) {
                        if (!v->AsUint64(x.raw)) x.raw = static_cast<uint64_t>(raw->GetInt64("value", 0));
                    }
                    x.rawString = raw->GetString("string");
                }
                r.ata.push_back(std::move(x));
            }
        }
        r.hasData = r.hasData || !r.ata.empty();
    }
    if (const Json::Value* n = doc.Find("nvme_smart_health_information_log"); n && n->IsObject()) {
        r.hasNvme = true;
        r.hasData = true;
        NvmeHealth& h = r.nvme;
        h.criticalWarning = n->GetInt64("critical_warning", -1);
        h.temperatureC = n->GetInt("temperature", -1000);
        h.availSpare = n->GetInt("available_spare", -1);
        h.spareThresh = n->GetInt("available_spare_threshold", -1);
        h.percentUsed = n->GetInt("percentage_used", -1);
        const int64_t unitsRead = n->GetInt64("data_units_read", -1);
        const int64_t unitsWritten = n->GetInt64("data_units_written", -1);
        if (unitsRead >= 0) h.bytesRead = unitsRead * 512000;           // a data unit is 1000 blocks of 512 bytes
        if (unitsWritten >= 0) h.bytesWritten = unitsWritten * 512000;
        h.hostReads = n->GetInt64("host_reads", -1);
        h.hostWrites = n->GetInt64("host_writes", -1);
        h.ctrlBusyMinutes = n->GetInt64("controller_busy_time", -1);
        h.powerCycles = n->GetInt64("power_cycles", -1);
        h.powerOnHours = n->GetInt64("power_on_hours", -1);
        h.unsafeShutdowns = n->GetInt64("unsafe_shutdowns", -1);
        h.mediaErrors = n->GetInt64("media_errors", -1);
        h.errLogEntries = n->GetInt64("num_err_log_entries", -1);
        h.warnTempMinutes = n->GetInt64("warning_temp_time", -1);
        h.critTempMinutes = n->GetInt64("critical_comp_time", -1);
        if (const Json::Value* s = n->Find("temperature_sensors"); s && s->IsArray()) {
            for (const Json::Value& v : s->arrVal) {
                if (v.IsNumber() && v.numVal > -100.0 && v.numVal < 250.0) h.sensorsC.push_back(static_cast<int>(v.numVal));
            }
        }
        if (r.temperatureC == -1000) r.temperatureC = h.temperatureC;
        if (r.powerOnHours < 0) r.powerOnHours = h.powerOnHours;
        if (r.powerCycles < 0) r.powerCycles = h.powerCycles;
    }
    return r;
}

// ---- The udev database (/run/udev/data/b<major>:<minor>) and volumes that are not mounted --------------------
//
// One file per block device, readable by everyone; the lines "E:NAME=value" are what the udev rules found out:
// ID_FS_USAGE=filesystem, ID_FS_TYPE=vfat, ID_FS_LABEL=KINGSTON, ID_BUS=usb ...

inline std::map<std::string, std::string> ParseUdevDb(const std::string& text) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string_view line(text.data() + pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 4 || line[0] != 'E' || line[1] != ':') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq <= 2) continue;
        out[std::string(line.substr(2, eq - 2))] = std::string(line.substr(eq + 1));
    }
    return out;
}

// udev writes a character it does not trust in a value as \xNN (a space in ID_FS_LABEL_ENC is "\x20")
inline std::string DecodeUdevEscapes(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() && s[i + 1] == 'x') {
            const int hi = Json::HexDigitValue(s[i + 2]);
            const int lo = Json::HexDigitValue(s[i + 3]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 3;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

// What is known about one partition (or a whole disk without partitions) when the drive list is made
struct BlockVolumeFacts {
    std::string name;               // "sdb1"
    uint64_t sizeBytes = 0;         // 0: a card reader without a card
    bool diskRemovable = false;     // /sys/block/<disk>/removable is 1
    bool diskOnUsb = false;         // the disk hangs off a USB port
    bool diskIsSdCard = false;      // an SD card (not the built-in eMMC)
    bool mounted = false;
    bool udevKnown = false;         // the udev database has an entry for the volume
    std::map<std::string, std::string> udev;

    std::string Udev(const char* key) const {
        const auto it = udev.find(key);
        return (it == udev.end()) ? std::string() : it->second;
    }
};

// A file system on a pluggable device that is not mounted: the drive list shows it, and a click mounts it.
// Fixed disks are left out (mounting them is the administrator's business), and so is what udisks hides itself.
inline bool IsMountableUnmountedVolume(const BlockVolumeFacts& v) {
    if (v.mounted || v.sizeBytes == 0) return false;
    if (!(v.diskRemovable || v.diskOnUsb || v.diskIsSdCard)) return false;
    if (!v.udevKnown) return false;                          // nothing says there is a file system on it
    if (v.Udev("ID_FS_USAGE") != "filesystem") return false; // "crypto", "raid", "other" (swap), or nothing at all
    const std::string type = v.Udev("ID_FS_TYPE");
    if (type.empty() || type == "swap") return false;
    if (v.Udev("UDISKS_IGNORE") == "1" || v.Udev("UDISKS_PRESENTATION_HIDE") == "1") return false;
    const std::string partType = v.Udev("ID_PART_ENTRY_TYPE");
    if (partType == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" || partType == "0xef") return false;     // EFI system partition
    return true;
}

// The volume's own name: its file system label, or empty when it has none
inline std::string VolumeLabelOf(const BlockVolumeFacts& v) {
    std::string label = DecodeUdevEscapes(v.Udev("ID_FS_LABEL_ENC"));
    if (label.empty()) label = v.Udev("ID_FS_LABEL");
    return Trim(label);
}

// "Mounted /dev/sdb1 at /run/media/user/KINGSTON" (udisksctl and gio print it alike; older udisksctl ends it with
// a full stop): the folder, or empty
inline std::string ParseMountedAt(const std::string& toolOutput) {
    const size_t at = toolOutput.find(" at ");
    if (at == std::string::npos) return {};
    size_t end = toolOutput.find('\n', at);
    if (end == std::string::npos) end = toolOutput.size();
    std::string path = Trim(std::string_view(toolOutput).substr(at + 4, end - at - 4));
    while (!path.empty() && (path.back() == '.' || path.back() == '\'' || path.back() == '`' || path.back() == '"')) path.pop_back();
    while (!path.empty() && (path.front() == '\'' || path.front() == '`' || path.front() == '"')) path.erase(0, 1);
    return (!path.empty() && path[0] == '/') ? path : std::string();
}

} // namespace Explorer2::LinuxStorage
