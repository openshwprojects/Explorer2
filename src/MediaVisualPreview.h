#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer2 {

struct MediaVisualPreview {
    bool valid = false;
    std::wstring containerName;
    std::wstring codecSummary;
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    double durationSec = 0.0;
    uint32_t videoWidth = 0;
    uint32_t videoHeight = 0;
    uint32_t sampleRateHz = 0;
    uint32_t channels = 0;
    bool hasCoverArt = false;
    std::wstring coverMime;
    uint32_t coverWidth = 0;
    uint32_t coverHeight = 0;
    std::vector<uint8_t> coverBgra;
    std::vector<float> waveformPeaks;
};

bool ExtractMediaVisualPreview(const std::wstring& fullPath, MediaVisualPreview& out);

} // namespace Explorer2
