#include "GocatorSourceController.h"
#include <QUuid>
#include <QtConcurrent>
#include <map>
#include <stdexcept>
#include "engine/GraphicsFrame.h"
#include "DeviceRuntime/Contract/SessionFrame.h"
#include "Internal/GocatorSelection.h"
#include "Utility/PlaygroundAdapter/GocatorGraphicsFrameStream.h"
#include "Utility/PlaygroundAdapter/GocatorDataSetGraphicsFrameAdapter.h"

namespace {

DeviceConnectionResult refused(DeviceConnectionError error, std::string message)
{
    return {error, std::move(message)};
}

DeviceImageRejection deviceImageRejection(const GraphicsImageFailure reason)
{
    switch (reason) {
    case GraphicsImageFailure::None: return DeviceImageRejection::NoImage;
    case GraphicsImageFailure::MissingBytes: return DeviceImageRejection::MissingBytes;
    case GraphicsImageFailure::InvalidDimensions: return DeviceImageRejection::InvalidDimensions;
    case GraphicsImageFailure::UnsupportedFormat: return DeviceImageRejection::UnsupportedFormat;
    case GraphicsImageFailure::InvalidStride: return DeviceImageRejection::InvalidStride;
    case GraphicsImageFailure::StrideBelowMinimum: return DeviceImageRejection::StrideBelowMinimum;
    case GraphicsImageFailure::ExtentOverflow: return DeviceImageRejection::ExtentOverflow;
    case GraphicsImageFailure::TruncatedBytes: return DeviceImageRejection::TruncatedBytes;
    case GraphicsImageFailure::OddWidth: return DeviceImageRejection::OddWidth;
    case GraphicsImageFailure::UnsupportedConversion: return DeviceImageRejection::UnsupportedConversion;
    case GraphicsImageFailure::ExcessBytes: return DeviceImageRejection::InvalidStride;
    case GraphicsImageFailure::StorageRefused: return DeviceImageRejection::StorageRefused;
    }
    return DeviceImageRejection::NoImage;
}

std::vector<Gocator::DeviceInfo> normalizedDevices(const std::vector<Gocator::DeviceInfo>& devices)
{
    std::vector<Gocator::DeviceInfo> uniqueDevices;
    uniqueDevices.reserve(devices.size());
    std::map<std::string, std::size_t> reported;
    for (const auto& device : devices) {
        if (device.controlPort == 0) continue;
        const auto targetId = gocator::targetIdForDevice(device);
        const auto [entry, inserted] = reported.emplace(targetId, uniqueDevices.size());
        if (inserted) {
            uniqueDevices.push_back(device);
            continue;
        }
        auto& existing = uniqueDevices[entry->second];
        if (existing.model.empty() && !device.model.empty()) existing.model = device.model;
        if ((existing.serial.empty() || existing.serial == "0")
            && !device.serial.empty() && device.serial != "0")
            existing.serial = device.serial;
        if (existing.isVirtual && !device.isVirtual) existing.isVirtual = false;
    }
    return uniqueDevices;
}

DeviceConnectionTargets copiedTargets(const std::vector<Gocator::DeviceInfo>& devices)
{
    const auto uniqueDevices = normalizedDevices(devices);
    DeviceConnectionTargets result;
    result.targets.reserve(uniqueDevices.size());
    for (const auto& device : uniqueDevices) {
        const auto targetId = gocator::targetIdForDevice(device);
        std::vector<DeviceConnectionAttribute> attributes{
            {"model", device.model},
            {"serial", device.serial},
            {"virtual", device.isVirtual ? "true" : "false"}};
        if (device.controlPort != gocator::kDefaultControlPort)
            attributes.push_back({"controlPort", std::to_string(device.controlPort)});
        // A virtual or serial-less device has no stable serial, so its index-free address is the
        // identity attribute that keeps distinct sensors apart in the Host's stable inventory.
        // Real sensors keep the serial-based identity so a DHCP address change does not rename them.
        if (device.isVirtual || device.serial.empty() || device.serial == "0")
            attributes.push_back({"address", device.address});
        result.targets.push_back({targetId,
            Gocator::formatDeviceName(device.model, device.serial, targetId, device.isVirtual),
            std::move(attributes)});
    }
    return result;
}

template<class Callback, class Value>
void completeConnection(const Callback& callback, Value value)
{
    try { callback(std::move(value)); } catch (...) {}
}

} // namespace

