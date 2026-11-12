#pragma once
#include "AbstractSourceController.h"
#include "Gocator.h"
#include "DeviceConnectionControl.h"
#include "DeviceParameterControl.h"
#include <QFuture>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class GocatorGraphicsFrameStream;

class GocatorSourceController : public AbstractSourceController, public IDeviceParameterControl, public IDeviceConnectionControl {
    Q_OBJECT
    Q_INTERFACES(IDeviceParameterControl IDeviceConnectionControl)
public:
    using ParameterCompletion = IDeviceParameterControl::Completion;
    using ConnectionCompletion = IDeviceConnectionControl::Completion;
    using TargetsCompletion = IDeviceConnectionControl::TargetsCompletion;

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
    DeviceParameterResult requestReadParameter(DeviceParameterTarget target, ParameterCompletion completion) override;
    /** @brief Applies Quick Edit through the SDK with validation and actual readback. */
    DeviceParameterResult requestWriteParameter(DeviceParameterTarget target, DeviceParameterValue value,
        std::optional<std::uint64_t> expectedRevision, ParameterCompletion completion) override;
    /** @brief Rejects commands: GoSystem exposes no software-trigger command. */
    DeviceParameterResult requestExecuteParameter(DeviceParameterTarget target,
        std::optional<std::uint64_t> expectedRevision, ParameterCompletion completion) override;

    /** @brief Declares discovery, connect and disconnect while the session exists. */
    DeviceConnectionCapabilities connectionCapabilities() const override;
    /** @brief Copies the cached discovery catalog without refreshing it. */
    DeviceConnectionResult requestConnectionTargets(TargetsCompletion completion) override;
    /** @brief Copies the logical connection and producer counters without SDK waits. */
    DeviceConnectionResult requestConnectionState(IDeviceConnectionControl::Completion completion) override;
    /** @brief Refreshes discovery while disconnected and idle. */
    DeviceConnectionResult requestDiscover(TargetsCompletion completion) override;
    /** @brief Opens one exact cached target while disconnected. */
    DeviceConnectionResult requestConnect(std::string targetId, IDeviceConnectionControl::Completion completion) override;
    /** @brief Closes the exact current connection while acquisition is stopped. */
    DeviceConnectionResult requestDisconnect(std::string connectionId, IDeviceConnectionControl::Completion completion) override;

signals:
    /** @brief Publishes a completed discovery cache change for presentation observers. */
    void inventoryChanged();

private:
    friend class GocatorParameterControlTests;
    friend class GocatorConnectionControlTests;
    // Copied control callbacks keep SDK I/O separate from contract validation.
    std::function<Gocator::SystemState()> _readSystem;
    std::function<void(bool, std::optional<Gocator::SystemState>)> _writeQuickEdit;
    std::function<bool()> _systemConnected;
    /** @brief Owns one accepted worker and drains it before controller destruction. */
    DeviceParameterResult scheduleParameterWork(std::function<void()> work);
    DeviceParameterResult parameterWork(DeviceParameterTarget target,
        std::optional<DeviceParameterValue> value, std::optional<std::uint64_t> revision, ParameterCompletion completion);
    std::vector<DeviceParameterDescriptor> observeSystem(const Gocator::SystemState& state);
    QFuture<void> _parameterWork;
    std::atomic<bool> _parameterBusy{false};
    std::string _parameterIdentity;
    std::optional<Gocator::SystemState> _lastSystemState;
    std::uint64_t _parameterRevision = 0;
    void registerCallbacks();
    void deregisterCallbacks();

    /** @brief Isolates lifecycle SDK work behind copied observations and admission seams. */
    struct LifecycleBackend {
        std::function<Gocator::ConnectionSnapshot()> snapshot;
        std::function<std::vector<Gocator::DeviceInfo>()> cachedDevices;
        std::function<std::vector<Gocator::DeviceInfo>(std::string*)> discover;
        std::function<bool(const std::string&)> open;
        std::function<void()> close;
        std::function<bool()> beginControl;
        std::function<void()> endControl;
        std::function<GocatorRuntimeDiagnostics()> diagnostics;
    };
    enum class Lifecycle { Discover, Connect, Disconnect };
    /** @brief Validates one lifecycle command, acquires the shared gate and schedules worker work. */
    DeviceConnectionResult admitLifecycle(Lifecycle operation, std::string argument,
        IDeviceConnectionControl::Completion completion, TargetsCompletion targetsCompletion);
    /** @brief Copies the logical connection from the shared facade and cached target facts. */
    DeviceConnectionState connectionState() const;
    /** @brief Builds the opaque id from this controller and the module connection generation. */
    std::string currentConnectionId(std::uint64_t generation) const;
    std::optional<Gocator::DeviceInfo> cachedTarget(const std::string& targetId) const;
    LifecycleBackend _lifecycle;
    bool _lifecycleAvailable = false;
    QFuture<void> _controlWork;

    Gocator* _gocator;
    FrameConsumer _frameConsumer;
    std::atomic<bool> _isGrabbing{false};
    std::atomic<uint64_t> _frameSeq{0};

    Gocator::CallbackId _statusCallbackId = 0;
    std::unique_ptr<GocatorGraphicsFrameStream> _graphicsStream;
};
