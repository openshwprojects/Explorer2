#pragma once
// GenCadPcb.h — IPC-2511 / Mitron GenCAD 1.4 ECAD PCB Interchange (.gencad)
// Parses $HEADER, $BOARD, $PADS, $SHAPES, $DEVICES, $COMPONENTS, $SIGNALS, $TRACKS, and $ROUTES
// with full metric/imperial/user unit scaling and rotated/bottom-side component footprint instantiation.

namespace Formats {

namespace DetailGenCad {

struct PadDef {
    std::string type;
    float wMm     = 1.2f;
    float hMm     = 1.2f;
    float drillMm = 0.0f;
    uint8_t shape = 0; // 0=circle, 1=rect, 2=roundrect, 3=oval
};

struct ShapeLine {
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
};

struct ShapePin {
    std::string pinName;
    std::string padName;
    float lx = 0.0f, ly = 0.0f;
    float rotDeg = 0.0f;
    std::string layer; // TOP, BOTTOM, ALL
};

struct ShapeDef {
    std::vector<ShapeLine> lines;
    std::vector<ShapePin> pins;
    float minX = -1.5f, minY = -1.0f;
    float maxX =  1.5f, maxY =  1.0f;
    bool hasBounds = false;

    void ExpandBounds(float x, float y) {
        if (!hasBounds) {
            minX = maxX = x;
            minY = maxY = y;
            hasBounds = true;
        } else {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
};

struct DeviceDef {
    std::string part;
    std::string value;
    std::string type;
};

inline bool NextGenCadToken(std::string_view& line, std::string& outTok) {
    outTok.clear();
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    line.remove_prefix(i);
    if (line.empty()) return false;
    if (line.front() == '"') {
        line.remove_prefix(1);
        size_t q = 0;
        while (q < line.size() && line[q] != '"') {
            if (line[q] == '\\' && q + 1 < line.size()) {
                outTok.push_back(line[q + 1]);
                q += 2;
            } else {
                outTok.push_back(line[q++]);
            }
        }
        if (q < line.size() && line[q] == '"') ++q;
        line.remove_prefix(q);
        return true;
    }
    size_t e = 0;
    while (e < line.size() && line[e] != ' ' && line[e] != '\t') ++e;
    outTok.assign(line.data(), e);
    line.remove_prefix(e);
    return true;
}

inline bool ParseTokDouble(const std::string& tok, double& outVal) {
    if (tok.empty()) return false;
    char* endp = nullptr;
    double v = std::strtod(tok.c_str(), &endp);
    if (endp == tok.c_str() || *endp != '\0' || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline EdaLayerId MapGenCadLayer(const std::string& upLayer, bool isThrough = false) {
    if (isThrough || upLayer == "ALL" || upLayer == "THRU") return EdaLayerId::PadsThrough;
    if (upLayer == "BOTTOM" || upLayer == "BOT" || upLayer == "SOLDER" || upLayer == "B.CU") {
        return EdaLayerId::BottomCopper;
    }
    if (upLayer.rfind("INNER", 0) == 0 || upLayer.rfind("IN", 0) == 0 ||
        upLayer == "GND" || upLayer == "PWR" || upLayer == "POWER") {
        return EdaLayerId::InnerCopper;
    }
    if (upLayer.find("SILK") != std::string::npos) {
        return (upLayer.find("BOT") != std::string::npos) ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
    }
    return EdaLayerId::TopCopper;
}

inline EdaPartCategory GuessCategory(const std::string& refDes, const std::string& devType) {
    std::string upType = ReaderText::ToUpperAscii(devType);
    if (upType.find("IC") != std::string::npos || upType.find("U") == 0) return EdaPartCategory::IC_Package;
    if (upType.find("RES") != std::string::npos) return EdaPartCategory::ResistorSMD;
    if (upType.find("CAP") != std::string::npos) return EdaPartCategory::CapacitorSMD;
    if (upType.find("CON") != std::string::npos) return EdaPartCategory::Connector;
    if (upType.find("IND") != std::string::npos) return EdaPartCategory::Inductor;
    if (upType.find("DIO") != std::string::npos) return EdaPartCategory::DiodeLED;
    if (!refDes.empty()) {
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(refDes[0])));
        if (c == 'U') return EdaPartCategory::IC_Package;
        if (c == 'R') return EdaPartCategory::ResistorSMD;
        if (c == 'C') return EdaPartCategory::CapacitorSMD;
        if (c == 'J' || c == 'P') return EdaPartCategory::Connector;
        if (c == 'L') return EdaPartCategory::Inductor;
        if (c == 'D') return EdaPartCategory::DiodeLED;
        if (c == 'Q') return EdaPartCategory::TransistorSOT;
        if (c == 'Y' || c == 'X') return EdaPartCategory::Crystal;
    }
    return EdaPartCategory::GenericSMD;
}

} // namespace DetailGenCad

inline bool ParseGenCadPcb(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view content;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, content, 24u, (32u << 20))) return false;

