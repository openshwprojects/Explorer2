#pragma once
#include "../../XmlTagScan.h"
// ColladaDae.h — COLLADA 1.4 / 1.5 (.dae) XML Geometry, Controller & Visual-Scene 3D Parser

namespace Formats {

namespace DetailDae {

// XML tag scanning lives in XmlTagScan.h (shared with the other XML-based 3D readers)
using ::Explorer2::XmlTagScan::XmlTag;
using ::Explorer2::XmlTagScan::IsWs;
using ::Explorer2::XmlTagScan::IsNameChar;
using ::Explorer2::XmlTagScan::TrimWs;
using ::Explorer2::XmlTagScan::NextTag;
using ::Explorer2::XmlTagScan::FindElementBody;
using ::Explorer2::XmlTagScan::GetAttr;

inline std::string_view StripHash(std::string_view s) {
    s = TrimWs(s);
    if (!s.empty() && s.front() == '#') s.remove_prefix(1);
    return s;
}

inline bool ParseOneFloat(std::string_view s, float& out) {
    s = TrimWs(s);
    if (s.empty()) return false;
    if (s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return false;
    float v = 0.0f;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || !std::isfinite(v)) return false;
    out = v;
    return true;
}

inline bool ParseOneU32(std::string_view s, uint32_t& out) {
    s = TrimWs(s);
    if (s.empty()) return false;
    uint32_t v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    out = v;
    return true;
}

inline void ParseFloats(std::string_view text, std::vector<float>& out, size_t maxCount = 12000000) {
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end && out.size() < maxCount) {
        while (p < end && IsWs(*p)) ++p;
        if (p >= end) break;
        const char* tokStart = p;
        if (*p == '+') ++p;
        float val = 0.0f;
        auto res = std::from_chars(p, end, val);
        if (res.ec != std::errc{} || res.ptr == p) {
            // Advance past non-whitespace token
            p = tokStart;
            while (p < end && !IsWs(*p)) ++p;
            continue;
        }
        p = res.ptr;
        if (std::isfinite(val)) out.push_back(val);
        else out.push_back(0.0f);
    }
}

inline void ParseUInts(std::string_view text, std::vector<uint32_t>& out, size_t maxCount = 24000000) {
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end && out.size() < maxCount) {
        while (p < end && IsWs(*p)) ++p;
        if (p >= end) break;
        if (*p == '-') {
            // Negative index invalid in COLLADA <p>/<vcount>; skip token safely
            while (p < end && !IsWs(*p)) ++p;
            out.push_back(0xFFFFFFFFu);
            continue;
        }
        if (*p == '+') ++p;
        uint32_t val = 0;
        auto res = std::from_chars(p, end, val);
        if (res.ec != std::errc{} || res.ptr == p) {
            while (p < end && !IsWs(*p)) ++p;
            continue;
        }
        p = res.ptr;
        out.push_back(val);
    }
}

inline void ParseSignedInts(std::string_view text, std::vector<int32_t>& out, size_t maxCount = 24000000) {
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end && out.size() < maxCount) {
        while (p < end && IsWs(*p)) ++p;
        if (p >= end) break;
        if (*p == '+') ++p;
        int32_t val = 0;
        auto res = std::from_chars(p, end, val);
        if (res.ec != std::errc{} || res.ptr == p) {
            while (p < end && !IsWs(*p)) ++p;
            continue;
        }
        p = res.ptr;
        out.push_back(val);
    }
}

inline void ParseTokens(std::string_view text, std::vector<std::string>& out, size_t maxCount = 65536) {
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end && out.size() < maxCount) {
        while (p < end && IsWs(*p)) ++p;
        if (p >= end) break;
        const char* s = p;
        while (p < end && !IsWs(*p)) ++p;
        out.emplace_back(s, static_cast<size_t>(p - s));
    }
}

// Row-major 4x4 matrix matching COLLADA <matrix> specification:
// m[row * 4 + col]
struct Mat4Row {
    std::array<float, 16> m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    static Mat4Row Identity() { return Mat4Row{}; }

    static Mat4Row Translate(float tx, float ty, float tz) {
        Mat4Row r;
        r.m[3]  = tx;
        r.m[7]  = ty;
        r.m[11] = tz;
        return r;
    }

    static Mat4Row Scale(float sx, float sy, float sz) {
        Mat4Row r;
        r.m[0]  = sx;
        r.m[5]  = sy;
        r.m[10] = sz;
        return r;
    }

    static Mat4Row RotateAxisDeg(float ax, float ay, float az, float angleDeg) {
        float lenSq = ax * ax + ay * ay + az * az;
        if (lenSq <= 1e-20f) return Identity();
        float inv = 1.0f / std::sqrt(lenSq);
        ax *= inv; ay *= inv; az *= inv;
        float rad = angleDeg * (3.14159265358979323846f / 180.0f);
        float c = std::cos(rad);
        float s = std::sin(rad);
        float t = 1.0f - c;

        Mat4Row r;
        r.m[0]  = t * ax * ax + c;
        r.m[1]  = t * ax * ay - s * az;
        r.m[2]  = t * ax * az + s * ay;
        r.m[3]  = 0.0f;

        r.m[4]  = t * ax * ay + s * az;
        r.m[5]  = t * ay * ay + c;
        r.m[6]  = t * ay * az - s * ax;
        r.m[7]  = 0.0f;

        r.m[8]  = t * ax * az - s * ay;
        r.m[9]  = t * ay * az + s * ax;
        r.m[10] = t * az * az + c;
        r.m[11] = 0.0f;
        return r;
    }

    static Mat4Row Mul(const Mat4Row& a, const Mat4Row& b) {
        Mat4Row r;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                r.m[row * 4 + col] =
                    a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                    a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                    a.m[row * 4 + 2] * b.m[2 * 4 + col] +
                    a.m[row * 4 + 3] * b.m[3 * 4 + col];
            }
        }
        return r;
    }

    Vec3f TransformPoint(const Vec3f& v) const {
        return {
            m[0] * v.x + m[1] * v.y + m[2]  * v.z + m[3],
            m[4] * v.x + m[5] * v.y + m[6]  * v.z + m[7],
            m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11]
        };
    }
};

enum class UpAxis : uint8_t { Y_UP, Z_UP, X_UP };

inline Vec3f ConvertUpAxis(const Vec3f& p, UpAxis axis) {
    switch (axis) {
        case UpAxis::Z_UP: return { p.x, p.z, -p.y };
        case UpAxis::X_UP: return { -p.y, p.x, p.z };
        case UpAxis::Y_UP:
        default:           return p;
    }
}

struct DaeSource {
    std::vector<float> floats;
    uint32_t stride = 3;
    uint32_t offset = 0;
    uint32_t declaredCount = 0;
};

