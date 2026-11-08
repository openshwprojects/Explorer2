#include "HexViewer.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace Explorer2 {

static D2D1_COLOR_F HexVColor(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

HexViewer::~HexViewer() {
    Shutdown();
}

void HexViewer::Initialize(HWND parentHwnd, IDWriteFactory* dwriteFactory) {
    m_parentHwnd = parentHwnd;
    if (dwriteFactory && !m_fmtMono) {
        dwriteFactory->CreateTextFormat(
            L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.5f, L"pl-PL", &m_fmtMono);
        if (m_fmtMono) {
            m_fmtMono->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            m_fmtMono->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

void HexViewer::Shutdown() {
    if (m_fmtMono) {
        m_fmtMono->Release();
        m_fmtMono = nullptr;
    }
}

void HexViewer::Clear() {
    m_filePath.clear();
    m_fileSize = 0;
    m_topRowIndex = 0;
    m_selectedOffset = 0;
    m_cacheBaseOffset = UINT64_MAX;
    m_cacheBytes.clear();
    m_headerBytes.clear();
}

void HexViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_filePath == fullPath && !m_filePath.empty()) {
        return;
    }
    m_filePath = fullPath;
    m_fileSize = 0;
    m_topRowIndex = 0;
    m_selectedOffset = 0;
    m_cacheBaseOffset = UINT64_MAX;
    m_cacheBytes.clear();
    m_headerBytes.clear();

    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER li = {};
    if (GetFileSizeEx(hFile, &li) && li.QuadPart > 0) {
        m_fileSize = static_cast<uint64_t>(li.QuadPart);
    }

    uint8_t hdr[128] = {};
    DWORD hdrRead = 0;
    if (ReadFile(hFile, hdr, sizeof(hdr), &hdrRead, nullptr) && hdrRead > 0) {
        m_headerBytes.assign(hdr, hdr + hdrRead);
    }
    CloseHandle(hFile);

    EnsureWindowCached(0, 8192);
}

bool HexViewer::ReadBytesAt(uint64_t offset, uint8_t* outBuf, size_t count, size_t* outRead) const {
    if (outRead) *outRead = 0;
    if (m_filePath.empty() || offset >= m_fileSize || count == 0) return false;

    if (m_cacheBaseOffset != UINT64_MAX &&
        offset >= m_cacheBaseOffset &&
        offset + count <= m_cacheBaseOffset + m_cacheBytes.size())
    {
        size_t rel = static_cast<size_t>(offset - m_cacheBaseOffset);
        std::memcpy(outBuf, m_cacheBytes.data() + rel, count);
        if (outRead) *outRead = count;
        return true;
    }

    HANDLE hFile = CreateFileW(m_filePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER pos = {};
    pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(hFile, pos, nullptr, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, outBuf, static_cast<DWORD>(count), &bytesRead, nullptr);
    CloseHandle(hFile);
    if (ok && outRead) *outRead = bytesRead;
    return ok != FALSE;
}

void HexViewer::EnsureWindowCached(uint64_t startOffset, size_t byteCount) {
    if (m_filePath.empty() || m_fileSize == 0) return;
    uint64_t alignedStart = (startOffset / 4096ULL) * 4096ULL;
    size_t windowLen = std::max<size_t>(8192, ((byteCount + 4095) / 4096 + 1) * 4096);
    if (m_cacheBaseOffset != UINT64_MAX &&
        startOffset >= m_cacheBaseOffset &&
        std::min<uint64_t>(m_fileSize, startOffset + byteCount) <= m_cacheBaseOffset + m_cacheBytes.size())
    {
        return;
    }

    HANDLE hFile = CreateFileW(m_filePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER pos = {};
    pos.QuadPart = static_cast<LONGLONG>(alignedStart);
    if (SetFilePointerEx(hFile, pos, nullptr, FILE_BEGIN)) {
        size_t toRead = static_cast<size_t>(std::min<uint64_t>(windowLen, m_fileSize - alignedStart));
        m_cacheBytes.resize(toRead);
        DWORD readBytes = 0;
        if (toRead > 0 && ReadFile(hFile, m_cacheBytes.data(), static_cast<DWORD>(toRead), &readBytes, nullptr)) {
            m_cacheBytes.resize(readBytes);
            m_cacheBaseOffset = alignedStart;
        }
    }
    CloseHandle(hFile);
}

std::wstring HexViewer::DetectMagicSignature(Lang lang) const {
    const bool pl = (lang == Lang::PL);
    const auto& h = m_headerBytes;
    if (h.empty()) return pl ? L"Pusty plik (0 B)" : L"Empty file (0 B)";

    if (h.size() >= 2 && h[0] == 'M' && h[1] == 'Z') {
        return pl ? L"PE / Plik wykonywalny Windows (MZ)" : L"PE / Windows Executable (MZ)";
    }
    if (h.size() >= 4 && h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' && h[3] == 'F') {
        return L"ELF Binary (Linux / Unix)";
    }
    if (h.size() >= 8 && h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') {
        return L"PNG Image (89 50 4E 47)";
    }
    if (h.size() >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) {
        return L"JPEG Image (FF D8 FF)";
    }
    if (h.size() >= 4 && h[0] == 'R' && h[1] == 'I' && h[2] == 'F' && h[3] == 'F') {
        if (h.size() >= 12 && h[8] == 'W' && h[9] == 'E' && h[10] == 'B' && h[11] == 'P') return L"WebP Image (RIFF/WEBP)";
        if (h.size() >= 12 && h[8] == 'W' && h[9] == 'A' && h[10] == 'V' && h[11] == 'E') return L"WAV Audio (RIFF/WAVE)";
        return L"RIFF Container";
    }
    if (h.size() >= 4 && h[0] == 'P' && h[1] == 'K' && h[2] == 0x03 && h[3] == 0x04) {
        return pl ? L"Archiwum ZIP / Kontener PKZIP" : L"ZIP Archive / PKZIP Container";
    }
    if (h.size() >= 6 && h[0] == '7' && h[1] == 'z' && h[2] == 0xBC && h[3] == 0xAF && h[4] == 0x27 && h[5] == 0x1C) {
        return L"7-Zip Archive (7z)";
    }
    if (h.size() >= 4 && h[0] == 'R' && h[1] == 'a' && h[2] == 'r' && h[3] == '!') {
        return L"RAR Archive";
    }
    if (h.size() >= 5 && h[0] == '%' && h[1] == 'P' && h[2] == 'D' && h[3] == 'F' && h[4] == '-') {
        return L"PDF Document (%PDF)";
    }
    if (h.size() >= 15 && std::memcmp(h.data(), "SQLite format 3", 15) == 0) {
        return L"SQLite 3 Database";
    }
    if (h.size() >= 4 && h[0] == 'G' && h[1] == 'G' && h[2] == 'U' && h[3] == 'F') {
        return L"GGUF LLM Tensor Weights";
    }
    if (h.size() >= 10 && h[8] == '{' && h[9] == '"') {
        uint64_t jsonLen = 0;
        std::memcpy(&jsonLen, h.data(), 8);
        if (jsonLen > 2 && jsonLen < 100 * 1024 * 1024) {
            return L"SafeTensors Model Weights (JSON Header: " + FileEngine::FormatSizeDetailed(jsonLen) + L")";
        }
    }
    if (h.size() >= 4 && h[0] == 'g' && h[1] == 'l' && h[2] == 'T' && h[3] == 'F') {
        return L"glTF 2.0 Binary 3D Asset (GLB)";
    }
    return pl ? L"Strumień binarny (Raw Binary)" : L"Raw Binary Stream";
}

void HexViewer::CopyVisibleHexDumpToClipboard() {
    if (!m_parentHwnd || m_fileSize == 0) return;
    uint64_t startOff = m_topRowIndex * 16ULL;
    size_t maxBytes = static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(m_visibleRows) * 16ULL, m_fileSize > startOff ? m_fileSize - startOff : 0));
    if (maxBytes == 0) return;

    EnsureWindowCached(startOff, maxBytes);
    std::wstring dump;
    for (int r = 0; r < m_visibleRows; ++r) {
        uint64_t rowOff = (m_topRowIndex + static_cast<uint64_t>(r)) * 16ULL;
        if (rowOff >= m_fileSize) break;
        wchar_t lineBuf[160] = {};
        swprintf_s(lineBuf, L"%08llX  ", static_cast<unsigned long long>(rowOff));
        dump += lineBuf;

        std::wstring asciiPart;
        for (int c = 0; c < 16; ++c) {
            uint64_t bOff = rowOff + static_cast<uint64_t>(c);
            uint8_t b = 0;
            size_t got = 0;
            if (bOff < m_fileSize && ReadBytesAt(bOff, &b, 1, &got) && got == 1) {
                wchar_t hb[8] = {};
                swprintf_s(hb, L"%02X ", b);
                dump += hb;
                asciiPart.push_back((b >= 32 && b <= 126) ? static_cast<wchar_t>(b) : L'.');
            } else {
                dump += L"   ";
                asciiPart.push_back(L' ');
            }
            if (c == 7) dump += L" ";
        }
        dump += L" |" + asciiPart + L"|\r\n";
    }

    if (OpenClipboard(m_parentHwnd)) {
        EmptyClipboard();
        size_t bytes = (dump.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* p = GlobalLock(hMem);
            if (p) {
                std::memcpy(p, dump.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

void HexViewer::CopySelectedBytesHexToClipboard() {
    if (!m_parentHwnd || m_selectedOffset >= m_fileSize) return;
    uint8_t buf[16] = {};
    size_t got = 0;
    size_t toRead = static_cast<size_t>(std::min<uint64_t>(16, m_fileSize - m_selectedOffset));
    if (!ReadBytesAt(m_selectedOffset, buf, toRead, &got) || got == 0) return;

    std::wstring hexStr;
    for (size_t i = 0; i < got; ++i) {
        wchar_t b[8] = {};
        swprintf_s(b, i + 1 < got ? L"%02X " : L"%02X", buf[i]);
        hexStr += b;
    }
    if (OpenClipboard(m_parentHwnd)) {
        EmptyClipboard();
        size_t bytes = (hexStr.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* p = GlobalLock(hMem);
            if (p) {
                std::memcpy(p, hexStr.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

void HexViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F /*mousePt*/)
{
    const bool pl = (lang == Lang::PL);
    IDWriteTextFormat* mono = m_fmtMono ? m_fmtMono : fmtSmall;
    m_toolbarHits.clear();

    // 1. Top Toolbar + Magic Signature Banner (52px total: 28px buttons + 24px signature/column header)
    float tbH = 28.0f;
    float sigH = 22.0f;
    float colHdrH = 22.0f;
    float inspectorH = 82.0f;

    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(HexVColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(HexVColor(0xE2E8F0));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto drawBtn = [&](int id, float w, const wchar_t* label) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 3.5f, bx + w, tbR.bottom - 3.5f);
        m_toolbarHits.push_back({ id, br });
        bool hov = (m_hoverBtnId == id);
        brush->SetColor(hov ? HexVColor(0xDBEAFE) : HexVColor(0xFFFFFF));
        rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
        brush->SetColor(hov ? HexVColor(0x3B82F6) : HexVColor(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush, 1.0f);
        brush->SetColor(HexVColor(0x1E293B));
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), fmtSmall, br, brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        bx += w + 4.0f;
    };

    drawBtn(BTN_JUMP_START, 62.0f, pl ? L"\x21E4 Początek" : L"\x21E4 Start");
    drawBtn(BTN_JUMP_MID,   52.0f, L"50%");
    drawBtn(BTN_JUMP_END,   58.0f, pl ? L"Koniec \x21E5" : L"End \x21E5");
    drawBtn(BTN_COPY_PAGE,  88.0f, pl ? L"\xD83D\xDCCB Kopiuj widok" : L"\xD83D\xDCCB Copy page");
    drawBtn(BTN_COPY_SEL,   86.0f, pl ? L"\xD83D\xDCCB Kopiuj 16B" : L"\xD83D\xDCCB Copy 16B");

    // 2. Signature bar
    D2D1_RECT_F sigR = D2D1::RectF(rect.left, tbR.bottom, rect.right, tbR.bottom + sigH);
    brush->SetColor(HexVColor(0xEFF6FF));
    rt->FillRectangle(sigR, brush);
    brush->SetColor(HexVColor(0xDBEAFE));
    rt->DrawLine(D2D1::Point2F(sigR.left, sigR.bottom - 0.5f), D2D1::Point2F(sigR.right, sigR.bottom - 0.5f), brush, 1.0f);

    std::wstring sigTxt = (pl ? L"Sygnatura: " : L"Signature: ") + DetectMagicSignature(lang) +
                          L"   \x2022   " + FileEngine::FormatSizeDetailed(m_fileSize);
    D2D1_RECT_F sigTxtR = D2D1::RectF(sigR.left + 8.0f, sigR.top, sigR.right - 8.0f, sigR.bottom);
    brush->SetColor(HexVColor(0x1E40AF));
    rt->DrawTextW(sigTxt.c_str(), static_cast<UINT32>(sigTxt.size()), fmtSmall, sigTxtR, brush);

    // 3. Column Header Bar (Offset | 00..0F | ASCII)
    D2D1_RECT_F hdrR = D2D1::RectF(rect.left, sigR.bottom, rect.right, sigR.bottom + colHdrH);
    brush->SetColor(HexVColor(0xF1F5F9));
    rt->FillRectangle(hdrR, brush);
    brush->SetColor(HexVColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(hdrR.left, hdrR.bottom - 0.5f), D2D1::Point2F(hdrR.right, hdrR.bottom - 0.5f), brush, 1.0f);

    float scrollW = 12.0f;
    float availW = std::max(240.0f, (rect.right - rect.left) - scrollW);
    float offsetColW = 68.0f;
    float byteCellW = std::clamp((availW - offsetColW - 130.0f) / 16.5f, 14.5f, 21.0f);
    float hexGroupGap = 5.0f;
    float hexAreaLeft = rect.left + offsetColW;
    float asciiAreaLeft = hexAreaLeft + 16.0f * byteCellW + hexGroupGap + 8.0f;
    float asciiCellW = std::clamp((rect.right - scrollW - asciiAreaLeft - 4.0f) / 16.0f, 6.2f, 9.0f);

    brush->SetColor(HexVColor(0x475569));
    D2D1_RECT_F offHdrR = D2D1::RectF(rect.left + 6.0f, hdrR.top, rect.left + offsetColW, hdrR.bottom);
    rt->DrawTextW(L"Offset", 6, fmtBold, offHdrR, brush);

    mono->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    for (int c = 0; c < 16; ++c) {
        float cx = hexAreaLeft + c * byteCellW + (c >= 8 ? hexGroupGap : 0.0f);
        wchar_t colLbl[4] = {};
        swprintf_s(colLbl, L"%02X", c);
        D2D1_RECT_F cr = D2D1::RectF(cx, hdrR.top, cx + byteCellW, hdrR.bottom);
        brush->SetColor((m_selectedOffset % 16ULL == static_cast<uint64_t>(c)) ? HexVColor(0x0067B8) : HexVColor(0x64748B));
        rt->DrawTextW(colLbl, 2, mono, cr, brush);
    }
    mono->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    if (asciiAreaLeft + 40.0f < rect.right - scrollW) {
        D2D1_RECT_F ascHdrR = D2D1::RectF(asciiAreaLeft, hdrR.top, rect.right - scrollW, hdrR.bottom);
        brush->SetColor(HexVColor(0x475569));
        rt->DrawTextW(L"ASCII", 5, fmtBold, ascHdrR, brush);
    }

    // 4. Main Hex Grid Area
    m_gridRect = D2D1::RectF(rect.left, hdrR.bottom, rect.right - scrollW, std::max(hdrR.bottom + 40.0f, rect.bottom - inspectorH));
    brush->SetColor(HexVColor(0xFFFFFF));
    rt->FillRectangle(m_gridRect, brush);

    float rowH = 18.0f;
    m_visibleRows = std::max(1, static_cast<int>((m_gridRect.bottom - m_gridRect.top) / rowH));
    uint64_t totalRows = (m_fileSize + 15ULL) / 16ULL;
    if (totalRows == 0) totalRows = 1;
    uint64_t maxTopRow = (totalRows > static_cast<uint64_t>(m_visibleRows)) ? (totalRows - static_cast<uint64_t>(m_visibleRows)) : 0;
    if (m_topRowIndex > maxTopRow) m_topRowIndex = maxTopRow;

    EnsureWindowCached(m_topRowIndex * 16ULL, static_cast<size_t>(m_visibleRows) * 16ULL + 64ULL);

    rt->PushAxisAlignedClip(m_gridRect, D2D1_ANTIALIAS_MODE_ALIASED);

    for (int r = 0; r < m_visibleRows; ++r) {
        uint64_t rowIdx = m_topRowIndex + static_cast<uint64_t>(r);
        uint64_t rowOff = rowIdx * 16ULL;
        if (rowOff >= m_fileSize && m_fileSize > 0) break;

        float ry = m_gridRect.top + r * rowH;
        D2D1_RECT_F rowR = D2D1::RectF(m_gridRect.left, ry, m_gridRect.right, ry + rowH);
        if (r % 2 == 1) {
            brush->SetColor(HexVColor(0xF8FAFC));
            rt->FillRectangle(rowR, brush);
        }

        // Row offset
        wchar_t offBuf[32] = {};
        swprintf_s(offBuf, L"%08llX", static_cast<unsigned long long>(rowOff));
        D2D1_RECT_F offR = D2D1::RectF(m_gridRect.left + 6.0f, ry, m_gridRect.left + offsetColW - 2.0f, ry + rowH);
        brush->SetColor(HexVColor(0x64748B));
        rt->DrawTextW(offBuf, static_cast<UINT32>(wcslen(offBuf)), mono, offR, brush);

        if (m_fileSize == 0) break;

        uint8_t rowBytes[16] = {};
        size_t gotBytes = 0;
        size_t wantBytes = static_cast<size_t>(std::min<uint64_t>(16ULL, m_fileSize - rowOff));
        ReadBytesAt(rowOff, rowBytes, wantBytes, &gotBytes);

        mono->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (size_t c = 0; c < gotBytes; ++c) {
            uint64_t bOff = rowOff + c;
            uint8_t b = rowBytes[c];
            float cx = hexAreaLeft + static_cast<float>(c) * byteCellW + (c >= 8 ? hexGroupGap : 0.0f);
            D2D1_RECT_F cellR = D2D1::RectF(cx, ry + 1.0f, cx + byteCellW, ry + rowH - 1.0f);

            bool isSel = (bOff == m_selectedOffset);
            if (isSel) {
                brush->SetColor(HexVColor(0x0078D7));
                rt->FillRoundedRectangle(D2D1::RoundedRect(cellR, 2.0f, 2.0f), brush);
            } else if (bOff > m_selectedOffset && bOff < m_selectedOffset + 4) {
                brush->SetColor(HexVColor(0xE0F2FE));
                rt->FillRectangle(cellR, brush);
            }

            if (isSel) {
                brush->SetColor(HexVColor(0xFFFFFF));
            } else if (b == 0x00) {
                brush->SetColor(HexVColor(0xCBD5E1));
            } else if (b >= 0x20 && b <= 0x7E) {
                brush->SetColor(HexVColor(0x0F172A));
            } else if (b == 0xFF) {
                brush->SetColor(HexVColor(0xDC2626));
            } else {
                brush->SetColor(HexVColor(0x2563EB));
            }

            wchar_t hb[4] = {};
            swprintf_s(hb, L"%02X", b);
            rt->DrawTextW(hb, 2, mono, cellR, brush);

            // Corresponding ASCII character cell
            if (asciiAreaLeft + (c + 1) * asciiCellW <= m_gridRect.right) {
                float ax = asciiAreaLeft + static_cast<float>(c) * asciiCellW;
                D2D1_RECT_F ascCellR = D2D1::RectF(ax, ry + 1.0f, ax + asciiCellW, ry + rowH - 1.0f);
                if (isSel) {
                    brush->SetColor(HexVColor(0x0078D7));
                    rt->FillRectangle(ascCellR, brush);
                    brush->SetColor(HexVColor(0xFFFFFF));
                } else {
                    brush->SetColor((b >= 0x20 && b <= 0x7E) ? HexVColor(0x0F172A) : HexVColor(0x94A3B8));
                }
                wchar_t ac[2] = { (b >= 0x20 && b <= 0x7E) ? static_cast<wchar_t>(b) : L'\x00B7', 0 };
                rt->DrawTextW(ac, 1, mono, ascCellR, brush);
            }
        }
        mono->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    rt->PopAxisAlignedClip();

    // 5. Right Scrollbar for 64-bit Hex View
    m_scrollTrackRect = D2D1::RectF(m_gridRect.right, m_gridRect.top, rect.right, m_gridRect.bottom);
    brush->SetColor(HexVColor(0xF1F5F9));
    rt->FillRectangle(m_scrollTrackRect, brush);
    if (totalRows > static_cast<uint64_t>(m_visibleRows)) {
        float trackH = m_scrollTrackRect.bottom - m_scrollTrackRect.top;
        float thumbH = std::max(24.0f, trackH * (static_cast<float>(m_visibleRows) / static_cast<float>(totalRows)));
        float frac = (maxTopRow > 0) ? static_cast<float>(static_cast<double>(m_topRowIndex) / static_cast<double>(maxTopRow)) : 0.0f;
        float thumbTop = m_scrollTrackRect.top + frac * (trackH - thumbH);
        m_scrollThumbRect = D2D1::RectF(m_scrollTrackRect.left + 2.0f, thumbTop, m_scrollTrackRect.right - 2.0f, thumbTop + thumbH);
        brush->SetColor(m_draggingScroll ? HexVColor(0x0078D7) : HexVColor(0x94A3B8));
        rt->FillRoundedRectangle(D2D1::RoundedRect(m_scrollThumbRect, 3.0f, 3.0f), brush);
    } else {
        m_scrollThumbRect = {};
    }

    // 6. Bottom Data Inspector Card (Int8/UInt8, Int16/UInt16, Int32/UInt32, Int64, Float32, Binary)
    D2D1_RECT_F inspR = D2D1::RectF(rect.left, m_gridRect.bottom, rect.right, rect.bottom);
    brush->SetColor(HexVColor(0xF8FAFC));
    rt->FillRectangle(inspR, brush);
    brush->SetColor(HexVColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(inspR.left, inspR.top), D2D1::Point2F(inspR.right, inspR.top), brush, 1.0f);

    if (m_fileSize > 0 && m_selectedOffset < m_fileSize) {
        uint8_t valBuf[8] = {};
        size_t got = 0;
        size_t avail = static_cast<size_t>(std::min<uint64_t>(8ULL, m_fileSize - m_selectedOffset));
        ReadBytesAt(m_selectedOffset, valBuf, avail, &got);

        uint8_t  u8  = valBuf[0];
        int8_t   i8  = static_cast<int8_t>(u8);
        uint16_t u16 = 0; int16_t i16 = 0;
        uint32_t u32 = 0; int32_t i32 = 0;
        int64_t  i64 = 0;
        float    f32 = 0.0f;
        std::memcpy(&u16, valBuf, sizeof(u16)); i16 = static_cast<int16_t>(u16);
        std::memcpy(&u32, valBuf, sizeof(u32)); i32 = static_cast<int32_t>(u32);
        std::memcpy(&i64, valBuf, sizeof(i64));
        std::memcpy(&f32, valBuf, sizeof(f32));

        wchar_t binBits[10] = {};
        for (int b = 0; b < 8; ++b) {
            binBits[b] = ((u8 >> (7 - b)) & 1) ? L'1' : L'0';
        }
        binBits[8] = 0;

        wchar_t line1[256] = {};
        wchar_t line2[256] = {};
        wchar_t line3[256] = {};
        swprintf_s(line1,
            pl ? L"Inspektor danych @ Offset: 0x%llX (%llu)   \x2022   Binarny: %s"
               : L"Data Inspector @ Offset: 0x%llX (%llu)   \x2022   Binary: %s",
            static_cast<unsigned long long>(m_selectedOffset),
            static_cast<unsigned long long>(m_selectedOffset),
            binBits);
        swprintf_s(line2, L"Int8: %d  (UInt8: %u)     |     Int16 LE: %d  (UInt16: %u)",
            static_cast<int>(i8), static_cast<unsigned>(u8),
            static_cast<int>(i16), static_cast<unsigned>(u16));
        if (std::isfinite(f32) && std::abs(f32) < 1e12f && (f32 == 0.0f || std::abs(f32) > 1e-9f)) {
            swprintf_s(line3, L"Int32 LE: %d  (UInt32: %u)     |     Float32: %.6g     |     Int64: %lld",
                i32, u32, f32, static_cast<long long>(i64));
        } else {
            swprintf_s(line3, L"Int32 LE: %d  (UInt32: %u)     |     Int64 LE: %lld",
                i32, u32, static_cast<long long>(i64));
        }

        brush->SetColor(HexVColor(0x0067B8));
        rt->DrawTextW(line1, static_cast<UINT32>(wcslen(line1)), fmtBold,
            D2D1::RectF(inspR.left + 8.0f, inspR.top + 4.0f, inspR.right - 8.0f, inspR.top + 26.0f), brush);
        brush->SetColor(HexVColor(0x1E293B));
        rt->DrawTextW(line2, static_cast<UINT32>(wcslen(line2)), mono,
            D2D1::RectF(inspR.left + 8.0f, inspR.top + 28.0f, inspR.right - 8.0f, inspR.top + 50.0f), brush);
        rt->DrawTextW(line3, static_cast<UINT32>(wcslen(line3)), mono,
            D2D1::RectF(inspR.left + 8.0f, inspR.top + 52.0f, inspR.right - 8.0f, inspR.top + 74.0f), brush);
    }
}

bool HexViewer::OnMouseDown(float x, float y, Lang lang, std::wstring* outStatusToast) {
    const bool pl = (lang == Lang::PL);
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            uint64_t totalRows = (m_fileSize + 15ULL) / 16ULL;
            uint64_t maxTopRow = (totalRows > static_cast<uint64_t>(m_visibleRows)) ? (totalRows - static_cast<uint64_t>(m_visibleRows)) : 0;
            switch (hit.id) {
            case BTN_JUMP_START:
                m_topRowIndex = 0;
                m_selectedOffset = 0;
                break;
            case BTN_JUMP_MID:
                m_topRowIndex = maxTopRow / 2;
                m_selectedOffset = std::min<uint64_t>(m_fileSize > 0 ? m_fileSize - 1 : 0, (totalRows / 2) * 16ULL);
                break;
            case BTN_JUMP_END:
                m_topRowIndex = maxTopRow;
                m_selectedOffset = (m_fileSize > 0) ? (m_fileSize - 1) : 0;
                break;
            case BTN_COPY_PAGE:
                CopyVisibleHexDumpToClipboard();
                if (outStatusToast) {
                    *outStatusToast = pl ? L"\x2713 Skopiowano zrzut Hex widocznych wierszy do schowka"
                                         : L"\x2713 Copied visible Hex dump to clipboard";
                }
                break;
            case BTN_COPY_SEL:
                CopySelectedBytesHexToClipboard();
                if (outStatusToast) {
                    *outStatusToast = pl ? L"\x2713 Skopiowano 16 bajtów Hex od wybranego offsetu"
                                         : L"\x2713 Copied 16 Hex bytes from selected offset";
                }
                break;
            }
            return true;
        }
    }

    // Check scrollbar click/drag
    if (PathUtils::PtInRectF(x, y, m_scrollTrackRect)) {
        if (PathUtils::PtInRectF(x, y, m_scrollThumbRect)) {
            m_draggingScroll = true;
            m_scrollDragOffsetY = y - m_scrollThumbRect.top;
        } else {
            uint64_t totalRows = (m_fileSize + 15ULL) / 16ULL;
            uint64_t maxTopRow = (totalRows > static_cast<uint64_t>(m_visibleRows)) ? (totalRows - static_cast<uint64_t>(m_visibleRows)) : 0;
            float trackH = std::max(1.0f, m_scrollTrackRect.bottom - m_scrollTrackRect.top);
            float frac = std::clamp((y - m_scrollTrackRect.top) / trackH, 0.0f, 1.0f);
            m_topRowIndex = static_cast<uint64_t>(frac * static_cast<double>(maxTopRow));
        }
        return true;
    }

    // Check byte click inside main Hex grid
    if (PathUtils::PtInRectF(x, y, m_gridRect) && m_fileSize > 0) {
        float rowH = 18.0f;
        int clickedRow = static_cast<int>((y - m_gridRect.top) / rowH);
        if (clickedRow >= 0 && clickedRow < m_visibleRows) {
            float availW = std::max(240.0f, m_gridRect.right - m_gridRect.left);
            float offsetColW = 68.0f;
            float byteCellW = std::clamp((availW - offsetColW - 130.0f) / 16.5f, 14.5f, 21.0f);
            float hexGroupGap = 5.0f;
            float hexAreaLeft = m_gridRect.left + offsetColW;
            float asciiAreaLeft = hexAreaLeft + 16.0f * byteCellW + hexGroupGap + 8.0f;
            float asciiCellW = std::clamp((m_gridRect.right - asciiAreaLeft - 4.0f) / 16.0f, 6.2f, 9.0f);

            int clickedCol = -1;
            if (x >= hexAreaLeft && x < asciiAreaLeft) {
                float relX = x - hexAreaLeft;
                if (relX > 8.0f * byteCellW) relX -= hexGroupGap;
                clickedCol = std::clamp(static_cast<int>(relX / byteCellW), 0, 15);
            } else if (x >= asciiAreaLeft) {
                clickedCol = std::clamp(static_cast<int>((x - asciiAreaLeft) / asciiCellW), 0, 15);
            }
            if (clickedCol >= 0) {
                uint64_t cand = (m_topRowIndex + static_cast<uint64_t>(clickedRow)) * 16ULL + static_cast<uint64_t>(clickedCol);
                if (cand < m_fileSize) {
                    m_selectedOffset = cand;
                    return true;
                }
            }
        }
    }
    return false;
}

bool HexViewer::OnMouseMove(float x, float y) {
    if (m_draggingScroll) {
        uint64_t totalRows = (m_fileSize + 15ULL) / 16ULL;
        uint64_t maxTopRow = (totalRows > static_cast<uint64_t>(m_visibleRows)) ? (totalRows - static_cast<uint64_t>(m_visibleRows)) : 0;
        float trackH = m_scrollTrackRect.bottom - m_scrollTrackRect.top;
        float thumbH = m_scrollThumbRect.bottom - m_scrollThumbRect.top;
        float usable = std::max(1.0f, trackH - thumbH);
        float frac = std::clamp((y - m_scrollDragOffsetY - m_scrollTrackRect.top) / usable, 0.0f, 1.0f);
        m_topRowIndex = static_cast<uint64_t>(frac * static_cast<double>(maxTopRow));
        return true;
    }

    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        return true;
    }
    return false;
}

bool HexViewer::OnMouseUp() {
    if (m_draggingScroll) {
        m_draggingScroll = false;
        return true;
    }
    return false;
}

bool HexViewer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_gridRect) && !PathUtils::PtInRectF(x, y, m_scrollTrackRect)) {
        return false;
    }
    int stepRows = (GetKeyState(VK_CONTROL) & 0x8000) ? 24 : 4;
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    uint64_t totalRows = (m_fileSize + 15ULL) / 16ULL;
    uint64_t maxTopRow = (totalRows > static_cast<uint64_t>(m_visibleRows)) ? (totalRows - static_cast<uint64_t>(m_visibleRows)) : 0;

    int64_t nextRow = static_cast<int64_t>(m_topRowIndex) - static_cast<int64_t>(notches * stepRows);
    if (nextRow < 0) nextRow = 0;
    if (static_cast<uint64_t>(nextRow) > maxTopRow) nextRow = static_cast<int64_t>(maxTopRow);
    m_topRowIndex = static_cast<uint64_t>(nextRow);
    return true;
}

} // namespace Explorer2
