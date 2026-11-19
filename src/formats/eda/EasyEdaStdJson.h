#pragma once
// EasyEdaStdJson.h — EasyEDA / JLCPCB Standard JSON (.json) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool IsLikelyEasyEdaJsonBytes(const uint8_t* data, size_t size) {
    if (!data || size < 24) return false;
    size_t scan = std::min<size_t>(size, 8192);
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == 0) return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    if (sv.find("[\"DOCTYPE\"") != std::string_view::npos || sv.find("[ \"DOCTYPE\"") != std::string_view::npos) {
        return true; // EasyEDA Pro JSON-Lines
    }
    return (sv.find("\"docType\"") != std::string_view::npos &&
            (sv.find("\"shape\"") != std::string_view::npos ||
             sv.find("\"canvas\"") != std::string_view::npos ||
             sv.find("\"editorVersion\"") != std::string_view::npos ||
             sv.find("CA~") != std::string_view::npos)) ||
           (sv.find("\"schematics\"") != std::string_view::npos && sv.find("\"dataStr\"") != std::string_view::npos);
}

// Parse EasyEDA Standard JSON ("head": {"docType": "1".."6"}, "shape": [ "TRACK~...", "PAD~...", "LIB~...", ... ])
inline bool ParseEasyEdaStandardJson(std::string_view sv, ParsedEdaBoard& out) {
    using namespace DetailEasyEda;
    if (!IsBalancedJsonText(sv)) return false;

    size_t p = 0;
    JVal root;
    if (!ParseJsonValue(sv, p, 0, root) || root.kind != JVal::Kind::Object) return false;

    // Support both single-doc {"head":..., "shape":[...]} and multi-doc {"schematics":[{"dataStr":{...}}], "pcbs":[...]}
    const JVal* docObj = &root;
    if (const JVal* pcbs = root.Find("pcbs"); pcbs && pcbs->kind == JVal::Kind::Array && !pcbs->arrVal.empty()) {
        if (const JVal* ds = pcbs->arrVal[0].Find("dataStr"); ds && ds->kind == JVal::Kind::Object) docObj = ds;
    } else if (const JVal* schs = root.Find("schematics"); schs && schs->kind == JVal::Kind::Array && !schs->arrVal.empty()) {
        if (const JVal* ds = schs->arrVal[0].Find("dataStr"); ds && ds->kind == JVal::Kind::Object) docObj = ds;
    }

    const JVal* head = docObj->Find("head");
    const JVal* shapes = docObj->Find("shape");
    if (!head || head->kind != JVal::Kind::Object || !shapes || shapes->kind != JVal::Kind::Array || shapes->arrVal.empty()) {
        return false;
    }

    int docType = 3;
    if (const JVal* dt = head->Find("docType")) {
        docType = (dt->kind == JVal::Kind::Number) ? static_cast<int>(dt->numVal) : std::atoi(dt->strVal.c_str());
    }
    const bool isSch = (docType == 1 || docType == 2 || docType == 5);
    const float kScale = 0.254f; // 1 EasyEDA unit = 10 mil = 0.254 mm

    std::string projTitle;
    if (const JVal* cp = head->Find("c_para"); cp && cp->kind == JVal::Kind::Object) {
        if (const JVal* nm = cp->Find("name"); nm && nm->kind == JVal::Kind::String) projTitle = nm->strVal;
        else if (const JVal* pk = cp->Find("package"); pk && pk->kind == JVal::Kind::String) projTitle = pk->strVal;
    }

    out.kind = isSch ? EdaDocKind::KiCadSch : EdaDocKind::KiCadPcb;
    out.formatTitle = isSch ? L"EasyEDA Standard Schematic (.json)" : L"EasyEDA Standard PCB (.json)";
    if (!projTitle.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(projTitle);

    std::unordered_map<std::string, int> netIds;
    auto getNetId = [&](std::string_view netSv) -> int {
        if (netSv.empty()) return 0;
        std::string s(netSv);
        auto it = netIds.find(s);
        if (it != netIds.end()) return it->second;
        int id = static_cast<int>(netIds.size() + 1);
        netIds[std::move(s)] = id;
        return id;
    };

    auto processPcbPrim = [&](std::string_view prim, std::string* ioRefDes, std::string* ioVal) {
        auto f = SplitByChar(prim, '~');
        if (f.size() < 3) return;
        std::string_view cmd = f[0];

        if (cmd == "TRACK" && f.size() >= 5) {
            float w = std::clamp(ParseF32Sv(f[1], 1.0f) * kScale, 0.08f, 4.0f);
            EdaLayerId lid = MapEasyEdaStdLayer(std::atoi(std::string(f[2]).c_str()));
            int nid = getNetId(f[3]);
            std::vector<float> coords;
            ParseSpaceFloats(f[4], coords);
            for (size_t i = 0; i + 3 < coords.size() && out.traces.size() < 120000; i += 2) {
                out.traces.push_back({ coords[i] * kScale, -coords[i + 1] * kScale,
                                       coords[i + 2] * kScale, -coords[i + 3] * kScale, w, lid, nid });
            }
        } else if (cmd == "PAD" && f.size() >= 10) {
            // PAD~shape~cx~cy~w~h~layerId~net~pinNum~holeRadius~points~rotDeg
            std::string_view sh = f[1];
            float cx = ParseF32Sv(f[2]) * kScale;
            float cy = -ParseF32Sv(f[3]) * kScale;
            float pw = std::max(0.15f, ParseF32Sv(f[4], 4.0f) * kScale);
            float ph = std::max(0.15f, ParseF32Sv(f[5], 4.0f) * kScale);
            int lyr = std::atoi(std::string(f[6]).c_str());
            getNetId(f[7]);
            float holeD = std::max(0.0f, ParseF32Sv(f[9], 0.0f) * 2.0f * kScale);
            float rot = (f.size() >= 12) ? ParseF32Sv(f[11], 0.0f) : 0.0f;

            EdaPadPrim pad{};
            pad.x = cx; pad.y = cy; pad.w = pw; pad.h = ph;
            pad.rotDeg = rot;
            pad.drill = holeD;
            pad.shape = (sh == "RECT") ? 1 : ((sh == "OVAL") ? 3 : 0);
            pad.layer = (holeD > 0.05f || lyr == 11) ? EdaLayerId::PadsThrough : MapEasyEdaStdLayer(lyr);
            pad.pinName = PathUtils::Utf8ToWide(f[8]);
            pad.netName = PathUtils::Utf8ToWide(f[7]);
            out.pads.push_back(std::move(pad));
        } else if (cmd == "VIA" && f.size() >= 6) {
            float vx = ParseF32Sv(f[1]) * kScale;
            float vy = -ParseF32Sv(f[2]) * kScale;
            float diam = std::max(0.25f, ParseF32Sv(f[3], 2.4f) * kScale);
            int nid = getNetId(f[4]);
            float drill = std::max(0.12f, ParseF32Sv(f[5], 0.6f) * 2.0f * kScale);
            out.vias.push_back({ vx, vy, diam, drill, nid });
        } else if (cmd == "CIRCLE" && f.size() >= 6) {
            float cx = ParseF32Sv(f[1]) * kScale;
            float cy = -ParseF32Sv(f[2]) * kScale;
            float r  = std::max(0.1f, ParseF32Sv(f[3]) * kScale);
            float sw = std::clamp(ParseF32Sv(f[4], 1.0f) * kScale, 0.08f, 2.0f);
            EdaLayerId lid = MapEasyEdaStdLayer(std::atoi(std::string(f[5]).c_str()));
            out.circles.push_back({ cx, cy, r, sw, false, lid });
        } else if ((cmd == "SOLIDREGION" || cmd == "COPPERAREA") && f.size() >= 4) {
            EdaLayerId lid = MapEasyEdaStdLayer(std::atoi(std::string(f[1]).c_str()));
            int nid = getNetId(f[2]);
            std::vector<float> pts;
            ParseSpaceFloats(f[3], pts);
            for (size_t i = 0; i + 3 < pts.size() && out.traces.size() < 120000; i += 2) {
                out.traces.push_back({ pts[i] * kScale, -pts[i + 1] * kScale,
                                       pts[i + 2] * kScale, -pts[i + 3] * kScale, 0.18f, lid, nid });
            }
            if (pts.size() >= 6) {
                out.traces.push_back({ pts[pts.size() - 2] * kScale, -pts[pts.size() - 1] * kScale,
                                       pts[0] * kScale, -pts[1] * kScale, 0.18f, lid, nid });
            }
        } else if (cmd == "TEXT" && f.size() >= 11) {
            std::string_view tType = f[1]; // P=Prefix/RefDes, N=Name/Value, L=Label
            float tx = ParseF32Sv(f[2]) * kScale;
            float ty = -ParseF32Sv(f[3]) * kScale;
            EdaLayerId lid = MapEasyEdaStdLayer(std::atoi(std::string(f[7]).c_str()));
            std::string_view txt = f[10];
            if (ioRefDes && tType == "P" && !txt.empty()) *ioRefDes = std::string(txt);
            if (ioVal && tType == "N" && !txt.empty()) *ioVal = std::string(txt);
            if (!txt.empty() && (f.size() < 13 || f[12] != "none") && out.labels.size() < 8000) {
                out.labels.push_back({ tx, ty, 0.9f, lid, PathUtils::Utf8ToWide(txt) });
            }
        }
    };

    auto processSchPrim = [&](std::string_view prim, std::string* ioRefDes, std::string* ioVal) {
        auto f = SplitByChar(prim, '~');
        if (f.size() < 2) return;
        std::string_view cmd = f[0];

        if ((cmd == "W" || cmd == "B" || cmd == "PL") && f.size() >= 2) {
            std::vector<float> pts;
            ParseSpaceFloats(f[1], pts);
            EdaLayerId lid = (cmd == "PL") ? EdaLayerId::SchSymbol : EdaLayerId::SchWire;
            float sw = (cmd == "B") ? 0.50f : 0.25f;
            for (size_t i = 0; i + 3 < pts.size() && out.traces.size() < 120000; i += 2) {
                out.traces.push_back({ pts[i] * kScale, -pts[i + 1] * kScale,
                                       pts[i + 2] * kScale, -pts[i + 3] * kScale, sw, lid, 1 });
            }
        } else if (cmd == "R" && f.size() >= 7) {
            float x = ParseF32Sv(f[1]) * kScale;
            float y = -ParseF32Sv(f[2]) * kScale;
            float w = ParseF32Sv(f[5]) * kScale;
            float h = ParseF32Sv(f[6]) * kScale;
            out.traces.push_back({ x,     y,     x + w, y,     0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x + w, y,     x + w, y - h, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x + w, y - h, x,     y - h, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x,     y - h, x,     y,     0.26f, EdaLayerId::SchSymbol, 0 });
        } else if (cmd == "E" && f.size() >= 5) {
            float cx = ParseF32Sv(f[1]) * kScale;
            float cy = -ParseF32Sv(f[2]) * kScale;
            float r  = std::max(0.2f, ParseF32Sv(f[3]) * kScale);
            out.circles.push_back({ cx, cy, r, 0.22f, false, EdaLayerId::SchSymbol });
        } else if (cmd == "J" && f.size() >= 3) {
            float jx = ParseF32Sv(f[1]) * kScale;
            float jy = -ParseF32Sv(f[2]) * kScale;
            out.vias.push_back({ jx, jy, 0.65f, 0.0f, 0 });
        } else if (cmd == "N" && f.size() >= 6) {
            float nx = ParseF32Sv(f[1]) * kScale;
            float ny = -ParseF32Sv(f[2]) * kScale;
            getNetId(f[5]);
            out.labels.push_back({ nx, ny, 0.95f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(f[5]) });
        } else if (cmd == "P" && f.size() >= 6) {
            // EasyEDA Sch Pin: P~show~0~pinNum~x~y~rot~id^^dotX~dotY^^M x1 y1 L x2 y2...^^0~nx~ny~...~pinName
            auto sections = SplitBySubstr(prim, "^^");
            auto hdr = SplitByChar(sections[0], '~');
            float px = (hdr.size() >= 5) ? ParseF32Sv(hdr[4]) * kScale : 0.0f;
            float py = (hdr.size() >= 6) ? -ParseF32Sv(hdr[5]) * kScale : 0.0f;
            std::string pinDes = (hdr.size() >= 4) ? std::string(hdr[3]) : "";
            std::string pinName = pinDes;
            if (sections.size() >= 3) {
                std::vector<float> pts;
                ParseSpaceFloats(sections[2], pts);
                for (size_t i = 0; i + 3 < pts.size(); i += 2) {
                    out.traces.push_back({ pts[i] * kScale, -pts[i + 1] * kScale,
                                           pts[i + 2] * kScale, -pts[i + 3] * kScale, 0.24f, EdaLayerId::SchSymbol, 0 });
                }
            }
            if (sections.size() >= 4) {
                auto nf = SplitByChar(sections[3], '~');
                if (nf.size() >= 5 && !nf[4].empty()) pinName = std::string(nf[4]);
            }
            EdaPadPrim pad{};
            pad.x = px; pad.y = py; pad.w = 0.65f; pad.h = 0.65f;
            pad.layer = EdaLayerId::SchSymbol;
            pad.pinName = PathUtils::Utf8ToWide(pinName);
            out.pads.push_back(std::move(pad));
        } else if (cmd == "T" && f.size() >= 12) {
            std::string_view tKind = f[1]; // P=Prefix, N=Name/Value
            float tx = ParseF32Sv(f[2]) * kScale;
            float ty = -ParseF32Sv(f[3]) * kScale;
            std::string_view txt = f[11];
            if (ioRefDes && tKind == "P" && !txt.empty()) *ioRefDes = std::string(txt);
            if (ioVal && tKind == "N" && !txt.empty()) *ioVal = std::string(txt);
            if (!txt.empty() && out.labels.size() < 8000) {
                out.labels.push_back({ tx, ty, 0.9f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(txt) });
            }
        }
    };

    for (const auto& item : shapes->arrVal) {
        if (item.kind != JVal::Kind::String || item.strVal.empty()) continue;
        std::string_view s = item.strVal;
        if (s.rfind("LIB~", 0) == 0) {
            auto parts = SplitBySubstr(s, "#@$");
            auto libHdr = SplitByChar(parts[0], '~');
            float lx = (libHdr.size() >= 2) ? ParseF32Sv(libHdr[1]) * kScale : 0.0f;
            float ly = (libHdr.size() >= 3) ? -ParseF32Sv(libHdr[2]) * kScale : 0.0f;
            float rot = (libHdr.size() >= 5) ? ParseF32Sv(libHdr[4], 0.0f) : 0.0f;

            std::string pkgName = "EASYEDA_PKG";
            if (libHdr.size() >= 4) {
                auto kvPairs = SplitByChar(libHdr[3], '`');
                for (size_t k = 0; k + 1 < kvPairs.size(); k += 2) {
                    if (kvPairs[k] == "package") pkgName = std::string(kvPairs[k + 1]);
                }
            }

            std::string refDes, valStr;
            size_t padsBefore = out.pads.size();
            for (size_t pi = 1; pi < parts.size(); ++pi) {
                if (isSch) processSchPrim(parts[pi], &refDes, &valStr);
                else       processPcbPrim(parts[pi], &refDes, &valStr);
            }

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(refDes.empty() ? ("U" + std::to_string(out.components.size() + 1)) : refDes);
            comp.value = PathUtils::Utf8ToWide(valStr);
            comp.footprint = PathUtils::Utf8ToWide(pkgName);
            comp.x = lx; comp.y = ly;
            comp.rotDeg = rot;
            comp.pinCount = std::max<int>(2, static_cast<int>(out.pads.size() - padsBefore));
            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
            out.components.push_back(std::move(comp));
        } else {
            if (isSch) processSchPrim(s, nullptr, nullptr);
            else       processPcbPrim(s, nullptr, nullptr);
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty() && out.circles.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    out.netCount = std::max<size_t>(1, netIds.empty() ? out.components.size() : netIds.size());
    return true;
}

inline bool ParseEasyEdaStdJsonFile(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16 || size > 64 * 1024 * 1024) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.find("[\"DOCTYPE\"") != std::string_view::npos || sv.find("[ \"DOCTYPE\"") != std::string_view::npos) {
        return false;
    }
    // 4. EasyEDA Standard JSON (.json)
    if (!IsLikelyEasyEdaJsonBytes(data, size)) {
        return false;
    }
    return ParseEasyEdaStandardJson(sv, out);
}

} // namespace Formats
