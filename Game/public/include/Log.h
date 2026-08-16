#pragma once
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"

#define MAX_LOGSIZE 1024 * 1024 * 5
#define MAX_LOGFILE 3

class Logger
{
	static const std::string default_log_path;

public:
	static Logger* Instance()
	{
		static Logger* m_instance = new Logger();
		return m_instance;
	}
	std::shared_ptr<spdlog::logger> GetLogger()
	{
		if (m_enabled)
			return m_logger;
	}
	void SetEnableLogger(bool enabled)
	{
		m_enabled = enabled;
	}
	void SetPath(const std::string& path)
	{
		if (path == m_curpath)
			return;

		std::shared_ptr<spdlog::logger> temp = spdlog::rotating_logger_mt("_file_logger_" + path, path, MAX_LOGSIZE, MAX_LOGFILE);
		m_logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%5l%$]  %v");
#ifndef DEBUG
		m_logger->flush_on(spdlog::level::info);
		m_logger->set_level(spdlog::level::info);
#else
		m_logger->flush_on(spdlog::level::trace);
		m_logger->set_level(spdlog::level::trace);
#endif // DEBUG
		m_logger = temp;
		m_curpath = path;
	}

private:
	Logger() {
		m_logger = spdlog::rotating_logger_mt("file_logger", default_log_path, MAX_LOGSIZE, MAX_LOGFILE);
		m_logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%5l%$]  %v");
#ifndef DEBUG
		m_logger->flush_on(spdlog::level::info);
		m_logger->set_level(spdlog::level::info);
#else
		m_logger->flush_on(spdlog::level::trace);
		m_logger->set_level(spdlog::level::trace);
#endif // DEBUG
	};
	std::string m_curpath;
	std::shared_ptr<spdlog::logger> m_logger;
	bool m_enabled = true;
};
const std::string Logger::default_log_path = "log.txt";

#define LOGDEBUG(...) Logger::Instance()->GetLogger()->debug(__VA_ARGS__)
#define LOGTRACE(...) Logger::Instance()->GetLogger()->trace(__VA_ARGS__)
#define LOGINFO(...) Logger::Instance()->GetLogger()->info(__VA_ARGS__)
#define LOGWARN(...) Logger::Instance()->GetLogger()->warn(__VA_ARGS__)
#define LOGERROR(...) Logger::Instance()->GetLogger()->error(__VA_ARGS__)
#define LOGCRITICAL(...) Logger::Instance()->GetLogger()->critical(__VA_ARGS__)


#ifdef WIN32
#define errorcode WSAGetLastError()
#endif