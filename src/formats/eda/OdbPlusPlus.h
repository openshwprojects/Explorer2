#pragma once
// OdbPlusPlus.h — ODB++ PCB Manufacturing Archive (.odb / .tgz) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailBoardFabEda {

struct OdbPkgDims {
    std::string name;
    float wMm = 2.4f;
    float hMm = 1.6f;
};

inline void ParseOdbEdaData(
    std::string_view sv,
    std::vector<OdbPkgDims>& outPkgs,
    std::set<std::string>& outNets)
{
    bool isMm = false;
    float uScale = 25.4f;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '#') continue;

        if (ln.rfind("UNITS=", 0) == 0) {
            isMm = DetailSiliconAndBusEda::ContainsAsciiI(ln.substr(6), "MM");
            uScale = isMm ? 1.0f : 25.4f;
        } else if (ln == "U MM") {
            isMm = true; uScale = 1.0f;
        } else if (ln == "U INCH" || ln == "U IN") {
            isMm = false; uScale = 25.4f;
        } else if (ln.rfind("NET ", 0) == 0 && ln.size() > 4) {
            std::string rest = DetailSiliconAndBusEda::TrimAsciiView(ln.substr(4));
            size_t sp = rest.find_first_of(" \t;");
            std::string netNm = (sp == std::string::npos) ? rest : rest.substr(0, sp);
            if (!netNm.empty() && netNm != "$NONE$") outNets.insert(netNm);
        } else if (ln.rfind("PKG ", 0) == 0 && ln.size() > 6) {
            // PKG <name> <pitch> <xmin> <ymin> <xmax> <ymax>
            char pkgNm[64] = {};
            float pitch = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            std::string body(ln.substr(4));
            size_t semi = body.find(';');
            if (semi != std::string::npos) body.resize(semi);
            if (sscanf_s(body.c_str(), "%63s %f %f %f %f %f",
                         pkgNm, static_cast<unsigned>(sizeof(pkgNm)),
                         &pitch, &x0, &y0, &x1, &y1) >= 6)
            {
                OdbPkgDims d;
                d.name = pkgNm;
                d.wMm = std::clamp(std::fabs(x1 - x0) * uScale, 0.6f, 80.0f);
                d.hMm = std::clamp(std::fabs(y1 - y0) * uScale, 0.6f, 80.0f);
                outPkgs.push_back(std::move(d));
            }
        }
    }
}

