#include "GocatorDataSetGraphicsFrameAdapter.h"
#include "GocatorRangeConversion.h"

#include "graphics/data/GraphicsRangeFrameWriter.h"

#include <GoPxLSdk/GoDataSet.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

struct RecordingStorage
{
    std::size_t calls = 0;
    std::size_t bytes = 0;
    std::shared_ptr<std::uint8_t> block;

    GraphicsImageStorage function()
    {
        return [this](const std::size_t requested)
        {
            ++calls;
            bytes = requested;
            block = std::shared_ptr<std::uint8_t>(
                new std::uint8_t[requested], std::default_delete<std::uint8_t[]>());
            return block;
        };
    }
};

} // namespace

int main()
{
    GoPxLSdk::GoDataSet empty;
    GocatorDataSetGraphicsFrameAdapter adapter;

    GraphicsFrameRequest rangeRequest;
    rangeRequest.components = GraphicsFrameComponent::Range;
    assert(!adapter.convertFrame(empty, rangeRequest).has_value());

    GraphicsFrameRequest pointCloudOnly;
    pointCloudOnly.components = GraphicsFrameComponent::PointCloud;
    assert(!adapter.convertFrame(empty, pointCloudOnly).has_value());

    // Uniform surface/profile conversion: null sensor codes stay zero and set
    // the mask; valid codes keep the adapter's resolution/offset math.
    {
        GraphicsRangeFrameLayout layout;
        layout.width = 2;
        layout.height = 2;
        layout.channels.validMask = true;
        RecordingStorage storage;
        GraphicsRangeFrameWriter writer(layout, storage.function());
        assert(writer.isValid());
        assert(storage.calls == 1);
        assert(storage.bytes == writer.blockBytes());

        const std::int16_t samples[] = {1, 2, gocator_range::kNullSample, 4};
        assert(gocator_range::scalarHasInvalid(samples, 4));
        gocator_range::writeScalarZ(samples, 4, 0.5, -1.0,
            writer.zData(), writer.validMaskData());
        const RangeFrame frame = writer.takeFrame();
        assert(frame.isValid());
        assert(frame.zValues.isBorrowed());
        assert(frame.validMask.isBorrowed());
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
        GraphicsRangeFrameLayout layout;
        layout.width = 2;
        layout.height = 1;
        layout.channels.x = true;
        layout.channels.y = true;
        layout.channels.validMask = true;
        RecordingStorage storage;
        GraphicsRangeFrameWriter writer(layout, storage.function());
        assert(writer.isValid());

        const std::int16_t points[] = {2, 3, 4, gocator_range::kNullSample, 1, 2};
        assert(gocator_range::surfacePointsHaveInvalid(points, 2));
        gocator_range::writeSurfacePoints(points, 2,
            0.5, 0.25, 2.0, 10.0, 20.0, -1.0,
            writer.xData(), writer.yData(), writer.zData(), writer.validMaskData());
        const RangeFrame frame = writer.takeFrame();
        assert(frame.xValues[0] == 11.0F);
        assert(frame.yValues[0] == 20.75F);
        assert(frame.zValues[0] == 7.0F);
        assert(frame.validMask[0] == 1U);
        assert(frame.xValues[1] == 0.0F);
        assert(frame.yValues[1] == 0.0F);
        assert(frame.zValues[1] == 0.0F);
        assert(frame.validMask[1] == 0U);
    }

    // Profile point clouds map the second sample to Z and zero the Y channel.
    {
        GraphicsRangeFrameLayout layout;
        layout.width = 2;
        layout.height = 1;
        layout.channels.x = true;
        layout.channels.y = true;
        layout.channels.validMask = true;
        RecordingStorage storage;
        GraphicsRangeFrameWriter writer(layout, storage.function());
        assert(writer.isValid());
        std::fill_n(writer.yData(), 2, 0.0F);

        const std::int16_t points[] = {1, 2, gocator_range::kNullSample, 3};
        assert(gocator_range::profilePointsHaveInvalid(points, 2));
        gocator_range::writeProfilePoints(points, 2, 2.0, 0.5, 10.0, -1.0,
            writer.xData(), writer.zData(), writer.validMaskData());
        const RangeFrame frame = writer.takeFrame();
        assert(frame.xValues[0] == 12.0F);
        assert(frame.zValues[0] == 0.0F);
        assert(frame.validMask[0] == 1U);
        assert(frame.xValues[1] == 0.0F);
        assert(frame.zValues[1] == 0.0F);
        assert(frame.validMask[1] == 0U);
        assert(frame.yValues[0] == 0.0F);
        assert(frame.yValues[1] == 0.0F);
    }

    // All-valid input omits the mask, matching the previous clear-on-valid rule.
    {
        GraphicsRangeFrameLayout layout;
        layout.width = 2;
        layout.height = 1;
        const std::int16_t samples[] = {5, 6};
        layout.channels.validMask = gocator_range::scalarHasInvalid(samples, 2);
        GraphicsRangeFrameWriter writer(layout);
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
        GraphicsRangeFrameWriter writer(layout,
            [](std::size_t) { return std::shared_ptr<std::uint8_t>(); });
        assert(!writer.isValid());
        assert(!writer.takeFrame().isValid());
    }

    return 0;
}
