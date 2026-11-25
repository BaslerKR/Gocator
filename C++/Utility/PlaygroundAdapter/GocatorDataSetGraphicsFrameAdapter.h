#pragma once

/**
 * @file GocatorDataSetGraphicsFrameAdapter.h
 * @brief LMI Gocator GDP dataset adapter for neutral GraphicsFrame data.
 */

#include "engine/GraphicsFrameAdapter.h"
#include "engine/GraphicsImageWriter.h"
#include "Gocator.h"

#include <memory>
#include <mutex>
#include <optional>
#include <limits>

namespace GoPxLSdk
{
class GoDataSet;
}

/** First range rejection in a requested GDP dataset. No SDK buffers are retained. */
struct GocatorRangeRejection {
    enum class Reason {
        None, MissingRange, MissingData, InvalidDimensions, UnsupportedFormat, TruncatedData,
        InvalidLayout, SizeLimitExceeded, StorageRefused, StorageMisaligned
    };
    Reason reason = Reason::None;
    std::uint32_t partCount = 0;
    std::uint32_t part = (std::numeric_limits<std::uint32_t>::max)();
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t requestedStorageBytes = 0;
    bool frameRetained = false;
    bool imageRetained = false;
    bool rangeRetained = false;
};

struct GocatorImageRejection {
    GraphicsImageFailure reason = GraphicsImageFailure::None;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t frameSequence = 0;
    std::uint64_t pixelFormat = 0;
    std::uint64_t reportedStride = 0;
    std::uint64_t effectiveStride = 0;
    std::uint64_t minimumRowBytes = 0;
    std::uint64_t readableBytes = 0;
    std::uint64_t requiredBytes = 0;
    bool imageRetained = false;
};

class GocatorDataSetGraphicsFrameAdapter final
    : public GraphicsFrameAdapter<GocatorDataSetGraphicsFrameAdapter, GoPxLSdk::GoDataSet>
{
public:
    GocatorDataSetGraphicsFrameAdapter() = default;
    ~GocatorDataSetGraphicsFrameAdapter() = default;

    using GraphicsFrameAdapter<GocatorDataSetGraphicsFrameAdapter, GoPxLSdk::GoDataSet>::convertFrame;

    [[nodiscard]] std::optional<GraphicsFrame> convertFrameWithRejection(
        const GoPxLSdk::GoDataSet& dataSet, const GraphicsFrameRequest& request,
        GocatorRangeRejection& rejection, GocatorImageRejection* imageRejection = nullptr) const;

    /** Sets where range samples are written; empty restores storage of the adapter's own. */
    void setFrameStorage(GraphicsImageStorage storage);

private:
    friend class GraphicsFrameAdapter<GocatorDataSetGraphicsFrameAdapter, GoPxLSdk::GoDataSet>;

    [[nodiscard]] std::optional<GraphicsFrame> convertGraphicsFrame(
        const GoPxLSdk::GoDataSet& dataSet,
        const GraphicsFrameRequest& request,
        GocatorRangeRejection* rejection = nullptr,
        GocatorImageRejection* imageRejection = nullptr) const;

    mutable std::mutex _storageMutex;
    GraphicsImageStorage _storage;
};
