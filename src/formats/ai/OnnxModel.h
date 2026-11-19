#pragma once
// OnnxModel.h — Open Neural Network Exchange (.onnx) streaming Protobuf graph reader.
#include "../common/AiModelCommon.h"

namespace Explorer2::AiModels::Detail {

// ============================================================================
// 3. ONNX (.onnx Protobuf ModelProto)
// ============================================================================
inline const wchar_t* OnnxElemTypeName(uint64_t dt, uint32_t& outElemBytes) {
    outElemBytes = 0;
    switch (dt) {
    case 1:  outElemBytes = 4;  return L"FLOAT32";
    case 2:  outElemBytes = 1;  return L"UINT8";
    case 3:  outElemBytes = 1;  return L"INT8";
    case 4:  outElemBytes = 2;  return L"UINT16";
    case 5:  outElemBytes = 2;  return L"INT16";
    case 6:  outElemBytes = 4;  return L"INT32";
    case 7:  outElemBytes = 8;  return L"INT64";
    case 8:  outElemBytes = 0;  return L"STRING";
    case 9:  outElemBytes = 1;  return L"BOOL";
    case 10: outElemBytes = 2;  return L"FLOAT16";
    case 11: outElemBytes = 8;  return L"DOUBLE";
    case 12: outElemBytes = 4;  return L"UINT32";
    case 13: outElemBytes = 8;  return L"UINT64";
    case 14: outElemBytes = 8;  return L"COMPLEX64";
    case 15: outElemBytes = 16; return L"COMPLEX128";
    case 16: outElemBytes = 2;  return L"BFLOAT16";
    case 17: outElemBytes = 1;  return L"FLOAT8E4M3FN";
    case 18: outElemBytes = 1;  return L"FLOAT8E4M3FNUZ";
    case 19: outElemBytes = 1;  return L"FLOAT8E5M2";
    case 20: outElemBytes = 1;  return L"FLOAT8E5M2FNUZ";
    case 21: outElemBytes = 1;  return L"UINT4";
    case 22: outElemBytes = 1;  return L"INT4";
    default: return L"UNKNOWN";
    }
}

inline bool ParseOnnxValueInfoStream(AiModelStream& st, uint64_t endPos, std::wstring& outName, std::wstring& outDtype, std::wstring& outShape) {
    std::string name;
    uint64_t elemType = 0;
    std::vector<std::wstring> dims;
    while (st.Pos() < endPos) {
        uint32_t fn = 0; uint8_t wt = 0;
        if (!st.ReadPbTag(endPos, fn, wt)) return false;
        if (fn == 1 && wt == 2) {
            if (!st.ReadPbStringBounded(endPos, name, 512)) return false;
        } else if (fn == 2 && wt == 2) { // TypeProto
            uint64_t tLen = 0;
            if (!st.ReadPbVarint(endPos, tLen) || st.Pos() + tLen > endPos) return false;
            const uint64_t tEnd = st.Pos() + tLen;
            while (st.Pos() < tEnd) {
                uint32_t tfn = 0; uint8_t twt = 0;
                if (!st.ReadPbTag(tEnd, tfn, twt)) return false;
                if (tfn == 1 && twt == 2) { // TypeProto.Tensor
                    uint64_t ttLen = 0;
                    if (!st.ReadPbVarint(tEnd, ttLen) || st.Pos() + ttLen > tEnd) return false;
                    const uint64_t ttEnd = st.Pos() + ttLen;
                    while (st.Pos() < ttEnd) {
                        uint32_t xfn = 0; uint8_t xwt = 0;
                        if (!st.ReadPbTag(ttEnd, xfn, xwt)) return false;
                        if (xfn == 1 && xwt == 0) {
                            if (!st.ReadPbVarint(ttEnd, elemType)) return false;
                        } else if (xfn == 2 && xwt == 2) { // TensorShapeProto
                            uint64_t sLen = 0;
                            if (!st.ReadPbVarint(ttEnd, sLen) || st.Pos() + sLen > ttEnd) return false;
                            const uint64_t sEnd = st.Pos() + sLen;
                            while (st.Pos() < sEnd) {
                                uint32_t sfn = 0; uint8_t swt = 0;
                                if (!st.ReadPbTag(sEnd, sfn, swt)) return false;
                                if (sfn == 1 && swt == 2) { // Dimension
                                    uint64_t dLen = 0;
                                    if (!st.ReadPbVarint(sEnd, dLen) || st.Pos() + dLen > sEnd) return false;
                                    const uint64_t dEnd = st.Pos() + dLen;
                                    std::wstring dimTxt = L"?";
                                    while (st.Pos() < dEnd) {
                                        uint32_t dfn = 0; uint8_t dwt = 0;
                                        if (!st.ReadPbTag(dEnd, dfn, dwt)) return false;
                                        if (dfn == 1 && dwt == 0) {
                                            uint64_t dv = 0;
                                            if (!st.ReadPbVarint(dEnd, dv)) return false;
                                            dimTxt = std::to_wstring(static_cast<int64_t>(dv));
                                        } else if (dfn == 2 && dwt == 2) {
                                            std::string dp;
                                            if (!st.ReadPbStringBounded(dEnd, dp, 128)) return false;
                                            dimTxt = PathUtils::Utf8ToWide(dp);
                                        } else if (!st.SkipPbField(dEnd, dwt)) {
                                            return false;
                                        }
                                    }
                                    if (dims.size() < 32) dims.push_back(std::move(dimTxt));
                                } else if (!st.SkipPbField(sEnd, swt)) {
                                    return false;
                                }
                            }
                        } else if (!st.SkipPbField(ttEnd, xwt)) {
                            return false;
                        }
                    }
                } else if (!st.SkipPbField(tEnd, twt)) {
                    return false;
                }
            }
        } else if (!st.SkipPbField(endPos, wt)) {
            return false;
        }
    }
    uint32_t dummyBytes = 0;
    outName = PathUtils::Utf8ToWide(name);
    outDtype = OnnxElemTypeName(elemType, dummyBytes);
    outShape = L"[";
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) outShape += L", ";
        outShape += dims[i];
    }
    outShape += L"]";
    return true;
}

