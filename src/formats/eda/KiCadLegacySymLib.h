#pragma once
// KiCadLegacySymLib.h — KiCad 4/5 Legacy EESchema Symbol Library (.lib) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool IsLikelyKiCadLegacySymLibBytes(const uint8_t* data, size_t size) {
    if (!data || size < 20) return false;
    size_t scan = std::min<size_t>(size, 1024);
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == 0) return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    sv = DetailKiCadLegacy::StripUtf8BomView(sv);
    return sv.rfind("EESchema-LIBRARY", 0) == 0;
}

inline bool ParseKiCadLegacySymLibBoard(std::string_view txt, ParsedEdaBoard& out) {
    using namespace DetailKiCadLegacy;
    std::unordered_map<std::string, LegSymDef> symMap;
    std::vector<std::string> order;
    if (!ParseLegacySymLibraryMap(txt, symMap, &order) || order.empty()) {
        return false;
    }

    out.kind = EdaDocKind::KiCadSch;
    const size_t cols = std::max<size_t>(1u, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(order.size())))));
    float curXMil = 0.0f, curYMil = 0.0f;
    size_t rendered = 0;

    for (size_t i = 0; i < order.size() && rendered < 128; ++i) {
        const auto& sym = symMap[order[i]];
        if (sym.segs.empty() && sym.circs.empty() && sym.pins.empty()) continue;
        if (rendered > 0 && (rendered % cols) == 0) {
            curXMil = 0.0f;
            curYMil += 800.0f; // +Y down in sheet mils => moves row downward on screen
        }
        StampLegacySymInstance(
            &sym, curXMil, curYMil,
            1, 0, 0, -1,
            1, 1,
            PathUtils::Utf8ToWide(sym.name),
            PathUtils::Utf8ToWide(sym.refPrefix),
            L"EESCHEMA_LIB",
            true, out);
        curXMil += 900.0f;
        ++rendered;
    }

    if (rendered == 0 || (out.traces.empty() && out.pads.empty() && out.circles.empty())) {
        out = ParsedEdaBoard{};
        return false;
    }
    out.formatTitle = L"KiCad Legacy Symbol Library (.lib — " + std::to_wstring(rendered) + L" symbols)";
    out.netCount = std::max<size_t>(1u, out.pads.empty() ? rendered : out.pads.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}