struct DaeGeometry {
    std::string id;
    std::vector<Vec3f> positions;
    std::vector<std::array<uint32_t, 3>> tris;
};

struct DaeController {
    std::string sourceId;
    Mat4Row bindShapeMat = Mat4Row::Identity();
    bool isSkin = false;
    std::vector<std::string> jointNames;
    std::vector<Mat4Row> invBindMats;
    std::vector<std::pair<int32_t, float>> influences;        // {jointIdx (-1 = bind shape), weight}
    std::vector<std::pair<uint32_t, uint32_t>> vertInfSpan;   // {start, count} per vertex
};

inline void ParseDaeSkinBody(std::string_view skinBody, DaeController& dc) {
    dc.isSkin = true;
    struct SkinSrc {
        std::vector<float> floats;
        std::vector<std::string> names;
        uint32_t stride = 1;
    };
    std::unordered_map<std::string, SkinSrc> sources;
    std::string jointNamesSrcId;
    std::string ibmSrcId;
    std::string vwJointSrcId;
    std::string vwWeightSrcId;
    uint32_t vwJointOff = 0, vwWeightOff = 1, vwStride = 2;
    bool hasVwJoint = false, hasVwWeight = false;
    std::vector<uint32_t> vwCounts;
    std::vector<int32_t> vwIndices;

    size_t bp = 0;
    XmlTag bt;
    while (NextTag(skinBody, bp, bt)) {
        if (bt.isClosing) continue;
        if (bt.localName == "bind_shape_matrix" && !bt.isSelfClosing) {
            std::string_view bsmTxt;
            if (FindElementBody(skinBody, bp, "bind_shape_matrix", bsmTxt, bp)) {
                std::vector<float> vals;
                ParseFloats(bsmTxt, vals, 16);
                if (vals.size() == 16) {
                    for (size_t k = 0; k < 16; ++k) dc.bindShapeMat.m[k] = vals[k];
                }
            }
        } else if (bt.localName == "source" && !bt.isSelfClosing) {
            std::string srcId(GetAttr(bt.attrs, "id"));
            std::string_view srcBody;
            if (!FindElementBody(skinBody, bp, "source", srcBody, bp)) break;
            if (srcId.empty()) continue;
            SkinSrc ss;
            size_t sp = 0;
            XmlTag st;
            while (NextTag(srcBody, sp, st)) {
                if (st.isClosing) continue;
                if (st.localName == "float_array" && !st.isSelfClosing) {
                    std::string_view faBody;
                    if (!FindElementBody(srcBody, sp, "float_array", faBody, sp)) break;
                    ParseFloats(faBody, ss.floats);
                } else if ((st.localName == "Name_array" || st.localName == "IDREF_array") && !st.isSelfClosing) {
                    std::string_view naBody;
                    if (!FindElementBody(srcBody, sp, st.localName, naBody, sp)) break;
                    ParseTokens(naBody, ss.names);
                } else if (st.localName == "accessor") {
                    uint32_t stVal = 0;
                    if (ParseOneU32(GetAttr(st.attrs, "stride"), stVal) && stVal > 0) ss.stride = stVal;
                    if (!st.isSelfClosing) {
                        std::string_view dummy;
                        FindElementBody(srcBody, sp, "accessor", dummy, sp);
                    }
                }
            }
            sources[srcId] = std::move(ss);
        } else if (bt.localName == "joints" && !bt.isSelfClosing) {
            std::string_view jBody;
            if (!FindElementBody(skinBody, bp, "joints", jBody, bp)) break;
            size_t jp = 0;
            XmlTag jt;
            while (NextTag(jBody, jp, jt)) {
                if (!jt.isClosing && jt.localName == "input") {
                    std::string_view sem = GetAttr(jt.attrs, "semantic");
                    std::string ref(StripHash(GetAttr(jt.attrs, "source")));
                    if (sem == "JOINT") jointNamesSrcId = ref;
                    else if (sem == "INV_BIND_MATRIX") ibmSrcId = ref;
                }
            }
        } else if (bt.localName == "vertex_weights" && !bt.isSelfClosing) {
            std::string_view vwBody;
            if (!FindElementBody(skinBody, bp, "vertex_weights", vwBody, bp)) break;
            uint32_t maxOff = 0;
            bool anyIn = false;
            size_t vp = 0;
            XmlTag vt;
            while (NextTag(vwBody, vp, vt)) {
                if (vt.isClosing) continue;
                if (vt.localName == "input") {
                    uint32_t off = 0;
                    ParseOneU32(GetAttr(vt.attrs, "offset"), off);
                    if (!anyIn || off > maxOff) maxOff = off;
                    anyIn = true;
                    std::string_view sem = GetAttr(vt.attrs, "semantic");
                    std::string ref(StripHash(GetAttr(vt.attrs, "source")));
                    if (sem == "JOINT") { vwJointSrcId = ref; vwJointOff = off; hasVwJoint = true; }
                    else if (sem == "WEIGHT") { vwWeightSrcId = ref; vwWeightOff = off; hasVwWeight = true; }
                    if (!vt.isSelfClosing) {
                        std::string_view dummy;
                        FindElementBody(vwBody, vp, "input", dummy, vp);
                    }
                } else if (vt.localName == "vcount" && !vt.isSelfClosing) {
                    std::string_view vcTxt;
                    if (FindElementBody(vwBody, vp, "vcount", vcTxt, vp)) ParseUInts(vcTxt, vwCounts);
                } else if (vt.localName == "v" && !vt.isSelfClosing) {
                    std::string_view vTxt;
                    if (FindElementBody(vwBody, vp, "v", vTxt, vp)) ParseSignedInts(vTxt, vwIndices);
                } else if (!vt.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(vwBody, vp, vt.localName, skip, vp);
                }
            }
            if (anyIn) vwStride = maxOff + 1u;
        } else if (!bt.isSelfClosing) {
            std::string_view skip;
            FindElementBody(skinBody, bp, bt.localName, skip, bp);
        }
    }

    const std::string& jSrcKey = !vwJointSrcId.empty() ? vwJointSrcId : jointNamesSrcId;
    if (!jSrcKey.empty()) {
        auto itJ = sources.find(jSrcKey);
        if (itJ != sources.end()) dc.jointNames = itJ->second.names;
    }
    if (!ibmSrcId.empty()) {
        auto itM = sources.find(ibmSrcId);
        if (itM != sources.end() && itM->second.floats.size() >= 16) {
            size_t nMats = itM->second.floats.size() / 16;
            dc.invBindMats.resize(nMats);
            for (size_t m = 0; m < nMats; ++m) {
                for (size_t k = 0; k < 16; ++k) dc.invBindMats[m].m[k] = itM->second.floats[m * 16 + k];
            }
        }
    }
    if (hasVwJoint && hasVwWeight && !vwCounts.empty() && !vwIndices.empty() && vwStride >= 2) {
        auto itW = sources.find(vwWeightSrcId);
        if (itW != sources.end() && !itW->second.floats.empty()) {
            const auto& wArr = itW->second.floats;
            dc.vertInfSpan.reserve(vwCounts.size());
            size_t cursor = 0;
            for (uint32_t cnt : vwCounts) {
                if (cnt > 256 || cursor + static_cast<size_t>(cnt) * vwStride > vwIndices.size()) break;
                uint32_t startOut = static_cast<uint32_t>(dc.influences.size());
                uint32_t validCnt = 0;
                for (uint32_t c = 0; c < cnt; ++c) {
                    int32_t jIdx = vwIndices[cursor + static_cast<size_t>(c) * vwStride + vwJointOff];
                    int32_t wIdx = vwIndices[cursor + static_cast<size_t>(c) * vwStride + vwWeightOff];
                    if (wIdx >= 0 && static_cast<size_t>(wIdx) < wArr.size() &&
                        (jIdx == -1 || (jIdx >= 0 && static_cast<size_t>(jIdx) < dc.jointNames.size()))) {
                        float w = wArr[static_cast<size_t>(wIdx)];
                        if (std::isfinite(w) && w > 1e-7f) {
                            dc.influences.emplace_back(jIdx, w);
                            ++validCnt;
                        }
                    }
                }
                dc.vertInfSpan.emplace_back(startOut, validCnt);
                cursor += static_cast<size_t>(cnt) * vwStride;
            }
        }
    }
}

