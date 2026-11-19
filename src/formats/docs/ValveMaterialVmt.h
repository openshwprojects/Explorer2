#pragma once
// ValveMaterialVmt.h — #64 Valve Source Engine Material (.vmt) KeyValues Shader/Texture/Proxy Inspector

namespace Formats {

inline bool ParseValveMaterialVmt(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    if (raw.size() < 8 || raw.size() > 8 * 1024 * 1024) return false;
    // Reject binary files with embedded NULs
    size_t textLen = raw.size();
    size_t startOff = 0;
    if (textLen >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) startOff = 3;
    for (size_t i = startOff; i < textLen; ++i) {
        if (raw[i] == 0) return false;
    }

    std::string_view src(reinterpret_cast<const char*>(raw.data() + startOff), textLen - startOff);
    size_t p = 0;

    auto skipWsAndComments = [&]() {
        while (p < src.size()) {
            char c = src[p];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++p;
                continue;
            }
            if (c == '/' && p + 1 < src.size() && src[p + 1] == '/') {
                p += 2;
                while (p < src.size() && src[p] != '\n') ++p;
                continue;
            }
            if (c == '/' && p + 1 < src.size() && src[p + 1] == '*') {
                p += 2;
                while (p + 1 < src.size() && !(src[p] == '*' && src[p + 1] == '/')) ++p;
                if (p + 1 < src.size()) p += 2;
                continue;
            }
            break;
        }
    };

    enum class KvTok { Eof, LBrace, RBrace, Str, Invalid };
    auto nextToken = [&](std::string& outVal) -> KvTok {
        outVal.clear();
        skipWsAndComments();
        if (p >= src.size()) return KvTok::Eof;
        char c = src[p];
        if (c == '{') { ++p; return KvTok::LBrace; }
        if (c == '}') { ++p; return KvTok::RBrace; }
        if (c == '"') {
            ++p;
            while (p < src.size()) {
                char ch = src[p++];
                if (ch == '"') return KvTok::Str;
                if (ch == '\\' && p < src.size() && (src[p] == '"' || src[p] == '\\')) {
                    outVal.push_back(src[p++]);
                } else {
                    outVal.push_back(ch);
                }
            }
            return KvTok::Invalid; // Unterminated quote
        }
        // Unquoted KeyValues token
        if (c == ';' || c == '=' || c == '<' || c == '>') return KvTok::Invalid;
        while (p < src.size()) {
            char ch = src[p];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '{' || ch == '}' || ch == '"') break;
            if (ch == '/' && p + 1 < src.size() && (src[p + 1] == '/' || src[p + 1] == '*')) break;
            outVal.push_back(ch);
            ++p;
        }
        return outVal.empty() ? KvTok::Invalid : KvTok::Str;
    };

    std::string shaderName;
    if (nextToken(shaderName) != KvTok::Str || shaderName.empty()) return false;
    // Shader name must look like an identifier (e.g. LightmappedGeneric, VertexLitGeneric, UnlitGeneric, Patch, Water, Cable)
    for (char c : shaderName) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }

    std::string dummy;
    if (nextToken(dummy) != KvTok::LBrace) return false;

    SpreadsheetSheet sheet;
    sheet.name = L"VMT Parameters";
    sheet.maxCols = 4;
    sheet.rows.push_back({ L"Section", L"Parameter", L"Value", L"Category" });

    auto classifyParam = [](std::string_view key, std::string_view section) -> std::wstring {
        if (!section.empty() && section != "root") return L"Material Proxy / Patch";
        std::string kl(key);
        for (char& ch : kl) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        if (kl.find("texture") != std::string::npos || kl.find("bumpmap") != std::string::npos ||
            kl.find("envmap") != std::string::npos || kl.find("normalmap") != std::string::npos ||
            kl.find("detail") != std::string::npos || kl.find("phongexponenttexture") != std::string::npos) {
            return L"Texture Slot";
        }
        if (kl.find("surfaceprop") != std::string::npos || kl.find("%compile") != std::string::npos) {
            return L"Surface / Physics";
        }
        if (kl.find("alpha") != std::string::npos || kl.find("translucent") != std::string::npos ||
            kl.find("additive") != std::string::npos || kl.find("nocull") != std::string::npos) {
            return L"Blend / Alpha";
        }
        if (kl.find("phong") != std::string::npos || kl.find("rimlight") != std::string::npos ||
            kl.find("selfillum") != std::string::npos || kl.find("color") != std::string::npos) {
            return L"Shading / Lighting";
        }
        return L"Shader Parameter";
    };

    size_t paramCount = 0;
    auto parseBlock = [&](auto& self, const std::string& sectionPath, int depth) -> bool {
        if (depth > 16) return false;
        for (;;) {
            std::string key;
            KvTok kt = nextToken(key);
            if (kt == KvTok::RBrace) return true;
            if (kt != KvTok::Str || key.empty()) return false;

            std::string valOrNext;
            size_t saveP = p;
            KvTok vt = nextToken(valOrNext);
            if (vt == KvTok::LBrace) {
                std::string subSec = (sectionPath == "root") ? key : (sectionPath + "/" + key);
                if (!self(self, subSec, depth + 1)) return false;
            } else if (vt == KvTok::Str) {
                // Optional conditional tag like [$WIN32] or [?$LDR] on the same line
                size_t condSave = p;
                skipWsAndComments();
                std::string condSuffix;
                if (p < src.size() && src[p] == '[') {
                    std::string ctok;
                    if (nextToken(ctok) == KvTok::Str) {
                        condSuffix = " " + ctok;
                    } else {
                        p = condSave;
                    }
                } else {
                    p = condSave;
                }
                sheet.rows.push_back({
                    PathUtils::Utf8ToWide(sectionPath == "root" ? shaderName : sectionPath),
                    PathUtils::Utf8ToWide(key),
                    PathUtils::Utf8ToWide(valOrNext + condSuffix),
                    classifyParam(key, sectionPath)
                });
                ++paramCount;
            } else {
                p = saveP;
                return false;
            }
        }
    };

    if (!parseBlock(parseBlock, "root", 0) || paramCount == 0) {
        return false;
    }

    outDoc.formatTitle = L"Valve Source Material (.vmt — " + PathUtils::Utf8ToWide(shaderName) + L")";
    outDoc.kind = OfficeFileKind::CsvTable;
    outDoc.isSpreadsheet = true;
    outDoc.sheets.push_back(std::move(sheet));
    outDoc.valid = true;
    return true;
}

} // namespace Formats
