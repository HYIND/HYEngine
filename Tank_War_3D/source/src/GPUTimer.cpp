#include "OpenGLRenderEngine/General/GPUTimer.h"
#include <vector>
#include "Helper/Tools.h"

#define GPUTimer_Disable

#ifdef GPUTimer_Disable
constexpr bool GPUTimer_Enable = false;
#else 
constexpr bool GPUTimer_Enable = true;
#endif

GPUTimer::GPUTimer()
	:forceEnable(false)
{
	if (!GPUTimer_Enable)
		return;
	Begin();
}

GPUTimer::~GPUTimer()
{
	if (!GPUTimer_Enable && !forceEnable)
		return;

	Release();
}

// 阻塞式获取结果（毫秒）
float GPUTimer::End()
{
	if (!GPUTimer_Enable && !forceEnable)
		return 0.f;

	glQueryCounter(m_query_end, GL_TIMESTAMP);

	GLint start_available = GL_FALSE;
	GLint end_available = GL_FALSE;
	while (end_available == GL_FALSE) glGetQueryObjectiv(m_query_end, GL_QUERY_RESULT_AVAILABLE, &end_available);
	while (start_available == GL_FALSE) glGetQueryObjectiv(m_query_start, GL_QUERY_RESULT_AVAILABLE, &start_available);

	GLuint64 startTime, endTime;
	glGetQueryObjectui64v(m_query_start, GL_QUERY_RESULT, &startTime);
	glGetQueryObjectui64v(m_query_end, GL_QUERY_RESULT, &endTime);

	float result = (endTime - startTime) / 1000000.f;

	Release();

	return result;
}

float GPUTimer::EndAndBeginNext()
{
	if (!GPUTimer_Enable && !forceEnable)
		return 0.f;

	float result = End();
	Begin();
	return result;
}

float GPUTimer::EndWithPrint(const std::string& fmt)
{
	if (!GPUTimer_Enable && !forceEnable)
		return 0.f;

	float result = End();
	std::cout << std::vformat(fmt, std::make_format_args(result));
	return result;
}

float GPUTimer::EndWithPrintAndBeginNext(const std::string& fmt)
{
	if (!GPUTimer_Enable && !forceEnable)
		return 0.f;

	float result = End();
	std::cout << std::vformat(fmt, std::make_format_args(result));
	Begin();
	return result;
}

void GPUTimer::SetForceEnable()
{
	if (forceEnable == true)
		return;
	forceEnable = true;
	Begin();
}

void GPUTimer::Begin()
{
	if (!GPUTimer_Enable && !forceEnable)
		return;

	glGenQueries(1, &m_query_start);
	glGenQueries(1, &m_query_end);

	glQueryCounter(m_query_start, GL_TIMESTAMP);
	glFlush();
}

void GPUTimer::Release()
{
	if (!GPUTimer_Enable && !forceEnable)
		return;

	for (auto query : { m_query_start ,m_query_end })
		if (query != 0) glDeleteQueries(1, &query);

	m_query_start = 0;
	m_query_end = 0;
}