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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

/**
 * Borrowed, bounds-checked view of one SDK array. `bytes` never exceeds what the SDK buffer holds,
 * so a reader that stays within `bytes` can never read past the buffer end.
 */
struct SourceBytes
{
    const std::uint8_t* data = nullptr;
    std::size_t bytes = 0;
    std::size_t rowBytes = 0;
    std::size_t rows = 0;
    std::size_t columns = 0;
    std::size_t itemSize = 0;
    std::size_t dataSize = 0;
    GraphicsImageFailure failure = GraphicsImageFailure::MissingBytes;

    [[nodiscard]] bool valid() const noexcept { return data != nullptr && bytes != 0U; }

    /** Whole rows that are fully backed by readable bytes. */
    [[nodiscard]] std::size_t backedRows() const noexcept
    {
        return rowBytes == 0U ? 0U : (std::min)(rows, bytes / rowBytes);
    }
};

[[nodiscard]] SourceBytes describeSource(const void* data, const std::size_t rows,
                                         const std::size_t columns, const std::size_t itemSize,
                                         const std::size_t dataSize) noexcept
{
    SourceBytes source;
    source.itemSize = itemSize;
    source.rows = rows;
    source.columns = columns;
    source.dataSize = dataSize;
    if (data == nullptr || itemSize == 0U || rows == 0U || columns == 0U || dataSize == 0U)
    {
        return source;
    }
    if (columns > (std::numeric_limits<std::size_t>::max)() / itemSize)
    {
        source.failure = GraphicsImageFailure::ExtentOverflow;
        return source;
    }
    const std::size_t rowBytes = columns * itemSize;
    if (rows > (std::numeric_limits<std::size_t>::max)() / rowBytes)
    {
        source.failure = GraphicsImageFailure::ExtentOverflow;
        return source;
    }
    source.rowBytes = rowBytes;
    source.bytes = (std::min)(dataSize, rowBytes * rows);
    source.data = static_cast<const std::uint8_t*>(data);
    return source;
}

[[nodiscard]] SourceBytes sourceOf2D(const kArray2 array) noexcept
{
    if (array == kNULL) return {};
    return describeSource(kArray2_Data(array), kArray2_Length(array, 0), kArray2_Length(array, 1),
        kArray2_ItemSize(array), kArray2_DataSize(array));
}

[[nodiscard]] SourceBytes sourceOf1D(const kArray1 array) noexcept
{
    if (array == kNULL) return {};
    return describeSource(kArray1_Data(array), 1U, kArray1_Length(array),
        kArray1_ItemSize(array), kArray1_DataSize(array));
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
    case GoGdpPixelFormat::Mono12Packed:
        return GraphicsImagePixelFormat::Mono12Packed;
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
    case GoGdpPixelFormat::BayerGR12Packed:
        return GraphicsImagePixelFormat::BayerGR12;
    case GoGdpPixelFormat::BayerRG12Packed:
        return GraphicsImagePixelFormat::BayerRG12;
    case GoGdpPixelFormat::BayerGB12Packed:
        return GraphicsImagePixelFormat::BayerGB12;
    case GoGdpPixelFormat::BayerBG12Packed:
        return GraphicsImagePixelFormat::BayerBG12;
    case GoGdpPixelFormat::BayerGR16:
        return GraphicsImagePixelFormat::BayerGR16;
    case GoGdpPixelFormat::BayerRG16:
        return GraphicsImagePixelFormat::BayerRG16;
    case GoGdpPixelFormat::BayerGB16:
        return GraphicsImagePixelFormat::BayerGB16;
    case GoGdpPixelFormat::BayerBG16:
        return GraphicsImagePixelFormat::BayerBG16;
    case GoGdpPixelFormat::Rgb16:
        return GraphicsImagePixelFormat::RGB48;
    case GoGdpPixelFormat::Bgr16:
        return GraphicsImagePixelFormat::BGR48;
    case GoGdpPixelFormat::Rgba16:
        return GraphicsImagePixelFormat::RGBA64;
    case GoGdpPixelFormat::Bgra16:
        return GraphicsImagePixelFormat::BGRA64;
    case GoGdpPixelFormat::BGR_8BPC:
        return GraphicsImagePixelFormat::BGR24;
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

/** Most fitted parts of one kind (image-like or range) published from one dataset. */
constexpr std::size_t kMaxParts = 8;

/** Fitted image destination plus the borrowed SDK source it is copied from. */
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
    const std::uint8_t* data = nullptr;
    std::size_t dataBytes = 0;
    std::size_t dataSize = 0;
    std::size_t sourceRowBytes = 0;
    std::size_t sourceRows = 0;
    std::uint32_t reportedWidth = 0;
    std::uint32_t reportedHeight = 0;
    std::uint64_t reportedFormat = 0;
};

using RangeReason = GocatorRangeRejection::Reason;

/** Routes anomaly evidence to the first-evidence slots and the fixed-capacity per-frame record. */
struct AnomalySink
{
    GocatorRangeRejection* firstRange = nullptr;
    GocatorImageRejection* firstImage = nullptr;
    GocatorFrameAnomalies* list = nullptr;
    std::uint32_t partCount = 0;

    void range(GocatorRangeRejection evidence) const noexcept
    {
        evidence.partCount = partCount;
        if (firstRange && firstRange->reason == RangeReason::None) *firstRange = evidence;
        if (list) list->add(evidence);
    }

    /** Storage evidence replaces the first slot: it describes why the frame was lost. */
    void rangeStorage(GocatorRangeRejection evidence) const noexcept
    {
        evidence.partCount = partCount;
        if (firstRange) *firstRange = evidence;
        if (list) list->add(evidence);
    }

    void image(const GocatorImageRejection& evidence) const noexcept
    {
        if (firstImage && firstImage->reason == GraphicsImageFailure::None) *firstImage = evidence;
        if (list) list->add(evidence);
    }
};

