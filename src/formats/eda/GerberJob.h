#pragma once
// GerberJob.h — Ucamco Gerber X2 Job Manifest (.gbrjob) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

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
