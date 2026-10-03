#pragma once

#include <string>

namespace miniant::Windows::Autostart {

// The entry of the program in the autostart of the current user: the value
// "REAL" of HKCU\Software\Microsoft\Windows\CurrentVersion\Run. Write() and
// Remove() return 0 or the error code of the registry.

// The command line of the entry; empty when there is none.
std::wstring Read();

// Creates the key when needed and writes the command line.
unsigned long Write(const std::wstring& command);

// Deletes the entry; an entry that is not there is no error.
unsigned long Remove();

}
