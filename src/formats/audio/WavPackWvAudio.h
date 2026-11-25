#pragma once
// WavPackWvAudio.h — WavPack Hybrid/Lossless/DSD Audio ("wvpk", .wv) + APEv2 Tag Reader

namespace Formats {
namespace DetailWavPack {

inline uint32_t StandardSampleRate(uint32_t idx) {
    static const uint32_t kRates[16] = {
        6000, 8000, 9600, 11025, 12000, 16000, 22050, 24000,
        32000, 44100, 48000, 64000, 88200, 96000, 192000, 0
    };
    return kRates[idx & 0x0Fu];
}

inline const wchar_t* SubBlockIdName(uint8_t id) {
    switch (id & 0x3Fu) {
        case 0x02: return L"Decorr Terms";
        case 0x03: return L"Decorr Weights";
        case 0x04: return L"Decorr Samples";
        case 0x05: return L"Entropy Vars";
        case 0x06: return L"Hybrid Profile";
        case 0x07: return L"Shaping Weights";
        case 0x08: return L"Float Info";
        case 0x09: return L"Int32 Info";
        case 0x0A: return L"WV Bitstream";
        case 0x0B: return L"WVC Bitstream";
        case 0x0C: return L"WVX Bitstream";
        case 0x0D: return L"Channel Info";
        case 0x0E: return L"DSD Audio Block";
        case 0x21: return L"RIFF Header";
        case 0x22: return L"RIFF Trailer";
        case 0x25: return L"Config Block";
        case 0x26: return L"MD5 Checksum";
        case 0x27: return L"Custom Sample Rate";
        case 0x2A: return L"CAF Header";
        default:   return L"Metadata Sub-Block";
    }
}

inline void ParseApeV2FooterAndItems(SourceBytes& source, uint64_t fileSize, SpreadsheetSheet& tagSh) {
    if (fileSize < 32) return;
    std::vector<uint8_t> fbuf;
    // APEv2 footer is at EOF, or 128 bytes before EOF if an ID3v1 tag trails it
    uint64_t footOff = fileSize - 32;
    const uint8_t* fp = source.Read(footOff, 32, fbuf);
    if (!fp || std::memcmp(fp, "APETAGEX", 8) != 0) {
        if (fileSize >= 160) {
            footOff = fileSize - 160;
            fp = source.Read(footOff, 32, fbuf);
        }
    }
    if (!fp || std::memcmp(fp, "APETAGEX", 8) != 0) return;

    uint32_t ver     = ::Explorer2::ByteIO::RdU32LE(fp + 8);
    uint32_t tagSize = ::Explorer2::ByteIO::RdU32LE(fp + 12); // items + 32B footer (excludes 32B header)
    uint32_t itemCnt = ::Explorer2::ByteIO::RdU32LE(fp + 16);
    (void)ver;
    if (tagSize <= 32 || tagSize > 16u * 1024u * 1024u || footOff + 32 < tagSize) return;

    uint64_t itemsOff = footOff + 32 - tagSize;
    size_t itemsLen = tagSize - 32;
    std::vector<uint8_t> ibuf;
    const uint8_t* ip = source.Read(itemsOff, itemsLen, ibuf);
    if (!ip) return;

    size_t pos = 0;
    for (uint32_t i = 0; i < itemCnt && i < 512 && pos + 9 <= itemsLen; ++i) {
        uint32_t valLen = ::Explorer2::ByteIO::RdU32LE(ip + pos);
        uint32_t flags  = ::Explorer2::ByteIO::RdU32LE(ip + pos + 4);
        pos += 8;
        size_t keyStart = pos;
        while (pos < itemsLen && ip[pos] != 0) ++pos;
        if (pos >= itemsLen) break;
        std::string key(reinterpret_cast<const char*>(ip + keyStart), pos - keyStart);
        ++pos; // skip NUL
        if (valLen > itemsLen - pos) break;

        uint32_t valType = (flags >> 1) & 0x03u; // 0=UTF-8, 1=Binary, 2=External
        std::wstring valW;
        if (valType == 1) {
            valW = L"(binary " + PathUtils::FormatByteSize(valLen) + L") " + DetailStructuredDataDoc::HexBytes(ip + pos, valLen, 16);
        } else {
            std::string vs(reinterpret_cast<const char*>(ip + pos), std::min<size_t>(valLen, 1024));
            for (char& c : vs) if (c == '\0') c = ';';
            valW = PathUtils::Utf8ToWide(vs);
        }
        tagSh.rows.push_back({ PathUtils::Utf8ToWide(key), valW });
        pos += valLen;
    }
}

} // namespace DetailWavPack

inline bool ParseWavPackWvAudio(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 32 || std::memcmp(raw.data(), "wvpk", 4) != 0) return false;

