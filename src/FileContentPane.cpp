#include "FileContentPane.h"
#include "CommandIds.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include "FtpClient.h"
#include "ThreadUtils.h"
#include <shellapi.h>
#include <bcrypt.h>
#include <wincodec.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <map>
#include <mutex>
#include <system_error>

#pragma comment(lib, "bcrypt.lib")

namespace Explorer2 {
#include "formats/UnrealAsset3DTex.h"

static D2D1_COLOR_F FcpColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

static bool IsAiModelExtension(const std::wstring& ext) {
    return ext == L".safetensors" || ext == L".gguf" || ext == L".onnx";
}

static void CopyUnicodeToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* p = GlobalLock(hMem);
            if (p) {
                std::memcpy(p, text.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

static std::wstring Utf8ToWideStr(const char* data, size_t len) {
    if (!data || len == 0) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), nullptr, 0);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(1252, 0, data, static_cast<int>(len), nullptr, 0);
        if (wlen <= 0) return L"";
        std::wstring out(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(1252, 0, data, static_cast<int>(len), out.data(), wlen);
        return out;
    }
    std::wstring out(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), out.data(), wlen);
    return out;
}

static void ParseSdParametersString(const std::wstring& rawParams, ImageMetadataSummary& out) {
    if (rawParams.empty()) return;
    out.hasAiMeta = true;
    if (out.generator.empty()) out.generator = L"Stable Diffusion / Fooocus";

    size_t negPos = rawParams.find(L"Negative prompt:");
    size_t stepsPos = rawParams.find(L"Steps:");
    if (stepsPos == std::wstring::npos) {
        stepsPos = rawParams.find(L"\nSteps:");
        if (stepsPos != std::wstring::npos) ++stepsPos;
    }

    if (negPos != std::wstring::npos) {
        out.positivePrompt = PathUtils::Trim(rawParams.substr(0, negPos));
        size_t negStart = negPos + 16;
        size_t negEnd = (stepsPos != std::wstring::npos && stepsPos > negStart) ? stepsPos : rawParams.size();
        out.negativePrompt = PathUtils::Trim(rawParams.substr(negStart, negEnd - negStart));
    } else if (stepsPos != std::wstring::npos) {
        out.positivePrompt = PathUtils::Trim(rawParams.substr(0, stepsPos));
    } else {
        out.positivePrompt = PathUtils::Trim(rawParams);
    }

    if (stepsPos != std::wstring::npos) {
        std::wstring tail = rawParams.substr(stepsPos);
        auto extractField = [&](const wchar_t* key) -> std::wstring {
            size_t p = tail.find(key);
            if (p == std::wstring::npos) return L"";
            p += wcslen(key);
            while (p < tail.size() && (tail[p] == L' ' || tail[p] == L'\t')) ++p;
            size_t end = tail.find_first_of(L",\r\n", p);
            if (end == std::wstring::npos) end = tail.size();
            return PathUtils::Trim(tail.substr(p, end - p));
        };
        out.steps    = extractField(L"Steps:");
        out.sampler  = extractField(L"Sampler:");
        out.cfgScale = extractField(L"CFG scale:");
        if (out.cfgScale.empty()) out.cfgScale = extractField(L"Guidance Scale:");
        out.seed     = extractField(L"Seed:");
        out.modelName = extractField(L"Model:");
        if (out.modelName.empty()) out.modelName = extractField(L"Base Model:");
    }
}

static void ParseComfyUiJsonPrompt(const std::wstring& jsonText, ImageMetadataSummary& out) {
    if (jsonText.empty()) return;
    out.hasAiMeta = true;
    out.generator = L"ComfyUI";

    // Look for "text": "..." fields inside CLIPTextEncode or prompt nodes
    size_t pos = 0;
    while ((pos = jsonText.find(L"\"text\"", pos)) != std::wstring::npos) {
        size_t colon = jsonText.find(L':', pos + 6);
        if (colon == std::wstring::npos) break;
        size_t q1 = jsonText.find(L'"', colon + 1);
        if (q1 == std::wstring::npos || q1 > colon + 12) { pos = colon + 1; continue; }
        std::wstring val;
        size_t i = q1 + 1;
        while (i < jsonText.size()) {
            if (jsonText[i] == L'\\' && i + 1 < jsonText.size()) {
                val.push_back(jsonText[i + 1]);
                i += 2;
            } else if (jsonText[i] == L'"') {
                break;
            } else {
                val.push_back(jsonText[i]);
                ++i;
            }
        }
        val = PathUtils::Trim(val);
        if (val.size() >= 4 && val.find(L"node") == std::wstring::npos) {
            if (out.positivePrompt.empty()) out.positivePrompt = val;
            else if (out.negativePrompt.empty() && val != out.positivePrompt) out.negativePrompt = val;
        }
        pos = i + 1;
    }

    auto findNumericJson = [&](const wchar_t* key) -> std::wstring {
        size_t p = jsonText.find(key);
        if (p == std::wstring::npos) return L"";
        size_t c = jsonText.find(L':', p + wcslen(key));
        if (c == std::wstring::npos) return L"";
        size_t s = jsonText.find_first_of(L"0123456789", c + 1);
        if (s == std::wstring::npos || s > c + 12) return L"";
        size_t e = jsonText.find_first_not_of(L"0123456789.", s);
        return jsonText.substr(s, (e == std::wstring::npos ? jsonText.size() : e) - s);
    };
    if (out.seed.empty()) out.seed = findNumericJson(L"\"seed\"");
    if (out.seed.empty()) out.seed = findNumericJson(L"\"noise_seed\"");
    if (out.steps.empty()) out.steps = findNumericJson(L"\"steps\"");
    if (out.cfgScale.empty()) out.cfgScale = findNumericJson(L"\"cfg\"");

    size_t ckpt = jsonText.find(L"\"ckpt_name\"");
    if (ckpt != std::wstring::npos) {
        size_t q1 = jsonText.find(L'"', ckpt + 11);
        size_t q2 = (q1 != std::wstring::npos) ? jsonText.find(L'"', q1 + 1) : std::wstring::npos;
        if (q1 != std::wstring::npos && q2 != std::wstring::npos) {
            out.modelName = jsonText.substr(q1 + 1, q2 - q1 - 1);
        }
    }
}

static ImageMetadataSummary InspectImageMetadata(const std::wstring& path) {
    ImageMetadataSummary res;
    res.inspected = true;
    if (path.empty()) return res;

    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        uint8_t sig[8] = {};
        DWORD got = 0;
        if (ReadFile(hFile, sig, 8, &got, nullptr) && got == 8 &&
            sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G')
        {
            // Walk PNG chunks for tEXt / iTXt
            for (int chunkIdx = 0; chunkIdx < 64; ++chunkIdx) {
                uint8_t hdr[8] = {};
                if (!ReadFile(hFile, hdr, 8, &got, nullptr) || got != 8) break;
                uint32_t len = (static_cast<uint32_t>(hdr[0]) << 24) |
                               (static_cast<uint32_t>(hdr[1]) << 16) |
                               (static_cast<uint32_t>(hdr[2]) << 8) |
                               static_cast<uint32_t>(hdr[3]);
                char type[5] = { static_cast<char>(hdr[4]), static_cast<char>(hdr[5]), static_cast<char>(hdr[6]), static_cast<char>(hdr[7]), '\0' };

                if (std::strcmp(type, "IEND") == 0 || std::strcmp(type, "IDAT") == 0) {
                    if (std::strcmp(type, "IEND") == 0) break;
                    // Skip IDAT payload + 4-byte CRC
                    LARGE_INTEGER skip = {};
                    skip.QuadPart = static_cast<LONGLONG>(len) + 4;
                    if (!SetFilePointerEx(hFile, skip, nullptr, FILE_CURRENT)) break;
                    continue;
                }

                if ((std::strcmp(type, "tEXt") == 0 || std::strcmp(type, "iTXt") == 0) && len > 0 && len <= (1024 * 1024)) {
                    std::vector<char> buf(len);
                    if (!ReadFile(hFile, buf.data(), len, &got, nullptr) || got != len) break;
                    LARGE_INTEGER crcSkip = {};
                    crcSkip.QuadPart = 4;
                    SetFilePointerEx(hFile, crcSkip, nullptr, FILE_CURRENT);

                    size_t nulPos = 0;
                    while (nulPos < buf.size() && buf[nulPos] != '\0') ++nulPos;
                    std::string key(buf.data(), nulPos);
                    size_t valStart = nulPos + 1;
                    if (std::strcmp(type, "iTXt") == 0 && valStart + 2 <= buf.size()) {
                        // iTXt: compression_flag (1), compression_method (1), language_tag (null-term), translated_keyword (null-term), text
                        valStart += 2;
                        while (valStart < buf.size() && buf[valStart] != '\0') ++valStart;
                        if (valStart < buf.size()) ++valStart;
                        while (valStart < buf.size() && buf[valStart] != '\0') ++valStart;
                        if (valStart < buf.size()) ++valStart;
                    }
                    if (valStart < buf.size()) {
                        std::wstring textVal = Utf8ToWideStr(buf.data() + valStart, buf.size() - valStart);
                        if (_stricmp(key.c_str(), "parameters") == 0 || _stricmp(key.c_str(), "Comment") == 0 || _stricmp(key.c_str(), "Description") == 0) {
                            if (textVal.find(L"{") == 0 && textVal.find(L"\"prompt\"") != std::wstring::npos) {
                                ParseComfyUiJsonPrompt(textVal, res);
                            } else {
                                ParseSdParametersString(textVal, res);
                            }
                        } else if (_stricmp(key.c_str(), "prompt") == 0) {
                            ParseComfyUiJsonPrompt(textVal, res);
                        }
                    }
                } else {
                    LARGE_INTEGER skip = {};
                    skip.QuadPart = static_cast<LONGLONG>(len) + 4;
                    if (!SetFilePointerEx(hFile, skip, nullptr, FILE_CURRENT)) break;
                }
            }
        }
        CloseHandle(hFile);
    }

    // Also query EXIF via WIC if JPEG/WebP/PNG
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) && wic) {
        HANDLE hRead = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hRead != INVALID_HANDLE_VALUE) {
            IWICStream* stream = nullptr;
            if (SUCCEEDED(wic->CreateStream(&stream)) && stream) {
                if (SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_READ))) {
                    IWICBitmapDecoder* decoder = nullptr;
                    if (SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) && decoder) {
                        IWICBitmapFrameDecode* frame = nullptr;
                        if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
                            IWICMetadataQueryReader* q = nullptr;
                            if (SUCCEEDED(frame->GetMetadataQueryReader(&q)) && q) {
                                auto getStrProp = [&](const wchar_t* query) -> std::wstring {
                                    PROPVARIANT pv;
                                    PropVariantInit(&pv);
                                    std::wstring out;
                                    if (SUCCEEDED(q->GetMetadataByName(query, &pv))) {
                                        if (pv.vt == VT_LPWSTR && pv.pwszVal) out = pv.pwszVal;
                                        else if (pv.vt == VT_LPSTR && pv.pszVal) out = Utf8ToWideStr(pv.pszVal, std::strlen(pv.pszVal));
                                    }
                                    PropVariantClear(&pv);
                                    return PathUtils::Trim(out);
                                };
                                auto getRationalProp = [&](const wchar_t* query, bool asFraction) -> std::wstring {
                                    PROPVARIANT pv;
                                    PropVariantInit(&pv);
                                    std::wstring out;
                                    if (SUCCEEDED(q->GetMetadataByName(query, &pv))) {
                                        uint32_t num = 0, den = 0;
                                        if (pv.vt == VT_UI8) {
                                            num = static_cast<uint32_t>(pv.uhVal.QuadPart & 0xFFFFFFFFu);
                                            den = static_cast<uint32_t>((pv.uhVal.QuadPart >> 32) & 0xFFFFFFFFu);
                                        }
                                        if (den > 0 && num > 0) {
                                            wchar_t b[64] = {};
                                            if (asFraction && num < den) {
                                                swprintf_s(b, L"1/%u s", static_cast<unsigned>((den + num / 2) / num));
                                            } else {
                                                swprintf_s(b, L"%.1f", static_cast<double>(num) / static_cast<double>(den));
                                            }
                                            out = b;
                                        }
                                    }
                                    PropVariantClear(&pv);
                                    return out;
                                };
                                auto getUintProp = [&](const wchar_t* query) -> std::wstring {
                                    PROPVARIANT pv;
                                    PropVariantInit(&pv);
                                    std::wstring out;
                                    if (SUCCEEDED(q->GetMetadataByName(query, &pv))) {
                                        if (pv.vt == VT_UI2) out = std::to_wstring(pv.uiVal);
                                        else if (pv.vt == VT_UI4) out = std::to_wstring(pv.ulVal);
                                    }
                                    PropVariantClear(&pv);
                                    return out;
                                };

                                std::wstring make = getStrProp(L"/app1/ifd/{ushort=271}");
                                std::wstring model = getStrProp(L"/app1/ifd/{ushort=272}");
                                std::wstring dt = getStrProp(L"/app1/ifd/exif/{ushort=36867}");
                                std::wstring expTime = getRationalProp(L"/app1/ifd/exif/{ushort=33434}", true);
                                std::wstring fnum = getRationalProp(L"/app1/ifd/exif/{ushort=33437}", false);
                                std::wstring iso = getUintProp(L"/app1/ifd/exif/{ushort=34855}");
                                std::wstring focal = getRationalProp(L"/app1/ifd/exif/{ushort=37386}", false);

                                std::vector<std::wstring> parts;
                                if (!model.empty()) {
                                    if (!make.empty() && model.find(make) == std::wstring::npos) parts.push_back(make + L" " + model);
                                    else parts.push_back(model);
                                } else if (!make.empty()) {
                                    parts.push_back(make);
                                }
                                if (!focal.empty()) parts.push_back(focal + L"mm");
                                if (!fnum.empty()) parts.push_back(L"f/" + fnum);
                                if (!expTime.empty()) parts.push_back(expTime);
                                if (!iso.empty()) parts.push_back(L"ISO " + iso);
                                if (!dt.empty()) parts.push_back(dt);

                                if (!parts.empty()) {
                                    res.hasExif = true;
                                    for (size_t i = 0; i < parts.size(); ++i) {
                                        if (i > 0) res.exifSummary += L"  \x2022  ";
                                        res.exifSummary += parts[i];
                                    }
                                }
                                q->Release();
                            }
                            frame->Release();
                        }
                        decoder->Release();
                    }
                }
                stream->Release();
            }
            CloseHandle(hRead);
        }
        wic->Release();
    }

    return res;
}

namespace {

static std::wstring FormatModelParamCount(uint64_t n) {
    wchar_t buf[64] = {};
    if (n >= 1000000000ULL) {
        swprintf_s(buf, L"%.2fB (%llu)", static_cast<double>(n) / 1e9, static_cast<unsigned long long>(n));
    } else if (n >= 1000000ULL) {
        swprintf_s(buf, L"%.2fM (%llu)", static_cast<double>(n) / 1e6, static_cast<unsigned long long>(n));
    } else if (n >= 10000ULL) {
        swprintf_s(buf, L"%.1fK (%llu)", static_cast<double>(n) / 1e3, static_cast<unsigned long long>(n));
    } else {
        swprintf_s(buf, L"%llu", static_cast<unsigned long long>(n));
    }
    return buf;
}

class WinAiFileStream {
public:
    explicit WinAiFileStream(HANDLE hFile, uint64_t fileSize)
        : m_hFile(hFile), m_fileSize(fileSize), m_buf(65536) {}

    uint64_t FileSize() const { return m_fileSize; }
    uint64_t Pos() const { return m_bufStart + m_bufPos; }
    uint64_t Remaining() const {
        const uint64_t p = Pos();
        return (p < m_fileSize) ? (m_fileSize - p) : 0;
    }

    bool Seek(uint64_t target) {
        if (target > m_fileSize) return false;
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
    uint64_t m_fileSize = 0;
    uint64_t m_bufStart = 0;
    size_t m_bufLen = 0;
    size_t m_bufPos = 0;
    std::vector<uint8_t> m_buf;
};

static std::wstring FormatAiScalarDouble(double v) {
    if (std::isnan(v)) return L"NaN";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.6g", v);
    return buf;
}

// Structural JSON parser for SafeTensors headers
struct StJsonCursor {
    const char* s = nullptr;
    size_t n = 0;
    size_t pos = 0;

    void SkipWs() {
        while (pos < n) {
            const char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos;
            else break;
        }
    }

    static void AppendUtf8Codepoint(std::string& out, uint32_t cp) {
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0x10FFFFu) {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    static bool ParseHex4(const char* p, uint32_t& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = p[i];
            uint32_t v = 0;
            if (c >= '0' && c <= '9') v = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = static_cast<uint32_t>(c - 'A' + 10);
            else return false;
            out = (out << 4) | v;
        }
        return true;
    }

    bool ParseString(std::string& out) {
        SkipWs();
        if (pos >= n || s[pos] != '"') return false;
        ++pos;
        out.clear();
        while (pos < n) {
            const unsigned char c = static_cast<unsigned char>(s[pos++]);
            if (c == '"') return true;
            if (c < 0x20u) return false;
            if (c == '\\') {
                if (pos >= n) return false;
                const char esc = s[pos++];
                switch (esc) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > n) return false;
                    uint32_t u1 = 0;
                    if (!ParseHex4(s + pos, u1)) return false;
                    pos += 4;
                    if (u1 >= 0xD800u && u1 <= 0xDBFFu && pos + 6 <= n && s[pos] == '\\' && s[pos + 1] == 'u') {
                        uint32_t u2 = 0;
                        if (ParseHex4(s + pos + 2, u2) && u2 >= 0xDC00u && u2 <= 0xDFFFu) {
                            pos += 6;
                            const uint32_t cp = 0x10000u + (((u1 - 0xD800u) << 10) | (u2 - 0xDC00u));
                            AppendUtf8Codepoint(out, cp);
                            break;
                        }
                    }
                    AppendUtf8Codepoint(out, u1);
                    break;
                }
                default:
                    return false;
                }
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        return false;
    }

