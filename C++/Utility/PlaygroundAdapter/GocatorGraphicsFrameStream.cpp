#include "GocatorGraphicsFrameStream.h"

#include "Gocator.h"
#include "GocatorDataSetGraphicsFrameAdapter.h"

#include <GoPxLSdk/GoDataSet.h>

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace {

[[nodiscard]] GraphicsFrameRequest gocatorGraphicsFrameRequest() noexcept
{
    GraphicsFrameRequest request;
    // Gocator GDP messages carry measured range data. Point-cloud/surface
    // views are derived by GraphicsEngine from this authoritative payload.
    request.components = GraphicsFrameComponent::Range;
    request.includeRangeAuxiliaryChannels = true;
    request.includePointCloudColors = true;
    return request;
}

} // namespace

class GocatorGraphicsFrameStream::Impl final
{
public:
    Impl(Gocator* gocator, GraphicsFrameCallback callback);
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

private:
    Gocator* _gocator = nullptr;
    GraphicsFrameCallback _callback;
    GocatorDataSetGraphicsFrameAdapter _adapter;
    GraphicsFrameCallbackGate _callbackGate;
    Gocator::CallbackId _grabCallbackId = 0;
};

GocatorGraphicsFrameStream::Impl::Impl(
    Gocator* gocator,
    GraphicsFrameCallback callback)
    : _gocator(gocator), _callback(std::move(callback))
{
    if (!_gocator || !_callback)
    {
        return;
    }

    const auto callbackToken = _callbackGate.token();
    _grabCallbackId = _gocator->registerGrabCallback(
        [this, callbackToken](const GoPxLSdk::GoDataSet& dataSet, const std::size_t sequence) {
            GraphicsFrameCallbackGate::Lease lease(callbackToken);
            if (!lease) return;
            try
            {
                auto frame = _adapter.convertFrame(dataSet, gocatorGraphicsFrameRequest());
                if (frame.has_value())
                {
                    frame->metadata.frameIndex = sequence;
                    _callback(std::move(*frame), 0U);
                }
            }
            catch (const std::exception& error)
            {
                Gocator::syslog(
                    std::string("Gocator GraphicsFrame callback failed: ") + error.what(), true);
            }
            catch (...)
            {
                Gocator::syslog("Gocator GraphicsFrame callback failed with an unknown exception.", true);
            }
        });
}

GocatorGraphicsFrameStream::Impl::~Impl()
{
    _callbackGate.beginShutdown();
    if (_gocator && _grabCallbackId != 0U)
    {
        _gocator->deregisterGrabCallback(_grabCallbackId);
    }
    _callbackGate.waitForDrain();
}

GocatorGraphicsFrameStream::GocatorGraphicsFrameStream(
    Gocator* gocator,
    GraphicsFrameCallback callback)
    : _impl(std::make_unique<Impl>(gocator, std::move(callback)))
{
}

GocatorGraphicsFrameStream::~GocatorGraphicsFrameStream() = default;
