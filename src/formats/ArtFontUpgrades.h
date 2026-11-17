#pragma once
// ArtFontUpgrades.h — #10 Digital Art, Aseprite v1.3 Tilemaps, OpenType CFF/CFF2/Color Fonts & .cur Cursors
// Covers:
//   1. OpenRaster (.ora) stack.xml recursive multi-layer compositor (src-over/multiply/screen/plus)
//   2. Krita (.kra) maindoc.xml + VERSION 2 native tile store (NONE & LibLZF planar BGRA) compositor
//   3. Procreate (.procreate) Document.archive + <UUID>/<col>~<row>.chunk LZ4/Zlib RGBA tile compositor
//   4. Clip Studio Paint (.clip) CSFCHUNK walker (CHNKHead, CHNKSQLi, CHNKExta Zlib/raw raster blocks)
//   5. Aseprite v1.3 (.aseprite) Tileset (0x2023) & Compressed Tilemap Cel (celType == 3) compositor
//   6. OpenType CFF / CFF2 Type 2 CharString cubic-Bezier rasterizer + COLR/CPAL + CBDT/CBLC/sbix color fonts
//   7. Windows Static Cursor (.cur) hotspot + PNG / DIB XOR+AND mask decoder

namespace Formats {

namespace DetailArtFont {

inline std::string_view TrimAsciiView(std::string_view sv) { return ::Explorer2::PathUtils::TrimAscii(sv); }

inline std::string GetXmlAttr(std::string_view tag, std::string_view attrName) {
    std::string_view v;
    return ::Explorer2::XmlTagScan::FindAttr(tag, attrName, v, false) ? std::string(v) : std::string();
}

inline int ParseIntOr(const std::string& s, int defVal) {
    if (s.empty()) return defVal;
    char* endP = nullptr;
    long v = std::strtol(s.c_str(), &endP, 10);
    return (endP != s.c_str()) ? static_cast<int>(v) : defVal;
}

inline float ParseFloatOr(const std::string& s, float defVal) {
    if (s.empty()) return defVal;
    char* endP = nullptr;
    float v = std::strtof(s.c_str(), &endP);
    return (endP != s.c_str() && std::isfinite(v)) ? v : defVal;
}

enum class LayerBlendOp { SrcOver, Multiply, Screen, Plus, Lighten, Darken };

inline LayerBlendOp ParseBlendOp(std::string_view op) {
    if (op.find("multiply") != std::string_view::npos) return LayerBlendOp::Multiply;
    if (op.find("screen") != std::string_view::npos)   return LayerBlendOp::Screen;
    if (op.find("plus") != std::string_view::npos || op.find("add") != std::string_view::npos) return LayerBlendOp::Plus;
    if (op.find("lighten") != std::string_view::npos)  return LayerBlendOp::Lighten;
    if (op.find("darken") != std::string_view::npos)   return LayerBlendOp::Darken;
    return LayerBlendOp::SrcOver;
}

// Blend straight or premultiplied BGRA source pixel onto premultiplied BGRA canvas
inline void BlendPremulBgraPixel(
    uint8_t* dstBgra,
    uint8_t srcB_pm, uint8_t srcG_pm, uint8_t srcR_pm, uint8_t srcA,
    float opacity,
    LayerBlendOp op)
{
    if (srcA == 0 || opacity <= 0.0f) return;
    float sa = (static_cast<float>(srcA) / 255.0f) * std::clamp(opacity, 0.0f, 1.0f);
    if (sa <= 0.0f) return;

    float scaleSrc = std::clamp(opacity, 0.0f, 1.0f);
    float sr_pm = (static_cast<float>(srcR_pm) / 255.0f) * scaleSrc;
    float sg_pm = (static_cast<float>(srcG_pm) / 255.0f) * scaleSrc;
    float sb_pm = (static_cast<float>(srcB_pm) / 255.0f) * scaleSrc;

    float da = static_cast<float>(dstBgra[3]) / 255.0f;
    float dr_pm = static_cast<float>(dstBgra[2]) / 255.0f;
    float dg_pm = static_cast<float>(dstBgra[1]) / 255.0f;
    float db_pm = static_cast<float>(dstBgra[0]) / 255.0f;

    float outA = sa + da * (1.0f - sa);
    float outR_pm = 0.0f, outG_pm = 0.0f, outB_pm = 0.0f;

    if (op == LayerBlendOp::SrcOver || da <= 1e-5f) {
        outR_pm = sr_pm + dr_pm * (1.0f - sa);
        outG_pm = sg_pm + dg_pm * (1.0f - sa);
        outB_pm = sb_pm + db_pm * (1.0f - sa);
    } else if (op == LayerBlendOp::Plus) {
        outR_pm = std::min(1.0f, sr_pm + dr_pm);
        outG_pm = std::min(1.0f, sg_pm + dg_pm);
        outB_pm = std::min(1.0f, sb_pm + db_pm);
        outA = std::min(1.0f, sa + da);
    } else {
        float sr = std::clamp(sr_pm / sa, 0.0f, 1.0f);
        float sg = std::clamp(sg_pm / sa, 0.0f, 1.0f);
        float sb = std::clamp(sb_pm / sa, 0.0f, 1.0f);
        float dr = std::clamp(dr_pm / da, 0.0f, 1.0f);
        float dg = std::clamp(dg_pm / da, 0.0f, 1.0f);
        float db = std::clamp(db_pm / da, 0.0f, 1.0f);
        auto blendCh = [&](float s, float d) -> float {
            switch (op) {
                case LayerBlendOp::Multiply: return s * d;
                case LayerBlendOp::Screen:   return s + d - s * d;
                case LayerBlendOp::Lighten:  return std::max(s, d);
                case LayerBlendOp::Darken:   return std::min(s, d);
                default: return s;
            }
        };
        outR_pm = sr_pm * (1.0f - da) + dr_pm * (1.0f - sa) + sa * da * blendCh(sr, dr);
        outG_pm = sg_pm * (1.0f - da) + dg_pm * (1.0f - sa) + sa * da * blendCh(sg, dg);
        outB_pm = sb_pm * (1.0f - da) + db_pm * (1.0f - sa) + sa * da * blendCh(sb, db);
    }

    dstBgra[0] = static_cast<uint8_t>(std::clamp(outB_pm * 255.0f + 0.5f, 0.0f, 255.0f));
    dstBgra[1] = static_cast<uint8_t>(std::clamp(outG_pm * 255.0f + 0.5f, 0.0f, 255.0f));
    dstBgra[2] = static_cast<uint8_t>(std::clamp(outR_pm * 255.0f + 0.5f, 0.0f, 255.0f));
    dstBgra[3] = static_cast<uint8_t>(std::clamp(outA    * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline const ZipDeflate::ArchiveItemMeta* FindZipEntryCaseInsensitive(
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    std::wstring relPath)
{
    for (wchar_t& ch : relPath) if (ch == L'\\') ch = L'/';
    while (!relPath.empty() && relPath.front() == L'/') relPath.erase(relPath.begin());
    std::wstring targetLower = PathUtils::ToLower(relPath);
    for (const auto& e : entries) {
        std::wstring nl = PathUtils::ToLower(e.pathInArchive);
        for (wchar_t& ch : nl) if (ch == L'\\') ch = L'/';
        while (!nl.empty() && nl.front() == L'/') nl.erase(nl.begin());
        if (nl == targetLower) return &e;
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
// 1. OpenRaster (.ora) stack.xml Multi-Layer Compositor
// ----------------------------------------------------------------------------
struct OraLayerSpec {
    std::wstring name;
    std::wstring srcPath;
    int x = 0;
    int y = 0;
    float opacity = 1.0f;
    bool visible = true;
    LayerBlendOp op = LayerBlendOp::SrcOver;
};

inline bool CompositeOraFromStackXml(
    const uint8_t* zipData,
    size_t zipSize,
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    std::string_view xml,
    ExtendedImageInfo& outInfo)
{
    int imgW = 0, imgH = 0;
    std::vector<OraLayerSpec> layersTopToBottom;
    struct StackScope { int x = 0, y = 0; float opacity = 1.0f; bool visible = true; };
    std::vector<StackScope> scopeStack;
    scopeStack.push_back({});

    size_t pos = 0;
    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) break;
        if (lt + 3 < xml.size() && xml.substr(lt, 4) == "<!--") {
            size_t endC = xml.find("-->", lt + 4);
            if (endC == std::string_view::npos) break;
            pos = endC + 3;
            continue;
        }
        size_t gt = xml.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = TrimAsciiView(xml.substr(lt + 1, gt - lt - 1));
        pos = gt + 1;
        if (tag.empty() || tag.front() == '?' || tag.front() == '!') continue;

        if (tag.front() == '/') {
            std::string_view cname = TrimAsciiView(tag.substr(1));
            if (cname == "stack" && scopeStack.size() > 1) scopeStack.pop_back();
            continue;
        }
        bool selfClose = (!tag.empty() && tag.back() == '/');
        if (selfClose) tag = TrimAsciiView(tag.substr(0, tag.size() - 1));

        size_t sp = 0;
        while (sp < tag.size() && tag[sp] != ' ' && tag[sp] != '\t' && tag[sp] != '\r' && tag[sp] != '\n') ++sp;
        std::string_view elem = tag.substr(0, sp);
        std::string_view attrs = tag.substr(sp);

        if (elem == "image") {
            imgW = ParseIntOr(GetXmlAttr(attrs, "w"), 0);
            imgH = ParseIntOr(GetXmlAttr(attrs, "h"), 0);
        } else if (elem == "stack") {
            StackScope cur = scopeStack.back();
            cur.x += ParseIntOr(GetXmlAttr(attrs, "x"), 0);
            cur.y += ParseIntOr(GetXmlAttr(attrs, "y"), 0);
            cur.opacity *= std::clamp(ParseFloatOr(GetXmlAttr(attrs, "opacity"), 1.0f), 0.0f, 1.0f);
            std::string vis = GetXmlAttr(attrs, "visibility");
            if (vis == "hidden") cur.visible = false;
            if (!selfClose) scopeStack.push_back(cur);
        } else if (elem == "layer") {
            const StackScope& sc = scopeStack.back();
            OraLayerSpec ls;
            ls.name = PathUtils::Utf8ToWide(GetXmlAttr(attrs, "name"));
            ls.srcPath = PathUtils::Utf8ToWide(GetXmlAttr(attrs, "src"));
            ls.x = sc.x + ParseIntOr(GetXmlAttr(attrs, "x"), 0);
            ls.y = sc.y + ParseIntOr(GetXmlAttr(attrs, "y"), 0);
            ls.opacity = sc.opacity * std::clamp(ParseFloatOr(GetXmlAttr(attrs, "opacity"), 1.0f), 0.0f, 1.0f);
            std::string vis = GetXmlAttr(attrs, "visibility");
            ls.visible = sc.visible && (vis != "hidden");
            ls.op = ParseBlendOp(GetXmlAttr(attrs, "composite-op"));
            if (!ls.name.empty() && outInfo.layerNames.size() < 64) {
                outInfo.layerNames.push_back(ls.name);
            }
            if (!ls.srcPath.empty()) {
                layersTopToBottom.push_back(std::move(ls));
            }
        }
    }

    if (imgW <= 0 || imgH <= 0 || imgW > 8192 || imgH > 8192 || layersTopToBottom.empty()) {
        return false;
    }

    std::vector<uint8_t> canvas(static_cast<size_t>(imgW) * imgH * 4u, 0u);
    size_t compositedCount = 0;

    // OpenRaster stack.xml lists layers top-to-bottom; composite bottom-to-top
    for (auto it = layersTopToBottom.rbegin(); it != layersTopToBottom.rend(); ++it) {
        if (!it->visible || it->opacity <= 0.0f) continue;
        const auto* ent = FindZipEntryCaseInsensitive(entries, it->srcPath);
        if (!ent) continue;
        std::vector<uint8_t> layerBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, *ent, layerBytes) || layerBytes.empty()) continue;
        ExtendedImageInfo lImg;
        if (!DetailImg100::DecodeEmbeddedWicBuffer(layerBytes.data(), layerBytes.size(), L"ORA Layer", lImg) || !lImg.valid) {
            continue;
        }
        ++compositedCount;
        for (uint32_t ly = 0; ly < lImg.height; ++ly) {
            int dy = it->y + static_cast<int>(ly);
            if (dy < 0 || dy >= imgH) continue;
            for (uint32_t lx = 0; lx < lImg.width; ++lx) {
                int dx = it->x + static_cast<int>(lx);
                if (dx < 0 || dx >= imgW) continue;
                const uint8_t* sp = &lImg.bgraPixels[(static_cast<size_t>(ly) * lImg.width + lx) * 4u];
                BlendPremulBgraPixel(
                    &canvas[(static_cast<size_t>(dy) * imgW + dx) * 4u],
                    sp[0], sp[1], sp[2], sp[3], it->opacity, it->op);
            }
        }
    }

    if (compositedCount == 0) return false;

    outInfo.width = static_cast<uint32_t>(imgW);
    outInfo.height = static_cast<uint32_t>(imgH);
    outInfo.origWidth = outInfo.width;
    outInfo.origHeight = outInfo.height;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"OpenRaster Multi-Layer Document (.ora, " + std::to_wstring(layersTopToBottom.size()) + L" layers)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}

// ----------------------------------------------------------------------------
// 2. Krita (.kra) Native VERSION 2 Tile-Engine Decompressor & Layer Compositor
// ----------------------------------------------------------------------------
inline bool DecompressLibLzf(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap, size_t& outLen) {
    size_t sp = 0, dp = 0;
    while (sp < srcLen) {
        uint8_t ctrl = src[sp++];
        if (ctrl < 32u) {
            size_t lit = static_cast<size_t>(ctrl) + 1u;
            if (sp + lit > srcLen || dp + lit > dstCap) return false;
            std::memcpy(dst + dp, src + sp, lit);
            sp += lit;
            dp += lit;
        } else {
            size_t len = (static_cast<size_t>(ctrl) >> 5) + 2u;
            size_t refOff = (static_cast<size_t>(ctrl & 0x1Fu) << 8) + 1u;
            if (len == 9u) {
                if (sp >= srcLen) return false;
                len += src[sp++];
            }
            if (sp >= srcLen) return false;
            refOff += src[sp++];
            if (refOff > dp || dp + len > dstCap) return false;
            for (size_t k = 0; k < len; ++k) {
                dst[dp] = dst[dp - refOff];
                ++dp;
            }
        }
    }
    outLen = dp;
    return true;
}

inline bool DecodeKritaTileLayer(
    const uint8_t* tileStream,
    size_t streamLen,
    int layerOffX,
    int layerOffY,
    float opacity,
    LayerBlendOp blendOp,
    uint8_t* canvasBgra,
    int imgW,
    int imgH)
{
    if (!tileStream || streamLen < 32) return false;
    size_t pos = 0;
    auto readLine = [&](std::string_view& outLine) -> bool {
        if (pos >= streamLen) return false;
        size_t nl = pos;
        while (nl < streamLen && tileStream[nl] != '\n') ++nl;
        if (nl >= streamLen) return false;
        outLine = TrimAsciiView(std::string_view(reinterpret_cast<const char*>(tileStream + pos), nl - pos));
        pos = nl + 1;
        return true;
    };

    int version = 2, tileW = 64, tileH = 64, pixelSize = 4, numTiles = -1;
    for (int hdrIdx = 0; hdrIdx < 8; ++hdrIdx) {
        std::string_view ln;
        if (!readLine(ln)) return false;
        if (ln.rfind("VERSION ", 0) == 0)         version   = ParseIntOr(std::string(ln.substr(8)), 2);
        else if (ln.rfind("TILEWIDTH ", 0) == 0)  tileW     = ParseIntOr(std::string(ln.substr(10)), 64);
        else if (ln.rfind("TILEHEIGHT ", 0) == 0) tileH     = ParseIntOr(std::string(ln.substr(11)), 64);
        else if (ln.rfind("PIXELSIZE ", 0) == 0)  pixelSize = ParseIntOr(std::string(ln.substr(10)), 4);
        else if (ln.rfind("DATA ", 0) == 0) {
            numTiles = ParseIntOr(std::string(ln.substr(5)), -1);
            break;
        }
    }
    (void)version;
    if (numTiles < 0 || tileW <= 0 || tileW > 512 || tileH <= 0 || tileH > 512 ||
        (pixelSize != 4 && pixelSize != 2 && pixelSize != 1 && pixelSize != 8)) {
        return false;
    }

    const size_t tilePixels = static_cast<size_t>(tileW) * tileH;
    const size_t rawTileBytes = tilePixels * static_cast<size_t>(pixelSize);
    std::vector<uint8_t> decodedTile(rawTileBytes);
    std::vector<uint8_t> linearTile(rawTileBytes);
    size_t tilesBlitted = 0;

    for (int t = 0; t < numTiles && pos < streamLen; ++t) {
        std::string_view tHdr;
        if (!readLine(tHdr)) break;
        // Format: col,row,LZF,compLen  or  col,row,NONE,rawLen
        size_t c1 = tHdr.find(',');
        if (c1 == std::string_view::npos) return false;
        size_t c2 = tHdr.find(',', c1 + 1);
        if (c2 == std::string_view::npos) return false;
        size_t c3 = tHdr.find(',', c2 + 1);
        if (c3 == std::string_view::npos) return false;

        int tileCol = ParseIntOr(std::string(tHdr.substr(0, c1)), 0);
        int tileRow = ParseIntOr(std::string(tHdr.substr(c1 + 1, c2 - c1 - 1)), 0);
        std::string_view codec = tHdr.substr(c2 + 1, c3 - c2 - 1);
        int byteCount = ParseIntOr(std::string(tHdr.substr(c3 + 1)), -1);
        if (byteCount <= 0 || static_cast<size_t>(byteCount) > streamLen - pos) return false;

        const uint8_t* payload = tileStream + pos;
        pos += static_cast<size_t>(byteCount);

        const uint8_t* pxData = nullptr;
        if (codec == "NONE") {
            if (static_cast<size_t>(byteCount) < rawTileBytes) return false;
            pxData = payload;
        } else if (codec == "LZF") {
            uint8_t compFlag = payload[0];
            if (compFlag == 0u) {
                if (static_cast<size_t>(byteCount - 1) < rawTileBytes) return false;
                pxData = payload + 1;
            } else if (compFlag == 1u) {
                size_t outLen = 0;
                if (!DecompressLibLzf(payload + 1, static_cast<size_t>(byteCount - 1),
                                      decodedTile.data(), rawTileBytes, outLen) ||
                    outLen != rawTileBytes) {
                    return false;
                }
                // Krita KisTileCompressor2 delinearize: planar [ch0_all, ch1_all, ...] -> interleaved
                for (size_t pi = 0; pi < tilePixels; ++pi) {
                    for (int ch = 0; ch < pixelSize; ++ch) {
                        linearTile[pi * pixelSize + ch] = decodedTile[static_cast<size_t>(ch) * tilePixels + pi];
                    }
                }
                pxData = linearTile.data();
            } else {
                return false;
            }
        } else {
            return false;
        }

        // Krita's default 8-bit "RGBA" color space stores bytes in BGRA order in memory!
        int baseX = tileCol * tileW + layerOffX;
        int baseY = tileRow * tileH + layerOffY;
        for (int ty = 0; ty < tileH; ++ty) {
            int dy = baseY + ty;
            if (dy < 0 || dy >= imgH) continue;
            for (int tx = 0; tx < tileW; ++tx) {
                int dx = baseX + tx;
                if (dx < 0 || dx >= imgW) continue;
                const uint8_t* sp = pxData + (static_cast<size_t>(ty) * tileW + tx) * pixelSize;
                uint8_t b = 0, g = 0, r = 0, a = 255;
                if (pixelSize == 4) {
                    b = sp[0]; g = sp[1]; r = sp[2]; a = sp[3];
                } else if (pixelSize == 8) {
                    b = sp[1]; g = sp[3]; r = sp[5]; a = sp[7];
                } else if (pixelSize == 2) {
                    b = g = r = sp[0]; a = sp[1];
                } else {
                    b = g = r = sp[0]; a = 255;
                }
                uint8_t pm[4];
                WritePremulBGRA(pm, r, g, b, a);
                BlendPremulBgraPixel(
                    &canvasBgra[(static_cast<size_t>(dy) * imgW + dx) * 4u],
                    pm[0], pm[1], pm[2], pm[3], opacity, blendOp);
            }
        }
        ++tilesBlitted;
    }
    return tilesBlitted > 0 || numTiles == 0;
}

inline bool CompositeKritaFromMainDoc(
    const uint8_t* zipData,
    size_t zipSize,
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    std::string_view xml,
    ExtendedImageInfo& outInfo)
{
    int imgW = 0, imgH = 0;
    std::wstring docName;
    struct KraLayer {
        std::wstring name;
        std::wstring filename;
        int x = 0, y = 0;
        float opacity = 1.0f;
        bool visible = true;
        LayerBlendOp op = LayerBlendOp::SrcOver;
    };
    std::vector<KraLayer> layersTopToBottom;

    size_t pos = 0;
    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) break;
        size_t gt = xml.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = TrimAsciiView(xml.substr(lt + 1, gt - lt - 1));
        pos = gt + 1;
        if (tag.empty() || tag.front() == '/' || tag.front() == '?' || tag.front() == '!') continue;
        if (!tag.empty() && tag.back() == '/') tag = TrimAsciiView(tag.substr(0, tag.size() - 1));

        size_t sp = 0;
        while (sp < tag.size() && tag[sp] != ' ' && tag[sp] != '\t' && tag[sp] != '\r' && tag[sp] != '\n') ++sp;
        std::string_view elem = tag.substr(0, sp);
        std::string_view attrs = tag.substr(sp);

        if (elem == "IMAGE") {
            imgW = ParseIntOr(GetXmlAttr(attrs, "width"), 0);
            imgH = ParseIntOr(GetXmlAttr(attrs, "height"), 0);
            docName = PathUtils::Utf8ToWide(GetXmlAttr(attrs, "name"));
        } else if (elem == "layer") {
            std::string nodeType = GetXmlAttr(attrs, "nodetype");
            std::string fn = GetXmlAttr(attrs, "filename");
            std::wstring nm = PathUtils::Utf8ToWide(GetXmlAttr(attrs, "name"));
            if (!nm.empty() && outInfo.layerNames.size() < 64) outInfo.layerNames.push_back(nm);
            if (!fn.empty() && (nodeType.empty() || nodeType == "paintlayer")) {
                KraLayer kl;
                kl.name = nm;
                kl.filename = PathUtils::Utf8ToWide(fn);
                kl.x = ParseIntOr(GetXmlAttr(attrs, "x"), 0);
                kl.y = ParseIntOr(GetXmlAttr(attrs, "y"), 0);
                kl.opacity = std::clamp(ParseFloatOr(GetXmlAttr(attrs, "opacity"), 255.0f) / 255.0f, 0.0f, 1.0f);
                kl.visible = (ParseIntOr(GetXmlAttr(attrs, "visible"), 1) != 0);
                kl.op = ParseBlendOp(GetXmlAttr(attrs, "compositeop"));
                layersTopToBottom.push_back(std::move(kl));
            }
        }
    }

    if (imgW <= 0 || imgH <= 0 || imgW > 8192 || imgH > 8192 || layersTopToBottom.empty()) {
        return false;
    }

    std::vector<uint8_t> canvas(static_cast<size_t>(imgW) * imgH * 4u, 0u);
    size_t decodedCount = 0;

    // Krita maindoc.xml lists layers top-to-bottom; composite bottom-to-top
    for (auto it = layersTopToBottom.rbegin(); it != layersTopToBottom.rend(); ++it) {
        if (!it->visible || it->opacity <= 0.0f) continue;
        const ZipDeflate::ArchiveItemMeta* tileEnt = nullptr;
        if (!docName.empty()) {
            tileEnt = FindZipEntryCaseInsensitive(entries, docName + L"/layers/" + it->filename);
        }
        if (!tileEnt) tileEnt = FindZipEntryCaseInsensitive(entries, L"layers/" + it->filename);
        if (!tileEnt) {
            std::wstring suffix = L"/layers/" + PathUtils::ToLower(it->filename);
            for (const auto& e : entries) {
                std::wstring nl = PathUtils::ToLower(e.pathInArchive);
                for (wchar_t& ch : nl) if (ch == L'\\') ch = L'/';
                if (nl.size() >= suffix.size() && nl.substr(nl.size() - suffix.size()) == suffix) {
                    tileEnt = &e;
                    break;
                }
            }
        }
        if (!tileEnt) continue;
        std::vector<uint8_t> tileBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, *tileEnt, tileBytes) || tileBytes.empty()) continue;
        if (DecodeKritaTileLayer(tileBytes.data(), tileBytes.size(), it->x, it->y, it->opacity, it->op,
                                 canvas.data(), imgW, imgH)) {
            ++decodedCount;
        }
    }

