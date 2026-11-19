#pragma once
// GarminFit.h — Garmin / ANT+ FIT Telemetry Activity (.fit) Reader

namespace Formats {
namespace GeoSciDetail {

// ============================================================================
// 2. Garmin / ANT+ FIT Telemetry Activity (.fit)
// ============================================================================
struct FitFieldDef {
    uint8_t num = 0;
    uint8_t size = 0;
    uint8_t baseType = 0;
};

struct FitLocalDef {
    bool active = false;
    bool bigEndian = false;
    uint16_t globalMsgNum = 0;
    std::vector<FitFieldDef> fields;
    std::vector<FitFieldDef> devFields;
};

inline bool ReadFitScalar(const uint8_t* p, uint8_t size, uint8_t baseType, bool be, int64_t& outInt, double& outDbl, bool& isSigned) {
    uint8_t bt = baseType & 0x1Fu;
    isSigned = false;
    outInt = 0;
    outDbl = 0.0;
    switch (bt) {
        case 0: // enum
        case 2: // uint8
        case 10: // uint8z
        case 13: // byte
            if (size < 1) return false;
            if ((bt == 0 || bt == 2 || bt == 13) && p[0] == 0xFFu) return false;
            if (bt == 10 && p[0] == 0x00u) return false;
            outInt = p[0]; outDbl = static_cast<double>(p[0]); return true;
        case 1: // sint8
            if (size < 1 || p[0] == 0x7Fu) return false;
            isSigned = true; outInt = static_cast<int8_t>(p[0]); outDbl = static_cast<double>(outInt); return true;
        case 3: // sint16
            if (size < 2) return false;
            { int16_t v = be ? RdI16BE(p) : RdI16LE(p); if (v == 0x7FFF) return false; isSigned = true; outInt = v; outDbl = v; return true; }
        case 4: // uint16
        case 11: // uint16z
            if (size < 2) return false;
            { uint16_t v = be ? RdU16BE(p) : RdU16LE(p); if ((bt == 4 && v == 0xFFFFu) || (bt == 11 && v == 0)) return false; outInt = v; outDbl = v; return true; }
        case 5: // sint32
            if (size < 4) return false;
            { int32_t v = be ? RdI32BE(p) : RdI32LE(p); if (v == 0x7FFFFFFF) return false; isSigned = true; outInt = v; outDbl = v; return true; }
        case 6: // uint32
        case 12: // uint32z
            if (size < 4) return false;
            { uint32_t v = be ? RdU32BE(p) : RdU32LE(p); if ((bt == 6 && v == 0xFFFFFFFFu) || (bt == 12 && v == 0)) return false; outInt = v; outDbl = v; return true; }
        case 8: // float32
            if (size < 4) return false;
            { float f = be ? RdF32BE(p) : RdF32LE(p); if (!std::isfinite(f)) return false; outDbl = f; outInt = static_cast<int64_t>(f); return true; }
        case 9: // float64
            if (size < 8) return false;
            { double d = be ? RdF64BE(p) : RdF64LE(p); if (!std::isfinite(d)) return false; outDbl = d; outInt = static_cast<int64_t>(d); return true; }
        case 14: // sint64
            if (size < 8) return false;
            { int64_t v = be ? RdI64BE(p) : RdI64LE(p); if (v == std::numeric_limits<int64_t>::max()) return false; isSigned = true; outInt = v; outDbl = static_cast<double>(v); return true; }
        case 15: // uint64
        case 16: // uint64z
            if (size < 8) return false;
            { uint64_t v = be ? RdU64BE(p) : RdU64LE(p); if ((bt == 15 && v == UINT64_MAX) || (bt == 16 && v == 0)) return false; outInt = static_cast<int64_t>(v); outDbl = static_cast<double>(v); return true; }
        default:
            return false;
    }
}

inline bool ParseGarminFit(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 14) return false;
    uint8_t hdrSize = raw[0];
    if ((hdrSize != 12 && hdrSize != 14) || hdrSize > raw.size()) return false;
    if (std::memcmp(raw.data() + 8, ".FIT", 4) != 0) return false;

    uint8_t protoVer = raw[1];
    uint16_t profileVer = RdU16LE(raw.data() + 2);
    uint32_t dataLen = RdU32LE(raw.data() + 4);
    size_t endPos = std::min<size_t>(raw.size(), static_cast<size_t>(hdrSize) + static_cast<size_t>(dataLen));

    FitLocalDef defs[16] = {};
    uint32_t lastTimestamp = 0;

