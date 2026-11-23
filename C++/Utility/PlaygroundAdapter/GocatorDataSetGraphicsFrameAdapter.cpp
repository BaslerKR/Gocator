#include "GocatorDataSetGraphicsFrameAdapter.h"

#include "GocatorRangeConversion.h"
#include "graphics/data/GraphicsRangeFrameWriter.h"

#include <GoPxLSdk/GoDataSet.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpMsgDef.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfilePointCloud.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfileUniform.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpStamp.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpSurfacePointCloud.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpSurfaceUniform.h>
#include <kApi/Data/kArray1.h>
#include <kApi/Data/kArray2.h>
#include <kApi/kApiDef.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace
{
#define GRAPHICSENGINE_OMP_PARALLEL_FOR _Pragma("omp parallel for")
#define GRAPHICSENGINE_OMP_PARALLEL_FOR_REDUCTION_SUM _Pragma("omp parallel for reduction(+:validPointCount)")

struct StampInfo
{
    std::uint64_t frameIndex = 0;
    bool valid = false;
};

[[nodiscard]] std::optional<int> positiveInt(const k32u value) noexcept
{
    if (value == 0U || value > static_cast<k32u>(std::numeric_limits<int>::max()))
    {
        return std::nullopt;
    }

    return static_cast<int>(value);
}

[[nodiscard]] std::optional<std::size_t> pixelCount(const int width, const int height) noexcept
{
    if (width <= 0 || height <= 0)
    {
        return std::nullopt;
    }

    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

[[nodiscard]] std::string frameId(const GoPxLSdk::GoGdpMsg& msg, const StampInfo& stamp)
{
    if (stamp.valid)
    {
        return std::to_string(stamp.frameIndex);
    }

    return std::to_string(msg.DataSetId());
}

void applyCommonFrameFields(RangeFrame& frame,
                            const int width,
                            const int height,
                            const kPoint3d64f& resolution,
                            const kPoint3d64f& offset,
                            const std::string& id)
{
    frame.width = width;
    frame.height = height;
    frame.lengthUnit = GraphicsLengthUnit::Millimeter;
    frame.xScale = resolution.x != 0.0 ? resolution.x : 1.0;
    frame.yScale = resolution.y != 0.0 ? resolution.y : 1.0;
    frame.zScale = 1.0;
    frame.xOffset = offset.x;
    frame.yOffset = offset.y;
    frame.zOffset = 0.0;
    frame.rangeField = {
        "gocator.range",
        "Range",
        "mm",
        MeasurementValueDomain::Calibrated,
        MeasurementSampleKind::GridSample,
        16U};
    frame.intensityField = {
        "gocator.intensity",
        "Intensity",
        "",
        MeasurementValueDomain::Native,
        MeasurementSampleKind::GridSample,
        0U};
    frame.sensorType = "LMI Gocator";
    frame.frameId = id;
}

[[nodiscard]] std::uint8_t intensityBitsFromItemSize(const kSize itemSize) noexcept
{
    if (itemSize == sizeof(k8u))
    {
        return 8U;
    }
    if (itemSize == sizeof(k16u))
    {
        return 16U;
    }

    return 0U;
}

[[nodiscard]] std::uint8_t array1IntensityBits(const kArray1 intensities,
                                               const std::size_t width)
{
    if (intensities == kNULL || kArray1_Length(intensities) < width)
    {
        return 0U;
    }
    return intensityBitsFromItemSize(kArray1_ItemSize(intensities));
}

void writeArray1Intensity(const kArray1 intensities,
                          const std::size_t width,
                          float* values)
{
    if (kArray1_ItemSize(intensities) == sizeof(k8u))
    {
        const auto* src = kArray1_DataT(intensities, k8u);
        GRAPHICSENGINE_OMP_PARALLEL_FOR
        for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(width); ++index)
        {
            values[static_cast<std::size_t>(index)] = static_cast<float>(src[index]);
        }
        return;
    }

    const auto* src = kArray1_DataT(intensities, k16u);
    GRAPHICSENGINE_OMP_PARALLEL_FOR
    for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(width); ++index)
    {
        values[static_cast<std::size_t>(index)] = static_cast<float>(src[index]);
    }
}

[[nodiscard]] std::uint8_t array2IntensityBits(const kArray2 intensities,
                                               const int width,
                                               const int height)
{
    const auto count = pixelCount(width, height);
    if (!count.has_value()
        || intensities == kNULL
        || kArray2_Length(intensities, 0) < static_cast<kSize>(height)
        || kArray2_Length(intensities, 1) < static_cast<kSize>(width))
    {
        return 0U;
    }
    return intensityBitsFromItemSize(kArray2_ItemSize(intensities));
}

