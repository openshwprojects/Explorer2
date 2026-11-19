#pragma once
// DeviceTreeDtb.h — Zero-dependency C++20 Flattened Device Tree Blob (.dtb, Devicetree Spec v0.4 0xD00DFEED) SoC diagram reader.
// Included inside `namespace Explorer2` in src/EdaReaders.cpp.

namespace Formats {

namespace DetailFdtDtb {

inline void DrawBoxRect(ParsedEdaBoard& out, float x0, float y0, float x1, float y1, float strokeW, EdaLayerId lid, int netId = 0) {
    out.traces.push_back({ x0, y0, x1, y0, strokeW, lid, netId });
    out.traces.push_back({ x1, y0, x1, y1, strokeW, lid, netId });
    out.traces.push_back({ x1, y1, x0, y1, strokeW, lid, netId });
    out.traces.push_back({ x0, y1, x0, y0, strokeW, lid, netId });
}

inline std::wstring FormatHex64(uint64_t val) {
    char buf[32] = {};
    if (val > 0xFFFFFFFFULL) {
        std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(val));
    } else {
        std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned int>(val));
    }
    return PathUtils::Utf8ToWide(buf);
}

inline std::wstring FormatByteSpan(uint64_t sz) {
    char buf[40] = {};
    if (sz >= (1024ULL * 1024ULL * 1024ULL) && (sz % (1024ULL * 1024ULL * 1024ULL)) == 0ULL) {
        std::snprintf(buf, sizeof(buf), "%llu GB", static_cast<unsigned long long>(sz >> 30));
    } else if (sz >= (1024ULL * 1024ULL)) {
        if ((sz % (1024ULL * 1024ULL)) == 0ULL) {
            std::snprintf(buf, sizeof(buf), "%llu MB", static_cast<unsigned long long>(sz >> 20));
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(sz) / (1024.0 * 1024.0));
        }
    } else if (sz >= 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%llu KB", static_cast<unsigned long long>(sz >> 10));
    } else {
        std::snprintf(buf, sizeof(buf), "0x%llX B", static_cast<unsigned long long>(sz));
    }
    return PathUtils::Utf8ToWide(buf);
}

struct FdtProp {
    std::string name;
    const uint8_t* data = nullptr;
    uint32_t len = 0;
};

struct FdtNode {
    std::string name;
    std::string path;
    std::vector<FdtProp> props;
    std::vector<FdtNode> children;

    const FdtProp* FindProp(std::string_view key) const {
        for (const auto& p : props) {
            if (p.name == key) return &p;
        }
        return nullptr;
    }

    bool ReadU32(std::string_view key, uint32_t& outVal) const {
        const FdtProp* p = FindProp(key);
        if (!p || p->len < 4u) return false;
        outVal = ByteIO::RdU32BE(p->data);
        return true;
    }

    uint64_t ReadU32OrU64(std::string_view key, uint64_t defVal = 0ULL) const {
        const FdtProp* p = FindProp(key);
        if (!p) return defVal;
        if (p->len >= 8u) return ByteIO::RdU64BE(p->data);
        if (p->len >= 4u) return static_cast<uint64_t>(ByteIO::RdU32BE(p->data));
        return defVal;
    }

    std::string ReadString(std::string_view key) const {
        const FdtProp* p = FindProp(key);
        if (!p || p->len == 0u) return {};
        std::string s;
        for (uint32_t i = 0; i < p->len; ++i) {
            uint8_t c = p->data[i];
            if (c == 0) break;
            if (c >= 32u && c < 127u) s.push_back(static_cast<char>(c));
            else return {};
        }
        return s;
    }

    std::vector<std::string> ReadStringList(std::string_view key) const {
        std::vector<std::string> res;
        const FdtProp* p = FindProp(key);
        if (!p || p->len == 0u) return res;
        uint32_t i = 0;
        while (i < p->len && res.size() < 16u) {
            std::string cur;
            while (i < p->len && p->data[i] != 0) {
                uint8_t c = p->data[i++];
                if (c >= 32u && c < 127u) cur.push_back(static_cast<char>(c));
            }
            if (i < p->len && p->data[i] == 0) ++i;
            if (!cur.empty()) res.push_back(std::move(cur));
        }
        return res;
    }
};