    bool ParseUint64(uint64_t& out) {
        SkipWs();
        if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
        if (s[pos] == '0' && pos + 1 < n && s[pos + 1] >= '0' && s[pos + 1] <= '9') return false;
        out = 0;
        while (pos < n && s[pos] >= '0' && s[pos] <= '9') {
            const uint64_t d = static_cast<uint64_t>(s[pos] - '0');
            if (out > (UINT64_MAX - d) / 10ULL) return false;
            out = out * 10ULL + d;
            ++pos;
        }
        if (pos < n && (s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E')) return false;
        return true;
    }

    bool SkipNumber() {
        if (pos < n && s[pos] == '-') ++pos;
        if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
        if (s[pos] == '0') {
            ++pos;
            if (pos < n && s[pos] >= '0' && s[pos] <= '9') return false;
        } else {
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos < n && s[pos] == '.') {
            ++pos;
            if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos < n && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < n && (s[pos] == '+' || s[pos] == '-')) ++pos;
            if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        return true;
    }

    bool SkipValue(int depth = 0) {
        if (depth > 32) return false;
        SkipWs();
        if (pos >= n) return false;
        const char c = s[pos];
        if (c == '"') {
            std::string dummy;
            return ParseString(dummy);
        }
        if (c == '{') {
            ++pos;
            SkipWs();
            if (pos < n && s[pos] == '}') { ++pos; return true; }
            for (;;) {
                std::string k;
                if (!ParseString(k)) return false;
                SkipWs();
                if (pos >= n || s[pos] != ':') return false;
                ++pos;
                if (!SkipValue(depth + 1)) return false;
                SkipWs();
                if (pos < n && s[pos] == ',') { ++pos; continue; }
                if (pos < n && s[pos] == '}') { ++pos; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++pos;
            SkipWs();
            if (pos < n && s[pos] == ']') { ++pos; return true; }
            for (;;) {
                if (!SkipValue(depth + 1)) return false;
                SkipWs();
                if (pos < n && s[pos] == ',') { ++pos; continue; }
                if (pos < n && s[pos] == ']') { ++pos; return true; }
                return false;
            }
        }
        if (c == 't') {
            if (pos + 4 <= n && std::memcmp(s + pos, "true", 4) == 0) { pos += 4; return true; }
            return false;
        }
        if (c == 'f') {
            if (pos + 5 <= n && std::memcmp(s + pos, "false", 5) == 0) { pos += 5; return true; }
            return false;
        }
        if (c == 'n') {
            if (pos + 4 <= n && std::memcmp(s + pos, "null", 4) == 0) { pos += 4; return true; }
            return false;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return SkipNumber();
        }
        return false;
    }

    bool ParseMetadataValueAsString(std::string& out) {
        SkipWs();
        if (pos >= n) return false;
        if (s[pos] == '"') return ParseString(out);
        const size_t start = pos;
        if (!SkipValue(1)) return false;
        out.assign(s + start, pos - start);
        return true;
    }
};

static bool ParseSafeTensorsSummary(WinAiFileStream& st, AiModelInfoSummary& info) {
    const uint64_t fileSize = st.FileSize();
    if (fileSize < 10) return false;
    uint64_t hdrLen = 0;
    if (!st.ReadU64(hdrLen)) return false;
    if (hdrLen < 2 || hdrLen > (64ULL * 1024ULL * 1024ULL)) return false;
    if (8ULL + hdrLen > fileSize) return false;

    std::string json(static_cast<size_t>(hdrLen), '\0');
    if (!st.ReadBytes(json.data(), static_cast<size_t>(hdrLen))) return false;

    size_t firstNonWs = 0;
    while (firstNonWs < json.size() && (json[firstNonWs] == ' ' || json[firstNonWs] == '\t' || json[firstNonWs] == '\r' || json[firstNonWs] == '\n')) {
        ++firstNonWs;
    }
    size_t lastNonWs = json.size();
    while (lastNonWs > firstNonWs && (json[lastNonWs - 1] == ' ' || json[lastNonWs - 1] == '\t' || json[lastNonWs - 1] == '\r' || json[lastNonWs - 1] == '\n')) {
        --lastNonWs;
    }
    if (lastNonWs - firstNonWs < 2 || json[firstNonWs] != '{' || json[lastNonWs - 1] != '}') return false;

    StJsonCursor cur{ json.data(), json.size(), 0 };
    cur.SkipWs();
    if (cur.pos >= cur.n || cur.s[cur.pos] != '{') return false;
    ++cur.pos;
    cur.SkipWs();

    std::map<std::string, uint64_t> dtypeCounts;
    std::map<std::string, std::wstring> metaMap;

    if (cur.pos < cur.n && cur.s[cur.pos] == '}') {
        ++cur.pos;
    } else {
        for (;;) {
            std::string topKey;
            if (!cur.ParseString(topKey)) return false;
            cur.SkipWs();
            if (cur.pos >= cur.n || cur.s[cur.pos] != ':') return false;
            ++cur.pos;
            cur.SkipWs();
            if (cur.pos >= cur.n || cur.s[cur.pos] != '{') return false;
            ++cur.pos;
            cur.SkipWs();

            if (topKey == "__metadata__") {
                if (cur.pos < cur.n && cur.s[cur.pos] == '}') {
                    ++cur.pos;
                } else {
                    for (;;) {
                        std::string mk, mv;
                        if (!cur.ParseString(mk)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ':') return false;
                        ++cur.pos;
                        if (!cur.ParseMetadataValueAsString(mv)) return false;
                        ++info.metadataKeyCount;
                        std::wstring wv = Utf8ToWideStr(mv.data(), mv.size());
                        metaMap[mk] = wv;
                        if (info.kvRows.size() < 256) {
                            std::wstring disp = wv.size() > 220 ? (wv.substr(0, 220) + L"...") : wv;
                            info.kvRows.push_back({ Utf8ToWideStr(mk.data(), mk.size()), std::move(disp) });
                        }
                        cur.SkipWs();
                        if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                        if (cur.pos < cur.n && cur.s[cur.pos] == '}') { ++cur.pos; break; }
                        return false;
                    }
                }
            } else {
                if (topKey.empty()) return false;
                bool hasDtype = false, hasShape = false, hasOffsets = false;
                std::string dtype;
                std::vector<uint64_t> shape;
                uint64_t offBegin = 0, offEnd = 0;

                if (cur.pos < cur.n && cur.s[cur.pos] == '}') {
                    return false; // Empty tensor descriptor is invalid
                }
                for (;;) {
                    std::string fk;
                    if (!cur.ParseString(fk)) return false;
                    cur.SkipWs();
                    if (cur.pos >= cur.n || cur.s[cur.pos] != ':') return false;
                    ++cur.pos;
                    cur.SkipWs();
                    if (fk == "dtype") {
                        if (!cur.ParseString(dtype) || dtype.empty() || dtype.size() > 32) return false;
                        hasDtype = true;
                    } else if (fk == "shape") {
                        if (cur.pos >= cur.n || cur.s[cur.pos] != '[') return false;
                        ++cur.pos;
                        cur.SkipWs();
                        shape.clear();
                        if (cur.pos < cur.n && cur.s[cur.pos] == ']') {
                            ++cur.pos;
                        } else {
                            for (;;) {
                                uint64_t dim = 0;
                                if (!cur.ParseUint64(dim) || shape.size() >= 32) return false;
                                shape.push_back(dim);
                                cur.SkipWs();
                                if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                                if (cur.pos < cur.n && cur.s[cur.pos] == ']') { ++cur.pos; break; }
                                return false;
                            }
                        }
                        hasShape = true;
                    } else if (fk == "data_offsets") {
                        if (cur.pos >= cur.n || cur.s[cur.pos] != '[') return false;
                        ++cur.pos;
                        if (!cur.ParseUint64(offBegin)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ',') return false;
                        ++cur.pos;
                        if (!cur.ParseUint64(offEnd)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ']') return false;
                        ++cur.pos;
                        hasOffsets = true;
                    } else {
                        if (!cur.SkipValue(2)) return false;
                    }
                    cur.SkipWs();
                    if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                    if (cur.pos < cur.n && cur.s[cur.pos] == '}') { ++cur.pos; break; }
                    return false;
                }

                if (!hasDtype || !hasShape || !hasOffsets) return false;
                if (offBegin > offEnd) return false;
                if (offEnd > fileSize - (8ULL + hdrLen)) return false;

                uint64_t params = 1;
                if (shape.empty()) {
                    params = (offEnd > offBegin) ? 1ULL : 0ULL;
                } else {
                    for (uint64_t d : shape) {
                        if (d == 0) { params = 0; break; }
                        if (params > UINT64_MAX / d) { params = UINT64_MAX; break; }
                        params *= d;
                    }
                }

                ++info.tensorCount;
                if (info.totalParameters <= UINT64_MAX - params) info.totalParameters += params;
                else info.totalParameters = UINT64_MAX;
                dtypeCounts[dtype]++;

                if (info.tensors.size() < 1024) {
                    AiModelInfoSummary::TensorEntry te;
                    te.name = Utf8ToWideStr(topKey.data(), topKey.size());
                    te.dtype = Utf8ToWideStr(dtype.data(), dtype.size());
                    te.shape = L"[";
                    for (size_t i = 0; i < shape.size(); ++i) {
                        if (i > 0) te.shape += L", ";
                        te.shape += std::to_wstring(shape[i]);
                    }
                    te.shape += L"]";
                    te.paramCount = params;
                    te.byteSize = offEnd - offBegin;
                    info.tensors.push_back(std::move(te));
                }
            }

            cur.SkipWs();
            if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
            if (cur.pos < cur.n && cur.s[cur.pos] == '}') { ++cur.pos; break; }
            return false;
        }
    }

    cur.SkipWs();
    if (cur.pos != cur.n) return false;

    info.valid = true;
    info.formatName = L"SafeTensors";

    for (const auto& kv : dtypeCounts) {
        if (!info.precisionSummary.empty()) info.precisionSummary += L", ";
        info.precisionSummary += Utf8ToWideStr(kv.first.data(), kv.first.size()) + L" (" + std::to_wstring(kv.second) + L")";
    }

    auto getMeta = [&](const char* k) -> std::wstring {
        auto it = metaMap.find(k);
        return (it != metaMap.end()) ? it->second : L"";
    };

    info.modelTitle = getMeta("modelspec.title");
    if (info.modelTitle.empty()) info.modelTitle = getMeta("ss_output_name");
    if (info.modelTitle.empty()) info.modelTitle = getMeta("name");

    info.arch = getMeta("modelspec.architecture");
    if (info.arch.empty()) info.arch = getMeta("ss_network_module");
    if (info.arch.empty()) info.arch = getMeta("architecture");

    info.baseModel = getMeta("modelspec.base_model");
    if (info.baseModel.empty()) info.baseModel = getMeta("ss_base_model_version");
    if (info.baseModel.empty()) info.baseModel = getMeta("ss_sd_model_name");

    const std::wstring dim = getMeta("ss_network_dim");
    const std::wstring alpha = getMeta("ss_network_alpha");
    if (!dim.empty()) {
        info.networkDimAlpha = L"Rank/Dim: " + dim + (alpha.empty() ? L"" : (L"  /  Alpha: " + alpha));
    }
    info.triggerWords = getMeta("modelspec.trigger_phrase");
    return true;
}

struct GgmlTypeTrait {
    const wchar_t* name;
    uint32_t blockElems;
    uint32_t blockBytes;
};

static bool GetGgmlTypeTrait(uint32_t t, GgmlTypeTrait& out) {
    switch (t) {
    case 0:  out = { L"F32",     1,   4   }; return true;
    case 1:  out = { L"F16",     1,   2   }; return true;
    case 2:  out = { L"Q4_0",    32,  18  }; return true;
    case 3:  out = { L"Q4_1",    32,  20  }; return true;
    case 4:  out = { L"Q4_2",    16,  10  }; return true;
    case 5:  out = { L"Q4_3",    16,  12  }; return true;
    case 6:  out = { L"Q5_0",    32,  22  }; return true;
    case 7:  out = { L"Q5_1",    32,  24  }; return true;
    case 8:  out = { L"Q8_0",    32,  34  }; return true;
    case 9:  out = { L"Q8_1",    32,  36  }; return true;
    case 10: out = { L"Q2_K",    256, 84  }; return true;
    case 11: out = { L"Q3_K",    256, 110 }; return true;
    case 12: out = { L"Q4_K",    256, 144 }; return true;
    case 13: out = { L"Q5_K",    256, 176 }; return true;
    case 14: out = { L"Q6_K",    256, 210 }; return true;
    case 15: out = { L"Q8_K",    256, 292 }; return true;
    case 16: out = { L"IQ2_XXS", 256, 66  }; return true;
    case 17: out = { L"IQ2_XS",  256, 74  }; return true;
    case 18: out = { L"IQ3_XXS", 256, 98  }; return true;
    case 19: out = { L"IQ1_S",   256, 50  }; return true;
    case 20: out = { L"IQ4_NL",  32,  18  }; return true;
    case 21: out = { L"IQ3_S",   256, 110 }; return true;
    case 22: out = { L"IQ2_S",   256, 82  }; return true;
    case 23: out = { L"IQ4_XS",  256, 136 }; return true;
    case 24: out = { L"I8",      1,   1   }; return true;
    case 25: out = { L"I16",     1,   2   }; return true;
    case 26: out = { L"I32",     1,   4   }; return true;
    case 27: out = { L"I64",     1,   8   }; return true;
    case 28: out = { L"F64",     1,   8   }; return true;
    case 29: out = { L"IQ1_M",   256, 56  }; return true;
    case 30: out = { L"BF16",    1,   2   }; return true;
    case 31: out = { L"Q4_0_4_4",32,  18  }; return true;
    case 32: out = { L"Q4_0_4_8",32,  18  }; return true;
    case 33: out = { L"Q4_0_8_8",32,  18  }; return true;
    case 34: out = { L"TQ1_0",   256, 54  }; return true;
    case 35: out = { L"TQ2_0",   256, 66  }; return true;
    case 36: out = { L"IQ4_NL_4_4", 32, 18 }; return true;
    case 37: out = { L"IQ4_NL_4_8", 32, 18 }; return true;
    case 38: out = { L"IQ4_NL_8_8", 32, 18 }; return true;
    case 39: out = { L"MXFP4",   32,  17  }; return true;
    default: return false;
    }
}

static const wchar_t* GgufMetaTypeName(uint32_t vt) {
    switch (vt) {
    case 0:  return L"uint8";
    case 1:  return L"int8";
    case 2:  return L"uint16";
    case 3:  return L"int16";
    case 4:  return L"uint32";
    case 5:  return L"int32";
    case 6:  return L"float32";
    case 7:  return L"bool";
    case 8:  return L"string";
    case 9:  return L"array";
    case 10: return L"uint64";
    case 11: return L"int64";
    case 12: return L"float64";
    default: return L"unknown";
    }
}

static uint32_t GgufFixedScalarByteSize(uint32_t vt) {
    switch (vt) {
    case 0: case 1: case 7: return 1;
    case 2: case 3:         return 2;
    case 4: case 5: case 6: return 4;
    case 10: case 11: case 12: return 8;
    default: return 0;
    }
}

static bool ReadGgufCount(WinAiFileStream& st, uint32_t version, uint64_t& out) {
    if (version == 1) {
        uint32_t v32 = 0;
        if (!st.ReadU32(v32)) return false;
        out = v32;
        return true;
    }
    return st.ReadU64(out);
}

static bool ReadGgufString(WinAiFileStream& st, uint32_t version, std::string& out, size_t maxKeep = 512) {
    uint64_t len = 0;
    if (!ReadGgufCount(st, version, len)) return false;
    if (len > st.Remaining()) return false;
    const size_t keep = static_cast<size_t>(std::min<uint64_t>(len, maxKeep));
    out.resize(keep);
    if (keep > 0 && !st.ReadBytes(out.data(), keep)) return false;
    if (len > keep && !st.Skip(len - keep)) return false;
    return true;
}

static bool ReadGgufMetadataValue(WinAiFileStream& st, uint32_t version, uint32_t vtype, std::wstring& outVal, bool captureText, int depth = 0) {
    if (depth > 4) return false;
    switch (vtype) {
    case 0: {
        uint8_t v = 0;
        if (!st.ReadU8(v)) return false;
        if (captureText) outVal = std::to_wstring(v);
        return true;
    }
    case 1: {
        uint8_t v = 0;
        if (!st.ReadU8(v)) return false;
        if (captureText) outVal = std::to_wstring(static_cast<int8_t>(v));
        return true;
    }
    case 2: {
        uint16_t v = 0;
        if (!st.ReadU16(v)) return false;
        if (captureText) outVal = std::to_wstring(v);
        return true;
    }
    case 3: {
        uint16_t v = 0;
        if (!st.ReadU16(v)) return false;
        if (captureText) outVal = std::to_wstring(static_cast<int16_t>(v));
        return true;
    }
    case 4: {
        uint32_t v = 0;
        if (!st.ReadU32(v)) return false;
        if (captureText) outVal = std::to_wstring(v);
        return true;
    }
    case 5: {
        uint32_t v = 0;
        if (!st.ReadU32(v)) return false;
        if (captureText) outVal = std::to_wstring(static_cast<int32_t>(v));
        return true;
    }
    case 6: {
        float v = 0.0f;
        if (!st.ReadF32(v)) return false;
        if (captureText) outVal = FormatAiScalarDouble(v);
        return true;
    }
    case 7: {
        uint8_t v = 0;
        if (!st.ReadU8(v) || v > 1u) return false;
        if (captureText) outVal = (v != 0) ? L"true" : L"false";
        return true;
    }
    case 8: {
        std::string s;
        if (!ReadGgufString(st, version, s, captureText ? 256 : 0)) return false;
        if (captureText) outVal = Utf8ToWideStr(s.data(), s.size());
        return true;
    }
    case 9: {
        uint32_t elemType = 0;
        uint64_t count = 0;
        if (!st.ReadU32(elemType) || elemType > 12 || !ReadGgufCount(st, version, count)) return false;
        const uint32_t fixedSz = GgufFixedScalarByteSize(elemType);
        if (fixedSz > 0) {
            if (count > st.Remaining() / fixedSz) return false;
            if (!captureText) {
                return st.Skip(count * fixedSz);
            }
            const uint64_t previewN = std::min<uint64_t>(count, 6);
            std::wstring sample;
            for (uint64_t i = 0; i < previewN; ++i) {
                std::wstring ev;
                if (!ReadGgufMetadataValue(st, version, elemType, ev, true, depth + 1)) return false;
                if (i > 0) sample += L", ";
                sample += ev;
            }
            if (count > previewN) {
                if (!st.Skip((count - previewN) * fixedSz)) return false;
                outVal = L"[" + std::to_wstring(count) + L" \x00D7 " + GgufMetaTypeName(elemType) + L"] (" + sample + L", ...)";
            } else {
                outVal = L"[" + sample + L"]";
            }
            return true;
        }
        // Variable-length elements (strings or nested arrays)
        if (count > 10000000ULL) return false;
        const uint64_t previewN = captureText ? std::min<uint64_t>(count, 5) : 0;
        std::wstring sample;
        for (uint64_t i = 0; i < count; ++i) {
            std::wstring ev;
            const bool wantElem = (i < previewN);
            if (!ReadGgufMetadataValue(st, version, elemType, ev, wantElem, depth + 1)) return false;
            if (wantElem) {
                if (i > 0) sample += L", ";
                if (ev.size() > 24) ev = ev.substr(0, 24) + L"...";
                sample += L"\"" + ev + L"\"";
            }
        }
        if (captureText) {
            if (count > previewN) {
                outVal = L"[" + std::to_wstring(count) + L" \x00D7 " + GgufMetaTypeName(elemType) + L"] (" + sample + L", ...)";
            } else {
                outVal = L"[" + sample + L"]";
            }
        }
        return true;
    }
    case 10: {
        uint64_t v = 0;
        if (!st.ReadU64(v)) return false;
        if (captureText) outVal = std::to_wstring(v);
        return true;
    }
    case 11: {
        uint64_t v = 0;
        if (!st.ReadU64(v)) return false;
        if (captureText) outVal = std::to_wstring(static_cast<int64_t>(v));
        return true;
    }
    case 12: {
        double v = 0.0;
        if (!st.ReadF64(v)) return false;
        if (captureText) outVal = FormatAiScalarDouble(v);
        return true;
    }
    default:
        return false;
    }
}

static bool ParseGgufSummary(WinAiFileStream& st, AiModelInfoSummary& info) {
    if (st.FileSize() < 16) return false;
    uint8_t magic[4] = {};
    uint32_t version = 0;
    if (!st.ReadBytes(magic, 4) || std::memcmp(magic, "GGUF", 4) != 0) return false;
    if (!st.ReadU32(version) || version < 1 || version > 3) return false;

    uint64_t tensorCount = 0;
    uint64_t kvCount = 0;
    if (!ReadGgufCount(st, version, tensorCount) || !ReadGgufCount(st, version, kvCount)) return false;
    if (kvCount > 1000000ULL || tensorCount > 5000000ULL) return false;

    std::map<std::string, std::wstring> kvMap;
    for (uint64_t i = 0; i < kvCount; ++i) {
        std::string key;
        if (!ReadGgufString(st, version, key, 256) || key.empty()) return false;
        uint32_t vtype = 0;
        if (!st.ReadU32(vtype) || vtype > 12) return false;
        std::wstring valW;
        if (!ReadGgufMetadataValue(st, version, vtype, valW, true, 0)) return false;

        kvMap[key] = valW;
        if (info.kvRows.size() < 256) {
            std::wstring disp = valW;
            if (disp.size() > 160) disp = disp.substr(0, 160) + L"...";
            info.kvRows.push_back({ Utf8ToWideStr(key.data(), key.size()), std::move(disp) });
        }
    }

    std::map<std::wstring, uint64_t> quantHist;
    for (uint64_t t = 0; t < tensorCount; ++t) {
        std::string tName;
        if (!ReadGgufString(st, version, tName, 512) || tName.empty()) return false;
        uint32_t nDims = 0;
        if (!st.ReadU32(nDims) || nDims < 1 || nDims > 8) return false;

        uint64_t dims[8] = {};
        uint64_t params = 1;
        for (uint32_t d = 0; d < nDims; ++d) {
            if (!st.ReadU64(dims[d]) || dims[d] == 0) return false;
            if (params > UINT64_MAX / dims[d]) params = UINT64_MAX;
            else params *= dims[d];
        }

        uint32_t ggmlType = 0;
        uint64_t relOffset = 0;
        if (!st.ReadU32(ggmlType) || !st.ReadU64(relOffset)) return false;
        GgmlTypeTrait trait{};
        if (!GetGgmlTypeTrait(ggmlType, trait)) return false;
        if (relOffset > st.FileSize()) return false;

        const uint64_t blocks = (params + trait.blockElems - 1ULL) / trait.blockElems;
        const uint64_t byteSize = (blocks <= UINT64_MAX / trait.blockBytes) ? (blocks * trait.blockBytes) : UINT64_MAX;

        if (info.totalParameters <= UINT64_MAX - params) info.totalParameters += params;
        else info.totalParameters = UINT64_MAX;
        quantHist[trait.name]++;

        if (info.tensors.size() < 512) {
            AiModelInfoSummary::TensorEntry te;
            te.name = Utf8ToWideStr(tName.data(), tName.size());
            te.dtype = trait.name;
            te.shape = L"[";
            for (uint32_t d = 0; d < nDims; ++d) {
                if (d > 0) te.shape += L", ";
                te.shape += std::to_wstring(dims[d]);
            }
            te.shape += L"]";
            te.paramCount = params;
            te.byteSize = byteSize;
            info.tensors.push_back(std::move(te));
        }
    }

    info.valid = true;
    info.formatName = L"GGUF v" + std::to_wstring(version);
    info.tensorCount = tensorCount;
    info.metadataKeyCount = kvCount;

    auto findKv = [&](const std::string& k) -> std::wstring {
        auto it = kvMap.find(k);
        return (it != kvMap.end()) ? it->second : L"";
    };

    info.arch = findKv("general.architecture");
    info.modelTitle = findKv("general.name");
    info.baseModel = findKv("general.basename");
    if (!info.arch.empty()) {
        const std::string archUtf8 = PathUtils::WideToUtf8(info.arch);
        info.contextLength = findKv(archUtf8 + ".context_length");
    }
    if (info.contextLength.empty()) {
        for (const auto& kv : kvMap) {
            if (kv.first.size() > 15 && kv.first.rfind(".context_length") == kv.first.size() - 15) {
                info.contextLength = kv.second;
                break;
            }
        }
    }

    for (const auto& q : quantHist) {
        if (!info.precisionSummary.empty()) info.precisionSummary += L", ";
        info.precisionSummary += q.first + L" (" + std::to_wstring(q.second) + L")";
    }
    if (info.precisionSummary.empty()) {
        std::wstring ft = findKv("general.file_type");
        if (!ft.empty()) info.precisionSummary = L"FileType #" + ft;
    }
    return true;
}

static const wchar_t* OnnxElemTypeName(uint64_t dt, uint32_t& outElemBytes) {
    outElemBytes = 0;
    switch (dt) {
    case 1:  outElemBytes = 4;  return L"FLOAT32";
    case 2:  outElemBytes = 1;  return L"UINT8";
    case 3:  outElemBytes = 1;  return L"INT8";
    case 4:  outElemBytes = 2;  return L"UINT16";
    case 5:  outElemBytes = 2;  return L"INT16";
    case 6:  outElemBytes = 4;  return L"INT32";
    case 7:  outElemBytes = 8;  return L"INT64";
    case 8:  outElemBytes = 0;  return L"STRING";
    case 9:  outElemBytes = 1;  return L"BOOL";
    case 10: outElemBytes = 2;  return L"FLOAT16";
    case 11: outElemBytes = 8;  return L"DOUBLE";
    case 12: outElemBytes = 4;  return L"UINT32";
    case 13: outElemBytes = 8;  return L"UINT64";
    case 14: outElemBytes = 8;  return L"COMPLEX64";
    case 15: outElemBytes = 16; return L"COMPLEX128";
    case 16: outElemBytes = 2;  return L"BFLOAT16";
    case 17: outElemBytes = 1;  return L"FLOAT8E4M3FN";
    case 18: outElemBytes = 1;  return L"FLOAT8E4M3FNUZ";
    case 19: outElemBytes = 1;  return L"FLOAT8E5M2";
    case 20: outElemBytes = 1;  return L"FLOAT8E5M2FNUZ";
    case 21: outElemBytes = 1;  return L"UINT4";
    case 22: outElemBytes = 1;  return L"INT4";
    default: return L"UNKNOWN";
    }
}

static bool ParseOnnxValueInfoStream(WinAiFileStream& st, uint64_t endPos, std::wstring& outName, std::wstring& outDtype, std::wstring& outShape) {
    std::string name;
    uint64_t elemType = 0;
    std::vector<std::wstring> dims;
    while (st.Pos() < endPos) {
        uint32_t fn = 0; uint8_t wt = 0;
        if (!st.ReadPbTag(endPos, fn, wt)) return false;
        if (fn == 1 && wt == 2) {
            if (!st.ReadPbStringBounded(endPos, name, 512)) return false;
        } else if (fn == 2 && wt == 2) { // TypeProto
            uint64_t tLen = 0;
            if (!st.ReadPbVarint(endPos, tLen) || st.Pos() + tLen > endPos) return false;
            const uint64_t tEnd = st.Pos() + tLen;
            while (st.Pos() < tEnd) {
                uint32_t tfn = 0; uint8_t twt = 0;
                if (!st.ReadPbTag(tEnd, tfn, twt)) return false;
                if (tfn == 1 && twt == 2) { // TypeProto.Tensor
                    uint64_t ttLen = 0;
                    if (!st.ReadPbVarint(tEnd, ttLen) || st.Pos() + ttLen > tEnd) return false;
                    const uint64_t ttEnd = st.Pos() + ttLen;
                    while (st.Pos() < ttEnd) {
                        uint32_t xfn = 0; uint8_t xwt = 0;
                        if (!st.ReadPbTag(ttEnd, xfn, xwt)) return false;
                        if (xfn == 1 && xwt == 0) {
                            if (!st.ReadPbVarint(ttEnd, elemType)) return false;
                        } else if (xfn == 2 && xwt == 2) { // TensorShapeProto
                            uint64_t sLen = 0;
                            if (!st.ReadPbVarint(ttEnd, sLen) || st.Pos() + sLen > ttEnd) return false;
                            const uint64_t sEnd = st.Pos() + sLen;
                            while (st.Pos() < sEnd) {
                                uint32_t sfn = 0; uint8_t swt = 0;
                                if (!st.ReadPbTag(sEnd, sfn, swt)) return false;
                                if (sfn == 1 && swt == 2) { // Dimension
                                    uint64_t dLen = 0;
                                    if (!st.ReadPbVarint(sEnd, dLen) || st.Pos() + dLen > sEnd) return false;
                                    const uint64_t dEnd = st.Pos() + dLen;
                                    std::wstring dimTxt = L"?";
                                    while (st.Pos() < dEnd) {
                                        uint32_t dfn = 0; uint8_t dwt = 0;
                                        if (!st.ReadPbTag(dEnd, dfn, dwt)) return false;
                                        if (dfn == 1 && dwt == 0) {
                                            uint64_t dv = 0;
                                            if (!st.ReadPbVarint(dEnd, dv)) return false;
                                            dimTxt = std::to_wstring(static_cast<int64_t>(dv));
                                        } else if (dfn == 2 && dwt == 2) {
                                            std::string dp;
                                            if (!st.ReadPbStringBounded(dEnd, dp, 128)) return false;
                                            dimTxt = Utf8ToWideStr(dp.data(), dp.size());
                                        } else if (!st.SkipPbField(dEnd, dwt)) {
                                            return false;
                                        }
                                    }
                                    if (dims.size() < 32) dims.push_back(std::move(dimTxt));
                                } else if (!st.SkipPbField(sEnd, swt)) {
                                    return false;
                                }
                            }
                        } else if (!st.SkipPbField(ttEnd, xwt)) {
                            return false;
                        }
                    }
                } else if (!st.SkipPbField(tEnd, twt)) {
                    return false;
                }
            }
        } else if (!st.SkipPbField(endPos, wt)) {
            return false;
        }
    }
    uint32_t dummyBytes = 0;
    outName = Utf8ToWideStr(name.data(), name.size());
    outDtype = OnnxElemTypeName(elemType, dummyBytes);
    outShape = L"[";
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) outShape += L", ";
        outShape += dims[i];
    }
    outShape += L"]";
    return true;
}

static bool ParseOnnxSummary(WinAiFileStream& st, AiModelInfoSummary& info) {
    const uint64_t fileSize = st.FileSize();
    if (fileSize < 4) return false;

    uint64_t irVersion = 0;
    uint64_t modelVersion = 0;
    std::string producerName, producerVersion, domain, docString, graphName;
    std::vector<std::wstring> opsets;
    std::vector<std::pair<std::wstring, std::wstring>> metaProps;
    std::map<std::string, uint64_t> opHist;
    std::map<std::wstring, uint64_t> dtypeHist;
    std::vector<AiModelInfoSummary::TensorEntry> ioEntries;

    bool hasGraph = false;
    uint64_t nodeCount = 0;
    uint64_t initCount = 0;
    uint64_t ioCount = 0;

    while (st.Pos() < fileSize) {
        uint32_t fn = 0; uint8_t wt = 0;
        if (!st.ReadPbTag(fileSize, fn, wt)) return false;
        if (fn == 1 && wt == 0) {
            if (!st.ReadPbVarint(fileSize, irVersion)) return false;
        } else if (fn == 2 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, producerName, 256)) return false;
        } else if (fn == 3 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, producerVersion, 128)) return false;
        } else if (fn == 4 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, domain, 256)) return false;
        } else if (fn == 5 && wt == 0) {
            if (!st.ReadPbVarint(fileSize, modelVersion)) return false;
        } else if (fn == 6 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, docString, 512)) return false;
        } else if (fn == 8 && wt == 2) { // opset_import
            uint64_t oLen = 0;
            if (!st.ReadPbVarint(fileSize, oLen) || st.Pos() + oLen > fileSize) return false;
            const uint64_t oEnd = st.Pos() + oLen;
            std::string opDom;
            uint64_t opVer = 0;
            while (st.Pos() < oEnd) {
                uint32_t ofn = 0; uint8_t owt = 0;
                if (!st.ReadPbTag(oEnd, ofn, owt)) return false;
                if (ofn == 1 && owt == 2) { if (!st.ReadPbStringBounded(oEnd, opDom, 128)) return false; }
                else if (ofn == 2 && owt == 0) { if (!st.ReadPbVarint(oEnd, opVer)) return false; }
                else if (!st.SkipPbField(oEnd, owt)) return false;
            }
            if (opVer == 0 || opVer > 1000) return false;
            opsets.push_back((opDom.empty() ? L"ai.onnx" : Utf8ToWideStr(opDom.data(), opDom.size())) + L" v" + std::to_wstring(opVer));
        } else if (fn == 14 && wt == 2) { // metadata_props
            uint64_t mLen = 0;
            if (!st.ReadPbVarint(fileSize, mLen) || st.Pos() + mLen > fileSize) return false;
            const uint64_t mEnd = st.Pos() + mLen;
            std::string mk, mv;
            while (st.Pos() < mEnd) {
                uint32_t mfn = 0; uint8_t mwt = 0;
                if (!st.ReadPbTag(mEnd, mfn, mwt)) return false;
                if (mfn == 1 && mwt == 2) { if (!st.ReadPbStringBounded(mEnd, mk, 256)) return false; }
                else if (mfn == 2 && mwt == 2) { if (!st.ReadPbStringBounded(mEnd, mv, 512)) return false; }
                else if (!st.SkipPbField(mEnd, mwt)) return false;
            }
            if (!mk.empty() && metaProps.size() < 128) {
                metaProps.push_back({ Utf8ToWideStr(mk.data(), mk.size()), Utf8ToWideStr(mv.data(), mv.size()) });
            }
        } else if (fn == 7 && wt == 2) { // GraphProto
            hasGraph = true;
            uint64_t gLen = 0;
            if (!st.ReadPbVarint(fileSize, gLen) || st.Pos() + gLen > fileSize) return false;
            const uint64_t gEnd = st.Pos() + gLen;
            while (st.Pos() < gEnd) {
                uint32_t gfn = 0; uint8_t gwt = 0;
                if (!st.ReadPbTag(gEnd, gfn, gwt)) return false;
                if (gfn == 2 && gwt == 2) {
                    if (!st.ReadPbStringBounded(gEnd, graphName, 256)) return false;
                } else if (gfn == 1 && gwt == 2) { // NodeProto
                    uint64_t nLen = 0;
                    if (!st.ReadPbVarint(gEnd, nLen) || st.Pos() + nLen > gEnd) return false;
                    const uint64_t nEnd = st.Pos() + nLen;
                    std::string opType;
                    while (st.Pos() < nEnd) {
                        uint32_t nfn = 0; uint8_t nwt = 0;
                        if (!st.ReadPbTag(nEnd, nfn, nwt)) return false;
                        if (nfn == 4 && nwt == 2) {
                            if (!st.ReadPbStringBounded(nEnd, opType, 128)) return false;
                        } else if (!st.SkipPbField(nEnd, nwt)) {
                            return false;
                        }
                    }
                    ++nodeCount;
                    if (!opType.empty()) opHist[opType]++;
                } else if (gfn == 5 && gwt == 2) { // initializer TensorProto
                    uint64_t tLen = 0;
                    if (!st.ReadPbVarint(gEnd, tLen) || st.Pos() + tLen > gEnd) return false;
                    const uint64_t tEnd = st.Pos() + tLen;
                    std::vector<int64_t> dims;
                    uint64_t dt = 0;
                    std::string tName;
                    uint64_t rawDataBytes = 0;
                    while (st.Pos() < tEnd) {
                        uint32_t tfn = 0; uint8_t twt = 0;
                        if (!st.ReadPbTag(tEnd, tfn, twt)) return false;
                        if (tfn == 1 && twt == 0) {
                            uint64_t dv = 0;
                            if (!st.ReadPbVarint(tEnd, dv)) return false;
                            if (dims.size() < 32) dims.push_back(static_cast<int64_t>(dv));
                        } else if (tfn == 1 && twt == 2) { // packed repeated int64 dims
                            uint64_t pLen = 0;
                            if (!st.ReadPbVarint(tEnd, pLen) || st.Pos() + pLen > tEnd) return false;
                            const uint64_t pEnd = st.Pos() + pLen;
                            while (st.Pos() < pEnd) {
                                uint64_t dv = 0;
                                if (!st.ReadPbVarint(pEnd, dv)) return false;
                                if (dims.size() < 32) dims.push_back(static_cast<int64_t>(dv));
                            }
                        } else if (tfn == 2 && twt == 0) {
                            if (!st.ReadPbVarint(tEnd, dt)) return false;
                        } else if (tfn == 8 && twt == 2) {
                            if (!st.ReadPbStringBounded(tEnd, tName, 512)) return false;
                        } else if (tfn == 9 && twt == 2) { // raw_data (skip cleanly via Seek!)
                            uint64_t rLen = 0;
                            if (!st.ReadPbVarint(tEnd, rLen) || st.Pos() + rLen > tEnd) return false;
                            rawDataBytes = rLen;
                            if (!st.Skip(rLen)) return false;
                        } else if (!st.SkipPbField(tEnd, twt)) {
                            return false;
                        }
                    }
                    uint32_t elemBytes = 0;
                    const wchar_t* dtName = OnnxElemTypeName(dt, elemBytes);
                    if (dt == 0 || dt > 22) return false;

                    uint64_t params = 1;
                    if (dims.empty()) {
                        params = 1;
                    } else {
                        for (int64_t d : dims) {
                            if (d <= 0) { params = 0; break; }
                            const uint64_t ud = static_cast<uint64_t>(d);
                            if (params > UINT64_MAX / ud) { params = UINT64_MAX; break; }
                            params *= ud;
                        }
                    }
                    const uint64_t byteSz = (rawDataBytes > 0)
                        ? rawDataBytes
                        : ((elemBytes > 0 && params <= UINT64_MAX / elemBytes) ? (params * elemBytes) : 0ULL);

                    ++initCount;
                    if (info.totalParameters <= UINT64_MAX - params) info.totalParameters += params;
                    else info.totalParameters = UINT64_MAX;
                    dtypeHist[dtName]++;

                    if (info.tensors.size() < 512) {
                        AiModelInfoSummary::TensorEntry te;
                        te.name = tName.empty() ? (L"initializer_" + std::to_wstring(initCount)) : Utf8ToWideStr(tName.data(), tName.size());
                        te.dtype = dtName;
                        te.shape = L"[";
                        for (size_t i = 0; i < dims.size(); ++i) {
                            if (i > 0) te.shape += L", ";
                            te.shape += std::to_wstring(dims[i]);
                        }
                        te.shape += L"]";
                        te.paramCount = params;
                        te.byteSize = byteSz;
                        info.tensors.push_back(std::move(te));
                    }
                } else if ((gfn == 11 || gfn == 12) && gwt == 2) { // input / output ValueInfoProto
                    uint64_t vLen = 0;
                    if (!st.ReadPbVarint(gEnd, vLen) || st.Pos() + vLen > gEnd) return false;
                    const uint64_t vEnd = st.Pos() + vLen;
                    std::wstring vName, vDt, vShape;
                    if (!ParseOnnxValueInfoStream(st, vEnd, vName, vDt, vShape)) return false;
                    ++ioCount;
                    if (ioEntries.size() < 128) {
                        AiModelInfoSummary::TensorEntry ioTe;
                        ioTe.name = (gfn == 11 ? L"[in] " : L"[out] ") + vName;
                        ioTe.dtype = vDt;
                        ioTe.shape = vShape;
                        ioTe.paramCount = 0;
                        ioTe.byteSize = 0;
                        ioEntries.push_back(std::move(ioTe));
                    }
                } else if (!st.SkipPbField(gEnd, gwt)) {
                    return false;
                }
            }
        } else if (!st.SkipPbField(fileSize, wt)) {
            return false;
        }
    }

    if (st.Pos() != fileSize) return false;
    if (irVersion < 1 || irVersion > 20) return false;
    if (!hasGraph || (nodeCount == 0 && initCount == 0 && ioCount == 0)) return false;

    // Prepend graph I/O entries before initializer weights if room allows
    if (!ioEntries.empty()) {
        std::vector<AiModelInfoSummary::TensorEntry> merged;
        merged.reserve(ioEntries.size() + info.tensors.size());
        for (auto& e : ioEntries) merged.push_back(std::move(e));
        for (auto& e : info.tensors) {
            if (merged.size() >= 512) break;
            merged.push_back(std::move(e));
        }
        info.tensors = std::move(merged);
    }

    info.valid = true;
    info.formatName = L"ONNX v" + std::to_wstring(irVersion);
    info.modelTitle = graphName.empty() ? L"" : Utf8ToWideStr(graphName.data(), graphName.size());
    info.arch = producerName.empty()
        ? L""
        : (Utf8ToWideStr(producerName.data(), producerName.size()) +
           (producerVersion.empty() ? L"" : (L" " + Utf8ToWideStr(producerVersion.data(), producerVersion.size()))));
    info.tensorCount = initCount > 0 ? initCount : ioCount;

    for (const auto& kv : dtypeHist) {
        if (!info.precisionSummary.empty()) info.precisionSummary += L", ";
        info.precisionSummary += kv.first + L" (" + std::to_wstring(kv.second) + L")";
    }

    info.kvRows.push_back({ L"ir_version", std::to_wstring(irVersion) });
    if (!producerName.empty()) {
        info.kvRows.push_back({ L"producer", info.arch });
    }
    if (!domain.empty()) {
        info.kvRows.push_back({ L"domain", Utf8ToWideStr(domain.data(), domain.size()) });
    }
    if (modelVersion != 0) {
        info.kvRows.push_back({ L"model_version", std::to_wstring(modelVersion) });
    }
    if (!graphName.empty()) {
        info.kvRows.push_back({ L"graph.name", info.modelTitle });
    }
    info.kvRows.push_back({ L"graph.nodes", std::to_wstring(nodeCount) });
    info.kvRows.push_back({ L"graph.initializers", std::to_wstring(initCount) });
    info.kvRows.push_back({ L"graph.io_ports", std::to_wstring(ioCount) });
    if (!opsets.empty()) {
        std::wstring opJoined;
        for (size_t i = 0; i < opsets.size(); ++i) {
            if (i > 0) opJoined += L", ";
            opJoined += opsets[i];
        }
        info.kvRows.push_back({ L"opset_import", std::move(opJoined) });
    }
    if (!opHist.empty()) {
        std::vector<std::pair<std::string, uint64_t>> sortedOps(opHist.begin(), opHist.end());
        std::sort(sortedOps.begin(), sortedOps.end(), [](const auto& a, const auto& b) {
            return (a.second != b.second) ? (a.second > b.second) : (a.first < b.first);
        });
        std::wstring histStr;
        for (size_t i = 0; i < sortedOps.size() && i < 16; ++i) {
            if (i > 0) histStr += L", ";
            histStr += Utf8ToWideStr(sortedOps[i].first.data(), sortedOps[i].first.size()) +
                       L" (" + std::to_wstring(sortedOps[i].second) + L")";
        }
        info.kvRows.push_back({ L"operators", std::move(histStr) });
    }
    if (!docString.empty()) {
        std::wstring docW = Utf8ToWideStr(docString.data(), docString.size());
        if (docW.size() > 180) docW = docW.substr(0, 180) + L"...";
        info.kvRows.push_back({ L"doc_string", std::move(docW) });
    }
    for (auto& mp : metaProps) {
        info.kvRows.push_back(std::move(mp));
    }
    info.metadataKeyCount = info.kvRows.size();
    return true;
}

} // namespace

