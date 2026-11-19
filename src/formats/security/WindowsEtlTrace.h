#pragma once
// WindowsEtlTrace.h — Windows Event Trace Log (ETW .etl)

namespace Formats {

inline bool ParseWindowsEtlTraceDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #93. Windows Event Trace Log (.etl — WMI_BUFFER_HEADER + TRACE_LOGFILE_HEADER + ETW Event Records)
    if (ext == L".etl" && raw.size() >= 1024) {
        const uint32_t buf0Size = DetailDocs200::RdU32LE(raw.data() + 0);
        // WMI_BUFFER_HEADER BufferSize must be a 1 KB multiple in [1024, 16 MB] and fit inside raw
        if (buf0Size < 1024u || buf0Size > (16u * 1024u * 1024u) || (buf0Size % 1024u) != 0 || buf0Size > raw.size()) {
            return false;
        }

        auto getValidBufferEnd = [](const uint8_t* bHdr, uint32_t bSize) -> uint32_t {
            const uint32_t savedOff = DetailDocs200::RdU32LE(bHdr + 4);
            const uint32_t wnodeOff = DetailDocs200::RdU32LE(bHdr + 48);
            if (wnodeOff >= 72u && wnodeOff <= bSize) return wnodeOff;
            if (savedOff >= 72u && savedOff <= bSize) return savedOff;
            return 0u;
        };

        auto formatGuid = [](const uint8_t* g) -> std::wstring {
            const uint32_t d1 = DetailDocs200::RdU32LE(g + 0);
            const uint16_t d2 = DetailDocs200::RdU16LE(g + 4);
            const uint16_t d3 = DetailDocs200::RdU16LE(g + 6);
            wchar_t buf[64] = {};
            swprintf_s(buf, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                d1, d2, d3, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
            return buf;
        };

        auto formatFileTime = [](uint64_t ft100ns) -> std::wstring {
            // Windows FILETIME: 100-ns intervals since 1601-01-01 UTC; if small QPC tick, render raw tick
            if (ft100ns < 116444736000000000ULL || ft100ns > 200000000000000000ULL) {
                return L"QPC " + std::to_wstring(ft100ns);
            }
            FILETIME ft{};
            ft.dwLowDateTime  = static_cast<DWORD>(ft100ns & 0xFFFFFFFFULL);
            ft.dwHighDateTime = static_cast<DWORD>(ft100ns >> 32);
            SYSTEMTIME st{};
            if (!FileTimeToSystemTime(&ft, &st)) return std::to_wstring(ft100ns);
            const uint32_t us = static_cast<uint32_t>((ft100ns / 10ULL) % 1000000ULL);
            wchar_t buf[64] = {};
            swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%06u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, us);
            return buf;
        };

        auto levelName = [](uint8_t lvl) -> std::wstring {
            switch (lvl) {
            case 0: return L"0 (LogAlways)";
            case 1: return L"1 (Critical)";
            case 2: return L"2 (Error)";
            case 3: return L"3 (Warning)";
            case 4: return L"4 (Info)";
            case 5: return L"5 (Verbose)";
            default: return std::to_wstring(lvl);
            }
        };

        auto isValidEtwRecordAt = [](const uint8_t* rec, size_t maxBytes) -> bool {
            if (maxBytes < 16) return false;
            const uint16_t recSize = DetailDocs200::RdU16LE(rec + 0);
            const uint8_t hdrType  = rec[2];
            const uint8_t marker   = rec[3];
            if (marker != 0xC0u || recSize > maxBytes) return false;
            if (hdrType == 0x12u || hdrType == 0x13u) return recSize >= 80u;      // EVENT_HEADER (32/64-bit)
            if (hdrType == 0x10u || hdrType == 0x11u) return recSize >= 32u;      // Compact EVENT_HEADER
            if (hdrType == 0x01u || hdrType == 0x02u) return recSize >= 32u;      // SYSTEM_TRACE_HEADER (32/64-bit)
            if (hdrType == 0x03u || hdrType == 0x0Cu || hdrType == 0x0Du) return recSize >= 16u; // PERFINFO_TRACE_HEADER
            if (hdrType <= 0x0Fu) return recSize >= 48u;                          // EVENT_TRACE_HEADER
            return false;
        };

        const uint32_t validEnd0 = getValidBufferEnd(raw.data(), buf0Size);
        if (validEnd0 <= 72u || !isValidEtwRecordAt(raw.data() + 72, validEnd0 - 72u)) {
            return false;
        }

        struct ProviderAgg {
            uint64_t count = 0;
            uint16_t minId = 0xFFFFu;
            uint16_t maxId = 0;
            uint32_t levelMask = 0;
        };
        std::map<std::wstring, ProviderAgg> providers;

        SpreadsheetSheet sessSh;
        sessSh.name = L"Session & Buffers";
        sessSh.maxCols = 2;
        sessSh.rows.push_back({ L"ETW Property", L"Value" });

        SpreadsheetSheet evtSh;
        evtSh.name = L"Events";
        evtSh.maxCols = 9;
        evtSh.rows.push_back({ L"Seq", L"Timestamp", L"PID", L"TID", L"Provider GUID", L"EventID", L"Level", L"Opcode", L"Payload Bytes" });

        bool parsedLogfileHdr = false;
        uint64_t totalBuffers = 0;
        uint64_t totalEvents  = 0;

        size_t bufFileOff = 0;
        while (bufFileOff + 72u <= raw.size()) {
            const uint8_t* bHdr = raw.data() + bufFileOff;
            const uint32_t bSize = DetailDocs200::RdU32LE(bHdr + 0);
            if (bSize < 1024u || bSize > (16u * 1024u * 1024u) || (bSize % 1024u) != 0) break;
            if (bufFileOff + bSize > raw.size()) break;

            const uint32_t vEnd = getValidBufferEnd(bHdr, bSize);
            ++totalBuffers;
            if (vEnd > 72u && vEnd <= bSize) {
                size_t rOff = 72u;
                while (rOff + 16u <= vEnd) {
                    const uint8_t* rec = bHdr + rOff;
                    if (rec[3] != 0xC0u) break;
                    const uint16_t recSize = DetailDocs200::RdU16LE(rec + 0);
                    const uint8_t hdrType  = rec[2];
                    if (recSize < 16u || rOff + recSize > vEnd) break;

                    if ((hdrType == 0x12u || hdrType == 0x13u) && recSize >= 80u) {
                        // Vista+ Crimson EVENT_HEADER (80 bytes)
                        const uint16_t flags   = DetailDocs200::RdU16LE(rec + 4);
                        const uint32_t tid     = DetailDocs200::RdU32LE(rec + 8);
                        const uint32_t pid     = DetailDocs200::RdU32LE(rec + 12);
                        const uint64_t ts      = DetailDocs200::RdU64LE(rec + 16);
                        const std::wstring gid = formatGuid(rec + 24);
                        const uint16_t evtId   = DetailDocs200::RdU16LE(rec + 40);
                        const uint8_t level    = rec[44];
                        const uint8_t opcode   = rec[45];
                        uint32_t payloadLen    = recSize - 80u;
                        if ((flags & 0x0001u) != 0 && payloadLen >= 2u) {
                            // Adjust for extended data items if present
                            const uint16_t extCnt = DetailDocs200::RdU16LE(rec + 80);
                            size_t extPos = 82u;
                            for (uint16_t e = 0; e < extCnt && extPos + 8u <= recSize; ++e) {
                                const uint16_t dSz = DetailDocs200::RdU16LE(rec + extPos + 6u);
                                extPos += 8u + dSz;
                            }
                            if (extPos <= recSize) payloadLen = static_cast<uint32_t>(recSize - extPos);
                        }

                        ++totalEvents;
                        auto& pa = providers[gid];
                        pa.count++;
                        pa.minId = std::min(pa.minId, evtId);
                        pa.maxId = std::max(pa.maxId, evtId);
                        if (level < 32u) pa.levelMask |= (1u << level);

                        if (evtSh.rows.size() <= 512) {
                            evtSh.rows.push_back({
                                std::to_wstring(totalEvents),
                                formatFileTime(ts),
                                std::to_wstring(pid),
                                std::to_wstring(tid),
                                gid,
                                std::to_wstring(evtId),
                                levelName(level),
                                std::to_wstring(opcode),
                                std::to_wstring(payloadLen)
                            });
                        }
                    } else if ((hdrType == 0x01u || hdrType == 0x02u) && recSize >= 32u) {
                        // Kernel SYSTEM_TRACE_HEADER (32 bytes)
                        const uint8_t opcode = rec[4];
                        const uint8_t group  = rec[5];
                        const uint32_t tid   = DetailDocs200::RdU32LE(rec + 8);
                        const uint32_t pid   = DetailDocs200::RdU32LE(rec + 12);
                        const uint64_t ts    = DetailDocs200::RdU64LE(rec + 16);
                        const uint16_t synId = static_cast<uint16_t>((static_cast<uint16_t>(group) << 8) | opcode);
                        const std::wstring kGuid = L"{9E814AAD-3204-11D2-9A82-006008A86939}"; // SystemTraceControlGuid

                        ++totalEvents;
                        auto& pa = providers[kGuid];
                        pa.count++;
                        pa.minId = std::min(pa.minId, synId);
                        pa.maxId = std::max(pa.maxId, synId);
                        pa.levelMask |= (1u << 4);

                        if (evtSh.rows.size() <= 512) {
                            evtSh.rows.push_back({
                                std::to_wstring(totalEvents),
                                formatFileTime(ts),
                                std::to_wstring(pid),
                                std::to_wstring(tid),
                                kGuid,
                                std::to_wstring(synId),
                                L"4 (Kernel)",
                                std::to_wstring(opcode),
                                std::to_wstring(recSize - 32u)
                            });
                        }
                    } else if (hdrType <= 0x0Fu && hdrType != 0x03u && hdrType != 0x0Cu && hdrType != 0x0Du && recSize >= 48u) {
                        // Classic EVENT_TRACE_HEADER (48 bytes)
                        const uint8_t opcode   = rec[4];
                        const uint8_t level    = rec[5];
                        const uint32_t tid     = DetailDocs200::RdU32LE(rec + 8);
                        const uint32_t pid     = DetailDocs200::RdU32LE(rec + 12);
                        const uint64_t ts      = DetailDocs200::RdU64LE(rec + 16);
                        const std::wstring gid = formatGuid(rec + 24);
                        const uint32_t payLen  = recSize - 48u;

                        if (!parsedLogfileHdr && totalBuffers == 1 && rOff == 72u && opcode == 0u && payLen >= 64u) {
                            parsedLogfileHdr = true;
                            const uint8_t* lh = rec + 48;
                            const uint8_t majVer  = lh[4];
                            const uint8_t minVer  = lh[5];
                            const uint32_t build  = DetailDocs200::RdU32LE(lh + 8);
                            const uint32_t nProcs = DetailDocs200::RdU32LE(lh + 12);
                            const uint64_t endFt  = DetailDocs200::RdU64LE(lh + 16);
                            const uint32_t timRes = DetailDocs200::RdU32LE(lh + 24);
                            const uint32_t bufWr  = DetailDocs200::RdU32LE(lh + 36);
                            const uint32_t ptrSz  = DetailDocs200::RdU32LE(lh + 44);
                            const uint32_t evtLst = DetailDocs200::RdU32LE(lh + 48);
                            const uint32_t cpuMhz = DetailDocs200::RdU32LE(lh + 52);

                            sessSh.rows.push_back({ L"OS Version",
                                std::to_wstring(majVer) + L"." + std::to_wstring(minVer) + L" (Build " + std::to_wstring(build) + L")" });
                            if (ptrSz == 4u || ptrSz == 8u) {
                                sessSh.rows.push_back({ L"Architecture Pointer Size", std::to_wstring(ptrSz * 8u) + L"-bit" });
                            }
                            sessSh.rows.push_back({ L"Number of Processors", std::to_wstring(nProcs) });
                            if (cpuMhz > 0) sessSh.rows.push_back({ L"CPU Speed", std::to_wstring(cpuMhz) + L" MHz" });
                            if (timRes > 0) sessSh.rows.push_back({ L"Timer Resolution (100ns)", std::to_wstring(timRes) });
                            sessSh.rows.push_back({ L"Header Buffers Written", std::to_wstring(bufWr) });
                            sessSh.rows.push_back({ L"Events Lost", std::to_wstring(evtLst) });

                            const size_t tailBase = (ptrSz == 4u) ? 240u : 248u;
                            if (payLen >= tailBase + 32u) {
                                const uint64_t bootFt  = DetailDocs200::RdU64LE(lh + tailBase + 0);
                                const uint64_t startFt = DetailDocs200::RdU64LE(lh + tailBase + 16);
                                const uint32_t bufLost = DetailDocs200::RdU32LE(lh + tailBase + 28);
                                if (bootFt > 0)  sessSh.rows.push_back({ L"Boot Time (UTC)", formatFileTime(bootFt) });
                                if (startFt > 0) sessSh.rows.push_back({ L"Session Start Time (UTC)", formatFileTime(startFt) });
                                if (endFt > 0)   sessSh.rows.push_back({ L"Session End Time (UTC)", formatFileTime(endFt) });
                                sessSh.rows.push_back({ L"Buffers Lost", std::to_wstring(bufLost) });

                                const size_t strOff = tailBase + 32u;
                                if (payLen > strOff + 2u) {
                                    std::wstring loggerName = DetailDocs200::ReadUtf16LE(lh + strOff, payLen - strOff);
                                    if (!loggerName.empty()) {
                                        sessSh.rows.push_back({ L"Logger Name", loggerName });
                                        const size_t nextStr = strOff + (loggerName.size() + 1u) * 2u;
                                        if (payLen > nextStr + 2u) {
                                            std::wstring logFile = DetailDocs200::ReadUtf16LE(lh + nextStr, payLen - nextStr);
                                            if (!logFile.empty()) sessSh.rows.push_back({ L"Log File Name", logFile });
                                        }
                                    }
                                }
                            }
                        }

                        ++totalEvents;
                        auto& pa = providers[gid];
                        pa.count++;
                        pa.minId = std::min<uint16_t>(pa.minId, opcode);
                        pa.maxId = std::max<uint16_t>(pa.maxId, opcode);
                        if (level < 32u) pa.levelMask |= (1u << level);

                        if (evtSh.rows.size() <= 512) {
                            evtSh.rows.push_back({
                                std::to_wstring(totalEvents),
                                formatFileTime(ts),
                                std::to_wstring(pid),
                                std::to_wstring(tid),
                                gid,
                                std::to_wstring(opcode),
                                levelName(level),
                                std::to_wstring(opcode),
                                std::to_wstring(payLen)
                            });
                        }
                    } else {
                        // PERFINFO_TRACE_HEADER (0x03/0x0C/0x0D)
                        const uint16_t hookId = DetailDocs200::RdU16LE(rec + 4);
                        const uint64_t ts     = DetailDocs200::RdU64LE(rec + 8);
                        const std::wstring pGuid = L"{CE1DBFB4-137E-4DA6-87B0-3F59AA102CBC}";
                        ++totalEvents;
                        auto& pa = providers[pGuid];
                        pa.count++;
                        pa.minId = std::min(pa.minId, hookId);
                        pa.maxId = std::max(pa.maxId, hookId);
                        pa.levelMask |= (1u << 4);
                        if (evtSh.rows.size() <= 512) {
                            evtSh.rows.push_back({
                                std::to_wstring(totalEvents),
                                formatFileTime(ts),
                                L"0",
                                L"0",
                                pGuid,
                                std::to_wstring(hookId),
                                L"4 (PerfInfo)",
                                std::to_wstring(hookId & 0xFFu),
                                std::to_wstring(recSize - 16u)
                            });
                        }
                    }

                    const size_t alignedStep = (static_cast<size_t>(recSize) + 7u) & ~static_cast<size_t>(7u);
                    if (alignedStep == 0) break;
                    rOff += alignedStep;
                }
            }

            bufFileOff += bSize;
        }

        if (totalEvents == 0) return false;

        sessSh.rows.insert(sessSh.rows.begin() + 1, { L"WMI Buffer Size", PathUtils::FormatByteSize(buf0Size) });
        sessSh.rows.insert(sessSh.rows.begin() + 2, { L"Parsed ETW Buffers", std::to_wstring(totalBuffers) });
        sessSh.rows.insert(sessSh.rows.begin() + 3, { L"Total Decoded Event Records", std::to_wstring(totalEvents) });
        sessSh.rows.insert(sessSh.rows.begin() + 4, { L"Distinct Provider GUIDs", std::to_wstring(providers.size()) });
        sessSh.rows.push_back({ L"First Buffer LoggerId", std::to_wstring(DetailDocs200::RdU16LE(raw.data() + 42)) });
        sessSh.rows.push_back({ L"First Buffer Processor", std::to_wstring(raw[40]) });

        SpreadsheetSheet provSh;
        provSh.name = L"Providers";
        provSh.maxCols = 4;
        provSh.rows.push_back({ L"Provider GUID", L"Event Count", L"Levels Seen", L"EventID Range" });
        for (const auto& kv : providers) {
            std::wstring lvls;
            for (uint8_t l = 0; l < 8u; ++l) {
                if ((kv.second.levelMask & (1u << l)) != 0) {
                    if (!lvls.empty()) lvls += L", ";
                    lvls += std::to_wstring(l);
                }
            }
            provSh.rows.push_back({
                kv.first,
                std::to_wstring(kv.second.count),
                lvls.empty() ? L"0" : lvls,
                std::to_wstring(kv.second.minId) + L" .. " + std::to_wstring(kv.second.maxId)
            });
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Event Trace Log (ETW .etl)";
        out.sheets.push_back(std::move(sessSh));
        out.sheets.push_back(std::move(provSh));
        out.sheets.push_back(std::move(evtSh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
