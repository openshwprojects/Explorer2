#pragma once
// IgesCad.h — IGES 5.3 (.iges / .igs) Initial Graphics Exchange Specification 3D CAD Parser & NURBS Evaluator

namespace Formats {

namespace DetailIges {

struct Vec3d {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct Mat3x4d {
    // Row-major 3x4 affine matrix: [ R11 R12 R13 T1 ; R21 R22 R23 T2 ; R31 R32 R33 T3 ]
    std::array<double, 12> m = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0
    };

    static Mat3x4d Identity() { return Mat3x4d{}; }

    static Mat3x4d Mul(const Mat3x4d& a, const Mat3x4d& b) {
        Mat3x4d r;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                r.m[row * 4 + col] =
                    a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                    a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                    a.m[row * 4 + 2] * b.m[2 * 4 + col];
            }
            r.m[row * 4 + 3] =
                a.m[row * 4 + 0] * b.m[0 * 4 + 3] +
                a.m[row * 4 + 1] * b.m[1 * 4 + 3] +
                a.m[row * 4 + 2] * b.m[2 * 4 + 3] +
                a.m[row * 4 + 3];
        }
        return r;
    }

    Vec3d TransformPoint(const Vec3d& p) const {
        return {
            m[0] * p.x + m[1] * p.y + m[2]  * p.z + m[3],
            m[4] * p.x + m[5] * p.y + m[6]  * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]
        };
    }
};

inline Vec3f ToViewerYUp(const Vec3d& p) {
    float x = static_cast<float>(p.x);
    float y = static_cast<float>(p.z);
    float z = static_cast<float>(-p.y);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return { x, y, z };
}

inline int ParseFixedInt(std::string_view s, int defVal = 0) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    if (s.empty()) return defVal;
    if (s.front() == '+') s.remove_prefix(1);
    int v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return defVal;
    return v;
}

inline double ParseIgesDouble(std::string_view s, double defVal = 0.0) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    if (s.empty()) return defVal;
    if (s.size() >= 90) return defVal;

    char buf[96];
    size_t outLen = 0;
    for (char c : s) {
        if (c == ' ' || c == '\t') continue; // IGES permits spaces inside split numeric fields
        if (c == 'd' || c == 'D') c = 'E';
        buf[outLen++] = c;
    }
    if (outLen == 0) return defVal;
    buf[outLen] = '\0';
    char* endPtr = nullptr;
    double val = std::strtod(buf, &endPtr);
    if (endPtr == buf || !std::isfinite(val)) return defVal;
    return val;
}

inline int ParseIgesInt(std::string_view s, int defVal = 0) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    if (s.empty()) return defVal;
    char buf[48];
    size_t outLen = 0;
    for (char c : s) {
        if (c == ' ' || c == '\t') continue;
        if (outLen + 1 >= sizeof(buf)) break;
        buf[outLen++] = c;
    }
    if (outLen == 0) return defVal;
    buf[outLen] = '\0';
    char* endPtr = nullptr;
    long v = std::strtol(buf, &endPtr, 10);
    if (endPtr == buf) return defVal;
    return static_cast<int>(v);
}

inline void ParseGlobalDelimiters(std::string_view gText, char& paramDelim, char& recDelim) {
    paramDelim = ',';
    recDelim = ';';
    size_t i = 0;
    const size_t n = gText.size();
    while (i < n && gText[i] == ' ') ++i;
    if (i >= n) return;

    if (gText[i] == ',') {
        paramDelim = ',';
        ++i;
    } else if (gText[i] >= '1' && gText[i] <= '9') {
        size_t numStart = i;
        while (i < n && gText[i] >= '0' && gText[i] <= '9') ++i;
        if (i < n && gText[i] == 'H') {
            int hLen = ParseFixedInt(gText.substr(numStart, i - numStart), 0);
            ++i; // skip 'H'
            if (hLen >= 1 && i + static_cast<size_t>(hLen) <= n) {
                paramDelim = gText[i];
                i += static_cast<size_t>(hLen);
                while (i < n && gText[i] == ' ') ++i;
                if (i < n && gText[i] == paramDelim) ++i;
            } else {
                return;
            }
        } else {
            return;
        }
    } else {
        return;
    }

    while (i < n && gText[i] == ' ') ++i;
    if (i >= n) return;
    if (gText[i] == paramDelim || gText[i] == ';') {
        recDelim = ';';
    } else if (gText[i] >= '1' && gText[i] <= '9') {
        size_t numStart = i;
        while (i < n && gText[i] >= '0' && gText[i] <= '9') ++i;
        if (i < n && gText[i] == 'H') {
            int hLen = ParseFixedInt(gText.substr(numStart, i - numStart), 0);
            ++i;
            if (hLen >= 1 && i + static_cast<size_t>(hLen) <= n) {
                recDelim = gText[i];
            }
        }
    }
}

// Checks whether a record buffer contains an unescaped record delimiter outside Hollerith strings
inline bool HasRecordTerminator(std::string_view rec, char paramDelim, char recDelim) {
    size_t i = 0;
    const size_t n = rec.size();
    bool atFieldStart = true;
    while (i < n) {
        char c = rec[i];
        if (atFieldStart && (c == ' ' || c == '\t')) {
            ++i;
            continue;
        }
        if (atFieldStart && c >= '1' && c <= '9') {
            size_t d = i;
            while (d < n && rec[d] >= '0' && rec[d] <= '9') ++d;
            if (d < n && rec[d] == 'H') {
                int hLen = ParseFixedInt(rec.substr(i, d - i), 0);
                if (hLen > 0) {
                    i = std::min(n, d + 1 + static_cast<size_t>(hLen));
                    atFieldStart = false;
                    continue;
                }
            }
        }
        if (c == recDelim) return true;
        if (c == paramDelim) {
            atFieldStart = true;
        } else {
            atFieldStart = false;
        }
        ++i;
    }
    return false;
}

inline void TokenizeParamRecord(
    std::string_view rec,
    char paramDelim,
    char recDelim,
    std::vector<std::string_view>& outTokens)
{
    outTokens.clear();
    size_t i = 0;
    const size_t n = rec.size();
    size_t fieldStart = 0;
    bool atFieldStart = true;

    while (i < n) {
        char c = rec[i];
        if (atFieldStart && (c == ' ' || c == '\t')) {
            ++i;
            continue;
        }
        if (atFieldStart && c >= '1' && c <= '9') {
            size_t d = i;
            while (d < n && rec[d] >= '0' && rec[d] <= '9') ++d;
            if (d < n && rec[d] == 'H') {
                int hLen = ParseFixedInt(rec.substr(i, d - i), 0);
                if (hLen > 0 && d + 1 + static_cast<size_t>(hLen) <= n) {
                    i = d + 1 + static_cast<size_t>(hLen);
                    atFieldStart = false;
                    continue;
                }
            }
        }
        if (c == recDelim) {
            outTokens.push_back(rec.substr(fieldStart, i - fieldStart));
            return;
        }
        if (c == paramDelim) {
            outTokens.push_back(rec.substr(fieldStart, i - fieldStart));
            ++i;
            fieldStart = i;
            atFieldStart = true;
            continue;
        }
        atFieldStart = false;
        ++i;
    }
    if (fieldStart < n) {
        outTokens.push_back(rec.substr(fieldStart, n - fieldStart));
    }
}

