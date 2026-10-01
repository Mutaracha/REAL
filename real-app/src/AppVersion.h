#pragma once

#include "Version.h"

#include <string>

// The identification of the build. The CI passes the number of the run and the
// commit it builds (see .github/workflows/build.yml, CMakeLists.txt and
// build.bat); a build without them shows the version alone.
#ifndef REAL_BUILD_NUMBER
#define REAL_BUILD_NUMBER 0
#endif

#ifndef REAL_COMMIT
#define REAL_COMMIT
#endif

#define REAL_STRINGIFY_DETAIL(value) #value
#define REAL_STRINGIFY(value) REAL_STRINGIFY_DETAIL(value)

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

// "v0.3.0 RC 39 (47d5109)": the version for the user, with the number of the CI
// run and the commit the executable was built from. A local build of a checkout
// shows the commit only, a build outside a repository - just the version.
inline std::string DisplayVersion() {
    std::string text = VERSION.ToString();

    const std::string build = REAL_STRINGIFY(REAL_BUILD_NUMBER);
    if (!build.empty() && build != "0") {
        text += " RC " + build;
    }

    const std::string commit = REAL_STRINGIFY(REAL_COMMIT);
    if (!commit.empty()) {
        text += " (" + commit.substr(0, 7) + ")";
    }

    return text;
}

}

#undef REAL_STRINGIFY_DETAIL
#undef REAL_STRINGIFY
