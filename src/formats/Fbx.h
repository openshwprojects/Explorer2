#pragma once
// Fbx.h — Autodesk FBX (.fbx) meshes: binary files (FBX 6.1 to 7.7, with 32- and 64-bit records) and text
// files (FBX 6 and 7)
//
// Every geometry of class "Mesh" is read (Vertices and PolygonVertexIndex; a polygon is cut into a fan of
// triangles) and put where the file puts it: through the transform of each model that uses it (Lcl
// Translation, Lcl Rotation in the model's rotation order, Lcl Scaling, PreRotation, PostRotation, and the
// rotation and scaling pivots and offsets), the transforms of that model's parents, and the geometric
// transform that applies to the geometry alone. A file whose up axis is Z or X is turned so that its up
// becomes +Y. Materials, normals, UVs, skinning and animation are left out: the model is shown in the pose
// it was saved in.
//
// Included by Model3DViewer.cpp inside namespace Explorer2, after <charconv>, <unordered_map> and ZipDeflate.h.

namespace Formats {
namespace Fbx {

constexpr int kMaxDepth = 48;
constexpr size_t kMaxNodes = 4000000;
constexpr size_t kMaxArrayValues = 48u * 1024 * 1024;     // values in one array
constexpr size_t kMaxVertices = 6000000;
constexpr size_t kMaxTriangles = 6000000;

struct Prop {
    char type = 0;                      // 'I' whole number, 'D' number, 'S' text, 'A' array, 'R' raw bytes (not kept)
    int64_t i = 0;
    double d = 0.0;
    std::string s;
    char arrayType = 0;                 // a binary array ('f' 'd' 'l' 'i' 'b') is read only when asked for
    uint32_t arrayCount = 0;
    uint32_t arrayEncoding = 0;         // 0 plain, 1 zlib
    const uint8_t* arrayData = nullptr;
    uint32_t arrayBytes = 0;
    std::vector<double> numbers;        // a text array, read already

    bool IsNumber() const { return type == 'I' || type == 'D'; }
    double Number() const { return type == 'I' ? static_cast<double>(i) : d; }
};

struct Node {
    std::string name;
    std::vector<Prop> props;
    std::vector<Node> children;

    const Node* Child(const char* childName) const {
        for (const Node& c : children) {
            if (c.name == childName) return &c;
        }
        return nullptr;
    }
};

// ---- binary files -------------------------------------------------------------------------------------------

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size, uint32_t version)
        : m_data(data), m_size(size), m_wide(version >= 7500) {}

    // Reads the records from pos up to end. A null record (all zero) ends a list.
    bool ReadList(size_t pos, size_t end, std::vector<Node>& out, int depth) {
        if (end > m_size) return false;
        while (pos < end) {
            Node node;
            bool isNull = false;
            if (!ReadRecord(pos, end, node, isNull, depth)) return false;
            if (isNull) return true;
            if (++m_nodes > kMaxNodes) return false;
            out.push_back(std::move(node));
        }
        return true;
    }

private:
    uint64_t U32(size_t p) const { uint32_t v = 0; std::memcpy(&v, m_data + p, 4); return v; }
    uint64_t U64(size_t p) const { uint64_t v = 0; std::memcpy(&v, m_data + p, 8); return v; }

    bool ReadRecord(size_t& pos, size_t end, Node& node, bool& isNull, int depth) {
        const size_t headerLen = m_wide ? 25 : 13;
        if (end - pos < headerLen) {        // what is left cannot be a record: the list ends here
            isNull = true;
            pos = end;
            return true;
        }
        const uint64_t endOffset = m_wide ? U64(pos) : U32(pos);
        const uint64_t propCount = m_wide ? U64(pos + 8) : U32(pos + 4);
        const uint64_t propBytes = m_wide ? U64(pos + 16) : U32(pos + 8);
        const size_t nameLen = m_data[pos + headerLen - 1];
        if (endOffset == 0) {
            isNull = true;
            pos += headerLen;
            return true;
        }
        size_t p = pos + headerLen;
        if (endOffset > end || endOffset < p + nameLen) return false;
        node.name.assign(reinterpret_cast<const char*>(m_data + p), nameLen);
        p += nameLen;
        if (propBytes > endOffset - p || propCount > propBytes) return false;   // a property takes a byte at least
        const size_t propsEnd = p + static_cast<size_t>(propBytes);
        node.props.reserve(static_cast<size_t>(std::min<uint64_t>(propCount, 64)));
        for (uint64_t k = 0; k < propCount; ++k) {
            Prop prop;
            if (!ReadProp(p, propsEnd, prop)) return false;
            node.props.push_back(std::move(prop));
        }
        p = propsEnd;
        if (p < endOffset) {
            if (depth >= kMaxDepth) return false;
            if (!ReadList(p, static_cast<size_t>(endOffset), node.children, depth + 1)) return false;
        }
        pos = static_cast<size_t>(endOffset);
        return true;
    }

