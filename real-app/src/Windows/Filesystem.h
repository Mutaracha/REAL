#pragma once

#include <cstddef>
#include <string>

namespace miniant::Windows::Filesystem {

std::wstring GetExecutablePath();
std::wstring GetExecutableDirectory();
std::wstring GetTempDirectory();

std::wstring JoinPath(const std::wstring& directory, const std::wstring& name);
std::wstring GetFileName(const std::wstring& path);
std::wstring GetFileExtension(const std::wstring& path);

bool IsFile(const std::wstring& path);
bool IsDirectory(const std::wstring& path);

// The whole file. With a limit, a larger file is not read at all: the result
// is empty, success is false and tooLarge says why.
std::string ReadTextFileUtf8(
    const std::wstring& path, bool* success = nullptr, size_t maxBytes = 0, bool* tooLarge = nullptr);
bool WriteTextFileUtf8(const std::wstring& path, const std::string& content);

// The same write, but through a temporary file next to the target: the file
// either keeps its previous content or gets the new one whole, an interrupted
// write cannot leave a half-written file behind.
bool WriteTextFileUtf8Atomic(const std::wstring& path, const std::string& content);

}
