#include "GocatorSourceController.h"
#include "GocatorSystemParameters.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

std::string scopeKey(const std::string& scope, const std::string& name)
{
    return scope + '\x1f' + name;
}

bool parseResource(const std::string& schemaText, const std::string& dataText,
    QJsonObject* schema, QJsonObject* data, std::string* error)
{
    QJsonParseError parse{};
    const auto schemaDocument = QJsonDocument::fromJson(QByteArray::fromStdString(schemaText), &parse);
    if (parse.error != QJsonParseError::NoError || !schemaDocument.isObject()) {
        if (error) *error = "The GoPxL resource schema is not a JSON object.";
        return false;
    }
    const auto dataDocument = QJsonDocument::fromJson(QByteArray::fromStdString(dataText), &parse);
    if (parse.error != QJsonParseError::NoError || !dataDocument.isObject()) {
        if (error) *error = "The GoPxL resource data is not a JSON object.";
        return false;
    }
    *schema = schemaDocument.object();
    *data = dataDocument.object();
    return true;
}

} // namespace

std::vector<DeviceParameterDescriptor> GocatorSourceController::observeSystem(
    const Gocator::SystemState& state, const std::string& connectionId)
{
    std::lock_guard<std::mutex> lock(_observationMutex);
    if (!_lastSystemState || _lastSystemState->connectionGeneration != state.connectionGeneration
        || _lastSystemState->running != state.running
        || _lastSystemState->quickEditEnabled != state.quickEditEnabled)
        ++_parameterRevision;
    _lastSystemState = state;
    auto parameters = gocator::systemParameters(state, _parameterIdentity, _parameterRevision);
    for (auto& parameter : parameters) parameter.target.connectionId = connectionId;
    return parameters;
}

GocatorSourceController::Observation GocatorSourceController::observeAll(bool snapshot)
{
    Observation observation;
    {
        std::lock_guard<std::mutex> cycle(_observationCycleMutex);
        if (!_systemConnected || !_systemConnected()) throw std::logic_error("Gocator is disconnected.");

        const auto state = _readSystem();
        std::uint64_t generation = state.connectionGeneration;
        if (_lifecycleAvailable && _lifecycle.snapshot) {
            const auto snapshot = _lifecycle.snapshot();
            if (!snapshot.opened) throw std::logic_error("Gocator is disconnected.");
            generation = snapshot.generation;
        }

        observation.connectionId = currentConnectionId(generation);
        {
            std::lock_guard<std::mutex> lock(_observationMutex);
            if (_lastConnectionId != observation.connectionId) {
                // Revisions belong to one connection generation; a reconnect starts a fresh tree.
                _observedParameters.clear();
                _publishedParameters.clear();
                _lastSystemState.reset();
                _parameterRevision = 0;
                _lastConnectionId = observation.connectionId;
            }
        }

        observation.systemState = state;
        auto system = observeSystem(state, observation.connectionId);
        observation.parameters.insert(observation.parameters.end(), system.begin(), system.end());

        observeResource(&observation, Gocator::ParameterTarget::Scanner, "Scanner",
            observation.connectionId);
        observeResource(&observation, Gocator::ParameterTarget::Sensor, "Sensor",
            observation.connectionId);

        {
            std::lock_guard<std::mutex> lock(_observationMutex);
            _lastCatalog = observation.parameters;
        }
        // Every completed observation publishes what changed since the last published catalog,
        // even when it came from inventory, a read or a write readback. A change is therefore
        // never consumed silently by an observation that does not deliver.
        publishDifferencesLocked(observation);
        // The initial snapshot is queued under the same cycle lock as the catalog copy, so no
        // later observation can change the catalog between the snapshot build and its push.
        if (snapshot) _notifications.publishSnapshot(observation.connectionId);
    }
    _notifications.drain();
    return observation;
}