inline bool ParseOnnxStream(AiModelStream& st, bool captureSheetDetails, ParsedModelData& out) {
    out = {};
    const uint64_t fileSize = st.FileSize();
    if (fileSize < 4) return false;

    uint64_t irVersion = 0;
    uint64_t modelVersion = 0;
    std::string producerName, producerVersion, domain, docString, graphName;
    std::vector<std::pair<std::wstring, std::wstring>> metaProps;
    std::map<std::string, uint64_t> opHist;
    std::map<std::wstring, uint64_t> dtypeHist;
    std::vector<ParsedTensorRow> ioEntries;

    bool hasGraph = false;
    uint64_t nodeCount = 0;
    uint64_t initCount = 0;
    uint64_t ioCount = 0;

    while (st.Pos() < fileSize) {
        uint32_t fn = 0; uint8_t wt = 0;
        if (!st.ReadPbTag(fileSize, fn, wt)) return false;
        if (fn == 1 && wt == 0) {
            if (!st.ReadPbVarint(fileSize, irVersion)) return false;
        } else if (fn == 2 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, producerName, 256)) return false;
        } else if (fn == 3 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, producerVersion, 128)) return false;
        } else if (fn == 4 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, domain, 256)) return false;
        } else if (fn == 5 && wt == 0) {
            if (!st.ReadPbVarint(fileSize, modelVersion)) return false;
        } else if (fn == 6 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, docString, 512)) return false;
        } else if (fn == 8 && wt == 2) { // opset_import
            uint64_t oLen = 0;
            if (!st.ReadPbVarint(fileSize, oLen) || st.Pos() + oLen > fileSize) return false;
            const uint64_t oEnd = st.Pos() + oLen;
            std::string opDom;
            uint64_t opVer = 0;
            while (st.Pos() < oEnd) {
                uint32_t ofn = 0; uint8_t owt = 0;
                if (!st.ReadPbTag(oEnd, ofn, owt)) return false;
                if (ofn == 1 && owt == 2) { if (!st.ReadPbStringBounded(oEnd, opDom, 128)) return false; }
                else if (ofn == 2 && owt == 0) { if (!st.ReadPbVarint(oEnd, opVer)) return false; }
                else if (!st.SkipPbField(oEnd, owt)) return false;
            }
            if (opVer == 0 || opVer > 1000) return false;
            out.onnxOpsets.push_back((opDom.empty() ? L"ai.onnx" : PathUtils::Utf8ToWide(opDom)) + L" v" + std::to_wstring(opVer));
        } else if (fn == 14 && wt == 2) { // metadata_props
            uint64_t mLen = 0;
            if (!st.ReadPbVarint(fileSize, mLen) || st.Pos() + mLen > fileSize) return false;
            const uint64_t mEnd = st.Pos() + mLen;
            std::string mk, mv;
            while (st.Pos() < mEnd) {
                uint32_t mfn = 0; uint8_t mwt = 0;
                if (!st.ReadPbTag(mEnd, mfn, mwt)) return false;
                if (mfn == 1 && mwt == 2) { if (!st.ReadPbStringBounded(mEnd, mk, 256)) return false; }
                else if (mfn == 2 && mwt == 2) { if (!st.ReadPbStringBounded(mEnd, mv, 512)) return false; }
                else if (!st.SkipPbField(mEnd, mwt)) return false;
            }
            if (!mk.empty()) {
                std::wstring wmk = PathUtils::Utf8ToWide(mk);
                std::wstring wmv = PathUtils::Utf8ToWide(mv);
                if (metaProps.size() < 128) metaProps.push_back({ wmk, wmv });
                if (captureSheetDetails && out.onnxSheetRows.size() < 300) {
                    out.onnxSheetRows.push_back({ L"Metadata Property", std::move(wmk), L"string", std::move(wmv) });
                }
            }
        } else if (fn == 7 && wt == 2) { // GraphProto
            hasGraph = true;
            uint64_t gLen = 0;
            if (!st.ReadPbVarint(fileSize, gLen) || st.Pos() + gLen > fileSize) return false;
            const uint64_t gEnd = st.Pos() + gLen;
            while (st.Pos() < gEnd) {
                uint32_t gfn = 0; uint8_t gwt = 0;
                if (!st.ReadPbTag(gEnd, gfn, gwt)) return false;
                if (gfn == 2 && gwt == 2) {
                    if (!st.ReadPbStringBounded(gEnd, graphName, 256)) return false;
                } else if (gfn == 1 && gwt == 2) { // NodeProto
                    uint64_t nLen = 0;
                    if (!st.ReadPbVarint(gEnd, nLen) || st.Pos() + nLen > gEnd) return false;
                    const uint64_t nEnd = st.Pos() + nLen;
                    const bool wantNodeRow = captureSheetDetails && (out.onnxNodeRows.size() < 500);
                    std::string nName, opType;
                    std::vector<std::string> ins, outs;
                    while (st.Pos() < nEnd) {
                        uint32_t nfn = 0; uint8_t nwt = 0;
                        if (!st.ReadPbTag(nEnd, nfn, nwt)) return false;
                        if (wantNodeRow && nfn == 1 && nwt == 2) {
                            std::string s;
                            if (!st.ReadPbStringBounded(nEnd, s, 256)) return false;
                            if (ins.size() < 32) ins.push_back(std::move(s));
                        } else if (wantNodeRow && nfn == 2 && nwt == 2) {
                            std::string s;
                            if (!st.ReadPbStringBounded(nEnd, s, 256)) return false;
                            if (outs.size() < 32) outs.push_back(std::move(s));
                        } else if (wantNodeRow && nfn == 3 && nwt == 2) {
                            if (!st.ReadPbStringBounded(nEnd, nName, 256)) return false;
                        } else if (nfn == 4 && nwt == 2) {
                            if (!st.ReadPbStringBounded(nEnd, opType, 128)) return false;
                        } else if (!st.SkipPbField(nEnd, nwt)) {
                            return false;
                        }
                    }
                    ++nodeCount;
                    if (!opType.empty()) opHist[opType]++;
                    if (wantNodeRow) {
                        auto joinVec = [](const std::vector<std::string>& v) {
                            std::wstring w;
                            for (size_t i = 0; i < v.size(); ++i) {
                                if (i > 0) w += L", ";
                                w += PathUtils::Utf8ToWide(v[i]);
                            }
                            return w;
                        };
                        out.onnxNodeRows.push_back({
                            std::to_wstring(nodeCount),
                            PathUtils::Utf8ToWide(opType),
                            PathUtils::Utf8ToWide(nName),
                            joinVec(ins),
                            joinVec(outs)
                        });
                    }
                } else if (gfn == 5 && gwt == 2) { // initializer TensorProto
                    uint64_t tLen = 0;
                    if (!st.ReadPbVarint(gEnd, tLen) || st.Pos() + tLen > gEnd) return false;
                    const uint64_t tEnd = st.Pos() + tLen;
                    std::vector<int64_t> dims;
                    uint64_t dt = 0;
                    std::string tName;
                    uint64_t rawDataBytes = 0;
                    std::vector<float> sampleFloats;
                    uint8_t rawHead[16] = {};
                    size_t rawHeadLen = 0;

                    while (st.Pos() < tEnd) {
                        uint32_t tfn = 0; uint8_t twt = 0;
                        if (!st.ReadPbTag(tEnd, tfn, twt)) return false;
                        if (tfn == 1 && twt == 0) {
                            uint64_t dv = 0;
                            if (!st.ReadPbVarint(tEnd, dv)) return false;
                            if (dims.size() < 32) dims.push_back(static_cast<int64_t>(dv));
                        } else if (tfn == 1 && twt == 2) { // packed repeated int64 dims
                            uint64_t pLen = 0;
                            if (!st.ReadPbVarint(tEnd, pLen) || st.Pos() + pLen > tEnd) return false;
                            const uint64_t pEnd = st.Pos() + pLen;
                            while (st.Pos() < pEnd) {
                                uint64_t dv = 0;
                                if (!st.ReadPbVarint(pEnd, dv)) return false;
                                if (dims.size() < 32) dims.push_back(static_cast<int64_t>(dv));
                            }
                        } else if (tfn == 2 && twt == 0) {
                            if (!st.ReadPbVarint(tEnd, dt)) return false;
                        } else if (captureSheetDetails && tfn == 4 && twt == 5) {
                            float fv = 0.0f;
                            if (st.Pos() + 4 > tEnd || !st.ReadF32(fv)) return false;
                            if (sampleFloats.size() < 4) sampleFloats.push_back(fv);
                        } else if (captureSheetDetails && tfn == 4 && twt == 2) {
                            uint64_t fLen = 0;
                            if (!st.ReadPbVarint(tEnd, fLen) || st.Pos() + fLen > tEnd) return false;
                            const size_t take = static_cast<size_t>(std::min<uint64_t>(fLen, 16));
                            uint8_t fb[16] = {};
                            if (take > 0 && !st.ReadBytes(fb, take)) return false;
                            if (fLen > take && !st.Skip(fLen - take)) return false;
                            for (size_t k = 0; k + 4 <= take && sampleFloats.size() < 4; k += 4) {
                                sampleFloats.push_back(RdF32LE(fb + k));
                            }
                        } else if (tfn == 8 && twt == 2) {
                            if (!st.ReadPbStringBounded(tEnd, tName, 512)) return false;
                        } else if (tfn == 9 && twt == 2) { // raw_data: sample up to 16B, then O(1) seek past multi-GB weights
                            uint64_t rLen = 0;
                            if (!st.ReadPbVarint(tEnd, rLen) || st.Pos() + rLen > tEnd) return false;
                            rawDataBytes = rLen;
                            rawHeadLen = captureSheetDetails ? static_cast<size_t>(std::min<uint64_t>(rLen, 16)) : 0;
                            if (rawHeadLen > 0 && !st.ReadBytes(rawHead, rawHeadLen)) return false;
                            if (rLen > rawHeadLen && !st.Skip(rLen - rawHeadLen)) return false;
                        } else if (!st.SkipPbField(tEnd, twt)) {
                            return false;
                        }
                    }

                    uint32_t elemBytes = 0;
                    const wchar_t* dtName = OnnxElemTypeName(dt, elemBytes);
                    if (dt == 0 || dt > 22) return false;

                    if (captureSheetDetails && dt == 1 && sampleFloats.empty() && rawHeadLen >= 4) {
                        for (size_t k = 0; k + 4 <= rawHeadLen && sampleFloats.size() < 4; k += 4) {
                            sampleFloats.push_back(RdF32LE(rawHead + k));
                        }
                    }

                    uint64_t params = 1;
                    if (!dims.empty()) {
                        for (int64_t d : dims) {
                            if (d <= 0) { params = 0; break; }
                            const uint64_t ud = static_cast<uint64_t>(d);
                            if (params > UINT64_MAX / ud) { params = UINT64_MAX; break; }
                            params *= ud;
                        }
                    }
                    const uint64_t byteSz = (rawDataBytes > 0)
                        ? rawDataBytes
                        : ((elemBytes > 0 && params <= UINT64_MAX / elemBytes) ? (params * elemBytes) : 0ULL);

                    ++initCount;
                    if (out.totalParameters <= UINT64_MAX - params) out.totalParameters += params;
                    else out.totalParameters = UINT64_MAX;
                    dtypeHist[dtName]++;

                    std::wstring shStr = L"[";
                    for (size_t i = 0; i < dims.size(); ++i) {
                        if (i > 0) shStr += L", ";
                        shStr += std::to_wstring(dims[i]);
                    }
                    shStr += L"]";

                    if (captureSheetDetails && out.onnxSheetRows.size() < 300) {
                        std::wstring shWithSample = shStr;
                        if (!sampleFloats.empty()) {
                            shWithSample += L"  e.g. ";
                            for (size_t i = 0; i < sampleFloats.size(); ++i) {
                                if (i > 0) shWithSample += L", ";
                                shWithSample += FormatScalarDouble(sampleFloats[i]);
                            }
                        }
                        out.onnxSheetRows.push_back({ L"Initializer Weight", PathUtils::Utf8ToWide(tName), dtName, std::move(shWithSample) });
                    }

                    if (out.tensors.size() < 512) {
                        ParsedTensorRow tr;
                        tr.name = tName.empty() ? (L"initializer_" + std::to_wstring(initCount)) : PathUtils::Utf8ToWide(tName);
                        tr.dtype = dtName;
                        tr.shape = std::move(shStr);
                        tr.paramCount = params;
                        tr.byteSize = byteSz;
                        out.tensors.push_back(std::move(tr));
                    }
                } else if ((gfn == 11 || gfn == 12) && gwt == 2) { // input / output ValueInfoProto
                    uint64_t vLen = 0;
                    if (!st.ReadPbVarint(gEnd, vLen) || st.Pos() + vLen > gEnd) return false;
                    const uint64_t vEnd = st.Pos() + vLen;
                    std::wstring vName, vDt, vShape;
                    if (!ParseOnnxValueInfoStream(st, vEnd, vName, vDt, vShape)) return false;
                    ++ioCount;
                    if (captureSheetDetails && out.onnxSheetRows.size() < 300) {
                        out.onnxSheetRows.push_back({ gfn == 11 ? L"Graph Input" : L"Graph Output", vName, vDt, vShape });
                    }
                    if (ioEntries.size() < 128) {
                        ParsedTensorRow ioTr;
                        ioTr.name = (gfn == 11 ? L"[in] " : L"[out] ") + vName;
                        ioTr.dtype = std::move(vDt);
                        ioTr.shape = std::move(vShape);
                        ioEntries.push_back(std::move(ioTr));
                    }
                } else if (!st.SkipPbField(gEnd, gwt)) {
                    return false;
                }
            }
        } else if (!st.SkipPbField(fileSize, wt)) {
            return false;
        }
    }

    if (st.Pos() != fileSize) return false;
    if (irVersion < 1 || irVersion > 20) return false;
    if (!hasGraph || (nodeCount == 0 && initCount == 0 && ioCount == 0)) return false;

    if (!ioEntries.empty()) {
        std::vector<ParsedTensorRow> merged;
        merged.reserve(ioEntries.size() + out.tensors.size());
        for (auto& e : ioEntries) merged.push_back(std::move(e));
        for (auto& e : out.tensors) {
            if (merged.size() >= 512) break;
            merged.push_back(std::move(e));
        }
        out.tensors = std::move(merged);
    }

    out.valid = true;
    out.formatVersion = static_cast<uint32_t>(irVersion);
    out.formatName = L"ONNX v" + std::to_wstring(irVersion);
    out.modelTitle = graphName.empty() ? L"" : PathUtils::Utf8ToWide(graphName);
    out.onnxProducerVersion = PathUtils::Utf8ToWide(producerVersion);
    out.arch = producerName.empty()
        ? L""
        : (PathUtils::Utf8ToWide(producerName) + (producerVersion.empty() ? L"" : (L" " + out.onnxProducerVersion)));
    out.baseModel = PathUtils::Utf8ToWide(producerName);
    out.tensorCount = initCount > 0 ? initCount : ioCount;

    for (const auto& kv : dtypeHist) {
        if (!out.precisionSummary.empty()) out.precisionSummary += L", ";
        out.precisionSummary += kv.first + L" (" + std::to_wstring(kv.second) + L")";
    }

    out.kvEntries.push_back({ L"ir_version", L"int64", std::to_wstring(irVersion) });
    if (!producerName.empty()) out.kvEntries.push_back({ L"producer", L"string", out.arch });
    if (!domain.empty()) out.kvEntries.push_back({ L"domain", L"string", PathUtils::Utf8ToWide(domain) });
    if (modelVersion != 0) out.kvEntries.push_back({ L"model_version", L"int64", std::to_wstring(modelVersion) });
    if (!graphName.empty()) out.kvEntries.push_back({ L"graph.name", L"string", out.modelTitle });
    out.kvEntries.push_back({ L"graph.nodes", L"uint64", std::to_wstring(nodeCount) });
    out.kvEntries.push_back({ L"graph.initializers", L"uint64", std::to_wstring(initCount) });
    out.kvEntries.push_back({ L"graph.io_ports", L"uint64", std::to_wstring(ioCount) });
    if (!out.onnxOpsets.empty()) {
        std::wstring opJoined;
        for (size_t i = 0; i < out.onnxOpsets.size(); ++i) {
            if (i > 0) opJoined += L", ";
            opJoined += out.onnxOpsets[i];
        }
        out.kvEntries.push_back({ L"opset_import", L"string", std::move(opJoined) });
    }
    if (!opHist.empty()) {
        std::vector<std::pair<std::string, uint64_t>> sortedOps(opHist.begin(), opHist.end());
        std::sort(sortedOps.begin(), sortedOps.end(), [](const auto& a, const auto& b) {
            return (a.second != b.second) ? (a.second > b.second) : (a.first < b.first);
        });
        std::wstring histStr;
        for (size_t i = 0; i < sortedOps.size() && i < 16; ++i) {
            if (i > 0) histStr += L", ";
            histStr += PathUtils::Utf8ToWide(sortedOps[i].first) + L" (" + std::to_wstring(sortedOps[i].second) + L")";
        }
        out.kvEntries.push_back({ L"operators", L"string", std::move(histStr) });
    }
    if (!docString.empty()) {
        out.kvEntries.push_back({ L"doc_string", L"string", TruncateWithEllipsis(PathUtils::Utf8ToWide(docString), 180) });
    }
    for (auto& mp : metaProps) {
        out.kvEntries.push_back({ std::move(mp.first), L"string", std::move(mp.second) });
    }
    out.metadataKeyCount = out.kvEntries.size();
    return true;
}


