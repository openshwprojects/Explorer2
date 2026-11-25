#pragma once
// AltiumSchDoc.h — Altium Designer Schematic & Symbol Library (.schdoc / .schlib) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailAltium {

struct SchCompBuilder {
    std::wstring refDes;
    std::wstring value;
    std::wstring libRef;
    float x = 0.0f, y = 0.0f;
    float rotDeg = 0.0f;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    int pinCount = 0;
    int currentPartId = 1;
    bool hasBody = false;
};

inline bool ProcessSchRecords(const std::vector<KvMap>& records, ParsedEdaBoard& out, float offsetX = 0.0f, float offsetY = 0.0f) {
    std::unordered_map<int, SchCompBuilder> compMap;
    std::set<std::wstring> netSet;
    size_t decodedCount = 0;
    int lastCompIdx = -1;

    for (size_t idx = 0; idx < records.size(); ++idx) {
        const KvMap& kv = records[idx];
        std::string recStr = GetKvStr(kv, "record");
        if (recStr.empty()) continue;
        int recId = std::atoi(recStr.c_str());
        int ownerIdx = GetKvInt(kv, "ownerindex", -1);
        int ownerPartId = GetKvInt(kv, "ownerpartid", 0);
        const bool isBinaryPin = (GetKvInt(kv, "isbinarypin", 0) != 0);

        if (ownerIdx < 0 && recId != 1 && recId != 31) {
            if (isBinaryPin) {
                int candOwner = GetKvInt(kv, "binownerraw", -1);
                if (candOwner >= 0 && compMap.find(candOwner) != compMap.end()) {
                    ownerIdx = candOwner;
                } else if (lastCompIdx >= 0) {
                    ownerIdx = lastCompIdx;
                } else if (compMap.size() == 1) {
                    ownerIdx = compMap.begin()->first;
                }
            } else if (compMap.size() == 1 && compMap.find(0) != compMap.end()) {
                ownerIdx = 0;
            }
        }

        // Multi-part symbol filtering: skip primitives belonging to inactive sub-parts (e.g. Part B/C when CurrentPartId=1)
        if (ownerIdx >= 0 && ownerPartId > 0) {
            auto itOwner = compMap.find(ownerIdx);
            if (itOwner != compMap.end() && itOwner->second.currentPartId > 0 && ownerPartId != itOwner->second.currentPartId) {
                continue;
            }
        }

        auto expandOwnerBody = [&](float bx, float by) {
            auto it = compMap.find(ownerIdx);
            if (it != compMap.end()) {
                it->second.hasBody = true;
                it->second.minX = std::min(it->second.minX, bx);
                it->second.maxX = std::max(it->second.maxX, bx);
                it->second.minY = std::min(it->second.minY, by);
                it->second.maxY = std::max(it->second.maxY, by);
            }
        };

        switch (recId) {
        case 1: { // SchComponent
            SchCompBuilder cb;
            cb.libRef = PathUtils::Utf8ToWide(GetKvStr(kv, "libreference"));
            if (cb.libRef.empty()) cb.libRef = PathUtils::Utf8ToWide(GetKvStr(kv, "designitemid"));
            std::string inlDes = GetKvStr(kv, "designator");
            if (!inlDes.empty()) cb.refDes = PathUtils::Utf8ToWide(inlDes);
            std::string inlCmt = GetKvStr(kv, "comment");
            if (!inlCmt.empty()) cb.value = PathUtils::Utf8ToWide(inlCmt);
            cb.x = GetSchCoordMm(kv, "location.x") + offsetX;
            cb.y = GetSchCoordMm(kv, "location.y") + offsetY;
            cb.rotDeg = static_cast<float>((GetKvInt(kv, "orientation", 0) & 3) * 90);
            cb.currentPartId = std::max(1, GetKvInt(kv, "currentpartid", 1));
            lastCompIdx = static_cast<int>(idx);
            compMap[lastCompIdx] = cb;
            ++decodedCount;
            break;
        }
        case 2: { // Pin (ASCII |RECORD=2| or binary flags=1 record 0x02)
            float px = GetSchCoordMm(kv, "location.x") + offsetX;
            float py = GetSchCoordMm(kv, "location.y") + offsetY;
            float plen = std::fabs(GetSchCoordMm(kv, "pinlength", 5.08f));
            int cong = GetKvInt(kv, "pinconglomerate", 0);
            int orient = cong & 3; // 0=0 deg (right), 1=90 (up), 2=180 (left), 3=270 (down)
            float dx = (orient == 0) ? plen : ((orient == 2) ? -plen : 0.0f);
            float dy = (orient == 1) ? plen : ((orient == 3) ? -plen : 0.0f);
            float tipX = px + dx, tipY = py + dy;
            out.traces.push_back({ px, py, tipX, tipY, 0.25f, EdaLayerId::SchSymbol, 0 });

            std::wstring pName = PathUtils::Utf8ToWide(GetKvStr(kv, "name"));
            std::wstring pDes  = PathUtils::Utf8ToWide(GetKvStr(kv, "designator"));
            out.pads.push_back({ tipX, tipY, 0.7f, 0.7f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol, pDes.empty() ? pName : pDes, pName });
            if (isBinaryPin) {
                bool anyVisBits = (cong & 0x0C) != 0;
                bool showName = !anyVisBits || ((cong & 0x04) != 0);
                bool showDes  = !anyVisBits || ((cong & 0x08) != 0);
                if (!pName.empty() && showName) {
                    out.labels.push_back({ px, py + 0.8f, 0.9f, EdaLayerId::SchLabel, pName });
                }
                if (!pDes.empty() && pDes != pName && showDes) {
                    out.labels.push_back({ (px + tipX) * 0.5f, (py + tipY) * 0.5f + 0.65f, 0.82f, EdaLayerId::SchLabel, pDes });
                }
            } else if (!pName.empty()) {
                out.labels.push_back({ px, py + 0.8f, 0.9f, EdaLayerId::SchLabel, pName });
            }
            auto it = compMap.find(ownerIdx);
            if (it != compMap.end()) {
                ++it->second.pinCount;
                it->second.minX = std::min({ it->second.minX, px, tipX });
                it->second.maxX = std::max({ it->second.maxX, px, tipX });
                it->second.minY = std::min({ it->second.minY, py, tipY });
                it->second.maxY = std::max({ it->second.maxY, py, tipY });
            }
            ++decodedCount;
            break;
        }
        case 14:   // Rectangle
        case 15: { // RoundRect
            float x1 = GetSchCoordMm(kv, "location.x") + offsetX;
            float y1 = GetSchCoordMm(kv, "location.y") + offsetY;
            float x2 = GetSchCoordMm(kv, "corner.x", (x1 - offsetX) + 10.16f) + offsetX;
            float y2 = GetSchCoordMm(kv, "corner.y", (y1 - offsetY) + 7.62f) + offsetY;
            DetailSiliconAndBusEda::AddRectOutline(out, (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, std::fabs(x2 - x1), std::fabs(y2 - y1), EdaLayerId::SchSymbol, 0.30f);
            expandOwnerBody(x1, y1);
            expandOwnerBody(x2, y2);
            ++decodedCount;
            break;
        }
        case 13: { // Line
            float x1 = GetSchCoordMm(kv, "location.x") + offsetX;
            float y1 = GetSchCoordMm(kv, "location.y") + offsetY;
            float x2 = GetSchCoordMm(kv, "corner.x") + offsetX;
            float y2 = GetSchCoordMm(kv, "corner.y") + offsetY;
            out.traces.push_back({ x1, y1, x2, y2, 0.25f, EdaLayerId::SchSymbol, 0 });
            expandOwnerBody(x1, y1);
            expandOwnerBody(x2, y2);
            ++decodedCount;
            break;
        }
        case 5: // Bezier
        case 6: // Polyline
        case 7: // Polygon
        case 26: // Bus
        case 27: { // Wire
            int cnt = std::clamp(GetKvInt(kv, "locationcount", 0), 0, 128);
            const bool isSymBody = (recId == 5 || recId == 6 || recId == 7);
            EdaLayerId lid = isSymBody ? EdaLayerId::SchSymbol : EdaLayerId::SchWire;
            float stroke = (recId == 26) ? 0.55f : 0.28f;
            for (int i = 1; i < cnt; ++i) {
                std::string kx1 = "x" + std::to_string(i), ky1 = "y" + std::to_string(i);
                std::string kx2 = "x" + std::to_string(i + 1), ky2 = "y" + std::to_string(i + 1);
                float x1 = GetSchCoordMm(kv, kx1) + offsetX;
                float y1 = GetSchCoordMm(kv, ky1) + offsetY;
                float x2 = GetSchCoordMm(kv, kx2) + offsetX;
                float y2 = GetSchCoordMm(kv, ky2) + offsetY;
                out.traces.push_back({ x1, y1, x2, y2, stroke, lid, isSymBody ? 0 : 1 });
                if (isSymBody) {
                    expandOwnerBody(x1, y1);
                    expandOwnerBody(x2, y2);
                }
            }
            if (recId == 7 && cnt >= 3) {
                float x1 = GetSchCoordMm(kv, "x" + std::to_string(cnt)) + offsetX;
                float y1 = GetSchCoordMm(kv, "y" + std::to_string(cnt)) + offsetY;
                float x2 = GetSchCoordMm(kv, "x1") + offsetX;
                float y2 = GetSchCoordMm(kv, "y1") + offsetY;
                out.traces.push_back({ x1, y1, x2, y2, stroke, lid, 0 });
            }
            if (cnt >= 2) ++decodedCount;
            break;
        }
        case 8:  // Ellipse
        case 11: // EllipticalArc
        case 12: { // Arc
            float cx = GetSchCoordMm(kv, "location.x") + offsetX;
            float cy = GetSchCoordMm(kv, "location.y") + offsetY;
            float r  = std::max(0.4f, GetSchCoordMm(kv, "radius", 1.27f));
            out.circles.push_back({ cx, cy, r, 0.22f, false, EdaLayerId::SchSymbol });
            expandOwnerBody(cx - r, cy - r);
            expandOwnerBody(cx + r, cy + r);
            ++decodedCount;
            break;
        }
        case 17: // PowerPort
        case 18: // Port
        case 25: // NetLabel
        case 4: { // Text Label
            std::wstring txt = PathUtils::Utf8ToWide(GetKvStr(kv, "text"));
            if (txt.empty()) txt = PathUtils::Utf8ToWide(GetKvStr(kv, "name"));
            float lx = GetSchCoordMm(kv, "location.x") + offsetX;
            float ly = GetSchCoordMm(kv, "location.y") + offsetY;
            if (!txt.empty()) {
                out.labels.push_back({ lx, ly, 1.15f, EdaLayerId::SchLabel, txt });
                if (recId == 17 || recId == 25) netSet.insert(txt);
                if (recId == 17) {
                    out.traces.push_back({ lx, ly, lx, ly + 1.8f, 0.25f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx - 1.2f, ly + 1.8f, lx + 1.2f, ly + 1.8f, 0.30f, EdaLayerId::SchSymbol, 0 });
                }
                ++decodedCount;
            }
            break;
        }
        case 29: { // Junction
            float jx = GetSchCoordMm(kv, "location.x") + offsetX;
            float jy = GetSchCoordMm(kv, "location.y") + offsetY;
            out.circles.push_back({ jx, jy, 0.50f, 0.50f, true, EdaLayerId::SchWire });
            ++decodedCount;
            break;
        }
        case 34: { // Designator
            std::wstring des = PathUtils::Utf8ToWide(GetKvStr(kv, "text"));
            auto it = compMap.find(ownerIdx);
            if (it != compMap.end() && !des.empty()) {
                it->second.refDes = des;
            }
            if (!des.empty() && GetKvStr(kv, "ishidden") != "T") {
                out.labels.push_back({ GetSchCoordMm(kv, "location.x") + offsetX, GetSchCoordMm(kv, "location.y") + offsetY, 1.1f, EdaLayerId::SchLabel, des });
            }
            ++decodedCount;
            break;
        }
        case 41: { // Parameter
            std::string pName = PathUtils::ToLowerAscii(GetKvStr(kv, "name"));
            std::wstring pVal = PathUtils::Utf8ToWide(GetKvStr(kv, "text"));
            auto it = compMap.find(ownerIdx);
            if (it != compMap.end() && !pVal.empty()) {
                if (pName == "comment" || pName == "value") it->second.value = pVal;
                else if (pName == "designator" && it->second.refDes.empty()) it->second.refDes = pVal;
            }
            break;
        }
        default:
            break;
        }
    }

    for (auto& kvComp : compMap) {
        SchCompBuilder& cb = kvComp.second;
        EdaComponent3D c;
        c.refDes = cb.refDes.empty() ? (cb.libRef.empty() ? (L"U" + std::to_wstring(kvComp.first + 1)) : cb.libRef) : cb.refDes;
        c.value = cb.value;
        c.footprint = cb.libRef.empty() ? L"SCH_SYM" : cb.libRef;
        c.x = cb.x;
        c.y = cb.y;
        c.rotDeg = cb.rotDeg;
        c.pinCount = std::max(2, cb.pinCount);
        if (cb.minX < cb.maxX && cb.minY < cb.maxY) {
            c.widthMm = std::clamp(cb.maxX - cb.minX, 3.0f, 40.0f);
            c.lengthMm = std::clamp(cb.maxY - cb.minY, 2.5f, 40.0f);
            c.x = (cb.minX + cb.maxX) * 0.5f;
            c.y = (cb.minY + cb.maxY) * 0.5f;
        } else {
            c.widthMm = 7.62f;
            c.lengthMm = 5.08f;
        }
        if (!cb.hasBody) {
            DetailSiliconAndBusEda::AddRectOutline(out, c.x, c.y, c.widthMm, c.lengthMm, EdaLayerId::SchSymbol, 0.28f);
        }
        c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
        out.components.push_back(c);
    }

    out.netCount = std::max<size_t>(1, netSet.empty() ? out.components.size() : netSet.size());
    return decodedCount > 0 && (!out.traces.empty() || !out.components.empty() || !out.pads.empty() || !out.circles.empty());
}

inline bool ParseAltiumSchOle2(const Explorer2::Ole2::CompoundFile& cf, ParsedEdaBoard& out) {
    std::vector<KvMap> records;
    const auto* fh = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"FileHeader");
    if (fh) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *fh, bytes, 32 * 1024 * 1024)) {
            WalkLengthPrefixedBlocks(bytes, records, true, true);
        }
    }
    const auto* add = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Additional");
    if (add) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *add, bytes, 32 * 1024 * 1024)) {
            WalkLengthPrefixedBlocks(bytes, records, true, true);
        }
    }
    if (records.empty()) return false;
    return ProcessSchRecords(records, out);
}

