#pragma once

/**
 * @file GocatorDataSetGraphicsFrameAdapter.h
 * @brief LMI Gocator GDP dataset adapter for neutral GraphicsFrame data.
 */

#include "engine/GraphicsFrame.h"
#include "engine/GraphicsFrameContext.h"
#include "engine/GraphicsImageWriter.h"
#include "Gocator.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace GoPxLSdk
{
class GoDataSet;
}

/**
 * One range-side anomaly observed while fitting a requested GDP dataset. The adapter never drops a
 * part for a validation reason: the part is fitted into the GraphicsEngine layout and published, and
 * this record only reports what was wrong with the delivered data. No SDK buffers are retained.
 */
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

/** One image-side anomaly (or a storage refusal) observed while fitting a GDP image part. */
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

/**
 * Bounded, allocation-free anomaly observations for one dataset.
 *
 * The first `capacity` observations of each side are kept in order; later ones only increment a
 * per-reason counter so the stream can still report every anomaly without allocating.
 */
struct GocatorFrameAnomalies {
    static constexpr std::size_t capacity = 8;
    static constexpr std::size_t rangeReasonCount =
        static_cast<std::size_t>(GocatorRangeRejection::Reason::StorageRefused) + 1U;
    static constexpr std::size_t imageReasonCount =
        static_cast<std::size_t>(GraphicsImageFailure::StorageRefused) + 1U;

    std::array<GocatorRangeRejection, capacity> ranges{};
    std::size_t rangeCount = 0;
    std::array<std::size_t, rangeReasonCount> rangeOverflowByReason{};
    std::array<GocatorImageRejection, capacity> images{};
    std::size_t imageCount = 0;
    std::array<std::size_t, imageReasonCount> imageOverflowByReason{};

    void add(const GocatorRangeRejection& evidence) noexcept
    {
        if (rangeCount < ranges.size()) ranges[rangeCount++] = evidence;
        else ++rangeOverflowByReason[static_cast<std::size_t>(evidence.reason)];
    }

    void add(const GocatorImageRejection& evidence) noexcept
    {
        if (imageCount < images.size()) images[imageCount++] = evidence;
        else ++imageOverflowByReason[static_cast<std::size_t>(evidence.reason)];
    }
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
        const std::atomic<bool>* cancelled = nullptr,
        GocatorFrameAnomalies* anomalies = nullptr) const;

private:
    [[nodiscard]] std::optional<GraphicsFrame> convertGraphicsFrame(
        const GoPxLSdk::GoDataSet& dataSet,
        const GraphicsFrameRequest& request,
        GraphicsFrameContextSource& contextSource,
        GocatorRangeRejection* rejection = nullptr,
        GocatorImageRejection* imageRejection = nullptr,
        std::optional<std::uint64_t> frameIndex = std::nullopt,
        GraphicsFrameError* storageError = nullptr,
        const std::atomic<bool>* cancelled = nullptr,
        GocatorFrameAnomalies* anomalies = nullptr) const;
};
