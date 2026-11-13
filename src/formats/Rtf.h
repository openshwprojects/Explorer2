#pragma once

static bool ParseRtfDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::RichTextRtf;
    out.isSpreadsheet = false;
    out.formatTitle = L"Rich Text Document (.rtf)";

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::string curLine;
    int braceDepth = 0;
    int skipDepth = -1;

    auto flushPara = [&]() {
        if (!curLine.empty()) {
            std::wstring w = PathUtils::Utf8ToWide(curLine);
            out.charCount += w.size();
            bool inW = false;
            for (wchar_t c : w) {
                if (c > L' ') { if (!inW) { ++out.wordCount; inW = true; } }
                else inW = false;
            }
            out.paragraphs.push_back({ std::move(w), 0, false, false });
            curLine.clear();
        }
    };

    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == '{') {
            ++braceDepth;
            if (i + 2 < sv.size() && sv[i + 1] == '\\' && sv[i + 2] == '*') {
                if (skipDepth < 0) skipDepth = braceDepth;
            }
            continue;
        }
        if (c == '}') {
            if (braceDepth == skipDepth) skipDepth = -1;
            --braceDepth;
            continue;
        }
        if (skipDepth >= 0) continue;

        if (c == '\\') {
            if (i + 1 < sv.size() && (sv[i + 1] == '\\' || sv[i + 1] == '{' || sv[i + 1] == '}')) {
                curLine.push_back(sv[++i]);
                continue;
            }
            size_t kwStart = ++i;
            while (i < sv.size() && ((sv[i] >= 'a' && sv[i] <= 'z') || (sv[i] >= 'A' && sv[i] <= 'Z'))) ++i;
            std::string_view kw = sv.substr(kwStart, i - kwStart);
            while (i < sv.size() && ((sv[i] >= '0' && sv[i] <= '9') || sv[i] == '-')) ++i;
            if (i < sv.size() && sv[i] == ' ') ++i;
            --i;

            if (kw == "fonttbl" || kw == "colortbl" || kw == "stylesheet" || kw == "info" || kw == "pict") {
                skipDepth = braceDepth;
            } else if (kw == "par" || kw == "line") {
                flushPara();
            } else if (kw == "tab") {
                curLine += "    ";
            }
            continue;
        }
        if (c != '\r' && c != '\n') {
            curLine.push_back(c);
        }
    }
    flushPara();
    out.valid = !out.paragraphs.empty();
    return out.valid;
}

static void AddDocParagraph(ParsedOfficeDocument& out, std::wstring text, int headingLevel = 0, bool isBullet = false) {
    // Trim leading/trailing whitespace
    size_t first = 0;
    while (first < text.size() && (text[first] == L' ' || text[first] == L'\t' || text[first] == L'\r' || text[first] == L'\n')) ++first;
    size_t last = text.size();
    while (last > first && (text[last - 1] == L' ' || text[last - 1] == L'\t' || text[last - 1] == L'\r' || text[last - 1] == L'\n')) --last;
    if (first >= last) return;
    std::wstring clean = text.substr(first, last - first);
    out.charCount += clean.size();
    bool inW = false;
    for (wchar_t ch : clean) {
        if (ch > L' ') { if (!inW) { ++out.wordCount; inW = true; } }
        else inW = false;
    }
    out.paragraphs.push_back({ std::move(clean), headingLevel, isBullet, false });
}

static std::wstring StripXmlHtmlTags(std::string_view sv) {
    std::string plain;
    plain.reserve(sv.size());
    bool inTag = false;
    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == '<') {
            inTag = true;
            if (i + 3 < sv.size() && (sv.substr(i, 3) == "<br" || sv.substr(i, 3) == "<BR")) {
                plain.push_back(' ');
            }
            continue;
        }
        if (c == '>') {
            inTag = false;
            continue;
        }
        if (!inTag) {
            if (c == '\r' || c == '\n' || c == '\t') plain.push_back(' ');
            else plain.push_back(c);
        }
    }
    return DecodeXmlEntities(PathUtils::Utf8ToWide(plain));
}
