#include "Log.h"

#include "Lang.h"
#include "Settings.h"
#include "Text.h"
#include "Windows/Filesystem.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <system_error>

using namespace miniant;
using namespace miniant::Log;

namespace {

class BufferSink: public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit BufferSink(LogBuffer* buffer):
        m_buffer(buffer) {}

protected:
    void sink_it_(const spdlog::details::log_msg& message) override {
        fmt::memory_buffer formatted;
        formatter_->format(message, formatted);
        m_buffer->Append(std::string(formatted.data(), formatted.size()));
    }

    void flush_() override {}

private:
    LogBuffer* m_buffer;
};

std::shared_ptr<spdlog::logger> g_logger;
// The file of the log is a sink of its own: it carries the level from the
// settings while the window of the program always shows the operations.
std::shared_ptr<spdlog::sinks::sink> g_fileSink;
std::unique_ptr<LogBuffer> g_buffer;
std::wstring g_logFilePath;

// Repeating lines. A device that is being switched off and on again makes the
// application retry, and every retry fails with the very same text: a log full
// of identical lines hides everything else. The first line is written as
// usual, the following identical ones are counted and reported by a single
// line once the run of repetitions is over (see FlushRepeatedMessages).
struct RepeatTracker {
    bool hasLast = false;
    Level level = Level::Info;
    std::string text;
    unsigned int count = 0;
};

RepeatTracker g_repeat;

void WriteToLoggers(Level level, const std::string& message) {
    if (!g_logger) {
        return;
    }

    // Every sink decides for itself: the buffer of the window keeps the info
    // level, the file keeps the level of the settings, and neither of them can
    // silence the other.
    switch (level) {
        case Level::Trace:
            g_logger->trace(message);
            return;
        case Level::Debug:
            g_logger->debug(message);
            return;
        case Level::Warn:
            g_logger->warn(message);
            return;
        case Level::Error:
            g_logger->error(message);
            return;
        default:
            g_logger->info(message);
            return;
    }
}

// "ещё 1 раз", "ещё 3 раза", "ещё 11 раз": the word for the repetitions has to
// agree with the number, and English has its own rule.
const char* RepeatWord(unsigned int count) {
    if (Lang::Current() == Lang::Language::Russian) {
        const unsigned int last = count % 10;
        const unsigned int lastTwo = count % 100;

        if (last >= 2 && last <= 4 && (lastTwo < 12 || lastTwo > 14)) {
            return "раза";
        }

        return "раз";
    }

    return count == 1 ? "time" : "times";
}

// Reports how many times the previous message was repeated, if it was. The
// summary carries the level of the repeated message and is written directly:
// it is a line about the log itself, not a new message.
void FlushRepeatedMessages() {
    if (g_repeat.count == 0) {
        return;
    }

    const unsigned int count = g_repeat.count;
    g_repeat.count = 0;

    WriteToLoggers(
        g_repeat.level,
        fmt::format(Lang::Utf8(Lang::Str::LogRepeated), count, RepeatWord(count)));
}

spdlog::level::level_enum ToSpdlogLevel(const std::string& level) {
    const std::string value = Text::ToLowerAscii(Text::Trim(level));

    if (value == "trace") {
        return spdlog::level::trace;
    }

    if (value == "debug") {
        return spdlog::level::debug;
    }

    if (value == "warn" || value == "warning") {
        return spdlog::level::warn;
    }

    if (value == "error" || value == "err") {
        return spdlog::level::err;
    }

    if (value == "off" || value == "none") {
        return spdlog::level::off;
    }

    return spdlog::level::info;
}

std::wstring ResolveLogPath(const std::string& configuredPath) {
    std::wstring path = Text::ToWide(configuredPath);
    if (path.empty()) {
        path = L"REAL.log";
    }

    std::error_code error;
    if (std::filesystem::path(path).is_absolute()) {
        return path;
    }

    return Windows::Filesystem::JoinPath(Windows::Filesystem::GetExecutableDirectory(), path);
}

}

LogBuffer::LogBuffer(size_t limit):
    m_limit(limit == 0 ? 1 : limit) {}

void LogBuffer::SetNotifyHandler(std::function<void()> handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_notify = std::move(handler);
}

void LogBuffer::Append(const std::string& text) {
    std::function<void()> notify;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        size_t begin = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '\n') {
                if (i > begin) {
                    m_lines.emplace_back(text.substr(begin, i - begin));
                    ++m_pending;
                }

                begin = i + 1;
            }
        }

        while (m_lines.size() > m_limit) {
            m_lines.pop_front();
            if (m_pending > 0) {
                --m_pending;
            }
        }

        notify = m_notify;
    }

    if (notify) {
        notify();
    }
}

