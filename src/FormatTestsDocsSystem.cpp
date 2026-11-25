// FormatTestsDocsSystem.cpp: self-tests of the binary, media and system document readers (SpreadsheetDocViewer, formats/Extended*), stage 15 ("15.docs.system" runs these alone)
#include "Crc32.h"
#include "SelfTestCommon.h"
#include "TestBytes.h"
#include "SpreadsheetDocViewer.h"
#include "PathUtils.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

struct ByteBuf {
    std::vector<uint8_t> b;

    void U8(uint8_t v) { b.push_back(v); }
    void U16LE(uint16_t v) {
        b.push_back(static_cast<uint8_t>(v & 0xFFu));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }
    void U32LE(uint32_t v) {
        b.push_back(static_cast<uint8_t>(v & 0xFFu));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }
    void U64LE(uint64_t v) {
        U32LE(static_cast<uint32_t>(v & 0xFFFFFFFFULL));
        U32LE(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFULL));
    }
    void U16BE(uint16_t v) {
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        b.push_back(static_cast<uint8_t>(v & 0xFFu));
    }
    void U32BE(uint32_t v) {
        b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
        b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        b.push_back(static_cast<uint8_t>(v & 0xFFu));
    }
    void Bytes(const void* p, size_t n) {
        const auto* u = static_cast<const uint8_t*>(p);
        b.insert(b.end(), u, u + n);
    }
    void Str(std::string_view s) {
        Bytes(s.data(), s.size());
    }
    void FixedStr(std::string_view s, size_t totalLen, uint8_t pad = 0) {
        size_t take = (s.size() < totalLen) ? s.size() : totalLen;
        Bytes(s.data(), take);
        if (totalLen > take) b.insert(b.end(), totalLen - take, pad);
    }
    void Utf16LE(std::wstring_view w, bool nullTerm = false) {
        for (wchar_t ch : w) U16LE(static_cast<uint16_t>(ch));
        if (nullTerm) U16LE(0);
    }
    void Zeros(size_t n) {
        b.insert(b.end(), n, 0);
    }
    void PadTo(size_t targetOffset, uint8_t pad = 0) {
        if (b.size() < targetOffset) b.insert(b.end(), targetOffset - b.size(), pad);
    }
    void Uleb(uint64_t v) {
        do {
            uint8_t byte = static_cast<uint8_t>(v & 0x7Fu);
            v >>= 7;
            if (v != 0) byte |= 0x80u;
            b.push_back(byte);
        } while (v != 0);
    }
    void EncInt(uint64_t v) {
        uint8_t tmp[10] = {};
        int n = 0;
        do {
            tmp[n++] = static_cast<uint8_t>(v & 0x7Fu);
            v >>= 7;
        } while (v != 0 && n < 10);
        for (int i = n - 1; i >= 0; --i) {
            b.push_back(static_cast<uint8_t>(tmp[i] | (i > 0 ? 0x80u : 0x00u)));
        }
    }
};

using namespace ::Explorer2::TestBytes; // byte writers (TestBytes.h)


static std::vector<uint8_t> BuildStoredZipSingle(std::string_view innerPath, std::string_view content) {
    const auto* payload = reinterpret_cast<const uint8_t*>(content.data());
    uint32_t crc = Checksum::Crc32(payload, content.size());
    uint32_t sz  = static_cast<uint32_t>(content.size());

    ByteBuf z;
    // Local file header
    z.U32LE(0x04034B50u);
    z.U16LE(20);
    z.U16LE(0);
    z.U16LE(0); // Store
    z.U16LE(0);
    z.U16LE(0);
    z.U32LE(crc);
    z.U32LE(sz);
    z.U32LE(sz);
    z.U16LE(static_cast<uint16_t>(innerPath.size()));
    z.U16LE(0);
    z.Str(innerPath);
    z.Str(content);

    uint32_t cdOff = static_cast<uint32_t>(z.b.size());
    // Central directory header
    z.U32LE(0x02014B50u);
    z.U16LE(20);
    z.U16LE(20);
    z.U16LE(0);
    z.U16LE(0);
    z.U16LE(0);
    z.U16LE(0);
    z.U32LE(crc);
    z.U32LE(sz);
    z.U32LE(sz);
    z.U16LE(static_cast<uint16_t>(innerPath.size()));
    z.U16LE(0);
    z.U16LE(0);
    z.U16LE(0);
    z.U16LE(0);
    z.U32LE(0);
    z.U32LE(0);
    z.Str(innerPath);
    uint32_t cdSize = static_cast<uint32_t>(z.b.size()) - cdOff;

    // EOCD
    z.U32LE(0x06054B50u);
    z.U16LE(0);
    z.U16LE(0);
    z.U16LE(1);
    z.U16LE(1);
    z.U32LE(cdSize);
    z.U32LE(cdOff);
    z.U16LE(0);
    return z.b;
}

static std::vector<uint8_t> BuildStoredGzip(std::string_view content) {
    const auto* payload = reinterpret_cast<const uint8_t*>(content.data());
    uint32_t crc = Checksum::Crc32(payload, content.size());
    uint16_t len = static_cast<uint16_t>(content.size());

    ByteBuf gz;
    // RFC 1952 Gzip 10-byte header
    gz.Bytes("\x1F\x8B\x08\x00\x00\x00\x00\x00\x00\x03", 10);
    // RFC 1951 Stored final block (BFINAL=1, BTYPE=00 => 0x01)
    gz.U8(0x01);
    gz.U16LE(len);
    gz.U16LE(static_cast<uint16_t>(~len));
    gz.Str(content);
    gz.U32LE(crc);
    gz.U32LE(static_cast<uint32_t>(content.size()));
    return gz.b;
}

static uint64_t HashDocContent(const ParsedOfficeDocument& doc) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](const void* p, size_t n) {
        const auto* u = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= u[i];
            h *= 1099511628211ULL;
        }
    };
    for (const auto& s : doc.sheets) {
        mix(s.name.data(), s.name.size() * sizeof(wchar_t));
        for (const auto& r : s.rows) {
            for (const auto& c : r) mix(c.data(), c.size() * sizeof(wchar_t));
        }
    }
    for (const auto& p : doc.paragraphs) {
        mix(p.text.data(), p.text.size() * sizeof(wchar_t));
    }
    return h;
}

static bool DocContainsToken(const ParsedOfficeDocument& doc, std::wstring_view needle) {
    std::wstring lowNeedle = PathUtils::ToLower(needle);
    for (const auto& s : doc.sheets) {
        for (const auto& r : s.rows) {
            for (const auto& c : r) {
                if (PathUtils::ToLower(c).find(lowNeedle) != std::wstring::npos) return true;
            }
        }
    }
    for (const auto& p : doc.paragraphs) {
        if (PathUtils::ToLower(p.text).find(lowNeedle) != std::wstring::npos) return true;
    }
    return false;
}

static void VerifyPairAndCorrupt(
    TestCounter& tc,
    const std::wstring& dir,
    const wchar_t* testLabel,
    const wchar_t* ext,
    const std::vector<uint8_t>& bytesA,
    std::wstring_view expectedTokenA,
    const std::vector<uint8_t>& bytesB,
    std::wstring_view expectedTokenB,
    const std::vector<uint8_t>& corruptBytes)
{
    std::wstring stem = testLabel;
    for (wchar_t& ch : stem) {
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'))) ch = L'_';
    }
    std::wstring pathA   = dir + L"\\" + stem + L"_A" + ext;
    std::wstring pathB   = dir + L"\\" + stem + L"_B" + ext;
    std::wstring pathBad = dir + L"\\" + stem + L"_bad" + ext;

    PathUtils::WriteFileBytes(pathA, bytesA.data(), bytesA.size());
    PathUtils::WriteFileBytes(pathB, bytesB.data(), bytesB.size());
    PathUtils::WriteFileBytes(pathBad, corruptBytes.data(), corruptBytes.size());

    ParsedOfficeDocument docA, docB, docBad;
    bool okA = SpreadsheetDocViewer::InspectOfficeFile(pathA, docA) && docA.valid;
    bool okB = SpreadsheetDocViewer::InspectOfficeFile(pathB, docB) && docB.valid;
    bool badRejected = !SpreadsheetDocViewer::InspectOfficeFile(pathBad, docBad) || !docBad.valid;

    DeleteFileW(pathA.c_str());
    DeleteFileW(pathB.c_str());
    DeleteFileW(pathBad.c_str());

    if (!okA || !okB) {
        tc.Record(false, testLabel, std::wstring(L"Valid A/B parse failed (okA=") + (okA ? L"1" : L"0") + L", okB=" + (okB ? L"1" : L"0") + L")");
        return;
    }
    uint64_t hA = HashDocContent(docA);
    uint64_t hB = HashDocContent(docB);
    bool distinct = (hA != hB);
    bool tokA = expectedTokenA.empty() || DocContainsToken(docA, expectedTokenA);
    bool tokB = expectedTokenB.empty() || DocContainsToken(docB, expectedTokenB);

    bool allOk = distinct && tokA && tokB && badRejected;
    std::wstring detail;
    if (!distinct) detail += L"hashA==hashB; ";
    if (!tokA) detail += L"missing tokenA '" + std::wstring(expectedTokenA) + L"'; ";
    if (!tokB) detail += L"missing tokenB '" + std::wstring(expectedTokenB) + L"'; ";
    if (!badRejected) detail += L"corrupt input was not rejected; ";
    tc.Record(allOk, testLabel, detail);
}

// ----------------------------------------------------------------------------
// Format Builders
// ----------------------------------------------------------------------------

// 1. ELF64 with .shstrtab, .dynstr, .dynsym, .dynamic (DT_NEEDED), PT_INTERP
static std::vector<uint8_t> MakeElf64(std::string_view neededSo, std::string_view symFuncName, size_t padBeforeTables = 0) {
    std::string shstr;
    shstr.push_back('\0');
    uint32_t shIdxStrtab = static_cast<uint32_t>(shstr.size()); shstr += ".shstrtab"; shstr.push_back('\0');
    uint32_t shIdxDynstr = static_cast<uint32_t>(shstr.size()); shstr += ".dynstr";   shstr.push_back('\0');
    uint32_t shIdxDynsym = static_cast<uint32_t>(shstr.size()); shstr += ".dynsym";   shstr.push_back('\0');
    uint32_t shIdxDyn    = static_cast<uint32_t>(shstr.size()); shstr += ".dynamic";  shstr.push_back('\0');

    std::string dynstr;
    dynstr.push_back('\0');
    uint32_t dsNeeded = static_cast<uint32_t>(dynstr.size()); dynstr.append(neededSo);    dynstr.push_back('\0');
    uint32_t dsSym    = static_cast<uint32_t>(dynstr.size()); dynstr.append(symFuncName); dynstr.push_back('\0');

    ByteBuf dynsym;
    dynsym.Zeros(24); // STN_UNDEF
    dynsym.U32LE(dsSym);
    dynsym.U8(0x12);  // STB_GLOBAL | STT_FUNC
    dynsym.U8(0);
    dynsym.U16LE(1);
    dynsym.U64LE(0x401200);
    dynsym.U64LE(64);

    ByteBuf dynamicSec;
    dynamicSec.U64LE(1); dynamicSec.U64LE(dsNeeded); // DT_NEEDED
    dynamicSec.U64LE(0); dynamicSec.U64LE(0);        // DT_NULL

    std::string interp = "/lib64/ld-linux-x86-64.so.2";
    interp.push_back('\0');

    ByteBuf f;
    // ELF64 header (64B)
    f.Bytes("\x7F" "ELF\x02\x01\x01\x00", 8);
    f.Zeros(8);
    f.U16LE(3);      // ET_DYN
    f.U16LE(62);     // EM_X86_64
    f.U32LE(1);      // EV_CURRENT
    f.U64LE(0x401000);
    f.U64LE(64);     // e_phoff
    size_t shoffPatch = f.b.size();
    f.U64LE(0);      // e_shoff (patched below)
    f.U32LE(0);
    f.U16LE(64);
    f.U16LE(56);     // e_phentsize
    f.U16LE(1);      // e_phnum
    f.U16LE(64);     // e_shentsize
    f.U16LE(5);      // e_shnum
    f.U16LE(1);      // e_shstrndx

    // 1 Program Header: PT_INTERP (56B at offset 64)
    uint64_t interpOff = 120;
    f.U32LE(3);      // PT_INTERP
    f.U32LE(4);      // PF_R
    f.U64LE(interpOff);
    f.U64LE(0x400200);
    f.U64LE(0x400200);
    f.U64LE(interp.size());
    f.U64LE(interp.size());
    f.U64LE(1);

    f.Str(interp);
    f.PadTo((f.b.size() + 7) & ~size_t(7));
    f.Zeros(padBeforeTables & ~size_t(7));

    uint64_t offShstr = f.b.size(); f.Str(shstr);
    uint64_t offDynstr = f.b.size(); f.Str(dynstr);
    f.PadTo((f.b.size() + 7) & ~size_t(7));
    uint64_t offDynsym = f.b.size(); f.Bytes(dynsym.b.data(), dynsym.b.size());
    uint64_t offDyn = f.b.size();    f.Bytes(dynamicSec.b.data(), dynamicSec.b.size());
    uint64_t offShdr = f.b.size();
    PutU32LE(f.b.data() + shoffPatch, static_cast<uint32_t>(offShdr));

    auto writeShdr = [&](uint32_t name, uint32_t type, uint64_t off, uint64_t sz, uint32_t link, uint64_t entsz) {
        f.U32LE(name);
        f.U32LE(type);
        f.U64LE(0);
        f.U64LE(0);
        f.U64LE(off);
        f.U64LE(sz);
        f.U32LE(link);
        f.U32LE(0);
        f.U64LE(8);
        f.U64LE(entsz);
    };
    writeShdr(0, 0, 0, 0, 0, 0);
    writeShdr(shIdxStrtab, 3, offShstr, shstr.size(), 0, 0);
    writeShdr(shIdxDynstr, 3, offDynstr, dynstr.size(), 0, 0);
    writeShdr(shIdxDynsym, 11, offDynsym, dynsym.b.size(), 2, 24);
    writeShdr(shIdxDyn,    6, offDyn, dynamicSec.b.size(), 2, 16);
    return f.b;
}

// 2. WebAssembly (.wasm)
static std::vector<uint8_t> MakeWasmModule(std::string_view impMod, std::string_view impName, std::string_view expName, uint8_t valType) {
    auto makeSec = [](uint8_t id, const ByteBuf& pay, ByteBuf& dst) {
        dst.U8(id);
        dst.Uleb(pay.b.size());
        dst.Bytes(pay.b.data(), pay.b.size());
    };
    ByteBuf w;
    w.Bytes("\x00" "asm\x01\x00\x00\x00", 8);

    // Type section (id=1): 2 types: ()->() for import, (valType)->(valType) for exported func
    ByteBuf tSec;
    tSec.Uleb(2);
    tSec.U8(0x60); tSec.Uleb(0); tSec.Uleb(0);
    tSec.U8(0x60); tSec.Uleb(1); tSec.U8(valType); tSec.Uleb(1); tSec.U8(valType);
    makeSec(1, tSec, w);

    // Import section (id=2)
    ByteBuf iSec;
    iSec.Uleb(1);
    iSec.Uleb(impMod.size());  iSec.Str(impMod);
    iSec.Uleb(impName.size()); iSec.Str(impName);
    iSec.U8(0x00); // func import
    iSec.Uleb(0);  // typeidx 0
    makeSec(2, iSec, w);

    // Function section (id=3): 1 defined func -> typeidx 1
    ByteBuf fSec;
    fSec.Uleb(1);
    fSec.Uleb(1);
    makeSec(3, fSec, w);

    // Export section (id=7): export funcidx 1
    ByteBuf eSec;
    eSec.Uleb(1);
    eSec.Uleb(expName.size()); eSec.Str(expName);
    eSec.U8(0x00);
    eSec.Uleb(1);
    makeSec(7, eSec, w);

    // Code section (id=10): local.get 0, end
    ByteBuf cSec;
    cSec.Uleb(1);
    ByteBuf body;
    body.Uleb(0); // 0 local decls
    body.U8(0x20); body.U8(0x00); body.U8(0x0B);
    cSec.Uleb(body.b.size());
    cSec.Bytes(body.b.data(), body.b.size());
    makeSec(10, cSec, w);
    return w.b;
}

// 3. MP3 with ID3v2.3 + MPEG-1 Layer III Frames
static std::vector<uint8_t> MakeMp3(std::wstring_view title, std::string_view artist, uint32_t mpegHdr, size_t frameLen) {
    ByteBuf frames;
    auto addTextUtf8 = [&](const char* id, std::string_view val) {
        frames.Bytes(id, 4);
        frames.U32BE(static_cast<uint32_t>(1 + val.size()));
        frames.U16BE(0);
        frames.U8(3); // UTF-8
        frames.Str(val);
    };
    // TIT2 in UTF-16LE with BOM (encoding 1)
    frames.Bytes("TIT2", 4);
    frames.U32BE(static_cast<uint32_t>(3 + title.size() * 2));
    frames.U16BE(0);
    frames.U8(1);
    frames.U8(0xFF); frames.U8(0xFE);
    frames.Utf16LE(title, false);

    addTextUtf8("TPE1", artist);

    ByteBuf m;
    m.Bytes("ID3\x03\x00\x00", 6);
    uint32_t sz = static_cast<uint32_t>(frames.b.size());
    m.U8(static_cast<uint8_t>((sz >> 21) & 0x7Fu));
    m.U8(static_cast<uint8_t>((sz >> 14) & 0x7Fu));
    m.U8(static_cast<uint8_t>((sz >> 7) & 0x7Fu));
    m.U8(static_cast<uint8_t>(sz & 0x7Fu));
    m.Bytes(frames.b.data(), frames.b.size());

    // Two consecutive valid MPEG-1 Layer III frames
    for (int f = 0; f < 2; ++f) {
        size_t start = m.b.size();
        m.U32BE(mpegHdr);
        if (f == 0) {
            m.Zeros(32);
            m.Bytes("Xing", 4);
            m.U32BE(1);     // frames flag
            m.U32BE(4600);  // 4600 frames (~120s)
        }
        m.PadTo(start + frameLen);
    }
    return m.b;
}

// 4. Ogg Vorbis / Opus Container
static std::vector<uint8_t> MakeOggVorbis(uint32_t serial, uint32_t sampleRate, int64_t endGranule, std::string_view titleTag) {
    ByteBuf ogg;
    auto addPage = [&](uint8_t hdrType, uint64_t granule, uint32_t seq, const ByteBuf& pkt) {
        ogg.Bytes("OggS", 4);
        ogg.U8(0);
        ogg.U8(hdrType);
        ogg.U64LE(granule);
        ogg.U32LE(serial);
        ogg.U32LE(seq);
        ogg.U32LE(0); // CRC
        ogg.U8(1);    // 1 segment (<255B)
        ogg.U8(static_cast<uint8_t>(pkt.b.size()));
        ogg.Bytes(pkt.b.data(), pkt.b.size());
    };

    ByteBuf bos;
    bos.U8(0x01); bos.Str("vorbis");
    bos.U32LE(0);
    bos.U8(2); // stereo
    bos.U32LE(sampleRate);
    bos.U32LE(0);
    bos.U32LE(192000);
    bos.U32LE(0);
    bos.U8(0xB8);
    bos.U8(0x01);
    addPage(0x02, 0, 0, bos);

    ByteBuf cmt;
    cmt.U8(0x03); cmt.Str("vorbis");
    std::string vendor = "Xiph.Org libVorbis 1.3.7";
    cmt.U32LE(static_cast<uint32_t>(vendor.size())); cmt.Str(vendor);
    cmt.U32LE(1);
    std::string kv = "TITLE=" + std::string(titleTag);
    cmt.U32LE(static_cast<uint32_t>(kv.size())); cmt.Str(kv);
    cmt.U8(1);
    addPage(0x00, 0, 1, cmt);

    ByteBuf audio;
    audio.Zeros(32);
    addPage(0x04, static_cast<uint64_t>(endGranule), 2, audio);
    return ogg.b;
}

// 5. SoundFont 2 (.sf2)
static std::vector<uint8_t> MakeSf2(std::string_view bankName, std::string_view presetName, std::string_view sampleName, uint32_t sr) {
    ByteBuf info;
    info.Str("INFO");
    info.Str("ifil"); info.U32LE(4); info.U16LE(2); info.U16LE(1);
    std::string nm(bankName); nm.push_back('\0');
    if (nm.size() & 1u) nm.push_back('\0');
    info.Str("INAM"); info.U32LE(static_cast<uint32_t>(nm.size())); info.Str(nm);

    ByteBuf pdta;
    pdta.Str("pdta");
    // phdr: 2 * 38 = 76B
    pdta.Str("phdr"); pdta.U32LE(76);
    pdta.FixedStr(presetName, 20); pdta.U16LE(0); pdta.U16LE(0); pdta.Zeros(14);
    pdta.FixedStr("EOP", 20); pdta.Zeros(18);
    // inst: 2 * 22 = 44B
    pdta.Str("inst"); pdta.U32LE(44);
    pdta.FixedStr(presetName, 20); pdta.U16LE(0);
    pdta.FixedStr("EOI", 20); pdta.U16LE(0);
    // shdr: 2 * 46 = 92B
    pdta.Str("shdr"); pdta.U32LE(92);
    pdta.FixedStr(sampleName, 20); pdta.U32LE(0); pdta.U32LE(4096); pdta.U32LE(0); pdta.U32LE(0); pdta.U32LE(sr); pdta.U8(60); pdta.Zeros(5);
    pdta.FixedStr("EOS", 20); pdta.Zeros(26);

    ByteBuf riff;
    riff.Str("RIFF");
    uint32_t total = static_cast<uint32_t>(4 + 8 + info.b.size() + 8 + pdta.b.size());
    riff.U32LE(total);
    riff.Str("sfbk");
    riff.Str("LIST"); riff.U32LE(static_cast<uint32_t>(info.b.size())); riff.Bytes(info.b.data(), info.b.size());
    riff.Str("LIST"); riff.U32LE(static_cast<uint32_t>(pdta.b.size())); riff.Bytes(pdta.b.data(), pdta.b.size());
    return riff.b;
}

// 6. Outlook .msg (Valid 512B-sector OLE2 Compound File Binary)
static std::vector<uint8_t> MakeOutlookMsg(std::wstring_view subject, std::wstring_view sender, std::wstring_view body, std::wstring_view attachName,
                                           const std::vector<uint8_t>& rtfCompressedBody = {}) {
    struct StreamDef {
        std::wstring name;
        uint8_t objType;
        uint32_t childId;
        uint32_t rightSibling;
        std::vector<uint8_t> data;
    };
    auto utf16Vec = [](std::wstring_view w) {
        ByteBuf b; b.Utf16LE(w, false); return b.b;
    };

    std::vector<StreamDef> dirs = {
        { L"Root Entry",                    5, 1, 0xFFFFFFFFu, {} },
        { L"__substg1.0_0037001F",          2, 0xFFFFFFFFu, 2, utf16Vec(subject) },
        { L"__substg1.0_0C1A001F",          2, 0xFFFFFFFFu, 3, utf16Vec(sender) },
        { L"__substg1.0_1000001F",          2, 0xFFFFFFFFu, 4, utf16Vec(body) },
        { L"__attach_version1.0_#00000000", 1, 5, 0xFFFFFFFFu, {} },
        { L"__substg1.0_3707001F",          2, 0xFFFFFFFFu, 6, utf16Vec(attachName) },
        { L"__substg1.0_37010102",          2, 0xFFFFFFFFu, 0xFFFFFFFFu, { 0x25, 0x50, 0x44, 0x46, 0x2D, 0x31, 0x2E, 0x37 } }
    };
    if (!rtfCompressedBody.empty()) dirs[3] = { L"__substg1.0_10090102", 2, 0xFFFFFFFFu, 4, rtfCompressedBody };

    // Sector 0 = FAT, Sectors 1..2 = Directory (8 entries), Sectors 3.. = stream data
    ByteBuf cfb;
    cfb.Bytes("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
    cfb.Zeros(16);
    cfb.U16LE(0x003E); // minor
    cfb.U16LE(0x0003); // major v3 (512B sectors)
    cfb.U16LE(0xFFFE); // little-endian
    cfb.U16LE(9);      // sectorShift = 9 (512)
    cfb.U16LE(6);      // miniShift = 6 (64)
    cfb.Zeros(6);
    cfb.U32LE(0);      // numDirSecs (0 for v3)
    cfb.U32LE(1);      // numFatSecs = 1
    cfb.U32LE(1);      // firstDirSec = 1
    cfb.U32LE(0);
    cfb.U32LE(4096);   // miniCutoff (Root Entry size=0 => miniStream empty => regular sector fallback)
    cfb.U32LE(0xFFFFFFFEu); // firstMiniFat
    cfb.U32LE(0);
    cfb.U32LE(0xFFFFFFFEu); // firstDifat
    cfb.U32LE(0);
    cfb.U32LE(0);      // DIFAT[0] = sector 0
    for (int i = 1; i < 109; ++i) cfb.U32LE(0xFFFFFFFFu);

    // Reserve FAT (sector 0) & Directory (sectors 1..2)
    cfb.Zeros(512 * 3);
    uint32_t nextSec = 3;
    std::vector<uint32_t> fat(128, 0xFFFFFFFFu);
    fat[0] = 0xFFFFFFFDu; // FATSECT
    fat[1] = 2;
    fat[2] = 0xFFFFFFFEu; // ENDOFCHAIN

    for (size_t i = 0; i < dirs.size(); ++i) {
        uint8_t* ent = cfb.b.data() + 1024 + i * 128;
        const auto& d = dirs[i];
        for (size_t c = 0; c < d.name.size() && c < 31; ++c) {
            PutU16LE(ent + c * 2, static_cast<uint16_t>(d.name[c]));
        }
        PutU16LE(ent + 64, static_cast<uint16_t>((d.name.size() + 1) * 2));
        ent[66] = d.objType;
        ent[67] = 1; // black
        PutU32LE(ent + 68, 0xFFFFFFFFu);
        PutU32LE(ent + 72, d.rightSibling);
        PutU32LE(ent + 76, d.childId);
        if (d.objType == 2 && !d.data.empty()) {
            uint32_t startS = nextSec++;
            fat[startS] = 0xFFFFFFFEu;
            PutU32LE(ent + 116, startS);
            PutU32LE(ent + 120, static_cast<uint32_t>(d.data.size()));
            cfb.Bytes(d.data.data(), d.data.size());
            size_t rem = d.data.size() % 512;
            if (rem != 0) cfb.Zeros(512 - rem);
        } else {
            PutU32LE(ent + 116, 0xFFFFFFFEu);
            PutU32LE(ent + 120, 0);
        }
    }

    for (size_t i = 0; i < 128; ++i) {
        PutU32LE(cfb.b.data() + 512 + i * 4, fat[i]);
    }
    return cfb.b;
}

// 7. Mobipocket (.mobi) with Real PalmDOC LZ77 Compression (space-pairs + back-references)
static std::vector<uint8_t> CompressPalmDocLz77(std::string_view src) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < src.size()) {
        // Try LZ77 back-reference (len 3..10, dist 1..2047)
        size_t bestLen = 0, bestDist = 0;
        size_t maxBack = (i < 2047) ? i : 2047;
        for (size_t dist = 1; dist <= maxBack; ++dist) {
            size_t m = 0;
            while (m < 10 && i + m < src.size() && src[i + m] == src[i - dist + m]) ++m;
            if (m >= 3 && m > bestLen) {
                bestLen = m;
                bestDist = dist;
                if (bestLen == 10) break;
            }
        }
        if (bestLen >= 3) {
            uint16_t pair = static_cast<uint16_t>(0x8000u | (bestDist << 3) | (bestLen - 3));
            out.push_back(static_cast<uint8_t>((pair >> 8) & 0xFFu));
            out.push_back(static_cast<uint8_t>(pair & 0xFFu));
            i += bestLen;
            continue;
        }
        // Try ' ' + printable ASCII pair (0xC0..0xFE)
        if (src[i] == ' ' && i + 1 < src.size()) {
            uint8_t nx = static_cast<uint8_t>(src[i + 1]);
            if (nx >= 0x40 && nx <= 0x7E) {
                out.push_back(static_cast<uint8_t>(nx ^ 0x80u));
                i += 2;
                continue;
            }
        }
        uint8_t c = static_cast<uint8_t>(src[i++]);
        if (c == 0 || (c >= 9 && c <= 0x7F)) {
            out.push_back(c);
        } else {
            out.push_back(1);
            out.push_back(c);
        }
    }
    return out;
}

static std::vector<uint8_t> MakeMobiBook(std::string_view title, std::string_view author, std::string_view htmlBody) {
    std::vector<uint8_t> lzText = CompressPalmDocLz77(htmlBody);

    // Record 0: 16B PalmDOC + 232B MOBI + EXTH + FullName
    ByteBuf exth;
    exth.Str("EXTH");
    uint32_t r1Len = static_cast<uint32_t>(8 + author.size());
    uint32_t r2Len = static_cast<uint32_t>(8 + title.size());
    uint32_t exthTotal = 12 + r1Len + r2Len;
    while (exthTotal & 3u) ++exthTotal;
    exth.U32BE(exthTotal);
    exth.U32BE(2); // 2 EXTH records
    exth.U32BE(100); exth.U32BE(r1Len); exth.Str(author);
    exth.U32BE(503); exth.U32BE(r2Len); exth.Str(title);
    exth.PadTo(exthTotal);

    ByteBuf rec0;
    // PalmDOC 16B
    rec0.U16BE(2); // PalmDOC LZ77
    rec0.U16BE(0);
    rec0.U32BE(static_cast<uint32_t>(htmlBody.size()));
    rec0.U16BE(1); // 1 text record
    rec0.U16BE(4096);
    rec0.U16BE(0); // unencrypted
    rec0.U16BE(0);

    // MOBI header (232B)
    uint32_t mobiHdrLen = 232;
    uint32_t fnOff = static_cast<uint32_t>(16 + mobiHdrLen + exth.b.size());
    rec0.Str("MOBI");
    rec0.U32BE(mobiHdrLen);
    rec0.U32BE(2);     // book
    rec0.U32BE(65001); // UTF-8
    rec0.U32BE(12345);
    rec0.U32BE(6);     // MOBI v6
    rec0.PadTo(84);
    rec0.U32BE(fnOff);
    rec0.U32BE(static_cast<uint32_t>(title.size()));
    rec0.PadTo(128);
    rec0.U32BE(0x40);  // EXTH flag present
    rec0.PadTo(16 + mobiHdrLen);
    rec0.Bytes(exth.b.data(), exth.b.size());
    rec0.Str(title);
    rec0.Zeros(4);

    // PalmDB 78B + 2*8B = 94B
    ByteBuf pdb;
    pdb.FixedStr(title, 32);
    pdb.Zeros(28);
    pdb.Str("BOOKMOBI");
    pdb.Zeros(8);
    pdb.U16BE(2); // 2 records
    uint32_t off0 = 78 + 16;
    uint32_t off1 = off0 + static_cast<uint32_t>(rec0.b.size());
    pdb.U32BE(off0); pdb.U32BE(0);
    pdb.U32BE(off1); pdb.U32BE(1);
    pdb.Bytes(rec0.b.data(), rec0.b.size());
    pdb.Bytes(lzText.data(), lzText.size());
    return pdb.b;
}

