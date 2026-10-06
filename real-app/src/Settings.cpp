#include "Settings.h"

#include "AppVersion.h"
#include "Lang.h"
#include "Text.h"
#include "Windows/Filesystem.h"

#include <nlohmann/json.hpp>

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <utility>

using json = nlohmann::json;
using namespace miniant;
using namespace miniant::Config;
using namespace miniant::Lang;

namespace {

const std::pair<const char*, CloseAction> CLOSE_ACTION_MAP[] = {
    { "minimize", CloseAction::Minimize },
    { "exit", CloseAction::Exit },
};

const std::pair<const char*, DataFlow> DATA_FLOW_MAP[] = {
    { "render", DataFlow::Render },
    { "capture", DataFlow::Capture },
    { "both", DataFlow::Both },
};

const std::pair<const char*, BufferMode> BUFFER_MODE_MAP[] = {
    { "min", BufferMode::Minimum },
    { "fixed", BufferMode::Fixed },
};

const std::pair<const char*, ProcessPriority> PROCESS_PRIORITY_MAP[] = {
    { "normal", ProcessPriority::Normal },
    { "belownormal", ProcessPriority::BelowNormal },
    { "idle", ProcessPriority::Idle },
};

// "audio.dataFlow" for a parameter of a section, "configVersion" for one of the
// root object (its section name is empty).
std::string KeyName(const std::string& sectionName, const std::string& key) {
    return sectionName.empty() ? key : sectionName + "." + key;
}

// A value as it is written in the file: the comments of the file and the
// warnings of the reader show values this way.
std::string JsonValue(bool value) {
    return value ? "true" : "false";
}

std::string JsonValue(int value) {
    return std::to_string(value);
}

std::string JsonValue(unsigned int value) {
    return std::to_string(value);
}

std::string JsonValue(const std::string& value) {
    return json(value).dump();
}

std::string JsonValue(const char* value) {
    return json(value).dump();
}

const char* ToString(CloseAction value) {
    return value == CloseAction::Exit ? "exit" : "minimize";
}

const char* ToString(DataFlow value) {
    switch (value) {
        case DataFlow::Capture: return "capture";
        case DataFlow::Both: return "both";
        default: return "render";
    }
}

const char* ToString(BufferMode value) {
    return value == BufferMode::Fixed ? "fixed" : "min";
}

const char* ToString(ProcessPriority value) {
    switch (value) {
        case ProcessPriority::BelowNormal: return "belowNormal";
        case ProcessPriority::Idle: return "idle";
        default: return "normal";
    }
}

// The warnings of one reading of the file, in the language they are shown in:
// the program writes them to its log in its own language, the settings window
// lists them in the one it shows (see Load).
struct Warnings {
    Language language;
    std::vector<std::string>& list;
};

// The readers below leave a missing key with its default value. A value that
// is there but cannot be used gets the fallback - the value in use when the
// file is read again, the default at startup - and a warning that names the
// key, what is wrong with it and the value that is used instead.

template <typename T, size_t N>
void ReadEnum(
    const json& section,
    const char* key,
    const std::pair<const char*, T> (&table)[N],
    T& target,
    T fallback,
    Warnings& warnings,
    const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_string()) {
        target = fallback;
        warnings.list.push_back(fmt::format(
            Lang::Utf8(Str::CfgWarnString, warnings.language), KeyName(sectionName, key),
            JsonValue(ToString(fallback))));
        return;
    }

    const std::string value = Text::ToLowerAscii(Text::Trim(it->get<std::string>()));
    for (size_t i = 0; i < N; ++i) {
        if (value == table[i].first) {
            target = table[i].second;
            return;
        }
    }

    // The allowed values are named the way the file writes them.
    std::string allowed;
    for (size_t i = 0; i < N; ++i) {
        if (!allowed.empty()) {
            allowed += ", ";
        }

        allowed += ToString(table[i].second);
    }

    target = fallback;
    warnings.list.push_back(fmt::format(
        Lang::Utf8(Str::CfgWarnUnknownValue, warnings.language), KeyName(sectionName, key), it->get<std::string>(),
        allowed, JsonValue(ToString(fallback))));
}

