#pragma once
// AsamOdxDiag.h — ASAM MCD-2 D (ISO 22901-1) Open Diagnostic Data Exchange (.odx) Reader
// Uses `XmlTagScan` to parse `<DIAG-SERVICE>`, `<REQUEST>`, `<POS-RESPONSE>`, `<NEG-RESPONSE>`,
// `<DATA-OBJECT-PROP>`, and `<PARAM>` (`SHORT-NAME`, `SEMANTIC`, `BYTE-POSITION`, `BIT-POSITION`,
// `BIT-LENGTH`, `CODED-CONST` `<V>` SID/DID values), rendering UDS Service -> Request/Response PDU byte-frame maps.
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailAsamOdx {

struct OdxParamField {
    std::string name;
    std::string semantic;
    std::string codedValue; // e.g. "0x22", "34", "0xF190"
    uint32_t bytePos = 0;
    uint32_t bitPos = 0;
    uint32_t bitLen = 8;
    bool hasBytePos = false;
};

struct OdxPduMessage {
    std::string id;
    std::string name;
    int kind = 0; // 0 = REQUEST, 1 = POS-RESPONSE, 2 = NEG-RESPONSE
    std::vector<OdxParamField> params;
};

struct OdxDiagService {
    std::string name;
    std::string semantic;
    std::string reqRefId;
    std::vector<std::string> posRespRefIds;
    std::vector<std::string> negRespRefIds;
    std::vector<OdxParamField> inlineParams;
};

} // namespace DetailAsamOdx

