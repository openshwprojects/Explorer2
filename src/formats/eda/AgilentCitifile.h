#pragma once
// AgilentCitifile.h — HP / Agilent / Keysight RF VNA CITIFILE (.cti, CITIFILE A.01.00 / A.01.01) reader
// Parses CITIFILE packages: NAME, VAR <indep> MAG <N>, DATA <param> RI|MAGANGLE|DBANGLE|MAG,
// SEG_LIST_BEGIN..SEG_LIST_END, VAR_LIST_BEGIN..VAR_LIST_END, and BEGIN..END complex/real arrays,
// converting complex S-parameters into insertion/return loss dB and phase degree traces.
// Included inside namespace Explorer2 in EdaReadersVlsiWave.cpp (do NOT put #include <...> here).

namespace Formats {

namespace DetailCitifile {

enum class DataFmt { RI, MagAngle, DbAngle, MagScalar };

struct DataDecl {
    std::string name;
    DataFmt fmt = DataFmt::RI;
};

} // namespace DetailCitifile

inline bool ParseAgilentCitifile(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 24 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    bool sawHeader = false;
    std::string pkgName = "VNA";
    std::string indepName = "FREQ";
    std::vector<uint64_t> varCounts;
    std::vector<DetailCitifile::DataDecl> dataDecls;
    std::vector<double> indepVals;
    std::vector<std::vector<std::pair<double, double>>> dataBlocks;

    enum class BlockMode { None, VarList, SegList, DataBlock };
    BlockMode mode = BlockMode::None;
    std::vector<std::pair<double, double>> curBlock;

    size_t pos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || line.front() == '!' || line.front() == '#') continue;

        if (!sawHeader) {
            if (!ReaderText::StartsWithCI(line, "CITIFILE")) return false;
            sawHeader = true;
            continue;
        }

        if (mode == BlockMode::VarList) {
            if (ReaderText::AsciiEqCI(line, "VAR_LIST_END")) {
                mode = BlockMode::None;
                continue;
            }
            double fv = 0.0;
            if (!ReaderText::ParseDoubleSv(line, fv) || !std::isfinite(fv)) return false;
            if (indepVals.size() < 262144) indepVals.push_back(fv);
            continue;
        }

        if (mode == BlockMode::SegList) {
            if (ReaderText::AsciiEqCI(line, "SEG_LIST_END")) {
                mode = BlockMode::None;
                continue;
            }
            // SEG <start> <stop> <numPts>
            std::string_view rem = line, kw, tStart, tStop, tNum;
            double f0 = 0.0, f1 = 0.0;
            uint64_t nPts = 0;
            if (ReaderText::NextTokenSv(rem, kw) && ReaderText::AsciiEqCI(kw, "SEG") &&
                ReaderText::NextTokenSv(rem, tStart) && ReaderText::ParseDoubleSv(tStart, f0) &&
                ReaderText::NextTokenSv(rem, tStop)  && ReaderText::ParseDoubleSv(tStop, f1) &&
                ReaderText::NextTokenSv(rem, tNum)   && ReaderText::ParseUint64Sv(tNum, nPts, 10) &&
                std::isfinite(f0) && std::isfinite(f1) && nPts >= 1 && nPts <= 131072)
            {
                for (uint64_t i = 0; i < nPts && indepVals.size() < 262144; ++i) {
                    const double t = (nPts > 1) ? (static_cast<double>(i) / static_cast<double>(nPts - 1)) : 0.0;
                    indepVals.push_back(f0 + (f1 - f0) * t);
                }
            } else {
                return false;
            }
            continue;
        }