    SourceBytes source(raw, out);
    const uint64_t fileSize = source.Size();

    const uint32_t ckSize0 = ::Explorer2::ByteIO::RdU32LE(raw.data() + 4);
    const uint16_t version = ::Explorer2::ByteIO::RdU16LE(raw.data() + 8);
    if (version < 0x0402u || version > 0x04FFu || ckSize0 < 24 || ckSize0 > 32u * 1024u * 1024u) return false;

    // 40-bit total_samples & block_index in WavPack 5 (version >= 0x0410)
    const uint32_t totLow = ::Explorer2::ByteIO::RdU32LE(raw.data() + 12);
    const uint8_t  totHi  = (version >= 0x0410u) ? raw[11] : 0u;
    const bool unknownTotal = (totLow == 0xFFFFFFFFu && (version < 0x0410u || totHi == 0xFFu));
    const uint64_t totalSamples = unknownTotal ? 0ULL : ((static_cast<uint64_t>(totHi) << 32) | totLow);
    const uint64_t blockIndex   = ((version >= 0x0410u ? static_cast<uint64_t>(raw[10]) : 0ULL) << 32) | ::Explorer2::ByteIO::RdU32LE(raw.data() + 16);
    const uint32_t blockSamples = ::Explorer2::ByteIO::RdU32LE(raw.data() + 20);
    const uint32_t flags0       = ::Explorer2::ByteIO::RdU32LE(raw.data() + 24);
    const uint32_t crc0         = ::Explorer2::ByteIO::RdU32LE(raw.data() + 28);

    const bool isDsd    = (flags0 & 0x80000000u) != 0;
    const bool isFloat  = (flags0 & 0x00000080u) != 0;
    const bool isHybrid = (flags0 & 0x00000008u) != 0;
    const bool isJoint  = (flags0 & 0x00000010u) != 0;
    const bool isCross  = (flags0 & 0x00000020u) != 0;
    uint32_t channels   = (flags0 & 0x00000004u) ? 1u : 2u;
    uint32_t channelMask = 0;
    uint32_t bytesPerSample = (flags0 & 0x03u) + 1u;
    uint32_t shiftBits      = (flags0 >> 13) & 0x1Fu;
    uint32_t bitDepth       = isDsd ? 1u : (bytesPerSample * 8u - shiftBits);
    uint32_t sampleRate     = DetailWavPack::StandardSampleRate((flags0 >> 23) & 0x0Fu);
    uint32_t dsdMultiplier  = 1u;
    std::wstring md5Hex;
    std::wstring configDesc;

    SpreadsheetSheet subSh;
    subSh.name = L"WavPack_SubBlocks";
    subSh.rows.push_back({ L"Block #", L"Sub-Block ID", L"Type", L"Payload Size", L"Details" });

