#include "GocatorDataSetGraphicsFrameAdapter.h"

#include "GocatorRangeConversion.h"
#include "engine/GraphicsFrameBuilder.h"
#include "engine/detail/GraphicsWriteLayout.h"
#include "graphics/data/GraphicsRangeFrameWriter.h"

#include <GoPxLSdk/GoDataSet.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpImage.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpMsgDef.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpPixelFormat.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfilePointCloud.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfileUniform.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpStamp.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpSurfacePointCloud.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpSurfaceUniform.h>
#include <kApi/Data/kArray1.h>
#include <kApi/Data/kArray2.h>
#include <kApi/kApiDef.h>

#include <algorithm>
#include <cmath>
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
        || kArray2_Length(intensities, 0) != static_cast<kSize>(height)
        || kArray2_Length(intensities, 1) != static_cast<kSize>(width))
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

[[nodiscard]] GraphicsImagePixelFormat graphicsPixelFormat(
    const GoPxLSdk::GoGdpPixelFormat::FormatType format,
    const kCfa colorFilter)
{
    using GoPxLSdk::GoGdpPixelFormat;
    switch (format)
    {
    case GoGdpPixelFormat::Greyscale_8BPP:
    case GoGdpPixelFormat::Mono8:
        return GraphicsImagePixelFormat::Mono8;
    case GoGdpPixelFormat::Greyscale_16BPP:
    case GoGdpPixelFormat::Mono10:
    case GoGdpPixelFormat::Mono12:
    case GoGdpPixelFormat::Mono14:
    case GoGdpPixelFormat::Mono16:
        return GraphicsImagePixelFormat::Mono16;
    case GoGdpPixelFormat::Mono10p:
        return GraphicsImagePixelFormat::Mono10Packed;
    case GoGdpPixelFormat::Mono12p:
        return GraphicsImagePixelFormat::Mono12Packed;
    case GoGdpPixelFormat::BayerGR8:
        return GraphicsImagePixelFormat::BayerGR8;
    case GoGdpPixelFormat::BayerRG8:
        return GraphicsImagePixelFormat::BayerRG8;
    case GoGdpPixelFormat::BayerGB8:
        return GraphicsImagePixelFormat::BayerGB8;
    case GoGdpPixelFormat::BayerBG8:
        return GraphicsImagePixelFormat::BayerBG8;
    case GoGdpPixelFormat::BayerGR10:
        return GraphicsImagePixelFormat::BayerGR10;
    case GoGdpPixelFormat::BayerGR10p:
        return GraphicsImagePixelFormat::BayerGR10;
    case GoGdpPixelFormat::BayerRG10:
        return GraphicsImagePixelFormat::BayerRG10;
    case GoGdpPixelFormat::BayerRG10p:
        return GraphicsImagePixelFormat::BayerRG10;
    case GoGdpPixelFormat::BayerGB10:
        return GraphicsImagePixelFormat::BayerGB10;
    case GoGdpPixelFormat::BayerGB10p:
        return GraphicsImagePixelFormat::BayerGB10;
    case GoGdpPixelFormat::BayerBG10:
        return GraphicsImagePixelFormat::BayerBG10;
    case GoGdpPixelFormat::BayerBG10p:
        return GraphicsImagePixelFormat::BayerBG10;
    case GoGdpPixelFormat::BayerGR12:
        return GraphicsImagePixelFormat::BayerGR12;
    case GoGdpPixelFormat::BayerGR12p:
        return GraphicsImagePixelFormat::BayerGR12;
    case GoGdpPixelFormat::BayerRG12:
        return GraphicsImagePixelFormat::BayerRG12;
    case GoGdpPixelFormat::BayerRG12p:
        return GraphicsImagePixelFormat::BayerRG12;
    case GoGdpPixelFormat::BayerGB12:
        return GraphicsImagePixelFormat::BayerGB12;
    case GoGdpPixelFormat::BayerGB12p:
        return GraphicsImagePixelFormat::BayerGB12;
    case GoGdpPixelFormat::BayerBG12:
        return GraphicsImagePixelFormat::BayerBG12;
    case GoGdpPixelFormat::BayerBG12p:
        return GraphicsImagePixelFormat::BayerBG12;
    case GoGdpPixelFormat::BayerGR16:
        return GraphicsImagePixelFormat::BayerGR16;
    case GoGdpPixelFormat::BayerRG16:
        return GraphicsImagePixelFormat::BayerRG16;
    case GoGdpPixelFormat::BayerGB16:
        return GraphicsImagePixelFormat::BayerGB16;
    case GoGdpPixelFormat::BayerBG16:
        return GraphicsImagePixelFormat::BayerBG16;
    case GoGdpPixelFormat::Rgb8:
        return GraphicsImagePixelFormat::RGB24;
    case GoGdpPixelFormat::Bgr8:
        return GraphicsImagePixelFormat::BGR24;
    case GoGdpPixelFormat::Rgba8:
        return GraphicsImagePixelFormat::RGBA32;
    case GoGdpPixelFormat::Bgra8:
        return GraphicsImagePixelFormat::BGRA32;
    case GoGdpPixelFormat::YCbCr422_8:
        return GraphicsImagePixelFormat::YCbCr422_8;
    case GoGdpPixelFormat::YCbCr422_8_CbYCrY:
        return GraphicsImagePixelFormat::YCbCr422_8_CbYCrY;
    case GoGdpPixelFormat::CFA_8BPP:
        switch (colorFilter)
        {
        case 1: return GraphicsImagePixelFormat::BayerBG8;
        case 2: return GraphicsImagePixelFormat::BayerGB8;
        case 3: return GraphicsImagePixelFormat::BayerRG8;
        case 4: return GraphicsImagePixelFormat::BayerGR8;
        default: return GraphicsImagePixelFormat::Unknown;
        }
    default:
        return GraphicsImagePixelFormat::Unknown;
    }
}