static AiModelInfoSummary InspectAiModelHeader(const std::wstring& path, const std::wstring& ext) {
    AiModelInfoSummary info;
    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return info;

    LARGE_INTEGER liSize{};
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart <= 0) {
        CloseHandle(hFile);
        return info;
    }
    WinAiFileStream stream(hFile, static_cast<uint64_t>(liSize.QuadPart));

    const std::wstring extLower = PathUtils::ToLower(ext.empty() ? PathUtils::GetExtension(path) : ext);
    if (extLower == L".safetensors") {
        AiModelInfoSummary parsed;
        if (ParseSafeTensorsSummary(stream, parsed)) info = std::move(parsed);
    } else if (extLower == L".gguf") {
        AiModelInfoSummary parsed;
        if (ParseGgufSummary(stream, parsed)) info = std::move(parsed);
    } else if (extLower == L".onnx") {
        AiModelInfoSummary parsed;
        if (ParseOnnxSummary(stream, parsed)) info = std::move(parsed);
    }

    CloseHandle(hFile);
    return info;
}

AiModelInfoSummary FileContentPane::InspectAiModelFile(const std::wstring& path, const std::wstring& ext) {
    return InspectAiModelHeader(path, ext);
}

static bool ComputeFileHashes(const std::wstring& path, std::wstring& outSummary, std::wstring& outSha256Hex) {
    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    // 1. CRC32 table
    static uint32_t crcTable[256] = {};
    static bool crcInit = false;
    if (!crcInit) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            crcTable[i] = c;
        }
        crcInit = true;
    }

    // 2. Windows BCrypt SHA-256
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::vector<uint8_t> hashObj;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 && hAlg) {
        DWORD objLen = 0, cbData = 0;
        if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cbData, 0) == 0 && objLen > 0) {
            hashObj.resize(objLen);
            BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0);
        }
    }

    uint32_t crc = 0xFFFFFFFFu;
    uint64_t fnv64 = 14695981039346656037ULL;
    std::vector<uint8_t> buf(256 * 1024);
    DWORD got = 0;
    uint64_t totalRead = 0;
    const uint64_t kMaxSyncBytes = 512ULL * 1024 * 1024; // cap synchronous UI hash at 512 MB

    while (ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) && got > 0) {
        for (DWORD i = 0; i < got; ++i) {
            uint8_t b = buf[i];
            crc = crcTable[(crc ^ b) & 0xFFu] ^ (crc >> 8);
            fnv64 ^= static_cast<uint64_t>(b);
            fnv64 *= 1099511628211ULL;
        }
        if (hHash) {
            BCryptHashData(hHash, buf.data(), got, 0);
        }
        totalRead += got;
        if (totalRead >= kMaxSyncBytes) break;
    }
    CloseHandle(hFile);
    crc ^= 0xFFFFFFFFu;

    uint8_t sha256[32] = {};
    bool hasSha = false;
    if (hHash) {
        if (BCryptFinishHash(hHash, sha256, 32, 0) == 0) hasSha = true;
        BCryptDestroyHash(hHash);
    }
    if (hAlg) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    wchar_t crcBuf[32] = {};
    swprintf_s(crcBuf, L"%08X", crc);
    wchar_t fnvBuf[32] = {};
    swprintf_s(fnvBuf, L"%016llX", static_cast<unsigned long long>(fnv64));

    outSha256Hex.clear();
    if (hasSha) {
        for (int i = 0; i < 32; ++i) {
            wchar_t hb[4] = {};
            swprintf_s(hb, L"%02x", sha256[i]);
            outSha256Hex += hb;
        }
    }

    outSummary = std::wstring(L"CRC32: ") + crcBuf + L"   \x2022   Hash64: " + fnvBuf;
    if (!outSha256Hex.empty()) {
        outSummary += L"   \x2022   SHA-256: " + outSha256Hex;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// Reading previews on a worker thread (see FileContentPane::OnPreviewLoaded)
// ---------------------------------------------------------------------------------------------------------

namespace {
// A preview read within this long is shown as if it had been read on the window's thread, so that no note
// flashes up for it. The window waits this long only when the worker was free.
constexpr DWORD kPreviewQuickMs = 50;
// For the same reason the note (a moving mark, and what is being read) appears only after this long
constexpr ULONGLONG kWaitNoteDelayMs = 300;
}

struct FileContentPane::PreviewJob {
    uint64_t generation = 0;
    PreviewJobKind kind = PreviewJobKind::None;
    std::wstring path;          // the file or folder to read; Fetch: the FTP address, or the archive
    std::wstring innerPath;     // Fetch out of an archive: the entry
    std::wstring ext;           // AiModel: the file's extension
    uint32_t page = 0;          // PdfPage
};

struct FileContentPane::PreviewResult {
    uint64_t generation = 0;
    PreviewJobKind kind = PreviewJobKind::None;
    std::wstring path;
    bool ok = false;
    std::wstring localPath;                     // Fetch: the downloaded or unpacked copy
    PdfDocumentSummary pdf;
    PdfViewer::PagePicture page;
    ArchiveSummaryInfo archive;
    std::vector<ArchiveItemEntry> archiveRoot;
    ParsedOfficeDocument office;
    ParsedEdaBoard eda;
    AiModelInfoSummary aiModel;
    std::vector<DirPreviewEntry> folderItems;
    size_t folderCount = 0;
    size_t fileCount = 0;
    uint64_t folderBytes = 0;
};

// The worker thread holds this too, so it never points at a pane that is gone
struct FileContentPane::PreviewWorker {
    std::mutex mutex;                           // guards everything but the atomics
    std::condition_variable wake;               // to the worker: a job, a stop, or its job given up
    std::condition_variable finished;           // to the pane: the worker has finished a job
    std::atomic<bool> stop{ false };
    std::atomic<uint64_t> wanted{ 0 };          // the job the pane waits for; work on any other is given up
    bool busy = false;
    bool hasJob = false;
    PreviewJob job;                             // the newest job not started yet
    std::unique_ptr<PreviewResult> result;      // the result the pane waits for, until it takes it
    HWND notify = nullptr;
    DWORD delayMs = 0;                          // tests: a pause before each job
    uint64_t jobsFinished = 0;
    uint64_t jobsRead = 0;
};

void FileContentPane::PreviewWorkerLoop(std::shared_ptr<PreviewWorker> w) {
    // Some readers use COM (Windows' imaging components). This thread has no window, so it joins the
    // multithreaded apartment.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        PreviewJob job;
        {
            std::unique_lock<std::mutex> lock(w->mutex);
            w->wake.wait(lock, [&]() { return w->stop.load() || w->hasJob; });
            if (w->stop.load()) break;
            job = std::move(w->job);
            w->job = PreviewJob();
            w->hasJob = false;
            w->busy = true;
            if (w->delayMs > 0) {
                const uint64_t waitFor = job.generation;
                w->wake.wait_for(lock, std::chrono::milliseconds(w->delayMs),
                    [&]() { return w->stop.load() || w->wanted.load() != waitFor; });
            }
        }

        const uint64_t gen = job.generation;
        const std::function<bool()> stillWanted = [&w, gen]() { return !w->stop.load() && w->wanted.load() == gen; };
        auto result = std::make_unique<PreviewResult>();
        result->generation = gen;
        result->kind = job.kind;
        result->path = job.path;
        const bool read = stillWanted();
        if (read) {
            try {
                RunPreviewJob(job, *result, stillWanted);
            } catch (...) {
                result->ok = false;     // the readers catch what they throw; this is for running out of memory
            }
        }

        bool post = false;
        HWND notify = nullptr;
        {
            std::lock_guard<std::mutex> lock(w->mutex);
            w->busy = false;
            ++w->jobsFinished;
            if (read) ++w->jobsRead;
            if (stillWanted()) {
                w->result = std::move(result);
                post = true;
                notify = w->notify;
            }
            w->finished.notify_all();
        }
        result.reset();     // read for nothing: another file was selected meanwhile
        if (post && notify) PostMessageW(notify, WM_APP_PREVIEW_READY, 0, 0);
    }
    if (SUCCEEDED(com)) CoUninitialize();
}