// A word of the file with its synonyms: the value is kept in its main form
// ("russian" becomes "ru"), so the window and the program read it alike.
struct Word {
    const char* text;
    const char* value;
};

const Word LANGUAGE_WORDS[] = {
    { "auto", "auto" },
    { "", "auto" },
    { "system", "auto" },
    { "default", "auto" },
    { "en", "en" },
    { "english", "en" },
    { "ru", "ru" },
    { "rus", "ru" },
    { "russian", "ru" },
};

const char LANGUAGE_ALLOWED[] = "auto, en, ru";

const Word LOG_LEVEL_WORDS[] = {
    { "off", "off" },
    { "none", "off" },
    { "error", "error" },
    { "err", "error" },
    { "warn", "warn" },
    { "warning", "warn" },
    { "info", "info" },
    { "debug", "debug" },
    { "trace", "trace" },
};

template <size_t N>
void ReadWord(
    const json& section,
    const char* key,
    const Word (&words)[N],
    const char* allowed,
    std::string& target,
    const std::string& fallback,
    Warnings& warnings,
    const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_string()) {
        target = fallback;
        warnings.list.push_back(fmt::format(
            Lang::Utf8(Str::CfgWarnString, warnings.language), KeyName(sectionName, key), JsonValue(fallback)));
        return;
    }

    const std::string value = Text::ToLowerAscii(Text::Trim(it->get<std::string>()));
    for (size_t i = 0; i < N; ++i) {
        if (value == words[i].text) {
            target = words[i].value;
            return;
        }
    }

    target = fallback;
    warnings.list.push_back(fmt::format(
        Lang::Utf8(Str::CfgWarnUnknownValue, warnings.language), KeyName(sectionName, key), it->get<std::string>(),
        allowed, JsonValue(fallback)));
}

void ReadBool(
    const json& section,
    const char* key,
    bool& target,
    bool fallback,
    Warnings& warnings,
    const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_boolean()) {
        target = fallback;
        warnings.list.push_back(fmt::format(
            Lang::Utf8(Str::CfgWarnBool, warnings.language), KeyName(sectionName, key), JsonValue(fallback)));
        return;
    }

    target = it->get<bool>();
}

// A whole number of the file within its limits. A number beyond the range of
// int is out of the limits as well: it is never cut down to a value that
// happens to fit (4294968296 is not 1000).
bool WithinLimits(const json& value, const NumberLimits& limits, long long& result) {
    if (value.is_number_unsigned()) {
        const unsigned long long number = value.get<unsigned long long>();
        if (number > static_cast<unsigned long long>(limits.maximum)) {
            return false;
        }

        result = static_cast<long long>(number);
    } else {
        result = value.get<long long>();
    }

    return result >= limits.minimum && result <= limits.maximum;
}

// Checks the type and the limits; false (with a warning) when the fallback
// has to be used.
bool ReadNumber(
    const json& section,
    const NumberLimits& limits,
    long long& value,
    const std::string& fallbackText,
    Warnings& warnings,
    const std::string& sectionName) {
    const auto it = section.find(limits.key);

    if (!it->is_number_integer()) {
        warnings.list.push_back(fmt::format(
            Lang::Utf8(Str::CfgWarnInteger, warnings.language), KeyName(sectionName, limits.key), fallbackText));
        return false;
    }

    if (!WithinLimits(*it, limits, value)) {
        warnings.list.push_back(fmt::format(
            Lang::Utf8(Str::CfgWarnRange, warnings.language), KeyName(sectionName, limits.key), it->dump(),
            limits.minimum, limits.maximum, fallbackText));
        return false;
    }

    return true;
}

void ReadInt(
    const json& section,
    const NumberLimits& limits,
    int& target,
    int fallback,
    Warnings& warnings,
    const std::string& sectionName) {
    if (section.find(limits.key) == section.end()) {
        return;
    }

    long long value = 0;
    target = ReadNumber(section, limits, value, JsonValue(fallback), warnings, sectionName)
        ? static_cast<int>(value)
        : fallback;
}

