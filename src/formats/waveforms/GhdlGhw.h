#pragma once
// GhdlGhw.h — GHDL VHDL Simulation Waveform (.ghw) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailHwBin {

// Authentic GHDL Waveform (.ghw) Reader
inline bool ParseGhdlGhw(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 24 || std::memcmp(data, "GHDLwave\n", 9) != 0) return false;
    uint8_t hdrLen = data[9];
    uint8_t endian = data[12]; // 1 = LE, 2 = BE
    if (hdrLen < 12 || hdrLen > size || (endian != 1 && endian != 2)) return false;
    const bool be = (endian == 2);

    auto rd32 = [&](const uint8_t* p) -> uint32_t {
        return be ? DetailEda100::RdU32BE(p) : DetailEda100::RdU32LE(p);
    };
    auto rd64 = [&](const uint8_t* p) -> int64_t {
        return static_cast<int64_t>(be ? DetailEda100::RdU64BE(p) : DetailEda100::RdU64LE(p));
    };

    std::vector<std::string> strTable(1); // 1-based
    enum class GhwScalarKind : uint8_t { Enum1B = 0, IntLeb = 1, Float64 = 2 };
    struct GhwTypeDef {
        GhwScalarKind kind = GhwScalarKind::Enum1B;
        std::vector<char> enumChars; // e.g. {'U','X','0','1','Z','W','L','H','-'}
        uint32_t elemCount = 1;
        uint32_t elemTypeId = 0; // 1-based for arrays
    };
    std::vector<GhwTypeDef> types(1); // 1-based

    struct GhwBasicSig {
        uint32_t typeId = 1;
        std::vector<std::pair<uint64_t, char>> changes;
    };
    std::vector<GhwBasicSig> basicSigs; // 1-based

    struct GhwSignalDecl {
        std::wstring name;
        std::vector<uint32_t> elIds; // 1-based basic signal IDs
    };
    std::vector<GhwSignalDecl> declSignals;

    uint64_t minTimeFs = 0, maxTimeFs = 0;
    size_t pos = hdrLen;

    auto findMarker = [&](const char marker[4], size_t startOff) -> size_t {
        for (size_t i = startOff; i + 4 <= size; ++i) {
            if (std::memcmp(data + i, marker, 4) == 0) return i;
        }
        return std::string_view::npos;
    };

    while (pos + 4 <= size) {
        const uint8_t* sec = data + pos;
        if (std::memcmp(sec, "TAI\0", 4) == 0 || std::memcmp(sec, "DIR\0", 4) == 0) {
            pos += 4;
            if (std::memcmp(sec, "DIR\0", 4) == 0) {
                size_t eod = findMarker("EOD\0", pos);
                if (eod == std::string_view::npos) break;
                pos = eod + 4;
            } else {
                break;
            }
            continue;
        }

        if (std::memcmp(sec, "STR\0", 4) == 0) {
            if (pos + 16 > size) return false;
            uint32_t nbrStr = rd32(data + pos + 8);
            size_t strStart = pos + 16;
            size_t eosPos = findMarker("EOS\0", strStart);
            if (eosPos == std::string_view::npos || nbrStr > 200000) return false;

            strTable.assign(nbrStr + 1, std::string{});
            size_t sp = strStart;
            // Check whether string 1 is preceded by an explicit 0x00 prefix-len byte or starts with ASCII directly
            if (nbrStr > 0 && sp < eosPos && data[sp] == 0) {
                size_t ctrlRuns = 0;
                bool inCtrl = false;
                for (size_t k = sp; k < eosPos; ++k) {
                    bool isCtrl = (data[k] < 32 || (data[k] >= 128 && data[k] < 160));
                    if (isCtrl && !inCtrl) ++ctrlRuns;
                    inCtrl = isCtrl;
                }
                if (ctrlRuns >= nbrStr + 1) ++sp;
            }

            size_t prevLen = 0;
            for (uint32_t i = 1; i <= nbrStr && sp < eosPos; ++i) {
                std::string cur;
                if (prevLen > 0 && prevLen <= strTable[i - 1].size()) {
                    cur = strTable[i - 1].substr(0, prevLen);
                }
                while (sp < eosPos) {
                    uint8_t c = data[sp];
                    if (c < 32 || (c >= 128 && c < 160)) break;
                    cur.push_back(static_cast<char>(c));
                    ++sp;
                }
                strTable[i] = std::move(cur);
                // Decode prev_len for string i + 1
                prevLen = 0;
                int sh = 0;
                while (sp < eosPos) {
                    uint8_t c = data[sp++];
                    prevLen |= static_cast<size_t>(c & 0x1Fu) << sh;
                    sh += 5;
                    if (c < 128) break;
                }
            }
            pos = eosPos + 4;
            continue;
        }

        if (std::memcmp(sec, "TYP\0", 4) == 0) {
            if (pos + 12 > size) return false;
            uint32_t nbrTypes = rd32(data + pos + 8);
            size_t tp = pos + 12;
            size_t eosPos = findMarker("EOS\0", tp);
            if (eosPos == std::string_view::npos || nbrTypes > 100000) return false;
            types.assign(nbrTypes + 1, GhwTypeDef{});

            for (uint32_t ti = 1; ti <= nbrTypes && tp < eosPos; ++ti) {
                uint8_t rtik = data[tp++];
                GhwTypeDef td;
                auto litCharFromStrId = [&](uint64_t sid) -> char {
                    if (sid < strTable.size() && !strTable[sid].empty()) {
                        const std::string& s = strTable[sid];
                        if (s.size() >= 3 && s[0] == '\'') return s[1];
                        if (DetailEda100::ContainsAsciiI(s, "true")) return '1';
                        if (DetailEda100::ContainsAsciiI(s, "false")) return '0';
                        return s[0];
                    }
                    return '0';
                };
                if (rtik == 22) { // ghw_rtik_type_b2
                    DetailEda100::ReadULeb128(data, eosPos, tp); // name
                    uint64_t l0 = DetailEda100::ReadULeb128(data, eosPos, tp);
                    uint64_t l1 = DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.kind = GhwScalarKind::Enum1B;
                    td.enumChars = { litCharFromStrId(l0), litCharFromStrId(l1) };
                } else if (rtik == 23) { // ghw_rtik_type_e8
                    DetailEda100::ReadULeb128(data, eosPos, tp); // name
                    uint64_t nLits = DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.kind = GhwScalarKind::Enum1B;
                    for (uint64_t li = 0; li < nLits && li < 256 && tp < eosPos; ++li) {
                        uint64_t lsid = DetailEda100::ReadULeb128(data, eosPos, tp);
                        td.enumChars.push_back(litCharFromStrId(lsid));
                    }
                } else if (rtik == 25 || rtik == 26) { // i32 / i64
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.kind = GhwScalarKind::IntLeb;
                } else if (rtik == 27) { // f64
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.kind = GhwScalarKind::Float64;
                } else if (rtik == 28 || rtik == 29) { // p32 / p64
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    uint64_t nUnits = DetailEda100::ReadULeb128(data, eosPos, tp);
                    for (uint64_t ui = 0; ui < nUnits && tp < eosPos; ++ui) {
                        DetailEda100::ReadULeb128(data, eosPos, tp);
                        DetailEda100::ReadSLeb128(data, eosPos, tp);
                    }
                    td.kind = GhwScalarKind::IntLeb;
                } else if (rtik == 31) { // type_array
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.elemTypeId = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, tp));
                    uint64_t nDims = DetailEda100::ReadULeb128(data, eosPos, tp);
                    for (uint64_t di = 0; di < nDims && tp < eosPos; ++di) {
                        DetailEda100::ReadULeb128(data, eosPos, tp);
                    }
                } else if (rtik == 34) { // subtype_scalar
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    uint32_t baseId = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, tp));
                    if (baseId < ti) td = types[baseId];
                    if (tp < eosPos) {
                        uint8_t rkind = data[tp++] & 0x7Fu;
                        if (rkind == 22 || rkind == 23) tp += 2;
                        else if (rkind == 25 || rkind == 26 || rkind == 28 || rkind == 29) {
                            DetailEda100::ReadSLeb128(data, eosPos, tp);
                            DetailEda100::ReadSLeb128(data, eosPos, tp);
                        } else if (rkind == 27) tp += 16;
                    }
                } else if (rtik == 35) { // subtype_array (constrained 1D array e.g. std_logic_vector(7 downto 0))
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    uint32_t baseArr = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, tp));
                    if (baseArr < ti) {
                        td.elemTypeId = types[baseArr].elemTypeId;
                        if (td.elemTypeId < ti) td.kind = types[td.elemTypeId].kind;
                    }
                    if (tp < eosPos) {
                        uint8_t rkind = data[tp++];
                        bool desc = (rkind & 0x80u) != 0;
                        uint8_t rk = rkind & 0x7Fu;
                        int64_t left = 0, right = 0;
                        if (rk == 22 || rk == 23) {
                            if (tp + 2 <= eosPos) { left = data[tp]; right = data[tp + 1]; tp += 2; }
                        } else {
                            left  = DetailEda100::ReadSLeb128(data, eosPos, tp);
                            right = DetailEda100::ReadSLeb128(data, eosPos, tp);
                        }
                        int64_t len = desc ? (left - right + 1) : (right - left + 1);
                        td.elemCount = static_cast<uint32_t>(std::clamp<int64_t>(len, 1, 512));
                    }
                } else {
                    break;
                }
                types[ti] = std::move(td);
            }
            pos = eosPos + 4;
            continue;
        }

        if (std::memcmp(sec, "WKT\0", 4) == 0) {
            size_t eosPos = findMarker("EOS\0", pos + 8);
            if (eosPos == std::string_view::npos) break;
            pos = eosPos + 4;
            continue;
        }

        if (std::memcmp(sec, "HIE\0", 4) == 0) {
            if (pos + 20 > size) return false;
            uint32_t nbrBasicSigs = rd32(data + pos + 16);
            size_t hp = pos + 20;
            size_t eosPos = findMarker("EOS\0", hp);
            if (eosPos == std::string_view::npos || nbrBasicSigs > 200000) return false;

            basicSigs.assign(nbrBasicSigs + 1, GhwBasicSig{});
            std::vector<std::string> scopes;

            while (hp < eosPos) {
                uint8_t hkind = data[hp++];
                if (hkind == 0) break; // end of HIE
                if (hkind == 15) { // ghw_hie_eos (upscope)
                    if (!scopes.empty()) scopes.pop_back();
                } else if (hkind == 1 || hkind == 3 || hkind == 4 || hkind == 5 || hkind == 6 || hkind == 7 || hkind == 13) {
                    uint64_t sid = DetailEda100::ReadULeb128(data, eosPos, hp);
                    std::string sname = (sid < strTable.size()) ? strTable[sid] : "";
                    if (hkind == 5) { // generate_for
                        uint32_t itType = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, hp));
                        if (itType < types.size() && types[itType].kind == GhwScalarKind::Enum1B) ++hp;
                        else DetailEda100::ReadSLeb128(data, eosPos, hp);
                    }
                    if (hkind != 13) scopes.push_back(std::move(sname)); // process has no upscope
                } else if (hkind >= 16 && hkind <= 21) { // signal / port_*
                    uint64_t sid = DetailEda100::ReadULeb128(data, eosPos, hp);
                    uint32_t tid = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, hp));
                    std::string vname = (sid < strTable.size()) ? strTable[sid] : ("sig_" + std::to_string(declSignals.size()));
                    if (!scopes.empty() && !scopes.back().empty()) vname = scopes.back() + "." + vname;

                    uint32_t nElems = (tid < types.size()) ? types[tid].elemCount : 1u;
                    uint32_t scalarTid = (tid < types.size() && types[tid].elemTypeId != 0) ? types[tid].elemTypeId : tid;
                    GhwSignalDecl decl;
                    decl.name = PathUtils::Utf8ToWide(vname);
                    for (uint32_t ei = 0; ei < nElems && hp < eosPos; ++ei) {
                        uint32_t elId = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, hp));
                        if (elId >= 1 && elId < basicSigs.size()) {
                            basicSigs[elId].typeId = scalarTid;
                            decl.elIds.push_back(elId);
                        }
                    }
                    if (!decl.elIds.empty()) declSignals.push_back(std::move(decl));
                } else {
                    break;
                }
            }
            pos = eosPos + 4;
            continue;
        }

        auto readBasicSigChar = [&](uint32_t sigId, size_t& cur, size_t limit) -> char {
            uint32_t tid = (sigId < basicSigs.size()) ? basicSigs[sigId].typeId : 1u;
            const GhwTypeDef* td = (tid < types.size()) ? &types[tid] : nullptr;
            if (!td || td->kind == GhwScalarKind::Enum1B) {
                if (cur >= limit) return '0';
                uint8_t idx = data[cur++];
                if (td && idx < td->enumChars.size()) return td->enumChars[idx];
                return (idx == 3 || idx == 1) ? '1' : '0';
            }
            if (td->kind == GhwScalarKind::IntLeb) {
                int64_t v = DetailEda100::ReadSLeb128(data, limit, cur);
                return (v != 0) ? '1' : '0';
            }
            cur = std::min(limit, cur + 8);
            return '0';
        };

        if (std::memcmp(sec, "SNP\0", 4) == 0) {
            if (pos + 16 > size) break;
            int64_t snapFs = std::max<int64_t>(0, rd64(data + pos + 8));
            minTimeFs = static_cast<uint64_t>(snapFs);
            maxTimeFs = std::max(maxTimeFs, minTimeFs);
            size_t sp = pos + 16;
            size_t esnPos = findMarker("ESN\0", sp);
            size_t limit = (esnPos != std::string_view::npos) ? esnPos : size;
            for (size_t si = 1; si < basicSigs.size() && sp < limit; ++si) {
                char ch = readBasicSigChar(static_cast<uint32_t>(si), sp, limit);
                basicSigs[si].changes.push_back({ static_cast<uint64_t>(snapFs), ch });
            }
            if (esnPos == std::string_view::npos) break;
            pos = esnPos + 4;
            continue;
        }

        if (std::memcmp(sec, "CYC\0", 4) == 0) {
            if (pos + 12 > size) break;
            int64_t curFs = std::max<int64_t>(0, rd64(data + pos + 4));
            size_t cp = pos + 12;
            size_t ecyPos = findMarker("ECY\0", cp);
            size_t limit = (ecyPos != std::string_view::npos) ? ecyPos : size;

            while (cp < limit) {
                uint32_t sigIdx = 0;
                while (cp < limit) {
                    uint64_t deltaSig = DetailEda100::ReadULeb128(data, limit, cp);
                    if (deltaSig == 0) break;
                    sigIdx += static_cast<uint32_t>(deltaSig);
                    char ch = readBasicSigChar(sigIdx, cp, limit);
                    if (sigIdx >= 1 && sigIdx < basicSigs.size() && basicSigs[sigIdx].changes.size() < 8192) {
                        basicSigs[sigIdx].changes.push_back({ static_cast<uint64_t>(curFs), ch });
                    }
                }
                maxTimeFs = std::max(maxTimeFs, static_cast<uint64_t>(curFs));
                if (cp >= limit) break;
                int64_t dtFs = DetailEda100::ReadSLeb128(data, limit, cp);
                if (dtFs < 0) break; // -1 terminates CYC
                curFs += dtFs;
            }
            if (ecyPos == std::string_view::npos) break;
            pos = ecyPos + 4;
            continue;
        }

        break;
    }

    if (declSignals.empty()) return false;

    std::vector<DecodedWaveSignal> sigs;
    sigs.reserve(declSignals.size());
    for (const auto& d : declSignals) {
        DecodedWaveSignal ws;
        ws.name = d.name;
        ws.bitWidth = static_cast<int>(d.elIds.size());
        if (d.elIds.size() == 1) {
            for (const auto& kv : basicSigs[d.elIds[0]].changes) {
                ws.changes.push_back({ kv.first, std::string(1, kv.second) });
            }
        } else {
            // Merge multi-bit vector timestamps across scalar element signals
            std::set<uint64_t> allTimes;
            for (uint32_t eid : d.elIds) {
                for (const auto& kv : basicSigs[eid].changes) allTimes.insert(kv.first);
            }
            std::vector<size_t> cursors(d.elIds.size(), 0);
            for (uint64_t t : allTimes) {
                std::string bits(d.elIds.size(), '0');
                for (size_t bi = 0; bi < d.elIds.size(); ++bi) {
                    const auto& chVec = basicSigs[d.elIds[bi]].changes;
                    while (cursors[bi] + 1 < chVec.size() && chVec[cursors[bi] + 1].first <= t) ++cursors[bi];
                    if (!chVec.empty()) bits[bi] = chVec[cursors[bi]].second;
                }
                ws.changes.push_back({ t, std::move(bits) });
            }
        }
        sigs.push_back(std::move(ws));
    }

    wchar_t titleBuf[160] = {};
    swprintf_s(titleBuf, L"GHDL VHDL Waveform (.ghw — %zu signals, %.3f ns)",
        sigs.size(), static_cast<double>(maxTimeFs) * 1e-6);
    return RenderWaveSignalsToBoard(sigs, minTimeFs, maxTimeFs, titleBuf, out);
}

} // namespace DetailHwBin

inline bool ParseGhdlGhwBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
    return DetailHwBin::ParseGhdlGhw(data, size, out);
}

} // namespace Formats