[[nodiscard]] GocatorImageRejection evidenceFromLayout(
    const ImageLayout& layout, const GraphicsImageFailure reason) noexcept
{
    GocatorImageRejection evidence;
    evidence.reason = reason;
    evidence.width = layout.reportedWidth;
    evidence.height = layout.reportedHeight;
    evidence.frameSequence = layout.frameSequence;
    evidence.pixelFormat = layout.reportedFormat;
    evidence.reportedStride = layout.sourceRowBytes;
    evidence.effectiveStride = static_cast<std::uint64_t>(layout.stride);
    evidence.minimumRowBytes = layout.minimumRowBytes;
    evidence.readableBytes = layout.dataSize;
    evidence.requiredBytes = layout.size;
    return evidence;
}

[[nodiscard]] GocatorImageRejection evidenceFromImage(
    const GoPxLSdk::GoGdpImage& image, const StampInfo& stamp,
    const GraphicsImageFailure reason, const SourceBytes& source) noexcept
{
    GocatorImageRejection evidence;
    evidence.reason = reason;
    evidence.width = image.Width();
    evidence.height = image.Height();
    evidence.frameSequence = stamp.valid ? stamp.frameIndex : image.DataSetId();
    evidence.pixelFormat = static_cast<std::uint64_t>(image.PixelFormat());
    evidence.readableBytes = source.dataSize;
    evidence.reportedStride = source.rowBytes;
    evidence.effectiveStride = source.rowBytes;
    return evidence;
}

[[nodiscard]] std::size_t imageAlignment(const GraphicsImagePixelFormat format) noexcept
{
    const auto& info = graphicsImagePixelFormatInfo(format);
    return info.componentBits >= 32U ? 4U : info.componentBits >= 16U ? 2U : 1U;
}

/** Last-resort fit: publish the delivered bytes as a Mono8 image, one source row per image row. */
[[nodiscard]] std::optional<ImageLayout> rawBytesLayout(
    const SourceBytes& source, const std::uint64_t frameSequence, const int sourceFormat) noexcept
{
    if (!source.valid()
        || source.rowBytes > static_cast<std::size_t>((std::numeric_limits<int>::max)())
        || source.rows > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return std::nullopt;
    }
    ImageLayout layout;
    layout.width = static_cast<int>(source.rowBytes);
    layout.height = static_cast<int>(source.rows);
    layout.stride = layout.width;
    layout.size = source.rowBytes * source.rows;
    layout.minimumRowBytes = source.rowBytes;
    layout.pixelFormat = GraphicsImagePixelFormat::Mono8;
    layout.bitsPerPixel = 8;
    layout.sourceFormat = sourceFormat;
    layout.frameSequence = frameSequence;
    layout.data = source.data;
    layout.dataBytes = source.bytes;
    layout.dataSize = source.dataSize;
    layout.sourceRowBytes = source.rowBytes;
    layout.sourceRows = source.rows;
    return layout;
}

/**
 * Fits one GDP image into a valid GraphicsEngine layout. Wrong data never drops the part: an unknown
 * format or unrepresentable layout is published as raw Mono8 bytes, a short or narrow buffer is
 * planned at its declared size and zero-filled, and every anomaly is reported to the sink. Only a
 * part with no bytes at all yields no layout.
 */
[[nodiscard]] std::optional<ImageLayout> fitImage(
    const GoPxLSdk::GoGdpImage& image, const StampInfo& stamp, const AnomalySink& sink)
{
    const SourceBytes source = sourceOf2D(image.Pixels());
    const std::uint64_t sequence = stamp.valid ? stamp.frameIndex : image.DataSetId();
    GraphicsImageFailure reasons[8]{};
    std::size_t reasonCount = 0;
    const auto note = [&](const GraphicsImageFailure reason) {
        if (reasonCount < 8U) reasons[reasonCount++] = reason;
    };
    const auto finish = [&](std::optional<ImageLayout> layout) -> std::optional<ImageLayout> {
        if (layout) {
            layout->reportedWidth = image.Width();
            layout->reportedHeight = image.Height();
            layout->reportedFormat = static_cast<std::uint64_t>(image.PixelFormat());
        }
        for (std::size_t index = 0; index < reasonCount; ++index) {
            sink.image(layout ? evidenceFromLayout(*layout, reasons[index])
                              : evidenceFromImage(image, stamp, reasons[index], source));
        }
        return layout;
    };
    const auto rawFallback = [&]() -> std::optional<ImageLayout> {
        auto layout = rawBytesLayout(source, sequence, static_cast<int>(image.PixelFormat()));
        if (!layout) note(GraphicsImageFailure::ExtentOverflow);
        return finish(std::move(layout));
    };

    const auto width = positiveInt(image.Width());
    const auto height = positiveInt(image.Height());
    if (!width || !height) note(GraphicsImageFailure::InvalidDimensions);
    if (!source.valid()) {
        note(source.failure);
        return finish(std::nullopt);
    }
    if (!width || !height) return rawFallback();

    const auto pixelFormat = graphicsPixelFormat(image.PixelFormat(), image.ColorFilter());
    if (pixelFormat == GraphicsImagePixelFormat::Unknown) {
        note(GraphicsImageFailure::UnsupportedFormat);
        return rawFallback();
    }
    const double pixelBytes = GoPxLSdk::GoGdpPixelFormat::PixelBytes(image.PixelFormat());
    const double declaredPixelBytes = std::ceil(pixelBytes);
    const bool pixelBytesUsable = std::isfinite(pixelBytes) && pixelBytes > 0.0
        && declaredPixelBytes <= static_cast<double>((std::numeric_limits<k32u>::max)());
    if (!pixelBytesUsable || image.PixelSize() != static_cast<k32u>(declaredPixelBytes))
        note(GraphicsImageFailure::InvalidStride);

    std::size_t minimumRowBytes = GraphicsImageValidation::minimumRowBytes(pixelFormat, *width);
    if (pixelBytesUsable) {
        const double sdkRowBytesExact = static_cast<double>(*width) * pixelBytes;
        if (!std::isfinite(sdkRowBytesExact)
            || sdkRowBytesExact > static_cast<double>((std::numeric_limits<std::size_t>::max)())) {
            note(GraphicsImageFailure::ExtentOverflow);
            return rawFallback();
        }
        minimumRowBytes = (std::max)(minimumRowBytes,
            static_cast<std::size_t>(std::ceil(sdkRowBytesExact)));
    }
    if (wordAlignedSourceFormat(image.PixelFormat()))
        minimumRowBytes = (std::max)(minimumRowBytes,
            static_cast<std::size_t>(*width) * sizeof(std::uint16_t));
    if (minimumRowBytes == 0U) {
        note(GraphicsImageFailure::ExtentOverflow);
        return rawFallback();
    }
    if (source.rowBytes < minimumRowBytes) note(GraphicsImageFailure::StrideBelowMinimum);
    const std::size_t stride = (std::max)(source.rowBytes, minimumRowBytes);
    if (stride > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        note(GraphicsImageFailure::InvalidStride);
        return rawFallback();
    }
    if (static_cast<std::size_t>(*height) > (std::numeric_limits<std::size_t>::max)() / stride) {
        note(GraphicsImageFailure::ExtentOverflow);
        return rawFallback();
    }
    const std::size_t destinationBytes = stride * static_cast<std::size_t>(*height);
    if (source.backedRows() < static_cast<std::size_t>(*height)) note(GraphicsImageFailure::TruncatedBytes);
    const auto layoutFailure = GraphicsImageValidation::layoutFailure(
        pixelFormat, *width, *height, static_cast<int>(stride), destinationBytes);
    if (layoutFailure != GraphicsImageFailure::None) {
        note(layoutFailure);
        return rawFallback();
    }

    ImageLayout layout;
    layout.width = *width;
    layout.height = *height;
    layout.stride = static_cast<int>(stride);
    layout.size = destinationBytes;
    layout.minimumRowBytes = minimumRowBytes;
    layout.pixelFormat = pixelFormat;
    layout.bitsPerPixel = GraphicsImageValidation::packedBitsPerPixel(pixelFormat);
    layout.sourceFormat = static_cast<int>(image.PixelFormat());
    layout.declaredMonoSourceBits = declaredMonoSourceBits(image.PixelFormat());
    layout.bitAlignment = wordAlignedSourceFormat(image.PixelFormat())
        ? GraphicsImageBitAlignment::LeastSignificant : GraphicsImageBitAlignment::Packed;
    layout.frameSequence = sequence;
    layout.data = source.data;
    layout.dataBytes = source.bytes;
    layout.dataSize = source.dataSize;
    layout.sourceRowBytes = source.rowBytes;
    layout.sourceRows = source.rows;
    return finish(std::move(layout));
}

