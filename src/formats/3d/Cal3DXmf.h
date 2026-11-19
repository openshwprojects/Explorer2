#pragma once
// Cal3DXmf.h — Cal3D XML Skeletal Mesh (.xmf) reader.
#include "../common/MeshFormatCommon.h"
#include "Cal3DCmf.h"

namespace Formats {

// 2. Cal3D XML Mesh (.xmf — MAGIC="XMF" / <MESH><SUBMESH>)
inline bool ParseCal3DXmf(std::string_view sv, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace CalCryDetail;
    // Also accept binary CMF accidentally saved with .xmf extension
    if (sv.size() >= 16 && std::memcmp(sv.data(), "CMF\0", 4) == 0) {
        return ParseCal3DCmf(reinterpret_cast<const uint8_t*>(sv.data()), sv.size(), fullPath, mesh);
    }
    if (sv.find("XMF") == std::string_view::npos && sv.find("<SUBMESH") == std::string_view::npos &&
        sv.find("<submesh") == std::string_view::npos) {
        return false;
    }

    std::vector<CalWeightedVert> allVerts;
    mesh.tris.clear();

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t smOpen = sv.find("<SUBMESH", pos);
        bool lowerCase = false;
        if (smOpen == std::string_view::npos) {
            smOpen = sv.find("<submesh", pos);
            lowerCase = true;
        }
        if (smOpen == std::string_view::npos) break;
        size_t smClose = sv.find(lowerCase ? "</submesh>" : "</SUBMESH>", smOpen);
        if (smClose == std::string_view::npos) return false;
        std::string_view smBody = sv.substr(smOpen, smClose - smOpen);
        pos = smClose + 10;

        uint32_t baseVert = static_cast<uint32_t>(allVerts.size());
        uint32_t smVertCnt = 0;

        // Parse <VERTEX ...>...</VERTEX>
        size_t vp = 0;
        while (vp < smBody.size()) {
            size_t vOpen = smBody.find(lowerCase ? "<vertex" : "<VERTEX", vp);
            if (vOpen == std::string_view::npos) break;
            size_t vEndTag = smBody.find('>', vOpen);
            size_t vClose = smBody.find(lowerCase ? "</vertex>" : "</VERTEX>", vOpen);
            if (vEndTag == std::string_view::npos || vClose == std::string_view::npos || vClose < vEndTag) return false;
            std::string_view vInner = smBody.substr(vEndTag + 1, vClose - (vEndTag + 1));
            vp = vClose + 9;

            size_t pOpen = vInner.find(lowerCase ? "<pos>" : "<POS>");
            size_t pClose = vInner.find(lowerCase ? "</pos>" : "</POS>");
            if (pOpen == std::string_view::npos || pClose == std::string_view::npos || pClose <= pOpen + 5) continue;
            std::vector<float> pNums;
            ParseXmlFloatsSimple(vInner.substr(pOpen + 5, pClose - (pOpen + 5)), pNums);
            if (pNums.size() < 3) continue;

            CalWeightedVert wv;
            wv.pos = { pNums[0], pNums[1], pNums[2] };

            // Parse optional <INFLUENCE ID="boneId">weight</INFLUENCE>
            size_t ip = 0;
            while (ip < vInner.size()) {
                size_t iOpen = vInner.find(lowerCase ? "<influence" : "<INFLUENCE", ip);
                if (iOpen == std::string_view::npos) break;
                size_t iGt = vInner.find('>', iOpen);
                size_t iClose = vInner.find(lowerCase ? "</influence>" : "</INFLUENCE>", iOpen);
                if (iGt == std::string_view::npos || iClose == std::string_view::npos || iClose < iGt) break;
                std::string_view idAttr = ExtractXmlAttr(vInner.substr(iOpen, iGt - iOpen), "ID");
                int32_t bId = -1;
                if (!idAttr.empty()) std::from_chars(idAttr.data(), idAttr.data() + idAttr.size(), bId);
                std::vector<float> wNums;
                ParseXmlFloatsSimple(vInner.substr(iGt + 1, iClose - (iGt + 1)), wNums);
                if (bId >= 0 && !wNums.empty()) wv.infs.emplace_back(bId, wNums[0]);
                ip = iClose + 12;
            }
            allVerts.push_back(std::move(wv));
            ++smVertCnt;
        }

        // Parse <FACE VERTEXID="i0 i1 i2" .../>
        size_t fp = 0;
        while (fp < smBody.size()) {
            size_t fOpen = smBody.find(lowerCase ? "<face" : "<FACE", fp);
            if (fOpen == std::string_view::npos) break;
            size_t fGt = smBody.find('>', fOpen);
            if (fGt == std::string_view::npos) break;
            std::string_view fTag = smBody.substr(fOpen, fGt - fOpen + 1);
            fp = fGt + 1;

            std::string_view vidTxt = ExtractXmlAttr(fTag, "VERTEXID");
            std::vector<float> fIdx;
            if (!vidTxt.empty()) {
                ParseXmlFloatsSimple(vidTxt, fIdx);
            } else {
                size_t fClose = smBody.find(lowerCase ? "</face>" : "</FACE>", fp);
                if (fClose != std::string_view::npos) {
                    ParseXmlFloatsSimple(smBody.substr(fp, fClose - fp), fIdx);
                    fp = fClose + 7;
                }
            }
            if (fIdx.size() >= 3) {
                int32_t i0 = static_cast<int32_t>(fIdx[0]);
                int32_t i1 = static_cast<int32_t>(fIdx[1]);
                int32_t i2 = static_cast<int32_t>(fIdx[2]);
                if (i0 >= 0 && static_cast<uint32_t>(i0) < smVertCnt &&
                    i1 >= 0 && static_cast<uint32_t>(i1) < smVertCnt &&
                    i2 >= 0 && static_cast<uint32_t>(i2) < smVertCnt &&
                    i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.tris.push_back({
                        baseVert + static_cast<uint32_t>(i0),
                        baseVert + static_cast<uint32_t>(i1),
                        baseVert + static_cast<uint32_t>(i2),
                        {}
                    });
                }
            }
        }
    }

    if (allVerts.empty() || mesh.tris.empty()) return false;
    std::vector<CalBone> bones;
    LoadCompanionCalSkeleton(fullPath, bones);
    ApplyCal3DSkinning(bones, allVerts, mesh.vertices);
    mesh.formatName = L"Cal3D XML Skeletal Mesh (.xmf)";
    return true;
}

} // namespace Formats
