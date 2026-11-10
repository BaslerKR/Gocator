#include "Gocator.h"

#include <GoPxLSdk/GoDataSet.h>
#include <GoPxLSdk/GoGdpClient.h>
#include <GoPxLSdk/GoJson.h>
#include <GoPxLSdk/GoSystem.h>

#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Internal/GocatorConnection.h"
#include "GocatorDiscoveryDiagnostics.h"
#include "GrabCallbackState.h"
#include "Internal/GocatorDiscovery.h"
#include "Internal/GocatorResourceClient.h"

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

[[nodiscard]] const char* scanModeName(Gocator::ScanMode mode)
{
    return mode == Gocator::ScanMode::SurfaceMode ? "Surface" : "Profile";
}

[[nodiscard]] const char* onOff(bool enabled)
{
    return enabled ? "on" : "off";
}

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

[[nodiscard]] gocator::GocatorConnectionConfig resolveTarget(const std::string& ipAddress)
{
    if (!ipAddress.empty())
    {
        Gocator::syslog("Using manual Gocator target: " + ipAddress + ".");
        return gocator::GocatorDiscovery::manualTarget(ipAddress);
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

[[nodiscard]] std::string detectEngineId(gocator::GocatorResourceClient& resources)
{
    try
    {
        GoPxLSdk::GoJson sensorsResponse = resources.read("/scan/visibleSensors/");
        GoPxLSdk::GoJson sensors = sensorsResponse.At("/sensors");
        if (sensors.Size() > 0U)
        {
            return sensors.At("/0/engineId").Get<std::string>();
        }
    }
    catch (...) {}
    return "LMIConfocalLineProfiler";
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

struct Gocator::Impl
{
    mutable std::recursive_mutex controlMutex;
    std::uint64_t connectionGeneration = 0;
    std::string ipAddress;
    std::unique_ptr<gocator::GocatorConnection> connection;
    std::unique_ptr<gocator::GocatorResourceClient> resources;
    std::unique_ptr<GoPxLSdk::GoGdpClient> gdpClient;
    std::atomic<bool> isOpened{false};
    std::atomic<bool> isGrabbing{false};
    std::atomic<bool> stopRequested{false};

    std::string getScannerPath() const
    {
        if (!resources) return "";
        const std::string engineId = detectEngineId(*resources);
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
    std::thread stopWorker;
    std::mutex stopMutex;

    ~Impl()
    {
        close();
        joinStopWorker();
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

    bool open(const std::string& ip)
    {
        std::lock_guard<std::recursive_mutex> lock(controlMutex);
        if (isOpened.load())
        {
            Gocator::syslog("Open skipped: Gocator is already connected to " + ipAddress + ".");
            return true;
        }

        try
        {
            Gocator::syslog("Try to open Gocator" + std::string(ip.empty() ? " by discovery." : " at " + ip + "."));
            auto target = resolveTarget(ip);
            Gocator::syslog("Resolved Gocator target: address=" + target.address
                + " controlPort=" + std::to_string(target.controlPort)
                + " timeoutMs=" + std::to_string(target.commandTimeoutMs) + ".");
            connection = std::make_unique<gocator::GocatorConnection>(target);
            resources = std::make_unique<gocator::GocatorResourceClient>(*connection);
            connection->connect();

            ipAddress = connection->config().address;
            ++connectionGeneration;
            isOpened.store(true);
            Gocator::syslog("Gocator connected: address=" + ipAddress
                + " gdpPort=" + std::to_string(connection->gdpPort()) + ".");
            notifyStatus(ConnectionStatus, true);
            return true;
        }
        catch (const std::exception& e)
        {
            Gocator::syslog("Gocator open failed: " + std::string(e.what()), true);
            close();
            return false;
        }
    }

    void close()
    {
        const bool wasOpened = isOpened.load();
        if (wasOpened)
        {
            Gocator::syslog("Closing Gocator connection: address=" + ipAddress + ".");
        }

        joinStopWorker();
        stop();

        if (gdpClient)
        {
            try { gdpClient->Close(); } catch (...) {}
            gdpClient.reset();
        }

        std::lock_guard<std::recursive_mutex> controlLock(controlMutex);
        resources.reset();
        if (connection)
        {
            connection->disconnect();
            connection.reset();
        }

        if (isOpened.load())
        {
            isOpened.store(false);
            notifyStatus(ConnectionStatus, false);
            Gocator::syslog("Gocator connection closed.");
        }
    }

    void configure(double scanLengthMm, ScanMode mode, bool intensityEnabled, bool uniformSpacingEnabled, int exposureUs)
    {
        if (!isOpened.load())
        {
            Gocator::syslog("Configure skipped: Gocator is not connected.", true);
            return;
        }

        try
        {
            Gocator::syslog("Configuring Gocator scan: mode=" + std::string(scanModeName(mode))
                + " scanLengthMm=" + std::to_string(scanLengthMm)
                + " intensity=" + onOff(intensityEnabled)
                + " uniformSpacing=" + onOff(uniformSpacingEnabled)
                + " exposureUs=" + std::to_string(exposureUs) + ".");

            const std::string engineId = detectEngineId(*resources);
            const std::string scannerPath = "/scan/engines/" + engineId + "/scanners/scanner-0";
            const std::string sensorPath = scannerPath + "/sensors/sensor-0";

            int sdkScanMode = 2;
            if (mode == ScanMode::SurfaceMode)
            {
                sdkScanMode = 3;
            }

            std::string payload = R"({"parameters":{"scanModeSettings":{)"
                                  R"("scanMode":)" + std::to_string(sdkScanMode) + R"(,)"
                                  R"("intensityEnabled":)" + (intensityEnabled ? "true" : "false") + R"(,)"
                                  R"("uniformSpacingEnabled":)" + (uniformSpacingEnabled ? "true" : "false") +
                                  R"(}}})";
            resources->update(scannerPath, GoPxLSdk::GoJson(payload));

            std::string exposurePayload = R"({"parameters":{"exposureSettings":{)"
                                          R"("exposureMode":0,)"
                                          R"("singleExposure":)" + std::to_string(exposureUs) +
                                          R"(}}})";
            resources->update(sensorPath, GoPxLSdk::GoJson(exposurePayload));

            std::string scanLengthPayload = R"({"parameters":{"scanModeSettings":{"scanLengthMm":)" + std::to_string(scanLengthMm) + R"(}}})";
            resources->update(scannerPath, GoPxLSdk::GoJson(scanLengthPayload));

            resources->update(GOCATOR_CONTROL_PATH, GoPxLSdk::GoJson(R"({"enabled": true})"));

            try
            {
                resources->call(GOCATOR_REMOVE_ALL_OUTPUT_PATH, GoPxLSdk::GoJson("{}"));
            }
            catch (...) {}

            const std::string sourceId = profileSourceForEngine(engineId, mode);
            std::string addOutputPayload = R"({"source":")" + sourceId + R"(","outputId":0,"autoShift":true})";
            resources->call(GOCATOR_ADD_OUTPUT_PATH, GoPxLSdk::GoJson(addOutputPayload));
            Gocator::syslog("Gocator scan configured: engine=" + engineId + " source=" + sourceId + ".");
        }
        catch (const std::exception& e)
        {
            Gocator::syslog("Gocator configure failed: " + std::string(e.what()), true);
        }
    }

    void setParameterValue(ParameterTarget target, const std::string& path, const std::string& jsonValue)
    {
        if (!isOpened.load())
        {
            Gocator::syslog("Set parameter skipped: Gocator is not connected. target="
                + std::string(parameterTargetName(target)) + " path=" + path + ".", true);
            return;
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
        }
        catch (const std::exception& e)
        {
            Gocator::syslog("setParameterValue failed: " + std::string(e.what())
                + " (target: " + parameterTargetName(target)
                + ", path: " + path
                + ", val: " + jsonValue + ")", true);
        }
    }

    void grab(size_t frames)
    {
        joinStopWorker();

        if (!isOpened.load())
        {
            Gocator::syslog("Grab skipped: Gocator is not connected.", true);
            return;
        }

        if (isGrabbing.load())
        {
            Gocator::syslog("Grab skipped: Gocator is already grabbing.");
            return;
        }

        try
        {
            Gocator::syslog("Starting Gocator grab: frames="
                + std::string(frames == 0 ? "continuous" : std::to_string(frames)) + ".");
            gdpClient = std::make_unique<GoPxLSdk::GoGdpClient>();
            gdpClient->Connect(connection->system().Address(), connection->gdpPort());
            Gocator::syslog("Gocator GDP connected: gdpPort=" + std::to_string(connection->gdpPort()) + ".");

            frameSeq = 0;
            frameTarget = frames;
            callbackState.reset();
            stopRequested.store(false);
            isGrabbing.store(true);

            dataCallback = [this](const GoPxLSdk::GoDataSet& dataSet) {
                handleData(dataSet);
            };
            gdpClient->ReceiveDataAsync(dataCallback);

            connection->start();
            notifyStatus(GrabbingStatus, true);
            Gocator::syslog("Gocator grabbing started.");
        }
        catch (const std::exception& e)
        {
            Gocator::syslog("Gocator grab failed: " + std::string(e.what()), true);
            stop();
        }
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(stopMutex);
        if (!isGrabbing.exchange(false)) return;

        Gocator::syslog("Stopping Gocator grab.");
        stopRequested.store(true);

        if (connection)
        {
            connection->stopNoThrow();
        }

        if (gdpClient)
        {
            try { gdpClient->Close(); } catch (...) {}
            gdpClient.reset();
        }

        // Close has stopped reception; serialize with the last admitted callback.
        std::lock_guard<std::mutex> dataLock(dataMutex);
        dataCallback = {};
        Gocator::syslog("Gocator diagnostics: delivered=" + std::to_string(callbackState.deliveredFrames())
            + " failed=" + std::to_string(callbackState.failedCallbacks()) + ".");
        notifyStatus(GrabbingStatus, false);
        Gocator::syslog("Gocator grabbing stopped.");
    }

    void handleData(const GoPxLSdk::GoDataSet& dataSet)
    {
        std::lock_guard<std::mutex> dataLock(dataMutex);
        if (!isGrabbing.load()) return;

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
            requestStopFromCallback();
        }
    }

    void requestStopFromCallback()
    {
        if (stopRequested.exchange(true))
        {
            return;
        }

        std::lock_guard<std::mutex> lock(stopWorkerMutex);
        if (stopWorker.joinable())
        {
            return;
        }

        stopWorker = std::thread([this]() {
            stop();
        });
    }

    void joinStopWorker()
    {
        std::thread worker;
        {
            std::lock_guard<std::mutex> lock(stopWorkerMutex);
            if (!stopWorker.joinable() || stopWorker.get_id() == std::this_thread::get_id())
            {
                return;
            }
            worker = std::move(stopWorker);
        }

        worker.join();
    }
};

Gocator::Gocator()
    : _impl(std::make_unique<Impl>())
{
    syslog("New Gocator instance created.");
}

Gocator::~Gocator()
{
    if (_impl)
    {
        _impl->close();
    }
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
    return _impl->open(ipAddress);
}

bool Gocator::isOpened() const
{
    return _impl->isOpened.load();
}

void Gocator::close()
{
    _impl->close();
}

std::vector<Gocator::DeviceInfo> Gocator::discoverDevices()
{
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

        // If nothing found, try classic discovery for older G2/G3 sensors
        if (devices.empty())
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
        }

        result.reserve(devices.size());
        for (const auto& d : devices)
        {
            syslog(deviceSummary(d));
            bool isVirt = (d.serialNumber == 0 || d.deviceModel.empty() || d.webPort == 8100);
            result.push_back({d.address, d.deviceModel, std::to_string(d.serialNumber), isVirt});
        }
        syslog("Gocator discovery completed: " + std::to_string(result.size()) + " device(s) available.");
    }
    catch (const std::exception& e)
    {
        syslog("Discovery failed: " + std::string(e.what()), true);
    }
    catch (...)
    {
        syslog("Discovery failed with unknown error", true);
    }
    return result;
}

void Gocator::configure(double scanLengthMm, ScanMode mode, bool intensityEnabled, bool uniformSpacingEnabled, int exposureUs)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    _impl->configure(scanLengthMm, mode, intensityEnabled, uniformSpacingEnabled, exposureUs);
}

void Gocator::setScanLengthMm(double length)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    _impl->setParameterValue(ParameterTarget::Scanner, SCAN_LENGTH_PARAMETER_PATH, std::to_string(length));
}

