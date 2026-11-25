#pragma once

#include "GocatorTypes.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gocator {

struct GocatorTargetEndpoint {
    std::string address;
    std::uint16_t controlPort = kDefaultControlPort;
    bool explicitPort = false;
};

struct GocatorCachedControlPort {
    std::uint16_t port = kDefaultControlPort;
    bool ambiguous = false;
};

[[nodiscard]] inline std::string targetIdForEndpoint(
    const std::string& address, const std::uint16_t controlPort)
{
    if (controlPort == 0 || controlPort == kDefaultControlPort) return address;
    return address + ":" + std::to_string(controlPort);
}

template<class Device>
[[nodiscard]] std::string targetIdForDevice(const Device& device)
{
    return targetIdForEndpoint(device.address, device.controlPort);
}

[[nodiscard]] inline GocatorTargetEndpoint parseTargetId(const std::string& targetId)
{
    const auto separator = targetId.rfind(':');
    if (separator != std::string::npos && separator > 0U && separator + 1 < targetId.size()
        && targetId.find(':') == separator) {
        int port = 0;
        const auto* begin = targetId.data() + separator + 1;
        const auto* end = targetId.data() + targetId.size();
        const auto parsed = std::from_chars(begin, end, port);
        if (parsed.ec == std::errc{} && parsed.ptr == end && port > 0 && port <= 65535)
            return {targetId.substr(0, separator), static_cast<std::uint16_t>(port), true};
    }
    return {targetId, kDefaultControlPort, false};
}

template<class Device>
[[nodiscard]] GocatorCachedControlPort controlPortForTarget(
    const std::vector<Device>& devices, const std::string& address)
{
    for (const auto& device : devices) {
        if (device.address == address && device.controlPort == kDefaultControlPort)
            return {kDefaultControlPort, false};
    }
    std::uint16_t first = 0;
    for (const auto& device : devices) {
        if (device.address != address || device.controlPort == 0) continue;
        if (first == 0) first = device.controlPort;
        else if (first != device.controlPort) return {0, true};
    }
    return {first == 0 ? kDefaultControlPort : first, false};
}

}
