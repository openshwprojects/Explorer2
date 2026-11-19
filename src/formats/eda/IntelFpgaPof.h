#pragma once
// IntelFpgaPof.h — Intel / Altera FPGA Programmer Object File (.pof) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseIntelFpgaPofBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (ext == L".pof") {
        if (size < 14 || std::memcmp(data, "POF\0", 4) != 0) return false;
        const bool isPofMagic = true;

        uint32_t packetCount = DetailEda200::RdU32LE(data + 4);
        if (packetCount == 0 || packetCount > 4096) return false;

        std::string devStr;
        std::string creatorStr;
        std::vector<uint8_t> cfgBytes;
        size_t p = 8;
        for (uint32_t pk = 0; pk < packetCount; ++pk) {
            if (p + 6 > size) return false;
            uint16_t tag  = DetailEda200::RdU16LE(data + p);
            uint32_t plen = DetailEda200::RdU32LE(data + p + 2);
            p += 6;
            if (plen > size - p) return false;

            const uint8_t* pay = data + p;
            auto extractCleanAscii = [](const uint8_t* b, size_t n) -> std::string {
                size_t s = 0;
                while (s < n && (b[s] == 0 || b[s] == ' ' || b[s] == '\t')) ++s;
                size_t e = s;
                while (e < n && b[e] >= 32 && b[e] <= 126) ++e;
                while (e > s && b[e - 1] == ' ') --e;
                return (e > s) ? std::string(reinterpret_cast<const char*>(b + s), e - s) : std::string{};
            };

            if (tag == 1 && creatorStr.empty() && plen > 0 && plen <= 512) {
                creatorStr = extractCleanAscii(pay, plen);
            } else if (tag == 2 && devStr.empty() && plen > 0 && plen <= 256) {
                devStr = extractCleanAscii(pay, plen);
            } else if (plen >= 16) {
                if (cfgBytes.size() < 4 * 1024 * 1024) {
                    size_t take = std::min<size_t>(plen, 4 * 1024 * 1024 - cfgBytes.size());
                    cfgBytes.insert(cfgBytes.end(), pay, pay + take);
                }
            }
            if (devStr.empty() && tag != 1 && plen >= 4 && plen <= 64) {
                std::string cand = extractCleanAscii(pay, plen);
                if (cand.size() >= 4 && isalnum(static_cast<unsigned char>(cand[0]))) devStr = cand;
            }
            p += plen;
        }

        if (cfgBytes.size() < 16) return false;
        if (devStr.empty()) devStr = isPofMagic ? "Altera_POF" : "Altera_SOF";

        out.kind = EdaDocKind::KiCadPcb;
        std::wstring wDev = PathUtils::Utf8ToWide(devStr);
        out.formatTitle = (isPofMagic ? L"Intel/Altera POF Flash Bitstream (" : L"Intel/Altera SOF FPGA Bitstream (") + wDev + L")";

        int side = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(cfgBytes.size()) / 4.0)), 4, 16);
        int cols = side, rows = side;
        int numSec = cols * rows;
        float pitch = 2.8f;
        float pkgW = cols * pitch + 5.0f, pkgH = rows * pitch + 5.0f;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, pkgW, pkgH, EdaLayerId::EdgeCuts, 0.42f);

        auto popcnt8 = [](uint8_t v) -> int {
            v = static_cast<uint8_t>((v & 0x55u) + ((v >> 1) & 0x55u));
            v = static_cast<uint8_t>((v & 0x33u) + ((v >> 2) & 0x33u));
            return static_cast<int>((v + (v >> 4)) & 0x0Fu);
        };

        std::vector<float> secDensity(static_cast<size_t>(numSec), 0.0f);
        for (int s = 0; s < numSec; ++s) {
            size_t b0 = (static_cast<size_t>(s) * cfgBytes.size()) / static_cast<size_t>(numSec);
            size_t b1 = (static_cast<size_t>(s + 1) * cfgBytes.size()) / static_cast<size_t>(numSec);
            if (b1 <= b0) continue;
            size_t activeBits = 0;
            for (size_t i = b0; i < b1; ++i) {
                uint8_t bv = isPofMagic ? static_cast<uint8_t>(~cfgBytes[i]) : cfgBytes[i];
                activeBits += static_cast<size_t>(popcnt8(bv));
            }
            secDensity[static_cast<size_t>(s)] = static_cast<float>(activeBits) / static_cast<float>((b1 - b0) * 8u);
        }

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float sx = (c - (cols - 1) * 0.5f) * pitch;
                float sy = ((rows - 1) * 0.5f - r) * pitch;
                float dens = secDensity[static_cast<size_t>(r * cols + c)];
                if (dens <= 0.0f) continue;

                float padSz = 0.75f + dens * 1.55f;
                EdaLayerId lid = (dens >= 0.55f) ? EdaLayerId::TopCopper :
                                 (dens >= 0.20f) ? EdaLayerId::InnerCopper : EdaLayerId::BottomCopper;
                std::wstring sName = L"S" + std::to_wstring(r) + L"_" + std::to_wstring(c);
                out.pads.push_back({ sx, sy, padSz, padSz, 0.0f, 0.0f, 1, lid, sName, L"CFG" });

                if (c + 1 < cols && secDensity[static_cast<size_t>(r * cols + c + 1)] > 0.0f) {
                    float nx = ((c + 1) - (cols - 1) * 0.5f) * pitch;
                    out.traces.push_back({ sx, sy, nx, sy, 0.12f + dens * 0.22f, lid, r + 1 });
                }
                if (r + 1 < rows && secDensity[static_cast<size_t>((r + 1) * cols + c)] > 0.0f) {
                    float ny = ((rows - 1) * 0.5f - (r + 1)) * pitch;
                    out.traces.push_back({ sx, sy, sx, ny, 0.12f + dens * 0.22f, lid, r + 1 });
                }
            }
        }

        EdaComponent3D die;
        die.refDes = L"U1_FPGA";
        die.value = wDev;
        die.footprint = isPofMagic ? L"CFM_FLASH" : L"FPGA_DIE";
        die.widthMm = cols * pitch;
        die.lengthMm = rows * pitch;
        die.heightMm = 1.6f;
        die.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(die));

        out.labels.push_back({ -pkgW * 0.46f, pkgH * 0.5f + 2.0f, 1.25f, EdaLayerId::TopSilk,
            wDev + L" (" + std::to_wstring(packetCount) + L" pkts, " + std::to_wstring(cfgBytes.size()) + L" B)" });
        return true;
    }
    return false;
}

} // namespace Formats
