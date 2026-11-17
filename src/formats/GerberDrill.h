#pragma once
// GerberDrill.h — #21 Ucamco Gerber RS-274X / X2, Gerber Job (.gbrjob), & Excellon NC Drill (.drl / .xln / .exc)

struct GerberMacroPrim {
    int code = 0;                 // 1=circle, 4=outline, 5=polygon, 7=thermal, 20=vectorLine, 21=centerRect
    std::vector<std::string> exprs;
};

struct GerberApertureDef {
    uint8_t kind = 0;             // 0=circle, 1=rect, 2=polygon, 3=obround, 4=macro
    float w = 0.25f;              // mm
    float h = 0.25f;              // mm
    float drill = 0.0f;           // mm
    float rotDeg = 0.0f;
    int polyVerts = 0;
    std::vector<EdaPadPrim> macroPads;
    std::vector<EdaTraceSeg> macroSegs;
    std::vector<EdaCirclePrim> macroCircles;
};

static double EvalGerberExpr(std::string_view expr, const std::vector<double>& params) {
    struct Parser {
        std::string_view s;
        const std::vector<double>& prm;
        size_t p = 0;

        void skipWs() {
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
        }
        double parseFactor() {
            skipWs();
            if (p >= s.size()) return 0.0;
            double sign = 1.0;
            while (p < s.size() && (s[p] == '+' || s[p] == '-')) {
                if (s[p] == '-') sign = -sign;
                ++p;
                skipWs();
            }
            if (p < s.size() && s[p] == '(') {
                ++p;
                double v = parseAddSub();
                skipWs();
                if (p < s.size() && s[p] == ')') ++p;
                return sign * v;
            }
            if (p < s.size() && s[p] == '$') {
                ++p;
                int idx = 0;
                while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
                    idx = idx * 10 + (s[p] - '0');
                    ++p;
                }
                double pv = (idx >= 1 && static_cast<size_t>(idx) <= prm.size()) ? prm[static_cast<size_t>(idx - 1)] : 0.0;
                return sign * pv;
            }
            size_t start = p;
            while (p < s.size() && ((s[p] >= '0' && s[p] <= '9') || s[p] == '.')) ++p;
            if (p == start) return 0.0;
            char buf[64] = {};
            size_t n = std::min<size_t>(p - start, 63);
            std::memcpy(buf, s.data() + start, n);
            return sign * std::strtod(buf, nullptr);
        }
        double parseTerm() {
            double lhs = parseFactor();
            for (;;) {
                skipWs();
                if (p >= s.size()) break;
                char op = s[p];
                if (op != 'x' && op != 'X' && op != '*' && op != '/') break;
                ++p;
                double rhs = parseFactor();
                if (op == '/') lhs = (std::fabs(rhs) > 1e-12) ? (lhs / rhs) : 0.0;
                else lhs *= rhs;
            }
            return lhs;
        }
        double parseAddSub() {
            double lhs = parseTerm();
            for (;;) {
                skipWs();
                if (p >= s.size()) break;
                char op = s[p];
                if (op != '+' && op != '-') break;
                ++p;
                double rhs = parseTerm();
                lhs = (op == '+') ? (lhs + rhs) : (lhs - rhs);
            }
            return lhs;
        }
    };
    Parser parser{ expr, params, 0 };
    return parser.parseAddSub();
}

static bool ParseGerberCoordField(
    std::string_view rawDigits,
    int intDigits,
    int decDigits,
    bool trailingZeroOmit,
    double unitToMm,
    float& outValMm)
{
    if (rawDigits.empty()) return false;
    bool neg = false;
    size_t pos = 0;
    if (rawDigits[pos] == '-' || rawDigits[pos] == '+') {
        neg = (rawDigits[pos] == '-');
        ++pos;
    }
    if (pos >= rawDigits.size()) return false;
    std::string_view numPart = rawDigits.substr(pos);
    if (numPart.find('.') != std::string_view::npos) {
        char buf[64] = {};
        size_t n = std::min<size_t>(rawDigits.size(), 63);
        std::memcpy(buf, rawDigits.data(), n);
        double v = std::strtod(buf, nullptr) * unitToMm;
        outValMm = static_cast<float>(v);
        return std::isfinite(outValMm);
    }
    for (char c : numPart) {
        if (c < '0' || c > '9') return false;
    }
    std::string digits(numPart);
    const int totalDigits = std::max(1, intDigits + decDigits);
    if (trailingZeroOmit && static_cast<int>(digits.size()) < totalDigits) {
        digits.append(static_cast<size_t>(totalDigits - static_cast<int>(digits.size())), '0');
    }
    double intVal = 0.0;
    for (char c : digits) {
        intVal = intVal * 10.0 + static_cast<double>(c - '0');
    }
    if (neg) intVal = -intVal;
    double scaled = intVal * std::pow(10.0, -decDigits) * unitToMm;
    outValMm = static_cast<float>(scaled);
    return std::isfinite(outValMm);
}

