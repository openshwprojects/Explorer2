#pragma once
// CorelDrawCdr.h — CorelDRAW X4+ ZIP & Classic v3-X3 RIFF/cmpr DISP Preview Decoder (.cdr)

namespace Formats {

inline bool DecodeCorelDrawCdr(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 12) return false;
    const std::wstring label = L"CorelDRAW Artwork (.cdr)";

    // 1. Modern CorelDRAW X4+ ZIP container (previews/thumbnail.png, metadata/thumbnails/page1.bmp, ...)
    if (data[0] == 'P' && data[1] == 'K' && (data[2] == 3 || data[2] == 5)) {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

        // Prioritize canonical CorelDRAW X4+ thumbnail paths before generic ZIP preview entries
        static constexpr std::wstring_view kPriorityPaths[] = {
            L"previews/thumbnail.png",
            L"previews/page1.png",
            L"metadata/thumbnails/page1.bmp",
            L"metadata/thumbnails/thumbnail.bmp",
            L"content/riffdata.cdr"
        };
        for (std::wstring_view want : kPriorityPaths) {
            for (const auto& e : entries) {
                if (e.isDirectory) continue;
                std::wstring norm = PathUtils::ToLower(e.pathInArchive);
                for (wchar_t& ch : norm) if (ch == L'\\') ch = L'/';
                if (norm != want) continue;
                std::vector<uint8_t> buf;
                if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, e, buf) || buf.size() < 16u) continue;
                if (want == L"content/riffdata.cdr") {
                    if (std::memcmp(buf.data(), "RIFF", 4) == 0 &&
                        DetailVectorAndRawImg::WalkRiffCdrForDisp(buf.data(), buf.size(), 12u, buf.size(), 0, label, outInfo))
                    {
                        return true;
                    }
                    continue;
                }
                if (DetailPngPure::DecodePngToImageInfo(buf.data(), buf.size(), label, outInfo)) return true;
                if (buf.size() > 14u && buf[0] == 'B' && buf[1] == 'M' &&
                    DetailVectorAndRawImg::DecodeDibBufferToInfo(buf.data() + 14u, buf.size() - 14u, label, outInfo))
                {
                    return true;
                }
                if (DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(buf.data(), buf.size(), label, outInfo)) return true;
            }
        }
        return DetailVectorAndRawImg::TryDecodeBestZipThumbnail(data, size, label, outInfo);
    }

    // 2. Classic CorelDRAW v3–X3 RIFF container ('RIFF'....'CDR*'/'cdr*' including zlib 'cmpr' sub-containers & 'DISP' DIB)
    if (size >= 16 && std::memcmp(data, "RIFF", 4) == 0 &&
        (std::memcmp(data + 8, "CDR", 3) == 0 || std::memcmp(data + 8, "cdr", 3) == 0 ||
         std::memcmp(data + 8, "CDT", 3) == 0 || std::memcmp(data + 8, "CMX", 3) == 0))
    {
        const uint32_t riffLen = DetailLayeredAndHdrImg::RdU32LE(data + 4);
        size_t riffEnd = std::min<size_t>(size, 8u + static_cast<size_t>(riffLen));
        if (riffEnd < 16u) riffEnd = size;
        return DetailVectorAndRawImg::WalkRiffCdrForDisp(data, size, 12u, riffEnd, 0, label, outInfo);
    }

    return false;
}

} // namespace Formats
