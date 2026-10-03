#include "Settings.h"

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

template <typename T, size_t N>
void ReadEnum(
    const json& section,
    const char* key,
    const std::pair<const char*, T> (&table)[N],
    T& target,
    std::vector<std::string>& warnings,
    const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_string()) {
        warnings.push_back(fmt::format(Lang::Utf8(Str::CfgWarnString), KeyName(sectionName, key)));
        return;
    }

    const std::string value = Text::ToLowerAscii(Text::Trim(it->get<std::string>()));
    for (size_t i = 0; i < N; ++i) {
        if (value == table[i].first) {
            target = table[i].second;
            return;
        }
    }

    std::string allowed;
    for (size_t i = 0; i < N; ++i) {
        if (!allowed.empty()) {
            allowed += ", ";
        }

        allowed += table[i].first;
    }

    warnings.push_back(
        fmt::format(Lang::Utf8(Str::CfgWarnUnknownValue), KeyName(sectionName, key), value, allowed));
}

void ReadBool(const json& section, const char* key, bool& target, std::vector<std::string>& warnings, const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_boolean()) {
        warnings.push_back(fmt::format(Lang::Utf8(Str::CfgWarnBool), KeyName(sectionName, key)));
        return;
    }

    target = it->get<bool>();
}

void ReadInt(const json& section, const char* key, int& target, int minValue, int maxValue, std::vector<std::string>& warnings, const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_number_integer()) {
        warnings.push_back(fmt::format(Lang::Utf8(Str::CfgWarnInteger), KeyName(sectionName, key)));
        return;
    }

    const int value = it->get<int>();
    if (value < minValue || value > maxValue) {
        warnings.push_back(fmt::format(Lang::Utf8(Str::CfgWarnRange), KeyName(sectionName, key)));
        return;
    }

    target = value;
}

void ReadUnsigned(const json& section, const char* key, unsigned int& target, unsigned int maxValue, std::vector<std::string>& warnings, const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_number_integer()) {
        warnings.push_back(fmt::format(Lang::Utf8(Str::CfgWarnInteger), KeyName(sectionName, key)));
        return;
    }

    const int value = it->get<int>();
    if (value < 0 || static_cast<unsigned int>(value) > maxValue) {
        warnings.push_back(fmt::format(Lang::Utf8(Str::CfgWarnRange), KeyName(sectionName, key)));
        return;
    }

    target = static_cast<unsigned int>(value);
}

void ReadString(const json& section, const char* key, std::string& target, std::vector<std::string>& warnings, const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_string()) {
        warnings.push_back(fmt::format(Lang::Utf8(Str::CfgWarnString), KeyName(sectionName, key)));
        return;
    }

    target = it->get<std::string>();
}

void WarnUnknownKeys(
    const json& section,
    const std::string& sectionName,
    std::initializer_list<const char*> known,
    std::vector<std::string>& warnings) {
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
            warnings.push_back(fmt::format(Lang::Utf8(Str::CfgWarnUnknownKey), KeyName(sectionName, key)));
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

}

bool miniant::Config::IsLogFileOff(const LoggingSettings& logging) {
    const std::string level = Text::ToLowerAscii(Text::Trim(logging.level));
    return level == "off" || level == "none";
}

std::wstring miniant::Config::GetDefaultPath() {
    return Windows::Filesystem::JoinPath(Windows::Filesystem::GetExecutableDirectory(), L"real.settings.json");
}