void ReadUnsigned(
    const json& section,
    const NumberLimits& limits,
    unsigned int& target,
    unsigned int fallback,
    Warnings& warnings,
    const std::string& sectionName) {
    if (section.find(limits.key) == section.end()) {
        return;
    }

    long long value = 0;
    target = ReadNumber(section, limits, value, JsonValue(fallback), warnings, sectionName)
        ? static_cast<unsigned int>(value)
        : fallback;
}

void ReadString(
    const json& section,
    const char* key,
    std::string& target,
    const std::string& fallback,
    Warnings& warnings,
    const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_string()) {
        target = fallback;
        warnings.list.push_back(fmt::format(
            Lang::Utf8(Str::CfgWarnString, warnings.language), KeyName(sectionName, key), JsonValue(fallback)));
        return;
    }

    target = it->get<std::string>();
}

void WarnUnknownKeys(
    const json& section,
    const std::string& sectionName,
    std::initializer_list<const char*> known,
    Warnings& warnings) {
    for (auto it = section.begin(); it != section.end(); ++it) {
        const std::string key = it.key();
        bool found = false;
        for (const char* candidate : known) {
            if (key == candidate) {
                found = true;
                break;
            }
        }

        if (!found) {
            warnings.list.push_back(fmt::format(
                Lang::Utf8(Str::CfgWarnUnknownKey, warnings.language), KeyName(sectionName, key)));
        }
    }
}

const json* FindSection(const json& root, const char* key) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_object()) {
        return nullptr;
    }

    return &(*it);
}

// The service field of the root object.
constexpr NumberLimits CONFIG_VERSION_LIMITS = { "configVersion", 1, 1000 };

}

bool miniant::Config::NormalizeLogLevel(const std::string& value, std::string& level) {
    const std::string word = Text::ToLowerAscii(Text::Trim(value));

    for (const Word& candidate : LOG_LEVEL_WORDS) {
        if (word == candidate.text) {
            level = candidate.value;
            return true;
        }
    }

    return false;
}

bool miniant::Config::IsLogFileOff(const LoggingSettings& logging) {
    const std::string level = Text::ToLowerAscii(Text::Trim(logging.level));
    return level == "off" || level == "none";
}

std::wstring miniant::Config::GetDefaultPath() {
    return Windows::Filesystem::JoinPath(Windows::Filesystem::GetExecutableDirectory(), L"real.settings.json");
}