        if (mode == BlockMode::DataBlock) {
            if (ReaderText::AsciiEqCI(line, "END")) {
                dataBlocks.push_back(std::move(curBlock));
                curBlock.clear();
                mode = BlockMode::None;
                continue;
            }
            const size_t blkIdx = dataBlocks.size();
            if (blkIdx >= dataDecls.size()) return false;
            const auto fmt = dataDecls[blkIdx].fmt;

            if (fmt == DetailCitifile::DataFmt::MagScalar) {
                double v0 = 0.0;
                if (!ReaderText::ParseDoubleSv(line, v0) || !std::isfinite(v0)) return false;
                if (curBlock.size() < 262144) curBlock.emplace_back(v0, 0.0);
            } else {
                // Pair separated by comma or whitespace: <v0>,<v1> or <v0> <v1>
                double v0 = 0.0, v1 = 0.0;
                const size_t comma = line.find(',');
                if (comma != std::string_view::npos) {
                    if (!ReaderText::ParseDoubleSv(ReaderText::TrimSv(line.substr(0, comma)), v0) ||
                        !ReaderText::ParseDoubleSv(ReaderText::TrimSv(line.substr(comma + 1)), v1)) {
                        return false;
                    }
                } else {
                    std::string_view rem = line, a, b;
                    if (!ReaderText::NextTokenSv(rem, a) || !ReaderText::NextTokenSv(rem, b) ||
                        !ReaderText::ParseDoubleSv(a, v0) || !ReaderText::ParseDoubleSv(b, v1)) {
                        return false;
                    }
                }
                if (!std::isfinite(v0) || !std::isfinite(v1)) return false;
                if (curBlock.size() < 262144) curBlock.emplace_back(v0, v1);
            }
            continue;
        }

        // Top-level package declarations
        std::string_view rem = line, kw;
        if (!ReaderText::NextTokenSv(rem, kw)) continue;

