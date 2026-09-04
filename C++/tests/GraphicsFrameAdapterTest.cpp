#include "GocatorDataSetGraphicsFrameAdapter.h"

#include <GoPxLSdk/GoDataSet.h>

#include <cassert>

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
    return 0;
}
