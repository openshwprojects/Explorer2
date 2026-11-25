#pragma once
// CoreAudioCaf.h — Apple Core Audio Format ("caff" v1, .caf) Streaming Reader

namespace Formats {
namespace DetailCoreAudioCaf {

inline std::wstring DescribeCafFormatId(const char id[4], uint32_t fmtFlags) {
    std::string s(id, 4);
    if (s == "lpcm") {
        bool isFloat = (fmtFlags & 1u) != 0;
        bool isLE    = (fmtFlags & 2u) != 0;
        return std::wstring(L"Linear PCM (") + (isFloat ? L"Float, " : L"Integer, ") + (isLE ? L"Little-Endian)" : L"Big-Endian)");
    }
    if (s == "alac") return L"Apple Lossless (ALAC)";
    if (s == "aac ") return L"MPEG-4 AAC";
    if (s == "flac") return L"Free Lossless Audio Codec (FLAC)";
    if (s == "opus") return L"Opus Interactive Audio";
    if (s == "ima4") return L"Apple IMA 4:1 ADPCM";
    if (s == ".mp3") return L"MPEG-1 Layer III (MP3)";
    if (s == "ulaw") return L"ITU-T G.711 mu-Law";
    if (s == "alaw") return L"ITU-T G.711 A-Law";
    if (s == "ac-3") return L"Dolby Digital (AC-3)";
    if (s == "ec-3") return L"Dolby Digital Plus (E-AC-3)";
    return L"FourCC '" + DetailMediaAndScoreDoc::FixedAsciiField(reinterpret_cast<const uint8_t*>(id), 4) + L"'";
}

inline std::wstring DescribeCafChannelLayout(uint32_t layoutTag, uint32_t bitmap) {
    uint16_t code = static_cast<uint16_t>(layoutTag >> 16);
    uint16_t chs  = static_cast<uint16_t>(layoutTag & 0xFFFFu);
    switch (code) {
        case 0:   return bitmap ? (L"Channel Bitmap 0x" + DetailBinarySysDoc::Hex32(bitmap).substr(2)) : L"Use Channel Descriptions";
        case 100: return L"Mono (1.0)";
        case 101: return L"Stereo (L, R)";
        case 102: return L"Stereo Headphones";
        case 108: return L"Quadraphonic (4.0)";
        case 113: return L"MPEG 3.0";
        case 115: return L"MPEG 4.0";
        case 117: case 118: case 119: case 120: return L"MPEG 5.1 Surround (" + std::to_wstring(chs) + L" ch)";
        case 121: case 122: case 124: return L"MPEG 7.1 Surround (" + std::to_wstring(chs) + L" ch)";
        case 147: return L"DiscreteInOrder (" + std::to_wstring(chs) + L" ch)";
        default:  return L"LayoutTag " + std::to_wstring(code) + L" (" + std::to_wstring(chs) + L" ch)";
    }
}

inline std::wstring FormatCafDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return L"-";
    int totalMs = static_cast<int>(std::llround(seconds * 1000.0));
    int hrs = totalMs / 3600000;
    int mins = (totalMs % 3600000) / 60000;
    double secs = (totalMs % 60000) / 1000.0;
    wchar_t buf[64] = {};
    if (hrs > 0) swprintf_s(buf, L"%d:%02d:%06.3f (%.3f s)", hrs, mins, secs, seconds);
    else swprintf_s(buf, L"%d:%06.3f (%.3f s)", mins, secs, seconds);
    return buf;
}

} // namespace DetailCoreAudioCaf

