#pragma once

#include <GoPxLSdk/GoJson.h>

#include <string>

namespace gocator {

[[nodiscard]] inline GoPxLSdk::GoJson outputRequest(
    const std::string& sourceId, const int outputId)
{
    GoPxLSdk::GoJson payload("{}");
    payload.Set("/source", sourceId);
    payload.Set("/outputId", outputId);
    payload.Set("/autoShift", true);
    return payload;
}

}
