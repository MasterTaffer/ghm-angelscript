#pragma once
#include <vector>
#include <string>
#include <cstring>

//Couple of simple tools



static std::vector<std::string> SplitStringByComma(const std::string & str)
{
    return SplitStringByComma(str.c_str());
}

static std::vector<std::string> SplitStringByComma(const char * str)
{

    std::vector<std::string> res;
    if (str == nullptr)
        return res;

    const char* bg = str;

    auto addsec = [&]
    {
        res.push_back(std::string(bg, str));
        bg = str + 1;
    };

    while (*str != 0)
    {
        if (*str == ',')
            addsec();
        str++;
    }
    addsec();

    return res;
}

static bool IsPresentInList(const std::vector<std::string>& haystack, const char * needle)
{
    if (needle == nullptr)
        return false;
    for (const std::string& t : haystack)
    {
        if (strcmp(t.c_str(), needle) == 0)
            return true;
    }
    return false;
}