    bool ReadProp(size_t& p, size_t end, Prop& prop) {
        if (p >= end) return false;
        const char t = static_cast<char>(m_data[p++]);
        const size_t left = end - p;
        switch (t) {
        case 'C':
            if (left < 1) return false;
            prop.type = 'I';
            prop.i = m_data[p] ? 1 : 0;
            p += 1;
            return true;
        case 'Y': {
            if (left < 2) return false;
            int16_t v = 0;
            std::memcpy(&v, m_data + p, 2);
            prop.type = 'I';
            prop.i = v;
            p += 2;
            return true;
        }
        case 'I': {
            if (left < 4) return false;
            int32_t v = 0;
            std::memcpy(&v, m_data + p, 4);
            prop.type = 'I';
            prop.i = v;
            p += 4;
            return true;
        }
        case 'L': {
            if (left < 8) return false;
            int64_t v = 0;
            std::memcpy(&v, m_data + p, 8);
            prop.type = 'I';
            prop.i = v;
            p += 8;
            return true;
        }
        case 'F': {
            if (left < 4) return false;
            float v = 0.0f;
            std::memcpy(&v, m_data + p, 4);
            prop.type = 'D';
            prop.d = v;
            p += 4;
            return true;
        }
        case 'D': {
            if (left < 8) return false;
            double v = 0.0;
            std::memcpy(&v, m_data + p, 8);
            prop.type = 'D';
            prop.d = v;
            p += 8;
            return true;
        }
        case 'f': case 'd': case 'l': case 'i': case 'b': {
            if (left < 12) return false;
            const uint64_t count = U32(p);
            const uint64_t encoding = U32(p + 4);
            const uint64_t bytes = U32(p + 8);
            p += 12;
            if (bytes > end - p || encoding > 1) return false;
            const uint64_t elem = (t == 'd' || t == 'l') ? 8 : (t == 'b' ? 1 : 4);
            if (encoding == 0 && count * elem != bytes) return false;
            prop.type = 'A';
            prop.arrayType = t;
            prop.arrayCount = static_cast<uint32_t>(count);
            prop.arrayEncoding = static_cast<uint32_t>(encoding);
            prop.arrayData = m_data + p;
            prop.arrayBytes = static_cast<uint32_t>(bytes);
            p += static_cast<size_t>(bytes);
            return true;
        }
        case 'S': case 'R': {
            if (left < 4) return false;
            const uint64_t len = U32(p);
            p += 4;
            if (len > end - p) return false;
            if (t == 'S') {
                prop.type = 'S';
                prop.s.assign(reinterpret_cast<const char*>(m_data + p), static_cast<size_t>(std::min<uint64_t>(len, 4096)));
            } else {
                prop.type = 'R';                // embedded files (textures) are not kept
            }
            p += static_cast<size_t>(len);
            return true;
        }
        default:
            return false;
        }
    }

    const uint8_t* m_data;
    size_t m_size;
    bool m_wide;
    size_t m_nodes = 0;
};

// ---- text files ---------------------------------------------------------------------------------------------
//
//     Name: value, value, ... {      a node: its values up to the end of the line (a line that ends with a
//         Child: ...                 comma goes on), then its children between braces
//     }
//     Vertices: *24 {                FBX 7 arrays: the count, then the values in a child named "a"
//         a: 1,2,3,...
//     }
//     ; comment

class TextReader {
public:
    TextReader(const char* text, size_t size) : m_p(text), m_end(text + size) {}

