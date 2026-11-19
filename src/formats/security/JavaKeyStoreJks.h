#pragma once
// JavaKeyStoreJks.h — Java KeyStore & JCEKS Binary Vault (.jks)

namespace Formats {

inline bool ParseJavaKeyStoreJksDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #90. Java KeyStore Binary Vault (.jks)
    if (ext == L".jks" || (raw.size() >= 12 && (DetailDocs200::RdU32BE(raw.data()) == 0xFEEDFEEDu || DetailDocs200::RdU32BE(raw.data()) == 0xCECECECEu))) {
        if (raw.size() < 12) return false;
        uint32_t magic = DetailDocs200::RdU32BE(raw.data());
        if (magic != 0xFEEDFEEDu && magic != 0xCECECECEu) return false;
        uint32_t ver   = DetailDocs200::RdU32BE(raw.data() + 4);
        uint32_t count = DetailDocs200::RdU32BE(raw.data() + 8);

        SpreadsheetSheet sh;
        sh.name = L"KeyStore_Entries";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Alias", L"Entry Type", L"Created (Epoch ms)", L"Certificate / Key Info" });

        size_t pos = 12;
        for (uint32_t i = 0; i < count && i < 256 && pos + 6 <= raw.size(); ++i) {
            uint32_t tag = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
            if (pos + 2 > raw.size()) break;
            uint16_t aLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
            if (pos + aLen + 8 > raw.size()) break;
            std::wstring alias = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), aLen));
            pos += aLen;
            uint64_t ts = DetailDocs200::RdU64BE(raw.data() + pos); pos += 8;

            if (tag == 1) { // PrivateKey
                if (pos + 4 > raw.size()) break;
                uint32_t kLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                if (kLen > raw.size() - pos) break;
                pos += kLen;
                uint32_t chainCnt = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32BE(raw.data() + pos) : 0;
                if (pos + 4 <= raw.size()) pos += 4;
                for (uint32_t c = 0; c < chainCnt && pos + 2 <= raw.size(); ++c) {
                    uint16_t ctLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
                    if (pos + ctLen + 4 > raw.size()) { pos = raw.size(); break; }
                    pos += ctLen;
                    uint32_t cbLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                    if (cbLen > raw.size() - pos) { pos = raw.size(); break; }
                    pos += cbLen;
                }
                sh.rows.push_back({ alias, L"PrivateKeyEntry", std::to_wstring(ts),
                    L"PKCS#8 Encrypted Key (" + std::to_wstring(kLen) + L" B), Chain=" + std::to_wstring(chainCnt) });
            } else if (tag == 2) { // TrustedCert
                std::wstring ctype = L"X.509";
                if (ver == 2) {
                    if (pos + 2 > raw.size()) break;
                    uint16_t ctLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
                    if (pos + ctLen > raw.size()) break;
                    ctype = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), ctLen));
                    pos += ctLen;
                }
                if (pos + 4 > raw.size()) break;
                uint32_t cbLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                if (cbLen > raw.size() - pos) break;
                pos += cbLen;
                sh.rows.push_back({ alias, L"TrustedCertificateEntry", std::to_wstring(ts),
                    ctype + L" DER Certificate (" + std::to_wstring(cbLen) + L" B)" });
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

    return false;
}

} // namespace Formats
