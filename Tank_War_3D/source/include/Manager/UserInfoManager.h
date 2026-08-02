#pragma once

#include <iostream>

class UserInfoManager
{
public:
	static UserInfoManager* Instance();

private:
	UserInfoManager();

public:
	std::string userjwt() const;
	std::string usertoken() const;
	std::string username() const;
	std::string gameid() const;

	void setUserJwt(const std::string& jwt);
	void setUserToken(const std::string& token);
	void setUserName(const std::string& name);
	void setGameId(const std::string& gameid);

	bool isMyToken(const std::string& token);

private:
	std::string m_jwt;
	std::string m_token;
	std::string m_name;
	std::string m_gameid;
};