    if (decodedCount == 0) return false;

    outInfo.width = static_cast<uint32_t>(imgW);
    outInfo.height = static_cast<uint32_t>(imgH);
    outInfo.origWidth = outInfo.width;
    outInfo.origHeight = outInfo.height;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Krita Tile-Composited Document (.kra, " + std::to_wstring(layersTopToBottom.size()) + L" layers)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}

// ----------------------------------------------------------------------------
// 3. Procreate (.procreate) Native .chunk LZ4 / Zlib RGBA Tile Compositor
// ----------------------------------------------------------------------------
inline bool DecompressRawLz4BlockSimple(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t expectedMax) {
    out.clear();
    out.reserve(expectedMax);
    size_t p = 0;
    while (p < srcLen) {
        uint8_t token = src[p++];
        size_t litLen = token >> 4;
        if (litLen == 15) {
            uint8_t s = 0;
            do {
                if (p >= srcLen) return false;
                s = src[p++];
                litLen += s;
                if (litLen > expectedMax) return false;
            } while (s == 255);
        }
        if (p + litLen > srcLen || out.size() + litLen > expectedMax) return false;
        out.insert(out.end(), src + p, src + p + litLen);
        p += litLen;
        if (p == srcLen) break;
        if (p + 2 > srcLen) return false;
        uint16_t off = DetailImg100::RdU16LE(src + p);
        p += 2;
        if (off == 0 || off > out.size()) return false;
        size_t matchLen = (token & 0x0Fu) + 4u;
        if ((token & 0x0Fu) == 15u) {
            uint8_t s = 0;
            do {
                if (p >= srcLen) return false;
                s = src[p++];
                matchLen += s;
                if (matchLen > expectedMax) return false;
            } while (s == 255);
        }
        if (out.size() + matchLen > expectedMax) return false;
        for (size_t k = 0; k < matchLen; ++k) {
            out.push_back(out[out.size() - off]);
        }
    }
    return !out.empty();
}

