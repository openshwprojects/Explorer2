#pragma once
// OpenRasterOra.h — OpenRaster Layered Artwork Compositor (.ora)

namespace Formats {

namespace DetailArtFont {

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
        if (!DetailPngPure::DecodePngToImageInfo(layerBytes.data(), layerBytes.size(), L"ORA Layer", lImg) &&
            !DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(layerBytes.data(), layerBytes.size(), L"ORA Layer", lImg)) {
            continue;
        }
        if (!lImg.valid) continue;
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


} // namespace DetailArtFont

inline bool DecodeOpenRasterOra(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;
    const ZipDeflate::ArchiveItemMeta* stackEnt = DetailArtFont::FindZipEntryCaseInsensitive(entries, L"stack.xml");
    if (stackEnt) {
        std::vector<uint8_t> xmlBytes;
        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, *stackEnt, xmlBytes) && !xmlBytes.empty()) {
            std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
            if (DetailArtFont::CompositeOraFromStackXml(data, size, entries, sv, outInfo)) {
                return true;
            }
        }
    }
    return DetailArtFont::DecodeZipArtworkFallback(data, size, entries, L"OpenRaster Document (.ora)", outInfo);
}

} // namespace Formats
