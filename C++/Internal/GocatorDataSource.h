#pragma once

#include <GoPxLSdk/GoGdpMsg/GoGdpMsgDef.h>
#include <GoPxLSdk/GoJson.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gocator {

struct GocatorDataSource {
    std::string id;
    GoPxLSdk::MessageType type = GoPxLSdk::MessageType::NULL_TYPE;
};

struct GocatorEngineDetection {
    std::optional<std::string> engineId;
    bool failed = false;
};

template<class Read>
[[nodiscard]] GocatorEngineDetection detectEngineId(Read&& read)
{
    try {
        const auto response = read("/scan/visibleSensors/");
        if (!response.IsArray("/sensors")) return {std::nullopt, true};
        const auto sensors = response.At("/sensors");
        if (sensors.Size() == 0U) return {std::nullopt, false};
        const auto sensor = sensors.At("/0");
        if (!sensor.IsString("/engineId")) return {std::nullopt, true};
        const auto engineId = sensor.At("/engineId").template Get<std::string>();
        if (engineId.empty()) return {std::nullopt, true};
        return {engineId, false};
    } catch (...) {
        return {std::nullopt, true};
    }
}

[[nodiscard]] inline bool isRangeDataType(const GoPxLSdk::MessageType type)
{
    return type == GoPxLSdk::MessageType::UNIFORM_PROFILE
        || type == GoPxLSdk::MessageType::PROFILE_POINT_CLOUD
        || type == GoPxLSdk::MessageType::UNIFORM_SURFACE
        || type == GoPxLSdk::MessageType::SURFACE_POINT_CLOUD;
}

[[nodiscard]] inline std::optional<std::string> selectReplayDataSource(
    const std::vector<GocatorDataSource>& sources)
{
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& source : sources) {
            if (source.id.empty() || !isRangeDataType(source.type)) continue;
            const bool surface = source.type == GoPxLSdk::MessageType::UNIFORM_SURFACE
                || source.type == GoPxLSdk::MessageType::SURFACE_POINT_CLOUD;
            if (pass == 0 && surface) continue;
            if (pass == 1 && !surface) continue;
            return source.id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<std::string> selectReplayImageDataSource(
    const std::vector<GocatorDataSource>& sources)
{
    for (const auto& source : sources) {
        if (!source.id.empty() && source.type == GoPxLSdk::MessageType::IMAGE)
            return source.id;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::string dataSourceIdFromLink(const std::string& link)
{
    constexpr std::string_view marker = "/dataSources/";
    const auto position = link.find(marker);
    if (position == std::string::npos) return {};
    auto id = link.substr(position + marker.size());
    while (!id.empty() && id.back() == '/') id.pop_back();
    if (id.empty() || id.find('/') != std::string::npos) return {};
    return id;
}

template<class Read>
[[nodiscard]] std::vector<GocatorDataSource> replayDataSources(Read&& read)
{
    const auto collection = read("/dataSources/");
    if (!collection.IsObject() || !collection.IsArray("/_embedded/item"))
        throw std::runtime_error("Malformed Gocator data source collection.");

    std::vector<GocatorDataSource> sources;
    const auto items = collection.At("/_embedded/item");
    sources.reserve(items.Size());
    for (std::size_t index = 0; index < items.Size(); ++index) {
        const auto item = items.At(static_cast<int>(index));
        if (!item.IsString("/_links/self/href"))
            throw std::runtime_error("Malformed Gocator data source link.");
        const auto id = dataSourceIdFromLink(item.At("/_links/self/href").template Get<std::string>());
        if (id.empty()) throw std::runtime_error("Malformed Gocator data source link.");
        const auto data = read("/dataSources/" + id);
        if (!data.IsInteger("/dataFormat/type"))
            throw std::runtime_error("Malformed Gocator data source format.");
        const int type = data.At("/dataFormat/type").template Get<int>();
        if (type < 0 || type > (std::numeric_limits<std::uint16_t>::max)())
            throw std::runtime_error("Unsupported Gocator data source format value.");
        sources.push_back({id, static_cast<GoPxLSdk::MessageType>(type)});
    }
    return sources;
}

}
