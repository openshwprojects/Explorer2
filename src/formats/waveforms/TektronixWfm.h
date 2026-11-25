#pragma once
// TektronixWfm.h — Tektronix WFM#001/002/003 Binary Oscilloscope Waveform (.wfm) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseTektronixWfmBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
        std::vector<float> samples;
        std::wstring fmtTitle;
        std::wstring metaLabel;

        // 1. Tektronix Binary WFM (0x0F0F LE / 0xF0F0 BE + ":WFM#001" / ":WFM#002" / ":WFM#003")
        bool isTekLe = (size >= 780 && data[0] == 0x0Fu && data[1] == 0x0Fu);
        bool isTekBe = (size >= 780 && data[0] == 0xF0u && data[1] == 0xF0u);
        if ((isTekLe || isTekBe) && std::memcmp(data + 2, ":WFM#00", 7) == 0 &&
            (data[9] == '1' || data[9] == '2' || data[9] == '3'))
        {
            int wfmVer = data[9] - '0';
            size_t minHdr = (wfmVer == 3) ? 838u : 780u;
            if (size < minHdr) return false;
            bool be = isTekBe;

            auto rdU16 = [&](size_t o) -> uint16_t {
                return be ? static_cast<uint16_t>((static_cast<uint16_t>(data[o]) << 8) | data[o + 1])
                          : DetailIndustrialAndWaveEda::RdU16LE(data + o);
            };
            auto rdU32 = [&](size_t o) -> uint32_t {
                return be ? ((static_cast<uint32_t>(data[o]) << 24) |
                             (static_cast<uint32_t>(data[o + 1]) << 16) |
                             (static_cast<uint32_t>(data[o + 2]) << 8) |
                             static_cast<uint32_t>(data[o + 3]))
                          : DetailIndustrialAndWaveEda::RdU32LE(data + o);
            };
            auto rdU64 = [&](size_t o) -> uint64_t {
                return be ? ((static_cast<uint64_t>(rdU32(o)) << 32) | rdU32(o + 4))
                          : DetailIndustrialAndWaveEda::RdU64LE(data + o);
            };
            auto rdF64 = [&](size_t o) -> double {
                uint64_t bits = rdU64(o);
                double dv = 0.0;
                std::memcpy(&dv, &bits, 8);
                return dv;
            };

            uint8_t bpp = data[15];
            uint32_t curveBufOff = rdU32(16);
            if ((bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) || curveBufOff < minHdr || curveBufOff >= size) {
                return false;
            }

            size_t ed1 = (wfmVer == 1) ? 154u : 168u;
            double dimScale  = rdF64(ed1 + 0);
            double dimOffset = rdF64(ed1 + 8);
            int32_t expFmt   = static_cast<int32_t>(rdU32(ed1 + 72));
            if (!std::isfinite(dimScale) || std::fabs(dimScale) < 1e-18 || std::fabs(dimScale) > 1e12) dimScale = 1.0;
            if (!std::isfinite(dimOffset) || std::fabs(dimOffset) > 1e12) dimOffset = 0.0;

            size_t sStart = curveBufOff;
            size_t sEnd   = size;
            size_t cobj   = curveBufOff - 30u;
            uint32_t dStartOff = rdU32(cobj + 14);
            uint32_t dPostOff  = rdU32(cobj + 18);
            if (dPostOff > dStartOff && curveBufOff + static_cast<size_t>(dPostOff) <= size &&
                (dPostOff - dStartOff) >= static_cast<uint32_t>(bpp) * 2u)
            {
                sStart = curveBufOff + static_cast<size_t>(dStartOff);
                sEnd   = curveBufOff + static_cast<size_t>(dPostOff);
            }

            size_t totalPts = (sEnd - sStart) / bpp;
            if (totalPts < 2) return false;
            size_t stepPts = std::max<size_t>(1, totalPts / 1024);
            samples.reserve(std::min<size_t>(totalPts, 1025));

            for (size_t idx = 0; idx < totalPts && samples.size() < 1024; idx += stepPts) {
                size_t off = sStart + idx * bpp;
                double raw = 0.0;
                if (bpp == 1) {
                    raw = (expFmt == 7) ? static_cast<double>(data[off]) : static_cast<double>(static_cast<int8_t>(data[off]));
                } else if (bpp == 2) {
                    uint16_t u = rdU16(off);
                    raw = static_cast<double>(static_cast<int16_t>(u));
                } else if (bpp == 4) {
                    uint32_t u = rdU32(off);
                    if (expFmt == 4) {
                        float fv = 0.0f; std::memcpy(&fv, &u, 4);
                        raw = std::isfinite(fv) ? static_cast<double>(fv) : 0.0;
                    } else if (expFmt == 2) {
                        raw = static_cast<double>(u);
                    } else {
                        raw = static_cast<double>(static_cast<int32_t>(u));
                    }
                } else {
                    uint64_t u = rdU64(off);
                    if (expFmt == 5 || expFmt == 6) {
                        double dv = 0.0; std::memcpy(&dv, &u, 8);
                        raw = std::isfinite(dv) ? dv : 0.0;
                    } else {
                        raw = static_cast<double>(static_cast<int64_t>(u));
                    }
                }
                samples.push_back(static_cast<float>(raw * dimScale + dimOffset));
            }
            fmtTitle = L"Tektronix WFM#00" + std::to_wstring(wfmVer) + L" Oscilloscope Waveform (.wfm)";
            metaLabel = L"Tek WFM#00" + std::to_wstring(wfmVer) + L" (" + std::to_wstring(totalPts) + L" pts)";
        }
        else {
            return false;
        }
        return DetailIndustrialAndWaveEda::RenderSingleScopeTrace(out, samples, fmtTitle, metaLabel);
}

} // namespace Formats
