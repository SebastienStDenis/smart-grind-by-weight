/**
 * Host stand-in for Arduino HTTPClient, backed by libcurl.
 */
#pragma once

#include <string>
#include "Arduino.h"

#define HTTP_CODE_OK 200

class HTTPClient {
public:
    bool begin(const String& url);
    int GET();
    String getString();
    void end();
    void setTimeout(uint16_t timeout_ms) { timeout_ms_ = timeout_ms; }
    void setConnectTimeout(int32_t timeout_ms) { connect_timeout_ms_ = timeout_ms; }
    void setReuse(bool reuse) { (void)reuse; }
    void addHeader(const String& name, const String& value) { (void)name; (void)value; }

private:
    std::string url_;
    std::string payload_;
    uint16_t timeout_ms_ = 5000;
    int32_t connect_timeout_ms_ = 5000;
};