LoadResult miniant::Config::Load(const std::wstring& path, const Settings& previous, Language language) {
    LoadResult result;

    bool readSucceeded = false;
    bool tooLarge = false;
    std::string content = Windows::Filesystem::ReadTextFileUtf8(path, &readSucceeded, MAX_FILE_BYTES, &tooLarge);
    if (!readSucceeded) {
        // A file that exists but cannot be read is not the same as no file at
        // all: the program says so and starts on the defaults without touching
        // the file of the user.
        result.fileExists = Windows::Filesystem::IsFile(path);
        if (result.fileExists) {
            result.parseFailed = true;
            result.tooLarge = tooLarge;
            result.error = Lang::Utf8(tooLarge ? Str::CfgErrTooLarge : Str::CfgErrRead, language);
        }

        return result;
    }

    result.fileExists = true;

    content = Text::StripJsonComments(Text::StripUtf8Bom(content));

    json root;
    try {
        root = json::parse(content);
    } catch (const json::exception& error) {
        result.parseFailed = true;
        result.error = fmt::format(Lang::Utf8(Str::CfgErrParse, language), error.what());
        return result;
    }

    if (!root.is_object()) {
        result.parseFailed = true;
        result.error = Lang::Utf8(Str::CfgErrNotObject, language);
        return result;
    }

    Settings& settings = result.settings;
    Warnings warnings{ language, result.warnings };

    ReadInt(root, CONFIG_VERSION_LIMITS, settings.configVersion, previous.configVersion, warnings, "");
    ReadString(root, "commentLanguage", settings.commentLanguage, previous.commentLanguage, warnings, "");
    WarnUnknownKeys(root, "",
        { "configVersion", "commentLanguage", "application", "tray", "audio", "performance", "updates", "logging" },
        warnings);

    if (const json* section = FindSection(root, "application")) {
        const ApplicationSettings& before = previous.application;
        ApplicationSettings& application = settings.application;

        ReadWord(*section, "language", LANGUAGE_WORDS, LANGUAGE_ALLOWED,
            application.language, before.language, warnings, "application");
        ReadBool(*section, "startMinimizedToTray", application.startMinimizedToTray,
            before.startMinimizedToTray, warnings, "application");
        ReadBool(*section, "minimizeToTray", application.minimizeToTray, before.minimizeToTray, warnings, "application");
        ReadEnum(*section, "closeButtonAction", CLOSE_ACTION_MAP, application.closeButtonAction,
            before.closeButtonAction, warnings, "application");
        ReadBool(*section, "startWithWindows", application.startWithWindows, before.startWithWindows, warnings, "application");
        WarnUnknownKeys(*section, "application",
            { "language", "startMinimizedToTray", "minimizeToTray", "closeButtonAction", "startWithWindows" },
            warnings);
    }

    if (const json* section = FindSection(root, "tray")) {
        const TraySettings& before = previous.tray;
        TraySettings& tray = settings.tray;

        ReadBool(*section, "enabled", tray.enabled, before.enabled, warnings, "tray");

        if (const json* notifications = FindSection(*section, "notifications")) {
            const char* name = "tray.notifications";
            ReadBool(*notifications, "onError", tray.notifications.onError, before.notifications.onError, warnings, name);
            ReadBool(*notifications, "onDeviceChange", tray.notifications.onDeviceChange,
                before.notifications.onDeviceChange, warnings, name);
            ReadBool(*notifications, "onStateChange", tray.notifications.onStateChange,
                before.notifications.onStateChange, warnings, name);
            WarnUnknownKeys(*notifications, name, { "onError", "onDeviceChange", "onStateChange" }, warnings);
        }

        if (const json* menu = FindSection(*section, "menu")) {
            const char* name = "tray.menu";
            ReadBool(*menu, "toggleEnabled", tray.menu.toggleEnabled, before.menu.toggleEnabled, warnings, name);
            ReadBool(*menu, "reinitialize", tray.menu.reinitialize, before.menu.reinitialize, warnings, name);
            ReadBool(*menu, "openLog", tray.menu.openLog, before.menu.openLog, warnings, name);
            ReadBool(*menu, "diagnostics", tray.menu.diagnostics, before.menu.diagnostics, warnings, name);
            WarnUnknownKeys(*menu, name, { "toggleEnabled", "reinitialize", "openLog", "diagnostics" }, warnings);
        }

        WarnUnknownKeys(*section, "tray", { "enabled", "notifications", "menu" }, warnings);
    }

    if (const json* section = FindSection(root, "audio")) {
        const AudioSettings& before = previous.audio;
        AudioSettings& audio = settings.audio;

        ReadEnum(*section, "dataFlow", DATA_FLOW_MAP, audio.dataFlow, before.dataFlow, warnings, "audio");
        ReadEnum(*section, "buffer", BUFFER_MODE_MAP, audio.buffer, before.buffer, warnings, "audio");
        ReadUnsigned(*section, FIXED_BUFFER_FRAMES_LIMITS, audio.fixedBufferFrames, before.fixedBufferFrames,
            warnings, "audio");

        // A fixed buffer without a size means nothing: the buffer in use is
        // kept (the minimum one at startup), and the window shows that choice.
        if (audio.buffer == BufferMode::Fixed && audio.fixedBufferFrames == 0) {
            const bool fixedBefore = before.buffer == BufferMode::Fixed && before.fixedBufferFrames > 0;
            audio.buffer = fixedBefore ? BufferMode::Fixed : BufferMode::Minimum;
            audio.fixedBufferFrames = fixedBefore ? before.fixedBufferFrames : 0;

            warnings.list.push_back(fmt::format(
                Lang::Utf8(Str::CfgWarnFixedBufferZero, warnings.language),
                KeyName("audio", FIXED_BUFFER_FRAMES_LIMITS.key), DescribeBuffer(settings, language)));
        }

        if (const json* reinit = FindSection(*section, "reinit")) {
            const ReinitSettings& was = before.reinit;
            ReinitSettings& now = audio.reinit;
            const char* name = "audio.reinit";

            ReadBool(*reinit, "defaultDeviceChanged", now.defaultDeviceChanged, was.defaultDeviceChanged, warnings, name);
            ReadBool(*reinit, "resumeFromSleep", now.resumeFromSleep, was.resumeFromSleep, warnings, name);
            ReadBool(*reinit, "sessionUnlock", now.sessionUnlock, was.sessionUnlock, warnings, name);
            ReadInt(*reinit, FAILURE_TIMEOUT_SEC_LIMITS, now.failureTimeoutSec, was.failureTimeoutSec, warnings, name);
            ReadInt(*reinit, DEBOUNCE_SEC_LIMITS, now.debounceSec, was.debounceSec, warnings, name);
            WarnUnknownKeys(*reinit, name,
                { "defaultDeviceChanged", "resumeFromSleep", "sessionUnlock", FAILURE_TIMEOUT_SEC_LIMITS.key,
                  DEBOUNCE_SEC_LIMITS.key },
                warnings);
        }

        WarnUnknownKeys(*section, "audio",
            { "dataFlow", "buffer", FIXED_BUFFER_FRAMES_LIMITS.key, "reinit" },
            warnings);
    }

    if (const json* section = FindSection(root, "performance")) {
        ReadEnum(*section, "processPriority", PROCESS_PRIORITY_MAP, settings.performance.processPriority,
            previous.performance.processPriority, warnings, "performance");
        WarnUnknownKeys(*section, "performance", { "processPriority" }, warnings);
    }

    if (const json* section = FindSection(root, "updates")) {
        ReadBool(*section, "checkOnStartup", settings.updates.checkOnStartup, previous.updates.checkOnStartup,
            warnings, "updates");
        WarnUnknownKeys(*section, "updates", { "checkOnStartup" }, warnings);
    }

    if (const json* section = FindSection(root, "logging")) {
        const LoggingSettings& before = previous.logging;
        LoggingSettings& logging = settings.logging;

        ReadWord(*section, "level", LOG_LEVEL_WORDS, LOG_LEVELS_ALLOWED, logging.level, before.level, warnings, "logging");
        ReadString(*section, "filePath", logging.filePath, before.filePath, warnings, "logging");
        ReadInt(*section, LOG_FILE_SIZE_MB_LIMITS, logging.maxFileSizeMb, before.maxFileSizeMb, warnings, "logging");
        ReadInt(*section, LOG_FILES_LIMITS, logging.maxFiles, before.maxFiles, warnings, "logging");
        WarnUnknownKeys(*section, "logging",
            { "level", "filePath", LOG_FILE_SIZE_MB_LIMITS.key, LOG_FILES_LIMITS.key },
            warnings);
    }

    return result;
}