void Gocator::setScanMode(ScanMode mode)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    const int value = (mode == ScanMode::SurfaceMode) ? 3 : 2;
    _impl->setParameterValue(ParameterTarget::Scanner, SCAN_MODE_PARAMETER_PATH, std::to_string(value));
}

void Gocator::setExposureUs(int exposure)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    _impl->setParameterValue(ParameterTarget::Sensor, EXPOSURE_PARAMETER_PATH, std::to_string(exposure));
}

void Gocator::setIntensityEnabled(bool enable)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    _impl->setParameterValue(ParameterTarget::Scanner, INTENSITY_PARAMETER_PATH, enable ? "true" : "false");
}

void Gocator::setUniformSpacingEnabled(bool enable)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    _impl->setParameterValue(ParameterTarget::Scanner, UNIFORM_SPACING_PARAMETER_PATH, enable ? "true" : "false");
}

void Gocator::grab(size_t frames)
{
    _impl->grab(frames);
}

void Gocator::stop()
{
    _impl->stop();
}

bool Gocator::isGrabbing() const
{
    return _impl->isGrabbing.load();
}

GocatorRuntimeDiagnostics Gocator::runtimeDiagnostics() const noexcept
{
    return {_impl->isGrabbing.load(std::memory_order_acquire),
        _impl->callbackState.deliveredFrames(),
        _impl->callbackState.failedCallbacks()};
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

void Gocator::setParameterValue(ParameterTarget target, const std::string& path, const std::string& jsonValue)
{
    if (target == ParameterTarget::System) {
        if (path != "/parameters/quickEditEnabled" || (jsonValue != "true" && jsonValue != "false"))
            throw std::invalid_argument("Only the boolean System quickEditEnabled setting is writable.");
        setQuickEditEnabled(jsonValue == "true");
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(_impl->controlMutex);
    _impl->setParameterValue(target, path, jsonValue);
}

std::string Gocator::getParametersSchema(const std::string& type) const
{
    return getParametersSchema(parameterTargetFromString(type));
}

std::string Gocator::getParametersData(const std::string& type) const
{
    return getParametersData(parameterTargetFromString(type));
}

void Gocator::setParameterValue(const std::string& type, const std::string& path, const std::string& jsonValue)
{
    setParameterValue(parameterTargetFromString(type), path, jsonValue);
}

void Gocator::syslog(const std::string& message, bool warning)
{
    const std::string line = "[Gocator] " + message + '\n';
    std::ostream& stream = warning ? std::cerr : std::cout;
    stream << line << std::flush;
}
