#include "SettingsWindow.h"

#include "BalloonTip.h"
#include "Dpi.h"
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
const int HEADER_STEP = 30;
const int GROUP_GAP = 12;
// The space between the longest label and the fields next to it.
const int LABEL_GAP = 16;
const int FIELD_WIDTH = 220;
// A list is as wide as its longest item in either language plus what the list
// draws around it: the borders, the margins of the text and the arrow button.
const int COMBO_CHROME = 30;
// A row of choices ("Language:" and its buttons): the space after the caption
// and between two buttons.
const int CHOICE_CAPTION_GAP = 10;
const int CHOICE_GAP = 16;
// The longest path the field of the log file takes.
const int MAX_PATH_LENGTH = 1024;
const int PAGE_TOP = 62;
// The buttons have the size of every push button of the program (see
// StandardButtonWidth): the width is known once the font is.
const int BUTTON_HEIGHT = STANDARD_BUTTON_HEIGHT;
const int BUTTON_GAP = 8;
// The part of the first row of buttons that is at least left for the path of
// the settings file; a longer path loses its middle.
const int MINIMUM_PATH_WIDTH = 160;

// The two rows of buttons at the bottom of the window, the space between them
// and the tabs, and the space between the lowest control of a page and the
// bottom border of the tabs.
const int FRAME_HEIGHT = 2 * BUTTON_HEIGHT + BUTTON_GAP;
const int FRAME_GAP = 8;
const int PAGE_BOTTOM_PADDING = 8;

// The air between the border of the tabs and the controls of a page, in real
// pixels: the same on the left and on the right.
const int PAGE_AIR = 2;

// A header is measured in real pixels and rounded to design ones: a few pixels
// of reserve keep its last letter from being cut off.
const int HEADER_SLACK = 4;

// A value that the window does not take is put back, and a balloon at its
// field says why: it disappears by itself after a while, or when the user
// types, switches the page or leaves the window. The check of a field runs
// after the keyboard has really moved on (see WM_APP_CHECK_FIELD).
const UINT_PTR TIP_TIMER_ID = 1;
const UINT TIP_DURATION_MS = 6000;
const int TIP_MAX_WIDTH = 320;
const UINT WM_APP_CHECK_FIELD = WM_APP + 1;

// Every control of the window has a number: the values are read back by it, so
// a control that is not on screen at the moment (another page is open) keeps
// its state.
enum class Id : int {
    LanguageAuto = 1000,
    LanguageEnglish,
    LanguageRussian,
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
    ReinitDeviceState,
    ReinitDeviceAdded,
    ReinitDeviceRemoved,
    ReinitResumeFromSleep,
    ReinitSessionUnlock,
    ReinitEnableWhenDisabled,
    ReinitFailureTimeout,
    ReinitDebounce,

    ProcessPriority,

    UpdateCheckOnStartup,

    LogLevel,
    LogFilePath,
    LogMaxFileSize,
    LogMaxFiles,

    // The frame of the window: the tab control and the buttons belong to no
    // page, they stay on screen all the time.
    Tabs,
    SettingsPath,
    OpenFile,
    Reload,
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

    // The dots per inch of the monitor the window is on: every coordinate below
    // is a design pixel of a 96 DPI layout and is scaled by CreateControl.
    UINT dpi = 96;

    int Scale(int value) const {
        return Dpi::Scale(value, dpi);
    }

    // Every control of the window: the pages use the list to show and hide
    // themselves, and a change of the DPI uses it to build the window again.
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

    // The top of the two rows of buttons in design pixels: right under the
    // tabs, whose height follows the tallest page.
    int frameTop = WINDOW_HEIGHT - MARGIN - FRAME_HEIGHT;

    // The width of the client area in design pixels: it follows the widest
    // row of the pages (see FitWindowToContent).
    int windowWidth = WINDOW_WIDTH;

    // The right edge of the widest control of the pages in design pixels.
    int contentRight = 0;

    // The column of the labels in front of the lists and the fields, and the
    // width of a button: both follow the texts of the current language.
    int labelWidth = 220;
    int buttonWidth = 75;

    bool saved = false;
    bool done = false;

    HFONT font = nullptr;
    HFONT headerFont = nullptr;
    HFONT captionFont = nullptr;

