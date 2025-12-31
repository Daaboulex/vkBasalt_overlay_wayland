#ifndef LOGGER_HPP_INCLUDED
#define LOGGER_HPP_INCLUDED

#include <array>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace vkBasalt
{

    enum class LogLevel : uint32_t
    {
        Trace = 0,
        Debug = 1,
        Info  = 2,
        Warn  = 3,
        Error = 4,
        None  = 5,
    };

    // Log entry for history
    struct LogEntry
    {
        LogLevel level;
        std::string message;
    };

    class Logger
    {

    public:
        Logger();
        ~Logger();

        static void trace(const std::string& message);
        static void debug(const std::string& message);
        static void info(const std::string& message);
        static void warn(const std::string& message);
        static void err(const std::string& message);
        static void log(LogLevel level, const std::string& message);

        static LogLevel logLevel()
        {
            return s_instance.m_minLevel;
        }

        // Get log history (thread-safe copy)
        static std::vector<LogEntry> getHistory();

        // Clear log history
        static void clearHistory();

        // Get level name string
        static const char* levelName(LogLevel level);

    private:
        static Logger s_instance;
        static constexpr size_t MAX_HISTORY_SIZE = 1000;

        const LogLevel m_minLevel;

        std::mutex m_mutex;

        std::unique_ptr<std::ostream, std::function<void(std::ostream*)>> m_outStream;

        std::vector<LogEntry> m_history;

        void emitMsg(LogLevel level, const std::string& message);

        static LogLevel getMinLogLevel();

        static std::string getFileName();
    };

} // namespace vkBasalt

#endif // LOGGER_HPP_INCLUDED
