#pragma once
// SynopsysSdc.h — Synopsys Design Constraints (.sdc) Timing & Clock Diagram Reader
// Uses shared TclTimingPinCommon.h (with backslash line continuations, balanced Tcl bracket/brace
// tokenization, clock waveform tracks, I/O setup/hold windows, and CDC exception paths).
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseSynopsysSdcBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& ext,
    ParsedEdaBoard& out)
{
    if (ext != L".sdc" || !data || size < 10 || size > (16u << 20)) return false;

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    std::vector<std::string> cmds;
    if (!TclTimingPin::ExtractLogicalTclCommands(sv, cmds) || cmds.empty()) return false;

    std::vector<TclTimingPin::TclClockDef> clocks;
    std::vector<TclTimingPin::TclIoDelay> ioDelays;
    std::vector<TclTimingPin::TclTimingException> exceptions;
    size_t miscTiming = 0;

    for (const auto& cmdStr : cmds) {
        std::vector<std::string> toks;
        TclTimingPin::TokenizeTclCommand(cmdStr, toks);
        if (toks.empty()) continue;

        TclTimingPin::TryConsumeTimingCommand(toks, clocks, ioDelays, exceptions, miscTiming);
    }

    return TclTimingPin::RenderTimingDiagramBoard(
        L"Synopsys Design Constraints (.sdc)",
        clocks,
        ioDelays,
        exceptions,
        miscTiming,
        out);
}

} // namespace Formats
