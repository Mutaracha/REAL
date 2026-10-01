#pragma once

#include <string>
#include <vector>

namespace miniant::Config {

// The layout of the settings file. The number grows when the layout changes;
// a file with another number is rewritten once in the current layout, with
// every value the program knows kept.
inline constexpr int CONFIG_VERSION = 1;

enum class CloseAction {
    Minimize,
    Exit,
};

enum class DataFlow {
    Render,
    Capture,
    Both,
};

enum class PeriodSelection {
    Minimum,
    Fundamental,
    Fixed,
};

enum class ProcessPriority {
    Normal,
    BelowNormal,
    Idle,
};

struct ReinitSettings {
    bool defaultDeviceChanged = true;
    bool deviceStateChanged = true;
    // A device that is plugged back in appears as a new endpoint, so this flag
    // matters for the "device was switched off, then on again" case.
    bool deviceAdded = true;
    bool deviceRemoved = false;
    bool resumeFromSleep = true;
    bool sessionUnlock = true;
    // A device change switches the latency reduction back on when it was off.
    bool enableWhenDisabled = true;
    // How long the application keeps trying before it gives up (and stops
    // polling) when the device does not answer at all.
    int failureTimeoutMs = 60000;
    int debounceMs = 1000;
};

struct ApplicationSettings {
    std::string language = "auto";
    bool startMinimizedToTray = false;
    bool minimizeToTray = true;
    CloseAction closeButtonAction = CloseAction::Minimize;
    bool singleInstance = true;
    bool startWithWindows = false;
};

struct NotificationSettings {
    bool onError = true;
    bool onDeviceChange = true;
    bool onStateChange = false;
};

struct TrayMenuSettings {
    bool showStatus = true;
    bool toggleEnabled = true;
    bool reinitialize = true;
    bool openLog = true;
    bool diagnostics = true;
    bool startWithWindows = true;
    bool about = true;
    bool exit = true;
};

struct TraySettings {
    bool enabled = true;
    NotificationSettings notifications;
    TrayMenuSettings menu;
};

struct AudioSettings {
    bool enabledOnStartup = true;
    DataFlow dataFlow = DataFlow::Render;
    PeriodSelection periodSelection = PeriodSelection::Minimum;
    unsigned int requestedPeriodFrames = 0;
    bool allowPeriodSnap = true;
    ReinitSettings reinit;
};

struct PerformanceSettings {
    ProcessPriority processPriority = ProcessPriority::Normal;
    bool disablePowerThrottling = true;
};

struct UpdateSettings {
    // The only setting: a single check at startup. Off by default, so there is
    // no network request at all unless the user asks for it; the application
    // never checks again while it is running and never installs anything by
    // itself. The releases are read from the project this build belongs to, so
    // there is no repository setting either.
    bool checkOnStartup = false;
};

struct HotkeySettings {
    bool enabled = true;
    std::string toggleEnabled = "Ctrl+Alt+L";
    std::string reinitialize = "Ctrl+Alt+R";
};

struct LoggingSettings {
    // These settings describe the log file only: the window of the program
    // always shows the operations at the info level and does not depend on
    // them. "off" switches the file off, any other value switches it on and
    // tells how detailed it is (see Log::Initialize).
    std::string level = "info";
    std::string filePath = "REAL.log";
    int maxFileSizeMb = 1;
    int maxFiles = 3;
};

struct Settings {
    int configVersion = CONFIG_VERSION;

    // Language of the comments in the file. A service field: it only tells the
    // application whether the comments have to be rewritten after a language
    // change (values are never touched by that).
    std::string commentLanguage;
    ApplicationSettings application;
    TraySettings tray;
    AudioSettings audio;
    PerformanceSettings performance;
    UpdateSettings updates;
    HotkeySettings hotkeys;
    LoggingSettings logging;
};

struct LoadResult {
    Settings settings;
    bool fileExists = false;
    bool parseFailed = false;
    std::vector<std::string> warnings;
    std::string error;
};

// "off" in logging.level means "no log file at all"; any other value writes
// the file and tells how detailed it is. The window of the program never
// depends on it (see Log::Initialize).
bool IsLogFileOff(const LoggingSettings& logging);

// Path of "real.settings.json" next to the executable.
std::wstring GetDefaultPath();

LoadResult Load(const std::wstring& path);
bool Write(const Settings& settings, const std::wstring& path);
// Plain JSON without comments (used as a fallback).
std::string ToJsonString(const Settings& settings);
// JSON with comments that explain every parameter: this is what is written to
// the settings file, so that the file itself is the reference.
std::string ToDocumentedJsonString(const Settings& settings);
// The values that take part in the latency reduction, written with the keys of
// the settings file ("dataFlow=render, periodSelection=min, ..."): the keys are
// what the user finds in the file, so they are not translated.
std::string Describe(const Settings& settings);

// The "commentLanguage" field of a file on disk, read without touching the
// rest: the application compares it with the current language and rewrites the
// settings file only when they differ (a value that cannot be read means "the
// file has to be rewritten").
std::string PeekCommentLanguage(const std::wstring& path);

// The "application.language" field of a file on disk: the language has to be
// known before the file is parsed, because everything the reader says about it
// is written to the journal in that language. An empty value means "auto".
std::string PeekLanguage(const std::wstring& path);

}
