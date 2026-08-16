#include "RenderEngine/D2DWrapper.h"

D2D_GIF::D2D_GIF(int x1, int y1, int x2, int y2, GIFINFO* gifInfo, int loopCount, float opacity) :
	locationX1(x1), locationY1(y1), locationX2(x2), locationY2(y2), gifInfo(gifInfo), loopCount(loopCount), opacity(opacity) {
	totalTime = gifInfo->getDefaultMsTime();
}

bool D2D_GIF::Draw(double time_diff) {
	UINT framecount = gifInfo->getFrameCount();
	UINT curIndex = UINT(lastTime / (totalTime / framecount));
	UINT i = curIndex % framecount;
	ID2D1Bitmap* pBitmap = gifInfo->getFrame(i);
	if (pBitmap != nullptr)
	{
		pRenderTarget->DrawBitmap(pBitmap,
			RectF(locationX1, locationY1, locationX2, locationY2),
			opacity);
	}
	lastTime += time_diff;
	if (loopCount <= 0)return true;
	if (lastTime > loopCount * totalTime)
		return false;
}

D2D_Bitmap::D2D_Bitmap(int loc1, int loc2, int loc3, int loc4, ID2D1Bitmap* pBitmap, float opacity) :
	Bitmap_location1(loc1), Bitmap_location2(loc2), Bitmap_location3(loc3), Bitmap_location4(loc4), pBitmap(pBitmap), opacity(opacity) {
}

D2D_Text::D2D_Text(int loc1, int loc2, int loc3, int loc4, const wchar_t* pwch,
	ID2D1SolidColorBrush* pDefaultBrush,
	ID2D1SolidColorBrush* pClickBrush,
	IDWriteTextFormat* pTextFormat,
	int id
) :
	Text_location1(loc1), Text_location2(loc2), Text_location3(loc3), Text_location4(loc4),
	str(pwch), pDefaultBrush(pDefaultBrush), pClickBrush(pClickBrush), pTextFormat(pTextFormat), id(id) {
}

D2D_Button::D2D_Button(int loc1, int loc2, int loc3, int loc4, int id) :
	Button_location1(loc1), Button_location2(loc2), Button_location3(loc3), Button_location4(loc4), id(id) {
}

D2D_Button::D2D_Button(int loc1, int loc2, int loc3, int loc4, int id, D2D_Bitmap* Bitmap) :
	Button_location1(loc1), Button_location2(loc2), Button_location3(loc3), Button_location4(loc4), id(id), Bitmap(Bitmap) {
	Bitmap->pButton = this;
}

D2D_Button::D2D_Button(int loc1, int loc2, int loc3, int loc4, int id, D2D_Text* Text) :
	Button_location1(loc1), Button_location2(loc2), Button_location3(loc3), Button_location4(loc4), id(id), Text(Text) {
	Text->pButton = this;
}
