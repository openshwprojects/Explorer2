#pragma once
// AiModelDispatch.h — Streaming dispatcher across SafeTensors, GGUF, and ONNX readers.
#include "../ai/SafeTensorsModel.h"
#include "../ai/GgufModel.h"
#include "../ai/OnnxModel.h"

namespace Explorer2::AiModels {
namespace Detail {
inline bool BuildDocumentFromParsedModel(const ParsedModelData& m, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".safetensors") return BuildSafeTensorsDocument(m, out);
    if (extLower == L".gguf")        return BuildGgufDocument(m, out);
    if (extLower == L".onnx")        return BuildOnnxDocument(m, out);
    return false;
}
} // namespace Detail

template <typename SummaryT>
inline SummaryT InspectAiModelFileToSummary(const std::wstring& path, const std::wstring& ext) {
    SummaryT info{};
    Detail::ScopedFileHandle fh;
    uint64_t fileSize = 0;
    if (!Detail::OpenAiModelReadHandle(path, fh, fileSize)) return info;

    AiModelStream stream(fh.h, fileSize);
    const std::wstring extLower = PathUtils::ToLower(ext.empty() ? PathUtils::GetExtension(path) : ext);
    Detail::ParsedModelData m;
    bool ok = false;
    if (extLower == L".safetensors") ok = Detail::ParseSafeTensorsStream(stream, false, m);
    else if (extLower == L".gguf")   ok = Detail::ParseGgufStream(stream, m);
    else if (extLower == L".onnx")   ok = Detail::ParseOnnxStream(stream, false, m);
    if (!ok || !m.valid) return info;

    info.valid = true;
    info.formatName = std::move(m.formatName);
    info.arch = std::move(m.arch);
    info.modelTitle = std::move(m.modelTitle);
    info.baseModel = (extLower == L".onnx") ? std::wstring{} : std::move(m.baseModel);
    info.precisionSummary = std::move(m.precisionSummary);
    info.networkDimAlpha = std::move(m.networkDimAlpha);
    info.contextLength = std::move(m.contextLength);
    info.triggerWords = std::move(m.triggerWords);
    info.tensorCount = m.tensorCount;
    info.totalParameters = m.totalParameters;
    info.metadataKeyCount = m.metadataKeyCount;

    const size_t maxKv = 256;
    const size_t truncLen = (extLower == L".safetensors") ? 220 : 160;
    for (size_t i = 0; i < m.kvEntries.size() && i < maxKv; ++i) {
        info.kvRows.push_back({
            m.kvEntries[i].key,
            Detail::TruncateWithEllipsis(m.kvEntries[i].value, truncLen)
        });
    }

    const size_t maxTensors = (extLower == L".safetensors") ? 1024 : 512;
    for (size_t i = 0; i < m.tensors.size() && i < maxTensors; ++i) {
        typename SummaryT::TensorEntry te;
        te.name = std::move(m.tensors[i].name);
        te.dtype = std::move(m.tensors[i].dtype);
        te.shape = std::move(m.tensors[i].shape);
        te.paramCount = m.tensors[i].paramCount;
        te.byteSize = m.tensors[i].byteSize;
        info.tensors.push_back(std::move(te));
    }
    return info;
}

inline bool ParseAiModelStreamToDocument(AiModelStream& stream, const std::wstring& extLower, ParsedOfficeDocument& out) {
    Detail::ParsedModelData m;
    bool ok = false;
    if (extLower == L".safetensors") ok = Detail::ParseSafeTensorsStream(stream, true, m);
    else if (extLower == L".gguf")   ok = Detail::ParseGgufStream(stream, m);
    else if (extLower == L".onnx")   ok = Detail::ParseOnnxStream(stream, true, m);
    if (!ok || !m.valid) return false;
    return Detail::BuildDocumentFromParsedModel(m, extLower, out);
}

inline bool ParseAiModelFileToDocument(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
    Detail::ScopedFileHandle fh;
    uint64_t fileSize = 0;
    if (!Detail::OpenAiModelReadHandle(fullPath, fh, fileSize)) return false;
    AiModelStream stream(fh.h, fileSize);
    return ParseAiModelStreamToDocument(stream, PathUtils::ToLower(extLower), out);
}

inline bool ParseAiModelMemoryToDocument(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (!data || size == 0) return false;
    AiModelStream stream(data, size);
    return ParseAiModelStreamToDocument(stream, extLower, out);
}

} // namespace Explorer2::AiModels