namespace {

// The settings file that the application writes is filled with comments, so it
// doubles as the reference: every parameter is explained next to its value.
// Text::StripJsonComments() removes the comments when the file is read back,
// and Config::Write() verifies that the result still parses.
class SettingsDocument {
public:
    void Blank() {
        m_text += "\n";
    }

    void Comment(int indent, const std::string& comment) {
        m_text += std::string(static_cast<size_t>(indent), ' ') + "// " + comment + "\n";
    }

    void Line(int indent, const std::string& line) {
        m_text += std::string(static_cast<size_t>(indent), ' ') + line + "\n";
    }

    // A key without a comment: service fields that the user must not change.
    template <typename T>
    void PlainKey(int indent, const char* name, const T& value, bool comma = true) {
        Line(indent, std::string("\"") + name + "\": " + JsonValue(value) + (comma ? "," : ""));
    }

    // The explanation follows the value on the same line: the file stays
    // compact and a value is read together with what it means.
    void InlineComment(const std::string& comment) {
        if (!comment.empty()) {
            m_text += "  // " + comment;
        }
    }

    template <typename T>
    void Key(int indent, const char* name, const T& value, const std::string& comment, bool comma = true) {
        m_text += std::string(static_cast<size_t>(indent), ' ') + "\"" + name + "\": " + JsonValue(value) + (comma ? "," : "");
        InlineComment(comment);
        m_text += "\n";
    }

