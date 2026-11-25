#pragma once
// AuthenticodeCat.h — Windows Authenticode PKCS#7 Security Catalog (.cat)

namespace Formats {

inline bool ParseAuthenticodeCatDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #91. Windows Authenticode Security Catalog (.cat — Real ASN.1 BER/DER PKCS#7 SignedData + SPC_CATALOG_LIST)
    if (ext == L".cat") {
        if (raw.size() < 16 || raw[0] != 0x30u) return false;

        struct BerTlv {
            uint8_t tag = 0;
            const uint8_t* val = nullptr;
            size_t valLen = 0;
            size_t totalAdv = 0;
        };

        auto readBerTlv = [](auto& self, const uint8_t* p, size_t maxLen, BerTlv& outTlv, int depth) -> bool {
            if (depth > 28 || maxLen < 2) return false;
            size_t pos = 0;
            uint8_t tag = p[pos++];
            if ((tag & 0x1Fu) == 0x1Fu) {
                int c = 0;
                while (pos < maxLen && c < 4) {
                    uint8_t b = p[pos++];
                    ++c;
                    if ((b & 0x80u) == 0) break;
                }
                if (pos >= maxLen) return false;
            }
            uint8_t lb = p[pos++];
            if ((lb & 0x80u) == 0) {
                size_t vl = lb;
                if (vl > maxLen - pos) return false;
                outTlv = { tag, p + pos, vl, pos + vl };
                return true;
            }
            if (lb == 0x80u) { // Indefinite BER length (constructed tags only, terminated by 0x00 0x00 EOC)
                if ((tag & 0x20u) == 0) return false;
                size_t cur = pos;
                while (cur + 2 <= maxLen) {
                    if (p[cur] == 0x00u && p[cur + 1] == 0x00u) {
                        outTlv = { tag, p + pos, cur - pos, cur + 2 };
                        return true;
                    }
                    BerTlv child;
                    if (!self(self, p + cur, maxLen - cur, child, depth + 1) || child.totalAdv == 0) return false;
                    cur += child.totalAdv;
                }
                return false;
            }
            size_t nLenBytes = lb & 0x7Fu;
            if (nLenBytes == 0 || nLenBytes > 4 || nLenBytes > maxLen - pos) return false;
            size_t vl = 0;
            for (size_t i = 0; i < nLenBytes; ++i) vl = (vl << 8) | p[pos + i];
            pos += nLenBytes;
            if (vl > maxLen - pos) return false;
            outTlv = { tag, p + pos, vl, pos + vl };
            return true;
        };

        auto listChildren = [&](const uint8_t* p, size_t len, std::vector<BerTlv>& kids) -> bool {
            kids.clear();
            size_t pos = 0;
            while (pos < len && kids.size() < 20000) {
                if (pos + 2 <= len && p[pos] == 0x00u && p[pos + 1] == 0x00u) break;
                BerTlv t;
                if (!readBerTlv(readBerTlv, p + pos, len - pos, t, 0) || t.totalAdv == 0) return false;
                kids.push_back(t);
                pos += t.totalAdv;
            }
            return true;
        };

        auto decodeBmpOrUtf16 = [](const uint8_t* p, size_t len, bool bigEndian) -> std::wstring {
            std::wstring s;
            s.reserve(len / 2);
            for (size_t i = 0; i + 2 <= len; i += 2) {
                uint16_t u = bigEndian ? DetailStructuredDataDoc::RdU16BE(p + i) : DetailStructuredDataDoc::RdU16LE(p + i);
                if (u == 0) break;
                if (u >= 0xD800u && u <= 0xDBFFu && i + 4 <= len) {
                    uint16_t lo = bigEndian ? DetailStructuredDataDoc::RdU16BE(p + i + 2) : DetailStructuredDataDoc::RdU16LE(p + i + 2);
                    if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                        PathUtils::AppendUtf16(s, 0x10000u + (((static_cast<uint32_t>(u) - 0xD800u) << 10) | (lo - 0xDC00u)));
                        i += 2;
                        continue;
                    }
                }
                PathUtils::AppendUtf16(s, u);
            }
            return s;
        };

