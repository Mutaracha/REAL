#include "Settings.h"

#include "Text.h"
#include "Windows/Filesystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <utility>

using json = nlohmann::json;
using namespace miniant::Config;

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

const std::pair<const char*, DeviceRole> DEVICE_ROLE_MAP[] = {
    { "console", DeviceRole::Console },
    { "multimedia", DeviceRole::Multimedia },
    { "communications", DeviceRole::Communications },
};

const std::pair<const char*, PeriodSelection> PERIOD_SELECTION_MAP[] = {
    { "min", PeriodSelection::Minimum },
    { "fundamental", PeriodSelection::Fundamental },
    { "fixed", PeriodSelection::Fixed },
};

const std::pair<const char*, ProcessPriority> PROCESS_PRIORITY_MAP[] = {
    { "normal", ProcessPriority::Normal },
    { "belownormal", ProcessPriority::BelowNormal },
    { "idle", ProcessPriority::Idle },
};

const std::pair<const char*, UpdatesMode> UPDATES_MODE_MAP[] = {
    { "off", UpdatesMode::Off },
    { "manual", UpdatesMode::Manual },
};

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
        warnings.push_back(sectionName + "." + key + ": expected a string value, using default");
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

    warnings.push_back(sectionName + "." + key + ": unknown value '" + value + "' (allowed: " + allowed + "), using default");
}

