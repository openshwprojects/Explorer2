#pragma once
// Ipc2581OdbFritzing.h — #69 IPC-2581 rev B/C (.cvg/.ipc2581), ODB++ Manufacturing Archives (.odb/.tgz),
//                        Fritzing Sketch & Part Archives (.fzz/.fzpz/.fz/.fzp), and gEDA / PCB-rnd (.pcb)
// NOTE: Included inside `namespace Explorer2` in src/EdaViewer.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailMfg69 {

inline void HatchPolygonHorizontal(
    const std::vector<std::pair<float, float>>& pts,
    EdaLayerId layer,
    int netId,
    ParsedEdaBoard& out)
{
    if (pts.size() < 3 || out.traces.size() > 55000) return;
    float minY = pts[0].second, maxY = pts[0].second;
    float minX = pts[0].first,  maxX = pts[0].first;
    for (const auto& p : pts) {
        minX = std::min(minX, p.first);  maxX = std::max(maxX, p.first);
        minY = std::min(minY, p.second); maxY = std::max(maxY, p.second);
    }
    const float spanY = maxY - minY;
    if (spanY < 0.05f || (maxX - minX) < 0.05f) return;

    const float pitch = std::clamp(spanY / 20.0f, 0.28f, 1.25f);
    const float strokeW = pitch * 0.65f;
    std::vector<float> xs;
    xs.reserve(16);
    for (float y = minY + pitch * 0.5f; y < maxY - 1e-4f && out.traces.size() < 58000; y += pitch) {
        xs.clear();
        for (size_t i = 0; i < pts.size(); ++i) {
            const auto& a = pts[i];
            const auto& b = pts[(i + 1) % pts.size()];
            if ((a.second <= y && b.second > y) || (b.second <= y && a.second > y)) {
                float t = (y - a.second) / (b.second - a.second);
                xs.push_back(a.first + t * (b.first - a.first));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            if (xs[k + 1] - xs[k] > 0.02f) {
                out.traces.push_back({ xs[k], y, xs[k + 1], y, strokeW, layer, netId });
            }
        }
    }
}

inline bool HasOdbPathSignature(std::string_view lowPath) {
    return lowPath.find("matrix/matrix") != std::string_view::npos ||
           (lowPath.find("steps/") != std::string_view::npos &&
            (lowPath.find("/layers/") != std::string_view::npos ||
             lowPath.find("/profile") != std::string_view::npos ||
             lowPath.find("/eda/data") != std::string_view::npos));
}

inline bool TarBufferHasOdbSignature(const uint8_t* tarData, size_t tarSize) {
    if (!DetailEda100::LooksLikeTarBlock(tarData, tarSize)) return false;
    size_t pos = 0;
    std::string gnuLong;
    int entriesChecked = 0;
    while (pos + 512 <= tarSize && entriesChecked < 512) {
        const uint8_t* hdr = tarData + pos;
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;
        uint64_t fsize = 0;
        for (size_t i = 124; i < 136; ++i) {
            if (hdr[i] >= '0' && hdr[i] <= '7') fsize = (fsize << 3) | static_cast<uint64_t>(hdr[i] - '0');
        }
        char tf = static_cast<char>(hdr[156]);
        std::string name;
        if (!gnuLong.empty()) {
            name = std::move(gnuLong);
            gnuLong.clear();
        } else {
            size_t nl = 0;
            while (nl < 100 && hdr[nl] != 0) ++nl;
            name.assign(reinterpret_cast<const char*>(hdr), nl);
            if (std::memcmp(hdr + 257, "ustar", 5) == 0 && hdr[345] != 0) {
                size_t pl = 0;
                while (pl < 155 && hdr[345 + pl] != 0) ++pl;
                name = std::string(reinterpret_cast<const char*>(hdr + 345), pl) + "/" + name;
            }
        }
        pos += 512;
        if (fsize > tarSize - pos) break;
        if (tf == 'L' && fsize > 0 && fsize < 4096) {
            gnuLong.assign(reinterpret_cast<const char*>(tarData + pos), static_cast<size_t>(fsize));
            while (!gnuLong.empty() && gnuLong.back() == '\0') gnuLong.pop_back();
        } else {
            std::string low = PathUtils::ToLowerAscii(name);
            for (char& ch : low) if (ch == '\\') ch = '/';
            if (HasOdbPathSignature(low)) return true;
        }
        size_t padded = static_cast<size_t>((fsize + 511ULL) & ~511ULL);
        if (padded > tarSize - pos) break;
        pos += padded;
        ++entriesChecked;
    }
    return false;
}

inline void ExtractOdbTarEntriesFull(
    const uint8_t* tarData,
    size_t tarSize,
    std::vector<std::pair<std::string, std::string>>& outFiles)
{
    size_t pos = 0;
    std::string gnuLongName;
    while (pos + 512 <= tarSize && outFiles.size() < 1500) {
        const uint8_t* hdr = tarData + pos;
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;

        uint64_t fsize = 0;
        for (size_t i = 124; i < 136; ++i) {
            if (hdr[i] >= '0' && hdr[i] <= '7') fsize = (fsize << 3) | static_cast<uint64_t>(hdr[i] - '0');
        }
        char typeflag = static_cast<char>(hdr[156]);
        std::string name;
        if (!gnuLongName.empty()) {
            name = std::move(gnuLongName);
            gnuLongName.clear();
        } else {
            size_t nlen = 0;
            while (nlen < 100 && hdr[nlen] != 0) ++nlen;
            name.assign(reinterpret_cast<const char*>(hdr), nlen);
            if (std::memcmp(hdr + 257, "ustar", 5) == 0 && hdr[345] != 0) {
                size_t plen = 0;
                while (plen < 155 && hdr[345 + plen] != 0) ++plen;
                name = std::string(reinterpret_cast<const char*>(hdr + 345), plen) + "/" + name;
            }
        }
        pos += 512;
        if (fsize > tarSize - pos) break;

        if (typeflag == 'L' && fsize > 0 && fsize < 4096) {
            gnuLongName.assign(reinterpret_cast<const char*>(tarData + pos), static_cast<size_t>(fsize));
            while (!gnuLongName.empty() && gnuLongName.back() == '\0') gnuLongName.pop_back();
        } else if ((typeflag == '0' || typeflag == '\0') && fsize > 0 && fsize <= 32 * 1024 * 1024) {
            std::string low = PathUtils::ToLowerAscii(name);
            for (char& ch : low) if (ch == '\\') ch = '/';
            if (low.find("matrix") != std::string::npos ||
                low.find("profile") != std::string::npos ||
                low.find("features") != std::string::npos ||
                low.find("components") != std::string::npos ||
                low.find("eda/data") != std::string::npos ||
                low.find("misc/info") != std::string::npos)
            {
                outFiles.push_back({ low, DetailEda100::UnpackMaybeCompressedText(tarData + pos, static_cast<size_t>(fsize)) });
            }
        }
        size_t padded = static_cast<size_t>((fsize + 511ULL) & ~511ULL);
        if (padded > tarSize - pos) break;
        pos += padded;
    }
}

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
            isMm = DetailEda100::ContainsAsciiI(ln.substr(6), "MM");
            uScale = isMm ? 1.0f : 25.4f;
        } else if (ln == "U MM") {
            isMm = true; uScale = 1.0f;
        } else if (ln == "U INCH" || ln == "U IN") {
            isMm = false; uScale = 25.4f;
        } else if (ln.rfind("NET ", 0) == 0 && ln.size() > 4) {
            std::string rest = DetailEda100::TrimAsciiView(ln.substr(4));
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
    std::unordered_map<int, DetailEda100::OdbSymInfo> symTable;

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
            isMm = DetailEda100::ContainsAsciiI(geomLn.substr(6), "MM");
            uScale = isMm ? 1.0f : 25.4f;
            continue;
        }
        if (geomLn == "U MM") { isMm = true; uScale = 1.0f; continue; }
        if (geomLn == "U INCH" || geomLn == "U IN") { isMm = false; uScale = 25.4f; continue; }

        if (geomLn[0] == '$' && geomLn.size() >= 3 && geomLn[1] >= '0' && geomLn[1] <= '9') {
            int idx = 0, consumed = 0;
            std::string tmp(geomLn.substr(1));
            if (sscanf_s(tmp.c_str(), "%d%n", &idx, &consumed) == 1 && consumed > 0) {
                symTable[idx] = DetailEda100::ParseOdbSymbolDef(std::string_view(tmp).substr(consumed), isMm);
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
                DetailEda100::OdbSymInfo si;
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
                DetailEda100::AddTessellatedArc(out, xs * uScale, ys * uScale, xe * uScale, ye * uScale,
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
                DetailEda100::AddTessellatedArc(out, polyPts.back().first, polyPts.back().second,
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

// ============================================================================
// Upgraded IPC-2581 Rev B / C XML Parser (.cvg / .ipc2581)
// ============================================================================
inline bool ParseIpc2581RevBC(std::string_view sv, ParsedEdaBoard& out) {
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }
    if (sv.find("<IPC-2581") == std::string_view::npos && sv.find("<Ecad") == std::string_view::npos) {
        return false;
    }
    // Strict closing-tag check so truncated XML files are rejected cleanly
    if (sv.find("</IPC-2581>") == std::string_view::npos && sv.find("</Ecad>") == std::string_view::npos &&
        sv.find(":IPC-2581>") == std::string_view::npos) {
        return false;
    }

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"IPC-2581 Digital Twin PCB (.cvg / .ipc2581)";

    float unitScale = 1.0f; // MILLIMETER default
    struct StdPrim { float w = 1.0f; float h = 1.0f; uint8_t shape = 0; };
    struct PkgBox { float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f; bool valid = false; };

    std::unordered_map<std::string, float> lineDescMap;
    std::unordered_map<std::string, StdPrim> stdPrimMap;
    std::unordered_map<std::string, float> padstackHoleMap;
    std::unordered_map<std::string, PkgBox> pkgBoxMap;
    std::unordered_map<std::string, EdaLayerId> layerMap;
    std::set<std::string> netNames;

    std::string curEntryLineId, curEntryStdId, curPadstackDefId, curPackageName, revStr;
    EdaLayerId activeLayer = EdaLayerId::TopCopper;
    float activeLineWidth = 0.25f;
    int activeNetId = 0;
    bool setIsVia = false;

    bool inProfile = false;
    bool inPoly = false;
    bool inCutout = false;
    std::vector<std::pair<float, float>> polyPts;
    EdaLayerId polyLayer = EdaLayerId::EdgeCuts;

    bool inComponent = false;
    EdaComponent3D curComp;
    bool compHasLoc = false;

    bool inPad = false;
    bool padIsVia = false;
    EdaPadPrim curPad;
    bool padHasLoc = false;

    auto trackPkgPt = [&](float xMm, float yMm) {
        if (curPackageName.empty()) return;
        PkgBox& b = pkgBoxMap[curPackageName];
        b.minX = std::min(b.minX, xMm); b.maxX = std::max(b.maxX, xMm);
        b.minY = std::min(b.minY, yMm); b.maxY = std::max(b.maxY, yMm);
        b.valid = true;
    };

    size_t pos = 0;
    while (pos < sv.size() && out.traces.size() < 55000) {
        size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos) break;
        if (lt + 4 <= sv.size() && sv.substr(lt, 4) == "<!--") {
            size_t ce = sv.find("-->", lt + 4);
            if (ce == std::string_view::npos) return false;
            pos = ce + 3;
            continue;
        }
        size_t gt = sv.find('>', lt + 1);
        if (gt == std::string_view::npos) return false;
        std::string_view rawTag = sv.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        if (rawTag.empty() || rawTag[0] == '?' || rawTag[0] == '!') continue;

        bool isClosing = (rawTag[0] == '/');
        bool selfClosing = (!isClosing && rawTag.back() == '/');
        std::string_view tag = isClosing ? rawTag.substr(1) : (selfClosing ? rawTag.substr(0, rawTag.size() - 1) : rawTag);
        while (!tag.empty() && (tag.front() == ' ' || tag.front() == '\t')) tag.remove_prefix(1);

        size_t nameEnd = tag.find_first_of(" \t\r\n/");
        std::string_view tname = (nameEnd == std::string_view::npos) ? tag : tag.substr(0, nameEnd);
        size_t colon = tname.find(':');
        if (colon != std::string_view::npos) tname = tname.substr(colon + 1);

        if (isClosing) {
            if (tname == "EntryLineDesc") curEntryLineId.clear();
            else if (tname == "EntryStandard") curEntryStdId.clear();
            else if (tname == "PadstackDef" || tname == "PadStackDef") curPadstackDefId.clear();
            else if (tname == "Package") curPackageName.clear();
            else if (tname == "Set") setIsVia = false;
            else if (tname == "Profile") inProfile = false;
            else if (tname == "Cutout") inCutout = false;
            else if (tname == "Polygon") {
                if (curPackageName.empty() && polyPts.size() >= 3) {
                    if (std::hypot(polyPts.back().first - polyPts.front().first, polyPts.back().second - polyPts.front().second) > 1e-3f) {
                        out.traces.push_back({ polyPts.back().first, polyPts.back().second,
                                               polyPts.front().first, polyPts.front().second,
                                               (polyLayer == EdaLayerId::EdgeCuts) ? 0.30f : 0.20f, polyLayer, activeNetId });
                    }
                    if (!inProfile && !inCutout &&
                        (polyLayer == EdaLayerId::TopCopper || polyLayer == EdaLayerId::BottomCopper || polyLayer == EdaLayerId::InnerCopper))
                    {
                        HatchPolygonHorizontal(polyPts, polyLayer, activeNetId, out);
                    }
                }
                polyPts.clear();
                inPoly = false;
            } else if (tname == "Component") {
                if (inComponent && (!curComp.refDes.empty() || compHasLoc)) {
                    std::string pkgKey = PathUtils::WideToUtf8(curComp.footprint);
                    if (pkgBoxMap.count(pkgKey) && pkgBoxMap[pkgKey].valid) {
                        curComp.widthMm  = std::clamp(pkgBoxMap[pkgKey].maxX - pkgBoxMap[pkgKey].minX, 0.8f, 60.0f);
                        curComp.lengthMm = std::clamp(pkgBoxMap[pkgKey].maxY - pkgBoxMap[pkgKey].minY, 0.8f, 60.0f);
                    }
                    curComp.category = ClassifyComponent(
                        curComp.refDes, curComp.footprint, curComp.widthMm, curComp.lengthMm, curComp.heightMm, curComp.pinCount);
                    out.components.push_back(curComp);
                }
                inComponent = false;
            } else if (tname == "Pad" || tname == "Via") {
                if (inPad && padHasLoc) {
                    if (padIsVia) {
                        float d = std::max(0.2f, curPad.drill > 0.0f ? curPad.drill : curPad.w * 0.5f);
                        out.vias.push_back({ curPad.x, curPad.y, std::max(curPad.w, d * 1.6f), d, activeNetId });
                    } else {
                        out.pads.push_back(curPad);
                    }
                }
                inPad = false;
                padIsVia = false;
            }
            continue;
        }

        if (tname == "IPC-2581") {
            std::string_view rv;
            if (GetXmlAttr(tag, "revision", rv)) revStr = std::string(rv);
        } else if (tname == "CadHeader" || tname == "Units") {
            std::string_view u;
            if (GetXmlAttr(tag, "units", u)) {
                if (DetailEda100::ContainsAsciiI(u, "INCH")) unitScale = 25.4f;
                else if (DetailEda100::ContainsAsciiI(u, "MICRON")) unitScale = 0.001f;
                else if (DetailEda100::ContainsAsciiI(u, "MIL") && !DetailEda100::ContainsAsciiI(u, "MILLI")) unitScale = 0.0254f;
                else unitScale = 1.0f;
            }
        } else if (tname == "EntryLineDesc") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && !selfClosing) curEntryLineId = std::string(id);
        } else if (tname == "LineDesc") {
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            if (lw > 0.0f) {
                float wMm = std::max(0.05f, lw * unitScale);
                if (!curEntryLineId.empty()) lineDescMap[curEntryLineId] = wMm;
                activeLineWidth = wMm;
            }
        } else if (tname == "LineDescRef") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && lineDescMap.count(std::string(id))) {
                activeLineWidth = lineDescMap[std::string(id)];
            }
        } else if (tname == "EntryStandard") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && !selfClosing) curEntryStdId = std::string(id);
        } else if (!curEntryStdId.empty() &&
                   (tname == "Circle" || tname == "RectCenter" || tname == "RectRound" ||
                    tname == "Oval" || tname == "Square" || tname == "Donut" || tname == "Thermal"))
        {
            StdPrim sp;
            if (tname == "Circle" || tname == "Donut" || tname == "Thermal") {
                float d = GetXmlFloat(tag, "diameter", GetXmlFloat(tag, "outerDiameter", 1.0f)) * unitScale;
                sp = { std::max(0.15f, d), std::max(0.15f, d), 0 };
            } else if (tname == "Square") {
                float s = std::max(0.15f, GetXmlFloat(tag, "size", 1.0f) * unitScale);
                sp = { s, s, 1 };
            } else {
                float w = std::max(0.15f, GetXmlFloat(tag, "width", 1.0f) * unitScale);
                float h = std::max(0.15f, GetXmlFloat(tag, "height", 1.0f) * unitScale);
                uint8_t sh = (tname == "Oval") ? 3 : ((tname == "RectRound") ? 2 : 1);
                sp = { w, h, sh };
            }
            stdPrimMap[curEntryStdId] = sp;
        } else if (tname == "PadstackDef" || tname == "PadStackDef") {
            std::string_view nm;
            if (GetXmlAttr(tag, "name", nm) && !selfClosing) curPadstackDefId = std::string(nm);
        } else if (!curPadstackDefId.empty() && (tname == "PadstackHoleDef" || tname == "PadStackHoleDef")) {
            float hd = GetXmlFloat(tag, "diameter", 0.0f) * unitScale;
            if (hd > 0.05f) padstackHoleMap[curPadstackDefId] = hd;
        } else if (tname == "Package") {
            std::string_view nm;
            if (GetXmlAttr(tag, "name", nm) && !selfClosing) curPackageName = std::string(nm);
        } else if (tname == "Layer") {
            std::string_view nm, fn, sd;
            if (GetXmlAttr(tag, "name", nm)) {
                GetXmlAttr(tag, "layerFunction", fn);
                GetXmlAttr(tag, "side", sd);
                layerMap[std::string(nm)] = DetailEda100::ClassifyIpcLayerName(nm, fn, sd);
            }
        } else if (tname == "Profile") {
            if (!selfClosing) inProfile = true;
        } else if (tname == "Cutout") {
            if (!selfClosing) inCutout = true;
        } else if (tname == "Polygon") {
            if (!selfClosing) {
                inPoly = true;
                polyPts.clear();
                polyLayer = inProfile ? EdaLayerId::EdgeCuts : activeLayer;
            }
        } else if (inPoly && tname == "PolyBegin") {
            float px = GetXmlFloat(tag, "x") * unitScale;
            float py = GetXmlFloat(tag, "y") * unitScale;
            if (!curPackageName.empty()) trackPkgPt(px, py);
            else polyPts.push_back({ px, py });
        } else if (inPoly && tname == "PolyStepSegment") {
            float nx = GetXmlFloat(tag, "x") * unitScale;
            float ny = GetXmlFloat(tag, "y") * unitScale;
            if (!curPackageName.empty()) {
                trackPkgPt(nx, ny);
            } else if (!polyPts.empty()) {
                float w = (polyLayer == EdaLayerId::EdgeCuts) ? 0.30f : std::max(0.12f, activeLineWidth);
                out.traces.push_back({ polyPts.back().first, polyPts.back().second, nx, ny, w, polyLayer, activeNetId });
                polyPts.push_back({ nx, ny });
            }
        } else if (inPoly && tname == "PolyStepCurve") {
            float nx = GetXmlFloat(tag, "x") * unitScale;
            float ny = GetXmlFloat(tag, "y") * unitScale;
            float cx = GetXmlFloat(tag, "centerX") * unitScale;
            float cy = GetXmlFloat(tag, "centerY") * unitScale;
            std::string_view cwSv;
            bool cw = GetXmlAttr(tag, "clockwise", cwSv) && DetailEda100::ContainsAsciiI(cwSv, "true");
            if (!curPackageName.empty()) {
                trackPkgPt(nx, ny);
            } else if (!polyPts.empty()) {
                float w = (polyLayer == EdaLayerId::EdgeCuts) ? 0.30f : std::max(0.12f, activeLineWidth);
                DetailEda100::AddTessellatedArc(out, polyPts.back().first, polyPts.back().second, nx, ny, cx, cy, cw, w, polyLayer, activeNetId);
                polyPts.push_back({ nx, ny });
            }
        } else if (tname == "LayerFeature") {
            std::string_view lref;
            if (GetXmlAttr(tag, "layerRef", lref)) {
                auto it = layerMap.find(std::string(lref));
                activeLayer = (it != layerMap.end()) ? it->second : DetailEda100::ClassifyIpcLayerName(lref);
            }
        } else if (tname == "Set" || tname == "LogicalNet") {
            std::string_view netSv, usageSv;
            if (GetXmlAttr(tag, "padUsage", usageSv)) {
                setIsVia = DetailEda100::ContainsAsciiI(usageSv, "VIA");
            }
            if (GetXmlAttr(tag, "net", netSv) && !netSv.empty() && netSv != "No Net") {
                netNames.insert(std::string(netSv));
                activeNetId = static_cast<int>(netNames.size());
            } else if (tname == "LogicalNet" && GetXmlAttr(tag, "name", netSv) && !netSv.empty()) {
                netNames.insert(std::string(netSv));
            }
        } else if (tname == "Line" && curPackageName.empty()) {
            float x1 = GetXmlFloat(tag, "startX") * unitScale;
            float y1 = GetXmlFloat(tag, "startY") * unitScale;
            float x2 = GetXmlFloat(tag, "endX") * unitScale;
            float y2 = GetXmlFloat(tag, "endY") * unitScale;
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            float w = (lw > 0.0f) ? (lw * unitScale) : activeLineWidth;
            EdaLayerId lid = inProfile ? EdaLayerId::EdgeCuts : activeLayer;
            out.traces.push_back({ x1, y1, x2, y2, std::max(0.08f, w), lid, activeNetId });
        } else if (tname == "Arc" && curPackageName.empty()) {
            float xs = GetXmlFloat(tag, "startX") * unitScale;
            float ys = GetXmlFloat(tag, "startY") * unitScale;
            float xe = GetXmlFloat(tag, "endX") * unitScale;
            float ye = GetXmlFloat(tag, "endY") * unitScale;
            float xc = GetXmlFloat(tag, "centerX") * unitScale;
            float yc = GetXmlFloat(tag, "centerY") * unitScale;
            std::string_view cwSv;
            bool cw = GetXmlAttr(tag, "clockwise", cwSv) && DetailEda100::ContainsAsciiI(cwSv, "true");
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            float w = (lw > 0.0f) ? (lw * unitScale) : activeLineWidth;
            DetailEda100::AddTessellatedArc(out, xs, ys, xe, ye, xc, yc, cw, std::max(0.08f, w),
                inProfile ? EdaLayerId::EdgeCuts : activeLayer, activeNetId);
        } else if ((tname == "Pad" || tname == "Via") && curPackageName.empty() && curPadstackDefId.empty()) {
            curPad = EdaPadPrim{};
            curPad.w = 1.0f; curPad.h = 1.0f; curPad.shape = 1;
            curPad.layer = (activeLayer == EdaLayerId::Drills) ? EdaLayerId::PadsThrough : activeLayer;
            curPad.rotDeg = GetXmlFloat(tag, "rotation", 0.0f);
            padIsVia = (tname == "Via") || setIsVia;
            std::string_view psRef;
            if (GetXmlAttr(tag, "padstackDefRef", psRef)) {
                curPad.pinName = PathUtils::Utf8ToWide(psRef);
                auto itH = padstackHoleMap.find(std::string(psRef));
                if (itH != padstackHoleMap.end()) {
                    curPad.drill = itH->second;
                    curPad.layer = EdaLayerId::PadsThrough;
                }
            }
            std::string_view xSv, ySv;
            if (GetXmlAttr(tag, "x", xSv) && GetXmlAttr(tag, "y", ySv)) {
                curPad.x = GetXmlFloat(tag, "x") * unitScale;
                curPad.y = GetXmlFloat(tag, "y") * unitScale;
                padHasLoc = true;
            } else {
                padHasLoc = false;
            }
            if (selfClosing) {
                if (padHasLoc) {
                    if (padIsVia) out.vias.push_back({ curPad.x, curPad.y, curPad.w, std::max(0.25f, curPad.drill), activeNetId });
                    else out.pads.push_back(curPad);
                }
                padIsVia = false;
            } else {
                inPad = true;
            }
        } else if (inPad && tname == "StandardPrimitiveRef") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && stdPrimMap.count(std::string(id))) {
                const auto& sp = stdPrimMap[std::string(id)];
                curPad.w = sp.w; curPad.h = sp.h; curPad.shape = sp.shape;
            }
        } else if (inPad && tname == "PinRef") {
            std::string_view pinSv;
            if (GetXmlAttr(tag, "pin", pinSv)) curPad.pinName = PathUtils::Utf8ToWide(pinSv);
        } else if (tname == "Hole" && curPackageName.empty()) {
            float d = std::max(0.15f, GetXmlFloat(tag, "diameter", 0.4f) * unitScale);
            std::string_view plSv;
            bool nonPlated = GetXmlAttr(tag, "platingStatus", plSv) && DetailEda100::ContainsAsciiI(plSv, "NONPLATED");
            if (inPad) {
                curPad.drill = d;
                curPad.layer = EdaLayerId::PadsThrough;
            } else {
                float hx = GetXmlFloat(tag, "x") * unitScale;
                float hy = GetXmlFloat(tag, "y") * unitScale;
                out.vias.push_back({ hx, hy, nonPlated ? d * 1.15f : d * 1.8f, d, activeNetId });
            }
        } else if (tname == "Component") {
            curComp = EdaComponent3D{};
            compHasLoc = false;
            std::string_view refSv, partSv, pkgSv, lyrSv;
            if (GetXmlAttr(tag, "refDes", refSv)) curComp.refDes = PathUtils::Utf8ToWide(refSv);
            if (GetXmlAttr(tag, "part", partSv)) curComp.value = PathUtils::Utf8ToWide(partSv);
            if (GetXmlAttr(tag, "packageRef", pkgSv)) curComp.footprint = PathUtils::Utf8ToWide(pkgSv);
            if (GetXmlAttr(tag, "layerRef", lyrSv)) {
                EdaLayerId cl = DetailEda100::ClassifyIpcLayerName(lyrSv);
                curComp.bottomSide = (cl == EdaLayerId::BottomCopper || cl == EdaLayerId::BottomSilk);
            }
            curComp.rotDeg = GetXmlFloat(tag, "rotation", 0.0f);
            std::string_view xSv, ySv;
            if (GetXmlAttr(tag, "x", xSv) && GetXmlAttr(tag, "y", ySv)) {
                curComp.x = GetXmlFloat(tag, "x") * unitScale;
                curComp.y = GetXmlFloat(tag, "y") * unitScale;
                compHasLoc = true;
            }
            if (selfClosing) {
                if (!curComp.refDes.empty() && compHasLoc) {
                    curComp.category = ClassifyComponent(
                        curComp.refDes, curComp.footprint, curComp.widthMm, curComp.lengthMm, curComp.heightMm, curComp.pinCount);
                    out.components.push_back(curComp);
                }
            } else {
                inComponent = true;
            }
        } else if (tname == "Location") {
            float lx = GetXmlFloat(tag, "x") * unitScale;
            float ly = GetXmlFloat(tag, "y") * unitScale;
            if (inPad) { curPad.x = lx; curPad.y = ly; padHasLoc = true; }
            else if (inComponent) { curComp.x = lx; curComp.y = ly; compHasLoc = true; }
        } else if (tname == "Xform") {
            float rot = GetXmlFloat(tag, "rotation", -9999.0f);
            std::string_view mirSv;
            bool mir = GetXmlAttr(tag, "mirror", mirSv) && DetailEda100::ContainsAsciiI(mirSv, "true");
            if (inPad && rot > -9000.0f) curPad.rotDeg = rot;
            if (inComponent) {
                if (rot > -9000.0f) curComp.rotDeg = rot;
                if (mir) curComp.bottomSide = true;
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty()) {
        return false;
    }
    if (!revStr.empty()) {
        out.formatTitle = L"IPC-2581 Rev " + PathUtils::Utf8ToWide(revStr) + L" Digital Twin PCB";
    }
    out.netCount = std::max<size_t>(1, netNames.empty() ? out.components.size() : netNames.size());
    return true;
}