    // The last value of every checked field that the window took: a value it
    // does not take is replaced with it. At first it is the value in use.
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

// The buttons of the language, in the order of their identifiers
// (LanguageAuto, LanguageEnglish, LanguageRussian).
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

Id LanguageId(int index) {
    return static_cast<Id>(static_cast<int>(Id::LanguageAuto) + index);
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

// The labels in front of the lists and the fields. The column is as wide as
// the longest of them in the language of the window, plus a gap: a label
// added to a page has to be added here as well.
const Lang::Str FIELD_LABELS[] = {
    Lang::Str::SettingsCloseAction,
    Lang::Str::SettingsDataFlow,
    Lang::Str::SettingsBuffer,
    Lang::Str::SettingsFixedBufferFrames,
    Lang::Str::SettingsReinitFailureTimeout,
    Lang::Str::SettingsReinitDebounce,
    Lang::Str::SettingsProcessPriority,
    Lang::Str::SettingsLogLevel,
    Lang::Str::SettingsLogFilePath,
    Lang::Str::SettingsLogMaxFileSize,
    Lang::Str::SettingsLogMaxFiles,
};

int LabelColumnWidth(Context& context) {
    int widest = 0;

    for (const Lang::Str label : FIELD_LABELS) {
        widest = (std::max)(widest, DesignTextWidth(context, context.font, Lang::Wide(label)));
    }

    return widest > 0 ? widest + LABEL_GAP : 220;
}

// A control is never wider than its own text: a click far to the right of a
// caption does nothing, and the caption is the only thing that reacts. It is
// not wider than the page either, whatever the length of a translation.
int CheckWidth(Context& context, const std::wstring& text, int x) {
    const int available = context.pageRight - x;
    const int textWidth = DesignTextWidth(context, context.font, text);

    return textWidth > 0 ? (std::min)(textWidth + CHECK_GLYPH_WIDTH, available) : available;
}

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
    const HWND control = ::CreateWindowExW(
        extendedStyle,
        className,
        text.c_str(),
        style | WS_CHILD | WS_VISIBLE,
        context.Scale(x) + context.offsetX,
        context.Scale(y) + context.offsetY,
        context.Scale(width),
        context.Scale(height),
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

// A header is as wide as its own text: a label up to the edge of the window
// painted its background over the right border of the tabs.
int AddHeader(Context& context, Lang::Str text, int y) {
    const std::wstring caption = Lang::Wide(text);
    const int available = context.pageRight - MARGIN;
    const int textWidth = DesignTextWidth(
        context, context.headerFont != nullptr ? context.headerFont : context.font, caption);
    const int width = textWidth > 0 ? (std::min)(textWidth + HEADER_SLACK, available) : available;

    const HWND label = CreateControl(
        context, L"STATIC", caption, SS_LEFT | SS_CENTERIMAGE, static_cast<Id>(0),
        MARGIN, y, width, ROW_HEIGHT);

    if (label != nullptr && context.headerFont != nullptr) {
        ::SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(context.headerFont), TRUE);
    }

    BindToPage(context, label);
    ExtendContent(context, MARGIN + width);
    return y + HEADER_STEP;
}

// A check box at the left edge of the page. WS_GROUP in extraStyle starts a
// new group of controls (see AddChoiceRow).
int AddCheck(Context& context, Id id, Lang::Str text, bool value, int y, DWORD extraStyle = 0) {
    const std::wstring caption = Lang::Wide(text);
    const int x = MARGIN;
    const int width = CheckWidth(context, caption, x);

    const HWND check = CreateControl(
        context, L"BUTTON", caption, BS_AUTOCHECKBOX | WS_TABSTOP | extraStyle, id,
        x, y, width, ROW_HEIGHT);

    if (check != nullptr) {
        ::SendMessageW(check, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    BindToPage(context, check);
    ExtendContent(context, x + width);
    return y + ROW_STEP;
}

// The caption of a group of controls inside a section ("Icon menu items:"):
// as bold as a header, but of the size of the rows and with their step.
int AddCaption(Context& context, Id id, Lang::Str text, int y) {
    const std::wstring caption = Lang::Wide(text);
    const HFONT font = context.captionFont != nullptr ? context.captionFont : context.font;
    const int available = context.pageRight - MARGIN;
    const int textWidth = DesignTextWidth(context, font, caption);
    const int width = textWidth > 0 ? (std::min)(textWidth + HEADER_SLACK, available) : available;

    const HWND label = CreateControl(
        context, L"STATIC", caption, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, id,
        MARGIN, y, width, ROW_HEIGHT);

    if (label != nullptr && context.captionFont != nullptr) {
        ::SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(context.captionFont), TRUE);
    }

    BindToPage(context, label);
    ExtendContent(context, MARGIN + width);
    return y + ROW_STEP;
}

HWND Get(Context& context, Id id);

// Tab enters a group of choices at its selected button, as in every dialog
// box of the system: only that button is a tab stop.
void UpdateChoiceTabStop(Context& context, Id firstId, size_t count) {
    bool anyChecked = false;

    for (size_t i = 0; i < count; ++i) {
        const HWND button = Get(context, static_cast<Id>(static_cast<int>(firstId) + static_cast<int>(i)));
        anyChecked = anyChecked || (button != nullptr && ::SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }

    for (size_t i = 0; i < count; ++i) {
        const HWND button = Get(context, static_cast<Id>(static_cast<int>(firstId) + static_cast<int>(i)));
        if (button == nullptr) {
            continue;
        }

        const bool stop = anyChecked ? ::SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED : i == 0;
        const LONG_PTR style = ::GetWindowLongPtrW(button, GWL_STYLE);
        ::SetWindowLongPtrW(
            button, GWL_STYLE, stop ? (style | WS_TABSTOP) : (style & ~static_cast<LONG_PTR>(WS_TABSTOP)));
    }
}

// A row of choices right after its caption, outside the column of the labels:
// "Language:  (o) As in Windows  ( ) English  ( ) Russian". The buttons have
// consecutive identifiers from firstId and are one group: the arrows move
// between them, and the control after the row starts the next group.
int AddChoiceRow(
    Context& context, Id firstId, Lang::Str label, const std::vector<Lang::Str>& texts, int selected, int y) {
    const std::wstring caption = Lang::Wide(label);
    const int captionWidth = DesignTextWidth(context, context.font, caption) + HEADER_SLACK;

    BindToPage(context, CreateControl(
        context, L"STATIC", caption, SS_LEFT | SS_CENTERIMAGE, static_cast<Id>(0),
        MARGIN, y, captionWidth, ROW_HEIGHT));

    int x = MARGIN + captionWidth + CHOICE_CAPTION_GAP;

    for (size_t i = 0; i < texts.size(); ++i) {
        const std::wstring text = Lang::Wide(texts[i]);
        const int width = CheckWidth(context, text, x);
        const Id id = static_cast<Id>(static_cast<int>(firstId) + static_cast<int>(i));

        const HWND button = CreateControl(
            context, L"BUTTON", text, BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0), id,
            x, y, width, ROW_HEIGHT);

        if (button != nullptr && static_cast<int>(i) == selected) {
            ::SendMessageW(button, BM_SETCHECK, BST_CHECKED, 0);
        }

        BindToPage(context, button);
        ExtendContent(context, x + width);
        x += width + CHOICE_GAP;
    }

    UpdateChoiceTabStop(context, firstId, texts.size());
    return y + ROW_STEP;
}

// A list is as wide as its longest item in either language: a change of the
// language moves nothing, and no list is wider than what it shows.
int ComboWidth(Context& context, const std::vector<Lang::Str>& texts) {
    int widest = 0;

    for (const Lang::Str text : texts) {
        for (const Lang::Language language : { Lang::Language::English, Lang::Language::Russian }) {
            widest = (std::max)(widest, DesignTextWidth(context, context.font, Lang::Wide(text, language)));
        }
    }

    return widest > 0 ? widest + COMBO_CHROME : FIELD_WIDTH;
}

int AddCombo(Context& context, Id id, Lang::Str label, const std::vector<Lang::Str>& texts, int selected, int y) {
    BindToPage(context, CreateControl(
        context, L"STATIC", Lang::Wide(label), SS_LEFT | SS_CENTERIMAGE, static_cast<Id>(0),
        MARGIN, y, context.labelWidth, ROW_HEIGHT));

    const int width = ComboWidth(context, texts);

    const HWND combo = CreateControl(
        context, L"COMBOBOX", L"",
        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, id,
        MARGIN + context.labelWidth, y, width, ROW_HEIGHT * 8);

    ExtendContent(context, MARGIN + context.labelWidth + width);

    // A list belongs to its page like every other control: without this the
    // lists of all three pages would be drawn in the same place at once.
    BindToPage(context, combo);

    if (combo != nullptr) {
        for (const Lang::Str text : texts) {
            ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Lang::Wide(text).c_str()));
        }

        if (selected >= 0 && selected < static_cast<int>(texts.size())) {
            ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        }
    }

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
    int maxLength, bool digitsOnly = false) {
    BindToPage(context, CreateControl(
        context, L"STATIC", Lang::Wide(label), SS_LEFT | SS_CENTERIMAGE, static_cast<Id>(0),
        MARGIN, y, context.labelWidth, ROW_HEIGHT));

    const HWND edit = CreateControl(
        context, L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL | (digitsOnly ? ES_NUMBER : 0), id,
        MARGIN + context.labelWidth, y, width, ROW_HEIGHT, WS_EX_CLIENTEDGE);

    if (edit != nullptr && maxLength > 0) {
        ::SendMessageW(edit, EM_LIMITTEXT, static_cast<WPARAM>(maxLength), 0);
    }

    BindToPage(context, edit);
    RememberValid(context, id, value);

    ExtendContent(context, MARGIN + context.labelWidth + width);
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

int AddNumber(
    Context& context, Id id, Lang::Str label, const std::wstring& value, const Config::NumberLimits& limits, int y) {
    return AddEdit(context, id, label, value, FIELD_WIDTH, y, DigitCount(limits.maximum), true);
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

    const int length = ::GetWindowTextLengthW(control);
    if (length <= 0) {
        return fallback;
    }

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = ::GetWindowTextW(control, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<size_t>(copied) : 0);

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
// frames and in milliseconds. Empty while no device has been seen.
std::wstring BufferHintText(const Context& context) {
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
        Lang::Utf8(Lang::Str::SettingsBufferHint),
        range.minimum,
        range.maximum,
        BufferStep(range),
        Lang::Decimal(milliseconds(range.minimum)),
        Lang::Decimal(milliseconds(range.maximum))));
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
    { Id::ReinitFailureTimeout, Config::FAILURE_TIMEOUT_MS_LIMITS },
    { Id::ReinitDebounce, Config::DEBOUNCE_MS_LIMITS },
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
        return FixedBufferProblem(context.bufferRange, text);
    }

