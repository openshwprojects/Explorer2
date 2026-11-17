#pragma once
#include "MeshBuild3D.h"
// Ply.h — Stanford Polygon File Format (.ply) 3D Mesh & Point Cloud Parser
// Supports format ascii, format binary_little_endian, and format binary_big_endian,
// exact element/property schemas (all scalar widths + list properties), tristrips, edges,
// vertex normals/RGB, and point-cloud-only PLY files.

namespace Formats {

namespace DetailPly {

enum class PlyEncoding {
    Unknown,
    Ascii,
    BinaryLE,
    BinaryBE
};

enum class PlyScalarType : uint8_t {
    Invalid = 0,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    F32,
    F64
};

inline PlyScalarType ParsePlyScalarType(std::string_view s) {
    if (s == "char"   || s == "int8")    return PlyScalarType::I8;
    if (s == "uchar"  || s == "uint8")   return PlyScalarType::U8;
    if (s == "short"  || s == "int16")   return PlyScalarType::I16;
    if (s == "ushort" || s == "uint16")  return PlyScalarType::U16;
    if (s == "int"    || s == "int32")   return PlyScalarType::I32;
    if (s == "uint"   || s == "uint32")  return PlyScalarType::U32;
    if (s == "float"  || s == "float32") return PlyScalarType::F32;
    if (s == "double" || s == "float64") return PlyScalarType::F64;
    return PlyScalarType::Invalid;
}

inline size_t PlyScalarByteSize(PlyScalarType t) {
    switch (t) {
    case PlyScalarType::I8:
    case PlyScalarType::U8:  return 1;
    case PlyScalarType::I16:
    case PlyScalarType::U16: return 2;
    case PlyScalarType::I32:
    case PlyScalarType::U32:
    case PlyScalarType::F32: return 4;
    case PlyScalarType::F64: return 8;
    default:                 return 0;
    }
}

inline uint16_t Swap16Ply(uint16_t v) {
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

inline uint32_t Swap32Ply(uint32_t v) {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}

inline uint64_t Swap64Ply(uint64_t v) {
    return ((v & 0x00000000000000FFULL) << 56) |
           ((v & 0x000000000000FF00ULL) << 40) |
           ((v & 0x0000000000FF0000ULL) << 24) |
           ((v & 0x00000000FF000000ULL) << 8)  |
           ((v & 0x000000FF00000000ULL) >> 8)  |
           ((v & 0x0000FF0000000000ULL) >> 24) |
           ((v & 0x00FF000000000000ULL) >> 40) |
           ((v & 0xFF00000000000000ULL) >> 56);
}

inline double ReadPlyBinaryDouble(const uint8_t* p, PlyScalarType t, bool bigEndian) {
    switch (t) {
    case PlyScalarType::I8: {
        int8_t v = 0; std::memcpy(&v, p, 1); return static_cast<double>(v);
    }
    case PlyScalarType::U8:
        return static_cast<double>(p[0]);
    case PlyScalarType::I16: {
        uint16_t u = 0; std::memcpy(&u, p, 2);
        if (bigEndian) u = Swap16Ply(u);
        return static_cast<double>(static_cast<int16_t>(u));
    }
    case PlyScalarType::U16: {
        uint16_t u = 0; std::memcpy(&u, p, 2);
        if (bigEndian) u = Swap16Ply(u);
        return static_cast<double>(u);
    }
    case PlyScalarType::I32: {
        uint32_t u = 0; std::memcpy(&u, p, 4);
        if (bigEndian) u = Swap32Ply(u);
        return static_cast<double>(static_cast<int32_t>(u));
    }
    case PlyScalarType::U32: {
        uint32_t u = 0; std::memcpy(&u, p, 4);
        if (bigEndian) u = Swap32Ply(u);
        return static_cast<double>(u);
    }
    case PlyScalarType::F32: {
        uint32_t u = 0; std::memcpy(&u, p, 4);
        if (bigEndian) u = Swap32Ply(u);
        float f = 0.0f; std::memcpy(&f, &u, 4);
        return static_cast<double>(f);
    }
    case PlyScalarType::F64: {
        uint64_t u = 0; std::memcpy(&u, p, 8);
        if (bigEndian) u = Swap64Ply(u);
        double d = 0.0; std::memcpy(&d, &u, 8);
        return d;
    }
    default:
        return 0.0;
    }
}

inline int64_t ReadPlyBinaryInt(const uint8_t* p, PlyScalarType t, bool bigEndian) {
    switch (t) {
    case PlyScalarType::I8: {
        int8_t v = 0; std::memcpy(&v, p, 1); return static_cast<int64_t>(v);
    }
    case PlyScalarType::U8:
        return static_cast<int64_t>(p[0]);
    case PlyScalarType::I16: {
        uint16_t u = 0; std::memcpy(&u, p, 2);
        if (bigEndian) u = Swap16Ply(u);
        return static_cast<int64_t>(static_cast<int16_t>(u));
    }
    case PlyScalarType::U16: {
        uint16_t u = 0; std::memcpy(&u, p, 2);
        if (bigEndian) u = Swap16Ply(u);
        return static_cast<int64_t>(u);
    }
    case PlyScalarType::I32: {
        uint32_t u = 0; std::memcpy(&u, p, 4);
        if (bigEndian) u = Swap32Ply(u);
        return static_cast<int64_t>(static_cast<int32_t>(u));
    }
    case PlyScalarType::U32: {
        uint32_t u = 0; std::memcpy(&u, p, 4);
        if (bigEndian) u = Swap32Ply(u);
        return static_cast<int64_t>(u);
    }
    case PlyScalarType::F32:
    case PlyScalarType::F64:
        return static_cast<int64_t>(std::llround(ReadPlyBinaryDouble(p, t, bigEndian)));
    default:
        return 0;
    }
}

inline bool NextAsciiDoublePly(const char*& cur, const char* end, double& out) {
    while (cur < end) {
        while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
        if (cur >= end) return false;
        if (*cur == '#') {
            while (cur < end && *cur != '\n') ++cur;
            continue;
        }
        break;
    }
    if (cur >= end) return false;
    const char* tok = cur;
    while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n') ++cur;
    char stackBuf[64];
    size_t len = static_cast<size_t>(cur - tok);
    if (len == 0 || len >= sizeof(stackBuf)) return false;
    std::memcpy(stackBuf, tok, len);
    stackBuf[len] = '\0';
    char* next = nullptr;
    out = std::strtod(stackBuf, &next);
    return next != stackBuf;
}

inline bool NextAsciiIntPly(const char*& cur, const char* end, int64_t& out) {
    while (cur < end) {
        while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
        if (cur >= end) return false;
        if (*cur == '#') {
            while (cur < end && *cur != '\n') ++cur;
            continue;
        }
        break;
    }
    if (cur >= end) return false;
    const char* tok = cur;
    while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n') ++cur;
    auto [ptr, ec] = std::from_chars(tok, cur, out);
    if (ec == std::errc{} && ptr == cur) return true;
    char stackBuf[64];
    size_t len = static_cast<size_t>(cur - tok);
    if (len == 0 || len >= sizeof(stackBuf)) return false;
    std::memcpy(stackBuf, tok, len);
    stackBuf[len] = '\0';
    char* next = nullptr;
    out = std::strtoll(stackBuf, &next, 10);
    return next != stackBuf;
}

struct PlyProperty {
    std::string name;
    bool isList = false;
    PlyScalarType countType = PlyScalarType::Invalid;
    PlyScalarType itemType = PlyScalarType::Invalid;
    size_t byteOffset = 0;
};

struct PlyElement {
    std::string name;
    size_t count = 0;
    std::vector<PlyProperty> props;
    bool fixedSize = true;
    size_t fixedStride = 0;
};

inline void SplitPlyHeaderTokens(std::string_view line, std::vector<std::string_view>& out) {
    out.clear();
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= line.size()) break;
        size_t s = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
        out.push_back(line.substr(s, i - s));
    }
}

} // namespace DetailPly

