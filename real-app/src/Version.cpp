#include "Version.h"

#include <algorithm>
#include <string>

using namespace miniant::AutoUpdater;

namespace {

// A number of a version past this is not a version.
constexpr uint32_t TOO_BIG = UINT16_MAX + 1u;

// "v" and three numbers with dots between them, at the given position of the
// text: all the digits of a number belong to it (a number of the regular
// expression v(\d+)\.(\d+)\.(\d+), as the program read it before). Gives the
// position after the last digit, or npos when there is no version there. A
// number past 65535 is kept as TOO_BIG.
size_t ReadVersion(const std::string& text, size_t position, uint32_t (&numbers)[3]) {
    if (position >= text.size() || text[position] != 'v') {
        return std::string::npos;
    }

    ++position;

    for (int index = 0; index < 3; ++index) {
        if (index > 0) {
            if (position >= text.size() || text[position] != '.') {
                return std::string::npos;
            }

            ++position;
        }

        const size_t first = position;
        uint32_t value = 0;

        while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
            value = std::min(value * 10 + static_cast<uint32_t>(text[position] - '0'), TOO_BIG);
            ++position;
        }

        if (position == first) {
            return std::string::npos;
        }

        numbers[index] = value;
    }

    return position;
}

tl::expected<Version, VersionError> MakeVersion(const uint32_t (&numbers)[3]) {
    if (numbers[0] >= TOO_BIG || numbers[1] >= TOO_BIG || numbers[2] >= TOO_BIG) {
        return tl::make_unexpected(VersionError("Failed to parse version string."));
    }

    return Version(
        static_cast<uint16_t>(numbers[0]),
        static_cast<uint16_t>(numbers[1]),
        static_cast<uint16_t>(numbers[2]));
}

}

std::string Version::ToString() const {
    return "v" + std::to_string(m_major) + "." + std::to_string(m_minor) + "." + std::to_string(m_patch);
}

bool Version::operator< (const Version& rhs) const noexcept {
    if (m_major != rhs.m_major) {
        return m_major < rhs.m_major;
    }

    if (m_minor != rhs.m_minor) {
        return m_minor < rhs.m_minor;
    }

    return m_patch < rhs.m_patch;
}

bool Version::operator> (const Version& rhs) const noexcept {
    return rhs < *this;
}

bool Version::operator==(const Version& rhs) const noexcept {
    return m_major == rhs.m_major && m_minor == rhs.m_minor && m_patch == rhs.m_patch;
}

bool Version::operator!=(const Version& rhs) const noexcept {
    return !(*this == rhs);
}

bool Version::operator<=(const Version& rhs) const noexcept {
    return *this == rhs || *this < rhs;
}

bool Version::operator>=(const Version& rhs) const noexcept {
    return *this == rhs || *this > rhs;
}

// The whole string is the version.
tl::expected<Version, VersionError> Version::Parse(const std::string& versionString) {
    uint32_t numbers[3] = {};
    if (ReadVersion(versionString, 0, numbers) != versionString.size()) {
        return tl::make_unexpected(VersionError("Failed to parse version string."));
    }

    return MakeVersion(numbers);
}

// The first version in the string; a number of it past 65535 is an error, even
// if a correct version follows.
tl::expected<Version, VersionError> Version::Find(const std::string& string) {
    for (size_t position = string.find('v'); position != std::string::npos;
         position = string.find('v', position + 1)) {
        uint32_t numbers[3] = {};
        if (ReadVersion(string, position, numbers) != std::string::npos) {
            return MakeVersion(numbers);
        }
    }

    return tl::make_unexpected(VersionError("String does not contain properly formatted version."));
}
