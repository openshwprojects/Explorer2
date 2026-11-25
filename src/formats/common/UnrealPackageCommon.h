#pragma once
// UnrealPackageCommon.h — Shared Unreal Engine 4/5 package summary & stream loader.
#include "../../ByteIO.h"

namespace Formats {

enum class UnrealPackageAssetKind {
    Unknown = 0,
    StaticMesh3D = 1,
    Texture2D = 2,
    GenericPackage = 3
};

namespace UnrealPkgDetail {

constexpr uint32_t kUnrealPackageTagLE = 0x9E2A83C1u;

inline uint16_t UeU16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t UeU32(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline int32_t  UeI32(const uint8_t* p) { int32_t  v = 0; std::memcpy(&v, p, 4); return v; }
inline int64_t  UeI64(const uint8_t* p) { int64_t  v = 0; std::memcpy(&v, p, 8); return v; }
inline float    UeF32(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }
inline double   UeF64(const uint8_t* p) { double   v = 0.0; std::memcpy(&v, p, 8); return v; }

using ::Explorer2::ByteIO::HalfToFloat;

inline bool ReadUeFString(const uint8_t* data, size_t size, size_t& pos, std::string& out) {
    out.clear();
    if (pos + 4 > size) return false;
    int32_t saveNum = UeI32(data + pos);
    pos += 4;
    if (saveNum == 0) return true;
    if (saveNum > 0) {
        if (saveNum > 65536 || pos + static_cast<size_t>(saveNum) > size) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(saveNum));
        pos += static_cast<size_t>(saveNum);
        while (!out.empty() && out.back() == '\0') out.pop_back();
        return true;
    }
    // Negative length: UTF-16LE character count
    int64_t wCount = -static_cast<int64_t>(saveNum);
    if (wCount <= 0 || wCount > 65536 || pos + static_cast<size_t>(wCount) * 2 > size) return false;
    std::wstring ws;
    ws.reserve(static_cast<size_t>(wCount));
    for (int64_t i = 0; i < wCount; ++i) {
        uint16_t wc = UeU16(data + pos + static_cast<size_t>(i) * 2);
        if (wc != 0) ws.push_back(static_cast<wchar_t>(wc));
    }
    pos += static_cast<size_t>(wCount) * 2;
    for (wchar_t c : ws) out.push_back((c < 0x80) ? static_cast<char>(c) : '?');
    return true;
}

struct ParsedUnrealImport {
    std::string classPackage;
    std::string className;
    int32_t outerIndex = 0;
    std::string objectName;
};

struct ParsedUnrealExport {
    int32_t classIndex = 0;
    int32_t superIndex = 0;
    int32_t outerIndex = 0;
    std::string objectName;
    uint32_t objectFlags = 0;
    int64_t serialSize = 0;
    int64_t serialOffset = 0;
    bool isAsset = false;
};

struct ParsedUnrealProperty {
    std::string exportName;
    std::string name;
    std::string type;
    std::string value;
};

struct ParsedUnrealSummary {
    int32_t legacyVer = 0;
    int32_t verUe4 = 0;
    int32_t verUe5 = 0;
    uint32_t pkgFlags = 0;
    int32_t totalHeaderSize = 0;
    std::string folderName;
    int32_t nameCount = 0;
    int32_t nameOffset = 0;
    int32_t exportCount = 0;
    int32_t exportOffset = 0;
    int32_t importCount = 0;
    int32_t importOffset = 0;
    std::vector<std::string> names;
};

inline bool ParseUnrealPackageSummary(const uint8_t* data, size_t size, ParsedUnrealSummary& out) {
    out = {};
    if (!data || size < 64 || UeU32(data) != kUnrealPackageTagLE) return false;

    size_t pos = 4;
    out.legacyVer = UeI32(data + pos); pos += 4;
    if (out.legacyVer >= 0 || out.legacyVer < -12) return false; // Reject UE1/UE2/UE3 or garbage
    if (out.legacyVer != -4) {
        if (pos + 4 > size) return false;
        pos += 4; // LegacyUE3Version
    }
    if (pos + 8 > size) return false;
    out.verUe4 = UeI32(data + pos); pos += 4;
    if (out.legacyVer <= -8) {
        if (pos + 4 > size) return false;
        out.verUe5 = UeI32(data + pos); pos += 4;
    }
    if (pos + 4 > size) return false;
    pos += 4; // FileVersionLicenseeUE

    if (out.legacyVer <= -2) {
        if (pos + 4 > size) return false;
        int32_t customCnt = UeI32(data + pos); pos += 4;
        if (customCnt < 0 || customCnt > 4096) return false;
        if (out.legacyVer <= -6) {
            if (pos + static_cast<size_t>(customCnt) * 20 > size) return false;
            pos += static_cast<size_t>(customCnt) * 20;
        } else if (out.legacyVer == -5) {
            for (int32_t i = 0; i < customCnt; ++i) {
                if (pos + 20 > size) return false;
                pos += 20;
                std::string fn;
                if (!ReadUeFString(data, size, pos, fn)) return false;
            }
        } else {
            if (pos + static_cast<size_t>(customCnt) * 8 > size) return false;
            pos += static_cast<size_t>(customCnt) * 8;
        }
    }

    if (pos + 4 > size) return false;
    out.totalHeaderSize = UeI32(data + pos); pos += 4;
    if (!ReadUeFString(data, size, pos, out.folderName)) return false;
    if (pos + 12 > size) return false;
    out.pkgFlags   = UeU32(data + pos); pos += 4;
    out.nameCount  = UeI32(data + pos); pos += 4;
    out.nameOffset = UeI32(data + pos); pos += 4;

    if (out.nameCount <= 0 || out.nameCount > 500000) return false;
    if (out.nameOffset < static_cast<int32_t>(pos) || static_cast<size_t>(out.nameOffset) >= size) return false;

    if (out.verUe5 >= 1008) { // ADD_SOFTOBJECTPATH_LIST
        if (pos + 8 > size) return false;
        pos += 8;
    }
    if ((out.pkgFlags & 0x80000000u) == 0 && out.verUe4 >= 516) {
        std::string locId;
        if (!ReadUeFString(data, size, pos, locId)) return false;
    }
    if (out.verUe4 >= 459) {
        if (pos + 8 > size) return false;
        pos += 8; // GatherableTextDataCount + Offset
    }
    if (pos + 16 > size) return false;
    out.exportCount  = UeI32(data + pos); pos += 4;
    out.exportOffset = UeI32(data + pos); pos += 4;
    out.importCount  = UeI32(data + pos); pos += 4;
    out.importOffset = UeI32(data + pos); pos += 4;

    if (out.exportCount < 0 || out.exportCount > 500000 || out.importCount < 0 || out.importCount > 500000) {
        return false;
    }

    // Parse NameMap (auto-detecting 4-byte hash trailer used in UE4.12+ vs 0-byte trailer in UE4.0-4.11 / unversioned)
    auto tryReadNames = [&](bool with4ByteHashes) -> bool {
        out.names.clear();
        out.names.reserve(static_cast<size_t>(out.nameCount));
        size_t nPos = static_cast<size_t>(out.nameOffset);
        for (int32_t i = 0; i < out.nameCount; ++i) {
            std::string s;
            if (!ReadUeFString(data, size, nPos, s)) return false;
            if (with4ByteHashes) {
                if (nPos + 4 > size) return false;
                nPos += 4;
            }
            out.names.push_back(std::move(s));
        }
        return true;
    };

    bool preferHashes = (out.verUe4 >= 504 || out.verUe4 == 0);
    if (!tryReadNames(preferHashes)) {
        if (!tryReadNames(!preferHashes)) return false;
    }
    return !out.names.empty();
}

inline void LoadCombinedUnrealStreams(
    const uint8_t* uassetData,
    size_t uassetSize,
    const ParsedUnrealSummary& sum,
    const std::wstring& fullPath,
    std::vector<uint8_t>& outCombined,
    std::vector<uint8_t>& outUbulk)
{
    outCombined.clear();
    outUbulk.clear();
    size_t hdrCopy = uassetSize;
    std::vector<uint8_t> uexpBytes;
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        PathUtils::ReadFileBytes(dir + L"\\" + stem + L".uexp", 256 * 1024 * 1024, uexpBytes);
        PathUtils::ReadFileBytes(dir + L"\\" + stem + L".ubulk", 256 * 1024 * 1024, outUbulk);
    }