[[nodiscard]] std::uint8_t declaredMonoSourceBits(
    const GoPxLSdk::GoGdpPixelFormat::FormatType format)
{
    using GoPxLSdk::GoGdpPixelFormat;
    switch (format)
    {
    case GoGdpPixelFormat::Mono10: return 10U;
    case GoGdpPixelFormat::Mono12: return 12U;
    case GoGdpPixelFormat::Mono14: return 14U;
    default: return 0U;
    }
}

[[nodiscard]] bool wordAlignedSourceFormat(
    const GoPxLSdk::GoGdpPixelFormat::FormatType format)
{
    using GoPxLSdk::GoGdpPixelFormat;
    switch (format)
    {
    case GoGdpPixelFormat::Mono10:
    case GoGdpPixelFormat::Mono12:
    case GoGdpPixelFormat::Mono14:
    case GoGdpPixelFormat::BayerGR10:
    case GoGdpPixelFormat::BayerRG10:
    case GoGdpPixelFormat::BayerGB10:
    case GoGdpPixelFormat::BayerBG10:
    case GoGdpPixelFormat::BayerGR12:
    case GoGdpPixelFormat::BayerRG12:
    case GoGdpPixelFormat::BayerGB12:
    case GoGdpPixelFormat::BayerBG12:
        return true;
    default:
        return false;
    }
}

struct ImageLayout
{
    int width = 0;
    int height = 0;
    int stride = 0;
    std::size_t size = 0;
    std::size_t minimumRowBytes = 0;
    GraphicsImagePixelFormat pixelFormat = GraphicsImagePixelFormat::Unknown;
    int bitsPerPixel = 0;
    int sourceFormat = 0;
    std::uint8_t declaredMonoSourceBits = 0;
    GraphicsImageBitAlignment bitAlignment = GraphicsImageBitAlignment::Packed;
    std::uint64_t frameSequence = 0;
};

struct ImagePlanResult
{
    std::optional<ImageLayout> layout;
    GraphicsImageFailure failure = GraphicsImageFailure::None;
};

