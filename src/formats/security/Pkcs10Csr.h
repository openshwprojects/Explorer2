#pragma once
// Pkcs10Csr.h — PKCS#10 Certificate Signing Request (.csr) Reader

namespace Formats {

inline bool ParsePkcs10CsrDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::vector<DetailX509::DerBlock> blocks;
    bool isPem = false;
    if (!DetailX509::ExtractPemOrDerBlocks(raw, blocks, isPem)) return false;

    auto rows = DetailX509::InitPkiHeaderRows(isPem, blocks.size(), extLower);
    bool parsedAny = false;
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto& blk = blocks[bi];
        const std::wstring prefix = (blocks.size() > 1) ? (L"[#" + std::to_wstring(bi + 1) + L"] ") : L"";
        rows.push_back({ prefix + L"Block Type", blk.label + L" (" + PathUtils::FormatByteSize(blk.der.size()) + L")" });
            uint8_t t = 0; size_t hl = 0, vl = 0;
            if (blk.der[0] == 0x30 && DetailX509::ReadDerTlv(blk.der.data(), blk.der.size(), t, hl, vl)) {
                std::wstring lastOid;
                DetailX509::WalkAsn1Der(blk.der.data(), blk.der.size(), 0, lastOid, rows);
                parsedAny = true;
            }
    }
    if (!parsedAny) return false;
    return DetailX509::FinalizePkiDoc(L"PKCS#10 Certificate Signing Request (" + extLower + L")", std::move(rows), out);
}

} // namespace Formats
