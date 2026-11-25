// DocReadersSecurityBin.cpp — Executables, Cryptography, Packet Captures, Forensics, SBOM & KV Store Readers
// Compiled in its own translation unit so DocReaders.cpp stays fast and low-memory on MSVC and Linux GCC.
#include "DocReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#ifdef _WIN32
#include <wincrypt.h>
#include <bcrypt.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Explorer2 {

#include "formats/common/GpsTrackMap.h"
#include "formats/common/DocFormatCommon.h"
#include "formats/security/ElfBinary.h"
#include "formats/security/WebAssemblyWasm.h"
#include "formats/security/WindowsPeBinary.h"
#include "formats/security/WindowsShortcutLnk.h"
#include "formats/security/WindowsPrefetchPf.h"
#include "formats/security/WindowsEvtxLog.h"
#include "formats/security/WindowsMinidumpDmp.h"
#include "formats/security/MsfPdbSymbols.h"
#include "formats/security/JavaClassFile.h"
#include "formats/security/AndroidDexFile.h"
#include "formats/security/AppleMachOBinary.h"
#include "formats/security/X509Cert.h"
#include "formats/security/Pkcs10Csr.h"
#include "formats/security/Pkcs7Bundle.h"
#include "formats/security/Pkcs12Keystore.h"
#include "formats/security/KeePassKdbx.h"
#include "formats/security/LibPcap.h"
#include "formats/security/PcapNgCapture.h"
#include "formats/security/Uf2Firmware.h"
#include "formats/data/AccessDatabaseMdb.h"
#include "formats/data/RedisRdb.h"
#include "formats/data/RocksLevelDbSst.h"
#include "formats/gis/PmTilesMap.h"
#include "formats/gis/OsmPbfMap.h"
#include "formats/data/ProtobufWire.h"
#include "formats/data/TerraformState.h"
#include "formats/security/SarifLog.h"
#include "formats/security/SpdxSbom.h"
#include "formats/security/CycloneDxSbom.h"
#include "formats/security/OpenPgpPacket.h"
#include "formats/security/JavaKeyStoreJks.h"
#include "formats/security/AuthenticodeCat.h"
#include "formats/security/WindowsRegistryHive.h"
#include "formats/security/WindowsEtlTrace.h"
#include "formats/security/GroupPolicyPol.h"

