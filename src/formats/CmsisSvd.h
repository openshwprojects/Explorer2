#pragma once
// CmsisSvd.h — ARM CMSIS System View Description (.svd) MCU Peripheral & Register Map Diagram Parser

namespace Formats {

inline bool ParseCmsisSvdBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("<device") == std::string_view::npos && sv.find("<peripheral") == std::string_view::npos) return false;
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"ARM CMSIS-SVD MCU Architecture (.svd)";

    auto getTag = [](std::string_view blk, const char* openT, const char* closeT) -> std::wstring {
        size_t p1 = blk.find(openT);
        if (p1 == std::string_view::npos) return L"";
        p1 += std::strlen(openT);
        size_t p2 = blk.find(closeT, p1);
        if (p2 == std::string_view::npos) return L"";
        return PathUtils::Utf8ToWide(blk.substr(p1, p2 - p1));
    };

    // Draw Central System Bus spine
    out.traces.push_back({ 0.0f, 10.0f, 0.0f, -120.0f, 0.6f, EdaLayerId::SchWire, 1 });
    out.labels.push_back({ 0.0f, 14.0f, 1.5f, EdaLayerId::SchLabel, L"AHB/APB System Bus" });

    size_t pos = 0;
    int idx = 0;
    while (pos < sv.size() && idx < 32) {
        size_t p1 = sv.find("<peripheral>", pos);
        if (p1 == std::string_view::npos) break;
        size_t p2 = sv.find("</peripheral>", p1);
        if (p2 == std::string_view::npos) break;
        std::string_view blk = sv.substr(p1, p2 - p1);
        pos = p2 + 13;

        std::wstring pname = getTag(blk, "<name>", "</name>");
        std::wstring baddr = getTag(blk, "<baseAddress>", "</baseAddress>");
        if (pname.empty()) continue;

        bool rightSide = (idx % 2 == 0);
        float cy = -static_cast<float>((idx / 2) * 14.0f);
        float bx = rightSide ? 18.0f : -18.0f;

        // Bus tap wire
        out.traces.push_back({ 0.0f, cy, rightSide ? 8.0f : -8.0f, cy, 0.30f, EdaLayerId::SchWire, idx + 2 });

        // Peripheral block
        float w = 18.0f, h = 8.0f;
        out.traces.push_back({ bx - w * 0.5f, cy - h * 0.5f, bx + w * 0.5f, cy - h * 0.5f, 0.30f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bx + w * 0.5f, cy - h * 0.5f, bx + w * 0.5f, cy + h * 0.5f, 0.30f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bx + w * 0.5f, cy + h * 0.5f, bx - w * 0.5f, cy + h * 0.5f, 0.30f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bx - w * 0.5f, cy + h * 0.5f, bx - w * 0.5f, cy - h * 0.5f, 0.30f, EdaLayerId::SchSymbol, 0 });
        out.labels.push_back({ bx, cy + 1.2f, 1.1f, EdaLayerId::SchLabel, pname });
        if (!baddr.empty()) {
            out.labels.push_back({ bx, cy - 1.6f, 0.85f, EdaLayerId::TopSilk, baddr });
        }

        EdaComponent3D comp;
        comp.refDes = pname;
        comp.value = baddr;
        comp.footprint = L"SVD_PERIPH";
        comp.x = bx;
        comp.y = cy;
        comp.widthMm = w;
        comp.lengthMm = h;
        comp.heightMm = 1.2f;
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));
        ++idx;
    }
    out.netCount = idx + 1;
    return !out.components.empty();
}

} // namespace Formats
