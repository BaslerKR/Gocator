#pragma once
#include "DeviceRuntime/Contract/AbstractSourceController.h"
#include "Gocator.h"
#include "DeviceRuntime/Contract/DeviceConnectionControl.h"
#include "DeviceRuntime/Contract/DeviceParameterControl.h"
#include "DeviceRuntime/Contract/FrameStorage.h"
#include "GocatorParameterNotifications.h"
#include "GocatorParameterTree.h"
#include <QFuture>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class GocatorGraphicsFrameStream;

class GocatorSourceController : public AbstractSourceController, public IDeviceParameterControl, public IDeviceConnectionControl,
                                public IFrameStorageClient {
    Q_OBJECT
    Q_INTERFACES(IDeviceParameterControl IDeviceConnectionControl IFrameStorageClient)
public:
    using ParameterCompletion = IDeviceParameterControl::Completion;
    using ChangeListener = IDeviceParameterControl::ChangeListener;
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

    /** @brief Observes the System, Scanner and Sensor resource trees independently of widget visibility. */
    DeviceParameterResult requestParameters(InventoryCompletion completion) override;
    /** @brief Reads one exact connection-scoped parameter in the current connection generation. */
    DeviceParameterResult requestReadParameter(DeviceParameterTarget target, ParameterCompletion completion) override;
    /** @brief Writes one parameter through the SDK with fresh validation and actual readback. */
    DeviceParameterResult requestWriteParameter(DeviceParameterTarget target, DeviceParameterValue value,
        std::optional<std::uint64_t> expectedRevision, ParameterCompletion completion) override;
    /** @brief Rejects commands: GoSystem exposes no software-trigger command. */
    DeviceParameterResult requestExecuteParameter(DeviceParameterTarget target,
        std::optional<std::uint64_t> expectedRevision, ParameterCompletion completion) override;
    /** @brief Starts one module-owned observation subscription and its initial snapshot. */
    DeviceParameterResult requestParameterChanges(std::string connectionId,
        std::string subscriptionId, ChangeListener delivery) override;
    /** @brief Stops delivery for one subscription and completes after the module drained it. */
    DeviceParameterResult releaseParameterChanges(std::string connectionId,
        std::string subscriptionId, ParameterCompletion completion) override;

    /** @brief Declares discovery, address connect and disconnect while the session exists. */
    DeviceConnectionCapabilities connectionCapabilities() const override;
    /** @brief Copies the cached discovery catalog without refreshing it. */
    DeviceConnectionResult requestConnectionTargets(TargetsCompletion completion) override;
    /** @brief Copies the logical connection and producer counters without SDK waits. */
    DeviceConnectionResult requestConnectionState(IDeviceConnectionControl::Completion completion) override;
    /** @brief Refreshes discovery while disconnected and idle. */
    DeviceConnectionResult requestDiscover(TargetsCompletion completion) override;
    /** @brief Opens one exact cached target or an undiscovered address while disconnected. */
    DeviceConnectionResult requestConnect(std::string targetId, IDeviceConnectionControl::Completion completion) override;
    /** @brief Closes the exact current connection while acquisition is stopped. */
    DeviceConnectionResult requestDisconnect(std::string connectionId, IDeviceConnectionControl::Completion completion) override;
    /** @brief Admits one frame acquisition and completes after the producer delivered it. */
    DeviceConnectionResult requestSingle(std::string connectionId, IDeviceConnectionControl::Completion completion) override;
    /** @brief Admits continuous acquisition on the exact connection. */
    DeviceConnectionResult requestLiveStart(std::string connectionId, IDeviceConnectionControl::Completion completion) override;
    /** @brief Admits stopping continuous acquisition on the exact connection. */
    DeviceConnectionResult requestLiveStop(std::string connectionId, IDeviceConnectionControl::Completion completion) override;

    /** @brief Accepts the storage its 3D frames should be written into. */
    void setFrameStorageProvider(std::shared_ptr<IFrameStorageProvider> provider) override;

signals:
    /** @brief Publishes a completed discovery cache change for presentation observers. */
    void inventoryChanged();

private:
    friend class GocatorParameterControlTests;
    friend class GocatorConnectionControlTests;
    friend class GocatorParameterLoopbackTests;
    friend class GocatorParameterNotificationTests;
    friend struct GocatorParameterTestAccess;

    /** @brief One admitted single run; defined with the acquisition members. */
    struct SingleRun;

    /** @brief One resource schema/data pair copied from the SDK. */
public:
    struct ResourceSnapshot {
        std::string schema;
        std::string data;
    };
private:

    /** @brief Last observed state of one flattened resource leaf. */
    struct ObservedParameter {
        std::uint64_t revision = 0;
        QJsonValue rawValue;
        bool writable = false;
        bool present = false;
    };

    /** @brief Result of one full observation. */
    struct Observation {
        std::vector<DeviceParameterDescriptor> parameters;
        std::vector<gocator::ResourceParameter> resourceParameters;
        std::optional<Gocator::SystemState> systemState;
        std::string connectionId;
    };

    // Copied control callbacks keep SDK I/O separate from contract validation.
    std::function<Gocator::SystemState()> _readSystem;
    std::function<void(bool, std::optional<Gocator::SystemState>)> _writeQuickEdit;
    std::function<bool()> _systemConnected;
    std::function<ResourceSnapshot(Gocator::ParameterTarget)> _readResource;
    std::function<Gocator::ParameterWriteResult(Gocator::ParameterTarget, const std::string&,
        const std::string&)> _writeParameter;
    /** @brief Owns one accepted worker and drains it before controller destruction. */
    DeviceParameterResult scheduleParameterWork(std::function<void()> work);
    DeviceParameterResult parameterWork(DeviceParameterTarget target,
        std::optional<DeviceParameterValue> value, std::optional<std::uint64_t> revision, ParameterCompletion completion);
    std::vector<DeviceParameterDescriptor> observeSystem(const Gocator::SystemState& state,
        const std::string& connectionId);
    /** @brief Observes every resource scope and publishes differences under one cycle lock.
     *  @param snapshot When true, queues a complete snapshot instead of only the differences.
     */
    Observation observeAll(bool snapshot = false);
    /** @brief Updates one resource scope's revision cache and appends its descriptors. */
    void observeResource(Observation* observation, Gocator::ParameterTarget target,
        const std::string& scope, const std::string& connectionId);
    /** @brief Builds one complete snapshot from the last observation, never calling the SDK. */
    DeviceParameterChange snapshotChange(const std::string& connectionId,
        const std::string& subscriptionId) const;
    /** @brief Publishes differences from the last published catalog while the cycle lock is held. */
    void publishDifferencesLocked(const Observation& observation);
    /** @brief Starts the module-owned observation thread; the caller must not hold a lock. */
    void startObservationThread();
    /** @brief Requests an immediate observation pass (for example after acquisition stops). */
    void wakeObservationThread();
    /** @brief Stops and joins the observation thread. */
    void stopObservationThread();
    /** @brief Runs observation, notification and drain until no subscription remains. */
    void observationLoop();
    QFuture<void> _parameterWork;
    std::atomic<bool> _parameterBusy{false};
    std::string _parameterIdentity;
    std::optional<Gocator::SystemState> _lastSystemState;
    std::uint64_t _parameterRevision = 0;
    mutable std::mutex _observationMutex;
    /** @brief Serializes one complete read cycle so observations and publications stay ordered. */
    std::mutex _observationCycleMutex;
    std::map<std::string, DeviceParameterDescriptor> _publishedParameters;
    std::map<std::string, ObservedParameter> _observedParameters;
    std::vector<DeviceParameterDescriptor> _lastCatalog;
    std::string _lastConnectionId;
    /** @brief Connection that owns the current subscriptions; used to terminate them on failure. */
    std::string _notificationConnectionId;
    gocator::ParameterNotificationHub _notifications;
    std::thread _observationThread;
    mutable std::mutex _observationThreadMutex;
    std::condition_variable _observationWake;
    bool _observationStop = false;
    bool _observationRunning = false;
    bool _observationSnapshotPending = false;
    std::chrono::milliseconds _observationInterval{1000};
    void registerCallbacks();
    void deregisterCallbacks();

    /** @brief Isolates lifecycle SDK work behind copied observations and admission seams. */
    struct LifecycleBackend {
        std::function<Gocator::ConnectionSnapshot()> snapshot;
        std::function<std::vector<Gocator::DeviceInfo>()> cachedDevices;
        std::function<std::vector<Gocator::DeviceInfo>(std::string*)> discover;
        std::function<bool(const std::string&, std::string*)> open;
        std::function<void()> close;
        std::function<bool()> beginControl;
        std::function<void()> endControl;
        std::function<GocatorRuntimeDiagnostics()> diagnostics;
        std::function<bool(std::size_t frames)> grab;
        std::function<void()> stop;
    };
    enum class Lifecycle { Discover, Connect, Disconnect, Single, LiveStart, LiveStop };
    /** @brief Validates one lifecycle command, acquires the shared gate and schedules worker work. */
    DeviceConnectionResult admitLifecycle(Lifecycle operation, std::string argument,
        IDeviceConnectionControl::Completion completion, TargetsCompletion targetsCompletion);
    /** @brief Copies the logical connection from the shared facade and cached target facts. */
    DeviceConnectionState connectionState() const;
    /** @brief Copies the observed producer acquisition state without SDK waits. */
    bool liveActive() const;
    /** @brief Starts continuous acquisition while the caller owns the start reservation. */
    bool beginLive();
    /** @brief Starts one acquisition run through the shared SDK seam. */
    bool beginGrab(std::size_t frames);
    /** @brief Reports one producer frame so a pending single can complete. */
    void noteAcquisitionFrame();
    /** @brief Reports the producer's end transition so a pending single can complete. */
    void noteAcquisitionEnded();
    /** @brief Wakes a pending single with the explicit stopped outcome. */
    void noteAcquisitionStopped();
    /** @brief Closes one single run before its completion is delivered. */
    void finishSingle(const std::shared_ptr<SingleRun>& run);
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

    /** @brief One admitted single run; the producer's frame and end transitions share it. */
    struct SingleRun {
        std::mutex mutex;
        std::condition_variable condition;
        bool delivered = false;
        bool ended = false;
        bool stopped = false;
    };
    std::shared_ptr<SingleRun> _singleRun;
    std::mutex _singleMutex;
    std::atomic<bool> _singleInFlight{false};
    /** @brief Serializes a source.start or live-start admission before the SDK grab begins. */
    std::atomic<bool> _acquisitionStarting{false};

    Gocator::CallbackId _statusCallbackId = 0;
    Gocator::CallbackId _singleCallbackId = 0;
    std::unique_ptr<GocatorGraphicsFrameStream> _graphicsStream;
    std::shared_ptr<IFrameStorageProvider> _frameStorage;
};