/** Copies the existing source bytes row by row into the fitted stride and zero-fills the rest. */
void fillImageBytes(const ImageLayout& layout, std::uint8_t* const destination) noexcept
{
    const std::size_t stride = static_cast<std::size_t>(layout.stride);
    if (layout.sourceRowBytes == stride) {
        const std::size_t copied = (std::min)(layout.dataBytes, layout.size);
        std::memcpy(destination, layout.data, copied);
        if (copied < layout.size) std::memset(destination + copied, 0, layout.size - copied);
        return;
    }
    const std::size_t rows = static_cast<std::size_t>(layout.height);
    for (std::size_t row = 0; row < rows; ++row) {
        std::uint8_t* const out = destination + row * stride;
        const std::size_t sourceOffset = row * layout.sourceRowBytes;
        std::size_t copied = 0U;
        if (row < layout.sourceRows && sourceOffset < layout.dataBytes) {
            copied = (std::min)((std::min)(layout.sourceRowBytes, stride), layout.dataBytes - sourceOffset);
            std::memcpy(out, layout.data + sourceOffset, copied);
        }
        if (copied < stride) std::memset(out + copied, 0, stride - copied);
    }
}

[[nodiscard]] GraphicsImage writeFittedImage(
    GraphicsFrameBuilder& builder, const ImageLayout& layout, GraphicsImageFailure* failure)
{
    auto reserved = builder.reserve<std::uint8_t>(layout.size, imageAlignment(layout.pixelFormat));
    if (!reserved) {
        *failure = GraphicsImageFailure::StorageRefused;
        return {};
    }
    GraphicsWriteRegion<std::uint8_t> region = std::move(reserved).value();
    fillImageBytes(layout, region.data());

    GraphicsImage image;
    image.pixels = builder.seal(std::move(region));
    image.width = layout.width;
    image.height = layout.height;
    image.stride = layout.stride;
    image.bitsPerPixel = layout.bitsPerPixel;
    image.declaredMonoSourceBits = layout.declaredMonoSourceBits;
    image.sourceFormat = layout.sourceFormat;
    image.pixelFormat = layout.pixelFormat;
    image.bitAlignment = layout.bitAlignment;
    image.frameSequence = layout.frameSequence;
    *failure = GraphicsImageFailure::None;
    return image;
}

/** Fitted range conversion: metadata, planned writer layout, and borrowed SDK sample pointers. */
struct RangeConversion
{
    enum class Codec { UniformSurface, SurfacePointCloud, UniformProfile, ProfilePointCloud };

    Codec codec = Codec::UniformSurface;
    GraphicsRangeFrameLayout layout;
    RangeFrame fields;
    int width = 0;
    int height = 0;
    const std::int16_t* samples = nullptr;
    std::size_t sourceRows = 0;
    std::size_t sourceColumns = 0;
    const std::uint8_t* intensityData = nullptr;
    std::size_t intensityItemSize = 0;
    std::size_t intensityRows = 0;
    std::size_t intensityColumns = 0;
    kPoint3d64f resolution{};
    kPoint3d64f offset{};
    GraphicsRangeWriteLayout writePlan;
};

