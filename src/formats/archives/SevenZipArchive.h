#pragma once
// SevenZipArchive.h — In-process 7-Zip ('7z\xBC\xAF\x27\x1C') StreamsInfo, kEncodedHeader, kHeader & solid folder extractor (LZMA1, LZMA2, BCJ, BCJ2, Delta, Deflate, BZip2).
#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

inline bool Read7zNumberChecked(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    if (pos >= size) return false;
    uint8_t first = data[pos++];
    uint8_t mask = 0x80u;
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        if ((first & mask) == 0) {
            uint64_t high = first & (mask - 1u);
            outVal = val | (high << (i * 8));
            return true;
        }
        if (pos >= size) return false;
        val |= (static_cast<uint64_t>(data[pos++]) << (i * 8));
        mask >>= 1;
    }
    outVal = val;
    return true;
}

inline bool Read7zBoolVector(const uint8_t* data, size_t size, size_t& pos, size_t count, bool checkAllDefined, std::vector<bool>& outVec) {
    outVec.assign(count, false);
    if (count == 0) return true;
    if (checkAllDefined) {
        if (pos >= size) return false;
        uint8_t allDef = data[pos++];
        if (allDef != 0) {
            outVec.assign(count, true);
            return true;
        }
    }
    size_t numBytes = (count + 7u) / 8u;
    if (numBytes > size - pos) return false;
    for (size_t i = 0; i < count; ++i) {
        uint8_t b = data[pos + (i >> 3)];
        outVec[i] = ((b & (0x80u >> (i & 7u))) != 0);
    }
    pos += numBytes;
    return true;
}

inline bool Read7zDigests(const uint8_t* data, size_t size, size_t& pos, size_t count, std::vector<bool>& outDef, std::vector<uint32_t>& outCrcs) {
    outCrcs.assign(count, 0u);
    if (!Read7zBoolVector(data, size, pos, count, true, outDef)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (outDef[i]) {
            if (size - pos < 4u) return false;
            outCrcs[i] = ByteIO::RdU32LE(data + pos);
            pos += 4;
        }
    }
    return true;
}

struct SevenZPackInfo {
    uint64_t packPos = 0;
    std::vector<uint64_t> packSizes;
};

struct SevenZCoder {
    uint64_t methodId = 0;
    uint64_t numInStreams = 1;
    uint64_t numOutStreams = 1;
    uint64_t firstInStream = 0;
    uint64_t firstOutStream = 0;
    std::vector<uint8_t> props;
};

struct SevenZBindPair {
    uint64_t inIndex = 0;
    uint64_t outIndex = 0;
};

struct SevenZFolder {
    std::vector<SevenZCoder> coders;
    std::vector<SevenZBindPair> bindPairs;
    std::vector<uint64_t> packedStreamIndices;
    std::vector<uint64_t> coderUnpackSizes;
    uint64_t totalInStreams = 0;
    uint64_t totalOutStreams = 0;
    uint64_t primaryOutStream = 0;
    uint64_t finalUnpackSize = 0;
    size_t firstPackStreamIdx = 0;
    uint64_t totalPackBytes = 0;
    bool hasCrc = false;
    uint32_t crc = 0;
};

struct SevenZSubStream {
    size_t folderIndex = 0;
    uint64_t offsetInFolder = 0;
    uint64_t unpackSize = 0;
    uint64_t proportionalPackSize = 0;
    bool hasCrc = false;
    uint32_t crc = 0;
};

struct SevenZStreamsInfo {
    SevenZPackInfo pack;
    std::vector<SevenZFolder> folders;
    std::vector<SevenZSubStream> subStreams;
};

struct SevenZFileSlot {
    std::wstring pathInArchive;
    uint64_t uncompressedSize = 0;
    uint64_t compressedSize = 0;
    uint32_t crc32 = 0;
    bool hasCrc = false;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    bool isDirectory = false;
    bool isEmpty = false;
    size_t folderIndex = 0;
    uint64_t offsetInFolder = 0;
};

struct ParsedSevenZipArchive {
    bool valid = false;
    bool hasEncryptedHeader = false;
    SevenZStreamsInfo mainStreams;
    std::vector<SevenZFileSlot> files;
};

inline bool Parse7zPackInfo(const uint8_t* hdr, size_t hSize, size_t& pos, SevenZPackInfo& outPack) {
    uint64_t numPack = 0;
    if (!Read7zNumberChecked(hdr, hSize, pos, outPack.packPos) ||
        !Read7zNumberChecked(hdr, hSize, pos, numPack) ||
        numPack > 262144ULL)
    {
        return false;
    }
    outPack.packSizes.assign(static_cast<size_t>(numPack), 0);

    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) return true;
        if (id == 0x09u) { // kSize
            for (size_t i = 0; i < outPack.packSizes.size(); ++i) {
                if (!Read7zNumberChecked(hdr, hSize, pos, outPack.packSizes[i])) return false;
            }
        } else if (id == 0x0Au) { // kCRC
            std::vector<bool> def;
            std::vector<uint32_t> crcs;
            if (!Read7zDigests(hdr, hSize, pos, outPack.packSizes.size(), def, crcs)) return false;
        } else {
            uint64_t skip = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, skip) || skip > hSize - pos) return false;
            pos += static_cast<size_t>(skip);
        }
    }
    return false;
}

