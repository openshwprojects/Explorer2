#pragma once
// OpusAudio.h — IETF / Xiph Opus Audio (.opus) Inspector

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseOpusAudio(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    OggStreamWalk walk;
    if (!WalkOggBitstream(raw, out.sourceTail, walk)) return false;

    const auto& bos = walk.headerPackets[0];
    if (bos.size() < 19 || std::memcmp(bos.data(), "OpusHead", 8) != 0) return false;

    std::wstring codec = L"IETF / Xiph Opus (v" + std::to_wstring(bos[8]) + L")";
    uint32_t channels = bos[9];
    uint16_t preSkip = DetailBinarySysDoc::RdU16LE(bos.data() + 10);
    uint32_t sampleRate = DetailBinarySysDoc::RdU32LE(bos.data() + 12);
    uint32_t granuleRate = 48000;
    int16_t gain = static_cast<int16_t>(DetailBinarySysDoc::RdU16LE(bos.data() + 16));
    std::wstring extraCodecInfo = L"PreSkip=" + std::to_wstring(preSkip) + L", Gain=" + std::to_wstring(gain) + L", MapFamily=" + std::to_wstring(bos[18]);

    std::wstring vendor;
    std::vector<std::pair<std::wstring, std::wstring>> comments;
    if (walk.headerPackets.size() >= 2) {
        const auto& cpkt = walk.headerPackets[1];
        if (cpkt.size() >= 16 && std::memcmp(cpkt.data(), "OpusTags", 8) == 0) {
            ParseOggVorbisComments(cpkt, 8, vendor, comments);
        }
    }

    return FinalizeOggStreamDoc(
        walk,
        L"IETF Opus Audio (" + extLower + L")",
        codec, channels, sampleRate, granuleRate, preSkip, 0, extraCodecInfo, vendor, comments, out);
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseOpusAudioDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    return DetailMediaAndScoreDoc::ParseOpusAudio(raw, extLower, out);
}

} // namespace Formats
