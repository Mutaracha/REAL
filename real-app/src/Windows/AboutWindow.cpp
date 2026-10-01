#include "AboutWindow.h"

#include "../../res/resource.h"
#include "../AppVersion.h"
#include "../Lang.h"
#include "../Text.h"

#include <shellapi.h>

#include <string>

using namespace miniant;
using namespace miniant::Windows;

namespace {

const wchar_t ABOUT_CLASS_NAME[] = L"REAL.AboutWindow";

const int WINDOW_WIDTH = 560;
const int WINDOW_HEIGHT = 300;

const int MARGIN = 16;
const int ICON_SIZE = 48;
const int LINE_HEIGHT = 20;
const int ROW_HEIGHT = 24;
const int LABEL_WIDTH = 130;
const int NAME_HEIGHT = 30;

const int NAME_ID = 2001;
const int DESCRIPTION_ID = 2002;
const int VERSION_ID = 2003;
const int SETTINGS_LABEL_ID = 2004;
const int SETTINGS_PATH_ID = 2005;
const int LINK_LABEL_FIRST_ID = 2010;
const int LINK_FIRST_ID = 2020;
const int LINK_COUNT = 3;

// The documentation and the project: the same files and the same repository the
// rest of the program points at.
const char* const LINK_URLS[LINK_COUNT] = {
    "https://github.com/Mutaracha/REAL/blob/master/docs/usage.ru.md",
    "https://github.com/Mutaracha/REAL/blob/master/docs/CONFIG.md",
    "https://github.com/Mutaracha/REAL",
};

const Lang::Str LINK_LABELS[LINK_COUNT] = {
    Lang::Str::AboutUsage,
    Lang::Str::AboutConfig,
    Lang::Str::AboutProject,
};

// The text of a link: the address is cut down to the file name (or to the name
// of the repository) so that the line stays short, the click still opens the
// whole address.
std::wstring LinkText(int index) {
    switch (index) {
        case 0: return L"docs/usage.ru.md";
        case 1: return L"docs/CONFIG.md";
        default: return L"github.com/Mutaracha/REAL";
    }
}

struct Context {
    HWND window = nullptr;
    HINSTANCE instance = nullptr;
    HICON icon = nullptr;
    HFONT font = nullptr;
    HFONT nameFont = nullptr;
    HFONT linkFont = nullptr;
    bool done = false;
};

bool IsLinkId(int id) {
    return id >= LINK_FIRST_ID && id < LINK_FIRST_ID + LINK_COUNT;
}

HWND CreateControl(
    Context& context,
    const wchar_t* className,
    const std::wstring& text,
    DWORD style,
    int id,
    int x,
    int y,
    int width,
    int height,
    HFONT font) {
    const HWND control = ::CreateWindowExW(
        0,
        className,
        text.c_str(),
        style | WS_CHILD | WS_VISIBLE,
        x, y, width, height,
        context.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        context.instance,
        nullptr);

    if (control != nullptr && font != nullptr) {
        ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    return control;
}

void OpenUrl(HWND window, const char* url) {
    ::ShellExecuteW(window, L"open", Text::ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProcedureThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* context = static_cast<Context*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));

        if (context != nullptr) {
            context->window = window;
        }
    }

    return WindowProcedure(window, message, wParam, lParam);
}

LRESULT WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<Context*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    if (context == nullptr) {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint = {};
            const HDC dc = ::BeginPaint(window, &paint);

            if (context->icon != nullptr) {
                ::DrawIconEx(dc, MARGIN, MARGIN, context->icon, ICON_SIZE, ICON_SIZE, 0, nullptr, DI_NORMAL);
            }

            ::EndPaint(window, &paint);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            // A link is drawn in the colour of a link of the system, everything
            // else in the usual text colour.
            const HDC dc = reinterpret_cast<HDC>(wParam);
            const HWND control = reinterpret_cast<HWND>(lParam);
            const int id = control != nullptr ? ::GetDlgCtrlID(control) : 0;

            ::SetBkColor(dc, ::GetSysColor(COLOR_BTNFACE));

            if (IsLinkId(id)) {
                ::SetTextColor(dc, ::GetSysColor(COLOR_HOTLIGHT));
            } else if (id == DESCRIPTION_ID || id == VERSION_ID ||
                id == SETTINGS_LABEL_ID || id == SETTINGS_PATH_ID) {
                ::SetTextColor(dc, ::GetSysColor(COLOR_GRAYTEXT));
            } else {
                ::SetTextColor(dc, ::GetSysColor(COLOR_BTNTEXT));
            }

            return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_BTNFACE));
        }

        case WM_SETCURSOR: {
            // The hand over a link is the sign of a text that can be clicked.
            POINT cursor = {};
            ::GetCursorPos(&cursor);

            const HWND under = ::WindowFromPoint(cursor);
            if (under != nullptr && ::GetParent(under) == window && IsLinkId(::GetDlgCtrlID(under))) {
                ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }

            break;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);

            if (HIWORD(wParam) == STN_CLICKED && IsLinkId(id)) {
                OpenUrl(window, LINK_URLS[id - LINK_FIRST_ID]);
                return 0;
            }

            if (id == IDCANCEL) {
                ::DestroyWindow(window);
                return 0;
            }

            break;
        }

        case WM_CLOSE:
            ::DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            context->window = nullptr;
            context->done = true;
            return 0;

        default:
            break;
    }

    return ::DefWindowProcW(window, message, wParam, lParam);
}

}