inline bool ParseCoreAudioCafDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 44 || std::memcmp(raw.data(), "caff", 4) != 0) return false;
    const uint16_t fileVer   = ::Explorer2::ByteIO::RdU16BE(raw.data() + 4);
    const uint16_t fileFlags = ::Explorer2::ByteIO::RdU16BE(raw.data() + 6);
    if (fileVer != 1) return false;

    SourceBytes source(raw, out);
    const uint64_t fileSize = source.Size();

    bool hasDesc = false;
    double sampleRate = 0.0;
    char fmtFourCC[4] = {};
    uint32_t fmtFlags = 0, bytesPerPkt = 0, framesPerPkt = 0, chPerFrame = 0, bitsPerCh = 0;

    bool hasPakt = false;
    int64_t numPackets = 0, numValidFrames = 0;
    int32_t primingFrames = 0, remainderFrames = 0;

    bool hasData = false;
    uint64_t audioDataBytes = 0;
    uint32_t editCount = 0;
    std::wstring chanDesc;
    size_t cookieBytes = 0;

    SpreadsheetSheet chunkSh;
    chunkSh.name = L"CAF_Chunks";
    chunkSh.rows.push_back({ L"#", L"Chunk Type", L"Offset", L"Payload Size", L"Details" });

    SpreadsheetSheet infoSh;
    infoSh.name = L"CAF_Info_Tags";
    infoSh.rows.push_back({ L"Tag Key", L"Value" });

    SpreadsheetSheet markSh;
    markSh.name = L"CAF_Markers";
    markSh.rows.push_back({ L"Marker ID", L"Type", L"Frame Position", L"Channel" });

    uint64_t pos = 8;
    size_t chunkIdx = 0;
    std::vector<uint8_t> ioBuf;
    while (pos + 12 <= fileSize && chunkIdx < 4096) {
        const uint8_t* chdr = source.Read(pos, 12, ioBuf);
        if (!chdr) break;
        char ctype[4];
        std::memcpy(ctype, chdr, 4);
        for (int i = 0; i < 4; ++i) {
            if (static_cast<uint8_t>(ctype[i]) < 0x20u || static_cast<uint8_t>(ctype[i]) > 0x7Eu) {
                if (chunkIdx == 0) return false;
                pos = fileSize;
                break;
            }
        }
        if (pos == fileSize) break;

        int64_t csizeSigned = ::Explorer2::ByteIO::RdI64BE(chdr + 4);
        uint64_t payOff = pos + 12;
        uint64_t paySize = 0;
        if (csizeSigned == -1) {
            if (std::memcmp(ctype, "data", 4) != 0) return false;
            paySize = fileSize - payOff;
        } else if (csizeSigned < 0 || static_cast<uint64_t>(csizeSigned) > fileSize - payOff) {
            if (chunkIdx == 0) return false;
            paySize = fileSize - payOff;
        } else {
            paySize = static_cast<uint64_t>(csizeSigned);
        }

        ++chunkIdx;
        std::string ctypeStr(ctype, 4);
        std::wstring detail;

        if (ctypeStr == "desc" && paySize >= 32) {
            const uint8_t* p = source.Read(payOff, 32, ioBuf);
            if (!p) return false;
            sampleRate   = ::Explorer2::ByteIO::RdF64BE(p + 0);
            std::memcpy(fmtFourCC, p + 8, 4);
            fmtFlags     = ::Explorer2::ByteIO::RdU32BE(p + 12);
            bytesPerPkt  = ::Explorer2::ByteIO::RdU32BE(p + 16);
            framesPerPkt = ::Explorer2::ByteIO::RdU32BE(p + 20);
            chPerFrame   = ::Explorer2::ByteIO::RdU32BE(p + 24);
            bitsPerCh    = ::Explorer2::ByteIO::RdU32BE(p + 28);
            if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || chPerFrame == 0 || chPerFrame > 256) return false;
            hasDesc = true;
            detail = DetailCoreAudioCaf::DescribeCafFormatId(fmtFourCC, fmtFlags) + L", " +
                     AiDataDetail::FmtDouble(sampleRate) + L" Hz, " + std::to_wstring(chPerFrame) + L" ch";
        } else if (ctypeStr == "pakt" && paySize >= 24) {
            const uint8_t* p = source.Read(payOff, 24, ioBuf);
            if (p) {
                numPackets      = ::Explorer2::ByteIO::RdI64BE(p + 0);
                numValidFrames  = ::Explorer2::ByteIO::RdI64BE(p + 8);
                primingFrames   = ::Explorer2::ByteIO::RdI32BE(p + 16);
                remainderFrames = ::Explorer2::ByteIO::RdI32BE(p + 20);
                hasPakt = true;
                detail = std::to_wstring(numPackets) + L" packets, " + std::to_wstring(numValidFrames) + L" valid frames";
            }
        } else if (ctypeStr == "data" && paySize >= 4) {
            const uint8_t* p = source.Read(payOff, 4, ioBuf);
            if (p) {
                editCount = ::Explorer2::ByteIO::RdU32BE(p);
                hasData = true;
                audioDataBytes = paySize - 4;
                detail = L"Audio stream (" + PathUtils::FormatByteSize(audioDataBytes) +
                         (csizeSigned == -1 ? L", open-ended length=-1)" : L")");
            }
        } else if (ctypeStr == "chan" && paySize >= 12) {
            const uint8_t* p = source.Read(payOff, 12, ioBuf);
            if (p) {
                uint32_t ltag = ::Explorer2::ByteIO::RdU32BE(p + 0);
                uint32_t bmap = ::Explorer2::ByteIO::RdU32BE(p + 4);
                chanDesc = DetailCoreAudioCaf::DescribeCafChannelLayout(ltag, bmap);
                detail = chanDesc;
            }
        } else if (ctypeStr == "info" && paySize >= 4 && paySize <= 4u * 1024u * 1024u) {
            const uint8_t* p = source.Read(payOff, static_cast<size_t>(paySize), ioBuf);
            if (p) {
                uint32_t nEntries = ::Explorer2::ByteIO::RdU32BE(p);
                size_t cur = 4;
                auto readC = [&](std::string& s) -> bool {
                    size_t st = cur;
                    while (cur < paySize && p[cur] != 0) ++cur;
                    if (cur >= paySize) return false;
                    s.assign(reinterpret_cast<const char*>(p + st), cur - st);
                    ++cur;
                    return true;
                };
                for (uint32_t i = 0; i < nEntries && i < 1024 && cur < paySize; ++i) {
                    std::string k, v;
                    if (!readC(k) || !readC(v)) break;
                    infoSh.rows.push_back({ PathUtils::Utf8ToWide(k), PathUtils::Utf8ToWide(v) });
                }
                detail = std::to_wstring(infoSh.rows.size() - 1) + L" metadata string(s)";
            }
        } else if (ctypeStr == "mark" && paySize >= 8 && paySize <= 4u * 1024u * 1024u) {
            const uint8_t* p = source.Read(payOff, static_cast<size_t>(paySize), ioBuf);
            if (p) {
                uint32_t nMarkers = ::Explorer2::ByteIO::RdU32BE(p + 4);
                size_t cur = 8;
                for (uint32_t i = 0; i < nMarkers && i < 1000 && cur + 24 <= paySize; ++i, cur += 24) {
                    std::wstring mtype = DetailMediaAndScoreDoc::FixedAsciiField(p + cur, 4);
                    double fpos = ::Explorer2::ByteIO::RdF64BE(p + cur + 4);
                    uint32_t mid = ::Explorer2::ByteIO::RdU32BE(p + cur + 12);
                    uint32_t mch = ::Explorer2::ByteIO::RdU32BE(p + cur + 20);
                    markSh.rows.push_back({ std::to_wstring(mid), mtype, AiDataDetail::FmtDouble(fpos), std::to_wstring(mch) });
                }
                detail = std::to_wstring(nMarkers) + L" marker(s)";
            }
        } else if (ctypeStr == "kuki") {
            cookieBytes = static_cast<size_t>(paySize);
            detail = L"Codec Magic Cookie (" + std::to_wstring(cookieBytes) + L" B)";
        } else if (ctypeStr == "uuid" && paySize >= 16) {
            const uint8_t* p = source.Read(payOff, 16, ioBuf);
            if (p) detail = L"UUID " + DetailStructuredDataDoc::HexBytes(p, 16, 16);
        }

        chunkSh.rows.push_back({
            std::to_wstring(chunkIdx),
            PathUtils::Utf8ToWide(ctypeStr),
            std::to_wstring(pos),
            PathUtils::FormatByteSize(paySize),
            detail
        });

        pos = payOff + paySize;
    }

    if (!hasDesc) return false;

    uint64_t totalFrames = 0;
    if (hasPakt && numValidFrames > 0) {
        totalFrames = static_cast<uint64_t>(numValidFrames);
    } else if (hasData && bytesPerPkt > 0 && framesPerPkt > 0) {
        totalFrames = (audioDataBytes / bytesPerPkt) * framesPerPkt;
    }
    const double durationSec = (sampleRate > 0.0 && totalFrames > 0) ? (static_cast<double>(totalFrames) / sampleRate) : 0.0;

    SpreadsheetSheet sumSh;
    sumSh.name = L"CAF_Summary";
    sumSh.rows.push_back({ L"Core Audio Property", L"Value" });
    sumSh.rows.push_back({ L"Container Version", L"CAF v" + std::to_wstring(fileVer) + L" (flags=" + std::to_wstring(fileFlags) + L")" });
    sumSh.rows.push_back({ L"Audio Codec", DetailCoreAudioCaf::DescribeCafFormatId(fmtFourCC, fmtFlags) });
    sumSh.rows.push_back({ L"Sample Rate", AiDataDetail::FmtDouble(sampleRate) + L" Hz" });
    sumSh.rows.push_back({ L"Channels per Frame", std::to_wstring(chPerFrame) });
    if (!chanDesc.empty()) sumSh.rows.push_back({ L"Channel Layout", chanDesc });
    if (bitsPerCh > 0) sumSh.rows.push_back({ L"Bits per Channel", std::to_wstring(bitsPerCh) + L"-bit" });
    sumSh.rows.push_back({ L"Packet Geometry",
        std::to_wstring(bytesPerPkt) + L" B/pkt, " + std::to_wstring(framesPerPkt) + L" frames/pkt" +
        (bytesPerPkt == 0 ? L" (VBR)" : L" (CBR)") });
    if (hasPakt) {
        sumSh.rows.push_back({ L"Packet Table (pakt)",
            std::to_wstring(numPackets) + L" packets, " + std::to_wstring(numValidFrames) +
            L" valid frames (priming=" + std::to_wstring(primingFrames) + L", remainder=" + std::to_wstring(remainderFrames) + L")" });
    }
    if (totalFrames > 0) {
        sumSh.rows.push_back({ L"Duration", DetailCoreAudioCaf::FormatCafDuration(durationSec) });
    }
    if (hasData) {
        sumSh.rows.push_back({ L"Audio Data Payload", PathUtils::FormatByteSize(audioDataBytes) + L" (edits=" + std::to_wstring(editCount) + L")" });
    }
    if (cookieBytes > 0) {
        sumSh.rows.push_back({ L"Codec Magic Cookie", std::to_wstring(cookieBytes) + L" B" });
    }
    FinalizeSheetCols(sumSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apple Core Audio Format (.caf — " + DetailCoreAudioCaf::DescribeCafFormatId(fmtFourCC, fmtFlags) + L")";
    if (!source.ReadFailed()) out.sourceReadFully = true;
    out.sheets.push_back(std::move(sumSh));
    if (infoSh.rows.size() > 1) { FinalizeSheetCols(infoSh); out.sheets.push_back(std::move(infoSh)); }
    if (markSh.rows.size() > 1) { FinalizeSheetCols(markSh); out.sheets.push_back(std::move(markSh)); }
    FinalizeSheetCols(chunkSh);
    out.sheets.push_back(std::move(chunkSh));
    out.valid = true;
    return true;
}

} // namespace Formats
