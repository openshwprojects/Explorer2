#pragma once
// IpcD356.h — IPC-D-356 Bare-Board PCB Netlist & Testpoints (.d356 / .ipc) Parser

namespace Formats {

inline bool ParseIpcD356(std::string_view sv, ParsedEdaBoard& out) {
    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"IPC-D-356 PCB Testpoint Netlist (.d356)";

    struct NetPt { float x; float y; };
    std::unordered_map<std::string, std::vector<NetPt>> netGroups;

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.size() < 40) continue;

        // IPC-D-356 test records start with 317 (thru-hole) or 327 (SMD)
        if (ln.rfind("317", 0) == 0 || ln.rfind("327", 0) == 0) {
            bool isThru = (ln[1] == '1');
            std::string netName(ln.substr(3, std::min<size_t>(14, ln.size() - 3)));
            while (!netName.empty() && netName.back() == ' ') netName.pop_back();

            float x = 0.0f, y = 0.0f;
            size_t xPos = ln.find('X', 35);
            size_t yPos = ln.find('Y', 35);
            if (xPos != std::string_view::npos) x = std::strtof(std::string(ln.substr(xPos + 1, 8)).c_str(), nullptr) * 0.01f;
            if (yPos != std::string_view::npos) y = std::strtof(std::string(ln.substr(yPos + 1, 8)).c_str(), nullptr) * 0.01f;

            EdaPadPrim pad;
            pad.x = x;
            pad.y = -y;
            pad.w = 1.4f;
            pad.h = 1.4f;
            pad.drill = isThru ? 0.8f : 0.0f;
            pad.shape = isThru ? 0 : 1;
            pad.layer = isThru ? EdaLayerId::PadsThrough : EdaLayerId::TopCopper;
            pad.netName = PathUtils::Utf8ToWide(netName);
            out.pads.push_back(pad);

            if (!netName.empty()) {
                netGroups[netName].push_back({ x, -y });
            }
        }
    }

    for (const auto& kv : netGroups) {
        ++out.netCount;
        const auto& pts = kv.second;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            out.traces.push_back({ pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, 0.25f, EdaLayerId::TopCopper, 0 });
        }
    }

    return (!out.pads.empty() || !out.traces.empty());
}

} // namespace Formats
