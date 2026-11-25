#pragma once
// SpeexAudio.h — Xiph Speex Audio (.spx) Inspector

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseSpeexAudio(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    OggStreamWalk walk;
    if (!WalkOggBitstream(raw, out.sourceTail, walk)) return false;

    const auto& bos = walk.headerPackets[0];
    if (bos.size() < 80 || std::memcmp(bos.data(), "Speex   ", 8) != 0) return false;

    std::wstring ver = FixedAsciiField(bos.data() + 8, 20);
    std::wstring codec = L"Xiph Speex " + ver;
    uint32_t sampleRate = DetailBinarySysDoc::RdU32LE(bos.data() + 36);
    uint32_t granuleRate = sampleRate;
    uint32_t mode = DetailBinarySysDoc::RdU32LE(bos.data() + 40);
    uint32_t channels = DetailBinarySysDoc::RdU32LE(bos.data() + 48);
    int32_t nominalBitrate = static_cast<int32_t>(DetailBinarySysDoc::RdU32LE(bos.data() + 52));
    std::wstring extraCodecInfo = (mode == 0) ? L"Narrowband" : ((mode == 1) ? L"Wideband" : L"Ultra-Wideband");

    std::wstring vendor;
    std::vector<std::pair<std::wstring, std::wstring>> comments;
    if (walk.headerPackets.size() >= 2) {
        const auto& cpkt = walk.headerPackets[1];
        if (cpkt.size() >= 8) {
            ParseOggVorbisComments(cpkt, 0, vendor, comments);
        }
    }

    return FinalizeOggStreamDoc(
        walk,
        L"Xiph Speex Audio (" + extLower + L")",
        codec, channels, sampleRate, granuleRate, 0, nominalBitrate, extraCodecInfo, vendor, comments, out);
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseSpeexAudioDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    return DetailMediaAndScoreDoc::ParseSpeexAudio(raw, extLower, out);
}

} // namespace Formats
