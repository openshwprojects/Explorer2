#pragma once
// IpcD356.h — IPC-D-356 / IPC-D-356A Bare-Board PCB Testpoint & Netlist (.d356 / .ipc) Parser

namespace Formats {

inline bool ParseIpcD356(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.size() < 24 || sv.size() > 64 * 1024 * 1024) return false;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }

    // IPC-D-356 P  UNITS CUST:
    //   CUST 0 = inches / 0.1 mil = 0.00254 mm per integer unit (default per IPC-D-356 spec)
    //   CUST 1 = millimeters / 1 um = 0.001 mm per integer unit
    //   CUST 2 = inches / 1 mil = 0.0254 mm per integer unit
    float unitToMm = 0.00254f;
    std::string jobName;
    bool sawHeaderOrEnd = false;

    struct NetPt { float x = 0.0f; float y = 0.0f; int netId = 0; };
    std::map<std::string, std::vector<NetPt>> netGroups;
    std::unordered_map<std::string, int> netIdMap;

    struct CompAccum {
        float sumX = 0.0f, sumY = 0.0f;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        int pins = 0;
        bool bottom = false;
    };
    std::map<std::string, CompAccum> comps;

    auto trimField = [](std::string_view f) -> std::string {
        while (!f.empty() && (f.front() == ' ' || f.front() == '\t')) f.remove_prefix(1);
        while (!f.empty() && (f.back() == ' ' || f.back() == '\t' || f.back() == '\r')) f.remove_suffix(1);
        return std::string(f);
    };
    auto parseSignedDigits = [](std::string_view s, int& outVal) -> bool {
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
        while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
        if (s.empty()) return false;
        char b[32] = {};
        std::memcpy(b, s.data(), std::min<size_t>(s.size(), 31));
        char* endp = nullptr;
        long v = std::strtol(b, &endp, 10);
        if (endp == b) return false;
        outVal = static_cast<int>(v);
        return true;
    };

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln.rfind("C ", 0) == 0 || ln.rfind("C\t", 0) == 0) {
            sawHeaderOrEnd = true;
            continue;
        }
        if (ln.rfind("999", 0) == 0) {
            sawHeaderOrEnd = true;
            break;
        }
        if (ln.rfind("P ", 0) == 0 || ln.rfind("P  ", 0) == 0) {
            sawHeaderOrEnd = true;
            if (ln.find("UNITS") != std::string_view::npos) {
                if (ln.find("CUST 1") != std::string_view::npos || ln.find("SI") != std::string_view::npos || ln.find("MM") != std::string_view::npos) {
                    unitToMm = 0.001f;
                } else if (ln.find("CUST 2") != std::string_view::npos) {
                    unitToMm = 0.0254f;
                } else {
                    unitToMm = 0.00254f; // CUST 0 = 0.1 mil
                }
            } else if (ln.find("JOB") != std::string_view::npos) {
                size_t jp = ln.find("JOB") + 3;
                jobName = trimField(ln.substr(jp));
            }
            continue;
        }

        // IPC-D-356 fixed-column test records:
        //   317 = Through-hole feature
        //   327 = SMT pad feature
        //   307 = Blind/buried via feature
        //   367 = Non-plated tooling hole
        if (ln.size() < 42) continue;
        bool is317 = (ln.rfind("317", 0) == 0);
        bool is327 = (ln.rfind("327", 0) == 0);
        bool is307 = (ln.rfind("307", 0) == 0);
        bool is367 = (ln.rfind("367", 0) == 0);
        if (!is317 && !is327 && !is307 && !is367) continue;

        // Cols 4..17 (0-indexed 3..16): Net name (14 chars)
        std::string netName = trimField(ln.substr(3, std::min<size_t>(14, ln.size() - 3)));
        // Cols 21..26 (0-indexed 20..25): RefDes (6 chars), Cols 27..31 (0-indexed 26..30): -Pin (5 chars)
        std::string refDes = (ln.size() >= 26) ? trimField(ln.substr(20, 6)) : "";
        std::string pinId  = (ln.size() >= 31) ? trimField(ln.substr(26, 5)) : "";
        if (!pinId.empty() && pinId.front() == '-') pinId.erase(pinId.begin());

        // Scan tail from column 32 onwards for Dxxxx, Axx, X[+-]xxxxxx, Y[+-]xxxxxx, Xxxxx, Yxxxx, Rxxx
        std::string_view tail = ln.substr(std::min<size_t>(31, ln.size()));
        float drillMm = 0.0f;
        int accessLayer = is317 ? 0 : 1; // A00=both/THT, A01=top, A02=bottom
        bool hasCoordX = false, hasCoordY = false;
        float xMm = 0.0f, yMm = 0.0f;
        float padWMm = 0.0f, padHMm = 0.0f;
        float rotDeg = 0.0f;

        size_t tp = 0;
        while (tp < tail.size()) {
            char c = tail[tp];
            if (c == 'D' && tp + 5 <= tail.size()) {
                int dv = 0;
                if (parseSignedDigits(tail.substr(tp + 1, 4), dv) && dv > 0) {
                    drillMm = static_cast<float>(dv) * unitToMm;
                    tp += 5;
                    continue;
                }
            } else if (c == 'A' && tp + 3 <= tail.size() && std::isdigit(static_cast<unsigned char>(tail[tp + 1]))) {
                int av = 0;
                if (parseSignedDigits(tail.substr(tp + 1, 2), av)) {
                    accessLayer = av;
                    tp += 3;
                    continue;
                }
            } else if (c == 'X' && tp + 2 < tail.size()) {
                if ((tail[tp + 1] == '+' || tail[tp + 1] == '-' || tail[tp + 1] == ' ') && !hasCoordX && tp + 8 <= tail.size()) {
                    int xv = 0;
                    if (parseSignedDigits(tail.substr(tp + 1, 7), xv)) {
                        xMm = static_cast<float>(xv) * unitToMm;
                        hasCoordX = true;
                        tp += 8;
                        continue;
                    }
                } else if (hasCoordX && tp + 5 <= tail.size() && std::isdigit(static_cast<unsigned char>(tail[tp + 1]))) {
                    int wv = 0;
                    if (parseSignedDigits(tail.substr(tp + 1, 4), wv) && wv > 0) {
                        padWMm = static_cast<float>(wv) * unitToMm;
                        tp += 5;
                        continue;
                    }
                }
            } else if (c == 'Y' && tp + 2 < tail.size()) {
                if ((tail[tp + 1] == '+' || tail[tp + 1] == '-' || tail[tp + 1] == ' ') && !hasCoordY && tp + 8 <= tail.size()) {
                    int yv = 0;
                    if (parseSignedDigits(tail.substr(tp + 1, 7), yv)) {
                        yMm = static_cast<float>(yv) * unitToMm;
                        hasCoordY = true;
                        tp += 8;
                        continue;
                    }
                } else if (hasCoordY && tp + 5 <= tail.size() && std::isdigit(static_cast<unsigned char>(tail[tp + 1]))) {
                    int hv = 0;
                    if (parseSignedDigits(tail.substr(tp + 1, 4), hv) && hv > 0) {
                        padHMm = static_cast<float>(hv) * unitToMm;
                        tp += 5;
                        continue;
                    }
                }
            } else if (c == 'R' && hasCoordX && tp + 4 <= tail.size() && std::isdigit(static_cast<unsigned char>(tail[tp + 1]))) {
                int rv = 0;
                if (parseSignedDigits(tail.substr(tp + 1, 3), rv)) {
                    rotDeg = static_cast<float>(rv % 360);
                    tp += 4;
                    continue;
                }
            }
            ++tp;
        }

        if (!hasCoordX && !hasCoordY) continue;

        int nid = 0;
        if (!netName.empty() && netName != "N/C") {
            auto it = netIdMap.find(netName);
            if (it == netIdMap.end()) {
                nid = static_cast<int>(netIdMap.size() + 1);
                netIdMap[netName] = nid;
            } else {
                nid = it->second;
            }
            netGroups[netName].push_back({ xMm, yMm, nid });
        }

        if (is367 || (refDes == "VIA" || is307)) {
            float d = (drillMm > 0.05f) ? drillMm : 0.35f;
            float od = (padWMm > d) ? padWMm : (d * 1.7f);
            out.vias.push_back({ xMm, yMm, od, d, nid });
            continue;
        }

        if (padWMm <= 0.05f) padWMm = (drillMm > 0.05f) ? (drillMm * 1.6f) : 1.1f;
        if (padHMm <= 0.05f) padHMm = padWMm;

        EdaPadPrim pad{};
        pad.x = xMm;
        pad.y = yMm;
        pad.w = std::clamp(padWMm, 0.15f, 25.0f);
        pad.h = std::clamp(padHMm, 0.15f, 25.0f);
        pad.rotDeg = rotDeg;
        pad.drill = is317 ? std::max(0.15f, drillMm > 0.0f ? drillMm : pad.w * 0.5f) : 0.0f;
        pad.shape = (is317 && std::fabs(pad.w - pad.h) < 0.05f) ? 0 : 1;
        pad.layer = (is317 || accessLayer == 0) ? EdaLayerId::PadsThrough
                  : ((accessLayer == 2) ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper);
        pad.pinName = PathUtils::Utf8ToWide(pinId);
        pad.netName = PathUtils::Utf8ToWide(netName);
        out.pads.push_back(std::move(pad));

        if (!refDes.empty() && refDes != "VIA" && refDes != "NOREF") {
            auto& ca = comps[refDes];
            ca.sumX += xMm; ca.sumY += yMm; ca.pins++;
            ca.minX = std::min(ca.minX, xMm - pad.w * 0.5f);
            ca.maxX = std::max(ca.maxX, xMm + pad.w * 0.5f);
            ca.minY = std::min(ca.minY, yMm - pad.h * 0.5f);
            ca.maxY = std::max(ca.maxY, yMm + pad.h * 0.5f);
            if (accessLayer == 2) ca.bottom = true;
        }
    }

    if (out.pads.empty() && out.vias.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    (void)sawHeaderOrEnd;

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"IPC-D-356 Bare-Board Test Netlist (.d356";
    if (!jobName.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(jobName);
    out.formatTitle += L")";

    // Reconstruct placed component outlines from RefDes pad clusters
    for (const auto& kv : comps) {
        if (kv.second.pins <= 0 || out.components.size() >= 4000) continue;
        EdaComponent3D c;
        c.refDes = PathUtils::Utf8ToWide(kv.first);
        c.x = kv.second.sumX / static_cast<float>(kv.second.pins);
        c.y = kv.second.sumY / static_cast<float>(kv.second.pins);
        c.widthMm = std::max(1.2f, kv.second.maxX - kv.second.minX);
        c.lengthMm = std::max(1.0f, kv.second.maxY - kv.second.minY);
        c.pinCount = kv.second.pins;
        c.bottomSide = kv.second.bottom;
        c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
        out.labels.push_back({ c.x, c.y, 0.85f, c.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, c.refDes });
        out.components.push_back(std::move(c));
    }

    // Ratsnest / airwire connectivity between pads sharing the same net
    for (const auto& kv : netGroups) {
        const auto& pts = kv.second;
        for (size_t i = 0; i + 1 < pts.size() && out.traces.size() < 80000; ++i) {
            out.traces.push_back({ pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, 0.14f, EdaLayerId::TopCopper, pts[i].netId });
        }
    }

    out.netCount = std::max<size_t>(1, netIdMap.size());
    return true;
}

} // namespace Formats
