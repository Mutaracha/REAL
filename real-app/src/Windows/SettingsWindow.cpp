#include "SettingsWindow.h"

#include "../../res/resource.h"
#include "../Lang.h"
#include "../Text.h"
#include "TextMetrics.h"
#include "WindowPlacement.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>
#include <utility>
#include <vector>

using namespace miniant;
using namespace miniant::Windows;

namespace {

const wchar_t SETTINGS_CLASS_NAME[] = L"REAL.SettingsWindow";

const int WINDOW_WIDTH = 680;
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
    TrayTooltip,
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
    AudioRole,
    AudioPeriodSelection,
    AudioRequestedPeriodFrames,
    AudioAllowPeriodSnap,
    AudioReleaseOnExit,

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

    std::vector<std::pair<HWND, Page>> pageControls;
    Page page = Page::Window;

    // The system tab control and the offset of a page inside it: every control
    // of a page is created with this offset, so the pages sit in the display
    // area of the tabs and never overlap them.
    HWND tabs = nullptr;
    int offsetX = 0;
    int offsetY = 0;

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

const Choice<Config::DeviceRole> DEVICE_ROLES[] = {
    { Config::DeviceRole::Console, Lang::Str::SettingsRoleConsole },
    { Config::DeviceRole::Multimedia, Lang::Str::SettingsRoleMultimedia },
    { Config::DeviceRole::Communications, Lang::Str::SettingsRoleCommunications },
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

// The level names are the values of the file: they are not translated.
const char* LOG_LEVELS[] = { "trace", "debug", "info", "warn", "error", "off" };
const size_t LOG_LEVEL_COUNT = sizeof(LOG_LEVELS) / sizeof(LOG_LEVELS[0]);

int LogLevelIndex(const std::string& level) {
    for (size_t i = 0; i < LOG_LEVEL_COUNT; ++i) {
        if (level == LOG_LEVELS[i]) {
            return static_cast<int>(i);
        }
    }

    return 2; // "info"
}

std::vector<std::wstring> LogLevelTexts() {
    std::vector<std::wstring> items;
    items.reserve(LOG_LEVEL_COUNT);

    for (size_t i = 0; i < LOG_LEVEL_COUNT; ++i) {
        items.push_back(Text::ToWide(LOG_LEVELS[i]));
    }

    return items;
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

// A control is never wider than its own text: a click far to the right of a
// caption does nothing, and the caption is the only thing that reacts.
int CheckWidth(Context& context, const std::wstring& text) {
    const int textWidth = MeasureTextWidth(context.font, text);
    return textWidth > 0 ? textWidth + CHECK_GLYPH_WIDTH : WINDOW_WIDTH - 2 * MARGIN;
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
        x + context.offsetX,
        y + context.offsetY,
        width,
        height,
        context.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ControlId(id))),
        context.instance,
        nullptr);

    if (control != nullptr && context.font != nullptr) {
        ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(context.font), TRUE);
    }

    return control;
}

int AddHeader(Context& context, Lang::Str text, int y) {
    const HWND label = CreateControl(
        context, L"STATIC", Lang::Wide(text), SS_LEFT | SS_CENTERIMAGE, static_cast<Id>(0),
        MARGIN, y, WINDOW_WIDTH - 2 * MARGIN, ROW_HEIGHT);

    if (label != nullptr && context.headerFont != nullptr) {
        ::SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(context.headerFont), TRUE);
    }

    BindToPage(context, label);
    return y + HEADER_STEP;
}

// An explanation under a control: it belongs to the page like everything else
// and takes as many rows as it has lines.
int AddHint(Context& context, Lang::Str text, int lines, int y) {
    BindToPage(context, CreateControl(
        context, L"STATIC", Lang::Wide(text), SS_LEFT, static_cast<Id>(0),
        MARGIN, y, WINDOW_WIDTH - 2 * MARGIN, ROW_HEIGHT * lines));

    return y + ROW_HEIGHT * lines;
}

