#include "SettingsWindow.h"

#include "Dpi.h"

#include "../../res/resource.h"
#include "../Lang.h"
#include "../Text.h"
#include "TextMetrics.h"
#include "WindowPlacement.h"

#include <commctrl.h>
#include <shellapi.h>

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace miniant;
using namespace miniant::Windows;

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
// A check box that depends on the one above it (an item of the tray menu
// under "Show the tray icon") starts under the caption of that one.
const int CHECK_INDENT = 20;
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
    // check of its value (see CheckFixedBuffer).
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

template <typename Enum, size_t N>
std::vector<std::wstring> Texts(const Choice<Enum> (&choices)[N]) {
    std::vector<std::wstring> items;
    items.reserve(N);

    for (size_t i = 0; i < N; ++i) {
        items.push_back(Lang::Wide(choices[i].text));
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

int LanguageIndex(const std::string& code) {
    if (code == "en" || code == "english") {
        return 1;
    }

    if (code == "ru" || code == "russian") {
        return 2;
    }

    return 0;
}

const char* LanguageCode(int index) {
    switch (index) {
        case 1: return "en";
        case 2: return "ru";
        default: return "auto";
    }
}

std::vector<std::wstring> LanguageTexts() {
    return {
        Lang::Wide(Lang::Str::SettingsLanguageAuto),
        Lang::Wide(Lang::Str::SettingsLanguageEnglish),
        Lang::Wide(Lang::Str::SettingsLanguageRussian),
    };
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
    Lang::Str::SettingsLanguage,
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

// The indent moves a check box that depends on the one above it under the
// caption of that one.
int AddCheck(Context& context, Id id, Lang::Str text, bool value, int y, int indent = 0) {
    const std::wstring caption = Lang::Wide(text);
    const int x = MARGIN + indent;
    const int width = CheckWidth(context, caption, x);

    const HWND check = CreateControl(
        context, L"BUTTON", caption, BS_AUTOCHECKBOX | WS_TABSTOP, id,
        x, y, width, ROW_HEIGHT);

    if (check != nullptr) {
        ::SendMessageW(check, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    BindToPage(context, check);
    ExtendContent(context, x + width);
    return y + ROW_STEP;
}

int AddCombo(Context& context, Id id, Lang::Str label, const std::vector<std::wstring>& items, int selected, int y) {
    BindToPage(context, CreateControl(
        context, L"STATIC", Lang::Wide(label), SS_LEFT | SS_CENTERIMAGE, static_cast<Id>(0),
        MARGIN, y, context.labelWidth, ROW_HEIGHT));

    // The same width as the fields: a list does not need the whole window, and
    // a row of controls of different lengths looks ragged.
    const HWND combo = CreateControl(
        context, L"COMBOBOX", L"",
        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, id,
        MARGIN + context.labelWidth, y, FIELD_WIDTH, ROW_HEIGHT * 8);

    ExtendContent(context, MARGIN + context.labelWidth + FIELD_WIDTH);

    // A list belongs to its page like every other control: without this the
    // lists of all three pages would be drawn in the same place at once.
    BindToPage(context, combo);

    if (combo != nullptr) {
        for (const std::wstring& item : items) {
            ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        }

        if (selected >= 0 && selected < static_cast<int>(items.size())) {
            ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        }
    }

    return y + ROW_STEP;
}

int AddEdit(Context& context, Id id, Lang::Str label, const std::wstring& value, int width, int y) {
    BindToPage(context, CreateControl(
        context, L"STATIC", Lang::Wide(label), SS_LEFT | SS_CENTERIMAGE, static_cast<Id>(0),
        MARGIN, y, context.labelWidth, ROW_HEIGHT));

    BindToPage(context, CreateControl(
        context, L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL, id,
        MARGIN + context.labelWidth, y, width, ROW_HEIGHT, WS_EX_CLIENTEDGE));

    ExtendContent(context, MARGIN + context.labelWidth + width);
    return y + ROW_STEP;
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

// A number from an edit control. A value outside the allowed range, or a text
// that is not a number at all, keeps the field as it was and is reported to the
// user instead of being written to the file.
int ReadInt(Context& context, Id id, int minimum, int maximum, int fallback, bool& valid) {
    const std::wstring text = ReadText(context, id, L"");
    if (text.empty()) {
        valid = true;
        return fallback;
    }

    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || value < minimum || value > maximum) {
        valid = false;
        return fallback;
    }

    valid = true;
    return static_cast<int>(value);
}

void ReportInvalid(std::wstring& fields, const std::wstring& text) {
    if (!fields.empty()) {
        fields += L", ";
    }

    fields += text;
}

void ReportInvalid(std::wstring& fields, Lang::Str label) {
    ReportInvalid(fields, Lang::Wide(label));
}

// The step of the grid of the device; a driver that reports none accepts every
// value of its range.
uint32_t BufferStep(const BufferRange& range) {
    return range.step > 0 ? range.step : 1;
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

// A fixed buffer has to be a value the device accepts: inside its range and on
// its grid (a multiple of the step). Returns an empty text when the value fits
// or the range is not known yet, otherwise the text of the message with the
// range and the nearest values that fit.
std::wstring CheckFixedBuffer(const Context& context, uint32_t frames) {
    const BufferRange& range = context.bufferRange;
    if (range.minimum == 0) {
        return {};
    }

    const uint32_t step = BufferStep(range);
    const std::string label = Lang::Utf8(Lang::Str::SettingsFixedBufferFrames);
    const std::string device = Text::ToUtf8(
        range.deviceName.empty() ? Lang::Wide(Lang::Str::UnknownDevice) : range.deviceName);

    if (frames < range.minimum || frames > range.maximum) {
        const uint32_t nearest = frames < range.minimum ? range.minimum : range.maximum;

        return Text::ToWide(fmt::format(
            Lang::Utf8(Lang::Str::SettingsBufferOutOfRange),
            label, device, range.minimum, range.maximum, step, nearest));
    }

    if (frames % step != 0) {
        const uint32_t lower = (std::max)(range.minimum, frames - frames % step);
        const uint32_t upper = (std::min)(range.maximum, lower + step);

        return Text::ToWide(fmt::format(
            Lang::Utf8(Lang::Str::SettingsBufferNotOnStep),
            label, device, range.minimum, range.maximum, step, lower, upper));
    }

    return {};
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
    y = AddCombo(context, Id::Language, Lang::Str::SettingsLanguage, LanguageTexts(),
        LanguageIndex(settings.application.language), y);
    y = AddCheck(context, Id::StartWithWindows, Lang::Str::SettingsStartWithWindows,
        settings.application.startWithWindows, y);
    y = AddCheck(context, Id::StartMinimizedToTray, Lang::Str::SettingsStartMinimized,
        settings.application.startMinimizedToTray, y);
    y = AddCheck(context, Id::MinimizeToTray, Lang::Str::SettingsMinimizeToTray,
        settings.application.minimizeToTray, y);
    y = AddCombo(context, Id::CloseButtonAction, Lang::Str::SettingsCloseAction, Texts(CLOSE_ACTIONS),
        IndexOf(CLOSE_ACTIONS, settings.application.closeButtonAction), y);

    // The icon and the items of its menu that can be hidden. The items depend
    // on the icon: they stand under its caption and are greyed out while it is
    // off. The status line, "Settings" and "Exit" are always in the menu, so
    // they have no switch here.
    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderTray, y);
    y = AddCheck(context, Id::TrayEnabled, Lang::Str::SettingsTrayEnabled, settings.tray.enabled, y);
    y = AddCheck(context, Id::TrayMenuToggle, Lang::Str::TrayToggleEnabled,
        settings.tray.menu.toggleEnabled, y, CHECK_INDENT);
    y = AddCheck(context, Id::TrayMenuReinit, Lang::Str::TrayReinitialize,
        settings.tray.menu.reinitialize, y, CHECK_INDENT);
    y = AddCheck(context, Id::TrayMenuLog, Lang::Str::TrayLog,
        settings.tray.menu.openLog, y, CHECK_INDENT);
    y = AddCheck(context, Id::TrayMenuDiagnostics, Lang::Str::TrayDiagnostics,
        settings.tray.menu.diagnostics, y, CHECK_INDENT);

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
    y = AddEdit(context, Id::AudioFixedBufferFrames, Lang::Str::SettingsFixedBufferFrames,
        FixedBufferText(settings.audio.fixedBufferFrames), FIELD_WIDTH, y);

    // The range of the device under the field: the values the fixed buffer
    // accepts, so that nobody has to look them up in the diagnostics report.
    const std::wstring hint = BufferHintText(context);
    if (!hint.empty()) {
        const int x = MARGIN + context.labelWidth;
        const int textWidth = DesignTextWidth(context, context.font, hint);
        const int width = textWidth > 0 ? textWidth + HEADER_SLACK : FIELD_WIDTH;

        BindToPage(context, CreateControl(
            context, L"STATIC", hint, SS_LEFT | SS_CENTERIMAGE, Id::AudioBufferHint,
            x, y, width, ROW_HEIGHT));

        ExtendContent(context, x + width);
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
    y = AddEdit(context, Id::ReinitFailureTimeout, Lang::Str::SettingsReinitFailureTimeout,
        std::to_wstring(settings.audio.reinit.failureTimeoutMs), FIELD_WIDTH, y);
    y = AddEdit(context, Id::ReinitDebounce, Lang::Str::SettingsReinitDebounce,
        std::to_wstring(settings.audio.reinit.debounceMs), FIELD_WIDTH, y);

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
        Text::ToWide(settings.logging.filePath), FIELD_WIDTH, y);
    y = AddEdit(context, Id::LogMaxFileSize, Lang::Str::SettingsLogMaxFileSize,
        std::to_wstring(settings.logging.maxFileSizeMb), FIELD_WIDTH, y);
    y = AddEdit(context, Id::LogMaxFiles, Lang::Str::SettingsLogMaxFiles,
        std::to_wstring(settings.logging.maxFiles), FIELD_WIDTH, y);

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

// The parts of the window live further down the file; the rebuild uses them.
void ShowPage(Context& context, Page page);
bool ReadControls(Context& context, Config::Settings& updated, std::wstring& invalidFields);
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
    std::wstring invalidFields;
    ReadControls(context, edited, invalidFields);
    *context.settings = edited;

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

    context.dpi = dpi != 0 ? dpi : Dpi::ForSystem();
    context.font = Dpi::CreateUiFont(context.dpi);
    context.headerFont = Dpi::CreateHeaderFont(context.dpi);

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

// Reads every control back into a copy of the settings. Returns false when a
// value cannot be used: the window then stays open and points at the field.
bool ReadControls(Context& context, Config::Settings& updated, std::wstring& invalidFields) {
    const Config::Settings& current = *context.settings;

    updated.application.language = LanguageCode(SelectedIndex(context, Id::Language));
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

    bool valid = true;

    // The size matters only for a fixed buffer: then it has to be a number the
    // device accepts. With the minimum buffer the field is greyed out and keeps
    // what it had.
    const bool framesEmpty = ReadText(context, Id::AudioFixedBufferFrames, L"").empty();
    const int maximumFrames = static_cast<int>(Config::FIXED_BUFFER_FRAMES_MAX);

    if (updated.audio.buffer == Config::BufferMode::Fixed) {
        const int frames = ReadInt(context, Id::AudioFixedBufferFrames, 1, maximumFrames, 0, valid);

        if (!valid || framesEmpty) {
            ReportInvalid(invalidFields, Lang::Str::SettingsFixedBufferFrames);
        } else {
            const std::wstring problem = CheckFixedBuffer(context, static_cast<uint32_t>(frames));

            if (problem.empty()) {
                updated.audio.fixedBufferFrames = static_cast<unsigned int>(frames);
            } else {
                ReportInvalid(invalidFields, problem);
            }
        }
    } else {
        const int frames = ReadInt(
            context, Id::AudioFixedBufferFrames, 0, maximumFrames,
            static_cast<int>(current.audio.fixedBufferFrames), valid);

        if (valid) {
            updated.audio.fixedBufferFrames = framesEmpty ? 0u : static_cast<unsigned int>(frames);
        }
    }

    updated.audio.reinit.defaultDeviceChanged = IsChecked(context, Id::ReinitDefaultDevice);
    updated.audio.reinit.deviceStateChanged = IsChecked(context, Id::ReinitDeviceState);
    updated.audio.reinit.deviceAdded = IsChecked(context, Id::ReinitDeviceAdded);
    updated.audio.reinit.deviceRemoved = IsChecked(context, Id::ReinitDeviceRemoved);
    updated.audio.reinit.resumeFromSleep = IsChecked(context, Id::ReinitResumeFromSleep);
    updated.audio.reinit.sessionUnlock = IsChecked(context, Id::ReinitSessionUnlock);
    updated.audio.reinit.enableWhenDisabled = IsChecked(context, Id::ReinitEnableWhenDisabled);

    updated.audio.reinit.failureTimeoutMs = ReadInt(
        context, Id::ReinitFailureTimeout, Config::FAILURE_TIMEOUT_MS_MIN, Config::FAILURE_TIMEOUT_MS_MAX,
        current.audio.reinit.failureTimeoutMs, valid);
    if (!valid) {
        ReportInvalid(invalidFields, Lang::Str::SettingsReinitFailureTimeout);
    }

    updated.audio.reinit.debounceMs = ReadInt(
        context, Id::ReinitDebounce, Config::DEBOUNCE_MS_MIN, Config::DEBOUNCE_MS_MAX,
        current.audio.reinit.debounceMs, valid);
    if (!valid) {
        ReportInvalid(invalidFields, Lang::Str::SettingsReinitDebounce);
    }

    updated.performance.processPriority = ValueAt(PRIORITIES, SelectedIndex(context, Id::ProcessPriority));

    updated.updates.checkOnStartup = IsChecked(context, Id::UpdateCheckOnStartup);

    const int levelIndex = SelectedIndex(context, Id::LogLevel);
    if (levelIndex >= 0 && levelIndex < static_cast<int>(LOG_LEVEL_COUNT)) {
        updated.logging.level = LOG_LEVELS[static_cast<size_t>(levelIndex)].value;
    }

    updated.logging.filePath = Text::ToUtf8(
        ReadText(context, Id::LogFilePath, Text::ToWide(current.logging.filePath)));

    updated.logging.maxFileSizeMb = ReadInt(
        context, Id::LogMaxFileSize, Config::LOG_FILE_SIZE_MB_MIN, Config::LOG_FILE_SIZE_MB_MAX,
        current.logging.maxFileSizeMb, valid);
    if (!valid) {
        ReportInvalid(invalidFields, Lang::Str::SettingsLogMaxFileSize);
    }

    updated.logging.maxFiles = ReadInt(
        context, Id::LogMaxFiles, Config::LOG_FILES_MIN, Config::LOG_FILES_MAX, current.logging.maxFiles, valid);
    if (!valid) {
        ReportInvalid(invalidFields, Lang::Str::SettingsLogMaxFiles);
    }

    return invalidFields.empty();
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
    SetText(context, Id::AudioFixedBufferFrames, FixedBufferText(settings.audio.fixedBufferFrames));

    SetChecked(context, Id::ReinitDefaultDevice, settings.audio.reinit.defaultDeviceChanged);
    SetChecked(context, Id::ReinitDeviceState, settings.audio.reinit.deviceStateChanged);
    SetChecked(context, Id::ReinitDeviceAdded, settings.audio.reinit.deviceAdded);
    SetChecked(context, Id::ReinitDeviceRemoved, settings.audio.reinit.deviceRemoved);
    SetChecked(context, Id::ReinitResumeFromSleep, settings.audio.reinit.resumeFromSleep);
    SetChecked(context, Id::ReinitSessionUnlock, settings.audio.reinit.sessionUnlock);
    SetChecked(context, Id::ReinitEnableWhenDisabled, settings.audio.reinit.enableWhenDisabled);
    SetText(context, Id::ReinitFailureTimeout, std::to_wstring(settings.audio.reinit.failureTimeoutMs));
    SetText(context, Id::ReinitDebounce, std::to_wstring(settings.audio.reinit.debounceMs));

    SetSelected(context, Id::ProcessPriority, IndexOf(PRIORITIES, settings.performance.processPriority));

    SetChecked(context, Id::UpdateCheckOnStartup, settings.updates.checkOnStartup);

    SetSelected(context, Id::LogLevel, LogLevelIndex(settings.logging.level));
    SetText(context, Id::LogFilePath, Text::ToWide(settings.logging.filePath));
    SetText(context, Id::LogMaxFileSize, std::to_wstring(settings.logging.maxFileSizeMb));
    SetText(context, Id::LogMaxFiles, std::to_wstring(settings.logging.maxFiles));
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
        Id::TrayMenuToggle, Id::TrayMenuReinit, Id::TrayMenuLog, Id::TrayMenuDiagnostics,
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
        { Id::AudioBufferHint, fixedBuffer },
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
        SetText(context, Id::AudioFixedBufferFrames, std::to_wstring(frames));
    }
}

// Re-reads the settings file into the window. Nothing is applied here: "Save"
// is still the only button that writes the file and applies the settings.
void OnReload(Context& context) {
    const Config::LoadResult result = Config::Load(context.settingsPath);

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
}

// "Check these values: {0}" without a formatting library: the text of the
// message is the only place where a placeholder is filled by hand.
std::wstring InvalidValuesText(const std::wstring& fields) {
    std::wstring text = Lang::Wide(Lang::Str::SettingsInvalidValues);

    const std::wstring placeholder = L"{0}";
    const size_t position = text.find(placeholder);
    if (position != std::wstring::npos) {
        text.replace(position, placeholder.size(), fields);
    }

    return text;
}

void OnSave(Context& context) {
    Config::Settings updated = *context.settings;
    std::wstring invalidFields;

    if (!ReadControls(context, updated, invalidFields)) {
        ::MessageBoxW(
            context.window, InvalidValuesText(invalidFields).c_str(),
            Lang::Wide(Lang::Str::SettingsWindowTitle).c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

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
