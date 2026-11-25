#include "Gocator.h"

#include <GoPxLSdk/GoDataSet.h>
#include <GoPxLSdk/GoGdpClient.h>
#include <GoPxLSdk/GoJson.h>
#include <GoPxLSdk/GoSystem.h>

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include "GocatorDiscoveryDiagnostics.h"
#include "GrabCallbackState.h"
#include "Internal/GocatorConnection.h"
#include "Internal/GocatorDataSource.h"
#include "Internal/GocatorDiscovery.h"
#include "Internal/GocatorOutputPreparation.h"
#include "Internal/GocatorOutputRequest.h"
#include "Internal/GocatorResourceClient.h"
#include "Internal/GocatorSelection.h"

namespace
{
constexpr const char* GOCATOR_CONTROL_PATH = "/controls/gocator";
constexpr const char* GOCATOR_ADD_OUTPUT_PATH = "/controls/gocator/outputs/commands/add";
constexpr const char* GOCATOR_REMOVE_ALL_OUTPUT_PATH = "/controls/gocator/outputs/commands/removeAll";
constexpr const char* SCAN_LENGTH_PARAMETER_PATH = "/parameters/scanModeSettings/scanLengthMm";
constexpr const char* SCAN_MODE_PARAMETER_PATH = "/parameters/scanModeSettings/scanMode";
constexpr const char* INTENSITY_PARAMETER_PATH = "/parameters/scanModeSettings/intensityEnabled";
constexpr const char* UNIFORM_SPACING_PARAMETER_PATH = "/parameters/scanModeSettings/uniformSpacingEnabled";
constexpr const char* EXPOSURE_PARAMETER_PATH = "/parameters/exposureSettings/singleExposure";

[[nodiscard]] Gocator::ParameterTarget parameterTargetFromString(const std::string& type)
{
    if (type == "system") return Gocator::ParameterTarget::System;
    if (type == "scanner") return Gocator::ParameterTarget::Scanner;
    if (type == "sensor") return Gocator::ParameterTarget::Sensor;
    throw std::invalid_argument("Unknown Gocator parameter target: " + type);
}

[[nodiscard]] const char* parameterTargetName(Gocator::ParameterTarget target)
{
    if (target == Gocator::ParameterTarget::System) return "system";
    return target == Gocator::ParameterTarget::Scanner ? "scanner" : "sensor";
}

[[nodiscard]] std::string deviceSummary(const gocator::GocatorDeviceInfo& device)
{
    std::string summary = "-- address=" + device.address
        + " model=" + (device.deviceModel.empty() ? "<unknown>" : device.deviceModel)
        + " serial=" + std::to_string(device.serialNumber)
        + " controlPort=" + std::to_string(device.controlPort)
        + " gdpPort=" + std::to_string(device.gdpPort);

    if (device.remote) summary += " remote=true";
    if (device.addressConflict) summary += " addressConflict=true";
    return summary;
}

[[nodiscard]] gocator::GocatorConnectionConfig resolveTarget(
    const gocator::GocatorTargetEndpoint& endpoint)
{
    if (!endpoint.address.empty())
    {
        Gocator::syslog("Using manual Gocator target: " + endpoint.address
            + " controlPort=" + std::to_string(endpoint.controlPort) + ".");
        return gocator::GocatorDiscovery::manualTarget(endpoint.address, endpoint.controlPort);
    }

    Gocator::syslog("Resolving local Gocator target by discovery.");
    const gocator::GocatorDiscovery discovery;
    const std::vector<gocator::GocatorDeviceInfo> devices = discovery.discover({3000, false});
    Gocator::syslog("Local discovery found " + std::to_string(devices.size()) + " device(s).");
    for (const gocator::GocatorDeviceInfo& device : devices)
    {
        Gocator::syslog(deviceSummary(device));
        if (device.canConnectLocally())
        {
            return device.connectionConfig();
        }
    }

    throw std::runtime_error("no local Gocator/GoPxL instance discovered");
}

[[nodiscard]] std::string profileSourceForEngine(const std::string& engineId, Gocator::ScanMode mode)
{
    if (mode == Gocator::ScanMode::SurfaceMode)
    {
        return (engineId == "LMIConfocalLineProfiler")
            ? "scan:" + engineId + ":scanner-0:topUniformSurfaceLayer0"
            : "scan:" + engineId + ":scanner-0:topUniformSurface";
    }

    return (engineId == "LMIConfocalLineProfiler")
        ? "scan:" + engineId + ":scanner-0:topUniformProfileLayer0"
        : "scan:" + engineId + ":scanner-0:topUniformProfile";
}

} // namespace

