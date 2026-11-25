#pragma once
// PhotometricIes.h — IESNA LM-63 Photometric Light Profile Renderer (.ies)

namespace Formats {

// ============================================================================
// 17. IESNA LM-63 Photometric Light Profile (.ies)
//     Parses full IESNA:LM-63 header, vertical/horizontal angle arrays, and
//     2D candela web; renders both physically-sampled wall-wash cone AND
//     overlaid C0-C180 / C90-C270 polar candela distribution curves.
// ============================================================================
inline bool DecodePhotometricIES(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 24) return false;
    (void)extLower;
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // Walk header lines until TILT=NONE or TILT=INCLUDE (case-insensitive)
    std::string manufac;
    std::string lumcat;
    bool foundTilt = false;
    bool tiltInclude = false;
    size_t pos = 0;

    auto trimView = [](std::string_view s) -> std::string_view {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    };
    auto startsWithICase = [](std::string_view s, std::string_view pfx) -> bool {
        return s.size() >= pfx.size() && DetailLayeredAndHdrImg::EqualsIgnoreCaseAscii(s.substr(0, pfx.size()), pfx);
    };

    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = trimView(sv.substr(pos, eol - pos));
        pos = (eol < sv.size()) ? (eol + 1) : sv.size();

        if (startsWithICase(ln, "[MANUFAC]")) {
            manufac = std::string(trimView(ln.substr(9)));
        } else if (startsWithICase(ln, "[LUMCAT]")) {
            lumcat = std::string(trimView(ln.substr(8)));
        } else if (startsWithICase(ln, "TILT=")) {
            foundTilt = true;
            std::string_view mode = trimView(ln.substr(5));
            tiltInclude = startsWithICase(mode, "INCLUDE");
            break;
        }
    }
    if (!foundTilt) return false;

    // Fast locale-independent floating-point scanner
    auto nextFloat = [&](double& outVal) -> bool {
        while (pos < sv.size()) {
            char c = sv[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';') {
                ++pos;
                continue;
            }
            break;
        }
        if (pos >= sv.size()) return false;

        size_t start = pos;
        if (sv[pos] == '+' || sv[pos] == '-') ++pos;
        bool hasDigits = false;
        while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9') { ++pos; hasDigits = true; }
        if (pos < sv.size() && sv[pos] == '.') {
            ++pos;
            while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9') { ++pos; hasDigits = true; }
        }
        if (!hasDigits) return false;
        if (pos < sv.size() && (sv[pos] == 'e' || sv[pos] == 'E')) {
            size_t ePos = pos++;
            if (pos < sv.size() && (sv[pos] == '+' || sv[pos] == '-')) ++pos;
            bool expDigits = false;
            while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9') { ++pos; expDigits = true; }
            if (!expDigits) pos = ePos;
        }

        char buf[64];
        size_t tokLen = std::min<size_t>(pos - start, sizeof(buf) - 1);
        std::memcpy(buf, sv.data() + start, tokLen);
        buf[tokLen] = '\0';
        char* endPtr = nullptr;
        outVal = std::strtod(buf, &endPtr);
        return (endPtr != buf) && std::isfinite(outVal);
    };

    if (tiltInclude) {
        double lampGeom = 0.0, nTiltD = 0.0;
        if (!nextFloat(lampGeom) || !nextFloat(nTiltD)) return false;
        int nTilt = static_cast<int>(std::llround(nTiltD));
        if (nTilt < 0 || nTilt > 4096) return false;
        double dummy = 0.0;
        for (int i = 0; i < nTilt * 2; ++i) {
            if (!nextFloat(dummy)) return false;
        }
    }

    // Standard 13 numeric header parameters in IESNA LM-63:
    // 0:numLamps, 1:lumensPerLamp, 2:candelaMult, 3:nVertAngles, 4:nHorizAngles,
    // 5:photometricType, 6:unitsType, 7:width, 8:length, 9:height,
    // 10:ballastFactor, 11:futureUse, 12:inputWatts
    double hdr[13] = {};
    for (int i = 0; i < 13; ++i) {
        if (!nextFloat(hdr[i])) return false;
    }

    int nVert = static_cast<int>(std::llround(hdr[3]));
    int nHoriz = static_cast<int>(std::llround(hdr[4]));
    if (nVert < 2 || nVert > 4096 || nHoriz < 1 || nHoriz > 4096) return false;
    if (static_cast<size_t>(nVert) * static_cast<size_t>(nHoriz) > 2000000) return false;

    float candelaScale = static_cast<float>((hdr[2] > 0.0 ? hdr[2] : 1.0) * (hdr[10] > 0.0 ? hdr[10] : 1.0));
    float inputWatts = static_cast<float>(hdr[12]);

    std::vector<float> vertAngles(static_cast<size_t>(nVert));
    for (int v = 0; v < nVert; ++v) {
        double val = 0.0;
        if (!nextFloat(val)) return false;
        vertAngles[static_cast<size_t>(v)] = static_cast<float>(val);
        if (v > 0 && vertAngles[static_cast<size_t>(v)] < vertAngles[static_cast<size_t>(v - 1)] - 0.01f) {
            return false; // Vertical angles must be non-decreasing
        }
    }

    std::vector<float> horizAngles(static_cast<size_t>(nHoriz));
    for (int hIdx = 0; hIdx < nHoriz; ++hIdx) {
        double val = 0.0;
        if (!nextFloat(val)) return false;
        horizAngles[static_cast<size_t>(hIdx)] = static_cast<float>(val);
    }

    std::vector<float> candela(static_cast<size_t>(nHoriz) * static_cast<size_t>(nVert), 0.0f);
    float maxCandela = 0.0f;
    for (int hIdx = 0; hIdx < nHoriz; ++hIdx) {
        for (int v = 0; v < nVert; ++v) {
            double val = 0.0;
            if (!nextFloat(val)) return false;
            float cd = std::max(0.0f, static_cast<float>(val) * candelaScale);
            candela[static_cast<size_t>(hIdx) * static_cast<size_t>(nVert) + static_cast<size_t>(v)] = cd;
            if (cd > maxCandela) maxCandela = cd;
        }
    }
    if (!(maxCandela > 0.0f) || !std::isfinite(maxCandela)) return false;

    auto sampleCandela = [&](float thetaDeg, float phiDeg) -> float {
        if (thetaDeg < vertAngles.front() || thetaDeg > vertAngles.back()) {
            return 0.0f;
        }
        auto itV = std::lower_bound(vertAngles.begin(), vertAngles.end(), thetaDeg);
        size_t v1 = static_cast<size_t>(std::distance(vertAngles.begin(), itV));
        if (v1 >= vertAngles.size()) v1 = vertAngles.size() - 1;
        size_t v0 = (v1 > 0) ? (v1 - 1) : 0;
        float denomV = vertAngles[v1] - vertAngles[v0];
        float tv = (denomV > 1e-5f) ? std::clamp((thetaDeg - vertAngles[v0]) / denomV, 0.0f, 1.0f) : 0.0f;

        if (nHoriz == 1) {
            return candela[v0] * (1.0f - tv) + candela[v1] * tv;
        }

        float p = std::fmod(phiDeg, 360.0f);
        if (p < 0.0f) p += 360.0f;
        float hMax = horizAngles.back();
        if (hMax <= 90.05f) { // Quadrant symmetric
            if (p > 180.0f) p = 360.0f - p;
            if (p > 90.0f) p = 180.0f - p;
        } else if (hMax <= 180.05f) { // Bilaterally symmetric across C0-C180
            if (p > 180.0f) p = 360.0f - p;
        }
        p = std::clamp(p, horizAngles.front(), horizAngles.back());

        auto itH = std::lower_bound(horizAngles.begin(), horizAngles.end(), p);
        size_t h1 = static_cast<size_t>(std::distance(horizAngles.begin(), itH));
        if (h1 >= horizAngles.size()) h1 = horizAngles.size() - 1;
        size_t h0 = (h1 > 0) ? (h1 - 1) : 0;
        float denomH = horizAngles[h1] - horizAngles[h0];
        float th = (denomH > 1e-5f) ? std::clamp((p - horizAngles[h0]) / denomH, 0.0f, 1.0f) : 0.0f;

        float c00 = candela[h0 * static_cast<size_t>(nVert) + v0];
        float c01 = candela[h0 * static_cast<size_t>(nVert) + v1];
        float c10 = candela[h1 * static_cast<size_t>(nVert) + v0];
        float c11 = candela[h1 * static_cast<size_t>(nVert) + v1];
        float c0 = c00 * (1.0f - tv) + c01 * tv;
        float c1 = c10 * (1.0f - tv) + c11 * tv;
        return c0 * (1.0f - th) + c1 * th;
    };

    const uint32_t W = 320, H = 320;
    outInfo.width = W;
    outInfo.height = H;
    outInfo.origWidth = W;
    outInfo.origHeight = H;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    std::wstring desc = L"IESNA LM-63 Photometric Web (";
    if (!lumcat.empty()) desc += PathUtils::Utf8ToWide(lumcat) + L", ";
    else if (!manufac.empty()) desc += PathUtils::Utf8ToWide(manufac) + L", ";
    desc += std::to_wstring(static_cast<int>(std::round(maxCandela))) + L" cd max)";
    outInfo.formatName = desc;
    outInfo.bgraPixels.resize(static_cast<size_t>(W) * H * 4);

    bool hasUplight = (vertAngles.back() > 95.0f);
    const float cx = W * 0.5f;
    const float ly = hasUplight ? (H * 0.44f) : (H * 0.14f);
    const float zWall = H * 0.20f;
    const float polarMaxR = hasUplight ? (H * 0.38f) : (H * 0.72f);

    // Subtle color temperature shift from lamp wattage/lumens (warm 3000K -> neutral 4200K)
    float efficacy = (inputWatts > 1.0f && hdr[1] > 0.0) ? static_cast<float>((hdr[0] * hdr[1]) / inputWatts) : 75.0f;
    float coolMix = std::clamp((efficacy - 40.0f) / 100.0f, 0.0f, 1.0f);
    float tintR = 1.00f - 0.05f * coolMix;
    float tintG = 0.90f + 0.06f * coolMix;
    float tintB = 0.72f + 0.22f * coolMix;

    // Pass 1: Physically-sampled architectural wall-wash cone + subtle polar reference rings
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            float dx = static_cast<float>(x) - cx;
            float dy = static_cast<float>(y) - ly; // +dy = downward nadir (theta = 0)
            float r2d = std::sqrt(dx * dx + dy * dy);
            float r3d = std::sqrt(dx * dx + dy * dy + zWall * zWall);

            float cosTheta = std::clamp(dy / std::max(1e-4f, r3d), -1.0f, 1.0f);
            float thetaDeg = std::acos(cosTheta) * 57.2957795f;
            // Right half samples C0..C90, left half samples C180..C90 so asymmetric fixtures show true asymmetry
            float phiDeg = (dx >= 0.0f)
                ? (std::atan2(zWall * 0.35f, dx + 0.01f) * 57.2957795f)
                : (180.0f - std::atan2(zWall * 0.35f, -dx + 0.01f) * 57.2957795f);

            float normI = sampleCandela(thetaDeg, phiDeg) / maxCandela;
            float distNorm = r3d / static_cast<float>(H);
            float illuminance = normI * (zWall / r3d) / (0.14f + distNorm * distNorm * 2.2f);
            float mapped = 1.0f - std::exp(-illuminance * 2.35f);
            mapped = std::pow(std::clamp(mapped, 0.0f, 1.0f), 1.0f / 2.1f);

            // Polar grid rings at 25%, 50%, 75%, 100% of polarMaxR
            float ringPos = (r2d / polarMaxR) * 4.0f;
            bool onRing = (r2d <= polarMaxR + 1.0f) &&
                          (std::abs(ringPos - std::round(ringPos)) * (polarMaxR * 0.25f) < 0.65f) &&
                          (ringPos >= 0.5f);

            float bgR = 14.0f + (onRing ? 18.0f : 0.0f);
            float bgG = 17.0f + (onRing ? 24.0f : 0.0f);
            float bgB = 24.0f + (onRing ? 34.0f : 0.0f);

            uint8_t pr = static_cast<uint8_t>(std::clamp(bgR + mapped * 245.0f * tintR, 0.0f, 255.0f));
            uint8_t pg = static_cast<uint8_t>(std::clamp(bgG + mapped * 245.0f * tintG, 0.0f, 255.0f));
            uint8_t pb = static_cast<uint8_t>(std::clamp(bgB + mapped * 245.0f * tintB, 0.0f, 255.0f));
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * W + x) * 4], pr, pg, pb, 255);
        }
    }

    // Pass 2: Overlaid polar candela distribution curves (C0-C180 gold & C90-C270 cyan)
    auto blendPixel = [&](int px, int py, uint8_t cr, uint8_t cg, uint8_t cb, float alpha) {
        if (px < 0 || py < 0 || px >= static_cast<int>(W) || py >= static_cast<int>(H)) return;
        uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(py) * W + static_cast<size_t>(px)) * 4];
        dst[0] = static_cast<uint8_t>(std::clamp(dst[0] * (1.0f - alpha) + cb * alpha, 0.0f, 255.0f));
        dst[1] = static_cast<uint8_t>(std::clamp(dst[1] * (1.0f - alpha) + cg * alpha, 0.0f, 255.0f));
        dst[2] = static_cast<uint8_t>(std::clamp(dst[2] * (1.0f - alpha) + cr * alpha, 0.0f, 255.0f));
        dst[3] = 255;
    };
    auto drawLineAA = [&](float x0, float y0, float x1, float y1, uint8_t cr, uint8_t cg, uint8_t cb, float alpha) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy)) * 2.0f)));
        for (int s = 0; s <= steps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(steps);
            float fx = x0 + dx * t;
            float fy = y0 + dy * t;
            int ix = static_cast<int>(std::floor(fx));
            int iy = static_cast<int>(std::floor(fy));
            blendPixel(ix, iy, cr, cg, cb, alpha);
            blendPixel(ix + 1, iy, cr, cg, cb, alpha * 0.45f);
            blendPixel(ix, iy + 1, cr, cg, cb, alpha * 0.45f);
        }
    };

    auto plotPolarPlane = [&](float phiRight, float phiLeft, uint8_t cr, uint8_t cg, uint8_t cb) {
        const int N = 360;
        for (int side = 0; side < 2; ++side) {
            float phi = (side == 0) ? phiRight : phiLeft;
            float signX = (side == 0) ? 1.0f : -1.0f;
            float prevX = cx, prevY = ly;
            bool hasPrev = false;
            for (int i = 0; i <= N; ++i) {
                float theta = (static_cast<float>(i) / static_cast<float>(N)) * 180.0f;
                float rad = (sampleCandela(theta, phi) / maxCandela) * polarMaxR;
                float radAngle = theta * 0.0174532925f;
                float px = cx + signX * std::sin(radAngle) * rad;
                float py = ly + std::cos(radAngle) * rad;
                if (hasPrev) {
                    drawLineAA(prevX, prevY, px, py, cr, cg, cb, 0.85f);
                }
                prevX = px;
                prevY = py;
                hasPrev = true;
            }
        }
    };

    plotPolarPlane(90.0f, 270.0f, 75, 215, 255);  // C90 - C270 plane (cyan)
    plotPolarPlane(0.0f,  180.0f, 255, 215, 65);  // C0 - C180 plane (amber/gold)

    outInfo.valid = true;
    return true;
}


} // namespace Formats
