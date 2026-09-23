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

// Update checks are never forced: the application only looks for a newer
// release when the user asks for it (tray menu, --check-updates) and the
// settings allow it. A newer release is reported, never installed silently.
class AutoUpdater {
public:
    AutoUpdater(std::string repository, int timeoutSeconds);

    tl::expected<UpdateInfo, std::string> GetLatestRelease() const;

    // Removes the "<exe>~DELETE" file left behind by the self-updater of v0.2.0.
    static bool CleanupPreviousInstall(std::string* message);

private:
    std::string m_repository;
    int m_timeoutSeconds;
};

}