// 8. CHM (ITSF v3 + ITSP v1 + PMGL with ENCINT entries + Section 0 preview)
static std::vector<uint8_t> MakeChm(std::string_view topicPath, std::string_view htmlSnippet) {
    ByteBuf pmglEntries;
    auto addEntry = [&](std::string_view path, uint64_t sec, uint64_t off, uint64_t len) {
        pmglEntries.EncInt(path.size());
        pmglEntries.Str(path);
        pmglEntries.EncInt(sec);
        pmglEntries.EncInt(off);
        pmglEntries.EncInt(len);
    };
    addEntry(topicPath, 0, 0, htmlSnippet.size());
    addEntry("/#SYSTEM", 0, htmlSnippet.size(), 4);

    const uint32_t blkSize = 512;
    ByteBuf pmgl;
    pmgl.Str("PMGL");
    pmgl.U32LE(blkSize - 20 - static_cast<uint32_t>(pmglEntries.b.size())); // freeSpace
    pmgl.U32LE(0);
    pmgl.U32LE(0xFFFFFFFFu);
    pmgl.U32LE(0xFFFFFFFFu);
    pmgl.Bytes(pmglEntries.b.data(), pmglEntries.b.size());
    pmgl.PadTo(blkSize);

    ByteBuf itsp;
    itsp.Str("ITSP");
    itsp.U32LE(1);       // version
    itsp.U32LE(84);      // hdrLen
    itsp.U32LE(0x0A);
    itsp.U32LE(blkSize); // dirBlockSize
    itsp.U32LE(2);
    itsp.U32LE(1);       // indexDepth
    itsp.U32LE(0xFFFFFFFFu);
    itsp.U32LE(0);
    itsp.U32LE(0);
    itsp.U32LE(0xFFFFFFFFu);
    itsp.U32LE(1);       // numDirBlocks
    itsp.PadTo(84);

    uint64_t dirOff = 96;
    uint64_t dirLen = itsp.b.size() + pmgl.b.size();
    uint64_t dataOff = dirOff + dirLen;

    ByteBuf chm;
    chm.Str("ITSF");
    chm.U32LE(3);        // ITSF v3
    chm.U32LE(96);       // hdrLen
    chm.U32LE(1);
    chm.U32LE(0);
    chm.U32LE(0x0409);   // en-US
    chm.PadTo(56);
    chm.U64LE(dataOff);
    chm.U64LE(htmlSnippet.size() + 4);
    chm.U64LE(dirOff);
    chm.U64LE(dirLen);
    chm.U64LE(dataOff);
    chm.Bytes(itsp.b.data(), itsp.b.size());
    chm.Bytes(pmgl.b.data(), pmgl.b.size());
    chm.Str(htmlSnippet);
    chm.Zeros(4);
    return chm.b;
}

// 8b. CHM whose topics are in section 1: an LZX stream (32 KB window) of a verbatim block that holds only literals
// (main tree: every byte value an 8-bit code, sent through the pretree as deltas) and then a stored block, plus the
// LZXC control data and the reset table that describe it
struct LzxBitWriter { // 16-bit little-endian words, filled from the most significant bit
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int n = 0;
    void Put(uint32_t v, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            acc = (acc << 1) | ((v >> i) & 1u);
            if (++n == 16) {
                out.push_back(static_cast<uint8_t>(acc));
                out.push_back(static_cast<uint8_t>(acc >> 8));
                acc = 0;
                n = 0;
            }
        }
    }
};

static std::vector<uint8_t> MakeChmLzx(std::string_view pageA, std::string_view pageB) {
    LzxBitWriter w;
    // pretree: symbol 9 ("0"), 17 ("10"), 18 ("11"); 9 turns a length of 0 into 8, 18 writes 20..51 zeros
    auto pretree = [&]() {
        for (int s = 0; s < 20; ++s) w.Put(s == 9 ? 1u : (s == 17 || s == 18) ? 2u : 0u, 4);
    };
    auto zeros = [&](uint32_t count) {
        while (count >= 20) {
            const uint32_t run = std::min<uint32_t>(count, 51);
            w.Put(3, 2); // symbol 18
            w.Put(run - 20, 5);
            count -= run;
        }
        if (count > 0) { // 4..19 left: symbol 17 (a last run under 4 cannot happen with these counts)
            w.Put(2, 2);
            w.Put(count - 4, 4);
        }
    };
    w.Put(0, 1);                                     // no E8 translation
    w.Put(1, 3);                                     // verbatim block
    w.Put(static_cast<uint32_t>(pageA.size()) >> 8, 16);
    w.Put(static_cast<uint32_t>(pageA.size()) & 0xFF, 8);
    pretree();
    for (int s = 0; s < 256; ++s) w.Put(0, 1);       // 256 x symbol 9: every literal 8 bits long
    pretree();
    zeros(30 * 8);                                   // no match symbols (30 position slots for a 32 KB window)
    pretree();
    zeros(249);                                      // empty length tree
    for (unsigned char c : pageA) w.Put(c, 8);       // canonical 8-bit codes: a byte's code is the byte
    w.Put(3, 3);                                     // stored block
    w.Put(static_cast<uint32_t>(pageB.size()) >> 8, 16);
    w.Put(static_cast<uint32_t>(pageB.size()) & 0xFF, 8);
    w.Put(0, w.n == 0 ? 16 : 16 - w.n);              // 1..16 bits to the next word
    std::vector<uint8_t> stream = w.out;
    for (int r = 0; r < 3; ++r) { stream.push_back(1); stream.push_back(0); stream.push_back(0); stream.push_back(0); } // R0..R2
    stream.insert(stream.end(), pageB.begin(), pageB.end());
    if (pageB.size() & 1) stream.push_back(0);

    ByteBuf ctl; // LZXC v2: reset every 2 frames, 32 KB window
    ctl.U32LE(6); ctl.Str("LZXC"); ctl.U32LE(2); ctl.U32LE(2); ctl.U32LE(1); ctl.U32LE(1); ctl.U32LE(0);
    ByteBuf rt;  // reset table: one frame at offset 0
    rt.U32LE(2); rt.U32LE(1); rt.U32LE(8); rt.U32LE(40);
    rt.U64LE(pageA.size() + pageB.size()); rt.U64LE(stream.size()); rt.U64LE(0x8000); rt.U64LE(0);

    const std::string ctlPath = "::DataSpace/Storage/MSCompressed/ControlData";
    const std::string rtPath = "::DataSpace/Storage/MSCompressed/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable";
    const std::string contPath = "::DataSpace/Storage/MSCompressed/Content";
    ByteBuf entries;
    auto addEntry = [&](std::string_view path, uint64_t sec, uint64_t off, uint64_t len) {
        entries.EncInt(path.size());
        entries.Str(path);
        entries.EncInt(sec);
        entries.EncInt(off);
        entries.EncInt(len);
    };
    addEntry(ctlPath, 0, 0, ctl.b.size());
    addEntry(rtPath, 0, ctl.b.size(), rt.b.size());
    addEntry(contPath, 0, ctl.b.size() + rt.b.size(), stream.size());
    addEntry("/intro.htm", 1, 0, pageA.size());
    addEntry("/details.htm", 1, pageA.size(), pageB.size());

    const uint32_t blkSize = 4096;
    ByteBuf pmgl;
    pmgl.Str("PMGL");
    pmgl.U32LE(blkSize - 20 - static_cast<uint32_t>(entries.b.size()));
    pmgl.U32LE(0); pmgl.U32LE(0xFFFFFFFFu); pmgl.U32LE(0xFFFFFFFFu);
    pmgl.Bytes(entries.b.data(), entries.b.size());
    pmgl.PadTo(blkSize);
    ByteBuf itsp;
    itsp.Str("ITSP"); itsp.U32LE(1); itsp.U32LE(84); itsp.U32LE(0x0A); itsp.U32LE(blkSize); itsp.U32LE(2); itsp.U32LE(1);
    itsp.U32LE(0xFFFFFFFFu); itsp.U32LE(0); itsp.U32LE(0); itsp.U32LE(0xFFFFFFFFu); itsp.U32LE(1);
    itsp.PadTo(84);
    const uint64_t dirOff = 96, dirLen = itsp.b.size() + pmgl.b.size(), dataOff = dirOff + dirLen;
    ByteBuf chm;
    chm.Str("ITSF"); chm.U32LE(3); chm.U32LE(96); chm.U32LE(1); chm.U32LE(0); chm.U32LE(0x0409);
    chm.PadTo(56);
    chm.U64LE(dataOff); chm.U64LE(ctl.b.size() + rt.b.size() + stream.size()); chm.U64LE(dirOff); chm.U64LE(dirLen); chm.U64LE(dataOff);
    chm.Bytes(itsp.b.data(), itsp.b.size());
    chm.Bytes(pmgl.b.data(), pmgl.b.size());
    chm.Bytes(ctl.b.data(), ctl.b.size());
    chm.Bytes(rt.b.data(), rt.b.size());
    chm.Bytes(stream.data(), stream.size());
    return chm.b;
}

// 9. PE32+ (.dll) with Real Import & Export Tables
static std::vector<uint8_t> MakePe64WithImpExp(std::string_view importDll, std::string_view importFunc, std::string_view exportFunc) {
    ByteBuf pe;
    // DOS Header (64B)
    pe.Str("MZ");
    pe.PadTo(0x3C);
    pe.U32LE(0x80);
    pe.PadTo(0x80);

    // PE Signature + COFF Header (24B)
    pe.Bytes("PE\0\0", 4);
    pe.U16LE(0x8664); // AMD64
    pe.U16LE(1);      // 1 section (.rdata)
    pe.U32LE(0x65001122u);
    pe.U32LE(0);
    pe.U32LE(0);
    pe.U16LE(240);    // SizeOfOptionalHeader (PE32+)
    pe.U16LE(0x2022); // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE | DLL

    // Optional Header PE32+ (240B at 0x98, DataDirectories at +112 = 0x108)
    size_t optStart = pe.b.size();
    pe.U16LE(0x020B); // PE32+
    pe.PadTo(optStart + 56);
    pe.U32LE(0x3000); // SizeOfImage
    pe.U32LE(0x200);  // SizeOfHeaders
    pe.PadTo(optStart + 108);
    pe.U32LE(16);     // NumberOfRvaAndSizes

    // Build .rdata at RVA 0x1000, file offset 0x200:
    // +0x000: Export Directory (40B)
    // +0x028: Export Address Table (4B), NamePtrTable (4B), OrdinalTable (2B)
    // +0x040: Import Descriptor [0] (20B) + Null Descriptor [1] (20B)
    // +0x068: ILT 64-bit [0] (8B) + Null [1] (8B)
    // +0x080: Strings & Hint/Name
    ByteBuf rdata;
    rdata.Zeros(0x80);
    auto addRStr = [&](std::string_view s) -> uint32_t {
        uint32_t rva = 0x1000u + static_cast<uint32_t>(rdata.b.size());
        rdata.Str(s);
        rdata.U8(0);
        if (rdata.b.size() & 1u) rdata.U8(0);
        return rva;
    };
    uint32_t rvaDllMod  = addRStr("testmod.dll");
    uint32_t rvaExpName = addRStr(exportFunc);
    uint32_t rvaImpDll  = addRStr(importDll);
    uint32_t rvaHintNm  = 0x1000u + static_cast<uint32_t>(rdata.b.size());
    rdata.U16LE(7); // Hint
    rdata.Str(importFunc);
    rdata.U8(0);
    rdata.PadTo(0x200);

    // Fill Export Directory at rdata+0x00
    PutU32LE(rdata.b.data() + 12, rvaDllMod);
    PutU32LE(rdata.b.data() + 16, 1); // Base ordinal
    PutU32LE(rdata.b.data() + 20, 1); // NumberOfFunctions
    PutU32LE(rdata.b.data() + 24, 1); // NumberOfNames
    PutU32LE(rdata.b.data() + 28, 0x1028); // AddressOfFunctions
    PutU32LE(rdata.b.data() + 32, 0x102C); // AddressOfNames
    PutU32LE(rdata.b.data() + 36, 0x1030); // AddressOfNameOrdinals
    PutU32LE(rdata.b.data() + 0x28, 0x1150);
    PutU32LE(rdata.b.data() + 0x2C, rvaExpName);
    PutU16LE(rdata.b.data() + 0x30, 0);

    // Fill Import Descriptor at rdata+0x40
    PutU32LE(rdata.b.data() + 0x40 + 0,  0x1068); // OriginalFirstThunk
    PutU32LE(rdata.b.data() + 0x40 + 12, rvaImpDll);
    PutU32LE(rdata.b.data() + 0x40 + 16, 0x1068); // FirstThunk
    PutU32LE(rdata.b.data() + 0x68, rvaHintNm);   // 64-bit Thunk RVA

    // DataDirectory[0] = Export (RVA 0x1000, size 0x34), DataDirectory[1] = Import (RVA 0x1040, size 40)
    pe.U32LE(0x1000); pe.U32LE(0x34);
    pe.U32LE(0x1040); pe.U32LE(40);
    pe.PadTo(optStart + 240);

    // Section Header (.rdata, 40B)
    pe.FixedStr(".rdata", 8);
    pe.U32LE(static_cast<uint32_t>(rdata.b.size())); // VirtualSize
    pe.U32LE(0x1000); // VirtualAddress
    pe.U32LE(static_cast<uint32_t>(rdata.b.size())); // SizeOfRawData
    pe.U32LE(0x200);  // PointerToRawData
    pe.Zeros(12);
    pe.U32LE(0x40000040u); // INITIALIZED_DATA | MEM_READ

    pe.PadTo(0x200);
    pe.Bytes(rdata.b.data(), rdata.b.size());
    return pe.b;
}

// 10. Windows Shortcut (.lnk MS-SHLLINK)
static std::vector<uint8_t> MakeShellLink(std::string_view localTarget, std::wstring_view cmdArgs) {
    ByteBuf lnk;
    lnk.U32LE(0x0000004Cu);
    static const uint8_t kClsid[16] = { 0x01,0x14,0x02,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 };
    lnk.Bytes(kClsid, 16);
    lnk.U32LE(0x02u | 0x20u | 0x80u); // HasLinkInfo | HasArguments | IsUnicode
    lnk.U32LE(0x20u); // FILE_ATTRIBUTE_ARCHIVE
    lnk.U64LE(133500000000000000ULL);
    lnk.U64LE(133500000000000000ULL);
    lnk.U64LE(133500000000000000ULL);
    lnk.U32LE(65536);
    lnk.U32LE(0);
    lnk.U32LE(1); // SW_SHOWNORMAL
    lnk.PadTo(76);

    // LinkInfo structure (VolumeIDAndLocalBasePath = 1)
    ByteBuf li;
    uint32_t volOff = 28;
    uint32_t baseOff = volOff + 17;
    uint32_t liSize = baseOff + static_cast<uint32_t>(localTarget.size()) + 2;
    li.U32LE(liSize);
    li.U32LE(28);     // LinkInfoHeaderSize
    li.U32LE(1);      // VolumeIDAndLocalBasePath
    li.U32LE(volOff);
    li.U32LE(baseOff);
    li.U32LE(0);
    li.U32LE(0);
    // Minimal VolumeID (17B)
    li.U32LE(17); li.U32LE(3); li.U32LE(0x12345678u); li.U32LE(16); li.U8(0);
    li.Str(localTarget);
    li.U8(0);
    li.U8(0);
    lnk.Bytes(li.b.data(), li.b.size());

    // StringData: COMMAND_LINE_ARGUMENTS (CountCharacters + UTF-16LE)
    lnk.U16LE(static_cast<uint16_t>(cmdArgs.size()));
    lnk.Utf16LE(cmdArgs, false);
    lnk.U32LE(0); // Terminal ExtraData block
    return lnk.b;
}

// 11. Windows Prefetch (.pf SCCA v30)
static std::vector<uint8_t> MakePrefetchV30(std::wstring_view exeName, uint32_t hash, uint32_t runCount, std::wstring_view refDll) {
    ByteBuf fnStrings;
    fnStrings.Utf16LE(refDll, true);

    ByteBuf pf;
    pf.U32LE(30); // Win10/11 v30
    pf.Str("SCCA");
    pf.U32LE(0x11);
    pf.U32LE(0);  // patched file size
    size_t nmStart = pf.b.size();
    pf.Utf16LE(exeName, true);
    pf.PadTo(nmStart + 60);
    pf.U32LE(hash);
    pf.U32LE(0);

    // Section offsets @ 84..151 (304B total header)
    uint32_t fnOff = 304;
    uint32_t fnSz  = static_cast<uint32_t>(fnStrings.b.size());
    pf.PadTo(84);
    pf.U32LE(304); pf.U32LE(0); // metrics
    pf.U32LE(304); pf.U32LE(0); // trace chains
    pf.U32LE(fnOff); pf.U32LE(fnSz);
    pf.U32LE(fnOff + fnSz); pf.U32LE(0);
    pf.PadTo(128);
    pf.U64LE(133600000000000000ULL); // Last run FILETIME
    pf.PadTo(208);
    pf.U32LE(runCount);
    pf.PadTo(304);
    pf.Bytes(fnStrings.b.data(), fnStrings.b.size());
    PutU32LE(pf.b.data() + 12, static_cast<uint32_t>(pf.b.size()));
    return pf.b;
}

// 12. Windows Event Log (.evtx: 4096B ElfFile + 65536B ElfChnk + 0x2A2A structured BinXML record)
static std::vector<uint8_t> MakeEvtx(uint64_t recordId, std::wstring_view providerMsg) {
    ByteBuf ev;
    ev.Bytes("ElfFile\0", 8);
    ev.U64LE(0);
    ev.U64LE(0);
    ev.U64LE(recordId + 1);
    ev.U32LE(128);
    ev.U16LE(1);
    ev.U16LE(3);
    ev.U16LE(4096);
    ev.U16LE(1); // chunkCount = 1
    ev.PadTo(4096);

    const size_t chnkBase = ev.b.size();
    ev.b.resize(chnkBase + 65536, 0u);
    std::memcpy(ev.b.data() + chnkBase, "ElfChnk\0", 8);
    PutU64LE(ev.b.data() + chnkBase + 8,  1);
    PutU64LE(ev.b.data() + chnkBase + 16, recordId);
    PutU64LE(ev.b.data() + chnkBase + 24, 1);
    PutU64LE(ev.b.data() + chnkBase + 32, recordId);
    PutU32LE(ev.b.data() + chnkBase + 40, 128); // headerSize

    // Emit chunk-resident BinXML Name records starting at chunk offset 0x2000
    uint32_t nameCur = 0x2000u;
    auto putChunkName = [&](std::wstring_view nm) -> uint32_t {
        const uint32_t off = nameCur;
        uint8_t* dst = ev.b.data() + chnkBase + off;
        PutU32LE(dst + 0, 0u);
        PutU16LE(dst + 4, 0x1234u);
        PutU16LE(dst + 6, static_cast<uint16_t>(nm.size()));
        for (size_t i = 0; i < nm.size(); ++i) {
            PutU16LE(dst + 8 + i * 2, static_cast<uint16_t>(nm[i]));
        }
        PutU16LE(dst + 8 + nm.size() * 2, 0u);
        const uint32_t recSz = static_cast<uint32_t>(8 + (nm.size() + 1) * 2);
        nameCur += (recSz + 3u) & ~3u;
        return off;
    };
    const uint32_t nEvent    = putChunkName(L"Event");
    const uint32_t nSystem   = putChunkName(L"System");
    const uint32_t nProvider = putChunkName(L"Provider");
    const uint32_t nNameAttr = putChunkName(L"Name");
    const uint32_t nEventId  = putChunkName(L"EventID");

    auto openElem = [](ByteBuf& bx, uint32_t nameOff, bool hasAttrs) {
        bx.U8(hasAttrs ? 0x41u : 0x01u);
        bx.U16LE(0xFFFFu);
        bx.U32LE(6u);
        bx.U32LE(nameOff);
    };

    ByteBuf provAttr;
    provAttr.U8(0x06u); // AttributeToken
    provAttr.U32LE(nNameAttr);
    provAttr.U8(0x05u); // ValueTextToken
    provAttr.U8(0x01u); // StringType UTF-16LE
    provAttr.U16LE(static_cast<uint16_t>(providerMsg.size()));
    provAttr.Utf16LE(providerMsg, false);

    std::wstring idText = std::to_wstring(recordId);
    ByteBuf binXml;
    binXml.Bytes("\x0F\x01\x01\x00", 4); // FragmentHeader
    openElem(binXml, nEvent, false);    binXml.U8(0x02u);
    openElem(binXml, nSystem, false);   binXml.U8(0x02u);
    openElem(binXml, nProvider, true);  binXml.U32LE(static_cast<uint32_t>(provAttr.b.size()));
    binXml.Bytes(provAttr.b.data(), provAttr.b.size());
    binXml.U8(0x03u); // CloseEmptyElement (<Provider .../>)
    openElem(binXml, nEventId, false);  binXml.U8(0x02u);
    binXml.U8(0x05u); binXml.U8(0x01u); binXml.U16LE(static_cast<uint16_t>(idText.size()));
    binXml.Utf16LE(idText, false);
    binXml.U8(0x04u); // </EventID>
    binXml.U8(0x04u); // </System>
    binXml.U8(0x04u); // </Event>
    binXml.U8(0x00u); // EOF token

    uint32_t recLen = static_cast<uint32_t>(24 + binXml.b.size() + 4);
    if (recLen & 3u) recLen += 4u - (recLen & 3u);
    uint8_t* recPtr = ev.b.data() + chnkBase + 512;
    PutU32LE(recPtr + 0,  0x00002A2Au);
    PutU32LE(recPtr + 4,  recLen);
    PutU64LE(recPtr + 8,  recordId);
    PutU64LE(recPtr + 16, 133610000000000000ULL);
    std::memcpy(recPtr + 24, binXml.b.data(), binXml.b.size());
    PutU32LE(recPtr + recLen - 4, recLen);

    PutU32LE(ev.b.data() + chnkBase + 44, 512u);          // lastEventRecOff
    PutU32LE(ev.b.data() + chnkBase + 48, 512u + recLen); // freeSpaceOff
    return ev.b;
}

// 13. PDB v7.00 (MSF 7.00 with Stream 1 PDB GUID + Stream 3 DBI Module)
static std::vector<uint8_t> MakePdbMsf7(uint8_t guidLastByte, std::string_view objPath, uint32_t firstBlock = 3) {
    const uint32_t blkSz = 512;
    // Blocks: 0=SuperBlock, 1=FPM1, 2=FPM2, 3=BlockMapAddr, 4=StreamDir, 5=Stream1(PDB), 6=Stream3(DBI)
    // (the last four from firstBlock on)
    ByteBuf s1; // Stream 1: PDB Info
    s1.U32LE(20000404u); // VC70
    s1.U32LE(0x65123456u);
    s1.U32LE(1); // age
    uint8_t guid[16] = { 0x11,0x22,0x33,0x44, 0x55,0x66, 0x77,0x88, 0x99,0xAA, 0xBB,0xCC,0xDD,0xEE,0xFF, guidLastByte };
    s1.Bytes(guid, 16);
    uint32_t s1Len = static_cast<uint32_t>(s1.b.size());
    s1.PadTo(blkSz);

    ByteBuf modSub; // DBI ModInfo record (64B fixed + moduleName\0 + objFileName\0, 4-aligned)
    modSub.Zeros(64);
    modSub.Str(objPath); modSub.U8(0);
    modSub.Str("libcore.lib"); modSub.U8(0);
    while (modSub.b.size() & 3u) modSub.U8(0);

    ByteBuf s3; // Stream 3: DBI Header (64B) + ModInfo substream
    s3.U32LE(0xFFFFFFFFu); // versionSignature = -1
    s3.U32LE(19990903u);   // DBI V70
    s3.U32LE(1);           // age
    s3.U16LE(0);
    s3.U16LE(0x8664);
    s3.U16LE(1);
    s3.U16LE(0);
    s3.U16LE(0);
    s3.U16LE(0);
    s3.U32LE(static_cast<uint32_t>(modSub.b.size())); // modInfoSize @ offset 24
    s3.PadTo(60);
    s3.U16LE(0x8664); // Machine @ offset 60
    s3.PadTo(64);
    s3.Bytes(modSub.b.data(), modSub.b.size());
    uint32_t s3Len = static_cast<uint32_t>(s3.b.size());
    s3.PadTo(blkSz);

    ByteBuf dir; // 4 streams: 0(empty), 1(PDB), 2(empty), 3(DBI)
    dir.U32LE(4);
    dir.U32LE(0);
    dir.U32LE(s1Len);
    dir.U32LE(0);
    dir.U32LE(s3Len);
    dir.U32LE(firstBlock + 2); // Stream 1 -> block 5
    dir.U32LE(firstBlock + 3); // Stream 3 -> block 6
    uint32_t dirLen = static_cast<uint32_t>(dir.b.size());
    dir.PadTo(blkSz);

    ByteBuf pdb;
    pdb.Bytes("Microsoft C/C++ MSF 7.00\r\n\x1A\x44\x53\0\0\0", 32);
    pdb.U32LE(blkSz);
    pdb.U32LE(1); // freeBlockMapBlock
    pdb.U32LE(firstBlock + 4); // numBlocks
    pdb.U32LE(dirLen);
    pdb.U32LE(0);
    pdb.U32LE(firstBlock); // blockMapAddr = block 3
    pdb.PadTo(blkSz);
    pdb.Zeros(blkSz * 2); // blocks 1 & 2 (FPM)
    pdb.PadTo(static_cast<size_t>(blkSz) * firstBlock);
    // Block 3: blockMapAddr -> points to block 4
    pdb.U32LE(firstBlock + 1);
    pdb.PadTo(static_cast<size_t>(blkSz) * (firstBlock + 1));
    // Block 4: StreamDir, Block 5: Stream 1, Block 6: Stream 3
    pdb.Bytes(dir.b.data(), dir.b.size());
    pdb.Bytes(s1.b.data(), s1.b.size());
    pdb.Bytes(s3.b.data(), s3.b.size());
    return pdb.b;
}

// 14. Java Classfile (.class 0xCAFEBABE)
static std::vector<uint8_t> MakeJavaClass(std::string_view internalClassName, std::string_view methodName) {
    ByteBuf jc;
    jc.U32BE(0xCAFEBABEu);
    jc.U16BE(0);
    jc.U16BE(61); // Java 17
    jc.U16BE(7);  // constant_pool_count = 7 (1..6)
    auto addUtf8 = [&](std::string_view s) {
        jc.U8(1); jc.U16BE(static_cast<uint16_t>(s.size())); jc.Str(s);
    };
    addUtf8(internalClassName); // #1
    jc.U8(7); jc.U16BE(1);      // #2 Class -> #1
    addUtf8("java/lang/Object");// #3
    jc.U8(7); jc.U16BE(3);      // #4 Class -> #3
    addUtf8(methodName);        // #5
    addUtf8("(I)I");            // #6

    jc.U16BE(0x0021); // ACC_PUBLIC | ACC_SUPER
    jc.U16BE(2);      // this_class
    jc.U16BE(4);      // super_class
    jc.U16BE(0);      // interfaces_count
    jc.U16BE(0);      // fields_count
    jc.U16BE(1);      // methods_count
    jc.U16BE(0x0009); // public static
    jc.U16BE(5);      // name_index
    jc.U16BE(6);      // descriptor_index
    jc.U16BE(0);      // method attributes_count
    jc.U16BE(0);      // class attributes_count
    return jc.b;
}

// 15. Android Dalvik Executable (.dex)
static std::vector<uint8_t> MakeDex(std::string_view classDesc, std::string_view methodName) {
    // 4 sorted strings: "I", "Ljava/lang/Object;", classDesc, methodName (ensure classDesc starts with "Lpkg/")
    std::vector<std::string> strs = { "I", "Ljava/lang/Object;", std::string(classDesc), std::string(methodName) };
    // Layout: 0..111 Header, 112..127 string_ids(4*4), 128..139 type_ids(3*4), 140..151 proto_ids(1*12),
    //         152..159 method_ids(1*8), 160..191 class_defs(1*32), 192.. string_data
    ByteBuf sData;
    uint32_t strOffs[4] = {};
    for (int i = 0; i < 4; ++i) {
        strOffs[i] = 192u + static_cast<uint32_t>(sData.b.size());
        sData.Uleb(strs[i].size());
        sData.Str(strs[i]);
        sData.U8(0);
    }

    ByteBuf dx;
    dx.Bytes("dex\n035\0", 8);
    dx.Zeros(24); // checksum + sha1
    uint32_t totalSz = 192u + static_cast<uint32_t>(sData.b.size());
    dx.U32LE(totalSz);
    dx.U32LE(0x70);       // header_size = 112
    dx.U32LE(0x12345678u);// endian_tag
    dx.U32LE(0); dx.U32LE(0); // link
    dx.U32LE(0);          // map_off
    dx.U32LE(4); dx.U32LE(112); // string_ids
    dx.U32LE(3); dx.U32LE(128); // type_ids (0="I", 1="Ljava/lang/Object;", 2=classDesc)
    dx.U32LE(1); dx.U32LE(140); // proto_ids
    dx.U32LE(0); dx.U32LE(0);   // field_ids
    dx.U32LE(1); dx.U32LE(152); // method_ids
    dx.U32LE(1); dx.U32LE(160); // class_defs
    dx.U32LE(static_cast<uint32_t>(sData.b.size())); dx.U32LE(192);

    for (int i = 0; i < 4; ++i) dx.U32LE(strOffs[i]);
    dx.U32LE(0); dx.U32LE(1); dx.U32LE(2); // type_ids
    dx.U32LE(0); dx.U32LE(0); dx.U32LE(0); // proto: ()I
    dx.U16LE(2); dx.U16LE(0); dx.U32LE(3); // method: class=2, proto=0, name=3
    // class_def (32B): class_idx=2, access=1(public), superclass_idx=1, interfaces=0, source=0xFFFFFFFF, ann=0, data=0, static=0
    dx.U32LE(2); dx.U32LE(1); dx.U32LE(1); dx.U32LE(0); dx.U32LE(0xFFFFFFFFu); dx.U32LE(0); dx.U32LE(0); dx.U32LE(0);
    dx.Bytes(sData.b.data(), sData.b.size());
    return dx.b;
}

