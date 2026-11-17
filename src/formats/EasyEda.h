#pragma once
// EasyEda.h — #28 EasyEDA / JLCPCB Standard (.json) & Pro (.epro, .eprj, .epcb, .esch, .esym, .efoo) Parser

namespace Formats {

namespace DetailEasyEda {

inline bool IsBalancedJsonText(std::string_view sv) {
    int braces = 0, brackets = 0;
    bool inStr = false;
    bool sawStruct = false;
    for (size_t i = 0; i < sv.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(sv[i]);
        if (uc == 0) return false;
        char c = sv[i];
        if (inStr) {
            if (c == '\\' && i + 1 < sv.size()) { ++i; continue; }
            if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{') { ++braces; sawStruct = true; }
            else if (c == '}') { if (--braces < 0) return false; }
            else if (c == '[') { ++brackets; sawStruct = true; }
            else if (c == ']') { if (--brackets < 0) return false; }
        }
    }
    return sawStruct && !inStr && braces == 0 && brackets == 0;
}

inline std::vector<std::string_view> SplitByChar(std::string_view sv, char delim) {
    std::vector<std::string_view> out;
    size_t p = 0;
    while (p <= sv.size()) {
        size_t e = sv.find(delim, p);
        if (e == std::string_view::npos) {
            out.push_back(sv.substr(p));
            break;
        }
        out.push_back(sv.substr(p, e - p));
        p = e + 1;
    }
    return out;
}

inline std::vector<std::string_view> SplitBySubstr(std::string_view sv, std::string_view delim) {
    std::vector<std::string_view> out;
    size_t p = 0;
    while (p <= sv.size()) {
        size_t e = sv.find(delim, p);
        if (e == std::string_view::npos) {
            out.push_back(sv.substr(p));
            break;
        }
        out.push_back(sv.substr(p, e - p));
        p = e + delim.size();
    }
    return out;
}

inline float ParseF32Sv(std::string_view sv, float defVal = 0.0f) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    if (sv.empty()) return defVal;
    char b[64] = {};
    std::memcpy(b, sv.data(), std::min<size_t>(sv.size(), 63));
    char* endp = nullptr;
    float v = std::strtof(b, &endp);
    return (endp != b && std::isfinite(v)) ? v : defVal;
}

inline void ParseSpaceFloats(std::string_view sv, std::vector<float>& outNums) {
    outNums.clear();
    size_t p = 0;
    while (p < sv.size()) {
        while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t' || sv[p] == ',' ||
               sv[p] == 'M' || sv[p] == 'L' || sv[p] == 'Z' || sv[p] == 'z' || sv[p] == 'm' || sv[p] == 'l')) ++p;
        if (p >= sv.size()) break;
        // Skip SVG arc flags if 'A' encountered; caller handles 'A' separately when needed
        if ((sv[p] >= 'A' && sv[p] <= 'Z') || (sv[p] >= 'a' && sv[p] <= 'z')) { ++p; continue; }
        size_t e = p;
        while (e < sv.size() && ((sv[e] >= '0' && sv[e] <= '9') || sv[e] == '.' || sv[e] == '-' || sv[e] == '+' || sv[e] == 'e' || sv[e] == 'E')) ++e;
        if (e == p) { ++p; continue; }
        outNums.push_back(ParseF32Sv(sv.substr(p, e - p)));
        p = e;
    }
}

inline EdaLayerId MapEasyEdaStdLayer(int layerId) {
    if (layerId == 1) return EdaLayerId::TopCopper;
    if (layerId == 2) return EdaLayerId::BottomCopper;
    if (layerId == 3) return EdaLayerId::TopSilk;
    if (layerId == 4) return EdaLayerId::BottomSilk;
    if (layerId == 10 || layerId == 12 || layerId == 99 || layerId == 100) return EdaLayerId::EdgeCuts;
    if (layerId == 11) return EdaLayerId::PadsThrough;
    if (layerId >= 21 && layerId <= 52) return EdaLayerId::InnerCopper;
    return EdaLayerId::TopSilk;
}

