#include "AboutDialog.h"

#include "../AppVersion.h"
#include "../Lang.h"
#include "../Text.h"

#include <commctrl.h>
#include <shellapi.h>

#include <spdlog/fmt/fmt.h>

// The link control lives in comctl32; both build paths (CMake and build.bat)
// pick the library up from here.
#pragma comment(lib, "comctl32.lib")

#include <string>

using namespace miniant;
using namespace miniant::Windows;

namespace {

const wchar_t ABOUT_CLASS_NAME[] = L"REAL.AboutWindow";

const int CLIENT_WIDTH = 440;
const int CLIENT_HEIGHT = 168;
const int MARGIN = 14;

// A framed window with an owner: it stays out of the taskbar and gets the
// standard caption, while the modal frame makes it look like a dialog.
const DWORD ABOUT_STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
const DWORD ABOUT_EX_STYLE = WS_EX_DLGMODALFRAME;

const int ID_VERSION = 201;
const int ID_DESCRIPTION = 202;
const int ID_PROJECT = 203;
const int ID_CLOSE = 204;

struct AboutContext {
    std::wstring projectUrl;
    HWND link = nullptr;
};

// The link of the SysLink control answers to clicks and to the keyboard; both
// open the project page in the default browser.
void OpenProjectPage(const AboutContext& context) {
    if (!context.projectUrl.empty()) {
        ::ShellExecuteW(nullptr, L"open", context.projectUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void ApplyFont(HWND control, HFONT font) {
    if (control != nullptr && font != nullptr) {
        ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

LRESULT CALLBACK AboutWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<AboutContext*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            const UINT notification = HIWORD(wParam);

            if (id == ID_CLOSE && (notification == BN_CLICKED || notification == 0)) {
                ::DestroyWindow(window);
                return 0;
            }

            break;
        }

        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header != nullptr && header->idFrom == static_cast<UINT_PTR>(ID_PROJECT) &&
                (header->code == NM_CLICK || header->code == NM_RETURN)) {
                if (context != nullptr) {
                    OpenProjectPage(*context);
                }

                return 0;
            }

            break;
        }

        case WM_SETCURSOR:
            // A hand over the link, an arrow everywhere else.
            if (context != nullptr && reinterpret_cast<HWND>(wParam) == context->link) {
                ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }

            break;

        case WM_CTLCOLORSTATIC:
            ::SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_BTNFACE));

        case WM_CLOSE:
            ::DestroyWindow(window);
            return 0;

        case WM_NCDESTROY: {
            delete context;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        }

        default:
            break;
    }

    return ::DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterAboutClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return true;
    }

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &AboutWindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = ABOUT_CLASS_NAME;

    if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    registered = true;
    return true;
}

}

void miniant::Windows::ShowAboutDialog(HWND owner, HINSTANCE instance) {
    if (instance == nullptr || !RegisterAboutClass(instance)) {
        return;
    }

    auto* context = new AboutContext();
    context->projectUrl = Text::ToWide(AppInfo::PROJECT_URL);

    const std::wstring title = Lang::Wide(Str::AboutTitle);
    const std::wstring versionText = Text::ToWide(
        fmt::format(Lang::Utf8(Str::AboutVersion), AppInfo::VERSION.ToString()));
    const std::wstring description = Lang::Wide(Str::AboutDescription);
    const std::wstring project = Text::ToWide(
        fmt::format(Lang::Utf8(Str::AboutProject), AppInfo::PROJECT_URL));
    const std::wstring closeText = Lang::Wide(Str::AboutClose);

    RECT desired = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
    ::AdjustWindowRectEx(&desired, ABOUT_STYLE, FALSE, ABOUT_EX_STYLE);

    const HWND window = ::CreateWindowExW(
        ABOUT_EX_STYLE,
        ABOUT_CLASS_NAME,
        title.c_str(),
        ABOUT_STYLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        owner,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        delete context;
        return;
    }

    ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));

    const HFONT titleFont = ::CreateFontW(
        -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const HFONT bodyFont = ::CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const HWND versionLabel = ::CreateWindowExW(
        0, L"STATIC", versionText.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN, MARGIN, CLIENT_WIDTH - 2 * MARGIN, 22,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_VERSION)), instance, nullptr);

    const HWND descriptionLabel = ::CreateWindowExW(
        0, L"STATIC", description.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN, MARGIN + 30, CLIENT_WIDTH - 2 * MARGIN, 40,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DESCRIPTION)), instance, nullptr);

    HWND link = ::CreateWindowExW(
        0, WC_LINK, project.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        MARGIN, MARGIN + 76, CLIENT_WIDTH - 2 * MARGIN, 22,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROJECT)), instance, nullptr);

    if (link == nullptr) {
        // Without the link control the address is still shown, only not
        // clickable.
        link = ::CreateWindowExW(
            0, L"STATIC", Text::ToWide(AppInfo::PROJECT_URL).c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            MARGIN, MARGIN + 76, CLIENT_WIDTH - 2 * MARGIN, 22,
            window, nullptr, instance, nullptr);
    }

    const HWND closeButton = ::CreateWindowExW(
        0, L"BUTTON", closeText.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
        CLIENT_WIDTH - MARGIN - 110, CLIENT_HEIGHT - MARGIN - 30, 110, 30,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CLOSE)), instance, nullptr);

    context->link = link;

    ApplyFont(versionLabel, titleFont);
    ApplyFont(descriptionLabel, bodyFont);
    ApplyFont(link, bodyFont);
    ApplyFont(closeButton, bodyFont);

    // Centre on the owner (or on the screen when there is none).
    RECT ownerRect = { 0, 0, 0, 0 };
    if (owner == nullptr || ::GetWindowRect(owner, &ownerRect) == FALSE) {
        ownerRect = { 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN) };
    }

    RECT windowRect = { 0, 0, 0, 0 };
    ::GetWindowRect(window, &windowRect);

    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    ::SetWindowPos(window, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    if (owner != nullptr) {
        ::EnableWindow(owner, FALSE);
    }

    ::SetFocus(link != nullptr ? link : closeButton);

    MSG message = {};
    while (::IsWindow(window) != FALSE && ::GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (::IsDialogMessageW(window, &message) == FALSE) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }

    if (owner != nullptr) {
        ::EnableWindow(owner, TRUE);
        ::SetActiveWindow(owner);
    }

    if (::IsWindow(window) != FALSE) {
        ::DestroyWindow(window);
    }

    ::DeleteObject(titleFont);
    ::DeleteObject(bodyFont);
}
