#pragma once
// JedecPldJed.h — JEDEC JESD3-C PLD/CPLD/GAL Fuse Map (.jed) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseJedecPldJedBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #69. JEDEC JESD3-C PLD/CPLD/GAL Fuse Map (.jed)
    if (ext == L".jed") {
        if (sv.find("*L") == std::string_view::npos && sv.find("QF") == std::string_view::npos &&
            sv.find("\nL") == std::string_view::npos) return false;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"JEDEC JESD3-C PLD Programmable Fuse Matrix (.jed)";

        std::vector<std::string> fuseRows;
        size_t pos = 0;
        while (pos < sv.size() && fuseRows.size() < 128) {
            // JEDEC commands start after '*' + optional whitespace; 'L' starts a fuse-list record terminated by '*'
            size_t lpos = std::string_view::npos;
            for (size_t i = pos; i + 2 < sv.size(); ++i) {
                if (sv[i] == 'L' && sv[i + 1] >= '0' && sv[i + 1] <= '9' &&
                    (i == 0 || sv[i - 1] == '*' || sv[i - 1] == '\n' || sv[i - 1] == '\r' || sv[i - 1] == ' ' || sv[i - 1] == '\t'))
                {
                    lpos = i;
                    break;
                }
            }
            if (lpos == std::string_view::npos) break;
            size_t star = sv.find('*', lpos + 1);
            if (star == std::string_view::npos) break;
            std::string_view blk = sv.substr(lpos + 1, star - lpos - 1);
            pos = star + 1;
            // Skip address digits, collect '0'/'1' fuse bits
            size_t sp = blk.find_first_of(" \t\r\n");
            std::string bits;
            if (sp != std::string_view::npos) {
                for (size_t k = sp; k < blk.size() && bits.size() < 64; ++k) {
                    if (blk[k] == '0' || blk[k] == '1') bits.push_back(blk[k]);
                }
            }
            if (!bits.empty()) fuseRows.push_back(std::move(bits));
        }
        if (fuseRows.empty()) return false;

        int rows = static_cast<int>(fuseRows.size());
        int cols = static_cast<int>(fuseRows[0].size());
        float pitch = 1.4f;
        float w = cols * pitch, h = rows * pitch;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, w + 6.0f, h + 6.0f, EdaLayerId::EdgeCuts, 0.35f);

        for (int r = 0; r < rows; ++r) {
            float y = ((rows - 1) * 0.5f - r) * pitch;
            out.traces.push_back({ -w * 0.5f, y, w * 0.5f, y, 0.10f, EdaLayerId::BottomCopper, 1 });
            for (int c = 0; c < static_cast<int>(fuseRows[r].size()); ++c) {
                float x = (c - (cols - 1) * 0.5f) * pitch;
                if (r == 0) out.traces.push_back({ x, -h * 0.5f, x, h * 0.5f, 0.10f, EdaLayerId::InnerCopper, 1 });
                if (fuseRows[r][c] == '0') {
                    // Intact programmed cross-point fuse!
                    out.circles.push_back({ x, y, 0.36f, 0.18f, true, EdaLayerId::TopCopper });
                }
            }
        }
        return true;
    }
    return false;
}

} // namespace Formats
