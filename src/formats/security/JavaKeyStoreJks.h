#pragma once
// JavaKeyStoreJks.h — Java KeyStore (JKS 0xFEEDFEED) & JCEKS (0xCECECECE) Binary Vault (.jks / .jceks) Reader

namespace Formats {

inline bool ParseJavaKeyStoreJksDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if (raw.size() < 12) return false;
    const uint32_t magic = DetailStructuredDataDoc::RdU32BE(raw.data());
    if (magic != 0xFEEDFEEDu && magic != 0xCECECECEu) return false;
    if (ext != L".jks" && ext != L".jceks" && ext != L".keystore") {
        // Magic matched via content-based inspection
    }

    const uint32_t ver   = DetailStructuredDataDoc::RdU32BE(raw.data() + 4);
    const uint32_t count = DetailStructuredDataDoc::RdU32BE(raw.data() + 8);
    if ((ver != 1 && ver != 2) || count > 65536u) return false;

    auto fmtEpochMs = [](uint64_t epochMs) -> std::wstring {
        if (epochMs == 0) return L"1970-01-01 00:00:00 UTC";
        return GeoSciDetail::FormatUtcEpoch(static_cast<int64_t>(epochMs / 1000ULL));
    };

    auto summarizeDerCert = [](const uint8_t* der, size_t len) -> std::wstring {
        DetailX509::ParsedX509CertSummary s;
        if (!DetailX509::ParseDerX509CertificateSummary(der, len, s)) {
            return L"DER Certificate (" + std::to_wstring(len) + L" B)";
        }
        std::wstring info;
        if (!s.subjectDn.empty()) info += L"Subject: " + s.subjectDn;
        if (!s.issuerDn.empty())  { if (!info.empty()) info += L" | "; info += L"Issuer: " + s.issuerDn; }
        if (!s.serialHex.empty()) { if (!info.empty()) info += L" | "; info += L"Serial: " + s.serialHex; }
        if (!s.notBefore.empty() || !s.notAfter.empty()) {
            if (!info.empty()) info += L" | ";
            info += L"Valid: " + s.notBefore + L" .. " + s.notAfter;
        }
        if (!s.pubKeySummary.empty()) info += L" [" + s.pubKeySummary + L"]";
        return info;
    };