void writeArray2Intensity(const kArray2 intensities,
                          const int width,
                          const int height,
                          float* values)
{
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (kArray2_ItemSize(intensities) == sizeof(k8u))
    {
        const auto* src = kArray2_DataT(intensities, k8u);
        GRAPHICSENGINE_OMP_PARALLEL_FOR
        for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(count); ++index)
        {
            values[static_cast<std::size_t>(index)] = static_cast<float>(src[index]);
        }
        return;
    }

    const auto* src = kArray2_DataT(intensities, k16u);
    GRAPHICSENGINE_OMP_PARALLEL_FOR
    for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(count); ++index)
    {
        values[static_cast<std::size_t>(index)] = static_cast<float>(src[index]);
    }
}
[[nodiscard]] StampInfo readStamp(const GoPxLSdk::GoDataSet& dataSet)
{
    for (std::size_t index = 0; index < dataSet.Count(); ++index)
    {
        const GoPxLSdk::GoGdpMsg& msg = dataSet.GdpMsgAt(index);
        if (msg.Type() == GoPxLSdk::MessageType::STAMP)
        {
            const auto& stamp = static_cast<const GoPxLSdk::GoGdpStamp&>(msg);
            return {static_cast<std::uint64_t>(stamp.FrameIndex()), true};
        }
    }

    return {};
}

[[nodiscard]] GraphicsFrame makeFrame(RangeFrame&& frame,
                                        const GoPxLSdk::GoGdpMsg& msg,
                                        const StampInfo& stamp)
{
    GraphicsFrame result;
    result.addRange("range", std::move(frame), "Range");
    result.metadata.sourceName = "LMI Gocator";
    result.metadata.frameId = frameId(msg, stamp);
    result.metadata.frameIndex = stamp.valid ? stamp.frameIndex : msg.DataSetId();
    return result;
}

using RangeReason = GocatorRangeRejection::Reason;

std::nullopt_t rejectRange(GocatorRangeRejection* rejection, const RangeReason reason,
                           const int width, const int height)
{
    if (rejection) {
        rejection->reason = reason;
        rejection->width = static_cast<std::uint32_t>(width);
        rejection->height = static_cast<std::uint32_t>(height);
    }
    return std::nullopt;
}

