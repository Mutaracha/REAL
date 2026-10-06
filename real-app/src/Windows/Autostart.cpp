#include "Autostart.h"

#include <Windows.h>

namespace {

const wchar_t RUN_KEY_PATH[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t RUN_VALUE_NAME[] = L"REAL";

}

std::wstring miniant::Windows::Autostart::Read() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }

    // The value is not guaranteed to end with a zero: the last character of
    // the buffer is never given to the registry, so it always does.
    wchar_t buffer[1025] = {};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG result = ::RegQueryValueExW(
        key, RUN_VALUE_NAME, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    ::RegCloseKey(key);

    if (result != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }

    return buffer;
}

unsigned long miniant::Windows::Autostart::Write(const std::wstring& command) {
    HKEY key = nullptr;
    LONG result = ::RegCreateKeyExW(
        HKEY_CURRENT_USER, RUN_KEY_PATH, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        return static_cast<unsigned long>(result);
    }

    result = ::RegSetValueExW(
        key,
        RUN_VALUE_NAME,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);

    return static_cast<unsigned long>(result);
}

unsigned long miniant::Windows::Autostart::Remove() {
    HKEY key = nullptr;
    LONG result = ::RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_SET_VALUE, &key);
    if (result != ERROR_SUCCESS) {
        // Without the key there is no entry either.
        return result == ERROR_FILE_NOT_FOUND ? 0 : static_cast<unsigned long>(result);
    }

    result = ::RegDeleteValueW(key, RUN_VALUE_NAME);
    ::RegCloseKey(key);

    return result == ERROR_FILE_NOT_FOUND ? 0 : static_cast<unsigned long>(result);
}
