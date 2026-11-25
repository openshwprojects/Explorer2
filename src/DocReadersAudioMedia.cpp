// DocReadersAudioMedia.cpp — Audio, Trackers, DAWs, Chiptunes, Video Timelines, Subtitles, E-Books & iWork Readers
// Compiled in its own translation unit so DocReaders.cpp stays fast and low-memory on MSVC and Linux GCC.
#include "DocReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#ifdef _WIN32
#include <wincrypt.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Explorer2 {

#include "formats/common/GpsTrackMap.h"
#include "formats/common/DocFormatCommon.h"
#include "formats/audio/CueSheet.h"
#include "formats/audio/PlaylistM3u.h"
#include "formats/audio/PlaylistPls.h"
#include "formats/audio/MidiFile.h"
#include "formats/audio/WavAudio.h"
#include "formats/audio/FlacAudio.h"
#include "formats/audio/AiffAudio.h"
#include "formats/audio/Mp3Audio.h"
#include "formats/audio/OggVorbisAudio.h"
#include "formats/audio/OpusAudio.h"
#include "formats/audio/SpeexAudio.h"
#include "formats/audio/TrackerMod.h"
#include "formats/audio/TrackerXm.h"
#include "formats/audio/TrackerS3m.h"
#include "formats/audio/TrackerIt.h"
#include "formats/audio/SoundFontSf2.h"
#include "formats/audio/SoundFontSfz.h"
#include "formats/audio/MuseScoreMscz.h"
#include "formats/audio/AbletonAls.h"
#include "formats/audio/FlStudioFlp.h"
#include "formats/audio/GuitarProTab.h"
#include "formats/audio/ReaperRpp.h"
#include "formats/audio/ChiptuneSid.h"
#include "formats/audio/ChiptuneNsf.h"
#include "formats/audio/ChiptuneSpc.h"
#include "formats/audio/ChiptuneVgm.h"
#include "formats/audio/MusicXmlScore.h"
#include "formats/docs/EmailEml.h"
#include "formats/docs/UnixMbox.h"
#include "formats/docs/CmxEdl.h"
#include "formats/docs/FinalCutFcpxml.h"
#include "formats/docs/OpenTimelineOtio.h"
#include "formats/docs/VobSubIdx.h"
#include "formats/docs/VobSubMpegSub.h"
#include "formats/docs/BluraySup.h"
#include "formats/docs/MicroDvdSub.h"
#include "formats/docs/SubViewerSub.h"
#include "formats/docs/KindleMobi.h"
#include "formats/docs/DjVuDoc.h"
#include "formats/docs/ChmHelp.h"
#include "formats/docs/AdobeXmp.h"
#include "formats/docs/UnrealAssetDoc.h"
#include "formats/docs/UnityYamlScene.h"
#include "formats/docs/VisioVsdx.h"
#include "formats/docs/AppleIwa.h"
#include "formats/docs/ApplePages.h"
#include "formats/docs/AppleNumbers.h"
#include "formats/docs/AppleKeynote.h"

