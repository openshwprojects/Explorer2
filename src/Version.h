#ifndef E2_VERSION_H
#define E2_VERSION_H

#ifdef E2_HAS_GENERATED_VERSION
#include "version_gen.h"
#endif

#ifndef E2_VERSION_MAJOR
#define E2_VERSION_MAJOR 1
#endif
#ifndef E2_VERSION_MINOR
#define E2_VERSION_MINOR 0
#endif
#ifndef E2_VERSION_PATCH
#define E2_VERSION_PATCH 0
#endif
#ifndef E2_VERSION_BUILD
#define E2_VERSION_BUILD 0
#endif

#ifndef E2_VERSION_SEMVER_STR
#define E2_VERSION_SEMVER_STR "1.0.0"
#endif
#ifndef E2_VERSION_SEMVER_WSTR
#define E2_VERSION_SEMVER_WSTR L"1.0.0"
#endif

#ifndef E2_VERSION_FILE_STR
#define E2_VERSION_FILE_STR "1.0.0.0"
#endif
#ifndef E2_VERSION_FILE_WSTR
#define E2_VERSION_FILE_WSTR L"1.0.0.0"
#endif

#ifndef E2_VERSION_FULL_STR
#define E2_VERSION_FULL_STR "1.0.0"
#endif
#ifndef E2_VERSION_FULL_WSTR
#define E2_VERSION_FULL_WSTR L"1.0.0"
#endif

#ifndef E2_GIT_SHA_STR
#define E2_GIT_SHA_STR "dev"
#endif
#ifndef E2_GIT_SHA_WSTR
#define E2_GIT_SHA_WSTR L"dev"
#endif

#endif // E2_VERSION_H
