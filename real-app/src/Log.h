#pragma once

#include <spdlog/fmt/fmt.h>

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace miniant::Config {
struct Settings;
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
    std::vector<std::string> Snapshot() const;

private:
    mutable std::mutex m_mutex;
    std::deque<std::string> m_lines;
    size_t m_limit;
    size_t m_pending = 0;
    std::function<void()> m_notify;
};

LogBuffer& Buffer();

void Initialize(const Config::Settings& settings, bool consoleAttached);
void Shutdown();

// Switches the level at runtime (used by --log-level).
void SetLevel(const std::string& level);

void Write(Level level, const std::string& message);

bool WriteSnapshotToFile(const std::wstring& path);

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
