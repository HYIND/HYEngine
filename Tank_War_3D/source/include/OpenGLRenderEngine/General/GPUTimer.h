#pragma once
#include <gl\glew.h>
#include <format>

class GPUTimer {
public:
	GPUTimer();
	~GPUTimer();

	float End();				// 阻塞式获取结果（毫秒）
	float EndAndBeginNext();	// 阻塞式获取结果并启动下一轮计时（毫秒）

	float EndWithPrint(const std::string& fmt);				//fmt格式 "somestr.....{}"
	float EndWithPrintAndBeginNext(const std::string& fmt);	//fmt格式 "somestr.....{}"

	void SetForceEnable();

private:
	void Begin();
	void Release();

private:
	GLuint m_query_start = 0;
	GLuint m_query_end = 0;
	bool forceEnable;
};