inline bool ParseAsamOdxDiag(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    using namespace DetailAsamOdx;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 32u, (32u << 20))) return false;

    std::vector<std::string> tagStack;
    bool sawOdxRoot    = false;
    bool closedOdxRoot = false;
    std::string ecuName;

    std::vector<OdxDiagService> services;
    std::vector<OdxPduMessage> messages;
    std::vector<std::string> didProps;

    int inServiceDepth = -1;
    int inMessageDepth = -1;
    int inParamDepth   = -1;
    OdxParamField curParam{};

    size_t prevAfterTag = 0u;
    size_t pos = 0u;
    XmlTagScan::XmlTag tag{};

    while (XmlTagScan::NextTag(sv, pos, tag)) {
        const std::string upName = ReaderText::ToUpperAscii(tag.localName);

        if (tag.isClosing) {
            if (tagStack.empty() || tagStack.back() != upName) return false;
            const std::string textBetween = XmlTagScan::DecodeText(
                ReaderText::TrimSv(sv.substr(prevAfterTag, tag.tagStart - prevAfterTag)));

            const int curDepth = static_cast<int>(tagStack.size()) - 1;
            const std::string parent = (tagStack.size() >= 2u) ? tagStack[tagStack.size() - 2u] : std::string{};

            if (!textBetween.empty()) {
                if (upName == "SHORT-NAME") {
                    if ((parent == "BASE-VARIANT" || parent == "ECU-VARIANT" || parent == "DIAG-LAYER-CONTAINER") && ecuName.empty()) {
                        ecuName = textBetween;
                    } else if (inParamDepth >= 0 && curDepth == inParamDepth + 1) {
                        curParam.name = textBetween;
                    } else if (inMessageDepth >= 0 && curDepth == inMessageDepth + 1 && !messages.empty() && messages.back().name.empty()) {
                        messages.back().name = textBetween;
                    } else if (inServiceDepth >= 0 && curDepth == inServiceDepth + 1 && !services.empty() && services.back().name.empty()) {
                        services.back().name = textBetween;
                    } else if (parent == "DATA-OBJECT-PROP" && didProps.size() < 256u) {
                        didProps.push_back(textBetween);
                    }
                } else if (inParamDepth >= 0) {
                    uint64_t uv = 0;
                    if (upName == "BYTE-POSITION" && ReaderText::ParseUint64Sv(textBetween, uv, 10)) {
                        curParam.bytePos = static_cast<uint32_t>(std::min<uint64_t>(uv, 4096ULL));
                        curParam.hasBytePos = true;
                    } else if (upName == "BIT-POSITION" && ReaderText::ParseUint64Sv(textBetween, uv, 10)) {
                        curParam.bitPos = static_cast<uint32_t>(std::min<uint64_t>(uv, 7ULL));
                    } else if (upName == "BIT-LENGTH" && ReaderText::ParseUint64Sv(textBetween, uv, 10) && uv > 0) {
                        curParam.bitLen = static_cast<uint32_t>(std::min<uint64_t>(uv, 256ULL));
                    } else if (upName == "V" || upName == "CODED-CONST") {
                        curParam.codedValue = textBetween;
                    }
                }
            }

            if (curDepth == inParamDepth) {
                if (!curParam.name.empty() || !curParam.codedValue.empty()) {
                    if (inMessageDepth >= 0 && !messages.empty()) {
                        if (!curParam.hasBytePos) curParam.bytePos = static_cast<uint32_t>(messages.back().params.size());
                        messages.back().params.push_back(curParam);
                    } else if (inServiceDepth >= 0 && !services.empty()) {
                        if (!curParam.hasBytePos) curParam.bytePos = static_cast<uint32_t>(services.back().inlineParams.size());
                        services.back().inlineParams.push_back(curParam);
                    }
                }
                inParamDepth = -1;
            }
            if (curDepth == inMessageDepth) inMessageDepth = -1;
            if (curDepth == inServiceDepth) inServiceDepth = -1;

            tagStack.pop_back();
            if (upName == "ODX" && tagStack.empty()) closedOdxRoot = true;
            prevAfterTag = tag.afterTag;
            continue;
        }

        if (tagStack.empty()) {
            if (upName != "ODX") return false;
            sawOdxRoot = true;
        }

        const int openDepth = static_cast<int>(tagStack.size());
        if (upName == "DIAG-SERVICE" && inServiceDepth < 0 && services.size() < 256u) {
            OdxDiagService svc;
            std::string_view semSv;
            if (XmlTagScan::FindAttr(tag.attrs, "SEMANTIC", semSv, true, true)) {
                svc.semantic = XmlTagScan::DecodeText(semSv);
            } else {
                svc.semantic = "UDS_SVC";
            }
            services.push_back(std::move(svc));
            if (!tag.isSelfClosing) inServiceDepth = openDepth;
        } else if ((upName == "REQUEST" || upName == "POS-RESPONSE" || upName == "NEG-RESPONSE") &&
                   inMessageDepth < 0 && messages.size() < 512u)
        {
            OdxPduMessage msg;
            msg.kind = (upName == "REQUEST") ? 0 : ((upName == "POS-RESPONSE") ? 1 : 2);
            std::string_view idSv;
            if (XmlTagScan::FindAttr(tag.attrs, "ID", idSv, true, true)) {
                msg.id = XmlTagScan::DecodeText(idSv);
            }
            messages.push_back(std::move(msg));
            if (!tag.isSelfClosing) inMessageDepth = openDepth;
        } else if (upName == "PARAM" && inParamDepth < 0 && (inMessageDepth >= 0 || inServiceDepth >= 0)) {
            curParam = OdxParamField{};
            std::string_view semSv;
            if (XmlTagScan::FindAttr(tag.attrs, "SEMANTIC", semSv, true, true)) {
                curParam.semantic = XmlTagScan::DecodeText(semSv);
            }
            if (!tag.isSelfClosing) inParamDepth = openDepth;
        } else if (inServiceDepth >= 0 && !services.empty()) {
            std::string_view idRef;
            if (upName == "REQUEST-REF" && XmlTagScan::FindAttr(tag.attrs, "ID-REF", idRef, true, true)) {
                services.back().reqRefId = XmlTagScan::DecodeText(idRef);
            } else if (upName == "POS-RESPONSE-REF" && XmlTagScan::FindAttr(tag.attrs, "ID-REF", idRef, true, true)) {
                services.back().posRespRefIds.push_back(XmlTagScan::DecodeText(idRef));
            } else if (upName == "NEG-RESPONSE-REF" && XmlTagScan::FindAttr(tag.attrs, "ID-REF", idRef, true, true)) {
                services.back().negRespRefIds.push_back(XmlTagScan::DecodeText(idRef));
            }
        }

        if (!tag.isSelfClosing) {
            if (tagStack.size() >= 64u) return false;
            tagStack.push_back(upName);
        } else if (upName == "ODX" && tagStack.empty()) {
            closedOdxRoot = true;
        }
        prevAfterTag = tag.afterTag;
    }

    if (!sawOdxRoot || !closedOdxRoot || !tagStack.empty()) return false;
    if (services.empty() && messages.empty() && didProps.empty()) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    // Index parsed REQUEST / POS-RESPONSE / NEG-RESPONSE PDUs by `ID` and positional fallback
    std::unordered_map<std::string, size_t> msgById;
    std::vector<size_t> reqIndices, respIndices;
    for (size_t mi = 0; mi < messages.size(); ++mi) {
        if (!messages[mi].id.empty()) msgById[messages[mi].id] = mi;
        if (messages[mi].kind == 0) reqIndices.push_back(mi);
        else                        respIndices.push_back(mi);
    }

    auto formatCodedHex = [](const std::string& raw) -> std::string {
        if (raw.empty()) return {};
        if (ReaderText::StartsWithCI(raw, "0x")) return raw;
        uint64_t v = 0;
        if (ReaderText::ParseUint64Sv(raw, v, 10)) {
            char hb[24] = {};
            if (v <= 0xFFULL) std::snprintf(hb, sizeof(hb), "0x%02llX", static_cast<unsigned long long>(v));
            else              std::snprintf(hb, sizeof(hb), "0x%04llX", static_cast<unsigned long long>(v));
            return hb;
        }
        return raw;
    };

    auto drawPduFrame = [&](const OdxPduMessage& msg, float xLeft, float yCenter, EdaLayerId lyr, int netId, float& outRightX) {
        const size_t nParams = std::min<size_t>(msg.params.size(), 8u);
        const float slotW = 10.5f;
        const float frameW = std::max(18.0f, static_cast<float>(std::max<size_t>(1u, nParams)) * slotW);
        const float frameH = 5.6f;
        outRightX = xLeft + frameW;

        DrawEdaBoxRect(out, xLeft, yCenter - frameH * 0.5f, outRightX, yCenter + frameH * 0.5f, 0.26f, lyr, netId);

        const std::wstring kindTag = (msg.kind == 0) ? L"REQ: " : ((msg.kind == 1) ? L"POS: " : L"NEG: ");
        out.labels.push_back({
            xLeft + 0.5f, yCenter + frameH * 0.5f + 0.55f, 0.78f, lyr,
            kindTag + PathUtils::Utf8ToWide(msg.name.empty() ? msg.id : msg.name)
        });

        for (size_t pi = 0; pi < nParams; ++pi) {
            const auto& pf = msg.params[pi];
            const float sx0 = xLeft + static_cast<float>(pi) * slotW;
            if (pi > 0u) {
                out.traces.push_back({ sx0, yCenter - frameH * 0.5f, sx0, yCenter + frameH * 0.5f, 0.16f, lyr, netId });
            }
            std::string cellTxt = "B" + std::to_string(pf.bytePos);
            if (pf.bitLen != 8u || pf.bitPos != 0u) {
                cellTxt += "." + std::to_string(pf.bitPos) + ":" + std::to_string(pf.bitLen) + "b";
            }
            std::string valHex = formatCodedHex(pf.codedValue);
            std::string topLine = pf.name.empty() ? pf.semantic : pf.name;
            if (!valHex.empty()) topLine += "=" + valHex;

            out.labels.push_back({ sx0 + 0.45f, yCenter + 0.45f, 0.65f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(topLine) });
            out.labels.push_back({ sx0 + 0.45f, yCenter - 1.65f, 0.58f, EdaLayerId::BottomSilk, PathUtils::Utf8ToWide(cellTxt) });
        }
    };

    const size_t nSvc = std::min<size_t>(services.size(), 24u);
    const float rowStep = 11.5f;

    for (size_t si = 0; si < nSvc; ++si) {
        const auto& svc = services[si];
        const float y = -static_cast<float>(si) * rowStep;
        const float svcX0 = -46.0f, svcX1 = -20.0f;

        // UDS Service block on left
        DrawEdaBoxRect(out, svcX0, y - 3.0f, svcX1, y + 3.0f, 0.30f, EdaLayerId::SchSymbol, 0);
        out.labels.push_back({ svcX0 + 0.9f, y + 0.6f, 0.90f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(svc.name) });
        out.labels.push_back({ svcX0 + 0.9f, y - 1.6f, 0.70f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide("[" + svc.semantic + "]") });

        out.pads.push_back({
            svcX1, y, 0.90f, 0.90f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper,
            PathUtils::Utf8ToWide(svc.name), PathUtils::Utf8ToWide(svc.semantic)
        });

        EdaComponent3D sc;
        sc.refDes = L"SVC" + std::to_wstring(si + 1u);
        sc.value = PathUtils::Utf8ToWide(svc.name);
        sc.footprint = L"ODX_DIAG_SERVICE";
        sc.x = (svcX0 + svcX1) * 0.5f; sc.y = y;
        sc.widthMm = svcX1 - svcX0; sc.lengthMm = 6.0f; sc.heightMm = 1.2f;
        sc.pinCount = 2;
        sc.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(sc));

        // Resolve linked Request PDU (via `REQUEST-REF` ID, inline params, or positional Request)
        const OdxPduMessage* reqMsg = nullptr;
        OdxPduMessage synthInlineReq;
        if (!svc.reqRefId.empty() && msgById.find(svc.reqRefId) != msgById.end()) {
            reqMsg = &messages[msgById[svc.reqRefId]];
        } else if (!svc.inlineParams.empty()) {
            synthInlineReq.name = svc.name + "_Req";
            synthInlineReq.kind = 0;
            synthInlineReq.params = svc.inlineParams;
            reqMsg = &synthInlineReq;
        } else if (si < reqIndices.size()) {
            reqMsg = &messages[reqIndices[si]];
        }

        float curX = -14.0f;
        const int netId = static_cast<int>(si + 1u);
        if (reqMsg) {
            out.traces.push_back({ svcX1, y, curX, y, 0.26f, EdaLayerId::TopCopper, netId });
            // Arrowhead into Request PDU
            out.traces.push_back({ curX - 1.1f, y + 0.6f, curX, y, 0.24f, EdaLayerId::TopCopper, netId });
            out.traces.push_back({ curX - 1.1f, y - 0.6f, curX, y, 0.24f, EdaLayerId::TopCopper, netId });
            out.pads.push_back({ curX, y, 0.80f, 0.80f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"REQ", PathUtils::Utf8ToWide(svc.name) });

            float reqEndX = curX;
            drawPduFrame(*reqMsg, curX, y, EdaLayerId::TopCopper, netId, reqEndX);
            curX = reqEndX + 5.5f;
        }

        // Resolve linked Positive / Negative Response PDU
        const OdxPduMessage* respMsg = nullptr;
        if (!svc.posRespRefIds.empty() && msgById.find(svc.posRespRefIds[0]) != msgById.end()) {
            respMsg = &messages[msgById[svc.posRespRefIds[0]]];
        } else if (!svc.negRespRefIds.empty() && msgById.find(svc.negRespRefIds[0]) != msgById.end()) {
            respMsg = &messages[msgById[svc.negRespRefIds[0]]];
        } else if (si < respIndices.size()) {
            respMsg = &messages[respIndices[si]];
        }

        if (respMsg) {
            const float wireStartX = reqMsg ? (curX - 5.5f) : svcX1;
            const EdaLayerId rLyr = (respMsg->kind == 2) ? EdaLayerId::BottomCopper : EdaLayerId::InnerCopper;
            out.traces.push_back({ wireStartX, y, curX, y, 0.26f, rLyr, netId });
            out.traces.push_back({ curX - 1.1f, y + 0.6f, curX, y, 0.24f, rLyr, netId });
            out.traces.push_back({ curX - 1.1f, y - 0.6f, curX, y, 0.24f, rLyr, netId });
            out.pads.push_back({ curX, y, 0.80f, 0.80f, 0.0f, 0.0f, 1, rLyr, L"RESP", PathUtils::Utf8ToWide(respMsg->name) });

            float respEndX = curX;
            drawPduFrame(*respMsg, curX, y, rLyr, netId, respEndX);
        }
    }

    // If ODX file had standalone messages without `<DIAG-SERVICE>`, render them cleanly
    if (nSvc == 0u) {
        for (size_t mi = 0; mi < std::min<size_t>(messages.size(), 16u); ++mi) {
            float endX = 0.0f;
            drawPduFrame(messages[mi], -30.0f, -static_cast<float>(mi) * 9.5f, EdaLayerId::TopCopper, static_cast<int>(mi + 1u), endX);
            out.pads.push_back({ -30.0f, -static_cast<float>(mi) * 9.5f, 0.85f, 0.85f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"PDU", L"ODX" });
        }
    }
    // Render `<DATA-OBJECT-PROP>` DID dictionary entries below services/messages when present
    if (!didProps.empty() && messages.empty()) {
        const float dopBaseY = (nSvc > 0u) ? (-static_cast<float>(nSvc) * 9.5f - 2.0f) : 0.0f;
        for (size_t di = 0; di < std::min<size_t>(didProps.size(), 16u); ++di) {
            const float dy = dopBaseY - static_cast<float>(di) * 6.5f;
            DrawEdaBoxRect(out, -20.0f, dy - 2.0f, 20.0f, dy + 2.0f, 0.26f, EdaLayerId::InnerCopper, static_cast<int>(nSvc + di + 1u));
            out.labels.push_back({ -18.5f, dy - 0.35f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(didProps[di]) });
            out.pads.push_back({ -20.0f, dy, 0.80f, 0.80f, 0.0f, 0.0f, 1, EdaLayerId::InnerCopper, L"DOP", PathUtils::Utf8ToWide(didProps[di]) });
        }
    }

    const std::wstring wEcu = PathUtils::Utf8ToWide(ecuName.empty() ? "UDS_ECU" : ecuName);
    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"ASAM ODX Diagnostic Layer (.odx — %ls, %zu services, %zu PDUs, %zu DOPs)",
               wEcu.c_str(), services.size(), messages.size(), didProps.size());
    out.labels.push_back({ -46.0f, 6.5f, 1.25f, EdaLayerId::TopSilk, titleBuf });
    out.formatTitle = titleBuf;
    out.netCount = std::max<size_t>(1u, services.size() + messages.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return !out.traces.empty();
}

} // namespace Formats
