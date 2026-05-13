#pragma once

#include "Arduino.h"

#include <cstring>
#include <string>

extern std::string g_mock_prefs_api_url;
extern std::string g_mock_prefs_api_key;
extern bool g_mock_prefs_api_key_enabled;

class Preferences
{
public:
    bool begin(const char*, bool)
    {
        return true;
    }
    String getString(const char* key, const char* default_val)
    {
        if (strcmp(key, "api_key") == 0)
        {
            if (g_mock_prefs_api_key.empty())
                return String(default_val);
            return String(g_mock_prefs_api_key.c_str());
        }
        if (g_mock_prefs_api_url.empty())
            return String(default_val);
        return String(g_mock_prefs_api_url.c_str());
    }
    bool getBool(const char* key, bool default_val)
    {
        if (strcmp(key, "api_key_enabled") == 0)
            return g_mock_prefs_api_key_enabled;
        return default_val;
    }
    void end()
    {}
    void putString(const char* key, const char* val)
    {
        if (strcmp(key, "api_key") == 0)
            g_mock_prefs_api_key = val;
        else
            g_mock_prefs_api_url = val;
    }
    void putBool(const char* key, bool val)
    {
        if (strcmp(key, "api_key_enabled") == 0)
            g_mock_prefs_api_key_enabled = val;
    }
    void remove(const char* key)
    {
        if (strcmp(key, "api_key") == 0)
            g_mock_prefs_api_key.clear();
        else if (strcmp(key, "api_key_enabled") == 0)
            g_mock_prefs_api_key_enabled = true;
        else
            g_mock_prefs_api_url.clear();
    }
};