// 16. Mach-O 64-bit (.macho)
static std::vector<uint8_t> MakeMachO64(uint32_t cpuType, std::string_view dylibPath, std::string_view symName) {
    ByteBuf dylibCmd;
    uint32_t dLen = static_cast<uint32_t>((24 + dylibPath.size() + 1 + 7) & ~size_t(7));
    dylibCmd.U32LE(0x0C); // LC_LOAD_DYLIB
    dylibCmd.U32LE(dLen);
    dylibCmd.U32LE(24);   // name offset
    dylibCmd.U32LE(2);
    dylibCmd.U32LE(0x00010000u);
    dylibCmd.U32LE(0x00010000u);
    dylibCmd.Str(dylibPath); dylibCmd.U8(0);
    dylibCmd.PadTo(dLen);

    std::string strtab;
    strtab.push_back('\0');
    uint32_t sIdx = static_cast<uint32_t>(strtab.size());
    strtab.append(symName);
    strtab.push_back('\0');

    uint32_t symOff = static_cast<uint32_t>(32 + dylibCmd.b.size() + 24);
    uint32_t strOff = symOff + 16;

    ByteBuf symCmd;
    symCmd.U32LE(0x02); // LC_SYMTAB
    symCmd.U32LE(24);
    symCmd.U32LE(symOff);
    symCmd.U32LE(1);
    symCmd.U32LE(strOff);
    symCmd.U32LE(static_cast<uint32_t>(strtab.size()));

    ByteBuf mo;
    mo.U32LE(0xFEEDFACFu);
    mo.U32LE(cpuType);
    mo.U32LE(0);
    mo.U32LE(2); // MH_EXECUTE
    mo.U32LE(2); // ncmds
    mo.U32LE(static_cast<uint32_t>(dylibCmd.b.size() + symCmd.b.size()));
    mo.U32LE(0);
    mo.U32LE(0);
    mo.Bytes(dylibCmd.b.data(), dylibCmd.b.size());
    mo.Bytes(symCmd.b.data(), symCmd.b.size());

    // 1 nlist_64 (16B)
    mo.U32LE(sIdx);
    mo.U8(0x0F); // N_SECT | N_EXT
    mo.U8(1);
    mo.U16LE(0);
    mo.U64LE(0x100003F00ULL);
    mo.Str(strtab);
    return mo.b;
}