[[nodiscard]] constexpr std::size_t samplesPerPoint(const RangeConversion::Codec codec) noexcept
{
    switch (codec) {
    case RangeConversion::Codec::UniformSurface: return 1U;
    case RangeConversion::Codec::SurfacePointCloud: return 3U;
    case RangeConversion::Codec::UniformProfile: return 1U;
    case RangeConversion::Codec::ProfilePointCloud: return 2U;
    }
    return 1U;
}

void convertIntensity(const std::uint8_t* const source, const std::size_t itemSize,
                      const std::size_t count, float* const values)
{
    if (itemSize == sizeof(k8u))
    {
        GRAPHICSENGINE_OMP_PARALLEL_FOR
        for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(count); ++index)
        {
            values[static_cast<std::size_t>(index)] = static_cast<float>(source[index]);
        }
        return;
    }

    const auto* src = reinterpret_cast<const k16u*>(source);
    GRAPHICSENGINE_OMP_PARALLEL_FOR
    for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(count); ++index)
    {
        values[static_cast<std::size_t>(index)] = static_cast<float>(src[index]);
    }
}

void fillIntensity(const RangeConversion& conversion, float* const values)
{
    const std::size_t width = static_cast<std::size_t>(conversion.width);
    const std::size_t height = static_cast<std::size_t>(conversion.height);
    const std::size_t itemSize = conversion.intensityItemSize;
    if (conversion.intensityColumns == width && conversion.intensityRows >= height) {
        convertIntensity(conversion.intensityData, itemSize, width * height, values);
        return;
    }
    for (std::size_t row = 0; row < height; ++row) {
        float* const out = values + row * width;
        const std::size_t available = row < conversion.intensityRows
            ? (std::min)(conversion.intensityColumns, width) : 0U;
        if (available != 0U) {
            convertIntensity(conversion.intensityData + row * conversion.intensityColumns * itemSize,
                itemSize, available, out);
        }
        if (available < width) std::fill(out + available, out + width, 0.0F);
    }
}

void fillRangeSamples(const RangeConversion& conversion, GraphicsRangeFrameWriter& writer)
{
    const std::size_t width = static_cast<std::size_t>(conversion.width);
    const std::size_t height = static_cast<std::size_t>(conversion.height);
    const std::size_t pointSamples = samplesPerPoint(conversion.codec);
    float* const x = writer.xData();
    float* const y = writer.yData();
    float* const z = writer.zData();
    std::uint8_t* const mask = writer.validMaskData();

    if (conversion.codec == RangeConversion::Codec::ProfilePointCloud)
        std::fill_n(y, width * height, 0.0F);

    const auto convert = [&](const std::int16_t* const source, const std::size_t count,
                             const std::size_t offset) {
        std::uint8_t* const m = mask ? mask + offset : nullptr;
        switch (conversion.codec) {
        case RangeConversion::Codec::UniformSurface:
        case RangeConversion::Codec::UniformProfile:
            gocator_range::writeScalarZ(source, count, conversion.resolution.z, conversion.offset.z,
                z + offset, m);
            break;
        case RangeConversion::Codec::SurfacePointCloud:
            gocator_range::writeSurfacePoints(source, count,
                conversion.resolution.x, conversion.resolution.y, conversion.resolution.z,
                conversion.offset.x, conversion.offset.y, conversion.offset.z,
                x + offset, y + offset, z + offset, m);
            break;
        case RangeConversion::Codec::ProfilePointCloud:
            gocator_range::writeProfilePoints(source, count,
                conversion.resolution.x, conversion.resolution.z,
                conversion.offset.x, conversion.offset.z,
                x + offset, z + offset, m);
            break;
        }
    };

    if (conversion.sourceColumns == width && conversion.sourceRows >= height) {
        convert(conversion.samples, width * height, 0U);
    } else {
        for (std::size_t row = 0; row < height; ++row) {
            const std::size_t offset = row * width;
            const std::size_t available = row < conversion.sourceRows
                ? (std::min)(conversion.sourceColumns, width) : 0U;
            if (available != 0U)
                convert(conversion.samples + row * conversion.sourceColumns * pointSamples, available, offset);
            if (available < width) {
                for (float* const channel : {x, y, z}) {
                    if (channel) std::fill(channel + offset + available, channel + offset + width, 0.0F);
                }
                if (mask) std::fill(mask + offset + available, mask + offset + width, std::uint8_t{0});
            }
        }
    }
    if (conversion.layout.channels.intensity) fillIntensity(conversion, writer.intensityData());
}

/** Outcome of writing one fitted range part; a part failure never decides the other parts. */
struct RangeWrite
{
    std::optional<RangeFrame> frame;
    GocatorRangeRejection evidence;
    bool storageRefused = false;
};

[[nodiscard]] RangeWrite writeRange(const RangeConversion& conversion, GraphicsFrameBuilder& builder)
{
    RangeWrite result;
    GraphicsRangeFrameWriter writer(builder, conversion.layout);
    if (!writer.isValid())
    {
        result.evidence.width = static_cast<std::uint32_t>(conversion.width);
        result.evidence.height = static_cast<std::uint32_t>(conversion.height);
        result.evidence.requestedStorageBytes = writer.blockBytes();
        switch (writer.failure()) {
        case GraphicsRangeFrameWriterFailure::InvalidDimensions:
            result.evidence.reason = RangeReason::InvalidDimensions;
            break;
        case GraphicsRangeFrameWriterFailure::SizeLimitExceeded:
            result.evidence.reason = RangeReason::SizeLimitExceeded;
            break;
        case GraphicsRangeFrameWriterFailure::StorageRefused:
            result.evidence.reason = RangeReason::StorageRefused;
            result.storageRefused = true;
            break;
        case GraphicsRangeFrameWriterFailure::None:
            result.evidence.reason = RangeReason::InvalidLayout;
            break;
        }
        return result;
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
        result.evidence.reason = RangeReason::InvalidLayout;
        result.evidence.width = static_cast<std::uint32_t>(conversion.width);
        result.evidence.height = static_cast<std::uint32_t>(conversion.height);
        return result;
    }
    result.frame = std::move(frame);
    return result;
}

