#include "EdaViewer.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace Explorer2 {

namespace {

static D2D1_COLOR_F EdaColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct ProjectedQuad3D {
    D2D1_POINT_2F pts[4] = {};
    float depth = 0.0f;
    uint32_t fillRgb = 0x1E293B;
    uint32_t strokeRgb = 0x0F172A;
    float alpha = 1.0f;
    bool drawStroke = false;
};

static EdaPartCategory ClassifyComponent(const std::wstring& refDes, const std::wstring& fp, float& outW, float& outL, float& outH, int& outPins) {
    std::wstring r = PathUtils::ToUpper(refDes);
    std::wstring f = PathUtils::ToUpper(fp);

    // Default dimensions based on common footprint names
    if (f.find(L"0402") != std::wstring::npos) { outW = 1.0f; outL = 0.5f; outH = 0.45f; }
    else if (f.find(L"0603") != std::wstring::npos) { outW = 1.6f; outL = 0.85f; outH = 0.55f; }
    else if (f.find(L"0805") != std::wstring::npos) { outW = 2.0f; outL = 1.25f; outH = 0.65f; }
    else if (f.find(L"1206") != std::wstring::npos) { outW = 3.2f; outL = 1.6f; outH = 0.75f; }
    else if (f.find(L"SOT-23") != std::wstring::npos || f.find(L"SOT23") != std::wstring::npos) {
        outW = 2.9f; outL = 1.6f; outH = 1.1f; outPins = std::max(outPins, 3);
        return EdaPartCategory::TransistorSOT;
    } else if (f.find(L"SOIC") != std::wstring::npos || f.find(L"SOP") != std::wstring::npos ||
               f.find(L"TSSOP") != std::wstring::npos || f.find(L"MSOP") != std::wstring::npos) {
        outW = std::max(outW, 5.0f); outL = std::max(outL, 4.0f); outH = 1.55f;
        outPins = std::max(outPins, 8);
        return EdaPartCategory::IC_Package;
    } else if (f.find(L"QFP") != std::wstring::npos || f.find(L"QFN") != std::wstring::npos ||
               f.find(L"BGA") != std::wstring::npos || f.find(L"DIP") != std::wstring::npos) {
        outW = std::max(outW, 7.0f); outL = std::max(outL, 7.0f); outH = 1.6f;
        outPins = std::max(outPins, 16);
        return EdaPartCategory::IC_Package;
    }

    if (!r.empty()) {
        if (r[0] == L'U' || (r.size() >= 2 && r[0] == L'I' && r[1] == L'C')) {
            outW = std::max(outW, 4.8f); outL = std::max(outL, 3.8f); outH = std::max(outH, 1.5f);
            outPins = std::max(outPins, 8);
            return EdaPartCategory::IC_Package;
        }
        if (r[0] == L'R') {
            return EdaPartCategory::ResistorSMD;
        }
        if (r[0] == L'C') {
            if (f.find(L"ELEC") != std::wstring::npos || f.find(L"CP_") != std::wstring::npos ||
                f.find(L"RADIAL") != std::wstring::npos || outW >= 5.0f) {
                outW = std::max(outW, 5.0f); outL = std::max(outL, 5.0f); outH = 5.5f;
                return EdaPartCategory::ElectrolyticCap;
            }
            return EdaPartCategory::CapacitorSMD;
        }
        if (r[0] == L'J' || (r.size() >= 2 && r[0] == L'C' && r[1] == L'N') || r[0] == L'P') {
            outW = std::max(outW, 5.0f); outL = std::max(outL, 2.54f); outH = 4.2f;
            return EdaPartCategory::Connector;
        }
        if (r[0] == L'Q' || r[0] == L'T') {
            outW = std::max(outW, 2.8f); outL = std::max(outL, 1.6f); outH = 1.1f;
            return EdaPartCategory::TransistorSOT;
        }
        if (r[0] == L'D' || r.rfind(L"LED", 0) == 0) {
            return EdaPartCategory::DiodeLED;
        }
        if (r[0] == L'L' || r.rfind(L"FB", 0) == 0) {
            outH = std::max(outH, 1.8f);
            return EdaPartCategory::Inductor;
        }
        if (r[0] == L'Y' || r[0] == L'X') {
            outW = std::max(outW, 5.0f); outL = std::max(outL, 3.2f); outH = 1.8f;
            return EdaPartCategory::Crystal;
        }
        if (r[0] == L'H' || r.rfind(L"MH", 0) == 0) {
            return EdaPartCategory::MountingHole;
        }
    }
    return EdaPartCategory::GenericSMD;
}

static void FinalizeBoardBounds(ParsedEdaBoard& b) {
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    bool hasEdgeCuts = false;

    auto expand = [&](float x, float y) {
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    };

    for (const auto& t : b.traces) {
        if (t.layer == EdaLayerId::EdgeCuts) {
            hasEdgeCuts = true;
            expand(t.x1, t.y1);
            expand(t.x2, t.y2);
        }
    }

    if (!hasEdgeCuts) {
        for (const auto& t : b.traces) { expand(t.x1, t.y1); expand(t.x2, t.y2); }
        for (const auto& p : b.pads)   { expand(p.x - p.w, p.y - p.h); expand(p.x + p.w, p.y + p.h); }
        for (const auto& v : b.vias)   { expand(v.x - v.diameter, v.y - v.diameter); expand(v.x + v.diameter, v.y + v.diameter); }
        for (const auto& c : b.components) {
            expand(c.x - c.widthMm, c.y - c.lengthMm);
            expand(c.x + c.widthMm, c.y + c.lengthMm);
        }
        for (const auto& cr : b.circles) {
            expand(cr.cx - cr.radius, cr.cy - cr.radius);
            expand(cr.cx + cr.radius, cr.cy + cr.radius);
        }
    }

    if (minX > maxX || minY > maxY) {
        b.minX = -30.0f; b.minY = -20.0f;
        b.maxX = 30.0f;  b.maxY = 20.0f;
    } else {
        float pad = hasEdgeCuts ? 0.0f : std::max(2.0f, std::min(maxX - minX, maxY - minY) * 0.06f);
        b.minX = minX - pad;
        b.minY = minY - pad;
        b.maxX = maxX + pad;
        b.maxY = maxY + pad;
        if (b.maxX - b.minX < 2.0f) { b.minX -= 5.0f; b.maxX += 5.0f; }
        if (b.maxY - b.minY < 2.0f) { b.minY -= 5.0f; b.maxY += 5.0f; }
    }
}

// Helper for XML attribute extraction in Eagle .brd / .sch files
static bool GetXmlAttr(std::string_view tag, std::string_view attrName, std::string_view& outVal) {
    size_t pos = 0;
    while (pos < tag.size()) {
        size_t found = tag.find(attrName, pos);
        if (found == std::string_view::npos) return false;
        if (found > 0) {
            char prev = tag[found - 1];
            if (prev != ' ' && prev != '\t' && prev != '\r' && prev != '\n') {
                pos = found + attrName.size();
                continue;
            }
        }
        size_t eq = found + attrName.size();
        while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
        if (eq >= tag.size() || tag[eq] != '=') {
            pos = eq;
            continue;
        }
        ++eq;
        while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
        if (eq >= tag.size() || (tag[eq] != '"' && tag[eq] != '\'')) return false;
        char quote = tag[eq++];
        size_t endQ = tag.find(quote, eq);
        if (endQ == std::string_view::npos) return false;
        outVal = tag.substr(eq, endQ - eq);
        return true;
    }
    return false;
}

static float GetXmlFloat(std::string_view tag, std::string_view attrName, float defVal = 0.0f) {
    std::string_view v;
    if (!GetXmlAttr(tag, attrName, v) || v.empty()) return defVal;
    char buf[64] = {};
    size_t n = std::min<size_t>(v.size(), 63);
    std::memcpy(buf, v.data(), n);
    return std::strtof(buf, nullptr);
}

static int GetXmlInt(std::string_view tag, std::string_view attrName, int defVal = 0) {
    std::string_view v;
    if (!GetXmlAttr(tag, attrName, v) || v.empty()) return defVal;
    char buf[32] = {};
    size_t n = std::min<size_t>(v.size(), 31);
    std::memcpy(buf, v.data(), n);
    return std::atoi(buf);
}

static EdaLayerId MapEagleLayer(int eagleLayer, bool isSch) {
    if (isSch) {
        if (eagleLayer == 91 || eagleLayer == 92) return EdaLayerId::SchWire;
        if (eagleLayer == 94) return EdaLayerId::SchSymbol;
        return EdaLayerId::SchLabel;
    }
    switch (eagleLayer) {
    case 1:  return EdaLayerId::TopCopper;
    case 16: return EdaLayerId::BottomCopper;
    case 20: return EdaLayerId::EdgeCuts;
    case 21: case 25: case 27: case 51: return EdaLayerId::TopSilk;
    case 22: case 26: case 28: case 52: return EdaLayerId::BottomSilk;
    case 17: case 18: return EdaLayerId::PadsThrough;
    default:
        if (eagleLayer >= 2 && eagleLayer <= 15) return EdaLayerId::InnerCopper;
        return EdaLayerId::TopSilk;
    }
}

struct EaglePkgDef {
    std::vector<EdaPadPrim> localPads;
    std::vector<EdaTraceSeg> localSilk;
    float minX = -1.5f, minY = -1.0f, maxX = 1.5f, maxY = 1.0f;
};

#include "formats/EagleXml.h"
#include "formats/KiCadSExpr.h"
#include "formats/GerberDrill.h"
#include "formats/KiCadLegacySch.h"
#include "formats/AutoCadDxf.h"

} // namespace

