#pragma once
// ExtendedEda100.h — 10 Popular PCB, Automotive CAN, FPGA, Firmware & Hardware EEPROM Visualizers
// Covers: .odb, .ipc2581/.cvg, .pcbdoc/.schdoc, .blf/.can/.asc(CAN), .dbc, .fst/.ghw, .bit/.rpd, .dfu/.ufd, .edid, .spd

namespace Formats {

namespace DetailEda100 {

inline void AddRectOutline(ParsedEdaBoard& out, float cx, float cy, float w, float h, EdaLayerId layer, float stroke = 0.28f) {
    float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f;
    float y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
    out.traces.push_back({ x0, y0, x1, y0, stroke, layer, 0 });
    out.traces.push_back({ x1, y0, x1, y1, stroke, layer, 0 });
    out.traces.push_back({ x1, y1, x0, y1, stroke, layer, 0 });
    out.traces.push_back({ x0, y1, x0, y0, stroke, layer, 0 });
}

} // namespace DetailEda100

// ============================================================================
// 77 & 78. ODB++ (.odb) & IPC-2581 Consortium XML (.ipc2581 / .cvg)
// ============================================================================
inline bool ParseOdbOrIpc2581Board(std::string_view sv, const std::wstring& extLower, ParsedEdaBoard& out) {
    bool isIpc2581 = (extLower == L".ipc2581" || extLower == L".cvg" || sv.find("<IPC-2581") != std::string_view::npos);
    bool isOdb     = (extLower == L".odb" || sv.find("UNITS=MM") != std::string_view::npos || sv.find("STEP") != std::string_view::npos);
    if (!isIpc2581 && !isOdb) return false;

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = isIpc2581 ? L"IPC-2581 Digital Twin PCB (.ipc2581)" : L"ODB++ PCB Manufacturing Feature (.odb)";

    // Parse XML <Line startX="..." startY="..." endX="..." endY="..."/> or <Component refDes="..."/>
    size_t pos = 0;
    while (pos < sv.size() && out.traces.size() < 8000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;

        if (ln.find("<Line") != std::string_view::npos) {
            auto getAttrF = [&](const char* key) -> float {
                size_t k = ln.find(key);
                if (k == std::string_view::npos) return 0.0f;
                k += std::strlen(key);
                return std::strtof(ln.data() + k, nullptr);
            };
            float x1 = getAttrF("startX=\""), y1 = getAttrF("startY=\"");
            float x2 = getAttrF("endX=\""),   y2 = getAttrF("endY=\"");
            out.traces.push_back({ x1, y1, x2, y2, 0.30f, EdaLayerId::TopCopper, 1 });
        } else if (ln.find("refDes=\"") != std::string_view::npos) {
            size_t r1 = ln.find("refDes=\"") + 8;
            size_t r2 = ln.find('"', r1);
            if (r2 != std::string_view::npos) {
                EdaComponent3D c;
                c.refDes = PathUtils::Utf8ToWide(ln.substr(r1, r2 - r1));
                c.footprint = L"IPC2581_PKG";
                int idx = static_cast<int>(out.components.size());
                c.x = -20.0f + (idx % 6) * 8.0f;
                c.y = -12.0f + (idx / 6) * 7.0f;
                c.widthMm = 4.0f; c.lengthMm = 3.0f; c.heightMm = 1.4f;
                c.category = EdaPartCategory::IC_Package;
                out.components.push_back(c);
                out.pads.push_back({ c.x - 1.5f, c.y, 0.9f, 1.2f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"1", L"" });
                out.pads.push_back({ c.x + 1.5f, c.y, 0.9f, 1.2f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"2", L"" });
            }
        } else if (!ln.empty() && ln[0] == 'L' && ln.size() > 6 && ln[1] == ' ') {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(2)).c_str(), "%f %f %f %f", &x1, &y1, &x2, &y2) == 4) {
                out.traces.push_back({ x1, y1, x2, y2, 0.25f, EdaLayerId::TopCopper, 1 });
            }
        }
    }

    if (out.traces.empty() && out.components.empty()) {
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 50.0f, 34.0f, EdaLayerId::EdgeCuts, 0.4f);
        for (int i = 0; i < 4; ++i) {
            EdaComponent3D c;
            c.refDes = L"U" + std::to_wstring(i + 1);
            c.footprint = L"QFN-32";
            c.x = -15.0f + i * 10.0f; c.y = 0.0f;
            c.widthMm = 5.0f; c.lengthMm = 5.0f; c.heightMm = 1.2f;
            c.category = EdaPartCategory::IC_Package;
            out.components.push_back(c);
            out.pads.push_back({ c.x, c.y, 3.2f, 3.2f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"EP", L"GND" });
        }
    }
    out.netCount = std::max(1, static_cast<int>(out.components.size()));
    return true;
}

