#pragma once
// AiModelCommon.h — Shared streaming I/O & parsed metadata types for AI/ML model formats.
#include "../../ByteIO.h"
// AiModelFormats.h — Unified streaming SafeTensors (.safetensors), GGUF (.gguf), and ONNX (.onnx)
// inspector shared by both FileContentPane (AI Model card view) and SpreadsheetDocViewer (Sheet/Doc tables).
// Streams directly from disk (with O(1) SetFilePointerEx skipping over multi-GB tensor payloads) or memory.

#include "../../PathUtils.h"
#include "../../OfficeDocTypes.h"
#include "../../PlatformCompat.h"   // <windows.h> on Windows
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::AiModels {

class AiModelStream {
public:
    AiModelStream(HANDLE hFile, uint64_t fileSize)
        : m_hFile(hFile), m_fileSize(fileSize), m_buf(65536) {}

    AiModelStream(const uint8_t* memData, size_t memSize)
        : m_memData(memData), m_fileSize(memSize) {}

    uint64_t FileSize() const { return m_fileSize; }

    uint64_t Pos() const {
        return m_memData ? m_memPos : (m_bufStart + m_bufPos);
    }

    uint64_t Remaining() const {
        const uint64_t p = Pos();
        return (p < m_fileSize) ? (m_fileSize - p) : 0;
    }

    bool Seek(uint64_t target) {
        if (target > m_fileSize) return false;
        if (m_memData) {
            m_memPos = target;
            return true;
        }
        if (target >= m_bufStart && target <= m_bufStart + m_bufLen) {
            m_bufPos = static_cast<size_t>(target - m_bufStart);
            return true;
        }
        LARGE_INTEGER li{};
        li.QuadPart = static_cast<LONGLONG>(target);
        if (!SetFilePointerEx(m_hFile, li, nullptr, FILE_BEGIN)) return false;
        m_bufStart = target;
        m_bufLen = 0;
        m_bufPos = 0;
        return true;
    }

    bool Skip(uint64_t n) {
        if (n > Remaining()) return false;
        return Seek(Pos() + n);
    }

    bool ReadBytes(void* dst, size_t n) {
        if (n == 0) return true;
        if (n > Remaining()) return false;
        if (m_memData) {
            std::memcpy(dst, m_memData + static_cast<size_t>(m_memPos), n);
            m_memPos += n;
            return true;
        }
        uint8_t* out = static_cast<uint8_t*>(dst);
        size_t done = 0;
        while (done < n) {
            if (m_bufPos < m_bufLen) {
                const size_t avail = m_bufLen - m_bufPos;
                const size_t take = std::min(avail, n - done);
                std::memcpy(out + done, m_buf.data() + m_bufPos, take);
                m_bufPos += take;
                done += take;
            } else {
                const uint64_t curPos = m_bufStart + m_bufLen;
                if (n - done >= m_buf.size()) {
                    const DWORD chunk = static_cast<DWORD>(std::min<size_t>(n - done, 16u * 1024u * 1024u));
                    DWORD got = 0;
                    if (!ReadFile(m_hFile, out + done, chunk, &got, nullptr) || got != chunk) return false;
                    done += got;
                    m_bufStart = curPos + got;
                    m_bufLen = 0;
                    m_bufPos = 0;
                } else {
                    const uint64_t remFile = m_fileSize - curPos;
                    const DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(m_buf.size(), remFile));
                    DWORD got = 0;
                    if (!ReadFile(m_hFile, m_buf.data(), toRead, &got, nullptr) || got == 0) return false;
                    m_bufStart = curPos;
                    m_bufLen = got;
                    m_bufPos = 0;
                }
            }
        }
        return true;
    }

    bool ReadU8(uint8_t& v) {
        if (m_memData) {
            if (m_memPos >= m_fileSize) return false;
            v = m_memData[static_cast<size_t>(m_memPos++)];
            return true;
        }
        if (m_bufPos < m_bufLen) {
            v = m_buf[m_bufPos++];
            return true;
        }
        return ReadBytes(&v, 1);
    }

    bool ReadU16(uint16_t& v) {
        uint8_t b[2];
        if (!ReadBytes(b, 2)) return false;
        v = static_cast<uint16_t>(b[0] | (static_cast<uint16_t>(b[1]) << 8));
        return true;
    }

    bool ReadU32(uint32_t& v) {
        uint8_t b[4];
        if (!ReadBytes(b, 4)) return false;
        v = static_cast<uint32_t>(b[0]) |
            (static_cast<uint32_t>(b[1]) << 8) |
            (static_cast<uint32_t>(b[2]) << 16) |
            (static_cast<uint32_t>(b[3]) << 24);
        return true;
    }

    bool ReadU64(uint64_t& v) {
        uint32_t lo = 0, hi = 0;
        if (!ReadU32(lo) || !ReadU32(hi)) return false;
        v = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        return true;
    }

    bool ReadF32(float& v) {
        uint32_t u = 0;
        if (!ReadU32(u)) return false;
        std::memcpy(&v, &u, 4);
        return true;
    }

    bool ReadF64(double& v) {
        uint64_t u = 0;
        if (!ReadU64(u)) return false;
        std::memcpy(&v, &u, 8);
        return true;
    }

    bool ReadPbVarint(uint64_t endPos, uint64_t& out) {
        out = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (Pos() >= endPos) return false;
            uint8_t b = 0;
            if (!ReadU8(b)) return false;
            if (shift == 63 && b > 1) return false;
            out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) return true;
        }
        return false;
    }

    bool ReadPbTag(uint64_t endPos, uint32_t& fieldNum, uint8_t& wireType) {
        uint64_t tag = 0;
        if (!ReadPbVarint(endPos, tag) || tag == 0) return false;
        wireType = static_cast<uint8_t>(tag & 0x07u);
        const uint64_t fn = tag >> 3;
        if (fn == 0 || fn > 0x1FFFFFFFULL) return false;
        if (wireType != 0 && wireType != 1 && wireType != 2 && wireType != 5) return false;
        fieldNum = static_cast<uint32_t>(fn);
        return true;
    }

    bool SkipPbField(uint64_t endPos, uint8_t wireType) {
        switch (wireType) {
        case 0: {
            uint64_t dummy = 0;
            return ReadPbVarint(endPos, dummy);
        }
        case 1:
            return (Pos() + 8 <= endPos) && Skip(8);
        case 2: {
            uint64_t len = 0;
            if (!ReadPbVarint(endPos, len)) return false;
            return (Pos() + len <= endPos) && Skip(len);
        }
        case 5:
            return (Pos() + 4 <= endPos) && Skip(4);
        default:
            return false;
        }
    }

    bool ReadPbStringBounded(uint64_t endPos, std::string& out, size_t maxKeep = 1024) {
        uint64_t len = 0;
        if (!ReadPbVarint(endPos, len) || Pos() + len > endPos) return false;
        const size_t keep = static_cast<size_t>(std::min<uint64_t>(len, maxKeep));
        out.resize(keep);
        if (keep > 0 && !ReadBytes(out.data(), keep)) return false;
        if (len > keep && !Skip(len - keep)) return false;
        return true;
    }

