#pragma once
// The document readers behind SpreadsheetDocViewer (tables, text documents, logs, GPS tracks, data files:
// src/formats/*.h, dispatched by extension in DocReaders.cpp), without the viewer. They also build on Linux.
#include "OfficeDocTypes.h"
#include <string>

namespace Explorer2::DocReaders {

// Extensions the table / document view opens (lower case, with the dot)
bool IsSpreadsheetOrDocExtension(const std::wstring& extLower);
bool InspectOfficeFile(const std::wstring& fullPath, ParsedOfficeDocument& outDoc);

bool IsCatalogOrRpgDocExtension(const std::wstring& extLower);
bool TryParseCatalogOrRpgDoc(
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    ParsedOfficeDocument& outDoc);

// Domain dispatchers compiled in dedicated translation units (DocReadersSecurityBin / DocReadersSciAiData / DocReadersAudioMedia)
bool DispatchDocDirectColumnarFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& outDoc, bool& outMatched);
bool DispatchDocSecurityAndSys(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& outDoc);
bool DispatchDocSciAiAndData(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& outDoc);
bool DispatchDocAudioAndPublishing(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& outDoc);

} // namespace Explorer2::DocReaders