    SpreadsheetSheet trackSh;
    trackSh.name = L"FIT_Track_Records";
    trackSh.rows.push_back({
        L"Timestamp (UTC)", L"Latitude", L"Longitude", L"Altitude (m)",
        L"Heart Rate (bpm)", L"Cadence (rpm)", L"Distance (m)", L"Speed (m/s)", L"Power (W)"
    });

    SpreadsheetSheet sumSh;
    sumSh.name = L"FIT_Activity_Summary";
    sumSh.rows.push_back({ L"Message / Metric", L"Field", L"Value" });
    sumSh.rows.push_back({ L"FIT Header", L"Protocol / Profile", std::to_wstring(protoVer >> 4) + L"." + std::to_wstring(protoVer & 0x0Fu) + L" / " + std::to_wstring(profileVer) });
    sumSh.rows.push_back({ L"FIT Header", L"Data Payload Size", PathUtils::FormatByteSize(dataLen) });

    constexpr int64_t kGarminEpochOffset = 631065600LL; // 1989-12-31 00:00:00 UTC
    constexpr double kSemicircleToDeg = 180.0 / 2147483648.0;

    size_t pos = hdrSize;
    size_t totalDataMsgs = 0;
    while (pos < endPos) {
        uint8_t hdr = raw[pos++];
        bool isCompTs = (hdr & 0x80u) != 0;
        if (!isCompTs && (hdr & 0x40u) != 0) {
            // Definition Message
            bool hasDev = (hdr & 0x20u) != 0;
            uint8_t ltype = hdr & 0x0Fu;
            if (pos + 5 > endPos) break;
            bool be = (raw[pos + 1] != 0);
            uint16_t gmsg = be ? RdU16BE(raw.data() + pos + 2) : RdU16LE(raw.data() + pos + 2);
            uint8_t nFields = raw[pos + 4];
            pos += 5;
            if (static_cast<size_t>(nFields) * 3 > endPos - pos) break;

            FitLocalDef d;
            d.active = true;
            d.bigEndian = be;
            d.globalMsgNum = gmsg;
            d.fields.resize(nFields);
            for (uint8_t i = 0; i < nFields; ++i) {
                d.fields[i] = { raw[pos], raw[pos + 1], raw[pos + 2] };
                pos += 3;
            }
            if (hasDev) {
                if (pos >= endPos) break;
                uint8_t nDev = raw[pos++];
                if (static_cast<size_t>(nDev) * 3 > endPos - pos) break;
                d.devFields.resize(nDev);
                for (uint8_t i = 0; i < nDev; ++i) {
                    d.devFields[i] = { raw[pos], raw[pos + 1], raw[pos + 2] };
                    pos += 3;
                }
            }
            defs[ltype] = std::move(d);
            continue;
        }

        // Data Message (Normal or Compressed Timestamp)
        uint8_t ltype = isCompTs ? ((hdr >> 5) & 0x03u) : (hdr & 0x0Fu);
        if (isCompTs) {
            uint32_t offs = hdr & 0x1Fu;
            if (offs >= (lastTimestamp & 0x1Fu)) lastTimestamp = (lastTimestamp & ~0x1Fu) + offs;
            else lastTimestamp = (lastTimestamp & ~0x1Fu) + offs + 0x20u;
        }
        const FitLocalDef& def = defs[ltype];
        if (!def.active) break;

        size_t msgBytes = 0;
        for (const auto& f : def.fields) msgBytes += f.size;
        for (const auto& df : def.devFields) msgBytes += df.size;
        if (msgBytes > endPos - pos) break;

        const uint8_t* fp = raw.data() + pos;
        pos += msgBytes;
        ++totalDataMsgs;

        if (def.globalMsgNum == 20) { // record
            uint32_t ts = lastTimestamp;
            bool hasTs = isCompTs, hasLat = false, hasLon = false, hasAlt = false;
            double lat = 0.0, lon = 0.0, altM = 0.0;
            std::wstring hrStr, cadStr, distStr, spdStr, pwrStr;

            for (const auto& f : def.fields) {
                int64_t iv = 0; double dv = 0.0; bool sgn = false;
                if (ReadFitScalar(fp, f.size, f.baseType, def.bigEndian, iv, dv, sgn)) {
                    switch (f.num) {
                        case 253: ts = static_cast<uint32_t>(iv); lastTimestamp = ts; hasTs = true; break;
                        case 0:   lat = static_cast<double>(static_cast<int32_t>(iv)) * kSemicircleToDeg; hasLat = true; break;
                        case 1:   lon = static_cast<double>(static_cast<int32_t>(iv)) * kSemicircleToDeg; hasLon = true; break;
                        case 2:   if (!hasAlt) { altM = dv / 5.0 - 500.0; hasAlt = true; } break;
                        case 78:  altM = dv / 5.0 - 500.0; hasAlt = true; break; // enhanced_altitude
                        case 3:   hrStr = std::to_wstring(iv); break;
                        case 4:   cadStr = std::to_wstring(iv); break;
                        case 5:   distStr = FmtNum(dv / 100.0, 6); break;
                        case 6:   if (spdStr.empty()) spdStr = FmtNum(dv / 1000.0, 4); break;
                        case 73:  spdStr = FmtNum(dv / 1000.0, 4); break; // enhanced_speed
                        case 7:   pwrStr = std::to_wstring(iv); break;
                    }
                }
                fp += f.size;
            }

            std::wstring tsStr = hasTs ? FormatUtcEpoch(static_cast<int64_t>(ts) + kGarminEpochOffset) : L"";
            int rowIdx = static_cast<int>(trackSh.rows.size());
            if (hasLat && hasLon && std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0 && out.geoPoints.size() < 20000) {
                GpsTrackPoint gp;
                gp.lat = lat;
                gp.lon = lon;
                gp.elevM = hasAlt ? static_cast<float>(altM) : 0.0f;
                gp.rowIndex = rowIdx;
                gp.label = tsStr + (hrStr.empty() ? L"" : (L" | " + hrStr + L" bpm"));
                out.geoPoints.push_back(std::move(gp));
            }
            if (trackSh.rows.size() <= 2000) {
                trackSh.rows.push_back({
                    tsStr,
                    hasLat ? FmtCoord(lat) : L"",
                    hasLon ? FmtCoord(lon) : L"",
                    hasAlt ? FmtNum(altM, 5) : L"",
                    hrStr, cadStr, distStr, spdStr, pwrStr
                });
            }
        } else if (def.globalMsgNum == 18 || def.globalMsgNum == 19 || def.globalMsgNum == 0) { // session / lap / file_id
            std::wstring msgLbl = (def.globalMsgNum == 18) ? L"Session" : (def.globalMsgNum == 19 ? L"Lap" : L"File ID");
            for (const auto& f : def.fields) {
                int64_t iv = 0; double dv = 0.0; bool sgn = false;
                if (sumSh.rows.size() < 200 && ReadFitScalar(fp, f.size, f.baseType, def.bigEndian, iv, dv, sgn)) {
                    if (def.globalMsgNum == 0 && f.num == 1) sumSh.rows.push_back({ msgLbl, L"Manufacturer ID", std::to_wstring(iv) });
                    else if (def.globalMsgNum == 0 && f.num == 4) sumSh.rows.push_back({ msgLbl, L"Time Created", FormatUtcEpoch(iv + kGarminEpochOffset) });
                    else if (def.globalMsgNum == 18 && f.num == 5) sumSh.rows.push_back({ msgLbl, L"Sport Enum", std::to_wstring(iv) });
                    else if ((def.globalMsgNum == 18 || def.globalMsgNum == 19) && f.num == 7) sumSh.rows.push_back({ msgLbl, L"Total Elapsed Time (s)", FmtNum(dv / 1000.0) });
                    else if ((def.globalMsgNum == 18 || def.globalMsgNum == 19) && f.num == 9) sumSh.rows.push_back({ msgLbl, L"Total Distance (m)", FmtNum(dv / 100.0) });
                    else if ((def.globalMsgNum == 18 || def.globalMsgNum == 19) && f.num == 16) sumSh.rows.push_back({ msgLbl, L"Avg Heart Rate (bpm)", std::to_wstring(iv) });
                    else if ((def.globalMsgNum == 18 || def.globalMsgNum == 19) && f.num == 17) sumSh.rows.push_back({ msgLbl, L"Max Heart Rate (bpm)", std::to_wstring(iv) });
                }
                fp += f.size;
            }
        }
    }

    sumSh.rows.push_back({ L"FIT Stream", L"Decoded Data Messages", std::to_wstring(totalDataMsgs) });
    FinalizeCols(trackSh);
    FinalizeCols(sumSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Garmin FIT Telemetry Activity (.fit)";
    if (trackSh.rows.size() > 1) {
        out.sheets.push_back(std::move(trackSh));
        out.sheets.push_back(std::move(sumSh));
    } else {
        out.sheets.push_back(std::move(sumSh));
    }
    out.valid = true;
    return true;
}

} // namespace GeoSciDetail

inline bool ParseGarminFitDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return GeoSciDetail::ParseGarminFit(raw, out); }

} // namespace Formats