#include "formats/LtspiceAsc.h"
#include "formats/SpecctraDsn.h"
#include "formats/IpcD356.h"
#include "formats/IntelHex.h"
#include "formats/MotorolaSrec.h"
#include "formats/SpiceNetlist.h"
#include "formats/HpglPlot.h"
#include "formats/VerilogVcd.h"
#include "formats/TouchstoneSnp.h"
#include "formats/AsicLef.h"
#include "formats/AsicDef.h"
#include "formats/GdsiiStream.h"
#include "formats/JtagBsdl.h"
#include "formats/CmsisSvd.h"
#include "formats/ExtendedEda100.h"
#include "formats/ExtendedEda200.h"

EdaViewer::~EdaViewer() {
    Shutdown();
}

void EdaViewer::Initialize(HWND notifyHwnd, ID2D1Factory* d2dFactory) {
    m_notifyHwnd = notifyHwnd;
    m_d2dFactory = d2dFactory;
}

void EdaViewer::Shutdown() {
    Clear();
}

bool EdaViewer::IsEdaExtension(const std::wstring& extLower) {
    return extLower == L".kicad_pcb" || extLower == L".kicad_sch" || extLower == L".kicad_mod" ||
           extLower == L".brd"       || extLower == L".sch"       || extLower == L".lbr"       ||
           extLower == L".gbr"       || extLower == L".ger"       || extLower == L".gtl"       ||
           extLower == L".gbl"       || extLower == L".gts"       || extLower == L".gbs"       ||
           extLower == L".gto"       || extLower == L".gbo"       || extLower == L".gm1"       ||
           extLower == L".gko"       || extLower == L".drl"       || extLower == L".xln"       ||
           extLower == L".dxf"       || extLower == L".asc"       || extLower == L".dsn"       ||
           extLower == L".d356"      || extLower == L".ipc"       || extLower == L".hex"       ||
           extLower == L".ihex"      || extLower == L".srec"      || extLower == L".s19"       ||
           extLower == L".mot"       || extLower == L".cir"       || extLower == L".spice"     ||
           extLower == L".sp"        || extLower == L".net"       || extLower == L".hpgl"      ||
           extLower == L".plt"       || extLower == L".vcd"       || extLower == L".s1p"       ||
           extLower == L".s2p"       || extLower == L".lef"       || extLower == L".def"       ||
           extLower == L".gds"       || extLower == L".gds2"      || extLower == L".bsdl"      ||
           extLower == L".svd"       || extLower == L".odb"       || extLower == L".ipc2581"   ||
           extLower == L".cvg"       || extLower == L".pcbdoc"    || extLower == L".schdoc"    ||
           extLower == L".blf"       || extLower == L".can"       || extLower == L".dbc"       ||
           extLower == L".fst"       || extLower == L".ghw"       || extLower == L".bit"       ||
           extLower == L".rpd"       || extLower == L".dfu"       || extLower == L".ufd"       ||
           extLower == L".edid"      || extLower == L".spd"       ||
           extLower == L".kicad_sym" || extLower == L".ses"       || extLower == L".pos"       ||
           extLower == L".cpl"       || extLower == L".mnt"       || extLower == L".ibs"       ||
           extLower == L".s3p"       || extLower == L".s4p"       || extLower == L".s8p"       ||
           extLower == L".oas"       || extLower == L".xdc"       || extLower == L".sdc"       ||
           extLower == L".qsf"       || extLower == L".jed"       || extLower == L".svf"       ||
           extLower == L".xsvf"      || extLower == L".sof"       || extLower == L".pof"       ||
           extLower == L".sr"        || extLower == L".sal"       || extLower == L".wfm"       ||
           extLower == L".isf"       || extLower == L".mf4"       || extLower == L".mdf"       ||
           extLower == L".ldf"       || extLower == L".arxml"     || extLower == L".eds"       ||
           extLower == L".dcf"       || extLower == L".gsdml"     || extLower == L".l5x";
}