    if (id == Id::LogFilePath) {
        return PathProblem(text);
    }

    for (const NumberField& field : NUMBER_FIELDS) {
        if (field.id == id) {
            return NumberProblem(text, field.limits);
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

    int y = PAGE_TOP;

    y = AddHeader(context, Lang::Str::SettingsHeaderApplication, y);

    // The language as a row of buttons right after its caption: in the column
    // of the labels a list stood far away from the word "Language".
    y = AddChoiceRow(context, Id::LanguageAuto, Lang::Str::SettingsLanguage, Texts(LANGUAGES),
        LanguageIndex(settings.application.language), y);

    // WS_GROUP: the buttons of the language end here, the arrows stay in them.
    y = AddCheck(context, Id::StartWithWindows, Lang::Str::SettingsStartWithWindows,
        settings.application.startWithWindows, y, WS_GROUP);
    y = AddCheck(context, Id::StartMinimizedToTray, Lang::Str::SettingsStartMinimized,
        settings.application.startMinimizedToTray, y);
    y = AddCheck(context, Id::MinimizeToTray, Lang::Str::SettingsMinimizeToTray,
        settings.application.minimizeToTray, y);
    y = AddCombo(context, Id::CloseButtonAction, Lang::Str::SettingsCloseAction, Texts(CLOSE_ACTIONS),
        IndexOf(CLOSE_ACTIONS, settings.application.closeButtonAction), y);

    // The icon and the items of its menu that can be hidden. The items have a
    // caption of their own and are greyed out with it while the icon is off.
    // The status line, "Settings" and "Exit" are always in the menu, so they
    // have no switch here.
    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderTray, y);
    y = AddCheck(context, Id::TrayEnabled, Lang::Str::SettingsTrayEnabled, settings.tray.enabled, y);
    y = AddCaption(context, Id::TrayMenuCaption, Lang::Str::SettingsTrayMenuCaption, y);
    y = AddCheck(context, Id::TrayMenuToggle, Lang::Str::TrayToggleEnabled, settings.tray.menu.toggleEnabled, y);
    y = AddCheck(context, Id::TrayMenuReinit, Lang::Str::TrayReinitialize, settings.tray.menu.reinitialize, y);
    y = AddCheck(context, Id::TrayMenuLog, Lang::Str::TrayLog, settings.tray.menu.openLog, y);
    y = AddCheck(context, Id::TrayMenuDiagnostics, Lang::Str::TrayDiagnostics, settings.tray.menu.diagnostics, y);

    // The balloons are shown by the tray icon: without it they are greyed out
    // as well.
    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderNotifications, y);
    y = AddCheck(context, Id::NotificationError, Lang::Str::SettingsNotifyError,
        settings.tray.notifications.onError, y);
    y = AddCheck(context, Id::NotificationDeviceChange, Lang::Str::SettingsNotifyDeviceChange,
        settings.tray.notifications.onDeviceChange, y);
    y = AddCheck(context, Id::NotificationStateChange, Lang::Str::SettingsNotifyStateChange,
        settings.tray.notifications.onStateChange, y);