void GocatorSourceController::observeResource(Observation* observation,
    Gocator::ParameterTarget target, const std::string& scope, const std::string& connectionId)
{
    if (!observation) return;
    if (!_readResource) return;

    ResourceSnapshot snapshot;
    std::string readError;
    try {
        snapshot = _readResource(target);
    } catch (const std::exception& error) {
        readError = error.what();
    } catch (...) {
        readError = "Unexpected GoPxL resource observation failure.";
    }

    std::vector<gocator::ResourceParameter> flattened;
    if (readError.empty()) {
        QJsonObject schema;
        QJsonObject data;
        if (parseResource(snapshot.schema, snapshot.data, &schema, &data, &readError)) {
            flattened = gocator::flattenResource(schema, data, scope, connectionId);
        }
    }
    if (!readError.empty()) throw std::runtime_error(readError);

    std::vector<std::string> seen;
    {
        std::lock_guard<std::mutex> lock(_observationMutex);
        seen.reserve(flattened.size());
        for (auto& parameter : flattened) {
            const auto key = scopeKey(scope, parameter.descriptor.target.name);
            seen.push_back(key);
            auto& cached = _observedParameters[key];
            const bool changed = !cached.present || cached.rawValue != parameter.rawValue
                || cached.writable != parameter.descriptor.writable;
            // A removed leaf keeps its last revision so a reappearance can never reuse an old
            // revision and pass an expectedRevision fence (ABA).
            if (changed) cached.revision = cached.revision == 0 ? 1 : cached.revision + 1;
            cached.present = true;
            cached.rawValue = parameter.rawValue;
            cached.writable = parameter.descriptor.writable;
            parameter.descriptor.revision = cached.revision;
            observation->parameters.push_back(parameter.descriptor);
            observation->resourceParameters.push_back(parameter);
        }
        const auto prefix = scopeKey(scope, {});
        for (auto& entry : _observedParameters) {
            if (entry.first.compare(0, prefix.size(), prefix) != 0 || !entry.second.present) continue;
            if (std::find(seen.begin(), seen.end(), entry.first) != seen.end()) continue;
            entry.second.present = false;
        }
    }
}

DeviceParameterChange GocatorSourceController::snapshotChange(const std::string& connectionId,
    const std::string& subscriptionId) const
{
    DeviceParameterChange change;
    change.kind = DeviceParameterChangeKind::Snapshot;
    change.connectionId = connectionId;
    change.subscriptionId = subscriptionId;
    std::lock_guard<std::mutex> lock(_observationMutex);
    change.descriptors = _lastCatalog;
    for (const auto& descriptor : change.descriptors)
        change.revision = std::max(change.revision, descriptor.revision);
    return change;
}

void GocatorSourceController::publishDifferencesLocked(const Observation& observation)
{
    std::map<std::string, DeviceParameterDescriptor> current;
    for (const auto& descriptor : observation.parameters) {
        current.insert_or_assign(descriptor.target.scope + '\x1f' + descriptor.target.name,
            descriptor);
    }

    if (!_notifications.active()) {
        _publishedParameters = std::move(current);
        return;
    }

    std::vector<DeviceParameterDescriptor> changed;
    std::vector<DeviceParameterTarget> invalidated;
    for (const auto& entry : current) {
        const auto published = _publishedParameters.find(entry.first);
        if (published == _publishedParameters.end()
            || published->second.revision != entry.second.revision)
            changed.push_back(entry.second);
    }
    for (const auto& entry : _publishedParameters) {
        if (!current.count(entry.first)) invalidated.push_back(entry.second.target);
    }
    _publishedParameters = std::move(current);
    if (changed.empty() && invalidated.empty()) return;

    if (changed.size() > deviceParameterDeltaDescriptorLimit
        || invalidated.size() > deviceParameterDeltaInvalidationLimit) {
        // A whole scope can disappear at once; publish a complete snapshot instead of a delta
        // that would exceed the contract bound and terminate the subscription.
        _notifications.publishSnapshot(observation.connectionId);
    } else {
        _notifications.publishDelta(observation.connectionId, changed, invalidated);
    }
}

DeviceParameterResult GocatorSourceController::requestParameterChanges(std::string connectionId,
    std::string subscriptionId, ChangeListener delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (validateDeviceParameterSubscription(connectionId, subscriptionId)
        != DeviceParameterError::None || !delivery) {
        return {DeviceParameterError::InvalidArgument,
            "The Gocator change subscription identity is invalid.",
            DeviceParameterEffect::None, std::nullopt};
    }
    if (!_systemConnected || !_systemConnected())
        return {DeviceParameterError::Disconnected, "Gocator is disconnected.",
            DeviceParameterEffect::None, std::nullopt};
    if (!_lifecycleAvailable || !_lifecycle.snapshot)
        return {DeviceParameterError::Unavailable, "The Gocator session has ended.",
            DeviceParameterEffect::None, std::nullopt};
    const auto snapshot = _lifecycle.snapshot();
    if (!snapshot.opened)
        return {DeviceParameterError::Disconnected, "Gocator is disconnected.",
            DeviceParameterEffect::None, std::nullopt};
    if (currentConnectionId(snapshot.generation) != connectionId)
        return {DeviceParameterError::Conflict,
            "The connection generation changed; observe it again.",
            DeviceParameterEffect::None, std::nullopt};

    const auto admitted = _notifications.add(connectionId, subscriptionId, std::move(delivery));
    if (admitted.error != DeviceParameterError::None) return admitted;
    {
        std::lock_guard<std::mutex> lock(_observationMutex);
        _notificationConnectionId = connectionId;
    }

    {
        std::lock_guard<std::mutex> lock(_observationThreadMutex);
        _observationSnapshotPending = true;
    }
    try {
        startObservationThread();
    } catch (...) {
        _notifications.remove(connectionId, subscriptionId);
        return {DeviceParameterError::BackendFailure,
            "The Gocator observation thread could not start.",
            DeviceParameterEffect::None, std::nullopt};
    }
    return {};
}