LoadResult miniant::Config::Load(const std::wstring& path) {
    LoadResult result;

    bool readSucceeded = false;
    std::string content = Windows::Filesystem::ReadTextFileUtf8(path, &readSucceeded);
    if (!readSucceeded) {
        // A file that exists but cannot be read is not the same as no file at
        // all: the program says so and starts on the defaults without touching
        // the file of the user.
        result.fileExists = Windows::Filesystem::IsFile(path);
        if (result.fileExists) {
            result.parseFailed = true;
            result.error = Lang::Utf8(Str::CfgErrRead);
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
        result.error = fmt::format(Lang::Utf8(Str::CfgErrParse), error.what());
        return result;
    }

    if (!root.is_object()) {
        result.parseFailed = true;
        result.error = Lang::Utf8(Str::CfgErrNotObject);
        return result;
    }

    Settings& settings = result.settings;

    ReadInt(root, "configVersion", settings.configVersion, 1, 1000, result.warnings, "");
    ReadString(root, "commentLanguage", settings.commentLanguage, result.warnings, "");
    WarnUnknownKeys(root, "",
        { "configVersion", "commentLanguage", "application", "tray", "audio", "performance", "updates", "logging" },
        result.warnings);

    if (const json* section = FindSection(root, "application")) {
        ReadString(*section, "language", settings.application.language, result.warnings, "application");
        ReadBool(*section, "startMinimizedToTray", settings.application.startMinimizedToTray, result.warnings, "application");
        ReadBool(*section, "minimizeToTray", settings.application.minimizeToTray, result.warnings, "application");
        ReadEnum(*section, "closeButtonAction", CLOSE_ACTION_MAP, settings.application.closeButtonAction, result.warnings, "application");
        ReadBool(*section, "startWithWindows", settings.application.startWithWindows, result.warnings, "application");
        WarnUnknownKeys(*section, "application",
            { "language", "startMinimizedToTray", "minimizeToTray", "closeButtonAction", "startWithWindows" },
            result.warnings);
    }

    if (const json* section = FindSection(root, "tray")) {
        ReadBool(*section, "enabled", settings.tray.enabled, result.warnings, "tray");

        if (const json* notifications = FindSection(*section, "notifications")) {
            ReadBool(*notifications, "onError", settings.tray.notifications.onError, result.warnings, "tray.notifications");
            ReadBool(*notifications, "onDeviceChange", settings.tray.notifications.onDeviceChange, result.warnings, "tray.notifications");
            ReadBool(*notifications, "onStateChange", settings.tray.notifications.onStateChange, result.warnings, "tray.notifications");
            WarnUnknownKeys(*notifications, "tray.notifications", { "onError", "onDeviceChange", "onStateChange" }, result.warnings);
        }

        if (const json* menu = FindSection(*section, "menu")) {
            ReadBool(*menu, "toggleEnabled", settings.tray.menu.toggleEnabled, result.warnings, "tray.menu");
            ReadBool(*menu, "reinitialize", settings.tray.menu.reinitialize, result.warnings, "tray.menu");
            ReadBool(*menu, "openLog", settings.tray.menu.openLog, result.warnings, "tray.menu");
            ReadBool(*menu, "diagnostics", settings.tray.menu.diagnostics, result.warnings, "tray.menu");
            WarnUnknownKeys(*menu, "tray.menu",
                { "toggleEnabled", "reinitialize", "openLog", "diagnostics" },
                result.warnings);
        }

        WarnUnknownKeys(*section, "tray", { "enabled", "notifications", "menu" }, result.warnings);
    }

    if (const json* section = FindSection(root, "audio")) {
        ReadEnum(*section, "dataFlow", DATA_FLOW_MAP, settings.audio.dataFlow, result.warnings, "audio");
        ReadEnum(*section, "buffer", BUFFER_MODE_MAP, settings.audio.buffer, result.warnings, "audio");
        ReadUnsigned(*section, "fixedBufferFrames", settings.audio.fixedBufferFrames,
            FIXED_BUFFER_FRAMES_MAX, result.warnings, "audio");

        // A fixed buffer without a size means nothing: the smallest buffer is
        // taken instead, and the window shows that choice.
        if (settings.audio.buffer == BufferMode::Fixed && settings.audio.fixedBufferFrames == 0) {
            result.warnings.push_back(
                fmt::format(Lang::Utf8(Str::CfgWarnFixedBufferZero), KeyName("audio", "fixedBufferFrames")));
            settings.audio.buffer = BufferMode::Minimum;
        }

        if (const json* reinit = FindSection(*section, "reinit")) {
            ReadBool(*reinit, "defaultDeviceChanged", settings.audio.reinit.defaultDeviceChanged, result.warnings, "audio.reinit");
            ReadBool(*reinit, "deviceStateChanged", settings.audio.reinit.deviceStateChanged, result.warnings, "audio.reinit");
            ReadBool(*reinit, "deviceAdded", settings.audio.reinit.deviceAdded, result.warnings, "audio.reinit");
            ReadBool(*reinit, "deviceRemoved", settings.audio.reinit.deviceRemoved, result.warnings, "audio.reinit");
            ReadBool(*reinit, "resumeFromSleep", settings.audio.reinit.resumeFromSleep, result.warnings, "audio.reinit");
            ReadBool(*reinit, "sessionUnlock", settings.audio.reinit.sessionUnlock, result.warnings, "audio.reinit");
            ReadBool(*reinit, "enableWhenDisabled", settings.audio.reinit.enableWhenDisabled, result.warnings, "audio.reinit");
            ReadInt(*reinit, "failureTimeoutMs", settings.audio.reinit.failureTimeoutMs,
                FAILURE_TIMEOUT_MS_MIN, FAILURE_TIMEOUT_MS_MAX, result.warnings, "audio.reinit");
            ReadInt(*reinit, "debounceMs", settings.audio.reinit.debounceMs,
                DEBOUNCE_MS_MIN, DEBOUNCE_MS_MAX, result.warnings, "audio.reinit");
            WarnUnknownKeys(*reinit, "audio.reinit",
                { "defaultDeviceChanged", "deviceStateChanged", "deviceAdded", "deviceRemoved", "resumeFromSleep", "sessionUnlock", "enableWhenDisabled", "failureTimeoutMs", "debounceMs" },
                result.warnings);
        }

        WarnUnknownKeys(*section, "audio",
            { "dataFlow", "buffer", "fixedBufferFrames", "reinit" },
            result.warnings);
    }

    if (const json* section = FindSection(root, "performance")) {
        ReadEnum(*section, "processPriority", PROCESS_PRIORITY_MAP, settings.performance.processPriority, result.warnings, "performance");
        WarnUnknownKeys(*section, "performance", { "processPriority" }, result.warnings);
    }

    if (const json* section = FindSection(root, "updates")) {
        ReadBool(*section, "checkOnStartup", settings.updates.checkOnStartup, result.warnings, "updates");
        WarnUnknownKeys(*section, "updates", { "checkOnStartup" }, result.warnings);
    }

    if (const json* section = FindSection(root, "logging")) {
        ReadString(*section, "level", settings.logging.level, result.warnings, "logging");
        ReadString(*section, "filePath", settings.logging.filePath, result.warnings, "logging");
        ReadInt(*section, "maxFileSizeMb", settings.logging.maxFileSizeMb,
            LOG_FILE_SIZE_MB_MIN, LOG_FILE_SIZE_MB_MAX, result.warnings, "logging");
        ReadInt(*section, "maxFiles", settings.logging.maxFiles,
            LOG_FILES_MIN, LOG_FILES_MAX, result.warnings, "logging");
        WarnUnknownKeys(*section, "logging",
            { "level", "filePath", "maxFileSizeMb", "maxFiles" },
            result.warnings);
    }

    return result;
}

namespace {

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
    audio["fixedBufferFrames"] = settings.audio.fixedBufferFrames;

    json& reinit = root["audio"]["reinit"];
    reinit["defaultDeviceChanged"] = settings.audio.reinit.defaultDeviceChanged;
    reinit["deviceStateChanged"] = settings.audio.reinit.deviceStateChanged;
    reinit["deviceAdded"] = settings.audio.reinit.deviceAdded;
    reinit["deviceRemoved"] = settings.audio.reinit.deviceRemoved;
    reinit["resumeFromSleep"] = settings.audio.reinit.resumeFromSleep;
    reinit["sessionUnlock"] = settings.audio.reinit.sessionUnlock;
    reinit["enableWhenDisabled"] = settings.audio.reinit.enableWhenDisabled;
    reinit["failureTimeoutMs"] = settings.audio.reinit.failureTimeoutMs;
    reinit["debounceMs"] = settings.audio.reinit.debounceMs;

    json& performance = root["performance"];
    performance["processPriority"] = ToString(settings.performance.processPriority);

    json& updates = root["updates"];
    updates["checkOnStartup"] = settings.updates.checkOnStartup;

    json& logging = root["logging"];
    logging["level"] = settings.logging.level;
    logging["filePath"] = settings.logging.filePath;
    logging["maxFileSizeMb"] = settings.logging.maxFileSizeMb;
    logging["maxFiles"] = settings.logging.maxFiles;

    return root.dump(2);
}

std::string miniant::Config::ToDocumentedJsonString(const Settings& settings) {
    SettingsDocument document;

    auto text = [](Str id) { return std::string(Lang::Utf8(id)); };

    document.Comment(0, text(Str::CfgFileHeader));
    document.Line(0, "{");
    // Service fields: documented in CONFIG.md, not next to the value.
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
    document.Key(4, "fixedBufferFrames", settings.audio.fixedBufferFrames, text(Str::CfgFixedBufferFrames), true);
    document.SectionOpen(4, "reinit", text(Str::CfgReinitSection));
    document.Key(6, "defaultDeviceChanged", settings.audio.reinit.defaultDeviceChanged, text(Str::CfgReinitDeviceChanged), true);
    document.Key(6, "deviceStateChanged", settings.audio.reinit.deviceStateChanged, text(Str::CfgReinitDeviceState), true);
    document.Key(6, "deviceAdded", settings.audio.reinit.deviceAdded, text(Str::CfgReinitDeviceAdded), true);
    document.Key(6, "deviceRemoved", settings.audio.reinit.deviceRemoved, text(Str::CfgReinitDeviceRemoved), true);
    document.Key(6, "resumeFromSleep", settings.audio.reinit.resumeFromSleep, text(Str::CfgReinitResume), true);
    document.Key(6, "sessionUnlock", settings.audio.reinit.sessionUnlock, text(Str::CfgReinitUnlock), true);
    document.Key(6, "enableWhenDisabled", settings.audio.reinit.enableWhenDisabled, text(Str::CfgReinitEnableWhenDisabled), true);
    document.Key(6, "failureTimeoutMs", settings.audio.reinit.failureTimeoutMs, text(Str::CfgReinitFailureTimeout), true);
    document.Key(6, "debounceMs", settings.audio.reinit.debounceMs, text(Str::CfgReinitDebounce), false);
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
    document.Key(4, "maxFileSizeMb", settings.logging.maxFileSizeMb, text(Str::CfgLoggingMaxFileSize), true);
    document.Key(4, "maxFiles", settings.logging.maxFiles, text(Str::CfgLoggingMaxFiles), false);
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

std::string miniant::Config::DescribeBuffer(const Settings& settings) {
    if (settings.audio.buffer == BufferMode::Fixed && settings.audio.fixedBufferFrames > 0) {
        return fmt::format(Lang::Utf8(Str::DescribeBufferFixed), Lang::Frames(settings.audio.fixedBufferFrames));
    }

    return Lang::Utf8(Str::DescribeBufferMinimum);
}

namespace {

// One string field of the file, read without touching the rest of it. The
// section is empty for a field of the root object.
std::string PeekString(const std::wstring& path, const char* section, const char* key) {
    bool readSucceeded = false;
    const std::string content = Windows::Filesystem::ReadTextFileUtf8(path, &readSucceeded);
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
