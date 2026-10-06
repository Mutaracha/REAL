#include "SettingsWindow.h"

#include "BalloonTip.h"
#include "Dpi.h"
#include "Filesystem.h"
#include "SettingsChecks.h"

#include "../../res/resource.h"
#include "../Lang.h"
#include "../Text.h"
#include "TextMetrics.h"
#include "WindowPlacement.h"

#include <commctrl.h>
#include <shellapi.h>

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

using namespace miniant;
using namespace miniant::Windows;
using namespace miniant::Windows::SettingsChecks;

namespace {

const wchar_t SETTINGS_CLASS_NAME[] = L"REAL.SettingsWindow";

// The window is created with this size; once the pages are built it gets the
// width of the widest row and the height of the tallest page (see
// FitWindowToContent).
const int WINDOW_WIDTH = 680;
const int WINDOW_HEIGHT = 660;

const int MARGIN = 14;
const int ROW_HEIGHT = 24;
const int ROW_STEP = 24;

// The settings of a page stand in groups: a frame of the system with its
// caption on the top line. A frame keeps GROUP_INSET from the border of the
// tabs and GROUP_GAP from the next frame; its rows start GROUP_PADDING inside
// it, GROUP_TOP below its top (under the caption) and end GROUP_BOTTOM above
// its bottom.
const int GROUP_INSET = 4;
const int GROUP_PADDING = 10;
const int GROUP_TOP = 22;
const int GROUP_BOTTOM = 8;
const int GROUP_GAP = 8;
// The left edge of the rows of a group.
const int ROW_X = MARGIN + GROUP_INSET + GROUP_PADDING;

// The space between the longest label of a group and its lists and fields.
const int LABEL_GAP = 8;
// A list is as wide as its longest item in either language plus what the list
// draws around it: the borders, the margins of the text and the arrow button.
const int COMBO_CHROME = 30;
// A field of a number is as wide as the largest value it takes plus its border
// and the margins of the text.
const int EDIT_CHROME = 16;
// The space between two columns of check boxes.
const int COLUMN_GAP = 24;
// The longest path the field of the log file takes.
const int MAX_PATH_LENGTH = 1024;
const int PAGE_TOP = 62;
// The buttons have the size of every push button of the program (see
// StandardButtonWidth): the width is known once the font is.
const int BUTTON_HEIGHT = STANDARD_BUTTON_HEIGHT;
const int BUTTON_GAP = 8;

// The row of buttons at the bottom of the window, the space between it and the
// tabs, and the space between the lowest frame of a page and the bottom border
// of the tabs.
const int FRAME_HEIGHT = BUTTON_HEIGHT;
const int FRAME_GAP = 8;
const int PAGE_BOTTOM_PADDING = 6;

// The air between the border of the tabs and the controls of a page, in real
// pixels: the same on the left and on the right.
const int PAGE_AIR = 2;

// A caption is measured in real pixels and rounded to design ones: a few
// pixels of reserve keep its last letter from being cut off.
const int HEADER_SLACK = 4;

// "Save" puts back a value that the window does not take, and a balloon at its
// field says why: it disappears by itself after a while, or when the user
// types, switches the page or leaves the window (see CheckAllFields).
const UINT_PTR TIP_TIMER_ID = 1;
const UINT TIP_DURATION_MS = 6000;
const int TIP_MAX_WIDTH = 320;
// The note that the folder of the settings file has been copied: shorter, it
// only confirms the click.
const UINT COPY_TIP_DURATION_MS = 3000;

// The background of the pages is made again after a change of the theme or of
// the colours of the system, once the tab control has taken it.
const UINT WM_APP_PAGE_BRUSH = WM_APP + 2;

// A language chosen in the window (or read by "Reload"): the window takes it
// once the notification of the control is over (see SwitchLanguage). wParam is
// the control that keeps the keyboard.
const UINT WM_APP_LANGUAGE = WM_APP + 3;

// The descriptions of the settings that show when the mouse rests on them stay
// long enough to be read. The tools of the areas of greyed out controls have
// numbers of their own (see UpdateHintAreas).
const int HINT_DURATION_MS = 20000;
const UINT_PTR HINT_AREA_BASE = 0x10000;

// Every control of the window has a number: the values are read back by it, so
// a control that is not on screen at the moment (another page is open) keeps
// its state.
enum class Id : int {
    Language = 1000,
    StartWithWindows,
    StartMinimizedToTray,
    MinimizeToTray,
    CloseButtonAction,

    TrayEnabled,
    TrayMenuCaption,
    TrayMenuToggle,
    TrayMenuReinit,
    TrayMenuLog,
    TrayMenuDiagnostics,

    NotificationError,
    NotificationDeviceChange,
    NotificationStateChange,

    AudioDataFlow,
    AudioBufferMode,
    AudioFixedBufferFrames,
    AudioBufferHint,

    ReinitDefaultDevice,
    ReinitResumeFromSleep,
    ReinitSessionUnlock,
    ReinitFailureTimeout,
    ReinitDebounce,

    ProcessPriority,

    UpdateCheckOnStartup,

    LogLevel,
    LogFilePath,
    LogMaxFileSize,
    LogMaxFiles,

    // The settings file: its path and the buttons that open it and read it
    // again (the last group of the page "Other").
    SettingsPath,
    OpenFile,
    Reload,

    // The frame of the window: the tab control and the buttons belong to no
    // page, they stay on screen all the time.
    Tabs,
    Cancel,
    Save,
};

enum class Page {
    Window,
    Audio,
    Other,
};

struct Context {
    HWND window = nullptr;
    HINSTANCE instance = nullptr;
    Config::Settings* settings = nullptr;
    std::wstring settingsPath;

    // The buffer range of the device: the hint under the fixed buffer and the
    // check of its value (see FixedBufferProblem).
    BufferRange bufferRange;

    // The language the window shows: the language of the program when it
    // opens, then the one chosen in it (see SwitchLanguage). The rest of the
    // program takes a new language only with "Save".
    Lang::Language language = Lang::Language::English;

    // The dots per inch of the monitor the window is on: every coordinate below
    // is a design pixel of a 96 DPI layout and is scaled by CreateControl.
    UINT dpi = 96;

    int Scale(int value) const {
        return Dpi::Scale(value, dpi);
    }

    // Every control of the window in the order of creation: the pages use the
    // list to show and hide themselves, a change of the DPI uses it to build
    // the window again, and a change of the language to lay the same controls
    // out again (see Relayout).
    std::vector<HWND> allControls;

    std::vector<std::pair<HWND, Page>> pageControls;
    Page page = Page::Window;

    // The system tab control and the offset of a page inside it: every control
    // of a page is created with this offset, so the pages sit in the display
    // area of the tabs and never overlap them.
    HWND tabs = nullptr;
    int offsetX = 0;
    int offsetY = 0;

    // The right edge of the display area of the tabs in design pixels: no
    // control of a page is wider than the space up to it, so nothing is drawn
    // over the border of the tabs.
    int pageRight = WINDOW_WIDTH - MARGIN;

    // The top of the row of buttons in design pixels: right under the tabs,
    // whose height follows the tallest page.
    int frameTop = WINDOW_HEIGHT - MARGIN - FRAME_HEIGHT;

    // The width of the client area in design pixels: it follows the widest
    // row of the pages (see FitWindowToContent).
    int windowWidth = WINDOW_WIDTH;

    // The right edge of the widest control of the pages in design pixels.
    int contentRight = 0;

    // The right edge of the last tab in real pixels, with the labels of the
    // language that makes it longer (see CreateTabs).
    int tabsRight = 0;

    // The column of the labels in front of the lists and the fields of the
    // group being built, and the width of a button: both follow the longer of
    // the two texts of every caption, so they do not depend on the language.
    int labelWidth = 0;
    int buttonWidth = 75;

    // The group being built: the top of its frame and its caption (see
    // BeginGroup). The frames of all groups are stretched to the width of the
    // pages once the window has got its size (see StretchGroups).
    int groupTop = 0;
    Lang::Str groupCaption = Lang::Str::Count;
    std::vector<HWND> groupBoxes;

    bool saved = false;
    bool done = false;

    // The window is being laid out again over the controls it has (another
    // language, see Relayout): CreateControl takes them in the order of their
    // creation instead of making new ones, and the size of the window waits
    // until the window may be drawn again.
    struct Relayout {
        bool active = false;
        bool failed = false;
        size_t next = 0;
        SIZE windowSize = {};
    };

    Relayout relayout;

    HFONT font = nullptr;

    // The height Windows gives a list in this font, in real pixels: the fields
    // take it too (see MeasureFieldHeight).
    int fieldHeight = 0;

    // The background of the controls of the pages: a picture of the empty
    // display area of the tabs, and the top left corner of that area in the
    // client coordinates of the tab control (see CreatePageBrush).
    HBRUSH pageBrush = nullptr;
    HBITMAP pageBitmap = nullptr;
    POINT pageOrigin = {};

    // The descriptions of the settings: one tooltip of the system with a tool
    // for every described control and its label, and an area tool of the
    // window for the time the control is greyed out (see UpdateHintAreas).
    struct HintArea {
        HWND control;
        Page page;
    };

    HWND hints = nullptr;
    std::vector<HintArea> hintAreas;

    // The value of every checked field that the window took last: the one in
    // use when it opened, then the one of Reload or of the device. "Save"
    // replaces a value it does not take with it.
    std::vector<std::pair<Id, std::wstring>> lastValid;