/** Borrowed SDK views and calibration of one range message. */
struct RangeInput
{
    RangeConversion::Codec codec = RangeConversion::Codec::UniformSurface;
    std::optional<int> declaredWidth;
    std::optional<int> declaredHeight;
    SourceBytes samples;
    std::size_t expectedItemSize = 0;
    /** True when a sample grid larger than the declared size is itself an anomaly. */
    bool strictExtent = false;
    bool profile = false;
    SourceBytes intensity;
    kPoint3d64f resolution{};
    kPoint3d64f offset{};
};

/** What one range message became: a fitted range and/or raw Mono8 images for unrepresentable arrays. */
struct RangePartPlan
{
    std::optional<RangeConversion> range;
    std::optional<ImageLayout> rawImage;
    std::optional<ImageLayout> auxRawImage;
};

[[nodiscard]] bool scanInvalid(const RangeConversion::Codec codec, const std::int16_t* const samples,
                               const std::size_t count) noexcept
{
    switch (codec) {
    case RangeConversion::Codec::UniformSurface:
    case RangeConversion::Codec::UniformProfile:
        return gocator_range::scalarHasInvalid(samples, count);
    case RangeConversion::Codec::SurfacePointCloud:
        return gocator_range::surfacePointsHaveInvalid(samples, count);
    case RangeConversion::Codec::ProfilePointCloud:
        return gocator_range::profilePointsHaveInvalid(samples, count);
    }
    return false;
}

[[nodiscard]] RangePartPlan planRange(
    const RangeInput& input, const GoPxLSdk::GoGdpMsg& message, const StampInfo& stamp,
    const GraphicsFrameRequest& request, const AnomalySink& sink, const std::uint32_t partIndex)
{
    RangePartPlan plan;
    const std::uint64_t sequence = stamp.valid ? stamp.frameIndex : message.DataSetId();
    const auto note = [&](const RangeReason reason, const std::size_t width, const std::size_t height) {
        GocatorRangeRejection evidence;
        evidence.reason = reason;
        evidence.part = partIndex;
        evidence.width = static_cast<std::uint32_t>(width);
        evidence.height = static_cast<std::uint32_t>(height);
        sink.range(evidence);
    };
    const bool declaredOk = input.declaredWidth && input.declaredHeight;
    const std::size_t declaredWidth = static_cast<std::size_t>(input.declaredWidth.value_or(0));
    const std::size_t declaredHeight = static_cast<std::size_t>(input.declaredHeight.value_or(0));
    if (!declaredOk) note(RangeReason::InvalidDimensions, declaredWidth, declaredHeight);
    if (!input.samples.valid()) {
        note(RangeReason::MissingData, declaredWidth, declaredHeight);
        return plan;
    }
    if (input.samples.itemSize != input.expectedItemSize) {
        note(RangeReason::UnsupportedFormat, declaredWidth, declaredHeight);
        plan.rawImage = rawBytesLayout(input.samples, sequence, 0);
        if (!plan.rawImage) note(RangeReason::SizeLimitExceeded, declaredWidth, declaredHeight);
        return plan;
    }

    const std::size_t columns = input.samples.columns;
    const std::size_t rows = input.samples.backedRows();
    const std::size_t pointSamples = samplesPerPoint(input.codec);
    const auto* const samples = reinterpret_cast<const std::int16_t*>(input.samples.data);
    const std::uint8_t intensityBits = request.includeRangeAuxiliaryChannels && input.intensity.valid()
        ? intensityBitsFromItemSize(input.intensity.itemSize) : 0U;
    const std::size_t intensityRows = input.intensity.backedRows();
    const std::size_t intensityColumns = input.intensity.columns;

    struct Fit
    {
        GraphicsRangeFrameLayout layout;
        GraphicsRangeWriteLayout writePlan;
        std::size_t width = 0;
        std::size_t height = 0;
    };
    RangeReason lastFailure = RangeReason::InvalidDimensions;
    const auto attempt = [&](const std::size_t width, const std::size_t height) -> std::optional<Fit> {
        const std::size_t intMax = static_cast<std::size_t>((std::numeric_limits<int>::max)());
        if (width == 0U || height == 0U || width > intMax || height > intMax) {
            lastFailure = RangeReason::InvalidDimensions;
            return std::nullopt;
        }
        if (height > (std::numeric_limits<std::size_t>::max)() / width / pointSamples) {
            lastFailure = RangeReason::SizeLimitExceeded;
            return std::nullopt;
        }
        bool invalid = rows < height || columns < width;
        if (!invalid) {
            if (columns == width && rows >= height) {
                invalid = scanInvalid(input.codec, samples, width * height);
            } else {
                for (std::size_t row = 0; row < height && !invalid; ++row)
                    invalid = scanInvalid(input.codec, samples + row * columns * pointSamples, width);
            }
        }
        Fit fit;
        fit.width = width;
        fit.height = height;
        fit.layout.width = static_cast<int>(width);
        fit.layout.height = static_cast<int>(height);
        fit.layout.channels.x = input.codec == RangeConversion::Codec::SurfacePointCloud
            || input.codec == RangeConversion::Codec::ProfilePointCloud;
        fit.layout.channels.y = fit.layout.channels.x;
        fit.layout.channels.validMask = invalid;
        fit.layout.channels.intensity = intensityBits != 0U;
        const auto writePlan = planGraphicsRangeWrite(fit.layout);
        if (!writePlan) {
            lastFailure = writePlan.error() == GraphicsFrameError::Capacity
                ? RangeReason::SizeLimitExceeded : RangeReason::InvalidDimensions;
            return std::nullopt;
        }
        fit.writePlan = writePlan.value();
        return fit;
    };

    const std::size_t arrayHeight = input.profile ? 1U : rows;
    std::optional<Fit> fit = attempt(declaredOk ? declaredWidth : columns,
                                     declaredOk ? declaredHeight : arrayHeight);
    if (!fit) {
        note(lastFailure, declaredWidth, declaredHeight);
        if (declaredOk && (declaredWidth != columns || declaredHeight != arrayHeight)) {
            fit = attempt(columns, arrayHeight);
            if (!fit) note(lastFailure, columns, arrayHeight);
        }
        if (!fit) {
            plan.rawImage = rawBytesLayout(input.samples, sequence, 0);
            return plan;
        }
    }

    if (rows < fit->height || columns < fit->width)
        note(RangeReason::TruncatedData, fit->width, fit->height);
    else if (input.strictExtent && (rows != fit->height || columns != fit->width))
        note(RangeReason::InvalidLayout, fit->width, fit->height);
    if (request.includeRangeAuxiliaryChannels && input.intensity.valid()) {
        if (intensityBits == 0U) {
            note(RangeReason::UnsupportedFormat, fit->width, fit->height);
            plan.auxRawImage = rawBytesLayout(input.intensity, sequence, 0);
        } else if (intensityRows < fit->height || intensityColumns < fit->width) {
            note(RangeReason::TruncatedData, fit->width, fit->height);
        } else if (input.strictExtent
                   && (intensityRows != fit->height || intensityColumns != fit->width)) {
            note(RangeReason::InvalidLayout, fit->width, fit->height);
        }
    }

    RangeConversion conversion;
    conversion.codec = input.codec;
    conversion.layout = fit->layout;
    conversion.width = fit->layout.width;
    conversion.height = fit->layout.height;
    conversion.samples = samples;
    conversion.sourceRows = rows;
    conversion.sourceColumns = columns;
    conversion.intensityData = input.intensity.data;
    conversion.intensityItemSize = input.intensity.itemSize;
    conversion.intensityRows = intensityRows;
    conversion.intensityColumns = intensityColumns;
    conversion.resolution = input.resolution;
    conversion.offset = input.offset;
    conversion.writePlan = fit->writePlan;
    applyCommonFrameFields(conversion.fields, conversion.width, conversion.height,
        conversion.resolution, conversion.offset, frameId(message, stamp));
    if (input.profile) conversion.fields.yScale = 1.0;
    if (conversion.codec == RangeConversion::Codec::SurfacePointCloud
        || conversion.codec == RangeConversion::Codec::ProfilePointCloud)
        conversion.fields.xyCoordinateMode = RangeFrameXYCoordinateMode::ExplicitXY;
    if (conversion.layout.channels.intensity)
    {
        conversion.fields.intensityBits = intensityBits;
        conversion.fields.intensityField.bitsPerSample = intensityBits;
    }
    plan.range = std::move(conversion);
    return plan;
}