inline bool Parse7zSingleFolder(const uint8_t* hdr, size_t hSize, size_t& pos, SevenZFolder& outFolder) {
    outFolder = {};
    uint64_t numCoders = 0;
    if (!Read7zNumberChecked(hdr, hSize, pos, numCoders) || numCoders == 0 || numCoders > 16) return false;
    outFolder.coders.resize(static_cast<size_t>(numCoders));

    for (size_t c = 0; c < outFolder.coders.size(); ++c) {
        if (pos >= hSize) return false;
        uint8_t mainByte = hdr[pos++];
        size_t idSize = mainByte & 0x0Fu;
        bool isComplex = (mainByte & 0x10u) != 0;
        bool hasAttrs  = (mainByte & 0x20u) != 0;
        if (idSize == 0 || idSize > 8 || idSize > hSize - pos) return false;

        uint64_t methodId = 0;
        for (size_t k = 0; k < idSize; ++k) {
            methodId = (methodId << 8) | hdr[pos++];
        }
        auto& cd = outFolder.coders[c];
        cd.methodId = methodId;
        if (isComplex) {
            if (!Read7zNumberChecked(hdr, hSize, pos, cd.numInStreams) ||
                !Read7zNumberChecked(hdr, hSize, pos, cd.numOutStreams))
            {
                return false;
            }
            if (cd.numInStreams == 0 || cd.numInStreams > 16 || cd.numOutStreams == 0 || cd.numOutStreams > 16) {
                return false;
            }
        }
        if (hasAttrs) {
            uint64_t propLen = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, propLen) || propLen > 1024 || propLen > hSize - pos) {
                return false;
            }
            cd.props.assign(hdr + pos, hdr + pos + static_cast<size_t>(propLen));
            pos += static_cast<size_t>(propLen);
        }
        cd.firstInStream = outFolder.totalInStreams;
        cd.firstOutStream = outFolder.totalOutStreams;
        outFolder.totalInStreams += cd.numInStreams;
        outFolder.totalOutStreams += cd.numOutStreams;
    }

    if (outFolder.totalOutStreams == 0 || outFolder.totalInStreams == 0) return false;
    uint64_t numBindPairs = outFolder.totalOutStreams - 1;
    outFolder.bindPairs.resize(static_cast<size_t>(numBindPairs));
    for (size_t b = 0; b < outFolder.bindPairs.size(); ++b) {
        if (!Read7zNumberChecked(hdr, hSize, pos, outFolder.bindPairs[b].inIndex) ||
            !Read7zNumberChecked(hdr, hSize, pos, outFolder.bindPairs[b].outIndex))
        {
            return false;
        }
        if (outFolder.bindPairs[b].inIndex >= outFolder.totalInStreams ||
            outFolder.bindPairs[b].outIndex >= outFolder.totalOutStreams)
        {
            return false;
        }
    }

    if (outFolder.totalInStreams < numBindPairs) return false;
    uint64_t numPacked = outFolder.totalInStreams - numBindPairs;
    if (numPacked == 1) {
        for (uint64_t inIdx = 0; inIdx < outFolder.totalInStreams; ++inIdx) {
            bool bound = false;
            for (const auto& bp : outFolder.bindPairs) {
                if (bp.inIndex == inIdx) { bound = true; break; }
            }
            if (!bound) {
                outFolder.packedStreamIndices.push_back(inIdx);
                break;
            }
        }
        if (outFolder.packedStreamIndices.size() != 1) return false;
    } else {
        outFolder.packedStreamIndices.resize(static_cast<size_t>(numPacked));
        for (size_t k = 0; k < outFolder.packedStreamIndices.size(); ++k) {
            if (!Read7zNumberChecked(hdr, hSize, pos, outFolder.packedStreamIndices[k]) ||
                outFolder.packedStreamIndices[k] >= outFolder.totalInStreams)
            {
                return false;
            }
        }
    }

    // Identify primary unbound output stream
    bool foundPrimary = false;
    for (uint64_t outIdx = 0; outIdx < outFolder.totalOutStreams; ++outIdx) {
        bool bound = false;
        for (const auto& bp : outFolder.bindPairs) {
            if (bp.outIndex == outIdx) { bound = true; break; }
        }
        if (!bound) {
            outFolder.primaryOutStream = outIdx;
            foundPrimary = true;
            break;
        }
    }
    return foundPrimary;
}

inline bool Parse7zUnPackInfo(const uint8_t* hdr, size_t hSize, size_t& pos, std::vector<SevenZFolder>& outFolders) {
    outFolders.clear();
    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) return true;
        if (id == 0x0Bu) { // kFolder
            uint64_t numFolders = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, numFolders) || numFolders > 131072ULL) return false;
            if (pos >= hSize || hdr[pos++] != 0u) return false; // external must be 0
            outFolders.resize(static_cast<size_t>(numFolders));
            for (size_t f = 0; f < outFolders.size(); ++f) {
                if (!Parse7zSingleFolder(hdr, hSize, pos, outFolders[f])) return false;
            }
        } else if (id == 0x0Cu) { // kCodersUnPackSize
            for (auto& f : outFolders) {
                f.coderUnpackSizes.resize(static_cast<size_t>(f.totalOutStreams));
                for (size_t o = 0; o < f.coderUnpackSizes.size(); ++o) {
                    if (!Read7zNumberChecked(hdr, hSize, pos, f.coderUnpackSizes[o])) return false;
                }
                if (f.primaryOutStream < f.coderUnpackSizes.size()) {
                    f.finalUnpackSize = f.coderUnpackSizes[static_cast<size_t>(f.primaryOutStream)];
                }
            }
        } else if (id == 0x0Au) { // kCRC
            std::vector<bool> def;
            std::vector<uint32_t> crcs;
            if (!Read7zDigests(hdr, hSize, pos, outFolders.size(), def, crcs)) return false;
            for (size_t f = 0; f < outFolders.size(); ++f) {
                outFolders[f].hasCrc = def[f];
                outFolders[f].crc = crcs[f];
            }
        } else {
            uint64_t skip = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, skip) || skip > hSize - pos) return false;
            pos += static_cast<size_t>(skip);
        }
    }
    return false;
}

