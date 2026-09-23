#include "AutoUpdater.h"

#include "AppVersion.h"
#include "Http/HttpClient.h"
#include "Text.h"
#include "Windows/Filesystem.h"

#include <nlohmann/json.hpp>

#include <Windows.h>

#include <filesystem>

using json = nlohmann::json;
using namespace miniant::AutoUpdater;

namespace {

constexpr size_t MAX_RELEASE_NOTES_LENGTH = 500;

}

AutoUpdater::AutoUpdater(std::string repository, int timeoutSeconds):
    m_repository(std::move(repository)),
    m_timeoutSeconds(timeoutSeconds) {}

tl::expected<UpdateInfo, std::string> AutoUpdater::GetLatestRelease() const {
    if (m_repository.empty()) {
        return tl::make_unexpected(std::string("No repository is configured for update checks (updates.repository)."));
    }

    const std::wstring url = L"https://api.github.com/repos/" + Text::ToWide(m_repository) + L"/releases/latest";

    std::vector<std::pair<std::wstring, std::wstring>> headers;
    headers.emplace_back(L"User-Agent", L"REAL-updater/" + Text::ToWide(AppInfo::VERSION.ToString()));
    headers.emplace_back(L"Accept", L"application/vnd.github+json");
    headers.emplace_back(L"Cache-Control", L"no-cache");

    const Http::Response response = Http::Get(url, headers, m_timeoutSeconds);
    if (!response.networkOk) {
        return tl::make_unexpected(std::string("Could not reach GitHub: ") + response.error);
    }

    if (response.statusCode == 404) {
        return tl::make_unexpected(std::string("The repository '") + m_repository + "' has no published releases.");
    }

    if (response.statusCode == 403 || response.statusCode == 429) {
        return tl::make_unexpected(std::string("GitHub refused the request (HTTP ") + std::to_string(response.statusCode) + "), probably the API rate limit.");
    }

    if (response.statusCode != 200) {
        return tl::make_unexpected(std::string("GitHub returned HTTP ") + std::to_string(response.statusCode) + ".");
    }

    json release;
    try {
        release = json::parse(response.body);
    } catch (const json::exception& error) {
        return tl::make_unexpected(std::string("Could not parse the GitHub response: ") + error.what());
    }

    if (!release.is_object()) {
        return tl::make_unexpected(std::string("Unexpected GitHub response."));
    }

    UpdateInfo info;

    const auto urlIt = release.find("html_url");
    if (urlIt != release.end() && urlIt->is_string()) {
        info.releaseUrl = urlIt->get<std::string>();
    }

    const auto tagIt = release.find("tag_name");
    if (tagIt != release.end() && tagIt->is_string()) {
        info.tag = tagIt->get<std::string>();
    }

    const auto nameIt = release.find("name");
    if (nameIt != release.end() && nameIt->is_string()) {
        const std::string name = nameIt->get<std::string>();
        if (auto version = Version::Find(name)) {
            info.version = *version;
        }
    }

    if (info.version == Version() && !info.tag.empty()) {
        if (auto version = Version::Find(info.tag)) {
            info.version = *version;
        }
    }

    if (info.version == Version()) {
        return tl::make_unexpected(std::string("Could not detect the version of the latest release."));
    }

    const auto bodyIt = release.find("body");
    if (bodyIt != release.end() && bodyIt->is_string()) {
        std::string notes = bodyIt->get<std::string>();
        if (notes.size() > MAX_RELEASE_NOTES_LENGTH) {
            notes.resize(MAX_RELEASE_NOTES_LENGTH);
            notes += "...";
        }

        info.releaseNotes = notes;
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

    std::error_code error;
    const bool removed = std::filesystem::remove(std::filesystem::path(leftover), error);
    if (!removed && message != nullptr) {
        *message = std::string("Could not delete the leftover file from a previous update: ") + error.message();
    }

    return removed;
}