// 17. RFC 5280 ASN.1 DER X.509 v3 Certificate (.cer & .pem)
static std::vector<uint8_t> Asn1Wrap(uint8_t tag, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    out.push_back(tag);
    size_t n = body.size();
    if (n < 128) {
        out.push_back(static_cast<uint8_t>(n));
    } else if (n < 256) {
        out.push_back(0x81);
        out.push_back(static_cast<uint8_t>(n));
    } else {
        out.push_back(0x82);
        out.push_back(static_cast<uint8_t>((n >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>(n & 0xFFu));
    }
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static void Asn1Append(std::vector<uint8_t>& dst, const std::vector<uint8_t>& item) {
    dst.insert(dst.end(), item.begin(), item.end());
}

static std::vector<uint8_t> MakeX509Der(uint8_t serialByte, std::string_view subjectCn, std::string_view sanDns) {
    auto makeNameCn = [&](std::string_view cn) {
        std::vector<uint8_t> oidCn = { 0x06, 0x03, 0x55, 0x04, 0x03 }; // 2.5.4.3 commonName
        std::vector<uint8_t> val(cn.begin(), cn.end());
        std::vector<uint8_t> atv;
        Asn1Append(atv, oidCn);
        Asn1Append(atv, Asn1Wrap(0x0C, val)); // UTF8String
        return Asn1Wrap(0x30, Asn1Wrap(0x31, Asn1Wrap(0x30, atv)));
    };
    // sha256WithRSAEncryption: 1.2.840.113549.1.1.11
    std::vector<uint8_t> sigAlg = Asn1Wrap(0x30, { 0x06,0x09, 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0B, 0x05,0x00 });

    std::vector<uint8_t> tbs;
    Asn1Append(tbs, Asn1Wrap(0xA0, { 0x02, 0x01, 0x02 })); // v3
    Asn1Append(tbs, { 0x02, 0x02, 0x01, serialByte });     // serialNumber
    Asn1Append(tbs, sigAlg);
    Asn1Append(tbs, makeNameCn("Explorer2 Root CA"));

    std::string t1 = "250101000000Z", t2 = "350101000000Z";
    std::vector<uint8_t> valSeq;
    Asn1Append(valSeq, Asn1Wrap(0x17, std::vector<uint8_t>(t1.begin(), t1.end())));
    Asn1Append(valSeq, Asn1Wrap(0x17, std::vector<uint8_t>(t2.begin(), t2.end())));
    Asn1Append(tbs, Asn1Wrap(0x30, valSeq));

    Asn1Append(tbs, makeNameCn(subjectCn));

    // SubjectPublicKeyInfo (RSA 512-bit public key)
    std::vector<uint8_t> rsaAlg = Asn1Wrap(0x30, { 0x06,0x09, 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01, 0x05,0x00 });
    std::vector<uint8_t> modBytes(65, 0xAB);
    modBytes[0] = 0x00; modBytes[1] = 0xC3; modBytes[64] = static_cast<uint8_t>(0x01 | serialByte);
    std::vector<uint8_t> rsaPubSeq;
    Asn1Append(rsaPubSeq, Asn1Wrap(0x02, modBytes));
    Asn1Append(rsaPubSeq, { 0x02, 0x03, 0x01, 0x00, 0x01 });
    std::vector<uint8_t> rsaDer = Asn1Wrap(0x30, rsaPubSeq);
    std::vector<uint8_t> bitStrBody = { 0x00 };
    Asn1Append(bitStrBody, rsaDer);
    std::vector<uint8_t> spki;
    Asn1Append(spki, rsaAlg);
    Asn1Append(spki, Asn1Wrap(0x03, bitStrBody));
    Asn1Append(tbs, Asn1Wrap(0x30, spki));

    // v3 Extension: subjectAltName (2.5.29.17) -> GeneralNames SEQUENCE -> [2] dNSName
    std::vector<uint8_t> gn = Asn1Wrap(0x82, std::vector<uint8_t>(sanDns.begin(), sanDns.end()));
    std::vector<uint8_t> sanExt;
    Asn1Append(sanExt, { 0x06, 0x03, 0x55, 0x1D, 0x11 });
    Asn1Append(sanExt, Asn1Wrap(0x04, Asn1Wrap(0x30, gn)));
    Asn1Append(tbs, Asn1Wrap(0xA3, Asn1Wrap(0x30, Asn1Wrap(0x30, sanExt))));

    std::vector<uint8_t> sigVal(65, serialByte);
    sigVal[0] = 0x00;

    std::vector<uint8_t> cert;
    Asn1Append(cert, Asn1Wrap(0x30, tbs));
    Asn1Append(cert, sigAlg);
    Asn1Append(cert, Asn1Wrap(0x03, sigVal));
    return Asn1Wrap(0x30, cert);
}

static std::vector<uint8_t> WrapDerAsPem(const std::vector<uint8_t>& der, std::string_view label) {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "-----BEGIN " + std::string(label) + "-----\r\n";
    int col = 0;
    for (size_t i = 0; i < der.size(); i += 3) {
        uint32_t v = static_cast<uint32_t>(der[i]) << 16;
        if (i + 1 < der.size()) v |= static_cast<uint32_t>(der[i + 1]) << 8;
        if (i + 2 < der.size()) v |= static_cast<uint32_t>(der[i + 2]);
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back((i + 1 < der.size()) ? kB64[(v >> 6) & 63] : '=');
        out.push_back((i + 2 < der.size()) ? kB64[v & 63] : '=');
        col += 4;
        if (col >= 64) { out += "\r\n"; col = 0; }
    }
    if (col > 0) out += "\r\n";
    out += "-----END " + std::string(label) + "-----\r\n";
    return std::vector<uint8_t>(out.begin(), out.end());
}

// 18. KeePass KDBX v4 with Argon2id VariantDictionary
static std::vector<uint8_t> MakeKdbx4(bool chachaCipher, uint64_t memBytes, uint64_t iterations) {
    ByteBuf vd;
    vd.U16LE(0x0100); // VariantDictionary v1.0
    auto addVdBytes = [&](std::string_view key, const uint8_t* p, uint32_t n) {
        vd.U8(0x42);
        vd.U32LE(static_cast<uint32_t>(key.size())); vd.Str(key);
        vd.U32LE(n); vd.Bytes(p, n);
    };
    auto addVdU64 = [&](std::string_view key, uint64_t val) {
        vd.U8(0x05);
        vd.U32LE(static_cast<uint32_t>(key.size())); vd.Str(key);
        vd.U32LE(8); vd.U64LE(val);
    };
    static const uint8_t kArgon2id[16] = {
        0x9E,0x29,0x8B,0x19,0x56,0xDB,0x47,0x73,0xB2,0x3D,0xFC,0x3E,0xC6,0xF0,0xA1,0xE6
    };
    addVdBytes("$UUID", kArgon2id, 16);
    addVdU64("M", memBytes);
    addVdU64("I", iterations);
    addVdU64("P", 4);
    vd.U8(0x00); // VD end

    static const uint8_t kAes256[16] = {
        0x31,0xC1,0xF2,0xE6,0xBF,0x71,0x43,0x50,0xBE,0x58,0x05,0x21,0x6A,0xFC,0x5A,0xFF
    };
    static const uint8_t kChaCha[16] = {
        0xD6,0x03,0x8A,0x2B,0x8B,0x6F,0x4C,0xB5,0xA5,0x24,0x33,0x9A,0x31,0xDB,0xB5,0x9A
    };

    ByteBuf k;
    k.U32LE(0x9AA2D903u);
    k.U32LE(0xB54BFB67u);
    k.U32LE(0x00040001u); // KDBX 4.1
    // Field 2: CipherID
    k.U8(2); k.U32LE(16); k.Bytes(chachaCipher ? kChaCha : kAes256, 16);
    // Field 3: CompressionFlags (1 = GZip)
    k.U8(3); k.U32LE(4); k.U32LE(1);
    // Field 11: KdfParameters
    k.U8(11); k.U32LE(static_cast<uint32_t>(vd.b.size())); k.Bytes(vd.b.data(), vd.b.size());
    // Field 0: EndOfHeader
    k.U8(0); k.U32LE(4); k.Bytes("\r\n\r\n", 4);
    k.Zeros(64); // SHA-256 + HMAC trailer stub
    return k.b;
}

// 19. PCAPNG (SHB + IDB + EPB Ethernet/IPv4/TCP vs UDP/DNS)
static std::vector<uint8_t> MakePcapNg(bool dnsPacket) {
    ByteBuf ng;
    // SHB (28B)
    ng.U32LE(0x0A0D0D0Au);
    ng.U32LE(28);
    ng.U32LE(0x1A2B3C4Du);
    ng.U16LE(1); ng.U16LE(0);
    ng.U64LE(0xFFFFFFFFFFFFFFFFULL);
    ng.U32LE(28);

    // IDB (20B): Ethernet linkType=1
    ng.U32LE(0x00000001u);
    ng.U32LE(20);
    ng.U16LE(1);
    ng.U16LE(0);
    ng.U32LE(65535);
    ng.U32LE(20);

    // Frame bytes: 14B Eth + 20B IPv4 + L4
    ByteBuf frm;
    frm.Bytes("\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\x08\x00", 14);
    if (!dnsPacket) {
        // IPv4 TCP SYN to 10.0.0.42:80 + HTTP GET
        std::string http = "GET /status HTTP/1.1\r\n";
        uint16_t ipLen = static_cast<uint16_t>(20 + 20 + http.size());
        frm.U8(0x45); frm.U8(0); frm.U16BE(ipLen);
        frm.U16BE(1); frm.U16BE(0);
        frm.U8(64); frm.U8(6); frm.U16BE(0); // proto=6 TCP
        frm.Bytes("\xC0\xA8\x01\x0A\x0A\x00\x00\x2A", 8);
        // TCP header (20B)
        frm.U16BE(51234); frm.U16BE(80);
        frm.U32BE(1); frm.U32BE(0);
        frm.U8(0x50); frm.U8(0x02); // dataOff=5, SYN
        frm.U16BE(8192); frm.U16BE(0); frm.U16BE(0);
        frm.Str(http);
    } else {
        // IPv4 UDP port 53 DNS query for "api.explorer2.local"
        ByteBuf dns;
        dns.U16BE(0x1234); dns.U16BE(0x0100); // standard query
        dns.U16BE(1); dns.U16BE(0); dns.U16BE(0); dns.U16BE(0);
        dns.U8(3); dns.Str("api");
        dns.U8(9); dns.Str("explorer2");
        dns.U8(5); dns.Str("local");
        dns.U8(0);
        dns.U16BE(1); dns.U16BE(1);

        uint16_t udpLen = static_cast<uint16_t>(8 + dns.b.size());
        uint16_t ipLen  = static_cast<uint16_t>(20 + udpLen);
        frm.U8(0x45); frm.U8(0); frm.U16BE(ipLen);
        frm.U16BE(2); frm.U16BE(0);
        frm.U8(64); frm.U8(17); frm.U16BE(0); // proto=17 UDP
        frm.Bytes("\xC0\xA8\x01\x0A\x08\x08\x08\x08", 8);
        frm.U16BE(53000); frm.U16BE(53);
        frm.U16BE(udpLen); frm.U16BE(0);
        frm.Bytes(dns.b.data(), dns.b.size());
    }

    uint32_t capLen = static_cast<uint32_t>(frm.b.size());
    while (frm.b.size() & 3u) frm.U8(0);
    uint32_t epbLen = static_cast<uint32_t>(32 + frm.b.size());

    ng.U32LE(0x00000006u); // EPB
    ng.U32LE(epbLen);
    ng.U32LE(0);
    ng.U32LE(0); ng.U32LE(100000);
    ng.U32LE(capLen);
    ng.U32LE(capLen);
    ng.Bytes(frm.b.data(), frm.b.size());
    ng.U32LE(epbLen);
    return ng.b;
}

} // namespace

void RunFormatTestsDocsSystem(TestCounter& tc) {
    // 1. Existing testdata/sample.elf & testdata/sample.wasm regression check
    {
        std::wstring td = TestDataDir();
        ParsedOfficeDocument dElf, dWasm;
        bool okElf  = SpreadsheetDocViewer::InspectOfficeFile(td + L"\\sample.elf", dElf) && dElf.valid;
        bool okWasm = SpreadsheetDocViewer::InspectOfficeFile(td + L"\\sample.wasm", dWasm) && dWasm.valid && DocContainsToken(dWasm, L"add");
        tc.Record(okElf,  L"DocsSystem testdata/sample.elf compatibility");
        tc.Record(okWasm, L"DocsSystem testdata/sample.wasm compatibility");
    }

    std::wstring dir = ScratchTestDir() + L"\\DocsSystem";
    CreateDirectoryW(dir.c_str(), nullptr);

    const std::vector<uint8_t> kGarbage = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF, 0x13, 0x37, 0x42, 0x99 };

    // 2. ELF64 & WebAssembly
    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .elf (ELF64 symbols & DT_NEEDED)", L".elf",
        MakeElf64("libm.so.6", "alpha_compute"), L"alpha_compute",
        MakeElf64("libvulkan.so.1", "beta_render"), L"beta_render",
        kGarbage);

    // An ELF larger than the 32 MB a reader is handed, its section header table and string, symbol and dynamic
    // tables past that (as at the end of a large binary): all read from the file, and the title does not claim
    // only the start was read
    {
        std::vector<uint8_t> big = MakeElf64("libfar.so.3", "far_away_symbol", 33u * 1024 * 1024);
        std::wstring bigPath = dir + L"\\far_tables.elf";
        PathUtils::WriteFileBytes(bigPath, big.data(), big.size());
        big.clear();
        big.shrink_to_fit();
        ParsedOfficeDocument dBig;
        bool bigOk = SpreadsheetDocViewer::InspectOfficeFile(bigPath, dBig) && dBig.valid &&
                     DocContainsToken(dBig, L"far_away_symbol") && DocContainsToken(dBig, L"libfar.so.3") &&
                     DocContainsToken(dBig, L".dynamic") && DocContainsToken(dBig, L"/lib64/ld-linux-x86-64.so.2") &&
                     dBig.formatTitle.find(L"only the first") == std::wstring::npos;
        DeleteFileW(bigPath.c_str());
        tc.Record(bigOk, L"DocsSystem.Elf.Over32MbTablesReadFromTheFile", dBig.formatTitle);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .wasm (Imports, Exports & Signatures)", L".wasm",
        MakeWasmModule("env", "log_i32", "calc_sum", 0x7F), L"calc_sum",
        MakeWasmModule("wasi", "fd_write", "transform_f64", 0x7C), L"transform_f64",
        kGarbage);

    // 3. Email (.eml, .mbox, .msg)
    {
        std::string emlA =
            "From: Alice <alice@example.com>\r\n"
            "To: Bob <bob@example.com>\r\n"
            "Subject: =?UTF-8?B?UXVhcnRlcmx5IFJvYWRtYXA=?=\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
            "Milestone=20Alpha=20approved.\r\n";
        std::string emlB =
            "From: Carol <carol@example.org>\r\n"
            "To: Dave <dave@example.org>\r\n"
            "Subject: Security Audit Findings\r\n"
            "Content-Type: multipart/mixed; boundary=\"BND42\"\r\n\r\n"
            "--BND42\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n"
            "Zero critical vulnerabilities found.\r\n"
            "--BND42\r\nContent-Disposition: attachment; filename=\"audit_report.pdf\"\r\n\r\nPDFDATA\r\n--BND42--\r\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .eml (RFC 2047 + MIME Multipart)", L".eml",
            std::vector<uint8_t>(emlA.begin(), emlA.end()), L"Quarterly Roadmap",
            std::vector<uint8_t>(emlB.begin(), emlB.end()), L"audit_report.pdf",
            kGarbage);
    }

    // E-mail as mailers send it: multipart/mixed holding multipart/alternative (whose Content-Type header is folded)
    // and an attachment named the RFC 2231 way; an ISO-8859-2 subject (RFC 2047) and body; the plain text wins over
    // the HTML alternative
    {
        const std::string eml =
            "From: Ewa <ewa@example.pl>\r\nTo: Jan <jan@example.pl>\r\n"
            "Subject: =?iso-8859-2?Q?Za=BF=F3=B3=E6?=\r\nMIME-Version: 1.0\r\n"
            "Content-Type: multipart/mixed; boundary=\"MIX\"\r\n\r\n"
            "--MIX\r\nContent-Type: multipart/alternative;\r\n\tboundary=\"ALT\"\r\n\r\n"
            "--ALT\r\nContent-Type: text/plain; charset=iso-8859-2\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n"
            "G=EAsi jazd=B1.\r\n"
            "--ALT\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<p>HTML version</p>\r\n--ALT--\r\n\r\n"
            "--MIX\r\nContent-Type: application/pdf\r\nContent-Disposition: attachment;\r\n"
            " filename*=UTF-8''Raport%20Q4%20%C5%BC%C3%B3%C5%82ty.pdf\r\nContent-Transfer-Encoding: base64\r\n\r\n"
            "JVBERi0=\r\n--MIX--\r\n";
        std::wstring emlPath = dir + L"\\nested.eml";
        PathUtils::WriteFileBytes(emlPath, eml.data(), eml.size());
        ParsedOfficeDocument dEml;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(emlPath, dEml) && dEml.valid &&
                  DocContainsToken(dEml, L"Za\u017C\u00F3\u0142\u0107") &&
                  DocContainsToken(dEml, L"G\u0119si jazd\u0105.") &&
                  DocContainsToken(dEml, L"Raport Q4 \u017C\u00F3\u0142ty.pdf") &&
                  !DocContainsToken(dEml, L"HTML version");
        DeleteFileW(emlPath.c_str());
        std::wstring dump;
        for (const auto& para : dEml.paragraphs) dump += para.text.substr(0, 40) + L" | ";
        tc.Record(ok, L"DocsSystem.Email.NestedMultipartCharsetsAndRfc2231Names", dump.substr(0, 300));
    }

    // .msg whose body exists only as compressed RTF (MS-OXRTFCP "LZFu": literal runs, a back reference into the
    // preset dictionary preamble, the end marker)
    {
        const std::string rtfText = "{\\rtf1\\ansi Quarterly figures attached\\par}";
        // tokens: "{" (literal), "\\rtf1\\ansi" copied from dictionary offset 1 (10 bytes), then the rest as literals
        std::vector<uint8_t> tokens, flags;
        tokens.push_back('{');
        flags.push_back(0);
        tokens.push_back(static_cast<uint8_t>((1 << 4) >> 8));
        tokens.push_back(static_cast<uint8_t>(((1 << 4) | (10 - 2)) & 0xFF));
        flags.push_back(1);
        for (size_t i = 11; i < rtfText.size(); ++i) { tokens.push_back(static_cast<uint8_t>(rtfText[i])); flags.push_back(0); }
        const size_t writePos = (207 + rtfText.size()) & 0xFFF; // end marker: a reference to the write position
        tokens.push_back(static_cast<uint8_t>((writePos << 4) >> 8));
        tokens.push_back(static_cast<uint8_t>((writePos << 4) & 0xFF));
        flags.push_back(1);
        std::vector<uint8_t> data;
        size_t t = 0;
        for (size_t f = 0; f < flags.size(); f += 8) {
            uint8_t control = 0;
            for (size_t b = 0; b < 8 && f + b < flags.size(); ++b) if (flags[f + b]) control |= static_cast<uint8_t>(1u << b);
            data.push_back(control);
            for (size_t b = 0; b < 8 && f + b < flags.size(); ++b) {
                size_t n = flags[f + b] ? 2 : 1;
                data.insert(data.end(), tokens.begin() + static_cast<std::ptrdiff_t>(t), tokens.begin() + static_cast<std::ptrdiff_t>(t + n));
                t += n;
            }
        }
        std::vector<uint8_t> lzfu;
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) lzfu.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        u32(static_cast<uint32_t>(data.size() + 12));
        u32(static_cast<uint32_t>(rtfText.size()));
        u32(0x75465A4Cu); // "LZFu"
        u32(0);           // CRC (not checked)
        lzfu.insert(lzfu.end(), data.begin(), data.end());
        std::vector<uint8_t> msg = MakeOutlookMsg(L"Figures", L"Finance", L"", L"q3.xlsx", lzfu);
        std::wstring msgPath = dir + L"\\rtf_only.msg";
        PathUtils::WriteFileBytes(msgPath, msg.data(), msg.size());
        ParsedOfficeDocument dMsg;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(msgPath, dMsg) && dMsg.valid &&
                  DocContainsToken(dMsg, L"Quarterly figures attached") && !DocContainsToken(dMsg, L"rtf1");
        DeleteFileW(msgPath.c_str());
        tc.Record(ok, L"DocsSystem.OutlookMsg.CompressedRtfOnlyBody");
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .msg (OLE2 MAPI Subject, Body & Attachment)", L".msg",
        MakeOutlookMsg(L"Architecture Review", L"Dr. Vance", L"Please review the propulsion schematic.", L"schematic_v1.pdf"), L"schematic_v1.pdf",
        MakeOutlookMsg(L"Budget Sign-Off Q4", L" CFO Office", L"Approved capital expenditure table.", L"capex_q4.xlsx"), L"capex_q4.xlsx",
        kGarbage);

    // 4. Audio & Tracker (.mp3, .ogg, .mod, .xm, .s3m, .it, .sf2, .sfz)
    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .mp3 (ID3v2.3 UTF-16 + MPEG Layer III + Xing)", L".mp3",
        MakeMp3(L"Solar Horizon", "Synthwave Collective", 0xFFFB9064u, 417), L"Solar Horizon",
        MakeMp3(L"Nocturne in C#", "Classical Ensemble", 0xFFFBE000u, 1044), L"Nocturne in C#",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .ogg (Ogg Vorbis BOS + Comments + Granule Duration)", L".ogg",
        MakeOggVorbis(0x11223344u, 44100, 44100LL * 125, "Aurora Borealis"), L"Aurora Borealis",
        MakeOggVorbis(0x99887766u, 48000, 48000LL * 63, "Deep Ocean Echo"), L"Deep Ocean Echo",
        kGarbage);

    // Media files larger than the 32 MB a reader is handed: a CBR MP3's duration and its ID3v1 tag come from the whole
    // file (size, tail), and an Ogg stream's duration from its last page, at the end of the file
    {
        std::vector<uint8_t> mp3;
        const size_t kFrame = 417; // MPEG-1 Layer III, 128 kbps, 44.1 kHz, no padding
        mp3.reserve(34u * 1024 * 1024);
        while (mp3.size() < 33u * 1024 * 1024) {
            const uint8_t hdr[4] = { 0xFF, 0xFB, 0x90, 0x00 };
            mp3.insert(mp3.end(), hdr, hdr + 4);
            mp3.resize(mp3.size() + kFrame - 4, 0);
        }
        const size_t audioBytes = mp3.size();
        std::vector<uint8_t> id3v1(128, 0);
        std::memcpy(id3v1.data(), "TAGBig Show", 11);
        mp3.insert(mp3.end(), id3v1.begin(), id3v1.end());
        auto durationText = [](double sec) { // the reader's mm:ss
            int totalSec = static_cast<int>(sec * 1000.0 + 0.5) / 1000;
            wchar_t b[32];
            swprintf_s(b, L"%02d:%02d", totalSec / 60, totalSec % 60);
            return std::wstring(b);
        };
        const std::wstring mp3Expected = durationText(static_cast<double>(audioBytes) * 8.0 / 128000.0);
        std::wstring mp3Path = dir + L"\\long_show.mp3";
        PathUtils::WriteFileBytes(mp3Path, mp3.data(), mp3.size());
        mp3.clear();
        mp3.shrink_to_fit();
        ParsedOfficeDocument dMp3;
        bool mp3Ok = SpreadsheetDocViewer::InspectOfficeFile(mp3Path, dMp3) && dMp3.valid &&
                     DocContainsToken(dMp3, mp3Expected) && DocContainsToken(dMp3, L"Big Show") &&
                     dMp3.formatTitle.find(L"only the first") != std::wstring::npos;
        DeleteFileW(mp3Path.c_str());

        // Ogg: the usual header pages, 33 MB of other data, then the stream's last page (granule = 1 hour at 44.1 kHz)
        std::vector<uint8_t> ogg = MakeOggVorbis(0x5151A0A0u, 44100, 44100LL * 10, "Night Drive");
        ogg.resize(ogg.size() + 33u * 1024 * 1024, 0);
        const uint64_t lastGranule = 44100ULL * 3600;
        uint8_t page[28] = { 'O', 'g', 'g', 'S', 0, 0x04 };
        for (int i = 0; i < 8; ++i) page[6 + i] = static_cast<uint8_t>(lastGranule >> (8 * i));
        for (int i = 0; i < 4; ++i) page[14 + i] = static_cast<uint8_t>(0x5151A0A0u >> (8 * i));
        page[18] = 9;  // page sequence
        page[26] = 1;  // one segment
        page[27] = 1;  // of one byte
        ogg.insert(ogg.end(), page, page + 28);
        ogg.push_back(0);
        std::wstring oggPath = dir + L"\\long_drive.ogg";
        PathUtils::WriteFileBytes(oggPath, ogg.data(), ogg.size());
        ogg.clear();
        ogg.shrink_to_fit();
        ParsedOfficeDocument dOgg;
        bool oggOk = SpreadsheetDocViewer::InspectOfficeFile(oggPath, dOgg) && dOgg.valid && DocContainsToken(dOgg, L"1:00:00");
        DeleteFileW(oggPath.c_str());
        tc.Record(mp3Ok && oggOk, L"DocsSystem.Media.Over32MbDurationAndTrailingTagsFromWholeFile",
                  L"mp3=" + std::to_wstring(mp3Ok) + L" (" + mp3Expected + L") ogg=" + std::to_wstring(oggOk));
    }

    {
        auto makeMod = [](std::string_view title, const char* tag, std::string_view smpName, uint16_t words) {
            ByteBuf m;
            m.FixedStr(title, 20);
            m.FixedStr(smpName, 22); m.U16BE(words); m.U8(0); m.U8(64); m.U16BE(0); m.U16BE(1);
            m.Zeros(30 * 30);
            m.U8(4); m.U8(127); // songLength=4
            m.Zeros(128);
            m.Bytes(tag, 4);
            m.Zeros(1024);
            return m.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .mod (ProTracker 4ch/8ch & Sample Headers)", L".mod",
            makeMod("Stardust Memories", "M.K.", "lead_saw.pcm", 2048), L"Stardust Memories",
            makeMod("Cybernetic Pulse", "8CHN", "bass_sub.pcm", 4096), L"Cybernetic Pulse",
            kGarbage);
    }

    {
        auto makeXm = [](std::string_view modTitle, uint16_t bpm, std::string_view instName) {
            ByteBuf x;
            x.Str("Extended Module: ");
            x.FixedStr(modTitle, 20);
            x.U8(0x1A);
            x.FixedStr("FastTracker v2.00", 20);
            x.U16LE(0x0104);
            x.U32LE(276); // headerSize
            x.U16LE(1); x.U16LE(0); x.U16LE(8);
            x.U16LE(1); // 1 pattern
            x.U16LE(1); // 1 instrument
            x.U16LE(1); x.U16LE(6); x.U16LE(bpm);
            x.Zeros(256);
            // Pattern 0 (9B header + 0 packed)
            x.U32LE(9); x.U8(0); x.U16LE(64); x.U16LE(0);
            // Instrument 1 (29B, 0 samples)
            x.U32LE(29); x.FixedStr(instName, 22); x.U8(0); x.U16LE(0);
            return x.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .xm (FastTracker II Patterns & Instruments)", L".xm",
            makeXm("Nebula Runner", 125, "Warm Pad"), L"Warm Pad",
            makeXm("Acid Raindrop", 145, "TB303 Resonance"), L"TB303 Resonance",
            kGarbage);
    }

    {
        auto makeS3m = [](std::string_view title, std::string_view insName, uint32_t c2spd) {
            ByteBuf s;
            s.FixedStr(title, 28);
            s.U8(0x1A); s.U8(16); s.U16LE(0);
            s.U16LE(2); // ordNum=2
            s.U16LE(1); // insNum=1
            s.U16LE(0); // patNum=0
            s.U16LE(0); s.U16LE(0x1320); s.U16LE(2);
            s.Str("SCRM");
            s.U8(64); s.U8(6); s.U8(130); s.U8(48);
            s.PadTo(96);
            s.U8(0); s.U8(255); // 2 orders
            s.U16LE(8);         // parapointer = 8 => offset 128
            s.PadTo(128);
            // 80B instrument @ 128
            s.U8(1);
            s.FixedStr("LEAD01.WAV", 12);
            s.U8(0); s.U16LE(0);
            s.U32LE(2048); s.U32LE(0); s.U32LE(0);
            s.U8(64); s.U8(0); s.U8(0); s.U8(0);
            s.U32LE(c2spd);
            s.Zeros(12);
            s.FixedStr(insName, 28);
            s.Str("SCRS");
            return s.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .s3m (ScreamTracker 3 Parapointers & Instruments)", L".s3m",
            makeS3m("Second Reality Echo", "Hollow Choir", 8363), L"Hollow Choir",
            makeS3m("Crystal Hammer", "Distorted Guitar", 16726), L"Distorted Guitar",
            kGarbage);
    }

    {
        auto makeIt = [](std::string_view songName, std::string_view smpName, uint32_t c5spd) {
            ByteBuf it;
            it.Str("IMPM");
            it.FixedStr(songName, 26);
            it.U16LE(0x0404);
            it.U16LE(2); // ordNum
            it.U16LE(0); // insNum
            it.U16LE(1); // smpNum
            it.U16LE(0); // patNum
            it.U16LE(0x0214); it.U16LE(0x0200);
            it.U16LE(9); it.U16LE(0);
            it.U8(128); it.U8(48); it.U8(6); it.U8(128);
            it.PadTo(192);
            it.U8(0); it.U8(255); // orders
            it.U32LE(208);        // sample offset @ 208
            it.PadTo(208);
            it.Str("IMPS");
            it.FixedStr("SMP01.WAV", 12);
            it.U8(0); it.U8(64); it.U8(0); it.U8(64);
            it.FixedStr(smpName, 26);
            it.U8(1); it.U8(0);
            it.U32LE(4096); it.U32LE(0); it.U32LE(0);
            it.U32LE(c5spd);
            it.Zeros(16);
            return it.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .it (Impulse Tracker IMPS Samples)", L".it",
            makeIt("Unreal Superhero", "Chiptune Square", 22050), L"Chiptune Square",
            makeIt("Enigma Horizon", "Orchestral Hit", 44100), L"Orchestral Hit",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .sf2 (SoundFont RIFF sfbk INFO & pdta)", L".sf2",
        MakeSf2("Concert Grand Bank", "Concert Piano", "Steinway_C4", 44100), L"Steinway_C4",
        MakeSf2("Vintage Synth Bank", "Analog Brass", "Oberheim_Saw", 48000), L"Oberheim_Saw",
        kGarbage);

    {
        std::string sfzA = "<global> volume=-3\n<region> sample=piano_c4.wav lokey=48 hikey=72 pitch_keycenter=60\n";
        std::string sfzB = "<group> lovel=64 hivel=127\n<region> sample=snare_rim.wav key=38 pan=10\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .sfz (SFZ Regions & Opcodes)", L".sfz",
            std::vector<uint8_t>(sfzA.begin(), sfzA.end()), L"piano_c4.wav",
            std::vector<uint8_t>(sfzB.begin(), sfzB.end()), L"snare_rim.wav",
            kGarbage);
    }

    // 5. Score, DAW & NLE (.mscz, .als, .flp, .gp, .rpp, .edl, .fcpxml, .otio, .idx, .sup, .sub)
    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .mscz (MuseScore ZIP + XML Score)", L".mscz",
        BuildStoredZipSingle("score.mscx",
            "<?xml version=\"1.0\"?><museScore version=\"4.1\"><Score>"
            "<metaTag name=\"workTitle\">Moonlight Etude</metaTag>"
            "<Part><trackName>Violin I</trackName></Part>"
            "<Staff id=\"1\"><Measure/><Measure/><Measure/></Staff></Score></museScore>"),
        L"Moonlight Etude",
        BuildStoredZipSingle("symphony.mscx",
            "<?xml version=\"1.0\"?><museScore version=\"4.2\"><Score>"
            "<metaTag name=\"workTitle\">Brass Fanfare</metaTag>"
            "<Part><trackName>French Horn</trackName></Part>"
            "<Staff id=\"1\"><Measure/></Staff></Score></museScore>"),
        L"Brass Fanfare",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .als (Ableton Gzip Live Set)", L".als",
        BuildStoredGzip("<?xml version=\"1.0\"?><Ableton MajorVersion=\"5\" MinorVersion=\"11.0\" Creator=\"Ableton Live 11.3\">"
                        "<LiveSet><Tempo><Manual Value=\"124\"/></Tempo><Tracks>"
                        "<MidiTrack><Name><EffectiveName Value=\"Pluck Arp\"/></Name><MidiClip/></MidiTrack>"
                        "</Tracks></LiveSet></Ableton>"),
        L"Pluck Arp",
        BuildStoredGzip("<?xml version=\"1.0\"?><Ableton MajorVersion=\"5\" MinorVersion=\"12.0\" Creator=\"Ableton Live 12.0\">"
                        "<LiveSet><Tempo><Manual Value=\"172\"/></Tempo><Tracks>"
                        "<AudioTrack><Name><EffectiveName Value=\"Amen Break Drum\"/></Name><AudioClip/><AudioClip/></AudioTrack>"
                        "</Tracks></LiveSet></Ableton>"),
        L"Amen Break Drum",
        kGarbage);

    // Ableton tracks in project order with their group; session clips (clip slots) and arrangement clips are counted
    // apart, frozen copies are not; the tempo comes from the master track's mixer, not from another device's "Tempo"
    {
        const std::string set =
            "<?xml version=\"1.0\"?><Ableton MajorVersion=\"5\" MinorVersion=\"11.0\" Creator=\"Ableton Live 11.3\"><LiveSet><Tracks>"
            "<GroupTrack Id=\"10\"><Name><EffectiveName Value=\"Drums\"/></Name><TrackGroupId Value=\"-1\"/></GroupTrack>"
            "<AudioTrack Id=\"11\"><Name><EffectiveName Value=\"Kick\"/></Name><TrackGroupId Value=\"10\"/><DeviceChain><MainSequencer>"
            "<ClipSlotList><ClipSlot><ClipSlot><Value><AudioClip/></Value></ClipSlot></ClipSlot><ClipSlot><ClipSlot><Value/></ClipSlot></ClipSlot></ClipSlotList>"
            "<Sample><ArrangerAutomation><Events><AudioClip/><AudioClip/></Events></ArrangerAutomation></Sample></MainSequencer>"
            "<FreezeSequencer><ClipSlotList><ClipSlot><ClipSlot><Value><AudioClip/></Value></ClipSlot></ClipSlot></ClipSlotList></FreezeSequencer>"
            "<DeviceChain><Devices><Compressor2/><Eq8/></Devices></DeviceChain></DeviceChain></AudioTrack>"
            "<MidiTrack Id=\"12\"><Name><EffectiveName Value=\"Bass\"/></Name><TrackGroupId Value=\"-1\"/><DeviceChain><MainSequencer>"
            "<ClipTimeable><ArrangerAutomation><Events><MidiClip/></Events></ArrangerAutomation></ClipTimeable></MainSequencer>"
            "<DeviceChain><Devices><InstrumentVector><Tempo><Manual Value=\"99\"/></Tempo></InstrumentVector></Devices></DeviceChain></DeviceChain></MidiTrack>"
            "<ReturnTrack Id=\"13\"><Name><EffectiveName Value=\"A-Reverb\"/></Name></ReturnTrack>"
            "</Tracks><MasterTrack><DeviceChain><Mixer><Tempo><Manual Value=\"128\"/></Tempo></Mixer></DeviceChain></MasterTrack>"
            "<Scenes><Scene/><Scene/></Scenes></LiveSet></Ableton>";
        std::wstring alsPath = dir + L"\\order.als";
        std::vector<uint8_t> gz = BuildStoredGzip(set);
        PathUtils::WriteFileBytes(alsPath, gz.data(), gz.size());
        ParsedOfficeDocument als;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(alsPath, als) && als.valid && als.sheets.size() == 2;
        DeleteFileW(alsPath.c_str());
        const SpreadsheetSheet* tr = ok ? &als.sheets[1] : nullptr;
        auto cell = [&](size_t r, size_t c) { return (tr && r < tr->rows.size() && c < tr->rows[r].size()) ? tr->rows[r][c] : std::wstring(L"<none>"); };
        ok = ok && tr->rows.size() == 5 &&
             cell(1, 2) == L"Drums" && cell(2, 2) == L"Kick" && cell(3, 2) == L"Bass" && cell(4, 2) == L"A-Reverb" &&
             cell(2, 3) == L"Drums" && cell(2, 4) == L"1" && cell(2, 5) == L"2" && cell(2, 6) == L"2" &&
             cell(3, 3) == L"" && cell(3, 5) == L"1" && DocContainsToken(als, L"128 BPM") && !DocContainsToken(als, L"99 BPM") &&
             DocContainsToken(als, L"Scenes");
        tc.Record(ok, L"DocsSystem.Ableton.TracksInOrderGroupsClipsAndMasterTempo",
                  L"kick=" + cell(2, 4) + L"/" + cell(2, 5) + L"/" + cell(2, 6) + L" group=" + cell(2, 3));
    }

    {
        auto makeFlp = [](std::string_view ver, uint32_t tempoMilli, std::wstring_view title, std::wstring_view plugin) {
            ByteBuf dt;
            // 199 Version (ASCII)
            dt.U8(199); dt.Uleb(ver.size() + 1); dt.Str(ver); dt.U8(0);
            // 156 Tempo (BPM * 1000)
            dt.U8(156); dt.U32LE(tempoMilli);
            // 194 Title (UTF-16LE)
            dt.U8(194); dt.Uleb((title.size() + 1) * 2); dt.Utf16LE(title, true);
            // 201 Plugin (UTF-16LE)
            dt.U8(201); dt.Uleb((plugin.size() + 1) * 2); dt.Utf16LE(plugin, true);

            ByteBuf flp;
            flp.Str("FLhd"); flp.U32LE(6); flp.U16LE(0); flp.U16LE(4); flp.U16LE(96);
            flp.Str("FLdt"); flp.U32LE(static_cast<uint32_t>(dt.b.size()));
            flp.Bytes(dt.b.data(), dt.b.size());
            return flp.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .flp (FL Studio TLV Events)", L".flp",
            makeFlp("21.2.0", 128000, L"Festival Anthem", L"Sytrus"), L"Sytrus",
            makeFlp("20.9.2", 140500, L"LoFi Chillhop", L"Harmless"), L"Harmless",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .gp (Guitar Pro 7/8 GPIF ZIP)", L".gp",
        BuildStoredZipSingle("Content/score.gpif",
            "<GPIF><GPVersion>8.0</GPVersion><Score><Title>Cliffs of Dover</Title><Artist>Eric Johnson</Artist></Score>"
            "<Tracks><Track id=\"0\"><Name>Lead Strat</Name><Pitches>40 45 50 55 59 64</Pitches></Track></Tracks>"
            "<MasterBars><MasterBar/><MasterBar/></MasterBars></GPIF>"),
        L"Lead Strat",
        BuildStoredZipSingle("Content/score.gpif",
            "<GPIF><GPVersion>7.6</GPVersion><Score><Title>Classical Gas</Title><Artist>Mason Williams</Artist></Score>"
            "<Tracks><Track id=\"0\"><Name>Nylon Acoustic</Name><Pitches>40 45 50 55 59 64</Pitches></Track></Tracks>"
            "<MasterBars><MasterBar/></MasterBars></GPIF>"),
        L"Nylon Acoustic",
        kGarbage);

    {
        auto makeGp5 = [](std::string_view title, std::string_view artist) {
            ByteBuf g;
            std::string ver = "FICHIER GUITAR PRO v5.00";
            g.U8(static_cast<uint8_t>(ver.size()));
            g.FixedStr(ver, 30);
            auto putGpStr = [&](std::string_view s) {
                g.U32LE(static_cast<uint32_t>(s.size() + 1));
                g.U8(static_cast<uint8_t>(s.size()));
                g.Str(s);
            };
            putGpStr(title);
            putGpStr("");
            putGpStr(artist);
            putGpStr("Studio Album");
            putGpStr("");
            putGpStr("");
            putGpStr("");
            putGpStr("");
            putGpStr("");
            g.U32LE(0); // 0 notices
            return g.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .gp5 (Guitar Pro 5 Binary Score Header)", L".gp5",
            makeGp5("Glasgow Kiss", "John Petrucci"), L"Glasgow Kiss",
            makeGp5("Satch Boogie", "Joe Satriani"), L"Satch Boogie",
            kGarbage);
    }

    {
        std::string rppA = "<REAPER_PROJECT 0.1 \"7.11/win64\" 1700000000\n  TEMPO 120 4 4\n  <TRACK\n    NAME \"Lead Vocal\"\n    <ITEM\n      <SOURCE WAVE\n        FILE \"vox_take1.wav\"\n      >\n    >\n  >\n>\n";
        std::string rppB = "<REAPER_PROJECT 0.1 \"7.12/win64\" 1700000100\n  TEMPO 95 6 8\n  <TRACK\n    NAME \"Bass DI\"\n    <FXCHAIN\n      <VST \"ReaComp (Cockos)\"\n      >\n    >\n  >\n>\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .rpp (REAPER Project S-Expressions)", L".rpp",
            std::vector<uint8_t>(rppA.begin(), rppA.end()), L"vox_take1.wav",
            std::vector<uint8_t>(rppB.begin(), rppB.end()), L"ReaComp",
            kGarbage);
    }

    {
        std::string edlA = "TITLE: DOCUMENTARY_CUT_V1\n001  CAM_A    V     C        01:00:00:00 01:00:05:12 00:00:00:00 00:00:05:12\n* FROM CLIP NAME: INTERVIEW_WIDE.MOV\n";
        std::string edlB = "TITLE: COMMERCIAL_30S\n001  REEL_09  V     D    024 02:10:00:00 02:10:03:00 00:00:00:00 00:00:03:00\n* FROM CLIP NAME: PRODUCT_HERO.R3D\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .edl (CMX 3600 Timeline)", L".edl",
            std::vector<uint8_t>(edlA.begin(), edlA.end()), L"INTERVIEW_WIDE.MOV",
            std::vector<uint8_t>(edlB.begin(), edlB.end()), L"PRODUCT_HERO.R3D",
            kGarbage);
    }

    {
        std::string fcpA = "<?xml version=\"1.0\"?><fcpxml version=\"1.10\"><resources><format id=\"r1\" name=\"FFVideoFormat1080p24\" width=\"1920\" height=\"1080\"/><asset id=\"r2\" name=\"DroneShot\" src=\"file:///drone.mov\" duration=\"240/24s\"/></resources><library><asset-clip name=\"Opening Aerial\" ref=\"r2\" offset=\"0s\" duration=\"120/24s\"/></library></fcpxml>";
        std::string fcpB = "<?xml version=\"1.0\"?><fcpxml version=\"1.11\"><resources><format id=\"r1\" name=\"FFVideoFormat4K60\" width=\"3840\" height=\"2160\"/><asset id=\"r2\" name=\"StudioDialog\" src=\"file:///dialog.wav\" duration=\"600/60s\"/></resources><library><asset-clip name=\"Host Intro\" ref=\"r2\" offset=\"10s\" duration=\"300/60s\"/></library></fcpxml>";
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .fcpxml (Final Cut Pro XML)", L".fcpxml",
            std::vector<uint8_t>(fcpA.begin(), fcpA.end()), L"Opening Aerial",
            std::vector<uint8_t>(fcpB.begin(), fcpB.end()), L"Host Intro",
            kGarbage);
    }

    {
        std::string otioA = "{\"OTIO_SCHEMA\":\"Timeline.1\",\"name\":\"Episode1\",\"tracks\":{\"OTIO_SCHEMA\":\"Stack.1\",\"children\":[{\"OTIO_SCHEMA\":\"Track.1\",\"name\":\"V1\",\"kind\":\"Video\",\"children\":[{\"OTIO_SCHEMA\":\"Clip.1\",\"name\":\"Scene01_Take2\",\"media_reference\":{\"OTIO_SCHEMA\":\"ExternalReference.1\",\"target_url\":\"file:///scene01.mov\"},\"source_range\":{\"OTIO_SCHEMA\":\"TimeRange.1\",\"start_time\":{\"value\":0,\"rate\":24},\"duration\":{\"value\":120,\"rate\":24}}}]}]}}";
        std::string otioB = "{\"OTIO_SCHEMA\":\"Timeline.1\",\"name\":\"TrailerCut\",\"tracks\":{\"OTIO_SCHEMA\":\"Stack.1\",\"children\":[{\"OTIO_SCHEMA\":\"Track.1\",\"name\":\"A1\",\"kind\":\"Audio\",\"children\":[{\"OTIO_SCHEMA\":\"Clip.1\",\"name\":\"TrailerBoom\",\"media_reference\":{\"OTIO_SCHEMA\":\"ExternalReference.1\",\"target_url\":\"file:///boom_sfx.wav\"},\"source_range\":{\"OTIO_SCHEMA\":\"TimeRange.1\",\"start_time\":{\"value\":48,\"rate\":48},\"duration\":{\"value\":960,\"rate\":48}}}]}]}}";
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .otio (OpenTimelineIO JSON)", L".otio",
            std::vector<uint8_t>(otioA.begin(), otioA.end()), L"Scene01_Take2",
            std::vector<uint8_t>(otioB.begin(), otioB.end()), L"TrailerBoom",
            kGarbage);
    }

    {
        auto makeSup = [](uint16_t w, uint16_t h, uint32_t pts90k, uint16_t bmpW, uint16_t bmpH) {
            ByteBuf s;
            // PCS (0x16)
            s.Str("PG"); s.U32BE(pts90k); s.U32BE(0); s.U8(0x16); s.U16BE(11);
            s.U16BE(w); s.U16BE(h); s.U8(0x10); s.U16BE(1); s.U8(0x80); s.U8(0); s.U8(0); s.U8(1);
            // ODS (0x15)
            s.Str("PG"); s.U32BE(pts90k); s.U32BE(0); s.U8(0x15); s.U16BE(11);
            s.U16BE(0); s.U8(0); s.U8(0xC0); s.U8(0); s.U16BE(4); s.U16BE(bmpW); s.U16BE(bmpH);
            // END (0x80)
            s.Str("PG"); s.U32BE(pts90k); s.U32BE(0); s.U8(0x80); s.U16BE(0);
            return s.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .sup (Blu-ray PGS Segments)", L".sup",
            makeSup(1920, 1080, 90000 * 12, 640, 82), L"640x82",
            makeSup(3840, 2160, 90000 * 45, 920, 120), L"920x120",
            kGarbage);
    }

    {
        std::string idxA = "# VobSub index file, v7\nsize: 720x480\nid: en, index: 0\ntimestamp: 00:01:12:340, filepos: 00001800\n";
        std::string idxB = "# VobSub index file, v7\nsize: 720x576\nid: fr, index: 1\ntimestamp: 00:05:44:120, filepos: 0009a000\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .idx (DVD VobSub Index)", L".idx",
            std::vector<uint8_t>(idxA.begin(), idxA.end()), L"720x480",
            std::vector<uint8_t>(idxB.begin(), idxB.end()), L"0009a000",
            kGarbage);

        std::string subA = "{120}{180}Welcome aboard the starship.|Prepare for launch.\n{200}{260}All engines nominal.\n";
        std::string subB = "{50}{110}Encrypted transmission received.\n{140}{210}Decoding coordinates now.\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .sub (MicroDVD Frame Subtitles)", L".sub",
            std::vector<uint8_t>(subA.begin(), subA.end()), L"starship",
            std::vector<uint8_t>(subB.begin(), subB.end()), L"Encrypted transmission",
            kGarbage);
    }

    // 6. E-Books & Help (.mobi PalmDOC LZ77, .djvu, .chm)
    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .mobi (PalmDB + EXTH + Real PalmDOC LZ77 Decompression)", L".mobi",
        MakeMobiBook("Orbital Mechanics Primer", "Kepler Press",
            "<html><body><h1>Chapter One</h1><p>The elliptical trajectory transfers spacecraft momentum across orbital perigee.</p></body></html>"),
        L"spacecraft momentum",
        MakeMobiBook("Distributed Consensus Systems", "Lamport House",
            "<html><body><h1>Paxos Protocol</h1><p>Quorum intersection guarantees replicated state machine safety under network partitions.</p></body></html>"),
        L"replicated state machine",
        kGarbage);

    {
        auto makeDjvu = [](uint16_t w, uint16_t h, uint16_t dpi, std::string_view ocrText) {
            ByteBuf txta;
            uint32_t tl = static_cast<uint32_t>(ocrText.size());
            txta.U8(static_cast<uint8_t>((tl >> 16) & 0xFFu));
            txta.U8(static_cast<uint8_t>((tl >> 8) & 0xFFu));
            txta.U8(static_cast<uint8_t>(tl & 0xFFu));
            txta.Str(ocrText);

            ByteBuf d;
            d.Str("AT&TFORM");
            uint32_t formSz = static_cast<uint32_t>(4 + 18 + 8 + txta.b.size() + (txta.b.size() & 1u));
            d.U32BE(formSz);
            d.Str("DJVU");
            d.Str("INFO"); d.U32BE(10);
            d.U16BE(w); d.U16BE(h); d.U8(24); d.U8(0); d.U16LE(dpi); d.U8(22); d.U8(1);
            d.Str("TXTa"); d.U32BE(static_cast<uint32_t>(txta.b.size()));
            d.Bytes(txta.b.data(), txta.b.size());
            if (txta.b.size() & 1u) d.U8(0);
            return d.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .djvu (AT&TFORM INFO & TXTa OCR)", L".djvu",
            makeDjvu(2480, 3508, 300, "Theorem 4.2: Every compact manifold admits a smooth partition."), L"300 DPI",
            makeDjvu(1654, 2339, 600, "Appendix B: Schematic pinout for transceiver module."), L"600 DPI",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .chm (ITSF/ITSP/PMGL ENCINT Directory)", L".chm",
        MakeChm("/reference/kernel_api.htm", "<html><body>Kernel API Reference Manual</body></html>"), L"kernel_api.htm",
        MakeChm("/tutorials/shader_graph.htm", "<html><body>PBR Shader Graph Guide</body></html>"), L"shader_graph.htm",
        kGarbage);

    // Topics in the LZX section are decoded: titles and text of a page from a verbatim block and one from a stored block
    {
        const std::vector<uint8_t> lzxChm = MakeChmLzx(
            "<html><head><title>Introduction &amp; Setup</title></head><body><p>Explorer2 help body text.</p></body></html>",
            "<html><head><title>Details</title></head><body><p>Stored block topic.</p></body></html>");
        std::wstring lzxPath = dir + L"\\lzx_topics.chm";
        PathUtils::WriteFileBytes(lzxPath, lzxChm.data(), lzxChm.size());
        ParsedOfficeDocument dChm;
        bool lzxOk = SpreadsheetDocViewer::InspectOfficeFile(lzxPath, dChm) && dChm.valid && !dChm.sheets.empty() &&
                     dChm.sheets[0].name == L"CHM_Topics" && dChm.sheets[0].rows.size() == 3 &&
                     dChm.sheets[0].rows[1][1] == L"Introduction & Setup" &&
                     dChm.sheets[0].rows[1][2].find(L"Explorer2 help body text.") != std::wstring::npos &&
                     dChm.sheets[0].rows[2][1] == L"Details" && dChm.sheets[0].rows[2][2].find(L"Stored block topic.") != std::wstring::npos;
        DeleteFileW(lzxPath.c_str());
        tc.Record(lzxOk, L"DocsSystem.Chm.LzxSectionTopicsDecoded",
                  dChm.sheets.empty() ? std::wstring(L"no sheets") : dChm.sheets[0].name + L" rows=" + std::to_wstring(dChm.sheets[0].rows.size()));
    }

    // 7. System, Forensics, Runtime & Security (.dll, .lnk, .pf, .evtx, .pdb, .class, .dex, .macho, .cer, .pem, .kdbx, .pcapng, .uf2)
    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .dll (PE32+ Import & Export Tables)", L".dll",
        MakePe64WithImpExp("KERNEL32.dll", "CreateFileW", "InitializePhysicsEngine"), L"InitializePhysicsEngine",
        MakePe64WithImpExp("ADVAPI32.dll", "RegOpenKeyExW", "CompileAudioGraph"), L"CompileAudioGraph",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .lnk (MS-SHLLINK Target & Arguments)", L".lnk",
        MakeShellLink("C:\\Tools\\Compiler\\clang-cl.exe", L"--target=x86_64-pc-windows-msvc -O3"), L"clang-cl.exe",
        MakeShellLink("D:\\Studio\\Blender\\blender.exe", L"--background scene.blend --render-frame 1"), L"blender.exe",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .pf (Windows Prefetch SCCA v30)", L".pf",
        MakePrefetchV30(L"POWERSHELL.EXE", 0xAABB1122u, 42, L"\\VOLUME{01}\\WINDOWS\\SYSTEM32\\KERNELBASE.DLL"), L"POWERSHELL.EXE",
        MakePrefetchV30(L"MSBUILD.EXE", 0xCCDD3344u, 19, L"\\VOLUME{01}\\PROGRAM FILES\\DOTNET\\HOSTFXR.DLL"), L"MSBUILD.EXE",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .evtx (Windows Event Log BinXML)", L".evtx",
        MakeEvtx(1001, L"Service Control Manager started ExplorerIndexingSvc"), L"ExplorerIndexingSvc",
        MakeEvtx(2048, L"Kerberos ticket renewal succeeded for workstation07"), L"workstation07",
        kGarbage);

    {
        auto makeMinidump = [](uint32_t excCode, std::wstring_view modPath) {
            ByteBuf d;
            d.Str("MDMP");
            d.U16LE(0xA793); d.U16LE(0);
            d.U32LE(2);  // 2 streams: ExceptionStream(6) + ModuleListStream(4)
            d.U32LE(32); // StreamDirectoryRva = 32
            d.U32LE(0);
            d.U32LE(1700000000u);
            d.U64LE(0);
            // Directory [0] @ 32: ExceptionStream(6), len=32, rva=56
            d.U32LE(6); d.U32LE(32); d.U32LE(56);
            // Directory [1] @ 44: ModuleListStream(4), len=112, rva=88
            d.U32LE(4); d.U32LE(112); d.U32LE(88);
            // ExceptionStream @ 56 (32B)
            d.U32LE(4120); d.U32LE(0); d.U32LE(excCode); d.U32LE(0); d.U64LE(0); d.U64LE(0x7FF612340000ULL);
            // ModuleListStream @ 88 (4 + 108 = 112B)
            d.U32LE(1);
            d.U64LE(0x7FF600000000ULL);
            d.U32LE(0x84000);
            d.U32LE(0); d.U32LE(0);
            d.U32LE(200); // ModuleNameRva = 200
            d.PadTo(200);
            // MINIDUMP_STRING @ 200
            d.U32LE(static_cast<uint32_t>(modPath.size() * 2));
            d.Utf16LE(modPath, true);
            return d.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .dmp (Windows Minidump Exception & Modules)", L".dmp",
            makeMinidump(0xC0000005u, L"C:\\Program Files\\App\\RenderCore.dll"), L"RenderCore.dll",
            makeMinidump(0xC0000094u, L"C:\\Windows\\System32\\dxgi.dll"), L"dxgi.dll",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .pdb (MSF v7.00 GUID & DBI Modules)", L".pdb",
        MakePdbMsf7(0xA1, "W:\\build\\obj\\RendererD3D12.obj"), L"RendererD3D12.obj",
        MakePdbMsf7(0xB2, "W:\\build\\obj\\AudioMixerWasapi.obj"), L"AudioMixerWasapi.obj",
        kGarbage);

    // A PDB larger than the 32 MB a reader is handed, its block map, stream directory and streams past that (large
    // PDBs keep the directory near the end): all read from the file, and the title does not claim only the start
    // was read
    {
        std::vector<uint8_t> big = MakePdbMsf7(0xC3, "W:\\build\\obj\\FarAwayModule.obj", 67600); // 33 MB in
        std::wstring bigPath = dir + L"\\far_streams.pdb";
        PathUtils::WriteFileBytes(bigPath, big.data(), big.size());
        big.clear();
        big.shrink_to_fit();
        ParsedOfficeDocument dBig;
        bool bigOk = SpreadsheetDocViewer::InspectOfficeFile(bigPath, dBig) && dBig.valid &&
                     DocContainsToken(dBig, L"FarAwayModule.obj") && DocContainsToken(dBig, L"x64 / AMD64") &&
                     dBig.formatTitle.find(L"only the first") == std::wstring::npos;
        DeleteFileW(bigPath.c_str());
        tc.Record(bigOk, L"DocsSystem.Pdb.Over32MbStreamsReadFromTheFile", dBig.formatTitle);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .class (JVM Constant Pool & Methods)", L".class",
        MakeJavaClass("com/explorer2/OrderMatcher", "matchLimitOrder"), L"matchLimitOrder",
        MakeJavaClass("org/analytics/TensorKernel", "dispatchMatMul"), L"dispatchMatMul",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .dex (Dalvik Strings, Classes & Methods)", L".dex",
        MakeDex("Lcom/app/AuthManager;", "verifyPasskey"), L"verifyPasskey",
        MakeDex("Lcom/app/SyncWorker;", "uploadDeltaBatch"), L"uploadDeltaBatch",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .macho (Mach-O 64 Load Commands & Symtab)", L".macho",
        MakeMachO64(0x0100000Cu, "/usr/lib/libSystem.B.dylib", "_metal_encode_frame"), L"_metal_encode_frame",
        MakeMachO64(0x01000007u, "/System/Library/Frameworks/CoreAudio.framework/CoreAudio", "_audio_unit_render"), L"_audio_unit_render",
        kGarbage);

    // A universal binary larger than the 32 MB a reader is handed, its second slice past that: the slice's load
    // commands and symbols are read from the file, and the title does not claim only the start was read
    {
        const std::vector<uint8_t> nearSlice = MakeMachO64(0x0100000Cu, "/usr/lib/libSystem.B.dylib", "_near_symbol");
        const std::vector<uint8_t> farSlice = MakeMachO64(0x01000007u, "/usr/lib/libc++.1.dylib", "_far_symbol");
        const uint32_t nearOff = 4096, farOff = 33u * 1024 * 1024;
        ByteBuf fat;
        fat.U32BE(0xCAFEBABEu);
        fat.U32BE(2);
        fat.U32BE(0x0100000Cu); fat.U32BE(0); fat.U32BE(nearOff); fat.U32BE(static_cast<uint32_t>(nearSlice.size())); fat.U32BE(12);
        fat.U32BE(0x01000007u); fat.U32BE(3); fat.U32BE(farOff); fat.U32BE(static_cast<uint32_t>(farSlice.size())); fat.U32BE(12);
        fat.PadTo(nearOff);
        fat.Bytes(nearSlice.data(), nearSlice.size());
        fat.PadTo(farOff);
        fat.Bytes(farSlice.data(), farSlice.size());
        std::wstring fatPath = dir + L"\\universal.macho";
        PathUtils::WriteFileBytes(fatPath, fat.b.data(), fat.b.size());
        fat.b.clear();
        fat.b.shrink_to_fit();
        ParsedOfficeDocument dFat;
        bool fatOk = SpreadsheetDocViewer::InspectOfficeFile(fatPath, dFat) && dFat.valid &&
                     DocContainsToken(dFat, L"_near_symbol") && DocContainsToken(dFat, L"_far_symbol") &&
                     DocContainsToken(dFat, L"libc++.1.dylib") && dFat.formatTitle.find(L"only the first") == std::wstring::npos;
        DeleteFileW(fatPath.c_str());
        tc.Record(fatOk, L"DocsSystem.MachO.Over32MbUniversalSliceReadFromTheFile", dFat.formatTitle);
    }

    {
        auto derA = MakeX509Der(0x11, "api.alpha-cloud.internal", "api.alpha-cloud.internal");
        auto derB = MakeX509Der(0x77, "vault.beta-sec.internal", "vault.beta-sec.internal");
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .cer (X.509 v3 DER Certificate + SAN)", L".cer",
            derA, L"api.alpha-cloud.internal",
            derB, L"vault.beta-sec.internal",
            kGarbage);
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .pem (PEM Armored X.509 Certificate)", L".pem",
            WrapDerAsPem(derA, "CERTIFICATE"), L"api.alpha-cloud.internal",
            WrapDerAsPem(derB, "CERTIFICATE"), L"vault.beta-sec.internal",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .kdbx (KeePass v4 Cipher & Argon2id Params)", L".kdbx",
        MakeKdbx4(false, 64ULL * 1024 * 1024, 12), L"AES-256",
        MakeKdbx4(true, 256ULL * 1024 * 1024, 32), L"ChaCha20",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsSystem .pcapng (Ethernet/IPv4/TCP HTTP vs UDP DNS)", L".pcapng",
        MakePcapNg(false), L"HTTP",
        MakePcapNg(true), L"api.explorer2.local",
        kGarbage);

    {
        auto makeUf2 = [](uint32_t familyId, uint32_t targetAddr, std::string_view fwTag) {
            ByteBuf u;
            u.U32LE(0x0A324655u); // "UF2\n"
            u.U32LE(0x9E5D5157u);
            u.U32LE(0x00002000u); // familyID present
            u.U32LE(targetAddr);
            u.U32LE(256);
            u.U32LE(0);
            u.U32LE(1);
            u.U32LE(familyId);
            u.FixedStr(fwTag, 256);
            u.PadTo(508);
            u.U32LE(0x0AB16F30u);
            return u.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .uf2 (UF2 Firmware Family & Flash Range)", L".uf2",
            makeUf2(0xE48BFF56u, 0x10000000u, "RP2040_BLINK_V1"), L"RP2040",
            makeUf2(0xC47E5767u, 0x00010000u, "ESP32S3_WIFI_V2"), L"ESP32-S3",
            kGarbage);
    }

    // 8. Stage 15 Deep Decoders: Apple iWork (.pages/.numbers), RTF (.rtf), Classic PCAP (.pcap),
    //    Standard MIDI (.mid), BitTorrent (.torrent), and Windows ETW Trace (.etl)
    {
        auto writeTempAndInspect = [&](const std::wstring& fileName, const std::vector<uint8_t>& bytes, ParsedOfficeDocument& outDoc) -> bool {
            std::wstring p = dir + L"\\" + fileName;
            PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
            bool ok = SpreadsheetDocViewer::InspectOfficeFile(p, outDoc) && outDoc.valid;
            DeleteFileW(p.c_str());
            return ok;
        };

        // 8A. Apple iWork '13+ (.pages & .numbers) with real Snappy LZ77 backreference compression + TSP::ArchiveInfo Protobuf
        auto snappyCompressWithBackref = [](const std::vector<uint8_t>& plain, size_t backrefPos, size_t backrefDist, size_t backrefLen) {
            ByteBuf sn;
            sn.Uleb(plain.size());
            auto emitLiteral = [&](const uint8_t* ptr, size_t len) {
                if (len == 0) return;
                const uint32_t lm1 = static_cast<uint32_t>(len - 1);
                if (lm1 < 60u) {
                    sn.U8(static_cast<uint8_t>(lm1 << 2));
                } else {
                    sn.U8(static_cast<uint8_t>(60u << 2));
                    sn.U8(static_cast<uint8_t>(lm1 & 0xFFu));
                }
                sn.Bytes(ptr, len);
            };
            if (backrefLen >= 4 && backrefLen <= 11 && backrefDist > 0 && backrefDist <= backrefPos && backrefPos + backrefLen <= plain.size()) {
                emitLiteral(plain.data(), backrefPos);
                // Snappy Copy-1 tag: tagType=1, len-4 in bits 2..4, offset[8..10] in bits 5..7 + offset[0..7]
                const uint8_t tag = static_cast<uint8_t>(0x01u | (((backrefLen - 4u) & 0x07u) << 2) | (((backrefDist >> 8) & 0x07u) << 5));
                sn.U8(tag);
                sn.U8(static_cast<uint8_t>(backrefDist & 0xFFu));
                emitLiteral(plain.data() + backrefPos + backrefLen, plain.size() - (backrefPos + backrefLen));
            } else {
                emitLiteral(plain.data(), plain.size());
            }

            // Frame inside .iwa chunk: [0xFF][6,0,0]"sNaPpY" + [0x00][u24_le len][snappy block]
            ByteBuf iwa;
            iwa.U8(0xFFu); iwa.U8(6); iwa.U8(0); iwa.U8(0); iwa.Str("sNaPpY");
            const uint32_t cLen = static_cast<uint32_t>(sn.b.size());
            iwa.U8(0x00u);
            iwa.U8(static_cast<uint8_t>(cLen & 0xFFu));
            iwa.U8(static_cast<uint8_t>((cLen >> 8) & 0xFFu));
            iwa.U8(static_cast<uint8_t>((cLen >> 16) & 0xFFu));
            iwa.Bytes(sn.b.data(), sn.b.size());
            return iwa.b;
        };

        auto makeIwaArchiveStream = [](uint32_t msgType, const std::vector<uint8_t>& msgPayload) {
            ByteBuf mi; // TSP::MessageInfo { 1: type, 3: length }
            mi.U8((1u << 3) | 0u); mi.Uleb(msgType);
            mi.U8((3u << 3) | 0u); mi.Uleb(msgPayload.size());

            ByteBuf ai; // TSP::ArchiveInfo { 1: identifier=42, 2: message_infos }
            ai.U8((1u << 3) | 0u); ai.Uleb(42);
            ai.U8((2u << 3) | 2u); ai.Uleb(mi.b.size()); ai.Bytes(mi.b.data(), mi.b.size());

            ByteBuf stream;
            stream.Uleb(ai.b.size());
            stream.Bytes(ai.b.data(), ai.b.size());
            stream.Bytes(msgPayload.data(), msgPayload.size());
            return stream.b;
        };

        // Pages TSWP::StorageArchive (type 2001): field 3 = "KeynoteVision_2026_KeynoteVision"
        // Compress second "KeynoteVision" (10 bytes "KeynoteVis") as an LZ77 backreference so raw ASCII scan CANNOT find the full string!
        std::string pagesBody = "KeynoteVision_2026_KeynoteVision";
        ByteBuf pagesMsg;
        pagesMsg.U8((3u << 3) | 2u); pagesMsg.Uleb(pagesBody.size()); pagesMsg.Str(pagesBody);
        auto pagesRawStream = makeIwaArchiveStream(2001u, pagesMsg.b);
        size_t secondOcc = std::string_view(reinterpret_cast<const char*>(pagesRawStream.data()), pagesRawStream.size()).rfind("KeynoteVision");
        auto pagesIwa = snappyCompressWithBackref(pagesRawStream, secondOcc, 19, 10);
        std::string pagesIwaStr(reinterpret_cast<const char*>(pagesIwa.data()), pagesIwa.size());
        auto pagesZip = BuildStoredZipSingle("Index/Document.iwa", pagesIwaStr);

        // Numbers TST::TableDataList (type 6005): field 3 = ListEntry { 1: key=1, 3: string="Quarterly_EBITDA" }, { 1: key=2, 6: double=9420.5 }
        ByteBuf ent1;
        ent1.U8((1u << 3) | 0u); ent1.Uleb(1);
        ent1.U8((3u << 3) | 2u); ent1.Uleb(16); ent1.Str("Quarterly_EBITDA");
        ByteBuf ent2;
        ent2.U8((1u << 3) | 0u); ent2.Uleb(2);
        ent2.U8((6u << 3) | 1u);
        double numVal = 9420.5;
        uint64_t numBits = 0; std::memcpy(&numBits, &numVal, 8);
        ent2.U64LE(numBits);
        ByteBuf numMsg;
        numMsg.U8((3u << 3) | 2u); numMsg.Uleb(ent1.b.size()); numMsg.Bytes(ent1.b.data(), ent1.b.size());
        numMsg.U8((3u << 3) | 2u); numMsg.Uleb(ent2.b.size()); numMsg.Bytes(ent2.b.data(), ent2.b.size());
        auto numRawStream = makeIwaArchiveStream(6005u, numMsg.b);
        auto numIwa = snappyCompressWithBackref(numRawStream, 0, 0, 0);
        std::string numIwaStr(reinterpret_cast<const char*>(numIwa.data()), numIwa.size());
        auto numZip = BuildStoredZipSingle("Index/Tables/DataList.iwa", numIwaStr);

        // Anti-cheat negative test: plain ZIP renamed to .pages without any .iwa or iWork '09 index.xml must fail!
        auto plainNonIworkZip = BuildStoredZipSingle("notes/hello.txt", "Not an Apple Pages document");

        ParsedOfficeDocument pgDoc, nmDoc, badPgDoc;
        bool okPages   = writeTempAndInspect(L"spec_test.pages", pagesZip, pgDoc);
        bool okNumbers = writeTempAndInspect(L"spec_test.numbers", numZip, nmDoc);
        bool badPgFail = !writeTempAndInspect(L"plain_zip_cheat.pages", plainNonIworkZip, badPgDoc);
        tc.Record(okPages && okNumbers && badPgFail &&
                  DocContainsToken(pgDoc, L"KeynoteVision_2026_KeynoteVision") &&
                  DocContainsToken(nmDoc, L"Quarterly_EBITDA") &&
                  DocContainsToken(nmDoc, L"9420.5"),
            L"DocsSystem .pages/.numbers (iWork Snappy LZ77 .iwa + TSP Protobuf & Plain-ZIP Reject)");

        // 8B. Rich Text Format (.rtf) — Control-Word State Stack, CP1250 + FontTbl fcharset204, UTF-16 Surrogates, Destination Skip & Tables
        std::string rtfGood =
            "{\\rtf1\\ansi\\ansicpg1250\\uc2"
            "{\\fonttbl{\\f0\\fswiss\\fcharset238 Helvetica;}{\\f1\\froman\\fcharset204 Times New Roman;}}"
            "{\\colortbl;\\red255\\green0\\blue0;}"
            "{\\*\\generator HiddenGeneratorNoise;}"
            "\\pard\\f0 Central European: \\'af\\'f3\\'b3\\'e6\\par\n"
            "{\\f1 Cyrillic: \\'cc\\'e8\\'f0}\\par\n"
            "Emoji Rocket: \\u-10179??\\u-8576?? Ready\\par\n"
            "{\\field{\\*\\fldinst HYPERLINK \"https://hidden.secret.url\"}{\\fldrslt VisibleFieldLabel}}\\par\n"
            "\\trowd\\cellx2000\\cellx4000\\intbl SensorA\\cell 99.4 K\\cell\\row\n"
            "}";
        std::string rtfNotRtf = "Plain text pretending to be RTF without header";
        ParsedOfficeDocument rtfDoc, rtfBadDoc;
        bool okRtf = writeTempAndInspect(L"rich_cp1250.rtf", std::vector<uint8_t>(rtfGood.begin(), rtfGood.end()), rtfDoc);
        bool badRtfFail = !writeTempAndInspect(L"not_rtf.rtf", std::vector<uint8_t>(rtfNotRtf.begin(), rtfNotRtf.end()), rtfBadDoc);
        tc.Record(okRtf && badRtfFail &&
                  DocContainsToken(rtfDoc, L"\x017B\x00F3\x0142\x0107") && // "Żółć" decoded via Windows-1250
                  DocContainsToken(rtfDoc, L"\x041C\x0438\x0440") &&         // "Мир" decoded via \f1 \fcharset204 (Windows-1251)!
                  DocContainsToken(rtfDoc, L"VisibleFieldLabel") &&
                  !DocContainsToken(rtfDoc, L"Helvetica") &&
                  !DocContainsToken(rtfDoc, L"HiddenGeneratorNoise") &&
                  !DocContainsToken(rtfDoc, L"hidden.secret.url") &&
                  DocContainsToken(rtfDoc, L"SensorA | 99.4 K"),
            L"DocsSystem .rtf (State Stack, CP1250, Surrogate Pairs, Destination Skip & Tables)");

        // 8C. Classic libpcap (.pcap) — Full L2-L7 Dissector (Ethernet + IPv4 + TCP HTTP & UDP DNS)
        auto makeClassicPcap = [](bool dnsFrame) {
            ByteBuf p;
            p.U32LE(0xA1B2C3D4u); // classic microsecond LE magic
            p.U16LE(2); p.U16LE(4);
            p.U32LE(0); p.U32LE(0);
            p.U32LE(65535);
            p.U32LE(1); // LINKTYPE_ETHERNET = 1

            ByteBuf frm;
            frm.Bytes("\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\x08\x00", 14);
            if (!dnsFrame) {
                std::string http = "GET /v1/telemetry HTTP/1.1\r\nHost: edge.local\r\n\r\n";
                uint16_t ipLen = static_cast<uint16_t>(20 + 20 + http.size());
                frm.U8(0x45); frm.U8(0); frm.U16BE(ipLen);
                frm.U16BE(10); frm.U16BE(0);
                frm.U8(64); frm.U8(6); frm.U16BE(0);
                frm.Bytes("\x0A\x01\x02\x03\x0A\x09\x08\x07", 8);
                frm.U16BE(49152); frm.U16BE(80);
                frm.U32BE(100); frm.U32BE(200);
                frm.U8(0x50); frm.U8(0x18); // PSH+ACK
                frm.U16BE(4096); frm.U16BE(0); frm.U16BE(0);
                frm.Str(http);
            } else {
                ByteBuf dns;
                dns.U16BE(0xCAFEu); dns.U16BE(0x0100u);
                dns.U16BE(1); dns.U16BE(0); dns.U16BE(0); dns.U16BE(0);
                dns.U8(9); dns.Str("telemetry");
                dns.U8(4); dns.Str("corp");
                dns.U8(5); dns.Str("local");
                dns.U8(0);
                dns.U16BE(1); dns.U16BE(1);
                uint16_t udpLen = static_cast<uint16_t>(8 + dns.b.size());
                uint16_t ipLen  = static_cast<uint16_t>(20 + udpLen);
                frm.U8(0x45); frm.U8(0); frm.U16BE(ipLen);
                frm.U16BE(11); frm.U16BE(0);
                frm.U8(64); frm.U8(17); frm.U16BE(0);
                frm.Bytes("\xC0\xA8\x0A\x05\x01\x01\x01\x01", 8);
                frm.U16BE(55111); frm.U16BE(53);
                frm.U16BE(udpLen); frm.U16BE(0);
                frm.Bytes(dns.b.data(), dns.b.size());
            }
            p.U32LE(1700000123u); p.U32LE(456789u);
            p.U32LE(static_cast<uint32_t>(frm.b.size()));
            p.U32LE(static_cast<uint32_t>(frm.b.size()));
            p.Bytes(frm.b.data(), frm.b.size());
            return p.b;
        };
        VerifyPairAndCorrupt(tc, dir, L"DocsSystem .pcap (Classic libpcap L2-L7 HTTP & DNS Dissector)", L".pcap",
            makeClassicPcap(false), L"/v1/telemetry",
            makeClassicPcap(true), L"telemetry.corp.local",
            kGarbage);

        // 8D. Standard MIDI (.mid) — Format 1 Multi-Track VLQ, Running Status, Tempo Map & Note-Pair Durations
        {
            ByteBuf trk0; // Conductor track: Name="Conductor", Set Tempo 500000 us/qn (120 BPM), TimeSig 4/4, EoT
            trk0.U8(0); trk0.U8(0xFF); trk0.U8(0x03); trk0.U8(9); trk0.Str("Conductor");
            trk0.U8(0); trk0.U8(0xFF); trk0.U8(0x51); trk0.U8(3); trk0.U8(0x07); trk0.U8(0xA1); trk0.U8(0x20); // 500,000 us/qn = 120 BPM
            trk0.U8(0); trk0.U8(0xFF); trk0.U8(0x58); trk0.U8(4); trk0.U8(4); trk0.U8(2); trk0.U8(24); trk0.U8(8);
            trk0.U8(0); trk0.U8(0xFF); trk0.U8(0x2F); trk0.U8(0);

            ByteBuf trk1; // Lead track: Name="Solo Violin", Program 40 (Violin), Running-Status NoteOn/NoteOff pairs!
            trk1.U8(0); trk1.U8(0xFF); trk1.U8(0x03); trk1.U8(11); trk1.Str("Solo Violin");
            trk1.U8(0); trk1.U8(0xC0); trk1.U8(40); // Program Change Ch1 -> 40 ("Violin")
            trk1.U8(0); trk1.U8(0x90); trk1.U8(60); trk1.U8(105); // Note On C4 vel=105 at tick 0
            trk1.EncInt(480); trk1.U8(60); trk1.U8(0);            // RUNNING STATUS NoteOn vel=0 (NoteOff C4) at tick 480 (0.5000 s)!
            trk1.U8(0); trk1.U8(67); trk1.U8(98);                 // RUNNING STATUS NoteOn G4 vel=98 at tick 480
            trk1.EncInt(960); trk1.U8(67); trk1.U8(0);            // RUNNING STATUS NoteOff G4 at tick 1440 (duration = 1.0000 s, 960 ticks)!
            trk1.U8(0); trk1.U8(0xFF); trk1.U8(0x2F); trk1.U8(0);

            ByteBuf mid;
            mid.Str("MThd"); mid.U32BE(6); mid.U16BE(1); mid.U16BE(2); mid.U16BE(480);
            mid.Str("MTrk"); mid.U32BE(static_cast<uint32_t>(trk0.b.size())); mid.Bytes(trk0.b.data(), trk0.b.size());
            mid.Str("MTrk"); mid.U32BE(static_cast<uint32_t>(trk1.b.size())); mid.Bytes(trk1.b.data(), trk1.b.size());

            // Anti-cheat negative tests: (1) raw 0x90 bytes without MThd, (2) orphan data byte before any status byte in MTrk
            std::vector<uint8_t> raw90Cheat = { 0x90, 0x3C, 0x64, 0x90, 0x40, 0x64, 0x90, 0x43, 0x64, 0x00, 0xFF, 0x2F, 0x00, 0x00 };
            ByteBuf badRunStatus;
            badRunStatus.Str("MThd"); badRunStatus.U32BE(6); badRunStatus.U16BE(0); badRunStatus.U16BE(1); badRunStatus.U16BE(480);
            badRunStatus.Str("MTrk"); badRunStatus.U32BE(4); badRunStatus.U8(0); badRunStatus.U8(60); badRunStatus.U8(100); badRunStatus.U8(0);

            ParsedOfficeDocument midDoc, midBad1, midBad2;
            bool okMid = writeTempAndInspect(L"concerto_fmt1.mid", mid.b, midDoc);
            bool badMid1 = !writeTempAndInspect(L"raw90_cheat.mid", raw90Cheat, midBad1);
            bool badMid2 = !writeTempAndInspect(L"orphan_running.mid", badRunStatus.b, midBad2);
            tc.Record(okMid && badMid1 && badMid2 &&
                      DocContainsToken(midDoc, L"Solo Violin") &&
                      DocContainsToken(midDoc, L"Violin") &&
                      DocContainsToken(midDoc, L"120.00 BPM") &&
                      DocContainsToken(midDoc, L"C4 (60)") &&
                      DocContainsToken(midDoc, L"G4 (67)") &&
                      DocContainsToken(midDoc, L"0.5000 s (480 ticks)") &&
                      DocContainsToken(midDoc, L"1.0000 s (960 ticks)"),
                L"DocsSystem .mid (SMF VLQ, Running Status, Tempo Map, GM Instruments & Note Durations)");
        }

        // 8E. BitTorrent (.torrent) — Recursive Bencode, CNG SHA-1 info_hash Invariant, Magnet URI & Multi-File Manifest
        {
            auto bencStr = [](std::string_view s) {
                return std::to_string(s.size()) + ":" + std::string(s);
            };
            std::string pieces20(20, '\x5A');
            std::string infoMulti =
                "d"
                "5:filesl"
                    "d6:lengthi4096e4:pathl" + bencStr("src") + bencStr("engine.cpp") + "ee"
                    "d6:lengthi1024e4:pathl" + bencStr("README.md") + "ee"
                "e"
                "4:name" + bencStr("Explorer2_SDK") +
                "12:piece lengthi262144e"
                "6:pieces" + bencStr(pieces20) +
                "e";

            std::string torrentA =
                "d"
                "8:announce" + bencStr("https://tracker.opentrackr.org:1337/announce") +
                "7:comment" + bencStr("Release Build A") +
                "13:creation datei1735689600e"
                "4:info" + infoMulti +
                "e";
            // Torrent B has a DIFFERENT outer "comment" but the EXACT SAME "info" dictionary -> SHA-1 info_hash MUST be identical!
            std::string torrentSameInfoDiffComment =
                "d"
                "8:announce" + bencStr("https://tracker.opentrackr.org:1337/announce") +
                "7:comment" + bencStr("Mirror Re-seed Comment Changed") +
                "4:info" + infoMulti +
                "e";

            std::string badNoInfo = "d8:announce15:http://t.local/7:comment4:nonee";
            std::string badLeadZeroInt = "d4:infod6:lengthi03e4:name4:bad112:piece lengthi512e6:pieces" + bencStr(pieces20) + "ee";

            auto extractInfoHash = [](const ParsedOfficeDocument& doc) -> std::wstring {
                for (const auto& s : doc.sheets) {
                    for (const auto& r : s.rows) {
                        if (r.size() >= 2 && r[0] == L"Info Hash (SHA-1)") return r[1];
                    }
                }
                return L"";
            };

            ParsedOfficeDocument torDocA, torDocB, torBad1, torBad2;
            bool okTorA = writeTempAndInspect(L"sdk_a.torrent", std::vector<uint8_t>(torrentA.begin(), torrentA.end()), torDocA);
            bool okTorB = writeTempAndInspect(L"sdk_b.torrent", std::vector<uint8_t>(torrentSameInfoDiffComment.begin(), torrentSameInfoDiffComment.end()), torDocB);
            bool badTor1 = !writeTempAndInspect(L"no_info.torrent", std::vector<uint8_t>(badNoInfo.begin(), badNoInfo.end()), torBad1);
            bool badTor2 = !writeTempAndInspect(L"bad_benc.torrent", std::vector<uint8_t>(badLeadZeroInt.begin(), badLeadZeroInt.end()), torBad2);
            std::wstring hashA = extractInfoHash(torDocA);
            std::wstring hashB = extractInfoHash(torDocB);

            tc.Record(okTorA && okTorB && badTor1 && badTor2 &&
                      hashA.size() == 40 && hashA == hashB &&
                      DocContainsToken(torDocA, L"magnet:?xt=urn:btih:" + hashA) &&
                      DocContainsToken(torDocA, L"src/engine.cpp") &&
                      DocContainsToken(torDocA, L"README.md") &&
                      DocContainsToken(torDocA, L"2025-01-01"),
                L"DocsSystem .torrent (Recursive Bencode, SHA-1 info_hash Invariant, Magnet & Multi-File)");
        }

        // 8F. Windows Event Trace Log (.etl) — WMI_BUFFER_HEADER + TRACE_LOGFILE_HEADER + 64-bit EVENT_HEADER
        {
            ByteBuf etl;
            etl.Zeros(4096);

            // Record 0 @ offset 72: Classic EVENT_TRACE_HEADER (48B) + 64-bit TRACE_LOGFILE_HEADER + UTF-16LE LoggerName & LogFileName
            ByteBuf logHdrPayload;
            logHdrPayload.Zeros(280); // 248 (tailBase for 64-bit ptrSz=8) + 32 = 280B before strings
            PutU32LE(logHdrPayload.b.data() + 0, 4096);
            logHdrPayload.b[4] = 10; // MajorVersion = 10
            logHdrPayload.b[5] = 0;  // MinorVersion = 0
            PutU32LE(logHdrPayload.b.data() + 8,  22631); // Build 22631
            PutU32LE(logHdrPayload.b.data() + 12, 16);    // 16 Logical Processors
            PutU32LE(logHdrPayload.b.data() + 36, 1);     // BuffersWritten = 1
            PutU32LE(logHdrPayload.b.data() + 44, 8);     // PointerSize = 8 (64-bit)
            PutU32LE(logHdrPayload.b.data() + 52, 3800);  // CpuSpeedInMHz = 3800
            logHdrPayload.Utf16LE(L"NT Kernel Logger", true);
            logHdrPayload.Utf16LE(L"C:\\PerfLogs\\BootTrace.etl", true);
            while ((logHdrPayload.b.size() & 7u) != 0) logHdrPayload.U8(0);

            const uint16_t rec0Size = static_cast<uint16_t>(48u + logHdrPayload.b.size());
            uint8_t* r0 = etl.b.data() + 72;
            PutU16LE(r0 + 0, rec0Size);
            r0[2] = 0x00u; // EVENT_TRACE_HEADER
            r0[3] = 0xC0u; // TRACE_HEADER_FLAG | TRACE_HEADER_EVENT_TRACE
            r0[4] = 0x00u; // EVENT_TRACE_TYPE_INFO (logfile header)
            r0[5] = 4;
            PutU32LE(r0 + 8,  100); // TID
            PutU32LE(r0 + 12, 4);   // PID = System(4)
            // TimeStamp = 2025-01-01 00:00:00 UTC FILETIME (133801632000000000)
            PutU32LE(r0 + 16, static_cast<uint32_t>(133801632000000000ULL & 0xFFFFFFFFULL));
            PutU32LE(r0 + 20, static_cast<uint32_t>(133801632000000000ULL >> 32));
            std::memcpy(r0 + 48, logHdrPayload.b.data(), logHdrPayload.b.size());

            // Record 1 @ 72 + rec0Size: 64-bit Vista+ EVENT_HEADER (80B + 16B payload = 96B)
            const size_t r1Off = 72u + rec0Size;
            const uint16_t rec1Size = 96u;
            uint8_t* r1 = etl.b.data() + r1Off;
            PutU16LE(r1 + 0, rec1Size);
            r1[2] = 0x13u; // 64-bit EVENT_HEADER
            r1[3] = 0xC0u; // MarkerFlags = 0xC0
            PutU32LE(r1 + 8,  7412); // ThreadId = 7412
            PutU32LE(r1 + 12, 3180); // ProcessId = 3180
            PutU32LE(r1 + 16, static_cast<uint32_t>(133801632050000000ULL & 0xFFFFFFFFULL));
            PutU32LE(r1 + 20, static_cast<uint32_t>(133801632050000000ULL >> 32));
            // ProviderId = {22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716} (Microsoft-Windows-Kernel-Process)
            PutU32LE(r1 + 24, 0x22FB2CD6u);
            PutU16LE(r1 + 28, 0x0E7Bu);
            PutU16LE(r1 + 30, 0x422Bu);
            const uint8_t provTail[8] = { 0xA0, 0xC7, 0x2F, 0xAD, 0x1F, 0xD0, 0xE7, 0x16 };
            std::memcpy(r1 + 32, provTail, 8);
            PutU16LE(r1 + 40, 42); // EventDescriptor.Id = 42
            r1[44] = 4;            // Level = 4 (Info)
            r1[45] = 1;            // Opcode = 1 (Start)

            const uint32_t validEnd = static_cast<uint32_t>(r1Off + rec1Size);
            PutU32LE(etl.b.data() + 0,  4096);     // WMI_BUFFER_HEADER.BufferSize = 4096
            PutU32LE(etl.b.data() + 4,  validEnd); // SavedOffset
            PutU32LE(etl.b.data() + 48, validEnd); // Offset

            // Anti-cheat negative test: 4096-byte buffer starting with uint32_t(4096) + raw UTF-16LE strings at 72 WITHOUT 0xC0 ETW markers!
            ByteBuf etlFakeScrape;
            etlFakeScrape.Zeros(4096);
            PutU32LE(etlFakeScrape.b.data() + 0, 4096);
            PutU32LE(etlFakeScrape.b.data() + 4, 512);
            PutU32LE(etlFakeScrape.b.data() + 48, 512);
            const wchar_t* fakeStr = L"ScrapedFakeLoggerString_ShouldBeRejected";
            for (size_t i = 0; fakeStr[i] != 0; ++i) {
                PutU16LE(etlFakeScrape.b.data() + 72 + i * 2, static_cast<uint16_t>(fakeStr[i]));
            }

            ParsedOfficeDocument etlDoc, etlFakeDoc;
            bool okEtl = writeTempAndInspect(L"boot_kernel.etl", etl.b, etlDoc);
            bool fakeEtlRejected = !writeTempAndInspect(L"scraped_cheat.etl", etlFakeScrape.b, etlFakeDoc);
            tc.Record(okEtl && fakeEtlRejected &&
                      DocContainsToken(etlDoc, L"NT Kernel Logger") &&
                      DocContainsToken(etlDoc, L"BootTrace.etl") &&
                      DocContainsToken(etlDoc, L"3800 MHz") &&
                      DocContainsToken(etlDoc, L"22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716") &&
                      DocContainsToken(etlDoc, L"7412") &&
                      DocContainsToken(etlDoc, L"3180"),
                L"DocsSystem .etl (WMI_BUFFER_HEADER, TRACE_LOGFILE_HEADER, 64-bit EVENT_HEADER & Cheat Reject)");
        }

        // =====================================================================
        // 9. GIS / Security / Database / E-Book & Office/Data Readers
        // =====================================================================
        {
            // Verify extension registration for newly wired & previously missing formats
            bool extsOk =
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".azw") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".prc") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".djv") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".gp3") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".gp4") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".xlsb") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".pst") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ost") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".idml") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".mht") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".mhtml") &&
                SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".plist");
            tc.Record(extsOk, L"DocsSystem Extension Registration (.azw/.prc/.djv/.gp3/.gp4/.xlsb/.pst/.ost/.idml/.mht/.mhtml/.plist)");

            auto buildStoredZlib = [](const std::vector<uint8_t>& payload) -> std::vector<uint8_t> {
                uint32_t s1 = 1u, s2 = 0u;
                for (uint8_t byte : payload) {
                    s1 = (s1 + byte) % 65521u;
                    s2 = (s2 + s1) % 65521u;
                }
                uint16_t len = static_cast<uint16_t>(payload.size());
                ByteBuf z;
                z.U8(0x78u); z.U8(0x01u); // RFC 1950 zlib header (30721 % 31 == 0)
                z.U8(0x01u);              // RFC 1951 final stored block
                z.U16LE(len);
                z.U16LE(static_cast<uint16_t>(~len));
                if (!payload.empty()) z.Bytes(payload.data(), payload.size());
                z.U32BE((s2 << 16) | s1); // Adler-32 BE
                return z.b;
            };

            auto buildStoredZipMulti = [](const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries) -> std::vector<uint8_t> {
                struct CdItem { std::string path; uint32_t crc; uint32_t sz; uint32_t lhoff; };
                std::vector<CdItem> cd;
                ByteBuf z;
                for (const auto& [path, data] : entries) {
                    uint32_t crc = data.empty() ? 0u : Checksum::Crc32(data.data(), data.size());
                    uint32_t sz  = static_cast<uint32_t>(data.size());
                    uint32_t off = static_cast<uint32_t>(z.b.size());
                    z.U32LE(0x04034B50u);
                    z.U16LE(20); z.U16LE(0); z.U16LE(0); z.U16LE(0); z.U16LE(0);
                    z.U32LE(crc); z.U32LE(sz); z.U32LE(sz);
                    z.U16LE(static_cast<uint16_t>(path.size())); z.U16LE(0);
                    z.Str(path);
                    if (!data.empty()) z.Bytes(data.data(), data.size());
                    cd.push_back({ path, crc, sz, off });
                }
                uint32_t cdOff = static_cast<uint32_t>(z.b.size());
                for (const auto& c : cd) {
                    z.U32LE(0x02014B50u);
                    z.U16LE(20); z.U16LE(20); z.U16LE(0); z.U16LE(0); z.U16LE(0); z.U16LE(0);
                    z.U32LE(c.crc); z.U32LE(c.sz); z.U32LE(c.sz);
                    z.U16LE(static_cast<uint16_t>(c.path.size()));
                    z.U16LE(0); z.U16LE(0); z.U16LE(0); z.U16LE(0);
                    z.U32LE(0); z.U32LE(c.lhoff);
                    z.Str(c.path);
                }
                uint32_t cdSize = static_cast<uint32_t>(z.b.size()) - cdOff;
                z.U32LE(0x06054B50u);
                z.U16LE(0); z.U16LE(0);
                z.U16LE(static_cast<uint16_t>(cd.size()));
                z.U16LE(static_cast<uint16_t>(cd.size()));
                z.U32LE(cdSize);
                z.U32LE(cdOff);
                z.U16LE(0);
                return z.b;
            };

            auto zigZag64 = [](int64_t v) -> uint64_t {
                return (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
            };
            auto pbBytes = [](ByteBuf& dst, uint32_t fn, const void* p, size_t n) {
                dst.Uleb((static_cast<uint64_t>(fn) << 3) | 2u);
                dst.Uleb(n);
                if (n > 0) dst.Bytes(p, n);
            };
            auto pbStr = [&](ByteBuf& dst, uint32_t fn, std::string_view s) {
                pbBytes(dst, fn, s.data(), s.size());
            };
            auto pbVar = [](ByteBuf& dst, uint32_t fn, uint64_t v) {
                dst.Uleb((static_cast<uint64_t>(fn) << 3) | 0u);
                dst.Uleb(v);
            };

            // 9A. OpenStreetMap Binary PBF (.pbf): OSMHeader + Zlib-compressed OSMData (DenseNodes + Way + geoPoints)
            {
                auto appendOsmBlock = [](ByteBuf& file, std::string_view btype, const ByteBuf& blob) {
                    ByteBuf bh;
                    bh.Uleb((1u << 3) | 2u); bh.Uleb(btype.size()); bh.Str(btype);
                    bh.Uleb((3u << 3) | 0u); bh.Uleb(blob.b.size());
                    file.U32BE(static_cast<uint32_t>(bh.b.size()));
                    file.Bytes(bh.b.data(), bh.b.size());
                    file.Bytes(blob.b.data(), blob.b.size());
                };

                ByteBuf bbox;
                pbVar(bbox, 1, zigZag64(11570000000LL)); // left  = 11.57
                pbVar(bbox, 2, zigZag64(11590000000LL)); // right = 11.59
                pbVar(bbox, 3, zigZag64(48150000000LL)); // top   = 48.15
                pbVar(bbox, 4, zigZag64(48130000000LL)); // bot   = 48.13
                ByteBuf hdrBlk;
                pbBytes(hdrBlk, 1, bbox.b.data(), bbox.b.size());
                pbStr(hdrBlk, 4, "OsmSchema-V0.6");
                pbStr(hdrBlk, 4, "DenseNodes");
                pbStr(hdrBlk, 16, "Osmium/1.16.0");
                ByteBuf hdrBlob;
                pbBytes(hdrBlob, 1, hdrBlk.b.data(), hdrBlk.b.size()); // raw

                // StringTable: 0="", 1="name", 2="Marienplatz", 3="amenity", 4="townhall", 5="highway", 6="pedestrian", 7="Kaufingerstrasse"
                ByteBuf st;
                static const char* const kOsmStrs[] = { "", "name", "Marienplatz", "amenity", "townhall", "highway", "pedestrian", "Kaufingerstrasse" };
                for (const char* s : kOsmStrs) {
                    pbStr(st, 1, s);
                }
                // DenseNodes: 2 nodes (1001 @ 48.1371540, 11.5754980 with name=Marienplatz, amenity=townhall; 1005 @ +0.0005, -0.0012)
                ByteBuf dnIds, dnLats, dnLons, dnKv;
                dnIds.Uleb(zigZag64(1001));      dnIds.Uleb(zigZag64(4));
                dnLats.Uleb(zigZag64(481371540)); dnLats.Uleb(zigZag64(500));
                dnLons.Uleb(zigZag64(115754980)); dnLons.Uleb(zigZag64(-1200));
                for (uint32_t kv : { 1u, 2u, 3u, 4u, 0u, 0u }) dnKv.Uleb(kv);
                ByteBuf dense;
                pbBytes(dense, 1, dnIds.b.data(), dnIds.b.size());
                pbBytes(dense, 8, dnLats.b.data(), dnLats.b.size());
                pbBytes(dense, 9, dnLons.b.data(), dnLons.b.size());
                pbBytes(dense, 10, dnKv.b.data(), dnKv.b.size());

                // Way 5001: name=Kaufingerstrasse, highway=pedestrian, refs=[1001, 1005]
                ByteBuf wKeys, wVals, wRefs;
                wKeys.Uleb(1); wKeys.Uleb(5);
                wVals.Uleb(7); wVals.Uleb(6);
                wRefs.Uleb(zigZag64(1001)); wRefs.Uleb(zigZag64(4));
                ByteBuf way;
                pbVar(way, 1, 5001);
                pbBytes(way, 2, wKeys.b.data(), wKeys.b.size());
                pbBytes(way, 3, wVals.b.data(), wVals.b.size());
                pbBytes(way, 8, wRefs.b.data(), wRefs.b.size());

                ByteBuf pg;
                pbBytes(pg, 2, dense.b.data(), dense.b.size());
                pbBytes(pg, 3, way.b.data(), way.b.size());

                ByteBuf primBlk;
                pbBytes(primBlk, 1, st.b.data(), st.b.size());
                pbBytes(primBlk, 2, pg.b.data(), pg.b.size());

                auto zPrim = buildStoredZlib(primBlk.b);
                ByteBuf dataBlob;
                pbVar(dataBlob, 2, primBlk.b.size());
                pbBytes(dataBlob, 3, zPrim.data(), zPrim.size()); // zlib_data!

                ByteBuf pbfFile;
                appendOsmBlock(pbfFile, "OSMHeader", hdrBlob);
                appendOsmBlock(pbfFile, "OSMData", dataBlob);

                // Corrupt OSM PBF: valid BlobHeader("OSMData") but broken zlib stream
                ByteBuf badZBlob;
                pbVar(badZBlob, 2, 64);
                pbStr(badZBlob, 3, "\x78\x01\xFF\xFF\x00\x00");
                ByteBuf badPbf;
                appendOsmBlock(badPbf, "OSMData", badZBlob);

                ParsedOfficeDocument pbfDoc, pbfBadDoc;
                bool okPbf = writeTempAndInspect(L"munich_center.pbf", pbfFile.b, pbfDoc);
                bool badPbfRej = !writeTempAndInspect(L"corrupt_zlib.pbf", badPbf.b, pbfBadDoc);
                tc.Record(okPbf && badPbfRej &&
                          DocContainsToken(pbfDoc, L"Osmium/1.16.0") &&
                          DocContainsToken(pbfDoc, L"Marienplatz") &&
                          DocContainsToken(pbfDoc, L"amenity=townhall") &&
                          DocContainsToken(pbfDoc, L"Kaufingerstrasse") &&
                          DocContainsToken(pbfDoc, L"highway=pedestrian") &&
                          DocContainsToken(pbfDoc, L"1001 -> 1005") &&
                          pbfDoc.geoPoints.size() >= 2 && pbfDoc.hasGeoTrack,
                    L"DocsSystem .pbf (OSMHeader + Zlib OSMData DenseNodes/Ways + Map GeoPoints)");
            }

            // 9B. Protomaps PMTiles v3 (.pmtiles): Gzip JSON Metadata + Gzip Hilbert Root Directory (TileIdToZXY)
            {
                // Root directory with 3 entries: tileId 0 (0/0/0), tileId 4 (1/1/0), tileId 19 (2/2/0)
                ByteBuf rootRaw;
                rootRaw.Uleb(3);  // nEntries = 3
                rootRaw.Uleb(0);  rootRaw.Uleb(4);  rootRaw.Uleb(15);  // deltas -> 0, 4, 19
                rootRaw.Uleb(1);  rootRaw.Uleb(2);  rootRaw.Uleb(1);   // run_length
                rootRaw.Uleb(64); rootRaw.Uleb(96); rootRaw.Uleb(80);  // length
                rootRaw.Uleb(1);  rootRaw.Uleb(0);  rootRaw.Uleb(0);   // offset: 0, contiguous(64), contiguous(160)
                auto rootGz = BuildStoredGzip(std::string_view(reinterpret_cast<const char*>(rootRaw.b.data()), rootRaw.b.size()));

                std::string metaJson =
                    "{\"name\":\"Alps Vector Basemap\",\"format\":\"pbf\",\"attribution\":\"OSM Contributors\","
                    "\"vector_layers\":[{\"id\":\"transportation\",\"minzoom\":0,\"maxzoom\":14,"
                    "\"description\":\"Alpine highway network\",\"fields\":{\"class\":\"String\"}}]}";
                auto metaGz = BuildStoredGzip(metaJson);

                ByteBuf pm;
                pm.Bytes("PMTiles\x03", 8);
                uint64_t rOff = 127;
                uint64_t jOff = rOff + rootGz.size();
                uint64_t tOff = jOff + metaGz.size();
                pm.U64LE(rOff); pm.U64LE(rootGz.size()); // root dir
                pm.U64LE(jOff); pm.U64LE(metaGz.size()); // json metadata
                pm.U64LE(0);    pm.U64LE(0);             // leaf dirs
                pm.U64LE(tOff); pm.U64LE(240);           // tile data
                pm.U64LE(4);    pm.U64LE(3); pm.U64LE(3);
                pm.U8(1); // clustered
                pm.U8(2); // internal_compression = Gzip (2)
                pm.U8(2); // tile_compression = Gzip (2)
                pm.U8(1); // MVT
                pm.U8(0); // minZoom = 0
                pm.U8(14);// maxZoom = 14
                pm.U32LE(static_cast<uint32_t>(100000000)); // minLon = 10.0
                pm.U32LE(static_cast<uint32_t>(460000000)); // minLat = 46.0
                pm.U32LE(static_cast<uint32_t>(130000000)); // maxLon = 13.0
                pm.U32LE(static_cast<uint32_t>(480000000)); // maxLat = 48.0
                pm.U8(8);                                   // centerZoom = 8
                pm.U32LE(static_cast<uint32_t>(115000000)); // centerLon = 11.5
                pm.U32LE(static_cast<uint32_t>(470000000)); // centerLat = 47.0
                pm.Bytes(rootGz.data(), rootGz.size());
                pm.Bytes(metaGz.data(), metaGz.size());
                pm.Zeros(240);

                // Corrupt PMTiles: 127B header claiming Gzip root dir, but invalid non-Gzip root dir payload
                std::vector<uint8_t> badPm = pm.b;
                badPm[127] = 0xDE; badPm[128] = 0xAD;

                ParsedOfficeDocument pmDoc, pmBadDoc;
                bool okPm = writeTempAndInspect(L"alps_v3.pmtiles", pm.b, pmDoc);
                bool badPmRej = !writeTempAndInspect(L"corrupt_v3.pmtiles", badPm, pmBadDoc);
                tc.Record(okPm && badPmRej &&
                          DocContainsToken(pmDoc, L"Alps Vector Basemap") &&
                          DocContainsToken(pmDoc, L"transportation") &&
                          DocContainsToken(pmDoc, L"Alpine highway network") &&
                          DocContainsToken(pmDoc, L"0/0/0") &&
                          DocContainsToken(pmDoc, L"1/1/0") &&
                          DocContainsToken(pmDoc, L"2/2/0") &&
                          pmDoc.geoPoints.size() >= 5,
                    L"DocsSystem .pmtiles (PMTiles v3 Gzip Metadata + Inverse Hilbert TileIdToZXY)");
            }

            // 9C. Windows Authenticode Security Catalog (.cat): PKCS#7 SignedData + SPC_CATALOG_LIST + X.509 Signer & Anti-Cheat
            {
                auto makeBmpStr = [](std::string_view s) {
                    std::vector<uint8_t> body;
                    for (char c : s) { body.push_back(0); body.push_back(static_cast<uint8_t>(c)); }
                    return Asn1Wrap(0x1E, body);
                };

                // CAT_NAMEVALUE (1.3.6.1.4.1.311.12.2.1): name="File" (BMPString), flags=1, value=UTF-16LE "nvme_storport.sys\0"
                ByteBuf utf16Val;
                utf16Val.Utf16LE(L"nvme_storport.sys", true);
                std::vector<uint8_t> nvSeqBody;
                Asn1Append(nvSeqBody, makeBmpStr("File"));
                Asn1Append(nvSeqBody, { 0x02, 0x01, 0x01 });
                Asn1Append(nvSeqBody, Asn1Wrap(0x04, utf16Val.b));
                std::vector<uint8_t> attrNvBody = { 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x0C, 0x02, 0x01 };
                Asn1Append(attrNvBody, Asn1Wrap(0x31, Asn1Wrap(0x30, nvSeqBody)));

                // SPC_INDIRECT_DATA_CONTENT (1.3.6.1.4.1.311.2.1.4): DigestInfo with SHA-256 (2.16.840.1.101.3.4.2.1)
                std::vector<uint8_t> realSha256(32);
                for (size_t i = 0; i < 32; ++i) realSha256[i] = static_cast<uint8_t>(0xA0u + (i & 0x0Fu));
                std::vector<uint8_t> sha256Alg = Asn1Wrap(0x30, { 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00 });
                std::vector<uint8_t> digestInfoBody;
                Asn1Append(digestInfoBody, sha256Alg);
                Asn1Append(digestInfoBody, Asn1Wrap(0x04, realSha256));
                std::vector<uint8_t> spcIdcBody;
                Asn1Append(spcIdcBody, Asn1Wrap(0x30, { 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F }));
                Asn1Append(spcIdcBody, Asn1Wrap(0x30, digestInfoBody));
                std::vector<uint8_t> attrIdcBody = { 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x04 };
                Asn1Append(attrIdcBody, Asn1Wrap(0x31, Asn1Wrap(0x30, spcIdcBody)));

                std::vector<uint8_t> memAttrsBody;
                Asn1Append(memAttrsBody, Asn1Wrap(0x30, attrNvBody));
                Asn1Append(memAttrsBody, Asn1Wrap(0x30, attrIdcBody));
                std::vector<uint8_t> memberBody;
                Asn1Append(memberBody, makeBmpStr("E2DRV001"));
                Asn1Append(memberBody, Asn1Wrap(0x31, memAttrsBody));

                // CTL / SPC_CATALOG_LIST (SubjectUsage = 1.3.6.1.4.1.311.12.1.1)
                std::vector<uint8_t> ctlBody;
                Asn1Append(ctlBody, Asn1Wrap(0x30, { 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x0C, 0x01, 0x01 }));
                Asn1Append(ctlBody, Asn1Wrap(0x04, { 0x11, 0x22, 0x33, 0x44 }));
                std::string ctlTime = "250315120000Z";
                Asn1Append(ctlBody, Asn1Wrap(0x17, std::vector<uint8_t>(ctlTime.begin(), ctlTime.end())));
                Asn1Append(ctlBody, Asn1Wrap(0x30, { 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x0C, 0x01, 0x02 }));
                Asn1Append(ctlBody, Asn1Wrap(0x30, Asn1Wrap(0x30, memberBody)));

                // EncapsulatedContentInfo: szOID_CTL (1.3.6.1.4.1.311.10.1) + [0] EXPLICIT CTL SEQUENCE
                std::vector<uint8_t> encapBody = { 0x06, 0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x0A, 0x01 };
                Asn1Append(encapBody, Asn1Wrap(0xA0, Asn1Wrap(0x30, ctlBody)));

                auto signerCert = MakeX509Der(0x42, "Explorer2 WHQL Catalog Signer", "whql.explorer2.local");

                // Deliberate anti-cheat trap inside SignerInfo RSA encryptedDigest: 0x04 0x20 + 32 bytes of 0xDE,0xAD,0xBE,0xEF!
                std::vector<uint8_t> fakeSigBytes = { 0x99, 0x88, 0x04, 0x20 };
                for (int k = 0; k < 8; ++k) {
                    fakeSigBytes.push_back(0xDE); fakeSigBytes.push_back(0xAD);
                    fakeSigBytes.push_back(0xBE); fakeSigBytes.push_back(0xEF);
                }
                std::vector<uint8_t> signerInfoBody = { 0x02, 0x01, 0x01 };
                Asn1Append(signerInfoBody, Asn1Wrap(0x04, fakeSigBytes));

                std::vector<uint8_t> signedDataBody;
                Asn1Append(signedDataBody, { 0x02, 0x01, 0x01 }); // version = 1
                Asn1Append(signedDataBody, Asn1Wrap(0x31, sha256Alg));
                Asn1Append(signedDataBody, Asn1Wrap(0x30, encapBody));
                Asn1Append(signedDataBody, Asn1Wrap(0xA0, signerCert)); // [0] IMPLICIT certificates
                Asn1Append(signedDataBody, Asn1Wrap(0x31, Asn1Wrap(0x30, signerInfoBody)));

                std::vector<uint8_t> contentInfoBody = { 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x02 }; // SignedData
                Asn1Append(contentInfoBody, Asn1Wrap(0xA0, Asn1Wrap(0x30, signedDataBody)));
                std::vector<uint8_t> catDer = Asn1Wrap(0x30, contentInfoBody);

                // Anti-cheat negative test: non-catalog DER sequence containing raw 0x04 0x20 bytes
                std::vector<uint8_t> fakeCat = Asn1Wrap(0x30, fakeSigBytes);

                ParsedOfficeDocument catDoc, catBadDoc;
                bool okCat = writeTempAndInspect(L"driver_whql.cat", catDer, catDoc);
                bool badCatRej = !writeTempAndInspect(L"fake_thumbprint_cheat.cat", fakeCat, catBadDoc);
                tc.Record(okCat && badCatRej &&
                          DocContainsToken(catDoc, L"E2DRV001") &&
                          DocContainsToken(catDoc, L"nvme_storport.sys") &&
                          DocContainsToken(catDoc, L"SHA-256") &&
                          DocContainsToken(catDoc, L"A0A1A2A3A4A5A6A7A8A9AAABACADAEAF") &&
                          DocContainsToken(catDoc, L"Explorer2 WHQL Catalog Signer") &&
                          !DocContainsToken(catDoc, L"DEADBEEFDEADBEEF"),
                    L"DocsSystem .cat (Authenticode ASN.1 SPC_CATALOG_LIST, SHA-256 Member, X.509 Signer & RSA Trap Reject)");
            }

            // 9D. Apache ORC (.orc): LZ4 & ZSTD Compression + RLEv2 DELTA & DICTIONARY_V2 Row Decoding + Corrupt Footer Reject
            {
                auto makeOrcChunk = [](const std::vector<uint8_t>& plain, uint64_t codec) -> std::vector<uint8_t> {
                    ByteBuf chunk;
                    if (codec == 4) { // LZ4 raw block (token + literals)
                        ByteBuf lz4;
                        if (plain.size() < 15) {
                            lz4.U8(static_cast<uint8_t>(plain.size() << 4));
                        } else {
                            lz4.U8(0xF0u);
                            size_t rem = plain.size() - 15u;
                            while (rem >= 255u) { lz4.U8(255u); rem -= 255u; }
                            lz4.U8(static_cast<uint8_t>(rem));
                        }
                        lz4.Bytes(plain.data(), plain.size());
                        uint32_t hdr = static_cast<uint32_t>(lz4.b.size() << 1); // isOriginal = 0
                        chunk.U8(static_cast<uint8_t>(hdr & 0xFFu));
                        chunk.U8(static_cast<uint8_t>((hdr >> 8) & 0xFFu));
                        chunk.U8(static_cast<uint8_t>((hdr >> 16) & 0xFFu));
                        chunk.Bytes(lz4.b.data(), lz4.b.size());
                    } else if (codec == 5) { // ZSTD single-segment raw-block frame
                        ByteBuf zst;
                        zst.U32LE(0xFD2FB528u); // ZSTD frame magic
                        zst.U8(0x20u);          // FHD: Single_Segment_Flag=1 (1-byte FCS)
                        zst.U8(static_cast<uint8_t>(plain.size()));
                        uint32_t blkHdr = (static_cast<uint32_t>(plain.size()) << 3) | 1u; // Last_Block=1, Block_Type=0 (Raw_Block)
                        zst.U8(static_cast<uint8_t>(blkHdr & 0xFFu));
                        zst.U8(static_cast<uint8_t>((blkHdr >> 8) & 0xFFu));
                        zst.U8(static_cast<uint8_t>((blkHdr >> 16) & 0xFFu));
                        zst.Bytes(plain.data(), plain.size());
                        uint32_t hdr = static_cast<uint32_t>(zst.b.size() << 1); // isOriginal = 0
                        chunk.U8(static_cast<uint8_t>(hdr & 0xFFu));
                        chunk.U8(static_cast<uint8_t>((hdr >> 8) & 0xFFu));
                        chunk.U8(static_cast<uint8_t>((hdr >> 16) & 0xFFu));
                        chunk.Bytes(zst.b.data(), zst.b.size());
                    }
                    return chunk.b;
                };

                auto buildOrcWithRows = [&](uint64_t codec, int64_t baseSensorId, std::string_view st0, std::string_view st1) {
                    // Col 1 (LONG, DIRECT_V2): RLEv2 DELTA (subEnc=3, bw=0, runLen=3, base=baseSensorId, delta=+50)
                    ByteBuf col1Raw;
                    col1Raw.U8(0xC0u); col1Raw.U8(0x02u);
                    col1Raw.Uleb(zigZag64(baseSensorId));
                    col1Raw.Uleb(zigZag64(50));
                    auto col1Comp = makeOrcChunk(col1Raw.b, codec);

                    // Col 2 (STRING, DICTIONARY_V2, dictSize=2):
                    //   DATA (indices for 3 rows: [0, 1, 0] via RLEv2 DIRECT 8-bit)
                    //   LENGTH (lengths [st0.size, st1.size] via RLEv2 DIRECT 8-bit)
                    //   DICTIONARY_DATA (st0 + st1)
                    ByteBuf c2DataRaw, c2LenRaw, c2DictRaw;
                    c2DataRaw.U8(0x4Eu); c2DataRaw.U8(0x02u); c2DataRaw.U8(0); c2DataRaw.U8(1); c2DataRaw.U8(0);
                    c2LenRaw.U8(0x4Eu);  c2LenRaw.U8(0x01u);
                    c2LenRaw.U8(static_cast<uint8_t>(st0.size()));
                    c2LenRaw.U8(static_cast<uint8_t>(st1.size()));
                    c2DictRaw.Str(st0); c2DictRaw.Str(st1);

                    auto c2DataComp = makeOrcChunk(c2DataRaw.b, codec);
                    auto c2LenComp  = makeOrcChunk(c2LenRaw.b, codec);
                    auto c2DictComp = makeOrcChunk(c2DictRaw.b, codec);

                    auto makeStreamMsg = [&](uint64_t kind, uint32_t col, uint64_t len) {
                        ByteBuf sm;
                        pbVar(sm, 1, kind); pbVar(sm, 2, col); pbVar(sm, 3, len);
                        return sm.b;
                    };
                    auto makeColEncMsg = [&](uint64_t kind, uint64_t dictSz) {
                        ByteBuf ce;
                        pbVar(ce, 1, kind);
                        if (dictSz > 0) pbVar(ce, 2, dictSz);
                        return ce.b;
                    };

                    ByteBuf sfRaw;
                    auto s1 = makeStreamMsg(1, 1, col1Comp.size());   pbBytes(sfRaw, 1, s1.data(), s1.size());
                    auto s2 = makeStreamMsg(1, 2, c2DataComp.size()); pbBytes(sfRaw, 1, s2.data(), s2.size());
                    auto s3 = makeStreamMsg(2, 2, c2LenComp.size());  pbBytes(sfRaw, 1, s3.data(), s3.size());
                    auto s4 = makeStreamMsg(3, 2, c2DictComp.size()); pbBytes(sfRaw, 1, s4.data(), s4.size());
                    auto e0 = makeColEncMsg(0, 0); pbBytes(sfRaw, 2, e0.data(), e0.size()); // col 0 (root struct)
                    auto e1 = makeColEncMsg(2, 0); pbBytes(sfRaw, 2, e1.data(), e1.size()); // col 1: DIRECT_V2
                    auto e2 = makeColEncMsg(3, 2); pbBytes(sfRaw, 2, e2.data(), e2.size()); // col 2: DICTIONARY_V2 (size=2)
                    auto sfComp = makeOrcChunk(sfRaw.b, codec);

                    ByteBuf orc;
                    orc.Str("ORC"); // offset 3
                    uint64_t stripeOff = orc.b.size();
                    orc.Bytes(col1Comp.data(), col1Comp.size());
                    orc.Bytes(c2DataComp.data(), c2DataComp.size());
                    orc.Bytes(c2LenComp.data(), c2LenComp.size());
                    orc.Bytes(c2DictComp.data(), c2DictComp.size());
                    uint64_t dataLen = orc.b.size() - stripeOff;
                    orc.Bytes(sfComp.data(), sfComp.size());

                    // FileFooter
                    ByteBuf stInfo;
                    pbVar(stInfo, 1, stripeOff);
                    pbVar(stInfo, 2, 0); // indexLength = 0
                    pbVar(stInfo, 3, dataLen);
                    pbVar(stInfo, 4, sfComp.size());
                    pbVar(stInfo, 5, 3); // numRows = 3

                    ByteBuf t0, t1, t2;
                    pbVar(t0, 1, 12); pbVar(t0, 2, 1); pbVar(t0, 2, 2);
                    pbStr(t0, 3, "sensor_id"); pbStr(t0, 3, "station_name");
                    pbVar(t1, 1, 4); // LONG
                    pbVar(t2, 1, 7); // STRING

                    ByteBuf ffRaw;
                    pbVar(ffRaw, 1, 3);
                    pbVar(ffRaw, 2, dataLen + sfComp.size());
                    pbBytes(ffRaw, 3, stInfo.b.data(), stInfo.b.size());
                    pbBytes(ffRaw, 4, t0.b.data(), t0.b.size());
                    pbBytes(ffRaw, 4, t1.b.data(), t1.b.size());
                    pbBytes(ffRaw, 4, t2.b.data(), t2.b.size());
                    pbVar(ffRaw, 6, 3); // numberOfRows = 3
                    auto ffComp = makeOrcChunk(ffRaw.b, codec);
                    orc.Bytes(ffComp.data(), ffComp.size());

                    ByteBuf ps;
                    pbVar(ps, 1, ffComp.size());
                    pbVar(ps, 2, codec);
                    pbStr(ps, 8000, "ORC");
                    orc.Bytes(ps.b.data(), ps.b.size());
                    orc.U8(static_cast<uint8_t>(ps.b.size()));
                    return orc.b;
                };

                // Anti-cheat negative test: "ORC" + undecodable Zlib footer MUST return false (NOT fake Total Rows = 0!)
                ByteBuf undecodableOrc;
                undecodableOrc.Str("ORC");
                undecodableOrc.Bytes("\x08\x00\x00\xDE\xAD\xBE\xEF", 7); // 4-byte fake compressed chunk
                ByteBuf badPs;
                pbVar(badPs, 1, 7);
                pbVar(badPs, 2, 1); // codec = ZLIB, but chunk is invalid deflate!
                pbStr(badPs, 8000, "ORC");
                undecodableOrc.Bytes(badPs.b.data(), badPs.b.size());
                undecodableOrc.U8(static_cast<uint8_t>(badPs.b.size()));

                ParsedOfficeDocument orcLz4Doc, orcZstdDoc, orcBadDoc;
                bool okLz4  = writeTempAndInspect(L"telemetry_lz4.orc", buildOrcWithRows(4, 9100, "Zugspitze_Lab", "Jungfraujoch_Obs"), orcLz4Doc);
                bool okZstd = writeTempAndInspect(L"telemetry_zstd.orc", buildOrcWithRows(5, 4200, "MaunaLoa_Station", "Svalbard_Vault"), orcZstdDoc);
                bool badOrcRej = !writeTempAndInspect(L"corrupt_footer_cheat.orc", undecodableOrc.b, orcBadDoc);
                tc.Record(okLz4 && okZstd && badOrcRej &&
                          DocContainsToken(orcLz4Doc, L"LZ4") &&
                          DocContainsToken(orcLz4Doc, L"9150") &&
                          DocContainsToken(orcLz4Doc, L"Jungfraujoch_Obs") &&
                          DocContainsToken(orcZstdDoc, L"ZSTD") &&
                          DocContainsToken(orcZstdDoc, L"4250") &&
                          DocContainsToken(orcZstdDoc, L"Svalbard_Vault"),
                    L"DocsSystem .orc (LZ4 & ZSTD Streams, RLEv2 DELTA + DICTIONARY_V2 Rows & Corrupt Footer Reject)");
            }

            // 9E. Redis RDB Snapshot (.rdb v11): Real LZF Backref String + SET_INTSET + HASH_ZIPLIST + ZSET_LISTPACK
            {
                ByteBuf rdb;
                rdb.Str("REDIS0011");
                // AUX (0xFA): redis-ver = "7.2.4"
                rdb.U8(0xFAu); rdb.U8(9); rdb.Str("redis-ver"); rdb.U8(5); rdb.Str("7.2.4");
                // SELECTDB (0xFE) db=0, RESIZEDB (0xFB) dbSize=4, expSize=0
                rdb.U8(0xFEu); rdb.U8(0);
                rdb.U8(0xFBu); rdb.U8(4); rdb.U8(0);

                // Key 1 (0x00 String, LZF enc=3): "ClusterNodeAlpha_2026_ClusterNodeAlpha" (38B)
                // Compressed with genuine LZF backreference (ctrl=21 -> 22 literals "ClusterNodeAlpha_2026_",
                // then match len=16 -> ctrl=((7<<5)|0)=0xE0, extraLen=7 (9+7=16), off-1=21 -> copies "ClusterNodeAlpha"!)
                rdb.U8(0x00u);
                rdb.U8(7); rdb.Str("lzf_key");
                ByteBuf lzfComp;
                lzfComp.U8(21u); lzfComp.Str("ClusterNodeAlpha_2026_");
                lzfComp.U8(0xE0u); lzfComp.U8(7u); lzfComp.U8(21u);
                rdb.U8(0xC3u); // enc = 3 (RDB_ENC_LZF)
                rdb.U8(static_cast<uint8_t>(lzfComp.b.size()));
                rdb.U8(38u);   // uncompressed len = 38
                rdb.Bytes(lzfComp.b.data(), lzfComp.b.size());

                // Key 2 (0x0B RDB_TYPE_SET_INTSET): 32-bit intset { 1024, 2048 }
                ByteBuf intset;
                intset.U32LE(4); // 32-bit ints
                intset.U32LE(2); // 2 elements
                intset.U32LE(1024); intset.U32LE(2048);
                rdb.U8(0x0Bu);
                rdb.U8(6); rdb.Str("ids_32");
                rdb.U8(static_cast<uint8_t>(intset.b.size()));
                rdb.Bytes(intset.b.data(), intset.b.size());

                // Key 3 (0x0D RDB_TYPE_HASH_ZIPLIST): { "role": "primary_replica" }
                ByteBuf zl;
                zl.U32LE(0); // zlbytes patched below
                zl.U32LE(10);// zltail
                zl.U16LE(2); // 2 entries
                zl.U8(0);    zl.U8(4);  zl.Str("role");                       // entry 0 (len=6)
                zl.U8(6);    zl.U8(15); zl.Str("primary_replica");            // entry 1
                zl.U8(0xFFu);
                PutU32LE(zl.b.data(), static_cast<uint32_t>(zl.b.size()));
                rdb.U8(0x0Du);
                rdb.U8(8); rdb.Str("cfg_hash");
                rdb.U8(static_cast<uint8_t>(zl.b.size()));
                rdb.Bytes(zl.b.data(), zl.b.size());

                // Key 4 (0x11 RDB_TYPE_ZSET_LISTPACK): { "hyperion_node": "99.5" }
                ByteBuf lp;
                lp.U32LE(0); // tot-bytes patched below
                lp.U16LE(2); // 2 elements
                // Element 0: 6-bit string (0x80 | 13) + "hyperion_node" + backlen(14)
                lp.U8(static_cast<uint8_t>(0x80u | 13u)); lp.Str("hyperion_node"); lp.U8(14u);
                // Element 1: 6-bit string (0x80 | 4) + "99.5" + backlen(5)
                lp.U8(static_cast<uint8_t>(0x80u | 4u));  lp.Str("99.5");          lp.U8(5u);
                lp.U8(0xFFu);
                PutU32LE(lp.b.data(), static_cast<uint32_t>(lp.b.size()));
                rdb.U8(0x11u);
                rdb.U8(9); rdb.Str("rank_zset");
                rdb.U8(static_cast<uint8_t>(lp.b.size()));
                rdb.Bytes(lp.b.data(), lp.b.size());

                rdb.U8(0xFFu); // EOF
                rdb.U64LE(0);  // CRC64

                std::vector<uint8_t> badRdb = { 'R','E','D','I','S','0','0','9','9', 0xFF }; // invalid version 99
                ParsedOfficeDocument rdbDoc, rdbBadDoc;
                bool okRdb = writeTempAndInspect(L"dump_v11.rdb", rdb.b, rdbDoc);
                bool badRdbRej = !writeTempAndInspect(L"bad_ver.rdb", badRdb, rdbBadDoc);
                tc.Record(okRdb && badRdbRej &&
                          DocContainsToken(rdbDoc, L"ClusterNodeAlpha_2026_ClusterNodeAlpha") &&
                          DocContainsToken(rdbDoc, L"2048") &&
                          DocContainsToken(rdbDoc, L"primary_replica") &&
                          DocContainsToken(rdbDoc, L"hyperion_node") &&
                          DocContainsToken(rdbDoc, L"99.5"),
                    L"DocsSystem .rdb (Redis v11 LZF Backref Decompression + INTSET + ZIPLIST + LISTPACK)");
            }

            // 9F. Kindle / Mobipocket HUFF/CDIC (.mobi / .azw3): compression==17480 + Recursive CDIC + extraDataFlags Tail Trim
            {
                // 4-symbol canonical 2-bit HUFF codebook:
                //   code 11 (3) -> r=0 (leaf "<html><body><h1>")
                //   code 10 (2) -> r=1 (NON-LEAF recursive phrase: codes [2(r=1? no: 01->r=2), 00->r=3]!)
                //   code 01 (1) -> r=2 (leaf "Quantum Cryptography ")
                //   code 00 (0) -> r=3 (leaf "Handbook</h1></body></html>")
                ByteBuf huff;
                huff.Bytes("HUFF\x00\x00\x00\x18", 8);
                huff.U32BE(24);          // off1 = 24
                huff.U32BE(24 + 1024);   // off2 = 1048
                huff.U32BE(0);           // BE dict1/dict2
                huff.U32BE(0);
                // dict1: 256 BE uint32 entries; for all 8-bit prefixes, cl=2, term=1 (0x80), maxcode_raw=3 -> (3<<8)|0x82 = 0x00000382
                for (int i = 0; i < 256; ++i) huff.U32BE(0x00000382u);
                // dict2: 32 pairs of (mincode, maxcode) BE uint32
                for (int cl = 1; cl <= 32; ++cl) {
                    huff.U32BE(0);
                    huff.U32BE(cl == 2 ? 3u : 0u);
                }

                // CDIC record: numPhrases=4, bits=2
                std::string p0 = "<html><body><h1>";
                // p1 is recursive non-leaf (flag 0x0000): 4 bits: '01' (r=2) + '00' (r=3) -> 0b0100_0000 = 0x40 (1 byte — wait:
                // UnpackSlice consumes 2+2=4 bits; with 1 byte (8 bits), remaining 4 bits '0000' would decode 2 more symbols!
                // So make r=1 encode EXACTLY 4 symbols in 1 byte (8 bits): '01' (r=2) + '01' (r=2) + '01' (r=2) + '00' (r=3) = 0b01010100 = 0x54!
                uint8_t p1RecByte = 0x54u; // 4 x 2-bit codes: r=2, r=2, r=2, r=3 -> "Quantum Cryptography " x3 + "Handbook</h1></body></html>"!
                std::string p2 = "Quantum Cryptography ";
                std::string p3 = "Handbook</h1><p>HuffCdic Verified.</p></body></html>";

                ByteBuf cdicData;
                uint16_t offP0 = 8;
                cdicData.U16BE(static_cast<uint16_t>(0x8000u | p0.size())); cdicData.Str(p0);
                uint16_t offP1 = static_cast<uint16_t>(8 + cdicData.b.size());
                cdicData.U16BE(0x0001u); cdicData.U8(p1RecByte); // flag 0x8000 CLEAR -> recursive UnpackSlice!
                uint16_t offP2 = static_cast<uint16_t>(8 + cdicData.b.size());
                cdicData.U16BE(static_cast<uint16_t>(0x8000u | p2.size())); cdicData.Str(p2);
                uint16_t offP3 = static_cast<uint16_t>(8 + cdicData.b.size());
                cdicData.U16BE(static_cast<uint16_t>(0x8000u | p3.size())); cdicData.Str(p3);

                ByteBuf cdic;
                cdic.Bytes("CDIC\x00\x00\x00\x10", 8);
                cdic.U32BE(4); // numPhrases = 4
                cdic.U32BE(2); // bits = 2
                cdic.U16BE(offP0); cdic.U16BE(offP1); cdic.U16BE(offP2); cdic.U16BE(offP3);
                cdic.Bytes(cdicData.b.data(), cdicData.b.size());

                // Text record 1: 1 compressed byte: '11' (r=0) + '10' (r=1) + '10' (r=1) + '10' (r=1) = 0b11101010 = 0xEA
                // PLUS 2 trailing multibyte overlap bytes (extraDataFlags = 1, last byte (overlapCount & 3) = 1 -> trims 2 bytes!)
                ByteBuf txtRec;
                txtRec.U8(0xEAu);
                txtRec.U8(0xFFu); txtRec.U8(0x01u); // trailing overlap bytes trimmed before HUFF/CDIC!

                // Record 0: 16B PalmDOC (compression=17480) + 248B MOBI header (huffRecIdx=2, huffRecCnt=2, extraDataFlags=1 at +0xF2)
                std::string bookTitle = "Post-Quantum Cipher Compendium";
                ByteBuf rec0;
                rec0.U16BE(17480); // HUFF/CDIC!
                rec0.U16BE(0);
                rec0.U32BE(512);
                rec0.U16BE(1);     // 1 text record (record 1)
                rec0.U16BE(4096);
                rec0.U16BE(0);
                rec0.U16BE(0);

                uint32_t mobiHdrLen = 248;
                uint32_t fnOff = 16 + mobiHdrLen;
                rec0.Str("MOBI");
                rec0.U32BE(mobiHdrLen);
                rec0.U32BE(2);
                rec0.U32BE(65001);
                rec0.U32BE(77777);
                rec0.U32BE(8); // KF8 v8
                rec0.PadTo(84);
                rec0.U32BE(fnOff);
                rec0.U32BE(static_cast<uint32_t>(bookTitle.size()));
                rec0.PadTo(16 + 96);
                rec0.U32BE(2); // huffRecIdx = 2
                rec0.U32BE(2); // huffRecCnt = 2 (rec 2 = HUFF, rec 3 = CDIC)
                rec0.PadTo(0xF2);
                rec0.U16BE(1); // extraDataFlags = 1 (multibyte overlap enabled)
                rec0.PadTo(16 + mobiHdrLen);
                rec0.Str(bookTitle);

                ByteBuf pdb;
                pdb.FixedStr("PQ_Cipher_KF8", 32);
                pdb.Zeros(28);
                pdb.Str("BOOKMOBI");
                pdb.Zeros(8);
                pdb.U16BE(4); // 4 records: 0=MOBI, 1=Text, 2=HUFF, 3=CDIC
                uint32_t o0 = 78 + 4 * 8;
                uint32_t o1 = o0 + static_cast<uint32_t>(rec0.b.size());
                uint32_t o2 = o1 + static_cast<uint32_t>(txtRec.b.size());
                uint32_t o3 = o2 + static_cast<uint32_t>(huff.b.size());
                pdb.U32BE(o0); pdb.U32BE(0);
                pdb.U32BE(o1); pdb.U32BE(1);
                pdb.U32BE(o2); pdb.U32BE(2);
                pdb.U32BE(o3); pdb.U32BE(3);
                pdb.Bytes(rec0.b.data(), rec0.b.size());
                pdb.Bytes(txtRec.b.data(), txtRec.b.size());
                pdb.Bytes(huff.b.data(), huff.b.size());
                pdb.Bytes(cdic.b.data(), cdic.b.size());

                ParsedOfficeDocument azw3Doc, azwDoc;
                bool okAzw3 = writeTempAndInspect(L"pq_cipher.azw3", pdb.b, azw3Doc);
                bool okAzw  = writeTempAndInspect(L"pq_cipher.azw", pdb.b, azwDoc);
                tc.Record(okAzw3 && okAzw &&
                          DocContainsToken(azw3Doc, L"Post-Quantum Cipher Compendium") &&
                          DocContainsToken(azw3Doc, L"Quantum Cryptography Quantum Cryptography Quantum Cryptography Handbook") &&
                          DocContainsToken(azw3Doc, L"HuffCdic Verified.") &&
                          DocContainsToken(azwDoc, L"HuffCdic Verified."),
                    L"DocsSystem .azw3/.azw (Mobipocket HUFF/CDIC Recursive Dictionary & extraDataFlags Trim)");
            }

            // 9G. Microsoft Excel Binary Workbook (.xlsb, [MS-XLSB]): BIFF12 Workbook + SST + Styles + Sheet Cells
            {
                auto biff12Rec = [](ByteBuf& dst, uint32_t recId, const void* pay, size_t len) {
                    uint32_t id = recId;
                    do {
                        uint8_t b = static_cast<uint8_t>(id & 0x7Fu);
                        id >>= 7;
                        if (id != 0) b |= 0x80u;
                        dst.U8(b);
                    } while (id != 0);
                    uint32_t ln = static_cast<uint32_t>(len);
                    do {
                        uint8_t b = static_cast<uint8_t>(ln & 0x7Fu);
                        ln >>= 7;
                        if (ln != 0) b |= 0x80u;
                        dst.U8(b);
                    } while (ln != 0);
                    if (len > 0) dst.Bytes(pay, len);
                };
                auto xlWide = [](ByteBuf& dst, std::wstring_view w) {
                    dst.U32LE(static_cast<uint32_t>(w.size()));
                    dst.Utf16LE(w, false);
                };

                // xl/workbook.bin: BrtBeginBook(0x0083) + BrtBundleSh(0x009C: hsState=0, iTabID=1, rId="rId1", name="Revenue_Q4") + BrtEndBook(0x0084)
                ByteBuf wb, bsh;
                biff12Rec(wb, 0x0083u, nullptr, 0);
                bsh.U32LE(0); bsh.U32LE(1); xlWide(bsh, L"rId1"); xlWide(bsh, L"Revenue_Q4");
                biff12Rec(wb, 0x009Cu, bsh.b.data(), bsh.b.size());
                biff12Rec(wb, 0x0084u, nullptr, 0);

                std::string wbRels =
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                    "<Relationship Id=\"rId1\" Target=\"worksheets/sheet1.bin\"/>"
                    "</Relationships>";

                // xl/sharedStrings.bin: BrtBeginSst(0x009F) + BrtSSTItem(0x0013: flags=0, "Enterprise SaaS") + BrtEndSst(0x00A0)
                ByteBuf sst, ssti;
                uint64_t sstHdr = 1;
                biff12Rec(sst, 0x009Fu, &sstHdr, 8);
                ssti.U8(0); xlWide(ssti, L"Enterprise SaaS");
                biff12Rec(sst, 0x0013u, ssti.b.data(), ssti.b.size());
                biff12Rec(sst, 0x00A0u, nullptr, 0);

                // xl/styles.bin: BrtBeginCellXFs(0x0269) + xf0 (general ifmt=0) + xf1 (date ifmt=14 -> YYYY-MM-DD) + BrtEndCellXFs(0x026A)
                ByteBuf sty, xf0, xf1;
                uint32_t cxfCnt = 2;
                biff12Rec(sty, 0x0269u, &cxfCnt, 4);
                xf0.U16LE(0); xf0.U16LE(0);  xf0.Zeros(12);
                xf1.U16LE(0); xf1.U16LE(14); xf1.Zeros(12); // ifmt=14 built-in date!
                biff12Rec(sty, 0x002Fu, xf0.b.data(), xf0.b.size());
                biff12Rec(sty, 0x002Fu, xf1.b.data(), xf1.b.size());
                biff12Rec(sty, 0x026Au, nullptr, 0);

                // xl/worksheets/sheet1.bin: BrtBeginSheetData(0x0091) + Row 0:
                //   col 0: BrtCellIsst(0x0007) -> sst[0] ("Enterprise SaaS")
                //   col 1: BrtCellRk(0x0002)   -> integer 125075 / 100 = 1250.75 (rk = (125075 << 2) | 0x03)
                //   col 2: BrtCellReal(0x0005) -> style=1 (date), serial 45658.0 -> 2025-01-01
                //   col 3: BrtCellBool(0x0004) -> TRUE
                ByteBuf sh1, r0, c0, c1, c2, c3;
                biff12Rec(sh1, 0x0091u, nullptr, 0);
                r0.U32LE(0); r0.Zeros(12);
                biff12Rec(sh1, 0x0000u, r0.b.data(), r0.b.size());
                c0.U32LE(0); c0.U32LE(0); c0.U32LE(0);
                biff12Rec(sh1, 0x0007u, c0.b.data(), c0.b.size());
                c1.U32LE(1); c1.U32LE(0); c1.U32LE((125075u << 2) | 0x03u);
                biff12Rec(sh1, 0x0002u, c1.b.data(), c1.b.size());
                double serialDate = 45658.0; // 2025-01-01
                uint64_t dateBits = 0; std::memcpy(&dateBits, &serialDate, 8);
                c2.U32LE(2); c2.U32LE(1); c2.U64LE(dateBits); // stBits = 1 -> xf1 (ifmt=14)
                biff12Rec(sh1, 0x0005u, c2.b.data(), c2.b.size());
                c3.U32LE(3); c3.U32LE(0); c3.U8(1);
                biff12Rec(sh1, 0x0004u, c3.b.data(), c3.b.size());
                biff12Rec(sh1, 0x0092u, nullptr, 0);

                auto xlsbZip = buildStoredZipMulti({
                    { "xl/workbook.bin", wb.b },
                    { "xl/_rels/workbook.bin.rels", std::vector<uint8_t>(wbRels.begin(), wbRels.end()) },
                    { "xl/sharedStrings.bin", sst.b },
                    { "xl/styles.bin", sty.b },
                    { "xl/worksheets/sheet1.bin", sh1.b }
                });
                auto nonXlsbZip = BuildStoredZipSingle("readme.txt", "Plain archive renamed to .xlsb");

                ParsedOfficeDocument xlsbDoc, xlsbBadDoc;
                bool okXlsb = writeTempAndInspect(L"financials.xlsb", xlsbZip, xlsbDoc);
                bool badXlsbRej = !writeTempAndInspect(L"not_workbook.xlsb", nonXlsbZip, xlsbBadDoc);
                tc.Record(okXlsb && badXlsbRej &&
                          !xlsbDoc.sheets.empty() && xlsbDoc.sheets[0].name == L"Revenue_Q4" &&
                          DocContainsToken(xlsbDoc, L"Enterprise SaaS") &&
                          DocContainsToken(xlsbDoc, L"1250.75") &&
                          DocContainsToken(xlsbDoc, L"2025-01-01") &&
                          DocContainsToken(xlsbDoc, L"TRUE"),
                    L"DocsSystem .xlsb ([MS-XLSB] BIFF12 Workbook, SST, Date Styles, RK/Real/Bool Cells & ZIP Reject)");
            }

            // 9H. Microsoft Outlook Personal Folders (.pst, [MS-PST]): Unicode v23 + Permutative Encryption (bCryptMethod=1) + BBT/NBT/HN/PC
            {
                // Exact [MS-PST] §5.1 mpbbR encryption table so data blocks on disk are genuinely encrypted
                static const uint8_t kMpbbR[256] = {
                    0x47,0xF1,0xB4,0xE6,0x0B,0x6A,0x72,0x48,0x85,0x4E,0x9E,0xEB,0xE2,0xF8,0x94,0x53,
                    0xE0,0xBB,0xA0,0x02,0xE8,0x5A,0x09,0xAB,0xDB,0xE3,0xBA,0xC6,0x7C,0xC3,0x10,0xDD,
                    0x39,0x05,0x96,0x30,0xF5,0x37,0x60,0x82,0x8C,0xC9,0x13,0x4A,0x6B,0x1D,0xF3,0xFB,
                    0x8F,0x26,0x97,0xCA,0x91,0x17,0x01,0xC4,0x32,0x2D,0x6E,0x31,0x95,0xFF,0xD9,0x23,
                    0xD1,0x00,0x5E,0x79,0xDC,0x44,0x3B,0x1A,0x28,0xC5,0x61,0x57,0x20,0x90,0x3D,0x83,
                    0xB9,0x43,0xBE,0x67,0xD2,0x46,0x42,0x76,0xC0,0x6D,0x5B,0x7E,0xB2,0x0F,0x16,0x29,
                    0x3C,0xA9,0x03,0x54,0x0D,0xDA,0x5D,0xDF,0xF6,0xB7,0xC7,0x62,0xCD,0x8D,0x06,0xD3,
                    0x69,0x5C,0x86,0xD6,0x14,0xF7,0xA5,0x66,0x75,0xAC,0xB1,0xE9,0x45,0x21,0x70,0x0C,
                    0x87,0x9F,0x74,0xA4,0x22,0x4C,0x6F,0xBF,0x1F,0x56,0xAA,0x2E,0xB3,0x78,0x33,0x50,
                    0xB0,0xA3,0x92,0xBC,0xCF,0x19,0x1C,0xA7,0x63,0xCB,0x1E,0x4D,0x3E,0x4B,0x1B,0x9B,
                    0x4F,0xE7,0xF0,0xEE,0xAD,0x3A,0xB5,0x59,0x04,0xEA,0x40,0x55,0x25,0x51,0xE5,0x7A,
                    0x89,0x38,0x68,0x52,0x7B,0xFC,0x27,0xAE,0xD7,0xBD,0xFA,0x07,0xF4,0xCC,0x8E,0x5F,
                    0xEF,0x35,0x9C,0x84,0x2B,0x15,0xD5,0x77,0x34,0x49,0xB6,0x12,0x0A,0x7F,0x71,0x88,
                    0xFD,0x9D,0x18,0x41,0x7D,0x93,0xD8,0x58,0x2C,0xCE,0xFE,0x24,0xAF,0xDE,0xB8,0x36,
                    0xC8,0xA1,0x80,0xA6,0x99,0x98,0xA8,0x2F,0x0E,0x81,0x65,0x73,0xE4,0xC2,0xA2,0x8A,
                    0xD4,0xE1,0x11,0xD0,0x08,0x8B,0x2A,0xF2,0xED,0x9A,0x64,0x3F,0xC1,0x6C,0xF9,0xEC
                };

                // Build a Heap-on-Node (HN, bSig=0xEC, bClientSig=0xBC Property Context) block encrypted via mpbbR
                auto makeEncryptedPcBlock = [&](const std::vector<std::pair<uint16_t, std::wstring>>& strProps) {
                    // HID index 1 (0x20): BTHHEADER (8B: bType=0xB5, cbKey=2, cbEnt=6, bIdxLevels=0, hidRoot=0x40)
                    // HID index 2 (0x40): Leaf PC records (8B each: wPropId, wPropType=0x001F, dwValueHnid)
                    // HID index 3+ (0x60+): UTF-16LE string slices
                    std::vector<std::vector<uint8_t>> allocs(2 + strProps.size());
                    ByteBuf bthHdr;
                    bthHdr.U8(0xB5u); bthHdr.U8(2); bthHdr.U8(6); bthHdr.U8(0); bthHdr.U32LE(0x40u);
                    allocs[0] = bthHdr.b;

                    ByteBuf leafRecs;
                    for (size_t i = 0; i < strProps.size(); ++i) {
                        uint32_t valHid = static_cast<uint32_t>((i + 3u) << 5);
                        leafRecs.U16LE(strProps[i].first);
                        leafRecs.U16LE(0x001Fu); // PtypString (UTF-16LE)
                        leafRecs.U32LE(valHid);
                        ByteBuf sb;
                        sb.Utf16LE(strProps[i].second, false);
                        allocs[2 + i] = sb.b;
                    }
                    allocs[1] = leafRecs.b;

                    ByteBuf blk;
                    blk.U16LE(0);    // ibHnpm patched below
                    blk.U8(0xECu);   // bSig = HN
                    blk.U8(0xBCu);   // bClientSig = PC
                    blk.U32LE(0x20u);// hidUserRoot = HID #1 (0x20)
                    blk.U32LE(0);    // rgbFillLevel

                    std::vector<uint16_t> offsets;
                    offsets.push_back(static_cast<uint16_t>(blk.b.size()));
                    for (const auto& a : allocs) {
                        blk.Bytes(a.data(), a.size());
                        offsets.push_back(static_cast<uint16_t>(blk.b.size()));
                    }
                    if (blk.b.size() & 1u) blk.U8(0);
                    uint16_t ibHnpm = static_cast<uint16_t>(blk.b.size());
                    PutU16LE(blk.b.data(), ibHnpm);
                    blk.U16LE(static_cast<uint16_t>(allocs.size()));
                    blk.U16LE(static_cast<uint16_t>(allocs.size()));
                    for (uint16_t o : offsets) blk.U16LE(o);

                    for (uint8_t& byte : blk.b) byte = kMpbbR[byte]; // Permutative encryption!
                    return blk.b;
                };

                auto folderBlk = makeEncryptedPcBlock({
                    { 0x3001u, L"Incident_Response" } // PidTagDisplayName
                });
                auto msgBlk = makeEncryptedPcBlock({
                    { 0x0037u, L"Zero-Day Patch Deployment" },        // PidTagSubject
                    { 0x0C1Au, L"SecOps Lead" },                      // PidTagSenderName
                    { 0x0E04u, L"Platform Engineering" },             // PidTagDisplayTo
                    { 0x1000u, L"All production nodes patched cleanly." } // PidTagBody
                });

                // Layout:
                //   0..1023    : Unicode PST header (wVer=23, bCryptMethod=1 @ 513, NBT @ 1024, BBT @ 1536)
                //   1024..1535 : NBT root leaf page (512B, ptype=0x81)
                //   1536..2047 : BBT root leaf page (512B, ptype=0x80)
                //   2048..     : Encrypted data blocks (bid=0x04 folderBlk, bid=0x08 msgBlk)
                ByteBuf pst;
                pst.Zeros(2048);
                PutU32LE(pst.b.data() + 0,  0x4E444221u); // "!BDN"
                PutU16LE(pst.b.data() + 10, 23u);         // Unicode PST v23
                pst.b[512] = 0x80u;                       // bSentinel
                pst.b[513] = 0x01u;                       // bCryptMethod = 1 (NDB_CRYPT_PERMUTE)

                uint64_t offFolder = pst.b.size();
                pst.Bytes(folderBlk.data(), folderBlk.size());
                pst.PadTo((pst.b.size() + 63u) & ~size_t(63u));
                uint64_t offMsg = pst.b.size();
                pst.Bytes(msgBlk.data(), msgBlk.size());
                pst.PadTo((pst.b.size() + 63u) & ~size_t(63u));

                // Patch NDB header root pointers: ibRootNBT @ 224 = 1024, ibRootBBT @ 240 = 1536
                PutU32LE(pst.b.data() + 224, 1024u);
                PutU32LE(pst.b.data() + 240, 1536u);

                // Populate NBT leaf page @ 1024 (2 entries, cbEnt=32):
                //   Entry 0: Folder NID = 0x8022 (type 0x02 NORMAL_FOLDER), bidData=0x04, bidSub=0, nidParent=0x8022
                //   Entry 1: Message NID = 0x200024 (type 0x04 NORMAL_MESSAGE), bidData=0x08, bidSub=0, nidParent=0x8022
                uint8_t* nbt = pst.b.data() + 1024;
                PutU32LE(nbt + 0,  0x00008022u); PutU32LE(nbt + 8,  0x04u); PutU32LE(nbt + 24, 0x00008022u);
                PutU32LE(nbt + 32, 0x00200024u); PutU32LE(nbt + 40, 0x08u); PutU32LE(nbt + 56, 0x00008022u);
                nbt[488] = 2u;    // cEnt = 2
                nbt[490] = 32u;   // cbEnt = 32
                nbt[491] = 0u;    // cLevel = 0
                nbt[496] = 0x81u; // ptype = ptypeNBT

                // Populate BBT leaf page @ 1536 (2 entries, cbEnt=24):
                //   Entry 0: bid=0x04, ib=offFolder, cb=folderBlk.size()
                //   Entry 1: bid=0x08, ib=offMsg,    cb=msgBlk.size()
                uint8_t* bbt = pst.b.data() + 1536;
                PutU32LE(bbt + 0,  0x04u); PutU32LE(bbt + 8,  static_cast<uint32_t>(offFolder)); PutU16LE(bbt + 16, static_cast<uint16_t>(folderBlk.size()));
                PutU32LE(bbt + 24, 0x08u); PutU32LE(bbt + 32, static_cast<uint32_t>(offMsg));    PutU16LE(bbt + 40, static_cast<uint16_t>(msgBlk.size()));
                bbt[488] = 2u;    // cEnt = 2
                bbt[490] = 24u;   // cbEnt = 24
                bbt[491] = 0u;    // cLevel = 0
                bbt[496] = 0x80u; // ptype = ptypeBBT

                std::vector<uint8_t> badPst(600, 0);
                PutU32LE(badPst.data(), 0x4E444221u); // "!BDN" with wVer=0 and zeroed BBT/NBT

                ParsedOfficeDocument pstDoc, pstBadDoc;
                bool okPst = writeTempAndInspect(L"secops_archive.pst", pst.b, pstDoc);
                bool badPstRej = !writeTempAndInspect(L"empty_bad.pst", badPst, pstBadDoc);
                tc.Record(okPst && badPstRej &&
                          DocContainsToken(pstDoc, L"Incident_Response") &&
                          DocContainsToken(pstDoc, L"Zero-Day Patch Deployment") &&
                          DocContainsToken(pstDoc, L"SecOps Lead") &&
                          DocContainsToken(pstDoc, L"Platform Engineering") &&
                          DocContainsToken(pstDoc, L"All production nodes patched cleanly."),
                    L"DocsSystem .pst ([MS-PST] Unicode v23 Permutative Decryption, NBT/BBT, Folder & Message PC)");
            }

            // 9I. Adobe InDesign Markup Language Package (.idml): designmap.xml + Story Paragraphs & Table Grid
            {
                std::string designMap =
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                    "<Document DOMVersion=\"19.0\" Name=\"Annual_Report_2025\" StoryList=\"u101\">"
                    "<idPkg:Spread src=\"Spreads/Spread_u201.xml\"/>"
                    "<idPkg:Story src=\"Stories/Story_u101.xml\"/>"
                    "</Document>";
                std::string spreadXml =
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                    "<idPkg:Spread DOMVersion=\"19.0\"><Spread Self=\"u201\">"
                    "<Page Self=\"p1\" Name=\"1\"/><Page Self=\"p2\" Name=\"2\"/>"
                    "<TextFrame Self=\"tf1\" ParentStory=\"u101\"/>"
                    "</Spread></idPkg:Spread>";
                std::string storyXml =
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                    "<idPkg:Story DOMVersion=\"19.0\"><Story Self=\"u101\">"
                    "<ParagraphStyleRange AppliedParagraphStyle=\"ParagraphStyle/Heading 1\">"
                    "<CharacterStyleRange><Content>Executive Overview</Content></CharacterStyleRange><Br/>"
                    "</ParagraphStyleRange>"
                    "<ParagraphStyleRange AppliedParagraphStyle=\"ParagraphStyle/Body\">"
                    "<CharacterStyleRange><Content>Global revenue expanded by 28 percent year over year.</Content></CharacterStyleRange>"
                    "<Table HeaderRowCount=\"1\" BodyRowCount=\"1\" ColumnCount=\"2\">"
                    "<Cell Name=\"0:0\"><ParagraphStyleRange><CharacterStyleRange><Content>Region</Content></CharacterStyleRange></ParagraphStyleRange></Cell>"
                    "<Cell Name=\"1:0\"><ParagraphStyleRange><CharacterStyleRange><Content>Growth</Content></CharacterStyleRange></ParagraphStyleRange></Cell>"
                    "<Cell Name=\"0:1\"><ParagraphStyleRange><CharacterStyleRange><Content>EMEA</Content></CharacterStyleRange></ParagraphStyleRange></Cell>"
                    "<Cell Name=\"1:1\"><ParagraphStyleRange><CharacterStyleRange><Content>+31.4%</Content></CharacterStyleRange></ParagraphStyleRange></Cell>"
                    "</Table>"
                    "</ParagraphStyleRange>"
                    "</Story></idPkg:Story>";

                auto idmlZip = buildStoredZipMulti({
                    { "designmap.xml", std::vector<uint8_t>(designMap.begin(), designMap.end()) },
                    { "Spreads/Spread_u201.xml", std::vector<uint8_t>(spreadXml.begin(), spreadXml.end()) },
                    { "Stories/Story_u101.xml", std::vector<uint8_t>(storyXml.begin(), storyXml.end()) }
                });
                auto nonIdmlZip = BuildStoredZipSingle("notes.xml", "<note>Not an IDML package</note>");

                ParsedOfficeDocument idmlDoc, idmlBadDoc;
                bool okIdml = writeTempAndInspect(L"annual_report.idml", idmlZip, idmlDoc);
                bool badIdmlRej = !writeTempAndInspect(L"fake.idml", nonIdmlZip, idmlBadDoc);
                tc.Record(okIdml && badIdmlRej &&
                          !idmlDoc.paragraphs.empty() &&
                          DocContainsToken(idmlDoc, L"Annual_Report_2025") &&
                          DocContainsToken(idmlDoc, L"Executive Overview") &&
                          DocContainsToken(idmlDoc, L"Global revenue expanded by 28 percent") &&
                          DocContainsToken(idmlDoc, L"EMEA") &&
                          DocContainsToken(idmlDoc, L"+31.4%"),
                    L"DocsSystem .idml (Adobe InDesign UCF Package, StoryList Headings/Paragraphs, Spreads & Table Grid)");
            }

            // 9J. RFC 2557 MIME HTML Web Archive (.mht / .mhtml): multipart/related Quoted-Printable HTML + Base64 Sub-Resource
            {
                std::string mht =
                    "From: <saved@explorer2.local>\r\n"
                    "Subject: Architecture Specification Snapshot\r\n"
                    "Date: Wed, 15 Jan 2025 10:30:00 +0000\r\n"
                    "Snapshot-Content-Location: https://docs.explorer2.dev/spec/v3\r\n"
                    "MIME-Version: 1.0\r\n"
                    "Content-Type: multipart/related; boundary=\"----=_NextPart_E2_001\"; type=\"text/html\"\r\n"
                    "\r\n"
                    "------=_NextPart_E2_001\r\n"
                    "Content-Type: text/html; charset=\"utf-8\"\r\n"
                    "Content-Transfer-Encoding: quoted-printable\r\n"
                    "Content-Location: https://docs.explorer2.dev/spec/v3\r\n"
                    "\r\n"
                    "<html><head><title>Zero-Copy Storage Engine</title></head><body>\r\n"
                    "<h1>Memory-Mapped B+Tree</h1>\r\n"
                    "<p>Latency=3D0.42ms at 99.9th percentile.</p>\r\n"
                    "<table><tr><th>Metric</th><th>Value</th></tr><tr><td>Throughput</td><td>1.8M ops/s</td></tr></table>\r\n"
                    "</body></html>\r\n"
                    "------=_NextPart_E2_001\r\n"
                    "Content-Type: image/png\r\n"
                    "Content-Transfer-Encoding: base64\r\n"
                    "Content-Location: https://docs.explorer2.dev/assets/diagram.png\r\n"
                    "\r\n"
                    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==\r\n"
                    "------=_NextPart_E2_001--\r\n";

                std::string badMht = "Random binary noise without MIME headers or HTML";
                ParsedOfficeDocument mhtDoc, mhtBadDoc;
                bool okMht = writeTempAndInspect(L"snapshot.mhtml", std::vector<uint8_t>(mht.begin(), mht.end()), mhtDoc);
                bool badMhtRej = !writeTempAndInspect(L"invalid.mht", std::vector<uint8_t>(badMht.begin(), badMht.end()), mhtBadDoc);
                tc.Record(okMht && badMhtRej &&
                          DocContainsToken(mhtDoc, L"Zero-Copy Storage Engine") &&
                          DocContainsToken(mhtDoc, L"Memory-Mapped B+Tree") &&
                          DocContainsToken(mhtDoc, L"Latency=0.42ms") && // =3D decoded via Quoted-Printable!
                          DocContainsToken(mhtDoc, L"1.8M ops/s") &&
                          DocContainsToken(mhtDoc, L"diagram.png") &&
                          DocContainsToken(mhtDoc, L"image/png"),
                    L"DocsSystem .mht/.mhtml (RFC 2557 multipart/related QP HTML + Table + Base64 Embedded_Resources)");
            }

            // 9K. Apple Property List (.plist): BOTH Binary "bplist00" (Dict, Array, UTF-16BE, NSDate) AND XML <plist>
            {
                // Build a genuine bplist00 with 7 objects:
                //   0: NSDictionary { keys=[1,2,3], vals=[4,5,6] }
                //   1: ASCII "BundleName"
                //   2: ASCII "BuildTimestamp"
                //   3: ASCII "Capabilities"
                //   4: UTF-16BE (0x60) L"Explorer2 Pro \x03A9" (tests 0x60 UTF-16BE decoding!)
                //   5: NSDate (0x33) 757382400.0 seconds since 2001-01-01 -> 2025-01-01 00:00:00 UTC!
                //   6: NSArray [1] -> ["BundleName"] (or add object 7 ASCII "MetalGPU")
                ByteBuf bp;
                bp.Bytes("bplist00", 8);
                std::vector<uint8_t> offs;
                auto markObj = [&]() { offs.push_back(static_cast<uint8_t>(bp.b.size())); };

                markObj(); // #0: NSDictionary of 3 pairs: keys [1,2,3], vals [4,5,6]
                bp.U8(0xD3u); bp.U8(1); bp.U8(2); bp.U8(3); bp.U8(4); bp.U8(5); bp.U8(6);

                markObj(); // #1: ASCII "BundleName" (10 chars)
                bp.U8(0x5Au); bp.Str("BundleName");

                markObj(); // #2: ASCII "BuildTimestamp" (14 chars)
                bp.U8(0x5Eu); bp.Str("BuildTimestamp");

                markObj(); // #3: ASCII "Capabilities" (12 chars)
                bp.U8(0x5Cu); bp.Str("Capabilities");

                markObj(); // #4: UTF-16BE (0x60 | 0x0F + int 0x10 len=15): "Explorer2_Omega" in UTF-16BE
                std::wstring u16Val = L"Explorer2_\x03A9"; // 11 UTF-16 code units -> 0x6B!
                bp.U8(static_cast<uint8_t>(0x60u | u16Val.size()));
                for (wchar_t ch : u16Val) bp.U16BE(static_cast<uint16_t>(ch));

                markObj(); // #5: NSDate (0x33 + float64 BE = 757382400.0 -> 2025-01-01 00:00:00 UTC)
                bp.U8(0x33u);
                double nsDateSec = 757382400.0; // (2025-01-01 - 2001-01-01) = 8766 days * 86400 = 757,382,400 s
                uint64_t dateBits = 0; std::memcpy(&dateBits, &nsDateSec, 8);
                bp.U32BE(static_cast<uint32_t>(dateBits >> 32));
                bp.U32BE(static_cast<uint32_t>(dateBits & 0xFFFFFFFFULL));

                markObj(); // #6: NSArray of 1 element -> #7
                bp.U8(0xA1u); bp.U8(7);

                markObj(); // #7: ASCII "MetalCompute" (12 chars)
                bp.U8(0x5Cu); bp.Str("MetalCompute");

                uint64_t offTblOff = bp.b.size();
                for (uint8_t o : offs) bp.U8(o);
                // 32-byte bplist00 trailer: 6 unused + offsetIntSize(1) + objectRefSize(1) + numObjects(8B BE) + topObject(8B BE) + offsetTableOffset(8B BE)
                bp.Zeros(6);
                bp.U8(1); // offsetIntSize = 1
                bp.U8(1); // objectRefSize = 1
                bp.U32BE(0); bp.U32BE(static_cast<uint32_t>(offs.size()));
                bp.U32BE(0); bp.U32BE(0); // topObject = 0
                bp.U32BE(0); bp.U32BE(static_cast<uint32_t>(offTblOff));

                std::string xmlPlist =
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                    "<plist version=\"1.0\"><dict>"
                    "<key>CFBundleIdentifier</key><string>com.deepmind.explorer2</string>"
                    "<key>MinimumSystemVersion</key><integer>14</integer>"
                    "<key>SupportsHiDPI</key><true/>"
                    "<key>SupportedArchitectures</key><array><string>arm64</string><string>x86_64</string></array>"
                    "</dict></plist>";

                // Corrupt bplist00: valid magic, zeroed invalid trailer (offsetIntSize=0)
                ByteBuf badBp;
                badBp.Bytes("bplist00", 8);
                badBp.Zeros(32);

                ParsedOfficeDocument bpDoc, xpDoc, badBpDoc;
                bool okBp = writeTempAndInspect(L"Info_bin.plist", bp.b, bpDoc);
                bool okXp = writeTempAndInspect(L"Info_xml.plist", std::vector<uint8_t>(xmlPlist.begin(), xmlPlist.end()), xpDoc);
                bool badBpRej = !writeTempAndInspect(L"corrupt_trailer.plist", badBp.b, badBpDoc);
                tc.Record(okBp && okXp && badBpRej &&
                          DocContainsToken(bpDoc, L"Explorer2_\x03A9") &&
                          DocContainsToken(bpDoc, L"2025-01-01 00:00:00 UTC") &&
                          DocContainsToken(bpDoc, L"MetalCompute") &&
                          DocContainsToken(xpDoc, L"com.deepmind.explorer2") &&
                          DocContainsToken(xpDoc, L"arm64") &&
                          DocContainsToken(xpDoc, L"true"),
                    L"DocsSystem .plist (Apple bplist00 UTF-16BE/NSDate/Dict/Array + XML Plist & Corrupt Trailer Reject)");
            }

            // 9L. Windows Registry Binary Hive (.hiv) & EVTX Anti-Scrape Verification
            {
                std::vector<uint8_t> hiv(8192, 0u);
                std::memcpy(hiv.data(), "regf", 4);
                PutU32LE(hiv.data() + 4,  7u);
                PutU32LE(hiv.data() + 8,  7u);
                PutU64LE(hiv.data() + 12, 133801632000000000ULL);
                PutU32LE(hiv.data() + 20, 1u);
                PutU32LE(hiv.data() + 24, 5u);
                PutU32LE(hiv.data() + 36, 0x20u);   // rootCell at hbin+0x20
                PutU32LE(hiv.data() + 40, 4096u);   // hbinsLen
                const std::wstring hiveLabel = L"SYSTEM_FORENSIC_HIVE";
                for (size_t i = 0; i < hiveLabel.size(); ++i) {
                    PutU16LE(hiv.data() + 48 + i * 2, static_cast<uint16_t>(hiveLabel[i]));
                }

                uint8_t* hb = hiv.data() + 4096;
                std::memcpy(hb, "hbin", 4);
                PutU32LE(hb + 4, 0u);
                PutU32LE(hb + 8, 4096u);

                uint32_t curRel = 0x20u;
                auto allocCell = [&](const std::vector<uint8_t>& pay) -> uint32_t {
                    const uint32_t rel = curRel;
                    const uint32_t cellSz = static_cast<uint32_t>((4u + pay.size() + 7u) & ~size_t(7u));
                    PutI32LE(hb + rel, -static_cast<int32_t>(cellSz));
                    if (!pay.empty()) std::memcpy(hb + rel + 4u, pay.data(), pay.size());
                    curRel += cellSz;
                    return rel;
                };

                // External REG_SZ payload cell: L"NvmeBootStor\0"
                ByteBuf szData;
                szData.Utf16LE(L"NvmeBootStor", true);
                const uint32_t offSzData = allocCell(szData.b);

                // vk #0: inline REG_DWORD "Start" = 0x00000002
                ByteBuf vk0;
                vk0.Str("vk"); vk0.U16LE(5);
                vk0.U32LE(0x80000004u); // inline 4B
                vk0.U32LE(2u);
                vk0.U32LE(4u);          // REG_DWORD
                vk0.U16LE(1u);          // ASCII value name
                vk0.U16LE(0u);
                vk0.Str("Start");
                const uint32_t offVk0 = allocCell(vk0.b);

                // vk #1: external REG_SZ "ImagePath" -> offSzData
                ByteBuf vk1;
                vk1.Str("vk"); vk1.U16LE(9);
                vk1.U32LE(static_cast<uint32_t>(szData.b.size()));
                vk1.U32LE(offSzData);
                vk1.U32LE(1u); // REG_SZ
                vk1.U16LE(1u); // ASCII value name
                vk1.U16LE(0u);
                vk1.Str("ImagePath");
                const uint32_t offVk1 = allocCell(vk1.b);

                // Value list for child key (2 values)
                ByteBuf vList;
                vList.U32LE(offVk0);
                vList.U32LE(offVk1);
                const uint32_t offValList = allocCell(vList.b);

                // Child nk cell: "Services" (owns 2 values)
                ByteBuf childNk;
                childNk.Zeros(76);
                childNk.b[0] = 'n'; childNk.b[1] = 'k';
                PutU16LE(childNk.b.data() + 2,  0x0020u); // ASCII name
                PutU32LE(childNk.b.data() + 20, 0u);      // 0 subkeys
                PutU32LE(childNk.b.data() + 28, 0xFFFFFFFFu);
                PutU32LE(childNk.b.data() + 36, 2u);      // 2 values
                PutU32LE(childNk.b.data() + 40, offValList);
                PutU16LE(childNk.b.data() + 72, 8u);
                childNk.Str("Services");
                const uint32_t offChildNk = allocCell(childNk.b);

                // Subkey list "lf" pointing to childNk
                ByteBuf lf;
                lf.Str("lf"); lf.U16LE(1u);
                lf.U32LE(offChildNk);
                lf.Str("Serv");
                const uint32_t offLf = allocCell(lf.b);

                // Place root nk cell at 0x20? Wait: rootCell in header can point directly to offRootNk!
                ByteBuf rootNk;
                rootNk.Zeros(76);
                rootNk.b[0] = 'n'; rootNk.b[1] = 'k';
                PutU16LE(rootNk.b.data() + 2,  0x002Cu); // Root + ASCII name
                PutU32LE(rootNk.b.data() + 20, 1u);      // 1 subkey
                PutU32LE(rootNk.b.data() + 28, offLf);
                PutU32LE(rootNk.b.data() + 36, 0u);
                PutU32LE(rootNk.b.data() + 40, 0xFFFFFFFFu);
                PutU16LE(rootNk.b.data() + 72, 12u);
                rootNk.Str("ControlSet01");
                const uint32_t offRootNk = allocCell(rootNk.b);
                PutU32LE(hiv.data() + 36, offRootNk);

                // EVTX anti-scrape check: 0x2A2A record with invalid BinXML token (0xFE) followed by raw UTF-16LE text
                std::vector<uint8_t> unscrapedEvtx = MakeEvtx(99, L"ValidProvider");
                uint8_t* badRecBxml = unscrapedEvtx.data() + 4096 + 512 + 24;
                badRecBxml[4] = 0xFEu; // invalid BinXML token right after FragmentHeader
                const std::wstring fakeScrape = L"UnscrapedRawUtf16TrapString";
                for (size_t i = 0; i < fakeScrape.size(); ++i) {
                    PutU16LE(badRecBxml + 6 + i * 2, static_cast<uint16_t>(fakeScrape[i]));
                }

                ParsedOfficeDocument hivDoc, hivBadDoc, evTrapDoc;
                bool okHiv = SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".hiv") &&
                             writeTempAndInspect(L"system_forensic.hiv", hiv, hivDoc);
                bool badHivRej = !writeTempAndInspect(L"corrupt.hiv", kGarbage, hivBadDoc);
                bool okEvTrap = writeTempAndInspect(L"trap_unscraped.evtx", unscrapedEvtx, evTrapDoc);
                tc.Record(okHiv && badHivRej && okEvTrap &&
                          DocContainsToken(hivDoc, L"SYSTEM_FORENSIC_HIVE") &&
                          DocContainsToken(hivDoc, L"\\ControlSet01\\Services") &&
                          DocContainsToken(hivDoc, L"REG_DWORD") &&
                          DocContainsToken(hivDoc, L"NvmeBootStor") &&
                          !DocContainsToken(evTrapDoc, L"UnscrapedRawUtf16TrapString"),
                    L"DocsSystem .hiv & .evtx (regf/hbin nk+lf+vk Key Tree & EVTX BinXML Anti-Scrape Guard)");
            }

            // 9M. Java KeyStore (.jks), PKCS#12 (.pfx / .p12) & PKCS#10 Certificate Signing Request (.csr)
            {
                auto jksCert = MakeX509Der(0x33, "jks-vault.explorer2.internal", "jks-vault.explorer2.internal");
                ByteBuf jks;
                jks.U32BE(0xFEEDFEEDu); // JKS magic
                jks.U32BE(2u);          // version 2
                jks.U32BE(1u);          // 1 entry
                jks.U32BE(2u);          // TrustedCertificateEntry
                jks.U16BE(12u); jks.Str("prod_root_ca");
                AppendU64BE(jks.b, 1735689600000ULL); // 2025-01-01 00:00:00 UTC
                jks.U16BE(5u);  jks.Str("X.509");
                jks.U32BE(static_cast<uint32_t>(jksCert.size()));
                jks.Bytes(jksCert.data(), jksCert.size());
                jks.Zeros(20); // SHA-1 whitening digest trailer

                ByteBuf badJks;
                badJks.U32BE(0xFEEDFEEDu); badJks.U32BE(99u); badJks.U32BE(1u);

                // PKCS#12 PFX v3 with certBag (1.2.840.113549.1.12.10.1.3) + friendlyName + SHA-256 MacData
                auto pfxCert = MakeX509Der(0x55, "pfx-gateway.explorer2.internal", "pfx-gateway.explorer2.internal");
                std::vector<uint8_t> certBagBody = {
                    0x06, 0x0A, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x16, 0x01 // x509Certificate
                };
                Asn1Append(certBagBody, Asn1Wrap(0xA0, Asn1Wrap(0x04, pfxCert)));
                // friendlyName (1.2.840.113549.1.9.20) BMPString "Gateway TLS"
                std::vector<uint8_t> bmpFriendly;
                for (char c : std::string_view("Gateway TLS")) { bmpFriendly.push_back(0u); bmpFriendly.push_back(static_cast<uint8_t>(c)); }
                std::vector<uint8_t> fnAttr = {
                    0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x14
                };
                Asn1Append(fnAttr, Asn1Wrap(0x31, Asn1Wrap(0x1E, bmpFriendly)));

                std::vector<uint8_t> safeBag = {
                    0x06, 0x0B, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x0C, 0x0A, 0x01, 0x03 // certBag
                };
                Asn1Append(safeBag, Asn1Wrap(0xA0, Asn1Wrap(0x30, certBagBody)));
                Asn1Append(safeBag, Asn1Wrap(0x31, Asn1Wrap(0x30, fnAttr)));
                std::vector<uint8_t> safeContents = Asn1Wrap(0x30, Asn1Wrap(0x30, safeBag));

                std::vector<uint8_t> innerCi = { 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x01 };
                Asn1Append(innerCi, Asn1Wrap(0xA0, Asn1Wrap(0x04, safeContents)));
                std::vector<uint8_t> authSafeSeq = Asn1Wrap(0x30, Asn1Wrap(0x30, innerCi));

                std::vector<uint8_t> outerCi = { 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x01 };
                Asn1Append(outerCi, Asn1Wrap(0xA0, Asn1Wrap(0x04, authSafeSeq)));

                std::vector<uint8_t> sha256Alg = Asn1Wrap(0x30, { 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00 });
                std::vector<uint8_t> digestInfo;
                Asn1Append(digestInfo, sha256Alg);
                Asn1Append(digestInfo, Asn1Wrap(0x04, std::vector<uint8_t>(32, 0x5Au)));
                std::vector<uint8_t> macData;
                Asn1Append(macData, Asn1Wrap(0x30, digestInfo));
                Asn1Append(macData, Asn1Wrap(0x04, std::vector<uint8_t>(8, 0x33u)));
                Asn1Append(macData, { 0x02, 0x02, 0x08, 0x00 }); // iterations = 2048

                std::vector<uint8_t> pfxSeq = { 0x02, 0x01, 0x03 }; // version 3
                Asn1Append(pfxSeq, Asn1Wrap(0x30, outerCi));
                Asn1Append(pfxSeq, Asn1Wrap(0x30, macData));
                std::vector<uint8_t> pfxDer = Asn1Wrap(0x30, pfxSeq);

                std::vector<uint8_t> badPfxSeq = { 0x02, 0x01, 0x01 }; // invalid version 1
                std::vector<uint8_t> badPfxDer = Asn1Wrap(0x30, badPfxSeq);

                // PKCS#10 CSR (RFC 2986): CertificationRequest v1(0) + CN="csr-edge.explorer2.internal" + RSA 512 + SHA256withRSA
                std::vector<uint8_t> csrCnAtv = { 0x06, 0x03, 0x55, 0x04, 0x03 };
                std::string_view csrCn = "csr-edge.explorer2.internal";
                Asn1Append(csrCnAtv, Asn1Wrap(0x0C, std::vector<uint8_t>(csrCn.begin(), csrCn.end())));
                std::vector<uint8_t> csrSubj = Asn1Wrap(0x30, Asn1Wrap(0x31, Asn1Wrap(0x30, csrCnAtv)));

                std::vector<uint8_t> rsaAlg = Asn1Wrap(0x30, { 0x06,0x09, 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01, 0x05,0x00 });
                std::vector<uint8_t> modBytes(65, 0xCDu);
                modBytes[0] = 0x00u; modBytes[1] = 0xD1u; modBytes[64] = 0x01u;
                std::vector<uint8_t> rsaPub;
                Asn1Append(rsaPub, Asn1Wrap(0x02, modBytes));
                Asn1Append(rsaPub, { 0x02, 0x03, 0x01, 0x00, 0x01 });
                std::vector<uint8_t> spkiBit = { 0x00u };
                Asn1Append(spkiBit, Asn1Wrap(0x30, rsaPub));
                std::vector<uint8_t> spkiBody;
                Asn1Append(spkiBody, rsaAlg);
                Asn1Append(spkiBody, Asn1Wrap(0x03, spkiBit));

                std::vector<uint8_t> criBody = { 0x02, 0x01, 0x00 }; // version v1(0)
                Asn1Append(criBody, csrSubj);
                Asn1Append(criBody, Asn1Wrap(0x30, spkiBody));
                Asn1Append(criBody, { 0xA0, 0x00 }); // empty [0] IMPLICIT Attributes

                std::vector<uint8_t> csrSigAlg = Asn1Wrap(0x30, { 0x06,0x09, 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0B, 0x05,0x00 });
                std::vector<uint8_t> sigBits(65, 0x77u);
                sigBits[0] = 0x00u;
                std::vector<uint8_t> csrOuter;
                Asn1Append(csrOuter, Asn1Wrap(0x30, criBody));
                Asn1Append(csrOuter, csrSigAlg);
                Asn1Append(csrOuter, Asn1Wrap(0x03, sigBits));
                std::vector<uint8_t> csrDer = Asn1Wrap(0x30, csrOuter);
                std::vector<uint8_t> csrPem = WrapDerAsPem(csrDer, "CERTIFICATE REQUEST");

                ParsedOfficeDocument jksDoc, badJksDoc, pfxDoc, badPfxDoc, csrDoc, badCsrDoc;
                bool okJks  = SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".jks") &&
                              writeTempAndInspect(L"truststore.jks", jks.b, jksDoc);
                bool rejJks = !writeTempAndInspect(L"bad.jks", badJks.b, badJksDoc);
                bool okPfx  = SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".pfx") &&
                              SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".p12") &&
                              writeTempAndInspect(L"bundle.p12", pfxDer, pfxDoc);
                bool rejPfx = !writeTempAndInspect(L"bad.pfx", badPfxDer, badPfxDoc);
                bool okCsr  = SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".csr") &&
                              writeTempAndInspect(L"edge_req.csr", csrPem, csrDoc);
                bool rejCsr = !writeTempAndInspect(L"bad.csr", kGarbage, badCsrDoc);

                tc.Record(okJks && rejJks && okPfx && rejPfx && okCsr && rejCsr &&
                          DocContainsToken(jksDoc, L"prod_root_ca") &&
                          DocContainsToken(jksDoc, L"jks-vault.explorer2.internal") &&
                          DocContainsToken(pfxDoc, L"Gateway TLS") &&
                          DocContainsToken(pfxDoc, L"pfx-gateway.explorer2.internal") &&
                          DocContainsToken(pfxDoc, L"Iterations=2048") &&
                          DocContainsToken(csrDoc, L"csr-edge.explorer2.internal") &&
                          DocContainsToken(csrDoc, L"512 bits"),
                    L"DocsSystem .jks, .p12/.pfx & .csr (JKS v2 Vault, PKCS#12 SafeBag/MacData & PKCS#10 CSR)");
            }

            // 9N. Guitar Pro 4 (.gp4) Full Measure, Track, Tuning & Beat/Note Table Verification
            {
                ByteBuf gp4;
                std::string ver = "FICHIER GUITAR PRO v4.06";
                gp4.U8(static_cast<uint8_t>(ver.size()));
                gp4.FixedStr(ver, 30);
                auto putGpStr = [&](std::string_view s) {
                    gp4.U32LE(static_cast<uint32_t>(s.size() + 1u));
                    gp4.U8(static_cast<uint8_t>(s.size()));
                    gp4.Str(s);
                };
                putGpStr("Hyperion Etude");
                putGpStr("");
                putGpStr("Studio Ensemble");
                putGpStr(" Chamber Works");
                putGpStr("");
                putGpStr("");
                putGpStr("");
                putGpStr("");
                gp4.U32LE(0u); // 0 notices
                gp4.U8(0u);    // tripletFeel = false
                gp4.U32LE(0u); // lyricTrack = 0
                for (int i = 0; i < 5; ++i) { gp4.U32LE(0u); gp4.U32LE(0u); }
                gp4.U32LE(140u); // tempo = 140 BPM
                gp4.U8(0u);      // key = C
                gp4.U32LE(0u);   // octave = 0
                gp4.Zeros(768);  // 64 MIDI channel ports
                gp4.U32LE(2u);   // 2 measures
                gp4.U32LE(1u);   // 1 track

                // Measure 0: 4/4 + marker "Intro Riff"
                gp4.U8(0x01u | 0x02u | 0x20u);
                gp4.U8(4u); gp4.U8(4u);
                putGpStr("Intro Riff");
                gp4.U32LE(0x00FF8000u);
                // Measure 1: repeatOpen
                gp4.U8(0x04u);

                // Track 0: 6-string melodic guitar ("Lead Synth Guitar"), standard E2..E4 tuning
                gp4.U8(0u);
                std::string trkName = "Lead Synth Guitar";
                gp4.U8(static_cast<uint8_t>(trkName.size()));
                gp4.FixedStr(trkName, 40);
                gp4.U32LE(6u);
                const int32_t stdTuning[7] = { 64, 59, 55, 50, 45, 40, 0 };
                for (int s = 0; s < 7; ++s) gp4.U32LE(static_cast<uint32_t>(stdTuning[s]));
                gp4.U32LE(1u); gp4.U32LE(1u); gp4.U32LE(2u); gp4.U32LE(24u); gp4.U32LE(2u); // port, chan, fxChan, 24 frets, capo 2
                gp4.U8(0xFFu); gp4.U8(0x80u); gp4.U8(0x00u); gp4.U8(0u);

                // Measure 0 beats (2 quarter notes on string 6)
                gp4.U32LE(2u);
                for (int b = 0; b < 2; ++b) {
                    gp4.U8(0u);    // bFlags
                    gp4.U8(0u);    // quarter note
                    gp4.U8(0x40u); // string bit 6
                    gp4.U8(0x30u); // noteType + velocity + fret
                    gp4.U8(1u);    // normal note
                    gp4.U8(6u);    // forte
                    gp4.U8(static_cast<uint8_t>(5 + b * 2));
                }
                // Measure 1 beat (1 half note)
                gp4.U32LE(1u);
                gp4.U8(0u);
                gp4.U8(static_cast<uint8_t>(-1)); // half note
                gp4.U8(0x20u); // string bit 5
                gp4.U8(0x20u); // noteType + fret
                gp4.U8(1u);
                gp4.U8(7u);

                ParsedOfficeDocument gp4Doc, badGp4Doc;
                bool okGp4  = writeTempAndInspect(L"hyperion.gp4", gp4.b, gp4Doc);
                bool rejGp4 = !writeTempAndInspect(L"bad.gp4", kGarbage, badGp4Doc);
                tc.Record(okGp4 && rejGp4 &&
                          DocContainsToken(gp4Doc, L"Hyperion Etude") &&
                          DocContainsToken(gp4Doc, L"140 BPM") &&
                          DocContainsToken(gp4Doc, L"Lead Synth Guitar") &&
                          DocContainsToken(gp4Doc, L"E2 A2 D3 G3 B3 E4") &&
                          DocContainsToken(gp4Doc, L"Intro Riff"),
                    L"DocsSystem .gp4 (Guitar Pro 4 Measures, Track Tuning, Capo & Beat/Note Stream)");
            }
        }
    }

    RemoveDirectoryW(dir.c_str());
}

} // namespace Explorer2::SelfTestSuite