inline bool CompositeProcreateChunks(
    const uint8_t* zipData,
    size_t zipSize,
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    ExtendedImageInfo& outInfo)
{
    // Scan Document.archive (ASCII or bplist00 strings) for "{W, H}" canvas size & tileSize if present
    int imgW = 0, imgH = 0, tileSize = 0;
    for (const auto& e : entries) {
        std::wstring nl = PathUtils::ToLower(e.pathInArchive);
        if (nl == L"document.archive") {
            std::vector<uint8_t> docBytes;
            if (ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, e, docBytes) && docBytes.size() >= 8) {
                std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
                // Find "{W, H}" pattern used by NSStringFromCGSize in Procreate SilicaDocument
                size_t brace = 0;
                while ((brace = sv.find('{', brace)) != std::string_view::npos) {
                    size_t endB = sv.find('}', brace + 1);
                    if (endB != std::string_view::npos && endB - brace < 32) {
                        std::string_view inner = sv.substr(brace + 1, endB - brace - 1);
                        size_t comma = inner.find(',');
                        if (comma != std::string_view::npos) {
                            int wCand = ParseIntOr(std::string(TrimAsciiView(inner.substr(0, comma))), 0);
                            int hCand = ParseIntOr(std::string(TrimAsciiView(inner.substr(comma + 1))), 0);
                            if (wCand > 0 && wCand <= 8192 && hCand > 0 && hCand <= 8192) {
                                imgW = wCand;
                                imgH = hCand;
                                break;
                            }
                        }
                    }
                    ++brace;
                }
            }
        }
    }

    struct ChunkRef {
        std::wstring layerId;
        int col = 0;
        int row = 0;
        const ZipDeflate::ArchiveItemMeta* meta = nullptr;
    };
    std::vector<ChunkRef> chunks;
    std::vector<std::wstring> layerOrder;
    int maxCol = 0, maxRow = 0;

    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        std::wstring p = e.pathInArchive;
        for (wchar_t& ch : p) if (ch == L'\\') ch = L'/';
        std::wstring pl = PathUtils::ToLower(p);
        if (pl.size() <= 6 || pl.substr(pl.size() - 6) != L".chunk") continue;
        size_t slash = p.find(L'/');
        if (slash == std::wstring::npos) continue;
        std::wstring layerId = p.substr(0, slash);
        if (PathUtils::ToLower(layerId).find(L"composite") != std::wstring::npos) {
            // Composite layer can still be used, keep order
        }
        std::wstring leaf = p.substr(slash + 1, p.size() - slash - 1 - 6);
        size_t tilde = leaf.find(L'~');
        if (tilde == std::wstring::npos) continue;
        int col = _wtoi(leaf.substr(0, tilde).c_str());
        int row = _wtoi(leaf.substr(tilde + 1).c_str());
        if (col < 0 || col > 128 || row < 0 || row > 128) continue;
        maxCol = std::max(maxCol, col);
        maxRow = std::max(maxRow, row);
        if (std::find(layerOrder.begin(), layerOrder.end(), layerId) == layerOrder.end()) {
            layerOrder.push_back(layerId);
        }
        chunks.push_back({ layerId, col, row, &e });
    }
    if (chunks.empty()) return false;

    // Decode first chunk to determine tileSize (e.g. 256, 128, 64, 16, 8)
    auto unpackProcreateTile = [&](const ZipDeflate::ArchiveItemMeta& m, std::vector<uint8_t>& outRgba) -> int {
        std::vector<uint8_t> raw;
        if (!ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, m, raw) || raw.empty()) return 0;
        if (!StreamCodecs::DecompressLz4Frame(raw.data(), raw.size(), outRgba, 4 * 1024 * 1024)) {
            if (!ZipDeflate::InflateZlib(raw.data(), raw.size(), outRgba, 4 * 1024 * 1024)) {
                if (!DecompressRawLz4BlockSimple(raw.data(), raw.size(), outRgba, 4 * 1024 * 1024)) {
                    outRgba = std::move(raw);
                }
            }
        }
        if ((outRgba.size() & 3u) != 0u || outRgba.size() < 16u) return 0;
        size_t px = outRgba.size() / 4u;
        int side = static_cast<int>(std::llround(std::sqrt(static_cast<double>(px))));
        return (side > 0 && static_cast<size_t>(side) * side == px) ? side : 0;
    };

    std::vector<uint8_t> probeRgba;
    tileSize = unpackProcreateTile(*chunks[0].meta, probeRgba);
    if (tileSize <= 0 || tileSize > 1024) return false;

    if (imgW <= 0 || imgH <= 0) {
        imgW = (maxCol + 1) * tileSize;
        imgH = (maxRow + 1) * tileSize;
    }
    if (imgW <= 0 || imgH <= 0 || imgW > 8192 || imgH > 8192) return false;

    std::vector<uint8_t> canvas(static_cast<size_t>(imgW) * imgH * 4u, 0u);
    size_t blitted = 0;

    for (const auto& lid : layerOrder) {
        if (outInfo.layerNames.size() < 64) outInfo.layerNames.push_back(lid);
        for (const auto& ck : chunks) {
            if (ck.layerId != lid) continue;
            std::vector<uint8_t> tileRgba;
            int ts = unpackProcreateTile(*ck.meta, tileRgba);
            if (ts != tileSize) continue;
            ++blitted;
            int baseX = ck.col * tileSize;
            int baseY = ck.row * tileSize;
            for (int ty = 0; ty < tileSize; ++ty) {
                int dy = baseY + ty;
                if (dy < 0 || dy >= imgH) continue;
                for (int tx = 0; tx < tileSize; ++tx) {
                    int dx = baseX + tx;
                    if (dx < 0 || dx >= imgW) continue;
                    const uint8_t* sp = &tileRgba[(static_cast<size_t>(ty) * tileSize + tx) * 4u];
                    uint8_t pm[4];
                    WritePremulBGRA(pm, sp[0], sp[1], sp[2], sp[3]);
                    BlendPremulBgraPixel(
                        &canvas[(static_cast<size_t>(dy) * imgW + dx) * 4u],
                        pm[0], pm[1], pm[2], pm[3], 1.0f, LayerBlendOp::SrcOver);
                }
            }
        }
    }
    if (blitted == 0) return false;

    outInfo.width = static_cast<uint32_t>(imgW);
    outInfo.height = static_cast<uint32_t>(imgH);
    outInfo.origWidth = outInfo.width;
    outInfo.origHeight = outInfo.height;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Procreate Silica Tile Artwork (.procreate, " + std::to_wstring(layerOrder.size()) + L" layers)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}

