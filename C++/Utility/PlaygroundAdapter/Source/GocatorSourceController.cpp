#include "GocatorSourceController.h"
#include <QUuid>
#include <QtConcurrent>
#include <stdexcept>
#include "engine/GraphicsFrame.h"
#include "SessionFrame.h"
#include "Utility/PlaygroundAdapter/GocatorGraphicsFrameStream.h"

namespace {

DeviceConnectionResult refused(DeviceConnectionError error, std::string message)
{
    return {error, std::move(message)};
}

DeviceConnectionTargets copiedTargets(const std::vector<Gocator::DeviceInfo>& devices)
{
    DeviceConnectionTargets result;
    result.targets.reserve(devices.size());
    for (const auto& device : devices) {
        result.targets.push_back({
            device.address,
            Gocator::formatDeviceName(device.model, device.serial, device.address, device.isVirtual),
            {{"model", device.model}, {"serial", device.serial}, {"virtual", device.isVirtual ? "true" : "false"}}});
    }
    return result;
}

template<class Callback, class Value>
void completeConnection(const Callback& callback, Value value)
{
    try { callback(std::move(value)); } catch (...) {}
}

} // namespace

GocatorSourceController::GocatorSourceController(Gocator* gocator, QObject* parent)
    : AbstractSourceController(parent), _gocator(gocator) {
    _parameterIdentity = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    _readSystem = [gocator] { return gocator->systemState(); };
    _writeQuickEdit = [gocator](bool enabled, std::optional<Gocator::SystemState> expected) {
        gocator->setQuickEditEnabled(enabled, expected);
    };
    _systemConnected = [gocator] { return gocator && gocator->isOpened(); };
    if (gocator) {
        _lifecycleAvailable = true;
        _lifecycle.snapshot = [gocator] { return gocator->connectionSnapshot(); };
        _lifecycle.cachedDevices = [gocator] { return gocator->cachedDevices(); };
        _lifecycle.discover = [gocator](std::string* error) { return gocator->discoverDevices(error); };
        _lifecycle.open = [gocator](const std::string& targetId) { return gocator->open(targetId); };
        _lifecycle.close = [gocator] { gocator->close(); };
        _lifecycle.beginControl = [gocator] { return gocator->tryBeginControl(); };
        _lifecycle.endControl = [gocator] { gocator->endControl(); };
        _lifecycle.diagnostics = [gocator] { return gocator->runtimeDiagnostics(); };
    }
    registerCallbacks();
}

GocatorSourceController::~GocatorSourceController() {
    _parameterWork.waitForFinished();
    _controlWork.waitForFinished();
    stop();
    deregisterCallbacks();
}

void GocatorSourceController::start() {
    if (_gocator) {
        _gocator->grab();
    }
}

void GocatorSourceController::stop() {
    if (_gocator) {
        _gocator->stop();
        _isGrabbing = false;
    }
}

bool GocatorSourceController::isGrabbing() const {
    return _isGrabbing;
}

void GocatorSourceController::setFrameConsumer(FrameConsumer consumer) {
    _frameConsumer = std::move(consumer);
}

void GocatorSourceController::registerCallbacks() {
    if (!_gocator) return;

    // 1. Status callback
    _statusCallbackId = _gocator->registerStatusCallback([this](Gocator::Status status, bool on) {
        if (status == Gocator::GrabbingStatus) {
            _isGrabbing = on;
            emit diagnosticAcquisitionChanged(on, QStringLiteral("gocator"));
        }
    });

    _graphicsStream = std::make_unique<GocatorGraphicsFrameStream>(
        _gocator,
        [this](GraphicsFrame&& payload, const unsigned int sourceIndex) {
            if (!_frameConsumer) return;
            SessionFrame frame;
            frame.payload = std::move(payload);
            frame.frameSeq = _frameSeq.fetch_add(1, std::memory_order_relaxed);
            _frameConsumer(std::move(frame), sourceIndex);
        });
}

void GocatorSourceController::deregisterCallbacks() {
    if (!_gocator) return;

    _graphicsStream.reset();

    if (_statusCallbackId != 0) {
        _gocator->deregisterStatusCallback(_statusCallbackId);
        _statusCallbackId = 0;
    }
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
    state.targetId = snapshot.address;
    state.live = diagnostics.grabbing;
    const auto target = cachedTarget(snapshot.address);
    state.displayName = target
        ? Gocator::formatDeviceName(target->model, target->serial, target->address, target->isVirtual)
        : snapshot.address;
    return state;
}

std::string GocatorSourceController::currentConnectionId(std::uint64_t generation) const
{
    return _parameterIdentity + ":" + std::to_string(generation);
}

