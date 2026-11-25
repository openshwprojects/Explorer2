#pragma once
// CmsisSvd.h — ARM CMSIS System View Description (.svd) MCU Architecture, Bus Matrix, Interrupt & Bitfield Register-Map Parser

namespace Formats {

namespace DetailCmsisSvd {

struct XmlNode {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;
    std::vector<XmlNode> children;

    std::string GetAttr(std::string_view keyLower) const {
        for (const auto& kv : attrs) {
            if (kv.first.size() == keyLower.size()) {
                bool match = true;
                for (size_t i = 0; i < keyLower.size(); ++i) {
                    char a = kv.first[i];
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                    if (a != keyLower[i]) { match = false; break; }
                }
                if (match) return kv.second;
            }
        }
        return {};
    }

    const XmlNode* FindChild(std::string_view nameLower) const {
        for (const auto& ch : children) {
            if (ch.tag == nameLower) return &ch;
        }
        return nullptr;
    }

    std::string ChildText(std::string_view nameLower) const {
        const XmlNode* c = FindChild(nameLower);
        return c ? c->text : std::string{};
    }
};

inline std::string TrimSvdAscii(std::string_view sv) {
    while (!sv.empty() && static_cast<unsigned char>(sv.front()) <= 0x20u) sv.remove_prefix(1);
    while (!sv.empty() && static_cast<unsigned char>(sv.back()) <= 0x20u) sv.remove_suffix(1);
    return std::string(sv);
}

inline std::string LowerSvdTag(std::string_view raw) {
    // Strip XML namespace prefix if present (e.g. "svd:device" -> "device")
    size_t colon = raw.find(':');
    if (colon != std::string_view::npos && colon + 1 < raw.size()) {
        raw.remove_prefix(colon + 1);
    }
    return ::Explorer2::PathUtils::ToLowerAscii(raw);
}

inline std::string UnescapeXmlEntities(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '&') {
            size_t semi = sv.find(';', i + 1);
            if (semi != std::string_view::npos && semi - i <= 10) {
                std::string_view ent = sv.substr(i + 1, semi - i - 1);
                if (ent == "lt")        { out.push_back('<');  i = semi; continue; }
                if (ent == "gt")        { out.push_back('>');  i = semi; continue; }
                if (ent == "amp")       { out.push_back('&');  i = semi; continue; }
                if (ent == "quot")      { out.push_back('"');  i = semi; continue; }
                if (ent == "apos")      { out.push_back('\''); i = semi; continue; }
                if (!ent.empty() && ent[0] == '#') {
                    uint32_t cp = 0;
                    if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                        cp = static_cast<uint32_t>(std::strtoul(std::string(ent.substr(2)).c_str(), nullptr, 16));
                    } else {
                        cp = static_cast<uint32_t>(std::strtoul(std::string(ent.substr(1)).c_str(), nullptr, 10));
                    }
                    if (cp >= 0x20u && cp < 0x7Fu) {
                        out.push_back(static_cast<char>(cp));
                        i = semi;
                        continue;
                    }
                }
            }
        }
        out.push_back(sv[i]);
    }
    return TrimSvdAscii(out);
}

inline bool DecodeSvdXmlText(std::string_view rawBytes, std::string& outUtf8) {
    outUtf8.clear();
    if (rawBytes.size() < 16 || rawBytes.size() > 64 * 1024 * 1024) return false;
    const auto* b = reinterpret_cast<const uint8_t*>(rawBytes.data());
    const size_t n = rawBytes.size();

    // UTF-16LE / UTF-16BE BOM check (portable via ReaderText::DecodeUtf16Span)
    if (n >= 4 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        const bool be = (b[0] == 0xFE);
        const size_t nCu = (n - 2) / 2;
        for (size_t i = 0; i < nCu; ++i) {
            const uint16_t cu = be ? ByteIO::RdU16BE(b + 2 + i * 2) : ByteIO::RdU16LE(b + 2 + i * 2);
            if (cu == 0) return false;
        }
        outUtf8 = PathUtils::WideToUtf8(ReaderText::DecodeUtf16Span(b + 2, nCu, be));
        return !outUtf8.empty();
    }

    size_t start = 0;
    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) start = 3;
    for (size_t i = start; i < n; ++i) {
        if (b[i] == 0) return false; // Reject binary files
    }
    outUtf8.assign(reinterpret_cast<const char*>(b + start), n - start);
    return true;
}