std::string Gocator::formatDeviceName(const std::string& model, const std::string& serial,
    const std::string& address, bool isVirtual)
{
    if (model.empty() || model == "Unknown")
    {
        if (isVirtual)
        {
            return "Unknown (Virtual) - " + address;
        }
        return "Unknown - " + address;
    }

    std::string displayName = model;
    bool replaced = false;
    for (const std::string& target : {"Gocator ", "gocator ", "Gocator", "gocator"})
    {
        size_t pos = displayName.find(target);
        if (pos != std::string::npos)
        {
            displayName.replace(pos, target.length(), "G");
            replaced = true;
            break;
        }
    }
    if (!replaced && !displayName.empty() && std::isdigit(static_cast<unsigned char>(displayName[0])))
    {
        displayName = "G" + displayName;
    }

    std::string serialStr = serial;
    if (isVirtual)
    {
        serialStr = "Virtual";
    }
    else if (serialStr.empty() || serialStr == "0")
    {
        serialStr = "0";
    }

    return displayName + " (" + serialStr + ") - " + address;
}

struct Gocator::Impl
{
    explicit Impl(std::function<void(gocator::OutputPreparationFailure)> observer = {})
        : outputFailure(std::move(observer)) {}
    const std::function<void(gocator::OutputPreparationFailure)> outputFailure;
    mutable std::recursive_mutex controlMutex;
    std::uint64_t connectionGeneration = 0;
    std::string ipAddress;
    std::unique_ptr<gocator::GocatorConnection> connection;
    std::unique_ptr<gocator::GocatorResourceClient> resources;
    std::unique_ptr<GoPxLSdk::GoGdpClient> gdpClient;
    std::atomic<bool> isOpened{false};
    std::atomic<bool> isGrabbing{false};
    bool stopFailed = false;
    std::string stopError;
    std::mutex diagnosticsMutex;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> controlBusy{false};
    mutable std::mutex devicesMutex;
    std::vector<Gocator::DeviceInfo> cachedDevices;
    mutable std::mutex snapshotMutex;
    Gocator::ConnectionSnapshot snapshot;

    std::string getScannerPath() const
    {
        if (!resources) return "";
        const auto detected = gocator::detectEngineId(
            [&](const std::string& path) { return resources->read(path); });
        const std::string engineId = detected.engineId.value_or("LMIConfocalLineProfiler");
        return "/scan/engines/" + engineId + "/scanners/scanner-0";
    }

    std::string getSensorPath() const
    {
        return getScannerPath() + "/sensors/sensor-0";
    }

    std::string getParameterTargetPath(ParameterTarget target) const
    {
        return (target == ParameterTarget::Scanner) ? getScannerPath() : getSensorPath();
    }

    std::mutex statusMutex;
    std::unordered_map<size_t, StatusCallback> statusObservers;
    std::atomic<size_t> nextStatusObserverId{1};

    std::mutex grabCallbackMutex;
    std::unordered_map<size_t, GrabCallback> grabCallbacks;
    std::atomic<size_t> nextGrabCallbackId{1};

