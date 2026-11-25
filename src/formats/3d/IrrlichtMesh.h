#pragma once
// IrrlichtMesh.h — Irrlicht Engine XML 3D Mesh (.irrmesh) reader
// Parses <mesh> / <buffer> blocks, supporting all 3 Irrlicht vertex layouts ("standard" 9-token,
// "2tcoords" 11-token, and "tangents" 15-token with hex ARGB colors) and per-buffer <indices>.

namespace Formats {

namespace DetailIrrlichtMesh {

inline std::string_view GetIrrAttr(std::string_view tag, std::string_view attrName) {
    return XmlTagScan::GetAttr(tag, attrName);
}

} // namespace DetailIrrlichtMesh

inline bool ParseIrrlichtMesh(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 32 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;
    if (sv.find("<mesh") == std::string_view::npos || sv.find("<buffer") == std::string_view::npos) {
        return false;
    }

    size_t searchPos = 0;
    while (searchPos < sv.size()) {
        const size_t bufOpen = sv.find("<buffer", searchPos);
        if (bufOpen == std::string_view::npos) break;
        const size_t bufTagClose = sv.find('>', bufOpen);
        if (bufTagClose == std::string_view::npos) return false;
        const size_t bufEnd = sv.find("</buffer>", bufTagClose + 1);
        if (bufEnd == std::string_view::npos) return false;

        std::string_view bufBody = sv.substr(bufTagClose + 1, bufEnd - (bufTagClose + 1));
        searchPos = bufEnd + 9;

        // Parse <vertices ...> ... </vertices>
        const size_t vOpen = bufBody.find("<vertices");
        if (vOpen == std::string_view::npos) continue;
        const size_t vTagEnd = bufBody.find('>', vOpen);
        const size_t vClose = bufBody.find("</vertices>", (vTagEnd == std::string_view::npos) ? vOpen : vTagEnd);
        if (vTagEnd == std::string_view::npos || vClose == std::string_view::npos) return false;

        std::string_view vTag = bufBody.substr(vOpen, vTagEnd - vOpen + 1);
        std::string_view vType = DetailIrrlichtMesh::GetIrrAttr(vTag, "type");
        std::string_view vCntSv = DetailIrrlichtMesh::GetIrrAttr(vTag, "vertexCount");

        size_t stride = 9u; // "standard": pos(3) + nrm(3) + hexColor(1) + uv(2)
        if (ReaderText::AsciiEqCI(vType, "2tcoords")) stride = 11u;
        else if (ReaderText::AsciiEqCI(vType, "tangents")) stride = 15u;

        int declaredVerts = 0;
        if (!vCntSv.empty() && (!ReaderText::ParseIntSv(vCntSv, declaredVerts) || declaredVerts < 0 || declaredVerts > 2000000)) {
            return false;
        }

        std::string_view vContent = bufBody.substr(vTagEnd + 1, vClose - (vTagEnd + 1));
        std::vector<std::string_view> vToks;
        {
            std::string_view rem = vContent, tk;
            size_t pos = 0;
            std::string_view ln;
            while (ReaderText::NextLineSv(rem, pos, ln)) {
                while (ReaderText::NextTokenSv(ln, tk)) {
                    vToks.push_back(tk);
                    if (vToks.size() > 16000000) break;
                }
            }
        }
        if (vToks.size() < stride * 3u || (vToks.size() % stride) != 0) return false;
        const size_t bufVertCount = vToks.size() / stride;
        if (declaredVerts > 0 && static_cast<size_t>(declaredVerts) != bufVertCount) return false;

        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        for (size_t i = 0; i < bufVertCount; ++i) {
            float x = 0, y = 0, z = 0;
            if (!ReaderText::ParseFloatSv(vToks[i * stride + 0], x) ||
                !ReaderText::ParseFloatSv(vToks[i * stride + 1], y) ||
                !ReaderText::ParseFloatSv(vToks[i * stride + 2], z)) {
                return false;
            }
            // Irrlicht is left-handed Y-up -> negate Z for right-handed Y-up viewer
            mesh.vertices.push_back({ x, y, -z });
        }

        // Parse <indices ...> ... </indices>
        const size_t iOpen = bufBody.find("<indices");
        if (iOpen == std::string_view::npos) continue;
        const size_t iTagEnd = bufBody.find('>', iOpen);
        const size_t iClose = bufBody.find("</indices>", (iTagEnd == std::string_view::npos) ? iOpen : iTagEnd);
        if (iTagEnd == std::string_view::npos || iClose == std::string_view::npos) return false;

        std::string_view iTag = bufBody.substr(iOpen, iTagEnd - iOpen + 1);
        std::string_view iCntSv = DetailIrrlichtMesh::GetIrrAttr(iTag, "indexCount");
        int declaredIdx = 0;
        if (!iCntSv.empty() && (!ReaderText::ParseIntSv(iCntSv, declaredIdx) || declaredIdx < 0 || declaredIdx > 12000000)) {
            return false;
        }

        std::string_view iContent = bufBody.substr(iTagEnd + 1, iClose - (iTagEnd + 1));
        std::vector<uint32_t> indices;
        {
            size_t pos = 0;
            std::string_view ln, tk;
            while (ReaderText::NextLineSv(iContent, pos, ln)) {
                while (ReaderText::NextTokenSv(ln, tk)) {
                    int vi = -1;
                    if (!ReaderText::ParseIntSv(tk, vi) || vi < 0 || static_cast<size_t>(vi) >= bufVertCount) {
                        return false;
                    }
                    indices.push_back(static_cast<uint32_t>(vi));
                }
            }
        }
        if (declaredIdx > 0 && static_cast<size_t>(declaredIdx) != indices.size()) return false;
        for (size_t k = 0; k + 2 < indices.size(); k += 3) {
            uint32_t i0 = baseV + indices[k + 0];
            uint32_t i1 = baseV + indices[k + 1];
            uint32_t i2 = baseV + indices[k + 2];
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                mesh.tris.push_back({ i0, i2, i1, {} });
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Irrlicht XML Mesh (.irrmesh)";
    return true;
}

} // namespace Formats