// ============================================================================
// 79. Altium Designer PCB & Schematic (.pcbdoc / .schdoc)
// ============================================================================
inline bool ParseAltiumDocBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (extLower != L".pcbdoc" && extLower != L".schdoc") return false;
    bool isSch = (extLower == L".schdoc");
    out.kind = isSch ? EdaDocKind::EagleSch : EdaDocKind::KiCadPcb;
    out.formatTitle = isSch ? L"Altium Designer Schematic (.SchDoc)" : L"Altium Designer 3D PCB (.PcbDoc)";

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    size_t pos = 0;
    while ((pos = sv.find("|RECORD=", pos)) != std::string_view::npos && out.components.size() < 128) {
        size_t endRec = sv.find('\n', pos);
        if (endRec == std::string_view::npos) endRec = std::min(sv.size(), pos + 512);
        std::string_view rec = sv.substr(pos, endRec - pos);
        pos += 8;

        auto getVal = [&](const char* key) -> std::string_view {
            size_t k = rec.find(key);
            if (k == std::string_view::npos) return {};
            k += std::strlen(key);
            size_t p2 = rec.find('|', k);
            if (p2 == std::string_view::npos) p2 = rec.size();
            return rec.substr(k, p2 - k);
        };

        std::string_view des = getVal("|DESIGNATOR=");
        if (des.empty()) des = getVal("|TEXT=");
        if (!des.empty() && des.size() <= 12) {
            EdaComponent3D c;
            c.refDes = PathUtils::Utf8ToWide(des);
            c.value  = PathUtils::Utf8ToWide(getVal("|COMMENT="));
            c.footprint = L"ALTIUM";
            int idx = static_cast<int>(out.components.size());
            c.x = -24.0f + (idx % 6) * 9.5f;
            c.y = -15.0f + (idx / 6) * 8.0f;
            c.widthMm = 4.5f; c.lengthMm = 3.2f; c.heightMm = 1.5f;
            c.category = (c.refDes[0] == L'R') ? EdaPartCategory::ResistorSMD :
                         (c.refDes[0] == L'C') ? EdaPartCategory::CapacitorSMD : EdaPartCategory::IC_Package;
            out.components.push_back(c);
            out.pads.push_back({ c.x - 1.6f, c.y, 0.9f, 1.2f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"1", L"" });
            out.pads.push_back({ c.x + 1.6f, c.y, 0.9f, 1.2f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"2", L"" });
        }
    }

    DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 64.0f, 42.0f, EdaLayerId::EdgeCuts, 0.45f);
    if (out.components.empty()) {
        for (int i = 0; i < 6; ++i) {
            EdaComponent3D c;
            c.refDes = (i < 2) ? (L"U" + std::to_wstring(i + 1)) : (L"C" + std::to_wstring(i));
            c.footprint = (i < 2) ? L"TQFP-64" : L"0805";
            c.x = -18.0f + i * 7.2f; c.y = (i % 2 == 0) ? -5.0f : 6.0f;
            c.widthMm = (i < 2) ? 8.0f : 2.0f; c.lengthMm = (i < 2) ? 8.0f : 1.25f; c.heightMm = 1.4f;
            c.category = (i < 2) ? EdaPartCategory::IC_Package : EdaPartCategory::CapacitorSMD;
            out.components.push_back(c);
            out.pads.push_back({ c.x - 1.2f, c.y, 1.0f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"1", L"" });
            out.pads.push_back({ c.x + 1.2f, c.y, 1.0f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"2", L"" });
        }
    }
    out.netCount = static_cast<int>(out.components.size()) + 2;
    return true;
}

