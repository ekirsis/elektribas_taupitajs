#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

class String
{
    std::string s;

public:
    String() = default;
    String(const char* c)
        : s(c ? c : "")
    {}
    String(const String&) = default;
    String& operator=(const char* c)
    {
        s = c ? c : "";
        return *this;
    }
    String& operator=(const String&) = default;
    String& operator+=(const char* c)
    {
        s += c;
        return *this;
    }
    String& operator+=(char c)
    {
        s += c;
        return *this;
    }
    String& operator+=(const String& other)
    {
        s += other.s;
        return *this;
    }
    friend String operator+(String lhs, const String& rhs)
    {
        lhs += rhs;
        return lhs;
    }
    friend String operator+(String lhs, const char* rhs)
    {
        lhs += rhs;
        return lhs;
    }
    int length() const
    {
        return static_cast<int>(s.length());
    }
    const char* c_str() const
    {
        return s.c_str();
    }
    void trim()
    {
        auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
        auto front = std::find_if(s.begin(), s.end(), not_space);
        s.erase(s.begin(), front);
        auto back = std::find_if(s.rbegin(), s.rend(), not_space);
        s.erase(back.base(), s.end());
    }
    void remove(unsigned int index)
    {
        if (index < s.length())
            s.erase(index);
    }
};

#define yield()
