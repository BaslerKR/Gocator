#include "GocatorDataSetGraphicsFrameAdapter.h"
#include "GocatorRangeConversion.h"
#include "Internal/GocatorSdkRuntime.h"

#include "DeviceRuntime/Contract/FrameStorage.h"
#include "graphics/data/GraphicsRangeFrameWriter.h"

#include <GoPxLSdk/GoDataSet.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpImage.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpPixelFormat.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfilePointCloud.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfileUniform.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpStamp.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpSurfacePointCloud.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpSurfaceUniform.h>
#include <kApi/Data/kArray1.h>
#include <kApi/Data/kArray2.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<const GraphicsFrame&>().images())>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(*std::declval<GraphicsSpan<std::uint8_t>>().data())>>);

struct RecordingStorage : IFrameStorageProvider
{
    std::size_t calls = 0;
    std::size_t bytes = 0;
    std::shared_ptr<std::uint8_t> block;
    std::shared_ptr<std::uint8_t> acquire(std::size_t requested) override
    {
        ++calls;
        bytes = requested;
        block = std::shared_ptr<std::uint8_t>(new std::uint8_t[requested], std::default_delete<std::uint8_t[]>());
        return block;
    }
};

/** Plans one lease the way the worker does for the adapter: blocks laid out back to back. */
class RecordingProvider final : public IFrameStorageProvider
{
public:
    std::size_t calls = 0;
    std::size_t singleCalls = 0;
    std::size_t bytes = 0;
    bool refuse = false;
    std::shared_ptr<std::uint8_t> block;
    FrameStorageShape shape;
    FrameStoragePlan plan;

    std::shared_ptr<std::uint8_t> acquire(std::size_t) override
    {
        ++singleCalls;
        return {};
    }

    FrameStoragePlan acquireFrame(const FrameStorageShape& requested) override
    {
        ++calls;
        shape = requested;
        FrameStoragePlan result;
        std::size_t end = 0;
        for (const auto& image : requested.images) {
            result.images.push_back({end, image.size});
            end += image.size;
        }
        for (const auto& range : requested.ranges) {
            end = (end + 63U) / 64U * 64U;
            GraphicsFrameBuilder measureBuilder;
            const GraphicsRangeFrameWriter measure(measureBuilder, range.layout);
            result.ranges.push_back({end, measure.blockBytes()});
            end += measure.blockBytes();
        }
        result.totalBytes = end;
        bytes = end;
        if (refuse) {
            result.refusal = FrameStorageRefusal::NoStorage;
            plan = result;
            return result;
        }
        block = std::shared_ptr<std::uint8_t>(new std::uint8_t[end], std::default_delete<std::uint8_t[]>());
        result.storage = block;
        plan = result;
        return result;
    }
};

} // namespace

/**
 * @brief Test-owned friend of the GoPxL message classes.
 *
 * The SDK headers declare this exact global class as a friend so their own tests can fill
 * message fields without a serializer. This suite uses the same seam to build the synthetic
 * `GoDataSet` inputs the Gocator adapter consumes; production code never depends on it.
 */
class GoGdpMsgTests
{
public:
    template <typename Message>
    static void setDataSetId(Message& message, const k64u id)
    {
        message.attributes.dataSetId = id;
    }

    static void setStamp(GoPxLSdk::GoGdpStamp& message, const k64u frameIndex)
    {
        message.frameIndex = frameIndex;
    }

    static void setImage(GoPxLSdk::GoGdpImage& message,
                         const k32u width,
                         const k32u height,
                         const GoPxLSdk::GoGdpPixelFormat::FormatType format,
                         const std::vector<k8u>& pixels)
    {
        assert(pixels.size() == std::size_t(width) * std::size_t(height));
        message.width = width;
        message.height = height;
        message.pixelSize = sizeof(k8u);
        message.pixelFormat = format;
        kArray2_Construct(message.pixels.Ref(), kTypeOf(k8u), height, width, kNULL);
        std::memcpy(kArray2_DataT(message.pixels.Get(), k8u), pixels.data(), pixels.size());
    }

    static void setImage(GoPxLSdk::GoGdpImage& message,
                         const k32u width,
                         const k32u height,
                         const GoPxLSdk::GoGdpPixelFormat::FormatType format,
                         const std::vector<k16u>& pixels)
    {
        assert(pixels.size() == std::size_t(width) * std::size_t(height));
        message.width = width;
        message.height = height;
        message.pixelSize = sizeof(k16u);
        message.pixelFormat = format;
        kArray2_Construct(message.pixels.Ref(), kTypeOf(k16u), height, width, kNULL);
        std::memcpy(kArray2_DataT(message.pixels.Get(), k16u), pixels.data(),
            pixels.size() * sizeof(k16u));
    }

    static void setImageBuffer(GoPxLSdk::GoGdpImage& message,
                               const k32u width,
                               const k32u height,
                               const GoPxLSdk::GoGdpPixelFormat::FormatType format,
                               const k32u pixelSize,
                               const k32u rowBytes,
                               const std::vector<k8u>& bytes)
    {
        assert(bytes.size() == std::size_t(rowBytes) * std::size_t(height));
        message.width = width;
        message.height = height;
        message.pixelSize = pixelSize;
        message.pixelFormat = format;
        kArray2_Construct(message.pixels.Ref(), kTypeOf(k8u), height, rowBytes, kNULL);
        std::memcpy(kArray2_DataT(message.pixels.Get(), k8u), bytes.data(), bytes.size());
    }

