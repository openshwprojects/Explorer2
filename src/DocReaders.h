#pragma once
// The document readers behind SpreadsheetDocViewer (tables, text documents, logs, GPS tracks, data files:
// src/formats/*.h, dispatched by extension in DocReaders.cpp), without the viewer. They also build on Linux.
#include "OfficeDocTypes.h"
#include <string>

namespace Explorer2::DocReaders {

// Extensions the table / document view opens (lower case, with the dot)
bool IsSpreadsheetOrDocExtension(const std::wstring& extLower);
bool InspectOfficeFile(const std::wstring& fullPath, ParsedOfficeDocument& outDoc);

} // namespace Explorer2::DocReaders
