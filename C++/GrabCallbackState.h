#pragma once

#include <atomic>
#include <cstdint>

/**
 * @brief Counts delivered frames and failed grab callbacks for one acquisition run.
 *
 * A frame is delivered when at least one registered callback returned normally.
 * A callback exception increments the failure count without making the same
 * frame count as delivered. The owner calls one method set from its grab thread;
 * diagnostics may read the counters from another thread.
 */
class GrabCallbackState
{
public:
    void reset() noexcept
    {
        _deliveredFrames.store(0, std::memory_order_relaxed);
        _failedCallbacks.store(0, std::memory_order_relaxed);
        _frameDelivered.store(false, std::memory_order_relaxed);
    }

    void beginFrame() noexcept
    {
        _frameDelivered.store(false, std::memory_order_relaxed);
    }

    void noteSuccess() noexcept
    {
        _frameDelivered.store(true, std::memory_order_relaxed);
    }

    void noteFailure() noexcept
    {
        _failedCallbacks.fetch_add(1, std::memory_order_relaxed);
    }

    bool finishFrame() noexcept
    {
        if (!_frameDelivered.load(std::memory_order_relaxed))
        {
            return false;
        }
        _deliveredFrames.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] std::uint64_t deliveredFrames() const noexcept
    {
        return _deliveredFrames.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t failedCallbacks() const noexcept
    {
        return _failedCallbacks.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uint64_t> _deliveredFrames{0};
    std::atomic<std::uint64_t> _failedCallbacks{0};
    std::atomic<bool> _frameDelivered{false};
};