inline void ParseDaeMeshBody(std::string_view meshBody, DaeGeometry& geomOut) {
    std::unordered_map<std::string, DaeSource> sources;
    std::unordered_map<std::string, std::string> verticesToPosSrc;
    std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> importedSrcRange; // srcId -> {baseVert, count}

    auto ensurePositionSourceLoaded = [&](const std::string& srcId) -> std::pair<uint32_t, uint32_t> {
        auto itImp = importedSrcRange.find(srcId);
        if (itImp != importedSrcRange.end()) return itImp->second;

        auto itSrc = sources.find(srcId);
        if (itSrc == sources.end()) return { 0u, 0u };
        const DaeSource& src = itSrc->second;
        if (src.stride == 0 || src.floats.size() <= src.offset) return { 0u, 0u };

        size_t avail = (src.floats.size() - src.offset) / src.stride;
        if (src.declaredCount > 0 && static_cast<size_t>(src.declaredCount) < avail) {
            avail = static_cast<size_t>(src.declaredCount);
        }
        if (avail == 0 || avail > 4000000) return { 0u, 0u };

        uint32_t base = static_cast<uint32_t>(geomOut.positions.size());
        geomOut.positions.reserve(geomOut.positions.size() + avail);
        for (size_t i = 0; i < avail; ++i) {
            size_t fIdx = static_cast<size_t>(src.offset) + i * static_cast<size_t>(src.stride);
            float x = src.floats[fIdx];
            float y = (src.stride >= 2 && fIdx + 1 < src.floats.size()) ? src.floats[fIdx + 1] : 0.0f;
            float z = (src.stride >= 3 && fIdx + 2 < src.floats.size()) ? src.floats[fIdx + 2] : 0.0f;
            geomOut.positions.push_back({ x, y, z });
        }
        auto res = std::make_pair(base, static_cast<uint32_t>(avail));
        importedSrcRange[srcId] = res;
        return res;
    };

    // Pass 1: collect <source> and <vertices>; Pass 2 (in order, since sources/vertices precede primitives,
    // or defer primitives into a small list so any XML element ordering works!)
    struct PendingPrim {
        std::string_view kind;
        std::string_view body;
    };
    std::vector<PendingPrim> prims;

    size_t pos = 0;
    XmlTag tag;
    while (NextTag(meshBody, pos, tag)) {
        if (tag.isClosing) continue;

        if (tag.localName == "source") {
            std::string id(GetAttr(tag.attrs, "id"));
            if (tag.isSelfClosing) continue;
            std::string_view srcBody;
            if (!FindElementBody(meshBody, pos, "source", srcBody, pos)) break;
            if (id.empty()) continue;

            DaeSource ds;
            size_t sp = 0;
            XmlTag st;
            while (NextTag(srcBody, sp, st)) {
                if (st.isClosing) continue;
                if (st.localName == "float_array" && !st.isSelfClosing) {
                    uint32_t cntHint = 0;
                    ParseOneU32(GetAttr(st.attrs, "count"), cntHint);
                    std::string_view faBody;
                    if (!FindElementBody(srcBody, sp, "float_array", faBody, sp)) break;
                    if (cntHint > 0 && cntHint <= 12000000u) ds.floats.reserve(cntHint);
                    ParseFloats(faBody, ds.floats);
                } else if (st.localName == "accessor") {
                    uint32_t stride = 0, off = 0, cnt = 0;
                    if (ParseOneU32(GetAttr(st.attrs, "stride"), stride) && stride > 0) ds.stride = stride;
                    if (ParseOneU32(GetAttr(st.attrs, "offset"), off)) ds.offset = off;
                    if (ParseOneU32(GetAttr(st.attrs, "count"), cnt)) ds.declaredCount = cnt;
                    if (!st.isSelfClosing) {
                        std::string_view dummy;
                        FindElementBody(srcBody, sp, "accessor", dummy, sp);
                    }
                }
            }
            sources[id] = std::move(ds);
        } else if (tag.localName == "vertices") {
            std::string vId(GetAttr(tag.attrs, "id"));
            if (tag.isSelfClosing) continue;
            std::string_view vBody;
            if (!FindElementBody(meshBody, pos, "vertices", vBody, pos)) break;
            if (vId.empty()) continue;

            size_t vp = 0;
            XmlTag vt;
            while (NextTag(vBody, vp, vt)) {
                if (!vt.isClosing && vt.localName == "input") {
                    std::string_view sem = GetAttr(vt.attrs, "semantic");
                    if (sem == "POSITION") {
                        std::string_view srcRef = StripHash(GetAttr(vt.attrs, "source"));
                        if (!srcRef.empty()) {
                            verticesToPosSrc[vId] = std::string(srcRef);
                        }
                    }
                }
            }
        } else if (tag.localName == "triangles" || tag.localName == "polylist" ||
                   tag.localName == "polygons"  || tag.localName == "tristrips" ||
                   tag.localName == "trifans") {
            if (tag.isSelfClosing) continue;
            std::string_view pBody;
            if (!FindElementBody(meshBody, pos, tag.localName, pBody, pos)) break;
            prims.push_back({ tag.localName, pBody });
        } else if (!tag.isSelfClosing) {
            std::string_view skipBody;
            if (!FindElementBody(meshBody, pos, tag.localName, skipBody, pos)) break;
        }
    }

    // Evaluate all collected mesh primitives
    for (const PendingPrim& pr : prims) {
        uint32_t maxOffset = 0;
        bool hasAnyInput = false;
        uint32_t posInputOffset = 0;
        std::string posSourceId;

        std::vector<uint32_t> vcounts;
        std::vector<std::string_view> pBlocks;

        size_t pp = 0;
        XmlTag pt;
        while (NextTag(pr.body, pp, pt)) {
            if (pt.isClosing) continue;
            if (pt.localName == "input") {
                uint32_t off = 0;
                ParseOneU32(GetAttr(pt.attrs, "offset"), off);
                if (!hasAnyInput || off > maxOffset) maxOffset = off;
                hasAnyInput = true;

                std::string_view sem = GetAttr(pt.attrs, "semantic");
                std::string refId(StripHash(GetAttr(pt.attrs, "source")));
                if (sem == "VERTEX") {
                    auto itV = verticesToPosSrc.find(refId);
                    if (itV != verticesToPosSrc.end()) {
                        posSourceId = itV->second;
                        posInputOffset = off;
                    }
                } else if (sem == "POSITION" && posSourceId.empty()) {
                    posSourceId = refId;
                    posInputOffset = off;
                }
                if (!pt.isSelfClosing) {
                    std::string_view dummy;
                    FindElementBody(pr.body, pp, "input", dummy, pp);
                }
            } else if (pt.localName == "vcount" && !pt.isSelfClosing) {
                std::string_view vcBody;
                if (!FindElementBody(pr.body, pp, "vcount", vcBody, pp)) break;
                ParseUInts(vcBody, vcounts);
            } else if (pt.localName == "p" && !pt.isSelfClosing) {
                std::string_view pText;
                if (!FindElementBody(pr.body, pp, "p", pText, pp)) break;
                pBlocks.push_back(pText);
            } else if (pt.localName == "ph" && !pt.isSelfClosing) {
                std::string_view phBody;
                if (!FindElementBody(pr.body, pp, "ph", phBody, pp)) break;
                size_t php = 0;
                XmlTag pht;
                while (NextTag(phBody, php, pht)) {
                    if (!pht.isClosing && pht.localName == "p" && !pht.isSelfClosing) {
                        std::string_view outerP;
                        if (FindElementBody(phBody, php, "p", outerP, php)) {
                            pBlocks.push_back(outerP);
                        }
                        break; // only outer boundary <p>, skip <h> holes
                    } else if (!pht.isClosing && !pht.isSelfClosing) {
                        std::string_view skipH;
                        FindElementBody(phBody, php, pht.localName, skipH, php);
                    }
                }
            } else if (!pt.isSelfClosing) {
                std::string_view skipOther;
                if (!FindElementBody(pr.body, pp, pt.localName, skipOther, pp)) break;
            }
        }

        if (posSourceId.empty() && sources.size() == 1) {
            posSourceId = sources.begin()->first;
            posInputOffset = 0;
        }
        auto [bVert, vCnt] = ensurePositionSourceLoaded(posSourceId);
        const auto baseVert = bVert;
        const auto vertCount = vCnt;
        if (vertCount < 3 || pBlocks.empty()) continue;

        const size_t stride = hasAnyInput ? static_cast<size_t>(maxOffset + 1u) : 1u;
        if (posInputOffset >= stride) continue;

        auto emitTri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
            if (i0 < vertCount && i1 < vertCount && i2 < vertCount &&
                i0 != i1 && i1 != i2 && i0 != i2) {
                geomOut.tris.push_back({ baseVert + i0, baseVert + i1, baseVert + i2 });
            }
        };

        if (pr.kind == "triangles") {
            for (std::string_view pTxt : pBlocks) {
                std::vector<uint32_t> idx;
                ParseUInts(pTxt, idx);
                const size_t step3 = 3u * stride;
                for (size_t k = 0; k + step3 <= idx.size(); k += step3) {
                    emitTri(idx[k + posInputOffset],
                            idx[k + stride + posInputOffset],
                            idx[k + 2u * stride + posInputOffset]);
                }
            }
        } else if (pr.kind == "polylist") {
            std::vector<uint32_t> idx;
            for (std::string_view pTxt : pBlocks) ParseUInts(pTxt, idx);
            if (!vcounts.empty()) {
                size_t cursor = 0;
                for (uint32_t vc : vcounts) {
                    if (vc == 0xFFFFFFFFu) break;
                    size_t span = static_cast<size_t>(vc) * stride;
                    if (cursor + span > idx.size()) break;
                    if (vc >= 3) {
                        uint32_t v0 = idx[cursor + posInputOffset];
                        for (uint32_t j = 1; j + 1 < vc; ++j) {
                            uint32_t v1 = idx[cursor + static_cast<size_t>(j) * stride + posInputOffset];
                            uint32_t v2 = idx[cursor + static_cast<size_t>(j + 1) * stride + posInputOffset];
                            emitTri(v0, v1, v2);
                        }
                    }
                    cursor += span;
                }
            } else {
                const size_t step3 = 3u * stride;
                for (size_t k = 0; k + step3 <= idx.size(); k += step3) {
                    emitTri(idx[k + posInputOffset],
                            idx[k + stride + posInputOffset],
                            idx[k + 2u * stride + posInputOffset]);
                }
            }
        } else if (pr.kind == "polygons") {
            for (std::string_view pTxt : pBlocks) {
                std::vector<uint32_t> idx;
                ParseUInts(pTxt, idx);
                size_t nPolyVerts = idx.size() / stride;
                if (nPolyVerts >= 3) {
                    uint32_t v0 = idx[posInputOffset];
                    for (size_t j = 1; j + 1 < nPolyVerts; ++j) {
                        emitTri(v0,
                                idx[j * stride + posInputOffset],
                                idx[(j + 1u) * stride + posInputOffset]);
                    }
                }
            }
        } else if (pr.kind == "tristrips") {
            for (std::string_view pTxt : pBlocks) {
                std::vector<uint32_t> idx;
                ParseUInts(pTxt, idx);
                size_t nVerts = idx.size() / stride;
                for (size_t j = 0; j + 2 < nVerts; ++j) {
                    uint32_t v0 = idx[j * stride + posInputOffset];
                    uint32_t v1 = idx[(j + 1u) * stride + posInputOffset];
                    uint32_t v2 = idx[(j + 2u) * stride + posInputOffset];
                    if ((j & 1u) == 0u) emitTri(v0, v1, v2);
                    else                emitTri(v1, v0, v2);
                }
            }
        } else if (pr.kind == "trifans") {
            for (std::string_view pTxt : pBlocks) {
                std::vector<uint32_t> idx;
                ParseUInts(pTxt, idx);
                size_t nVerts = idx.size() / stride;
                if (nVerts >= 3) {
                    uint32_t v0 = idx[posInputOffset];
                    for (size_t j = 1; j + 1 < nVerts; ++j) {
                        emitTri(v0,
                                idx[j * stride + posInputOffset],
                                idx[(j + 1u) * stride + posInputOffset]);
                    }
                }
            }
        }
    }
}

