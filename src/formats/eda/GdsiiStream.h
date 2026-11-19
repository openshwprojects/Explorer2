#pragma once
// GdsiiStream.h — Calma GDSII Binary IC Layout Stream (.gds / .gds2) Hierarchical Parser

namespace Formats {

inline bool ParseGdsiiStreamBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;

    auto rdU16BE = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(data[off]) << 8) | data[off + 1]);
    };
    auto rdI16BE = [&](size_t off) -> int16_t {
        return static_cast<int16_t>(rdU16BE(off));
    };
    auto rdI32BE = [&](size_t off) -> int32_t {
        uint32_t u = (static_cast<uint32_t>(data[off]) << 24) |
                     (static_cast<uint32_t>(data[off + 1]) << 16) |
                     (static_cast<uint32_t>(data[off + 2]) << 8) |
                     static_cast<uint32_t>(data[off + 3]);
        return static_cast<int32_t>(u);
    };
    // IBM System/370 8-byte excess-64 base-16 floating point (GDSII dataType 0x05)
    auto rdIbmF64 = [&](size_t off) -> double {
        if (off + 8 > size) return 0.0;
        const bool neg = (data[off] & 0x80u) != 0u;
        const int exp16 = static_cast<int>(data[off] & 0x7Fu) - 64;
        uint64_t mant = 0;
        for (size_t i = 1; i < 8; ++i) {
            mant = (mant << 8) | static_cast<uint64_t>(data[off + i]);
        }
        if (mant == 0ULL) return 0.0;
        double val = std::ldexp(static_cast<double>(mant), 4 * exp16 - 56);
        return neg ? -val : val;
    };
    auto rdGdsAscii = [&](size_t off, size_t len) -> std::string {
        if (off + len > size) return {};
        while (len > 0 && data[off + len - 1] == 0) --len;
        std::string s;
        s.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            uint8_t c = data[off + i];
            if (c == 0) break;
            if (c >= 0x20u && c < 0x7Fu) s.push_back(static_cast<char>(c));
        }
        return s;
    };

    // GDSII starts with HEADER record: length == 6, recordType = 0x00, dataType = 0x02
    uint16_t firstLen = rdU16BE(0);
    if (firstLen < 6 || firstLen > size || data[2] != 0x00 || data[3] != 0x02) return false;

    struct GdsPt { double x = 0.0; double y = 0.0; };
    struct GdsShape {
        uint8_t elemKind = 0; // 1=BOUNDARY/BOX, 2=PATH
        int layer = 0;
        int dataType = 0;
        double widthDbu = 0.0;
        std::vector<GdsPt> pts;
    };
    struct GdsText {
        int layer = 0;
        GdsPt pos;
        std::string text;
    };
    struct GdsInst {
        bool isAref = false;
        std::string sname;
        uint16_t strans = 0;
        double mag = 1.0;
        double angleDeg = 0.0;
        int cols = 1;
        int rows = 1;
        std::vector<GdsPt> xy; // 1 pt for SREF, 3 pts [origin, colEnd, rowEnd] for AREF
    };
    struct GdsCell {
        std::string name;
        std::vector<GdsShape> shapes;
        std::vector<GdsText> texts;
        std::vector<GdsInst> insts;
    };

    std::string libName;
    double userUnitInDbu = 0.001;
    double dbuInMeters = 1e-9;
    bool hasUnits = false;

    std::vector<std::string> cellOrder;
    std::unordered_map<std::string, GdsCell> cellMap;

    bool inStruct = false;
    GdsCell curCell;

    enum class ElemMode : uint8_t { None = 0, Boundary, Path, Box, Text, Sref, Aref };
    ElemMode elemMode = ElemMode::None;
    int curLayer = 0;
    int curDType = 0;
    int32_t curWidth = 0;
    uint16_t curStrans = 0;
    double curMag = 1.0;
    double curAngle = 0.0;
    int curCols = 1;
    int curRows = 1;
    std::string curSname;
    std::string curString;
    std::vector<GdsPt> curXY;

    auto resetElem = [&]() {
        elemMode = ElemMode::None;
        curLayer = 0;
        curDType = 0;
        curWidth = 0;
        curStrans = 0;
        curMag = 1.0;
        curAngle = 0.0;
        curCols = 1;
        curRows = 1;
        curSname.clear();
        curString.clear();
        curXY.clear();
    };

    auto commitElem = [&]() {
        if (!inStruct || elemMode == ElemMode::None) {
            resetElem();
            return;
        }
        if (elemMode == ElemMode::Boundary || elemMode == ElemMode::Box) {
            if (curXY.size() >= 3) {
                if (curXY.front().x != curXY.back().x || curXY.front().y != curXY.back().y) {
                    curXY.push_back(curXY.front());
                }
                GdsShape sh;
                sh.elemKind = 1;
                sh.layer = curLayer;
                sh.dataType = curDType;
                sh.pts = std::move(curXY);
                curCell.shapes.push_back(std::move(sh));
            }
        } else if (elemMode == ElemMode::Path) {
            if (curXY.size() >= 2) {
                GdsShape sh;
                sh.elemKind = 2;
                sh.layer = curLayer;
                sh.dataType = curDType;
                sh.widthDbu = (curWidth > 0) ? static_cast<double>(curWidth) : 0.0;
                sh.pts = std::move(curXY);
                curCell.shapes.push_back(std::move(sh));
            }
        } else if (elemMode == ElemMode::Text) {
            if (!curString.empty() && !curXY.empty()) {
                GdsText tx;
                tx.layer = curLayer;
                tx.pos = curXY.front();
                tx.text = std::move(curString);
                curCell.texts.push_back(std::move(tx));
            }
        } else if (elemMode == ElemMode::Sref || elemMode == ElemMode::Aref) {
            if (!curSname.empty() && !curXY.empty()) {
                GdsInst inst;
                inst.isAref = (elemMode == ElemMode::Aref);
                inst.sname = std::move(curSname);
                inst.strans = curStrans;
                inst.mag = (std::isfinite(curMag) && curMag > 1e-9) ? curMag : 1.0;
                inst.angleDeg = std::isfinite(curAngle) ? curAngle : 0.0;
                inst.cols = std::clamp(curCols, 1, 512);
                inst.rows = std::clamp(curRows, 1, 512);
                inst.xy = std::move(curXY);
                curCell.insts.push_back(std::move(inst));
            }
        }
        resetElem();
    };

    size_t pos = 0;
    while (pos + 4 <= size) {
        uint16_t recLen = rdU16BE(pos);
        if (recLen < 4 || pos + recLen > size) break;
        uint8_t recType  = data[pos + 2];
        uint8_t dataType = data[pos + 3];
        size_t payOff = pos + 4;
        size_t payLen = recLen - 4;

        switch (recType) {
        case 0x02: // LIBNAME
            if (dataType == 0x06 && payLen > 0) libName = rdGdsAscii(payOff, payLen);
            break;
        case 0x03: // UNITS (two 8-byte IBM System/370 hex floats)
            if (dataType == 0x05 && payLen >= 16) {
                double uDb = rdIbmF64(payOff);
                double mDb = rdIbmF64(payOff + 8);
                if (std::isfinite(uDb) && uDb > 0.0) userUnitInDbu = uDb;
                if (std::isfinite(mDb) && mDb > 0.0) dbuInMeters = mDb;
                hasUnits = true;
            }
            break;
        case 0x04: // ENDLIB
            if (inStruct) {
                commitElem();
                if (!curCell.name.empty()) {
                    if (!cellMap.count(curCell.name)) cellOrder.push_back(curCell.name);
                    cellMap[curCell.name] = std::move(curCell);
                }
                inStruct = false;
            }
            pos = size; // clean end
            continue;
        case 0x05: // BGNSTR
            if (inStruct) {
                commitElem();
                if (!curCell.name.empty()) {
                    if (!cellMap.count(curCell.name)) cellOrder.push_back(curCell.name);
                    cellMap[curCell.name] = std::move(curCell);
                }
            }
            inStruct = true;
            curCell = GdsCell{};
            resetElem();
            break;
        case 0x06: // STRNAME
            if (inStruct && dataType == 0x06 && payLen > 0) {
                curCell.name = rdGdsAscii(payOff, payLen);
            }
            break;
        case 0x07: // ENDSTR
            if (inStruct) {
                commitElem();
                if (curCell.name.empty()) {
                    curCell.name = "CELL_" + std::to_string(cellOrder.size() + 1);
                }
                if (!cellMap.count(curCell.name)) cellOrder.push_back(curCell.name);
                cellMap[curCell.name] = std::move(curCell);
                inStruct = false;
            }
            break;
        case 0x08: commitElem(); elemMode = ElemMode::Boundary; break; // BOUNDARY
        case 0x09: commitElem(); elemMode = ElemMode::Path;     break; // PATH
        case 0x0A: commitElem(); elemMode = ElemMode::Sref;     break; // SREF
        case 0x0B: commitElem(); elemMode = ElemMode::Aref;     break; // AREF
        case 0x0C: commitElem(); elemMode = ElemMode::Text;     break; // TEXT
        case 0x2D: commitElem(); elemMode = ElemMode::Box;      break; // BOX
        case 0x11: commitElem();                                break; // ENDEL
        case 0x0D: // LAYER
            if (payLen >= 2) curLayer = static_cast<int>(rdI16BE(payOff));
            break;
        case 0x0E: // DATATYPE
        case 0x16: // TEXTTYPE
        case 0x2E: // BOXTYPE
            if (payLen >= 2) curDType = static_cast<int>(rdI16BE(payOff));
            break;
        case 0x0F: // WIDTH
            if (dataType == 0x03 && payLen >= 4) curWidth = std::abs(rdI32BE(payOff));
            break;
        case 0x12: // SNAME
            if (dataType == 0x06 && payLen > 0) curSname = rdGdsAscii(payOff, payLen);
            break;
        case 0x13: // COLROW
            if (dataType == 0x02 && payLen >= 4) {
                curCols = static_cast<int>(rdI16BE(payOff));
                curRows = static_cast<int>(rdI16BE(payOff + 2));
            }
            break;
        case 0x19: // STRING
            if (dataType == 0x06 && payLen > 0) curString = rdGdsAscii(payOff, payLen);
            break;
        case 0x1A: // STRANS (bit 15 = 0x8000 = X-axis reflection before rotation)
            if (payLen >= 2) curStrans = rdU16BE(payOff);
            break;
        case 0x1B: // MAG
            if (dataType == 0x05 && payLen >= 8) curMag = rdIbmF64(payOff);
            break;
        case 0x1C: // ANGLE
            if (dataType == 0x05 && payLen >= 8) curAngle = rdIbmF64(payOff);
            break;
        case 0x10: // XY
            if (dataType == 0x03 && payLen >= 8) {
                // Support legacy single-record streams without BGNSTR/BOUNDARY wrappers as fallback
                if (!inStruct) {
                    inStruct = true;
                    curCell.name = "TOP";
                }
                if (elemMode == ElemMode::None) {
                    elemMode = ElemMode::Path;
                }
                size_t nPts = payLen / 8;
                curXY.reserve(curXY.size() + nPts);
                for (size_t i = 0; i < nPts; ++i) {
                    double px = static_cast<double>(rdI32BE(payOff + i * 8));
                    double py = static_cast<double>(rdI32BE(payOff + i * 8 + 4));
                    curXY.push_back({ px, py });
                }
            }
            break;
        default:
            break;
        }
        pos += recLen;
    }

    if (inStruct) {
        commitElem();
        if (!curCell.name.empty()) {
            if (!cellMap.count(curCell.name)) cellOrder.push_back(curCell.name);
            cellMap[curCell.name] = std::move(curCell);
        }
    }

    if (cellMap.empty()) return false;

    // Convert DB integer units to micrometers (IC scale) or millimeters (package scale)
    double coordScale = 0.01;
    std::wstring unitTag = L"um";
    if (hasUnits) {
        if (dbuInMeters >= 1e-5) {
            coordScale = dbuInMeters * 1e3; // millimeters
            unitTag = L"mm";
        } else if (dbuInMeters > 0.0) {
            coordScale = dbuInMeters * 1e6; // micrometers
            unitTag = L"um";
        } else if (userUnitInDbu > 0.0) {
            coordScale = userUnitInDbu;
        }
    }

    // Identify top-level root structure(s): cells never referenced by SREF or AREF
    std::set<std::string> referenced;
    for (const auto& kv : cellMap) {
        for (const auto& inst : kv.second.insts) {
            referenced.insert(inst.sname);
        }
    }
    std::vector<std::string> roots;
    for (const auto& cname : cellOrder) {
        if (!referenced.count(cname)) {
            roots.push_back(cname);
        }
    }
    if (roots.empty() && !cellOrder.empty()) {
        roots.push_back(cellOrder.back());
    }

    auto mapGdsLayer = [](int lyr) -> EdaLayerId {
        switch (((lyr % 5) + 5) % 5) {
        case 0:  return EdaLayerId::TopCopper;
        case 1:  return EdaLayerId::BottomCopper;
        case 2:  return EdaLayerId::InnerCopper;
        case 3:  return EdaLayerId::TopSilk;
        default: return EdaLayerId::BottomSilk;
        }
    };

    struct Mat2D {
        double m00 = 1.0, m01 = 0.0, tx = 0.0;
        double m10 = 0.0, m11 = 1.0, ty = 0.0;
        double scaleMag = 1.0;

        GdsPt Apply(double x, double y) const {
            return { m00 * x + m01 * y + tx, m10 * x + m11 * y + ty };
        }
    };

    auto composeMat = [](const Mat2D& parent, bool reflectX, double mag, double angleDeg, double dx, double dy) -> Mat2D {
        const double rad = angleDeg * 3.14159265358979323846 / 180.0;
        const double cs = std::cos(rad);
        const double sn = std::sin(rad);
        const double sy = reflectX ? -mag : mag;
        const double sx = mag;
        // Local matrix L: [ sx*cs,  sy*(-sn), dx ;  sx*sn,  sy*cs, dy ]
        const double l00 = sx * cs, l01 = -sy * sn;
        const double l10 = sx * sn, l11 =  sy * cs;

        Mat2D r;
        r.m00 = parent.m00 * l00 + parent.m01 * l10;
        r.m01 = parent.m00 * l01 + parent.m01 * l11;
        r.tx  = parent.m00 * dx  + parent.m01 * dy + parent.tx;
        r.m10 = parent.m10 * l00 + parent.m11 * l10;
        r.m11 = parent.m10 * l01 + parent.m11 * l11;
        r.ty  = parent.m10 * dx  + parent.m11 * dy + parent.ty;
        r.scaleMag = parent.scaleMag * mag;
        return r;
    };

    out.kind = EdaDocKind::GerberLayer;
    std::set<int> distinctLayers;
    std::set<std::string> activeStack;
    static constexpr size_t kMaxTraces = 60000;
    static constexpr size_t kMaxLabels = 2048;

    auto flattenCell = [&](auto& self, const std::string& cellName, const Mat2D& xf, int depth) -> void {
        if (depth > 16 || out.traces.size() >= kMaxTraces) return;
        auto it = cellMap.find(cellName);
        if (it == cellMap.end() || activeStack.count(cellName)) return;

        activeStack.insert(cellName);
        const GdsCell& cell = it->second;

        for (const auto& sh : cell.shapes) {
            if (out.traces.size() >= kMaxTraces) break;
            distinctLayers.insert(sh.layer);
            EdaLayerId lid = mapGdsLayer(sh.layer);
            float strokeW = (sh.elemKind == 2 && sh.widthDbu > 0.0)
                ? static_cast<float>(std::clamp(sh.widthDbu * xf.scaleMag * coordScale, 0.06, 4.0))
                : 0.14f;

            for (size_t i = 0; i + 1 < sh.pts.size() && out.traces.size() < kMaxTraces; ++i) {
                GdsPt p0 = xf.Apply(sh.pts[i].x, sh.pts[i].y);
                GdsPt p1 = xf.Apply(sh.pts[i + 1].x, sh.pts[i + 1].y);
                float x0 = static_cast<float>(p0.x * coordScale);
                float y0 = static_cast<float>(p0.y * coordScale);
                float x1 = static_cast<float>(p1.x * coordScale);
                float y1 = static_cast<float>(p1.y * coordScale);
                out.traces.push_back({ x0, y0, x1, y1, strokeW, lid, sh.layer + 1 });
            }
        }

        for (const auto& tx : cell.texts) {
            if (out.labels.size() >= kMaxLabels) break;
            distinctLayers.insert(tx.layer);
            GdsPt pt = xf.Apply(tx.pos.x, tx.pos.y);
            out.labels.push_back({
                static_cast<float>(pt.x * coordScale),
                static_cast<float>(pt.y * coordScale),
                1.0f,
                mapGdsLayer(tx.layer),
                PathUtils::Utf8ToWide(tx.text)
            });
        }

        for (const auto& inst : cell.insts) {
            if (out.traces.size() >= kMaxTraces) break;
            const bool reflectX = (inst.strans & 0x8000u) != 0u;

            if (!inst.isAref) {
                const GdsPt& o = inst.xy[0];
                Mat2D childXf = composeMat(xf, reflectX, inst.mag, inst.angleDeg, o.x, o.y);
                if (depth == 0 && out.components.size() < 256) {
                    GdsPt wOrigin = xf.Apply(o.x, o.y);
                    EdaComponent3D mac;
                    mac.refDes = PathUtils::Utf8ToWide(inst.sname);
                    mac.footprint = L"GDS_SREF";
                    mac.x = static_cast<float>(wOrigin.x * coordScale);
                    mac.y = static_cast<float>(wOrigin.y * coordScale);
                    mac.rotDeg = static_cast<float>(inst.angleDeg);
                    mac.widthMm = 2.0f; mac.lengthMm = 2.0f; mac.heightMm = 0.8f;
                    mac.category = EdaPartCategory::IC_Package;
                    out.components.push_back(std::move(mac));
                }
                self(self, inst.sname, childXf, depth + 1);
            } else {
                const GdsPt o = inst.xy[0];
                const double colStepX = (inst.xy.size() >= 2 && inst.cols > 0)
                    ? (inst.xy[1].x - o.x) / static_cast<double>(inst.cols) : 0.0;
                const double colStepY = (inst.xy.size() >= 2 && inst.cols > 0)
                    ? (inst.xy[1].y - o.y) / static_cast<double>(inst.cols) : 0.0;
                const double rowStepX = (inst.xy.size() >= 3 && inst.rows > 0)
                    ? (inst.xy[2].x - o.x) / static_cast<double>(inst.rows) : 0.0;
                const double rowStepY = (inst.xy.size() >= 3 && inst.rows > 0)
                    ? (inst.xy[2].y - o.y) / static_cast<double>(inst.rows) : 0.0;

                if (depth == 0 && out.components.size() < 256) {
                    GdsPt wOrigin = xf.Apply(o.x, o.y);
                    EdaComponent3D mac;
                    mac.refDes = PathUtils::Utf8ToWide(inst.sname) + L"[" +
                                 std::to_wstring(inst.cols) + L"x" + std::to_wstring(inst.rows) + L"]";
                    mac.footprint = L"GDS_AREF";
                    mac.x = static_cast<float>(wOrigin.x * coordScale);
                    mac.y = static_cast<float>(wOrigin.y * coordScale);
                    mac.rotDeg = static_cast<float>(inst.angleDeg);
                    mac.widthMm = 2.5f; mac.lengthMm = 2.5f; mac.heightMm = 0.8f;
                    mac.category = EdaPartCategory::IC_Package;
                    out.components.push_back(std::move(mac));
                }

                for (int r = 0; r < inst.rows && out.traces.size() < kMaxTraces; ++r) {
                    for (int c = 0; c < inst.cols && out.traces.size() < kMaxTraces; ++c) {
                        double ix = o.x + c * colStepX + r * rowStepX;
                        double iy = o.y + c * colStepY + r * rowStepY;
                        Mat2D elemXf = composeMat(xf, reflectX, inst.mag, inst.angleDeg, ix, iy);
                        self(self, inst.sname, elemXf, depth + 1);
                    }
                }
            }
        }

        activeStack.erase(cellName);
    };

    // Place multiple unreferenced root cells side-by-side if more than one exists
    double rootOffsetX = 0.0;
    for (const auto& rootName : roots) {
        Mat2D rootXf;
        rootXf.tx = rootOffsetX;
        size_t beforeTraces = out.traces.size();
        flattenCell(flattenCell, rootName, rootXf, 0);

        if (out.traces.size() > beforeTraces) {
            float rMinX = 1e9f, rMaxX = -1e9f, rMaxY = -1e9f;
            for (size_t ti = beforeTraces; ti < out.traces.size(); ++ti) {
                rMinX = std::min({ rMinX, out.traces[ti].x1, out.traces[ti].x2 });
                rMaxX = std::max({ rMaxX, out.traces[ti].x1, out.traces[ti].x2 });
                rMaxY = std::max({ rMaxY, out.traces[ti].y1, out.traces[ti].y2 });
            }
            if (out.labels.size() < kMaxLabels && rMinX <= rMaxX) {
                out.labels.push_back({
                    rMinX,
                    rMaxY + std::max(0.8f, (rMaxX - rMinX) * 0.04f),
                    1.15f,
                    EdaLayerId::TopSilk,
                    PathUtils::Utf8ToWide(rootName) + L" (" + unitTag + L")"
                });
                rootOffsetX = (static_cast<double>(rMaxX) / std::max(coordScale, 1e-12)) +
                              (static_cast<double>(std::max(5.0f, (rMaxX - rMinX) * 0.25f)) / std::max(coordScale, 1e-12));
            }
        }
    }

    if (out.traces.empty()) return false;

    out.netCount = std::max<size_t>(1, distinctLayers.size());
    std::wstring hdrLib = libName.empty()
        ? (roots.empty() ? L"IC" : PathUtils::Utf8ToWide(roots.front()))
        : PathUtils::Utf8ToWide(libName);
    out.formatTitle = L"GDSII IC Layout Stream — " + hdrLib +
                      L" (" + std::to_wstring(cellMap.size()) + L" cells, " +
                      std::to_wstring(distinctLayers.size()) + L" layers, " + unitTag + L")";
    return true;
}

} // namespace Formats