    size_t frameSeq = 0;
    size_t frameTarget = 0;
    GrabCallbackState callbackState;
    std::mutex dataMutex;
    std::function<void(const GoPxLSdk::GoDataSet&)> dataCallback;
    std::mutex stopWorkerMutex;
    std::condition_variable stopWorkerWake;
    std::thread stopWorker;
    bool stopWorkerShutdown = false;
    std::optional<std::uint64_t> pendingStop;
    std::uint64_t acquisitionGeneration = 0;

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(stopWorkerMutex);
            stopWorkerShutdown = true;
        }
        stopWorkerWake.notify_one();
        if (stopWorker.joinable()) stopWorker.join();
    }

    void notifyStatus(Status status, bool on)
    {
        std::vector<StatusCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            callbacks.reserve(statusObservers.size());
            for (const auto& [id, cb] : statusObservers)
            {
                callbacks.push_back(cb);
            }
        }

        for (const auto& cb : callbacks)
        {
            if (status == GrabbingStatus && on != isGrabbing.load()) break;
            if (status == ConnectionStatus && on != isOpened.load()) break;
            if (!cb) continue;
            try
            {
                cb(status, on);
            }
            catch (const std::exception& e)
            {
                Gocator::syslog("Gocator status callback failed: " + std::string(e.what()), true);
            }
            catch (...)
            {
                Gocator::syslog("Gocator status callback failed with an unknown exception.", true);
            }
        }
    }

    bool open(const std::string& ip, std::string* error)
    {
        if (error) error->clear();
        std::unique_lock<std::recursive_mutex> lock(controlMutex);
        if (isOpened.load())
        {
            Gocator::syslog("Open skipped: Gocator is already connected to " + ipAddress + ".");
            return true;
        }

        try
        {
            Gocator::syslog("Try to open Gocator" + std::string(ip.empty() ? " by discovery." : " at " + ip + "."));
            auto endpoint = gocator::parseTargetId(ip);
            if (!endpoint.explicitPort && !ip.empty()) {
                std::vector<DeviceInfo> discovered;
                {
                    std::lock_guard<std::mutex> lock(devicesMutex);
                    discovered = cachedDevices;
                }
                const auto cached = gocator::controlPortForTarget(discovered, endpoint.address);
                if (cached.ambiguous)
                    throw std::invalid_argument("Gocator target is ambiguous; specify address:port.");
                endpoint.controlPort = cached.port;
            }
            auto target = resolveTarget(endpoint);
            const std::string targetId = gocator::targetIdForEndpoint(
                target.address, target.controlPort);
            Gocator::syslog("Resolved Gocator target: address=" + target.address
                + " controlPort=" + std::to_string(target.controlPort)
                + " timeoutMs=" + std::to_string(target.commandTimeoutMs) + ".");
            connection = std::make_unique<gocator::GocatorConnection>(target);
            resources = std::make_unique<gocator::GocatorResourceClient>(*connection);
            connection->connect();

            ipAddress = connection->config().address;
            ++connectionGeneration;
            {
                std::lock_guard<std::mutex> snapshotLock(snapshotMutex);
                snapshot = {true, connectionGeneration, ipAddress, targetId};
            }
            isOpened.store(true);
            Gocator::syslog("Gocator connected: address=" + ipAddress
                + " gdpPort=" + std::to_string(connection->gdpPort()) + ".");
        }
        catch (const std::exception& e)
        {
            // Keep the SDK reason: an address that was not discovered may still be refused.
            if (error) *error = e.what();
            Gocator::syslog("Gocator open failed: " + std::string(e.what()), true);
            resources.reset();
            connection.reset();
            return false;
        }
        lock.unlock();
        notifyStatus(ConnectionStatus, true);
        return true;
    }

    void close(const bool throwOnFailure = false)
    {
        std::unique_lock<std::recursive_mutex> lock(controlMutex);
        const bool wasOpened = isOpened.load();
        if (wasOpened)
        {
            Gocator::syslog("Closing Gocator connection: address=" + ipAddress + ".");
        }

        auto stopped = stopAcquisition();
        resources.reset();
        connection.reset();
        {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex);
            snapshot = {};
        }
        isOpened.store(false);
        lock.unlock();

        if (wasOpened)
        {
            notifyStatus(ConnectionStatus, false);
            Gocator::syslog("Gocator connection closed.");
        }
        finishStop(stopped, throwOnFailure);
    }

    /** @brief Installs the control output the current scan mode needs before GDP reception starts.
     *  @note Output selection is acquisition setup, not a parameter write. Callers set scan
     *        settings through explicit parameter writes; this function only mirrors the mode the
     *        sensor currently reports so a grab can publish frames.
     */
    void prepareOutputs(bool& mayHaveEffect)
    {
        if (!isOpened.load() || !resources) return;

        const auto reportFailure = [this](gocator::OutputPreparationFailure stage) {
            if (outputFailure) {
                try { outputFailure(stage); return; } catch (...) {}
            }
            const char* step = stage == gocator::OutputPreparationFailure::Preparation ? "preparation"
                : stage == gocator::OutputPreparationFailure::Enable ? "enable" : "registration";
            Gocator::syslog(std::string("Gocator acquisition output ") + step + " failed.", true);
        };

        std::string engineId;
        std::string sourceId;
        bool replaySource = false;
        try {
            ScanMode mode = ScanMode::ProfileMode;
            const auto detectedEngine = gocator::detectEngineId(
                [&](const std::string& path) { return resources->read(path); });
            if (detectedEngine.failed)
                throw std::runtime_error("Unable to determine the Gocator scan engine.");
            if (detectedEngine.engineId) {
                engineId = *detectedEngine.engineId;
                const std::string scannerPath = "/scan/engines/" + engineId + "/scanners/scanner-0";
                try {
                    const auto scannerData = resources->data(scannerPath);
                    try {
                        if (scannerData.At("/parameters/scanModeSettings/scanMode").Get<int>() == 3)
                            mode = ScanMode::SurfaceMode;
                    } catch (...) {
                    }
                } catch (...) {
                }
                sourceId = profileSourceForEngine(engineId, mode);
            } else {
                const auto sources = gocator::replayDataSources(
                    [&](const std::string& path) { return resources->read(path); });
                if (const auto selected = gocator::selectReplayDataSource(sources)) {
                    sourceId = *selected;
                } else if (const auto image = gocator::selectReplayImageDataSource(sources)) {
                    sourceId = *image;
                } else {
                    throw std::runtime_error("No Gocator scan engine or replay range/image source is available.");
                }
                replaySource = true;
            }
            if (connection->isRunning())
                throw std::runtime_error("Gocator was started by another client during output preparation.");
        } catch (...) {
            reportFailure(gocator::OutputPreparationFailure::Preparation);
            throw;
        }

        gocator::prepareOutput(
            [] {},
            [&] {
                mayHaveEffect = true;
                resources->update(GOCATOR_CONTROL_PATH, GoPxLSdk::GoJson(R"({"enabled": true})"));
            },
            [&] { resources->call(GOCATOR_REMOVE_ALL_OUTPUT_PATH, GoPxLSdk::GoJson("{}")); },
            [&] {
                resources->call(GOCATOR_ADD_OUTPUT_PATH, gocator::outputRequest(sourceId, 0));
                if (replaySource)
                    Gocator::syslog("Gocator acquisition output: replay source=" + sourceId + ".");
                else
                    Gocator::syslog("Gocator acquisition output: engine=" + engineId
                        + " source=" + sourceId + ".");
            },
            reportFailure);
    }

    Gocator::ParameterWriteResult setParameterValue(ParameterTarget target, const std::string& path,
        const std::string& jsonValue)
    {
        if (!isOpened.load())
        {
            const std::string reason = "Gocator is not connected.";
            Gocator::syslog("Set parameter skipped: " + reason + " target="
                + std::string(parameterTargetName(target)) + " path=" + path + ".", true);
            return {false, false, reason};
        }
        try
        {
            Gocator::syslog("Setting Gocator parameter: target=" + std::string(parameterTargetName(target))
                + " path=" + path + " value=" + jsonValue + ".");
            GoPxLSdk::GoJson patch;
            patch.Set(path, GoPxLSdk::GoJson(jsonValue));
            resources->setJson(getParameterTargetPath(target), patch);
            Gocator::syslog("Gocator parameter applied: target=" + std::string(parameterTargetName(target))
                + " path=" + path + ".");
            return {true, false, {}};
        }
        catch (const std::exception& e)
        {
            const std::string reason = e.what();
            Gocator::syslog("setParameterValue failed: " + reason
                + " (target: " + parameterTargetName(target)
                + ", path: " + path
                + ", val: " + jsonValue + ")", true);
            return {false, true, reason};
        }
    }

    bool grab(size_t frames)
    {
        std::unique_lock<std::recursive_mutex> lock(controlMutex);

        if (!isOpened.load())
        {
            Gocator::syslog("Grab skipped: Gocator is not connected.", true);
            return false;
        }

        if (isGrabbing.load())
        {
            Gocator::syslog("Grab skipped: Gocator is already grabbing.");
            return false;
        }

        Gocator::GrabFailure failure = Gocator::GrabFailure::OutputPreparation;
        bool mayHaveEffect = false;
        bool localStartConfirmed = false;
        try
        {
            Gocator::syslog("Starting Gocator grab: frames="
                + std::string(frames == 0 ? "continuous" : std::to_string(frames)) + ".");
            failure = Gocator::GrabFailure::StateUnknown;
            const bool attachToRunning = connection->isRunning();
            if (attachToRunning) {
                Gocator::syslog("Attaching to an already-running Gocator system without changing outputs.");
            } else {
                failure = Gocator::GrabFailure::OutputPreparation;
                prepareOutputs(mayHaveEffect);
            }
            failure = Gocator::GrabFailure::GdpConnect;
            gdpClient = std::make_unique<GoPxLSdk::GoGdpClient>();
            gdpClient->Connect(connection->system().Address(), connection->gdpPort());
            Gocator::syslog("Gocator GDP connected: gdpPort=" + std::to_string(connection->gdpPort()) + ".");

            frameSeq = 0;
            frameTarget = frames;
            callbackState.reset();
            stopRequested.store(false);
            {
                std::lock_guard<std::mutex> lock(diagnosticsMutex);
                stopFailed = false;
                stopError.clear();
            }
            isGrabbing.store(true);

            const auto generation = ++acquisitionGeneration;
            dataCallback = [this, generation](const GoPxLSdk::GoDataSet& dataSet) {
                handleData(dataSet, generation);
            };

            if (attachToRunning) {
                failure = Gocator::GrabFailure::Attach;
                connection->attachToRunning();
            } else {
                const auto started = connection->tryStart();
                switch (started) {
                case gocator::GocatorStartResult::Started:
                    localStartConfirmed = true;
                    mayHaveEffect = true;
                    break;
                case gocator::GocatorStartResult::AlreadyRunning:
                    throw Gocator::GrabError(Gocator::GrabFailure::AlreadyRunning,
                        "Gocator was started by another client during output preparation.", true);
                case gocator::GocatorStartResult::StateUnknown:
                    failure = Gocator::GrabFailure::StateUnknown;
                    break;
                case gocator::GocatorStartResult::StartUncertain:
                    failure = Gocator::GrabFailure::StartUncertain;
                    break;
                case gocator::GocatorStartResult::PriorStopUncertain:
                    failure = Gocator::GrabFailure::PriorStopUncertain;
                    break;
                case gocator::GocatorStartResult::Refused:
                    failure = Gocator::GrabFailure::StartRefused;
                    break;
                }
                if (!localStartConfirmed)
                    throw Gocator::GrabError(failure, connection->lastOperationError().empty()
                        ? "Gocator start could not be confirmed." : connection->lastOperationError(), true);
            }
            failure = Gocator::GrabFailure::ReceiveStart;
            mayHaveEffect = true;
            if (frames > 0 && !stopWorker.joinable())
                stopWorker = std::thread([this] { runStopWorker(); });
            gdpClient->ReceiveDataAsync(dataCallback);
        }
        catch (const std::exception& e)
        {
            Gocator::syslog("Gocator grab failed: " + std::string(e.what()), true);
            auto stopped = stopAcquisition(localStartConfirmed);
            lock.unlock();
            finishStop(stopped, false);
            if (dynamic_cast<const Gocator::GrabError*>(&e)) throw;
            throw Gocator::GrabError(failure, e.what(), mayHaveEffect);
        }
        lock.unlock();
        notifyStatus(GrabbingStatus, true);
        if (isGrabbing.load()) Gocator::syslog("Gocator grabbing started.");
        return true;
    }

    struct StopOutcome
    {
        bool wasGrabbing;
        std::string error;
    };

    // The caller holds controlMutex until the connection and GDP state are consistent.
    StopOutcome stopAcquisition(const bool stopSystem = true)
    {
        StopOutcome outcome{isGrabbing.exchange(false), {}};
        stopRequested.store(true);

        if (connection && stopSystem)
        {
            const auto stopped = connection->tryStop();
            if (stopped == gocator::GocatorStopResult::Refused
                || stopped == gocator::GocatorStopResult::Uncertain) {
                outcome.error = connection->lastOperationError();
                if (outcome.error.empty()) outcome.error = "Gocator stop could not be confirmed.";
                {
                    std::lock_guard<std::mutex> diagnosticsLock(diagnosticsMutex);
                    stopError = outcome.error;
                    stopFailed = true;
                }
                Gocator::syslog("Gocator stop failed: " + outcome.error, true);
            }
        }

        if (gdpClient)
        {
            try { gdpClient->Close(); } catch (...) {}
            gdpClient.reset();
        }

        {
            std::lock_guard<std::mutex> dataLock(dataMutex);
            dataCallback = {};
        }
        if (outcome.wasGrabbing)
            Gocator::syslog("Gocator diagnostics: delivered=" + std::to_string(callbackState.deliveredFrames())
                + " failed=" + std::to_string(callbackState.failedCallbacks()) + ".");
        return outcome;
    }

    void finishStop(const StopOutcome& outcome, const bool throwOnFailure)
    {
        if (outcome.wasGrabbing) notifyStatus(GrabbingStatus, false);
        if (throwOnFailure && !outcome.error.empty()) throw std::runtime_error(outcome.error);
    }

    void stop(const bool throwOnFailure = false)
    {
        std::unique_lock<std::recursive_mutex> lock(controlMutex);
        auto stopped = stopAcquisition();
        lock.unlock();
        finishStop(stopped, throwOnFailure);
    }

    void handleData(const GoPxLSdk::GoDataSet& dataSet, const std::uint64_t generation)
    {
        std::lock_guard<std::mutex> dataLock(dataMutex);
        if (!isGrabbing.load() || stopRequested.load()) return;

        callbackState.beginFrame();
        {
            std::vector<GrabCallback> callbacks;
            {
                std::lock_guard<std::mutex> lock(grabCallbackMutex);
                callbacks.reserve(grabCallbacks.size());
                for (const auto& [id, cb] : grabCallbacks)
                {
                    callbacks.push_back(cb);
                }
            }

            for (const auto& cb : callbacks)
            {
                if (!cb) continue;
                try
                {
                    cb(dataSet, frameSeq);
                    callbackState.noteSuccess();
                }
                catch (const std::exception& e)
                {
                    callbackState.noteFailure();
                    Gocator::syslog("Gocator grab callback failed: " + std::string(e.what()), true);
                }
                catch (...)
                {
                    callbackState.noteFailure();
                    Gocator::syslog("Gocator grab callback failed with an unknown exception.", true);
                }
            }
        }

        callbackState.finishFrame();
        frameSeq++;
        if (frameTarget > 0 && frameSeq >= frameTarget)
        {
            Gocator::syslog("Gocator frame target reached: " + std::to_string(frameSeq) + " frame(s).");
            requestStopFromCallback(generation);
        }
    }

    void requestStopFromCallback(const std::uint64_t generation)
    {
        if (stopRequested.exchange(true))
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(stopWorkerMutex);
            pendingStop = generation;
        }
        stopWorkerWake.notify_one();
    }

    void runStopWorker()
    {
        std::unique_lock<std::mutex> workLock(stopWorkerMutex);
        for (;;)
        {
            stopWorkerWake.wait(workLock, [this] { return stopWorkerShutdown || pendingStop; });
            if (stopWorkerShutdown) return;
            const auto generation = *pendingStop;
            pendingStop.reset();
            workLock.unlock();
            {
                std::unique_lock<std::recursive_mutex> lock(controlMutex);
                if (isGrabbing.load() && generation == acquisitionGeneration)
                {
                    auto stopped = stopAcquisition();
                    lock.unlock();
                    finishStop(stopped, false);
                }
            }
            workLock.lock();
        }
    }
};