// Piegl & Tiller "The NURBS Book" Algorithm A2.1/A2.2 adapted for arbitrary clamped or unclamped IGES knot vectors
inline int FindKnotSpan(int K, int M, double u, const std::vector<double>& U) {
    // U has size K + M + 2, indices 0 .. K + M + 1; active domain [U[M], U[K + 1]]
    if (u <= U[static_cast<size_t>(M)]) {
        for (int i = M; i <= K; ++i) {
            if (U[static_cast<size_t>(i)] < U[static_cast<size_t>(i + 1)]) return i;
        }
        return M;
    }
    if (u >= U[static_cast<size_t>(K + 1)]) {
        for (int i = K; i >= M; --i) {
            if (U[static_cast<size_t>(i)] < U[static_cast<size_t>(i + 1)]) return i;
        }
        return K;
    }
    int low = M;
    int high = K + 1;
    int mid = (low + high) / 2;
    while (u < U[static_cast<size_t>(mid)] || u >= U[static_cast<size_t>(mid + 1)]) {
        if (u < U[static_cast<size_t>(mid)]) high = mid;
        else                                 low = mid;
        mid = (low + high) / 2;
        if (low >= high - 1) {
            mid = low;
            break;
        }
    }
    return mid;
}

inline void EvalBsplineBasis(
    int span,
    double u,
    int M,
    const std::vector<double>& U,
    std::vector<double>& N,
    std::vector<double>& left,
    std::vector<double>& right)
{
    N.assign(static_cast<size_t>(M + 1), 0.0);
    left.assign(static_cast<size_t>(M + 1), 0.0);
    right.assign(static_cast<size_t>(M + 1), 0.0);
    N[0] = 1.0;

    for (int j = 1; j <= M; ++j) {
        left[static_cast<size_t>(j)]  = u - U[static_cast<size_t>(span + 1 - j)];
        right[static_cast<size_t>(j)] = U[static_cast<size_t>(span + j)] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            double denom = right[static_cast<size_t>(r + 1)] + left[static_cast<size_t>(j - r)];
            double temp = (std::abs(denom) > 1e-30) ? (N[static_cast<size_t>(r)] / denom) : 0.0;
            N[static_cast<size_t>(r)] = saved + right[static_cast<size_t>(r + 1)] * temp;
            saved = left[static_cast<size_t>(j - r)] * temp;
        }
        N[static_cast<size_t>(j)] = saved;
    }
}

struct IgesEntity {
    int deSeq = 0;
    int entityType = 0;
    int paramLine = 0;
    int lineCount = 0;
    int transformPtr = 0;
    int formNumber = 0;
    bool blanked = false;
    bool isHelper = false;
    std::string paramBuf;
    std::vector<std::string_view> toks;
};

inline bool IsRecognizedStandaloneType(int t) {
    switch (t) {
        case 100: case 102: case 106: case 108: case 110: case 116:
        case 120: case 122: case 124: case 126: case 128:
        case 141: case 142: case 143: case 144: case 190: case 510:
            return true;
        default:
            return false;
    }
}

} // namespace DetailIges