    return y;
}

int BuildAudioPage(Context& context) {
    context.page = Page::Audio;

    const Config::Settings& settings = *context.settings;

    int y = PAGE_TOP;

    y = AddHeader(context, Lang::Str::SettingsHeaderAudio, y);
    y = AddCombo(context, Id::AudioDataFlow, Lang::Str::SettingsDataFlow, Texts(DATA_FLOWS),
        IndexOf(DATA_FLOWS, settings.audio.dataFlow), y);
    y = AddCombo(context, Id::AudioBufferMode, Lang::Str::SettingsBuffer, Texts(BUFFER_MODES),
        IndexOf(BUFFER_MODES, settings.audio.buffer), y);
    y = AddNumber(context, Id::AudioFixedBufferFrames, Lang::Str::SettingsFixedBufferFrames,
        FixedBufferText(settings.audio.fixedBufferFrames), Config::FIXED_BUFFER_FRAMES_LIMITS, y);

    // The range of the device under the name of the field: the values the
    // fixed buffer takes, so that nobody has to look them up in the report of
    // the diagnostics. It is shown for the fixed buffer only, and its row
    // stays when it is hidden: nothing below moves with the choice of the
    // buffer (see UpdateHintVisibility).
    {
        const std::wstring hint = BufferHintText(context);
        const int textWidth = hint.empty() ? 0 : DesignTextWidth(context, context.font, hint);
        const int width = textWidth > 0
            ? (std::min)(textWidth + HEADER_SLACK, context.pageRight - MARGIN)
            : context.labelWidth;

        BindToPage(context, CreateControl(
            context, L"STATIC", hint, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, Id::AudioBufferHint,
            MARGIN, y, width, ROW_HEIGHT));

        ExtendContent(context, MARGIN + width);
        y += ROW_STEP;
    }

    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderReinit, y);
    y = AddCheck(context, Id::ReinitDefaultDevice, Lang::Str::SettingsReinitDeviceChanged,
        settings.audio.reinit.defaultDeviceChanged, y);
    y = AddCheck(context, Id::ReinitDeviceState, Lang::Str::SettingsReinitDeviceState,
        settings.audio.reinit.deviceStateChanged, y);
    y = AddCheck(context, Id::ReinitDeviceAdded, Lang::Str::SettingsReinitDeviceAdded,
        settings.audio.reinit.deviceAdded, y);
    y = AddCheck(context, Id::ReinitDeviceRemoved, Lang::Str::SettingsReinitDeviceRemoved,
        settings.audio.reinit.deviceRemoved, y);
    y = AddCheck(context, Id::ReinitResumeFromSleep, Lang::Str::SettingsReinitResume,
        settings.audio.reinit.resumeFromSleep, y);
    y = AddCheck(context, Id::ReinitSessionUnlock, Lang::Str::SettingsReinitUnlock,
        settings.audio.reinit.sessionUnlock, y);
    y = AddCheck(context, Id::ReinitEnableWhenDisabled, Lang::Str::SettingsReinitEnableWhenDisabled,
        settings.audio.reinit.enableWhenDisabled, y);
    y = AddNumber(context, Id::ReinitFailureTimeout, Lang::Str::SettingsReinitFailureTimeout,
        std::to_wstring(settings.audio.reinit.failureTimeoutMs), Config::FAILURE_TIMEOUT_MS_LIMITS, y);
    y = AddNumber(context, Id::ReinitDebounce, Lang::Str::SettingsReinitDebounce,
        std::to_wstring(settings.audio.reinit.debounceMs), Config::DEBOUNCE_MS_LIMITS, y);

    return y;
}