Gocator::Gocator() : Gocator(std::function<void(gocator::OutputPreparationFailure)>{}) {}

Gocator::Gocator(std::function<void(gocator::OutputPreparationFailure)> outputFailure)
    : _impl(std::make_unique<Impl>(std::move(outputFailure)))
{
    syslog("New Gocator instance created.");
}

Gocator::~Gocator()
{
    _impl->close();
    syslog("Gocator instance destroyed.");
}

Gocator::CallbackId Gocator::registerStatusCallback(StatusCallback cb)
{
    std::lock_guard<std::mutex> lock(_impl->statusMutex);
    CallbackId id = _impl->nextStatusObserverId++;
    _impl->statusObservers[id] = cb;
    return id;
}

bool Gocator::deregisterStatusCallback(CallbackId id)
{
    std::lock_guard<std::mutex> lock(_impl->statusMutex);
    return _impl->statusObservers.erase(id) > 0;
}

void Gocator::clearStatusCallbacks()
{
    std::lock_guard<std::mutex> lock(_impl->statusMutex);
    _impl->statusObservers.clear();
}

Gocator::CallbackId Gocator::registerGrabCallback(GrabCallback cb)
{
    std::lock_guard<std::mutex> lock(_impl->grabCallbackMutex);
    CallbackId id = _impl->nextGrabCallbackId++;
    _impl->grabCallbacks[id] = cb;
    return id;
}