[[nodiscard]] RangePartPlan planUniformSurface(
    const GoPxLSdk::GoGdpSurfaceUniform& msg, const StampInfo& stamp,
    const GraphicsFrameRequest& request, const AnomalySink& sink, const std::uint32_t index)
{
    RangeInput input;
    input.codec = RangeConversion::Codec::UniformSurface;
    input.declaredWidth = positiveInt(msg.Width());
    input.declaredHeight = positiveInt(msg.Length());
    input.samples = sourceOf2D(msg.Ranges());
    input.expectedItemSize = sizeof(k16s);
    input.strictExtent = true;
    input.intensity = sourceOf2D(msg.Intensities());
    input.resolution = msg.Resolution();
    input.offset = msg.Offset();
    return planRange(input, msg, stamp, request, sink, index);
}

[[nodiscard]] RangePartPlan planSurfacePointCloud(
    const GoPxLSdk::GoGdpSurfacePointCloud& msg, const StampInfo& stamp,
    const GraphicsFrameRequest& request, const AnomalySink& sink, const std::uint32_t index)
{
    RangeInput input;
    input.codec = RangeConversion::Codec::SurfacePointCloud;
    input.declaredWidth = positiveInt(msg.Width());
    input.declaredHeight = positiveInt(msg.Length());
    input.samples = sourceOf2D(msg.Ranges());
    input.expectedItemSize = sizeof(kPoint3d16s);
    input.strictExtent = true;
    input.intensity = sourceOf2D(msg.Intensities());
    input.resolution = msg.Resolution();
    input.offset = msg.Offset();
    return planRange(input, msg, stamp, request, sink, index);
}

[[nodiscard]] RangePartPlan planUniformProfile(
    const GoPxLSdk::GoGdpProfileUniform& msg, const StampInfo& stamp,
    const GraphicsFrameRequest& request, const AnomalySink& sink, const std::uint32_t index)
{
    RangeInput input;
    input.codec = RangeConversion::Codec::UniformProfile;
    input.declaredWidth = positiveInt(msg.Width());
    input.declaredHeight = 1;
    input.samples = sourceOf1D(msg.Ranges());
    input.expectedItemSize = sizeof(k16s);
    input.profile = true;
    input.intensity = sourceOf1D(msg.Intensities());
    input.resolution = msg.Resolution();
    input.offset = msg.Offset();
    return planRange(input, msg, stamp, request, sink, index);
}

[[nodiscard]] RangePartPlan planProfilePointCloud(
    const GoPxLSdk::GoGdpProfilePointCloud& msg, const StampInfo& stamp,
    const GraphicsFrameRequest& request, const AnomalySink& sink, const std::uint32_t index)
{
    RangeInput input;
    input.codec = RangeConversion::Codec::ProfilePointCloud;
    input.declaredWidth = positiveInt(msg.Width());
    input.declaredHeight = 1;
    input.samples = sourceOf1D(msg.Ranges());
    input.expectedItemSize = sizeof(kPoint16s);
    input.profile = true;
    input.intensity = sourceOf1D(msg.Intensities());
    input.resolution = msg.Resolution();
    input.offset = msg.Offset();
    return planRange(input, msg, stamp, request, sink, index);
}

struct PartName
{
    char id[16]{};
    char display[24]{};
};

