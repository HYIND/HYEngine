#include "Helper/FileIO.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   
#include <commdlg.h>   
#include <cstdio>      
#include <string>
#include <format>
#include <shlobj.h>
#include "Helper/Tools.h"

namespace FileIO
{

	bool LoadFile(const TCHAR* filename, char*& buf, int& len)
	{

		FILE* fp = nullptr;
		_wfopen_s(&fp, filename, TEXT("rb"));
		if (!fp)
			return false;

		fseek(fp, 0, SEEK_END);      //将文件指针指向该文件的最后
		len = ftell(fp);   //根据指针位置，此时可以算出文件的字符数
		buf = new char[len + 1];
		memset(buf, '\0', len + 1);
		fseek(fp, 0, SEEK_SET);                 //重新将指针指向文件首部
		int l = fread(buf, sizeof(char), len, fp);
		bool  result = (len == l);
		//bool result = (len == fread(buf, sizeof(char), len, fp)); //开始读取整个文件
		fclose(fp);
		return result;
	}

	bool SaveFile(const TCHAR* filename, const char* buf, int len)
	{
		FILE* fp = nullptr;
		_wfopen_s(&fp, filename, TEXT("wb"));
		if (!fp)
			return false;
		bool result = (len == fwrite(buf, sizeof(char), len, fp));
		fclose(fp);
		return result;
	}

	bool SelectFolder(std::string& folderPath, const std::string& title)
	{
		BROWSEINFO bi = { 0 };
		bi.hwndOwner = NULL;
		bi.lpszTitle = Tool::UTF8ToWString(title).c_str();
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		bi.lpfn = NULL;

		LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
		if (pidl != NULL)
		{
			TCHAR path[MAX_PATH] = { '\0' };
			if (SHGetPathFromIDList(pidl, path))
			{
#ifdef UNICODE
				folderPath = Tool::WStringToUTF8(path);
#else
				folderPath = std::string(path);
#endif

				CoTaskMemFree(pidl);
				return true;
			}
			CoTaskMemFree(pidl);
		}
		return false;
	}

	bool OpenOneFile(std::string& filePath, const std::map<std::string, std::string>& filters)
	{
		std::string filterString;
		if (!filters.empty())
		{
			for (auto& [filter, ext] : filters)
				filterString += std::format("{}(*.{})\0*.{}\0", filter, ext, ext);
			filterString += "\0";
		}
		else
		{
			static char defaultFilters[] = "All Files(*.*)\0*.*\0\0";
			filterString = std::string(defaultFilters, sizeof(defaultFilters) - 1);
		}

		OPENFILENAME ofn;
		TCHAR szOpenFileNames[80 * MAX_PATH] = { 0 };
		TCHAR szPath[MAX_PATH] = { 0 };
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR;
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szOpenFileNames;
		ofn.nMaxFile = sizeof(szOpenFileNames);
		ofn.lpstrFile[0] = '\0';
#ifdef UNICODE
		auto wfilterString = Tool::UTF8ToWString(filterString);
		ofn.lpstrFilter = wfilterString.c_str();
#else
		ofn.lpstrFilter = filterString.c_str();
#endif
		if (GetOpenFileName(&ofn))
		{

			lstrcpyn(szPath, szOpenFileNames, ofn.nFileOffset);
			szPath[ofn.nFileOffset] = '\0';
			lstrcat(szPath, TEXT("\\"));
			TCHAR* p = szOpenFileNames + ofn.nFileOffset; //把指针移到第一个文件

			TCHAR* temp = new TCHAR[80 * MAX_PATH];
			ZeroMemory(temp, sizeof(80 * MAX_PATH));
			lstrcat(temp, szPath);  //给文件名加上路径  
			lstrcat(temp, p);    //加上文件名 

#ifdef UNICODE
			filePath = Tool::WStringToUTF8(temp);
#else
			filePath = std::string(temp);
#endif
			delete[] temp;
			return true;
		}
		else
			return false;
	}

	bool SaveOneFile(std::string& filePath, const std::string& defaultFileName, std::string defExt, const std::map<std::string, std::string>& filters)
	{
		std::string filterString;
		if (!filters.empty())
		{
			for (auto& [filter, ext] : filters)
				filterString += std::format("{}(*.{})\0*.{}\0", filter, ext, ext);
			filterString += "\0";
		}
		else
		{
			static char defaultFilters[] = "All Files(*.*)\0*.*\0\0";
			filterString = std::string(defaultFilters, sizeof(defaultFilters) - 1);
		}

		OPENFILENAME ofn;
		TCHAR szOpenFileNames[80 * MAX_PATH] = { 0 };
		TCHAR szPath[MAX_PATH] = { 0 };
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR;
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szOpenFileNames;
		ofn.nMaxFile = sizeof(szOpenFileNames);
		ofn.lpstrFile[0] = '\0';

		while (!defExt.empty() && defExt[0] == '.')
			defExt.erase(0, 1);

#ifdef UNICODE
		std::wstring wDefault = Tool::UTF8ToWString(defaultFileName);
		auto wfilterString = Tool::UTF8ToWString(filterString);
		auto wdefExt = Tool::UTF8ToWString(defExt + "\0");

		wcsncpy(ofn.lpstrFile, wDefault.c_str(), wDefault.size());
		ofn.lpstrFilter = wfilterString.c_str();
		ofn.lpstrDefExt = wdefExt.c_str();
#else
		auto temp = std::string(defExt + "\0");
		strncpy(ofn.lpstrFile, defaultFileName.c_str(), defaultFileName.size());
		ofn.lpstrFilter = filterString.c_str();
		ofn.lpstrDefExt = temp.c_str();
#endif

		if (GetSaveFileName(&ofn))
		{

			lstrcpyn(szPath, szOpenFileNames, ofn.nFileOffset);
			szPath[ofn.nFileOffset] = '\0';
			lstrcat(szPath, TEXT("\\"));
			TCHAR* p = szOpenFileNames + ofn.nFileOffset; //把指针移到第一个文件

			TCHAR* temp = new TCHAR[80 * MAX_PATH];
			ZeroMemory(temp, sizeof(80 * MAX_PATH));
			lstrcat(temp, szPath);  //给文件名加上路径  
			lstrcat(temp, p);		//加上文件名 

#ifdef UNICODE
			filePath = Tool::WStringToUTF8(temp);
#else
			filePath = std::string(temp);
#endif
			delete[] temp;
			return true;
		}
		else
			return false;
	}

}