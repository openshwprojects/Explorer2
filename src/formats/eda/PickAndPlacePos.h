#pragma once
// PickAndPlacePos.h — SMT Pick-and-Place Centroid Assembly (.pos / .cpl / .mnt) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailKiCadSymSesPos {

// Split a CSV or whitespace Pick-and-Place line while honoring double-quoted strings containing commas/spaces
inline std::vector<std::string> SplitPickAndPlaceRow(std::string_view ln, bool csvDelimited) {
    std::vector<std::string> out;
    if (csvDelimited) {
        char sep = (ln.find(',') != std::string_view::npos) ? ',' : ((ln.find(';') != std::string_view::npos) ? ';' : '\t');
        size_t i = 0;
        while (i <= ln.size()) {
            while (i < ln.size() && (ln[i] == ' ' || (sep != '\t' && ln[i] == '\t'))) ++i;
            std::string cell;
            if (i < ln.size() && ln[i] == '"') {
                ++i;
                while (i < ln.size()) {
                    if (ln[i] == '"') {
                        if (i + 1 < ln.size() && ln[i + 1] == '"') { cell.push_back('"'); i += 2; }
                        else { ++i; break; }
                    } else {
                        cell.push_back(ln[i++]);
                    }
                }
                while (i < ln.size() && ln[i] != sep) ++i;
            } else {
                size_t e = ln.find(sep, i);
                if (e == std::string_view::npos) e = ln.size();
                cell.assign(ln.data() + i, e - i);
                i = e;
            }
            while (!cell.empty() && static_cast<unsigned char>(cell.back()) <= 0x20u) cell.pop_back();
            while (!cell.empty() && static_cast<unsigned char>(cell.front()) <= 0x20u) cell.erase(cell.begin());
            out.push_back(std::move(cell));
            if (i >= ln.size()) break;
            ++i; // consume separator
        }
        return out;
    }

    size_t i = 0;
    while (i < ln.size()) {
        while (i < ln.size() && static_cast<unsigned char>(ln[i]) <= 0x20u) ++i;
        if (i >= ln.size()) break;
        std::string tok;
        if (ln[i] == '"') {
            ++i;
            while (i < ln.size() && ln[i] != '"') tok.push_back(ln[i++]);
            if (i < ln.size()) ++i;
        } else {
            while (i < ln.size() && static_cast<unsigned char>(ln[i]) > 0x20u) tok.push_back(ln[i++]);
        }
        out.push_back(std::move(tok));
    }
    return out;
}

inline bool ParsePnpCoordTokenMm(std::string_view raw, float defaultUnitToMm, float& outMm) {
    std::string s(raw);
    while (!s.empty() && (static_cast<unsigned char>(s.front()) <= 0x20u || s.front() == '"')) s.erase(s.begin());
    while (!s.empty() && (static_cast<unsigned char>(s.back()) <= 0x20u || s.back() == '"')) s.pop_back();
    if (s.empty()) return false;

    char* endp = nullptr;
    double val = std::strtod(s.c_str(), &endp);
    if (!endp || endp == s.c_str() || !std::isfinite(val)) return false;

    std::string unit = LowerAsciiSymSes(endp);
    while (!unit.empty() && static_cast<unsigned char>(unit.front()) <= 0x20u) unit.erase(unit.begin());

    double scale = defaultUnitToMm;
    if (unit.empty()) {
        scale = defaultUnitToMm;
    } else if (unit == "mm") {
        scale = 1.0;
    } else if (unit == "mil" || unit == "mils" || unit == "th") {
        scale = 0.0254;
    } else if (unit == "in" || unit == "inch" || unit == "inches") {
        scale = 25.4;
    } else if (unit == "um") {
        scale = 0.001;
    } else {
        return false; // Reject non-coordinate tokens like "100nF" or "10k"!
    }

    outMm = static_cast<float>(val * scale);
    return std::fabs(outMm) < 50000.0f;
}

