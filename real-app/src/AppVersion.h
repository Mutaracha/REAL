#pragma once

#include "Version.h"

namespace miniant::AppInfo {

inline constexpr AutoUpdater::Version VERSION(0, 3, 0);

inline constexpr const wchar_t* NAME = L"REAL";
inline constexpr const wchar_t* DESCRIPTION = L"REduce Audio Latency";
// The releases of the update check are read from this repository; it is part of
// the build instead of a setting, so a settings file cannot point the updater
// somewhere else.
inline constexpr const char* GITHUB_REPOSITORY = "Mutaracha/REAL";
inline constexpr const char* PROJECT_URL = "https://github.com/Mutaracha/REAL";
inline constexpr const char* UPSTREAM_URL = "https://github.com/miniant-git/REAL";

}