inline bool ParseSvdXmlTree(std::string_view xml, XmlNode& outRoot) {
    outRoot = XmlNode{};
    std::vector<XmlNode*> stack;
    stack.push_back(&outRoot);

    size_t pos = 0;
    size_t totalNodes = 0;
    constexpr size_t kMaxNodes = 300000;
    constexpr size_t kMaxDepth = 32;

    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) {
            if (stack.size() > 1 && stack.back()->children.empty()) {
                stack.back()->text += UnescapeXmlEntities(xml.substr(pos));
            }
            break;
        }
        if (lt > pos && stack.size() > 1) {
            std::string chunk = UnescapeXmlEntities(xml.substr(pos, lt - pos));
            if (!chunk.empty()) {
                if (!stack.back()->text.empty()) stack.back()->text.push_back(' ');
                stack.back()->text += chunk;
            }
        }

        if (lt + 4 <= xml.size() && xml.substr(lt, 4) == "<!--") {
            size_t endCmt = xml.find("-->", lt + 4);
            if (endCmt == std::string_view::npos) return false; // Unterminated XML comment
            pos = endCmt + 3;
            continue;
        }
        if (lt + 9 <= xml.size() && xml.substr(lt, 9) == "<![CDATA[") {
            size_t endCd = xml.find("]]>", lt + 9);
            if (endCd == std::string_view::npos) return false;
            if (stack.size() > 1) {
                std::string cd = TrimSvdAscii(xml.substr(lt + 9, endCd - (lt + 9)));
                if (!cd.empty()) stack.back()->text += cd;
            }
            pos = endCd + 3;
            continue;
        }
        if (lt + 2 <= xml.size() && (xml[lt + 1] == '?' || xml[lt + 1] == '!')) {
            size_t gt = xml.find('>', lt + 2);
            if (gt == std::string_view::npos) return false;
            pos = gt + 1;
            continue;
        }

        // Find closing '>' respecting quoted attribute strings
        size_t gt = lt + 1;
        char inQuote = 0;
        while (gt < xml.size()) {
            char c = xml[gt];
            if (inQuote) {
                if (c == inQuote) inQuote = 0;
            } else {
                if (c == '"' || c == '\'') inQuote = c;
                else if (c == '>') break;
            }
            ++gt;
        }
        if (gt >= xml.size()) return false; // Unterminated tag

        std::string_view tagBody = xml.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        while (!tagBody.empty() && static_cast<unsigned char>(tagBody.front()) <= 0x20u) tagBody.remove_prefix(1);
        while (!tagBody.empty() && static_cast<unsigned char>(tagBody.back()) <= 0x20u) tagBody.remove_suffix(1);
        if (tagBody.empty()) continue;

        if (tagBody.front() == '/') {
            tagBody.remove_prefix(1);
            std::string closeName = LowerSvdTag(TrimSvdAscii(tagBody));
            if (stack.size() <= 1 || stack.back()->tag != closeName) {
                return false; // Mismatched XML closing tag
            }
            stack.pop_back();
            continue;
        }

        bool selfClosing = false;
        if (tagBody.back() == '/') {
            selfClosing = true;
            tagBody.remove_suffix(1);
            while (!tagBody.empty() && static_cast<unsigned char>(tagBody.back()) <= 0x20u) tagBody.remove_suffix(1);
        }

        size_t nameEnd = 0;
        while (nameEnd < tagBody.size() && static_cast<unsigned char>(tagBody[nameEnd]) > 0x20u && tagBody[nameEnd] != '=') {
            ++nameEnd;
        }
        if (nameEnd == 0) return false;
        if (++totalNodes > kMaxNodes || stack.size() >= kMaxDepth) return false;

        XmlNode node;
        node.tag = LowerSvdTag(tagBody.substr(0, nameEnd));

        // Parse attributes
        size_t ap = nameEnd;
        while (ap < tagBody.size()) {
            while (ap < tagBody.size() && static_cast<unsigned char>(tagBody[ap]) <= 0x20u) ++ap;
            if (ap >= tagBody.size()) break;
            size_t eq = tagBody.find('=', ap);
            if (eq == std::string_view::npos) break;
            std::string attrKey = TrimSvdAscii(tagBody.substr(ap, eq - ap));
            ap = eq + 1;
            while (ap < tagBody.size() && static_cast<unsigned char>(tagBody[ap]) <= 0x20u) ++ap;
            if (ap >= tagBody.size() || (tagBody[ap] != '"' && tagBody[ap] != '\'')) break;
            char q = tagBody[ap++];
            size_t qe = tagBody.find(q, ap);
            if (qe == std::string_view::npos) return false;
            node.attrs.push_back({ std::move(attrKey), UnescapeXmlEntities(tagBody.substr(ap, qe - ap)) });
            ap = qe + 1;
        }

        stack.back()->children.push_back(std::move(node));
        if (!selfClosing) {
            stack.push_back(&stack.back()->children.back());
        }
    }

    return stack.size() == 1 && !outRoot.children.empty();
}

// Parse CMSIS-SVD scaledNonNegativeInteger: hex (0x...), binary (#...), decimal, with optional K/M/G/T multiplier
inline bool ParseSvdUint64(std::string_view sv, uint64_t& outVal) {
    std::string s = TrimSvdAscii(sv);
    if (s.empty()) return false;
    uint64_t mult = 1ULL;
    char last = s.back();
    if (s.rfind("0x", 0) != 0 && s.rfind("0X", 0) != 0) {
        if (last == 'k' || last == 'K')      { mult = 1024ULL; s.pop_back(); }
        else if (last == 'm' || last == 'M') { mult = 1024ULL * 1024ULL; s.pop_back(); }
        else if (last == 'g' || last == 'G') { mult = 1024ULL * 1024ULL * 1024ULL; s.pop_back(); }
    }
    if (s.empty()) return false;
    if (s[0] == '#') {
        uint64_t v = 0;
        for (size_t i = 1; i < s.size(); ++i) {
            char c = s[i];
            if (c == '0' || c == '1') v = (v << 1) | static_cast<uint64_t>(c - '0');
            else if (c == 'x' || c == 'X') v <<= 1;
            else return false;
        }
        outVal = v * mult;
        return true;
    }
    char* endp = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &endp, 0);
    if (!endp || endp == s.c_str()) return false;
    outVal = static_cast<uint64_t>(v) * mult;
    return true;
}

inline bool IsSvdTrue(std::string_view sv) {
    std::string s = LowerSvdTag(TrimSvdAscii(sv));
    return s == "true" || s == "1";
}