void FileContentPane::RunPreviewJob(const PreviewJob& job, PreviewResult& out, const std::function<bool()>& keepGoing) {
    switch (job.kind) {
    case PreviewJobKind::Fetch:
        if (FtpClient::IsFtpPath(job.path)) {
            out.ok = FtpClient::DownloadFtpFileToTempCache(job.path, out.localPath, keepGoing);
        } else {
            out.ok = ArchiveExplorer::ExtractSingleEntryToTempFile(job.path, job.innerPath, out.localPath);
        }
        out.ok = out.ok && !out.localPath.empty();
        break;
    case PreviewJobKind::Pdf:
        out.ok = PdfViewer::InspectPdfFile(job.path, out.pdf);
        // A document the reader turned down is shown as unreadable, picture or not
        if (out.pdf.valid && keepGoing()) {
            PdfViewer::RenderPagePicture(job.path, 0, out.page);
        }
        break;
    case PreviewJobKind::PdfPage:
        PdfViewer::RenderPagePicture(job.path, job.page, out.page);
        out.ok = !out.page.bgra.empty();
        break;
    case PreviewJobKind::Archive:
        out.ok = ArchiveExplorer::EnumerateArchiveFolder(job.path, L"", out.archiveRoot, &out.archive);
        break;
    case PreviewJobKind::Office:
        out.ok = SpreadsheetDocViewer::InspectOfficeFile(job.path, out.office);
        break;
    case PreviewJobKind::Eda:
        out.ok = EdaViewer::InspectEdaFile(job.path, out.eda);
        break;
    case PreviewJobKind::AiModel:
        out.aiModel = InspectAiModelHeader(job.path, job.ext);
        out.ok = out.aiModel.valid;
        break;
    case PreviewJobKind::Folder:
        ListFolderForPreview(job.path, out, keepGoing);
        out.ok = true;
        break;
    default:
        break;
    }
}

void FileContentPane::ListFolderForPreview(const std::wstring& folderPath, PreviewResult& out, const std::function<bool()>& keepGoing) {
    std::wstring searchPattern = PathUtils::EnsureTrailingSlash(folderPath) + L"*";
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileExW(
        searchPattern.c_str(),
        FindExInfoBasic,
        &fd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE) {
        std::wstring arcFile, innerDir;
        if (ArchiveExplorer::SplitVirtualArchivePath(folderPath, arcFile, innerDir)) {
            std::vector<ArchiveItemEntry> arcChildren;
            if (ArchiveExplorer::EnumerateArchiveFolder(arcFile, innerDir, arcChildren)) {
                for (const auto& ac : arcChildren) {
                    if (ac.isDirectory) {
                        ++out.folderCount;
                    } else {
                        ++out.fileCount;
                        out.folderBytes += ac.uncompressedSize;
                    }
                    if (out.folderItems.size() < 2500) {
                        DirPreviewEntry item;
                        item.name = ac.name;
                        item.fullPath = PathUtils::EnsureTrailingSlash(folderPath) + ac.name;
                        item.isDirectory = ac.isDirectory;
                        item.size = ac.uncompressedSize;
                        if (!ac.isDirectory) {
                            item.ext = PathUtils::GetExtension(ac.name);
                            item.isImage = IsImageExtension(item.ext);
                            item.sizeStr = FileEngine::FormatSizeKB(ac.uncompressedSize);
                        }
                        out.folderItems.push_back(std::move(item));
                    }
                }
            }
        }
        return;
    }

    size_t seen = 0;
    do {
        if ((++seen & 255) == 0 && !keepGoing()) break;     // another file or folder was selected meanwhile
        if (fd.cFileName[0] == L'.') {
            if (fd.cFileName[1] == L'\0' || (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0')) {
                continue;
            }
        }
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        uint64_t sz = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        if (isDir) {
            ++out.folderCount;
        } else {
            ++out.fileCount;
            out.folderBytes += sz;
        }

        if (out.folderItems.size() < 2500) {
            DirPreviewEntry item;
            item.name = fd.cFileName;
            item.fullPath = PathUtils::EnsureTrailingSlash(folderPath) + item.name;
            item.isDirectory = isDir;
            item.size = sz;
            if (!isDir) {
                item.ext = PathUtils::GetExtension(item.name);
                item.isImage = IsImageExtension(item.ext);
                item.sizeStr = FileEngine::FormatSizeKB(sz);
            }
            item.dateStr = FileEngine::FormatFileTime(fd.ftLastWriteTime);
            out.folderItems.push_back(std::move(item));
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    std::sort(out.folderItems.begin(), out.folderItems.end(),
        [](const DirPreviewEntry& a, const DirPreviewEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
}

bool FileContentPane::EnsurePreviewWorker() {
    if (m_workerThread.joinable()) return true;
    if (!m_worker) m_worker = std::make_shared<PreviewWorker>();
    {
        std::lock_guard<std::mutex> lock(m_worker->mutex);
        m_worker->notify = m_parentHwnd;
        m_worker->stop.store(false);
    }
    try {
        m_workerThread = std::thread(&FileContentPane::PreviewWorkerLoop, m_worker);
    } catch (const std::system_error&) {
        return false;
    }
    return true;
}

void FileContentPane::StartPreviewJob(PreviewJob&& job) {
    job.generation = ++m_previewGeneration;
    const uint64_t gen = job.generation;
    m_previewWanted = gen;
    m_previewWantedKind = job.kind;
    m_previewWantedPath = job.path;
    m_previewStartTick = GetTickCount64();

    if (!EnsurePreviewWorker()) {
        // No thread to be had: read it here, as it used to be
        PreviewResult result;
        result.generation = gen;
        result.kind = job.kind;
        result.path = job.path;
        try {
            RunPreviewJob(job, result, []() { return true; });
        } catch (...) {
            result.ok = false;
        }
        m_previewWanted = 0;
        m_previewWantedKind = PreviewJobKind::None;
        m_previewWantedPath.clear();
        ShowPreviewResult(result);
        return;
    }

    std::unique_ptr<PreviewResult> stale;
    bool workerFree = false;
    {
        std::lock_guard<std::mutex> lock(m_worker->mutex);
        m_worker->job = std::move(job);
        m_worker->hasJob = true;
        m_worker->wanted.store(gen);
        m_worker->delayMs = m_previewDelayForTesting;
        stale = std::move(m_worker->result);
        workerFree = !m_worker->busy;
        m_worker->wake.notify_all();
    }
    stale.reset();

    if (workerFree) {
        bool ready = false;
        {
            std::unique_lock<std::mutex> lock(m_worker->mutex);
            ready = m_worker->finished.wait_for(lock, std::chrono::milliseconds(kPreviewQuickMs), [&]() {
                return (m_worker->result && m_worker->result->generation == gen) || (!m_worker->busy && !m_worker->hasJob);
            });
        }
        if (ready) OnPreviewLoaded();
    }
}

bool FileContentPane::OnPreviewLoaded() {
    if (!m_worker) return false;
    std::unique_ptr<PreviewResult> result;
    {
        std::lock_guard<std::mutex> lock(m_worker->mutex);
        result = std::move(m_worker->result);
    }
    if (!result || m_previewWanted == 0 || result->generation != m_previewWanted) return false;
    m_previewWanted = 0;
    m_previewWantedKind = PreviewJobKind::None;
    m_previewWantedPath.clear();
    m_worker->wanted.store(0);
    ShowPreviewResult(*result);
    return true;
}

void FileContentPane::StopWaitingForPreview() {
    if (m_previewWanted == 0) return;
    m_previewWanted = 0;
    m_previewWantedKind = PreviewJobKind::None;
    m_previewWantedPath.clear();
    if (!m_worker) return;
    std::unique_ptr<PreviewResult> stale;
    {
        std::lock_guard<std::mutex> lock(m_worker->mutex);
        m_worker->hasJob = false;
        m_worker->job = PreviewJob();
        m_worker->wanted.store(0);
        stale = std::move(m_worker->result);
        m_worker->wake.notify_all();    // a job the worker is pausing over (tests) ends at once
    }
}

void FileContentPane::StopPreviewWorker() {
    StopWaitingForPreview();
    m_fetching = false;
    if (m_worker) {
        std::lock_guard<std::mutex> lock(m_worker->mutex);
        m_worker->stop.store(true);
        m_worker->hasJob = false;
        m_worker->wake.notify_all();
    }
    // A job being read is finished first: a download stops at once (it asks whether to go on), a file
    // being read takes as long as it takes, which is never longer than it used to hold the window up.
    // Waiting here also means that no PDF page is drawn after this, so the PDF library can be let go of
    // (PdfViewer::ReleaseRenderer).
    JoinServingCom(m_workerThread);
    if (m_worker) {
        std::unique_ptr<PreviewResult> stale;
        {
            std::lock_guard<std::mutex> lock(m_worker->mutex);
            stale = std::move(m_worker->result);
        }
    }
}

bool FileContentPane::IsWaitingFor(PreviewJobKind kind, const std::wstring& path) const {
    return m_previewWanted != 0 && m_previewWantedKind == kind && m_previewWantedPath == path;
}

void FileContentPane::RequestPreview(PreviewJobKind kind, const std::wstring& path, bool alreadyShown) {
    if (alreadyShown) {
        // Nothing to read. A page of this document that is being drawn is still wanted; anything else is not.
        if (!IsWaitingFor(PreviewJobKind::PdfPage, path)) StopWaitingForPreview();
        return;
    }
    if (IsWaitingFor(kind, path)) return;      // on its way already

    // What the viewer holds is of another file: it goes now, so nothing of it can be clicked meanwhile
    switch (kind) {
    case PreviewJobKind::Pdf: m_pdf.Clear(); break;
    case PreviewJobKind::Archive: m_archive.Clear(); break;
    case PreviewJobKind::Office: m_office.Clear(); break;
    case PreviewJobKind::Eda: m_eda.Clear(); break;
    case PreviewJobKind::AiModel:
        m_aiModelPath.clear();
        m_aiModelInfo = AiModelInfoSummary();
        m_aiModelScrollY = 0.0f;
        break;
    default:
        break;
    }
    PreviewJob job;
    job.kind = kind;
    job.path = path;
    job.ext = m_currentEntry.ext;
    StartPreviewJob(std::move(job));
}

void FileContentPane::RequestPdfPage(uint32_t page) {
    PreviewJob job;
    job.kind = PreviewJobKind::PdfPage;
    job.path = m_pdf.LoadedPath();
    job.page = page;
    StartPreviewJob(std::move(job));
}

void FileContentPane::ForwardPdfPageRequest() {
    uint32_t page = 0;
    if (m_pdf.TakePageRequest(page)) RequestPdfPage(page);
}

void FileContentPane::TurnPdfPageForTesting(int direction) {
    if (m_pdf.TurnPage(direction)) ForwardPdfPageRequest();
}

void FileContentPane::ShowCurrentEntry() {
    m_fetching = false;
    m_fetchFailed = false;
    if (!m_currentEntry.isDirectory && !m_currentEntry.isDrive) {
        if (FtpClient::IsFtpPath(m_currentEntry.fullPath)) {
            StartFetch(m_currentEntry.fullPath, std::wstring());
            return;
        }
        std::wstring arcFile, innerPath;
        if (ArchiveExplorer::SplitVirtualArchivePath(m_currentEntry.fullPath, arcFile, innerPath) && !innerPath.empty()) {
            StartFetch(arcFile, innerPath);
            return;
        }
    }
    ActivateResolvedViewer();
}

void FileContentPane::StartFetch(const std::wstring& source, const std::wstring& innerPath) {
    // The viewer is chosen once the copy is here (some are chosen by what the file holds)
    m_fetching = true;
    m_fetchFailed = false;
    m_resolvedKind = ResolvedContentKind::None;
    m_notepad.HideWindow();
    PreviewJob job;
    job.kind = PreviewJobKind::Fetch;
    job.path = source;
    job.innerPath = innerPath;
    StartPreviewJob(std::move(job));
}

void FileContentPane::ShowPreviewResult(PreviewResult& result) {
    ++m_resultsShown;
    switch (result.kind) {
    case PreviewJobKind::Fetch:
        m_fetching = false;
        if (result.ok) {
            m_currentEntry.fullPath = result.localPath;
        } else {
            m_fetchFailed = true;
        }
        ActivateResolvedViewer();
        break;
    case PreviewJobKind::Pdf:
        m_pdf.ShowLoadedDocument(result.path, std::move(result.pdf), std::move(result.page));
        break;
    case PreviewJobKind::PdfPage:
        m_pdf.ShowPagePicture(result.path, std::move(result.page));
        break;
    case PreviewJobKind::Archive:
        m_archive.ShowLoadedArchive(result.path, std::move(result.archive), std::move(result.archiveRoot));
        break;
    case PreviewJobKind::Office:
        m_office.ShowLoadedDocument(result.path, std::move(result.office));
        break;
    case PreviewJobKind::Eda:
        m_eda.ShowLoadedBoard(result.path, std::move(result.eda));
        break;
    case PreviewJobKind::AiModel:
        m_aiModelPath = result.path;
        m_aiModelInfo = std::move(result.aiModel);
        m_aiModelScrollY = 0.0f;
        break;
    case PreviewJobKind::Folder:
        m_dirPreviewPath = result.path;
        m_dirPreviewItems = std::move(result.folderItems);
        m_dirFolderCount = result.folderCount;
        m_dirFileCount = result.fileCount;
        m_dirTotalFileBytes = result.folderBytes;
        m_dirScrollY = 0.0f;
        m_dirSelectedIdx = -1;
        m_dirHoverIdx = -1;
        break;
    default:
        break;
    }
}

FileContentPane::PreviewTestState FileContentPane::GetPreviewStateForTesting() const {
    PreviewTestState s;
    s.kind = m_resolvedKind;
    s.loading = m_previewWanted != 0;
    s.fetching = m_fetching;
    s.fetchFailed = m_fetchFailed;
    s.currentPath = m_currentEntry.fullPath;
    s.pdfPath = m_pdf.LoadedPath();
    s.pdfPages = m_pdf.PageCount();
    s.pdfCurrentPage = m_pdf.CurrentPage();
    s.pdfPicturePage = m_pdf.PicturePage();
    s.pdfPictureWidth = m_pdf.PictureWidth();
    s.archivePath = m_archive.LoadedPath();
    s.archiveEntries = m_archive.EntryCount();
    s.officePath = m_office.LoadedPath();
    s.officeValid = m_office.HasValidDocument();
    s.edaPath = m_eda.LoadedPath();
    s.edaValid = m_eda.HasValidBoard();
    s.folderPath = m_dirPreviewPath;
    s.folderItems = m_dirPreviewItems.size();
    if (m_worker) {
        std::lock_guard<std::mutex> lock(m_worker->mutex);
        s.jobsFinished = m_worker->jobsFinished;
        s.jobsRead = m_worker->jobsRead;
    }
    s.resultsShown = m_resultsShown;
    return s;
}

void FileContentPane::DrawWaitNote(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, IDWriteTextFormat* fmtCenterSmall, bool pl) {
    // For a short while the body stays empty: a preview read within a moment must not flash a note up
    const ULONGLONG waited = GetTickCount64() - m_previewStartTick;
    if (waited < kWaitNoteDelayMs) return;

    const float cx = (m_bodyRect.left + m_bodyRect.right) * 0.5f;
    const float cy = m_bodyRect.top + std::min(120.0f, std::max(40.0f, (m_bodyRect.bottom - m_bodyRect.top) * 0.3f));
    // Eight dots in a ring; the bright one goes round once in 0.8 s (the window repaints every 100 ms while
    // something is being read)
    const int head = static_cast<int>((GetTickCount64() / 100) % 8);
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * 0.7853982f - 1.5707963f;
        const int behind = (head - i + 8) % 8;
        brush->SetColor(FcpColor(0x0078D7, std::max(0.18f, 1.0f - static_cast<float>(behind) * 0.12f)));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + std::cos(angle) * 13.0f, cy + std::sin(angle) * 13.0f), 2.8f, 2.8f), brush);
    }

    std::wstring text;
    if (m_fetching) {
        text = FtpClient::IsFtpPath(m_currentEntry.fullPath)
            ? (pl ? L"Pobieranie pliku z serwera FTP\x2026" : L"Downloading the file from the FTP server\x2026")
            : (pl ? L"Rozpakowywanie pliku z archiwum\x2026" : L"Unpacking the file from the archive\x2026");
    } else if (m_previewWantedKind == PreviewJobKind::Folder) {
        text = pl ? L"Wczytywanie zawartości folderu\x2026" : L"Reading the folder\x2026";
    } else {
        text = pl ? L"Wczytywanie podglądu\x2026" : L"Loading the preview\x2026";
    }
    text += L"\n" + m_currentEntry.name;
    const ULONGLONG seconds = waited / 1000;
    if (seconds >= 2) text += L"   \x2022   " + std::to_wstring(seconds) + L" s";
    D2D1_RECT_F textR = D2D1::RectF(m_bodyRect.left + 16.0f, cy + 24.0f, m_bodyRect.right - 16.0f, cy + 72.0f);
    brush->SetColor(FcpColor(0x475569));
    rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmtCenterSmall, textR, brush);
}