// ============================================================================
// Fritzing Sketch (.fzz / .fz) & Part Bundle (.fzpz / .fzp) Parser
// ============================================================================
struct FritzingPartSvgFootprint {
    float widthMm = 3.0f;
    float heightMm = 2.0f;
    struct PinPad {
        std::string id;
        float rxMm = 0.0f, ryMm = 0.0f;
        float wMm = 1.0f, hMm = 1.0f;
        float drillMm = 0.0f;
        uint8_t shape = 0;
    };
    std::vector<PinPad> pads;
};

inline float ParseSvgDimMm(std::string_view sv, float viewScaleMmPerUnit = 0.254f) {
    if (sv.empty()) return 0.0f;
    std::string s(sv);
    char* ep = nullptr;
    float val = std::strtof(s.c_str(), &ep);
    if (!ep || ep == s.c_str()) return 0.0f;
    std::string_view unit(ep);
    while (!unit.empty() && unit.front() == ' ') unit.remove_prefix(1);
    if (DetailEda100::ContainsAsciiI(unit, "mm")) return val;
    if (DetailEda100::ContainsAsciiI(unit, "cm")) return val * 10.0f;
    if (DetailEda100::ContainsAsciiI(unit, "in")) return val * 25.4f;
    if (DetailEda100::ContainsAsciiI(unit, "mil")) return val * 0.0254f;
    if (DetailEda100::ContainsAsciiI(unit, "pt")) return val * (25.4f / 72.0f);
    if (DetailEda100::ContainsAsciiI(unit, "px")) return val * (25.4f / 90.0f);
    return val * viewScaleMmPerUnit;
}

