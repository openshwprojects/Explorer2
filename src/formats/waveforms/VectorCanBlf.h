#pragma once
// VectorCanBlf.h — Vector Binary Logging Format CAN/CAN-FD (.blf) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailCan {

inline void WalkBlfObjects(const uint8_t* buf, size_t sz, std::vector<CanFrame>& outFrames, int depth) {
    if (!buf || sz < 16 || depth > 4) return;
    size_t pos = 0;
    while (pos + 16 <= sz && outFrames.size() < 20000) {
        if (std::memcmp(buf + pos, "LOBJ", 4) != 0) {
            // Try 4-byte alignment step if previous object wasn't padded
            size_t next = (pos + 3) & ~size_t(3);
            if (next > pos && next + 16 <= sz && std::memcmp(buf + next, "LOBJ", 4) == 0) {
                pos = next;
            } else {
                break;
            }
        }
        uint16_t hdrSize = DetailEda100::RdU16LE(buf + pos + 4);
        uint32_t objSize = DetailEda100::RdU32LE(buf + pos + 8);
        uint32_t objType = DetailEda100::RdU32LE(buf + pos + 12);
        if (hdrSize < 16 || objSize < hdrSize || objSize > sz - pos) break;

        if (objType == 10) { // BL_OBJ_TYPE_LOG_CONTAINER
            if (objSize > static_cast<uint32_t>(hdrSize) + 16u) {
                const uint8_t* cpay = buf + pos + hdrSize;
                size_t cpayLen = objSize - hdrSize;
                uint16_t compMethod = DetailEda100::RdU16LE(cpay);
                uint32_t uncompLen  = DetailEda100::RdU32LE(cpay + 8);
                const uint8_t* inner = cpay + 16;
                size_t innerLen = cpayLen - 16;
                if (compMethod == 0) {
                    WalkBlfObjects(inner, innerLen, outFrames, depth + 1);
                } else if (compMethod == 2 && innerLen >= 2) {
                    size_t cap = std::clamp<size_t>(uncompLen > 0 ? uncompLen + 64 : 16 * 1024 * 1024, 1024, 32 * 1024 * 1024);
                    std::vector<uint8_t> decomp;
                    if (ZipDeflate::InflateZlib(inner, innerLen, decomp, cap) && !decomp.empty()) {
                        WalkBlfObjects(decomp.data(), decomp.size(), outFrames, depth + 1);
                    }
                }
            }
        } else if ((objType == 1 || objType == 86 || objType == 100 || objType == 101) && hdrSize >= 32) {
            uint32_t objFlags = DetailEda100::RdU32LE(buf + pos + 16);
            uint64_t tsRaw    = DetailEda100::RdU64LE(buf + pos + 24);
            double tsSec = (objFlags == 1) ? (static_cast<double>(tsRaw) * 1e-5) : (static_cast<double>(tsRaw) * 1e-9);
            const uint8_t* pay = buf + pos + hdrSize;
            size_t payLen = objSize - hdrSize;

            if ((objType == 1 || objType == 86) && payLen >= 16) { // CAN_MESSAGE / CAN_MESSAGE2
                CanFrame f;
                f.timestamp = tsSec;
                f.channel = DetailEda100::RdU16LE(pay);
                f.isTx = (pay[2] & 0x01) != 0;
                f.dlcBytes = std::min<uint8_t>(pay[3], 8);
                uint32_t rawId = DetailEda100::RdU32LE(pay + 4);
                f.extId = (rawId & 0x80000000u) != 0;
                f.canId = rawId & 0x1FFFFFFFu;
                std::memcpy(f.data, pay + 8, f.dlcBytes);
                outFrames.push_back(f);
            } else if (objType == 100 && payLen >= 20) { // CAN_FD_MESSAGE
                CanFrame f;
                f.timestamp = tsSec;
                f.channel = DetailEda100::RdU16LE(pay);
                f.isTx = (pay[2] & 0x01) != 0;
                f.isFd = true;
                uint8_t validBytes = (pay[14] > 0) ? pay[14] : CanFdDlcToBytes(pay[3]);
                f.dlcBytes = static_cast<uint8_t>(std::min<size_t>({ validBytes, size_t(64), payLen - 20 }));
                uint32_t rawId = DetailEda100::RdU32LE(pay + 4);
                f.extId = (rawId & 0x80000000u) != 0;
                f.canId = rawId & 0x1FFFFFFFu;
                if (f.dlcBytes > 0) std::memcpy(f.data, pay + 20, f.dlcBytes);
                outFrames.push_back(f);
            } else if (objType == 101 && payLen >= 40) { // CAN_FD_MESSAGE_64
                CanFrame f;
                f.timestamp = tsSec;
                f.channel = pay[0];
                f.isFd = true;
                uint8_t validBytes = (pay[2] > 0) ? pay[2] : CanFdDlcToBytes(pay[1]);
                f.dlcBytes = static_cast<uint8_t>(std::min<size_t>({ validBytes, size_t(64), payLen - 40 }));
                uint32_t rawId = DetailEda100::RdU32LE(pay + 4);
                f.extId = (rawId & 0x80000000u) != 0;
                f.canId = rawId & 0x1FFFFFFFu;
                if (f.dlcBytes > 0) std::memcpy(f.data, pay + 40, f.dlcBytes);
                outFrames.push_back(f);
            }
        }

        size_t step = (objSize + 3u) & ~size_t(3);
        if (pos + objSize + 4 <= sz && std::memcmp(buf + pos + objSize, "LOBJ", 4) == 0) {
            pos += objSize;
        } else if (step <= sz - pos) {
            pos += step;
        } else {
            break;
        }
    }
}

} // namespace DetailCan

inline bool ParseVectorCanBlfBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 8) return false;
    // 1. Vector Binary Logging Format (.blf, "LOGG" magic)
    if (extLower == L".blf" || (size >= 72 && std::memcmp(data, "LOGG", 4) == 0)) {
        if (size < 72 || std::memcmp(data, "LOGG", 4) != 0) return false;
        uint32_t hdrSize = DetailEda100::RdU32LE(data + 4);
        if (hdrSize < 32 || hdrSize > size) return false;

        std::vector<DetailCan::CanFrame> frames;
        DetailCan::WalkBlfObjects(data + hdrSize, size - hdrSize, frames, 0);
        return DetailCan::RenderCanFramesToBoard(frames, L"Vector BLF CAN/CAN-FD Log", out);
    }
    return false;
}

} // namespace Formats
