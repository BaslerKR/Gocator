#pragma once
#include "AbstractSourceController.h"
#include "Gocator.h"
#include <atomic>
#include <memory>

class GocatorGraphicsFrameStream;

class GocatorSourceController : public AbstractSourceController {
    Q_OBJECT
public:
    explicit GocatorSourceController(Gocator* gocator, QObject* parent = nullptr);
    ~GocatorSourceController() override;

    void start() override;
    void stop() override;
    bool isGrabbing() const override;
    void setFrameConsumer(FrameConsumer consumer) override;
    bool supports3D() const override { return true; }

private:
    void registerCallbacks();
    void deregisterCallbacks();

    Gocator* _gocator;
    FrameConsumer _frameConsumer;
    std::atomic<bool> _isGrabbing{false};
    std::atomic<uint64_t> _frameSeq{0};

    Gocator::CallbackId _statusCallbackId = 0;
    std::unique_ptr<GocatorGraphicsFrameStream> _graphicsStream;
};