void miniant::Windows::ShowAboutWindow(HWND owner, HINSTANCE instance, const std::wstring& settingsPath) {
    Context context;
    context.instance = instance;
    context.icon = static_cast<HICON>(::LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, ICON_SIZE, ICON_SIZE, LR_DEFAULTCOLOR));

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WindowProcedureThunk;
    windowClass.hInstance = instance;
    windowClass.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON1));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = ABOUT_CLASS_NAME;

    if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (context.icon != nullptr) {
            ::DestroyIcon(context.icon);
        }

        return;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;

    RECT desired = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    ::AdjustWindowRectEx(&desired, style, FALSE, 0);

    const HWND window = ::CreateWindowExW(
        WS_EX_CONTROLPARENT,
        ABOUT_CLASS_NAME,
        Lang::Wide(Lang::Str::AboutTitle).c_str(),
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        owner,
        nullptr,
        instance,
        &context);

    if (window == nullptr) {
        if (context.icon != nullptr) {
            ::DestroyIcon(context.icon);
        }

        ::UnregisterClassW(ABOUT_CLASS_NAME, instance);
        return;
    }

    context.font = ::CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    context.nameFont = ::CreateFontW(
        -24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    context.linkFont = ::CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const int textLeft = MARGIN + ICON_SIZE + 14;
    const int textWidth = WINDOW_WIDTH - textLeft - MARGIN;

    // The name of the program, large and bold, next to its icon.
    CreateControl(context, L"STATIC", AppInfo::NAME, SS_LEFT | SS_CENTERIMAGE,
        NAME_ID, textLeft, MARGIN - 2, textWidth, NAME_HEIGHT, context.nameFont);
    CreateControl(context, L"STATIC", AppInfo::DESCRIPTION, SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
        DESCRIPTION_ID, textLeft, MARGIN + NAME_HEIGHT - 2, textWidth, LINE_HEIGHT, context.font);
    CreateControl(context, L"STATIC", Text::ToWide(AppInfo::DisplayVersion()),
        SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, VERSION_ID,
        textLeft, MARGIN + NAME_HEIGHT + LINE_HEIGHT - 2, textWidth, LINE_HEIGHT, context.font);

    int y = MARGIN + ICON_SIZE + 22;

    // The sentence about the program, in the language of the interface.
    CreateControl(context, L"STATIC", Lang::Wide(Lang::Str::AboutText),
        SS_LEFT | SS_CENTERIMAGE, 0, MARGIN, y, WINDOW_WIDTH - 2 * MARGIN, ROW_HEIGHT, context.font);

    y += ROW_HEIGHT + 10;

    for (int i = 0; i < LINK_COUNT; ++i) {
        CreateControl(context, L"STATIC", Lang::Wide(LINK_LABELS[i]), SS_LEFT | SS_CENTERIMAGE,
            LINK_LABEL_FIRST_ID + i, MARGIN, y, LABEL_WIDTH, ROW_HEIGHT, context.font);

        // SS_NOTIFY makes the control report its clicks: the window opens the
        // page in the browser of the user.
        CreateControl(context, L"STATIC", LinkText(i), SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY,
            LINK_FIRST_ID + i, MARGIN + LABEL_WIDTH, y,
            WINDOW_WIDTH - 2 * MARGIN - LABEL_WIDTH, ROW_HEIGHT, context.linkFont);

        y += ROW_HEIGHT;
    }

    y += 6;

    CreateControl(context, L"STATIC", Lang::Wide(Lang::Str::AboutSettings), SS_LEFT | SS_CENTERIMAGE,
        SETTINGS_LABEL_ID, MARGIN, y, LABEL_WIDTH, ROW_HEIGHT, context.font);
    CreateControl(context, L"STATIC", settingsPath, SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
        SETTINGS_PATH_ID, MARGIN + LABEL_WIDTH, y,
        WINDOW_WIDTH - 2 * MARGIN - LABEL_WIDTH, ROW_HEIGHT, context.font);

    if (owner != nullptr) {
        ::EnableWindow(owner, FALSE);
    }

    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);

    MSG message = {};
    bool quit = false;

    while (!context.done) {
        const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);

        if (result <= 0) {
            quit = result == 0;
            break;
        }

        if (context.window != nullptr && ::IsDialogMessageW(context.window, &message) != FALSE) {
            continue;
        }

        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    if (quit) {
        ::PostQuitMessage(static_cast<int>(message.wParam));
    }

    if (owner != nullptr) {
        ::EnableWindow(owner, TRUE);
        ::SetForegroundWindow(owner);
    }

    if (context.window != nullptr && ::IsWindow(context.window) != FALSE) {
        ::DestroyWindow(context.window);
    }

    if (context.font != nullptr) {
        ::DeleteObject(context.font);
    }

    if (context.nameFont != nullptr) {
        ::DeleteObject(context.nameFont);
    }

    if (context.linkFont != nullptr) {
        ::DeleteObject(context.linkFont);
    }

    if (context.icon != nullptr) {
        ::DestroyIcon(context.icon);
    }

    ::UnregisterClassW(ABOUT_CLASS_NAME, instance);
}