// ----------------------------------------------------------------------------
// 4. Clip Studio Paint (.clip) CSFCHUNK Chunk Walker + CHNKExta Tile Compositor
// ----------------------------------------------------------------------------
inline bool DecodeClipStudioUpgraded(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 24 || std::memcmp(data, "CSFCHUNK", 8) != 0) return false;

    struct ExtaBlock {
        const uint8_t* payload = nullptr;
        size_t len = 0;
    };
    std::vector<ExtaBlock> extaBlocks;
    uint32_t canvasW = 0, canvasH = 0;

    // Walk CSFCHUNK big-endian chunk chain (header is 24 bytes: "CSFCHUNK" + 8-byte fileSize + 8-byte firstOffset)
    size_t pos = 24;
    while (pos + 16 <= size) {
        if (std::memcmp(data + pos, "CHNK", 4) != 0) break;
        char subType[5] = {
            static_cast<char>(data[pos + 4]), static_cast<char>(data[pos + 5]),
            static_cast<char>(data[pos + 6]), static_cast<char>(data[pos + 7]), 0
        };
        uint64_t ckLen = DetailImg100::RdU64BE(data + pos + 8);
        pos += 16;
        if (ckLen > size - pos) break;
        const uint8_t* ckData = data + pos;
        size_t len = static_cast<size_t>(ckLen);

        if (std::strcmp(subType, "Head") == 0 && len >= 16) {
            uint32_t cw = DetailImg100::RdU32BE(ckData + 4);
            uint32_t ch = DetailImg100::RdU32BE(ckData + 8);
            if (cw > 0 && cw <= 8192 && ch > 0 && ch <= 8192) {
                canvasW = cw;
                canvasH = ch;
            }
        } else if (std::strcmp(subType, "Exta") == 0 && len >= 12) {
            extaBlocks.push_back({ ckData, len });
        } else if (std::strcmp(subType, "Foot") == 0) {
            break;
        }
        pos += len;
    }

    // 1. If CHNKExta external raster blocks exist and encode raw/zlib BGRA/RGBA canvas or layer tiles, composite them!
    if (!extaBlocks.empty()) {
        uint32_t w = (canvasW > 0) ? canvasW : 0;
        uint32_t h = (canvasH > 0) ? canvasH : 0;
        std::vector<uint8_t> canvas;
        size_t appliedLayers = 0;

        for (const auto& eb : extaBlocks) {
            // Support CHNKExta external raster payload:
            // Either [uint32BE width, uint32BE height, zlib/raw RGBA] or [nameLenBE32 + name + uint32BE w, uint32BE h + zlib]
            size_t p = 0;
            if (eb.len >= 16) {
                uint32_t nLen = DetailImg100::RdU32BE(eb.payload);
                if (nLen > 0 && nLen <= 128 && 4u + nLen + 8u <= eb.len) {
                    bool allPrint = true;
                    for (uint32_t k = 0; k < nLen; ++k) {
                        if (eb.payload[4 + k] < 0x20 || eb.payload[4 + k] > 0x7E) { allPrint = false; break; }
                    }
                    if (allPrint) {
                        std::string extId(reinterpret_cast<const char*>(eb.payload + 4), nLen);
                        if (outInfo.layerNames.size() < 64) outInfo.layerNames.push_back(PathUtils::Utf8ToWide(extId));
                        p = 4u + nLen;
                    }
                }
            }
            if (p + 8u <= eb.len) {
                uint32_t bw = DetailImg100::RdU32BE(eb.payload + p);
                uint32_t bh = DetailImg100::RdU32BE(eb.payload + p + 4);
                if (bw > 0 && bw <= 4096 && bh > 0 && bh <= 4096) {
                    std::vector<uint8_t> rawPx;
                    const uint8_t* src = eb.payload + p + 8u;
                    size_t rem = eb.len - (p + 8u);
                    size_t need = static_cast<size_t>(bw) * bh * 4u;
                    if (!ZipDeflate::InflateZlib(src, rem, rawPx, need + 64) && rem >= need) {
                        rawPx.assign(src, src + need);
                    }
                    if (rawPx.size() >= need) {
                        if (w == 0 || h == 0) { w = bw; h = bh; }
                        if (canvas.empty()) canvas.assign(static_cast<size_t>(w) * h * 4u, 0u);
                        uint32_t limY = std::min(h, bh), limX = std::min(w, bw);
                        for (uint32_t y = 0; y < limY; ++y) {
                            for (uint32_t x = 0; x < limX; ++x) {
                                const uint8_t* sp = &rawPx[(static_cast<size_t>(y) * bw + x) * 4u];
                                uint8_t pm[4];
                                WritePremulBGRA(pm, sp[0], sp[1], sp[2], sp[3]);
                                BlendPremulBgraPixel(
                                    &canvas[(static_cast<size_t>(y) * w + x) * 4u],
                                    pm[0], pm[1], pm[2], pm[3], 1.0f, LayerBlendOp::SrcOver);
                            }
                        }
                        ++appliedLayers;
                    }
                }
            }
        }
        if (appliedLayers > 0 && w > 0 && h > 0) {
            outInfo.width = w;
            outInfo.height = h;
            outInfo.origWidth = w;
            outInfo.origHeight = h;
            outInfo.channels = 4u;
            outInfo.bitsPerChannel = 8u;
            outInfo.formatName = L"Clip Studio Paint Layered Canvas (.clip)";
            outInfo.bgraPixels = std::move(canvas);
            outInfo.valid = true;
            return true;
        }
    }

    // 2. Fallback: scan SQLite CHNKSQLi / embedded CanvasPreview PNG
    static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    static const uint8_t kIend[8]   = { 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
    for (size_t i = 8; i + 32 < size; ++i) {
        if (std::memcmp(data + i, kPngSig, 8) == 0) {
            size_t pngEnd = size - i;
            for (size_t j = i + 16; j + 8 <= size; ++j) {
                if (std::memcmp(data + j, kIend, 8) == 0) {
                    pngEnd = (j + 8) - i;
                    break;
                }
            }
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + i, pngEnd, L"Clip Studio Paint Canvas (.clip)", outInfo)) {
                return true;
            }
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// 5. Aseprite v1.3 (.aseprite) with 0x2023 Tileset & celType == 3 Tilemap Cel
// ----------------------------------------------------------------------------
inline bool DecodeAsepriteUpgraded(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 128 || DetailImg100::RdU16LE(data + 4) != 0xA5E0u) return false;
    uint16_t frames = DetailImg100::RdU16LE(data + 6);
    uint16_t w      = DetailImg100::RdU16LE(data + 8);
    uint16_t h      = DetailImg100::RdU16LE(data + 10);
    uint16_t depth  = DetailImg100::RdU16LE(data + 12);
    uint8_t  transIdx = data[28];
    if (frames == 0 || w == 0 || h == 0 || w > 8192 || h > 8192 || (depth != 32 && depth != 16 && depth != 8)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Aseprite Sprite (" + std::to_wstring(frames) + L" frames, " + std::to_wstring(depth) + L"-bit)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4u, 0u);

    uint32_t palette[256] = {};
    for (int i = 0; i < 256; ++i) {
        uint8_t c = static_cast<uint8_t>(i);
        uint8_t a = (depth == 8 && i == transIdx) ? 0u : 255u;
        palette[i] = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(c) << 8) | c;
    }

    struct AseLayer {
        uint16_t flags = 1;
        uint16_t layerType = 0; // 0 = normal, 1 = group, 2 = tilemap
        uint16_t blendMode = 0;
        uint8_t  opacity = 255;
        uint32_t tilesetIndex = 0;
    };
    struct AseTileset {
        uint32_t id = 0;
        uint32_t flags = 0;
        uint32_t numTiles = 0;
        uint16_t tileW = 0;
        uint16_t tileH = 0;
        std::vector<uint8_t> pixels; // tileW * (tileH * numTiles) * (depth / 8)
    };
    std::vector<AseLayer> layers;
    std::map<uint32_t, AseTileset> tilesets;

    auto samplePixelToRgba = [&](const uint8_t* sp, bool isBackgroundLayer, uint8_t& sr, uint8_t& sg, uint8_t& sb, uint8_t& sa) {
        if (depth == 32) {
            sr = sp[0]; sg = sp[1]; sb = sp[2]; sa = sp[3];
        } else if (depth == 16) {
            sr = sg = sb = sp[0]; sa = sp[1];
        } else {
            uint8_t idx = sp[0];
            if (!isBackgroundLayer && idx == transIdx) {
                sr = sg = sb = sa = 0u;
            } else {
                uint32_t c32 = palette[idx];
                sb = static_cast<uint8_t>(c32 & 0xFFu);
                sg = static_cast<uint8_t>((c32 >> 8) & 0xFFu);
                sr = static_cast<uint8_t>((c32 >> 16) & 0xFFu);
                sa = isBackgroundLayer ? 255u : static_cast<uint8_t>((c32 >> 24) & 0xFFu);
            }
        }
    };

    const uint32_t bpp = depth / 8u;
    size_t pos = 128;
    bool decodedAnyCel = false;

    for (uint16_t f = 0; f < std::min<uint16_t>(frames, 4) && pos + 16 <= size; ++f) {
        uint32_t frameBytes = DetailImg100::RdU32LE(data + pos);
        uint16_t frameMagic = DetailImg100::RdU16LE(data + pos + 4);
        if (frameMagic != 0xF1FAu || frameBytes < 16 || pos + frameBytes > size) break;
        uint32_t numChunks = DetailImg100::RdU32LE(data + pos + 12);
        if (numChunks == 0) numChunks = DetailImg100::RdU16LE(data + pos + 6);

        size_t cpos = pos + 16;
        size_t frameEnd = pos + frameBytes;
        for (uint32_t c = 0; c < numChunks && cpos + 6 <= frameEnd; ++c) {
            uint32_t chunkSize = DetailImg100::RdU32LE(data + cpos);
            uint16_t chunkType = DetailImg100::RdU16LE(data + cpos + 4);
            if (chunkSize < 6 || cpos + chunkSize > frameEnd) break;
            const uint8_t* cdata = data + cpos + 6;
            size_t clen = chunkSize - 6;

            if (chunkType == 0x2004u && clen >= 18) { // Layer chunk
                AseLayer ly;
                ly.flags     = DetailImg100::RdU16LE(cdata + 0);
                ly.layerType = DetailImg100::RdU16LE(cdata + 2);
                ly.blendMode = DetailImg100::RdU16LE(cdata + 10);
                ly.opacity   = cdata[12];
                uint16_t nameLen = DetailImg100::RdU16LE(cdata + 16);
                if (18u + nameLen <= clen) {
                    std::string lname(reinterpret_cast<const char*>(cdata + 18), nameLen);
                    outInfo.layerNames.push_back(PathUtils::Utf8ToWide(lname));
                    if (ly.layerType == 2u && 18u + nameLen + 4u <= clen) {
                        ly.tilesetIndex = DetailImg100::RdU32LE(cdata + 18 + nameLen);
                    }
                }
                layers.push_back(ly);
            } else if (chunkType == 0x2019u && clen >= 20) { // Palette chunk
                uint32_t firstIdx = DetailImg100::RdU32LE(cdata + 4);
                uint32_t lastIdx  = DetailImg100::RdU32LE(cdata + 8);
                size_t pOff = 20;
                for (uint32_t pi = firstIdx; pi <= lastIdx && pi < 256 && pOff + 6 <= clen; ++pi) {
                    uint16_t flags = DetailImg100::RdU16LE(cdata + pOff);
                    uint8_t pr = cdata[pOff + 2], pg = cdata[pOff + 3], pb = cdata[pOff + 4], pa = cdata[pOff + 5];
                    palette[pi] = (static_cast<uint32_t>(pa) << 24) | (static_cast<uint32_t>(pr) << 16) |
                                  (static_cast<uint32_t>(pg) << 8) | pb;
                    pOff += 6;
                    if ((flags & 1u) != 0 && pOff + 2 <= clen) {
                        uint16_t nlen = DetailImg100::RdU16LE(cdata + pOff);
                        pOff += 2 + nlen;
                    }
                }
            } else if (chunkType == 0x2023u && clen >= 34) { // Aseprite v1.3 Tileset chunk
                AseTileset ts;
                ts.id       = DetailImg100::RdU32LE(cdata + 0);
                ts.flags    = DetailImg100::RdU32LE(cdata + 4);
                ts.numTiles = DetailImg100::RdU32LE(cdata + 8);
                ts.tileW    = DetailImg100::RdU16LE(cdata + 12);
                ts.tileH    = DetailImg100::RdU16LE(cdata + 14);
                uint16_t nameLen = DetailImg100::RdU16LE(cdata + 32);
                size_t pOff = 34u + nameLen;
                if ((ts.flags & 0x01u) != 0u) pOff += 8u; // external file ID + tileset ID
                if ((ts.flags & 0x02u) != 0u && ts.numTiles > 0 && ts.tileW > 0 && ts.tileH > 0 &&
                    ts.tileW <= 1024 && ts.tileH <= 1024 && ts.numTiles <= 65536 && pOff + 4u <= clen)
                {
                    uint32_t compLen = DetailImg100::RdU32LE(cdata + pOff);
                    pOff += 4u;
                    size_t expectedBytes = static_cast<size_t>(ts.tileW) * ts.tileH * ts.numTiles * bpp;
                    if (compLen <= clen - pOff && expectedBytes <= 64ULL * 1024 * 1024 &&
                        ZipDeflate::InflateZlib(cdata + pOff, compLen, ts.pixels, expectedBytes + 64) &&
                        ts.pixels.size() >= expectedBytes)
                    {
                        tilesets[ts.id] = std::move(ts);
                    }
                }
            } else if (chunkType == 0x2005u && f == 0 && clen >= 16) { // Cel chunk (Composite frame 0)
                uint16_t layerIdx = DetailImg100::RdU16LE(cdata + 0);
                int16_t celX      = static_cast<int16_t>(DetailImg100::RdU16LE(cdata + 2));
                int16_t celY      = static_cast<int16_t>(DetailImg100::RdU16LE(cdata + 4));
                uint8_t celOpac   = cdata[6];
                uint16_t celType  = DetailImg100::RdU16LE(cdata + 7);

                bool isVisible = true;
                bool isBgLayer = false;
                uint8_t layerOpac = 255;
                uint32_t tsId = 0;
                if (layerIdx < layers.size()) {
                    isVisible = (layers[layerIdx].flags & 0x01u) != 0u;
                    isBgLayer = (layers[layerIdx].flags & 0x08u) != 0u;
                    if (layers[layerIdx].opacity > 0) layerOpac = layers[layerIdx].opacity;
                    tsId = layers[layerIdx].tilesetIndex;
                }
                if (!isVisible) { cpos += chunkSize; continue; }
                float effOpacity = (static_cast<float>(celOpac) / 255.0f) * (static_cast<float>(layerOpac) / 255.0f);

                if ((celType == 0u || celType == 2u) && clen >= 20) {
                    uint16_t cw = DetailImg100::RdU16LE(cdata + 16);
                    uint16_t ch = DetailImg100::RdU16LE(cdata + 18);
                    std::vector<uint8_t> rawCel;
                    const uint8_t* pixPtr = nullptr;
                    size_t pixLen = 0;
                    if (celType == 0u) {
                        pixPtr = cdata + 20;
                        pixLen = clen - 20;
                    } else if (celType == 2u && clen > 22) {
                        if (ZipDeflate::InflateZlib(cdata + 20, clen - 20, rawCel)) {
                            pixPtr = rawCel.data();
                            pixLen = rawCel.size();
                        }
                    }
                    if (pixPtr && pixLen >= static_cast<size_t>(cw) * ch * bpp) {
                        decodedAnyCel = true;
                        for (uint16_t py = 0; py < ch; ++py) {
                            int dy = celY + py;
                            if (dy < 0 || dy >= h) continue;
                            for (uint16_t px = 0; px < cw; ++px) {
                                int dx = celX + px;
                                if (dx < 0 || dx >= w) continue;
                                const uint8_t* sp = pixPtr + (static_cast<size_t>(py) * cw + px) * bpp;
                                uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
                                samplePixelToRgba(sp, isBgLayer, sr, sg, sb, sa);
                                uint8_t pm[4];
                                WritePremulBGRA(pm, sr, sg, sb, sa);
                                BlendPremulBgraPixel(
                                    &outInfo.bgraPixels[(static_cast<size_t>(dy) * w + dx) * 4u],
                                    pm[0], pm[1], pm[2], pm[3], effOpacity, LayerBlendOp::SrcOver);
                            }
                        }
                    }
                } else if (celType == 3u && clen >= 48) { // Compressed Tilemap Cel (Aseprite v1.3+)
                    auto tsIt = tilesets.find(tsId);
                    if (tsIt == tilesets.end() && !tilesets.empty()) tsIt = tilesets.begin();
                    if (tsIt != tilesets.end()) {
                        const AseTileset& ts = tsIt->second;
                        uint16_t mapW        = DetailImg100::RdU16LE(cdata + 16);
                        uint16_t mapH        = DetailImg100::RdU16LE(cdata + 18);
                        uint16_t bitsPerTile = DetailImg100::RdU16LE(cdata + 20);
                        uint32_t tileIdMask  = DetailImg100::RdU32LE(cdata + 22);
                        uint32_t xFlipMask   = DetailImg100::RdU32LE(cdata + 26);
                        uint32_t yFlipMask   = DetailImg100::RdU32LE(cdata + 30);
                        uint32_t diagMask    = DetailImg100::RdU32LE(cdata + 34);
                        uint32_t bytesPerTile = (bitsPerTile == 8) ? 1u : (bitsPerTile == 16) ? 2u : 4u;
                        size_t needMapBytes = static_cast<size_t>(mapW) * mapH * bytesPerTile;

                        std::vector<uint8_t> mapData;
                        if (mapW > 0 && mapH > 0 && clen > 48 &&
                            ZipDeflate::InflateZlib(cdata + 48, clen - 48, mapData, needMapBytes + 64) &&
                            mapData.size() >= needMapBytes)
                        {
                            decodedAnyCel = true;
                            bool tileZeroIsEmpty = (ts.flags & 0x04u) == 0u;
                            size_t singleTileBytes = static_cast<size_t>(ts.tileW) * ts.tileH * bpp;

                            for (uint16_t my = 0; my < mapH; ++my) {
                                for (uint16_t mx = 0; mx < mapW; ++mx) {
                                    const uint8_t* mp = mapData.data() + (static_cast<size_t>(my) * mapW + mx) * bytesPerTile;
                                    uint32_t rawTile = (bytesPerTile == 1u) ? mp[0] :
                                                       (bytesPerTile == 2u) ? DetailImg100::RdU16LE(mp) :
                                                       DetailImg100::RdU32LE(mp);
                                    uint32_t tileIdx = (tileIdMask != 0u) ? (rawTile & tileIdMask) : rawTile;
                                    if (tileZeroIsEmpty) {
                                        if (tileIdx == 0u) continue;
                                        tileIdx -= 1u;
                                    }
                                    if (tileIdx >= ts.numTiles) continue;

                                    bool flipX = (xFlipMask != 0u) && ((rawTile & xFlipMask) != 0u);
                                    bool flipY = (yFlipMask != 0u) && ((rawTile & yFlipMask) != 0u);
                                    bool flipD = (diagMask  != 0u) && ((rawTile & diagMask)  != 0u);

                                    const uint8_t* tileBase = ts.pixels.data() + static_cast<size_t>(tileIdx) * singleTileBytes;
                                    int baseX = celX + static_cast<int>(mx) * ts.tileW;
                                    int baseY = celY + static_cast<int>(my) * ts.tileH;

                                    for (uint16_t ty = 0; ty < ts.tileH; ++ty) {
                                        int dy = baseY + ty;
                                        if (dy < 0 || dy >= h) continue;
                                        for (uint16_t tx = 0; tx < ts.tileW; ++tx) {
                                            int dx = baseX + tx;
                                            if (dx < 0 || dx >= w) continue;
                                            uint16_t sx = tx, sy = ty;
                                            if (flipD && ts.tileW == ts.tileH) std::swap(sx, sy);
                                            if (flipX) sx = static_cast<uint16_t>(ts.tileW - 1u - sx);
                                            if (flipY) sy = static_cast<uint16_t>(ts.tileH - 1u - sy);

                                            const uint8_t* sp = tileBase + (static_cast<size_t>(sy) * ts.tileW + sx) * bpp;
                                            uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
                                            samplePixelToRgba(sp, isBgLayer, sr, sg, sb, sa);
                                            uint8_t pm[4];
                                            WritePremulBGRA(pm, sr, sg, sb, sa);
                                            BlendPremulBgraPixel(
                                                &outInfo.bgraPixels[(static_cast<size_t>(dy) * w + dx) * 4u],
                                                pm[0], pm[1], pm[2], pm[3], effOpacity, LayerBlendOp::SrcOver);
                                        }
                                    }
                                }
                            }
                            outInfo.formatName = L"Aseprite v1.3 Tilemap Sprite (" + std::to_wstring(ts.numTiles) + L" tiles)";
                        }
                    }
                } else if (celType == 1u && clen >= 18) { // Linked Cel (reuses earlier frame cel; mark valid if non-empty)
                    decodedAnyCel = decodedAnyCel || false;
                }
            }
            cpos += chunkSize;
        }
        pos += frameBytes;
    }
    if (!decodedAnyCel) return false;
    outInfo.valid = true;
    return true;
}

