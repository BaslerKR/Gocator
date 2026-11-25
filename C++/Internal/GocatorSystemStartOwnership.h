#pragma once

namespace gocator {

class GocatorSystemStartOwnership final
{
public:
    void markStarted() noexcept
    {
        _started = true;
        _stopUncertain = false;
    }

    void markStopUncertain() noexcept { _stopUncertain = true; }

    [[nodiscard]] bool end() noexcept
    {
        const bool started = _started;
        _started = false;
        _stopUncertain = false;
        return started;
    }

    void reset() noexcept
    {
        _started = false;
        _stopUncertain = false;
    }

    [[nodiscard]] bool started() const noexcept { return _started; }
    [[nodiscard]] bool stopUncertain() const noexcept { return _stopUncertain; }

private:
    bool _started = false;
    bool _stopUncertain = false;
};

}
