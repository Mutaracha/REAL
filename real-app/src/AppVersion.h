#pragma once

#include "Version.h"
#include "VersionNumber.h"

#include <string>

// The identification of the build. The CI passes the number of the run and the
// commit it builds (see .github/workflows/build.yml, CMakeLists.txt and
// build.bat); a build without them shows the version alone. A release build
// (REAL_RELEASE) shows the version alone as well.
#ifndef REAL_BUILD_NUMBER
#define REAL_BUILD_NUMBER 0
#endif

#ifndef REAL_COMMIT
#define REAL_COMMIT
#endif

#define REAL_STRINGIFY_DETAIL(value) #value
#define REAL_STRINGIFY(value) REAL_STRINGIFY_DETAIL(value)

namespace miniant::AppInfo {

inline constexpr AutoUpdater::Version VERSION(REAL_VERSION_MAJOR, REAL_VERSION_MINOR, REAL_VERSION_PATCH);

inline constexpr const wchar_t* NAME = L"REAL";
inline constexpr const wchar_t* DESCRIPTION = L"REduce Audio Latency";
// The releases of the update check are read from this repository; it is part of
// the build instead of a setting, so a settings file cannot point the updater
// somewhere else.
inline constexpr const char* GITHUB_REPOSITORY = "Mutaracha/REAL";
inline constexpr const char* PROJECT_URL = "https://github.com/Mutaracha/REAL";
inline constexpr const char* UPSTREAM_URL = "https://github.com/miniant-git/REAL";
// The documentation in the master branch of the repository: the About window and
// the header of the settings file append the path of a file in the language of
// the interface ("docs/usage.en.md"). The master branch, not the tag of the
// version: a test build has no tag of its own.
inline constexpr const char* DOCS_URL = "https://github.com/Mutaracha/REAL/blob/master/";

// The version for the user. A release: "v1.0.0". Any other build of the CI:
// "v1.0.0 RC <run> (<commit>)", with the number of the CI run and the commit the
// executable was built from. A local build of a checkout shows the commit only,
// a build outside a repository - just the version.
inline std::string DisplayVersion() {
    std::string text = VERSION.ToString();

#ifndef REAL_RELEASE
    const std::string build = REAL_STRINGIFY(REAL_BUILD_NUMBER);
    if (!build.empty() && build != "0") {
        text += " RC " + build;
    }

    const std::string commit = REAL_STRINGIFY(REAL_COMMIT);
    if (!commit.empty()) {
        text += " (" + commit.substr(0, 7) + ")";
    }
#endif

    return text;
}

}

#undef REAL_STRINGIFY_DETAIL
#undef REAL_STRINGIFY
