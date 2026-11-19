#pragma once
// Pkcs7Bundle.h — PKCS#7 / CMS Certificate Bundle (.p7b / .p7c) Reader

namespace Formats {

inline bool ParsePkcs7BundleDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::vector<DetailX509::DerBlock> blocks;
    bool isPem = false;
    if (!DetailX509::ExtractPemOrDerBlocks(raw, blocks, isPem)) return false;

    auto rows = DetailX509::InitPkiHeaderRows(isPem, blocks.size(), extLower);
    bool parsedAny = false;
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto& blk = blocks[bi];
        const std::wstring prefix = (blocks.size() > 1) ? (L"[#" + std::to_wstring(bi + 1) + L"] ") : L"";
        rows.push_back({ prefix + L"Block Type", blk.label + L" (" + PathUtils::FormatByteSize(blk.der.size()) + L")" });

        bool handledAsCert = false;
#ifdef _WIN32
        {
            CRYPT_DATA_BLOB blob = { static_cast<DWORD>(blk.der.size()), const_cast<BYTE*>(blk.der.data()) };
            HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_PKCS7, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, &blob);
            if (hStore) {
                PCCERT_CONTEXT pCur = nullptr;
                int cIdx = 1;
                while ((pCur = CertEnumCertificatesInStore(hStore, pCur)) != nullptr && cIdx <= 16) {
                    DetailX509::AppendCertContextRows(pCur, L"[Cert #" + std::to_wstring(cIdx++) + L"] ", rows);
                    handledAsCert = true;
                    parsedAny = true;
                }
                CertCloseStore(hStore, 0);
            }
        }
#endif
        if (!handledAsCert) {
            uint8_t t = 0; size_t hl = 0, vl = 0;
            if (blk.der[0] == 0x30 && DetailX509::ReadDerTlv(blk.der.data(), blk.der.size(), t, hl, vl)) {
                std::wstring lastOid;
                DetailX509::WalkAsn1Der(blk.der.data(), blk.der.size(), 0, lastOid, rows);
                parsedAny = true;
            }
        }
    }

    if (!parsedAny) return false;
    return DetailX509::FinalizePkiDoc(L"PKCS#7 Certificate Bundle (" + extLower + L")", std::move(rows), out);
}

} // namespace Formats
