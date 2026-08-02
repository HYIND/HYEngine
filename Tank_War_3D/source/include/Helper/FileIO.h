#pragma once

namespace FileIO
{

	bool OpenOneFile(TCHAR*& filename);
	bool SaveOneFile(TCHAR*& filename);
	bool OpenMultiFile();

	bool LoadFile(const TCHAR* filename, char*& buf, int& len);
	bool SaveFile(const TCHAR* filename, const char* buf, int len);
}