bool Gocator::deregisterGrabCallback(CallbackId id)
{
    std::lock_guard<std::mutex> lock(_impl->grabCallbackMutex);
    return _impl->grabCallbacks.erase(id) > 0;
}

void Gocator::clearGrabCallbacks()
{
    std::lock_guard<std::mutex> lock(_impl->grabCallbackMutex);
    _impl->grabCallbacks.clear();
}

bool Gocator::open(const std::string& ipAddress)
{
    return _impl->open(ipAddress, nullptr);
}

bool Gocator::open(const std::string& ipAddress, std::string* error)
{
    return _impl->open(ipAddress, error);
}

bool Gocator::isOpened() const
{
    return _impl->isOpened.load();
}

Gocator::ConnectionSnapshot Gocator::connectionSnapshot() const
{
    std::lock_guard<std::mutex> lock(_impl->snapshotMutex);
    return _impl->snapshot;
}

void Gocator::close(const bool throwOnFailure)
{
    _impl->close(throwOnFailure);
}

std::vector<Gocator::DeviceInfo> Gocator::cachedDevices() const
{
    std::lock_guard<std::mutex> lock(_impl->devicesMutex);
    return _impl->cachedDevices;
}

void Gocator::setCachedDevices(std::vector<DeviceInfo> devices)
{
    std::lock_guard<std::mutex> lock(_impl->devicesMutex);
    _impl->cachedDevices = std::move(devices);
}

