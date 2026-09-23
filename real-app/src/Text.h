#pragma once

#include <string>

namespace miniant::Text {

std::string ToUtf8(const std::wstring& text);
std::string ToUtf8(const wchar_t* text);
std::wstring ToWide(const std::string& text);
std::wstring ToWide(const char* text);

std::string ToLowerAscii(std::string value);
std::string Trim(const std::string& value);

// Removes // and /* */ comments so that a human-readable JSON file with comments can be parsed.
// String literals are respected. Newlines are preserved (line numbers stay valid).
std::string StripJsonComments(const std::string& text);

// Removes a leading UTF-8 BOM if present.
std::string StripUtf8Bom(const std::string& text);

}
