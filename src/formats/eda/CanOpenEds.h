#pragma once
// CanOpenEds.h — CiA 306 CANopen Electronic Data Sheet & Device Configuration (.eds / .dcf) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// CiA 306 CANopen Electronic Data Sheet / Device Configuration (.eds / .dcf)
inline bool ParseCanOpenEdsBoard(std::string_view sv, const std::wstring& ext, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    if (ext == L".eds" || ext == L".dcf") {
        if (sv.find("[DeviceInfo]") == std::string_view::npos &&
            sv.find("[MandatoryObjects]") == std::string_view::npos) {
            return false;
        }

        struct OdObject {
            uint16_t index = 0;
            int subIndex = -1;
            std::string paramName;
            std::string accessType;
            std::string defaultVal;
            uint16_t dataType = 0;
        };

        std::string vendorName, productName;
        int maxBaudKbps = 0;
        std::map<uint16_t, OdObject> mainOd;
        std::map<uint16_t, std::vector<std::string>> pdoMappings;

        std::string curSec;
        uint16_t curIdx = 0;
        int curSub = -1;
        bool curIsOd = false;

        size_t pos = 0;
        while (pos < sv.size()) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;

            size_t sc = ln.find(';');
            if (sc != std::string_view::npos) ln = ln.substr(0, sc);
            while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
            while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
            if (ln.empty()) continue;

            if (ln.front() == '[' && ln.back() == ']') {
                curSec.assign(ln.data() + 1, ln.size() - 2);
                curIsOd = false;
                curIdx = 0;
                curSub = -1;
                if (curSec.size() >= 4 && std::isxdigit(static_cast<unsigned char>(curSec[0])) &&
                    std::isxdigit(static_cast<unsigned char>(curSec[1])) &&
                    std::isxdigit(static_cast<unsigned char>(curSec[2])) &&
                    std::isxdigit(static_cast<unsigned char>(curSec[3])))
                {
                    char hex4[5] = { curSec[0], curSec[1], curSec[2], curSec[3], 0 };
                    uint32_t idxVal = std::strtoul(hex4, nullptr, 16);
                    if (idxVal >= 0x1000u && idxVal <= 0x9FFFu) {
                        curIdx = static_cast<uint16_t>(idxVal);
                        if (curSec.size() == 4) {
                            curIsOd = true;
                            mainOd[curIdx].index = curIdx;
                        } else {
                            std::string lowS = PathUtils::ToLowerAscii(curSec.substr(4));
                            if (lowS.rfind("sub", 0) == 0 && lowS.size() > 3) {
                                curSub = static_cast<int>(std::strtol(lowS.c_str() + 3, nullptr, 16));
                                curIsOd = true;
                            }
                        }
                    }
                }
                continue;
            }

            size_t eq = ln.find('=');
            if (eq == std::string_view::npos) continue;
            std::string_view keySv = ln.substr(0, eq);
            std::string_view valSv = ln.substr(eq + 1);
            while (!keySv.empty() && static_cast<unsigned char>(keySv.back()) <= 0x20u) keySv.remove_suffix(1);
            while (!valSv.empty() && static_cast<unsigned char>(valSv.front()) <= 0x20u) valSv.remove_prefix(1);
            std::string key(keySv);
            std::string val(valSv);

            if (curSec == "DeviceInfo") {
                if (key == "VendorName") vendorName = val;
                else if (key == "ProductName") productName = val;
                else if (key.rfind("BaudRate_", 0) == 0 && val == "1") {
                    int b = std::atoi(key.c_str() + 9);
                    maxBaudKbps = std::max(maxBaudKbps, b);
                }
            } else if (curIsOd) {
                if (curSub < 0) {
                    auto& od = mainOd[curIdx];
                    od.index = curIdx;
                    if (key == "ParameterName") od.paramName = val;
                    else if (key == "AccessType") od.accessType = val;
                    else if (key == "DefaultValue") od.defaultVal = val;
                    else if (key == "DataType") od.dataType = static_cast<uint16_t>(std::strtoul(val.c_str(), nullptr, 0));
                } else if (curSub >= 1 && ((curIdx >= 0x1600u && curIdx <= 0x17FFu) || (curIdx >= 0x1A00u && curIdx <= 0x1BFFu))) {
                    if (key == "DefaultValue" && !val.empty()) {
                        uint32_t rawMap = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
                        if (rawMap >= 0x10000001u) {
                            char mbuf[48] = {};
                            sprintf_s(mbuf, "0x%04X:%02X(%ub)", (rawMap >> 16) & 0xFFFFu, (rawMap >> 8) & 0xFFu, rawMap & 0xFFu);
                            pdoMappings[curIdx].emplace_back(mbuf);
                        }
                    }
                }
            }
        }

        // Zero-Cheat: require real Object Dictionary entries [1000]..[9FFF]
        if (mainOd.empty()) return false;

        std::vector<OdObject> rpdoObjs, tpdoObjs, coreObjs;
        for (const auto& kv : mainOd) {
            uint16_t idx = kv.first;
            if (idx >= 0x1400u && idx <= 0x17FFu) rpdoObjs.push_back(kv.second);
            else if (idx >= 0x1800u && idx <= 0x1BFFu) tpdoObjs.push_back(kv.second);
            else coreObjs.push_back(kv.second);
        }

        out.kind = EdaDocKind::KiCadSch;
        std::wstring devTitle = PathUtils::Utf8ToWide(productName.empty() ? (vendorName.empty() ? "CANopen Node" : vendorName) : productName);
        wchar_t titleBuf[192] = {};
        swprintf_s(titleBuf, L"CiA 306 CANopen Node — %ls (%zu OD objects, %zu RPDO, %zu TPDO)",
            devTitle.c_str(), mainOd.size(), rpdoObjs.size(), tpdoObjs.size());
        out.formatTitle = titleBuf;

        const size_t nRows = std::max<size_t>({ 4u, std::min<size_t>(coreObjs.size(), 10u), rpdoObjs.size(), tpdoObjs.size() });
        const float boxW = 34.0f;
        const float boxH = static_cast<float>(nRows) * 4.2f + 8.0f;
        drawSchRect(0.0f, 0.0f, boxW, boxH, 0.38f, EdaLayerId::SchSymbol, 0);

        std::wstring hdrTxt = devTitle;
        if (!vendorName.empty() && productName != vendorName) hdrTxt = PathUtils::Utf8ToWide(vendorName) + L" — " + hdrTxt;
        if (maxBaudKbps > 0) hdrTxt += L" (" + std::to_wstring(maxBaudKbps) + L" kbps)";
        out.labels.push_back({ -boxW * 0.48f, boxH * 0.5f + 2.0f, 1.05f, EdaLayerId::SchLabel, hdrTxt });

        EdaComponent3D devComp;
        devComp.refDes = devTitle;
        devComp.value = std::to_wstring(mainOd.size()) + L" OD entries";
        devComp.footprint = L"CANOPEN_DEVICE";
        devComp.x = 0.0f; devComp.y = 0.0f;
        devComp.widthMm = boxW; devComp.lengthMm = boxH;
        devComp.pinCount = static_cast<int>(rpdoObjs.size() + tpdoObjs.size() + 2u);
        devComp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(devComp));

        // Central Object Dictionary entries list inside device core
        for (size_t ci = 0; ci < std::min<size_t>(coreObjs.size(), 10u); ++ci) {
            const auto& od = coreObjs[ci];
            float ry = boxH * 0.5f - 4.0f - static_cast<float>(ci) * 3.8f;
            wchar_t odLine[128] = {};
            swprintf_s(odLine, L"[0x%04X] %hs%hs",
                od.index,
                od.paramName.empty() ? "Object" : od.paramName.c_str(),
                od.defaultVal.empty() ? "" : (" = " + od.defaultVal).c_str());
            out.labels.push_back({ -boxW * 0.44f, ry, 0.85f, EdaLayerId::SchLabel, odLine });
        }

        // Left side: CAN_H/CAN_L bus + RPDO input pins ([1400]..[17FF])
        const float leftEdge = -boxW * 0.5f;
        const float busX = leftEdge - 20.0f;
        out.traces.push_back({ busX, -boxH * 0.55f, busX, boxH * 0.55f, 0.42f, EdaLayerId::SchWire, 1 });
        out.labels.push_back({ busX - 4.0f, boxH * 0.55f + 1.8f, 0.95f, EdaLayerId::SchWire, L"CAN Bus (RPDO In)" });

        for (size_t ri = 0; ri < std::min<size_t>(rpdoObjs.size(), 8u); ++ri) {
            const auto& rp = rpdoObjs[ri];
            float py = boxH * 0.38f - static_cast<float>(ri) * 4.6f;
            out.traces.push_back({ busX, py, leftEdge, py, 0.26f, EdaLayerId::SchWire, static_cast<int>(ri + 2) });
            out.circles.push_back({ busX, py, 0.45f, 0.22f, true, EdaLayerId::SchWire });
            out.pads.push_back({ leftEdge, py, 0.65f, 0.65f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"RPDO", L"" });

            std::string mapDesc;
            auto mit = pdoMappings.find(rp.index);
            if (mit != pdoMappings.end() && !mit->second.empty()) mapDesc = " -> " + mit->second.front();
            wchar_t rpLbl[128] = {};
            swprintf_s(rpLbl, L"RPDO [0x%04X] %hs%hs",
                rp.index, rp.paramName.c_str(), mapDesc.c_str());
            out.labels.push_back({ busX + 1.2f, py + 0.8f, 0.82f, EdaLayerId::SchWire, rpLbl });
        }

        // Right side: TPDO output pins ([1800]..[1BFF])
        const float rightEdge = boxW * 0.5f;
        const float outBusX = rightEdge + 20.0f;
        out.traces.push_back({ outBusX, -boxH * 0.55f, outBusX, boxH * 0.55f, 0.42f, EdaLayerId::SchWire, 2 });
        out.labels.push_back({ outBusX - 6.0f, boxH * 0.55f + 1.8f, 0.95f, EdaLayerId::SchWire, L"CAN Bus (TPDO Out)" });

        for (size_t ti = 0; ti < std::min<size_t>(tpdoObjs.size(), 8u); ++ti) {
            const auto& tp = tpdoObjs[ti];
            float py = boxH * 0.38f - static_cast<float>(ti) * 4.6f;
            out.traces.push_back({ rightEdge, py, outBusX, py, 0.26f, EdaLayerId::SchWire, static_cast<int>(ti + 20) });
            out.circles.push_back({ outBusX, py, 0.45f, 0.22f, true, EdaLayerId::SchWire });
            out.pads.push_back({ rightEdge, py, 0.65f, 0.65f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"TPDO", L"" });

            std::string mapDesc;
            auto mit = pdoMappings.find(tp.index);
            if (mit != pdoMappings.end() && !mit->second.empty()) mapDesc = " <- " + mit->second.front();
            wchar_t tpLbl[128] = {};
            swprintf_s(tpLbl, L"TPDO [0x%04X] %hs%hs",
                tp.index, tp.paramName.c_str(), mapDesc.c_str());
            out.labels.push_back({ rightEdge + 1.2f, py + 0.8f, 0.82f, EdaLayerId::SchWire, tpLbl });
        }

        out.netCount = std::max<size_t>(1u, rpdoObjs.size() + tpdoObjs.size() + 1u);
        return true;
    }
    return false;
}

} // namespace Formats