    void SectionOpen(int indent, const char* name, const std::string& comment) {
        m_text += std::string(static_cast<size_t>(indent), ' ') + "\"" + name + "\": {";
        InlineComment(comment);
        m_text += "\n";
    }

    void SectionClose(int indent, bool comma) {
        Line(indent, comma ? "}," : "}");
    }

    const std::string& Text() const {
        return m_text;
    }

private:
    std::string m_text;
};

}

std::string miniant::Config::ToJsonString(const Settings& settings) {
    json root;

    root["configVersion"] = settings.configVersion;
    root["commentLanguage"] = settings.commentLanguage;

    json& application = root["application"];
    application["language"] = settings.application.language;
    application["startMinimizedToTray"] = settings.application.startMinimizedToTray;
    application["minimizeToTray"] = settings.application.minimizeToTray;
    application["closeButtonAction"] = ToString(settings.application.closeButtonAction);
    application["startWithWindows"] = settings.application.startWithWindows;

    json& notifications = root["tray"]["notifications"];
    notifications["onError"] = settings.tray.notifications.onError;
    notifications["onDeviceChange"] = settings.tray.notifications.onDeviceChange;
    notifications["onStateChange"] = settings.tray.notifications.onStateChange;

    json& menu = root["tray"]["menu"];
    menu["toggleEnabled"] = settings.tray.menu.toggleEnabled;
    menu["reinitialize"] = settings.tray.menu.reinitialize;
    menu["openLog"] = settings.tray.menu.openLog;
    menu["diagnostics"] = settings.tray.menu.diagnostics;

    json& tray = root["tray"];
    tray["enabled"] = settings.tray.enabled;

    json& audio = root["audio"];
    audio["dataFlow"] = ToString(settings.audio.dataFlow);
    audio["buffer"] = ToString(settings.audio.buffer);
    audio[FIXED_BUFFER_FRAMES_LIMITS.key] = settings.audio.fixedBufferFrames;

    json& reinit = root["audio"]["reinit"];
    reinit["defaultDeviceChanged"] = settings.audio.reinit.defaultDeviceChanged;
    reinit["resumeFromSleep"] = settings.audio.reinit.resumeFromSleep;
    reinit["sessionUnlock"] = settings.audio.reinit.sessionUnlock;
    reinit[FAILURE_TIMEOUT_SEC_LIMITS.key] = settings.audio.reinit.failureTimeoutSec;
    reinit[DEBOUNCE_SEC_LIMITS.key] = settings.audio.reinit.debounceSec;

    json& performance = root["performance"];
    performance["processPriority"] = ToString(settings.performance.processPriority);

    json& updates = root["updates"];
    updates["checkOnStartup"] = settings.updates.checkOnStartup;

    json& logging = root["logging"];
    logging["level"] = settings.logging.level;
    logging["filePath"] = settings.logging.filePath;
    logging[LOG_FILE_SIZE_MB_LIMITS.key] = settings.logging.maxFileSizeMb;
    logging[LOG_FILES_LIMITS.key] = settings.logging.maxFiles;

    return root.dump(2);
}

