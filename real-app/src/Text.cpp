#include "Text.h"

#include <Windows.h>

#include <cctype>

using namespace miniant::Text;

std::string miniant::Text::ToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    const int written = ::WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], size, nullptr, nullptr);

    result.resize(written > 0 ? static_cast<size_t>(written) : 0);
    return result;
}

std::string miniant::Text::ToUtf8(const wchar_t* text) {
    if (text == nullptr) {
        return {};
    }

    return ToUtf8(std::wstring(text));
}

std::wstring miniant::Text::ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    const int written = ::MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], size);

    result.resize(written > 0 ? static_cast<size_t>(written) : 0);
    return result;
}

std::wstring miniant::Text::ToWide(const char* text) {
    if (text == nullptr) {
        return {};
    }

    return ToWide(std::string(text));
}

std::string miniant::Text::ToLowerAscii(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return value;
}

std::string miniant::Text::Trim(const std::string& value) {
    size_t begin = 0;
    size_t end = value.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string miniant::Text::StripUtf8Bom(const std::string& text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        return text.substr(3);
    }

    return text;
}

std::string miniant::Text::StripJsonComments(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];

        if (inString) {
            result.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }

            continue;
        }

        if (c == '"') {
            inString = true;
            result.push_back(c);
            continue;
        }

        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }

            if (i < text.size()) {
                result.push_back('\n');
            }

            continue;
        }

        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                if (text[i] == '\n') {
                    result.push_back('\n');
                }

                ++i;
            }

            i += 1;
            continue;
        }

        result.push_back(c);
    }

    return result;
}