struct FdtDecodedReg {
    uint64_t addr = 0;
    uint64_t size = 0;
};

struct FdtPeriphCard {
    std::string name;
    std::string busName;
    std::vector<std::string> compatibles;
    std::vector<FdtDecodedReg> regs;
    std::vector<uint32_t> irqs;
    uint64_t clockHz = 0;
    uint32_t phandle = 0;
    bool disabled = false;
};

struct FdtMemCard {
    std::string name;
    uint64_t base = 0;
    uint64_t size = 0;
    bool isReserved = false;
};

struct FdtCpuInfo {
    std::string name;
    std::string isaOrComp;
    uint64_t clockHz = 0;
    uint64_t regId = 0;
};

inline std::vector<FdtDecodedReg> DecodeFdtRegs(const FdtNode& node, uint32_t addrCells, uint32_t sizeCells) {
    std::vector<FdtDecodedReg> out;
    const FdtProp* rp = node.FindProp("reg");
    if (!rp || rp->len < 4u) return out;
    uint32_t strideCells = addrCells + sizeCells;
    if (strideCells == 0u || strideCells > 8u) return out;
    uint32_t strideBytes = strideCells * 4u;
    for (uint32_t off = 0; off + strideBytes <= rp->len && out.size() < 8u; off += strideBytes) {
        uint64_t a = 0, s = 0;
        for (uint32_t k = 0; k < addrCells; ++k) {
            a = (a << 32) | static_cast<uint64_t>(ByteIO::RdU32BE(rp->data + off + k * 4u));
        }
        for (uint32_t k = 0; k < sizeCells; ++k) {
            s = (s << 32) | static_cast<uint64_t>(ByteIO::RdU32BE(rp->data + off + (addrCells + k) * 4u));
        }
        out.push_back({ a, s });
    }
    return out;
}

inline std::vector<uint32_t> DecodeFdtIrqs(const FdtNode& node) {
    std::vector<uint32_t> irqs;
    const FdtProp* ip = node.FindProp("interrupts");
    if (ip && ip->len >= 4u) {
        uint32_t nCells = ip->len / 4u;
        if (nCells >= 3u && (nCells % 3u) == 0u && ByteIO::RdU32BE(ip->data) <= 1u) {
            // ARM GIC 3-cell specifier: <type irq_num flags>
            for (uint32_t i = 0; i + 2u < nCells && irqs.size() < 8u; i += 3u) {
                irqs.push_back(ByteIO::RdU32BE(ip->data + (i + 1u) * 4u));
            }
        } else if (nCells >= 2u && (nCells % 2u) == 0u && ByteIO::RdU32BE(ip->data + 4u) <= 8u && ByteIO::RdU32BE(ip->data) > 0u) {
            // 2-cell <irq_num trigger_flags>
            for (uint32_t i = 0; i + 1u < nCells && irqs.size() < 8u; i += 2u) {
                irqs.push_back(ByteIO::RdU32BE(ip->data + i * 4u));
            }
        } else {
            for (uint32_t i = 0; i < nCells && irqs.size() < 8u; ++i) {
                irqs.push_back(ByteIO::RdU32BE(ip->data + i * 4u));
            }
        }
        return irqs;
    }
    const FdtProp* iext = node.FindProp("interrupts-extended");
    if (iext && iext->len >= 8u) {
        uint32_t nCells = iext->len / 4u;
        for (uint32_t i = 1u; i < nCells && irqs.size() < 8u; i += 2u) {
            irqs.push_back(ByteIO::RdU32BE(iext->data + i * 4u));
        }
    }
    return irqs;
}