inline bool ParsePlyMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh, bool allowPointCloudSplats = true) {
    if (!data || size < 16) return false;

    size_t startOff = 0;
    if (size >= 19 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        startOff = 3;
    }
    if (startOff + 4 > size || std::memcmp(data + startOff, "ply", 3) != 0 ||
        (data[startOff + 3] != '\n' && data[startOff + 3] != '\r' && data[startOff + 3] != ' ' && data[startOff + 3] != '\t')) {
        return false;
    }

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    DetailPly::PlyEncoding encoding = DetailPly::PlyEncoding::Unknown;
    std::vector<DetailPly::PlyElement> elements;
    bool foundEndHeader = false;
    size_t payloadOffset = 0;

    std::vector<std::string_view> toks;
    size_t pos = startOff;
    while (pos < size) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) return false; // end_header must end with newline
        std::string_view line = sv.substr(pos, eol - pos);
        pos = eol + 1;

        DetailPly::SplitPlyHeaderTokens(line, toks);
        if (toks.empty()) continue;

        std::string key(toks[0]);
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (key == "ply" || key == "comment" || key == "obj_info") {
            continue;
        } else if (key == "format") {
            if (toks.size() < 2) return false;
            std::string fmt(toks[1]);
            for (char& c : fmt) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (fmt == "ascii") encoding = DetailPly::PlyEncoding::Ascii;
            else if (fmt == "binary_little_endian") encoding = DetailPly::PlyEncoding::BinaryLE;
            else if (fmt == "binary_big_endian") encoding = DetailPly::PlyEncoding::BinaryBE;
            else return false;
        } else if (key == "element") {
            if (toks.size() < 3) return false;
            DetailPly::PlyElement el;
            el.name.assign(toks[1]);
            for (char& c : el.name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            uint64_t cnt = 0;
            auto [ptr, ec] = std::from_chars(toks[2].data(), toks[2].data() + toks[2].size(), cnt);
            if (ec != std::errc{} || ptr != toks[2].data() + toks[2].size() || cnt > 100000000ULL) return false;
            el.count = static_cast<size_t>(cnt);
            elements.push_back(std::move(el));
        } else if (key == "property") {
            if (elements.empty() || toks.size() < 3) return false;
            DetailPly::PlyElement& curEl = elements.back();
            DetailPly::PlyProperty prop;
            std::string pkind(toks[1]);
            for (char& c : pkind) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (pkind == "list" || pkind == "list_size") {
                if (toks.size() < 5) return false;
                std::string ct(toks[2]), it(toks[3]);
                for (char& c : ct) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (char& c : it) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                prop.isList = true;
                prop.countType = DetailPly::ParsePlyScalarType(ct);
                prop.itemType = DetailPly::ParsePlyScalarType(it);
                if (prop.countType == DetailPly::PlyScalarType::Invalid ||
                    prop.itemType == DetailPly::PlyScalarType::Invalid) {
                    return false;
                }
                prop.name.assign(toks[4]);
                curEl.fixedSize = false;
            } else {
                prop.isList = false;
                prop.itemType = DetailPly::ParsePlyScalarType(pkind);
                if (prop.itemType == DetailPly::PlyScalarType::Invalid) return false;
                prop.name.assign(toks[2]);
                prop.byteOffset = curEl.fixedStride;
                curEl.fixedStride += DetailPly::PlyScalarByteSize(prop.itemType);
            }
            for (char& c : prop.name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            curEl.props.push_back(std::move(prop));
        } else if (key == "end_header") {
            foundEndHeader = true;
            payloadOffset = pos;
            break;
        }
    }

    if (!foundEndHeader || encoding == DetailPly::PlyEncoding::Unknown || elements.empty()) {
        return false;
    }

    if (encoding == DetailPly::PlyEncoding::BinaryLE) mesh.formatName = L"Stanford PLY (Binary LE)";
    else if (encoding == DetailPly::PlyEncoding::BinaryBE) mesh.formatName = L"Stanford PLY (Binary BE)";
    else mesh.formatName = L"Stanford PLY (ASCII)";

    const bool isAscii = (encoding == DetailPly::PlyEncoding::Ascii);
    const bool isBE = (encoding == DetailPly::PlyEncoding::BinaryBE);
    const uint8_t* bp = data + payloadOffset;
    const uint8_t* const bend = data + size;
    const char* ap = reinterpret_cast<const char*>(bp);
    const char* const aend = reinterpret_cast<const char*>(bend);

    std::vector<std::pair<uint32_t, uint32_t>> parsedEdges;
    bool hasColors = false;
    bool hasNormals = false;

    for (const auto& el : elements) {
        if (el.count == 0) continue;

        if (el.name == "vertex") {
            int ix = -1, iy = -1, iz = -1;
            int inx = -1, iny = -1, inz = -1;
            int ir = -1, ig = -1, ib = -1;
            for (size_t pi = 0; pi < el.props.size(); ++pi) {
                if (el.props[pi].isList) continue;
                const std::string& nm = el.props[pi].name;
                if (nm == "x" && ix < 0) ix = static_cast<int>(pi);
                else if (nm == "y" && iy < 0) iy = static_cast<int>(pi);
                else if (nm == "z" && iz < 0) iz = static_cast<int>(pi);
                else if (nm == "nx" && inx < 0) inx = static_cast<int>(pi);
                else if (nm == "ny" && iny < 0) iny = static_cast<int>(pi);
                else if (nm == "nz" && inz < 0) inz = static_cast<int>(pi);
                else if ((nm == "red" || nm == "r" || nm == "diffuse_red") && ir < 0) ir = static_cast<int>(pi);
                else if ((nm == "green" || nm == "g" || nm == "diffuse_green") && ig < 0) ig = static_cast<int>(pi);
                else if ((nm == "blue" || nm == "b" || nm == "diffuse_blue") && ib < 0) ib = static_cast<int>(pi);
            }
            if (ix < 0 || iy < 0 || iz < 0) return false;
            if (ir >= 0 && ig >= 0 && ib >= 0) hasColors = true;
            if (inx >= 0 && iny >= 0 && inz >= 0) hasNormals = true;

            if (!isAscii && el.fixedSize) {
                if (el.fixedStride == 0 || static_cast<uint64_t>(bend - bp) < static_cast<uint64_t>(el.count) * el.fixedStride) {
                    return false;
                }
                mesh.vertices.reserve(mesh.vertices.size() + std::min<size_t>(el.count, 1000000));
                const auto& px = el.props[ix];
                const auto& py = el.props[iy];
                const auto& pz = el.props[iz];
                for (size_t i = 0; i < el.count; ++i) {
                    const uint8_t* rec = bp + i * el.fixedStride;
                    double vx = DetailPly::ReadPlyBinaryDouble(rec + px.byteOffset, px.itemType, isBE);
                    double vy = DetailPly::ReadPlyBinaryDouble(rec + py.byteOffset, py.itemType, isBE);
                    double vz = DetailPly::ReadPlyBinaryDouble(rec + pz.byteOffset, pz.itemType, isBE);
                    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) {
                        return false;
                    }
                    if (hasColors) {
                        (void)DetailPly::ReadPlyBinaryDouble(rec + el.props[ir].byteOffset, el.props[ir].itemType, isBE);
                        (void)DetailPly::ReadPlyBinaryDouble(rec + el.props[ig].byteOffset, el.props[ig].itemType, isBE);
                        (void)DetailPly::ReadPlyBinaryDouble(rec + el.props[ib].byteOffset, el.props[ib].itemType, isBE);
                    }
                    mesh.vertices.push_back({ static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz) });
                }
                bp += el.count * el.fixedStride;
            } else if (!isAscii) {
                mesh.vertices.reserve(mesh.vertices.size() + std::min<size_t>(el.count, 500000));
                for (size_t i = 0; i < el.count; ++i) {
                    double vx = 0.0, vy = 0.0, vz = 0.0;
                    for (size_t pi = 0; pi < el.props.size(); ++pi) {
                        const auto& pr = el.props[pi];
                        if (!pr.isList) {
                            size_t bsz = DetailPly::PlyScalarByteSize(pr.itemType);
                            if (static_cast<size_t>(bend - bp) < bsz) return false;
                            double val = DetailPly::ReadPlyBinaryDouble(bp, pr.itemType, isBE);
                            bp += bsz;
                            if (static_cast<int>(pi) == ix) vx = val;
                            else if (static_cast<int>(pi) == iy) vy = val;
                            else if (static_cast<int>(pi) == iz) vz = val;
                        } else {
                            size_t csz = DetailPly::PlyScalarByteSize(pr.countType);
                            size_t isz = DetailPly::PlyScalarByteSize(pr.itemType);
                            if (static_cast<size_t>(bend - bp) < csz) return false;
                            int64_t n = DetailPly::ReadPlyBinaryInt(bp, pr.countType, isBE);
                            bp += csz;
                            if (n < 0 || static_cast<uint64_t>(bend - bp) < static_cast<uint64_t>(n) * isz) return false;
                            bp += static_cast<size_t>(n) * isz;
                        }
                    }
                    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                    mesh.vertices.push_back({ static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz) });
                }
            } else {
                mesh.vertices.reserve(mesh.vertices.size() + std::min<size_t>(el.count, 500000));
                for (size_t i = 0; i < el.count; ++i) {
                    double vx = 0.0, vy = 0.0, vz = 0.0;
                    for (size_t pi = 0; pi < el.props.size(); ++pi) {
                        const auto& pr = el.props[pi];
                        if (!pr.isList) {
                            double val = 0.0;
                            if (!DetailPly::NextAsciiDoublePly(ap, aend, val)) return false;
                            if (static_cast<int>(pi) == ix) vx = val;
                            else if (static_cast<int>(pi) == iy) vy = val;
                            else if (static_cast<int>(pi) == iz) vz = val;
                        } else {
                            int64_t n = 0;
                            if (!DetailPly::NextAsciiIntPly(ap, aend, n) || n < 0 || n > 1000000) return false;
                            double dummy = 0.0;
                            for (int64_t k = 0; k < n; ++k) {
                                if (!DetailPly::NextAsciiDoublePly(ap, aend, dummy)) return false;
                            }
                        }
                    }
                    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                    mesh.vertices.push_back({ static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz) });
                }
            }
        } else if (el.name == "face" || el.name == "tristrips") {
            const bool isStrip = (el.name == "tristrips");
            int idxProp = -1;
            for (size_t pi = 0; pi < el.props.size(); ++pi) {
                if (!el.props[pi].isList) continue;
                const std::string& nm = el.props[pi].name;
                if (nm == "vertex_indices" || nm == "vertex_index" || nm == "indices") {
                    idxProp = static_cast<int>(pi);
                    break;
                }
                if (idxProp < 0) idxProp = static_cast<int>(pi);
            }

            std::vector<int64_t> idxList;
            idxList.reserve(32);

            for (size_t fi = 0; fi < el.count; ++fi) {
                idxList.clear();
                for (size_t pi = 0; pi < el.props.size(); ++pi) {
                    const auto& pr = el.props[pi];
                    if (!isAscii) {
                        if (!pr.isList) {
                            size_t bsz = DetailPly::PlyScalarByteSize(pr.itemType);
                            if (static_cast<size_t>(bend - bp) < bsz) return false;
                            bp += bsz;
                        } else {
                            size_t csz = DetailPly::PlyScalarByteSize(pr.countType);
                            size_t isz = DetailPly::PlyScalarByteSize(pr.itemType);
                            if (static_cast<size_t>(bend - bp) < csz) return false;
                            int64_t n = DetailPly::ReadPlyBinaryInt(bp, pr.countType, isBE);
                            bp += csz;
                            if (n < 0 || static_cast<uint64_t>(bend - bp) < static_cast<uint64_t>(n) * isz) return false;
                            if (static_cast<int>(pi) == idxProp) {
                                idxList.resize(static_cast<size_t>(n));
                                for (int64_t k = 0; k < n; ++k) {
                                    idxList[static_cast<size_t>(k)] = DetailPly::ReadPlyBinaryInt(bp + static_cast<size_t>(k) * isz, pr.itemType, isBE);
                                }
                            }
                            bp += static_cast<size_t>(n) * isz;
                        }
                    } else {
                        if (!pr.isList) {
                            double dummy = 0.0;
                            if (!DetailPly::NextAsciiDoublePly(ap, aend, dummy)) return false;
                        } else {
                            int64_t n = 0;
                            if (!DetailPly::NextAsciiIntPly(ap, aend, n) || n < 0 || n > 10000000) return false;
                            if (static_cast<int>(pi) == idxProp) {
                                idxList.resize(static_cast<size_t>(n));
                                for (int64_t k = 0; k < n; ++k) {
                                    if (!DetailPly::NextAsciiIntPly(ap, aend, idxList[static_cast<size_t>(k)])) return false;
                                }
                            } else {
                                double dummy = 0.0;
                                for (int64_t k = 0; k < n; ++k) {
                                    if (!DetailPly::NextAsciiDoublePly(ap, aend, dummy)) return false;
                                }
                            }
                        }
                    }
                }

                const size_t nv = mesh.vertices.size();
                if (!isStrip) {
                    if (idxList.size() >= 3 && idxList[0] >= 0 && static_cast<size_t>(idxList[0]) < nv) {
                        uint32_t i0 = static_cast<uint32_t>(idxList[0]);
                        for (size_t k = 1; k + 1 < idxList.size(); ++k) {
                            int64_t a = idxList[k], b = idxList[k + 1];
                            if (a >= 0 && b >= 0 && static_cast<size_t>(a) < nv && static_cast<size_t>(b) < nv) {
                                mesh.tris.push_back({ i0, static_cast<uint32_t>(a), static_cast<uint32_t>(b), {} });
                            }
                        }
                    }
                } else {
                    size_t stripLen = 0;
                    uint32_t s0 = 0, s1 = 0;
                    for (int64_t rawIdx : idxList) {
                        if (rawIdx < 0) {
                            stripLen = 0;
                            continue;
                        }
                        if (static_cast<size_t>(rawIdx) >= nv) {
                            stripLen = 0;
                            continue;
                        }
                        uint32_t s2 = static_cast<uint32_t>(rawIdx);
                        if (stripLen >= 2) {
                            if (s0 != s1 && s1 != s2 && s2 != s0) {
                                if (((stripLen - 2) & 1u) == 0u) {
                                    mesh.tris.push_back({ s0, s1, s2, {} });
                                } else {
                                    mesh.tris.push_back({ s1, s0, s2, {} });
                                }
                            }
                        }
                        s0 = s1;
                        s1 = s2;
                        ++stripLen;
                    }
                }
            }
        } else if (el.name == "edge") {
            int iv1 = -1, iv2 = -1;
            for (size_t pi = 0; pi < el.props.size(); ++pi) {
                if (el.props[pi].isList) continue;
                if ((el.props[pi].name == "vertex1" || el.props[pi].name == "v1") && iv1 < 0) iv1 = static_cast<int>(pi);
                else if ((el.props[pi].name == "vertex2" || el.props[pi].name == "v2") && iv2 < 0) iv2 = static_cast<int>(pi);
            }
            for (size_t ei = 0; ei < el.count; ++ei) {
                int64_t e1 = -1, e2 = -1;
                for (size_t pi = 0; pi < el.props.size(); ++pi) {
                    const auto& pr = el.props[pi];
                    if (!isAscii) {
                        if (!pr.isList) {
                            size_t bsz = DetailPly::PlyScalarByteSize(pr.itemType);
                            if (static_cast<size_t>(bend - bp) < bsz) return false;
                            int64_t val = DetailPly::ReadPlyBinaryInt(bp, pr.itemType, isBE);
                            bp += bsz;
                            if (static_cast<int>(pi) == iv1) e1 = val;
                            else if (static_cast<int>(pi) == iv2) e2 = val;
                        } else {
                            size_t csz = DetailPly::PlyScalarByteSize(pr.countType);
                            size_t isz = DetailPly::PlyScalarByteSize(pr.itemType);
                            if (static_cast<size_t>(bend - bp) < csz) return false;
                            int64_t n = DetailPly::ReadPlyBinaryInt(bp, pr.countType, isBE);
                            bp += csz;
                            if (n < 0 || static_cast<uint64_t>(bend - bp) < static_cast<uint64_t>(n) * isz) return false;
                            bp += static_cast<size_t>(n) * isz;
                        }
                    } else {
                        if (!pr.isList) {
                            int64_t val = 0;
                            if (!DetailPly::NextAsciiIntPly(ap, aend, val)) return false;
                            if (static_cast<int>(pi) == iv1) e1 = val;
                            else if (static_cast<int>(pi) == iv2) e2 = val;
                        } else {
                            int64_t n = 0;
                            if (!DetailPly::NextAsciiIntPly(ap, aend, n) || n < 0 || n > 1000000) return false;
                            double dummy = 0.0;
                            for (int64_t k = 0; k < n; ++k) {
                                if (!DetailPly::NextAsciiDoublePly(ap, aend, dummy)) return false;
                            }
                        }
                    }
                }
                if (e1 >= 0 && e2 >= 0 &&
                    static_cast<size_t>(e1) < mesh.vertices.size() &&
                    static_cast<size_t>(e2) < mesh.vertices.size()) {
                    parsedEdges.emplace_back(static_cast<uint32_t>(e1), static_cast<uint32_t>(e2));
                }
            }
        } else {
            // Advance accurately past any custom element so subsequent elements stay byte-aligned
            if (!isAscii && el.fixedSize) {
                if (static_cast<uint64_t>(bend - bp) < static_cast<uint64_t>(el.count) * el.fixedStride) return false;
                bp += el.count * el.fixedStride;
            } else if (!isAscii) {
                for (size_t ci = 0; ci < el.count; ++ci) {
                    for (const auto& pr : el.props) {
                        if (!pr.isList) {
                            size_t bsz = DetailPly::PlyScalarByteSize(pr.itemType);
                            if (static_cast<size_t>(bend - bp) < bsz) return false;
                            bp += bsz;
                        } else {
                            size_t csz = DetailPly::PlyScalarByteSize(pr.countType);
                            size_t isz = DetailPly::PlyScalarByteSize(pr.itemType);
                            if (static_cast<size_t>(bend - bp) < csz) return false;
                            int64_t n = DetailPly::ReadPlyBinaryInt(bp, pr.countType, isBE);
                            bp += csz;
                            if (n < 0 || static_cast<uint64_t>(bend - bp) < static_cast<uint64_t>(n) * isz) return false;
                            bp += static_cast<size_t>(n) * isz;
                        }
                    }
                }
            } else {
                for (size_t ci = 0; ci < el.count; ++ci) {
                    for (const auto& pr : el.props) {
                        if (!pr.isList) {
                            double dummy = 0.0;
                            if (!DetailPly::NextAsciiDoublePly(ap, aend, dummy)) return false;
                        } else {
                            int64_t n = 0;
                            if (!DetailPly::NextAsciiIntPly(ap, aend, n) || n < 0 || n > 10000000) return false;
                            double dummy = 0.0;
                            for (int64_t k = 0; k < n; ++k) {
                                if (!DetailPly::NextAsciiDoublePly(ap, aend, dummy)) return false;
                            }
                        }
                    }
                }
            }
        }
    }

    (void)hasColors;
    (void)hasNormals;

    if (mesh.vertices.empty()) return false;

    if (mesh.tris.empty() && !parsedEdges.empty()) {
        Vec3f bmin = mesh.vertices[0], bmax = mesh.vertices[0];
        for (const auto& v : mesh.vertices) {
            bmin.x = std::min(bmin.x, v.x); bmin.y = std::min(bmin.y, v.y); bmin.z = std::min(bmin.z, v.z);
            bmax.x = std::max(bmax.x, v.x); bmax.y = std::max(bmax.y, v.y); bmax.z = std::max(bmax.z, v.z);
        }
        float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1e-4f });
        float rad = span * 0.008f;
        std::vector<Vec3f> baseVerts = std::move(mesh.vertices);
        mesh.vertices.clear();
        for (const auto& e : parsedEdges) {
            MeshBuild3D::EmitEdgeStrut(mesh, baseVerts[e.first], baseVerts[e.second], rad);
        }
    } else if (mesh.tris.empty() && allowPointCloudSplats) {
        // Point-cloud PLY (numFaces == 0 && numVerts > 0): emit micro-tetrahedra splats
        std::vector<Vec3f> cloud = std::move(mesh.vertices);
        mesh.vertices.clear();
        constexpr size_t kMaxPlyPts = 25000;
        const size_t step = std::max<size_t>(1, cloud.size() / kMaxPlyPts);

        Vec3f bmin = cloud[0], bmax = cloud[0];
        for (const auto& p : cloud) {
            bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
            bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
        }
        float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
        float r = std::max(0.015f, (span > 1e-5f) ? (span * 0.018f) : 0.02f);

        size_t estPts = (cloud.size() + step - 1) / step;
        mesh.vertices.reserve(estPts * 4);
        mesh.tris.reserve(estPts * 4);
        for (size_t i = 0; i < cloud.size() && mesh.tris.size() < kMaxPlyPts * 4; i += step) {
            const Vec3f& p = cloud[i];
            uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
            mesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
            mesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
            mesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
            mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
            mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
            mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
            mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
        }
        mesh.formatName += L" Point Cloud";
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
