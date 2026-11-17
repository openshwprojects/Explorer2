#pragma once
// FormatSupport.h — shared building blocks of the header-only readers in src/formats/.
//
// Include this at global scope near the top of every .cpp that includes formats/*.h. Several of those
// .cpp files include format headers from inside `namespace Explorer2 { ... }`; a format header's own
// #include of these files would then be nested in that namespace. Including them here first makes the
// nested includes no-ops (#pragma once), so `::Explorer2::ByteIO` and friends always name the real thing.
#include "Base64.h"
#include "ByteIO.h"
#include "Crc32.h"
#include "JsonDom.h"
#include "Lz4Block.h"
#include "LzxDecoder.h"
#include "StreamCodecs.h"
#include "XmlTagScan.h"
#include "ZipDeflate.h"