[[nodiscard]] ImagePlanResult planImage(
    const GoPxLSdk::GoGdpImage& image, const StampInfo& stamp)
{
    const auto reject = [](const GraphicsImageFailure reason) {
        return ImagePlanResult{std::nullopt, reason};
    };
    const auto width = positiveInt(image.Width());
    const auto height = positiveInt(image.Height());
    const kArray2 pixels = image.Pixels();
    if (!width || !height) return reject(GraphicsImageFailure::InvalidDimensions);
    if (pixels == kNULL) return reject(GraphicsImageFailure::MissingBytes);

    const auto pixelFormat = graphicsPixelFormat(image.PixelFormat(), image.ColorFilter());
    if (pixelFormat == GraphicsImagePixelFormat::Unknown)
        return reject(GraphicsImageFailure::UnsupportedFormat);
    const double pixelBytes = GoPxLSdk::GoGdpPixelFormat::PixelBytes(image.PixelFormat());
    const double declaredPixelBytes = std::ceil(pixelBytes);
    if (!std::isfinite(pixelBytes) || pixelBytes <= 0.0
        || declaredPixelBytes > static_cast<double>((std::numeric_limits<k32u>::max)())
        || image.PixelSize() != static_cast<k32u>(declaredPixelBytes))
        return reject(GraphicsImageFailure::InvalidStride);
    if (kArray2_Length(pixels, 0) < static_cast<kSize>(*height))
        return reject(GraphicsImageFailure::TruncatedBytes);

    const auto itemSize = kArray2_ItemSize(pixels);
    const auto columns = kArray2_Length(pixels, 1);
    if (itemSize == 0U || columns > (std::numeric_limits<std::size_t>::max)() / itemSize)
        return reject(GraphicsImageFailure::ExtentOverflow);
    const std::size_t rowBytes = static_cast<std::size_t>(columns) * itemSize;
    const double sdkRowBytesExact = static_cast<double>(*width) * pixelBytes;
    if (!std::isfinite(sdkRowBytesExact)
        || sdkRowBytesExact > static_cast<double>((std::numeric_limits<std::size_t>::max)()))
        return reject(GraphicsImageFailure::ExtentOverflow);
    std::size_t minimumRowBytes = GraphicsImageValidation::minimumRowBytes(pixelFormat, *width);
    minimumRowBytes = (std::max)(minimumRowBytes,
        static_cast<std::size_t>(std::ceil(sdkRowBytesExact)));
    if (wordAlignedSourceFormat(image.PixelFormat()))
    {
        const std::size_t wordRowBytes = static_cast<std::size_t>(*width) * sizeof(std::uint16_t);
        minimumRowBytes = (std::max)(minimumRowBytes, wordRowBytes);
    }
    if (minimumRowBytes == 0U) return reject(GraphicsImageFailure::ExtentOverflow);
    if (rowBytes < minimumRowBytes) return reject(GraphicsImageFailure::StrideBelowMinimum);
    if (rowBytes > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return reject(GraphicsImageFailure::InvalidStride);
    if (static_cast<std::size_t>(*height) > (std::numeric_limits<std::size_t>::max)() / rowBytes)
        return reject(GraphicsImageFailure::ExtentOverflow);
    const std::size_t readableBytes = rowBytes * static_cast<std::size_t>(*height);
    if (kArray2_DataSize(pixels) < readableBytes)
        return reject(GraphicsImageFailure::TruncatedBytes);
    const auto layoutFailure = GraphicsImageValidation::layoutFailure(
        pixelFormat, *width, *height, static_cast<int>(rowBytes), readableBytes);
    if (layoutFailure != GraphicsImageFailure::None) return reject(layoutFailure);

    ImageLayout layout;
    layout.width = *width;
    layout.height = *height;
    layout.stride = static_cast<int>(rowBytes);
    layout.size = readableBytes;
    layout.minimumRowBytes = minimumRowBytes;
    layout.pixelFormat = pixelFormat;
    layout.bitsPerPixel = GraphicsImageValidation::packedBitsPerPixel(pixelFormat);
    layout.sourceFormat = static_cast<int>(image.PixelFormat());
    layout.declaredMonoSourceBits = declaredMonoSourceBits(image.PixelFormat());
    layout.bitAlignment = wordAlignedSourceFormat(image.PixelFormat())
        ? GraphicsImageBitAlignment::LeastSignificant : GraphicsImageBitAlignment::Packed;
    layout.frameSequence = stamp.valid ? stamp.frameIndex : image.DataSetId();
    return {std::move(layout), GraphicsImageFailure::None};
}

[[nodiscard]] GraphicsImageWriterSource imageWriterSource(const ImageLayout& layout, const kArray2 pixels)
{
    GraphicsImageWriterSource source;
    source.data = static_cast<const std::uint8_t*>(kArray2_Data(pixels));
    source.size = layout.size;
    source.width = layout.width;
    source.height = layout.height;
    source.stride = layout.stride;
    source.pixelFormat = layout.pixelFormat;
    source.bitsPerPixel = layout.bitsPerPixel;
    source.sourceFormat = layout.sourceFormat;
    source.declaredMonoSourceBits = layout.declaredMonoSourceBits;
    source.bitAlignment = layout.bitAlignment;
    source.frameSequence = layout.frameSequence;
    source.operation = GraphicsImageWriterOperation::Copy;
    return source;
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
    case GraphicsRangeFrameWriterFailure::None:
        return rejectRange(rejection, RangeReason::InvalidLayout, width, height);
    }
    return std::nullopt;
}

/** Validated range conversion: metadata, planned writer layout, and borrowed SDK sample pointers. */
struct RangeConversion
{
    enum class Codec { UniformSurface, SurfacePointCloud, UniformProfile, ProfilePointCloud };

    Codec codec = Codec::UniformSurface;
    GraphicsRangeFrameLayout layout;
    RangeFrame fields;
    int width = 0;
    int height = 0;
    const std::int16_t* samples = nullptr;
    std::size_t count = 0;
    kArray1 intensities1 = kNULL;
    kArray2 intensities2 = kNULL;
    kPoint3d64f resolution{};
    kPoint3d64f offset{};
    GraphicsRangeWriteLayout writePlan;
};

