#pragma once

#include "Arduino.h"

#include <cstring>
#include <string>

extern std::string g_captured_url;
extern int g_mock_http_status;
extern std::string g_mock_response_body;
extern bool g_key_header_was_set;

class HTTPClient
{
public:
    void begin(const String& url)
    {
        g_captured_url = url.c_str();
    }
    void setTimeout(int)
    {}
    void addHeader(const char* name, const char*)
    {
        if (strcmp(name, "X-API-Key") == 0)
            g_key_header_was_set = true;
    }
    int GET()
    {
        return g_mock_http_status;
    }
    String getString()
    {
        return g_mock_response_body.c_str();
    }
    void end()
    {}
};