    // Walk initial multichannel block chain (and up to 8 blocks) to collect metadata sub-blocks
    uint64_t bOff = 0;
    uint32_t chAccum = 0;
    bool chFromSubBlock = false;
    std::vector<uint8_t> blkBuf;
    for (int bi = 0; bi < 8 && bOff + 32 <= fileSize; ++bi) {
        const uint8_t* hdr = source.Read(bOff, 32, blkBuf);
        if (!hdr || std::memcmp(hdr, "wvpk", 4) != 0) break;
        uint32_t ckSz = ::Explorer2::ByteIO::RdU32LE(hdr + 4);
        uint32_t flg  = ::Explorer2::ByteIO::RdU32LE(hdr + 24);
        if (ckSz < 24 || ckSz > 16u * 1024u * 1024u || bOff + 8ULL + ckSz > fileSize) break;
        if (bi == 0 || ((flg & 0x0800u) == 0)) {
            chAccum += (flg & 0x04u) ? 1u : 2u;
        }

        const size_t fullBlkLen = static_cast<size_t>(ckSz + 8u);
        const uint8_t* bp = source.Read(bOff, fullBlkLen, blkBuf);
        if (!bp) break;

        size_t sp = 32;
        while (sp < fullBlkLen && subSh.rows.size() <= 500) {
            uint8_t id = bp[sp++];
            uint32_t words = 0;
            if (id & 0x80u) {
                if (sp + 3 > fullBlkLen) break;
                words = static_cast<uint32_t>(bp[sp]) | (static_cast<uint32_t>(bp[sp + 1]) << 8) | (static_cast<uint32_t>(bp[sp + 2]) << 16);
                sp += 3;
            } else {
                if (sp >= fullBlkLen) break;
                words = bp[sp++];
            }
            size_t storedBytes = static_cast<size_t>(words) * 2u;
            if (storedBytes > fullBlkLen - sp) break;
            size_t actualBytes = (storedBytes > 0 && (id & 0x40u)) ? (storedBytes - 1u) : storedBytes;
            const uint8_t* pay = bp + sp;
            uint8_t coreId = id & 0x3Fu;

            std::wstring det;
            if (coreId == 0x27 && actualBytes >= 3) { // Custom Sample Rate
                uint32_t csr = static_cast<uint32_t>(pay[0]) | (static_cast<uint32_t>(pay[1]) << 8) | (static_cast<uint32_t>(pay[2]) << 16);
                if (actualBytes >= 4) csr |= (static_cast<uint32_t>(pay[3]) << 24);
                sampleRate = csr;
                det = std::to_wstring(csr) + L" Hz";
            } else if (coreId == 0x0D && actualBytes >= 1) { // Channel Info
                if (actualBytes <= 5) {
                    channels = pay[0];
                    for (size_t k = 1; k < actualBytes && k <= 4; ++k) channelMask |= static_cast<uint32_t>(pay[k]) << (8 * (k - 1));
                } else {
                    channels = pay[0] | (static_cast<uint32_t>(pay[1] & 0x0Fu) << 8);
                }
                chFromSubBlock = true;
                det = std::to_wstring(channels) + L" ch (mask=0x" + DetailBinarySysDoc::Hex32(channelMask).substr(2) + L")";
            } else if (coreId == 0x26 && actualBytes == 16) { // Audio MD5
                md5Hex = DetailStructuredDataDoc::HexBytes(pay, 16, 16);
                det = md5Hex;
            } else if (coreId == 0x0E && actualBytes >= 1) { // DSD block
                uint8_t shift = pay[0];
                if (shift < 16) dsdMultiplier = 4u * (1u << shift);
                det = L"DSD rate multiplier x" + std::to_wstring(dsdMultiplier);
            } else if (coreId == 0x25 && actualBytes >= 3) { // Config block
                uint32_t cfg = static_cast<uint32_t>(pay[0]) | (static_cast<uint32_t>(pay[1]) << 8) | (static_cast<uint32_t>(pay[2]) << 16);
                if (cfg & 0x0800u) configDesc += L"HIGH ";
                if (cfg & 0x1000u) configDesc += L"FAST ";
                if (cfg & 0x0040u) configDesc += L"VERY_HIGH ";
                det = PathUtils::Trim(configDesc);
            }

            if (bi == 0) {
                wchar_t idHex[16] = {};
                swprintf_s(idHex, L"0x%02X", coreId);
                subSh.rows.push_back({
                    std::to_wstring(bi),
                    idHex,
                    DetailWavPack::SubBlockIdName(id),
                    PathUtils::FormatByteSize(actualBytes),
                    det
                });
            }
            sp += storedBytes;
        }

        bOff += fullBlkLen;
        if ((flg & 0x1000u) != 0) break; // FINAL_BLOCK reached
    }
    if (!chFromSubBlock && chAccum > channels) channels = chAccum;

