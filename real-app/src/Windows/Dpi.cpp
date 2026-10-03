#include "Dpi.h"

#include <cwchar>

using namespace miniant::Windows::Dpi;

namespace {

// The DPI functions are looked up at runtime: they exist since Windows 10 1607,
// and a program that links them directly would not even start on an older
// system. The header of the build may predate them as well.
using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
using GetDpiForSystemFunction = UINT(WINAPI*)();
using AdjustWindowRectExForDpiFunction = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);

template <typename Function>
Function FromUser32(const char* name) {
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return nullptr;
    }

    return reinterpret_cast<Function>(::GetProcAddress(user32, name));
}

// The message font of the system: the face and the size the user picked in the
// display settings. The size is in the pixels of the system DPI.
LOGFONTW MessageFont() {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);

    LOGFONTW font = {};
    if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE) {
        font = metrics.lfMessageFont;
    }

    if (font.lfFaceName[0] == L'\0') {
        // The system call failed: the face and the size the program used before.
        ::wcscpy_s(font.lfFaceName, L"Segoe UI");
        font.lfHeight = -::MulDiv(12, static_cast<int>(ForSystem()), 96);
        font.lfWeight = FW_NORMAL;
    }

    font.lfQuality = CLEARTYPE_QUALITY;
    return font;
}

// A font built on the message font: the same face and weight (unless asked
// otherwise), the size of the given DPI (and, when asked, a percentage of it).
HFONT CreateFontFromMessage(const wchar_t* face, int heightPercent, int weight, bool underline, UINT dpi) {
    LOGFONTW font = MessageFont();

    const UINT systemDpi = ForSystem();
    font.lfHeight = ::MulDiv(font.lfHeight, static_cast<int>(dpi), static_cast<int>(systemDpi));

    if (heightPercent != 100) {
        font.lfHeight = ::MulDiv(font.lfHeight, heightPercent, 100);
    }

    if (weight != 0) {
        font.lfWeight = weight;
    }

    if (face != nullptr) {
        ::wcscpy_s(font.lfFaceName, face);
    }

    font.lfUnderline = underline ? TRUE : FALSE;
    return ::CreateFontIndirectW(&font);
}

}

UINT miniant::Windows::Dpi::ForWindow(HWND window) {
    static const GetDpiForWindowFunction getDpiForWindow = FromUser32<GetDpiForWindowFunction>("GetDpiForWindow");

    if (getDpiForWindow != nullptr && window != nullptr) {
        const UINT dpi = getDpiForWindow(window);
        if (dpi != 0) {
            return dpi;
        }
    }

    return ForSystem();
}

UINT miniant::Windows::Dpi::ForSystem() {
    static const GetDpiForSystemFunction getDpiForSystem = FromUser32<GetDpiForSystemFunction>("GetDpiForSystem");

    if (getDpiForSystem != nullptr) {
        const UINT dpi = getDpiForSystem();
        if (dpi != 0) {
            return dpi;
        }
    }

    const HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) {
        return 96;
    }

    const int dpi = ::GetDeviceCaps(screen, LOGPIXELSX);
    ::ReleaseDC(nullptr, screen);

    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

int miniant::Windows::Dpi::Scale(int value, UINT dpi) {
    return ::MulDiv(value, static_cast<int>(dpi), 96);
}

void miniant::Windows::Dpi::AdjustWindowRect(RECT& rect, DWORD style, UINT dpi, bool hasMenu) {
    static const AdjustWindowRectExForDpiFunction adjust =
        FromUser32<AdjustWindowRectExForDpiFunction>("AdjustWindowRectExForDpi");

    if (adjust != nullptr) {
        adjust(&rect, style, hasMenu ? TRUE : FALSE, 0, dpi);
        return;
    }

    // An older system: the frame is computed for the DPI of the desktop and
    // scaled afterwards.
    const UINT systemDpi = ForSystem();

    RECT frame = { 0, 0, rect.right - rect.left, rect.bottom - rect.top };
    ::AdjustWindowRectEx(&frame, style, hasMenu ? TRUE : FALSE, 0);

    rect.left = ::MulDiv(frame.left, static_cast<int>(dpi), static_cast<int>(systemDpi));
    rect.top = ::MulDiv(frame.top, static_cast<int>(dpi), static_cast<int>(systemDpi));
    rect.right = ::MulDiv(frame.right, static_cast<int>(dpi), static_cast<int>(systemDpi));
    rect.bottom = ::MulDiv(frame.bottom, static_cast<int>(dpi), static_cast<int>(systemDpi));
}

HFONT miniant::Windows::Dpi::CreateUiFont(UINT dpi) {
    return CreateFontFromMessage(nullptr, 100, 0, false, dpi);
}

HFONT miniant::Windows::Dpi::CreateHeaderFont(UINT dpi) {
    return CreateFontFromMessage(nullptr, 108, FW_SEMIBOLD, false, dpi);
}

HFONT miniant::Windows::Dpi::CreateMonoFont(UINT dpi) {
    return CreateFontFromMessage(L"Consolas", 100, FW_NORMAL, false, dpi);
}

HFONT miniant::Windows::Dpi::CreateNameFont(UINT dpi) {
    return CreateFontFromMessage(nullptr, 200, FW_BOLD, false, dpi);
}

HFONT miniant::Windows::Dpi::CreateLinkFont(UINT dpi) {
    return CreateFontFromMessage(nullptr, 100, 0, true, dpi);
}