void fillRangeSamples(const RangeConversion& conversion, GraphicsRangeFrameWriter& writer)
{
    switch (conversion.codec) {
    case RangeConversion::Codec::UniformSurface:
        gocator_range::writeScalarZ(conversion.samples, conversion.count,
            conversion.resolution.z, conversion.offset.z,
            writer.zData(), writer.validMaskData());
        if (conversion.layout.channels.intensity) {
            writeArray2Intensity(conversion.intensities2,
                conversion.width, conversion.height, writer.intensityData());
        }
        break;
    case RangeConversion::Codec::SurfacePointCloud:
        gocator_range::writeSurfacePoints(conversion.samples, conversion.count,
            conversion.resolution.x, conversion.resolution.y, conversion.resolution.z,
            conversion.offset.x, conversion.offset.y, conversion.offset.z,
            writer.xData(), writer.yData(), writer.zData(), writer.validMaskData());
        if (conversion.layout.channels.intensity) {
            writeArray2Intensity(conversion.intensities2,
                conversion.width, conversion.height, writer.intensityData());
        }
        break;
    case RangeConversion::Codec::UniformProfile:
        gocator_range::writeScalarZ(conversion.samples, conversion.count,
            conversion.resolution.z, conversion.offset.z,
            writer.zData(), writer.validMaskData());
        if (conversion.layout.channels.intensity) {
            writeArray1Intensity(conversion.intensities1,
                conversion.count, writer.intensityData());
        }
        break;
    case RangeConversion::Codec::ProfilePointCloud:
        std::fill_n(writer.yData(), conversion.count, 0.0F);
        gocator_range::writeProfilePoints(conversion.samples, conversion.count,
            conversion.resolution.x, conversion.resolution.z,
            conversion.offset.x, conversion.offset.z,
            writer.xData(), writer.zData(), writer.validMaskData());
        if (conversion.layout.channels.intensity) {
            writeArray1Intensity(conversion.intensities1,
                conversion.count, writer.intensityData());
        }
        break;
    }
}

[[nodiscard]] std::optional<RangeFrame> writeRange(
    const RangeConversion& conversion, GraphicsFrameBuilder& builder, GocatorRangeRejection* rejection)
{
    GraphicsRangeFrameWriter writer(builder, conversion.layout);
    if (!writer.isValid())
    {
        return rejectRangeStorage(rejection, writer, conversion.width, conversion.height);
    }
    fillRangeSamples(conversion, writer);
    auto samples = writer.takeFrame();
    RangeFrame frame = conversion.fields;
    frame.zValues = std::move(samples.zValues);
    frame.xValues = std::move(samples.xValues);
    frame.yValues = std::move(samples.yValues);
    frame.validMask = std::move(samples.validMask);
    frame.intensityValidMask = std::move(samples.intensityValidMask);
    frame.confidenceValidMask = std::move(samples.confidenceValidMask);
    frame.intensity = std::move(samples.intensity);
    frame.confidence = std::move(samples.confidence);
    frame.rangeRaw = std::move(samples.rangeRaw);
    frame.intensityRaw = std::move(samples.intensityRaw);
    frame.confidenceRaw = std::move(samples.confidenceRaw);
    frame.rgb = std::move(samples.rgb);
    if (!frame.isValid())
    {
        return rejectRange(rejection, RangeReason::InvalidLayout, conversion.width, conversion.height);
    }
    return frame;
}

