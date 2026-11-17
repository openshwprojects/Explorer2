#pragma once
// The names Explorer2 uses for its virtual folders in place of a path. No Windows headers: the archive readers use
// this too, and they also build on Linux.
#include <string>

namespace Explorer2 {

static inline bool IsVirtualFolder(const std::wstring& p) {
    return p == L"THIS_PC" || p == L"RECYCLE_BIN";
}

} // namespace Explorer2
