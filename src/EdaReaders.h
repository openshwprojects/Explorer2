#pragma once
// The board / schematic readers behind EdaViewer (PCB, Gerber, schematics, netlists, simulation results:
// src/formats/*.h, dispatched by extension in EdaReaders.cpp), without the viewer. They also build on Linux.
#include "EdaTypes.h"
#include <string>
#include <string_view>

namespace Explorer2::EdaReaders {

// Extensions the board view opens (lower case, with the dot), and content checks for ambiguous ones
bool IsEdaExtension(const std::wstring& extLower);
bool IsLikelySpiceRawFile(const std::wstring& fullPath);
bool IsLikelyAsicDefFile(const std::wstring& fullPath);
bool IsLikelyEasyEdaJsonFile(const std::wstring& fullPath);
bool IsLikelyLibertyOrEdaSdfFile(const std::wstring& fullPath, const std::wstring& extLower);
bool IsLikelyOdbPlusPlusTgz(const std::wstring& fullPath);
bool InspectEdaFile(const std::wstring& fullPath, ParsedEdaBoard& outBoard);

bool IsModularEdaExtension(const std::wstring& extLower);
bool TryParseModularEda(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard);

// Domain dispatchers compiled in dedicated translation units (EdaReadersAsicBitstream.cpp / EdaReadersScopeAuto.cpp)
bool CheckLikelyAsicDefHeader(const uint8_t* data, size_t size);
bool CheckLikelyLibertyHeader(const uint8_t* data, size_t size);
bool CheckLikelyIeeeSdfHeader(const uint8_t* data, size_t size);
bool DispatchEdaAsicAndBitstream(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled);
bool DispatchEdaScopeAndAutomotive(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard,
    bool& outMatched,
    bool& outStrictHandled);
bool DispatchEdaCanAscFallback(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard);

} // namespace Explorer2::EdaReaders