[[nodiscard]] std::optional<RangeConversion> planUniformSurface(
    const GoPxLSdk::GoGdpSurfaceUniform& msg,
    const StampInfo& stamp,
    const GraphicsFrameRequest& request,
    GocatorRangeRejection* rejection)
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
    if (kArray2_Length(ranges, 0) != static_cast<kSize>(*height)
        || kArray2_Length(ranges, 1) != static_cast<kSize>(*width))
        return rejectRange(rejection, RangeReason::InvalidLayout, *width, *height);

    const auto* src = kArray2_DataT(ranges, k16s);
    const std::uint8_t intensityBits = request.includeRangeAuxiliaryChannels
        ? array2IntensityBits(msg.Intensities(), *width, *height)
        : 0U;

    GraphicsRangeFrameLayout layout;
    layout.width = *width;
    layout.height = *height;
    layout.channels.validMask = gocator_range::scalarHasInvalid(src, *count);
    layout.channels.intensity = intensityBits != 0U;
    const auto writePlan = planGraphicsRangeWrite(layout);
    if (!writePlan)
    {
        return rejectRange(rejection,
            writePlan.error() == GraphicsFrameError::Capacity
                ? RangeReason::SizeLimitExceeded : RangeReason::InvalidDimensions,
            *width, *height);
    }

    RangeConversion conversion;
    conversion.codec = RangeConversion::Codec::UniformSurface;
    conversion.layout = layout;
    conversion.width = *width;
    conversion.height = *height;
    conversion.samples = src;
    conversion.count = *count;
    conversion.intensities2 = msg.Intensities();
    conversion.resolution = msg.Resolution();
    conversion.offset = msg.Offset();
    conversion.writePlan = writePlan.value();
    applyCommonFrameFields(conversion.fields, *width, *height,
        conversion.resolution, conversion.offset, frameId(msg, stamp));
    if (layout.channels.intensity)
    {
        conversion.fields.intensityBits = intensityBits;
        conversion.fields.intensityField.bitsPerSample = intensityBits;
    }
    return conversion;
}
[[nodiscard]] std::optional<RangeConversion> planSurfacePointCloud(
    const GoPxLSdk::GoGdpSurfacePointCloud& msg,
    const StampInfo& stamp,
    const GraphicsFrameRequest& request,
    GocatorRangeRejection* rejection)
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
    if (kArray2_Length(ranges, 0) != static_cast<kSize>(*height)
        || kArray2_Length(ranges, 1) != static_cast<kSize>(*width))
        return rejectRange(rejection, RangeReason::InvalidLayout, *width, *height);

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
    const auto writePlan = planGraphicsRangeWrite(layout);
    if (!writePlan)
    {
        return rejectRange(rejection,
            writePlan.error() == GraphicsFrameError::Capacity
                ? RangeReason::SizeLimitExceeded : RangeReason::InvalidDimensions,
            *width, *height);
    }

    RangeConversion conversion;
    conversion.codec = RangeConversion::Codec::SurfacePointCloud;
    conversion.layout = layout;
    conversion.width = *width;
    conversion.height = *height;
    conversion.samples = src;
    conversion.count = *count;
    conversion.intensities2 = msg.Intensities();
    conversion.resolution = msg.Resolution();
    conversion.offset = msg.Offset();
    conversion.writePlan = writePlan.value();
    applyCommonFrameFields(conversion.fields, *width, *height,
        conversion.resolution, conversion.offset, frameId(msg, stamp));
    conversion.fields.xyCoordinateMode = RangeFrameXYCoordinateMode::ExplicitXY;
    if (layout.channels.intensity)
    {
        conversion.fields.intensityBits = intensityBits;
        conversion.fields.intensityField.bitsPerSample = intensityBits;
    }
    return conversion;
}
[[nodiscard]] std::optional<RangeConversion> planUniformProfile(
    const GoPxLSdk::GoGdpProfileUniform& msg,
    const StampInfo& stamp,
    const GraphicsFrameRequest& request,
    GocatorRangeRejection* rejection)
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
    const auto writePlan = planGraphicsRangeWrite(layout);
    if (!writePlan)
    {
        return rejectRange(rejection,
            writePlan.error() == GraphicsFrameError::Capacity
                ? RangeReason::SizeLimitExceeded : RangeReason::InvalidDimensions,
            *width, 1);
    }

    RangeConversion conversion;
    conversion.codec = RangeConversion::Codec::UniformProfile;
    conversion.layout = layout;
    conversion.width = *width;
    conversion.height = 1;
    conversion.samples = src;
    conversion.count = count;
    conversion.intensities1 = msg.Intensities();
    conversion.resolution = msg.Resolution();
    conversion.offset = msg.Offset();
    conversion.writePlan = writePlan.value();
    applyCommonFrameFields(conversion.fields, *width, 1,
        conversion.resolution, conversion.offset, frameId(msg, stamp));
    conversion.fields.yScale = 1.0;
    if (layout.channels.intensity)
    {
        conversion.fields.intensityBits = intensityBits;
        conversion.fields.intensityField.bitsPerSample = intensityBits;
    }
    return conversion;
}
[[nodiscard]] std::optional<RangeConversion> planProfilePointCloud(
    const GoPxLSdk::GoGdpProfilePointCloud& msg,
    const StampInfo& stamp,
    const GraphicsFrameRequest& request,
    GocatorRangeRejection* rejection)
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
    const auto writePlan = planGraphicsRangeWrite(layout);
    if (!writePlan)
    {
        return rejectRange(rejection,
            writePlan.error() == GraphicsFrameError::Capacity
                ? RangeReason::SizeLimitExceeded : RangeReason::InvalidDimensions,
            *width, 1);
    }

    RangeConversion conversion;
    conversion.codec = RangeConversion::Codec::ProfilePointCloud;
    conversion.layout = layout;
    conversion.width = *width;
    conversion.height = 1;
    conversion.samples = src;
    conversion.count = count;
    conversion.intensities1 = msg.Intensities();
    conversion.resolution = msg.Resolution();
    conversion.offset = msg.Offset();
    conversion.writePlan = writePlan.value();
    applyCommonFrameFields(conversion.fields, *width, 1,
        conversion.resolution, conversion.offset, frameId(msg, stamp));
    conversion.fields.yScale = 1.0;
    conversion.fields.xyCoordinateMode = RangeFrameXYCoordinateMode::ExplicitXY;
    if (layout.channels.intensity)
    {
        conversion.fields.intensityBits = intensityBits;
        conversion.fields.intensityField.bitsPerSample = intensityBits;
    }
    return conversion;
}

} // namespace

