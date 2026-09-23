#include "WindowsError.h"

#include "../Text.h"

#include <Windows.h>

#include <sstream>

using namespace miniant;
using namespace miniant::Windows;

namespace {

struct HResultName {
    long code;
    const char* name;
};

// Only the codes that the audio engine is known to return for the low latency
// shared mode path are listed; everything else is reported as hex + system text.
const HResultName KNOWN_HRESULTS[] = {
    { static_cast<long>(0x88890001), "AUDCLNT_E_NOT_INITIALIZED" },
    { static_cast<long>(0x88890002), "AUDCLNT_E_ALREADY_INITIALIZED" },
    { static_cast<long>(0x88890004), "AUDCLNT_E_DEVICE_INVALIDATED" },
    { static_cast<long>(0x88890005), "AUDCLNT_E_NOT_STOPPED" },
    { static_cast<long>(0x88890008), "AUDCLNT_E_UNSUPPORTED_FORMAT" },
    { static_cast<long>(0x8889000A), "AUDCLNT_E_DEVICE_IN_USE" },
    { static_cast<long>(0x8889000C), "AUDCLNT_E_THREAD_NOT_REGISTERED" },
    { static_cast<long>(0x8889000F), "AUDCLNT_E_ENDPOINT_CREATE_FAILED" },
    { static_cast<long>(0x88890010), "AUDCLNT_E_SERVICE_NOT_RUNNING" },
    { static_cast<long>(0x88890017), "AUDCLNT_E_CPUUSAGE_EXCEEDED" },
    { static_cast<long>(0x88890019), "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED" },
    { static_cast<long>(0x88890020), "AUDCLNT_E_INVALID_DEVICE_PERIOD" },
    { static_cast<long>(0x88890021), "AUDCLNT_E_INVALID_STREAM_FLAG" },
    { static_cast<long>(0x88890024), "AUDCLNT_E_OFFLOAD_MODE_ONLY" },
    { static_cast<long>(0x88890025), "AUDCLNT_E_NONOFFLOAD_MODE_ONLY" },
    { static_cast<long>(0x88890026), "AUDCLNT_E_RESOURCES_INVALIDATED" },
    { static_cast<long>(0x88890027), "AUDCLNT_E_RAW_MODE_UNSUPPORTED" },
    { static_cast<long>(0x88890028), "AUDCLNT_E_ENGINE_PERIODICITY_LOCKED" },
    { static_cast<long>(0x88890029), "AUDCLNT_E_ENGINE_FORMAT_LOCKED" },
    { static_cast<long>(0x80070005), "E_ACCESSDENIED" },
    { static_cast<long>(0x80004005), "E_FAIL" },
    { static_cast<long>(0x8007000E), "E_OUTOFMEMORY" },
    { static_cast<long>(0x80070057), "E_INVALIDARG" },
    { static_cast<long>(0x80004002), "E_NOINTERFACE" },
    { static_cast<long>(0x800401F0), "CO_E_NOTINITIALIZED" },
};

std::string ToHex(long value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << static_cast<unsigned long>(value);
    return stream.str();
}

std::string FormatSystemMessage(long code) {
    LPWSTR buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    if (length == 0 || buffer == nullptr) {
        return {};
    }

    std::wstring message(buffer, length);
    ::LocalFree(buffer);

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    return Text::ToUtf8(message);
}

std::string Describe(long code, const char* fallbackName) {
    std::string result = "HRESULT " + ToHex(code);

    const char* name = fallbackName;
    for (const auto& known : KNOWN_HRESULTS) {
        if (known.code == code) {
            name = known.name;
            break;
        }
    }

    if (name != nullptr) {
        result += " (";
        result += name;
        result += ")";
    }

    const std::string systemMessage = FormatSystemMessage(code);
    if (!systemMessage.empty()) {
        result += ": ";
        result += systemMessage;
    }

    return result;
}

}

std::string miniant::Windows::DescribeHResult(long hr) {
    return Describe(hr, nullptr);
}

std::string miniant::Windows::DescribeLastError() {
    return Describe(static_cast<long>(::GetLastError()), nullptr);
}

WindowsError::WindowsError():
    ExpectedError(DescribeLastError()) {}