namespace DocReaders {

#if defined(_MSC_VER)
#define E2_DOCAUD_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_DOCAUD_NOINLINE __attribute__((noinline))
#else
#define E2_DOCAUD_NOINLINE
#endif

namespace {

E2_DOCAUD_NOINLINE bool DispatchAudioScoresAndChiptunes(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".cue")     return Formats::ParseCueSheetDoc(raw, out);
    if (extLower == L".m3u" || extLower == L".m3u8") return Formats::ParsePlaylistM3uDoc(raw, out);
    if (extLower == L".pls")     return Formats::ParsePlaylistPlsDoc(raw, out);
    if (extLower == L".mid" || extLower == L".midi") return Formats::ParseMidiFileDoc(raw, out);
    if (extLower == L".wav")     return Formats::ParseWavAudioDoc(raw, out);
    if (extLower == L".flac")    return Formats::ParseFlacAudioDoc(raw, out);
    if (extLower == L".aiff" || extLower == L".aif" || extLower == L".aifc") return Formats::ParseAiffAudioDoc(raw, out);
    if (extLower == L".mp3")     return Formats::ParseMp3AudioDoc(raw, out);
    if (extLower == L".opus")    return Formats::ParseOpusAudioDoc(raw, extLower, out) || Formats::ParseOggVorbisAudioDoc(raw, extLower, out);
    if (extLower == L".spx")     return Formats::ParseSpeexAudioDoc(raw, extLower, out) || Formats::ParseOggVorbisAudioDoc(raw, extLower, out);
    if (extLower == L".ogg")     return Formats::ParseOggVorbisAudioDoc(raw, extLower, out) || Formats::ParseOpusAudioDoc(raw, extLower, out) || Formats::ParseSpeexAudioDoc(raw, extLower, out);
    if (extLower == L".mod")     return Formats::ParseTrackerModDoc(raw, out);
    if (extLower == L".xm")      return Formats::ParseTrackerXmDoc(raw, out);
    if (extLower == L".s3m")     return Formats::ParseTrackerS3mDoc(raw, out);
    if (extLower == L".it")      return Formats::ParseTrackerItDoc(raw, out);
    if (extLower == L".sf2")     return Formats::ParseSoundFontSf2Doc(raw, out);
    if (extLower == L".sfz")     return Formats::ParseSoundFontSfzDoc(raw, out);
    if (extLower == L".mscz")    return Formats::ParseMuseScoreMsczDoc(raw, out);
    if (extLower == L".als")     return Formats::ParseAbletonAlsDoc(raw, out);
    if (extLower == L".flp")     return Formats::ParseFlStudioFlpDoc(raw, out);
    if (extLower == L".gp" || extLower == L".gp3" || extLower == L".gp4" || extLower == L".gp5") return Formats::ParseGuitarProTabDoc(raw, extLower, out);
    if (extLower == L".rpp")     return Formats::ParseReaperRppDoc(raw, out);
    if (extLower == L".sid")     return Formats::ParseChiptuneSidDoc(raw, out);
    if (extLower == L".nsf" || extLower == L".nsfe") return Formats::ParseChiptuneNsfDoc(raw, out);
    if (extLower == L".spc")     return Formats::ParseChiptuneSpcDoc(raw, out);
    if (extLower == L".vgm" || extLower == L".vgz") return Formats::ParseChiptuneVgmDoc(raw, out);
    if (extLower == L".musicxml" || extLower == L".mxl") return Formats::ParseMusicXmlScoreDoc(raw, extLower, out);
    return false;
}

E2_DOCAUD_NOINLINE bool DispatchTimelinesBooksAndPublishing(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".eml")     return Formats::ParseEmailEmlDoc(raw, out);
    if (extLower == L".mbox")    return Formats::ParseUnixMboxDoc(raw, out);
    if (extLower == L".edl")     return Formats::ParseCmxEdlDoc(raw, out);
    if (extLower == L".fcpxml")  return Formats::ParseFinalCutFcpxmlDoc(raw, out);
    if (extLower == L".otio")    return Formats::ParseOpenTimelineOtioDoc(raw, out);
    if (extLower == L".idx")     return Formats::ParseVobSubIdxDoc(raw, out);
    if (extLower == L".sup")     return Formats::ParseBluraySupDoc(raw, out);
    if (extLower == L".sub")     return Formats::ParseVobSubSpuDoc(raw, out) || Formats::ParseMicroDvdSubDoc(raw, out) || Formats::ParseSubViewerSubDoc(raw, out);
    if (extLower == L".mobi" || extLower == L".azw" || extLower == L".azw3" || extLower == L".prc") return Formats::ParseKindleMobiDoc(raw, extLower, out);
    if (extLower == L".djvu" || extLower == L".djv") return Formats::ParseDjVuDoc(raw, out);
    if (extLower == L".chm")     return Formats::ParseChmHelpDoc(raw, out);
    if (extLower == L".xmp")     return Formats::ParseAdobeXmpDoc(raw, extLower, out);
    if (extLower == L".uasset" || extLower == L".umap") return Formats::ParseUnrealAssetDoc(raw, extLower, out);
    if (extLower == L".prefab" || extLower == L".unity") return Formats::ParseUnityYamlSceneDoc(raw, extLower, out);
    if (extLower == L".vsdx")    return Formats::ParseVisioVsdxDoc(raw, extLower, out);
    if (extLower == L".pages")   return Formats::ParseApplePagesDoc(raw, out);
    if (extLower == L".numbers") return Formats::ParseAppleNumbersDoc(raw, out);
    if (extLower == L".key")     return Formats::ParseAppleKeynoteDoc(raw, out);
    if (extLower == L".iwa")     return Formats::ParseAppleIwaDoc(raw, out);
    return false;
}

} // namespace

bool DispatchDocAudioAndPublishing(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& outDoc) {
    return DispatchAudioScoresAndChiptunes(raw, extLower, outDoc) || DispatchTimelinesBooksAndPublishing(raw, extLower, outDoc);
}

} // namespace DocReaders
} // namespace Explorer2