bool Gocator::tryBeginControl()
{
    bool expected = false;
    if (!_impl->controlBusy.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return false;
    }
    _impl->notifyStatus(ControlStatus, true);
    return true;
}

void Gocator::endControl()
{
    if (_impl->controlBusy.exchange(false, std::memory_order_acq_rel))
    {
        _impl->notifyStatus(ControlStatus, false);
    }
}

bool Gocator::controlBusy() const noexcept
{
    return _impl->controlBusy.load(std::memory_order_acquire);
}

void Gocator::holdControlForTest(const std::function<void()>& entered, const std::function<void()>& release)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    if (entered) entered();
    if (release) release();
}

void Gocator::receiveDataForTest(const GoPxLSdk::GoDataSet& dataSet)
{
    _impl->handleData(dataSet, _impl->acquisitionGeneration);
}

void Gocator::notifyStatusForTest(Status status, bool on)
{
    if (status == GrabbingStatus) _impl->isGrabbing.store(on);
    if (status == ConnectionStatus) _impl->isOpened.store(on);
    _impl->notifyStatus(status, on);
}

std::vector<Gocator::DeviceInfo> Gocator::discoverDevices(std::string* error)
{
    if (error) error->clear();
    std::vector<Gocator::DeviceInfo> result;
    try
    {
        gocator::GocatorDiscovery discovery;
        std::vector<gocator::GocatorDeviceInfo> devices;

        // Try standard discovery first. The SDK throws for an empty result, so
        // treat only that message as no devices and let classic discovery run.
        syslog("Starting Gocator discovery: mode=standard timeoutMs=3000.");
        try
        {
            devices = discovery.discover({3000, false});
        }
        catch (const std::exception& e)
        {
            if (!gocator::discoveryMeansNoDevices(e.what()))
            {
                throw;
            }
            devices.clear();
        }
        syslog("Gocator standard discovery found " + std::to_string(devices.size()) + " device(s).");

        bool hasConnectableDevice = false;
        for (const auto& device : devices)
        {
            syslog(deviceSummary(device));
            if (device.canConnectLocally()) hasConnectableDevice = true;
        }

        // If no usable device was found, try classic discovery for older G2/G3 sensors
        if (!hasConnectableDevice)
        {
            syslog("Starting Gocator discovery: mode=classic timeoutMs=2000.");
            try
            {
                devices = discovery.discover({2000, true});
            }
            catch (const std::exception& e)
            {
                if (!gocator::discoveryMeansNoDevices(e.what()))
                {
                    throw;
                }
                devices.clear();
            }
            syslog("Gocator classic discovery found " + std::to_string(devices.size()) + " device(s).");
            for (const auto& device : devices) syslog(deviceSummary(device));
        }

        result.reserve(devices.size());
        for (const auto& d : devices)
        {
            if (!d.canConnectLocally()) continue;
            bool isVirt = (d.serialNumber == 0 || d.deviceModel.empty() || d.webPort == 8100);
            result.push_back({d.address, d.deviceModel, std::to_string(d.serialNumber), isVirt,
                d.controlPort});
        }
        syslog("Gocator discovery completed: " + std::to_string(result.size()) + " device(s) available.");
        setCachedDevices(result);
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        syslog("Discovery failed: " + std::string(e.what()), true);
    }
    catch (...)
    {
        if (error) *error = "Unexpected discovery failure.";
        syslog("Discovery failed with unknown error", true);
    }
    return result;
}