inline bool Parse7zSubStreamsInfo(const uint8_t* hdr, size_t hSize, size_t& pos, const std::vector<SevenZFolder>& folders, std::vector<SevenZSubStream>& outSubs) {
    outSubs.clear();
    std::vector<uint64_t> numPerFolder(folders.size(), 1ULL);
    std::vector<std::vector<uint64_t>> explicitSizes(folders.size());
    bool hasExplicitSizes = false;
    std::vector<bool> subCrcDef;
    std::vector<uint32_t> subCrcs;
    bool sawSubCrcs = false;

    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) break;
        if (id == 0x0Du) { // kNumUnPackStream
            for (size_t f = 0; f < folders.size(); ++f) {
                if (!Read7zNumberChecked(hdr, hSize, pos, numPerFolder[f]) || numPerFolder[f] > 262144ULL) return false;
            }
        } else if (id == 0x09u) { // kSize
            hasExplicitSizes = true;
            for (size_t f = 0; f < folders.size(); ++f) {
                uint64_t n = numPerFolder[f];
                if (n == 0) continue;
                explicitSizes[f].resize(static_cast<size_t>(n));
                uint64_t sum = 0;
                for (uint64_t s = 0; s + 1 < n; ++s) {
                    uint64_t sz = 0;
                    if (!Read7zNumberChecked(hdr, hSize, pos, sz) || sz > folders[f].finalUnpackSize - sum) return false;
                    explicitSizes[f][static_cast<size_t>(s)] = sz;
                    sum += sz;
                }
                explicitSizes[f][static_cast<size_t>(n - 1)] = folders[f].finalUnpackSize - sum;
            }
        } else if (id == 0x0Au) { // kCRC
            size_t numDigests = 0;
            for (size_t f = 0; f < folders.size(); ++f) {
                if (numPerFolder[f] != 1ULL || !folders[f].hasCrc) {
                    numDigests += static_cast<size_t>(numPerFolder[f]);
                }
            }
            if (!Read7zDigests(hdr, hSize, pos, numDigests, subCrcDef, subCrcs)) return false;
            sawSubCrcs = true;
        } else {
            uint64_t skip = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, skip) || skip > hSize - pos) return false;
            pos += static_cast<size_t>(skip);
        }
    }

    size_t digestCursor = 0;
    for (size_t f = 0; f < folders.size(); ++f) {
        uint64_t n = numPerFolder[f];
        if (n == 0) continue;
        uint64_t offInFolder = 0;
        for (uint64_t s = 0; s < n; ++s) {
            SevenZSubStream sub;
            sub.folderIndex = f;
            sub.offsetInFolder = offInFolder;
            sub.unpackSize = hasExplicitSizes
                ? explicitSizes[f][static_cast<size_t>(s)]
                : ((n == 1ULL) ? folders[f].finalUnpackSize : 0ULL);
            if (folders[f].finalUnpackSize > 0) {
                sub.proportionalPackSize = (folders[f].totalPackBytes * sub.unpackSize) / folders[f].finalUnpackSize;
            } else {
                sub.proportionalPackSize = folders[f].totalPackBytes / n;
            }
            if (n == 1ULL && folders[f].hasCrc) {
                sub.hasCrc = true;
                sub.crc = folders[f].crc;
            } else if (sawSubCrcs && digestCursor < subCrcs.size()) {
                sub.hasCrc = subCrcDef[digestCursor];
                sub.crc = subCrcs[digestCursor];
                ++digestCursor;
            }
            offInFolder += sub.unpackSize;
            outSubs.push_back(sub);
        }
    }
    return true;
}

inline bool Parse7zStreamsInfo(const uint8_t* hdr, size_t hSize, size_t& pos, SevenZStreamsInfo& outSi) {
    outSi = {};
    bool sawSubStreams = false;
    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) break;
        if (id == 0x06u) {
            if (!Parse7zPackInfo(hdr, hSize, pos, outSi.pack)) return false;
        } else if (id == 0x07u) {
            if (!Parse7zUnPackInfo(hdr, hSize, pos, outSi.folders)) return false;
            size_t packCursor = 0;
            for (auto& f : outSi.folders) {
                f.firstPackStreamIdx = packCursor;
                f.totalPackBytes = 0;
                for (size_t k = 0; k < f.packedStreamIndices.size(); ++k) {
                    if (packCursor + k >= outSi.pack.packSizes.size()) return false;
                    f.totalPackBytes += outSi.pack.packSizes[packCursor + k];
                }
                packCursor += f.packedStreamIndices.size();
            }
        } else if (id == 0x08u) {
            sawSubStreams = true;
            if (!Parse7zSubStreamsInfo(hdr, hSize, pos, outSi.folders, outSi.subStreams)) return false;
        } else {
            uint64_t skip = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, skip) || skip > hSize - pos) return false;
            pos += static_cast<size_t>(skip);
        }
    }

    if (!sawSubStreams) {
        for (size_t f = 0; f < outSi.folders.size(); ++f) {
            SevenZSubStream sub;
            sub.folderIndex = f;
            sub.offsetInFolder = 0;
            sub.unpackSize = outSi.folders[f].finalUnpackSize;
            sub.proportionalPackSize = outSi.folders[f].totalPackBytes;
            sub.hasCrc = outSi.folders[f].hasCrc;
            sub.crc = outSi.folders[f].crc;
            outSi.subStreams.push_back(sub);
        }
    }
    return true;
}

