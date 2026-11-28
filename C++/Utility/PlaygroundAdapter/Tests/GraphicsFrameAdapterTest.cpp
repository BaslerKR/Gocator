// This suite checks its results with assert(); Release configurations must not compile them out.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "GocatorDataSetGraphicsFrameAdapter.h"
#include "GocatorRangeConversion.h"
#include "Internal/GocatorSdkRuntime.h"

#include "engine/detail/GraphicsWriteLayout.h"
#include "engine/GraphicsFrameBuilder.h"
#include "engine/GraphicsFrameContext.h"
#include "engine/GraphicsImageWriter.h"
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

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<const GraphicsFrame&>().images().front())>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(*std::declval<GraphicsSpan<std::uint8_t>>().data())>>);

/** Managed test backing that tracks live references and enforces exact sample limits. */
class TrackedManagedAllocation final : public GraphicsAllocation
{
public:
    [[nodiscard]] static GraphicsAllocation* create(
        const std::size_t capacityBytes,
        const std::uint64_t domainId,
        std::atomic<std::size_t>* const liveCount) noexcept
    {
        auto* rawBuffer = new (std::nothrow) std::byte[capacityBytes + 64U];
        if (!rawBuffer) return nullptr;
        const auto raw = reinterpret_cast<std::uintptr_t>(rawBuffer);
        auto* aligned = reinterpret_cast<std::byte*>((raw + 63U) & ~std::uintptr_t(63U));
        // Fresh storage is poisoned so a test can tell zero-filled bytes from untouched ones.
        std::memset(aligned, 0xAB, capacityBytes);
        auto* alloc = new (std::nothrow) TrackedManagedAllocation(
            rawBuffer, aligned, capacityBytes, domainId, liveCount);
        if (!alloc) delete[] rawBuffer;
        return alloc;
    }

    ~TrackedManagedAllocation() override
    {
        if (_liveCount) _liveCount->fetch_sub(1U, std::memory_order_relaxed);
    }

    void destroy() noexcept override
    {
        delete this;
    }

private:
    TrackedManagedAllocation(
        std::byte* rawBuffer,
        std::byte* alignedBase,
        std::size_t capacityBytes,
        std::uint64_t domainId,
        std::atomic<std::size_t>* liveCount) noexcept
        : GraphicsAllocation(alignedBase, capacityBytes, GraphicsAllocationOrigin::Managed, 1U,
                             GraphicsAllocationState::Writable, domainId)
        , _buffer(rawBuffer)
        , _liveCount(liveCount)
    {
        if (_liveCount) _liveCount->fetch_add(1U, std::memory_order_relaxed);
    }

    std::unique_ptr<std::byte[]> _buffer;
    std::atomic<std::size_t>* _liveCount = nullptr;
};

class TrackedFrameAllocator final : public GraphicsFrameAllocator
{
public:
    TrackedFrameAllocator(
        GraphicsAllocationRef backing,
        std::size_t sampleLimit,
        std::uint64_t domainId,
        std::atomic<std::size_t>* reserveCalls,
        std::atomic<std::size_t>* usedBytes)
        : _backing(std::move(backing))
        , _sampleLimit(sampleLimit)
        , _domainId(domainId)
        , _reserveCalls(reserveCalls)
        , _usedBytes(usedBytes)
    {
    }

    GraphicsResult<GraphicsAllocationSlice> allocate(
        const std::size_t bytes, const std::size_t alignment) noexcept override
    {
        if (_reserveCalls) _reserveCalls->fetch_add(1U, std::memory_order_relaxed);
        if (alignment == 0U || (alignment & (alignment - 1U)) != 0U) {
            return GraphicsFrameError::InvalidInput;
        }
        const auto baseAddr = reinterpret_cast<std::uintptr_t>(_backing->base());
        const auto curAddr = baseAddr + static_cast<std::uintptr_t>(_cursor);
        const std::size_t alignMask = alignment - 1U;
        const std::size_t misalignment = static_cast<std::size_t>(curAddr & alignMask);
        const std::size_t pad = (misalignment == 0U) ? 0U : (alignment - misalignment);
        if (pad > _sampleLimit || _cursor + pad > _sampleLimit
            || bytes > _sampleLimit - (_cursor + pad)) {
            return GraphicsFrameError::Capacity;
        }
        const std::size_t offset = _cursor + pad;
        _cursor = offset + bytes;
        if (_usedBytes) _usedBytes->store(_cursor, std::memory_order_relaxed);
        return GraphicsAllocationSlice{_backing, offset, bytes};
    }

    bool isManaged() const noexcept override { return true; }
    std::uint64_t domainId() const noexcept override { return _domainId; }

private:
    GraphicsAllocationRef _backing;
    std::size_t _sampleLimit = 0U;
    std::uint64_t _domainId = 0U;
    std::size_t _cursor = 0U;
    std::atomic<std::size_t>* _reserveCalls = nullptr;
    std::atomic<std::size_t>* _usedBytes = nullptr;
};

/** Test context source mirroring WorkerFrameContextSource + FrameScopeAllocator invariants. */
class TrackedContextSource final : public GraphicsFrameContextSource
{
public:
    static constexpr std::uint64_t kDomainId = 7001U;

    std::atomic<std::size_t> calls{0};
    std::atomic<std::size_t> reserveCalls{0};
    std::atomic<std::size_t> requestedBytes{0};
    std::atomic<std::size_t> usedBytes{0};
    std::atomic<std::size_t> liveAllocations{0};
    GraphicsFrameError nextError = GraphicsFrameError::None;
    std::size_t budgetDelta = 0; // Subtracted from allocated limit to test reserve overflow.
    const std::byte* lastBackingBase = nullptr;
    std::function<void()> onBeginFrame;

