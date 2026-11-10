#pragma once
#include "AbstractSourceController.h"
#include "Gocator.h"
#include "DeviceParameterControl.h"
#include <QFuture>
#include <atomic>
#include <memory>

class GocatorGraphicsFrameStream;

class GocatorSourceController : public AbstractSourceController, public IDeviceParameterControl {
    Q_OBJECT
    Q_INTERFACES(IDeviceParameterControl)
public:
    explicit GocatorSourceController(Gocator* gocator, QObject* parent = nullptr);
    ~GocatorSourceController() override;

    /** @brief Copies producer counters used by the device statusbar. */
    std::optional<SourceRuntimeDiagnostics> sourceRuntimeDiagnostics(unsigned sourceIndex) const override;
    void start() override;
    void stop() override;
    bool isGrabbing() const override;
    void setFrameConsumer(FrameConsumer consumer) override;
    bool supports3D() const override { return true; }

    /** @brief Observes official GoSystem controls independently of widget visibility. */
    DeviceParameterResult requestParameters(InventoryCompletion completion) override;
    /** @brief Reads an exact System parameter in the current connection generation. */
    DeviceParameterResult requestReadParameter(DeviceParameterTarget target, Completion completion) override;
    /** @brief Applies Quick Edit through the SDK with validation and actual readback. */
    DeviceParameterResult requestWriteParameter(DeviceParameterTarget target, DeviceParameterValue value,
        std::optional<std::uint64_t> expectedRevision, Completion completion) override;
    /** @brief Rejects commands: GoSystem exposes no software-trigger command. */
    DeviceParameterResult requestExecuteParameter(DeviceParameterTarget target,
        std::optional<std::uint64_t> expectedRevision, Completion completion) override;

private:
    friend class GocatorParameterControlTests;
    // Copied control callbacks keep SDK I/O separate from contract validation.
    std::function<Gocator::SystemState()> _readSystem;
    std::function<void(bool, std::optional<Gocator::SystemState>)> _writeQuickEdit;
    std::function<bool()> _systemConnected;
    /** @brief Owns one accepted worker and drains it before controller destruction. */
    DeviceParameterResult scheduleParameterWork(std::function<void()> work);
    DeviceParameterResult parameterWork(DeviceParameterTarget target,
        std::optional<DeviceParameterValue> value, std::optional<std::uint64_t> revision, Completion completion);
    std::vector<DeviceParameterDescriptor> observeSystem(const Gocator::SystemState& state);
    QFuture<void> _parameterWork;
    std::atomic<bool> _parameterBusy{false};
    std::string _parameterIdentity;
    std::optional<Gocator::SystemState> _lastSystemState;
    std::uint64_t _parameterRevision = 0;
    void registerCallbacks();
    void deregisterCallbacks();

    Gocator* _gocator;
    FrameConsumer _frameConsumer;
    std::atomic<bool> _isGrabbing{false};
    std::atomic<uint64_t> _frameSeq{0};

    Gocator::CallbackId _statusCallbackId = 0;
    std::unique_ptr<GocatorGraphicsFrameStream> _graphicsStream;
};
