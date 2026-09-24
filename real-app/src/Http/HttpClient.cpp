#include "HttpClient.h"

#include "../AppVersion.h"
#include "../Text.h"
#include "../Lang.h"
#include "../Windows/WindowsError.h"

#include <spdlog/fmt/fmt.h>

#include <Windows.h>
#include <winhttp.h>

#include <memory>

#pragma comment(lib, "winhttp.lib")

using namespace miniant::Http;

namespace {

// TLS 1.2 and 1.3, taken as literals so that the code does not depend on the
// SDK version it is compiled with.
constexpr DWORD SUPPORTED_SECURE_PROTOCOLS = 0x00000800 | 0x00002000;

constexpr size_t MAX_RESPONSE_BYTES = 4 * 1024 * 1024;

struct InternetHandleCloser {
    void operator()(void* handle) const {
        if (handle != nullptr) {
            ::WinHttpCloseHandle(static_cast<HINTERNET>(handle));
        }
    }
};

using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

}

Response miniant::Http::Get(
    const std::wstring& url,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    int timeoutSeconds) {
    Response response;

    if (url.empty()) {
        response.error = Lang::Utf8(Lang::Str::ErrEmptyUrl);
        return response;
    }

    const std::wstring userAgent = std::wstring(L"REAL/") + Text::ToWide(AppInfo::VERSION.ToString());

    InternetHandle session(::WinHttpOpen(
        userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        response.error = Windows::DescribeLastError();
        return response;
    }

    const int timeoutMs = (timeoutSeconds > 0 ? timeoutSeconds : 15) * 1000;
    ::WinHttpSetTimeouts(static_cast<HINTERNET>(session.get()), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    ::WinHttpSetOption(static_cast<HINTERNET>(session.get()), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    DWORD protocols = SUPPORTED_SECURE_PROTOCOLS;
    ::WinHttpSetOption(static_cast<HINTERNET>(session.get()), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

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

    InternetHandle connection(::WinHttpConnect(
        static_cast<HINTERNET>(session.get()),
        host.c_str(),
        components.nPort,
        0));
    if (!connection) {
        response.error = Windows::DescribeLastError();
        return response;
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;

    InternetHandle request(::WinHttpOpenRequest(
        static_cast<HINTERNET>(connection.get()),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));
    if (!request) {
        response.error = Windows::DescribeLastError();
        return response;
    }

    DWORD requestProtocols = SUPPORTED_SECURE_PROTOCOLS;
    ::WinHttpSetOption(static_cast<HINTERNET>(request.get()), WINHTTP_OPTION_SECURE_PROTOCOLS, &requestProtocols, sizeof(requestProtocols));

    for (const auto& header : headers) {
        const std::wstring line = header.first + L": " + header.second;
        ::WinHttpAddRequestHeaders(
            static_cast<HINTERNET>(request.get()),
            line.c_str(),
            static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (::WinHttpSendRequest(
        static_cast<HINTERNET>(request.get()),
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0) == FALSE) {
        response.error = Windows::DescribeLastError();
        return response;
    }

    if (::WinHttpReceiveResponse(static_cast<HINTERNET>(request.get()), nullptr) == FALSE) {
        response.error = Windows::DescribeLastError();
        return response;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (::WinHttpQueryHeaders(
        static_cast<HINTERNET>(request.get()),
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

    char buffer[4096];
    for (;;) {
        DWORD bytesRead = 0;
        if (::WinHttpReadData(static_cast<HINTERNET>(request.get()), buffer, sizeof(buffer), &bytesRead) == FALSE) {
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
