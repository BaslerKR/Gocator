#include "GocatorGraphicsFrameStream.h"

#include "Gocator.h"
#include "GocatorDataSetGraphicsFrameAdapter.h"

#include <GoPxLSdk/GoDataSet.h>

#include <atomic>
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
    Impl(Gocator* gocator,
         std::shared_ptr<GraphicsFrameContextSource> contextSource,
         GraphicsFrameCallback callback,
         RejectedRange rejectedRange,
         RejectedImage rejectedImage,
         StorageOutcome storageOutcome,
         std::shared_ptr<const std::atomic<bool>> cancelled);
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void cancel() noexcept
    {
        _localCancelled.store(true, std::memory_order_release);
    }

private:
    Gocator* _gocator = nullptr;
    std::shared_ptr<GraphicsFrameContextSource> _contextSource;
    GraphicsFrameCallback _callback;
    StorageOutcome _storageOutcome;
    std::shared_ptr<const std::atomic<bool>> _externalCancelled;
    std::atomic<bool> _localCancelled{false};
    GocatorDataSetGraphicsFrameAdapter _adapter;
    GraphicsFrameCallbackGate _callbackGate;
    Gocator::CallbackId _grabCallbackId = 0;

    [[nodiscard]] bool isCancelled() const noexcept
    {
        return _localCancelled.load(std::memory_order_acquire)
            || (_externalCancelled && _externalCancelled->load(std::memory_order_acquire));
    }
};

GocatorGraphicsFrameStream::Impl::Impl(
    Gocator* gocator,
    std::shared_ptr<GraphicsFrameContextSource> contextSource,
    GraphicsFrameCallback callback,
    RejectedRange rejectedRange,
    RejectedImage rejectedImage,
    StorageOutcome storageOutcome,
    std::shared_ptr<const std::atomic<bool>> cancelled)
    : _gocator(gocator),
      _contextSource(std::move(contextSource)),
      _callback(std::move(callback)),
      _storageOutcome(std::move(storageOutcome)),
      _externalCancelled(std::move(cancelled))
{
    if (!_gocator || !_contextSource || !_callback)
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
            if (isCancelled())
            {
                return;
            }
            try
            {
                GocatorRangeRejection rejection;
                GocatorImageRejection imageRejection;
                GraphicsFrameError storageError = GraphicsFrameError::None;
                const auto* activeCancel = _externalCancelled ? _externalCancelled.get() : &_localCancelled;
                auto frame = _adapter.convertFrameWithRejection(
                    dataSet, gocatorGraphicsFrameRequest(), *_contextSource,
                    rejection, &imageRejection, sequence, &storageError, activeCancel);
                if (storageError != GraphicsFrameError::None && _storageOutcome)
                {
                    _storageOutcome(storageError);
                }
                if (isCancelled())
                {
                    return;
                }
                if (rejectedRange && rejection.reason != GocatorRangeRejection::Reason::None)
                    rejectedRange(rejection, sequence);
                if (rejectedImage && imageRejection.reason != GraphicsImageFailure::None)
                    rejectedImage(imageRejection, sequence);
                if (frame.has_value())
                {
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
    _localCancelled.store(true, std::memory_order_release);
    _callbackGate.beginShutdown();
    if (_gocator && _grabCallbackId != 0U)
    {
        _gocator->deregisterGrabCallback(_grabCallbackId);
    }
    _callbackGate.waitForDrain();
}

GocatorGraphicsFrameStream::GocatorGraphicsFrameStream(
    Gocator* gocator,
    std::shared_ptr<GraphicsFrameContextSource> contextSource,
    GraphicsFrameCallback callback,
    RejectedRange rejectedRange,
    RejectedImage rejectedImage,
    StorageOutcome storageOutcome,
    std::shared_ptr<const std::atomic<bool>> cancelled)
    : _impl(std::make_unique<Impl>(gocator, std::move(contextSource), std::move(callback),
        std::move(rejectedRange), std::move(rejectedImage), std::move(storageOutcome),
        std::move(cancelled)))
{
}

GocatorGraphicsFrameStream::~GocatorGraphicsFrameStream() = default;

void GocatorGraphicsFrameStream::cancel() noexcept
{
    if (_impl) _impl->cancel();
}
