// FormatTestsEdaWaveformsBus.cpp: Stage 15 Oscilloscope, Logic Analyzer, RF, Fieldbus & FPGA Constraint self-tests
#include "Crc32.h"
#include "SelfTestCommon.h"
#include "TestBytes.h"
#include "EdaViewer.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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

static std::vector<uint8_t> WrapGzipStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> gz = { 0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0x03 };
    size_t pos = 0;
    do {
        const size_t take = std::min<size_t>(raw.size() - pos, 65535u);
        const bool last = (pos + take >= raw.size());
        gz.push_back(last ? 0x01u : 0x00u);
        const uint16_t len = static_cast<uint16_t>(take);
        const uint16_t nlen = static_cast<uint16_t>(~len);
        AppendU16LE(gz, len);
        AppendU16LE(gz, nlen);
        gz.insert(gz.end(), raw.begin() + static_cast<ptrdiff_t>(pos), raw.begin() + static_cast<ptrdiff_t>(pos + take));
        pos += take;
    } while (pos < raw.size());

    AppendU32LE(gz, Checksum::Crc32(raw.data(), raw.size()));
    AppendU32LE(gz, static_cast<uint32_t>(raw.size()));
    return gz;
}

static std::vector<uint8_t> BuildStoredZipBytes(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files)
{
    std::vector<uint8_t> out;
    struct CdRec {
        std::string name;
        uint32_t crc = 0;
        uint32_t sz = 0;
        uint32_t locOff = 0;
    };
    std::vector<CdRec> cds;
    for (const auto& [name, payload] : files) {
        CdRec cd;
        cd.name = name;
        cd.sz = static_cast<uint32_t>(payload.size());
        cd.crc = Checksum::Crc32(payload.data(), payload.size());
        cd.locOff = static_cast<uint32_t>(out.size());

        uint8_t lhdr[30] = {};
        PutU32LE(lhdr + 0, 0x04034B50u);
        PutU16LE(lhdr + 4, 20u);
        PutU32LE(lhdr + 14, cd.crc);
        PutU32LE(lhdr + 18, cd.sz);
        PutU32LE(lhdr + 22, cd.sz);
        PutU16LE(lhdr + 26, static_cast<uint16_t>(cd.name.size()));
        out.insert(out.end(), lhdr, lhdr + 30);
        out.insert(out.end(), cd.name.begin(), cd.name.end());
        out.insert(out.end(), payload.begin(), payload.end());
        cds.push_back(std::move(cd));
    }

    const uint32_t cdStart = static_cast<uint32_t>(out.size());
    for (const auto& cd : cds) {
        uint8_t chdr[46] = {};
        PutU32LE(chdr + 0, 0x02014B50u);
        PutU16LE(chdr + 4, 20u);
        PutU16LE(chdr + 6, 20u);
        PutU32LE(chdr + 16, cd.crc);
        PutU32LE(chdr + 20, cd.sz);
        PutU32LE(chdr + 24, cd.sz);
        PutU16LE(chdr + 28, static_cast<uint16_t>(cd.name.size()));
        PutU32LE(chdr + 42, cd.locOff);
        out.insert(out.end(), chdr, chdr + 46);
        out.insert(out.end(), cd.name.begin(), cd.name.end());
    }
    const uint32_t cdSize = static_cast<uint32_t>(out.size()) - cdStart;
    uint8_t eocd[22] = {};
    PutU32LE(eocd + 0, 0x06054B50u);
    PutU16LE(eocd + 8, static_cast<uint16_t>(cds.size()));
    PutU16LE(eocd + 10, static_cast<uint16_t>(cds.size()));
    PutU32LE(eocd + 12, cdSize);
    PutU32LE(eocd + 16, cdStart);
    out.insert(out.end(), eocd, eocd + 22);
    return out;
}

// GTKWave VZT MSB-terminated 7-bit little-endian varints
static void AppendVztV32(std::vector<uint8_t>& out, uint32_t val) {
    for (;;) {
        const uint32_t nxt = val >> 7u;
        if (nxt == 0u) {
            out.push_back(static_cast<uint8_t>((val & 0x7Fu) | 0x80u));
            break;
        }
        out.push_back(static_cast<uint8_t>(val & 0x7Fu));
        val = nxt;
    }
}

static void AppendVztV64(std::vector<uint8_t>& out, uint64_t val) {
    for (;;) {
        const uint64_t nxt = val >> 7u;
        if (nxt == 0ULL) {
            out.push_back(static_cast<uint8_t>((val & 0x7Fu) | 0x80u));
            break;
        }
        out.push_back(static_cast<uint8_t>(val & 0x7Fu));
        val = nxt;
    }
}

// Encodes `raw` as a valid LZMA1 literal stream (props=0x5D: lc=3, lp=0, pb=2, dict=65536)
// inside GTKWave's `ztype=2` + `'z','7'` + VZT-v64 size + 5B LZMA1 header container.
static std::vector<uint8_t> WrapGtkWaveLzmaZ7(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> lzmaBody;
    uint64_t low = 0ULL;
    uint32_t range = 0xFFFFFFFFu;
    uint32_t cacheSize = 1u;
    uint8_t cache = 0u;

    uint16_t isMatch0[4] = { 1024u, 1024u, 1024u, 1024u };
    std::vector<uint16_t> litProbs(8u * 0x300u, 1024u);

    auto shiftLow = [&]() {
        const uint32_t lowHi = static_cast<uint32_t>(low >> 32u);
        if (static_cast<uint32_t>(low) < 0xFF000000u || lowHi != 0u) {
            uint8_t temp = cache;
            do {
                lzmaBody.push_back(static_cast<uint8_t>(temp + lowHi));
                temp = 0xFFu;
            } while (--cacheSize != 0u);
            cache = static_cast<uint8_t>(static_cast<uint32_t>(low) >> 24u);
        }
        ++cacheSize;
        low = static_cast<uint64_t>(static_cast<uint32_t>(low) << 8u);
    };

    auto encodeBit = [&](uint16_t& prob, uint32_t bit) {
        const uint32_t bound = (range >> 11u) * prob;
        if (bit == 0u) {
            range = bound;
            prob = static_cast<uint16_t>(prob + ((2048u - prob) >> 5u));
        } else {
            low += bound;
            range -= bound;
            prob = static_cast<uint16_t>(prob - (prob >> 5u));
        }
        while (range < 0x01000000u) {
            range <<= 8u;
            shiftLow();
        }
    };

    uint8_t prevByte = 0u;
    for (size_t i = 0; i < raw.size(); ++i) {
        const uint32_t posState = static_cast<uint32_t>(i & 3u);
        encodeBit(isMatch0[posState], 0u);
        const uint32_t litState = static_cast<uint32_t>(prevByte) >> 5u;
        uint16_t* probs = &litProbs[static_cast<size_t>(litState) * 0x300u];
        uint32_t ctx = 1u;
        const uint8_t sym = raw[i];
        for (int bitIdx = 7; bitIdx >= 0; --bitIdx) {
            const uint32_t b = (sym >> bitIdx) & 1u;
            encodeBit(probs[ctx], b);
            ctx = (ctx << 1u) | b;
        }
        prevByte = sym;
    }
    for (int f = 0; f < 5; ++f) shiftLow();

    std::vector<uint8_t> out = { 2u, 'z', '7' };
    AppendVztV64(out, raw.size());
    out.push_back(0x5Du);
    AppendU32LE(out, 65536u);
    out.insert(out.end(), lzmaBody.begin(), lzmaBody.end());
    return out;
}

static std::string MakeTekHexRecord(char recType, std::string_view bodyAfterCc) {
    auto tekVal = [](char c, bool sixBitMode) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return (c - 'A') + 10;
        if (!sixBitMode && c >= 'a' && c <= 'f') return (c - 'a') + 10;
        if (sixBitMode) {
            if (c >= 'G' && c <= 'Z') return (c - 'A') + 10;
            if (c == '$') return 36;
            if (c == '%') return 37;
            if (c == '.') return 38;
            if (c == '_') return 39;
            if (c >= 'a' && c <= 'z') return (c - 'a') + 40;
        }
        return 0;
    };

    const bool isSym = (recType == '3');
    const unsigned recLen = static_cast<unsigned>(5u + bodyAfterCc.size());
    char llHex[8] = {};
    std::snprintf(llHex, sizeof(llHex), "%02X", recLen & 0xFFu);

    uint32_t sum = 0u;
    sum += static_cast<uint32_t>(tekVal(llHex[0], isSym));
    sum += static_cast<uint32_t>(tekVal(llHex[1], isSym));
    sum += static_cast<uint32_t>(tekVal(recType, isSym));
    for (char ch : bodyAfterCc) {
        sum += static_cast<uint32_t>(tekVal(ch, isSym));
    }

    char prefix[16] = {};
    std::snprintf(prefix, sizeof(prefix), "%%%s%c%02X", llHex, recType, sum & 0xFFu);
    std::string rec(prefix);
    rec.append(bodyAfterCc);
    rec.push_back('\n');
    return rec;
}

} // namespace

