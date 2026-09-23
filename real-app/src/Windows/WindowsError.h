#pragma once

#include "../ExpectedError.h"

#include <string>

namespace miniant::Windows {

// Formats a HRESULT as "HRESULT 0x88890028 (AUDCLNT_E_ENGINE_PERIODICITY_LOCKED): <system text>".
std::string DescribeHResult(long hr);

// Formats the last Win32 error the same way.
std::string DescribeLastError();

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