    static void setImageHeight(GoPxLSdk::GoGdpImage& message, const k32u height)
    {
        message.height = height;
    }

    static void setSurfaceWidth(GoPxLSdk::GoGdpSurfaceUniform& message, k32u width)
    {
        message.width = width;
    }

    static void setUniformSurface(GoPxLSdk::GoGdpSurfaceUniform& message,
                                  const k32u width,
                                  const k32u height,
                                  const kPoint3d64f& resolution,
                                  const kPoint3d64f& offset,
                                  const std::vector<k16s>& ranges,
                                  const std::vector<k16u>& intensities,
                                  const k32u intensityWidth = 0,
                                  const k32u intensityLength = 0)
    {
        assert(ranges.size() == std::size_t(width) * std::size_t(height));
        message.width = width;
        message.length = height;
        message.resolution = resolution;
        message.offset = offset;
        kArray2_Construct(message.ranges.Ref(), kTypeOf(k16s), height, width, kNULL);
        std::memcpy(kArray2_DataT(message.ranges.Get(), k16s), ranges.data(),
            ranges.size() * sizeof(k16s));
        if (!intensities.empty())
        {
            const k32u columns = intensityWidth != 0 ? intensityWidth : width;
            const k32u rows = intensityLength != 0 ? intensityLength : height;
            assert(intensities.size() == std::size_t(columns) * std::size_t(rows));
            message.intensityWidth = columns;
            message.intensityLength = rows;
            kArray2_Construct(message.intensities.Ref(), kTypeOf(k16u), rows, columns, kNULL);
            std::memcpy(kArray2_DataT(message.intensities.Get(), k16u), intensities.data(),
                intensities.size() * sizeof(k16u));
        }
    }

    static void setSurfacePointCloud(GoPxLSdk::GoGdpSurfacePointCloud& message,
                                     const k32u width,
                                     const k32u height,
                                     const kPoint3d64f& resolution,
                                     const kPoint3d64f& offset,
                                     const std::vector<kPoint3d16s>& points,
                                     const std::vector<k16u>& intensities = {})
    {
        assert(points.size() == std::size_t(width) * std::size_t(height));
        message.width = width;
        message.length = height;
        message.resolution = resolution;
        message.offset = offset;
        kArray2_Construct(message.ranges.Ref(), kTypeOf(kPoint3d16s), height, width, kNULL);
        std::memcpy(kArray2_DataT(message.ranges.Get(), kPoint3d16s), points.data(),
            points.size() * sizeof(kPoint3d16s));
        if (!intensities.empty())
        {
            assert(intensities.size() == points.size());
            message.intensityWidth = width;
            message.intensityLength = height;
            kArray2_Construct(message.intensities.Ref(), kTypeOf(k16u), height, width, kNULL);
            std::memcpy(kArray2_DataT(message.intensities.Get(), k16u), intensities.data(),
                intensities.size() * sizeof(k16u));
        }
    }

    static void setUniformProfile(GoPxLSdk::GoGdpProfileUniform& message,
                                  const k32u width,
                                  const kPoint3d64f& resolution,
                                  const kPoint3d64f& offset,
                                  const std::vector<k16s>& ranges,
                                  const std::vector<k8u>& intensities = {})
    {
        assert(ranges.size() == std::size_t(width));
        message.width = width;
        message.resolution = resolution;
        message.offset = offset;
        kArray1_Construct(message.ranges.Ref(), kTypeOf(k16s), width, kNULL);
        std::memcpy(kArray1_DataT(message.ranges.Get(), k16s), ranges.data(),
            ranges.size() * sizeof(k16s));
        if (!intensities.empty())
        {
            assert(intensities.size() == ranges.size());
            message.intensityWidth = width;
            kArray1_Construct(message.intensities.Ref(), kTypeOf(k8u), width, kNULL);
            std::memcpy(kArray1_DataT(message.intensities.Get(), k8u), intensities.data(),
                intensities.size() * sizeof(k8u));
        }
    }

    static void setProfilePointCloud(GoPxLSdk::GoGdpProfilePointCloud& message,
                                     const k32u width,
                                     const kPoint3d64f& resolution,
                                     const kPoint3d64f& offset,
                                     const std::vector<kPoint16s>& points)
    {
        assert(points.size() == std::size_t(width));
        message.width = width;
        message.resolution = resolution;
        message.offset = offset;
        kArray1_Construct(message.ranges.Ref(), kTypeOf(kPoint16s), width, kNULL);
        std::memcpy(kArray1_DataT(message.ranges.Get(), kPoint16s), points.data(),
            points.size() * sizeof(kPoint16s));
    }
};