[[nodiscard]] PartName partName(const bool image, const std::size_t ordinal) noexcept
{
    PartName name;
    const char* const base = image ? "image" : "range";
    const char* const display = image ? "Image" : "Range";
    if (ordinal == 0U) {
        std::snprintf(name.id, sizeof(name.id), "%s", base);
        std::snprintf(name.display, sizeof(name.display), "%s", display);
    } else {
        std::snprintf(name.id, sizeof(name.id), "%s-%zu", base, ordinal + 1U);
        std::snprintf(name.display, sizeof(name.display), "%s %zu", display, ordinal + 1U);
    }
    return name;
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
    const std::atomic<bool>* cancelled,
    GocatorFrameAnomalies* anomalies) const
{
    rejection = {};
    if (imageRejection) *imageRejection = {};
    if (storageError) *storageError = GraphicsFrameError::None;
    if (anomalies) *anomalies = {};
    auto frame = convertGraphicsFrame(dataSet, request, contextSource, &rejection,
        imageRejection, frameIndex, storageError, cancelled, anomalies);
    if (!frame || !frame->isValid()) return std::nullopt;
    const bool hasImage = frame->hasImage();
    const bool hasRange = frame->hasRangeFrame();
    rejection.frameRetained = true;
    rejection.imageRetained = hasImage;
    rejection.rangeRetained = hasRange;
    if (imageRejection) imageRejection->imageRetained = hasImage;
    if (anomalies) {
        for (std::size_t index = 0; index < anomalies->rangeCount; ++index) {
            auto& entry = anomalies->ranges[index];
            entry.frameRetained = true;
            entry.imageRetained = hasImage;
            entry.rangeRetained = hasRange;
        }
        for (std::size_t index = 0; index < anomalies->imageCount; ++index)
            anomalies->images[index].imageRetained = hasImage;
    }
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
    const std::atomic<bool>* cancelled,
    GocatorFrameAnomalies* anomalies) const
{
    const bool wantsRange = hasGraphicsFrameComponent(request.components, GraphicsFrameComponent::Range);
    const bool wantsImage = hasGraphicsFrameComponent(request.components, GraphicsFrameComponent::Image);
    if (!wantsRange && !wantsImage) return std::nullopt;

    const StampInfo stamp = readStamp(dataSet);
    const auto count = dataSet.Count();
    const auto partCount = static_cast<std::uint32_t>(
        (std::min)(count, static_cast<decltype(count)>((std::numeric_limits<std::uint32_t>::max)())));
    if (rejection) rejection->partCount = partCount;
    const AnomalySink sink{rejection, imageRejection, anomalies, partCount};

    // Pass 1: fit every part of the dataset. No part is dropped for a validation reason and one
    // part never decides another; only a part without any bytes is absent.
    struct ImagePart
    {
        ImageLayout layout;
        const GoPxLSdk::GoGdpMsg* message = nullptr;
    };
    struct RangePart
    {
        RangeConversion conversion;
        const GoPxLSdk::GoGdpMsg* message = nullptr;
        std::uint32_t index = 0;
    };
    std::array<std::optional<ImagePart>, kMaxParts> images;
    std::array<std::optional<RangePart>, kMaxParts> ranges;
    std::size_t imageSlots = 0;
    std::size_t rangeSlots = 0;
    bool sawRange = false;

    const auto addImage = [&](std::optional<ImageLayout>&& layout, const GoPxLSdk::GoGdpMsg& message) {
        if (!layout) return;
        if (imageSlots == kMaxParts) {
            GocatorImageRejection evidence = evidenceFromLayout(*layout, GraphicsImageFailure::ExtentOverflow);
            sink.image(evidence);
            return;
        }
        images[imageSlots++].emplace(ImagePart{std::move(*layout), &message});
    };
    const auto addPlan = [&](RangePartPlan&& plan, const GoPxLSdk::GoGdpMsg& message,
                             const std::uint32_t index) {
        addImage(std::move(plan.rawImage), message);
        addImage(std::move(plan.auxRawImage), message);
        if (!plan.range) return;
        if (rangeSlots == kMaxParts) {
            GocatorRangeRejection evidence;
            evidence.reason = RangeReason::SizeLimitExceeded;
            evidence.part = index;
            evidence.width = static_cast<std::uint32_t>(plan.range->width);
            evidence.height = static_cast<std::uint32_t>(plan.range->height);
            sink.range(evidence);
            return;
        }
        ranges[rangeSlots++].emplace(RangePart{std::move(*plan.range), &message, index});
    };

    for (std::size_t index = 0; index < count; ++index) {
        const auto& message = dataSet.GdpMsgAt(index);
        const auto partIndex = static_cast<std::uint32_t>(index);
        switch (message.Type()) {
        case GoPxLSdk::MessageType::IMAGE:
            if (wantsImage)
                addImage(fitImage(static_cast<const GoPxLSdk::GoGdpImage&>(message), stamp, sink), message);
            break;
        case GoPxLSdk::MessageType::UNIFORM_SURFACE:
            if (!wantsRange) break;
            sawRange = true;
            addPlan(planUniformSurface(static_cast<const GoPxLSdk::GoGdpSurfaceUniform&>(message),
                stamp, request, sink, partIndex), message, partIndex);
            break;
        case GoPxLSdk::MessageType::SURFACE_POINT_CLOUD:
            if (!wantsRange) break;
            sawRange = true;
            addPlan(planSurfacePointCloud(static_cast<const GoPxLSdk::GoGdpSurfacePointCloud&>(message),
                stamp, request, sink, partIndex), message, partIndex);
            break;
        case GoPxLSdk::MessageType::UNIFORM_PROFILE:
            if (!wantsRange) break;
            sawRange = true;
            addPlan(planUniformProfile(static_cast<const GoPxLSdk::GoGdpProfileUniform&>(message),
                stamp, request, sink, partIndex), message, partIndex);
            break;
        case GoPxLSdk::MessageType::PROFILE_POINT_CLOUD:
            if (!wantsRange) break;
            sawRange = true;
            addPlan(planProfilePointCloud(static_cast<const GoPxLSdk::GoGdpProfilePointCloud&>(message),
                stamp, request, sink, partIndex), message, partIndex);
            break;
        default:
            break;
        }
    }
    if (wantsRange && !sawRange) {
        GocatorRangeRejection evidence;
        evidence.reason = RangeReason::MissingRange;
        sink.range(evidence);
    }
    if (imageSlots == 0U && rangeSlots == 0U) return std::nullopt;

    // Plan the exact destination footprint in real reserve order: image-like parts first, then
    // ranges. A part whose size cannot be added to the frame total is skipped with its diagnostic;
    // the other parts still publish. This is a property of the part, not a storage refusal.
    std::size_t sampleBytes = 0U;
    std::size_t liveImages = 0U;
    std::size_t liveRanges = 0U;
    for (auto& part : images) {
        if (!part) continue;
        if (!appendAligned(sampleBytes, part->layout.size, imageAlignment(part->layout.pixelFormat))) {
            sink.image(evidenceFromLayout(part->layout, GraphicsImageFailure::ExtentOverflow));
            part.reset();
            continue;
        }
        ++liveImages;
    }
    for (auto& part : ranges) {
        if (!part) continue;
        if (!appendAligned(sampleBytes, part->conversion.writePlan.totalBytes,
                           part->conversion.writePlan.alignment)) {
            GocatorRangeRejection evidence;
            evidence.reason = RangeReason::SizeLimitExceeded;
            evidence.part = part->index;
            evidence.width = static_cast<std::uint32_t>(part->conversion.width);
            evidence.height = static_cast<std::uint32_t>(part->conversion.height);
            sink.range(evidence);
            part.reset();
            continue;
        }
        ++liveRanges;
    }
    if (liveImages == 0U && liveRanges == 0U) return std::nullopt;

    const auto firstImagePart = [&]() -> const ImagePart* {
        for (const auto& part : images) if (part) return &*part;
        return nullptr;
    };
    const auto firstRangePart = [&]() -> const RangePart* {
        for (const auto& part : ranges) if (part) return &*part;
        return nullptr;
    };

    if (cancelled && cancelled->load(std::memory_order_acquire)) {
        if (storageError) *storageError = GraphicsFrameError::Disconnected;
        return std::nullopt;
    }

    auto contextResult = contextSource.beginFrame(sampleBytes);
    if (!contextResult) {
        const GraphicsFrameError err = contextResult.error();
        if (storageError) *storageError = err;
        if (const auto* range = firstRangePart()) {
            GocatorRangeRejection evidence;
            evidence.reason = err == GraphicsFrameError::Capacity
                ? RangeReason::SizeLimitExceeded : RangeReason::StorageRefused;
            evidence.width = static_cast<std::uint32_t>(range->conversion.width);
            evidence.height = static_cast<std::uint32_t>(range->conversion.height);
            evidence.requestedStorageBytes = sampleBytes;
            evidence.part = range->index;
            sink.rangeStorage(evidence);
        } else if (const auto* image = firstImagePart()) {
            sink.image(evidenceFromLayout(image->layout,
                err == GraphicsFrameError::Capacity
                    ? GraphicsImageFailure::ExtentOverflow
                    : GraphicsImageFailure::StorageRefused));
        }
        return std::nullopt;
    }

    if (cancelled && cancelled->load(std::memory_order_acquire)) {
        if (storageError) *storageError = GraphicsFrameError::Disconnected;
        return std::nullopt;
    }

    // Pass 2: write each fitted part. A part that cannot be written is reported and skipped; only
    // a storage refusal, which is a transport condition, drops the frame.
    GraphicsFrameBuilder builder(contextResult.value());
    std::size_t addedImages = 0U;
    std::size_t addedRanges = 0U;
    const GoPxLSdk::GoGdpMsg* primaryImage = nullptr;
    const GoPxLSdk::GoGdpMsg* primaryRange = nullptr;
    for (const auto& part : images) {
        if (!part) continue;
        GraphicsImageFailure failure = GraphicsImageFailure::None;
        auto converted = writeFittedImage(builder, part->layout, &failure);
        if (!converted.isValid()) {
            if (failure == GraphicsImageFailure::StorageRefused) {
                if (storageError) *storageError = GraphicsFrameError::Allocation;
                sink.image(evidenceFromLayout(part->layout, failure));
                return std::nullopt;
            }
            sink.image(evidenceFromLayout(part->layout,
                failure == GraphicsImageFailure::None ? GraphicsImageFailure::InvalidStride : failure));
            continue;
        }
        const PartName name = partName(true, addedImages);
        builder.addImage(name.id, std::move(converted), name.display, "primary");
        builder.content().images.back().descriptor.registration = GraphicsImageRegistration::Unregistered;
        if (!primaryImage) primaryImage = part->message;
        ++addedImages;
    }
    for (const auto& part : ranges) {
        if (!part) continue;
        auto written = writeRange(part->conversion, builder);
        if (!written.frame) {
            written.evidence.part = part->index;
            if (written.storageRefused) {
                if (storageError) *storageError = GraphicsFrameError::Allocation;
                sink.rangeStorage(written.evidence);
                return std::nullopt;
            }
            sink.range(written.evidence);
            continue;
        }
        const PartName name = partName(false, addedRanges);
        builder.addRange(name.id, std::move(*written.frame), name.display, "primary");
        if (!primaryRange) primaryRange = part->message;
        ++addedRanges;
    }
    if (addedImages == 0U && addedRanges == 0U) return std::nullopt;

    GraphicsFrameMetadata metadata;
    metadata.sourceName = "LMI Gocator";
    const GoPxLSdk::GoGdpMsg& primaryMsg = primaryRange ? *primaryRange : *primaryImage;
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
        if (const auto* range = firstRangePart()) {
            GocatorRangeRejection evidence;
            evidence.reason = RangeReason::StorageRefused;
            evidence.part = range->index;
            evidence.requestedStorageBytes = sampleBytes;
            sink.rangeStorage(evidence);
        } else if (const auto* image = firstImagePart()) {
            sink.image(evidenceFromLayout(image->layout, GraphicsImageFailure::StorageRefused));
        }
        return std::nullopt;
    }
    return std::move(published).value();
}