std::vector<std::string> LogBuffer::TakePending() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<std::string> result;
    if (m_pending == 0) {
        return result;
    }

    const size_t start = m_lines.size() > m_pending ? m_lines.size() - m_pending : 0;
    result.reserve(m_lines.size() - start);
    for (size_t i = start; i < m_lines.size(); ++i) {
        result.push_back(m_lines[i]);
    }

    m_pending = 0;
    return result;
}

std::vector<std::string> LogBuffer::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::vector<std::string>(m_lines.begin(), m_lines.end());
}

LogBuffer& miniant::Log::Buffer() {
    if (!g_buffer) {
        g_buffer = std::make_unique<LogBuffer>();
    }

    return *g_buffer;
}

// The file is switched on and off by the level itself: "off" means "no file at
// all", any other value means "write the file, this detailed". The path and the
// rotation are read once here, so a change in the settings window takes effect
// at once - the file is re-created from the new values.
void ApplyFileSettings(const Config::LoggingSettings& logging) {
    if (g_logger == nullptr) {
        return;
    }

    auto& sinks = g_logger->sinks();

    if (g_fileSink != nullptr) {
        sinks.erase(std::remove(sinks.begin(), sinks.end(), g_fileSink), sinks.end());
        g_fileSink.reset();
        g_logFilePath.clear();
    }

    if (Config::IsLogFileOff(logging)) {
        return;
    }

    try {
        const size_t maxBytes = static_cast<size_t>(logging.maxFileSizeMb) * 1024 * 1024;
        const std::wstring path = ResolveLogPath(logging.filePath);

        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            path,
            maxBytes,
            static_cast<size_t>(logging.maxFiles));
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        fileSink->set_level(ToSpdlogLevel(logging.level));

        g_logFilePath = path;
        g_fileSink = fileSink;
        sinks.push_back(fileSink);
    } catch (const std::exception&) {
        g_logFilePath.clear();
    }
}

void miniant::Log::Initialize(const Config::Settings& settings) {
    g_buffer = std::make_unique<LogBuffer>();

    auto bufferSink = std::make_shared<BufferSink>(g_buffer.get());
    bufferSink->set_pattern("[%H:%M:%S] [%l] %v");

    // The window of the program is not a log of its own: it always shows the
    // operations at the info level, whatever the settings say about the file.
    bufferSink->set_level(spdlog::level::info);

    g_logger = std::make_shared<spdlog::logger>("real", bufferSink);

    // The level of the file belongs to the sink of the file, so the logger
    // itself lets every line through.
    g_logger->set_level(spdlog::level::trace);
    g_logger->flush_on(spdlog::level::warn);

    ApplyFileSettings(settings.logging);
}

void miniant::Log::Shutdown() {
    FlushRepeatedMessages();
    g_fileSink.reset();
    if (g_logger) {
        g_logger->flush();
        g_logger.reset();
    }

    spdlog::shutdown();
}

void miniant::Log::SetFileSettings(const Config::LoggingSettings& logging) {
    ApplyFileSettings(logging);
}

void miniant::Log::WriteOperation(const std::string& message) {
    Write(Level::Info, message);
}

void miniant::Log::WriteHint(const std::string& message) {
    // The window shows the hints as well, so the file is the only place they
    // are left out of. The timestamp matches the pattern of the buffer sink.
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    ::localtime_s(&local, &now);

    char timestamp[16] = {};
    std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &local);

    Buffer().Append(fmt::format("[{}] [info] {}\n", timestamp, message));
}

void miniant::Log::Write(Level level, const std::string& message) {
    if (!g_logger) {
        return;
    }

    if (g_repeat.hasLast && level == g_repeat.level && message == g_repeat.text) {
        ++g_repeat.count;
        return;
    }

    // Another message means the run of repetitions (if any) is over.
    FlushRepeatedMessages();

    g_repeat.hasLast = true;
    g_repeat.level = level;
    g_repeat.text = message;

    WriteToLoggers(level, message);
}

void miniant::Log::Flush() {
    // The counted repetitions are a part of the log as well: without this they
    // would only appear at the next different line or at shutdown.
    FlushRepeatedMessages();

    if (g_logger != nullptr) {
        g_logger->flush();
    }
}

bool miniant::Log::WriteSnapshotToFile(const std::wstring& path) {
    std::string content;
    for (const auto& line : Buffer().Snapshot()) {
        content += line;
        content += "\r\n";
    }

    return Windows::Filesystem::WriteTextFileUtf8(path, content);
}
