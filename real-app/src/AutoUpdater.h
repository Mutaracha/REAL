#pragma once

#include "Version.h"

#include <tl/expected.hpp>

#include <string>

namespace miniant::AutoUpdater {

struct UpdateInfo {
    Version version;
    std::string tag;
    std::string releaseUrl;
    std::string releaseNotes;
};

// Update checks are never forced and never happen while the application is
// running: at most one check is made at startup, and only when the settings ask
// for it (updates.mode = "manual" and updates.checkOnStartup = true). A newer
// release is reported to the user, never downloaded or installed silently.
class AutoUpdater {
public:
    explicit AutoUpdater(std::string repository);

    tl::expected<UpdateInfo, std::string> GetLatestRelease() const;

    // Removes the "<exe>~DELETE" file left behind by the self-updater of v0.2.0.
    static bool CleanupPreviousInstall(std::string* message);

private:
    std::string m_repository;
};

}
