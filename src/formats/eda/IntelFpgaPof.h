#pragma once
// IntelFpgaPof.h — Intel / Altera Quartus Programmer Object File (.pof) Reader
// Delegates binary TLV packet directory decoding & CFM/UFM/LAB floorplan rendering to AlteraBitstreamCommon.h.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseIntelFpgaPofBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& ext,
    ParsedEdaBoard& out)
{
    if (ext != L".pof") return false;
    AlteraBitstreamCommon::AlteraDecodedContainer container;
    if (!AlteraBitstreamCommon::ParseAlteraPacketContainer(data, size, /*expectPof=*/true, container)) {
        return false;
    }
    return AlteraBitstreamCommon::RenderAlteraFloorplanToBoard(data, size, container, out);
}

} // namespace Formats