    bool ReadAll(std::vector<Node>& out) {
        if (m_end - m_p >= 3 && static_cast<unsigned char>(m_p[0]) == 0xEF &&
            static_cast<unsigned char>(m_p[1]) == 0xBB && static_cast<unsigned char>(m_p[2]) == 0xBF) {
            m_p += 3;       // UTF-8 byte order mark
        }
        return ReadList(out, 0, false);
    }

private:
    static bool IsNameChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '|' || c == '-' || c == '.';
    }

    void SkipSpaces() {
        while (m_p < m_end && (*m_p == ' ' || *m_p == '\t')) ++m_p;
    }

    void SkipToLineEnd() {
        while (m_p < m_end && *m_p != '\n') ++m_p;
    }

    void SkipBlank() {
        for (;;) {
            while (m_p < m_end && (*m_p == ' ' || *m_p == '\t' || *m_p == '\r' || *m_p == '\n')) ++m_p;
            if (m_p < m_end && *m_p == ';') {
                SkipToLineEnd();
                continue;
            }
            return;
        }
    }

    bool ReadList(std::vector<Node>& out, int depth, bool insideBraces) {
        for (;;) {
            SkipBlank();
            if (m_p >= m_end) return !insideBraces;
            if (*m_p == '}') {
                if (!insideBraces) return false;
                ++m_p;
                return true;
            }
            Node node;
            if (!ReadNode(node, depth)) return false;
            if (++m_nodes > kMaxNodes) return false;
            out.push_back(std::move(node));
        }
    }

    bool ReadNode(Node& node, int depth) {
        const char* nameStart = m_p;
        while (m_p < m_end && IsNameChar(*m_p)) ++m_p;
        if (m_p == nameStart || m_p >= m_end || *m_p != ':') return false;
        node.name.assign(nameStart, static_cast<size_t>(m_p - nameStart));
        ++m_p;
        if (!ReadValues(node)) return false;
        SkipSpaces();
        if (m_p < m_end && *m_p == '{') {
            ++m_p;
            if (depth >= kMaxDepth) return false;
            return ReadList(node.children, depth + 1, true);
        }
        return true;
    }

    // Adds a value. Numbers that come in long runs (arrays) are kept together, as one array.
    static void AddValue(Node& node, Prop&& value) {
        if (value.IsNumber()) {
            if (!node.props.empty() && node.props.back().type == 'A') {
                node.props.back().numbers.push_back(value.Number());
                return;
            }
            if (node.props.size() >= 16) {
                bool allNumbers = true;
                for (const Prop& p : node.props) allNumbers = allNumbers && p.IsNumber();
                if (allNumbers) {
                    Prop arr;
                    arr.type = 'A';
                    arr.numbers.reserve(64);
                    for (const Prop& p : node.props) arr.numbers.push_back(p.Number());
                    arr.numbers.push_back(value.Number());
                    node.props.clear();
                    node.props.push_back(std::move(arr));
                    return;
                }
            }
        }
        node.props.push_back(std::move(value));
    }

    // A number; false (nothing read) when the token is not one
    bool ReadNumber(Prop& out) {
        const char* const tokenStart = m_p;
        if (m_p < m_end && *m_p == '+') ++m_p;
        const char* start = m_p;
        bool isFloat = false;
        while (m_p < m_end) {
            const char c = *m_p;
            if ((c >= '0' && c <= '9') || c == '-' || c == '+') { ++m_p; continue; }
            if (c == '.' || c == 'e' || c == 'E') { isFloat = true; ++m_p; continue; }
            break;
        }
        if (!isFloat) {
            int64_t v = 0;
            const auto r = std::from_chars(start, m_p, v);
            if (r.ec == std::errc() && r.ptr == m_p) {
                out.type = 'I';
                out.i = v;
                return true;
            }
        }
        double d = 0.0;
        const auto r = std::from_chars(start, m_p, d);
        if (r.ec != std::errc() || r.ptr != m_p) {
            m_p = tokenStart;
            return false;
        }
        out.type = 'D';
        out.d = d;
        return true;
    }

    bool ReadValues(Node& node) {
        bool more = false;          // the last value was followed by a comma: the next line goes on
        for (;;) {
            SkipSpaces();
            if (m_p >= m_end) return true;
            const char c = *m_p;
            if (c == '{' || c == '}') return true;
            if (c == ';') {
                SkipToLineEnd();
                continue;
            }
            if (c == '\r' || c == '\n') {
                if (!more) return true;
                ++m_p;
                continue;
            }
            if (c == ',') {                 // an empty value ("Content: ,")
                ++m_p;
                more = true;
                continue;
            }
            Prop value;
            const bool numberStart = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
            if (c == '"') {
                const char* s = ++m_p;
                while (m_p < m_end && *m_p != '"' && *m_p != '\n') ++m_p;
                value.type = 'S';
                value.s.assign(s, static_cast<size_t>(std::min<ptrdiff_t>(m_p - s, 4096)));
                if (m_p < m_end && *m_p == '"') ++m_p;
            } else if (numberStart && ReadNumber(value)) {
                // read
            } else {
                // a count ("*24"), a bare word ("T", "Y"), or something like a number that is not one ("-1.#IND")
                const char* s = m_p;
                while (m_p < m_end && *m_p != ',' && *m_p != ' ' && *m_p != '\t' && *m_p != '\r' && *m_p != '\n' && *m_p != '{' && *m_p != '}') ++m_p;
                if (m_p == s) return false;
                value.type = 'S';
                value.s.assign(s, static_cast<size_t>(std::min<ptrdiff_t>(m_p - s, 256)));
            }
            AddValue(node, std::move(value));
            SkipSpaces();
            more = (m_p < m_end && *m_p == ',');
            if (more) ++m_p;
        }
    }

    const char* m_p;
    const char* m_end;
    size_t m_nodes = 0;
};