void RunFormatTestsEdaWaveformsBus(TestCounter& tc) {
    const std::wstring edaDir = TestDataDir() + L"\\eda";
    const std::wstring waveformsDir = TestDataDir() + L"\\waveforms";
    const std::wstring workDir = ScratchTestDir() + L"\\eda_waveforms_bus";
    CreateDirectoryW(workDir.c_str(), nullptr);
    std::vector<std::wstring> createdFiles;

    auto writeEdaBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = workDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdFiles.push_back(p);
        return p;
    };
    auto writeEdaText = [&](const wchar_t* name, const char* text) -> std::wstring {
        std::wstring p = workDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, text, std::strlen(text));
        createdFiles.push_back(p);
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
    // Tektronix Extended Hexadecimal Firmware (.tek — Type '6', Type '3' 6-bit Checksum, Type '8')
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".tek"),
                  L"Stage15.EDA.TekHex_ExtensionRegistered");

        ParsedEdaBoard b1, b2, bSym;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\z80_monitor_rom.tek", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 2 && b1.pads.size() >= 3 &&
                  HasLabelSubstr(b1, L"ENTRY") && b1.netCount == 3,
                  L"Stage15.EDA.TekHex_DisjointSpansChecksumAndEntryVectorParsed");

        const char* tek32 =
            "%16669808004000C0FFEE11\n"
            "%0E82A808004000\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"stm32.tek", tek32), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 &&
                  HasLabelSubstr(b2, L"4 B") && b2.netCount == 2 &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.TekHex_32BitAddrAndDistinctHash");

        // Type '3' symbol record using full 6-bit Tektronix alphabet ('_', '$', lowercase, uppercase)
        // Section "CODE" (len 4) + symbol 1: flag=1, len=A (10), name="_reset$vec", addrLen=4, addr=1000
        std::string tekWithSym =
            MakeTekHexRecord('3', "4CODE1A_reset$vec41000") +
            MakeTekHexRecord('6', "4100031FF0FC30010") +
            MakeTekHexRecord('8', "41000");
        bool okSym = EdaViewer::InspectEdaFile(writeEdaText(L"sym_type3.tek", tekWithSym.c_str()), bSym);
        tc.Record(okSym && bSym.valid && HasLabelSubstr(bSym, L"_reset$vec") &&
                  HashEdaBoard(bSym) != HashEdaBoard(b1),
                  L"Stage15.EDA.TekHex_Type3SixBitSymbolRecordChecksumAndLabels");

        // Corrupting a single character in Type '3' symbol record invalidates the 6-bit checksum
        std::string badType3 = tekWithSym;
        const size_t posVec = badType3.find("_reset$vec");
        if (posVec != std::string::npos) badType3[posVec + 1u] = 'R'; // 'r'(57) != 'R'(27)
        assertEdaRejected(L"Stage15.EDA.TekHex_RejectBadType3SixBitChecksum",
                          writeEdaText(L"bad_sym_cs.tek", badType3.c_str()));

        assertEdaRejected(L"Stage15.EDA.TekHex_RejectBadChecksum",
                          writeEdaText(L"bad_cs.tek", "%166FF808004000C0FFEE11\n%0E82A808004000\n"));
        assertEdaRejected(L"Stage15.EDA.TekHex_RejectMissingTermination",
                          writeEdaText(L"no_eof.tek", "%16669808004000C0FFEE11\n"));
        assertEdaRejected(L"Stage15.EDA.TekHex_RejectIntelHexColon",
                          writeEdaText(L"ihex.tek", ":020000040800F2\n:00000001FF\n"));
    }

    // ========================================================================
    // IEEE 1364 / 1800 Verilog $readmemh & Vivado BRAM Image (.mem)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".mem"),
                  L"Stage15.EDA.VerilogMEM_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\riscv_bootrom.mem", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 2 && b1.pads.size() >= 3 &&
                  HasLabelSubstr(b1, L"ENTRY") && b1.netCount == 3,
                  L"Stage15.EDA.VerilogMEM_Underscore32BitAndCommentsParsed");

        const char* mem8 = "@0200\nAA 55 F0 0F 12 34\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"lut8.mem", mem8), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 &&
                  HasLabelSubstr(b2, L"6 B") && b2.netCount == 1 &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.VerilogMEM_8BitLutAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.VerilogMEM_RejectWithoutAtOrigin",
                          writeEdaText(L"no_at.mem", "DEAD BEEF CAFE BABE\n"));
        assertEdaRejected(L"Stage15.EDA.VerilogMEM_RejectNonHexToken",
                          writeEdaText(L"bad_tok.mem", "@0000\nAA BB ZZ DD\n"));
        assertEdaRejected(L"Stage15.EDA.VerilogMEM_RejectUnclosedBlockComment",
                          writeEdaText(L"unclosed.mem", "/* unclosed\n@0000\nAA BB\n"));
    }

    // ========================================================================
    // Xilinx FPGA Block RAM Memory Map (.bmm)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".bmm"),
                  L"Stage15.EDA.XilinxBMM_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\microblaze_lmb.bmm", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 4 && b1.pads.size() == 4 && b1.netCount == 4,
                  L"Stage15.EDA.XilinxBMM_AddressMapAndPlacedBramTilesParsed");

        const char* bmmUnplaced =
            "ADDRESS_SPACE boot_rom RAMB16 [0x0000:0x07FF]\n"
            "  BUS_BLOCK\n"
            "    u_rom/ram_hi [15:8];\n"
            "    u_rom/ram_lo [7:0];\n"
            "  END_BUS_BLOCK;\n"
            "END_ADDRESS_SPACE;\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"unplaced.bmm", bmmUnplaced), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.XilinxBMM_StandaloneAddressSpaceAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.XilinxBMM_RejectUnclosedAddressSpace",
                          writeEdaText(L"unclosed.bmm", "ADDRESS_SPACE s RAMB16 [0x0:0xFF] BUS_BLOCK a [7:0]; END_BUS_BLOCK;"));
        assertEdaRejected(L"Stage15.EDA.XilinxBMM_RejectInvertedBitRange",
                          writeEdaText(L"inv_bits.bmm", "ADDRESS_SPACE s RAMB16 [0x0:0xFF] BUS_BLOCK a [0:7]; END_BUS_BLOCK; END_ADDRESS_SPACE;"));
    }

    // ========================================================================
    // Project IceStorm / Lattice iCE40 Physical Constraints (.pcf)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".pcf"),
                  L"Stage15.EDA.IceStormPCF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\icebreaker_up5k.pcf", b1);
        tc.Record(ok1 && b1.valid && b1.pads.size() == 8 && b1.components.size() == 1 && b1.netCount == 9,
                  L"Stage15.EDA.IceStormPCF_Qfn48SetIoAndFreqParsed");

        const char* pcfBga =
            "set_io clk_100 J3\n"
            "set_io led0 B5\n"
            "set_io led1 B4\n"
            "set_io led2 A2\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"hx8k_bga.pcf", pcfBga), b2);
        tc.Record(ok2 && b2.valid && b2.pads.size() == 4 && b2.pads[0].shape == 0u &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.IceStormPCF_BgaBallGridAndDistinctHash");

        std::vector<uint8_t> x11Font = { 0x01, 'f', 'c', 'p', 0, 0, 0, 0, 1, 2, 3, 4 };
        assertEdaRejected(L"Stage15.EDA.IceStormPCF_RejectX11BinaryFont", writeEdaBytes(L"x11.pcf", x11Font));
        assertEdaRejected(L"Stage15.EDA.IceStormPCF_RejectUnknownDirective", writeEdaText(L"bad_cmd.pcf", "random_cmd a b\n"));
        assertEdaRejected(L"Stage15.EDA.IceStormPCF_RejectMissingSite", writeEdaText(L"no_site.pcf", "set_io clk_only\n"));
    }

    // ========================================================================
    // Lattice Diamond / Trellis ECP5 Logical Preference File (.lpf)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".lpf"),
                  L"Stage15.EDA.LatticeLPF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\ulx3s_ecp5.lpf", b1);
        tc.Record(ok1 && b1.valid && b1.pads.size() == 5 && b1.components.size() == 1 && b1.netCount == 6,
                  L"Stage15.EDA.LatticeLPF_Cabga381LocateIobufAndFreqParsed");

        const char* lpfQfp =
            "BLOCK RESETPATHS ;\n"
            "LOCATE COMP \"osc\" SITE \"12\" ;\n"
            "LOCATE COMP \"led\" SITE \"44\" ;\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"machxo2_qfp.lpf", lpfQfp), b2);
        tc.Record(ok2 && b2.valid && b2.pads.size() == 2 && b2.pads[0].shape == 1u &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.LatticeLPF_TqfpNumberedSitesAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.LatticeLPF_RejectMissingSemicolon",
                          writeEdaText(L"no_semi.lpf", "LOCATE COMP \"clk\" SITE \"G2\"\n"));
        assertEdaRejected(L"Stage15.EDA.LatticeLPF_RejectNoLocateComp",
                          writeEdaText(L"empty_pref.lpf", "BLOCK RESETPATHS ;\nBLOCK ASYNCPATHS ;\n"));
    }

    // ========================================================================
    // Xilinx ISE User Constraints File (.ucf)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".ucf"),
                  L"Stage15.EDA.XilinxUCF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\spartan6_atlys.ucf", b1);
        tc.Record(ok1 && b1.valid && b1.pads.size() == 6 && b1.pads[0].shape == 0u && b1.netCount == 7,
                  L"Stage15.EDA.XilinxUCF_Csg324NetLocAndTimespecParsed");

        const char* ucfTqfp =
            "NET clk32 LOC = P84 | IOSTANDARD = LVCMOS33 ;\n"
            "NET sw0   LOC = P123 | PULLUP ;\n"
            "NET led0  LOC = P134 ;\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"spartan3_tqfp.ucf", ucfTqfp), b2);
        tc.Record(ok2 && b2.valid && b2.pads.size() == 3 && b2.pads[0].shape == 1u &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.XilinxUCF_TqfpPinsAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.XilinxUCF_RejectMissingSemicolon",
                          writeEdaText(L"no_semi.ucf", "NET clk LOC = P84\n"));
        assertEdaRejected(L"Stage15.EDA.XilinxUCF_RejectUnknownKeyword",
                          writeEdaText(L"bad_kw.ucf", "BOGUS_CONSTRAINT foo = bar ;\n"));
    }

    // ========================================================================
    // Xschem Open-Source VLSI/Analog IC Schematic (.xsch)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".xsch"),
                  L"Stage15.EDA.Xschem_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\sky130_cmos_inv.xsch", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 3 && b1.circles.size() == 3 && b1.traces.size() >= 16,
                  L"Stage15.EDA.Xschem_Sky130InverterMosfetsAndNetsParsed");

        const char* xschSym =
            "v {xschem version=3.1.0 file_version=1.2}\n"
            "K {type=subcircuit}\n"
            "A 4 0 0 20 0 360 {}\n"
            "L 4 -40 0 -20 0 {}\n"
            "B 5 -42 -2 -38 2 {name=PLUS dir=in}\n"
            "B 5 38 -2 42 2 {name=MINUS dir=out}\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"subckt.xsch", xschSym), b2);
        tc.Record(ok2 && b2.valid && b2.pads.size() == 2 && b2.circles.size() == 1 &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.Xschem_SymbolPinsArcsAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.Xschem_RejectMissingVersionHeader",
                          writeEdaText(L"no_ver.xsch", "N 0 0 100 0 {lab=NET1}\n"));
        assertEdaRejected(L"Stage15.EDA.Xschem_RejectUnclosedAttrBrace",
                          writeEdaText(L"unclosed.xsch", "v {xschem version=3.4}\nN 0 0 10 10 {lab=A\n"));
    }

    // ========================================================================
    // FidoCad / FidoCadJ Electronic Circuit & PCB Layout (.fcd)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".fcd"),
                  L"Stage15.EDA.FidoCadJ_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\audio_preamp_pcb.fcd", b1);
        tc.Record(ok1 && b1.valid && b1.pads.size() == 6 && b1.components.size() == 1 &&
                  b1.components[0].refDes == L"R1" && b1.components[0].value == L"4k7",
                  L"Stage15.EDA.FidoCadJ_PcbTracksPadsAndMacroTyParsed");

        const char* fcdSch =
            "[FIDOCAD]\n"
            "LI 10 10 60 10 0\n"
            "BE 60 10 80 0 80 30 100 20\n"
            "EV 95 15 105 25\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"bezier_sch.fcd", fcdSch), b2);
        tc.Record(ok2 && b2.valid && b2.traces.size() == 9 && b2.circles.size() == 1 &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.FidoCadJ_SchematicBezierAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.FidoCadJ_RejectMissingFidocadBanner",
                          writeEdaText(L"no_hdr.fcd", "LI 10 10 50 50 0\n"));
        assertEdaRejected(L"Stage15.EDA.FidoCadJ_RejectEmptyHeaderOnly",
                          writeEdaText(L"empty.fcd", "[FIDOCAD]\nFJC C 1.0\n"));
    }

    // ========================================================================
    // KiCad Drawing Sheet / Title Block Layout (.kicad_wks)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".kicad_wks"),
                  L"Stage15.EDA.KiCadWks_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\cern_ohl_a4.kicad_wks", b1);
        tc.Record(ok1 && b1.valid && b1.traces.size() == 21 && b1.labels.size() == 6 &&
                  b1.labels[3].text == L"4",
                  L"Stage15.EDA.KiCadWks_TitleBlockRepeatZonesAndLogoParsed");

        const char* wksLegacy =
            "(page_layout\n"
            "  (setup (left_margin 15) (right_margin 15) (top_margin 15) (bottom_margin 15))\n"
            "  (rect (start 80 20 lbcorner) (end 0 0 lbcorner))\n"
            "  (tbtext \"A\" (pos 5 20 ltcorner) (repeat 3) (incry 30))\n"
            ")\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"legacy.kicad_wks", wksLegacy), b2);
        tc.Record(ok2 && b2.valid && b2.labels.size() == 3 && b2.labels[2].text == L"C" &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.KiCadWks_LegacyPageLayoutAlphaRepeatAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.KiCadWks_RejectWrongRootSExpr",
                          writeEdaText(L"wrong_root.kicad_wks", "(kicad_pcb (version 20221018))\n"));
        assertEdaRejected(L"Stage15.EDA.KiCadWks_RejectUnclosedParen",
                          writeEdaText(L"unclosed.kicad_wks", "(kicad_wks (rect (start 0 0) (end 10 10))\n"));
    }

    // ========================================================================
    // KiCad Custom Design Rules (.kicad_dru)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".kicad_dru"),
                  L"Stage15.EDA.KiCadDru_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\ddr4_hv_stackup.kicad_dru", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 3 && b1.vias.size() == 1 && b1.netCount == 3,
                  L"Stage15.EDA.KiCadDru_Ddr4HvAndHdiViaCouponsParsed");

        const char* druImperial =
            "(version 1)\n"
            "(rule \"RF_50Ohm_Bottom\"\n"
            "  (layer \"B.Cu\")\n"
            "  (constraint track_width (min 8mil) (opt 12mil))\n"
            "  (constraint clearance (min 15mil)))\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"rf_50r.kicad_dru", druImperial), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && b2.vias.empty() &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.KiCadDru_MilUnitsAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.KiCadDru_RejectMissingVersion",
                          writeEdaText(L"no_ver.kicad_dru", "(rule \"R1\" (constraint clearance (min 0.2mm)))\n"));
        assertEdaRejected(L"Stage15.EDA.KiCadDru_RejectRuleWithoutConstraint",
                          writeEdaText(L"empty_rule.kicad_dru", "(version 1)\n(rule \"Bad\" (layer \"F.Cu\"))\n"));
        assertEdaRejected(L"Stage15.EDA.KiCadDru_RejectInvalidUnit",
                          writeEdaText(L"bad_unit.kicad_dru", "(version 1)\n(rule \"Bad\" (constraint clearance (min 5parsecs)))\n"));
    }

    // ========================================================================
    // IPC-2511 / Mitron GenCAD 1.4 ECAD PCB Interchange (.gencad)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".gencad"),
                  L"Stage15.EDA.GenCad_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\can_fd_transceiver.gencad", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 2 && b1.pads.size() == 10 &&
                  b1.vias.size() == 2 && b1.netCount == 3 && b1.components[1].bottomSide,
                  L"Stage15.EDA.GenCad_MetricSoic8BottomResRoutesAndViasParsed");

        const char* gcThou =
            "$HEADER\n"
            "GENCAD 1.4\n"
            "DRAWING \"THOU_TEST\"\n"
            "UNITS THOU\n"
            "$ENDHEADER\n"
            "$BOARD\n"
            "RECTANGLE 0 0 1000 800\n"
            "CIRCLE 500 400 60\n"
            "$ENDBOARD\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"thou_board.gencad", gcThou), b2);
        tc.Record(ok2 && b2.valid && b2.traces.size() == 4 && b2.circles.size() == 1 &&
                  std::fabs((b2.maxX - b2.minX) - 25.4f) < 1.0f &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.GenCad_ThouUnitsAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.GenCad_RejectMissingGenCadBanner",
                          writeEdaText(L"no_magic.gencad", "$HEADER\nUNITS MM\n$ENDHEADER\n$BOARD\nLINE 0 0 10 10\n$ENDBOARD\n"));
        assertEdaRejected(L"Stage15.EDA.GenCad_RejectUnclosedSection",
                          writeEdaText(L"unclosed.gencad", "$HEADER\nGENCAD 1.4\nUNITS MM\n$ENDHEADER\n$BOARD\nLINE 0 0 10 10\n"));
    }

    // ========================================================================
    // IEEE 1364 Extended Value Change Dump (.evcd)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".evcd"),
                  L"Stage15.EDA.VerilogEVCD_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(waveformsDir + L"\\i2c_bidirectional.evcd", b1);
        tc.Record(ok1 && b1.valid && b1.netCount == 3 &&
                  HasLabelSubstr(b1, L"SDA [IO]") && b1.traces.size() >= 12,
                  L"Stage15.EDA.VerilogEVCD_DirectionalPortsAndStrengthsParsed");

        const char* evcdSpi =
            "$timescale 100ps $end\n"
            "$scope module spi_phy $end\n"
            "$var port 1 <0 SCK $end\n"
            "$var port 1 <1 MISO $end\n"
            "$upscope $end\n"
            "$enddefinitions $end\n"
            "#0\npD 6 0 <0\npT 0 0 <1\n"
            "#5\npU 0 6 <0\npH 0 6 <1\n"
            "#10\npD 6 0 <0\npL 6 0 <1\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"spi_phy.evcd", evcdSpi), b2);
        tc.Record(ok2 && b2.valid && b2.netCount == 2 &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.VerilogEVCD_SpiTristateAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.VerilogEVCD_RejectClassicWireVcd",
                          writeEdaText(L"classic_vcd.evcd",
                              "$var wire 1 ! clk $end\n$enddefinitions $end\n#0\n0!\n#10\n1!\n"));
        assertEdaRejected(L"Stage15.EDA.VerilogEVCD_RejectInvalidStrength",
                          writeEdaText(L"bad_strength.evcd",
                              "$var port 1 <0 clk $end\n$enddefinitions $end\n#0\npU 9 0 <0\n"));
        assertEdaRejected(L"Stage15.EDA.VerilogEVCD_RejectUnknownPortId",
                          writeEdaText(L"bad_id.evcd",
                              "$var port 1 <0 clk $end\n$enddefinitions $end\n#0\npU 0 6 <99\n"));
    }

    // ========================================================================
    // ASAM MCD-2 MC / ASAP2 Automotive ECU Calibration & Measurement Map (.a2l)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".a2l"),
                  L"Stage15.EDA.AsamA2L_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\ev_traction_inverter.a2l", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 5 && b1.pads.size() == 5 && b1.netCount == 5,
                  L"Stage15.EDA.AsamA2L_EcuFlashRamMapsAndMeasurementsParsed");

        const char* a2lBoost =
            "ASAP2_VERSION 1 61\n"
            "/begin PROJECT TURBO_EMS \"Boost Ctrl\"\n"
            "  /begin MODULE MPC5777C \"Engine\"\n"
            "    /begin CHARACTERISTIC C_BoostTarget \"Boost Curve\"\n"
            "      CURVE 0x00804000 Cur_U16 0.0 NO_COMPU_METHOD 100.0 260.0\n"
            "    /end CHARACTERISTIC\n"
            "    /begin MEASUREMENT P_ManifoldKpa \"MAP Sensor\"\n"
            "      UWORD NO_COMPU_METHOD 1 0.0 20.0 300.0 ECU_ADDRESS 0x40000100\n"
            "    /end MEASUREMENT\n"
            "  /end MODULE\n"
            "/end PROJECT\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"turbo_ems.a2l", a2lBoost), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 3 && b2.pads.size() == 2 &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.AsamA2L_CurveAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.AsamA2L_RejectMismatchedBeginEnd",
                          writeEdaText(L"mismatch.a2l",
                              "ASAP2_VERSION 1 71\n/begin PROJECT P \"\"\n/begin MODULE M \"\"\n/end PROJECT\n"));
        assertEdaRejected(L"Stage15.EDA.AsamA2L_RejectUnclosedComment",
                          writeEdaText(L"unclosed_cmt.a2l",
                              "ASAP2_VERSION 1 71 /* unterminated\n/begin PROJECT P \"\" /end PROJECT\n"));
        assertEdaRejected(L"Stage15.EDA.AsamA2L_RejectEmptyModule",
                          writeEdaText(L"empty_mod.a2l",
                              "ASAP2_VERSION 1 71\n/begin PROJECT P \"\"\n/begin MODULE M \"\"\n/end MODULE\n/end PROJECT\n"));
    }

    // ========================================================================
    // Berkeley SIS / Espresso / ABC Sequential FSM (.kiss2)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".kiss2"),
                  L"Stage15.EDA.BerkeleyKISS2_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\axi_lite_ctrl.kiss2", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 4 && b1.circles.size() == 7 &&
                  b1.pads.size() == 1 && b1.netCount == 7,
                  L"Stage15.EDA.BerkeleyKISS2_AxiLiteStatesSelfLoopsAndResetParsed");

        const char* kissToggle =
            ".i 1\n.o 1\n.s 2\n.r S0\n"
            "0 S0 S0 0\n"
            "1 S0 S1 0\n"
            "0 S1 S1 1\n"
            "1 S1 S0 1\n.end\n";
        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"toggle.kiss2", kissToggle), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 2 && b2.netCount == 4 &&
                  HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.BerkeleyKISS2_ToggleFsmAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.BerkeleyKISS2_RejectCombinational2ColPla",
                          writeEdaText(L"comb_pla.kiss2", ".i 2\n.o 1\n01 1\n10 1\n.e\n"));
        assertEdaRejected(L"Stage15.EDA.BerkeleyKISS2_RejectWrongCubeWidth",
                          writeEdaText(L"bad_width.kiss2", ".i 3\n.o 1\n01 S0 S1 1\n.e\n"));
        assertEdaRejected(L"Stage15.EDA.BerkeleyKISS2_RejectInvalidCubeChar",
                          writeEdaText(L"bad_char.kiss2", ".i 2\n.o 1\n0X S0 S1 1\n.e\n"));
    }

    // ========================================================================
    // Microchip Libero / PolarFire Physical Design Constraints (.pdc)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".pdc"), L"Stage15.EDA.MicrochipPDC_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\polarfire_soc_io.pdc", b1);
        tc.Record(ok1 && b1.valid && b1.pads.size() == 4 && b1.components.size() == 1 &&
                  b1.formatTitle.find(L".pdc") != std::wstring::npos,
                  L"Stage15.EDA.MicrochipPDC_PolarFireBgaSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"sf2_qfp.pdc",
            "set_io JTAG_TCK -pinname 82 -iostd LVCMOS33\n"
            "set_io LED_OUT -pinname 94 -iostd LVCMOS33\n"), b2);
        tc.Record(ok2 && b2.valid && b2.pads.size() == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.MicrochipPDC_SmartFusion2PositionalAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.MicrochipPDC_RejectNonLiberoScript",
                          writeEdaText(L"bad.pdc", "puts \"hello world\"\nset x 42\n"));
    }

    // ========================================================================
    // Gowin EDA FPGA Constraints (.cst)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".cst"), L"Stage15.EDA.GowinCST_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\tang_nano_9k.cst", b1);
        tc.Record(ok1 && b1.valid && b1.pads.size() == 4 && b1.components.size() == 1 &&
                  b1.formatTitle.find(L".cst") != std::wstring::npos,
                  L"Stage15.EDA.GowinCST_TangNano9kSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"arora_bga.cst",
            "IO_LOC \"ddr_ck\" A4,B4;\nIO_PORT \"ddr_ck\" IO_TYPE=SSTL15D;\n"
            "IO_LOC \"ddr_dq0\" C6;\nIO_PORT \"ddr_dq0\" IO_TYPE=SSTL15;\n"), b2);
        tc.Record(ok2 && b2.valid && b2.pads.size() == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.GowinCST_DiffPairBgaAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.GowinCST_RejectMissingSemicolon",
                          writeEdaText(L"no_semi.cst", "IO_LOC \"clk\" 52\n"));
    }

    // ========================================================================
    // JEDEC JESD-71 STAPL / Altera JAM JTAG (.jam)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".jam"), L"Stage15.EDA.JtagJAM_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\max10_jtag_prog.jam", b1);
        tc.Record(ok1 && b1.valid && b1.pads.size() == 6 && b1.components.size() == 6 &&
                  b1.formatTitle.find(L".jam") != std::wstring::npos,
                  L"Stage15.EDA.JtagJAM_Max10StaplSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"read_id.jam",
            "NOTE \"DEVICE\" \"EPM240\";\nACTION READ_ID = DO_ID;\n"
            "PROCEDURE DO_ID;\n  IRSCAN 10, $006;\n  DRSCAN 32, $00000000;\nENDPROC;\nCRC 12AF;\n"), b2);
        tc.Record(ok2 && b2.valid && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.JtagJAM_ReadIdcodeAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.JtagJAM_RejectUnclosedProcedure",
                          writeEdaText(L"unclosed.jam", "ACTION A = P;\nPROCEDURE P;\n  IRSCAN 8, $FF;\n"));
    }

    // ========================================================================
    // CoralEDA pcb-rnd Lihata Board (.lht)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".lht"), L"Stage15.EDA.PcbRndLHT_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\sensor_breakout.lht", b1);
        tc.Record(ok1 && b1.valid && b1.traces.size() == 6 && b1.pads.size() == 2 && b1.components.size() == 1,
                  L"Stage15.EDA.PcbRndLHT_BoardSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"footprint.lht",
            "li:pcb-rnd-subcircuit-v6 {\n  li:objects {\n    ha:subc.0 {\n      ha:data {\n"
            "        li:objects {\n          ha:padstack_ref.1 { x = -2.54mm; y = 0mm; }\n"
            "          ha:padstack_ref.2 { x =  2.54mm; y = 0mm; }\n        }\n      }\n    }\n  }\n}\n"), b2);
        tc.Record(ok2 && b2.valid && b2.pads.size() == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.PcbRndLHT_SubcircuitAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.PcbRndLHT_RejectUnclosedBrace",
                          writeEdaText(L"bad.lht", "li:pcb-rnd-board-v8 { ha:data { li:layers { "));
    }

    // ========================================================================
    // IPC-NC-349 CNC Drill & Rout (.xnc)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".xnc"), L"Stage15.EDA.IpcXNC_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\panel_rout_drill.xnc", b1);
        tc.Record(ok1 && b1.valid && b1.vias.size() == 3 && b1.traces.size() == 4,
                  L"Stage15.EDA.IpcXNC_DrillAndRoutSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"slot_inch.xnc",
            "M48\nINCH\nT01C0.040\n%\nT01\nX0.100Y0.200G85X0.500Y0.200\nM30\n"), b2);
        tc.Record(ok2 && b2.valid && b2.traces.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.IpcXNC_G85SlotInchAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.IpcXNC_RejectMissingM30End",
                          writeEdaText(L"no_end.xnc", "M48\nMETRIC\nT01C0.8\n%\nT01\nX5.0Y5.0\n"));

        const std::string bulgedXnc = "M48\nMETRIC\nT01C0.800\n%\nT01\nG00" + std::string(65536, 'A') + "\nM30\n";
        assertEdaRejected(L"Stage15.EDA.IpcXNC_RejectBulgedG00LineNoDanglingView",
                          writeEdaText(L"bulged_g00.xnc", bulgedXnc.c_str()));
    }

    // ========================================================================
    // IC LVS Circuit Description Language Netlist (.cdl)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".cdl"), L"Stage15.EDA.IcCDL_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\cmos_nand2_lvs.cdl", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 4 && b1.netCount == 6 &&
                  b1.formatTitle.find(L".cdl") != std::wstring::npos,
                  L"Stage15.EDA.IcCDL_CmosNand2Sample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"inv.cdl",
            "*.SCALE METER\n.SUBCKT INV_X1 IN OUT VDD VSS\n"
            "MP0 OUT IN VDD VDD pch W=200n L=28n\nMN0 OUT IN VSS VSS nch W=100n L=28n\n.ENDS\n"), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.IcCDL_InverterAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.IcCDL_RejectUnclosedSubckt",
                          writeEdaText(L"unclosed.cdl", ".SUBCKT BAD A B\nR1 A B 1k\n"));
    }

    // ========================================================================
    // Synopsys Interconnect Technology Format (.itf)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".itf"), L"Stage15.EDA.SynopsysITF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\finfet_7nm_rc.itf", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 3 && b1.vias.size() == 2 &&
                  b1.formatTitle.find(L".itf") != std::wstring::npos,
                  L"Stage15.EDA.SynopsysITF_7nmStackupSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"cmos180.itf",
            "TECHNOLOGY = cmos_180nm\nDIELECTRIC IMD1 { THICKNESS = 0.8 ER = 4.1 }\n"
            "CONDUCTOR METAL1 { THICKNESS = 0.53 RPSQ = 0.078 }\n"), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.SynopsysITF_180nmAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.SynopsysITF_RejectMissingTechnology",
                          writeEdaText(L"no_tech.itf", "CONDUCTOR M1 { THICKNESS = 0.1 }\n"));
    }

    // ========================================================================
    // IEEE 1801 / Synopsys SAIF Power Activity (.saif)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".saif"), L"Stage15.EDA.SynopsysSAIF_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(waveformsDir + L"\\alu_power_toggle.saif", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 3 && b1.netCount == 3 &&
                  b1.formatTitle.find(L".saif") != std::wstring::npos,
                  L"Stage15.EDA.SynopsysSAIF_AluToggleSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"fifo.saif",
            "(SAIFILE (SAIFVERSION \"2.0\") (DESIGN \"fifo\") (DURATION 500) "
            "(INSTANCE u_fifo (PORT (wr_en (T0 100) (T1 400) (TC 88)))))\n"), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.SynopsysSAIF_PortActivityAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.SynopsysSAIF_RejectNonSaifSExpr",
                          writeEdaText(L"bad.saif", "(kicad_pcb (version 4) (host pcbnew 4.0))\n"));
    }

    // ========================================================================
    // IBIS Electronic Board Description (.ebd)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".ebd"), L"Stage15.EDA.IbisEBD_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\ddr4_sodimm_rank.ebd", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 5 && b1.netCount == 2 &&
                  b1.formatTitle.find(L".ebd") != std::wstring::npos,
                  L"Stage15.EDA.IbisEBD_Ddr4FlybySample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"ptp.ebd",
            "[Begin Board Description] 1.0\n[Board Name] MCM\n"
            "[Path Description] CLK\nPin P1.1\nLen=0.02 L=4n C=1p /\nNode U9.A1\n[End Board Description]\n"), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.IbisEBD_PointToPointAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.IbisEBD_RejectUnclosedFork",
                          writeEdaText(L"bad_fork.ebd",
                              "[Begin Board Description] 1.0\n[Path Description] X\nPin J1.1\nFork\nNode U1.1\n[End Board Description]\n"));
    }

    // ========================================================================
    // ASAM MCD-2 D Automotive UDS Diagnostics (.odx)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".odx"), L"Stage15.EDA.AsamODX_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\uds_powertrain_ecu.odx", b1);
        tc.Record(ok1 && b1.valid && b1.pads.size() == 5 &&
                  b1.formatTitle.find(L".odx") != std::wstring::npos,
                  L"Stage15.EDA.AsamODX_PowertrainUdsSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"abs.odx",
            "<ODX><DIAG-LAYER-CONTAINER><BASE-VARIANT><SHORT-NAME>ABS_ESP</SHORT-NAME>"
            "<DIAG-SERVICE SEMANTIC=\"ROUTINE\"><SHORT-NAME>BleedBrakes_0x3101</SHORT-NAME></DIAG-SERVICE>"
            "</BASE-VARIANT></DIAG-LAYER-CONTAINER></ODX>"), b2);
        tc.Record(ok2 && b2.valid && b2.pads.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.AsamODX_AbsVariantAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.AsamODX_RejectMismatchedXmlTag",
                          writeEdaText(L"bad.odx", "<ODX><DIAG-SERVICE><SHORT-NAME>S</ODX>"));
    }

    // ========================================================================
    // PEAK PCAN Symbol File (.sym)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".sym"), L"Stage15.EDA.PeakSYM_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(edaDir + L"\\bms_canfd_bus.sym", b1);
        tc.Record(ok1 && b1.valid && b1.components.size() == 2 && b1.pads.size() == 6 &&
                  b1.formatTitle.find(L".sym") != std::wstring::npos,
                  L"Stage15.EDA.PeakSYM_BmsCanFdSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"obd.sym",
            "FormatVersion=5.0\n{RECEIVE}\n[WheelSpeed]\nID=120h\nLen=4\nVar=FrontLeft unsigned 0,16\n"), b2);
        tc.Record(ok2 && b2.valid && b2.components.size() == 1 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.PeakSYM_V5AndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.PeakSYM_RejectMissingFormatVersion",
                          writeEdaText(L"no_ver.sym", "[Msg]\nID=100h\nLen=8\nVar=A unsigned 0,8\n"));
    }

    // ========================================================================
    // OpenBench Logic Sniffer Capture (.ols)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".ols"), L"Stage15.EDA.OpenLogicOLS_ExtensionRegistered");

        ParsedEdaBoard b1, b2;
        bool ok1 = EdaViewer::InspectEdaFile(waveformsDir + L"\\spi_flash_capture.ols", b1);
        tc.Record(ok1 && b1.valid && !b1.traces.empty() && b1.netCount == 4 &&
                  b1.formatTitle.find(L".ols") != std::wstring::npos,
                  L"Stage15.EDA.OpenLogicOLS_SpiCaptureSample");

        bool ok2 = EdaViewer::InspectEdaFile(writeEdaText(L"i2c.ols",
            ";Rate: 24000000\n;Channels: 2\n00@0\n03@50\n01@100\n00@150\n"), b2);
        tc.Record(ok2 && b2.valid && b2.netCount == 2 && HashEdaBoard(b1) != HashEdaBoard(b2),
                  L"Stage15.EDA.OpenLogicOLS_I2cAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.OpenLogicOLS_RejectNonMonotonicTimestamps",
                          writeEdaText(L"backwards.ols", ";Rate: 1000000\n01@100\n00@50\n"));
    }

    // ========================================================================
    // GTKWave VZTD v1 Compressed Digital/Analog Waveform (.vzt — Gzip, Bzip2 & LZMA)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".vzt"), L"Stage15.EDA.GtkWaveVZT_ExtensionRegistered");

        ParsedEdaBoard bAxi, bLzma;
        // Sample `riscv_axi_bus.vzt` exercises 36-byte VZTD v1 header, gzip raw bitplane block 0,
        // bzip2 RLE bitplane block 1, DOUBLE analog string table (`axi_tb.vcore_v`), ALIAS (`axi_tb.clk_bus_alias`), and high-Z `'z'`
        bool okAxi = EdaViewer::InspectEdaFile(waveformsDir + L"\\riscv_axi_bus.vzt", bAxi);
        tc.Record(okAxi && bAxi.valid && bAxi.netCount == 8 && bAxi.traces.size() > 60 &&
                  bAxi.formatTitle.find(L"8 signals, 2 blocks") != std::wstring::npos &&
                  HasLabelSubstr(bAxi, L"axi_tb.awaddr") &&
                  HasLabelSubstr(bAxi, L"axi_tb.vcore_v") &&
                  HasLabelSubstr(bAxi, L"axi_tb.clk_bus_alias"),
                  L"Stage15.EDA.GtkWaveVZT_GzipAndBzip2RleBlocksAnalogAndAlias");

        // Build a second genuine VZTD v1 file whose value block is compressed with GTKWave LZMA (`ztype=2`, `'z','7'`)!
        // 2 real 1-bit signals ("spi.sck", "spi.cs_n") -> totalValues = 4 (2 val lanes + 2 X/Z lanes)
        std::vector<uint8_t> fnRaw = { 's','p','i','.','s','c','k', 0u,  0u, 4u, 'c','s','_','n', 0u };
        std::vector<uint8_t> gmRaw(32u, 0u); // 2 * [rows=0, msb=0, lsb=0, flags=0]
        auto zFn = WrapGzipStored(fnRaw);
        auto zGm = WrapGzipStored(gmRaw);

        std::vector<uint8_t> rawBlk;
        AppendVztV32(rawBlk, 4u);            // 4 ticks: 0, 20, 40, 60
        AppendVztV64(rawBlk, 0ULL);
        AppendVztV64(rawBlk, 20ULL);
        AppendVztV64(rawBlk, 20ULL);
        AppendVztV64(rawBlk, 20ULL);
        AppendVztV32(rawBlk, 1u);            // num_sections = 1
        AppendVztV32(rawBlk, 3u);            // 3 dictionary words: [0]=0x0000000A (sck 0,1,0,1), [1]=0x00000001 (cs_n 1,0,0,0), [2]=0x00000000 (no X/Z)
        rawBlk.push_back(0u);                // bp_rle = 0
        AppendU32BE(rawBlk, 0x0000000Au);
        AppendU32BE(rawBlk, 0x00000001u);
        AppendU32BE(rawBlk, 0x00000000u);
        AppendVztV32(rawBlk, 0u);            // num_str_entries = 0
        // vindex for 4 lanes: sck_val=0, csn_val=1, sck_xz=2, csn_xz=2
        AppendVztV32(rawBlk, 0u);
        AppendVztV32(rawBlk, 1u);
        AppendVztV32(rawBlk, 2u);
        AppendVztV32(rawBlk, 2u);

        auto lzmaCompBlk = WrapGtkWaveLzmaZ7(rawBlk);

        std::vector<uint8_t> vztLzma;
        AppendStr(vztLzma, "VZTD");
        AppendU16BE(vztLzma, 1u);
        vztLzma.push_back(32u); // granule_size = 32
        AppendU32BE(vztLzma, 2u); // numfacs = 2
        AppendU32BE(vztLzma, 2u); // numrealfacs = 2
        AppendU32BE(vztLzma, static_cast<uint32_t>(fnRaw.size()));
        AppendU32BE(vztLzma, 8u);
        AppendU32BE(vztLzma, static_cast<uint32_t>(zFn.size()));
        AppendU32BE(vztLzma, static_cast<uint32_t>(fnRaw.size()));
        AppendU32BE(vztLzma, static_cast<uint32_t>(zGm.size()));
        vztLzma.push_back(static_cast<uint8_t>(static_cast<int8_t>(-9)));
        vztLzma.insert(vztLzma.end(), zFn.begin(), zFn.end());
        vztLzma.insert(vztLzma.end(), zGm.begin(), zGm.end());
        AppendU32BE(vztLzma, static_cast<uint32_t>(rawBlk.size()));
        AppendU32BE(vztLzma, static_cast<uint32_t>(lzmaCompBlk.size()));
        AppendU64BE(vztLzma, 0ULL);
        AppendU64BE(vztLzma, 60ULL);
        vztLzma.insert(vztLzma.end(), lzmaCompBlk.begin(), lzmaCompBlk.end());

        bool okLzma = EdaViewer::InspectEdaFile(writeEdaBytes(L"spi_lzma.vzt", vztLzma), bLzma);
        tc.Record(okLzma && bLzma.valid && bLzma.netCount == 2 &&
                  HasLabelSubstr(bLzma, L"spi.sck") && HasLabelSubstr(bLzma, L"spi.cs_n") &&
                  HashEdaBoard(bAxi) != HashEdaBoard(bLzma),
                  L"Stage15.EDA.GtkWaveVZT_LzmaZ7CompressedSectionAndDistinctHash");

        std::vector<uint8_t> badGranVzt = vztLzma;
        badGranVzt[6] = 0u; // granule_size == 0 is invalid
        assertEdaRejected(L"Stage15.EDA.GtkWaveVZT_RejectZeroGranuleSize",
                          writeEdaBytes(L"bad_gran.vzt", badGranVzt));

        std::vector<uint8_t> truncVzt(vztLzma.begin(), vztLzma.end() - 10);
        assertEdaRejected(L"Stage15.EDA.GtkWaveVZT_RejectTruncatedBlockStream",
                          writeEdaBytes(L"trunc_blk.vzt", truncVzt));
    }

    // ========================================================================
    // DARPA / GNU Radio SigMF RF Metadata & Baseband IQ (.sigmf-meta + .sigmf-data)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".sigmf-meta"), L"Stage15.EDA.SigMF_ExtensionRegistered");

        ParsedEdaBoard bLora, bGps;
        // Sample `ism915_lora_burst.sigmf-meta` + companion `ism915_lora_burst.sigmf-data` (2048 ci16_le IQ pts)
        bool okLora = EdaViewer::InspectEdaFile(waveformsDir + L"\\ism915_lora_burst.sigmf-meta", bLora);
        tc.Record(okLora && bLora.valid &&
                  bLora.components.empty() && bLora.pads.size() == 2 && bLora.netCount == 5 &&
                  bLora.traces.size() > 1500 &&
                  bLora.formatTitle.find(L"2048 IQ pts") != std::wstring::npos &&
                  HasLabelSubstr(bLora, L"LoRa_SF7_BW250k_UpChirp") &&
                  HasLabelSubstr(bLora, L"I (In-Phase)") &&
                  HasLabelSubstr(bLora, L"Q (Quadrature)"),
                  L"Stage15.EDA.SigMF_LoraCompanionCi16IqAndZeroFakePcbChips");

        // Second variant: cf32_le complex float32 companion `.sigmf-data` + GPS L1 capture
        std::vector<uint8_t> gpsIqBytes;
        for (int i = 0; i < 64; ++i) {
            const float iv = std::cos(static_cast<float>(i) * 0.25f);
            const float qv = std::sin(static_cast<float>(i) * 0.25f);
            AppendF32LE(gpsIqBytes, iv);
            AppendF32LE(gpsIqBytes, qv);
        }
        writeEdaBytes(L"gps_l1.sigmf-data", gpsIqBytes);
        std::wstring gpsMetaPath = writeEdaText(L"gps_l1.sigmf-meta",
            "{\"global\":{\"core:datatype\":\"cf32_le\",\"core:sample_rate\":4092000},"
            "\"captures\":[{\"core:sample_start\":0,\"core:frequency\":1575420000}]}");
        bool okGps = EdaViewer::InspectEdaFile(gpsMetaPath, bGps);
        tc.Record(okGps && bGps.valid && bGps.components.empty() &&
                  bGps.formatTitle.find(L"64 IQ pts") != std::wstring::npos &&
                  HashEdaBoard(bLora) != HashEdaBoard(bGps),
                  L"Stage15.EDA.SigMF_Cf32CompanionIqAndDistinctHash");

        assertEdaRejected(L"Stage15.EDA.SigMF_RejectMissingCoreDatatype",
                          writeEdaText(L"no_dt.sigmf-meta", "{\"global\":{\"core:sample_rate\":1000000},\"captures\":[{}]}"));
        assertEdaRejected(L"Stage15.EDA.SigMF_RejectEmptyCapturesWithoutIqData",
                          writeEdaText(L"empty_cap.sigmf-meta", "{\"global\":{\"core:datatype\":\"ci16_le\",\"core:sample_rate\":1000000}}"));
    }

    // ========================================================================
    // Vector CAN Database (.dbc & .can Routing — 11/29-bit IDs & Motorola/Intel Bitfields)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".dbc") && EdaViewer::IsEdaExtension(L".can"),
                  L"Stage15.Eda.VectorDbcRouting.ExtensionsRegistered");

        const char* j1939Dbc =
            "VERSION \"J1939_EV\"\n"
            "BO_ 2566844926 EEC1_Motor: 8 INV_ECU\n"
            " SG_ MotorTorque : 7|16@0- (0.1,-3200) [-3200|3200] \"Nm\" VCU\n"
            " SG_ MotorRpm : 24|16@1+ (0.25,0) [0|16000] \"rpm\" VCU\n"
            "BO_ 290 BMS_Pack: 8 BMS_ECU\n"
            " SG_ PackSoc : 0|8@1+ (0.5,0) [0|100] \"%\" VCU\n";

        ParsedEdaBoard dbcBrd, canRoutedBrd;
        bool okDbc = EdaViewer::InspectEdaFile(writeEdaText(L"j1939_ev.dbc", j1939Dbc), dbcBrd);
        bool okCan = EdaViewer::InspectEdaFile(writeEdaText(L"j1939_ev.can", j1939Dbc), canRoutedBrd);

        tc.Record(okDbc && dbcBrd.valid && okCan && canRoutedBrd.valid &&
                  dbcBrd.components.size() == 2 && dbcBrd.pads.size() == 3 &&
                  dbcBrd.netCount == 4 &&
                  HasLabelSubstr(dbcBrd, L"0x18FEF1FE") &&
                  HasLabelSubstr(dbcBrd, L"MotorTorque") &&
                  canRoutedBrd.components.size() == 2,
                  L"Stage15.Eda.VectorDbcRouting.Extended29BitBeLeSignalsAndCanDispatch");

        assertEdaRejected(L"Stage15.Eda.VectorDbcRouting.RejectEmptyVersionOnlyDbc",
                          writeEdaText(L"empty_ver.dbc", "VERSION \"1.0\"\nNS_ :\nBS_:\n"));
    }

    // ========================================================================
    // Touchstone Multi-Port RF S-Parameters (.s1p .. .s8p & .snp)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".s1p") &&
                  EdaViewer::IsEdaExtension(L".s2p") &&
                  EdaViewer::IsEdaExtension(L".s3p") &&
                  EdaViewer::IsEdaExtension(L".s4p") &&
                  EdaViewer::IsEdaExtension(L".s5p") &&
                  EdaViewer::IsEdaExtension(L".s6p") &&
                  EdaViewer::IsEdaExtension(L".s7p") &&
                  EdaViewer::IsEdaExtension(L".s8p") &&
                  EdaViewer::IsEdaExtension(L".snp"),
                  L"Stage15.Eda.TouchstoneMultiPort.AllS1pToS8pExtensionsRegistered");

        // 3-port directional coupler (.s3p -> S11, S21, S31)
        const char* s3pCoupler =
            "! 3-Port RF Directional Coupler\n"
            "# GHz S DB R 50\n"
            "1.0  -22.0 10  -0.5 -25  -20.0 80\n"
            "     -0.5 -25  -23.0 12  -32.0 -40\n"
            "     -20.0 80  -32.0 -40 -21.0 15\n"
            "2.0  -25.0 -5  -0.6 -50  -19.8 45\n"
            "     -0.6 -50  -24.5 -8  -34.0 -75\n"
            "     -19.8 45  -34.0 -75 -24.0 -2\n"
            "3.0  -18.0 -40 -0.9 -80  -20.3 10\n"
            "     -0.9 -80  -18.5 -42 -30.0 -110\n"
            "     -20.3 10  -30.0 -110 -19.0 -38\n";

        // 8-port high-speed SERDES channel (.s8p: 3 frequencies x [1 + 2 * 64] = 387 numbers)
        std::string s8pSerdes = "! 8-Port SERDES Backplane\n[Version] 2.0\n# GHz S MA R 50\n[Number of Ports] 8\n[Network Data]\n";
        for (int fi = 0; fi < 3; ++fi) {
            const double fGhz = 2.0 + fi * 4.0;
            char fbuf[32] = {};
            std::snprintf(fbuf, sizeof(fbuf), "%.1f", fGhz);
            s8pSerdes += fbuf;
            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    double mag = 0.02;
                    if (r == c) mag = 0.08 + 0.05 * fi;
                    else if ((r == 1 && c == 0) || (r == 3 && c == 2)) mag = 0.88 - 0.14 * fi;
                    const double ang = -25.0 * (fi + 1) - (r + c) * 3.0;
                    char pbuf[40] = {};
                    std::snprintf(pbuf, sizeof(pbuf), " %.4f %.1f", mag, ang);
                    s8pSerdes += pbuf;
                }
                s8pSerdes += "\n";
            }
        }
        s8pSerdes += "[End]\n";

        ParsedEdaBoard bS3p, bS8p;
        bool okS3p = EdaViewer::InspectEdaFile(writeEdaText(L"coupler.s3p", s3pCoupler), bS3p);
        bool okS8p = EdaViewer::InspectEdaFile(writeEdaText(L"serdes_8port.s8p", s8pSerdes.c_str()), bS8p);

        tc.Record(okS3p && bS3p.valid && bS3p.components.empty() && bS3p.netCount == 3 &&
                  HasLabelSubstr(bS3p, L"S31 (Coupled)") &&
                  okS8p && bS8p.valid && bS8p.components.empty() && bS8p.netCount == 4 &&
                  bS8p.formatTitle.find(L"8-Port S-Parameters") != std::wstring::npos &&
                  HasLabelSubstr(bS8p, L"SDD21") && !bS8p.circles.empty() &&
                  HashEdaBoard(bS3p) != HashEdaBoard(bS8p),
                  L"Stage15.Eda.TouchstoneMultiPort.S3pCouplerAndS8pDifferentialMatrix");

        assertEdaRejected(L"Stage15.Eda.TouchstoneMultiPort.RejectSingleFrequencyRowOnly",
                          writeEdaText(L"one_pt.s1p", "# GHz S DB R 50\n1.0 -12.0 45.0\n"));
        assertEdaRejected(L"Stage15.Eda.TouchstoneMultiPort.RejectZeroPortCount",
                          writeEdaText(L"zero_ports.snp", "[Version] 2.0\n# GHz S MA R 50\n[Number of Ports] 0\n1.0 0.5 0\n2.0 0.4 -10\n"));
    }

    // ========================================================================
    // Yokogawa DL-Series Oscilloscope Waveform (.wvf + .hdr & Inline Big-Endian .wvf)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".wvf"),
                  L"Stage15.Eda.YokogawaWvf.ExtensionRegistered");

        ParsedEdaBoard dl750Wvf, inlineBeWvf;
        bool okDl750 = EdaViewer::InspectEdaFile(waveformsDir + L"\\yokogawa_dl750_motor.wvf", dl750Wvf);
        tc.Record(okDl750 && dl750Wvf.valid &&
                  dl750Wvf.netCount == 3 && dl750Wvf.traces.size() > 500 &&
                  dl750Wvf.formatTitle.find(L"DL750, 3 ch, 512 pts, Little-endian") != std::wstring::npos &&
                  HasLabelSubstr(dl750Wvf, L"U_PHASE_V") &&
                  HasLabelSubstr(dl750Wvf, L"I_DCBUS_A"),
                  L"Stage15.Eda.YokogawaWvf.CompanionHdrThreePhaseMotorCapture");

        // Single-file inline `$PublicInfo` + `$EndOfHeader` Big-Endian FS4 (float32 BE) `.wvf`
        const std::string inlineHdr =
            "//YOKOGAWA ASCII FILE FORMAT\n"
            "$PublicInfo\n"
            "Model DL1640\n"
            "Endian Big\n"
            "$Group1\n"
            "TraceNumber 1\n"
            "TraceName V_GATE\n"
            "BlockSize 16\n"
            "VDataType FS4\n"
            "VResolution 1.0\n"
            "VOffset 0.0\n"
            "VUnit V\n"
            "HResolution 1.0E-7\n"
            "HOffset 0.0\n"
            "HUnit s\n"
            "$EndOfHeader\n";
        std::vector<uint8_t> singleWvf(inlineHdr.begin(), inlineHdr.end());
        for (int i = 0; i < 16; ++i) {
            const float vf = (i < 8) ? 0.0f : 12.0f;
            AppendF32BE(singleWvf, vf);
        }
        bool okInline = EdaViewer::InspectEdaFile(writeEdaBytes(L"inline_be.wvf", singleWvf), inlineBeWvf);
        tc.Record(okInline && inlineBeWvf.valid && inlineBeWvf.netCount == 1 &&
                  inlineBeWvf.formatTitle.find(L"Big-endian") != std::wstring::npos &&
                  HashEdaBoard(dl750Wvf) != HashEdaBoard(inlineBeWvf),
                  L"Stage15.Eda.YokogawaWvf.SingleFileBigEndianFs4AndDistinctHash");

        // Orphan binary `.wvf` without `.hdr`, truncated binary `.wvf`, and invalid `VDataType`
        assertEdaRejected(L"Stage15.Eda.YokogawaWvf.RejectOrphanBinaryWithoutHdr",
                          writeEdaBytes(L"orphan_no_hdr.wvf", std::vector<uint8_t>(128, 0x42u)));

        writeEdaText(L"trunc_pair.hdr",
            "$PublicInfo\nEndian Little\n$Group1\nTraceNumber 1\nTraceName CH1\nBlockSize 512\nVDataType IS2\nVResolution 0.01\nHResolution 1e-6\n");
        assertEdaRejected(L"Stage15.Eda.YokogawaWvf.RejectTruncatedTracePayload",
                          writeEdaBytes(L"trunc_pair.wvf", std::vector<uint8_t>(24, 0x01u)));

        writeEdaText(L"bad_dt.hdr",
            "$PublicInfo\nEndian Little\n$Group1\nTraceNumber 1\nTraceName CH1\nBlockSize 16\nVDataType BOGUS8\n");
        assertEdaRejected(L"Stage15.Eda.YokogawaWvf.RejectUnknownVDataType",
                          writeEdaBytes(L"bad_dt.wvf", std::vector<uint8_t>(128, 0x00u)));
    }

    // ========================================================================
    // DreamSourceLab DSView Logic Analyzer Session (.dsl — ZIP `header` + `L-p-b` Bitplanes)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".dsl"),
                  L"Stage15.Eda.DreamSourceDsl.ExtensionRegistered");

        ParsedEdaBoard spiDsl, i2cDsl;
        bool okSpi = EdaViewer::InspectEdaFile(waveformsDir + L"\\dslogic_spi_flash.dsl", spiDsl);
        tc.Record(okSpi && spiDsl.valid &&
                  spiDsl.netCount == 4 && spiDsl.traces.size() > 100 &&
                  spiDsl.formatTitle.find(L"4 probes @ 50 MHz, 512 samples, DSLogic") != std::wstring::npos &&
                  HasLabelSubstr(spiDsl, L"D0:CS_N") &&
                  HasLabelSubstr(spiDsl, L"D1:SCLK") &&
                  HasLabelSubstr(spiDsl, L"D2:MOSI") &&
                  HasLabelSubstr(spiDsl, L"D3:MISO"),
                  L"Stage15.Eda.DreamSourceDsl.SpiFlash4ProbesMultiBlockSample");

        // Second variant: 1-based probe names (`probe1 = SDA`, `probe2 = SCL`) with `L-0-0` & `L-1-0` @ 1 MHz
        const std::string hdrI2c =
            "[version]\nversion = 2\n[header]\ndriver = DSLogic Plus\nsamplerate = 1 MHz\n"
            "total probes = 2\ntotal blocks = 1\nprobe1 = SDA\nprobe2 = SCL\n";
        auto i2cZip = BuildStoredZipBytes({
            { "header", std::vector<uint8_t>(hdrI2c.begin(), hdrI2c.end()) },
            { "L-0-0",  { 0xFFu, 0x00u, 0xAAu, 0x55u } },
            { "L-1-0",  { 0xCCu, 0x33u, 0xCCu, 0x33u } }
        });
        bool okI2c = EdaViewer::InspectEdaFile(writeEdaBytes(L"i2c_1mhz.dsl", i2cZip), i2cDsl);
        tc.Record(okI2c && i2cDsl.valid && i2cDsl.netCount == 2 &&
                  HasLabelSubstr(i2cDsl, L"D0:SDA") && HasLabelSubstr(i2cDsl, L"D1:SCL") &&
                  HashEdaBoard(spiDsl) != HashEdaBoard(i2cDsl),
                  L"Stage15.Eda.DreamSourceDsl.OneBasedProbeNamesAndDistinctHash");

        // Reject sigrok `.sr`-style ZIP (no `header` / `L-0-0`), header-only ZIP, and non-ZIP bytes
        const std::string srMeta = "[device 1]\nsamplerate=1 MHz\ntotal probes=1\n";
        auto fakeSrZip = BuildStoredZipBytes({
            { "metadata",  std::vector<uint8_t>(srMeta.begin(), srMeta.end()) },
            { "logic-1-1", { 0x00u, 0xFFu } }
        });
        assertEdaRejected(L"Stage15.Eda.DreamSourceDsl.RejectSigrokSrLayoutInDsl",
                          writeEdaBytes(L"sigrok_renamed.dsl", fakeSrZip));

        auto hdrOnlyZip = BuildStoredZipBytes({
            { "header", std::vector<uint8_t>(hdrI2c.begin(), hdrI2c.end()) }
        });
        assertEdaRejected(L"Stage15.Eda.DreamSourceDsl.RejectHeaderOnlyWithoutLogicBlocks",
                          writeEdaBytes(L"hdr_only.dsl", hdrOnlyZip));
        assertEdaRejected(L"Stage15.Eda.DreamSourceDsl.RejectNonZipGarbage",
                          writeEdaText(L"garbage.dsl", "NOT_A_PKZIP_DSVIEW_CAPTURE"));
    }

    for (const auto& f : createdFiles) DeleteFileW(f.c_str());
    RemoveDirectoryW(workDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