inline EdaLayerId MapEasyEdaProLayer(int layerId) {
    // EasyEDA Pro layer IDs: 1=TopLayer, 2=BottomLayer, 3=TopSilk, 4=BottomSilk, 11=BoardOutline, 12=MultiLayer, 15..46=InnerLayer
    if (layerId == 1) return EdaLayerId::TopCopper;
    if (layerId == 2) return EdaLayerId::BottomCopper;
    if (layerId == 3) return EdaLayerId::TopSilk;
    if (layerId == 4) return EdaLayerId::BottomSilk;
    if (layerId == 11 || layerId == 47 || layerId == 48) return EdaLayerId::EdgeCuts;
    if (layerId == 12) return EdaLayerId::PadsThrough;
    if (layerId >= 15 && layerId <= 46) return EdaLayerId::InnerCopper;
    return EdaLayerId::TopCopper;
}

// EasyEDA documents and JSON-Lines records are read with the shared JSON reader (JsonDom.h)
using JVal = ::Explorer2::Json::Value;

// EasyEDA's readers treat a JSON boolean as the number 1 / 0 (e.g. layer visibility flags)
inline void BoolsAsNumbers(JVal& v, int depth = 0) {
    if (depth > 64) return;
    if (v.kind == JVal::Kind::Bool) v.numVal = v.boolVal ? 1.0 : 0.0;
    for (auto& e : v.arrVal) BoolsAsNumbers(e, depth + 1);
    for (auto& kv : v.objVal) BoolsAsNumbers(kv.second, depth + 1);
}

// One JSON value starting at sv[p] (leading whitespace skipped); p moves past it
inline bool ParseJsonValue(std::string_view sv, size_t& p, int depth, JVal& out) {
    if (p > sv.size()) return false;
    ::Explorer2::Json::Limits limits;
    limits.maxDepth = std::max(1, 48 - depth);
    limits.maxNodes = 8000000;
    ::Explorer2::Json::Parser parser(sv.substr(p), limits);
    if (!parser.ParseValue(out)) return false;
    p += parser.Position();
    BoolsAsNumbers(out);
    return true;
}

} // namespace DetailEasyEda

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