// ---- values ---------------------------------------------------------------------------------------------------

// The numbers of an array: a binary array (unpacked when it is packed), a text array, or the node's own numbers
inline bool ReadArray(const Prop& p, std::vector<double>& out) {
    out.clear();
    if (p.type != 'A') return false;
    if (!p.arrayData) {
        if (p.numbers.size() > kMaxArrayValues) return false;
        out = p.numbers;
        return true;
    }
    if (p.arrayCount > kMaxArrayValues) return false;
    const size_t elem = (p.arrayType == 'd' || p.arrayType == 'l') ? 8 : (p.arrayType == 'b' ? 1 : 4);
    const size_t rawBytes = static_cast<size_t>(p.arrayCount) * elem;
    const uint8_t* src = p.arrayData;
    std::vector<uint8_t> unpacked;
    if (p.arrayEncoding == 1) {
        if (!ZipDeflate::InflateZlib(p.arrayData, p.arrayBytes, unpacked, rawBytes) || unpacked.size() != rawBytes) return false;
        src = unpacked.data();
    }
    out.resize(p.arrayCount);
    for (size_t k = 0; k < p.arrayCount; ++k) {
        const uint8_t* e = src + k * elem;
        switch (p.arrayType) {
        case 'd': { double v; std::memcpy(&v, e, 8); out[k] = v; break; }
        case 'f': { float v; std::memcpy(&v, e, 4); out[k] = v; break; }
        case 'l': { int64_t v; std::memcpy(&v, e, 8); out[k] = static_cast<double>(v); break; }
        case 'i': { int32_t v; std::memcpy(&v, e, 4); out[k] = v; break; }
        default:  { int8_t v; std::memcpy(&v, e, 1); out[k] = v; break; }
        }
    }
    return true;
}

inline bool NodeNumbers(const Node& n, std::vector<double>& out) {
    for (const Prop& p : n.props) {
        if (p.type == 'A') return ReadArray(p, out);
    }
    if (const Node* a = n.Child("a")) return NodeNumbers(*a, out);
    out.clear();
    for (const Prop& p : n.props) {
        if (p.IsNumber()) out.push_back(p.Number());
    }
    return !out.empty();
}

// An object's id: a number (FBX 7), or its name (FBX 6, where objects are known by name)
inline int64_t TextId(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char ch : s) {
        h ^= ch;
        h *= 1099511628211ULL;
    }
    return static_cast<int64_t>(h | 0x8000000000000000ULL);
}

inline int64_t IdOf(const Prop& p) {
    if (p.type == 'I') return p.i;
    if (p.type == 'S') return TextId(p.s);
    return 0;
}

// "Mesh", "Null", "LimbNode": the last text value of an object ("Geometry: id, "Geometry::Name", "Mesh"")
inline std::string ObjectClass(const Node& n) {
    for (size_t k = n.props.size(); k-- > 0;) {
        if (n.props[k].type == 'S') return n.props[k].s;
    }
    return std::string();
}

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Mat4 {
    double m[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j] + m[i][3] * o.m[3][j];
            }
        }
        return r;
    }
    Vec3d Apply(const Vec3d& v) const {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]
        };
    }
    Mat4 RotationInverse() const {      // for a pure rotation: its transpose
        Mat4 r;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
        }
        return r;
    }
    bool Inverse(Mat4& out) const {
        double aug[4][8] = {};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                aug[i][j] = m[i][j];
                aug[i][j + 4] = (i == j) ? 1.0 : 0.0;
            }
        }
        for (int col = 0; col < 4; ++col) {
            int piv = col;
            double best = std::fabs(aug[col][col]);
            for (int r = col + 1; r < 4; ++r) {
                double cand = std::fabs(aug[r][col]);
                if (cand > best) { best = cand; piv = r; }
            }
            if (!std::isfinite(best) || best < 1e-12) return false;
            if (piv != col) {
                for (int k = 0; k < 8; ++k) std::swap(aug[col][k], aug[piv][k]);
            }
            const double invDiag = 1.0 / aug[col][col];
            for (int k = 0; k < 8; ++k) aug[col][k] *= invDiag;
            for (int r = 0; r < 4; ++r) {
                if (r == col) continue;
                const double factor = aug[r][col];
                if (factor == 0.0) continue;
                for (int k = 0; k < 8; ++k) aug[r][k] -= factor * aug[col][k];
            }
        }
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) out.m[i][j] = aug[i][j + 4];
        }
        return true;
    }
    // Converts FBX 16-element FbxAMatrix (4 row-vectors / OpenGL column-major) to column-vector Mat4
    static Mat4 FromFbxMatrixArray(const std::vector<double>& vals) {
        Mat4 r;
        if (vals.size() >= 16) {
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    r.m[row][col] = vals[col * 4 + row];
                }
            }
        }
        return r;
    }
};