FileContentPane::~FileContentPane() {
    Shutdown();
}

void FileContentPane::Initialize(
    HWND parentHwnd,
    HINSTANCE hInstance,
    ID2D1Factory* d2dFactory,
    IDWriteFactory* dwriteFactory)
{
    m_parentHwnd = parentHwnd;
    m_notepad.Initialize(parentHwnd, hInstance, dwriteFactory);
    m_hex.Initialize(parentHwnd, dwriteFactory);
    m_model3D.Initialize(parentHwnd, d2dFactory);
    m_eda.Initialize(parentHwnd, d2dFactory);
    m_office.Initialize(parentHwnd);
    m_archive.Initialize(parentHwnd);
    m_pdf.Initialize(parentHwnd);
    m_pdf.SetPagesDrawnByOwner(true);       // turned pages are drawn by the preview worker
}

void FileContentPane::Shutdown() {
    StopPreviewWorker();
    m_notepad.Shutdown();
    m_hex.Shutdown();
    m_model3D.Shutdown();
    m_eda.Shutdown();
    m_office.Shutdown();
    m_archive.Shutdown();
    m_pdf.Shutdown();
}

void FileContentPane::HideNativeControls() {
    m_notepad.HideWindow();
}

void FileContentPane::LoadDirectoryPreview(const std::wstring& folderPath, bool forceReload) {
    if (!forceReload && m_dirPreviewPath == folderPath && !folderPath.empty()) {
        StopWaitingForPreview();
        return;
    }
    if (!forceReload && IsWaitingFor(PreviewJobKind::Folder, folderPath)) return;
    m_dirPreviewPath.clear();
    m_dirPreviewItems.clear();
    m_dirFolderCount = 0;
    m_dirFileCount = 0;
    m_dirTotalFileBytes = 0;
    m_dirScrollY = 0.0f;
    m_dirSelectedIdx = -1;
    m_dirHoverIdx = -1;

    if (folderPath.empty() || IsVirtualFolder(folderPath)) {
        StopWaitingForPreview();
        m_dirPreviewPath = folderPath;
        return;
    }

    // A folder on a slow disk or a network share, or inside a large archive, can take a while to list
    PreviewJob job;
    job.kind = PreviewJobKind::Folder;
    job.path = folderPath;
    StartPreviewJob(std::move(job));
}

ResolvedContentKind FileContentPane::ResolveKindForCurrentEntry() const {
    if (!m_hasEntry || m_currentEntry.fullPath.empty()) {
        return ResolvedContentKind::None;
    }
    if (m_currentEntry.isDirectory || m_currentEntry.isDrive) {
        return ResolvedContentKind::DirectoryInfo;
    }

    if (m_userTabMode == FileContentTabMode::Notepad) {
        return ResolvedContentKind::NotepadEditor;
    }
    if (m_userTabMode == FileContentTabMode::Hex) {
        return ResolvedContentKind::HexInspector;
    }
    if (m_userTabMode == FileContentTabMode::Model3D) {
        return ResolvedContentKind::Model3DViewport;
    }
    if (m_userTabMode == FileContentTabMode::EdaPcb) {
        return ResolvedContentKind::EdaViewport;
    }
    if (m_userTabMode == FileContentTabMode::Office) {
        return ResolvedContentKind::OfficeDocument;
    }
    if (m_userTabMode == FileContentTabMode::Archive) {
        return ResolvedContentKind::ArchiveSubExplorer;
    }
    if (m_userTabMode == FileContentTabMode::Pdf) {
        return ResolvedContentKind::PdfDocument;
    }
    if (m_userTabMode == FileContentTabMode::AiModel) {
        return ResolvedContentKind::AiModelInspector;
    }

    // Auto detection based on file type & content
    const std::wstring& ext = m_currentEntry.ext;
    if (IsAiModelExtension(ext)) {
        return ResolvedContentKind::AiModelInspector;
    }
    if (PdfViewer::IsPdfExtension(ext)) {
        return ResolvedContentKind::PdfDocument;
    }
    if (ext == L".dxf") {
        if (Model3DViewer::IsLikely3DDxfFile(m_currentEntry.fullPath)) {
            return ResolvedContentKind::Model3DViewport;
        }
        return ResolvedContentKind::EdaViewport;
    }
    if (ext == L".raw" && !EdaViewer::IsLikelySpiceRawFile(m_currentEntry.fullPath)) {
        return ResolvedContentKind::HexInspector;
    }
    if (ext == L".def" && !EdaViewer::IsLikelyAsicDefFile(m_currentEntry.fullPath)) {
        return ResolvedContentKind::NotepadEditor;
    }
    if (ext == L".json" && !EdaViewer::IsLikelyEasyEdaJsonFile(m_currentEntry.fullPath)) {
        return ResolvedContentKind::NotepadEditor;
    }
    if (ext == L".lib" && !EdaViewer::IsLikelyLibertyOrEdaSdfFile(m_currentEntry.fullPath, ext)) {
        if (ArchiveExplorer::IsArchiveExtension(ext)) {
            return ResolvedContentKind::ArchiveSubExplorer;
        }
        return ResolvedContentKind::HexInspector;
    }
    if (ext == L".sdf" && !EdaViewer::IsLikelyLibertyOrEdaSdfFile(m_currentEntry.fullPath, ext)) {
        return ResolvedContentKind::Model3DViewport;
    }
    if (ext == L".tgz" && !EdaViewer::IsLikelyOdbPlusPlusTgz(m_currentEntry.fullPath)) {
        return ResolvedContentKind::ArchiveSubExplorer;
    }
    if (ext == L".ipc") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 16, hdr) && hdr.size() >= 8 &&
            (std::memcmp(hdr.data(), "ARROW1", 6) == 0 ||
             (hdr[0] == 0xFF && hdr[1] == 0xFF && hdr[2] == 0xFF && hdr[3] == 0xFF))) {
            return ResolvedContentKind::OfficeDocument;
        }
    }
    if (EdaViewer::IsEdaExtension(ext)) {
        return ResolvedContentKind::EdaViewport;
    }
    if (ext == L".pdb") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 32, hdr) &&
            hdr.size() >= 19 && std::memcmp(hdr.data(), "Microsoft C/C++ MSF", 19) == 0) {
            return ResolvedContentKind::OfficeDocument;
        }
        return ResolvedContentKind::Model3DViewport;
    }
    if (ext == L".fit") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 16, hdr) &&
            hdr.size() >= 12 && std::memcmp(hdr.data() + 8, ".FIT", 4) == 0) {
            return ResolvedContentKind::OfficeDocument;
        }
        return ResolvedContentKind::ImagePreview;
    }
    if (ext == L".dat") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 8, hdr) &&
            hdr.size() >= 4 && std::memcmp(hdr.data(), "regf", 4) == 0) {
            return ResolvedContentKind::OfficeDocument;
        }
    }
    if (ext == L".res") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 32, hdr) && hdr.size() >= 4) {
            if (std::memcmp(hdr.data(), "RSRC", 4) == 0 || std::memcmp(hdr.data(), "RSCC", 4) == 0 ||
                std::memcmp(hdr.data(), "[gd_", 4) == 0) {
                return ResolvedContentKind::Model3DViewport;
            }
        }
        return ResolvedContentKind::HexInspector;
    }
    if (ext == L".mod") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 16, hdr) && hdr.size() >= 8 &&
            (std::memcmp(hdr.data(), "MOD V1.", 7) == 0 || std::memcmp(hdr.data(), "ERF V1.", 7) == 0)) {
            return ResolvedContentKind::ArchiveSubExplorer;
        }
        return ResolvedContentKind::OfficeDocument;
    }
    if (ext == L".uasset" || ext == L".umap") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 65536, hdr)) {
            auto uKind = Formats::ClassifyUnrealPackageMemory(hdr.data(), hdr.size());
            if (uKind == Formats::UnrealPackageAssetKind::StaticMesh3D) return ResolvedContentKind::Model3DViewport;
            if (uKind == Formats::UnrealPackageAssetKind::Texture2D)    return ResolvedContentKind::ImagePreview;
        }
        return ResolvedContentKind::OfficeDocument;
    }
    if (ext == L".shp" || ext == L".geojson") {
        if (Model3DViewer::IsLikely3DGeoOrDemFile(m_currentEntry.fullPath, ext)) {
            return ResolvedContentKind::Model3DViewport;
        }
        return ResolvedContentKind::OfficeDocument;
    }
    if ((ext == L".tif" || ext == L".tiff") &&
        Model3DViewer::IsLikely3DGeoOrDemFile(m_currentEntry.fullPath, ext)) {
        return ResolvedContentKind::Model3DViewport;
    }
    if (ext == L".tex") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 8, hdr) && hdr.size() >= 4 &&
            (std::memcmp(hdr.data(), "TEX\0", 4) == 0 || std::memcmp(hdr.data(), "\0XET", 4) == 0)) {
            return ResolvedContentKind::ImagePreview;
        }
    }
    if (ext == L".wad") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 8, hdr) && hdr.size() >= 4 &&
            (std::memcmp(hdr.data(), "WAD2", 4) == 0 || std::memcmp(hdr.data(), "WAD3", 4) == 0)) {
            return ResolvedContentKind::ImagePreview;
        }
        return ResolvedContentKind::ArchiveSubExplorer;
    }
    if (ext == L".nc") {
        std::vector<uint8_t> hdr;
        if (PathUtils::ReadFileBytes(m_currentEntry.fullPath, 560, hdr)) {
            if ((hdr.size() >= 4 && std::memcmp(hdr.data(), "CDF", 3) == 0 && (hdr[3] == 1 || hdr[3] == 2 || hdr[3] == 5)) ||
                (hdr.size() >= 8 && std::memcmp(hdr.data(), "\x89HDF\r\n\x1A\n", 8) == 0) ||
                (hdr.size() >= 520 && std::memcmp(hdr.data() + 512, "\x89HDF\r\n\x1A\n", 8) == 0)) {
                return ResolvedContentKind::OfficeDocument;
            }
        }
        return ResolvedContentKind::Model3DViewport;
    }
    if (Model3DViewer::Is3DModelExtension(ext)) {
        return ResolvedContentKind::Model3DViewport;
    }
    if (ArchiveExplorer::IsArchiveExtension(ext)) {
        return ResolvedContentKind::ArchiveSubExplorer;
    }
    if (m_currentEntry.isImage) {
        return ResolvedContentKind::ImagePreview;
    }
    if (m_currentEntry.isVideo || m_currentEntry.isAudio) {
        return ResolvedContentKind::MediaPreview;
    }
    if (SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(ext)) {
        return ResolvedContentKind::OfficeDocument;
    }
    if (NotepadViewer::IsLikelyTextFile(m_currentEntry.fullPath, ext, m_currentEntry.size)) {
        return ResolvedContentKind::NotepadEditor;
    }
    return ResolvedContentKind::HexInspector;
}

void FileContentPane::ActivateResolvedViewer() {
    if (m_fetching || m_fetchFailed) {
        // The selected file is still on its way here, or could not be fetched: no viewer for it (yet)
        m_resolvedKind = ResolvedContentKind::None;
        m_notepad.HideWindow();
        return;
    }
    m_resolvedKind = ResolveKindForCurrentEntry();
    if (m_resolvedKind != ResolvedContentKind::NotepadEditor) {
        m_notepad.HideWindow();
    }

    if (!m_hasEntry) {
        StopWaitingForPreview();
        return;
    }

    if (m_currentEntry.isDirectory || m_currentEntry.isDrive) {
        LoadDirectoryPreview(m_currentEntry.fullPath, false);
        return;
    }

    // The readers that can take a while run on the worker (RequestPreview). The others read a little of the
    // file, or have a thread of their own (3D).
    const std::wstring loadPath = m_currentEntry.fullPath;
    switch (m_resolvedKind) {
    case ResolvedContentKind::NotepadEditor:
        StopWaitingForPreview();
        m_notepad.LoadFile(loadPath, false);
        break;
    case ResolvedContentKind::HexInspector:
        StopWaitingForPreview();
        m_hex.LoadFile(loadPath, false);
        break;
    case ResolvedContentKind::Model3DViewport:
        StopWaitingForPreview();
        m_model3D.LoadFile(loadPath, false);
        break;
    case ResolvedContentKind::EdaViewport:
        RequestPreview(PreviewJobKind::Eda, loadPath, m_eda.IsShowing(loadPath));
        break;
    case ResolvedContentKind::OfficeDocument:
        RequestPreview(PreviewJobKind::Office, loadPath, m_office.IsShowing(loadPath));
        break;
    case ResolvedContentKind::ArchiveSubExplorer:
        RequestPreview(PreviewJobKind::Archive, loadPath, m_archive.IsShowing(loadPath));
        break;
    case ResolvedContentKind::PdfDocument:
        if (m_pdf.IsShowing(loadPath) && m_pdf.IsWaitingForPagePicture() && !IsWaitingFor(PreviewJobKind::PdfPage, loadPath)) {
            // A page turned to just before another file was looked at was never drawn: it is drawn now
            RequestPdfPage(m_pdf.CurrentPage());
        } else {
            RequestPreview(PreviewJobKind::Pdf, loadPath, m_pdf.IsShowing(loadPath));
        }
        break;
    case ResolvedContentKind::AiModelInspector:
        RequestPreview(PreviewJobKind::AiModel, loadPath, !m_aiModelPath.empty() && m_aiModelPath == loadPath);
        break;
    case ResolvedContentKind::ImagePreview:
        StopWaitingForPreview();
        if (m_imgMetaPath != loadPath) {
            m_imgMetaPath = loadPath;
            m_imgMeta = InspectImageMetadata(loadPath);
        }
        break;
    default:
        StopWaitingForPreview();
        break;
    }
}

