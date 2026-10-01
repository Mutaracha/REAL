#include "AboutWindow.h"

#include "Dpi.h"

#include "../../res/resource.h"
#include "../AppVersion.h"
#include "../Lang.h"
#include "../Text.h"
#include "TextMetrics.h"
#include "WindowPlacement.h"

#include <shellapi.h>

#include <string>
#include <vector>

using namespace miniant;
using namespace miniant::Windows;

namespace {

const wchar_t ABOUT_CLASS_NAME[] = L"REAL.AboutWindow";

// The width follows the two lines of the sentence about the program: a wider
// window would only add empty space on the right.
const int WINDOW_WIDTH = 440;
const int WINDOW_HEIGHT = 264;

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

// The project first, then the documentation: the same files and the same
// repository the rest of the program points at.
const char* const LINK_URLS[LINK_COUNT] = {
    "https://github.com/Mutaracha/REAL",
    "https://github.com/Mutaracha/REAL/blob/master/docs/usage.ru.md",
    "https://github.com/Mutaracha/REAL/blob/master/docs/CONFIG.md",
};

const Lang::Str LINK_LABELS[LINK_COUNT] = {
    Lang::Str::AboutProject,
    Lang::Str::AboutUsage,
    Lang::Str::AboutConfig,
};

// The text of a link: the address is cut down to the file name (or to the name
// of the repository) so that the line stays short, the click still opens the
// whole address.
std::wstring LinkText(int index) {
    switch (index) {
        case 1: return L"docs/usage.ru.md";
        case 2: return L"docs/CONFIG.md";
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

    // The dots per inch of the monitor the window is on: the coordinates below
    // are design pixels of a 96 DPI layout.
    UINT dpi = 96;

    int Scale(int value) const {
        return Dpi::Scale(value, dpi);
    }

    std::vector<HWND> controls;
    std::wstring settingsPath;
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
        context.Scale(x), context.Scale(y), context.Scale(width), context.Scale(height),
        context.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        context.instance,
        nullptr);

    if (control != nullptr) {
        context.controls.push_back(control);

        if (font != nullptr) {
            ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    return control;
}

// The icon is loaded in the size the current DPI asks for: a 48 pixel bitmap
// stretched to 108 pixels on a 225 % monitor would be blurry.
void ReloadIcon(Context& context) {
    if (context.icon != nullptr) {
        ::DestroyIcon(context.icon);
        context.icon = nullptr;
    }

    context.icon = static_cast<HICON>(::LoadImageW(
        context.instance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
        context.Scale(ICON_SIZE), context.Scale(ICON_SIZE), LR_DEFAULTCOLOR));
}

// The fonts and the controls of the window: built once and built again when the
// DPI changes (the design coordinates stay the same).
void BuildContent(Context& context, const std::wstring& settingsPath) {
    context.font = Dpi::CreateUiFont(context.dpi);
    context.nameFont = Dpi::CreateNameFont(context.dpi);
    context.linkFont = Dpi::CreateLinkFont(context.dpi);

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

    // The sentence about the program, in the language of the interface: two
    // lines, the break is a part of the text itself.
    CreateControl(context, L"STATIC", Lang::Wide(Lang::Str::AboutText),
        SS_LEFT, 0, MARGIN, y, WINDOW_WIDTH - 2 * MARGIN, 2 * LINE_HEIGHT, context.font);

    y += 2 * LINE_HEIGHT + 8;

    for (int i = 0; i < LINK_COUNT; ++i) {
        CreateControl(context, L"STATIC", Lang::Wide(LINK_LABELS[i]), SS_LEFT | SS_CENTERIMAGE,
            LINK_LABEL_FIRST_ID + i, MARGIN, y, LABEL_WIDTH, ROW_HEIGHT, context.font);

        // SS_NOTIFY makes the control report its clicks: the window opens the
        // page in the browser of the user. The width is the width of the text
        // itself, so the hand of the cursor appears over the link only.
        const std::wstring link = LinkText(i);

        // The measured width is in real pixels, the layout is in design ones.
        const int linkWidth = ::MulDiv(
            MeasureTextWidth(context.linkFont, link) + 2, 96, static_cast<int>(context.dpi));

        CreateControl(context, L"STATIC", link, SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY,
            LINK_FIRST_ID + i, MARGIN + LABEL_WIDTH, y, linkWidth, ROW_HEIGHT, context.linkFont);

        y += ROW_HEIGHT;
    }

    y += 6;

    CreateControl(context, L"STATIC", Lang::Wide(Lang::Str::AboutSettings), SS_LEFT | SS_CENTERIMAGE,
        SETTINGS_LABEL_ID, MARGIN, y, LABEL_WIDTH, ROW_HEIGHT, context.font);
    CreateControl(context, L"STATIC", settingsPath, SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
        SETTINGS_PATH_ID, MARGIN + LABEL_WIDTH, y,
        WINDOW_WIDTH - 2 * MARGIN - LABEL_WIDTH, ROW_HEIGHT, context.font);
}

void DestroyContent(Context& context) {
    for (const HWND control : context.controls) {
        if (control != nullptr) {
            ::DestroyWindow(control);
        }
    }

    context.controls.clear();

    for (HFONT* font : { &context.font, &context.nameFont, &context.linkFont }) {
        if (*font != nullptr) {
            ::DeleteObject(*font);
            *font = nullptr;
        }
    }
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
                ::DrawIconEx(
                    dc, context->Scale(MARGIN), context->Scale(MARGIN), context->icon,
                    context->Scale(ICON_SIZE), context->Scale(ICON_SIZE), 0, nullptr, DI_NORMAL);
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

        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                ::SetWindowPos(
                    window, nullptr, suggested->left, suggested->top,
                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }

            context->dpi = HIWORD(wParam) != 0 ? HIWORD(wParam) : Dpi::ForSystem();

            const std::wstring settingsPath = context->settingsPath;
            ReloadIcon(*context);
            DestroyContent(*context);
            BuildContent(*context, settingsPath);
            ::InvalidateRect(window, nullptr, TRUE);
            return 0;
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
    context.dpi = Dpi::ForWindow(owner);
    context.settingsPath = settingsPath;
    ReloadIcon(context);

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

    RECT desired = { 0, 0, context.Scale(WINDOW_WIDTH), context.Scale(WINDOW_HEIGHT) };
    Dpi::AdjustWindowRect(desired, style, context.dpi);

    // Near the window of the program, not in the middle of the screen.
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;

    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;

    PlaceNearOwner(owner, width, height, windowX, windowY);

    const HWND window = ::CreateWindowExW(
        WS_EX_CONTROLPARENT,
        ABOUT_CLASS_NAME,
        Lang::Wide(Lang::Str::AboutTitle).c_str(),
        style,
        windowX,
        windowY,
        width,
        height,
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

    BuildContent(context, settingsPath);

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

    DestroyContent(context);

    if (context.icon != nullptr) {
        ::DestroyIcon(context.icon);
        context.icon = nullptr;
    }

    ::UnregisterClassW(ABOUT_CLASS_NAME, instance);
}
