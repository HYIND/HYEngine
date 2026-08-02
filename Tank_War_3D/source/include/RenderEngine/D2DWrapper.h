#pragma once

#include "RenderEngine/D2DTools.h"
#include "Manager/ResourceManager.h"

using namespace D2D1;

//位图类，文字类、按钮类
class D2D_Bitmap;
class D2D_Text;
class D2D_Button;

class D2D_GIF {
public:
	int locationX1 = 0;
	int locationY1 = 0;
	int locationX2 = 0;
	int locationY2 = 0;

	float opacity = 1.0f;

	double lastTime = 0.f;	//从第一次绘制开始的持续时间
	double totalTime = 1.f;
	int loopCount = 1;
	GIFINFO* gifInfo = nullptr;

	D2D_GIF(int x1, int y1, int x2, int y2, GIFINFO* gifInfo, int loopCount = 1, float opacity = 1.0f);
	bool Draw(double time_diff);
};

//D2D 贴图
class D2D_Bitmap
{
public:
	int Bitmap_location1 = 0;
	int Bitmap_location2 = 0;
	int Bitmap_location3 = 0;
	int Bitmap_location4 = 0;

	float opacity = 1.0f;

	D2D_Button* pButton = NULL;
	ID2D1Bitmap* pBitmap = NULL;

	D2D_Bitmap(int loc1, int loc2, int loc3, int loc4, ID2D1Bitmap* pBitmap = NULL, float opacity = 1.0f);
};

//D2D 文本
class D2D_Text
{
public:
	int Text_location1 = 0;
	int Text_location2 = 0;
	int Text_location3 = 0;
	int Text_location4 = 0;
	std::wstring str;
	int id = 0;

	ID2D1SolidColorBrush* pDefaultBrush = NULL;
	ID2D1SolidColorBrush* pClickBrush = NULL;
	IDWriteTextFormat* pTextFormat = NULL;

	D2D_Button* pButton = NULL;

	D2D_Text(int loc1, int loc2, int loc3, int loc4, const wchar_t* pwch,
		ID2D1SolidColorBrush* pDefaultBrush,
		ID2D1SolidColorBrush* pClickBrush,
		IDWriteTextFormat* pTextFormat,
		int id = 0
	);
};

// D2D 按钮，绑定文本或贴图
class D2D_Button
{
public:
	int Button_location1 = 0;
	int Button_location2 = 0;
	int Button_location3 = 0;
	int Button_location4 = 0;
	int id = 0;

	D2D_Bitmap* Bitmap = NULL;
	D2D_Text* Text = NULL;

	D2D_Button(int loc1, int loc2, int loc3, int loc4, int id);
	D2D_Button(int loc1, int loc2, int loc3, int loc4, int id, D2D_Bitmap* Bitmap);
	D2D_Button(int loc1, int loc2, int loc3, int loc4, int id, D2D_Text* Text);
};
