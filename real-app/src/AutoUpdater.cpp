#include "AutoUpdater.h"

#include "AppVersion.h"
#include "Lang.h"
#include "Http/HttpClient.h"
#include "Text.h"
#include "Windows/Filesystem.h"
#include "Windows/WindowsError.h"

#include <spdlog/fmt/fmt.h>

#include <Windows.h>

using namespace miniant::AutoUpdater;

namespace {

// One request with a generous timeout: the user has just started the
// application and is not waiting for it.
constexpr int REQUEST_TIMEOUT_SECONDS = 15;

const char RELEASE_TAG_PATH[] = "/releases/tag/";

}

AutoUpdater::AutoUpdater(std::string repository):
    m_repository(std::move(repository)) {}

// The latest release is asked from the web site, not from the API: the page
// "releases/latest" of a project redirects to the page of its latest release,
// and the address of that page ends with the tag. A project without releases
// redirects to the list of releases instead. The web site has no limit of
// requests per hour the way the API has (60 for a request without an account,
// counted per address, so users behind one address share them).
tl::expected<UpdateInfo, std::string> AutoUpdater::GetLatestRelease(Http::Cancellation* cancellation) const {
    if (m_repository.empty()) {
        return tl::make_unexpected(std::string(Lang::Utf8(Lang::Str::ErrNoUpdateRepository)));
    }

    const std::wstring url = L"https://github.com/" + Text::ToWide(m_repository) + L"/releases/latest";

    std::vector<std::pair<std::wstring, std::wstring>> headers;
    headers.emplace_back(L"User-Agent", L"REAL-updater/" + Text::ToWide(AppInfo::VERSION.ToString()));
    headers.emplace_back(L"Cache-Control", L"no-cache");

    const Http::Response response = Http::Get(url, headers, REQUEST_TIMEOUT_SECONDS, false, cancellation);
    if (!response.networkOk) {
        return tl::make_unexpected(fmt::format(Lang::Utf8(Lang::Str::ErrGithubUnreachable), response.error));
    }

    if (response.statusCode == 403 || response.statusCode == 429) {
        return tl::make_unexpected(fmt::format(Lang::Utf8(Lang::Str::ErrGithubRefused), response.statusCode));
    }

    if (response.statusCode < 300 || response.statusCode >= 400) {
        if (response.statusCode == 200) {
            return tl::make_unexpected(std::string(Lang::Utf8(Lang::Str::ErrGithubUnexpected)));
        }

        return tl::make_unexpected(fmt::format(Lang::Utf8(Lang::Str::ErrHttpStatus), response.statusCode));
    }

    if (response.location.empty()) {
        return tl::make_unexpected(std::string(Lang::Utf8(Lang::Str::ErrGithubUnexpected)));
    }

    std::string location = response.location;
    if (location.front() == '/') {
        location = "https://github.com" + location;
    }

    UpdateInfo info;

    const size_t tagPosition = location.find(RELEASE_TAG_PATH);
    if (tagPosition == std::string::npos) {
        info.published = false;
        return info;
    }

    info.releaseUrl = location;
    info.tag = location.substr(tagPosition + sizeof(RELEASE_TAG_PATH) - 1);

    const size_t tagEnd = info.tag.find_first_of("?#");
    if (tagEnd != std::string::npos) {
        info.tag.resize(tagEnd);
    }

    if (auto version = Version::Find(info.tag)) {
        info.version = *version;
    }

    if (info.version == Version()) {
        return tl::make_unexpected(std::string(Lang::Utf8(Lang::Str::ErrNoReleaseVersion)));
    }

    return info;
}

bool AutoUpdater::CleanupPreviousInstall(std::string* message) {
    const std::wstring executable = Windows::Filesystem::GetExecutablePath();
    if (executable.empty()) {
        return false;
    }

    const std::wstring leftover = executable + L"~DELETE";
    if (!Windows::Filesystem::IsFile(leftover)) {
        return false;
    }

    if (::DeleteFileW(leftover.c_str()) != FALSE) {
        return true;
    }

    const DWORD error = ::GetLastError();
    if (message != nullptr) {
        *message = fmt::format(Lang::Utf8(Lang::Str::ErrDeleteLeftover), Windows::SystemMessage(error));
    }

    return false;
}