GocatorSourceController::GocatorSourceController(Gocator* gocator, QObject* parent,
                                                 DeviceRangeRejectionSink rangeRejectionSink,
                                                 DeviceImageRejectionSink imageRejectionSink)
    : AbstractSourceController(parent),
      _notifications([this](const std::string& connectionId, const std::string& subscriptionId) {
          return snapshotChange(connectionId, subscriptionId);
      }),
      _gocator(gocator), _rangeRejectionSink(rangeRejectionSink),
      _imageRejectionSink(imageRejectionSink) {
    _parameterIdentity = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    _readSystem = [gocator] { return gocator->systemState(); };
    _writeQuickEdit = [gocator](bool enabled, std::optional<Gocator::SystemState> expected) {
        gocator->setQuickEditEnabled(enabled, expected);
    };
    _systemConnected = [gocator] { return gocator && gocator->isOpened(); };
    if (gocator) {
        _readResource = [gocator](Gocator::ParameterTarget target) {
            return ResourceSnapshot{gocator->getParametersSchema(target),
                gocator->getParametersData(target)};
        };
        _writeParameter = [gocator](Gocator::ParameterTarget target, const std::string& path,
            const std::string& value) {
            return gocator->setParameterValue(target, path, value);
        };
    }
    if (gocator) {
        _lifecycleAvailable = true;
        _lifecycle.snapshot = [gocator] { return gocator->connectionSnapshot(); };
        _lifecycle.cachedDevices = [gocator] { return gocator->cachedDevices(); };
        _lifecycle.discover = [gocator](std::string* error) { return gocator->discoverDevices(error); };
        _lifecycle.open = [gocator](const std::string& targetId, std::string* error) {
            return gocator->open(targetId, error);
        };
        _lifecycle.close = [gocator] { gocator->close(true); };
        _lifecycle.beginControl = [gocator] { return gocator->tryBeginControl(); };
        _lifecycle.endControl = [gocator] { gocator->endControl(); };
        _lifecycle.diagnostics = [gocator] { return gocator->runtimeDiagnostics(); };
        _lifecycle.grab = [gocator](std::size_t frames) { return gocator->grab(frames); };
        _lifecycle.stop = [gocator] { gocator->stop(); };
    }
    registerCallbacks();
}

GocatorSourceController::~GocatorSourceController() {
    stopObservationThread();
    _notifications.close();
    _parameterWork.waitForFinished();
    try {
        stop();
    } catch (const std::exception& error) {
        Gocator::syslog(std::string("Gocator session stop failed during teardown: ") + error.what(), true);
    } catch (...) {
        Gocator::syslog("Gocator session stop failed during teardown with an unknown error.", true);
    }
    _controlWork.waitForFinished();
    deregisterCallbacks();
}

void GocatorSourceController::start() {
    if (!_gocator && !_lifecycle.grab) return;
    if (_singleInFlight.load())
        throw std::runtime_error("A single grab is in flight.");
    if (liveActive()) return;
    bool expected = false;
    if (!_acquisitionStarting.compare_exchange_strong(expected, true)) {
        if (_singleInFlight.load())
            throw std::runtime_error("A single grab is in flight.");
        if (liveActive()) return;
        throw std::runtime_error("Live acquisition is already starting.");
    }
    bool started = false;
    try {
        started = beginGrab(0);
    } catch (...) {
        _acquisitionStarting = false;
        throw;
    }
    _acquisitionStarting = false;
    // A disconnected producer refuses the grab without a status transition. Report the observed
    // state so an in-process caller and a worker proxy that optimistically marked the start
    // both keep the same inactive state.
    if (!started && !liveActive())
        emit diagnosticAcquisitionChanged(false, QStringLiteral("gocator"));
}

