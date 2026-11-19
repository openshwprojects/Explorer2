#pragma once
// Hdf5Data.h — HDF5 Hierarchical Data Format (.h5 / .hdf5) Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 9. HDF5 Hierarchical Data Format (.h5 / .hdf5 / NetCDF-4 / MATLAB v7.3)
// ============================================================================
struct Hdf5Ctx {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint8_t offSize = 8;
    uint8_t lenSize = 8;
    uint64_t baseAddr = 0;
    std::set<uint64_t> visitedObjs;

    uint64_t RdOff(const uint8_t* p) const {
        if (offSize == 4) { uint32_t v = RdU32LE(p); return v == 0xFFFFFFFFu ? UINT64_MAX : v; }
        if (offSize == 2) { uint16_t v = RdU16LE(p); return v == 0xFFFFu ? UINT64_MAX : v; }
        return RdU64LE(p);
    }
    uint64_t RdLen(const uint8_t* p) const {
        if (lenSize == 4) return RdU32LE(p);
        if (lenSize == 2) return RdU16LE(p);
        return RdU64LE(p);
    }
    bool AddrOk(uint64_t relAddr, size_t need) const {
        if (relAddr == UINT64_MAX) return false;
        uint64_t abs = baseAddr + relAddr;
        return abs <= size && need <= size - static_cast<size_t>(abs);
    }
    const uint8_t* Ptr(uint64_t relAddr) const {
        return data + static_cast<size_t>(baseAddr + relAddr);
    }
};

struct Hdf5FilterDesc {
    uint16_t id = 0;    // 1=deflate, 2=shuffle, 3=fletcher32
    uint16_t flags = 0; // bit 0: optional
    std::vector<uint32_t> cdVals;
};

struct Hdf5DatasetInfo {
    std::wstring path;
    std::wstring dtypeDesc;
    uint8_t dtClass = 0xFF; // 0=int, 1=float, 3=string, 6=compound
    uint32_t dtSize = 0;
    bool dtSigned = true;
    bool dtBigEndian = false;
    std::vector<uint64_t> dims;
    std::vector<uint8_t> compactData;
    uint64_t contigAddr = UINT64_MAX;
    uint64_t contigSize = 0;
    // Chunked storage + filter pipeline (#32)
    bool isChunked = false;
    uint8_t chunkLayoutVer = 0;
    uint8_t chunkIdxType = 0; // v4: 1=SingleChunk, 2=Implicit
    uint64_t chunkAddr = UINT64_MAX;
    uint64_t singleChunkCompSize = 0;
    uint32_t singleChunkFilterMask = 0;
    std::vector<uint64_t> chunkDims;
    std::vector<Hdf5FilterDesc> filters;
};