    // Bounded Java ObjectOutputStream (0xACED 0x0005) skipper/inspector for JCEKS SecretKeyEntry (tag == 3)
    auto skipJceksSealedObject = [&](size_t& p, std::wstring& outClassDesc) -> bool {
        outClassDesc.clear();
        if (p + 4 > raw.size() || DetailStructuredDataDoc::RdU16BE(raw.data() + p) != 0xACEDu ||
            DetailStructuredDataDoc::RdU16BE(raw.data() + p + 2) != 0x0005u) {
            return false;
        }
        // Scan within Java ObjectSerialization stream up to either next valid JKS/JCEKS entry header [tag 1..3][u16 aLen][printable alias]
        // or 20-byte SHA-1 trailer at EOF, extracting top-level TC_CLASSDESC (0x72) class name along the way!
        const size_t streamStart = p;
        if (p + 7 < raw.size() && raw[p + 4] == 0x73u && raw[p + 5] == 0x72u) { // TC_OBJECT + TC_CLASSDESC
            uint16_t clLen = DetailStructuredDataDoc::RdU16BE(raw.data() + p + 6);
            if (p + 8u + clLen <= raw.size()) {
                outClassDesc = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + p + 8), clLen));
            }
        }
        const size_t trailerStart = (raw.size() >= 20) ? (raw.size() - 20u) : raw.size();
        for (size_t cand = streamStart + 16u; cand + 14u <= trailerStart; ++cand) {
            uint32_t nextTag = DetailStructuredDataDoc::RdU32BE(raw.data() + cand);
            if (nextTag >= 1u && nextTag <= 3u) {
                uint16_t nl = DetailStructuredDataDoc::RdU16BE(raw.data() + cand + 4);
                if (nl > 0 && nl <= 128u && cand + 6u + nl + 8u <= trailerStart) {
                    bool asciiOk = true;
                    for (uint16_t k = 0; k < nl; ++k) {
                        uint8_t ch = raw[cand + 6u + k];
                        if (ch < 0x20u || ch > 0x7Eu) { asciiOk = false; break; }
                    }
                    uint64_t nextTs = DetailStructuredDataDoc::RdU64BE(raw.data() + cand + 6u + nl);
                    if (asciiOk && nextTs >= 631152000000ULL && nextTs <= 4102444800000ULL) {
                        p = cand;
                        return true;
                    }
                }
            }
        }
        p = trailerStart;
        return true;
    };

    SpreadsheetSheet sh;
    sh.name = L"KeyStore_Entries";
    sh.maxCols = 4;
    sh.rows.push_back({ L"Alias", L"Entry Type", L"Created (UTC)", L"Certificate / Key Info" });

    size_t pos = 12;
    for (uint32_t i = 0; i < count && i < 512 && pos + 6 <= raw.size(); ++i) {
        const uint32_t tag = DetailStructuredDataDoc::RdU32BE(raw.data() + pos); pos += 4;
        if (pos + 2 > raw.size()) break;
        const uint16_t aLen = DetailStructuredDataDoc::RdU16BE(raw.data() + pos); pos += 2;
        if (pos + aLen + 8 > raw.size()) break;
        std::wstring alias = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), aLen));
        pos += aLen;
        const uint64_t ts = DetailStructuredDataDoc::RdU64BE(raw.data() + pos); pos += 8;
        const std::wstring tsStr = fmtEpochMs(ts);

        if (tag == 1) { // PrivateKeyEntry
            if (pos + 4 > raw.size()) break;
            const uint32_t kLen = DetailStructuredDataDoc::RdU32BE(raw.data() + pos); pos += 4;
            if (kLen > raw.size() - pos) break;
            const uint8_t* keyDer = raw.data() + pos;
            pos += kLen;

            std::wstring keyAlgDesc = L"PKCS#8 Encrypted Key (" + std::to_wstring(kLen) + L" B)";
            uint8_t epkiTag = 0; size_t eh = 0, ev = 0;
            if (kLen > 4 && DetailX509::ReadDerTlv(keyDer, kLen, epkiTag, eh, ev) && epkiTag == 0x30) {
                uint8_t algTag = 0; size_t ah = 0, av = 0;
                if (DetailX509::ReadDerTlv(keyDer + eh, ev, algTag, ah, av) && algTag == 0x30) {
                    std::string kOid, kp; const uint8_t* kr = nullptr; size_t kl = 0;
                    if (DetailX509::ParseDerAlgorithmIdentifier(keyDer + eh + ah, av, kOid, kp, kr, kl)) {
                        keyAlgDesc = L"EncryptedPrivateKeyInfo [" + DetailX509::MapKnownOid(kOid) + L", " + std::to_wstring(kLen) + L" B]";
                    }
                }
            }

            if (pos + 4 > raw.size()) break;
            const uint32_t chainCnt = DetailStructuredDataDoc::RdU32BE(raw.data() + pos); pos += 4;
            std::wstring leafCertInfo;
            for (uint32_t c = 0; c < chainCnt && pos < raw.size(); ++c) {
                std::wstring ctype = L"X.509";
                if (ver == 2) {
                    if (pos + 2 > raw.size()) { pos = raw.size(); break; }
                    uint16_t ctLen = DetailStructuredDataDoc::RdU16BE(raw.data() + pos); pos += 2;
                    if (pos + ctLen > raw.size()) { pos = raw.size(); break; }
                    ctype = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), ctLen));
                    pos += ctLen;
                }
                if (pos + 4 > raw.size()) { pos = raw.size(); break; }
                uint32_t cbLen = DetailStructuredDataDoc::RdU32BE(raw.data() + pos); pos += 4;
                if (cbLen > raw.size() - pos) { pos = raw.size(); break; }
                std::wstring cSummary = summarizeDerCert(raw.data() + pos, cbLen);
                pos += cbLen;
                if (c == 0) {
                    leafCertInfo = cSummary;
                } else if (sh.rows.size() < 1024) {
                    sh.rows.push_back({ alias + L" [Chain #" + std::to_wstring(c + 1) + L"]", ctype + L" Chain Cert", tsStr, cSummary });
                }
            }
            std::wstring combined = keyAlgDesc + L", Chain=" + std::to_wstring(chainCnt);
            if (!leafCertInfo.empty()) combined += L" | " + leafCertInfo;
            sh.rows.insert(sh.rows.end() - std::min<size_t>(chainCnt > 1 ? chainCnt - 1 : 0, sh.rows.size() - 1),
                { alias, L"PrivateKeyEntry", tsStr, std::move(combined) });
        } else if (tag == 2) { // TrustedCertificateEntry
            std::wstring ctype = L"X.509";
            if (ver == 2) {
                if (pos + 2 > raw.size()) break;
                uint16_t ctLen = DetailStructuredDataDoc::RdU16BE(raw.data() + pos); pos += 2;
                if (pos + ctLen > raw.size()) break;
                ctype = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), ctLen));
                pos += ctLen;
            }
            if (pos + 4 > raw.size()) break;
            const uint32_t cbLen = DetailStructuredDataDoc::RdU32BE(raw.data() + pos); pos += 4;
            if (cbLen > raw.size() - pos) break;
            std::wstring certSummary = summarizeDerCert(raw.data() + pos, cbLen);
            pos += cbLen;
            sh.rows.push_back({ alias, L"TrustedCertificateEntry (" + ctype + L")", tsStr, std::move(certSummary) });
        } else if (tag == 3) { // JCEKS SecretKeyEntry (Java serialized SealedObject)
            std::wstring clsName;
            size_t before = pos;
            if (!skipJceksSealedObject(pos, clsName)) break;
            sh.rows.push_back({
                alias,
                L"SecretKeyEntry (JCEKS)",
                tsStr,
                (clsName.empty() ? L"Sealed Symmetric Key" : clsName) + L" (" + std::to_wstring(pos - before) + L" B)"
            });
        } else {
            break;
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = (magic == 0xCECECECEu) ? L"Java JCEKS Cryptographic KeyStore (.jks)" : L"Java KeyStore Binary Vault (.jks)";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

} // namespace Formats
