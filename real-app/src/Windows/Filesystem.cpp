#include "Filesystem.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

using namespace miniant::Windows::Filesystem;

std::wstring miniant::Windows::Filesystem::GetExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;) {
        const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }

        if (length < buffer.size()) {
            return std::wstring(buffer.data(), length);
        }

        if (buffer.size() >= 32768) {
            return {};
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::wstring miniant::Windows::Filesystem::GetExecutableDirectory() {
    const std::filesystem::path path(GetExecutablePath());
    return path.parent_path().wstring();
}

std::wstring miniant::Windows::Filesystem::GetTempDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;) {
        const DWORD length = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0) {
            return {};
        }

        if (length < buffer.size()) {
            return std::wstring(buffer.data(), length);
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::wstring miniant::Windows::Filesystem::JoinPath(const std::wstring& directory, const std::wstring& name) {
    if (directory.empty()) {
        return name;
    }

    std::wstring result = directory;
    if (result.back() != L'\\' && result.back() != L'/') {
        result.push_back(L'\\');
    }

    result += name;
    return result;
}

std::wstring miniant::Windows::Filesystem::GetFileName(const std::wstring& path) {
    return std::filesystem::path(path).filename().wstring();
}

std::wstring miniant::Windows::Filesystem::GetFileExtension(const std::wstring& path) {
    return std::filesystem::path(path).extension().wstring();
}

bool miniant::Windows::Filesystem::IsFile(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::path(path), error);
}

bool miniant::Windows::Filesystem::IsDirectory(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code error;
    return std::filesystem::is_directory(std::filesystem::path(path), error);
}

std::string miniant::Windows::Filesystem::ReadTextFileUtf8(const std::wstring& path, bool* success) {
    if (success != nullptr) {
        *success = false;
    }

    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) {
        return {};
    }

    std::ostringstream content;
    content << stream.rdbuf();

    if (success != nullptr) {
        *success = true;
    }

    return content.str();
}

bool miniant::Windows::Filesystem::WriteTextFileUtf8(const std::wstring& path, const std::string& content) {
    std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }

    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream.good();
}