std::string miniant::Config::ToDocumentedJsonString(const Settings& settings) {
    SettingsDocument document;

    auto text = [](Str id) { return std::string(Lang::Utf8(id)); };

    document.Comment(0, text(Str::CfgFileHeader));
    // The address of the description of every setting, in the language of the
    // comments.
    document.Comment(0, fmt::format(text(Str::CfgFileDocs), AppInfo::DOCS_URL + text(Str::DocsSettingsPath)));
    document.Line(0, "{");
    // Service fields: documented in docs/settings.en.md and docs/settings.ru.md,
    // not next to the value.
    document.PlainKey(2, "configVersion", settings.configVersion, true);
    document.PlainKey(2, "commentLanguage", settings.commentLanguage, true);
    document.Blank();

    document.SectionOpen(2, "application", text(Str::CfgApplicationSection));
    document.Key(4, "language", settings.application.language, text(Str::CfgLanguage), true);
    document.Key(4, "startMinimizedToTray", settings.application.startMinimizedToTray, text(Str::CfgStartMinimizedToTray), true);
    document.Key(4, "minimizeToTray", settings.application.minimizeToTray, text(Str::CfgMinimizeToTray), true);
    document.Key(4, "closeButtonAction", ToString(settings.application.closeButtonAction), text(Str::CfgCloseButtonAction), true);
    document.Key(4, "startWithWindows", settings.application.startWithWindows, text(Str::CfgStartWithWindows), false);
    document.SectionClose(2, true);
    document.Blank();

    document.SectionOpen(2, "tray", text(Str::CfgTraySection));
    document.Key(4, "enabled", settings.tray.enabled, text(Str::CfgTrayEnabled), true);
    document.SectionOpen(4, "notifications", text(Str::CfgNotificationsSection));
    document.Key(6, "onError", settings.tray.notifications.onError, text(Str::CfgNotifyOnError), true);
    document.Key(6, "onDeviceChange", settings.tray.notifications.onDeviceChange, text(Str::CfgNotifyOnDeviceChange), true);
    document.Key(6, "onStateChange", settings.tray.notifications.onStateChange, text(Str::CfgNotifyOnStateChange), false);
    document.SectionClose(4, true);
    document.SectionOpen(4, "menu", text(Str::CfgMenuSection));
    document.Key(6, "toggleEnabled", settings.tray.menu.toggleEnabled, text(Str::CfgMenuToggle), true);
    document.Key(6, "reinitialize", settings.tray.menu.reinitialize, text(Str::CfgMenuReinitialize), true);
    document.Key(6, "openLog", settings.tray.menu.openLog, text(Str::CfgMenuLog), true);
    document.Key(6, "diagnostics", settings.tray.menu.diagnostics, text(Str::CfgMenuDiagnostics), false);
    document.SectionClose(4, false);
    document.SectionClose(2, true);
    document.Blank();

    document.SectionOpen(2, "audio", text(Str::CfgAudioSection));
    document.Key(4, "dataFlow", ToString(settings.audio.dataFlow), text(Str::CfgDataFlow), true);
    document.Key(4, "buffer", ToString(settings.audio.buffer), text(Str::CfgBuffer), true);
    document.Key(4, FIXED_BUFFER_FRAMES_LIMITS.key, settings.audio.fixedBufferFrames, text(Str::CfgFixedBufferFrames), true);
    document.SectionOpen(4, "reinit", text(Str::CfgReinitSection));
    document.Key(6, "defaultDeviceChanged", settings.audio.reinit.defaultDeviceChanged, text(Str::CfgReinitDeviceChanged), true);
    document.Key(6, "resumeFromSleep", settings.audio.reinit.resumeFromSleep, text(Str::CfgReinitResume), true);
    document.Key(6, "sessionUnlock", settings.audio.reinit.sessionUnlock, text(Str::CfgReinitUnlock), true);
    document.Key(6, FAILURE_TIMEOUT_SEC_LIMITS.key, settings.audio.reinit.failureTimeoutSec,
        fmt::format(
            Lang::Utf8(Str::CfgReinitFailureTimeout),
            FAILURE_TIMEOUT_SEC_LIMITS.minimum, FAILURE_TIMEOUT_SEC_LIMITS.maximum),
        true);
    document.Key(6, DEBOUNCE_SEC_LIMITS.key, settings.audio.reinit.debounceSec,
        fmt::format(Lang::Utf8(Str::CfgReinitDebounce), DEBOUNCE_SEC_LIMITS.minimum, DEBOUNCE_SEC_LIMITS.maximum),
        false);
    document.SectionClose(4, false);
    document.SectionClose(2, true);
    document.Blank();

    document.SectionOpen(2, "performance", text(Str::CfgPerformanceSection));
    document.Key(4, "processPriority", ToString(settings.performance.processPriority), text(Str::CfgProcessPriority), false);
    document.SectionClose(2, true);
    document.Blank();

    document.SectionOpen(2, "updates", text(Str::CfgUpdatesSection));
    document.Key(4, "checkOnStartup", settings.updates.checkOnStartup, text(Str::CfgUpdatesCheckOnStartup), false);
    document.SectionClose(2, true);
    document.Blank();

    document.SectionOpen(2, "logging", text(Str::CfgLoggingSection));
    document.Key(4, "level", settings.logging.level, text(Str::CfgLoggingLevel), true);
    document.Key(4, "filePath", settings.logging.filePath, text(Str::CfgLoggingFilePath), true);
    document.Key(4, LOG_FILE_SIZE_MB_LIMITS.key, settings.logging.maxFileSizeMb,
        fmt::format(Lang::Utf8(Str::CfgLoggingMaxFileSize), LOG_FILE_SIZE_MB_LIMITS.minimum, LOG_FILE_SIZE_MB_LIMITS.maximum),
        true);
    document.Key(4, LOG_FILES_LIMITS.key, settings.logging.maxFiles,
        fmt::format(Lang::Utf8(Str::CfgLoggingMaxFiles), LOG_FILES_LIMITS.minimum, LOG_FILES_LIMITS.maximum),
        false);
    document.SectionClose(2, false);
    document.Line(0, "}");

    return document.Text();
}

