#pragma once
// The board / schematic readers behind EdaViewer (PCB, Gerber, schematics, netlists, simulation results:
// src/formats/*.h, dispatched by extension in EdaReaders.cpp), without the viewer. They also build on Linux.
#include "EdaTypes.h"
#include <string>

namespace Explorer2::EdaReaders {

// Extensions the board view opens (lower case, with the dot), and content checks for ambiguous ones
bool IsEdaExtension(const std::wstring& extLower);
bool IsLikelySpiceRawFile(const std::wstring& fullPath);
bool IsLikelyAsicDefFile(const std::wstring& fullPath);
bool IsLikelyEasyEdaJsonFile(const std::wstring& fullPath);
bool IsLikelyLibertyOrEdaSdfFile(const std::wstring& fullPath, const std::wstring& extLower);
bool IsLikelyOdbPlusPlusTgz(const std::wstring& fullPath);
bool InspectEdaFile(const std::wstring& fullPath, ParsedEdaBoard& outBoard);

} // namespace Explorer2::EdaReaders