DeviceParameterResult GocatorSourceController::releaseParameterChanges(std::string connectionId,
    std::string subscriptionId, ParameterCompletion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!completion)
        return {DeviceParameterError::InvalidArgument, "A completion is required.",
            DeviceParameterEffect::None, std::nullopt};
    const auto removed = _notifications.remove(connectionId, subscriptionId);
    if (removed.error != DeviceParameterError::None) return removed;
    wakeObservationThread();
    try { completion({}); } catch (...) {}
    return {};
}

void GocatorSourceController::startObservationThread()
{
    std::thread previous;
    {
        std::lock_guard<std::mutex> lock(_observationThreadMutex);
        if (_observationRunning) {
            _observationWake.notify_all();
            return;
        }
        // The loop returned but its thread object is still joinable; joining it here, outside the
        // lock, prevents a move-assign onto a joinable std::thread (std::terminate).
        if (_observationThread.joinable()
            && _observationThread.get_id() != std::this_thread::get_id())
            previous = std::move(_observationThread);
        try {
            _observationThread = std::thread([this] { observationLoop(); });
        } catch (...) {
            _observationRunning = false;
            if (previous.joinable()) previous.join();
            throw;
        }
        _observationStop = false;
        _observationRunning = true;
    }
    if (previous.joinable()) previous.join();
}

void GocatorSourceController::wakeObservationThread()
{
    std::lock_guard<std::mutex> lock(_observationThreadMutex);
    _observationWake.notify_all();
}

void GocatorSourceController::stopObservationThread()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(_observationThreadMutex);
        _observationStop = true;
        _observationWake.notify_all();
        if (_observationThread.joinable()
            && _observationThread.get_id() != std::this_thread::get_id())
            worker = std::move(_observationThread);
    }
    if (worker.joinable()) worker.join();
}

void GocatorSourceController::observationLoop()
{
    std::unique_lock<std::mutex> lock(_observationThreadMutex);
    for (;;) {
        if (_observationStop || !_notifications.active()) {
            _observationRunning = false;
            return;
        }
        const bool snapshot = _observationSnapshotPending;
        _observationSnapshotPending = false;
        lock.unlock();
        try {
            observeAll(snapshot);
        } catch (const std::logic_error& error) {
            std::string connection;
            {
                std::lock_guard<std::mutex> observationLock(_observationMutex);
                connection = _lastConnectionId.empty() ? _notificationConnectionId : _lastConnectionId;
            }
            if (!connection.empty()) {
                _notifications.publishTerminal(connection, DeviceParameterError::Disconnected,
                    error.what());
                _notifications.drain();
            }
        } catch (const std::exception& error) {
            std::string connection;
            {
                std::lock_guard<std::mutex> observationLock(_observationMutex);
                connection = _lastConnectionId.empty() ? _notificationConnectionId : _lastConnectionId;
            }
            if (!connection.empty()) {
                // A malformed or failed resource observation is a backend failure, not a loss of
                // the connection generation.
                _notifications.publishTerminal(connection, DeviceParameterError::BackendFailure,
                    error.what());
                _notifications.drain();
            }
        } catch (...) {
            std::string connection;
            {
                std::lock_guard<std::mutex> observationLock(_observationMutex);
                connection = _lastConnectionId.empty() ? _notificationConnectionId : _lastConnectionId;
            }
            if (!connection.empty()) {
                _notifications.publishTerminal(connection, DeviceParameterError::BackendFailure,
                    "The Gocator parameter observation failed.");
                _notifications.drain();
            }
        }
        lock.lock();
        _observationWake.wait_for(lock, _observationInterval, [this] {
            return _observationStop || _observationSnapshotPending;
        });
    }
}