    // The balloon of a field whose value was put back (see ShowFieldTip).
    BalloonTip tip{ TIP_TIMER_ID };
};

int ControlId(Id id) {
    return static_cast<int>(id);
}

// The tab control lives in comctl32: it has to be asked for before the first
// control of that library is created.
void InitCommonControlsOnce() {
    static bool ready = false;

    if (ready) {
        return;
    }

    INITCOMMONCONTROLSEX settings = {};
    settings.dwSize = sizeof(settings);
    settings.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;

    ready = ::InitCommonControlsEx(&settings) != FALSE;
}

// A pair "value of the settings - text of the window": the lists below are read
// in both directions, so a combo box and the settings file cannot disagree.
template <typename Enum>
struct Choice {
    Enum value;
    Lang::Str text;
};

const Choice<Config::CloseAction> CLOSE_ACTIONS[] = {
    { Config::CloseAction::Minimize, Lang::Str::SettingsCloseMinimize },
    { Config::CloseAction::Exit, Lang::Str::SettingsCloseExit },
};

const Choice<Config::DataFlow> DATA_FLOWS[] = {
    { Config::DataFlow::Render, Lang::Str::SettingsFlowRender },
    { Config::DataFlow::Capture, Lang::Str::SettingsFlowCapture },
    { Config::DataFlow::Both, Lang::Str::SettingsFlowBoth },
};

const Choice<Config::BufferMode> BUFFER_MODES[] = {
    { Config::BufferMode::Minimum, Lang::Str::SettingsBufferMinimum },
    { Config::BufferMode::Fixed, Lang::Str::SettingsBufferFixed },
};

const Choice<Config::ProcessPriority> PRIORITIES[] = {
    { Config::ProcessPriority::Normal, Lang::Str::SettingsPriorityNormal },
    { Config::ProcessPriority::BelowNormal, Lang::Str::SettingsPriorityBelowNormal },
    { Config::ProcessPriority::Idle, Lang::Str::SettingsPriorityIdle },
};

// The texts of a list, as identifiers: the list shows them in the current
// language and is as wide as the longest of them in either language.
template <typename Enum, size_t N>
std::vector<Lang::Str> Texts(const Choice<Enum> (&choices)[N]) {
    std::vector<Lang::Str> items;
    items.reserve(N);

    for (size_t i = 0; i < N; ++i) {
        items.push_back(choices[i].text);
    }

    return items;
}

template <typename Enum, size_t N>
int IndexOf(const Choice<Enum> (&choices)[N], Enum value) {
    for (size_t i = 0; i < N; ++i) {
        if (choices[i].value == value) {
            return static_cast<int>(i);
        }
    }

    return 0;
}

template <typename Enum, size_t N>
Enum ValueAt(const Choice<Enum> (&choices)[N], int index) {
    if (index < 0 || index >= static_cast<int>(N)) {
        return choices[0].value;
    }

    return choices[static_cast<size_t>(index)].value;
}

// The levels of the log file from "no file" to "everything": every next one
// keeps more. The text says what gets into the file, the value of the settings
// file follows it in parentheses.
const Choice<const char*> LOG_LEVELS[] = {
    { "off", Lang::Str::SettingsLogLevelOff },
    { "error", Lang::Str::SettingsLogLevelError },
    { "warn", Lang::Str::SettingsLogLevelWarn },
    { "info", Lang::Str::SettingsLogLevelInfo },
    { "debug", Lang::Str::SettingsLogLevelDebug },
    { "trace", Lang::Str::SettingsLogLevelTrace },
};

const size_t LOG_LEVEL_COUNT = sizeof(LOG_LEVELS) / sizeof(LOG_LEVELS[0]);

// The value of the file is read the way the log reads it (Log.cpp): the case
// of the letters and the synonyms do not matter, anything unknown is "info".
int LogLevelIndex(const std::string& level) {
    std::string value = Text::ToLowerAscii(Text::Trim(level));

    if (value == "none") {
        value = "off";
    } else if (value == "warning") {
        value = "warn";
    } else if (value == "err") {
        value = "error";
    }

    int info = 0;

    for (size_t i = 0; i < LOG_LEVEL_COUNT; ++i) {
        if (value == LOG_LEVELS[i].value) {
            return static_cast<int>(i);
        }

        if (std::string(LOG_LEVELS[i].value) == "info") {
            info = static_cast<int>(i);
        }
    }

    return info;
}

bool IsLogLevelOff(int index) {
    return index >= 0 && index < static_cast<int>(LOG_LEVEL_COUNT) &&
        std::string(LOG_LEVELS[static_cast<size_t>(index)].value) == "off";
}

// The items of the list of the language. The window shows the chosen language
// at once (see SwitchLanguage).
const Choice<const char*> LANGUAGES[] = {
    { "auto", Lang::Str::SettingsLanguageAuto },
    { "en", Lang::Str::SettingsLanguageEnglish },
    { "ru", Lang::Str::SettingsLanguageRussian },
};

const size_t LANGUAGE_COUNT = sizeof(LANGUAGES) / sizeof(LANGUAGES[0]);

// The reader of the file keeps the language in its main form ("auto", "en",
// "ru"); anything else is "auto", as for the program itself.
int LanguageIndex(const std::string& code) {
    const std::string value = Text::ToLowerAscii(Text::Trim(code));

    for (size_t i = 0; i < LANGUAGE_COUNT; ++i) {
        if (value == LANGUAGES[i].value) {
            return static_cast<int>(i);
        }
    }

    return 0;
}

const char* LanguageCode(int index) {
    return index >= 0 && index < static_cast<int>(LANGUAGE_COUNT)
        ? LANGUAGES[static_cast<size_t>(index)].value
        : LANGUAGES[0].value;
}

// Every control of the page that is being built is remembered: switching a page
// is nothing but showing and hiding them.
void BindToPage(Context& context, HWND control) {
    if (control != nullptr) {
        context.pageControls.emplace_back(control, context.page);
    }
}

// The box of a check mark plus the gap between it and the caption.
const int CHECK_GLYPH_WIDTH = 24;

// The width of a text in design pixels: the measured width is in real pixels
// of the current DPI, the layout works in design ones. Zero when the text could
// not be measured.
int DesignTextWidth(Context& context, HFONT font, const std::wstring& text) {
    const int textWidth = MeasureTextWidth(font, text);

    return textWidth > 0 ? ::MulDiv(textWidth, 96, static_cast<int>(context.dpi)) : 0;
}

// The pages remember the right edge of their widest control: the window is
// made just wide enough for it.
void ExtendContent(Context& context, int right) {
    context.contentRight = (std::max)(context.contentRight, right);
}

// A text of the window in the language it shows.
std::wstring TextOf(const Context& context, Lang::Str id) {
    return Lang::Wide(id, context.language);
}

// The label of a list or a field ends with a colon: the value follows it.
std::wstring LabelText(const Context& context, Lang::Str label) {
    return TextOf(context, label) + L":";
}

// A control is never wider than its own text: a click far to the right of a
// caption does nothing, and the caption is the only thing that reacts. It is
// not wider than the page either, whatever the length of a translation.
int CheckWidth(Context& context, const std::wstring& text, int x) {
    const int available = context.pageRight - x;
    const int textWidth = DesignTextWidth(context, context.font, text);

    return textWidth > 0 ? (std::min)(textWidth + CHECK_GLYPH_WIDTH, available) : available;
}

// The languages of the window. The layout makes room for the longer of the two
// texts of every caption: a change of the language moves nothing but the field
// of a row with a label of its own (see RowColumn), and the window keeps its
// size.
const Lang::Language LAYOUT_LANGUAGES[] = { Lang::Language::English, Lang::Language::Russian };

// The width of a text of the window in design pixels in the language that
// needs more room for it; the suffix is the colon of a label.
int WidestTextWidth(Context& context, Lang::Str id, const wchar_t* suffix = L"") {
    int widest = 0;

    for (const Lang::Language language : LAYOUT_LANGUAGES) {
        widest = (std::max)(widest, DesignTextWidth(context, context.font, Lang::Wide(id, language) + suffix));
    }

    return widest;
}

// The room of a check box in the language that needs more of it: the next
// column and the width of the window follow it, while the box itself is as
// wide as its own text in the language shown.
int WidestCheckWidth(Context& context, Lang::Str text, int x) {
    int widest = 0;

    for (const Lang::Language language : LAYOUT_LANGUAGES) {
        widest = (std::max)(widest, CheckWidth(context, Lang::Wide(text, language), x));
    }

    return widest;
}

// The text of a control as it is now.
std::wstring WindowText(HWND control) {
    const int length = ::GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = ::GetWindowTextW(control, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<size_t>(copied) : 0);

    return text;
}

// The control created at this point of the layout, when the window lays its
// controls out again (see Relayout): it gets the text of the language shown -
// unless its text is a value, then text is null - and its new place. A control
// that does not answer to the layout stops the relayout, and the window is
// built again instead.
HWND ReuseControl(Context& context, const std::wstring* text, Id id, int x, int y, int width, int height) {
    Context::Relayout& relayout = context.relayout;

    if (relayout.failed || relayout.next >= context.allControls.size()) {
        relayout.failed = true;
        return nullptr;
    }

    const HWND control = context.allControls[relayout.next++];

    if (::GetDlgCtrlID(control) != ControlId(id)) {
        relayout.failed = true;
        return nullptr;
    }

    if (text != nullptr && WindowText(control) != *text) {
        ::SetWindowTextW(control, text->c_str());
    }

    ::SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    return control;
}

// A place given in design pixels of the page being built, in real pixels of
// the client area.
RECT PlaceOnPage(const Context& context, int x, int y, int width, int height) {
    const int left = context.Scale(x) + context.offsetX;
    const int top = context.Scale(y) + context.offsetY;

    return { left, top, left + context.Scale(width), top + context.Scale(height) };
}

// A control at a place in real pixels of the client area.
HWND CreateControlAt(
    Context& context,
    const wchar_t* className,
    const std::wstring& text,
    DWORD style,
    Id id,
    const RECT& place,
    DWORD extendedStyle = 0) {
    const int width = place.right - place.left;
    const int height = place.bottom - place.top;

    // Laid out again, the window keeps its controls. A field keeps what is
    // typed in it; a list and the tabs have no text of their own.
    if (context.relayout.active) {
        const bool valueText = ::lstrcmpiW(className, L"EDIT") == 0 || ::lstrcmpiW(className, L"COMBOBOX") == 0 ||
            ::lstrcmpiW(className, WC_TABCONTROLW) == 0;

        return ReuseControl(context, valueText ? nullptr : &text, id, place.left, place.top, width, height);
    }

    const HWND control = ::CreateWindowExW(
        extendedStyle,
        className,
        text.c_str(),
        style | WS_CHILD | WS_VISIBLE,
        place.left,
        place.top,
        width,
        height,
        context.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ControlId(id))),
        context.instance,
        nullptr);

    if (control != nullptr) {
        context.allControls.push_back(control);

        if (context.font != nullptr) {
            ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(context.font), TRUE);
        }
    }

    return control;
}

// A control at a place in design pixels of the page being built.
HWND CreateControl(
    Context& context,
    const wchar_t* className,
    const std::wstring& text,
    DWORD style,
    Id id,
    int x,
    int y,
    int width,
    int height,
    DWORD extendedStyle = 0) {
    return CreateControlAt(
        context, className, text, style, id, PlaceOnPage(context, x, y, width, height), extendedStyle);
}

HWND Get(Context& context, Id id);

// ------------------------------------------------------------ groups ------

// A group starts at y: its rows begin at ROW_X under the caption of the frame,
// and its lists and fields stand in a column of their own, right after the
// longest of the given labels in either language (see RowColumn for a row with
// a longer label).
int BeginGroup(Context& context, Lang::Str caption, int y, const std::vector<Lang::Str>& labels = {}) {
    context.groupTop = y;
    context.groupCaption = caption;

    int widest = 0;
    for (const Lang::Str label : labels) {
        widest = (std::max)(widest, WidestTextWidth(context, label, L":"));
    }

    context.labelWidth = widest > 0 ? widest + LABEL_GAP : 0;

    return y + GROUP_TOP;
}

// The frame of the group around its rows, with the caption on its top line.
// It is created after the rows, so it lies under them and never paints over
// them (WS_CLIPSIBLINGS). Its width is set when the window has got its size
// (see StretchGroups). Returns the bottom of the frame.
int EndGroup(Context& context, int y) {
    const int left = MARGIN + GROUP_INSET;
    const int bottom = y + GROUP_BOTTOM;
    const std::wstring caption = TextOf(context, context.groupCaption);

    // The caption is in the font of the window, as the rest of the text.
    const HWND frame = CreateControl(
        context, L"BUTTON", caption, BS_GROUPBOX | WS_CLIPSIBLINGS, static_cast<Id>(0),
        left, context.groupTop, context.pageRight - GROUP_INSET - left, bottom - context.groupTop);

    if (frame != nullptr) {
        context.groupBoxes.push_back(frame);
    }

    BindToPage(context, frame);

    // The caption has to fit into the frame as well, in either language.
    ExtendContent(context, ROW_X + WidestTextWidth(context, context.groupCaption) + HEADER_SLACK);

    return bottom;
}

// ------------------------------------------------------- descriptions -----

// The description of a setting shows when the mouse rests on the control or on
// its label (a static control lets the mouse through to the window unless it
// has SS_NOTIFY, see AddLabel). A greyed out control takes no mouse at all:
// for that time an area tool of the window covers it (see UpdateHintAreas).
void AddHint(Context& context, HWND control, HWND label, Lang::Str text) {
    if (context.hints == nullptr || control == nullptr || text == Lang::Str::Count) {
        return;
    }

    std::wstring body = TextOf(context, text);

    // Laid out again, the window keeps its tools and gives them the texts of
    // the language shown.
    const UINT message = context.relayout.active ? TTM_UPDATETIPTEXTW : TTM_ADDTOOLW;

    for (const HWND tool : { control, label }) {
        if (tool == nullptr) {
            continue;
        }

        TTTOOLINFOW info = {};
        info.cbSize = TTTOOLINFOW_V2_SIZE;
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = context.window;
        info.uId = reinterpret_cast<UINT_PTR>(tool);
        info.lpszText = body.data();
        ::SendMessageW(context.hints, message, 0, reinterpret_cast<LPARAM>(&info));
    }

    // The area starts empty: UpdateHintAreas sets it while the control is
    // greyed out on the open page.
    TTTOOLINFOW area = {};
    area.cbSize = TTTOOLINFOW_V2_SIZE;
    area.uFlags = TTF_SUBCLASS;
    area.hwnd = context.window;
    area.uId = HINT_AREA_BASE + context.hintAreas.size();
    area.lpszText = body.data();
    ::SendMessageW(context.hints, message, 0, reinterpret_cast<LPARAM>(&area));

    context.hintAreas.push_back({ control, context.page });
}