inline bool LooksLikeValidRefDes(const std::string& ref) {
    if (ref.empty() || ref.size() > 24) return false;
    if (!std::isalpha(static_cast<unsigned char>(ref[0])) && ref[0] != '#') return false;
    bool hasDigitOrShort = false;
    for (size_t i = 0; i < ref.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(ref[i]);
        if (std::isdigit(uc) || ref[i] == '?') hasDigitOrShort = true;
        else if (!std::isalnum(uc) && ref[i] != '_' && ref[i] != '-' && ref[i] != '#') return false;
    }
    return hasDigitOrShort || ref.size() <= 4;
}

inline int ExtractTrailingPinCount(const std::string& pkgUpper, int defPins) {
    size_t xPos = pkgUpper.find('X');
    if (xPos != std::string::npos && xPos > 0 && xPos + 1 < pkgUpper.size() &&
        std::isdigit(static_cast<unsigned char>(pkgUpper[xPos - 1])) &&
        std::isdigit(static_cast<unsigned char>(pkgUpper[xPos + 1])))
    {
        size_t s = xPos - 1;
        while (s > 0 && std::isdigit(static_cast<unsigned char>(pkgUpper[s - 1]))) --s;
        int r = std::atoi(pkgUpper.c_str() + s);
        int c = std::atoi(pkgUpper.c_str() + xPos + 1);
        if (r >= 1 && r <= 8 && c >= 1 && c <= 64) return r * c;
    }
    for (size_t i = 0; i < pkgUpper.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(pkgUpper[i]))) {
            size_t j = i;
            while (j < pkgUpper.size() && std::isdigit(static_cast<unsigned char>(pkgUpper[j]))) ++j;
            int n = std::atoi(pkgUpper.substr(i, j - i).c_str());
            if (n >= 3 && n <= 256 && n != 402 && n != 603 && n != 805 && n != 1206 && n != 2512 && n != 23 && n != 223) {
                return n;
            }
            i = j;
        }
    }
    return defPins;
}

} // namespace DetailKiCadSymSesPos