    GraphicsResult<GraphicsFrameContext> beginFrame(const std::size_t sampleBytes) noexcept override
    {
        calls.fetch_add(1U, std::memory_order_relaxed);
        requestedBytes.store(sampleBytes, std::memory_order_relaxed);
        usedBytes.store(0U, std::memory_order_relaxed);
        if (onBeginFrame) onBeginFrame();
        if (nextError != GraphicsFrameError::None) {
            return nextError;
        }
        if (sampleBytes == 0U) {
            return GraphicsFrameError::InvalidInput;
        }
        const std::size_t limit = sampleBytes > budgetDelta ? sampleBytes - budgetDelta : 0U;
        auto* rawAlloc = TrackedManagedAllocation::create(limit, kDomainId, &liveAllocations);
        if (!rawAlloc) return GraphicsFrameError::Allocation;
        auto backing = GraphicsAllocationRef::adoptNew(rawAlloc);
        lastBackingBase = backing->base();
        auto allocator = std::make_shared<TrackedFrameAllocator>(
            std::move(backing), limit, kDomainId, &reserveCalls, &usedBytes);
        return GraphicsFrameContext(std::move(allocator));
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

    static void setSurfacePointCloudWidth(GoPxLSdk::GoGdpSurfacePointCloud& message, k32u width)
    {
        message.width = width;
    }

    static void setUniformProfileWidth(GoPxLSdk::GoGdpProfileUniform& message, k32u width)
    {
        message.width = width;
    }

    static void setProfilePointCloudWidth(GoPxLSdk::GoGdpProfilePointCloud& message, k32u width)
    {
        message.width = width;
    }

    /** Range samples of an item size the adapter cannot interpret (four bytes per sample). */
    static void setUniformSurfaceInt32(GoPxLSdk::GoGdpSurfaceUniform& message,
                                       const k32u width,
                                       const k32u height,
                                       const std::vector<k32s>& ranges)
    {
        assert(ranges.size() == std::size_t(width) * std::size_t(height));
        message.width = width;
        message.length = height;
        kArray2_Construct(message.ranges.Ref(), kTypeOf(k32s), height, width, kNULL);
        std::memcpy(kArray2_DataT(message.ranges.Get(), k32s), ranges.data(),
            ranges.size() * sizeof(k32s));
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
                                     const std::vector<kPoint16s>& points,
                                     const std::vector<k8u>& intensities = {})
    {
        assert(points.size() == std::size_t(width));
        message.width = width;
        message.resolution = resolution;
        message.offset = offset;
        kArray1_Construct(message.ranges.Ref(), kTypeOf(kPoint16s), width, kNULL);
        std::memcpy(kArray1_DataT(message.ranges.Get(), kPoint16s), points.data(),
            points.size() * sizeof(kPoint16s));
        if (!intensities.empty())
        {
            assert(intensities.size() == points.size());
            message.intensityWidth = width;
            kArray1_Construct(message.intensities.Ref(), kTypeOf(k8u), width, kNULL);
            std::memcpy(kArray1_DataT(message.intensities.Get(), k8u), intensities.data(),
                intensities.size() * sizeof(k8u));
        }
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
    GraphicsLocalFrameContextSource localSource;

    GraphicsFrameRequest rangeRequest;
    rangeRequest.components = GraphicsFrameComponent::Range;
    rangeRequest.includeRangeAuxiliaryChannels = true;
    assert(!adapter.convertFrame(empty, rangeRequest, localSource).has_value());
    GocatorRangeRejection rejection;
    using Rejection = GocatorRangeRejection::Reason;
    assert(!adapter.convertFrameWithRejection(empty, rangeRequest, localSource, rejection).has_value());
    assert(rejection.reason == Rejection::MissingRange);

    GraphicsFrameRequest pointCloudOnly;
    pointCloudOnly.components = GraphicsFrameComponent::PointCloud;
    assert(!adapter.convertFrame(empty, pointCloudOnly, localSource).has_value());
    assert(!adapter.convertFrameWithRejection(empty, pointCloudOnly, localSource, rejection).has_value());
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
        const auto converted = adapter.convertFrameWithRejection(dataSet, imageRequest, localSource, rejection);
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
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 2, 1, GoPxLSdk::GoGdpPixelFormat::Mono16,
            std::vector<k16u>{256, 4096});
        GoGdpMsgTests::setDataSetId(*image, 9U);
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        const auto converted = adapter.convertFrame(dataSet, imageRequest, storage);
        assert(converted && converted->images().size() == 1U);
        const auto& result = converted->images().front().payload;
        assert(result.pixelFormat == GraphicsImagePixelFormat::Mono16);
        assert(result.pixels.size() == 4U && storage.calls == 1U && storage.reserveCalls == 1U
            && storage.requestedBytes == 4U && storage.usedBytes == 4U);
        const auto* samples = reinterpret_cast<const std::byte*>(result.data());
        assert(samples >= storage.lastBackingBase && samples < storage.lastBackingBase + storage.usedBytes);
        assert(converted->metadata().frameId == "9");
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::BayerGR10,
            std::vector<k16u>{0, 64, 128, 1023});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        const auto converted = adapter.convertFrame(dataSet, imageRequest, localSource);
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
        const auto converted = adapter.convertFrame(dataSet, imageRequest, localSource);
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
        // The GEV packed format maps to the nearest GraphicsEngine format instead of being refused.
        const auto converted = adapter.convertFrame(dataSet, imageRequest, localSource);
        assert(converted && converted->images().size() == 1U);
        const auto& result = converted->images().front().payload;
        assert(result.pixelFormat == GraphicsImagePixelFormat::Mono12Packed);
        assert(result.stride == 3 && result.pixels.size() == 3U);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            1U, 3U, std::vector<k8u>{1, 2, 0, 3, 4, 0});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        const auto converted = adapter.convertFrame(dataSet, imageRequest, localSource);
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
        // A pixel size that disagrees with the format is counted, and the pixel is still published.
        GocatorImageRejection evidence;
        GocatorFrameAnomalies anomalies;
        const auto converted = adapter.convertFrameWithRejection(
            dataSet, imageRequest, localSource, rejection, &evidence, std::nullopt, nullptr, nullptr, &anomalies);
        assert(converted && converted->images().size() == 1U);
        assert(converted->images().front().payload.pixelFormat == GraphicsImagePixelFormat::Mono8);
        assert(converted->images().front().payload.data()[0] == 255U);
        assert(evidence.reason == GraphicsImageFailure::InvalidStride && evidence.imageRetained);
        assert(anomalies.imageCount == 1U && anomalies.rangeCount == 0U);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 3, 1, GoPxLSdk::GoGdpPixelFormat::YCbCr422_8,
            2U, 6U, std::vector<k8u>{1, 2, 3, 4, 5, 6});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        // An odd-width 4:2:2 image has no GraphicsEngine layout; its bytes publish as raw Mono8.
        GocatorImageRejection evidence;
        const auto converted = adapter.convertFrameWithRejection(
            dataSet, imageRequest, localSource, rejection, &evidence);
        assert(converted && converted->images().size() == 1U);
        const auto& result = converted->images().front().payload;
        assert(result.pixelFormat == GraphicsImagePixelFormat::Mono8);
        assert(result.width == 6 && result.height == 1 && result.pixels.size() == 6U);
        assert(result.data()[0] == 1U && result.data()[5] == 6U);
        assert(evidence.reason == GraphicsImageFailure::OddWidth && evidence.imageRetained);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Null_Format,
            std::vector<k8u>{1});
        dataSet.Add(image);
        GraphicsFrameRequest imageRequest;
        imageRequest.components = GraphicsFrameComponent::Image;
        // An unknown pixel format publishes its raw bytes as Mono8.
        const auto converted = adapter.convertFrame(dataSet, imageRequest, localSource);
        assert(converted && converted->images().size() == 1U);
        const auto& result = converted->images().front().payload;
        assert(result.pixelFormat == GraphicsImagePixelFormat::Mono8);
        assert(result.width == 1 && result.height == 1 && result.data()[0] == 1U);
    }

    // Image + uniform surface in one managed builder transaction: alignment padding between
    // image (1 byte) and range (4-byte float aligned) must match tracked usedBytes exactly.
    {
        TrackedContextSource storage;
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
        const auto converted = adapter.convertFrameWithRejection(dataSet, allRequest, storage, rejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().size() == 1U);
        assert(converted->metadata().frameId == "7");
        assert(converted->ranges().front().descriptor.groupId == "primary");
        assert(converted->images().front().descriptor.groupId == "primary");
        assert(converted->images().front().descriptor.registration == GraphicsImageRegistration::Unregistered);
        assert(storage.calls == 1U && storage.reserveCalls == 2U);
        GraphicsImageWriterSource expectedImg;
        expectedImg.data = static_cast<const std::uint8_t*>(kArray2_Data(image->Pixels()));
        expectedImg.size = 1U;
        expectedImg.width = 1;
        expectedImg.height = 1;
        expectedImg.stride = 1;
        expectedImg.pixelFormat = GraphicsImagePixelFormat::Mono8;
        expectedImg.bitsPerPixel = 8;
        const auto expectedImgPlan = planGraphicsImageWrite(expectedImg);
        GraphicsRangeFrameLayout expectedRangeLayout;
        expectedRangeLayout.width = 1;
        expectedRangeLayout.height = 1;
        const auto expectedRangePlan = planGraphicsRangeWrite(expectedRangeLayout);
        assert(expectedImgPlan && expectedRangePlan);
        std::size_t expectedBytes = 0U;
        assert(appendAligned(expectedBytes, expectedImgPlan.value().bytes, expectedImgPlan.value().alignment));
        assert(appendAligned(expectedBytes, expectedRangePlan.value().totalBytes, expectedRangePlan.value().alignment));
        assert(storage.requestedBytes == expectedBytes && storage.usedBytes == expectedBytes);
        assert(converted->images().front().payload.pixels.allocation().get()
            == converted->ranges().front().payload.zValues.allocation().get());
        assert(converted->images().front().payload.pixels.allocation()->state() == GraphicsAllocationState::Published);
        assert(converted->ranges().front().payload.zValues.allocation()->state() == GraphicsAllocationState::Published);
        assert(rejection.reason == Rejection::None && rejection.rangeRetained
            && rejection.imageRetained);
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
            dataSet, allRequest, localSource, rejection, &imageRejection);
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
        const auto converted = adapter.convertFrameWithRejection(dataSet, allRequest, localSource, rejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().size() == 1U);
        assert(converted->images().front().payload.pixelFormat == GraphicsImagePixelFormat::Mono12Packed);
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
        const auto converted = adapter.convertFrameWithRejection(
            dataSet, allRequest, localSource, rejection, &imageRejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().empty());
        assert(rejection.reason == Rejection::MissingRange && rejection.frameRetained);
        assert(imageRejection.reason == GraphicsImageFailure::UnsupportedFormat && imageRejection.imageRetained);
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
        // A narrow source stride is copied row by row into the minimum GraphicsEngine stride.
        const auto frame = adapter.convertFrameWithRejection(dataSet, request, localSource, rejection, &evidence);
        assert(frame && frame->images().size() == 1U);
        const auto& fitted = frame->images().front().payload;
        assert(fitted.stride == 2 && fitted.pixels.size() == 4U);
        assert(fitted.data()[0] == 10U && fitted.data()[1] == 0U);
        assert(fitted.data()[2] == 20U && fitted.data()[3] == 0U);
        assert(evidence.reason == GraphicsImageFailure::StrideBelowMinimum && evidence.imageRetained);
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
        TrackedContextSource storage;
        const auto frame = adapter.convertFrame(dataSet, request, storage);
        assert(frame && frame->images().size() == 1U);
        assert(frame->images().front().payload.pixels.size() == 3U);
        assert(storage.calls == 1U && storage.requestedBytes == 3U && storage.usedBytes == 3U);
    }

    // NoReadyCredit and ClassRefused drop image-only frames before any reserve/copy.
    for (const auto err : {GraphicsFrameError::NoReadyCredit, GraphicsFrameError::ClassRefused,
                           GraphicsFrameError::ClassNotPrepared}) {
        TrackedContextSource refusing;
        refusing.nextError = err;
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
        GraphicsFrameError storageError = GraphicsFrameError::None;
        assert(!adapter.convertFrameWithRejection(
            dataSet, allRequest, refusing, rejection, &imageRejection, std::nullopt, &storageError).has_value());
        assert(refusing.calls == 1U && refusing.reserveCalls == 0U);
        assert(storageError == err);
        assert(imageRejection.reason == GraphicsImageFailure::StorageRefused);
        assert(refusing.liveAllocations == 0U);
    }

    {
        TrackedContextSource refusing;
        refusing.nextError = GraphicsFrameError::ClassRefused;
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
        GraphicsFrameError storageError = GraphicsFrameError::None;
        assert(!adapter.convertFrameWithRejection(
            dataSet, allRequest, refusing, rejection, nullptr, std::nullopt, &storageError).has_value());
        assert(refusing.calls == 1U && refusing.reserveCalls == 0U);
        assert(storageError == GraphicsFrameError::ClassRefused);
        assert(rejection.reason == Rejection::StorageRefused);
        assert(refusing.liveAllocations == 0U);
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
        TrackedContextSource storage;
        // A short range no longer drops the frame: both parts publish and the shortfall is counted.
        const auto converted = adapter.convertFrameWithRejection(dataSet, allRequest, storage, rejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().size() == 1U);
        assert(rejection.reason == Rejection::TruncatedData);
        assert(storage.calls == 1U && storage.usedBytes == storage.requestedBytes);
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
        const auto converted = adapter.convertFrameWithRejection(dataSet, rangeRequest, localSource, rejection);
        assert(converted && converted->ranges().size() == 2U);
        assert(rejection.reason == Rejection::TruncatedData);
        assert(rejection.partCount == 2U && rejection.part == 0U);
        assert(rejection.width == 3U && rejection.height == 1U);
        assert(rejection.rangeRetained);
    }

    // Synthetic uniform surface with a stamp, auxiliary intensity, null sensor
    // codes and calibration. The adapter must keep every field and store the
    // samples once into the managed context.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(42));
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 3, 2,
            {0.5, 0.25, 2.0}, {10.0, 20.0, -1.0},
            {1, 2, gocator_range::kNullSample, 4, gocator_range::kNullSample, 6},
            {10, 20, 30, 40, 50, 60});
        dataSet.Add(surface);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest, storage);
        assert(converted.has_value());
        expectCalibratedSurface(*converted);
        assert(storage.calls == 1U && storage.reserveCalls == 1U && storage.usedBytes == storage.requestedBytes);
        const auto* samples = reinterpret_cast<const std::byte*>(
            converted->ranges().front().payload.zValues.data());
        assert(samples >= storage.lastBackingBase && samples < storage.lastBackingBase + storage.usedBytes);
    }

    // Malformed selected messages across all four codecs are fitted and published after one
    // beginFrame; a message with no sample array at all has no data and publishes nothing.
    {
        TrackedContextSource storage;
        std::size_t expectedCalls = 0U;
        {
            GoPxLSdk::GoDataSet dataSet;
            auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
            GoGdpMsgTests::setUniformSurface(*surface, 2, 2,
                {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2, 3, 4}, {});
            GoGdpMsgTests::setSurfaceWidth(*surface, 1);
            dataSet.Add(surface);
            const auto frame = adapter.convertFrameWithRejection(dataSet, rangeRequest, storage, rejection);
            assert(frame && frame->ranges().size() == 1U);
            assert(rejection.reason == Rejection::InvalidLayout);
            expectArray(frame->ranges().front().payload.zValues, {1.0F, 3.0F}, "narrowed surface Z");
            ++expectedCalls;
        }
        {
            GoPxLSdk::GoDataSet dataSet;
            auto cloud = std::make_shared<GoPxLSdk::GoGdpSurfacePointCloud>();
            GoGdpMsgTests::setSurfacePointCloud(*cloud, 2, 1,
                {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {{1, 2, 3}, {4, 5, 6}});
            GoGdpMsgTests::setSurfacePointCloudWidth(*cloud, 3);
            dataSet.Add(cloud);
            const auto frame = adapter.convertFrameWithRejection(dataSet, rangeRequest, storage, rejection);
            assert(frame && frame->ranges().size() == 1U);
            assert(rejection.reason == Rejection::TruncatedData);
            expectMask(frame->ranges().front().payload.validMask, {1U, 1U, 0U});
            ++expectedCalls;
        }
        {
            GoPxLSdk::GoDataSet dataSet;
            auto profile = std::make_shared<GoPxLSdk::GoGdpProfileUniform>();
            GoGdpMsgTests::setUniformProfile(*profile, 2,
                {1.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2});
            GoGdpMsgTests::setUniformProfileWidth(*profile, 4);
            dataSet.Add(profile);
            const auto frame = adapter.convertFrameWithRejection(dataSet, rangeRequest, storage, rejection);
            assert(frame && frame->ranges().size() == 1U);
            assert(rejection.reason == Rejection::TruncatedData);
            assert(frame->ranges().front().payload.width == 4);
            expectMask(frame->ranges().front().payload.validMask, {1U, 1U, 0U, 0U});
            ++expectedCalls;
        }
        {
            GoPxLSdk::GoDataSet dataSet;
            auto profile = std::make_shared<GoPxLSdk::GoGdpProfilePointCloud>();
            GoGdpMsgTests::setProfilePointCloud(*profile, 2,
                {1.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {{1, 2}, {3, 4}});
            GoGdpMsgTests::setProfilePointCloudWidth(*profile, 4);
            dataSet.Add(profile);
            const auto frame = adapter.convertFrameWithRejection(dataSet, rangeRequest, storage, rejection);
            assert(frame && frame->ranges().size() == 1U);
            assert(rejection.reason == Rejection::TruncatedData);
            expectMask(frame->ranges().front().payload.validMask, {1U, 1U, 0U, 0U});
            ++expectedCalls;
        }
        {
            GoPxLSdk::GoDataSet dataSet;
            dataSet.Add(std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>());
            assert(!adapter.convertFrameWithRejection(dataSet, rangeRequest, storage, rejection));
            assert(rejection.reason == Rejection::InvalidDimensions);
        }
        assert(storage.calls == expectedCalls);
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 1, 2,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2}, {10, 20, 30, 40}, 2, 2);
        dataSet.Add(surface);
        // A differently sized intensity grid is fitted to the range grid and counted.
        const auto frame = adapter.convertFrameWithRejection(dataSet, rangeRequest, localSource, rejection);
        assert(frame && frame->ranges().size() == 1U);
        assert(rejection.reason == Rejection::InvalidLayout);
        expectArray(frame->ranges().front().payload.intensity, {10.0F, 30.0F}, "fitted intensity");
    }

    {
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(42));
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 3, 2,
            {0.5, 0.25, 2.0}, {10.0, 20.0, -1.0},
            {1, 2, gocator_range::kNullSample, 4, gocator_range::kNullSample, 6},
            {10, 20, 30, 40, 50, 60});
        dataSet.Add(surface);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest, localSource);
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
        const auto plan = planGraphicsRangeWrite(layout);
        assert(plan.hasValue());
        TrackedContextSource storage;
        auto ctx = storage.beginFrame(plan.value().totalBytes);
        assert(ctx.hasValue());
        GraphicsFrameBuilder builder(ctx.value());
        GraphicsRangeFrameWriter writer(builder, layout);
        assert(writer.isValid());
        assert(storage.calls == 1U && storage.reserveCalls == 1U);
        assert(storage.usedBytes == writer.blockBytes());

        const std::int16_t samples[] = {1, 2, gocator_range::kNullSample, 4};
        assert(gocator_range::scalarHasInvalid(samples, 4));
        gocator_range::writeScalarZ(samples, 4, 0.5, -1.0,
            writer.zData(), writer.validMaskData());
        const RangeFrame frame = writer.takeFrame();
        assert(frame.isValid());
        assert(frame.zValues.data() == reinterpret_cast<const float*>(storage.lastBackingBase));
        assert(frame.validMask.data() != nullptr);
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
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(7));
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfacePointCloud>();
        GoGdpMsgTests::setSurfacePointCloud(*surface, 2, 1,
            {0.5, 0.25, 2.0}, {10.0, 20.0, -1.0},
            {{2, 3, 4}, {gocator_range::kNullSample, 1, 2}},
            {7, 8});
        dataSet.Add(surface);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest, storage);
        assert(converted.has_value());
        const RangeFrame& frame = converted->ranges().front().payload;
        expectRange(frame, 2, 1, RangeFrameXYCoordinateMode::ExplicitXY, 0.5, 0.25, 10.0, 20.0);
        expectArray(frame.xValues, {11.0F, 0.0F}, "surface X");
        expectArray(frame.yValues, {20.75F, 0.0F}, "surface Y");
        expectArray(frame.zValues, {7.0F, 0.0F}, "surface Z");
        expectMask(frame.validMask, {1U, 0U});
        expectArray(frame.intensity, {7.0F, 8.0F}, "surface intensity");
        assert(frame.frameId == "7");
        assert(storage.calls == 1U && storage.usedBytes == storage.requestedBytes);
    }

    // Profile point clouds map the second sample to Z and zero the Y channel.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(77));
        auto profile = std::make_shared<GoPxLSdk::GoGdpProfilePointCloud>();
        GoGdpMsgTests::setProfilePointCloud(*profile, 2,
            {2.0, 0.0, 0.5}, {10.0, 0.0, -1.0},
            {{1, 2}, {gocator_range::kNullSample, 3}},
            {5, 9});
        dataSet.Add(profile);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest, storage);
        assert(converted.has_value());
        const RangeFrame& frame = converted->ranges().front().payload;
        expectRange(frame, 2, 1, RangeFrameXYCoordinateMode::ExplicitXY, 2.0, 1.0, 10.0, 0.0);
        expectArray(frame.xValues, {12.0F, 0.0F}, "profile X");
        expectArray(frame.yValues, {0.0F, 0.0F}, "profile Y");
        expectArray(frame.zValues, {0.0F, 0.0F}, "profile Z");
        expectMask(frame.validMask, {1U, 0U});
        expectArray(frame.intensity, {5.0F, 9.0F}, "profile cloud intensity");
        assert(frame.intensityBits == 8U);
        assert(frame.frameId == "77");
        assert(storage.calls == 1U && storage.usedBytes == storage.requestedBytes);
    }

    // Uniform profiles use a single row, eight-bit intensity and the dataset id
    // as the frame identity when the sensor sends no stamp.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        auto profile = std::make_shared<GoPxLSdk::GoGdpProfileUniform>();
        GoGdpMsgTests::setDataSetId(*profile, 9U);
        GoGdpMsgTests::setUniformProfile(*profile, 3,
            {0.0, 0.0, 0.5}, {0.0, 0.0, -1.0},
            {5, gocator_range::kNullSample, 7},
            {1, 2, 3});
        dataSet.Add(profile);
        const auto converted = adapter.convertFrame(dataSet, rangeRequest, storage);
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
        assert(storage.calls == 1U && storage.usedBytes == storage.requestedBytes);
    }

    // A refused range context drops the frame before any reserve or copy.
    {
        TrackedContextSource refusing;
        refusing.nextError = GraphicsFrameError::NoReadyCredit;
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(1));
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 2, 1,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2}, {});
        dataSet.Add(surface);
        GraphicsFrameError storageError = GraphicsFrameError::None;
        assert(!adapter.convertFrameWithRejection(
            dataSet, rangeRequest, refusing, rejection, nullptr, std::nullopt, &storageError).has_value());
        assert(refusing.calls == 1U && refusing.reserveCalls == 0U);
        assert(storageError == GraphicsFrameError::NoReadyCredit);
        assert(rejection.reason == Rejection::StorageRefused);
        assert(rejection.part == 1U && rejection.requestedStorageBytes > 0U);
        assert(!rejection.rangeRetained);
        assert(refusing.liveAllocations == 0U);
    }

    // Image + range across all four codecs share one builder/allocation with exact planned footprint.
    {
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        allRequest.includeRangeAuxiliaryChannels = true;

        const auto checkImagePlusRange = [&](GoPxLSdk::GoDataSet& dataSet) {
            TrackedContextSource storage;
            GocatorImageRejection imageRejection;
            const auto converted = adapter.convertFrameWithRejection(
                dataSet, allRequest, storage, rejection, &imageRejection);
            assert(converted && converted->images().size() == 1U && converted->ranges().size() == 1U);
            assert(storage.calls == 1U && storage.reserveCalls == 2U);
            assert(storage.usedBytes == storage.requestedBytes);
            const auto& planned = converted->ranges().front();
            const auto& plannedImage = converted->images().front();
            assert(plannedImage.payload.pixels.allocation().get()
                == planned.payload.zValues.allocation().get());
            assert(plannedImage.payload.pixels.allocation()->state() == GraphicsAllocationState::Published);
            assert(rejection.reason == Rejection::None && rejection.rangeRetained && rejection.imageRetained);
            assert(imageRejection.reason == GraphicsImageFailure::None);
            return converted;
        };

        // 1. Image + UNIFORM_SURFACE
        {
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
            const auto converted = checkImagePlusRange(dataSet);
            assert(converted->images().front().payload.data()[3] == 255U);
            expectCalibratedSurface(*converted, true);
        }

        // 2. Image + SURFACE_POINT_CLOUD
        {
            GoPxLSdk::GoDataSet dataSet;
            dataSet.Add(stamp(43));
            auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
            GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
                std::vector<k8u>{99});
            dataSet.Add(image);
            auto cloud = std::make_shared<GoPxLSdk::GoGdpSurfacePointCloud>();
            GoGdpMsgTests::setSurfacePointCloud(*cloud, 2, 1,
                {0.5, 0.25, 2.0}, {10.0, 20.0, -1.0},
                {{2, 3, 4}, {gocator_range::kNullSample, 1, 2}});
            dataSet.Add(cloud);
            const auto converted = checkImagePlusRange(dataSet);
            assert(converted->images().front().payload.data()[0] == 99U);
            expectArray(converted->ranges().front().payload.xValues, {11.0F, 0.0F}, "img+cloud X");
        }

        // 3. Image + UNIFORM_PROFILE
        {
            GoPxLSdk::GoDataSet dataSet;
            dataSet.Add(stamp(44));
            auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
            GoGdpMsgTests::setImage(*image, 3, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
                std::vector<k8u>{1, 2, 3});
            dataSet.Add(image);
            auto profile = std::make_shared<GoPxLSdk::GoGdpProfileUniform>();
            GoGdpMsgTests::setUniformProfile(*profile, 2,
                {0.0, 0.0, 0.5}, {0.0, 0.0, -1.0},
                {5, 7}, {10, 20});
            dataSet.Add(profile);
            const auto converted = checkImagePlusRange(dataSet);
            expectArray(converted->ranges().front().payload.zValues, {1.5F, 2.5F}, "img+profile Z");
        }

        // 4. Image + PROFILE_POINT_CLOUD
        {
            GoPxLSdk::GoDataSet dataSet;
            dataSet.Add(stamp(45));
            auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
            GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8,
                std::vector<k8u>{77});
            dataSet.Add(image);
            auto profile = std::make_shared<GoPxLSdk::GoGdpProfilePointCloud>();
            GoGdpMsgTests::setProfilePointCloud(*profile, 2,
                {2.0, 0.0, 0.5}, {10.0, 0.0, -1.0},
                {{1, 2}, {3, 4}});
            dataSet.Add(profile);
            const auto converted = checkImagePlusRange(dataSet);
            expectArray(converted->ranges().front().payload.xValues, {12.0F, 16.0F}, "img+pcloud X");
            expectArray(converted->ranges().front().payload.yValues, {0.0F, 0.0F}, "img+pcloud Y");
        }
    }

    // A narrow image is fitted and published beside the range, and still reported.
    {
        TrackedContextSource storage;
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
            dataSet, allRequest, storage, rejection, &imageRejection);
        assert(converted && converted->images().size() == 1U && converted->ranges().size() == 1U);
        assert(storage.calls == 1U && storage.reserveCalls == 2U
            && storage.usedBytes == storage.requestedBytes);
        assert(imageRejection.reason == GraphicsImageFailure::StrideBelowMinimum);
        assert(rejection.rangeRetained && rejection.imageRetained);
    }

    // A refused context for a range plus image frame drops the whole frame before any reserve.
    {
        TrackedContextSource refusing;
        refusing.nextError = GraphicsFrameError::NoReadyCredit;
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
        assert(!adapter.convertFrameWithRejection(dataSet, allRequest, refusing, rejection).has_value());
        assert(refusing.calls == 1U && refusing.reserveCalls == 0U);
        assert(rejection.reason == Rejection::StorageRefused && rejection.requestedStorageBytes > 0U);
        assert(!rejection.rangeRetained && !rejection.imageRetained);
        assert(refusing.liveAllocations == 0U);
    }

    // Tracked output bounds: if a reserve exceeds the allocated budget (simulated via budgetDelta),
    // the adapter aborts under RAII without publishing a truncated frame and returns Allocation.
    {
        TrackedContextSource shortBudget;
        shortBudget.budgetDelta = 1U;
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            std::vector<k8u>{1, 2, 3, 4});
        dataSet.Add(image);
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 1, 1, {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {5}, {});
        dataSet.Add(surface);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        GraphicsFrameError storageError = GraphicsFrameError::None;
        assert(!adapter.convertFrameWithRejection(
            dataSet, allRequest, shortBudget, rejection, nullptr, std::nullopt, &storageError).has_value());
        assert(shortBudget.calls == 1U && shortBudget.reserveCalls == 2U);
        assert(storageError == GraphicsFrameError::Allocation);
        assert(rejection.reason == Rejection::StorageRefused);
        assert(!rejection.frameRetained && !rejection.imageRetained && !rejection.rangeRetained);
        assert(shortBudget.liveAllocations == 0U);
    }

    // Cancellation during beginFrame aborts under RAII without publishing or retaining allocations.
    {
        TrackedContextSource storage;
        std::atomic<bool> cancelled{false};
        storage.onBeginFrame = [&cancelled] { cancelled.store(true, std::memory_order_release); };
        GoPxLSdk::GoDataSet dataSet;
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 2, 1, {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {1, 2}, {});
        dataSet.Add(surface);
        GraphicsFrameError storageError = GraphicsFrameError::None;
        assert(!adapter.convertFrameWithRejection(
            dataSet, rangeRequest, storage, rejection, nullptr, std::nullopt, &storageError, &cancelled).has_value());
        assert(storage.calls == 1U && storage.reserveCalls == 0U);
        assert(storageError == GraphicsFrameError::Disconnected);
        assert(storage.liveAllocations == 0U);
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

    // Short image buffer: planned at the declared size, the bytes that exist are copied, the rest is
    // zero-filled (the test storage is poisoned), and the shortfall is counted.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImageBuffer(*image, 2, 2, GoPxLSdk::GoGdpPixelFormat::Mono8,
            1U, 2U, std::vector<k8u>{1, 2, 3, 4});
        GoGdpMsgTests::setImageHeight(*image, 3);
        dataSet.Add(image);
        GraphicsFrameRequest request;
        request.components = GraphicsFrameComponent::Image;
        GocatorImageRejection evidence;
        GocatorFrameAnomalies anomalies;
        const auto frame = adapter.convertFrameWithRejection(
            dataSet, request, storage, rejection, &evidence, std::nullopt, nullptr, nullptr, &anomalies);
        assert(frame && frame->images().size() == 1U);
        const auto& fitted = frame->images().front().payload;
        assert(fitted.width == 2 && fitted.height == 3 && fitted.stride == 2 && fitted.pixels.size() == 6U);
        const std::uint8_t expected[] = {1, 2, 3, 4, 0, 0};
        for (std::size_t index = 0; index < 6U; ++index) assert(fitted.data()[index] == expected[index]);
        assert(evidence.reason == GraphicsImageFailure::TruncatedBytes);
        assert(evidence.readableBytes == 4U && evidence.requiredBytes == 6U && evidence.imageRetained);
        assert(anomalies.imageCount == 1U && anomalies.rangeCount == 0U);
        assert(storage.requestedBytes == 6U && storage.usedBytes == 6U);
    }

    // A short sample array is planned at its declared size: the samples that exist convert, the rest
    // is zero and marked invalid.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        auto profile = std::make_shared<GoPxLSdk::GoGdpProfileUniform>();
        GoGdpMsgTests::setUniformProfile(*profile, 2,
            {0.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {5, 6});
        GoGdpMsgTests::setUniformProfileWidth(*profile, 4);
        dataSet.Add(profile);
        GocatorFrameAnomalies anomalies;
        const auto frame = adapter.convertFrameWithRejection(
            dataSet, rangeRequest, storage, rejection, nullptr, std::nullopt, nullptr, nullptr, &anomalies);
        assert(frame && frame->ranges().size() == 1U);
        const RangeFrame& fitted = frame->ranges().front().payload;
        assert(fitted.width == 4 && fitted.height == 1);
        expectArray(fitted.zValues, {5.0F, 6.0F, 0.0F, 0.0F}, "short profile Z");
        expectMask(fitted.validMask, {1U, 1U, 0U, 0U});
        assert(anomalies.rangeCount == 1U && anomalies.ranges[0].reason == Rejection::TruncatedData);
        assert(anomalies.ranges[0].frameRetained && anomalies.ranges[0].rangeRetained);
    }

    // Declared dimensions of zero with data present fit the delivered array instead of dropping it.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 2, 1,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {7, 8}, {});
        GoGdpMsgTests::setSurfaceWidth(*surface, 0);
        dataSet.Add(surface);
        GocatorFrameAnomalies anomalies;
        const auto frame = adapter.convertFrameWithRejection(
            dataSet, rangeRequest, storage, rejection, nullptr, std::nullopt, nullptr, nullptr, &anomalies);
        assert(frame && frame->ranges().size() == 1U);
        assert(frame->ranges().front().payload.width == 2 && frame->ranges().front().payload.height == 1);
        expectArray(frame->ranges().front().payload.zValues, {7.0F, 8.0F}, "array-sized surface Z");
        assert(anomalies.rangeCount == 1U && anomalies.ranges[0].reason == Rejection::InvalidDimensions);
    }

    // Sample arrays of an item size the adapter cannot interpret publish their bytes as raw Mono8.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurfaceInt32(*surface, 2, 1, {100, 200});
        dataSet.Add(surface);
        GocatorFrameAnomalies anomalies;
        const auto frame = adapter.convertFrameWithRejection(
            dataSet, rangeRequest, storage, rejection, nullptr, std::nullopt, nullptr, nullptr, &anomalies);
        assert(frame && frame->ranges().empty() && frame->images().size() == 1U);
        const auto& raw = frame->images().front().payload;
        assert(raw.pixelFormat == GraphicsImagePixelFormat::Mono8);
        assert(raw.width == 8 && raw.height == 1 && raw.pixels.size() == 8U);
        assert(raw.data()[0] == 100U && raw.data()[4] == 200U);
        assert(rejection.reason == Rejection::UnsupportedFormat && rejection.imageRetained);
        assert(anomalies.rangeCount == 1U);
    }

    // One bad part never removes another: parts without any bytes are absent and counted, every
    // other part publishes.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(stamp(5));
        dataSet.Add(std::make_shared<GoPxLSdk::GoGdpImage>());
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Mono8, std::vector<k8u>{9});
        dataSet.Add(image);
        dataSet.Add(std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>());
        auto surface = std::make_shared<GoPxLSdk::GoGdpSurfaceUniform>();
        GoGdpMsgTests::setUniformSurface(*surface, 1, 1,
            {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {3}, {});
        dataSet.Add(surface);
        GraphicsFrameRequest allRequest;
        allRequest.components = GraphicsFrameComponent::All;
        GocatorImageRejection imageEvidence;
        GocatorFrameAnomalies anomalies;
        const auto frame = adapter.convertFrameWithRejection(
            dataSet, allRequest, storage, rejection, &imageEvidence, std::nullopt, nullptr, nullptr, &anomalies);
        assert(frame && frame->images().size() == 1U && frame->ranges().size() == 1U);
        assert(frame->images().front().payload.data()[0] == 9U);
        assert(anomalies.imageCount == 2U);
        assert(anomalies.images[0].reason == GraphicsImageFailure::InvalidDimensions);
        assert(anomalies.images[1].reason == GraphicsImageFailure::MissingBytes);
        assert(anomalies.rangeCount == 2U);
        assert(anomalies.ranges[0].reason == Rejection::InvalidDimensions && anomalies.ranges[0].part == 3U);
        assert(anomalies.ranges[1].reason == Rejection::MissingData && anomalies.ranges[1].partCount == 5U);
        assert(anomalies.ranges[1].frameRetained && anomalies.ranges[1].imageRetained
            && anomalies.ranges[1].rangeRetained);
    }

    // Parts that cannot be sensibly interpreted still publish, and the per-frame anomaly record is
    // fixed-capacity: observations beyond it are counted per reason instead of allocating.
    {
        TrackedContextSource storage;
        GoPxLSdk::GoDataSet dataSet;
        for (int index = 0; index < 10; ++index) {
            auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
            GoGdpMsgTests::setImage(*image, 1, 1, GoPxLSdk::GoGdpPixelFormat::Null_Format,
                std::vector<k8u>{static_cast<k8u>(index)});
            dataSet.Add(image);
        }
        GraphicsFrameRequest request;
        request.components = GraphicsFrameComponent::Image;
        GocatorFrameAnomalies anomalies;
        const auto frame = adapter.convertFrameWithRejection(
            dataSet, request, storage, rejection, nullptr, std::nullopt, nullptr, nullptr, &anomalies);
        assert(frame && frame->images().size() == 8U);
        assert(frame->images().front().descriptor.id == "image");
        std::string lastId;
        for (const auto& resource : frame->images()) lastId = resource.descriptor.id;
        assert(lastId == "image-8");
        assert(anomalies.imageCount == GocatorFrameAnomalies::capacity);
        const auto overflowOf = [&](const GraphicsImageFailure reason) {
            return anomalies.imageOverflowByReason[static_cast<std::size_t>(reason)];
        };
        assert(overflowOf(GraphicsImageFailure::UnsupportedFormat) == 2U);
        assert(overflowOf(GraphicsImageFailure::ExtentOverflow) == 2U);
    }

    return 0;
}
