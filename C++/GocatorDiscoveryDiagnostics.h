#pragma once

#include <string_view>

namespace gocator
{

/**
 * @brief Returns whether a Gocator discovery exception means no device was found.
 *
 * The SDK reports an empty discovery as an exception instead of an empty list.
 * Keep that classification in one place so a normal missing camera stays
 * informational while other discovery failures remain warnings.
 */
[[nodiscard]] inline bool discoveryMeansNoDevices(const std::string_view message) noexcept
{
    return message.find("Failed in discovering instances") != std::string_view::npos;
}

} // namespace gocator
