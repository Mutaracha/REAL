#include "Filesystem.h"

#include <Windows.h>

#include <algorithm>
#include <vector>

using namespace miniant::Windows::Filesystem;

namespace {

// The files are read and written in pieces of this size.
constexpr size_t CHUNK_BYTES = 64 * 1024;

bool IsSeparator(wchar_t character) {
    return character == L'\\' || character == L'/';
}

// The folder of a path: everything before its last separator. The separator
// stays after a drive, so a file in the root gives the root ("G:\").
std::wstring ParentOf(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return {};
    }

    if (separator == 2 && path[1] == L':') {
        return path.substr(0, 3);
    }

    return path.substr(0, separator);
}

// A file handle that is closed with the object.
class FileHandle {
public:
    explicit FileHandle(HANDLE handle):
        m_handle(handle) {}

    ~FileHandle() {
        if (IsOpen()) {
            ::CloseHandle(m_handle);
        }
    }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    bool IsOpen() const {
        return m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr;
    }

    HANDLE Get() const {
        return m_handle;
    }

private:
    HANDLE m_handle;
};

}

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
    return ParentOf(GetExecutablePath());
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

bool miniant::Windows::Filesystem::IsAbsolutePath(const std::wstring& path) {
    // A drive with the root folder after it; "C:file" is relative to the
    // current folder of the drive.
    if (path.size() >= 3 && path[1] == L':' && IsSeparator(path[2])
        && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'))) {
        return true;
    }

    // A network share ("\\server\share") or a path of a device ("\\?\C:\").
    return path.size() >= 3 && IsSeparator(path[0]) && IsSeparator(path[1]) && !IsSeparator(path[2]);
}

std::wstring miniant::Windows::Filesystem::GetDirectory(const std::wstring& path) {
    if (path.empty()) {
        return {};
    }

    // Windows makes the path full and puts its own separators into it.
    std::wstring full = path;
    const DWORD needed = ::GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);

    if (needed > 0) {
        std::wstring buffer(needed, L'\0');
        const DWORD length = ::GetFullPathNameW(path.c_str(), needed, buffer.data(), nullptr);

        if (length > 0 && length < needed) {
            buffer.resize(length);
            full = buffer;
        }
    }

    std::replace(full.begin(), full.end(), L'/', L'\\');
    return ParentOf(full);
}

bool miniant::Windows::Filesystem::IsFile(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }

    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool miniant::Windows::Filesystem::GetFileInfo(const std::wstring& path, unsigned long long* size, bool* readOnly) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};

    if (path.empty() || ::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) == FALSE
        || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }

    if (size != nullptr) {
        *size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }

    if (readOnly != nullptr) {
        *readOnly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    }

    return true;
}

bool miniant::Windows::Filesystem::RenameFile(const std::wstring& from, const std::wstring& to, unsigned long* error) {
    if (::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE) {
        return true;
    }

    if (error != nullptr) {
        *error = ::GetLastError();
    }

    return false;
}

std::string miniant::Windows::Filesystem::ReadTextFileUtf8(
    const std::wstring& path, bool* success, size_t maxBytes, bool* tooLarge) {
    if (success != nullptr) {
        *success = false;
    }

    if (tooLarge != nullptr) {
        *tooLarge = false;
    }

    // Other programs may read and write the file meanwhile (an editor that has
    // it open).
    const FileHandle file(::CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsOpen()) {
        return {};
    }

    // The size is known before anything is read: a file far beyond the limit
    // never gets into the memory.
    LARGE_INTEGER size = {};
    if (::GetFileSizeEx(file.Get(), &size) == FALSE || size.QuadPart < 0) {
        return {};
    }

    const unsigned long long bytes = static_cast<unsigned long long>(size.QuadPart);
    if (maxBytes > 0 && bytes > maxBytes) {
        if (tooLarge != nullptr) {
            *tooLarge = true;
        }

        return {};
    }

    std::string content;
    content.reserve(static_cast<size_t>(std::min<unsigned long long>(bytes, 16 * 1024 * 1024)));

    std::vector<char> chunk(CHUNK_BYTES);
    for (;;) {
        DWORD read = 0;
        if (::ReadFile(file.Get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) == FALSE) {
            return {};
        }

        if (read == 0) {
            break;
        }

        content.append(chunk.data(), read);

        // A file that grows while it is read is held to the limit as well.
        if (maxBytes > 0 && content.size() > maxBytes) {
            if (tooLarge != nullptr) {
                *tooLarge = true;
            }

            return {};
        }
    }

    if (success != nullptr) {
        *success = true;
    }

    return content;
}

bool miniant::Windows::Filesystem::WriteTextFileUtf8(const std::wstring& path, const std::string& content) {
    const FileHandle file(::CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsOpen()) {
        return false;
    }

    size_t written = 0;
    while (written < content.size()) {
        const DWORD piece = static_cast<DWORD>(std::min(content.size() - written, CHUNK_BYTES));
        DWORD done = 0;

        if (::WriteFile(file.Get(), content.data() + written, piece, &done, nullptr) == FALSE || done == 0) {
            return false;
        }

        written += done;
    }

    return true;
}

bool miniant::Windows::Filesystem::WriteTextFileUtf8Atomic(const std::wstring& path, const std::string& content) {
    const std::wstring temporary = path + L".tmp";

    if (!WriteTextFileUtf8(temporary, content)) {
        return false;
    }

    if (::MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        ::DeleteFileW(temporary.c_str());
        return false;
    }

    return true;
}