void GocatorSourceController::stop() {
    if (!_lifecycle.stop && !_gocator) return;
    if (_lifecycle.stop) _lifecycle.stop();
    else _gocator->stop();
    _isGrabbing = false;
    noteAcquisitionStopped();
}

bool GocatorSourceController::isGrabbing() const {
    return _isGrabbing;
}

bool GocatorSourceController::liveActive() const {
    return _isGrabbing.load()
        || (_lifecycle.diagnostics && _lifecycle.diagnostics().grabbing);
}

bool GocatorSourceController::beginLive() {
    // The caller already owns the acquisition reservation used by source.start.
    if (_singleInFlight.load() || liveActive()) {
        _acquisitionStarting = false;
        return false;
    }
    bool started = false;
    try {
        started = beginGrab(0);
    } catch (...) {
        _acquisitionStarting = false;
        throw;
    }
    _acquisitionStarting = false;
    return started;
}

bool GocatorSourceController::beginGrab(std::size_t frames) {
    if (_rangeRejectionSink) {
        for (auto& count : _rangeRejections) count.store(0, std::memory_order_relaxed);
    }
    if (_imageRejectionSink) {
        for (auto& count : _imageRejections) count.store(0, std::memory_order_relaxed);
    }
    if (_lifecycle.grab) return _lifecycle.grab(frames);
    if (!_gocator) return false;
    return _gocator->grab(frames);
}

void GocatorSourceController::noteAcquisitionFrame() {
    if (!_singleInFlight.load(std::memory_order_relaxed)) return;
    std::shared_ptr<SingleRun> run;
    {
        std::lock_guard<std::mutex> lock(_singleMutex);
        run = _singleRun;
    }
    if (!run) return;
    {
        std::lock_guard<std::mutex> lock(run->mutex);
        run->delivered = true;
    }
    run->condition.notify_all();
}

void GocatorSourceController::noteAcquisitionEnded() {
    if (!_singleInFlight.load(std::memory_order_relaxed)) return;
    std::shared_ptr<SingleRun> run;
    {
        std::lock_guard<std::mutex> lock(_singleMutex);
        run = _singleRun;
    }
    if (!run) return;
    {
        std::lock_guard<std::mutex> lock(run->mutex);
        run->ended = true;
    }
    run->condition.notify_all();
}

void GocatorSourceController::noteAcquisitionStopped() {
    std::shared_ptr<SingleRun> run;
    {
        std::lock_guard<std::mutex> lock(_singleMutex);
        run = _singleRun;
    }
    if (!run) return;
    {
        std::lock_guard<std::mutex> lock(run->mutex);
        run->stopped = true;
    }
    run->condition.notify_all();
}

void GocatorSourceController::finishSingle(const std::shared_ptr<SingleRun>& run) {
    {
        std::lock_guard<std::mutex> lock(_singleMutex);
        if (_singleRun == run) _singleRun.reset();
    }
    _singleInFlight = false;
}

void GocatorSourceController::setFrameConsumer(FrameConsumer consumer) {
    _frameConsumer = std::move(consumer);
}

void GocatorSourceController::setFrameStorageProvider(std::shared_ptr<IFrameStorageProvider> provider) {
    _frameStorage = std::move(provider);
    if (_graphicsStream) _graphicsStream->setFrameStorageProvider(_frameStorage);
}

