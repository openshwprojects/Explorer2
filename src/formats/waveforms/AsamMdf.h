#pragma once
// AsamMdf.h — ASAM MDF 3.x & MDF 4.x ECU Measurement Data (.mdf / .mf4) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// #74. ASAM MDF 3.x & MDF 4.x ECU Telemetry (.mdf / .mf4 — "MDF     " / "UnFinMF ")
inline bool ParseAsamMdf200(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (ext == L".mf4" || ext == L".mdf") {
        if (size < 64) return false;
        if (std::memcmp(data, "MDF     ", 8) != 0 && std::memcmp(data, "UnFinMF ", 8) != 0) return false;

        std::string verStr(reinterpret_cast<const char*>(data + 8), strnlen(reinterpret_cast<const char*>(data + 8), 8));
        while (!verStr.empty() && (verStr.back() == ' ' || verStr.back() == '\0')) verStr.pop_back();
        const uint16_t idVerNum = DetailEda200::RdU16LE(data + 28);

        struct MdfConv {
            uint8_t type = 0; // 0=identity, 1=linear P1+P2*x, 2=rational
            std::vector<double> p;
            std::string unit;

            double Apply(double raw) const {
                if (type == 1 && p.size() >= 2) {
                    return p[0] + p[1] * raw;
                }
                if (type == 2 && p.size() >= 6) {
                    double den = p[3] * raw * raw + p[4] * raw + p[5];
                    return (std::fabs(den) > 1e-18) ? ((p[0] * raw * raw + p[1] * raw + p[2]) / den) : raw;
                }
                return raw;
            }
        };

        struct MdfDecodedCh {
            std::wstring name;
            std::wstring unit;
            std::vector<double> t;
            std::vector<double> y;
            double yMin =  1e300;
            double yMax = -1e300;
            size_t totalRecords = 0;
        };

        auto extractRawSample = [](const uint8_t* rec, size_t recBytes, uint32_t byteOff, uint8_t bitOff,
                                   uint32_t bitCnt, uint8_t dType, double& outVal) -> bool {
            byteOff += static_cast<uint32_t>(bitOff / 8u);
            bitOff  = static_cast<uint8_t>(bitOff % 8u);
            if (bitCnt == 0u || bitCnt > 64u) return false;
            const size_t needBytes = (static_cast<size_t>(bitOff) + static_cast<size_t>(bitCnt) + 7u) / 8u;
            if (static_cast<size_t>(byteOff) + needBytes > recBytes) return false;

            const uint8_t* p = rec + byteOff;
            if (dType == 4u && bitOff == 0u) { // FLOAT_LE
                if (bitCnt == 32u) {
                    uint32_t u = DetailEda200::RdU32LE(p);
                    float fv = 0.0f;
                    std::memcpy(&fv, &u, 4);
                    if (!std::isfinite(fv)) return false;
                    outVal = static_cast<double>(fv);
                    return true;
                }
                if (bitCnt == 64u) {
                    uint64_t u = DetailEda200::RdU64LE(p);
                    double dv = 0.0;
                    std::memcpy(&dv, &u, 8);
                    if (!std::isfinite(dv)) return false;
                    outVal = dv;
                    return true;
                }
                return false;
            }
            if (dType == 5u && bitOff == 0u) { // FLOAT_BE
                if (bitCnt == 32u) {
                    uint32_t u = DetailEda200::RdU32BE(p);
                    float fv = 0.0f;
                    std::memcpy(&fv, &u, 4);
                    if (!std::isfinite(fv)) return false;
                    outVal = static_cast<double>(fv);
                    return true;
                }
                if (bitCnt == 64u) {
                    uint64_t u = (static_cast<uint64_t>(DetailEda200::RdU32BE(p)) << 32) |
                                 static_cast<uint64_t>(DetailEda200::RdU32BE(p + 4));
                    double dv = 0.0;
                    std::memcpy(&dv, &u, 8);
                    if (!std::isfinite(dv)) return false;
                    outVal = dv;
                    return true;
                }
                return false;
            }
            if (dType > 3u) return false;

            uint64_t uval = 0;
            const bool isBE = (dType == 1u || dType == 3u);
            if (!isBE) {
                if (bitOff == 0u && bitCnt == 8u)  uval = p[0];
                else if (bitOff == 0u && bitCnt == 16u) uval = DetailEda200::RdU16LE(p);
                else if (bitOff == 0u && bitCnt == 32u) uval = DetailEda200::RdU32LE(p);
                else if (bitOff == 0u && bitCnt == 64u) uval = DetailEda200::RdU64LE(p);
                else {
                    for (uint32_t b = 0; b < bitCnt; ++b) {
                        uint32_t totBit = static_cast<uint32_t>(bitOff) + b;
                        uint8_t bv = p[totBit >> 3];
                        if ((bv >> (totBit & 7u)) & 1u) {
                            uval |= (1ULL << b);
                        }
                    }
                }
            } else {
                size_t nb = std::min<size_t>(needBytes, 8u);
                uint64_t acc = 0;
                for (size_t k = 0; k < nb; ++k) {
                    acc = (acc << 8) | static_cast<uint64_t>(p[k]);
                }
                uint64_t mask = (bitCnt == 64u) ? ~0ULL : ((1ULL << bitCnt) - 1ULL);
                uval = (acc >> bitOff) & mask;
            }

            if (dType == 2u || dType == 3u) {
                if (bitCnt == 64u) {
                    outVal = static_cast<double>(static_cast<int64_t>(uval));
                } else {
                    uint64_t signBit = 1ULL << (bitCnt - 1u);
                    int64_t sval = (uval & signBit)
                        ? static_cast<int64_t>(uval | (~0ULL << bitCnt))
                        : static_cast<int64_t>(uval);
                    outVal = static_cast<double>(sval);
                }
            } else {
                outVal = static_cast<double>(uval);
            }
            return std::isfinite(outVal);
        };

        std::vector<MdfDecodedCh> channels;
        const bool isMdf4 = (size >= 88 && std::memcmp(data + 64, "##HD", 4) == 0);
        const bool isMdf3 = (!isMdf4 && idVerNum < 400 && size >= 84 && std::memcmp(data + 64, "HD", 2) == 0);
        if (!isMdf4 && !isMdf3) return false;

        if (isMdf4) {
            struct Mdf4Blk {
                char id[4] = {};
                uint64_t dataOff = 0;
                uint64_t dataLen = 0;
                std::vector<uint64_t> links;
            };
            auto rdMdf4Blk = [&](uint64_t off, Mdf4Blk& b) -> bool {
                if (off < 64ULL || off + 24ULL > size) return false;
                const size_t o = static_cast<size_t>(off);
                if (data[o] != '#' || data[o + 1] != '#') return false;
                std::memcpy(b.id, data + o, 4);
                uint64_t blkLen = DetailEda200::RdU64LE(data + o + 8);
                uint64_t lnkCnt = DetailEda200::RdU64LE(data + o + 16);
                if (lnkCnt > 65536ULL) return false;
                uint64_t hdrBytes = 24ULL + lnkCnt * 8ULL;
                if (blkLen < hdrBytes || off + blkLen > size) return false;
                b.links.resize(static_cast<size_t>(lnkCnt));
                for (size_t i = 0; i < b.links.size(); ++i) {
                    b.links[i] = DetailEda200::RdU64LE(data + o + 24 + i * 8);
                }
                b.dataOff = off + hdrBytes;
                b.dataLen = blkLen - hdrBytes;
                return true;
            };

            auto rdMdf4F64 = [&](size_t off) -> double {
                uint64_t u = DetailEda200::RdU64LE(data + off);
                double dv = 0.0;
                std::memcpy(&dv, &u, 8);
                return dv;
            };

            auto rdMdf4Text = [&](uint64_t off) -> std::string {
                Mdf4Blk tb;
                if (!rdMdf4Blk(off, tb) || tb.dataLen == 0) return {};
                const char* p = reinterpret_cast<const char*>(data + static_cast<size_t>(tb.dataOff));
                size_t sl = strnlen(p, static_cast<size_t>(std::min<uint64_t>(tb.dataLen, 4096ULL)));
                std::string raw(p, sl);
                if (std::memcmp(tb.id, "##MD", 4) == 0) {
                    size_t tx0 = raw.find("<TX>");
                    size_t tx1 = (tx0 != std::string::npos) ? raw.find("</TX>", tx0 + 4) : std::string::npos;
                    if (tx0 != std::string::npos && tx1 != std::string::npos) {
                        raw = raw.substr(tx0 + 4, tx1 - (tx0 + 4));
                    } else {
                        std::string stripped;
                        bool inTag = false;
                        for (char c : raw) {
                            if (c == '<') inTag = true;
                            else if (c == '>') inTag = false;
                            else if (!inTag) stripped.push_back(c);
                        }
                        raw = std::move(stripped);
                    }
                }
                while (!raw.empty() && static_cast<unsigned char>(raw.front()) <= 0x20u) raw.erase(raw.begin());
                while (!raw.empty() && static_cast<unsigned char>(raw.back()) <= 0x20u) raw.pop_back();
                return raw;
            };

            auto rdMdf4Conv = [&](uint64_t off) -> MdfConv {
                MdfConv cv;
                Mdf4Blk cb;
                if (!rdMdf4Blk(off, cb) || std::memcmp(cb.id, "##CC", 4) != 0 || cb.dataLen < 8) return cv;
                if (cb.links.size() > 1 && cb.links[1] != 0) {
                    cv.unit = rdMdf4Text(cb.links[1]);
                }
                const size_t doff = static_cast<size_t>(cb.dataOff);
                cv.type = data[doff + 0];
                uint16_t valCnt = DetailEda200::RdU16LE(data + doff + 6);
                size_t paramOff = 0;
                if (cb.dataLen >= 24ULL + static_cast<uint64_t>(valCnt) * 8ULL) {
                    paramOff = doff + 24;
                } else if (cb.dataLen >= 8ULL + static_cast<uint64_t>(valCnt) * 8ULL) {
                    paramOff = doff + 8;
                }
                if (paramOff != 0 && valCnt <= 32u) {
                    cv.p.resize(valCnt);
                    for (uint16_t i = 0; i < valCnt; ++i) {
                        cv.p[i] = rdMdf4F64(paramOff + static_cast<size_t>(i) * 8u);
                    }
                }
                return cv;
            };

            std::set<uint64_t> visitedDataBlks;
            auto appendMdf4DataStream = [&](auto& self, uint64_t off, std::vector<uint8_t>& outStream) -> void {
                static constexpr size_t kMaxDataStream = 16 * 1024 * 1024;
                if (off < 64ULL || outStream.size() >= kMaxDataStream || visitedDataBlks.count(off)) return;
                visitedDataBlks.insert(off);

                Mdf4Blk db;
                if (!rdMdf4Blk(off, db)) return;

                if (std::memcmp(db.id, "##DT", 4) == 0 || std::memcmp(db.id, "##RD", 4) == 0) {
                    size_t take = static_cast<size_t>(std::min<uint64_t>(db.dataLen, kMaxDataStream - outStream.size()));
                    const uint8_t* sp = data + static_cast<size_t>(db.dataOff);
                    outStream.insert(outStream.end(), sp, sp + take);
                } else if (std::memcmp(db.id, "##DZ", 4) == 0) {
                    if (db.dataLen < 24ULL) return;
                    const size_t doff = static_cast<size_t>(db.dataOff);
                    const uint8_t zipType = data[doff + 2];
                    const uint32_t zipParam = DetailEda200::RdU32LE(data + doff + 4);
                    const uint64_t orgLen = DetailEda200::RdU64LE(data + doff + 8);
                    const uint64_t cmpLen = DetailEda200::RdU64LE(data + doff + 16);
                    if (orgLen == 0ULL || orgLen > kMaxDataStream || 24ULL + cmpLen > db.dataLen) return;

                    const uint8_t* cmpPtr = data + doff + 24;
                    std::vector<uint8_t> inflated;
                    bool infOk = Explorer2::ZipDeflate::InflateZlib(cmpPtr, static_cast<size_t>(cmpLen), inflated, static_cast<size_t>(orgLen) + 64u);
                    if (!infOk) {
                        inflated.clear();
                        infOk = Explorer2::ZipDeflate::InflateRaw(cmpPtr, static_cast<size_t>(cmpLen), inflated, static_cast<size_t>(orgLen) + 64u);
                    }
                    if (!infOk || inflated.empty()) return;
                    if (inflated.size() > static_cast<size_t>(orgLen)) {
                        inflated.resize(static_cast<size_t>(orgLen));
                    }

                    if (zipType == 1u && zipParam > 1u && inflated.size() >= static_cast<size_t>(zipParam)) {
                        const size_t M = static_cast<size_t>(zipParam);
                        const size_t N = inflated.size() / M;
                        std::vector<uint8_t> untrans(inflated.size());
                        for (size_t r = 0; r < N; ++r) {
                            for (size_t c = 0; c < M; ++c) {
                                untrans[r * M + c] = inflated[c * N + r];
                            }
                        }
                        const size_t mainBytes = N * M;
                        if (mainBytes < inflated.size()) {
                            std::memcpy(untrans.data() + mainBytes, inflated.data() + mainBytes, inflated.size() - mainBytes);
                        }
                        size_t take = std::min<size_t>(untrans.size(), kMaxDataStream - outStream.size());
                        outStream.insert(outStream.end(), untrans.begin(), untrans.begin() + static_cast<ptrdiff_t>(take));
                    } else {
                        size_t take = std::min<size_t>(inflated.size(), kMaxDataStream - outStream.size());
                        outStream.insert(outStream.end(), inflated.begin(), inflated.begin() + static_cast<ptrdiff_t>(take));
                    }
                } else if (std::memcmp(db.id, "##DL", 4) == 0) {
                    for (size_t li = 1; li < db.links.size(); ++li) {
                        if (db.links[li] != 0ULL) self(self, db.links[li], outStream);
                    }
                    if (!db.links.empty() && db.links[0] != 0ULL) {
                        self(self, db.links[0], outStream);
                    }
                } else if (std::memcmp(db.id, "##HL", 4) == 0) {
                    if (!db.links.empty() && db.links[0] != 0ULL) {
                        self(self, db.links[0], outStream);
                    }
                }
            };

            Mdf4Blk hd;
            if (!rdMdf4Blk(64ULL, hd) || std::memcmp(hd.id, "##HD", 4) != 0 || hd.links.empty()) return false;

            std::set<uint64_t> visitedDG;
            uint64_t dgOff = hd.links[0];
            while (dgOff != 0ULL && !visitedDG.count(dgOff) && channels.size() < 12u) {
                visitedDG.insert(dgOff);
                Mdf4Blk dg;
                if (!rdMdf4Blk(dgOff, dg) || std::memcmp(dg.id, "##DG", 4) != 0) break;
                const uint64_t nextDG = (dg.links.size() > 0) ? dg.links[0] : 0ULL;
                const uint64_t firstCG = (dg.links.size() > 1) ? dg.links[1] : 0ULL;
                const uint64_t dataLink = (dg.links.size() > 2) ? dg.links[2] : 0ULL;
                const uint8_t recIdSize = (dg.dataLen >= 1ULL) ? data[static_cast<size_t>(dg.dataOff)] : 0u;

                std::vector<uint8_t> rawStream;
                visitedDataBlks.clear();
                if (dataLink != 0ULL) {
                    appendMdf4DataStream(appendMdf4DataStream, dataLink, rawStream);
                }

                struct Mdf4ChSpec {
                    std::string name;
                    std::string unit;
                    uint8_t cnType = 0;
                    uint8_t dataType = 0;
                    uint8_t bitOff = 0;
                    uint32_t byteOff = 0;
                    uint32_t bitCnt = 0;
                    MdfConv conv;
                };
                struct Mdf4CgSpec {
                    uint64_t recId = 0;
                    uint64_t cycleCnt = 0;
                    uint32_t dataBytes = 0;
                    uint32_t invalBytes = 0;
                    std::vector<Mdf4ChSpec> chs;
                    int masterIdx = -1;
                };
                std::vector<Mdf4CgSpec> cgs;

                std::set<uint64_t> visitedCG;
                uint64_t cgOff = firstCG;
                while (cgOff != 0ULL && !visitedCG.count(cgOff) && cgs.size() < 16u) {
                    visitedCG.insert(cgOff);
                    Mdf4Blk cg;
                    if (!rdMdf4Blk(cgOff, cg) || std::memcmp(cg.id, "##CG", 4) != 0 || cg.dataLen < 24ULL) break;
                    const size_t cdo = static_cast<size_t>(cg.dataOff);

                    Mdf4CgSpec cgSpec;
                    cgSpec.recId = DetailEda200::RdU64LE(data + cdo + 0);
                    cgSpec.cycleCnt = DetailEda200::RdU64LE(data + cdo + 8);
                    if (cg.dataLen >= 32ULL) {
                        cgSpec.dataBytes  = DetailEda200::RdU32LE(data + cdo + 24);
                        cgSpec.invalBytes = DetailEda200::RdU32LE(data + cdo + 28);
                    } else {
                        cgSpec.dataBytes  = DetailEda200::RdU32LE(data + cdo + 16);
                        cgSpec.invalBytes = DetailEda200::RdU32LE(data + cdo + 20);
                    }

                    std::set<uint64_t> visitedCN;
                    uint64_t cnOff = (cg.links.size() > 1) ? cg.links[1] : 0ULL;
                    while (cnOff != 0ULL && !visitedCN.count(cnOff) && cgSpec.chs.size() < 32u) {
                        visitedCN.insert(cnOff);
                        Mdf4Blk cn;
                        if (!rdMdf4Blk(cnOff, cn) || std::memcmp(cn.id, "##CN", 4) != 0 || cn.dataLen < 12ULL) break;
                        const size_t ndo = static_cast<size_t>(cn.dataOff);

                        Mdf4ChSpec cs;
                        cs.cnType   = data[ndo + 0];
                        cs.dataType = data[ndo + 2];
                        cs.bitOff   = data[ndo + 3];
                        cs.byteOff  = DetailEda200::RdU32LE(data + ndo + 4);
                        cs.bitCnt   = DetailEda200::RdU32LE(data + ndo + 8);
                        if (cn.links.size() > 2 && cn.links[2] != 0ULL) cs.name = rdMdf4Text(cn.links[2]);
                        if (cn.links.size() > 4 && cn.links[4] != 0ULL) cs.conv = rdMdf4Conv(cn.links[4]);
                        if (cn.links.size() > 6 && cn.links[6] != 0ULL && cs.conv.unit.empty()) cs.unit = rdMdf4Text(cn.links[6]);
                        else cs.unit = cs.conv.unit;

                        if (cs.name.empty()) {
                            cs.name = ((cs.cnType == 2u || cs.cnType == 3u) ? "time" : ("ch_" + std::to_string(cgSpec.chs.size())));
                        }
                        if ((cs.cnType == 2u || cs.cnType == 3u) && cgSpec.masterIdx < 0) {
                            cgSpec.masterIdx = static_cast<int>(cgSpec.chs.size());
                        }
                        cgSpec.chs.push_back(std::move(cs));
                        cnOff = !cn.links.empty() ? cn.links[0] : 0ULL;
                    }

                    cgs.push_back(std::move(cgSpec));
                    cgOff = !cg.links.empty() ? cg.links[0] : 0ULL;
                }

                if (!rawStream.empty() && !cgs.empty()) {
                    for (const auto& cgSpec : cgs) {
                        if (channels.size() >= 12u) break;
                        const size_t recPayBytes = static_cast<size_t>(cgSpec.dataBytes) + static_cast<size_t>(cgSpec.invalBytes);
                        if (recPayBytes == 0u || cgSpec.cycleCnt == 0ULL) continue;

                        // Collect matching record pointers (sorted recIdSize==0 or unsorted recIdSize in {1,2,4,8})
                        std::vector<const uint8_t*> recPtrs;
                        if (recIdSize == 0u) {
                            size_t availRecs = rawStream.size() / recPayBytes;
                            size_t nRecs = static_cast<size_t>(std::min<uint64_t>(cgSpec.cycleCnt, availRecs));
                            recPtrs.reserve(std::min<size_t>(nRecs, 65536u));
                            for (size_t r = 0; r < nRecs && recPtrs.size() < 65536u; ++r) {
                                recPtrs.push_back(rawStream.data() + r * recPayBytes);
                            }
                        } else if (recIdSize == 1u || recIdSize == 2u || recIdSize == 4u || recIdSize == 8u) {
                            std::unordered_map<uint64_t, size_t> idToPaySz;
                            for (const auto& ocg : cgs) {
                                idToPaySz[ocg.recId] = static_cast<size_t>(ocg.dataBytes) + static_cast<size_t>(ocg.invalBytes);
                            }
                            size_t rp = 0;
                            while (rp + recIdSize <= rawStream.size() && recPtrs.size() < 65536u) {
                                uint64_t rid = 0;
                                if (recIdSize == 1u) rid = rawStream[rp];
                                else if (recIdSize == 2u) rid = DetailEda200::RdU16LE(rawStream.data() + rp);
                                else if (recIdSize == 4u) rid = DetailEda200::RdU32LE(rawStream.data() + rp);
                                else rid = DetailEda200::RdU64LE(rawStream.data() + rp);

                                auto szIt = idToPaySz.find(rid);
                                if (szIt == idToPaySz.end() || szIt->second == 0u || rp + recIdSize + szIt->second > rawStream.size()) break;
                                if (rid == cgSpec.recId) {
                                    recPtrs.push_back(rawStream.data() + rp + recIdSize);
                                }
                                rp += recIdSize + szIt->second;
                            }
                        }

                        if (recPtrs.empty()) continue;
                        const size_t totalRecs = recPtrs.size();
                        const size_t strideStep = std::max<size_t>(1u, totalRecs / 512u);

                        for (size_t ci = 0; ci < cgSpec.chs.size() && channels.size() < 12u; ++ci) {
                            const auto& cs = cgSpec.chs[ci];
                            if (static_cast<int>(ci) == cgSpec.masterIdx) continue;
                            if (cs.cnType != 0u && cs.cnType != 4u && cs.cnType != 6u) continue;

                            MdfDecodedCh dch;
                            dch.name = PathUtils::Utf8ToWide(cs.name);
                            dch.unit = PathUtils::Utf8ToWide(cs.unit);
                            dch.totalRecords = totalRecs;

                            for (size_t r = 0; r < totalRecs; ++r) {
                                double rawY = 0.0;
                                if (cs.cnType == 6u && cs.bitCnt == 0u) {
                                    rawY = static_cast<double>(r);
                                } else if (!extractRawSample(recPtrs[r], cgSpec.dataBytes, cs.byteOff, cs.bitOff, cs.bitCnt, cs.dataType, rawY)) {
                                    continue;
                                }
                                double phyY = cs.conv.Apply(rawY);
                                if (!std::isfinite(phyY)) continue;
                                dch.yMin = std::min(dch.yMin, phyY);
                                dch.yMax = std::max(dch.yMax, phyY);

                                if ((r % strideStep) == 0u || r + 1u == totalRecs) {
                                    double phyT = static_cast<double>(r);
                                    if (cgSpec.masterIdx >= 0) {
                                        const auto& mc = cgSpec.chs[static_cast<size_t>(cgSpec.masterIdx)];
                                        double rawT = static_cast<double>(r);
                                        if (mc.cnType == 3u) {
                                            phyT = mc.conv.Apply(rawT);
                                        } else if (extractRawSample(recPtrs[r], cgSpec.dataBytes, mc.byteOff, mc.bitOff, mc.bitCnt, mc.dataType, rawT)) {
                                            phyT = mc.conv.Apply(rawT);
                                        }
                                    }
                                    if (!std::isfinite(phyT)) phyT = static_cast<double>(r);
                                    dch.t.push_back(phyT);
                                    dch.y.push_back(phyY);
                                }
                            }

                            if (!dch.y.empty()) {
                                channels.push_back(std::move(dch));
                            }
                        }
                    }
                }
                dgOff = nextDG;
            }
        } else {
            // ASAM MDF 3.x ("HD" / "DG" / "CG" / "CN" / "CC" / "TX")
            const bool be = (DetailEda200::RdU16LE(data + 24) != 0u);
            auto rd16 = [&](size_t o) -> uint16_t {
                return be ? DetailEda200::RdU16BE(data + o) : DetailEda200::RdU16LE(data + o);
            };
            auto rd32 = [&](size_t o) -> uint32_t {
                return be ? DetailEda200::RdU32BE(data + o) : DetailEda200::RdU32LE(data + o);
            };
            auto rdF64 = [&](size_t o) -> double {
                uint64_t u = be
                    ? ((static_cast<uint64_t>(DetailEda200::RdU32BE(data + o)) << 32) |
                        static_cast<uint64_t>(DetailEda200::RdU32BE(data + o + 4)))
                    : DetailEda200::RdU64LE(data + o);
                double dv = 0.0;
                std::memcpy(&dv, &u, 8);
                return dv;
            };
            auto rdFixedStr = [&](size_t o, size_t maxN) -> std::string {
                if (o + maxN > size) return {};
                const char* p = reinterpret_cast<const char*>(data + o);
                size_t sl = strnlen(p, maxN);
                std::string s(p, sl);
                while (!s.empty() && static_cast<unsigned char>(s.back()) <= 0x20u) s.pop_back();
                return s;
            };
            auto rdMdf3Tx = [&](uint32_t off) -> std::string {
                if (off < 64u || static_cast<size_t>(off) + 4u > size) return {};
                if (data[off] != 'T' || data[off + 1] != 'X') return {};
                uint16_t bsz = rd16(off + 2);
                if (bsz <= 4u || static_cast<size_t>(off) + bsz > size) return {};
                return rdFixedStr(static_cast<size_t>(off) + 4u, static_cast<size_t>(bsz - 4u));
            };
            auto rdMdf3Cc = [&](uint32_t off) -> MdfConv {
                MdfConv cv;
                if (off < 64u || static_cast<size_t>(off) + 46u > size) return cv;
                if (data[off] != 'C' || data[off + 1] != 'C') return cv;
                uint16_t bsz = rd16(off + 2);
                if (bsz < 46u || static_cast<size_t>(off) + bsz > size) return cv;
                cv.unit = rdFixedStr(static_cast<size_t>(off) + 22u, 20u);
                uint16_t ccType = rd16(static_cast<size_t>(off) + 42u);
                uint16_t nPar   = rd16(static_cast<size_t>(off) + 44u);
                if (ccType == 0u && nPar >= 2u && static_cast<size_t>(off) + 46u + nPar * 8u <= size) {
                    cv.type = 1u;
                    cv.p = { rdF64(static_cast<size_t>(off) + 46u), rdF64(static_cast<size_t>(off) + 54u) };
                } else if (ccType == 9u && nPar >= 6u && static_cast<size_t>(off) + 46u + nPar * 8u <= size) {
                    cv.type = 2u;
                    for (uint16_t i = 0; i < 6u; ++i) cv.p.push_back(rdF64(static_cast<size_t>(off) + 46u + i * 8u));
                }
                return cv;
            };
            auto mapMdf3Type = [&](uint16_t sdt) -> uint8_t {
                switch (sdt) {
                case 0u:  return be ? 1u : 0u; // UINT default
                case 1u:  return be ? 3u : 2u; // SINT default
                case 2u:
                case 3u:  return be ? 5u : 4u; // FLOAT32/64 default
                case 9u:  return 1u;           // UINT_BE
                case 10u: return 3u;           // SINT_BE
                case 11u:
                case 12u: return 5u;           // FLOAT_BE
                case 13u: return 0u;           // UINT_LE
                case 14u: return 2u;           // SINT_LE
                case 15u:
                case 16u: return 4u;           // FLOAT_LE
                default:  return 255u;
                }
            };

            uint16_t hdSize = rd16(66);
            if (hdSize < 20u || 64u + static_cast<size_t>(hdSize) > size) return false;
            uint32_t dgOff = rd32(68);

            std::set<uint32_t> visitedDG;
            while (dgOff >= 64u && static_cast<size_t>(dgOff) + 24u <= size && !visitedDG.count(dgOff) && channels.size() < 12u) {
                visitedDG.insert(dgOff);
                if (data[dgOff] != 'D' || data[dgOff + 1] != 'G') break;
                uint16_t dgSize = rd16(dgOff + 2);
                if (dgSize < 24u || static_cast<size_t>(dgOff) + dgSize > size) break;

                uint32_t nextDG    = rd32(dgOff + 4);
                uint32_t firstCG   = rd32(dgOff + 8);
                uint32_t dataOff   = rd32(dgOff + 16);
                uint16_t numRecIds = rd16(dgOff + 22);

                struct Mdf3ChSpec {
                    std::string name;
                    uint16_t cnType = 0; // 0=data, 1=time
                    uint32_t byteOff = 0;
                    uint8_t bitOff = 0;
                    uint32_t bitCnt = 0;
                    uint8_t dataType = 0;
                    MdfConv conv;
                };
                struct Mdf3CgSpec {
                    uint16_t recId = 0;
                    uint16_t recBytes = 0;
                    uint32_t numRecs = 0;
                    std::vector<Mdf3ChSpec> chs;
                    int masterIdx = -1;
                };
                std::vector<Mdf3CgSpec> cgs;

                std::set<uint32_t> visitedCG;
                uint32_t cgOff = firstCG;
                while (cgOff >= 64u && static_cast<size_t>(cgOff) + 26u <= size && !visitedCG.count(cgOff) && cgs.size() < 16u) {
                    visitedCG.insert(cgOff);
                    if (data[cgOff] != 'C' || data[cgOff + 1] != 'G') break;
                    uint16_t cgSize = rd16(cgOff + 2);
                    if (cgSize < 26u || static_cast<size_t>(cgOff) + cgSize > size) break;

                    Mdf3CgSpec cgSpec;
                    uint32_t nextCG  = rd32(cgOff + 4);
                    uint32_t firstCN = rd32(cgOff + 8);
                    cgSpec.recId     = rd16(cgOff + 16);
                    cgSpec.recBytes  = rd16(cgOff + 20);
                    cgSpec.numRecs   = rd32(cgOff + 22);

                    std::set<uint32_t> visitedCN;
                    uint32_t cnOff = firstCN;
                    while (cnOff >= 64u && static_cast<size_t>(cnOff) + 216u <= size && !visitedCN.count(cnOff) && cgSpec.chs.size() < 32u) {
                        visitedCN.insert(cnOff);
                        if (data[cnOff] != 'C' || data[cnOff + 1] != 'N') break;
                        uint16_t cnSize = rd16(cnOff + 2);
                        if (cnSize < 216u || static_cast<size_t>(cnOff) + cnSize > size) break;

                        uint32_t nextCN = rd32(cnOff + 4);
                        uint32_t convCC = rd32(cnOff + 8);

                        // Standard ASAM MDF 3.x layout (5 links: +4..+23; cnType at +24, shortName at +26, startBit at +186, numBits at +188, sigType at +190)
                        uint16_t cnType   = rd16(cnOff + 24);
                        std::string sName = rdFixedStr(static_cast<size_t>(cnOff) + 26u, 32u);
                        uint16_t startBit = rd16(cnOff + 186);
                        uint16_t numBits  = rd16(cnOff + 188);
                        uint16_t sigDType = rd16(cnOff + 190);
                        // Fallback if a compact synthetic CN omitted the 5th link
                        if ((numBits == 0u || numBits > 64u) && rd16(cnOff + 186) >= 1u && rd16(cnOff + 186) <= 64u) {
                            cnType   = rd16(cnOff + 22);
                            sName    = rdFixedStr(static_cast<size_t>(cnOff) + 24u, 32u);
                            startBit = rd16(cnOff + 184);
                            numBits  = rd16(cnOff + 186);
                            sigDType = rd16(cnOff + 188);
                        }
                        uint32_t addByteOff = (cnSize >= 228u) ? static_cast<uint32_t>(rd16(cnOff + 226)) : 0u;
                        if (cnSize >= 222u) {
                            uint32_t longTx = rd32(cnOff + 218);
                            std::string lName = rdMdf3Tx(longTx);
                            if (!lName.empty()) sName = std::move(lName);
                        }

                        uint8_t mappedDt = mapMdf3Type(sigDType);
                        if (mappedDt != 255u && numBits >= 1u && numBits <= 64u) {
                            Mdf3ChSpec cs;
                            cs.name = sName.empty() ? ("ch_" + std::to_string(cgSpec.chs.size())) : std::move(sName);
                            cs.cnType = cnType;
                            uint32_t totalBitOff = addByteOff * 8u + static_cast<uint32_t>(startBit);
                            cs.byteOff = totalBitOff / 8u;
                            cs.bitOff  = static_cast<uint8_t>(totalBitOff % 8u);
                            cs.bitCnt  = numBits;
                            cs.dataType = mappedDt;
                            if (convCC != 0u) cs.conv = rdMdf3Cc(convCC);

                            if (cs.cnType == 1u && cgSpec.masterIdx < 0) {
                                cgSpec.masterIdx = static_cast<int>(cgSpec.chs.size());
                            }
                            cgSpec.chs.push_back(std::move(cs));
                        }
                        cnOff = nextCN;
                    }

                    cgs.push_back(std::move(cgSpec));
                    cgOff = nextCG;
                }

                if (dataOff >= 64u && static_cast<size_t>(dataOff) < size && !cgs.empty()) {
                    const size_t streamAvail = size - static_cast<size_t>(dataOff);
                    const uint8_t* streamBase = data + dataOff;

                    for (const auto& cgSpec : cgs) {
                        if (channels.size() >= 12u) break;
                        if (cgSpec.recBytes == 0u || cgSpec.numRecs == 0u) continue;

                        std::vector<const uint8_t*> recPtrs;
                        if (numRecIds == 0u) {
                            size_t availRecs = streamAvail / cgSpec.recBytes;
                            size_t nRecs = std::min<size_t>(cgSpec.numRecs, availRecs);
                            recPtrs.reserve(std::min<size_t>(nRecs, 65536u));
                            for (size_t r = 0; r < nRecs && recPtrs.size() < 65536u; ++r) {
                                recPtrs.push_back(streamBase + r * cgSpec.recBytes);
                            }
                        } else if (numRecIds == 1u || numRecIds == 2u) {
                            std::unordered_map<uint16_t, size_t> idSz;
                            for (const auto& ocg : cgs) idSz[ocg.recId] = ocg.recBytes;
                            size_t suffix = (numRecIds == 2u) ? 1u : 0u;
                            size_t rp = 0;
                            while (rp + 1u <= streamAvail && recPtrs.size() < 65536u) {
                                uint16_t rid = streamBase[rp];
                                auto it = idSz.find(rid);
                                if (it == idSz.end() || it->second == 0u || rp + 1u + it->second + suffix > streamAvail) break;
                                if (rid == cgSpec.recId) recPtrs.push_back(streamBase + rp + 1u);
                                rp += 1u + it->second + suffix;
                            }
                        }

                        if (recPtrs.empty()) continue;
                        const size_t totalRecs = recPtrs.size();
                        const size_t strideStep = std::max<size_t>(1u, totalRecs / 512u);

                        for (size_t ci = 0; ci < cgSpec.chs.size() && channels.size() < 12u; ++ci) {
                            const auto& cs = cgSpec.chs[ci];
                            if (static_cast<int>(ci) == cgSpec.masterIdx || cs.cnType == 1u) continue;

                            MdfDecodedCh dch;
                            dch.name = PathUtils::Utf8ToWide(cs.name);
                            dch.unit = PathUtils::Utf8ToWide(cs.conv.unit);
                            dch.totalRecords = totalRecs;

                            for (size_t r = 0; r < totalRecs; ++r) {
                                double rawY = 0.0;
                                if (!extractRawSample(recPtrs[r], cgSpec.recBytes, cs.byteOff, cs.bitOff, cs.bitCnt, cs.dataType, rawY)) {
                                    continue;
                                }
                                double phyY = cs.conv.Apply(rawY);
                                if (!std::isfinite(phyY)) continue;
                                dch.yMin = std::min(dch.yMin, phyY);
                                dch.yMax = std::max(dch.yMax, phyY);

                                if ((r % strideStep) == 0u || r + 1u == totalRecs) {
                                    double phyT = static_cast<double>(r);
                                    if (cgSpec.masterIdx >= 0) {
                                        const auto& mc = cgSpec.chs[static_cast<size_t>(cgSpec.masterIdx)];
                                        double rawT = 0.0;
                                        if (extractRawSample(recPtrs[r], cgSpec.recBytes, mc.byteOff, mc.bitOff, mc.bitCnt, mc.dataType, rawT)) {
                                            phyT = mc.conv.Apply(rawT);
                                        }
                                    }
                                    if (!std::isfinite(phyT)) phyT = static_cast<double>(r);
                                    dch.t.push_back(phyT);
                                    dch.y.push_back(phyY);
                                }
                            }

                            if (!dch.y.empty()) {
                                channels.push_back(std::move(dch));
                            }
                        }
                    }
                }
                dgOff = nextDG;
            }
        }

        // Zero-Cheat Guarantee: reject files with zero decoded signal channels or zero data samples
        if (channels.empty()) return false;

        double globalTMin =  1e300;
        double globalTMax = -1e300;
        size_t totalPtsCount = 0;
        for (const auto& ch : channels) {
            totalPtsCount += ch.totalRecords;
            for (double tv : ch.t) {
                globalTMin = std::min(globalTMin, tv);
                globalTMax = std::max(globalTMax, tv);
            }
        }
        if (totalPtsCount == 0u) return false;
        if (!std::isfinite(globalTMin) || !std::isfinite(globalTMax)) {
            globalTMin = 0.0;
            globalTMax = 1.0;
        }
        double globalTSpan = (globalTMax > globalTMin + 1e-12) ? (globalTMax - globalTMin) : 1.0;

        out.kind = EdaDocKind::KiCadSch;
        out.netCount = channels.size();
        if (verStr.empty()) verStr = isMdf4 ? "4.10" : "3.30";
        out.formatTitle = L"ASAM MDF " + PathUtils::Utf8ToWide(verStr) + L" ECU Telemetry (" +
                          std::to_wstring(channels.size()) + L" ch, " + ext + L")";

        DetailEda200::DrawGraticule(out, -40.0f, -24.0f, 80.0f, 48.0f, 10, 8);

        wchar_t tSpanBuf[128] = {};
        swprintf_s(tSpanBuf, L"ASAM MDF %hs — %zu ch | t = [%.3g .. %.3g s]",
            verStr.c_str(), channels.size(), globalTMin, globalTMax);
        out.labels.push_back({ -39.0f, 25.6f, 1.05f, EdaLayerId::SchLabel, tSpanBuf });

        const float laneStep = 44.0f / static_cast<float>(channels.size());
        const float halfAmp  = std::min(laneStep * 0.38f, 14.0f);
        static const EdaLayerId kLaneLayers[4] = {
            EdaLayerId::TopCopper,
            EdaLayerId::SchWire,
            EdaLayerId::InnerCopper,
            EdaLayerId::BottomCopper
        };

        for (size_t ci = 0; ci < channels.size(); ++ci) {
            const auto& ch = channels[ci];
            const float centerY = 22.0f - (static_cast<float>(ci) + 0.5f) * laneStep;
            const EdaLayerId lid = kLaneLayers[ci % 4u];
            const int netId = static_cast<int>(ci + 1u);

            std::wstring leftLbl = ch.name + (ch.unit.empty() ? L"" : (L" [" + ch.unit + L"]"));
            out.labels.push_back({ -39.2f, centerY + halfAmp + 0.7f, 0.92f, EdaLayerId::SchLabel, leftLbl });

            wchar_t statBuf[96] = {};
            swprintf_s(statBuf, L"[%.3g..%.3g%ls] (%zu pts)",
                ch.yMin, ch.yMax, ch.unit.empty() ? L"" : (L" " + ch.unit).c_str(), ch.totalRecords);
            out.labels.push_back({ 8.0f, centerY + halfAmp + 0.7f, 0.85f, EdaLayerId::SchLabel, statBuf });

            const double ySpan = ch.yMax - ch.yMin;
            auto mapX = [&](double tv) -> float {
                double u = std::clamp((tv - globalTMin) / globalTSpan, 0.0, 1.0);
                return -38.0f + static_cast<float>(u * 76.0);
            };
            auto mapY = [&](double yv) -> float {
                if (ySpan <= 1e-12) return centerY;
                double norm = std::clamp((yv - ch.yMin) / ySpan, 0.0, 1.0);
                return centerY + static_cast<float>(norm * 2.0 - 1.0) * halfAmp;
            };

            if (ch.y.size() == 1u) {
                float y0 = mapY(ch.y[0]);
                out.traces.push_back({ -38.0f, y0, 38.0f, y0, 0.30f, lid, netId });
            } else {
                for (size_t i = 1u; i < ch.y.size(); ++i) {
                    out.traces.push_back({
                        mapX(ch.t[i - 1u]), mapY(ch.y[i - 1u]),
                        mapX(ch.t[i]),      mapY(ch.y[i]),
                        0.32f, lid, netId
                    });
                }
            }

            EdaComponent3D comp;
            comp.refDes = ch.name;
            comp.value = statBuf;
            comp.footprint = isMdf4 ? L"MDF4_CN" : L"MDF3_CN";
            comp.x = -36.0f;
            comp.y = centerY;
            comp.widthMm = 6.0f;
            comp.lengthMm = std::max(2.0f, halfAmp * 1.5f);
            comp.pinCount = static_cast<int>(std::min<size_t>(ch.totalRecords, 65535u));
            out.components.push_back(std::move(comp));
        }

        return !out.traces.empty();
    }
    return false;
}

} // namespace Formats