int BuildOtherPage(Context& context) {
    context.page = Page::Other;

    const Config::Settings& settings = *context.settings;

    int y = PAGE_TOP;

    y = AddHeader(context, Lang::Str::SettingsHeaderPerformance, y);
    y = AddCombo(context, Id::ProcessPriority, Lang::Str::SettingsProcessPriority, Texts(PRIORITIES),
        IndexOf(PRIORITIES, settings.performance.processPriority), y);

    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderUpdates, y);
    y = AddCheck(context, Id::UpdateCheckOnStartup, Lang::Str::SettingsCheckOnStartup,
        settings.updates.checkOnStartup, y);

    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderLog, y);
    y = AddCombo(context, Id::LogLevel, Lang::Str::SettingsLogLevel, Texts(LOG_LEVELS),
        LogLevelIndex(settings.logging.level), y);
    y = AddEdit(context, Id::LogFilePath, Lang::Str::SettingsLogFilePath,
        Text::ToWide(settings.logging.filePath), FIELD_WIDTH, y, MAX_PATH_LENGTH);
    y = AddNumber(context, Id::LogMaxFileSize, Lang::Str::SettingsLogMaxFileSize,
        std::to_wstring(settings.logging.maxFileSizeMb), Config::LOG_FILE_SIZE_MB_LIMITS, y);
    y = AddNumber(context, Id::LogMaxFiles, Lang::Str::SettingsLogMaxFiles,
        std::to_wstring(settings.logging.maxFiles), Config::LOG_FILES_LIMITS, y);

    return y;
}

