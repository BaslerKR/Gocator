#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "GocatorTypes.h"
#include "Internal/GocatorSystemControl.h"
#include "Internal/GocatorSystemStartOwnership.h"

namespace GoPxLSdk
{
class GoSystem;
}

namespace gocator
{

enum class GocatorStartResult {
    Started, AlreadyRunning, Refused, StateUnknown, StartUncertain, PriorStopUncertain
};
enum class GocatorStopResult {
    Stopped, NotOwned, Refused, Uncertain
};

class GocatorConnection
{
public:
    explicit GocatorConnection(GocatorConnectionConfig config);
    GocatorConnection(GocatorConnectionConfig config, std::unique_ptr<GocatorSystemControl> control);
    ~GocatorConnection();

    GocatorConnection(const GocatorConnection&) = delete;
    GocatorConnection& operator=(const GocatorConnection&) = delete;

    const GocatorConnectionConfig& config() const noexcept;

    void connect();
    void disconnect() noexcept;
    bool isConnected();
    bool isRunning();

    void start();
    GocatorStartResult tryStart();
    void attachToRunning();
    void stop();
    GocatorStopResult tryStop();
    void stopNoThrow() noexcept;
    std::string lastOperationError() const;

    std::uint16_t controlPort() const noexcept;
    std::uint16_t gdpPort();
    int commandTimeoutMs() const noexcept;

    GoPxLSdk::GoSystem& system();
    const GoPxLSdk::GoSystem& system() const;

private:
    GocatorConnectionConfig config_;
    std::unique_ptr<GoPxLSdk::GoSystem> system_;
    std::unique_ptr<GocatorSystemControl> control_;
    mutable std::mutex startMutex_;
    GocatorSystemStartOwnership startOwnership_;
    std::string lastOperationError_;
};

} // namespace gocator
