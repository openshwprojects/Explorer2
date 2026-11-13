#pragma once
// TouchstoneSnp.h — Touchstone RF S-Parameter (.s1p / .s2p) Frequency Response Chart Parser

namespace Formats {

inline bool ParseTouchstoneSnpBoard(std::string_view sv, ParsedEdaBoard& out) {
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"Touchstone RF S-Parameter Network (.s1p / .s2p)";

    struct RfPt { float freq; float s11; float s21; bool hasS21; };
    std::vector<RfPt> pts;

    size_t pos = 0;
    while (pos < sv.size() && pts.size() < 4000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '!' || ln[0] == '#') continue;

        float v[6] = {};
        int n = sscanf_s(std::string(ln).c_str(), "%f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4]);
        if (n >= 3) {
            pts.push_back({ v[0], v[1], (n >= 5) ? v[3] : 0.0f, (n >= 5) });
        }
    }
    if (pts.size() < 2) return false;

    float minF = pts.front().freq, maxF = pts.back().freq;
    if (maxF <= minF) maxF = minF + 1.0f;

    // Draw RF chart frame
    const float w = 100.0f, h = 50.0f;
    out.traces.push_back({ 0, 0, w, 0, 0.25f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ w, 0, w, h, 0.25f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ w, h, 0, h, 0.25f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ 0, h, 0, 0, 0.25f, EdaLayerId::SchSymbol, 0 });
    out.labels.push_back({ 4.0f, h - 4.0f, 1.4f, EdaLayerId::SchLabel, L"S11 / S21 Magnitude vs Frequency" });

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        float x0 = ((pts[i].freq - minF) / (maxF - minF)) * w;
        float x1 = ((pts[i + 1].freq - minF) / (maxF - minF)) * w;
        float y0 = std::clamp(h + pts[i].s11 * 1.0f, 2.0f, h - 2.0f);
        float y1 = std::clamp(h + pts[i + 1].s11 * 1.0f, 2.0f, h - 2.0f);
        out.traces.push_back({ x0, y0, x1, y1, 0.35f, EdaLayerId::SchWire, 1 });
        if (pts[i].hasS21) {
            float y20 = std::clamp(h + pts[i].s21 * 1.0f, 2.0f, h - 2.0f);
            float y21 = std::clamp(h + pts[i + 1].s21 * 1.0f, 2.0f, h - 2.0f);
            out.traces.push_back({ x0, y20, x1, y21, 0.35f, EdaLayerId::TopCopper, 2 });
        }
    }
    out.netCount = pts.front().hasS21 ? 2 : 1;
    return !out.traces.empty();
}

} // namespace Formats
