#pragma once
// NiTdmsWaveform.h — National Instruments TDMS Binary Waveform (.tdms) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// 6. National Instruments TDMS Binary Waveform (.tdms — "TDSm" v1.0/v2.0 Lead-In + Meta + Raw Channels)
inline bool ParseNiTdmsBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 28 || size > 128 * 1024 * 1024) return false;
    if (std::memcmp(data, "TDSm", 4) != 0) return false;

    struct TdmsChannel {
        std::string path;
        std::string displayName;
        std::string unit;
        uint32_t dataType = 0;
        uint64_t numValues = 0;
        double wfIncrement = 1e-4;
        double wfStartOffset = 0.0;
        bool hasRawIndex = false;
        std::vector<double> samples;
    };
    std::map<std::string, TdmsChannel> chanMap;
    std::vector<std::string> activeOrder;

    size_t segPos = 0;
    size_t validSegs = 0;

    while (segPos + 28 <= size && validSegs < 4096) {
        if (std::memcmp(data + segPos, "TDSm", 4) != 0) {
            if (validSegs == 0) return false;
            break;
        }
        uint32_t toc = static_cast<uint32_t>(data[segPos + 4]) |
                       (static_cast<uint32_t>(data[segPos + 5]) << 8) |
                       (static_cast<uint32_t>(data[segPos + 6]) << 16) |
                       (static_cast<uint32_t>(data[segPos + 7]) << 24);
        const bool be = (toc & (1u << 6)) != 0;
        auto rdU32 = [&](const uint8_t* p) -> uint32_t {
            return be ? ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3])
                      : (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
        };
        auto rdU64 = [&](const uint8_t* p) -> uint64_t {
            return be ? ((static_cast<uint64_t>(rdU32(p)) << 32) | rdU32(p + 4))
                      : (static_cast<uint64_t>(rdU32(p)) | (static_cast<uint64_t>(rdU32(p + 4)) << 32));
        };
        auto rdF64 = [&](const uint8_t* p) -> double {
            double d = 0.0;
            if (be) {
                uint8_t r[8] = { p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0] };
                std::memcpy(&d, r, 8);
            } else {
                std::memcpy(&d, p, 8);
            }
            return d;
        };
        auto rdF32 = [&](const uint8_t* p) -> double {
            float f = 0.0f;
            if (be) {
                uint8_t r[4] = { p[3], p[2], p[1], p[0] };
                std::memcpy(&f, r, 4);
            } else {
                std::memcpy(&f, p, 4);
            }
            return static_cast<double>(f);
        };

        uint32_t ver = rdU32(data + segPos + 8);
        if (ver != 4712u && ver != 4713u) return false;
        uint64_t nextSegOff = rdU64(data + segPos + 12);
        uint64_t rawDataOff = rdU64(data + segPos + 20);
        if (nextSegOff == 0xFFFFFFFFFFFFFFFFULL) return false; // Incomplete crash segment
        if (rawDataOff > nextSegOff || segPos + 28ULL + nextSegOff > size) return false;

        const size_t metaStart = segPos + 28;
        const size_t rawStart  = metaStart + static_cast<size_t>(rawDataOff);
        const size_t segEnd    = metaStart + static_cast<size_t>(nextSegOff);

        const bool hasMeta    = (toc & (1u << 1)) != 0;
        const bool newObjList = (toc & (1u << 2)) != 0;
        const bool hasRaw     = (toc & (1u << 3)) != 0;
        const bool interleave = (toc & (1u << 5)) != 0;

        if (newObjList) activeOrder.clear();

        if (hasMeta) {
            size_t mp = metaStart;
            if (mp + 4 > rawStart) return false;
            uint32_t numObjs = rdU32(data + mp);
            mp += 4;
            if (numObjs > 4096) return false;

            for (uint32_t oi = 0; oi < numObjs; ++oi) {
                if (mp + 8 > rawStart) return false;
                uint32_t pathLen = rdU32(data + mp);
                mp += 4;
                if (pathLen > rawStart - mp) return false;
                std::string objPath(reinterpret_cast<const char*>(data + mp), pathLen);
                mp += pathLen;

                TdmsChannel& ch = chanMap[objPath];
                if (ch.path.empty()) {
                    ch.path = objPath;
                    // Clean "/'Group'/'Chan'" -> "Group.Chan"
                    std::string dn;
                    for (char c : objPath) {
                        if (c == '\'') continue;
                        if (c == '/' && !dn.empty()) dn.push_back('.');
                        else if (c != '/') dn.push_back(c);
                    }
                    ch.displayName = dn.empty() ? objPath : dn;
                }

                if (mp + 4 > rawStart) return false;
                uint32_t rawIdx = rdU32(data + mp);
                mp += 4;
                if (rawIdx == 0xFFFFFFFFu) {
                    // No raw data in this segment for this object
                } else if (rawIdx == 0x00000000u) {
                    // Reuses previous raw data index
                    if (ch.hasRawIndex && std::find(activeOrder.begin(), activeOrder.end(), objPath) == activeOrder.end()) {
                        activeOrder.push_back(objPath);
                    }
                } else {
                    if (rawIdx < 20u || mp + 16 > rawStart) return false;
                    ch.dataType = rdU32(data + mp);
                    uint32_t arrDim = rdU32(data + mp + 4);
                    ch.numValues = rdU64(data + mp + 8);
                    mp += 16;
                    if (arrDim != 1u) return false;
                    if (ch.dataType == 0x20u) { // tdsTypeString has extra uint64 totalSizeBytes
                        if (mp + 8 > rawStart) return false;
                        mp += 8;
                    }
                    ch.hasRawIndex = true;
                    if (std::find(activeOrder.begin(), activeOrder.end(), objPath) == activeOrder.end()) {
                        activeOrder.push_back(objPath);
                    }
                }

                // Properties ([numProps:u32])
                if (mp + 4 > rawStart) return false;
                uint32_t numProps = rdU32(data + mp);
                mp += 4;
                if (numProps > 4096) return false;
                for (uint32_t pi = 0; pi < numProps; ++pi) {
                    if (mp + 8 > rawStart) return false;
                    uint32_t pNameLen = rdU32(data + mp);
                    mp += 4;
                    if (pNameLen > rawStart - mp) return false;
                    std::string pName(reinterpret_cast<const char*>(data + mp), pNameLen);
                    mp += pNameLen;
                    if (mp + 4 > rawStart) return false;
                    uint32_t pType = rdU32(data + mp);
                    mp += 4;

                    if (pType == 0x20u) { // String
                        if (mp + 4 > rawStart) return false;
                        uint32_t sl = rdU32(data + mp);
                        mp += 4;
                        if (sl > rawStart - mp) return false;
                        std::string svProp(reinterpret_cast<const char*>(data + mp), sl);
                        mp += sl;
                        if (pName == "unit_string") ch.unit = svProp;
                    } else if (pType == 10u || pType == 0x1Au) { // Double
                        if (mp + 8 > rawStart) return false;
                        double dv = rdF64(data + mp);
                        mp += 8;
                        if (pName == "wf_increment" && dv > 0.0) ch.wfIncrement = dv;
                        else if (pName == "wf_start_offset") ch.wfStartOffset = dv;
                    } else if (pType == 9u || pType == 0x19u) { // Float
                        if (mp + 4 > rawStart) return false;
                        double fv = rdF32(data + mp);
                        mp += 4;
                        if (pName == "wf_increment" && fv > 0.0) ch.wfIncrement = fv;
                    } else if (pType == 0x44u) { // Timestamp (16 bytes: u64 fractions + i64 seconds)
                        if (mp + 16 > rawStart) return false;
                        mp += 16;
                    } else if (pType == 1u || pType == 5u || pType == 0x21u) {
                        if (mp + 1 > rawStart) return false;
                        mp += 1;
                    } else if (pType == 2u || pType == 6u) {
                        if (mp + 2 > rawStart) return false;
                        mp += 2;
                    } else if (pType == 3u || pType == 7u) {
                        if (mp + 4 > rawStart) return false;
                        mp += 4;
                    } else if (pType == 4u || pType == 8u) {
                        if (mp + 8 > rawStart) return false;
                        mp += 8;
                    } else {
                        break;
                    }
                }
            }
        }

        auto elemBytesOf = [](uint32_t dt) -> size_t {
            switch (dt) {
            case 1: case 5: case 0x21: return 1;
            case 2: case 6: return 2;
            case 3: case 7: case 9: case 0x19: return 4;
            case 4: case 8: case 10: case 0x1A: return 8;
            default: return 0;
            }
        };
        auto decodeSample = [&](uint32_t dt, const uint8_t* p) -> double {
            switch (dt) {
            case 1: return static_cast<double>(static_cast<int8_t>(p[0]));
            case 5: case 0x21: return static_cast<double>(p[0]);
            case 2: return static_cast<double>(static_cast<int16_t>(be ? ((p[0] << 8) | p[1]) : (p[0] | (p[1] << 8))));
            case 6: return static_cast<double>(static_cast<uint16_t>(be ? ((p[0] << 8) | p[1]) : (p[0] | (p[1] << 8))));
            case 3: return static_cast<double>(static_cast<int32_t>(rdU32(p)));
            case 7: return static_cast<double>(rdU32(p));
            case 4: return static_cast<double>(static_cast<int64_t>(rdU64(p)));
            case 8: return static_cast<double>(rdU64(p));
            case 9: case 0x19: return rdF32(p);
            case 10: case 0x1A: return rdF64(p);
            default: return 0.0;
            }
        };

        if (hasRaw && rawStart < segEnd && !activeOrder.empty()) {
            uint64_t chunkBytes = 0;
            for (const auto& op : activeOrder) {
                const auto& ch = chanMap[op];
                size_t eb = elemBytesOf(ch.dataType);
                if (eb == 0) return false;
                chunkBytes += ch.numValues * eb;
            }
            if (chunkBytes > 0) {
                size_t rp = rawStart;
                while (rp + chunkBytes <= segEnd) {
                    if (!interleave) {
                        for (const auto& op : activeOrder) {
                            auto& ch = chanMap[op];
                            size_t eb = elemBytesOf(ch.dataType);
                            for (uint64_t i = 0; i < ch.numValues; ++i) {
                                if (ch.samples.size() < 65536) {
                                    ch.samples.push_back(decodeSample(ch.dataType, data + rp));
                                }
                                rp += eb;
                            }
                        }
                    } else {
                        uint64_t nRows = chanMap[activeOrder[0]].numValues;
                        size_t rowStride = 0;
                        for (const auto& op : activeOrder) rowStride += elemBytesOf(chanMap[op].dataType);
                        if (rowStride * nRows != chunkBytes) break;
                        for (uint64_t r = 0; r < nRows; ++r) {
                            for (const auto& op : activeOrder) {
                                auto& ch = chanMap[op];
                                size_t eb = elemBytesOf(ch.dataType);
                                if (ch.samples.size() < 65536) {
                                    ch.samples.push_back(decodeSample(ch.dataType, data + rp));
                                }
                                rp += eb;
                            }
                        }
                    }
                }
            }
        }

        segPos = segEnd;
        ++validSegs;
    }

    std::vector<SpiceWaveVar> yVars;
    size_t maxPts = 0;
    double dt = 1e-4, t0 = 0.0;
    for (auto& kv : chanMap) {
        if (kv.second.samples.size() >= 2 && yVars.size() < 16) {
            if (yVars.empty()) { dt = kv.second.wfIncrement; t0 = kv.second.wfStartOffset; }
            maxPts = std::max(maxPts, kv.second.samples.size());
            SpiceWaveVar sv;
            sv.name = kv.second.displayName;
            sv.unit = kv.second.unit;
            sv.values = std::move(kv.second.samples);
            yVars.push_back(std::move(sv));
        }
    }
    if (yVars.empty() || maxPts < 2) return false;

    SpiceWaveVar xTime;
    xTime.name = "time (s)";
    xTime.values.resize(maxPts);
    for (size_t i = 0; i < maxPts; ++i) {
        xTime.values[i] = t0 + static_cast<double>(i) * dt;
    }

    std::wstring title = L"NI TDMS High-Speed Telemetry (.tdms — " + std::to_wstring(yVars.size()) +
                         L" channels, " + std::to_wstring(validSegs) + L" segments)";
    RenderSpiceWaveformsToBoard(title, false, xTime, yVars, out);
    return !out.traces.empty();
}

} // namespace Formats