void GocatorSourceController::registerCallbacks() {
    if (!_gocator) return;

    // 1. Status callback
    _statusCallbackId = _gocator->registerStatusCallback(
        [this, token = _statusCallbackGate.token()](Gocator::Status status, bool on) {
        GraphicsFrameCallbackGate::Lease lease(token);
        if (!lease) return;
        if (status == Gocator::GrabbingStatus) {
            _isGrabbing = on;
            if (!on) noteAcquisitionEnded();
            emit diagnosticAcquisitionChanged(on, QStringLiteral("gocator"));
            // Writability is re-observed after acquisition changes; never cached from a locked
            // state and never deferred to a GUI timer.
            wakeObservationThread();
        } else if (status == Gocator::ConnectionStatus && !on) {
            std::string connection;
            {
                std::lock_guard<std::mutex> lock(_observationMutex);
                connection = _lastConnectionId.empty() ? _notificationConnectionId : _lastConnectionId;
            }
            if (!connection.empty()) {
                _notifications.publishTerminal(connection, DeviceParameterError::Disconnected,
                    "The Gocator connection closed.");
                _notifications.drain();
            }
        }
    });

    GocatorGraphicsFrameStream::RejectedRange rejectedRange;
    if (_rangeRejectionSink) {
        rejectedRange = [this](const GocatorRangeRejection& rejection, const std::size_t sequence) {
            reportRangeRejection(rejection, sequence);
        };
    }
    GocatorGraphicsFrameStream::RejectedImage rejectedImage;
    if (_imageRejectionSink) {
        rejectedImage = [this](const GocatorImageRejection& rejection, const std::size_t sequence) {
            reportImageRejection(rejection, sequence);
        };
    }
    _graphicsStream = std::make_unique<GocatorGraphicsFrameStream>(
        _gocator,
        [this](GraphicsFrame&& payload, const unsigned int sourceIndex) {
            if (!_frameConsumer) return;
            SessionFrame frame;
            frame.payload = std::move(payload);
            frame.frameSeq = _frameSeq.fetch_add(1, std::memory_order_relaxed);
            _frameConsumer(std::move(frame), sourceIndex);
            noteAcquisitionFrame();
        }, std::move(rejectedRange), std::move(rejectedImage));
    if (_frameStorage) _graphicsStream->setFrameStorageProvider(_frameStorage);
}

void GocatorSourceController::reportRangeRejection(
    const GocatorRangeRejection& rejection, const std::size_t frameSequence) noexcept
{
    using Reason = GocatorRangeRejection::Reason;
    DeviceRangeRejectionObservation observation;
    switch (rejection.reason) {
    case Reason::None: return;
    case Reason::MissingRange: observation.reason = DeviceRangeRejection::MissingRange; break;
    case Reason::MissingData: observation.reason = DeviceRangeRejection::MissingData; break;
    case Reason::InvalidDimensions: observation.reason = DeviceRangeRejection::InvalidDimensions; break;
    case Reason::UnsupportedFormat: observation.reason = DeviceRangeRejection::UnsupportedFormat; break;
    case Reason::TruncatedData: observation.reason = DeviceRangeRejection::TruncatedData; break;
    case Reason::InvalidLayout: observation.reason = DeviceRangeRejection::InvalidLayout; break;
    case Reason::SizeLimitExceeded: observation.reason = DeviceRangeRejection::SizeLimitExceeded; break;
    case Reason::StorageRefused: observation.reason = DeviceRangeRejection::StorageRefused; break;
    case Reason::StorageMisaligned: observation.reason = DeviceRangeRejection::StorageMisaligned; break;
    }
    if (!_singleInFlight.load(std::memory_order_relaxed)) {
        auto& counter = _rangeRejections[static_cast<std::size_t>(observation.reason)];
        const auto count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count != 1 && count % 100 != 0) return;
    }
    observation.partCount = rejection.partCount;
    observation.affectedPart = rejection.part;
    observation.width = rejection.width;
    observation.height = rejection.height;
    observation.frameSequence = frameSequence;
    observation.requestedStorageBytes = rejection.requestedStorageBytes;
    observation.frameRetained = rejection.frameRetained;
    observation.imageRetained = rejection.imageRetained;
    observation.rangeRetained = rejection.rangeRetained;
    _rangeRejectionSink(observation);
}

void GocatorSourceController::reportImageRejection(
    const GocatorImageRejection& rejection, const std::size_t frameSequence) noexcept
{
    if (!_imageRejectionSink) return;
    const auto reason = deviceImageRejection(rejection.reason);
    const bool single = _singleInFlight.load(std::memory_order_relaxed);
    auto& counter = _imageRejections[static_cast<std::size_t>(reason)];
    const auto count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!single && count != 1 && count % 100 != 0) return;

    DeviceImageRejectionObservation observation;
    observation.reason = reason;
    observation.conversion = DeviceImageConversion::Copy;
    observation.sourceIndex = 0U;
    observation.width = rejection.width;
    observation.height = rejection.height;
    observation.frameSequence = frameSequence;
    observation.pixelFormat = rejection.pixelFormat;
    observation.reportedStride = rejection.reportedStride;
    observation.effectiveStride = rejection.effectiveStride;
    observation.minimumRowBytes = rejection.minimumRowBytes;
    observation.readableBytes = rejection.readableBytes;
    observation.requiredBytes = rejection.requiredBytes;
    _imageRejectionSink(observation);
}