// Gerber X2 job file (.gbrjob, Ucamco): a JSON manifest (Header.GenerationSoftware, GeneralSpecs with ProjectId,
// Size, LayerNumber, BoardThickness, FilesAttributes [Path, FileFunction], MaterialStackup [Type, Name,
// Thickness]). Shown as the board outline (when Size is given), a stackup cross-section and the file list.
static bool ParseGerberJobJson(std::string_view txt, ParsedEdaBoard& out) {
    using JVal = ::Explorer2::Json::Value;
    JVal root;
    if (!::Explorer2::Json::ParseDocument(txt, root) || !root.IsObject()) return false;
    const JVal* general = root.Find("GeneralSpecs");
    const JVal* files = root.Find("FilesAttributes");
    const JVal* stack = root.Find("MaterialStackup");
    if ((!general || !general->IsObject()) && (!files || !files->IsArray()) && (!stack || !stack->IsArray())) return false;
    auto num = [](const JVal* v, double defVal) { return (v && v->IsNumber() && std::isfinite(v->numVal)) ? v->numVal : defVal; };

    double sizeX = -1.0, sizeY = -1.0, boardThick = 0.0;
    int layerNumber = 0;
    std::string projName, revision;
    if (general && general->IsObject()) {
        if (const JVal* size = general->Find("Size")) {
            sizeX = num(size->Find("X"), -1.0);
            sizeY = num(size->Find("Y"), -1.0);
        }
        layerNumber = general->GetInt("LayerNumber", 0);
        boardThick = num(general->Find("BoardThickness"), 0.0);
        if (const JVal* pid = general->Find("ProjectId")) {
            projName = pid->IsObject() ? pid->GetString("Name") : (pid->IsString() ? pid->strVal : std::string());
            if (pid->IsObject()) revision = pid->GetString("Revision");
        }
    }
    const bool hasSize = sizeX > 0.1 && sizeY > 0.1 && sizeX <= 5000.0 && sizeY <= 5000.0;
    std::string vendor, appName;
    if (const JVal* hdr = root.Find("Header")) {
        if (const JVal* gs = hdr->Find("GenerationSoftware")) {
            vendor = gs->GetString("Vendor");
            appName = gs->GetString("Application");
            std::string ver = gs->GetString("Version");
            if (!ver.empty()) appName += (appName.empty() ? "" : " ") + ver;
        }
    }

    std::vector<std::pair<std::string, std::string>> fileAttrs;
    if (files && files->IsArray()) {
        for (const auto& f : files->arrVal) {
            std::string path = f.GetString("Path", f.GetString("FilePath"));
            std::string fn = f.GetString("FileFunction");
            if (!path.empty() || !fn.empty()) fileAttrs.push_back({ path, fn });
            if (fileAttrs.size() >= 256) break;
        }
    }
    struct StackLayer { std::string type, name; double thickMm = 0.0; };
    std::vector<StackLayer> stackLayers;
    if (stack && stack->IsArray()) {
        for (const auto& l : stack->arrVal) {
            StackLayer sl;
            sl.type = l.GetString("Type");
            sl.name = l.GetString("Name");
            sl.thickMm = num(l.Find("Thickness"), 0.0);
            std::string material = l.GetString("Material");
            if (!material.empty() && sl.name.find(material) == std::string::npos) sl.name += (sl.name.empty() ? "" : ", ") + material;
            if (!sl.type.empty() || !sl.name.empty()) stackLayers.push_back(std::move(sl));
            if (stackLayers.size() >= 128) break;
        }
    }
    if (!hasSize && fileAttrs.empty() && stackLayers.empty() && layerNumber <= 0) return false;

    out.kind = EdaDocKind::GerberLayer;
    out.formatTitle = L"Gerber X2 Job Specification (.gbrjob)";
    if (!projName.empty()) out.formatTitle += L" \u2014 " + PathUtils::Utf8ToWide(projName + (revision.empty() ? "" : " " + revision));

    // Without a declared size there is no outline; the text is laid out on a nominal 60 mm width
    const float boardW = hasSize ? static_cast<float>(sizeX) : 60.0f;
    const float topY = hasSize ? static_cast<float>(sizeY) : 0.0f;
    if (hasSize) {
        const float w = boardW, h = topY;
        out.traces.push_back({ 0.0f, 0.0f, w, 0.0f, 0.25f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ w, 0.0f, w, h, 0.25f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ w, h, 0.0f, h, 0.25f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ 0.0f, h, 0.0f, 0.0f, 0.25f, EdaLayerId::EdgeCuts, 0 });
    }
    std::wstring header;
    if (hasSize) {
        wchar_t buf[96] = {};
        swprintf_s(buf, L"Board %.2f x %.2f mm", sizeX, sizeY);
        header = buf;
    }
    if (layerNumber > 0) header += (header.empty() ? L"" : L" | ") + std::to_wstring(layerNumber) + L" copper layers";
    if (boardThick > 0.0) {
        wchar_t buf[48] = {};
        swprintf_s(buf, L"thickness %.2f mm", boardThick);
        header += (header.empty() ? L"" : L" | ") + std::wstring(buf);
    }
    if (!header.empty()) out.labels.push_back({ 2.0f, topY - 3.0f, 1.4f, EdaLayerId::TopSilk, header });
    if (!vendor.empty() || !appName.empty()) {
        out.labels.push_back({ 2.0f, topY - 5.5f, 1.1f, EdaLayerId::TopSilk,
            PathUtils::Utf8ToWide("Generator: " + vendor + (appName.empty() ? "" : " " + appName)) });
    }

    // Stackup cross-section, top to bottom; the lists continue below the outline when the board is small
    float y = topY - 9.0f;
    const float barW = std::max(10.0f, boardW * 0.42f);
    size_t copperSeen = 0, copperTotal = 0;
    for (const auto& sl : stackLayers) copperTotal += (sl.type == "Copper") ? 1 : 0;
    for (size_t i = 0; i < stackLayers.size(); ++i) {
        const auto& sl = stackLayers[i];
        EdaLayerId lid = EdaLayerId::InnerCopper;
        if (sl.type == "Copper") {
            ++copperSeen;
            lid = (copperSeen == 1) ? EdaLayerId::TopCopper : (copperSeen == copperTotal ? EdaLayerId::BottomCopper : EdaLayerId::InnerCopper);
        } else if (sl.type.find("Legend") != std::string::npos || sl.type.find("Silk") != std::string::npos) {
            lid = EdaLayerId::TopSilk;
        }
        float strokeW = std::clamp(static_cast<float>(sl.thickMm) * 1.5f, 0.18f, 1.2f);
        out.traces.push_back({ 2.5f, y, 2.5f + barW, y, strokeW, lid, static_cast<int>(i + 1) });
        std::string desc = sl.type + (sl.name.empty() ? "" : " (" + sl.name + ")");
        if (sl.thickMm > 0.0) {
            char t[32] = {};
            snprintf(t, sizeof(t), " %.3f mm", sl.thickMm);
            desc += t;
        }
        out.labels.push_back({ 3.5f + barW, y - 0.3f, 0.95f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(desc) });
        y -= 2.4f;
    }
    y -= 1.5f;
    for (const auto& fa : fileAttrs) {
        out.labels.push_back({ 2.5f, y, 0.9f, EdaLayerId::BottomSilk, PathUtils::Utf8ToWide(fa.first + " [" + fa.second + "]") });
        y -= 2.0f;
    }

    out.netCount = std::max<size_t>(1, stackLayers.size() + fileAttrs.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

static bool ParseExcellonDrillFile(std::string_view txt, ParsedEdaBoard& out) {
    // Reject binary data
    for (unsigned char c : txt) {
        if (c == 0) return false;
    }

    out.kind = EdaDocKind::ExcellonDrill;
    out.formatTitle = L"Excellon NC Drill (.drl)";

    bool metric = false;         // Excellon defaults to INCH (2.4) unless METRIC / M71 specified
    bool explicitUnit = false;
    bool trailingZeroOmit = false; // LZ (omit leading zeros) is most common; TZ pads trailing zeros
    int intDigits = 2;
    int decDigits = 4;
    bool hasFormatHint = false;

    std::unordered_map<int, float> toolDiamMm;
    int curTool = 0;
    float curDiamMm = 0.8f;
    float curX = 0.0f, curY = 0.0f;
    bool routMode = false;
    bool plungeDown = false;
    int interpG = 1; // 0=rapid, 1=linear, 2=CW arc, 3=CCW arc
    bool sawHeaderOrCmd = false;

    size_t pos = 0;
    while (pos < txt.size()) {
        size_t eol = txt.find_first_of("\r\n", pos);
        if (eol == std::string_view::npos) eol = txt.size();
        std::string_view rawLine = txt.substr(pos, eol - pos);
        pos = eol;
        while (pos < txt.size() && (txt[pos] == '\r' || txt[pos] == '\n')) ++pos;

        // Trim whitespace
        while (!rawLine.empty() && (rawLine.front() == ' ' || rawLine.front() == '\t')) rawLine.remove_prefix(1);
        while (!rawLine.empty() && (rawLine.back() == ' ' || rawLine.back() == '\t')) rawLine.remove_suffix(1);
        if (rawLine.empty()) continue;

        // Parse KiCad/Altium format comment hints like ;FILE_FORMAT=3:3 or ;FORMAT={3:3/ absolute / metric / ...}
        if (rawLine[0] == ';') {
            size_t ff = rawLine.find("FILE_FORMAT=");
            if (ff != std::string_view::npos && ff + 15 <= rawLine.size()) {
                int id = rawLine[ff + 12] - '0';
                char sep = rawLine[ff + 13];
                int dd = rawLine[ff + 14] - '0';
                if (sep == ':' && id >= 1 && id <= 6 && dd >= 1 && dd <= 6) {
                    intDigits = id;
                    decDigits = dd;
                    hasFormatHint = true;
                }
            }
            size_t fmtBr = rawLine.find("FORMAT={");
            if (fmtBr != std::string_view::npos && fmtBr + 11 <= rawLine.size()) {
                int id = rawLine[fmtBr + 8] - '0';
                char sep = rawLine[fmtBr + 9];
                int dd = rawLine[fmtBr + 10] - '0';
                if (sep == ':' && id >= 1 && id <= 6 && dd >= 1 && dd <= 6) {
                    intDigits = id;
                    decDigits = dd;
                    hasFormatHint = true;
                }
            }
            continue;
        }

        if (rawLine == "%" || rawLine == "M48" || rawLine == "M95" || rawLine.rfind("FMAT", 0) == 0 || rawLine == "G90") {
            sawHeaderOrCmd = true;
            continue;
        }
        if (rawLine == "M30" || rawLine == "M00") {
            sawHeaderOrCmd = true;
            break;
        }

        // Unit & zero suppression directives: METRIC,LZ,000.000 / INCH,TZ,00.0000 / M71 / M72
        if (rawLine.rfind("METRIC", 0) == 0 || rawLine == "M71") {
            sawHeaderOrCmd = true;
            metric = true;
            explicitUnit = true;
            if (!hasFormatHint) { intDigits = 3; decDigits = 3; }
            if (rawLine.find("TZ") != std::string_view::npos) trailingZeroOmit = true;
            else if (rawLine.find("LZ") != std::string_view::npos) trailingZeroOmit = false;
            size_t zMask = rawLine.find("00");
            if (zMask != std::string_view::npos) {
                size_t dot = rawLine.find('.', zMask);
                if (dot != std::string_view::npos) {
                    int id = static_cast<int>(dot - zMask);
                    size_t ze = dot + 1;
                    while (ze < rawLine.size() && rawLine[ze] == '0') ++ze;
                    int dd = static_cast<int>(ze - (dot + 1));
                    if (id >= 1 && id <= 6 && dd >= 1 && dd <= 6) {
                        intDigits = id; decDigits = dd; hasFormatHint = true;
                    }
                }
            }
            continue;
        }
        if (rawLine.rfind("INCH", 0) == 0 || rawLine == "M72") {
            sawHeaderOrCmd = true;
            metric = false;
            explicitUnit = true;
            if (!hasFormatHint) { intDigits = 2; decDigits = 4; }
            if (rawLine.find("TZ") != std::string_view::npos) trailingZeroOmit = true;
            else if (rawLine.find("LZ") != std::string_view::npos) trailingZeroOmit = false;
            size_t zMask = rawLine.find("00");
            if (zMask != std::string_view::npos) {
                size_t dot = rawLine.find('.', zMask);
                if (dot != std::string_view::npos) {
                    int id = static_cast<int>(dot - zMask);
                    size_t ze = dot + 1;
                    while (ze < rawLine.size() && rawLine[ze] == '0') ++ze;
                    int dd = static_cast<int>(ze - (dot + 1));
                    if (id >= 1 && id <= 6 && dd >= 1 && dd <= 6) {
                        intDigits = id; decDigits = dd; hasFormatHint = true;
                    }
                }
            }
            continue;
        }

        // Rout plunge / retract / drill mode
        if (rawLine == "M15") { plungeDown = true; routMode = true; continue; }
        if (rawLine == "M16" || rawLine == "M17") { plungeDown = false; continue; }
        if (rawLine == "G05") { routMode = false; plungeDown = false; continue; }

        // Tool definition or selection: T01C0.800 or T01
        if (rawLine[0] == 'T' && rawLine.size() >= 2 && rawLine[1] >= '0' && rawLine[1] <= '9') {
            sawHeaderOrCmd = true;
            size_t p = 1;
            int tNum = 0;
            while (p < rawLine.size() && rawLine[p] >= '0' && rawLine[p] <= '9') {
                tNum = tNum * 10 + (rawLine[p] - '0');
                ++p;
            }
            size_t cPos = rawLine.find('C', p);
            if (cPos != std::string_view::npos && cPos + 1 < rawLine.size()) {
                size_t ce = cPos + 1;
                while (ce < rawLine.size() && ((rawLine[ce] >= '0' && rawLine[ce] <= '9') || rawLine[ce] == '.')) ++ce;
                char buf[64] = {};
                std::memcpy(buf, rawLine.data() + cPos + 1, std::min<size_t>(ce - (cPos + 1), 63));
                float d = std::strtof(buf, nullptr);
                if (!explicitUnit && d >= 0.1f) {
                    // Heuristic only if header omitted unit before tool def
                }
                if (!metric) d *= 25.4f;
                if (d > 0.02f && d <= 50.0f) {
                    toolDiamMm[tNum] = d;
                    curTool = tNum;
                    curDiamMm = d;
                }
            } else if (tNum == 0) {
                curTool = 0;
            } else {
                curTool = tNum;
                auto it = toolDiamMm.find(tNum);
                if (it != toolDiamMm.end()) curDiamMm = it->second;
            }
            continue;
        }

        const double unitMm = metric ? 1.0 : 25.4;
        auto readFieldAfter = [&](std::string_view s, char letter, float& outMm) -> bool {
            size_t lp = s.find(letter);
            if (lp == std::string_view::npos) return false;
            size_t vs = lp + 1;
            size_t ve = vs;
            while (ve < s.size() && ((s[ve] >= '0' && s[ve] <= '9') || s[ve] == '-' || s[ve] == '+' || s[ve] == '.')) ++ve;
            if (ve == vs) return false;
            // In Excellon TZ, digits are left-aligned (omit trailing zeros); in LZ (default), digits are right-aligned!
            // Note: Some Excellon writers write "LZ" meaning "Leading Zeros INCLUDED" (= omit trailing zeros) — wait:
            // Standard Excellon:
            //   INCH,TZ = Trailing Zeros INCLUDED (= leading zeros omitted, right-aligned decimal!)
            //   INCH,LZ = Leading Zeros INCLUDED (= trailing zeros omitted, left-aligned!)
            //Wait: KiCad writes `METRIC,TZ` when trailing zeros are KEPT (i.e. right-aligned standard integer*10^-dec), OR decimal points `X12.34Y56.78`!
            // Let's handle explicit decimal points directly, and for integer strings:
            // If `trailingZeroOmit` is true and string length < intDigits+decDigits, pad right; otherwise standard *10^-decDigits!
            return ParseGerberCoordField(s.substr(vs, ve - vs), intDigits, decDigits, trailingZeroOmit, unitMm, outMm);
        };

        // Check for G85 slot: X...Y...G85X...Y...
        size_t g85Pos = rawLine.find("G85");
        if (g85Pos != std::string_view::npos) {
            std::string_view firstHalf = rawLine.substr(0, g85Pos);
            std::string_view secondHalf = rawLine.substr(g85Pos + 3);
            float x1 = curX, y1 = curY, x2 = curX, y2 = curY;
            bool has1 = readFieldAfter(firstHalf, 'X', x1) | readFieldAfter(firstHalf, 'Y', y1);
            bool has2 = readFieldAfter(secondHalf, 'X', x2) | readFieldAfter(secondHalf, 'Y', y2);
            if (has1 && has2) {
                float d = std::max(0.15f, curDiamMm);
                out.vias.push_back({ x1, y1, d * 1.35f, d, curTool });
                out.vias.push_back({ x2, y2, d * 1.35f, d, curTool });
                out.traces.push_back({ x1, y1, x2, y2, d, EdaLayerId::Drills, curTool });
                curX = x2; curY = y2;
            }
            continue;
        }

        // Check for G00 / G01 / G02 / G03 rout prefix
        std::string_view coordPart = rawLine;
        if (coordPart.rfind("G00", 0) == 0) { routMode = true; plungeDown = false; interpG = 0; coordPart.remove_prefix(3); }
        else if (coordPart.rfind("G01", 0) == 0) { routMode = true; interpG = 1; coordPart.remove_prefix(3); }
        else if (coordPart.rfind("G02", 0) == 0) { routMode = true; interpG = 2; coordPart.remove_prefix(3); }
        else if (coordPart.rfind("G03", 0) == 0) { routMode = true; interpG = 3; coordPart.remove_prefix(3); }

        // Repeat drill command: R<count>X<dx>Y<dy>
        if (coordPart.size() >= 2 && coordPart[0] == 'R' && coordPart[1] >= '0' && coordPart[1] <= '9') {
            size_t rp = 1;
            int repCount = 0;
            while (rp < coordPart.size() && coordPart[rp] >= '0' && coordPart[rp] <= '9') {
                repCount = repCount * 10 + (coordPart[rp] - '0');
                ++rp;
            }
            float dx = 0.0f, dy = 0.0f;
            readFieldAfter(coordPart.substr(rp), 'X', dx);
            readFieldAfter(coordPart.substr(rp), 'Y', dy);
            repCount = std::min(repCount, 2000);
            float d = std::max(0.15f, curDiamMm);
            for (int r = 0; r < repCount; ++r) {
                curX += dx;
                curY += dy;
                out.vias.push_back({ curX, curY, d * 1.4f, d, curTool });
            }
            continue;
        }

        float nx = curX, ny = curY;
        bool hasX = readFieldAfter(coordPart, 'X', nx);
        bool hasY = readFieldAfter(coordPart, 'Y', ny);
        if (hasX || hasY) {
            float d = std::max(0.15f, curDiamMm);
            if (routMode) {
                if (plungeDown && interpG >= 1) {
                    out.traces.push_back({ curX, curY, nx, ny, d, EdaLayerId::Drills, curTool });
                }
            } else {
                out.vias.push_back({ nx, ny, d * 1.4f, d, curTool });
            }
            curX = nx;
            curY = ny;
        }
    }

    if (!sawHeaderOrCmd && toolDiamMm.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    if (out.vias.empty() && out.traces.empty() && out.pads.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    out.netCount = std::max<size_t>(1, toolDiamMm.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

static bool ParseGerberOrDrill(std::string_view txt, const std::wstring& ext, ParsedEdaBoard& out) {
    if (txt.empty() || txt.size() > 64 * 1024 * 1024) {
        out = ParsedEdaBoard{};
        return false;
    }
    for (unsigned char c : txt) {
        if (c == 0) { out = ParsedEdaBoard{}; return false; }
    }

    if (ext == L".gbrjob") {
        if (ParseGerberJobJson(txt, out)) return true;
        out = ParsedEdaBoard{};
        return false;
    }

    // Trim leading whitespace to check for M48 Excellon header
    std::string_view trimmed = txt;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r' || trimmed.front() == '\n')) {
        trimmed.remove_prefix(1);
    }
    const bool looksLikeExcellon =
        ext == L".drl" || ext == L".xln" || ext == L".exc" ||
        trimmed.rfind("M48", 0) == 0;
    if (looksLikeExcellon && trimmed.find("%FS") == std::string_view::npos && trimmed.find("%ADD") == std::string_view::npos) {
        return ParseExcellonDrillFile(txt, out);
    }

    // Gerber RS-274X / X2 Parser
    EdaLayerId activeLayer = EdaLayerId::TopCopper;
    if (ext == L".gbl" || ext == L".gbs" || ext == L".gbo" || ext == L".gbp") activeLayer = EdaLayerId::BottomCopper;
    else if (ext == L".gto" || ext == L".gts" || ext == L".gtp") activeLayer = EdaLayerId::TopSilk;
    else if (ext == L".gm1" || ext == L".gko" || ext == L".oln") activeLayer = EdaLayerId::EdgeCuts;
    else if (ext == L".g2" || ext == L".g3" || ext == L".gp1" || ext == L".gp2") activeLayer = EdaLayerId::InnerCopper;

    out.kind = EdaDocKind::GerberLayer;
    std::wstring x2FuncLabel;

    bool metric = true;
    bool hasFS = false;
    bool sawGerberSyntax = false;
    bool trailingZeroOmit = false;
    bool incrementalCoords = false;
    int xInt = 2, xDec = 4;
    int yInt = 2, yDec = 4;

    std::unordered_map<std::string, std::vector<GerberMacroPrim>> macroTemplates;
    std::unordered_map<int, GerberApertureDef> apertures;
    int curApertureCode = 10;
    GerberApertureDef defaultAp{};
    defaultAp.kind = 0; defaultAp.w = 0.25f; defaultAp.h = 0.25f;

    float curX = 0.0f, curY = 0.0f;
    int interpMode = 1;       // 1=G01 linear, 2=G02 CW arc, 3=G03 CCW arc
    bool multiQuadrant = true; // G75 default in modern Gerber
    bool inRegion = false;    // G36..G37 contour region
    bool clearPolarity = false; // %LPC*% vs %LPD*%
    float loadScale = 1.0f;   // %LS...*%
    float loadRotDeg = 0.0f;  // %LR...*%
    bool mirrorX = false, mirrorY = false; // %LM...*%
    int modalDCode = 0;

    // X2 object attributes (%TO.N, %TO.C, %TO.P)
    std::string curNetName;
    std::string curCompRef;
    std::string curPinNum;
    std::unordered_map<std::string, int> netNameIds;
    struct X2CompAccum { float sumX = 0.0f, sumY = 0.0f; float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f; int pins = 0; };
    std::map<std::string, X2CompAccum> x2Comps;

    // Step-and-Repeat (%SRX..Y..I..J..*%) state
    bool inSR = false;
    int srNx = 1, srNy = 1;
    float srDxMm = 0.0f, srDyMm = 0.0f;
    size_t srTraceStart = 0, srPadStart = 0, srCircleStart = 0;

    auto finishSR = [&]() {
        if (!inSR) return;
        inSR = false;
        if (srNx <= 1 && srNy <= 1) return;
        const size_t baseTraceEnd = out.traces.size();
        const size_t basePadEnd = out.pads.size();
        const size_t baseCircEnd = out.circles.size();
        const int maxCopies = 400;
        int copies = 0;
        for (int ix = 0; ix < srNx; ++ix) {
            for (int iy = 0; iy < srNy; ++iy) {
                if (ix == 0 && iy == 0) continue;
                if (++copies > maxCopies) break;
                const float ox = static_cast<float>(ix) * srDxMm;
                const float oy = static_cast<float>(iy) * srDyMm;
                for (size_t ti = srTraceStart; ti < baseTraceEnd && out.traces.size() < 120000; ++ti) {
                    EdaTraceSeg seg = out.traces[ti];
                    seg.x1 += ox; seg.y1 += oy; seg.x2 += ox; seg.y2 += oy;
                    out.traces.push_back(seg);
                }
                for (size_t pi = srPadStart; pi < basePadEnd && out.pads.size() < 80000; ++pi) {
                    EdaPadPrim pad = out.pads[pi];
                    pad.x += ox; pad.y += oy;
                    out.pads.push_back(std::move(pad));
                }
                for (size_t ci = srCircleStart; ci < baseCircEnd && out.circles.size() < 40000; ++ci) {
                    EdaCirclePrim cr = out.circles[ci];
                    cr.cx += ox; cr.cy += oy;
                    out.circles.push_back(cr);
                }
            }
        }
    };

    // Region contour vertices
    std::vector<std::pair<float, float>> contourPts;
    auto flushContour = [&]() {
        if (contourPts.size() < 3) {
            contourPts.clear();
            return;
        }
        // Close contour if needed
        if (std::hypot(contourPts.front().first - contourPts.back().first,
                       contourPts.front().second - contourPts.back().second) > 1e-3f) {
            contourPts.push_back(contourPts.front());
        }
        EdaLayerId lid = clearPolarity ? EdaLayerId::EdgeCuts : activeLayer;
        float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
        for (size_t i = 0; i + 1 < contourPts.size(); ++i) {
            float x1 = contourPts[i].first, y1 = contourPts[i].second;
            float x2 = contourPts[i + 1].first, y2 = contourPts[i + 1].second;
            minX = std::min({ minX, x1, x2 }); maxX = std::max({ maxX, x1, x2 });
            minY = std::min({ minY, y1, y2 }); maxY = std::max({ maxY, y1, y2 });
            int nid = curNetName.empty() ? 0 : netNameIds[curNetName];
            if (out.traces.size() < 120000) {
                out.traces.push_back({ x1, y1, x2, y2, 0.14f, lid, nid });
            }
        }
        // Scanline copper fill hatch inside polygon so filled G36..G37 copper pours look unmistakably solid
        if (!clearPolarity && maxY > minY + 0.15f && maxX > minX + 0.15f) {
            const float spanY = maxY - minY;
            const int nLines = std::clamp(static_cast<int>(spanY / 0.35f), 2, 48);
            const float stepY = spanY / static_cast<float>(nLines + 1);
            const float fillW = std::clamp(stepY * 0.85f, 0.12f, 0.65f);
            int nid = curNetName.empty() ? 0 : netNameIds[curNetName];
            std::vector<float> xHits;
            for (int li = 1; li <= nLines; ++li) {
                float scanY = minY + stepY * static_cast<float>(li);
                xHits.clear();
                for (size_t i = 0; i + 1 < contourPts.size(); ++i) {
                    float x1 = contourPts[i].first, y1 = contourPts[i].second;
                    float x2 = contourPts[i + 1].first, y2 = contourPts[i + 1].second;
                    if ((y1 <= scanY && y2 > scanY) || (y2 <= scanY && y1 > scanY)) {
                        float t = (scanY - y1) / (y2 - y1);
                        xHits.push_back(x1 + t * (x2 - x1));
                    }
                }
                std::sort(xHits.begin(), xHits.end());
                for (size_t k = 0; k + 1 < xHits.size() && out.traces.size() < 120000; k += 2) {
                    if (xHits[k + 1] > xHits[k] + 0.05f) {
                        out.traces.push_back({ xHits[k], scanY, xHits[k + 1], scanY, fillW, lid, nid });
                    }
                }
            }
        }
        contourPts.clear();
    };

    auto xformLocal = [&](float lx, float ly, float& ox, float& oy) {
        if (mirrorX) lx = -lx;
        if (mirrorY) ly = -ly;
        lx *= loadScale;
        ly *= loadScale;
        if (std::fabs(loadRotDeg) > 1e-3f) {
            float rad = loadRotDeg * 3.14159265f / 180.0f;
            float cs = std::cos(rad), sn = std::sin(rad);
            ox = lx * cs - ly * sn;
            oy = lx * sn + ly * cs;
        } else {
            ox = lx;
            oy = ly;
        }
    };

    auto emitArcPoints = [&](float x1, float y1, float x2, float y2, float iOff, float jOff, bool cw,
                             std::vector<std::pair<float, float>>& outPts) {
        float cx = x1 + iOff;
        float cy = y1 + jOff;
        if (!multiQuadrant) {
            // Single-quadrant G74: pick sign of |iOff|, |jOff| minimizing radius discrepancy
            float ai = std::fabs(iOff), aj = std::fabs(jOff);
            float bestErr = 1e9f;
            for (int sx : { -1, 1 }) {
                for (int sy : { -1, 1 }) {
                    float tcx = x1 + sx * ai;
                    float tcy = y1 + sy * aj;
                    float r1 = std::hypot(x1 - tcx, y1 - tcy);
                    float r2 = std::hypot(x2 - tcx, y2 - tcy);
                    float err = std::fabs(r1 - r2);
                    if (err < bestErr) { bestErr = err; cx = tcx; cy = tcy; }
                }
            }
        }
        float r = std::hypot(x1 - cx, y1 - cy);
        if (r < 1e-4f) {
            outPts.push_back({ x2, y2 });
            return;
        }
        float a1 = std::atan2(y1 - cy, x1 - cx);
        float a2 = std::atan2(y2 - cy, x2 - cx);
        const float twoPi = 6.283185307f;
        float sweep = 0.0f;
        if (std::hypot(x2 - x1, y2 - y1) < 1e-4f && multiQuadrant) {
            sweep = cw ? -twoPi : twoPi;
        } else if (cw) {
            sweep = a2 - a1;
            if (sweep >= -1e-5f) sweep -= twoPi;
        } else {
            sweep = a2 - a1;
            if (sweep <= 1e-5f) sweep += twoPi;
        }
        int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / (twoPi / 24.0f))), 4, 36);
        float rEnd = std::hypot(x2 - cx, y2 - cy);
        for (int s = 1; s <= segs; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(segs);
            if (s == segs) {
                outPts.push_back({ x2, y2 });
            } else {
                float ang = a1 + sweep * t;
                float rt = r + (rEnd - r) * t;
                outPts.push_back({ cx + std::cos(ang) * rt, cy + std::sin(ang) * rt });
            }
        }
    };

    // Main Gerber tokenizer: extended `%...%` blocks vs `*`-terminated data blocks
    size_t pos = 0;
    while (pos < txt.size()) {
        while (pos < txt.size() && (txt[pos] == ' ' || txt[pos] == '\t' || txt[pos] == '\r' || txt[pos] == '\n')) ++pos;
        if (pos >= txt.size()) break;

        if (txt[pos] == '%') {
            size_t closePct = txt.find('%', pos + 1);
            if (closePct == std::string_view::npos) {
                // Unterminated extended command block -> corrupt Gerber!
                out = ParsedEdaBoard{};
                return false;
            }
            std::string_view extBlock = txt.substr(pos + 1, closePct - (pos + 1));
            pos = closePct + 1;
            sawGerberSyntax = true;

            // Trim whitespace inside %...%
            while (!extBlock.empty() && (extBlock.front() == ' ' || extBlock.front() == '\t' || extBlock.front() == '\r' || extBlock.front() == '\n')) extBlock.remove_prefix(1);

            // Handle %AM<MacroName>*<prim>*<prim>*% (multi-statement inside single %...%)
            if (extBlock.rfind("AM", 0) == 0) {
                size_t firstStar = extBlock.find('*');
                if (firstStar != std::string_view::npos && firstStar > 2) {
                    std::string macroName(extBlock.substr(2, firstStar - 2));
                    while (!macroName.empty() && (macroName.back() == ' ' || macroName.back() == '\r' || macroName.back() == '\n')) macroName.pop_back();
                    std::vector<GerberMacroPrim> prims;
                    size_t mp = firstStar + 1;
                    while (mp < extBlock.size()) {
                        size_t nextStar = extBlock.find('*', mp);
                        if (nextStar == std::string_view::npos) nextStar = extBlock.size();
                        std::string_view stmt = extBlock.substr(mp, nextStar - mp);
                        mp = nextStar + 1;
                        while (!stmt.empty() && (stmt.front() == ' ' || stmt.front() == '\t' || stmt.front() == '\r' || stmt.front() == '\n')) stmt.remove_prefix(1);
                        if (stmt.empty() || stmt[0] == '0') continue; // '0 ...' is macro comment
                        // Variable assignment $K=<expr> or primitive code,e1,e2,...
                        if (stmt[0] == '$') continue;
                        GerberMacroPrim gp;
                        size_t cp = 0;
                        bool firstField = true;
                        while (cp <= stmt.size()) {
                            size_t comma = stmt.find(',', cp);
                            if (comma == std::string_view::npos) comma = stmt.size();
                            std::string field(stmt.substr(cp, comma - cp));
                            // Strip newlines inside expressions
                            field.erase(std::remove_if(field.begin(), field.end(), [](char ch) { return ch == '\r' || ch == '\n'; }), field.end());
                            if (firstField) {
                                gp.code = std::atoi(field.c_str());
                                firstField = false;
                            } else {
                                gp.exprs.push_back(std::move(field));
                            }
                            if (comma == stmt.size()) break;
                            cp = comma + 1;
                        }
                        if (gp.code > 0) prims.push_back(std::move(gp));
                    }
                    macroTemplates[macroName] = std::move(prims);
                }
                continue;
            }

            // Other %-commands may contain one or more *-terminated statements
            size_t sp = 0;
            while (sp < extBlock.size()) {
                size_t star = extBlock.find('*', sp);
                if (star == std::string_view::npos) star = extBlock.size();
                std::string_view cmd = extBlock.substr(sp, star - sp);
                sp = star + 1;
                while (!cmd.empty() && (cmd.front() == ' ' || cmd.front() == '\t' || cmd.front() == '\r' || cmd.front() == '\n')) cmd.remove_prefix(1);
                if (cmd.empty()) continue;

                if (cmd == "MOIN") {
                    metric = false;
                } else if (cmd == "MOMM") {
                    metric = true;
                } else if (cmd.rfind("FS", 0) == 0) {
                    hasFS = true;
                    for (size_t i = 2; i < cmd.size(); ++i) {
                        char c = cmd[i];
                        if (c == 'L') trailingZeroOmit = false;
                        else if (c == 'T') trailingZeroOmit = true;
                        else if (c == 'A') incrementalCoords = false;
                        else if (c == 'I') incrementalCoords = true;
                        else if (c == 'X' && i + 2 < cmd.size() && cmd[i + 1] >= '0' && cmd[i + 1] <= '7' && cmd[i + 2] >= '0' && cmd[i + 2] <= '7') {
                            xInt = cmd[i + 1] - '0';
                            xDec = cmd[i + 2] - '0';
                            i += 2;
                        } else if (c == 'Y' && i + 2 < cmd.size() && cmd[i + 1] >= '0' && cmd[i + 1] <= '7' && cmd[i + 2] >= '0' && cmd[i + 2] <= '7') {
                            yInt = cmd[i + 1] - '0';
                            yDec = cmd[i + 2] - '0';
                            i += 2;
                        }
                    }
                } else if (cmd.rfind("LPD", 0) == 0) {
                    if (inRegion) flushContour();
                    clearPolarity = false;
                } else if (cmd.rfind("LPC", 0) == 0) {
                    if (inRegion) flushContour();
                    clearPolarity = true;
                } else if (cmd.rfind("LM", 0) == 0 && cmd.size() >= 3) {
                    std::string_view m = cmd.substr(2);
                    mirrorX = (m.find('X') != std::string_view::npos);
                    mirrorY = (m.find('Y') != std::string_view::npos);
                } else if (cmd.rfind("LR", 0) == 0 && cmd.size() > 2) {
                    char buf[32] = {};
                    std::memcpy(buf, cmd.data() + 2, std::min<size_t>(cmd.size() - 2, 31));
                    loadRotDeg = std::strtof(buf, nullptr);
                } else if (cmd.rfind("LS", 0) == 0 && cmd.size() > 2) {
                    char buf[32] = {};
                    std::memcpy(buf, cmd.data() + 2, std::min<size_t>(cmd.size() - 2, 31));
                    float sc = std::strtof(buf, nullptr);
                    if (sc > 0.001f && sc < 1000.0f) loadScale = sc;
                } else if (cmd.rfind("SR", 0) == 0) {
                    finishSR();
                    if (cmd.size() > 2) {
                        int nx = 1, ny = 1;
                        double dx = 0.0, dy = 0.0;
                        auto parseSrNum = [&](char ch) -> double {
                            size_t p2 = cmd.find(ch, 2);
                            if (p2 == std::string_view::npos) return 0.0;
                            size_t e2 = p2 + 1;
                            while (e2 < cmd.size() && ((cmd[e2] >= '0' && cmd[e2] <= '9') || cmd[e2] == '.' || cmd[e2] == '-' || cmd[e2] == '+')) ++e2;
                            char b[48] = {};
                            std::memcpy(b, cmd.data() + p2 + 1, std::min<size_t>(e2 - (p2 + 1), 47));
                            return std::strtod(b, nullptr);
                        };
                        nx = std::max(1, static_cast<int>(parseSrNum('X')));
                        ny = std::max(1, static_cast<int>(parseSrNum('Y')));
                        dx = parseSrNum('I') * (metric ? 1.0 : 25.4);
                        dy = parseSrNum('J') * (metric ? 1.0 : 25.4);
                        if (nx > 1 || ny > 1) {
                            inSR = true;
                            srNx = nx; srNy = ny;
                            srDxMm = static_cast<float>(dx);
                            srDyMm = static_cast<float>(dy);
                            srTraceStart = out.traces.size();
                            srPadStart = out.pads.size();
                            srCircleStart = out.circles.size();
                        }
                    }
                } else if (cmd.rfind("TF", 0) == 0) {
                    // Gerber X2 File Attribute: %TF.FileFunction,Copper,L1,Top*%
                    if (cmd.rfind("TF.FileFunction,", 0) == 0) {
                        std::string_view val = cmd.substr(16);
                        x2FuncLabel = PathUtils::Utf8ToWide(val);
                        if (val.find("Copper") != std::string_view::npos) {
                            if (val.find("Bot") != std::string_view::npos) activeLayer = EdaLayerId::BottomCopper;
                            else if (val.find("Inr") != std::string_view::npos) activeLayer = EdaLayerId::InnerCopper;
                            else activeLayer = EdaLayerId::TopCopper;
                        } else if (val.find("Legend") != std::string_view::npos) {
                            activeLayer = (val.find("Bot") != std::string_view::npos) ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
                        } else if (val.find("Profile") != std::string_view::npos) {
                            activeLayer = EdaLayerId::EdgeCuts;
                        } else if (val.find("Plated") != std::string_view::npos || val.find("NonPlated") != std::string_view::npos) {
                            activeLayer = EdaLayerId::Drills;
                        }
                    }
                } else if (cmd.rfind("TO.", 0) == 0) {
                    // Gerber X2 Object Attribute: %TO.N,VCC*% / %TO.C,U1*% / %TO.P,U1,1*%
                    if (cmd.rfind("TO.N,", 0) == 0) {
                        curNetName = std::string(cmd.substr(5));
                        if (!curNetName.empty() && curNetName != "N/C" && !netNameIds.count(curNetName)) {
                            int nextId = static_cast<int>(netNameIds.size() + 1);
                            netNameIds[curNetName] = nextId;
                        }
                    } else if (cmd.rfind("TO.C,", 0) == 0) {
                        curCompRef = std::string(cmd.substr(5));
                    } else if (cmd.rfind("TO.P,", 0) == 0) {
                        std::string_view rest = cmd.substr(5);
                        size_t cm = rest.find(',');
                        if (cm != std::string_view::npos) {
                            curCompRef = std::string(rest.substr(0, cm));
                            curPinNum = std::string(rest.substr(cm + 1));
                        }
                    }
                } else if (cmd.rfind("TD", 0) == 0) {
                    if (cmd == "TD") { curNetName.clear(); curCompRef.clear(); curPinNum.clear(); }
                    else if (cmd.find("TO.N") != std::string_view::npos) curNetName.clear();
                    else if (cmd.find("TO.C") != std::string_view::npos) curCompRef.clear();
                    else if (cmd.find("TO.P") != std::string_view::npos) curPinNum.clear();
                } else if (cmd.rfind("ADD", 0) == 0 && cmd.size() >= 6) {
                    // %ADD10C,0.25*% or %ADD12R,1.2X0.8*% or %ADD15MyMacro,0.5X1.0*%
                    size_t dp = 3;
                    int dCode = 0;
                    while (dp < cmd.size() && cmd[dp] >= '0' && cmd[dp] <= '9') {
                        dCode = dCode * 10 + (cmd[dp] - '0');
                        ++dp;
                    }
                    if (dCode >= 10 && dp < cmd.size()) {
                        size_t comma = cmd.find(',', dp);
                        std::string apType(cmd.substr(dp, (comma == std::string_view::npos ? cmd.size() : comma) - dp));
                        std::vector<double> mods;
                        if (comma != std::string_view::npos) {
                            size_t mp2 = comma + 1;
                            while (mp2 <= cmd.size()) {
                                size_t nxX = cmd.find_first_of("Xx", mp2);
                                if (nxX == std::string_view::npos) nxX = cmd.size();
                                char mb[48] = {};
                                std::memcpy(mb, cmd.data() + mp2, std::min<size_t>(nxX - mp2, 47));
                                mods.push_back(std::strtod(mb, nullptr));
                                if (nxX == cmd.size()) break;
                                mp2 = nxX + 1;
                            }
                        }
                        const float uMm = metric ? 1.0f : 25.4f;
                        GerberApertureDef ap;
                        if (apType == "C") {
                            ap.kind = 0;
                            float d = !mods.empty() ? static_cast<float>(mods[0]) * uMm : 0.25f;
                            ap.w = ap.h = std::max(0.02f, d);
                            if (mods.size() >= 2) ap.drill = std::max(0.0f, static_cast<float>(mods[1]) * uMm);
                        } else if (apType == "R") {
                            ap.kind = 1;
                            float rw = !mods.empty() ? static_cast<float>(mods[0]) : 0.25f;
                            float rh = mods.size() >= 2 ? static_cast<float>(mods[1]) : rw;
                            ap.w = std::max(0.02f, rw * uMm);
                            ap.h = std::max(0.02f, rh * uMm);
                            if (mods.size() >= 3) ap.drill = std::max(0.0f, static_cast<float>(mods[2]) * uMm);
                        } else if (apType == "O") {
                            ap.kind = 3;
                            float ow = !mods.empty() ? static_cast<float>(mods[0]) : 0.25f;
                            float oh = mods.size() >= 2 ? static_cast<float>(mods[1]) : ow;
                            ap.w = std::max(0.02f, ow * uMm);
                            ap.h = std::max(0.02f, oh * uMm);
                            if (mods.size() >= 3) ap.drill = std::max(0.0f, static_cast<float>(mods[2]) * uMm);
                        } else if (apType == "P") {
                            ap.kind = 2;
                            float od = std::max(0.05f, (!mods.empty() ? static_cast<float>(mods[0]) : 0.5f) * uMm);
                            ap.w = ap.h = od;
                            ap.polyVerts = (mods.size() >= 2) ? std::clamp(static_cast<int>(mods[1]), 3, 24) : 6;
                            if (mods.size() >= 3) ap.rotDeg = static_cast<float>(mods[2]);
                            if (mods.size() >= 4) ap.drill = std::max(0.0f, static_cast<float>(mods[3]) * uMm);
                        } else if (macroTemplates.count(apType)) {
                            ap.kind = 4;
                            float maxExt = 0.25f;
                            for (const auto& mp : macroTemplates[apType]) {
                                std::vector<double> ev;
                                ev.reserve(mp.exprs.size());
                                for (const auto& ex : mp.exprs) ev.push_back(EvalGerberExpr(ex, mods));
                                auto rotPt = [](float px, float py, float deg, float& rx, float& ry) {
                                    float r = deg * 3.14159265f / 180.0f;
                                    rx = px * std::cos(r) - py * std::sin(r);
                                    ry = px * std::sin(r) + py * std::cos(r);
                                };
                                if (mp.code == 1 && ev.size() >= 4 && ev[0] > 0.5) { // Circle: exp, diam, cx, cy[, rot]
                                    float d = std::max(0.04f, static_cast<float>(ev[1]) * uMm);
                                    float cx = static_cast<float>(ev[2]) * uMm;
                                    float cy = static_cast<float>(ev[3]) * uMm;
                                    if (ev.size() >= 5 && std::fabs(ev[4]) > 1e-3) rotPt(cx, cy, static_cast<float>(ev[4]), cx, cy);
                                    EdaPadPrim p{}; p.x = cx; p.y = cy; p.w = d; p.h = d; p.shape = 0; p.layer = activeLayer;
                                    ap.macroPads.push_back(p);
                                    maxExt = std::max(maxExt, std::hypot(cx, cy) + d * 0.5f);
                                } else if ((mp.code == 20 || mp.code == 2) && ev.size() >= 7 && ev[0] > 0.5) { // Vector Line
                                    float w = std::max(0.04f, static_cast<float>(ev[1]) * uMm);
                                    float x1 = static_cast<float>(ev[2]) * uMm, y1 = static_cast<float>(ev[3]) * uMm;
                                    float x2 = static_cast<float>(ev[4]) * uMm, y2 = static_cast<float>(ev[5]) * uMm;
                                    float deg = static_cast<float>(ev[6]);
                                    rotPt(x1, y1, deg, x1, y1);
                                    rotPt(x2, y2, deg, x2, y2);
                                    ap.macroSegs.push_back({ x1, y1, x2, y2, w, activeLayer, 0 });
                                    maxExt = std::max({ maxExt, std::hypot(x1, y1) + w, std::hypot(x2, y2) + w });
                                } else if (mp.code == 21 && ev.size() >= 6 && ev[0] > 0.5) { // Center Rect: exp, w, h, cx, cy, rot
                                    float rw = std::max(0.04f, static_cast<float>(ev[1]) * uMm);
                                    float rh = std::max(0.04f, static_cast<float>(ev[2]) * uMm);
                                    float cx = static_cast<float>(ev[3]) * uMm;
                                    float cy = static_cast<float>(ev[4]) * uMm;
                                    float deg = static_cast<float>(ev[5]);
                                    rotPt(cx, cy, deg, cx, cy);
                                    EdaPadPrim p{}; p.x = cx; p.y = cy; p.w = rw; p.h = rh; p.rotDeg = deg; p.shape = 1; p.layer = activeLayer;
                                    ap.macroPads.push_back(p);
                                    maxExt = std::max(maxExt, std::hypot(cx, cy) + std::max(rw, rh) * 0.5f);
                                } else if (mp.code == 4 && ev.size() >= 8 && ev[0] > 0.5) { // Outline: exp, nPts, x0,y0..xn,yn, rot
                                    int nPts = static_cast<int>(ev[1]);
                                    float deg = static_cast<float>(ev.back());
                                    if (nPts >= 2 && 2 + static_cast<size_t>(nPts + 1) * 2 <= ev.size()) {
                                        for (int k = 0; k < nPts; ++k) {
                                            float x1 = static_cast<float>(ev[2 + static_cast<size_t>(k) * 2]) * uMm;
                                            float y1 = static_cast<float>(ev[3 + static_cast<size_t>(k) * 2]) * uMm;
                                            float x2 = static_cast<float>(ev[2 + static_cast<size_t>(k + 1) * 2]) * uMm;
                                            float y2 = static_cast<float>(ev[3 + static_cast<size_t>(k + 1) * 2]) * uMm;
                                            rotPt(x1, y1, deg, x1, y1);
                                            rotPt(x2, y2, deg, x2, y2);
                                            ap.macroSegs.push_back({ x1, y1, x2, y2, 0.14f, activeLayer, 0 });
                                            maxExt = std::max(maxExt, std::hypot(x1, y1));
                                        }
                                    }
                                } else if (mp.code == 5 && ev.size() >= 6 && ev[0] > 0.5) { // Polygon: exp, nVerts, cx, cy, diam, rot
                                    int nv = std::clamp(static_cast<int>(ev[1]), 3, 24);
                                    float cx = static_cast<float>(ev[2]) * uMm;
                                    float cy = static_cast<float>(ev[3]) * uMm;
                                    float r = std::max(0.03f, static_cast<float>(ev[4]) * uMm * 0.5f);
                                    float deg = static_cast<float>(ev[5]);
                                    for (int k = 0; k < nv; ++k) {
                                        float a0 = (deg * 3.14159265f / 180.0f) + 6.2831853f * static_cast<float>(k) / static_cast<float>(nv);
                                        float a1 = (deg * 3.14159265f / 180.0f) + 6.2831853f * static_cast<float>(k + 1) / static_cast<float>(nv);
                                        ap.macroSegs.push_back({ cx + std::cos(a0) * r, cy + std::sin(a0) * r,
                                                                 cx + std::cos(a1) * r, cy + std::sin(a1) * r, r * 0.45f, activeLayer, 0 });
                                    }
                                    maxExt = std::max(maxExt, std::hypot(cx, cy) + r);
                                } else if (mp.code == 7 && ev.size() >= 6) { // Thermal: cx, cy, outerD, innerD, gap, rot
                                    float cx = static_cast<float>(ev[0]) * uMm;
                                    float cy = static_cast<float>(ev[1]) * uMm;
                                    float rMid = static_cast<float>(ev[2] + ev[3]) * 0.25f * uMm;
                                    float ringW = std::max(0.06f, static_cast<float>(ev[2] - ev[3]) * 0.5f * uMm);
                                    ap.macroCircles.push_back({ cx, cy, std::max(0.08f, rMid), ringW, false, activeLayer });
                                    maxExt = std::max(maxExt, std::hypot(cx, cy) + rMid + ringW);
                                }
                            }
                            ap.w = ap.h = maxExt * 2.0f;
                        }
                        apertures[dCode] = std::move(ap);
                        curApertureCode = dCode;
                    }
                }
            }
            continue;
        }

        // Standard data block terminated by '*'
        size_t star = txt.find('*', pos);
        if (star == std::string_view::npos) {
            // Trailing whitespace already skipped; non-empty unterminated statement without '*'
            break;
        }
        std::string_view blk = txt.substr(pos, star - pos);
        pos = star + 1;

        // Strip CR/LF inside block
        std::string clean;
        clean.reserve(blk.size());
        for (char c : blk) {
            if (c != '\r' && c != '\n' && c != '\t') clean.push_back(c);
        }
        std::string_view ln(clean);
        while (!ln.empty() && ln.front() == ' ') ln.remove_prefix(1);
        if (ln.empty()) continue;

        if (ln.rfind("G04", 0) == 0 || ln.rfind("G4", 0) == 0) { sawGerberSyntax = true; continue; }
        if (ln == "M02" || ln == "M00" || ln == "M2") {
            sawGerberSyntax = true;
            if (inRegion) { flushContour(); inRegion = false; }
            finishSR();
            break;
        }
        if (ln == "G70") { metric = false; sawGerberSyntax = true; continue; }
        if (ln == "G71") { metric = true;  sawGerberSyntax = true; continue; }
        if (ln == "G74") { multiQuadrant = false; sawGerberSyntax = true; continue; }
        if (ln == "G75") { multiQuadrant = true;  sawGerberSyntax = true; continue; }
        if (ln == "G90") { incrementalCoords = false; sawGerberSyntax = true; continue; }
        if (ln == "G91") { incrementalCoords = true;  sawGerberSyntax = true; continue; }
        if (ln == "G36") { inRegion = true; contourPts.clear(); sawGerberSyntax = true; continue; }
        if (ln == "G37") { flushContour(); inRegion = false; sawGerberSyntax = true; continue; }
        if (ln == "G01" || ln == "G1") { interpMode = 1; sawGerberSyntax = true; continue; }
        if (ln == "G02" || ln == "G2") { interpMode = 2; sawGerberSyntax = true; continue; }
        if (ln == "G03" || ln == "G3") { interpMode = 3; sawGerberSyntax = true; continue; }

        // Strip inline G01/G02/G03/G54 prefixes before coordinates or D-codes
        if (ln.rfind("G54", 0) == 0) { ln.remove_prefix(3); sawGerberSyntax = true; }
        if (ln.rfind("G01", 0) == 0) { interpMode = 1; ln.remove_prefix(3); sawGerberSyntax = true; }
        else if (ln.rfind("G1", 0) == 0 && ln.size() > 2 && (ln[2] < '0' || ln[2] > '9')) { interpMode = 1; ln.remove_prefix(2); sawGerberSyntax = true; }
        else if (ln.rfind("G02", 0) == 0) { interpMode = 2; ln.remove_prefix(3); sawGerberSyntax = true; }
        else if (ln.rfind("G2", 0) == 0 && ln.size() > 2 && (ln[2] < '0' || ln[2] > '9')) { interpMode = 2; ln.remove_prefix(2); sawGerberSyntax = true; }
        else if (ln.rfind("G03", 0) == 0) { interpMode = 3; ln.remove_prefix(3); sawGerberSyntax = true; }
        else if (ln.rfind("G3", 0) == 0 && ln.size() > 2 && (ln[2] < '0' || ln[2] > '9')) { interpMode = 3; ln.remove_prefix(2); sawGerberSyntax = true; }

        // Standalone aperture select D10..D9999
        if (!ln.empty() && ln[0] == 'D' && ln.find('X') == std::string_view::npos && ln.find('Y') == std::string_view::npos) {
            int dc = std::atoi(std::string(ln.substr(1)).c_str());
            if (dc >= 10) {
                curApertureCode = dc;
                sawGerberSyntax = true;
                continue;
            }
        }

        // Parse X, Y, I, J, D fields inside statement
        const double unitMm = metric ? 1.0 : 25.4;
        bool hasX = false, hasY = false, hasI = false, hasJ = false, hasD = false;
        float valX = 0.0f, valY = 0.0f, valI = 0.0f, valJ = 0.0f;
        int dCode = modalDCode;

        size_t fp = 0;
        bool badToken = false;
        while (fp < ln.size()) {
            char code = ln[fp++];
            if (code == ' ') continue;
            size_t ns = fp;
            while (fp < ln.size() && ((ln[fp] >= '0' && ln[fp] <= '9') || ln[fp] == '-' || ln[fp] == '+' || ln[fp] == '.')) ++fp;
            std::string_view digits = ln.substr(ns, fp - ns);
            if (code == 'X') {
                if (ParseGerberCoordField(digits, xInt, xDec, trailingZeroOmit, unitMm, valX)) hasX = true;
            } else if (code == 'Y') {
                if (ParseGerberCoordField(digits, yInt, yDec, trailingZeroOmit, unitMm, valY)) hasY = true;
            } else if (code == 'I') {
                if (ParseGerberCoordField(digits, xInt, xDec, trailingZeroOmit, unitMm, valI)) hasI = true;
            } else if (code == 'J') {
                if (ParseGerberCoordField(digits, yInt, yDec, trailingZeroOmit, unitMm, valJ)) hasJ = true;
            } else if (code == 'D' && !digits.empty()) {
                dCode = 0;
                for (char dc : digits) if (dc >= '0' && dc <= '9') dCode = dCode * 10 + (dc - '0');
                hasD = true;
            } else if (code == 'G') {
                int gc = std::atoi(std::string(digits).c_str());
                if (gc == 1) interpMode = 1;
                else if (gc == 2) interpMode = 2;
                else if (gc == 3) interpMode = 3;
                else if (gc == 74) multiQuadrant = false;
                else if (gc == 75) multiQuadrant = true;
            } else {
                badToken = true;
                break;
            }
        }
        if (badToken) continue;

        if (hasD && dCode >= 10 && !hasX && !hasY) {
            curApertureCode = dCode;
            sawGerberSyntax = true;
            continue;
        }
        if (!hasX && !hasY && !hasI && !hasJ && !hasD) continue;
        sawGerberSyntax = true;

        float nx = curX, ny = curY;
        if (hasX) nx = incrementalCoords ? (curX + valX) : valX;
        if (hasY) ny = incrementalCoords ? (curY + valY) : valY;
        if (dCode >= 1 && dCode <= 3) modalDCode = dCode;

        const GerberApertureDef& ap = apertures.count(curApertureCode) ? apertures[curApertureCode] : defaultAp;
        const EdaLayerId drawLayer = clearPolarity ? EdaLayerId::EdgeCuts : activeLayer;
        const int netId = curNetName.empty() ? 0 : netNameIds[curNetName];

        if (inRegion) {
            if (dCode == 2) {
                flushContour();
                contourPts.push_back({ nx, ny });
            } else if (dCode == 1) {
                if (contourPts.empty()) contourPts.push_back({ curX, curY });
                if ((interpMode == 2 || interpMode == 3) && (hasI || hasJ)) {
                    emitArcPoints(curX, curY, nx, ny, valI, valJ, interpMode == 2, contourPts);
                } else {
                    contourPts.push_back({ nx, ny });
                }
            }
        } else if (dCode == 3) {
            // Flash aperture at (nx, ny)
            if (!curCompRef.empty()) {
                auto& ca = x2Comps[curCompRef];
                ca.sumX += nx; ca.sumY += ny; ca.pins++;
                ca.minX = std::min(ca.minX, nx - ap.w * 0.5f);
                ca.maxX = std::max(ca.maxX, nx + ap.w * 0.5f);
                ca.minY = std::min(ca.minY, ny - ap.h * 0.5f);
                ca.maxY = std::max(ca.maxY, ny + ap.h * 0.5f);
            }
            if (ap.kind == 4) {
                // Stamp macro primitives with %LM/%LR/%LS transforms
                for (const auto& mp : ap.macroPads) {
                    if (out.pads.size() >= 80000) break;
                    float ox = 0.0f, oy = 0.0f;
                    xformLocal(mp.x, mp.y, ox, oy);
                    EdaPadPrim p = mp;
                    p.x = nx + ox; p.y = ny + oy;
                    p.w *= loadScale; p.h *= loadScale;
                    p.rotDeg += loadRotDeg;
                    p.layer = drawLayer;
                    if (!curPinNum.empty()) p.pinName = PathUtils::Utf8ToWide(curPinNum);
                    if (!curNetName.empty()) p.netName = PathUtils::Utf8ToWide(curNetName);
                    out.pads.push_back(std::move(p));
                }
                for (const auto& ms : ap.macroSegs) {
                    if (out.traces.size() >= 120000) break;
                    float ox1 = 0.0f, oy1 = 0.0f, ox2 = 0.0f, oy2 = 0.0f;
                    xformLocal(ms.x1, ms.y1, ox1, oy1);
                    xformLocal(ms.x2, ms.y2, ox2, oy2);
                    out.traces.push_back({ nx + ox1, ny + oy1, nx + ox2, ny + oy2, ms.width * loadScale, drawLayer, netId });
                }
                for (const auto& mc : ap.macroCircles) {
                    if (out.circles.size() >= 40000) break;
                    float ocx = 0.0f, ocy = 0.0f;
                    xformLocal(mc.cx, mc.cy, ocx, ocy);
                    out.circles.push_back({ nx + ocx, ny + ocy, mc.radius * loadScale, mc.width * loadScale, mc.filled, drawLayer });
                }
            } else if (ap.kind == 2 && ap.polyVerts >= 3) {
                // Regular polygon flash: pad center + polygon perimeter edges
                float r = ap.w * 0.5f * loadScale;
                float baseRad = (ap.rotDeg + loadRotDeg) * 3.14159265f / 180.0f;
                for (int k = 0; k < ap.polyVerts && out.traces.size() < 120000; ++k) {
                    float a0 = baseRad + 6.2831853f * static_cast<float>(k) / static_cast<float>(ap.polyVerts);
                    float a1 = baseRad + 6.2831853f * static_cast<float>(k + 1) / static_cast<float>(ap.polyVerts);
                    out.traces.push_back({ nx + std::cos(a0) * r, ny + std::sin(a0) * r,
                                           nx + std::cos(a1) * r, ny + std::sin(a1) * r, std::max(0.10f, r * 0.45f), drawLayer, netId });
                }
                if (out.pads.size() < 80000) {
                    EdaPadPrim p{};
                    p.x = nx; p.y = ny; p.w = ap.w * loadScale * 0.75f; p.h = ap.h * loadScale * 0.75f;
                    p.rotDeg = ap.rotDeg + loadRotDeg; p.drill = ap.drill * loadScale; p.shape = 0; p.layer = drawLayer;
                    if (!curPinNum.empty()) p.pinName = PathUtils::Utf8ToWide(curPinNum);
                    if (!curNetName.empty()) p.netName = PathUtils::Utf8ToWide(curNetName);
                    out.pads.push_back(std::move(p));
                }
            } else if (out.pads.size() < 80000) {
                EdaPadPrim p{};
                p.x = nx; p.y = ny;
                p.w = ap.w * loadScale;
                p.h = ap.h * loadScale;
                p.rotDeg = ap.rotDeg + loadRotDeg;
                p.drill = ap.drill * loadScale;
                p.shape = ap.kind; // 0=circle, 1=rect, 3=oval
                p.layer = drawLayer;
                if (!curPinNum.empty()) p.pinName = PathUtils::Utf8ToWide(curPinNum);
                if (!curNetName.empty()) p.netName = PathUtils::Utf8ToWide(curNetName);
                out.pads.push_back(std::move(p));
            }
        } else if (dCode == 1) {
            float strokeW = std::max(0.06f, ap.w * loadScale);
            if ((interpMode == 2 || interpMode == 3) && (hasI || hasJ || !multiQuadrant)) {
                std::vector<std::pair<float, float>> arcPts;
                arcPts.push_back({ curX, curY });
                emitArcPoints(curX, curY, nx, ny, valI, valJ, interpMode == 2, arcPts);
                for (size_t k = 0; k + 1 < arcPts.size() && out.traces.size() < 120000; ++k) {
                    out.traces.push_back({ arcPts[k].first, arcPts[k].second,
                                           arcPts[k + 1].first, arcPts[k + 1].second, strokeW, drawLayer, netId });
                }
            } else if (out.traces.size() < 120000) {
                out.traces.push_back({ curX, curY, nx, ny, strokeW, drawLayer, netId });
            }
        }

        curX = nx;
        curY = ny;
    }

    if (inRegion) flushContour();
    finishSR();

    // Synthesize X2 components when %TO.C attributes were present
    for (const auto& kv : x2Comps) {
        if (kv.second.pins <= 0 || out.components.size() >= 4000) continue;
        EdaComponent3D c;
        c.refDes = PathUtils::Utf8ToWide(kv.first);
        c.x = kv.second.sumX / static_cast<float>(kv.second.pins);
        c.y = kv.second.sumY / static_cast<float>(kv.second.pins);
        c.widthMm = std::max(1.2f, kv.second.maxX - kv.second.minX);
        c.lengthMm = std::max(1.0f, kv.second.maxY - kv.second.minY);
        c.pinCount = kv.second.pins;
        c.bottomSide = (activeLayer == EdaLayerId::BottomCopper || activeLayer == EdaLayerId::BottomSilk);
        c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
        out.components.push_back(std::move(c));
    }

    if ((!hasFS && apertures.empty() && !sawGerberSyntax) ||
        (out.traces.empty() && out.pads.empty() && out.circles.empty() && out.vias.empty())) {
        out = ParsedEdaBoard{};
        return false;
    }

    if (!x2FuncLabel.empty()) {
        out.formatTitle = L"Gerber X2 (" + x2FuncLabel + L")";
    } else {
        out.formatTitle = L"Gerber RS-274X (" + PathUtils::ToUpper(ext.empty() ? L".GBR" : ext) + L")";
    }
    out.netCount = std::max<size_t>(1, netNameIds.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}