inline bool ParseAltiumSchLibOle2(const Explorer2::Ole2::CompoundFile& cf, ParsedEdaBoard& out) {
    // Altium .SchLib stores each symbol inside <SectionName>/Data (pipe-delimited |RECORD=...| blocks), indexed by FileHeader
    size_t symCount = 0;
    float offX = 0.0f;
    for (const auto& e : cf.entries) {
        if (e.objectType != 2 || e.size < 8) continue;
        std::wstring lowPath = PathUtils::ToLower(e.fullPath);
        if (lowPath == L"fileheader" || lowPath == L"storage" || lowPath.find(L"/data") == std::wstring::npos) {
            continue;
        }
        std::vector<uint8_t> bytes;
        if (!Explorer2::Ole2::ExtractStream(cf, e, bytes, 16 * 1024 * 1024) || bytes.empty()) continue;
        std::vector<KvMap> recs;
        WalkLengthPrefixedBlocks(bytes, recs, true, true);
        if (recs.empty()) continue;

        size_t before = out.traces.size() + out.pads.size() + out.components.size() + out.circles.size();
        ProcessSchRecords(recs, out, offX, 0.0f);
        if (out.traces.size() + out.pads.size() + out.components.size() + out.circles.size() > before) {
            size_t sl = e.fullPath.find(L'/');
            std::wstring symTitle = (sl != std::wstring::npos) ? e.fullPath.substr(0, sl) : e.name;
            out.labels.push_back({ offX, 16.0f, 1.2f, EdaLayerId::SchLabel, symTitle });
            offX += 36.0f;
            ++symCount;
        }
    }
    if (symCount == 0) return false;
    out.kind = EdaDocKind::EagleSch;
    out.formatTitle = L"Altium Schematic Symbol Library (.SchLib — " + std::to_wstring(symCount) + L" symbols)";
    out.netCount = std::max<size_t>(1, symCount);
    return true;
}

} // namespace DetailAltium