// 7-Zip BCJ2 (0x0303011B) 4-stream x86 jump reassembler:
//   in0 = main instruction stream
//   in1 = big-endian uint32 CALL target stream (0xE8)
//   in2 = big-endian uint32 JUMP target stream (0xE9 / 0x0F 0x80..0x8F)
//   in3 = range-coded branch selector stream (258 adaptive 11-bit probability models)
inline bool Decode7zBcj2Streams(
    const uint8_t* mainBuf, size_t mainLen,
    const uint8_t* callBuf, size_t callLen,
    const uint8_t* jumpBuf, size_t jumpLen,
    const uint8_t* rcBuf,   size_t rcLen,
    uint32_t baseIp,
    uint64_t wantOut,
    std::vector<uint8_t>& out,
    size_t maxOut)
{
    out.clear();
    if (wantOut > maxOut) return false;
    if (wantOut == 0) return true;
    if (!mainBuf || mainLen == 0) return false;

    out.reserve(static_cast<size_t>(wantOut));

    uint32_t range = 0xFFFFFFFFu;
    uint32_t code  = 0;
    size_t rcPos   = 0;
    bool rcInitialized = false;

    auto initRcIfNeeded = [&]() -> bool {
        if (rcInitialized) return true;
        if (!rcBuf || rcLen < 5u) return false;
        code = ByteIO::RdU32BE(rcBuf + 1u);
        rcPos = 5u;
        rcInitialized = true;
        return true;
    };

    uint16_t probs[258];
    for (uint16_t& p : probs) p = 1024u;

    size_t p0 = 0, p1 = 0, p2 = 0;
    uint8_t prevByte = 0;

    while (out.size() < wantOut) {
        if (p0 >= mainLen) return false;
        const uint8_t b = mainBuf[p0++];
        out.push_back(b);
        if (out.size() == wantOut) break;

        const bool isCall = (b == 0xE8u);
        const bool isJmp  = (b == 0xE9u);
        const bool isJcc  = (prevByte == 0x0Fu && (b & 0xF0u) == 0x80u);
        if (!isCall && !isJmp && !isJcc) {
            prevByte = b;
            continue;
        }

        if (!initRcIfNeeded()) return false;
        if (range < 0x01000000u) {
            if (rcPos >= rcLen) return false;
            range <<= 8;
            code = (code << 8) | rcBuf[rcPos++];
        }

        const size_t probIdx = isCall ? static_cast<size_t>(prevByte) : (isJmp ? 256u : 257u);
        uint16_t& prob = probs[probIdx];
        const uint32_t bound = (range >> 11) * prob;
        if (code < bound) {
            range = bound;
            prob = static_cast<uint16_t>(prob + ((2048u - prob) >> 5));
            prevByte = b;
            continue;
        }

        range -= bound;
        code  -= bound;
        prob = static_cast<uint16_t>(prob - (prob >> 5));

        uint32_t absDest = 0;
        if (isCall) {
            if (!callBuf || callLen - p1 < 4u) return false;
            absDest = ByteIO::RdU32BE(callBuf + p1);
            p1 += 4u;
        } else {
            if (!jumpBuf || jumpLen - p2 < 4u) return false;
            absDest = ByteIO::RdU32BE(jumpBuf + p2);
            p2 += 4u;
        }

        const uint32_t curIp = baseIp + static_cast<uint32_t>(out.size()) + 4u;
        const uint32_t rel   = absDest - curIp;
        const size_t remOut  = static_cast<size_t>(wantOut) - out.size();
        const size_t writeN  = std::min<size_t>(4u, remOut);
        for (size_t k = 0; k < writeN; ++k) {
            out.push_back(static_cast<uint8_t>((rel >> (k * 8u)) & 0xFFu));
        }
        prevByte = static_cast<uint8_t>((rel >> 24u) & 0xFFu);
    }

    return out.size() == wantOut;
}

