#pragma once

/**
 * @file GocatorDataSetGraphicsFrameAdapter.h
 * @brief LMI Gocator GDP dataset adapter for neutral GraphicsFrame data.
 */

#include "engine/GraphicsFrame.h"
#include "engine/GraphicsFrameContext.h"
#include "engine/GraphicsImageWriter.h"
#include "Gocator.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

namespace GoPxLSdk
{
class GoDataSet;
}

/** First range rejection in a requested GDP dataset. No SDK buffers are retained. */
struct GocatorRangeRejection {
    enum class Reason {
        None, MissingRange, MissingData, InvalidDimensions, UnsupportedFormat, TruncatedData,
        InvalidLayout, SizeLimitExceeded, StorageRefused
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
{
public:
    GocatorDataSetGraphicsFrameAdapter() = default;
    ~GocatorDataSetGraphicsFrameAdapter() = default;

    [[nodiscard]] std::optional<GraphicsFrame> convertFrame(
        const GoPxLSdk::GoDataSet& dataSet,
        const GraphicsFrameRequest& request,
        GraphicsFrameContextSource& contextSource) const;

    [[nodiscard]] std::optional<GraphicsFrame> convertFrameWithRejection(
        const GoPxLSdk::GoDataSet& dataSet,
        const GraphicsFrameRequest& request,
        GraphicsFrameContextSource& contextSource,
        GocatorRangeRejection& rejection,
        GocatorImageRejection* imageRejection = nullptr,
        std::optional<std::uint64_t> frameIndex = std::nullopt,
        GraphicsFrameError* storageError = nullptr,
        const std::atomic<bool>* cancelled = nullptr) const;

private:
    [[nodiscard]] std::optional<GraphicsFrame> convertGraphicsFrame(
        const GoPxLSdk::GoDataSet& dataSet,
        const GraphicsFrameRequest& request,
        GraphicsFrameContextSource& contextSource,
        GocatorRangeRejection* rejection = nullptr,
        GocatorImageRejection* imageRejection = nullptr,
        std::optional<std::uint64_t> frameIndex = std::nullopt,
        GraphicsFrameError* storageError = nullptr,
        const std::atomic<bool>* cancelled = nullptr) const;
};
