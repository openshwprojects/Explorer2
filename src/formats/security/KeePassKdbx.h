#pragma once
// KeePassKdbx.h — KeePass Password Database (.kdbx — v3.1 & v4.x)

namespace Formats {

// ============================================================================
// 48. KeePass Password Database (.kdbx — v3.1 & v4.0/v4.1 Outer Header & KDF)
// ============================================================================
inline bool ParseKeePassKdbx(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16 || DetailBinarySysDoc::RdU32LE(raw.data()) != 0x9AA2D903u ||
        DetailBinarySysDoc::RdU32LE(raw.data() + 4) != 0xB54BFB67u) {
        return false;
    }

    const uint16_t minVer = DetailBinarySysDoc::RdU16LE(raw.data() + 8);
    const uint16_t majVer = DetailBinarySysDoc::RdU16LE(raw.data() + 10);
    if (majVer != 3 && majVer != 4) return false;

    std::vector<std::vector<std::wstring>> rows = {
        { L"KeePass Header Field", L"Value" },
        { L"Signature", L"0x9AA2D903 0xB54BFB67 (KeePass 2.x KDBX)" },
        { L"Format Version", L"KDBX " + std::to_wstring(majVer) + L"." + std::to_wstring(minVer) }
    };

    static const uint8_t kUuidAes256[16]   = { 0x31,0xC1,0xF2,0xE6,0xBF,0x71,0x43,0x50,0xBE,0x58,0x05,0x21,0x6A,0xFC,0x5A,0xFF };
    static const uint8_t kUuidChaCha20[16] = { 0xD6,0x03,0x8A,0x2B,0x8B,0x6F,0x4C,0xB5,0xA5,0x24,0x33,0x9A,0x31,0xDB,0xB5,0x9A };
    static const uint8_t kUuidTwofish[16]  = { 0xAD,0x68,0xF2,0x9F,0x57,0x6F,0x4B,0xB9,0xA3,0x6A,0xD4,0x7A,0xF9,0x65,0x34,0x6C };

    static const uint8_t kKdfArgon2d[16]   = { 0xEF,0x63,0x6D,0xDF,0x8C,0x29,0x44,0x4B,0x91,0xF7,0xA9,0xA4,0x03,0xE3,0x0A,0x0C };
    static const uint8_t kKdfArgon2id[16]  = { 0x9E,0x29,0x8B,0x19,0x56,0xDB,0x47,0x73,0xB2,0x3D,0xFC,0x3E,0xC6,0xF0,0xA1,0xE6 };
    static const uint8_t kKdfAes[16]       = { 0xC9,0xD9,0xF3,0x9A,0x62,0x8A,0x44,0x60,0xBF,0x74,0x0D,0x08,0xC1,0x8A,0x4F,0xEA };

    size_t pos = 12;
    const size_t lenFieldSz = (majVer >= 4) ? 4 : 2;
    bool sawEndOfHeader = false;

