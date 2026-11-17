#pragma once
// MatrixMarket.h — NIST Matrix Market exchange files (.mtx): the banner (object, coordinate / array format,
// real / integer / complex / pattern field, general / symmetric / skew-symmetric / hermitian symmetry), the
// size line, and the entries (coordinate triplets, or array values in column-major order). Small matrices
// are also shown as a dense grid.

namespace Formats {

inline bool ParseMatrixMarketDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.rfind("%%MatrixMarket", 0) != 0) return false;

    auto lower = [](std::string_view s) {
        std::string r(s);
        for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return r;
    };
    auto words = [](std::string_view s) {
        std::vector<std::string_view> w;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
            size_t st = i;
            while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
            if (i > st) w.push_back(s.substr(st, i - st));
        }
        return w;
    };

    size_t eol0 = sv.find('\n');
    std::string_view banner = sv.substr(0, eol0 == std::string_view::npos ? sv.size() : eol0);
    while (!banner.empty() && (banner.back() == '\r' || banner.back() == ' ')) banner.remove_suffix(1);
    auto bw = words(banner);
    std::string object = bw.size() > 1 ? lower(bw[1]) : "matrix";
    std::string format = bw.size() > 2 ? lower(bw[2]) : "coordinate";
    std::string field = bw.size() > 3 ? lower(bw[3]) : "real";
    std::string symmetry = bw.size() > 4 ? lower(bw[4]) : "general";
    if (format != "coordinate" && format != "array") return false;
    const bool isArray = (format == "array");
    const bool isPattern = (field == "pattern");
    const bool isComplex = (field == "complex");

    uint64_t rows = 0, cols = 0, declared = 0;
    bool haveSize = false;
    struct Entry {
        uint64_t i = 0, j = 0;
        std::string re, im; // im only for complex matrices
        std::string Text(bool conjugate = false, bool negate = false) const {
            std::string r = negate ? (re.rfind("-", 0) == 0 ? re.substr(1) : "-" + re) : re;
            if (im.empty()) return r;
            bool neg = (im.rfind("-", 0) == 0) != (conjugate != negate);
            std::string mag = im.rfind("-", 0) == 0 ? im.substr(1) : im;
            return r + (neg ? " - " : " + ") + mag + "i";
        }
    };
    std::vector<Entry> entries;
    uint64_t found = 0, bad = 0, arrayIndex = 0;
    double minV = 0.0, maxV = 0.0;
    bool anyV = false;

    for (size_t pos = (eol0 == std::string_view::npos ? sv.size() : eol0 + 1); pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.empty() || ln[0] == '%') continue;
        auto w = words(ln);
        auto u64 = [](std::string_view s, uint64_t& v) {
            auto res = std::from_chars(s.data(), s.data() + s.size(), v);
            return res.ec == std::errc{} && res.ptr == s.data() + s.size();
        };
        if (!haveSize) {
            if (w.size() < 2 || !u64(w[0], rows) || !u64(w[1], cols)) return false;
            if (!isArray && (w.size() < 3 || !u64(w[2], declared))) return false;
            if (isArray) declared = (symmetry == "general") ? rows * cols : rows * (rows + 1) / 2;
            haveSize = true;
            continue;
        }
        Entry e{};
        std::string_view valueText;
        if (isArray) {
            // Column-major; symmetric arrays store the lower triangle only
            if (w.empty()) continue;
            uint64_t k = arrayIndex++;
            if (symmetry == "general") {
                if (rows == 0) break;
                e.i = k % rows + 1;
                e.j = k / rows + 1;
            } else {
                uint64_t col = 0, left = k;
                while (col < cols && left >= rows - col) { left -= rows - col; ++col; }
                e.j = col + 1;
                e.i = col + left + 1;
            }
            valueText = w[0];
            e.re = std::string(w[0]);
            if (isComplex && w.size() > 1) e.im = std::string(w[1]);
        } else {
            if (w.size() < 2 || !u64(w[0], e.i) || !u64(w[1], e.j) || e.i == 0 || e.j == 0 || e.i > rows || e.j > cols) {
                ++bad;
                continue;
            }
            if (isPattern) e.re = "1";
            else if (w.size() >= 3) {
                valueText = w[2];
                e.re = std::string(w[2]);
                if (isComplex && w.size() > 3) e.im = std::string(w[3]);
            } else { ++bad; continue; }
        }
        double d = 0.0;
        if (!valueText.empty() && std::from_chars(valueText.data(), valueText.data() + valueText.size(), d).ec == std::errc{} && std::isfinite(d)) {
            if (!anyV) { minV = maxV = d; anyV = true; }
            else { minV = std::min(minV, d); maxV = std::max(maxV, d); }
        }
        ++found;
        if (entries.size() < 20000) entries.push_back(std::move(e));
    }
    if (!haveSize) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"NIST Matrix Market Sparse Matrix (.mtx)";
    auto w = [](const std::string& s) { return PathUtils::Utf8ToWide(s); };

    SpreadsheetSheet sheet;
    sheet.name = L"Matrix_Entries";
    sheet.rows.push_back({ L"Entry #", L"Row (i)", L"Column (j)", L"Value (A[i,j])" });
    for (size_t k = 0; k < entries.size(); ++k) {
        sheet.rows.push_back({ std::to_wstring(k + 1), std::to_wstring(entries[k].i), std::to_wstring(entries[k].j), w(entries[k].Text()) });
    }
    sheet.maxCols = 4;
    out.sheets.push_back(std::move(sheet));

    if (rows > 0 && cols > 0 && rows <= 60 && cols <= 60) {
        SpreadsheetSheet dense;
        dense.name = L"Matrix_Dense";
        std::vector<std::wstring> hdr = { L"" };
        for (uint64_t c = 1; c <= cols; ++c) hdr.push_back(std::to_wstring(c));
        dense.rows.push_back(hdr);
        std::vector<std::vector<std::wstring>> grid(static_cast<size_t>(rows), std::vector<std::wstring>(static_cast<size_t>(cols), L"·"));
        for (const auto& e : entries) {
            grid[static_cast<size_t>(e.i - 1)][static_cast<size_t>(e.j - 1)] = w(e.Text());
            // Stored triangle mirrored: A[j,i] = A[i,j] (symmetric), -A[i,j] (skew), conj(A[i,j]) (hermitian)
            bool mirror = (symmetry == "symmetric" || symmetry == "skew-symmetric" || symmetry == "hermitian") && e.i != e.j;
            if (mirror && e.j <= rows && e.i <= cols) {
                grid[static_cast<size_t>(e.j - 1)][static_cast<size_t>(e.i - 1)] =
                    w(e.Text(symmetry == "hermitian", symmetry == "skew-symmetric"));
            }
        }
        for (uint64_t r = 0; r < rows; ++r) {
            std::vector<std::wstring> row = { std::to_wstring(r + 1) };
            row.insert(row.end(), grid[static_cast<size_t>(r)].begin(), grid[static_cast<size_t>(r)].end());
            dense.rows.push_back(std::move(row));
        }
        dense.maxCols = static_cast<size_t>(cols) + 1;
        out.sheets.push_back(std::move(dense));
    }

    SpreadsheetSheet summary;
    summary.name = L"Matrix_Info";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Object / format", w(object + " / " + format) });
    summary.rows.push_back({ L"Field / symmetry", w(field + " / " + symmetry) });
    summary.rows.push_back({ L"Size", std::to_wstring(rows) + L" x " + std::to_wstring(cols) });
    summary.rows.push_back({ L"Entries", std::to_wstring(found) + L" of " + std::to_wstring(declared) + L" declared" });
    if (rows > 0 && cols > 0 && !isArray) {
        wchar_t buf[48];
        swprintf_s(buf, L"%.4g %%", 100.0 * static_cast<double>(found) / (static_cast<double>(rows) * static_cast<double>(cols)));
        summary.rows.push_back({ L"Density (stored entries)", buf });
    }
    if (anyV) {
        wchar_t buf[96];
        swprintf_s(buf, L"%.6g .. %.6g", minV, maxV);
        summary.rows.push_back({ L"Value range", buf });
    }
    if (bad) summary.rows.push_back({ L"Malformed / out-of-range lines (skipped)", std::to_wstring(bad) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
