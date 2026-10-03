#pragma once

#include <mutex>
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

// Lets another thread stop a request that is waiting for the network (the
// program is closing): Cancel() closes the open handles of the request, and
// the call that waits on one of them returns at once with an error. A request
// that starts after Cancel() fails at once.
class Cancellation {
public:
    void Cancel();

    // Used by Get() for every handle it opens (a WinHTTP handle): Track()
    // returns false when the request is cancelled already; Untrack() returns
    // false when Cancel() has closed the handle, so it is not closed twice.
    bool Track(void* handle);
    bool Untrack(void* handle);

private:
    std::mutex m_mutex;
    std::vector<void*> m_handles;
    bool m_cancelled = false;
};

// Minimal WinHTTP GET helper: no external dependencies, TLS handled by the OS.
// Without followRedirects a redirect is returned as it is: its status code and
// the address it points to.
Response Get(
    const std::wstring& url,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    int timeoutSeconds,
    bool followRedirects = true,
    Cancellation* cancellation = nullptr);

}
