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
    request.components = GraphicsFrameComponent::Image | GraphicsFrameComponent::Range;
    request.includeRangeAuxiliaryChannels = true;
    request.includePointCloudColors = true;
    return request;
}

} // namespace

class GocatorGraphicsFrameStream::Impl final
{
public:
    Impl(Gocator* gocator, GraphicsFrameCallback callback, RejectedRange rejectedRange,
         RejectedImage rejectedImage);
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void setFrameStorageProvider(std::shared_ptr<IFrameStorageProvider> provider)
    {
        _adapter.setFrameStorageProvider(std::move(provider));
    }

private:
    Gocator* _gocator = nullptr;
    GraphicsFrameCallback _callback;
    GocatorDataSetGraphicsFrameAdapter _adapter;
    GraphicsFrameCallbackGate _callbackGate;
    Gocator::CallbackId _grabCallbackId = 0;
};

GocatorGraphicsFrameStream::Impl::Impl(
    Gocator* gocator,
    GraphicsFrameCallback callback,
    RejectedRange rejectedRange,
    RejectedImage rejectedImage)
    : _gocator(gocator), _callback(std::move(callback))
{
    if (!_gocator || !_callback)
    {
        return;
    }

    const auto callbackToken = _callbackGate.token();
    _grabCallbackId = _gocator->registerGrabCallback(
        [this, callbackToken, rejectedRange = std::move(rejectedRange),
         rejectedImage = std::move(rejectedImage)](
            const GoPxLSdk::GoDataSet& dataSet, const std::size_t sequence) {
            GraphicsFrameCallbackGate::Lease lease(callbackToken);
            if (!lease) return;
            try
            {
                GocatorRangeRejection rejection;
                GocatorImageRejection imageRejection;
                auto frame = rejectedRange || rejectedImage
                    ? _adapter.convertFrameWithRejection(dataSet, gocatorGraphicsFrameRequest(),
                        rejection, &imageRejection)
                    : _adapter.convertFrame(dataSet, gocatorGraphicsFrameRequest());
                if (rejectedRange && rejection.reason != GocatorRangeRejection::Reason::None)
                    rejectedRange(rejection, sequence);
                if (rejectedImage && imageRejection.reason != GraphicsImageFailure::None)
                    rejectedImage(imageRejection, sequence);
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
    GraphicsFrameCallback callback,
    RejectedRange rejectedRange,
    RejectedImage rejectedImage)
    : _impl(std::make_unique<Impl>(gocator, std::move(callback), std::move(rejectedRange),
        std::move(rejectedImage)))
{
}

GocatorGraphicsFrameStream::~GocatorGraphicsFrameStream() = default;

void GocatorGraphicsFrameStream::setFrameStorageProvider(std::shared_ptr<IFrameStorageProvider> provider)
{
    if (_impl) _impl->setFrameStorageProvider(std::move(provider));
}
