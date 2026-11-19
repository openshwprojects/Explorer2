#pragma once
// Stm32Dfu.h — STM32 DfuSe & USB DFU Firmware Image (.dfu / .ufd) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailHwBin {

// STM32 DfuSe (.dfu) & USB DFU (.ufd) Firmware Image Parser
inline bool ParseStm32Dfu(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 24) return false;
    out.kind = EdaDocKind::KiCadPcb;

    struct FlashElem {
        uint32_t address = 0;
        uint32_t byteSize = 0;
        float fillRatio = 0.0f;
        uint32_t fnvHash = 0;
        std::wstring targetName;
    };
    std::vector<FlashElem> elems;

    // Decode optional 16-byte USB DFU 1.0/1.1 Suffix ("UFD" at offset +8..+10 of suffix)
    uint16_t vid = 0, pid = 0, bcdDev = 0;
    bool hasUfdSuffix = false;
    if (size >= 16) {
        const uint8_t* sfx = data + size - 16;
        if (sfx[8] == 'U' && sfx[9] == 'F' && sfx[10] == 'D' && sfx[11] >= 16) {
            bcdDev = DetailEda100::RdU16LE(sfx + 0);
            pid    = DetailEda100::RdU16LE(sfx + 2);
            vid    = DetailEda100::RdU16LE(sfx + 4);
            hasUfdSuffix = true;
        }
    }

    if (size >= 11 + 274 + 8 && std::memcmp(data, "DfuSe", 5) == 0 && data[5] == 0x01) {
        uint8_t numTargets = data[10];
        if (numTargets == 0 || numTargets > 32) return false;

        size_t pos = 11;
        size_t limit = hasUfdSuffix ? (size - 16) : size;
        for (uint8_t ti = 0; ti < numTargets && pos + 274 <= limit; ++ti) {
            if (std::memcmp(data + pos, "Target", 6) != 0) return false;
            bool named = DetailEda100::RdU32LE(data + pos + 7) != 0;
            std::string tname;
            if (named) {
                size_t nlen = 0;
                while (nlen < 255 && data[pos + 11 + nlen] != 0) ++nlen;
                tname = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + pos + 11), nlen));
            }
            if (tname.empty()) tname = "Target_" + std::to_string(data[pos + 6]);
            uint32_t nbElems = DetailEda100::RdU32LE(data + pos + 270);
            pos += 274;
            if (nbElems == 0 || nbElems > 512) return false;

            for (uint32_t ei = 0; ei < nbElems; ++ei) {
                if (pos + 8 > limit) return false;
                uint32_t elAddr = DetailEda100::RdU32LE(data + pos);
                uint32_t elSize = DetailEda100::RdU32LE(data + pos + 4);
                pos += 8;
                if (elSize > limit - pos) return false;

                size_t nonFF = 0;
                uint32_t h = 2166136261u;
                for (uint32_t k = 0; k < elSize; ++k) {
                    uint8_t b = data[pos + k];
                    if (b != 0xFF) ++nonFF;
                    h = (h ^ b) * 16777619u;
                }
                FlashElem fe;
                fe.address = elAddr;
                fe.byteSize = elSize;
                fe.fillRatio = (elSize > 0) ? (static_cast<float>(nonFF) / static_cast<float>(elSize)) : 0.0f;
                fe.fnvHash = h;
                fe.targetName = PathUtils::Utf8ToWide(tname);
                elems.push_back(std::move(fe));
                pos += elSize;
            }
        }
    } else if ((extLower == L".ufd" || extLower == L".dfu") && hasUfdSuffix && size > 16) {
        size_t payLen = size - 16;
        size_t nonFF = 0;
        for (size_t k = 0; k < payLen; ++k) if (data[k] != 0xFF) ++nonFF;
        FlashElem fe;
        fe.address = 0x08000000u;
        fe.byteSize = static_cast<uint32_t>(payLen);
        fe.fillRatio = static_cast<float>(nonFF) / static_cast<float>(payLen);
        fe.targetName = L"USB_DFU_RAW";
        elems.push_back(std::move(fe));
    } else {
        return false;
    }

    if (elems.empty()) return false;

    const float mapW = 72.0f;
    float totalH = static_cast<float>(elems.size()) * 11.0f + 8.0f;
    DetailEda100::AddRectOutline(out, 0.0f, 0.0f, mapW + 8.0f, totalH, EdaLayerId::EdgeCuts, 0.40f);

    uint64_t totalBytes = 0;
    for (size_t i = 0; i < elems.size(); ++i) {
        const auto& e = elems[i];
        totalBytes += e.byteSize;
        float cy = (totalH * 0.5f - 7.5f) - static_cast<float>(i) * 11.0f;

        wchar_t addrHex[48] = {};
        swprintf_s(addrHex, L"0x%08X", e.address);
        wchar_t valStr[96] = {};
        swprintf_s(valStr, L"%u B (%.1f%% code)", e.byteSize, e.fillRatio * 100.0f);

        EdaComponent3D secComp;
        secComp.refDes = addrHex;
        secComp.value = valStr;
        secComp.footprint = e.targetName;
        secComp.x = 0.0f;
        secComp.y = cy;
        secComp.widthMm = mapW;
        secComp.lengthMm = 7.5f;
        secComp.heightMm = 1.4f;
        secComp.category = EdaPartCategory::IC_Package;
        out.components.push_back(secComp);

        DetailEda100::AddRectOutline(out, 0.0f, cy, mapW, 7.5f, EdaLayerId::TopSilk, 0.25f);
        float fillW = std::clamp(e.fillRatio * (mapW - 2.0f), 1.0f, mapW - 2.0f);
        float fillCx = -(mapW - 2.0f) * 0.5f + fillW * 0.5f;
        out.pads.push_back({ fillCx, cy, fillW, 5.2f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, addrHex, valStr });
        out.labels.push_back({ 0.0f, cy, 1.05f, EdaLayerId::SchLabel,
            std::wstring(addrHex) + L"  [" + e.targetName + L"]  " + valStr });
    }

    wchar_t hdrBuf[192] = {};
    if (hasUfdSuffix) {
        swprintf_s(hdrBuf, L"STM32 DfuSe Firmware (%zu sectors, %llu B, VID:PID %04X:%04X, rev %04X)",
            elems.size(), static_cast<unsigned long long>(totalBytes), vid, pid, bcdDev);
    } else {
        swprintf_s(hdrBuf, L"STM32 DfuSe Firmware (%zu sectors, %llu B)",
            elems.size(), static_cast<unsigned long long>(totalBytes));
    }
    out.formatTitle = hdrBuf;
    out.netCount = elems.size();
    return true;
}

} // namespace DetailHwBin

inline bool ParseStm32DfuBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
    return DetailHwBin::ParseStm32Dfu(data, size, extLower, out);
}

} // namespace Formats