// ----------------------------------------------------------------------------
// 6. OpenType CFF / CFF2 Type 2 CharString Rasterizer + COLR/CPAL + CBDT/sbix
// ----------------------------------------------------------------------------
struct CffIndexView {
    const uint8_t* base = nullptr;
    size_t tableRem = 0;
    uint32_t count = 0;
    uint8_t offSize = 0;
    size_t offsetsPos = 0;
    size_t dataBase = 0;
    size_t nextPos = 0;
    bool valid = false;

    uint32_t ReadOff(uint32_t idx) const {
        const uint8_t* p = base + offsetsPos + static_cast<size_t>(idx) * offSize;
        uint32_t v = 0;
        for (uint8_t i = 0; i < offSize; ++i) v = (v << 8) | p[i];
        return v;
    }

    bool GetItem(uint32_t idx, const uint8_t*& outPtr, size_t& outLen) const {
        if (!valid || idx >= count) return false;
        uint32_t o0 = ReadOff(idx);
        uint32_t o1 = ReadOff(idx + 1u);
        if (o0 < 1u || o1 < o0) return false;
        size_t s0 = dataBase + (o0 - 1u);
        size_t s1 = dataBase + (o1 - 1u);
        if (s1 > tableRem) return false;
        outPtr = base + s0;
        outLen = s1 - s0;
        return true;
    }
};

inline bool ParseCffIndex(const uint8_t* tbl, size_t tblLen, size_t pos, bool isCff2, CffIndexView& outIdx) {
    outIdx = {};
    if (isCff2) {
        if (pos + 4u > tblLen) return false;
        outIdx.count = DetailImg100::RdU32BE(tbl + pos);
        pos += 4u;
    } else {
        if (pos + 2u > tblLen) return false;
        outIdx.count = DetailImg100::RdU16BE(tbl + pos);
        pos += 2u;
    }
    if (outIdx.count == 0u) {
        outIdx.base = tbl;
        outIdx.tableRem = tblLen;
        outIdx.nextPos = pos;
        outIdx.valid = true;
        return true;
    }
    if (outIdx.count > 65536u || pos >= tblLen) return false;
    uint8_t offSize = tbl[pos++];
    if (offSize < 1u || offSize > 4u) return false;
    uint64_t offArrayBytes = static_cast<uint64_t>(outIdx.count + 1u) * offSize;
    if (offArrayBytes > tblLen - pos) return false;

    outIdx.base = tbl;
    outIdx.tableRem = tblLen;
    outIdx.offSize = offSize;
    outIdx.offsetsPos = pos;
    outIdx.dataBase = pos + static_cast<size_t>(offArrayBytes);

    uint32_t firstOff = outIdx.ReadOff(0);
    uint32_t lastOff  = outIdx.ReadOff(outIdx.count);
    if (firstOff != 1u || lastOff < 1u || outIdx.dataBase + (lastOff - 1u) > tblLen) return false;
    outIdx.nextPos = outIdx.dataBase + (lastOff - 1u);
    outIdx.valid = true;
    return true;
}

inline void ParseCffDict(
    const uint8_t* dict, size_t dictLen,
    int32_t& outCharStringsOff,
    int32_t& outPrivateSize, int32_t& outPrivateOff,
    int32_t& outSubrsRelOff,
    int32_t& outFdArrayOff)
{
    std::vector<double> args;
    size_t p = 0;
    while (p < dictLen) {
        uint8_t b0 = dict[p++];
        if (b0 <= 21u) {
            uint16_t op = b0;
            if (b0 == 12u) {
                if (p >= dictLen) break;
                op = static_cast<uint16_t>(0x0C00u | dict[p++]);
            }
            if (op == 17u && !args.empty()) {
                outCharStringsOff = static_cast<int32_t>(args.back());
            } else if (op == 18u && args.size() >= 2u) {
                outPrivateSize = static_cast<int32_t>(args[args.size() - 2u]);
                outPrivateOff  = static_cast<int32_t>(args[args.size() - 1u]);
            } else if (op == 19u && !args.empty()) {
                outSubrsRelOff = static_cast<int32_t>(args.back());
            } else if (op == 0x0C24u && !args.empty()) { // 12 36 = FDArray
                outFdArrayOff = static_cast<int32_t>(args.back());
            }
            args.clear();
        } else if (b0 == 28u) {
            if (p + 2u > dictLen) break;
            args.push_back(static_cast<double>(DetailImg100::RdI16BE(dict + p)));
            p += 2u;
        } else if (b0 == 29u) {
            if (p + 4u > dictLen) break;
            int32_t v = static_cast<int32_t>(DetailImg100::RdU32BE(dict + p));
            args.push_back(static_cast<double>(v));
            p += 4u;
        } else if (b0 == 30u) {
            // BCD real number until 0xF nibble
            std::string numStr;
            bool done = false;
            while (p < dictLen && !done) {
                uint8_t nb = dict[p++];
                for (int k = 0; k < 2; ++k) {
                    uint8_t nib = (k == 0) ? (nb >> 4) : (nb & 0x0Fu);
                    if (nib <= 9) numStr.push_back(static_cast<char>('0' + nib));
                    else if (nib == 0x0A) numStr.push_back('.');
                    else if (nib == 0x0B) numStr += "E";
                    else if (nib == 0x0C) numStr += "E-";
                    else if (nib == 0x0E) numStr.push_back('-');
                    else if (nib == 0x0F) { done = true; break; }
                }
            }
            args.push_back(ParseFloatOr(numStr, 0.0f));
        } else if (b0 >= 32u && b0 <= 246u) {
            args.push_back(static_cast<double>(static_cast<int>(b0) - 139));
        } else if (b0 >= 247u && b0 <= 250u) {
            if (p >= dictLen) break;
            args.push_back(static_cast<double>((static_cast<int>(b0) - 247) * 256 + dict[p++] + 108));
        } else if (b0 >= 251u && b0 <= 254u) {
            if (p >= dictLen) break;
            args.push_back(static_cast<double>(-(static_cast<int>(b0) - 251) * 256 - dict[p++] - 108));
        }
    }
}

struct ParsedCffFont {
    const uint8_t* tbl = nullptr;
    size_t tblLen = 0;
    bool isCff2 = false;
    CffIndexView charStrings;
    CffIndexView globalSubrs;
    CffIndexView localSubrs;
    bool valid = false;
};

inline bool ParseCffTable(const uint8_t* tbl, size_t tblLen, bool isCff2Hint, ParsedCffFont& outCff) {
    outCff = {};
    if (!tbl || tblLen < 8) return false;
    uint8_t major = tbl[0];
    uint8_t hdrSize = tbl[2];
    if (hdrSize < 4u || hdrSize > tblLen) return false;

    outCff.tbl = tbl;
    outCff.tblLen = tblLen;

    int32_t csOff = -1, privSz = 0, privOff = -1, subrsRel = -1, fdArrOff = -1;

    if (major == 2u || isCff2Hint) {
        if (tblLen < 5u || major != 2u) return false;
        outCff.isCff2 = true;
        uint16_t topDictLen = DetailImg100::RdU16BE(tbl + 3);
        if (static_cast<size_t>(hdrSize) + topDictLen > tblLen) return false;
        ParseCffDict(tbl + hdrSize, topDictLen, csOff, privSz, privOff, subrsRel, fdArrOff);
        if (!ParseCffIndex(tbl, tblLen, static_cast<size_t>(hdrSize) + topDictLen, true, outCff.globalSubrs)) {
            return false;
        }
    } else if (major == 1u) {
        outCff.isCff2 = false;
        CffIndexView nameIdx, topDictIdx, strIdx;
        if (!ParseCffIndex(tbl, tblLen, hdrSize, false, nameIdx) ||
            !ParseCffIndex(tbl, tblLen, nameIdx.nextPos, false, topDictIdx) ||
            topDictIdx.count == 0u ||
            !ParseCffIndex(tbl, tblLen, topDictIdx.nextPos, false, strIdx) ||
            !ParseCffIndex(tbl, tblLen, strIdx.nextPos, false, outCff.globalSubrs))
        {
            return false;
        }
        const uint8_t* tdPtr = nullptr;
        size_t tdLen = 0;
        if (!topDictIdx.GetItem(0, tdPtr, tdLen)) return false;
        ParseCffDict(tdPtr, tdLen, csOff, privSz, privOff, subrsRel, fdArrOff);
    } else {
        return false;
    }

    if (csOff <= 0 || static_cast<size_t>(csOff) >= tblLen) return false;
    if (!ParseCffIndex(tbl, tblLen, static_cast<size_t>(csOff), outCff.isCff2, outCff.charStrings) ||
        outCff.charStrings.count == 0u) {
        return false;
    }

    // Parse Private DICT (either direct or from FDArray[0]) to locate Local Subrs if present
    if (privOff < 0 && fdArrOff > 0 && static_cast<size_t>(fdArrOff) < tblLen) {
        CffIndexView fdIdx;
        if (ParseCffIndex(tbl, tblLen, static_cast<size_t>(fdArrOff), outCff.isCff2, fdIdx) && fdIdx.count > 0u) {
            const uint8_t* fd0 = nullptr; size_t fd0Len = 0;
            if (fdIdx.GetItem(0, fd0, fd0Len)) {
                int32_t dummyCs = -1, dummySub = -1, dummyFd = -1;
                ParseCffDict(fd0, fd0Len, dummyCs, privSz, privOff, dummySub, dummyFd);
            }
        }
    }
    if (privOff > 0 && privSz > 0 && static_cast<size_t>(privOff) + static_cast<size_t>(privSz) <= tblLen) {
        int32_t dummyCs = -1, dummyPs = 0, dummyPo = -1, dummyFd = -1;
        ParseCffDict(tbl + privOff, static_cast<size_t>(privSz), dummyCs, dummyPs, dummyPo, subrsRel, dummyFd);
        if (subrsRel > 0 && static_cast<size_t>(privOff + subrsRel) < tblLen) {
            ParseCffIndex(tbl, tblLen, static_cast<size_t>(privOff + subrsRel), outCff.isCff2, outCff.localSubrs);
        }
    }

    outCff.valid = true;
    return true;
}