inline std::vector<std::string> ExpandSvdDimIndices(uint32_t dim, std::string_view dimIdxSv) {
    std::vector<std::string> out;
    if (dim == 0) return out;
    dim = std::min<uint32_t>(dim, 32u);
    std::string spec = TrimSvdAscii(dimIdxSv);
    if (!spec.empty()) {
        if (spec.find(',') != std::string::npos) {
            size_t p = 0;
            while (p < spec.size() && out.size() < dim) {
                size_t cm = spec.find(',', p);
                if (cm == std::string::npos) cm = spec.size();
                out.push_back(TrimSvdAscii(std::string_view(spec).substr(p, cm - p)));
                p = cm + 1;
            }
        } else {
            size_t dash = spec.find('-');
            if (dash != std::string::npos && dash > 0 && dash + 1 < spec.size()) {
                std::string lhs = TrimSvdAscii(std::string_view(spec).substr(0, dash));
                std::string rhs = TrimSvdAscii(std::string_view(spec).substr(dash + 1));
                if (lhs.size() == 1 && rhs.size() == 1 && std::isalpha(static_cast<unsigned char>(lhs[0])) && std::isalpha(static_cast<unsigned char>(rhs[0]))) {
                    for (char c = lhs[0]; c <= rhs[0] && out.size() < dim; ++c) {
                        out.emplace_back(1, c);
                    }
                } else {
                    int a = std::atoi(lhs.c_str());
                    int b = std::atoi(rhs.c_str());
                    for (int k = a; k <= b && out.size() < dim; ++k) {
                        out.push_back(std::to_string(k));
                    }
                }
            }
        }
    }
    while (out.size() < dim) {
        out.push_back(std::to_string(out.size()));
    }
    return out;
}

inline std::string SubstSvdDimName(const std::string& tmpl, const std::string& idxStr) {
    std::string res;
    size_t pct = tmpl.find("%s");
    if (pct != std::string::npos) {
        res = tmpl.substr(0, pct) + idxStr + tmpl.substr(pct + 2);
    } else if (!tmpl.empty() && tmpl.substr(tmpl.size() >= 4 ? tmpl.size() - 4 : 0) == "[%s]") {
        res = tmpl.substr(0, tmpl.size() - 4) + "[" + idxStr + "]";
    } else {
        res = tmpl + idxStr;
    }
    return res;
}

struct SvdField {
    std::string name;
    uint32_t lsb = 0;
    uint32_t msb = 0;
    std::string access; // R, W, RW
    uint32_t enumCount = 0;
};

struct SvdRegister {
    std::string name;
    std::string derivedFrom;
    uint64_t offset = 0;
    uint32_t sizeBits = 32;
    uint64_t resetValue = 0;
    bool hasReset = false;
    std::string access;
    std::vector<SvdField> fields;
};

struct SvdInterrupt {
    std::string name;
    int32_t value = 0;
};

struct SvdAddrBlock {
    uint64_t offset = 0;
    uint64_t size = 0;
    std::string usage;
};

struct SvdPeripheral {
    std::string name;
    std::string derivedFrom;
    std::string groupName;
    std::string prependToName;
    std::string appendToName;
    uint64_t baseAddress = 0;
    bool hasBaseAddress = false;
    uint32_t defaultSizeBits = 32;
    std::vector<SvdAddrBlock> addrBlocks;
    std::vector<SvdInterrupt> interrupts;
    std::vector<SvdRegister> registers;
};

inline std::string AbbrevSvdAccess(std::string_view acc) {
    std::string a = LowerSvdTag(TrimSvdAscii(acc));
    if (a == "read-only") return "RO";
    if (a == "write-only" || a == "writeonce") return "WO";
    if (a == "read-write" || a == "read-writeonce") return "RW";
    return {};
}

inline SvdField ParseSvdFieldNode(const XmlNode& fn) {
    SvdField f;
    f.name = fn.ChildText("name");
    f.access = AbbrevSvdAccess(fn.ChildText("access"));

    uint64_t bitOff = 0, bitW = 1, lsb = 0, msb = 0;
    std::string bitRange = fn.ChildText("bitrange");
    if (!bitRange.empty()) {
        // Format: [msb:lsb]
        size_t lb = bitRange.find('[');
        size_t cl = bitRange.find(':');
        size_t rb = bitRange.find(']');
        if (cl != std::string::npos) {
            size_t s0 = (lb != std::string::npos) ? (lb + 1) : 0;
            size_t s1 = (rb != std::string::npos) ? rb : bitRange.size();
            uint64_t m = 0, l = 0;
            if (ParseSvdUint64(std::string_view(bitRange).substr(s0, cl - s0), m) &&
                ParseSvdUint64(std::string_view(bitRange).substr(cl + 1, s1 - cl - 1), l)) {
                f.lsb = static_cast<uint32_t>(std::min(m, l));
                f.msb = static_cast<uint32_t>(std::max(m, l));
            }
        }
    } else if (ParseSvdUint64(fn.ChildText("bitoffset"), bitOff)) {
        if (!ParseSvdUint64(fn.ChildText("bitwidth"), bitW) || bitW == 0) bitW = 1;
        f.lsb = static_cast<uint32_t>(bitOff);
        f.msb = static_cast<uint32_t>(bitOff + bitW - 1ULL);
    } else if (ParseSvdUint64(fn.ChildText("lsb"), lsb) && ParseSvdUint64(fn.ChildText("msb"), msb)) {
        f.lsb = static_cast<uint32_t>(std::min(lsb, msb));
        f.msb = static_cast<uint32_t>(std::max(lsb, msb));
    }

    if (const XmlNode* evs = fn.FindChild("enumeratedvalues")) {
        for (const auto& ch : evs->children) {
            if (ch.tag == "enumeratedvalue") ++f.enumCount;
        }
    }
    return f;
}

