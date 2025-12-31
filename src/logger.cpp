#include "logger.hpp"

#include <cstdlib>

#include <sstream>

namespace vkBasalt
{

    Logger::Logger() : m_minLevel(getMinLogLevel())
    {
        if (m_minLevel != LogLevel::None)
        {
            std::string filename = getFileName();
            if (filename == "stderr")
            {
                m_outStream = std::unique_ptr<std::ostream, std::function<void(std::ostream*)>>(&std::cerr, [](std::ostream*) {});
            }
            else if (filename == "stdout")
            {
                m_outStream = std::unique_ptr<std::ostream, std::function<void(std::ostream*)>>(&std::cout, [](std::ostream*) {});
            }
            else
            {
                m_outStream = std::unique_ptr<std::ostream, std::function<void(std::ostream*)>>(new std::ofstream(filename),
                                                                                                [](std::ostream* os) { delete os; });
            }
        }
    }

    Logger::~Logger()
    {
    }

    void Logger::trace(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Trace, message);
    }

    void Logger::debug(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Debug, message);
    }

    void Logger::info(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Info, message);
    }

    void Logger::warn(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Warn, message);
    }

    void Logger::err(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Error, message);
    }

    void Logger::log(LogLevel level, const std::string& message)
    {
        s_instance.emitMsg(level, message);
    }

    void Logger::emitMsg(LogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Always store in history (regardless of log level filter)
        m_history.push_back({level, message});
        if (m_history.size() > MAX_HISTORY_SIZE)
            m_history.erase(m_history.begin());

        if (level >= m_minLevel)
        {
            static std::array<const char*, 5> s_prefixes = {
                {"vkBasalt trace: ", "vkBasalt debug: ", "vkBasalt info:  ", "vkBasalt warn:  ", "vkBasalt err:   "}};

            const char* prefix = s_prefixes.at(static_cast<uint32_t>(level));

            std::stringstream stream(message);
            std::string       line;

            while (std::getline(stream, line, '\n'))
            {
                *m_outStream << prefix << line << std::endl;
            }
        }
    }

    std::vector<LogEntry> Logger::getHistory()
    {
        std::lock_guard<std::mutex> lock(s_instance.m_mutex);
        return s_instance.m_history;
    }

    void Logger::clearHistory()
    {
        std::lock_guard<std::mutex> lock(s_instance.m_mutex);
        s_instance.m_history.clear();
    }

    const char* Logger::levelName(LogLevel level)
    {
        static std::array<const char*, 5> names = {{"TRACE", "DEBUG", "INFO", "WARN", "ERROR"}};
        uint32_t idx = static_cast<uint32_t>(level);
        if (idx < names.size())
            return names[idx];
        return "UNKNOWN";
    }

    LogLevel Logger::getMinLogLevel()
    {
        const std::array<std::pair<const char*, LogLevel>, 6> logLevels = {{
            {"trace", LogLevel::Trace},
            {"debug", LogLevel::Debug},
            {"info", LogLevel::Info},
            {"warn", LogLevel::Warn},
            {"error", LogLevel::Error},
            {"none", LogLevel::None},
        }};

        const char* envVar = getenv("VKBASALT_LOG_LEVEL");

        const std::string logLevelStr = envVar ? envVar : "";

        for (const auto& pair : logLevels)
        {
            if (logLevelStr == pair.first)
                return pair.second;
        }

        return LogLevel::Info;
    }

    std::string Logger::getFileName()
    {
        const char* envVar = getenv("VKBASALT_LOG_FILE");

        std::string filename = envVar ? envVar : "";

        if (filename.empty())
        {
            filename = "stderr";
        }

        return filename;
    }

} // namespace vkBasalt