// The tab control of the system: it draws the tabs itself, so they can never
// overlap each other or the page. The pages are ordinary controls of the window
// that are placed inside the display area of the tabs and are shown one page at
// a time.
void CreateTabs(Context& context) {
    const struct {
        Lang::Str text;
    } ITEMS[] = {
        { Lang::Str::SettingsTabWindow },
        { Lang::Str::SettingsTabAudio },
        { Lang::Str::SettingsTabOther },
    };

    const int top = MARGIN;
    // A provisional bottom: FitWindowToContent sets the real one when the pages
    // are built and their height is known.
    const int bottom = WINDOW_HEIGHT - MARGIN - FRAME_HEIGHT - FRAME_GAP;

    context.tabs = CreateControl(
        context, WC_TABCONTROLW, L"",
        WS_TABSTOP | TCS_TABS, Id::Tabs,
        MARGIN, top, WINDOW_WIDTH - 2 * MARGIN, bottom - top);

    if (context.tabs == nullptr) {
        return;
    }

    for (size_t i = 0; i < sizeof(ITEMS) / sizeof(ITEMS[0]); ++i) {
        const std::wstring text = Lang::Wide(ITEMS[i].text);

        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(text.c_str());

        ::SendMessageW(context.tabs, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&item));
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
    context.tip.Show(context.window, field, Lang::Wide(title), text, context.Scale(TIP_MAX_WIDTH), TIP_DURATION_MS);
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
// tabs end a little below and to the right of the controls of the pages, the
// two rows of buttons follow right under them. Everything is counted in real
// pixels of the current DPI, so the window fits again on another monitor.
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

    // The labels of the tabs fit as well...
    const int count = static_cast<int>(::SendMessageW(context.tabs, TCM_GETITEMCOUNT, 0, 0));
    RECT lastTab = {};
    if (count > 0 &&
        ::SendMessageW(context.tabs, TCM_GETITEMRECT, static_cast<WPARAM>(count - 1), reinterpret_cast<LPARAM>(&lastTab)) != FALSE) {
        width = (std::max)(width, static_cast<int>(lastTab.right) + context.Scale(4));
    }

    // ...and so do the two buttons of the first row and a part of the path.
    width = (std::max)(width, context.Scale(2 * context.buttonWidth + 2 * BUTTON_GAP + MINIMUM_PATH_WIDTH));

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

    ::SetWindowPos(
        context.window, nullptr, 0, 0, window.right - window.left, window.bottom - window.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// The frame of the window: the buttons under the tabs.
void BuildFrame(Context& context) {
    // Two rows: the file, the button that reads it again and the path of the
    // file on the first one, the buttons that end the window on the second one.
    // In one row the path was overlapped by the buttons next to it. Every
    // button has the same size.
    const int firstRowY = context.frameTop;
    const int secondRowY = firstRowY + BUTTON_HEIGHT + BUTTON_GAP;
    const int buttonWidth = context.buttonWidth;
    const int right = context.windowWidth - MARGIN;

    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsOpenFile),
        BS_PUSHBUTTON | WS_TABSTOP, Id::OpenFile,
        MARGIN, firstRowY, buttonWidth, BUTTON_HEIGHT);

    // Re-reads the file without closing the window: a value edited in a text
    // editor gets in, and "Save" is still what writes and applies it.
    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsReload),
        BS_PUSHBUTTON | WS_TABSTOP, Id::Reload,
        MARGIN + buttonWidth + BUTTON_GAP, firstRowY, buttonWidth, BUTTON_HEIGHT);

    // The path starts right after "Reload" and takes the rest of the row.
    const int pathX = MARGIN + 2 * (buttonWidth + BUTTON_GAP);

    // A long path loses its middle, not its end: the name of the file stays
    // visible.
    CreateControl(
        context, L"STATIC", context.settingsPath, SS_LEFT | SS_CENTERIMAGE | SS_PATHELLIPSIS,
        Id::SettingsPath, pathX, firstRowY, (std::max)(0, right - pathX), BUTTON_HEIGHT);

    // Created from left to right, so that Tab walks the row in reading order.
    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsCancel),
        BS_PUSHBUTTON | WS_TABSTOP, Id::Cancel,
        right - 2 * buttonWidth - BUTTON_GAP, secondRowY, buttonWidth, BUTTON_HEIGHT);

    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsSave),
        BS_DEFPUSHBUTTON | WS_TABSTOP, Id::Save,
        right - buttonWidth, secondRowY, buttonWidth, BUTTON_HEIGHT);
}

// Every control of the window for the current DPI. The order of creation is
// the order of Tab: the tabs, the fields of the page, the buttons of the frame
// last. The frame is built after the pages because it is placed under the
// tabs, whose height follows the tallest page.
void BuildContent(Context& context) {
    // The sizes that follow the texts of the current language and font.
    context.labelWidth = LabelColumnWidth(context);
    context.buttonWidth = StandardButtonWidth(context.font, context.dpi);
    context.contentRight = 0;

    CreateTabs(context);

    const int windowBottom = BuildWindowPage(context);
    const int audioBottom = BuildAudioPage(context);
    const int otherBottom = BuildOtherPage(context);

    FitWindowToContent(context, (std::max)({ windowBottom, audioBottom, otherBottom }));

    ResetPageOffset(context);
    BuildFrame(context);
}

