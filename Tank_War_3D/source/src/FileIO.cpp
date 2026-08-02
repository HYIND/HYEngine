#include "header.h"
#include "Helper/FileIO.h"
#include "commdlg.h"

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

	bool OpenOneFile(TCHAR*& filename)
	{
		OPENFILENAME ofn;
		TCHAR szOpenFileNames[80 * MAX_PATH];
		TCHAR szPath[MAX_PATH];
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.Flags = OFN_EXPLORER;
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szOpenFileNames;
		ofn.nMaxFile = sizeof(szOpenFileNames);
		ofn.lpstrFile[0] = '\0';
		ofn.lpstrFilter = TEXT("All Files(*.*)\0*.*\0Map Files(*.map)\0*.map\0\0");
		if (GetOpenFileName(&ofn))
		{

			lstrcpyn(szPath, szOpenFileNames, ofn.nFileOffset);
			szPath[ofn.nFileOffset] = '\0';
			lstrcat(szPath, TEXT("\\"));
			TCHAR* p = szOpenFileNames + ofn.nFileOffset; //把指针移到第一个文件

			filename = new TCHAR[80 * MAX_PATH];
			ZeroMemory(filename, sizeof(80 * MAX_PATH));
			lstrcat(filename, szPath);  //给文件名加上路径  
			lstrcat(filename, p);    //加上文件名 
			//MessageBox(NULL, filename, TEXT("OneFileOpen"), MB_OK);

			return true;
		}
		else return false;
	}

	bool SaveOneFile(TCHAR*& filename)
	{
		OPENFILENAME ofn;
		TCHAR szOpenFileNames[80 * MAX_PATH];
		TCHAR szPath[MAX_PATH];
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.Flags = OFN_EXPLORER;
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szOpenFileNames;
		ofn.nMaxFile = sizeof(szOpenFileNames);
		ofn.lpstrFile[0] = '\0';
		ofn.lpstrFilter = TEXT("Map Files(*.map)\0*.map\0All Files(*.*)\0*.*\0\0");
		ofn.lpstrDefExt = TEXT("map\0");
		if (GetSaveFileName(&ofn))
		{

			lstrcpyn(szPath, szOpenFileNames, ofn.nFileOffset);
			szPath[ofn.nFileOffset] = '\0';
			lstrcat(szPath, TEXT("\\"));
			TCHAR* p = szOpenFileNames + ofn.nFileOffset; //把指针移到第一个文件

			filename = new TCHAR[80 * MAX_PATH];
			ZeroMemory(filename, sizeof(80 * MAX_PATH));
			lstrcat(filename, szPath);  //给文件名加上路径  
			lstrcat(filename, p);    //加上文件名 
			//MessageBox(NULL, filename, TEXT("OneFileOpen"), MB_OK);

			return true;
		}
		else return false;
	}

	bool OpenMultiFile()
	{
		OPENFILENAME ofn;
		TCHAR szOpenFileNames[80 * MAX_PATH];
		TCHAR szPath[MAX_PATH];
		TCHAR szFileName[80 * MAX_PATH];
		TCHAR* p;
		int nLen = 0;
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT;
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szOpenFileNames;
		ofn.nMaxFile = sizeof(szOpenFileNames);
		ofn.lpstrFile[0] = '/0';
		ofn.lpstrFilter = TEXT("All Files(*.*)\0*.*\0Map Files(*.map)\0*.map\0\0");
		if (GetOpenFileName(&ofn))
		{
			//把第一个文件名前的复制到szPath,即:
			//如果只选了一个文件,就复制到最后一个'/'
			//如果选了多个文件,就复制到第一个NULL字符
			lstrcpyn(szPath, szOpenFileNames, ofn.nFileOffset);
			//当只选了一个文件时,下面这个NULL字符是必需的.
			//这里不区别对待选了一个和多个文件的情况
			szPath[ofn.nFileOffset] = '/0';
			nLen = lstrlen(szPath);

			if (szPath[nLen - 1] != '//')   //如果选了多个文件,则必须加上'//'
			{
				lstrcat(szPath, TEXT("//"));
			}

			p = szOpenFileNames + ofn.nFileOffset; //把指针移到第一个文件

			ZeroMemory(szFileName, sizeof(szFileName));
			while (*p)
			{
				lstrcat(szFileName, szPath);  //给文件名加上路径  
				lstrcat(szFileName, p);    //加上文件名  
				lstrcat(szFileName, TEXT("/n")); //换行   
				p += lstrlen(p) + 1;     //移至下一个文件
			}
			return true;
			//MessageBox(NULL, szFileName, TEXT("MultiSelect"), MB_OK);
		}
		else return false;
	}

}