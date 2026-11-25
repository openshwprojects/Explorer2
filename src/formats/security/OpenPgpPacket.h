#pragma once
// OpenPgpPacket.h — OpenPGP RFC 4880 Binary & ASCII-Armored Packet Inspector (.gpg/.pgp/.sig)

namespace Formats {

inline bool ParseOpenPgpPacketDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #89. OpenPGP RFC 4880 Binary & ASCII-Armored Packet Inspector (.gpg / .pgp / .sig)
    if (ext == L".gpg" || ext == L".pgp" || ext == L".sig") {
        std::vector<uint8_t> decoded;
        const uint8_t* pktData = raw.data();
        size_t pktSize = raw.size();
        bool armored = false;

        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("-----BEGIN PGP ") != std::string_view::npos) {
            armored = true;
            size_t hdrEnd = sv.find("\r\n\r\n");
            size_t sep = 4;
            if (hdrEnd == std::string_view::npos) { hdrEnd = sv.find("\n\n"); sep = 2; }
            if (hdrEnd != std::string_view::npos) {
                size_t ftr = sv.find("-----END PGP ", hdrEnd + sep);
                std::string_view b64 = sv.substr(hdrEnd + sep, (ftr == std::string_view::npos ? sv.size() : ftr) - (hdrEnd + sep));
                // Strip CRC24 line starting with '='
                size_t crcEq = b64.rfind("\n=");
                if (crcEq != std::string_view::npos) b64 = b64.substr(0, crcEq);
                decoded = DetailStructuredDataDoc::DecodeBase64Block(b64);
                pktData = decoded.data();
                pktSize = decoded.size();
            }
        }
        if (pktSize < 2 || (pktData[0] & 0x80u) == 0) return false;

        auto pgpTagName = [](uint8_t tag) -> const wchar_t* {
            switch (tag) {
            case 1:  return L"Public-Key Encrypted Session Key (PKESK)";
            case 2:  return L"Signature Packet";
            case 3:  return L"Symmetric-Key Encrypted Session Key (SKESK)";
            case 4:  return L"One-Pass Signature";
            case 5:  return L"Secret-Key Packet";
            case 6:  return L"Public-Key Packet";
            case 7:  return L"Secret-Subkey Packet";
            case 8:  return L"Compressed Data";
            case 9:  return L"Symmetrically Encrypted Data";
            case 11: return L"Literal Data";
            case 13: return L"User ID";
            case 14: return L"Public-Subkey Packet";
            case 18: return L"Sym. Encrypted Integrity Protected Data (SEIPD)";
            default: return L"OpenPGP Packet";
            }
        };
        auto pgpAlgName = [](uint8_t alg) -> std::wstring {
            switch (alg) {
            case 1: case 2: case 3: return L"RSA";
            case 16: return L"Elgamal";
            case 17: return L"DSA";
            case 18: return L"ECDH (Curve25519/NIST)";
            case 19: return L"ECDSA";
            case 22: return L"EdDSA (Ed25519)";
            default: return L"Alg#" + std::to_wstring(alg);
            }
        };

        SpreadsheetSheet sh;
        sh.name = L"OpenPGP_Packets";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Packet #", L"RFC 4880 Tag", L"Body Length", L"Algorithm / Identity Details" });

        size_t pos = 0;
        int idx = 0;
        while (pos < pktSize && idx < 128) {
            uint8_t ctb = pktData[pos++];
            if ((ctb & 0x80u) == 0) break;
            uint8_t tag = 0;
            size_t bodyLen = 0;
            if ((ctb & 0x40u) != 0) { // New packet format
                tag = ctb & 0x3Fu;
                if (pos >= pktSize) break;
                uint8_t l0 = pktData[pos++];
                if (l0 < 192) bodyLen = l0;
                else if (l0 < 224) {
                    if (pos >= pktSize) break;
                    bodyLen = ((static_cast<size_t>(l0 - 192) << 8) + pktData[pos++]) + 192;
                } else if (l0 == 255) {
                    if (pos + 4 > pktSize) break;
                    bodyLen = DetailStructuredDataDoc::RdU32BE(pktData + pos);
                    pos += 4;
                } else {
                    bodyLen = 1ull << (l0 & 0x1Fu); // Partial body length
                }
            } else { // Old packet format
                tag = (ctb >> 2) & 0x0Fu;
                uint8_t ltype = ctb & 0x03u;
                if (ltype == 0) { if (pos >= pktSize) break; bodyLen = pktData[pos++]; }
                else if (ltype == 1) { if (pos + 2 > pktSize) break; bodyLen = DetailStructuredDataDoc::RdU16BE(pktData + pos); pos += 2; }
                else if (ltype == 2) { if (pos + 4 > pktSize) break; bodyLen = DetailStructuredDataDoc::RdU32BE(pktData + pos); pos += 4; }
                else bodyLen = pktSize - pos;
            }
            if (bodyLen > pktSize - pos) bodyLen = pktSize - pos;
            const uint8_t* body = pktData + pos;

            std::wstring detail = armored ? L"ASCII-Armored" : L"Binary";
            if (tag == 13 && bodyLen > 0) {
                detail = L"UID: " + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(body), bodyLen));
            } else if ((tag == 6 || tag == 14 || tag == 5 || tag == 7) && bodyLen >= 6) {
                uint8_t ver = body[0];
                uint32_t ts = DetailStructuredDataDoc::RdU32BE(body + 1);
                uint8_t alg = (ver == 4) ? body[5] : (bodyLen >= 8 ? body[7] : 0);
                detail = L"v" + std::to_wstring(ver) + L" " + pgpAlgName(alg) + L" (Created Unix=" + std::to_wstring(ts) + L")";
            } else if (tag == 2 && bodyLen >= 4) {
                uint8_t ver = body[0];
                uint8_t pkAlg = (ver == 4) ? body[2] : (bodyLen >= 17 ? body[15] : 0);
                detail = L"v" + std::to_wstring(ver) + L" Sig via " + pgpAlgName(pkAlg);
            } else if (tag == 1 && bodyLen >= 10) {
                detail = L"Recipient KeyID: " + DetailStructuredDataDoc::HexBytes(body + 1, 8, 8) + L" (" + pgpAlgName(body[9]) + L")";
            }

            sh.rows.push_back({ std::to_wstring(++idx), pgpTagName(tag), std::to_wstring(bodyLen) + L" B", detail });
            pos += bodyLen;
        }
        if (idx == 0) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = std::wstring(L"OpenPGP Cryptographic Packet Stream (") + (armored ? L"ASCII-Armored " : L"Binary ") + ext + L")";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
