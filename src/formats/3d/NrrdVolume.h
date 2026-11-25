#pragma once
#include "../../ByteIO.h"
#include "../../ReaderTextUtils.h"
#include "../common/MeshBuild3D.h"
// NrrdVolume.h — Zero-dependency C++20 NRRD 3D Volumetric Dataset (.nrrd) + 256-case Marching Cubes reader.

namespace Formats {

namespace DetailNrrd {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdI32BE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64BE;

using ::Explorer2::ReaderText::TrimSv;
inline std::string ToLowerStr(std::string_view s) { return ::Explorer2::ReaderText::ToLowerAscii(s); }
inline std::string ToUpperStr(std::string_view s) { return ::Explorer2::ReaderText::ToUpperAscii(s); }
using ::Explorer2::ReaderText::NextTokenSv;
using ::Explorer2::ReaderText::ParseFloatSv;
using ::Explorer2::ReaderText::ParseDoubleSv;
using ::Explorer2::ReaderText::ParseIntSv;
using ::Explorer2::ReaderText::IsSafeRelativeSubFile;

inline void ParseFloatsFromRowString(std::string_view s, std::vector<double>& outNums) {
    outNums.clear();
    for (size_t i = 0; i < s.size(); ) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '[' || s[i] == ']' || s[i] == '(' || s[i] == ')')) ++i;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '[' && s[j] != ']' && s[j] != '(' && s[j] != ')') ++j;
        double v = 0.0;
        if (ParseDoubleSv(s.substr(i, j - i), v)) outNums.push_back(v);
        i = j;
    }
}

enum class NrrdScalarType {
    Invalid, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64
};

inline NrrdScalarType ParseNrrdType(std::string_view s) {
    std::string t = ToLowerStr(TrimSv(s));
    if (t == "signed char" || t == "int8" || t == "int8_t") return NrrdScalarType::I8;
    if (t == "uchar" || t == "unsigned char" || t == "uint8" || t == "uint8_t") return NrrdScalarType::U8;
    if (t == "short" || t == "short int" || t == "signed short" || t == "signed short int" || t == "int16" || t == "int16_t")
        return NrrdScalarType::I16;
    if (t == "ushort" || t == "unsigned short" || t == "unsigned short int" || t == "uint16" || t == "uint16_t")
        return NrrdScalarType::U16;
    if (t == "int" || t == "signed int" || t == "int32" || t == "int32_t") return NrrdScalarType::I32;
    if (t == "uint" || t == "unsigned int" || t == "uint32" || t == "uint32_t") return NrrdScalarType::U32;
    if (t == "longlong" || t == "long long" || t == "long long int" || t == "signed long long" ||
        t == "signed long long int" || t == "int64" || t == "int64_t")
        return NrrdScalarType::I64;
    if (t == "ulonglong" || t == "unsigned long long" || t == "unsigned long long int" || t == "uint64" || t == "uint64_t")
        return NrrdScalarType::U64;
    if (t == "float") return NrrdScalarType::F32;
    if (t == "double") return NrrdScalarType::F64;
    return NrrdScalarType::Invalid;
}

inline size_t NrrdScalarBytes(NrrdScalarType t) {
    switch (t) {
    case NrrdScalarType::I8:
    case NrrdScalarType::U8:  return 1u;
    case NrrdScalarType::I16:
    case NrrdScalarType::U16: return 2u;
    case NrrdScalarType::I32:
    case NrrdScalarType::U32:
    case NrrdScalarType::F32: return 4u;
    case NrrdScalarType::I64:
    case NrrdScalarType::U64:
    case NrrdScalarType::F64: return 8u;
    default: return 0u;
    }
}

inline double RdNrrdScalar(const uint8_t* p, NrrdScalarType t, bool isBE) {
    switch (t) {
    case NrrdScalarType::I8:  return static_cast<double>(static_cast<int8_t>(p[0]));
    case NrrdScalarType::U8:  return static_cast<double>(p[0]);
    case NrrdScalarType::I16: return static_cast<double>(isBE ? RdI16BE(p) : RdI16LE(p));
    case NrrdScalarType::U16: return static_cast<double>(isBE ? RdU16BE(p) : RdU16LE(p));
    case NrrdScalarType::I32: return static_cast<double>(isBE ? RdI32BE(p) : RdI32LE(p));
    case NrrdScalarType::U32: return static_cast<double>(isBE ? RdU32BE(p) : RdU32LE(p));
    case NrrdScalarType::I64:
        return static_cast<double>(static_cast<int64_t>(isBE ? RdU64BE(p) : RdU64LE(p)));
    case NrrdScalarType::U64:
        return static_cast<double>(isBE ? RdU64BE(p) : RdU64LE(p));
    case NrrdScalarType::F32: return static_cast<double>(isBE ? RdF32BE(p) : RdF32LE(p));
    case NrrdScalarType::F64: return isBE ? RdF64BE(p) : RdF64LE(p);
    default: return 0.0;
    }
}

