#pragma once
// WindowsMinidumpDmp.h — Windows Crash Minidump (.dmp / .mdmp — MDMP, SourceBytes >32 MB Safe)

namespace Formats {

inline bool ParseWindowsMinidump(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    SourceBytes src(raw, out);
    uint8_t hdr[32] = {};
    if (src.Size() < 32 || !DetailBinarySysDoc::ReadSourceExact(src, 0, hdr, 32) || std::memcmp(hdr, "MDMP", 4) != 0) return false;

    const uint64_t fileSize   = src.Size();
    const uint16_t ver        = DetailBinarySysDoc::RdU16LE(hdr + 4);
    const uint32_t numStreams = DetailBinarySysDoc::RdU32LE(hdr + 8);
    const uint32_t dirRva     = DetailBinarySysDoc::RdU32LE(hdr + 12);
    const uint32_t timeStamp  = DetailBinarySysDoc::RdU32LE(hdr + 20);
    const uint64_t flags      = DetailBinarySysDoc::RdU64LE(hdr + 24);

    SpreadsheetSheet dirSh;
    dirSh.name = L"Minidump_Streams";
    dirSh.maxCols = 2;
    dirSh.rows = {
        { L"Minidump Property", L"Value" },
        { L"Signature", L"MDMP v" + std::to_wstring(ver) },
        { L"Crash Timestamp", DetailBinarySysDoc::FormatUnixTimeUtc(timeStamp) },
        { L"Dump Flags", DetailBinarySysDoc::Hex64(flags) },
        { L"Directory Stream Count", std::to_wstring(numStreams) },
        { L"Dump File Size", PathUtils::FormatByteSize(fileSize) }
    };

    SpreadsheetSheet modSh;
    modSh.name = L"Minidump_Modules";
    modSh.maxCols = 3;
    modSh.rows.push_back({ L"Module Path", L"Base Address", L"Image Size" });

    SpreadsheetSheet thrSh;
    thrSh.name = L"Minidump_Threads";
    thrSh.maxCols = 6;
    thrSh.rows.push_back({ L"Thread ID", L"Suspend Count", L"Priority", L"TEB Address", L"Stack Base / Size", L"Instruction Pointer" });

    auto readMinidumpString = [&](uint32_t rva) -> std::wstring {
        uint8_t lenBuf[4] = {};
        if (rva == 0 || static_cast<uint64_t>(rva) + 4ULL > fileSize || !DetailBinarySysDoc::ReadSourceExact(src, rva, lenBuf, 4)) return {};
        const uint32_t byteLen = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(lenBuf), 2048u);
        std::vector<uint8_t> strBuf;
        if (byteLen == 0 || !DetailBinarySysDoc::ReadSourceVec(src, static_cast<uint64_t>(rva) + 4ULL, byteLen, strBuf)) return {};
        return DetailBinarySysDoc::ReadUtf16LEBounded(strBuf.data(), strBuf.size(), true);
    };

