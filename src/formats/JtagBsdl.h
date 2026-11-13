#pragma once
// JtagBsdl.h — IEEE 1149.1 JTAG Boundary-Scan Description Language (.bsdl) IC Pinout Parser

namespace Formats {

inline bool ParseJtagBsdlBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("entity") == std::string_view::npos && sv.find("BOUNDARY_LENGTH") == std::string_view::npos &&
        sv.find("PIN_MAP") == std::string_view::npos && sv.find("port") == std::string_view::npos) {
        return false;
    }
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"IEEE 1149.1 JTAG Boundary-Scan IC (.bsdl)";

    std::wstring entityName = L"JTAG_IC";
    size_t entPos = sv.find("entity ");
    if (entPos != std::string_view::npos) {
        char ename[64] = {};
        if (sscanf_s(std::string(sv.substr(entPos + 7, 64)).c_str(), "%63s", ename, (unsigned)_countof(ename)) == 1) {
            entityName = PathUtils::Utf8ToWide(ename);
        }
    }

    std::vector<std::wstring> pins;
    size_t pos = 0;
    while (pos < sv.size() && pins.size() < 128) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        size_t colon = ln.find(':');
        if (colon != std::string_view::npos &&
            (ln.find(" in ") != std::string_view::npos || ln.find(" out ") != std::string_view::npos ||
             ln.find(" inout ") != std::string_view::npos || ln.find(" linkage ") != std::string_view::npos ||
             ln.find(" bit") != std::string_view::npos))
        {
            std::string_view pname = ln.substr(0, colon);
            while (!pname.empty() && (pname.front() == ' ' || pname.front() == '\t')) pname.remove_prefix(1);
            while (!pname.empty() && (pname.back() == ' ' || pname.back() == '\t')) pname.remove_suffix(1);
            if (!pname.empty() && std::isalpha(static_cast<unsigned char>(pname[0]))) {
                pins.push_back(PathUtils::Utf8ToWide(pname));
            }
        }
    }
    if (pins.empty()) {
        pins = { L"TDI", L"TDO", L"TCK", L"TMS", L"TRST", L"VCC", L"GND", L"GPIO0" };
    }

    // Draw IC quad package body + boundary scan cell pins around perimeter
    float boxW = 32.0f;
    float boxH = std::max(24.0f, static_cast<float>((pins.size() / 2 + 2) * 4.0f));
    out.traces.push_back({ -boxW * 0.5f, -boxH * 0.5f,  boxW * 0.5f, -boxH * 0.5f, 0.35f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({  boxW * 0.5f, -boxH * 0.5f,  boxW * 0.5f,  boxH * 0.5f, 0.35f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({  boxW * 0.5f,  boxH * 0.5f, -boxW * 0.5f,  boxH * 0.5f, 0.35f, EdaLayerId::SchSymbol, 0 });
    out.traces.push_back({ -boxW * 0.5f,  boxH * 0.5f, -boxW * 0.5f, -boxH * 0.5f, 0.35f, EdaLayerId::SchSymbol, 0 });
    out.labels.push_back({ 0.0f, 0.0f, 1.6f, EdaLayerId::SchLabel, entityName });

    size_t half = (pins.size() + 1) / 2;
    for (size_t i = 0; i < pins.size(); ++i) {
        bool leftSide = (i < half);
        size_t idx = leftSide ? i : (i - half);
        float py = (boxH * 0.5f - 4.0f) - static_cast<float>(idx) * 4.0f;
        float x0 = leftSide ? (-boxW * 0.5f - 5.0f) : (boxW * 0.5f);
        float x1 = leftSide ? (-boxW * 0.5f) : (boxW * 0.5f + 5.0f);
        out.traces.push_back({ x0, py, x1, py, 0.25f, EdaLayerId::SchWire, static_cast<int>(i + 1) });
        EdaPadPrim pad;
        pad.x = (x0 + x1) * 0.5f;
        pad.y = py;
        pad.w = 1.2f;
        pad.h = 1.2f;
        pad.shape = 1;
        pad.layer = EdaLayerId::SchSymbol;
        pad.pinName = pins[i];
        out.pads.push_back(std::move(pad));
        out.labels.push_back({ leftSide ? (x1 + 4.5f) : (x0 - 4.5f), py, 0.9f, EdaLayerId::SchLabel, pins[i] });
    }

    EdaComponent3D comp;
    comp.refDes = entityName;
    comp.footprint = L"BSDL_QFP";
    comp.widthMm = boxW;
    comp.lengthMm = boxH;
    comp.heightMm = 1.6f;
    comp.pinCount = static_cast<int>(pins.size());
    comp.category = EdaPartCategory::IC_Package;
    out.components.push_back(std::move(comp));
    out.netCount = static_cast<int>(pins.size());
    return true;
}

} // namespace Formats