inline FritzingPartSvgFootprint ParseFritzingPcbSvg(std::string_view svg) {
    FritzingPartSvgFootprint fp;
    float scaleMm = 25.4f / 90.0f; // Fritzing default 90 DPI
    size_t svgTag = svg.find("<svg");
    if (svgTag != std::string_view::npos) {
        size_t gt = svg.find('>', svgTag);
        if (gt != std::string_view::npos) {
            std::string_view hdr = svg.substr(svgTag, gt - svgTag);
            std::string_view wSv, hSv, vbSv;
            float wMm = 0.0f, hMm = 0.0f;
            if (GetXmlAttr(hdr, "width", wSv))  wMm = ParseSvgDimMm(wSv, scaleMm);
            if (GetXmlAttr(hdr, "height", hSv)) hMm = ParseSvgDimMm(hSv, scaleMm);
            if (wMm > 0.2f && hMm > 0.2f) {
                fp.widthMm = wMm;
                fp.heightMm = hMm;
                if (GetXmlAttr(hdr, "viewBox", vbSv)) {
                    float vx = 0, vy = 0, vw = 0, vh = 0;
                    std::string vbs(vbSv);
                    if (sscanf_s(vbs.c_str(), "%f %f %f %f", &vx, &vy, &vw, &vh) == 4 && vw > 1e-3f) {
                        scaleMm = wMm / vw;
                    }
                }
            }
        }
    }

    size_t pos = 0;
    while (pos < svg.size() && fp.pads.size() < 256) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string_view::npos) break;
        size_t gt = svg.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = svg.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        if (tag.empty() || tag[0] == '/' || tag[0] == '?' || tag[0] == '!') continue;

        size_t ne = tag.find_first_of(" \t\r\n/");
        std::string_view tn = (ne == std::string_view::npos) ? tag : tag.substr(0, ne);
        std::string_view idSv;
        GetXmlAttr(tag, "id", idSv);
        if (idSv.find("connector") == std::string_view::npos) continue;

        if (tn == "circle") {
            float cx = GetXmlFloat(tag, "cx") * scaleMm;
            float cy = GetXmlFloat(tag, "cy") * scaleMm;
            float r  = GetXmlFloat(tag, "r", 1.5f) * scaleMm;
            float sw = GetXmlFloat(tag, "stroke-width", 0.5f) * scaleMm;
            float outerD = std::max(0.5f, (r + sw * 0.5f) * 2.0f);
            float drillD = std::max(0.0f, (r - sw * 0.5f) * 2.0f);
            fp.pads.push_back({ std::string(idSv), cx - fp.widthMm * 0.5f, -(cy - fp.heightMm * 0.5f), outerD, outerD, drillD, 0 });
        } else if (tn == "rect") {
            float rx = GetXmlFloat(tag, "x") * scaleMm;
            float ry = GetXmlFloat(tag, "y") * scaleMm;
            float rw = std::max(0.4f, GetXmlFloat(tag, "width", 2.0f) * scaleMm);
            float rh = std::max(0.4f, GetXmlFloat(tag, "height", 2.0f) * scaleMm);
            float cx = rx + rw * 0.5f;
            float cy = ry + rh * 0.5f;
            fp.pads.push_back({ std::string(idSv), cx - fp.widthMm * 0.5f, -(cy - fp.heightMm * 0.5f), rw, rh, 0.0f, 1 });
        }
    }
    return fp;
}