    bool sawHeaderOpen = false;
    bool sawHeaderClose = false;
    bool sawGenCadMagic = false;
    std::string activeSection;
    std::string drawingName;
    double unitToMm = 25.4; // GenCAD default if UNITS INCH; updated by UNITS directive

    std::unordered_map<std::string, DetailGenCad::PadDef> padDefs;
    std::unordered_map<std::string, DetailGenCad::ShapeDef> shapeDefs;
    std::unordered_map<std::string, DetailGenCad::DeviceDef> deviceDefs;
    std::unordered_map<std::string, float> trackWidthsMm;
    std::unordered_map<std::string, std::string> compPinToNet; // "REF:PIN" -> netName
    std::unordered_map<std::string, int> netNameToId;

    struct PendingComp {
        std::string refDes;
        std::string deviceId;
        std::string shapeName;
        float xMm = 0.0f, yMm = 0.0f;
        float rotDeg = 0.0f;
        bool bottomSide = false;
        bool mirrored = false;
    };
    std::vector<PendingComp> pendingComps;

    std::string curPadName;
    std::string curShapeName;
    std::string curDevId;
    PendingComp curComp;
    bool hasCurComp = false;
    std::string curSignalName;
    std::string curRouteNet;
    float curRouteWidthMm = 0.25f;
    EdaLayerId curRouteLayer = EdaLayerId::TopCopper;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadPcb;

    auto flushComp = [&]() {
        if (hasCurComp && !curComp.refDes.empty()) {
            pendingComps.push_back(curComp);
        }
        curComp = PendingComp{};
        hasCurComp = false;
    };