bool EdaViewer::InspectEdaFile(const std::wstring& fullPath, ParsedEdaBoard& outBoard) try {
    outBoard = ParsedEdaBoard{};
    outBoard.filePath = fullPath;

    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, 32 * 1024 * 1024, raw) || raw.empty()) {
        return false;
    }

    std::wstring ext = PathUtils::GetExtension(fullPath);
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    if (ext == L".dxf") {
        return ParseAutoCadDxf(sv, outBoard);
    }

    // Modular EDA & Firmware formats (Format15 .. Format20)
    {
        bool okMod = false;
        if (ext == L".asc") okMod = Formats::ParseLtspiceAsc(sv, outBoard);
        else if (ext == L".dsn") okMod = Formats::ParseSpecctraDsn(sv, outBoard);
        else if (ext == L".d356" || ext == L".ipc") okMod = Formats::ParseIpcD356(sv, outBoard);
        else if (ext == L".hex" || ext == L".ihex") okMod = Formats::ParseIntelHexBoard(sv, outBoard);
        else if (ext == L".srec" || ext == L".s19" || ext == L".mot") okMod = Formats::ParseMotorolaSrecBoard(sv, outBoard);
        else if (ext == L".cir" || ext == L".spice" || ext == L".sp" || ext == L".net") okMod = Formats::ParseSpiceNetlistBoard(sv, outBoard);
        else if (ext == L".hpgl" || ext == L".plt") okMod = Formats::ParseHpglPlotterBoard(sv, outBoard);
        else if (ext == L".vcd") okMod = Formats::ParseVerilogVcdWaveforms(sv, outBoard);
        else if (ext == L".s1p" || ext == L".s2p") okMod = Formats::ParseTouchstoneSnpBoard(sv, outBoard);
        else if (ext == L".lef") okMod = Formats::ParseAsicLefBoard(sv, outBoard);
        else if (ext == L".def") okMod = Formats::ParseAsicDefBoard(sv, outBoard);
        else if (ext == L".gds" || ext == L".gds2") okMod = Formats::ParseGdsiiStreamBoard(raw.data(), raw.size(), outBoard);
        else if (ext == L".bsdl") okMod = Formats::ParseJtagBsdlBoard(sv, outBoard);
        else if (ext == L".svd") okMod = Formats::ParseCmsisSvdBoard(sv, outBoard);
        else if (ext == L".odb" || ext == L".ipc2581" || ext == L".cvg") okMod = Formats::ParseOdbOrIpc2581Board(sv, ext, outBoard);
        else if (ext == L".pcbdoc" || ext == L".schdoc") okMod = Formats::ParseAltiumDocBoard(raw.data(), raw.size(), ext, outBoard);
        else if (ext == L".dbc" || ext == L".blf" || ext == L".can") okMod = Formats::ParseVectorCanBoard(sv, ext, outBoard);
        else if (ext == L".fst" || ext == L".ghw" || ext == L".bit" || ext == L".rpd" ||
                 ext == L".dfu" || ext == L".ufd" || ext == L".edid" || ext == L".spd")
            okMod = Formats::ParseHardwareBinBoard(raw.data(), raw.size(), ext, outBoard);
        else
            okMod = Formats::ParseExtendedEda200(raw.data(), raw.size(), ext, outBoard);
        if (!okMod && ext == L".asc") okMod = Formats::ParseVectorCanBoard(sv, ext, outBoard);
        if (okMod) {
            FinalizeBoardBounds(outBoard);
            outBoard.valid = true;
            return true;
        }
    }

    if (sv.rfind("EESchema Schematic", 0) == 0) {
        return ParseKiCadLegacySch(sv, outBoard);
    }

    if (ext == L".kicad_pcb" || ext == L".kicad_sch" || ext == L".kicad_mod" ||
        sv.find("(kicad_pcb") != std::string_view::npos ||
        sv.find("(kicad_sch") != std::string_view::npos ||
        sv.find("(footprint ") != std::string_view::npos)
    {
        return ParseKiCadSExpr(sv, ext, outBoard);
    }

    if (sv.find("<eagle") != std::string_view::npos || sv.find("<board>") != std::string_view::npos ||
        sv.find("<schematic>") != std::string_view::npos || ext == L".brd" || ext == L".sch" || ext == L".lbr")
    {
        if (sv.find('<') != std::string_view::npos && ParseEagleXml(sv, ext, outBoard)) {
            return true;
        }
        // Binary Eagle v4/v5 (.brd/.sch) fallback: extract embedded RefDes & generate layout grid
        if (raw.size() >= 24 && raw[0] == 0x10) {
            outBoard.kind = (ext == L".sch") ? EdaDocKind::EagleSch : EdaDocKind::EagleBrd;
            outBoard.formatTitle = (ext == L".sch") ? L"Eagle Binary Schematic (v4/v5)" : L"Eagle Binary Board (v4/v5)";
            int idx = 0;
            for (size_t i = 24; i + 8 < raw.size(); ++i) {
                if ((raw[i] == 'U' || raw[i] == 'R' || raw[i] == 'C' || raw[i] == 'J' || raw[i] == 'Q' || raw[i] == 'D') &&
                    raw[i + 1] >= '1' && raw[i + 1] <= '9')
                {
                    std::string ref;
                    size_t k = i;
                    while (k < raw.size() && k - i < 8 && ((raw[k] >= 'A' && raw[k] <= 'Z') || (raw[k] >= '0' && raw[k] <= '9'))) {
                        ref.push_back(static_cast<char>(raw[k++]));
                    }
                    if (ref.size() >= 2 && k < raw.size() && raw[k] == 0) {
                        EdaComponent3D c;
                        c.refDes = PathUtils::Utf8ToWide(ref);
                        c.footprint = L"SMD";
                        int col = idx % 8;
                        int row = idx / 8;
                        c.x = -24.0f + col * 7.0f;
                        c.y = -16.0f + row * 6.0f;
                        c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
                        outBoard.components.push_back(c);
                        outBoard.pads.push_back({ c.x - c.widthMm * 0.4f, c.y, 0.8f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"1", L"" });
                        outBoard.pads.push_back({ c.x + c.widthMm * 0.4f, c.y, 0.8f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"2", L"" });
                        ++idx;
                        i = k;
                        if (idx >= 64) break;
                    }
                }
            }
            FinalizeBoardBounds(outBoard);
            outBoard.valid = !outBoard.components.empty();
            return outBoard.valid;
        }
    }

    return ParseGerberOrDrill(sv, ext, outBoard);
} E2_READER_CATCH(L"Board reader", fullPath, { outBoard = ParsedEdaBoard(); return false; })