std::nullopt_t rejectRangeStorage(GocatorRangeRejection* rejection, const GraphicsRangeFrameWriter& writer,
                                  const int width, const int height)
{
    if (!rejection) return std::nullopt;
    rejection->requestedStorageBytes = writer.blockBytes();
    switch (writer.failure()) {
    case GraphicsRangeFrameWriterFailure::InvalidDimensions:
        return rejectRange(rejection, RangeReason::InvalidDimensions, width, height);
    case GraphicsRangeFrameWriterFailure::SizeLimitExceeded:
        return rejectRange(rejection, RangeReason::SizeLimitExceeded, width, height);
    case GraphicsRangeFrameWriterFailure::StorageRefused:
        return rejectRange(rejection, RangeReason::StorageRefused, width, height);
    case GraphicsRangeFrameWriterFailure::MisalignedStorage:
        return rejectRange(rejection, RangeReason::StorageMisaligned, width, height);
    case GraphicsRangeFrameWriterFailure::None:
        return rejectRange(rejection, RangeReason::InvalidLayout, width, height);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<RangeFrame> convertUniformSurface(
    const GoPxLSdk::GoGdpSurfaceUniform& msg,
    const StampInfo& stamp,
    const GraphicsFrameRequest& request,
    const GraphicsImageStorage& storage, GocatorRangeRejection* rejection)
{
    const auto width = positiveInt(msg.Width());
    const auto height = positiveInt(msg.Length());
    if (!width.has_value() || !height.has_value())
    {
        return rejectRange(rejection, RangeReason::InvalidDimensions, width.value_or(0), height.value_or(0));
    }

    const auto count = pixelCount(*width, *height);
    const kArray2 ranges = msg.Ranges();
    if (!count) return rejectRange(rejection, RangeReason::SizeLimitExceeded, *width, *height);
    if (ranges == kNULL) return rejectRange(rejection, RangeReason::MissingData, *width, *height);
    if (kArray2_ItemSize(ranges) != sizeof(k16s))
        return rejectRange(rejection, RangeReason::UnsupportedFormat, *width, *height);
    if (kArray2_Length(ranges, 0) < static_cast<kSize>(*height)
        || kArray2_Length(ranges, 1) < static_cast<kSize>(*width))
        return rejectRange(rejection, RangeReason::TruncatedData, *width, *height);

    const auto* src = kArray2_DataT(ranges, k16s);
    const std::uint8_t intensityBits = request.includeRangeAuxiliaryChannels
        ? array2IntensityBits(msg.Intensities(), *width, *height)
        : 0U;

    GraphicsRangeFrameLayout layout;
    layout.width = *width;
    layout.height = *height;
    layout.channels.validMask = gocator_range::scalarHasInvalid(src, *count);
    layout.channels.intensity = intensityBits != 0U;
    GraphicsRangeFrameWriter writer(layout, storage);
    if (!writer.isValid())
    {
        return rejectRangeStorage(rejection, writer, *width, *height);
    }

    const kPoint3d64f resolution = msg.Resolution();
    const kPoint3d64f offset = msg.Offset();
    gocator_range::writeScalarZ(src, *count, resolution.z, offset.z,
        writer.zData(), writer.validMaskData());
    if (layout.channels.intensity)
    {
        writeArray2Intensity(msg.Intensities(), *width, *height, writer.intensityData());
    }

    RangeFrame frame = writer.takeFrame();
    applyCommonFrameFields(frame, *width, *height, resolution, offset, frameId(msg, stamp));
    if (layout.channels.intensity)
    {
        frame.intensityBits = intensityBits;
        frame.intensityField.bitsPerSample = intensityBits;
    }
    if (!frame.isValid()) return rejectRange(rejection, RangeReason::InvalidLayout, *width, *height);
    return frame;
}
[[nodiscard]] std::optional<RangeFrame> convertSurfacePointCloud(
    const GoPxLSdk::GoGdpSurfacePointCloud& msg,
    const StampInfo& stamp,
    const GraphicsFrameRequest& request,
    const GraphicsImageStorage& storage, GocatorRangeRejection* rejection)
{
    const auto width = positiveInt(msg.Width());
    const auto height = positiveInt(msg.Length());
    if (!width.has_value() || !height.has_value())
    {
        return rejectRange(rejection, RangeReason::InvalidDimensions, width.value_or(0), height.value_or(0));
    }

    const auto count = pixelCount(*width, *height);
    const kArray2 ranges = msg.Ranges();
    if (!count) return rejectRange(rejection, RangeReason::SizeLimitExceeded, *width, *height);
    if (ranges == kNULL) return rejectRange(rejection, RangeReason::MissingData, *width, *height);
    if (kArray2_ItemSize(ranges) != sizeof(kPoint3d16s))
        return rejectRange(rejection, RangeReason::UnsupportedFormat, *width, *height);
    if (kArray2_Length(ranges, 0) < static_cast<kSize>(*height)
        || kArray2_Length(ranges, 1) < static_cast<kSize>(*width))
        return rejectRange(rejection, RangeReason::TruncatedData, *width, *height);

    const auto* src = reinterpret_cast<const std::int16_t*>(kArray2_DataT(ranges, kPoint3d16s));
    const std::uint8_t intensityBits = request.includeRangeAuxiliaryChannels
        ? array2IntensityBits(msg.Intensities(), *width, *height)
        : 0U;

    GraphicsRangeFrameLayout layout;
    layout.width = *width;
    layout.height = *height;
    layout.channels.x = true;
    layout.channels.y = true;
    layout.channels.validMask = gocator_range::surfacePointsHaveInvalid(src, *count);
    layout.channels.intensity = intensityBits != 0U;
    GraphicsRangeFrameWriter writer(layout, storage);
    if (!writer.isValid())
    {
        return rejectRangeStorage(rejection, writer, *width, *height);
    }

    const kPoint3d64f resolution = msg.Resolution();
    const kPoint3d64f offset = msg.Offset();
    gocator_range::writeSurfacePoints(src, *count,
        resolution.x, resolution.y, resolution.z,
        offset.x, offset.y, offset.z,
        writer.xData(), writer.yData(), writer.zData(), writer.validMaskData());
    if (layout.channels.intensity)
    {
        writeArray2Intensity(msg.Intensities(), *width, *height, writer.intensityData());
    }

    RangeFrame frame = writer.takeFrame();
    applyCommonFrameFields(frame, *width, *height, resolution, offset, frameId(msg, stamp));
    frame.xyCoordinateMode = RangeFrameXYCoordinateMode::ExplicitXY;
    if (layout.channels.intensity)
    {
        frame.intensityBits = intensityBits;
        frame.intensityField.bitsPerSample = intensityBits;
    }
    if (!frame.isValid()) return rejectRange(rejection, RangeReason::InvalidLayout, *width, *height);
    return frame;
}
[[nodiscard]] std::optional<RangeFrame> convertUniformProfile(
    const GoPxLSdk::GoGdpProfileUniform& msg,
    const StampInfo& stamp,
    const GraphicsFrameRequest& request,
    const GraphicsImageStorage& storage, GocatorRangeRejection* rejection)
{
    const auto width = positiveInt(msg.Width());
    if (!width.has_value())
    {
        return rejectRange(rejection, RangeReason::InvalidDimensions, 0, 1);
    }

    const std::size_t count = static_cast<std::size_t>(*width);
    const kArray1 ranges = msg.Ranges();
    if (ranges == kNULL) return rejectRange(rejection, RangeReason::MissingData, *width, 1);
    if (kArray1_ItemSize(ranges) != sizeof(k16s))
        return rejectRange(rejection, RangeReason::UnsupportedFormat, *width, 1);
    if (kArray1_Length(ranges) < count)
        return rejectRange(rejection, RangeReason::TruncatedData, *width, 1);

    const auto* src = kArray1_DataT(ranges, k16s);
    const std::uint8_t intensityBits = request.includeRangeAuxiliaryChannels
        ? array1IntensityBits(msg.Intensities(), count)
        : 0U;

    GraphicsRangeFrameLayout layout;
    layout.width = *width;
    layout.height = 1;
    layout.channels.validMask = gocator_range::scalarHasInvalid(src, count);
    layout.channels.intensity = intensityBits != 0U;
    GraphicsRangeFrameWriter writer(layout, storage);
    if (!writer.isValid())
    {
        return rejectRangeStorage(rejection, writer, *width, 1);
    }

    const kPoint3d64f resolution = msg.Resolution();
    const kPoint3d64f offset = msg.Offset();
    gocator_range::writeScalarZ(src, count, resolution.z, offset.z,
        writer.zData(), writer.validMaskData());
    if (layout.channels.intensity)
    {
        writeArray1Intensity(msg.Intensities(), count, writer.intensityData());
    }

    RangeFrame frame = writer.takeFrame();
    applyCommonFrameFields(frame, *width, 1, resolution, offset, frameId(msg, stamp));
    frame.yScale = 1.0;
    if (layout.channels.intensity)
    {
        frame.intensityBits = intensityBits;
        frame.intensityField.bitsPerSample = intensityBits;
    }
    if (!frame.isValid()) return rejectRange(rejection, RangeReason::InvalidLayout, *width, 1);
    return frame;
}
[[nodiscard]] std::optional<RangeFrame> convertProfilePointCloud(
    const GoPxLSdk::GoGdpProfilePointCloud& msg,
    const StampInfo& stamp,
    const GraphicsFrameRequest& request,
    const GraphicsImageStorage& storage, GocatorRangeRejection* rejection)
{
    const auto width = positiveInt(msg.Width());
    if (!width.has_value())
    {
        return rejectRange(rejection, RangeReason::InvalidDimensions, 0, 1);
    }

    const std::size_t count = static_cast<std::size_t>(*width);
    const kArray1 ranges = msg.Ranges();
    if (ranges == kNULL) return rejectRange(rejection, RangeReason::MissingData, *width, 1);
    if (kArray1_ItemSize(ranges) != sizeof(kPoint16s))
        return rejectRange(rejection, RangeReason::UnsupportedFormat, *width, 1);
    if (kArray1_Length(ranges) < count)
        return rejectRange(rejection, RangeReason::TruncatedData, *width, 1);

    const auto* src = reinterpret_cast<const std::int16_t*>(kArray1_DataT(ranges, kPoint16s));
    const std::uint8_t intensityBits = request.includeRangeAuxiliaryChannels
        ? array1IntensityBits(msg.Intensities(), count)
        : 0U;

    GraphicsRangeFrameLayout layout;
    layout.width = *width;
    layout.height = 1;
    layout.channels.x = true;
    layout.channels.y = true;
    layout.channels.validMask = gocator_range::profilePointsHaveInvalid(src, count);
    layout.channels.intensity = intensityBits != 0U;
    GraphicsRangeFrameWriter writer(layout, storage);
    if (!writer.isValid())
    {
        return rejectRangeStorage(rejection, writer, *width, 1);
    }

    const kPoint3d64f resolution = msg.Resolution();
    const kPoint3d64f offset = msg.Offset();
    std::fill_n(writer.yData(), count, 0.0F);
    gocator_range::writeProfilePoints(src, count, resolution.x, resolution.z,
        offset.x, offset.z, writer.xData(), writer.zData(), writer.validMaskData());
    if (layout.channels.intensity)
    {
        writeArray1Intensity(msg.Intensities(), count, writer.intensityData());
    }

    RangeFrame frame = writer.takeFrame();
    applyCommonFrameFields(frame, *width, 1, resolution, offset, frameId(msg, stamp));
    frame.yScale = 1.0;
    frame.xyCoordinateMode = RangeFrameXYCoordinateMode::ExplicitXY;
    if (layout.channels.intensity)
    {
        frame.intensityBits = intensityBits;
        frame.intensityField.bitsPerSample = intensityBits;
    }
    if (!frame.isValid()) return rejectRange(rejection, RangeReason::InvalidLayout, *width, 1);
    return frame;
}

} // namespace

void GocatorDataSetGraphicsFrameAdapter::setFrameStorage(GraphicsImageStorage storage)
{
    std::lock_guard<std::mutex> lock(_storageMutex);
    _storage = std::move(storage);
}

std::optional<GraphicsFrame> GocatorDataSetGraphicsFrameAdapter::convertFrameWithRejection(
    const GoPxLSdk::GoDataSet& dataSet, const GraphicsFrameRequest& request,
    GocatorRangeRejection& rejection) const
{
    rejection = {};
    auto frame = convertGraphicsFrame(dataSet, request, &rejection);
    if (!frame || !frame->isValid()) return std::nullopt;
    rejection.rangeRetained = true;
    return frame;
}

std::optional<GraphicsFrame> GocatorDataSetGraphicsFrameAdapter::convertGraphicsFrame(
    const GoPxLSdk::GoDataSet& dataSet,
    const GraphicsFrameRequest& request,
    GocatorRangeRejection* rejection) const
{
    if (!hasGraphicsFrameComponent(request.components, GraphicsFrameComponent::Range))
    {
        return std::nullopt;
    }

    GraphicsImageStorage storage;
    {
        std::lock_guard<std::mutex> lock(_storageMutex);
        storage = _storage;
    }

    const StampInfo stamp = readStamp(dataSet);
    const auto count = dataSet.Count();
    if (rejection) {
        rejection->partCount = static_cast<std::uint32_t>(
            (std::min)(count, static_cast<decltype(count)>((std::numeric_limits<std::uint32_t>::max)())));
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        const GoPxLSdk::GoGdpMsg& msg = dataSet.GdpMsgAt(index);
        std::optional<RangeFrame> frame;
        auto* evidence = rejection && rejection->reason == RangeReason::None ? rejection : nullptr;

        switch (msg.Type())
        {
        case GoPxLSdk::MessageType::UNIFORM_SURFACE:
            frame = convertUniformSurface(static_cast<const GoPxLSdk::GoGdpSurfaceUniform&>(msg), stamp, request, storage, evidence);
            break;

        case GoPxLSdk::MessageType::SURFACE_POINT_CLOUD:
            frame = convertSurfacePointCloud(static_cast<const GoPxLSdk::GoGdpSurfacePointCloud&>(msg), stamp, request, storage, evidence);
            break;

        case GoPxLSdk::MessageType::UNIFORM_PROFILE:
            frame = convertUniformProfile(static_cast<const GoPxLSdk::GoGdpProfileUniform&>(msg), stamp, request, storage, evidence);
            break;

        case GoPxLSdk::MessageType::PROFILE_POINT_CLOUD:
            frame = convertProfilePointCloud(static_cast<const GoPxLSdk::GoGdpProfilePointCloud&>(msg), stamp, request, storage, evidence);
            break;

        default:
            break;
        }

        if (evidence && evidence->reason != RangeReason::None && index < evidence->partCount)
            evidence->part = static_cast<std::uint32_t>(index);
        if (frame.has_value())
        {
            return makeFrame(std::move(*frame), msg, stamp);
        }
    }

    if (rejection && rejection->reason == RangeReason::None)
        rejection->reason = RangeReason::MissingRange;
    return std::nullopt;
}
