#include "Manager/UserInfoManager.h"

UserInfoManager* UserInfoManager::Instance()
{
	static UserInfoManager* m_instance = new UserInfoManager();
	return m_instance;
}

UserInfoManager::UserInfoManager()
{
	m_jwt = "";
	m_token = "";
	m_name = "";
	m_gameid = "";
}

std::string UserInfoManager::userjwt() const
{
	return m_jwt;
}

std::string UserInfoManager::usertoken() const
{
	return m_token;
}

std::string UserInfoManager::username() const
{
	return m_name;
}

std::string UserInfoManager::gameid() const
{
	return m_gameid;
}

void UserInfoManager::setUserJwt(const std::string& jwt)
{
	m_jwt = jwt;
}

void UserInfoManager::setUserToken(const std::string& token)
{
	m_token = token;
}

void UserInfoManager::setUserName(const std::string& name)
{
	m_name = name;
}

void UserInfoManager::setGameId(const std::string& gameid)
{
	m_gameid = gameid;
}

bool UserInfoManager::isMyToken(const std::string& token)
{
	return token == m_token;
}