// Parse EasyEDA Pro JSON-Lines (.epcb, .esch, .esym, .efoo)
inline bool ParseEasyEdaProJsonLines(std::string_view sv, ParsedEdaBoard& out, float sheetOffX = 0.0f) {
    using namespace DetailEasyEda;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }

    bool sawDocType = false;
    bool isSch = false;
    constexpr float kMilToMm = 0.0254f; // EasyEDA Pro coordinates are in mils

    std::unordered_map<std::string, size_t> compIdxById;
    std::unordered_map<std::string, int> netIds;
    auto getNetId = [&](const std::string& net) -> int {
        if (net.empty()) return 0;
        auto it = netIds.find(net);
        if (it != netIds.end()) return it->second;
        int id = static_cast<int>(netIds.size() + 1);
        netIds[net] = id;
        return id;
    };

    size_t pos = 0;
    size_t validLines = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
        while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
        if (ln.empty()) continue;

        size_t lp = 0;
        JVal rec;
        if (!ParseJsonValue(ln, lp, 0, rec) || rec.kind != JVal::Kind::Array || rec.arrVal.empty() || rec.arrVal[0].kind != JVal::Kind::String) {
            if (!sawDocType) return false;
            continue;
        }
        ++validLines;
        const std::string& cmd = rec.arrVal[0].strVal;

        if (cmd == "DOCTYPE" && rec.arrVal.size() >= 2) {
            sawDocType = true;
            const std::string& dt = rec.arrVal[1].strVal;
            isSch = (dt == "SCH" || dt == "SYMBOL" || dt == "SCHLIB");
            out.kind = isSch ? EdaDocKind::KiCadSch : EdaDocKind::KiCadPcb;
            out.formatTitle = isSch ? L"EasyEDA Pro Schematic (.esch)" : L"EasyEDA Pro 3D PCB (.epcb)";
        } else if (cmd == "NET" && rec.arrVal.size() >= 2 && rec.arrVal[1].kind == JVal::Kind::String) {
            getNetId(rec.arrVal[1].strVal);
        } else if (!isSch && cmd == "COMPONENT" && rec.arrVal.size() >= 7) {
            // ["COMPONENT", id, fpName, layer, x_mil, y_mil, rot, {"Designator":"U1","Value":"ESP32"}]
            std::string cid = rec.arrVal[1].strVal;
            std::string fp  = rec.arrVal[2].strVal;
            int lyr = static_cast<int>(rec.arrVal[3].numVal);
            float cx = sheetOffX + static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            float cy = static_cast<float>(rec.arrVal[5].numVal) * kMilToMm;
            float rot = static_cast<float>(rec.arrVal[6].numVal);

            std::string des, val;
            if (rec.arrVal.size() >= 8 && rec.arrVal[7].kind == JVal::Kind::Object) {
                if (const JVal* d = rec.arrVal[7].Find("Designator")) des = d->strVal;
                if (const JVal* v = rec.arrVal[7].Find("Value")) val = v->strVal;
                else if (const JVal* m = rec.arrVal[7].Find("Manufacturer Part")) val = m->strVal;
            }
            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(des.empty() ? cid : des);
            comp.value = PathUtils::Utf8ToWide(val);
            comp.footprint = PathUtils::Utf8ToWide(fp.empty() ? "EPCB_FP" : fp);
            comp.x = cx; comp.y = cy;
            comp.rotDeg = rot;
            comp.bottomSide = (lyr == 2);
            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
            compIdxById[cid] = out.components.size();
            out.labels.push_back({ cx, cy, 0.9f, comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, comp.refDes });
            out.components.push_back(std::move(comp));
        } else if (!isSch && cmd == "PAD" && rec.arrVal.size() >= 10) {
            // ["PAD", id, compId, net, layer, pinNum, x_mil, y_mil, rot, ["RECT"|"ELLIPSE"|"OVAL", w_mil, h_mil], holeDiam_mil]
            int nid = getNetId(rec.arrVal[3].strVal);
            (void)nid;
            int lyr = static_cast<int>(rec.arrVal[4].numVal);
            std::string pinNum = rec.arrVal[5].strVal;
            float px = sheetOffX + static_cast<float>(rec.arrVal[6].numVal) * kMilToMm;
            float py = static_cast<float>(rec.arrVal[7].numVal) * kMilToMm;
            float rot = static_cast<float>(rec.arrVal[8].numVal);
            float pw = 1.2f, ph = 1.2f;
            uint8_t sh = 1;
            if (rec.arrVal[9].kind == JVal::Kind::Array && rec.arrVal[9].arrVal.size() >= 3) {
                const std::string& st = rec.arrVal[9].arrVal[0].strVal;
                sh = (st == "ELLIPSE" || st == "CIRCLE") ? 0 : ((st == "OVAL") ? 3 : 1);
                pw = std::max(0.15f, static_cast<float>(rec.arrVal[9].arrVal[1].numVal) * kMilToMm);
                ph = std::max(0.15f, static_cast<float>(rec.arrVal[9].arrVal[2].numVal) * kMilToMm);
            }
            float holeD = 0.0f;
            if (rec.arrVal.size() >= 11) {
                if (rec.arrVal[10].kind == JVal::Kind::Number) holeD = static_cast<float>(rec.arrVal[10].numVal) * kMilToMm;
                else if (rec.arrVal[10].kind == JVal::Kind::Array && rec.arrVal[10].arrVal.size() >= 2) holeD = static_cast<float>(rec.arrVal[10].arrVal[1].numVal) * kMilToMm;
            }
            EdaPadPrim pad{};
            pad.x = px; pad.y = py; pad.w = pw; pad.h = ph;
            pad.rotDeg = rot; pad.drill = std::max(0.0f, holeD); pad.shape = sh;
            pad.layer = (pad.drill > 0.05f || lyr == 12) ? EdaLayerId::PadsThrough : MapEasyEdaProLayer(lyr);
            pad.pinName = PathUtils::Utf8ToWide(pinNum);
            pad.netName = PathUtils::Utf8ToWide(rec.arrVal[3].strVal);
            out.pads.push_back(std::move(pad));
        } else if (!isSch && cmd == "VIA" && rec.arrVal.size() >= 7) {
            // ["VIA", id, net, x_mil, y_mil, hole_mil, diam_mil]
            int nid = getNetId(rec.arrVal[2].strVal);
            float vx = sheetOffX + static_cast<float>(rec.arrVal[3].numVal) * kMilToMm;
            float vy = static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            float hole = std::max(0.12f, static_cast<float>(rec.arrVal[5].numVal) * kMilToMm);
            float diam = std::max(hole * 1.4f, static_cast<float>(rec.arrVal[6].numVal) * kMilToMm);
            out.vias.push_back({ vx, vy, diam, hole, nid });
        } else if (!isSch && cmd == "LINE" && rec.arrVal.size() >= 9) {
            // ["LINE", id, net, layer, x1, y1, x2, y2, width]
            int nid = getNetId(rec.arrVal[2].strVal);
            EdaLayerId lid = MapEasyEdaProLayer(static_cast<int>(rec.arrVal[3].numVal));
            float x1 = sheetOffX + static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            float y1 = static_cast<float>(rec.arrVal[5].numVal) * kMilToMm;
            float x2 = sheetOffX + static_cast<float>(rec.arrVal[6].numVal) * kMilToMm;
            float y2 = static_cast<float>(rec.arrVal[7].numVal) * kMilToMm;
            float w  = std::clamp(static_cast<float>(rec.arrVal[8].numVal) * kMilToMm, 0.08f, 4.0f);
            out.traces.push_back({ x1, y1, x2, y2, w, lid, nid });
        } else if (!isSch && (cmd == "POLY" || cmd == "POUR" || cmd == "FILL") && rec.arrVal.size() >= 5) {
            // ["POLY", id, net, layer, width, [x0, y0, x1, y1, ...]]
            int nid = getNetId(rec.arrVal[2].strVal);
            EdaLayerId lid = MapEasyEdaProLayer(static_cast<int>(rec.arrVal[3].numVal));
            size_t arrIdx = (rec.arrVal[4].kind == JVal::Kind::Array) ? 4 : 5;
            float w = (arrIdx == 5 && rec.arrVal[4].kind == JVal::Kind::Number) ? std::clamp(static_cast<float>(rec.arrVal[4].numVal) * kMilToMm, 0.12f, 2.0f) : 0.22f;
            if (arrIdx < rec.arrVal.size() && rec.arrVal[arrIdx].kind == JVal::Kind::Array) {
                const auto& pts = rec.arrVal[arrIdx].arrVal;
                for (size_t i = 0; i + 3 < pts.size() && out.traces.size() < 120000; i += 2) {
                    out.traces.push_back({
                        sheetOffX + static_cast<float>(pts[i].numVal) * kMilToMm,
                        static_cast<float>(pts[i + 1].numVal) * kMilToMm,
                        sheetOffX + static_cast<float>(pts[i + 2].numVal) * kMilToMm,
                        static_cast<float>(pts[i + 3].numVal) * kMilToMm,
                        w, lid, nid
                    });
                }
            }
        } else if (isSch && cmd == "COMPONENT" && rec.arrVal.size() >= 6) {
            // ["COMPONENT", id, symName, x, y, rot]
            std::string cid = rec.arrVal[1].strVal;
            std::string sym = rec.arrVal[2].strVal;
            float cx = sheetOffX + static_cast<float>(rec.arrVal[3].numVal) * kMilToMm;
            float cy = static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            float rot = static_cast<float>(rec.arrVal[5].numVal);

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(cid);
            comp.footprint = PathUtils::Utf8ToWide(sym.empty() ? "ESCH_SYM" : sym);
            comp.x = cx; comp.y = cy; comp.rotDeg = rot;
            comp.widthMm = 6.0f; comp.lengthMm = 4.5f; comp.heightMm = 1.2f;
            float hw = 3.0f, hh = 2.25f;
            out.traces.push_back({ cx - hw, cy - hh, cx + hw, cy - hh, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + hw, cy - hh, cx + hw, cy + hh, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + hw, cy + hh, cx - hw, cy + hh, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - hw, cy + hh, cx - hw, cy - hh, 0.26f, EdaLayerId::SchSymbol, 0 });
            compIdxById[cid] = out.components.size();
            out.components.push_back(std::move(comp));
        } else if (isSch && cmd == "ATTR" && rec.arrVal.size() >= 5) {
            // ["ATTR", id, parentId, key, val]
            auto it = compIdxById.find(rec.arrVal[2].strVal);
            if (it != compIdxById.end()) {
                auto& c = out.components[it->second];
                const std::string& k = rec.arrVal[3].strVal;
                const std::string& v = rec.arrVal[4].strVal;
                if (k == "Designator" && !v.empty()) {
                    c.refDes = PathUtils::Utf8ToWide(v);
                    out.labels.push_back({ c.x - 2.5f, c.y + 2.8f, 0.95f, EdaLayerId::SchLabel, c.refDes });
                } else if ((k == "Value" || k == "Device" || k == "Name") && !v.empty() && c.value.empty()) {
                    c.value = PathUtils::Utf8ToWide(v);
                }
            }
        } else if (isSch && cmd == "PIN" && rec.arrVal.size() >= 7) {
            // ["PIN", id, parentId, pinNum, pinName, x, y]
            float px = sheetOffX + static_cast<float>(rec.arrVal[5].numVal) * kMilToMm;
            float py = static_cast<float>(rec.arrVal[6].numVal) * kMilToMm;
            EdaPadPrim pad{};
            pad.x = px; pad.y = py; pad.w = 0.65f; pad.h = 0.65f;
            pad.layer = EdaLayerId::SchSymbol;
            pad.pinName = PathUtils::Utf8ToWide(rec.arrVal[4].strVal.empty() ? rec.arrVal[3].strVal : rec.arrVal[4].strVal);
            out.pads.push_back(std::move(pad));
            auto it = compIdxById.find(rec.arrVal[2].strVal);
            if (it != compIdxById.end()) {
                const auto& c = out.components[it->second];
                float bx = std::clamp(px, c.x - 3.0f, c.x + 3.0f);
                float by = std::clamp(py, c.y - 2.25f, c.y + 2.25f);
                out.traces.push_back({ bx, by, px, py, 0.22f, EdaLayerId::SchSymbol, 0 });
            }
        } else if (isSch && cmd == "WIRE" && rec.arrVal.size() >= 3 && rec.arrVal[2].kind == JVal::Kind::Array) {
            // ["WIRE", id, [[x1, y1, x2, y2], ...], netName]
            int nid = (rec.arrVal.size() >= 4 && rec.arrVal[3].kind == JVal::Kind::String) ? getNetId(rec.arrVal[3].strVal) : 1;
            for (const auto& seg : rec.arrVal[2].arrVal) {
                if (seg.kind == JVal::Kind::Array && seg.arrVal.size() >= 4) {
                    out.traces.push_back({
                        sheetOffX + static_cast<float>(seg.arrVal[0].numVal) * kMilToMm,
                        static_cast<float>(seg.arrVal[1].numVal) * kMilToMm,
                        sheetOffX + static_cast<float>(seg.arrVal[2].numVal) * kMilToMm,
                        static_cast<float>(seg.arrVal[3].numVal) * kMilToMm,
                        0.25f, EdaLayerId::SchWire, nid
                    });
                }
            }
        } else if (isSch && (cmd == "NETLABEL" || cmd == "FLAG") && rec.arrVal.size() >= 5) {
            // ["NETLABEL", id, netName, x, y]
            std::string nn = rec.arrVal[2].strVal;
            getNetId(nn);
            float nx = sheetOffX + static_cast<float>(rec.arrVal[3].numVal) * kMilToMm;
            float ny = static_cast<float>(rec.arrVal[4].numVal) * kMilToMm;
            out.vias.push_back({ nx, ny, 0.6f, 0.0f, 0 });
            out.labels.push_back({ nx + 0.3f, ny + 0.5f, 0.9f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(nn) });
        }
    }

    if (!sawDocType || (out.traces.empty() && out.pads.empty() && out.components.empty() && out.vias.empty())) {
        return false;
    }
    out.netCount = std::max<size_t>(1, netIds.empty() ? out.components.size() : netIds.size());
    return true;
}

