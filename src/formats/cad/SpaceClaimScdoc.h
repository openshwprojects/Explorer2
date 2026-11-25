#pragma once
// SpaceClaimScdoc.h — ANSYS SpaceClaim / Discovery 3D CAD Document (.scdoc) OPC reader.
//
// Parses Open Packaging Conventions (OPC) ZIP packages containing `SpaceClaim/Geometry/*.sab`,
// `*.sat`, `*.x_b`, `*.x_t`, and `*.brep` B-Rep geometry parts + optional `SpaceClaim/document.xml`
// component placement matrices.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseSpaceClaimScdoc(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace CadPkgDetail;
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

    bool isSpaceClaimPkg = false;
    std::unordered_map<std::wstring, std::array<double, 16>> partTransforms;

    for (const auto& ze : entries) {
        std::wstring lp = PathUtils::ToLower(ze.pathInArchive);
        for (wchar_t& c : lp) if (c == L'\\') c = L'/';
        if (lp.find(L"spaceclaim/") != std::wstring::npos || lp == L"[content_types].xml") {
            isSpaceClaimPkg = true;
        }
        if (!ze.isDirectory && (lp == L"spaceclaim/document.xml" || lp == L"document.xml")) {
            std::vector<uint8_t> docXml;
            if (ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, docXml, 16 * 1024 * 1024) && !docXml.empty()) {
                std::string_view sv(reinterpret_cast<const char*>(docXml.data()), docXml.size());
                if (sv.find("SpaceClaim") != std::string_view::npos) isSpaceClaimPkg = true;
                size_t pos = 0;
                while ((pos = sv.find("<Component", pos)) != std::string_view::npos) {
                    size_t closeTag = sv.find("</Component>", pos);
                    size_t gt = sv.find('>', pos);
                    if (gt == std::string_view::npos) break;
                    std::string_view span = sv.substr(pos, (closeTag != std::string_view::npos ? closeTag : gt) - pos);
                    std::string_view geomRef = CalCryDetail::ExtractXmlAttr(span.substr(0, gt - pos + 1), "Geometry");
                    if (geomRef.empty()) geomRef = CalCryDetail::ExtractXmlAttr(span.substr(0, gt - pos + 1), "Part");
                    size_t mTag = span.find("<Matrix>");
                    size_t mEnd = (mTag != std::string_view::npos) ? span.find("</Matrix>", mTag) : std::string_view::npos;
                    if (!geomRef.empty() && mTag != std::string_view::npos && mEnd != std::string_view::npos) {
                        std::vector<float> flts;
                        CalCryDetail::ParseXmlFloatsSimple(span.substr(mTag + 8, mEnd - (mTag + 8)), flts);
                        if (flts.size() >= 16) {
                            std::array<double, 16> m16{};
                            for (int i = 0; i < 16; ++i) m16[static_cast<size_t>(i)] = flts[static_cast<size_t>(i)];
                            std::wstring wKey;
                            for (char c : geomRef) wKey.push_back((c == '\\') ? L'/' : static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(c))));
                            partTransforms[PathUtils::GetFileName(wKey)] = m16;
                        }
                    }
                    pos = gt + 1;
                }
            }
        }
    }
    if (!isSpaceClaimPkg) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    for (const auto& ze : entries) {
        if (ze.isDirectory) continue;
        std::wstring ext = PathUtils::ToLower(PathUtils::GetExtension(ze.name));
        if (ext != L".sab" && ext != L".sat" && ext != L".x_t" && ext != L".x_b" && ext != L".brep" && ext != L".brp") {
            continue;
        }
        std::vector<uint8_t> partBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, partBytes, 64 * 1024 * 1024) || partBytes.empty()) continue;

        std::vector<uint8_t> inflated;
        const uint8_t* pay = partBytes.data();
        size_t payLen = partBytes.size();
        if (TryInflateCadStream(partBytes, inflated)) {
            pay = inflated.data();
            payLen = inflated.size();
        }

        LoadedMesh3D partMesh;
        if (TryDecodeCadPayloadBuffer(pay, payLen, partMesh) ||
            (pay != partBytes.data() && TryDecodeCadPayloadBuffer(partBytes.data(), partBytes.size(), partMesh)))
        {
            std::wstring baseLower = PathUtils::ToLower(PathUtils::GetFileName(ze.name));
            auto xit = partTransforms.find(baseLower);
            if (xit != partTransforms.end()) {
                AppendSubMeshMat4(partMesh, xit->second.data(), mesh);
            } else {
                AppendSubMeshPlaced(partMesh, {}, mesh);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"ANSYS SpaceClaim 3D CAD Document (.scdoc)";
    return true;
}

} // namespace Formats