    size_t linePos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(content, linePos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;
        // GenCAD comments start with '//' or '#' or 'REM'
        if (line.rfind("//", 0) == 0 || line.front() == '#') continue;

        std::string kwRaw;
        std::string_view rest = line;
        if (!DetailGenCad::NextGenCadToken(rest, kwRaw)) continue;
        std::string kw = ReaderText::ToUpperAscii(kwRaw);
        if (kw == "REM") continue;

        if (kw.front() == '$') {
            if (kw.rfind("$END", 0) == 0) {
                std::string closing = kw.substr(4);
                if (activeSection.empty() || activeSection != closing) {
                    return false; // Mismatched or stray $END...
                }
                if (closing == "HEADER") sawHeaderClose = true;
                if (closing == "COMPONENTS") flushComp();
                activeSection.clear();
                curPadName.clear();
                curShapeName.clear();
                curDevId.clear();
                curSignalName.clear();
                curRouteNet.clear();
            } else {
                if (!activeSection.empty()) return false; // Unclosed previous section!
                activeSection = kw.substr(1);
                if (activeSection == "HEADER") sawHeaderOpen = true;
            }
            continue;
        }

        if (activeSection == "HEADER") {
            if (kw == "GENCAD") {
                sawGenCadMagic = true;
            } else if (kw == "DRAWING") {
                DetailGenCad::NextGenCadToken(rest, drawingName);
            } else if (kw == "UNITS") {
                std::string uTok;
                if (DetailGenCad::NextGenCadToken(rest, uTok)) {
                    std::string uUp = ReaderText::ToUpperAscii(uTok);
                    if (uUp == "MM" || uUp == "MILLIMETER" || uUp == "MILLIMETERS") unitToMm = 1.0;
                    else if (uUp == "INCH" || uUp == "INCHES") unitToMm = 25.4;
                    else if (uUp == "THOU" || uUp == "MIL" || uUp == "MILS") unitToMm = 0.0254;
                    else if (uUp == "CM") unitToMm = 10.0;
                    else if (uUp == "USER") {
                        std::string scaleTok;
                        double perInch = 0.0;
                        if (!DetailGenCad::NextGenCadToken(rest, scaleTok) ||
                            !DetailGenCad::ParseTokDouble(scaleTok, perInch) || perInch <= 0.0) {
                            return false;
                        }
                        unitToMm = 25.4 / perInch;
                    } else {
                        return false;
                    }
                }
            }
        } else if (activeSection == "BOARD") {
            std::string t1, t2, t3, t4;
            double v1 = 0, v2 = 0, v3 = 0, v4 = 0;
            if (kw == "LINE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                DetailGenCad::NextGenCadToken(rest, t3) && DetailGenCad::NextGenCadToken(rest, t4) &&
                DetailGenCad::ParseTokDouble(t1, v1) && DetailGenCad::ParseTokDouble(t2, v2) &&
                DetailGenCad::ParseTokDouble(t3, v3) && DetailGenCad::ParseTokDouble(t4, v4)) {
                out.traces.push_back({
                    static_cast<float>(v1 * unitToMm), static_cast<float>(v2 * unitToMm),
                    static_cast<float>(v3 * unitToMm), static_cast<float>(v4 * unitToMm),
                    0.25f, EdaLayerId::EdgeCuts, 0
                });
            } else if (kw == "RECTANGLE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                       DetailGenCad::NextGenCadToken(rest, t3) && DetailGenCad::NextGenCadToken(rest, t4) &&
                       DetailGenCad::ParseTokDouble(t1, v1) && DetailGenCad::ParseTokDouble(t2, v2) &&
                       DetailGenCad::ParseTokDouble(t3, v3) && DetailGenCad::ParseTokDouble(t4, v4)) {
                float x = static_cast<float>(v1 * unitToMm), y = static_cast<float>(v2 * unitToMm);
                float w = static_cast<float>(v3 * unitToMm), h = static_cast<float>(v4 * unitToMm);
                DetailSiliconAndBusEda::AddRectOutline(out, x + w * 0.5f, y + h * 0.5f, w, h, EdaLayerId::EdgeCuts, 0.25f);
            } else if (kw == "CIRCLE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                       DetailGenCad::NextGenCadToken(rest, t3) &&
                       DetailGenCad::ParseTokDouble(t1, v1) && DetailGenCad::ParseTokDouble(t2, v2) &&
                       DetailGenCad::ParseTokDouble(t3, v3) && v3 > 0.0) {
                out.circles.push_back({
                    static_cast<float>(v1 * unitToMm), static_cast<float>(v2 * unitToMm),
                    static_cast<float>(v3 * unitToMm), 0.25f, false, EdaLayerId::EdgeCuts
                });
            }
        } else if (activeSection == "PADS") {
            if (kw == "PAD") {
                std::string pName, pType, pDrill;
                if (DetailGenCad::NextGenCadToken(rest, pName)) {
                    curPadName = pName;
                    DetailGenCad::PadDef pd{};
                    if (DetailGenCad::NextGenCadToken(rest, pType)) {
                        pd.type = ReaderText::ToUpperAscii(pType);
                        if (pd.type == "RECTANGLE" || pd.type == "SQUARE") pd.shape = 1;
                        else if (pd.type == "FINGER" || pd.type == "OVAL") pd.shape = 3;
                        else pd.shape = 0;
                    }
                    double drl = 0.0;
                    if (DetailGenCad::NextGenCadToken(rest, pDrill) && DetailGenCad::ParseTokDouble(pDrill, drl) && drl > 0.0) {
                        pd.drillMm = static_cast<float>(drl * unitToMm);
                    }
                    padDefs[curPadName] = pd;
                }
            } else if (!curPadName.empty()) {
                auto& pd = padDefs[curPadName];
                std::string t1, t2, t3, t4;
                double v1 = 0, v2 = 0, v3 = 0, v4 = 0;
                if (kw == "CIRCLE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                    DetailGenCad::NextGenCadToken(rest, t3) && DetailGenCad::ParseTokDouble(t3, v3) && v3 > 0.0) {
                    float d = static_cast<float>(v3 * 2.0 * unitToMm);
                    pd.wMm = pd.hMm = d;
                } else if (kw == "RECTANGLE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                           DetailGenCad::NextGenCadToken(rest, t3) && DetailGenCad::NextGenCadToken(rest, t4) &&
                           DetailGenCad::ParseTokDouble(t3, v3) && DetailGenCad::ParseTokDouble(t4, v4)) {
                    pd.wMm = std::max(0.1f, static_cast<float>( std::abs(v3) * unitToMm));
                    pd.hMm = std::max(0.1f, static_cast<float>( std::abs(v4) * unitToMm));
                } else if (kw == "SIZE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                           DetailGenCad::ParseTokDouble(t1, v1) && DetailGenCad::ParseTokDouble(t2, v2)) {
                    pd.wMm = std::max(0.1f, static_cast<float>(std::abs(v1) * unitToMm));
                    pd.hMm = std::max(0.1f, static_cast<float>(std::abs(v2) * unitToMm));
                }
            }
        } else if (activeSection == "SHAPES") {
            if (kw == "SHAPE") {
                DetailGenCad::NextGenCadToken(rest, curShapeName);
                if (!curShapeName.empty()) (void)shapeDefs[curShapeName];
            } else if (!curShapeName.empty()) {
                auto& sh = shapeDefs[curShapeName];
                std::string t1, t2, t3, t4, t5;
                double v1 = 0, v2 = 0, v3 = 0, v4 = 0;
                if (kw == "LINE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                    DetailGenCad::NextGenCadToken(rest, t3) && DetailGenCad::NextGenCadToken(rest, t4) &&
                    DetailGenCad::ParseTokDouble(t1, v1) && DetailGenCad::ParseTokDouble(t2, v2) &&
                    DetailGenCad::ParseTokDouble(t3, v3) && DetailGenCad::ParseTokDouble(t4, v4)) {
                    float x1 = static_cast<float>(v1 * unitToMm), y1 = static_cast<float>(v2 * unitToMm);
                    float x2 = static_cast<float>(v3 * unitToMm), y2 = static_cast<float>(v4 * unitToMm);
                    sh.lines.push_back({ x1, y1, x2, y2 });
                    sh.ExpandBounds(x1, y1);
                    sh.ExpandBounds(x2, y2);
                } else if (kw == "RECTANGLE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                           DetailGenCad::NextGenCadToken(rest, t3) && DetailGenCad::NextGenCadToken(rest, t4) &&
                           DetailGenCad::ParseTokDouble(t1, v1) && DetailGenCad::ParseTokDouble(t2, v2) &&
                           DetailGenCad::ParseTokDouble(t3, v3) && DetailGenCad::ParseTokDouble(t4, v4)) {
                    float x0 = static_cast<float>(v1 * unitToMm), y0 = static_cast<float>(v2 * unitToMm);
                    float x1 = x0 + static_cast<float>(v3 * unitToMm), y1 = y0 + static_cast<float>(v4 * unitToMm);
                    sh.lines.push_back({ x0, y0, x1, y0 });
                    sh.lines.push_back({ x1, y0, x1, y1 });
                    sh.lines.push_back({ x1, y1, x0, y1 });
                    sh.lines.push_back({ x0, y1, x0, y0 });
                    sh.ExpandBounds(x0, y0);
                    sh.ExpandBounds(x1, y1);
                } else if (kw == "PIN" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                           DetailGenCad::NextGenCadToken(rest, t3) && DetailGenCad::NextGenCadToken(rest, t4) &&
                           DetailGenCad::ParseTokDouble(t3, v1) && DetailGenCad::ParseTokDouble(t4, v2)) {
                    DetailGenCad::ShapePin sp{};
                    sp.pinName = t1;
                    sp.padName = t2;
                    sp.lx = static_cast<float>(v1 * unitToMm);
                    sp.ly = static_cast<float>(v2 * unitToMm);
                    if (DetailGenCad::NextGenCadToken(rest, t5)) {
                        sp.layer = ReaderText::ToUpperAscii(t5);
                    }
                    std::string tRot;
                    double rot = 0.0;
                    if (DetailGenCad::NextGenCadToken(rest, tRot) && DetailGenCad::ParseTokDouble(tRot, rot)) {
                        sp.rotDeg = static_cast<float>(rot);
                    }
                    sh.ExpandBounds(sp.lx, sp.ly);
                    sh.pins.push_back(std::move(sp));
                }
            }
        } else if (activeSection == "DEVICES") {
            if (kw == "DEVICE") {
                DetailGenCad::NextGenCadToken(rest, curDevId);
            } else if (!curDevId.empty()) {
                auto& dv = deviceDefs[curDevId];
                std::string val;
                if (kw == "PART" && DetailGenCad::NextGenCadToken(rest, val)) dv.part = val;
                else if (kw == "VALUE" && DetailGenCad::NextGenCadToken(rest, val)) dv.value = val;
                else if (kw == "TYPE" && DetailGenCad::NextGenCadToken(rest, val)) dv.type = val;
            }
        } else if (activeSection == "COMPONENTS") {
            if (kw == "COMPONENT") {
                flushComp();
                if (DetailGenCad::NextGenCadToken(rest, curComp.refDes)) {
                    hasCurComp = true;
                }
            } else if (hasCurComp) {
                std::string t1, t2;
                double v1 = 0, v2 = 0;
                if (kw == "DEVICE" && DetailGenCad::NextGenCadToken(rest, t1)) {
                    curComp.deviceId = t1;
                } else if (kw == "PLACE" && DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                           DetailGenCad::ParseTokDouble(t1, v1) && DetailGenCad::ParseTokDouble(t2, v2)) {
                    curComp.xMm = static_cast<float>(v1 * unitToMm);
                    curComp.yMm = static_cast<float>(v2 * unitToMm);
                } else if (kw == "LAYER" && DetailGenCad::NextGenCadToken(rest, t1)) {
                    std::string up = ReaderText::ToUpperAscii(t1);
                    curComp.bottomSide = (up == "BOTTOM" || up == "BOT");
                } else if (kw == "ROTATION" && DetailGenCad::NextGenCadToken(rest, t1) &&
                           DetailGenCad::ParseTokDouble(t1, v1)) {
                    curComp.rotDeg = static_cast<float>(v1);
                } else if (kw == "SHAPE" && DetailGenCad::NextGenCadToken(rest, t1)) {
                    curComp.shapeName = t1;
                    if (DetailGenCad::NextGenCadToken(rest, t2)) {
                        std::string mUp = ReaderText::ToUpperAscii(t2);
                        if (mUp == "MIRRORX" || mUp == "MIRROR") curComp.mirrored = true;
                    }
                }
            }
        } else if (activeSection == "SIGNALS") {
            if (kw == "SIGNAL") {
                if (DetailGenCad::NextGenCadToken(rest, curSignalName) && !curSignalName.empty()) {
                    if (netNameToId.find(curSignalName) == netNameToId.end()) {
                        int nid = static_cast<int>(netNameToId.size() + 1);
                        netNameToId[curSignalName] = nid;
                    }
                }
            } else if (kw == "NODE" && !curSignalName.empty()) {
                std::string cRef, cPin;
                if (DetailGenCad::NextGenCadToken(rest, cRef) && DetailGenCad::NextGenCadToken(rest, cPin)) {
                    compPinToNet[cRef + ":" + cPin] = curSignalName;
                }
            }
        } else if (activeSection == "TRACKS") {
            if (kw == "TRACK") {
                std::string trName, trW;
                double w = 0.0;
                if (DetailGenCad::NextGenCadToken(rest, trName) && DetailGenCad::NextGenCadToken(rest, trW) &&
                    DetailGenCad::ParseTokDouble(trW, w) && w > 0.0) {
                    trackWidthsMm[trName] = std::clamp(static_cast<float>(w * unitToMm), 0.05f, 10.0f);
                }
            }
        } else if (activeSection == "ROUTES") {
            if (kw == "ROUTE") {
                if (DetailGenCad::NextGenCadToken(rest, curRouteNet) && !curRouteNet.empty()) {
                    if (netNameToId.find(curRouteNet) == netNameToId.end()) {
                        int nid = static_cast<int>(netNameToId.size() + 1);
                        netNameToId[curRouteNet] = nid;
                    }
                }
            } else if (kw == "TRACK") {
                std::string trTok;
                if (DetailGenCad::NextGenCadToken(rest, trTok)) {
                    auto it = trackWidthsMm.find(trTok);
                    double rawW = 0.0;
                    if (it != trackWidthsMm.end()) {
                        curRouteWidthMm = it->second;
                    } else if (DetailGenCad::ParseTokDouble(trTok, rawW) && rawW > 0.0) {
                        curRouteWidthMm = std::clamp(static_cast<float>(rawW * unitToMm), 0.05f, 10.0f);
                    }
                }
            } else if (kw == "LAYER") {
                std::string lTok;
                if (DetailGenCad::NextGenCadToken(rest, lTok)) {
                    curRouteLayer = DetailGenCad::MapGenCadLayer(ReaderText::ToUpperAscii(lTok));
                }
            } else if (kw == "WIRE") {
                std::string t1, t2, t3, t4;
                double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                if (DetailGenCad::NextGenCadToken(rest, t1) && DetailGenCad::NextGenCadToken(rest, t2) &&
                    DetailGenCad::NextGenCadToken(rest, t3) && DetailGenCad::NextGenCadToken(rest, t4) &&
                    DetailGenCad::ParseTokDouble(t1, x1) && DetailGenCad::ParseTokDouble(t2, y1) &&
                    DetailGenCad::ParseTokDouble(t3, x2) && DetailGenCad::ParseTokDouble(t4, y2)) {
                    int nid = curRouteNet.empty() ? 0 : netNameToId[curRouteNet];
                    out.traces.push_back({
                        static_cast<float>(x1 * unitToMm), static_cast<float>(y1 * unitToMm),
                        static_cast<float>(x2 * unitToMm), static_cast<float>(y2 * unitToMm),
                        curRouteWidthMm, curRouteLayer, nid
                    });
                }
            } else if (kw == "VIA") {
                std::string vPad, tx, ty;
                double vx = 0, vy = 0;
                if (DetailGenCad::NextGenCadToken(rest, vPad) &&
                    DetailGenCad::NextGenCadToken(rest, tx) && DetailGenCad::NextGenCadToken(rest, ty) &&
                    DetailGenCad::ParseTokDouble(tx, vx) && DetailGenCad::ParseTokDouble(ty, vy)) {
                    EdaViaPrim via{};
                    via.x = static_cast<float>(vx * unitToMm);
                    via.y = static_cast<float>(vy * unitToMm);
                    auto pit = padDefs.find(vPad);
                    if (pit != padDefs.end()) {
                        via.diameter = std::max(0.3f, pit->second.wMm);
                        via.drill = pit->second.drillMm > 0.0f ? pit->second.drillMm : via.diameter * 0.5f;
                    }
                    std::string layerSetTok, drlTok;
                    double drl = 0.0;
                    if (DetailGenCad::NextGenCadToken(rest, layerSetTok) &&
                        DetailGenCad::NextGenCadToken(rest, drlTok) &&
                        DetailGenCad::ParseTokDouble(drlTok, drl) && drl > 0.0) {
                        via.drill = static_cast<float>(drl * unitToMm);
                    }
                    via.netId = curRouteNet.empty() ? 0 : netNameToId[curRouteNet];
                    out.vias.push_back(via);
                }
            }
        }
    }

