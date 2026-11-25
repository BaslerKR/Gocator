#include "Internal/GocatorConnection.h"

#include <stdexcept>
#include <utility>

#include <GoPxLSdk/Def.h>
#include <GoPxLSdk/GoChannelError.h>
#include <GoPxLSdk/GoSystem.h>
#include <kApi/Io/kNetwork.h>

#include "Internal/GocatorSdkRuntime.h"

namespace gocator
{
namespace
{

class GoSystemControl final : public GocatorSystemControl
{
public:
    explicit GoSystemControl(GoPxLSdk::GoSystem& system) : _system(system) {}
    GoPxLSdk::GoSystem::State runningState() override { return _system.RunningState(); }
    void start() override { _system.Start(); }
    void stop() override { _system.Stop(); }

private:
    GoPxLSdk::GoSystem& _system;
};

}

GocatorConnection::GocatorConnection(GocatorConnectionConfig config)
    : GocatorConnection(std::move(config), nullptr)
{
}

GocatorConnection::GocatorConnection(
    GocatorConnectionConfig config, std::unique_ptr<GocatorSystemControl> control)
    : config_(std::move(config)), control_(std::move(control))
{
    GocatorSdkRuntime::ensureInitialized();

    kIpAddress address = {};
    const kStatus parseStatus = kIpAddress_Parse(&address, config_.address.c_str());
    if (parseStatus != kOK)
    {
        throw std::invalid_argument("Invalid Gocator IP address: " + config_.address);
    }

    system_ = std::make_unique<GoPxLSdk::GoSystem>(address, config_.controlPort);
    if (!control_) control_ = std::make_unique<GoSystemControl>(*system_);
}

GocatorConnection::~GocatorConnection()
{
    disconnect();
}

const GocatorConnectionConfig& GocatorConnection::config() const noexcept
{
    return config_;
}

void GocatorConnection::connect()
{
    system().Connect();
}

void GocatorConnection::disconnect() noexcept
{
    if (!system_) return;

    try
    {
        {
            std::lock_guard<std::mutex> lock(startMutex_);
            if (startOwnership_.started() && !startOwnership_.stopUncertain())
            {
                try { control_->stop(); } catch (...) {}
            }
            startOwnership_.reset();
        }
        if (system_->IsConnected()) system_->Disconnect();
    }
    catch (...)
    {
    }
}

bool GocatorConnection::isConnected()
{
    return system().IsConnected();
}

bool GocatorConnection::isRunning()
{
    std::lock_guard<std::mutex> lock(startMutex_);
    switch (control_->runningState()) {
    case GoPxLSdk::GoSystem::State::Ready: return false;
    case GoPxLSdk::GoSystem::State::Running: return true;
    default: throw std::runtime_error("Gocator is neither Ready nor Running.");
    }
}

GocatorStartResult GocatorConnection::tryStart()
{
    std::lock_guard<std::mutex> lock(startMutex_);
    GoPxLSdk::GoSystem::State state = GoPxLSdk::GoSystem::State::Ready;
    try {
        state = control_->runningState();
    } catch (const GoPxLSdk::GoChannelError& error) {
        lastOperationError_ = error.what();
        return GocatorStartResult::StateUnknown;
    } catch (const std::exception& error) {
        lastOperationError_ = error.what();
        return GocatorStartResult::Refused;
    } catch (...) {
        lastOperationError_ = "Gocator state query failed with an unknown error.";
        return GocatorStartResult::Refused;
    }
    if (startOwnership_.stopUncertain()) {
        if (state != GoPxLSdk::GoSystem::State::Ready) {
            lastOperationError_ = "A previous Gocator stop was not confirmed.";
            return GocatorStartResult::PriorStopUncertain;
        }
        startOwnership_.reset();
    } else if (state == GoPxLSdk::GoSystem::State::Ready) {
        startOwnership_.reset();
    }
    lastOperationError_.clear();
    if (state == GoPxLSdk::GoSystem::State::Running)
        return GocatorStartResult::AlreadyRunning;
    if (state != GoPxLSdk::GoSystem::State::Ready) {
        lastOperationError_ = "Gocator is not Ready to start.";
        return GocatorStartResult::StateUnknown;
    }
    try {
        control_->start();
        startOwnership_.markStarted();
        return GocatorStartResult::Started;
    } catch (const GoPxLSdk::GoChannelError& error) {
        lastOperationError_ = error.what();
        return GocatorStartResult::StartUncertain;
    } catch (const std::exception& error) {
        lastOperationError_ = error.what();
        return GocatorStartResult::Refused;
    } catch (...) {
        lastOperationError_ = "Gocator start failed with an unknown error.";
        return GocatorStartResult::Refused;
    }
}

void GocatorConnection::start()
{
    const auto result = tryStart();
    if (result != GocatorStartResult::Started && result != GocatorStartResult::AlreadyRunning)
        throw std::runtime_error(lastOperationError().empty()
            ? "Gocator start could not be confirmed." : lastOperationError());
}

void GocatorConnection::attachToRunning()
{
    std::lock_guard<std::mutex> lock(startMutex_);
    if (startOwnership_.stopUncertain())
        throw std::runtime_error("A previous Gocator stop was not confirmed.");
    try {
        if (control_->runningState() != GoPxLSdk::GoSystem::State::Running)
            throw std::runtime_error("Gocator replay stopped before attachment completed.");
    } catch (const GoPxLSdk::GoChannelError& error) {
        throw std::runtime_error(error.what());
    }
}

GocatorStopResult GocatorConnection::tryStop()
{
    std::lock_guard<std::mutex> lock(startMutex_);
    if (startOwnership_.stopUncertain()) return GocatorStopResult::Uncertain;
    lastOperationError_.clear();
    if (!startOwnership_.started()) return GocatorStopResult::NotOwned;
    try {
        control_->stop();
        (void)startOwnership_.end();
        return GocatorStopResult::Stopped;
    } catch (const GoPxLSdk::GoChannelError& error) {
        lastOperationError_ = error.what();
        startOwnership_.markStopUncertain();
        return GocatorStopResult::Uncertain;
    } catch (const std::exception& error) {
        lastOperationError_ = error.what();
        return GocatorStopResult::Refused;
    } catch (...) {
        lastOperationError_ = "Gocator stop failed with an unknown error.";
        return GocatorStopResult::Refused;
    }
}

void GocatorConnection::stop()
{
    const auto result = tryStop();
    if (result == GocatorStopResult::Refused || result == GocatorStopResult::Uncertain)
        throw std::runtime_error(lastOperationError());
}

void GocatorConnection::stopNoThrow() noexcept
{
    try { stop(); } catch (...) {}
}

GocatorStopResult GocatorConnection::stopIfRunning()
{
    std::lock_guard<std::mutex> lock(startMutex_);
    if (startOwnership_.stopUncertain()) return GocatorStopResult::Uncertain;
    lastOperationError_.clear();
    try {
        if (control_->runningState() != GoPxLSdk::GoSystem::State::Running)
            return GocatorStopResult::NotRunning;
    } catch (const GoPxLSdk::GoChannelError& error) {
        lastOperationError_ = error.what();
        return GocatorStopResult::StateUnknown;
    } catch (const std::exception& error) {
        lastOperationError_ = error.what();
        return GocatorStopResult::Refused;
    } catch (...) {
        lastOperationError_ = "Gocator state query failed with an unknown error.";
        return GocatorStopResult::Refused;
    }
    try {
        control_->stop();
        (void)startOwnership_.end();
        return GocatorStopResult::Stopped;
    } catch (const GoPxLSdk::GoChannelError& error) {
        lastOperationError_ = error.what();
        if (startOwnership_.started()) startOwnership_.markStopUncertain();
        return GocatorStopResult::Uncertain;
    } catch (const std::exception& error) {
        lastOperationError_ = error.what();
        return GocatorStopResult::Refused;
    } catch (...) {
        lastOperationError_ = "Gocator stop failed with an unknown error.";
        return GocatorStopResult::Refused;
    }
}

std::string GocatorConnection::lastOperationError() const
{
    std::lock_guard<std::mutex> lock(startMutex_);
    return lastOperationError_;
}

std::uint16_t GocatorConnection::controlPort() const noexcept
{
    return config_.controlPort;
}

std::uint16_t GocatorConnection::gdpPort()
{
    return system().GdpPort();
}

int GocatorConnection::commandTimeoutMs() const noexcept
{
    return config_.commandTimeoutMs;
}

GoPxLSdk::GoSystem& GocatorConnection::system()
{
    if (!system_)
    {
        throw std::logic_error("Gocator system is not initialized");
    }

    return *system_;
}

const GoPxLSdk::GoSystem& GocatorConnection::system() const
{
    if (!system_)
    {
        throw std::logic_error("Gocator system is not initialized");
    }

    return *system_;
}

} // namespace gocator