    const uint64_t effectiveSampleRate = static_cast<uint64_t>(sampleRate) * (isDsd ? dsdMultiplier : 1u);
    const double durationSec = (sampleRate > 0 && totalSamples > 0)
        ? (static_cast<double>(totalSamples) / static_cast<double>(sampleRate)) // note: in WavPack DSD, totalSamples is byte-samples at base sampleRate
        : 0.0;

    std::wstring modeStr = isDsd ? L"DSD 1-bit" : (isFloat ? L"32-bit IEEE Float" : L"PCM Integer");
    modeStr += isHybrid ? L" (Hybrid)" : L" (Lossless)";
    if (isJoint) modeStr += L", Joint-Stereo";
    if (isCross) modeStr += L", Cross-Decorrelated";

    SpreadsheetSheet sumSh;
    sumSh.name = L"WavPack_Summary";
    sumSh.rows.push_back({ L"WavPack Property", L"Value" });
    wchar_t verBuf[32] = {};
    swprintf_s(verBuf, L"0x%04X (v%u.%u)", version, version >> 8, version & 0xFFu);
    sumSh.rows.push_back({ L"Stream Version", verBuf });
    sumSh.rows.push_back({ L"Encoding Mode", modeStr });
    if (!configDesc.empty()) sumSh.rows.push_back({ L"Encoder Profile", PathUtils::Trim(configDesc) });
    sumSh.rows.push_back({ L"Sample Rate",
        isDsd ? (std::to_wstring(effectiveSampleRate) + L" Hz DSD (base " + std::to_wstring(sampleRate) + L" Hz)")
              : (std::to_wstring(sampleRate) + L" Hz") });
    sumSh.rows.push_back({ L"Channels", std::to_wstring(channels) + (channelMask ? (L" (mask 0x" + DetailBinarySysDoc::Hex32(channelMask).substr(2) + L")") : L"") });
    sumSh.rows.push_back({ L"Bit Depth", std::to_wstring(bitDepth) + L"-bit" });
    sumSh.rows.push_back({ L"Total Samples", unknownTotal ? L"Unknown (Streaming)" : std::to_wstring(totalSamples) });
    sumSh.rows.push_back({ L"First Block Index / Samples", std::to_wstring(blockIndex) + L" / " + std::to_wstring(blockSamples) });
    if (durationSec > 0.0) {
        wchar_t dbuf[64] = {};
        swprintf_s(dbuf, L"%.3f s", durationSec);
        sumSh.rows.push_back({ L"Duration", dbuf });
    }
    sumSh.rows.push_back({ L"First Block CRC", DetailBinarySysDoc::Hex32(crc0) });
    if (!md5Hex.empty()) sumSh.rows.push_back({ L"Audio MD5 Digest", md5Hex });
    FinalizeSheetCols(sumSh);

    SpreadsheetSheet tagSh;
    tagSh.name = L"WavPack_APEv2_Tags";
    tagSh.rows.push_back({ L"APEv2 Tag Key", L"Value" });
    DetailWavPack::ParseApeV2FooterAndItems(source, fileSize, tagSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"WavPack Audio Stream (.wv — " + modeStr + L")";
    if (!source.ReadFailed()) out.sourceReadFully = true;
    out.sheets.push_back(std::move(sumSh));
    if (tagSh.rows.size() > 1) { FinalizeSheetCols(tagSh); out.sheets.push_back(std::move(tagSh)); }
    if (subSh.rows.size() > 1) { FinalizeSheetCols(subSh); out.sheets.push_back(std::move(subSh)); }
    out.valid = true;
    return true;
}

} // namespace Formats