// Builds the content again for another DPI. The design coordinates never
// change, CreateControl scales them: the window of one monitor keeps the
// layout that was approved, only larger or smaller.
void Rebuild(Context& context, UINT dpi) {
    // Whatever the user has already typed is read back first: the controls are
    // destroyed and built again, and a typed value must not be lost because the
    // window was moved to another monitor. A value that cannot be used stays as
    // it is in the settings; the window shows it again.
    Config::Settings edited = *context.settings;
    ReadControls(context, edited);
    *context.settings = edited;

    // The balloon points at a field that is about to be destroyed.
    HideFieldTip(context);

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
    context.tabs = nullptr;

    if (context.font != nullptr) {
        ::DeleteObject(context.font);
    }

    if (context.headerFont != nullptr) {
        ::DeleteObject(context.headerFont);
    }

    if (context.captionFont != nullptr) {
        ::DeleteObject(context.captionFont);
    }

    context.dpi = dpi != 0 ? dpi : Dpi::ForSystem();
    context.font = Dpi::CreateUiFont(context.dpi);
    context.headerFont = Dpi::CreateHeaderFont(context.dpi);
    context.captionFont = Dpi::CreateCaptionFont(context.dpi);

    BuildContent(context);

    ShowPage(context, page);
    UpdateEnabledStates(context);

    // The control that had the keyboard is gone: the tabs take it, as when the
    // window opens.
    if (context.tabs != nullptr) {
        ::SetFocus(context.tabs);
    }
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

    // A hidden field keeps no balloon, and the range of the device is shown
    // only when it means something.
    HideFieldTip(context);
    UpdateHintVisibility(context);
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

// The button of the language that is on.
int SelectedLanguage(Context& context) {
    for (size_t i = 0; i < LANGUAGE_COUNT; ++i) {
        if (IsChecked(context, LanguageId(static_cast<int>(i)))) {
            return static_cast<int>(i);
        }
    }

    return 0;
}

void SetLanguage(Context& context, int index) {
    for (size_t i = 0; i < LANGUAGE_COUNT; ++i) {
        SetChecked(context, LanguageId(static_cast<int>(i)), static_cast<int>(i) == index);
    }

    UpdateChoiceTabStop(context, Id::LanguageAuto, LANGUAGE_COUNT);
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
    updated.audio.reinit.deviceStateChanged = IsChecked(context, Id::ReinitDeviceState);
    updated.audio.reinit.deviceAdded = IsChecked(context, Id::ReinitDeviceAdded);
    updated.audio.reinit.deviceRemoved = IsChecked(context, Id::ReinitDeviceRemoved);
    updated.audio.reinit.resumeFromSleep = IsChecked(context, Id::ReinitResumeFromSleep);
    updated.audio.reinit.sessionUnlock = IsChecked(context, Id::ReinitSessionUnlock);
    updated.audio.reinit.enableWhenDisabled = IsChecked(context, Id::ReinitEnableWhenDisabled);
    updated.audio.reinit.failureTimeoutMs = ReadNumber(
        context, Id::ReinitFailureTimeout, Config::FAILURE_TIMEOUT_MS_LIMITS, current.audio.reinit.failureTimeoutMs);
    updated.audio.reinit.debounceMs = ReadNumber(
        context, Id::ReinitDebounce, Config::DEBOUNCE_MS_LIMITS, current.audio.reinit.debounceMs);

    updated.performance.processPriority = ValueAt(PRIORITIES, SelectedIndex(context, Id::ProcessPriority));

    updated.updates.checkOnStartup = IsChecked(context, Id::UpdateCheckOnStartup);

    const int levelIndex = SelectedIndex(context, Id::LogLevel);
    if (levelIndex >= 0 && levelIndex < static_cast<int>(LOG_LEVEL_COUNT)) {
        updated.logging.level = LOG_LEVELS[static_cast<size_t>(levelIndex)].value;
    }

    const std::wstring path = ReadText(context, Id::LogFilePath, L"");
    updated.logging.filePath = PathProblem(path).empty() ? Text::ToUtf8(path) : current.logging.filePath;

    updated.logging.maxFileSizeMb = ReadNumber(
        context, Id::LogMaxFileSize, Config::LOG_FILE_SIZE_MB_LIMITS, current.logging.maxFileSizeMb);
    updated.logging.maxFiles = ReadNumber(
        context, Id::LogMaxFiles, Config::LOG_FILES_LIMITS, current.logging.maxFiles);
}

// Writes a settings object into the controls. The file is the source of truth,
// so "Reload" brings its values into the window exactly like the page builder
// does when the window opens.
void ApplyToControls(Context& context, const Config::Settings& settings) {
    SetLanguage(context, LanguageIndex(settings.application.language));
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
    SetChecked(context, Id::ReinitDeviceState, settings.audio.reinit.deviceStateChanged);
    SetChecked(context, Id::ReinitDeviceAdded, settings.audio.reinit.deviceAdded);
    SetChecked(context, Id::ReinitDeviceRemoved, settings.audio.reinit.deviceRemoved);
    SetChecked(context, Id::ReinitResumeFromSleep, settings.audio.reinit.resumeFromSleep);
    SetChecked(context, Id::ReinitSessionUnlock, settings.audio.reinit.sessionUnlock);
    SetChecked(context, Id::ReinitEnableWhenDisabled, settings.audio.reinit.enableWhenDisabled);
    SetFieldText(context, Id::ReinitFailureTimeout, std::to_wstring(settings.audio.reinit.failureTimeoutMs));
    SetFieldText(context, Id::ReinitDebounce, std::to_wstring(settings.audio.reinit.debounceMs));

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
    // buffer.
    UpdateHintVisibility(context);
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
    const Config::LoadResult result = Config::Load(context.settingsPath, *context.settings);

    if (!result.fileExists || result.parseFailed) {
        std::wstring text = Lang::Wide(Lang::Str::SettingsReloadFailed);
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
            context.window, text.c_str(), Lang::Wide(Lang::Str::SettingsWindowTitle).c_str(),
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
        std::wstring text = Lang::Wide(Lang::Str::SettingsReloadWarnings);
        text += L"\n";

        for (const std::string& warning : result.warnings) {
            text += L"\n" + Text::ToWide(warning);
        }

        ::MessageBoxW(
            context.window, text.c_str(), Lang::Wide(Lang::Str::SettingsWindowTitle).c_str(),
            MB_OK | MB_ICONWARNING);
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

        text += L" " + Text::ToWide(fmt::format(Lang::Utf8(Lang::Str::SettingsValueRestored), Text::ToUtf8(restored)));
    }

    if (showTip) {
        ShowFieldTip(context, control, field.label, text);
    }
}

// A field the keyboard has left (see WM_APP_CHECK_FIELD): a value the window
// does not take is put back at once, a value it takes is remembered.
void CheckField(Context& context, const CheckedField& field) {
    const HWND control = Get(context, field.id);
    if (control == nullptr || ::IsWindowEnabled(control) == FALSE || ::IsWindowVisible(control) == FALSE) {
        return;
    }

    const std::wstring text = ReadText(context, field.id, L"");
    const std::wstring problem = FieldProblem(context, field.id, text);

    if (problem.empty()) {
        RememberValid(context, field.id, text);
        return;
    }

    RejectField(context, field, problem, true);
}

// Before the settings are saved: every value the window does not take is put
// back. The first such field is brought to the front with its balloon, and
// the window stays open so that the user sees what has changed. A greyed out
// field does not count (its value is not used).
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
            context.window, Lang::Wide(Lang::Str::SettingsOpenFileFailed).c_str(),
            Lang::Wide(Lang::Str::SettingsWindowTitle).c_str(), MB_OK | MB_ICONWARNING);
    }
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

            // A checked field: its value is checked once the keyboard has
            // really moved on (the new focus is known only then), and typing
            // in it hides the balloon.
            if (FindCheckedField(id) != nullptr) {
                if (notification == EN_KILLFOCUS) {
                    ::PostMessageW(window, WM_APP_CHECK_FIELD, static_cast<WPARAM>(id), 0);
                } else if (notification == EN_CHANGE) {
                    HideFieldTip(*context);
                }

                break;
            }

            // Tab enters the buttons of the language at the selected one.
            if (notification == BN_CLICKED &&
                id >= static_cast<UINT>(ControlId(Id::LanguageAuto)) &&
                id < static_cast<UINT>(ControlId(Id::LanguageAuto)) + LANGUAGE_COUNT) {
                UpdateChoiceTabStop(*context, Id::LanguageAuto, LANGUAGE_COUNT);
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

        case WM_CTLCOLORSTATIC: {
            // The range under the fixed buffer is a note, not a value: it is
            // written in the grey of the system.
            const HWND control = reinterpret_cast<HWND>(lParam);

            if (control != nullptr && control == Get(*context, Id::AudioBufferHint)) {
                const LRESULT brush = ::DefWindowProcW(window, message, wParam, lParam);
                ::SetTextColor(reinterpret_cast<HDC>(wParam), ::GetSysColor(COLOR_GRAYTEXT));
                return brush;
            }

            break;
        }

        case WM_APP_CHECK_FIELD: {
            const CheckedField* field = FindCheckedField(static_cast<UINT>(wParam));
            const HWND focus = ::GetFocus();

            // Nothing is checked when the keyboard went to another program, came
            // back to the field, or went to Save (it checks every field itself)
            // or Cancel (the values are dropped).
            const bool inWindow = focus != nullptr && (focus == window || ::IsChild(window, focus) != FALSE);

            if (field != nullptr && inWindow &&
                focus != Get(*context, field->id) &&
                focus != Get(*context, Id::Save) &&
                focus != Get(*context, Id::Cancel)) {
                CheckField(*context, *field);
            }

            return 0;
        }

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
            // The balloon is owned by the window and goes with it.
            context->tip.Forget();
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
        Lang::Wide(Lang::Str::SettingsWindowTitle).c_str(),
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
    context.headerFont = Dpi::CreateHeaderFont(context.dpi);
    context.captionFont = Dpi::CreateCaptionFont(context.dpi);

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

    if (context.headerFont != nullptr) {
        ::DeleteObject(context.headerFont);
    }

    if (context.captionFont != nullptr) {
        ::DeleteObject(context.captionFont);
    }

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