inline bool ParseFritzingPartXmlAndSvg(
    std::string_view fzpXml,
    const std::unordered_map<std::string, std::string>& svgByName,
    ParsedEdaBoard& out)
{
    if (fzpXml.find("<module") == std::string_view::npos || fzpXml.find("</module>") == std::string_view::npos) {
        return false;
    }
    std::string titleStr = "FritzingPart";
    size_t t1 = fzpXml.find("<title>");
    size_t t2 = (t1 != std::string_view::npos) ? fzpXml.find("</title>", t1 + 7) : std::string_view::npos;
    if (t1 != std::string_view::npos && t2 != std::string_view::npos) {
        titleStr = std::string(fzpXml.substr(t1 + 7, t2 - (t1 + 7)));
    }

    // Locate pcbView image if available
    FritzingPartSvgFootprint fp;
    for (const auto& kv : svgByName) {
        if (kv.first.find("pcb") != std::string::npos) {
            fp = ParseFritzingPcbSvg(kv.second);
            if (!fp.pads.empty()) break;
        }
    }

    // Count <connector id="..." name="..."> entries
    std::vector<std::pair<std::string, std::string>> conns;
    size_t p = 0;
    while ((p = fzpXml.find("<connector ", p)) != std::string_view::npos && conns.size() < 256) {
        size_t gt = fzpXml.find('>', p);
        if (gt == std::string_view::npos) break;
        std::string_view ctag = fzpXml.substr(p, gt - p);
        std::string_view cid, cname;
        GetXmlAttr(ctag, "id", cid);
        GetXmlAttr(ctag, "name", cname);
        if (!cid.empty()) conns.push_back({ std::string(cid), std::string(cname.empty() ? cid : cname) });
        p = gt + 1;
    }
    if (fp.pads.empty() && conns.empty()) return false;

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"Fritzing Part Package (" + PathUtils::Utf8ToWide(titleStr) + L")";

    if (fp.pads.empty()) {
        // Synthesize true dual-row 2.54mm DIP/header pads from actual <connector> definitions
        size_t half = (conns.size() + 1) / 2;
        fp.widthMm = std::max(4.0f, static_cast<float>(half) * 2.54f);
        fp.heightMm = 7.62f;
        for (size_t i = 0; i < conns.size(); ++i) {
            bool topRow = (i >= half);
            size_t col = topRow ? (conns.size() - 1 - i) : i;
            float rx = -fp.widthMm * 0.5f + 1.27f + static_cast<float>(col) * 2.54f;
            float ry = topRow ? 3.81f : -3.81f;
            fp.pads.push_back({ conns[i].second, rx, ry, 1.5f, 1.5f, 0.8f, static_cast<uint8_t>(i == 0 ? 1 : 0) });
        }
    }

    float hw = fp.widthMm * 0.5f + 1.0f, hh = fp.heightMm * 0.5f + 1.0f;
    out.traces.push_back({ -hw, -hh,  hw, -hh, 0.22f, EdaLayerId::TopSilk, 0 });
    out.traces.push_back({  hw, -hh,  hw,  hh, 0.22f, EdaLayerId::TopSilk, 0 });
    out.traces.push_back({  hw,  hh, -hw,  hh, 0.22f, EdaLayerId::TopSilk, 0 });
    out.traces.push_back({ -hw,  hh, -hw, -hh, 0.22f, EdaLayerId::TopSilk, 0 });

    for (size_t i = 0; i < fp.pads.size(); ++i) {
        const auto& pd = fp.pads[i];
        std::wstring pinNm = (i < conns.size()) ? PathUtils::Utf8ToWide(conns[i].second) : PathUtils::Utf8ToWide(pd.id);
        EdaLayerId lid = (pd.drillMm > 0.05f) ? EdaLayerId::PadsThrough : EdaLayerId::TopCopper;
        out.pads.push_back({ pd.rxMm, pd.ryMm, pd.wMm, pd.hMm, 0.0f, pd.drillMm, pd.shape, lid, pinNm, L"" });
    }

    EdaComponent3D comp;
    comp.refDes = L"PART1";
    comp.value = PathUtils::Utf8ToWide(titleStr);
    comp.footprint = L"FRITZING_FZP";
    comp.widthMm = fp.widthMm;
    comp.lengthMm = fp.heightMm;
    comp.pinCount = static_cast<int>(fp.pads.size());
    comp.category = ClassifyComponent(comp.refDes, comp.value, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
    out.components.push_back(std::move(comp));
    out.netCount = std::max<size_t>(1, out.pads.size());
    return true;
}

inline bool ParseFritzingSketchXml(
    std::string_view fzXml,
    const std::unordered_map<std::string, FritzingPartSvgFootprint>& customFootprints,
    ParsedEdaBoard& out)
{
    for (unsigned char c : fzXml) {
        if (c == 0) return false;
    }
    if (fzXml.find("<module") == std::string_view::npos ||
        fzXml.find("<instances>") == std::string_view::npos ||
        fzXml.find("</module>") == std::string_view::npos)
    {
        return false;
    }

    // Fritzing scene coordinates are in 90-DPI pixels (1 px = 25.4 / 90 mm = 0.282222 mm)
    constexpr float kPxToMm = 25.4f / 90.0f;

    struct FzWire {
        float x = 0, y = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        float widthMm = 0.40f;
        bool bottomLayer = false;
        bool isSch = false;
        std::string c0TargetModel, c0TargetConn;
        std::string c1TargetModel, c1TargetConn;
    };
    struct FzPart {
        std::string modelIndex;
        std::string moduleId;
        std::string title;
        std::string valueProp;
        std::string pkgProp;
        float xMm = 0.0f, yMm = 0.0f, rotDeg = 0.0f;
        float boardWMm = 0.0f, boardHMm = 0.0f;
        bool bottomSide = false;
        bool hasPcbPos = false, hasAnyPos = false;
        std::set<std::string> usedConnectorIds;
        std::vector<std::tuple<std::string, std::string, std::string>> connects; // (myConn, dstModel, dstConn)
    };

    std::vector<FzPart> parts;
    std::vector<FzWire> wires;
    bool anyPcbGeometry = false;

    size_t pos = 0;
    while ((pos = fzXml.find("<instance ", pos)) != std::string_view::npos && (parts.size() + wires.size()) < 4096) {
        size_t instEnd = fzXml.find("</instance>", pos);
        if (instEnd == std::string_view::npos) return false; // Truncated XML!
        std::string_view blk = fzXml.substr(pos, instEnd + 11 - pos);
        pos = instEnd + 11;

        size_t hdrGt = blk.find('>');
        if (hdrGt == std::string_view::npos) continue;
        std::string_view instHdr = blk.substr(0, hdrGt);
        std::string_view modIdSv, modIdxSv;
        GetXmlAttr(instHdr, "moduleIdRef", modIdSv);
        GetXmlAttr(instHdr, "modelIndex", modIdxSv);
        std::string modId(modIdSv);
        std::string modIdx(modIdxSv);

        // Extract <title>...</title>
        std::string title;
        size_t t1 = blk.find("<title>");
        size_t t2 = (t1 != std::string_view::npos) ? blk.find("</title>", t1 + 7) : std::string_view::npos;
        if (t1 != std::string_view::npos && t2 != std::string_view::npos) {
            title = std::string(blk.substr(t1 + 7, t2 - (t1 + 7)));
        }

        // Properties
        std::string valProp, pkgProp;
        float propWMm = 0.0f, propHMm = 0.0f, holeD = 0.4f;
        size_t pp = 0;
        while ((pp = blk.find("<property ", pp)) != std::string_view::npos) {
            size_t pgt = blk.find('>', pp);
            if (pgt == std::string_view::npos) break;
            std::string_view ptag = blk.substr(pp, pgt - pp);
            pp = pgt + 1;
            std::string_view pn, pv;
            GetXmlAttr(ptag, "name", pn);
            GetXmlAttr(ptag, "value", pv);
            if (DetailEda100::ContainsAsciiI(pn, "package")) pkgProp = std::string(pv);
            else if (DetailEda100::ContainsAsciiI(pn, "width")) propWMm = ParseSvgDimMm(pv, 1.0f);
            else if (DetailEda100::ContainsAsciiI(pn, "height")) propHMm = ParseSvgDimMm(pv, 1.0f);
            else if (DetailEda100::ContainsAsciiI(pn, "hole size")) holeD = std::max(0.2f, std::strtof(std::string(pv).c_str(), nullptr));
            else if (valProp.empty() &&
                     (DetailEda100::ContainsAsciiI(pn, "resistance") ||
                      DetailEda100::ContainsAsciiI(pn, "capacitance") ||
                      DetailEda100::ContainsAsciiI(pn, "voltage") ||
                      DetailEda100::ContainsAsciiI(pn, "chip label") ||
                      DetailEda100::ContainsAsciiI(pn, "part number") ||
                      DetailEda100::ContainsAsciiI(pn, "type")))
            {
                valProp = std::string(pv);
            }
        }

        // Helper to extract <pcbView> (preferred) or <schematicView> / <breadboardView>
        auto findViewBlock = [&](const char* vTag) -> std::string_view {
            std::string openT = std::string("<") + vTag;
            std::string closeT = std::string("</") + vTag + ">";
            size_t vs = blk.find(openT);
            if (vs == std::string_view::npos) return {};
            size_t ve = blk.find(closeT, vs);
            if (ve == std::string_view::npos) return {};
            return blk.substr(vs, ve + closeT.size() - vs);
        };

        std::string_view viewBlk = findViewBlock("pcbView");
        bool usedPcb = !viewBlk.empty();
        if (viewBlk.empty()) viewBlk = findViewBlock("schematicView");
        if (viewBlk.empty()) viewBlk = findViewBlock("breadboardView");
        if (viewBlk.empty()) continue;

        size_t gPos = viewBlk.find("<geometry ");
        if (gPos == std::string_view::npos) continue;
        size_t gGt = viewBlk.find('>', gPos);
        if (gGt == std::string_view::npos) continue;
        std::string_view gTag = viewBlk.substr(gPos, gGt - gPos);

        std::string_view lyrSv;
        GetXmlAttr(viewBlk.substr(0, viewBlk.find('>')), "layer", lyrSv);
        bool isBot = DetailEda100::ContainsAsciiI(lyrSv, "copper0") || DetailEda100::ContainsAsciiI(lyrSv, "bottom");

        const bool isWire = DetailEda100::ContainsAsciiI(modId, "WireModuleID") ||
                            DetailEda100::ContainsAsciiI(modId, "TraceModuleID") ||
                            viewBlk.find("<wireExtras") != std::string_view::npos;
        const bool isBoard = DetailEda100::ContainsAsciiI(modId, "RectanglePCB") ||
                             DetailEda100::ContainsAsciiI(modId, "PCBModuleID");
        const bool isVia   = DetailEda100::ContainsAsciiI(modId, "ViaModuleID");

        if (isWire) {
            FzWire w;
            w.x  = GetXmlFloat(gTag, "x")  * kPxToMm;
            w.y  = -GetXmlFloat(gTag, "y") * kPxToMm;
            w.x1 = GetXmlFloat(gTag, "x1") * kPxToMm;
            w.y1 = -GetXmlFloat(gTag, "y1") * kPxToMm;
            w.x2 = GetXmlFloat(gTag, "x2") * kPxToMm;
            w.y2 = -GetXmlFloat(gTag, "y2") * kPxToMm;
            w.bottomLayer = isBot;
            w.isSch = !usedPcb;
            size_t wePos = viewBlk.find("<wireExtras ");
            if (wePos != std::string_view::npos) {
                size_t weGt = viewBlk.find('>', wePos);
                if (weGt != std::string_view::npos) {
                    float mils = GetXmlFloat(viewBlk.substr(wePos, weGt - wePos), "mils", 16.0f);
                    w.widthMm = std::clamp(mils * 0.0254f, 0.15f, 3.0f);
                }
            }
            if (usedPcb) anyPcbGeometry = true;
            wires.push_back(std::move(w));
            continue;
        }

        if (isVia) {
            float vx = GetXmlFloat(gTag, "x") * kPxToMm;
            float vy = -GetXmlFloat(gTag, "y") * kPxToMm;
            out.vias.push_back({ vx, vy, std::max(0.8f, holeD * 1.8f), holeD, 0 });
            if (usedPcb) anyPcbGeometry = true;
            continue;
        }

        if (isBoard) {
            float bx = GetXmlFloat(gTag, "x") * kPxToMm;
            float by = -GetXmlFloat(gTag, "y") * kPxToMm;
            float bw = (propWMm > 1.0f) ? propWMm : std::max(20.0f, GetXmlFloat(gTag, "width", 50.0f) * kPxToMm);
            float bh = (propHMm > 1.0f) ? propHMm : std::max(15.0f, GetXmlFloat(gTag, "height", 40.0f) * kPxToMm);
            out.traces.push_back({ bx,      by,      bx + bw, by,      0.35f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ bx + bw, by,      bx + bw, by - bh, 0.35f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ bx + bw, by - bh, bx,      by - bh, 0.35f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ bx,      by - bh, bx,      by,      0.35f, EdaLayerId::EdgeCuts, 0 });
            if (usedPcb) anyPcbGeometry = true;
            continue;
        }

        FzPart fp;
        fp.modelIndex = modIdx;
        fp.moduleId = modId;
        fp.title = title.empty() ? ("U" + modIdx) : title;
        fp.valueProp = valProp;
        fp.pkgProp = pkgProp;
        fp.xMm = GetXmlFloat(gTag, "x") * kPxToMm;
        fp.yMm = -GetXmlFloat(gTag, "y") * kPxToMm;
        fp.bottomSide = isBot;
        fp.hasPcbPos = usedPcb;
        fp.hasAnyPos = true;
        if (usedPcb) anyPcbGeometry = true;

        // Parse 2D rotation from <transform m11="..." m12="..."/> if present
        size_t trPos = viewBlk.find("<transform ");
        if (trPos != std::string_view::npos) {
            size_t trGt = viewBlk.find('>', trPos);
            if (trGt != std::string_view::npos) {
                std::string_view trTag = viewBlk.substr(trPos, trGt - trPos);
                float m11 = GetXmlFloat(trTag, "m11", 1.0f);
                float m12 = GetXmlFloat(trTag, "m12", 0.0f);
                fp.rotDeg = static_cast<float>(std::atan2(m12, m11) * (180.0 / 3.14159265358979323846));
            }
        }

        // Parse <connectors><connector id="connector0">...<connect modelIndex="..." connectorId="..."/>
        size_t cp = 0;
        while ((cp = viewBlk.find("<connector ", cp)) != std::string_view::npos) {
            size_t cEnd = viewBlk.find("</connector>", cp);
            if (cEnd == std::string_view::npos) break;
            std::string_view cBlk = viewBlk.substr(cp, cEnd - cp);
            cp = cEnd + 12;
            std::string_view myCid;
            GetXmlAttr(cBlk.substr(0, cBlk.find('>')), "connectorId", myCid);
            if (myCid.empty()) GetXmlAttr(cBlk.substr(0, cBlk.find('>')), "id", myCid);
            if (myCid.empty()) continue;
            fp.usedConnectorIds.insert(std::string(myCid));

            size_t kp = 0;
            while ((kp = cBlk.find("<connect ", kp)) != std::string_view::npos) {
                size_t kgt = cBlk.find('>', kp);
                if (kgt == std::string_view::npos) break;
                std::string_view kTag = cBlk.substr(kp, kgt - kp);
                kp = kgt + 1;
                std::string_view dstMod, dstCid, kLyr;
                GetXmlAttr(kTag, "modelIndex", dstMod);
                GetXmlAttr(kTag, "connectorId", dstCid);
                GetXmlAttr(kTag, "layer", kLyr);
                if (!dstMod.empty() && !dstCid.empty()) {
                    fp.connects.emplace_back(std::string(myCid), std::string(dstMod), std::string(dstCid));
                }
            }
        }

        parts.push_back(std::move(fp));
    }

    if (parts.empty() && wires.empty() && out.traces.empty() && out.vias.empty()) {
        return false;
    }

    out.kind = anyPcbGeometry ? EdaDocKind::KiCadPcb : EdaDocKind::KiCadSch;
    out.formatTitle = L"Fritzing Electronics Sketch (.fzz / .fz)";

    // Emit wires / copper traces
    int wireNetSeq = 1;
    for (const auto& w : wires) {
        float ax = w.x + w.x1, ay = w.y + w.y1;
        float bx = w.x + w.x2, by = w.y + w.y2;
        if (std::hypot(bx - ax, by - ay) < 0.02f) continue;
        EdaLayerId lid = w.isSch ? EdaLayerId::SchWire : (w.bottomLayer ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper);
        out.traces.push_back({ ax, ay, bx, by, w.widthMm, lid, wireNetSeq++ });
    }

    // Place components & their copper/schematic pads
    std::set<std::string> uniqueNetPairs;
    for (const auto& p : parts) {
        const FritzingPartSvgFootprint* customFp = nullptr;
        auto itFp = customFootprints.find(p.moduleId);
        if (itFp != customFootprints.end() && !itFp->second.pads.empty()) {
            customFp = &itFp->second;
        }

        size_t pinCnt = customFp ? customFp->pads.size() : std::max<size_t>(2, p.usedConnectorIds.size());
        float wMm = customFp ? customFp->widthMm  : std::max(3.2f, static_cast<float>((pinCnt + 1) / 2) * 2.54f);
        float hMm = customFp ? customFp->heightMm : ((pinCnt > 4) ? 7.62f : 2.54f);
        float cx = p.xMm + wMm * 0.5f;
        float cy = p.yMm - hMm * 0.5f;

        if (customFp) {
            for (const auto& pd : customFp->pads) {
                EdaLayerId lid = (pd.drillMm > 0.05f) ? EdaLayerId::PadsThrough
                               : (p.bottomSide ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper);
                out.pads.push_back({ cx + pd.rxMm, cy + pd.ryMm, pd.wMm, pd.hMm, p.rotDeg, pd.drillMm,
                                     pd.shape, lid, PathUtils::Utf8ToWide(pd.id), L"" });
            }
        } else {
            bool smd = DetailEda100::ContainsAsciiI(p.pkgProp, "0402") ||
                       DetailEda100::ContainsAsciiI(p.pkgProp, "0603") ||
                       DetailEda100::ContainsAsciiI(p.pkgProp, "0805") ||
                       DetailEda100::ContainsAsciiI(p.pkgProp, "1206") ||
                       DetailEda100::ContainsAsciiI(p.pkgProp, "SMD")  ||
                       DetailEda100::ContainsAsciiI(p.pkgProp, "SOIC") ||
                       DetailEda100::ContainsAsciiI(p.pkgProp, "SOT");
            size_t half = (pinCnt + 1) / 2;
            for (size_t i = 0; i < pinCnt; ++i) {
                float rx = 0.0f, ry = 0.0f;
                if (pinCnt == 2) {
                    rx = (i == 0) ? -wMm * 0.38f : wMm * 0.38f;
                    ry = 0.0f;
                } else {
                    bool topRow = (i >= half);
                    size_t col = topRow ? (pinCnt - 1 - i) : i;
                    rx = -wMm * 0.5f + 1.27f + static_cast<float>(col) * 2.54f;
                    ry = topRow ? (hMm * 0.5f - 0.5f) : (-hMm * 0.5f + 0.5f);
                }
                EdaLayerId lid = smd ? (p.bottomSide ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper)
                                     : EdaLayerId::PadsThrough;
                out.pads.push_back({ cx + rx, cy + ry, smd ? 1.1f : 1.4f, smd ? 1.3f : 1.4f, p.rotDeg,
                                     smd ? 0.0f : 0.8f, static_cast<uint8_t>(smd || i == 0 ? 1 : 0),
                                     lid, std::to_wstring(i + 1), L"" });
            }
        }

        // Silkscreen component outline + RefDes label
        float hw = wMm * 0.5f, hh = hMm * 0.5f;
        EdaLayerId silkL = p.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
        out.traces.push_back({ cx - hw, cy - hh, cx + hw, cy - hh, 0.18f, silkL, 0 });
        out.traces.push_back({ cx + hw, cy - hh, cx + hw, cy + hh, 0.18f, silkL, 0 });
        out.traces.push_back({ cx + hw, cy + hh, cx - hw, cy + hh, 0.18f, silkL, 0 });
        out.traces.push_back({ cx - hw, cy + hh, cx - hw, cy - hh, 0.18f, silkL, 0 });
        out.labels.push_back({ cx - hw, cy + hh + 0.6f, 0.95f, silkL, PathUtils::Utf8ToWide(p.title) });

        for (const auto& cn : p.connects) {
            std::string a = p.modelIndex + ":" + std::get<0>(cn);
            std::string b = std::get<1>(cn) + ":" + std::get<2>(cn);
            if (a > b) std::swap(a, b);
            uniqueNetPairs.insert(a + "->" + b);
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(p.title);
        comp.value = PathUtils::Utf8ToWide(p.valueProp.empty() ? p.moduleId : p.valueProp);
        comp.footprint = PathUtils::Utf8ToWide(p.pkgProp.empty() ? "FRITZING_PART" : p.pkgProp);
        comp.x = cx;
        comp.y = cy;
        comp.rotDeg = p.rotDeg;
        comp.widthMm = wMm;
        comp.lengthMm = hMm;
        comp.bottomSide = p.bottomSide;
        comp.pinCount = static_cast<int>(pinCnt);
        comp.category = ClassifyComponent(
            comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
        out.components.push_back(std::move(comp));
    }

    out.netCount = std::max<size_t>(1, !uniqueNetPairs.empty() ? uniqueNetPairs.size() : wires.size());
    return !out.traces.empty() || !out.pads.empty() || !out.components.empty();
}

inline bool ParseFritzingFile(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;

    // ZIP container (.fzz or .fzpz)
    if (size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

        std::string fzSketchXml;
        std::vector<std::string> fzpTexts;
        std::unordered_map<std::string, std::string> svgByName;

        for (const auto& e : entries) {
            if (e.isDirectory || e.uncompressedSize == 0 || e.uncompressedSize > 16 * 1024 * 1024) continue;
            std::string low = PathUtils::ToLowerAscii(PathUtils::WideToUtf8(e.pathInArchive));
            for (char& ch : low) if (ch == '\\') ch = '/';
            size_t sl = low.find_last_of('/');
            std::string base = (sl == std::string::npos) ? low : low.substr(sl + 1);

            if (low.size() >= 3 && low.substr(low.size() - 3) == ".fz") {
                std::vector<uint8_t> raw;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, raw, 16 * 1024 * 1024) && !raw.empty()) {
                    fzSketchXml.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
            } else if (low.size() >= 4 && low.substr(low.size() - 4) == ".fzp") {
                std::vector<uint8_t> raw;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, raw, 8 * 1024 * 1024) && !raw.empty()) {
                    fzpTexts.emplace_back(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
            } else if (low.size() >= 4 && low.substr(low.size() - 4) == ".svg") {
                std::vector<uint8_t> raw;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, raw, 8 * 1024 * 1024) && !raw.empty()) {
                    svgByName[base] = std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
            }
        }

        // Map moduleId -> custom PCB SVG footprint from bundled .fzp + .svg files
        std::unordered_map<std::string, FritzingPartSvgFootprint> customFps;
        for (const auto& fzp : fzpTexts) {
            size_t mPos = fzp.find("<module ");
            if (mPos == std::string::npos) continue;
            size_t mGt = fzp.find('>', mPos);
            if (mGt == std::string::npos) continue;
            std::string_view modIdSv;
            if (!GetXmlAttr(std::string_view(fzp).substr(mPos, mGt - mPos), "moduleId", modIdSv)) continue;

            // Find pcbView <layers image="pcb/foo.svg">
            size_t pv = fzp.find("<pcbView>");
            if (pv != std::string::npos) {
                size_t lyr = fzp.find("image=", pv);
                if (lyr != std::string::npos && lyr < fzp.find("</pcbView>", pv)) {
                    size_t q1 = fzp.find('"', lyr);
                    size_t q2 = (q1 != std::string::npos) ? fzp.find('"', q1 + 1) : std::string::npos;
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        std::string imgRef = PathUtils::ToLowerAscii(fzp.substr(q1 + 1, q2 - q1 - 1));
                        size_t sl = imgRef.find_last_of('/');
                        std::string stem = (sl == std::string::npos) ? imgRef : imgRef.substr(sl + 1);
                        for (const auto& skv : svgByName) {
                            if (skv.first.find(stem) != std::string::npos ||
                                (skv.first.find("pcb") != std::string::npos && svgByName.size() <= 4))
                            {
                                customFps[std::string(modIdSv)] = ParseFritzingPcbSvg(skv.second);
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (!fzSketchXml.empty()) {
            return ParseFritzingSketchXml(fzSketchXml, customFps, out);
        }
        if (!fzpTexts.empty()) {
            return ParseFritzingPartXmlAndSvg(fzpTexts[0], svgByName, out);
        }
        return false;
    }

    // Plain XML (.fz or .fzp)
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (extLower == L".fzp" || (sv.find("<connectors>") != std::string_view::npos && sv.find("<instances>") == std::string_view::npos)) {
        std::unordered_map<std::string, std::string> emptySvgs;
        return ParseFritzingPartXmlAndSvg(sv, emptySvgs, out);
    }
    std::unordered_map<std::string, FritzingPartSvgFootprint> emptyFps;
    return ParseFritzingSketchXml(sv, emptyFps, out);
}

// ============================================================================
// gEDA / PCB-rnd Layout Parser (.pcb)
// ============================================================================
struct GedaTok {
    enum Kind { Word, Quoted, LBrack, RBrack, LParen, RParen } kind = Word;
    std::string text;
};

inline bool TokenizeGedaPcb(std::string_view sv, std::vector<GedaTok>& outToks) {
    for (unsigned char c : sv) {
        if (c == 0) return false; // Reject binary P-CAD / Allegro .pcb files immediately!
    }
    int brackDepth = 0, parenDepth = 0;
    size_t i = 0;
    while (i < sv.size() && outToks.size() < 400000) {
        unsigned char c = static_cast<unsigned char>(sv[i]);
        if (c <= 0x20u) { ++i; continue; }
        if (c == '#') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (c == '[') { ++brackDepth; outToks.push_back({ GedaTok::LBrack, "[" }); ++i; continue; }
        if (c == ']') { if (--brackDepth < 0) return false; outToks.push_back({ GedaTok::RBrack, "]" }); ++i; continue; }
        if (c == '(') { ++parenDepth; outToks.push_back({ GedaTok::LParen, "(" }); ++i; continue; }
        if (c == ')') { if (--parenDepth < 0) return false; outToks.push_back({ GedaTok::RParen, ")" }); ++i; continue; }
        if (c == '"') {
            ++i;
            std::string s;
            while (i < sv.size() && sv[i] != '"') {
                if (sv[i] == '\\' && i + 1 < sv.size()) ++i;
                s.push_back(sv[i++]);
            }
            if (i >= sv.size()) return false; // Unterminated quote!
            ++i;
            outToks.push_back({ GedaTok::Quoted, std::move(s) });
            continue;
        }
        size_t s = i;
        while (i < sv.size()) {
            unsigned char ch = static_cast<unsigned char>(sv[i]);
            if (ch <= 0x20u || ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == '"' || ch == '#') break;
            ++i;
        }
        outToks.push_back({ GedaTok::Word, std::string(sv.substr(s, i - s)) });
    }
    return brackDepth == 0 && parenDepth == 0 && !outToks.empty();
}

inline float ParseGedaCoordMm(const std::string& tok, bool bracketHiRes) {
    if (tok.empty()) return 0.0f;
    char* ep = nullptr;
    double val = std::strtod(tok.c_str(), &ep);
    if (!ep || ep == tok.c_str()) return 0.0f;
    std::string unit = DetailEda100::TrimAsciiView(std::string_view(ep));
    for (char& c : unit) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (unit == "mm")  return static_cast<float>(val);
    if (unit == "cm")  return static_cast<float>(val * 10.0);
    if (unit == "um")  return static_cast<float>(val * 0.001);
    if (unit == "nm")  return static_cast<float>(val * 1e-6);
    if (unit == "in")  return static_cast<float>(val * 25.4);
    if (unit == "mil") return static_cast<float>(val * 0.0254);
    // Unitless gEDA coordinates: [...] = 1/100 mil (0.000254 mm), (...) = 1 mil (0.0254 mm)
    return static_cast<float>(val * (bracketHiRes ? 0.000254 : 0.0254));
}

inline bool ParseGedaPcbBoard(std::string_view sv, ParsedEdaBoard& out) {
    std::vector<GedaTok> toks;
    if (!TokenizeGedaPcb(sv, toks)) return false;

    bool sawPcbHeader = false;
    std::string boardName;
    float boardWMm = 0.0f, boardHMm = 0.0f;
    std::set<std::string> netNames;

    auto collectArgs = [&](size_t& idx, bool& outHiRes, std::vector<GedaTok>& args) -> bool {
        args.clear();
        if (idx >= toks.size()) return false;
        if (toks[idx].kind != GedaTok::LBrack && toks[idx].kind != GedaTok::LParen) return false;
        outHiRes = (toks[idx].kind == GedaTok::LBrack);
        GedaTok::Kind closeK = outHiRes ? GedaTok::RBrack : GedaTok::RParen;
        int depth = 1;
        ++idx;
        while (idx < toks.size() && depth > 0) {
            if (toks[idx].kind == (outHiRes ? GedaTok::LBrack : GedaTok::LParen)) ++depth;
            else if (toks[idx].kind == closeK) {
                if (--depth == 0) { ++idx; return true; }
            }
            if (depth == 1) args.push_back(toks[idx]);
            ++idx;
        }
        return false;
    };

    // Note: gEDA PCB Y axis points DOWNWARD (0 at top-left, +H at bottom-right), so negate Y to match Cartesian EDA view!
    size_t i = 0;
    while (i < toks.size() && out.traces.size() < 60000) {
        if (toks[i].kind != GedaTok::Word) { ++i; continue; }
        const std::string& kw = toks[i].text;
        ++i;

        if (kw == "PCB") {
            bool hi = true;
            std::vector<GedaTok> args;
            if (collectArgs(i, hi, args) && args.size() >= 3) {
                sawPcbHeader = true;
                boardName = args[0].text;
                boardWMm = ParseGedaCoordMm(args[1].text, hi);
                boardHMm = ParseGedaCoordMm(args[2].text, hi);
                if (boardWMm > 0.5f && boardHMm > 0.5f) {
                    out.traces.push_back({ 0.0f,     0.0f,      boardWMm, 0.0f,      0.32f, EdaLayerId::EdgeCuts, 0 });
                    out.traces.push_back({ boardWMm, 0.0f,      boardWMm, -boardHMm, 0.32f, EdaLayerId::EdgeCuts, 0 });
                    out.traces.push_back({ boardWMm, -boardHMm, 0.0f,     -boardHMm, 0.32f, EdaLayerId::EdgeCuts, 0 });
                    out.traces.push_back({ 0.0f,     -boardHMm, 0.0f,     0.0f,      0.32f, EdaLayerId::EdgeCuts, 0 });
                }
            }
        } else if (kw == "Via") {
            bool hi = true;
            std::vector<GedaTok> args;
            if (collectArgs(i, hi, args) && args.size() >= 6) {
                float vx = ParseGedaCoordMm(args[0].text, hi);
                float vy = -ParseGedaCoordMm(args[1].text, hi);
                float thick = std::max(0.4f, ParseGedaCoordMm(args[2].text, hi));
                float drill = std::max(0.2f, ParseGedaCoordMm(args[5].text, hi));
                out.vias.push_back({ vx, vy, thick, drill, 0 });
            }
        } else if (kw == "Element") {
            bool hi = true;
            std::vector<GedaTok> eArgs;
            if (!collectArgs(i, hi, eArgs) || eArgs.size() < 6) continue;
            std::string flags  = eArgs[0].text;
            std::string desc   = eArgs[1].text;
            std::string refdes = eArgs[2].text;
            std::string val    = eArgs[3].text;
            float mx = ParseGedaCoordMm(eArgs[4].text, hi);
            float my = -ParseGedaCoordMm(eArgs[5].text, hi);
            bool onBottom = DetailEda100::ContainsAsciiI(flags, "onsolder");

            float minPx = mx - 1.2f, maxPx = mx + 1.2f, minPy = my - 0.8f, maxPy = my + 0.8f;
            int pinCount = 0;

            if (i < toks.size() && toks[i].kind == GedaTok::LParen) {
                ++i; // enter Element ( ... )
                int pDepth = 1;
                while (i < toks.size() && pDepth > 0) {
                    if (toks[i].kind == GedaTok::LParen) { ++pDepth; ++i; continue; }
                    if (toks[i].kind == GedaTok::RParen) { --pDepth; ++i; continue; }
                    if (toks[i].kind != GedaTok::Word) { ++i; continue; }
                    std::string subKw = toks[i++].text;
                    bool subHi = true;
                    std::vector<GedaTok> pArgs;
                    if (!collectArgs(i, subHi, pArgs)) continue;

                    if (subKw == "Pin" && pArgs.size() >= 8) {
                        float px = mx + ParseGedaCoordMm(pArgs[0].text, subHi);
                        float py = my - ParseGedaCoordMm(pArgs[1].text, subHi);
                        float thick = std::max(0.5f, ParseGedaCoordMm(pArgs[2].text, subHi));
                        float drill = std::max(0.2f, ParseGedaCoordMm(pArgs[5].text, subHi));
                        std::string pFlags = (pArgs.size() >= 9) ? pArgs[8].text : "";
                        uint8_t sh = DetailEda100::ContainsAsciiI(pFlags, "square") ? 1 : 0;
                        out.pads.push_back({ px, py, thick, thick, 0.0f, drill, sh, EdaLayerId::PadsThrough,
                                             PathUtils::Utf8ToWide(pArgs[7].text), PathUtils::Utf8ToWide(pArgs[6].text) });
                        minPx = std::min(minPx, px - thick * 0.5f); maxPx = std::max(maxPx, px + thick * 0.5f);
                        minPy = std::min(minPy, py - thick * 0.5f); maxPy = std::max(maxPy, py + thick * 0.5f);
                        ++pinCount;
                    } else if (subKw == "Pad" && pArgs.size() >= 9) {
                        float x1 = mx + ParseGedaCoordMm(pArgs[0].text, subHi);
                        float y1 = my - ParseGedaCoordMm(pArgs[1].text, subHi);
                        float x2 = mx + ParseGedaCoordMm(pArgs[2].text, subHi);
                        float y2 = my - ParseGedaCoordMm(pArgs[3].text, subHi);
                        float thick = std::max(0.25f, ParseGedaCoordMm(pArgs[4].text, subHi));
                        std::string pFlags = (pArgs.size() >= 10) ? pArgs[9].text : "";
                        bool padBot = onBottom ^ DetailEda100::ContainsAsciiI(pFlags, "onsolder");
                        float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
                        float dx = x2 - x1, dy = y2 - y1;
                        float segLen = std::hypot(dx, dy);
                        float pw = segLen + thick, ph = thick;
                        float rot = (segLen > 1e-3f) ? static_cast<float>(std::atan2(dy, dx) * (180.0 / 3.14159265358979323846)) : 0.0f;
                        uint8_t sh = DetailEda100::ContainsAsciiI(pFlags, "square") ? 1 : 3;
                        EdaLayerId lid = padBot ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                        out.pads.push_back({ cx, cy, pw, ph, rot, 0.0f, sh, lid,
                                             PathUtils::Utf8ToWide(pArgs[8].text), PathUtils::Utf8ToWide(pArgs[7].text) });
                        minPx = std::min({ minPx, x1 - thick * 0.5f, x2 - thick * 0.5f });
                        maxPx = std::max({ maxPx, x1 + thick * 0.5f, x2 + thick * 0.5f });
                        minPy = std::min({ minPy, y1 - thick * 0.5f, y2 - thick * 0.5f });
                        maxPy = std::max({ maxPy, y1 + thick * 0.5f, y2 + thick * 0.5f });
                        ++pinCount;
                    } else if (subKw == "ElementLine" && pArgs.size() >= 5) {
                        float x1 = mx + ParseGedaCoordMm(pArgs[0].text, subHi);
                        float y1 = my - ParseGedaCoordMm(pArgs[1].text, subHi);
                        float x2 = mx + ParseGedaCoordMm(pArgs[2].text, subHi);
                        float y2 = my - ParseGedaCoordMm(pArgs[3].text, subHi);
                        float w = std::max(0.12f, ParseGedaCoordMm(pArgs[4].text, subHi));
                        out.traces.push_back({ x1, y1, x2, y2, w, onBottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, 0 });
                        minPx = std::min({ minPx, x1, x2 }); maxPx = std::max({ maxPx, x1, x2 });
                        minPy = std::min({ minPy, y1, y2 }); maxPy = std::max({ maxPy, y1, y2 });
                    } else if (subKw == "ElementArc" && pArgs.size() >= 7) {
                        float cx = mx + ParseGedaCoordMm(pArgs[0].text, subHi);
                        float cy = my - ParseGedaCoordMm(pArgs[1].text, subHi);
                        float r  = std::max(0.2f, ParseGedaCoordMm(pArgs[2].text, subHi));
                        float w  = std::max(0.12f, ParseGedaCoordMm(pArgs[6].text, subHi));
                        out.circles.push_back({ cx, cy, r, w, false, onBottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk });
                    }
                }
            }

            if (!refdes.empty() || pinCount > 0) {
                EdaComponent3D comp;
                comp.refDes = PathUtils::Utf8ToWide(refdes.empty() ? "E1" : refdes);
                comp.value = PathUtils::Utf8ToWide(val);
                comp.footprint = PathUtils::Utf8ToWide(desc);
                comp.x = (minPx + maxPx) * 0.5f;
                comp.y = (minPy + maxPy) * 0.5f;
                comp.widthMm  = std::clamp(maxPx - minPx, 1.0f, 60.0f);
                comp.lengthMm = std::clamp(maxPy - minPy, 1.0f, 60.0f);
                comp.bottomSide = onBottom;
                comp.pinCount = std::max(2, pinCount);
                comp.category = ClassifyComponent(
                    comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                out.labels.push_back({ comp.x - comp.widthMm * 0.4f, comp.y + comp.lengthMm * 0.5f + 0.5f, 0.85f,
                                       onBottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, comp.refDes });
                out.components.push_back(std::move(comp));
            }
        } else if (kw == "Layer") {
            bool hi = false;
            std::vector<GedaTok> lArgs;
            if (!collectArgs(i, hi, lArgs) || lArgs.size() < 2) continue;
            std::string lName = lArgs[1].text;
            std::string lType = (lArgs.size() >= 3) ? lArgs[2].text : "";
            EdaLayerId lid = DetailEda100::ClassifyIpcLayerName(lName, lType);
            if (DetailEda100::ContainsAsciiI(lName, "solder") && !DetailEda100::ContainsAsciiI(lName, "mask")) {
                lid = EdaLayerId::BottomCopper;
            } else if (DetailEda100::ContainsAsciiI(lName, "component") || DetailEda100::ContainsAsciiI(lName, "top")) {
                if (!DetailEda100::ContainsAsciiI(lName, "silk")) lid = EdaLayerId::TopCopper;
            }

            if (i < toks.size() && toks[i].kind == GedaTok::LParen) {
                ++i;
                int pDepth = 1;
                while (i < toks.size() && pDepth > 0) {
                    if (toks[i].kind == GedaTok::LParen) { ++pDepth; ++i; continue; }
                    if (toks[i].kind == GedaTok::RParen) { --pDepth; ++i; continue; }
                    if (toks[i].kind != GedaTok::Word) { ++i; continue; }
                    std::string subKw = toks[i++].text;
                    bool subHi = true;
                    std::vector<GedaTok> sArgs;
                    if (!collectArgs(i, subHi, sArgs)) continue;

                    if (subKw == "Line" && sArgs.size() >= 5) {
                        float x1 = ParseGedaCoordMm(sArgs[0].text, subHi);
                        float y1 = -ParseGedaCoordMm(sArgs[1].text, subHi);
                        float x2 = ParseGedaCoordMm(sArgs[2].text, subHi);
                        float y2 = -ParseGedaCoordMm(sArgs[3].text, subHi);
                        float w  = std::max(0.10f, ParseGedaCoordMm(sArgs[4].text, subHi));
                        out.traces.push_back({ x1, y1, x2, y2, w, lid, 1 });
                    } else if (subKw == "Arc" && sArgs.size() >= 7) {
                        float cx = ParseGedaCoordMm(sArgs[0].text, subHi);
                        float cy = -ParseGedaCoordMm(sArgs[1].text, subHi);
                        float rx = std::max(0.2f, ParseGedaCoordMm(sArgs[2].text, subHi));
                        float startDeg = std::strtof(sArgs[4].text.c_str(), nullptr);
                        float deltaDeg = std::strtof(sArgs[5].text.c_str(), nullptr);
                        float w = std::max(0.10f, ParseGedaCoordMm(sArgs[6].text, subHi));
                        int segs = std::clamp(static_cast<int>(std::fabs(deltaDeg) / 12.0f), 4, 32);
                        for (int s = 0; s < segs; ++s) {
                            // gEDA arc angles: 0 deg = -X (west), 90 deg = +Y (south = -Y in Cartesian)
                            float a0 = (180.0f + startDeg + deltaDeg * (static_cast<float>(s) / segs)) * (3.14159265358979323846f / 180.0f);
                            float a1 = (180.0f + startDeg + deltaDeg * (static_cast<float>(s + 1) / segs)) * (3.14159265358979323846f / 180.0f);
                            out.traces.push_back({ cx + rx * std::cos(a0), cy + rx * std::sin(a0),
                                                   cx + rx * std::cos(a1), cy + rx * std::sin(a1), w, lid, 1 });
                        }
                    } else if (subKw == "Polygon") {
                        std::vector<std::pair<float, float>> polyPts;
                        if (i < toks.size() && toks[i].kind == GedaTok::LParen) {
                            ++i;
                            int polyDepth = 1;
                            while (i < toks.size() && polyDepth > 0) {
                                if (toks[i].kind == GedaTok::LParen) {
                                    // Hole (...) inside Polygon — skip hole vertices cleanly
                                    int hDepth = 1; ++i;
                                    while (i < toks.size() && hDepth > 0) {
                                        if (toks[i].kind == GedaTok::LParen) ++hDepth;
                                        else if (toks[i].kind == GedaTok::RParen) --hDepth;
                                        ++i;
                                    }
                                    continue;
                                }
                                if (toks[i].kind == GedaTok::RParen) { --polyDepth; ++i; break; }
                                bool ptHi = true;
                                std::vector<GedaTok> ptArgs;
                                if (collectArgs(i, ptHi, ptArgs) && ptArgs.size() >= 2) {
                                    polyPts.push_back({ ParseGedaCoordMm(ptArgs[0].text, ptHi), -ParseGedaCoordMm(ptArgs[1].text, ptHi) });
                                } else {
                                    ++i;
                                }
                            }
                        }
                        if (polyPts.size() >= 3) {
                            for (size_t k = 0; k < polyPts.size(); ++k) {
                                const auto& a = polyPts[k];
                                const auto& b = polyPts[(k + 1) % polyPts.size()];
                                out.traces.push_back({ a.first, a.second, b.first, b.second, 0.20f, lid, 0 });
                            }
                            if (lid == EdaLayerId::TopCopper || lid == EdaLayerId::BottomCopper || lid == EdaLayerId::InnerCopper) {
                                HatchPolygonHorizontal(polyPts, lid, 0, out);
                            }
                        }
                    }
                }
            }
        } else if (kw == "NetList") {
            bool hi = false;
            std::vector<GedaTok> nlArgs;
            collectArgs(i, hi, nlArgs);
            if (i < toks.size() && toks[i].kind == GedaTok::LParen) {
                ++i;
                int nd = 1;
                while (i < toks.size() && nd > 0) {
                    if (toks[i].kind == GedaTok::LParen) { ++nd; ++i; continue; }
                    if (toks[i].kind == GedaTok::RParen) { --nd; ++i; continue; }
                    if (toks[i].kind == GedaTok::Word && toks[i].text == "Net") {
                        ++i;
                        bool nhi = false;
                        std::vector<GedaTok> nArgs;
                        if (collectArgs(i, nhi, nArgs) && !nArgs.empty() && !nArgs[0].text.empty()) {
                            netNames.insert(nArgs[0].text);
                        }
                    } else {
                        ++i;
                    }
                }
            }
        }
    }

    if (!sawPcbHeader || (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty())) {
        return false;
    }
    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"gEDA / PCB-rnd Layout (.pcb";
    if (!boardName.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(boardName);
    out.formatTitle += L")";
    out.netCount = std::max<size_t>(1, !netNames.empty() ? netNames.size() : out.components.size());
    return true;
}

} // namespace DetailMfg69

inline bool IsLikelyOdbPlusPlusArchiveBytes(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    if (size >= 10 && data[0] == 0x1F && data[1] == 0x8B) {
        std::vector<uint8_t> gunz;
        if (!ZipDeflate::InflateGzip(data, size, gunz, 16 * 1024 * 1024) || gunz.empty()) return false;
        return DetailMfg69::TarBufferHasOdbSignature(gunz.data(), gunz.size());
    }
    if (DetailEda100::LooksLikeTarBlock(data, size)) {
        return DetailMfg69::TarBufferHasOdbSignature(data, size);
    }
    if (size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries)) return false;
        for (const auto& e : entries) {
            std::string low = PathUtils::ToLowerAscii(PathUtils::WideToUtf8(e.pathInArchive));
            for (char& ch : low) if (ch == '\\') ch = '/';
            if (DetailMfg69::HasOdbPathSignature(low)) return true;
        }
    }
    return false;
}

inline bool ParseMfgIpcOdbFritzingGeda(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ParsedEdaBoard& out)
{
    if (!data || size < 12) return false;

    if (extLower == L".fzz" || extLower == L".fzpz" || extLower == L".fz" || extLower == L".fzp") {
        return DetailMfg69::ParseFritzingFile(data, size, extLower, out);
    }
    if (extLower == L".pcb") {
        std::string_view sv(reinterpret_cast<const char*>(data), size);
        return DetailMfg69::ParseGedaPcbBoard(sv, out);
    }

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (extLower == L".ipc2581" || extLower == L".cvg" ||
        (extLower == L".odb" && sv.find("<IPC-2581") != std::string_view::npos))
    {
        return DetailMfg69::ParseIpc2581RevBC(sv, out);
    }

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
                    vfiles.push_back({ low, DetailEda100::UnpackMaybeCompressedText(rawEntry.data(), rawEntry.size()) });
                }
            }
        }
    } else if (size >= 10 && data[0] == 0x1F && data[1] == 0x8B) {
        std::vector<uint8_t> gunz;
        if (!ZipDeflate::InflateGzip(data, size, gunz, 64 * 1024 * 1024) || gunz.empty()) return false;
        if (DetailEda100::LooksLikeTarBlock(gunz.data(), gunz.size())) {
            if (!DetailMfg69::TarBufferHasOdbSignature(gunz.data(), gunz.size())) return false; // Reject generic .tgz!
            DetailMfg69::ExtractOdbTarEntriesFull(gunz.data(), gunz.size(), vfiles);
        } else if (extLower == L".odb") {
            vfiles.push_back({ "layers/top/features", std::string(reinterpret_cast<const char*>(gunz.data()), gunz.size()) });
        } else {
            return false;
        }
    } else if (DetailEda100::LooksLikeTarBlock(data, size)) {
        if (!DetailMfg69::TarBufferHasOdbSignature(data, size)) return false;
        DetailMfg69::ExtractOdbTarEntriesFull(data, size, vfiles);
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
    std::vector<DetailMfg69::OdbPkgDims> pkgDims;
    std::set<std::string> edaNets;

    for (const auto& vf : vfiles) {
        if (vf.first.find("matrix") != std::string::npos) {
            DetailEda100::ParseOdbMatrixLayers(vf.second, matrixLayerMap);
        } else if (vf.first.find("eda/data") != std::string::npos) {
            DetailMfg69::ParseOdbEdaData(vf.second, pkgDims, edaNets);
        }
    }

    size_t validRecords = 0;
    for (const auto& vf : vfiles) {
        const std::string& path = vf.first;
        if (path.find("matrix") != std::string::npos || path.find("eda/data") != std::string::npos) continue;
        if (path.find("components") != std::string::npos) {
            bool isBot = (path.find("comp_+_bot") != std::string::npos || path.find("bottom") != std::string::npos);
            size_t before = out.components.size();
            DetailEda100::ParseOdbComponentsStream(vf.second, isBot, out, validRecords);
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
            DetailMfg69::ParseOdbFeaturesStreamV2(vf.second, EdaLayerId::EdgeCuts, true, out, validRecords);
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
            EdaLayerId lid = (it != matrixLayerMap.end()) ? it->second : DetailEda100::ClassifyIpcLayerName(layerName);
            DetailMfg69::ParseOdbFeaturesStreamV2(vf.second, lid, false, out, validRecords);
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, !edaNets.empty() ? edaNets.size() : out.components.size());
    return true;
}

} // namespace Formats
