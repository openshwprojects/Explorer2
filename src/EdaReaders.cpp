// EdaReaders.cpp - the board / schematic readers behind EdaViewer: src/formats/*.h, dispatched by extension.
// Moved out of EdaViewer.cpp so that they build without the viewer, also on Linux (see linux/).
// ASIC/FPGA bitstream readers live in EdaReadersAsicBitstream.cpp and oscilloscope/automotive readers live in EdaReadersScopeAuto.cpp.
#include "EdaReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include "ByteIO.h"

#ifndef _WIN32
#include <dirent.h>
#endif

#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <unordered_map>

namespace Explorer2 {

#include "formats/common/FormatPrologueCommon.h"
#include "formats/common/EdaFormatCommon.h"
#include "formats/common/FirmwareMemoryMapEda.h"
#include "formats/common/WaveformPlotCommon.h"

namespace {

#include "formats/eda/EagleBoard.h"
#include "formats/eda/EagleSchematic.h"
#include "formats/eda/EagleLibrary.h"
#include "formats/eda/KiCadPcb.h"
#include "formats/eda/KiCadSchematic.h"
#include "formats/eda/KiCadFootprint.h"
#include "formats/eda/GerberJob.h"
#include "formats/eda/ExcellonDrill.h"
#include "formats/eda/GerberRs274x.h"
#include "formats/eda/KiCadLegacySch.h"
#include "formats/eda/KiCadLegacySymLib.h"
#include "formats/eda/AutoCadDxf.h"
#include "formats/eda/EagleBinary.h"

} // namespace

#include "formats/eda/LtspiceAsc.h"
#include "formats/waveforms/SpiceRawWave.h"
#include "formats/waveforms/HspiceBinaryTr0.h"
#include "formats/eda/SpecctraAutorouterDsn.h"
#include "formats/eda/OrCadCaptureDsn.h"
#include "formats/eda/OrCadSymbolOlb.h"
#include "formats/eda/IpcD356.h"
#include "formats/eda/IntelHex.h"
#include "formats/eda/MotorolaSrec.h"
#include "formats/eda/SpiceNetlist.h"
#include "formats/eda/HpglPlot.h"
#include "formats/eda/TouchstoneSnp.h"
#include "formats/eda/AltiumSchDoc.h"
#include "formats/eda/AltiumPcbDoc.h"
#include "formats/eda/KiCadSymbolLib.h"
#include "formats/eda/SpecctraSessionSes.h"
#include "formats/eda/PickAndPlacePos.h"
#include "formats/eda/IbisModel.h"
#include "formats/eda/EasyEdaStdJson.h"
#include "formats/eda/EasyEdaPro.h"
#include "formats/eda/Ipc2581.h"
#include "formats/eda/OdbPlusPlus.h"
#include "formats/eda/FritzingProject.h"
#include "formats/eda/GedaPcb.h"
#include "formats/eda/HyperLynxPcb.h"

namespace EdaReaders {

#if defined(_MSC_VER)
#define E2_EDA_DISPATCH_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_EDA_DISPATCH_NOINLINE __attribute__((noinline))
#else
#define E2_EDA_DISPATCH_NOINLINE
#endif

namespace {

inline bool IsTouchstoneSnpExtension(const std::wstring& extLower) noexcept {
    if (extLower == L".snp") return true;
    if (extLower.size() >= 4 && extLower[0] == L'.' && extLower[1] == L's' && extLower.back() == L'p') {
        int n = 0;
        for (size_t i = 2; i + 1 < extLower.size(); ++i) {
            if (extLower[i] < L'0' || extLower[i] > L'9') return false;
            n = n * 10 + static_cast<int>(extLower[i] - L'0');
        }
        return n >= 1 && n <= 16;
    }
    return false;
}

inline void ResetEdaBoardForPath(ParsedEdaBoard& outBoard, const std::wstring& fullPath) {
    outBoard = ParsedEdaBoard{};
    outBoard.filePath = fullPath;
}

E2_EDA_DISPATCH_NOINLINE bool DispatchEdaSpiceNetlistCad(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched)
{
    outMatched = true;
    if (ext == L".asc" || ext == L".asy") return Formats::ParseLtspiceBytes(data, size, fullPath, ext, outBoard);
    if (ext == L".raw") {
        if (Formats::ParseLtspiceOrNgspiceRaw(data, size, outBoard)) return true;
        ResetEdaBoardForPath(outBoard, fullPath);
        return Formats::ParseHspiceBinaryTr0Board(data, size, ext, outBoard);
    }
    if (ext == L".tr0" || ext == L".ac0" || ext == L".sw0") {
        if (Formats::ParseHspiceBinaryTr0Board(data, size, ext, outBoard)) return true;
        ResetEdaBoardForPath(outBoard, fullPath);
        return Formats::ParseLtspiceOrNgspiceRaw(data, size, outBoard);
    }
    if (ext == L".olb") {
        if (size >= 512 && std::memcmp(data, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0) {
            if (Formats::ParseOrCadSymbolOlbBoard(data, size, outBoard)) return true;
            ResetEdaBoardForPath(outBoard, fullPath);
            return Formats::ParseOrCadCaptureOle2Board(data, size, outBoard);
        }
        return false;
    }
    if (ext == L".dsn") {
        if (size >= 12) {
            if (size >= 512 && std::memcmp(data, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0) {
                if (Formats::ParseOrCadCaptureOle2Board(data, size, outBoard)) return true;
                ResetEdaBoardForPath(outBoard, fullPath);
                return Formats::ParseOrCadSymbolOlbBoard(data, size, outBoard);
            }
            return Formats::ParseSpecctraSExprBoard(sv, outBoard);
        }
        return false;
    }
    if (ext == L".d356" || ext == L".ipc") return Formats::ParseIpcD356(sv, outBoard);
    if (ext == L".hex" || ext == L".ihex") return Formats::ParseIntelHexBoard(sv, outBoard);
    if (ext == L".srec" || ext == L".s19" || ext == L".mot") return Formats::ParseMotorolaSrecBoard(sv, outBoard);
    if (ext == L".cir" || ext == L".spice" || ext == L".sp" || ext == L".net") return Formats::ParseSpiceNetlistBoard(sv, outBoard);
    if (ext == L".hpgl" || ext == L".plt") return Formats::ParseHpglPlotterBoard(sv, outBoard);
    if (IsTouchstoneSnpExtension(ext) || ext == L".ts") return Formats::ParseTouchstoneSnpBoard(sv, ext, outBoard);
    outMatched = false;
    return false;
}

E2_EDA_DISPATCH_NOINLINE bool DispatchEdaEnterpriseBoard(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled)
{
    outMatched = true;
    outStrictHandled = true;
    if (ext == L".ipc2581" || ext == L".cvg" || (ext == L".odb" && sv.find("<IPC-2581") != std::string_view::npos))
        return Formats::ParseIpc2581Board(sv, outBoard);
    if (ext == L".odb" || ext == L".tgz")
        return Formats::ParseOdbPlusPlusBoard(data, size, ext, outBoard);
    if (ext == L".fzz" || ext == L".fzpz" || ext == L".fz" || ext == L".fzp")
        return Formats::ParseFritzingProjectBoard(data, size, ext, outBoard);
    if (ext == L".pcb")
        return Formats::ParseGedaPcbBoard(sv, outBoard);
    if (ext == L".schdoc" || ext == L".schlib") {
        if (Formats::ParseAltiumSchDocBoard(data, size, ext, outBoard)) return true;
        if (ext == L".schdoc") {
            ResetEdaBoardForPath(outBoard, fullPath);
            return Formats::ParseAltiumPcbDocBoard(data, size, L".pcbdoc", outBoard);
        }
        return false;
    }
    if (ext == L".pcbdoc" || ext == L".pcblib") {
        if (Formats::ParseAltiumPcbDocBoard(data, size, ext, outBoard)) return true;
        if (ext == L".pcbdoc") {
            ResetEdaBoardForPath(outBoard, fullPath);
            return Formats::ParseAltiumSchDocBoard(data, size, L".schdoc", outBoard);
        }
        return false;
    }
    if (ext == L".epro" || ext == L".eprj" || ext == L".epcb" || ext == L".esch" ||
        ext == L".esym" || ext == L".efoo")
        return Formats::ParseEasyEdaProFile(data, size, fullPath, ext, outBoard);
    if (ext == L".json")
        return Formats::ParseEasyEdaStdJsonFile(data, size, outBoard) ||
               Formats::ParseEasyEdaProFile(data, size, fullPath, ext, outBoard);
    if (ext == L".hyp")
        return Formats::ParseHyperLynxPcbBoard(sv, outBoard);

    outStrictHandled = false;
    if (ext == L".kicad_sym") return Formats::ParseKiCadSymbolLibBoard(sv, ext, outBoard);
    if (ext == L".ses") return Formats::ParseSpecctraSessionSesBoard(sv, ext, outBoard);
    if (ext == L".pos" || ext == L".cpl" || ext == L".mnt") return Formats::ParsePickAndPlacePosBoard(sv, ext, outBoard);
    if (ext == L".ibs") return Formats::ParseIbisModelBoard(data, size, ext, outBoard);
    outMatched = false;
    return false;
}

} // namespace

bool IsEdaExtension(const std::wstring& extLower) {
    return extLower == L".kicad_pcb" || extLower == L".kicad_sch" || extLower == L".kicad_mod" ||
           extLower == L".brd"       || extLower == L".sch"       || extLower == L".lbr"       ||
           extLower == L".gbr"       || extLower == L".ger"       || extLower == L".gtl"       ||
           extLower == L".gbl"       || extLower == L".gts"       || extLower == L".gbs"       ||
           extLower == L".gto"       || extLower == L".gbo"       || extLower == L".gm1"       ||
           extLower == L".gko"       || extLower == L".gtp"       || extLower == L".gbp"       ||
           extLower == L".g2"        || extLower == L".g3"        || extLower == L".gp1"       ||
           extLower == L".gp2"       || extLower == L".oln"       || extLower == L".gbrjob"    ||
           extLower == L".drl"       || extLower == L".xln"       || extLower == L".exc"       ||
           extLower == L".dxf"       || extLower == L".asc"       || extLower == L".asy"       ||
           extLower == L".dsn"       || extLower == L".olb"       ||
           extLower == L".d356"      || extLower == L".ipc"       || extLower == L".hex"       ||
           extLower == L".ihex"      || extLower == L".srec"      || extLower == L".s19"       ||
           extLower == L".mot"       || extLower == L".cir"       || extLower == L".spice"     ||
           extLower == L".sp"        || extLower == L".net"       || extLower == L".hpgl"      ||
           extLower == L".plt"       || extLower == L".vcd"       || IsTouchstoneSnpExtension(extLower) ||
           extLower == L".lef"       || extLower == L".def"       ||
           extLower == L".gds"       || extLower == L".gds2"      || extLower == L".bsdl"      || extLower == L".bsd" ||
           extLower == L".svd"       || extLower == L".odb"       || extLower == L".ipc2581"   ||
           extLower == L".cvg"       || extLower == L".pcbdoc"    || extLower == L".schdoc"    ||
           extLower == L".pcblib"    || extLower == L".schlib"    ||
           extLower == L".blf"       || extLower == L".can"       || extLower == L".dbc"       ||
           extLower == L".fst"       || extLower == L".ghw"       || extLower == L".bit"       ||
           extLower == L".rpd"       || extLower == L".dfu"       || extLower == L".ufd"       ||
           extLower == L".edid"      || extLower == L".spd"       ||
           extLower == L".kicad_sym" || extLower == L".ses"       || extLower == L".pos"       ||
           extLower == L".cpl"       || extLower == L".mnt"       || extLower == L".ibs"       ||
           extLower == L".oas"       || extLower == L".xdc"       || extLower == L".sdc"       ||
           extLower == L".qsf"       || extLower == L".jed"       || extLower == L".svf"       ||
           extLower == L".xsvf"      || extLower == L".sof"       || extLower == L".pof"       ||
           extLower == L".sr"        || extLower == L".sal"       || extLower == L".wfm"       ||
           extLower == L".isf"       || extLower == L".mf4"       || extLower == L".mdf"       ||
           extLower == L".ldf"       || extLower == L".arxml"     || extLower == L".eds"       ||
           extLower == L".dcf"       || extLower == L".gsdml"     || extLower == L".l5x"       ||
           extLower == L".raw"       || extLower == L".tr0"       || extLower == L".ac0"       || extLower == L".sw0"  ||
           extLower == L".epro"      || extLower == L".eprj"      || extLower == L".epcb"      || extLower == L".esch" ||
           extLower == L".esym"      || extLower == L".efoo"      || extLower == L".json"      ||
           extLower == L".edif"      || extLower == L".edn"       || extLower == L".blif"      || extLower == L".eblif"||
           extLower == L".aig"       || extLower == L".aag"       || extLower == L".lib"       || extLower == L".sdf"  ||
           extLower == L".tdms"      || extLower == L".tgz"       || extLower == L".fzz"       || extLower == L".fzpz" ||
           extLower == L".fz"        || extLower == L".fzp"       || extLower == L".pcb"       ||
           extLower == L".dtb"       || extLower == L".hyp"       || extLower == L".trc"       ||
           IsModularEdaExtension(extLower);
}

bool IsLikelyOdbPlusPlusTgz(const std::wstring& fullPath) {
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, 8 * 1024 * 1024, raw) || raw.size() < 16) return false;
    return Formats::IsLikelyOdbPlusPlusArchiveBytes(raw.data(), raw.size());
}

bool IsLikelyLibertyOrEdaSdfFile(const std::wstring& fullPath, const std::wstring& extLower) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 8192, hdr) || hdr.size() < 16) return false;
    if (extLower == L".lib") {
        return CheckLikelyLibertyHeader(hdr.data(), hdr.size()) ||
               IsLikelyKiCadLegacySymLibBytes(hdr.data(), hdr.size());
    }
    if (extLower == L".sdf") return CheckLikelyIeeeSdfHeader(hdr.data(), hdr.size());
    return false;
}

bool IsLikelyEasyEdaJsonFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 8192, hdr) || hdr.size() < 24) return false;
    return Formats::IsLikelyEasyEdaJsonBytes(hdr.data(), hdr.size());
}

bool IsLikelySpiceRawFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 1024, hdr) || hdr.size() < 24) return false;
    return Formats::IsLikelySpiceRawHeaderBytes(hdr.data(), hdr.size());
}

bool IsLikelyAsicDefFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 4096, hdr) || hdr.size() < 16) return false;
    return CheckLikelyAsicDefHeader(hdr.data(), hdr.size());
}

bool InspectEdaFile(const std::wstring& fullPath, ParsedEdaBoard& outBoard) try {
    outBoard = ParsedEdaBoard{};
    outBoard.filePath = fullPath;

    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, 32 * 1024 * 1024, raw) || raw.empty()) {
        return false;
    }

    std::wstring ext = PathUtils::GetExtension(fullPath);
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    if (ext == L".dxf") {
        return ParseAutoCadDxf(sv, outBoard);
    }

    // Modular EDA, VLSI, FPGA, Automotive & Waveform formats
    {
        bool handledExt = false;
        bool okMod = false;
        bool matched = false;
        bool strictHandled = false;

        okMod = DispatchEdaSpiceNetlistCad(ext, raw.data(), raw.size(), sv, fullPath, outBoard, matched);
        if (matched) {
            handledExt = true;
        } else {
            okMod = DispatchEdaEnterpriseBoard(ext, raw.data(), raw.size(), sv, fullPath, outBoard, matched, strictHandled);
            if (matched) {
                handledExt = strictHandled;
            } else {
                okMod = DispatchEdaAsicAndBitstream(ext, raw.data(), raw.size(), sv, fullPath, outBoard, matched, strictHandled);
                if (matched) {
                    handledExt = strictHandled;
                    if (!okMod && ext == L".lib") {
                        ResetEdaBoardForPath(outBoard, fullPath);
                        okMod = ParseKiCadLegacySymLibBoard(sv, outBoard);
                    }
                } else {
                    okMod = DispatchEdaScopeAndAutomotive(ext, raw.data(), raw.size(), sv, fullPath, outBoard, matched, strictHandled);
                    if (matched) {
                        handledExt = strictHandled;
                    } else if (IsModularEdaExtension(ext)) {
                        handledExt = true;
                        okMod = TryParseModularEda(ext, raw.data(), raw.size(), fullPath, outBoard);
                    }
                }
            }
        }

        if (!okMod && ext == L".asc") {
            ResetEdaBoardForPath(outBoard, fullPath);
            okMod = DispatchEdaCanAscFallback(ext, raw.data(), raw.size(), outBoard);
        }
        if (okMod) {
            FinalizeBoardBounds(outBoard);
            outBoard.valid = true;
            return true;
        }
        if (handledExt) {
            outBoard = ParsedEdaBoard{};
            return false;
        }
    }

    if (DetailKiCadLegacy::StripUtf8BomView(sv).rfind("EESchema Schematic", 0) == 0) {
        return ParseKiCadLegacySch(sv, fullPath, outBoard);
    }

    if (ext == L".kicad_sch" || sv.find("(kicad_sch") != std::string_view::npos) {
        return ParseKiCadSchematic(sv, outBoard);
    }
    if (ext == L".kicad_pcb" || sv.find("(kicad_pcb") != std::string_view::npos) {
        return ParseKiCadPcb(sv, outBoard);
    }
    if (ext == L".kicad_mod" || sv.find("(footprint ") != std::string_view::npos || sv.find("(module ") != std::string_view::npos) {
        return ParseKiCadFootprint(sv, outBoard);
    }

    if (sv.find("<eagle") != std::string_view::npos || sv.find("<board>") != std::string_view::npos ||
        sv.find("<schematic>") != std::string_view::npos || ext == L".brd" || ext == L".sch" || ext == L".lbr")
    {
        if (sv.find('<') != std::string_view::npos) {
            bool okXml = false;
            if (ext == L".sch" || sv.find("<schematic") != std::string_view::npos) {
                okXml = ParseEagleSchematicXml(sv, outBoard);
            } else if (ext == L".lbr" || (sv.find("<library") != std::string_view::npos && sv.find("<board") == std::string_view::npos)) {
                okXml = ParseEagleLibraryXml(sv, outBoard);
            } else {
                okXml = ParseEagleBoardXml(sv, outBoard);
            }
            if (okXml) return true;
        }
        outBoard = ParsedEdaBoard{};
        outBoard.filePath = fullPath;
        if (ParseEagleBinaryV4V5(raw.data(), raw.size(), ext, outBoard)) {
            return true;
        }
        outBoard = ParsedEdaBoard{};
        return false;
    }

    const bool isGerberOrDrillExt =
        ext == L".gbr" || ext == L".ger" || ext == L".gtl" || ext == L".gbl" ||
        ext == L".gts" || ext == L".gbs" || ext == L".gto" || ext == L".gbo" ||
        ext == L".gm1" || ext == L".gko" || ext == L".gtp" || ext == L".gbp" ||
        ext == L".g2"  || ext == L".g3"  || ext == L".gp1" || ext == L".gp2" ||
        ext == L".oln" || ext == L".gbrjob" || ext == L".drl" || ext == L".xln" || ext == L".exc";
    if (!isGerberOrDrillExt && sv.find("%FS") == std::string_view::npos && sv.rfind("M48", 0) != 0) {
        outBoard = ParsedEdaBoard{};
        return false;
    }

    if (sv.empty() || sv.size() > 64 * 1024 * 1024) {
        outBoard = ParsedEdaBoard{};
        return false;
    }
    for (unsigned char c : sv) {
        if (c == 0) { outBoard = ParsedEdaBoard{}; return false; }
    }
    if (ext == L".gbrjob") {
        if (ParseGerberJobJson(sv, outBoard)) return true;
        outBoard = ParsedEdaBoard{};
        return false;
    }
    std::string_view trimmed = sv;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r' || trimmed.front() == '\n')) {
        trimmed.remove_prefix(1);
    }
    const bool looksLikeExcellon =
        ext == L".drl" || ext == L".xln" || ext == L".exc" ||
        trimmed.rfind("M48", 0) == 0;
    if (looksLikeExcellon && trimmed.find("%FS") == std::string_view::npos && trimmed.find("%ADD") == std::string_view::npos) {
        return ParseExcellonDrillFile(sv, outBoard);
    }
    return ParseGerberRs274x(sv, ext, outBoard);
} E2_READER_CATCH(L"Board reader", fullPath, { outBoard = ParsedEdaBoard(); return false; })

} // namespace EdaReaders

} // namespace Explorer2
