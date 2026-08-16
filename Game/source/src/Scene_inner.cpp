#include "Scene.h"
#include "OpenGLRenderEngine/OpenGLRenderContextManager.h"

D2D_Bitmap* Scene::AddResourceBitmap(int loc1, int loc2, int loc3, int loc4, ID2D1Bitmap* pBitmap, float opacity)
{
	D2D_Bitmap* Bitmap = new D2D_Bitmap(loc1, loc2, loc3, loc4, pBitmap, opacity);

	Bitmap_list.emplace_back(Bitmap);


	return Bitmap;
}

D2D_Text* Scene::AddText(int loc1, int loc2, int loc3, int loc4, const wchar_t* pwch,
	ID2D1SolidColorBrush* pDefaultBrush,
	ID2D1SolidColorBrush* pClickBrush,
	IDWriteTextFormat* pTextFormat,
	int id)
{
	D2D_Text* Text = new D2D_Text(loc1, loc2, loc3, loc4, pwch, pDefaultBrush, pClickBrush, pTextFormat, id);
	Text_list.emplace_back(Text);
	return Text;
}

D2D_Button* Scene::AddButton(int loc1, int loc2, int loc3, int loc4, int id)
{
	D2D_Button* Button = new D2D_Button(loc1, loc2, loc3, loc4, id);
	Button_list.emplace_back(Button);
	return Button;
}

D2D_Button* Scene::AddButton(int loc1, int loc2, int loc3, int loc4, int id, D2D_Bitmap* Bitmap)
{
	D2D_Button* Button = new D2D_Button(loc1, loc2, loc3, loc4, id, Bitmap);
	Button_list.emplace_back(Button);
	return Button;
}

D2D_Button* Scene::AddButton(int loc1, int loc2, int loc3, int loc4, int id, D2D_Text* Text)
{
	D2D_Button* Button = new D2D_Button(loc1, loc2, loc3, loc4, id, Text);
	Button_list.emplace_back(Button);
	return Button;
}

D2D_GIF* Scene::AddGIF(int loc1, int loc2, int loc3, int loc4, GIFINFO* gifInfo, int loopCount)
{
	D2D_GIF* Gif = new D2D_GIF(loc1, loc2, loc3, loc4, gifInfo, loopCount);
	Gif_list.emplace_back(Gif);
	return Gif;
}

void Scene::DrawScene(double time_diff)
{
	if (!pRenderTarget)
		return;

	pRenderTarget->BeginDraw();
	OnDrawScene(time_diff);
	pRenderTarget->EndDraw();
}

void Scene::OnDrawScene(double time_diff)
{
	for (auto& v : Text_list)
	{
		D2D1_RECT_F layoutRect = RectF(v->Text_location1, v->Text_location2, v->Text_location3, v->Text_location4);
		//draw text
		pRenderTarget->DrawText(
			v->str.c_str(),
			wcslen(v->str.c_str()),
			v->pTextFormat,
			layoutRect,
			v->pDefaultBrush
		);
	}
	for (auto& v : Bitmap_list)
	{
		pRenderTarget->DrawBitmap(v->pBitmap,
			RectF(v->Bitmap_location1, v->Bitmap_location2, v->Bitmap_location3, v->Bitmap_location4),
			v->opacity);
	}

	for (auto it = Gif_list.begin(); it != Gif_list.end();)
	{
		if (!(*it)->Draw(time_diff))
			it = Gif_list.erase(it);
		else it++;
	}
}

void Scene::Move()
{
	OnMove();
}

void Scene::OnMove()
{
	if (Text_changed)
	{
		D2D_Button* Button = Text_changed->pButton;
		if (MousePos::MoveX < Button->Button_location1 || MousePos::MoveX > Button->Button_location3 ||
			MousePos::MoveY < Button->Button_location2 || MousePos::MoveY > Button->Button_location4)
		{
			swap(Text_changed->pDefaultBrush, Text_changed->pClickBrush);
			Text_changed = NULL;
		}
		return;
	}
	else if (Bitmap_changed) {
		D2D_Button* Button = Bitmap_changed->pButton;
		if (MousePos::MoveX < Button->Button_location1 || MousePos::MoveX > Button->Button_location3 ||
			MousePos::MoveY < Button->Button_location2 || MousePos::MoveY > Button->Button_location4)
		{
			Bitmap_changed->Bitmap_location1 += 10;
			Bitmap_changed->Bitmap_location2 += 10;
			Bitmap_changed->Bitmap_location3 -= 10;
			Bitmap_changed->Bitmap_location4 -= 10;
			Bitmap_changed = NULL;
		}
		return;
	}
	else
	{
		for (auto& v : Button_list)
		{
			if (MousePos::MoveX > v->Button_location1 && MousePos::MoveX < v->Button_location3 &&
				MousePos::MoveY > v->Button_location2 && MousePos::MoveY < v->Button_location4)
			{
				if (v->Text)
				{
					swap(v->Text->pDefaultBrush, v->Text->pClickBrush);
					Text_changed = v->Text;
					return;
				}
				else if (v->Bitmap) {
					v->Bitmap->Bitmap_location1 -= 10;
					v->Bitmap->Bitmap_location2 -= 10;
					v->Bitmap->Bitmap_location3 += 10;
					v->Bitmap->Bitmap_location4 += 10;
					Bitmap_changed = v->Bitmap;
				}
			}
		}
	}
}