std::optional<GraphicsFrame> GocatorDataSetGraphicsFrameAdapter::convertFrame(
    const GoPxLSdk::GoDataSet& dataSet,
    const GraphicsFrameRequest& request,
    GraphicsFrameContextSource& contextSource) const
{
    auto result = convertGraphicsFrame(dataSet, request, contextSource);
    if (!result.has_value() || !result->isValid())
    {
        return std::nullopt;
    }
    return result;
}

std::optional<GraphicsFrame> GocatorDataSetGraphicsFrameAdapter::convertFrameWithRejection(
    const GoPxLSdk::GoDataSet& dataSet,
    const GraphicsFrameRequest& request,
    GraphicsFrameContextSource& contextSource,
    GocatorRangeRejection& rejection,
    GocatorImageRejection* imageRejection,
    std::optional<std::uint64_t> frameIndex,
    GraphicsFrameError* storageError,
    const std::atomic<bool>* cancelled) const
{
    rejection = {};
    if (imageRejection) *imageRejection = {};
    if (storageError) *storageError = GraphicsFrameError::None;
    auto frame = convertGraphicsFrame(dataSet, request, contextSource, &rejection,
        imageRejection, frameIndex, storageError, cancelled);
    if (!frame || !frame->isValid()) return std::nullopt;
    rejection.frameRetained = true;
    rejection.imageRetained = frame->hasImage();
    rejection.rangeRetained = frame->hasRangeFrame();
    if (imageRejection) imageRejection->imageRetained = frame->hasImage();
    return frame;
}

