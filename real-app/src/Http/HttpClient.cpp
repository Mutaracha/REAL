#include "HttpClient.h"

#include "../AppVersion.h"
#include "../Text.h"
#include "../Lang.h"
#include "../Windows/WindowsError.h"

#include <spdlog/fmt/fmt.h>

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>

using namespace miniant::Http;

namespace {

// TLS 1.2 and 1.3, taken as literals so that the code does not depend on the
// SDK version it is compiled with.
constexpr DWORD SUPPORTED_SECURE_PROTOCOLS = 0x00000800 | 0x00002000;

constexpr size_t MAX_RESPONSE_BYTES = 4 * 1024 * 1024;

// The handles of one request, closed in the reverse order of opening when the
// request ends. A cancellation may have closed them already; it tells which.
class RequestHandles {
public:
    explicit RequestHandles(Cancellation* cancellation):
        m_cancellation(cancellation) {}

    ~RequestHandles() {
        for (auto it = m_handles.rbegin(); it != m_handles.rend(); ++it) {
            if (m_cancellation == nullptr || m_cancellation->Untrack(*it)) {
                ::WinHttpCloseHandle(*it);
            }
        }
    }

    RequestHandles(const RequestHandles&) = delete;
    RequestHandles& operator=(const RequestHandles&) = delete;

    // Takes a handle that has just been opened. False when there is none (the
    // error is in GetLastError) or the request has been cancelled meanwhile
    // (the handle is closed at once).
    bool Add(HINTERNET handle, bool& cancelled) {
        cancelled = false;

        if (handle == nullptr) {
            return false;
        }

        if (m_cancellation != nullptr && !m_cancellation->Track(handle)) {
            ::WinHttpCloseHandle(handle);
            cancelled = true;
            return false;
        }

        m_handles.push_back(handle);
        return true;
    }

private:
    Cancellation* m_cancellation;
    std::vector<HINTERNET> m_handles;
};

// The error of a failed call: a cancelled request says so in plain words
// instead of the code of a closed handle.
std::string FailureText(bool cancelled) {
    return cancelled ? std::string(miniant::Lang::Utf8(miniant::Lang::Str::ErrRequestCancelled))
                     : miniant::Windows::DescribeLastError();
}

}

void Cancellation::Cancel() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelled = true;

    // The request handle first, the session last: the reverse of the order in
    // which they were opened.
    for (auto it = m_handles.rbegin(); it != m_handles.rend(); ++it) {
        ::WinHttpCloseHandle(static_cast<HINTERNET>(*it));
    }

    m_handles.clear();
}

bool Cancellation::Track(void* handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cancelled) {
        return false;
    }

    m_handles.push_back(handle);
    return true;
}

bool Cancellation::Untrack(void* handle) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto it = std::find(m_handles.begin(), m_handles.end(), handle);
    if (it == m_handles.end()) {
        return false;
    }

    m_handles.erase(it);
    return true;
}

Response miniant::Http::Get(
    const std::wstring& url,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    int timeoutSeconds,
    bool followRedirects,
    Cancellation* cancellation) {
    Response response;

    if (url.empty()) {
        response.error = Lang::Utf8(Lang::Str::ErrEmptyUrl);
        return response;
    }

    const std::wstring userAgent = std::wstring(L"REAL/") + Text::ToWide(AppInfo::VERSION.ToString());

    RequestHandles handles(cancellation);
    bool cancelled = false;

    const HINTERNET session = ::WinHttpOpen(
        userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!handles.Add(session, cancelled)) {
        response.error = FailureText(cancelled);
        return response;
    }

    const int timeoutMs = (timeoutSeconds > 0 ? timeoutSeconds : 15) * 1000;
    ::WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    DWORD redirectPolicy = followRedirects
        ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS
        : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    ::WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    DWORD protocols = SUPPORTED_SECURE_PROTOCOLS;
    ::WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (::WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) == FALSE) {
        response.error = fmt::format(Lang::Utf8(Lang::Str::ErrUrlParse), Windows::DescribeLastError());
        return response;
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }

    if (path.empty()) {
        path = L"/";
    }

    const HINTERNET connection = ::WinHttpConnect(session, host.c_str(), components.nPort, 0);
    if (!handles.Add(connection, cancelled)) {
        response.error = FailureText(cancelled);
        return response;
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;

    const HINTERNET request = ::WinHttpOpenRequest(
        connection,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!handles.Add(request, cancelled)) {
        response.error = FailureText(cancelled);
        return response;
    }

    DWORD requestProtocols = SUPPORTED_SECURE_PROTOCOLS;
    ::WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS, &requestProtocols, sizeof(requestProtocols));

    for (const auto& header : headers) {
        const std::wstring line = header.first + L": " + header.second;
        ::WinHttpAddRequestHeaders(
            request,
            line.c_str(),
            static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (::WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0) == FALSE) {
        response.error = Windows::DescribeLastError();
        return response;
    }

    if (::WinHttpReceiveResponse(request, nullptr) == FALSE) {
        response.error = Windows::DescribeLastError();
        return response;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (::WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX) == FALSE) {
        response.error = Windows::DescribeLastError();
        return response;
    }

    response.statusCode = statusCode;
    response.networkOk = true;

    // The address of a redirect: the size is asked first, then the header is
    // read into a buffer of that size (in bytes, with the terminating zero).
    if (!followRedirects && statusCode >= 300 && statusCode < 400) {
        DWORD locationSize = 0;
        ::WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_LOCATION,
            WINHTTP_HEADER_NAME_BY_INDEX,
            WINHTTP_NO_OUTPUT_BUFFER,
            &locationSize,
            WINHTTP_NO_HEADER_INDEX);

        if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER && locationSize > 0) {
            std::wstring location(locationSize / sizeof(wchar_t) + 1, L'\0');
            if (::WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX,
                location.data(),
                &locationSize,
                WINHTTP_NO_HEADER_INDEX) != FALSE) {
                location.resize(locationSize / sizeof(wchar_t));
                response.location = Text::ToUtf8(location);
            }
        }
    }

    char buffer[4096];
    for (;;) {
        DWORD bytesRead = 0;
        if (::WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead) == FALSE) {
            response.error = Windows::DescribeLastError();
            response.networkOk = false;
            return response;
        }

        if (bytesRead == 0) {
            break;
        }

        if (response.body.size() + bytesRead > MAX_RESPONSE_BYTES) {
            response.error = Lang::Utf8(Lang::Str::ErrResponseTooLarge);
            response.networkOk = false;
            return response;
        }

        response.body.append(buffer, bytesRead);
    }

    return response;
}