namespace {

void expectArray(const GraphicsSpan<float>& values,
                 const std::vector<float>& expected,
                 const char* what)
{
    assert(values.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        if (values[index] != expected[index])
        {
            std::fprintf(stderr, "%s[%zu] = %f, expected %f\n", what, index,
                static_cast<double>(values[index]), static_cast<double>(expected[index]));
            assert(false);
        }
    }
}

void expectMask(const GraphicsSpan<std::uint8_t>& values,
                const std::vector<std::uint8_t>& expected)
{
    assert(values.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        assert(values[index] == expected[index]);
    }
}

void expectRange(const RangeFrame& frame,
                 const int width,
                 const int height,
                 const RangeFrameXYCoordinateMode xyMode,
                 const double xScale,
                 const double yScale,
                 const double xOffset,
                 const double yOffset)
{
    assert(frame.isValid());
    assert(frame.width == width);
    assert(frame.height == height);
    assert(frame.lengthUnit == GraphicsLengthUnit::Millimeter);
    assert(frame.xyCoordinateMode == xyMode);
    assert(frame.xScale == xScale);
    assert(frame.yScale == yScale);
    assert(frame.zScale == 1.0);
    assert(frame.xOffset == xOffset);
    assert(frame.yOffset == yOffset);
    assert(frame.zOffset == 0.0);
    assert(frame.sensorType == "LMI Gocator");
    assert(frame.rangeField.id == "gocator.range");
    assert(frame.rangeField.displayName == "Range");
    assert(frame.rangeField.unitSymbol == "mm");
    assert(frame.rangeField.domain == MeasurementValueDomain::Calibrated);
    assert(frame.rangeField.sampleKind == MeasurementSampleKind::GridSample);
    assert(frame.rangeField.bitsPerSample == 16U);
    assert(frame.intensityField.id == "gocator.intensity");
    assert(frame.intensityField.displayName == "Intensity");
    assert(frame.intensityField.unitSymbol.empty());
    assert(frame.intensityField.domain == MeasurementValueDomain::Native);
    assert(frame.intensityField.sampleKind == MeasurementSampleKind::GridSample);
}

std::shared_ptr<GoPxLSdk::GoGdpStamp> stamp(const k64u frameIndex)
{
    auto value = std::make_shared<GoPxLSdk::GoGdpStamp>();
    GoGdpMsgTests::setStamp(*value, frameIndex);
    return value;
}

/** The calibrated surface used by the Playground 3D codec parity fixture. */
void expectCalibratedSurface(const GraphicsFrame& converted, const bool withImage = false)
{
    assert(converted.ranges().size() == 1U);
    assert(withImage ? converted.images().size() == 1U : converted.images().empty());
    assert(converted.pointClouds().empty());
    assert(converted.surfaces().empty());
    assert(converted.ranges().front().descriptor.id == "range");
    assert(converted.ranges().front().descriptor.displayName == "Range");
    const RangeFrame& range = converted.ranges().front().payload;
    expectRange(range, 3, 2, RangeFrameXYCoordinateMode::UniformPhysical, 0.5, 0.25, 10.0, 20.0);
    expectArray(range.zValues, {1.0F, 3.0F, 0.0F, 7.0F, 0.0F, 11.0F}, "surface Z");
    expectMask(range.validMask, {1U, 1U, 0U, 1U, 0U, 1U});
    expectArray(range.intensity, {10.0F, 20.0F, 30.0F, 40.0F, 50.0F, 60.0F}, "surface intensity");
    assert(range.intensityBits == 16U);
    assert(range.intensityField.bitsPerSample == 16U);
    assert(range.rangeBits == 0U);
    assert(range.frameId == "42");
    assert(converted.metadata().sourceName == "LMI Gocator");
    assert(converted.metadata().frameId == "42");
    assert(converted.metadata().frameIndex == 42U);
}

} // namespace