    if (!uexpBytes.empty()) {
        // Cooked split asset: SerialOffset in FObjectExport is relative to TotalHeaderSize
        if (sum.totalHeaderSize > 0 && static_cast<size_t>(sum.totalHeaderSize) <= uassetSize) {
            hdrCopy = static_cast<size_t>(sum.totalHeaderSize);
        }
        outCombined.reserve(hdrCopy + uexpBytes.size());
        outCombined.insert(outCombined.end(), uassetData, uassetData + hdrCopy);
        outCombined.insert(outCombined.end(), uexpBytes.begin(), uexpBytes.end());
    } else {
        outCombined.assign(uassetData, uassetData + uassetSize);
    }
}

inline bool EqualsAsciiCI(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

inline bool ContainsNameCI(const ParsedUnrealSummary& sum, std::string_view target) {
    for (const auto& n : sum.names) {
        if (EqualsAsciiCI(n, target)) return true;
    }
    return false;
}

inline bool ReadUeFName(const uint8_t* data, size_t size, size_t& pos, const ParsedUnrealSummary& sum, std::string& outName) {
    outName.clear();
    if (pos + 8 > size) return false;
    int32_t idx = UeI32(data + pos);
    int32_t num = UeI32(data + pos + 4);
    pos += 8;
    if (idx < 0 || static_cast<size_t>(idx) >= sum.names.size() || num < 0) return false;
    outName = sum.names[static_cast<size_t>(idx)];
    if (num > 0) outName += "_" + std::to_string(num - 1);
    return true;
}

inline void ParseUnrealImportAndExportTables(
    const uint8_t* data,
    size_t size,
    const ParsedUnrealSummary& sum,
    std::vector<ParsedUnrealImport>& outImports,
    std::vector<ParsedUnrealExport>& outExports)
{
    outImports.clear();
    outExports.clear();

    // 1. ImportMap
    if (sum.importCount > 0 && sum.importOffset > 0 && static_cast<size_t>(sum.importOffset) < size) {
        const size_t impStart = static_cast<size_t>(sum.importOffset);
        const int32_t capImp = std::min<int32_t>(sum.importCount, 4096);
        size_t fixedStride = 0;
        if (sum.exportOffset > sum.importOffset &&
            static_cast<size_t>(sum.exportOffset) <= size &&
            ((sum.exportOffset - sum.importOffset) % sum.importCount) == 0)
        {
            size_t cand = static_cast<size_t>((sum.exportOffset - sum.importOffset) / sum.importCount);
            if (cand >= 28 && cand <= 48) fixedStride = cand;
        }
        size_t p = impStart;
        for (int32_t i = 0; i < capImp && p + 28 <= size; ++i) {
            const size_t entryStart = p;
            ParsedUnrealImport imp;
            if (!ReadUeFName(data, size, p, sum, imp.classPackage)) break;
            if (!ReadUeFName(data, size, p, sum, imp.className)) break;
            if (p + 4 > size) break;
            imp.outerIndex = UeI32(data + p); p += 4;
            if (!ReadUeFName(data, size, p, sum, imp.objectName)) break;
            if (fixedStride > 0) {
                p = entryStart + fixedStride;
            } else {
                if ((sum.pkgFlags & 0x80000000u) == 0 && sum.verUe4 >= 520 && p + 8 <= size) p += 8; // PackageName
                if (sum.verUe5 >= 1001 && p + 4 <= size) p += 4; // bImportOptional
            }
            outImports.push_back(std::move(imp));
        }
    }

    // 2. ExportMap
    if (sum.exportCount > 0 && sum.exportOffset > 0 && static_cast<size_t>(sum.exportOffset) < size) {
        const size_t expStart = static_cast<size_t>(sum.exportOffset);
        const int32_t capExp = std::min<int32_t>(sum.exportCount, 4096);
        const int32_t ver4 = (sum.verUe4 > 0) ? sum.verUe4 : 522;
        size_t p = expStart;
        for (int32_t i = 0; i < capExp && p + 36 <= size; ++i) {
            ParsedUnrealExport ex;
            ex.classIndex = UeI32(data + p); p += 4;
            ex.superIndex = UeI32(data + p); p += 4;
            if (ver4 >= 508) { if (p + 4 > size) break; p += 4; } // TemplateIndex
            if (p + 4 > size) break;
            ex.outerIndex = UeI32(data + p); p += 4;
            if (!ReadUeFName(data, size, p, sum, ex.objectName)) break;
            if (p + 4 > size) break;
            ex.objectFlags = UeU32(data + p); p += 4;
            if (ver4 < 511) {
                if (p + 8 > size) break;
                ex.serialSize = UeI32(data + p);
                ex.serialOffset = UeI32(data + p + 4);
                p += 8;
            } else {
                if (p + 16 > size) break;
                ex.serialSize = UeI64(data + p);
                ex.serialOffset = UeI64(data + p + 8);
                p += 16;
            }
            if (p + 12 > size) break;
            p += 12; // bForcedExport, bNotForClient, bNotForServer
            if (sum.verUe5 < 1005) { if (p + 16 > size) break; p += 16; } // PackageGuid
            if (sum.verUe5 >= 1007) { if (p + 4 > size) break; p += 4; }  // bIsInheritedInstance
            if (p + 4 > size) break;
            p += 4; // PackageFlags
            if (ver4 >= 365) { if (p + 4 > size) break; p += 4; } // bNotAlwaysLoadedForEditorGame
            if (ver4 >= 485) { if (p + 4 > size) break; ex.isAsset = (UeI32(data + p) != 0); p += 4; }
            if (sum.verUe5 >= 1004) { if (p + 4 > size) break; p += 4; } // bGeneratePublicHash
            if (ver4 >= 507) { if (p + 20 > size) break; p += 20; } // 5 preload dependency counts/indices
            if (sum.verUe5 >= 1012) { if (p + 16 > size) break; p += 16; } // ScriptSerialization offsets
            outExports.push_back(std::move(ex));
        }
    }
}

inline std::string ResolveUnrealPackageIndex(
    int32_t pkgIdx,
    const std::vector<ParsedUnrealImport>& imports,
    const std::vector<ParsedUnrealExport>& exports)
{
    if (pkgIdx == 0) return "None";
    if (pkgIdx < 0) {
        size_t ii = static_cast<size_t>(-pkgIdx - 1);
        if (ii < imports.size()) return imports[ii].className + "'" + imports[ii].objectName + "'";
        return "Import#" + std::to_string(-pkgIdx);
    }
    size_t ei = static_cast<size_t>(pkgIdx - 1);
    if (ei < exports.size()) return exports[ei].objectName;
    return "Export#" + std::to_string(pkgIdx);
}

inline void ParseUnrealTaggedProperties(
    const uint8_t* combined,
    size_t combinedSize,
    size_t uassetHeaderCopy,
    const ParsedUnrealSummary& sum,
    const std::vector<ParsedUnrealImport>& imports,
    const std::vector<ParsedUnrealExport>& exports,
    std::vector<ParsedUnrealProperty>& outProps)
{
    outProps.clear();
    if (exports.empty() || sum.verUe5 >= 1012) return;
    const int32_t ver4 = (sum.verUe4 > 0) ? sum.verUe4 : 522;

    for (size_t ei = 0; ei < exports.size() && outProps.size() < 512; ++ei) {
        const auto& ex = exports[ei];
        if (ex.serialSize <= 8 || ex.serialOffset <= 0) continue;
        size_t startOff = static_cast<size_t>(ex.serialOffset);
        if (startOff + static_cast<size_t>(ex.serialSize) > combinedSize) {
            if (uassetHeaderCopy + startOff + static_cast<size_t>(ex.serialSize) <= combinedSize) {
                startOff += uassetHeaderCopy;
            } else {
                continue;
            }
        }
        const size_t endOff = std::min<size_t>(combinedSize, startOff + static_cast<size_t>(ex.serialSize));
        size_t p = startOff;

        for (int propIter = 0; propIter < 256 && p + 8 <= endOff && outProps.size() < 512; ++propIter) {
            std::string propName;
            if (!ReadUeFName(combined, endOff, p, sum, propName)) break;
            if (EqualsAsciiCI(propName, "None")) break;

            std::string propType;
            if (!ReadUeFName(combined, endOff, p, sum, propType)) break;
            if (p + 8 > endOff) break;
            const int32_t propSize = UeI32(combined + p);
            const int32_t arrIdx   = UeI32(combined + p + 4);
            p += 8;
            if (propSize < 0 || arrIdx < 0) break;

            std::string structName, enumName, innerType, valType;
            uint8_t boolVal = 0;
            if (EqualsAsciiCI(propType, "StructProperty")) {
                if (!ReadUeFName(combined, endOff, p, sum, structName)) break;
                if (ver4 >= 441) { if (p + 16 > endOff) break; p += 16; }
            } else if (EqualsAsciiCI(propType, "BoolProperty")) {
                if (p + 1 > endOff) break;
                boolVal = combined[p++];
            } else if (EqualsAsciiCI(propType, "ByteProperty") || EqualsAsciiCI(propType, "EnumProperty")) {
                if (!ReadUeFName(combined, endOff, p, sum, enumName)) break;
            } else if (EqualsAsciiCI(propType, "ArrayProperty") || EqualsAsciiCI(propType, "SetProperty")) {
                if (!ReadUeFName(combined, endOff, p, sum, innerType)) break;
            } else if (EqualsAsciiCI(propType, "MapProperty")) {
                if (!ReadUeFName(combined, endOff, p, sum, innerType)) break;
                if (!ReadUeFName(combined, endOff, p, sum, valType)) break;
            } else if (!EqualsAsciiCI(propType, "IntProperty") && !EqualsAsciiCI(propType, "Int64Property") &&
                       !EqualsAsciiCI(propType, "UInt32Property") && !EqualsAsciiCI(propType, "UInt64Property") &&
                       !EqualsAsciiCI(propType, "Int16Property") && !EqualsAsciiCI(propType, "UInt16Property") &&
                       !EqualsAsciiCI(propType, "Int8Property") && !EqualsAsciiCI(propType, "FloatProperty") &&
                       !EqualsAsciiCI(propType, "DoubleProperty") && !EqualsAsciiCI(propType, "StrProperty") &&
                       !EqualsAsciiCI(propType, "NameProperty") && !EqualsAsciiCI(propType, "TextProperty") &&
                       !EqualsAsciiCI(propType, "ObjectProperty") && !EqualsAsciiCI(propType, "SoftObjectProperty") &&
                       !EqualsAsciiCI(propType, "WeakObjectProperty") && !EqualsAsciiCI(propType, "LazyObjectProperty"))
            {
                break; // Unrecognized tag -> not a tagged UObject header
            }

            if (ver4 >= 503) {
                if (p + 1 > endOff) break;
                uint8_t hasGuid = combined[p++];
                if (hasGuid == 1) {
                    if (p + 16 > endOff) break;
                    p += 16;
                } else if (hasGuid > 1) {
                    break;
                }
            }

            if (p + static_cast<size_t>(propSize) > endOff) break;
            const uint8_t* vp = combined + p;
            const size_t vsz = static_cast<size_t>(propSize);
            p += vsz;

            std::string typeLabel = propType;
            if (!structName.empty()) typeLabel += "<" + structName + ">";
            else if (!enumName.empty() && !EqualsAsciiCI(enumName, "None")) typeLabel += "<" + enumName + ">";
            else if (!innerType.empty()) typeLabel += "<" + innerType + (valType.empty() ? "" : ("," + valType)) + ">";

            std::string valStr;
            char numBuf[128] = {};
            if (EqualsAsciiCI(propType, "BoolProperty")) {
                valStr = (boolVal != 0) ? "true" : "false";
            } else if (EqualsAsciiCI(propType, "IntProperty") && vsz == 4) {
                valStr = std::to_string(UeI32(vp));
            } else if (EqualsAsciiCI(propType, "UInt32Property") && vsz == 4) {
                valStr = std::to_string(UeU32(vp));
            } else if (EqualsAsciiCI(propType, "Int64Property") && vsz == 8) {
                valStr = std::to_string(UeI64(vp));
            } else if (EqualsAsciiCI(propType, "UInt64Property") && vsz == 8) {
                uint64_t u64 = 0; std::memcpy(&u64, vp, 8);
                valStr = std::to_string(u64);
            } else if (EqualsAsciiCI(propType, "Int16Property") && vsz == 2) {
                int16_t i16 = 0; std::memcpy(&i16, vp, 2);
                valStr = std::to_string(i16);
            } else if (EqualsAsciiCI(propType, "UInt16Property") && vsz == 2) {
                valStr = std::to_string(UeU16(vp));
            } else if (EqualsAsciiCI(propType, "Int8Property") && vsz == 1) {
                valStr = std::to_string(static_cast<int8_t>(vp[0]));
            } else if (EqualsAsciiCI(propType, "FloatProperty") && vsz == 4) {
                std::snprintf(numBuf, sizeof(numBuf), "%.6g", UeF32(vp));
                valStr = numBuf;
            } else if (EqualsAsciiCI(propType, "DoubleProperty") && vsz == 8) {
                std::snprintf(numBuf, sizeof(numBuf), "%.10g", UeF64(vp));
                valStr = numBuf;
            } else if (EqualsAsciiCI(propType, "StrProperty")) {
                size_t sp = 0;
                ReadUeFString(vp, vsz, sp, valStr);
            } else if ((EqualsAsciiCI(propType, "NameProperty") || EqualsAsciiCI(propType, "EnumProperty")) && vsz >= 8) {
                size_t sp = 0;
                ReadUeFName(vp, vsz, sp, sum, valStr);
            } else if (EqualsAsciiCI(propType, "ByteProperty")) {
                if (vsz == 1) valStr = std::to_string(vp[0]);
                else if (vsz >= 8) { size_t sp = 0; ReadUeFName(vp, vsz, sp, sum, valStr); }
            } else if (EqualsAsciiCI(propType, "ObjectProperty") && vsz == 4) {
                valStr = ResolveUnrealPackageIndex(UeI32(vp), imports, exports);
            } else if (EqualsAsciiCI(propType, "SoftObjectProperty") && vsz >= 8) {
                size_t sp = 0;
                std::string assetName, subPath;
                if (ReadUeFName(vp, vsz, sp, sum, assetName)) {
                    valStr = assetName;
                    if (sp < vsz && ReadUeFString(vp, vsz, sp, subPath) && !subPath.empty()) valStr += ":" + subPath;
                }
            } else if (EqualsAsciiCI(propType, "StructProperty")) {
                if ((EqualsAsciiCI(structName, "Vector") || EqualsAsciiCI(structName, "Rotator")) && vsz == 12) {
                    std::snprintf(numBuf, sizeof(numBuf), "(%.4g, %.4g, %.4g)", UeF32(vp), UeF32(vp + 4), UeF32(vp + 8));
                    valStr = numBuf;
                } else if ((EqualsAsciiCI(structName, "Vector") || EqualsAsciiCI(structName, "Rotator")) && vsz == 24) {
                    std::snprintf(numBuf, sizeof(numBuf), "(%.6g, %.6g, %.6g)", UeF64(vp), UeF64(vp + 8), UeF64(vp + 16));
                    valStr = numBuf;
                } else if (EqualsAsciiCI(structName, "Vector2D") && (vsz == 8 || vsz == 16)) {
                    if (vsz == 8) std::snprintf(numBuf, sizeof(numBuf), "(%.4g, %.4g)", UeF32(vp), UeF32(vp + 4));
                    else std::snprintf(numBuf, sizeof(numBuf), "(%.6g, %.6g)", UeF64(vp), UeF64(vp + 8));
                    valStr = numBuf;
                } else if ((EqualsAsciiCI(structName, "LinearColor") || EqualsAsciiCI(structName, "Quat") || EqualsAsciiCI(structName, "Vector4")) && vsz == 16) {
                    std::snprintf(numBuf, sizeof(numBuf), "(%.4g, %.4g, %.4g, %.4g)", UeF32(vp), UeF32(vp + 4), UeF32(vp + 8), UeF32(vp + 12));
                    valStr = numBuf;
                } else if (EqualsAsciiCI(structName, "Color") && vsz == 4) {
                    std::snprintf(numBuf, sizeof(numBuf), "RGBA(%u, %u, %u, %u)", vp[2], vp[1], vp[0], vp[3]);
                    valStr = numBuf;
                } else if (EqualsAsciiCI(structName, "Guid") && vsz == 16) {
                    std::snprintf(numBuf, sizeof(numBuf), "%08X-%04X-%04X-%04X-%04X%08X",
                        UeU32(vp), UeU32(vp + 4) >> 16, UeU32(vp + 4) & 0xFFFFu,
                        UeU32(vp + 8) >> 16, UeU32(vp + 8) & 0xFFFFu, UeU32(vp + 12));
                    valStr = numBuf;
                } else {
                    valStr = std::to_string(vsz) + " B";
                }
            } else if (EqualsAsciiCI(propType, "ArrayProperty") && vsz >= 4) {
                valStr = std::to_string(UeI32(vp)) + " elements (" + std::to_string(vsz) + " B)";
            } else {
                valStr = std::to_string(vsz) + " B";
            }

            if (arrIdx > 0) propName += "[" + std::to_string(arrIdx) + "]";
            outProps.push_back({ ex.objectName, std::move(propName), std::move(typeLabel), std::move(valStr) });
        }
    }
}

} // namespace UnrealPkgDetail

// Fast package classifier used by FileContentPane to route .uasset / .umap to 3D, Image, or Metadata Inspector
inline UnrealPackageAssetKind ClassifyUnrealPackageMemory(const uint8_t* data, size_t size) {
    using namespace UnrealPkgDetail;
    ParsedUnrealSummary sum;
    if (!ParseUnrealPackageSummary(data, size, sum)) return UnrealPackageAssetKind::Unknown;

    bool hasTex = ContainsNameCI(sum, "Texture2D") ||
                  ContainsNameCI(sum, "PF_DXT1") || ContainsNameCI(sum, "PF_DXT3") ||
                  ContainsNameCI(sum, "PF_DXT5") || ContainsNameCI(sum, "PF_BC4") ||
                  ContainsNameCI(sum, "PF_BC5")  || ContainsNameCI(sum, "PF_B8G8R8A8") ||
                  ContainsNameCI(sum, "PF_R8G8B8A8") || ContainsNameCI(sum, "PF_FloatRGBA");
    bool hasMesh = ContainsNameCI(sum, "StaticMesh") || ContainsNameCI(sum, "StaticMeshRenderData");
    if (hasMesh && !hasTex) return UnrealPackageAssetKind::StaticMesh3D;
    if (hasTex && !hasMesh) return UnrealPackageAssetKind::Texture2D;
    if (hasMesh) return UnrealPackageAssetKind::StaticMesh3D;
    return UnrealPackageAssetKind::GenericPackage;
}

} // namespace Formats