namespace DocReaders {

#if defined(_MSC_VER)
#define E2_DOCSEC_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_DOCSEC_NOINLINE __attribute__((noinline))
#else
#define E2_DOCSEC_NOINLINE
#endif

namespace {

E2_DOCSEC_NOINLINE bool DispatchBinAndPki(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".pcap") {
        return Formats::ParseLibPcapDoc(raw, out) || Formats::ParsePcapNgCapture(raw, out);
    }
    if (extLower == L".elf" ||
        (extLower == L".so" && raw.size() >= 16 && raw[0] == 0x7Fu && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F') ||
        (extLower.empty() && raw.size() >= 16 && raw[0] == 0x7Fu && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F'))
    {
        return Formats::ParseElfBinaryDoc(raw, out);
    }
    if (extLower == L".wasm") {
        return Formats::ParseWebAssemblyWasmDoc(raw, out);
    }
    if (extLower == L".exe" || extLower == L".dll" || extLower == L".sys" || extLower == L".ocx" ||
        extLower == L".scr" || extLower == L".efi" || extLower == L".drv" || extLower == L".cpl" ||
        (raw.size() >= 64 && raw[0] == 'M' && raw[1] == 'Z' &&
         Formats::DetailBinarySysDoc::RdU32LE(raw.data() + 60) + 24 <= raw.size() &&
         std::memcmp(raw.data() + Formats::DetailBinarySysDoc::RdU32LE(raw.data() + 60), "PE\0\0", 4) == 0))
    {
        return Formats::ParseWindowsPeBinary(raw, out);
    }
    if (extLower == L".o" && raw.size() >= 52 &&
        raw[0] == 0x7F && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F')
    {
        return Formats::ParseElfBinaryDoc(raw, out);
    }
    if (extLower == L".lnk" || (raw.size() >= 76 && Formats::DetailBinarySysDoc::RdU32LE(raw.data()) == 0x0000004Cu && raw[4] == 0x01 && raw[5] == 0x14)) {
        return Formats::ParseWindowsLnkShortcut(raw, out);
    }
    if (extLower == L".pf" || (raw.size() >= 84 && std::memcmp(raw.data() + 4, "SCCA", 4) == 0) ||
        (raw.size() >= 12 && raw[0] == 'M' && raw[1] == 'A' && raw[2] == 'M' && (raw[3] & 0x0Fu) == 0x04u))
    {
        return Formats::ParseWindowsPrefetch(raw, out);
    }
    if (extLower == L".evtx" || (raw.size() >= 128 && std::memcmp(raw.data(), "ElfFile\0", 8) == 0)) {
        return Formats::ParseWindowsEvtxLog(raw, out);
    }
    if (extLower == L".dmp" || extLower == L".mdmp" || (raw.size() >= 32 && std::memcmp(raw.data(), "MDMP", 4) == 0)) {
        return Formats::ParseWindowsMinidump(raw, out);
    }
    if (extLower == L".pdb" || (raw.size() >= 56 && std::memcmp(raw.data(), "Microsoft C/C++ MSF 7.00", 24) == 0)) {
        return Formats::ParseMsfPdbSymbols(raw, out);
    }
    if (extLower == L".macho" || extLower == L".dylib" || extLower == L".so") {
        return Formats::ParseAppleMachOBinary(raw, out);
    }
    if (extLower == L".class") {
        return Formats::ParseJavaClassFile(raw, out);
    }
    if (raw.size() >= 28 && Formats::IsValidMachOFatArchive(raw, out.sourceFileSize)) {
        return Formats::ParseAppleMachOBinary(raw, out);
    }
    if (raw.size() >= 24 && Formats::DetailBinarySysDoc::RdU32BE(raw.data()) == 0xCAFEBABEu) {
        return Formats::ParseJavaClassFile(raw, out);
    }
    if (raw.size() >= 28 && Formats::IsValidMachOMagic(Formats::DetailBinarySysDoc::RdU32LE(raw.data()))) {
        return Formats::ParseAppleMachOBinary(raw, out);
    }
    if (extLower == L".dex" || (raw.size() >= 112 && std::memcmp(raw.data(), "dex\n", 4) == 0)) {
        return Formats::ParseAndroidDexFile(raw, out);
    }
    if (extLower == L".crt" || extLower == L".cer" || extLower == L".pem" || extLower == L".der") {
        return Formats::ParseX509CertDoc(raw, extLower, out);
    }
    if (extLower == L".csr") return Formats::ParsePkcs10CsrDoc(raw, extLower, out);
    if (extLower == L".p7b" || extLower == L".p7c") return Formats::ParsePkcs7BundleDoc(raw, extLower, out);
    if (extLower == L".p12" || extLower == L".pfx") return Formats::ParsePkcs12KeystoreDoc(raw, extLower, out);
    if (extLower == L".kdbx" || (raw.size() >= 16 && Formats::DetailBinarySysDoc::RdU32LE(raw.data()) == 0x9AA2D903u && Formats::DetailBinarySysDoc::RdU32LE(raw.data() + 4) == 0xB54BFB67u)) {
        return Formats::ParseKeePassKdbx(raw, out);
    }
    if (extLower == L".pcapng" || (raw.size() >= 28 && Formats::DetailBinarySysDoc::RdU32LE(raw.data()) == 0x0A0D0D0Au)) {
        return Formats::ParsePcapNgCapture(raw, out) || Formats::ParseLibPcapDoc(raw, out);
    }
    if (extLower == L".uf2" || (raw.size() >= 512 && std::memcmp(raw.data(), "UF2\n", 4) == 0)) {
        return Formats::ParseUf2Firmware(raw, out);
    }
    return false;
}

E2_DOCSEC_NOINLINE bool DispatchSecOpsAndKvStores(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".accdb" || extLower == L".mdb") return Formats::ParseAccessDatabaseMdb(raw, extLower, out);
    if (extLower == L".rdb") return Formats::ParseRedisRdbSnapshot(raw, out);
    if (extLower == L".sst" || extLower == L".ldb") return Formats::ParseRocksLevelDbSst(raw, extLower, out);
    if (extLower == L".pmtiles") return Formats::ParsePmTilesMapDoc(raw, extLower, out);
    if (extLower == L".pbf") return Formats::ParseOsmPbfMapDoc(raw, extLower, out) || Formats::ParseProtobufWireDoc(raw, extLower, out);
    if (extLower == L".pb") return Formats::ParseProtobufWireDoc(raw, extLower, out);
    if (extLower == L".tfstate") return Formats::ParseTerraformStateDoc(raw, out);
    if (extLower == L".sarif") return Formats::ParseSarifLogDoc(raw, out);
    if (extLower == L".spdx") return Formats::ParseSpdxSbomDoc(raw, out);
    if (extLower == L".cdx") return Formats::ParseCycloneDxSbomDoc(raw, out);
    if (extLower == L".gpg" || extLower == L".pgp" || extLower == L".sig") return Formats::ParseOpenPgpPacketDoc(raw, extLower, out);
    if (extLower == L".jks") return Formats::ParseJavaKeyStoreJksDoc(raw, extLower, out);
    if (extLower == L".cat") return Formats::ParseAuthenticodeCatDoc(raw, extLower, out);
    if (extLower == L".hiv" || extLower == L".dat") {
        return Formats::ParseWindowsRegistryHiveDoc(raw, extLower, out) || Formats::ParseGroupPolicyPolDoc(raw, extLower, out);
    }
    if (extLower == L".etl") return Formats::ParseWindowsEtlTraceDoc(raw, extLower, out);
    if (extLower == L".pol") return Formats::ParseGroupPolicyPolDoc(raw, extLower, out);
    return false;
}

} // namespace

bool DispatchDocSecurityAndSys(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& outDoc) {
    return DispatchBinAndPki(raw, extLower, outDoc) || DispatchSecOpsAndKvStores(raw, extLower, outDoc);
}

} // namespace DocReaders
} // namespace Explorer2
