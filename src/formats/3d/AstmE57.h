#pragma once
// AstmE57.h — ASTM E2807 3D Laser Scan (.e57) point cloud reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DetailE57_200 {

inline std::string_view XmlAttr(std::string_view tag, std::string_view attrName) {
    size_t pos = 0;
    while (pos < tag.size()) {
        size_t hit = tag.find(attrName, pos);
        if (hit == std::string_view::npos) return {};
        bool leftOk = (hit == 0) || tag[hit - 1] == ' ' || tag[hit - 1] == '\t' ||
                      tag[hit - 1] == '\r' || tag[hit - 1] == '\n' || tag[hit - 1] == ':';
        size_t eq = hit + attrName.size();
        while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
        if (leftOk && eq < tag.size() && tag[eq] == '=') {
            size_t q0 = eq + 1;
            while (q0 < tag.size() && (tag[q0] == ' ' || tag[q0] == '\t')) ++q0;
            if (q0 < tag.size() && (tag[q0] == '"' || tag[q0] == '\'')) {
                char qc = tag[q0++];
                size_t q1 = tag.find(qc, q0);
                if (q1 != std::string_view::npos) return tag.substr(q0, q1 - q0);
            }
        }
        pos = hit + attrName.size();
    }
    return {};
}

inline double XmlChildFloat(std::string_view scope, std::string_view localName, double defVal) {
    size_t p = 0;
    while (p < scope.size()) {
        size_t lt = scope.find('<', p);
        if (lt == std::string_view::npos || lt + 1 >= scope.size()) break;
        if (scope[lt + 1] == '/' || scope[lt + 1] == '!' || scope[lt + 1] == '?') { p = lt + 2; continue; }
        size_t gt = scope.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view openTag = scope.substr(lt + 1, gt - lt - 1);
        size_t nameEnd = openTag.find_first_of(" \t\r\n/");
        std::string_view qName = (nameEnd == std::string_view::npos) ? openTag : openTag.substr(0, nameEnd);
        size_t colon = qName.rfind(':');
        std::string_view base = (colon == std::string_view::npos) ? qName : qName.substr(colon + 1);
        if (base == localName) {
            if (!openTag.empty() && openTag.back() == '/') return 0.0; // E57 XML self-closing Float tag == 0.0!
            size_t closeLt = scope.find('<', gt + 1);
            if (closeLt == std::string_view::npos) return defVal;
            std::string txt(scope.substr(gt + 1, closeLt - gt - 1));
            char* ep = nullptr;
            double v = std::strtod(txt.c_str(), &ep);
            return (ep != txt.c_str() && std::isfinite(v)) ? v : 0.0;
        }
        p = gt + 1;
    }
    return defVal;
}

struct E57FieldDesc {
    std::string name;
    enum Kind { Float32, Float64, Integer, ScaledInteger } kind = Float32;
    uint32_t bitWidth = 32;
    int64_t minimum = 0;
    int64_t maximum = 0;
    double scale = 1.0;
    double offset = 0.0;
};

struct E57BitStream {
    std::vector<uint8_t> bytes;
    size_t bitPos = 0;

    size_t AvailBits() const {
        size_t total = bytes.size() * 8;
        return (total > bitPos) ? (total - bitPos) : 0;
    }

    void Append(const uint8_t* src, size_t len) {
        if (bitPos >= 65536) {
            size_t dropBytes = bitPos >> 3;
            bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(dropBytes));
            bitPos &= 7u;
        }
        bytes.insert(bytes.end(), src, src + len);
    }

    uint64_t ReadBitsLSB(uint32_t nBits) {
        if (nBits == 0) return 0;
        if (nBits == 32 && (bitPos & 7u) == 0) {
            uint32_t v = Detail3D200::RdU32LE(bytes.data() + (bitPos >> 3));
            bitPos += 32;
            return v;
        }
        if (nBits == 64 && (bitPos & 7u) == 0) {
            uint64_t v = Detail3D200::RdU64LE(bytes.data() + (bitPos >> 3));
            bitPos += 64;
            return v;
        }
        uint64_t out = 0;
        for (uint32_t b = 0; b < nBits; ++b) {
            size_t bp = bitPos + b;
            uint64_t bit = (bytes[bp >> 3] >> (bp & 7u)) & 1u;
            out |= (bit << b);
        }
        bitPos += nBits;
        return out;
    }
};

} // namespace DetailE57_200