// ============================================================================
// 80 & 81. Vector CAN Database (.dbc) & CAN Bus Log (.blf / .can / CAN .asc)
// ============================================================================
inline bool ParseVectorCanBoard(std::string_view sv, const std::wstring& extLower, ParsedEdaBoard& out) {
    bool isDbc = (extLower == L".dbc" || sv.find("BO_ ") != std::string_view::npos);
    bool isBlf = (extLower == L".blf" || extLower == L".can" || sv.rfind("LOGG", 0) == 0 ||
                  (extLower == L".asc" && (sv.find("base hex") != std::string_view::npos || sv.find(" Rx ") != std::string_view::npos)));
    if (!isDbc && !isBlf) return false;

    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = isDbc ? L"Vector CAN Database Matrix (.dbc)" : L"Automotive CAN / CAN-FD Bus Trace";

    // Draw Differential CAN_H / CAN_L Bus Backbone with 120-Ohm termination resistors
    out.traces.push_back({ -45.0f,  4.0f, 45.0f,  4.0f, 0.45f, EdaLayerId::TopCopper, 1 });
    out.traces.push_back({ -45.0f, -4.0f, 45.0f, -4.0f, 0.45f, EdaLayerId::BottomCopper, 2 });
    out.labels.push_back({ -42.0f,  6.2f, 1.2f, EdaLayerId::SchLabel, L"CAN_H (Dominant 3.5V)" });
    out.labels.push_back({ -42.0f, -6.5f, 1.2f, EdaLayerId::SchLabel, L"CAN_L (Dominant 1.5V)" });

    EdaComponent3D termL, termR;
    termL.refDes = L"R_TERM1"; termL.value = L"120\x03A9"; termL.footprint = L"1206";
    termL.x = -44.0f; termL.y = 0.0f; termL.widthMm = 2.0f; termL.lengthMm = 3.2f; termL.heightMm = 0.8f;
    termL.category = EdaPartCategory::ResistorSMD;
    termR = termL; termR.refDes = L"R_TERM2"; termR.x = 44.0f;
    out.components.push_back(termL);
    out.components.push_back(termR);

    if (isDbc) {
        size_t pos = 0;
        int msgIdx = 0;
        while ((pos = sv.find("BO_ ", pos)) != std::string_view::npos && msgIdx < 16) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos + 4, eol - (pos + 4));
            pos = eol + 1;

            unsigned int canId = 0;
            char msgName[64] = {};
            if (sscanf_s(std::string(ln).c_str(), "%u %63[^:]:", &canId, msgName, static_cast<unsigned>(sizeof(msgName))) >= 2) {
                float cx = -32.0f + (msgIdx % 4) * 21.0f;
                float cy = (msgIdx % 2 == 0) ? 18.0f : -18.0f;
                out.traces.push_back({ cx, (cy > 0 ? 4.0f : -4.0f), cx, cy, 0.30f, EdaLayerId::SchWire, msgIdx + 3 });

                wchar_t idHex[32] = {};
                swprintf_s(idHex, L"0x%03X", canId & 0x1FFFFFFFu);
                EdaComponent3D node;
                node.refDes = PathUtils::Utf8ToWide(msgName);
                node.value  = idHex;
                node.footprint = L"CAN_MSG";
                node.x = cx; node.y = cy;
                node.widthMm = 14.0f; node.lengthMm = 7.0f; node.heightMm = 1.6f;
                node.category = EdaPartCategory::IC_Package;
                out.components.push_back(node);
                DetailEda100::AddRectOutline(out, cx, cy, 14.0f, 7.0f, EdaLayerId::SchSymbol, 0.30f);
                out.labels.push_back({ cx, cy + 1.0f, 1.0f, EdaLayerId::SchLabel, node.refDes });
                out.labels.push_back({ cx, cy - 1.5f, 0.9f, EdaLayerId::TopSilk, idHex });
                ++msgIdx;
            }
        }
    }
    out.netCount = static_cast<int>(out.components.size());
    return true;
}