int AddCheck(Context& context, Id id, Lang::Str text, bool value, int y) {
    const std::wstring caption = Lang::Wide(text);

    const HWND check = CreateControl(
        context, L"BUTTON", caption, BS_AUTOCHECKBOX | WS_TABSTOP, id,
        MARGIN, y, CheckWidth(context, caption), ROW_HEIGHT);

    if (check != nullptr) {
        ::SendMessageW(check, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    BindToPage(context, check);
    return y + ROW_STEP;
}

void AddCheckColumn(Context& context, Id id, Lang::Str text, bool value, int x, int width, int y) {
    const std::wstring caption = Lang::Wide(text);

    const HWND check = CreateControl(
        context, L"BUTTON", caption, BS_AUTOCHECKBOX | WS_TABSTOP, id,
        x, y, CheckWidth(context, caption), ROW_HEIGHT);

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
    y = AddCheck(context, Id::TrayTooltip, Lang::Str::SettingsTrayTooltip,
        settings.tray.showStatusInTooltip, y);
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
            MARGIN + column * (columnWidth + MARGIN), columnWidth, y + row * ROW_STEP);
    }

    y += 3 * ROW_STEP + GROUP_GAP;

    return y + ROW_STEP;
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
    y = AddCombo(context, Id::AudioRole, Lang::Str::SettingsRole, Texts(DEVICE_ROLES),
        IndexOf(DEVICE_ROLES, settings.audio.role), y);
    y = AddHint(context, Lang::Str::SettingsRoleHint, 3, y);
    y = AddCombo(context, Id::AudioPeriodSelection, Lang::Str::SettingsPeriod, Texts(PERIODS),
        IndexOf(PERIODS, settings.audio.periodSelection), y);
    y = AddEdit(context, Id::AudioRequestedPeriodFrames, Lang::Str::SettingsRequestedPeriod,
        std::to_wstring(settings.audio.requestedPeriodFrames), FIELD_WIDTH, y);
    y = AddCheck(context, Id::AudioAllowPeriodSnap, Lang::Str::SettingsAllowPeriodSnap,
        settings.audio.allowPeriodSnap, y);
    y = AddCheck(context, Id::AudioReleaseOnExit, Lang::Str::SettingsReleaseOnExit,
        settings.audio.releaseOnExit, y);

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
    y = AddCombo(context, Id::LogLevel, Lang::Str::SettingsLogLevel, LogLevelTexts(),
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
    const int bottom = WINDOW_HEIGHT - MARGIN - BUTTON_HEIGHT - 8;

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

    context.offsetX = display.left;
    context.offsetY = top + display.top - PAGE_TOP;

    // Two pixels of air between the frame of the tabs and the first control.
    context.offsetX += 2;
    context.offsetY += 2;
}

void ResetPageOffset(Context& context) {
    context.offsetX = 0;
    context.offsetY = 0;
}

// The frame of the window: the buttons at the bottom.
void BuildFrame(Context& context) {
    const int buttonY = WINDOW_HEIGHT - MARGIN - BUTTON_HEIGHT;

    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsOpenFile),
        BS_PUSHBUTTON | WS_TABSTOP, Id::OpenFile,
        MARGIN, buttonY, BUTTON_WIDTH + 30, BUTTON_HEIGHT);

    CreateControl(
        context, L"STATIC", context.settingsPath, SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
        Id::SettingsPath, MARGIN + BUTTON_WIDTH + 44, buttonY,
        WINDOW_WIDTH - 2 * MARGIN - 2 * BUTTON_WIDTH - 74, BUTTON_HEIGHT);

    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsSave),
        BS_DEFPUSHBUTTON | WS_TABSTOP, Id::Save,
        WINDOW_WIDTH - MARGIN - BUTTON_WIDTH, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT);

    CreateControl(
        context, L"BUTTON", Lang::Wide(Lang::Str::SettingsCancel),
        BS_PUSHBUTTON | WS_TABSTOP, Id::Cancel,
        WINDOW_WIDTH - MARGIN - 2 * BUTTON_WIDTH - 8, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT);
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
    updated.tray.showStatusInTooltip = IsChecked(context, Id::TrayTooltip);
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
    updated.audio.role = ValueAt(DEVICE_ROLES, SelectedIndex(context, Id::AudioRole));
    updated.audio.periodSelection = ValueAt(PERIODS, SelectedIndex(context, Id::AudioPeriodSelection));
    updated.audio.allowPeriodSnap = IsChecked(context, Id::AudioAllowPeriodSnap);
    updated.audio.releaseOnExit = IsChecked(context, Id::AudioReleaseOnExit);

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
        updated.logging.level = LOG_LEVELS[static_cast<size_t>(levelIndex)];
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

    RECT desired = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    ::AdjustWindowRectEx(&desired, style, FALSE, 0);

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

    context.font = ::CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    context.headerFont = ::CreateFontW(
        -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // The frame is built first (it is not inside the tabs), then the tab
    // control, and only after it the pages, which are placed inside its display
    // area by the offset it reports.
    BuildFrame(context);

    InitCommonControlsOnce();

    CreateTabs(context);
    BuildWindowPage(context);
    BuildAudioPage(context);
    BuildOtherPage(context);

    ResetPageOffset(context);
    ShowPage(context, Page::Window);

    // Modal for the main window: it takes no commands while the settings are
    // being edited, but the tray icon and the audio stream keep working.
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
            // 0 is WM_QUIT: the window of the program was closed while the
            // settings were open. The message belongs to the loop of the
            // program, so it is put back and read there.
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

    if (context.headerFont != nullptr) {
        ::DeleteObject(context.headerFont);
    }

    ::UnregisterClassW(SETTINGS_CLASS_NAME, instance);

    return context.saved;
}
