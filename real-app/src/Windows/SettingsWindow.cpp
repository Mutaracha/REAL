#include "SettingsWindow.h"

#include "Dpi.h"

#include "../../res/resource.h"
#include "../Lang.h"
#include "../Text.h"
#include "TextMetrics.h"
#include "WindowPlacement.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace miniant;
using namespace miniant::Windows;

namespace {

const wchar_t SETTINGS_CLASS_NAME[] = L"REAL.SettingsWindow";

const int WINDOW_WIDTH = 680;
// The window is created with this height; once the pages are built it gets the
// height of the tallest one (see FitWindowToContent).
const int WINDOW_HEIGHT = 660;

const int MARGIN = 14;
const int ROW_HEIGHT = 24;
const int ROW_STEP = 24;
const int HEADER_STEP = 30;
const int GROUP_GAP = 12;
const int LABEL_WIDTH = 220;
const int FIELD_WIDTH = 220;
const int PAGE_TOP = 62;
const int BUTTON_HEIGHT = 30;
const int BUTTON_WIDTH = 120;
const int BUTTON_GAP = 8;

// The two rows of buttons at the bottom of the window, the space between them
// and the tabs, and the space between the lowest control of a page and the
// bottom border of the tabs.
const int FRAME_HEIGHT = 2 * BUTTON_HEIGHT + BUTTON_GAP;
const int FRAME_GAP = 8;
const int PAGE_BOTTOM_PADDING = 8;

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
    SingleInstance,

    NotificationError,
    NotificationDeviceChange,
    NotificationStateChange,

    TrayEnabled,
    TrayMenuStatus,
    TrayMenuToggle,
    TrayMenuReinit,
    TrayMenuLog,
    TrayMenuDiagnostics,
    TrayMenuStartWithWindows,
    TrayMenuAbout,
    TrayMenuExit,

    AudioEnabledOnStartup,
    AudioDataFlow,
    AudioPeriodSelection,
    AudioRequestedPeriodFrames,
    AudioAllowPeriodSnap,

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
    DisablePowerThrottling,

