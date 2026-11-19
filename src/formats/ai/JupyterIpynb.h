#pragma once
// JupyterIpynb.h — Jupyter Notebook (.ipynb) JSON Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 2. Jupyter Notebook (.ipynb) Strict JSON Parser
// ============================================================================


inline bool ParseIpynbNotebook(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using JVal = ::Explorer2::Json::Value;
    JVal root;
    std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (!::Explorer2::Json::ParseDocument(text, root) || !root.IsObject()) return false;

    const JVal* cells = root.Find("cells");
    const JVal* nbf = root.Find("nbformat");
    if (!cells && !nbf) return false;
    if (cells && !cells->IsArray()) return false;

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"Jupyter Interactive Notebook (.ipynb)";

    std::wstring hdr = L"Jupyter Interactive Notebook";
    if (nbf && nbf->IsNumber()) {
        hdr += L" (nbformat v" + std::to_wstring(static_cast<int>(nbf->numVal));
        const JVal* nbfMin = root.Find("nbformat_minor");
        if (nbfMin && nbfMin->IsNumber()) hdr += L"." + std::to_wstring(static_cast<int>(nbfMin->numVal));
        hdr += L")";
    }
    out.paragraphs.push_back({ hdr, 1, false, false });

    if (const JVal* meta = root.Find("metadata")) {
        std::string kinfo;
        if (const JVal* ks = meta->Find("kernelspec")) {
            std::string dn = ks->GetString("display_name");
            std::string lang = ks->GetString("language");
            if (!dn.empty()) kinfo += "Kernel: " + dn;
            if (!lang.empty()) kinfo += (kinfo.empty() ? "" : " | ") + std::string("Language: ") + lang;
        }
        if (const JVal* li = meta->Find("language_info")) {
            std::string ver = li->GetString("version");
            if (!ver.empty()) kinfo += (kinfo.empty() ? "" : " | ") + std::string("Version: ") + ver;
        }
        if (!kinfo.empty()) out.paragraphs.push_back({ PathUtils::Utf8ToWide(kinfo), 3, false, false });
    }

    // nbformat stores multi-line text either as one string or as an array of line strings
    auto joinLines = [](const JVal* v) -> std::string {
        if (!v) return {};
        if (v->IsString()) return v->strVal;
        std::string res;
        if (v->IsArray()) {
            for (const auto& item : v->arrVal) {
                if (item.IsString()) res += item.strVal;
            }
        }
        return res;
    };
    auto addText = [&](const std::wstring& prefix, const std::string& txt, bool bullet) {
        std::wstring w = PathUtils::Trim(PathUtils::Utf8ToWide(txt));
        if (!w.empty()) out.paragraphs.push_back({ prefix + w, 0, bullet, false });
    };

    size_t idx = 1;
    size_t codeCells = 0, markdownCells = 0, outputsShown = 0;
    if (cells) {
        for (const auto& cell : cells->arrVal) {
            if (!cell.IsObject()) continue;
            std::string ctype = cell.GetString("cell_type", "code");
            std::wstring title = L"Cell [" + std::to_wstring(idx++) + L"] — " + PathUtils::Utf8ToWide(ctype);
            if (ctype == "code") {
                ++codeCells;
                if (const JVal* ec = cell.Find("execution_count"); ec && ec->IsNumber()) {
                    title += L"  In [" + std::to_wstring(static_cast<long long>(ec->numVal)) + L"]";
                }
            } else if (ctype == "markdown") {
                ++markdownCells;
            }
            out.paragraphs.push_back({ title, 2, false, false });
            addText(L"", joinLines(cell.Find("source")), false);

            const JVal* outs = cell.Find("outputs");
            if (outs && outs->IsArray()) {
                for (const auto& o : outs->arrVal) {
                    if (!o.IsObject()) continue;
                    std::string otype = o.GetString("output_type");
                    if (otype == "error") {
                        addText(L"Error: ", o.GetString("ename") + ": " + o.GetString("evalue"), true);
                        ++outputsShown;
                        continue;
                    }
                    // stream outputs (and nbformat 3 / minimal writers) carry "text"
                    std::string txt = joinLines(o.Find("text"));
                    const JVal* data = o.Find("data");
                    if (txt.empty() && data && data->IsObject()) {
                        txt = joinLines(data->Find("text/plain"));
                        if (txt.empty()) {
                            for (const char* mime : { "image/png", "image/jpeg", "image/svg+xml", "text/html", "application/json" }) {
                                if (data->Find(mime)) {
                                    txt = std::string("[") + mime + " output]";
                                    break;
                                }
                            }
                        }
                    }
                    if (!txt.empty()) {
                        std::wstring prefix = L"Output: ";
                        if (const JVal* ec = o.Find("execution_count"); ec && ec->IsNumber()) {
                            prefix = L"Out [" + std::to_wstring(static_cast<long long>(ec->numVal)) + L"]: ";
                        }
                        addText(prefix, txt, true);
                        ++outputsShown;
                    }
                }
            }
            if (out.paragraphs.size() >= 300) break;
        }
    }
    if (cells) {
        out.paragraphs.insert(out.paragraphs.begin() + 1, DocParagraph{ std::to_wstring(cells->arrVal.size()) + L" cells (" +
            std::to_wstring(codeCells) + L" code, " + std::to_wstring(markdownCells) + L" markdown), " +
            std::to_wstring(outputsShown) + L" outputs shown", 3, false, false });
    }

    RecountDocText(out);
    out.valid = !out.paragraphs.empty();
    return out.valid;
}

} // namespace AiDataDetail

inline bool ParseJupyterIpynb(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseIpynbNotebook(raw, out); }

} // namespace Formats
