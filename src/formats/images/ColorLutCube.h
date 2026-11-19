#pragma once
// ColorLutCube.h — Adobe / Resolve .cube 1D & 3D Color Grading LUT Renderer (.cube)

namespace Formats {

inline bool DecodeColorLutCube(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size == 0) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // .cube (Adobe / Resolve): TITLE, LUT_1D_SIZE / LUT_3D_SIZE, DOMAIN_MIN / DOMAIN_MAX, float triplets, red fastest.
    using Rgb = DetailColorLut::Rgb;
    std::vector<Rgb> table;
    int size1d = 0, size3d = 0;
    float dmin[3] = { 0.0f, 0.0f, 0.0f }, dmax[3] = { 1.0f, 1.0f, 1.0f };
    std::string title;
    for (size_t pos = 0; pos < sv.size() && table.size() <= 256u * 256u * 256u;) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\t' || ln.back() == '\r')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '#') continue;
        std::string line(ln);
        if (ln.rfind("TITLE", 0) == 0) {
            size_t q1 = ln.find('"'), q2 = ln.rfind('"');
            if (q1 != std::string_view::npos && q2 > q1) title = std::string(ln.substr(q1 + 1, q2 - q1 - 1));
            continue;
        }
        if (ln.rfind("LUT_3D_SIZE", 0) == 0) { sscanf_s(line.c_str() + 11, "%d", &size3d); continue; }
        if (ln.rfind("LUT_1D_SIZE", 0) == 0) { sscanf_s(line.c_str() + 11, "%d", &size1d); continue; }
        if (ln.rfind("DOMAIN_MIN", 0) == 0) { sscanf_s(line.c_str() + 10, "%f %f %f", &dmin[0], &dmin[1], &dmin[2]); continue; }
        if (ln.rfind("DOMAIN_MAX", 0) == 0) { sscanf_s(line.c_str() + 10, "%f %f %f", &dmax[0], &dmax[1], &dmax[2]); continue; }
        if (ln.rfind("LUT_3D_INPUT_RANGE", 0) == 0 || ln.rfind("LUT_1D_INPUT_RANGE", 0) == 0) {
            float a = 0.0f, b = 1.0f;
            if (sscanf_s(line.c_str() + 18, "%f %f", &a, &b) == 2) { dmin[0] = dmin[1] = dmin[2] = a; dmax[0] = dmax[1] = dmax[2] = b; }
            continue;
        }
        if (!((ln[0] >= '0' && ln[0] <= '9') || ln[0] == '.' || ln[0] == '-' || ln[0] == '+')) continue;
        // Count the numbers on the line: a .3dl mesh line has more than three
        std::vector<float> nums;
        const char* c = line.c_str();
        while (*c && nums.size() < 70) {
            char* endp = nullptr;
            float v = std::strtof(c, &endp);
            if (endp == c) break;
            nums.push_back(v);
            c = endp;
        }
        if (nums.size() < 3 || !std::isfinite(nums[0]) || !std::isfinite(nums[1]) || !std::isfinite(nums[2])) continue;
        table.push_back({ nums[0], nums[1], nums[2] });
    }
    if (table.empty()) return false;

    if (size3d <= 1 && size1d <= 1) {
        size3d = static_cast<int>(std::lround(std::cbrt(static_cast<double>(table.size()))));
    }
    const bool use3d = size3d >= 2 && table.size() >= static_cast<size_t>(size3d) * size3d * size3d;
    const bool use1d = !use3d && size1d >= 2 && table.size() >= static_cast<size_t>(size1d);
    if (!use3d && !use1d) return false;

    std::wstring name = use3d ? (L"3D Color Grading LUT (" + std::to_wstring(size3d) + L"\xB3 Cube)")
                              : (L"1D Color LUT (" + std::to_wstring(size1d) + L" entries)");
    if (!title.empty()) name += L" \u2014 " + PathUtils::Utf8ToWide(title);
    return DetailColorLut::RenderColorLutTestCard(table, use3d, size3d, use1d, size1d, false, dmin, dmax, name, outInfo);
}

} // namespace Formats
