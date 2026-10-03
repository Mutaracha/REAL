#pragma once

#include <spdlog/fmt/fmt.h>

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace miniant::Config {
struct Settings;
struct LoggingSettings;
}

namespace miniant::Log {

enum class Level {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
};

// Keeps the recent log lines so that the main window can display them and the
// user can save them to a file even when file logging is disabled.
class LogBuffer {
public:
    explicit LogBuffer(size_t limit = 4000);

    void SetNotifyHandler(std::function<void()> handler);
    void Append(const std::string& text);
    std::vector<std::string> TakePending();

private:
    mutable std::mutex m_mutex;
    std::deque<std::string> m_lines;
    size_t m_limit;
    size_t m_pending = 0;
    std::function<void()> m_notify;
};

LogBuffer& Buffer();

void Initialize(const Config::Settings& settings);
void Shutdown();

// Re-applies the settings of the log file at runtime (used when the settings
// window saves them). The window of the program is not affected.
void SetFileSettings(const Config::LoggingSettings& logging);

void Write(Level level, const std::string& message);

// A message about an operation the user performs or watches: it is written to
// the file and shown in the window.
void WriteOperation(const std::string& message);

// A hint for the user (why the log cannot be opened and the like): it is shown
// in the window, but never written to the log file, which stays a record of
// what the program did.
void WriteHint(const std::string& message);

// Writes everything that is still buffered to the sinks. A log line that is
// followed by a crash is otherwise lost (the file sinks cache the output).
void Flush();

// Logs an operation (see WriteOperation); the text is written in the language
// of the interface.
template <typename... Args>
void Operation(const char* format, const Args&... args) {
    WriteOperation(fmt::format(format, args...));
}

template <typename... Args>
void Hint(const char* format, const Args&... args) {
    WriteHint(fmt::format(format, args...));
}

template <typename... Args>
void Trace(const char* format, const Args&... args) {
    Write(Level::Trace, fmt::format(format, args...));
}

template <typename... Args>
void Debug(const char* format, const Args&... args) {
    Write(Level::Debug, fmt::format(format, args...));
}

template <typename... Args>
void Info(const char* format, const Args&... args) {
    Write(Level::Info, fmt::format(format, args...));
}

template <typename... Args>
void Warn(const char* format, const Args&... args) {
    Write(Level::Warn, fmt::format(format, args...));
}

template <typename... Args>
void Error(const char* format, const Args&... args) {
    Write(Level::Error, fmt::format(format, args...));
}

}