void EdaViewer::Clear() {
    m_currentPath.clear();
    m_board = ParsedEdaBoard{};
    m_selectedCompIdx = -1;
}

void EdaViewer::Fit2DView() {
    m_zoom2D = 1.0f;
    m_pan2DX = 0.0f;
    m_pan2DY = 0.0f;
}

void EdaViewer::Reset3DCamera() {
    m_yaw3D = 0.62f;
    m_pitch3D = 0.68f;
    m_zoom3D = 1.0f;
    m_pan3DX = 0.0f;
    m_pan3DY = 0.0f;
}

void EdaViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_currentPath == fullPath && !m_currentPath.empty()) {
        return;
    }
    ParsedEdaBoard board;
    InspectEdaFile(fullPath, board);
    ShowLoadedBoard(fullPath, std::move(board));
}

void EdaViewer::ShowLoadedBoard(const std::wstring& fullPath, ParsedEdaBoard&& board) {
    m_currentPath = fullPath;
    m_selectedCompIdx = -1;
    m_bomScrollY = 0.0f;
    m_board = std::move(board);
    Fit2DView();
    Reset3DCamera();

    // Default to 3D PCB view if it's a board with components, or 2D CAD for schematics/gerbers (user can toggle anytime!)
    m_view3D = false;
}

void EdaViewer::StepAnimation() {
    if (!m_view3D || !m_autoRotate3D) return;
    ULONGLONG now = GetTickCount64();
    float dt = (m_lastAnimTick > 0 && now > m_lastAnimTick)
        ? std::min(0.1f, static_cast<float>(now - m_lastAnimTick) * 0.001f)
        : 0.016f;
    m_lastAnimTick = now;
    m_yaw3D += dt * 0.75f;
    if (m_yaw3D > 6.2831853f) m_yaw3D -= 6.2831853f;
}