Gocator::ParameterWriteResult Gocator::setScanLengthMm(double length)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    return _impl->setParameterValue(ParameterTarget::Scanner, SCAN_LENGTH_PARAMETER_PATH, std::to_string(length));
}

Gocator::ParameterWriteResult Gocator::setScanMode(ScanMode mode)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    const int value = (mode == ScanMode::SurfaceMode) ? 3 : 2;
    return _impl->setParameterValue(ParameterTarget::Scanner, SCAN_MODE_PARAMETER_PATH, std::to_string(value));
}

Gocator::ParameterWriteResult Gocator::setExposureUs(int exposure)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    return _impl->setParameterValue(ParameterTarget::Sensor, EXPOSURE_PARAMETER_PATH, std::to_string(exposure));
}

Gocator::ParameterWriteResult Gocator::setIntensityEnabled(bool enable)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    return _impl->setParameterValue(ParameterTarget::Scanner, INTENSITY_PARAMETER_PATH, enable ? "true" : "false");
}

Gocator::ParameterWriteResult Gocator::setUniformSpacingEnabled(bool enable)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    return _impl->setParameterValue(ParameterTarget::Scanner, UNIFORM_SPACING_PARAMETER_PATH, enable ? "true" : "false");
}

bool Gocator::grab(size_t frames)
{
    return _impl->grab(frames);
}

void Gocator::stop()
{
    _impl->stop(true);
}

bool Gocator::isGrabbing() const
{
    return _impl->isGrabbing.load();
}

GocatorRuntimeDiagnostics Gocator::runtimeDiagnostics() const
{
    std::lock_guard<std::mutex> lock(_impl->diagnosticsMutex);
    return {_impl->isGrabbing.load(std::memory_order_acquire),
        _impl->callbackState.deliveredFrames(),
        _impl->callbackState.failedCallbacks(),
        _impl->stopFailed,
        _impl->stopError};
}

std::string Gocator::getConnectedAddress() const
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    return _impl->ipAddress;
}

Gocator::DeviceInfo Gocator::getConnectedDeviceInfo() const
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    DeviceInfo info;
    if (!_impl || !_impl->isOpened.load()) return info;
    info.address = _impl->ipAddress;
    if (_impl->connection) info.controlPort = _impl->connection->controlPort();

    try
    {
        if (_impl->resources)
        {
            GoPxLSdk::GoJson sensorsResponse = _impl->resources->read("/scan/visibleSensors/");
            GoPxLSdk::GoJson sensors = sensorsResponse.At("/sensors");
            if (sensors.Size() > 0U)
            {
                GoPxLSdk::GoJson sensor = sensors.At("/0");
                info.model = sensor.At("/model").Get<std::string>();
                info.serial = sensor.At("/serialNumber").Get<std::string>();
                
                if (sensor.HasKey("virtual"))
                {
                    info.isVirtual = sensor.At("/virtual").Get<bool>();
                }
                else if (sensor.HasKey("deviceType"))
                {
                    info.isVirtual = (sensor.At("/deviceType").Get<std::string>() == "Virtual");
                }
                else if (info.serial.empty() || info.serial == "0")
                {
                    info.isVirtual = true;
                }
            }
            else
            {
                info.isVirtual = true;
            }
        }
    }
    catch (...) {}
    return info;
}

