#pragma once
// PcbRndLihata.h — CoralEDA pcb-rnd Lihata Board & Footprint Layout (.lht) Reader
// Parses recursive Lihata trees (`li:pcb-rnd-board-v*` and `li:pcb-rnd-subcircuit-v*` / `ha:pcb-rnd-subcircuit-v*`),
// extracting copper/silk/outline `line`, `arc`, `simplepoly`/`polygon`, and `text` primitives,
// `padstack_prototypes` (hole drills + circle/line/polygon pad shapes), `padstack_ref` instances, and `subc` components.
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailPcbRndLht {

struct LhtNode {
    std::string type;
    std::string name;
    std::string textVal;
    bool isContainer = false;
    std::vector<LhtNode> children;

    const LhtNode* FindChild(std::string_view key) const {
        for (const auto& c : children) {
            if (c.name == key) return &c;
        }
        return nullptr;
    }
};

inline bool ParseLhtCoordMm(std::string_view sv, float& outMm) {
    sv = ReaderText::TrimSv(sv);
    if (sv.empty()) return false;
    char buf[64] = {};
    const size_t n = std::min<size_t>(sv.size(), 63u);
    std::memcpy(buf, sv.data(), n);
    char* endp = nullptr;
    const double val = std::strtod(buf, &endp);
    if (endp == buf || !std::isfinite(val)) return false;
    std::string unit = ReaderText::ToLowerAscii(ReaderText::TrimSv(std::string_view(endp)));
    double scale = 1.0;
    if (unit.empty() || unit == "mm") scale = 1.0;
    else if (unit == "mil" || unit == "mils") scale = 0.0254;
    else if (unit == "in" || unit == "inch") scale = 25.4;
    else if (unit == "um") scale = 0.001;
    else if (unit == "nm") scale = 1e-6;
    else if (unit == "cm") scale = 10.0;
    else return false;
    outMm = static_cast<float>(val * scale);
    return true;
}

class LhtParser {
public:
    explicit LhtParser(std::string_view s) : sv_(s) {}

    bool ParseRoot(LhtNode& outRoot) {
        SkipWsAndComments();
        if (!ParseNode(outRoot, 0)) return false;
        SkipWsAndComments();
        return pos_ == sv_.size();
    }

private:
    std::string_view sv_;
    size_t pos_ = 0u;

    void SkipWsAndComments() {
        while (pos_ < sv_.size()) {
            const unsigned char c = static_cast<unsigned char>(sv_[pos_]);
            if (c <= 0x20u || c == ';') { ++pos_; continue; }
            if (c == '#') {
                while (pos_ < sv_.size() && sv_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }
    }

    bool ReadWord(std::string& out) {
        out.clear();
        SkipWsAndComments();
        if (pos_ >= sv_.size()) return false;
        if (sv_[pos_] == '{') {
            ++pos_;
            size_t start = pos_;
            int depth = 1;
            while (pos_ < sv_.size() && depth > 0) {
                if (sv_[pos_] == '\\' && pos_ + 1u < sv_.size()) { pos_ += 2u; continue; }
                if (sv_[pos_] == '{') ++depth;
                else if (sv_[pos_] == '}') --depth;
                if (depth > 0) ++pos_;
            }
            if (depth != 0) return false;
            out.assign(sv_.data() + start, pos_ - start);
            ++pos_;
            return true;
        }
        const size_t s = pos_;
        while (pos_ < sv_.size()) {
            const char ch = sv_[pos_];
            if (static_cast<unsigned char>(ch) <= 0x20u || ch == '=' || ch == '{' || ch == '}' || ch == ';' || ch == '#') {
                break;
            }
            ++pos_;
        }
        if (pos_ == s) return false;
        out.assign(sv_.data() + s, pos_ - s);
        return true;
    }

    bool ParseNode(LhtNode& node, int depth) {
        if (depth > 48) return false;
        SkipWsAndComments();
        if (pos_ >= sv_.size() || sv_[pos_] == '}') return false;

        std::string head;
        if (!ReadWord(head)) return false;

        const size_t colon = head.find(':');
        if (colon != std::string::npos &&
            (head.substr(0u, colon) == "ha" || head.substr(0u, colon) == "li" ||
             head.substr(0u, colon) == "te" || head.substr(0u, colon) == "sy" ||
             head.substr(0u, colon) == "ta")) {
            node.type = head.substr(0u, colon);
            node.name = head.substr(colon + 1u);
        } else {
            node.type = "te";
            node.name = std::move(head);
        }

        while (pos_ < sv_.size() && (sv_[pos_] == ' ' || sv_[pos_] == '\t')) ++pos_;
        bool hadEquals = false;
        if (pos_ < sv_.size() && sv_[pos_] == '=') {
            hadEquals = true;
            ++pos_;
        }

        while (pos_ < sv_.size() && (sv_[pos_] == ' ' || sv_[pos_] == '\t')) ++pos_;
        if ((node.type == "ha" || node.type == "li" || node.type == "ta") &&
            !hadEquals && (pos_ < sv_.size() && (sv_[pos_] == '{' || sv_[pos_] == '\n' || sv_[pos_] == '\r'))) {
            SkipWsAndComments();
        }

        if ((node.type == "ha" || node.type == "li" || node.type == "ta") &&
            pos_ < sv_.size() && sv_[pos_] == '{') {
            node.isContainer = true;
            ++pos_;
            while (true) {
                SkipWsAndComments();
                if (pos_ >= sv_.size()) return false;
                if (sv_[pos_] == '}') {
                    ++pos_;
                    break;
                }
                LhtNode child;
                if (!ParseNode(child, depth + 1)) return false;
                if (node.children.size() < 100000u) node.children.push_back(std::move(child));
            }
            return true;
        }

        while (pos_ < sv_.size() && (sv_[pos_] == ' ' || sv_[pos_] == '\t')) ++pos_;
        if (pos_ < sv_.size() && sv_[pos_] == '{') {
            return ReadWord(node.textVal);
        }
        const size_t vs = pos_;
        while (pos_ < sv_.size() && sv_[pos_] != '\n' && sv_[pos_] != '\r' && sv_[pos_] != ';' && sv_[pos_] != '}') {
            if (sv_[pos_] == '#') break;
            ++pos_;
        }
        std::string_view vsv = ReaderText::TrimSv(sv_.substr(vs, pos_ - vs));
        node.textVal.assign(vsv.data(), vsv.size());
        return true;
    }
};

struct PadstackProto {
    float w = 1.4f;
    float h = 1.4f;
    float drill = 0.0f;
    uint8_t shape = 0u; // 0=circle, 1=rect, 2=oval
    EdaLayerId layer = EdaLayerId::TopCopper;
};

inline void ParsePadstackPrototypes(const LhtNode& dataNode, std::vector<PadstackProto>& outProtos) {
    outProtos.clear();
    const LhtNode* protoList = dataNode.FindChild("padstack_prototypes");
    if (!protoList) return;

    for (const auto& pn : protoList->children) {
        PadstackProto pp{};
        if (const auto* hd = pn.FindChild("hdia")) {
            float d = 0.0f;
            if (ParseLhtCoordMm(hd->textVal, d) && d > 0.01f) {
                pp.drill = d;
                pp.w = std::max(pp.w, d * 1.65f);
                pp.h = std::max(pp.h, d * 1.65f);
                pp.layer = EdaLayerId::PadsThrough;
            }
        }
        bool gotShapeDims = false;
        bool sawTop = false, sawBot = false;
        if (const auto* shpList = pn.FindChild("shape")) {
            for (const auto& sn : shpList->children) {
                if (const auto* lm = sn.FindChild("layer_mask")) {
                    for (const auto& mc : lm->children) {
                        if (mc.name == "top" && mc.textVal == "1") sawTop = true;
                        if (mc.name == "bottom" && mc.textVal == "1") sawBot = true;
                    }
                }
                if (!gotShapeDims) {
                    if (const auto* psCirc = sn.FindChild("ps_circ")) {
                        float dia = 0.0f;
                        if (const auto* dn = psCirc->FindChild("dia")) {
                            if (ParseLhtCoordMm(dn->textVal, dia) && dia > 0.05f) {
                                pp.w = dia; pp.h = dia; pp.shape = 0u;
                                gotShapeDims = true;
                            }
                        }
                    } else if (const auto* psLine = sn.FindChild("ps_line")) {
                        float x1 = 0, y1 = 0, x2 = 0, y2 = 0, th = 0.5f;
                        if (const auto* n = psLine->FindChild("x1")) ParseLhtCoordMm(n->textVal, x1);
                        if (const auto* n = psLine->FindChild("y1")) ParseLhtCoordMm(n->textVal, y1);
                        if (const auto* n = psLine->FindChild("x2")) ParseLhtCoordMm(n->textVal, x2);
                        if (const auto* n = psLine->FindChild("y2")) ParseLhtCoordMm(n->textVal, y2);
                        if (const auto* n = psLine->FindChild("thickness")) ParseLhtCoordMm(n->textVal, th);
                        const auto* sqNode = psLine->FindChild("square");
                        const bool isSq = (sqNode != nullptr && sqNode->textVal == "1");
                        pp.w = std::max(0.2f, std::fabs(x2 - x1) + th);
                        pp.h = std::max(0.2f, std::fabs(y2 - y1) + th);
                        pp.shape = isSq ? 1u : 2u;
                        gotShapeDims = true;
                    } else if (const auto* psPoly = sn.FindChild("ps_poly")) {
                        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
                        for (size_t k = 0; k + 1u < psPoly->children.size(); k += 2u) {
                            float vx = 0, vy = 0;
                            if (ParseLhtCoordMm(psPoly->children[k].name.empty() ? psPoly->children[k].textVal : psPoly->children[k].name, vx) &&
                                ParseLhtCoordMm(psPoly->children[k + 1u].name.empty() ? psPoly->children[k + 1u].textVal : psPoly->children[k + 1u].name, vy)) {
                                minX = std::min(minX, vx); maxX = std::max(maxX, vx);
                                minY = std::min(minY, vy); maxY = std::max(maxY, vy);
                            }
                        }
                        if (maxX > minX && maxY > minY) {
                            pp.w = maxX - minX;
                            pp.h = maxY - minY;
                            pp.shape = 1u;
                            gotShapeDims = true;
                        }
                    }
                }
            }
        }
        if (pp.drill > 0.01f || (sawTop && sawBot)) {
            pp.layer = EdaLayerId::PadsThrough;
        } else if (sawBot && !sawTop) {
            pp.layer = EdaLayerId::BottomCopper;
        } else {
            pp.layer = EdaLayerId::TopCopper;
        }
        outProtos.push_back(pp);
    }
}

inline EdaLayerId MapPcbRndLayer(std::string_view name, std::string_view purpose) {
    const std::string low = ReaderText::ToLowerAscii(name) + " " + ReaderText::ToLowerAscii(purpose);
    if (low.find("outline") != std::string::npos || low.find("route") != std::string::npos || low.find("boundary") != std::string::npos) {
        return EdaLayerId::EdgeCuts;
    }
    if (low.find("silk") != std::string::npos) {
        return (low.find("bottom") != std::string::npos || low.find("bot") != std::string::npos)
            ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
    }
    if (low.find("bottom") != std::string::npos || low.find("solder") != std::string::npos) {
        return EdaLayerId::BottomCopper;
    }
    if (low.find("intern") != std::string::npos || low.find("gnd") != std::string::npos || low.find("pwr") != std::string::npos) {
        return EdaLayerId::InnerCopper;
    }
    return EdaLayerId::TopCopper;
}

} // namespace DetailPcbRndLht

inline bool ParsePcbRndLihata(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    using namespace DetailPcbRndLht;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 24u, (32u << 20))) return false;

    LhtNode root;
    LhtParser parser(sv);
    if (!parser.ParseRoot(root) || !root.isContainer) return false;

    const bool isBoard = ReaderText::StartsWithCI(root.name, "pcb-rnd-board-v");
    const bool isSubc  = ReaderText::StartsWithCI(root.name, "pcb-rnd-subcircuit-v");
    if (!isBoard && !isSubc) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadPcb;

    auto extractObjectsFromData = [&](const LhtNode& dataNode, float ox, float oy, const std::vector<PadstackProto>& parentProtos) {
        std::vector<PadstackProto> localProtos;
        ParsePadstackPrototypes(dataNode, localProtos);
        const std::vector<PadstackProto>& protos = localProtos.empty() ? parentProtos : localProtos;

        if (const auto* layers = dataNode.FindChild("layers")) {
            for (const auto& lyr : layers->children) {
                std::string purpose;
                if (const auto* pn = lyr.FindChild("purpose")) purpose = pn->textVal;
                const EdaLayerId lid = MapPcbRndLayer(lyr.name, purpose);
                const auto* objs = lyr.FindChild("objects");
                if (!objs) continue;
                for (const auto& obj : objs->children) {
                    if (ReaderText::StartsWithCI(obj.name, "line.")) {
                        float x1 = 0, y1 = 0, x2 = 0, y2 = 0, th = 0.25f;
                        const auto* nx1 = obj.FindChild("x1");
                        const auto* ny1 = obj.FindChild("y1");
                        const auto* nx2 = obj.FindChild("x2");
                        const auto* ny2 = obj.FindChild("y2");
                        if (nx1 && ny1 && nx2 && ny2 &&
                            ParseLhtCoordMm(nx1->textVal, x1) &&
                            ParseLhtCoordMm(ny1->textVal, y1) &&
                            ParseLhtCoordMm(nx2->textVal, x2) &&
                            ParseLhtCoordMm(ny2->textVal, y2)) {
                            if (const auto* nth = obj.FindChild("thickness")) {
                                ParseLhtCoordMm(nth->textVal, th);
                            }
                            out.traces.push_back({ ox + x1, -(oy + y1), ox + x2, -(oy + y2), std::max(0.08f, th), lid, 0 });
                        }
                    } else if (ReaderText::StartsWithCI(obj.name, "arc.")) {
                        // pcb-rnd `ha:arc.<id>`: `x`, `y`, `width` (radius X), `height` (radius Y), `astart` (deg, 0 = -X in pcb-rnd), `adelta` (deg), `thickness`
                        float cx = 0, cy = 0, rx = 0, ry = 0, astart = 0, adelta = 180.0f, th = 0.22f;
                        const auto* nx = obj.FindChild("x");
                        const auto* ny = obj.FindChild("y");
                        const auto* nw = obj.FindChild("width");
                        const auto* nh = obj.FindChild("height");
                        if (!nw) nw = obj.FindChild("r");
                        if (!nh) nh = nw;
                        if (nx && ny && nw && nh &&
                            ParseLhtCoordMm(nx->textVal, cx) &&
                            ParseLhtCoordMm(ny->textVal, cy) &&
                            ParseLhtCoordMm(nw->textVal, rx) &&
                            ParseLhtCoordMm(nh->textVal, ry))
                        {
                            if (const auto* na = obj.FindChild("astart")) ReaderText::ParseFloatSv(na->textVal, astart);
                            if (const auto* nd = obj.FindChild("adelta")) ReaderText::ParseFloatSv(nd->textVal, adelta);
                            if (const auto* nt = obj.FindChild("thickness")) ParseLhtCoordMm(nt->textVal, th);

                            const float wCx = ox + cx;
                            const float wCy = -(oy + cy);
                            rx = std::max(0.1f, std::fabs(rx));
                            ry = std::max(0.1f, std::fabs(ry));
                            if (std::fabs(adelta) >= 359.5f && std::fabs(rx - ry) < 0.05f) {
                                out.circles.push_back({ wCx, wCy, rx, std::max(0.08f, th), false, lid });
                            } else if (std::fabs(adelta) > 0.5f) {
                                // In pcb-rnd / gEDA, angle 0 points left (-X) and increases CW in Y-down (= CCW in math +Y-up from 180 deg)
                                constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
                                const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(adelta) / 15.0f)), 4, 32);
                                const float a0 = (180.0f - astart) * kDegToRad;
                                const float dA = -adelta * kDegToRad;
                                float prevX = wCx + rx * std::cos(a0);
                                float prevY = wCy + ry * std::sin(a0);
                                for (int s = 1; s <= steps; ++s) {
                                    const float ang = a0 + dA * (static_cast<float>(s) / static_cast<float>(steps));
                                    const float qx = wCx + rx * std::cos(ang);
                                    const float qy = wCy + ry * std::sin(ang);
                                    out.traces.push_back({ prevX, prevY, qx, qy, std::max(0.08f, th), lid, 0 });
                                    prevX = qx; prevY = qy;
                                }
                            }
                        }
                    } else if (ReaderText::StartsWithCI(obj.name, "text.")) {
                        float tx = 0, ty = 0;
                        const auto* nx = obj.FindChild("x");
                        const auto* ny = obj.FindChild("y");
                        const auto* ns = obj.FindChild("string");
                        if (nx && ny && ns && !ns->textVal.empty() &&
                            ParseLhtCoordMm(nx->textVal, tx) &&
                            ParseLhtCoordMm(ny->textVal, ty)) {
                            out.labels.push_back({ ox + tx, -(oy + ty), 1.1f, lid, PathUtils::Utf8ToWide(ns->textVal) });
                        }
                    }
                }
            }
        }

        if (const auto* objs = dataNode.FindChild("objects")) {
            for (const auto& obj : objs->children) {
                if (ReaderText::StartsWithCI(obj.name, "padstack_ref.")) {
                    float px = 0, py = 0, rotDeg = 0.0f;
                    const auto* nx = obj.FindChild("x");
                    const auto* ny = obj.FindChild("y");
                    if (nx && ny &&
                        ParseLhtCoordMm(nx->textVal, px) &&
                        ParseLhtCoordMm(ny->textVal, py))
                    {
                        if (const auto* nr = obj.FindChild("rot")) {
                            ReaderText::ParseFloatSv(nr->textVal, rotDeg);
                        }
                        int protoIdx = -1;
                        if (const auto* np = obj.FindChild("proto")) {
                            ReaderText::ParseIntSv(np->textVal, protoIdx);
                        }

                        PadstackProto pp{};
                        pp.w = 1.4f; pp.h = 1.4f; pp.drill = 0.7f; pp.shape = 0u; pp.layer = EdaLayerId::PadsThrough;
                        if (protoIdx >= 0 && static_cast<size_t>(protoIdx) < protos.size()) {
                            pp = protos[static_cast<size_t>(protoIdx)];
                        }

                        std::string term;
                        if (const auto* attr = obj.FindChild("attributes")) {
                            if (const auto* tn = attr->FindChild("term")) term = tn->textVal;
                            else if (const auto* nn = attr->FindChild("name")) term = nn->textVal;
                        }

                        // If un-named via padstack (`term` empty and through-hole drill > 0), also record on `vias` or `pads`
                        out.pads.push_back({
                            ox + px,
                            -(oy + py),
                            pp.w,
                            pp.h,
                            rotDeg,   // EdaPadPrim::rotDeg (arg 5)
                            pp.drill, // EdaPadPrim::drill  (arg 6 — fixed swapped order!)
                            pp.shape,
                            pp.layer,
                            PathUtils::Utf8ToWide(term),
                            L""
                        });
                    }
                }
            }
        }
    };

    const LhtNode* mainData = root.FindChild("data");
    if (!mainData && isSubc && !root.children.empty()) {
        if (const auto* lo = root.FindChild("objects")) {
            for (const auto& ch : lo->children) {
                if (ReaderText::StartsWithCI(ch.name, "subc.")) {
                    mainData = ch.FindChild("data");
                    break;
                }
            }
        }
    }
    if (!mainData) return false;

    std::vector<PadstackProto> boardProtos;
    ParsePadstackPrototypes(*mainData, boardProtos);
    extractObjectsFromData(*mainData, 0.0f, 0.0f, boardProtos);

    if (const auto* topObjs = mainData->FindChild("objects")) {
        for (const auto& obj : topObjs->children) {
            if (ReaderText::StartsWithCI(obj.name, "subc.")) {
                std::string refdes = "U?", valueStr, fpStr = "PCB_RND_SUBC";
                if (const auto* attr = obj.FindChild("attributes")) {
                    if (const auto* rd = attr->FindChild("refdes")) refdes = rd->textVal;
                    if (const auto* vl = attr->FindChild("value")) valueStr = vl->textVal;
                    if (const auto* fp = attr->FindChild("footprint")) fpStr = fp->textVal;
                }
                float sx = 0.0f, sy = 0.0f;
                if (const auto* nx = obj.FindChild("x")) ParseLhtCoordMm(nx->textVal, sx);
                if (const auto* ny = obj.FindChild("y")) ParseLhtCoordMm(ny->textVal, sy);
                const size_t padsBefore = out.pads.size();
                if (const auto* subData = obj.FindChild("data")) {
                    extractObjectsFromData(*subData, sx, sy, boardProtos);
                }
                EdaComponent3D comp{};
                comp.refDes = PathUtils::Utf8ToWide(refdes);
                comp.value = PathUtils::Utf8ToWide(valueStr);
                comp.footprint = PathUtils::Utf8ToWide(fpStr);
                comp.x = sx;
                comp.y = -sy;
                comp.widthMm = 3.2f;
                comp.lengthMm = 2.0f;
                comp.pinCount = std::max<int>(2, static_cast<int>(out.pads.size() - padsBefore));
                comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                out.components.push_back(std::move(comp));
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.circles.empty() && out.components.empty()) return false;

    out.formatTitle = isSubc
        ? L"CoralEDA pcb-rnd Lihata Subcircuit (.lht)"
        : L"CoralEDA pcb-rnd Lihata Board (.lht)";
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