std::optional<GraphicsFrame> GocatorDataSetGraphicsFrameAdapter::convertGraphicsFrame(
    const GoPxLSdk::GoDataSet& dataSet,
    const GraphicsFrameRequest& request,
    GraphicsFrameContextSource& contextSource,
    GocatorRangeRejection* rejection,
    GocatorImageRejection* imageRejection,
    std::optional<std::uint64_t> frameIndex,
    GraphicsFrameError* storageError,
    const std::atomic<bool>* cancelled) const
{
    const bool wantsRange = hasGraphicsFrameComponent(request.components, GraphicsFrameComponent::Range);
    const bool wantsImage = hasGraphicsFrameComponent(request.components, GraphicsFrameComponent::Image);
    if (!wantsRange && !wantsImage) return std::nullopt;

    const StampInfo stamp = readStamp(dataSet);
    const auto count = dataSet.Count();
    const auto partCount = static_cast<std::uint32_t>(
        (std::min)(count, static_cast<decltype(count)>((std::numeric_limits<std::uint32_t>::max)())));
    if (rejection) rejection->partCount = partCount;

    const auto planRange = [&](const GoPxLSdk::GoGdpMsg& message,
                               const std::size_t index,
                               GocatorRangeRejection* evidence) -> std::optional<RangeConversion> {
        std::optional<RangeConversion> conversion;
        switch (message.Type()) {
        case GoPxLSdk::MessageType::UNIFORM_SURFACE:
            conversion = planUniformSurface(static_cast<const GoPxLSdk::GoGdpSurfaceUniform&>(message),
                stamp, request, evidence);
            break;
        case GoPxLSdk::MessageType::SURFACE_POINT_CLOUD:
            conversion = planSurfacePointCloud(static_cast<const GoPxLSdk::GoGdpSurfacePointCloud&>(message),
                stamp, request, evidence);
            break;
        case GoPxLSdk::MessageType::UNIFORM_PROFILE:
            conversion = planUniformProfile(static_cast<const GoPxLSdk::GoGdpProfileUniform&>(message),
                stamp, request, evidence);
            break;
        case GoPxLSdk::MessageType::PROFILE_POINT_CLOUD:
            conversion = planProfilePointCloud(static_cast<const GoPxLSdk::GoGdpProfilePointCloud&>(message),
                stamp, request, evidence);
            break;
        default:
            break;
        }
        if (evidence && evidence->reason != RangeReason::None && index < evidence->partCount)
            evidence->part = static_cast<std::uint32_t>(index);
        return conversion;
    };

    bool sawRange = false;
    std::optional<RangeConversion> rangePlan;
    const GoPxLSdk::GoGdpMsg* rangeMessage = nullptr;
    std::size_t rangeIndex = 0;
    if (wantsRange) {
        for (std::size_t index = 0; index < count; ++index) {
            const auto& message = dataSet.GdpMsgAt(index);
            switch (message.Type()) {
            case GoPxLSdk::MessageType::UNIFORM_SURFACE:
            case GoPxLSdk::MessageType::SURFACE_POINT_CLOUD:
            case GoPxLSdk::MessageType::UNIFORM_PROFILE:
            case GoPxLSdk::MessageType::PROFILE_POINT_CLOUD:
                break;
            default:
                continue;
            }
            sawRange = true;
            GocatorRangeRejection candidate;
            candidate.partCount = partCount;
            auto plan = planRange(message, index, &candidate);
            if (rejection && rejection->reason == RangeReason::None) *rejection = candidate;
            if (!plan) continue;
            rangePlan = std::move(plan);
            rangeMessage = &message;
            rangeIndex = index;
            break;
        }
    }
    // A range that was present but could not be converted drops the frame; the image is never
    // published in its place.
    if (sawRange && !rangePlan) return std::nullopt;

    if (rejection && wantsRange && !sawRange && rejection->reason == RangeReason::None)
        rejection->reason = RangeReason::MissingRange;
    if (!rangePlan && !wantsImage) return std::nullopt;

    const auto reportImageFailure = [&](const GoPxLSdk::GoGdpImage& image,
                                        const GraphicsImageFailure reason,
                                        const ImageLayout* layout) {
        if (!imageRejection || imageRejection->reason != GraphicsImageFailure::None) return;
        imageRejection->reason = reason;
        imageRejection->width = image.Width();
        imageRejection->height = image.Height();
        imageRejection->frameSequence = stamp.valid ? stamp.frameIndex : image.DataSetId();
        imageRejection->pixelFormat = static_cast<std::uint64_t>(image.PixelFormat());
        const auto pixels = image.Pixels();
        if (pixels != kNULL) {
            imageRejection->readableBytes = kArray2_DataSize(pixels);
            imageRejection->reportedStride = static_cast<std::uint64_t>(
                kArray2_Length(pixels, 1)) * kArray2_ItemSize(pixels);
            imageRejection->effectiveStride = imageRejection->reportedStride;
        }
        const auto width = positiveInt(image.Width());
        const auto height = positiveInt(image.Height());
        const auto pixelFormat = graphicsPixelFormat(image.PixelFormat(), image.ColorFilter());
        if (width && height && pixelFormat != GraphicsImagePixelFormat::Unknown) {
            auto minimumRowBytes = GraphicsImageValidation::minimumRowBytes(pixelFormat, *width);
            if (wordAlignedSourceFormat(image.PixelFormat()))
                minimumRowBytes = (std::max)(minimumRowBytes,
                    static_cast<std::size_t>(*width) * sizeof(std::uint16_t));
            imageRejection->minimumRowBytes = minimumRowBytes;
            const auto requiredStride = (std::max)(imageRejection->reportedStride,
                static_cast<std::uint64_t>(minimumRowBytes));
            if (requiredStride <= (std::numeric_limits<std::size_t>::max)()
                / static_cast<std::size_t>(*height))
                imageRejection->requiredBytes = requiredStride * static_cast<std::size_t>(*height);
        }
        if (layout) {
            imageRejection->effectiveStride = static_cast<std::uint64_t>(layout->stride);
            imageRejection->minimumRowBytes = layout->minimumRowBytes;
            imageRejection->requiredBytes = layout->size;
        }
    };

    // The first valid image. With a range it is an optional companion: an invalid image leaves a
    // range-only frame and its rejection stays observable.
    std::optional<ImageLayout> imageLayout;
    const GoPxLSdk::GoGdpImage* imageMessage = nullptr;
    GraphicsImageWriterSource imageSource;
    GraphicsImageWriteLayout imageWriteLayout;
    if (wantsImage) {
        for (std::size_t index = 0; index < count; ++index) {
            const auto& message = dataSet.GdpMsgAt(index);
            if (message.Type() != GoPxLSdk::MessageType::IMAGE) continue;
            const auto& image = static_cast<const GoPxLSdk::GoGdpImage&>(message);
            auto plan = planImage(image, stamp);
            if (!plan.layout) {
                reportImageFailure(image, plan.failure, nullptr);
                continue;
            }
            auto candidateSource = imageWriterSource(*plan.layout, image.Pixels());
            GraphicsImageFailure writerFailure = GraphicsImageFailure::None;
            const auto writePlan = planGraphicsImageWrite(candidateSource, &writerFailure);
            if (!writePlan) {
                reportImageFailure(image, writerFailure, &*plan.layout);
                continue;
            }
            imageLayout = std::move(plan.layout);
            imageMessage = &image;
            imageSource = candidateSource;
            imageWriteLayout = writePlan.value();
            break;
        }
    }
    if (!rangePlan && !imageLayout) return std::nullopt;

    // Plan the exact destination footprint in real reserve order: image first, then range.
    std::size_t sampleBytes = 0U;
    if (imageLayout) {
        if (!appendAligned(sampleBytes, imageWriteLayout.bytes, imageWriteLayout.alignment)) {
            if (storageError) *storageError = GraphicsFrameError::Capacity;
            reportImageFailure(*imageMessage, GraphicsImageFailure::ExtentOverflow, &*imageLayout);
            return std::nullopt;
        }
    }
    if (rangePlan) {
        if (!appendAligned(sampleBytes, rangePlan->writePlan.totalBytes, rangePlan->writePlan.alignment)) {
            if (storageError) *storageError = GraphicsFrameError::Capacity;
            if (rejection) {
                rejection->reason = RangeReason::SizeLimitExceeded;
                rejection->width = static_cast<std::uint32_t>(rangePlan->width);
                rejection->height = static_cast<std::uint32_t>(rangePlan->height);
                rejection->part = static_cast<std::uint32_t>(rangeIndex);
            }
            return std::nullopt;
        }
    }

    if (cancelled && cancelled->load(std::memory_order_acquire)) {
        if (storageError) *storageError = GraphicsFrameError::Disconnected;
        return std::nullopt;
    }

    auto contextResult = contextSource.beginFrame(sampleBytes);
    if (!contextResult) {
        const GraphicsFrameError err = contextResult.error();
        if (storageError) *storageError = err;
        if (rangePlan) {
            if (rejection) {
                rejection->reason = err == GraphicsFrameError::Capacity
                    ? RangeReason::SizeLimitExceeded
                    : RangeReason::StorageRefused;
                rejection->width = static_cast<std::uint32_t>(rangePlan->width);
                rejection->height = static_cast<std::uint32_t>(rangePlan->height);
                rejection->requestedStorageBytes = sampleBytes;
                rejection->part = static_cast<std::uint32_t>(rangeIndex);
            }
        } else if (imageLayout) {
            reportImageFailure(*imageMessage,
                err == GraphicsFrameError::Capacity
                    ? GraphicsImageFailure::ExtentOverflow
                    : GraphicsImageFailure::StorageRefused,
                &*imageLayout);
        }
        return std::nullopt;
    }

    if (cancelled && cancelled->load(std::memory_order_acquire)) {
        if (storageError) *storageError = GraphicsFrameError::Disconnected;
        return std::nullopt;
    }

    GraphicsFrameBuilder builder(contextResult.value());
    if (imageLayout) {
        GraphicsImageFailure failure = GraphicsImageFailure::None;
        auto converted = writeGraphicsImageWithFailure(builder, imageSource, &failure);
        if (!converted.isValid()) {
            if (storageError && failure == GraphicsImageFailure::StorageRefused) {
                *storageError = GraphicsFrameError::Allocation;
            }
            reportImageFailure(*imageMessage, failure, &*imageLayout);
            return std::nullopt;
        }
        builder.addImage("image", std::move(converted), "Image", "primary");
        builder.content().images.back().descriptor.registration = GraphicsImageRegistration::Unregistered;
    }
    if (rangePlan) {
        GocatorRangeRejection evidence;
        evidence.partCount = partCount;
        auto range = writeRange(*rangePlan, builder, &evidence);
        if (!range) {
            if (storageError && evidence.reason == RangeReason::StorageRefused) {
                *storageError = GraphicsFrameError::Allocation;
            }
            if (rejection) {
                *rejection = evidence;
                rejection->part = static_cast<std::uint32_t>(rangeIndex);
            }
            return std::nullopt;
        }
        builder.addRange("range", std::move(*range), "Range", "primary");
    }

    GraphicsFrameMetadata metadata;
    metadata.sourceName = "LMI Gocator";
    const GoPxLSdk::GoGdpMsg& primaryMsg = rangePlan ? *rangeMessage : *imageMessage;
    metadata.frameId = frameId(primaryMsg, stamp);
    metadata.frameIndex = frameIndex.value_or(stamp.valid ? stamp.frameIndex : primaryMsg.DataSetId());
    builder.setMetadata(std::move(metadata));

    if (cancelled && cancelled->load(std::memory_order_acquire)) {
        if (storageError) *storageError = GraphicsFrameError::Disconnected;
        return std::nullopt;
    }

    auto published = std::move(builder).publish();
    if (!published) {
        if (storageError) *storageError = published.error();
        if (rangePlan && rejection) {
            rejection->reason = RangeReason::StorageRefused;
            rejection->part = static_cast<std::uint32_t>(rangeIndex);
            rejection->requestedStorageBytes = sampleBytes;
        } else if (imageLayout) {
            reportImageFailure(*imageMessage, GraphicsImageFailure::StorageRefused, &*imageLayout);
        }
        return std::nullopt;
    }
    return std::move(published).value();
}