inline bool Decode7zFolder(
    const uint8_t* arcData,
    size_t arcSize,
    const SevenZStreamsInfo& si,
    size_t folderIdx,
    std::vector<uint8_t>& outUnpacked,
    size_t maxOut = 128 * 1024 * 1024)
{
    outUnpacked.clear();
    if (!arcData || folderIdx >= si.folders.size()) return false;
    const auto& folder = si.folders[folderIdx];
    if (folder.coders.empty() || folder.finalUnpackSize > maxOut) return false;

    for (const auto& c : folder.coders) {
        if (c.methodId == 0x06F10701ULL || c.numOutStreams != 1) {
            return false;
        }
        if (c.numInStreams != 1 && !(c.methodId == 0x0303011BULL && c.numInStreams == 4)) {
            return false;
        }
    }

    if (folder.packedStreamIndices.empty() ||
        folder.firstPackStreamIdx > si.pack.packSizes.size() ||
        folder.packedStreamIndices.size() > si.pack.packSizes.size() - folder.firstPackStreamIdx)
    {
        return false;
    }

    // Compute exact byte slices for all packed streams feeding this folder
    uint64_t relOff = si.pack.packPos;
    if (relOff > arcSize || arcSize < 32u || relOff > arcSize - 32u) return false;
    uint64_t curAbsOff = 32ULL + relOff;
    for (size_t p = 0; p < folder.firstPackStreamIdx; ++p) {
        const uint64_t ps = si.pack.packSizes[p];
        if (ps > arcSize - curAbsOff) return false;
        curAbsOff += ps;
    }

    struct StreamSpan {
        const uint8_t* ptr = nullptr;
        size_t len = 0;
    };
    std::vector<StreamSpan> packSpans(folder.packedStreamIndices.size());
    for (size_t k = 0; k < folder.packedStreamIndices.size(); ++k) {
        const uint64_t ps = si.pack.packSizes[folder.firstPackStreamIdx + k];
        if (ps > arcSize - curAbsOff) return false;
        packSpans[k] = { arcData + static_cast<size_t>(curAbsOff), static_cast<size_t>(ps) };
        curAbsOff += ps;
    }

    std::vector<std::vector<uint8_t>> outStreams(static_cast<size_t>(folder.totalOutStreams));
    std::vector<bool> streamReady(static_cast<size_t>(folder.totalOutStreams), false);
    std::vector<bool> coderDone(folder.coders.size(), false);

    auto resolveInStream = [&](uint64_t inStreamIdx, StreamSpan& outSpan) -> bool {
        for (size_t k = 0; k < folder.packedStreamIndices.size(); ++k) {
            if (folder.packedStreamIndices[k] == inStreamIdx) {
                outSpan = packSpans[k];
                return true;
            }
        }
        for (const auto& bp : folder.bindPairs) {
            if (bp.inIndex == inStreamIdx) {
                const size_t oIdx = static_cast<size_t>(bp.outIndex);
                if (!streamReady[oIdx]) return false;
                outSpan = { outStreams[oIdx].data(), outStreams[oIdx].size() };
                return true;
            }
        }
        return false;
    };

    for (size_t step = 0; step < folder.coders.size(); ++step) {
        bool progressed = false;
        for (size_t c = 0; c < folder.coders.size(); ++c) {
            if (coderDone[c]) continue;
            const auto& cd = folder.coders[c];

            StreamSpan inputs[4] = {};
            bool allReady = true;
            for (uint64_t s = 0; s < cd.numInStreams; ++s) {
                if (!resolveInStream(cd.firstInStream + s, inputs[static_cast<size_t>(s)])) {
                    allReady = false;
                    break;
                }
            }
            if (!allReady) continue;

            uint64_t wantOut = (cd.firstOutStream < folder.coderUnpackSizes.size())
                ? folder.coderUnpackSizes[static_cast<size_t>(cd.firstOutStream)]
                : folder.finalUnpackSize;
            if (wantOut > maxOut) return false;

            auto& dst = outStreams[static_cast<size_t>(cd.firstOutStream)];
            const uint8_t* inPtr = inputs[0].ptr;
            const size_t   inLen = inputs[0].len;
            bool ok = false;

            if (cd.methodId == 0x00ULL) {
                // Copy
                if (wantOut > inLen) return false;
                dst.assign(inPtr, inPtr + static_cast<size_t>(wantOut));
                ok = true;
            } else if (cd.methodId == 0x030101ULL) {
                // LZMA1
                if (cd.props.size() < 5) return false;
                if (wantOut == 0) {
                    dst.clear();
                    ok = true;
                } else {
                    uint32_t dictSz = ByteIO::RdU32LE(cd.props.data() + 1);
                    ok = StreamCodecs::DecompressLzma1(cd.props[0], dictSz, inPtr, inLen, dst, wantOut, true, maxOut);
                }
            } else if (cd.methodId == 0x21ULL) {
                // LZMA2
                if (cd.props.empty()) return false;
                if (wantOut == 0) {
                    dst.clear();
                    ok = true;
                } else {
                    ok = StreamCodecs::DecompressLzma2(cd.props[0], inPtr, inLen, dst, maxOut, nullptr, wantOut);
                    if (ok && dst.size() > wantOut) dst.resize(static_cast<size_t>(wantOut));
                }
            } else if (cd.methodId == 0x03030103ULL) {
                // BCJ x86 1-stream converter
                uint32_t ip = (cd.props.size() == 4) ? ByteIO::RdU32LE(cd.props.data()) : 0u;
                dst.assign(inPtr, inPtr + inLen);
                StreamCodecs::ApplyBcjX86Decode(dst.data(), dst.size(), ip);
                ok = true;
            } else if (cd.methodId == 0x0303011BULL) {
                // BCJ2 x86 4-stream jump reassembler
                if (cd.numInStreams != 4) return false;
                uint32_t ip = (cd.props.size() == 4) ? ByteIO::RdU32LE(cd.props.data()) : 0u;
                ok = Decode7zBcj2Streams(
                    inputs[0].ptr, inputs[0].len,
                    inputs[1].ptr, inputs[1].len,
                    inputs[2].ptr, inputs[2].len,
                    inputs[3].ptr, inputs[3].len,
                    ip, wantOut, dst, maxOut);
            } else if (cd.methodId == 0x03ULL) {
                // Delta filter
                size_t dist = (cd.props.empty() ? 0u : cd.props[0]) + 1u;
                dst.assign(inPtr, inPtr + inLen);
                StreamCodecs::ApplyXzDeltaDecode(dst.data(), dst.size(), dist);
                ok = true;
            } else if (cd.methodId == 0x040108ULL) {
                // Deflate
                ok = ZipDeflate::InflateRaw(inPtr, inLen, dst, maxOut);
            } else if (cd.methodId == 0x040202ULL) {
                // BZip2
                ok = StreamCodecs::DecompressBzip2Stream(inPtr, inLen, dst, maxOut);
            } else {
                return false;
            }

            if (!ok) return false;
            streamReady[static_cast<size_t>(cd.firstOutStream)] = true;
            coderDone[c] = true;
            progressed = true;
        }
        if (!progressed) break;
    }

    if (!streamReady[static_cast<size_t>(folder.primaryOutStream)]) return false;
    outUnpacked = std::move(outStreams[static_cast<size_t>(folder.primaryOutStream)]);
    if (folder.hasCrc && Checksum::Crc32(outUnpacked.data(), outUnpacked.size()) != folder.crc) {
        outUnpacked.clear();
        return false;
    }
    return true;
}

