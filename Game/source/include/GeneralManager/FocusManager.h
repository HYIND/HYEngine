#pragma once

class FocusManager
{
public:
	static FocusManager* Instance();

	void SetFocus(bool enabled);
	void SetActive(bool enabled);

	bool HasFocus() const;
	bool IsActive() const;

	bool ShouldProcessInput() const;

private:
	FocusManager() = default;

private:
	bool m_hasFocus = false;
	bool m_isActive = false;
};