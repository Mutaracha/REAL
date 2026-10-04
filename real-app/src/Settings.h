#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace miniant::Config {

// The layout of the settings file. The number grows when the layout changes;
// a file with another number is rewritten once in the current layout, with
// every value the program knows kept.
inline constexpr int CONFIG_VERSION = 1;

// A number of the settings file: its key in its section and the range it has
// to be in. The reader of the file, the comments of the file and the fields of
// the settings window all take the range from here, so a value the window
// accepts is never rejected by the reader on the next start.
struct NumberLimits {
    const char* key;
    int minimum;
    int maximum;
};

// 0 - no size is set (see AudioSettings::fixedBufferFrames).
inline constexpr NumberLimits FIXED_BUFFER_FRAMES_LIMITS = { "fixedBufferFrames", 0, 100000 };
inline constexpr NumberLimits FAILURE_TIMEOUT_MS_LIMITS = { "failureTimeoutMs", 5000, 3600000 };
// With a shorter pause a faulty device that connects and disconnects several
// times a second would restart the streams on every event.
inline constexpr NumberLimits DEBOUNCE_MS_LIMITS = { "debounceMs", 500, 60000 };
inline constexpr NumberLimits LOG_FILE_SIZE_MB_LIMITS = { "maxFileSizeMb", 1, 1024 };
inline constexpr NumberLimits LOG_FILES_LIMITS = { "maxFiles", 1, 100 };

// A settings file is a few kilobytes: anything larger is not read at all.
inline constexpr size_t MAX_FILE_BYTES = 1024 * 1024;

enum class CloseAction {
    Minimize,
    Exit,
};

enum class DataFlow {
    Render,
    Capture,
    Both,
};

// The buffer REAL asks the audio engine for: the smallest one the driver of
// the device supports, or a fixed number of frames (fixedBufferFrames).
enum class BufferMode {
    Minimum,
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
    // An endpoint that did not exist before. A device that is plugged back in
    // keeps its endpoint and only changes its state (deviceStateChanged), so
    // this flag is off by default.
    bool deviceAdded = false;
    bool deviceRemoved = false;
    bool resumeFromSleep = true;
    bool sessionUnlock = true;
    // How long the application keeps trying before it gives up (and stops
    // polling) when the device does not answer at all.
    int failureTimeoutMs = 60000;
    // The pause between a device event and the restart: Windows sends a burst
    // of events, and the restart waits for the last one.
    int debounceMs = 1000;
};

struct ApplicationSettings {
    std::string language = "auto";
    bool startMinimizedToTray = false;
    bool minimizeToTray = true;
    CloseAction closeButtonAction = CloseAction::Exit;
    bool startWithWindows = false;
};

struct NotificationSettings {
    bool onError = true;
    bool onDeviceChange = true;
    bool onStateChange = true;
};

// The items of the tray menu that can be hidden. The status line, "Settings"
// and "Exit" are always there: a menu without them would leave no way to the
// settings or out of the program.
struct TrayMenuSettings {
    bool toggleEnabled = true;
    bool reinitialize = true;
    bool openLog = true;
    bool diagnostics = true;
};

struct TraySettings {
    bool enabled = true;
    NotificationSettings notifications;
    TrayMenuSettings menu;
};

struct AudioSettings {
    DataFlow dataFlow = DataFlow::Render;
    BufferMode buffer = BufferMode::Minimum;
    // The size of the fixed buffer in frames; 0 - not set. Only "fixed" uses
    // it, a value the device does not accept is adjusted to the nearest one.
    unsigned int fixedBufferFrames = 0;
    ReinitSettings reinit;
};

struct PerformanceSettings {
    ProcessPriority processPriority = ProcessPriority::Normal;
};

struct UpdateSettings {
    // The only setting: a single check at startup (false - no network request
    // at all). The application never checks again while it is running and
    // never installs anything by itself. The releases are read from the
    // project this build belongs to, so there is no repository setting either.
    bool checkOnStartup = true;
};

struct LoggingSettings {
    // These settings describe the log file only: the window of the program
    // always shows the operations at the info level and does not depend on
    // them. "off" (the default) switches the file off, any other value
    // switches it on and tells how detailed it is (see Log::Initialize).
    std::string level = "off";
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
    LoggingSettings logging;
};

struct LoadResult {
    Settings settings;
    bool fileExists = false;
    bool parseFailed = false;
    std::vector<std::string> warnings;
    std::string error;
};

// The levels of the log file, as the comments of the file and the warnings
// name them.
inline constexpr char LOG_LEVELS_ALLOWED[] = "off, error, warn, info, debug, trace";

// A level of the log file in its main form ("Warning" becomes "warn", "none"
// becomes "off"); false for a word that is not a level. The settings file and
// --log-level are read with it.
bool NormalizeLogLevel(const std::string& value, std::string& level);

// "off" in logging.level means "no log file at all"; any other value writes
// the file and tells how detailed it is. The window of the program never
// depends on it (see Log::Initialize).
bool IsLogFileOff(const LoggingSettings& logging);

// Path of "real.settings.json" next to the executable.
std::wstring GetDefaultPath();

// Reads the file. A key that is missing gets its default value; a value that
// cannot be used (a wrong type, a number out of its range, an unknown word)
// is replaced with the one of "previous" - the settings in use when the file
// is read again, the defaults at startup - and a warning names both.
LoadResult Load(const std::wstring& path, const Settings& previous = Settings());
bool Write(const Settings& settings, const std::wstring& path);
// Plain JSON without comments (used as a fallback).
std::string ToJsonString(const Settings& settings);
// JSON with comments that explain every parameter: this is what is written to
// the settings file, so that the file itself is the reference.
std::string ToDocumentedJsonString(const Settings& settings);
// The values that take part in the latency reduction, in the words of the
// settings window and in the language of the interface: the direction of the
// devices ("playback", "воспроизведения") and the buffer ("minimum buffer",
// "буфер фиксированный, 480 фреймов"). The journal and the diagnostics report
// put them into sentences of their own (LogAudioSettings, DiagConfig).
std::string DescribeFlow(const Settings& settings);
std::string DescribeBuffer(const Settings& settings);

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
