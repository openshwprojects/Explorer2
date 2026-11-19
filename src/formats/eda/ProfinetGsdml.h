#pragma once
// ProfinetGsdml.h — PROFINET IO GSDML Device Profile (.gsdml) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// #78. PROFINET IO GSDML Device Profile (.gsdml) — Backplane, DAP & IO Module Slots
inline bool ParseProfinetGsdml200(std::string_view sv, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    {
        if (sv.find("<ISO15745Profile") == std::string_view::npos &&
            sv.find("<ProfileBody") == std::string_view::npos) {
            return false;
        }

        auto xmlAttr = [](std::string_view tag, std::string_view attr) -> std::string {
            std::string_view v;
            return GetXmlAttr(tag, attr, v) ? std::string(v) : std::string{};
        };

        // 1. Build <ExternalTextList> <PrimaryLanguage> <Text TextId="..." Value="..."/> dictionary
        std::unordered_map<std::string, std::string> textDict;
        {
            size_t p = 0;
            while ((p = sv.find("<Text ", p)) != std::string_view::npos && textDict.size() < 4096u) {
                size_t gt = sv.find('>', p + 6);
                if (gt == std::string_view::npos) break;
                std::string_view tag = sv.substr(p + 1, gt - p - 1);
                std::string tid = xmlAttr(tag, "TextId");
                std::string val = xmlAttr(tag, "Value");
                if (!tid.empty() && !val.empty() && !textDict.count(tid)) {
                    textDict[tid] = val;
                }
                p = gt + 1;
            }
        }
        auto resolveText = [&](const std::string& tid, const std::string& fallback) -> std::string {
            if (!tid.empty()) {
                auto it = textDict.find(tid);
                if (it != textDict.end() && !it->second.empty()) return it->second;
            }
            return fallback;
        };

        // 2. Parse <DeviceIdentity> & <DeviceFunction>
        std::string vendorId, deviceId, vendorName, productFamily;
        {
            size_t dip = sv.find("<DeviceIdentity");
            if (dip != std::string_view::npos) {
                size_t digt = sv.find('>', dip);
                size_t dice = sv.find("</DeviceIdentity>", dip);
                if (digt != std::string_view::npos) {
                    std::string_view dtag = sv.substr(dip + 1, digt - dip - 1);
                    vendorId = xmlAttr(dtag, "VendorID");
                    deviceId = xmlAttr(dtag, "DeviceID");
                    if (dice != std::string_view::npos && dice > digt) {
                        std::string_view inner = sv.substr(digt + 1, dice - digt - 1);
                        size_t vnp = inner.find("<VendorName");
                        if (vnp != std::string_view::npos) {
                            size_t vgt = inner.find('>', vnp);
                            if (vgt != std::string_view::npos) vendorName = xmlAttr(inner.substr(vnp + 1, vgt - vnp - 1), "Value");
                        }
                    }
                }
            }
            size_t fmp = sv.find("<Family ");
            if (fmp != std::string_view::npos) {
                size_t fgt = sv.find('>', fmp);
                if (fgt != std::string_view::npos) {
                    std::string_view ftag = sv.substr(fmp + 1, fgt - fmp - 1);
                    productFamily = xmlAttr(ftag, "ProductFamily");
                    if (productFamily.empty()) productFamily = xmlAttr(ftag, "MainFamily");
                }
            }
        }

        auto calcIoBytes = [&](std::string_view ioSection) -> uint32_t {
            uint32_t bytes = 0;
            size_t dp = 0;
            while ((dp = ioSection.find("<DataItem", dp)) != std::string_view::npos) {
                size_t dgt = ioSection.find('>', dp);
                if (dgt == std::string_view::npos) break;
                std::string_view dtag = ioSection.substr(dp + 1, dgt - dp - 1);
                std::string dt = PathUtils::ToLowerAscii(xmlAttr(dtag, "DataType"));
                std::string lenStr = xmlAttr(dtag, "Length");
                uint32_t explicitLen = lenStr.empty() ? 0u : static_cast<uint32_t>(std::strtoul(lenStr.c_str(), nullptr, 10));
                if (explicitLen > 0u) bytes += explicitLen;
                else if (dt.find("64") != std::string::npos || dt == "float64") bytes += 8u;
                else if (dt.find("32") != std::string::npos || dt == "float32") bytes += 4u;
                else if (dt.find("16") != std::string::npos) bytes += 2u;
                else bytes += 1u;
                dp = dgt + 1;
            }
            return bytes;
        };

        auto parseNameAndIo = [&](std::string_view blk, std::string& outName, uint32_t& outInB, uint32_t& outOutB) {
            size_t np = blk.find("<Name ");
            if (np != std::string_view::npos) {
                size_t ngt = blk.find('>', np);
                if (ngt != std::string_view::npos) {
                    std::string tid = xmlAttr(blk.substr(np + 1, ngt - np - 1), "TextId");
                    outName = resolveText(tid, outName);
                }
            }
            size_t ip0 = blk.find("<Input");
            size_t ip1 = (ip0 != std::string_view::npos) ? blk.find("</Input>", ip0) : std::string_view::npos;
            if (ip0 != std::string_view::npos && ip1 != std::string_view::npos) {
                outInB += calcIoBytes(blk.substr(ip0, ip1 - ip0));
            }
            size_t op0 = blk.find("<Output");
            size_t op1 = (op0 != std::string_view::npos) ? blk.find("</Output>", op0) : std::string_view::npos;
            if (op0 != std::string_view::npos && op1 != std::string_view::npos) {
                outOutB += calcIoBytes(blk.substr(op0, op1 - op0));
            }
        };

        struct GsdSlot {
            bool isDap = false;
            std::string id;
            std::string name;
            std::string slotsAttr;
            std::string minInterval;
            uint32_t inBytes = 0;
            uint32_t outBytes = 0;
        };
        std::vector<GsdSlot> slots;

        // 3. Parse <DeviceAccessPointItem>
        {
            size_t p = 0;
            while ((p = sv.find("<DeviceAccessPointItem", p)) != std::string_view::npos && slots.size() < 16u) {
                size_t gt = sv.find('>', p);
                if (gt == std::string_view::npos) break;
                std::string_view openTag = sv.substr(p + 1, gt - p - 1);
                GsdSlot dap;
                dap.isDap = true;
                dap.id = xmlAttr(openTag, "ID");
                dap.name = dap.id;
                dap.slotsAttr = xmlAttr(openTag, "PhysicalSlots");
                if (dap.slotsAttr.empty()) dap.slotsAttr = xmlAttr(openTag, "ModuleSlots");
                dap.minInterval = xmlAttr(openTag, "MinDeviceInterval");

                if (sv[gt - 1] != '/') {
                    size_t ce = sv.find("</DeviceAccessPointItem>", gt + 1);
                    if (ce == std::string_view::npos) break;
                    parseNameAndIo(sv.substr(gt + 1, ce - gt - 1), dap.name, dap.inBytes, dap.outBytes);
                    p = ce + 24;
                } else {
                    p = gt + 1;
                }
                if (!dap.name.empty()) slots.push_back(std::move(dap));
            }
        }

        // 4. Parse <ModuleItem> (and standalone <VirtualSubmoduleItem> if no ModuleItem)
        {
            size_t p = 0;
            while ((p = sv.find("<ModuleItem", p)) != std::string_view::npos && slots.size() < 20u) {
                size_t gt = sv.find('>', p);
                if (gt == std::string_view::npos) break;
                std::string_view openTag = sv.substr(p + 1, gt - p - 1);
                GsdSlot mod;
                mod.isDap = false;
                mod.id = xmlAttr(openTag, "ID");
                mod.name = mod.id;

                if (sv[gt - 1] != '/') {
                    size_t ce = sv.find("</ModuleItem>", gt + 1);
                    if (ce == std::string_view::npos) break;
                    parseNameAndIo(sv.substr(gt + 1, ce - gt - 1), mod.name, mod.inBytes, mod.outBytes);
                    p = ce + 13;
                } else {
                    p = gt + 1;
                }
                if (!mod.name.empty()) slots.push_back(std::move(mod));
            }
        }

        // Zero-Cheat: reject empty GSDML profiles with zero DAPs and zero IO modules
        if (slots.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        std::string devHeader = !productFamily.empty() ? productFamily : (!vendorName.empty() ? vendorName : slots.front().name);
        if (!vendorId.empty() || !deviceId.empty()) {
            devHeader += " [" + vendorId + ":" + deviceId + "]";
        }
        out.formatTitle = L"PROFINET IO Device Profile — " + PathUtils::Utf8ToWide(devHeader) +
                          L" (" + std::to_wstring(slots.size()) + L" slots)";

        const float slotPitch = 20.0f;
        const float backplaneW = std::max(36.0f, static_cast<float>(slots.size()) * slotPitch);
        const float bpX0 = -backplaneW * 0.5f;
        const float bpX1 =  backplaneW * 0.5f;

        // PROFINET Industrial Ethernet top bus + IO Backplane rail
        out.traces.push_back({ bpX0, 14.0f, bpX1, 14.0f, 0.45f, EdaLayerId::SchWire, 1 });
        out.labels.push_back({ bpX0, 15.6f, 1.0f, EdaLayerId::SchWire, L"PROFINET RT/IRT Ethernet (100BASE-TX)" });

        out.traces.push_back({ bpX0,  6.0f, bpX1,  6.0f, 0.36f, EdaLayerId::InnerCopper, 2 });
        out.labels.push_back({ bpX0,  7.3f, 0.9f, EdaLayerId::SchLabel, L"IO Device Backplane Bus — " + PathUtils::Utf8ToWide(devHeader) });

        for (size_t i = 0; i < slots.size(); ++i) {
            const auto& sl = slots[i];
            float sx = (slots.size() == 1u)
                ? 0.0f
                : (bpX0 + 10.0f + ((backplaneW - 20.0f) * static_cast<float>(i)) / static_cast<float>(slots.size() - 1u));
            float sy = -6.0f;
            float sw = 16.5f;
            float sh = 16.0f;

            drawSchRect(sx, sy, sw, sh, sl.isDap ? 0.38f : 0.28f, EdaLayerId::SchSymbol, 0);
            // Backplane connector stub
            out.traces.push_back({ sx, sy + sh * 0.5f, sx, 6.0f, 0.26f, EdaLayerId::InnerCopper, 2 });
            out.circles.push_back({ sx, 6.0f, 0.45f, 0.22f, true, EdaLayerId::InnerCopper });

            if (sl.isDap) {
                // Ethernet uplink from DAP to PROFINET bus
                out.traces.push_back({ sx - 2.5f, sy + sh * 0.5f, sx - 2.5f, 14.0f, 0.30f, EdaLayerId::SchWire, 1 });
                out.circles.push_back({ sx - 2.5f, 14.0f, 0.50f, 0.25f, true, EdaLayerId::SchWire });
            }

            // Field IO Input / Output terminal pins below module
            if (sl.inBytes > 0u) {
                out.traces.push_back({ sx - 3.5f, sy - sh * 0.5f - 3.5f, sx - 3.5f, sy - sh * 0.5f, 0.26f, EdaLayerId::SchWire, 3 });
                out.pads.push_back({ sx - 3.5f, sy - sh * 0.5f - 3.5f, 0.6f, 0.6f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"IN", L"" });
                out.labels.push_back({ sx - 7.5f, sy - sh * 0.5f - 5.2f, 0.82f, EdaLayerId::SchWire,
                                       L"IN:" + std::to_wstring(sl.inBytes) + L"B" });
            }
            if (sl.outBytes > 0u) {
                out.traces.push_back({ sx + 3.5f, sy - sh * 0.5f, sx + 3.5f, sy - sh * 0.5f - 3.5f, 0.26f, EdaLayerId::TopCopper, 4 });
                out.pads.push_back({ sx + 3.5f, sy - sh * 0.5f - 3.5f, 0.6f, 0.6f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"OUT", L"" });
                out.labels.push_back({ sx + 0.5f, sy - sh * 0.5f - 5.2f, 0.82f, EdaLayerId::SchLabel,
                                       L"OUT:" + std::to_wstring(sl.outBytes) + L"B" });
            }

            std::wstring slotHdr = sl.isDap ? L"[DAP] " : (L"[Slot " + std::to_wstring(i) + L"] ");
            out.labels.push_back({ sx - sw * 0.45f, sy + 4.5f, 0.88f, EdaLayerId::SchLabel, slotHdr });
            std::wstring humanName = PathUtils::Utf8ToWide(sl.name);
            if (humanName.size() > 18u) humanName = humanName.substr(0, 18u);
            out.labels.push_back({ sx - sw * 0.45f, sy + 1.2f, 0.90f, EdaLayerId::SchLabel, humanName });
            if (sl.isDap && !sl.slotsAttr.empty()) {
                out.labels.push_back({ sx - sw * 0.45f, sy - 2.2f, 0.80f, EdaLayerId::SchLabel,
                                       L"Slots: " + PathUtils::Utf8ToWide(sl.slotsAttr) });
            }

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(sl.name);
            comp.value = sl.isDap ? L"PROFINET DAP" : (L"IN " + std::to_wstring(sl.inBytes) + L"B / OUT " + std::to_wstring(sl.outBytes) + L"B");
            comp.footprint = sl.isDap ? L"GSDML_DAP" : L"GSDML_MOD";
            comp.x = sx; comp.y = sy;
            comp.widthMm = sw; comp.lengthMm = sh;
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));
        }

        out.netCount = slots.size() + 2u;
        return true;
    }
}

} // namespace Formats