void GocatorSourceController::deregisterCallbacks() {
    if (!_gocator) return;

    _statusCallbackGate.beginShutdown();
    if (_statusCallbackId != 0) {
        _gocator->deregisterStatusCallback(_statusCallbackId);
        _statusCallbackId = 0;
    }
    _statusCallbackGate.waitForDrain();
    _graphicsStream.reset();
}

std::optional<SourceRuntimeDiagnostics> GocatorSourceController::sourceRuntimeDiagnostics(unsigned sourceIndex) const
{
    if (sourceIndex != 0 || !_gocator) return std::nullopt;
    const auto value = _gocator->runtimeDiagnostics();
    return SourceRuntimeDiagnostics{value.grabbing, value.delivered, value.failed};
}

DeviceConnectionState GocatorSourceController::connectionState() const
{
    DeviceConnectionState state;
    if (!_lifecycleAvailable || !_lifecycle.snapshot) return state;
    const auto snapshot = _lifecycle.snapshot();
    const auto diagnostics = _lifecycle.diagnostics
        ? _lifecycle.diagnostics() : GocatorRuntimeDiagnostics{};
    state.deliveredFrames = diagnostics.delivered;
    if (!snapshot.opened) return state;

    state.connected = true;
    state.connectionId = currentConnectionId(snapshot.generation);
    state.targetId = snapshot.targetId;
    state.live = diagnostics.grabbing;
    const auto target = cachedTarget(state.targetId);
    state.displayName = target
        ? Gocator::formatDeviceName(target->model, target->serial, state.targetId, target->isVirtual)
        : state.targetId;
    return state;
}

std::string GocatorSourceController::currentConnectionId(std::uint64_t generation) const
{
    return _parameterIdentity + ":" + std::to_string(generation);
}

std::optional<Gocator::DeviceInfo> GocatorSourceController::cachedTarget(const std::string& targetId) const
{
    if (!_lifecycle.cachedDevices) return std::nullopt;
    for (const auto& device : normalizedDevices(_lifecycle.cachedDevices()))
        if (gocator::targetIdForDevice(device) == targetId) return device;
    return std::nullopt;
}

DeviceConnectionCapabilities GocatorSourceController::connectionCapabilities() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!_lifecycleAvailable) {
        const DeviceConnectionAvailability ended{false, "The Gocator session has ended."};
        return {ended, ended, ended, ended, ended, ended};
    }
    const DeviceConnectionAvailability available{true, {}};
    return {available, available, available, available, available, available};
}

DeviceConnectionResult GocatorSourceController::requestConnectionTargets(TargetsCompletion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!completion) return refused(DeviceConnectionError::InvalidArgument, "Completion is required.");
    if (!_lifecycleAvailable || !_lifecycle.cachedDevices)
        return refused(DeviceConnectionError::Unavailable, "The Gocator session has ended.");
    try {
        completeConnection(completion, copiedTargets(_lifecycle.cachedDevices()));
    } catch (const std::exception& error) {
        return refused(DeviceConnectionError::BackendFailure, error.what());
    }
    return {};
}

DeviceConnectionResult GocatorSourceController::requestConnectionState(IDeviceConnectionControl::Completion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!completion) return refused(DeviceConnectionError::InvalidArgument, "Completion is required.");
    if (!_lifecycleAvailable)
        return refused(DeviceConnectionError::Unavailable, "The Gocator session has ended.");
    try {
        completeConnection(completion, DeviceConnectionResult{{}, {}, DeviceConnectionEffect::None, connectionState()});
    } catch (const std::exception& error) {
        return refused(DeviceConnectionError::BackendFailure, error.what());
    }
    return {};
}

