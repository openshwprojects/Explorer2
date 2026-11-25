#pragma once
// MagicVlsiMag.h — UC Berkeley / OpenLane Magic VLSI CMOS Physical Layout (.mag) reader
// Parses `magic` header, `tech`, `magscale`, `<< layer >>` sections (`rect`, `rlabel`,
// hierarchical `use`/`transform`/`box`), and mandatory `<< end >>` footer.

namespace Formats {

namespace DetailMagicVlsi {

inline EdaLayerId MapMagicLayer(std::string_view lyr, bool& outIsViaCut) {
    outIsViaCut = false;
    std::string low = ReaderText::ToLowerAscii(lyr);
    if (low.find("via") != std::string::npos || low.find("contact") != std::string::npos ||
        low == "mcon" || low == "licon" || low == "viali" || low == "ndcontact" || low == "pdcontact" || low == "polycontact") {
        outIsViaCut = true;
        return EdaLayerId::PadsThrough;
    }
    if (low.find("poly") != std::string::npos) return EdaLayerId::SchSymbol;
    if (low == "li1" || low == "locali" || low == "met1" || low == "metal1" || low == "m1") return EdaLayerId::TopCopper;
    if (low == "met2" || low == "metal2" || low == "m2") return EdaLayerId::BottomCopper;
    if (low.rfind("met", 0) == 0 || low.rfind("m", 0) == 0) return EdaLayerId::InnerCopper;
    if (low.find("diff") != std::string::npos || low.find("tap") != std::string::npos) return EdaLayerId::TopSilk;
    if (low.find("well") != std::string::npos) return EdaLayerId::EdgeCuts;
    return EdaLayerId::InnerCopper;
}

} // namespace DetailMagicVlsi

inline bool ParseMagicVlsiMag(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 16 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    size_t pos = 0;
    std::string_view line;
    bool sawMagicHeader = false;
    bool sawEndFooter = false;
    std::string techName = "cmos";
    float scaleUm = 0.05f; // Default lambda -> visual mm/um scale

    std::string curLayer;
    EdaLayerId curLayerId = EdaLayerId::TopCopper;
    bool curIsVia = false;

    // Pending `use <cell> <inst>` hierarchical subcell state
    std::string pendingCell, pendingInst;
    float useXf[6] = { 1, 0, 0, 0, 1, 0 }; // Magic: x' = a*x + b*y + c; y' = d*x + e*y + f

    size_t rectCount = 0;
    std::set<std::string> portNames;

    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        if (!sawMagicHeader) {
            if (line != "magic") return false;
            sawMagicHeader = true;
            continue;
        }

        if (line.size() >= 6 && line.substr(0, 2) == "<<" && line.substr(line.size() - 2) == ">>") {
            curLayer = std::string(ReaderText::TrimSv(line.substr(2, line.size() - 4)));
            if (ReaderText::AsciiEqCI(curLayer, "end")) {
                sawEndFooter = true;
                break;
            }
            curLayerId = DetailMagicVlsi::MapMagicLayer(curLayer, curIsVia);
            continue;
        }

        std::string_view rem = line, cmd;
        if (!ReaderText::NextTokenSv(rem, cmd)) continue;

        if (cmd == "tech") {
            std::string_view t;
            if (ReaderText::NextTokenSv(rem, t)) techName = std::string(t);
        } else if (cmd == "magscale") {
            std::string_view tn, td;
            float n = 1.0f, d = 1.0f;
            if (ReaderText::NextTokenSv(rem, tn) && ReaderText::ParseFloatSv(tn, n) &&
                ReaderText::NextTokenSv(rem, td) && ReaderText::ParseFloatSv(td, d) && d > 0.0f && n > 0.0f) {
                // Keep coordinates comfortable in viewer mm space (~0.05..1.0 per lambda)
                scaleUm = std::clamp((n / d) * 0.05f, 0.002f, 1.0f);
            }
        } else if (cmd == "rect") {
            std::string_view tx0, ty0, tx1, ty1;
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (!ReaderText::NextTokenSv(rem, tx0) || !ReaderText::ParseFloatSv(tx0, x0) ||
                !ReaderText::NextTokenSv(rem, ty0) || !ReaderText::ParseFloatSv(ty0, y0) ||
                !ReaderText::NextTokenSv(rem, tx1) || !ReaderText::ParseFloatSv(tx1, x1) ||
                !ReaderText::NextTokenSv(rem, ty1) || !ReaderText::ParseFloatSv(ty1, y1)) {
                return false;
            }
            if (x1 <= x0 || y1 <= y0) return false;
            x0 *= scaleUm; y0 *= scaleUm; x1 *= scaleUm; y1 *= scaleUm;
            const float cx = (x0 + x1) * 0.5f;
            const float cy = (y0 + y1) * 0.5f;
            const float w = x1 - x0;
            const float h = y1 - y0;
            ++rectCount;

            if (curIsVia) {
                const float dia = std::max(w, h);
                out.vias.push_back({ cx, cy, dia, dia * 0.55f, 0 });
            } else {
                // Draw crisp filled-look Manhattan rectangle + centerline bar
                DrawEdaBoxRect(out, x0, y0, x1, y1, std::clamp(std::min(w, h) * 0.18f, 0.05f, 0.35f), curLayerId, 0);
                if (w >= h) {
                    out.traces.push_back({ x0 + h * 0.25f, cy, x1 - h * 0.25f, cy, std::min(h * 0.7f, 2.5f), curLayerId, 0 });
                } else {
                    out.traces.push_back({ cx, y0 + w * 0.25f, cx, y1 - w * 0.25f, std::min(w * 0.7f, 2.5f), curLayerId, 0 });
                }
            }
        } else if (cmd == "rlabel" || cmd == "flabel") {
            // rlabel <layer> <x0> <y0> <x1> <y1> <pos> <text...>
            std::string_view tl, tx0, ty0, tx1, ty1, tpos;
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (ReaderText::NextTokenSv(rem, tl) &&
                ReaderText::NextTokenSv(rem, tx0) && ReaderText::ParseFloatSv(tx0, x0) &&
                ReaderText::NextTokenSv(rem, ty0) && ReaderText::ParseFloatSv(ty0, y0) &&
                ReaderText::NextTokenSv(rem, tx1) && ReaderText::ParseFloatSv(tx1, x1) &&
                ReaderText::NextTokenSv(rem, ty1) && ReaderText::ParseFloatSv(ty1, y1) &&
                ReaderText::NextTokenSv(rem, tpos))
            {
                std::string_view lbl = ReaderText::TrimSv(rem);
                // flabel has extra rotation/size/offset tokens before last token; take last token if spaces remain
                const size_t lastSp = lbl.find_last_of(" \t");
                if (cmd == "flabel" && lastSp != std::string_view::npos) lbl = lbl.substr(lastSp + 1);
                if (!lbl.empty()) {
                    portNames.insert(std::string(lbl));
                    const float cx = (x0 + x1) * 0.5f * scaleUm;
                    const float cy = (y0 + y1) * 0.5f * scaleUm;
                    out.labels.push_back({ cx, cy, 0.65f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(lbl) });
                    EdaPadPrim pin;
                    pin.x = cx; pin.y = cy;
                    pin.w = std::max(0.25f, (x1 - x0) * scaleUm);
                    pin.h = std::max(0.25f, (y1 - y0) * scaleUm);
                    pin.shape = 1;
                    pin.layer = EdaLayerId::TopCopper;
                    pin.pinName = PathUtils::Utf8ToWide(lbl);
                    out.pads.push_back(std::move(pin));
                }
            }
        } else if (cmd == "use") {
            std::string_view tc, ti;
            if (ReaderText::NextTokenSv(rem, tc)) pendingCell = std::string(tc);
            if (ReaderText::NextTokenSv(rem, ti)) pendingInst = std::string(ti);
            else pendingInst = pendingCell;
            useXf[0] = 1; useXf[1] = 0; useXf[2] = 0;
            useXf[3] = 0; useXf[4] = 1; useXf[5] = 0;
        } else if (cmd == "transform") {
            for (int k = 0; k < 6; ++k) {
                std::string_view tv;
                if (ReaderText::NextTokenSv(rem, tv)) ReaderText::ParseFloatSv(tv, useXf[k]);
            }
        } else if (cmd == "box" && !pendingCell.empty()) {
            std::string_view tx0, ty0, tx1, ty1;
            float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
            if (ReaderText::NextTokenSv(rem, tx0) && ReaderText::ParseFloatSv(tx0, bx0) &&
                ReaderText::NextTokenSv(rem, ty0) && ReaderText::ParseFloatSv(ty0, by0) &&
                ReaderText::NextTokenSv(rem, tx1) && ReaderText::ParseFloatSv(tx1, bx1) &&
                ReaderText::NextTokenSv(rem, ty1) && ReaderText::ParseFloatSv(ty1, by1))
            {
                auto xfPt = [&](float lx, float ly) -> std::pair<float, float> {
                    return {
                        (useXf[0] * lx + useXf[1] * ly + useXf[2]) * scaleUm,
                        (useXf[3] * lx + useXf[4] * ly + useXf[5]) * scaleUm
                    };
                };
                auto p0 = xfPt(bx0, by0);
                auto p1 = xfPt(bx1, by1);
                const float minX = std::min(p0.first, p1.first), maxX = std::max(p0.first, p1.first);
                const float minY = std::min(p0.second, p1.second), maxY = std::max(p0.second, p1.second);
                DrawEdaBoxRect(out, minX, minY, maxX, maxY, 0.16f, EdaLayerId::EdgeCuts, 0);

                EdaComponent3D sub;
                sub.refDes = PathUtils::Utf8ToWide(pendingInst);
                sub.value = PathUtils::Utf8ToWide(pendingCell);
                sub.footprint = L"VLSI_CELL";
                sub.x = (minX + maxX) * 0.5f;
                sub.y = (minY + maxY) * 0.5f;
                sub.widthMm = std::max(0.5f, maxX - minX);
                sub.lengthMm = std::max(0.5f, maxY - minY);
                sub.heightMm = 0.6f;
                sub.category = EdaPartCategory::IC_Package;
                out.labels.push_back({ sub.x, sub.y, 0.7f, EdaLayerId::SchLabel, sub.refDes + L":" + sub.value });
                out.components.push_back(std::move(sub));
                pendingCell.clear();
            }
        }
    }

    if (!sawMagicHeader || !sawEndFooter) return false;
    if (rectCount == 0 && out.components.empty()) return false;

    out.kind = EdaDocKind::KiCadPcb;
    out.netCount = portNames.size();
    wchar_t title[160] = {};
    swprintf_s(title, L"Magic VLSI Layout [%hs] (%zu rects, %zu vias, %zu ports)",
               techName.c_str(), rectCount, out.vias.size(), portNames.size());
    out.formatTitle = title;
    return true;
}

} // namespace Formats