std::optional<Gocator::DeviceInfo> GocatorSourceController::cachedTarget(const std::string& targetId) const
{
    if (!_lifecycle.cachedDevices) return std::nullopt;
    for (const auto& device : _lifecycle.cachedDevices())
        if (device.address == targetId) return device;
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
    const DeviceConnectionAvailability acquisition{false,
        "Gocator acquisition uses source.start and source.stop."};
    return {available, available, available, available, acquisition, acquisition};
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

DeviceConnectionResult GocatorSourceController::admitLifecycle(Lifecycle operation, std::string argument,
    IDeviceConnectionControl::Completion completion, TargetsCompletion targetsCompletion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const bool discover = operation == Lifecycle::Discover;
    if (discover ? !targetsCompletion : !completion)
        return refused(DeviceConnectionError::InvalidArgument, "Completion is required.");
    if (!_lifecycleAvailable || !_lifecycle.snapshot || !_lifecycle.beginControl || !_lifecycle.endControl)
        return refused(DeviceConnectionError::Unavailable, "The Gocator session has ended.");
    if (_parameterBusy.load())
        return refused(DeviceConnectionError::Busy, "A parameter operation is active.");

    const auto snapshot = _lifecycle.snapshot();
    const auto diagnostics = _lifecycle.diagnostics
        ? _lifecycle.diagnostics() : GocatorRuntimeDiagnostics{};
    if (discover) {
        if (snapshot.opened)
            return refused(DeviceConnectionError::Busy, "Disconnect before discovery.");
        if (diagnostics.grabbing)
            return refused(DeviceConnectionError::Busy, "Stop live acquisition before discovery.");
    } else if (operation == Lifecycle::Connect) {
        if (snapshot.opened)
            return refused(DeviceConnectionError::Conflict, "Disconnect before connecting another device.");
        if (argument.empty() || argument.find('\0') != std::string::npos)
            return refused(DeviceConnectionError::InvalidArgument, "An exact targetId is required.");
        if (!cachedTarget(argument))
            return refused(DeviceConnectionError::NotFound, "Exact declared target was not found.");
        if (diagnostics.grabbing)
            return refused(DeviceConnectionError::Busy, "Stop live acquisition before connecting.");
    } else {
        if (!snapshot.opened)
            return refused(DeviceConnectionError::Disconnected, "Device is disconnected.");
        if (argument.empty() || argument.find('\0') != std::string::npos)
            return refused(DeviceConnectionError::InvalidArgument, "An exact connectionId is required.");
        if (currentConnectionId(snapshot.generation) != argument)
            return refused(DeviceConnectionError::Conflict, "The connection changed; observe it again.");
        if (diagnostics.grabbing)
            return refused(DeviceConnectionError::Busy, "Stop live acquisition before disconnecting.");
    }

    if (!_lifecycle.beginControl())
        return refused(DeviceConnectionError::Busy, "Another device control operation is active.");
    try {
        _controlWork = QtConcurrent::run([this, operation, argument = std::move(argument),
            completion = std::move(completion), targetsCompletion = std::move(targetsCompletion)] {
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
                    if (_lifecycle.open && _lifecycle.open(argument))
                        result = {{}, {}, DeviceConnectionEffect::Applied, copiedState()};
                    else
                        result = {DeviceConnectionError::BackendFailure,
                            "The Gocator connection could not be opened.", DeviceConnectionEffect::None, copiedState()};
                } else {
                    if (_lifecycle.close) _lifecycle.close();
                    const bool opened = _lifecycle.snapshot && _lifecycle.snapshot().opened;
                    result = opened
                        ? DeviceConnectionResult{DeviceConnectionError::BackendFailure,
                            "The Gocator connection could not be closed.", DeviceConnectionEffect::Unknown, copiedState()}
                        : DeviceConnectionResult{{}, {}, DeviceConnectionEffect::Applied, copiedState()};
                }
            } catch (const std::exception& error) {
                if (operation == Lifecycle::Discover)
                    discovered.result = refused(DeviceConnectionError::BackendFailure, error.what());
                else
                    result = {DeviceConnectionError::BackendFailure, error.what(),
                        DeviceConnectionEffect::Unknown, copiedState()};
            } catch (...) {
                if (operation == Lifecycle::Discover)
                    discovered.result = refused(DeviceConnectionError::BackendFailure,
                        "Unexpected device backend failure.");
                else
                    result = {DeviceConnectionError::BackendFailure,
                        "Unexpected device backend failure.", DeviceConnectionEffect::Unknown, copiedState()};
            }

            _lifecycle.endControl();
            if (operation == Lifecycle::Discover) {
                if (discovered.result.error == DeviceConnectionError::None) emit inventoryChanged();
                completeConnection(targetsCompletion, std::move(discovered));
            } else {
                completeConnection(completion, std::move(result));
            }
        });
    } catch (...) {
        _lifecycle.endControl();
        return refused(DeviceConnectionError::BackendFailure, "Could not schedule device control work.");
    }
    return {};
}