inline Mat4 Translation(const Vec3d& t) {
    Mat4 r;
    r.m[0][3] = t.x;
    r.m[1][3] = t.y;
    r.m[2][3] = t.z;
    return r;
}

inline Mat4 Scaling(const Vec3d& s) {
    Mat4 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    return r;
}

inline Mat4 AxisRotation(int axis, double degrees) {
    const double a = degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    Mat4 r;
    if (axis == 0) {
        r.m[1][1] = c; r.m[1][2] = -s; r.m[2][1] = s; r.m[2][2] = c;
    } else if (axis == 1) {
        r.m[0][0] = c; r.m[0][2] = s; r.m[2][0] = -s; r.m[2][2] = c;
    } else {
        r.m[0][0] = c; r.m[0][1] = -s; r.m[1][0] = s; r.m[1][1] = c;
    }
    return r;
}

// FBX rotation orders: 0 XYZ, 1 XZY, 2 YZX, 3 YXZ, 4 ZXY, 5 ZYX (6, spheric XYZ, is taken as XYZ). The first
// axis is turned about first, so XYZ is Rz * Ry * Rx.
inline Mat4 EulerRotation(const Vec3d& degrees, int order) {
    static const int kOrders[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 }, { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 } };
    if (order < 0 || order > 5) order = 0;
    const double angles[3] = { degrees.x, degrees.y, degrees.z };
    Mat4 r;
    for (int k = 0; k < 3; ++k) {
        const int axis = kOrders[order][k];
        if (angles[axis] != 0.0) r = AxisRotation(axis, angles[axis]) * r;
    }
    return r;
}

struct ModelInfo {
    Vec3d t, r, s{ 1, 1, 1 }, pre, post, rotPivot, rotOffset, scalePivot, scaleOffset;
    Vec3d geoT, geoR, geoS{ 1, 1, 1 };
    int order = 0;
    int64_t parent = 0;
    bool hasParent = false;
    int state = 0;                      // 0 world not worked out yet, 1 being worked out, 2 done
    Mat4 world;

    Mat4 Local() const {
        return Translation(t) * Translation(rotOffset) * Translation(rotPivot) *
               EulerRotation(pre, 0) * EulerRotation(r, order) * EulerRotation(post, 0).RotationInverse() *
               Translation({ -rotPivot.x, -rotPivot.y, -rotPivot.z }) *
               Translation(scaleOffset) * Translation(scalePivot) * Scaling(s) *
               Translation({ -scalePivot.x, -scalePivot.y, -scalePivot.z });
    }
    Mat4 Geometric() const {
        return Translation(geoT) * EulerRotation(geoR, 0) * Scaling(geoS);
    }
};

// The values of an object's properties (Properties70 "P" in FBX 7, Properties60 "Property" in FBX 6):
// the first is the name, the numbers that follow are the value
template <typename Fn>
inline void ForEachProperty(const Node& obj, Fn&& fn) {
    for (const char* block : { "Properties70", "Properties60" }) {
        const Node* props = obj.Child(block);
        if (!props) continue;
        for (const Node& p : props->children) {
            if ((p.name != "P" && p.name != "Property") || p.props.empty() || p.props[0].type != 'S') continue;
            double nums[3] = { 0, 0, 0 };
            int count = 0;
            for (size_t k = 1; k < p.props.size() && count < 3; ++k) {
                if (p.props[k].IsNumber()) nums[count++] = p.props[k].Number();
            }
            fn(p.props[0].s, nums, count);
        }
    }
}

inline void ReadModel(const Node& obj, ModelInfo& m) {
    ForEachProperty(obj, [&m](const std::string& name, const double* v, int count) {
        if (count >= 3) {
            const Vec3d vec{ v[0], v[1], v[2] };
            if (name == "Lcl Translation") m.t = vec;
            else if (name == "Lcl Rotation") m.r = vec;
            else if (name == "Lcl Scaling") m.s = vec;
            else if (name == "PreRotation") m.pre = vec;
            else if (name == "PostRotation") m.post = vec;
            else if (name == "RotationPivot") m.rotPivot = vec;
            else if (name == "RotationOffset") m.rotOffset = vec;
            else if (name == "ScalingPivot") m.scalePivot = vec;
            else if (name == "ScalingOffset") m.scaleOffset = vec;
            else if (name == "GeometricTranslation") m.geoT = vec;
            else if (name == "GeometricRotation") m.geoR = vec;
            else if (name == "GeometricScaling") m.geoS = vec;
        } else if (count >= 1 && name == "RotationOrder") {
            m.order = static_cast<int>(v[0]);
        }
    });
}