struct CffBuildState {
    std::vector<float> stack;
    float curX = 0.0f, curY = 0.0f;
    float startX = 0.0f, startY = 0.0f;
    bool contourOpen = false;
    bool sawFirstMoveOrStem = false;
    uint32_t numHints = 0;
    float minX = 1e9f, maxX = -1e9f;
    std::vector<DetailFont::EdgeSeg> edges;

    void CloseContour() {
        if (contourOpen) {
            if (std::abs(curX - startX) > 1e-3f || std::abs(curY - startY) > 1e-3f) {
                edges.push_back({ curX, curY, startX, startY });
            }
            contourOpen = false;
        }
    }
    void MoveTo(float dx, float dy) {
        CloseContour();
        curX += dx; curY += dy;
        startX = curX; startY = curY;
        minX = std::min(minX, curX); maxX = std::max(maxX, curX);
        contourOpen = true;
    }
    void LineTo(float dx, float dy) {
        float nx = curX + dx, ny = curY + dy;
        edges.push_back({ curX, curY, nx, ny });
        curX = nx; curY = ny;
        minX = std::min(minX, curX); maxX = std::max(maxX, curX);
        contourOpen = true;
    }
    void CubicTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        float x0 = curX, y0 = curY;
        float x1 = x0 + dx1, y1 = y0 + dy1;
        float x2 = x1 + dx2, y2 = y1 + dy2;
        float x3 = x2 + dx3, y3 = y2 + dy3;
        float lx = x0, ly = y0;
        constexpr int kSteps = 10;
        for (int s = 1; s <= kSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(kSteps);
            float u = 1.0f - t;
            float nx = u * u * u * x0 + 3.0f * u * u * t * x1 + 3.0f * u * t * t * x2 + t * t * t * x3;
            float ny = u * u * u * y0 + 3.0f * u * u * t * y1 + 3.0f * u * t * t * y2 + t * t * t * y3;
            edges.push_back({ lx, ly, nx, ny });
            lx = nx; ly = ny;
            minX = std::min(minX, nx); maxX = std::max(maxX, nx);
        }
        curX = x3; curY = y3;
        contourOpen = true;
    }
};

inline int32_t CalcCffSubrBias(uint32_t count) {
    if (count < 1240u) return 107;
    if (count < 33900u) return 1131;
    return 32768;
}

inline bool ExecuteCffCharString(
    const ParsedCffFont& cff,
    const uint8_t* cs,
    size_t csLen,
    CffBuildState& st,
    int callDepth,
    bool& ended)
{
    if (callDepth > 10) return false;
    size_t p = 0;
    auto dropOptionalWidth = [&](size_t modParity) {
        if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
            if ((st.stack.size() % 2u) == modParity && !st.stack.empty()) {
                st.stack.erase(st.stack.begin());
            }
            st.sawFirstMoveOrStem = true;
        }
    };

    while (p < csLen && !ended) {
        uint8_t b0 = cs[p++];
        if (b0 == 28u) {
            if (p + 2u > csLen) return false;
            st.stack.push_back(static_cast<float>(DetailImg100::RdI16BE(cs + p)));
            p += 2u;
        } else if (b0 == 255u) {
            if (p + 4u > csLen) return false;
            int32_t fx = static_cast<int32_t>(DetailImg100::RdU32BE(cs + p));
            st.stack.push_back(static_cast<float>(fx) / 65536.0f);
            p += 4u;
        } else if (b0 >= 32u && b0 <= 246u) {
            st.stack.push_back(static_cast<float>(static_cast<int>(b0) - 139));
        } else if (b0 >= 247u && b0 <= 250u) {
            if (p >= csLen) return false;
            st.stack.push_back(static_cast<float>((static_cast<int>(b0) - 247) * 256 + cs[p++] + 108));
        } else if (b0 >= 251u && b0 <= 254u) {
            if (p >= csLen) return false;
            st.stack.push_back(static_cast<float>(-(static_cast<int>(b0) - 251) * 256 - cs[p++] - 108));
        } else {
            if (b0 == 1u || b0 == 3u || b0 == 18u || b0 == 23u) { // hstem / vstem / hstemhm / vstemhm
                dropOptionalWidth(1u);
                st.numHints += static_cast<uint32_t>(st.stack.size() / 2u);
                st.stack.clear();
            } else if (b0 == 19u || b0 == 20u) { // hintmask / cntrmask
                dropOptionalWidth(1u);
                st.numHints += static_cast<uint32_t>(st.stack.size() / 2u);
                st.stack.clear();
                size_t maskBytes = (st.numHints + 7u) / 8u;
                if (p + maskBytes > csLen) return false;
                p += maskBytes;
            } else if (b0 == 21u) { // rmoveto
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 2u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.size() < 2u) return false;
                st.MoveTo(st.stack[0], st.stack[1]);
                st.stack.clear();
            } else if (b0 == 22u) { // hmoveto
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 1u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.empty()) return false;
                st.MoveTo(st.stack[0], 0.0f);
                st.stack.clear();
            } else if (b0 == 4u) { // vmoveto
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 1u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.empty()) return false;
                st.MoveTo(0.0f, st.stack[0]);
                st.stack.clear();
            } else if (b0 == 5u) { // rlineto
                for (size_t i = 0; i + 1u < st.stack.size(); i += 2u) st.LineTo(st.stack[i], st.stack[i + 1u]);
                st.stack.clear();
            } else if (b0 == 6u || b0 == 7u) { // hlineto / vlineto
                bool horiz = (b0 == 6u);
                for (size_t i = 0; i < st.stack.size(); ++i) {
                    if (horiz) st.LineTo(st.stack[i], 0.0f);
                    else       st.LineTo(0.0f, st.stack[i]);
                    horiz = !horiz;
                }
                st.stack.clear();
            } else if (b0 == 8u) { // rrcurveto
                for (size_t i = 0; i + 5u < st.stack.size(); i += 6u) {
                    st.CubicTo(st.stack[i], st.stack[i + 1], st.stack[i + 2],
                               st.stack[i + 3], st.stack[i + 4], st.stack[i + 5]);
                }
                st.stack.clear();
            } else if (b0 == 24u) { // rcurveline
                size_t i = 0;
                for (; i + 7u < st.stack.size(); i += 6u) {
                    st.CubicTo(st.stack[i], st.stack[i + 1], st.stack[i + 2],
                               st.stack[i + 3], st.stack[i + 4], st.stack[i + 5]);
                }
                if (i + 1u < st.stack.size()) st.LineTo(st.stack[i], st.stack[i + 1u]);
                st.stack.clear();
            } else if (b0 == 25u) { // rlinecurve
                size_t n = st.stack.size();
                if (n >= 8u) {
                    size_t lineEnd = n - 6u;
                    for (size_t i = 0; i + 1u < lineEnd; i += 2u) st.LineTo(st.stack[i], st.stack[i + 1u]);
                    st.CubicTo(st.stack[lineEnd], st.stack[lineEnd + 1], st.stack[lineEnd + 2],
                               st.stack[lineEnd + 3], st.stack[lineEnd + 4], st.stack[lineEnd + 5]);
                }
                st.stack.clear();
            } else if (b0 == 26u) { // vvcurveto
                size_t i = 0;
                float dx1 = 0.0f;
                if ((st.stack.size() & 3u) == 1u) dx1 = st.stack[i++];
                for (; i + 3u < st.stack.size(); i += 4u) {
                    st.CubicTo(dx1, st.stack[i], st.stack[i + 1], st.stack[i + 2], 0.0f, st.stack[i + 3]);
                    dx1 = 0.0f;
                }
                st.stack.clear();
            } else if (b0 == 27u) { // hhcurveto
                size_t i = 0;
                float dy1 = 0.0f;
                if ((st.stack.size() & 3u) == 1u) dy1 = st.stack[i++];
                for (; i + 3u < st.stack.size(); i += 4u) {
                    st.CubicTo(st.stack[i], dy1, st.stack[i + 1], st.stack[i + 2], st.stack[i + 3], 0.0f);
                    dy1 = 0.0f;
                }
                st.stack.clear();
            } else if (b0 == 30u || b0 == 31u) { // vhcurveto / hvcurveto
                bool vh = (b0 == 30u);
                size_t i = 0;
                while (i + 3u < st.stack.size()) {
                    float extra = (i + 5u == st.stack.size()) ? st.stack[i + 4u] : 0.0f;
                    if (vh) st.CubicTo(0.0f, st.stack[i], st.stack[i + 1], st.stack[i + 2], st.stack[i + 3], extra);
                    else    st.CubicTo(st.stack[i], 0.0f, st.stack[i + 1], st.stack[i + 2], extra, st.stack[i + 3]);
                    i += (extra != 0.0f || i + 5u == st.stack.size()) ? 5u : 4u;
                    vh = !vh;
                }
                st.stack.clear();
            } else if (b0 == 10u || b0 == 29u) { // callsubr / callgsubr
                if (st.stack.empty()) return false;
                int32_t subNum = static_cast<int32_t>(std::llround(st.stack.back()));
                st.stack.pop_back();
                const CffIndexView& bank = (b0 == 10u) ? cff.localSubrs : cff.globalSubrs;
                if (!bank.valid || bank.count == 0u) return false;
                int32_t idx = subNum + CalcCffSubrBias(bank.count);
                if (idx < 0 || static_cast<uint32_t>(idx) >= bank.count) return false;
                const uint8_t* subPtr = nullptr; size_t subLen = 0;
                if (!bank.GetItem(static_cast<uint32_t>(idx), subPtr, subLen)) return false;
                if (!ExecuteCffCharString(cff, subPtr, subLen, st, callDepth + 1, ended)) return false;
            } else if (b0 == 11u) { // return
                return true;
            } else if (b0 == 14u) { // endchar
                st.CloseContour();
                st.stack.clear();
                ended = true;
                return true;
            } else if (b0 == 12u) {
                if (p >= csLen) return false;
                uint8_t b1 = cs[p++];
                if (b1 == 35u && st.stack.size() >= 12u) { // flex
                    st.CubicTo(st.stack[0], st.stack[1], st.stack[2], st.stack[3], st.stack[4], st.stack[5]);
                    st.CubicTo(st.stack[6], st.stack[7], st.stack[8], st.stack[9], st.stack[10], st.stack[11]);
                } else if (b1 == 34u && st.stack.size() >= 7u) { // hflex
                    st.CubicTo(st.stack[0], 0.0f, st.stack[1], st.stack[2], st.stack[3], 0.0f);
                    st.CubicTo(st.stack[4], 0.0f, st.stack[5], -st.stack[2], st.stack[6], 0.0f);
                }
                st.stack.clear();
            } else {
                st.stack.clear();
            }
        }
    }
    if (callDepth == 0) st.CloseContour();
    return true;
}

