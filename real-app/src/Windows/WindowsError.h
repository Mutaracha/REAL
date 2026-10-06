#pragma once

#include "../ExpectedError.h"

#include <string>

namespace miniant::Windows {

// Formats a HRESULT as "HRESULT 0x88890028 (AUDCLNT_E_ENGINE_PERIODICITY_LOCKED): <system text>".
std::string DescribeHResult(long hr);

// Formats the last Win32 error the same way.
std::string DescribeLastError();

// Formats a Win32 error code that a function returned instead of setting the
// last error (the registry functions do so).
std::string DescribeError(unsigned long code);

// Only the text Windows has for an error ("Access is denied."), for a message
// the user reads; the code as above when Windows has no text for it.
std::string SystemMessage(unsigned long code);

class WindowsError : public ExpectedError {
public:
    WindowsError();

    WindowsError(std::string message) noexcept:
        ExpectedError(std::move(message)) {}

    WindowsError(const char* message):
        ExpectedError(message) {}

    explicit WindowsError(long hr):
        ExpectedError(DescribeHResult(hr)) {}
};

}
