#pragma once
// SpecctraDsn.h — Specctra Autorouter PCB Design (.dsn) Parser

namespace Formats {

inline bool ParseSpecctraDsn(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("(pcb") == std::string_view::npos && sv.find("(wiring") == std::string_view::npos &&
        sv.find("(structure") == std::string_view::npos) {
        return false;
    }
    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"Specctra Autorouter PCB (.dsn)";

    size_t pos = 0;
    while ((pos = sv.find("(path ", pos)) != std::string_view::npos) {
        size_t closeP = sv.find(')', pos);
        if (closeP == std::string_view::npos) break;
        std::string_view blk = sv.substr(pos + 6, closeP - (pos + 6));
        bool isBoundary = (pos >= 32 && sv.substr(pos - 32, 32).find("boundary") != std::string_view::npos);
        EdaLayerId lid = isBoundary ? EdaLayerId::EdgeCuts : EdaLayerId::TopCopper;
        if (blk.find("B.Cu") != std::string_view::npos || blk.find("Bottom") != std::string_view::npos) {
            lid = EdaLayerId::BottomCopper;
        }

        const char* cur = blk.data();
        const char* end = blk.data() + blk.size();
        // Skip layer token
        while (cur < end && (*cur == ' ' || *cur == '\t')) ++cur;
        while (cur < end && *cur > ' ') ++cur;
        char* next = nullptr;
        float w = std::strtof(cur, &next);
        if (next != cur) {
            cur = next;
            if (w > 10.0f) w *= 0.001f; // mil/um to mm scaling
            w = std::clamp(w, 0.15f, 2.5f);
            std::vector<std::pair<float, float>> pts;
            while (cur < end) {
                float x = std::strtof(cur, &next); if (next == cur) break; cur = next;
                float y = std::strtof(cur, &next); if (next == cur) break; cur = next;
                if (std::abs(x) > 500.0f || std::abs(y) > 500.0f) { x *= 0.01f; y *= 0.01f; }
                pts.push_back({ x, -y });
            }
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                out.traces.push_back({ pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second, w, lid, 0 });
            }
        }
        pos = closeP + 1;
    }

    pos = 0;
    while ((pos = sv.find("(place ", pos)) != std::string_view::npos) {
        size_t closeP = sv.find(')', pos);
        if (closeP == std::string_view::npos) break;
        std::string s(sv.substr(pos + 7, closeP - (pos + 7)));
        char refBuf[64] = {};
        float px = 0.0f, py = 0.0f;
        if (sscanf_s(s.c_str(), "%63s %f %f", refBuf, (unsigned)_countof(refBuf), &px, &py) >= 3) {
            if (std::abs(px) > 500.0f || std::abs(py) > 500.0f) { px *= 0.01f; py *= 0.01f; }
            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(refBuf);
            comp.x = px;
            comp.y = -py;
            comp.widthMm = 3.2f;
            comp.lengthMm = 2.0f;
            comp.heightMm = 1.2f;
            out.pads.push_back({ px - 1.0f, -py, 1.0f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"1", comp.refDes });
            out.pads.push_back({ px + 1.0f, -py, 1.0f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"2", comp.refDes });
            out.labels.push_back({ px, -py, 1.0f, EdaLayerId::TopSilk, comp.refDes });
            out.components.push_back(std::move(comp));
        }
        pos = closeP + 1;
    }

    return (!out.traces.empty() || !out.pads.empty() || !out.components.empty());
}

} // namespace Formats
