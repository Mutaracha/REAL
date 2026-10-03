#pragma once

#include <string>
#include <utility>
#include <vector>

namespace miniant::Http {

struct Response {
    bool networkOk = false;
    unsigned long statusCode = 0;
    // The target of a redirect, when redirects are not followed.
    std::string location;
    std::string body;
    std::string error;
};

// Minimal WinHTTP GET helper: no external dependencies, TLS handled by the OS.
// Without followRedirects a redirect is returned as it is: its status code and
// the address it points to.
Response Get(
    const std::wstring& url,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    int timeoutSeconds,
    bool followRedirects = true);

}