void EdaViewer::Draw2DCanvas(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& canvasR,
    const ParsedEdaBoard& board,
    Lang lang)
{
    const bool isSch = (board.kind == EdaDocKind::KiCadSch || board.kind == EdaDocKind::EagleSch);
    brush->SetColor(isSch ? EdaColor(0xFFFDF7) : EdaColor(0x0B1320));
    rt->FillRectangle(canvasR, brush);

    float availW = std::max(40.0f, canvasR.right - canvasR.left - 32.0f);
    float availH = std::max(40.0f, canvasR.bottom - canvasR.top - 32.0f);
    float bw = std::max(1.0f, board.maxX - board.minX);
    float bh = std::max(1.0f, board.maxY - board.minY);
    float baseScale = std::min(availW / bw, availH / bh);
    float scale = baseScale * m_zoom2D;

    float cx = (canvasR.left + canvasR.right) * 0.5f + m_pan2DX;
    float cy = (canvasR.top + canvasR.bottom) * 0.5f + m_pan2DY;
    float midX = (board.minX + board.maxX) * 0.5f;
    float midY = (board.minY + board.maxY) * 0.5f;

    auto toScreen = [&](float bx, float by) -> D2D1_POINT_2F {
        return D2D1::Point2F(cx + (bx - midX) * scale, cy - (by - midY) * scale);
    };

    // Subtle CAD grid
    float gridStepMm = (scale > 15.0f) ? 1.27f : ((scale > 4.0f) ? 5.0f : 10.0f);
    float stepPx = gridStepMm * scale;
    if (stepPx >= 10.0f) {
        brush->SetColor(isSch ? EdaColor(0xE2E8F0, 0.7f) : EdaColor(0x1E293B, 0.8f));
        for (float gx = std::fmod(cx - canvasR.left, stepPx) + canvasR.left; gx < canvasR.right; gx += stepPx) {
            rt->DrawLine(D2D1::Point2F(gx, canvasR.top), D2D1::Point2F(gx, canvasR.bottom), brush, 0.5f);
        }
        for (float gy = std::fmod(cy - canvasR.top, stepPx) + canvasR.top; gy < canvasR.bottom; gy += stepPx) {
            rt->DrawLine(D2D1::Point2F(canvasR.left, gy), D2D1::Point2F(canvasR.right, gy), brush, 0.5f);
        }
    }

    // Board outline substrate fill in 2D PCB mode
    if (!isSch) {
        D2D1_POINT_2F pMin = toScreen(board.minX, board.maxY);
        D2D1_POINT_2F pMax = toScreen(board.maxX, board.minY);
        D2D1_RECT_F brdR = D2D1::RectF(pMin.x, pMin.y, pMax.x, pMax.y);
        brush->SetColor(EdaColor(0x0F291E, 0.85f));
        rt->FillRectangle(brdR, brush);
        brush->SetColor(EdaColor(0xFACC15, 0.9f));
        rt->DrawRectangle(brdR, brush, 1.4f);
    }

    // 1. Bottom Copper traces
    if (m_showBotCu) {
        brush->SetColor(EdaColor(0x3B82F6, 0.85f));
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::BottomCopper) continue;
            D2D1_POINT_2F p1 = toScreen(t.x1, t.y1);
            D2D1_POINT_2F p2 = toScreen(t.x2, t.y2);
            rt->DrawLine(p1, p2, brush, std::max(1.2f, t.width * scale));
        }
    }

    // 2. Top Copper & Schematic traces
    if (m_showTopCu) {
        for (const auto& t : board.traces) {
            if (t.layer == EdaLayerId::BottomCopper || t.layer == EdaLayerId::TopSilk || t.layer == EdaLayerId::BottomSilk) continue;
            if (t.layer == EdaLayerId::EdgeCuts) brush->SetColor(EdaColor(0xFACC15));
            else if (t.layer == EdaLayerId::SchWire) brush->SetColor(EdaColor(0x15803D));
            else if (t.layer == EdaLayerId::SchSymbol) brush->SetColor(EdaColor(0x991B1B));
            else if (t.layer == EdaLayerId::InnerCopper) brush->SetColor(EdaColor(0xA855F7, 0.8f));
            else brush->SetColor(EdaColor(0xEF4444, 0.9f));

            D2D1_POINT_2F p1 = toScreen(t.x1, t.y1);
            D2D1_POINT_2F p2 = toScreen(t.x2, t.y2);
            rt->DrawLine(p1, p2, brush, std::max(1.2f, t.width * scale));
        }
    }

    // 3. Silkscreen lines & circles
    if (m_showSilk) {
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::TopSilk && t.layer != EdaLayerId::BottomSilk) continue;
            brush->SetColor(t.layer == EdaLayerId::BottomSilk ? EdaColor(0xC084FC, 0.8f) : EdaColor(0xF8FAFC, 0.9f));
            rt->DrawLine(toScreen(t.x1, t.y1), toScreen(t.x2, t.y2), brush, std::max(1.0f, t.width * scale));
        }
        for (const auto& c : board.circles) {
            D2D1_POINT_2F pc = toScreen(c.cx, c.cy);
            float r = std::max(1.5f, c.radius * scale);
            brush->SetColor(EdaColor(0xF8FAFC, 0.85f));
            rt->DrawEllipse(D2D1::Ellipse(pc, r, r), brush, std::max(1.0f, c.width * scale));
        }
    }

    // 4. Pads & Vias
    if (m_showPads) {
        for (const auto& p : board.pads) {
            if (p.layer == EdaLayerId::TopCopper && !m_showTopCu) continue;
            if (p.layer == EdaLayerId::BottomCopper && !m_showBotCu) continue;
            D2D1_POINT_2F pc = toScreen(p.x, p.y);
            float hw = std::max(1.8f, p.w * scale * 0.5f);
            float hh = std::max(1.8f, p.h * scale * 0.5f);
            if (p.layer == EdaLayerId::PadsThrough) brush->SetColor(EdaColor(0x10B981));
            else if (p.layer == EdaLayerId::BottomCopper) brush->SetColor(EdaColor(0x3B82F6));
            else brush->SetColor(EdaColor(0xF59E0B));

            if (p.shape == 0) {
                rt->FillEllipse(D2D1::Ellipse(pc, hw, hh), brush);
            } else {
                D2D1_RECT_F pr = D2D1::RectF(pc.x - hw, pc.y - hh, pc.x + hw, pc.y + hh);
                rt->FillRoundedRectangle(D2D1::RoundedRect(pr, 1.5f, 1.5f), brush);
            }
            if (p.drill > 0.05f) {
                float dr = std::max(1.0f, p.drill * scale * 0.5f);
                brush->SetColor(EdaColor(0x0B1320));
                rt->FillEllipse(D2D1::Ellipse(pc, dr, dr), brush);
            }
        }

        for (const auto& v : board.vias) {
            D2D1_POINT_2F pc = toScreen(v.x, v.y);
            float r = std::max(2.0f, v.diameter * scale * 0.5f);
            float dr = std::max(0.9f, v.drill * scale * 0.5f);
            brush->SetColor(EdaColor(0xEAB308));
            rt->FillEllipse(D2D1::Ellipse(pc, r, r), brush);
            brush->SetColor(EdaColor(0x0B1320));
            rt->FillEllipse(D2D1::Ellipse(pc, dr, dr), brush);
        }
    }

    // 5. Component RefDes Labels & Selection Highlight
    if (m_showSilk) {
        for (size_t i = 0; i < board.components.size(); ++i) {
            const auto& c = board.components[i];
            D2D1_POINT_2F pc = toScreen(c.x, c.y);
            float hw = std::max(4.0f, c.widthMm * scale * 0.5f);
            float hh = std::max(3.0f, c.lengthMm * scale * 0.5f);

            if (static_cast<int>(i) == m_selectedCompIdx) {
                D2D1_RECT_F selR = D2D1::RectF(pc.x - hw - 3.0f, pc.y - hh - 3.0f, pc.x + hw + 3.0f, pc.y + hh + 3.0f);
                brush->SetColor(EdaColor(0x22D3EE, 0.28f));
                rt->FillRoundedRectangle(D2D1::RoundedRect(selR, 3.0f, 3.0f), brush);
                brush->SetColor(EdaColor(0x22D3EE));
                rt->DrawRoundedRectangle(D2D1::RoundedRect(selR, 3.0f, 3.0f), brush, 2.0f);
            }

            if (scale >= 2.2f && !c.refDes.empty()) {
                brush->SetColor(isSch ? EdaColor(0x0F172A) : (c.bottomSide ? EdaColor(0x93C5FD) : EdaColor(0xFEF08A)));
                D2D1_RECT_F tr = D2D1::RectF(pc.x - 45.0f, pc.y - 10.0f, pc.x + 45.0f, pc.y + 10.0f);
                fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(c.refDes.c_str(), static_cast<UINT32>(c.refDes.size()), fmtSmall, tr, brush);
                fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
    }
    (void)fmtBold;
    (void)lang;
}

void EdaViewer::Draw3DPcbViewport(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& canvasR,
    const ParsedEdaBoard& board,
    Lang lang)
{
    // Studio Dark 3D background
    brush->SetColor(EdaColor(0x0F172A));
    rt->FillRectangle(canvasR, brush);

    float cx = (canvasR.left + canvasR.right) * 0.5f + m_pan3DX;
    float cy = (canvasR.top + canvasR.bottom) * 0.5f + m_pan3DY;
    float availW = std::max(80.0f, canvasR.right - canvasR.left);
    float availH = std::max(80.0f, canvasR.bottom - canvasR.top);

    float bw = std::max(4.0f, board.maxX - board.minX);
    float bh = std::max(4.0f, board.maxY - board.minY);
    float midX = (board.minX + board.maxX) * 0.5f;
    float midY = (board.minY + board.maxY) * 0.5f;
    float maxSpan = std::max(bw, bh);
    float scale = (std::min(availW, availH) * 0.72f / maxSpan) * m_zoom3D;

    float cyaw = std::cos(m_yaw3D), syaw = std::sin(m_yaw3D);
    float cpitch = std::cos(m_pitch3D), spitch = std::sin(m_pitch3D);

    auto project3D = [&](float wx, float wy, float wz, float* outDepth = nullptr) -> D2D1_POINT_2F {
        float lx = wx - midX;
        float ly = wy - midY;
        float lz = wz;
        // Rotate around Z (Yaw) then tilt around X (Pitch)
        float rx = lx * cyaw - ly * syaw;
        float ry = lx * syaw + ly * cyaw;
        float sy2 = ry * cpitch - lz * spitch;
        float sz2 = ry * spitch + lz * cpitch;
        if (outDepth) *outDepth = sz2;
        float persp = 1.0f / std::max(0.35f, 1.0f - (sz2 / (maxSpan * 3.5f)));
        return D2D1::Point2F(cx + rx * scale * persp, cy - sy2 * scale * persp);
    };

    // Solder mask color palette: Green, Blue, Red, Matte Black, Purple, White
    static const uint32_t kMaskTopColors[6]  = { 0x14532D, 0x1E3A8A, 0x7F1D1D, 0x18181B, 0x4C1D95, 0xE2E8F0 };
    static const uint32_t kMaskEdgeColors[6] = { 0xA16207, 0x854D0E, 0x854D0E, 0x52525B, 0x713F12, 0x94A3B8 };
    uint32_t maskRgb = kMaskTopColors[m_solderMaskPreset % 6];
    uint32_t edgeRgb = kMaskEdgeColors[m_solderMaskPreset % 6];

    std::vector<ProjectedQuad3D> quads;
    quads.reserve(board.components.size() * 10 + board.pads.size() + 16);

    auto pushQuad3D = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, uint32_t fill, uint32_t stroke, bool drawStrk = true, float alpha = 1.0f) {
        ProjectedQuad3D q;
        float d0 = 0, d1 = 0, d2 = 0, d3 = 0;
        q.pts[0] = project3D(p0.x, p0.y, p0.z, &d0);
        q.pts[1] = project3D(p1.x, p1.y, p1.z, &d1);
        q.pts[2] = project3D(p2.x, p2.y, p2.z, &d2);
        q.pts[3] = project3D(p3.x, p3.y, p3.z, &d3);
        q.depth = (d0 + d1 + d2 + d3) * 0.25f;
        q.fillRgb = fill;
        q.strokeRgb = stroke;
        q.drawStroke = drawStrk;
        q.alpha = alpha;
        quads.push_back(q);
    };

    auto pushBox3D = [&](float cx3, float cy3, float zMin, float zMax, float w, float l, float rotDeg,
                         uint32_t topRgb, uint32_t sideRgb, uint32_t strokeRgb) {
        float hw = w * 0.5f, hl = l * 0.5f;
        float rad = rotDeg * 3.14159265f / 180.0f;
        float cs = std::cos(rad), sn = std::sin(rad);
        auto corner = [&](float dx, float dy, float z) -> Vec3 {
            return { cx3 + (dx * cs - dy * sn), cy3 + (dx * sn + dy * cs), z };
        };
        Vec3 b0 = corner(-hw, -hl, zMin), b1 = corner(hw, -hl, zMin), b2 = corner(hw, hl, zMin), b3 = corner(-hw, hl, zMin);
        Vec3 t0 = corner(-hw, -hl, zMax), t1 = corner(hw, -hl, zMax), t2 = corner(hw, hl, zMax), t3 = corner(-hw, hl, zMax);
        pushQuad3D(b0, b1, t1, t0, sideRgb, strokeRgb, true);
        pushQuad3D(b1, b2, t2, t1, sideRgb, strokeRgb, true);
        pushQuad3D(b2, b3, t3, t2, sideRgb, strokeRgb, true);
        pushQuad3D(b3, b0, t0, t3, sideRgb, strokeRgb, true);
        pushQuad3D(t0, t1, t2, t3, topRgb, strokeRgb, true);
    };

    // 1. Draw 3D FR4 PCB Board Substrate (-0.8 mm to +0.8 mm)
    float zBot = -0.8f, zTop = 0.8f;
    Vec3 brdB0{ board.minX, board.minY, zBot }, brdB1{ board.maxX, board.minY, zBot },
         brdB2{ board.maxX, board.maxY, zBot }, brdB3{ board.minX, board.maxY, zBot };
    Vec3 brdT0{ board.minX, board.minY, zTop }, brdT1{ board.maxX, board.minY, zTop },
         brdT2{ board.maxX, board.maxY, zTop }, brdT3{ board.minX, board.maxY, zTop };

    // Board side edges (FR4 epoxy glass) + Top/Bottom solder mask planes
    pushQuad3D(brdB0, brdB1, brdT1, brdT0, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB1, brdB2, brdT2, brdT1, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB2, brdB3, brdT3, brdT2, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB3, brdB0, brdT0, brdT3, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdT0, brdT1, brdT2, brdT3, maskRgb, 0x22C55E, true, 0.96f);

    // 2. Render 3D Surface Pads (ENIG Gold / HASL Silver)
    if (m_showPads) {
        for (const auto& p : board.pads) {
            float pz = (p.layer == EdaLayerId::BottomCopper) ? (zBot - 0.04f) : (zTop + 0.04f);
            float hw = p.w * 0.5f, hh = p.h * 0.5f;
            uint32_t padRgb = (p.layer == EdaLayerId::PadsThrough) ? 0xFBBF24 : 0xE2E8F0;
            pushQuad3D(
                { p.x - hw, p.y - hh, pz },
                { p.x + hw, p.y - hh, pz },
                { p.x + hw, p.y + hh, pz },
                { p.x - hw, p.y + hh, pz },
                padRgb, 0x94A3B8, false);
        }
    }

    // 3. Render 3D Extruded Electronic Components (ICs, SMD Resistors/Capacitors, Connectors, Transistors, LEDs)
    if (m_show3DParts) {
        for (size_t i = 0; i < board.components.size(); ++i) {
            const auto& c = board.components[i];
            if (c.category == EdaPartCategory::MountingHole) continue;

            bool isSelected = (static_cast<int>(i) == m_selectedCompIdx);
            float baseZ = c.bottomSide ? (zBot - c.heightMm) : zTop;
            float topZ  = c.bottomSide ? zBot : (zTop + c.heightMm);
            uint32_t stroke = isSelected ? 0x22D3EE : 0x0F172A;

            switch (c.category) {
            case EdaPartCategory::ResistorSMD: {
                // Silver metallic end-caps + dark resistive ceramic body in center
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.03f, c.widthMm * 0.62f, c.lengthMm * 0.96f, c.rotDeg,
                    isSelected ? 0x0284C7 : 0x1E293B, 0x0F172A, stroke);
                break;
            }
            case EdaPartCategory::CapacitorSMD: {
                // Silver metallic end-caps + warm MLCC tan/brown ceramic body
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.03f, c.widthMm * 0.66f, c.lengthMm * 0.96f, c.rotDeg,
                    isSelected ? 0x0284C7 : 0xB45309, 0x78350F, stroke);
                break;
            }
            case EdaPartCategory::ElectrolyticCap: {
                // Black plastic base + tall aluminum cylinder body
                pushBox3D(c.x, c.y, baseZ, baseZ + 0.6f, c.widthMm, c.lengthMm, c.rotDeg, 0x1E293B, 0x0F172A, stroke);
                pushBox3D(c.x, c.y, baseZ + 0.6f, topZ, c.widthMm * 0.82f, c.lengthMm * 0.82f, c.rotDeg + 22.5f,
                    isSelected ? 0x38BDF8 : 0x94A3B8, 0x475569, stroke);
                break;
            }
            case EdaPartCategory::IC_Package:
            case EdaPartCategory::TransistorSOT: {
                // Dark molded epoxy IC package + metallic lead frame base
                pushBox3D(c.x, c.y, baseZ, baseZ + 0.25f, c.widthMm * 1.08f, c.lengthMm * 1.08f, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ + 0.15f, topZ, c.widthMm * 0.88f, c.lengthMm * 0.88f, c.rotDeg,
                    isSelected ? 0x0369A1 : 0x1E293B, 0x0F172A, stroke);
                break;
            }
            case EdaPartCategory::Connector: {
                // Beige/Black header housing + gold pin posts
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg,
                    isSelected ? 0x0284C7 : 0xD6D3D1, 0x78716C, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.6f, c.widthMm * 0.7f, c.lengthMm * 0.25f, c.rotDeg, 0xFACC15, 0xCA8A04, stroke);
                break;
            }
            case EdaPartCategory::DiodeLED: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0x22C55E, 0x15803D, stroke);
                break;
            }
            case EdaPartCategory::Crystal: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xE2E8F0, 0x94A3B8, stroke);
                break;
            }
            default: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg,
                    isSelected ? 0x0284C7 : 0x334155, 0x1E293B, stroke);
                break;
            }
            }
        }
    }

    // Painter's algorithm sort (back to front)
    std::sort(quads.begin(), quads.end(), [](const ProjectedQuad3D& a, const ProjectedQuad3D& b) {
        return a.depth < b.depth;
    });

    for (const auto& q : quads) {
        if (m_d2dFactory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink)) && sink) {
                    sink->BeginFigure(q.pts[0], D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(q.pts[1]);
                    sink->AddLine(q.pts[2]);
                    sink->AddLine(q.pts[3]);
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();

                    brush->SetColor(EdaColor(q.fillRgb, q.alpha));
                    rt->FillGeometry(path, brush);
                    if (q.drawStroke) {
                        brush->SetColor(EdaColor(q.strokeRgb, 0.85f));
                        rt->DrawGeometry(path, brush, 0.85f);
                    }
                }
                path->Release();
            }
        }
    }

    // Overlay 3D Copper Traces on Top Surface
    if (m_showTopCu) {
        brush->SetColor(EdaColor(0x22C55E, 0.75f));
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::TopCopper) continue;
            D2D1_POINT_2F p1 = project3D(t.x1, t.y1, zTop + 0.03f);
            D2D1_POINT_2F p2 = project3D(t.x2, t.y2, zTop + 0.03f);
            rt->DrawLine(p1, p2, brush, std::max(1.0f, t.width * scale * 0.8f));
        }
    }

    // Overlay 3D Component Silkscreen Designators on Top of 3D Packages
    if (m_showSilk && scale >= 2.2f) {
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (const auto& c : board.components) {
            if (c.refDes.empty()) continue;
            float labelZ = m_show3DParts ? (c.bottomSide ? (zBot - c.heightMm - 0.1f) : (zTop + c.heightMm + 0.1f)) : (zTop + 0.1f);
            D2D1_POINT_2F pt = project3D(c.x, c.y, labelZ);
            brush->SetColor(EdaColor(0xF8FAFC));
            rt->DrawTextW(c.refDes.c_str(), static_cast<UINT32>(c.refDes.size()), fmtSmall,
                D2D1::RectF(pt.x - 35.0f, pt.y - 8.0f, pt.x + 35.0f, pt.y + 8.0f), brush);
        }
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    (void)fmtBold;
    (void)lang;
}