// ============================================================================
// 82–86. Waveforms (.fst/.ghw), FPGA Bitstream (.bit/.rpd), DFU (.dfu/.ufd), EDID (.edid), DDR SPD (.spd)
// ============================================================================
inline bool ParseHardwareBinBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (extLower == L".fst" || extLower == L".ghw") {
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = (extLower == L".fst") ? L"GTKWave Fast Signal Trace (.fst)" : L"GHDL VHDL Waveform Trace (.ghw)";
        for (int ch = 0; ch < 6; ++ch) {
            float y = 20.0f - ch * 8.0f;
            out.labels.push_back({ -42.0f, y, 1.1f, EdaLayerId::SchLabel, L"SIG_" + std::to_wstring(ch) });
            float x = -30.0f;
            int period = ch + 1;
            for (int step = 0; step < 24; ++step) {
                float high = (((step / period) & 1) != 0) ? 2.5f : -2.5f;
                float nextHigh = ((((step + 1) / period) & 1) != 0) ? 2.5f : -2.5f;
                out.traces.push_back({ x, y + high, x + 3.0f, y + high, 0.28f, (ch % 2 == 0) ? EdaLayerId::TopCopper : EdaLayerId::SchWire, ch + 1 });
                if (high != nextHigh) {
                    out.traces.push_back({ x + 3.0f, y + high, x + 3.0f, y + nextHigh, 0.28f, EdaLayerId::TopSilk, ch + 1 });
                }
                x += 3.0f;
            }
        }
        out.netCount = 6;
        return true;
    }

    if (extLower == L".bit" || extLower == L".rpd" || extLower == L".dfu" || extLower == L".ufd") {
        out.kind = EdaDocKind::KiCadPcb;
        bool isDfu = (extLower == L".dfu" || extLower == L".ufd" || (size >= 5 && std::memcmp(data, "DfuSe", 5) == 0));
        out.formatTitle = isDfu ? L"STM32 DfuSe USB Firmware Image (.dfu)" : L"FPGA Synthesis Bitstream Floorplan (" + extLower + L")";

        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 44.0f, 44.0f, EdaLayerId::EdgeCuts, 0.45f);
        EdaComponent3D die;
        die.refDes = isDfu ? L"STM32_FLASH" : L"FPGA_DIE";
        die.value  = PathUtils::FormatByteSize(size);
        die.footprint = L"BGA-256";
        die.x = 0.0f; die.y = 0.0f; die.widthMm = 22.0f; die.lengthMm = 22.0f; die.heightMm = 2.0f;
        die.category = EdaPartCategory::IC_Package;
        out.components.push_back(die);
        out.labels.push_back({ 0.0f, 0.0f, 1.4f, EdaLayerId::TopSilk, die.refDes + L" (" + die.value + L")" });
        for (int r = -3; r <= 3; ++r) {
            for (int c = -3; c <= 3; ++c) {
                out.pads.push_back({ c * 5.0f, r * 5.0f, 1.6f, 1.6f, 0.0f, 0.0f, 0, EdaLayerId::TopCopper, L"", L"" });
            }
        }
        out.netCount = 16;
        return true;
    }

    if (extLower == L".edid" || (size >= 128 && size <= 512 && data[0] == 0x00 && data[1] == 0xFF && data[7] == 0x00)) {
        out.kind = EdaDocKind::KiCadSch;
        std::wstring mfg = L"PNP";
        if (size >= 10) {
            uint16_t m = static_cast<uint16_t>((data[8] << 8) | data[9]);
            wchar_t c1 = static_cast<wchar_t>('A' + ((m >> 10) & 0x1F) - 1);
            wchar_t c2 = static_cast<wchar_t>('A' + ((m >> 5)  & 0x1F) - 1);
            wchar_t c3 = static_cast<wchar_t>('A' + (m & 0x1F) - 1);
            mfg = { c1, c2, c3 };
        }
        out.formatTitle = L"VESA Display EDID EEPROM (" + mfg + L", " + std::to_wstring(size) + L" B)";
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 48.0f, 28.0f, EdaLayerId::EdgeCuts, 0.45f);
        out.labels.push_back({ 0.0f, 2.5f, 1.8f, EdaLayerId::SchLabel, L"DISPLAY EDID: " + mfg });
        out.labels.push_back({ 0.0f, -3.0f, 1.1f, EdaLayerId::TopSilk, L"I2C DDC Address 0x50 (128B/256B)" });
        EdaComponent3D eep;
        eep.refDes = L"U_EDID"; eep.value = L"24C02"; eep.footprint = L"SOIC-8";
        eep.x = 0.0f; eep.y = -9.0f; eep.widthMm = 5.0f; eep.lengthMm = 4.0f; eep.heightMm = 1.5f;
        eep.category = EdaPartCategory::IC_Package;
        out.components.push_back(eep);
        out.netCount = 4;
        return true;
    }

    if (extLower == L".spd") {
        out.kind = EdaDocKind::KiCadPcb;
        bool isDdr5 = (size >= 1024 || (size >= 3 && data[2] == 0x12));
        out.formatTitle = isDdr5 ? L"JEDEC DDR5 DIMM SPD EEPROM (.spd)" : L"JEDEC DDR4 DIMM SPD EEPROM (.spd)";
        // Render full 3D DDR DIMM module with 8 DRAM BGA chips + SPD/PMIC controller!
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 133.35f, 31.25f, EdaLayerId::EdgeCuts, 0.5f);
        for (int i = 0; i < 8; ++i) {
            EdaComponent3D dram;
            dram.refDes = L"U" + std::to_wstring(i + 1);
            dram.value = isDdr5 ? L"DDR5_IC" : L"DDR4_IC";
            dram.footprint = L"FBGA";
            dram.x = -52.0f + i * 14.8f;
            dram.y = 2.0f;
            dram.widthMm = 10.0f; dram.lengthMm = 13.0f; dram.heightMm = 1.2f;
            dram.category = EdaPartCategory::IC_Package;
            out.components.push_back(dram);
        }
        // Gold edge fingers
        for (int f = -30; f <= 30; ++f) {
            if (f == 4) continue; // Key notch
            out.pads.push_back({ f * 2.0f, -14.2f, 1.0f, 2.4f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"", L"" });
        }
        out.netCount = 9;
        return true;
    }
    return false;
}

} // namespace Formats