private:
    HANDLE m_hFile = INVALID_HANDLE_VALUE;
    const uint8_t* m_memData = nullptr;
    uint64_t m_memPos = 0;
    uint64_t m_fileSize = 0;
    uint64_t m_bufStart = 0;
    size_t m_bufLen = 0;
    size_t m_bufPos = 0;
    std::vector<uint8_t> m_buf;
};

namespace Detail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdI64LE;
using ::Explorer2::ByteIO::RdF32LE;

using ::Explorer2::ByteIO::HalfToFloat;

using ::Explorer2::ByteIO::BFloat16ToFloat;

inline std::wstring FormatScalarDouble(double v) {
    if (std::isnan(v)) return L"NaN";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.6g", v);
    return buf;
}


inline std::wstring TruncateWithEllipsis(const std::wstring& s, size_t maxLen) {
    return (s.size() > maxLen) ? (s.substr(0, maxLen) + L"...") : s;
}

struct ParsedTensorRow {
    std::wstring name;
    std::wstring dtype;
    std::wstring shape;
    uint64_t paramCount = 0;
    uint64_t byteSize = 0;
    uint64_t dataOffset = 0;
    std::wstring sampleValues;
};

struct ParsedKvEntry {
    std::wstring key;
    std::wstring typeName;
    std::wstring value;
};

struct ParsedModelData {
    bool valid = false;
    uint32_t formatVersion = 0;
    uint64_t headerBytes = 0;
    std::wstring formatName;
    std::wstring arch;
    std::wstring modelTitle;
    std::wstring baseModel;
    std::wstring precisionSummary;
    std::wstring networkDimAlpha;
    std::wstring contextLength;
    std::wstring triggerWords;
    uint64_t tensorCount = 0;
    uint64_t totalParameters = 0;
    uint64_t metadataKeyCount = 0;
    std::vector<ParsedKvEntry> kvEntries;
    std::vector<ParsedTensorRow> tensors;
    // ONNX extra table rows (when requested by Sheet/Doc mode)
    std::vector<std::wstring> onnxOpsets;
    std::vector<std::vector<std::wstring>> onnxSheetRows;
    std::vector<std::vector<std::wstring>> onnxNodeRows;
    std::wstring onnxProducerVersion;
};


struct ScopedFileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~ScopedFileHandle() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
    ScopedFileHandle() = default;
    ScopedFileHandle(const ScopedFileHandle&) = delete;
    ScopedFileHandle& operator=(const ScopedFileHandle&) = delete;
};

inline bool OpenAiModelReadHandle(const std::wstring& path, ScopedFileHandle& outHandle, uint64_t& outSize) {
    outHandle.h = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (outHandle.h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(outHandle.h, &li) || li.QuadPart <= 0) {
        return false;
    }
    outSize = static_cast<uint64_t>(li.QuadPart);
    return true;
}

} // namespace Detail

inline bool IsSupportedAiModelExt(const std::wstring& ext) {
    const std::wstring extLower = PathUtils::ToLower(ext);
    return extLower == L".safetensors" || extLower == L".gguf" || extLower == L".onnx";
}


} // namespace Explorer2::AiModels
