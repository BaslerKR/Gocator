#include "gocator/Gocator.h"

#include <GoApi/GoApiLib.h>
#include <GoPxLSdk/GoDataSet.h>
#include <GoPxLSdk/GoDiscoveryClient.h>
#include <GoPxLSdk/GoGdpClient.h>
#include <GoPxLSdk/GoInstance.h>
#include <GoPxLSdk/GoJson.h>
#include <GoPxLSdk/GoSystem.h>
#include <kApi/Io/kNetwork.h>
#include <kApi/kApiDef.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <iostream>

#include "gocator/GocatorAcquisition.h"

namespace
{
constexpr const char* GOCATOR_CONTROL_PATH = "/controls/gocator";
constexpr const char* GOCATOR_ADD_OUTPUT_PATH = "/controls/gocator/outputs/commands/add";
constexpr const char* GOCATOR_REMOVE_ALL_OUTPUT_PATH = "/controls/gocator/outputs/commands/removeAll";

struct GocatorTarget
{
    kIpAddress address{};
    k16u controlPort = GO_PXL_SDK_DEFAULT_CONTROL_PORT;
};

[[nodiscard]] GocatorTarget parseTarget(const std::string& ipAddress)
{
    GocatorTarget target;
    if (kIpAddress_Parse(&target.address, ipAddress.c_str()) != kOK)
    {
        throw std::runtime_error("invalid Gocator IP address: " + ipAddress);
    }
    return target;
}

[[nodiscard]] GocatorTarget discoverTarget(int timeoutMs)
{
    GoPxLSdk::GoDiscoveryClient discovery;
    discovery.BlockingDiscover(static_cast<k64u>(timeoutMs), false);
    const std::vector<GoPxLSdk::GoInstance>& instances = discovery.InstanceList();
    for (const GoPxLSdk::GoInstance& instance : instances)
    {
        if (!instance.GetIsRemote())
        {
            return {instance.GetIpAddress(), instance.GetControlPort()};
        }
    }
    throw std::runtime_error("no local Gocator/GoPxL instance discovered");
}

[[nodiscard]] std::string detectEngineId(GoPxLSdk::GoSystem& system)
{
    try
    {
        GoPxLSdk::GoJson sensorsResponse = system.Client().Read("/scan/visibleSensors/").GetResponse().Payload();
        GoPxLSdk::GoJson sensors = sensorsResponse.At("/sensors");
        if (sensors.Size() > 0U)
        {
            return sensors.At("/0/engineId").Get<std::string>();
        }
    }
    catch (...) {}
    return "LMIConfocalLineProfiler";
}

void enableGocatorProtocol(GoPxLSdk::GoSystem& system, int timeoutMs)
{
    system.Client()
        .Update(GOCATOR_CONTROL_PATH, GoPxLSdk::GoJson(R"({"enabled": true})"))
        .CheckResponse(static_cast<k64u>(timeoutMs));
}

void configureProfileOutput(GoPxLSdk::GoSystem& system, int timeoutMs)
{
    try { system.Stop(); } catch (...) {}

    const std::string engineId = detectEngineId(system);
    const std::string scannerPath = "/scan/engines/" + engineId + "/scanners/scanner-0";
    const std::string profileSourceId = engineId == "LMIConfocalLineProfiler"
        ? "scan:" + engineId + ":scanner-0:topUniformProfileLayer0"
        : "scan:" + engineId + ":scanner-0:topUniformProfile";

    try
    {
        system.Client()
            .Update(scannerPath,
                    GoPxLSdk::GoJson(R"({
                        "parameters" : {
                            "scanModeSettings" : {
                                "scanMode" : 2,
                                "intensityEnabled" : true,
                                "uniformSpacingEnabled" : true
                            }
                        }
                    })"))
            .CheckResponse(static_cast<k64u>(timeoutMs));
    }
    catch (...) {}

    enableGocatorProtocol(system, timeoutMs);

    try
    {
        system.Client()
            .Call(GOCATOR_REMOVE_ALL_OUTPUT_PATH, GoPxLSdk::GoJson("{}"))
            .CheckResponse(static_cast<k64u>(timeoutMs));
    }
    catch (...) {}

    const std::string payload = R"({"source":")" + profileSourceId + R"(","outputId":0,"autoShift":true})";
    system.Client()
        .Call(GOCATOR_ADD_OUTPUT_PATH, GoPxLSdk::GoJson(payload))
        .CheckResponse(static_cast<k64u>(timeoutMs));
}
} // namespace

struct Gocator::Impl
{
    std::string ipAddress;
    kAssembly goApiLib = kNULL;
    std::unique_ptr<GoPxLSdk::GoSystem> system;
    std::unique_ptr<GoPxLSdk::GoGdpClient> gdpClient;
    std::atomic<bool> isOpened{false};
    std::atomic<bool> isGrabbing{false};

