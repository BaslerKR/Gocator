#pragma once

/**
 * @file GocatorDataSetGraphicsFrameAdapter.h
 * @brief LMI Gocator GDP dataset adapter for neutral GraphicsFrame data.
 */

#include "engine/GraphicsFrameAdapter.h"

#include <optional>

namespace GoPxLSdk
{
class GoDataSet;
}

class GocatorDataSetGraphicsFrameAdapter final
    : public GraphicsFrameAdapter<GocatorDataSetGraphicsFrameAdapter, GoPxLSdk::GoDataSet>
{
public:
    GocatorDataSetGraphicsFrameAdapter() = default;
    ~GocatorDataSetGraphicsFrameAdapter() = default;

    using GraphicsFrameAdapter<GocatorDataSetGraphicsFrameAdapter, GoPxLSdk::GoDataSet>::convertFrame;

private:
    friend class GraphicsFrameAdapter<GocatorDataSetGraphicsFrameAdapter, GoPxLSdk::GoDataSet>;

    [[nodiscard]] std::optional<GraphicsFrame> convertGraphicsFrame(
        const GoPxLSdk::GoDataSet& dataSet,
        const GraphicsFrameRequest& request) const;
};
