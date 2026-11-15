// FormatTestsDocsB.cpp: self-tests of the binary, media and system document readers (SpreadsheetDocViewer, formats/Extended*), stage 15 ("15.docsb" runs these alone)
#include "SelfTestCommon.h"
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

static void PutU16LE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}
static void PutU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

static uint32_t CalcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static std::vector<uint8_t> BuildStoredZipSingle(std::string_view innerPath, std::string_view content) {
    const auto* payload = reinterpret_cast<const uint8_t*>(content.data());
    uint32_t crc = CalcCrc32(payload, content.size());
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
    uint32_t crc = CalcCrc32(payload, content.size());
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
static std::vector<uint8_t> MakeElf64(std::string_view neededSo, std::string_view symFuncName) {
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
static std::vector<uint8_t> MakeOutlookMsg(std::wstring_view subject, std::wstring_view sender, std::wstring_view body, std::wstring_view attachName) {
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

// 12. Windows Event Log (.evtx: 4096B ElfFile + 65536B ElfChnk + 0x2A2A BinXML record)
static std::vector<uint8_t> MakeEvtx(uint64_t recordId, std::wstring_view providerMsg) {
    ByteBuf ev;
    ev.Bytes("ElfFile\0", 8);
    ev.U64LE(0);
    ev.U64LE(1);
    ev.U64LE(recordId + 1);
    ev.U32LE(128);
    ev.U16LE(1);
    ev.U16LE(3);
    ev.U16LE(4096);
    ev.U16LE(1); // chunkCount = 1
    ev.PadTo(4096);

    size_t chnkBase = ev.b.size();
    ev.Bytes("ElfChnk\0", 8);
    ev.U64LE(1);
    ev.U64LE(recordId);
    ev.U64LE(1);
    ev.U64LE(recordId);
    ev.U32LE(128);
    ev.PadTo(chnkBase + 512);

    // Event Record @ chunk+512
    ByteBuf binXml;
    binXml.Bytes("\x0F\x01\x01\x00", 4); // BinXML fragment header
    binXml.U8(0x05);                     // ValueText token
    binXml.U8(0x01);                     // StringType UTF-16
    binXml.U16LE(static_cast<uint16_t>(providerMsg.size()));
    binXml.Utf16LE(providerMsg, false);
    binXml.U8(0x00);                     // EOF token

    uint32_t recLen = static_cast<uint32_t>(24 + binXml.b.size() + 4);
    if (recLen & 3u) recLen += 4 - (recLen & 3u);
    size_t recStart = ev.b.size();
    ev.U32LE(0x00002A2Au);
    ev.U32LE(recLen);
    ev.U64LE(recordId);
    ev.U64LE(133610000000000000ULL);
    ev.Bytes(binXml.b.data(), binXml.b.size());
    ev.PadTo(recStart + recLen - 4);
    ev.U32LE(recLen);

    PutU32LE(ev.b.data() + chnkBase + 40, static_cast<uint32_t>(ev.b.size() - chnkBase)); // freeSpaceOff
    ev.PadTo(chnkBase + 65536);
    return ev.b;
}

// 13. PDB v7.00 (MSF 7.00 with Stream 1 PDB GUID + Stream 3 DBI Module)
static std::vector<uint8_t> MakePdbMsf7(uint8_t guidLastByte, std::string_view objPath) {
    const uint32_t blkSz = 512;
    // Blocks: 0=SuperBlock, 1=FPM1, 2=FPM2, 3=BlockMapAddr, 4=StreamDir, 5=Stream1(PDB), 6=Stream3(DBI)
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
    dir.U32LE(5); // Stream 1 -> block 5
    dir.U32LE(6); // Stream 3 -> block 6
    uint32_t dirLen = static_cast<uint32_t>(dir.b.size());
    dir.PadTo(blkSz);

    ByteBuf pdb;
    pdb.Bytes("Microsoft C/C++ MSF 7.00\r\n\x1A\x44\x53\0\0\0", 32);
    pdb.U32LE(blkSz);
    pdb.U32LE(1); // freeBlockMapBlock
    pdb.U32LE(7); // numBlocks
    pdb.U32LE(dirLen);
    pdb.U32LE(0);
    pdb.U32LE(3); // blockMapAddr = block 3
    pdb.PadTo(blkSz);
    pdb.Zeros(blkSz * 2); // blocks 1 & 2 (FPM)
    // Block 3: blockMapAddr -> points to block 4
    pdb.U32LE(4);
    pdb.PadTo(blkSz * 4);
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

void RunFormatTestsDocsB(TestCounter& tc) {
    // 1. Existing testdata/sample.elf & testdata/sample.wasm regression check
    {
        std::wstring td = TestDataDir();
        ParsedOfficeDocument dElf, dWasm;
        bool okElf  = SpreadsheetDocViewer::InspectOfficeFile(td + L"\\sample.elf", dElf) && dElf.valid;
        bool okWasm = SpreadsheetDocViewer::InspectOfficeFile(td + L"\\sample.wasm", dWasm) && dWasm.valid && DocContainsToken(dWasm, L"add");
        tc.Record(okElf,  L"DocsB testdata/sample.elf compatibility");
        tc.Record(okWasm, L"DocsB testdata/sample.wasm compatibility");
    }

    std::wstring dir = ScratchTestDir() + L"\\DocsB";
    CreateDirectoryW(dir.c_str(), nullptr);

    const std::vector<uint8_t> kGarbage = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF, 0x13, 0x37, 0x42, 0x99 };

    // 2. ELF64 & WebAssembly
    VerifyPairAndCorrupt(tc, dir, L"DocsB .elf (ELF64 symbols & DT_NEEDED)", L".elf",
        MakeElf64("libm.so.6", "alpha_compute"), L"alpha_compute",
        MakeElf64("libvulkan.so.1", "beta_render"), L"beta_render",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .wasm (Imports, Exports & Signatures)", L".wasm",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .eml (RFC 2047 + MIME Multipart)", L".eml",
            std::vector<uint8_t>(emlA.begin(), emlA.end()), L"Quarterly Roadmap",
            std::vector<uint8_t>(emlB.begin(), emlB.end()), L"audit_report.pdf",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsB .msg (OLE2 MAPI Subject, Body & Attachment)", L".msg",
        MakeOutlookMsg(L"Architecture Review", L"Dr. Vance", L"Please review the propulsion schematic.", L"schematic_v1.pdf"), L"schematic_v1.pdf",
        MakeOutlookMsg(L"Budget Sign-Off Q4", L" CFO Office", L"Approved capital expenditure table.", L"capex_q4.xlsx"), L"capex_q4.xlsx",
        kGarbage);

    // 4. Audio & Tracker (.mp3, .ogg, .mod, .xm, .s3m, .it, .sf2, .sfz)
    VerifyPairAndCorrupt(tc, dir, L"DocsB .mp3 (ID3v2.3 UTF-16 + MPEG Layer III + Xing)", L".mp3",
        MakeMp3(L"Solar Horizon", "Synthwave Collective", 0xFFFB9064u, 417), L"Solar Horizon",
        MakeMp3(L"Nocturne in C#", "Classical Ensemble", 0xFFFBE000u, 1044), L"Nocturne in C#",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .ogg (Ogg Vorbis BOS + Comments + Granule Duration)", L".ogg",
        MakeOggVorbis(0x11223344u, 44100, 44100LL * 125, "Aurora Borealis"), L"Aurora Borealis",
        MakeOggVorbis(0x99887766u, 48000, 48000LL * 63, "Deep Ocean Echo"), L"Deep Ocean Echo",
        kGarbage);

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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .mod (ProTracker 4ch/8ch & Sample Headers)", L".mod",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .xm (FastTracker II Patterns & Instruments)", L".xm",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .s3m (ScreamTracker 3 Parapointers & Instruments)", L".s3m",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .it (Impulse Tracker IMPS Samples)", L".it",
            makeIt("Unreal Superhero", "Chiptune Square", 22050), L"Chiptune Square",
            makeIt("Enigma Horizon", "Orchestral Hit", 44100), L"Orchestral Hit",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsB .sf2 (SoundFont RIFF sfbk INFO & pdta)", L".sf2",
        MakeSf2("Concert Grand Bank", "Concert Piano", "Steinway_C4", 44100), L"Steinway_C4",
        MakeSf2("Vintage Synth Bank", "Analog Brass", "Oberheim_Saw", 48000), L"Oberheim_Saw",
        kGarbage);

    {
        std::string sfzA = "<global> volume=-3\n<region> sample=piano_c4.wav lokey=48 hikey=72 pitch_keycenter=60\n";
        std::string sfzB = "<group> lovel=64 hivel=127\n<region> sample=snare_rim.wav key=38 pan=10\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsB .sfz (SFZ Regions & Opcodes)", L".sfz",
            std::vector<uint8_t>(sfzA.begin(), sfzA.end()), L"piano_c4.wav",
            std::vector<uint8_t>(sfzB.begin(), sfzB.end()), L"snare_rim.wav",
            kGarbage);
    }

    // 5. Score, DAW & NLE (.mscz, .als, .flp, .gp, .rpp, .edl, .fcpxml, .otio, .idx, .sup, .sub)
    VerifyPairAndCorrupt(tc, dir, L"DocsB .mscz (MuseScore ZIP + XML Score)", L".mscz",
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

    VerifyPairAndCorrupt(tc, dir, L"DocsB .als (Ableton Gzip Live Set)", L".als",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .flp (FL Studio TLV Events)", L".flp",
            makeFlp("21.2.0", 128000, L"Festival Anthem", L"Sytrus"), L"Sytrus",
            makeFlp("20.9.2", 140500, L"LoFi Chillhop", L"Harmless"), L"Harmless",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsB .gp (Guitar Pro 7/8 GPIF ZIP)", L".gp",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .gp5 (Guitar Pro 5 Binary Score Header)", L".gp5",
            makeGp5("Glasgow Kiss", "John Petrucci"), L"Glasgow Kiss",
            makeGp5("Satch Boogie", "Joe Satriani"), L"Satch Boogie",
            kGarbage);
    }

    {
        std::string rppA = "<REAPER_PROJECT 0.1 \"7.11/win64\" 1700000000\n  TEMPO 120 4 4\n  <TRACK\n    NAME \"Lead Vocal\"\n    <ITEM\n      <SOURCE WAVE\n        FILE \"vox_take1.wav\"\n      >\n    >\n  >\n>\n";
        std::string rppB = "<REAPER_PROJECT 0.1 \"7.12/win64\" 1700000100\n  TEMPO 95 6 8\n  <TRACK\n    NAME \"Bass DI\"\n    <FXCHAIN\n      <VST \"ReaComp (Cockos)\"\n      >\n    >\n  >\n>\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsB .rpp (REAPER Project S-Expressions)", L".rpp",
            std::vector<uint8_t>(rppA.begin(), rppA.end()), L"vox_take1.wav",
            std::vector<uint8_t>(rppB.begin(), rppB.end()), L"ReaComp",
            kGarbage);
    }

    {
        std::string edlA = "TITLE: DOCUMENTARY_CUT_V1\n001  CAM_A    V     C        01:00:00:00 01:00:05:12 00:00:00:00 00:00:05:12\n* FROM CLIP NAME: INTERVIEW_WIDE.MOV\n";
        std::string edlB = "TITLE: COMMERCIAL_30S\n001  REEL_09  V     D    024 02:10:00:00 02:10:03:00 00:00:00:00 00:00:03:00\n* FROM CLIP NAME: PRODUCT_HERO.R3D\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsB .edl (CMX 3600 Timeline)", L".edl",
            std::vector<uint8_t>(edlA.begin(), edlA.end()), L"INTERVIEW_WIDE.MOV",
            std::vector<uint8_t>(edlB.begin(), edlB.end()), L"PRODUCT_HERO.R3D",
            kGarbage);
    }

    {
        std::string fcpA = "<?xml version=\"1.0\"?><fcpxml version=\"1.10\"><resources><format id=\"r1\" name=\"FFVideoFormat1080p24\" width=\"1920\" height=\"1080\"/><asset id=\"r2\" name=\"DroneShot\" src=\"file:///drone.mov\" duration=\"240/24s\"/></resources><library><asset-clip name=\"Opening Aerial\" ref=\"r2\" offset=\"0s\" duration=\"120/24s\"/></library></fcpxml>";
        std::string fcpB = "<?xml version=\"1.0\"?><fcpxml version=\"1.11\"><resources><format id=\"r1\" name=\"FFVideoFormat4K60\" width=\"3840\" height=\"2160\"/><asset id=\"r2\" name=\"StudioDialog\" src=\"file:///dialog.wav\" duration=\"600/60s\"/></resources><library><asset-clip name=\"Host Intro\" ref=\"r2\" offset=\"10s\" duration=\"300/60s\"/></library></fcpxml>";
        VerifyPairAndCorrupt(tc, dir, L"DocsB .fcpxml (Final Cut Pro XML)", L".fcpxml",
            std::vector<uint8_t>(fcpA.begin(), fcpA.end()), L"Opening Aerial",
            std::vector<uint8_t>(fcpB.begin(), fcpB.end()), L"Host Intro",
            kGarbage);
    }

    {
        std::string otioA = "{\"OTIO_SCHEMA\":\"Timeline.1\",\"name\":\"Episode1\",\"tracks\":{\"OTIO_SCHEMA\":\"Stack.1\",\"children\":[{\"OTIO_SCHEMA\":\"Track.1\",\"name\":\"V1\",\"kind\":\"Video\",\"children\":[{\"OTIO_SCHEMA\":\"Clip.1\",\"name\":\"Scene01_Take2\",\"media_reference\":{\"OTIO_SCHEMA\":\"ExternalReference.1\",\"target_url\":\"file:///scene01.mov\"},\"source_range\":{\"OTIO_SCHEMA\":\"TimeRange.1\",\"start_time\":{\"value\":0,\"rate\":24},\"duration\":{\"value\":120,\"rate\":24}}}]}]}}";
        std::string otioB = "{\"OTIO_SCHEMA\":\"Timeline.1\",\"name\":\"TrailerCut\",\"tracks\":{\"OTIO_SCHEMA\":\"Stack.1\",\"children\":[{\"OTIO_SCHEMA\":\"Track.1\",\"name\":\"A1\",\"kind\":\"Audio\",\"children\":[{\"OTIO_SCHEMA\":\"Clip.1\",\"name\":\"TrailerBoom\",\"media_reference\":{\"OTIO_SCHEMA\":\"ExternalReference.1\",\"target_url\":\"file:///boom_sfx.wav\"},\"source_range\":{\"OTIO_SCHEMA\":\"TimeRange.1\",\"start_time\":{\"value\":48,\"rate\":48},\"duration\":{\"value\":960,\"rate\":48}}}]}]}}";
        VerifyPairAndCorrupt(tc, dir, L"DocsB .otio (OpenTimelineIO JSON)", L".otio",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .sup (Blu-ray PGS Segments)", L".sup",
            makeSup(1920, 1080, 90000 * 12, 640, 82), L"640x82",
            makeSup(3840, 2160, 90000 * 45, 920, 120), L"920x120",
            kGarbage);
    }

    {
        std::string idxA = "# VobSub index file, v7\nsize: 720x480\nid: en, index: 0\ntimestamp: 00:01:12:340, filepos: 00001800\n";
        std::string idxB = "# VobSub index file, v7\nsize: 720x576\nid: fr, index: 1\ntimestamp: 00:05:44:120, filepos: 0009a000\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsB .idx (DVD VobSub Index)", L".idx",
            std::vector<uint8_t>(idxA.begin(), idxA.end()), L"720x480",
            std::vector<uint8_t>(idxB.begin(), idxB.end()), L"0009a000",
            kGarbage);

        std::string subA = "{120}{180}Welcome aboard the starship.|Prepare for launch.\n{200}{260}All engines nominal.\n";
        std::string subB = "{50}{110}Encrypted transmission received.\n{140}{210}Decoding coordinates now.\n";
        VerifyPairAndCorrupt(tc, dir, L"DocsB .sub (MicroDVD Frame Subtitles)", L".sub",
            std::vector<uint8_t>(subA.begin(), subA.end()), L"starship",
            std::vector<uint8_t>(subB.begin(), subB.end()), L"Encrypted transmission",
            kGarbage);
    }

    // 6. E-Books & Help (.mobi PalmDOC LZ77, .djvu, .chm)
    VerifyPairAndCorrupt(tc, dir, L"DocsB .mobi (PalmDB + EXTH + Real PalmDOC LZ77 Decompression)", L".mobi",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .djvu (AT&TFORM INFO & TXTa OCR)", L".djvu",
            makeDjvu(2480, 3508, 300, "Theorem 4.2: Every compact manifold admits a smooth partition."), L"300 DPI",
            makeDjvu(1654, 2339, 600, "Appendix B: Schematic pinout for transceiver module."), L"600 DPI",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsB .chm (ITSF/ITSP/PMGL ENCINT Directory)", L".chm",
        MakeChm("/reference/kernel_api.htm", "<html><body>Kernel API Reference Manual</body></html>"), L"kernel_api.htm",
        MakeChm("/tutorials/shader_graph.htm", "<html><body>PBR Shader Graph Guide</body></html>"), L"shader_graph.htm",
        kGarbage);

    // 7. System, Forensics, Runtime & Security (.dll, .lnk, .pf, .evtx, .pdb, .class, .dex, .macho, .cer, .pem, .kdbx, .pcapng, .uf2)
    VerifyPairAndCorrupt(tc, dir, L"DocsB .dll (PE32+ Import & Export Tables)", L".dll",
        MakePe64WithImpExp("KERNEL32.dll", "CreateFileW", "InitializePhysicsEngine"), L"InitializePhysicsEngine",
        MakePe64WithImpExp("ADVAPI32.dll", "RegOpenKeyExW", "CompileAudioGraph"), L"CompileAudioGraph",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .lnk (MS-SHLLINK Target & Arguments)", L".lnk",
        MakeShellLink("C:\\Tools\\Compiler\\clang-cl.exe", L"--target=x86_64-pc-windows-msvc -O3"), L"clang-cl.exe",
        MakeShellLink("D:\\Studio\\Blender\\blender.exe", L"--background scene.blend --render-frame 1"), L"blender.exe",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .pf (Windows Prefetch SCCA v30)", L".pf",
        MakePrefetchV30(L"POWERSHELL.EXE", 0xAABB1122u, 42, L"\\VOLUME{01}\\WINDOWS\\SYSTEM32\\KERNELBASE.DLL"), L"POWERSHELL.EXE",
        MakePrefetchV30(L"MSBUILD.EXE", 0xCCDD3344u, 19, L"\\VOLUME{01}\\PROGRAM FILES\\DOTNET\\HOSTFXR.DLL"), L"MSBUILD.EXE",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .evtx (Windows Event Log BinXML)", L".evtx",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .dmp (Windows Minidump Exception & Modules)", L".dmp",
            makeMinidump(0xC0000005u, L"C:\\Program Files\\App\\RenderCore.dll"), L"RenderCore.dll",
            makeMinidump(0xC0000094u, L"C:\\Windows\\System32\\dxgi.dll"), L"dxgi.dll",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsB .pdb (MSF v7.00 GUID & DBI Modules)", L".pdb",
        MakePdbMsf7(0xA1, "W:\\build\\obj\\RendererD3D12.obj"), L"RendererD3D12.obj",
        MakePdbMsf7(0xB2, "W:\\build\\obj\\AudioMixerWasapi.obj"), L"AudioMixerWasapi.obj",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .class (JVM Constant Pool & Methods)", L".class",
        MakeJavaClass("com/explorer2/OrderMatcher", "matchLimitOrder"), L"matchLimitOrder",
        MakeJavaClass("org/analytics/TensorKernel", "dispatchMatMul"), L"dispatchMatMul",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .dex (Dalvik Strings, Classes & Methods)", L".dex",
        MakeDex("Lcom/app/AuthManager;", "verifyPasskey"), L"verifyPasskey",
        MakeDex("Lcom/app/SyncWorker;", "uploadDeltaBatch"), L"uploadDeltaBatch",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .macho (Mach-O 64 Load Commands & Symtab)", L".macho",
        MakeMachO64(0x0100000Cu, "/usr/lib/libSystem.B.dylib", "_metal_encode_frame"), L"_metal_encode_frame",
        MakeMachO64(0x01000007u, "/System/Library/Frameworks/CoreAudio.framework/CoreAudio", "_audio_unit_render"), L"_audio_unit_render",
        kGarbage);

    {
        auto derA = MakeX509Der(0x11, "api.alpha-cloud.internal", "api.alpha-cloud.internal");
        auto derB = MakeX509Der(0x77, "vault.beta-sec.internal", "vault.beta-sec.internal");
        VerifyPairAndCorrupt(tc, dir, L"DocsB .cer (X.509 v3 DER Certificate + SAN)", L".cer",
            derA, L"api.alpha-cloud.internal",
            derB, L"vault.beta-sec.internal",
            kGarbage);
        VerifyPairAndCorrupt(tc, dir, L"DocsB .pem (PEM Armored X.509 Certificate)", L".pem",
            WrapDerAsPem(derA, "CERTIFICATE"), L"api.alpha-cloud.internal",
            WrapDerAsPem(derB, "CERTIFICATE"), L"vault.beta-sec.internal",
            kGarbage);
    }

    VerifyPairAndCorrupt(tc, dir, L"DocsB .kdbx (KeePass v4 Cipher & Argon2id Params)", L".kdbx",
        MakeKdbx4(false, 64ULL * 1024 * 1024, 12), L"AES-256",
        MakeKdbx4(true, 256ULL * 1024 * 1024, 32), L"ChaCha20",
        kGarbage);

    VerifyPairAndCorrupt(tc, dir, L"DocsB .pcapng (Ethernet/IPv4/TCP HTTP vs UDP DNS)", L".pcapng",
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
        VerifyPairAndCorrupt(tc, dir, L"DocsB .uf2 (UF2 Firmware Family & Flash Range)", L".uf2",
            makeUf2(0xE48BFF56u, 0x10000000u, "RP2040_BLINK_V1"), L"RP2040",
            makeUf2(0xC47E5767u, 0x00010000u, "ESP32S3_WIFI_V2"), L"ESP32-S3",
            kGarbage);
    }

    RemoveDirectoryW(dir.c_str());
}

} // namespace Explorer2::SelfTestSuite