    std::mutex statusMutex;
    std::unordered_map<size_t, StatusCallback> statusObservers;
    std::atomic<size_t> nextStatusObserverId{1};

    std::mutex grabCallbackMutex;
    std::unordered_map<size_t, GrabCallback> grabCallbacks;
    std::atomic<size_t> nextGrabCallbackId{1};

    size_t frameSeq = 0;
    size_t frameTarget = 0;
    std::function<void(const GoPxLSdk::GoDataSet&)> dataCallback;

    ~Impl()
    {
        close();
    }

    void notifyStatus(Status status, bool on)
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        for (const auto& [id, cb] : statusObservers)
        {
            cb(status, on);
        }
    }

    bool open(const std::string& ip)
    {
        if (isOpened.load()) return true;

        const kStatus constructStatus = GoApiLib_Construct(&goApiLib);
        if (constructStatus != kOK)
        {
            goApiLib = kNULL;
            return false;
        }

        try
        {
            GocatorTarget target;
            if (!ip.empty())
            {
                target = parseTarget(ip);
            }
            else
            {
                target = discoverTarget(3000); // 3 seconds timeout
            }

            system = std::make_unique<GoPxLSdk::GoSystem>(target.address, target.controlPort);
            system->Connect();

            ipAddress = ip;
            isOpened.store(true);
            notifyStatus(ConnectionStatus, true);
            return true;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Gocator open failed: " << e.what() << std::endl;
            close();
            return false;
        }
    }

    void close()
    {
        stop();

        if (gdpClient)
        {
            try { gdpClient->Close(); } catch (...) {}
            gdpClient.reset();
        }

        if (system)
        {
            try { system->Disconnect(); } catch (...) {}
            system.reset();
        }

        if (goApiLib != kNULL)
        {
            kDestroyRef(&goApiLib);
            goApiLib = kNULL;
        }

        if (isOpened.load())
        {
            isOpened.store(false);
            notifyStatus(ConnectionStatus, false);
        }
    }

    void configure(double scanLengthMm, bool configureProfile)
    {
        if (!isOpened.load()) return;

        if (configureProfile)
        {
            configureProfileOutput(*system, 30000);
        }
        else
        {
            enableGocatorProtocol(*system, 30000);
        }
        
        try
        {
            const std::string engineId = detectEngineId(*system);
            const std::string areaPath = "/scan/engines/" + engineId + "/scanners/scanner-0";
            system->Client().Update(areaPath, GoPxLSdk::GoJson(R"({"parameters":{"scanModeSettings":{"scanLengthMm":)" + std::to_string(scanLengthMm) + R"(}}})"))
                           .CheckResponse(30000);
        }
        catch (...) {}
    }

    void grab(size_t frames)
    {
        if (!isOpened.load() || isGrabbing.load()) return;

        try
        {
            gdpClient = std::make_unique<GoPxLSdk::GoGdpClient>();
            gdpClient->Connect(system->Address(), system->GdpPort());

            frameSeq = 0;
            frameTarget = frames;
            isGrabbing.store(true);

            dataCallback = [this](const GoPxLSdk::GoDataSet& dataSet) {
                handleData(dataSet);
            };
            gdpClient->ReceiveDataAsync(dataCallback);

            system->Start();
            notifyStatus(GrabbingStatus, true);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Gocator grab failed: " << e.what() << std::endl;
            stop();
        }
    }

    void stop()
    {
        if (!isGrabbing.load()) return;

        isGrabbing.store(false);

        if (system)
        {
            try { system->Stop(); } catch (...) {}
        }

        if (gdpClient)
        {
            try { gdpClient->Close(); } catch (...) {}
            gdpClient.reset();
        }

        dataCallback = {};
        notifyStatus(GrabbingStatus, false);
    }

    void handleData(const GoPxLSdk::GoDataSet& dataSet)
    {
        if (!isGrabbing.load()) return;

        {
            std::lock_guard<std::mutex> lock(grabCallbackMutex);
            for (const auto& [id, cb] : grabCallbacks)
            {
                cb(dataSet, frameSeq);
            }
        }

        frameSeq++;
        if (frameTarget > 0 && frameSeq >= frameTarget)
        {
            stop();
        }
    }
};

Gocator::Gocator()
    : _impl(std::make_unique<Impl>())
{}

Gocator::~Gocator() = default;

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

void Gocator::configure(double scanLengthMm, bool configureProfileOutput)
{
    _impl->configure(scanLengthMm, configureProfileOutput);
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

std::string Gocator::getConnectedAddress() const
{
    return _impl->ipAddress;
}