inline void ParseOdbFeaturesStreamV2(
    std::string_view sv,
    EdaLayerId layerId,
    bool isProfile,
    ParsedEdaBoard& out,
    size_t& validRecords)
{
    bool isMm = false;
    float uScale = 25.4f;
    std::unordered_map<int, DetailSiliconAndBusEda::OdbSymInfo> symTable;

    bool inPolygon = false;
    bool polyIsIsland = true;
    std::vector<std::pair<float, float>> polyPts;

    size_t pos = 0;
    while (pos < sv.size() && out.traces.size() < 60000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '#') continue;

        size_t semi = ln.find(';');
        std::string_view geomLn = (semi == std::string_view::npos) ? ln : ln.substr(0, semi);
        while (!geomLn.empty() && (geomLn.back() == ' ' || geomLn.back() == '\t')) geomLn.remove_suffix(1);
        if (geomLn.empty()) continue;

        if (geomLn.rfind("UNITS=", 0) == 0) {
            isMm = DetailSiliconAndBusEda::ContainsAsciiI(geomLn.substr(6), "MM");
            uScale = isMm ? 1.0f : 25.4f;
            continue;
        }
        if (geomLn == "U MM") { isMm = true; uScale = 1.0f; continue; }
        if (geomLn == "U INCH" || geomLn == "U IN") { isMm = false; uScale = 25.4f; continue; }

        if (geomLn[0] == '$' && geomLn.size() >= 3 && geomLn[1] >= '0' && geomLn[1] <= '9') {
            int idx = 0, consumed = 0;
            std::string tmp(geomLn.substr(1));
            if (sscanf_s(tmp.c_str(), "%d%n", &idx, &consumed) == 1 && consumed > 0) {
                symTable[idx] = DetailSiliconAndBusEda::ParseOdbSymbolDef(std::string_view(tmp).substr(consumed), isMm);
                ++validRecords;
            }
            continue;
        }

        if (geomLn[0] == 'L' && geomLn.size() > 4 && (geomLn[1] == ' ' || geomLn[1] == '\t')) {
            float xs = 0, ys = 0, xe = 0, ye = 0;
            int symIdx = 0;
            std::string tmp(geomLn.substr(2));
            int n = sscanf_s(tmp.c_str(), "%f %f %f %f %d", &xs, &ys, &xe, &ye, &symIdx);
            if (n >= 4) {
                float w = 0.25f;
                if (n >= 5 && symTable.count(symIdx)) w = symTable[symIdx].wMm;
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                out.traces.push_back({ xs * uScale, ys * uScale, xe * uScale, ye * uScale, w, lid, 1 });
                ++validRecords;
            }
        } else if (geomLn[0] == 'P' && geomLn.size() > 4 && (geomLn[1] == ' ' || geomLn[1] == '\t')) {
            // P <x> <y> <apt_def> [P|N] [dcode] [orient_def] [angle]
            float px = 0, py = 0, customAng = 0.0f;
            int symIdx = 0, dcode = 0, orient = 0;
            char pol = 'P';
            std::string tmp(geomLn.substr(2));
            int n = sscanf_s(tmp.c_str(), "%f %f %d %c %d %d %f",
                             &px, &py, &symIdx, &pol, 1, &dcode, &orient, &customAng);
            if (n >= 2) {
                DetailSiliconAndBusEda::OdbSymInfo si;
                si.wMm = 1.0f; si.hMm = 1.0f; si.shape = 0;
                if (n >= 3 && symTable.count(symIdx)) si = symTable[symIdx];
                float rotDeg = 0.0f;
                if (n >= 6) {
                    if (orient >= 0 && orient <= 3) rotDeg = static_cast<float>(orient * 90);
                    else if (orient >= 4 && orient <= 7) rotDeg = static_cast<float>((orient - 4) * 90);
                    else if ((orient == 8 || orient == 9) && n >= 7) rotDeg = customAng;
                }
                if (layerId == EdaLayerId::Drills) {
                    out.vias.push_back({ px * uScale, py * uScale, si.wMm * 1.75f, si.wMm, 0 });
                } else {
                    out.pads.push_back({ px * uScale, py * uScale, si.wMm, si.hMm, rotDeg, 0.0f, si.shape, layerId, L"", L"" });
                }
                ++validRecords;
            }
        } else if (geomLn[0] == 'A' && geomLn.size() > 8 && (geomLn[1] == ' ' || geomLn[1] == '\t')) {
            float xs = 0, ys = 0, xe = 0, ye = 0, xc = 0, yc = 0;
            int symIdx = 0;
            char pol = 'P', cwCh = 'Y';
            std::string tmp(geomLn.substr(2));
            int n = sscanf_s(tmp.c_str(), "%f %f %f %f %f %f %d %c %c",
                             &xs, &ys, &xe, &ye, &xc, &yc, &symIdx, &pol, 1, &cwCh, 1);
            if (n >= 6) {
                float w = (n >= 7 && symTable.count(symIdx)) ? symTable[symIdx].wMm : 0.25f;
                bool cw = (cwCh == 'Y' || cwCh == 'y' || cwCh == '1');
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                DetailSiliconAndBusEda::AddTessellatedArc(out, xs * uScale, ys * uScale, xe * uScale, ye * uScale,
                                                xc * uScale, yc * uScale, cw, w, lid, 1);
                ++validRecords;
            }
        } else if (geomLn.rfind("OB ", 0) == 0 || geomLn.rfind("OB\t", 0) == 0) {
            float bx = 0, by = 0;
            char polyType = 'I';
            std::string tmp(geomLn.substr(3));
            int n = sscanf_s(tmp.c_str(), "%f %f %c", &bx, &by, &polyType, 1);
            if (n >= 2) {
                polyPts.clear();
                polyPts.push_back({ bx * uScale, by * uScale });
                polyIsIsland = (polyType != 'H' && polyType != 'h');
                inPolygon = true;
                ++validRecords;
            }
        } else if (inPolygon && (geomLn.rfind("OS ", 0) == 0 || geomLn.rfind("OS\t", 0) == 0)) {
            float nx = 0, ny = 0;
            std::string tmp(geomLn.substr(3));
            if (sscanf_s(tmp.c_str(), "%f %f", &nx, &ny) == 2 && !polyPts.empty()) {
                float sx = nx * uScale, sy = ny * uScale;
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                out.traces.push_back({ polyPts.back().first, polyPts.back().second, sx, sy, isProfile ? 0.32f : 0.20f, lid, 0 });
                polyPts.push_back({ sx, sy });
            }
        } else if (inPolygon && (geomLn.rfind("OC ", 0) == 0 || geomLn.rfind("OC\t", 0) == 0)) {
            float xe = 0, ye = 0, xc = 0, yc = 0;
            char cwCh = 'Y';
            std::string tmp(geomLn.substr(3));
            if (sscanf_s(tmp.c_str(), "%f %f %f %f %c", &xe, &ye, &xc, &yc, &cwCh, 1) >= 4 && !polyPts.empty()) {
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                DetailSiliconAndBusEda::AddTessellatedArc(out, polyPts.back().first, polyPts.back().second,
                    xe * uScale, ye * uScale, xc * uScale, yc * uScale,
                    cwCh == 'Y' || cwCh == 'y', isProfile ? 0.32f : 0.20f, lid, 0);
                polyPts.push_back({ xe * uScale, ye * uScale });
            }
        } else if (inPolygon && geomLn.rfind("OE", 0) == 0) {
            if (polyPts.size() >= 3) {
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                if (std::hypot(polyPts.back().first - polyPts.front().first, polyPts.back().second - polyPts.front().second) > 1e-3f) {
                    out.traces.push_back({ polyPts.back().first, polyPts.back().second,
                                           polyPts.front().first, polyPts.front().second, isProfile ? 0.32f : 0.20f, lid, 0 });
                }
                if (!isProfile && polyIsIsland &&
                    (lid == EdaLayerId::TopCopper || lid == EdaLayerId::BottomCopper || lid == EdaLayerId::InnerCopper))
                {
                    HatchPolygonHorizontal(polyPts, lid, 0, out);
                }
            }
            polyPts.clear();
            inPolygon = false;
        }
    }
}

} // namespace DetailBoardFabEda