Gocator::SystemState Gocator::systemState() const
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    if (!_impl->isOpened.load() || !_impl->connection)
        throw std::logic_error("Gocator is disconnected.");
    auto& system = _impl->connection->system();
    SystemState state;
    state.connectionGeneration = _impl->connectionGeneration;
    switch (system.RunningState()) {
    case GoPxLSdk::GoSystem::State::Ready: state.running = SystemState::Running::Ready; break;
    case GoPxLSdk::GoSystem::State::Running: state.running = SystemState::Running::Running; break;
    case GoPxLSdk::GoSystem::State::Conflict: state.running = SystemState::Running::Conflict; break;
    default: throw std::runtime_error("The SDK returned an unknown running state.");
    }
    state.quickEditEnabled = system.QuickEditEnabled();
    return state;
}

void Gocator::setQuickEditEnabled(bool enabled, std::optional<SystemState> expected)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    SystemState current;
    try { current = systemState(); }
    catch (const std::logic_error&) { throw; }
    catch (const std::exception& error) { throw SystemControlError(error.what(), false); }
    catch (...) { throw SystemControlError("System observation failed before SDK dispatch.", false); }
    if (expected && (current.connectionGeneration != expected->connectionGeneration
        || current.running != expected->running || current.quickEditEnabled != expected->quickEditEnabled))
        throw std::logic_error("Gocator system state changed before the write.");
    auto& system = _impl->connection->system();
    try {
        if (enabled) system.EnableQuickEdit();
        else system.DisableQuickEdit();
    } catch (const std::exception& error) { throw SystemControlError(error.what(), true); }
    catch (...) { throw SystemControlError("Quick Edit SDK completion is unknown.", true); }
}

std::string Gocator::getParametersSchema(ParameterTarget target) const
{
    if (target == ParameterTarget::System)
        return R"({"type":"object","properties":{"parameters":{"type":"object","properties":{"runningState":{"type":"string","enum":["ready","running","conflict"],"readOnly":true,"title":"Running state"},"quickEditEnabled":{"type":"boolean","title":"Quick edit","description":"Suppresses recalculation of existing scan data while editing configuration."}}}}})";
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    if (!_impl->isOpened.load()) return "{}";
    try
    {
        const std::string path = _impl->getParameterTargetPath(target);
        return _impl->resources->schema(path).ToString();
    }
    catch (const std::exception& e)
    {
        syslog("getParametersSchema failed: " + std::string(e.what()), true);
    }
    return "{}";
}

std::string Gocator::getParametersData(ParameterTarget target) const
{
    if (target == ParameterTarget::System) {
        const auto state = systemState();
        const char* running = state.running == SystemState::Running::Ready ? "ready"
            : state.running == SystemState::Running::Running ? "running" : "conflict";
        return std::string("{\"parameters\":{\"runningState\":\"") + running
            + "\",\"quickEditEnabled\":" + (state.quickEditEnabled ? "true" : "false") + "}}";
    }
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    if (!_impl->isOpened.load()) return "{}";
    try
    {
        const std::string path = _impl->getParameterTargetPath(target);
        return _impl->resources->data(path).ToString();
    }
    catch (const std::exception& e)
    {
        syslog("getParametersData failed: " + std::string(e.what()), true);
    }
    return "{}";
}

Gocator::ParameterWriteResult Gocator::setParameterValue(ParameterTarget target, const std::string& path,
    const std::string& jsonValue)
{
    if (target == ParameterTarget::System) {
        if (path != "/parameters/quickEditEnabled" || (jsonValue != "true" && jsonValue != "false"))
            throw std::invalid_argument("Only the boolean System quickEditEnabled setting is writable.");
        try {
            setQuickEditEnabled(jsonValue == "true");
            return {true, false, {}};
        } catch (const SystemControlError& error) {
            return {false, error.mayHaveApplied, error.what()};
        } catch (const std::exception& error) {
            return {false, false, error.what()};
        }
    }
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    return _impl->setParameterValue(target, path, jsonValue);
}

std::string Gocator::getParametersSchema(const std::string& type) const
{
    return getParametersSchema(parameterTargetFromString(type));
}

std::string Gocator::getParametersData(const std::string& type) const
{
    return getParametersData(parameterTargetFromString(type));
}

Gocator::ParameterWriteResult Gocator::setParameterValue(const std::string& type, const std::string& path,
    const std::string& jsonValue)
{
    return setParameterValue(parameterTargetFromString(type), path, jsonValue);
}

void Gocator::syslog(const std::string& message, bool warning)
{
    const std::string line = "[Gocator] " + message + '\n';
    std::ostream& stream = warning ? std::cerr : std::cout;
    stream << line << std::flush;
}