int main()
{
    // Builds the kApi type and allocator assembly the synthetic message arrays need.
    gocator::GocatorSdkRuntime::ensureInitialized();
    GoPxLSdk::GoDataSet empty;
    GocatorDataSetGraphicsFrameAdapter adapter;

    GraphicsFrameRequest rangeRequest;
    rangeRequest.components = GraphicsFrameComponent::Range;
    rangeRequest.includeRangeAuxiliaryChannels = true;
    assert(!adapter.convertFrame(empty, rangeRequest).has_value());
    GocatorRangeRejection rejection;
    using Rejection = GocatorRangeRejection::Reason;
    assert(!adapter.convertFrameWithRejection(empty, rangeRequest, rejection).has_value());
    assert(rejection.reason == Rejection::MissingRange);

    GraphicsFrameRequest pointCloudOnly;
    pointCloudOnly.components = GraphicsFrameComponent::PointCloud;
    assert(!adapter.convertFrame(empty, pointCloudOnly).has_value());
    assert(!adapter.convertFrameWithRejection(empty, pointCloudOnly, rejection).has_value());
    assert(rejection.reason == Rejection::None);

    {
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(42));
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            std::vector<k8u>{0, 64, 128, 255});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        const auto converted = adapter.convertFrameWithRejection(dataSet, imageRequest, rejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().empty());
        const auto& result = converted->images().front().payload;
        assert(result.isValid());
        assert(result.width == 2 && result.height == 2);
        assert(result.pixelFormat == GraphicsImagePixelFormat::Mono8);
        assert(result.pixels.size() == 4U);
        assert(result.data()[0] == 0U && result.data()[1] == 64U
            && result.data()[2] == 128U && result.data()[3] == 255U);
        assert(converted->metadata().frameId == "42");
        assert(rejection.reason == Rejection::None);
        assert(!rejection.rangeRetained);
    }

    {
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 2, 1, GoPxLSdk::GoGdpPixelFormat::Mono16,
            std::vector<k16u>{256, 4096});
        GoGdpMsgTests::setDataSetId(*image, 9U);
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        const auto converted = adapter.convertFrame(dataSet, imageRequest);
        assert(converted && converted->images().size() == 1U);
        const auto& result = converted->images().front().payload;
        assert(result.pixelFormat == GraphicsImagePixelFormat::Mono16);
        assert(result.pixels.size() == 4U && storage.calls == 1U && storage.bytes == 4U);
        const auto* samples = reinterpret_cast<const std::uint8_t*>(result.data());
        assert(samples >= storage.block.get() && samples < storage.block.get() + storage.bytes);
        assert(converted->metadata().frameId == "9");
        adapter.setFrameStorageProvider({});
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::BayerGR10,
            std::vector<k16u>{0, 64, 128, 1023});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        const auto converted = adapter.convertFrame(dataSet, imageRequest);
        assert(converted && converted->images().size() == 1U);
        const auto& result = converted->images().front().payload;
        assert(result.pixelFormat == GraphicsImagePixelFormat::BayerGR10);
        assert(result.bitAlignment == GraphicsImageBitAlignment::LeastSignificant);
        assert(result.stride == 4 && result.pixels.size() == 8U);
        const auto* samples = reinterpret_cast<const std::uint16_t*>(result.data());
        assert((samples[0] & 0x3FFU) == 0U);
        assert((samples[1] & 0x3FFU) == 64U);
        assert((samples[2] & 0x3FFU) == 128U);
        assert((samples[3] & 0x3FFU) == 1023U);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 1, GoPxLSdk::GoGdpPixelFormat::Mono12p,
            2U, 3U, std::vector<k8u>{0x23, 0xC1, 0xAB});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        const auto converted = adapter.convertFrame(dataSet, imageRequest);
        assert(converted && converted->images().size() == 1U);
        const auto& result = converted->images().front().payload;
        assert(result.pixelFormat == GraphicsImagePixelFormat::Mono12Packed);
        assert(result.bitAlignment == GraphicsImageBitAlignment::Packed);
        assert(result.stride == 3 && result.pixels.size() == 3U);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 1, GoPxLSdk::GoGdpPixelFormat::Mono12Packed,
            2U, 3U, std::vector<k8u>{0x23, 0xC1, 0xAB});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        assert(!adapter.convertFrame(dataSet, imageRequest));
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            1U, 3U, std::vector<k8u>{1, 2, 0, 3, 4, 0});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        const auto converted = adapter.convertFrame(dataSet, imageRequest);
        assert(converted && converted->images().size() == 1U);
        const auto& result = converted->images().front().payload;
        assert(result.stride == 3 && result.pixels.size() == 6U);
        assert(result.data()[0] == 1U && result.data()[1] == 2U);
        assert(result.data()[3] == 3U && result.data()[4] == 4U);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
            2U, 1U, std::vector<k8u>{255});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        assert(!adapter.convertFrame(dataSet, imageRequest));
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 3, 1, GoPxLSdk::GoGdpPixelFormat::YCbCr422_8,
            2U, 6U, std::vector<k8u>{0, 0, 0, 0, 0, 0});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        assert(!adapter.convertFrame(dataSet, imageRequest));
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Null_Format,
            std::vector<k8u>{1});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        assert(!adapter.convertFrame(dataSet, imageRequest));
    }

    {
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(7));
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
            std::vector<k8u>{255});
        dataSet.Add(image);
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 1, 1, {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0},
            {1}, {});
        dataSet.Add(surface);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        const auto converted = adapter.convertFrameWithRejection(dataSet, allRequest, rejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().size() == 1U);
        assert(converted->metadata().frameId == "7");
        assert(converted->ranges().front().descriptor.groupId == "primary");
        assert(converted->images().front().descriptor.groupId == "primary");
        assert(storage.calls == 1U && storage.singleCalls == 0U);
        assert(storage.shape.images.size() == 1U && storage.shape.ranges.size() == 1U);
        assert(storage.shape.metadata.frameId == "7");
        assert(reinterpret_cast<const std::uint8_t*>(converted->images().front().payload.data())
            == storage.block.get() + storage.plan.images[0].offset);
        assert(reinterpret_cast<const std::uint8_t*>(
            converted->ranges().front().payload.zValues.data())
            == storage.block.get() + storage.plan.ranges[0].offset);
        assert(converted->images().front().payload.pixels.allocation()->state() == GraphicsAllocationState::Published);
        assert(converted->ranges().front().payload.zValues.allocation()->state() == GraphicsAllocationState::Published);
        auto mismatchContext = frameStoragePlanContext(storage.plan);
        GraphicsFrameBuilder mismatchBuilder(mismatchContext);
        auto mismatch = mismatchBuilder.reserve<std::uint8_t>(storage.plan.images[0].size + 1U);
        assert(!mismatch && mismatch.error() == GraphicsFrameError::Allocation);
        assert(rejection.reason == Rejection::None && rejection.rangeRetained
            && rejection.imageRetained);
        adapter.setFrameStorageProvider({});
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
            std::vector<k8u>{255});
        dataSet.Add(image);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        GocatorImageRejection imageRejection;
        const auto converted = adapter.convertFrameWithRejection(
            dataSet, allRequest, rejection, &imageRejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().empty());
        assert(rejection.reason == Rejection::MissingRange);
        assert(rejection.frameRetained && rejection.imageRetained && !rejection.rangeRetained);
        assert(imageRejection.reason == GraphicsImageFailure::None && imageRejection.imageRetained);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 1, GoPxLSdk::GoGdpPixelFormat::Mono12Packed,
            2U, 3U, std::vector<k8u>{0x23, 0xC1, 0xAB});
        dataSet.Add(image);
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 1, 1, {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0},
            {1}, {});
        dataSet.Add(surface);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        const auto converted = adapter.convertFrameWithRejection(dataSet, allRequest, rejection);
        assert(converted && converted->images().empty() && converted->ranges().size() == 1U);
        assert(rejection.reason == Rejection::None && rejection.rangeRetained);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Null_Format,
            std::vector<k8u>{1});
        dataSet.Add(image);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        GocatorImageRejection imageRejection;
        assert(!adapter.convertFrameWithRejection(
            dataSet, allRequest, rejection, &imageRejection).has_value());
        assert(rejection.reason == Rejection::MissingRange && !rejection.frameRetained);
        assert(imageRejection.reason == GraphicsImageFailure::UnsupportedFormat);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            1, 1, {10, 20});
        dataSet.Add(image);
        GraphicsFrameRequest request;
        request.components = GraphicsFrameComponent::Image;
        GocatorImageRejection evidence;
        assert(!adapter.convertFrameWithRejection(dataSet, request, rejection, &evidence));
        assert(evidence.reason == GraphicsImageFailure::StrideBelowMinimum);
        assert(evidence.reportedStride == 1U && evidence.minimumRowBytes == 2U);
        assert(evidence.readableBytes == 2U && evidence.requiredBytes == 4U);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            1, 3, {10, 20, 0, 30, 40, 0});
        GoGdpMsgTests::setImageHeight(*image, 1);
        dataSet.Add(image);
        GraphicsFrameRequest request;
        request.components = GraphicsFrameComponent::Image;
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        const auto frame = adapter.convertFrame(dataSet, request);
        assert(frame && frame->images().size() == 1U);
        assert(frame->images().front().payload.pixels.size() == 3U);
        assert(storage.calls == 1U && storage.bytes == 3U);
        adapter.setFrameStorageProvider({});
    }

    {
        auto refusing = std::make_shared<RecordingProvider>();
        refusing->refuse = true;
        adapter.setFrameStorageProvider(refusing);
        GoPxLSdk::GoDataSet dataSet;
        for (int index = 0; index < 2; ++index) {
            auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
            GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
                std::vector<k8u>{255});
            dataSet.Add(image);
        }
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        GocatorImageRejection imageRejection;
        assert(!adapter.convertFrameWithRejection(
            dataSet, allRequest, rejection, &imageRejection).has_value());
        assert(refusing->calls == 1U && refusing->singleCalls == 0U);
        assert(imageRejection.reason == GraphicsImageFailure::StorageRefused);
        adapter.setFrameStorageProvider({});
    }

    {
        auto refusing = std::make_shared<RecordingProvider>();
        refusing->refuse = true;
        adapter.setFrameStorageProvider(refusing);
        GoPxLSdk::GoDataSet dataSet;
        for (int index = 0; index < 2; ++index) {
            auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
            GoGdpMsgTests::setUniformSurface(*surface, 1, 1,
                {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1}, {});
            dataSet.Add(surface);
        }
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
            std::vector<k8u>{255});
        dataSet.Add(image);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        assert(!adapter.convertFrameWithRejection(dataSet, allRequest, rejection).has_value());
        assert(refusing->calls == 1U && refusing->singleCalls == 0U);
        assert(rejection.reason == Rejection::StorageRefused);
        adapter.setFrameStorageProvider({});
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 2, 1,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2}, {});
        GoGdpMsgTests::setSurfaceWidth(*surface, 3);
        dataSet.Add(surface);
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
            std::vector<k8u>{255});
        dataSet.Add(image);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        assert(!adapter.convertFrameWithRejection(dataSet, allRequest, rejection).has_value());
        assert(rejection.reason == Rejection::TruncatedData);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto shortSurface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        auto validSurface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        for (const auto& surface : {shortSurface, validSurface}) {
            GoGdpMsgTests::setUniformSurface(*surface, 2, 1,
                {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2}, {});
        }
        GoGdpMsgTests::setSurfaceWidth(*shortSurface, 3);
        dataSet.Add(shortSurface);
        dataSet.Add(validSurface);
        const auto converted = adapter.convertFrameWithRejection(dataSet, rangeRequest, rejection);
        assert(converted && converted->ranges().size() == 1U);
        assert(rejection.reason == Rejection::TruncatedData);
        assert(rejection.partCount == 2U && rejection.part == 0U);
        assert(rejection.width == 3U && rejection.height == 1U);
        assert(rejection.rangeRetained);
    }

    // Synthetic uniform surface with a stamp, auxiliary intensity, null sensor
    // codes and calibration. The adapter must keep every field and store the
    // samples exactly once through the provided storage.
    {
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(42));
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 3, 2,
            {0.5, 0.25, 2.0}, {10.0, 20.0, -1.0},
            {1, 2, gocator_range::kNullSample, 4, gocator_range::kNullSample, 6},
            {10, 20, 30, 40, 50, 60});
        dataSet.Add(surface);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest);
        assert(converted.has_value());
        expectCalibratedSurface(*converted);
        assert(storage.calls == 1U);
        const auto* samples = reinterpret_cast<const std::uint8_t*>(
            converted->ranges().front().payload.zValues.data());
        assert(samples >= storage.block.get() && samples < storage.block.get() + storage.bytes);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 2, 2,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2, 3, 4}, {});
        GoGdpMsgTests::setSurfaceWidth(*surface, 1);
        dataSet.Add(surface);
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        assert(!adapter.convertFrameWithRejection(dataSet, rangeRequest, rejection));
        assert(rejection.reason == Rejection::InvalidLayout);
        assert(storage.calls == 0U);
        adapter.setFrameStorageProvider({});
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 1, 2,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2}, {10, 20, 30, 40}, 2, 2);
        dataSet.Add(surface);
        const auto frame = adapter.convertFrame(dataSet, rangeRequest);
        assert(frame && frame->ranges().size() == 1U);
        assert(frame->ranges().front().payload.intensity.empty());
    }

    // The same input without provided storage still converts, through the
    // adapter's own block, so an in-process call is not tied to the Host pool.
    {
        adapter.setFrameStorageProvider({});
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(42));
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 3, 2,
            {0.5, 0.25, 2.0}, {10.0, 20.0, -1.0},
            {1, 2, gocator_range::kNullSample, 4, gocator_range::kNullSample, 6},
            {10, 20, 30, 40, 50, 60});
        dataSet.Add(surface);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest);
        assert(converted.has_value());
        expectCalibratedSurface(*converted);
    }

    // Uniform surface/profile conversion: null sensor codes stay zero and set
    // the mask; valid codes keep the adapter's resolution/offset math.
    {
        GraphicsRangeFrameLayout layout;
        layout.width = 2;
        layout.height = 2;
        layout.channels.validMask = true;
        auto storageHolder = std::make_shared<RecordingStorage>();
        auto& storage = *storageHolder;
        auto context = frameStorageContext(storageHolder);
        GraphicsFrameBuilder builder(context);
        GraphicsRangeFrameWriter writer(builder, layout);
        assert(writer.isValid());
        assert(storage.calls == 1);
        assert(storage.bytes == writer.blockBytes());

        const std::int16_t samples[] = {1, 2, gocator_range::kNullSample, 4};
        assert(gocator_range::scalarHasInvalid(samples, 4));
        gocator_range::writeScalarZ(samples, 4, 0.5, -1.0,
            writer.zData(), writer.validMaskData());
        const RangeFrame frame = writer.takeFrame();
        assert(frame.isValid());
        assert(frame.zValues.data() == reinterpret_cast<const float*>(storage.block.get()));
        assert(frame.validMask.data() != nullptr);
        assert(frame.zValues.data() == reinterpret_cast<const float*>(storage.block.get()));
        assert(frame.zValues[0] == -0.5F);
        assert(frame.zValues[1] == 0.0F);
        assert(frame.zValues[2] == 0.0F);
        assert(frame.zValues[3] == 1.0F);
        assert(frame.validMask[0] == 1U);
        assert(frame.validMask[1] == 1U);
        assert(frame.validMask[2] == 0U);
        assert(frame.validMask[3] == 1U);
        assert(frame.intensity.empty());
    }

    // Surface point cloud conversion keeps per-axis calibration and clears the
    // whole tuple when any component is null.
    {
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(7));
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfacePointCloud>();
        GoGdpMsgTests::setSurfacePointCloud(*surface, 2, 1,
            {0.5, 0.25, 2.0}, {10.0, 20.0, -1.0},
            {{2, 3, 4}, {gocator_range::kNullSample, 1, 2}},
            {7, 8});
        dataSet.Add(surface);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest);
        assert(converted.has_value());
        const RangeFrame& frame = converted->ranges().front().payload;
        expectRange(frame, 2, 1, RangeFrameXYCoordinateMode::ExplicitXY, 0.5, 0.25, 10.0, 20.0);
        expectArray(frame.xValues, {11.0F, 0.0F}, "surface X");
        expectArray(frame.yValues, {20.75F, 0.0F}, "surface Y");
        expectArray(frame.zValues, {7.0F, 0.0F}, "surface Z");
        expectMask(frame.validMask, {1U, 0U});
        expectArray(frame.intensity, {7.0F, 8.0F}, "surface intensity");
        assert(frame.frameId == "7");
        assert(storage.calls == 1U);
    }

    // Profile point clouds map the second sample to Z and zero the Y channel.
    {
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(77));
        auto profile = std::make_shared<GoPxLSdk::GoGdpProfilePointCloud>();
        GoGdpMsgTests::setProfilePointCloud(*profile, 2,
            {2.0, 0.0, 0.5}, {10.0, 0.0, -1.0},
            {{1, 2}, {gocator_range::kNullSample, 3}});
        dataSet.Add(profile);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest);
        assert(converted.has_value());
        const RangeFrame& frame = converted->ranges().front().payload;
        expectRange(frame, 2, 1, RangeFrameXYCoordinateMode::ExplicitXY, 2.0, 1.0, 10.0, 0.0);
        expectArray(frame.xValues, {12.0F, 0.0F}, "profile X");
        expectArray(frame.yValues, {0.0F, 0.0F}, "profile Y");
        expectArray(frame.zValues, {0.0F, 0.0F}, "profile Z");
        expectMask(frame.validMask, {1U, 0U});
        assert(frame.intensity.empty());
        assert(frame.frameId == "77");
        assert(storage.calls == 1U);
    }

    // Uniform profiles use a single row, eight-bit intensity and the dataset id
    // as the frame identity when the sensor sends no stamp.
    {
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        GoPxLSdk::GoDataSet dataSet;
        auto profile = std::make_shared<GoPxLSdk::GoGdpProfileUniform>();
        GoGdpMsgTests::setDataSetId(*profile, 9U);
        GoGdpMsgTests::setUniformProfile(*profile, 3,
            {0.0, 0.0, 0.5}, {0.0, 0.0, -1.0},
            {5, gocator_range::kNullSample, 7},
            {1, 2, 3});
        dataSet.Add(profile);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest);
        assert(converted.has_value());
        const RangeFrame& frame = converted->ranges().front().payload;
        expectRange(frame, 3, 1, RangeFrameXYCoordinateMode::UniformPhysical, 1.0, 1.0, 0.0, 0.0);
        expectArray(frame.zValues, {1.5F, 0.0F, 2.5F}, "profile Z");
        expectMask(frame.validMask, {1U, 0U, 1U});
        expectArray(frame.intensity, {1.0F, 2.0F, 3.0F}, "profile intensity");
        assert(frame.intensityBits == 8U);
        assert(frame.intensityField.bitsPerSample == 8U);
        assert(frame.frameId == "9");
        assert(converted->metadata().frameIndex == 9U);
        assert(storage.calls == 1U);
    }

    // A refused provided-storage request drops the frame instead of falling
    // back to an adapter-owned block.
    {
        auto refusing = std::make_shared<RecordingProvider>();
        refusing->refuse = true;
        adapter.setFrameStorageProvider(refusing);
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(1));
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 2, 1,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2}, {});
        dataSet.Add(surface);
        assert(!adapter.convertFrameWithRejection(dataSet, rangeRequest, rejection).has_value());
        assert(rejection.reason == Rejection::StorageRefused);
        assert(rejection.part == 1U && rejection.requestedStorageBytes > 0U);
        assert(!rejection.rangeRetained);
        adapter.setFrameStorageProvider({});
    }

    // A range plus a valid image share one planned lease; the shape carries exactly the fields of
    // the frame that is then published.
    {
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(42));
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            std::vector<k8u>{0, 64, 128, 255});
        dataSet.Add(image);
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 3, 2,
            {0.5, 0.25, 2.0}, {10.0, 20.0, -1.0},
            {1, 2, gocator_range::kNullSample, 4, gocator_range::kNullSample, 6},
            {10, 20, 30, 40, 50, 60});
        dataSet.Add(surface);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        allRequest.includeRangeAuxiliaryChannels = true;
        GocatorImageRejection imageRejection;
        const auto converted = adapter.convertFrameWithRejection(
            dataSet, allRequest, rejection, &imageRejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().size() == 1U);
        assert(storage.calls == 1U && storage.singleCalls == 0U);
        assert(storage.plan.images.size() == 1U && storage.plan.ranges.size() == 1U);
        assert(storage.plan.ranges[0].offset >= storage.plan.images[0].offset + storage.plan.images[0].size);
        const auto& planned = converted->ranges().front();
        const auto& plannedImage = converted->images().front();
        assert(reinterpret_cast<const std::uint8_t*>(plannedImage.payload.data())
            == storage.block.get() + storage.plan.images[0].offset);
        assert(reinterpret_cast<const std::uint8_t*>(planned.payload.zValues.data())
            == storage.block.get() + storage.plan.ranges[0].offset);
        assert(plannedImage.payload.data()[3] == 255U);
        expectCalibratedSurface(*converted, true);
        const auto& imageShape = storage.shape.images.front();
        assert(imageShape.descriptor.id == plannedImage.descriptor.id
            && imageShape.descriptor.displayName == plannedImage.descriptor.displayName
            && imageShape.descriptor.groupId == plannedImage.descriptor.groupId
            && imageShape.descriptor.registration == plannedImage.descriptor.registration);
        assert(imageShape.width == plannedImage.payload.width && imageShape.height == plannedImage.payload.height
            && imageShape.stride == plannedImage.payload.stride && imageShape.size == plannedImage.payload.pixels.size()
            && imageShape.pixelFormat == plannedImage.payload.pixelFormat
            && imageShape.frameSequence == plannedImage.payload.frameSequence);
        const auto& rangeShape = storage.shape.ranges.front();
        const auto& finalRange = planned.payload;
        assert(rangeShape.descriptor.id == planned.descriptor.id
            && rangeShape.descriptor.displayName == planned.descriptor.displayName
            && rangeShape.descriptor.groupId == planned.descriptor.groupId);
        assert(rangeShape.layout.width == finalRange.width && rangeShape.layout.height == finalRange.height);
        assert(rangeShape.layout.channels.intensity == !finalRange.intensity.empty()
            && rangeShape.layout.channels.validMask == !finalRange.validMask.empty());
        assert(rangeShape.xScale == finalRange.xScale && rangeShape.yScale == finalRange.yScale
            && rangeShape.zScale == finalRange.zScale && rangeShape.xOffset == finalRange.xOffset
            && rangeShape.yOffset == finalRange.yOffset && rangeShape.zOffset == finalRange.zOffset);
        assert(rangeShape.xyCoordinateMode == finalRange.xyCoordinateMode
            && rangeShape.intensityBits == finalRange.intensityBits
            && rangeShape.rangeField.id == finalRange.rangeField.id
            && rangeShape.intensityField.bitsPerSample == finalRange.intensityField.bitsPerSample
            && rangeShape.frameId == finalRange.frameId && rangeShape.sensorType == finalRange.sensorType);
        assert(storage.shape.metadata.frameId == converted->metadata().frameId
            && storage.shape.metadata.frameIndex == converted->metadata().frameIndex
            && storage.shape.metadata.sourceName == converted->metadata().sourceName);
        assert(rejection.reason == Rejection::None && rejection.rangeRetained && rejection.imageRetained);
        assert(imageRejection.reason == GraphicsImageFailure::None);
        adapter.setFrameStorageProvider({});
    }

    // An invalid image leaves a range-only frame and is still reported; the lease covers the range.
    {
        auto storageHolder = std::make_shared<RecordingProvider>();
        auto& storage = *storageHolder;
        adapter.setFrameStorageProvider(storageHolder);
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            1, 1, {10, 20});
        dataSet.Add(image);
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 1, 1,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1}, {});
        dataSet.Add(surface);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        GocatorImageRejection imageRejection;
        const auto converted = adapter.convertFrameWithRejection(
            dataSet, allRequest, rejection, &imageRejection);
        assert(converted && converted->images().empty() && converted->ranges().size() == 1U);
        assert(storage.calls == 1U && storage.shape.images.empty() && storage.shape.ranges.size() == 1U);
        assert(imageRejection.reason == GraphicsImageFailure::StrideBelowMinimum);
        assert(rejection.rangeRetained && !rejection.imageRetained);
        adapter.setFrameStorageProvider({});
    }

    // A refused plan for a range plus image frame drops the whole frame with one provider call.
    {
        auto refusing = std::make_shared<RecordingProvider>();
        refusing->refuse = true;
        adapter.setFrameStorageProvider(refusing);
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
            std::vector<k8u>{255});
        dataSet.Add(image);
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 1, 1,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1}, {});
        dataSet.Add(surface);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        assert(!adapter.convertFrameWithRejection(dataSet, allRequest, rejection).has_value());
        assert(refusing->calls == 1U && refusing->singleCalls == 0U);
        assert(rejection.reason == Rejection::StorageRefused && rejection.requestedStorageBytes > 0U);
        assert(!rejection.rangeRetained && !rejection.imageRetained);
        adapter.setFrameStorageProvider({});
    }

    // The planned image shape equals what the image writer produces for every operation.
    {
        std::vector<std::uint8_t> bytes(64U, 7U);
        const auto check = [&](GraphicsImageWriterSource source) {
            source.data = bytes.data();
            const auto shape = frameStorageImageShape({}, std::nullopt, source);
            GraphicsFrameBuilder builder;
            const auto written = writeGraphicsImage(builder, source);
            assert(shape.has_value() == written.isValid());
            if (!shape) return;
            assert(shape->width == written.width && shape->height == written.height);
            assert(shape->stride == written.stride && shape->size == written.pixels.size());
            assert(shape->pixelFormat == written.pixelFormat);
        };
        GraphicsImageWriterSource mono8;
        mono8.size = 8U; mono8.width = 4; mono8.height = 2; mono8.stride = 4;
        mono8.pixelFormat = GraphicsImagePixelFormat::Mono8; mono8.bitsPerPixel = 8;
        check(mono8);
        mono8.stride = 0;
        check(mono8);
        mono8.stride = 3;
        check(mono8);
        GraphicsImageWriterSource mono16;
        mono16.size = 16U; mono16.width = 4; mono16.height = 2; mono16.stride = 8;
        mono16.pixelFormat = GraphicsImagePixelFormat::Mono16; mono16.bitsPerPixel = 16;
        check(mono16);
        mono16.operation = GraphicsImageWriterOperation::NarrowMono16To8;
        check(mono16);
        mono16.operation = GraphicsImageWriterOperation::SwapByteOrder;
        check(mono16);
        mono16.operation = GraphicsImageWriterOperation::ShiftMono16;
        mono16.declaredMonoSourceBits = 12;
        check(mono16);
        mono16.size = 8U;
        check(mono16);
    }

    // All-valid input omits the mask, matching the previous clear-on-valid rule.
    {
        GraphicsRangeFrameLayout layout;
        layout.width = 2;
        layout.height = 1;
        const std::int16_t samples[] = {5, 6};
        layout.channels.validMask = gocator_range::scalarHasInvalid(samples, 2);
        GraphicsFrameBuilder builder;
        GraphicsRangeFrameWriter writer(builder, layout);
        assert(writer.isValid());
        gocator_range::writeScalarZ(samples, 2, 1.0, 0.0,
            writer.zData(), writer.validMaskData());
        const RangeFrame frame = writer.takeFrame();
        assert(frame.validMask.empty());
        assert(frame.zValues[0] == 5.0F);
        assert(frame.zValues[1] == 6.0F);
    }

    // Provided storage refusal drops the frame instead of falling back.
    {
        GraphicsRangeFrameLayout layout;
        layout.width = 2;
        layout.height = 1;
        auto provider = std::make_shared<RecordingProvider>();
        auto context = frameStorageContext(provider);
        GraphicsFrameBuilder builder(context);
        GraphicsRangeFrameWriter writer(builder, layout);
        assert(!writer.isValid());
        assert(!writer.takeFrame().isValid());
    }

    return 0;
}
