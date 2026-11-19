#pragma once
// UnixMbox.h — Unix Mailbox Archive (.mbox) Reader

namespace Formats {

inline bool ParseUnixMboxDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 8) return false;
    size_t checkLen = std::min<size_t>(raw.size(), 4096);
    size_t nulCount = 0;
    for (size_t i = 0; i < checkLen; ++i) if (raw[i] == 0) ++nulCount;
    if (nulCount * 50 > checkLen) return false;

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"Unix Mailbox Archive (.mbox)";
    int parsedCount = 0;
    if (sv.substr(0, 5) == "From " || sv.find("\nFrom ") != std::string_view::npos) {
        size_t pos = 0;
        while (pos < sv.size() && parsedCount < 64) {
            size_t nextMsg = sv.find("\nFrom ", pos + 5);
            std::string_view slice = (nextMsg != std::string_view::npos)
                ? sv.substr(pos, nextMsg - pos)
                : sv.substr(pos);
            if (DetailMedia100::ParseSingleEmailMessage(slice, parsedCount, out)) ++parsedCount;
            if (nextMsg == std::string_view::npos) break;
            pos = nextMsg + 1;
        }
    } else {
        if (DetailMedia100::ParseSingleEmailMessage(sv, 0, out)) parsedCount = 1;
    }
    if (parsedCount == 0 || out.paragraphs.empty()) return false;
    DetailMedia100::ComputeDocCounts(out);
    out.valid = true;
    return true;
}

} // namespace Formats