void ReadBool(const json& section, const char* key, bool& target, std::vector<std::string>& warnings, const std::string& sectionName) {
    const auto it = section.find(key);
    if (it == section.end()) {
        return;
    }

    if (!it->is_boolean()) {
        warnings.push_back(sectionName + "." + key + ": expected true/false, using default");
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
        warnings.push_back(sectionName + "." + key + ": expected an integer, using default");
        return;
    }

    const int value = it->get<int>();
    if (value < minValue || value > maxValue) {
        warnings.push_back(sectionName + "." + key + ": value out of range, using default");
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
        warnings.push_back(sectionName + "." + key + ": expected an integer, using default");
        return;
    }

    const int value = it->get<int>();
    if (value < 0 || static_cast<unsigned int>(value) > maxValue) {
        warnings.push_back(sectionName + "." + key + ": value out of range, using default");
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
        warnings.push_back(sectionName + "." + key + ": expected a string, using default");
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
            warnings.push_back(sectionName + "." + key + ": unknown option (ignored)");
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

const char* ToString(DeviceRole value) {
    switch (value) {
        case DeviceRole::Multimedia: return "multimedia";
        case DeviceRole::Communications: return "communications";
        default: return "console";
    }
}

const char* ToString(PeriodSelection value) {
    switch (value) {
        case PeriodSelection::Fundamental: return "fundamental";
        case PeriodSelection::Fixed: return "fixed";
        default: return "min";
    }
}

const char* ToString(ProcessPriority value) {
    switch (value) {
        case ProcessPriority::BelowNormal: return "belowNormal";
        case ProcessPriority::Idle: return "idle";
        default: return "normal";
    }
}

const char* ToString(UpdatesMode value) {
    return value == UpdatesMode::Manual ? "manual" : "off";
}

}

std::wstring miniant::Config::GetDefaultPath() {
    return Windows::Filesystem::JoinPath(Windows::Filesystem::GetExecutableDirectory(), L"real.settings.json");
}

LoadResult miniant::Config::Load(const std::wstring& path) {
    LoadResult result;

    bool readSucceeded = false;
    std::string content = Windows::Filesystem::ReadTextFileUtf8(path, &readSucceeded);
    if (!readSucceeded) {
        result.fileExists = false;
        return result;
    }

    result.fileExists = true;

    content = Text::StripJsonComments(Text::StripUtf8Bom(content));

    json root;
    try {
        root = json::parse(content);
    } catch (const json::exception& error) {
        result.parseFailed = true;
        result.error = std::string("Could not parse the settings file: ") + error.what();
        return result;
    }

    if (!root.is_object()) {
        result.parseFailed = true;
        result.error = "The settings file must contain a JSON object.";
        return result;
    }

    Settings& settings = result.settings;

    ReadInt(root, "configVersion", settings.configVersion, 1, 1000, result.warnings, "configVersion");
    WarnUnknownKeys(root, "root", { "configVersion", "application", "tray", "audio", "performance", "updates", "hotkeys", "logging" }, result.warnings);

    if (const json* section = FindSection(root, "application")) {
        ReadBool(*section, "startMinimizedToTray", settings.application.startMinimizedToTray, result.warnings, "application");
        ReadBool(*section, "minimizeToTray", settings.application.minimizeToTray, result.warnings, "application");
        ReadEnum(*section, "closeButtonAction", CLOSE_ACTION_MAP, settings.application.closeButtonAction, result.warnings, "application");
        ReadBool(*section, "showConsole", settings.application.showConsole, result.warnings, "application");
        ReadBool(*section, "singleInstance", settings.application.singleInstance, result.warnings, "application");
        ReadBool(*section, "startWithWindows", settings.application.startWithWindows, result.warnings, "application");
        WarnUnknownKeys(*section, "application",
            { "startMinimizedToTray", "minimizeToTray", "closeButtonAction", "showConsole", "singleInstance", "startWithWindows" },
            result.warnings);
    }

    if (const json* section = FindSection(root, "tray")) {
        ReadBool(*section, "enabled", settings.tray.enabled, result.warnings, "tray");
        ReadBool(*section, "showStatusInTooltip", settings.tray.showStatusInTooltip, result.warnings, "tray");

        if (const json* notifications = FindSection(*section, "notifications")) {
            ReadBool(*notifications, "onError", settings.tray.notifications.onError, result.warnings, "tray.notifications");
            ReadBool(*notifications, "onDeviceChange", settings.tray.notifications.onDeviceChange, result.warnings, "tray.notifications");
            ReadBool(*notifications, "onStateChange", settings.tray.notifications.onStateChange, result.warnings, "tray.notifications");
            WarnUnknownKeys(*notifications, "tray.notifications", { "onError", "onDeviceChange", "onStateChange" }, result.warnings);
        }

        if (const json* menu = FindSection(*section, "menu")) {
            ReadBool(*menu, "showStatus", settings.tray.menu.showStatus, result.warnings, "tray.menu");
            ReadBool(*menu, "toggleEnabled", settings.tray.menu.toggleEnabled, result.warnings, "tray.menu");
            ReadBool(*menu, "reinitialize", settings.tray.menu.reinitialize, result.warnings, "tray.menu");
            ReadBool(*menu, "openSettings", settings.tray.menu.openSettings, result.warnings, "tray.menu");
            ReadBool(*menu, "openLog", settings.tray.menu.openLog, result.warnings, "tray.menu");
            ReadBool(*menu, "checkForUpdates", settings.tray.menu.checkForUpdates, result.warnings, "tray.menu");
            ReadBool(*menu, "startWithWindows", settings.tray.menu.startWithWindows, result.warnings, "tray.menu");
            ReadBool(*menu, "about", settings.tray.menu.about, result.warnings, "tray.menu");
            ReadBool(*menu, "exit", settings.tray.menu.exit, result.warnings, "tray.menu");
            WarnUnknownKeys(*menu, "tray.menu",
                { "showStatus", "toggleEnabled", "reinitialize", "openSettings", "openLog", "checkForUpdates", "startWithWindows", "about", "exit" },
                result.warnings);
        }

        WarnUnknownKeys(*section, "tray", { "enabled", "showStatusInTooltip", "notifications", "menu" }, result.warnings);
    }

    if (const json* section = FindSection(root, "audio")) {
        ReadBool(*section, "enabledOnStartup", settings.audio.enabledOnStartup, result.warnings, "audio");
        ReadEnum(*section, "dataFlow", DATA_FLOW_MAP, settings.audio.dataFlow, result.warnings, "audio");
        ReadEnum(*section, "role", DEVICE_ROLE_MAP, settings.audio.role, result.warnings, "audio");
        ReadEnum(*section, "periodSelection", PERIOD_SELECTION_MAP, settings.audio.periodSelection, result.warnings, "audio");
        ReadUnsigned(*section, "requestedPeriodFrames", settings.audio.requestedPeriodFrames, 0xFFFFFFFFu, result.warnings, "audio");
        ReadBool(*section, "allowPeriodSnap", settings.audio.allowPeriodSnap, result.warnings, "audio");
        ReadBool(*section, "releaseOnExit", settings.audio.releaseOnExit, result.warnings, "audio");

        if (const json* reinit = FindSection(*section, "reinit")) {
            ReadBool(*reinit, "defaultDeviceChanged", settings.audio.reinit.defaultDeviceChanged, result.warnings, "audio.reinit");
            ReadBool(*reinit, "deviceStateChanged", settings.audio.reinit.deviceStateChanged, result.warnings, "audio.reinit");
            ReadBool(*reinit, "deviceAdded", settings.audio.reinit.deviceAdded, result.warnings, "audio.reinit");
            ReadBool(*reinit, "deviceRemoved", settings.audio.reinit.deviceRemoved, result.warnings, "audio.reinit");
            ReadBool(*reinit, "resumeFromSleep", settings.audio.reinit.resumeFromSleep, result.warnings, "audio.reinit");
            ReadBool(*reinit, "sessionUnlock", settings.audio.reinit.sessionUnlock, result.warnings, "audio.reinit");
            ReadInt(*reinit, "debounceMs", settings.audio.reinit.debounceMs, 0, 60000, result.warnings, "audio.reinit");
            WarnUnknownKeys(*reinit, "audio.reinit",
                { "defaultDeviceChanged", "deviceStateChanged", "deviceAdded", "deviceRemoved", "resumeFromSleep", "sessionUnlock", "debounceMs" },
                result.warnings);
        }

        WarnUnknownKeys(*section, "audio",
            { "enabledOnStartup", "dataFlow", "role", "periodSelection", "requestedPeriodFrames", "allowPeriodSnap", "releaseOnExit", "reinit" },
            result.warnings);
    }

    if (const json* section = FindSection(root, "performance")) {
        ReadEnum(*section, "processPriority", PROCESS_PRIORITY_MAP, settings.performance.processPriority, result.warnings, "performance");
        ReadBool(*section, "disablePowerThrottling", settings.performance.disablePowerThrottling, result.warnings, "performance");
        WarnUnknownKeys(*section, "performance", { "processPriority", "disablePowerThrottling" }, result.warnings);
    }

    if (const json* section = FindSection(root, "updates")) {
        ReadEnum(*section, "mode", UPDATES_MODE_MAP, settings.updates.mode, result.warnings, "updates");
        ReadString(*section, "repository", settings.updates.repository, result.warnings, "updates");
        ReadInt(*section, "timeoutSeconds", settings.updates.timeoutSeconds, 1, 120, result.warnings, "updates");
        ReadBool(*section, "checkOnStartup", settings.updates.checkOnStartup, result.warnings, "updates");
        WarnUnknownKeys(*section, "updates", { "mode", "repository", "timeoutSeconds", "checkOnStartup" }, result.warnings);
    }

    if (const json* section = FindSection(root, "hotkeys")) {
        ReadBool(*section, "enabled", settings.hotkeys.enabled, result.warnings, "hotkeys");
        ReadString(*section, "toggleEnabled", settings.hotkeys.toggleEnabled, result.warnings, "hotkeys");
        ReadString(*section, "reinitialize", settings.hotkeys.reinitialize, result.warnings, "hotkeys");
        WarnUnknownKeys(*section, "hotkeys", { "enabled", "toggleEnabled", "reinitialize" }, result.warnings);
    }

    if (const json* section = FindSection(root, "logging")) {
        ReadString(*section, "level", settings.logging.level, result.warnings, "logging");
        ReadBool(*section, "toConsole", settings.logging.toConsole, result.warnings, "logging");
        ReadBool(*section, "toFile", settings.logging.toFile, result.warnings, "logging");
        ReadString(*section, "filePath", settings.logging.filePath, result.warnings, "logging");
        ReadInt(*section, "maxFileSizeMb", settings.logging.maxFileSizeMb, 1, 1024, result.warnings, "logging");
        ReadInt(*section, "maxFiles", settings.logging.maxFiles, 1, 100, result.warnings, "logging");
        WarnUnknownKeys(*section, "logging", { "level", "toConsole", "toFile", "filePath", "maxFileSizeMb", "maxFiles" }, result.warnings);
    }

    return result;
}

std::string miniant::Config::ToJsonString(const Settings& settings) {
    json root;

    root["configVersion"] = settings.configVersion;

    json& application = root["application"];
    application["startMinimizedToTray"] = settings.application.startMinimizedToTray;
    application["minimizeToTray"] = settings.application.minimizeToTray;
    application["closeButtonAction"] = ToString(settings.application.closeButtonAction);
    application["showConsole"] = settings.application.showConsole;
    application["singleInstance"] = settings.application.singleInstance;
    application["startWithWindows"] = settings.application.startWithWindows;

    json& notifications = root["tray"]["notifications"];
    notifications["onError"] = settings.tray.notifications.onError;
    notifications["onDeviceChange"] = settings.tray.notifications.onDeviceChange;
    notifications["onStateChange"] = settings.tray.notifications.onStateChange;

    json& menu = root["tray"]["menu"];
    menu["showStatus"] = settings.tray.menu.showStatus;
    menu["toggleEnabled"] = settings.tray.menu.toggleEnabled;
    menu["reinitialize"] = settings.tray.menu.reinitialize;
    menu["openSettings"] = settings.tray.menu.openSettings;
    menu["openLog"] = settings.tray.menu.openLog;
    menu["checkForUpdates"] = settings.tray.menu.checkForUpdates;
    menu["startWithWindows"] = settings.tray.menu.startWithWindows;
    menu["about"] = settings.tray.menu.about;
    menu["exit"] = settings.tray.menu.exit;

    json& tray = root["tray"];
    tray["enabled"] = settings.tray.enabled;
    tray["showStatusInTooltip"] = settings.tray.showStatusInTooltip;

    json& audio = root["audio"];
    audio["enabledOnStartup"] = settings.audio.enabledOnStartup;
    audio["dataFlow"] = ToString(settings.audio.dataFlow);
    audio["role"] = ToString(settings.audio.role);
    audio["periodSelection"] = ToString(settings.audio.periodSelection);
    audio["requestedPeriodFrames"] = settings.audio.requestedPeriodFrames;
    audio["allowPeriodSnap"] = settings.audio.allowPeriodSnap;
    audio["releaseOnExit"] = settings.audio.releaseOnExit;

    json& reinit = root["audio"]["reinit"];
    reinit["defaultDeviceChanged"] = settings.audio.reinit.defaultDeviceChanged;
    reinit["deviceStateChanged"] = settings.audio.reinit.deviceStateChanged;
    reinit["deviceAdded"] = settings.audio.reinit.deviceAdded;
    reinit["deviceRemoved"] = settings.audio.reinit.deviceRemoved;
    reinit["resumeFromSleep"] = settings.audio.reinit.resumeFromSleep;
    reinit["sessionUnlock"] = settings.audio.reinit.sessionUnlock;
    reinit["debounceMs"] = settings.audio.reinit.debounceMs;

    json& performance = root["performance"];
    performance["processPriority"] = ToString(settings.performance.processPriority);
    performance["disablePowerThrottling"] = settings.performance.disablePowerThrottling;

    json& updates = root["updates"];
    updates["mode"] = ToString(settings.updates.mode);
    updates["repository"] = settings.updates.repository;
    updates["timeoutSeconds"] = settings.updates.timeoutSeconds;
    updates["checkOnStartup"] = settings.updates.checkOnStartup;

    json& hotkeys = root["hotkeys"];
    hotkeys["enabled"] = settings.hotkeys.enabled;
    hotkeys["toggleEnabled"] = settings.hotkeys.toggleEnabled;
    hotkeys["reinitialize"] = settings.hotkeys.reinitialize;

    json& logging = root["logging"];
    logging["level"] = settings.logging.level;
    logging["toConsole"] = settings.logging.toConsole;
    logging["toFile"] = settings.logging.toFile;
    logging["filePath"] = settings.logging.filePath;
    logging["maxFileSizeMb"] = settings.logging.maxFileSizeMb;
    logging["maxFiles"] = settings.logging.maxFiles;

    return root.dump(2);
}

bool miniant::Config::Write(const Settings& settings, const std::wstring& path) {
    return Windows::Filesystem::WriteTextFileUtf8(path, ToJsonString(settings) + "\n");
}

std::string miniant::Config::Describe(const Settings& settings) {
    std::string result;
    result += "tray=";
    result += settings.tray.enabled ? "on" : "off";
    result += ", minimizeToTray=";
    result += settings.application.minimizeToTray ? "on" : "off";
    result += ", startMinimized=";
    result += settings.application.startMinimizedToTray ? "on" : "off";
    result += ", closeButton=";
    result += ToString(settings.application.closeButtonAction);
    result += ", dataFlow=";
    result += ToString(settings.audio.dataFlow);
    result += ", role=";
    result += ToString(settings.audio.role);
    result += ", period=";
    result += ToString(settings.audio.periodSelection);
    result += ", updates=";
    result += ToString(settings.updates.mode);
    result += ", priority=";
    result += ToString(settings.performance.processPriority);
    return result;
}