    HotkeysEnabled,
    HotkeyToggle,
    HotkeyReinitialize,

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

const Choice<Config::PeriodSelection> PERIODS[] = {
    { Config::PeriodSelection::Minimum, Lang::Str::SettingsPeriodMinimum },
    { Config::PeriodSelection::Fundamental, Lang::Str::SettingsPeriodFundamental },
    { Config::PeriodSelection::Fixed, Lang::Str::SettingsPeriodFixed },
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
    return y + HEADER_STEP;
}

int AddCheck(Context& context, Id id, Lang::Str text, bool value, int y) {
    const std::wstring caption = Lang::Wide(text);

    const HWND check = CreateControl(
        context, L"BUTTON", caption, BS_AUTOCHECKBOX | WS_TABSTOP, id,
        MARGIN, y, CheckWidth(context, caption, MARGIN), ROW_HEIGHT);

    if (check != nullptr) {
        ::SendMessageW(check, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    BindToPage(context, check);
    return y + ROW_STEP;
}

void AddCheckColumn(Context& context, Id id, Lang::Str text, bool value, int x, int y) {
    const std::wstring caption = Lang::Wide(text);

    const HWND check = CreateControl(
        context, L"BUTTON", caption, BS_AUTOCHECKBOX | WS_TABSTOP, id,
        x, y, CheckWidth(context, caption, x), ROW_HEIGHT);

    if (check != nullptr) {
        ::SendMessageW(check, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    BindToPage(context, check);
}

int AddCombo(Context& context, Id id, Lang::Str label, const std::vector<std::wstring>& items, int selected, int y) {
    BindToPage(context, CreateControl(
        context, L"STATIC", Lang::Wide(label), SS_LEFT | SS_CENTERIMAGE, static_cast<Id>(0),
        MARGIN, y, LABEL_WIDTH, ROW_HEIGHT));

    // The same width as the fields of the hotkeys: a list does not need the
    // whole window, and a row of controls of different lengths looks ragged.
    const HWND combo = CreateControl(
        context, L"COMBOBOX", L"",
        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, id,
        MARGIN + LABEL_WIDTH, y, FIELD_WIDTH, ROW_HEIGHT * 8);

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
        MARGIN, y, LABEL_WIDTH, ROW_HEIGHT));

    BindToPage(context, CreateControl(
        context, L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL, id,
        MARGIN + LABEL_WIDTH, y, width, ROW_HEIGHT, WS_EX_CLIENTEDGE));

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

void ReportInvalid(std::wstring& fields, Lang::Str label) {
    if (!fields.empty()) {
        fields += L", ";
    }

    fields += Lang::Wide(label);
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
    y = AddCheck(context, Id::SingleInstance, Lang::Str::SettingsSingleInstance,
        settings.application.singleInstance, y);

    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderTray, y);
    y = AddCheck(context, Id::TrayEnabled, Lang::Str::SettingsTrayEnabled, settings.tray.enabled, y);
    y = AddCheck(context, Id::NotificationError, Lang::Str::SettingsNotifyError,
        settings.tray.notifications.onError, y);
    y = AddCheck(context, Id::NotificationDeviceChange, Lang::Str::SettingsNotifyDeviceChange,
        settings.tray.notifications.onDeviceChange, y);
    y = AddCheck(context, Id::NotificationStateChange, Lang::Str::SettingsNotifyStateChange,
        settings.tray.notifications.onStateChange, y);

    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderMenu, y);

    const int columnWidth = (WINDOW_WIDTH - 4 * MARGIN) / 3;

    const struct {
        Id id;
        Lang::Str text;
        bool value;
    } MENU_ITEMS[] = {
        { Id::TrayMenuStatus, Lang::Str::SettingsMenuStatus, settings.tray.menu.showStatus },
        { Id::TrayMenuToggle, Lang::Str::TrayToggleEnabled, settings.tray.menu.toggleEnabled },
        { Id::TrayMenuReinit, Lang::Str::TrayReinitialize, settings.tray.menu.reinitialize },
        { Id::TrayMenuLog, Lang::Str::TrayLog, settings.tray.menu.openLog },
        { Id::TrayMenuDiagnostics, Lang::Str::TrayDiagnostics, settings.tray.menu.diagnostics },
        { Id::TrayMenuStartWithWindows, Lang::Str::TrayStartWithWindows, settings.tray.menu.startWithWindows },
        { Id::TrayMenuAbout, Lang::Str::TrayAbout, settings.tray.menu.about },
        { Id::TrayMenuExit, Lang::Str::TrayExit, settings.tray.menu.exit },
    };

    const size_t menuItemCount = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

    for (size_t i = 0; i < menuItemCount; ++i) {
        const int column = static_cast<int>(i % 3);
        const int row = static_cast<int>(i / 3);

        AddCheckColumn(
            context, MENU_ITEMS[i].id, MENU_ITEMS[i].text, MENU_ITEMS[i].value,
            MARGIN + column * (columnWidth + MARGIN), y + row * ROW_STEP);
    }

    // The grid of the menu items is the lowest block of the page: its last row
    // is where the page ends.
    const int rows = static_cast<int>((menuItemCount + 2) / 3);

    return y + rows * ROW_STEP;
}

int BuildAudioPage(Context& context) {
    context.page = Page::Audio;

    const Config::Settings& settings = *context.settings;

    int y = PAGE_TOP;

    y = AddHeader(context, Lang::Str::SettingsHeaderAudio, y);
    y = AddCheck(context, Id::AudioEnabledOnStartup, Lang::Str::SettingsEnabledOnStartup,
        settings.audio.enabledOnStartup, y);
    y = AddCombo(context, Id::AudioDataFlow, Lang::Str::SettingsDataFlow, Texts(DATA_FLOWS),
        IndexOf(DATA_FLOWS, settings.audio.dataFlow), y);
    y = AddCombo(context, Id::AudioPeriodSelection, Lang::Str::SettingsPeriod, Texts(PERIODS),
        IndexOf(PERIODS, settings.audio.periodSelection), y);
    y = AddEdit(context, Id::AudioRequestedPeriodFrames, Lang::Str::SettingsRequestedPeriod,
        std::to_wstring(settings.audio.requestedPeriodFrames), FIELD_WIDTH, y);
    y = AddCheck(context, Id::AudioAllowPeriodSnap, Lang::Str::SettingsAllowPeriodSnap,
        settings.audio.allowPeriodSnap, y);

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
    y = AddCheck(context, Id::DisablePowerThrottling, Lang::Str::SettingsDisablePowerThrottling,
        settings.performance.disablePowerThrottling, y);

    y += GROUP_GAP;
    y = AddHeader(context, Lang::Str::SettingsHeaderHotkeys, y);
    y = AddCheck(context, Id::HotkeysEnabled, Lang::Str::SettingsHotkeysEnabled,
        settings.hotkeys.enabled, y);
    y = AddEdit(context, Id::HotkeyToggle, Lang::Str::SettingsHotkeyToggle,
        Text::ToWide(settings.hotkeys.toggleEnabled), FIELD_WIDTH, y);
    y = AddEdit(context, Id::HotkeyReinitialize, Lang::Str::SettingsHotkeyReinitialize,
        Text::ToWide(settings.hotkeys.reinitialize), FIELD_WIDTH, y);

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

    // Two pixels of air between the frame of the tabs and the first control.
    context.offsetX += 2;
    context.offsetY += 2;

    // The same two pixels on the right: a control of a page ends before the
    // right border of the tabs.
    context.pageRight = MARGIN + ::MulDiv(
        display.right - display.left - 4, 96, static_cast<int>(context.dpi));
}

// The parts of the window live further down the file; the rebuild uses them.
void ShowPage(Context& context, Page page);
bool ReadControls(Context& context, Config::Settings& updated, std::wstring& invalidFields);
void UpdateEnabledStates(Context& context);

void ResetPageOffset(Context& context) {
    context.offsetX = 0;
    context.offsetY = 0;
}

// The window is as high as its tallest page: the tabs end a little below the
// lowest control of the pages (the menu items of the tray on "Window"), the
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

    // What the tabs draw under their display area: the bottom border.
    RECT display = { 0, 0, tabsWidth, tabsHeight };
    ::SendMessageW(context.tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&display));
    const int borderBelow = tabsHeight - display.bottom;

