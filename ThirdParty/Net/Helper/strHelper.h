#pragma once

#include <string>

class strHelper
{
public:
    // split the string to array
    template <typename TYPE>
    static int splitStr(TYPE &list,
                        const std::string &str, const char *delim);

    // trim the specify char
    static std::string &trim(std::string &str, const char thechar = ' ');
    // convert type T to string
    template <typename T, typename S>
    static const T valueOf(const S &a);
};

#include "strHelper.inl"