inline void ParseSvdRegistersContainer(
    const XmlNode& container,
    uint64_t baseOffset,
    const std::string& namePrefix,
    uint32_t inheritedBits,
    std::vector<SvdRegister>& outRegs)
{
    for (const auto& ch : container.children) {
        if (outRegs.size() >= 512) break;
        if (ch.tag == "register") {
            uint64_t off = 0;
            ParseSvdUint64(ch.ChildText("addressoffset"), off);
            uint64_t szBits = inheritedBits;
            if (!ParseSvdUint64(ch.ChildText("size"), szBits) || szBits == 0) szBits = inheritedBits;

            uint64_t rst = 0;
            bool hasRst = ParseSvdUint64(ch.ChildText("resetvalue"), rst);
            std::string acc = AbbrevSvdAccess(ch.ChildText("access"));
            std::string rawName = ch.ChildText("name");
            if (rawName.empty()) continue;

            std::vector<SvdField> fields;
            if (const XmlNode* flds = ch.FindChild("fields")) {
                for (const auto& fn : flds->children) {
                    if (fn.tag == "field" && fields.size() < 64) {
                        uint64_t fDim = 0, fInc = 1;
                        if (ParseSvdUint64(fn.ChildText("dim"), fDim) && fDim >= 1) {
                            if (!ParseSvdUint64(fn.ChildText("dimincrement"), fInc) || fInc == 0) fInc = 1;
                            SvdField baseF = ParseSvdFieldNode(fn);
                            auto idxs = ExpandSvdDimIndices(static_cast<uint32_t>(fDim), fn.ChildText("dimindex"));
                            uint32_t w = (baseF.msb >= baseF.lsb) ? (baseF.msb - baseF.lsb + 1u) : 1u;
                            for (size_t di = 0; di < idxs.size() && fields.size() < 64; ++di) {
                                SvdField instF = baseF;
                                instF.name = SubstSvdDimName(baseF.name, idxs[di]);
                                instF.lsb = baseF.lsb + static_cast<uint32_t>(di * fInc);
                                instF.msb = instF.lsb + w - 1u;
                                if (!instF.name.empty()) fields.push_back(std::move(instF));
                            }
                        } else {
                            SvdField sf = ParseSvdFieldNode(fn);
                            if (!sf.name.empty()) fields.push_back(std::move(sf));
                        }
                    }
                }
            }
            std::sort(fields.begin(), fields.end(), [](const SvdField& a, const SvdField& b) {
                return a.lsb < b.lsb;
            });

            std::string deriv = ch.GetAttr("derivedfrom");
            uint64_t rDim = 0, rInc = (szBits + 7ULL) / 8ULL;
            if (ParseSvdUint64(ch.ChildText("dim"), rDim) && rDim >= 1) {
                ParseSvdUint64(ch.ChildText("dimincrement"), rInc);
                auto idxs = ExpandSvdDimIndices(static_cast<uint32_t>(rDim), ch.ChildText("dimindex"));
                for (size_t di = 0; di < idxs.size() && outRegs.size() < 512; ++di) {
                    SvdRegister sr;
                    sr.name = namePrefix + SubstSvdDimName(rawName, idxs[di]);
                    sr.derivedFrom = deriv;
                    sr.offset = baseOffset + off + di * rInc;
                    sr.sizeBits = static_cast<uint32_t>(std::clamp<uint64_t>(szBits, 8, 64));
                    sr.resetValue = rst;
                    sr.hasReset = hasRst;
                    sr.access = acc;
                    sr.fields = fields;
                    outRegs.push_back(std::move(sr));
                }
            } else {
                SvdRegister sr;
                sr.name = namePrefix + rawName;
                sr.derivedFrom = deriv;
                sr.offset = baseOffset + off;
                sr.sizeBits = static_cast<uint32_t>(std::clamp<uint64_t>(szBits, 8, 64));
                sr.resetValue = rst;
                sr.hasReset = hasRst;
                sr.access = acc;
                sr.fields = std::move(fields);
                outRegs.push_back(std::move(sr));
            }
        } else if (ch.tag == "cluster") {
            uint64_t cOff = 0;
            ParseSvdUint64(ch.ChildText("addressoffset"), cOff);
            std::string cName = ch.ChildText("name");
            uint64_t cBits = inheritedBits;
            if (!ParseSvdUint64(ch.ChildText("size"), cBits) || cBits == 0) cBits = inheritedBits;

            uint64_t cDim = 0, cInc = 4;
            if (ParseSvdUint64(ch.ChildText("dim"), cDim) && cDim >= 1) {
                ParseSvdUint64(ch.ChildText("dimincrement"), cInc);
                auto idxs = ExpandSvdDimIndices(static_cast<uint32_t>(cDim), ch.ChildText("dimindex"));
                for (size_t di = 0; di < idxs.size() && outRegs.size() < 512; ++di) {
                    std::string subPfx = namePrefix + (cName.empty() ? "" : (SubstSvdDimName(cName, idxs[di]) + "."));
                    ParseSvdRegistersContainer(ch, baseOffset + cOff + di * cInc, subPfx, static_cast<uint32_t>(cBits), outRegs);
                }
            } else {
                std::string subPfx = namePrefix + (cName.empty() ? "" : (cName + "."));
                ParseSvdRegistersContainer(ch, baseOffset + cOff, subPfx, static_cast<uint32_t>(cBits), outRegs);
            }
        }
    }
}

inline void DrawBoxOutlineSvd(ParsedEdaBoard& out, float x0, float y0, float x1, float y1, float w, EdaLayerId lid, int netId = 0) {
    out.traces.push_back({ x0, y0, x1, y0, w, lid, netId });
    out.traces.push_back({ x1, y0, x1, y1, w, lid, netId });
    out.traces.push_back({ x1, y1, x0, y1, w, lid, netId });
    out.traces.push_back({ x0, y1, x0, y0, w, lid, netId });
}

inline std::wstring FormatHexAddr32(uint64_t addr) {
    wchar_t buf[32] = {};
    if (addr > 0xFFFFFFFFULL) {
        swprintf_s(buf, L"0x%llX", static_cast<unsigned long long>(addr));
    } else {
        swprintf_s(buf, L"0x%08X", static_cast<unsigned>(addr));
    }
    return buf;
}

} // namespace DetailCmsisSvd