    // The pages are still placed with the offset of the display area here.
    const int tabsBottom =
        context.Scale(contentBottom + PAGE_BOTTOM_PADDING) + context.offsetY + borderBelow;

    ::SetWindowPos(
        context.tabs, nullptr, 0, 0, tabsWidth, tabsBottom - tabsRect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    context.frameTop = ::MulDiv(tabsBottom, 96, static_cast<int>(context.dpi)) + FRAME_GAP;

    RECT window = {
        0, 0, context.Scale(WINDOW_WIDTH), context.Scale(context.frameTop + FRAME_HEIGHT + MARGIN) };
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
    // In one row the path was overlapped by the buttons next to it.
    const int firstRowY = context.frameTop;
    const int secondRowY = firstRowY + BUTTON_HEIGHT + BUTTON_GAP;

    const int openFileWidth = BUTTON_WIDTH + 30;

    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsOpenFile),
        BS_PUSHBUTTON | WS_TABSTOP, Id::OpenFile,
        MARGIN, firstRowY, openFileWidth, BUTTON_HEIGHT);

    // Re-reads the file without closing the window: a value edited in a text
    // editor gets in, and "Save" is still what writes and applies it.
    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsReload),
        BS_PUSHBUTTON | WS_TABSTOP, Id::Reload,
        MARGIN + openFileWidth + BUTTON_GAP, firstRowY, BUTTON_WIDTH, BUTTON_HEIGHT);

    // The path starts right after "Reload" and takes the rest of the row.
    const int pathX = MARGIN + openFileWidth + 2 * BUTTON_GAP + BUTTON_WIDTH;

    // A long path loses its middle, not its end: the name of the file stays
    // visible.
    CreateControl(
        context, L"STATIC", context.settingsPath, SS_LEFT | SS_CENTERIMAGE | SS_PATHELLIPSIS,
        Id::SettingsPath, pathX, firstRowY, WINDOW_WIDTH - MARGIN - pathX, BUTTON_HEIGHT);

    // Created from left to right, so that Tab walks the row in reading order.
    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsCancel),
        BS_PUSHBUTTON | WS_TABSTOP, Id::Cancel,
        WINDOW_WIDTH - MARGIN - 2 * BUTTON_WIDTH - BUTTON_GAP, secondRowY, BUTTON_WIDTH, BUTTON_HEIGHT);

    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsSave),
        BS_DEFPUSHBUTTON | WS_TABSTOP, Id::Save,
        WINDOW_WIDTH - MARGIN - BUTTON_WIDTH, secondRowY, BUTTON_WIDTH, BUTTON_HEIGHT);
}

