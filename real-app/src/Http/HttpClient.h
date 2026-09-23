#pragma once

#include <string>
#include <utility>
#include <vector>

namespace miniant::Http {

struct Response {
    bool networkOk = false;
    unsigned long statusCode = 0;
    std::string body;
    std::string error;
};

// Minimal WinHTTP GET helper: no external dependencies, TLS handled by the OS.
Response Get(
    const std::wstring& url,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    int timeoutSeconds);

}
