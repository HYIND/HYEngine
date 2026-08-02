#pragma once

class FocusManager
{
public:
	static FocusManager* Instance();

	void SetFocus(bool enabled) { m_hasFocus = enabled; }
	void SetActive(bool enabled) { m_isActive = enabled; }

	bool HasFocus() const { return m_hasFocus; }
	bool IsActive() const { return m_isActive; }

	bool ShouldProcessInput() const
	{
		return m_hasFocus && m_isActive;
	}

private:
	FocusManager() = default;

private:
	bool m_hasFocus = false;
	bool m_isActive = false;
};