void FileContentPane::SyncToSelection(const FileEntry* entry, bool paneVisible) {
    if (!paneVisible) {
        HideNativeControls();
        return;
    }
    if (!entry) {
        m_hasEntry = false;
        m_currentEntry = {};
        m_inspectingChildOfFolder = false;
        m_resolvedKind = ResolvedContentKind::None;
        m_fetching = false;
        m_fetchFailed = false;
        StopWaitingForPreview();
        HideNativeControls();
        return;
    }

    bool changedPath = (!m_hasEntry || m_rootFolderEntry.fullPath != entry->fullPath);
    m_hasEntry = true;
    if (changedPath) {
        m_currentEntry = *entry;
        m_rootFolderEntry = *entry;
        m_inspectingChildOfFolder = false;
        m_userTabMode = FileContentTabMode::Auto;
        // A file inside an archive (C:\path\test.zip\src\main.cpp) or on an FTP server (ftp://...) is
        // first copied to a local preview file, so that every viewer works on it
        ShowCurrentEntry();
        return;
    }
    ActivateResolvedViewer();
}

void FileContentPane::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    IDWriteTextFormat* fmtCenterSmall,
    ImageCache& imageCache,
    MediaPlayer& mediaPlayer,
    const D2D1_RECT_F& rect,
    bool draggingSplitter,
    Lang lang,
    D2D1_POINT_2F mousePt,
    D2D1_RECT_F& outMediaSeekBarRect)
{
    m_paneRect = rect;
    m_headerHits.clear();
    const bool pl = (lang == Lang::PL);

    if (rect.right <= rect.left + 40.0f || rect.bottom <= rect.top + 40.0f) {
        HideNativeControls();
        return;
    }

    // 1. Left Splitter Bar (5px draggable grip) + Pane Background
    float splitW = 5.0f;
    D2D1_RECT_F splitR = D2D1::RectF(rect.left, rect.top, rect.left + splitW, rect.bottom);
    D2D1_RECT_F innerR = D2D1::RectF(rect.left + splitW, rect.top, rect.right, rect.bottom);

    brush->SetColor(draggingSplitter ? FcpColor(0xCCE8FF) : FcpColor(0xF1F5F9));
    rt->FillRectangle(splitR, brush);
    brush->SetColor(draggingSplitter ? FcpColor(0x0078D7) : FcpColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(splitR.left + 0.5f, splitR.top), D2D1::Point2F(splitR.left + 0.5f, splitR.bottom), brush, 1.0f);
    rt->DrawLine(D2D1::Point2F(splitR.right - 0.5f, splitR.top), D2D1::Point2F(splitR.right - 0.5f, splitR.bottom), brush, 1.0f);
    float gripCy = (splitR.top + splitR.bottom) * 0.5f;
    float gripCx = (splitR.left + splitR.right) * 0.5f;
    brush->SetColor(FcpColor(0x64748B));
    for (int g = -2; g <= 2; ++g) {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(gripCx, gripCy + g * 5.0f), 1.2f, 1.2f), brush);
    }

    brush->SetColor(FcpColor(0xFFFFFF));
    rt->FillRectangle(innerR, brush);

    // 2. Top Header Bar (34px: File title on left + Mode Switcher Tabs [Auto] [Notatnik] [Hex] [3D] [X] on right)
    float hdrH = 34.0f;
    D2D1_RECT_F hdrR = D2D1::RectF(innerR.left, innerR.top, innerR.right, innerR.top + hdrH);
    brush->SetColor(FcpColor(0xEFF6FF));
    rt->FillRectangle(hdrR, brush);
    brush->SetColor(FcpColor(0x0078D7));
    rt->FillRectangle(D2D1::RectF(hdrR.left, hdrR.top, hdrR.right, hdrR.top + 2.5f), brush);
    brush->SetColor(FcpColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(hdrR.left, hdrR.bottom - 0.5f), D2D1::Point2F(hdrR.right, hdrR.bottom - 0.5f), brush, 1.0f);

    float rx = hdrR.right - 5.0f;
    auto drawTabPill = [&](int id, float w, const wchar_t* label, bool active, bool isClose = false) {
        rx -= w;
        D2D1_RECT_F br = D2D1::RectF(rx, hdrR.top + 6.0f, rx + w, hdrR.bottom - 5.0f);
        m_headerHits.push_back({ id, br });
        bool hov = (m_hoverHeaderBtn == id);
        if (isClose) {
            if (hov) {
                brush->SetColor(FcpColor(0xFEE2E2));
                rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
            }
            brush->SetColor(hov ? FcpColor(0xDC2626) : FcpColor(0x475569));
        } else {
            brush->SetColor(active ? FcpColor(0x0078D7) : (hov ? FcpColor(0xDBEAFE) : FcpColor(0xFFFFFF)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
            brush->SetColor(active ? FcpColor(0x005A9E) : FcpColor(0x93C5FD));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush, 1.0f);
            brush->SetColor(active ? FcpColor(0xFFFFFF) : FcpColor(0x1E3A8A));
        }
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), fmtSmall, br, brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        rx -= 3.0f;
    };

    drawTabPill(HBTN_CLOSE_PANE, 22.0f, L"\x2715", false, true);
    if (m_hasEntry && !m_currentEntry.isDirectory && !m_currentEntry.isDrive) {
        drawTabPill(HBTN_CALC_HASH, pl ? 54.0f : 48.0f, pl ? L"# Suma" : L"# Hash", !m_hashFilePath.empty() && m_hashFilePath == m_currentEntry.fullPath);
        bool isAiExt = IsAiModelExtension(m_currentEntry.ext);
        if (isAiExt || m_resolvedKind == ResolvedContentKind::AiModelInspector) {
            drawTabPill(HBTN_TAB_AIMODEL, 64.0f, L"AI Model", m_resolvedKind == ResolvedContentKind::AiModelInspector);
        }
        bool isPdfExt = PdfViewer::IsPdfExtension(m_currentEntry.ext);
        if (isPdfExt || m_resolvedKind == ResolvedContentKind::PdfDocument) {
            drawTabPill(HBTN_TAB_PDF, 42.0f, L"PDF", m_resolvedKind == ResolvedContentKind::PdfDocument);
        }
        bool isArchiveExt = ArchiveExplorer::IsArchiveExtension(m_currentEntry.ext);
        if (isArchiveExt || m_resolvedKind == ResolvedContentKind::ArchiveSubExplorer) {
            drawTabPill(HBTN_TAB_ARCHIVE, pl ? 62.0f : 54.0f, pl ? L"Archiwum" : L"Archive", m_resolvedKind == ResolvedContentKind::ArchiveSubExplorer);
        }
        bool isOfficeExt = SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(m_currentEntry.ext);
        if (isOfficeExt || m_resolvedKind == ResolvedContentKind::OfficeDocument) {
            drawTabPill(HBTN_TAB_OFFICE, pl ? 58.0f : 52.0f, pl ? L"Tabela/Doc" : L"Sheet/Doc", m_resolvedKind == ResolvedContentKind::OfficeDocument);
        }
        bool isEdaExt = EdaViewer::IsEdaExtension(m_currentEntry.ext);
        if (isEdaExt || m_resolvedKind == ResolvedContentKind::EdaViewport) {
            drawTabPill(HBTN_TAB_EDA, 52.0f, L"PCB/EDA", m_resolvedKind == ResolvedContentKind::EdaViewport);
        }
        bool is3DExt = Model3DViewer::Is3DModelExtension(m_currentEntry.ext);
        if (is3DExt || m_resolvedKind == ResolvedContentKind::Model3DViewport) {
            drawTabPill(HBTN_TAB_3D, 40.0f, L"3D", m_resolvedKind == ResolvedContentKind::Model3DViewport);
        }
        drawTabPill(HBTN_TAB_HEX,     44.0f, L"Hex", m_resolvedKind == ResolvedContentKind::HexInspector);
        drawTabPill(HBTN_TAB_NOTEPAD, pl ? 64.0f : 56.0f, pl ? L"Notatnik" : L"Text", m_resolvedKind == ResolvedContentKind::NotepadEditor);
        drawTabPill(HBTN_TAB_AUTO,    44.0f, L"Auto", m_userTabMode == FileContentTabMode::Auto);
        if (m_inspectingChildOfFolder) {
            bool fromArc = ArchiveExplorer::IsArchiveExtension(m_rootFolderEntry.ext);
            drawTabPill(HBTN_DIR_BACK_TO_FOLDER, pl ? 78.0f : 72.0f,
                fromArc ? (pl ? L"\x2190 Archiwum" : L"\x2190 Archive") : (pl ? L"\x2190 Folder" : L"\x2190 Folder"), false);
        }
    } else if (m_hasEntry && (m_currentEntry.isDirectory || m_currentEntry.isDrive)) {
        drawTabPill(HBTN_DIR_ENTER, pl ? 98.0f : 92.0f, pl ? L"Otwórz folder \x2794" : L"Open folder \x2794", false);
        if (m_inspectingChildOfFolder) {
            drawTabPill(HBTN_DIR_BACK_TO_FOLDER, pl ? 68.0f : 62.0f, pl ? L"\x2191 Wstecz" : L"\x2191 Back", false);
        }
    }

    std::wstring hdrTitle;
    if (!m_hasEntry) {
        hdrTitle = pl ? L"Podgląd zawartości pliku" : L"File Content Inspector";
    } else if (m_currentEntry.isDirectory || m_currentEntry.isDrive) {
        hdrTitle = L"\xD83D\xDCC1 " + m_currentEntry.name;
    } else {
        hdrTitle = L"\xD83D\xDCC4 " + m_currentEntry.name + L"  (" + m_currentEntry.sizeStr + L")";
    }
    D2D1_RECT_F titleR = D2D1::RectF(hdrR.left + 8.0f, hdrR.top + 2.0f, std::max(hdrR.left + 20.0f, rx - 4.0f), hdrR.bottom);
    brush->SetColor(FcpColor(0x0F172A));
    rt->DrawTextW(hdrTitle.c_str(), static_cast<UINT32>(hdrTitle.size()), fmtBold, titleR, brush);

    m_bodyRect = D2D1::RectF(innerR.left, hdrR.bottom, innerR.right, innerR.bottom);

    // Optional Checksum Banner (CRC32 + Hash64 + SHA-256) when computed for the current file
    if (m_hasEntry && !m_currentEntry.isDirectory && !m_hashSummary.empty() && m_hashFilePath == m_currentEntry.fullPath) {
        D2D1_RECT_F hashBar = D2D1::RectF(m_bodyRect.left, m_bodyRect.top, m_bodyRect.right, m_bodyRect.top + 24.0f);
        m_bodyRect.top += 24.0f;
        brush->SetColor(FcpColor(0xF0FDF4));
        rt->FillRectangle(hashBar, brush);
        brush->SetColor(FcpColor(0x86EFAC));
        rt->DrawLine(D2D1::Point2F(hashBar.left, hashBar.bottom - 0.5f), D2D1::Point2F(hashBar.right, hashBar.bottom - 0.5f), brush, 1.0f);
        brush->SetColor(FcpColor(0x166534));
        D2D1_RECT_F hTxt = D2D1::RectF(hashBar.left + 8.0f, hashBar.top + 2.0f, hashBar.right - 8.0f, hashBar.bottom - 2.0f);
        rt->DrawTextW(m_hashSummary.c_str(), static_cast<UINT32>(m_hashSummary.size()), fmtSmall, hTxt, brush);
    }

    // 3. Dispatch Body Rendering to the active specialized viewer
    if (m_resolvedKind != ResolvedContentKind::NotepadEditor) {
        m_notepad.HideWindow();
    }

    if (m_fetchFailed) {
        const std::wstring failMsg = FtpClient::IsFtpPath(m_currentEntry.fullPath)
            ? (pl ? L"Nie udało się pobrać tego pliku z serwera FTP." : L"Could not download this file from the FTP server.")
            : (pl ? L"Nie udało się rozpakować tego pliku z archiwum." : L"Could not unpack this file from the archive.");
        D2D1_RECT_F failR = D2D1::RectF(m_bodyRect.left + 20.0f, m_bodyRect.top + 40.0f, m_bodyRect.right - 20.0f, m_bodyRect.bottom - 40.0f);
        brush->SetColor(FcpColor(0x64748B));
        rt->DrawTextW(failMsg.c_str(), static_cast<UINT32>(failMsg.size()), fmtCenterSmall, failR, brush);
        return;
    }
    if (BodyShowsWaitNote()) {
        DrawWaitNote(rt, brush, fmtCenterSmall, pl);
        return;
    }

    switch (m_resolvedKind) {
    case ResolvedContentKind::None: {
        std::wstring emptyMsg = pl
            ? L"Zaznacz dowolny plik lub folder na liście po lewej stronie,\naby zobaczyć jego zawartość (Notatnik, Hex, 3D, podgląd folderu lub multimedia)."
            : L"Select any file or folder in the list on the left\nto view its content (Notepad, Hex, 3D, Folder Sub-Explorer, or Media).";
        D2D1_RECT_F msgR = D2D1::RectF(m_bodyRect.left + 20.0f, m_bodyRect.top + 40.0f, m_bodyRect.right - 20.0f, m_bodyRect.bottom - 40.0f);
        brush->SetColor(FcpColor(0x64748B));
        rt->DrawTextW(emptyMsg.c_str(), static_cast<UINT32>(emptyMsg.size()), fmtCenterSmall, msgR, brush);
        break;
    }

    case ResolvedContentKind::DirectoryInfo: {
        // Folder Summary Banner (30px) + Column Header (22px) + Scrollable Folder Content List
        float bannerH = 30.0f;
        float colHdrH = 22.0f;
        D2D1_RECT_F bannerR = D2D1::RectF(m_bodyRect.left, m_bodyRect.top, m_bodyRect.right, m_bodyRect.top + bannerH);
        brush->SetColor(FcpColor(0xF8FAFC));
        rt->FillRectangle(bannerR, brush);
        brush->SetColor(FcpColor(0xE2E8F0));
        rt->DrawLine(D2D1::Point2F(bannerR.left, bannerR.bottom - 0.5f), D2D1::Point2F(bannerR.right, bannerR.bottom - 0.5f), brush, 1.0f);

        wchar_t sumBuf[256] = {};
        std::wstring bytesStr = PathUtils::FormatByteSize(m_dirTotalFileBytes);
        swprintf_s(sumBuf,
            pl ? L"Zawartość: %zu folderów, %zu plików   \x2022   Rozmiar plików: %s   (2\xD7klik = otwórz)"
               : L"Contents: %zu folders, %zu files   \x2022   Files size: %s   (double-click = open)",
            m_dirFolderCount, m_dirFileCount, bytesStr.c_str());
        brush->SetColor(FcpColor(0x334155));
        rt->DrawTextW(sumBuf, static_cast<UINT32>(wcslen(sumBuf)), fmtSmall,
            D2D1::RectF(bannerR.left + 10.0f, bannerR.top, bannerR.right - 10.0f, bannerR.bottom), brush);

        D2D1_RECT_F colR = D2D1::RectF(m_bodyRect.left, bannerR.bottom, m_bodyRect.right, bannerR.bottom + colHdrH);
        brush->SetColor(FcpColor(0xF1F5F9));
        rt->FillRectangle(colR, brush);
        brush->SetColor(FcpColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(colR.left, colR.bottom - 0.5f), D2D1::Point2F(colR.right, colR.bottom - 0.5f), brush, 1.0f);

        float scrollW = 12.0f;
        float listRight = m_bodyRect.right - scrollW;
        float sizeColW = 78.0f;
        float dateColW = 108.0f;
        float nameColRight = std::max(colR.left + 80.0f, listRight - sizeColW - dateColW);

        brush->SetColor(FcpColor(0x475569));
        rt->DrawTextW(pl ? L"Nazwa" : L"Name", pl ? 5 : 4, fmtBold,
            D2D1::RectF(colR.left + 10.0f, colR.top, nameColRight - 4.0f, colR.bottom), brush);
        if (nameColRight + dateColW <= listRight) {
            rt->DrawTextW(pl ? L"Data modyfikacji" : L"Date modified", pl ? 16 : 13, fmtBold,
                D2D1::RectF(nameColRight + 4.0f, colR.top, nameColRight + dateColW - 4.0f, colR.bottom), brush);
        }
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        rt->DrawTextW(pl ? L"Rozmiar" : L"Size", pl ? 7 : 4, fmtBold,
            D2D1::RectF(listRight - sizeColW, colR.top, listRight - 8.0f, colR.bottom), brush);
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        m_dirListRect = D2D1::RectF(m_bodyRect.left, colR.bottom, listRight, m_bodyRect.bottom);
        float rowH = 22.0f;
        float listH = std::max(1.0f, m_dirListRect.bottom - m_dirListRect.top);
        float totalContentH = static_cast<float>(m_dirPreviewItems.size()) * rowH;
        float maxScroll = std::max(0.0f, totalContentH - listH);
        m_dirScrollY = std::clamp(m_dirScrollY, 0.0f, maxScroll);

        if (m_dirPreviewItems.empty()) {
            const wchar_t* emptyDirLbl = pl ? L"(Ten folder jest pusty)" : L"(This folder is empty)";
            brush->SetColor(FcpColor(0x94A3B8));
            rt->DrawTextW(emptyDirLbl, static_cast<UINT32>(wcslen(emptyDirLbl)), fmtCenterSmall, m_dirListRect, brush);
        } else {
            rt->PushAxisAlignedClip(m_dirListRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            int firstIdx = std::max(0, static_cast<int>(m_dirScrollY / rowH));
            int lastIdx = std::min(static_cast<int>(m_dirPreviewItems.size()) - 1,
                                   static_cast<int>((m_dirScrollY + listH) / rowH) + 1);
            for (int i = firstIdx; i <= lastIdx; ++i) {
                const auto& item = m_dirPreviewItems[i];
                float ry = m_dirListRect.top + i * rowH - m_dirScrollY;
                D2D1_RECT_F rowR = D2D1::RectF(m_dirListRect.left, ry, m_dirListRect.right, ry + rowH);

                if (i == m_dirSelectedIdx) {
                    brush->SetColor(FcpColor(0xCCE8FF));
                    rt->FillRectangle(rowR, brush);
                    brush->SetColor(FcpColor(0x0078D7));
                    rt->DrawRectangle(D2D1::RectF(rowR.left + 0.5f, rowR.top + 0.5f, rowR.right - 0.5f, rowR.bottom - 0.5f), brush, 1.0f);
                } else if (i == m_dirHoverIdx) {
                    brush->SetColor(FcpColor(0xE5F3FF));
                    rt->FillRectangle(rowR, brush);
                } else if (i % 2 == 1) {
                    brush->SetColor(FcpColor(0xF8FAFC));
                    rt->FillRectangle(rowR, brush);
                }

                // Draw small folder or file badge
                D2D1_RECT_F icoR = D2D1::RectF(rowR.left + 8.0f, rowR.top + 4.0f, rowR.left + 21.0f, rowR.bottom - 4.0f);
                if (item.isDirectory) {
                    brush->SetColor(FcpColor(0xF59E0B));
                    rt->FillRoundedRectangle(D2D1::RoundedRect(icoR, 2.0f, 2.0f), brush);
                } else {
                    brush->SetColor(item.isImage ? FcpColor(0x10B981) : FcpColor(0x3B82F6));
                    rt->DrawRoundedRectangle(D2D1::RoundedRect(icoR, 1.5f, 1.5f), brush, 1.2f);
                }

                brush->SetColor(FcpColor(0x0F172A));
                rt->DrawTextW(item.name.c_str(), static_cast<UINT32>(item.name.size()), fmtRegular,
                    D2D1::RectF(rowR.left + 26.0f, rowR.top, nameColRight - 4.0f, rowR.bottom), brush);

                if (nameColRight + dateColW <= listRight) {
                    brush->SetColor(FcpColor(0x64748B));
                    rt->DrawTextW(item.dateStr.c_str(), static_cast<UINT32>(item.dateStr.size()), fmtSmall,
                        D2D1::RectF(nameColRight + 4.0f, rowR.top, nameColRight + dateColW - 4.0f, rowR.bottom), brush);
                }

                if (!item.isDirectory && !item.sizeStr.empty()) {
                    brush->SetColor(FcpColor(0x475569));
                    fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    rt->DrawTextW(item.sizeStr.c_str(), static_cast<UINT32>(item.sizeStr.size()), fmtSmall,
                        D2D1::RectF(listRight - sizeColW, rowR.top, listRight - 8.0f, rowR.bottom), brush);
                    fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
            }
            rt->PopAxisAlignedClip();
        }

        // Scrollbar for Folder Content View
        m_dirScrollTrackRect = D2D1::RectF(listRight, colR.bottom, m_bodyRect.right, m_bodyRect.bottom);
        brush->SetColor(FcpColor(0xF1F5F9));
        rt->FillRectangle(m_dirScrollTrackRect, brush);
        if (totalContentH > listH) {
            float thumbH = std::max(24.0f, listH * (listH / totalContentH));
            float frac = (maxScroll > 0.0f) ? (m_dirScrollY / maxScroll) : 0.0f;
            float thumbTop = m_dirScrollTrackRect.top + frac * (listH - thumbH);
            m_dirScrollThumbRect = D2D1::RectF(m_dirScrollTrackRect.left + 2.0f, thumbTop, m_dirScrollTrackRect.right - 2.0f, thumbTop + thumbH);
            brush->SetColor(m_dirDraggingScroll ? FcpColor(0x0078D7) : FcpColor(0x94A3B8));
            rt->FillRoundedRectangle(D2D1::RoundedRect(m_dirScrollThumbRect, 3.0f, 3.0f), brush);
        } else {
            m_dirScrollThumbRect = {};
        }
        break;
    }

    case ResolvedContentKind::NotepadEditor:
        m_notepad.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::HexInspector:
        m_hex.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::Model3DViewport:
        m_model3D.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::EdaViewport:
        m_eda.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::OfficeDocument:
        m_office.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::ArchiveSubExplorer:
        m_archive.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::PdfDocument:
        m_pdf.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::AiModelInspector: {
        D2D1_RECT_F cardR = D2D1::RectF(m_bodyRect.left + 12.0f, m_bodyRect.top + 12.0f, m_bodyRect.right - 12.0f, m_bodyRect.bottom - 12.0f);
        brush->SetColor(FcpColor(0xF8FAFC));
        rt->FillRoundedRectangle(D2D1::RoundedRect(cardR, 6.0f, 6.0f), brush);
        brush->SetColor(FcpColor(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(cardR, 6.0f, 6.0f), brush, 1.0f);

        if (!m_aiModelInfo.valid) {
            std::wstring errMsg = pl
                ? L"Nie udało się odczytać nagłówka modelu AI (.safetensors / .gguf / .onnx)."
                : L"Could not parse AI model header (.safetensors / .gguf / .onnx).";
            brush->SetColor(FcpColor(0x64748B));
            rt->DrawTextW(errMsg.c_str(), static_cast<UINT32>(errMsg.size()), fmtCenterSmall, cardR, brush);
            break;
        }

        rt->PushAxisAlignedClip(cardR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float y = cardR.top + 10.0f - m_aiModelScrollY;
        float lx = cardR.left + 14.0f;
        float rxCard = cardR.right - 14.0f;

        // Top badge + Copy Metadata button
        std::wstring title = L"\xD83E\xDDE0 " + m_aiModelInfo.formatName +
            (m_aiModelInfo.modelTitle.empty() ? L"" : (L"  \x2014  " + m_aiModelInfo.modelTitle));
        brush->SetColor(FcpColor(0x0F172A));
        rt->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), fmtBold, D2D1::RectF(lx, y, rxCard - 130.0f, y + 24.0f), brush);

        D2D1_RECT_F copyMetaBtn = D2D1::RectF(rxCard - 124.0f, y, rxCard, y + 24.0f);
        m_headerHits.push_back({ HBTN_COPY_MODEL_META, copyMetaBtn });
        bool hovCopyMeta = (m_hoverHeaderBtn == HBTN_COPY_MODEL_META);
        brush->SetColor(hovCopyMeta ? FcpColor(0xDBEAFE) : FcpColor(0xEFF6FF));
        rt->FillRoundedRectangle(D2D1::RoundedRect(copyMetaBtn, 3.5f, 3.5f), brush);
        brush->SetColor(FcpColor(0x3B82F6));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(copyMetaBtn, 3.5f, 3.5f), brush, 1.0f);
        brush->SetColor(FcpColor(0x1D4ED8));
        const wchar_t* cpLbl = pl ? L"\xD83D\xDCCB Kopiuj info" : L"\xD83D\xDCCB Copy Meta";
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(cpLbl, static_cast<UINT32>(wcslen(cpLbl)), fmtSmall, copyMetaBtn, brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        y += 30.0f;

        // Summary metrics strip
        std::wstring metrics = (pl ? L"Tensory: " : L"Tensors: ") + std::to_wstring(m_aiModelInfo.tensorCount);
        if (m_aiModelInfo.totalParameters > 0) {
            metrics += (pl ? L"   \x2022   Parametry: " : L"   \x2022   Total Params: ") +
                       FormatModelParamCount(m_aiModelInfo.totalParameters);
        }
        if (!m_aiModelInfo.arch.empty()) metrics += (pl ? L"   \x2022   Architektura: " : L"   \x2022   Arch: ") + m_aiModelInfo.arch;
        if (!m_aiModelInfo.baseModel.empty()) metrics += (pl ? L"   \x2022   Baza: " : L"   \x2022   Base: ") + m_aiModelInfo.baseModel;
        if (!m_aiModelInfo.contextLength.empty()) metrics += (pl ? L"   \x2022   Kontekst: " : L"   \x2022   Ctx: ") + m_aiModelInfo.contextLength;
        if (!m_aiModelInfo.precisionSummary.empty()) metrics += (pl ? L"   \x2022   Precyzja: " : L"   \x2022   Precision: ") + m_aiModelInfo.precisionSummary;
        if (!m_aiModelInfo.networkDimAlpha.empty()) metrics += L"   \x2022   " + m_aiModelInfo.networkDimAlpha;

        D2D1_RECT_F sumBox = D2D1::RectF(lx, y, rxCard, y + 44.0f);
        brush->SetColor(FcpColor(0xEFF6FF));
        rt->FillRoundedRectangle(D2D1::RoundedRect(sumBox, 4.0f, 4.0f), brush);
        brush->SetColor(FcpColor(0xBFDBFE));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(sumBox, 4.0f, 4.0f), brush, 1.0f);
        brush->SetColor(FcpColor(0x1E3A8A));
        rt->DrawTextW(metrics.c_str(), static_cast<UINT32>(metrics.size()), fmtSmall, D2D1::RectF(sumBox.left + 8.0f, sumBox.top + 4.0f, sumBox.right - 8.0f, sumBox.bottom - 4.0f), brush);
        y += 52.0f;

        // Key-Value Metadata rows
        for (size_t i = 0; i < m_aiModelInfo.kvRows.size(); ++i) {
            if (y + 22.0f >= cardR.top && y <= cardR.bottom) {
                D2D1_RECT_F rowR = D2D1::RectF(lx, y, rxCard, y + 22.0f);
                if (i % 2 == 0) {
                    brush->SetColor(FcpColor(0xFFFFFF));
                    rt->FillRectangle(rowR, brush);
                }
                float keyW = std::min(190.0f, (rxCard - lx) * 0.42f);
                brush->SetColor(FcpColor(0x475569));
                rt->DrawTextW(m_aiModelInfo.kvRows[i].first.c_str(), static_cast<UINT32>(m_aiModelInfo.kvRows[i].first.size()), fmtBold,
                    D2D1::RectF(lx + 6.0f, y + 2.0f, lx + keyW, y + 20.0f), brush);
                brush->SetColor(FcpColor(0x0F172A));
                rt->DrawTextW(m_aiModelInfo.kvRows[i].second.c_str(), static_cast<UINT32>(m_aiModelInfo.kvRows[i].second.size()), fmtSmall,
                    D2D1::RectF(lx + keyW + 8.0f, y + 2.0f, rxCard - 6.0f, y + 20.0f), brush);
            }
            y += 22.0f;
        }

        // Tensor Table (Name, DType, Shape, Params / Size)
        if (!m_aiModelInfo.tensors.empty()) {
            y += 10.0f;
            std::wstring tblTitle = (pl ? L"Tabela tensorów (" : L"Tensor Table (") +
                std::to_wstring(m_aiModelInfo.tensors.size()) +
                (m_aiModelInfo.tensorCount > m_aiModelInfo.tensors.size()
                    ? (L" / " + std::to_wstring(m_aiModelInfo.tensorCount))
                    : L"") + L")";
            if (y + 24.0f >= cardR.top && y <= cardR.bottom) {
                brush->SetColor(FcpColor(0x1E293B));
                rt->DrawTextW(tblTitle.c_str(), static_cast<UINT32>(tblTitle.size()), fmtBold,
                    D2D1::RectF(lx, y, rxCard, y + 20.0f), brush);
            }
            y += 22.0f;

            const float totalW = std::max(160.0f, rxCard - lx);
            const float cNameW  = totalW * 0.40f;
            const float cTypeW  = totalW * 0.14f;
            const float cShapeW = totalW * 0.23f;
            const float xName  = lx;
            const float xType  = xName + cNameW;
            const float xShape = xType + cTypeW;
            const float xSize  = xShape + cShapeW;

            if (y + 24.0f >= cardR.top && y <= cardR.bottom) {
                D2D1_RECT_F hdrR = D2D1::RectF(lx, y, rxCard, y + 22.0f);
                brush->SetColor(FcpColor(0xE2E8F0));
                rt->FillRoundedRectangle(D2D1::RoundedRect(hdrR, 3.0f, 3.0f), brush);
                brush->SetColor(FcpColor(0x334155));
                const wchar_t* hName  = pl ? L"Nazwa" : L"Name";
                const wchar_t* hDType = L"DType";
                const wchar_t* hShape = pl ? L"Kształt" : L"Shape";
                const wchar_t* hSize  = pl ? L"Parametry / Rozmiar" : L"Params / Size";
                rt->DrawTextW(hName,  static_cast<UINT32>(wcslen(hName)),  fmtBold, D2D1::RectF(xName + 6.0f,  y + 2.0f, xType - 4.0f,  y + 20.0f), brush);
                rt->DrawTextW(hDType, static_cast<UINT32>(wcslen(hDType)), fmtBold, D2D1::RectF(xType + 4.0f,  y + 2.0f, xShape - 4.0f, y + 20.0f), brush);
                rt->DrawTextW(hShape, static_cast<UINT32>(wcslen(hShape)), fmtBold, D2D1::RectF(xShape + 4.0f, y + 2.0f, xSize - 4.0f,  y + 20.0f), brush);
                rt->DrawTextW(hSize,  static_cast<UINT32>(wcslen(hSize)),  fmtBold, D2D1::RectF(xSize + 4.0f,  y + 2.0f, rxCard - 6.0f, y + 20.0f), brush);
            }
            y += 23.0f;

            for (size_t i = 0; i < m_aiModelInfo.tensors.size(); ++i) {
                if (y + 21.0f >= cardR.top && y <= cardR.bottom) {
                    const auto& te = m_aiModelInfo.tensors[i];
                    D2D1_RECT_F trR = D2D1::RectF(lx, y, rxCard, y + 20.0f);
                    if (i % 2 == 0) {
                        brush->SetColor(FcpColor(0xFFFFFF));
                        rt->FillRectangle(trR, brush);
                    }
                    std::wstring psCell;
                    if (te.paramCount > 0 || te.byteSize > 0) {
                        psCell = std::to_wstring(te.paramCount);
                        if (te.byteSize > 0) {
                            psCell += L" (" + PathUtils::FormatByteSize(te.byteSize) + L")";
                        }
                    } else {
                        psCell = L"dynamic";
                    }
                    brush->SetColor(FcpColor(0x0F172A));
                    rt->DrawTextW(te.name.c_str(),  static_cast<UINT32>(te.name.size()),  fmtSmall, D2D1::RectF(xName + 6.0f,  y + 1.0f, xType - 4.0f,  y + 19.0f), brush);
                    brush->SetColor(FcpColor(0x1D4ED8));
                    rt->DrawTextW(te.dtype.c_str(), static_cast<UINT32>(te.dtype.size()), fmtSmall, D2D1::RectF(xType + 4.0f,  y + 1.0f, xShape - 4.0f, y + 19.0f), brush);
                    brush->SetColor(FcpColor(0x334155));
                    rt->DrawTextW(te.shape.c_str(), static_cast<UINT32>(te.shape.size()), fmtSmall, D2D1::RectF(xShape + 4.0f, y + 1.0f, xSize - 4.0f,  y + 19.0f), brush);
                    brush->SetColor(FcpColor(0x475569));
                    rt->DrawTextW(psCell.c_str(),   static_cast<UINT32>(psCell.size()),   fmtSmall, D2D1::RectF(xSize + 4.0f,  y + 1.0f, rxCard - 6.0f, y + 19.0f), brush);
                }
                y += 20.0f;
            }
        }
        rt->PopAxisAlignedClip();
        break;
    }

    case ResolvedContentKind::ImagePreview: {
        if (m_imgMetaPath != m_currentEntry.fullPath) {
            m_imgMetaPath = m_currentEntry.fullPath;
            m_imgMeta = InspectImageMetadata(m_currentEntry.fullPath);
        }
        bool hasRichMeta = (m_imgMeta.hasAiMeta || m_imgMeta.hasExif);
        float botPanelH = m_imgMeta.hasAiMeta ? 132.0f : (m_imgMeta.hasExif ? 68.0f : 42.0f);
        if (m_bodyRect.bottom - m_bodyRect.top < 240.0f) botPanelH = 42.0f;

        D2D1_RECT_F imgArea = D2D1::RectF(m_bodyRect.left + 12.0f, m_bodyRect.top + 12.0f, m_bodyRect.right - 12.0f, std::max(m_bodyRect.top + 40.0f, m_bodyRect.bottom - botPanelH - 8.0f));
        FullImageMeta meta = imageCache.GetOrQueueFullImage(rt, m_currentEntry.fullPath, L"", L"");
        ID2D1Bitmap* bmp = meta.bitmap ? meta.bitmap : imageCache.GetOrQueueThumbnail(rt, m_currentEntry.fullPath);
        if (bmp) {
            D2D1_SIZE_F sz = bmp->GetSize();
            D2D1_RECT_F dr = PathUtils::FitRectCentered(imgArea, sz.width, sz.height);
            rt->DrawBitmap(bmp, dr);
        }

        D2D1_RECT_F botBar = D2D1::RectF(m_bodyRect.left, m_bodyRect.bottom - botPanelH, m_bodyRect.right, m_bodyRect.bottom);
        brush->SetColor(FcpColor(0xF8FAFC));
        rt->FillRectangle(botBar, brush);
        brush->SetColor(FcpColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(botBar.left, botBar.top), D2D1::Point2F(botBar.right, botBar.top), brush, 1.0f);

        D2D1_RECT_F toolBtn = D2D1::RectF(botBar.left + 10.0f, botBar.top + 6.0f, std::min(botBar.right - 10.0f, botBar.left + 168.0f), botBar.top + 32.0f);
        m_headerHits.push_back({ HBTN_IMG_TOOLS, toolBtn });
        bool hovTool = (m_hoverHeaderBtn == HBTN_IMG_TOOLS);
        brush->SetColor(hovTool ? FcpColor(0xDBEAFE) : FcpColor(0xEFF6FF));
        rt->FillRoundedRectangle(D2D1::RoundedRect(toolBtn, 3.5f, 3.5f), brush);
        brush->SetColor(hovTool ? FcpColor(0x0067B8) : FcpColor(0x93C5FD));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(toolBtn, 3.5f, 3.5f), brush, 1.0f);
        brush->SetColor(FcpColor(0x1D4ED8));
        const wchar_t* toolLbl = pl ? L"\xD83D\xDCD0 Narzędzia obrazu \x25BE" : L"\xD83D\xDCD0 Image Tools \x25BE";
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(toolLbl, static_cast<UINT32>(wcslen(toolLbl)), fmtSmall, toolBtn, brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        float nextBtnX = toolBtn.right + 6.0f;
        if (m_imgMeta.hasAiMeta) {
            auto drawAiCopyBtn = [&](int id, float w, const wchar_t* lbl) {
                if (nextBtnX + w > botBar.right - 8.0f) return;
                D2D1_RECT_F br = D2D1::RectF(nextBtnX, botBar.top + 6.0f, nextBtnX + w, botBar.top + 32.0f);
                m_headerHits.push_back({ id, br });
                bool hov = (m_hoverHeaderBtn == id);
                brush->SetColor(hov ? FcpColor(0xDCFCE7) : FcpColor(0xF0FDF4));
                rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.5f, 3.5f), brush);
                brush->SetColor(hov ? FcpColor(0x16A34A) : FcpColor(0x86EFAC));
                rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.5f, 3.5f), brush, 1.0f);
                brush->SetColor(FcpColor(0x166534));
                fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(lbl, static_cast<UINT32>(wcslen(lbl)), fmtSmall, br, brush);
                fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                nextBtnX += w + 5.0f;
            };
            if (!m_imgMeta.positivePrompt.empty()) {
                drawAiCopyBtn(HBTN_COPY_PROMPT, pl ? 104.0f : 98.0f, pl ? L"\xD83D\xDCCB Kopiuj Prompt" : L"\xD83D\xDCCB Copy Prompt");
            }
            if (!m_imgMeta.negativePrompt.empty()) {
                drawAiCopyBtn(HBTN_COPY_NEG_PROMPT, pl ? 92.0f : 86.0f, pl ? L"\xD83D\xDCCB Negatywny" : L"\xD83D\xDCCB Copy Neg");
            }
            if (!m_imgMeta.seed.empty()) {
                drawAiCopyBtn(HBTN_COPY_SEED, 78.0f, L"\xD83C\xDFB2 Seed");
            }
        }

        if (meta.origWidth > 0 && meta.origHeight > 0) {
            wchar_t dimBuf[96] = {};
            swprintf_s(dimBuf, L"%u \xD7 %u px  \x2022  %s", meta.origWidth, meta.origHeight, m_currentEntry.sizeStr.c_str());
            D2D1_RECT_F dimR = D2D1::RectF(nextBtnX + 4.0f, toolBtn.top, botBar.right - 10.0f, toolBtn.bottom);
            if (dimR.right > dimR.left + 40.0f) {
                brush->SetColor(FcpColor(0x475569));
                rt->DrawTextW(dimBuf, static_cast<UINT32>(wcslen(dimBuf)), fmtSmall, dimR, brush);
            }
        }

        if (botPanelH > 50.0f && hasRichMeta) {
            float metaTop = botBar.top + 36.0f;
            if (m_imgMeta.hasAiMeta) {
                std::wstring paramLine = L"\x2728 " + m_imgMeta.generator;
                if (!m_imgMeta.modelName.empty()) paramLine += L"  \x2022  Model: " + m_imgMeta.modelName;
                if (!m_imgMeta.seed.empty()) paramLine += L"  \x2022  Seed: " + m_imgMeta.seed;
                if (!m_imgMeta.steps.empty()) paramLine += L"  \x2022  Steps: " + m_imgMeta.steps;
                if (!m_imgMeta.cfgScale.empty()) paramLine += L"  \x2022  CFG: " + m_imgMeta.cfgScale;
                if (!m_imgMeta.sampler.empty()) paramLine += L"  \x2022  " + m_imgMeta.sampler;

                brush->SetColor(FcpColor(0x0369A1));
                rt->DrawTextW(paramLine.c_str(), static_cast<UINT32>(paramLine.size()), fmtBold,
                    D2D1::RectF(botBar.left + 10.0f, metaTop, botBar.right - 10.0f, metaTop + 19.0f), brush);

                std::wstring promptDisp = (pl ? L"Prompt: " : L"Prompt: ") + m_imgMeta.positivePrompt;
                if (!m_imgMeta.negativePrompt.empty()) {
                    promptDisp += (pl ? L"\nNegatywny: " : L"\nNegative: ") + m_imgMeta.negativePrompt;
                }
                brush->SetColor(FcpColor(0x1E293B));
                rt->DrawTextW(promptDisp.c_str(), static_cast<UINT32>(promptDisp.size()), fmtSmall,
                    D2D1::RectF(botBar.left + 10.0f, metaTop + 20.0f, botBar.right - 10.0f, botBar.bottom - 4.0f), brush);
            } else if (m_imgMeta.hasExif) {
                std::wstring exifLine = L"\xD83D\xDCF7 EXIF:  " + m_imgMeta.exifSummary;
                brush->SetColor(FcpColor(0x1E293B));
                rt->DrawTextW(exifLine.c_str(), static_cast<UINT32>(exifLine.size()), fmtSmall,
                    D2D1::RectF(botBar.left + 10.0f, metaTop, botBar.right - 10.0f, botBar.bottom - 4.0f), brush);
            }
        }
        break;
    }

    case ResolvedContentKind::MediaPreview: {
        D2D1_RECT_F imgArea = D2D1::RectF(m_bodyRect.left + 12.0f, m_bodyRect.top + 12.0f, m_bodyRect.right - 12.0f, std::max(m_bodyRect.top + 40.0f, m_bodyRect.bottom - 56.0f));
        if (mediaPlayer.HasMedia()) {
            if (m_currentEntry.isVideo && mediaPlayer.HasVideo()) {
                uint32_t vw = 0, vh = 0;
                ID2D1Bitmap* vbmp = mediaPlayer.GetCurrentVideoFrame(rt, vw, vh);
                if (!vbmp) vbmp = imageCache.GetOrQueueThumbnail(rt, m_currentEntry.fullPath);
                if (vbmp) {
                    D2D1_SIZE_F sz = vbmp->GetSize();
                    D2D1_RECT_F dr = PathUtils::FitRectCentered(imgArea, sz.width, sz.height);
                    rt->DrawBitmap(vbmp, dr);
                }
            } else {
                brush->SetColor(FcpColor(0x1E293B));
                rt->FillRoundedRectangle(D2D1::RoundedRect(imgArea, 6.0f, 6.0f), brush);
                brush->SetColor(FcpColor(0xF8FAFC));
                std::wstring noteLbl = L"\x266B  " + m_currentEntry.name;
                rt->DrawTextW(noteLbl.c_str(), static_cast<UINT32>(noteLbl.size()), fmtCenterSmall, imgArea, brush);
            }

            D2D1_RECT_F playBtn = D2D1::RectF(m_bodyRect.left + 14.0f, m_bodyRect.bottom - 42.0f, m_bodyRect.left + 44.0f, m_bodyRect.bottom - 16.0f);
            m_headerHits.push_back({ HBTN_MEDIA_PLAY_PAUSE, playBtn });
            brush->SetColor(FcpColor(0x0078D7));
            rt->FillRoundedRectangle(D2D1::RoundedRect(playBtn, 3.0f, 3.0f), brush);
            brush->SetColor(FcpColor(0xFFFFFF));
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt->DrawTextW(mediaPlayer.IsPlaying() ? L"\x23F8" : L"\x25B6", 1, fmtSmall, playBtn, brush);
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

            double pos = mediaPlayer.GetCurrentTimeSec();
            double dur = mediaPlayer.GetDurationSec();
            float frac = (dur > 0.05) ? static_cast<float>(std::clamp(pos / dur, 0.0, 1.0)) : 0.0f;
            outMediaSeekBarRect = D2D1::RectF(playBtn.right + 10.0f, playBtn.top + 4.0f, m_bodyRect.right - 14.0f, playBtn.bottom - 4.0f);
            float ty = (outMediaSeekBarRect.top + outMediaSeekBarRect.bottom) * 0.5f;
            brush->SetColor(FcpColor(0xCBD5E1));
            rt->FillRectangle(D2D1::RectF(outMediaSeekBarRect.left, ty - 2.0f, outMediaSeekBarRect.right, ty + 2.0f), brush);
            float fx = outMediaSeekBarRect.left + frac * std::max(0.0f, outMediaSeekBarRect.right - outMediaSeekBarRect.left);
            brush->SetColor(FcpColor(0x0078D7));
            rt->FillRectangle(D2D1::RectF(outMediaSeekBarRect.left, ty - 2.0f, fx, ty + 2.0f), brush);
        }
        break;
    }
    }
}

