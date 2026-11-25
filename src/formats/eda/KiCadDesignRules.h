#pragma once
// KiCadDesignRules.h — KiCad Custom Design Rules (.kicad_dru)
// Parses KiCad 6–9 custom DRC rule scripts:
//   (version 1)
//   (rule "<name>" (layer ...) (severity ...) (constraint <type> (min ...) (opt ...) (max ...)) (condition "..."))
// Renders an interactive DRC Copper Gauge & Via/Clearance Coupon Sheet in ParsedEdaBoard.

namespace Formats {

namespace DetailKiCadDru {

inline bool ParseDruValueMm(const DetailSpecctra::SNode& n, double& outMm) {
    if (n.isList || n.atom.empty()) return false;
    const std::string& s = n.atom;
    char* endp = nullptr;
    double val = std::strtod(s.c_str(), &endp);
    if (endp == s.c_str() || !std::isfinite(val)) return false;
    std::string_view unit(endp, static_cast<size_t>((s.c_str() + s.size()) - endp));
    while (!unit.empty() && unit.front() == ' ') unit.remove_prefix(1);
    if (unit.empty() || unit == "mm") {
        outMm = val;
        return true;
    }
    if (unit == "mil" || unit == "mils" || unit == "th") {
        outMm = val * 0.0254;
        return true;
    }
    if (unit == "in" || unit == "inch") {
        outMm = val * 25.4;
        return true;
    }
    if (unit == "um") {
        outMm = val * 0.001;
        return true;
    }
    if (unit == "deg" || unit == "%") {
        outMm = val;
        return true;
    }
    return false;
}

inline EdaLayerId MapDruLayer(std::string_view layerSpec) {
    if (layerSpec.find("B.Cu") != std::string_view::npos || layerSpec == "bottom") {
        return EdaLayerId::BottomCopper;
    }
    if (layerSpec.find("In") != std::string_view::npos || layerSpec == "inner") {
        return EdaLayerId::InnerCopper;
    }
    if (layerSpec.find("Silk") != std::string_view::npos) {
        return EdaLayerId::TopSilk;
    }
    if (layerSpec.find("Edge") != std::string_view::npos) {
        return EdaLayerId::EdgeCuts;
    }
    return EdaLayerId::TopCopper;
}

struct DruConstraint {
    std::string type;
    bool hasMin = false;
    bool hasOpt = false;
    bool hasMax = false;
    double minMm = 0.0;
    double optMm = 0.0;
    double maxMm = 0.0;
};

struct DruRule {
    std::string name;
    std::string layerSpec;
    std::string condition;
    std::string severity;
    std::vector<DruConstraint> constraints;
};

} // namespace DetailKiCadDru

inline bool ParseKiCadDesignRules(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 16u, (8u << 20))) return false;

    std::vector<DetailSpecctra::SNode> roots;
    if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.size() < 2) return false;

    // First root MUST be (version <positive_int>)
    const auto& verNode = roots[0];
    if (DetailSpecctra::HeadKey(verNode) != "version" || verNode.list.size() != 2 || verNode.list[1].isList) {
        return false;
    }
    double verNum = 0.0;
    if (!DetailSpecctra::AtomFloat(verNode.list[1], verNum) || verNum < 1.0 || verNum > 100.0) {
        return false;
    }

    std::vector<DetailKiCadDru::DruRule> rules;
    rules.reserve(roots.size() - 1);

    for (size_t i = 1; i < roots.size(); ++i) {
        const auto& rn = roots[i];
        if (DetailSpecctra::HeadKey(rn) != "rule" || rn.list.size() < 3 || rn.list[1].isList) {
            return false;
        }
        DetailKiCadDru::DruRule rule;
        rule.name = rn.list[1].atom;
        if (rule.name.empty()) return false;

        for (size_t j = 2; j < rn.list.size(); ++j) {
            const auto& attr = rn.list[j];
            std::string_view ak = DetailSpecctra::HeadKey(attr);
            if (ak == "layer" && attr.list.size() >= 2 && !attr.list[1].isList) {
                rule.layerSpec = attr.list[1].atom;
            } else if (ak == "condition" && attr.list.size() >= 2 && !attr.list[1].isList) {
                rule.condition = attr.list[1].atom;
            } else if (ak == "severity" && attr.list.size() >= 2 && !attr.list[1].isList) {
                rule.severity = attr.list[1].atom;
            } else if (ak == "constraint") {
                if (attr.list.size() < 2 || attr.list[1].isList || attr.list[1].atom.empty()) {
                    return false;
                }
                DetailKiCadDru::DruConstraint c;
                c.type = attr.list[1].atom;
                for (size_t k = 2; k < attr.list.size(); ++k) {
                    const auto& sub = attr.list[k];
                    std::string_view sk = DetailSpecctra::HeadKey(sub);
                    if (sk == "min" || sk == "opt" || sk == "max") {
                        if (sub.list.size() != 2) return false;
                        double mm = 0.0;
                        if (!DetailKiCadDru::ParseDruValueMm(sub.list[1], mm) || mm < 0.0 || mm > 10000.0) {
                            return false;
                        }
                        if (sk == "min") { c.hasMin = true; c.minMm = mm; }
                        if (sk == "opt") { c.hasOpt = true; c.optMm = mm; }
                        if (sk == "max") { c.hasMax = true; c.maxMm = mm; }
                    }
                }
                rule.constraints.push_back(std::move(c));
            } else {
                return false; // Unknown sub-clause inside (rule ...)
            }
        }

        if (rule.constraints.empty()) return false;
        rules.push_back(std::move(rule));
    }

    if (rules.empty()) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadPcb;
    out.netCount = rules.size();

    const float sheetW = 120.0f;
    const float rowH   = 18.0f;
    const float sheetH = 12.0f + static_cast<float>(rules.size()) * rowH;

    // Outer DRC coupon board outline
    DetailSiliconAndBusEda::AddRectOutline(out, sheetW * 0.5f, sheetH * 0.5f, sheetW, sheetH, EdaLayerId::EdgeCuts, 0.30f);
    out.labels.push_back({ 4.0f, sheetH - 5.0f, 2.2f, EdaLayerId::TopSilk,
                           L"KICAD DRC COUPON SHEET (" + std::to_wstring(rules.size()) + L" RULES)" });

    for (size_t idx = 0; idx < rules.size(); ++idx) {
        const auto& r = rules[idx];
        const float yCenter = sheetH - 16.0f - static_cast<float>(idx) * rowH;
        const EdaLayerId cuLayer = DetailKiCadDru::MapDruLayer(r.layerSpec);

        // Rule row divider
        out.traces.push_back({ 3.0f, yCenter - rowH * 0.45f, sheetW - 3.0f, yCenter - rowH * 0.45f,
                               0.12f, EdaLayerId::TopSilk, 0 });

        double trackW = 0.25;
        double gapMm  = 0.25;
        double viaDia = 0.0;
        double viaDrl = 0.0;
        bool hasTrackOrGap = false;
        std::string summary;

        for (const auto& c : r.constraints) {
            const double primary = c.hasOpt ? c.optMm : (c.hasMin ? c.minMm : c.maxMm);
            if (!summary.empty()) summary += ", ";
            summary += c.type;
            if (c.type == "track_width" || c.type == "thermal_spoke_width" || c.type == "annular_width") {
                trackW = std::clamp(primary > 0.0 ? primary : 0.2, 0.05, 6.0);
                hasTrackOrGap = true;
            } else if (c.type == "clearance" || c.type == "diff_pair_gap" ||
                       c.type == "edge_clearance" || c.type == "courtyard_clearance" ||
                       c.type == "hole_clearance" || c.type == "silk_clearance") {
                gapMm = std::clamp(primary > 0.0 ? primary : 0.2, 0.05, 8.0);
                hasTrackOrGap = true;
            } else if (c.type == "via_diameter" || c.type == "via_size" || c.type == "uvia_diameter") {
                viaDia = std::clamp(primary, 0.15, 6.0);
            } else if (c.type == "via_drill" || c.type == "hole_size" || c.type == "uvia_drill") {
                viaDrl = std::clamp(primary, 0.08, 5.0);
            } else if (c.type == "length" || c.type == "skew" || c.type == "diff_pair_uncoupled") {
                hasTrackOrGap = true;
                // Modulate meander segment height cleanly by primary length target
                gapMm = std::clamp(0.3 + std::fmod(primary, 4.0) * 0.5, 0.2, 4.0);
            }
        }

        // Rule title + condition label on left half
        std::wstring titleLbl = PathUtils::Utf8ToWide(r.name);
        if (!r.layerSpec.empty()) {
            titleLbl += L" [" + PathUtils::Utf8ToWide(r.layerSpec) + L"]";
        }
        out.labels.push_back({ 5.0f, yCenter + 2.2f, 1.6f, EdaLayerId::TopSilk, std::move(titleLbl) });
        out.labels.push_back({ 5.0f, yCenter - 2.2f, 1.2f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(summary) });

        // Physical copper trace-and-gap coupon on right half (X = 62..102)
        const float tw = static_cast<float>(std::clamp(trackW * 2.5, 0.18, 3.5));
        const float sep = tw + static_cast<float>(std::clamp(gapMm * 3.0, 0.35, 6.5));
        const int netId = static_cast<int>(idx + 1);
        out.traces.push_back({ 62.0f, yCenter + sep * 0.5f, 98.0f, yCenter + sep * 0.5f, tw, cuLayer, netId });
        if (hasTrackOrGap) {
            out.traces.push_back({ 62.0f, yCenter - sep * 0.5f, 98.0f, yCenter - sep * 0.5f, tw, cuLayer, netId });
        }

        // Emit via coupon if rule constrains via size or drill
        if (viaDia > 0.0 || viaDrl > 0.0) {
            EdaViaPrim via{};
            via.x = 108.0f;
            via.y = yCenter;
            via.diameter = static_cast<float>(viaDia > 0.0 ? viaDia * 2.0 : std::max(0.8, viaDrl * 3.0));
            via.drill = static_cast<float>(viaDrl > 0.0 ? viaDrl * 2.0 : via.diameter * 0.5f);
            via.netId = netId;
            out.vias.push_back(via);
        }

        // Emit rule component marker for BOM / inspector summary
        EdaComponent3D comp{};
        comp.refDes = L"DRC" + std::to_wstring(idx + 1);
        comp.value = PathUtils::Utf8ToWide(r.name);
        comp.footprint = PathUtils::Utf8ToWide(summary);
        comp.x = 52.0f;
        comp.y = yCenter;
        comp.widthMm = 6.0f;
        comp.lengthMm = 3.0f;
        comp.heightMm = 0.8f;
        comp.category = EdaPartCategory::GenericSMD;
        comp.pinCount = static_cast<int>(r.constraints.size());
        out.components.push_back(std::move(comp));
    }

    out.formatTitle = L"KiCad Custom Design Rules (.kicad_dru, v" +
                      std::to_wstring(static_cast<int>(verNum)) + L", " +
                      std::to_wstring(rules.size()) + L" rules)";
    return true;
}

} // namespace Formats
