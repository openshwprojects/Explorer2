#pragma once
// GpsTrackMap.h — Interactive Direct2D GPS / Log Cartographic Map & Elevation Profile Renderer

namespace Formats {

inline double HaversineKm(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    double dLat = (lat2 - lat1) * kDegToRad;
    double dLon = (lon2 - lon1) * kDegToRad;
    double a = std::sin(dLat * 0.5) * std::sin(dLat * 0.5) +
               std::cos(lat1 * kDegToRad) * std::cos(lat2 * kDegToRad) *
               std::sin(dLon * 0.5) * std::sin(dLon * 0.5);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return 6371.0 * c;
}

inline void ExtractGeoTrackFromSheets(ParsedOfficeDocument& doc) {
    doc.geoPoints.clear();
    doc.hasGeoTrack = false;
    doc.totalDistanceKm = 0.0;
    doc.totalAscentM = 0.0;
    if (!doc.isSpreadsheet || doc.sheets.empty()) return;

    const auto& sh = doc.sheets[0];
    if (sh.rows.size() < 2) return;

    int latCol = -1, lonCol = -1, elevCol = -1, coordCol = -1, labelCol = -1;
    const auto& hdr = sh.rows[0];
    for (size_t c = 0; c < hdr.size(); ++c) {
        std::wstring h = PathUtils::ToLower(hdr[c]);
        if (h.find(L"coord") != std::wstring::npos) coordCol = static_cast<int>(c);
        else if (h.find(L"lat") != std::wstring::npos) latCol = static_cast<int>(c);
        else if (h.find(L"lon") != std::wstring::npos) lonCol = static_cast<int>(c);
        else if (h.find(L"elev") != std::wstring::npos || h.find(L"alt") != std::wstring::npos) elevCol = static_cast<int>(c);
        else if (h.find(L"time") != std::wstring::npos || h.find(L"name") != std::wstring::npos || h.find(L"tag") != std::wstring::npos || h.find(L"prop") != std::wstring::npos) {
            labelCol = static_cast<int>(c);
        }
    }
    if ((latCol < 0 || lonCol < 0) && coordCol < 0) return;

    for (size_t r = 1; r < sh.rows.size(); ++r) {
        const auto& row = sh.rows[r];
        double lat = 0.0, lon = 0.0;
        float elev = 0.0f;
        bool okCoord = false;

        if (latCol >= 0 && lonCol >= 0 && static_cast<size_t>( std::max(latCol, lonCol) ) < row.size()) {
            wchar_t* e1 = nullptr;
            wchar_t* e2 = nullptr;
            lat = std::wcstod(row[latCol].c_str(), &e1);
            lon = std::wcstod(row[lonCol].c_str(), &e2);
            if (e1 != row[latCol].c_str() && e2 != row[lonCol].c_str() &&
                std::isfinite(lat) && std::isfinite(lon) && std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0 &&
                (std::fabs(lat) > 1e-5 || std::fabs(lon) > 1e-5)) {
                okCoord = true;
            }
            if (elevCol >= 0 && static_cast<size_t>(elevCol) < row.size()) {
                elev = static_cast<float>(std::wcstod(row[elevCol].c_str(), nullptr));
            }
        } else if (coordCol >= 0 && static_cast<size_t>(coordCol) < row.size()) {
            // Parse KML "lon,lat,alt" or GeoJSON "[lon, lat]"
            std::wstring s = row[coordCol];
            for (wchar_t& ch : s) {
                if (ch == L'[' || ch == L']' || ch == L',') ch = L' ';
            }
            double c1 = 0.0, c2 = 0.0, c3 = 0.0;
            int n = swscanf_s(s.c_str(), L"%lf %lf %lf", &c1, &c2, &c3);
            if (n >= 2 && std::fabs(c2) <= 90.0 && std::fabs(c1) <= 180.0) {
                lon = c1;
                lat = c2;
                if (n >= 3) elev = static_cast<float>(c3);
                okCoord = true;
            }
        }

        if (okCoord) {
            GpsTrackPoint pt;
            pt.lat = lat;
            pt.lon = lon;
            pt.elevM = std::isfinite(elev) ? elev : 0.0f;
            pt.rowIndex = static_cast<int>(r);
            if (labelCol >= 0 && static_cast<size_t>(labelCol) < row.size()) {
                pt.label = row[labelCol];
            }
            if (!doc.geoPoints.empty()) {
                const auto& prev = doc.geoPoints.back();
                double d = HaversineKm(prev.lat, prev.lon, pt.lat, pt.lon);
                doc.totalDistanceKm += d;
                if (pt.elevM > prev.elevM) {
                    doc.totalAscentM += (pt.elevM - prev.elevM);
                }
            }
            pt.cumDistKm = static_cast<float>(doc.totalDistanceKm);
            doc.geoPoints.push_back(std::move(pt));
        }
    }

    doc.hasGeoTrack = (doc.geoPoints.size() >= 2);
}

inline void DrawGpsMapAndElevationView(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& bodyR,
    const ParsedOfficeDocument& doc,
    float mapZoom,
    float mapPanX,
    float mapPanY,
    int selectedRow,
    D2D1_POINT_2F mousePt,
    bool pl)
{
    if (doc.geoPoints.empty()) return;

    float profileH = std::clamp((bodyR.bottom - bodyR.top) * 0.28f, 84.0f, 150.0f);
    D2D1_RECT_F mapR = D2D1::RectF(bodyR.left, bodyR.top, bodyR.right, bodyR.bottom - profileH);
    D2D1_RECT_F profR = D2D1::RectF(bodyR.left, mapR.bottom, bodyR.right, bodyR.bottom);

    // 1. Cartographic Map Background (Dark Navy/Slate Topo Canvas)
    brush->SetColor(PathUtils::ColorFromRgb(0x0F172A));
    rt->FillRectangle(mapR, brush);

    rt->PushAxisAlignedClip(mapR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    double minLat = 90.0, maxLat = -90.0, minLon = 180.0, maxLon = -180.0;
    float minElev = 1e9f, maxElev = -1e9f;
    for (const auto& p : doc.geoPoints) {
        minLat = std::min(minLat, p.lat); maxLat = std::max(maxLat, p.lat);
        minLon = std::min(minLon, p.lon); maxLon = std::max(maxLon, p.lon);
        minElev = std::min(minElev, p.elevM); maxElev = std::max(maxElev, p.elevM);
    }
    double midLat = (minLat + maxLat) * 0.5;
    double cosLat = std::max(0.2, std::cos(midLat * 3.14159265358979323846 / 180.0));
    double spanLat = std::max(0.0005, (maxLat - minLat) * 1.25);
    double spanLon = std::max(0.0005, (maxLon - minLon) * 1.25);

    float mapW = std::max(40.0f, mapR.right - mapR.left - 48.0f);
    float mapH = std::max(40.0f, mapR.bottom - mapR.top - 48.0f);
    double scaleX = mapW / (spanLon * cosLat);
    double scaleY = mapH / spanLat;
    double baseScale = std::min(scaleX, scaleY) * std::clamp(mapZoom, 0.25f, 30.0f);

    float cx = (mapR.left + mapR.right) * 0.5f + mapPanX;
    float cy = (mapR.top + mapR.bottom) * 0.5f + mapPanY;

    auto projectPt = [&](double lat, double lon) -> D2D1_POINT_2F {
        float px = cx + static_cast<float>((lon - (minLon + maxLon) * 0.5) * cosLat * baseScale);
        float py = cy - static_cast<float>((lat - midLat) * baseScale);
        return D2D1::Point2F(px, py);
    };

    // 2. Coordinate Graticule Grid + Degree Labels
    brush->SetColor(PathUtils::ColorFromRgb(0x1E293B));
    const int gridSteps = 6;
    for (int i = -gridSteps; i <= gridSteps; ++i) {
        double gLat = midLat + (spanLat / gridSteps) * i * 0.6;
        double gLon = (minLon + maxLon) * 0.5 + (spanLon / gridSteps) * i * 0.6;
        D2D1_POINT_2F pLat = projectPt(gLat, minLon);
        D2D1_POINT_2F pLon = projectPt(midLat, gLon);

        if (pLat.y >= mapR.top && pLat.y <= mapR.bottom) {
            brush->SetColor(PathUtils::ColorFromRgb(0x1E293B));
            rt->DrawLine(D2D1::Point2F(mapR.left, pLat.y), D2D1::Point2F(mapR.right, pLat.y), brush, 1.0f);
            wchar_t lb[32] = {};
            swprintf_s(lb, L"%.4f\xB0", gLat);
            brush->SetColor(PathUtils::ColorFromRgb(0x64748B));
            rt->DrawText(lb, static_cast<UINT32>(wcslen(lb)), fmtSmall,
                D2D1::RectF(mapR.left + 6.0f, pLat.y - 14.0f, mapR.left + 90.0f, pLat.y), brush);
        }
        if (pLon.x >= mapR.left && pLon.x <= mapR.right) {
            brush->SetColor(PathUtils::ColorFromRgb(0x1E293B));
            rt->DrawLine(D2D1::Point2F(pLon.x, mapR.top), D2D1::Point2F(pLon.x, mapR.bottom), brush, 1.0f);
            wchar_t lb[32] = {};
            swprintf_s(lb, L"%.4f\xB0", gLon);
            brush->SetColor(PathUtils::ColorFromRgb(0x64748B));
            rt->DrawText(lb, static_cast<UINT32>(wcslen(lb)), fmtSmall,
                D2D1::RectF(pLon.x + 3.0f, mapR.bottom - 18.0f, pLon.x + 88.0f, mapR.bottom - 2.0f), brush);
        }
    }

    // 3. Draw Color-Graded GPS Route Polyline & Track Nodes
    float elevSpan = std::max(1.0f, maxElev - minElev);
    int hoverIdx = -1;
    float bestDist2 = 225.0f; // 15px snap radius

    std::vector<D2D1_POINT_2F> screenPts(doc.geoPoints.size());
    for (size_t i = 0; i < doc.geoPoints.size(); ++i) {
        screenPts[i] = projectPt(doc.geoPoints[i].lat, doc.geoPoints[i].lon);
        if (PathUtils::PtInRectF(mousePt.x, mousePt.y, mapR)) {
            float dx = screenPts[i].x - mousePt.x;
            float dy = screenPts[i].y - mousePt.y;
            if (dx * dx + dy * dy < bestDist2) {
                bestDist2 = dx * dx + dy * dy;
                hoverIdx = static_cast<int>(i);
            }
        }
        if (hoverIdx < 0 && doc.geoPoints[i].rowIndex == selectedRow) {
            hoverIdx = static_cast<int>(i);
        }
    }

    // Route glow under-stroke
    brush->SetColor(PathUtils::ColorFromRgb(0x0284C7, 0.28f));
    for (size_t i = 0; i + 1 < screenPts.size(); ++i) {
        rt->DrawLine(screenPts[i], screenPts[i + 1], brush, 7.0f);
    }

    // Main altitude-colored route segments
    for (size_t i = 0; i + 1 < screenPts.size(); ++i) {
        float t = std::clamp((doc.geoPoints[i].elevM - minElev) / elevSpan, 0.0f, 1.0f);
        uint32_t segColor = (t < 0.33f) ? 0x10B981 : ((t < 0.66f) ? 0x38BDF8 : 0xF59E0B);
        brush->SetColor(PathUtils::ColorFromRgb(segColor));
        rt->DrawLine(screenPts[i], screenPts[i + 1], brush, 3.0f);
    }

    // Draw trackpoint dots + Start (S) & Finish (F) badges
    for (size_t i = 0; i < screenPts.size(); ++i) {
        bool isStart = (i == 0);
        bool isEnd   = (i + 1 == screenPts.size());
        bool isHov   = (static_cast<int>(i) == hoverIdx);
        float rad = (isStart || isEnd) ? 6.5f : (isHov ? 5.5f : 2.8f);
        uint32_t col = isStart ? 0x22C55E : (isEnd ? 0xEF4444 : (isHov ? 0xFACC15 : 0xE2E8F0));
        brush->SetColor(PathUtils::ColorFromRgb(col));
        rt->FillEllipse(D2D1::Ellipse(screenPts[i], rad, rad), brush);
        if (isStart || isEnd || isHov) {
            brush->SetColor(PathUtils::ColorFromRgb(0x0F172A));
            rt->DrawEllipse(D2D1::Ellipse(screenPts[i], rad, rad), brush, 1.5f);
        }
    }

    // 4. Compass Rose & Telemetry Summary HUD Overlay (top-right)
    D2D1_RECT_F hudR = D2D1::RectF(mapR.right - 235.0f, mapR.top + 10.0f, mapR.right - 10.0f, mapR.top + 78.0f);
    if (hudR.left > mapR.left + 20.0f) {
        brush->SetColor(PathUtils::ColorFromRgb(0x1E293B, 0.90f));
        rt->FillRoundedRectangle(D2D1::RoundedRect(hudR, 5.0f, 5.0f), brush);
        brush->SetColor(PathUtils::ColorFromRgb(0x334155));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(hudR, 5.0f, 5.0f), brush, 1.0f);

        wchar_t hudTxt[256] = {};
        swprintf_s(hudTxt,
            pl ? L"\xD83E\xDDED Punkty GPS: %zu\nDystans: %.2f km  |  Wznios: +%.0f m\nWysokość: %.0f .. %.0f m n.p.m."
               : L"\xD83E\xDDED GPS Points: %zu\nDistance: %.2f km  |  Ascent: +%.0f m\nElevation: %.0f .. %.0f m",
            doc.geoPoints.size(), doc.totalDistanceKm, doc.totalAscentM, minElev, maxElev);
        brush->SetColor(PathUtils::ColorFromRgb(0xF8FAFC));
        rt->DrawText(hudTxt, static_cast<UINT32>(wcslen(hudTxt)), fmtSmall,
            D2D1::RectF(hudR.left + 8.0f, hudR.top + 4.0f, hudR.right - 6.0f, hudR.bottom - 4.0f), brush);
    }

    // 5. Hovered / Selected Waypoint Callout Badge
    if (hoverIdx >= 0 && static_cast<size_t>(hoverIdx) < doc.geoPoints.size()) {
        const auto& hp = doc.geoPoints[hoverIdx];
        wchar_t tip[256] = {};
        swprintf_s(tip, L"#%d  (%.5f\xB0, %.5f\xB0)   Alt: %.1f m   Dist: %.2f km   %s",
            hoverIdx + 1, hp.lat, hp.lon, hp.elevM, hp.cumDistKm, hp.label.c_str());
        D2D1_RECT_F tipR = D2D1::RectF(mapR.left + 12.0f, mapR.top + 10.0f, std::min(mapR.right - 245.0f, mapR.left + 460.0f), mapR.top + 34.0f);
        if (tipR.right > tipR.left + 80.0f) {
            brush->SetColor(PathUtils::ColorFromRgb(0x0F172A, 0.88f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(tipR, 4.0f, 4.0f), brush);
            brush->SetColor(PathUtils::ColorFromRgb(0x38BDF8));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(tipR, 4.0f, 4.0f), brush, 1.0f);
            brush->SetColor(PathUtils::ColorFromRgb(0xF8FAFC));
            rt->DrawText(tip, static_cast<UINT32>(wcslen(tip)), fmtSmall,
                D2D1::RectF(tipR.left + 8.0f, tipR.top + 3.0f, tipR.right - 6.0f, tipR.bottom), brush);
        }
    }

    rt->PopAxisAlignedClip();

    // 6. Bottom Elevation Profile Chart (m vs km)
    brush->SetColor(PathUtils::ColorFromRgb(0x0B1120));
    rt->FillRectangle(profR, brush);
    brush->SetColor(PathUtils::ColorFromRgb(0x334155));
    rt->DrawLine(D2D1::Point2F(profR.left, profR.top), D2D1::Point2F(profR.right, profR.top), brush, 1.2f);

    D2D1_RECT_F chartR = D2D1::RectF(profR.left + 52.0f, profR.top + 18.0f, profR.right - 16.0f, profR.bottom - 16.0f);
    if (chartR.right > chartR.left + 40.0f && chartR.bottom > chartR.top + 20.0f) {
        brush->SetColor(PathUtils::ColorFromRgb(0x1E293B));
        rt->DrawRectangle(chartR, brush, 1.0f);

        wchar_t topLbl[64] = {}, botLbl[64] = {};
        swprintf_s(topLbl, L"%.0f m", maxElev);
        swprintf_s(botLbl, L"%.0f m", minElev);
        brush->SetColor(PathUtils::ColorFromRgb(0x94A3B8));
        rt->DrawText(topLbl, static_cast<UINT32>(wcslen(topLbl)), fmtSmall,
            D2D1::RectF(profR.left + 4.0f, chartR.top - 4.0f, chartR.left - 4.0f, chartR.top + 14.0f), brush);
        rt->DrawText(botLbl, static_cast<UINT32>(wcslen(botLbl)), fmtSmall,
            D2D1::RectF(profR.left + 4.0f, chartR.bottom - 14.0f, chartR.left - 4.0f, chartR.bottom + 4.0f), brush);

        float cw = chartR.right - chartR.left;
        float ch = chartR.bottom - chartR.top;
        size_t n = doc.geoPoints.size();
        for (size_t i = 0; i + 1 < n; ++i) {
            float x0 = chartR.left + (static_cast<float>(i) / static_cast<float>(n - 1)) * cw;
            float x1 = chartR.left + (static_cast<float>(i + 1) / static_cast<float>(n - 1)) * cw;
            float y0 = chartR.bottom - std::clamp((doc.geoPoints[i].elevM - minElev) / elevSpan, 0.0f, 1.0f) * ch;
            float y1 = chartR.bottom - std::clamp((doc.geoPoints[i + 1].elevM - minElev) / elevSpan, 0.0f, 1.0f) * ch;

            brush->SetColor(PathUtils::ColorFromRgb(0x0284C7, 0.30f));
            rt->DrawLine(D2D1::Point2F(x0, chartR.bottom), D2D1::Point2F(x0, y0), brush, std::max(1.5f, cw / n + 1.0f));
            brush->SetColor(PathUtils::ColorFromRgb(0x38BDF8));
            rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush, 2.0f);
        }

        if (hoverIdx >= 0 && static_cast<size_t>(hoverIdx) < n) {
            float hx = chartR.left + (static_cast<float>(hoverIdx) / static_cast<float>(n - 1)) * cw;
            float hy = chartR.bottom - std::clamp((doc.geoPoints[hoverIdx].elevM - minElev) / elevSpan, 0.0f, 1.0f) * ch;
            brush->SetColor(PathUtils::ColorFromRgb(0xFACC15));
            rt->DrawLine(D2D1::Point2F(hx, chartR.top), D2D1::Point2F(hx, chartR.bottom), brush, 1.2f);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(hx, hy), 4.5f, 4.5f), brush);
        }
    }
    (void)fmtRegular;
    (void)fmtBold;
}

} // namespace Formats
