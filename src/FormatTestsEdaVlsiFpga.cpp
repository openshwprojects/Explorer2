// FormatTestsEdaVlsiFpga.cpp: Stage 15 EDA, PCB, VLSI, ASIC & FPGA format reader self-tests
#include "SelfTestCommon.h"
#include "TestBytes.h"
#include "EdaViewer.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

using namespace ::Explorer2::TestBytes;

static uint64_t HashEdaBoard(const ParsedEdaBoard& b) {
    uint64_t h = 14695981039346656037ULL;
    auto mix32 = [&](int32_t v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        for (int i = 0; i < 4; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    };
    for (wchar_t c : b.formatTitle) mix32(static_cast<int32_t>(c));
    mix32(static_cast<int32_t>(b.kind));
    mix32(static_cast<int32_t>(b.netCount));
    for (const auto& t : b.traces) {
        mix32(static_cast<int32_t>(std::lround(t.x1 * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(t.y1 * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(t.x2 * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(t.y2 * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(t.width * 1000.0f)));
        mix32(static_cast<int32_t>(t.layer));
    }
    for (const auto& p : b.pads) {
        mix32(static_cast<int32_t>(std::lround(p.x * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(p.y * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(p.w * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(p.h * 1000.0f)));
        mix32(static_cast<int32_t>(p.shape));
    }
    for (const auto& v : b.vias) {
        mix32(static_cast<int32_t>(std::lround(v.x * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(v.y * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(v.drill * 1000.0f)));
    }
    for (const auto& c : b.circles) {
        mix32(static_cast<int32_t>(std::lround(c.cx * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(c.cy * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(c.radius * 1000.0f)));
    }
    for (const auto& comp : b.components) {
        for (wchar_t ch : comp.refDes) mix32(static_cast<int32_t>(ch));
        for (wchar_t ch : comp.value) mix32(static_cast<int32_t>(ch));
        mix32(static_cast<int32_t>(std::lround(comp.x * 1000.0f)));
        mix32(static_cast<int32_t>(std::lround(comp.y * 1000.0f)));
    }
    for (const auto& l : b.labels) {
        for (wchar_t ch : l.text) mix32(static_cast<int32_t>(ch));
    }
    return h;
}

static bool HasLabelSubstr(const ParsedEdaBoard& b, std::wstring_view sub) {
    for (const auto& l : b.labels) {
        if (l.text.find(sub) != std::wstring::npos) return true;
    }
    return false;
}

// Spec-compliant MS-CFB v3 (512B sector, cutoff=0) compound file builder supporting nested '/' storage paths
static std::vector<uint8_t> BuildOle2HierarchicalFile(
    const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& streams)
{
    struct Node {
        std::wstring name;
        uint8_t objType = 2; // 5=Root, 1=Storage, 2=Stream
        uint32_t left = 0xFFFFFFFFu;
        uint32_t right = 0xFFFFFFFFu;
        uint32_t child = 0xFFFFFFFFu;
        uint32_t startSec = 0xFFFFFFFEu;
        uint32_t byteSize = 0;
        std::vector<uint8_t> payload;
        std::vector<uint32_t> children;
    };

    std::vector<Node> nodes(1);
    nodes[0].name = L"Root Entry";
    nodes[0].objType = 5;

    auto findOrAddStorage = [&](uint32_t parentIdx, const std::wstring& segName) -> uint32_t {
        for (uint32_t cid : nodes[parentIdx].children) {
            if (nodes[cid].objType == 1 && nodes[cid].name == segName) {
                return cid;
            }
        }
        const uint32_t newId = static_cast<uint32_t>(nodes.size());
        Node sn;
        sn.name = segName;
        sn.objType = 1;
        nodes.push_back(std::move(sn));
        nodes[parentIdx].children.push_back(newId);
        return newId;
    };

    for (const auto& [fullPath, bytes] : streams) {
        uint32_t curParent = 0u;
        size_t pos = 0u;
        while (pos < fullPath.size()) {
            const size_t sl = fullPath.find(L'/', pos);
            if (sl == std::wstring::npos) {
                const uint32_t leafId = static_cast<uint32_t>(nodes.size());
                Node ln;
                ln.name = fullPath.substr(pos);
                ln.objType = 2;
                ln.payload = bytes;
                ln.byteSize = static_cast<uint32_t>(bytes.size());
                nodes.push_back(std::move(ln));
                nodes[curParent].children.push_back(leafId);
                break;
            }
            curParent = findOrAddStorage(curParent, fullPath.substr(pos, sl - pos));
            pos = sl + 1u;
        }
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& ch = nodes[i].children;
        if (!ch.empty()) {
            nodes[i].child = ch[0];
            for (size_t k = 0; k + 1u < ch.size(); ++k) {
                nodes[ch[k]].right = ch[k + 1u];
            }
        }
    }

    const uint32_t dirSecs = static_cast<uint32_t>((nodes.size() + 3u) / 4u);
    uint32_t nextSec = 1u + dirSecs;
    std::vector<uint32_t> fat(128, 0xFFFFFFFFu);
    fat[0] = 0xFFFFFFFDu; // FATSECT
    for (uint32_t d = 0; d < dirSecs; ++d) {
        fat[1u + d] = (d + 1u < dirSecs) ? (2u + d) : 0xFFFFFFFEu;
    }

    std::vector<uint8_t> streamData;
    for (auto& n : nodes) {
        if (n.objType != 2 || n.byteSize == 0u) continue;
        n.startSec = nextSec;
        const uint32_t secsNeeded = (n.byteSize + 511u) / 512u;
        for (uint32_t s = 0; s < secsNeeded; ++s) {
            const uint32_t cur = nextSec + s;
            if (cur >= fat.size()) fat.resize(cur + 128u, 0xFFFFFFFFu);
            fat[cur] = (s + 1u < secsNeeded) ? (cur + 1u) : 0xFFFFFFFEu;
        }
        nextSec += secsNeeded;
        const size_t oldSz = streamData.size();
        streamData.resize(oldSz + static_cast<size_t>(secsNeeded) * 512u, 0u);
        std::memcpy(streamData.data() + oldSz, n.payload.data(), n.payload.size());
    }

    std::vector<uint8_t> file(512u + static_cast<size_t>(1u + dirSecs) * 512u + streamData.size(), 0u);
    static const uint8_t kOleMagic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    std::memcpy(file.data(), kOleMagic, 8);
    PutU16LE(file.data() + 24, 0x003Eu);
    PutU16LE(file.data() + 26, 0x0003u);
    PutU16LE(file.data() + 28, 0xFFFEu);
    PutU16LE(file.data() + 30, 9u);
    PutU16LE(file.data() + 32, 6u);
    PutU32LE(file.data() + 44, 1u);
    PutU32LE(file.data() + 48, 1u);
    PutU32LE(file.data() + 56, 0u);          // miniStreamCutoff = 0 -> all streams in main FAT
    PutU32LE(file.data() + 60, 0xFFFFFFFEu);
    PutU32LE(file.data() + 68, 0xFFFFFFFEu);
    PutU32LE(file.data() + 76, 0u);
    for (int i = 1; i < 109; ++i) PutU32LE(file.data() + 76 + i * 4, 0xFFFFFFFFu);

    for (size_t i = 0; i < 128u; ++i) {
        PutU32LE(file.data() + 512u + i * 4u, fat[i]);
    }

    uint8_t* dirBase = file.data() + 1024u;
    for (size_t i = 0; i < static_cast<size_t>(dirSecs) * 4u; ++i) {
        uint8_t* ent = dirBase + i * 128u;
        PutU32LE(ent + 68, 0xFFFFFFFFu);
        PutU32LE(ent + 72, 0xFFFFFFFFu);
        PutU32LE(ent + 76, 0xFFFFFFFFu);
        if (i >= nodes.size()) continue;
        const Node& n = nodes[i];
        const size_t wlen = std::min<size_t>(n.name.size(), 31u);
        for (size_t c = 0; c < wlen; ++c) {
            PutU16LE(ent + c * 2u, static_cast<uint16_t>(n.name[c]));
        }
        PutU16LE(ent + 64, static_cast<uint16_t>((wlen + 1u) * 2u));
        ent[66] = n.objType;
        ent[67] = 1u;
        PutU32LE(ent + 68, n.left);
        PutU32LE(ent + 72, n.right);
        PutU32LE(ent + 76, n.child);
        PutU32LE(ent + 116, n.startSec);
        PutU32LE(ent + 120, n.byteSize);
    }

    if (!streamData.empty()) {
        std::memcpy(file.data() + 1024u + static_cast<size_t>(dirSecs) * 512u, streamData.data(), streamData.size());
    }
    return file;
}

static void AppendOrCadStrZero(std::vector<uint8_t>& v, std::string_view s) {
    AppendU16LE(v, static_cast<uint16_t>(s.size()));
    AppendStr(v, s);
    v.push_back(0u);
}

static std::vector<uint8_t> WrapOrCadStruct(
    uint8_t typeId,
    const std::vector<std::pair<uint32_t, uint32_t>>& props,
    const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> shortPfx;
    shortPfx.push_back(typeId);
    AppendU16LE(shortPfx, static_cast<uint16_t>(props.size()));
    for (const auto& [ni, vi] : props) {
        AppendU32LE(shortPfx, ni);
        AppendU32LE(shortPfx, vi);
    }
    std::vector<uint8_t> out;
    out.push_back(typeId);
    AppendU32LE(out, static_cast<uint32_t>(shortPfx.size() + body.size()));
    AppendU32LE(out, 0u);
    out.insert(out.end(), shortPfx.begin(), shortPfx.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static uint16_t ComputeJbiCrcReflected(const uint8_t* data, size_t len) noexcept {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408u) : static_cast<uint16_t>(crc >> 1);
        }
    }
    return static_cast<uint16_t>(~crc);
}

} // namespace

void RunFormatTestsEdaVlsiFpga(TestCounter& tc) {
    const std::wstring edaDir = TestDataDir() + L"\\eda";
    const std::wstring waveformsDir = TestDataDir() + L"\\waveforms";
    const std::wstring edaWorkDir = ScratchTestDir() + L"\\eda_vlsi_fpga";
    CreateDirectoryW(edaWorkDir.c_str(), nullptr);
    std::vector<std::wstring> createdEdaFiles;

    auto writeEdaBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = edaWorkDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdEdaFiles.push_back(p);
        return p;
    };
    auto writeEdaText = [&](const wchar_t* name, const char* text) -> std::wstring {
        std::wstring p = edaWorkDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, text, std::strlen(text));
        createdEdaFiles.push_back(p);
        return p;
    };
    auto assertEdaRejected = [&](const wchar_t* testName, const std::wstring& path) {
        ParsedEdaBoard badBrd;
        bool ok = EdaViewer::InspectEdaFile(path, badBrd);
        tc.Record(!ok && !badBrd.valid && badBrd.traces.empty() && badBrd.pads.empty() &&
                  badBrd.vias.empty() && badBrd.circles.empty() && badBrd.components.empty(),
                  testName, ok ? L"unexpectedly accepted corrupt EDA input" : L"rejected cleanly");
    };

    // ========================================================================
    // Gerber RS-274X / X2, .gbrjob, & Excellon NC Drill
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".gtl") &&
                  EdaViewer::IsEdaExtension(L".gbrjob") &&
                  EdaViewer::IsEdaExtension(L".drl") &&
                  EdaViewer::IsEdaExtension(L".exc"),
                  L"Stage15.Eda.Gerber.ExtensionsRegistered");

        ParsedEdaBoard gtl, gko, job, drl;
        bool okGtl = EdaViewer::InspectEdaFile(edaDir + L"\\gerber_x2_macro_region.gtl", gtl);
        bool okGko = EdaViewer::InspectEdaFile(edaDir + L"\\gerber_tz_outline.gko", gko);
        bool okJob = EdaViewer::InspectEdaFile(edaDir + L"\\gerber_stackup_spec.gbrjob", job);
        bool okDrl = EdaViewer::InspectEdaFile(edaDir + L"\\excellon_slots_rout.drl", drl);

        tc.Record(okGtl && gtl.valid &&
                  gtl.formatTitle.find(L"Copper,L1,Top") != std::wstring::npos &&
                  gtl.pads.size() >= 10 && gtl.circles.size() >= 1 &&
                  gtl.traces.size() >= 30 && !gtl.components.empty() &&
                  gtl.components[0].refDes == L"U1",
                  L"Stage15.Eda.Gerber.X2MacroRegionArcsAndStepRepeat");

        float gkoW = gko.maxX - gko.minX;
        float gkoH = gko.maxY - gko.minY;
        tc.Record(okGko && gko.valid && gko.traces.size() >= 20 &&
                  gkoW >= 49.5f && gkoW <= 56.0f && gkoH >= 34.5f && gkoH <= 41.0f &&
                  HashEdaBoard(gtl) != HashEdaBoard(gko),
                  L"Stage15.Eda.Gerber.TrailingZeroInchOutline");

        tc.Record(okJob && job.valid &&
                  job.formatTitle.find(L"STM32H7_FlightCtrl") != std::wstring::npos &&
                  job.traces.size() >= 12 && job.labels.size() >= 10,
                  L"Stage15.Eda.Gerber.GbrjobStackupAndSpecs");

        tc.Record(okDrl && drl.valid && drl.kind == EdaDocKind::ExcellonDrill &&
                  drl.vias.size() == 12 && drl.traces.size() == 4,
                  L"Stage15.Eda.Gerber.ExcellonG85SlotsRepeatAndRout");

        assertEdaRejected(L"Stage15.Eda.Gerber.RejectUnterminatedGerberBlock",
                          writeEdaText(L"bad_unclosed.gtl", "%FSLAX46Y46*\nX1000Y1000D03*\n"));
        assertEdaRejected(L"Stage15.Eda.Gerber.RejectTruncatedGbrjob",
                          writeEdaText(L"bad_job.gbrjob", "{ \"GeneralSpecs\": { \"Size\": { \"X\": 50, \"Y\": 40 } "));
        assertEdaRejected(L"Stage15.Eda.Gerber.RejectGarbageExcellon",
                          writeEdaText(L"bad_drill.drl", "NOT AN EXCELLON FILE AT ALL\nHELLO WORLD\n"));
    }

    // ========================================================================
    // LTspice Schematic (.asc) & Symbol (.asy) Geometry + R0..M270 Pins
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".asc") && EdaViewer::IsEdaExtension(L".asy"),
                  L"Stage15.Eda.Ltspice.ExtensionsRegistered");

        ParsedEdaBoard asyBrd, buckBrd, opampBrd;
        bool okAsy   = EdaViewer::InspectEdaFile(edaDir + L"\\ltspice_custom_ldo.asy", asyBrd);
        bool okBuck  = EdaViewer::InspectEdaFile(edaDir + L"\\ltspice_buck_ldo_utf16.asc", buckBrd);
        bool okOpamp = EdaViewer::InspectEdaFile(edaDir + L"\\ltspice_opamp_biquad.asc", opampBrd);

        tc.Record(okAsy && asyBrd.valid && asyBrd.pads.size() == 4 && asyBrd.traces.size() >= 20,
                  L"Stage15.Eda.Ltspice.StandaloneAsySymbolAndPins");

        bool sawUnicodeMicro = false, sawPinVout = false, verifiedR90IndPins = false;
        for (const auto& c : buckBrd.components) {
            if (c.refDes == L"L1" && c.value.find(L"\x00B5H") != std::wstring::npos) sawUnicodeMicro = true;
        }
        int indPinHits = 0;
        for (const auto& p : buckBrd.pads) {
            if (p.pinName == L"VOUT") sawPinVout = true;
            if (std::fabs(p.y - (-9.6f)) < 0.05f && (std::fabs(p.x - 24.0f) < 0.05f || std::fabs(p.x - 16.0f) < 0.05f)) {
                ++indPinHits;
            }
        }
        verifiedR90IndPins = (indPinHits >= 2);
        tc.Record(okBuck && buckBrd.valid && buckBrd.components.size() == 7 &&
                  buckBrd.pads.size() >= 17 && sawUnicodeMicro && sawPinVout && verifiedR90IndPins,
                  L"Stage15.Eda.Ltspice.Utf16CompanionAsyAndPinTransforms");

        tc.Record(okOpamp && opampBrd.valid && opampBrd.components.size() == 4 &&
                  HashEdaBoard(buckBrd) != HashEdaBoard(opampBrd),
                  L"Stage15.Eda.Ltspice.OpampBiquadAndDistinctHashes");

        assertEdaRejected(L"Stage15.Eda.Ltspice.RejectPgpArmorAsc",
                          writeEdaText(L"pgp_sig.asc", "-----BEGIN PGP SIGNATURE-----\nVersion: GnuPG v2\niQEzBAABCAAdFiEE\n-----END PGP SIGNATURE-----\n"));
        assertEdaRejected(L"Stage15.Eda.Ltspice.RejectInvalidAsy",
                          writeEdaText(L"bad_sym.asy", "Version 4\nNOT_A_SYMBOL_TYPE\n"));
    }

    // ========================================================================
    // SPICE Simulation Waveforms (.raw, .tr0, .ac0, .sw0)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".raw") &&
                  EdaViewer::IsEdaExtension(L".tr0") &&
                  EdaViewer::IsEdaExtension(L".ac0") &&
                  EdaViewer::IsEdaExtension(L".sw0"),
                  L"Stage15.Eda.SpiceRaw.ExtensionsRegistered");

        ParsedEdaBoard ltBin, ltFast, ngAc, hspTr0;
        bool okLtBin  = EdaViewer::InspectEdaFile(waveformsDir + L"\\ltspice_buck_utf16_bin.raw", ltBin);
        bool okLtFast = EdaViewer::InspectEdaFile(waveformsDir + L"\\ltspice_fastaccess.raw", ltFast);
        bool okNgAc   = EdaViewer::InspectEdaFile(waveformsDir + L"\\ngspice_ac_bode.raw", ngAc);
        bool okHsp    = EdaViewer::InspectEdaFile(waveformsDir + L"\\hspice_cmos_inv.tr0", hspTr0);

        tc.Record(okLtBin && ltBin.valid && ltBin.netCount == 2 && ltBin.traces.size() >= 120 &&
                  EdaViewer::IsLikelySpiceRawFile(waveformsDir + L"\\ltspice_buck_utf16_bin.raw"),
                  L"Stage15.Eda.SpiceRaw.LtspiceUtf16BinaryRaw");

        tc.Record(okLtFast && ltFast.valid && ltFast.netCount == 2 && ltFast.traces.size() >= 90 &&
                  HashEdaBoard(ltBin) != HashEdaBoard(ltFast),
                  L"Stage15.Eda.SpiceRaw.LtspiceFastAccessColumnMajor");

        tc.Record(okNgAc && ngAc.valid && ngAc.netCount == 1 && ngAc.traces.size() >= 45 &&
                  HasLabelSubstr(ngAc, L"dB"),
                  L"Stage15.Eda.SpiceRaw.NgspiceComplexAcBodePlot");

        tc.Record(okHsp && hspTr0.valid && hspTr0.netCount == 3 && hspTr0.traces.size() >= 95 &&
                  hspTr0.formatTitle.find(L".tr0") != std::wstring::npos,
                  L"Stage15.Eda.SpiceRaw.HspiceTransientTr0");

        std::vector<uint8_t> camRawFake(256, 0xA5);
        camRawFake[0] = 'I'; camRawFake[1] = 'I'; camRawFake[2] = 0x55; camRawFake[3] = 0x00;
        std::wstring camRawPath = writeEdaBytes(L"camera_sensor.raw", camRawFake);
        tc.Record(!EdaViewer::IsLikelySpiceRawFile(camRawPath),
                  L"Stage15.Eda.SpiceRaw.DisambiguateCameraRawFalse");
        assertEdaRejected(L"Stage15.Eda.SpiceRaw.RejectCameraRawInEda", camRawPath);

        assertEdaRejected(L"Stage15.Eda.SpiceRaw.RejectTruncatedBinaryRaw",
                          writeEdaText(L"trunc_spice.raw",
                              "Title: Truncated\nPlotname: Transient\nFlags: real\nNo. Variables: 2\nNo. Points: 100\nVariables:\n\t0\ttime\ttime\n\t1\tv(1)\tvoltage\nBinary:\n12345678"));
        assertEdaRejected(L"Stage15.Eda.SpiceRaw.RejectCorruptTr0",
                          writeEdaText(L"corrupt.tr0", "NOT AN HSPICE WAVEFORM FILE\n"));
    }

    // ========================================================================
    // GTKWave Fast Signal Trace (.fst) LZ4 / FastLZ / HIER_LZ4 / Real Analog
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".fst"),
                  L"Stage15.Eda.GtkWaveFst.ExtensionRegistered");

        ParsedEdaBoard fstLz4, fstFlz, fstGw;
        bool okLz4 = EdaViewer::InspectEdaFile(waveformsDir + L"\\verilator_lz4_real.fst", fstLz4);
        bool okFlz = EdaViewer::InspectEdaFile(waveformsDir + L"\\gtkwave_lz4duo_fastlz.fst", fstFlz);
        bool okGw  = EdaViewer::InspectEdaFile(waveformsDir + L"\\gtkwave_gwrapper.fst", fstGw);

        tc.Record(okLz4 && fstLz4.valid && fstLz4.netCount == 4 &&
                  fstLz4.traces.size() >= 80 &&
                  HasLabelSubstr(fstLz4, L"vdd_core [") &&
                  HasLabelSubstr(fstLz4, L"clk_alias"),
                  L"Stage15.Eda.GtkWaveFst.HierLz4WaveLz4AndRealAnalog");

        tc.Record(okFlz && fstFlz.valid && fstFlz.netCount == 3 &&
                  fstFlz.traces.size() >= 60 &&
                  HashEdaBoard(fstLz4) != HashEdaBoard(fstFlz),
                  L"Stage15.Eda.GtkWaveFst.HierLz4DuoAndFastLzWaves");

        tc.Record(okGw && fstGw.valid && fstGw.netCount == 3 && fstGw.traces.size() >= 60,
                  L"Stage15.Eda.GtkWaveFst.GwrapperCompressedContainer");

        std::vector<uint8_t> truncFst;
        PathUtils::ReadFileBytes(waveformsDir + L"\\verilator_lz4_real.fst", 65536, truncFst);
        if (truncFst.size() > 40) truncFst.resize(truncFst.size() - 20);
        assertEdaRejected(L"Stage15.Eda.GtkWaveFst.RejectTruncatedFst",
                          writeEdaBytes(L"trunc.fst", truncFst));
    }

    // ========================================================================
    // ASIC Physical Layout LEF & DEF 5.8 (.lef, .def)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".lef") && EdaViewer::IsEdaExtension(L".def"),
                  L"Stage15.Eda.AsicLefDef.ExtensionsRegistered");

        ParsedEdaBoard lefBrd, defAlu, defSram;
        bool okLef  = EdaViewer::InspectEdaFile(edaDir + L"\\sky130_stdcells_v58.lef", lefBrd);
        bool okDef1 = EdaViewer::InspectEdaFile(edaDir + L"\\sky130_alu_routed_v58.def", defAlu);
        bool okDef2 = EdaViewer::InspectEdaFile(edaDir + L"\\sram_macro_block.def", defSram);

        tc.Record(okLef && lefBrd.valid && lefBrd.components.size() == 3 &&
                  lefBrd.pads.size() >= 10 && lefBrd.traces.size() >= 30,
                  L"Stage15.Eda.AsicLefDef.LefMacrosPolygonPortsAndObs");

        tc.Record(okDef1 && defAlu.valid &&
                  defAlu.formatTitle.find(L"sky130_alu_slice") != std::wstring::npos &&
                  defAlu.components.size() == 5 && defAlu.vias.size() == 3 &&
                  defAlu.pads.size() >= 18 && defAlu.traces.size() >= 35,
                  L"Stage15.Eda.AsicLefDef.DefRectilinearDiePinsSpecialNetsAndWildcards");

        tc.Record(okDef2 && defSram.valid && defSram.components.size() == 2 &&
                  HashEdaBoard(defAlu) != HashEdaBoard(defSram),
                  L"Stage15.Eda.AsicLefDef.DistinctDefDesignHashes");

        std::wstring dllDefPath = writeEdaText(L"exports_dll.def", "LIBRARY ExplorerPlugin\nEXPORTS\n    DllMain @1\n    InitPlugin @2\n");
        tc.Record(!EdaViewer::IsLikelyAsicDefFile(dllDefPath),
                  L"Stage15.Eda.AsicLefDef.DisambiguateWindowsDllDefFalse");
        assertEdaRejected(L"Stage15.Eda.AsicLefDef.RejectWindowsDllDef", dllDefPath);
        assertEdaRejected(L"Stage15.Eda.AsicLefDef.RejectUnclosedMacroLef",
                          writeEdaText(L"unclosed.lef", "VERSION 5.8 ;\nMACRO BROKEN_CELL\n  SIZE 2.0 BY 3.0 ;\n  PIN A\n"));
        assertEdaRejected(L"Stage15.Eda.AsicLefDef.RejectTruncatedComponentsDef",
                          writeEdaText(L"trunc_comp.def", "VERSION 5.8 ;\nDESIGN broken ;\nDIEAREA ( 0 0 ) ( 1000 1000 ) ;\nCOMPONENTS 2 ;\n  - u1 INV_X1 + PLACED ( 100 100 ) N ;\n"));
    }

    // ========================================================================
    // Cadence OrCAD Capture OLE2 (.dsn/.olb) & Specctra PCB S-Expr (.dsn)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".dsn") && EdaViewer::IsEdaExtension(L".olb"),
                  L"Stage15.Eda.OrCadSpecctra.ExtensionsRegistered");

        ParsedEdaBoard orcDsn, orcOlb, orcSymOlb, spcDsn;
        bool okOrc = EdaViewer::InspectEdaFile(edaDir + L"\\orcad_capture_mcu.dsn", orcDsn);
        bool okOlb = EdaViewer::InspectEdaFile(edaDir + L"\\orcad_analog_lib.olb", orcOlb);
        bool okSpc = EdaViewer::InspectEdaFile(edaDir + L"\\specctra_4layer_routed.dsn", spcDsn);

        // Real OrCAD Capture OLE2 .dsn (Structure::Page == 10): 1 page, 2 symbols (U1 STM32G474 4p + U2 INA240A2 3p = 7 pads), 2 nets (SPI_SCK, SHUNT_OUT)
        tc.Record(okOrc && orcDsn.valid &&
                  orcDsn.formatTitle.find(L"1 page") != std::wstring::npos &&
                  orcDsn.components.size() == 2 && orcDsn.pads.size() == 7 &&
                  orcDsn.netCount == 2 && orcDsn.traces.size() >= 18 &&
                  HasLabelSubstr(orcDsn, L"U1 (STM32G474)") &&
                  HasLabelSubstr(orcDsn, L"U2 (INA240A2)") &&
                  HasLabelSubstr(orcDsn, L"SPI_SCK") &&
                  HasLabelSubstr(orcDsn, L"SHUNT_OUT"),
                  L"Stage15.Eda.OrCadSpecctra.OrCadCaptureRealPageOle2Dsn");

        // Real OrCAD OLE2 .olb (Packages/INA240_CSA 3p + Packages/STM32G474 4p = 2 symbols, 7 pads)
        // Plus Symbols/$Types$ + Symbols/OPAMP_DUAL storage layout test
        std::vector<uint8_t> symLibPartBody;
        AppendOrCadStrZero(symLibPartBody, "OPAMP_DUAL.Normal");
        AppendOrCadStrZero(symLibPartBody, "ANALOG.OLB");
        symLibPartBody.insert(symLibPartBody.end(), 4, 0u);
        AppendU16LE(symLibPartBody, 1u);
        symLibPartBody.push_back(40u); symLibPartBody.push_back(40u);
        AppendU32LE(symLibPartBody, 24u); AppendU32LE(symLibPartBody, 0u);
        AppendI32LE(symLibPartBody, 0);   AppendI32LE(symLibPartBody, 0);
        AppendI32LE(symLibPartBody, 40);  AppendI32LE(symLibPartBody, 30);
        AppendU16LE(symLibPartBody, 2u); // 2 pins
        for (int pi = 0; pi < 2; ++pi) {
            std::vector<uint8_t> pb;
            AppendOrCadStrZero(pb, pi == 0 ? "IN+" : "OUT");
            AppendI32LE(pb, pi == 0 ? 0 : 40);
            AppendI32LE(pb, 15);
            AppendI32LE(pb, pi == 0 ? -10 : 50);
            AppendI32LE(pb, 15);
            pb.insert(pb.end(), 12, 0u);
            auto pinSt = WrapOrCadStruct(26u, {}, pb);
            symLibPartBody.insert(symLibPartBody.end(), pinSt.begin(), pinSt.end());
        }
        AppendU16LE(symLibPartBody, 0u);
        std::vector<uint8_t> symStreamBytes = { 0, 0, 0, 0 };
        auto wrappedLibPart = WrapOrCadStruct(24u, {}, symLibPartBody);
        symStreamBytes.insert(symStreamBytes.end(), wrappedLibPart.begin(), wrappedLibPart.end());
        std::vector<uint8_t> typesMarker(24, 0x01u);

        auto olbSymbolsLayout = BuildOle2HierarchicalFile({
            { L"Symbols/$Types$",    typesMarker    },
            { L"Symbols/OPAMP_DUAL", symStreamBytes }
        });
        bool okSymOlb = EdaViewer::InspectEdaFile(writeEdaBytes(L"symbols_types.olb", olbSymbolsLayout), orcSymOlb);

        tc.Record(okOlb && orcOlb.valid &&
                  orcOlb.formatTitle.find(L".olb") != std::wstring::npos &&
                  orcOlb.components.size() == 2 && orcOlb.pads.size() == 7 &&
                  okSymOlb && orcSymOlb.valid &&
                  orcSymOlb.components.size() == 1 && orcSymOlb.pads.size() == 2 &&
                  HashEdaBoard(orcOlb) != HashEdaBoard(orcSymOlb),
                  L"Stage15.Eda.OrCadSpecctra.OrCadSymbolLibraryOlbAndSymbolsTypes");

        float spcW = spcDsn.maxX - spcDsn.minX;
        float spcH = spcDsn.maxY - spcDsn.minY;
        tc.Record(okSpc && spcDsn.valid &&
                  spcDsn.components.size() == 4 && spcDsn.pads.size() == 14 &&
                  spcDsn.vias.size() == 1 && spcDsn.netCount == 3 &&
                  spcW >= 51.0f && spcW <= 56.0f && spcH >= 35.0f && spcH <= 40.0f &&
                  HashEdaBoard(orcDsn) != HashEdaBoard(spcDsn),
                  L"Stage15.Eda.OrCadSpecctra.Specctra4LayerLibraryPadstacksAndWiring");

        // Verify deleted synthetic "ORCPAGE" / "ORCSYM" stream hacks are cleanly rejected!
        std::string fakeOrcPage = "ORCPAGE\0PAGE1\0\x01\x00U1\0STM32\0\x10\x00\x20\x00";
        std::vector<uint8_t> fakeOrcPageVec(fakeOrcPage.data(), fakeOrcPage.data() + 32);
        auto fakeOrcPageOle = BuildOle2HierarchicalFile({
            { L"Views/SCHEMATIC1/Pages/PAGE1", fakeOrcPageVec }
        });
        assertEdaRejected(L"Stage15.Eda.OrCadSpecctra.RejectDeletedSyntheticOrcPageHackDsn",
                          writeEdaBytes(L"synthetic_orcpage.dsn", fakeOrcPageOle));

        auto fakeOrcOlbOle = BuildOle2HierarchicalFile({
            { L"Symbols/$Types$", typesMarker    },
            { L"Symbols/FAKE1",   fakeOrcPageVec }
        });
        assertEdaRejected(L"Stage15.Eda.OrCadSpecctra.RejectDeletedSyntheticOrcPageHackOlb",
                          writeEdaBytes(L"synthetic_orcpage.olb", fakeOrcOlbOle));

        assertEdaRejected(L"Stage15.Eda.OrCadSpecctra.RejectUnclosedSpecctraDsn",
                          writeEdaText(L"unclosed.dsn", "(pcb \"broken.dsn\" (structure (boundary (path pcb 0 0 0 100 0"));
        std::vector<uint8_t> fakeOle(512, 0);
        std::memcpy(fakeOle.data(), "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
        assertEdaRejected(L"Stage15.Eda.OrCadSpecctra.RejectNonOrCadOle2Dsn",
                          writeEdaBytes(L"fake_ole.dsn", fakeOle));
    }

    // ========================================================================
    // Sigrok (.sr multi-chunk/unitsize/analog) & Altium (.pcbdoc/.schdoc/.pcblib/.schlib)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".sr") &&
                  EdaViewer::IsEdaExtension(L".pcbdoc") &&
                  EdaViewer::IsEdaExtension(L".schdoc") &&
                  EdaViewer::IsEdaExtension(L".pcblib") &&
                  EdaViewer::IsEdaExtension(L".schlib"),
                  L"Stage15.Eda.SigrokAltium.ExtensionsRegistered");

        ParsedEdaBoard srBrd, pcbDoc, schDoc, pcbLib, schLib;
        bool okSr     = EdaViewer::InspectEdaFile(waveformsDir + L"\\sigrok_16ch_mixed.sr", srBrd);
        bool okPcbDoc = EdaViewer::InspectEdaFile(edaDir + L"\\altium_polygons_fills.pcbdoc", pcbDoc);
        bool okSchDoc = EdaViewer::InspectEdaFile(edaDir + L"\\altium_multipart_utf8.schdoc", schDoc);
        bool okPcbLib = EdaViewer::InspectEdaFile(edaDir + L"\\altium_footprints.pcblib", pcbLib);
        bool okSchLib = EdaViewer::InspectEdaFile(edaDir + L"\\altium_symbols.schlib", schLib);

        bool sawHeartbeat = false;
        for (const auto& l : srBrd.labels) {
            if (l.text == L"HEARTBEAT") sawHeartbeat = true;
        }
        tc.Record(okSr && srBrd.valid && srBrd.netCount == 14 &&
                  srBrd.traces.size() >= 600 && sawHeartbeat &&
                  HasLabelSubstr(srBrd, L"VCC_3V3 ["),
                  L"Stage15.Eda.SigrokAltium.SigrokMultiChunkUnitsize2AndAnalog");

        bool sawWideStrText = false;
        for (const auto& l : pcbDoc.labels) {
            if (l.text == L"RP2040_REV_B") sawWideStrText = true;
        }
        tc.Record(okPcbDoc && pcbDoc.valid && pcbDoc.components.size() == 1 &&
                  pcbDoc.pads.size() == 1 && pcbDoc.traces.size() >= 20 && sawWideStrText,
                  L"Stage15.Eda.SigrokAltium.AltiumPcbDocPolygonsFillsRegionsWideStrings");

        tc.Record(okSchDoc && schDoc.valid && schDoc.components.size() == 1 &&
                  schDoc.pads.size() == 3 &&
                  schDoc.components[0].value.find(L"\x03A9") != std::wstring::npos,
                  L"Stage15.Eda.SigrokAltium.AltiumSchDocMultiPartFilterAndUtf8");

        tc.Record(okPcbLib && pcbLib.valid && pcbLib.components.size() == 2 && pcbLib.pads.size() == 12 &&
                  okSchLib && schLib.valid && schLib.components.size() == 2 && schLib.pads.size() == 4 &&
                  HashEdaBoard(pcbLib) != HashEdaBoard(schLib),
                  L"Stage15.Eda.SigrokAltium.AltiumPcbLibAndSchLib");

        assertEdaRejected(L"Stage15.Eda.SigrokAltium.RejectGarbageSr",
                          writeEdaText(L"bad.sr", "PK\x03\x04CORRUPT_NOT_A_VALID_SIGROK_ZIP"));
        assertEdaRejected(L"Stage15.Eda.SigrokAltium.RejectGarbagePcbLib",
                          writeEdaText(L"bad.pcblib", "NOT_AN_ALTIUM_LIBRARY"));
    }

    // ========================================================================
    // EasyEDA / JLCPCB Standard (.json) & Pro (.epro, .eprj, .epcb, .esch)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".epro") &&
                  EdaViewer::IsEdaExtension(L".eprj") &&
                  EdaViewer::IsEdaExtension(L".epcb") &&
                  EdaViewer::IsEdaExtension(L".esch") &&
                  EdaViewer::IsEdaExtension(L".json"),
                  L"Stage15.Eda.EasyEda.ExtensionsRegistered");

        ParsedEdaBoard stdPcb, stdSch, proPcb, proSch, proEpro, proEprj;
        bool okStdPcb  = EdaViewer::InspectEdaFile(edaDir + L"\\easyeda_std_pcb.json", stdPcb);
        bool okStdSch  = EdaViewer::InspectEdaFile(edaDir + L"\\easyeda_std_sch.json", stdSch);
        bool okProPcb  = EdaViewer::InspectEdaFile(edaDir + L"\\jlcpcb_pro_board.epcb", proPcb);
        bool okProSch  = EdaViewer::InspectEdaFile(edaDir + L"\\jlcpcb_pro_sheet.esch", proSch);
        bool okProEpro = EdaViewer::InspectEdaFile(edaDir + L"\\jlcpcb_pro_project.epro", proEpro);
        bool okProEprj = EdaViewer::InspectEdaFile(edaDir + L"\\jlcpcb_pro_manifest.eprj", proEprj);

        tc.Record(okStdPcb && stdPcb.valid && stdPcb.components.size() == 2 &&
                  stdPcb.pads.size() == 6 && stdPcb.vias.size() == 1 && stdPcb.traces.size() >= 10 &&
                  EdaViewer::IsLikelyEasyEdaJsonFile(edaDir + L"\\easyeda_std_pcb.json"),
                  L"Stage15.Eda.EasyEda.StandardPcbJson");

        tc.Record(okStdSch && stdSch.valid && stdSch.components.size() == 1 &&
                  stdSch.pads.size() == 3 && stdSch.vias.size() == 1 &&
                  HashEdaBoard(stdPcb) != HashEdaBoard(stdSch),
                  L"Stage15.Eda.EasyEda.StandardSchJson");

        tc.Record(okProPcb && proPcb.valid && proPcb.components.size() == 2 &&
                  proPcb.pads.size() == 7 && proPcb.vias.size() == 1 &&
                  okProSch && proSch.valid && proSch.components.size() == 1 && proSch.pads.size() == 3,
                  L"Stage15.Eda.EasyEda.ProEpcbAndEschJsonLines");

        tc.Record(okProEpro && proEpro.valid && proEpro.components.size() == 2 &&
                  proEpro.formatTitle.find(L"CH340N_USB_UART_Pro") != std::wstring::npos &&
                  okProEprj && proEprj.valid && !proEprj.components.empty(),
                  L"Stage15.Eda.EasyEda.ProEproZipAndEprjManifest");

        std::wstring pkgJsonPath = writeEdaText(L"package.json", "{ \"name\": \"my-web-app\", \"version\": \"1.0.0\", \"dependencies\": {} }\n");
        tc.Record(!EdaViewer::IsLikelyEasyEdaJsonFile(pkgJsonPath),
                  L"Stage15.Eda.EasyEda.DisambiguateGenericPackageJsonFalse");
        assertEdaRejected(L"Stage15.Eda.EasyEda.RejectGenericPackageJson", pkgJsonPath);
        assertEdaRejected(L"Stage15.Eda.EasyEda.RejectTruncatedEasyEdaJson",
                          writeEdaText(L"trunc_easyeda.json", "{ \"head\": { \"docType\": \"3\" }, \"shape\": [ \"TRACK~1~1~GND~0 0 10 10\" "));
        assertEdaRejected(L"Stage15.Eda.EasyEda.RejectInvalidEproZip",
                          writeEdaText(L"bad.epro", "PK\x03\x04NOT_A_VALID_EPRO_PROJECT"));
    }

    // ========================================================================
    // JTAG BSDL (.bsdl/.bsd), FPGA Constraints (.xdc/.sdc/.qsf) & IPC-D-356 (.d356/.ipc)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".bsdl") &&
                  EdaViewer::IsEdaExtension(L".bsd") &&
                  EdaViewer::IsEdaExtension(L".xdc") &&
                  EdaViewer::IsEdaExtension(L".sdc") &&
                  EdaViewer::IsEdaExtension(L".qsf") &&
                  EdaViewer::IsEdaExtension(L".d356") &&
                  EdaViewer::IsEdaExtension(L".ipc"),
                  L"Stage15.Eda.BsdlFpgaIpc.ExtensionsRegistered");

        ParsedEdaBoard bsdlBga, bsdQfp, xdcBrd, sdcBrd, sdcSlow, qsfBrd, d356Brd;
        bool okBsdl = EdaViewer::InspectEdaFile(edaDir + L"\\zynq7020_bga_jtag.bsdl", bsdlBga);
        bool okBsd  = EdaViewer::InspectEdaFile(edaDir + L"\\cortex_m4_lqfp16.bsd", bsdQfp);
        bool okXdc  = EdaViewer::InspectEdaFile(edaDir + L"\\artix7_lvds_pinout.xdc", xdcBrd);
        bool okSdc  = EdaViewer::InspectEdaFile(edaDir + L"\\asic_200mhz_timing.sdc", sdcBrd);
        bool okQsf  = EdaViewer::InspectEdaFile(edaDir + L"\\cyclone_v_pinout.qsf", qsfBrd);
        bool okD356 = EdaViewer::InspectEdaFile(edaDir + L"\\ipc_d356a_metric_test.d356", d356Brd);

        tc.Record(okBsdl && bsdlBga.valid && bsdlBga.pads.size() == 13 &&
                  bsdlBga.formatTitle.find(L"0x23727093") != std::wstring::npos &&
                  okBsd && bsdQfp.valid && bsdQfp.pads.size() == 16 &&
                  HashEdaBoard(bsdlBga) != HashEdaBoard(bsdQfp),
                  L"Stage15.Eda.BsdlFpgaIpc.BsdlBgaBallMapAndQfpPerimeter");

        bool okSdcSlow = EdaViewer::InspectEdaFile(
            writeEdaText(L"slow_10mhz.sdc", "create_clock -name slow_clk -period 100.0 [get_ports clk_i]\nset_input_delay -clock slow_clk 12.0 [get_ports din]\n"),
            sdcSlow);
        tc.Record(okXdc && xdcBrd.valid && xdcBrd.pads.size() == 6 &&
                  okQsf && qsfBrd.valid && qsfBrd.pads.size() == 4 &&
                  okSdc && sdcBrd.valid && okSdcSlow && sdcSlow.valid &&
                  HashEdaBoard(sdcBrd) != HashEdaBoard(sdcSlow),
                  L"Stage15.Eda.BsdlFpgaIpc.XdcQsfBalloutsAndDistinctSdcClocks");

        tc.Record(okD356 && d356Brd.valid && d356Brd.components.size() == 3 &&
                  d356Brd.pads.size() == 7 && d356Brd.vias.size() == 2 && d356Brd.netCount == 3,
                  L"Stage15.Eda.BsdlFpgaIpc.IpcD356aMetricPadsViasAndComponents");

        assertEdaRejected(L"Stage15.Eda.BsdlFpgaIpc.RejectFakePortWordBsdl",
                          writeEdaText(L"not_bsdl.bsdl", "This text mentions port and entity in prose without any IEEE 1149.1 syntax.\n"));
        assertEdaRejected(L"Stage15.Eda.BsdlFpgaIpc.RejectUnbalancedBracketXdc",
                          writeEdaText(L"bad.xdc", "set_property PACKAGE_PIN R4 [get_ports sys_clk\n"));
        assertEdaRejected(L"Stage15.Eda.BsdlFpgaIpc.RejectGarbageD356",
                          writeEdaText(L"bad.d356", "RANDOM_TEXT_WITHOUT_ANY_IPC_RECORDS\n"));
    }

    // ========================================================================
    // EDIF / BLIF / AIGER Gate Netlists, Liberty / SDF Timing & NI TDMS
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".edif") &&
                  EdaViewer::IsEdaExtension(L".edn") &&
                  EdaViewer::IsEdaExtension(L".blif") &&
                  EdaViewer::IsEdaExtension(L".eblif") &&
                  EdaViewer::IsEdaExtension(L".aig") &&
                  EdaViewer::IsEdaExtension(L".aag") &&
                  EdaViewer::IsEdaExtension(L".lib") &&
                  EdaViewer::IsEdaExtension(L".sdf") &&
                  EdaViewer::IsEdaExtension(L".tdms"),
                  L"Stage15.Eda.GateTdms.ExtensionsRegistered");

        ParsedEdaBoard edifBrd, blifBrd, aigBrd, libBrd, sdfBrd, tdmsBrd;
        bool okEdif = EdaViewer::InspectEdaFile(edaDir + L"\\vivado_spi_ctrl.edif", edifBrd);
        bool okBlif = EdaViewer::InspectEdaFile(edaDir + L"\\yosys_alu4.eblif", blifBrd);
        bool okAig  = EdaViewer::InspectEdaFile(edaDir + L"\\counter_latch_bin.aig", aigBrd);
        bool okLib  = EdaViewer::InspectEdaFile(edaDir + L"\\sky130_timing_nldm.lib", libBrd);
        bool okSdf  = EdaViewer::InspectEdaFile(edaDir + L"\\postroute_delays.sdf", sdfBrd);
        bool okTdms = EdaViewer::InspectEdaFile(waveformsDir + L"\\ni_daq_interleaved.tdms", tdmsBrd);

        tc.Record(okEdif && edifBrd.valid && edifBrd.components.size() == 4 && edifBrd.netCount == 7 &&
                  okBlif && blifBrd.valid && blifBrd.components.size() == 8 &&
                  HashEdaBoard(edifBrd) != HashEdaBoard(blifBrd),
                  L"Stage15.Eda.GateTdms.EdifAndYosysEblifGateDags");

        tc.Record(okAig && aigBrd.valid && aigBrd.components.size() == 3 && !aigBrd.circles.empty() &&
                  aigBrd.formatTitle.find(L"Binary (.aig)") != std::wstring::npos &&
                  HashEdaBoard(aigBrd) != HashEdaBoard(edifBrd),
                  L"Stage15.Eda.GateTdms.BinaryAigerDeltaAndLatchDag");

        tc.Record(okLib && libBrd.valid && libBrd.components.size() == 3 &&
                  EdaViewer::IsLikelyLibertyOrEdaSdfFile(edaDir + L"\\sky130_timing_nldm.lib", L".lib") &&
                  okSdf && sdfBrd.valid && sdfBrd.components.size() == 3 &&
                  EdaViewer::IsLikelyLibertyOrEdaSdfFile(edaDir + L"\\postroute_delays.sdf", L".sdf") &&
                  HashEdaBoard(libBrd) != HashEdaBoard(sdfBrd),
                  L"Stage15.Eda.GateTdms.LibertyNldmAndIeee1497Sdf");

        tc.Record(okTdms && tdmsBrd.valid && tdmsBrd.traces.size() > 100 &&
                  tdmsBrd.formatTitle.find(L"2 channels, 2 segments") != std::wstring::npos,
                  L"Stage15.Eda.GateTdms.NiTdmsMultiSegmentInterleavedWaveforms");

        std::wstring coffLib = writeEdaText(L"msvc_static.lib", "!<arch>\n/               0           0     0     0       4         `\n\x00\x00\x00\x00");
        std::wstring mdlSdf  = writeEdaText(L"benzene.sdf", "Benzene\n  OpenBabel\n\n  6  6  0  0  0  0  0  0  0  0999 V2000\n$$$$");
        tc.Record(!EdaViewer::IsLikelyLibertyOrEdaSdfFile(coffLib, L".lib") &&
                  !EdaViewer::IsLikelyLibertyOrEdaSdfFile(mdlSdf, L".sdf"),
                  L"Stage15.Eda.GateTdms.DisambiguateCoffLibAndMdlSdfFalse");
        assertEdaRejected(L"Stage15.Eda.GateTdms.RejectCoffArchiveLibInEda", coffLib);
        assertEdaRejected(L"Stage15.Eda.GateTdms.RejectMdlMoleculeSdfInEda", mdlSdf);
        assertEdaRejected(L"Stage15.Eda.GateTdms.RejectTruncatedBinAiger",
                          writeEdaText(L"trunc.aig", "aig 5 2 0 1 3\n10\n\x02"));
        assertEdaRejected(L"Stage15.Eda.GateTdms.RejectCorruptTdms",
                          writeEdaText(L"corrupt.tdms", "TDSm\x2E\x00\x00\x00\x69\x12\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00\x00\x00\x00"));

        // AIGER Fuzz-Hardening Regression Checks (negative / 64-bit header counts)
        assertEdaRejected(L"Stage15.Eda.GateTdms.AigerNegInputsNoWrap",
                          writeEdaText(L"neg_i.aig", "aig 4 -1 1 1 2\n0\n2\n"));
        assertEdaRejected(L"Stage15.Eda.GateTdms.AigerNegOutputsNoAlloc",
                          writeEdaText(L"neg_o.aig", "aig 4 1 1 -1 2\n0\n2\n"));
        assertEdaRejected(L"Stage15.Eda.GateTdms.AigerHugeOutputs64Bit",
                          writeEdaText(L"big_o.aig", "aig 4 1 1 99999999999 2\n0\n2\n"));
    }

    // ========================================================================
    // IPC-2581 Rev B/C (.cvg/.ipc2581), ODB++ (.tgz/.odb), Fritzing (.fzz/.fzpz) & gEDA (.pcb)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".cvg") &&
                  EdaViewer::IsEdaExtension(L".ipc2581") &&
                  EdaViewer::IsEdaExtension(L".odb") &&
                  EdaViewer::IsEdaExtension(L".tgz") &&
                  EdaViewer::IsEdaExtension(L".fzz") &&
                  EdaViewer::IsEdaExtension(L".fzpz") &&
                  EdaViewer::IsEdaExtension(L".fz") &&
                  EdaViewer::IsEdaExtension(L".fzp") &&
                  EdaViewer::IsEdaExtension(L".pcb"),
                  L"Stage15.Eda.MfgFritzingGeda.ExtensionsRegistered");

        ParsedEdaBoard cvgBrd, odbTgzBrd, fzzBrd, fzpzBrd, gedaBrd;
        bool okCvg  = EdaViewer::InspectEdaFile(edaDir + L"\\ipc2581_revb_board.cvg", cvgBrd);
        bool okOdb  = EdaViewer::InspectEdaFile(edaDir + L"\\odbpp_4layer_fab.tgz", odbTgzBrd);
        bool okFzz  = EdaViewer::InspectEdaFile(edaDir + L"\\fritzing_sensor_shield.fzz", fzzBrd);
        bool okFzpz = EdaViewer::InspectEdaFile(edaDir + L"\\fritzing_bme280_part.fzpz", fzpzBrd);
        bool okGeda = EdaViewer::InspectEdaFile(edaDir + L"\\geda_power_supply.pcb", gedaBrd);

        tc.Record(okCvg && cvgBrd.valid && cvgBrd.components.size() == 3 &&
                  cvgBrd.pads.size() == 6 && cvgBrd.vias.size() == 2 && cvgBrd.netCount == 4 &&
                  cvgBrd.formatTitle.find(L"Rev B") != std::wstring::npos,
                  L"Stage15.Eda.MfgFritzingGeda.Ipc2581RevBDigitalTwin");

        tc.Record(okOdb && odbTgzBrd.valid && odbTgzBrd.components.size() == 3 &&
                  odbTgzBrd.components[0].footprint == L"QFN16_3X3" &&
                  odbTgzBrd.pads.size() == 9 && odbTgzBrd.vias.size() == 3 && odbTgzBrd.netCount == 4 &&
                  EdaViewer::IsLikelyOdbPlusPlusTgz(edaDir + L"\\odbpp_4layer_fab.tgz") &&
                  HashEdaBoard(odbTgzBrd) != HashEdaBoard(cvgBrd),
                  L"Stage15.Eda.MfgFritzingGeda.OdbPlusPlusTgzArchiveWithGzipFeatures");

        tc.Record(okFzz && fzzBrd.valid && fzzBrd.components.size() == 3 &&
                  fzzBrd.vias.size() == 1 && fzzBrd.pads.size() >= 8 &&
                  okFzpz && fzpzBrd.valid && fzpzBrd.components.size() == 1 && fzpzBrd.pads.size() == 5 &&
                  HashEdaBoard(fzzBrd) != HashEdaBoard(fzpzBrd),
                  L"Stage15.Eda.MfgFritzingGeda.FritzingFzzSketchAndFzpzPart");

        tc.Record(okGeda && gedaBrd.valid && gedaBrd.components.size() == 3 &&
                  gedaBrd.pads.size() == 10 && gedaBrd.vias.size() == 2 && gedaBrd.netCount == 4 &&
                  gedaBrd.formatTitle.find(L"LM2596_Buck_5V") != std::wstring::npos &&
                  HashEdaBoard(gedaBrd) != HashEdaBoard(odbTgzBrd),
                  L"Stage15.Eda.MfgFritzingGeda.GedaPcbElementsArcsAndPolygons");

        std::wstring nonOdbTgz = writeEdaBytes(L"source_code.tgz", {
            0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        });
        tc.Record(!EdaViewer::IsLikelyOdbPlusPlusTgz(nonOdbTgz),
                  L"Stage15.Eda.MfgFritzingGeda.DisambiguateGenericTgzFalse");
        assertEdaRejected(L"Stage15.Eda.MfgFritzingGeda.RejectNonOdbTgzInEda", nonOdbTgz);
        assertEdaRejected(L"Stage15.Eda.MfgFritzingGeda.RejectTruncatedIpc2581",
                          writeEdaText(L"trunc.cvg", "<IPC-2581 revision=\"B\"><Ecad><CadData><Step><Component refDes=\"U1\">"));
        assertEdaRejected(L"Stage15.Eda.MfgFritzingGeda.RejectBinaryGarbagePcb",
                          writeEdaBytes(L"binary_pcad.pcb", { 'P','C','A','D','_','B','I','N', 0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00, 0x00, 0x42 }));
        assertEdaRejected(L"Stage15.Eda.MfgFritzingGeda.RejectInvalidFzzZip",
                          writeEdaText(L"bad.fzz", "PK\x03\x04CORRUPT_FRITZING_ZIP"));
    }

    // ========================================================================
    // Texas Instruments TI-TXT Firmware Image (.titxt)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".titxt"),
                  L"Stage15.EDA.TiTxt_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\msp430_bootloader.titxt", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 3 && b1.pads.size() == 4 &&
                  HasLabelSubstr(b1, L"ENTRY -> 0x8000") &&
                  b1.formatTitle.find(L"112 bytes") != std::wstring::npos && b1.netCount == 4,
                  L"Stage15.EDA.TiTxt_SampleSectionsAndResetVectorParsed");

        const char* tiMini = "@C000\n01 02 03 04 05 06 07 08\nq\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"mini.titxt", tiMini), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.TiTxt_DistinctFirmwareImage");

        assertEdaRejected(L"Stage15.EDA.TiTxt_RejectMissingQuit", writeEdaText(L"no_q.titxt", "@8000\n11 22 33 44\n"));
        assertEdaRejected(L"Stage15.EDA.TiTxt_RejectDataBeforeAddr", writeEdaText(L"no_at.titxt", "11 22 33 44\nq\n"));
        assertEdaRejected(L"Stage15.EDA.TiTxt_RejectNonHexByte", writeEdaText(L"bad_hex.titxt", "@8000\n11 ZZ 33\nq\n"));
    }

    // ========================================================================
    // Xilinx Vivado Coefficient File (.coe — FIR & Block Memory)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".coe"),
                  L"Stage15.EDA.XilinxCOE_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\vivado_fir_lut.coe", b1);
        tc.Record(ok1 && b1.valid && b1.circles.size() == 25 && b1.netCount == 25,
                  L"Stage15.EDA.XilinxCOE_FirFilterTapsParsed");

        const char* coeHex =
            "; Xilinx BRAM hex init\n"
            "memory_initialization_radix = 16;\n"
            "memory_initialization_vector = DEADBEEF, 01234567, 89ABCDEF, CAFEBABE;\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"bram16.coe", coeHex), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 &&
                  b2.formatTitle.find(L"4 words") != std::wstring::npos && b2.netCount == 1 &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.XilinxCOE_BlockMemoryRadix16AndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.XilinxCOE_RejectInvalidRadix",
                          writeEdaText(L"bad_rad.coe", "memory_initialization_radix = 7;\nmemory_initialization_vector = 1, 2;\n"));
        assertEdaRejected(L"Stage15.EDA.XilinxCOE_RejectBadDigitForRadix2",
                          writeEdaText(L"bad_bin.coe", "memory_initialization_radix = 2;\nmemory_initialization_vector = 0101, 1021;\n"));
    }

    // ========================================================================
    // Intel / Altera Quartus Memory Initialization File (.mif)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".mif"),
                  L"Stage15.EDA.QuartusMIF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\cyclone_sine_rom.mif", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 1 &&
                  b1.formatTitle.find(L"64 x") != std::wstring::npos && b1.netCount == 1,
                  L"Stage15.EDA.QuartusMIF_SampleSineRomAndRangeParsed");

        const char* mifBin =
            "DEPTH = 8; WIDTH = 4; ADDRESS_RADIX = DEC; DATA_RADIX = BIN;\n"
            "CONTENT BEGIN\n"
            "  0 : 1010 0101;\n"
            "  [2..7] : 1100;\n"
            "END;\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"tiny_bin.mif", mifBin), b2);
        tc.Record(ok2 && b2.valid && b2.formatTitle.find(L"8 x 4-bit") != std::wstring::npos &&
                  b2.netCount == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.QuartusMIF_BinMultiWordAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.QuartusMIF_RejectAddrPastDepth",
                          writeEdaText(L"ovf.mif", "DEPTH=4; WIDTH=8; ADDRESS_RADIX=DEC; DATA_RADIX=HEX; CONTENT BEGIN 9 : FF; END;"));
        assertEdaRejected(L"Stage15.EDA.QuartusMIF_RejectMissingEnd",
                          writeEdaText(L"no_end.mif", "DEPTH=4; WIDTH=8; CONTENT BEGIN 0 : AA;"));
    }

    // ========================================================================
    // Mentor Graphics PADS Layout ASCII (.pads, *PADS-PCB*)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".pads"),
                  L"Stage15.EDA.MentorPADS_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\mentor_rf_board.pads", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 4 && b1.pads.size() == 14 &&
                  b1.vias.size() == 1 && b1.netCount == 3,
                  L"Stage15.EDA.MentorPADS_DecalsPartsRoutesAndViaParsed");

        const char* padsMetric =
            "*PADS-PCB*\nMETRIC\n*PART*\nJ1 CONN2 10.0 8.0 0 U N\n*LINES*\nBRD BOARD 3 0.2\n0 0\n20 0\n20 16\n*END*\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"metric.pads", padsMetric), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.MentorPADS_MetricModeAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.MentorPADS_RejectMissingBanner",
                          writeEdaText(L"not_pads.pads", "RANDOM TEXT WITHOUT PADS HEADER\n1 2 3 4\n"));
        assertEdaRejected(L"Stage15.EDA.MentorPADS_RejectEmptyBannerOnly",
                          writeEdaText(L"empty.pads", "*PADS-PCB*\n*END*\n"));
    }

    // ========================================================================
    // Protel 99SE & Autotrax ASCII PCB (.protel)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".protel"),
                  L"Stage15.EDA.ProtelPCB_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\protel99_psu.protel", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 4 && b1.pads.size() == 6 &&
                  b1.vias.size() == 1 && b1.circles.size() == 1 && b1.netCount == 3,
                  L"Stage15.EDA.ProtelPCB_Sample99SePipeRecordsParsed");

        const char* autotrax =
            "PCB FILE 4\n"
            "COMP\nU1\nDIP8\nNE555\n500 400 0 1\n"
            "CP 450 350 60 60 1 32 1\nCP 550 350 60 60 1 32 2\n"
            "ENDCOMP\n"
            "FT 450 350 200 350 15 1\n"
            "FV 200 350 50 25\n"
            "ENDPCB\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"autotrax.protel", autotrax), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && b2.pads.size() == 2 &&
                  b2.vias.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.ProtelPCB_AutotraxDialectAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.ProtelPCB_RejectUnrelatedText",
                          writeEdaText(L"garbage.protel", "Just a plain text paragraph.\n"));
    }

    // ========================================================================
    // Zuken CADSTAR PCB Design Archive (.cpa)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".cpa"),
                  L"Stage15.EDA.CadstarCPA_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\zuken_motor_ctrl.cpa", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 4 && b1.vias.size() == 2 && b1.netCount == 3,
                  L"Stage15.EDA.CadstarCPA_SampleMotorBoardParsed");

        const char* cpaSimple =
            "(CADSTAR_PCB (HEADER (RESOLUTION MM 1)) "
            "(LAYOUT (BOARD (CLOSEDLINE (PT 0 0) (PT 25 0) (PT 25 18))) "
            "(COMP C1 R99 RES_1206 (PT 12 9))))\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"simple.cpa", cpaSimple), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.CadstarCPA_DistinctBoardHash");

        assertEdaRejected(L"Stage15.EDA.CadstarCPA_RejectUnclosedSExpr",
                          writeEdaText(L"unclosed.cpa", "(CADSTAR_PCB (LAYOUT (BOARD (PT 0 0) (PT 10 0))"));
        assertEdaRejected(L"Stage15.EDA.CadstarCPA_RejectNonCadstarSExpr",
                          writeEdaText(L"other.cpa", "(foo (bar 1 2 3))"));
    }

    // ========================================================================
    // Magic VLSI CMOS Physical IC Layout (.mag)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".mag"),
                  L"Stage15.EDA.MagicMAG_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\sky130_nand2_cell.mag", b1);
        tc.Record(ok1 && b1.valid && b1.vias.size() == 5 && b1.pads.size() == 5 && b1.netCount == 5,
                  L"Stage15.EDA.MagicMAG_Sky130Nand2RectsContactsAndPortsParsed");

        const char* magHier =
            "magic\ntech scmos\nuse inv_x1 u_inv0\ntransform 1 0 20 0 1 10\nbox 0 0 40 60\n<< met1 >>\nrect 0 0 15 8\n<< end >>\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"hier_inv.mag", magHier), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.MagicMAG_SubcellUseTransformAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.MagicMAG_RejectMissingEndFooter",
                          writeEdaText(L"no_end.mag", "magic\ntech sky130A\n<< met1 >>\nrect 0 0 10 10\n"));
        assertEdaRejected(L"Stage15.EDA.MagicMAG_RejectInvertedRectCoords",
                          writeEdaText(L"inv_rect.mag", "magic\n<< met1 >>\nrect 20 20 5 5\n<< end >>\n"));
    }

    // ========================================================================
    // UC Berkeley Espresso Two-Level PLA (.pla)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".pla"),
                  L"Stage15.EDA.EspressoPLA_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\alu_decoder.pla", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 10 && b1.traces.size() > 40,
                  L"Stage15.EDA.EspressoPLA_AluDecoderAndOrPlaneParsed");

        const char* plaXor = ".i 2\n.o 1\n01 1\n10 1\n.e\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"xor2.pla", plaXor), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 3 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.EspressoPLA_Xor2AndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.EspressoPLA_RejectWrongCubeWidth",
                          writeEdaText(L"bad_w.pla", ".i 3\n.o 2\n10 1\n.e\n"));
        assertEdaRejected(L"Stage15.EDA.EspressoPLA_RejectInvalidCubeChar",
                          writeEdaText(L"bad_ch.pla", ".i 2\n.o 1\n1X 1\n.e\n"));
    }

    // ========================================================================
    // ISCAS-85 / ISCAS-89 Gate-Level Benchmark Netlist (.bench)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".bench"),
                  L"Stage15.EDA.IscasBENCH_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\iscas85_c17_adder.bench", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 8 && b1.traces.size() > 50,
                  L"Stage15.EDA.IscasBENCH_C17AndDffSchematicParsed");

        const char* benchInv = "INPUT(A)\nOUTPUT(Z)\nZ = NOT(A)\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"inv.bench", benchInv), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.IscasBENCH_InverterAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.IscasBENCH_RejectUnknownGateType",
                          writeEdaText(L"bad_gate.bench", "INPUT(A)\nOUTPUT(Z)\nZ = FOOBAR(A)\n"));
        assertEdaRejected(L"Stage15.EDA.IscasBENCH_RejectNoGates",
                          writeEdaText(L"ports_only.bench", "INPUT(A)\nOUTPUT(B)\n"));
    }

    // ========================================================================
    // Kayak Automotive XML CAN Bus Database (.kcd)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".kcd"),
                  L"Stage15.EDA.KayakKCD_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\ev_powertrain_bus.kcd", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 3 && b1.vias.size() == 6 && b1.netCount == 5,
                  L"Stage15.EDA.KayakKCD_EcuBusAndBitfieldsParsed");

        const char* kcdChassis =
            "<NetworkDefinition><Node id=\"9\" name=\"ABS_ECU\"/>"
            "<Bus name=\"CHASSIS\"><Message id=\"0x080\" name=\"WheelSpeed\" length=\"8\">"
            "<Producer><NodeRef id=\"9\"/></Producer>"
            "<Signal name=\"FrontLeftKph\" offset=\"0\" length=\"16\"/>"
            "</Message></Bus></NetworkDefinition>";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"chassis.kcd", kcdChassis), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.KayakKCD_ChassisBusAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.KayakKCD_RejectNonKcdXml",
                          writeEdaText(L"other.kcd", "<root><item id=\"1\"/></root>"));
        assertEdaRejected(L"Stage15.EDA.KayakKCD_RejectEmptyNetwork",
                          writeEdaText(L"empty.kcd", "<NetworkDefinition></NetworkDefinition>"));
    }

    // ========================================================================
    // Linux SocketCAN candump Log (.candump)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".candump"),
                  L"Stage15.EDA.SocketCAN_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(waveformsDir + L"\\obd2_drive_cycle.candump", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 5 && b1.netCount == 5 && b1.traces.size() > 80,
                  L"Stage15.EDA.SocketCAN_CompactAndCanFdLogParsed");

        const char* humanLog =
            "  can0  100   [4]  DE AD BE EF\n"
            "  can0  200   [2]  12 34\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"human.candump", humanLog), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.SocketCAN_HumanReadableAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.SocketCAN_RejectProse",
                          writeEdaText(L"prose.candump", "Hello world this is not a CAN log\n"));
        assertEdaRejected(L"Stage15.EDA.SocketCAN_RejectOddHexNibble",
                          writeEdaText(L"odd_hex.candump", "(1.0) can0 123#ABC\n"));
        assertEdaRejected(L"Stage15.EDA.SocketCAN_RejectOversizeClassicPayload",
                          writeEdaText(L"too_long.candump", "(1.0) can0 123#001122334455667788\n"));
    }

    // ========================================================================
    // Agilent / Keysight RF VNA CITIFILE (.cti)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".cti"),
                  L"Stage15.EDA.CitifileCTI_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(waveformsDir + L"\\keysight_bandpass.cti", b1);
        tc.Record(ok1 && b1.valid && b1.netCount == 4 && b1.traces.size() > 150,
                  L"Stage15.EDA.CitifileCTI_BandpassSParamsParsed");

        const char* ctiAnt =
            "CITIFILE A.01.00\n"
            "NAME ANT_S11\n"
            "VAR FREQ MAG 4\n"
            "DATA S[1,1] DBANGLE\n"
            "SEG_LIST_BEGIN\n"
            "SEG 1.0E9 2.5E9 4\n"
            "SEG_LIST_END\n"
            "BEGIN\n"
            "-3.2, 45.0\n"
            "-18.5, 2.0\n"
            "-9.4, -38.0\n"
            "-2.8, -72.0\n"
            "END\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"ant_s11.cti", ctiAnt), b2);
        tc.Record(ok2 && b2.valid && b2.netCount == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.CitifileCTI_SegListDbAngleAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.CitifileCTI_RejectCountMismatch",
                          writeEdaText(L"mismatch.cti",
                              "CITIFILE A.01.00\nVAR FREQ MAG 5\nDATA S[1,1] RI\nBEGIN\n0.1,0.2\n0.3,0.4\nEND\n"));
        assertEdaRejected(L"Stage15.EDA.CitifileCTI_RejectUnclosedBegin",
                          writeEdaText(L"unclosed.cti",
                              "CITIFILE A.01.00\nVAR FREQ MAG 2\nDATA S[1,1] RI\nBEGIN\n0.1,0.2\n0.3,0.4\n"));
    }

    // ========================================================================
    // IEEE Std C37.111 COMTRADE Single-File (.cff)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".cff"),
                  L"Stage15.EDA.ComtradeCFF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(waveformsDir + L"\\substation_fault_110kv.cff", b1);
        tc.Record(ok1 && b1.valid && b1.netCount == 6 && b1.traces.size() > 200,
                  L"Stage15.EDA.ComtradeCFF_AsciiFaultOscillogramParsed");

        std::string cffBinHdr =
            "--- file type: CFG ---\n"
            "FEEDER_15KV,SEL_751,2013\n"
            "2,1A,1D\n"
            "1,I_PH_A,A,,A,0.25,0.0,0.0,-32767,32767,500,5,P\n"
            "1,OC_50P_TRIP,,,0\n"
            "60.0\n"
            "1\n"
            "960.0,4\n"
            "01/01/2026,00:00:00.000000\n"
            "01/01/2026,00:00:00.002000\n"
            "BINARY\n"
            "1.0\n"
            "--- file type: DAT BINARY: 48 ---\n";
        std::vector<uint8_t> cffBin(cffBinHdr.begin(), cffBinHdr.end());
        const int16_t iaVals[4] = { 400, 2400, -2200, 0 };
        const uint16_t digVals[4] = { 0, 1, 1, 1 };
        for (uint32_t r = 0; r < 4; ++r) {
            AppendU32LE(cffBin, r + 1u);
            AppendU32LE(cffBin, r * 1042u);
            AppendU16LE(cffBin, static_cast<uint16_t>(iaVals[r]));
            AppendU16LE(cffBin, digVals[r]);
        }
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaBytes(L"feeder_bin.cff", cffBin), b2);
        tc.Record(ok2 && b2.valid && b2.netCount == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.ComtradeCFF_BinarySectionAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.ComtradeCFF_RejectChannelSumMismatch",
                          writeEdaText(L"bad_tt.cff",
                              "--- file type: CFG ---\nST,DEV,2013\n5,1A,1D\n1,V,,,V,1,0,0,-10,10\n1,D,,,0\n50\n1\n100,2\n0\n0\nASCII\n"
                              "--- file type: DAT ASCII ---\n1,0,10,0\n2,100,20,1\n"));
        assertEdaRejected(L"Stage15.EDA.ComtradeCFF_RejectMissingDatSection",
                          writeEdaText(L"no_dat.cff", "--- file type: CFG ---\nST,DEV,2013\n1,1A,0D\n"));
    }

    // ========================================================================
    // GTKWave LXT2 Binary Waveform (.lxt2)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".lxt2"),
                  L"Stage15.EDA.GtkWaveLXT2_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(waveformsDir + L"\\riscv_pipeline.lxt2", b1);
        tc.Record(ok1 && b1.valid && b1.netCount == 6 && b1.traces.size() > 40,
                  L"Stage15.EDA.GtkWaveLXT2_GzipTraceAndAliasParsed");

        std::vector<uint8_t> fnRaw = { 0,0,'s','i','g','A',0, 0,3,'B',0 };
        std::vector<uint8_t> gmRaw;
        for (int k = 0; k < 2; ++k) {
            AppendU32BE(gmRaw, 0); AppendU32BE(gmRaw, 0); AppendU32BE(gmRaw, 0); AppendU32BE(gmRaw, 0);
        }
        std::vector<uint8_t> blkMem;
        blkMem.push_back(0);
        blkMem.push_back(3);
        AppendU64BE(blkMem, 0); AppendU64BE(blkMem, 25); AppendU64BE(blkMem, 50);
        blkMem.push_back(1);
        blkMem.push_back(1); blkMem.push_back(1);
        blkMem.push_back(1);
        blkMem.push_back(0); blkMem.push_back(3);
        blkMem.insert(blkMem.end(), { 0, 1, 0,  1, 0, 1 });
        AppendU64BE(blkMem, 0x07ULL);
        AppendU32BE(blkMem, 0);
        AppendU32BE(blkMem, 0);
        AppendU32BE(blkMem, 1);

        std::vector<uint8_t> lxt2Raw;
        AppendU16BE(lxt2Raw, 0x1380u);
        AppendU16BE(lxt2Raw, 0x0001u);
        lxt2Raw.push_back(64);
        AppendU32BE(lxt2Raw, 2);
        AppendU32BE(lxt2Raw, static_cast<uint32_t>(fnRaw.size()));
        AppendU32BE(lxt2Raw, 8);
        AppendU32BE(lxt2Raw, static_cast<uint32_t>(fnRaw.size()));
        AppendU32BE(lxt2Raw, 0);
        AppendU32BE(lxt2Raw, static_cast<uint32_t>(gmRaw.size()));
        lxt2Raw.push_back(static_cast<uint8_t>(static_cast<int8_t>(-12)));
        lxt2Raw.insert(lxt2Raw.end(), fnRaw.begin(), fnRaw.end());
        lxt2Raw.insert(lxt2Raw.end(), gmRaw.begin(), gmRaw.end());
        AppendU64BE(lxt2Raw, blkMem.size());
        AppendU64BE(lxt2Raw, blkMem.size());
        AppendU64BE(lxt2Raw, 0);
        AppendU64BE(lxt2Raw, 50);
        lxt2Raw.insert(lxt2Raw.end(), blkMem.begin(), blkMem.end());

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaBytes(L"raw64.lxt2", lxt2Raw), b2);
        tc.Record(ok2 && b2.valid && b2.netCount == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.GtkWaveLXT2_Uncompressed64GranuleAndDistinctHash");

        std::vector<uint8_t> badSpan = lxt2Raw;
        const size_t msbOff = 30u + fnRaw.size() + 4u;
        badSpan[msbOff + 0] = 0x7F; badSpan[msbOff + 1] = 0xFF; badSpan[msbOff + 2] = 0xFF; badSpan[msbOff + 3] = 0xFF;
        assertEdaRejected(L"Stage15.EDA.GtkWaveLXT2_RejectHugeBitSpanCve38583",
                          writeEdaBytes(L"cve_span.lxt2", badSpan));

        std::vector<uint8_t> badHdr = { 0x13, 0x80, 0x00, 0x01, 64, 0,0,0,5 };
        assertEdaRejected(L"Stage15.EDA.GtkWaveLXT2_RejectTruncatedHeader",
                          writeEdaBytes(L"trunc.lxt2", badHdr));
    }

    // ========================================================================
    // IEEE 1481 Standard Parasitic Exchange Format (.spef)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".spef"),
                  L"Stage15.EDA.IeeeSPEF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\asic_clock_tree.spef", b1);
        tc.Record(ok1 && b1.valid && b1.netCount == 2 && b1.components.size() == 6 && b1.traces.size() > 40,
                  L"Stage15.EDA.IeeeSPEF_ClockTreeAndCouplingCapParsed");

        const char* spefInv =
            "*SPEF \"IEEE 1481-1998\"\n"
            "*DESIGN \"inv_chain\"\n"
            "*C_UNIT 1 PF\n"
            "*R_UNIT 1 KOHM\n"
            "*D_NET n_mid 0.08\n"
            "*CONN\n"
            "*I u0:Y O *D INV_X1\n"
            "*I u1:A I *D INV_X2\n"
            "*CAP\n"
            "1 u0:Y 0.04\n"
            "2 u1:A 0.04\n"
            "*RES\n"
            "1 u0:Y u1:A 0.12\n"
            "*END\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"inv.spef", spefInv), b2);
        tc.Record(ok2 && b2.valid && b2.netCount == 1 && b2.components.size() == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.IeeeSPEF_DirectNetAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.IeeeSPEF_RejectUnclosedDNet",
                          writeEdaText(L"unclosed.spef", "*SPEF \"1481\"\n*D_NET n1 1.0\n*CAP\n1 n1 1.0\n"));
        assertEdaRejected(L"Stage15.EDA.IeeeSPEF_RejectMissingSpefHeader",
                          writeEdaText(L"no_hdr.spef", "*DESIGN \"foo\"\n*D_NET n1 1.0\n*END\n"));
    }

    // ========================================================================
    // IEEE 1801 Unified Power Format (.upf)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".upf"),
                  L"Stage15.EDA.IeeeUPF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\soc_power_islands.upf", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 5 && b1.traces.size() > 60,
                  L"Stage15.EDA.IeeeUPF_DomainsSwitchesAndPstParsed");

        const char* upfIo =
            "create_supply_set SS_AON -function {power VDD_IO} -function {ground VSS}\n"
            "create_power_domain PD_IO -include_scope -supply {primary SS_AON}\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"io_only.upf", upfIo), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.IeeeUPF_SupplySetAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.IeeeUPF_RejectGenericTclWithoutPowerDomain",
                          writeEdaText(L"plain_tcl.upf", "set a 10\nputs \"hello world\"\n"));
        assertEdaRejected(L"Stage15.EDA.IeeeUPF_RejectUnbalancedBraces",
                          writeEdaText(L"bad_brace.upf", "create_supply_net VDD\ncreate_power_domain PD -elements {u1 u2\n"));
    }

    // ========================================================================
    // QSPICE Circuit Schematic (.qsch — 0xFF 0xD8 0xFF 0xDB + Guillemet AST)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".qsch"),
                  L"Stage15.Eda.QspiceQsch.ExtensionRegistered");

        ParsedEdaBoard buckQsch, utf8Qsch;
        bool okBuck = EdaViewer::InspectEdaFile(edaDir + L"\\buck_converter.qsch", buckQsch);
        tc.Record(okBuck && buckQsch.valid &&
                  buckQsch.components.size() == 6 && buckQsch.pads.size() == 13 &&
                  !buckQsch.circles.empty() && buckQsch.netCount == 4 &&
                  buckQsch.formatTitle.find(L"6 components, 11 wires") != std::wstring::npos &&
                  HasLabelSubstr(buckQsch, L"M1 (BSC050N04)") &&
                  HasLabelSubstr(buckQsch, L"VOUT"),
                  L"Stage15.Eda.QspiceQsch.BuckConverterSymbolsCoilsAndWires");

        // UTF-8 guillemet (0xC2 0xAB / 0xC2 0xBB) variant with rotated/mirrored component + arc primitive
        const std::string qschUtf8Body =
            "\xC2\xAB" "schematic (0,0)\n"
            "  \xC2\xAB" "component (500,600) 1 1\n"
            "    \xC2\xAB" "symbol U\n"
            "      \xC2\xAB" "arc (-100,-100) (100,100) (100,0) (-100,0)" "\xC2\xBB\n"
            "      \xC2\xAB" "pin (-150,0) (0,0) \"IN\"" "\xC2\xBB\n"
            "      \xC2\xAB" "pin (150,0) (0,0) \"OUT\"" "\xC2\xBB\n"
            "    \xC2\xBB\n"
            "    \xC2\xAB" "property \"InstName\" \"U1\"" "\xC2\xBB\n"
            "    \xC2\xAB" "property \"Value\" \"OPA1612\"" "\xC2\xBB\n"
            "  \xC2\xBB\n"
            "  \xC2\xAB" "wire (500,450) (500,200) \"AUDIO_OUT\"" "\xC2\xBB\n"
            "\xC2\xBB\n";
        std::vector<uint8_t> qschUtf8 = { 0xFFu, 0xD8u, 0xFFu, 0xDBu };
        qschUtf8.insert(qschUtf8.end(), qschUtf8Body.begin(), qschUtf8Body.end());
        bool okUtf8 = EdaViewer::InspectEdaFile(writeEdaBytes(L"opamp_utf8.qsch", qschUtf8), utf8Qsch);
        tc.Record(okUtf8 && utf8Qsch.valid && utf8Qsch.components.size() == 1 &&
                  utf8Qsch.pads.size() == 2 && HasLabelSubstr(utf8Qsch, L"U1 (OPA1612)") &&
                  HashEdaBoard(buckQsch) != HashEdaBoard(utf8Qsch),
                  L"Stage15.Eda.QspiceQsch.Utf8GuillemetArcTransformAndDistinctHash");

        // Disambiguate binary JPEG starting with 0xFF 0xD8 0xFF 0xDB (SOI + DQT)
        std::vector<uint8_t> fakeJpeg = {
            0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08,
            0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07,
            0xFF, 0xD9
        };
        assertEdaRejected(L"Stage15.Eda.QspiceQsch.RejectBinaryJpegWithSame4ByteMagic",
                          writeEdaBytes(L"photo_renamed.qsch", fakeJpeg));
        assertEdaRejected(L"Stage15.Eda.QspiceQsch.RejectMissingMagicHeader",
                          writeEdaText(L"no_magic.qsch", "\xAB" "schematic (0,0) " "\xAB" "wire (0,0) (100,0)" "\xBB\xBB\n"));
        std::vector<uint8_t> unclosedQsch = { 0xFF, 0xD8, 0xFF, 0xDB, 0xAB, 's','c','h','e','m','a','t','i','c', ' ', '(', '0', ',', '0', ')' };
        assertEdaRejected(L"Stage15.Eda.QspiceQsch.RejectUnclosedGuillemetBlock",
                          writeEdaBytes(L"unclosed.qsch", unclosedQsch));
    }

    // ========================================================================
    // Autodesk / CadSoft EAGLE PCB Design Rules (.dru — HDI Stackup & DRC Coupon)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".dru"),
                  L"Stage15.Eda.EagleDru.ExtensionRegistered");

        ParsedEdaBoard hdiDru, std2LDru;
        bool okHdi = EdaViewer::InspectEdaFile(edaDir + L"\\multilayer_6l_hd.dru", hdiDru);
        tc.Record(okHdi && hdiDru.valid && hdiDru.components.size() == 1 &&
                  hdiDru.components[0].pinCount == 6 &&
                  hdiDru.vias.size() == 10 && hdiDru.pads.size() == 2 &&
                  hdiDru.netCount == 11 &&
                  hdiDru.formatTitle.find(L"6-layer") != std::wstring::npos &&
                  HasLabelSubstr(hdiDru, L"Prepreg") && HasLabelSubstr(hdiDru, L"FR4 Core"),
                  L"Stage15.Eda.EagleDru.SixLayerHdiBlindBuriedStackupAndCoupon");

        const char* dru2Layer =
            "description[en] = Standard 2-Layer FR4\n"
            "layerSetup = (1*16)\n"
            "mtCopper = 0.035mm 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0.035mm\n"
            "mtIsolate = 1.5mm\n"
            "mdWireWire = 6mil\n"
            "msWidth = 6mil\n"
            "msDrill = 0.3mm\n";
        bool ok2L = EdaViewer::InspectEdaFile(writeEdaText(L"fr4_2layer.dru", dru2Layer), std2LDru);
        tc.Record(ok2L && std2LDru.valid && std2LDru.components.size() == 1 &&
                  std2LDru.components[0].pinCount == 2 && std2LDru.vias.size() == 2 &&
                  HashEdaBoard(hdiDru) != HashEdaBoard(std2LDru),
                  L"Stage15.Eda.EagleDru.TwoLayerCoreAndDistinctHash");

        assertEdaRejected(L"Stage15.Eda.EagleDru.RejectGenericIniFile",
                          writeEdaText(L"config.dru", "host = localhost\nport = 8080\ntimeout = 30\n"));
        assertEdaRejected(L"Stage15.Eda.EagleDru.RejectMissingStackupKeys",
                          writeEdaText(L"no_stack.dru", "mdWireWire = 6mil\nmsWidth = 6mil\nmsDrill = 0.3mm\n"));
    }

    // ========================================================================
    // Microwind CMOS IC Physical Mask Layout (.msk — Lambda Boxes & MOSFET LVS)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".msk"),
                  L"Stage15.Eda.MicrowindMsk.ExtensionRegistered");

        ParsedEdaBoard nandMsk, invMsk;
        bool okNand = EdaViewer::InspectEdaFile(edaDir + L"\\cmos_nand2_cell.msk", nandMsk);
        tc.Record(okNand && nandMsk.valid &&
                  nandMsk.components.size() >= 4 &&
                  nandMsk.pads.size() == 5 && nandMsk.vias.size() == 13 &&
                  nandMsk.netCount == 5 &&
                  nandMsk.formatTitle.find(L"CMOS_NAND2_65NM_STDCELL") != std::wstring::npos &&
                  HasLabelSubstr(nandMsk, L"VDD") && HasLabelSubstr(nandMsk, L"Y"),
                  L"Stage15.Eda.MicrowindMsk.CmosNand2MasksContactsAndMosfetLvs");

        const char* invMskTxt =
            "VERSION 3.5\nTITLE CMOS_INV_CELL\nLAMBDA 0.05\nBB 0 0 20 30\n"
            "C NTUB 0 15 20 15\n"
            "C DP 4 18 12 6\n"
            "C DN 4 4 12 6\n"
            "C POLY 8 2 4 26\n"
            "P ME1 0 0 20 0 20 3 0 3\n"
            "C CO 5 5 2 2\n"
            "T ME1 10 15 OUT\n"
            "END\n";
        bool okInv = EdaViewer::InspectEdaFile(writeEdaText(L"cmos_inv.msk", invMskTxt), invMsk);
        tc.Record(okInv && invMsk.valid && invMsk.components.size() == 2 &&
                  invMsk.pads.size() == 1 && invMsk.vias.size() == 1 &&
                  HashEdaBoard(nandMsk) != HashEdaBoard(invMsk),
                  L"Stage15.Eda.MicrowindMsk.PolygonInverterAndDistinctHash");

        assertEdaRejected(L"Stage15.Eda.MicrowindMsk.RejectUnknownMaskLayer",
                          writeEdaText(L"bad_layer.msk", "VERSION 3.5\nC UNKNOWN_MASK 0 0 10 10\nEND\n"));
        assertEdaRejected(L"Stage15.Eda.MicrowindMsk.RejectTruncatedBoxCoords",
                          writeEdaText(L"trunc_box.msk", "VERSION 3.5\nC ME1 0 0 10\nEND\n"));
        assertEdaRejected(L"Stage15.Eda.MicrowindMsk.RejectBinaryMaskBitmap",
                          writeEdaBytes(L"bitmap.msk", { 0x4D, 0x53, 0x4B, 0x00, 0x10, 0x00, 0xFF, 0xAA, 0x55, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 }));
    }

    // ========================================================================
    // Berkeley SIS / ABC Boolean Equation Netlist (.eqn — Recursive-Descent DAG)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".eqn"),
                  L"Stage15.Eda.BerkeleyEqn.ExtensionRegistered");

        ParsedEdaBoard aluEqn, xorEqn;
        bool okAlu = EdaViewer::InspectEdaFile(edaDir + L"\\alu_slice_4bit.eqn", aluEqn);
        tc.Record(okAlu && aluEqn.valid &&
                  aluEqn.components.size() >= 13 && !aluEqn.traces.empty() &&
                  aluEqn.formatTitle.find(L"alu_slice_4bit") != std::wstring::npos &&
                  aluEqn.formatTitle.find(L"7 inputs, 4 outputs, 15 gates") != std::wstring::npos,
                  L"Stage15.Eda.BerkeleyEqn.AluSliceCarryLookaheadAndParity");

        // Postfix complement '\'' and implicit adjacency AND ("a b' + a' b")
        const char* implicitXorEqn =
            "NAME = xor_implicit;\n"
            "INORDER = a b;\n"
            "OUTORDER = z;\n"
            "z = a b' + a' b;\n";
        bool okXor = EdaViewer::InspectEdaFile(writeEdaText(L"xor_adj.eqn", implicitXorEqn), xorEqn);
        tc.Record(okXor && xorEqn.valid && xorEqn.components.size() >= 3 &&
                  HashEdaBoard(aluEqn) != HashEdaBoard(xorEqn),
                  L"Stage15.Eda.BerkeleyEqn.ImplicitAdjacencyPostfixNotAndDistinctHash");

        assertEdaRejected(L"Stage15.Eda.BerkeleyEqn.RejectMissingSemicolon",
                          writeEdaText(L"no_semi.eqn", "INORDER = a b;\nOUTORDER = y;\ny = a & b\n"));
        assertEdaRejected(L"Stage15.Eda.BerkeleyEqn.RejectUnbalancedParen",
                          writeEdaText(L"bad_paren.eqn", "y = (a + b * (c ^ d);\n"));
        assertEdaRejected(L"Stage15.Eda.BerkeleyEqn.RejectIllegalExprToken",
                          writeEdaText(L"bad_tok.eqn", "y = a @ b;\n"));
        assertEdaRejected(L"Stage15.Eda.BerkeleyEqn.RejectHeaderWithoutEquations",
                          writeEdaText(L"empty_eqn.eqn", "NAME = none;\nINORDER = a b;\nOUTORDER = y;\n"));
    }

    // ========================================================================
    // Altera / Intel Jam STAPL Byte-Code (.jbc — CCITT-16 CRC & VM Disassembler)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".jbc"),
                  L"Stage15.Eda.AlteraJbc.ExtensionRegistered");

        ParsedEdaBoard max10Jbc, v1Jbc;
        bool okMax10 = EdaViewer::InspectEdaFile(edaDir + L"\\max10_isp_prog.jbc", max10Jbc);
        tc.Record(okMax10 && max10Jbc.valid &&
                  max10Jbc.components.size() == 7 &&
                  max10Jbc.traces.size() > 80 &&
                  max10Jbc.formatTitle.find(L"10M08SAE144C8G") != std::wstring::npos &&
                  max10Jbc.formatTitle.find(L"2 actions, 5 procs, 4 SIR/4 SDR") != std::wstring::npos &&
                  HasLabelSubstr(max10Jbc, L"ACTION PROGRAM") &&
                  HasLabelSubstr(max10Jbc, L"TCK") && HasLabelSubstr(max10Jbc, L"TDO"),
                  L"Stage15.Eda.AlteraJbc.Max10BytecodeDagAndDisassembledJtagScans");

        // Build a compact JBC v1 ("JBC\0", 48-byte direct table directory) with 1 action + 1 proc + valid CCITT-16
        std::string strTbl("READ_ID\0\0DO_ID\0", 15);
        std::vector<uint8_t> actTbl(12, 0u);
        PutU32BE(actTbl.data() + 0, 0u); // "READ_ID"
        PutU32BE(actTbl.data() + 4, 8u); // ""
        PutU32BE(actTbl.data() + 8, 0u); // firstProc = 0
        std::vector<uint8_t> prcTbl(13, 0u);
        PutU32BE(prcTbl.data() + 0, 9u);          // "DO_ID"
        PutU32BE(prcTbl.data() + 4, 0xFFFFFFFFu); // nextProc = none
        prcTbl[8] = 1u;
        PutU32BE(prcTbl.data() + 9, 0u);          // codeRelOff = 0
        // VM bytecode: PSHL 10, PSHL 0x006, ISS (0x17), PSHL 32, PSHL 0x031820DD, DSS (0x15), RET (0x11)
        std::vector<uint8_t> codeSec = {
            0x40, 0,0,0,10,  0x40, 0,0,0,6,  0x17,
            0x40, 0,0,0,32,  0x40, 0x03,0x18,0x20,0xDD,  0x15,
            0x11
        };
        const uint32_t offAct = 44u;
        const uint32_t offPrc = offAct + static_cast<uint32_t>(actTbl.size());
        const uint32_t offStr = offPrc + static_cast<uint32_t>(prcTbl.size());
        const uint32_t offCod = offStr + static_cast<uint32_t>(strTbl.size());
        std::vector<uint8_t> jbcV1(44u, 0u);
        jbcV1[0] = 'J'; jbcV1[1] = 'B'; jbcV1[2] = 'C'; jbcV1[3] = 0u;
        PutU32BE(jbcV1.data() + 0x04, offAct);
        PutU32BE(jbcV1.data() + 0x08, offPrc);
        PutU32BE(jbcV1.data() + 0x0C, offStr);
        PutU32BE(jbcV1.data() + 0x10, offCod);
        PutU32BE(jbcV1.data() + 0x14, offCod);
        PutU32BE(jbcV1.data() + 0x18, offCod);
        PutU32BE(jbcV1.data() + 0x1C, 0u);
        PutU32BE(jbcV1.data() + 0x20, 1u); // actionCount = 1
        PutU32BE(jbcV1.data() + 0x24, 1u); // procCount = 1
        jbcV1.insert(jbcV1.end(), actTbl.begin(), actTbl.end());
        jbcV1.insert(jbcV1.end(), prcTbl.begin(), prcTbl.end());
        jbcV1.insert(jbcV1.end(), strTbl.begin(), strTbl.end());
        jbcV1.insert(jbcV1.end(), codeSec.begin(), codeSec.end());
        const uint16_t crcV1 = ComputeJbiCrcReflected(jbcV1.data(), jbcV1.size());
        AppendU16BE(jbcV1, crcV1);

        bool okV1 = EdaViewer::InspectEdaFile(writeEdaBytes(L"read_id_v1.jbc", jbcV1), v1Jbc);
        tc.Record(okV1 && v1Jbc.valid && v1Jbc.components.size() == 2 &&
                  HashEdaBoard(max10Jbc) != HashEdaBoard(v1Jbc),
                  L"Stage15.Eda.AlteraJbc.V1DirectDirectoryAndDistinctHash");

        // Corrupt trailing 16-bit CCITT CRC must be rejected!
        std::vector<uint8_t> badCrcJbc = jbcV1;
        badCrcJbc.back() ^= 0xFFu;
        assertEdaRejected(L"Stage15.Eda.AlteraJbc.RejectCorruptedCcitt16Crc",
                          writeEdaBytes(L"bad_crc.jbc", badCrcJbc));
        assertEdaRejected(L"Stage15.Eda.AlteraJbc.RejectAsciiJamInJbc",
                          writeEdaText(L"ascii_stapl.jbc", "NOTE \"DEVICE\" \"MAX10\";\nACTION PROGRAM = DO_PROG;\nCRC 1234;\n"));
    }

    // ========================================================================
    // UCLA / ISPD GSRC Bookshelf VLSI Placement Suite (.scl / .nodes / .pl / .nets)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".scl") && EdaViewer::IsEdaExtension(L".nodes"),
                  L"Stage15.Eda.IspdBookshelf.ExtensionsRegistered");

        ParsedEdaBoard sclSuite, nodesSuite, sclStandalone, nodesUnplaced;
        bool okScl   = EdaViewer::InspectEdaFile(edaDir + L"\\ispd_riscv_alu.scl", sclSuite);
        bool okNodes = EdaViewer::InspectEdaFile(edaDir + L"\\ispd_riscv_alu.nodes", nodesSuite);

        // Both .scl and .nodes resolve the full 4-file companion suite: 6 rows, 22 cells, 6 IO pads, 12 Steiner nets, HPWL > 0
        tc.Record(okScl && sclSuite.valid && okNodes && nodesSuite.valid &&
                  sclSuite.components.size() == 22 && sclSuite.pads.size() == 6 &&
                  sclSuite.netCount == 12 && sclSuite.traces.size() >= 60 &&
                  sclSuite.formatTitle.find(L"6 rows, 22 cells, 12 nets, HPWL=") != std::wstring::npos &&
                  nodesSuite.components.size() == 22 && nodesSuite.netCount == 12 &&
                  HasLabelSubstr(sclSuite, L"u_sram_rf") && HasLabelSubstr(sclSuite, L"u_mul_mac"),
                  L"Stage15.Eda.IspdBookshelf.FullCompanionSuiteFloorplanMacrosAndHpwl");

        // Standalone .scl (rows only) & standalone unplaced .nodes (auto-legalized) -> distinct hashes
        const char* soloScl =
            "UCLA scl 1.0\nNumRows : 2\n"
            "CoreRow Horizontal\n Coordinate : 0\n Height : 10\n Sitewidth : 1\n Sitespacing : 1\n SubrowOrigin : 0 NumSites : 64\nEnd\n"
            "CoreRow Horizontal\n Coordinate : 10\n Height : 10\n Sitewidth : 1\n Sitespacing : 1\n SubrowOrigin : 0 NumSites : 64\nEnd\n";
        const char* soloNodes =
            "UCLA nodes 1.0\nNumNodes : 3\nNumTerminals : 1\n"
            "io_clk 4 4 terminal\n"
            "u_inv0 8 10\n"
            "u_nand 12 10\n";
        bool okSoloScl   = EdaViewer::InspectEdaFile(writeEdaText(L"solo_floorplan.scl", soloScl), sclStandalone);
        bool okSoloNodes = EdaViewer::InspectEdaFile(writeEdaText(L"unplaced_cells.nodes", soloNodes), nodesUnplaced);
        tc.Record(okSoloScl && sclStandalone.valid &&
                  okSoloNodes && nodesUnplaced.valid && nodesUnplaced.components.size() == 3 &&
                  HashEdaBoard(sclSuite) != HashEdaBoard(sclStandalone) &&
                  HashEdaBoard(sclStandalone) != HashEdaBoard(nodesUnplaced),
                  L"Stage15.Eda.IspdBookshelf.StandaloneSclAndAutoLegalizedNodes");

        assertEdaRejected(L"Stage15.Eda.IspdBookshelf.RejectMissingUclaSclHeader",
                          writeEdaText(L"no_hdr.scl", "NumRows : 1\nCoreRow Horizontal\n Coordinate : 0\n Height : 8\n NumSites : 20\nEnd\n"));
        assertEdaRejected(L"Stage15.Eda.IspdBookshelf.RejectZeroRowsScl",
                          writeEdaText(L"zero_rows.scl", "UCLA scl 1.0\nNumRows : 0\n"));
        assertEdaRejected(L"Stage15.Eda.IspdBookshelf.RejectMalformedNodesEntry",
                          writeEdaText(L"bad.nodes", "UCLA nodes 1.0\nu_cell0 NOT_NUMERIC 10\n"));
    }

    for (const auto& f : createdEdaFiles) DeleteFileW(f.c_str());
    RemoveDirectoryW(edaWorkDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
