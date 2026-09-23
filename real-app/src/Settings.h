#pragma once

#include <string>
#include <vector>

namespace miniant::Config {

enum class CloseAction {
    Minimize,
    Exit,
};

enum class DataFlow {
    Render,
    Capture,
    Both,
};

enum class DeviceRole {
    Console,
    Multimedia,
    Communications,
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

enum class UpdatesMode {
    Off,
    Manual,
};

struct ReinitSettings {
    bool defaultDeviceChanged = true;
    bool deviceStateChanged = true;
    bool deviceAdded = false;
    bool deviceRemoved = false;
    bool resumeFromSleep = true;
    bool sessionUnlock = true;
    int debounceMs = 1000;
};

struct ApplicationSettings {
    bool startMinimizedToTray = false;
    bool minimizeToTray = true;
    CloseAction closeButtonAction = CloseAction::Minimize;
    bool showConsole = false;
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
    bool openSettings = true;
    bool openLog = true;
    bool checkForUpdates = true;
    bool startWithWindows = true;
    bool about = true;
    bool exit = true;
};

struct TraySettings {
    bool enabled = true;
    bool showStatusInTooltip = true;
    NotificationSettings notifications;
    TrayMenuSettings menu;
};

struct AudioSettings {
    bool enabledOnStartup = true;
    DataFlow dataFlow = DataFlow::Render;
    DeviceRole role = DeviceRole::Console;
    PeriodSelection periodSelection = PeriodSelection::Minimum;
    unsigned int requestedPeriodFrames = 0;
    bool allowPeriodSnap = true;
    bool releaseOnExit = true;
    ReinitSettings reinit;
};

struct PerformanceSettings {
    ProcessPriority processPriority = ProcessPriority::Normal;
    bool disablePowerThrottling = true;
};

struct UpdateSettings {
    UpdatesMode mode = UpdatesMode::Off;
    std::string repository = "Mutaracha/REAL";
    int timeoutSeconds = 15;
    bool checkOnStartup = false;
};

struct HotkeySettings {
    bool enabled = true;
    std::string toggleEnabled = "Ctrl+Alt+L";
    std::string reinitialize = "Ctrl+Alt+R";
};

struct LoggingSettings {
    std::string level = "info";
    bool toConsole = false;
    bool toFile = true;
    std::string filePath = "REAL.log";
    int maxFileSizeMb = 1;
    int maxFiles = 3;
};

struct Settings {
    int configVersion = 1;
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

// Path of "real.settings.json" next to the executable.
std::wstring GetDefaultPath();

LoadResult Load(const std::wstring& path);
bool Write(const Settings& settings, const std::wstring& path);
std::string ToJsonString(const Settings& settings);
std::string Describe(const Settings& settings);

}