        auto decodeAsn1Str = [&](const BerTlv& t) -> std::wstring {
            if (t.tag == 0x1Eu) return decodeBmpOrUtf16(t.val, t.valLen, true);
            if (t.tag == 0x04u) {
                // CAT_NAMEVALUE value & v1 ReferenceTag store UTF-16LE inside OCTET STRING
                if (t.valLen >= 2 && (t.valLen % 2u) == 0) {
                    bool looksUtf16LE = true;
                    size_t nonNullChars = 0;
                    for (size_t i = 0; i + 2 <= t.valLen; i += 2) {
                        uint16_t ch = DetailStructuredDataDoc::RdU16LE(t.val + i);
                        if (ch == 0 && i + 2 == t.valLen) break;
                        if (ch < 0x20u || (ch >= 0x7Fu && ch < 0xA0u) || (ch >= 0xD800u && ch <= 0xDFFFu)) {
                            looksUtf16LE = false;
                            break;
                        }
                        if (t.val[i + 1] != 0 && ch > 0x04FFu) { looksUtf16LE = false; break; }
                        ++nonNullChars;
                    }
                    if (looksUtf16LE && nonNullChars > 0) return decodeBmpOrUtf16(t.val, t.valLen, false);
                }
                if (DetailStructuredDataDoc::IsMostlyPrintableUtf8(t.val, t.valLen)) {
                    return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(t.val), t.valLen));
                }
                return PathUtils::HexBytes(t.val, t.valLen, L':', 64);
            }
            return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(t.val), t.valLen));
        };

        auto mapHashAlgOid = [](const std::string& oid) -> std::wstring {
            if (oid == "1.3.14.3.2.26")            return L"SHA-1";
            if (oid == "2.16.840.1.101.3.4.2.1")   return L"SHA-256";
            if (oid == "2.16.840.1.101.3.4.2.2")   return L"SHA-384";
            if (oid == "2.16.840.1.101.3.4.2.3")   return L"SHA-512";
            if (oid == "1.2.840.113549.2.5")       return L"MD5";
            if (oid == "1.3.6.1.4.1.311.12.1.2")   return L"SPC_CATALOG_LIST_MEMBER (v1)";
            if (oid == "1.3.6.1.4.1.311.12.1.3")   return L"SPC_CATALOG_LIST_MEMBER_V2";
            return DetailX509::MapKnownOid(oid);
        };

        // 1. Parse outer PKCS#7 ContentInfo { OID 1.2.840.113549.1.7.2 (signedData), [0] EXPLICIT SignedData }
        //    (or unwrapped SPC_CATALOG_LIST SEQUENCE { OID 1.3.6.1.4.1.311.12.1.1, ... })
        BerTlv rootSeq;
        if (!readBerTlv(readBerTlv, raw.data(), raw.size(), rootSeq, 0) || rootSeq.tag != 0x30u) return false;
        std::vector<BerTlv> ciKids;
        if (!listChildren(rootSeq.val, rootSeq.valLen, ciKids) || ciKids.size() < 2 || ciKids[0].tag != 0x06u) return false;
        std::string outerOid = DetailX509::DecodeDerOid(ciKids[0].val, ciKids[0].valLen);
        if (outerOid == "1.3.6.1.4.1.311.12.1.1") {
            std::wstring refTag, hashAlg, hashHex;
            for (size_t k = 1; k < ciKids.size(); ++k) {
                if ((ciKids[k].tag == 0x13u || ciKids[k].tag == 0x0Cu || ciKids[k].tag == 0x16u || ciKids[k].tag == 0x1Eu) && refTag.empty()) {
                    refTag = decodeAsn1Str(ciKids[k]);
                } else if (ciKids[k].tag == 0x04u && (ciKids[k].valLen == 20 || ciKids[k].valLen == 32) && hashHex.empty()) {
                    hashAlg = (ciKids[k].valLen == 32) ? L"SHA-256" : L"SHA-1";
                    hashHex = PathUtils::HexBytes(ciKids[k].val, ciKids[k].valLen, 0, 64);
                }
            }
            if (refTag.empty() && hashHex.empty()) return false;
            SpreadsheetSheet memSh;
            memSh.name = L"Catalog_Members";
            memSh.rows.push_back({ L"#", L"Reference Tag", L"File / OS Attributes", L"Subject GUID", L"Hash Algorithm", L"Authenticode Digest" });
            memSh.rows.push_back({ L"1", refTag, L"", L"", hashAlg, hashHex });
            SpreadsheetSheet sumSh;
            sumSh.name = L"Catalog_Summary";
            sumSh.rows.push_back({ L"Property", L"Value" });
            sumSh.rows.push_back({ L"Catalog Content OID", L"1.3.6.1.4.1.311.12.1.1 (SPC_CATALOG_LIST)" });
            sumSh.rows.push_back({ L"Catalog Member Entries", L"1" });
            FinalizeSheetCols(memSh);
            FinalizeSheetCols(sumSh);
            out.kind = OfficeFileKind::SqliteDb;
            out.isSpreadsheet = true;
            out.formatTitle = L"Windows Authenticode Security Catalog (.cat)";
            out.sheets.push_back(std::move(memSh));
            out.sheets.push_back(std::move(sumSh));
            out.valid = true;
            return true;
        }
        if (outerOid != "1.2.840.113549.1.7.2") return false;
        if (ciKids[1].tag != 0xA0u) return false;

        BerTlv signedDataSeq;
        if (!readBerTlv(readBerTlv, ciKids[1].val, ciKids[1].valLen, signedDataSeq, 1) || signedDataSeq.tag != 0x30u) return false;
        std::vector<BerTlv> sdKids;
        if (!listChildren(signedDataSeq.val, signedDataSeq.valLen, sdKids) || sdKids.size() < 3) return false;
        if (sdKids[0].tag != 0x02u || sdKids[2].tag != 0x30u) return false; // version INTEGER, digestAlgs SET, encapContentInfo SEQUENCE

        // 2. Locate encapContentInfo -> CTL / SPC_CATALOG_LIST
        std::vector<BerTlv> eciKids;
        if (!listChildren(sdKids[2].val, sdKids[2].valLen, eciKids) || eciKids.size() < 2 || eciKids[0].tag != 0x06u) return false;
        std::string eContentType = DetailX509::DecodeDerOid(eciKids[0].val, eciKids[0].valLen);
        if (eContentType != "1.3.6.1.4.1.311.10.1" && eContentType != "1.3.6.1.4.1.311.12.1.1") {
            return false;
        }
        if (eciKids[1].tag != 0xA0u) return false;

        // Unwrap [0] EXPLICIT -> either OCTET STRING (0x04 / constructed 0x24) wrapping SEQUENCE, or direct SEQUENCE (0x30)
        std::vector<uint8_t> constructedOctets;
        BerTlv eContentInner;
        if (!readBerTlv(readBerTlv, eciKids[1].val, eciKids[1].valLen, eContentInner, 2)) return false;
        BerTlv ctlSeq = eContentInner;
        if (eContentInner.tag == 0x04u) {
            if (!readBerTlv(readBerTlv, eContentInner.val, eContentInner.valLen, ctlSeq, 3) || ctlSeq.tag != 0x30u) return false;
        } else if (eContentInner.tag == 0x24u) { // BER constructed OCTET STRING
            std::vector<BerTlv> octParts;
            if (!listChildren(eContentInner.val, eContentInner.valLen, octParts)) return false;
            for (const auto& op : octParts) {
                if (op.tag == 0x04u) constructedOctets.insert(constructedOctets.end(), op.val, op.val + op.valLen);
            }
            if (!readBerTlv(readBerTlv, constructedOctets.data(), constructedOctets.size(), ctlSeq, 3) || ctlSeq.tag != 0x30u) return false;
        } else if (ctlSeq.tag != 0x30u) {
            return false;
        }

        // 3. Parse CTL / SPC_CATALOG_LIST header + SEQUENCE OF CatalogListMember
        std::vector<BerTlv> ctlKids;
        if (!listChildren(ctlSeq.val, ctlSeq.valLen, ctlKids) || ctlKids.empty()) return false;

        bool sawSpcCatalogOid = (eContentType == "1.3.6.1.4.1.311.12.1.1");
        std::wstring catVersionStr, listIdStr, effectiveTimeStr, subjAlgStr, catLevelAttrs;

        SpreadsheetSheet memSh;
        memSh.name = L"Catalog_Members";
        memSh.rows.push_back({ L"#", L"Reference Tag", L"File / OS Attributes", L"Subject GUID", L"Hash Algorithm", L"Authenticode Digest" });

        auto parseCatNameValueSeq = [&](const BerTlv& seq, std::wstring& ioAttrs) {
            std::vector<BerTlv> nv;
            if (seq.tag == 0x30u && listChildren(seq.val, seq.valLen, nv) && nv.size() >= 3) {
                std::wstring k = decodeAsn1Str(nv[0]);
                std::wstring v = decodeAsn1Str(nv[2]);
                if (!k.empty()) {
                    if (!ioAttrs.empty()) ioAttrs += L"; ";
                    ioAttrs += k + L"=" + v;
                }
            }
        };

        for (size_t idx = 0; idx < ctlKids.size(); ++idx) {
            const BerTlv& elem = ctlKids[idx];
            if (elem.tag == 0x02u && elem.valLen <= 4) {
                uint32_t v = 0;
                for (size_t k = 0; k < elem.valLen; ++k) v = (v << 8) | elem.val[k];
                if (catVersionStr.empty()) catVersionStr = L"v" + std::to_wstring(v + 1);
            } else if (elem.tag == 0x04u && listIdStr.empty()) {
                listIdStr = PathUtils::HexBytes(elem.val, elem.valLen, L':', 32);
            } else if ((elem.tag == 0x17u || elem.tag == 0x18u) && effectiveTimeStr.empty()) {
                effectiveTimeStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(elem.val), elem.valLen));
            } else if (elem.tag == 0x30u) {
                std::vector<BerTlv> sub;
                if (!listChildren(elem.val, elem.valLen, sub) || sub.empty()) continue;

                if (sub[0].tag == 0x06u) {
                    // Either SubjectUsage (SEQUENCE OF OID) or AlgorithmIdentifier (SEQUENCE { OID, params })
                    bool allOids = true;
                    for (const auto& s : sub) if (s.tag != 0x06u) { allOids = false; break; }
                    std::string firstOid = DetailX509::DecodeDerOid(sub[0].val, sub[0].valLen);
                    if (allOids) {
                        for (const auto& s : sub) {
                            if (DetailX509::DecodeDerOid(s.val, s.valLen) == "1.3.6.1.4.1.311.12.1.1") sawSpcCatalogOid = true;
                        }
                    }
                    if (subjAlgStr.empty() && firstOid != "1.3.6.1.4.1.311.12.1.1") {
                        subjAlgStr = mapHashAlgOid(firstOid);
                    }
                } else if (sub[0].tag == 0x30u) {
                    // SEQUENCE OF CatalogListMember!
                    for (const auto& memTlv : sub) {
                        if (memTlv.tag != 0x30u || memSh.rows.size() > 5000) continue;
                        std::vector<BerTlv> mparts;
                        if (!listChildren(memTlv.val, memTlv.valLen, mparts) || mparts.empty()) continue;

                        std::wstring refTag = decodeAsn1Str(mparts[0]);
                        std::wstring fileAttrs, subjGuid, hashAlg, hashHex;

                        // In Catalog v2, if mparts[0] is raw 20B/32B binary hash, capture as fallback digest
                        if (mparts[0].tag == 0x04u && (mparts[0].valLen == 20 || mparts[0].valLen == 32)) {
                            bool rawBin = (refTag.find(L':') != std::wstring::npos);
                            if (rawBin) {
                                hashAlg = (mparts[0].valLen == 32) ? L"SHA-256" : L"SHA-1";
                                hashHex = PathUtils::HexBytes(mparts[0].val, mparts[0].valLen, 0, 64);
                            }
                        }

                        if (mparts.size() >= 2 && (mparts[1].tag == 0x31u || mparts[1].tag == 0x30u)) {
                            std::vector<BerTlv> attrs;
                            if (listChildren(mparts[1].val, mparts[1].valLen, attrs)) {
                                for (const auto& attr : attrs) {
                                    std::vector<BerTlv> ak;
                                    if (attr.tag != 0x30u || !listChildren(attr.val, attr.valLen, ak) || ak.size() < 2 || ak[0].tag != 0x06u) continue;
                                    std::string aOid = DetailX509::DecodeDerOid(ak[0].val, ak[0].valLen);
                                    std::vector<BerTlv> aVals;
                                    if (!listChildren(ak[1].val, ak[1].valLen, aVals) || aVals.empty()) continue;

                                    if (aOid == "1.3.6.1.4.1.311.12.2.1") { // CAT_NAMEVALUE
                                        for (const auto& av : aVals) parseCatNameValueSeq(av, fileAttrs);
                                    } else if (aOid == "1.3.6.1.4.1.311.12.2.2") { // CAT_MEMBERINFO
                                        std::vector<BerTlv> mi;
                                        if (aVals[0].tag == 0x30u && listChildren(aVals[0].val, aVals[0].valLen, mi) && !mi.empty()) {
                                            subjGuid = decodeAsn1Str(mi[0]);
                                        }
                                    } else if (aOid == "1.3.6.1.4.1.311.2.1.4") { // SPC_INDIRECT_DATA_CONTENT
                                        std::vector<BerTlv> idc;
                                        if (aVals[0].tag == 0x30u && listChildren(aVals[0].val, aVals[0].valLen, idc) && idc.size() >= 2) {
                                            // idc[1] is DigestInfo ::= SEQUENCE { AlgorithmIdentifier, OCTET STRING digest }
                                            std::vector<BerTlv> di;
                                            if (idc[1].tag == 0x30u && listChildren(idc[1].val, idc[1].valLen, di) && di.size() >= 2 && di[1].tag == 0x04u) {
                                                std::vector<BerTlv> algKids;
                                                if (di[0].tag == 0x30u && listChildren(di[0].val, di[0].valLen, algKids) && !algKids.empty() && algKids[0].tag == 0x06u) {
                                                    hashAlg = mapHashAlgOid(DetailX509::DecodeDerOid(algKids[0].val, algKids[0].valLen));
                                                }
                                                hashHex = PathUtils::HexBytes(di[1].val, di[1].valLen, 0, 64);
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        memSh.rows.push_back({
                            std::to_wstring(memSh.rows.size()),
                            refTag,
                            fileAttrs,
                            subjGuid,
                            hashAlg,
                            hashHex
                        });
                    }
                }
            } else if (elem.tag == 0xA0u) { // ctlExtensions [0] EXPLICIT Extensions
                BerTlv extSeq;
                std::vector<BerTlv> exts;
                if (readBerTlv(readBerTlv, elem.val, elem.valLen, extSeq, 4) && extSeq.tag == 0x30u &&
                    listChildren(extSeq.val, extSeq.valLen, exts)) {
                    for (const auto& ex : exts) {
                        std::vector<BerTlv> ek;
                        if (ex.tag == 0x30u && listChildren(ex.val, ex.valLen, ek) && ek.size() >= 2 && ek[0].tag == 0x06u) {
                            std::string eOid = DetailX509::DecodeDerOid(ek[0].val, ek[0].valLen);
                            const BerTlv& oct = ek.back();
                            if (eOid == "1.3.6.1.4.1.311.12.2.1" && oct.tag == 0x04u) {
                                BerTlv nvSeq;
                                if (readBerTlv(readBerTlv, oct.val, oct.valLen, nvSeq, 5)) {
                                    parseCatNameValueSeq(nvSeq, catLevelAttrs);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!sawSpcCatalogOid && memSh.rows.size() <= 1) {
            return false;
        }

        // 4. Walk SignedData.certificates ([0] IMPLICIT = 0xA0 after encapContentInfo) -> Signer_Certificates
        SpreadsheetSheet certSh;
        certSh.name = L"Signer_Certificates";
        certSh.rows.push_back({ L"Certificate Field", L"Value" });
        size_t certCount = 0;

        auto extractRdnText = [&](const BerTlv& nameSeq) -> std::wstring {
            std::wstring outDn;
            std::vector<BerTlv> rdns;
            if (nameSeq.tag != 0x30u || !listChildren(nameSeq.val, nameSeq.valLen, rdns)) return outDn;
            for (const auto& rdn : rdns) {
                std::vector<BerTlv> atvs;
                if (rdn.tag != 0x31u || !listChildren(rdn.val, rdn.valLen, atvs)) continue;
                for (const auto& atv : atvs) {
                    std::vector<BerTlv> kv;
                    if (atv.tag != 0x30u || !listChildren(atv.val, atv.valLen, kv) || kv.size() < 2 || kv[0].tag != 0x06u) continue;
                    std::string oid = DetailX509::DecodeDerOid(kv[0].val, kv[0].valLen);
                    std::wstring key = (oid == "2.5.4.3") ? L"CN" : (oid == "2.5.4.10") ? L"O" :
                                       (oid == "2.5.4.11") ? L"OU" : (oid == "2.5.4.6") ? L"C" : DetailX509::MapKnownOid(oid);
                    if (!outDn.empty()) outDn += L", ";
                    outDn += key + L"=" + decodeAsn1Str(kv[1]);
                }
            }
            return outDn;
        };

        for (size_t si = 3; si < sdKids.size(); ++si) {
            if (sdKids[si].tag != 0xA0u) continue;
            std::vector<BerTlv> certs;
            if (!listChildren(sdKids[si].val, sdKids[si].valLen, certs)) continue;
            for (const auto& cTlv : certs) {
                if (cTlv.tag != 0x30u || certCount >= 32) continue;
                const uint8_t* certDerStart = cTlv.val - (cTlv.totalAdv - cTlv.valLen);
                size_t certDerLen = cTlv.totalAdv;
                std::wstring prefix = L"[Cert #" + std::to_wstring(++certCount) + L"] ";
                bool decodedWin = false;
#ifdef _WIN32
                PCCERT_CONTEXT pCert = CertCreateCertificateContext(
                    X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, certDerStart, static_cast<DWORD>(certDerLen));
                if (pCert) {
                    DetailX509::AppendCertContextRows(pCert, prefix, certSh.rows);
                    CertFreeCertificateContext(pCert);
                    decodedWin = true;
                }
#else
                (void)certDerStart;
                (void)certDerLen;
#endif
                if (!decodedWin) {
                    // Cross-platform pure ASN.1 X.509 TBSCertificate fallback
                    std::vector<BerTlv> topCert, tbs;
                    if (listChildren(cTlv.val, cTlv.valLen, topCert) && !topCert.empty() &&
                        topCert[0].tag == 0x30u && listChildren(topCert[0].val, topCert[0].valLen, tbs)) {
                        size_t ti = 0;
                        if (ti < tbs.size() && tbs[ti].tag == 0xA0u) ++ti; // [0] EXPLICIT Version
                        if (ti < tbs.size() && tbs[ti].tag == 0x02u) {
                            certSh.rows.push_back({ prefix + L"Serial Number", PathUtils::HexBytes(tbs[ti].val, tbs[ti].valLen, L':', 32) });
                            ++ti;
                        }
                        if (ti < tbs.size() && tbs[ti].tag == 0x30u) ++ti; // signature alg
                        if (ti < tbs.size() && tbs[ti].tag == 0x30u) {
                            certSh.rows.push_back({ prefix + L"Issuer DN", extractRdnText(tbs[ti]) });
                            ++ti;
                        }
                        if (ti < tbs.size() && tbs[ti].tag == 0x30u) {
                            std::vector<BerTlv> vseq;
                            if (listChildren(tbs[ti].val, tbs[ti].valLen, vseq) && vseq.size() >= 2) {
                                certSh.rows.push_back({ prefix + L"Valid Not Before", decodeAsn1Str(vseq[0]) });
                                certSh.rows.push_back({ prefix + L"Valid Not After",  decodeAsn1Str(vseq[1]) });
                            }
                            ++ti;
                        }
                        if (ti < tbs.size() && tbs[ti].tag == 0x30u) {
                            certSh.rows.push_back({ prefix + L"Subject DN", extractRdnText(tbs[ti]) });
                        }
                    }
                }
            }
        }

        SpreadsheetSheet sumSh;
        sumSh.name = L"Catalog_Summary";
        sumSh.rows.push_back({ L"Property", L"Value" });
        sumSh.rows.push_back({ L"Outer PKCS#7 ContentType", L"1.2.840.113549.1.7.2 (signedData)" });
        sumSh.rows.push_back({ L"Encapsulated Content", eContentType == "1.3.6.1.4.1.311.10.1"
            ? L"1.3.6.1.4.1.311.10.1 (CTL -> SPC_CATALOG_LIST)"
            : L"1.3.6.1.4.1.311.12.1.1 (SPC_CATALOG_LIST)" });
        if (!catVersionStr.empty())    sumSh.rows.push_back({ L"Catalog Version", catVersionStr });
        if (!listIdStr.empty())        sumSh.rows.push_back({ L"Catalog List ID", listIdStr });
        if (!effectiveTimeStr.empty()) sumSh.rows.push_back({ L"Timestamp (UTC)", effectiveTimeStr });
        if (!subjAlgStr.empty())       sumSh.rows.push_back({ L"Subject Algorithm", subjAlgStr });
        if (!catLevelAttrs.empty())    sumSh.rows.push_back({ L"Catalog Attributes", catLevelAttrs });
        sumSh.rows.push_back({ L"Catalog Member Entries", std::to_wstring(memSh.rows.size() - 1) });
        sumSh.rows.push_back({ L"Embedded Signer Certificates", std::to_wstring(certCount) });

        FinalizeSheetCols(sumSh);
        FinalizeSheetCols(memSh);
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Authenticode PKCS#7 Security Catalog (.cat)";
        out.sheets.push_back(std::move(memSh));
        out.sheets.push_back(std::move(sumSh));
        if (certSh.rows.size() > 1) {
            FinalizeSheetCols(certSh);
            out.sheets.push_back(std::move(certSh));
        }
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