inline std::string ReadHdf5HeapString(const Hdf5Ctx& ctx, uint64_t heapAddr, uint64_t strOff) {
    if (!ctx.AddrOk(heapAddr, 8 + 2 * ctx.lenSize + ctx.offSize)) return {};
    const uint8_t* hp = ctx.Ptr(heapAddr);
    if (std::memcmp(hp, "HEAP", 4) != 0) return {};
    uint64_t segSize = ctx.RdLen(hp + 8);
    uint64_t dataAddr = ctx.RdOff(hp + 8 + 2 * ctx.lenSize);
    if (strOff >= segSize || !ctx.AddrOk(dataAddr + strOff, 1)) return {};
    const uint8_t* sp = ctx.Ptr(dataAddr + strOff);
    size_t maxLen = std::min<size_t>(256, ctx.size - static_cast<size_t>(ctx.baseAddr + dataAddr + strOff));
    size_t n = 0;
    while (n < maxLen && sp[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(sp), n);
}

inline void WalkHdf5Object(Hdf5Ctx& ctx, uint64_t objAddr, const std::wstring& curPath,
    std::vector<Hdf5DatasetInfo>& outDatasets, std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs, int depth);

inline void WalkHdf5SnodOrBtree(Hdf5Ctx& ctx, uint64_t nodeAddr, uint64_t heapAddr,
    const std::wstring& parentPath, std::vector<Hdf5DatasetInfo>& outDatasets,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs, int depth)
{
    if (depth > 20 || !ctx.AddrOk(nodeAddr, 8) || outDatasets.size() >= 64) return;
    const uint8_t* p = ctx.Ptr(nodeAddr);
    if (std::memcmp(p, "SNOD", 4) == 0) {
        uint16_t nSyms = RdU16LE(p + 6);
        size_t entrySize = 2 * ctx.offSize + 4 + 4 + 16;
        size_t cur = 8;
        for (uint16_t i = 0; i < nSyms && ctx.AddrOk(nodeAddr + cur, entrySize); ++i, cur += entrySize) {
            const uint8_t* ep = p + cur;
            uint64_t nameOff = ctx.RdOff(ep);
            uint64_t childHdr = ctx.RdOff(ep + ctx.offSize);
            uint32_t cacheType = RdU32LE(ep + 2 * ctx.offSize);
            std::string nm = ReadHdf5HeapString(ctx, heapAddr, nameOff);
            std::wstring subPath = parentPath == L"/" ? (L"/" + PathUtils::Utf8ToWide(nm)) : (parentPath + L"/" + PathUtils::Utf8ToWide(nm));
            if (cacheType == 1) {
                uint64_t subBtree = ctx.RdOff(ep + 2 * ctx.offSize + 8);
                uint64_t subHeap  = ctx.RdOff(ep + 3 * ctx.offSize + 8);
                outGroupsAndAttrs.emplace_back(subPath, L"Group");
                WalkHdf5SnodOrBtree(ctx, subBtree, subHeap, subPath, outDatasets, outGroupsAndAttrs, depth + 1);
            }
            if (childHdr != UINT64_MAX) {
                WalkHdf5Object(ctx, childHdr, subPath, outDatasets, outGroupsAndAttrs, depth + 1);
            }
        }
    } else if (std::memcmp(p, "TREE", 4) == 0 && p[4] == 0) { // Group B-tree (type 0)
        uint8_t level = p[5];
        uint16_t entriesUsed = RdU16LE(p + 6);
        size_t cur = 8 + 2 * ctx.offSize; // skip left/right sibling addresses
        // Interleaved: key0 (lenSize), child0 (offSize), key1 (lenSize), child1 (offSize), ..., keyN
        for (uint16_t i = 0; i < entriesUsed; ++i) {
            cur += ctx.lenSize; // skip key_i
            if (!ctx.AddrOk(nodeAddr + cur, ctx.offSize)) break;
            uint64_t childAddr = ctx.RdOff(p + cur);
            cur += ctx.offSize;
            if (level == 0) {
                WalkHdf5SnodOrBtree(ctx, childAddr, heapAddr, parentPath, outDatasets, outGroupsAndAttrs, depth + 1);
            } else {
                WalkHdf5SnodOrBtree(ctx, childAddr, heapAddr, parentPath, outDatasets, outGroupsAndAttrs, depth + 1);
            }
        }
    }
}

inline void ParseHdf5HeaderMessage(
    Hdf5Ctx& ctx,
    uint16_t msgType,
    const uint8_t* mp,
    size_t msize,
    const std::wstring& curPath,
    Hdf5DatasetInfo& ds,
    bool& hasDataspace,
    bool& hasDatatype,
    std::vector<std::pair<std::wstring, uint64_t>>& childLinks,
    uint64_t& symBtree,
    uint64_t& symHeap,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs);

inline void WalkHdf5V1Messages(
    Hdf5Ctx& ctx, const uint8_t* p, size_t avail, const std::wstring& curPath,
    Hdf5DatasetInfo& ds, bool& hasDataspace, bool& hasDatatype,
    std::vector<std::pair<std::wstring, uint64_t>>& childLinks,
    uint64_t& symBtree, uint64_t& symHeap,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs, int contDepth)
{
    size_t pos = 0;
    while (pos + 8 <= avail) {
        uint16_t mtype = RdU16LE(p + pos);
        uint16_t msize = RdU16LE(p + pos + 2);
        pos += 8;
        if (msize > avail - pos) break;
        if (mtype == 0x0010 && contDepth < 8 && msize >= static_cast<size_t>(ctx.offSize + ctx.lenSize)) {
            uint64_t cOff = ctx.RdOff(p + pos);
            uint64_t cLen = ctx.RdLen(p + pos + ctx.offSize);
            if (cLen <= 256 * 1024 && ctx.AddrOk(cOff, static_cast<size_t>(cLen))) {
                WalkHdf5V1Messages(ctx, ctx.Ptr(cOff), static_cast<size_t>(cLen), curPath, ds,
                    hasDataspace, hasDatatype, childLinks, symBtree, symHeap, outGroupsAndAttrs, contDepth + 1);
            }
        } else {
            ParseHdf5HeaderMessage(ctx, mtype, p + pos, msize, curPath, ds, hasDataspace, hasDatatype,
                childLinks, symBtree, symHeap, outGroupsAndAttrs);
        }
        pos += (static_cast<size_t>(msize) + 7u) & ~size_t(7u);
    }
}

inline void WalkHdf5V2Messages(
    Hdf5Ctx& ctx, const uint8_t* p, size_t avail, uint8_t ohFlags, const std::wstring& curPath,
    Hdf5DatasetInfo& ds, bool& hasDataspace, bool& hasDatatype,
    std::vector<std::pair<std::wstring, uint64_t>>& childLinks,
    uint64_t& symBtree, uint64_t& symHeap,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs, int contDepth)
{
    size_t hdrExtra = (ohFlags & 0x04u) ? 2 : 0; // creation order tracked
    size_t pos = 0;
    while (pos + 4 + hdrExtra <= avail) {
        uint8_t mtype = p[pos];
        uint16_t msize = RdU16LE(p + pos + 1);
        pos += 4 + hdrExtra;
        if (mtype == 0 && msize == 0) break;
        if (msize > avail - pos) break;
        if (mtype == 0x10 && contDepth < 8 && msize >= static_cast<size_t>(ctx.offSize + ctx.lenSize)) {
            uint64_t cOff = ctx.RdOff(p + pos);
            uint64_t cLen = ctx.RdLen(p + pos + ctx.offSize);
            if (cLen >= 4 && cLen <= 256 * 1024 && ctx.AddrOk(cOff, static_cast<size_t>(cLen))) {
                const uint8_t* cp = ctx.Ptr(cOff);
                if (std::memcmp(cp, "OCHK", 4) == 0) {
                    WalkHdf5V2Messages(ctx, cp + 4, static_cast<size_t>(cLen - 4), ohFlags, curPath, ds,
                        hasDataspace, hasDatatype, childLinks, symBtree, symHeap, outGroupsAndAttrs, contDepth + 1);
                }
            }
        } else {
            ParseHdf5HeaderMessage(ctx, mtype, p + pos, msize, curPath, ds, hasDataspace, hasDatatype,
                childLinks, symBtree, symHeap, outGroupsAndAttrs);
        }
        pos += msize;
    }
}

inline void ParseHdf5HeaderMessage(
    Hdf5Ctx& ctx,
    uint16_t msgType,
    const uint8_t* mp,
    size_t msize,
    const std::wstring& curPath,
    Hdf5DatasetInfo& ds,
    bool& hasDataspace,
    bool& hasDatatype,
    std::vector<std::pair<std::wstring, uint64_t>>& childLinks,
    uint64_t& symBtree,
    uint64_t& symHeap,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs)
{
    if (msgType == 0x0001 && msize >= 4) { // Dataspace
        uint8_t ver = mp[0];
        uint8_t rank = mp[1];
        size_t dimOff = (ver == 1) ? 8 : 4;
        if (ver == 2 && msize >= 4 && mp[3] == 2) { // Null dataspace
            hasDataspace = true;
            return;
        }
        hasDataspace = true;
        ds.dims.clear();
        for (uint8_t d = 0; d < rank && dimOff + ctx.lenSize <= msize; ++d, dimOff += ctx.lenSize) {
            ds.dims.push_back(ctx.RdLen(mp + dimOff));
        }
    } else if (msgType == 0x0003 && msize >= 8) { // Datatype
        hasDatatype = true;
        uint8_t cls = mp[0] & 0x0Fu;
        uint8_t bf0 = mp[1];
        uint32_t sz = RdU32LE(mp + 4);
        ds.dtClass = cls;
        ds.dtSize = sz;
        ds.dtBigEndian = (bf0 & 0x01u) != 0;
        ds.dtSigned = (bf0 & 0x08u) != 0;
        if (cls == 0) ds.dtypeDesc = (ds.dtSigned ? L"int" : L"uint") + std::to_wstring(sz * 8);
        else if (cls == 1) ds.dtypeDesc = L"float" + std::to_wstring(sz * 8);
        else if (cls == 3) ds.dtypeDesc = L"string(" + std::to_wstring(sz) + L")";
        else if (cls == 6) ds.dtypeDesc = L"compound(" + std::to_wstring(sz) + L" B)";
        else if (cls == 9) ds.dtypeDesc = L"vlen";
        else ds.dtypeDesc = L"class(" + std::to_wstring(cls) + L"," + std::to_wstring(sz) + L"B)";
    } else if (msgType == 0x0008 && msize >= 2) { // Data Layout
        uint8_t ver = mp[0];
        if (ver == 3) {
            uint8_t layoutClass = mp[1];
            if (layoutClass == 0 && msize >= 4) { // Compact
                uint16_t csz = RdU16LE(mp + 2);
                if (4u + csz <= msize) ds.compactData.assign(mp + 4, mp + 4 + csz);
            } else if (layoutClass == 1 && msize >= static_cast<size_t>(2 + ctx.offSize + ctx.lenSize)) { // Contiguous
                ds.contigAddr = ctx.RdOff(mp + 2);
                ds.contigSize = ctx.RdLen(mp + 2 + ctx.offSize);
            } else if (layoutClass == 2 && msize >= static_cast<size_t>(3 + ctx.offSize + 4)) { // Chunked v3
                uint8_t nd = mp[2]; // dimensionality = rank + 1 (last is element byte size)
                ds.isChunked = true;
                ds.chunkLayoutVer = 3;
                ds.chunkAddr = ctx.RdOff(mp + 3);
                ds.chunkDims.clear();
                size_t doff = 3 + ctx.offSize;
                for (uint8_t d = 0; d < nd && doff + 4 <= msize; ++d, doff += 4) {
                    ds.chunkDims.push_back(RdU32LE(mp + doff));
                }
            }
        } else if (ver == 4 && msize >= 2) {
            uint8_t layoutClass = mp[1];
            if (layoutClass == 0 && msize >= 4) {
                uint16_t csz = RdU16LE(mp + 2);
                if (4u + csz <= msize) ds.compactData.assign(mp + 4, mp + 4 + csz);
            } else if (layoutClass == 1 && msize >= static_cast<size_t>(2 + ctx.offSize + ctx.lenSize)) {
                ds.contigAddr = ctx.RdOff(mp + 2);
                ds.contigSize = ctx.RdLen(mp + 2 + ctx.offSize);
            } else if (layoutClass == 2 && msize >= 6) { // Chunked v4
                uint8_t lflags = mp[2];
                uint8_t nd = mp[3];
                uint8_t dimBytes = mp[4];
                if (dimBytes >= 1 && dimBytes <= 8 && 5u + static_cast<size_t>(nd) * dimBytes + 1u <= msize) {
                    ds.isChunked = true;
                    ds.chunkLayoutVer = 4;
                    ds.chunkDims.clear();
                    size_t doff = 5;
                    for (uint8_t d = 0; d < nd; ++d, doff += dimBytes) {
                        uint64_t dv = 0;
                        for (uint8_t b = 0; b < dimBytes; ++b) dv |= static_cast<uint64_t>(mp[doff + b]) << (b * 8);
                        ds.chunkDims.push_back(dv);
                    }
                    ds.chunkIdxType = mp[doff++];
                    if (ds.chunkIdxType == 1) { // Single Chunk index
                        if (lflags & 0x02u) { // Filtered single chunk
                            if (doff + ctx.lenSize + 4 + ctx.offSize <= msize) {
                                ds.singleChunkCompSize = ctx.RdLen(mp + doff);
                                ds.singleChunkFilterMask = RdU32LE(mp + doff + ctx.lenSize);
                                ds.chunkAddr = ctx.RdOff(mp + doff + ctx.lenSize + 4);
                            }
                        } else if (doff + ctx.offSize <= msize) {
                            ds.chunkAddr = ctx.RdOff(mp + doff);
                        }
                    } else if (ds.chunkIdxType == 2 && doff + ctx.offSize <= msize) { // Implicit index
                        ds.chunkAddr = ctx.RdOff(mp + doff);
                    }
                }
            }
        } else if ((ver == 1 || ver == 2) && msize >= 4) {
            uint8_t rank = mp[1];
            uint8_t layoutClass = mp[2];
            size_t off = 8;
            if (layoutClass == 1 && off + ctx.offSize <= msize) { // Contiguous v1/v2
                ds.contigAddr = ctx.RdOff(mp + off);
                off += ctx.offSize;
                uint64_t prod = 1;
                for (uint8_t d = 0; d < rank && off + 4 <= msize; ++d, off += 4) prod *= RdU32LE(mp + off);
                ds.contigSize = prod;
            } else if (layoutClass == 2 && off + ctx.offSize <= msize) { // Chunked v1/v2
                ds.isChunked = true;
                ds.chunkLayoutVer = ver;
                ds.chunkAddr = ctx.RdOff(mp + off);
                off += ctx.offSize;
                ds.chunkDims.clear();
                for (uint8_t d = 0; d < rank && off + 4 <= msize; ++d, off += 4) {
                    ds.chunkDims.push_back(RdU32LE(mp + off));
                }
            }
        }
    } else if (msgType == 0x000B && msize >= 2) { // Filter Pipeline (v1 / v2)
        uint8_t ver = mp[0];
        uint8_t nFilt = mp[1];
        size_t pos = (ver == 1) ? 8 : 2;
        if ((ver == 1 || ver == 2) && nFilt <= 16 && pos <= msize) {
            ds.filters.clear();
            for (uint8_t fi = 0; fi < nFilt; ++fi) {
                Hdf5FilterDesc fd;
                if (ver == 1) {
                    if (pos + 8 > msize) break;
                    fd.id = RdU16LE(mp + pos);
                    uint16_t nameLen = RdU16LE(mp + pos + 2);
                    fd.flags = RdU16LE(mp + pos + 4);
                    uint16_t nCd = RdU16LE(mp + pos + 6);
                    pos += 8;
                    if (nameLen > 0) {
                        size_t npad = (static_cast<size_t>(nameLen) + 7u) & ~size_t(7u);
                        if (npad > msize - pos) break;
                        pos += npad;
                    }
                    if (static_cast<size_t>(nCd) * 4u > msize - pos) break;
                    for (uint16_t c = 0; c < nCd; ++c) {
                        fd.cdVals.push_back(RdU32LE(mp + pos + c * 4u));
                    }
                    pos += static_cast<size_t>(nCd) * 4u;
                    if ((nCd & 1u) != 0 && pos + 4 <= msize) pos += 4;
                } else {
                    if (pos + 6 > msize) break;
                    fd.id = RdU16LE(mp + pos);
                    pos += 2;
                    uint16_t nameLen = 0;
                    if (fd.id >= 256) {
                        if (pos + 2 > msize) break;
                        nameLen = RdU16LE(mp + pos);
                        pos += 2;
                    }
                    if (pos + 4 > msize) break;
                    fd.flags = RdU16LE(mp + pos);
                    uint16_t nCd = RdU16LE(mp + pos + 2);
                    pos += 4;
                    if (nameLen > msize - pos) break;
                    pos += nameLen;
                    if (static_cast<size_t>(nCd) * 4u > msize - pos) break;
                    for (uint16_t c = 0; c < nCd; ++c) {
                        fd.cdVals.push_back(RdU32LE(mp + pos + c * 4u));
                    }
                    pos += static_cast<size_t>(nCd) * 4u;
                }
                ds.filters.push_back(std::move(fd));
            }
        }
    } else if (msgType == 0x0011 && msize >= static_cast<size_t>(2 * ctx.offSize)) { // Symbol Table
        symBtree = ctx.RdOff(mp);
        symHeap  = ctx.RdOff(mp + ctx.offSize);
    } else if (msgType == 0x0006 && msize >= 4 && mp[0] == 1) { // Link message (v2 groups)
        uint8_t flags = mp[1];
        size_t pos = 2;
        uint8_t linkType = 0; // 0 = hard link
        if (flags & 0x08u) { if (pos >= msize) return; linkType = mp[pos++]; }
        if (flags & 0x04u) pos += 8; // creation order
        if (flags & 0x10u) pos += 1; // link name charset
        size_t nameLenBytes = size_t(1) << (flags & 0x03u);
        if (pos + nameLenBytes > msize) return;
        uint64_t nameLen = (nameLenBytes == 1) ? mp[pos] : (nameLenBytes == 2 ? RdU16LE(mp + pos) : (nameLenBytes == 4 ? RdU32LE(mp + pos) : RdU64LE(mp + pos)));
        pos += nameLenBytes;
        if (nameLen > msize - pos) return;
        std::string linkName(reinterpret_cast<const char*>(mp + pos), static_cast<size_t>(nameLen));
        pos += static_cast<size_t>(nameLen);
        if (linkType == 0 && pos + ctx.offSize <= msize) {
            uint64_t targetAddr = ctx.RdOff(mp + pos);
            std::wstring sub = curPath == L"/" ? (L"/" + PathUtils::Utf8ToWide(linkName)) : (curPath + L"/" + PathUtils::Utf8ToWide(linkName));
            childLinks.emplace_back(sub, targetAddr);
        }
    } else if (msgType == 0x000C && msize >= 8) { // Attribute
        uint8_t ver = mp[0];
        size_t pos = (ver == 1) ? 2 : 2;
        if (ver == 1 && pos + 6 <= msize) {
            uint16_t nl = RdU16LE(mp + 2);
            uint16_t dtl = RdU16LE(mp + 4);
            uint16_t dsl = RdU16LE(mp + 6);
            pos = 8;
            if (nl > 0 && nl <= msize - pos) {
                std::wstring aname = CleanAsciiFixed(mp + pos, nl);
                size_t dtOff = pos + ((nl + 7u) & ~size_t(7u));
                size_t dsOff = dtOff + ((dtl + 7u) & ~size_t(7u));
                size_t valOff = dsOff + ((dsl + 7u) & ~size_t(7u));
                std::wstring aval;
                if (dtOff + 8 <= msize && valOff < msize) {
                    uint8_t acls = mp[dtOff] & 0x0Fu;
                    uint32_t asz = RdU32LE(mp + dtOff + 4);
                    if (acls == 3 && valOff + asz <= msize) aval = CleanAsciiFixed(mp + valOff, asz);
                    else if (acls == 0 && asz == 4 && valOff + 4 <= msize) aval = std::to_wstring(RdI32LE(mp + valOff));
                    else if (acls == 1 && asz == 8 && valOff + 8 <= msize) aval = FmtDouble(RdF64LE(mp + valOff));
                }
                if (!aname.empty() && outGroupsAndAttrs.size() < 200) {
                    outGroupsAndAttrs.emplace_back(curPath + L"@" + aname, aval.empty() ? L"(attribute)" : aval);
                }
            }
        } else if ((ver == 2 || ver == 3) && pos + 6 <= msize) {
            uint16_t nl = RdU16LE(mp + 2);
            uint16_t dtl = RdU16LE(mp + 4);
            uint16_t dsl = RdU16LE(mp + 6);
            pos = (ver == 3) ? 9 : 8;
            if (nl > 0 && nl <= msize - pos) {
                std::wstring aname = CleanAsciiFixed(mp + pos, nl);
                size_t dtOff = pos + nl;
                size_t dsOff = dtOff + dtl;
                size_t valOff = dsOff + dsl;
                std::wstring aval;
                if (dtOff + 8 <= msize && valOff < msize) {
                    uint8_t acls = mp[dtOff] & 0x0Fu;
                    uint32_t asz = RdU32LE(mp + dtOff + 4);
                    if (acls == 3 && valOff + asz <= msize) aval = CleanAsciiFixed(mp + valOff, asz);
                }
                if (!aname.empty() && outGroupsAndAttrs.size() < 200) {
                    outGroupsAndAttrs.emplace_back(curPath + L"@" + aname, aval.empty() ? L"(attribute)" : aval);
                }
            }
        }
    }
}

inline void WalkHdf5Object(
    Hdf5Ctx& ctx,
    uint64_t objAddr,
    const std::wstring& curPath,
    std::vector<Hdf5DatasetInfo>& outDatasets,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs,
    int depth)
{
    if (depth > 20 || !ctx.visitedObjs.insert(objAddr).second || outDatasets.size() >= 64) return;
    if (!ctx.AddrOk(objAddr, 12)) return;
    const uint8_t* p = ctx.Ptr(objAddr);

    Hdf5DatasetInfo ds;
    ds.path = curPath;
    bool hasDataspace = false, hasDatatype = false;
    std::vector<std::pair<std::wstring, uint64_t>> childLinks;
    uint64_t symBtree = UINT64_MAX, symHeap = UINT64_MAX;

    if (std::memcmp(p, "OHDR", 4) == 0 && p[4] == 2) { // v2 Object Header
        uint8_t flags = p[5];
        size_t pos = 6;
        if (flags & 0x20u) pos += 16; // access/mod/change/birth times
        if (flags & 0x10u) pos += 4;  // attr phase change values
        size_t chunkLenBytes = size_t(1) << (flags & 0x03u);
        if (!ctx.AddrOk(objAddr + pos, chunkLenBytes)) return;
        uint64_t chunk0Size = (chunkLenBytes == 1) ? p[pos] : (chunkLenBytes == 2 ? RdU16LE(p + pos) : (chunkLenBytes == 4 ? RdU32LE(p + pos) : RdU64LE(p + pos)));
        pos += chunkLenBytes;
        if (chunk0Size > 256 * 1024 || !ctx.AddrOk(objAddr + pos, static_cast<size_t>(chunk0Size))) return;
        WalkHdf5V2Messages(ctx, p + pos, static_cast<size_t>(chunk0Size), flags, curPath, ds,
            hasDataspace, hasDatatype, childLinks, symBtree, symHeap, outGroupsAndAttrs, 0);
    } else if (p[0] == 1) { // v1 Object Header
        uint32_t objHdrSize = RdU32LE(p + 8);
        size_t pos = 16; // 12B v1 header + 4B alignment padding
        if (objHdrSize == 0 || objHdrSize > 256 * 1024 || !ctx.AddrOk(objAddr + pos, objHdrSize)) return;
        WalkHdf5V1Messages(ctx, p + pos, objHdrSize, curPath, ds,
            hasDataspace, hasDatatype, childLinks, symBtree, symHeap, outGroupsAndAttrs, 0);
    } else {
        return;
    }

    if (hasDataspace && hasDatatype) {
        outDatasets.push_back(std::move(ds));
    } else if (!curPath.empty()) {
        outGroupsAndAttrs.emplace_back(curPath, L"Group");
    }

    if (symBtree != UINT64_MAX && symHeap != UINT64_MAX) {
        WalkHdf5SnodOrBtree(ctx, symBtree, symHeap, curPath, outDatasets, outGroupsAndAttrs, depth + 1);
    }
    for (const auto& lk : childLinks) {
        WalkHdf5Object(ctx, lk.second, lk.first, outDatasets, outGroupsAndAttrs, depth + 1);
    }
}

inline bool ApplyHdf5ChunkFiltersReverse(
    const uint8_t* src,
    size_t srcLen,
    uint32_t filterMask,
    size_t expectedUncompBytes,
    const Hdf5DatasetInfo& ds,
    std::vector<uint8_t>& outBuf)
{
    outBuf.assign(src, src + srcLen);
    if (ds.filters.empty()) return true;

    const size_t maxCap = std::max<size_t>(expectedUncompBytes + 65536, 65536);
    for (int fi = static_cast<int>(ds.filters.size()) - 1; fi >= 0; --fi) {
        if ((filterMask & (1u << fi)) != 0) continue; // Filter was bypassed on write
        const auto& f = ds.filters[static_cast<size_t>(fi)];
        if (f.id == 1) { // H5Z_FILTER_DEFLATE (zlib / deflate)
            std::vector<uint8_t> dec;
            if (!ZipDeflate::InflateZlib(outBuf.data(), outBuf.size(), dec, maxCap) &&
                !ZipDeflate::InflateRaw(outBuf.data(), outBuf.size(), dec, maxCap))
            {
                return false;
            }
            outBuf = std::move(dec);
        } else if (f.id == 2) { // H5Z_FILTER_SHUFFLE (byte-unshuffle)
            size_t elemSz = (!f.cdVals.empty() && f.cdVals[0] > 0) ? static_cast<size_t>(f.cdVals[0]) : static_cast<size_t>(ds.dtSize);
            if (elemSz > 1 && outBuf.size() >= elemSz) {
                size_t nElems = outBuf.size() / elemSz;
                std::vector<uint8_t> unshuf(outBuf.size());
                for (size_t i = 0; i < nElems; ++i) {
                    for (size_t b = 0; b < elemSz; ++b) {
                        unshuf[i * elemSz + b] = outBuf[b * nElems + i];
                    }
                }
                size_t remOff = nElems * elemSz;
                if (remOff < outBuf.size()) {
                    std::memcpy(unshuf.data() + remOff, outBuf.data() + remOff, outBuf.size() - remOff);
                }
                outBuf = std::move(unshuf);
            }
        } else if (f.id == 3) { // H5Z_FILTER_FLETCHER32 (4-byte trailing checksum)
            if (outBuf.size() < 4) return false;
            outBuf.resize(outBuf.size() - 4);
        } else if ((f.flags & 0x0001u) == 0) {
            return false; // Unsupported mandatory filter
        }
    }
    return outBuf.size() >= expectedUncompBytes || expectedUncompBytes == 0;
}

inline void ScatterHdf5ChunkToLinear(
    const uint8_t* chunkData,
    size_t chunkDataBytes,
    const std::vector<uint64_t>& coords,
    const std::vector<uint64_t>& cDims,
    const std::vector<uint64_t>& dsDims,
    uint32_t elemBytes,
    std::vector<uint8_t>& dstLinear)
{
    if (elemBytes == 0 || dstLinear.empty()) return;
    const size_t rank = dsDims.size();
    if (rank == 0) {
        if (chunkDataBytes >= elemBytes && dstLinear.size() >= elemBytes) {
            std::memcpy(dstLinear.data(), chunkData, elemBytes);
        }
        return;
    }
    if (coords.size() < rank || cDims.size() < rank) return;

    // General N-D row-major coordinate scatter (exact for 1D, 2D, 3D, N-D!)
    uint64_t chunkTotalElems = 1;
    for (size_t d = 0; d < rank; ++d) {
        if (cDims[d] == 0) return;
        chunkTotalElems *= cDims[d];
    }
    const size_t maxSrcElems = std::min<size_t>(static_cast<size_t>(chunkTotalElems), chunkDataBytes / elemBytes);
    const size_t maxDstElems = dstLinear.size() / elemBytes;

    std::vector<uint64_t> dsStrides(rank, 1);
    std::vector<uint64_t> chStrides(rank, 1);
    for (size_t d = rank - 1; d > 0; --d) {
        dsStrides[d - 1] = dsStrides[d] * dsDims[d];
        chStrides[d - 1] = chStrides[d] * cDims[d];
    }

    for (size_t lin = 0; lin < maxSrcElems; ++lin) {
        uint64_t rem = lin;
        uint64_t dstIdx = 0;
        bool inBounds = true;
        for (size_t d = 0; d < rank; ++d) {
            uint64_t localC = rem / chStrides[d];
            rem %= chStrides[d];
            uint64_t globalC = coords[d] + localC;
            if (globalC >= dsDims[d]) { inBounds = false; break; }
            dstIdx += globalC * dsStrides[d];
        }
        if (inBounds && dstIdx < maxDstElems) {
            std::memcpy(dstLinear.data() + static_cast<size_t>(dstIdx) * elemBytes,
                        chunkData + lin * elemBytes, elemBytes);
        }
    }
}

inline bool WalkHdf5ChunkBTreeV1(
    const Hdf5Ctx& ctx,
    uint64_t nodeAddr,
    const Hdf5DatasetInfo& ds,
    const std::vector<uint64_t>& cDims,
    size_t uncompChunkBytes,
    std::vector<uint8_t>& dstLinear,
    size_t& decodedChunks,
    int depth)
{
    if (depth > 16 || !ctx.AddrOk(nodeAddr, 8 + 2 * ctx.offSize)) return false;
    const uint8_t* p = ctx.Ptr(nodeAddr);
    if (std::memcmp(p, "TREE", 4) != 0 || p[4] != 1) return false; // Must be B-tree type 1 (Raw Data Chunks)
    uint8_t level = p[5];
    uint16_t entriesUsed = RdU16LE(p + 6);
    if (entriesUsed == 0 || entriesUsed > 16384) return false;

    const size_t keyDimCnt = ds.dims.size() + 1; // HDF5 v1 chunk B-tree stores rank + 1 uint64 offsets
    const size_t keySize = 8 + keyDimCnt * 8;
    size_t cur = 8 + 2 * ctx.offSize;

    for (uint16_t i = 0; i < entriesUsed && decodedChunks < 4096; ++i) {
        if (!ctx.AddrOk(nodeAddr + cur, keySize + ctx.offSize)) return false;
        const uint8_t* kp = p + cur;
        uint32_t chunkByteSize = RdU32LE(kp);
        uint32_t filterMask    = RdU32LE(kp + 4);
        std::vector<uint64_t> coords(ds.dims.size(), 0);
        for (size_t d = 0; d < ds.dims.size(); ++d) {
            coords[d] = RdU64LE(kp + 8 + d * 8);
        }
        cur += keySize;
        uint64_t childAddr = ctx.RdOff(p + cur);
        cur += ctx.offSize;

        if (level > 0) {
            if (!WalkHdf5ChunkBTreeV1(ctx, childAddr, ds, cDims, uncompChunkBytes, dstLinear, decodedChunks, depth + 1)) {
                return false;
            }
        } else {
            if (chunkByteSize == 0 || chunkByteSize > 32 * 1024 * 1024 || !ctx.AddrOk(childAddr, chunkByteSize)) {
                return false;
            }
            std::vector<uint8_t> decChunk;
            if (!ApplyHdf5ChunkFiltersReverse(ctx.Ptr(childAddr), chunkByteSize, filterMask, uncompChunkBytes, ds, decChunk)) {
                return false;
            }
            ScatterHdf5ChunkToLinear(decChunk.data(), decChunk.size(), coords, cDims, ds.dims, ds.dtSize, dstLinear);
            ++decodedChunks;
        }
    }
    return decodedChunks > 0;
}

inline bool ReconstructHdf5ChunkedDataset(
    const Hdf5Ctx& ctx,
    const Hdf5DatasetInfo& ds,
    uint64_t totalElems,
    std::vector<uint8_t>& outBuf)
{
    outBuf.clear();
    if (!ds.isChunked || ds.dtSize == 0 || ds.chunkAddr == UINT64_MAX) return false;

    std::vector<uint64_t> cDims = ds.chunkDims;
    if (cDims.size() == ds.dims.size() + 1) cDims.pop_back();
    if (cDims.size() != ds.dims.size()) return false;

    uint64_t chunkElems = 1;
    for (uint64_t cd : cDims) {
        if (cd == 0 || cd > 64 * 1024 * 1024) return false;
        chunkElems *= cd;
        if (chunkElems > 64 * 1024 * 1024) return false;
    }
    const size_t uncompChunkBytes = static_cast<size_t>(chunkElems) * ds.dtSize;
    const uint64_t cappedElems = std::min<uint64_t>(totalElems, (16ULL * 1024ULL * 1024ULL) / ds.dtSize);
    if (cappedElems == 0) return false;
    outBuf.assign(static_cast<size_t>(cappedElems) * ds.dtSize, 0);

    if (ds.chunkLayoutVer == 4 && ds.chunkIdxType == 1) { // v4 Single Chunk
        size_t compSz = (ds.singleChunkCompSize > 0) ? static_cast<size_t>(ds.singleChunkCompSize) : uncompChunkBytes;
        if (compSz == 0 || compSz > 32 * 1024 * 1024 || !ctx.AddrOk(ds.chunkAddr, compSz)) return false;
        std::vector<uint8_t> dec;
        if (!ApplyHdf5ChunkFiltersReverse(ctx.Ptr(ds.chunkAddr), compSz, ds.singleChunkFilterMask, uncompChunkBytes, ds, dec)) {
            return false;
        }
        std::vector<uint64_t> zeroCoords(ds.dims.size(), 0);
        ScatterHdf5ChunkToLinear(dec.data(), dec.size(), zeroCoords, cDims, ds.dims, ds.dtSize, outBuf);
        return true;
    }

    size_t decodedChunks = 0;
    return WalkHdf5ChunkBTreeV1(ctx, ds.chunkAddr, ds, cDims, uncompChunkBytes, outBuf, decodedChunks, 0);
}

inline bool ParseHdf5Doc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    static const uint8_t kSig[8] = { 0x89, 'H', 'D', 'F', '\r', '\n', 0x1A, '\n' };
    size_t sbOff = SIZE_MAX;
    for (size_t cand : { size_t(0), size_t(512), size_t(1024), size_t(2048) }) {
        if (cand + 36 <= raw.size() && std::memcmp(raw.data() + cand, kSig, 8) == 0) {
            sbOff = cand;
            break;
        }
    }
    if (sbOff == SIZE_MAX) return false;

    const uint8_t* sb = raw.data() + sbOff;
    uint8_t sbVer = sb[8];
    if (sbVer > 3) return false;

    Hdf5Ctx ctx;
    ctx.data = raw.data();
    ctx.size = raw.size();

    uint64_t rootObjAddr = UINT64_MAX;
    uint64_t rootBtree = UINT64_MAX, rootHeap = UINT64_MAX;

    if (sbVer == 0 || sbVer == 1) {
        ctx.offSize = sb[13];
        ctx.lenSize = sb[14];
        if ((ctx.offSize != 2 && ctx.offSize != 4 && ctx.offSize != 8) ||
            (ctx.lenSize != 2 && ctx.lenSize != 4 && ctx.lenSize != 8)) return false;
        size_t pos = 24;
        if (sbVer == 1) pos = 28;
        if (sbOff + pos + 4 * ctx.offSize + 24 > raw.size()) return false;
        ctx.baseAddr = ctx.RdOff(sb + pos);
        if (ctx.baseAddr == UINT64_MAX || ctx.baseAddr >= raw.size()) ctx.baseAddr = sbOff;
        pos += 4 * ctx.offSize;
        rootObjAddr = ctx.RdOff(sb + pos + ctx.offSize);
        uint32_t cacheType = RdU32LE(sb + pos + 2 * ctx.offSize);
        if (cacheType == 1 && sbOff + pos + 4 * ctx.offSize + 8 <= raw.size()) {
            rootBtree = ctx.RdOff(sb + pos + 2 * ctx.offSize + 8);
            rootHeap  = ctx.RdOff(sb + pos + 3 * ctx.offSize + 8);
        }
    } else {
        ctx.offSize = sb[9];
        ctx.lenSize = sb[10];
        if ((ctx.offSize != 2 && ctx.offSize != 4 && ctx.offSize != 8) ||
            (ctx.lenSize != 2 && ctx.lenSize != 4 && ctx.lenSize != 8)) return false;
        if (sbOff + 12 + 4 * ctx.offSize > raw.size()) return false;
        ctx.baseAddr = ctx.RdOff(sb + 12);
        if (ctx.baseAddr == UINT64_MAX || ctx.baseAddr >= raw.size()) ctx.baseAddr = sbOff;
        rootObjAddr = ctx.RdOff(sb + 12 + 3 * ctx.offSize);
    }

    std::vector<Hdf5DatasetInfo> datasets;
    std::vector<std::pair<std::wstring, std::wstring>> groupsAndAttrs;
    if (rootObjAddr != UINT64_MAX) {
        WalkHdf5Object(ctx, rootObjAddr, L"/", datasets, groupsAndAttrs, 0);
    }
    if (rootBtree != UINT64_MAX && rootHeap != UINT64_MAX) {
        WalkHdf5SnodOrBtree(ctx, rootBtree, rootHeap, L"/", datasets, groupsAndAttrs, 0);
    }
    if (datasets.empty() && groupsAndAttrs.empty()) {
        return false; // Corrupt/truncated HDF5 superblock with no valid root group or objects
    }

    SpreadsheetSheet overview;
    overview.name = L"HDF5_Datasets";
    overview.rows.push_back({ L"Object Path", L"Type", L"Shape", L"Storage / Preview" });
    overview.rows.push_back({ L"(superblock)", L"HDF5 v" + std::to_wstring(sbVer),
        L"off=" + std::to_wstring(ctx.offSize) + L"B, len=" + std::to_wstring(ctx.lenSize) + L"B",
        PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });

    std::vector<SpreadsheetSheet> dsSheets;
    std::vector<double> ncLatVals, ncLonVals;

    for (const auto& ds : datasets) {
        std::wstring shStr = L"[";
        uint64_t totalElems = 1;
        for (size_t i = 0; i < ds.dims.size(); ++i) {
            if (i > 0) shStr += L", ";
            shStr += std::to_wstring(ds.dims[i]);
            totalElems *= ds.dims[i];
        }
        shStr += L"]";

        std::vector<uint8_t> chunkReconstructed;
        const uint8_t* rawBuf = nullptr;
        size_t rawAvail = 0;
        if (!ds.compactData.empty()) {
            rawBuf = ds.compactData.data();
            rawAvail = ds.compactData.size();
        } else if (ds.isChunked && totalElems > 0) {
            if (!ReconstructHdf5ChunkedDataset(ctx, ds, totalElems, chunkReconstructed)) {
                return false; // Corrupt chunk B-tree or corrupt deflate/shuffle stream!
            }
            rawBuf = chunkReconstructed.data();
            rawAvail = chunkReconstructed.size();
        } else if (ds.contigAddr != UINT64_MAX) {
            if (!ctx.AddrOk(ds.contigAddr, 1)) return false;
            rawBuf = ctx.Ptr(ds.contigAddr);
            rawAvail = std::min<uint64_t>(ds.contigSize > 0 ? ds.contigSize : (ctx.size - (ctx.baseAddr + ds.contigAddr)),
                                          ctx.size - static_cast<size_t>(ctx.baseAddr + ds.contigAddr));
        }

        auto rdDoubleVal = [&](size_t idx) -> double {
            const uint8_t* p = rawBuf + idx * ds.dtSize;
            if (ds.dtClass == 1) {
                if (ds.dtSize == 2) return HalfToFloat(ds.dtBigEndian ? RdU16BE(p) : RdU16LE(p));
                if (ds.dtSize == 4) return ds.dtBigEndian ? RdF32BE(p) : RdF32LE(p);
                if (ds.dtSize == 8) return ds.dtBigEndian ? RdF64BE(p) : RdF64LE(p);
            } else if (ds.dtClass == 0) {
                if (ds.dtSigned) {
                    return static_cast<double>(
                        (ds.dtSize == 1) ? static_cast<int8_t>(p[0]) :
                        (ds.dtSize == 2) ? (ds.dtBigEndian ? RdI16BE(p) : RdI16LE(p)) :
                        (ds.dtSize == 4) ? (ds.dtBigEndian ? RdI32BE(p) : RdI32LE(p)) :
                                           (ds.dtBigEndian ? RdI64BE(p) : RdI64LE(p)));
                }
                return static_cast<double>(
                    (ds.dtSize == 1) ? p[0] :
                    (ds.dtSize == 2) ? (ds.dtBigEndian ? RdU16BE(p) : RdU16LE(p)) :
                    (ds.dtSize == 4) ? (ds.dtBigEndian ? RdU32BE(p) : RdU32LE(p)) :
                                       (ds.dtBigEndian ? RdU64BE(p) : RdU64LE(p)));
            }
            return 0.0;
        };

        auto rdVal = [&](size_t idx) -> std::wstring {
            const uint8_t* p = rawBuf + idx * ds.dtSize;
            if (ds.dtClass == 1) {
                return FmtDouble(rdDoubleVal(idx));
            } else if (ds.dtClass == 0) {
                if (ds.dtSigned) {
                    int64_t v = (ds.dtSize == 1) ? static_cast<int8_t>(p[0]) :
                                (ds.dtSize == 2) ? (ds.dtBigEndian ? RdI16BE(p) : RdI16LE(p)) :
                                (ds.dtSize == 4) ? (ds.dtBigEndian ? RdI32BE(p) : RdI32LE(p)) :
                                                   (ds.dtBigEndian ? RdI64BE(p) : RdI64LE(p));
                    return std::to_wstring(v);
                } else {
                    uint64_t v = (ds.dtSize == 1) ? p[0] :
                                 (ds.dtSize == 2) ? (ds.dtBigEndian ? RdU16BE(p) : RdU16LE(p)) :
                                 (ds.dtSize == 4) ? (ds.dtBigEndian ? RdU32BE(p) : RdU32LE(p)) :
                                                    (ds.dtBigEndian ? RdU64BE(p) : RdU64LE(p));
                    return std::to_wstring(v);
                }
            } else if (ds.dtClass == 3) {
                return CleanAsciiFixed(p, ds.dtSize);
            }
            return L"";
        };

        std::wstring preview;
        if (rawBuf && ds.dtSize > 0 && rawAvail >= ds.dtSize) {
            size_t cnt = static_cast<size_t>(std::min<uint64_t>(totalElems, rawAvail / ds.dtSize));
            for (size_t i = 0; i < std::min<size_t>(cnt, 4); ++i) {
                if (i > 0) preview += L", ";
                preview += rdVal(i);
            }

            // Capture lat/lon coordinate vectors for NetCDF-4 map tracks
            std::wstring lowLeaf = PathUtils::ToLower(ds.path);
            size_t slash = lowLeaf.find_last_of(L'/');
            if (slash != std::wstring::npos) lowLeaf = lowLeaf.substr(slash + 1);
            if ((ds.dtClass == 0 || ds.dtClass == 1) && ds.dims.size() == 1 && cnt > 0) {
                if (lowLeaf == L"lat" || lowLeaf == L"latitude") {
                    for (size_t i = 0; i < std::min<size_t>(cnt, 2000); ++i) ncLatVals.push_back(rdDoubleVal(i));
                } else if (lowLeaf == L"lon" || lowLeaf == L"longitude") {
                    for (size_t i = 0; i < std::min<size_t>(cnt, 2000); ++i) ncLonVals.push_back(rdDoubleVal(i));
                }
            }

            if (dsSheets.size() < 8 && cnt > 0 && (ds.dtClass == 0 || ds.dtClass == 1 || ds.dtClass == 3)) {
                SpreadsheetSheet dsh;
                dsh.name = ds.path.size() > 1 ? ds.path.substr(1) : L"Dataset";
                if (ds.dims.size() == 2 && ds.dims[0] > 0 && ds.dims[1] > 0) {
                    size_t nr = static_cast<size_t>(std::min<uint64_t>(ds.dims[0], 128));
                    size_t nc = static_cast<size_t>(std::min<uint64_t>(ds.dims[1], 32));
                    std::vector<std::wstring> hr = { L"Row \\ Col" };
                    for (size_t c = 0; c < nc; ++c) hr.push_back(L"[" + std::to_wstring(c) + L"]");
                    dsh.rows.push_back(std::move(hr));
                    for (size_t r = 0; r < nr; ++r) {
                        std::vector<std::wstring> rv = { L"[" + std::to_wstring(r) + L"]" };
                        for (size_t c = 0; c < nc; ++c) {
                            uint64_t idx = r * ds.dims[1] + c;
                            rv.push_back(idx < cnt ? rdVal(static_cast<size_t>(idx)) : L"");
                        }
                        dsh.rows.push_back(std::move(rv));
                    }
                } else {
                    dsh.rows.push_back({ L"Index", ds.path + L" (" + ds.dtypeDesc + L")" });
                    for (size_t i = 0; i < std::min<size_t>(cnt, 256); ++i) {
                        dsh.rows.push_back({ std::to_wstring(i), rdVal(i) });
                    }
                }
                FinalizeSheetCols(dsh);
                dsSheets.push_back(std::move(dsh));
            }
        } else {
            preview = L"(empty/unallocated)";
        }
        overview.rows.push_back({ ds.path, ds.dtypeDesc, shStr, preview });
    }
    for (const auto& ga : groupsAndAttrs) {
        if (overview.rows.size() >= 400) break;
        overview.rows.push_back({ ga.first, ga.second == L"Group" ? L"Group" : L"Attribute", L"", ga.second });
    }
    FinalizeSheetCols(overview);

    if (!ncLatVals.empty() && ncLatVals.size() == ncLonVals.size()) {
        for (size_t i = 0; i < ncLatVals.size(); ++i) {
            double lonNorm = (ncLonVals[i] > 180.0 && ncLonVals[i] <= 360.0) ? (ncLonVals[i] - 360.0) : ncLonVals[i];
            if (std::fabs(ncLatVals[i]) <= 90.0 && std::fabs(lonNorm) <= 180.0) {
                GpsTrackPoint gp;
                gp.lat = ncLatVals[i];
                gp.lon = lonNorm;
                gp.rowIndex = static_cast<int>(i + 1);
                out.geoPoints.push_back(std::move(gp));
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = (extLower == L".mat") ? L"MATLAB v7.3 HDF5 Workspace (.mat)" :
                      (extLower == L".nc" || extLower == L".cdf") ? L"NetCDF-4 HDF5 Dataset (" + extLower + L")" :
                      L"HDF5 Hierarchical Data Format (" + extLower + L")";
    out.sheets.push_back(std::move(overview));
    for (auto& s : dsSheets) out.sheets.push_back(std::move(s));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseHdf5Doc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParseHdf5Doc(raw, extLower, out); }

} // namespace Formats