inline void IndexAllNamedNodes(
    std::string_view body,
    std::unordered_map<std::string, std::string_view>& nodeMap,
    int depth = 0)
{
    if (depth > 64) return;
    size_t pos = 0;
    XmlTag t;
    while (NextTag(body, pos, t)) {
        if (t.isClosing) continue;
        if (t.localName == "node") {
            std::string id(GetAttr(t.attrs, "id"));
            if (t.isSelfClosing) continue;
            std::string_view nBody;
            if (!FindElementBody(body, pos, "node", nBody, pos)) break;
            if (!id.empty()) {
                nodeMap[id] = nBody;
            }
            IndexAllNamedNodes(nBody, nodeMap, depth + 1);
        } else if (!t.isSelfClosing) {
            std::string_view skip;
            if (!FindElementBody(body, pos, t.localName, skip, pos)) break;
        }
    }
}

} // namespace DetailDae

inline bool ParseColladaDaeMesh(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace DetailDae;
    if (sv.find("<COLLADA") == std::string_view::npos &&
        sv.find(":COLLADA") == std::string_view::npos) {
        return false;
    }
    mesh.formatName = L"COLLADA 3D (.dae)";

    UpAxis upAxis = UpAxis::Y_UP;
    float unitMeter = 1.0f;

    std::vector<DaeGeometry> geometries;
    std::unordered_map<std::string, size_t> geomById;
    std::unordered_map<std::string, DaeController> controllers;
    std::unordered_map<std::string, std::string_view> namedNodes;
    std::unordered_map<std::string, std::string_view> visualScenes;
    std::vector<std::string_view> visualSceneOrder;
    std::string activeVisualSceneId;
    size_t animClipCount = 0;

    // Locate <COLLADA> root body (or scan whole document if root wrapper variant)
    std::string_view docBody = sv;
    {
        size_t p = 0;
        XmlTag rt;
        while (NextTag(sv, p, rt)) {
            if (!rt.isClosing && rt.localName == "COLLADA" && !rt.isSelfClosing) {
                std::string_view rootInner;
                size_t endRoot = 0;
                if (FindElementBody(sv, p, "COLLADA", rootInner, endRoot)) {
                    docBody = rootInner;
                }
                break;
            }
        }
    }

    size_t pos = 0;
    XmlTag tag;
    while (NextTag(docBody, pos, tag)) {
        if (tag.isClosing) continue;

        if (tag.localName == "asset" && !tag.isSelfClosing) {
            std::string_view assetBody;
            if (!FindElementBody(docBody, pos, "asset", assetBody, pos)) break;
            size_t ap = 0;
            XmlTag at;
            while (NextTag(assetBody, ap, at)) {
                if (at.isClosing) continue;
                if (at.localName == "up_axis" && !at.isSelfClosing) {
                    std::string_view uaTxt;
                    if (FindElementBody(assetBody, ap, "up_axis", uaTxt, ap)) {
                        uaTxt = TrimWs(uaTxt);
                        if (uaTxt == "Z_UP") upAxis = UpAxis::Z_UP;
                        else if (uaTxt == "X_UP") upAxis = UpAxis::X_UP;
                        else upAxis = UpAxis::Y_UP;
                    }
                } else if (at.localName == "unit") {
                    float m = 0.0f;
                    if (ParseOneFloat(GetAttr(at.attrs, "meter"), m) && m > 0.0f) {
                        unitMeter = m;
                    }
                    if (!at.isSelfClosing) {
                        std::string_view dummy;
                        FindElementBody(assetBody, ap, "unit", dummy, ap);
                    }
                } else if (!at.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(assetBody, ap, at.localName, skip, ap);
                }
            }
        } else if (tag.localName == "library_geometries" && !tag.isSelfClosing) {
            std::string_view libBody;
            if (!FindElementBody(docBody, pos, "library_geometries", libBody, pos)) break;
            size_t gp = 0;
            XmlTag gt;
            while (NextTag(libBody, gp, gt)) {
                if (gt.isClosing) continue;
                if (gt.localName == "geometry" && !gt.isSelfClosing) {
                    std::string geomId(GetAttr(gt.attrs, "id"));
                    std::string_view geomBody;
                    if (!FindElementBody(libBody, gp, "geometry", geomBody, gp)) break;

                    DaeGeometry dg;
                    dg.id = geomId;
                    size_t mp = 0;
                    XmlTag mt;
                    while (NextTag(geomBody, mp, mt)) {
                        if (mt.isClosing) continue;
                        if ((mt.localName == "mesh" || mt.localName == "convex_mesh") && !mt.isSelfClosing) {
                            std::string_view mBody;
                            if (!FindElementBody(geomBody, mp, mt.localName, mBody, mp)) break;
                            ParseDaeMeshBody(mBody, dg);
                        } else if (!mt.isSelfClosing) {
                            std::string_view skip;
                            FindElementBody(geomBody, mp, mt.localName, skip, mp);
                        }
                    }

                    if (!dg.positions.empty() && !dg.tris.empty()) {
                        size_t idx = geometries.size();
                        if (!dg.id.empty()) geomById[dg.id] = idx;
                        geometries.push_back(std::move(dg));
                    }
                } else if (!gt.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(libBody, gp, gt.localName, skip, gp);
                }
            }
        } else if (tag.localName == "library_controllers" && !tag.isSelfClosing) {
            std::string_view libBody;
            if (!FindElementBody(docBody, pos, "library_controllers", libBody, pos)) break;
            size_t cp = 0;
            XmlTag ct;
            while (NextTag(libBody, cp, ct)) {
                if (ct.isClosing) continue;
                if (ct.localName == "controller" && !ct.isSelfClosing) {
                    std::string ctrlId(GetAttr(ct.attrs, "id"));
                    std::string_view ctrlBody;
                    if (!FindElementBody(libBody, cp, "controller", ctrlBody, cp)) break;
                    if (ctrlId.empty()) continue;

                    DaeController dc;
                    size_t sp = 0;
                    XmlTag st;
                    while (NextTag(ctrlBody, sp, st)) {
                        if (st.isClosing) continue;
                        if ((st.localName == "skin" || st.localName == "morph") && !st.isSelfClosing) {
                            dc.sourceId = std::string(StripHash(GetAttr(st.attrs, "source")));
                            std::string_view skinBody;
                            if (!FindElementBody(ctrlBody, sp, st.localName, skinBody, sp)) break;
                            if (st.localName == "skin") {
                                ParseDaeSkinBody(skinBody, dc);
                            }
                        } else if (!st.isSelfClosing) {
                            std::string_view skip;
                            FindElementBody(ctrlBody, sp, st.localName, skip, sp);
                        }
                    }
                    if (!dc.sourceId.empty()) {
                        controllers[ctrlId] = dc;
                    }
                } else if (!ct.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(libBody, cp, ct.localName, skip, cp);
                }
            }
        } else if (tag.localName == "library_nodes" && !tag.isSelfClosing) {
            std::string_view libBody;
            if (!FindElementBody(docBody, pos, "library_nodes", libBody, pos)) break;
            IndexAllNamedNodes(libBody, namedNodes);
        } else if (tag.localName == "library_visual_scenes" && !tag.isSelfClosing) {
            std::string_view libBody;
            if (!FindElementBody(docBody, pos, "library_visual_scenes", libBody, pos)) break;
            size_t vp = 0;
            XmlTag vt;
            while (NextTag(libBody, vp, vt)) {
                if (vt.isClosing) continue;
                if (vt.localName == "visual_scene" && !vt.isSelfClosing) {
                    std::string vsId(GetAttr(vt.attrs, "id"));
                    std::string_view vsBody;
                    if (!FindElementBody(libBody, vp, "visual_scene", vsBody, vp)) break;
                    visualSceneOrder.push_back(vsBody);
                    if (!vsId.empty()) visualScenes[vsId] = vsBody;
                    IndexAllNamedNodes(vsBody, namedNodes);
                } else if (!vt.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(libBody, vp, vt.localName, skip, vp);
                }
            }
        } else if (tag.localName == "library_animations" && !tag.isSelfClosing) {
            std::string_view libBody;
            if (!FindElementBody(docBody, pos, "library_animations", libBody, pos)) break;
            size_t ap = 0;
            XmlTag at;
            while (NextTag(libBody, ap, at)) {
                if (!at.isClosing && at.localName == "animation") {
                    ++animClipCount;
                    if (!at.isSelfClosing) {
                        std::string_view skip;
                        if (!FindElementBody(libBody, ap, "animation", skip, ap)) break;
                    }
                } else if (!at.isClosing && !at.isSelfClosing) {
                    std::string_view skip;
                    if (!FindElementBody(libBody, ap, at.localName, skip, ap)) break;
                }
            }
        } else if (tag.localName == "scene" && !tag.isSelfClosing) {
            std::string_view scBody;
            if (!FindElementBody(docBody, pos, "scene", scBody, pos)) break;
            size_t sp = 0;
            XmlTag st;
            while (NextTag(scBody, sp, st)) {
                if (!st.isClosing && st.localName == "instance_visual_scene") {
                    activeVisualSceneId = std::string(StripHash(GetAttr(st.attrs, "url")));
                    break;
                }
            }
        } else if (!tag.isSelfClosing) {
            std::string_view skip;
            if (!FindElementBody(docBody, pos, tag.localName, skip, pos)) break;
        }
    }

    if (geometries.empty()) return false;

    constexpr size_t kMaxVerts = 4000000;
    constexpr size_t kMaxTris  = 4000000;

    auto emitGeometry = [&](const DaeGeometry& dg, const Mat4Row& worldMat) {
        if (mesh.vertices.size() + dg.positions.size() > kMaxVerts ||
            mesh.tris.size() + dg.tris.size() > kMaxTris) {
            return;
        }
        uint32_t baseOff = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.reserve(mesh.vertices.size() + dg.positions.size());
        for (const Vec3f& localPos : dg.positions) {
            Vec3f wp = worldMat.TransformPoint(localPos);
            wp.x *= unitMeter;
            wp.y *= unitMeter;
            wp.z *= unitMeter;
            Vec3f vp = ConvertUpAxis(wp, upAxis);
            if (!std::isfinite(vp.x) || !std::isfinite(vp.y) || !std::isfinite(vp.z)) {
                vp = { 0.0f, 0.0f, 0.0f };
            }
            mesh.vertices.push_back(vp);
        }
        mesh.tris.reserve(mesh.tris.size() + dg.tris.size());
        for (const auto& t : dg.tris) {
            mesh.tris.push_back({ baseOff + t[0], baseOff + t[1], baseOff + t[2], {} });
        }
    };

    auto resolveControllerToGeom = [&](std::string curId, Mat4Row& outBindMat) -> const DaeGeometry* {
        outBindMat = Mat4Row::Identity();
        for (int hop = 0; hop < 8; ++hop) {
            auto itG = geomById.find(curId);
            if (itG != geomById.end()) {
                return &geometries[itG->second];
            }
            auto itC = controllers.find(curId);
            if (itC == controllers.end()) return nullptr;
            outBindMat = Mat4Row::Mul(outBindMat, itC->second.bindShapeMat);
            curId = itC->second.sourceId;
        }
        return nullptr;
    };

    std::unordered_map<std::string, Mat4Row> jointWorldBySid;
    std::unordered_map<std::string, Mat4Row> jointWorldById;
    std::unordered_map<std::string, Mat4Row> jointWorldByName;

    auto lookupJointWorld = [&](const std::string& key, Mat4Row& outWorld) -> bool {
        auto itS = jointWorldBySid.find(key);
        if (itS != jointWorldBySid.end()) { outWorld = itS->second; return true; }
        auto itI = jointWorldById.find(key);
        if (itI != jointWorldById.end()) { outWorld = itI->second; return true; }
        auto itN = jointWorldByName.find(key);
        if (itN != jointWorldByName.end()) { outWorld = itN->second; return true; }
        return false;
    };

    auto emitSkinnedController = [&](const DaeController& dc, const DaeGeometry& dg, const Mat4Row& fallbackWorld) -> bool {
        if (!dc.isSkin || dc.jointNames.empty() || dc.vertInfSpan.empty()) return false;
        if (mesh.vertices.size() + dg.positions.size() > kMaxVerts ||
            mesh.tris.size() + dg.tris.size() > kMaxTris) {
            return true;
        }
        std::vector<Mat4Row> skinMats(dc.jointNames.size());
        std::vector<uint8_t> jointValid(dc.jointNames.size(), 0);
        size_t foundAnyJoint = 0;
        for (size_t j = 0; j < dc.jointNames.size(); ++j) {
            Mat4Row jWorld;
            if (lookupJointWorld(dc.jointNames[j], jWorld)) {
                const Mat4Row ibm = (j < dc.invBindMats.size()) ? dc.invBindMats[j] : Mat4Row::Identity();
                skinMats[j] = Mat4Row::Mul(jWorld, Mat4Row::Mul(ibm, dc.bindShapeMat));
                jointValid[j] = 1;
                ++foundAnyJoint;
            }
        }
        if (foundAnyJoint == 0) return false;

        uint32_t baseOff = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.reserve(mesh.vertices.size() + dg.positions.size());
        for (size_t vi = 0; vi < dg.positions.size(); ++vi) {
            const Vec3f& localPos = dg.positions[vi];
            Vec3f wp = { 0.0f, 0.0f, 0.0f };
            float wSum = 0.0f;
            if (vi < dc.vertInfSpan.size()) {
                auto [infStart, infCnt] = dc.vertInfSpan[vi];
                for (uint32_t k = 0; k < infCnt; ++k) {
                    auto [jIdx, w] = dc.influences[infStart + k];
                    Vec3f jp;
                    if (jIdx == -1) {
                        jp = dc.bindShapeMat.TransformPoint(localPos);
                    } else if (jIdx >= 0 && static_cast<size_t>(jIdx) < skinMats.size() && jointValid[static_cast<size_t>(jIdx)]) {
                        jp = skinMats[static_cast<size_t>(jIdx)].TransformPoint(localPos);
                    } else {
                        continue;
                    }
                    wp.x += jp.x * w;
                    wp.y += jp.y * w;
                    wp.z += jp.z * w;
                    wSum += w;
                }
            }
            if (wSum > 1e-6f) {
                float invW = 1.0f / wSum;
                wp.x *= invW; wp.y *= invW; wp.z *= invW;
            } else {
                wp = Mat4Row::Mul(fallbackWorld, dc.bindShapeMat).TransformPoint(localPos);
            }
            wp.x *= unitMeter; wp.y *= unitMeter; wp.z *= unitMeter;
            Vec3f vp = ConvertUpAxis(wp, upAxis);
            if (!std::isfinite(vp.x) || !std::isfinite(vp.y) || !std::isfinite(vp.z)) vp = { 0.0f, 0.0f, 0.0f };
            mesh.vertices.push_back(vp);
        }
        mesh.tris.reserve(mesh.tris.size() + dg.tris.size());
        for (const auto& t : dg.tris) {
            mesh.tris.push_back({ baseOff + t[0], baseOff + t[1], baseOff + t[2], {} });
        }
        return true;
    };

    std::vector<std::string> activeNodeIds;
    // Pass 1 over visual_scene: evaluate and index world matrices of all <node> elements by sid / id / name
    std::function<void(std::string_view, Mat4Row, std::string_view, int)> collectNodeMatrices =
        [&](std::string_view nBody, Mat4Row curWorld, std::string_view nodeAttrs, int depth) {
            if (depth > 64) return;
            size_t np = 0;
            XmlTag nt;
            std::vector<std::pair<std::string, std::string_view>> childNodes;
            std::vector<std::string> instNodeRefs;
            while (NextTag(nBody, np, nt)) {
                if (nt.isClosing) continue;
                if (nt.localName == "matrix" && !nt.isSelfClosing) {
                    std::string_view mTxt;
                    if (!FindElementBody(nBody, np, "matrix", mTxt, np)) break;
                    std::vector<float> vals;
                    ParseFloats(mTxt, vals, 16);
                    if (vals.size() == 16) {
                        Mat4Row m;
                        for (size_t k = 0; k < 16; ++k) m.m[k] = vals[k];
                        curWorld = Mat4Row::Mul(curWorld, m);
                    }
                } else if (nt.localName == "translate" && !nt.isSelfClosing) {
                    std::string_view tTxt;
                    if (!FindElementBody(nBody, np, "translate", tTxt, np)) break;
                    std::vector<float> vals;
                    ParseFloats(tTxt, vals, 3);
                    if (vals.size() >= 3) curWorld = Mat4Row::Mul(curWorld, Mat4Row::Translate(vals[0], vals[1], vals[2]));
                } else if (nt.localName == "rotate" && !nt.isSelfClosing) {
                    std::string_view rTxt;
                    if (!FindElementBody(nBody, np, "rotate", rTxt, np)) break;
                    std::vector<float> vals;
                    ParseFloats(rTxt, vals, 4);
                    if (vals.size() >= 4) curWorld = Mat4Row::Mul(curWorld, Mat4Row::RotateAxisDeg(vals[0], vals[1], vals[2], vals[3]));
                } else if (nt.localName == "scale" && !nt.isSelfClosing) {
                    std::string_view sTxt;
                    if (!FindElementBody(nBody, np, "scale", sTxt, np)) break;
                    std::vector<float> vals;
                    ParseFloats(sTxt, vals, 3);
                    if (vals.size() >= 3) curWorld = Mat4Row::Mul(curWorld, Mat4Row::Scale(vals[0], vals[1], vals[2]));
                } else if (nt.localName == "node") {
                    std::string attrsCopy(nt.attrs);
                    if (nt.isSelfClosing) continue;
                    std::string_view cBody;
                    if (!FindElementBody(nBody, np, "node", cBody, np)) break;
                    childNodes.emplace_back(std::move(attrsCopy), cBody);
                } else if (nt.localName == "instance_node") {
                    std::string refId(StripHash(GetAttr(nt.attrs, "url")));
                    if (!refId.empty()) instNodeRefs.push_back(std::move(refId));
                    if (!nt.isSelfClosing) {
                        std::string_view skip;
                        if (!FindElementBody(nBody, np, "instance_node", skip, np)) break;
                    }
                } else if (!nt.isSelfClosing) {
                    std::string_view skip;
                    if (!FindElementBody(nBody, np, nt.localName, skip, np)) break;
                }
            }
            if (!nodeAttrs.empty()) {
                std::string sid(GetAttr(nodeAttrs, "sid"));
                std::string id(GetAttr(nodeAttrs, "id"));
                std::string nm(GetAttr(nodeAttrs, "name"));
                if (!sid.empty()) jointWorldBySid[sid] = curWorld;
                if (!id.empty())  jointWorldById[id]   = curWorld;
                if (!nm.empty())  jointWorldByName[nm] = curWorld;
            }
            for (const auto& [cAttrs, cBody] : childNodes) {
                collectNodeMatrices(cBody, curWorld, cAttrs, depth + 1);
            }
            for (const std::string& refId : instNodeRefs) {
                auto itN = namedNodes.find(refId);
                if (itN != namedNodes.end() &&
                    std::find(activeNodeIds.begin(), activeNodeIds.end(), refId) == activeNodeIds.end()) {
                    activeNodeIds.push_back(refId);
                    collectNodeMatrices(itN->second, curWorld, "", depth + 1);
                    activeNodeIds.pop_back();
                }
            }
        };

    std::function<void(std::string_view, Mat4Row, int)> traverseNodeBody =
        [&](std::string_view nBody, Mat4Row curWorld, int depth) {
            if (depth > 64) return;
            size_t np = 0;
            XmlTag nt;
            while (NextTag(nBody, np, nt)) {
                if (nt.isClosing) continue;

                if (nt.localName == "matrix" && !nt.isSelfClosing) {
                    std::string_view mTxt;
                    if (!FindElementBody(nBody, np, "matrix", mTxt, np)) break;
                    std::vector<float> vals;
                    ParseFloats(mTxt, vals, 16);
                    if (vals.size() == 16) {
                        Mat4Row m;
                        for (size_t k = 0; k < 16; ++k) m.m[k] = vals[k];
                        curWorld = Mat4Row::Mul(curWorld, m);
                    }
                } else if (nt.localName == "translate" && !nt.isSelfClosing) {
                    std::string_view tTxt;
                    if (!FindElementBody(nBody, np, "translate", tTxt, np)) break;
                    std::vector<float> vals;
                    ParseFloats(tTxt, vals, 3);
                    if (vals.size() >= 3) {
                        curWorld = Mat4Row::Mul(curWorld, Mat4Row::Translate(vals[0], vals[1], vals[2]));
                    }
                } else if (nt.localName == "rotate" && !nt.isSelfClosing) {
                    std::string_view rTxt;
                    if (!FindElementBody(nBody, np, "rotate", rTxt, np)) break;
                    std::vector<float> vals;
                    ParseFloats(rTxt, vals, 4);
                    if (vals.size() >= 4) {
                        curWorld = Mat4Row::Mul(curWorld, Mat4Row::RotateAxisDeg(vals[0], vals[1], vals[2], vals[3]));
                    }
                } else if (nt.localName == "scale" && !nt.isSelfClosing) {
                    std::string_view sTxt;
                    if (!FindElementBody(nBody, np, "scale", sTxt, np)) break;
                    std::vector<float> vals;
                    ParseFloats(sTxt, vals, 3);
                    if (vals.size() >= 3) {
                        curWorld = Mat4Row::Mul(curWorld, Mat4Row::Scale(vals[0], vals[1], vals[2]));
                    }
                } else if (nt.localName == "instance_geometry") {
                    std::string targetId(StripHash(GetAttr(nt.attrs, "url")));
                    auto itG = geomById.find(targetId);
                    if (itG != geomById.end()) {
                        emitGeometry(geometries[itG->second], curWorld);
                    }
                    if (!nt.isSelfClosing) {
                        std::string_view skip;
                        if (!FindElementBody(nBody, np, "instance_geometry", skip, np)) break;
                    }
                } else if (nt.localName == "instance_controller") {
                    std::string ctrlId(StripHash(GetAttr(nt.attrs, "url")));
                    Mat4Row bindMat;
                    if (const DaeGeometry* dg = resolveControllerToGeom(ctrlId, bindMat)) {
                        auto itC = controllers.find(ctrlId);
                        if (itC == controllers.end() || !emitSkinnedController(itC->second, *dg, curWorld)) {
                            emitGeometry(*dg, Mat4Row::Mul(curWorld, bindMat));
                        }
                    }
                    if (!nt.isSelfClosing) {
                        std::string_view skip;
                        if (!FindElementBody(nBody, np, "instance_controller", skip, np)) break;
                    }
                } else if (nt.localName == "instance_node") {
                    std::string refNodeId(StripHash(GetAttr(nt.attrs, "url")));
                    if (!refNodeId.empty()) {
                        auto itN = namedNodes.find(refNodeId);
                        if (itN != namedNodes.end() &&
                            std::find(activeNodeIds.begin(), activeNodeIds.end(), refNodeId) == activeNodeIds.end()) {
                            activeNodeIds.push_back(refNodeId);
                            traverseNodeBody(itN->second, curWorld, depth + 1);
                            activeNodeIds.pop_back();
                        }
                    }
                    if (!nt.isSelfClosing) {
                        std::string_view skip;
                        if (!FindElementBody(nBody, np, "instance_node", skip, np)) break;
                    }
                } else if (nt.localName == "node") {
                    std::string childId(GetAttr(nt.attrs, "id"));
                    if (nt.isSelfClosing) continue;
                    std::string_view childBody;
                    if (!FindElementBody(nBody, np, "node", childBody, np)) break;
                    if (!childId.empty()) activeNodeIds.push_back(childId);
                    traverseNodeBody(childBody, curWorld, depth + 1);
                    if (!childId.empty()) activeNodeIds.pop_back();
                } else if (!nt.isSelfClosing) {
                    std::string_view skip;
                    if (!FindElementBody(nBody, np, nt.localName, skip, np)) break;
                }
            }
        };

    auto traverseVisualScene = [&](std::string_view vsBody) {
        // Pre-pass: index all joint/node world transforms before evaluating <instance_controller>
        {
            size_t jp = 0;
            XmlTag jt;
            while (NextTag(vsBody, jp, jt)) {
                if (jt.isClosing) continue;
                if (jt.localName == "node" && !jt.isSelfClosing) {
                    std::string attrsCopy(jt.attrs);
                    std::string_view nBody;
                    if (!FindElementBody(vsBody, jp, "node", nBody, jp)) break;
                    collectNodeMatrices(nBody, Mat4Row::Identity(), attrsCopy, 0);
                } else if (!jt.isSelfClosing) {
                    std::string_view skip;
                    if (!FindElementBody(vsBody, jp, jt.localName, skip, jp)) break;
                }
            }
        }
        size_t vp = 0;
        XmlTag vt;
        while (NextTag(vsBody, vp, vt)) {
            if (vt.isClosing) continue;
            if (vt.localName == "node" && !vt.isSelfClosing) {
                std::string rootId(GetAttr(vt.attrs, "id"));
                std::string_view nBody;
                if (!FindElementBody(vsBody, vp, "node", nBody, vp)) break;
                if (!rootId.empty()) activeNodeIds.push_back(rootId);
                traverseNodeBody(nBody, Mat4Row::Identity(), 0);
                if (!rootId.empty()) activeNodeIds.pop_back();
            } else if (!vt.isSelfClosing) {
                std::string_view skip;
                if (!FindElementBody(vsBody, vp, vt.localName, skip, vp)) break;
            }
        }
    };

    if (!activeVisualSceneId.empty()) {
        auto itVs = visualScenes.find(activeVisualSceneId);
        if (itVs != visualScenes.end()) {
            traverseVisualScene(itVs->second);
        }
    }
    if (mesh.tris.empty() && !visualSceneOrder.empty()) {
        for (std::string_view vsBody : visualSceneOrder) {
            traverseVisualScene(vsBody);
        }
    }

    // Fallback: if no <visual_scene> instantiated any geometry, emit all geometries from <library_geometries>
    if (mesh.tris.empty()) {
        mesh.vertices.clear();
        for (const DaeGeometry& dg : geometries) {
            emitGeometry(dg, Mat4Row::Identity());
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) {
        mesh.vertices.clear();
        mesh.tris.clear();
        return false;
    }
    if (animClipCount > 0) {
        mesh.animFrameCount = std::max<size_t>(mesh.animFrameCount, animClipCount + 1);
    }
    return true;
}

} // namespace Formats
