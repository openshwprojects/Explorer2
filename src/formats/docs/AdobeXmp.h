#pragma once
// AdobeXmp.h — Adobe XMP / Camera Raw Sidecar (.xmp)

namespace Formats {

inline bool ParseAdobeXmpDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #97. Adobe XMP / Camera Raw Develop Sidecar (.xmp)
    if (ext == L".xmp") {
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("xmpmeta") == std::string_view::npos && sv.find("rdf:RDF") == std::string_view::npos) return false;

        // Every property of every rdf:Description: attributes (prefix:name="value") and property elements, whose value
        // is their text, their rdf:Alt / Seq / Bag items ("; "), or their structure's fields ("name=value, ...")
        namespace X = ::Explorer2::XmlTagScan;
        SpreadsheetSheet sh;
        sh.name = L"XMP_Properties";
        sh.maxCols = 3;
        sh.rows.push_back({ L"Namespace", L"Property", L"Value" });
        auto forEachAttr = [](std::string_view attrs, auto&& fn) {
            size_t i = 0;
            const size_t n = attrs.size();
            while (i < n) {
                while (i < n && X::IsWs(attrs[i])) ++i;
                size_t nameStart = i;
                while (i < n && X::IsNameChar(attrs[i])) ++i;
                if (i == nameStart) { ++i; continue; }
                std::string_view name = attrs.substr(nameStart, i - nameStart);
                while (i < n && X::IsWs(attrs[i])) ++i;
                if (i >= n || attrs[i] != '=') continue;
                ++i;
                while (i < n && X::IsWs(attrs[i])) ++i;
                if (i >= n || (attrs[i] != '"' && attrs[i] != '\'')) continue;
                char q = attrs[i++];
                size_t valStart = i;
                while (i < n && attrs[i] != q) ++i;
                std::string_view val = attrs.substr(valStart, i - valStart);
                if (i < n) ++i;
                fn(name, val);
            }
        };
        auto isProperty = [](std::string_view name) {
            return name.rfind("xmlns", 0) != 0 && name.rfind("rdf:", 0) != 0 && name.rfind("xml:", 0) != 0;
        };
        auto localOf = [](std::string_view qname) {
            size_t colon = qname.find(':');
            return std::string(colon == std::string_view::npos ? qname : qname.substr(colon + 1));
        };
        auto qnameAt = [](std::string_view doc, size_t tagStart) {
            size_t b = tagStart + 1, e = b;
            while (e < doc.size() && X::IsNameChar(doc[e])) ++e;
            return doc.substr(b, e - b);
        };
        auto propertyValue = [&](std::string_view attrs, std::string_view body, bool selfClosing) {
            std::string value;
            auto append = [&](const std::string& piece, const char* sep) {
                if (piece.empty()) return;
                if (!value.empty()) value += sep;
                value += piece;
            };
            std::string_view resource = X::GetAttr(attrs, "resource");
            if (!resource.empty()) return X::DecodeText(resource);
            forEachAttr(attrs, [&](std::string_view name, std::string_view val) {
                if (isProperty(name)) append(localOf(name) + "=" + X::DecodeText(val), ", ");
            });
            if (selfClosing) return value;
            if (body.find('<') == std::string_view::npos) {
                append(std::string(X::TrimView(X::DecodeText(body))), ", ");
                return value;
            }
            size_t p = 0;
            X::XmlTag c;
            while (X::NextTag(body, p, c)) {
                if (c.isClosing) continue;
                std::string fields;
                forEachAttr(c.attrs, [&](std::string_view name, std::string_view val) {
                    if (!isProperty(name)) return;
                    if (!fields.empty()) fields += ", ";
                    fields += localOf(name) + "=" + X::DecodeText(val);
                });
                const bool container = c.localName == "Alt" || c.localName == "Seq" || c.localName == "Bag" || c.localName == "Description";
                std::string text;
                if (!c.isSelfClosing && !container) {
                    std::string_view inner;
                    size_t after = 0;
                    if (X::FindElementBody(body, c.afterTag, c.localName, inner, after) && inner.find('<') == std::string_view::npos) {
                        text = std::string(X::TrimView(X::DecodeText(inner)));
                        p = after; // a leaf
                    }
                }
                if (c.localName == "li") {
                    append(text.empty() ? fields : (fields.empty() ? text : text + " (" + fields + ")"), "; ");
                } else if (!container && !text.empty()) {
                    append(std::string(c.localName) + "=" + text, ", ");
                } else {
                    append(fields, ", ");
                }
            }
            return value;
        };
        std::string xmpXml = X::DocumentToUtf8(raw.data(), raw.size());
        X::ForEachElement(xmpXml, "Description", [&](const X::Element& d) {
            forEachAttr(d.attrs, [&](std::string_view name, std::string_view val) {
                if (isProperty(name) && sh.rows.size() <= 2000) {
                    size_t colon = name.find(':');
                    sh.rows.push_back({ PathUtils::Utf8ToWide(colon == std::string_view::npos ? std::string_view() : name.substr(0, colon)),
                                        PathUtils::Utf8ToWide(localOf(name)), PathUtils::Utf8ToWide(X::DecodeText(val)) });
                }
            });
            size_t p = 0;
            X::XmlTag c;
            while (X::NextTag(d.body, p, c) && sh.rows.size() <= 2000) {
                if (c.isClosing) continue;
                std::string_view qn = qnameAt(d.body, c.tagStart);
                std::string_view inner;
                if (!c.isSelfClosing) {
                    size_t after = 0;
                    if (!X::FindElementBody(d.body, c.afterTag, c.localName, inner, after)) break;
                    p = after;
                }
                size_t colon = qn.find(':');
                sh.rows.push_back({ PathUtils::Utf8ToWide(colon == std::string_view::npos ? std::string_view() : qn.substr(0, colon)),
                                    PathUtils::Utf8ToWide(localOf(qn)), PathUtils::Utf8ToWide(propertyValue(c.attrs, inner, c.isSelfClosing)) });
            }
            return true;
        });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Adobe XMP / Lightroom Camera Raw Sidecar (.xmp)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