        if (ReaderText::AsciiEqCI(kw, "CITIFILE")) {
            continue; // Multi-package CITIFILE header
        } else if (ReaderText::AsciiEqCI(kw, "NAME")) {
            std::string_view nm;
            if (ReaderText::NextTokenSv(rem, nm)) pkgName = std::string(nm);
        } else if (ReaderText::AsciiEqCI(kw, "VAR")) {
            // VAR <name> <MAG|RI> <count>  (or VAR <name> <count>)
            std::string_view vName, tok2, tok3;
            if (!ReaderText::NextTokenSv(rem, vName) || !ReaderText::NextTokenSv(rem, tok2)) return false;
            indepName = std::string(vName);
            uint64_t cnt = 0;
            if (ReaderText::NextTokenSv(rem, tok3)) {
                if (!ReaderText::ParseUint64Sv(tok3, cnt, 10)) return false;
            } else {
                if (!ReaderText::ParseUint64Sv(tok2, cnt, 10)) return false;
            }
            if (cnt < 1 || cnt > 262144) return false;
            varCounts.push_back(cnt);
        } else if (ReaderText::AsciiEqCI(kw, "DATA")) {
            // DATA <name> <RI|MAGANGLE|DBANGLE|MAG>
            std::string_view dName, dFmt;
            if (!ReaderText::NextTokenSv(rem, dName)) return false;
            DetailCitifile::DataFmt fmt = DetailCitifile::DataFmt::RI;
            if (ReaderText::NextTokenSv(rem, dFmt)) {
                if (ReaderText::AsciiEqCI(dFmt, "RI")) fmt = DetailCitifile::DataFmt::RI;
                else if (ReaderText::AsciiEqCI(dFmt, "MAGANGLE")) fmt = DetailCitifile::DataFmt::MagAngle;
                else if (ReaderText::AsciiEqCI(dFmt, "DBANGLE")) fmt = DetailCitifile::DataFmt::DbAngle;
                else if (ReaderText::AsciiEqCI(dFmt, "MAG")) fmt = DetailCitifile::DataFmt::MagScalar;
                else return false;
            }
            dataDecls.push_back({ std::string(dName), fmt });
        } else if (ReaderText::AsciiEqCI(kw, "VAR_LIST_BEGIN")) {
            mode = BlockMode::VarList;
        } else if (ReaderText::AsciiEqCI(kw, "SEG_LIST_BEGIN")) {
            mode = BlockMode::SegList;
        } else if (ReaderText::AsciiEqCI(kw, "BEGIN")) {
            if (dataBlocks.size() >= dataDecls.size()) return false;
            curBlock.clear();
            mode = BlockMode::DataBlock;
        }
    }

    if (!sawHeader || mode != BlockMode::None || dataDecls.empty() || dataBlocks.size() != dataDecls.size()) {
        return false;
    }

    uint64_t expectedPts = 1;
    if (varCounts.empty()) {
        expectedPts = dataBlocks[0].size();
    } else {
        for (uint64_t vc : varCounts) {
            if (vc > 262144ULL / expectedPts) return false;
            expectedPts *= vc;
        }
    }
    if (expectedPts < 2) return false;

    for (const auto& blk : dataBlocks) {
        if (blk.size() != expectedPts) return false;
    }

    // Map independent variable sweep to integer grid [0 .. 10000]
    double fMin = 0.0, fMax = static_cast<double>(expectedPts - 1);
    if (!indepVals.empty()) {
        fMin = indepVals.front();
        fMax = indepVals.back();
        for (double fv : indepVals) { fMin = std::min(fMin, fv); fMax = std::max(fMax, fv); }
    }
    const double fSpan = (fMax > fMin + 1e-18) ? (fMax - fMin) : 1.0;

    auto mapSweepTick = [&](size_t idx) -> uint64_t {
        if (indepVals.size() == expectedPts) {
            const double norm = (indepVals[idx] - fMin) / fSpan;
            return static_cast<uint64_t>(std::llround(std::clamp(norm, 0.0, 1.0) * 10000.0));
        }
        return static_cast<uint64_t>((idx * 10000ULL) / std::max<size_t>(1u, static_cast<size_t>(expectedPts - 1)));
    };

    std::vector<DetailHwBin::DecodedWaveSignal> sigs;
    for (size_t di = 0; di < dataDecls.size() && sigs.size() < 20; ++di) {
        const auto& decl = dataDecls[di];
        const auto& blk = dataBlocks[di];

        DetailHwBin::DecodedWaveSignal magSig;
        magSig.isReal = true;
        magSig.name = PathUtils::Utf8ToWide(decl.name) +
                      ((decl.fmt == DetailCitifile::DataFmt::MagScalar) ? L"" : L" |dB|");

        DetailHwBin::DecodedWaveSignal phSig;
        phSig.isReal = true;
        phSig.name = PathUtils::Utf8ToWide(decl.name) + L" ∠deg";
        const bool emitPhase = (decl.fmt != DetailCitifile::DataFmt::MagScalar && dataDecls.size() <= 4);

        for (size_t i = 0; i < blk.size(); ++i) {
            const uint64_t xTick = mapSweepTick(i);
            const double a = blk[i].first;
            const double b = blk[i].second;
            double dbVal = 0.0;
            double degVal = 0.0;

            switch (decl.fmt) {
            case DetailCitifile::DataFmt::RI: {
                const double lin = std::hypot(a, b);
                dbVal = 20.0 * std::log10(std::max(lin, 1e-12));
                degVal = std::atan2(b, a) * (180.0 / 3.14159265358979323846);
                break;
            }
            case DetailCitifile::DataFmt::MagAngle:
                dbVal = 20.0 * std::log10(std::max(std::fabs(a), 1e-12));
                degVal = b;
                break;
            case DetailCitifile::DataFmt::DbAngle:
                dbVal = a;
                degVal = b;
                break;
            case DetailCitifile::DataFmt::MagScalar:
                dbVal = a;
                break;
            }
            magSig.realChanges.emplace_back(xTick, dbVal);
            if (emitPhase) phSig.realChanges.emplace_back(xTick, degVal);
        }
        sigs.push_back(std::move(magSig));
        if (emitPhase) sigs.push_back(std::move(phSig));
    }

    wchar_t title[200] = {};
    if (!indepVals.empty() && fMax >= 1e6) {
        swprintf_s(title, L"Agilent/Keysight CITIFILE [%hs] (%zu params, %llu pts, %.3f..%.3f GHz)",
            pkgName.c_str(), dataDecls.size(), static_cast<unsigned long long>(expectedPts),
            fMin * 1e-9, fMax * 1e-9);
    } else {
        swprintf_s(title, L"Agilent/Keysight CITIFILE [%hs] (%zu params, %llu %hs pts)",
            pkgName.c_str(), dataDecls.size(), static_cast<unsigned long long>(expectedPts),
            indepName.c_str());
    }
    return DetailHwBin::RenderWaveSignalsToBoard(sigs, 0, 10000, title, out);
}

} // namespace Formats