inline void FileTime64ToDosDateTimePortable(uint32_t lowPart, uint32_t highPart, uint16_t& outDate, uint16_t& outTime) {
#ifdef _WIN32
    FILETIME ft{ lowPart, highPart };
    FILETIME localFt{};
    if (FileTimeToLocalFileTime(&ft, &localFt)) {
        FileTimeToDosDateTime(&localFt, &outDate, &outTime);
        return;
    }
#endif
    const uint64_t ticks = (static_cast<uint64_t>(highPart) << 32) | lowPart;
    if (ticks > 116444736000000000ULL) {
        const uint64_t unixSec = (ticks - 116444736000000000ULL) / 10000000ULL;
        DetailDiskAndGameArc::UnixSecondsToDosDateTime(unixSec, outDate, outTime);
    }
}

inline bool Parse7zHeaderBlock(
    const uint8_t* arcData,
    size_t arcSize,
    const uint8_t* hdr,
    size_t hSize,
    int depth,
    ParsedSevenZipArchive& outParsed)
{
    if (depth > 2 || hSize == 0) return false;
    size_t pos = 0;
    uint8_t topId = hdr[pos++];

    if (topId == 0x17u) {
        // kEncodedHeader: decompress packed header stream and recurse on resulting kHeader
        SevenZStreamsInfo hdrStreams;
        if (!Parse7zStreamsInfo(hdr, hSize, pos, hdrStreams) || hdrStreams.folders.empty()) {
            for (const auto& f : hdrStreams.folders) {
                for (const auto& c : f.coders) {
                    if (c.methodId == 0x06F10701ULL) outParsed.hasEncryptedHeader = true;
                }
            }
            return false;
        }
        for (const auto& f : hdrStreams.folders) {
            for (const auto& c : f.coders) {
                if (c.methodId == 0x06F10701ULL) {
                    outParsed.hasEncryptedHeader = true;
                    return false;
                }
            }
        }
        std::vector<uint8_t> decodedHdr;
        if (!Decode7zFolder(arcData, arcSize, hdrStreams, 0, decodedHdr, 32 * 1024 * 1024) || decodedHdr.empty()) {
            return false;
        }
        return Parse7zHeaderBlock(arcData, arcSize, decodedHdr.data(), decodedHdr.size(), depth + 1, outParsed);
    }

    if (topId != 0x01u) return false; // Expect kHeader (0x01)

    std::vector<std::wstring> fileNames;
    std::vector<bool> emptyStream;
    std::vector<bool> emptyFile;
    bool sawEmptyFileProp = false;
    std::vector<bool> hasMTime;
    struct RawFt64 { uint32_t lo = 0; uint32_t hi = 0; };
    std::vector<RawFt64> mtimes;
    std::vector<bool> hasWinAttr;
    std::vector<uint32_t> winAttrs;
    uint64_t numFiles = 0;
    bool sawFilesInfo = false;

    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) break;
        if (id == 0x02u) { // kArchiveProperties
            while (pos < hSize) {
                uint8_t pid = hdr[pos++];
                if (pid == 0x00u) break;
                uint64_t pSz = 0;
                if (!Read7zNumberChecked(hdr, hSize, pos, pSz) || pSz > hSize - pos) return false;
                pos += static_cast<size_t>(pSz);
            }
        } else if (id == 0x03u) { // kAdditionalStreamsInfo
            SevenZStreamsInfo dummy;
            if (!Parse7zStreamsInfo(hdr, hSize, pos, dummy)) return false;
        } else if (id == 0x04u) { // kMainStreamsInfo
            if (!Parse7zStreamsInfo(hdr, hSize, pos, outParsed.mainStreams)) return false;
        } else if (id == 0x05u) { // kFilesInfo
            sawFilesInfo = true;
            if (!Read7zNumberChecked(hdr, hSize, pos, numFiles) || numFiles > 262144ULL) return false;

            while (pos < hSize) {
                uint8_t propId = hdr[pos++];
                if (propId == 0x00u) break;
                uint64_t propSz = 0;
                if (!Read7zNumberChecked(hdr, hSize, pos, propSz) || propSz > hSize - pos) return false;
                size_t pStart = pos;
                size_t pEnd   = pos + static_cast<size_t>(propSz);
                pos = pEnd;

                if (propId == 0x0Eu && numFiles > 0) { // kEmptyStream
                    size_t bp = pStart;
                    if (!Read7zBoolVector(hdr, pEnd, bp, static_cast<size_t>(numFiles), false, emptyStream)) return false;
                } else if (propId == 0x0Fu) { // kEmptyFile
                    size_t emptyCount = 0;
                    for (bool b : emptyStream) if (b) ++emptyCount;
                    size_t bp = pStart;
                    if (!Read7zBoolVector(hdr, pEnd, bp, emptyCount, false, emptyFile)) return false;
                    sawEmptyFileProp = true;
                } else if (propId == 0x11u && propSz >= 1 && hdr[pStart] == 0u) { // kName (UTF-16LE)
                    size_t sp = pStart + 1;
                    while (sp + 2 <= pEnd) {
                        size_t nulPos = sp;
                        while (nulPos + 2 <= pEnd && ByteIO::RdU16LE(hdr + nulPos) != 0u) {
                            nulPos += 2;
                        }
                        const size_t u16Units = (nulPos - sp) / 2u;
                        fileNames.push_back(ReaderText::DecodeUtf16Span(hdr + sp, u16Units, false));
                        sp = nulPos + 2;
                    }
                } else if (propId == 0x14u && numFiles > 0) { // kMTime
                    size_t bp = pStart;
                    if (Read7zBoolVector(hdr, pEnd, bp, static_cast<size_t>(numFiles), true, hasMTime) &&
                        bp < pEnd && hdr[bp++] == 0u)
                    {
                        mtimes.assign(static_cast<size_t>(numFiles), {});
                        for (size_t i = 0; i < numFiles && pEnd - bp >= 8u; ++i) {
                            if (hasMTime[i]) {
                                mtimes[i].lo = ByteIO::RdU32LE(hdr + bp);
                                mtimes[i].hi = ByteIO::RdU32LE(hdr + bp + 4);
                                bp += 8;
                            }
                        }
                    }
                } else if (propId == 0x15u && numFiles > 0) { // kWinAttributes
                    size_t bp = pStart;
                    if (Read7zBoolVector(hdr, pEnd, bp, static_cast<size_t>(numFiles), true, hasWinAttr) &&
                        bp < pEnd && hdr[bp++] == 0u)
                    {
                        winAttrs.assign(static_cast<size_t>(numFiles), 0u);
                        for (size_t i = 0; i < numFiles && pEnd - bp >= 4u; ++i) {
                            if (hasWinAttr[i]) {
                                winAttrs[i] = ByteIO::RdU32LE(hdr + bp);
                                bp += 4;
                            }
                        }
                    }
                }
            }
        } else {
            return false;
        }
    }

    if (!sawFilesInfo || numFiles == 0) return false;

    const size_t count = static_cast<size_t>(numFiles);
    size_t emptyCursor = 0;
    size_t subStreamCursor = 0;

    for (size_t i = 0; i < count; ++i) {
        std::wstring rawFn = (i < fileNames.size()) ? fileNames[i] : (L"file_" + std::to_wstring(i));
        std::wstring norm = ArchivePath::Normalize(rawFn);
        if (norm.empty()) {
            if (i < emptyStream.size() && emptyStream[i]) ++emptyCursor;
            else ++subStreamCursor;
            continue;
        }

        bool isEmpStream = (i < emptyStream.size()) && emptyStream[i];
        bool isEmpFile   = false;
        if (isEmpStream) {
            if (sawEmptyFileProp && emptyCursor < emptyFile.size()) {
                isEmpFile = emptyFile[emptyCursor];
            } else if (i < hasWinAttr.size() && hasWinAttr[i] && (winAttrs[i] & 0x10u) == 0) {
                isEmpFile = true;
            }
            ++emptyCursor;
        }

        bool trailingSlash = !rawFn.empty() && (rawFn.back() == L'/' || rawFn.back() == L'\\');
        bool attrDir = (i < hasWinAttr.size() && hasWinAttr[i] && (winAttrs[i] & 0x10u) != 0);
        bool isDir = trailingSlash || attrDir || (isEmpStream && !isEmpFile);

        SevenZFileSlot slot;
        slot.pathInArchive = norm;
        slot.isDirectory = isDir;
        slot.isEmpty = isEmpStream && isEmpFile;

        if (i < hasMTime.size() && hasMTime[i]) {
            FileTime64ToDosDateTimePortable(mtimes[i].lo, mtimes[i].hi, slot.dosDate, slot.dosTime);
        }

        if (!isEmpStream && !isDir) {
            if (subStreamCursor >= outParsed.mainStreams.subStreams.size()) {
                return false;
            }
            const auto& sub = outParsed.mainStreams.subStreams[subStreamCursor++];
            slot.uncompressedSize = sub.unpackSize;
            slot.compressedSize   = sub.proportionalPackSize;
            slot.hasCrc           = sub.hasCrc;
            slot.crc32            = sub.hasCrc ? sub.crc : 0u;
            slot.folderIndex      = sub.folderIndex;
            slot.offsetInFolder   = sub.offsetInFolder;
        }

        outParsed.files.push_back(std::move(slot));
    }

    if (outParsed.files.empty()) return false;
    outParsed.valid = true;
    return true;
}

