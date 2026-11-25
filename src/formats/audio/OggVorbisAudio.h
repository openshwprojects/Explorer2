#pragma once
// OggVorbisAudio.h — Xiph Ogg Vorbis (.ogg) Audio Inspector

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseOggVorbis(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    OggStreamWalk walk;
    if (!WalkOggBitstream(raw, out.sourceTail, walk)) return false;

    const auto& bos = walk.headerPackets[0];
    // Defer Opus and Speex streams inside .ogg containers to their dedicated codec readers
    if (bos.size() >= 19 && std::memcmp(bos.data(), "OpusHead", 8) == 0) return false;
    if (bos.size() >= 80 && std::memcmp(bos.data(), "Speex   ", 8) == 0) return false;

    std::wstring codec = L"Ogg Bitstream";
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t granuleRate = 0;
    int32_t nominalBitrate = 0;

    if (bos.size() >= 30 && bos[0] == 0x01 && std::memcmp(bos.data() + 1, "vorbis", 6) == 0) {
        codec = L"Xiph Vorbis";
        channels = bos[11];
        sampleRate = DetailBinarySysDoc::RdU32LE(bos.data() + 12);
        granuleRate = sampleRate;
        nominalBitrate = static_cast<int32_t>(DetailBinarySysDoc::RdU32LE(bos.data() + 20));
    } else if (bos.size() >= 8 && std::memcmp(bos.data(), "\x7F" "FLAC", 5) == 0) {
        codec = L"Ogg FLAC";
    } else if (bos.size() >= 7 && std::memcmp(bos.data(), "\x80theora", 7) == 0) {
        codec = L"Ogg Theora Video";
    }

    std::wstring vendor;
    std::vector<std::pair<std::wstring, std::wstring>> comments;
    if (walk.headerPackets.size() >= 2) {
        const auto& cpkt = walk.headerPackets[1];
        if (cpkt.size() >= 15 && cpkt[0] == 0x03 && std::memcmp(cpkt.data() + 1, "vorbis", 6) == 0) {
            ParseOggVorbisComments(cpkt, 7, vendor, comments);
        }
    }

    return FinalizeOggStreamDoc(
        walk,
        L"Xiph Ogg Vorbis Audio (" + extLower + L")",
        codec, channels, sampleRate, granuleRate, 0, nominalBitrate, L"", vendor, comments, out);
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseOggVorbisAudioDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    return DetailMediaAndScoreDoc::ParseOggVorbis(raw, extLower, out);
}

} // namespace Formats