    if (!activeSection.empty()) return false; // Unterminated $SECTION!
    if (!sawHeaderOpen || !sawHeaderClose || !sawGenCadMagic) return false;

    // Instantiate placed components, silk outlines, and transformed pads
    for (const auto& pc : pendingComps) {
        const DetailGenCad::ShapeDef* sh = nullptr;
        auto sit = shapeDefs.find(pc.shapeName);
        if (sit != shapeDefs.end()) sh = &sit->second;

        const DetailGenCad::DeviceDef* dv = nullptr;
        auto dit = deviceDefs.find(pc.deviceId);
        if (dit != deviceDefs.end()) dv = &dit->second;

        double rad = pc.rotDeg * (3.14159265358979323846 / 180.0);
        double cr = std::cos(rad), sr = std::sin(rad);
        float xSign = (pc.bottomSide || pc.mirrored) ? -1.0f : 1.0f;

        auto xformPt = [&](float lx, float ly, float& ox, float& oy) {
            double mx = static_cast<double>(lx * xSign);
            double my = static_cast<double>(ly);
            ox = pc.xMm + static_cast<float>(mx * cr - my * sr);
            oy = pc.yMm + static_cast<float>(mx * sr + my * cr);
        };

        EdaLayerId silkLayer = pc.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
        if (sh) {
            for (const auto& ln : sh->lines) {
                float wx1 = 0, wy1 = 0, wx2 = 0, wy2 = 0;
                xformPt(ln.x1, ln.y1, wx1, wy1);
                xformPt(ln.x2, ln.y2, wx2, wy2);
                out.traces.push_back({ wx1, wy1, wx2, wy2, 0.15f, silkLayer, 0 });
            }
            for (const auto& sp : sh->pins) {
                EdaPadPrim pad{};
                xformPt(sp.lx, sp.ly, pad.x, pad.y);
                pad.rotDeg = std::fmod(pc.rotDeg + sp.rotDeg, 360.0f);
                auto pit = padDefs.find(sp.padName);
                if (pit != padDefs.end()) {
                    pad.w = pit->second.wMm;
                    pad.h = pit->second.hMm;
                    pad.drill = pit->second.drillMm;
                    pad.shape = pit->second.shape;
                }
                bool isThru = (pad.drill > 0.0f || sp.layer == "ALL");
                if (isThru) {
                    pad.layer = EdaLayerId::PadsThrough;
                } else {
                    bool pinBot = (sp.layer == "BOTTOM") ^ pc.bottomSide;
                    pad.layer = pinBot ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                }
                pad.pinName = PathUtils::Utf8ToWide(sp.pinName);
                auto nit = compPinToNet.find(pc.refDes + ":" + sp.pinName);
                if (nit != compPinToNet.end()) {
                    pad.netName = PathUtils::Utf8ToWide(nit->second);
                }
                out.pads.push_back(std::move(pad));
            }
        }

        EdaComponent3D comp{};
        comp.refDes = PathUtils::Utf8ToWide(pc.refDes);
        std::string valStr = (dv && !dv->value.empty()) ? dv->value : ((dv && !dv->part.empty()) ? dv->part : pc.deviceId);
        comp.value = PathUtils::Utf8ToWide(valStr);
        comp.footprint = PathUtils::Utf8ToWide(pc.shapeName);
        comp.x = pc.xMm;
        comp.y = pc.yMm;
        comp.rotDeg = pc.rotDeg;
        comp.bottomSide = pc.bottomSide;
        if (sh && sh->hasBounds) {
            comp.widthMm  = std::max(1.0f, sh->maxX - sh->minX);
            comp.lengthMm = std::max(1.0f, sh->maxY - sh->minY);
            comp.pinCount = std::max(1, static_cast<int>(sh->pins.size()));
        }
        comp.category = DetailGenCad::GuessCategory(pc.refDes, dv ? dv->type : "");
        out.components.push_back(std::move(comp));

        out.labels.push_back({ pc.xMm, pc.yMm, 1.2f, silkLayer, PathUtils::Utf8ToWide(pc.refDes) });
    }

    out.netCount = netNameToId.size();

    if (out.traces.empty() && out.pads.empty() && out.components.empty() &&
        out.vias.empty() && out.circles.empty()) {
        return false;
    }

    out.formatTitle = L"IPC-2511 / Mitron GenCAD 1.4 PCB (.gencad)";
    if (!drawingName.empty()) {
        out.formatTitle += L" — " + PathUtils::Utf8ToWide(drawingName);
    }
    return true;
}

} // namespace Formats