inline bool BuildOnnxDocument(const ParsedModelData& m, ParsedOfficeDocument& out) {
    if (!m.valid) return false;
        if (m.onnxNodeRows.empty() && m.onnxSheetRows.empty()) return false;
        SpreadsheetSheet ioSheet;
        ioSheet.name = L"ONNX_Model_And_Tensors";
        ioSheet.rows.push_back({ L"Role / Property", L"Name", L"Data Type", L"Shape / Value" });
        ioSheet.rows.push_back({ L"IR Version", L"ir_version", L"int64", std::to_wstring(m.formatVersion) });
        if (!m.baseModel.empty()) {
            ioSheet.rows.push_back({ L"Producer", m.baseModel, L"string", m.onnxProducerVersion });
        }
        if (!m.modelTitle.empty()) {
            ioSheet.rows.push_back({ L"Graph Name", m.modelTitle, L"GraphProto", std::to_wstring(m.onnxNodeRows.size()) + L" nodes" });
        }
        for (const auto& ops : m.onnxOpsets) {
            ioSheet.rows.push_back({ L"Opset Import", ops, L"OperatorSetId", L"" });
        }
        for (const auto& r : m.onnxSheetRows) {
            ioSheet.rows.push_back(r);
        }

        SpreadsheetSheet nodeSheet;
        nodeSheet.name = L"ONNX_Graph_Nodes";
        nodeSheet.rows.push_back({ L"#", L"OpType", L"Node Name", L"Inputs", L"Outputs" });
        for (const auto& nr : m.onnxNodeRows) {
            nodeSheet.rows.push_back(nr);
        }
        FinalizeSheetCols(ioSheet);
        FinalizeSheetCols(nodeSheet);

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"ONNX Neural Network Graph (.onnx)";
        out.sheets.push_back(std::move(ioSheet));
        if (nodeSheet.rows.size() > 1) out.sheets.push_back(std::move(nodeSheet));
        out.valid = true;
        return true;
}

} // namespace Explorer2::AiModels::Detail
