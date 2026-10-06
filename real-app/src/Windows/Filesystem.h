#pragma once

#include <cstddef>
#include <string>

namespace miniant::Windows::Filesystem {

std::wstring GetExecutablePath();
std::wstring GetExecutableDirectory();
std::wstring GetTempDirectory();

std::wstring JoinPath(const std::wstring& directory, const std::wstring& name);
// A full path: from the root of a drive ("C:\...") or of a network share
// ("\\server\share\..."). Any other path is read from some folder.
bool IsAbsolutePath(const std::wstring& path);
// The folder of a file as a full path: a relative path is resolved the way the
// file itself is opened, against the current folder. A file in the root of a
// drive gives the root ("G:\"), not the bare drive.
std::wstring GetDirectory(const std::wstring& path);

bool IsFile(const std::wstring& path);

// The size of a file and whether it has the "Read-only" attribute. False when
// there is no such file.
bool GetFileInfo(const std::wstring& path, unsigned long long* size, bool* readOnly);

// Gives a file another name; a file that already has that name is replaced.
// False when the file keeps its name, with the error code of Windows.
bool RenameFile(const std::wstring& from, const std::wstring& to, unsigned long* error = nullptr);

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
