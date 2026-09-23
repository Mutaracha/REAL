#pragma once

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

std::string ReadTextFileUtf8(const std::wstring& path, bool* success = nullptr);
bool WriteTextFileUtf8(const std::wstring& path, const std::string& content);

}