void EdaViewer::Draw(
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
    m_toolbarHits.clear();

    float tbH = 30.0f;
    float statusH = 26.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(EdaColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(EdaColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool active) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, br, label, active, m_hoverBtnId == id);
        bx += w + 4.0f;
    };

    addBtn(BTN_TOGGLE_2D_3D, pl ? 88.0f : 82.0f,
        m_view3D ? (pl ? L"\xD83E\xDDCA Widok 3D PCB" : L"\xD83E\xDDCA 3D PCB View")
                 : (pl ? L"\xD83D\xDCD0 Widok 2D CAD" : L"\xD83D\xDCD0 2D CAD View"),
        m_view3D);

    if (m_view3D) {
        addBtn(BTN_TOGGLE_3D_PARTS, pl ? 78.0f : 72.0f, pl ? L"Elementy 3D" : L"3D Parts", m_show3DParts);
        addBtn(BTN_SOLDER_MASK_COLOR, pl ? 74.0f : 70.0f, pl ? L"Kolor maski" : L"Mask Color", false);
        addBtn(BTN_AUTO_ROTATE_3D, pl ? 70.0f : 64.0f, pl ? L"Auto-obrót" : L"Turntable", m_autoRotate3D);
    } else {
        addBtn(BTN_LAYER_TOPCU, 54.0f, L"Top Cu", m_showTopCu);
        addBtn(BTN_LAYER_BOTCU, 54.0f, L"Bot Cu", m_showBotCu);
        addBtn(BTN_LAYER_SILK,  46.0f, L"Silk",   m_showSilk);
        addBtn(BTN_LAYER_PADS,  52.0f, L"Pads",   m_showPads);
    }
    addBtn(BTN_TOGGLE_BOM, 56.0f, L"BOM", m_showBomPanel);
    addBtn(BTN_FIT_RESET,  pl ? 58.0f : 52.0f, pl ? L"Dopasuj" : L"Fit", false);

    D2D1_RECT_F mainR = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom, rect.bottom - statusH));
    if (m_showBomPanel && (mainR.right - mainR.left) > 260.0f) {
        float bomW = std::min(190.0f, (mainR.right - mainR.left) * 0.42f);
        m_canvasRect = D2D1::RectF(mainR.left, mainR.top, mainR.right - bomW, mainR.bottom);
        m_bomRect = D2D1::RectF(mainR.right - bomW, mainR.top, mainR.right, mainR.bottom);
    } else {
        m_canvasRect = mainR;
        m_bomRect = {};
    }

    rt->PushAxisAlignedClip(m_canvasRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (m_view3D) {
        Draw3DPcbViewport(rt, brush, fmtSmall, fmtBold, m_canvasRect, m_board, lang);
    } else {
        Draw2DCanvas(rt, brush, fmtSmall, fmtBold, m_canvasRect, m_board, lang);
    }
    rt->PopAxisAlignedClip();

    // Optional Right-Side BOM Component List
    if (m_showBomPanel && m_bomRect.right > m_bomRect.left) {
        brush->SetColor(EdaColor(0xF8FAFC));
        rt->FillRectangle(m_bomRect, brush);
        brush->SetColor(EdaColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(m_bomRect.left, m_bomRect.top), D2D1::Point2F(m_bomRect.left, m_bomRect.bottom), brush, 1.0f);

        rt->PushAxisAlignedClip(m_bomRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float rowH = 20.0f;
        for (size_t i = 0; i < m_board.components.size(); ++i) {
            float ry = m_bomRect.top + i * rowH - m_bomScrollY;
            if (ry + rowH < m_bomRect.top || ry > m_bomRect.bottom) continue;
            D2D1_RECT_F rr = D2D1::RectF(m_bomRect.left + 2.0f, ry, m_bomRect.right - 2.0f, ry + rowH);
            if (static_cast<int>(i) == m_selectedCompIdx) {
                brush->SetColor(EdaColor(0xDBEAFE));
                rt->FillRectangle(rr, brush);
            }
            const auto& c = m_board.components[i];
            std::wstring lbl = c.refDes + (c.value.empty() ? L"" : (L"  " + c.value)) + (c.footprint.empty() ? L"" : (L" (" + c.footprint + L")"));
            brush->SetColor(EdaColor(0x0F172A));
            rt->DrawTextW(lbl.c_str(), static_cast<UINT32>(lbl.size()), fmtSmall,
                D2D1::RectF(rr.left + 4.0f, rr.top, rr.right - 4.0f, rr.bottom), brush);
        }
        rt->PopAxisAlignedClip();
    }

    // Bottom Status Strip
    D2D1_RECT_F stR = D2D1::RectF(rect.left, mainR.bottom, rect.right, rect.bottom);
    brush->SetColor(EdaColor(0xF1F5F9));
    rt->FillRectangle(stR, brush);
    brush->SetColor(EdaColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(stR.left, stR.top), D2D1::Point2F(stR.right, stR.top), brush, 1.0f);

    wchar_t stBuf[320] = {};
    if (m_selectedCompIdx >= 0 && m_selectedCompIdx < static_cast<int>(m_board.components.size())) {
        const auto& c = m_board.components[m_selectedCompIdx];
        swprintf_s(stBuf,
            L"\x2713 %s  |  Val: %s  |  Pkg: %s  |  Pos: (%.2f, %.2f mm) %s",
            c.refDes.c_str(), c.value.c_str(), c.footprint.c_str(), c.x, c.y, c.bottomSide ? L"[Bottom]" : L"[Top]");
    } else {
        swprintf_s(stBuf,
            pl ? L"%s   \x2022   Elementy: %zu   |   Pady/Via: %zu   |   Ścieżki: %zu   |   Wymiary: %.1f \xD7 %.1f mm"
               : L"%s   \x2022   Parts: %zu   |   Pads/Vias: %zu   |   Traces: %zu   |   Board: %.1f \xD7 %.1f mm",
            m_board.formatTitle.c_str(),
            m_board.components.size(),
            m_board.pads.size() + m_board.vias.size(),
            m_board.traces.size(),
            std::abs(m_board.maxX - m_board.minX),
            std::abs(m_board.maxY - m_board.minY));
    }
    brush->SetColor(EdaColor(0x334155));
    rt->DrawTextW(stBuf, static_cast<UINT32>(wcslen(stBuf)), fmtSmall,
        D2D1::RectF(stR.left + 8.0f, stR.top, stR.right - 8.0f, stR.bottom), brush);
    (void)fmtRegular;
}

bool EdaViewer::OnMouseDown(float x, float y, bool rightButton, Lang /*lang*/, std::wstring* /*outStatusToast*/) {
    if (!rightButton) {
        for (const auto& hit : m_toolbarHits) {
            if (PathUtils::PtInRectF(x, y, hit.rect)) {
                switch (hit.id) {
                case BTN_TOGGLE_2D_3D:      m_view3D = !m_view3D; break;
                case BTN_LAYER_TOPCU:       m_showTopCu = !m_showTopCu; break;
                case BTN_LAYER_BOTCU:       m_showBotCu = !m_showBotCu; break;
                case BTN_LAYER_SILK:        m_showSilk = !m_showSilk; break;
                case BTN_LAYER_PADS:        m_showPads = !m_showPads; break;
                case BTN_TOGGLE_3D_PARTS:   m_show3DParts = !m_show3DParts; break;
                case BTN_SOLDER_MASK_COLOR: m_solderMaskPreset = (m_solderMaskPreset + 1) % 6; break;
                case BTN_AUTO_ROTATE_3D:    m_autoRotate3D = !m_autoRotate3D; m_lastAnimTick = GetTickCount64(); break;
                case BTN_TOGGLE_BOM:        m_showBomPanel = !m_showBomPanel; break;
                case BTN_FIT_RESET:
                    if (m_view3D) Reset3DCamera();
                    else Fit2DView();
                    break;
                }
                return true;
            }
        }

        if (m_showBomPanel && PathUtils::PtInRectF(x, y, m_bomRect)) {
            int idx = static_cast<int>((y - m_bomRect.top + m_bomScrollY) / 20.0f);
            if (idx >= 0 && idx < static_cast<int>(m_board.components.size())) {
                m_selectedCompIdx = idx;
            }
            return true;
        }
    }

    if (PathUtils::PtInRectF(x, y, m_canvasRect)) {
        m_lastMouseX = x;
        m_lastMouseY = y;
        if (rightButton) {
            m_draggingRight = true;
        } else {
            m_draggingLeft = true;
        }
        return true;
    }
    return false;
}

bool EdaViewer::OnMouseMove(float x, float y, WPARAM keyFlags) {
    if (m_draggingLeft || m_draggingRight) {
        float dx = x - m_lastMouseX;
        float dy = y - m_lastMouseY;
        m_lastMouseX = x;
        m_lastMouseY = y;
        if (m_view3D) {
            if (m_draggingRight || (keyFlags & MK_SHIFT)) {
                m_pan3DX += dx;
                m_pan3DY += dy;
            } else {
                m_yaw3D += dx * 0.01f;
                m_pitch3D = std::clamp(m_pitch3D + dy * 0.01f, 0.1f, 1.48f);
            }
        } else {
            m_pan2DX += dx;
            m_pan2DY += dy;
        }
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

bool EdaViewer::OnMouseUp() {
    if (m_draggingLeft || m_draggingRight) {
        m_draggingLeft = false;
        m_draggingRight = false;
        return true;
    }
    return false;
}

bool EdaViewer::OnMouseWheel(short delta, float x, float y) {
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    if (m_showBomPanel && PathUtils::PtInRectF(x, y, m_bomRect)) {
        float maxScroll = std::max(0.0f, static_cast<float>(m_board.components.size()) * 20.0f - (m_bomRect.bottom - m_bomRect.top));
        m_bomScrollY = std::clamp(m_bomScrollY - notches * 40.0f, 0.0f, maxScroll);
        return true;
    }

    if (PathUtils::PtInRectF(x, y, m_canvasRect)) {
        float factor = std::pow(1.18f, static_cast<float>(notches));
        if (m_view3D) {
            m_zoom3D = std::clamp(m_zoom3D * factor, 0.25f, 18.0f);
        } else {
            m_zoom2D = std::clamp(m_zoom2D * factor, 0.2f, 40.0f);
        }
        return true;
    }
    return false;
}

} // namespace Explorer2
