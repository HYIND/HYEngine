#pragma once
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"

#define MAX_LOGSIZE 1024 * 1024 * 5
#define MAX_LOGFILE 3
static const char log_path[] = "logs/Tanklog.txt";

class Logger
{
public:
	static Logger* Instance()
	{
		static Logger* m_instance = new Logger();
		return m_instance;
	}
	std::shared_ptr<spdlog::logger> GetLogger()
	{
		return m_logger;
	}

private:
	Logger() {
		m_logger = spdlog::rotating_logger_mt("file_logger", log_path, MAX_LOGSIZE, MAX_LOGFILE);
		m_logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%5l%$]  %v");
#ifndef DEBUG
		m_logger->flush_on(spdlog::level::info);
		m_logger->set_level(spdlog::level::info);
#else
		m_logger->flush_on(spdlog::level::trace);
		m_logger->set_level(spdlog::level::trace);
#endif // DEBUG
	};
	std::shared_ptr<spdlog::logger> m_logger;
};

#ifdef LOGGERMODE_ON	//若启用日志则在header.h中启用LOGGERMODE_ON宏
#define LOGDEBUG(...) Logger::Instance()->GetLogger()->debug(__VA_ARGS__)
#define LOGTRACE(...) Logger::Instance()->GetLogger()->trace(__VA_ARGS__)
#define LOGINFO(...) Logger::Instance()->GetLogger()->info(__VA_ARGS__)
#define LOGWARN(...) Logger::Instance()->GetLogger()->warn(__VA_ARGS__)
#define LOGERROR(...) Logger::Instance()->GetLogger()->error(__VA_ARGS__)
#define LOGCRITICAL(...) Logger::Instance()->GetLogger()->critical(__VA_ARGS__)
#else 
#define LOGDEBUG(...)
#define LOGTRACE(...)
#define LOGINFO(...)
#define LOGWARN(...)
#define LOGERROR(...)
#define LOGCRITICAL(...)
#endif // LOGGERMODE


#ifdef WIN32
#define errorcode WSAGetLastError()
#endif