// Lorensen & Cline / Paul Bourke Full 256-Entry Marching Cubes Triangle Table
inline const int8_t (&GetMcTriTable())[256][16] {
    static const int8_t kTriTable[256][16] = {
        {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,1,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,8,3,9,8,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,8,3,1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {9,2,10,0,2,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {2,8,3,2,10,8,10,9,8,-1,-1,-1,-1,-1,-1,-1},
        {3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,11,2,8,11,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,9,0,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,11,2,1,9,11,9,8,11,-1,-1,-1,-1,-1,-1,-1},
        {3,10,1,11,10,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,10,1,0,8,10,8,11,10,-1,-1,-1,-1,-1,-1,-1},
        {3,9,0,3,11,9,11,10,9,-1,-1,-1,-1,-1,-1,-1},
        {9,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,3,0,7,3,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,1,9,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,1,9,4,7,1,7,3,1,-1,-1,-1,-1,-1,-1,-1},
        {1,2,10,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {3,4,7,3,0,4,1,2,10,-1,-1,-1,-1,-1,-1,-1},
        {9,2,10,9,0,2,8,4,7,-1,-1,-1,-1,-1,-1,-1},
        {2,10,9,2,9,7,2,7,3,7,9,4,-1,-1,-1,-1},
        {8,4,7,3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {11,4,7,11,2,4,2,0,4,-1,-1,-1,-1,-1,-1,-1},
        {9,0,1,8,4,7,2,3,11,-1,-1,-1,-1,-1,-1,-1},
        {4,7,11,9,4,11,9,11,2,9,2,1,-1,-1,-1,-1},
        {3,10,1,3,11,10,7,8,4,-1,-1,-1,-1,-1,-1,-1},
        {1,11,10,1,4,11,1,0,4,7,11,4,-1,-1,-1,-1},
        {4,7,8,9,0,11,9,11,10,11,0,3,-1,-1,-1,-1},
        {4,7,11,4,11,9,9,11,10,-1,-1,-1,-1,-1,-1,-1},
        {9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {9,5,4,0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,5,4,1,5,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {8,5,4,8,3,5,3,1,5,-1,-1,-1,-1,-1,-1,-1},
        {1,2,10,9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {3,0,8,1,2,10,4,9,5,-1,-1,-1,-1,-1,-1,-1},
        {5,2,10,5,4,2,4,0,2,-1,-1,-1,-1,-1,-1,-1},
        {2,10,5,3,2,5,3,5,4,3,4,8,-1,-1,-1,-1},
        {9,5,4,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,11,2,0,8,11,4,9,5,-1,-1,-1,-1,-1,-1,-1},
        {0,5,4,0,1,5,2,3,11,-1,-1,-1,-1,-1,-1,-1},
        {2,1,5,2,5,8,2,8,11,4,8,5,-1,-1,-1,-1},
        {10,3,11,10,1,3,9,5,4,-1,-1,-1,-1,-1,-1,-1},
        {4,9,5,0,8,1,8,10,1,8,11,10,-1,-1,-1,-1},
        {5,4,0,5,0,11,5,11,10,11,0,3,-1,-1,-1,-1},
        {5,4,8,5,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1},
        {9,7,8,5,7,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {9,3,0,9,5,3,5,7,3,-1,-1,-1,-1,-1,-1,-1},
        {0,7,8,0,1,7,1,5,7,-1,-1,-1,-1,-1,-1,-1},
        {1,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {9,7,8,9,5,7,10,1,2,-1,-1,-1,-1,-1,-1,-1},
        {10,1,2,9,5,0,5,3,0,5,7,3,-1,-1,-1,-1},
        {8,0,2,8,2,5,8,5,7,10,5,2,-1,-1,-1,-1},
        {2,10,5,2,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1},
        {7,9,5,7,8,9,3,11,2,-1,-1,-1,-1,-1,-1,-1},
        {9,5,7,9,7,2,9,2,0,2,7,11,-1,-1,-1,-1},
        {2,3,11,0,1,8,1,7,8,1,5,7,-1,-1,-1,-1},
        {11,2,1,11,1,7,7,1,5,-1,-1,-1,-1,-1,-1,-1},
        {9,5,8,8,5,7,10,1,3,10,3,11,-1,-1,-1,-1},
        {5,7,0,5,0,9,7,11,0,1,0,10,11,10,0,-1},
        {11,10,0,11,0,3,10,5,0,8,0,7,5,7,0,-1},
        {11,10,5,7,11,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,8,3,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {9,0,1,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,8,3,1,9,8,5,10,6,-1,-1,-1,-1,-1,-1,-1},
        {1,6,5,2,6,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,6,5,1,2,6,3,0,8,-1,-1,-1,-1,-1,-1,-1},
        {9,6,5,9,0,6,0,2,6,-1,-1,-1,-1,-1,-1,-1},
        {5,9,8,5,8,2,5,2,6,3,2,8,-1,-1,-1,-1},
        {2,3,11,10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {11,0,8,11,2,0,10,6,5,-1,-1,-1,-1,-1,-1,-1},
        {0,1,9,2,3,11,5,10,6,-1,-1,-1,-1,-1,-1,-1},
        {5,10,6,1,9,2,9,11,2,9,8,11,-1,-1,-1,-1},
        {6,3,11,6,5,3,5,1,3,-1,-1,-1,-1,-1,-1,-1},
        {0,8,11,0,11,5,0,5,1,5,11,6,-1,-1,-1,-1},
        {3,11,6,0,3,6,0,6,5,0,5,9,-1,-1,-1,-1},
        {6,5,9,6,9,11,11,9,8,-1,-1,-1,-1,-1,-1,-1},
        {5,10,6,4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,3,0,4,7,3,6,5,10,-1,-1,-1,-1,-1,-1,-1},
        {1,9,0,5,10,6,8,4,7,-1,-1,-1,-1,-1,-1,-1},
        {10,6,5,1,9,7,1,7,3,7,9,4,-1,-1,-1,-1},
        {6,1,2,6,5,1,4,7,8,-1,-1,-1,-1,-1,-1,-1},
        {1,2,5,5,2,6,3,0,4,3,4,7,-1,-1,-1,-1},
        {8,4,7,9,0,5,0,6,5,0,2,6,-1,-1,-1,-1},
        {7,3,9,7,9,4,3,2,9,5,9,6,2,6,9,-1},
        {3,11,2,7,8,4,10,6,5,-1,-1,-1,-1,-1,-1,-1},
        {5,10,6,4,7,2,4,2,0,2,7,11,-1,-1,-1,-1},
        {0,1,9,4,7,8,2,3,11,5,10,6,-1,-1,-1,-1},
        {9,2,1,9,11,2,9,4,11,7,11,4,5,10,6,-1},
        {8,4,7,3,11,5,3,5,1,5,11,6,-1,-1,-1,-1},
        {5,1,11,5,11,6,1,0,11,7,11,4,0,4,11,-1},
        {0,5,9,0,6,5,0,3,6,11,6,3,8,4,7,-1},
        {6,5,9,6,9,11,4,7,9,7,11,9,-1,-1,-1,-1},
        {10,4,9,6,4,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,10,6,4,9,10,0,8,3,-1,-1,-1,-1,-1,-1,-1},
        {10,0,1,10,6,0,6,4,0,-1,-1,-1,-1,-1,-1,-1},
        {8,3,1,8,1,6,8,6,4,6,1,10,-1,-1,-1,-1},
        {1,4,9,1,2,4,2,6,4,-1,-1,-1,-1,-1,-1,-1},
        {3,0,8,1,2,9,2,4,9,2,6,4,-1,-1,-1,-1},
        {0,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {8,3,2,8,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1},
        {10,4,9,10,6,4,11,2,3,-1,-1,-1,-1,-1,-1,-1},
        {0,8,2,2,8,11,4,9,10,4,10,6,-1,-1,-1,-1},
        {3,11,2,0,1,6,0,6,4,6,1,10,-1,-1,-1,-1},
        {6,4,1,6,1,10,4,8,1,2,1,11,8,11,1,-1},
        {9,6,4,9,3,6,9,1,3,11,6,3,-1,-1,-1,-1},
        {8,11,1,8,1,0,11,6,1,9,1,4,6,4,1,-1},
        {3,11,6,3,6,0,0,6,4,-1,-1,-1,-1,-1,-1,-1},
        {6,4,8,11,6,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {7,10,6,7,8,10,8,9,10,-1,-1,-1,-1,-1,-1,-1},
        {0,7,3,0,10,7,0,9,10,6,7,10,-1,-1,-1,-1},
        {10,6,7,1,10,7,1,7,8,1,8,0,-1,-1,-1,-1},
        {10,6,7,10,7,1,1,7,3,-1,-1,-1,-1,-1,-1,-1},
        {1,2,6,1,6,8,1,8,9,8,6,7,-1,-1,-1,-1},
        {2,6,9,2,9,1,6,7,9,0,9,3,7,3,9,-1},
        {7,8,0,7,0,6,6,0,2,-1,-1,-1,-1,-1,-1,-1},
        {7,3,2,6,7,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {2,3,11,10,6,8,10,8,9,8,6,7,-1,-1,-1,-1},
        {2,0,7,2,7,11,0,9,7,6,7,10,9,10,7,-1},
        {1,8,0,1,7,8,1,10,7,6,7,10,2,3,11,-1},
        {11,2,1,11,1,7,10,6,1,6,7,1,-1,-1,-1,-1},
        {8,9,6,8,6,7,9,1,6,11,6,3,1,3,6,-1},
        {0,9,1,11,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {7,8,0,7,0,6,3,11,0,11,6,0,-1,-1,-1,-1},
        {7,11,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {3,0,8,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,1,9,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {8,1,9,8,3,1,11,7,6,-1,-1,-1,-1,-1,-1,-1},
        {10,1,2,6,11,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,2,10,3,0,8,6,11,7,-1,-1,-1,-1,-1,-1,-1},
        {2,9,0,2,10,9,6,11,7,-1,-1,-1,-1,-1,-1,-1},
        {6,11,7,2,10,3,10,8,3,10,9,8,-1,-1,-1,-1},
        {7,2,3,6,2,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {7,0,8,7,6,0,6,2,0,-1,-1,-1,-1,-1,-1,-1},
        {2,7,6,2,3,7,0,1,9,-1,-1,-1,-1,-1,-1,-1},
        {1,6,2,1,8,6,1,9,8,8,7,6,-1,-1,-1,-1},
        {10,7,6,10,1,7,1,3,7,-1,-1,-1,-1,-1,-1,-1},
        {10,7,6,1,7,10,1,8,7,1,0,8,-1,-1,-1,-1},
        {0,3,7,0,7,10,0,10,9,6,10,7,-1,-1,-1,-1},
        {7,6,10,7,10,8,8,10,9,-1,-1,-1,-1,-1,-1,-1},
        {6,8,4,11,8,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {3,6,11,3,0,6,0,4,6,-1,-1,-1,-1,-1,-1,-1},
        {8,6,11,8,4,6,9,0,1,-1,-1,-1,-1,-1,-1,-1},
        {9,4,6,9,6,3,9,3,1,11,3,6,-1,-1,-1,-1},
        {6,8,4,6,11,8,2,10,1,-1,-1,-1,-1,-1,-1,-1},
        {1,2,10,3,0,11,0,6,11,0,4,6,-1,-1,-1,-1},
        {4,11,8,4,6,11,0,2,9,2,10,9,-1,-1,-1,-1},
        {10,9,3,10,3,2,9,4,3,11,3,6,4,6,3,-1},
        {8,2,3,8,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1},
        {0,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,9,0,2,3,4,2,4,6,4,3,8,-1,-1,-1,-1},
        {1,9,4,1,4,2,2,4,6,-1,-1,-1,-1,-1,-1,-1},
        {8,1,3,8,6,1,8,4,6,6,10,1,-1,-1,-1,-1},
        {10,1,0,10,0,6,6,0,4,-1,-1,-1,-1,-1,-1,-1},
        {4,6,3,4,3,8,6,10,3,0,3,9,10,9,3,-1},
        {10,9,4,6,10,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,9,5,7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,8,3,4,9,5,11,7,6,-1,-1,-1,-1,-1,-1,-1},
        {5,0,1,5,4,0,7,6,11,-1,-1,-1,-1,-1,-1,-1},
        {11,7,6,8,3,4,3,5,4,3,1,5,-1,-1,-1,-1},
        {9,5,4,10,1,2,7,6,11,-1,-1,-1,-1,-1,-1,-1},
        {6,11,7,1,2,10,0,8,3,4,9,5,-1,-1,-1,-1},
        {7,6,11,5,4,10,4,2,10,4,0,2,-1,-1,-1,-1},
        {3,4,8,3,5,4,3,2,5,10,5,2,11,7,6,-1},
        {7,2,3,7,6,2,5,4,9,-1,-1,-1,-1,-1,-1,-1},
        {9,5,4,0,8,6,0,6,2,6,8,7,-1,-1,-1,-1},
        {3,6,2,3,7,6,1,5,0,5,4,0,-1,-1,-1,-1},
        {6,2,8,6,8,7,2,1,8,4,8,5,1,5,8,-1},
        {9,5,4,10,1,6,1,7,6,1,3,7,-1,-1,-1,-1},
        {1,6,10,1,7,6,1,0,7,8,7,0,9,5,4,-1},
        {4,0,10,4,10,5,0,3,10,6,10,7,3,7,10,-1},
        {7,6,10,7,10,8,5,4,10,4,8,10,-1,-1,-1,-1},
        {6,9,5,6,11,9,11,8,9,-1,-1,-1,-1,-1,-1,-1},
        {3,6,11,0,6,3,0,5,6,0,9,5,-1,-1,-1,-1},
        {0,11,8,0,5,11,0,1,5,5,6,11,-1,-1,-1,-1},
        {6,11,3,6,3,5,5,3,1,-1,-1,-1,-1,-1,-1,-1},
        {1,2,10,9,5,11,9,11,8,11,5,6,-1,-1,-1,-1},
        {0,11,3,0,6,11,0,9,6,5,6,9,1,2,10,-1},
        {11,8,5,11,5,6,8,0,5,10,5,2,0,2,5,-1},
        {6,11,3,6,3,5,2,10,3,10,5,3,-1,-1,-1,-1},
        {5,8,9,5,2,8,5,6,2,3,8,2,-1,-1,-1,-1},
        {9,5,6,9,6,0,0,6,2,-1,-1,-1,-1,-1,-1,-1},
        {1,5,8,1,8,0,5,6,8,3,8,2,6,2,8,-1},
        {1,5,6,2,1,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,3,6,1,6,10,3,8,6,5,6,9,8,9,6,-1},
        {10,1,0,10,0,6,9,5,0,5,6,0,-1,-1,-1,-1},
        {0,3,8,5,6,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {10,5,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {11,5,10,7,5,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {11,5,10,11,7,5,8,3,0,-1,-1,-1,-1,-1,-1,-1},
        {5,11,7,5,10,11,1,9,0,-1,-1,-1,-1,-1,-1,-1},
        {10,7,5,10,11,7,9,8,1,8,3,1,-1,-1,-1,-1},
        {11,1,2,11,7,1,7,5,1,-1,-1,-1,-1,-1,-1,-1},
        {0,8,3,1,2,7,1,7,5,7,2,11,-1,-1,-1,-1},
        {9,7,5,9,2,7,9,0,2,2,11,7,-1,-1,-1,-1},
        {7,5,2,7,2,11,5,9,2,3,2,8,9,8,2,-1},
        {2,5,10,2,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1},
        {8,2,0,8,5,2,8,7,5,10,2,5,-1,-1,-1,-1},
        {9,0,1,5,10,3,5,3,7,3,10,2,-1,-1,-1,-1},
        {9,8,2,9,2,1,8,7,2,10,2,5,7,5,2,-1},
        {1,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,8,7,0,7,1,1,7,5,-1,-1,-1,-1,-1,-1,-1},
        {9,0,3,9,3,5,5,3,7,-1,-1,-1,-1,-1,-1,-1},
        {9,8,7,5,9,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {5,8,4,5,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1},
        {5,0,4,5,11,0,5,10,11,11,3,0,-1,-1,-1,-1},
        {0,1,9,8,4,10,8,10,11,10,4,5,-1,-1,-1,-1},
        {10,11,4,10,4,5,11,3,4,9,4,1,3,1,4,-1},
        {2,5,1,2,8,5,2,11,8,4,5,8,-1,-1,-1,-1},
        {0,4,11,0,11,3,4,5,11,2,11,1,5,1,11,-1},
        {0,2,5,0,5,9,2,11,5,4,5,8,11,8,5,-1},
        {9,4,5,2,11,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {2,5,10,3,5,2,3,4,5,3,8,4,-1,-1,-1,-1},
        {5,10,2,5,2,4,4,2,0,-1,-1,-1,-1,-1,-1,-1},
        {3,10,2,3,5,10,3,8,5,4,5,8,0,1,9,-1},
        {5,10,2,5,2,4,1,9,2,9,4,2,-1,-1,-1,-1},
        {8,4,5,8,5,3,3,5,1,-1,-1,-1,-1,-1,-1,-1},
        {0,4,5,1,0,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {8,4,5,8,5,3,9,0,5,0,3,5,-1,-1,-1,-1},
        {9,4,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,11,7,4,9,11,9,10,11,-1,-1,-1,-1,-1,-1,-1},
        {0,8,3,4,9,7,9,11,7,9,10,11,-1,-1,-1,-1},
        {1,10,11,1,11,4,1,4,0,7,4,11,-1,-1,-1,-1},
        {3,1,4,3,4,8,1,10,4,7,4,11,10,11,4,-1},
        {4,11,7,9,11,4,9,2,11,9,1,2,-1,-1,-1,-1},
        {9,7,4,9,11,7,9,1,11,2,11,1,0,8,3,-1},
        {11,7,4,11,4,2,2,4,0,-1,-1,-1,-1,-1,-1,-1},
        {11,7,4,11,4,2,8,3,4,3,2,4,-1,-1,-1,-1},
        {2,9,10,2,7,9,2,3,7,7,4,9,-1,-1,-1,-1},
        {9,10,7,9,7,4,10,2,7,8,7,0,2,0,7,-1},
        {3,7,10,3,10,2,7,4,10,1,10,0,4,0,10,-1},
        {1,10,2,8,7,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,9,1,4,1,7,7,1,3,-1,-1,-1,-1,-1,-1,-1},
        {4,9,1,4,1,7,0,8,1,8,7,1,-1,-1,-1,-1},
        {4,0,3,7,4,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {4,8,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {9,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {3,0,9,3,9,11,11,9,10,-1,-1,-1,-1,-1,-1,-1},
        {0,1,10,0,10,8,8,10,11,-1,-1,-1,-1,-1,-1,-1},
        {3,1,10,11,3,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,2,11,1,11,9,9,11,8,-1,-1,-1,-1,-1,-1,-1},
        {3,0,9,3,9,11,1,2,9,2,11,9,-1,-1,-1,-1},
        {0,2,11,8,0,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {3,2,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {2,3,8,2,8,10,10,8,9,-1,-1,-1,-1,-1,-1,-1},
        {9,10,2,0,9,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {2,3,8,2,8,10,0,1,8,1,10,8,-1,-1,-1,-1},
        {1,10,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {1,3,8,9,1,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,9,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {0,3,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
        {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}
    };
    return kTriTable;
}

inline double ComputeMidpointOtsuThreshold(const std::vector<float>& vol, double minV, double maxV) {
    constexpr int kBins = 256;
    double span = maxV - minV;
    if (span <= 1e-12 || vol.empty()) return 0.5 * (minV + maxV);

    uint64_t hist[kBins] = {};
    double invSpan = static_cast<double>(kBins - 1) / span;
    for (float fv : vol) {
        int b = std::clamp(static_cast<int>((static_cast<double>(fv) - minV) * invSpan + 0.5), 0, kBins - 1);
        ++hist[b];
    }

    double total = static_cast<double>(vol.size());
    double sumAll = 0.0;
    for (int i = 0; i < kBins; ++i) sumAll += static_cast<double>(i) * static_cast<double>(hist[i]);

    double wB = 0.0, sumB = 0.0;
    double bestVar = -1.0;
    int bestLo = kBins / 2, bestHi = kBins / 2;

    for (int t = 0; t < kBins - 1; ++t) {
        wB += static_cast<double>(hist[t]);
        if (wB <= 0.0) continue;
        double wF = total - wB;
        if (wF <= 0.0) break;
        sumB += static_cast<double>(t) * static_cast<double>(hist[t]);
        double mB = sumB / wB;
        double mF = (sumAll - sumB) / wF;
        double varBetween = wB * wF * (mB - mF) * (mB - mF);
        if (varBetween > bestVar + 1e-9) {
            bestVar = varBetween;
            bestLo = t;
            bestHi = t;
        } else if (std::fabs(varBetween - bestVar) <= 1e-9) {
            bestHi = t;
        }
    }

    if (bestVar <= 0.0) return 0.5 * (minV + maxV);
    double midBin = 0.5 * (static_cast<double>(bestLo + bestHi) + 1.0);
    double iso = minV + (midBin / static_cast<double>(kBins - 1)) * span;
    return std::clamp(iso, minV + 0.1 * span, maxV - 0.1 * span);
}

inline bool ExtractMarchingCubesIsosurface(
    const std::vector<float>& vol,
    size_t nx,
    size_t ny,
    size_t nz,
    float isoVal,
    float bgVal,
    const double spaceDirs[3][3],
    LoadedMesh3D& mesh)
{
    if (nx < 2u || ny < 2u || nz < 2u || vol.size() < nx * ny * nz) return false;

    static const int kCornerOff[8][3] = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}
    };
    static const int kEdgeCorners[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    const auto& triTable = GetMcTriTable();

    auto sampleAt = [&](int ix, int iy, int iz) -> float {
        if (ix < 0 || iy < 0 || iz < 0 ||
            static_cast<size_t>(ix) >= nx ||
            static_cast<size_t>(iy) >= ny ||
            static_cast<size_t>(iz) >= nz)
        {
            return bgVal;
        }
        return vol[(static_cast<size_t>(iz) * ny + static_cast<size_t>(iy)) * nx + static_cast<size_t>(ix)];
    };

    auto gridToWorld = [&](double gx, double gy, double gz) -> Vec3f {
        double wx = spaceDirs[0][0] * gx + spaceDirs[1][0] * gy + spaceDirs[2][0] * gz;
        double wy = spaceDirs[0][1] * gx + spaceDirs[1][1] * gy + spaceDirs[2][1] * gz;
        double wz = spaceDirs[0][2] * gx + spaceDirs[1][2] * gy + spaceDirs[2][2] * gz;
        return { static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz) };
    };

    double det = spaceDirs[0][0] * (spaceDirs[1][1] * spaceDirs[2][2] - spaceDirs[1][2] * spaceDirs[2][1]) -
                 spaceDirs[0][1] * (spaceDirs[1][0] * spaceDirs[2][2] - spaceDirs[1][2] * spaceDirs[2][0]) +
                 spaceDirs[0][2] * (spaceDirs[1][0] * spaceDirs[2][1] - spaceDirs[1][1] * spaceDirs[2][0]);
    bool negSpaceDet = (det < 0.0);

    // Pack directed grid edge key (ox, oy, oz in [-1..N], axis in 0..2) so shared grid edges weld into a watertight mesh!
    std::unordered_map<uint64_t, uint32_t> edgeVertCache;
    edgeVertCache.reserve(std::min<size_t>(nx * ny * nz, 262144u));

    auto getOrCreateEdgeVert = [&](int ax, int ay, int az, float va, int bx, int by, int bz, float vb) -> uint32_t {
        // Canonical order: a <= b componentwise
        if (ax > bx || ay > by || az > bz) {
            std::swap(ax, bx); std::swap(ay, by); std::swap(az, bz);
            std::swap(va, vb);
        }
        uint64_t axis = (bx > ax) ? 0ULL : ((by > ay) ? 1ULL : 2ULL);
        uint64_t kx = static_cast<uint64_t>(ax + 2) & 0xFFFFULL;
        uint64_t ky = static_cast<uint64_t>(ay + 2) & 0xFFFFULL;
        uint64_t kz = static_cast<uint64_t>(az + 2) & 0xFFFFULL;
        uint64_t key = (kx << 34u) | (ky << 18u) | (kz << 2u) | axis;

        auto it = edgeVertCache.find(key);
        if (it != edgeVertCache.end()) return it->second;

        double denom = static_cast<double>(vb) - static_cast<double>(va);
        double t = (std::fabs(denom) > 1e-12)
            ? std::clamp((static_cast<double>(isoVal) - static_cast<double>(va)) / denom, 0.0, 1.0)
            : 0.5;
        double gx = static_cast<double>(ax) + t * static_cast<double>(bx - ax);
        double gy = static_cast<double>(ay) + t * static_cast<double>(by - ay);
        double gz = static_cast<double>(az) + t * static_cast<double>(bz - az);

        uint32_t vIdx = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(gridToWorld(gx, gy, gz));
        edgeVertCache.emplace(key, vIdx);
        return vIdx;
    };

    const int maxX = static_cast<int>(nx);
    const int maxY = static_cast<int>(ny);
    const int maxZ = static_cast<int>(nz);

    // Sweep [-1 .. N-1] along each axis so foreground touching volume boundaries is capped into a closed surface
    for (int iz = -1; iz < maxZ && mesh.tris.size() < 3000000u; ++iz) {
        for (int iy = -1; iy < maxY; ++iy) {
            for (int ix = -1; ix < maxX; ++ix) {
                float cv[8];
                uint32_t cubeIdx = 0u;
                for (int c = 0; c < 8; ++c) {
                    cv[c] = sampleAt(ix + kCornerOff[c][0], iy + kCornerOff[c][1], iz + kCornerOff[c][2]);
                    if (cv[c] >= isoVal) cubeIdx |= (1u << c);
                }
                if (cubeIdx == 0u || cubeIdx == 255u) continue;

                uint32_t vertList[12] = {};
                for (int e = 0; e < 12; ++e) {
                    int c0 = kEdgeCorners[e][0];
                    int c1 = kEdgeCorners[e][1];
                    if (((cubeIdx >> c0) & 1u) != ((cubeIdx >> c1) & 1u)) {
                        vertList[e] = getOrCreateEdgeVert(
                            ix + kCornerOff[c0][0], iy + kCornerOff[c0][1], iz + kCornerOff[c0][2], cv[c0],
                            ix + kCornerOff[c1][0], iy + kCornerOff[c1][1], iz + kCornerOff[c1][2], cv[c1]);
                    }
                }

                const int8_t* row = triTable[cubeIdx];
                for (int t = 0; row[t] != -1; t += 3) {
                    uint32_t i0 = vertList[static_cast<int>(row[t + 0])];
                    uint32_t i1 = vertList[static_cast<int>(row[t + 1])];
                    uint32_t i2 = vertList[static_cast<int>(row[t + 2])];
                    if (i0 == i1 || i1 == i2 || i0 == i2) continue;
                    // With bit=1 for inside (val >= iso), swapping (i1, i2) orients triangles outward toward val < iso!
                    if (negSpaceDet) mesh.tris.push_back({ i0, i1, i2, {} });
                    else             mesh.tris.push_back({ i0, i2, i1, {} });
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailNrrd

inline bool ParseNrrdVolumeMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    using namespace DetailNrrd;
    if (!data || size < 24u || std::memcmp(data, "NRRD000", 7) != 0 || data[7] < '1' || data[7] > '8') {
        return false;
    }

    // Locate blank line (\n\n or \r\n\r\n) separating ASCII header from binary/encoded payload
    std::string_view fullSv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 262144u));
    size_t hdrEnd = std::string_view::npos;
    size_t dataStart = 0u;
    for (size_t i = 8u; i + 1u < fullSv.size(); ++i) {
        if (fullSv[i] == '\n' && fullSv[i + 1u] == '\n') {
            hdrEnd = i;
            dataStart = i + 2u;
            break;
        }
        if (i + 3u < fullSv.size() && fullSv[i] == '\r' && fullSv[i + 1u] == '\n' &&
            fullSv[i + 2u] == '\r' && fullSv[i + 3u] == '\n')
        {
            hdrEnd = i;
            dataStart = i + 4u;
            break;
        }
    }
    if (hdrEnd == std::string_view::npos) hdrEnd = fullSv.size();

    NrrdScalarType scalarType = NrrdScalarType::Invalid;
    int dimension = 0;
    std::vector<size_t> sizes;
    bool isBE = false;
    std::string encoding = "raw";
    std::vector<double> spacings;
    double spaceDirs[3][3] = { {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0} };
    bool hasSpaceDirs = false;
    int64_t lineSkip = 0;
    int64_t byteSkip = 0;
    std::string detachedFile;

    std::string_view hdrSv = fullSv.substr(8u, hdrEnd - 8u);
    size_t hp = 0u;
    while (hp < hdrSv.size()) {
        size_t eol = hdrSv.find('\n', hp);
        if (eol == std::string_view::npos) eol = hdrSv.size();
        std::string_view ln = TrimSv(hdrSv.substr(hp, eol - hp));
        hp = eol + 1u;
        if (ln.empty() || ln.front() == '#') continue;

        size_t colon = ln.find(':');
        if (colon == std::string_view::npos) continue;
        if (colon + 1u < ln.size() && ln[colon + 1u] == '=') continue; // key:=value user metadata

        std::string key = ToLowerStr(TrimSv(ln.substr(0u, colon)));
        std::string_view val = TrimSv(ln.substr(colon + 1u));

        if (key == "type") {
            scalarType = ParseNrrdType(val);
        } else if (key == "dimension") {
            ParseIntSv(val, dimension);
        } else if (key == "sizes") {
            sizes.clear();
            std::string_view rem = val, tok;
            while (NextTokenSv(rem, tok)) {
                int s = 0;
                if (!ParseIntSv(tok, s) || s <= 0) return false;
                sizes.push_back(static_cast<size_t>(s));
            }
        } else if (key == "endian") {
            isBE = (ToLowerStr(val) == "big");
        } else if (key == "encoding") {
            encoding = ToLowerStr(val);
        } else if (key == "spacings") {
            spacings.clear();
            std::string_view rem = val, tok;
            while (NextTokenSv(rem, tok)) {
                double sp = 1.0;
                if (ParseDoubleSv(tok, sp) && std::fabs(sp) > 1e-6) spacings.push_back(std::fabs(sp));
                else spacings.push_back(0.0); // NaN / none on non-spatial axis
            }
        } else if (key == "space directions") {
            int spAxis = 0;
            size_t p = 0u;
            while (p < val.size() && spAxis < 3) {
                while (p < val.size() && (val[p] == ' ' || val[p] == '\t')) ++p;
                if (p >= val.size()) break;
                if (val[p] == '(') {
                    size_t cl = val.find(')', p + 1u);
                    if (cl == std::string_view::npos) break;
                    std::string vecStr(val.substr(p + 1u, cl - (p + 1u)));
                    for (char& c : vecStr) if (c == ',') c = ' ';
                    std::vector<double> comps;
                    ParseFloatsFromRowString(vecStr, comps);
                    if (comps.size() >= 3u) {
                        spaceDirs[spAxis][0] = comps[0];
                        spaceDirs[spAxis][1] = comps[1];
                        spaceDirs[spAxis][2] = comps[2];
                        hasSpaceDirs = true;
                        ++spAxis;
                    }
                    p = cl + 1u;
                } else {
                    // "none" on channel axis
                    while (p < val.size() && val[p] != ' ' && val[p] != '\t') ++p;
                }
            }
        } else if (key == "line skip" || key == "lineskip") {
            int ls = 0;
            if (ParseIntSv(val, ls) && ls >= 0) lineSkip = ls;
        } else if (key == "byte skip" || key == "byteskip") {
            int bs = 0;
            if (ParseIntSv(val, bs)) byteSkip = bs;
        } else if (key == "data file" || key == "datafile") {
            detachedFile.assign(val);
        }
    }

    if (scalarType == NrrdScalarType::Invalid || (dimension != 3 && dimension != 4) ||
        sizes.size() != static_cast<size_t>(dimension))
    {
        return false;
    }

    size_t nx = 0, ny = 0, nz = 0, nChan = 1u;
    bool chanFirst = true;
    int spatialStartAxis = 0;
    if (dimension == 3) {
        nx = sizes[0]; ny = sizes[1]; nz = sizes[2];
    } else {
        if (sizes[0] >= 1u && sizes[0] <= 4u && sizes[1] >= 2u && sizes[2] >= 2u && sizes[3] >= 2u) {
            nChan = sizes[0]; nx = sizes[1]; ny = sizes[2]; nz = sizes[3];
            chanFirst = true; spatialStartAxis = 1;
        } else if (sizes[3] >= 1u && sizes[3] <= 4u && sizes[0] >= 2u && sizes[1] >= 2u && sizes[2] >= 2u) {
            nx = sizes[0]; ny = sizes[1]; nz = sizes[2]; nChan = sizes[3];
            chanFirst = false; spatialStartAxis = 0;
        } else {
            return false;
        }
    }

    if (nx < 2u || ny < 2u || nz < 2u) return false;
    uint64_t totalVoxels = static_cast<uint64_t>(nx) * ny * nz;
    uint64_t totalScalars = totalVoxels * nChan;
    if (totalVoxels == 0ULL || totalScalars > 64ULL * 1024ULL * 1024ULL) return false;

    if (!hasSpaceDirs && spacings.size() >= static_cast<size_t>(spatialStartAxis + 3)) {
        for (int a = 0; a < 3; ++a) {
            double s = spacings[static_cast<size_t>(spatialStartAxis + a)];
            spaceDirs[a][0] = (a == 0) ? (s > 0.0 ? s : 1.0) : 0.0;
            spaceDirs[a][1] = (a == 1) ? (s > 0.0 ? s : 1.0) : 0.0;
            spaceDirs[a][2] = (a == 2) ? (s > 0.0 ? s : 1.0) : 0.0;
        }
    }

    // Resolve inline payload or detached companion file
    std::vector<uint8_t> extPayload;
    const uint8_t* payPtr = nullptr;
    size_t payLen = 0u;
    if (!detachedFile.empty()) {
        if (fullPath.empty() || !IsSafeRelativeSubFile(detachedFile)) return false;
        std::wstring baseDir = PathUtils::GetParentPath(fullPath);
#ifdef _WIN32
        const wchar_t sep = L'\\';
#else
        const wchar_t sep = L'/';
#endif
        std::wstring dPath = baseDir + sep + PathUtils::Utf8ToWide(detachedFile);
        if (!PathUtils::ReadFileBytes(dPath, 256 * 1024 * 1024, extPayload) || extPayload.empty()) return false;
        payPtr = extPayload.data();
        payLen = extPayload.size();
    } else {
        if (dataStart == 0u || dataStart >= size) return false;
        payPtr = data + dataStart;
        payLen = size - dataStart;
    }

    // Decompress if gz / gzip
    std::vector<uint8_t> gzDecoded;
    if (encoding == "gz" || encoding == "gzip") {
        // For uncompressed line/byte skip before gzip or inside gzip: NRRD spec applies line/byte skip inside decompressed stream,
        // unless lineSkip precedes gzip magic 1F 8B!
        size_t preOff = 0u;
        if (payLen >= 2u && !(payPtr[0] == 0x1Fu && payPtr[1] == 0x8Bu) && lineSkip > 0) {
            for (int64_t l = 0; l < lineSkip && preOff < payLen; ++l) {
                while (preOff < payLen && payPtr[preOff] != '\n') ++preOff;
                if (preOff < payLen) ++preOff;
            }
            lineSkip = 0;
        }
        size_t maxExpectedBytes = static_cast<size_t>(totalScalars) * NrrdScalarBytes(scalarType) + 65536u;
        maxExpectedBytes = std::min<size_t>(std::max<size_t>(maxExpectedBytes, 1024u * 1024u), 256u * 1024u * 1024u);
        if (!ZipDeflate::InflateGzip(payPtr + preOff, payLen - preOff, gzDecoded, maxExpectedBytes)) {
            if (!ZipDeflate::InflateZlib(payPtr + preOff, payLen - preOff, gzDecoded, maxExpectedBytes)) {
                return false;
            }
        }
        payPtr = gzDecoded.data();
        payLen = gzDecoded.size();
        encoding = "raw";
    }

    // Apply line skip & byte skip
    size_t curOff = 0u;
    for (int64_t l = 0; l < lineSkip && curOff < payLen; ++l) {
        while (curOff < payLen && payPtr[curOff] != '\n') ++curOff;
        if (curOff < payLen) ++curOff;
    }

    const size_t elemSz = NrrdScalarBytes(scalarType);
    const size_t needRawBytes = static_cast<size_t>(totalScalars) * elemSz;
    if (byteSkip == -1 && encoding == "raw") {
        if (payLen < needRawBytes) return false;
        curOff = payLen - needRawBytes;
    } else if (byteSkip > 0) {
        if (static_cast<size_t>(byteSkip) > payLen - curOff) return false;
        curOff += static_cast<size_t>(byteSkip);
    }

    const uint8_t* bodyPtr = payPtr + curOff;
    const size_t bodyLen   = payLen - curOff;

    // Decode scalar values
    std::vector<double> rawScalars;
    if (encoding == "raw") {
        if (bodyLen < needRawBytes) return false;
        rawScalars.resize(static_cast<size_t>(totalScalars));
        for (size_t i = 0; i < static_cast<size_t>(totalScalars); ++i) {
            rawScalars[i] = RdNrrdScalar(bodyPtr + i * elemSz, scalarType, isBE);
        }
    } else if (encoding == "ascii" || encoding == "txt" || encoding == "text") {
        rawScalars.reserve(static_cast<size_t>(totalScalars));
        std::string_view asv(reinterpret_cast<const char*>(bodyPtr), bodyLen);
        size_t ap = 0u;
        while (ap < asv.size() && rawScalars.size() < static_cast<size_t>(totalScalars)) {
            while (ap < asv.size() && (asv[ap] == ' ' || asv[ap] == '\t' || asv[ap] == '\r' || asv[ap] == '\n' || asv[ap] == ',')) ++ap;
            if (ap >= asv.size()) break;
            size_t ae = ap;
            while (ae < asv.size() && asv[ae] != ' ' && asv[ae] != '\t' && asv[ae] != '\r' && asv[ae] != '\n' && asv[ae] != ',') ++ae;
            double v = 0.0;
            if (!ParseDoubleSv(asv.substr(ap, ae - ap), v)) return false;
            rawScalars.push_back(v);
            ap = ae;
        }
        if (rawScalars.size() < static_cast<size_t>(totalScalars)) return false;
    } else if (encoding == "hex") {
        std::vector<uint8_t> hexBytes;
        hexBytes.reserve(needRawBytes);
        auto hexNibble = [](uint8_t c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };
        int hi = -1;
        for (size_t i = 0; i < bodyLen && hexBytes.size() < needRawBytes; ++i) {
            int nib = hexNibble(bodyPtr[i]);
            if (nib < 0) continue;
            if (hi < 0) { hi = nib; }
            else { hexBytes.push_back(static_cast<uint8_t>((hi << 4) | nib)); hi = -1; }
        }
        if (hexBytes.size() < needRawBytes) return false;
        rawScalars.resize(static_cast<size_t>(totalScalars));
        for (size_t i = 0; i < static_cast<size_t>(totalScalars); ++i) {
            rawScalars[i] = RdNrrdScalar(hexBytes.data() + i * elemSz, scalarType, isBE);
        }
    } else {
        return false;
    }

    // Collapse multi-channel if needed & downsample cleanly if grid exceeds 96^3
    size_t stepX = (nx + 95u) / 96u;
    size_t stepY = (ny + 95u) / 96u;
    size_t stepZ = (nz + 95u) / 96u;
    size_t dxN = (nx + stepX - 1u) / stepX;
    size_t dyN = (ny + stepY - 1u) / stepY;
    size_t dzN = (nz + stepZ - 1u) / stepZ;
    if (dxN < 2u || dyN < 2u || dzN < 2u) return false;

    double dsSpaceDirs[3][3] = {};
    for (int k = 0; k < 3; ++k) {
        dsSpaceDirs[0][k] = spaceDirs[0][k] * static_cast<double>(stepX);
        dsSpaceDirs[1][k] = spaceDirs[1][k] * static_cast<double>(stepY);
        dsSpaceDirs[2][k] = spaceDirs[2][k] * static_cast<double>(stepZ);
    }

    std::vector<float> scalarVol(dxN * dyN * dzN, 0.0f);
    double minVal = 1e300, maxVal = -1e300;
    for (size_t iz = 0; iz < dzN; ++iz) {
        size_t sz = std::min(iz * stepZ, nz - 1u);
        for (size_t iy = 0; iy < dyN; ++iy) {
            size_t sy = std::min(iy * stepY, ny - 1u);
            for (size_t ix = 0; ix < dxN; ++ix) {
                size_t sx = std::min(ix * stepX, nx - 1u);
                size_t voxIdx = (sz * ny + sy) * nx + sx;
                double v = 0.0;
                if (nChan == 1u) {
                    v = rawScalars[voxIdx];
                } else if (chanFirst) {
                    size_t useC = (nChan >= 3u) ? 3u : 1u;
                    for (size_t c = 0; c < useC; ++c) v += rawScalars[voxIdx * nChan + c];
                    v /= static_cast<double>(useC);
                } else {
                    size_t useC = (nChan >= 3u) ? 3u : 1u;
                    for (size_t c = 0; c < useC; ++c) v += rawScalars[c * static_cast<size_t>(totalVoxels) + voxIdx];
                    v /= static_cast<double>(useC);
                }
                if (std::isfinite(v)) {
                    minVal = std::min(minVal, v);
                    maxVal = std::max(maxVal, v);
                    scalarVol[(iz * dyN + iy) * dxN + ix] = static_cast<float>(v);
                }
            }
        }
    }

    if (!(maxVal > minVal + 1e-9)) return false;
    for (float& fv : scalarVol) {
        if (!std::isfinite(fv)) fv = static_cast<float>(minVal);
    }

    double iso = ComputeMidpointOtsuThreshold(scalarVol, minVal, maxVal);
    float bgVal = static_cast<float>(minVal - (maxVal - minVal) * 0.25);

    mesh.vertices.clear();
    mesh.tris.clear();
    if (!ExtractMarchingCubesIsosurface(scalarVol, dxN, dyN, dzN, static_cast<float>(iso), bgVal, dsSpaceDirs, mesh)) {
        return false;
    }

    mesh.formatName = L"NRRD 3D Volumetric Isosurface (.nrrd)";
    return true;
}

} // namespace Formats
