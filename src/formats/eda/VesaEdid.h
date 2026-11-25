#pragma once
// VesaEdid.h — VESA E-EDID 1.3/1.4 & CTA-861 Display Descriptor (.edid) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailHwBin {

// VESA E-EDID 1.3 / 1.4 + CTA-861 (.edid) Parser
inline bool ParseVesaEdid(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    static const uint8_t kEdidMagic[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    if (!data || size < 128 || std::memcmp(data, kEdidMagic, 8) != 0) return false;
    if (data[18] == 0 || data[18] > 4) return false;

    // Strictly verify VESA E-EDID 128-byte modulo-256 base-block checksum
    uint32_t baseSum = 0;
    for (size_t i = 0; i < 128u; ++i) baseSum += data[i];
    if ((baseSum & 0xFFu) != 0u) return false;

    out.kind = EdaDocKind::KiCadSch;

    uint16_t mfgRaw = DetailSiliconAndBusEda::RdU16BE(data + 8);
    wchar_t c1 = static_cast<wchar_t>(L'A' + (((mfgRaw >> 10) & 0x1Fu) ? (((mfgRaw >> 10) & 0x1Fu) - 1u) : 0u));
    wchar_t c2 = static_cast<wchar_t>(L'A' + (((mfgRaw >> 5)  & 0x1Fu) ? (((mfgRaw >> 5)  & 0x1Fu) - 1u) : 0u));
    wchar_t c3 = static_cast<wchar_t>(L'A' + ((mfgRaw         & 0x1Fu) ? ((mfgRaw         & 0x1Fu) - 1u) : 0u));
    std::wstring mfg = { c1, c2, c3 };

    uint16_t prodCode  = DetailSiliconAndBusEda::RdU16LE(data + 10);
    uint32_t serialNum = DetailSiliconAndBusEda::RdU32LE(data + 12);
    int mfgWeek = data[16];
    int mfgYear = 1990 + data[17];
    int edidVer = data[18], edidRev = data[19];

    bool isDigital = (data[20] & 0x80u) != 0;
    std::wstring iface = L"Analog VGA";
    int bpc = 0;
    if (isDigital) {
        static const int kBpcLut[8] = { 0, 6, 8, 10, 12, 14, 16, 0 };
        bpc = kBpcLut[(data[20] >> 4) & 7u];
        switch (data[20] & 0x0Fu) {
        case 1: iface = L"DVI"; break;
        case 2: iface = L"HDMI-a"; break;
        case 3: iface = L"HDMI-b"; break;
        case 4: iface = L"MDDI"; break;
        case 5: iface = L"DisplayPort"; break;
        default: iface = L"Digital"; break;
        }
    }

    int hScreenCm = data[21], vScreenCm = data[22];
    float gamma = (data[23] != 0xFF) ? ((static_cast<float>(data[23]) + 100.0f) / 100.0f) : 2.2f;

    // 10-bit CIE 1931 Chromaticity coordinates
    float rx = static_cast<float>((static_cast<int>(data[27]) << 2) | ((data[25] >> 6) & 3)) / 1024.0f;
    float ry = static_cast<float>((static_cast<int>(data[28]) << 2) | ((data[25] >> 4) & 3)) / 1024.0f;
    float gx = static_cast<float>((static_cast<int>(data[29]) << 2) | ((data[25] >> 2) & 3)) / 1024.0f;
    float gy = static_cast<float>((static_cast<int>(data[30]) << 2) | ( data[25]       & 3)) / 1024.0f;
    float bx = static_cast<float>((static_cast<int>(data[31]) << 2) | ((data[26] >> 6) & 3)) / 1024.0f;
    float by = static_cast<float>((static_cast<int>(data[32]) << 2) | ((data[26] >> 4) & 3)) / 1024.0f;
    float wx = static_cast<float>((static_cast<int>(data[33]) << 2) | ((data[26] >> 2) & 3)) / 1024.0f;
    float wy = static_cast<float>((static_cast<int>(data[34]) << 2) | ( data[26]       & 3)) / 1024.0f;

    int prefHActive = 0, prefHBlank = 0, prefVActive = 0, prefVBlank = 0;
    int prefHImgMm = 0, prefVImgMm = 0;
    double prefHz = 0.0;
    std::wstring monName, monSerial, monText;
    int minVHz = 0, maxVHz = 0, maxPixClkMHz = 0;

    auto parseDtd = [&](const uint8_t* d) {
        uint16_t pixClk10k = DetailSiliconAndBusEda::RdU16LE(d);
        if (pixClk10k == 0) return;
        int ha = d[2] | ((d[4] & 0xF0) << 4);
        int hb = d[3] | ((d[4] & 0x0F) << 8);
        int va = d[5] | ((d[7] & 0xF0) << 4);
        int vb = d[6] | ((d[7] & 0x0F) << 8);
        int hm = d[12] | ((d[14] & 0xF0) << 4);
        int vm = d[13] | ((d[14] & 0x0F) << 8);
        double tot = static_cast<double>(ha + hb) * static_cast<double>(va + vb);
        double hz = (tot > 0.0) ? ((static_cast<double>(pixClk10k) * 10000.0) / tot) : 0.0;
        if (prefHActive == 0 && ha > 0 && va > 0) {
            prefHActive = ha; prefHBlank = hb;
            prefVActive = va; prefVBlank = vb;
            prefHImgMm = hm;  prefVImgMm = vm;
            prefHz = hz;
        }
    };

    for (int i = 0; i < 4; ++i) {
        const uint8_t* d = data + 54 + i * 18;
        if (DetailSiliconAndBusEda::RdU16LE(d) != 0) {
            parseDtd(d);
        } else if (d[2] == 0) {
            uint8_t tag = d[3];
            if (tag == 0xFC || tag == 0xFF || tag == 0xFE) {
                std::string s(reinterpret_cast<const char*>(d + 5), 13);
                size_t nl = s.find('\n');
                if (nl != std::string::npos) s.resize(nl);
                std::wstring ws = PathUtils::Utf8ToWide(DetailSiliconAndBusEda::TrimAsciiView(s));
                if (tag == 0xFC) monName = ws;
                else if (tag == 0xFF) monSerial = ws;
                else monText = ws;
            } else if (tag == 0xFD) {
                minVHz = d[5];
                maxVHz = d[6];
                maxPixClkMHz = static_cast<int>(d[9]) * 10;
            }
        }
    }

    // Parse CTA-861 Extension Blocks if present
    int extCount = data[126];
    int ctaVics = 0, ctaAudio = 0;
    bool hasHdmi = false;
    for (int ei = 0; ei < extCount && static_cast<size_t>((ei + 2) * 128) <= size; ++ei) {
        const uint8_t* ext = data + (ei + 1) * 128;
        uint32_t extSum = 0;
        for (size_t k = 0; k < 128u; ++k) extSum += ext[k];
        if ((extSum & 0xFFu) != 0u) return false;
        if (ext[0] == 0x02) { // CTA-861
            uint8_t dtdOff = ext[2];
            if (dtdOff >= 4 && dtdOff <= 127) {
                size_t cp = 4;
                while (cp < dtdOff) {
                    uint8_t hdr = ext[cp++];
                    uint8_t blkTag = (hdr >> 5) & 7u;
                    uint8_t blkLen = hdr & 0x1Fu;
                    if (cp + blkLen > dtdOff) break;
                    if (blkTag == 1) ctaAudio += blkLen / 3;
                    else if (blkTag == 2) ctaVics += blkLen;
                    else if (blkTag == 3 && blkLen >= 3) {
                        if ((ext[cp] == 0x03 && ext[cp + 1] == 0x0C && ext[cp + 2] == 0x00) ||
                            (ext[cp] == 0xD8 && ext[cp + 1] == 0x5D && ext[cp + 2] == 0xC4))
                        {
                            hasHdmi = true;
                        }
                    }
                    cp += blkLen;
                }
                for (size_t dp = dtdOff; dp + 18 <= 127; dp += 18) {
                    if (DetailSiliconAndBusEda::RdU16LE(ext + dp) != 0) parseDtd(ext + dp);
                }
            }
        }
    }
    if (hasHdmi && iface == L"Digital") iface = L"HDMI";

    // Proportional physical display frame
    float aspect = 16.0f / 9.0f;
    if (prefHImgMm > 0 && prefVImgMm > 0) aspect = static_cast<float>(prefHImgMm) / static_cast<float>(prefVImgMm);
    else if (hScreenCm > 0 && vScreenCm > 0) aspect = static_cast<float>(hScreenCm) / static_cast<float>(vScreenCm);
    else if (prefHActive > 0 && prefVActive > 0) aspect = static_cast<float>(prefHActive) / static_cast<float>(prefVActive);
    aspect = std::clamp(aspect, 0.5f, 3.6f);

    float panelH = 34.0f;
    float panelW = panelH * aspect;
    // Outer total blanking timing frame + inner active display frame
    float blankScaleX = (prefHActive > 0) ? (static_cast<float>(prefHActive + prefHBlank) / static_cast<float>(prefHActive)) : 1.08f;
    float blankScaleY = (prefVActive > 0) ? (static_cast<float>(prefVActive + prefVBlank) / static_cast<float>(prefVActive)) : 1.05f;
    DetailSiliconAndBusEda::AddRectOutline(out, -12.0f, 0.0f, panelW * blankScaleX, panelH * blankScaleY, EdaLayerId::BottomSilk, 0.16f);
    DetailSiliconAndBusEda::AddRectOutline(out, -12.0f, 0.0f, panelW, panelH, EdaLayerId::EdgeCuts, 0.45f);

    // CIE 1931 RGB Chromaticity Gamut Triangle on right side (x: +28..+54, y: -14..+14)
    const float gx0 = 28.0f, gy0 = -14.0f, gSpan = 26.0f;
    DetailSiliconAndBusEda::AddRectOutline(out, gx0 + gSpan * 0.5f, gy0 + gSpan * 0.5f, gSpan, gSpan, EdaLayerId::SchSymbol, 0.20f);
    if (rx > 0.01f || gx > 0.01f || bx > 0.01f) {
        float prx = gx0 + rx * gSpan, pry = gy0 + ry * gSpan;
        float pgx = gx0 + gx * gSpan, pgy = gy0 + gy * gSpan;
        float pbx = gx0 + bx * gSpan, pby = gy0 + by * gSpan;
        float pwx = gx0 + wx * gSpan, pwy = gy0 + wy * gSpan;
        out.traces.push_back({ prx, pry, pgx, pgy, 0.30f, EdaLayerId::TopCopper, 1 });
        out.traces.push_back({ pgx, pgy, pbx, pby, 0.30f, EdaLayerId::SchWire, 2 });
        out.traces.push_back({ pbx, pby, prx, pry, 0.30f, EdaLayerId::BottomCopper, 3 });
        out.circles.push_back({ pwx, pwy, 0.6f, 0.25f, true, EdaLayerId::TopSilk });
    }

    std::wstring dispName = monName.empty() ? (mfg + L"_" + std::to_wstring(prodCode)) : (mfg + L" " + monName);
    wchar_t resInfo[160] = {};
    if (prefHActive > 0 && prefVActive > 0) {
        swprintf_s(resInfo, L"%dx%d @ %.2f Hz (%s%s)",
            prefHActive, prefVActive, prefHz, iface.c_str(),
            bpc > 0 ? (L" " + std::to_wstring(bpc) + L"bpc").c_str() : L"");
    } else {
        swprintf_s(resInfo, L"EDID v%d.%d (%s, %d x %d cm)", edidVer, edidRev, iface.c_str(), hScreenCm, vScreenCm);
    }

    out.labels.push_back({ -12.0f, 5.0f, 1.35f, EdaLayerId::SchLabel, dispName });
    out.labels.push_back({ -12.0f, 0.0f, 1.15f, EdaLayerId::TopSilk, resInfo });

    wchar_t metaBuf[192] = {};
    swprintf_s(metaBuf, L"Mfg %d/W%d | Gamma %.2f | SN %u%s",
        mfgYear, mfgWeek, gamma, serialNum,
        maxVHz > 0 ? (L" | VRR " + std::to_wstring(minVHz) + L"-" + std::to_wstring(maxVHz) + L"Hz " + std::to_wstring(maxPixClkMHz) + L"MHz").c_str() : L"");
    out.labels.push_back({ -12.0f, -5.5f, 0.95f, EdaLayerId::SchLabel, metaBuf });
    if (ctaVics > 0 || ctaAudio > 0) {
        out.labels.push_back({ -12.0f, -10.0f, 0.90f, EdaLayerId::TopSilk,
            L"CTA-861: " + std::to_wstring(ctaVics) + L" VICs, " + std::to_wstring(ctaAudio) + L" Audio" });
    }
    if (!monSerial.empty() || !monText.empty()) {
        out.labels.push_back({ -12.0f, -13.5f, 0.85f, EdaLayerId::SchLabel, monSerial.empty() ? monText : monSerial });
    }

    out.formatTitle = L"VESA E-EDID v" + std::to_wstring(edidVer) + L"." + std::to_wstring(edidRev) +
                      L" (" + dispName + L" — " + resInfo + L")";
    out.netCount = 4 + extCount;
    return true;
}

} // namespace DetailHwBin

inline bool ParseVesaEdidBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
    return DetailHwBin::ParseVesaEdid(data, size, out);
}

} // namespace Formats
