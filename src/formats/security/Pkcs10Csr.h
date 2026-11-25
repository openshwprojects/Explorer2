#pragma once
// Pkcs10Csr.h — RFC 2986 PKCS#10 Certificate Signing Request (.csr) Structural Reader

namespace Formats {
namespace DetailPkcs10 {

inline bool ParseSingleCsrDer(const uint8_t* der, size_t len, const std::wstring& prefix, std::vector<std::vector<std::wstring>>& rows) {
    // CertificationRequest ::= SEQUENCE {
    //     certificationRequestInfo CertificationRequestInfo,
    //     signatureAlgorithm       AlgorithmIdentifier,
    //     signature                BIT STRING
    // }
    uint8_t outerTag = 0; size_t oh = 0, ov = 0;
    if (!DetailX509::ReadDerTlv(der, len, outerTag, oh, ov) || outerTag != 0x30) return false;
    const uint8_t* csr = der + oh;
    size_t pos = 0;

    // 1. CertificationRequestInfo ::= SEQUENCE { version INTEGER, subject Name, subjectPKInfo SubjectPublicKeyInfo, attributes [0] IMPLICIT Attributes }
    uint8_t criTag = 0; size_t crih = 0, criv = 0;
    if (!DetailX509::ReadDerTlv(csr, ov, criTag, crih, criv) || criTag != 0x30) return false;
    const uint8_t* cri = csr + crih;
    pos += crih + criv;

    size_t cp = 0;
    uint8_t verTag = 0; size_t vh = 0, vv = 0;
    if (!DetailX509::ReadDerTlv(cri, criv, verTag, vh, vv) || verTag != 0x02 || vv != 1 || cri[vh] > 1u) {
        return false; // RFC 2986 version is v1 (0)
    }
    const uint32_t csrVer = static_cast<uint32_t>(cri[vh]) + 1u;
    cp += vh + vv;

    // Subject Name (SEQUENCE)
    uint8_t subTag = 0; size_t subh = 0, subv = 0;
    if (cp >= criv || !DetailX509::ReadDerTlv(cri + cp, criv - cp, subTag, subh, subv) || subTag != 0x30) return false;
    std::wstring subjectDn = DetailX509::ParseDerDistinguishedName(cri + cp + subh, subv);
    cp += subh + subv;

    // SubjectPublicKeyInfo (SEQUENCE)
    uint8_t pkTag = 0; size_t pkh = 0, pkv = 0;
    if (cp >= criv || !DetailX509::ReadDerTlv(cri + cp, criv - cp, pkTag, pkh, pkv) || pkTag != 0x30) return false;
    std::wstring pubKeySummary;
    uint32_t pubKeyBits = 0;
    if (!DetailX509::ParseDerSubjectPublicKeyInfo(cri + cp + pkh, pkv, pubKeySummary, pubKeyBits)) return false;
    cp += pkh + pkv;

    // Optional [0] IMPLICIT Attributes (tag 0xA0)
    std::wstring san, keyUsage, basicConstraints;
    if (cp < criv) {
        uint8_t attrTag = 0; size_t ah = 0, av = 0;
        if (DetailX509::ReadDerTlv(cri + cp, criv - cp, attrTag, ah, av) && attrTag == 0xA0u) {
            const uint8_t* aBuf = cri + cp + ah;
            size_t ap = 0;
            while (ap < av) {
                uint8_t atSeq = 0; size_t ath = 0, atv = 0;
                if (!DetailX509::ReadDerTlv(aBuf + ap, av - ap, atSeq, ath, atv)) break;
                const uint8_t* aVal = aBuf + ap + ath;
                ap += ath + atv;
                if (atSeq != 0x30) continue;
                uint8_t oidTag = 0; size_t oih = 0, oiv = 0;
                if (!DetailX509::ReadDerTlv(aVal, atv, oidTag, oih, oiv) || oidTag != 0x06 || oih + oiv >= atv) continue;
                std::string aOid = DetailX509::DecodeDerOid(aVal + oih, oiv);
                if (aOid == "1.2.840.113549.1.9.14") { // extensionRequest -> SET OF Extensions
                    uint8_t setTag = 0; size_t sh = 0, sv = 0;
                    if (DetailX509::ReadDerTlv(aVal + oih + oiv, atv - (oih + oiv), setTag, sh, sv) && setTag == 0x31) {
                        uint8_t extSeqTag = 0; size_t eh = 0, ev = 0;
                        if (DetailX509::ReadDerTlv(aVal + oih + oiv + sh, sv, extSeqTag, eh, ev) && extSeqTag == 0x30) {
                            DetailX509::ParseDerX509Extensions(aVal + oih + oiv + sh + eh, ev, san, keyUsage, basicConstraints);
                        }
                    }
                }
            }
        }
    }

    // 2. Outer signatureAlgorithm AlgorithmIdentifier + 3. signature BIT STRING
    if (pos >= ov) return false;
    uint8_t saTag = 0; size_t sah = 0, sav = 0;
    if (!DetailX509::ReadDerTlv(csr + pos, ov - pos, saTag, sah, sav) || saTag != 0x30) return false;
    std::string sigOid, sigParamOid; const uint8_t* sr = nullptr; size_t sl = 0;
    if (!DetailX509::ParseDerAlgorithmIdentifier(csr + pos + sah, sav, sigOid, sigParamOid, sr, sl)) return false;
    pos += sah + sav;

    if (pos >= ov) return false;
    uint8_t sigBsTag = 0; size_t sbh = 0, sbv = 0;
    if (!DetailX509::ReadDerTlv(csr + pos, ov - pos, sigBsTag, sbh, sbv) || sigBsTag != 0x03 || sbv < 2) return false;
    const size_t sigBits = (sbv - 1u) * 8u;

    rows.push_back({ prefix + L"CSR Version", L"v" + std::to_wstring(csrVer) + L" (RFC 2986)" });
    rows.push_back({ prefix + L"Subject DN", subjectDn.empty() ? L"(Empty Subject)" : subjectDn });
    rows.push_back({ prefix + L"Public Key", pubKeySummary });
    if (!san.empty())              rows.push_back({ prefix + L"Requested SubjectAltNames", san });
    if (!keyUsage.empty())         rows.push_back({ prefix + L"Requested Key Usage", keyUsage });
    if (!basicConstraints.empty()) rows.push_back({ prefix + L"Requested Basic Constraints", basicConstraints });
    rows.push_back({ prefix + L"Signature Algorithm", DetailX509::MapKnownOid(sigOid) + L" (" + PathUtils::Utf8ToWide(sigOid) + L")" });
    rows.push_back({ prefix + L"Signature Length", std::to_wstring(sigBits) + L" bits" });
    return true;
}

} // namespace DetailPkcs10

inline bool ParsePkcs10CsrDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::vector<DetailX509::DerBlock> blocks;
    bool isPem = false;
    if (!DetailX509::ExtractPemOrDerBlocks(raw, blocks, isPem)) return false;

    auto rows = DetailX509::InitPkiHeaderRows(isPem, blocks.size(), extLower);
    bool parsedAny = false;
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto& blk = blocks[bi];
        if (isPem && blk.label.find(L"CERTIFICATE REQUEST") == std::wstring::npos && blk.label.find(L"CSR") == std::wstring::npos) {
            continue;
        }
        const std::wstring prefix = (blocks.size() > 1) ? (L"[#" + std::to_wstring(bi + 1) + L"] ") : L"";
        if (DetailPkcs10::ParseSingleCsrDer(blk.der.data(), blk.der.size(), prefix, rows)) {
            parsedAny = true;
        }
    }
    if (!parsedAny) return false;
    return DetailX509::FinalizePkiDoc(L"PKCS#10 Certificate Signing Request (" + extLower + L")", std::move(rows), out);
}

} // namespace Formats
