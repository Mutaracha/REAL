#include "Log.h"

#include "Settings.h"
#include "Text.h"
#include "Windows/Filesystem.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

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
// Console output is a separate logger: it carries the operations only.
std::shared_ptr<spdlog::logger> g_consoleLogger;
std::unique_ptr<LogBuffer> g_buffer;
std::wstring g_logFilePath;

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

void miniant::Log::Initialize(const Config::Settings& settings, bool consoleAttached) {
    g_buffer = std::make_unique<LogBuffer>();

    std::vector<spdlog::sink_ptr> sinks;

    auto bufferSink = std::make_shared<BufferSink>(g_buffer.get());
    bufferSink->set_pattern("[%H:%M:%S] %v");
    sinks.push_back(bufferSink);

    if (settings.logging.toConsole && consoleAttached) {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
        consoleSink->set_pattern("%v");
        g_consoleLogger = std::make_shared<spdlog::logger>("console", consoleSink);
        g_consoleLogger->set_level(spdlog::level::info);
    }

    if (settings.logging.toFile) {
        g_logFilePath = ResolveLogPath(settings.logging.filePath);

        try {
            const size_t maxBytes = static_cast<size_t>(settings.logging.maxFileSizeMb) * 1024 * 1024;
            auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                g_logFilePath,
                maxBytes,
                static_cast<size_t>(settings.logging.maxFiles));
            fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            sinks.push_back(fileSink);
        } catch (const std::exception&) {
            g_logFilePath.clear();
        }
    } else {
        g_logFilePath.clear();
    }

    g_logger = std::make_shared<spdlog::logger>("real", sinks.begin(), sinks.end());
    g_logger->set_level(ToSpdlogLevel(settings.logging.level));
    g_logger->flush_on(spdlog::level::warn);
}

void miniant::Log::Shutdown() {
    g_consoleLogger.reset();
    if (g_logger) {
        g_logger->flush();
        g_logger.reset();
    }

    spdlog::shutdown();
}

void miniant::Log::SetLevel(const std::string& level) {
    if (g_logger) {
        g_logger->set_level(ToSpdlogLevel(level));
    }
}

void miniant::Log::WriteOperation(const std::string& message) {
    Write(Level::Info, message);

    if (g_consoleLogger != nullptr) {
        g_consoleLogger->info(message);
    }
}

void miniant::Log::Write(Level level, const std::string& message) {
    if (!g_logger) {
        return;
    }

    // Failures are worth seeing in the console as well, everything else is
    // written to the file and shown in the window only.
    if (level == Level::Error && g_consoleLogger != nullptr) {
        g_consoleLogger->error(message);
    }

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

void miniant::Log::Flush() {
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