void Scene::Click(bool isLButtonPress, bool isShiftPress)
{
	OnClick(isLButtonPress, isShiftPress);
}

void Scene::OnClick(bool isLButtonPress, bool isShiftPress)
{
	if (!isLButtonPress)
		return;
	if (Text_changed)
	{
		D2D_Button* Button = Text_changed->pButton;
		if (MousePos::ClickX > Button->Button_location1 && MousePos::ClickX < Button->Button_location3 &&
			MousePos::ClickY > Button->Button_location2 && MousePos::ClickY < Button->Button_location4)
		{
			SendMessage(RENDERCONTEXMANAGER->GetHwnd(), WM_COMMAND, Button->id, (LPARAM)RENDERCONTEXMANAGER->GetHwnd());
		}
	}
	else if (Bitmap_changed) {
		D2D_Button* Button = Bitmap_changed->pButton;
		if (MousePos::ClickX > Button->Button_location1 && MousePos::ClickX < Button->Button_location3 &&
			MousePos::ClickY > Button->Button_location2 && MousePos::ClickY < Button->Button_location4)
		{
			SendMessage(RENDERCONTEXMANAGER->GetHwnd(), WM_COMMAND, Button->id, (LPARAM)RENDERCONTEXMANAGER->GetHwnd());
		}
	}
	else
	{
		for (auto& Button : Button_list)
		{
			if (MousePos::ClickX > Button->Button_location1 && MousePos::ClickX < Button->Button_location3 &&
				MousePos::ClickY > Button->Button_location2 && MousePos::ClickY < Button->Button_location4)
			{
				SendMessage(RENDERCONTEXMANAGER->GetHwnd(), WM_COMMAND, Button->id, (LPARAM)RENDERCONTEXMANAGER->GetHwnd());
			}
		}
	}
}

void Scene::Tick() {
	this->OnTick();
}

// 修改按钮
bool Scene::ModifyButton_Location(int id, int loc1, int loc2, int loc3, int loc4, bool offset)
{
	for (auto& v : Button_list)
	{
		if (v->id == id)
		{
			if (offset)
			{
				v->Button_location1 += loc1;
				v->Button_location2 += loc2;
				v->Button_location3 += loc3;
				v->Button_location4 += loc4;
			}
			else {
				v->Button_location1 = loc1;
				v->Button_location2 = loc2;
				v->Button_location3 = loc3;
				v->Button_location4 = loc4;
			}
			return true;
		}
	}
	return false;
}
bool Scene::ModifyButton_ID(int oldid, int newid)
{
	for (auto& v : Button_list)
	{
		if (v->id == oldid)
		{
			v->id = newid;
			return true;
		}
	}
	return false;
}
//修改文字
bool Scene::ModifyButton_Text(int id, const wstring& newstr)
{
	for (auto& v : Button_list)
	{
		if (v->id == id)
		{
			if (v->Text)
			{
				v->Text->str = newstr;
				return true;
			}
			else return false;
		}
	}
	return false;
}
//修改位图信息
bool Scene::ModifyButton_Bitmap(int id, int loc1, int loc2, int loc3, int loc4, bool offset, float opcaity)
{
	for (auto& v : Button_list)
	{
		if (v->id == id)
		{
			if (v->Bitmap)
			{
				if (offset)
				{
					v->Bitmap->Bitmap_location1 += loc1;
					v->Bitmap->Bitmap_location2 += loc2;
					v->Bitmap->Bitmap_location3 += loc3;
					v->Bitmap->Bitmap_location4 += loc4;
				}
				else {
					v->Bitmap->Bitmap_location1 = loc1;
					v->Bitmap->Bitmap_location2 = loc2;
					v->Bitmap->Bitmap_location3 = loc3;
					v->Bitmap->Bitmap_location4 = loc4;
				}
				v->Bitmap->opacity = opcaity;
				return true;
			}
			else return false;
		}
	}
	return false;
}

bool Scene::ModifyText_Context(int id, const wstring& newstr)
{
	for (auto& v : Text_list)
	{
		if (v->id == id)
		{
			v->str = newstr;
			return true;
		}
	}
	return false;
}

//删除按钮
bool Scene::DeleteButton(int id)
{
	for (auto button_it = Button_list.begin(); button_it != Button_list.end(); button_it++)
	{
		if ((*button_it)->id == id)
		{
			if ((*button_it)->Bitmap)
			{
				D2D_Bitmap* d2d_bitmap = (*button_it)->Bitmap;
				for (auto bitmap_it = Bitmap_list.begin(); bitmap_it != Bitmap_list.end(); bitmap_it++)
				{
					if ((*bitmap_it) == d2d_bitmap)
						Bitmap_list.erase(bitmap_it);
				}
			}
			if ((*button_it)->Text)
			{
				D2D_Text* d2d_text = (*button_it)->Text;
				for (auto text_it = Text_list.begin(); text_it != Text_list.end(); text_it++)
				{
					if ((*text_it) == d2d_text)
						Text_list.erase(text_it);
				}
			}

			D2D_Button* d2d_button = *button_it;
			Button_list.erase(button_it);
			delete(d2d_button);
			return true;
		}
	}
	return false;
}