// The area tools follow their controls: a greyed out control of the open page
// is covered, anything else is not (the controls of a hidden page take no
// mouse either, and their areas would answer for the open one).
void UpdateHintAreas(Context& context) {
    if (context.hints == nullptr || context.window == nullptr) {
        return;
    }

    for (size_t i = 0; i < context.hintAreas.size(); ++i) {
        const Context::HintArea& entry = context.hintAreas[i];
        RECT area = {};

        if (entry.page == context.page && ::IsWindowEnabled(entry.control) == FALSE) {
            ::GetWindowRect(entry.control, &area);
            ::MapWindowPoints(HWND_DESKTOP, context.window, reinterpret_cast<POINT*>(&area), 2);
        }

        TTTOOLINFOW info = {};
        info.cbSize = TTTOOLINFOW_V2_SIZE;
        info.hwnd = context.window;
        info.uId = HINT_AREA_BASE + i;
        info.rect = area;
        ::SendMessageW(context.hints, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

// The tooltip of the descriptions: made with the controls, in the font of the
// window (it follows the DPI of the monitor), and wrapped to a few lines.
void CreateHints(Context& context) {
    context.hints = ::CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        context.window,
        nullptr,
        context.instance,
        nullptr);

    if (context.hints == nullptr) {
        return;
    }

    if (context.font != nullptr) {
        ::SendMessageW(context.hints, WM_SETFONT, reinterpret_cast<WPARAM>(context.font), FALSE);
    }

    ::SendMessageW(context.hints, TTM_SETMAXTIPWIDTH, 0, context.Scale(TIP_MAX_WIDTH));
    ::SendMessageW(context.hints, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELPARAM(HINT_DURATION_MS, 0));
}

void DestroyHints(Context& context) {
    if (context.hints != nullptr) {
        ::DestroyWindow(context.hints);
        context.hints = nullptr;
    }

    context.hintAreas.clear();
}

// ------------------------------------------------------------- rows -------

// A check box at the given left edge: ROW_X, or the second column of a group.
// The box is as wide as its own text; the page makes room for the text of
// either language.
HWND AddCheckAt(Context& context, Id id, Lang::Str text, bool value, int x, int y) {
    const std::wstring caption = TextOf(context, text);
    const int width = CheckWidth(context, caption, x);

    const HWND check = CreateControl(
        context, L"BUTTON", caption, BS_AUTOCHECKBOX | WS_TABSTOP, id,
        x, y, width, ROW_HEIGHT);

    // Laid out again, a check box keeps the state the user has given it.
    if (check != nullptr && !context.relayout.active) {
        ::SendMessageW(check, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    BindToPage(context, check);
    ExtendContent(context, x + WidestCheckWidth(context, text, x));
    return check;
}

int AddCheck(Context& context, Id id, Lang::Str text, bool value, int y, Lang::Str hint = Lang::Str::Count) {
    AddHint(context, AddCheckAt(context, id, text, value, ROW_X, y), nullptr, hint);
    return y + ROW_STEP;
}

// The caption of a part of a group ("Icon menu items:"): plain text of a row.
int AddCaption(Context& context, Id id, Lang::Str text, int y) {
    const int available = context.pageRight - ROW_X;
    const auto widthOf = [&context, available](const std::wstring& caption) {
        const int textWidth = DesignTextWidth(context, context.font, caption);
        return textWidth > 0 ? (std::min)(textWidth + HEADER_SLACK, available) : available;
    };

    const std::wstring caption = TextOf(context, text);

    BindToPage(context, CreateControl(
        context, L"STATIC", caption, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, id,
        ROW_X, y, widthOf(caption), ROW_HEIGHT));

    for (const Lang::Language language : LAYOUT_LANGUAGES) {
        ExtendContent(context, ROW_X + widthOf(Lang::Wide(text, language)));
    }

    return y + ROW_STEP;
}

// A list is as wide as its longest item in either language: a change of the
// language moves nothing, and no list is wider than what it shows.
int ComboWidth(Context& context, const std::vector<Lang::Str>& texts) {
    int widest = 0;

    for (const Lang::Str text : texts) {
        for (const Lang::Language language : LAYOUT_LANGUAGES) {
            widest = (std::max)(widest, DesignTextWidth(context, context.font, Lang::Wide(text, language)));
        }
    }

    return widest > 0 ? widest + COMBO_CHROME : 160;
}

// The height Windows gives a list in the font of the window, measured on a list
// that is never shown. A field takes the same height: a field as high as its
// row would touch the field of the next row, and the fields and the lists of a
// page are of one height. Never as high as the row, so that two fields one
// under the other always keep a gap.
int MeasureFieldHeight(const Context& context) {
    const int row = context.Scale(ROW_HEIGHT);
    int height = 0;

    const HWND probe = ::CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 0, 0, context.Scale(100), row * 8,
        context.window, nullptr, context.instance, nullptr);

    if (probe != nullptr) {
        if (context.font != nullptr) {
            ::SendMessageW(probe, WM_SETFONT, reinterpret_cast<WPARAM>(context.font), FALSE);
        }

        RECT rect = {};
        if (::GetWindowRect(probe, &rect) != FALSE) {
            height = rect.bottom - rect.top;
        }

        ::DestroyWindow(probe);
    }

    return height > 0 ? (std::min)(height, row - 1) : context.Scale(BUTTON_HEIGHT);
}

// The room of the column of a row in the language that needs more of it: the
// width of the window follows it (see RowColumn).
int RowRoom(Context& context, Lang::Str label) {
    return (std::max)(context.labelWidth, WidestTextWidth(context, label, L":") + LABEL_GAP);
}

// The column of a row: the column of its group, or the end of its own label
// when that label is longer ("Fixed buffer, frames:" under the lists that
// follow "Devices:"). Such a field stands right after its label in the
// language shown, as every other field does after its column, so it moves
// with a change of the language; the window keeps the room of the longer label.
int RowColumn(Context& context, Lang::Str label) {
    const int own = DesignTextWidth(context, context.font, LabelText(context, label));

    return own > 0 ? (std::max)(context.labelWidth, own + LABEL_GAP) : RowRoom(context, label);
}

// The label of a list or a field, with its colon, up to the column of its row.
// A described label takes the mouse, so that its description shows.
HWND AddLabel(Context& context, Lang::Str label, int y, int column, bool described) {
    const HWND control = CreateControl(
        context, L"STATIC", LabelText(context, label), SS_LEFT | SS_CENTERIMAGE | (described ? SS_NOTIFY : 0),
        static_cast<Id>(0), ROW_X, y, column, ROW_HEIGHT);

    BindToPage(context, control);
    return control;
}

int AddCombo(
    Context& context, Id id, Lang::Str label, const std::vector<Lang::Str>& texts, int selected, int y,
    Lang::Str hint = Lang::Str::Count) {
    const int column = RowColumn(context, label);
    const HWND caption = AddLabel(context, label, y, column, hint != Lang::Str::Count);
    const int x = ROW_X + column;
    const int width = ComboWidth(context, texts);

    const HWND combo = CreateControl(
        context, L"COMBOBOX", L"",
        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, id,
        x, y, width, ROW_HEIGHT * 8);

    ExtendContent(context, ROW_X + RowRoom(context, label) + width);

    // A list belongs to its page like every other control: without this the
    // lists of all three pages would be drawn in the same place at once.
    BindToPage(context, combo);

    if (combo != nullptr) {
        // Laid out again, a list gets the items of the language shown and keeps
        // the one the user has chosen.
        const int chosen = context.relayout.active
            ? static_cast<int>(::SendMessageW(combo, CB_GETCURSEL, 0, 0))
            : selected;

        if (context.relayout.active) {
            ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        }

        for (const Lang::Str text : texts) {
            ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(TextOf(context, text).c_str()));
        }

        if (chosen >= 0 && chosen < static_cast<int>(texts.size())) {
            ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(chosen), 0);
        }
    }

    AddHint(context, combo, caption, hint);
    return y + ROW_STEP;
}

// The value a checked field had when the window took it last: a value it does
// not take is replaced with this one.
void RememberValid(Context& context, Id id, const std::wstring& text) {
    for (auto& entry : context.lastValid) {
        if (entry.first == id) {
            entry.second = text;
            return;
        }
    }

    context.lastValid.emplace_back(id, text);
}

std::wstring LastValid(const Context& context, Id id) {
    for (const auto& entry : context.lastValid) {
        if (entry.first == id) {
            return entry.second;
        }
    }

    return {};
}

// A text field. A field of a number takes digits only and no more of them than
// its largest value has.
int AddEdit(
    Context& context, Id id, Lang::Str label, const std::wstring& value, int width, int y,
    int maxLength, bool digitsOnly = false, Lang::Str hint = Lang::Str::Count) {
    const int column = RowColumn(context, label);
    const HWND caption = AddLabel(context, label, y, column, hint != Lang::Str::Count);
    const int x = ROW_X + column;

    // A field stands at the top of its row like a list and is as high as one
    // (see MeasureFieldHeight).
    RECT place = PlaceOnPage(context, x, y, width, ROW_HEIGHT);
    if (context.fieldHeight > 0) {
        place.bottom = place.top + context.fieldHeight;
    }

    const HWND edit = CreateControlAt(
        context, L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL | (digitsOnly ? ES_NUMBER : 0), id,
        place, WS_EX_CLIENTEDGE);

    if (edit != nullptr && maxLength > 0) {
        ::SendMessageW(edit, EM_LIMITTEXT, static_cast<WPARAM>(maxLength), 0);
    }

    BindToPage(context, edit);

    // Laid out again, a field keeps what is typed in it and the value the
    // window took last.
    if (!context.relayout.active) {
        RememberValid(context, id, value);
    }

    AddHint(context, edit, caption, hint);

    ExtendContent(context, ROW_X + RowRoom(context, label) + width);
    return y + ROW_STEP;
}

int DigitCount(int value) {
    int digits = 1;

    while (value >= 10) {
        value /= 10;
        ++digits;
    }

    return digits;
}

// A field of a number is as wide as its largest value, unless its group gives
// it a width (the fields of the log follow the list of the level).
int AddNumber(
    Context& context, Id id, Lang::Str label, const std::wstring& value, const Config::NumberLimits& limits, int y,
    Lang::Str hint = Lang::Str::Count, int width = 0) {
    if (width <= 0) {
        const int textWidth = DesignTextWidth(context, context.font, std::to_wstring(limits.maximum));
        width = (textWidth > 0 ? textWidth : 7 * DigitCount(limits.maximum)) + EDIT_CHROME;
    }

    return AddEdit(context, id, label, value, width, y, DigitCount(limits.maximum), true, hint);
}

HWND Get(Context& context, Id id) {
    if (context.window == nullptr) {
        return nullptr;
    }

    return ::GetDlgItem(context.window, ControlId(id));
}

bool IsChecked(Context& context, Id id) {
    const HWND control = Get(context, id);
    if (control == nullptr) {
        return false;
    }

    return ::SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int SelectedIndex(Context& context, Id id) {
    const HWND control = Get(context, id);
    if (control == nullptr) {
        return -1;
    }

    return static_cast<int>(::SendMessageW(control, CB_GETCURSEL, 0, 0));
}

std::wstring ReadText(Context& context, Id id, const std::wstring& fallback) {
    const HWND control = Get(context, id);
    if (control == nullptr) {
        return fallback;
    }

    const std::wstring text = WindowText(control);
    return text.empty() ? fallback : text;
}

void SetChecked(Context& context, Id id, bool value) {
    const HWND control = Get(context, id);
    if (control != nullptr) {
        ::SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void SetText(Context& context, Id id, const std::wstring& text) {
    const HWND control = Get(context, id);
    if (control != nullptr) {
        ::SetWindowTextW(control, text.c_str());
    }
}

void SetSelected(Context& context, Id id, int index) {
    const HWND control = Get(context, id);
    if (control != nullptr) {
        ::SendMessageW(control, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    }
}

// The number of a field within its limits, or the fallback.
int ReadNumber(Context& context, Id id, const Config::NumberLimits& limits, int fallback) {
    long value = 0;

    return ParseNumber(ReadText(context, id, L""), value) && value >= limits.minimum && value <= limits.maximum
        ? static_cast<int>(value)
        : fallback;
}

// The hint under the field of the fixed buffer: the range of the device in
// frames and in milliseconds, in the given language. Empty while no device has
// been seen.
std::wstring BufferHintText(const Context& context, Lang::Language language) {
    const BufferRange& range = context.bufferRange;
    if (range.minimum == 0) {
        return {};
    }

    const auto milliseconds = [&range](uint32_t frames) {
        return range.sampleRate > 0
            ? 1000.0 * static_cast<double>(frames) / static_cast<double>(range.sampleRate)
            : 0.0;
    };

    return Text::ToWide(fmt::format(
        Lang::Utf8(Lang::Str::SettingsBufferHint, language),
        range.minimum,
        range.maximum,
        BufferStep(range),
        Lang::Decimal(milliseconds(range.minimum), language),
        Lang::Decimal(milliseconds(range.maximum), language)));
}

// ------------------------------------------------------------ checks ------

// The fields whose values are checked, in the order of the pages, with the
// labels their balloons are titled with.
struct CheckedField {
    Id id;
    Lang::Str label;
};

const CheckedField CHECKED_FIELDS[] = {
    { Id::AudioFixedBufferFrames, Lang::Str::SettingsFixedBufferFrames },
    { Id::ReinitFailureTimeout, Lang::Str::SettingsReinitFailureTimeout },
    { Id::ReinitDebounce, Lang::Str::SettingsReinitDebounce },
    { Id::LogFilePath, Lang::Str::SettingsLogFilePath },
    { Id::LogMaxFileSize, Lang::Str::SettingsLogMaxFileSize },
    { Id::LogMaxFiles, Lang::Str::SettingsLogMaxFiles },
};

// The numbers among them take the limits of the settings file.
struct NumberField {
    Id id;
    Config::NumberLimits limits;
};

const NumberField NUMBER_FIELDS[] = {
    { Id::ReinitFailureTimeout, Config::FAILURE_TIMEOUT_SEC_LIMITS },
    { Id::ReinitDebounce, Config::DEBOUNCE_SEC_LIMITS },
    { Id::LogMaxFileSize, Config::LOG_FILE_SIZE_MB_LIMITS },
    { Id::LogMaxFiles, Config::LOG_FILES_LIMITS },
};

const CheckedField* FindCheckedField(UINT controlId) {
    for (const CheckedField& field : CHECKED_FIELDS) {
        if (static_cast<UINT>(ControlId(field.id)) == controlId) {
            return &field;
        }
    }

    return nullptr;
}

// What is wrong with a value of a checked field: the text of its balloon.
// Empty when the window takes the value.
std::wstring FieldProblem(const Context& context, Id id, const std::wstring& text) {
    if (id == Id::AudioFixedBufferFrames) {
        return FixedBufferProblem(context.bufferRange, text, context.language);
    }

    if (id == Id::LogFilePath) {
        return PathProblem(text, context.language);
    }

    for (const NumberField& field : NUMBER_FIELDS) {
        if (field.id == id) {
            return NumberProblem(text, field.limits, context.language);
        }
    }

    return {};
}

// The value a rejected field gets back: the last one the window took. When
// even that one does not fit (a size of the fixed buffer from the file that
// the device does not take), the buffer the device runs with, or the default
// path of the log.
std::wstring RestoreText(const Context& context, Id id) {
    const std::wstring last = LastValid(context, id);
    if (FieldProblem(context, id, last).empty()) {
        return last;
    }

    if (id == Id::AudioFixedBufferFrames) {
        const BufferRange& range = context.bufferRange;
        const uint32_t frames = range.current > 0 ? range.current : range.minimum;

        return frames > 0 ? std::to_wstring(frames) : std::wstring();
    }

    if (id == Id::LogFilePath) {
        return Text::ToWide(Config::LoggingSettings().filePath);
    }

    return last;
}

// The field of the fixed buffer is empty while no size is set (0 in the file).
std::wstring FixedBufferText(unsigned int frames) {
    return frames > 0 ? std::to_wstring(frames) : std::wstring();
}

// ---------------------------------------------------------------- pages -----

int BuildWindowPage(Context& context) {
    context.page = Page::Window;

    const Config::Settings& settings = *context.settings;

    // The frames keep the same distance from the top of the tabs as from their
    // sides.
    int y = PAGE_TOP + GROUP_INSET;

    y = BeginGroup(context, Lang::Str::SettingsHeaderApplication, y,
        { Lang::Str::SettingsLanguage, Lang::Str::SettingsCloseAction });

    // The language is a list, as wide as its longest item in either language:
    // the window keeps its size whatever the language (see SwitchLanguage).
    y = AddCombo(context, Id::Language, Lang::Str::SettingsLanguage, Texts(LANGUAGES),
        LanguageIndex(settings.application.language), y);
    y = AddCheck(context, Id::StartWithWindows, Lang::Str::SettingsStartWithWindows,
        settings.application.startWithWindows, y, Lang::Str::SettingsHintStartWithWindows);
    y = AddCheck(context, Id::StartMinimizedToTray, Lang::Str::SettingsStartMinimized,
        settings.application.startMinimizedToTray, y, Lang::Str::SettingsHintStartMinimized);
    y = AddCheck(context, Id::MinimizeToTray, Lang::Str::SettingsMinimizeToTray,
        settings.application.minimizeToTray, y, Lang::Str::SettingsHintMinimizeToTray);
    y = AddCombo(context, Id::CloseButtonAction, Lang::Str::SettingsCloseAction, Texts(CLOSE_ACTIONS),
        IndexOf(CLOSE_ACTIONS, settings.application.closeButtonAction), y, Lang::Str::SettingsHintCloseAction);

    y = EndGroup(context, y) + GROUP_GAP;

    // The icon and the items of its menu that can be hidden, under a caption of
    // their own and greyed out with it while the icon is off. The items stand
    // in two columns, as the groups of the menu itself: the state and the
    // restart, the log and the diagnostics. The status line, "Settings" and
    // "Exit" are always in the menu, so they have no switch here.
    y = BeginGroup(context, Lang::Str::SettingsHeaderTray, y);
    y = AddCheck(context, Id::TrayEnabled, Lang::Str::SettingsTrayEnabled, settings.tray.enabled, y);
    y = AddCaption(context, Id::TrayMenuCaption, Lang::Str::SettingsTrayMenuCaption, y);
    {
        const int secondColumn = ROW_X + COLUMN_GAP + (std::max)(
            WidestCheckWidth(context, Lang::Str::TrayToggleEnabled, ROW_X),
            WidestCheckWidth(context, Lang::Str::TrayReinitialize, ROW_X));

        AddCheckAt(context, Id::TrayMenuToggle, Lang::Str::TrayToggleEnabled,
            settings.tray.menu.toggleEnabled, ROW_X, y);
        AddCheckAt(context, Id::TrayMenuReinit, Lang::Str::TrayReinitialize,
            settings.tray.menu.reinitialize, ROW_X, y + ROW_STEP);
        AddCheckAt(context, Id::TrayMenuLog, Lang::Str::TrayLog,
            settings.tray.menu.openLog, secondColumn, y);
        AddCheckAt(context, Id::TrayMenuDiagnostics, Lang::Str::TrayDiagnostics,
            settings.tray.menu.diagnostics, secondColumn, y + ROW_STEP);

        y += 2 * ROW_STEP;
    }
    y = EndGroup(context, y) + GROUP_GAP;

    // The balloons are shown by the tray icon: without it they are greyed out
    // as well.
    y = BeginGroup(context, Lang::Str::SettingsHeaderNotifications, y);
    y = AddCheck(context, Id::NotificationError, Lang::Str::SettingsNotifyError,
        settings.tray.notifications.onError, y, Lang::Str::SettingsHintNotifyError);
    y = AddCheck(context, Id::NotificationDeviceChange, Lang::Str::SettingsNotifyDeviceChange,
        settings.tray.notifications.onDeviceChange, y, Lang::Str::SettingsHintNotifyDeviceChange);
    y = AddCheck(context, Id::NotificationStateChange, Lang::Str::SettingsNotifyStateChange,
        settings.tray.notifications.onStateChange, y, Lang::Str::SettingsHintNotifyStateChange);

    return EndGroup(context, y);
}

int BuildAudioPage(Context& context) {
    context.page = Page::Audio;

    const Config::Settings& settings = *context.settings;

    // The frames keep the same distance from the top of the tabs as from their
    // sides.
    int y = PAGE_TOP + GROUP_INSET;

    // The lists stand right after "Devices:"; the longer label of the fixed
    // buffer keeps its field right after itself in the language shown (see
    // RowColumn).
    y = BeginGroup(context, Lang::Str::SettingsHeaderAudio, y,
        { Lang::Str::SettingsDataFlow, Lang::Str::SettingsBuffer });
    y = AddCombo(context, Id::AudioDataFlow, Lang::Str::SettingsDataFlow, Texts(DATA_FLOWS),
        IndexOf(DATA_FLOWS, settings.audio.dataFlow), y, Lang::Str::SettingsHintDataFlow);
    y = AddCombo(context, Id::AudioBufferMode, Lang::Str::SettingsBuffer, Texts(BUFFER_MODES),
        IndexOf(BUFFER_MODES, settings.audio.buffer), y, Lang::Str::SettingsHintBuffer);
    y = AddNumber(context, Id::AudioFixedBufferFrames, Lang::Str::SettingsFixedBufferFrames,
        FixedBufferText(settings.audio.fixedBufferFrames), Config::FIXED_BUFFER_FRAMES_LIMITS, y,
        Lang::Str::SettingsHintFixedBuffer);

    // The range of the device under the name of the field: the values the
    // fixed buffer takes, so that nobody has to look them up in the report of
    // the diagnostics. It is shown for the fixed buffer only, and its row
    // stays when it is hidden: nothing below moves with the choice of the
    // buffer (see UpdateHintVisibility).
    {
        const int available = context.pageRight - GROUP_INSET - GROUP_PADDING - ROW_X;
        const auto widthOf = [&context, available](const std::wstring& hint) {
            const int textWidth = hint.empty() ? 0 : DesignTextWidth(context, context.font, hint);
            return textWidth > 0 ? (std::min)(textWidth + HEADER_SLACK, available) : context.labelWidth;
        };

        const std::wstring hint = BufferHintText(context, context.language);

        BindToPage(context, CreateControl(
            context, L"STATIC", hint, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, Id::AudioBufferHint,
            ROW_X, y, widthOf(hint), ROW_HEIGHT));

        for (const Lang::Language language : LAYOUT_LANGUAGES) {
            ExtendContent(context, ROW_X + widthOf(BufferHintText(context, language)));
        }

        y += ROW_STEP;
    }

    y = EndGroup(context, y) + GROUP_GAP;

    // Of the device events only the change of a default device restarts the
    // streams: REAL holds them on the default devices, and Windows reports a
    // default device that goes away or comes back as such a change as well
    // (see App::OnDeviceEvent).
    y = BeginGroup(context, Lang::Str::SettingsHeaderReinit, y,
        { Lang::Str::SettingsReinitFailureTimeout, Lang::Str::SettingsReinitDebounce });
    y = AddCheck(context, Id::ReinitDefaultDevice, Lang::Str::SettingsReinitDeviceChanged,
        settings.audio.reinit.defaultDeviceChanged, y);
    y = AddCheck(context, Id::ReinitResumeFromSleep, Lang::Str::SettingsReinitResume,
        settings.audio.reinit.resumeFromSleep, y);
    y = AddCheck(context, Id::ReinitSessionUnlock, Lang::Str::SettingsReinitUnlock,
        settings.audio.reinit.sessionUnlock, y);
    y = AddNumber(context, Id::ReinitFailureTimeout, Lang::Str::SettingsReinitFailureTimeout,
        std::to_wstring(settings.audio.reinit.failureTimeoutSec), Config::FAILURE_TIMEOUT_SEC_LIMITS, y);
    y = AddNumber(context, Id::ReinitDebounce, Lang::Str::SettingsReinitDebounce,
        std::to_wstring(settings.audio.reinit.debounceSec), Config::DEBOUNCE_SEC_LIMITS, y);

    return EndGroup(context, y);
}

int BuildOtherPage(Context& context) {
    context.page = Page::Other;

    const Config::Settings& settings = *context.settings;

    // The frames keep the same distance from the top of the tabs as from their
    // sides.
    int y = PAGE_TOP + GROUP_INSET;

    // The list of the priority stands in the column of the fields of the log:
    // the two groups share their labels.
    const std::vector<Lang::Str> columnLabels = {
        Lang::Str::SettingsProcessPriority, Lang::Str::SettingsLogLevel, Lang::Str::SettingsLogFilePath,
        Lang::Str::SettingsLogMaxFileSize, Lang::Str::SettingsLogMaxFiles,
    };

    y = BeginGroup(context, Lang::Str::SettingsHeaderPerformance, y, columnLabels);
    y = AddCombo(context, Id::ProcessPriority, Lang::Str::SettingsProcessPriority, Texts(PRIORITIES),
        IndexOf(PRIORITIES, settings.performance.processPriority), y, Lang::Str::SettingsHintProcessPriority);
    y = EndGroup(context, y) + GROUP_GAP;

    y = BeginGroup(context, Lang::Str::SettingsHeaderUpdates, y);
    y = AddCheck(context, Id::UpdateCheckOnStartup, Lang::Str::SettingsCheckOnStartup,
        settings.updates.checkOnStartup, y);
    y = EndGroup(context, y) + GROUP_GAP;

    // The fields of the log are as wide as the list of the level: the group
    // reads as one column.
    y = BeginGroup(context, Lang::Str::SettingsHeaderLog, y, columnLabels);
    {
        const int width = ComboWidth(context, Texts(LOG_LEVELS));

        y = AddCombo(context, Id::LogLevel, Lang::Str::SettingsLogLevel, Texts(LOG_LEVELS),
            LogLevelIndex(settings.logging.level), y);
        y = AddEdit(context, Id::LogFilePath, Lang::Str::SettingsLogFilePath,
            Text::ToWide(settings.logging.filePath), width, y, MAX_PATH_LENGTH, false,
            Lang::Str::SettingsHintLogFilePath);
        y = AddNumber(context, Id::LogMaxFileSize, Lang::Str::SettingsLogMaxFileSize,
            std::to_wstring(settings.logging.maxFileSizeMb), Config::LOG_FILE_SIZE_MB_LIMITS, y,
            Lang::Str::SettingsHintLogMaxFileSize, width);
        y = AddNumber(context, Id::LogMaxFiles, Lang::Str::SettingsLogMaxFiles,
            std::to_wstring(settings.logging.maxFiles), Config::LOG_FILES_LIMITS, y,
            Lang::Str::SettingsHintLogMaxFiles, width);
    }
    y = EndGroup(context, y) + GROUP_GAP;

    // The settings file: its path, the editor of the system for a manual edit
    // and Reload, which reads the file into the window again - "Save" is still
    // what writes and applies it. A click on the path copies the folder of the
    // file (see CopySettingsFolder); the path is as wide as its text, and a
    // long one gets the width of the frame and loses its middle, not the name
    // of the file (see StretchGroups).
    y = BeginGroup(context, Lang::Str::SettingsHeaderSettingsFile, y);
    {
        const HWND path = CreateControl(
            context, L"STATIC", context.settingsPath,
            SS_LEFT | SS_CENTERIMAGE | SS_PATHELLIPSIS | SS_NOPREFIX | SS_NOTIFY,
            Id::SettingsPath, ROW_X, y, context.pageRight - GROUP_INSET - GROUP_PADDING - ROW_X, ROW_HEIGHT);

        BindToPage(context, path);
        AddHint(context, path, nullptr, Lang::Str::SettingsHintSettingsPath);
        y += ROW_STEP;
    }
    {
        const int buttonWidth = context.buttonWidth;

        BindToPage(context, CreateControl(
            context, L"BUTTON", TextOf(context, Lang::Str::SettingsOpenFile),
            BS_PUSHBUTTON | WS_TABSTOP, Id::OpenFile,
            ROW_X, y, buttonWidth, BUTTON_HEIGHT));

        BindToPage(context, CreateControl(
            context, L"BUTTON", TextOf(context, Lang::Str::SettingsReload),
            BS_PUSHBUTTON | WS_TABSTOP, Id::Reload,
            ROW_X + buttonWidth + BUTTON_GAP, y, buttonWidth, BUTTON_HEIGHT));

        ExtendContent(context, ROW_X + 2 * buttonWidth + BUTTON_GAP);
        y += ROW_STEP;
    }

    return EndGroup(context, y);
}

// The labels of the tabs, in the order of the pages.
const Lang::Str TAB_TEXTS[] = {
    Lang::Str::SettingsTabWindow,
    Lang::Str::SettingsTabAudio,
    Lang::Str::SettingsTabOther,
};

const size_t TAB_COUNT = sizeof(TAB_TEXTS) / sizeof(TAB_TEXTS[0]);

// Puts the labels of a language on the tabs: inserted when the tabs are made,
// replaced afterwards.
void PutTabTexts(Context& context, Lang::Language language, bool insert) {
    for (size_t i = 0; i < TAB_COUNT; ++i) {
        const std::wstring text = Lang::Wide(TAB_TEXTS[i], language);

        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(text.c_str());

        ::SendMessageW(
            context.tabs, insert ? TCM_INSERTITEMW : TCM_SETITEMW, static_cast<WPARAM>(i),
            reinterpret_cast<LPARAM>(&item));
    }
}

// The tab control of the system: it draws the tabs itself, so they can never
// overlap each other or the page. The pages are ordinary controls of the window
// that are placed inside the display area of the tabs and are shown one page at
// a time.
void CreateTabs(Context& context) {
    const int top = MARGIN;
    // A provisional bottom: FitWindowToContent sets the real one when the pages
    // are built and their height is known.
    const int bottom = WINDOW_HEIGHT - MARGIN - FRAME_HEIGHT - FRAME_GAP;

    // WS_CLIPSIBLINGS: the tabs never paint over the controls of the pages that
    // lie on them; WS_GROUP: the arrows on the tabs switch the pages and never
    // walk into the controls.
    context.tabs = CreateControl(
        context, WC_TABCONTROLW, L"",
        WS_TABSTOP | WS_GROUP | WS_CLIPSIBLINGS | TCS_TABS, Id::Tabs,
        MARGIN, top, WINDOW_WIDTH - 2 * MARGIN, bottom - top);

    if (context.tabs == nullptr) {
        return;
    }

    // The tabs get the labels of the other language first and are measured
    // with both: the window fits them in either language (see
    // FitWindowToContent). The labels of the language shown stay; laid out
    // again, the window keeps its tabs and replaces their labels.
    const Lang::Language other =
        context.language == Lang::Language::English ? Lang::Language::Russian : Lang::Language::English;
    bool insert = !context.relayout.active;
    context.tabsRight = 0;

    for (const Lang::Language language : { other, context.language }) {
        PutTabTexts(context, language, insert);
        insert = false;

        RECT lastTab = {};
        if (::SendMessageW(
                context.tabs, TCM_GETITEMRECT, static_cast<WPARAM>(TAB_COUNT - 1),
                reinterpret_cast<LPARAM>(&lastTab)) != FALSE) {
            context.tabsRight = (std::max)(context.tabsRight, static_cast<int>(lastTab.right));
        }
    }

    // The display area of the tabs: the pages are built in its coordinates.
    RECT display = {};
    ::GetClientRect(context.tabs, &display);
    ::SendMessageW(context.tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&display));

    // The display area of the tabs is already in real pixels, the pages are
    // written in design ones: only the two design values are scaled here.
    context.offsetX = display.left;
    context.offsetY = context.Scale(top) + display.top - context.Scale(PAGE_TOP);

    // A little air between the frame of the tabs and the first control.
    context.offsetX += PAGE_AIR;
    context.offsetY += PAGE_AIR;

    // The same air on the right: a control of a page ends before the right
    // border of the tabs.
    context.pageRight = MARGIN + ::MulDiv(
        display.right - display.left - 2 * PAGE_AIR, 96, static_cast<int>(context.dpi));
}

// ------------------------------------------------------------ balloon -----

void HideFieldTip(Context& context) {
    context.tip.Hide();
}

// Shows the balloon under a field, with the sound of a warning: the title is
// the label of the field, the text says what it takes and what it got back.
void ShowFieldTip(Context& context, HWND field, Lang::Str title, const std::wstring& text) {
    ::MessageBeep(MB_ICONWARNING);
    context.tip.Show(
        context.window, field, TextOf(context, title), text, context.Scale(TIP_MAX_WIDTH), TIP_DURATION_MS);
}

// The range of the device is a note for the fixed buffer only: with the
// minimum buffer it is hidden, and its row stays empty.
void UpdateHintVisibility(Context& context) {
    const HWND hint = Get(context, Id::AudioBufferHint);
    if (hint == nullptr) {
        return;
    }

    const bool fixedBuffer =
        ValueAt(BUFFER_MODES, SelectedIndex(context, Id::AudioBufferMode)) == Config::BufferMode::Fixed;
    const bool visible = context.page == Page::Audio && fixedBuffer && ::GetWindowTextLengthW(hint) > 0;

    ::ShowWindow(hint, visible ? SW_SHOW : SW_HIDE);
}

// The parts of the window live further down the file; the rebuild uses them.
void ShowPage(Context& context, Page page);
void ReadControls(Context& context, Config::Settings& updated);
void UpdateEnabledStates(Context& context);

void ResetPageOffset(Context& context) {
    context.offsetX = 0;
    context.offsetY = 0;
}

// The window is as high as its tallest page and as wide as its widest row: the
// tabs end a little below and to the right of the frames of the pages, the row
// of buttons follows right under them. Everything is counted in real pixels of
// the current DPI, so the window fits again on another monitor.
void FitWindowToContent(Context& context, int contentBottom) {
    if (context.window == nullptr || context.tabs == nullptr) {
        return;
    }

    RECT tabsRect = {};
    ::GetWindowRect(context.tabs, &tabsRect);
    ::MapWindowPoints(HWND_DESKTOP, context.window, reinterpret_cast<POINT*>(&tabsRect), 2);

    const int tabsWidth = tabsRect.right - tabsRect.left;
    const int tabsHeight = tabsRect.bottom - tabsRect.top;

    // What the tabs draw around their display area: the borders.
    RECT display = { 0, 0, tabsWidth, tabsHeight };
    ::SendMessageW(context.tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&display));
    const int borderRight = tabsWidth - display.right;
    const int borderBelow = tabsHeight - display.bottom;

    // The pages are still placed with the offset of the display area here; the
    // widest control gets the same air on the right as the controls have on
    // the left.
    const int contentRight = context.Scale(context.contentRight) + context.offsetX + PAGE_AIR;
    int width = contentRight - tabsRect.left + borderRight;

    // The labels of the tabs fit as well, in either language...
    if (context.tabsRight > 0) {
        width = (std::max)(width, context.tabsRight + context.Scale(4));
    }

    // ...and so do the two buttons under the tabs.
    width = (std::max)(width, context.Scale(2 * context.buttonWidth + BUTTON_GAP));

    const int tabsBottom =
        context.Scale(contentBottom + PAGE_BOTTOM_PADDING) + context.offsetY + borderBelow;

    ::SetWindowPos(
        context.tabs, nullptr, 0, 0, width, tabsBottom - tabsRect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    const int clientWidth = tabsRect.left + width + context.Scale(MARGIN);

    context.windowWidth = ::MulDiv(clientWidth, 96, static_cast<int>(context.dpi));
    context.frameTop = ::MulDiv(tabsBottom, 96, static_cast<int>(context.dpi)) + FRAME_GAP;

    RECT window = { 0, 0, clientWidth, context.Scale(context.frameTop + FRAME_HEIGHT + MARGIN) };
    Dpi::AdjustWindowRect(
        window, static_cast<DWORD>(::GetWindowLongPtrW(context.window, GWL_STYLE)), context.dpi);

    // Laid out again, the window is not drawn at the moment: its size is set
    // when it may be drawn again (see Relayout).
    if (context.relayout.active) {
        context.relayout.windowSize = { window.right - window.left, window.bottom - window.top };
        return;
    }

    ::SetWindowPos(
        context.window, nullptr, 0, 0, window.right - window.left, window.bottom - window.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Every frame reaches the right border of the tabs, whatever the width of its
// own rows: the frames of a page line up, and so do the pages. The path of the
// settings file is as wide as its text, but not wider than its frame.
void StretchGroups(Context& context) {
    if (context.window == nullptr || context.tabs == nullptr) {
        return;
    }

    RECT tabs = {};
    ::GetWindowRect(context.tabs, &tabs);
    ::MapWindowPoints(HWND_DESKTOP, context.window, reinterpret_cast<POINT*>(&tabs), 2);

    RECT display = { 0, 0, tabs.right - tabs.left, tabs.bottom - tabs.top };
    ::SendMessageW(context.tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&display));

    const int frameRight = tabs.left + display.right - PAGE_AIR - context.Scale(GROUP_INSET);

    const auto widen = [&context](HWND control, int right) {
        RECT rect = {};
        ::GetWindowRect(control, &rect);
        ::MapWindowPoints(HWND_DESKTOP, context.window, reinterpret_cast<POINT*>(&rect), 2);

        ::SetWindowPos(
            control, nullptr, 0, 0, (std::max)(0, right - static_cast<int>(rect.left)), rect.bottom - rect.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    };

    for (const HWND frame : context.groupBoxes) {
        widen(frame, frameRight);
    }

    if (const HWND path = Get(context, Id::SettingsPath)) {
        RECT rect = {};
        ::GetWindowRect(path, &rect);
        ::MapWindowPoints(HWND_DESKTOP, context.window, reinterpret_cast<POINT*>(&rect), 2);

        const int textWidth = MeasureTextWidth(context.font, context.settingsPath);
        const int right = frameRight - context.Scale(GROUP_PADDING);

        widen(path, textWidth > 0
            ? (std::min)(right, static_cast<int>(rect.left) + textWidth + context.Scale(HEADER_SLACK))
            : right);
    }
}

// The background of the controls of the pages. With visual styles the tab
// control draws its display area in the colour (or the texture) of the theme,
// and a label, a check box or a frame that painted the usual grey of a window
// would stand out as a grey box. The brush is a picture of the empty display
// area, made by the tab control itself, and it is laid from the top left
// corner of that area wherever it is used (see AlignPageBrush): the controls
// blend in with any theme, and with the classic look as well.
void DeletePageBrush(Context& context) {
    if (context.pageBrush != nullptr) {
        ::DeleteObject(context.pageBrush);
        context.pageBrush = nullptr;
    }

    if (context.pageBitmap != nullptr) {
        ::DeleteObject(context.pageBitmap);
        context.pageBitmap = nullptr;
    }
}

void CreatePageBrush(Context& context) {
    DeletePageBrush(context);

    if (context.window == nullptr || context.tabs == nullptr) {
        return;
    }

    RECT display = {};
    ::GetClientRect(context.tabs, &display);
    ::SendMessageW(context.tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&display));

    const int width = display.right - display.left;
    const int height = display.bottom - display.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    const HDC screen = ::GetDC(context.window);
    if (screen == nullptr) {
        return;
    }

    const HDC memory = ::CreateCompatibleDC(screen);
    const HBITMAP bitmap = ::CreateCompatibleBitmap(screen, width, height);
    ::ReleaseDC(context.window, screen);

    if (memory == nullptr || bitmap == nullptr) {
        if (memory != nullptr) {
            ::DeleteDC(memory);
        }

        if (bitmap != nullptr) {
            ::DeleteObject(bitmap);
        }

        return;
    }

    // The tab control paints itself so that its display area falls on the
    // bitmap; the tabs and the borders fall outside it.
    const HGDIOBJ previous = ::SelectObject(memory, bitmap);
    ::SetViewportOrgEx(memory, -display.left, -display.top, nullptr);
    ::SendMessageW(context.tabs, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_ERASEBKGND);
    ::SelectObject(memory, previous);
    ::DeleteDC(memory);

    context.pageBrush = ::CreatePatternBrush(bitmap);
    context.pageBitmap = bitmap;
    context.pageOrigin = { display.left, display.top };
}

// Lays the brush of the pages from the top left corner of the display area in
// a DC of the control (or of the window) that paints with it.
void AlignPageBrush(const Context& context, HDC dc, HWND painter) {
    POINT origin = context.pageOrigin;
    ::MapWindowPoints(context.tabs, painter, &origin, 1);
    ::LPtoDP(dc, &origin, 1);
    ::SetBrushOrgEx(dc, origin.x, origin.y, nullptr);
}

bool IsPageControl(const Context& context, HWND control) {
    for (const auto& entry : context.pageControls) {
        if (entry.first == control) {
            return true;
        }
    }

    return false;
}

// The window under the tabs: the grey of a window, and the display area of the
// tabs in the colours of the pages. The frames of the groups do not paint their
// inside, and a themed control that draws the background of its parent behind
// its rounded corners gets the page there.
void EraseBackground(const Context& context, HDC dc) {
    RECT client = {};
    ::GetClientRect(context.window, &client);
    ::FillRect(dc, &client, ::GetSysColorBrush(COLOR_BTNFACE));

    if (context.pageBrush == nullptr || context.tabs == nullptr) {
        return;
    }

    RECT display = {};
    ::GetClientRect(context.tabs, &display);
    ::SendMessageW(context.tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&display));
    ::MapWindowPoints(context.tabs, context.window, reinterpret_cast<POINT*>(&display), 2);

    AlignPageBrush(context, dc, context.window);
    ::FillRect(dc, &display, context.pageBrush);
}

// The frame of the window: the buttons that end it, under the tabs.
void BuildFrame(Context& context) {
    // Every button has the same size; created from left to right, so that Tab
    // walks the row in reading order.
    const int rowY = context.frameTop;
    const int buttonWidth = context.buttonWidth;
    const int right = context.windowWidth - MARGIN;

    CreateControl(
        context, L"BUTTON", TextOf(context, Lang::Str::SettingsCancel),
        BS_PUSHBUTTON | WS_TABSTOP, Id::Cancel,
        right - 2 * buttonWidth - BUTTON_GAP, rowY, buttonWidth, BUTTON_HEIGHT);

    CreateControl(
        context, L"BUTTON", TextOf(context, Lang::Str::SettingsSave),
        BS_DEFPUSHBUTTON | WS_TABSTOP, Id::Save,
        right - buttonWidth, rowY, buttonWidth, BUTTON_HEIGHT);
}

// Every control of the window for the current DPI, or the same controls laid
// out again (see Relayout). The order of creation is the order of Tab: the
// fields of the pages, the buttons of the frame, and the tabs, which are put
// under everything else at the end (see below). The frame is built after the
// pages because it is placed under the tabs, whose height follows the tallest
// page.
void BuildContent(Context& context) {
    // The sizes that follow the texts of both languages and the font. Laid out
    // again, the window keeps its font and with it the height of the fields.
    context.buttonWidth = StandardButtonWidth(context.font, context.dpi);
    context.contentRight = 0;
    context.groupBoxes.clear();

    if (!context.relayout.active) {
        context.fieldHeight = MeasureFieldHeight(context);
    }

    CreateTabs(context);

    // Laid out again, the window keeps its tooltip (see AddHint).
    if (!context.relayout.active) {
        CreateHints(context);
    }

    const int windowBottom = BuildWindowPage(context);
    const int audioBottom = BuildAudioPage(context);
    const int otherBottom = BuildOtherPage(context);

    // The frames end GROUP_PADDING after the widest row, and the tabs a little
    // after the frames.
    ExtendContent(context, context.contentRight + GROUP_PADDING + GROUP_INSET);

    FitWindowToContent(context, (std::max)({ windowBottom, audioBottom, otherBottom }));
    StretchGroups(context);
    CreatePageBrush(context);

    ResetPageOffset(context);
    BuildFrame(context);

    // The tabs lie under the controls of the pages and never paint over them
    // (WS_CLIPSIBLINGS). Tab still walks the same circle: from the tabs to the
    // first field of the open page, through the page to the buttons and back
    // to the tabs.
    if (context.tabs != nullptr) {
        ::SetWindowPos(context.tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// Builds the content again for another DPI (or for another language when the
// controls do not answer to the layout, see SwitchLanguage). The design
// coordinates never change, CreateControl scales them: the window of one
// monitor keeps the layout that was approved, only larger or smaller.
void Rebuild(Context& context, UINT dpi) {
    // Whatever the user has already typed is read back first: the controls are
    // destroyed and built again, and nothing typed may be lost because the
    // window was moved to another monitor. A field keeps its text even when the
    // window does not take the value: "Save" checks it.
    Config::Settings edited = *context.settings;
    ReadControls(context, edited);
    *context.settings = edited;

    std::vector<std::pair<Id, std::wstring>> typed;
    for (const CheckedField& field : CHECKED_FIELDS) {
        typed.emplace_back(field.id, ReadText(context, field.id, L""));
    }

    // The balloon and the descriptions point at controls that are about to be
    // destroyed.
    HideFieldTip(context);
    DestroyHints(context);

    // The pages are built one after another and the last one would stay open:
    // the page the user was on is remembered.
    const Page page = context.page;

    for (const HWND control : context.allControls) {
        if (control != nullptr) {
            ::DestroyWindow(control);
        }
    }

    context.allControls.clear();
    context.pageControls.clear();
    context.groupBoxes.clear();
    context.tabs = nullptr;
    DeletePageBrush(context);

    if (context.font != nullptr) {
        ::DeleteObject(context.font);
    }

    context.dpi = dpi != 0 ? dpi : Dpi::ForSystem();
    context.font = Dpi::CreateUiFont(context.dpi);

    BuildContent(context);

    for (const auto& entry : typed) {
        SetText(context, entry.first, entry.second);
    }

    ShowPage(context, page);
    UpdateEnabledStates(context);

    // The control that had the keyboard is gone: the tabs take it, as when the
    // window opens.
    if (context.tabs != nullptr) {
        ::SetFocus(context.tabs);
    }

    ::RedrawWindow(context.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// Lays the window out again over the controls it has, in the language it shows
// now: every control gets the text of that language and its place, the values
// stay as the user has left them, and so do the open page and the keyboard. The
// layout makes room for both languages, so the window keeps its size; a check
// box follows the length of its own text, and a field after a label of its own
// follows that label (see RowColumn). Nothing is drawn meanwhile: the screen
// keeps the old picture, and then the window is drawn once, whole. False when
// the controls do not answer to the layout: the window has to be built again
// then.
bool Relayout(Context& context) {
    const HWND window = context.window;
    const Page page = context.page;

    HideFieldTip(context);
    ::SendMessageW(window, WM_SETREDRAW, FALSE, 0);

    context.pageControls.clear();
    context.hintAreas.clear();
    context.relayout = Context::Relayout();
    context.relayout.active = true;

    BuildContent(context);

    context.relayout.active = false;

    if (context.relayout.failed || context.relayout.next != context.allControls.size()) {
        context.page = page;
        ::SendMessageW(window, WM_SETREDRAW, TRUE, 0);
        return false;
    }

    ShowPage(context, page);
    UpdateEnabledStates(context);

    ::SendMessageW(window, WM_SETREDRAW, TRUE, 0);
    ::SetWindowTextW(window, TextOf(context, Lang::Str::SettingsWindowTitle).c_str());

    // The size does not depend on the language; should it differ all the same,
    // it is set before the window is drawn.
    RECT current = {};
    const SIZE size = context.relayout.windowSize;

    if (::GetWindowRect(window, &current) != FALSE && size.cx > 0 && size.cy > 0 &&
        (size.cx != current.right - current.left || size.cy != current.bottom - current.top)) {
        ::SetWindowPos(
            window, nullptr, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }

    ::RedrawWindow(
        window, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return true;
}

Page PageOfIndex(int index) {
    switch (index) {
        case 1: return Page::Audio;
        case 2: return Page::Other;
        default: return Page::Window;
    }
}

void ShowPage(Context& context, Page page) {
    context.page = page;

    for (const auto& entry : context.pageControls) {
        if (entry.first != nullptr) {
            ::ShowWindow(entry.first, entry.second == page ? SW_SHOW : SW_HIDE);
        }
    }

    if (context.tabs != nullptr) {
        const int index = page == Page::Audio ? 1 : (page == Page::Other ? 2 : 0);

        // Setting the selection does not send TCN_SELCHANGE, so the pages do not
        // switch back and forth.
        if (static_cast<int>(::SendMessageW(context.tabs, TCM_GETCURSEL, 0, 0)) != index) {
            ::SendMessageW(context.tabs, TCM_SETCURSEL, static_cast<WPARAM>(index), 0);
        }
    }

    // A hidden field keeps no balloon, the range of the device is shown only
    // when it means something, and the descriptions of greyed out controls
    // follow the open page.
    HideFieldTip(context);
    UpdateHintVisibility(context);
    UpdateHintAreas(context);
}

// A list whose drop-down part is open keeps its keys: Ctrl+PgDn there is
// still a key of the list, not a change of the page.
bool IsOpenList(HWND control) {
    wchar_t className[16] = {};
    ::GetClassNameW(control, className, static_cast<int>(sizeof(className) / sizeof(className[0])));

    return ::lstrcmpiW(className, L"ComboBox") == 0 &&
        ::SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0) != FALSE;
}

// Ctrl+Tab and Ctrl+Shift+Tab, Ctrl+PgDn and Ctrl+PgUp switch the pages from
// any control of the window, the way the tabs of the system do it everywhere.
// The keys are taken before IsDialogMessageW, which would move the focus to the
// next control instead.
bool HandlePageKeys(Context& context, const MSG& message) {
    if (message.message != WM_KEYDOWN || context.window == nullptr || context.tabs == nullptr) {
        return false;
    }

    if (message.hwnd != context.window && ::IsChild(context.window, message.hwnd) == FALSE) {
        return false;
    }

    if (::GetKeyState(VK_CONTROL) >= 0 || IsOpenList(message.hwnd)) {
        return false;
    }

    int step = 0;

    if (message.wParam == VK_TAB) {
        step = ::GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
    } else if (message.wParam == VK_NEXT) {
        step = 1;
    } else if (message.wParam == VK_PRIOR) {
        step = -1;
    } else {
        return false;
    }

    const int count = static_cast<int>(::SendMessageW(context.tabs, TCM_GETITEMCOUNT, 0, 0));
    if (count <= 0) {
        return false;
    }

    const int current = static_cast<int>(::SendMessageW(context.tabs, TCM_GETCURSEL, 0, 0));
    const int next = ((current < 0 ? 0 : current) + step + count) % count;

    ShowPage(context, PageOfIndex(next));

    // A field of the page that has just been hidden cannot keep the keyboard:
    // the tabs take it, and the next Tab goes into the new page.
    const HWND focus = ::GetFocus();
    if (focus == nullptr || ::IsWindowVisible(focus) == FALSE) {
        ::SetFocus(context.tabs);
    }

    return true;
}

// The item of the list of the language.
int SelectedLanguage(Context& context) {
    const int index = SelectedIndex(context, Id::Language);
    return index >= 0 && index < static_cast<int>(LANGUAGE_COUNT) ? index : 0;
}

// The window takes the language of its list at once: the title, the tabs, the
// frames, the labels, the lists, the descriptions and the balloons (see
// Relayout). Whatever is typed, the open page and the keyboard stay, and the
// window keeps its size. Nothing happens when the choice means the language
// already shown ("As in Windows" on a Windows of that language), and nothing
// while the list is open: the user may still be going through it, and the
// window takes the language once it closes. The rest of the program takes the
// language with "Save".
void SwitchLanguage(Context& context, Id focus) {
    if (context.window == nullptr) {
        return;
    }

    const HWND list = Get(context, Id::Language);
    if (list != nullptr && IsOpenList(list)) {
        return;
    }

    const Lang::Language language = Lang::FromCode(LanguageCode(SelectedLanguage(context)));
    if (language == context.language) {
        return;
    }

    context.language = language;

    if (Relayout(context)) {
        return;
    }

    // The controls did not answer to the layout: the window is built again,
    // and the control that had the keyboard gets it back.
    ::SetWindowTextW(context.window, TextOf(context, Lang::Str::SettingsWindowTitle).c_str());
    Rebuild(context, context.dpi);

    if (const HWND control = Get(context, focus)) {
        ::SetFocus(control);
    }
}

// A checked field gets a value that the window takes from now on (the page
// builder, Reload).
void SetFieldText(Context& context, Id id, const std::wstring& text) {
    SetText(context, id, text);
    RememberValid(context, id, text);
}

// Reads every control back into a copy of the settings. The checked fields
// hold values the window has taken (see CheckAllFields); a value that still
// cannot be read (the window is being built again for another monitor) keeps
// the one of the settings.
void ReadControls(Context& context, Config::Settings& updated) {
    const Config::Settings& current = *context.settings;

    updated.application.language = LanguageCode(SelectedLanguage(context));
    updated.application.startWithWindows = IsChecked(context, Id::StartWithWindows);
    updated.application.startMinimizedToTray = IsChecked(context, Id::StartMinimizedToTray);
    updated.application.minimizeToTray = IsChecked(context, Id::MinimizeToTray);
    updated.application.closeButtonAction = ValueAt(CLOSE_ACTIONS, SelectedIndex(context, Id::CloseButtonAction));

    updated.tray.enabled = IsChecked(context, Id::TrayEnabled);
    updated.tray.notifications.onError = IsChecked(context, Id::NotificationError);
    updated.tray.notifications.onDeviceChange = IsChecked(context, Id::NotificationDeviceChange);
    updated.tray.notifications.onStateChange = IsChecked(context, Id::NotificationStateChange);

    updated.tray.menu.toggleEnabled = IsChecked(context, Id::TrayMenuToggle);
    updated.tray.menu.reinitialize = IsChecked(context, Id::TrayMenuReinit);
    updated.tray.menu.openLog = IsChecked(context, Id::TrayMenuLog);
    updated.tray.menu.diagnostics = IsChecked(context, Id::TrayMenuDiagnostics);

    updated.audio.dataFlow = ValueAt(DATA_FLOWS, SelectedIndex(context, Id::AudioDataFlow));
    updated.audio.buffer = ValueAt(BUFFER_MODES, SelectedIndex(context, Id::AudioBufferMode));

    // The size matters only for a fixed buffer. With the minimum buffer the
    // field is greyed out and keeps what it had; an empty one means "not set".
    const std::wstring framesText = ReadText(context, Id::AudioFixedBufferFrames, L"");
    long frames = 0;

    if (framesText.empty()) {
        updated.audio.fixedBufferFrames =
            updated.audio.buffer == Config::BufferMode::Fixed ? current.audio.fixedBufferFrames : 0u;
    } else if (ParseNumber(framesText, frames) && frames <= Config::FIXED_BUFFER_FRAMES_LIMITS.maximum) {
        updated.audio.fixedBufferFrames = static_cast<unsigned int>(frames);
    } else {
        updated.audio.fixedBufferFrames = current.audio.fixedBufferFrames;
    }

    updated.audio.reinit.defaultDeviceChanged = IsChecked(context, Id::ReinitDefaultDevice);
    updated.audio.reinit.resumeFromSleep = IsChecked(context, Id::ReinitResumeFromSleep);
    updated.audio.reinit.sessionUnlock = IsChecked(context, Id::ReinitSessionUnlock);
    updated.audio.reinit.failureTimeoutSec = ReadNumber(
        context, Id::ReinitFailureTimeout, Config::FAILURE_TIMEOUT_SEC_LIMITS, current.audio.reinit.failureTimeoutSec);
    updated.audio.reinit.debounceSec = ReadNumber(
        context, Id::ReinitDebounce, Config::DEBOUNCE_SEC_LIMITS, current.audio.reinit.debounceSec);

    updated.performance.processPriority = ValueAt(PRIORITIES, SelectedIndex(context, Id::ProcessPriority));

    updated.updates.checkOnStartup = IsChecked(context, Id::UpdateCheckOnStartup);

    const int levelIndex = SelectedIndex(context, Id::LogLevel);
    if (levelIndex >= 0 && levelIndex < static_cast<int>(LOG_LEVEL_COUNT)) {
        updated.logging.level = LOG_LEVELS[static_cast<size_t>(levelIndex)].value;
    }

    const std::wstring path = ReadText(context, Id::LogFilePath, L"");
    updated.logging.filePath =
        PathProblem(path, context.language).empty() ? Text::ToUtf8(path) : current.logging.filePath;

    updated.logging.maxFileSizeMb = ReadNumber(
        context, Id::LogMaxFileSize, Config::LOG_FILE_SIZE_MB_LIMITS, current.logging.maxFileSizeMb);
    updated.logging.maxFiles = ReadNumber(
        context, Id::LogMaxFiles, Config::LOG_FILES_LIMITS, current.logging.maxFiles);
}

// Writes a settings object into the controls. The file is the source of truth,
// so "Reload" brings its values into the window exactly like the page builder
// does when the window opens.
void ApplyToControls(Context& context, const Config::Settings& settings) {
    SetSelected(context, Id::Language, LanguageIndex(settings.application.language));
    SetChecked(context, Id::StartWithWindows, settings.application.startWithWindows);
    SetChecked(context, Id::StartMinimizedToTray, settings.application.startMinimizedToTray);
    SetChecked(context, Id::MinimizeToTray, settings.application.minimizeToTray);
    SetSelected(context, Id::CloseButtonAction, IndexOf(CLOSE_ACTIONS, settings.application.closeButtonAction));

    SetChecked(context, Id::TrayEnabled, settings.tray.enabled);
    SetChecked(context, Id::NotificationError, settings.tray.notifications.onError);
    SetChecked(context, Id::NotificationDeviceChange, settings.tray.notifications.onDeviceChange);
    SetChecked(context, Id::NotificationStateChange, settings.tray.notifications.onStateChange);

    SetChecked(context, Id::TrayMenuToggle, settings.tray.menu.toggleEnabled);
    SetChecked(context, Id::TrayMenuReinit, settings.tray.menu.reinitialize);
    SetChecked(context, Id::TrayMenuLog, settings.tray.menu.openLog);
    SetChecked(context, Id::TrayMenuDiagnostics, settings.tray.menu.diagnostics);

    SetSelected(context, Id::AudioDataFlow, IndexOf(DATA_FLOWS, settings.audio.dataFlow));
    SetSelected(context, Id::AudioBufferMode, IndexOf(BUFFER_MODES, settings.audio.buffer));
    SetFieldText(context, Id::AudioFixedBufferFrames, FixedBufferText(settings.audio.fixedBufferFrames));

    SetChecked(context, Id::ReinitDefaultDevice, settings.audio.reinit.defaultDeviceChanged);
    SetChecked(context, Id::ReinitResumeFromSleep, settings.audio.reinit.resumeFromSleep);
    SetChecked(context, Id::ReinitSessionUnlock, settings.audio.reinit.sessionUnlock);
    SetFieldText(context, Id::ReinitFailureTimeout, std::to_wstring(settings.audio.reinit.failureTimeoutSec));
    SetFieldText(context, Id::ReinitDebounce, std::to_wstring(settings.audio.reinit.debounceSec));

    SetSelected(context, Id::ProcessPriority, IndexOf(PRIORITIES, settings.performance.processPriority));

    SetChecked(context, Id::UpdateCheckOnStartup, settings.updates.checkOnStartup);

    SetSelected(context, Id::LogLevel, LogLevelIndex(settings.logging.level));
    SetFieldText(context, Id::LogFilePath, Text::ToWide(settings.logging.filePath));
    SetFieldText(context, Id::LogMaxFileSize, std::to_wstring(settings.logging.maxFileSizeMb));
    SetFieldText(context, Id::LogMaxFiles, std::to_wstring(settings.logging.maxFiles));
}

// A control whose value means nothing while its master switch is off is greyed
// out instead of staying usable: an ignored field looks like a broken one.
void UpdateEnabledStates(Context& context) {
    const bool tray = IsChecked(context, Id::TrayEnabled);

    // The items of the tray menu and the balloons exist only with the icon,
    // and the window can go to the tray only when there is an icon to bring it
    // back from: without it the buttons of the window do what they always do.
    const Id trayDependent[] = {
        Id::StartMinimizedToTray, Id::MinimizeToTray, Id::CloseButtonAction,
        Id::TrayMenuCaption, Id::TrayMenuToggle, Id::TrayMenuReinit, Id::TrayMenuLog, Id::TrayMenuDiagnostics,
        Id::NotificationError, Id::NotificationDeviceChange, Id::NotificationStateChange,
    };

    for (const Id id : trayDependent) {
        const HWND control = Get(context, id);
        if (control != nullptr) {
            ::EnableWindow(control, tray ? TRUE : FALSE);
        }
    }

    const bool fixedBuffer =
        ValueAt(BUFFER_MODES, SelectedIndex(context, Id::AudioBufferMode)) == Config::BufferMode::Fixed;

    const bool fileLog = !IsLogLevelOff(SelectedIndex(context, Id::LogLevel));

    const struct {
        Id id;
        bool enabled;
    } STATES[] = {
        { Id::AudioFixedBufferFrames, fixedBuffer },
        { Id::LogFilePath, fileLog },
        { Id::LogMaxFileSize, fileLog },
        { Id::LogMaxFiles, fileLog },
    };

    for (const auto& state : STATES) {
        const HWND control = Get(context, state.id);
        if (control != nullptr) {
            ::EnableWindow(control, state.enabled ? TRUE : FALSE);
        }
    }

    // The range of the device is not greyed out but hidden with the minimum
    // buffer, and a greyed out control keeps its description.
    UpdateHintVisibility(context);
    UpdateHintAreas(context);
}

// A fixed buffer starts from the buffer the device runs with: an empty field
// next to "Fixed" would only send the user to look the number up.
void PrefillFixedBuffer(Context& context) {
    if (ValueAt(BUFFER_MODES, SelectedIndex(context, Id::AudioBufferMode)) != Config::BufferMode::Fixed) {
        return;
    }

    const std::wstring text = ReadText(context, Id::AudioFixedBufferFrames, L"");
    if (!text.empty() && text != L"0") {
        return;
    }

    const BufferRange& range = context.bufferRange;
    const uint32_t frames = range.current > 0 ? range.current : range.minimum;

    if (frames > 0) {
        SetFieldText(context, Id::AudioFixedBufferFrames, std::to_wstring(frames));
    }
}

// Re-reads the settings file into the window. Nothing is applied here: "Save"
// is still the only button that writes the file and applies the settings.
void OnReload(Context& context) {
    HideFieldTip(context);

    // A value of the file that cannot be used does not replace the one of the
    // window: the reader keeps the value in use and says so.
    const Config::LoadResult result = Config::Load(context.settingsPath, *context.settings, context.language);

    if (!result.fileExists || result.parseFailed) {
        std::wstring text = TextOf(context, Lang::Str::SettingsReloadFailed);
        const std::wstring placeholder = L"{0}";
        const size_t position = text.find(placeholder);

        if (position != std::wstring::npos) {
            text.replace(position, placeholder.size(), context.settingsPath);
        }

        // The reason (a broken comma, a file locked by an editor) is what the
        // user needs to fix it.
        if (!result.error.empty()) {
            text += L"\n\n" + Text::ToWide(result.error);
        }

        ::MessageBoxW(
            context.window, text.c_str(), TextOf(context, Lang::Str::SettingsWindowTitle).c_str(),
            MB_OK | MB_ICONWARNING);
        return;
    }

    Config::Settings loaded = result.settings;
    // The layout of the window is the current one and the comments stay in the
    // language the window writes them in; everything else comes from the file.
    loaded.configVersion = Config::CONFIG_VERSION;
    loaded.commentLanguage = context.settings->commentLanguage;

    *context.settings = loaded;

    ApplyToControls(context, loaded);
    UpdateEnabledStates(context);

    if (!result.warnings.empty()) {
        std::wstring text = TextOf(context, Lang::Str::SettingsReloadWarnings);
        text += L"\n";

        for (const std::string& warning : result.warnings) {
            text += L"\n" + Text::ToWide(warning);
        }

        ::MessageBoxW(
            context.window, text.c_str(), TextOf(context, Lang::Str::SettingsWindowTitle).c_str(),
            MB_OK | MB_ICONWARNING);
    }

    // The file may choose another language: the window takes it after its list
    // of problems has been read in the language the window had.
    if (context.window != nullptr) {
        ::PostMessageW(context.window, WM_APP_LANGUAGE, static_cast<WPARAM>(ControlId(Id::Reload)), 0);
    }
}

// The page a control belongs to.
Page PageOf(const Context& context, HWND control) {
    for (const auto& entry : context.pageControls) {
        if (entry.first == control) {
            return entry.second;
        }
    }

    return context.page;
}

// Puts the last value the window took back into a field whose value it does
// not take, and shows why (the balloon is left out for the second and further
// fields of one check). A field that has no such value keeps its text and
// stays wrong: the balloon tells what it takes.
void RejectField(Context& context, const CheckedField& field, const std::wstring& problem, bool showTip) {
    const HWND control = Get(context, field.id);
    if (control == nullptr) {
        return;
    }

    std::wstring text = problem;
    const std::wstring restored = RestoreText(context, field.id);

    if (FieldProblem(context, field.id, restored).empty()) {
        // Before the balloon: a change of the text hides it.
        ::SetWindowTextW(control, restored.c_str());
        RememberValid(context, field.id, restored);

        text += L" " + Text::ToWide(fmt::format(
            Lang::Utf8(Lang::Str::SettingsValueRestored, context.language), Text::ToUtf8(restored)));
    }

    if (showTip) {
        ShowFieldTip(context, control, field.label, text);
    }
}

// Before the settings are saved - the only time the fields are checked: every
// value the window does not take is put back. The first such field is brought
// to the front with its balloon, and the window stays open so that the user
// sees what has changed. A greyed out field does not count (its value is not
// used).
bool CheckAllFields(Context& context) {
    bool rejected = false;

    for (const CheckedField& field : CHECKED_FIELDS) {
        const HWND control = Get(context, field.id);
        if (control == nullptr || ::IsWindowEnabled(control) == FALSE) {
            continue;
        }

        const std::wstring problem = FieldProblem(context, field.id, ReadText(context, field.id, L""));
        if (problem.empty()) {
            continue;
        }

        if (!rejected) {
            ShowPage(context, PageOf(context, control));
            ::SetFocus(control);
            ::SendMessageW(control, EM_SETSEL, 0, -1);
        }

        RejectField(context, field, problem, !rejected);
        rejected = true;
    }

    return !rejected;
}

void OnSave(Context& context) {
    HideFieldTip(context);

    if (!CheckAllFields(context)) {
        return;
    }

    Config::Settings updated = *context.settings;
    ReadControls(context, updated);

    // The service field travels with the settings: the comments are written in
    // the language they were written in before.
    updated.commentLanguage = context.settings->commentLanguage;
    *context.settings = updated;

    context.saved = true;

    if (context.window != nullptr) {
        ::DestroyWindow(context.window);
    }
}

void OnOpenFile(Context& context) {
    const HINSTANCE result = ::ShellExecuteW(
        context.window, L"open", context.settingsPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        ::MessageBoxW(
            context.window, TextOf(context, Lang::Str::SettingsOpenFileFailed).c_str(),
            TextOf(context, Lang::Str::SettingsWindowTitle).c_str(), MB_OK | MB_ICONWARNING);
    }
}

bool CopyToClipboard(HWND owner, const std::wstring& text) {
    // Another program may hold the clipboard for a moment.
    bool open = false;

    for (int attempt = 0; attempt < 5 && !open; ++attempt) {
        open = ::OpenClipboard(owner) != FALSE;

        if (!open) {
            ::Sleep(20);
        }
    }

    if (!open) {
        return false;
    }

    bool copied = false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);

    if (::EmptyClipboard() != FALSE) {
        const HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);

        if (memory != nullptr) {
            void* data = ::GlobalLock(memory);

            if (data != nullptr) {
                std::memcpy(data, text.c_str(), bytes);
                ::GlobalUnlock(memory);

                // The clipboard owns the memory once it has taken it.
                copied = ::SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            }

            if (!copied) {
                ::GlobalFree(memory);
            }
        }
    }

    ::CloseClipboard();
    return copied;
}

// A click on the path of the settings file: the folder of the file goes to the
// clipboard, without the name of the file - ready for the address bar of
// Explorer. A short note under the path confirms it, without a sound.
void CopySettingsFolder(Context& context) {
    const HWND path = Get(context, Id::SettingsPath);
    const std::wstring folder = Filesystem::GetDirectory(context.settingsPath);

    const std::wstring note = !folder.empty() && CopyToClipboard(context.window, folder)
        ? Text::ToWide(fmt::format(Lang::Utf8(Lang::Str::SettingsPathCopied, context.language), Text::ToUtf8(folder)))
        : TextOf(context, Lang::Str::SettingsPathCopyFailed);

    context.tip.Show(context.window, path, L"", note, context.Scale(TIP_MAX_WIDTH), COPY_TIP_DURATION_MS);
}

void OnCommand(Context& context, UINT id, UINT notification) {
    // IsDialogMessageW reports the default button and Escape as IDOK/IDCANCEL:
    // the window is not a dialog box, so the two are translated here.
    if (id == IDOK) {
        id = static_cast<UINT>(ControlId(Id::Save));
    } else if (id == IDCANCEL) {
        id = static_cast<UINT>(ControlId(Id::Cancel));
    }

    if (notification != BN_CLICKED) {
        return;
    }

    if (id == static_cast<UINT>(ControlId(Id::OpenFile))) {
        OnOpenFile(context);
        return;
    }

    // STN_CLICKED of the path is the same number as BN_CLICKED.
    if (id == static_cast<UINT>(ControlId(Id::SettingsPath))) {
        CopySettingsFolder(context);
        return;
    }

    if (id == static_cast<UINT>(ControlId(Id::Reload))) {
        OnReload(context);
        return;
    }

    if (id == static_cast<UINT>(ControlId(Id::Save))) {
        OnSave(context);
        return;
    }

    if (id == static_cast<UINT>(ControlId(Id::Cancel)) && context.window != nullptr) {
        ::DestroyWindow(context.window);
    }
}

LRESULT CALLBACK WindowProcedureThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

LRESULT WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<Context*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    if (context == nullptr) {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            const UINT notification = HIWORD(wParam);

            if (id == 0) {
                break;
            }

            // A checked field is checked with "Save" only (see CheckAllFields);
            // typing in it hides the balloon.
            if (FindCheckedField(id) != nullptr) {
                if (notification == EN_CHANGE) {
                    HideFieldTip(*context);
                }

                break;
            }

            // The window shows the chosen language once the notification is
            // over; while the list is open it waits for the list to close.
            if (id == static_cast<UINT>(ControlId(Id::Language)) &&
                (notification == CBN_SELCHANGE || notification == CBN_CLOSEUP)) {
                ::PostMessageW(window, WM_APP_LANGUAGE, static_cast<WPARAM>(id), 0);
                return 0;
            }

            // The value of some controls decides whether the rest of their
            // section can be used at all: the fields are greyed out accordingly.
            if (notification == BN_CLICKED || notification == CBN_SELCHANGE) {
                if (id == static_cast<UINT>(ControlId(Id::TrayEnabled)) ||
                    id == static_cast<UINT>(ControlId(Id::AudioBufferMode)) ||
                    id == static_cast<UINT>(ControlId(Id::LogLevel))) {
                    UpdateEnabledStates(*context);
                }

                if (id == static_cast<UINT>(ControlId(Id::AudioBufferMode))) {
                    PrefillFixedBuffer(*context);
                }
            }

            if (notification == BN_CLICKED || id == IDOK || id == IDCANCEL) {
                OnCommand(*context, id, notification);
                return 0;
            }

            break;
        }

        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);

            if (header != nullptr && header->hwndFrom == context->tabs && header->code == TCN_SELCHANGE) {
                const int selected = static_cast<int>(
                    ::SendMessageW(context->tabs, TCM_GETCURSEL, 0, 0));

                ShowPage(*context, PageOfIndex(selected));
                return 0;
            }

            break;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            // The controls of the pages paint their background with the brush
            // of the pages (see CreatePageBrush); a field that is greyed out or
            // read-only keeps the background of a field, and the buttons under
            // the tabs keep the grey of the window.
            const HDC dc = reinterpret_cast<HDC>(wParam);
            const HWND control = reinterpret_cast<HWND>(lParam);

            if (control == nullptr || !IsPageControl(*context, control) ||
                FindCheckedField(static_cast<UINT>(::GetDlgCtrlID(control))) != nullptr) {
                break;
            }

            const LRESULT standard = ::DefWindowProcW(window, message, wParam, lParam);

            // The range under the fixed buffer is a note, not a value: it is
            // written in the grey of the system.
            if (control == Get(*context, Id::AudioBufferHint)) {
                ::SetTextColor(dc, ::GetSysColor(COLOR_GRAYTEXT));
            }

            if (context->pageBrush == nullptr) {
                return standard;
            }

            ::SetBkMode(dc, TRANSPARENT);
            AlignPageBrush(*context, dc, control);
            return reinterpret_cast<LRESULT>(context->pageBrush);
        }

        case WM_ERASEBKGND:
            EraseBackground(*context, reinterpret_cast<HDC>(wParam));
            return 1;

        case WM_SETCURSOR:
            // The hand over the path of the settings file: it takes a click.
            if (reinterpret_cast<HWND>(wParam) == Get(*context, Id::SettingsPath) && LOWORD(lParam) == HTCLIENT) {
                ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }

            break;

        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            // The tab control takes the change as well: the picture of its
            // display area is made again once it has.
            ::PostMessageW(window, WM_APP_PAGE_BRUSH, 0, 0);
            break;

        case WM_APP_PAGE_BRUSH:
            CreatePageBrush(*context);
            ::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            return 0;

        case WM_APP_LANGUAGE:
            SwitchLanguage(*context, static_cast<Id>(static_cast<int>(wParam)));
            return 0;

        case WM_TIMER:
            if (context->tip.OnTimer(static_cast<UINT_PTR>(wParam))) {
                return 0;
            }

            break;

        case WM_ACTIVATE:
            // The balloon belongs to the window that has the keyboard.
            if (LOWORD(wParam) == WA_INACTIVE) {
                HideFieldTip(*context);
            }

            break;

        case WM_MOVE:
            HideFieldTip(*context);
            break;

        case WM_DPICHANGED: {
            // The window is on another monitor: the system suggests the position
            // and the size for the new scale, and the content is built again.
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                ::SetWindowPos(
                    window, nullptr, suggested->left, suggested->top,
                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }

            Rebuild(*context, HIWORD(wParam));
            return 0;
        }

        case WM_CLOSE:
            if (context->window != nullptr) {
                ::DestroyWindow(window);
            }

            return 0;

        case WM_DESTROY:
            // The balloon and the descriptions are owned by the window and go
            // with it.
            context->tip.Forget();
            context->hints = nullptr;
            context->hintAreas.clear();
            context->window = nullptr;
            context->done = true;
            return 0;

        default:
            break;
    }

    return ::DefWindowProcW(window, message, wParam, lParam);
}

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

}

bool miniant::Windows::ShowSettingsWindow(
    HWND owner,
    HINSTANCE instance,
    Config::Settings& settings,
    const std::wstring& settingsPath,
    const BufferRange& bufferRange) {
    Context context;
    context.instance = instance;
    context.settings = &settings;
    context.settingsPath = settingsPath;
    context.bufferRange = bufferRange;
    context.language = Lang::Current();
    context.dpi = Dpi::ForWindow(owner);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WindowProcedureThunk;
    windowClass.hInstance = instance;
    windowClass.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON1));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    // The usual background of a window of the system: the settings are drawn by
    // the controls themselves, the window has nothing of its own to paint.
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = SETTINGS_CLASS_NAME;

    if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;

    RECT desired = { 0, 0, context.Scale(WINDOW_WIDTH), context.Scale(WINDOW_HEIGHT) };
    Dpi::AdjustWindowRect(desired, style, context.dpi);

    // Near the window of the program, not in the middle of the screen.
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;

    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;

    PlaceNearOwner(owner, width, height, x, y);

    // Opened from the tray while the main window is hidden, the window gets a
    // button on the taskbar of its own: an owned window has none, and behind
    // other windows it could not be found again.
    const bool ownerVisible = owner != nullptr && ::IsWindowVisible(owner) != FALSE && ::IsIconic(owner) == FALSE;
    const DWORD extendedStyle = WS_EX_CONTROLPARENT | (ownerVisible ? 0 : WS_EX_APPWINDOW);

    const HWND window = ::CreateWindowExW(
        extendedStyle,
        SETTINGS_CLASS_NAME,
        TextOf(context, Lang::Str::SettingsWindowTitle).c_str(),
        style,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        &context);

    if (window == nullptr) {
        ::UnregisterClassW(SETTINGS_CLASS_NAME, instance);
        return false;
    }

    // The scale of the monitor the window has really opened on: without the
    // main window on screen it is not necessarily the monitor of the owner.
    context.dpi = Dpi::ForWindow(window);

    context.font = Dpi::CreateUiFont(context.dpi);

    InitCommonControlsOnce();

    BuildContent(context);

    // The window has got its real size: it is placed near the main window again,
    // so that it is not pulled up for the height it was created with.
    RECT fitted = {};
    if (::GetWindowRect(window, &fitted) != FALSE) {
        PlaceNearOwner(owner, fitted.right - fitted.left, fitted.bottom - fitted.top, x, y);

        if (x != CW_USEDEFAULT && y != CW_USEDEFAULT) {
            ::SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    ShowPage(context, Page::Window);

    // The dependent fields are greyed out before the window is shown, so that
    // the first thing the user sees is the real state of the settings.
    UpdateEnabledStates(context);

    // Modal for the main window: it takes no commands while the settings are
    // being edited, but the tray icon and the audio stream keep working. The
    // state of the owner is given back as it was: another modal window of the
    // program may have disabled it before.
    const bool ownerWasEnabled = owner != nullptr && ::IsWindowEnabled(owner) != FALSE;

    if (owner != nullptr) {
        ::EnableWindow(owner, FALSE);
    }

    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);

    // The keyboard starts on the tabs: the arrows switch the pages, Tab goes
    // through the fields of the page and then to the buttons.
    if (context.tabs != nullptr) {
        ::SetFocus(context.tabs);
    }

    MSG message = {};
    bool quit = false;

    while (!context.done) {
        const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);

        if (result <= 0) {
            // 0 is WM_QUIT: the window of the program was closed while the
            // settings were open. The message belongs to the loop of the
            // program, so it is put back and read there.
            quit = result == 0;
            break;
        }

        if (HandlePageKeys(context, message)) {
            continue;
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
        ::EnableWindow(owner, ownerWasEnabled ? TRUE : FALSE);

        if (ownerWasEnabled && ::IsWindowVisible(owner) != FALSE) {
            ::SetForegroundWindow(owner);
        }
    }

    if (context.window != nullptr && ::IsWindow(context.window) != FALSE) {
        ::DestroyWindow(context.window);
    }

    if (context.font != nullptr) {
        ::DeleteObject(context.font);
    }

    DeletePageBrush(context);

    ::UnregisterClassW(SETTINGS_CLASS_NAME, instance);

    return context.saved;
}

void miniant::Windows::ActivateSettingsWindow() {
    HWND window = nullptr;

    // The class name is the same in every copy of the program: only a window of
    // this process counts.
    while ((window = ::FindWindowExW(nullptr, window, SETTINGS_CLASS_NAME, nullptr)) != nullptr) {
        DWORD process = 0;
        ::GetWindowThreadProcessId(window, &process);

        if (process == ::GetCurrentProcessId()) {
            if (::IsIconic(window) != FALSE) {
                ::ShowWindow(window, SW_RESTORE);
            }

            ::SetForegroundWindow(window);
            return;
        }
    }
}