DeviceConnectionResult GocatorSourceController::requestDiscover(TargetsCompletion completion)
{
    return admitLifecycle(Lifecycle::Discover, {}, {}, std::move(completion));
}

DeviceConnectionResult GocatorSourceController::requestConnect(std::string targetId,
    IDeviceConnectionControl::Completion completion)
{
    return admitLifecycle(Lifecycle::Connect, std::move(targetId), std::move(completion), {});
}

DeviceConnectionResult GocatorSourceController::requestDisconnect(std::string connectionId,
    IDeviceConnectionControl::Completion completion)
{
    return admitLifecycle(Lifecycle::Disconnect, std::move(connectionId), std::move(completion), {});
}

DeviceConnectionResult GocatorSourceController::requestSingle(std::string connectionId,
    IDeviceConnectionControl::Completion completion)
{
    return admitLifecycle(Lifecycle::Single, std::move(connectionId), std::move(completion), {});
}

DeviceConnectionResult GocatorSourceController::requestLiveStart(std::string connectionId,
    IDeviceConnectionControl::Completion completion)
{
    return admitLifecycle(Lifecycle::LiveStart, std::move(connectionId), std::move(completion), {});
}

DeviceConnectionResult GocatorSourceController::requestLiveStop(std::string connectionId,
    IDeviceConnectionControl::Completion completion)
{
    return admitLifecycle(Lifecycle::LiveStop, std::move(connectionId), std::move(completion), {});
}