inline bool ParseAltiumSchDocBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 8 || (extLower != L".schdoc" && extLower != L".schlib")) {
        return false;
    }
    out.kind = EdaDocKind::EagleSch;
    out.formatTitle = L"Altium Designer Schematic (.SchDoc)";

    // Case 1: Native Microsoft OLE2 Compound File Binary (D0 CF 11 E0 A1 B1 1A E1)
    static const uint8_t kOle2Magic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    if (size >= 512 && std::memcmp(data, kOle2Magic, 8) == 0) {
        Explorer2::Ole2::CompoundFile cf;
        if (!Explorer2::Ole2::ParseCompoundFile(data, size, cf)) return false;
        if (extLower == L".schlib") {
            return DetailAltium::ParseAltiumSchLibOle2(cf, out);
        }
        return DetailAltium::ParseAltiumSchOle2(cf, out);
    }

    // Case 2: Pipe-delimited ASCII Altium export (|RECORD=...|...)
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (!DetailSiliconAndBusEda::ContainsAsciiI(sv, "RECORD=")) return false;

    std::vector<DetailAltium::KvMap> asciiRecords;
    size_t pos = 0;
    while (pos < sv.size() && asciiRecords.size() < 20000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        if (!DetailSiliconAndBusEda::ContainsAsciiI(ln, "RECORD=")) continue;
        DetailAltium::KvMap kv = DetailAltium::ParsePipeRecord(ln);
        if (!kv.empty()) asciiRecords.push_back(std::move(kv));
    }
    if (asciiRecords.empty()) return false;

    return DetailAltium::ProcessSchRecords(asciiRecords, out);
}

} // namespace Formats