inline bool ParsePickAndPlacePosBoard(std::string_view sv, const std::wstring& ext, ParsedEdaBoard& out) {
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu &&
        static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3);
    }

    // #63. SMT Pick-and-Place Centroid File (.pos / .cpl / .mnt)
    if (ext == L".pos" || ext == L".cpl" || ext == L".mnt") {
        using namespace DetailKiCadSymSesPos;
        for (unsigned char c : sv) {
            if (c == 0) return false;
        }

        float defaultUnitToMm = 1.0f;
        int colRef = -1, colVal = -1, colPkg = -1, colX = -1, colY = -1, colRot = -1, colSide = -1;
        bool hasExplicitHeader = false;

        out.kind = EdaDocKind::KiCadPcb;

        size_t pos = 0;
        while (pos < sv.size() && out.components.size() < 8000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            if (ln.empty()) continue;

            if (ln.front() == '#' || ln.rfind("//", 0) == 0) {
                std::string lowHdr = LowerAsciiSymSes(ln);
                if (lowHdr.find("unit") != std::string::npos) {
                    if (lowHdr.find("inch") != std::string::npos || lowHdr.find(" = in") != std::string::npos) defaultUnitToMm = 25.4f;
                    else if (lowHdr.find("mil") != std::string::npos) defaultUnitToMm = 0.0254f;
                    else if (lowHdr.find("mm") != std::string::npos) defaultUnitToMm = 1.0f;
                }
                continue;
            }

            const bool isCsvLine = (ln.find(',') != std::string_view::npos || ln.find(';') != std::string_view::npos);
            std::vector<std::string> cols = SplitPickAndPlaceRow(ln, isCsvLine);
            if (cols.size() < 4) continue;

            std::string c0Low = LowerAsciiSymSes(cols[0]);
            if (c0Low == "ref" || c0Low == "refdes" || c0Low == "designator" || c0Low == "part" || c0Low == "component") {
                colRef = 0;
                colVal = colPkg = colX = colY = colRot = colSide = -1;
                for (size_t ci = 1; ci < cols.size(); ++ci) {
                    std::string h = LowerAsciiSymSes(cols[ci]);
                    if (h.find("(mil)") != std::string::npos) defaultUnitToMm = 0.0254f;
                    else if (h.find("(in)") != std::string::npos || h.find("(inch)") != std::string::npos) defaultUnitToMm = 25.4f;
                    else if (h.find("(mm)") != std::string::npos) defaultUnitToMm = 1.0f;

                    if (colX < 0 && (h == "posx" || h == "mid x" || h == "midx" || h == "x" || h.find("center-x") != std::string::npos || h.find("ref x") != std::string::npos || h.find("pad x") != std::string::npos)) {
                        colX = static_cast<int>(ci);
                    } else if (colY < 0 && (h == "posy" || h == "mid y" || h == "midy" || h == "y" || h.find("center-y") != std::string::npos || h.find("ref y") != std::string::npos || h.find("pad y") != std::string::npos)) {
                        colY = static_cast<int>(ci);
                    } else if (colRot < 0 && (h.find("rot") != std::string::npos || h.find("angle") != std::string::npos || h == "orient")) {
                        colRot = static_cast<int>(ci);
                    } else if (colSide < 0 && (h == "side" || h == "layer" || h == "tb")) {
                        colSide = static_cast<int>(ci);
                    } else if (colPkg < 0 && (h.find("pack") != std::string::npos || h.find("foot") != std::string::npos || h == "pattern")) {
                        colPkg = static_cast<int>(ci);
                    } else if (colVal < 0 && (h == "val" || h == "value" || h == "comment" || h == "device")) {
                        colVal = static_cast<int>(ci);
                    }
                }
                if (colX >= 0 && colY >= 0) hasExplicitHeader = true;
                continue;
            }

            std::string ref, valStr, pkgStr;
            float px = 0.0f, py = 0.0f, rot = 0.0f;
            bool bottom = false;

            if (hasExplicitHeader) {
                int maxReq = std::max(colRef, std::max(colX, colY));
                if (static_cast<int>(cols.size()) <= maxReq) continue;
                ref = cols[static_cast<size_t>(colRef)];
                if (!LooksLikeValidRefDes(ref)) continue;
                if (!ParsePnpCoordTokenMm(cols[static_cast<size_t>(colX)], defaultUnitToMm, px) ||
                    !ParsePnpCoordTokenMm(cols[static_cast<size_t>(colY)], defaultUnitToMm, py)) {
                    continue;
                }
                if (colVal >= 0 && colVal < static_cast<int>(cols.size())) valStr = cols[static_cast<size_t>(colVal)];
                if (colPkg >= 0 && colPkg < static_cast<int>(cols.size())) pkgStr = cols[static_cast<size_t>(colPkg)];
                if (colRot >= 0 && colRot < static_cast<int>(cols.size())) {
                    rot = std::strtof(cols[static_cast<size_t>(colRot)].c_str(), nullptr);
                }
                if (colSide >= 0 && colSide < static_cast<int>(cols.size())) {
                    std::string sl = LowerAsciiSymSes(cols[static_cast<size_t>(colSide)]);
                    bottom = (sl == "bottom" || sl == "bot" || sl == "b" || sl == "back" || sl == "bottomlayer");
                }
            } else {
                ref = cols[0];
                if (!LooksLikeValidRefDes(ref)) continue;

                float tx = 0.0f, ty = 0.0f;
                if (cols.size() >= 5 &&
                    ParsePnpCoordTokenMm(cols[1], defaultUnitToMm, tx) &&
                    ParsePnpCoordTokenMm(cols[2], defaultUnitToMm, ty))
                {
                    px = tx; py = ty;
                    std::string c3Low = LowerAsciiSymSes(cols[3]);
                    if (c3Low == "top" || c3Low == "bottom" || c3Low == "bot" || c3Low == "t" || c3Low == "b") {
                        bottom = (c3Low == "bottom" || c3Low == "bot" || c3Low == "b");
                        rot = std::strtof(cols[4].c_str(), nullptr);
                        if (cols.size() >= 6) valStr = cols[5];
                    } else {
                        rot = std::strtof(cols[3].c_str(), nullptr);
                        valStr = cols[4];
                        if (cols.size() >= 6) pkgStr = cols[5];
                    }
                } else if (cols.size() >= 6 &&
                           ParsePnpCoordTokenMm(cols[3], defaultUnitToMm, tx) &&
                           ParsePnpCoordTokenMm(cols[4], defaultUnitToMm, ty))
                {
                    valStr = cols[1];
                    pkgStr = cols[2];
                    px = tx; py = ty;
                    rot = std::strtof(cols[5].c_str(), nullptr);
                    if (cols.size() >= 7) {
                        std::string c6Low = LowerAsciiSymSes(cols[6]);
                        bottom = (c6Low == "bottom" || c6Low == "bot" || c6Low == "b" || c6Low == "back");
                    }
                } else {
                    continue;
                }
            }

            if (pkgStr.empty()) pkgStr = "SMD_0805";

            EdaComponent3D c;
            c.refDes = PathUtils::Utf8ToWide(ref);
            c.value = PathUtils::Utf8ToWide(valStr);
            c.footprint = PathUtils::Utf8ToWide(pkgStr);
            c.x = px;
            c.y = py;
            c.rotDeg = rot;
            c.bottomSide = bottom;
            c.widthMm = 2.0f;
            c.lengthMm = 1.25f;
            c.heightMm = 0.8f;
            c.pinCount = 2;
            c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);

            std::string pkgUp = pkgStr;
            for (char& ch : pkgUp) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            c.pinCount = ExtractTrailingPinCount(pkgUp, c.pinCount);

            const float rad = rot * 3.14159265f / 180.0f;
            const float cs = std::cos(rad), sn = std::sin(rad);
            auto emitRotPad = [&](float lx, float ly, float pw, float ph, float padLocalRot, uint8_t shape, int pinNo) {
                float wx = px + (lx * cs - ly * sn);
                float wy = py + (lx * sn + ly * cs);
                EdaLayerId padL = bottom ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                out.pads.push_back({ wx, wy, pw, ph, rot + padLocalRot, 0.0f, shape, padL, std::to_wstring(pinNo), L"" });
            };

            if (pkgUp.find("BGA") != std::string::npos) {
                int gridN = std::clamp(static_cast<int>(std::round(std::sqrt(static_cast<double>(std::max(4, c.pinCount))))), 2, 8);
                float pitch = 0.8f;
                c.widthMm = std::max(c.widthMm, static_cast<float>(gridN) * pitch + 1.0f);
                c.lengthMm = c.widthMm;
                int pno = 1;
                for (int r = 0; r < gridN; ++r) {
                    for (int cl = 0; cl < gridN; ++cl) {
                        float lx = (static_cast<float>(cl) - (gridN - 1) * 0.5f) * pitch;
                        float ly = (static_cast<float>(r)  - (gridN - 1) * 0.5f) * pitch;
                        emitRotPad(lx, ly, 0.42f, 0.42f, 0.0f, 0, pno++);
                    }
                }
                c.pinCount = gridN * gridN;
            } else if (pkgUp.find("QFP") != std::string::npos || pkgUp.find("QFN") != std::string::npos) {
                int perSide = std::clamp(std::max(16, c.pinCount) / 4, 3, 20);
                float pitch = (perSide >= 12) ? 0.5f : 0.65f;
                float bodySpan = static_cast<float>(perSide) * pitch + 1.2f;
                c.widthMm = bodySpan;
                c.lengthMm = bodySpan;
                float halfSpan = bodySpan * 0.5f;
                int pno = 1;
                for (int i = 0; i < perSide; ++i) {
                    float ofs = (static_cast<float>(i) - (perSide - 1) * 0.5f) * pitch;
                    emitRotPad(-halfSpan, -ofs, 0.70f, pitch * 0.55f, 0.0f, 1, pno++);
                }
                for (int i = 0; i < perSide; ++i) {
                    float ofs = (static_cast<float>(i) - (perSide - 1) * 0.5f) * pitch;
                    emitRotPad(ofs, -halfSpan, pitch * 0.55f, 0.70f, 0.0f, 1, pno++);
                }
                for (int i = 0; i < perSide; ++i) {
                    float ofs = (static_cast<float>(i) - (perSide - 1) * 0.5f) * pitch;
                    emitRotPad(halfSpan, ofs, 0.70f, pitch * 0.55f, 0.0f, 1, pno++);
                }
                for (int i = 0; i < perSide; ++i) {
                    float ofs = (static_cast<float>(i) - (perSide - 1) * 0.5f) * pitch;
                    emitRotPad(-ofs, halfSpan, pitch * 0.55f, 0.70f, 0.0f, 1, pno++);
                }
                c.pinCount = perSide * 4;
            } else if (c.category == EdaPartCategory::TransistorSOT && c.pinCount <= 3) {
                emitRotPad(-0.95f, -1.0f, 0.60f, 0.70f, 0.0f, 1, 1);
                emitRotPad( 0.95f, -1.0f, 0.60f, 0.70f, 0.0f, 1, 2);
                emitRotPad( 0.0f,   1.0f, 0.60f, 0.70f, 0.0f, 1, 3);
                c.pinCount = 3;
            } else if (c.pinCount >= 4 && (c.category == EdaPartCategory::IC_Package || c.category == EdaPartCategory::TransistorSOT || c.category == EdaPartCategory::Connector)) {
                int half = std::clamp((c.pinCount + 1) / 2, 2, 24);
                float pitch = (pkgUp.find("TSSOP") != std::string::npos || pkgUp.find("MSOP") != std::string::npos) ? 0.65f
                            : (c.category == EdaPartCategory::Connector ? 2.54f : 1.27f);
                c.lengthMm = std::max(c.lengthMm, static_cast<float>(half) * pitch + 0.6f);
                float rowX = c.widthMm * 0.5f;
                int pno = 1;
                for (int i = 0; i < half; ++i) {
                    float ly = ((half - 1) * 0.5f - static_cast<float>(i)) * pitch;
                    emitRotPad(-rowX, ly, 0.85f, pitch * 0.52f, 0.0f, 1, pno++);
                }
                for (int i = 0; i < half; ++i) {
                    float ly = (static_cast<float>(i) - (half - 1) * 0.5f) * pitch;
                    emitRotPad(rowX, ly, 0.85f, pitch * 0.52f, 0.0f, 1, pno++);
                }
            } else {
                float dx = c.widthMm * 0.44f;
                float pw = std::clamp(c.widthMm * 0.34f, 0.30f, 1.4f);
                float ph = std::clamp(c.lengthMm * 0.86f, 0.35f, 2.2f);
                emitRotPad(-dx, 0.0f, pw, ph, 0.0f, 1, 1);
                emitRotPad( dx, 0.0f, pw, ph, 0.0f, 1, 2);
            }

            float hw = c.widthMm * 0.5f, hl = c.lengthMm * 0.5f;
            auto rotCorner = [&](float lx, float ly) -> std::pair<float, float> {
                return { px + (lx * cs - ly * sn), py + (lx * sn + ly * cs) };
            };
            auto k0 = rotCorner(-hw, -hl), k1 = rotCorner(hw, -hl), k2 = rotCorner(hw, hl), k3 = rotCorner(-hw, hl);
            EdaLayerId silkL = bottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
            out.traces.push_back({ k0.first, k0.second, k1.first, k1.second, 0.14f, silkL, 0 });
            out.traces.push_back({ k1.first, k1.second, k2.first, k2.second, 0.14f, silkL, 0 });
            out.traces.push_back({ k2.first, k2.second, k3.first, k3.second, 0.14f, silkL, 0 });
            out.traces.push_back({ k3.first, k3.second, k0.first, k0.second, 0.14f, silkL, 0 });
            out.labels.push_back({ px, py, 0.85f, silkL, c.refDes });

            out.components.push_back(std::move(c));
        }

        if (out.components.empty()) {
            out = ParsedEdaBoard{};
            return false;
        }

        float mnX = out.components[0].x, mxX = mnX, mnY = out.components[0].y, mxY = mnY;
        for (const auto& c : out.components) {
            float span = std::max(c.widthMm, c.lengthMm) * 0.5f + 3.0f;
            mnX = std::min(mnX, c.x - span); mxX = std::max(mxX, c.x + span);
            mnY = std::min(mnY, c.y - span); mxY = std::max(mxY, c.y + span);
        }
        DetailEda100::AddRectOutline(out, (mnX + mxX) * 0.5f, (mnY + mxY) * 0.5f, mxX - mnX, mxY - mnY, EdaLayerId::EdgeCuts, 0.36f);
        out.formatTitle = L"SMT Pick-and-Place Assembly (" + ext + L" — " + std::to_wstring(out.components.size()) + L" parts)";
        out.netCount = out.components.size();
        return true;
    }
    return false;
}

} // namespace Formats
