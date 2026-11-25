#pragma once
// IntelFpgaSof.h — Intel / Altera Quartus SRAM Object File (.sof) Reader
// Delegates binary TLV packet directory decoding & LAB/M9K floorplan rendering to AlteraBitstreamCommon.h.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseIntelFpgaSofBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& ext,
    ParsedEdaBoard& out)
{
    if (ext != L".sof") return false;
    AlteraBitstreamCommon::AlteraDecodedContainer container;
    if (!AlteraBitstreamCommon::ParseAlteraPacketContainer(data, size, /*expectPof=*/false, container)) {
        return false;
    }
    return AlteraBitstreamCommon::RenderAlteraFloorplanToBoard(data, size, container, out);
}

} // namespace Formats