inline bool ParseFull7zArchive(const uint8_t* data, size_t size, ParsedSevenZipArchive& outParsed) {
    outParsed = {};
    static const uint8_t k7zSig[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
    if (!data || size < 32 || std::memcmp(data, k7zSig, 6) != 0) return false;

    const uint64_t nextHdrOffset = ByteIO::RdU64LE(data + 12);
    const uint64_t nextHdrSize   = ByteIO::RdU64LE(data + 20);
    if (nextHdrSize == 0 || nextHdrOffset > size - 32u || nextHdrSize > size - 32u - nextHdrOffset) {
        return false;
    }

    const uint8_t* hdr = data + static_cast<size_t>(32ULL + nextHdrOffset);
    const size_t hSize = static_cast<size_t>(nextHdrSize);
    return Parse7zHeaderBlock(data, size, hdr, hSize, 0, outParsed) && outParsed.valid && !outParsed.files.empty();
}

inline bool Parse7zHeaders(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    ParsedSevenZipArchive parsed;
    if (!ParseFull7zArchive(data, size, parsed) || !parsed.valid || parsed.files.empty()) return false;
    outInfo.formatName = L"7-Zip";
    for (const auto& f : parsed.files) {
        ArchiveItemEntry item;
        item.pathInArchive     = f.pathInArchive;
        item.name              = ArchivePath::BaseName(f.pathInArchive);
        item.uncompressedSize  = f.uncompressedSize;
        item.compressedSize    = f.compressedSize;
        item.crc32             = f.crc32;
        item.dosDate           = f.dosDate;
        item.dosTime           = f.dosTime;
        item.isDirectory       = f.isDirectory;
        item.rawDataOffset     = 0ULL;
        item.compressionMethod = 7;
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// 1-slot solid folder cache so consecutive single-entry extractions from a solid .7z archive reuse the folder!
struct Cached7zSolidFolder {
    std::wstring normKey;
    uint64_t fileSize = 0;
    size_t folderIndex = 0;
    std::vector<uint8_t> unpacked;
};
inline std::mutex g_7zFolderCacheMutex;
static Cached7zSolidFolder g_7zFolderCache;

inline bool Extract7zEntryInProcess(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    ParsedSevenZipArchive parsed;
    if (!ParseFull7zArchive(data, size, parsed) || !parsed.valid || parsed.hasEncryptedHeader) return false;

    const std::wstring want = ArchivePath::Normalize(normTarget);
    const SevenZFileSlot* hit = nullptr;
    for (const auto& f : parsed.files) {
        if (!f.isDirectory && _wcsicmp(f.pathInArchive.c_str(), want.c_str()) == 0) {
            hit = &f;
            break;
        }
    }
    if (!hit) return false;
    if (hit->isEmpty || hit->uncompressedSize == 0) {
        outBytes.clear();
        return true;
    }

    const std::wstring normKey = archivePath.empty() ? L"" : PathUtils::NormalizePathKey(archivePath);
    if (!normKey.empty()) {
        std::lock_guard<std::mutex> lock(g_7zFolderCacheMutex);
        if (g_7zFolderCache.normKey == normKey &&
            g_7zFolderCache.fileSize == size &&
            g_7zFolderCache.folderIndex == hit->folderIndex &&
            hit->offsetInFolder <= g_7zFolderCache.unpacked.size() &&
            hit->uncompressedSize <= g_7zFolderCache.unpacked.size() - hit->offsetInFolder)
        {
            const uint8_t* p = g_7zFolderCache.unpacked.data() + static_cast<size_t>(hit->offsetInFolder);
            outBytes.assign(p, p + static_cast<size_t>(hit->uncompressedSize));
            if (hit->hasCrc && Checksum::Crc32(outBytes.data(), outBytes.size()) != hit->crc32) {
                outBytes.clear();
                return false;
            }
            return true;
        }
    }

    std::vector<uint8_t> folderBuf;
    if (!Decode7zFolder(data, size, parsed.mainStreams, hit->folderIndex, folderBuf)) return false;
    if (hit->offsetInFolder > folderBuf.size() || hit->uncompressedSize > folderBuf.size() - hit->offsetInFolder) {
        return false;
    }

    const uint8_t* p = folderBuf.data() + static_cast<size_t>(hit->offsetInFolder);
    outBytes.assign(p, p + static_cast<size_t>(hit->uncompressedSize));
    if (hit->hasCrc && Checksum::Crc32(outBytes.data(), outBytes.size()) != hit->crc32) {
        outBytes.clear();
        return false;
    }

    if (!normKey.empty() && folderBuf.size() <= 64 * 1024 * 1024) {
        std::lock_guard<std::mutex> lock(g_7zFolderCacheMutex);
        g_7zFolderCache.normKey = normKey;
        g_7zFolderCache.fileSize = size;
        g_7zFolderCache.folderIndex = hit->folderIndex;
        g_7zFolderCache.unpacked = std::move(folderBuf);
    }
    return true;
}

} // namespace Explorer2::Formats