inline void RasterizeEdgeListToCanvas(
    const DetailFont::ParsedSfnt& sfnt,
    uint16_t glyphId,
    const std::vector<DetailFont::EdgeSeg>& rawEdges,
    float gxMin, float gxMax,
    float originX, float baselineY, float pixelHeight,
    uint8_t inkR, uint8_t inkG, uint8_t inkB, uint8_t inkA,
    std::vector<uint8_t>& bgra, uint32_t imgW, uint32_t imgH,
    float& outAdvPx)
{
    float scale = pixelHeight / static_cast<float>(std::max<uint16_t>(16, sfnt.unitsPerEm));
    outAdvPx = static_cast<float>(DetailFont::GetGlyphAdvance(sfnt, glyphId)) * scale;
    if (outAdvPx <= 1.0f && gxMax > gxMin) {
        outAdvPx = (gxMax - gxMin) * scale + pixelHeight * 0.12f;
    }
    if (rawEdges.empty() || inkA == 0) return;

    std::vector<DetailFont::EdgeSeg> scrEdges;
    scrEdges.reserve(rawEdges.size());
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& e : rawEdges) {
        DetailFont::EdgeSeg se{
            originX + e.x0 * scale, baselineY - e.y0 * scale,
            originX + e.x1 * scale, baselineY - e.y1 * scale
        };
        if (std::abs(se.y1 - se.y0) < 1e-5f) continue;
        minX = std::min({ minX, se.x0, se.x1 }); maxX = std::max({ maxX, se.x0, se.x1 });
        minY = std::min({ minY, se.y0, se.y1 }); maxY = std::max({ maxY, se.y0, se.y1 });
        scrEdges.push_back(se);
    }
    if (scrEdges.empty()) return;

    int x0 = std::max(0, static_cast<int>(std::floor(minX)) - 1);
    int x1 = std::min(static_cast<int>(imgW) - 1, static_cast<int>(std::ceil(maxX)) + 1);
    int y0 = std::max(0, static_cast<int>(std::floor(minY)) - 1);
    int y1 = std::min(static_cast<int>(imgH) - 1, static_cast<int>(std::ceil(maxY)) + 1);
    if (x0 > x1 || y0 > y1) return;

    struct Hit { float x; int dir; };
    std::vector<Hit> hits;
    std::vector<uint8_t> rowCov(static_cast<size_t>(x1 - x0 + 1), 0);

    for (int py = y0; py <= y1; ++py) {
        std::fill(rowCov.begin(), rowCov.end(), uint8_t(0));
        for (int subY = 0; subY < 4; ++subY) {
            float sy = static_cast<float>(py) + (static_cast<float>(subY) + 0.5f) * 0.25f;
            hits.clear();
            for (const auto& e : scrEdges) {
                if ((e.y0 <= sy && e.y1 > sy) || (e.y1 <= sy && e.y0 > sy)) {
                    int dir = (e.y1 > e.y0) ? 1 : -1;
                    float xHit = e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0);
                    hits.push_back({ xHit, dir });
                }
            }
            if (hits.size() < 2) continue;
            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.x < b.x; });
            int winding = 0;
            for (size_t hi = 0; hi + 1 < hits.size(); ++hi) {
                winding += hits[hi].dir;
                if (winding != 0) {
                    int pxS = std::max(x0, static_cast<int>(std::floor(hits[hi].x)));
                    int pxE = std::min(x1, static_cast<int>(std::ceil(hits[hi + 1].x)));
                    for (int px = pxS; px <= pxE; ++px) {
                        for (int subX = 0; subX < 4; ++subX) {
                            float sx = static_cast<float>(px) + (static_cast<float>(subX) + 0.5f) * 0.25f;
                            if (sx >= hits[hi].x && sx < hits[hi + 1].x) ++rowCov[static_cast<size_t>(px - x0)];
                        }
                    }
                }
            }
        }
        for (int px = x0; px <= x1; ++px) {
            uint8_t c = rowCov[static_cast<size_t>(px - x0)];
            if (c == 0) continue;
            float covA = (static_cast<float>(c) / 16.0f) * (static_cast<float>(inkA) / 255.0f);
            uint8_t* dst = &bgra[(static_cast<size_t>(py) * imgW + static_cast<size_t>(px)) * 4];
            dst[0] = static_cast<uint8_t>(std::clamp(dst[0] * (1.0f - covA) + inkB * covA, 0.0f, 255.0f));
            dst[1] = static_cast<uint8_t>(std::clamp(dst[1] * (1.0f - covA) + inkG * covA, 0.0f, 255.0f));
            dst[2] = static_cast<uint8_t>(std::clamp(dst[2] * (1.0f - covA) + inkR * covA, 0.0f, 255.0f));
            dst[3] = 255;
        }
    }
}

inline bool RenderCffOrColorFontSpecimen(
    const uint8_t* sfntPtr,
    size_t sfntLen,
    const DetailFont::ParsedSfnt& sfnt,
    ExtendedImageInfo& outInfo)
{
    const uint32_t W = outInfo.width, H = outInfo.height;

    // Locate optional CFF / CFF2 / COLR / CPAL / CBDT / CBLC / sbix tables from SFNT directory
    size_t hdrOff = 0;
    if (sfntLen >= 16 && std::memcmp(sfntPtr, "ttcf", 4) == 0) hdrOff = DetailImg100::RdU32BE(sfntPtr + 12);
    if (hdrOff + 12 > sfntLen) return false;
    uint16_t numTables = DetailImg100::RdU16BE(sfntPtr + hdrOff + 4);

    DetailFont::SfntTableSlice tblCff1{}, tblCff2{}, tblColr{}, tblCpal{}, tblCbdt{}, tblSbix{};
    for (uint16_t i = 0; i < numTables && hdrOff + 12u + static_cast<size_t>(i + 1) * 16u <= sfntLen; ++i) {
        const uint8_t* rec = sfntPtr + hdrOff + 12 + static_cast<size_t>(i) * 16;
        uint32_t tag = DetailImg100::RdU32BE(rec + 0);
        uint32_t off = DetailImg100::RdU32BE(rec + 8);
        uint32_t len = DetailImg100::RdU32BE(rec + 12);
        if (off > sfntLen || len > sfntLen - off) continue;
        DetailFont::SfntTableSlice sl{ off, len, len > 0 };
        if (tag == DetailFont::MakeSfntTag("CFF ")) tblCff1 = sl;
        else if (tag == DetailFont::MakeSfntTag("CFF2")) tblCff2 = sl;
        else if (tag == DetailFont::MakeSfntTag("COLR")) tblColr = sl;
        else if (tag == DetailFont::MakeSfntTag("CPAL")) tblCpal = sl;
        else if (tag == DetailFont::MakeSfntTag("CBDT")) tblCbdt = sl;
        else if (tag == DetailFont::MakeSfntTag("sbix")) tblSbix = sl;
    }

    ParsedCffFont cff;
    bool hasCff = false;
    if (tblCff2.valid) hasCff = ParseCffTable(sfntPtr + tblCff2.offset, tblCff2.length, true, cff);
    if (!hasCff && tblCff1.valid) hasCff = ParseCffTable(sfntPtr + tblCff1.offset, tblCff1.length, false, cff);

    auto getOutlineEdges = [&](uint16_t gid, std::vector<DetailFont::EdgeSeg>& edges, float& xmn, float& xmx) -> bool {
        edges.clear();
        if (sfnt.tblGlyf.valid && sfnt.tblLoca.valid) {
            int16_t iMin = 0, iMax = 0;
            if (DetailFont::ExtractGlyphEdges(sfnt, gid, edges, iMin, iMax, 0) && !edges.empty()) {
                xmn = static_cast<float>(iMin);
                xmx = static_cast<float>(iMax);
                return true;
            }
        }
        if (hasCff && gid < cff.charStrings.count) {
            const uint8_t* csPtr = nullptr; size_t csLen = 0;
            if (cff.charStrings.GetItem(gid, csPtr, csLen) && csLen > 0) {
                CffBuildState st;
                bool ended = false;
                if (ExecuteCffCharString(cff, csPtr, csLen, st, 0, ended) && !st.edges.empty()) {
                    xmn = (st.minX < st.maxX) ? st.minX : 0.0f;
                    xmx = (st.minX < st.maxX) ? st.maxX : static_cast<float>(sfnt.unitsPerEm / 2);
                    edges = std::move(st.edges);
                    return true;
                }
            }
        }
        return false;
    };

    // Draw header banner text
    std::string asciiHeader;
    for (wchar_t wc : sfnt.familyName) if (wc >= 32 && wc < 127) asciiHeader.push_back(static_cast<char>(wc));
    if (asciiHeader.empty()) asciiHeader = tblCff2.valid ? "OPENTYPE CFF2 SPECIMEN" : "OPENTYPE FONT SPECIMEN";
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 14, 13, asciiHeader, 2, 240, 246, 255);

    size_t drawnItems = 0;

    // 1. COLR v0 + CPAL multi-layer color vector glyphs!
    if (tblColr.valid && tblColr.length >= 14 && tblCpal.valid && tblCpal.length >= 14) {
        const uint8_t* colr = sfntPtr + tblColr.offset;
        const uint8_t* cpal = sfntPtr + tblCpal.offset;
        uint16_t numBase   = DetailImg100::RdU16BE(colr + 2);
        uint32_t baseOff   = DetailImg100::RdU32BE(colr + 4);
        uint32_t layerOff  = DetailImg100::RdU32BE(colr + 8);
        uint16_t numLayers = DetailImg100::RdU16BE(colr + 12);

        uint16_t numPalEntries = DetailImg100::RdU16BE(cpal + 2);
        uint16_t numPalettes   = DetailImg100::RdU16BE(cpal + 4);
        uint16_t numColorRecs  = DetailImg100::RdU16BE(cpal + 6);
        uint32_t colorRecOff   = DetailImg100::RdU32BE(cpal + 8);

        if (numBase > 0 && numPalettes > 0 && baseOff + static_cast<size_t>(numBase) * 6u <= tblColr.length &&
            layerOff + static_cast<size_t>(numLayers) * 4u <= tblColr.length &&
            colorRecOff + static_cast<size_t>(numColorRecs) * 4u <= tblCpal.length)
        {
            uint16_t firstColorIdx = DetailImg100::RdU16BE(cpal + 12);
            float curX = 24.0f, curY = 140.0f;
            for (uint16_t b = 0; b < numBase && curY < static_cast<float>(H) - 24.0f; ++b) {
                const uint8_t* br = colr + baseOff + static_cast<size_t>(b) * 6u;
                uint16_t baseGid   = DetailImg100::RdU16BE(br + 0);
                uint16_t firstLay  = DetailImg100::RdU16BE(br + 2);
                uint16_t layCount  = DetailImg100::RdU16BE(br + 4);
                float advPx = 48.0f;
                bool anyLay = false;
                for (uint16_t l = 0; l < layCount && firstLay + l < numLayers; ++l) {
                    const uint8_t* lr = colr + layerOff + static_cast<size_t>(firstLay + l) * 4u;
                    uint16_t layGid = DetailImg100::RdU16BE(lr + 0);
                    uint16_t palIdx = DetailImg100::RdU16BE(lr + 2);
                    uint8_t cr = 24, cg = 32, cb = 44, ca = 255;
                    if (palIdx != 0xFFFFu && palIdx < numPalEntries &&
                        static_cast<size_t>(firstColorIdx) + palIdx < numColorRecs)
                    {
                        const uint8_t* cp = cpal + colorRecOff + (static_cast<size_t>(firstColorIdx) + palIdx) * 4u;
                        cb = cp[0]; cg = cp[1]; cr = cp[2]; ca = cp[3]; // CPAL stores BGRA!
                    }
                    std::vector<DetailFont::EdgeSeg> edges;
                    float xmn = 0.0f, xmx = 0.0f;
                    if (getOutlineEdges(layGid, edges, xmn, xmx)) {
                        RasterizeEdgeListToCanvas(sfnt, baseGid, edges, xmn, xmx, curX, curY, 72.0f,
                                                  cr, cg, cb, ca, outInfo.bgraPixels, W, H, advPx);
                        anyLay = true;
                    }
                }
                if (anyLay) {
                    ++drawnItems;
                    curX += std::max(56.0f, advPx + 14.0f);
                    if (curX > static_cast<float>(W) - 72.0f) { curX = 24.0f; curY += 92.0f; }
                }
            }
            if (drawnItems > 0) {
                outInfo.formatName += L" [COLR/CPAL Color]";
                return true;
            }
        }
    }

    // 2. CBDT / sbix embedded PNG color bitmap glyphs!
    auto scanAndBlitEmbeddedGlyphPngs = [&](const uint8_t* buf, size_t len, const wchar_t* tagLabel) -> bool {
        static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        static const uint8_t kIend[8]   = { 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
        uint32_t curX = 24, curY = 64, rowH = 0;
        size_t count = 0;
        for (size_t i = 0; i + 24 < len && count < 32; ++i) {
            if (std::memcmp(buf + i, kPngSig, 8) == 0) {
                size_t pngLen = len - i;
                for (size_t j = i + 16; j + 8 <= len; ++j) {
                    if (std::memcmp(buf + j, kIend, 8) == 0) { pngLen = (j + 8) - i; break; }
                }
                ExtendedImageInfo gImg;
                if (DetailImg100::DecodeEmbeddedWicBuffer(buf + i, pngLen, L"GlyphPNG", gImg) && gImg.valid) {
                    if (curX + gImg.width + 16 > W) {
                        curX = 24;
                        curY += rowH + 16;
                        rowH = 0;
                    }
                    if (curY + gImg.height > H - 8) break;
                    for (uint32_t py = 0; py < gImg.height; ++py) {
                        for (uint32_t px = 0; px < gImg.width; ++px) {
                            const uint8_t* sp = &gImg.bgraPixels[(static_cast<size_t>(py) * gImg.width + px) * 4u];
                            BlendPremulBgraPixel(
                                &outInfo.bgraPixels[(static_cast<size_t>(curY + py) * W + (curX + px)) * 4u],
                                sp[0], sp[1], sp[2], sp[3], 1.0f, LayerBlendOp::SrcOver);
                        }
                    }
                    curX += gImg.width + 16;
                    rowH = std::max(rowH, gImg.height);
                    ++count;
                    i += pngLen - 1;
                }
            }
        }
        if (count > 0) {
            outInfo.formatName += tagLabel;
            return true;
        }
        return false;
    };

    if (tblCbdt.valid && scanAndBlitEmbeddedGlyphPngs(sfntPtr + tblCbdt.offset, tblCbdt.length, L" [CBDT Color Bitmap]")) {
        return true;
    }
    if (tblSbix.valid && scanAndBlitEmbeddedGlyphPngs(sfntPtr + tblSbix.offset, tblSbix.length, L" [sbix Color Bitmap]")) {
        return true;
    }

    // 3. CFF / CFF2 cubic Bezier specimen lines!
    if (hasCff) {
        auto drawLineCff = [&](std::wstring_view txt, float x, float baseY, float pxH, uint8_t r, uint8_t g, uint8_t b) {
            float curX = x;
            for (wchar_t wc : txt) {
                if (wc == L' ') { curX += pxH * 0.30f; continue; }
                uint16_t gid = DetailFont::LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc));
                if (gid == 0 && wc >= L'a' && wc <= L'z') {
                    gid = DetailFont::LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc - L'a' + L'A'));
                }
                if (gid == 0) { curX += pxH * 0.30f; continue; }
                std::vector<DetailFont::EdgeSeg> edges;
                float xmn = 0.0f, xmx = 0.0f, adv = 0.0f;
                if (getOutlineEdges(gid, edges, xmn, xmx)) {
                    RasterizeEdgeListToCanvas(sfnt, gid, edges, xmn, xmx, curX, baseY, pxH,
                                              r, g, b, 255, outInfo.bgraPixels, W, H, adv);
                    ++drawnItems;
                }
                curX += std::max(pxH * 0.22f, adv);
                if (curX >= static_cast<float>(W) - 14.0f) break;
            }
        };

        drawLineCff(L"ABCDEFGHIJKLM", 18.0f, 92.0f,  36.0f, 20, 26, 36);
        drawLineCff(L"NOPQRSTUVWXYZ", 18.0f, 138.0f, 36.0f, 20, 26, 36);
        drawLineCff(L"abcdefghijklm", 18.0f, 182.0f, 32.0f, 32, 42, 58);
        drawLineCff(L"0123456789 !?", 18.0f, 228.0f, 30.0f, 45, 78, 138);
        drawLineCff(L"OpenType CFF Specimen 64", 18.0f, 310.0f, 48.0f, 22, 48, 98);

        if (drawnItems == 0) {
            float curX = 20.0f, curY = 120.0f;
            uint32_t lim = std::min<uint32_t>(cff.charStrings.count, 64u);
            for (uint32_t gid = 0; gid < lim && curY < static_cast<float>(H) - 24.0f; ++gid) {
                std::vector<DetailFont::EdgeSeg> edges;
                float xmn = 0.0f, xmx = 0.0f, adv = 0.0f;
                if (getOutlineEdges(static_cast<uint16_t>(gid), edges, xmn, xmx)) {
                    RasterizeEdgeListToCanvas(sfnt, static_cast<uint16_t>(gid), edges, xmn, xmx, curX, curY, 56.0f,
                                              22, 30, 44, 255, outInfo.bgraPixels, W, H, adv);
                    ++drawnItems;
                    curX += std::max(42.0f, adv + 12.0f);
                    if (curX > static_cast<float>(W) - 64.0f) { curX = 20.0f; curY += 76.0f; }
                }
            }
        }
        if (drawnItems > 0) {
            if (cff.isCff2 && outInfo.formatName.find(L"CFF2") == std::wstring::npos) {
                outInfo.formatName += L" [CFF2]";
            }
            return true;
        }
    }

    return false;
}

