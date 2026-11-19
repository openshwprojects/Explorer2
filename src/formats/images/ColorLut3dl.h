#pragma once
// ColorLut3dl.h — Autodesk Lustre / Flame .3dl 3D Color Grading LUT Renderer (.3dl)

namespace Formats {

inline bool DecodeColorLut3dl(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size == 0) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // .3dl (Autodesk Lustre / Flame): a first line of input mesh points, then integer triplets, blue fastest;
    // the output bit depth follows from the largest value (10, 12 or 16 bit).
    using Rgb = DetailColorLut::Rgb;
    std::vector<Rgb> table;
    int size3d = 0, meshPoints = 0;
    float dmin[3] = { 0.0f, 0.0f, 0.0f }, dmax[3] = { 1.0f, 1.0f, 1.0f };
    std::string title;
    float maxValue = 0.0f;
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
        if (meshPoints == 0 && nums.size() > 3) { meshPoints = static_cast<int>(nums.size()); continue; }
        if (nums.size() < 3 || !std::isfinite(nums[0]) || !std::isfinite(nums[1]) || !std::isfinite(nums[2])) continue;
        table.push_back({ nums[0], nums[1], nums[2] });
        maxValue = std::max({ maxValue, nums[0], nums[1], nums[2] });
    }
    if (table.empty()) return false;

    float scale = (maxValue <= 1023.5f) ? 1.0f / 1023.0f : (maxValue <= 4095.5f) ? 1.0f / 4095.0f : 1.0f / 65535.0f;
    int n = meshPoints > 1 ? meshPoints : static_cast<int>(std::lround(std::cbrt(static_cast<double>(table.size()))));
    size3d = n;
    if (size3d < 2 || table.size() < static_cast<size_t>(size3d) * size3d * size3d) return false;
    for (auto& t : table) { t.r *= scale; t.g *= scale; t.b *= scale; }

    std::wstring name = L"3D Color Grading LUT (" + std::to_wstring(size3d) + L"\xB3 3DL)";
    if (!title.empty()) name += L" \u2014 " + PathUtils::Utf8ToWide(title);
    return DetailColorLut::RenderColorLutTestCard(table, true, size3d, false, 0, true, dmin, dmax, name, outInfo);
}

} // namespace Formats