DeviceConnectionResult GocatorSourceController::admitLifecycle(Lifecycle operation, std::string argument,
    IDeviceConnectionControl::Completion completion, TargetsCompletion targetsCompletion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const bool discover = operation == Lifecycle::Discover;
    const bool single = operation == Lifecycle::Single;
    const bool liveStart = operation == Lifecycle::LiveStart;
    const bool liveStop = operation == Lifecycle::LiveStop;
    if (discover ? !targetsCompletion : !completion)
        return refused(DeviceConnectionError::InvalidArgument, "Completion is required.");
    if (!_lifecycleAvailable || !_lifecycle.snapshot || !_lifecycle.beginControl || !_lifecycle.endControl
        || ((single || liveStart) && !_lifecycle.grab)
        || (liveStop && !_lifecycle.stop))
        return refused(DeviceConnectionError::Unavailable, "The Gocator session has ended.");
    if (_parameterBusy.load())
        return refused(DeviceConnectionError::Busy, "A parameter operation is active.");

    const auto snapshot = _lifecycle.snapshot();
    const auto diagnostics = _lifecycle.diagnostics
        ? _lifecycle.diagnostics() : GocatorRuntimeDiagnostics{};
    const bool grabbing = diagnostics.grabbing || _isGrabbing.load();
    const bool acquiring = grabbing || _acquisitionStarting.load();
    if (discover) {
        if (snapshot.opened)
            return refused(DeviceConnectionError::Busy, "Disconnect before discovery.");
        if (acquiring)
            return refused(DeviceConnectionError::Busy, "Stop live acquisition before discovery.");
    } else if (operation == Lifecycle::Connect) {
        if (snapshot.opened)
            return refused(DeviceConnectionError::Conflict, "Disconnect before connecting another device.");
        if (argument.empty() || argument.find('\0') != std::string::npos)
            return refused(DeviceConnectionError::InvalidArgument, "An exact target is required.");
        if (acquiring)
            return refused(DeviceConnectionError::Busy, "Stop live acquisition before connecting.");
    } else {
        if (!snapshot.opened)
            return refused(DeviceConnectionError::Disconnected, "Device is disconnected.");
        if (argument.empty() || argument.find('\0') != std::string::npos)
            return refused(DeviceConnectionError::InvalidArgument, "An exact connectionId is required.");
        if (currentConnectionId(snapshot.generation) != argument)
            return refused(DeviceConnectionError::Conflict, "The connection changed; observe it again.");
        if (single) {
            if (_singleInFlight.load())
                return refused(DeviceConnectionError::Busy, "A single grab is already in flight.");
            if (_acquisitionStarting.load())
                return refused(DeviceConnectionError::Busy, "Live acquisition is starting.");
            if (grabbing)
                return refused(DeviceConnectionError::Busy, "Stop Live before a single grab.");
        } else if (liveStart) {
            if (_singleInFlight.load())
                return refused(DeviceConnectionError::Busy, "A single grab is in flight.");
            if (grabbing)
                return refused(DeviceConnectionError::Conflict, "Live acquisition is already running.");
            if (_acquisitionStarting.load())
                return refused(DeviceConnectionError::Busy, "Live acquisition is already starting.");
        } else if (liveStop) {
            if (_singleInFlight.load() || _acquisitionStarting.load() || !grabbing)
                return refused(DeviceConnectionError::Conflict, "Live acquisition is not running.");
        } else if (acquiring) {
            return refused(DeviceConnectionError::Busy, "Stop live acquisition before disconnecting.");
        }
    }

    const bool reserveAcquisition = single || liveStart;
    if (reserveAcquisition) {
        bool expected = false;
        if (!_acquisitionStarting.compare_exchange_strong(expected, true))
            return refused(DeviceConnectionError::Busy,
                single ? "Live acquisition is starting." : "Live acquisition is already starting.");
    }
    if (!_lifecycle.beginControl()) {
        if (reserveAcquisition) _acquisitionStarting = false;
        return refused(DeviceConnectionError::Busy, "Another device control operation is active.");
    }
    std::shared_ptr<SingleRun> singleRun;
    if (single) {
        try {
            singleRun = std::make_shared<SingleRun>();
        } catch (...) {
            _acquisitionStarting = false;
            _lifecycle.endControl();
            return refused(DeviceConnectionError::BackendFailure,
                "Could not allocate the single acquisition state.");
        }
        std::lock_guard<std::mutex> lock(_singleMutex);
        _singleRun = singleRun;
        _singleInFlight = true;
    }
    try {
        _controlWork = QtConcurrent::run([this, operation, argument = std::move(argument),
            completion = std::move(completion), targetsCompletion = std::move(targetsCompletion),
            singleRun] {
            const auto copiedState = [this]() -> std::optional<DeviceConnectionState> {
                try { return connectionState(); } catch (...) { return std::nullopt; }
            };
            DeviceConnectionResult result;
            DeviceConnectionTargets discovered;
            try {
                if (operation == Lifecycle::Discover) {
                    std::string error;
                    const auto devices = _lifecycle.discover ? _lifecycle.discover(&error)
                        : std::vector<Gocator::DeviceInfo>{};
                    if (!error.empty()) {
                        discovered.result = refused(DeviceConnectionError::BackendFailure, error);
                    } else {
                        discovered = copiedTargets(devices);
                        result = {{}, {}, DeviceConnectionEffect::None, copiedState()};
                    }
                } else if (operation == Lifecycle::Connect) {
                    std::string error;
                    if (_lifecycle.open && _lifecycle.open(argument, &error))
                        result = {{}, {}, DeviceConnectionEffect::Applied, copiedState()};
                    else
                        result = {DeviceConnectionError::BackendFailure,
                            error.empty() ? "The Gocator connection could not be opened." : error,
                            DeviceConnectionEffect::None, copiedState()};
                } else if (operation == Lifecycle::Disconnect) {
                    if (_lifecycle.close) _lifecycle.close();
                    const bool opened = _lifecycle.snapshot && _lifecycle.snapshot().opened;
                    result = opened
                        ? DeviceConnectionResult{DeviceConnectionError::BackendFailure,
                            "The Gocator connection could not be closed.", DeviceConnectionEffect::Unknown, copiedState()}
                        : DeviceConnectionResult{{}, {}, DeviceConnectionEffect::Applied, copiedState()};
                } else if (operation == Lifecycle::Single) {
                    bool stopped = false;
                    {
                        std::lock_guard<std::mutex> lock(singleRun->mutex);
                        stopped = singleRun->stopped;
                    }
                    bool started = false;
                    if (!stopped) {
                        try {
                            started = beginGrab(1);
                        } catch (...) {
                            _acquisitionStarting = false;
                            throw;
                        }
                    }
                    _acquisitionStarting = false;
                    bool delivered = false;
                    bool ended = false;
                    {
                        std::unique_lock<std::mutex> lock(singleRun->mutex);
                        if (started || singleRun->delivered)
                            singleRun->condition.wait_for(lock, std::chrono::seconds(30),
                                [&] { return singleRun->ended || singleRun->stopped; });
                        stopped = singleRun->stopped;
                        delivered = singleRun->delivered;
                        ended = singleRun->ended;
                    }
                    if (stopped && started && !ended) stop();
                    if (!started && !delivered && !ended && !stopped) {
                        result = {DeviceConnectionError::Unavailable,
                            "The Gocator could not start a single grab.",
                            DeviceConnectionEffect::None, copiedState()};
                    } else if (!ended && !stopped) {
                        stop();
                        result = {DeviceConnectionError::Timeout,
                            "Single acquisition did not finish within 30 s; accepted effects may have occurred.",
                            DeviceConnectionEffect::Applied, copiedState()};
                    } else {
                        const auto diagnostics = _lifecycle.diagnostics
                            ? _lifecycle.diagnostics() : GocatorRuntimeDiagnostics{};
                        const bool acquisitionOccurred = started || delivered || ended;
                        if (acquisitionOccurred && diagnostics.stopFailed) {
                            result = {DeviceConnectionError::BackendFailure,
                                diagnostics.stopError.empty() ? "Gocator stop failed." : diagnostics.stopError,
                                DeviceConnectionEffect::Unknown, copiedState()};
                        } else {
                            result = delivered
                                ? DeviceConnectionResult{{}, {}, DeviceConnectionEffect::Applied, copiedState()}
                                : DeviceConnectionResult{DeviceConnectionError::BackendFailure,
                                    stopped ? "Single acquisition was stopped before a frame was delivered."
                                            : "Single acquisition ended without delivering a frame.",
                                    acquisitionOccurred ? DeviceConnectionEffect::Applied : DeviceConnectionEffect::None,
                                    copiedState()};
                        }
                    }
                } else if (operation == Lifecycle::LiveStart) {
                    result = beginLive()
                        ? DeviceConnectionResult{{}, {}, DeviceConnectionEffect::Applied, copiedState()}
                        : DeviceConnectionResult{DeviceConnectionError::Unavailable,
                            "The Gocator could not start live acquisition.",
                            DeviceConnectionEffect::None, copiedState()};
                } else {
                    stop();
                    result = {{}, {}, DeviceConnectionEffect::Applied, copiedState()};
                }
            } catch (const std::exception& error) {
                if (operation == Lifecycle::Discover)
                    discovered.result = refused(DeviceConnectionError::BackendFailure, error.what());
                else {
                    const auto* grabError = dynamic_cast<const Gocator::GrabError*>(&error);
                    result = {DeviceConnectionError::BackendFailure, error.what(),
                        grabError && !grabError->mayHaveEffect()
                            ? DeviceConnectionEffect::None : DeviceConnectionEffect::Unknown,
                        copiedState()};
                }
            } catch (...) {
                if (operation == Lifecycle::Discover)
                    discovered.result = refused(DeviceConnectionError::BackendFailure,
                        "Unexpected device backend failure.");
                else
                    result = {DeviceConnectionError::BackendFailure,
                        "Unexpected device backend failure.", DeviceConnectionEffect::Unknown, copiedState()};
            }

            if (operation == Lifecycle::Single) finishSingle(singleRun);
            _lifecycle.endControl();
            if (operation == Lifecycle::Discover) {
                if (discovered.result.error == DeviceConnectionError::None) emit inventoryChanged();
                completeConnection(targetsCompletion, std::move(discovered));
            } else {
                completeConnection(completion, std::move(result));
            }
        });
    } catch (...) {
        if (single) finishSingle(singleRun);
        if (reserveAcquisition) _acquisitionStarting = false;
        _lifecycle.endControl();
        return refused(DeviceConnectionError::BackendFailure, "Could not schedule device control work.");
    }
    return {};
}