inline bool ParseEasyEdaFile(const uint8_t* data, size_t size, const std::wstring& fullPath, const std::wstring& ext, ParsedEdaBoard& out) {
    if (!data || size < 16 || size > 64 * 1024 * 1024) return false;

    // 1. EasyEDA Pro .epro ZIP project archive (contains PCB/*.epcb and/or SHEET/*/*.esch + project.json)
    if (ext == L".epro" || (size >= 22 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4)) {
        std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries) || zEntries.empty()) {
            return false;
        }
        std::vector<std::string> epcbPaths, eschPaths;
        std::string projTitle;
        for (const auto& ze : zEntries) {
            if (ze.isDirectory) continue;
            std::string pUtf8 = PathUtils::WideToUtf8(ze.pathInArchive);
            std::wstring pLow = PathUtils::ToLower(ze.pathInArchive);
            if (pLow.size() > 5 && pLow.substr(pLow.size() - 5) == L".epcb") epcbPaths.push_back(pUtf8);
            else if (pLow.size() > 5 && pLow.substr(pLow.size() - 5) == L".esch") eschPaths.push_back(pUtf8);
            else if (pLow == L"project.json") {
                std::vector<uint8_t> pj;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, pUtf8.c_str(), pj) && !pj.empty()) {
                    size_t p = 0;
                    DetailEasyEda::JVal jv;
                    std::string_view psv(reinterpret_cast<const char*>(pj.data()), pj.size());
                    if (DetailEasyEda::ParseJsonValue(psv, p, 0, jv) && jv.kind == DetailEasyEda::JVal::Kind::Object) {
                        if (const auto* t = jv.Find("title"); t && t->kind == DetailEasyEda::JVal::Kind::String) projTitle = t->strVal;
                    }
                }
            }
        }
        if (epcbPaths.empty() && eschPaths.empty()) return false;

        if (!epcbPaths.empty()) {
            std::vector<uint8_t> pcbBytes;
            if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, epcbPaths[0].c_str(), pcbBytes) || pcbBytes.empty()) {
                return false;
            }
            std::string_view psv(reinterpret_cast<const char*>(pcbBytes.data()), pcbBytes.size());
            if (!ParseEasyEdaProJsonLines(psv, out)) return false;
            out.formatTitle = L"EasyEDA Pro Project (.epro — PCB";
            if (!projTitle.empty()) out.formatTitle += L": " + PathUtils::Utf8ToWide(projTitle);
            out.formatTitle += L")";
            return true;
        }

        float offX = 0.0f;
        size_t sheetsOk = 0;
        for (size_t i = 0; i < eschPaths.size() && i < 12; ++i) {
            std::vector<uint8_t> schBytes;
            if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, eschPaths[i].c_str(), schBytes) || schBytes.empty()) continue;
            std::string_view ssv(reinterpret_cast<const char*>(schBytes.data()), schBytes.size());
            if (ParseEasyEdaProJsonLines(ssv, out, offX)) {
                offX += 140.0f;
                ++sheetsOk;
            }
        }
        if (sheetsOk == 0) return false;
        out.formatTitle = L"EasyEDA Pro Project (.epro — " + std::to_wstring(sheetsOk) + L" Schematic Sheet" + (sheetsOk > 1 ? L"s)" : L")");
        return true;
    }

    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // 2. EasyEDA Pro .eprj project manifest JSON
    if (ext == L".eprj") {
        if (!DetailEasyEda::IsBalancedJsonText(sv)) return false;
        size_t p = 0;
        DetailEasyEda::JVal root;
        if (!DetailEasyEda::ParseJsonValue(sv, p, 0, root) || root.kind != DetailEasyEda::JVal::Kind::Object) return false;
        const auto* boards = root.Find("boards");
        const auto* schs   = root.Find("schematics");
        if (!boards && !schs && !root.Find("config")) return false;

        // Try loading companion PCB/*.epcb from disk if present alongside .eprj
        if (!fullPath.empty()) {
            size_t sep = fullPath.find_last_of(L"\\/");
            if (sep != std::wstring::npos) {
                std::wstring dir = fullPath.substr(0, sep);
                WIN32_FIND_DATAW fd{};
                HANDLE hFind = FindFirstFileW((dir + L"\\*.epcb").c_str(), &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    std::wstring epcbFile = dir + L"\\" + fd.cFileName;
                    FindClose(hFind);
                    std::vector<uint8_t> eb;
                    if (PathUtils::ReadFileBytes(epcbFile, 16 * 1024 * 1024, eb) && !eb.empty()) {
                        std::string_view esv(reinterpret_cast<const char*>(eb.data()), eb.size());
                        if (ParseEasyEdaProJsonLines(esv, out)) {
                            out.formatTitle = L"EasyEDA Pro Project (.eprj + .epcb)";
                            return true;
                        }
                    }
                }
            }
        }

        // Render structured project hierarchy card diagram from .eprj manifest
        std::string title;
        if (const auto* t = root.Find("title"); t && t->kind == DetailEasyEda::JVal::Kind::String) title = t->strVal;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"EasyEDA Pro Project Manifest (.eprj" + (title.empty() ? std::wstring(L")") : (L" — " + PathUtils::Utf8ToWide(title) + L")"));

        float y = 20.0f;
        size_t nodeCnt = 0;
        auto addProjBlock = [&](const std::string& kindTag, const std::string& itemName) {
            float x0 = 0.0f, x1 = 44.0f, y0 = y - 3.5f, y1 = y + 3.5f;
            out.traces.push_back({ x0, y0, x1, y0, 0.25f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y0, x1, y1, 0.25f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y1, x0, y1, 0.25f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x0, y1, x0, y0, 0.25f, EdaLayerId::SchSymbol, 0 });
            out.labels.push_back({ x0 + 1.5f, y, 1.0f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide("[" + kindTag + "] " + itemName) });
            EdaComponent3D c;
            c.refDes = PathUtils::Utf8ToWide(kindTag + std::to_string(nodeCnt + 1));
            c.value = PathUtils::Utf8ToWide(itemName);
            c.footprint = PathUtils::Utf8ToWide(kindTag);
            c.x = 22.0f; c.y = y; c.widthMm = 44.0f; c.lengthMm = 7.0f;
            out.components.push_back(std::move(c));
            y -= 10.0f;
            ++nodeCnt;
        };
        if (boards && boards->kind == DetailEasyEda::JVal::Kind::Object) {
            for (const auto& kv : boards->objVal) addProjBlock("PCB", kv.first);
        }
        if (schs && schs->kind == DetailEasyEda::JVal::Kind::Object) {
            for (const auto& kv : schs->objVal) addProjBlock("SCH", kv.first);
        }
        if (nodeCnt == 0) return false;
        out.netCount = nodeCnt;
        return true;
    }

    // 3. EasyEDA Pro JSON-Lines (.epcb, .esch, .esym, .efoo)
    if (ext == L".epcb" || ext == L".esch" || ext == L".esym" || ext == L".efoo" ||
        sv.find("[\"DOCTYPE\"") != std::string_view::npos || sv.find("[ \"DOCTYPE\"") != std::string_view::npos) {
        return ParseEasyEdaProJsonLines(sv, out);
    }

    // 4. EasyEDA Standard JSON (.json)
    if (!IsLikelyEasyEdaJsonBytes(data, size)) {
        return false;
    }
    return ParseEasyEdaStandardJson(sv, out);
}

} // namespace Formats
