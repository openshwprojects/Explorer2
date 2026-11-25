#pragma once
// Pkcs12Keystore.h — RFC 7292 PKCS#12 Personal Information Exchange Keystore (.p12 / .pfx) Structural Reader

namespace Formats {
namespace DetailPkcs12 {

inline uint64_t ReadDerUint(const uint8_t* p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < std::min<size_t>(n, 8); ++i) v = (v << 8) | p[i];
    return v;
}

inline std::wstring DescribePbeAlgorithm(const uint8_t* algSeqVal, size_t algSeqLen) {
    std::string algOid, paramOid;
    const uint8_t* pRaw = nullptr; size_t pRawLen = 0;
    if (!DetailX509::ParseDerAlgorithmIdentifier(algSeqVal, algSeqLen, algOid, paramOid, pRaw, pRawLen)) {
        return L"(unknown algorithm)";
    }
    std::wstring desc = DetailX509::MapKnownOid(algOid);
    if (algOid == "1.2.840.113549.1.5.13" && pRaw && pRawLen > 2) {
        // PBES2-params ::= SEQUENCE { keyDerivationFunc AlgorithmIdentifier, encryptionScheme AlgorithmIdentifier }
        uint8_t pTag = 0; size_t ph = 0, pv = 0;
        if (DetailX509::ReadDerTlv(pRaw, pRawLen, pTag, ph, pv) && pTag == 0x30) {
            const uint8_t* sVal = pRaw + ph;
            uint8_t kdfTag = 0; size_t kh = 0, kv = 0;
            if (DetailX509::ReadDerTlv(sVal, pv, kdfTag, kh, kv) && kdfTag == 0x30) {
                std::string kdfOid, kpOid; const uint8_t* kpRaw = nullptr; size_t kpLen = 0;
                if (DetailX509::ParseDerAlgorithmIdentifier(sVal + kh, kv, kdfOid, kpOid, kpRaw, kpLen)) {
                    desc += L" [KDF=" + DetailX509::MapKnownOid(kdfOid);
                    // PBKDF2-params ::= SEQUENCE { salt OCTET STRING, iterationCount INTEGER, keyLength INTEGER OPT, prf AlgorithmIdentifier OPT }
                    uint8_t kbTag = 0; size_t kbh = 0, kbv = 0;
                    if (kpRaw && DetailX509::ReadDerTlv(kpRaw, kpLen, kbTag, kbh, kbv) && kbTag == 0x30) {
                        size_t q = 0;
                        uint8_t st = 0; size_t sh = 0, sv = 0;
                        if (DetailX509::ReadDerTlv(kpRaw + kbh, kbv, st, sh, sv)) {
                            q += sh + sv;
                            uint8_t itTag = 0; size_t ith = 0, itv = 0;
                            if (q < kbv && DetailX509::ReadDerTlv(kpRaw + kbh + q, kbv - q, itTag, ith, itv) && itTag == 0x02) {
                                desc += L", iter=" + std::to_wstring(ReadDerUint(kpRaw + kbh + q + ith, itv));
                                q += ith + itv;
                                while (q < kbv) {
                                    uint8_t xt = 0; size_t xh = 0, xv = 0;
                                    if (!DetailX509::ReadDerTlv(kpRaw + kbh + q, kbv - q, xt, xh, xv)) break;
                                    if (xt == 0x30) {
                                        std::string prfOid, pp; const uint8_t* pr = nullptr; size_t pl = 0;
                                        if (DetailX509::ParseDerAlgorithmIdentifier(kpRaw + kbh + q + xh, xv, prfOid, pp, pr, pl)) {
                                            desc += L", PRF=" + DetailX509::MapKnownOid(prfOid);
                                        }
                                    }
                                    q += xh + xv;
                                }
                            }
                        }
                    }
                    desc += L"]";
                }
                if (kh + kv < pv) {
                    uint8_t encTag = 0; size_t eh = 0, ev = 0;
                    if (DetailX509::ReadDerTlv(sVal + kh + kv, pv - (kh + kv), encTag, eh, ev) && encTag == 0x30) {
                        std::string encOid, ep; const uint8_t* er = nullptr; size_t el = 0;
                        if (DetailX509::ParseDerAlgorithmIdentifier(sVal + kh + kv + eh, ev, encOid, ep, er, el)) {
                            desc += L" + Cipher=" + DetailX509::MapKnownOid(encOid);
                        }
                    }
                }
            }
        }
    } else if (pRaw && pRawLen > 2) {
        // PKCS#12 PBEParams ::= SEQUENCE { salt OCTET STRING, iterations INTEGER }
        uint8_t pTag = 0; size_t ph = 0, pv = 0;
        if (DetailX509::ReadDerTlv(pRaw, pRawLen, pTag, ph, pv) && pTag == 0x30) {
            uint8_t st = 0; size_t sh = 0, sv = 0;
            if (DetailX509::ReadDerTlv(pRaw + ph, pv, st, sh, sv) && st == 0x04 && sh + sv < pv) {
                uint8_t itTag = 0; size_t ith = 0, itv = 0;
                if (DetailX509::ReadDerTlv(pRaw + ph + sh + sv, pv - (sh + sv), itTag, ith, itv) && itTag == 0x02) {
                    desc += L" (salt=" + std::to_wstring(sv) + L" B, iter=" + std::to_wstring(ReadDerUint(pRaw + ph + sh + sv + ith, itv)) + L")";
                }
            }
        }
    }
    return desc;
}

inline void ParseSafeContentsDer(
    const uint8_t* scVal,
    size_t scLen,
    size_t& bagCounter,
    std::vector<std::vector<std::wstring>>& rows)
{
    size_t pos = 0;
    while (pos < scLen && bagCounter < 128) {
        uint8_t bagTag = 0; size_t bh = 0, bv = 0;
        if (!DetailX509::ReadDerTlv(scVal + pos, scLen - pos, bagTag, bh, bv)) break;
        const uint8_t* bVal = scVal + pos + bh;
        pos += bh + bv;
        if (bagTag != 0x30) continue;

        uint8_t oidTag = 0; size_t oh = 0, ov = 0;
        if (!DetailX509::ReadDerTlv(bVal, bv, oidTag, oh, ov) || oidTag != 0x06 || oh + ov >= bv) continue;
        std::string bagOid = DetailX509::DecodeDerOid(bVal + oh, ov);
        size_t bp = oh + ov;

        uint8_t valCtxTag = 0; size_t vch = 0, vcv = 0;
        if (!DetailX509::ReadDerTlv(bVal + bp, bv - bp, valCtxTag, vch, vcv) || valCtxTag != 0xA0u) continue;
        const uint8_t* bagContent = bVal + bp + vch;
        const size_t bagContentLen = vcv;
        bp += vch + vcv;

        // Optional SET OF PKCS12Attribute (friendlyName 1.2.840.113549.1.9.20, localKeyId 1.2.840.113549.1.9.21)
        std::wstring friendlyName, localKeyId;
        if (bp < bv) {
            uint8_t attrSetTag = 0; size_t ash = 0, asv = 0;
            if (DetailX509::ReadDerTlv(bVal + bp, bv - bp, attrSetTag, ash, asv) && attrSetTag == 0x31) {
                size_t ap = 0;
                const uint8_t* aSet = bVal + bp + ash;
                while (ap < asv) {
                    uint8_t atTag = 0; size_t ath = 0, atv = 0;
                    if (!DetailX509::ReadDerTlv(aSet + ap, asv - ap, atTag, ath, atv)) break;
                    const uint8_t* aSeq = aSet + ap + ath;
                    ap += ath + atv;
                    if (atTag != 0x30) continue;
                    uint8_t aoTag = 0; size_t aoh = 0, aov = 0;
                    if (!DetailX509::ReadDerTlv(aSeq, atv, aoTag, aoh, aov) || aoTag != 0x06 || aoh + aov >= atv) continue;
                    std::string aOid = DetailX509::DecodeDerOid(aSeq + aoh, aov);
                    uint8_t avSetTag = 0; size_t avh = 0, avv = 0;
                    if (!DetailX509::ReadDerTlv(aSeq + aoh + aov, atv - (aoh + aov), avSetTag, avh, avv) || avSetTag != 0x31) continue;
                    uint8_t ivTag = 0; size_t ivh = 0, ivv = 0;
                    if (!DetailX509::ReadDerTlv(aSeq + aoh + aov + avh, avv, ivTag, ivh, ivv)) continue;
                    const uint8_t* ivPtr = aSeq + aoh + aov + avh + ivh;
                    if (aOid == "1.2.840.113549.1.9.20") {
                        friendlyName = DetailX509::DecodeDerAnyString(ivTag, ivPtr, ivv);
                    } else if (aOid == "1.2.840.113549.1.9.21" && ivTag == 0x04) {
                        localKeyId = DetailBinarySysDoc::HexBytes(ivPtr, std::min<size_t>(ivv, 32), L':');
                    }
                }
            }
        }

        if (bagOid == "1.2.840.113549.1.12.10.1.6") { // safeContentsBag (nested SafeContents)
            uint8_t nsTag = 0; size_t nsh = 0, nsv = 0;
            if (DetailX509::ReadDerTlv(bagContent, bagContentLen, nsTag, nsh, nsv) && nsTag == 0x30) {
                ParseSafeContentsDer(bagContent + nsh, nsv, bagCounter, rows);
            }
            continue;
        }

        ++bagCounter;
        const std::wstring pfx = L"SafeBag #" + std::to_wstring(bagCounter) + L" ";
        rows.push_back({ pfx + L"Type", DetailX509::MapKnownOid(bagOid) + L" (" + PathUtils::Utf8ToWide(bagOid) + L")" });
        if (!friendlyName.empty()) rows.push_back({ pfx + L"friendlyName", friendlyName });
        if (!localKeyId.empty())   rows.push_back({ pfx + L"localKeyId", localKeyId });

        if (bagOid == "1.2.840.113549.1.12.10.1.2") {
            // pkcs8ShroudedKeyBag -> EncryptedPrivateKeyInfo ::= SEQUENCE { encryptionAlgorithm AlgorithmIdentifier, encryptedData OCTET STRING }
            uint8_t epkiTag = 0; size_t eh = 0, ev = 0;
            if (DetailX509::ReadDerTlv(bagContent, bagContentLen, epkiTag, eh, ev) && epkiTag == 0x30) {
                uint8_t algTag = 0; size_t ah = 0, av = 0;
                if (DetailX509::ReadDerTlv(bagContent + eh, ev, algTag, ah, av) && algTag == 0x30) {
                    rows.push_back({ pfx + L"Key Encryption", DescribePbeAlgorithm(bagContent + eh + ah, av) });
                    if (ah + av < ev) {
                        uint8_t edTag = 0; size_t edh = 0, edv = 0;
                        if (DetailX509::ReadDerTlv(bagContent + eh + ah + av, ev - (ah + av), edTag, edh, edv) && edTag == 0x04) {
                            rows.push_back({ pfx + L"Encrypted Key Size", std::to_wstring(edv) + L" B" });
                        }
                    }
                }
            }
        } else if (bagOid == "1.2.840.113549.1.12.10.1.1") {
            // keyBag -> PrivateKeyInfo ::= SEQUENCE { version INTEGER, privateKeyAlgorithm AlgorithmIdentifier, privateKey OCTET STRING }
            uint8_t pkiTag = 0; size_t ph = 0, pv = 0;
            if (DetailX509::ReadDerTlv(bagContent, bagContentLen, pkiTag, ph, pv) && pkiTag == 0x30) {
                uint8_t vTag = 0; size_t vh = 0, vv = 0;
                if (DetailX509::ReadDerTlv(bagContent + ph, pv, vTag, vh, vv) && vTag == 0x02 && vh + vv < pv) {
                    uint8_t algTag = 0; size_t ah = 0, av = 0;
                    if (DetailX509::ReadDerTlv(bagContent + ph + vh + vv, pv - (vh + vv), algTag, ah, av) && algTag == 0x30) {
                        std::string kOid, kpOid; const uint8_t* kr = nullptr; size_t kl = 0;
                        if (DetailX509::ParseDerAlgorithmIdentifier(bagContent + ph + vh + vv + ah, av, kOid, kpOid, kr, kl)) {
                            rows.push_back({ pfx + L"Key Algorithm", DetailX509::MapKnownOid(kOid) });
                        }
                    }
                }
            }
        } else if (bagOid == "1.2.840.113549.1.12.10.1.3") {
            // certBag -> CertBag ::= SEQUENCE { certId OID, certValue [0] EXPLICIT OCTET STRING }
            uint8_t cbTag = 0; size_t cbh = 0, cbv = 0;
            if (DetailX509::ReadDerTlv(bagContent, bagContentLen, cbTag, cbh, cbv) && cbTag == 0x30) {
                uint8_t coTag = 0; size_t coh = 0, cov = 0;
                if (DetailX509::ReadDerTlv(bagContent + cbh, cbv, coTag, coh, cov) && coTag == 0x06 && coh + cov < cbv) {
                    std::string certIdOid = DetailX509::DecodeDerOid(bagContent + cbh + coh, cov);
                    uint8_t cxTag = 0; size_t cxh = 0, cxv = 0;
                    if (certIdOid == "1.2.840.113549.1.9.22.1" &&
                        DetailX509::ReadDerTlv(bagContent + cbh + coh + cov, cbv - (coh + cov), cxTag, cxh, cxv) && cxTag == 0xA0u)
                    {
                        uint8_t octTag = 0; size_t och = 0, ocv = 0;
                        if (DetailX509::ReadDerTlv(bagContent + cbh + coh + cov + cxh, cxv, octTag, och, ocv) && octTag == 0x04) {
                            DetailX509::AppendPortableX509Rows(bagContent + cbh + coh + cov + cxh + och, ocv, pfx, rows);
                        }
                    }
                }
            }
        }
    }
}

inline bool UnwrapOctetStringPayload(const uint8_t* p, size_t len, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    uint8_t tag = 0; size_t h = 0, v = 0;
    if (!DetailX509::ReadDerTlv(p, len, tag, h, v)) return false;
    if (tag == 0x04u) {
        outBytes.assign(p + h, p + h + v);
        return true;
    }
    if (tag == 0x24u) { // Constructed BER OCTET STRING
        size_t q = 0;
        while (q < v) {
            uint8_t ct = 0; size_t ch = 0, cv = 0;
            if (!DetailX509::ReadDerTlv(p + h + q, v - q, ct, ch, cv)) break;
            if (ct == 0x04u) outBytes.insert(outBytes.end(), p + h + q + ch, p + h + q + ch + cv);
            q += ch + cv;
        }
        return !outBytes.empty();
    }
    return false;
}

inline bool ParseSinglePfxDer(const uint8_t* der, size_t len, const std::wstring& prefix, std::vector<std::vector<std::wstring>>& rows) {
    // PFX ::= SEQUENCE { version INTEGER {v3(3)}, authSafe ContentInfo, macData MacData OPTIONAL }
    uint8_t outerTag = 0; size_t oh = 0, ov = 0;
    if (!DetailX509::ReadDerTlv(der, len, outerTag, oh, ov) || outerTag != 0x30) return false;
    const uint8_t* pfx = der + oh;
    size_t pos = 0;

    // 1. version INTEGER (must be 3 per RFC 7292)
    uint8_t vTag = 0; size_t vh = 0, vv = 0;
    if (!DetailX509::ReadDerTlv(pfx, ov, vTag, vh, vv) || vTag != 0x02 || vv != 1 || pfx[vh] != 3u) {
        return false;
    }
    pos += vh + vv;

    // 2. authSafe ContentInfo ::= SEQUENCE { contentType OID, content [0] EXPLICIT ANY OPTIONAL }
    uint8_t ciTag = 0; size_t cih = 0, civ = 0;
    if (pos >= ov || !DetailX509::ReadDerTlv(pfx + pos, ov - pos, ciTag, cih, civ) || ciTag != 0x30) return false;
    const uint8_t* ciVal = pfx + pos + cih;
    pos += cih + civ;

    uint8_t ctTag = 0; size_t cth = 0, ctv = 0;
    if (!DetailX509::ReadDerTlv(ciVal, civ, ctTag, cth, ctv) || ctTag != 0x06) return false;
    std::string authSafeOid = DetailX509::DecodeDerOid(ciVal + cth, ctv);
    if (authSafeOid != "1.2.840.113549.1.7.1" && authSafeOid != "1.2.840.113549.1.7.2") return false;

    rows.push_back({ prefix + L"PFX Version", L"v3 (RFC 7292)" });
    rows.push_back({ prefix + L"AuthSafe Mode", authSafeOid == "1.2.840.113549.1.7.1" ? L"Password-Integrity / Unauthenticated (pkcs7-data)" : L"Public-Key Signed (pkcs7-signedData)" });

    // Unwrap authSafe -> AuthenticatedSafe ::= SEQUENCE OF ContentInfo
    size_t bagCounter = 0;
    if (authSafeOid == "1.2.840.113549.1.7.1" && cth + ctv < civ) {
        uint8_t ctx0Tag = 0; size_t x0h = 0, x0v = 0;
        std::vector<uint8_t> authSafeBytes;
        if (DetailX509::ReadDerTlv(ciVal + cth + ctv, civ - (cth + ctv), ctx0Tag, x0h, x0v) && ctx0Tag == 0xA0u &&
            UnwrapOctetStringPayload(ciVal + cth + ctv + x0h, x0v, authSafeBytes))
        {
            uint8_t asSeqTag = 0; size_t ash = 0, asv = 0;
            if (DetailX509::ReadDerTlv(authSafeBytes.data(), authSafeBytes.size(), asSeqTag, ash, asv) && asSeqTag == 0x30) {
                size_t sp = 0;
                size_t safeIdx = 0;
                const uint8_t* asVal = authSafeBytes.data() + ash;
                while (sp < asv) {
                    uint8_t sciTag = 0; size_t scih = 0, sciv = 0;
                    if (!DetailX509::ReadDerTlv(asVal + sp, asv - sp, sciTag, scih, sciv)) break;
                    const uint8_t* sciVal = asVal + sp + scih;
                    sp += scih + sciv;
                    if (sciTag != 0x30) continue;
                    ++safeIdx;

                    uint8_t soTag = 0; size_t soh = 0, sov = 0;
                    if (!DetailX509::ReadDerTlv(sciVal, sciv, soTag, soh, sov) || soTag != 0x06) continue;
                    std::string safeOid = DetailX509::DecodeDerOid(sciVal + soh, sov);

                    if (safeOid == "1.2.840.113549.1.7.1" && soh + sov < sciv) {
                        // Unencrypted SafeContents
                        uint8_t s0Tag = 0; size_t s0h = 0, s0v = 0;
                        std::vector<uint8_t> scBytes;
                        if (DetailX509::ReadDerTlv(sciVal + soh + sov, sciv - (soh + sov), s0Tag, s0h, s0v) && s0Tag == 0xA0u &&
                            UnwrapOctetStringPayload(sciVal + soh + sov + s0h, s0v, scBytes))
                        {
                            uint8_t scSeqTag = 0; size_t sch = 0, scv = 0;
                            if (DetailX509::ReadDerTlv(scBytes.data(), scBytes.size(), scSeqTag, sch, scv) && scSeqTag == 0x30) {
                                rows.push_back({ prefix + L"SafeContents #" + std::to_wstring(safeIdx), L"Unencrypted SafeContents (" + std::to_wstring(scBytes.size()) + L" B)" });
                                ParseSafeContentsDer(scBytes.data() + sch, scv, bagCounter, rows);
                            }
                        }
                    } else if (safeOid == "1.2.840.113549.1.7.6" && soh + sov < sciv) {
                        // EncryptedData ::= SEQUENCE { version INTEGER, encryptedContentInfo EncryptedContentInfo }
                        uint8_t s0Tag = 0; size_t s0h = 0, s0v = 0;
                        if (DetailX509::ReadDerTlv(sciVal + soh + sov, sciv - (soh + sov), s0Tag, s0h, s0v) && s0Tag == 0xA0u) {
                            uint8_t edTag = 0; size_t edh = 0, edv = 0;
                            if (DetailX509::ReadDerTlv(sciVal + soh + sov + s0h, s0v, edTag, edh, edv) && edTag == 0x30) {
                                const uint8_t* edVal = sciVal + soh + sov + s0h + edh;
                                uint8_t evTag = 0; size_t evh = 0, evv = 0;
                                if (DetailX509::ReadDerTlv(edVal, edv, evTag, evh, evv) && evTag == 0x02 && evh + evv < edv) {
                                    uint8_t eciTag = 0; size_t ecih = 0, eciv = 0;
                                    if (DetailX509::ReadDerTlv(edVal + evh + evv, edv - (evh + evv), eciTag, ecih, eciv) && eciTag == 0x30) {
                                        const uint8_t* eciVal = edVal + evh + evv + ecih;
                                        uint8_t ct2Tag = 0; size_t ct2h = 0, ct2v = 0;
                                        if (DetailX509::ReadDerTlv(eciVal, eciv, ct2Tag, ct2h, ct2v) && ct2Tag == 0x06 && ct2h + ct2v < eciv) {
                                            uint8_t ceaTag = 0; size_t ceah = 0, ceav = 0;
                                            if (DetailX509::ReadDerTlv(eciVal + ct2h + ct2v, eciv - (ct2h + ct2v), ceaTag, ceah, ceav) && ceaTag == 0x30) {
                                                std::wstring algDesc = DescribePbeAlgorithm(eciVal + ct2h + ct2v + ceah, ceav);
                                                size_t remOff = ct2h + ct2v + ceah + ceav;
                                                size_t encPayloadBytes = (remOff < eciv) ? (eciv - remOff) : 0;
                                                rows.push_back({
                                                    prefix + L"SafeContents #" + std::to_wstring(safeIdx),
                                                    L"EncryptedData: " + algDesc + L" (" + std::to_wstring(encPayloadBytes) + L" B)"
                                                });
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Optional MacData ::= SEQUENCE { mac DigestInfo, macSalt OCTET STRING, iterations INTEGER DEFAULT 1 }
    if (pos < ov) {
        uint8_t mdTag = 0; size_t mdh = 0, mdv = 0;
        if (DetailX509::ReadDerTlv(pfx + pos, ov - pos, mdTag, mdh, mdv) && mdTag == 0x30) {
            const uint8_t* mdVal = pfx + pos + mdh;
            size_t mp = 0;
            std::wstring macHashAlg = L"Unknown";
            size_t macDigestLen = 0, saltLen = 0;
            uint64_t iterations = 1;

            uint8_t diTag = 0; size_t dih = 0, div = 0;
            if (DetailX509::ReadDerTlv(mdVal, mdv, diTag, dih, div) && diTag == 0x30) {
                mp += dih + div;
                uint8_t daTag = 0; size_t dah = 0, dav = 0;
                if (DetailX509::ReadDerTlv(mdVal + dih, div, daTag, dah, dav) && daTag == 0x30) {
                    std::string hOid, hpOid; const uint8_t* hr = nullptr; size_t hl = 0;
                    if (DetailX509::ParseDerAlgorithmIdentifier(mdVal + dih + dah, dav, hOid, hpOid, hr, hl)) {
                        macHashAlg = DetailX509::MapKnownOid(hOid);
                    }
                    if (dah + dav < div) {
                        uint8_t dgTag = 0; size_t dgh = 0, dgv = 0;
                        if (DetailX509::ReadDerTlv(mdVal + dih + dah + dav, div - (dah + dav), dgTag, dgh, dgv) && dgTag == 0x04) {
                            macDigestLen = dgv;
                        }
                    }
                }
            }
            if (mp < mdv) {
                uint8_t msTag = 0; size_t msh = 0, msv = 0;
                if (DetailX509::ReadDerTlv(mdVal + mp, mdv - mp, msTag, msh, msv) && msTag == 0x04) {
                    saltLen = msv;
                    mp += msh + msv;
                }
            }
            if (mp < mdv) {
                uint8_t itTag = 0; size_t ith = 0, itv = 0;
                if (DetailX509::ReadDerTlv(mdVal + mp, mdv - mp, itTag, ith, itv) && itTag == 0x02) {
                    iterations = ReadDerUint(mdVal + mp + ith, itv);
                }
            }
            rows.push_back({
                prefix + L"MAC Integrity (MacData)",
                L"HMAC-" + macHashAlg + L" (" + std::to_wstring(macDigestLen * 8u) + L"-bit), Salt=" +
                std::to_wstring(saltLen) + L" B, Iterations=" + std::to_wstring(iterations)
            });
        }
    }

    return true;
}

} // namespace DetailPkcs12

inline bool ParsePkcs12KeystoreDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::vector<DetailX509::DerBlock> blocks;
    bool isPem = false;
    if (!DetailX509::ExtractPemOrDerBlocks(raw, blocks, isPem)) return false;

    auto rows = DetailX509::InitPkiHeaderRows(isPem, blocks.size(), extLower);
    bool parsedAny = false;
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto& blk = blocks[bi];
        const std::wstring prefix = (blocks.size() > 1) ? (L"[#" + std::to_wstring(bi + 1) + L"] ") : L"";
        if (!isPem) {
            if (DetailPkcs12::ParseSinglePfxDer(blk.der.data(), blk.der.size(), prefix, rows)) {
                parsedAny = true;
            }
        } else {
            if (DetailPkcs12::ParseSinglePfxDer(blk.der.data(), blk.der.size(), prefix, rows)) {
                parsedAny = true;
            } else if (blk.label.find(L"CERTIFICATE") != std::wstring::npos) {
                DetailX509::AppendPortableX509Rows(blk.der.data(), blk.der.size(), prefix, rows);
                parsedAny = true;
            }
        }
    }
    if (!parsedAny) return false;
    return DetailX509::FinalizePkiDoc(L"PKCS#12 Personal Information Exchange (" + extLower + L")", std::move(rows), out);
}

} // namespace Formats
