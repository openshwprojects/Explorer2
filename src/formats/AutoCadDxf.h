#pragma once

static bool ParseAutoCadDxf(std::string_view txt, ParsedEdaBoard& out) {
    out.kind = EdaDocKind::AutoCadDxf;
    out.formatTitle = L"AutoCAD Drawing Exchange (.dxf)";

    std::vector<std::string_view> lines;
    lines.reserve(4096);
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string_view::npos) eol = txt.size();
        std::string_view ln = txt.substr(pos, eol - pos);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        lines.push_back(ln);
        pos = eol + 1;
    }

    bool hasEntitiesSec = false;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        if (lines[i] == "2" && lines[i + 1] == "ENTITIES") {
            hasEntitiesSec = true;
            break;
        }
    }

    bool inEntitiesSec = !hasEntitiesSec;
    bool awaitingSecName = false;
    std::string curEnt;
    std::string curLayer = "0";
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, x3 = 0.0f, y3 = 0.0f, x4 = 0.0f, y4 = 0.0f;
    bool hasPt3 = false, hasPt4 = false;
    float radius = 1.0f, startAng = 0.0f, endAng = 360.0f;
    int polyFlags = 0;
    std::string textVal;
    std::vector<std::pair<float, float>> lwPts;
    float pendingLwX = 0.0f;
    bool hasPendingLwX = false;

    auto flushEntity = [&]() {
        if (curEnt.empty()) return;
        if (hasPendingLwX) {
            lwPts.push_back({ pendingLwX, y1 });
            hasPendingLwX = false;
        }
        EdaLayerId lid = MapDxfLayerToEda(curLayer);
        if (curEnt == "LINE") {
            out.traces.push_back({ x1, -y1, x2, -y2, 0.25f, lid, 0 });
        } else if (curEnt == "LWPOLYLINE" || curEnt == "POLYLINE" || curEnt == "SPLINE") {
            for (size_t k = 0; k + 1 < lwPts.size(); ++k) {
                out.traces.push_back({ lwPts[k].first, -lwPts[k].second, lwPts[k + 1].first, -lwPts[k + 1].second, 0.25f, lid, 0 });
            }
            if ((polyFlags & 1) != 0 && lwPts.size() >= 3) {
                out.traces.push_back({ lwPts.back().first, -lwPts.back().second, lwPts.front().first, -lwPts.front().second, 0.25f, lid, 0 });
            }
        } else if (curEnt == "SOLID" || curEnt == "3DFACE") {
            out.traces.push_back({ x1, -y1, x2, -y2, 0.25f, lid, 0 });
            if (hasPt3) {
                out.traces.push_back({ x2, -y2, x3, -y3, 0.25f, lid, 0 });
                if (hasPt4) {
                    out.traces.push_back({ x3, -y3, x4, -y4, 0.25f, lid, 0 });
                    out.traces.push_back({ x4, -y4, x1, -y1, 0.25f, lid, 0 });
                } else {
                    out.traces.push_back({ x3, -y3, x1, -y1, 0.25f, lid, 0 });
                }
            }
        } else if (curEnt == "CIRCLE" || curEnt == "ELLIPSE") {
            float r = (curEnt == "ELLIPSE") ? std::max(0.2f, std::hypot(x2, y2)) : std::max(0.1f, radius);
            out.circles.push_back({ x1, -y1, r, 0.22f, false, lid });
        } else if (curEnt == "POINT") {
            out.vias.push_back({ x1, -y1, 0.6f, 0.25f, 0 });
        } else if (curEnt == "ARC") {
            float a0 = startAng * 3.14159265f / 180.0f;
            float a1 = endAng * 3.14159265f / 180.0f;
            if (a1 < a0) a1 += 6.2831853f;
            int segs = 18;
            for (int s = 0; s < segs; ++s) {
                float t0 = a0 + (a1 - a0) * (static_cast<float>(s) / segs);
                float t1 = a0 + (a1 - a0) * (static_cast<float>(s + 1) / segs);
                out.traces.push_back({
                    x1 + std::cos(t0) * radius, -(y1 + std::sin(t0) * radius),
                    x1 + std::cos(t1) * radius, -(y1 + std::sin(t1) * radius),
                    0.22f, lid, 0
                });
            }
        } else if ((curEnt == "TEXT" || curEnt == "MTEXT") && !textVal.empty()) {
            out.labels.push_back({ x1, -y1, std::max(0.8f, radius), EdaLayerId::TopSilk, PathUtils::Utf8ToWide(textVal) });
        }

        curEnt.clear();
        curLayer = "0";
        x1 = y1 = x2 = y2 = x3 = y3 = x4 = y4 = 0.0f;
        hasPt3 = hasPt4 = false;
        radius = 1.0f; startAng = 0.0f; endAng = 360.0f;
        polyFlags = 0;
        textVal.clear();
        lwPts.clear();
        hasPendingLwX = false;
    };

    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        int code = std::atoi(std::string(lines[i]).c_str());
        std::string_view val = lines[i + 1];
        if (code == 0) {
            if (val == "SECTION") {
                flushEntity();
                awaitingSecName = true;
                continue;
            }
            if (val == "ENDSEC") {
                flushEntity();
                if (hasEntitiesSec) inEntitiesSec = false;
                continue;
            }
            if (!inEntitiesSec) continue;
            if (val == "VERTEX") {
                if (hasPendingLwX) {
                    lwPts.push_back({ pendingLwX, y1 });
                    hasPendingLwX = false;
                }
                continue;
            }
            if (val == "SEQEND") {
                flushEntity();
                continue;
            }
            flushEntity();
            if (val == "LINE" || val == "LWPOLYLINE" || val == "POLYLINE" || val == "SPLINE" ||
                val == "SOLID" || val == "3DFACE" || val == "CIRCLE" || val == "ELLIPSE" ||
                val == "ARC" || val == "POINT" || val == "TEXT" || val == "MTEXT") {
                curEnt.assign(val);
            }
            continue;
        }
        if (awaitingSecName && code == 2) {
            awaitingSecName = false;
            if (hasEntitiesSec) {
                inEntitiesSec = (val == "ENTITIES");
            }
            continue;
        }
        if (!inEntitiesSec || curEnt.empty()) continue;

        if (code == 8) {
            curLayer.assign(val);
        } else if (code == 10) {
            float v = std::strtof(std::string(val).c_str(), nullptr);
            if (curEnt == "LWPOLYLINE" || curEnt == "POLYLINE" || curEnt == "SPLINE") {
                if (hasPendingLwX) lwPts.push_back({ pendingLwX, y1 });
                pendingLwX = v;
                hasPendingLwX = true;
            } else {
                x1 = v;
            }
        } else if (code == 20) {
            float v = std::strtof(std::string(val).c_str(), nullptr);
            if (curEnt == "LWPOLYLINE" || curEnt == "POLYLINE" || curEnt == "SPLINE") {
                lwPts.push_back({ pendingLwX, v });
                hasPendingLwX = false;
            } else {
                y1 = v;
            }
        } else if (code == 11) {
            x2 = std::strtof(std::string(val).c_str(), nullptr);
        } else if (code == 21) {
            y2 = std::strtof(std::string(val).c_str(), nullptr);
        } else if (code == 12) {
            x3 = std::strtof(std::string(val).c_str(), nullptr); hasPt3 = true;
        } else if (code == 22) {
            y3 = std::strtof(std::string(val).c_str(), nullptr); hasPt3 = true;
        } else if (code == 13) {
            x4 = std::strtof(std::string(val).c_str(), nullptr); hasPt4 = true;
        } else if (code == 23) {
            y4 = std::strtof(std::string(val).c_str(), nullptr); hasPt4 = true;
        } else if (code == 40) {
            radius = std::strtof(std::string(val).c_str(), nullptr);
        } else if (code == 50) {
            startAng = std::strtof(std::string(val).c_str(), nullptr);
        } else if (code == 51) {
            endAng = std::strtof(std::string(val).c_str(), nullptr);
        } else if (code == 70) {
            polyFlags = std::atoi(std::string(val).c_str());
        } else if (code == 1) {
            textVal.assign(val);
        }
    }
    flushEntity();

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.circles.empty() || !out.labels.empty() || !out.vias.empty());
    return out.valid;
}