inline bool IsLikelyOdbPlusPlusArchiveBytes(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    if (size >= 10 && data[0] == 0x1F && data[1] == 0x8B) {
        std::vector<uint8_t> gunz;
        if (!ZipDeflate::InflateGzip(data, size, gunz, 16 * 1024 * 1024) || gunz.empty()) return false;
        return DetailBoardFabEda::TarBufferHasOdbSignature(gunz.data(), gunz.size());
    }
    if (DetailSiliconAndBusEda::LooksLikeTarBlock(data, size)) {
        return DetailBoardFabEda::TarBufferHasOdbSignature(data, size);
    }
    if (size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries)) return false;
        for (const auto& e : entries) {
            std::string low = PathUtils::ToLowerAscii(PathUtils::WideToUtf8(e.pathInArchive));
            for (char& ch : low) if (ch == '\\') ch = '/';
            if (DetailBoardFabEda::HasOdbPathSignature(low)) return true;
        }
    }
    return false;
}

inline bool ParseOdbPlusPlusBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ParsedEdaBoard& out)
{
    if (!data || size < 12) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (extLower != L".odb" && extLower != L".tgz") return false;

    std::vector<std::pair<std::string, std::string>> vfiles;
    if (size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
        if (!IsLikelyOdbPlusPlusArchiveBytes(data, size)) return false;
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;
        for (const auto& e : entries) {
            if (e.isDirectory || e.uncompressedSize == 0 || e.uncompressedSize > 32 * 1024 * 1024) continue;
            std::string low = PathUtils::ToLowerAscii(PathUtils::WideToUtf8(e.pathInArchive));
            for (char& ch : low) if (ch == '\\') ch = '/';
            if (low.find("matrix") != std::string::npos || low.find("profile") != std::string::npos ||
                low.find("features") != std::string::npos || low.find("components") != std::string::npos ||
                low.find("eda/data") != std::string::npos)
            {
                std::vector<uint8_t> rawEntry;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, rawEntry, 32 * 1024 * 1024) && !rawEntry.empty()) {
                    vfiles.push_back({ low, DetailSiliconAndBusEda::UnpackMaybeCompressedText(rawEntry.data(), rawEntry.size()) });
                }
            }
        }
    } else if (size >= 10 && data[0] == 0x1F && data[1] == 0x8B) {
        std::vector<uint8_t> gunz;
        if (!ZipDeflate::InflateGzip(data, size, gunz, 64 * 1024 * 1024) || gunz.empty()) return false;
        if (DetailSiliconAndBusEda::LooksLikeTarBlock(gunz.data(), gunz.size())) {
            if (!DetailBoardFabEda::TarBufferHasOdbSignature(gunz.data(), gunz.size())) return false; // Reject generic .tgz!
            DetailBoardFabEda::ExtractOdbTarEntriesFull(gunz.data(), gunz.size(), vfiles);
        } else if (extLower == L".odb") {
            vfiles.push_back({ "layers/top/features", std::string(reinterpret_cast<const char*>(gunz.data()), gunz.size()) });
        } else {
            return false;
        }
    } else if (DetailSiliconAndBusEda::LooksLikeTarBlock(data, size)) {
        if (!DetailBoardFabEda::TarBufferHasOdbSignature(data, size)) return false;
        DetailBoardFabEda::ExtractOdbTarEntriesFull(data, size, vfiles);
    } else if (extLower == L".odb") {
        bool hasOdbMarker = (sv.find("UNITS=") != std::string_view::npos ||
                             sv.find("\n$0 ") != std::string_view::npos ||
                             sv.rfind("$0 ", 0) == 0 ||
                             sv.find("CMP ") != std::string_view::npos ||
                             sv.find("\nOB ") != std::string_view::npos ||
                             sv.rfind("OB ", 0) == 0);
        if (!hasOdbMarker) return false;
        if (sv.find("CMP ") != std::string_view::npos) {
            vfiles.push_back({ "steps/pcb/layers/comp_+_top/components", std::string(sv) });
        }
        vfiles.push_back({ "steps/pcb/layers/top/features", std::string(sv) });
    } else {
        return false;
    }

    if (vfiles.empty()) return false;

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = (extLower == L".tgz")
        ? L"ODB++ PCB Manufacturing Tarball (.tgz)"
        : L"ODB++ PCB Manufacturing Archive (.odb)";

    std::unordered_map<std::string, EdaLayerId> matrixLayerMap;
    std::vector<DetailBoardFabEda::OdbPkgDims> pkgDims;
    std::set<std::string> edaNets;

    for (const auto& vf : vfiles) {
        if (vf.first.find("matrix") != std::string::npos) {
            DetailSiliconAndBusEda::ParseOdbMatrixLayers(vf.second, matrixLayerMap);
        } else if (vf.first.find("eda/data") != std::string::npos) {
            DetailBoardFabEda::ParseOdbEdaData(vf.second, pkgDims, edaNets);
        }
    }

    size_t validRecords = 0;
    for (const auto& vf : vfiles) {
        const std::string& path = vf.first;
        if (path.find("matrix") != std::string::npos || path.find("eda/data") != std::string::npos) continue;
        if (path.find("components") != std::string::npos) {
            bool isBot = (path.find("comp_+_bot") != std::string::npos || path.find("bottom") != std::string::npos);
            size_t before = out.components.size();
            DetailSiliconAndBusEda::ParseOdbComponentsStream(vf.second, isBot, out, validRecords);
            for (size_t ci = before; ci < out.components.size(); ++ci) {
                auto& comp = out.components[ci];
                if (comp.footprint.rfind(L"PKG_", 0) == 0) {
                    size_t pidx = static_cast<size_t>(std::wcstoul(comp.footprint.c_str() + 4, nullptr, 10));
                    if (pidx < pkgDims.size()) {
                        comp.footprint = PathUtils::Utf8ToWide(pkgDims[pidx].name);
                        comp.widthMm = pkgDims[pidx].wMm;
                        comp.lengthMm = pkgDims[pidx].hMm;
                        comp.category = ClassifyComponent(
                            comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                    }
                }
            }
        } else if (path.find("profile") != std::string::npos) {
            DetailBoardFabEda::ParseOdbFeaturesStreamV2(vf.second, EdaLayerId::EdgeCuts, true, out, validRecords);
        } else if (path.find("features") != std::string::npos) {
            std::string layerName = "top";
            size_t lpos = path.find("layers/");
            if (lpos != std::string::npos) {
                size_t s1 = lpos + 7;
                size_t s2 = path.find('/', s1);
                if (s2 != std::string::npos && s2 > s1) layerName = path.substr(s1, s2 - s1);
            }
            if (layerName.find("mask") != std::string::npos || layerName.find("paste") != std::string::npos) continue;
            auto it = matrixLayerMap.find(layerName);
            EdaLayerId lid = (it != matrixLayerMap.end()) ? it->second : DetailSiliconAndBusEda::ClassifyIpcLayerName(layerName);
            DetailBoardFabEda::ParseOdbFeaturesStreamV2(vf.second, lid, false, out, validRecords);
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, !edaNets.empty() ? edaNets.size() : out.components.size());
    return true;
}

} // namespace Formats
