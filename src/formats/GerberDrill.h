#pragma once

static bool ParseGerberOrDrill(std::string_view txt, const std::wstring& ext, ParsedEdaBoard& out) {
    bool isDrill = (ext == L".drl" || ext == L".xln" || txt.rfind("M48", 0) == 0);
    out.kind = isDrill ? EdaDocKind::ExcellonDrill : EdaDocKind::GerberLayer;
    out.formatTitle = isDrill ? L"Excellon NC Drill (.drl)" : (L"Gerber RS-274X (" + PathUtils::ToUpper(ext) + L")");

    EdaLayerId defaultLayer = EdaLayerId::TopCopper;
    if (ext == L".gbl" || ext == L".gbs" || ext == L".gbo") defaultLayer = EdaLayerId::BottomCopper;
    else if (ext == L".gto" || ext == L".gts") defaultLayer = EdaLayerId::TopSilk;
    else if (ext == L".gm1" || ext == L".gko" || ext == L".oln") defaultLayer = EdaLayerId::EdgeCuts;

    double coordScale = 0.0001; // Default 2.4 format or 10^-4
    bool metric = true;
    float curX = 0.0f, curY = 0.0f;
    float curApertureDiam = 0.25f;
    std::unordered_map<int, float> apertures;

    size_t lineStart = 0;
    while (lineStart < txt.size()) {
        size_t lineEnd = txt.find_first_of("\r\n*", lineStart);
        if (lineEnd == std::string_view::npos) lineEnd = txt.size();
        std::string_view ln = txt.substr(lineStart, lineEnd - lineStart);
        lineStart = (lineEnd < txt.size() && txt[lineEnd] == '*') ? (lineEnd + 1) : (lineEnd + 1);
        while (lineStart < txt.size() && (txt[lineStart] == '\r' || txt[lineStart] == '\n' || txt[lineStart] == '%')) ++lineStart;

        if (ln.empty()) continue;

        if (ln.find("MOIN") != std::string_view::npos || ln == "INCH") {
            metric = false;
            continue;
        }
        if (ln.find("MOMM") != std::string_view::npos || ln == "METRIC") {
            metric = true;
            continue;
        }
        size_t fsPos = ln.find("FSLAX");
        if (fsPos != std::string_view::npos && fsPos + 7 < ln.size()) {
            int decDigits = ln[fsPos + 6] - '0';
            if (decDigits >= 1 && decDigits <= 6) {
                coordScale = std::pow(10.0, -decDigits);
            }
            continue;
        }
        if (ln.rfind("ADD", 0) == 0 && ln.size() > 5) {
            int dCode = std::atoi(std::string(ln.substr(3)).c_str());
            size_t comma = ln.find(',');
            if (dCode >= 10 && comma != std::string_view::npos) {
                float diam = std::strtof(std::string(ln.substr(comma + 1)).c_str(), nullptr);
                if (!metric) diam *= 25.4f;
                apertures[dCode] = std::max(0.08f, diam);
            }
            continue;
        }
        if (ln[0] == 'T' && ln.size() >= 2 && ln[1] >= '0' && ln[1] <= '9') {
            int tCode = std::atoi(std::string(ln.substr(1)).c_str());
            size_t cPos = ln.find('C');
            if (cPos != std::string_view::npos) {
                float diam = std::strtof(std::string(ln.substr(cPos + 1)).c_str(), nullptr);
                if (!metric) diam *= 25.4f;
                apertures[tCode] = std::max(0.15f, diam);
                curApertureDiam = apertures[tCode];
            } else if (apertures.count(tCode)) {
                curApertureDiam = apertures[tCode];
            }
            continue;
        }
        if ((ln[0] == 'D' || (ln.rfind("G54D", 0) == 0)) && ln.find('X') == std::string_view::npos && ln.find('Y') == std::string_view::npos) {
            size_t dPos = ln.find('D');
            int dCode = std::atoi(std::string(ln.substr(dPos + 1)).c_str());
            if (dCode >= 10 && apertures.count(dCode)) {
                curApertureDiam = apertures[dCode];
            }
            continue;
        }

        // Coordinate command: X...Y...D01 / D02 / D03
        size_t xPos = ln.find('X');
        size_t yPos = ln.find('Y');
        if (xPos != std::string_view::npos || yPos != std::string_view::npos) {
            float nx = curX, ny = curY;
            auto parseCoord = [&](size_t p) -> float {
                std::string s;
                for (size_t i = p + 1; i < ln.size(); ++i) {
                    char c = ln[i];
                    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') s.push_back(c);
                    else break;
                }
                if (s.empty()) return 0.0f;
                double v = std::strtod(s.c_str(), nullptr);
                if (s.find('.') == std::string::npos && !isDrill) {
                    v *= coordScale;
                } else if (s.find('.') == std::string::npos && isDrill) {
                    v *= 0.001;
                }
                if (!metric) v *= 25.4;
                return static_cast<float>(v);
            };
            if (xPos != std::string_view::npos) nx = parseCoord(xPos);
            if (yPos != std::string_view::npos) ny = parseCoord(yPos);

            if (isDrill || ln.find("D03") != std::string_view::npos || ln.find("D3") != std::string_view::npos) {
                if (isDrill) {
                    out.vias.push_back({ nx, ny, curApertureDiam * 1.4f, curApertureDiam, 0 });
                } else {
                    out.pads.push_back({ nx, ny, curApertureDiam, curApertureDiam, 0.0f, 0.0f, 0, defaultLayer, L"", L"" });
                }
            } else if (ln.find("D01") != std::string_view::npos || ln.find("D1") != std::string_view::npos) {
                out.traces.push_back({ curX, curY, nx, ny, std::max(0.12f, curApertureDiam), defaultLayer, 0 });
            }
            curX = nx;
            curY = ny;
        }
    }

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.pads.empty() || !out.vias.empty());
    return out.valid;
}