bool FileContentPane::OnMouseDown(
    float x,
    float y,
    bool rightButton,
    Lang lang,
    bool* outCloseRequested,
    int* outTriggerCmdId,
    std::wstring* outStatusToast)
{
    if (outCloseRequested) *outCloseRequested = false;
    if (outTriggerCmdId) *outTriggerCmdId = 0;

    if (!PathUtils::PtInRectF(x, y, m_paneRect)) {
        return false;
    }

    if (!rightButton) {
        for (const auto& hit : m_headerHits) {
            if (PathUtils::PtInRectF(x, y, hit.rect)) {
                switch (hit.id) {
                case HBTN_CLOSE_PANE:
                    if (outCloseRequested) *outCloseRequested = true;
                    return true;
                case HBTN_TAB_AUTO:
                    m_userTabMode = FileContentTabMode::Auto;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_NOTEPAD:
                    m_userTabMode = FileContentTabMode::Notepad;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_HEX:
                    m_userTabMode = FileContentTabMode::Hex;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_3D:
                    m_userTabMode = FileContentTabMode::Model3D;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_EDA:
                    m_userTabMode = FileContentTabMode::EdaPcb;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_OFFICE:
                    m_userTabMode = FileContentTabMode::Office;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_ARCHIVE:
                    m_userTabMode = FileContentTabMode::Archive;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_PDF:
                    m_userTabMode = FileContentTabMode::Pdf;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_AIMODEL:
                    m_userTabMode = FileContentTabMode::AiModel;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_CALC_HASH:
                    if (!m_currentEntry.fullPath.empty()) {
                        if (ComputeFileHashes(m_currentEntry.fullPath, m_hashSummary, m_sha256Hex)) {
                            m_hashFilePath = m_currentEntry.fullPath;
                            CopyUnicodeToClipboard(m_parentHwnd, m_sha256Hex.empty() ? m_hashSummary : m_sha256Hex);
                            if (outStatusToast) {
                                *outStatusToast = (lang == Lang::PL ? L"\x2713 Obliczono i skopiowano SHA-256 / CRC32: " : L"\x2713 Calculated & copied SHA-256 / CRC32: ") +
                                    (m_sha256Hex.empty() ? m_hashSummary : m_sha256Hex.substr(0, 24) + L"...");
                            }
                        }
                    }
                    return true;
                case HBTN_COPY_PROMPT:
                    CopyUnicodeToClipboard(m_parentHwnd, m_imgMeta.positivePrompt);
                    if (outStatusToast) *outStatusToast = (lang == Lang::PL ? L"\x2713 Skopiowano pozytywny prompt AI do schowka" : L"\x2713 Copied AI positive prompt to clipboard");
                    return true;
                case HBTN_COPY_NEG_PROMPT:
                    CopyUnicodeToClipboard(m_parentHwnd, m_imgMeta.negativePrompt);
                    if (outStatusToast) *outStatusToast = (lang == Lang::PL ? L"\x2713 Skopiowano negatywny prompt AI do schowka" : L"\x2713 Copied AI negative prompt to clipboard");
                    return true;
                case HBTN_COPY_SEED:
                    CopyUnicodeToClipboard(m_parentHwnd, m_imgMeta.seed);
                    if (outStatusToast) *outStatusToast = (lang == Lang::PL ? L"\x2713 Skopiowano Seed: " : L"\x2713 Copied Seed: ") + m_imgMeta.seed;
                    return true;
                case HBTN_COPY_MODEL_META: {
                    std::wstring dump = m_aiModelInfo.formatName + L" - " + m_currentEntry.name + L"\r\n";
                    dump += L"Tensors: " + std::to_wstring(m_aiModelInfo.tensorCount) + L"\r\n";
                    if (m_aiModelInfo.totalParameters > 0) {
                        dump += L"Total Parameters: " + FormatModelParamCount(m_aiModelInfo.totalParameters) + L"\r\n";
                    }
                    if (!m_aiModelInfo.precisionSummary.empty()) dump += L"Precision: " + m_aiModelInfo.precisionSummary + L"\r\n";
                    for (const auto& kv : m_aiModelInfo.kvRows) {
                        dump += kv.first + L": " + kv.second + L"\r\n";
                    }
                    if (!m_aiModelInfo.tensors.empty()) {
                        dump += L"\r\n[Tensors]\r\n";
                        const size_t lim = std::min<size_t>(m_aiModelInfo.tensors.size(), 128);
                        for (size_t i = 0; i < lim; ++i) {
                            const auto& te = m_aiModelInfo.tensors[i];
                            dump += te.name + L"\t" + te.dtype + L"\t" + te.shape + L"\t" +
                                    std::to_wstring(te.paramCount) + L" (" + PathUtils::FormatByteSize(te.byteSize) + L")\r\n";
                        }
                    }
                    CopyUnicodeToClipboard(m_parentHwnd, dump);
                    if (outStatusToast) *outStatusToast = (lang == Lang::PL ? L"\x2713 Skopiowano metadane modelu AI do schowka" : L"\x2713 Copied AI model metadata to clipboard");
                    return true;
                }
                case HBTN_IMG_TOOLS:
                    if (outTriggerCmdId) *outTriggerCmdId = CMD_IMG_TOOL_MENU;
                    return true;
                case HBTN_MEDIA_PLAY_PAUSE:
                    if (outTriggerCmdId) *outTriggerCmdId = CMD_MEDIA_PLAY_PAUSE;
                    return true;
                case HBTN_DIR_ENTER:
                    if (!m_currentEntry.fullPath.empty()) {
                        m_pendingNavigatePath = m_currentEntry.fullPath;
                    }
                    return true;
                case HBTN_DIR_BACK_TO_FOLDER:
                    m_currentEntry = m_rootFolderEntry;
                    m_inspectingChildOfFolder = false;
                    m_userTabMode = FileContentTabMode::Auto;
                    ShowCurrentEntry();
                    return true;
                }
            }
        }
    }

    // While the preview is being read (or could not be fetched) there is no viewer to click in
    if (BodyShowsWaitNote() || m_fetchFailed) {
        return true;
    }

    switch (m_resolvedKind) {
    case ResolvedContentKind::DirectoryInfo: {
        if (!rightButton && PathUtils::PtInRectF(x, y, m_dirScrollTrackRect)) {
            if (PathUtils::PtInRectF(x, y, m_dirScrollThumbRect)) {
                m_dirDraggingScroll = true;
                m_dirScrollDragOffset = y - m_dirScrollThumbRect.top;
            } else {
                float rowH = 22.0f;
                float listH = std::max(1.0f, m_dirListRect.bottom - m_dirListRect.top);
                float maxScroll = std::max(0.0f, static_cast<float>(m_dirPreviewItems.size()) * rowH - listH);
                float frac = std::clamp((y - m_dirScrollTrackRect.top) / listH, 0.0f, 1.0f);
                m_dirScrollY = frac * maxScroll;
            }
            return true;
        }
        if (!rightButton && PathUtils::PtInRectF(x, y, m_dirListRect)) {
            float rowH = 22.0f;
            int idx = static_cast<int>((y - m_dirListRect.top + m_dirScrollY) / rowH);
            if (idx >= 0 && idx < static_cast<int>(m_dirPreviewItems.size())) {
                ULONGLONG now = GetTickCount64();
                bool isDbl = (idx == m_dirLastClickIdx && (now - m_dirLastClickTick) <= GetDoubleClickTime());
                m_dirSelectedIdx = idx;
                m_dirLastClickIdx = idx;
                m_dirLastClickTick = now;

                if (isDbl) {
                    m_dirLastClickIdx = -1;
                    const auto& item = m_dirPreviewItems[idx];
                    if (item.isDirectory) {
                        // Enter subfolder inside the right preview pane (and user can also click [Otwórz folder ->] to navigate main explorer)
                        m_currentEntry.name = item.name;
                        m_currentEntry.fullPath = item.fullPath;
                        m_currentEntry.isDirectory = true;
                        m_inspectingChildOfFolder = true;
                        ShowCurrentEntry();
                    } else {
                        // Preview the file right inside the File Content Pane (with [<- Folder] button to return!)
                        FileEntry childEntry = {};
                        childEntry.name = item.name;
                        childEntry.displayName = item.name;
                        childEntry.fullPath = item.fullPath;     // inside an archive: unpacked by ShowCurrentEntry
                        childEntry.ext = item.ext;
                        childEntry.size = item.size;
                        childEntry.sizeStr = item.sizeStr;
                        childEntry.dateStr = item.dateStr;
                        childEntry.isDirectory = false;
                        childEntry.isImage = item.isImage;
                        childEntry.isVideo = IsVideoExtension(item.ext);
                        childEntry.isAudio = IsAudioExtension(item.ext);
                        m_currentEntry = std::move(childEntry);
                        m_inspectingChildOfFolder = true;
                        m_userTabMode = FileContentTabMode::Auto;
                        ShowCurrentEntry();
                    }
                }
            }
            return true;
        }
        break;
    }
    case ResolvedContentKind::NotepadEditor:
        if (!rightButton && m_notepad.OnMouseDown(x, y, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::HexInspector:
        if (!rightButton && m_hex.OnMouseDown(x, y, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::Model3DViewport:
        if (m_model3D.OnMouseDown(x, y, rightButton, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::EdaViewport:
        if (m_eda.OnMouseDown(x, y, rightButton, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::OfficeDocument:
        if (m_office.OnMouseDown(x, y, rightButton, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::PdfDocument:
        if (m_pdf.OnMouseDown(x, y, rightButton, lang, outStatusToast)) {
            ForwardPdfPageRequest();
            return true;
        }
        break;
    case ResolvedContentKind::ArchiveSubExplorer: {
        if (m_archive.OnMouseDown(x, y, rightButton, lang, outStatusToast)) {
            std::wstring navArc = m_archive.TakePendingNavigateArchivePath();
            if (!navArc.empty()) {
                m_pendingNavigatePath = navArc;
            }
            ArchiveExplorer::PreviewEntryRequest entryReq;
            if (m_archive.TakePendingPreviewEntry(entryReq)) {
                // Shown once the worker has unpacked it (StartFetch)
                FileEntry childEntry = {};
                childEntry.name = entryReq.name;
                childEntry.displayName = childEntry.name;
                std::wstring innerWin = entryReq.innerPath;
                std::replace(innerWin.begin(), innerWin.end(), L'/', L'\\');
                childEntry.fullPath = PathUtils::EnsureTrailingSlash(entryReq.archivePath) + innerWin;
                childEntry.ext = PathUtils::GetExtension(entryReq.name);
                childEntry.size = entryReq.size;
                childEntry.sizeStr = FileEngine::FormatSizeKB(entryReq.size);
                childEntry.isDirectory = false;
                childEntry.isImage = IsImageExtension(childEntry.ext);
                childEntry.isVideo = IsVideoExtension(childEntry.ext);
                childEntry.isAudio = IsAudioExtension(childEntry.ext);
                m_currentEntry = std::move(childEntry);
                m_inspectingChildOfFolder = true;
                m_userTabMode = FileContentTabMode::Auto;
                StartFetch(entryReq.archivePath, entryReq.innerPath);
            }
            return true;
        }
        break;
    }
    default:
        break;
    }

    return true;
}

bool FileContentPane::OnMouseMove(float x, float y, WPARAM keyFlags) {
    bool changed = false;
    int newHov = 0;
    for (const auto& hit : m_headerHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverHeaderBtn) {
        m_hoverHeaderBtn = newHov;
        changed = true;
    }
    if (BodyShowsWaitNote() || m_fetchFailed) {
        return changed;
    }

    switch (m_resolvedKind) {
    case ResolvedContentKind::DirectoryInfo: {
        if (m_dirDraggingScroll) {
            float rowH = 22.0f;
            float listH = std::max(1.0f, m_dirListRect.bottom - m_dirListRect.top);
            float maxScroll = std::max(0.0f, static_cast<float>(m_dirPreviewItems.size()) * rowH - listH);
            float thumbH = m_dirScrollThumbRect.bottom - m_dirScrollThumbRect.top;
            float usable = std::max(1.0f, listH - thumbH);
            float frac = std::clamp((y - m_dirScrollDragOffset - m_dirScrollTrackRect.top) / usable, 0.0f, 1.0f);
            m_dirScrollY = frac * maxScroll;
            return true;
        }
        int newRowHov = -1;
        if (PathUtils::PtInRectF(x, y, m_dirListRect)) {
            int idx = static_cast<int>((y - m_dirListRect.top + m_dirScrollY) / 22.0f);
            if (idx >= 0 && idx < static_cast<int>(m_dirPreviewItems.size())) {
                newRowHov = idx;
            }
        }
        if (newRowHov != m_dirHoverIdx) {
            m_dirHoverIdx = newRowHov;
            changed = true;
        }
        break;
    }
    case ResolvedContentKind::NotepadEditor:
        if (m_notepad.OnMouseMove(x, y)) changed = true;
        break;
    case ResolvedContentKind::HexInspector:
        if (m_hex.OnMouseMove(x, y)) changed = true;
        break;
    case ResolvedContentKind::Model3DViewport:
        if (m_model3D.OnMouseMove(x, y, keyFlags)) changed = true;
        break;
    case ResolvedContentKind::EdaViewport:
        if (m_eda.OnMouseMove(x, y, keyFlags)) changed = true;
        break;
    case ResolvedContentKind::OfficeDocument:
        if (m_office.OnMouseMove(x, y)) changed = true;
        break;
    case ResolvedContentKind::PdfDocument:
        if (m_pdf.OnMouseMove(x, y, keyFlags)) changed = true;
        break;
    case ResolvedContentKind::ArchiveSubExplorer:
        if (m_archive.OnMouseMove(x, y)) changed = true;
        break;
    default:
        break;
    }
    return changed;
}

bool FileContentPane::OnMouseUp() {
    bool changed = false;
    if (m_dirDraggingScroll) {
        m_dirDraggingScroll = false;
        changed = true;
    }
    if (m_notepad.OnMouseUp()) changed = true;
    if (m_hex.OnMouseUp()) changed = true;
    if (m_model3D.OnMouseUp()) changed = true;
    if (m_eda.OnMouseUp()) changed = true;
    if (m_office.OnMouseUp()) changed = true;
    if (m_pdf.OnMouseUp()) changed = true;
    if (m_archive.OnMouseUp()) changed = true;
    return changed;
}

bool FileContentPane::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_paneRect)) return false;
    if (BodyShowsWaitNote() || m_fetchFailed) return true;
    if (m_resolvedKind == ResolvedContentKind::DirectoryInfo) {
        int notches = delta / WHEEL_DELTA;
        if (notches == 0) notches = (delta > 0) ? 1 : -1;
        float rowH = 22.0f;
        float listH = std::max(1.0f, m_dirListRect.bottom - m_dirListRect.top);
        float maxScroll = std::max(0.0f, static_cast<float>(m_dirPreviewItems.size()) * rowH - listH);
        m_dirScrollY = std::clamp(m_dirScrollY - notches * rowH * 3.0f, 0.0f, maxScroll);
        return true;
    }
    if (m_resolvedKind == ResolvedContentKind::NotepadEditor) {
        return m_notepad.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::HexInspector) {
        return m_hex.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::Model3DViewport) {
        return m_model3D.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::EdaViewport) {
        return m_eda.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::OfficeDocument) {
        return m_office.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::PdfDocument) {
        const bool handled = m_pdf.OnMouseWheel(delta, x, y);
        ForwardPdfPageRequest();
        return handled;
    }
    if (m_resolvedKind == ResolvedContentKind::ArchiveSubExplorer) {
        return m_archive.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::AiModelInspector) {
        int notches = delta / WHEEL_DELTA;
        if (notches == 0) notches = (delta > 0) ? 1 : -1;
        m_aiModelScrollY = std::max(0.0f, m_aiModelScrollY - notches * 44.0f);
        return true;
    }
    return false;
}

} // namespace Explorer2