inline Mat4 WorldOf(std::unordered_map<int64_t, ModelInfo>& models, int64_t id, int depth = 0) {
    auto it = models.find(id);
    if (it == models.end()) return Mat4();
    ModelInfo& m = it->second;
    if (m.state == 2) return m.world;
    if (m.state == 1 || depth > 512) return m.Local();     // a loop of parents, or a chain too long to follow
    m.state = 1;
    const Mat4 parentWorld = m.hasParent ? WorldOf(models, m.parent, depth + 1) : Mat4();
    m.world = parentWorld * m.Local();
    m.state = 2;
    return m.world;
}

inline std::wstring VersionText(uint32_t version) {
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%u.%u", version / 1000, (version % 1000) / 100);
    return buf;
}

} // namespace Fbx

inline bool LooksLikeFbx(const uint8_t* data, size_t size) {
    if (size >= 27 && std::memcmp(data, "Kaydara FBX Binary  \0", 21) == 0) return true;
    const std::string_view head(reinterpret_cast<const char*>(data), std::min<size_t>(size, 4096));
    return head.find("FBXHeaderExtension:") != std::string_view::npos ||
           (head.size() >= 5 && head.substr(0, 5) == "; FBX");
}

inline bool ParseFbxMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Fbx;
    std::vector<Node> roots;
    std::wstring format;
    if (size >= 27 && std::memcmp(data, "Kaydara FBX Binary  \0", 21) == 0) {
        uint32_t version = 0;
        std::memcpy(&version, data + 23, 4);
        BinaryReader reader(data, size, version);
        if (!reader.ReadList(27, size, roots, 0)) return false;
        format = L"Autodesk FBX " + VersionText(version) + L" (binary)";
    } else {
        if (!LooksLikeFbx(data, size)) return false;
        TextReader reader(reinterpret_cast<const char*>(data), size);
        if (!reader.ReadAll(roots)) return false;
        format = L"Autodesk FBX (text)";
    }

    const Node* objects = nullptr;
    const Node* connections = nullptr;
    const Node* settings = nullptr;
    for (const Node& n : roots) {
        if (n.name == "Objects") objects = &n;
        else if (n.name == "Connections") connections = &n;
        else if (n.name == "GlobalSettings") settings = &n;
    }
    if (!objects) return false;

    // The objects: models, meshes, skin deformers, cluster subdeformers, and animation stacks.
    struct MeshSource {
        int64_t id = 0;
        const Node* node = nullptr;
        std::vector<int64_t> models;
        std::vector<int64_t> skins;
    };
    struct SkinInfo {
        int64_t id = 0;
        std::vector<int64_t> clusters;
    };
    struct ClusterInfo {
        int64_t id = 0;
        int64_t boneModelId = 0;
        bool hasBone = false;
        std::vector<int32_t> indexes;
        std::vector<double> weights;
        Mat4 transform;
        Mat4 transformLink;
        bool hasTransformLink = false;
    };
    std::unordered_map<int64_t, ModelInfo> models;
    std::vector<MeshSource> meshes;
    std::unordered_map<int64_t, size_t> geometryById;
    std::unordered_map<int64_t, SkinInfo> skins;
    std::unordered_map<int64_t, ClusterInfo> clusters;
    size_t animStackCount = 0;
    std::vector<double> tmpNums;
    for (const Node& obj : objects->children) {
        if (obj.name == "AnimationStack" || obj.name == "Take") {
            ++animStackCount;
        }
        if (obj.props.empty()) continue;
        if (obj.name == "Model") {
            const int64_t id = IdOf(obj.props[0]);
            ReadModel(obj, models[id]);
            if (obj.Child("Vertices") && obj.Child("PolygonVertexIndex")) {
                MeshSource own;
                own.id = id;
                own.node = &obj;
                own.models.push_back(id);
                geometryById[id] = meshes.size();
                meshes.push_back(std::move(own));
            }
        } else if (obj.name == "Geometry" && ObjectClass(obj) == "Mesh") {
            MeshSource g;
            g.id = IdOf(obj.props[0]);
            g.node = &obj;
            geometryById[g.id] = meshes.size();
            meshes.push_back(std::move(g));
        } else if ((obj.name == "Deformer" || obj.name == "SubDeformer")) {
            const std::string cls = ObjectClass(obj);
            const int64_t id = IdOf(obj.props[0]);
            if (cls == "Skin") {
                skins[id].id = id;
            } else if (cls == "Cluster") {
                ClusterInfo cl;
                cl.id = id;
                if (const Node* idxNode = obj.Child("Indexes")) {
                    if (NodeNumbers(*idxNode, tmpNums)) {
                        cl.indexes.reserve(tmpNums.size());
                        for (double v : tmpNums) cl.indexes.push_back(static_cast<int32_t>(v));
                    }
                }
                if (const Node* wNode = obj.Child("Weights")) {
                    NodeNumbers(*wNode, cl.weights);
                }
                if (const Node* tNode = obj.Child("Transform")) {
                    if (NodeNumbers(*tNode, tmpNums) && tmpNums.size() >= 16) {
                        cl.transform = Mat4::FromFbxMatrixArray(tmpNums);
                    }
                }
                if (const Node* tlNode = obj.Child("TransformLink")) {
                    if (NodeNumbers(*tlNode, tmpNums) && tmpNums.size() >= 16) {
                        cl.transformLink = Mat4::FromFbxMatrixArray(tmpNums);
                        cl.hasTransformLink = true;
                    }
                }
                clusters[id] = std::move(cl);
            }
        }
    }
    if (meshes.empty()) return false;

    // Which model uses which geometry, skin/cluster links, and model parent hierarchy ("OO": object to object)
    if (connections) {
        for (const Node& c : connections->children) {
            if ((c.name != "C" && c.name != "Connect") || c.props.size() < 3) continue;
            if (c.props[0].type != 'S' || c.props[0].s != "OO") continue;
            const int64_t child = IdOf(c.props[1]);
            const int64_t parent = IdOf(c.props[2]);

            // Deformer (Skin) -> Geometry
            if (skins.find(child) != skins.end()) {
                auto gIt = geometryById.find(parent);
                if (gIt != geometryById.end()) {
                    meshes[gIt->second].skins.push_back(child);
                    continue;
                }
            }
            // SubDeformer (Cluster) -> Deformer (Skin)
            if (clusters.find(child) != clusters.end()) {
                auto sIt = skins.find(parent);
                if (sIt != skins.end()) {
                    sIt->second.clusters.push_back(child);
                    continue;
                }
                if (models.find(parent) != models.end()) {
                    clusters[child].boneModelId = parent;
                    clusters[child].hasBone = true;
                    continue;
                }
            }
            // Bone Model -> SubDeformer (Cluster)
            auto clParent = clusters.find(parent);
            if (clParent != clusters.end() && models.find(child) != models.end()) {
                clParent->second.boneModelId = child;
                clParent->second.hasBone = true;
                continue;
            }

            if (models.find(parent) == models.end()) continue;      // 0 / "Model::Scene": the root
            // An FBX 6 model that holds its own mesh is in geometryById only so skins can find it: its "OO" link to
            // another model is a parent link, not a second user of the mesh (that would draw it twice)
            auto g = geometryById.find(child);
            if (g != geometryById.end() && meshes[g->second].node->name != "Model") {
                auto& users = meshes[g->second].models;
                if (std::find(users.begin(), users.end(), parent) == users.end()) users.push_back(parent);
                continue;
            }
            auto m = models.find(child);
            if (m != models.end() && child != parent) {
                m->second.parent = parent;
                m->second.hasParent = true;
            }
        }
    }

    // The file's up axis becomes +Y (a turn, never a mirror)
    Mat4 toYUp;
    if (settings) {
        int upAxis = 1;
        int upSign = 1;
        ForEachProperty(*settings, [&](const std::string& name, const double* v, int count) {
            if (count < 1) return;
            if (name == "UpAxis") upAxis = static_cast<int>(v[0]);
            else if (name == "UpAxisSign") upSign = (v[0] < 0) ? -1 : 1;
        });
        if (upAxis == 2) toYUp = AxisRotation(0, -90.0 * upSign);
        else if (upAxis == 0) toYUp = AxisRotation(2, 90.0 * upSign);
        else if (upSign < 0) toYUp = AxisRotation(0, 180.0);
    }

    mesh.vertices.clear();
    mesh.tris.clear();
    std::vector<double> coords;
    std::vector<double> index;
    std::vector<uint32_t> polygon;
    size_t meshesShown = 0;
    bool cut = false;
    for (const MeshSource& src : meshes) {
        const Node* vNode = src.node->Child("Vertices");
        const Node* iNode = src.node->Child("PolygonVertexIndex");
        if (!vNode || !iNode || !NodeNumbers(*vNode, coords) || !NodeNumbers(*iNode, index)) continue;
        const size_t vertexCount = coords.size() / 3;
        if (vertexCount == 0) continue;

        std::vector<int64_t> users = src.models;
        if (users.empty()) users.push_back(0);      // used by no model: shown where it is
        for (int64_t modelId : users) {
            if (mesh.vertices.size() + vertexCount > kMaxVertices || mesh.tris.size() >= kMaxTriangles) {
                cut = true;
                break;
            }
            Mat4 place = toYUp;
            Mat4 geomLocal;
            auto mi = models.find(modelId);
            if (mi != models.end()) {
                geomLocal = mi->second.Geometric();
                place = toYUp * WorldOf(models, modelId) * geomLocal;
            }

            // Evaluate Linear-Blend Skinning (Deformer::Skin -> SubDeformer::Cluster) if rigged
            std::vector<Vec3d> skinAccum;
            std::vector<double> skinWeightSum;
            bool hasActiveSkin = false;
            if (!src.skins.empty()) {
                for (int64_t skinId : src.skins) {
                    auto sIt = skins.find(skinId);
                    if (sIt == skins.end()) continue;
                    for (int64_t clId : sIt->second.clusters) {
                        auto cIt = clusters.find(clId);
                        if (cIt == clusters.end() || !cIt->second.hasBone) continue;
                        const ClusterInfo& cl = cIt->second;
                        if (models.find(cl.boneModelId) == models.end() || cl.indexes.empty()) continue;

                        const Mat4 boneCurWorld = WorldOf(models, cl.boneModelId);
                        Mat4 invBoneBind;
                        if (!cl.hasTransformLink || !cl.transformLink.Inverse(invBoneBind)) {
                            if (!boneCurWorld.Inverse(invBoneBind)) continue;
                        }
                        const Mat4 clusterMat = boneCurWorld * invBoneBind * cl.transform * geomLocal;
                        if (!hasActiveSkin) {
                            skinAccum.assign(vertexCount, Vec3d{});
                            skinWeightSum.assign(vertexCount, 0.0);
                            hasActiveSkin = true;
                        }
                        const size_t pairCnt = std::min(cl.indexes.size(), cl.weights.size());
                        for (size_t p = 0; p < pairCnt; ++p) {
                            const int32_t vi = cl.indexes[p];
                            const double w = cl.weights[p];
                            if (vi < 0 || static_cast<size_t>(vi) >= vertexCount || !std::isfinite(w) || w <= 1e-9) continue;
                            const Vec3d bp = clusterMat.Apply({
                                coords[static_cast<size_t>(vi) * 3],
                                coords[static_cast<size_t>(vi) * 3 + 1],
                                coords[static_cast<size_t>(vi) * 3 + 2]
                            });
                            skinAccum[static_cast<size_t>(vi)].x += bp.x * w;
                            skinAccum[static_cast<size_t>(vi)].y += bp.y * w;
                            skinAccum[static_cast<size_t>(vi)].z += bp.z * w;
                            skinWeightSum[static_cast<size_t>(vi)] += w;
                        }
                    }
                }
            }

            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            for (size_t k = 0; k < vertexCount; ++k) {
                Vec3d w;
                if (hasActiveSkin && skinWeightSum[k] > 1e-8) {
                    const double invW = 1.0 / skinWeightSum[k];
                    w = toYUp.Apply({ skinAccum[k].x * invW, skinAccum[k].y * invW, skinAccum[k].z * invW });
                } else {
                    w = place.Apply({ coords[k * 3], coords[k * 3 + 1], coords[k * 3 + 2] });
                }
                if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z)) w = Vec3d();
                mesh.vertices.push_back({ static_cast<float>(w.x), static_cast<float>(w.y), static_cast<float>(w.z) });
            }

            // A negative index ends a polygon (it is the last index, as -index - 1)
            polygon.clear();
            bool bad = false;
            for (double raw : index) {
                int64_t v = static_cast<int64_t>(raw);
                const bool last = v < 0;
                if (last) v = -v - 1;
                if (v >= 0 && static_cast<size_t>(v) < vertexCount) polygon.push_back(base + static_cast<uint32_t>(v));
                else bad = true;
                if (!last) continue;
                if (!bad && polygon.size() >= 3) {
                    for (size_t k = 1; k + 1 < polygon.size(); ++k) {
                        mesh.tris.push_back({ polygon[0], polygon[k], polygon[k + 1], {} });
                    }
                }
                polygon.clear();
                bad = false;
                if (mesh.tris.size() >= kMaxTriangles) {
                    cut = true;
                    break;
                }
            }
            ++meshesShown;
        }
        if (cut) break;
    }
    if (mesh.tris.empty()) return false;

    if (animStackCount > 0) {
        mesh.animFrameCount = std::max<size_t>(mesh.animFrameCount, animStackCount + 1);
    }
    mesh.formatName = format;
    wchar_t note[96] = {};
    swprintf_s(note, cut ? L"%zu siatek (pokazano część)" : L"%zu siatek", meshesShown);
    mesh.statusMessage = note;
    return true;
}

} // namespace Formats