    while (pos + 1 + lenFieldSz <= raw.size()) {
        const uint8_t fid = raw[pos++];
        const uint32_t flen = (majVer >= 4) ? DetailBinarySysDoc::RdU32LE(raw.data() + pos)
                                            : DetailBinarySysDoc::RdU16LE(raw.data() + pos);
        pos += lenFieldSz;
        if (pos + flen > raw.size()) return false;
        const uint8_t* fp = raw.data() + pos;

        if (fid == 0) {
            pos += flen;
            sawEndOfHeader = true;
            break;
        } else if (fid == 2 && flen == 16) { // CipherID
            std::wstring cname = DetailBinarySysDoc::HexBytes(fp, 16);
            if (std::memcmp(fp, kUuidAes256, 16) == 0) cname = L"AES-256-CBC (31C1F2E6...)";
            else if (std::memcmp(fp, kUuidChaCha20, 16) == 0) cname = L"ChaCha20 (D6038A2B...)";
            else if (std::memcmp(fp, kUuidTwofish, 16) == 0) cname = L"Twofish-256 (AD68F29F...)";
            rows.push_back({ L"Outer Cipher Algorithm", cname });
        } else if (fid == 3 && flen == 4) { // CompressionFlags
            const uint32_t cf = DetailBinarySysDoc::RdU32LE(fp);
            rows.push_back({ L"Inner Compression", (cf == 1) ? L"GZip (1)" : ((cf == 0) ? L"None (0)" : std::to_wstring(cf)) });
        } else if (fid == 4 && flen > 0) { // MasterSeed
            rows.push_back({ L"Master Seed (" + std::to_wstring(flen) + L" B)", DetailBinarySysDoc::HexBytes(fp, flen) });
        } else if (fid == 5 && flen > 0) { // TransformSeed (v3)
            rows.push_back({ L"KDF Transform Seed", DetailBinarySysDoc::HexBytes(fp, flen) });
        } else if (fid == 6 && flen == 8) { // TransformRounds (v3)
            rows.push_back({ L"AES-KDF Transform Rounds", std::to_wstring(DetailBinarySysDoc::RdU64LE(fp)) });
        } else if (fid == 7 && flen > 0) { // EncryptionIV
            rows.push_back({ L"Encryption IV (" + std::to_wstring(flen) + L" B)", DetailBinarySysDoc::HexBytes(fp, flen) });
        } else if (fid == 10 && flen == 4) { // InnerRandomStreamID (v3)
            const uint32_t irs = DetailBinarySysDoc::RdU32LE(fp);
            rows.push_back({ L"Inner Protected Stream", (irs == 2) ? L"Salsa20" : ((irs == 3) ? L"ChaCha20" : std::to_wstring(irs)) });
        } else if (fid == 11 && flen >= 3) { // KdfParameters VariantDictionary (v4)
            const uint16_t vdVer = DetailBinarySysDoc::RdU16LE(fp);
            if ((vdVer & 0xFF00u) == 0x0100u) {
                size_t vp = 2;
                while (vp < flen) {
                    const uint8_t vtype = fp[vp++];
                    if (vtype == 0) break;
                    if (vp + 4 > flen) break;
                    const uint32_t nLen = DetailBinarySysDoc::RdU32LE(fp + vp);
                    vp += 4;
                    if (vp + nLen + 4 > flen) break;
                    std::string key(reinterpret_cast<const char*>(fp + vp), nLen);
                    vp += nLen;
                    const uint32_t vLen = DetailBinarySysDoc::RdU32LE(fp + vp);
                    vp += 4;
                    if (vp + vLen > flen) break;
                    const uint8_t* vpData = fp + vp;

                    if (key == "$UUID" && vLen == 16) {
                        std::wstring kdfAlg = DetailBinarySysDoc::HexBytes(vpData, 16);
                        if (std::memcmp(vpData, kKdfArgon2d, 16) == 0) kdfAlg = L"Argon2d";
                        else if (std::memcmp(vpData, kKdfArgon2id, 16) == 0) kdfAlg = L"Argon2id";
                        else if (std::memcmp(vpData, kKdfAes, 16) == 0) kdfAlg = L"AES-KDF";
                        rows.push_back({ L"KDF Algorithm", kdfAlg });
                    } else if (key == "M" && vLen == 8) {
                        const uint64_t memBytes = DetailBinarySysDoc::RdU64LE(vpData);
                        rows.push_back({ L"KDF Memory (M)", std::to_wstring(memBytes / 1024ULL) + L" KiB (" + PathUtils::FormatByteSize(memBytes) + L")" });
                    } else if ((key == "I" || key == "R") && vLen == 8) {
                        rows.push_back({ L"KDF Iterations (" + PathUtils::Utf8ToWide(key) + L")", std::to_wstring(DetailBinarySysDoc::RdU64LE(vpData)) });
                    } else if (key == "P" && vLen == 4) {
                        rows.push_back({ L"KDF Parallelism (P)", std::to_wstring(DetailBinarySysDoc::RdU32LE(vpData)) + L" lanes" });
                    } else if (key == "V" && vLen == 4) {
                        rows.push_back({ L"Argon2 Version", DetailBinarySysDoc::Hex32(DetailBinarySysDoc::RdU32LE(vpData)) });
                    } else if (key == "S" && vLen > 0) {
                        rows.push_back({ L"KDF Salt (" + std::to_wstring(vLen) + L" B)", DetailBinarySysDoc::HexBytes(vpData, vLen) });
                    } else if (vtype == 0x18) {
                        rows.push_back({ L"KDF[" + PathUtils::Utf8ToWide(key) + L"]", PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(vpData), vLen)) });
                    }
                    vp += vLen;
                }
            }
        }
        pos += flen;
    }

    if (!sawEndOfHeader) return false;
    rows.push_back({ L"Outer Header End Offset", std::to_wstring(pos) + L" B" });
    rows.push_back({ L"Encrypted Vault Payload", PathUtils::FormatByteSize(raw.size() - pos) });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"KeePass Password Database (.kdbx)";
    SpreadsheetSheet sh;
    sh.name = L"KDBX_Header";
    sh.maxCols = 2;
    sh.rows = std::move(rows);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}


} // namespace Formats