inline bool IsBusContainerName(std::string_view lowName, const std::vector<std::string>& comps, bool hasChildren) {
    if (!hasChildren) return false;
    for (const auto& c : comps) {
        std::string lc = PathUtils::ToLowerAscii(c);
        if (lc == "simple-bus" || lc == "simple-mfd" || lc.find("amba") != std::string::npos ||
            lc.find("axi") != std::string::npos || lc.find("ahb") != std::string::npos ||
            lc.find("apb") != std::string::npos) {
            return true;
        }
    }
    auto base = lowName.substr(0, lowName.find('@'));
    return base == "soc" || base == "amba" || base == "amba_pl" || base == "bus" ||
           base == "ahb" || base == "apb" || base == "axi" || base == "soc0" ||
           base == "pci" || base == "pcie";
}

inline void CollectFdtTopology(
    const FdtNode& node,
    uint32_t parentAddrCells,
    uint32_t parentSizeCells,
    const std::string& currentBus,
    bool inCpus,
    bool inResMem,
    std::vector<FdtCpuInfo>& outCpus,
    std::vector<FdtMemCard>& outMems,
    std::vector<std::string>& outBuses,
    std::vector<FdtPeriphCard>& outPeriphs)
{
    uint32_t myChildAddrCells = parentAddrCells;
    uint32_t myChildSizeCells = parentSizeCells;
    uint32_t tmp = 0;
    if (node.ReadU32("#address-cells", tmp) && tmp <= 4u) myChildAddrCells = tmp;
    if (node.ReadU32("#size-cells", tmp) && tmp <= 4u) myChildSizeCells = tmp;

    std::string lowName = PathUtils::ToLowerAscii(node.name);
    std::string baseName = lowName.substr(0, lowName.find('@'));
    std::string devType = PathUtils::ToLowerAscii(node.ReadString("device_type"));
    auto comps = node.ReadStringList("compatible");
    auto regs = DecodeFdtRegs(node, parentAddrCells, parentSizeCells);
    std::string status = PathUtils::ToLowerAscii(node.ReadString("status"));
    bool disabled = (status == "disabled" || status == "fail" || status.rfind("fail-", 0) == 0);

    if (!node.path.empty() && node.path != "/") {
        if (baseName == "chosen" || baseName == "aliases" || baseName == "__symbols__" ||
            baseName == "__fixups__" || baseName == "__local_fixups__") {
            return;
        }
        if (baseName == "cpus") {
            for (const auto& ch : node.children) {
                CollectFdtTopology(ch, myChildAddrCells, myChildSizeCells, currentBus, true, false,
                                   outCpus, outMems, outBuses, outPeriphs);
            }
            return;
        }
        if (baseName == "reserved-memory") {
            for (const auto& ch : node.children) {
                CollectFdtTopology(ch, myChildAddrCells, myChildSizeCells, currentBus, false, true,
                                   outCpus, outMems, outBuses, outPeriphs);
            }
            return;
        }

        if (inCpus || devType == "cpu" || baseName == "cpu") {
            if (baseName.rfind("cpu-map", 0) != 0 && baseName != "l2-cache") {
                FdtCpuInfo ci;
                ci.name = node.name;
                ci.isaOrComp = node.ReadString("riscv,isa");
                if (ci.isaOrComp.empty() && !comps.empty()) ci.isaOrComp = comps[0];
                ci.clockHz = node.ReadU32OrU64("clock-frequency", node.ReadU32OrU64("timebase-frequency", 0ULL));
                if (!regs.empty()) ci.regId = regs[0].addr;
                outCpus.push_back(std::move(ci));
            }
            return;
        }

        if (inResMem || devType == "memory" || baseName == "memory" || baseName == "sram") {
            if (!regs.empty()) {
                for (const auto& r : regs) {
                    outMems.push_back({ node.name, r.addr, r.size, inResMem });
                }
            }
            return;
        }

        if (IsBusContainerName(lowName, comps, !node.children.empty())) {
            std::string busLabel = node.name;
            if (std::find(outBuses.begin(), outBuses.end(), busLabel) == outBuses.end()) {
                outBuses.push_back(busLabel);
            }
            // If PCI host controller itself also has MMIO reg/compatible and leaf properties, emit card if PCI
            if ((baseName == "pci" || baseName == "pcie" || devType == "pci") && !regs.empty()) {
                FdtPeriphCard pc;
                pc.name = node.name;
                pc.busName = currentBus.empty() ? "root" : currentBus;
                pc.compatibles = comps;
                pc.regs = regs;
                pc.irqs = DecodeFdtIrqs(node);
                pc.clockHz = node.ReadU32OrU64("clock-frequency", 0ULL);
                node.ReadU32("phandle", pc.phandle);
                if (pc.phandle == 0u) node.ReadU32("linux,phandle", pc.phandle);
                pc.disabled = disabled;
                outPeriphs.push_back(std::move(pc));
            }
            for (const auto& ch : node.children) {
                CollectFdtTopology(ch, myChildAddrCells, myChildSizeCells, busLabel, false, false,
                                   outCpus, outMems, outBuses, outPeriphs);
            }
            return;
        }

        if (!regs.empty() || !comps.empty()) {
            FdtPeriphCard pc;
            pc.name = node.name;
            pc.busName = currentBus.empty() ? "soc" : currentBus;
            pc.compatibles = std::move(comps);
            pc.regs = std::move(regs);
            pc.irqs = DecodeFdtIrqs(node);
            pc.clockHz = node.ReadU32OrU64("clock-frequency", 0ULL);
            node.ReadU32("phandle", pc.phandle);
            if (pc.phandle == 0u) node.ReadU32("linux,phandle", pc.phandle);
            pc.disabled = disabled;
            outPeriphs.push_back(std::move(pc));
        }
    }

    for (const auto& ch : node.children) {
        CollectFdtTopology(ch, myChildAddrCells, myChildSizeCells, currentBus, false, false,
                           outCpus, outMems, outBuses, outPeriphs);
    }
}

} // namespace DetailFdtDtb

inline bool ParseFlattenedDeviceTreeBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 40u) return false;
    if (ByteIO::RdU32BE(data) != 0xD00DFEEDu) return false;

    const uint32_t totalSize   = ByteIO::RdU32BE(data + 4);
    const uint32_t offStruct   = ByteIO::RdU32BE(data + 8);
    const uint32_t offStrings  = ByteIO::RdU32BE(data + 12);
    const uint32_t offMemRsv   = ByteIO::RdU32BE(data + 16);
    const uint32_t version     = ByteIO::RdU32BE(data + 20);
    const uint32_t lastCompVer = ByteIO::RdU32BE(data + 24);

    if (totalSize < 40u || totalSize > size || totalSize > 64u * 1024u * 1024u) return false;
    if (version < 16u || lastCompVer > version) return false;
    if (offStruct < 40u || (offStruct & 3u) != 0u || offStruct >= totalSize) return false;
    if (offStrings >= totalSize) return false;
    if (offMemRsv < 28u || offMemRsv > totalSize) return false;

    size_t structEnd = totalSize;
    if (version >= 17u) {
        uint32_t szStruct = ByteIO::RdU32BE(data + 36);
        if (szStruct < 4u || static_cast<uint64_t>(offStruct) + szStruct > totalSize) return false;
        structEnd = static_cast<size_t>(offStruct) + szStruct;
    }
    size_t stringsEnd = totalSize;
    if (version >= 3u) {
        uint32_t szStrings = ByteIO::RdU32BE(data + 32);
        if (static_cast<uint64_t>(offStrings) + szStrings > totalSize) return false;
        stringsEnd = static_cast<size_t>(offStrings) + szStrings;
    }

    // 1. Parse /memreserve/ table
    std::vector<DetailFdtDtb::FdtMemCard> memReserves;
    {
        size_t rp = offMemRsv;
        while (rp + 16u <= totalSize && memReserves.size() < 64u) {
            uint64_t rAddr = ByteIO::RdU64BE(data + rp);
            uint64_t rSize = ByteIO::RdU64BE(data + rp + 8u);
            rp += 16u;
            if (rAddr == 0ULL && rSize == 0ULL) break;
            memReserves.push_back({ "memreserve", rAddr, rSize, true });
        }
    }

    // 2. Parse FDT Structure Block into tree
    DetailFdtDtb::FdtNode root;
    root.path = "/";
    std::vector<DetailFdtDtb::FdtNode*> stack;
    size_t pos = offStruct;
    bool sawRoot = false;
    bool sawEnd  = false;
    size_t nodeCount = 0;

    while (pos + 4u <= structEnd) {
        uint32_t tok = ByteIO::RdU32BE(data + pos);
        pos += 4u;

        if (tok == 1u) { // FDT_BEGIN_NODE
            size_t s = pos;
            while (pos < structEnd && data[pos] != 0) ++pos;
            if (pos >= structEnd) return false; // Unterminated node name
            std::string nm(reinterpret_cast<const char*>(data + s), pos - s);
            ++pos;
            pos = (pos + 3u) & ~static_cast<size_t>(3u);

            if (++nodeCount > 16384u || stack.size() >= 32u) return false;

            if (!sawRoot) {
                if (!stack.empty()) return false;
                sawRoot = true;
                root.name = nm;
                stack.push_back(&root);
            } else {
                if (stack.empty()) return false;
                DetailFdtDtb::FdtNode child;
                child.name = std::move(nm);
                child.path = (stack.back()->path == "/") ? ("/" + child.name) : (stack.back()->path + "/" + child.name);
                stack.back()->children.push_back(std::move(child));
                stack.push_back(&stack.back()->children.back());
            }
        } else if (tok == 2u) { // FDT_END_NODE
            if (stack.empty()) return false;
            stack.pop_back();
        } else if (tok == 3u) { // FDT_PROP
            if (stack.empty() || pos + 8u > structEnd) return false;
            uint32_t pLen    = ByteIO::RdU32BE(data + pos);
            uint32_t nameOff = ByteIO::RdU32BE(data + pos + 4u);
            pos += 8u;
            if (pLen > structEnd - pos) return false;

            size_t sp = static_cast<size_t>(offStrings) + nameOff;
            if (sp >= stringsEnd) return false;
            size_t se = sp;
            while (se < stringsEnd && data[se] != 0 && se - sp < 256u) ++se;
            if (se >= stringsEnd || data[se] != 0) return false;

            DetailFdtDtb::FdtProp prop;
            prop.name.assign(reinterpret_cast<const char*>(data + sp), se - sp);
            prop.data = data + pos;
            prop.len  = pLen;
            stack.back()->props.push_back(std::move(prop));

            pos += (static_cast<size_t>(pLen) + 3u) & ~static_cast<size_t>(3u);
        } else if (tok == 4u) { // FDT_NOP
            continue;
        } else if (tok == 9u) { // FDT_END
            if (!sawRoot || !stack.empty()) return false;
            sawEnd = true;
            break;
        } else {
            return false; // Invalid FDT token
        }
    }

    if (!sawEnd || !stack.empty()) return false;
    if (root.children.empty() && memReserves.empty()) return false;

    // 3. Extract SoC Hardware Topology with inherited #address-cells (default 2) & #size-cells (default 1)
    std::string modelName = root.ReadString("model");
    auto rootComps = root.ReadStringList("compatible");
    if (modelName.empty() && !rootComps.empty()) modelName = rootComps[0];

    std::vector<DetailFdtDtb::FdtCpuInfo> cpus;
    std::vector<DetailFdtDtb::FdtMemCard> mems = memReserves;
    std::vector<std::string> buses;
    std::vector<DetailFdtDtb::FdtPeriphCard> periphs;

    DetailFdtDtb::CollectFdtTopology(root, 2u, 1u, "", false, false, cpus, mems, buses, periphs);
    if (cpus.empty() && mems.empty() && periphs.empty()) return false;

    std::stable_sort(periphs.begin(), periphs.end(),
        [](const DetailFdtDtb::FdtPeriphCard& a, const DetailFdtDtb::FdtPeriphCard& b) {
            bool ha = !a.regs.empty(), hb = !b.regs.empty();
            if (ha != hb) return ha > hb;
            if (ha && a.regs[0].addr != b.regs[0].addr) return a.regs[0].addr < b.regs[0].addr;
            return a.name < b.name;
        });

    out.kind = EdaDocKind::KiCadSch;
    std::wstring title = L"Flattened Device Tree SoC (.dtb";
    if (!modelName.empty()) title += L" — " + PathUtils::Utf8ToWide(modelName);
    title += L", " + std::to_wstring(cpus.size()) + L" CPU, " +
             std::to_wstring(mems.size()) + L" mem, " +
             std::to_wstring(periphs.size()) + L" periph)";
    out.formatTitle = title;

    std::set<uint32_t> uniqueIrqs;
    for (const auto& p : periphs) {
        for (uint32_t irq : p.irqs) uniqueIrqs.insert(irq);
    }
    const bool anyIrqs = !uniqueIrqs.empty();
    const float busX = 0.0f;
    const float irqRailX = 3.4f;

    // 4. Render CPU Cluster Header Block
    const float coreW = 46.0f;
    const float coreH = std::max(10.5f, 6.2f + static_cast<float>(std::min<size_t>(cpus.size(), 4u)) * 2.1f);
    const float coreCx = 0.0f;
    const float coreCy = 18.0f;
    const float cx0 = coreCx - coreW * 0.5f, cx1 = coreCx + coreW * 0.5f;
    const float cy0 = coreCy - coreH * 0.5f, cy1 = coreCy + coreH * 0.5f;

    DetailFdtDtb::DrawBoxRect(out, cx0, cy0, cx1, cy1, 0.38f, EdaLayerId::SchSymbol, 0);
    DetailFdtDtb::DrawBoxRect(out, cx0 + 0.8f, cy0 + 0.8f, cx1 - 0.8f, cy1 - 0.8f, 0.16f, EdaLayerId::SchSymbol, 0);

    std::string topHdr = modelName.empty() ? "Devicetree SoC" : modelName;
    if (!rootComps.empty() && rootComps[0] != modelName) topHdr += " [" + rootComps[0] + "]";
    out.labels.push_back({ cx0 + 1.6f, cy1 - 2.3f, 1.05f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(topHdr) });

    if (!cpus.empty()) {
        for (size_t i = 0; i < std::min<size_t>(cpus.size(), 4u); ++i) {
            const auto& c = cpus[i];
            std::string cLine = c.name;
            if (!c.isaOrComp.empty()) cLine += " (" + c.isaOrComp + ")";
            if (c.clockHz >= 1000000ULL) {
                char fb[32] = {};
                std::snprintf(fb, sizeof(fb), " @ %llu MHz", static_cast<unsigned long long>(c.clockHz / 1000000ULL));
                cLine += fb;
            }
            out.labels.push_back({ cx0 + 2.0f, cy1 - 4.7f - static_cast<float>(i) * 2.0f, 0.84f, EdaLayerId::TopSilk,
                                   PathUtils::Utf8ToWide(cLine) });
        }
        EdaComponent3D cpuComp;
        cpuComp.refDes = PathUtils::Utf8ToWide(cpus[0].name);
        cpuComp.value  = PathUtils::Utf8ToWide(cpus[0].isaOrComp.empty() ? topHdr : cpus[0].isaOrComp);
        cpuComp.footprint = L"FDT_CPU_CLUSTER";
        cpuComp.x = coreCx;
        cpuComp.y = coreCy;
        cpuComp.widthMm = coreW;
        cpuComp.lengthMm = coreH;
        cpuComp.heightMm = 1.6f;
        cpuComp.pinCount = std::max<int>(2, static_cast<int>(cpus.size()) + 1);
        cpuComp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(cpuComp));
    }

    std::string busTitle = buses.empty() ? "soc / axi" : buses[0];
    for (size_t bi = 1; bi < std::min<size_t>(buses.size(), 3u); ++bi) busTitle += "/" + buses[bi];
    out.pads.push_back({ busX, cy0, 0.75f, 0.75f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"M_BUS", PathUtils::Utf8ToWide(busTitle) });
    out.labels.push_back({ busX - 7.5f, cy0 - 1.7f, 0.85f, EdaLayerId::SchWire, PathUtils::Utf8ToWide(busTitle) });

    if (anyIrqs) {
        out.pads.push_back({ irqRailX, cy0, 0.70f, 0.70f, 0.0f, 0.0f, 0, EdaLayerId::InnerCopper, L"IRQ_CTRL", L"IRQ_BUS" });
        out.labels.push_back({ irqRailX + 0.9f, cy0 - 1.7f, 0.82f, EdaLayerId::InnerCopper, L"IRQ" });
    }

    float curLeftY  = cy0 - 3.5f;
    float curRightY = cy0 - 3.5f;
    float lowestTapY = cy0 - 2.0f;
    float lowestIrqY = cy0;

    // 5. Render Memory Regions on Left Column Top
    const size_t nMems = std::min<size_t>(mems.size(), 8u);
    for (size_t mi = 0; mi < nMems; ++mi) {
        const auto& m = mems[mi];
        const float cardW = 31.0f;
        const float cardH = 7.4f;
        const float yTop = curLeftY;
        const float yBot = yTop - cardH;
        const float bx   = -8.5f - cardW * 0.5f;
        const float x0   = bx - cardW * 0.5f;
        const float x1   = bx + cardW * 0.5f;
        curLeftY = yBot - 2.8f;

        EdaLayerId mLayer = m.isReserved ? EdaLayerId::BottomSilk : EdaLayerId::TopCopper;
        DetailFdtDtb::DrawBoxRect(out, x0, yBot, x1, yTop, 0.30f, mLayer, 1);

        std::wstring mTitle = PathUtils::Utf8ToWide(m.name) + (m.isReserved ? L" [RSVD]" : L" [RAM]");
        std::wstring mSpan  = DetailFdtDtb::FormatHex64(m.base) + L" (" + DetailFdtDtb::FormatByteSpan(m.size) + L")";
        out.labels.push_back({ x0 + 1.0f, yTop - 2.2f, 0.92f, EdaLayerId::SchLabel, mTitle });
        out.labels.push_back({ x0 + 1.0f, yTop - 4.8f, 0.82f, EdaLayerId::TopSilk, mSpan });

        float tapY = yTop - 2.6f;
        out.traces.push_back({ busX, tapY, x1, tapY, 0.30f, EdaLayerId::SchWire, 1 });
        out.circles.push_back({ busX, tapY, 0.42f, 0.22f, true, EdaLayerId::SchWire });
        out.pads.push_back({ x1, tapY, 0.65f, 0.65f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol,
                             DetailFdtDtb::FormatHex64(m.base), L"MEM_BUS" });
        lowestTapY = std::min(lowestTapY, tapY);

        if (!m.isReserved) {
            EdaComponent3D mc;
            mc.refDes = PathUtils::Utf8ToWide(m.name);
            mc.value  = mSpan;
            mc.footprint = L"FDT_MEMORY";
            mc.x = bx;
            mc.y = (yTop + yBot) * 0.5f;
            mc.widthMm = cardW;
            mc.lengthMm = cardH;
            mc.heightMm = 1.2f;
            mc.pinCount = 2;
            mc.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(mc));
        }
    }

    // 6. Render MMIO Peripheral Cards along SoC Bus Spine
    const size_t nPer = std::min<size_t>(periphs.size(), 48u);
    for (size_t pi = 0; pi < nPer; ++pi) {
        const auto& sp = periphs[pi];
        const bool rightSide = (curRightY >= curLeftY);
        float& colTopY = rightSide ? curRightY : curLeftY;

        const float cardW = 31.0f;
        const float cardH = 9.2f;
        const float yTop  = colTopY;
        const float yBot  = yTop - cardH;
        const float bx    = rightSide ? (8.5f + cardW * 0.5f) : (-8.5f - cardW * 0.5f);
        const float x0    = bx - cardW * 0.5f;
        const float x1    = bx + cardW * 0.5f;
        colTopY = yBot - 2.8f;

        EdaLayerId cardLayer   = sp.disabled ? EdaLayerId::BottomSilk : EdaLayerId::SchSymbol;
        EdaLayerId accentLayer = sp.disabled ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
        DetailFdtDtb::DrawBoxRect(out, x0, yBot, x1, yTop, 0.30f, cardLayer, 0);
        out.traces.push_back({ x0, yTop - 3.0f, x1, yTop - 3.0f, 0.20f, accentLayer, static_cast<int>(pi + 2u) });

        std::wstring pHdr = PathUtils::Utf8ToWide(sp.name);
        if (sp.disabled) pHdr += L" [DISABLED]";
        out.labels.push_back({ x0 + 1.0f, yTop - 2.0f, 0.92f, cardLayer, pHdr });

        std::wstring addrLine = L"no-reg";
        if (!sp.regs.empty()) {
            addrLine = DetailFdtDtb::FormatHex64(sp.regs[0].addr);
            if (sp.regs[0].size > 0ULL) {
                addrLine += L" [" + DetailFdtDtb::FormatByteSpan(sp.regs[0].size) + L"]";
            }
        }
        out.labels.push_back({ x0 + 1.0f, yTop - 5.0f, 0.80f, accentLayer, addrLine });

        std::string drvStr = sp.compatibles.empty() ? sp.busName : sp.compatibles[0];
        if (sp.phandle != 0u) drvStr += " (ph:" + std::to_string(sp.phandle) + ")";
        out.labels.push_back({ x0 + 1.0f, yTop - 7.3f, 0.76f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(drvStr) });

        const float tapY = yTop - 2.2f;
        const float edgeX = rightSide ? x0 : x1;
        out.traces.push_back({ busX, tapY, edgeX, tapY, 0.26f, EdaLayerId::SchWire, 1 });
        out.circles.push_back({ busX, tapY, 0.40f, 0.20f, true, EdaLayerId::SchWire });
        out.pads.push_back({ edgeX, tapY, 0.62f, 0.62f, 0.0f, 0.0f, 1, cardLayer,
                             sp.regs.empty() ? PathUtils::Utf8ToWide(sp.name) : DetailFdtDtb::FormatHex64(sp.regs[0].addr),
                             PathUtils::Utf8ToWide(sp.busName) });
        lowestTapY = std::min(lowestTapY, tapY);

        if (!sp.irqs.empty() && anyIrqs) {
            const float irqY = tapY - 1.8f;
            out.traces.push_back({ irqRailX, irqY, edgeX, irqY, 0.20f, EdaLayerId::InnerCopper,
                                   static_cast<int>(2u + (sp.irqs[0] & 0xFFu)) });
            out.circles.push_back({ irqRailX, irqY, 0.34f, 0.18f, true, EdaLayerId::InnerCopper });
            std::wstring irqTxt = L"IRQ" + std::to_wstring(sp.irqs[0]);
            out.pads.push_back({ edgeX, irqY, 0.55f, 0.55f, 0.0f, 0.0f, 0, EdaLayerId::InnerCopper, irqTxt, L"IRQ_BUS" });
            out.labels.push_back({ std::min(irqRailX, edgeX) + 0.6f, irqY + 0.35f, 0.68f, EdaLayerId::InnerCopper, irqTxt });
            lowestIrqY = std::min(lowestIrqY, irqY);
        }

        EdaComponent3D pc;
        pc.refDes = PathUtils::Utf8ToWide(sp.name);
        pc.value  = PathUtils::Utf8ToWide(drvStr);
        pc.footprint = sp.disabled ? L"FDT_PERIPH_DISABLED" : L"FDT_PERIPH";
        pc.x = bx;
        pc.y = (yTop + yBot) * 0.5f;
        pc.widthMm = cardW;
        pc.lengthMm = cardH;
        pc.heightMm = 1.3f;
        pc.bottomSide = sp.disabled;
        pc.pinCount = std::max<int>(1, 1 + static_cast<int>(sp.irqs.size()));
        pc.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(pc));
    }

    // Draw Central SoC Bus Spine & IRQ Rail
    out.traces.push_back({ busX, cy0, busX, lowestTapY - 2.0f, 0.55f, EdaLayerId::SchWire, 1 });
    if (anyIrqs && lowestIrqY < cy0) {
        out.traces.push_back({ irqRailX, cy0, irqRailX, lowestIrqY - 1.6f, 0.35f, EdaLayerId::InnerCopper, 2 });
    }

    out.netCount = 1u + uniqueIrqs.size() + buses.size();
    return !out.components.empty() && !out.traces.empty();
}

} // namespace Formats