    uint16_t cpuArch = 0xFFFFu;
    if (dirRva > 0 && numStreams > 0 && numStreams <= 4096 &&
        static_cast<uint64_t>(dirRva) + static_cast<uint64_t>(numStreams) * 12ULL <= fileSize)
    {
        const uint32_t capStreams = std::min<uint32_t>(numStreams, 256u);
        std::vector<uint8_t> dirBuf;
        if (DetailBinarySysDoc::ReadSourceVec(src, dirRva, static_cast<size_t>(capStreams) * 12u, dirBuf)) {
            // Pre-scan SystemInfoStream (7) so CPU architecture is known before decoding ThreadContexts
            for (uint32_t i = 0; i < capStreams; ++i) {
                const uint32_t stype = DetailBinarySysDoc::RdU32LE(dirBuf.data() + i * 12u + 0);
                const uint32_t slen  = DetailBinarySysDoc::RdU32LE(dirBuf.data() + i * 12u + 4);
                const uint32_t srva  = DetailBinarySysDoc::RdU32LE(dirBuf.data() + i * 12u + 8);
                if (stype == 7 && slen >= 24 && static_cast<uint64_t>(srva) + 24ULL <= fileSize) {
                    uint8_t sysHdr[24] = {};
                    if (DetailBinarySysDoc::ReadSourceExact(src, srva, sysHdr, 24)) cpuArch = DetailBinarySysDoc::RdU16LE(sysHdr);
                }
            }

            for (uint32_t i = 0; i < capStreams; ++i) {
                const uint32_t stype = DetailBinarySysDoc::RdU32LE(dirBuf.data() + i * 12u + 0);
                const uint32_t slen  = DetailBinarySysDoc::RdU32LE(dirBuf.data() + i * 12u + 4);
                const uint32_t srva  = DetailBinarySysDoc::RdU32LE(dirBuf.data() + i * 12u + 8);

                std::wstring sname = L"Stream Type " + std::to_wstring(stype);
                if (stype == 3) sname = L"ThreadListStream (3)";
                else if (stype == 4) sname = L"ModuleListStream (4)";
                else if (stype == 5) sname = L"MemoryListStream (5)";
                else if (stype == 6) sname = L"ExceptionStream (6)";
                else if (stype == 7) sname = L"SystemInfoStream (7)";
                else if (stype == 8) sname = L"ThreadExListStream (8)";
                else if (stype == 9) sname = L"Memory64ListStream (9)";
                else if (stype == 11) sname = L"HandleDataStream (11)";
                else if (stype == 15) sname = L"MiscInfoStream (15)";
                else if (stype == 16) sname = L"MemoryInfoListStream (16)";
                else if (stype == 21) sname = L"SystemMemoryInfoStream (21)";

                dirSh.rows.push_back({ sname, PathUtils::FormatByteSize(slen) + L" @ " + DetailBinarySysDoc::Hex32(srva) });

                if (stype == 7 && slen >= 28 && static_cast<uint64_t>(srva) + 28ULL <= fileSize) {
                    // MINIDUMP_SYSTEM_INFO
                    uint8_t si[32] = {};
                    const size_t rd = std::min<size_t>(slen, 32u);
                    if (DetailBinarySysDoc::ReadSourceExact(src, srva, si, rd)) {
                        const uint16_t arch    = DetailBinarySysDoc::RdU16LE(si + 0);
                        const uint16_t level   = DetailBinarySysDoc::RdU16LE(si + 2);
                        const uint8_t  numCpus = si[6];
                        const uint8_t  prodTyp = si[7];
                        const uint32_t majVer  = DetailBinarySysDoc::RdU32LE(si + 8);
                        const uint32_t minVer  = DetailBinarySysDoc::RdU32LE(si + 12);
                        const uint32_t bldNum  = DetailBinarySysDoc::RdU32LE(si + 16);
                        const uint32_t csdRva  = DetailBinarySysDoc::RdU32LE(si + 24);

                        std::wstring archName = L"Arch " + std::to_wstring(arch);
                        if (arch == 9) archName = L"x64 / AMD64";
                        else if (arch == 12) archName = L"ARM64";
                        else if (arch == 0) archName = L"x86 / IA-32";
                        else if (arch == 5) archName = L"ARM";

                        std::wstring csd = readMinidumpString(csdRva);
                        std::wstring osDesc = std::to_wstring(majVer) + L"." + std::to_wstring(minVer) + L"." + std::to_wstring(bldNum);
                        if (prodTyp == 1) osDesc += L" (Workstation)";
                        else if (prodTyp == 3) osDesc += L" (Server)";
                        if (!csd.empty()) osDesc += L" " + csd;

                        dirSh.rows.push_back({ L"System CPU & Cores", archName + L" (Level " + std::to_wstring(level) + L", " + std::to_wstring(numCpus) + L" logical cores)" });
                        dirSh.rows.push_back({ L"Operating System Version", osDesc });
                    }
                } else if (stype == 15 && slen >= 24 && static_cast<uint64_t>(srva) + 24ULL <= fileSize) {
                    // MINIDUMP_MISC_INFO
                    uint8_t mi[24] = {};
                    if (DetailBinarySysDoc::ReadSourceExact(src, srva, mi, 24)) {
                        const uint32_t flags1     = DetailBinarySysDoc::RdU32LE(mi + 4);
                        const uint32_t pid        = DetailBinarySysDoc::RdU32LE(mi + 8);
                        const uint32_t createTime = DetailBinarySysDoc::RdU32LE(mi + 12);
                        const uint32_t userSec    = DetailBinarySysDoc::RdU32LE(mi + 16);
                        const uint32_t krnlSec    = DetailBinarySysDoc::RdU32LE(mi + 20);
                        if (flags1 & 0x01u) {
                            dirSh.rows.push_back({ L"Process ID (PID)", std::to_wstring(pid) });
                        }
                        if (flags1 & 0x02u) {
                            std::wstring tDesc = L"User=" + std::to_wstring(userSec) + L"s, Kernel=" + std::to_wstring(krnlSec) + L"s";
                            if (createTime > 0) {
                                tDesc += L", Started=" + DetailBinarySysDoc::FormatUnixTimeUtc(createTime);
                                if (timeStamp >= createTime) tDesc += L" (Uptime=" + std::to_wstring(timeStamp - createTime) + L"s)";
                            }
                            dirSh.rows.push_back({ L"Process Times", tDesc });
                        }
                    }
                } else if (stype == 6 && slen >= 32 && static_cast<uint64_t>(srva) + 32ULL <= fileSize) {
                    uint8_t ex[32] = {};
                    if (DetailBinarySysDoc::ReadSourceExact(src, srva, ex, 32)) {
                        const uint32_t excThread = DetailBinarySysDoc::RdU32LE(ex + 0);
                        const uint32_t excCode   = DetailBinarySysDoc::RdU32LE(ex + 8);
                        const uint64_t excAddr   = DetailBinarySysDoc::RdU64LE(ex + 24);
                        dirSh.rows.push_back({ L"Exception Code / Address",
                            DetailBinarySysDoc::Hex32(excCode) + L" @ " + DetailBinarySysDoc::Hex64(excAddr) + L" (TID " + std::to_wstring(excThread) + L")" });
                    }
                } else if (stype == 4 && slen >= 4 && static_cast<uint64_t>(srva) + 4ULL <= fileSize) {
                    uint8_t mcBuf[4] = {};
                    if (DetailBinarySysDoc::ReadSourceExact(src, srva, mcBuf, 4)) {
                        const uint32_t nMods = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(mcBuf), 512u);
                        const size_t modBytes = std::min<size_t>(static_cast<size_t>(nMods) * 108u, slen - 4u);
                        std::vector<uint8_t> mBuf;
                        if (modBytes >= 108 && DetailBinarySysDoc::ReadSourceVec(src, static_cast<uint64_t>(srva) + 4ULL, modBytes, mBuf)) {
                            for (size_t mp = 0; mp + 108 <= mBuf.size(); mp += 108) {
                                const uint64_t baseAddr = DetailBinarySysDoc::RdU64LE(mBuf.data() + mp + 0);
                                const uint32_t imgSz    = DetailBinarySysDoc::RdU32LE(mBuf.data() + mp + 8);
                                const uint32_t nameRva  = DetailBinarySysDoc::RdU32LE(mBuf.data() + mp + 20);
                                std::wstring mname = readMinidumpString(nameRva);
                                if (mname.empty()) mname = L"(module)";
                                modSh.rows.push_back({ std::move(mname), DetailBinarySysDoc::Hex64(baseAddr), PathUtils::FormatByteSize(imgSz) });
                            }
                        }
                    }
                } else if (stype == 3 && slen >= 4 && static_cast<uint64_t>(srva) + 4ULL <= fileSize) {
                    // MINIDUMP_THREAD_LIST: [u32 NumberOfThreads] + N * 48-byte MINIDUMP_THREAD
                    uint8_t tcBuf[4] = {};
                    if (DetailBinarySysDoc::ReadSourceExact(src, srva, tcBuf, 4)) {
                        const uint32_t nThreads = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(tcBuf), 512u);
                        const size_t thrBytes = std::min<size_t>(static_cast<size_t>(nThreads) * 48u, slen - 4u);
                        std::vector<uint8_t> tBuf;
                        if (thrBytes >= 48 && DetailBinarySysDoc::ReadSourceVec(src, static_cast<uint64_t>(srva) + 4ULL, thrBytes, tBuf)) {
                            for (size_t tp = 0; tp + 48 <= tBuf.size(); tp += 48) {
                                const uint32_t tid      = DetailBinarySysDoc::RdU32LE(tBuf.data() + tp + 0);
                                const uint32_t suspCnt  = DetailBinarySysDoc::RdU32LE(tBuf.data() + tp + 4);
                                const uint32_t prioCls  = DetailBinarySysDoc::RdU32LE(tBuf.data() + tp + 8);
                                const int32_t  prio     = static_cast<int32_t>(DetailBinarySysDoc::RdU32LE(tBuf.data() + tp + 12));
                                const uint64_t teb      = DetailBinarySysDoc::RdU64LE(tBuf.data() + tp + 16);
                                const uint64_t stkStart = DetailBinarySysDoc::RdU64LE(tBuf.data() + tp + 24);
                                const uint32_t stkSize  = DetailBinarySysDoc::RdU32LE(tBuf.data() + tp + 32);
                                const uint32_t ctxSize  = DetailBinarySysDoc::RdU32LE(tBuf.data() + tp + 40);
                                const uint32_t ctxRva   = DetailBinarySysDoc::RdU32LE(tBuf.data() + tp + 44);

                                std::wstring ipStr = L"-";
                                if (ctxRva > 0 && static_cast<uint64_t>(ctxRva) + ctxSize <= fileSize) {
                                    uint8_t ipBuf[8] = {};
                                    if ((cpuArch == 9 || (cpuArch == 0xFFFFu && ctxSize >= 1232)) && ctxSize >= 256 &&
                                        DetailBinarySysDoc::ReadSourceExact(src, static_cast<uint64_t>(ctxRva) + 248ULL, ipBuf, 8)) {
                                        ipStr = L"RIP=" + DetailBinarySysDoc::Hex64(DetailBinarySysDoc::RdU64LE(ipBuf));
                                    } else if (cpuArch == 12 && ctxSize >= 272 &&
                                               DetailBinarySysDoc::ReadSourceExact(src, static_cast<uint64_t>(ctxRva) + 264ULL, ipBuf, 8)) {
                                        ipStr = L"PC=" + DetailBinarySysDoc::Hex64(DetailBinarySysDoc::RdU64LE(ipBuf));
                                    } else if (cpuArch == 0 && ctxSize >= 188 &&
                                               DetailBinarySysDoc::ReadSourceExact(src, static_cast<uint64_t>(ctxRva) + 184ULL, ipBuf, 4)) {
                                        ipStr = L"EIP=" + DetailBinarySysDoc::Hex32(DetailBinarySysDoc::RdU32LE(ipBuf));
                                    }
                                }

                                thrSh.rows.push_back({
                                    std::to_wstring(tid),
                                    std::to_wstring(suspCnt),
                                    std::to_wstring(prio) + L" (Class " + DetailBinarySysDoc::Hex32(prioCls) + L")",
                                    DetailBinarySysDoc::Hex64(teb),
                                    DetailBinarySysDoc::Hex64(stkStart) + L" (" + PathUtils::FormatByteSize(stkSize) + L")",
                                    std::move(ipStr)
                                });
                            }
                        }
                    }
                } else if (stype == 16 && slen >= 16 && static_cast<uint64_t>(srva) + 16ULL <= fileSize) {
                    // MINIDUMP_MEMORY_INFO_LIST: [u32 SizeOfHeader][u32 SizeOfEntry][u64 NumberOfEntries]
                    uint8_t mih[16] = {};
                    if (DetailBinarySysDoc::ReadSourceExact(src, srva, mih, 16)) {
                        const uint32_t hdrSz = DetailBinarySysDoc::RdU32LE(mih + 0);
                        const uint32_t entSz = DetailBinarySysDoc::RdU32LE(mih + 4);
                        const uint64_t nEnts = DetailBinarySysDoc::RdU64LE(mih + 8);
                        if (hdrSz >= 16 && entSz >= 48 && nEnts > 0 && slen > hdrSz) {
                            const size_t scanCnt = static_cast<size_t>(std::min<uint64_t>(nEnts, 4096ULL));
                            std::vector<uint8_t> miEntries;
                            uint64_t committedBytes = 0;
                            size_t execRegions = 0;
                            if (DetailBinarySysDoc::ReadSourceVec(src, static_cast<uint64_t>(srva) + hdrSz, std::min<size_t>(scanCnt * entSz, slen - hdrSz), miEntries)) {
                                for (size_t ep = 0; ep + 48 <= miEntries.size(); ep += entSz) {
                                    const uint64_t regSize = DetailBinarySysDoc::RdU64LE(miEntries.data() + ep + 24);
                                    const uint32_t state   = DetailBinarySysDoc::RdU32LE(miEntries.data() + ep + 32);
                                    const uint32_t protect = DetailBinarySysDoc::RdU32LE(miEntries.data() + ep + 36);
                                    if (state == 0x1000u) committedBytes += regSize; // MEM_COMMIT
                                    if (protect & 0xF0u) ++execRegions;              // PAGE_EXECUTE*
                                }
                            }
                            dirSh.rows.push_back({
                                L"Virtual Memory Summary",
                                std::to_wstring(nEnts) + L" regions, Committed=" + PathUtils::FormatByteSize(committedBytes) +
                                L", Executable=" + std::to_wstring(execRegions)
                            });
                        }
                    }
                }
            }
        }
    }

    if (!src.ReadFailed()) out.sourceReadFully = true;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Crash Minidump (.dmp)";
    out.sheets.push_back(std::move(dirSh));
    if (thrSh.rows.size() > 1) out.sheets.push_back(std::move(thrSh));
    if (modSh.rows.size() > 1) out.sheets.push_back(std::move(modSh));
    out.valid = true;
    return true;
}

} // namespace Formats
