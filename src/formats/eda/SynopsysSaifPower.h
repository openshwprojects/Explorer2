#pragma once
// SynopsysSaifPower.h — IEEE 1801 / Synopsys Switching Activity Interchange Format (.saif)
// Reuses `DetailSpecctra::ParseSExprTree` to parse `(SAIFILE ... (INSTANCE ... (NET/PORT ... (T0)(T1)(TX)(TC))))`
// and renders a VLSI Power Toggle-Rate & Static-Probability Heatmap.

namespace Formats {

namespace DetailSynopsysSaif {

struct SaifNetActivity {
    std::string instPath;
    std::string sigName;
    uint64_t t0 = 0ULL;
    uint64_t t1 = 0ULL;
    uint64_t tx = 0ULL;
    uint64_t tc = 0ULL;
};

inline void CollectSaifInstance(
    const DetailSpecctra::SNode& instNode,
    const std::string& parentPath,
    int depth,
    std::vector<SaifNetActivity>& outNets)
{
    if (depth > 32 || !instNode.isList || instNode.list.size() < 2u || instNode.list[1].isList) return;
    const std::string curPath = parentPath.empty()
        ? instNode.list[1].atom
        : (parentPath + "/" + instNode.list[1].atom);

    for (size_t i = 2u; i < instNode.list.size(); ++i) {
        const auto& ch = instNode.list[i];
        const std::string head = ReaderText::ToUpperAscii(DetailSpecctra::HeadKey(ch));
        if (head == "INSTANCE") {
            CollectSaifInstance(ch, curPath, depth + 1, outNets);
        } else if (head == "NET" || head == "PORT") {
            for (size_t ni = 1u; ni < ch.list.size(); ++ni) {
                const auto& sigNode = ch.list[ni];
                if (!sigNode.isList || sigNode.list.empty() || sigNode.list[0].isList) continue;
                SaifNetActivity na{};
                na.instPath = curPath;
                na.sigName  = sigNode.list[0].atom;
                for (size_t ai = 1u; ai < sigNode.list.size(); ++ai) {
                    const auto& attr = sigNode.list[ai];
                    if (!attr.isList || attr.list.size() < 2u || attr.list[1].isList) continue;
                    const std::string ak = ReaderText::ToUpperAscii(DetailSpecctra::HeadKey(attr));
                    uint64_t v = 0ULL;
                    if (!ReaderText::ParseUint64Sv(attr.list[1].atom, v)) continue;
                    if      (ak == "T0") na.t0 = v;
                    else if (ak == "T1") na.t1 = v;
                    else if (ak == "TX") na.tx = v;
                    else if (ak == "TC") na.tc = v;
                }
                if (outNets.size() < 4096u) outNets.push_back(std::move(na));
            }
        }
    }
}

} // namespace DetailSynopsysSaif

inline bool ParseSynopsysSaifPower(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 20u, (32u << 20))) return false;

    std::vector<DetailSpecctra::SNode> roots;
    if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.size() != 1u) return false;

    const auto& root = roots[0];
    if (ReaderText::ToUpperAscii(DetailSpecctra::HeadKey(root)) != "SAIFILE") return false;

    std::string designName;
    double duration = 0.0;
    bool sawSaifVer = false;
    std::vector<DetailSynopsysSaif::SaifNetActivity> nets;

    for (size_t i = 1u; i < root.list.size(); ++i) {
        const auto& ch = root.list[i];
        const std::string hk = ReaderText::ToUpperAscii(DetailSpecctra::HeadKey(ch));
        if (hk == "SAIFVERSION" && ch.list.size() >= 2u && !ch.list[1].isList) {
            sawSaifVer = true;
        } else if (hk == "DESIGN" && ch.list.size() >= 2u && !ch.list[1].isList) {
            designName = ch.list[1].atom;
        } else if (hk == "DURATION" && ch.list.size() >= 2u) {
            DetailSpecctra::AtomFloat(ch.list[1], duration);
        } else if (hk == "INSTANCE") {
            DetailSynopsysSaif::CollectSaifInstance(ch, "", 0, nets);
        }
    }

    if (!sawSaifVer || nets.empty()) return false;

    uint64_t maxTc = 1ULL;
    for (const auto& n : nets) maxTc = std::max(maxTc, n.tc);

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadPcb;

    const size_t cols = std::max<size_t>(1u, std::min<size_t>(4u, nets.size()));
    for (size_t idx = 0u; idx < nets.size(); ++idx) {
        const auto& n = nets[idx];
        const float cx = static_cast<float>(idx % cols) * 34.0f;
        const float cy = -static_cast<float>(idx / cols) * 14.0f;

        const double toggleRatio = static_cast<double>(n.tc) / static_cast<double>(maxTc);
        const EdaLayerId lid = (toggleRatio >= 0.66) ? EdaLayerId::TopCopper
                             : ((toggleRatio >= 0.25) ? EdaLayerId::InnerCopper : EdaLayerId::BottomCopper);

        DetailSiliconAndBusEda::AddRectOutline(out, cx, cy, 30.0f, 10.0f, lid, 0.28f);

        const uint64_t tSum = std::max<uint64_t>(1ULL, n.t0 + n.t1 + n.tx);
        const float duty1 = static_cast<float>(static_cast<double>(n.t1) / static_cast<double>(tSum));
        // Duty-cycle bar inside card
        const float barW = std::max(0.5f, duty1 * 26.0f);
        out.traces.push_back({ cx - 13.0f, cy - 2.6f, cx - 13.0f + barW, cy - 2.6f, 1.1f, lid, static_cast<int>(idx + 1u) });

        std::wstring fullSig = PathUtils::Utf8ToWide(n.instPath + "." + n.sigName);
        wchar_t stats[96] = {};
        swprintf_s(stats, L"TC=%llu  P1=%.0f%%", static_cast<unsigned long long>(n.tc), duty1 * 100.0f);
        out.labels.push_back({ cx - 13.5f, cy + 1.8f, 0.85f, EdaLayerId::TopSilk, fullSig });
        out.labels.push_back({ cx - 13.5f, cy - 0.5f, 0.78f, EdaLayerId::SchLabel, stats });

        EdaComponent3D c{};
        c.refDes = fullSig;
        c.value  = stats;
        c.footprint = L"SAIF_TOGGLE_CELL";
        c.x = cx; c.y = cy;
        c.widthMm = 30.0f; c.lengthMm = 10.0f;
        c.category = EdaPartCategory::GenericSMD;
        out.components.push_back(std::move(c));
    }

    out.netCount = nets.size();
    wchar_t title[200] = {};
    swprintf_s(title, L"IEEE 1801 / Synopsys SAIF Power Activity (.saif, %zu nets, dur=%.0f) — %ls",
               nets.size(), duration, PathUtils::Utf8ToWide(designName.empty() ? "ASIC" : designName).c_str());
    out.formatTitle = title;
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