inline bool ParseCmsisSvdBoard(std::string_view sv, ParsedEdaBoard& out) {
    using namespace DetailCmsisSvd;

    std::string utf8;
    if (!DecodeSvdXmlText(sv, utf8)) return false;
    if (utf8.find('<') == std::string::npos) return false;

    XmlNode treeRoot;
    if (!ParseSvdXmlTree(utf8, treeRoot)) return false;

    const XmlNode* devNode = treeRoot.FindChild("device");
    if (!devNode) return false;

    std::string devName = devNode->ChildText("name");
    std::string vendor  = devNode->ChildText("vendor");
    if (vendor.empty()) vendor = devNode->ChildText("vendorid");
    std::string version = devNode->ChildText("version");

    uint64_t devBusWidth = 32;
    if (!ParseSvdUint64(devNode->ChildText("width"), devBusWidth) || devBusWidth == 0) {
        if (!ParseSvdUint64(devNode->ChildText("size"), devBusWidth) || devBusWidth == 0) {
            devBusWidth = 32;
        }
    }

    // Parse optional <cpu> descriptor
    bool hasCpu = false;
    std::string cpuName, cpuRev, cpuEndian;
    bool fpuPresent = false, mpuPresent = false, icache = false, dcache = false;
    uint64_t nvicBits = 0;
    if (const XmlNode* cpu = devNode->FindChild("cpu")) {
        hasCpu = true;
        cpuName    = cpu->ChildText("name");
        cpuRev     = cpu->ChildText("revision");
        cpuEndian  = cpu->ChildText("endian");
        fpuPresent = IsSvdTrue(cpu->ChildText("fpupresent"));
        mpuPresent = IsSvdTrue(cpu->ChildText("mpupresent"));
        icache     = IsSvdTrue(cpu->ChildText("icachepresent"));
        dcache     = IsSvdTrue(cpu->ChildText("dcachepresent"));
        ParseSvdUint64(cpu->ChildText("nvicpriobits"), nvicBits);
    }

    // Parse <peripherals>
    const XmlNode* periphsNode = devNode->FindChild("peripherals");
    if (!periphsNode) return false;

    std::vector<SvdPeripheral> rawPeriphs;
    for (const auto& pn : periphsNode->children) {
        if (pn.tag != "peripheral" || rawPeriphs.size() >= 256) continue;
        std::string pname = pn.ChildText("name");
        if (pname.empty()) continue;

        SvdPeripheral sp;
        sp.name = pname;
        sp.derivedFrom = pn.GetAttr("derivedfrom");
        sp.groupName = pn.ChildText("groupname");
        sp.prependToName = pn.ChildText("prependtoname");
        sp.appendToName = pn.ChildText("appendtoname");
        sp.hasBaseAddress = ParseSvdUint64(pn.ChildText("baseaddress"), sp.baseAddress);

        uint64_t pSizeBits = devBusWidth;
        if (ParseSvdUint64(pn.ChildText("size"), pSizeBits) && pSizeBits > 0) {
            sp.defaultSizeBits = static_cast<uint32_t>(std::clamp<uint64_t>(pSizeBits, 8, 64));
        } else {
            sp.defaultSizeBits = static_cast<uint32_t>(std::clamp<uint64_t>(devBusWidth, 8, 64));
        }

        for (const auto& sub : pn.children) {
            if (sub.tag == "addressblock") {
                SvdAddrBlock ab;
                ParseSvdUint64(sub.ChildText("offset"), ab.offset);
                ParseSvdUint64(sub.ChildText("size"), ab.size);
                ab.usage = sub.ChildText("usage");
                sp.addrBlocks.push_back(std::move(ab));
            } else if (sub.tag == "interrupt") {
                std::string iname = sub.ChildText("name");
                if (!iname.empty()) {
                    int32_t iv = static_cast<int32_t>(std::strtol(sub.ChildText("value").c_str(), nullptr, 0));
                    sp.interrupts.push_back({ std::move(iname), iv });
                }
            } else if (sub.tag == "registers") {
                ParseSvdRegistersContainer(sub, 0, "", sp.defaultSizeBits, sp.registers);
            }
        }

        // Expand peripheral-level <dim> array if present (e.g. GPIO%s -> GPIOA, GPIOB, GPIOC)
        uint64_t pDim = 0, pInc = 0x400;
        if (ParseSvdUint64(pn.ChildText("dim"), pDim) && pDim >= 1) {
            ParseSvdUint64(pn.ChildText("dimincrement"), pInc);
            auto idxs = ExpandSvdDimIndices(static_cast<uint32_t>(pDim), pn.ChildText("dimindex"));
            for (size_t di = 0; di < idxs.size() && rawPeriphs.size() < 256; ++di) {
                SvdPeripheral inst = sp;
                inst.name = SubstSvdDimName(sp.name, idxs[di]);
                inst.baseAddress = sp.baseAddress + di * pInc;
                rawPeriphs.push_back(std::move(inst));
            }
        } else {
            rawPeriphs.push_back(std::move(sp));
        }
    }

    if (rawPeriphs.empty()) return false;

    // Resolve peripheral-level derivedFrom="<BasePeriphName>" inheritance (including chained A <- B <- C)
    std::unordered_map<std::string, size_t> periphIdxByName;
    for (size_t i = 0; i < rawPeriphs.size(); ++i) {
        periphIdxByName[rawPeriphs[i].name] = i;
    }

    for (size_t pass = 0; pass < 3; ++pass) {
        for (auto& sp : rawPeriphs) {
            if (sp.derivedFrom.empty()) continue;
            auto it = periphIdxByName.find(sp.derivedFrom);
            if (it == periphIdxByName.end() || it->second >= rawPeriphs.size()) continue;
            const SvdPeripheral& base = rawPeriphs[it->second];
            if (!base.derivedFrom.empty() && pass < 2) continue; // Wait for parent to resolve first

            if (sp.groupName.empty()) sp.groupName = base.groupName;
            if (sp.addrBlocks.empty()) sp.addrBlocks = base.addrBlocks;
            if (sp.defaultSizeBits == 32 && base.defaultSizeBits != 32) sp.defaultSizeBits = base.defaultSizeBits;

            if (sp.registers.empty()) {
                sp.registers = base.registers;
            } else if (!base.registers.empty()) {
                // Merge base registers that were not overridden by name/offset
                std::set<std::string> ownNames;
                for (const auto& r : sp.registers) ownNames.insert(r.name);
                for (const auto& br : base.registers) {
                    if (!ownNames.count(br.name) && sp.registers.size() < 512) {
                        sp.registers.push_back(br);
                    }
                }
            }
            sp.derivedFrom.clear();
        }
    }

    // Resolve register-level derivedFrom within each peripheral and sort registers by addressOffset
    size_t totalRegCount = 0;
    size_t totalFieldCount = 0;
    std::set<int32_t> uniqueIrqs;
    for (auto& sp : rawPeriphs) {
        for (size_t i = 0; i < sp.registers.size(); ++i) {
            auto& r = sp.registers[i];
            if (!r.derivedFrom.empty() && r.fields.empty()) {
                for (const auto& cand : sp.registers) {
                    if (cand.name == r.derivedFrom && !cand.fields.empty()) {
                        r.fields = cand.fields;
                        r.sizeBits = cand.sizeBits;
                        break;
                    }
                }
            }
        }
        std::sort(sp.registers.begin(), sp.registers.end(), [](const SvdRegister& a, const SvdRegister& b) {
            return (a.offset != b.offset) ? (a.offset < b.offset) : (a.name < b.name);
        });
        totalRegCount += sp.registers.size();
        for (const auto& r : sp.registers) totalFieldCount += r.fields.size();
        for (const auto& irq : sp.interrupts) uniqueIrqs.insert(irq.value);
    }

    // Sort peripherals by baseAddress ascending for an authentic memory-map layout
    std::stable_sort(rawPeriphs.begin(), rawPeriphs.end(), [](const SvdPeripheral& a, const SvdPeripheral& b) {
        if (a.hasBaseAddress != b.hasBaseAddress) return a.hasBaseAddress > b.hasBaseAddress;
        return a.baseAddress < b.baseAddress;
    });

    out.kind = EdaDocKind::KiCadSch;

    std::wstring title = L"ARM CMSIS-SVD MCU Architecture (";
    if (!devName.empty()) {
        if (!vendor.empty() && devName.find(vendor) == std::string::npos && vendor.size() <= 16) {
            title += PathUtils::Utf8ToWide(vendor) + L" ";
        }
        title += PathUtils::Utf8ToWide(devName);
    } else {
        title += L".svd";
    }
    if (!cpuName.empty()) {
        title += L", " + PathUtils::Utf8ToWide(cpuName);
        if (fpuPresent) title += L"+FPU";
    }
    title += L" — " + std::to_wstring(rawPeriphs.size()) + L" periph, " + std::to_wstring(totalRegCount) + L" regs";
    if (totalFieldCount > 0) title += L", " + std::to_wstring(totalFieldCount) + L" fields";
    title += L")";
    out.formatTitle = title;

    const size_t nRender = std::min<size_t>(rawPeriphs.size(), 48u);
    const bool anyIrqs = !uniqueIrqs.empty();

    // Central System Bus at x = 0.0f; optional NVIC Interrupt Rail at x = 3.2f
    const float busX = 0.0f;
    const float irqRailX = 3.2f;

    // 1. Render Top CPU / SoC Core & NVIC Header Block (if <cpu> or <device> metadata/IRQs present)
    float curLeftY = 0.0f;
    float curRightY = 0.0f;
    float busTopY = 6.0f;

    if (hasCpu || !devName.empty() || anyIrqs) {
        const float coreW = 42.0f;
        const float coreH = 11.0f;
        const float coreCx = 0.0f;
        const float coreCy = 16.5f;
        const float x0 = coreCx - coreW * 0.5f, x1 = coreCx + coreW * 0.5f;
        const float y0 = coreCy - coreH * 0.5f, y1 = coreCy + coreH * 0.5f;

        DrawBoxOutlineSvd(out, x0, y0, x1, y1, 0.36f, EdaLayerId::SchSymbol, 0);
        DrawBoxOutlineSvd(out, x0 + 0.8f, y0 + 0.8f, x1 - 0.8f, y1 - 0.8f, 0.16f, EdaLayerId::SchSymbol, 0);

        std::string coreHdr = !devName.empty() ? devName : "ARM_MCU";
        if (!cpuName.empty()) coreHdr += " — Core: " + cpuName;
        if (!cpuRev.empty())  coreHdr += " (" + cpuRev + ")";
        out.labels.push_back({ x0 + 1.8f, y1 - 2.4f, 1.08f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(coreHdr) });

        std::string featLine = std::to_string(devBusWidth) + "-bit AHB/APB Bus";
        if (fpuPresent) featLine += " | FPU";
        if (mpuPresent) featLine += " | MPU";
        if (icache)     featLine += " | I$";
        if (dcache)     featLine += " | D$";
        if (nvicBits > 0) featLine += " | NVIC " + std::to_string(nvicBits) + "b";
        if (!uniqueIrqs.empty()) featLine += " (" + std::to_string(uniqueIrqs.size()) + " IRQs)";
        out.labels.push_back({ x0 + 1.8f, coreCy - 0.6f, 0.88f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(featLine) });

        // System Bus Master port + NVIC IRQ input port
        out.pads.push_back({ busX, y0, 0.75f, 0.75f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"M_AHB", L"SYS_BUS" });
        out.labels.push_back({ busX - 5.8f, y0 - 1.6f, 0.82f, EdaLayerId::SchWire, L"AHB/APB" });
        busTopY = y0;

        if (anyIrqs) {
            out.pads.push_back({ irqRailX, y0, 0.70f, 0.70f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol, L"NVIC", L"IRQ_BUS" });
            out.labels.push_back({ irqRailX + 0.8f, y0 - 1.6f, 0.82f, EdaLayerId::InnerCopper, L"NVIC_IRQ" });
        }

        if (hasCpu) {
            EdaComponent3D cpuComp;
            cpuComp.refDes = PathUtils::Utf8ToWide(cpuName.empty() ? "CPU" : cpuName);
            cpuComp.value = PathUtils::Utf8ToWide(devName.empty() ? featLine : devName);
            cpuComp.footprint = L"ARM_CORTEX_CORE";
            cpuComp.x = coreCx;
            cpuComp.y = coreCy;
            cpuComp.widthMm = coreW;
            cpuComp.lengthMm = coreH;
            cpuComp.heightMm = 1.6f;
            cpuComp.pinCount = anyIrqs ? 2 : 1;
            cpuComp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(cpuComp));
        }
    }

    // 2. Render Memory-Mapped Peripheral Cards with Real Register Rows & Bitfield Partitions
    float lowestTapY = 0.0f;
    float lowestIrqY = busTopY;

    for (size_t i = 0; i < nRender; ++i) {
        const SvdPeripheral& sp = rawPeriphs[i];
        const bool rightSide = ((i % 2u) == 0u);
        float& colTopY = rightSide ? curRightY : curLeftY;

        const size_t maxShownRegs = std::min<size_t>(sp.registers.size(), 5u);
        const bool hasMoreRegs = (sp.registers.size() > maxShownRegs);
        const float regRowH = 2.6f;
        const float hdrH = 4.6f;
        const float cardW = 30.0f;
        const float cardH = std::max(7.5f, hdrH + static_cast<float>(maxShownRegs + (hasMoreRegs ? 1u : 0u)) * regRowH + 1.2f);

        const float yTop = colTopY;
        const float yBot = yTop - cardH;
        const float cy   = (yTop + yBot) * 0.5f;
        const float bx   = rightSide ? (8.5f + cardW * 0.5f) : (-8.5f - cardW * 0.5f);
        const float x0   = bx - cardW * 0.5f;
        const float x1   = bx + cardW * 0.5f;

        colTopY = yBot - 3.2f;

        // Peripheral outer frame + header divider
        DrawBoxOutlineSvd(out, x0, yBot, x1, yTop, 0.30f, EdaLayerId::SchSymbol, 0);
        out.traces.push_back({ x0, yTop - hdrH, x1, yTop - hdrH, 0.18f, EdaLayerId::SchSymbol, 0 });

        // Compute peripheral address block size if declared
        uint64_t spanBytes = 0;
        for (const auto& ab : sp.addrBlocks) {
            spanBytes = std::max(spanBytes, ab.offset + ab.size);
        }
        std::wstring addrText = FormatHexAddr32(sp.baseAddress);
        if (spanBytes >= 1024ULL) {
            addrText += L" [" + std::to_wstring(spanBytes / 1024ULL) + L" KB]";
        } else if (spanBytes > 0) {
            addrText += L" [" + std::to_wstring(spanBytes) + L" B]";
        }

        std::wstring hdrTitle = PathUtils::Utf8ToWide(sp.name);
        if (!sp.groupName.empty() && sp.groupName != sp.name) {
            hdrTitle += L" (" + PathUtils::Utf8ToWide(sp.groupName) + L")";
        }
        out.labels.push_back({ x0 + 1.0f, yTop - 1.9f, 0.98f, EdaLayerId::SchLabel, hdrTitle });
        out.labels.push_back({ x0 + 1.0f, yTop - 3.8f, 0.80f, EdaLayerId::TopSilk,
                               addrText + L" \x2022 " + std::to_wstring(sp.registers.size()) + L" regs" });

        // Bus tap wire + solder dot on central AHB/APB bus
        const float tapY = yTop - 2.3f;
        const float edgeX = rightSide ? x0 : x1;
        out.traces.push_back({ busX, tapY, edgeX, tapY, 0.28f, EdaLayerId::SchWire, 1 });
        out.circles.push_back({ busX, tapY, 0.42f, 0.22f, true, EdaLayerId::SchWire });
        out.pads.push_back({ edgeX, tapY, 0.65f, 0.65f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol,
                             FormatHexAddr32(sp.baseAddress), L"SYS_BUS" });
        lowestTapY = std::min(lowestTapY, tapY);

        // Interrupt tap wire to NVIC rail if peripheral defines <interrupt>
        if (!sp.interrupts.empty() && anyIrqs) {
            const float irqY = tapY - 1.4f;
            out.traces.push_back({ irqRailX, irqY, edgeX, irqY, 0.20f, EdaLayerId::InnerCopper,
                                   2 + std::abs(sp.interrupts[0].value) });
            out.circles.push_back({ irqRailX, irqY, 0.34f, 0.18f, true, EdaLayerId::InnerCopper });
            std::wstring irqPin = L"IRQ" + std::to_wstring(sp.interrupts[0].value);
            out.pads.push_back({ edgeX, irqY, 0.55f, 0.55f, 0.0f, 0.0f, 0, EdaLayerId::InnerCopper,
                                 irqPin, PathUtils::Utf8ToWide(sp.interrupts[0].name) });
            if (!sp.interrupts[0].name.empty() && out.labels.size() < 4096) {
                out.labels.push_back({ std::min(irqRailX, edgeX) + 0.6f, irqY + 0.3f, 0.65f, EdaLayerId::InnerCopper,
                                       irqPin + L":" + PathUtils::Utf8ToWide(sp.interrupts[0].name) });
            }
            lowestIrqY = std::min(lowestIrqY, irqY);
        }

        // Draw register rows + proportional bitfield slices inside the peripheral card
        for (size_t ri = 0; ri < maxShownRegs; ++ri) {
            const SvdRegister& reg = sp.registers[ri];
            const float ryTop = yTop - hdrH - 0.3f - static_cast<float>(ri) * regRowH;
            const float ryBot = ryTop - (regRowH - 0.35f);
            const float rMidY = (ryTop + ryBot) * 0.5f;

            // Left label: "+0x00 CR [HSION,PLL1ON]"
            std::string fldSummary;
            for (size_t fi = 0; fi < reg.fields.size() && fi < 3u; ++fi) {
                if (!fldSummary.empty()) fldSummary += ",";
                fldSummary += reg.fields[fi].name;
            }
            if (reg.fields.size() > 3u) fldSummary += ",..";
            wchar_t rLbl[160] = {};
            if (!fldSummary.empty()) {
                swprintf_s(rLbl, L"+0x%02llX %hs [%hs]", static_cast<unsigned long long>(reg.offset), reg.name.c_str(), fldSummary.c_str());
            } else {
                swprintf_s(rLbl, L"+0x%02llX %hs", static_cast<unsigned long long>(reg.offset), reg.name.c_str());
            }
            out.labels.push_back({ x0 + 0.9f, rMidY - 0.3f, 0.70f, EdaLayerId::SchLabel, rLbl });

            // Right-side 32-bit bitfield bar [barX0 .. barX1]
            const float barX0 = x0 + 12.2f;
            const float barX1 = x1 - 0.9f;
            const float barW  = barX1 - barX0;
            const uint32_t regBits = std::max<uint32_t>(8u, reg.sizeBits);
            DrawBoxOutlineSvd(out, barX0, ryBot, barX1, ryTop, 0.16f, EdaLayerId::TopCopper, static_cast<int>(i + 2));

            for (size_t fi = 0; fi < reg.fields.size() && fi < 16u; ++fi) {
                const SvdField& fld = reg.fields[fi];
                if (fld.lsb >= regBits) continue;
                uint32_t clampedMsb = std::min<uint32_t>(fld.msb, regBits - 1u);
                // MSB on left, LSB on right (standard hardware register diagram convention: bit 31 -> bit 0)
                float fx0 = barX0 + barW * (static_cast<float>(regBits - 1u - clampedMsb) / static_cast<float>(regBits));
                float fx1 = barX0 + barW * (static_cast<float>(regBits - fld.lsb) / static_cast<float>(regBits));
                EdaLayerId flid = (fld.access == "RO") ? EdaLayerId::BottomCopper
                                : (fld.access == "WO") ? EdaLayerId::InnerCopper
                                                       : EdaLayerId::TopCopper;
                out.traces.push_back({ fx0, ryBot, fx0, ryTop, 0.15f, flid, static_cast<int>(i + 2) });
                out.traces.push_back({ fx1, ryBot, fx1, ryTop, 0.15f, flid, static_cast<int>(i + 2) });
                // Hatch active bitfield interior line so distinct bitfield widths produce distinct geometry
                out.traces.push_back({ fx0 + 0.08f, rMidY, std::max(fx0 + 0.12f, fx1 - 0.08f), rMidY, 0.22f, flid, static_cast<int>(i + 2) });
                if ((fx1 - fx0) >= 3.2f && out.labels.size() < 4096) {
                    out.labels.push_back({ fx0 + 0.25f, rMidY - 0.3f, 0.65f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(fld.name) });
                }
            }
        }

        if (hasMoreRegs) {
            const float moreY = yBot + 0.8f;
            std::wstring moreTxt = L"... +" + std::to_wstring(sp.registers.size() - maxShownRegs) + L" more registers";
            out.labels.push_back({ x0 + 1.0f, moreY, 0.72f, EdaLayerId::SchLabel, moreTxt });
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(sp.name);
        comp.value = FormatHexAddr32(sp.baseAddress) + L" (" + std::to_wstring(sp.registers.size()) + L" regs)";
        comp.footprint = PathUtils::Utf8ToWide(sp.groupName.empty() ? "SVD_PERIPH" : sp.groupName);
        comp.x = bx;
        comp.y = cy;
        comp.widthMm = cardW;
        comp.lengthMm = cardH;
        comp.heightMm = 1.3f;
        comp.pinCount = std::max<int>(1, 1 + static_cast<int>(sp.interrupts.size()));
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));
    }

    // Draw Central System Bus spine & NVIC Interrupt rail with exact fitted vertical extents
    const float busBotY = lowestTapY - 2.0f;
    out.traces.push_back({ busX, busTopY, busX, busBotY, 0.55f, EdaLayerId::SchWire, 1 });
    if (!hasCpu && devName.empty() && !anyIrqs) {
        out.labels.push_back({ busX - 5.5f, busTopY + 1.4f, 1.15f, EdaLayerId::SchWire, L"AHB/APB System Bus" });
    }
    if (anyIrqs && lowestIrqY < busTopY) {
        out.traces.push_back({ irqRailX, busTopY, irqRailX, lowestIrqY - 1.5f, 0.35f, EdaLayerId::InnerCopper, 2 });
    }

    out.netCount = 1u + uniqueIrqs.size() + nRender;
    return !out.components.empty() && !out.traces.empty();
}

} // namespace Formats