// Every control of the window for the current DPI. The order of creation is
// the order of Tab: the tabs, the fields of the page, the buttons of the frame
// last. The frame is built after the pages because it is placed under the
// tabs, whose height follows the tallest page.
void BuildContent(Context& context) {
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
    updated.application.singleInstance = IsChecked(context, Id::SingleInstance);

    updated.tray.enabled = IsChecked(context, Id::TrayEnabled);
    updated.tray.notifications.onError = IsChecked(context, Id::NotificationError);
    updated.tray.notifications.onDeviceChange = IsChecked(context, Id::NotificationDeviceChange);
    updated.tray.notifications.onStateChange = IsChecked(context, Id::NotificationStateChange);

    updated.tray.menu.showStatus = IsChecked(context, Id::TrayMenuStatus);
    updated.tray.menu.toggleEnabled = IsChecked(context, Id::TrayMenuToggle);
    updated.tray.menu.reinitialize = IsChecked(context, Id::TrayMenuReinit);
    updated.tray.menu.openLog = IsChecked(context, Id::TrayMenuLog);
    updated.tray.menu.diagnostics = IsChecked(context, Id::TrayMenuDiagnostics);
    updated.tray.menu.startWithWindows = IsChecked(context, Id::TrayMenuStartWithWindows);
    updated.tray.menu.about = IsChecked(context, Id::TrayMenuAbout);
    updated.tray.menu.exit = IsChecked(context, Id::TrayMenuExit);

    updated.audio.enabledOnStartup = IsChecked(context, Id::AudioEnabledOnStartup);
    updated.audio.dataFlow = ValueAt(DATA_FLOWS, SelectedIndex(context, Id::AudioDataFlow));
    updated.audio.periodSelection = ValueAt(PERIODS, SelectedIndex(context, Id::AudioPeriodSelection));
    updated.audio.allowPeriodSnap = IsChecked(context, Id::AudioAllowPeriodSnap);

    bool valid = true;

    const int requestedPeriodFrames = ReadInt(
        context, Id::AudioRequestedPeriodFrames, 0, 100000,
        static_cast<int>(current.audio.requestedPeriodFrames), valid);
    if (!valid) {
        ReportInvalid(invalidFields, Lang::Str::SettingsRequestedPeriod);
    }

    updated.audio.requestedPeriodFrames = static_cast<unsigned int>(requestedPeriodFrames);

    updated.audio.reinit.defaultDeviceChanged = IsChecked(context, Id::ReinitDefaultDevice);
    updated.audio.reinit.deviceStateChanged = IsChecked(context, Id::ReinitDeviceState);
    updated.audio.reinit.deviceAdded = IsChecked(context, Id::ReinitDeviceAdded);
    updated.audio.reinit.deviceRemoved = IsChecked(context, Id::ReinitDeviceRemoved);
    updated.audio.reinit.resumeFromSleep = IsChecked(context, Id::ReinitResumeFromSleep);
    updated.audio.reinit.sessionUnlock = IsChecked(context, Id::ReinitSessionUnlock);
    updated.audio.reinit.enableWhenDisabled = IsChecked(context, Id::ReinitEnableWhenDisabled);

    updated.audio.reinit.failureTimeoutMs = ReadInt(
        context, Id::ReinitFailureTimeout, 0, 3600000, current.audio.reinit.failureTimeoutMs, valid);
    if (!valid) {
        ReportInvalid(invalidFields, Lang::Str::SettingsReinitFailureTimeout);
    }

    updated.audio.reinit.debounceMs = ReadInt(
        context, Id::ReinitDebounce, 0, 600000, current.audio.reinit.debounceMs, valid);
    if (!valid) {
        ReportInvalid(invalidFields, Lang::Str::SettingsReinitDebounce);
    }

    updated.performance.processPriority = ValueAt(PRIORITIES, SelectedIndex(context, Id::ProcessPriority));
    updated.performance.disablePowerThrottling = IsChecked(context, Id::DisablePowerThrottling);

    updated.hotkeys.enabled = IsChecked(context, Id::HotkeysEnabled);
    updated.hotkeys.toggleEnabled = Text::ToUtf8(
        ReadText(context, Id::HotkeyToggle, Text::ToWide(current.hotkeys.toggleEnabled)));
    updated.hotkeys.reinitialize = Text::ToUtf8(
        ReadText(context, Id::HotkeyReinitialize, Text::ToWide(current.hotkeys.reinitialize)));

    updated.updates.checkOnStartup = IsChecked(context, Id::UpdateCheckOnStartup);

    const int levelIndex = SelectedIndex(context, Id::LogLevel);
    if (levelIndex >= 0 && levelIndex < static_cast<int>(LOG_LEVEL_COUNT)) {
        updated.logging.level = LOG_LEVELS[static_cast<size_t>(levelIndex)].value;
    }

    updated.logging.filePath = Text::ToUtf8(
        ReadText(context, Id::LogFilePath, Text::ToWide(current.logging.filePath)));

    updated.logging.maxFileSizeMb = ReadInt(
        context, Id::LogMaxFileSize, 1, 1024, current.logging.maxFileSizeMb, valid);
    if (!valid) {
        ReportInvalid(invalidFields, Lang::Str::SettingsLogMaxFileSize);
    }

    updated.logging.maxFiles = ReadInt(
        context, Id::LogMaxFiles, 1, 100, current.logging.maxFiles, valid);
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
    SetChecked(context, Id::SingleInstance, settings.application.singleInstance);

    SetChecked(context, Id::TrayEnabled, settings.tray.enabled);
    SetChecked(context, Id::NotificationError, settings.tray.notifications.onError);
    SetChecked(context, Id::NotificationDeviceChange, settings.tray.notifications.onDeviceChange);
    SetChecked(context, Id::NotificationStateChange, settings.tray.notifications.onStateChange);

    SetChecked(context, Id::TrayMenuStatus, settings.tray.menu.showStatus);
    SetChecked(context, Id::TrayMenuToggle, settings.tray.menu.toggleEnabled);
    SetChecked(context, Id::TrayMenuReinit, settings.tray.menu.reinitialize);
    SetChecked(context, Id::TrayMenuLog, settings.tray.menu.openLog);
    SetChecked(context, Id::TrayMenuDiagnostics, settings.tray.menu.diagnostics);
    SetChecked(context, Id::TrayMenuStartWithWindows, settings.tray.menu.startWithWindows);
    SetChecked(context, Id::TrayMenuAbout, settings.tray.menu.about);
    SetChecked(context, Id::TrayMenuExit, settings.tray.menu.exit);

    SetChecked(context, Id::AudioEnabledOnStartup, settings.audio.enabledOnStartup);
    SetSelected(context, Id::AudioDataFlow, IndexOf(DATA_FLOWS, settings.audio.dataFlow));
    SetSelected(context, Id::AudioPeriodSelection, IndexOf(PERIODS, settings.audio.periodSelection));
    SetText(context, Id::AudioRequestedPeriodFrames, std::to_wstring(settings.audio.requestedPeriodFrames));
    SetChecked(context, Id::AudioAllowPeriodSnap, settings.audio.allowPeriodSnap);

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
    SetChecked(context, Id::DisablePowerThrottling, settings.performance.disablePowerThrottling);

    SetChecked(context, Id::HotkeysEnabled, settings.hotkeys.enabled);
    SetText(context, Id::HotkeyToggle, Text::ToWide(settings.hotkeys.toggleEnabled));
    SetText(context, Id::HotkeyReinitialize, Text::ToWide(settings.hotkeys.reinitialize));

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

    const Id trayDependent[] = {
        Id::NotificationError, Id::NotificationDeviceChange, Id::NotificationStateChange,
        Id::TrayMenuStatus, Id::TrayMenuToggle, Id::TrayMenuReinit, Id::TrayMenuLog,
        Id::TrayMenuDiagnostics, Id::TrayMenuStartWithWindows, Id::TrayMenuAbout, Id::TrayMenuExit,
    };

    for (const Id id : trayDependent) {
        const HWND control = Get(context, id);
        if (control != nullptr) {
            ::EnableWindow(control, tray ? TRUE : FALSE);
        }
    }

    const bool hotkeys = IsChecked(context, Id::HotkeysEnabled);
    const bool fixedPeriod =
        ValueAt(PERIODS, SelectedIndex(context, Id::AudioPeriodSelection)) == Config::PeriodSelection::Fixed;

    const bool fileLog = !IsLogLevelOff(SelectedIndex(context, Id::LogLevel));

    const struct {
        Id id;
        bool enabled;
    } STATES[] = {
        { Id::HotkeyToggle, hotkeys },
        { Id::HotkeyReinitialize, hotkeys },
        { Id::AudioRequestedPeriodFrames, fixedPeriod },
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
                    id == static_cast<UINT>(ControlId(Id::HotkeysEnabled)) ||
                    id == static_cast<UINT>(ControlId(Id::AudioPeriodSelection)) ||
                    id == static_cast<UINT>(ControlId(Id::LogLevel))) {
                    UpdateEnabledStates(*context);
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
    const std::wstring& settingsPath) {
    Context context;
    context.instance = instance;
    context.settings = &settings;
    context.settingsPath = settingsPath;
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

    const HWND window = ::CreateWindowExW(
        WS_EX_CONTROLPARENT,
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
    // being edited, but the tray icon and the audio stream keep working.
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
        ::EnableWindow(owner, TRUE);
        ::SetForegroundWindow(owner);
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
