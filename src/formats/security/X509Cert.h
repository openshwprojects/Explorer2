#pragma once
// X509Cert.h — X.509 Certificates & PEM/DER Containers (.crt / .cer / .pem / .der) Reader

namespace Formats {

inline bool ParseX509CertDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
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
        const bool looksNonCert = (blk.label.find(L"REQUEST") != std::wstring::npos ||
                                   blk.label.find(L"KEY") != std::wstring::npos);

#ifdef _WIN32
        if (!looksNonCert) {
            PCCERT_CONTEXT pCert = CertCreateCertificateContext(
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                blk.der.data(),
                static_cast<DWORD>(blk.der.size()));
            if (pCert) {
                DetailX509::AppendCertContextRows(pCert, prefix, rows);
                CertFreeCertificateContext(pCert);
                handledAsCert = true;
                parsedAny = true;
            } else if (extLower == L".p7b" || blk.label == L"PKCS7") {
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
        }
#else
        (void)looksNonCert;
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
    return DetailX509::FinalizePkiDoc(L"X.509 Certificate Inspector (" + extLower + L")", std::move(rows), out);
}

} // namespace Formats