bool miniant::Config::Write(const Settings& settings, const std::wstring& path) {
    // The file remembers the language its comments are written in; when the
    // caller did not set it (first run, example file), the current one is used.
    Settings copy = settings;
    if (copy.commentLanguage.empty()) {
        copy.commentLanguage = Lang::Code(Lang::Current());
    }

    std::string content = ToDocumentedJsonString(copy);

    // A settings file that cannot be read back would be worse than an
    // undocumented one, so fall back to plain JSON when anything is off.
    try {
        const json parsed = json::parse(Text::StripJsonComments(Text::StripUtf8Bom(content)));
        if (!parsed.is_object()) {
            content = ToJsonString(copy);
        }
    } catch (const json::exception&) {
        content = ToJsonString(copy);
    }

    return Windows::Filesystem::WriteTextFileUtf8Atomic(path, content + "\n");
}

std::string miniant::Config::DescribeFlow(const Settings& settings) {
    // Only the values that take part in the latency reduction: window and tray
    // settings do not change what the application does with the audio.
    switch (settings.audio.dataFlow) {
        case DataFlow::Capture: return Lang::Utf8(Str::DescribeFlowCapture);
        case DataFlow::Both: return Lang::Utf8(Str::DescribeFlowBoth);
        default: return Lang::Utf8(Str::DescribeFlowRender);
    }
}

std::string miniant::Config::DescribeBuffer(const Settings& settings, Language language) {
    if (settings.audio.buffer == BufferMode::Fixed && settings.audio.fixedBufferFrames > 0) {
        return fmt::format(
            Lang::Utf8(Str::DescribeBufferFixed, language), Lang::Frames(settings.audio.fixedBufferFrames, language));
    }

    return Lang::Utf8(Str::DescribeBufferMinimum, language);
}

namespace {

// One string field of the file, read without touching the rest of it. The
// section is empty for a field of the root object.
std::string PeekString(const std::wstring& path, const char* section, const char* key) {
    bool readSucceeded = false;
    const std::string content = Windows::Filesystem::ReadTextFileUtf8(path, &readSucceeded, MAX_FILE_BYTES);
    if (!readSucceeded) {
        return {};
    }

    std::string text = Text::StripUtf8Bom(content);
    text = Text::StripJsonComments(text);

    try {
        const json root = json::parse(text);
        if (!root.is_object()) {
            return {};
        }

        const json* object = &root;

        if (section != nullptr) {
            const auto sectionIt = root.find(section);
            if (sectionIt == root.end() || !sectionIt->is_object()) {
                return {};
            }

            object = &(*sectionIt);
        }

        const auto it = object->find(key);
        if (it != object->end() && it->is_string()) {
            return it->get<std::string>();
        }
    } catch (const json::exception&) {
        return {};
    }

    return {};
}

}

std::string miniant::Config::PeekCommentLanguage(const std::wstring& path) {
    return PeekString(path, nullptr, "commentLanguage");
}

std::string miniant::Config::PeekLanguage(const std::wstring& path) {
    return PeekString(path, "application", "language");
}