inline bool ParseAstmE57(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    // #55. ASTM E2807 3D Laser Scan (.e57 — Paged CRC32C + XML + CompressedVectorSection Decoder)
    if (ext == L".e57" || (size >= 48 && std::memcmp(data, "ASTM-E57", 8) == 0)) {
        if (size < 48 || std::memcmp(data, "ASTM-E57", 8) != 0) return false;

        uint32_t majVer      = Detail3D200::RdU32LE(data + 8);
        uint32_t minVer      = Detail3D200::RdU32LE(data + 12);
        (void)minVer;
        uint64_t filePhysLen = Detail3D200::RdU64LE(data + 16);
        uint64_t xmlPhysOff  = Detail3D200::RdU64LE(data + 24);
        uint64_t xmlLogLen   = Detail3D200::RdU64LE(data + 32);
        uint64_t pageSize    = Detail3D200::RdU64LE(data + 40);

        if (majVer == 0 || majVer > 2) return false;
        if (pageSize < 64 || pageSize > 65536 || (pageSize % 4u) != 0) return false;
        const size_t ps     = static_cast<size_t>(pageSize);
        const size_t usable = ps - 4;

        // Validate physical file length (allowing exact 64MB viewer read cap if file exceeds 64MB)
        const size_t kViewerReadCap = 64ULL * 1024ULL * 1024ULL;
        if (filePhysLen < pageSize) return false;
        if (size < kViewerReadCap && filePhysLen > size) return false;
        const size_t effSize = static_cast<size_t>(std::min<uint64_t>(size, filePhysLen));
        if (effSize < ps) return false;

        if (xmlPhysOff < 48 || xmlPhysOff >= effSize || (xmlPhysOff % ps) >= usable) return false;
        if (xmlLogLen == 0 || xmlLogLen > 32ULL * 1024ULL * 1024ULL) return false;

        // Cached per-page Castagnoli CRC32C verifier
        std::vector<uint8_t> pageCrcState(effSize / ps + 1, 0); // 0=unchecked, 1=valid, 2=invalid
        auto verifyPage = [&](size_t pageIdx) -> bool {
            if (pageIdx >= pageCrcState.size()) return false;
            if (pageCrcState[pageIdx] != 0) return pageCrcState[pageIdx] == 1;
            size_t pageStart = pageIdx * ps;
            if (pageStart + ps > effSize) {
                pageCrcState[pageIdx] = 2;
                return false;
            }
            uint32_t expected = ::Explorer2::Checksum::Crc32C(data + pageStart, usable);
            uint32_t storedBE = Detail3D200::RdU32BE(data + pageStart + usable);
            uint32_t storedLE = Detail3D200::RdU32LE(data + pageStart + usable);
            bool ok = (storedBE == expected || storedLE == expected);
            pageCrcState[pageIdx] = ok ? 1 : 2;
            return ok;
        };

        if (!verifyPage(0)) return false;

        auto physToLogical = [&](uint64_t physOff, uint64_t& outLogOff) -> bool {
            if (physOff >= effSize || (physOff % ps) >= usable) return false;
            outLogOff = (physOff / ps) * usable + (physOff % ps);
            return true;
        };

        auto readLogicalBytes = [&](uint64_t logOff, size_t len, uint8_t* dst) -> bool {
            size_t done = 0;
            uint64_t curLog = logOff;
            while (done < len) {
                size_t pageIdx   = static_cast<size_t>(curLog / usable);
                size_t offInPage = static_cast<size_t>(curLog % usable);
                if (!verifyPage(pageIdx)) return false;
                size_t chunk = std::min<size_t>(len - done, usable - offInPage);
                std::memcpy(dst + done, data + pageIdx * ps + offInPage, chunk);
                done   += chunk;
                curLog += chunk;
            }
            return true;
        };

        // Extract and verify logical XML section
        uint64_t xmlLogOff = 0;
        if (!physToLogical(xmlPhysOff, xmlLogOff)) return false;
        std::string xmlStr(static_cast<size_t>(xmlLogLen), '\0');
        if (!readLogicalBytes(xmlLogOff, static_cast<size_t>(xmlLogLen), reinterpret_cast<uint8_t*>(xmlStr.data()))) {
            return false;
        }
        std::string_view xmlSv(xmlStr);
        if (xmlSv.find("e57Root") == std::string_view::npos || xmlSv.find("data3D") == std::string_view::npos) {
            return false;
        }

        mesh.formatName = L"ASTM E57 3D LiDAR Point Cloud (.e57)";

        size_t d3Start = xmlSv.find("data3D");
        size_t d3End   = xmlSv.rfind("data3D");
        if (d3Start == std::string_view::npos || d3End <= d3Start) return false;
        std::string_view data3DScope = xmlSv.substr(d3Start, d3End - d3Start);

        // Split into per-scan scopes (<vectorChild ...>...</vectorChild>, or entire data3DScope if single flat scan)
        std::vector<std::string_view> scanScopes;
        {
            size_t sp = 0;
            while (sp < data3DScope.size()) {
                size_t vcOpen = data3DScope.find("<vectorChild", sp);
                if (vcOpen == std::string_view::npos) break;
                // Find matching </vectorChild> accounting for nested <vectorChild> (e.g. indexBounds/grouping)
                size_t cur = vcOpen + 12;
                int depth = 1;
                while (cur < data3DScope.size() && depth > 0) {
                    size_t nextLt = data3DScope.find('<', cur);
                    if (nextLt == std::string_view::npos) break;
                    if (data3DScope.substr(nextLt, 13) == "</vectorChild") {
                        --depth;
                        size_t gt = data3DScope.find('>', nextLt + 13);
                        cur = (gt == std::string_view::npos) ? data3DScope.size() : (gt + 1);
                    } else if (data3DScope.substr(nextLt, 12) == "<vectorChild") {
                        size_t gt = data3DScope.find('>', nextLt + 12);
                        if (gt != std::string_view::npos && data3DScope[gt - 1] != '/') ++depth;
                        cur = (gt == std::string_view::npos) ? data3DScope.size() : (gt + 1);
                    } else {
                        cur = nextLt + 1;
                    }
                }
                scanScopes.push_back(data3DScope.substr(vcOpen, cur - vcOpen));
                sp = cur;
            }
            if (scanScopes.empty()) scanScopes.push_back(data3DScope);
        }

        std::vector<Vec3f> allPts;
        const size_t kMaxTotalPts = 25000;

        for (std::string_view scanSv : scanScopes) {
            if (allPts.size() >= kMaxTotalPts) break;

            // 1. Parse optional <pose> (<rotation> quaternion w,x,y,z + <translation> x,y,z)
            double qw = 1.0, qx = 0.0, qy = 0.0, qz = 0.0;
            double tx = 0.0, ty = 0.0, tz = 0.0;
            size_t poseOpen = scanSv.find("<pose");
            if (poseOpen != std::string_view::npos) {
                size_t poseClose = scanSv.find("</pose>", poseOpen);
                if (poseClose != std::string_view::npos) {
                    std::string_view poseSv = scanSv.substr(poseOpen, poseClose - poseOpen);
                    size_t rotOpen = poseSv.find("<rotation"), rotClose = poseSv.find("</rotation>", rotOpen == std::string_view::npos ? 0 : rotOpen);
                    if (rotOpen != std::string_view::npos && rotClose != std::string_view::npos) {
                        std::string_view rotSv = poseSv.substr(rotOpen + 9, rotClose - rotOpen - 9);
                        qw = DetailE57_200::XmlChildFloat(rotSv, "w", 1.0);
                        qx = DetailE57_200::XmlChildFloat(rotSv, "x", 0.0);
                        qy = DetailE57_200::XmlChildFloat(rotSv, "y", 0.0);
                        qz = DetailE57_200::XmlChildFloat(rotSv, "z", 0.0);
                        double qn = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
                        if (qn > 1e-12) { qw /= qn; qx /= qn; qy /= qn; qz /= qn; }
                        else { qw = 1.0; qx = qy = qz = 0.0; }
                    }
                    size_t trOpen = poseSv.find("<translation"), trClose = poseSv.find("</translation>", trOpen == std::string_view::npos ? 0 : trOpen);
                    if (trOpen != std::string_view::npos && trClose != std::string_view::npos) {
                        std::string_view trSv = poseSv.substr(trOpen + 12, trClose - trOpen - 12);
                        tx = DetailE57_200::XmlChildFloat(trSv, "x", 0.0);
                        ty = DetailE57_200::XmlChildFloat(trSv, "y", 0.0);
                        tz = DetailE57_200::XmlChildFloat(trSv, "z", 0.0);
                    }
                }
            }

            double r00 = 1.0 - 2.0 * (qy * qy + qz * qz), r01 = 2.0 * (qx * qy - qw * qz), r02 = 2.0 * (qx * qz + qw * qy);
            double r10 = 2.0 * (qx * qy + qw * qz), r11 = 1.0 - 2.0 * (qx * qx + qz * qz), r12 = 2.0 * (qy * qz - qw * qx);
            double r20 = 2.0 * (qx * qz - qw * qy), r21 = 2.0 * (qy * qz + qw * qx), r22 = 1.0 - 2.0 * (qx * qx + qy * qy);

            // 2. Locate <points type="CompressedVector" fileOffset="..." recordCount="...">
            size_t ptsOpen = std::string_view::npos;
            {
                size_t search = 0;
                while (search < scanSv.size()) {
                    size_t cand = scanSv.find("<points", search);
                    if (cand == std::string_view::npos) break;
                    char nextC = (cand + 7 < scanSv.size()) ? scanSv[cand + 7] : '\0';
                    if (nextC == ' ' || nextC == '\t' || nextC == '\r' || nextC == '\n' || nextC == '>') {
                        ptsOpen = cand;
                        break;
                    }
                    search = cand + 7;
                }
            }
            if (ptsOpen == std::string_view::npos) continue;
            size_t ptsTagGt = scanSv.find('>', ptsOpen);
            size_t ptsClose = scanSv.find("</points>", ptsOpen);
            if (ptsTagGt == std::string_view::npos || ptsClose == std::string_view::npos || ptsClose <= ptsTagGt) return false;

            std::string_view ptsOpenTag = scanSv.substr(ptsOpen, ptsTagGt - ptsOpen + 1);
            std::string_view ptsType    = DetailE57_200::XmlAttr(ptsOpenTag, "type");
            std::string_view fileOffSv  = DetailE57_200::XmlAttr(ptsOpenTag, "fileOffset");
            std::string_view recCntSv   = DetailE57_200::XmlAttr(ptsOpenTag, "recordCount");
            if (ptsType != "CompressedVector" || fileOffSv.empty() || recCntSv.empty()) return false;

            char* ep1 = nullptr;
            uint64_t cvPhysOff = std::strtoull(std::string(fileOffSv).c_str(), &ep1, 10);
            char* ep2 = nullptr;
            uint64_t recordCount = std::strtoull(std::string(recCntSv).c_str(), &ep2, 10);
            if (cvPhysOff < 48 || recordCount == 0 || recordCount > 500000000ULL) return false;

            // 3. Parse <prototype> field descriptors in exact declaration order
            std::string_view ptsBody = scanSv.substr(ptsTagGt + 1, ptsClose - ptsTagGt - 1);
            size_t protoOpen = ptsBody.find("<prototype");
            size_t protoClose = ptsBody.find("</prototype>", protoOpen == std::string_view::npos ? 0 : protoOpen);
            if (protoOpen == std::string_view::npos || protoClose == std::string_view::npos) return false;
            size_t protoGt = ptsBody.find('>', protoOpen);
            if (protoGt == std::string_view::npos || protoGt >= protoClose) return false;
            std::string_view protoBody = ptsBody.substr(protoGt + 1, protoClose - protoGt - 1);

            std::vector<DetailE57_200::E57FieldDesc> fields;
            int idxCartX = -1, idxCartY = -1, idxCartZ = -1, idxCartInv = -1;
            int idxSphR  = -1, idxSphAz = -1, idxSphEl = -1, idxSphInv  = -1;

            size_t fp = 0;
            while (fp < protoBody.size()) {
                size_t lt = protoBody.find('<', fp);
                if (lt == std::string_view::npos || lt + 1 >= protoBody.size()) break;
                if (protoBody[lt + 1] == '/' || protoBody[lt + 1] == '!' || protoBody[lt + 1] == '?') {
                    size_t gtSkip = protoBody.find('>', lt + 2);
                    if (gtSkip == std::string_view::npos) break;
                    fp = gtSkip + 1;
                    continue;
                }
                size_t gt = protoBody.find('>', lt + 1);
                if (gt == std::string_view::npos) return false;
                std::string_view fTag = protoBody.substr(lt + 1, gt - lt - 1);
                bool selfClosing = (!fTag.empty() && fTag.back() == '/');

                size_t nmEnd = fTag.find_first_of(" \t\r\n/");
                std::string_view qName = (nmEnd == std::string_view::npos) ? fTag : fTag.substr(0, nmEnd);
                size_t colon = qName.rfind(':');
                std::string fieldName(colon == std::string_view::npos ? qName : qName.substr(colon + 1));

                std::string_view fType = DetailE57_200::XmlAttr(fTag, "type");
                DetailE57_200::E57FieldDesc fd;
                fd.name = fieldName;

                auto parseAttrI64 = [&](std::string_view key, int64_t defV) -> int64_t {
                    std::string_view s = DetailE57_200::XmlAttr(fTag, key);
                    if (s.empty()) return defV;
                    return std::strtoll(std::string(s).c_str(), nullptr, 10);
                };
                auto parseAttrF64 = [&](std::string_view key, double defV) -> double {
                    std::string_view s = DetailE57_200::XmlAttr(fTag, key);
                    if (s.empty()) return defV;
                    double v = std::strtod(std::string(s).c_str(), nullptr);
                    return std::isfinite(v) ? v : defV;
                };

                if (fType == "Float") {
                    std::string_view prec = DetailE57_200::XmlAttr(fTag, "precision");
                    if (prec == "single") { fd.kind = DetailE57_200::E57FieldDesc::Float32; fd.bitWidth = 32; }
                    else                  { fd.kind = DetailE57_200::E57FieldDesc::Float64; fd.bitWidth = 64; }
                } else if (fType == "Integer" || fType == "ScaledInteger") {
                    fd.minimum = parseAttrI64("minimum", -2147483648LL);
                    fd.maximum = parseAttrI64("maximum",  2147483647LL);
                    if (fd.maximum < fd.minimum) return false;
                    uint64_t range = static_cast<uint64_t>(fd.maximum) - static_cast<uint64_t>(fd.minimum);
                    uint32_t bits = 0;
                    if (range == 0xFFFFFFFFFFFFFFFFULL) {
                        bits = 64;
                    } else if (range > 0) {
                        bits = 1;
                        while (bits < 64 && (range >> bits) != 0) ++bits;
                    }
                    fd.bitWidth = bits;
                    if (fType == "ScaledInteger") {
                        fd.kind   = DetailE57_200::E57FieldDesc::ScaledInteger;
                        fd.scale  = parseAttrF64("scale",  1.0);
                        fd.offset = parseAttrF64("offset", 0.0);
                    } else {
                        fd.kind = DetailE57_200::E57FieldDesc::Integer;
                    }
                } else {
                    return false;
                }

                int curFieldIdx = static_cast<int>(fields.size());
                if (fieldName == "cartesianX")            idxCartX   = curFieldIdx;
                else if (fieldName == "cartesianY")       idxCartY   = curFieldIdx;
                else if (fieldName == "cartesianZ")       idxCartZ   = curFieldIdx;
                else if (fieldName == "cartesianInvalidState") idxCartInv = curFieldIdx;
                else if (fieldName == "sphericalRange")   idxSphR    = curFieldIdx;
                else if (fieldName == "sphericalAzimuth") idxSphAz   = curFieldIdx;
                else if (fieldName == "sphericalElevation") idxSphEl = curFieldIdx;
                else if (fieldName == "sphericalInvalidState") idxSphInv = curFieldIdx;

                fields.push_back(std::move(fd));

                if (!selfClosing) {
                    std::string closePat = "</" + std::string(qName) + ">";
                    size_t cPos = protoBody.find(closePat, gt + 1);
                    fp = (cPos == std::string_view::npos) ? (gt + 1) : (cPos + closePat.size());
                } else {
                    fp = gt + 1;
                }
            }

            bool hasCartesian = (idxCartX >= 0 && idxCartY >= 0 && idxCartZ >= 0);
            bool hasSpherical = (idxSphR >= 0 && idxSphAz >= 0 && idxSphEl >= 0);
            if (fields.empty() || (!hasCartesian && !hasSpherical)) return false;

            // 4. Seek to CompressedVectorSection (32 logical bytes)
            uint64_t secLogOff = 0;
            if (!physToLogical(cvPhysOff, secLogOff)) return false;
            uint8_t secHdr[32] = {};
            if (!readLogicalBytes(secLogOff, 32, secHdr)) return false;
            if (secHdr[0] != 1) return false; // sectionId == 1 (CompressedVectorSection)

            uint64_t secLogLen   = Detail3D200::RdU64LE(secHdr + 8);
            uint64_t dataPhysOff = Detail3D200::RdU64LE(secHdr + 16);
            uint64_t pktLogPos   = 0;
            if (secLogLen < 32 || !physToLogical(dataPhysOff, pktLogPos)) return false;
            uint64_t secLogEnd = secLogOff + secLogLen;
            if (pktLogPos < secLogOff + 32 || pktLogPos >= secLogEnd) return false;

            // 5. Iterate DataPackets & unpack planar bitstreams
            const size_t nFields = fields.size();
            std::vector<DetailE57_200::E57BitStream> streams(nFields);
            std::vector<double> recVals(nFields, 0.0);
            std::vector<uint8_t> pktBuf;
            uint64_t decodedCount = 0;
            uint64_t subsampleStep = std::max<uint64_t>(1, recordCount / kMaxTotalPts);

            while (decodedCount < recordCount && pktLogPos + 4 <= secLogEnd && allPts.size() < kMaxTotalPts) {
                uint8_t pktHdr[4] = {};
                if (!readLogicalBytes(pktLogPos, 4, pktHdr)) return false;
                uint8_t pktType = pktHdr[0];
                size_t pktLen   = static_cast<size_t>(Detail3D200::RdU16LE(pktHdr + 2)) + 1;
                if (pktLogPos + pktLen > secLogEnd) return false;

                pktBuf.resize(pktLen);
                if (!readLogicalBytes(pktLogPos, pktLen, pktBuf.data())) return false;

                if (pktType == 1) { // DataPacket
                    if (pktLen < 6 + nFields * 2) return false;
                    uint16_t bsCount = Detail3D200::RdU16LE(pktBuf.data() + 4);
                    if (bsCount != nFields) return false;

                    size_t payCursor = 6 + nFields * 2;
                    for (size_t f = 0; f < nFields; ++f) {
                        uint16_t bsLen = Detail3D200::RdU16LE(pktBuf.data() + 6 + f * 2);
                        if (payCursor + bsLen > pktLen) return false;
                        if (bsLen > 0) streams[f].Append(pktBuf.data() + payCursor, bsLen);
                        payCursor += bsLen;
                    }

                    // Extract all complete records currently available across all streams
                    while (decodedCount < recordCount && allPts.size() < kMaxTotalPts) {
                        bool ready = true;
                        for (size_t f = 0; f < nFields; ++f) {
                            if (fields[f].bitWidth > 0 && streams[f].AvailBits() < fields[f].bitWidth) {
                                ready = false;
                                break;
                            }
                        }
                        if (!ready) break;

                        bool finiteOk = true;
                        for (size_t f = 0; f < nFields; ++f) {
                            const auto& fd = fields[f];
                            uint64_t rawBits = streams[f].ReadBitsLSB(fd.bitWidth);
                            if (fd.kind == DetailE57_200::E57FieldDesc::Float32) {
                                uint32_t u32 = static_cast<uint32_t>(rawBits);
                                float fv = 0.0f;
                                std::memcpy(&fv, &u32, 4);
                                if (!std::isfinite(fv)) finiteOk = false;
                                recVals[f] = static_cast<double>(fv);
                            } else if (fd.kind == DetailE57_200::E57FieldDesc::Float64) {
                                double dv = 0.0;
                                std::memcpy(&dv, &rawBits, 8);
                                if (!std::isfinite(dv)) finiteOk = false;
                                recVals[f] = dv;
                            } else if (fd.kind == DetailE57_200::E57FieldDesc::Integer) {
                                int64_t iv = static_cast<int64_t>(static_cast<uint64_t>(fd.minimum) + rawBits);
                                recVals[f] = static_cast<double>(iv);
                            } else { // ScaledInteger
                                int64_t iv = static_cast<int64_t>(static_cast<uint64_t>(fd.minimum) + rawBits);
                                recVals[f] = static_cast<double>(iv) * fd.scale + fd.offset;
                            }
                        }

                        uint64_t recIdx = decodedCount++;
                        if (!finiteOk || (recIdx % subsampleStep) != 0) continue;

                        double lx = 0.0, ly = 0.0, lz = 0.0;
                        if (hasCartesian) {
                            if (idxCartInv >= 0 && std::llround(recVals[static_cast<size_t>(idxCartInv)]) != 0) continue;
                            lx = recVals[static_cast<size_t>(idxCartX)];
                            ly = recVals[static_cast<size_t>(idxCartY)];
                            lz = recVals[static_cast<size_t>(idxCartZ)];
                        } else {
                            if (idxSphInv >= 0 && std::llround(recVals[static_cast<size_t>(idxSphInv)]) != 0) continue;
                            double rng = recVals[static_cast<size_t>(idxSphR)];
                            double az  = recVals[static_cast<size_t>(idxSphAz)];
                            double el  = recVals[static_cast<size_t>(idxSphEl)];
                            lx = rng * std::cos(el) * std::cos(az);
                            ly = rng * std::cos(el) * std::sin(az);
                            lz = rng * std::sin(el);
                        }

                        double wx = r00 * lx + r01 * ly + r02 * lz + tx;
                        double wy = r10 * lx + r11 * ly + r12 * lz + ty;
                        double wz = r20 * lx + r21 * ly + r22 * lz + tz;
                        if (std::isfinite(wx) && std::isfinite(wy) && std::isfinite(wz)) {
                            allPts.push_back({
                                static_cast<float>(wx),
                                static_cast<float>(wz),
                                static_cast<float>(-wy)
                            });
                        }
                    }
                } else if (pktType != 0 && pktType != 2) {
                    return false;
                }

                pktLogPos += (static_cast<uint64_t>(pktLen) + 3ULL) & ~3ULL;
            }
        }

        if (allPts.empty()) return false;
        float r = Detail3D200::ComputePointCloudSpan(allPts) * 0.018f;
        for (const auto& pt : allPts) Detail3D200::AddPointTetra(mesh, pt, r);
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