inline bool ParseIgesCadMesh(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace DetailIges;
    if (sv.size() < 8) return false;
    mesh.formatName = L"IGES 3D CAD (.iges)";

    std::string globalText;
    std::vector<std::string_view> dLines;
    struct PLine {
        std::string_view data64;
        int dePtr = 0;
    };
    std::vector<PLine> pLines;
    std::vector<std::string_view> freeLines;
    bool hasStandardSection = false;

    size_t pos = 0;
    while (pos < sv.size() && pLines.size() < 500000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view rawLn = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!rawLn.empty() && rawLn.back() == '\r') rawLn.remove_suffix(1);
        if (rawLn.empty()) continue;

        if (rawLn.size() >= 73) {
            char sec = rawLn[72];
            if (sec == 'S' || sec == 'G' || sec == 'D' || sec == 'P' || sec == 'T') {
                // Verify cols 74..end look like spaces/digits (sequence number)
                bool validSeq = true;
                bool hasDigit = false;
                for (size_t k = 73; k < rawLn.size() && k < 80; ++k) {
                    char c = rawLn[k];
                    if (c >= '0' && c <= '9') hasDigit = true;
                    else if (c != ' ' && c != '\t') { validSeq = false; break; }
                }
                if (validSeq && (hasDigit || rawLn.size() == 73)) {
                    hasStandardSection = true;
                    if (sec == 'G') {
                        globalText.append(rawLn.substr(0, 72));
                    } else if (sec == 'D') {
                        dLines.push_back(rawLn);
                    } else if (sec == 'P') {
                        PLine pl;
                        pl.data64 = rawLn.substr(0, 64);
                        pl.dePtr = ParseFixedInt(rawLn.substr(64, 8), 0);
                        pLines.push_back(pl);
                    }
                    continue;
                }
            }
        }
        freeLines.push_back(rawLn);
    }

    char paramDelim = ',';
    char recDelim = ';';
    if (!globalText.empty()) {
        ParseGlobalDelimiters(globalText, paramDelim, recDelim);
    }

    std::vector<IgesEntity> entities;
    std::unordered_map<int, size_t> entityByDe;

    if (!dLines.empty() && !pLines.empty()) {
        // Full 80-column IGES with Directory Entry (D) + Parameter Data (P) sections
        const size_t nPairs = dLines.size() / 2;
        entities.reserve(nPairs);
        for (size_t k = 0; k < nPairs; ++k) {
            std::string_view d0 = dLines[2 * k];
            std::string_view d1 = dLines[2 * k + 1];

            IgesEntity ent;
            ent.entityType   = ParseFixedInt(d0.substr(0, 8), 0);
            ent.paramLine    = ParseFixedInt(d0.substr(8, 8), 0);
            ent.transformPtr = ParseFixedInt(d0.substr(48, 8), 0);
            ent.deSeq        = (d0.size() > 73) ? ParseFixedInt(d0.substr(73), 0) : 0;
            if (ent.deSeq <= 0) ent.deSeq = static_cast<int>(2 * k + 1);

            // Check blank status in cols 65-66 ('01' = blanked/hidden)
            if (d0.size() >= 66 && d0[64] == '0' && d0[65] == '1') {
                ent.blanked = true;
            }

            if (d1.size() >= 32) ent.lineCount = ParseFixedInt(d1.substr(24, 8), 0);
            if (d1.size() >= 40) ent.formNumber = ParseFixedInt(d1.substr(32, 8), 0);

            if (ent.paramLine >= 1 && static_cast<size_t>(ent.paramLine) <= pLines.size()) {
                size_t startIdx = static_cast<size_t>(ent.paramLine - 1);
                if (ent.lineCount >= 1) {
                    size_t endIdx = std::min(pLines.size(), startIdx + static_cast<size_t>(ent.lineCount));
                    ent.paramBuf.reserve((endIdx - startIdx) * 64);
                    for (size_t pi = startIdx; pi < endIdx; ++pi) {
                        ent.paramBuf.append(pLines[pi].data64);
                    }
                } else {
                    for (size_t pi = startIdx; pi < pLines.size(); ++pi) {
                        if (pi > startIdx && pLines[pi].dePtr != 0 && pLines[pi].dePtr != ent.deSeq) break;
                        ent.paramBuf.append(pLines[pi].data64);
                        if (HasRecordTerminator(ent.paramBuf, paramDelim, recDelim)) break;
                    }
                }
            }

            if (!ent.paramBuf.empty()) {
                size_t idx = entities.size();
                entityByDe[ent.deSeq] = idx;
                entities.push_back(std::move(ent));
            }
        }
    } else if (!pLines.empty()) {
        // Standalone 80-column P-section snippet (e.g. testdata/sample.iges)
        size_t pi = 0;
        int fallbackDe = 1;
        while (pi < pLines.size()) {
            IgesEntity ent;
            int curDe = pLines[pi].dePtr;
            ent.deSeq = (curDe > 0) ? curDe : fallbackDe;
            fallbackDe = ent.deSeq + 2;

            while (pi < pLines.size()) {
                ent.paramBuf.append(pLines[pi].data64);
                ++pi;
                if (curDe > 0) {
                    if (pi >= pLines.size() || pLines[pi].dePtr != curDe) break;
                } else {
                    if (HasRecordTerminator(ent.paramBuf, paramDelim, recDelim)) break;
                }
            }
            size_t idx = entities.size();
            entityByDe[ent.deSeq] = idx;
            entities.push_back(std::move(ent));
        }
    } else if (!hasStandardSection && !freeLines.empty()) {
        // Unpadded free-form P-record snippet fallback
        std::string accum;
        int nextDe = 1;
        for (std::string_view ln : freeLines) {
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
            if (ln.empty()) continue;
            if (accum.empty()) {
                size_t comma = ln.find(paramDelim);
                if (comma == std::string_view::npos) continue;
                int t = ParseFixedInt(ln.substr(0, comma), -1);
                if (!IsRecognizedStandaloneType(t)) continue;
            }
            accum.append(ln);
            if (HasRecordTerminator(accum, paramDelim, recDelim)) {
                IgesEntity ent;
                ent.deSeq = nextDe;
                nextDe += 2;
                ent.paramBuf = std::move(accum);
                accum.clear();
                size_t idx = entities.size();
                entityByDe[ent.deSeq] = idx;
                entities.push_back(std::move(ent));
            }
        }
    }

    if (entities.empty()) return false;

    // Tokenize parameter records
    for (IgesEntity& ent : entities) {
        TokenizeParamRecord(ent.paramBuf, paramDelim, recDelim, ent.toks);
        if (!ent.toks.empty()) {
            int pType = ParseIgesInt(ent.toks[0], 0);
            if (ent.entityType == 0) ent.entityType = pType;
        }
    }

    auto findEnt = [&](int de) -> IgesEntity* {
        if (de <= 0) return nullptr;
        auto it = entityByDe.find(de);
        if (it == entityByDe.end()) return nullptr;
        return &entities[it->second];
    };

    // Resolve Entity 124 Transformation Matrices (cycle-guarded)
    std::unordered_map<int, Mat3x4d> xformCache;
    std::function<Mat3x4d(int, int)> resolveTransform = [&](int dePtr, int depth) -> Mat3x4d {
        if (dePtr <= 0 || depth > 16) return Mat3x4d::Identity();
        auto itC = xformCache.find(dePtr);
        if (itC != xformCache.end()) return itC->second;

        const IgesEntity* xfEnt = findEnt(dePtr);
        if (!xfEnt || xfEnt->entityType != 124 || xfEnt->toks.size() < 13) {
            return Mat3x4d::Identity();
        }
        Mat3x4d local;
        for (int k = 0; k < 12; ++k) {
            local.m[static_cast<size_t>(k)] = ParseIgesDouble(xfEnt->toks[static_cast<size_t>(k + 1)],
                (k == 0 || k == 5 || k == 10) ? 1.0 : 0.0);
        }
        // Guard temporary cache entry before upstream recursion
        xformCache[dePtr] = local;
        if (xfEnt->transformPtr > 0 && xfEnt->transformPtr != dePtr) {
            Mat3x4d parent = resolveTransform(xfEnt->transformPtr, depth + 1);
            local = Mat3x4d::Mul(parent, local);
            xformCache[dePtr] = local;
        }
        return local;
    };

    // Mark subordinate helper curves/surfaces referenced by composite/revolved/extruded/trimmed entities
    std::function<void(int, int)> markCurveHelpers = [&](int de, int depth) {
        if (de <= 0 || depth > 16) return;
        IgesEntity* e = findEnt(de);
        if (!e) return;
        e->isHelper = true;
        if (e->entityType == 102 && e->toks.size() >= 2) {
            int n = ParseIgesInt(e->toks[1], 0);
            for (int i = 0; i < n && static_cast<size_t>(2 + i) < e->toks.size(); ++i) {
                markCurveHelpers(ParseIgesInt(e->toks[static_cast<size_t>(2 + i)], 0), depth + 1);
            }
        } else if (e->entityType == 142 && e->toks.size() >= 5) {
            markCurveHelpers(ParseIgesInt(e->toks[3], 0), depth + 1);
            markCurveHelpers(ParseIgesInt(e->toks[4], 0), depth + 1);
        } else if (e->entityType == 141 && e->toks.size() >= 5) {
            int n = ParseIgesInt(e->toks[4], 0);
            size_t idx = 5;
            for (int i = 0; i < n && idx < e->toks.size(); ++i) {
                markCurveHelpers(ParseIgesInt(e->toks[idx], 0), depth + 1);
                if (idx + 2 < e->toks.size()) {
                    int kCnt = ParseIgesInt(e->toks[idx + 2], 0);
                    idx += 3 + static_cast<size_t>(std::max(0, kCnt));
                } else {
                    break;
                }
            }
        }
    };

    for (const IgesEntity& ent : entities) {
        if (ent.entityType == 102 && ent.toks.size() >= 2) {
            int n = ParseIgesInt(ent.toks[1], 0);
            for (int i = 0; i < n && static_cast<size_t>(2 + i) < ent.toks.size(); ++i) {
                markCurveHelpers(ParseIgesInt(ent.toks[static_cast<size_t>(2 + i)], 0), 0);
            }
        } else if (ent.entityType == 120 && ent.toks.size() >= 3) {
            markCurveHelpers(ParseIgesInt(ent.toks[1], 0), 0);
            markCurveHelpers(ParseIgesInt(ent.toks[2], 0), 0);
        } else if (ent.entityType == 122 && ent.toks.size() >= 2) {
            markCurveHelpers(ParseIgesInt(ent.toks[1], 0), 0);
        } else if (ent.entityType == 144 && ent.toks.size() >= 2) {
            if (IgesEntity* s = findEnt(ParseIgesInt(ent.toks[1], 0))) s->isHelper = true;
            if (ent.toks.size() >= 5) markCurveHelpers(ParseIgesInt(ent.toks[4], 0), 0);
            if (ent.toks.size() >= 4) {
                int n2 = ParseIgesInt(ent.toks[3], 0);
                for (int i = 0; i < n2 && static_cast<size_t>(5 + i) < ent.toks.size(); ++i) {
                    markCurveHelpers(ParseIgesInt(ent.toks[static_cast<size_t>(5 + i)], 0), 0);
                }
            }
        } else if (ent.entityType == 143 && ent.toks.size() >= 3) {
            if (IgesEntity* s = findEnt(ParseIgesInt(ent.toks[2], 0))) s->isHelper = true;
            if (ent.toks.size() >= 4) {
                int n = ParseIgesInt(ent.toks[3], 0);
                for (int i = 0; i < n && static_cast<size_t>(4 + i) < ent.toks.size(); ++i) {
                    markCurveHelpers(ParseIgesInt(ent.toks[static_cast<size_t>(4 + i)], 0), 0);
                }
            }
        } else if (ent.entityType == 510 && ent.toks.size() >= 2) {
            if (IgesEntity* s = findEnt(ParseIgesInt(ent.toks[1], 0))) s->isHelper = true;
        }
    }

    // Reusable 3D curve sampler in IGES model space (applies each curve's Entity 124 transform)
    std::function<bool(const IgesEntity&, std::vector<Vec3d>&, int)> sampleCurveEntity =
        [&](const IgesEntity& ce, std::vector<Vec3d>& outPts, int depth) -> bool {
            if (depth > 16) return false;
            Mat3x4d xf = resolveTransform(ce.transformPtr, 0);

            if (ce.entityType == 110) {
                if (ce.toks.size() < 7) return false;
                Vec3d p0{ ParseIgesDouble(ce.toks[1]), ParseIgesDouble(ce.toks[2]), ParseIgesDouble(ce.toks[3]) };
                Vec3d p1{ ParseIgesDouble(ce.toks[4]), ParseIgesDouble(ce.toks[5]), ParseIgesDouble(ce.toks[6]) };
                outPts.push_back(xf.TransformPoint(p0));
                outPts.push_back(xf.TransformPoint(p1));
                return true;
            }

            if (ce.entityType == 100) {
                // Circular Arc: 100, ZT, X1, Y1, X2, Y2, X3, Y3
                if (ce.toks.size() < 8) return false;
                double zt = ParseIgesDouble(ce.toks[1]);
                double cx = ParseIgesDouble(ce.toks[2]);
                double cy = ParseIgesDouble(ce.toks[3]);
                double sx = ParseIgesDouble(ce.toks[4]);
                double sy = ParseIgesDouble(ce.toks[5]);
                double ex = ParseIgesDouble(ce.toks[6]);
                double ey = ParseIgesDouble(ce.toks[7]);

                double r = std::hypot(sx - cx, sy - cy);
                if (r <= 1e-12) return false;
                double a0 = std::atan2(sy - cy, sx - cx);
                double a1 = std::atan2(ey - cy, ex - cx);
                constexpr double kTwoPi = 6.28318530717958647692;
                double da = a1 - a0;
                if (std::hypot(ex - sx, ey - sy) <= r * 1e-6) {
                    da = kTwoPi; // full circle
                } else {
                    while (da <= 0.0) da += kTwoPi;
                    while (da > kTwoPi) da -= kTwoPi;
                }
                const int nSeg = 24;
                for (int i = 0; i <= nSeg; ++i) {
                    double a = a0 + da * (static_cast<double>(i) / static_cast<double>(nSeg));
                    Vec3d pt{ cx + r * std::cos(a), cy + r * std::sin(a), zt };
                    outPts.push_back(xf.TransformPoint(pt));
                }
                return true;
            }

            if (ce.entityType == 126) {
                // Rational B-Spline Curve: 126, K, M, PROP1..4, T(0..K+M+1), W(0..K), XYZ(0..K), V0, V1
                if (ce.toks.size() < 8) return false;
                int K = ParseIgesInt(ce.toks[1], -1);
                int M = ParseIgesInt(ce.toks[2], -1);
                if (K < 1 || K > 10000 || M < 1 || M > K) return false;

                const size_t nCtrl = static_cast<size_t>(K + 1);
                const size_t nKnots = static_cast<size_t>(K + M + 2);
                const size_t minToks = 7 + nKnots + nCtrl + 3 * nCtrl;
                if (ce.toks.size() < minToks) return false;

                size_t cur = 7;
                std::vector<double> knots(nKnots);
                for (size_t i = 0; i < nKnots; ++i) knots[i] = ParseIgesDouble(ce.toks[cur++]);
                for (size_t i = 1; i < nKnots; ++i) {
                    if (knots[i] < knots[i - 1]) return false;
                }

                std::vector<double> weights(nCtrl);
                for (size_t i = 0; i < nCtrl; ++i) {
                    double w = ParseIgesDouble(ce.toks[cur++], 1.0);
                    weights[i] = (w > 1e-12) ? w : 1.0;
                }

                std::vector<Vec3d> ctrl(nCtrl);
                for (size_t i = 0; i < nCtrl; ++i) {
                    ctrl[i].x = ParseIgesDouble(ce.toks[cur++]);
                    ctrl[i].y = ParseIgesDouble(ce.toks[cur++]);
                    ctrl[i].z = ParseIgesDouble(ce.toks[cur++]);
                }

                double v0 = knots[static_cast<size_t>(M)];
                double v1 = knots[static_cast<size_t>(K + 1)];
                if (cur + 2 <= ce.toks.size()) {
                    double pv0 = ParseIgesDouble(ce.toks[cur], v0);
                    double pv1 = ParseIgesDouble(ce.toks[cur + 1], v1);
                    if (pv1 > pv0) { v0 = pv0; v1 = pv1; }
                }
                if (v1 <= v0) return false;

                const int nSteps = std::clamp((K + 1) * 8, 24, 64);
                std::vector<double> N, left, right;
                for (int s = 0; s <= nSteps; ++s) {
                    double u = v0 + (v1 - v0) * (static_cast<double>(s) / static_cast<double>(nSteps));
                    int span = FindKnotSpan(K, M, u, knots);
                    EvalBsplineBasis(span, u, M, knots, N, left, right);

                    double wx = 0.0, wy = 0.0, wz = 0.0, wSum = 0.0;
                    for (int r = 0; r <= M; ++r) {
                        size_t ci = static_cast<size_t>(span - M + r);
                        double bw = N[static_cast<size_t>(r)] * weights[ci];
                        wx += bw * ctrl[ci].x;
                        wy += bw * ctrl[ci].y;
                        wz += bw * ctrl[ci].z;
                        wSum += bw;
                    }
                    if (std::abs(wSum) > 1e-30) {
                        double invW = 1.0 / wSum;
                        outPts.push_back(xf.TransformPoint({ wx * invW, wy * invW, wz * invW }));
                    }
                }
                return outPts.size() >= 2;
            }

            if (ce.entityType == 106) {
                // Copious Data: 106, IP, N, [ZT if IP==1], tuples...
                if (ce.toks.size() < 4) return false;
                int ip = ParseIgesInt(ce.toks[1], 0);
                int n  = ParseIgesInt(ce.toks[2], 0);
                if (n <= 0 || n > 100000) return false;

                if (ip == 1) {
                    if (ce.toks.size() < 4 + 2 * static_cast<size_t>(n)) return false;
                    double zt = ParseIgesDouble(ce.toks[3]);
                    size_t cur = 4;
                    for (int i = 0; i < n; ++i) {
                        double x = ParseIgesDouble(ce.toks[cur++]);
                        double y = ParseIgesDouble(ce.toks[cur++]);
                        outPts.push_back(xf.TransformPoint({ x, y, zt }));
                    }
                } else if (ip == 2) {
                    if (ce.toks.size() < 3 + 3 * static_cast<size_t>(n)) return false;
                    size_t cur = 3;
                    for (int i = 0; i < n; ++i) {
                        double x = ParseIgesDouble(ce.toks[cur++]);
                        double y = ParseIgesDouble(ce.toks[cur++]);
                        double z = ParseIgesDouble(ce.toks[cur++]);
                        outPts.push_back(xf.TransformPoint({ x, y, z }));
                    }
                } else if (ip == 3) {
                    if (ce.toks.size() < 3 + 6 * static_cast<size_t>(n)) return false;
                    size_t cur = 3;
                    for (int i = 0; i < n; ++i) {
                        double x = ParseIgesDouble(ce.toks[cur]);
                        double y = ParseIgesDouble(ce.toks[cur + 1]);
                        double z = ParseIgesDouble(ce.toks[cur + 2]);
                        cur += 6;
                        outPts.push_back(xf.TransformPoint({ x, y, z }));
                    }
                } else {
                    return false;
                }
                if (ce.formNumber == 63 && outPts.size() >= 3) {
                    outPts.push_back(outPts.front());
                }
                return !outPts.empty();
            }

            if (ce.entityType == 102) {
                // Composite Curve: 102, N, DE1..DEN
                if (ce.toks.size() < 3) return false;
                int n = ParseIgesInt(ce.toks[1], 0);
                if (n <= 0 || n > 4096) return false;
                std::vector<Vec3d> compPts;
                for (int i = 0; i < n && static_cast<size_t>(2 + i) < ce.toks.size(); ++i) {
                    const IgesEntity* sub = findEnt(ParseIgesInt(ce.toks[static_cast<size_t>(2 + i)], 0));
                    if (!sub) continue;
                    std::vector<Vec3d> seg;
                    if (sampleCurveEntity(*sub, seg, depth + 1) && !seg.empty()) {
                        size_t startIdx = 0;
                        if (!compPts.empty() &&
                            std::hypot(compPts.back().x - seg.front().x,
                                       std::hypot(compPts.back().y - seg.front().y,
                                                  compPts.back().z - seg.front().z)) < 1e-5) {
                            startIdx = 1;
                        }
                        for (size_t k = startIdx; k < seg.size(); ++k) {
                            compPts.push_back(xf.TransformPoint(seg[k]));
                        }
                    }
                }
                if (compPts.size() < 2) return false;
                outPts.insert(outPts.end(), compPts.begin(), compPts.end());
                return true;
            }

            return false;
        };

    constexpr size_t kMaxVerts = 4000000;
    constexpr size_t kMaxTris  = 4000000;

    auto emitQuadGrid = [&](const std::vector<Vec3d>& grid, int rows, int cols, const Mat3x4d& extraXf) -> bool {
        if (rows < 2 || cols < 2) return false;
        size_t totalPts = static_cast<size_t>(rows) * static_cast<size_t>(cols);
        if (grid.size() < totalPts) return false;
        if (mesh.vertices.size() + totalPts > kMaxVerts) return false;

        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.reserve(mesh.vertices.size() + totalPts);
        for (size_t i = 0; i < totalPts; ++i) {
            mesh.vertices.push_back(ToViewerYUp(extraXf.TransformPoint(grid[i])));
        }

        size_t startTris = mesh.tris.size();
        for (int r = 0; r + 1 < rows; ++r) {
            for (int c = 0; c + 1 < cols; ++c) {
                uint32_t i00 = base + static_cast<uint32_t>(r * cols + c);
                uint32_t i10 = base + static_cast<uint32_t>(r * cols + (c + 1));
                uint32_t i01 = base + static_cast<uint32_t>((r + 1) * cols + c);
                uint32_t i11 = base + static_cast<uint32_t>((r + 1) * cols + (c + 1));

                auto nonDegen = [&](uint32_t a, uint32_t b, uint32_t d) {
                    const Vec3f& va = mesh.vertices[a];
                    const Vec3f& vb = mesh.vertices[b];
                    const Vec3f& vd = mesh.vertices[d];
                    float ux = vb.x - va.x, uy = vb.y - va.y, uz = vb.z - va.z;
                    float vx = vd.x - va.x, vy = vd.y - va.y, vz = vd.z - va.z;
                    float nx = uy * vz - uz * vy;
                    float ny = uz * vx - ux * vz;
                    float nz = ux * vy - uy * vx;
                    return (nx * nx + ny * ny + nz * nz) > 1e-20f;
                };

                if (mesh.tris.size() < kMaxTris && nonDegen(i00, i10, i11)) {
                    mesh.tris.push_back({ i00, i10, i11, {} });
                }
                if (mesh.tris.size() < kMaxTris && nonDegen(i00, i11, i01)) {
                    mesh.tris.push_back({ i00, i11, i01, {} });
                }
            }
        }
        return mesh.tris.size() > startTris;
    };

    auto emitPlanarLoop = [&](const std::vector<Vec3d>& loopPts) -> bool {
        if (loopPts.size() < 3 || mesh.vertices.size() + loopPts.size() > kMaxVerts) return false;
        size_t n = loopPts.size();
        if (n >= 4 &&
            std::hypot(loopPts.front().x - loopPts.back().x,
                       std::hypot(loopPts.front().y - loopPts.back().y,
                                  loopPts.front().z - loopPts.back().z)) < 1e-6) {
            --n;
        }
        if (n < 3) return false;
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (size_t i = 0; i < n; ++i) {
            mesh.vertices.push_back(ToViewerYUp(loopPts[i]));
        }
        size_t before = mesh.tris.size();
        for (size_t i = 1; i + 1 < n && mesh.tris.size() < kMaxTris; ++i) {
            mesh.tris.push_back({ base, base + static_cast<uint32_t>(i), base + static_cast<uint32_t>(i + 1), {} });
        }
        return mesh.tris.size() > before;
    };

    std::set<int> emittedSurfaces;
    std::function<bool(const IgesEntity&, const Mat3x4d&, int)> evaluateSurfaceEntity =
        [&](const IgesEntity& se, const Mat3x4d& parentXf, int depth) -> bool {
            if (depth > 8) return false;
            Mat3x4d totalXf = Mat3x4d::Mul(parentXf, resolveTransform(se.transformPtr, 0));

            if (se.entityType == 128) {
                // Rational B-Spline Surface (IGES 5.3 §4.23):
                // 128, K1, K2, M1, M2, PROP1..5, S(0..K1+M1+1), T(0..K2+M2+1),
                // W((K1+1)*(K2+1)), XYZ((K1+1)*(K2+1)), U0, U1, V0, V1
                if (se.toks.size() < 16) return false;
                int K1 = ParseIgesInt(se.toks[1], -1);
                int K2 = ParseIgesInt(se.toks[2], -1);
                int M1 = ParseIgesInt(se.toks[3], -1);
                int M2 = ParseIgesInt(se.toks[4], -1);
                if (K1 < 1 || K1 > 2000 || K2 < 1 || K2 > 2000) return false;
                if (M1 < 1 || M1 > K1   || M2 < 1 || M2 > K2)   return false;

                const size_t nCtrlU = static_cast<size_t>(K1 + 1);
                const size_t nCtrlV = static_cast<size_t>(K2 + 1);
                if (nCtrlU * nCtrlV > 500000) return false;
                const size_t nCtrl  = nCtrlU * nCtrlV;
                const size_t nKnotS = static_cast<size_t>(K1 + M1 + 2);
                const size_t nKnotT = static_cast<size_t>(K2 + M2 + 2);
                const size_t minToks = 10 + nKnotS + nKnotT + nCtrl + 3 * nCtrl;
                if (se.toks.size() < minToks) return false;

                size_t cur = 10; // skip type + K1,K2,M1,M2 + PROP1..5
                std::vector<double> knotS(nKnotS), knotT(nKnotT);
                for (size_t i = 0; i < nKnotS; ++i) knotS[i] = ParseIgesDouble(se.toks[cur++]);
                for (size_t i = 0; i < nKnotT; ++i) knotT[i] = ParseIgesDouble(se.toks[cur++]);
                for (size_t i = 1; i < nKnotS; ++i) if (knotS[i] < knotS[i - 1]) return false;
                for (size_t i = 1; i < nKnotT; ++i) if (knotT[i] < knotT[i - 1]) return false;

                std::vector<double> weights(nCtrl);
                for (size_t i = 0; i < nCtrl; ++i) {
                    double w = ParseIgesDouble(se.toks[cur++], 1.0);
                    weights[i] = (w > 1e-12) ? w : 1.0;
                }

                std::vector<Vec3d> ctrl(nCtrl);
                for (size_t i = 0; i < nCtrl; ++i) {
                    ctrl[i].x = ParseIgesDouble(se.toks[cur++]);
                    ctrl[i].y = ParseIgesDouble(se.toks[cur++]);
                    ctrl[i].z = ParseIgesDouble(se.toks[cur++]);
                }

                double u0 = knotS[static_cast<size_t>(M1)];
                double u1 = knotS[static_cast<size_t>(K1 + 1)];
                double v0 = knotT[static_cast<size_t>(M2)];
                double v1 = knotT[static_cast<size_t>(K2 + 1)];
                if (cur + 4 <= se.toks.size()) {
                    double pu0 = ParseIgesDouble(se.toks[cur],     u0);
                    double pu1 = ParseIgesDouble(se.toks[cur + 1], u1);
                    double pv0 = ParseIgesDouble(se.toks[cur + 2], v0);
                    double pv1 = ParseIgesDouble(se.toks[cur + 3], v1);
                    if (pu1 > pu0) { u0 = pu0; u1 = pu1; }
                    if (pv1 > pv0) { v0 = pv0; v1 = pv1; }
                }
                if (u1 <= u0 || v1 <= v0) return false;

                const int stepsU = (M1 == 1 && K1 == 1) ? 4 : std::clamp((K1 + 1) * 5, 16, 24);
                const int stepsV = (M2 == 1 && K2 == 1) ? 4 : std::clamp((K2 + 1) * 5, 16, 24);
                const int cols = stepsU + 1;
                const int rows = stepsV + 1;

                std::vector<Vec3d> grid(static_cast<size_t>(rows * cols));
                std::vector<double> Nu, Nv, left, right;

                // Precompute U basis functions for all columns
                struct SpanBasis {
                    int span = 0;
                    std::vector<double> B;
                };
                std::vector<SpanBasis> uBasis(static_cast<size_t>(cols));
                for (int cu = 0; cu < cols; ++cu) {
                    double u = u0 + (u1 - u0) * (static_cast<double>(cu) / static_cast<double>(stepsU));
                    int spanU = FindKnotSpan(K1, M1, u, knotS);
                    EvalBsplineBasis(spanU, u, M1, knotS, Nu, left, right);
                    uBasis[static_cast<size_t>(cu)].span = spanU;
                    uBasis[static_cast<size_t>(cu)].B = Nu;
                }

                for (int rv = 0; rv < rows; ++rv) {
                    double v = v0 + (v1 - v0) * (static_cast<double>(rv) / static_cast<double>(stepsV));
                    int spanV = FindKnotSpan(K2, M2, v, knotT);
                    EvalBsplineBasis(spanV, v, M2, knotT, Nv, left, right);

                    for (int cu = 0; cu < cols; ++cu) {
                        const SpanBasis& ub = uBasis[static_cast<size_t>(cu)];
                        double wx = 0.0, wy = 0.0, wz = 0.0, wSum = 0.0;
                        for (int b = 0; b <= M2; ++b) {
                            size_t jIdx = static_cast<size_t>(spanV - M2 + b);
                            double bvVal = Nv[static_cast<size_t>(b)];
                            size_t rowOff = jIdx * nCtrlU;
                            for (int a = 0; a <= M1; ++a) {
                                size_t iIdx = static_cast<size_t>(ub.span - M1 + a);
                                size_t cIdx = rowOff + iIdx;
                                double w = ub.B[static_cast<size_t>(a)] * bvVal * weights[cIdx];
                                wx += w * ctrl[cIdx].x;
                                wy += w * ctrl[cIdx].y;
                                wz += w * ctrl[cIdx].z;
                                wSum += w;
                            }
                        }
                        Vec3d pt{};
                        if (std::abs(wSum) > 1e-30) {
                            double inv = 1.0 / wSum;
                            pt = { wx * inv, wy * inv, wz * inv };
                        }
                        grid[static_cast<size_t>(rv * cols + cu)] = pt;
                    }
                }
                return emitQuadGrid(grid, rows, cols, totalXf);
            }

            if (se.entityType == 120) {
                // Surface of Revolution: 120, L (axis DE), C (generatrix DE), SA, TA
                if (se.toks.size() < 5) return false;
                const IgesEntity* axisEnt = findEnt(ParseIgesInt(se.toks[1], 0));
                const IgesEntity* genEnt  = findEnt(ParseIgesInt(se.toks[2], 0));
                if (!axisEnt || !genEnt) return false;
                double sa = ParseIgesDouble(se.toks[3], 0.0);
                double ta = ParseIgesDouble(se.toks[4], 6.28318530717958647692);
                if (std::abs(ta - sa) <= 1e-9) return false;

                std::vector<Vec3d> axisPts, genPts;
                if (!sampleCurveEntity(*axisEnt, axisPts, depth + 1) || axisPts.size() < 2) return false;
                if (!sampleCurveEntity(*genEnt, genPts, depth + 1) || genPts.size() < 2) return false;

                Vec3d a0 = axisPts.front();
                Vec3d ad{ axisPts.back().x - a0.x, axisPts.back().y - a0.y, axisPts.back().z - a0.z };
                double aLen = std::sqrt(ad.x * ad.x + ad.y * ad.y + ad.z * ad.z);
                if (aLen <= 1e-12) return false;
                ad.x /= aLen; ad.y /= aLen; ad.z /= aLen;

                const int nAng = 24;
                const int rows = static_cast<int>(genPts.size());
                const int cols = nAng + 1;
                std::vector<Vec3d> grid(static_cast<size_t>(rows * cols));

                for (int c = 0; c < cols; ++c) {
                    double ang = sa + (ta - sa) * (static_cast<double>(c) / static_cast<double>(nAng));
                    double ca = std::cos(ang), saSin = std::sin(ang);
                    for (int r = 0; r < rows; ++r) {
                        Vec3d v{ genPts[static_cast<size_t>(r)].x - a0.x,
                                 genPts[static_cast<size_t>(r)].y - a0.y,
                                 genPts[static_cast<size_t>(r)].z - a0.z };
                        double dotUV = ad.x * v.x + ad.y * v.y + ad.z * v.z;
                        Vec3d crossUV{
                            ad.y * v.z - ad.z * v.y,
                            ad.z * v.x - ad.x * v.z,
                            ad.x * v.y - ad.y * v.x
                        };
                        Vec3d rot{
                            a0.x + v.x * ca + crossUV.x * saSin + ad.x * dotUV * (1.0 - ca),
                            a0.y + v.y * ca + crossUV.y * saSin + ad.y * dotUV * (1.0 - ca),
                            a0.z + v.z * ca + crossUV.z * saSin + ad.z * dotUV * (1.0 - ca)
                        };
                        grid[static_cast<size_t>(r * cols + c)] = rot;
                    }
                }
                return emitQuadGrid(grid, rows, cols, totalXf);
            }

            if (se.entityType == 122) {
                // Tabulated Cylinder: 122, DE (directrix), LX, LY, LZ
                if (se.toks.size() < 5) return false;
                const IgesEntity* dirEnt = findEnt(ParseIgesInt(se.toks[1], 0));
                if (!dirEnt) return false;
                std::vector<Vec3d> dirPts;
                if (!sampleCurveEntity(*dirEnt, dirPts, depth + 1) || dirPts.size() < 2) return false;

                Vec3d lTerm{ ParseIgesDouble(se.toks[2]), ParseIgesDouble(se.toks[3]), ParseIgesDouble(se.toks[4]) };
                Vec3d delta{ lTerm.x - dirPts.front().x, lTerm.y - dirPts.front().y, lTerm.z - dirPts.front().z };
                if (std::hypot(delta.x, std::hypot(delta.y, delta.z)) <= 1e-12) return false;

                const int cols = static_cast<int>(dirPts.size());
                std::vector<Vec3d> grid(static_cast<size_t>(2 * cols));
                for (int c = 0; c < cols; ++c) {
                    const Vec3d& p = dirPts[static_cast<size_t>(c)];
                    grid[static_cast<size_t>(0 * cols + c)] = p;
                    grid[static_cast<size_t>(1 * cols + c)] = { p.x + delta.x, p.y + delta.y, p.z + delta.z };
                }
                return emitQuadGrid(grid, 2, cols, totalXf);
            }

            if (se.entityType == 144) {
                // Trimmed Parametric Surface: 144, PTS, N1, N2, PTO, PTI...
                if (se.toks.size() < 2) return false;
                int ptsDe = ParseIgesInt(se.toks[1], 0);
                const IgesEntity* baseSurf = findEnt(ptsDe);
                if (baseSurf) {
                    if (baseSurf->entityType == 128 || baseSurf->entityType == 120 || baseSurf->entityType == 122) {
                        if (emittedSurfaces.insert(ptsDe).second || se.transformPtr != 0) {
                            if (evaluateSurfaceEntity(*baseSurf, totalXf, depth + 1)) return true;
                        } else {
                            return true;
                        }
                    }
                    // For bounded Plane (108 / 190), triangulate outer 3D boundary curve from Entity 142 (PTO)
                    if (se.toks.size() >= 5) {
                        const IgesEntity* pto142 = findEnt(ParseIgesInt(se.toks[4], 0));
                        if (pto142 && pto142->entityType == 142 && pto142->toks.size() >= 5) {
                            const IgesEntity* cptr = findEnt(ParseIgesInt(pto142->toks[4], 0));
                            if (cptr) {
                                std::vector<Vec3d> loop3D;
                                if (sampleCurveEntity(*cptr, loop3D, depth + 1) && loop3D.size() >= 3) {
                                    for (Vec3d& p : loop3D) p = totalXf.TransformPoint(p);
                                    return emitPlanarLoop(loop3D);
                                }
                            }
                        }
                    }
                }
                return false;
            }

            if (se.entityType == 143) {
                // Bounded Surface: 143, TYPE, SPTR, N, BDPT...
                if (se.toks.size() < 3) return false;
                int sptrDe = ParseIgesInt(se.toks[2], 0);
                const IgesEntity* baseSurf = findEnt(sptrDe);
                if (baseSurf && (baseSurf->entityType == 128 || baseSurf->entityType == 120 || baseSurf->entityType == 122)) {
                    if (emittedSurfaces.insert(sptrDe).second || se.transformPtr != 0) {
                        return evaluateSurfaceEntity(*baseSurf, totalXf, depth + 1);
                    }
                    return true;
                }
                return false;
            }

            if (se.entityType == 510) {
                // Face (MSBO B-Rep): 510, SURF, N, OF, LOOP...
                if (se.toks.size() < 2) return false;
                int surfDe = ParseIgesInt(se.toks[1], 0);
                const IgesEntity* baseSurf = findEnt(surfDe);
                if (baseSurf && (baseSurf->entityType == 128 || baseSurf->entityType == 120 || baseSurf->entityType == 122)) {
                    if (emittedSurfaces.insert(surfDe).second) {
                        return evaluateSurfaceEntity(*baseSurf, totalXf, depth + 1);
                    }
                    return true;
                }
                return false;
            }

            return false;
        };

    std::vector<std::vector<Vec3f>> polylines;
    std::vector<Vec3f> discretePoints;

    for (const IgesEntity& ent : entities) {
        if (ent.blanked || ent.isHelper) continue;

        if (ent.entityType == 128 || ent.entityType == 120 || ent.entityType == 122 ||
            ent.entityType == 144 || ent.entityType == 143 || ent.entityType == 510) {
            if (ent.entityType == 128 || ent.entityType == 120 || ent.entityType == 122) {
                if (!emittedSurfaces.insert(ent.deSeq).second) continue;
            }
            evaluateSurfaceEntity(ent, Mat3x4d::Identity(), 0);
        } else if (ent.entityType == 106) {
            std::vector<Vec3d> pts;
            if (!sampleCurveEntity(ent, pts, 0) || pts.empty()) continue;
            if (ent.formNumber == 63 && pts.size() >= 3) {
                emitPlanarLoop(pts);
            } else if (ent.formNumber == 1 || ent.formNumber == 2 || ent.formNumber == 3 || pts.size() == 1) {
                for (const Vec3d& p : pts) discretePoints.push_back(ToViewerYUp(p));
            } else {
                std::vector<Vec3f> poly;
                poly.reserve(pts.size());
                for (const Vec3d& p : pts) poly.push_back(ToViewerYUp(p));
                polylines.push_back(std::move(poly));
            }
        } else if (ent.entityType == 110 || ent.entityType == 100 || ent.entityType == 126 || ent.entityType == 102) {
            std::vector<Vec3d> pts;
            if (sampleCurveEntity(ent, pts, 0) && pts.size() >= 2) {
                std::vector<Vec3f> poly;
                poly.reserve(pts.size());
                for (const Vec3d& p : pts) poly.push_back(ToViewerYUp(p));
                polylines.push_back(std::move(poly));
            }
        } else if (ent.entityType == 116 && ent.toks.size() >= 4) {
            Mat3x4d xf = resolveTransform(ent.transformPtr, 0);
            Vec3d p{ ParseIgesDouble(ent.toks[1]), ParseIgesDouble(ent.toks[2]), ParseIgesDouble(ent.toks[3]) };
            discretePoints.push_back(ToViewerYUp(xf.TransformPoint(p)));
        }
    }

    // Compute proportional tube/marker radius from scene bounding box across surfaces + polylines + points
    if (!polylines.empty() || (!discretePoints.empty() && mesh.tris.empty())) {
        Vec3f bMin{ 1e30f, 1e30f, 1e30f };
        Vec3f bMax{ -1e30f, -1e30f, -1e30f };
        auto accPt = [&](const Vec3f& v) {
            bMin.x = std::min(bMin.x, v.x); bMin.y = std::min(bMin.y, v.y); bMin.z = std::min(bMin.z, v.z);
            bMax.x = std::max(bMax.x, v.x); bMax.y = std::max(bMax.y, v.y); bMax.z = std::max(bMax.z, v.z);
        };
        for (const Vec3f& v : mesh.vertices) accPt(v);
        for (const auto& pl : polylines) for (const Vec3f& v : pl) accPt(v);
        for (const Vec3f& v : discretePoints) accPt(v);

        float dx = std::max(0.0f, bMax.x - bMin.x);
        float dy = std::max(0.0f, bMax.y - bMin.y);
        float dz = std::max(0.0f, bMax.z - bMin.z);
        float diag = std::sqrt(dx * dx + dy * dy + dz * dz);
        float r = (diag > 1e-6f) ? (diag * 0.008f) : 0.02f;

        // Emit each entity's polyline strictly along its own segments (NEVER connecting separate entities!)
        for (const auto& poly : polylines) {
            for (size_t i = 0; i + 1 < poly.size(); ++i) {
                if (mesh.vertices.size() + 6 > kMaxVerts || mesh.tris.size() + 6 > kMaxTris) break;
                const Vec3f& a = poly[i];
                const Vec3f& b = poly[i + 1];
                float sx = b.x - a.x, sy = b.y - a.y, sz = b.z - a.z;
                float segLen = std::sqrt(sx * sx + sy * sy + sz * sz);
                if (segLen <= 1e-9f) continue;
                sx /= segLen; sy /= segLen; sz /= segLen;

                // Orthonormal basis (u, v) perpendicular to segment direction s
                float refX = (std::abs(sx) < 0.85f) ? 1.0f : 0.0f;
                float refY = (std::abs(sx) < 0.85f) ? 0.0f : 1.0f;
                float ux = sy * 0.0f - sz * refY;
                float uy = sz * refX - sx * 0.0f;
                float uz = sx * refY - sy * refX;
                float uLen = std::sqrt(ux * ux + uy * uy + uz * uz);
                if (uLen <= 1e-9f) continue;
                ux /= uLen; uy /= uLen; uz /= uLen;

                float vx = sy * uz - sz * uy;
                float vy = sz * ux - sx * uz;
                float vz = sx * uy - sy * ux;

                // 3-sided triangular prism around segment a -> b
                constexpr float kC1 = -0.5f;
                constexpr float kS1 =  0.86602540378f;
                Vec3f off0{ ux * r, uy * r, uz * r };
                Vec3f off1{ (ux * kC1 + vx * kS1) * r, (uy * kC1 + vy * kS1) * r, (uz * kC1 + vz * kS1) * r };
                Vec3f off2{ (ux * kC1 - vx * kS1) * r, (uy * kC1 - vy * kS1) * r, (uz * kC1 - vz * kS1) * r };

                uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({ a.x + off0.x, a.y + off0.y, a.z + off0.z });
                mesh.vertices.push_back({ a.x + off1.x, a.y + off1.y, a.z + off1.z });
                mesh.vertices.push_back({ a.x + off2.x, a.y + off2.y, a.z + off2.z });
                mesh.vertices.push_back({ b.x + off0.x, b.y + off0.y, b.z + off0.z });
                mesh.vertices.push_back({ b.x + off1.x, b.y + off1.y, b.z + off1.z });
                mesh.vertices.push_back({ b.x + off2.x, b.y + off2.y, b.z + off2.z });

                mesh.tris.push_back({ base + 0, base + 3, base + 4, {} });
                mesh.tris.push_back({ base + 0, base + 4, base + 1, {} });
                mesh.tris.push_back({ base + 1, base + 4, base + 5, {} });
                mesh.tris.push_back({ base + 1, base + 5, base + 2, {} });
                mesh.tris.push_back({ base + 2, base + 5, base + 3, {} });
                mesh.tris.push_back({ base + 2, base + 3, base + 0, {} });
            }
        }

        // Emit discrete point tetrahedron markers only if point entities exist
        if (!discretePoints.empty() && mesh.tris.empty()) {
            float mr = r * 1.5f;
            for (const Vec3f& p : discretePoints) {
                if (mesh.vertices.size() + 4 > kMaxVerts || mesh.tris.size() + 4 > kMaxTris) break;
                uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({ p.x,      p.y + mr, p.z });
                mesh.vertices.push_back({ p.x - mr, p.y - mr, p.z - mr });
                mesh.vertices.push_back({ p.x + mr, p.y - mr, p.z - mr });
                mesh.vertices.push_back({ p.x,      p.y - mr, p.z + mr });
                mesh.tris.push_back({ base + 0, base + 1, base + 2, {} });
                mesh.tris.push_back({ base + 0, base + 2, base + 3, {} });
                mesh.tris.push_back({ base + 0, base + 3, base + 1, {} });
                mesh.tris.push_back({ base + 1, base + 3, base + 2, {} });
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) {
        mesh.vertices.clear();
        mesh.tris.clear();
        return false;
    }
    return true;
}

} // namespace Formats