// ----------------------------------------------------------------------------
// 7. Windows Static Cursor (.cur) — Header 00 00 02 00 + Hotspot + PNG/DIB+AND
// ----------------------------------------------------------------------------
inline bool DecodeCurCursor(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 22) return false;
    uint16_t idReserved = DetailImg100::RdU16LE(data + 0);
    uint16_t idType     = DetailImg100::RdU16LE(data + 2);
    uint16_t idCount    = DetailImg100::RdU16LE(data + 4);
    if (idReserved != 0u || idType != 2u || idCount == 0u || idCount > 256u) return false;
    if (6u + static_cast<size_t>(idCount) * 16u > size) return false;

    size_t bestIdx = SIZE_MAX;
    uint64_t bestScore = 0;

    for (uint16_t i = 0; i < idCount; ++i) {
        const uint8_t* ent = data + 6u + static_cast<size_t>(i) * 16u;
        uint32_t w = (ent[0] == 0u) ? 256u : ent[0];
        uint32_t h = (ent[1] == 0u) ? 256u : ent[1];
        uint32_t bytesInRes = DetailImg100::RdU32LE(ent + 8);
        uint32_t imgOff     = DetailImg100::RdU32LE(ent + 12);
        if (bytesInRes < 16u || imgOff < 6u + static_cast<size_t>(idCount) * 16u ||
            imgOff > size || bytesInRes > size - imgOff) {
            continue;
        }
        uint32_t bpp = 32u;
        if (bytesInRes >= 40u && std::memcmp(data + imgOff, "\x89PNG\r\n\x1A\n", 8) != 0) {
            bpp = DetailImg100::RdU16LE(data + imgOff + 14);
        }
        uint64_t score = static_cast<uint64_t>(w) * h * 64ull + bpp;
        if (score > bestScore || bestIdx == SIZE_MAX) {
            bestScore = score;
            bestIdx = i;
        }
    }
    if (bestIdx == SIZE_MAX) return false;

    const uint8_t* ent = data + 6u + bestIdx * 16u;
    uint16_t hotX = DetailImg100::RdU16LE(ent + 4);
    uint16_t hotY = DetailImg100::RdU16LE(ent + 6);
    uint32_t bytesInRes = DetailImg100::RdU32LE(ent + 8);
    uint32_t imgOff     = DetailImg100::RdU32LE(ent + 12);
    const uint8_t* img  = data + imgOff;

    std::wstring fmtLabel = L"Windows Cursor (.cur, Hotspot " + std::to_wstring(hotX) + L"," + std::to_wstring(hotY) + L")";

    // Case A: Embedded PNG cursor frame
    if (bytesInRes >= 24u && std::memcmp(img, "\x89PNG\r\n\x1A\n", 8) == 0) {
        if (DetailImg100::DecodeEmbeddedWicBuffer(img, bytesInRes, fmtLabel, outInfo)) {
            outInfo.origWidth = outInfo.width;
            outInfo.origHeight = outInfo.height;
            return true;
        }
        return false;
    }

    // Case B: BITMAPINFOHEADER DIB (XOR color bitmap + 1-bit AND mask)
    if (bytesInRes < 40u) return false;
    uint32_t biSize   = DetailImg100::RdU32LE(img + 0);
    int32_t  biWidth  = static_cast<int32_t>(DetailImg100::RdU32LE(img + 4));
    int32_t  biHeight = static_cast<int32_t>(DetailImg100::RdU32LE(img + 8));
    uint16_t biPlanes = DetailImg100::RdU16LE(img + 12);
    uint16_t biBits   = DetailImg100::RdU16LE(img + 14);
    uint32_t biComp   = DetailImg100::RdU32LE(img + 16);
    uint32_t biClrUsed= DetailImg100::RdU32LE(img + 32);

    if (biSize < 40u || biSize > bytesInRes || biPlanes != 1u || biComp != 0u ||
        biWidth <= 0 || biWidth > 1024 || biHeight == 0) {
        return false;
    }
    if (biBits != 1u && biBits != 4u && biBits != 8u && biBits != 24u && biBits != 32u) return false;

    const bool topDown = (biHeight < 0);
    const uint32_t absH = static_cast<uint32_t>(std::abs(biHeight));
    // ICO/CUR DIB biHeight stores XOR height + AND height (2 * cursorH)!
    const uint32_t w = static_cast<uint32_t>(biWidth);
    const uint32_t h = absH / 2u;
    if (h == 0u || h > 1024u) return false;

    uint32_t palCount = (biBits <= 8u) ? ((biClrUsed > 0u && biClrUsed <= (1u << biBits)) ? biClrUsed : (1u << biBits)) : 0u;
    size_t palBytes = static_cast<size_t>(palCount) * 4u;
    if (biSize + palBytes > bytesInRes) return false;
    const uint8_t* palPtr = img + biSize;

    size_t xorStride = ((static_cast<size_t>(w) * biBits + 31u) / 32u) * 4u;
    size_t andStride = ((static_cast<size_t>(w) + 31u) / 32u) * 4u;
    size_t xorOffset = biSize + palBytes;
    size_t xorBytes  = xorStride * h;
    if (xorOffset + xorBytes > bytesInRes) return false;
    const uint8_t* xorPtr = img + xorOffset;
    const uint8_t* andPtr = (xorOffset + xorBytes + andStride * h <= bytesInRes) ? (xorPtr + xorBytes) : nullptr;

    // Check if 32-bit cursor has non-zero per-pixel alpha channel
    bool anyAlpha32 = false;
    if (biBits == 32u) {
        for (uint32_t y = 0; y < h && !anyAlpha32; ++y) {
            const uint8_t* row = xorPtr + static_cast<size_t>(y) * xorStride;
            for (uint32_t x = 0; x < w; ++x) {
                if (row[x * 4u + 3u] != 0u) { anyAlpha32 = true; break; }
            }
        }
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = fmtLabel;
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t srcRow = topDown ? y : (h - 1u - y);
        const uint8_t* xRow = xorPtr + static_cast<size_t>(srcRow) * xorStride;
        const uint8_t* aRow = andPtr ? (andPtr + static_cast<size_t>(srcRow) * andStride) : nullptr;

        for (uint32_t x = 0; x < w; ++x) {
            uint8_t b = 0, g = 0, r = 0, a = 255u;
            bool andBit = aRow ? (((aRow[x >> 3] >> (7u - (x & 7u))) & 1u) != 0u) : false;

            if (biBits == 32u) {
                b = xRow[x * 4u + 0u];
                g = xRow[x * 4u + 1u];
                r = xRow[x * 4u + 2u];
                a = anyAlpha32 ? xRow[x * 4u + 3u] : (andBit ? 0u : 255u);
            } else if (biBits == 24u) {
                b = xRow[x * 3u + 0u];
                g = xRow[x * 3u + 1u];
                r = xRow[x * 3u + 2u];
                a = andBit ? ((b | g | r) ? 192u : 0u) : 255u;
            } else {
                uint32_t idx = 0;
                if (biBits == 8u) idx = xRow[x];
                else if (biBits == 4u) idx = ((x & 1u) == 0u) ? (xRow[x >> 1] >> 4) : (xRow[x >> 1] & 0x0Fu);
                else idx = (xRow[x >> 3] >> (7u - (x & 7u))) & 1u;

                if (idx < palCount) {
                    b = palPtr[idx * 4u + 0u];
                    g = palPtr[idx * 4u + 1u];
                    r = palPtr[idx * 4u + 2u];
                }
                if (andBit) {
                    // AND=1, XOR=0 -> transparent; AND=1, XOR!=0 -> inverted screen cursor pixel!
                    if (b == 0u && g == 0u && r == 0u && idx == 0u) {
                        a = 0u;
                    } else {
                        a = 220u;
                    }
                }
            }
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4u], r, g, b, a);
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace DetailArtFont

inline bool DecodeCur(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailArtFont::DecodeCurCursor(data, size, outInfo);
}

} // namespace Formats

