#include "Theme.h"

using namespace miniant;
using namespace miniant::Windows;

namespace {

Config::ThemeMode g_mode = Config::ThemeMode::Auto;
bool g_windowsIsDark = false;
bool g_resolvedDark = false;

HBRUSH g_backgroundBrush = nullptr;
HBRUSH g_editBackgroundBrush = nullptr;

// The dark look of the system controls is switched on by functions of uxtheme
// that Microsoft never documented; the numbers below are the addresses they
// live at since Windows 10 1809. The library is loaded by hand: the program
// links no import library of it. A Windows that does not have these functions
// keeps its usual look of the controls, while the colours of the windows
// themselves are painted by the program anyway.
using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
using SetPreferredAppModeFn = int(WINAPI*)(int);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using FlushMenuThemesFn = void(WINAPI*)();

const WORD ORDINAL_ALLOW_DARK_MODE_FOR_WINDOW = 133;
const WORD ORDINAL_SET_PREFERRED_APP_MODE = 135;
const WORD ORDINAL_FLUSH_MENU_THEMES = 136;

// 1 is "AllowDark" of the PreferredAppMode enumeration of uxtheme.
const int PREFERRED_APP_MODE_ALLOW_DARK = 1;

SetWindowThemeFn g_setWindowTheme = nullptr;
SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
AllowDarkModeForWindowFn g_allowDarkModeForWindow = nullptr;
FlushMenuThemesFn g_flushMenuThemes = nullptr;

bool g_uxthemeLoaded = false;

void LoadUxtheme() {
    if (g_uxthemeLoaded) {
        return;
    }

    g_uxthemeLoaded = true;

    const HMODULE module = ::LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        return;
    }

    g_setWindowTheme = reinterpret_cast<SetWindowThemeFn>(
        reinterpret_cast<void*>(::GetProcAddress(module, "SetWindowTheme")));
    g_setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(
        reinterpret_cast<void*>(::GetProcAddress(module, MAKEINTRESOURCEA(ORDINAL_SET_PREFERRED_APP_MODE))));
    g_allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(
        reinterpret_cast<void*>(::GetProcAddress(module, MAKEINTRESOURCEA(ORDINAL_ALLOW_DARK_MODE_FOR_WINDOW))));
    g_flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(
        reinterpret_cast<void*>(::GetProcAddress(module, MAKEINTRESOURCEA(ORDINAL_FLUSH_MENU_THEMES))));
}

// HKCU\...\Themes\Personalize\AppsUseLightTheme: 0 is a dark Windows. A Windows
// without the value (or without the key) is light.
bool ReadWindowsIsDark() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;

    const LSTATUS status = ::RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        &type,
        &value,
        &size);

    if (status != ERROR_SUCCESS) {
        return false;
    }

    return value == 0;
}

bool Resolve(Config::ThemeMode mode) {
    if (mode == Config::ThemeMode::Dark) {
        return true;
    }

    if (mode == Config::ThemeMode::Light) {
        return false;
    }

    return g_windowsIsDark;
}

void RecreateBrushes() {
    if (g_backgroundBrush != nullptr) {
        ::DeleteObject(g_backgroundBrush);
        g_backgroundBrush = nullptr;
    }

    if (g_editBackgroundBrush != nullptr) {
        ::DeleteObject(g_editBackgroundBrush);
        g_editBackgroundBrush = nullptr;
    }

    g_backgroundBrush = ::CreateSolidBrush(Theme::BackgroundColor());
    g_editBackgroundBrush = ::CreateSolidBrush(Theme::EditBackgroundColor());
}

// Tells Windows that the whole process prefers the dark look: the menus and the
// scrollbars of the system follow it.
void EnableDarkModeForProcess() {
    LoadUxtheme();

    if (g_setPreferredAppMode != nullptr) {
        g_setPreferredAppMode(PREFERRED_APP_MODE_ALLOW_DARK);
    }

    if (g_flushMenuThemes != nullptr) {
        g_flushMenuThemes();
    }
}

}

void Theme::SetMode(miniant::Config::ThemeMode mode) {
    g_mode = mode;
    g_windowsIsDark = ReadWindowsIsDark();
    g_resolvedDark = Resolve(g_mode);

    RecreateBrushes();

    if (g_resolvedDark) {
        EnableDarkModeForProcess();
    }
}

void Theme::Refresh() {
    const bool wasDark = g_resolvedDark;

    g_windowsIsDark = ReadWindowsIsDark();
    g_resolvedDark = Resolve(g_mode);

    if (g_resolvedDark != wasDark) {
        RecreateBrushes();
    }

    if (g_resolvedDark && g_flushMenuThemes != nullptr) {
        // The menu bar was built with the colours of the other theme.
        g_flushMenuThemes();
    }
}

bool Theme::IsDark() {
    return g_resolvedDark;
}

COLORREF Theme::BackgroundColor() {
    return g_resolvedDark ? RGB(32, 32, 32) : ::GetSysColor(COLOR_BTNFACE);
}

COLORREF Theme::TextColor() {
    return g_resolvedDark ? RGB(240, 240, 240) : ::GetSysColor(COLOR_WINDOWTEXT);
}

COLORREF Theme::MutedTextColor() {
    return g_resolvedDark ? RGB(170, 170, 170) : ::GetSysColor(COLOR_GRAYTEXT);
}

COLORREF Theme::LinkColor() {
    return g_resolvedDark ? RGB(106, 176, 255) : RGB(0, 90, 190);
}

COLORREF Theme::EditBackgroundColor() {
    return g_resolvedDark ? RGB(24, 24, 24) : ::GetSysColor(COLOR_WINDOW);
}

HBRUSH Theme::BackgroundBrush() {
    if (g_backgroundBrush == nullptr) {
        RecreateBrushes();
    }

    return g_backgroundBrush;
}

HBRUSH Theme::EditBackgroundBrush() {
    if (g_editBackgroundBrush == nullptr) {
        RecreateBrushes();
    }

    return g_editBackgroundBrush;
}

void Theme::ApplyToWindow(HWND window) {
    if (window == nullptr) {
        return;
    }

    LoadUxtheme();

    if (!g_resolvedDark) {
        return;
    }

    // The caption of the window: the number of the attribute is 20 since a
    // Windows 10 of 2020, 19 in the first builds that had it. Both are tried,
    // the later one first.
    const HMODULE dwmapi = ::LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dwmapi != nullptr) {
        using SetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        const auto setWindowAttribute = reinterpret_cast<SetWindowAttributeFn>(
            reinterpret_cast<void*>(::GetProcAddress(dwmapi, "DwmSetWindowAttribute")));

        if (setWindowAttribute != nullptr) {
            const BOOL dark = TRUE;

            if (FAILED(setWindowAttribute(window, 20, &dark, sizeof(dark)))) {
                setWindowAttribute(window, 19, &dark, sizeof(dark));
            }
        }
    }

    if (g_allowDarkModeForWindow != nullptr) {
        g_allowDarkModeForWindow(window, TRUE);
    }

    if (g_setWindowTheme != nullptr) {
        g_setWindowTheme(window, L"DarkMode_Explorer", nullptr);
    }

    if (g_flushMenuThemes != nullptr) {
        g_flushMenuThemes();
    }
}

void Theme::ApplyToControl(HWND control) {
    if (control == nullptr || !g_resolvedDark) {
        return;
    }

    LoadUxtheme();

    if (g_setWindowTheme != nullptr) {
        g_setWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
}
