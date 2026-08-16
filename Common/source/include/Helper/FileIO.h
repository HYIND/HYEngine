#pragma once

#include <string>
#include <tchar.h>
#include <map>

namespace FileIO
{

	bool SelectFolder(std::string& folderPath, const std::string& title = "请选择文件夹");
	bool OpenOneFile(std::string& filePath, const std::map<std::string, std::string>& filters = {});
	bool SaveOneFile(std::string& filePath, const std::string& defaultFileName = "", std::string defExt = "", const std::map<std::string, std::string>& filters = {});

	bool LoadFile(const TCHAR* filename, char*& buf, int& len);
	bool SaveFile(const TCHAR* filename, const char* buf, int len);
}